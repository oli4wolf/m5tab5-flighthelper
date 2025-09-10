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
bool drawJpgFromSD(const char* filePath) {
  if (!SD_MMC.begin()) {
    ESP_LOGE("SD_CARD", "SD Card Mount Failed in drawJpegFromSD");
    M5.Display.printf("SD Card Mount Failed\n");
    return false;
  }

  File file = SD_MMC.open(filePath);
  if (!file) {
    ESP_LOGE("SD_CARD", "Failed to open file for reading: %s", filePath);
    return false;
  }

  M5.Display.drawJpgFile(SD_MMC, filePath, 0, 0); // Draw at (0,0)
  file.close();
  ESP_LOGI("SD_CARD", "Successfully drew Jpeg: %s", filePath);
  return true;
}
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

// External function from main.cpp
extern bool drawJpgFromSD(const char* filePath);

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

      // Draw the image from SD card
      char filePathBuffer[128];
      sprintf(filePathBuffer, "/map/%d/%d/%d.jpeg", globalTileZ, globalTileX, globalTileY);
      ESP_LOGI("drawImageMatrixTask", "Attempting to draw jpeg from path: %s", filePathBuffer);

      M5.Display.clear(TFT_BLACK);
      if (!drawJpgFromSD(filePathBuffer))
      {
        M5.Display.printf("Failed to draw image!\n");
        ESP_LOGE("drawImageMatrixTask", "Failed to draw image from SD card.");
      }
      vTaskDelay(pdMS_TO_TICKS(5000)); // Display the image for 5 seconds
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