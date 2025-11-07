// This file will contain the implementation of GUI-related functions.
#include <Arduino.h>
#include "FS.h"              // SD Card ESP32
#include <cmath>             // For M_PI, sin, cos
#define M_PI_2 (M_PI / 2.0F) // Define M_PI_2 if not already defined by cmath
#include "SD_MMC.H"          // SD Card ESP32
#include <M5Unified.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/event_groups.h> // Include for EventGroupHandle_t
#include <limits>                  // For INT_MAX
#include "gps_task.h"
#include "tile_calculator.h"
#include "gui.h"    // Include its own header
#include "config.h" // Include configuration constants

// External global variables from main.cpp
extern EventGroupHandle_t xGuiUpdateEventGroup; // Declare extern for the event group
extern bool globalSoundEnabled;                 // Declare global sound enable flag
extern SemaphoreHandle_t xGPSMutex;             // Declare extern here
extern double globalLatitude;                   // Declare extern here
extern double globalLongitude;                  // Declare extern here
extern bool globalValid;                        // Declare extern here
extern bool globalTestdata;                     // Declare extern here
extern double globalSpeed;                      // Declare extern here
extern double globalAltitude;                   // Declare extern here
extern double globalDirection;                  // Declare extern here

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
M5Canvas verticalSpeedCanvas(&M5.Display);
M5Canvas hikeButtonCanvas(&M5.Display); // Declare M5Canvas for hike overlay button
M5Canvas bikeButtonCanvas(&M5.Display); // Declare M5Canvas for bike overlay button
M5Canvas dir_icon(&M5.Display);         // Declare M5Canvas globally for direction icon

// Define globalCurrentTilePath
char globalLastDrawnTilePath[TILE_PATH_MAX_LENGTH] = "";
char globalCurrentCenterTilePath[TILE_PATH_MAX_LENGTH] = "";
char tilePaths[SCREEN_BUFFER_TILE_DIMENSION][SCREEN_BUFFER_TILE_DIMENSION][TILE_PATH_MAX_LENGTH]; // Define global tilePaths

// Helper function to draw a single tile, handling cache and SD loading
void drawTile(M5Canvas &canvas, int tileX, int tileY, int zoom, const char *filePath)
{
  // Check if the requested tile is already loaded
  if (strcmp(globalLastDrawnTilePath, filePath) == 0)
  {
    ESP_LOGI("drawTile", "Tile already loaded: %s", filePath);
    return;
  }

  File file = SD_MMC.open(filePath);
  if (!file)
  {
    ESP_LOGE("SD_CARD", "Failed to open file for reading: %s", filePath);
    return;
  }
  canvas.drawJpgFile(SD_MMC, filePath, 0, 0);
  file.close();
  ESP_LOGI("drawTile", "Loaded and drew Jpeg from SD: %s", filePath);

  // Update the globalCurrentTilePath
  strncpy(globalLastDrawnTilePath, filePath, TILE_PATH_MAX_LENGTH - 1);
  globalLastDrawnTilePath[TILE_PATH_MAX_LENGTH - 1] = '\0'; // Ensure null-termination
}

