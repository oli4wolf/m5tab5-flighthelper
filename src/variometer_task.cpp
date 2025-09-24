#include "variometer_task.h"
#include <M5Unified.h>
#include "sensor_task.h"     // For globalPressure and xSensorMutex
#include "gui.h"             // For M5.Display functions
#include <freertos/semphr.h>
#include <math.h> // For pow()
#include <vector> // For std::vector
#include "config.h" // Include configuration constants

// Declare extern global variables from main.cpp
extern float globalPressure;
extern float globalTemperature; // Added for global temperature
extern SemaphoreHandle_t xSensorMutex;

// Moving average filter variables
static std::vector<float> altitudeBuffer;
static size_t bufferIndex = 0;
static bool bufferFull = false;


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
    altitudeBuffer.reserve(ALTITUDE_FILTER_SIZE); // Pre-allocate memory
    for (int i = 0; i < ALTITUDE_FILTER_SIZE; ++i) {
        altitudeBuffer.push_back(0.0); // Initialize with zeros
    }
    M5.Speaker.begin(); // Initialize the speaker
    M5.Speaker.setVolume(SPEAKER_DEFAULT_VOLUME); // Set a default volume (0-255)
    ESP_LOGI("Variometer", "Variometer task initialized. Speaker enabled.");
}

void variometerTask(void *pvParameters) {
    (void) pvParameters;

    float previousAltitude = 0.0;
    unsigned long previousMillis = millis();
    const unsigned long updateIntervalMs = VARIOMETER_UPDATE_INTERVAL_MS; // Update every VARIOMETER_UPDATE_INTERVAL_MS
    const float altitudeChangeThreshold_mps = ALTITUDE_CHANGE_THRESHOLD_MPS; // meters per second for tone trigger

    // Initial altitude reading
    // Initial altitude reading and fill buffer
    if (xSemaphoreTake(xSensorMutex, portMAX_DELAY) == pdTRUE) {
        float initialPressure = globalPressure;
        xSemaphoreGive(xSensorMutex);
        float initialAltitude = pressureToAltitude(initialPressure);
        for (int i = 0; i < ALTITUDE_FILTER_SIZE; ++i) {
            altitudeBuffer[i] = initialAltitude;
        }
        previousAltitude = initialAltitude; // Use initial altitude for the first comparison
    }

    for (;;) {
        unsigned long currentMillis = millis();
        if (currentMillis - previousMillis >= updateIntervalMs) {
            float currentPressure = 0;
            if (xSemaphoreTake(xSensorMutex, portMAX_DELAY) == pdTRUE) {
                currentPressure = globalPressure;
                xSemaphoreGive(xSensorMutex);
            }

            float rawAltitude = pressureToAltitude(currentPressure);

            // Add raw altitude to buffer
            altitudeBuffer[bufferIndex] = rawAltitude;
            bufferIndex = (bufferIndex + 1) % ALTITUDE_FILTER_SIZE;
            if (bufferIndex == 0) { // Buffer has wrapped around at least once
                bufferFull = true;
            }

            // Calculate averaged altitude
            float averagedAltitude = 0.0;
            int count = bufferFull ? ALTITUDE_FILTER_SIZE : bufferIndex;
            for (int i = 0; i < count; ++i) {
                averagedAltitude += altitudeBuffer[i];
            }
            averagedAltitude /= count;

            float altitudeChange = averagedAltitude - previousAltitude;
            float timeDeltaSeconds = (float)(currentMillis - previousMillis) / 1000.0;
            float verticalSpeed = altitudeChange / timeDeltaSeconds; // meters per second

            if (xSemaphoreTake(xVariometerMutex, portMAX_DELAY) == pdTRUE) {
                globalAltitude_m = averagedAltitude;
                globalVerticalSpeed_mps = verticalSpeed;
                xSemaphoreGive(xVariometerMutex);
            }

            // Tone generation logic
            if (SPEAKER_ENABLED) {
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
            }

            previousAltitude = averagedAltitude; // Update previous altitude with the averaged value
            previousMillis = currentMillis;
        }

        // Display update logic (moved from main.cpp)
        float currentPressure = 0;
        float currentTemperature = 0;
        // GPS and Tile related variables removed

        float currentBaroAltitude = 0;
        float currentVerticalSpeed = 0;

        if (xSemaphoreTake(xSensorMutex, portMAX_DELAY) == pdTRUE)
        {
            currentPressure = globalPressure;
            currentTemperature = globalTemperature;
            xSemaphoreGive(xSensorMutex);
        }

        if (xSemaphoreTake(xVariometerMutex, portMAX_DELAY) == pdTRUE)
        {
            currentBaroAltitude = globalAltitude_m;
            currentVerticalSpeed = globalVerticalSpeed_mps;
            xSemaphoreGive(xVariometerMutex);
        }

        // Display update logic (without GPS or tile data)
        //updateDisplayWithTelemetry(currentPressure, currentTemperature, currentBaroAltitude, currentVerticalSpeed);
        vTaskDelay(pdMS_TO_TICKS(VARIOMETER_TASK_DELAY_MS)); // Check more frequently than updateIntervalMs
    }
}

void updateDisplayWithTelemetry(float pressure, float temperature, float baroAltitude, float verticalSpeed){
    M5.Display.setCursor(0, 0);
    M5.Display.fillRect(0, 0, 720, 256, TFT_BLACK); // Clear the area for new text int32_t x, int32_t y, int32_t w, int32_t h
    M5.Display.printf("Pressure: %.2f hPa\n", pressure);
    M5.Display.printf("Temperature: %.2f C\n", temperature); // Display temperature
    M5.Display.printf("Baro Alt: %.1f m\n", baroAltitude);
    M5.Display.printf("Vario: %.1f m/s\n", verticalSpeed);
    M5.Display.setCursor(0, 256); // Move cursor down for next section
}