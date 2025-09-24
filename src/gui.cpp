// This file will contain the implementation of GUI-related functions.
#include <Arduino.h>
#include "FS.h"     // SD Card ESP32
#include "SD_MMC.h" // SD Card ESP32
#include <M5Unified.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <limits> // For INT_MAX
#include "gps_task.h"
#include "tile_calculator.h"
#include "gui.h"    // Include its own header
#include "config.h" // Include configuration constants

// External global variables from main.cpp
  extern SemaphoreHandle_t xGPSMutex; // Declare extern here
  extern double globalLatitude;       // Declare extern here
  extern double globalLongitude;      // Declare extern here
  extern bool globalValid;            // Declare extern here
  extern double globalSpeed;          // Declare extern here
  extern double globalAltitude;       // Declare extern here

extern SemaphoreHandle_t xSensorMutex;
extern float globalPressure;
extern float globalTemperature;
extern float globalAltitude_m;
extern float globalVerticalSpeed_mps;
extern SemaphoreHandle_t xVariometerMutex;

extern SemaphoreHandle_t xPositionMutex;
extern int globalTileX;
extern int globalTileY;
extern int globalTileZ;
// Global LRU Cache instance (1MB)
M5Canvas tileCanvas(&M5.Display);         // Declare M5Canvas globally for individual tile drawing
M5Canvas screenBufferCanvas(&M5.Display); // Declare M5Canvas globally for full screen buffer
M5Canvas gpsCanvas(&M5.Display);
M5Canvas varioCanvas(&M5.Display);

// Helper function to draw a single tile, handling cache and SD loading
void drawTile(M5Canvas &canvas, int tileX, int tileY, int zoom, const char *filePath)
{
  File file = SD_MMC.open(filePath);
  if (!file)
  {
    ESP_LOGE("SD_CARD", "Failed to open file for reading: %s", filePath);
    return;
  }
  canvas.drawJpgFile(SD_MMC, filePath, 0, 0);
  file.close();
  ESP_LOGI("drawTile", "Loaded and drew Jpeg from SD: %s", filePath);
}