void initDirectionIcon()
{
  /*
   * dir_icon color palette:
   *   0: DIR_ICON_TRANS_COLOR
   *   1: DIR_ICON_BG_COLOR
   *   2: foreground color (DIR_ICON_COLOD_ACTIVE or DIR_ICON_COLOR_INACTIVE)
   *   3: not used (default is TFT_WHITE)
   */

  // Allocate sprite
  dir_icon.setColorDepth(2);
  dir_icon.setPsram(false);
  dir_icon.createSprite(DIR_ICON_R * 2 + 1, DIR_ICON_R * 2 + 1);

  // Set palette colors
  dir_icon.setPaletteColor(dir_icon_palette_id_trans, DIR_ICON_TRANS_COLOR);
  dir_icon.setPaletteColor(dir_icon_palette_id_bg, DIR_ICON_BG_COLOR);
  dir_icon.setPaletteColor(dir_icon_palette_id_fg, DIR_ICON_COLOR_INACTIVE);

  // Draw icon
  dir_icon.fillSprite(dir_icon_palette_id_trans); // translucent background
  dir_icon.fillCircle(DIR_ICON_R, DIR_ICON_R, DIR_ICON_R, dir_icon_palette_id_fg);
  dir_icon.fillCircle(DIR_ICON_R, DIR_ICON_R, DIR_ICON_R - DIR_ICON_EDGE_WIDTH,
                      dir_icon_palette_id_bg);

  int x0 = DIR_ICON_R;
  int y0 = DIR_ICON_EDGE_WIDTH;
  int x1 = DIR_ICON_R + (DIR_ICON_R - DIR_ICON_EDGE_WIDTH) * cos(-M_PI_2 + DIR_ICON_ANGLE);
  int y1 = DIR_ICON_R - (DIR_ICON_R - DIR_ICON_EDGE_WIDTH) * sin(-M_PI_2 + DIR_ICON_ANGLE);
  int x2 = DIR_ICON_R - (DIR_ICON_R - DIR_ICON_EDGE_WIDTH) * cos(-M_PI_2 + DIR_ICON_ANGLE);
  int y2 = DIR_ICON_R - (DIR_ICON_R - DIR_ICON_EDGE_WIDTH) * sin(-M_PI_2 + DIR_ICON_ANGLE);

  dir_icon.fillTriangle(x0, y0, x1, y1, x2, y2, dir_icon_palette_id_fg);

  x0 = DIR_ICON_R;
  y0 = (int)(DIR_ICON_R * 1.2);
  dir_icon.fillTriangle(x0, y0, x1, y1, x2, y2, dir_icon_palette_id_bg);

  // set center of rotation
  dir_icon.setPivot(DIR_ICON_R, DIR_ICON_R);
}

