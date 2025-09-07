#include <esp_system.h> // For PRO_CPU_NUM and APP_CPU_NUM
#include <Arduino.h>
#include "FS.h"        // SD Card ESP32
#include "SD_MMC.h"    // SD Card ESP32
#include <M5Unified.h> // Make the M5Unified library available to your program.
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h> // Required for mutex
#include "sensor_task.h"     // Include the new sensor task header
#include "gps_task.h"        // Include the new GPS task header
#include "tile_calculator.h" // Include the new tile calculator header

// global variables (define variables to be used throughout the program)
uint32_t count;
float globalPressure;
float globalTemperature;
SemaphoreHandle_t xSensorMutex;

// GPS global variables
double globalLatitude;
double globalLongitude;
double globalAltitude;
unsigned long globalSatellites;
unsigned long globalHDOP;
bool globalValid = false; // Indicates if a valid GPS fix is available
double globalDirection;
double globalSpeed; // Added for GPS speed in km/h
uint32_t globalTime;
SemaphoreHandle_t xGPSMutex;

// Global variables for tile coordinates
SemaphoreHandle_t xPositionMutex;
int globalTileX;
int globalTileY;
int globalTileZ;

const int APP_CPU_NUM = 1; // Define the core number for the application CPU (ESP32 has two cores: PRO_CPU_NUM=0 and APP_CPU_NUM=1)

// SD Card variables
const int SD_CMD_PIN = 44; // GPIO number for SD card CMD pin
const int SD_CLK_PIN = 43; // GPIO number for SD card CLK pin
const int SD_D0_PIN = 39;  // GPIO number for SD card D0 pin
const int SD_D1_PIN = 40;  // GPIO number for SD card D1 pin
const int SD_D2_PIN = 41;  // GPIO number for SD card D2
const int SD_D3_PIN = 42;  // GPIO number for SD card D3 pin

// Function to draw a Jpeg image from SD card
bool drawJpgFromSD(const char* filePath) {
  if (!SD_MMC.begin()) {
    ESP_LOGE("SD_CARD", "SD Card Mount Failed in drawJpegFromSD");
    M5.Display.printf("SD Card Mount Failed\n");
    return false;
  }

  File file = SD_MMC.open(filePath);
  if (!file) {
    ESP_LOGE("SD_CARD", "Failed to open file for reading: %s", filePath);
    return false;
  }

  M5.Display.drawJpgFile(SD_MMC, filePath, 0, 0); // Draw at (0,0)
  file.close();
  ESP_LOGI("SD_CARD", "Successfully drew Jpeg: %s", filePath);
  return true;
}

void listDir(fs::FS &fs, const char *dirname, uint8_t levels)
{
  ESP_LOGI("SD_CARD", "Listing directory: %s", dirname);

  File root = fs.open(dirname);
  if (!root)
  {
    ESP_LOGE("SD_CARD", "Failed to open directory");
    return;
  }
  if (!root.isDirectory())
  {
    ESP_LOGE("SD_CARD", "Not a directory");
    return;
  }

  File file = root.openNextFile();
  while (file)
  {
    if (file.isDirectory())
    {
      ESP_LOGI("SD_CARD", "  DIR : %s", file.name());
      if (levels)
      {
        listDir(fs, file.name(), levels - 1);
      }
    }
    else
    {
      ESP_LOGI("SD_CARD", "  FILE: %s SIZE: %d", file.name(), file.size());
    }
    file = root.openNextFile();
  }
}

