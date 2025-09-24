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

#ifdef __cplusplus
} // extern "C"

extern M5Canvas screenBufferCanvas;
extern M5Canvas latLonCanvas;
extern M5Canvas speedCanvas;
extern M5Canvas altitudeCanvas;

#endif // __cplusplus

// Declarations for GUI-related functions will go here.
void updateDisplayWithLatLon();
void updateDisplayWithSpeed();
void updateDisplayWithAltitude();

#endif // GUI_H