void drawImageMatrixTask(void *pvParameters)
{
  ESP_LOGI("drawImageMatrixTask", "Task started.");
  ESP_LOGI("drawImageMatrixTask", "Display Width: %d, Height: %d", M5.Display.width(), M5.Display.height());

  int currentTileX = 0;
  int currentTileY = 0;
  int currentTileZ = 0;
  double currentLatitude = 0;
  double currentLongitude = 0;
  double currentSpeed = 0;
  bool currentValid = false;

  // SCREEN_BUFFER_TILE_DIMENSION x SCREEN_BUFFER_TILE_DIMENSION conceptual tile array to store file paths
  char tilePaths[SCREEN_BUFFER_TILE_DIMENSION][SCREEN_BUFFER_TILE_DIMENSION][TILE_PATH_MAX_LENGTH];

  tileCanvas.createSprite(TILE_SIZE, TILE_SIZE);                                                                       // Initialize M5Canvas for individual tiles
  ESP_LOGI("drawImageMatrixTask", "tileCanvas created. Width: %d, Height: %d", tileCanvas.width(), tileCanvas.height());
  screenBufferCanvas.createSprite(SCREEN_BUFFER_TILE_DIMENSION * TILE_SIZE, SCREEN_BUFFER_TILE_DIMENSION * TILE_SIZE); // Initialize M5Canvas for full screen buffer
  ESP_LOGI("drawImageMatrixTask", "screenBufferCanvas created. Width: %d, Height: %d", screenBufferCanvas.width(), screenBufferCanvas.height());
  gpsCanvas.createSprite(SCREEN_WIDTH, 128);
  ESP_LOGI("drawImageMatrixTask", "gpsCanvas created. Width: %d, Height: %d", gpsCanvas.width(), gpsCanvas.height());
  varioCanvas.createSprite(SCREEN_WIDTH, 128);
  ESP_LOGI("drawImageMatrixTask", "varioCanvas created. Width: %d, Height: %d", varioCanvas.width(), varioCanvas.height());

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

    if (!currentValid)
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
      for (int y = 0; y < SCREEN_BUFFER_TILE_DIMENSION; ++y)
      {
        for (int x = 0; x < SCREEN_BUFFER_TILE_DIMENSION; ++x)
        {
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
      for (int yOffset = -DRAW_GRID_CENTER_OFFSET; yOffset <= DRAW_GRID_CENTER_OFFSET; ++yOffset)
      {
        for (int xOffset = -DRAW_GRID_CENTER_OFFSET; xOffset <= DRAW_GRID_CENTER_OFFSET; ++xOffset)
        {
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
          TFT_RED);

      // Draw arrow shaft (line)
      screenBufferCanvas.fillRect(
          centerX - 1, // Thicker line
          centerY + ARROW_HEAD_LENGTH / 2,
          3, // Width of the shaft
          ARROW_SHAFT_LENGTH,
          TFT_RED);

      ESP_LOGI("drawImageMatrixTask", "Pushing screenBufferCanvas to M5.Display at (-152, 128).");
      screenBufferCanvas.pushSprite(-152, 128);
      ESP_LOGI("drawImageMatrixTask", "screenBufferCanvas pushed.");

      // Update and display text zones
      ESP_LOGI("drawImageMatrixTask", "Calling updateDisplayWithGPSTelemetry().");
      updateDisplayWithGPSTelemetry();
      ESP_LOGI("drawImageMatrixTask", "Calling updateDisplayWithVarioTelemetry().");
      updateDisplayWithVarioTelemetry();

      vTaskDelay(pdMS_TO_TICKS(DRAW_IMAGE_TASK_DELAY_MS)); // Display the image for DRAW_IMAGE_TASK_DELAY_MS milliseconds
    }
  }
}

// Implementations for text zone update functions
void updateDisplayWithVarioTelemetry()
{
  ESP_LOGI("updateDisplayWithVarioTelemetry", "Task started.");
  float currentPressure = 0;
  float currentTemperature = 0;
  float currentBaroAltitude = 0;
  float currentVerticalSpeed = 0;

  if (xSemaphoreTake(xSensorMutex, portMAX_DELAY) == pdTRUE)
  {
    currentPressure = globalPressure;
    currentTemperature = globalTemperature;
    xSemaphoreGive(xSensorMutex);
  }

  if (xSemaphoreTake(xVariometerMutex, portMAX_DELAY) == pdTRUE)
  {
    currentBaroAltitude = globalAltitude_m;
    currentVerticalSpeed = globalVerticalSpeed_mps;
    xSemaphoreGive(xVariometerMutex);
  }

  varioCanvas.clear(TFT_BLACK);
  varioCanvas.setFont(&fonts::Font2);
  varioCanvas.setTextSize(2);
  varioCanvas.setTextColor(TFT_WHITE);
  varioCanvas.setCursor(0, 0);
  varioCanvas.printf("Pressure: %.1f hPa\n", currentPressure);
  varioCanvas.printf("Temperature: %.1f C\n", currentTemperature);
  varioCanvas.printf("Altitude: %.1f m\n", currentBaroAltitude);
  varioCanvas.printf("Vertical Speed: %.1f m/s\n", currentVerticalSpeed);
  ESP_LOGI("updateDisplayWithVarioTelemetry", "Pushing varioCanvas to M5.Display at (0, 0).");
  varioCanvas.pushSprite(0, 0);
  ESP_LOGI("updateDisplayWithVarioTelemetry", "varioCanvas pushed.");

  return;
}

// Implementations for text zone update functions
void updateDisplayWithGPSTelemetry()
{
  ESP_LOGI("updateDisplayWithGPSTelemetry", "Task started.");
  double currentLatitude = 0;
  double currentLongitude = 0;
  double currentSpeed = 0;
  double currentAltitude = 0;

  if (xSemaphoreTake(xGPSMutex, portMAX_DELAY) == pdTRUE)
  {
    currentLatitude = globalLatitude;
    currentLongitude = globalLongitude;
    currentSpeed = globalSpeed;
    currentAltitude = globalAltitude;
    xSemaphoreGive(xGPSMutex);
  }

  gpsCanvas.clear(TFT_BLACK);
  gpsCanvas.setFont(&fonts::Font2);
  gpsCanvas.setTextSize(2);
  gpsCanvas.setTextColor(TFT_WHITE);
  gpsCanvas.setCursor(0, 0);
  if (!globalValid)
  {
    gpsCanvas.printf("Waiting for GPS fix...");
    ESP_LOGW("GPS", "No valid GPS fix.");
  }
  else
  {
    gpsCanvas.printf("Lat: %.6f\n", currentLatitude);
    gpsCanvas.printf("Lng: %.6f\n", currentLongitude);
    gpsCanvas.printf("Alt: %.1f m\n", currentAltitude);
    gpsCanvas.printf("Speed: %.1f km/h\n", currentSpeed);
    ESP_LOGI("GPS", "Valid GPS fix.");
  }
  // Adjusting the pushSprite coordinates to be visible on a typical M5Stack screen.
  // Assuming screen height is M5.Display.height().
  // Place gpsCanvas at the bottom of the screen.
  int gpsCanvasY = M5.Display.height() - gpsCanvas.height();
  ESP_LOGI("updateDisplayWithGPSTelemetry", "Pushing gpsCanvas to M5.Display at (0, %d).", gpsCanvasY);
  gpsCanvas.pushSprite(0, gpsCanvasY);
  ESP_LOGI("updateDisplayWithGPSTelemetry", "gpsCanvas pushed.");

  return;
}
