#include "gps_task.h"
#include <M5Unified.h>
#include <TinyGPSPlus.h>
#include <freertos/semphr.h> // Required for mutex
#include "config.h" // Include configuration constants
#include "tile_calculator.h"
#include "gui.h" // Include gui.h for event group

// Declare extern global variables and mutex from main.cpp
extern double globalLatitude;
extern double globalLongitude;
extern double globalAltitude;
extern unsigned long globalSatellites;
extern unsigned long globalHDOP;
extern bool globalValid; // Indicates if a valid GPS fix is available
extern double globalDirection;
extern uint32_t globalTime;
extern SemaphoreHandle_t xGPSMutex;

// The TinyGPSPlus object
TinyGPSPlus gps;

// The serial port for GPS
HardwareSerial gpsSerial(GPS_UART); // Use UART1

void initGPSTask() {
    // Initialize UART1 for GPS communication
    // RX (GPS TX) on GPIO0, TX (GPS RX) on GPIO1
    gpsSerial.begin(GPS_SERIAL_BAUD_RATE, GPS_SERIAL_MODE, GPS_SERIAL_RX_PIN, GPS_SERIAL_TX_PIN);

    if (!gpsSerial) {
        ESP_LOGE("GPS", "Failed to initialize GPS serial port.");
        while (1);
    } else {
        ESP_LOGI("GPS", "GPS serial port initialized successfully.");
    }
}

void gpsReadTask(void *pvParameters) {
    (void) pvParameters; // Suppress unused parameter warning

    for (;;) {
        while (gpsSerial.available() > 0) {
            char gpsChar = gpsSerial.read();
            if (gps.encode(gpsChar)) {
                if (gps.location.isValid()) {
                    if (xSemaphoreTake(xGPSMutex, portMAX_DELAY) == pdTRUE) {
                        globalLatitude = gps.location.lat();
                        globalLongitude = gps.location.lng();
                        globalAltitude = gps.altitude.meters();
                        globalSatellites = gps.satellites.value();
                        globalDirection = gps.course.deg();
                        globalSpeed = gps.speed.kmph(); // Update global speed
                        globalTime = gps.time.value();
                        globalHDOP = gps.hdop.value();
                        globalValid = true; // GPS fix is valid

                        xSemaphoreGive(xGPSMutex);
                        xEventGroupSetBits(xGuiUpdateEventGroup, GUI_EVENT_GPS_DATA_READY); // Signal GUI task
                    }
                }
            } else {
                // ESP_LOGD("GPS", "Failed to encode char: %c", gpsChar); // Too verbose, use only if needed
                xEventGroupSetBits(xGuiUpdateEventGroup, GUI_EVENT_GPS_DATA_READY);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(GPS_TASK_DELAY_MS));
    }
}
