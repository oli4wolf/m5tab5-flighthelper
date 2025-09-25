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

extern "C" {
#endif

// C-linkage function declarations
void drawImageMatrixTask(void *pvParameters);
bool drawJpgFromSD(const char* filePath);
void drawDirectionIcon(M5Canvas& canvas, int centerX, int centerY, double direction);

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

#endif // GUI_H