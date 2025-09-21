#include "variometer_task.h"
#include <M5Unified.h>
#include "sensor_task.h" // For globalPressure and xSensorMutex
#include <freertos/semphr.h>
#include <math.h> // For pow()
#include "config.h" // Include configuration constants

// Declare extern global variables from main.cpp
extern float globalPressure;
extern SemaphoreHandle_t xSensorMutex;

// Global variables for variometer
float globalAltitude_m = 0.0; // Current altitude in meters
float globalVerticalSpeed_mps = 0.0; // Vertical speed in meters per second
SemaphoreHandle_t xVariometerMutex;

// Constants for altitude calculation (standard atmosphere)
// P0 is now defined in config.h as STANDARD_SEA_LEVEL_PRESSURE_HPA

// Function to convert pressure (hPa) to altitude (meters)
float pressureToAltitude(float pressure_hPa) {
    return ALTITUDE_CONSTANT_A * (1.0 - pow(pressure_hPa / STANDARD_SEA_LEVEL_PRESSURE_HPA, 1.0 / ALTITUDE_CONSTANT_B));
}

void initVariometerTask() {
    xVariometerMutex = xSemaphoreCreateMutex();
    if (xVariometerMutex == NULL) {
        ESP_LOGE("Variometer", "Failed to create variometer mutex");
    }
    M5.Speaker.begin(); // Initialize the speaker
    M5.Speaker.setVolume(SPEAKER_DEFAULT_VOLUME); // Set a default volume (0-255)
    ESP_LOGI("Variometer", "Variometer task initialized. Speaker enabled.");
}

void variometerAudioTask(void *pvParameters) {
    (void) pvParameters;

    float previousAltitude = 0.0;
    unsigned long previousMillis = millis();
    const unsigned long updateIntervalMs = VARIOMETER_UPDATE_INTERVAL_MS; // Update every VARIOMETER_UPDATE_INTERVAL_MS
    const float altitudeChangeThreshold_mps = ALTITUDE_CHANGE_THRESHOLD_MPS; // meters per second for tone trigger

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
                int frequency = RISING_TONE_BASE_FREQ_HZ + (int)(verticalSpeed * RISING_TONE_MULTIPLIER_HZ_PER_MPS);
                M5.Speaker.tone(frequency, TONE_DURATION_MS); // Short tone
            } else if (verticalSpeed < -altitudeChangeThreshold_mps) {
                // Sinking tone: lower frequency, frequency decreases with sink rate
                int frequency = SINKING_TONE_BASE_FREQ_HZ - (int)(fabs(verticalSpeed) * SINKING_TONE_MULTIPLIER_HZ_PER_MPS);
                if (frequency < MIN_TONE_FREQ_HZ) frequency = MIN_TONE_FREQ_HZ; // Minimum frequency
                M5.Speaker.tone(frequency, TONE_DURATION_MS); // Short tone
            } else {
                // Stable or minor changes, no tone
                M5.Speaker.stop();
            }

            previousAltitude = currentAltitude;
            previousMillis = currentMillis;
        }
        vTaskDelay(pdMS_TO_TICKS(VARIOMETER_TASK_DELAY_MS)); // Check more frequently than updateIntervalMs
    }
}