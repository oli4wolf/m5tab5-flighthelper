#include "variometer_task.h"
#include <M5Unified.h>
#include "sensor_task.h" // For globalPressure and xSensorMutex
#include <freertos/semphr.h>
#include <math.h> // For pow()

// Declare extern global variables from main.cpp
extern float globalPressure;
extern SemaphoreHandle_t xSensorMutex;

// Global variables for variometer
float globalAltitude_m = 0.0; // Current altitude in meters
float globalVerticalSpeed_mps = 0.0; // Vertical speed in meters per second
SemaphoreHandle_t xVariometerMutex;

// Constants for altitude calculation (standard atmosphere)
const float P0 = 1013.25; // Standard sea-level pressure in hPa

// Function to convert pressure (hPa) to altitude (meters)
float pressureToAltitude(float pressure_hPa) {
    return 44330.0 * (1.0 - pow(pressure_hPa / P0, 1.0 / 5.255));
}

void initVariometerTask() {
    xVariometerMutex = xSemaphoreCreateMutex();
    if (xVariometerMutex == NULL) {
        ESP_LOGE("Variometer", "Failed to create variometer mutex");
    }
    M5.Speaker.begin(); // Initialize the speaker
    M5.Speaker.setVolume(64); // Set a default volume (0-255)
    ESP_LOGI("Variometer", "Variometer task initialized. Speaker enabled.");
}

void variometerAudioTask(void *pvParameters) {
    (void) pvParameters;

    float previousAltitude = 0.0;
    unsigned long previousMillis = millis();
    const unsigned long updateIntervalMs = 200; // Update every 200ms
    const float altitudeChangeThreshold_mps = 0.5; // meters per second for tone trigger

    // Initial altitude reading
    if (xSemaphoreTake(xSensorMutex, portMAX_DELAY) == pdTRUE) {
        previousAltitude = pressureToAltitude(globalPressure);
        xSemaphoreGive(xSensorMutex);
    }

    for (;;) {
        unsigned long currentMillis = millis();
        if (currentMillis - previousMillis >= updateIntervalMs) {
            float currentPressure = 0;
            if (xSemaphoreTake(xSensorMutex, portMAX_DELAY) == pdTRUE) {
                currentPressure = globalPressure;
                xSemaphoreGive(xSensorMutex);
            }

            float currentAltitude = pressureToAltitude(currentPressure);
            float altitudeChange = currentAltitude - previousAltitude;
            float timeDeltaSeconds = (float)(currentMillis - previousMillis) / 1000.0;
            float verticalSpeed = altitudeChange / timeDeltaSeconds; // meters per second

            if (xSemaphoreTake(xVariometerMutex, portMAX_DELAY) == pdTRUE) {
                globalAltitude_m = currentAltitude;
                globalVerticalSpeed_mps = verticalSpeed;
                xSemaphoreGive(xVariometerMutex);
            }

            // Tone generation logic
            if (verticalSpeed > altitudeChangeThreshold_mps) {
                // Rising tone: higher frequency, frequency increases with climb rate
                int frequency = 1000 + (int)(verticalSpeed * 50); // Example: 1000Hz + 50Hz per m/s
                M5.Speaker.tone(frequency, 50); // Short tone
            } else if (verticalSpeed < -altitudeChangeThreshold_mps) {
                // Sinking tone: lower frequency, frequency decreases with sink rate
                int frequency = 500 - (int)(fabs(verticalSpeed) * 50); // Example: 500Hz - 50Hz per m/s
                if (frequency < 100) frequency = 100; // Minimum frequency
                M5.Speaker.tone(frequency, 50); // Short tone
            } else {
                // Stable or minor changes, no tone
                M5.Speaker.stop();
            }

            previousAltitude = currentAltitude;
            previousMillis = currentMillis;
        }
        vTaskDelay(pdMS_TO_TICKS(50)); // Check more frequently than updateIntervalMs
    }
}