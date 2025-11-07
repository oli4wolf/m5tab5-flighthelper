#ifndef GUI_H
#define GUI_H
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h> // Include for EventGroupHandle_t

#ifdef __cplusplus
// C++ specific includes
#include <vector>
#include <list>
#include <map>
#include <string>
#include <freertos/semphr.h> // For SemaphoreHandle_t
#include "config.h" // For TILE_PATH_MAX_LENGTH

extern bool globalTwoFingerGestureActive; // New: Flag for active two-finger gesture
extern int globalManualZoomLevel; // New: Manually set zoom level

extern "C" {
#endif

extern EventGroupHandle_t xGuiUpdateEventGroup;

// Event bits for GUI updates
#define GUI_EVENT_GPS_DATA_READY    (1 << 0)
#define GUI_EVENT_VARIO_DATA_READY  (1 << 1)
#define GUI_EVENT_MAP_DATA_READY    (1 << 2)
#define GUI_EVENT_SOUND_BUTTON_READY (1 << 3)
#define GUI_EVENT_TOUCH_DATA_READY (1 << 4) // New: Event bit for touch data updates

extern char globalLastDrawnTilePath[TILE_PATH_MAX_LENGTH];
extern char globalCurrentCenterTilePath[TILE_PATH_MAX_LENGTH];
extern char tilePaths[SCREEN_BUFFER_TILE_DIMENSION][SCREEN_BUFFER_TILE_DIMENSION][TILE_PATH_MAX_LENGTH]; // Declare global tilePaths

// C-linkage function declarations
void drawImageMatrixTask(void *pvParameters);
bool drawJpgFromSD(const char* filePath);
void drawDirectionIcon(M5Canvas& canvas, int centerX, int centerY, double direction);
void drawSoundButton(); // Modified to not take canvas parameter
void updateTiles(double currentLatitude, double currentLongitude, int currentTileZ, int currentTileX, int currentTileY, double globalDirection); // New: Declare updateTiles function
void handleSoundButtonPress(int x, int y); // Declare handleSoundButtonPress
void initHikeButton();
void drawHikeOverlayButton();
void handleHikeOverlayButtonPress(int x, int y);
void initBikeOverlayButton();
void drawBikeOverlayButton();
void handleBikeOverlayButtonPress(int x, int y);
void initHikeButton();
void drawHikeOverlayButton();
void handleHikeOverlayButtonPress(int x, int y);
void initBikeOverlayButton();
void drawBikeOverlayButton();
void handleBikeOverlayButtonPress(int x, int y);

#ifdef __cplusplus
} // extern "C"

extern M5Canvas screenBufferCanvas;
extern M5Canvas gpsCanvas;
extern M5Canvas varioCanvas; // Added varioCanvas extern
extern M5Canvas dir_icon; // Declare dir_icon globally

#endif // __cplusplus

// Declarations for GUI-related functions will go here.
void updateDisplayWithGPSTelemetry();
void updateDisplayWithVarioTelemetry();
void initDirectionIcon(); // Declare the initialization function
void initSoundButton(); // Declare the initialization function for the sound button - moved to main.cpp

#endif // GUI_H