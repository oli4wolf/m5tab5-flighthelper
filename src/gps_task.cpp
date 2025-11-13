#include "gps_task.h"
#include <M5Unified.h>
#include <TinyGPSPlus.h>
#include <freertos/semphr.h> // Required for mutex
#include "config.h"          // Include configuration constants
#include "tile_calculator.h"
#include "gui.h" // Include gui.h for event group

#include "gpsTestData.h" // Include GPS test data

// Declare extern global variables and mutex from main.cpp
extern double globalLatitude;
extern double globalLongitude;
extern double globalAltitude;
extern bool globalTestdata; // Flag to indicate if test data is being used
extern bool globalValid;    // Indicates if a valid GPS fix is available
extern double globalDirection;
extern uint32_t globalTime;
extern SemaphoreHandle_t xGPSMutex;
extern bool globalManualMapMode; // New: Flag to indicate if map is in manual drag mode

// The TinyGPSPlus object
TinyGPSPlus gps;

// The serial port for GPS
HardwareSerial gpsSerial(GPS_UART); // Use UART1

void initGPSTask()
{
    // Initialize UART1 for GPS communication
    // RX (GPS TX) on GPIO0, TX (GPS RX) on GPIO1
    gpsSerial.begin(GPS_SERIAL_BAUD_RATE, GPS_SERIAL_MODE, GPS_SERIAL_RX_PIN, GPS_SERIAL_TX_PIN);

    if (!gpsSerial)
    {
        ESP_LOGE("GPS", "Failed to initialize GPS serial port.");
        while (1)
            ;
    }
    else
    {
        ESP_LOGI("GPS", "GPS serial port initialized successfully.");
    }
}

void gpsReadTask(void *pvParameters)
{
    (void)pvParameters; // Suppress unused parameter warning

    for (;;)
    {
        while (gpsSerial.available() > 0)
            gps.encode(gpsSerial.read());

        if (gps.location.isUpdated())
        {
            if (xSemaphoreTake(xGPSMutex, portMAX_DELAY) == pdTRUE)
            {
                globalLatitude = gps.location.lat();
                globalLongitude = gps.location.lng();
                globalAltitude = gps.altitude.meters();
                globalDirection = gps.course.deg();
                globalSpeed = gps.speed.kmph(); // Update global speed
                globalTime = gps.time.value();
                globalValid = true;     // GPS fix is valid
                globalTestdata = false; // Clear test data flag

                ESP_LOGI("GPS", "Updated GPS Data: Lat %.6f, Lon %.6f, Alt %.2f m, Speed %.2f km/h, Dir %.2f deg, Time %lu",
                         globalLatitude, globalLongitude, globalAltitude, globalSpeed, globalDirection, globalTime);

                xSemaphoreGive(xGPSMutex);
                xEventGroupSetBits(xGuiUpdateEventGroup, GUI_EVENT_GPS_DATA_READY); // Signal GUI task
            }
        }

        if (!gps.location.isValid())
        {
            // If gps.location.isValid() is false, then there is no valid GPS fix.
            if (xSemaphoreTake(xGPSMutex, portMAX_DELAY) == pdTRUE)
            {
                globalValid = false; // No valid GPS fix
                xSemaphoreGive(xGPSMutex);
            }
            ESP_LOGD("GPS", "GPS location is NOT valid. globalValid set to false and %s", globalManualMapMode ? "manual map mode is ON." : "manual map mode is OFF.");
        }

        // nothing to do if not a valid sentence
        // ESP_LOGD("GPS", "Failed to encode char: %c", gpsChar); // Too verbose, use only if needed
        static unsigned long lastTestDataUpdateTime = 0;
        const unsigned long TESTDATA_UPDATE_INTERVAL_MS = 15000; // 15 seconds

        if (USE_TESTDATA && !globalManualMapMode && !globalValid)
        {
            if ((millis() - lastTestDataUpdateTime) >= TESTDATA_UPDATE_INTERVAL_MS)
            {
                if (xSemaphoreTake(xGPSMutex, portMAX_DELAY) == pdTRUE)
                {
                    int randomIndex = rand() % gpsTestData.size();
                    globalLatitude = gpsTestData[randomIndex].lat;
                    globalLongitude = gpsTestData[randomIndex].lon;
                    globalTestdata = true;
                    ESP_LOGW("GPS", "Using test data: Lat %.5f, Lon %.5f (globalValid: %d)", globalLatitude, globalLongitude, globalValid);
                    lastTestDataUpdateTime = millis(); // Update the last update time
                    xSemaphoreGive(xGPSMutex);
                }
                xEventGroupSetBits(xGuiUpdateEventGroup, GUI_EVENT_GPS_DATA_READY);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(GPS_TASK_DELAY_MS));
    }
}