// This file will contain the implementation of GUI-related functions.
#include <Arduino.h>
#include "FS.h"        // SD Card ESP32
#include "SD_MMC.h"    // SD Card ESP32
#include <M5Unified.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
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

void drawImageMatrixTask(void *pvParameters)
{
  int currentTileX = 0;
  int currentTileY = 0;
  int currentTileZ = 0;
  double currentLatitude = 0;
  double currentLongitude = 0;
  double currentSpeed = 0;
  bool currentValid = false;

  // 5x5 conceptual tile array to store file paths
  char tilePaths[5][5][128];

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

      M5.Display.clear(TFT_BLACK);

      M5.Display.clear(TFT_BLACK);

      // Calculate the drawing origin so that the GPS coordinate (pixelOffsetX, pixelOffsetY within its tile)
      // is centered on the screen.
      // The tile containing the GPS coordinate is (currentTileX, currentTileY).
      // The top-left corner of this tile should be drawn at:
      // (M5.Display.width() / 2 - pixelOffsetX, M5.Display.height() / 2 - pixelOffsetY)
      int drawOriginX = (M5.Display.width() / 2) - pixelOffsetX;
      int drawOriginY = (M5.Display.height() / 2) - pixelOffsetY;

      // Iterate to draw a 5x5 matrix of tiles
      for (int yOffset = -2; yOffset <= 2; ++yOffset)
      {
        for (int xOffset = -2; xOffset <= 2; ++xOffset)
        {
          // Map yOffset -2, -1, 0, 1, 2 to array indices 0, 1, 2, 3, 4
          // Map xOffset -2, -1, 0, 1, 2 to array indices 0, 1, 2, 3, 4
          const char* filePath = tilePaths[yOffset + 2][xOffset + 2];
          ESP_LOGI("drawImageMatrixTask", "Attempting to draw jpeg from path: %s at offset (%d, %d)", filePath, xOffset, yOffset);

          int drawX = drawOriginX + (xOffset * TILE_SIZE);
          int drawY = drawOriginY + (yOffset * TILE_SIZE);

          if (!SD_MMC.begin()) {
            ESP_LOGE("SD_CARD", "SD Card Mount Failed in drawImageMatrixTask");
            M5.Display.printf("SD Card Mount Failed\n");
            // Handle error, maybe skip drawing this tile
            continue;
          }

          File file = SD_MMC.open(filePath);
          if (!file) {
            ESP_LOGE("SD_CARD", "Failed to open file for reading: %s", filePath);
            // Handle error, maybe draw a placeholder or skip
            file.close(); // Ensure file handle is closed even if open failed
            continue;
          }

          M5.Display.drawJpgFile(SD_MMC, filePath, drawX, drawY);
          file.close();
          ESP_LOGI("SD_CARD", "Successfully drew Jpeg: %s at (%d, %d)", filePath, drawX, drawY);
        }
      }
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