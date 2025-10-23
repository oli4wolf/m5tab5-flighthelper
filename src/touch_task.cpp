#include <M5Unified.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <cmath> // For sqrt, pow
#include "touch_task.h"
#include "config.h"
#include "gui.h"      // For xGuiUpdateEventGroup, GUI_EVENT_MAP_DATA_READY, and handleSoundButtonPress
#include "gps_task.h" // For globalTileZ

// Global variables declared in main.cpp
extern int globalTileZ;

// Global variables declared in config.h and gui.h
extern EventGroupHandle_t xGuiUpdateEventGroup;
extern bool globalTwoFingerGestureActive;
extern int globalManualZoomLevel;
extern int globalMapOffsetX;
extern int globalMapOffsetY;
extern bool globalManualMapMode;

// Internal variables for touch gesture
static int initialTouchDistance = 0;
static int lastTouchX = 0;
static int lastTouchY = 0;
static unsigned long lastTapTime = 0; // For double-tap detection
static int tapCount = 0;              // For double-tap detection

int singleTouchX = -1;
int singleTouchY = -1;
int doubleTouchX1 = -1;
int doubleTouchY1 = -1;

m5::touch_point_t touchPoint[5];

void initTouchMonitorTask()
{
    globalTwoFingerGestureActive = false;
    globalManualZoomLevel = 0; // Initialize to 0, meaning no manual zoom applied yet
    ESP_LOGI("initTouchMonitorTask", "Touch monitor task initialized.");
}

void touchMonitorTask(void *pvParameters)
{
    ESP_LOGI("touchMonitorTask", "Task started.");

    while (true)
    {
        M5.update(); // Update M5Unified internal states for touch events

        int nums = M5.Lcd.getTouchRaw(touchPoint, 5);
        if (nums == 2)
        {
            // Two fingers detected
            int x1 = touchPoint[0].x;
            int y1 = touchPoint[0].y;
            int x2 = touchPoint[1].x;
            int y2 = touchPoint[1].y;

            singleTouchX = -1;
            singleTouchY = -1;

            int currentTouchDistance = static_cast<int>(sqrt(pow(x2 - x1, 2) + pow(y2 - y1, 2)));

            if (!globalTwoFingerGestureActive)
            {
                // Start of a new two-finger gesture
                globalTwoFingerGestureActive = true;
                initialTouchDistance = currentTouchDistance;
                ESP_LOGD("touchMonitorTask", "Two-finger gesture started. Initial distance: %d", initialTouchDistance);
            }
            else
            {
                // Gesture in progress
                int distanceChange = currentTouchDistance - initialTouchDistance;

                if (distanceChange > ZOOM_THRESHOLD)
                {
                    // Spread gesture (zoom in)
                    if (globalManualZoomLevel == 0)
                    { // If no manual zoom set, use globalTileZ as base
                        globalManualZoomLevel = globalTileZ;
                    }
                    globalManualZoomLevel++;
                    if (globalManualZoomLevel > MAX_ZOOM_LEVEL)
                    {
                        globalManualZoomLevel = MAX_ZOOM_LEVEL;
                    }
                    globalTileZ = globalManualZoomLevel;
                    initialTouchDistance = currentTouchDistance; // Reset initial distance for continuous zooming
                    xEventGroupSetBits(xGuiUpdateEventGroup, GUI_EVENT_MAP_DATA_READY);
                    ESP_LOGI("touchMonitorTask", "Zoom In. New zoom level: %d", globalManualZoomLevel);
                }
                else if (distanceChange < -ZOOM_THRESHOLD)
                {
                    // Pinch gesture (zoom out)
                    if (globalManualZoomLevel == 0)
                    { // If no manual zoom set, use globalTileZ as base
                        globalManualZoomLevel = globalTileZ;
                    }
                    globalManualZoomLevel--;
                    if (globalManualZoomLevel < MIN_ZOOM_LEVEL)
                    {
                        globalManualZoomLevel = MIN_ZOOM_LEVEL;
                    }
                    globalTileZ = globalManualZoomLevel;
                    initialTouchDistance = currentTouchDistance; // Reset initial distance for continuous zooming
                    xEventGroupSetBits(xGuiUpdateEventGroup, GUI_EVENT_MAP_DATA_READY);
                    ESP_LOGI("touchMonitorTask", "Zoom Out. New zoom level: %d", globalManualZoomLevel);
                }
            }
        }
        else if (nums == 1)
        {
            int x1 = touchPoint[0].x;
            int y1 = touchPoint[0].y;
            globalTwoFingerGestureActive = false;
            unsigned long currentTime = M5.millis();

            // If it is the first touch after no touch, reset lastTouchX/Y
            if (singleTouchX == -1 && singleTouchY == -1)
            {
                singleTouchX = x1;
                singleTouchY = y1;
            }

            // Check for double-tap
            if (currentTime - lastTapTime < DOUBLE_TAP_THRESHOLD_MS)
            {
                tapCount++;
            }
            else
            {
                tapCount = 1; // Reset tap count if too much time has passed
            }
            lastTapTime = currentTime; // Update last tap time

            // Tap behavior
            if (tapCount == 2)
            {
                // Double-tap detected
                globalManualMapMode = false; // Toggle manual map mode
                // Reset manual offsets when exiting manual mode
                globalMapOffsetX = 0;
                globalMapOffsetY = 0;
                tapCount = 0; // Reset tap count after processing double-tap
                singleTouchX = -1;
                singleTouchY = -1;
                ESP_LOGI("touchMonitorTask", "Double-tap detected. Manual map mode: %s", globalManualMapMode ? "ON" : "OFF");
            }

            // double tap toggle to gps mode.
            // Single Touch is either a drag or a button press.
            else
            {

                // Todo Button press detection could be added here.
                handleSoundButtonPress(x1, y1);
            }
        }
        else // nums == 0 (no touch)
        {
            // No Touch is also a release of the fingers.
            // Map draged detected.
            int x1 = touchPoint[0].x;
            int y1 = touchPoint[0].y;
            // Single touch drag for panning
            if (singleTouchX != -1 || singleTouchY != -1)
            {
                globalManualMapMode = true; // Enter manual map mode on single touch
                int deltaX = x1 - singleTouchX;
                int deltaY = y1 - singleTouchY;

                globalMapOffsetX += deltaX;
                globalMapOffsetY += deltaY;
                xEventGroupSetBits(xGuiUpdateEventGroup, GUI_EVENT_MAP_DATA_READY);
                ESP_LOGD("touchMonitorTask", "Panning map. OffsetX: %d, OffsetY: %d", globalMapOffsetX, globalMapOffsetY);
            }

            // 1.B map drag released reset the singleTouchX/Y positions.
            singleTouchX = -1;
            singleTouchY = -1;

            // 2. two finger released.
            // 2.A zoom level to be adapted.

            globalTwoFingerGestureActive = false;
            // If no touch, and we were in manual map mode, reset tapCount if enough time has passed
            if (globalManualMapMode && tapCount > 0)
            {
                unsigned long currentTime = M5.millis();
                if (currentTime - lastTapTime > DOUBLE_TAP_THRESHOLD_MS)
                {
                    tapCount = 0; // Reset tap count if too much time has passed
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(TOUCH_TASK_DELAY_MS));
    }
}
