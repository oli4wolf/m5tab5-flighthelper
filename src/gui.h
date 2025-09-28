#ifndef GUI_H
#define GUI_H
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

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

extern char globalLastDrawnTilePath[TILE_PATH_MAX_LENGTH];
extern char globalCurrentCenterTilePath[TILE_PATH_MAX_LENGTH];

// C-linkage function declarations
void drawImageMatrixTask(void *pvParameters);
bool drawJpgFromSD(const char* filePath);
void drawDirectionIcon(M5Canvas& canvas, int centerX, int centerY, double direction);
void drawSoundButton(M5Canvas& canvas);

#ifdef __cplusplus
} // extern "C"

extern M5Canvas screenBufferCanvas;
extern M5Canvas gpsCanvas;
extern M5Canvas dir_icon; // Declare dir_icon globally

#endif // __cplusplus

// Declarations for GUI-related functions will go here.
void updateDisplayWithGPSTelemetry();
void updateDisplayWithVarioTelemetry();
void initDirectionIcon(); // Declare the initialization function
void initSoundButton(); // Declare the initialization function for the sound button - moved to main.cpp

#endif // GUI_H