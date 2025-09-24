#include "gps_task.h"
#include <M5Unified.h>
#include <TinyGPSPlus.h>
#include <freertos/semphr.h> // Required for mutex
#include "config.h" // Include configuration constants
#include "tile_calculator.h"

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
                    
                    // Update display with GPS telemetry
                    updateDisplayWithGPSTelemetry(globalLatitude, globalLongitude, globalAltitude, globalSatellites, globalHDOP, globalSpeed);
                } else {
                    updateDisplayGPSInvalid();
                }
            } else {
                // ESP_LOGD("GPS", "Failed to encode char: %c", gpsChar); // Too verbose, use only if needed
            }
        }

        vTaskDelay(pdMS_TO_TICKS(GPS_TASK_DELAY_MS));
    }
}

void updateDisplayGPSInvalid(){
    M5.Display.setCursor(0, 1024); // Start near the bottom of a 1280px height display
    M5.Display.fillRect(0, 1024, 720, 256, TFT_BLACK); // Clear the area for new text int32_t x, int32_t w, int32_t h
    M5.Display.printf("Waiting for GPS fix...\n");
}
    
void updateDisplayWithGPSTelemetry(double latitude, double longitude, double altitude, unsigned long satellites, unsigned long hdop, double speed){
    M5.Display.setCursor(0, 1024); // Start near the bottom of a 1280px height display
    M5.Display.fillRect(0, 1024, 720, 256, TFT_BLACK); // Clear the area for new text int32_t x, int32_t y, int32_t w, int32_t h
    M5.Display.printf("Lat: %.6f\n", latitude);
    M5.Display.printf("Lng: %.6f\n", longitude);
    M5.Display.printf("Alt: %.1f m\n", altitude);
    M5.Display.printf("Sats: %lu\n", satellites);
    M5.Display.printf("HDOP: %lu\n", hdop);
    M5.Display.printf("Speed: %.1f km/h\n", speed);
}