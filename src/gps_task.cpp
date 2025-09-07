#include "gps_task.h"
#include <M5Unified.h>
#include <TinyGPSPlus.h>
#include <freertos/semphr.h> // Required for mutex

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
HardwareSerial gpsSerial(1); // Use UART1

static uint32_t gps_count = 0;

void initGPSTask() {
    // Initialize UART1 for GPS communication
    // RX (GPS TX) on GPIO0, TX (GPS RX) on GPIO1
    gpsSerial.begin(115200, SERIAL_8N1, 17, 16); 

    if (!gpsSerial) {
        ESP_LOGE("GPS", "Failed to initialize GPS serial port.");
        // Serial.println("Failed to initialize GPS serial port."); // Removed to avoid USB CDC conflict
        while (1);
    } else {
        ESP_LOGI("GPS", "GPS serial port initialized successfully.");
        // Serial.println("GPS serial port initialized successfully."); // Removed to avoid USB CDC conflict
    }
}

void gpsReadTask(void *pvParameters) {
    (void) pvParameters; // Suppress unused parameter warning

    for (;;) {
        while (gpsSerial.available() > 0) {
            char gpsChar = gpsSerial.read();
            // Serial.print(gpsChar); // Uncomment to see raw GPS data
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
                    }
                    //ESP_LOGD("GPS", "Fix acquired! Lat: %.6f, Lng: %.6f, Alt: %.2f m, Sats: %lu, HDOP: %lu",
                    //         globalLatitude, globalLongitude, globalAltitude, globalSatellites, globalHDOP);
                } else {
                    ESP_LOGI("GPS", "Encoded data, but location is not valid yet.");
                    globalValid = false;
                }
            } else {
                // ESP_LOGD("GPS", "Failed to encode char: %c", gpsChar); // Too verbose, use only if needed
            }
        }

        if (gps.location.isValid()) {
            // Serial.printf("GPS Task Count: %d, Lat: %.6f, Lng: %.6f, Alt: %.2f m, Sats: %lu, HDOP: %lu\n",
            //               gps_count, globalLatitude, globalLongitude, globalAltitude, globalSatellites, globalHDOP); // Removed to avoid USB CDC conflict
        } else {
            // Serial.printf("GPS Task Count: %d, Waiting for GPS fix... (Sats: %lu, HDOP: %lu)\n", gps_count, gps.satellites.value(), gps.hdop.value()); // Removed to avoid USB CDC conflict
        }
        
        gps_count++;
        vTaskDelay(pdMS_TO_TICKS(500)); // Check for new GPS data every 100ms
    }
}