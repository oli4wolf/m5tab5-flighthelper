#include "variometer_task.h"
#include <M5Unified.h>
#include "sensor_task.h"     // For globalPressure and xSensorMutex
#include "gps_task.h"        // For global GPS variables and mutex
#include "tile_calculator.h" // For tile calculation functions
#include "gui.h"             // For M5.Display functions
#include <freertos/semphr.h>
#include <math.h> // For pow()
#include "config.h" // Include configuration constants

// Declare extern global variables from main.cpp
extern float globalPressure;
extern float globalTemperature; // Added for global temperature
extern SemaphoreHandle_t xSensorMutex;

// GPS global variables (declared extern as they are defined in main.cpp)
extern double globalLatitude;
extern double globalLongitude;
extern double globalAltitude;
extern unsigned long globalSatellites;
extern unsigned long globalHDOP;
extern bool globalValid; // Indicates if a valid GPS fix is available
extern double globalSpeed; // Added for GPS speed in km/h
extern SemaphoreHandle_t xGPSMutex;

// Global variables for tile coordinates (declared extern as they are defined in main.cpp)
extern SemaphoreHandle_t xPositionMutex;
extern int globalTileX;
extern int globalTileY;
extern int globalTileZ;
extern const int TILE_SIZE; // Standard size for map tiles (e.g., OpenStreetMap)

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

void variometerTask(void *pvParameters) {
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

            previousAltitude = currentAltitude;
            previousMillis = currentMillis;
        }

        // Display update logic (moved from main.cpp)
        float currentPressure = 0;
        float currentTemperature = 0;
        double currentLatitude = 0;
        double currentLongitude = 0;
        double currentAltitude = 0;
        unsigned long currentSatellites = 0;
        unsigned long currentHDOP = 0;
        double currentSpeed = 0;   // Added for current speed
        bool currentValid = false; // Added for GPS fix status

        int currentTileX = 0;
        int currentTileY = 0;
        int currentTileZ = 0;

        float currentBaroAltitude = 0;
        float currentVerticalSpeed = 0;

        if (xSemaphoreTake(xSensorMutex, portMAX_DELAY) == pdTRUE)
        {
            currentPressure = globalPressure;
            currentTemperature = globalTemperature;
            xSemaphoreGive(xSensorMutex);
        }

        if (xSemaphoreTake(xGPSMutex, portMAX_DELAY) == pdTRUE)
        {
            currentLatitude = globalLatitude;
            currentLongitude = globalLongitude;
            currentAltitude = globalAltitude;
            currentSatellites = globalSatellites;
            currentHDOP = globalHDOP;
            currentSpeed = globalSpeed; // Get current speed
            currentValid = globalValid; // Get GPS fix status
            xSemaphoreGive(xGPSMutex);
        }

        if (xSemaphoreTake(xVariometerMutex, portMAX_DELAY) == pdTRUE)
        {
            currentBaroAltitude = globalAltitude_m;
            currentVerticalSpeed = globalVerticalSpeed_mps;
            xSemaphoreGive(xVariometerMutex);
        }

        if (!currentValid)
        {
            M5.Display.setCursor(0, 0);
            M5.Display.clear(TFT_BLACK);
            M5.Display.printf("Waiting for GPS fix...\n");
            // No delay here, as the task already has a delay at the end of the loop
        }
        else
        {
            // Calculate tile coordinates
            currentTileZ = calculateZoomLevel(currentSpeed, M5.Display.width(), M5.Display.height());
            latLngToTile(currentLatitude, currentLongitude, currentTileZ, &currentTileX, &currentTileY);

            // Update global tile coordinates (if needed for other tasks)
            ESP_LOGI("Variometer", "Attempting to take xPositionMutex. Handle: %p", (void*)xPositionMutex);
            if (xSemaphoreTake(xPositionMutex, portMAX_DELAY) == pdTRUE)
            { // Using GPS mutex for tile data as well
                globalTileX = currentTileX;
                globalTileY = currentTileY;
                globalTileZ = currentTileZ;
                ESP_LOGI("TileCalc", "Tile X: %d, Tile Y: %d, Zoom: %d", globalTileX, globalTileY, globalTileZ);
                xSemaphoreGive(xPositionMutex);
            }

            updateDisplayWithTelemetry(currentPressure, currentTemperature, currentBaroAltitude, currentVerticalSpeed, currentLatitude, currentLongitude, currentAltitude, currentSatellites, currentHDOP, currentSpeed, currentTileX, currentTileY, currentTileZ);
        }
        vTaskDelay(pdMS_TO_TICKS(VARIOMETER_TASK_DELAY_MS)); // Check more frequently than updateIntervalMs
    }
}

void updateDisplayWithTelemetry(float pressure, float temperature, float baroAltitude, float verticalSpeed, double latitude, double longitude, double altitude, unsigned long satellites, unsigned long hdop, double speed, int tileX, int tileY, int tileZ){
    M5.Display.setCursor(0, 0);
    M5.Display.fillRect(0, 0, 720, 256, TFT_BLACK); // Clear the area for new text int32_t x, int32_t y, int32_t w, int32_t h
    M5.Display.printf("Pressure: %.2f hPa\n", pressure);
    M5.Display.printf("Temperature: %.2f C\n", temperature); // Display temperature
    M5.Display.printf("Baro Alt: %.1f m\n", baroAltitude);
    M5.Display.printf("Vario: %.1f m/s\n", verticalSpeed);
}