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
extern const int TILE_SIZE;
// Global LRU Cache instance (1MB)
LRUCache tileCache(1 * 1024 * 1024); // 1MB cache
M5Canvas tileCanvas(&M5.Display); // Declare M5Canvas globally

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

  // 5x5 conceptual tile array to store file paths
  char tilePaths[5][5][128];

  tileCanvas.createSprite(TILE_SIZE, TILE_SIZE); // Initialize M5Canvas

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
      // The central tile (globalTileX, globalTileY) will be at index [2][2] in the 5x5 array
      int conceptualGridStartX = currentTileX - 2;
      int conceptualGridStartY = currentTileY - 2;

      // Populate the 5x5 tilePaths array
      for (int y = 0; y < 5; ++y) {
        for (int x = 0; x < 5; ++x) {
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
        M5.Display.clear(TFT_BLACK); // Clear only if full redraw is needed
        ESP_LOGI("drawImageMatrixTask", "Performing full redraw.");
      }

      if (redrawNeeded) {
        M5.Display.clear(TFT_BLACK); // Clear only if full redraw is needed
        ESP_LOGI("drawImageMatrixTask", "Performing full redraw.");
        // Draw all 25 tiles
        for (int yOffset = -2; yOffset <= 2; ++yOffset) {
          for (int xOffset = -2; xOffset <= 2; ++xOffset) {
            int currentDrawX = drawOriginX + (xOffset * TILE_SIZE);
            int currentDrawY = drawOriginY + (yOffset * TILE_SIZE);
            tileCanvas.clear(); // Clear the canvas before drawing a new tile
            drawTile(tileCanvas, conceptualGridStartX + xOffset, conceptualGridStartY + yOffset,
                     globalTileZ, tilePaths[yOffset + 2][xOffset + 2]);
            tileCanvas.pushSprite(currentDrawX, currentDrawY);
          }
        }
      } else if (deltaX != 0 || deltaY != 0) {
        // Only draw newly exposed tiles after scrolling
        // Determine which rows/columns are newly exposed
        if (deltaX > 0) { // Scrolled right, new tiles on left
          for (int yOffset = -2; yOffset <= 2; ++yOffset) {
            int currentDrawX = drawOriginX + (-2 * TILE_SIZE);
            int currentDrawY = drawOriginY + (yOffset * TILE_SIZE);
            tileCanvas.clear(); // Clear the canvas before drawing a new tile
            drawTile(tileCanvas, conceptualGridStartX - 2, conceptualGridStartY + yOffset,
                     globalTileZ, tilePaths[yOffset + 2][0]);
            tileCanvas.pushSprite(currentDrawX, currentDrawY);
          }
        } else if (deltaX < 0) { // Scrolled left, new tiles on right
          for (int yOffset = -2; yOffset <= 2; ++yOffset) {
            int currentDrawX = drawOriginX + (2 * TILE_SIZE);
            int currentDrawY = drawOriginY + (yOffset * TILE_SIZE);
            tileCanvas.clear(); // Clear the canvas before drawing a new tile
            drawTile(tileCanvas, conceptualGridStartX + 2, conceptualGridStartY + yOffset,
                     globalTileZ, tilePaths[yOffset + 2][4]);
            tileCanvas.pushSprite(currentDrawX, currentDrawY);
          }
        }

        if (deltaY > 0) { // Scrolled down, new tiles on top
          for (int xOffset = -2; xOffset <= 2; ++xOffset) {
            int currentDrawX = drawOriginX + (xOffset * TILE_SIZE);
            int currentDrawY = drawOriginY + (-2 * TILE_SIZE);
            tileCanvas.clear(); // Clear the canvas before drawing a new tile
            drawTile(tileCanvas, conceptualGridStartX + xOffset, conceptualGridStartY - 2,
                     globalTileZ, tilePaths[0][xOffset + 2]);
            tileCanvas.pushSprite(currentDrawX, currentDrawY);
          }
        } else if (deltaY < 0) { // Scrolled up, new tiles on bottom
          for (int xOffset = -2; xOffset <= 2; ++xOffset) {
            int currentDrawX = drawOriginX + (xOffset * TILE_SIZE);
            int currentDrawY = drawOriginY + (2 * TILE_SIZE);
            tileCanvas.clear(); // Clear the canvas before drawing a new tile
            drawTile(tileCanvas, conceptualGridStartX + xOffset, conceptualGridStartY + 2,
                     globalTileZ, tilePaths[4][xOffset + 2]);
            tileCanvas.pushSprite(currentDrawX, currentDrawY);
          }
        }
      }
      // Update previous drawing origin
      prevDrawOriginX = drawOriginX;
      prevDrawOriginY = drawOriginY;
      // Draw a red point at the GPS fix location (center of the screen)
      M5.Display.fillCircle(M5.Display.width() / 2, M5.Display.height() / 2, 5, TFT_RED);
      vTaskDelay(pdMS_TO_TICKS(2000)); // Display the image for 2 seconds
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