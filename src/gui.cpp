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

      // Update global tile coordinates
      if (xSemaphoreTake(xPositionMutex, portMAX_DELAY) == pdTRUE)
      {
        globalTileX = currentTileX;
        globalTileY = currentTileY;
        globalTileZ = currentTileZ;
        ESP_LOGI("TileCalc", "Task Tile X: %d, Tile Y: %d, Zoom: %d", globalTileX, globalTileY, globalTileZ);
        xSemaphoreGive(xPositionMutex);
      }

      M5.Display.clear(TFT_BLACK);

      // Calculate the top-left corner for the center tile to be displayed in the middle of the screen
      int centerX = (M5.Display.width() - TILE_SIZE) / 2;
      int centerY = (M5.Display.height() - TILE_SIZE) / 2;

      // Iterate to draw a 3x3 matrix of tiles
      for (int yOffset = -1; yOffset <= 1; ++yOffset)
      {
        for (int xOffset = -1; xOffset <= 1; ++xOffset)
        {
          int tileToDrawX = globalTileX + xOffset;
          int tileToDrawY = globalTileY + yOffset;

          char filePathBuffer[128];
          sprintf(filePathBuffer, "/map/%d/%d/%d.jpeg", globalTileZ, tileToDrawX, tileToDrawY);
          ESP_LOGI("drawImageMatrixTask", "Attempting to draw jpeg from path: %s at offset (%d, %d)", filePathBuffer, xOffset, yOffset);

          int drawX = centerX + (xOffset * TILE_SIZE);
          int drawY = centerY + (yOffset * TILE_SIZE);

          if (!SD_MMC.begin()) {
            ESP_LOGE("SD_CARD", "SD Card Mount Failed in drawImageMatrixTask");
            M5.Display.printf("SD Card Mount Failed\n");
            // Handle error, maybe skip drawing this tile
            continue;
          }

          File file = SD_MMC.open(filePathBuffer);
          if (!file) {
            ESP_LOGE("SD_CARD", "Failed to open file for reading: %s", filePathBuffer);
            // Handle error, maybe draw a placeholder or skip
            file.close(); // Ensure file handle is closed even if open failed
            continue;
          }

          M5.Display.drawJpgFile(SD_MMC, filePathBuffer, drawX, drawY);
          file.close();
          ESP_LOGI("SD_CARD", "Successfully drew Jpeg: %s at (%d, %d)", filePathBuffer, drawX, drawY);
        }
      }
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