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

extern "C" {
#endif

extern EventGroupHandle_t xGuiUpdateEventGroup;

// Event bits for GUI updates
#define GUI_EVENT_GPS_DATA_READY    (1 << 0)
#define GUI_EVENT_VARIO_DATA_READY  (1 << 1)
#define GUI_EVENT_MAP_DATA_READY    (1 << 2)
#define GUI_EVENT_SOUND_BUTTON_READY (1 << 3)

extern char globalLastDrawnTilePath[TILE_PATH_MAX_LENGTH];
extern char globalCurrentCenterTilePath[TILE_PATH_MAX_LENGTH];

// C-linkage function declarations
void drawImageMatrixTask(void *pvParameters);
bool drawJpgFromSD(const char* filePath);
void drawDirectionIcon(M5Canvas& canvas, int centerX, int centerY, double direction);
void drawSoundButton(); // Modified to not take canvas parameter

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