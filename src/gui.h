#ifndef GUI_H
#define GUI_H
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#ifdef __cplusplus
extern "C" {
#endif

void drawImageMatrixTask(void *pvParameters);
bool drawJpgFromSD(const char* filePath);

#ifdef __cplusplus
}
#endif

extern const int TILE_SIZE;
// Declarations for GUI-related functions will go here.

#endif // GUI_H