// This file will contain the implementation of GUI-related functions.
#include <Arduino.h>
#include "FS.h"        // SD Card ESP32
#include "SD_MMC.h"    // SD Card ESP32
#include <M5Unified.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <limits> // For INT_MAX
#include "gps_task.h"
#include "tile_calculator.h"
#include "gui.h" // Include its own header
#include "config.h" // Include configuration constants

// External global variables from main.cpp
// Function to draw a Jpeg image from SD card
extern SemaphoreHandle_t xGPSMutex;
extern double globalLatitude;
extern double globalLongitude;
extern double globalSpeed;
extern bool globalValid;
extern SemaphoreHandle_t xPositionMutex;
extern int globalTileX;
extern int globalTileY;
extern int globalTileZ;
// Global LRU Cache instance (1MB)
LRUCache tileCache(TILE_CACHE_SIZE_BYTES); // 1MB cache
M5Canvas tileCanvas(&M5.Display); // Declare M5Canvas globally for individual tile drawing
M5Canvas screenBufferCanvas(&M5.Display); // Declare M5Canvas globally for full screen buffer

// LRUCache class implementation
LRUCache::LRUCache(size_t maxBytes) : currentSize(0), maxSize(maxBytes) {
    cacheMutex = xSemaphoreCreateMutex();
    if (cacheMutex == NULL) {
        ESP_LOGE("LRUCache", "Failed to create cache mutex");
        // Handle error appropriately, e.g., halt or throw exception
    }
    ESP_LOGI("LRUCache", "Cache initialized with max size: %u bytes", maxSize);
}

LRUCache::~LRUCache() {
    if (cacheMutex != NULL) {
        vSemaphoreDelete(cacheMutex);
    }
}

void LRUCache::evict() {
    if (cacheList.empty()) {
        return;
    }
    // Evict the least recently used item (at the back of the list)
    TileCacheEntry& lruEntry = cacheList.back();
    currentSize -= lruEntry.data.size();
    cacheMap.erase(lruEntry.path);
    cacheList.pop_back();
    ESP_LOGI("LRUCache", "Evicted tile: %s. Current size: %u bytes", lruEntry.path.c_str(), currentSize);
}