// New function to update and draw map tiles
void updateTiles(double currentLatitude, double currentLongitude, int currentTileZ, int currentTileX, int currentTileY, double globalDirection)
{
  int pixelOffsetX = 0;
  int pixelOffsetY = 0;
 
  double effectiveLatitude = currentLatitude;
  double effectiveLongitude = currentLongitude;
  int effectiveTileX = currentTileX;
  int effectiveTileY = currentTileY;
 
  ESP_LOGD("updateTiles", "Initial - Lat: %.6f, Lng: %.6f, TileZ: %d", currentLatitude, currentLongitude, currentTileZ);
  latLngToPixelOffset(currentLatitude, currentLongitude, currentTileZ, &pixelOffsetX, &pixelOffsetY);
 
  // Calculate the top-left coordinates for the drawing grid
  // The central tile (effectiveTileX, effectiveTileY) will be at index [DRAW_GRID_CENTER_OFFSET][DRAW_GRID_CENTER_OFFSET] in the DRAW_GRID_DIMENSION x DRAW_GRID_DIMENSION array
  int conceptualGridStartX = effectiveTileX - DRAW_GRID_CENTER_OFFSET;
  int conceptualGridStartY = effectiveTileY - DRAW_GRID_CENTER_OFFSET;

  // Populate the TILE_GRID_DIMENSION x TILE_GRID_DIMENSION tilePaths array
  for (int y = 0; y < SCREEN_BUFFER_TILE_DIMENSION; ++y)
  {
    for (int x = 0; x < SCREEN_BUFFER_TILE_DIMENSION; ++x)
    {
      int tileToLoadX = effectiveTileX - SCREEN_BUFFER_CENTER_OFFSET + x;
      int tileToLoadY = effectiveTileY - SCREEN_BUFFER_CENTER_OFFSET + y;
      sprintf(tilePaths[y][x], "/maps/pixelkarte-farbe/%d/%d/%d.jpeg", currentTileZ, tileToLoadX, tileToLoadY);
      if (x == SCREEN_BUFFER_CENTER_OFFSET && y == SCREEN_BUFFER_CENTER_OFFSET)
      {
          strncpy(globalCurrentCenterTilePath, tilePaths[y][x], TILE_PATH_MAX_LENGTH - 1);
          globalCurrentCenterTilePath[TILE_PATH_MAX_LENGTH - 1] = '\0';
      }
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
  ESP_LOGD("updateTiles", "Performing full redraw.");
  // Draw all DRAW_GRID_DIMENSION * DRAW_GRID_DIMENSION tiles to the screen buffer
  for (int yOffset = -DRAW_GRID_CENTER_OFFSET; yOffset <= DRAW_GRID_CENTER_OFFSET; ++yOffset)
  {
    for (int xOffset = -DRAW_GRID_CENTER_OFFSET; xOffset <= DRAW_GRID_CENTER_OFFSET; ++xOffset)
    {
      int currentDrawX = drawOriginX + (xOffset * TILE_SIZE);
      int currentDrawY = drawOriginY + (yOffset * TILE_SIZE);
      tileCanvas.clear(TFT_DARKCYAN); // Clear the individual tile canvas
      drawTile(tileCanvas, conceptualGridStartX + xOffset, conceptualGridStartY + yOffset,
               currentTileZ, tilePaths[yOffset + SCREEN_BUFFER_CENTER_OFFSET][xOffset + SCREEN_BUFFER_CENTER_OFFSET]);
      tileCanvas.pushSprite(&screenBufferCanvas, currentDrawX, currentDrawY); // Draw tile to screen buffer
    }
  }

  // Draw a red point at the GPS fix location (center of the screen) on the screen buffer
  int centerX = screenBufferCanvas.width() / 2;
  int centerY = screenBufferCanvas.height() / 2;

  // Draw arrow head (triangle)
  drawDirectionIcon(screenBufferCanvas, centerX, centerY, globalDirection);
  drawSoundButton(); // Sound button now drawn directly to M5.Display
  drawHikeOverlayButton();
  drawBikeButton();

  // Calculate offsets to center the screenBufferCanvas on the M5.Display.
  // The screenBufferCanvas is larger than the display, so negative offsets are expected.
  const int offsetX = (M5.Display.width() - screenBufferCanvas.width()) / 2;
  const int offsetY = (M5.Display.height() - screenBufferCanvas.height()) / 2;
  
  screenBufferCanvas.pushSprite(offsetX, offsetY);
  ESP_LOGD("updateTiles", "Pushing screenBufferCanvas with calculated offsetX: %d, offsetY: %d", offsetX, offsetY);
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
  bool currentTestdata = false;

  // Store previous tile coordinates to detect changes
  int prevTileX = -1;
  int prevTileY = -1;
  int prevTileZ = -1;

  tileCanvas.createSprite(TILE_SIZE, TILE_SIZE); // Initialize M5Canvas for individual tiles
  // Todo could not the complete screen size be used here?
  screenBufferCanvas.createSprite(SCREEN_BUFFER_TILE_DIMENSION * TILE_SIZE, SCREEN_BUFFER_TILE_DIMENSION * TILE_SIZE); // Initialize M5Canvas for full screen buffer
  gpsCanvas.createSprite(SCREEN_WIDTH / 4, 128);
  hikeButtonCanvas.createSprite(SCREEN_WIDTH / 4, 128);
  bikeButtonCanvas.createSprite(SCREEN_WIDTH / 4, 128);
  varioCanvas.createSprite(SCREEN_WIDTH / 2, 128);
  verticalSpeedCanvas.createSprite(SCREEN_WIDTH / 2, 128);
  ESP_LOGI("drawImageMatrixTask", "Canvas initialized.");

  initDirectionIcon(); // Initialize the direction icon once
  initSoundButton();   // Initialize the sound button once - moved to main.cpp
  initHikeButton();    // Initialize the hike overlay button
  initBikeButton();    // Initialize the bike overlay button
  ESP_LOGI("drawImageMatrixTask", "Direction icon initialized.");
  // ESP_LOGI("drawImageMatrixTask", "Sound button initialized.");

  while (true)
  {
    // Acquire GPS data for map calculation
    if (xSemaphoreTake(xGPSMutex, portMAX_DELAY) == pdTRUE)
    {
      currentLatitude = globalLatitude;
      currentLongitude = globalLongitude;
      currentSpeed = globalSpeed;
      currentValid = globalValid;
      currentTestdata = globalTestdata;
      xSemaphoreGive(xGPSMutex);
    }

    if (currentValid || (USE_TESTDATA && currentTestdata)) // Use Testdata if nothing else.
    {
        // Calculate tile coordinates
        currentTileZ = globalTileZ; // Use global zoom level
        latLngToTile(currentLatitude, currentLongitude, currentTileZ, &currentTileX, &currentTileY);

        // Update global tile coordinates
        if (xSemaphoreTake(xPositionMutex, portMAX_DELAY) == pdTRUE)
        {
            globalTileX = currentTileX;
            globalTileY = currentTileY;
            globalTileZ = currentTileZ;
            ESP_LOGV("TileCalc", "Task Tile X: %d, Tile Y: %d, Zoom: %d", globalTileX, globalTileY, globalTileZ);
            xSemaphoreGive(xPositionMutex);
        }

        // Only set map update event if the center tile has changed
        if (currentTileX != prevTileX || currentTileY != prevTileY || currentTileZ != prevTileZ)
        {
            xEventGroupSetBits(xGuiUpdateEventGroup, GUI_EVENT_MAP_DATA_READY); // Signal GUI task for map update
            prevTileX = currentTileX;
            prevTileY = currentTileY;
            prevTileZ = currentTileZ;
        }
    }

    // Wait for GUI update events
    EventBits_t uxBits = xEventGroupWaitBits(
        xGuiUpdateEventGroup,
        GUI_EVENT_GPS_DATA_READY | GUI_EVENT_VARIO_DATA_READY | GUI_EVENT_MAP_DATA_READY | GUI_EVENT_SOUND_BUTTON_READY,
        pdTRUE,           // Clear bits on exit
        pdFALSE,          // Don't wait for all bits
        pdMS_TO_TICKS(10) // Wait for a short period, then re-evaluate
    );

    if ((uxBits & GUI_EVENT_MAP_DATA_READY) != 0)
    {
      ESP_LOGD("drawImageMatrixTask", "updateTiles: %.6f, %.6f, Z:%d, X:%d, Y:%d, Dir:%.2f",
               currentLatitude, currentLongitude, currentTileZ, currentTileX, currentTileY, globalDirection);
      updateTiles(currentLatitude, currentLongitude, currentTileZ, currentTileX, currentTileY, globalDirection);
    }

    if ((uxBits & GUI_EVENT_GPS_DATA_READY) != 0)
    {
      updateDisplayWithGPSTelemetry();
    }

    if ((uxBits & GUI_EVENT_VARIO_DATA_READY) != 0)
    {
      updateDisplayWithVarioTelemetry();
    }

    if ((uxBits & GUI_EVENT_SOUND_BUTTON_READY) != 0)
    {
      drawSoundButton(); // Draw directly to the main display
    }

    // vTaskDelay(pdMS_TO_TICKS(10)); // Small delay to prevent busy-waiting, now handled by xEventGroupWaitBits timeout
  }
}

// Implementations for text zone update functions
void updateDisplayWithVarioTelemetry()
{
  // ESP_LOGD("updateDisplayWithVarioTelemetry", "Task started.");
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

  varioCanvas.clear(TFT_DARKGRAY);
  varioCanvas.setFont(&fonts::Font2);
  varioCanvas.setTextSize(2);
  varioCanvas.setTextColor(TFT_WHITE);
  varioCanvas.setCursor(0, 0);
  varioCanvas.printf("Pressure: %.1f hPa\n", currentPressure);
  varioCanvas.printf("Temperature: %.1f C\n", currentTemperature);
  varioCanvas.printf("Altitude: %.1f m\n", currentBaroAltitude);
  varioCanvas.printf("Vertical Speed: %.1f m/s\n", currentVerticalSpeed);
  varioCanvas.pushSprite(0, 0);

  // implement vertical speed display
  if (currentVerticalSpeed > 0.5)
  {
    verticalSpeedCanvas.clear(TFT_GREEN);
      verticalSpeedCanvas.setTextColor(TFT_BLACK);
  }
  else if (currentVerticalSpeed < -0.5)
  {
    verticalSpeedCanvas.clear(TFT_RED);
      verticalSpeedCanvas.setTextColor(TFT_WHITE);
  }
  else
  {
    verticalSpeedCanvas.clear(TFT_BLACK);
      verticalSpeedCanvas.setTextColor(TFT_WHITE);
  }
  verticalSpeedCanvas.setFont(&fonts::Font2);
  verticalSpeedCanvas.setTextSize(6);

  // Calculate text dimensions for centering
  char speedText[20]; // Buffer to hold the formatted string
  sprintf(speedText, "%.1f m/s", currentVerticalSpeed);
  int16_t textWidth = verticalSpeedCanvas.textWidth(speedText);
  int16_t textHeight = verticalSpeedCanvas.fontHeight();
  // Calculate x and y coordinates to center the text
  int16_t x = (verticalSpeedCanvas.width() - textWidth) / 2;
  int16_t y = (verticalSpeedCanvas.height() - textHeight) / 2;

  verticalSpeedCanvas.setCursor(x, y);
  verticalSpeedCanvas.printf(speedText);
  verticalSpeedCanvas.pushSprite(SCREEN_WIDTH/2, 0);

  return;
}

// Implementations for text zone update functions
void updateDisplayWithGPSTelemetry()
{
  // ESP_LOGD("updateDisplayWithGPSTelemetry", "Task started.");
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
 
  gpsCanvas.setFont(&fonts::Font2);
  gpsCanvas.setTextSize(2);
  gpsCanvas.setTextColor(TFT_WHITE);
  gpsCanvas.setCursor(0, 0);
  if (!globalValid)
  {
    gpsCanvas.clear(TFT_DARKGREY);
    gpsCanvas.printf("Waiting for GPS fix...\n");
 
    // Duplicate assignment, will be removed in fix
    ESP_LOGW("GPS", "No valid GPS fix.");
  }
  else
  {
    gpsCanvas.clear(TFT_DARKGREEN);
    gpsCanvas.printf("Lat: %.6f\n", currentLatitude);
    gpsCanvas.printf("Lng: %.6f\n", currentLongitude);
    gpsCanvas.printf("Alt: %.1f m\n", currentAltitude);
    gpsCanvas.printf("Speed: %.1f km/h\n", currentSpeed);
  }
  // Adjusting the pushSprite coordinates to be visible on a typical M5Stack screen.
  // Assuming screen height is M5.Display.height().
  // Place gpsCanvas at the bottom of the screen.
  int gpsCanvasY = M5.Display.height() - gpsCanvas.height();
  gpsCanvas.pushSprite(0, gpsCanvasY);

  return;
}

void drawDirectionIcon(M5Canvas &canvas, int centerX, int centerY, double direction)
{
  // When dir icon is out of canvas
  if (!((-DIR_ICON_R < centerX && centerX < M5.Display.width() + DIR_ICON_R) &&
        (-DIR_ICON_R < centerY && centerY < M5.Display.height() + DIR_ICON_R)))
  {
    ESP_LOGD("drawDirectionIcon()", "out of canvas offset=(%d,%d)\n", centerX, centerY);
    return;
  }

  if (globalValid)
  {
    dir_icon.setPaletteColor(dir_icon_palette_id_fg, DIR_ICON_COLOR_ACTIVE);
  }
  else
  {
    dir_icon.setPaletteColor(dir_icon_palette_id_fg, DIR_ICON_COLOR_INACTIVE);
  }

  dir_icon.pushRotateZoomWithAA(&canvas, centerX, centerY, direction, 1, 1,
                                dir_icon_palette_id_trans);
}

// Sound button variables buttom second/4 left
static M5Canvas soundButtonCanvas(&M5.Display);
int soundButtonX;
int soundButtonY;
int soundButtonWidth;
int soundButtonHeight;

void initSoundButton()
{
  soundButtonWidth = SCREEN_WIDTH / 4;                                     // Example width
  soundButtonHeight = gpsCanvas.height();                                    // Example height
  soundButtonX = SCREEN_WIDTH / 4; // 10px from right edge
  soundButtonY = M5.Display.height() - gpsCanvas.height();                                         // 10px from top edge

  soundButtonCanvas.createSprite(soundButtonWidth, soundButtonHeight);
  soundButtonCanvas.setFont(&fonts::Font2);
  soundButtonCanvas.setTextSize(1);
  ESP_LOGE("SoundButton", "initSoundButton() called. X: %d, Y: %d, W: %d, H: %d", soundButtonX, soundButtonY, soundButtonWidth, soundButtonHeight);
}

void drawSoundButton()
{ // Modified to not take canvas parameter
  ESP_LOGE("SoundButton", "drawSoundButton() called. globalSoundEnabled: %s", globalSoundEnabled ? "true" : "false");
  soundButtonCanvas.clear(globalSoundEnabled ? TFT_DARKGREEN : TFT_DARKGREY);
  soundButtonCanvas.setTextColor(TFT_WHITE);
  soundButtonCanvas.setTextDatum(CC_DATUM); // Center-center datum
  soundButtonCanvas.printf("%s", globalSoundEnabled ? "Sound ON" : "Sound OFF");
  soundButtonCanvas.pushSprite(soundButtonX, soundButtonY); // Push directly to M5.Display

  screenBufferCanvas.pushSprite(soundButtonX, soundButtonY); // Also update screenBufferCanvas
}

// Hike Overlay button variables
static int hikeButtonWidth;
static int hikeButtonHeight;

void initHikeButton()
{
  hikeButtonWidth = SCREEN_WIDTH / 4;
  hikeButtonHeight = gpsCanvas.height();

  hikeButtonCanvas.createSprite(hikeButtonWidth, hikeButtonHeight);
  hikeButtonCanvas.setFont(&fonts::Font2);
  hikeButtonCanvas.setTextSize(6);
}

void drawHikeOverlayButton()
{
  ESP_LOGE("HikeOverlayButton", "drawHikeOverlayButton() called.");
  hikeButtonCanvas.clear(TFT_DARKCYAN); // Example color
  hikeButtonCanvas.setTextColor(TFT_WHITE);
  hikeButtonCanvas.setTextSize(6);

  hikeButtonCanvas.drawString("Hike", hikeButtonWidth / 2, hikeButtonHeight / 2);
  int gpsCanvasY = M5.Display.height() - gpsCanvas.height();
  hikeButtonCanvas.pushSprite(SCREEN_WIDTH/2, gpsCanvasY);

}

void handleHikeButtonPress(int x, int y)
{
  int gpsCanvasY = M5.Display.height() - gpsCanvas.height();
  ESP_LOGE("HikeOverlayButton", "handleHikeOverlayButtonPress() called with x: %d, y: %d", x, y);
  if (x >= SCREEN_WIDTH/2 && x <= (SCREEN_WIDTH/2 + hikeButtonWidth) &&
      y >= gpsCanvasY && y <= (gpsCanvasY + hikeButtonHeight))
  {
    ESP_LOGE("HikeOverlayButton", "Hike Overlay button pressed.");
    // Add logic for hike overlay here
  }
  else
  {
    ESP_LOGE("HikeOverlayButton", "Press outside Hike Overlay button bounds.");
  }
}

// Bike Overlay button variables
static int bikeButtonWidth;
static int bikeButtonHeight;
static int bikeButtonX;
static int bikeButtonY;

void initBikeButton()
{
  bikeButtonX = (SCREEN_WIDTH / 4) * 3; // 10px from right edge
  bikeButtonY = M5.Display.height() - gpsCanvas.height(); // 10px from top edge
  bikeButtonWidth = SCREEN_WIDTH / 4;
  bikeButtonHeight = gpsCanvas.height();

  bikeButtonCanvas.createSprite(bikeButtonWidth, bikeButtonHeight);
  bikeButtonCanvas.setFont(&fonts::Font2);
  bikeButtonCanvas.setTextSize(6);
}

void drawBikeButton()
{
  ESP_LOGE("BikeOverlayButton", "drawBikeOverlayButton() called.");
  bikeButtonCanvas.clear(TFT_ORANGE); // Example color
  bikeButtonCanvas.setTextColor(TFT_WHITE);
  bikeButtonCanvas.setTextDatum(CC_DATUM);
  bikeButtonCanvas.printf("Bike");
  bikeButtonCanvas.pushSprite(bikeButtonX, bikeButtonY);
}

void handleBikeButtonPress(int x, int y)
{
  int gpsCanvasY = M5.Display.height() - gpsCanvas.height();
  ESP_LOGE("BikeOverlayButton", "handleBikeOverlayButtonPress() called with x: %d, y: %d", x, y);
  if (x >= bikeButtonX && x <= (bikeButtonX + bikeButtonWidth) &&
      y >= bikeButtonY && y <= (bikeButtonY + bikeButtonHeight))
  {
    ESP_LOGE("BikeOverlayButton", "Bike Overlay button pressed.");
    // Add logic for bike overlay here
  }
  else
  {
    ESP_LOGE("BikeOverlayButton", "Press outside Bike Overlay button bounds.");
  }
}
void handleSoundButtonPress(int x, int y)
{
  ESP_LOGE("SoundButton", "handleSoundButtonPress() called with x: %d, y: %d", x, y);
  if (x >= soundButtonX && x <= (soundButtonX + soundButtonWidth) &&
      y >= soundButtonY && y <= (soundButtonY + soundButtonHeight))
  {
    globalSoundEnabled = !globalSoundEnabled;
    ESP_LOGE("SoundButton", "Sound button pressed. globalSoundEnabled: %s", globalSoundEnabled ? "true" : "false");
    xEventGroupSetBits(xGuiUpdateEventGroup, GUI_EVENT_SOUND_BUTTON_READY); // Signal GUI task
  }
  else
  {
    ESP_LOGE("SoundButton", "Press outside sound button bounds.");
  }
}
