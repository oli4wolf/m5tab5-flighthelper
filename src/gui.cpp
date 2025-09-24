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
M5Canvas tileCanvas(&M5.Display); // Declare M5Canvas globally for individual tile drawing
M5Canvas screenBufferCanvas(&M5.Display); // Declare M5Canvas globally for full screen buffer


// Helper function to draw a single tile, handling cache and SD loading
void drawTile(M5Canvas& canvas, int tileX, int tileY, int zoom, const char* filePath) {
  File file = SD_MMC.open(filePath);
  if (!file) {
    ESP_LOGE("SD_CARD", "Failed to open file for reading: %s", filePath);
    return;
  }
  canvas.drawJpgFile(SD_MMC, filePath, 0, 0);
  file.close();
  ESP_LOGI("drawTile", "Loaded and drew Jpeg from SD: %s", filePath);
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


  // SCREEN_BUFFER_TILE_DIMENSION x SCREEN_BUFFER_TILE_DIMENSION conceptual tile array to store file paths
  char tilePaths[SCREEN_BUFFER_TILE_DIMENSION][SCREEN_BUFFER_TILE_DIMENSION][TILE_PATH_MAX_LENGTH];

  tileCanvas.createSprite(TILE_SIZE, TILE_SIZE); // Initialize M5Canvas for individual tiles
  screenBufferCanvas.createSprite(SCREEN_BUFFER_TILE_DIMENSION * TILE_SIZE, SCREEN_BUFFER_TILE_DIMENSION * TILE_SIZE); // Initialize M5Canvas for full screen buffer

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

    if(!currentValid)
    {
      // Todo Use Testdata
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

      // Calculate the top-left coordinates for the drawing grid
      // The central tile (globalTileX, globalTileY) will be at index [DRAW_GRID_CENTER_OFFSET][DRAW_GRID_CENTER_OFFSET] in the DRAW_GRID_DIMENSION x DRAW_GRID_DIMENSION array
      int conceptualGridStartX = currentTileX - DRAW_GRID_CENTER_OFFSET;
      int conceptualGridStartY = currentTileY - DRAW_GRID_CENTER_OFFSET;

      // Populate the TILE_GRID_DIMENSION x TILE_GRID_DIMENSION tilePaths array
      for (int y = 0; y < SCREEN_BUFFER_TILE_DIMENSION; ++y) {
        for (int x = 0; x < SCREEN_BUFFER_TILE_DIMENSION; ++x) {
          int tileToLoadX = currentTileX - SCREEN_BUFFER_CENTER_OFFSET + x;
          int tileToLoadY = currentTileY - SCREEN_BUFFER_CENTER_OFFSET + y;
          sprintf(tilePaths[y][x], "/map/%d/%d/%d.jpeg", globalTileZ, tileToLoadX, tileToLoadY);
        }
      }


      // Calculate the drawing origin so that the GPS coordinate (pixelOffsetX, pixelOffsetY within its tile)
      // is centered on the screen.
      // The tile containing the GPS coordinate is (currentTileX, currentTileY).
      // The top-left corner of this tile should be drawn at:
      // (screenBufferCanvas.width() / 2 - pixelOffsetX, screenBufferCanvas.height() / 2 - pixelOffsetY)
      int drawOriginX = (screenBufferCanvas.width() / 2) - pixelOffsetX;
      int drawOriginY = (screenBufferCanvas.height() / 2) - pixelOffsetY;

      screenBufferCanvas.clear(TFT_BLACK); // Clear the screen buffer
      ESP_LOGI("drawImageMatrixTask", "Performing full redraw.");
      // Draw all DRAW_GRID_DIMENSION * DRAW_GRID_DIMENSION tiles to the screen buffer
      for (int yOffset = -DRAW_GRID_CENTER_OFFSET; yOffset <= DRAW_GRID_CENTER_OFFSET; ++yOffset) {
        for (int xOffset = -DRAW_GRID_CENTER_OFFSET; xOffset <= DRAW_GRID_CENTER_OFFSET; ++xOffset) {
          int currentDrawX = drawOriginX + (xOffset * TILE_SIZE);
          int currentDrawY = drawOriginY + (yOffset * TILE_SIZE);
          tileCanvas.clear(); // Clear the individual tile canvas
          drawTile(tileCanvas, conceptualGridStartX + xOffset, conceptualGridStartY + yOffset,
                   globalTileZ, tilePaths[yOffset + SCREEN_BUFFER_CENTER_OFFSET][xOffset + SCREEN_BUFFER_CENTER_OFFSET]);
          tileCanvas.pushSprite(&screenBufferCanvas, currentDrawX, currentDrawY); // Draw tile to screen buffer
        }
      }

      // Draw a red point at the GPS fix location (center of the screen) on the screen buffer
      int centerX = screenBufferCanvas.width() / 2;
      int centerY = screenBufferCanvas.height() / 2;

      // Draw arrow head (triangle)
      screenBufferCanvas.fillTriangle(
          centerX, centerY - ARROW_HEAD_LENGTH / 2,
          centerX - ARROW_HEAD_WIDTH / 2, centerY + ARROW_HEAD_LENGTH / 2,
          centerX + ARROW_HEAD_WIDTH / 2, centerY + ARROW_HEAD_LENGTH / 2,
          TFT_RED
      );

      // Draw arrow shaft (line)
      screenBufferCanvas.fillRect(
          centerX - 1, // Thicker line
          centerY + ARROW_HEAD_LENGTH / 2,
          3, // Width of the shaft
          ARROW_SHAFT_LENGTH,
          TFT_RED
      );
      
      // Push the entire screen buffer to the M5.Display once
      screenBufferCanvas.pushSprite(-152, 128);

      vTaskDelay(pdMS_TO_TICKS(DRAW_IMAGE_TASK_DELAY_MS)); // Display the image for DRAW_IMAGE_TASK_DELAY_MS milliseconds
    }
  }
}