bool LRUCache::get(const std::string& key, std::vector<uint8_t>& outData) {
    if (xSemaphoreTake(cacheMutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE("LRUCache", "Failed to take cache mutex in get()");
        return false;
    }

    auto it = cacheMap.find(key);
    if (it == cacheMap.end()) {
        xSemaphoreGive(cacheMutex);
        return false; // Not found
    }

    // Move the found item to the front (most recently used)
    cacheList.splice(cacheList.begin(), cacheList, it->second);
    outData = it->second->data; // Copy data
    ESP_LOGI("LRUCache", "Cache hit for tile: %s. Current size: %u bytes", key.c_str(), currentSize);
    xSemaphoreGive(cacheMutex);
    return true;
}

void LRUCache::put(const std::string& key, const std::vector<uint8_t>& data, int z, int x, int y) {
    if (xSemaphoreTake(cacheMutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE("LRUCache", "Failed to take cache mutex in put()");
        return;
    }

    // Check if already in cache (update its position)
    auto it = cacheMap.find(key);
    if (it != cacheMap.end()) {
        // Update data and move to front
        it->second->data = data;
        cacheList.splice(cacheList.begin(), cacheList, it->second);
        ESP_LOGI("LRUCache", "Cache updated for tile: %s. Current size: %u bytes", key.c_str(), currentSize);
        xSemaphoreGive(cacheMutex);
        return;
    }

    // Add new item
    size_t itemSize = data.size();
    while (currentSize + itemSize > maxSize && !cacheList.empty()) {
        evict();
    }

    if (currentSize + itemSize > maxSize) {
        ESP_LOGW("LRUCache", "Item too large for cache, even after eviction: %s (size: %u bytes)", key.c_str(), itemSize);
        xSemaphoreGive(cacheMutex);
        return;
    }

    cacheList.emplace_front(key, data, z, x, y);
    cacheMap[key] = cacheList.begin();
    currentSize += itemSize;
    ESP_LOGI("LRUCache", "Added tile to cache: %s (size: %u bytes). Current size: %u bytes", key.c_str(), itemSize, currentSize);
    xSemaphoreGive(cacheMutex);
}

bool LRUCache::contains(const std::string& key) {
    if (xSemaphoreTake(cacheMutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE("LRUCache", "Failed to take cache mutex in contains()");
        return false;
    }
    bool found = cacheMap.count(key) > 0;
    xSemaphoreGive(cacheMutex);
    return found;
}

// Helper function to draw a single tile, handling cache and SD loading
void drawTile(M5Canvas& canvas, int tileX, int tileY, int zoom, const char* filePath) {
  std::vector<uint8_t> cachedData;
  if (tileCache.get(filePath, cachedData)) {
    canvas.drawJpg(cachedData.data(), cachedData.size(), 0, 0);
    ESP_LOGI("drawTile", "Drew Jpeg from cache: %s", filePath);
  } else {
    File file = SD_MMC.open(filePath);
    if (!file) {
      ESP_LOGE("SD_CARD", "Failed to open file for reading: %s", filePath);
      return;
    }
    size_t fileSize = file.size();
    cachedData.resize(fileSize);
    if (file.read(cachedData.data(), fileSize) != fileSize) {
      ESP_LOGE("SD_CARD", "Failed to read entire file: %s", filePath);
      file.close();
      return;
    }
    file.close();
    tileCache.put(filePath, cachedData, zoom, tileX, tileY);
    canvas.drawJpg(cachedData.data(), cachedData.size(), 0, 0);
    ESP_LOGI("drawTile", "Loaded and drew Jpeg from SD: %s", filePath);
  }
}

void drawImageMatrixTask(void *pvParameters)
{
  int currentTileX = 0;
  int currentTileY = 0;
  int currentTileZ = 0;
  double currentLatitude = 0;
  double currentLongitude = 0;
  double currentSpeed = 0;
  bool currentValid = false;

  // Variables to track previous drawing origin for optimized redrawing
  int prevDrawOriginX = INT_MAX;
  int prevDrawOriginY = INT_MAX;

  // TILE_GRID_DIMENSION x TILE_GRID_DIMENSION conceptual tile array to store file paths
  char tilePaths[TILE_GRID_DIMENSION][TILE_GRID_DIMENSION][TILE_PATH_MAX_LENGTH];

  tileCanvas.createSprite(TILE_SIZE, TILE_SIZE); // Initialize M5Canvas for individual tiles
  screenBufferCanvas.createSprite(M5.Display.width(), M5.Display.height()); // Initialize M5Canvas for full screen buffer

  while (true)
  {
    // Acquire GPS data
    if (xSemaphoreTake(xGPSMutex, portMAX_DELAY) == pdTRUE)
    {
      currentLatitude = globalLatitude;
      currentLongitude = globalLongitude;
      currentSpeed = globalSpeed;
      currentValid = globalValid;
      xSemaphoreGive(xGPSMutex);
    }

    if (currentValid)
    {
      // Calculate tile coordinates
      currentTileZ = calculateZoomLevel(currentSpeed, M5.Display.width(), M5.Display.height());
      latLngToTile(currentLatitude, currentLongitude, currentTileZ, &currentTileX, &currentTileY);
      int pixelOffsetX = 0;
      int pixelOffsetY = 0;
      latLngToPixelOffset(currentLatitude, currentLongitude, currentTileZ, &pixelOffsetX, &pixelOffsetY);

      // Update global tile coordinates
      if (xSemaphoreTake(xPositionMutex, portMAX_DELAY) == pdTRUE)
      {
        globalTileX = currentTileX;
        globalTileY = currentTileY;
        globalTileZ = currentTileZ;
        ESP_LOGI("TileCalc", "Task Tile X: %d, Tile Y: %d, Zoom: %d", globalTileX, globalTileY, globalTileZ);
        xSemaphoreGive(xPositionMutex);
      }

      // Calculate the top-left coordinates for the 5x5 conceptual grid
      // The central tile (globalTileX, globalTileY) will be at index [TILE_GRID_CENTER_OFFSET][TILE_GRID_CENTER_OFFSET] in the TILE_GRID_DIMENSION x TILE_GRID_DIMENSION array
      int conceptualGridStartX = currentTileX - TILE_GRID_CENTER_OFFSET;
      int conceptualGridStartY = currentTileY - TILE_GRID_CENTER_OFFSET;

      // Populate the TILE_GRID_DIMENSION x TILE_GRID_DIMENSION tilePaths array
      for (int y = 0; y < TILE_GRID_DIMENSION; ++y) {
        for (int x = 0; x < TILE_GRID_DIMENSION; ++x) {
          int tileToLoadX = conceptualGridStartX + x;
          int tileToLoadY = conceptualGridStartY + y;
          sprintf(tilePaths[y][x], "/map/%d/%d/%d.jpeg", globalTileZ, tileToLoadX, tileToLoadY);
        }
      }


      // Calculate the drawing origin so that the GPS coordinate (pixelOffsetX, pixelOffsetY within its tile)
      // is centered on the screen.
      // The tile containing the GPS coordinate is (currentTileX, currentTileY).
      // The top-left corner of this tile should be drawn at:
      // (M5.Display.width() / 2 - pixelOffsetX, M5.Display.height() / 2 - pixelOffsetY)
      int drawOriginX = (M5.Display.width() / 2) - pixelOffsetX;
      int drawOriginY = (M5.Display.height() / 2) - pixelOffsetY;

      bool redrawNeeded = false;
      int deltaX = 0;
      int deltaY = 0;

      if (prevDrawOriginX == INT_MAX || prevDrawOriginY == INT_MAX) {
        redrawNeeded = true; // First run, force full redraw
        ESP_LOGI("drawImageMatrixTask", "First run, forcing full redraw.");
      } else {
        deltaX = drawOriginX - prevDrawOriginX;
        deltaY = drawOriginY - prevDrawOriginY;

        // Check if the central tile coordinates have changed significantly
        // This is a more robust check than just pixel offset for full redraw
        if (abs(deltaX) >= TILE_SIZE || abs(deltaY) >= TILE_SIZE) {
          redrawNeeded = true;
          ESP_LOGI("drawImageMatrixTask", "Significant tile shift detected (deltaX: %d, deltaY: %d), forcing full redraw.", deltaX, deltaY);
        } else if (deltaX != 0 || deltaY != 0) {
          // Small shift, can scroll
          M5.Display.scroll(deltaX, deltaY);
          ESP_LOGI("drawImageMatrixTask", "Scrolling display by (%d, %d).", deltaX, deltaY);
        }
      }

      if (redrawNeeded) {
        screenBufferCanvas.clear(TFT_BLACK); // Clear the screen buffer
        ESP_LOGI("drawImageMatrixTask", "Performing full redraw.");
        // Draw all TILE_GRID_DIMENSION * TILE_GRID_DIMENSION tiles to the screen buffer
        for (int yOffset = -TILE_GRID_CENTER_OFFSET; yOffset <= TILE_GRID_CENTER_OFFSET; ++yOffset) {
          for (int xOffset = -TILE_GRID_CENTER_OFFSET; xOffset <= TILE_GRID_CENTER_OFFSET; ++xOffset) {
            int currentDrawX = drawOriginX + (xOffset * TILE_SIZE);
            int currentDrawY = drawOriginY + (yOffset * TILE_SIZE);
            tileCanvas.clear(); // Clear the individual tile canvas
            drawTile(tileCanvas, conceptualGridStartX + xOffset, conceptualGridStartY + yOffset,
                     globalTileZ, tilePaths[yOffset + TILE_GRID_CENTER_OFFSET][xOffset + TILE_GRID_CENTER_OFFSET]);
            tileCanvas.pushSprite(&screenBufferCanvas, currentDrawX, currentDrawY); // Draw tile to screen buffer
          }
        }
      } else if (deltaX != 0 || deltaY != 0) {
        // Small shift, can scroll the screen buffer
        screenBufferCanvas.scroll(deltaX, deltaY);
        ESP_LOGI("drawImageMatrixTask", "Scrolling screen buffer by (%d, %d).", deltaX, deltaY);

        // Only draw newly exposed tiles after scrolling onto the screen buffer
        if (deltaX > 0) { // Scrolled right, new tiles on left
          for (int yOffset = -TILE_GRID_CENTER_OFFSET; yOffset <= TILE_GRID_CENTER_OFFSET; ++yOffset) {
            int currentDrawX = drawOriginX + (-TILE_GRID_CENTER_OFFSET * TILE_SIZE);
            int currentDrawY = drawOriginY + (yOffset * TILE_SIZE);
            tileCanvas.clear();
            drawTile(tileCanvas, conceptualGridStartX - TILE_GRID_CENTER_OFFSET, conceptualGridStartY + yOffset,
                     globalTileZ, tilePaths[yOffset + TILE_GRID_CENTER_OFFSET][0]);
            tileCanvas.pushSprite(&screenBufferCanvas, currentDrawX, currentDrawY);
          }
        } else if (deltaX < 0) { // Scrolled left, new tiles on right
          for (int yOffset = -TILE_GRID_CENTER_OFFSET; yOffset <= TILE_GRID_CENTER_OFFSET; ++yOffset) {
            int currentDrawX = drawOriginX + (TILE_GRID_CENTER_OFFSET * TILE_SIZE);
            int currentDrawY = drawOriginY + (yOffset * TILE_SIZE);
            tileCanvas.clear();
            drawTile(tileCanvas, conceptualGridStartX + TILE_GRID_CENTER_OFFSET, conceptualGridStartY + yOffset,
                     globalTileZ, tilePaths[yOffset + TILE_GRID_CENTER_OFFSET][TILE_GRID_DIMENSION - 1]);
            tileCanvas.pushSprite(&screenBufferCanvas, currentDrawX, currentDrawY);
          }
        }

        if (deltaY > 0) { // Scrolled down, new tiles on top
          for (int xOffset = -TILE_GRID_CENTER_OFFSET; xOffset <= TILE_GRID_CENTER_OFFSET; ++xOffset) {
            int currentDrawX = drawOriginX + (xOffset * TILE_SIZE);
            int currentDrawY = drawOriginY + (-TILE_GRID_CENTER_OFFSET * TILE_SIZE);
            tileCanvas.clear();
            drawTile(tileCanvas, conceptualGridStartX + xOffset, conceptualGridStartY - TILE_GRID_CENTER_OFFSET,
                     globalTileZ, tilePaths[0][xOffset + TILE_GRID_CENTER_OFFSET]);
            tileCanvas.pushSprite(&screenBufferCanvas, currentDrawX, currentDrawY);
          }
        } else if (deltaY < 0) { // Scrolled up, new tiles on bottom
          for (int xOffset = -TILE_GRID_CENTER_OFFSET; xOffset <= TILE_GRID_CENTER_OFFSET; ++xOffset) {
            int currentDrawX = drawOriginX + (xOffset * TILE_SIZE);
            int currentDrawY = drawOriginY + (TILE_GRID_CENTER_OFFSET * TILE_SIZE);
            tileCanvas.clear();
            drawTile(tileCanvas, conceptualGridStartX + xOffset, conceptualGridStartY + TILE_GRID_CENTER_OFFSET,
                     globalTileZ, tilePaths[TILE_GRID_DIMENSION - 1][xOffset + TILE_GRID_CENTER_OFFSET]);
            tileCanvas.pushSprite(&screenBufferCanvas, currentDrawX, currentDrawY);
          }
        }
      }
      // Update previous drawing origin
      prevDrawOriginX = drawOriginX;
      prevDrawOriginY = drawOriginY;

      // Draw a red point at the GPS fix location (center of the screen) on the screen buffer
      screenBufferCanvas.fillCircle(M5.Display.width() / 2, M5.Display.height() / 2, GPS_FIX_CIRCLE_RADIUS, TFT_RED);
      
      // Push the entire screen buffer to the M5.Display once
      screenBufferCanvas.pushSprite(0, 0);

      vTaskDelay(pdMS_TO_TICKS(DRAW_IMAGE_TASK_DELAY_MS)); // Display the image for DRAW_IMAGE_TASK_DELAY_MS milliseconds
    }
    else
    {
      M5.Display.clear(TFT_BLACK);
      M5.Display.setCursor(0, 0);
      M5.Display.printf("Waiting for GPS fix...\n");
      vTaskDelay(pdMS_TO_TICKS(1000)); // Wait for 1 second before retrying
    }
  }
}