// setup function is executed only once at startup.
// This function mainly describes the initialization process.
void setup()
{

  auto cfg = M5.config();   // Assign a structure for initializing M5Stack
  cfg.internal_imu = false; // Disable internal IMU
  cfg.internal_mic = false; // Disable internal microphone
  M5.begin(cfg);            // initialize M5 device
  M5.Ex_I2C.begin();        // Initialize I2C for MS5637 with SDA=GPIO53, SCL=GPIO54

  initSensorTask(); // Initialize the sensor task components
  initGPSTask();    // Initialize the GPS task components

  xSensorMutex = xSemaphoreCreateMutex(); // Initialize the sensor mutex
  xGPSMutex = xSemaphoreCreateMutex();    // Initialize the GPS mutex
  xPositionMutex = xSemaphoreCreateMutex(); // Initialize the position mutex
  xPositionMutex = xSemaphoreCreateMutex(); // Initialize the position mutex

  M5.Display.setTextSize(3);              // change text size
  M5.Display.print("Hello World!!!");     // display Hello World! and one line is displayed on the screen
  ESP_LOGI("main.cpp", "Hello World!!!"); // display Hello World! and one line on the serial monitor
  count = 0;                              // initialize count

  // int clk, int cmd, int d0, int d1, int d2, int d3
  SD_MMC.setPins(SD_CLK_PIN, SD_CMD_PIN, SD_D0_PIN, SD_D1_PIN, SD_D2_PIN, SD_D3_PIN); // Set SD card pins: CLK=GPIO4, CMD=GPIO2, D0=GPIO15, D1=GPIO13
  if (!SD_MMC.begin())
  {
    ESP_LOGE("main.cpp", "SD Card Mount Failed");
    return;
  }
  else
  {
    ESP_LOGD("main.cpp", "SD Card Mount Success");
    listDir(SD_MMC, "/", 0); // List directories at the root level
  }

  // Create and start the sensor reading task
  xTaskCreatePinnedToCore(
      sensorReadTask,   // Task function
      "SensorReadTask", // Name of task
      8192,             // Stack size (bytes)
      NULL,             // Parameter to pass to function
      1,                // Task priority (0 to configMAX_PRIORITIES - 1)
      NULL,             // Task handle
      APP_CPU_NUM);     // Core where the task should run (APP_CPU_NUM or PRO_CPU_NUM)

  // Create and start the GPS reading task
  xTaskCreatePinnedToCore(
      gpsReadTask,   // Task function
      "GPSReadTask", // Name of task
      4096,          // Stack size (bytes)
      NULL,          // Parameter to pass to function
      1,             // Task priority (0 to configMAX_PRIORITIES - 1)
      NULL,          // Task handle
      APP_CPU_NUM);  // Core where the task should run (APP_CPU_NUM or PRO_CPU_NUM)
}

// loop function is executed repeatedly for as long as it is running.
// loop function acquires values from sensors, rewrites the screen, etc.
void loop()
{
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

  if (!currentValid)
  {
    M5.Display.setCursor(0, 0);
    M5.Display.clear(TFT_BLACK);
    M5.Display.printf("Waiting for GPS fix...\n");
    delay(1000);
    return; // Skip the rest of the loop until we have a valid GPS fix
  }
  else
  {
    // Calculate tile coordinates
    currentTileZ = calculateZoomLevel(currentSpeed, M5.Display.width(), M5.Display.height());
    latLngToTile(currentLatitude, currentLongitude, currentTileZ, &currentTileX, &currentTileY);

    // Update global tile coordinates (if needed for other tasks)
    ESP_LOGI("main.cpp", "Attempting to take xPositionMutex. Handle: %p", (void*)xPositionMutex);
    if (xSemaphoreTake(xPositionMutex, portMAX_DELAY) == pdTRUE)
    { // Using GPS mutex for tile data as well
      globalTileX = currentTileX;
      globalTileY = currentTileY;
      globalTileZ = currentTileZ;
      ESP_LOGI("TileCalc", "Tile X: %d, Tile Y: %d, Zoom: %d", globalTileX, globalTileY, globalTileZ);
      xSemaphoreGive(xPositionMutex);
    }
  }

  M5.Display.clear(TFT_BLACK);
  M5.Display.setCursor(0, 0);


  // Draw the image from SD card
  char filePathBuffer[128]; // Buffer to hold the formatted file path
  sprintf(filePathBuffer, "/map/%d/%d/%d.jpeg", globalTileZ, globalTileX, globalTileY);
  ESP_LOGI("main.cpp", "Attempting to draw jpeg from path: %s", filePathBuffer);
  if (!drawJpgFromSD(filePathBuffer)) {
    M5.Display.printf("Failed to draw image!\n");
    ESP_LOGE("main.cpp", "Failed to draw image from SD card.");
  }
  delay(3000); // Display the image for 3 seconds

  M5.Display.clear(TFT_BLACK); // Clear again before displaying sensor data
  M5.Display.setCursor(0, 0);
  M5.Display.printf("Pressure: %.2f hPa\n", currentPressure);
  M5.Display.printf("Temperature: %.2f C\n", currentTemperature);
  M5.Display.printf("Lat: %.6f\n", currentLatitude);
  M5.Display.printf("Lng: %.6f\n", currentLongitude);
  M5.Display.printf("Alt: %.2f m\n", currentAltitude);
  M5.Display.printf("Sats: %lu, HDOP: %lu\n", currentSatellites, currentHDOP);
  M5.Display.printf("Speed: %.2f km/h\n", currentSpeed);
  M5.Display.printf("Tile X: %d, Y: %d, Z: %d\n", currentTileX, currentTileY, currentTileZ);

  count++;
  delay(1000);
}