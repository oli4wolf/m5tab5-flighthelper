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
#include "gui.h"             // Include the new GUI header
#include "variometer_task.h" // Include the new variometer task header
#include "button_task.h"     // Include the new button task header
#include "touch_task.h"      // Include the new touch task header
#include "config.h"         // Include configuration constants

// global variables (define variables to be used throughout the program)
EventGroupHandle_t xGuiUpdateEventGroup; // Declare the event group handle
bool globalSoundEnabled = true; // Define global sound enable flag
int globalManualZoomLevel = 0; // Define global manual zoom level, initialized to 0
bool globalTwoFingerGestureActive = false; // New: Flag for active two-finger gesture
int globalMapOffsetX = 0; // New: Manual map offset in pixels
int globalMapOffsetY = 0; // New: Manual map offset in pixels
bool globalManualMapMode = false; // New: Flag to indicate if map is in manual drag mode
float globalPressure;
float globalTemperature;
SemaphoreHandle_t xSensorMutex;

// Variometer global variables
extern float globalAltitude_m;
extern float globalVerticalSpeed_mps;
extern SemaphoreHandle_t xVariometerMutex;

// GPS global variables
double globalLatitude = 46.947597;
double globalLongitude = 7.440434;
double globalAltitude = 542.5; // Initial altitude set to Bern, Switzerland
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
int globalTileZ = DEFAULT_MAP_ZOOM_LEVEL; // Initialize to default zoom level

extern const int TILE_SIZE; // Standard size for map tiles (e.g., OpenStreetMap)

const int APP_CPU_NUM = 1; // Define the core number for the application CPU (ESP32 has two cores: PRO_CPU_NUM=0 and APP_CPU_NUM=1)

// SD Card variables
extern const int SD_CMD_PIN; // GPIO number for SD card CMD pin
extern const int SD_CLK_PIN; // GPIO number for SD card CLK pin
extern const int SD_D0_PIN;  // GPIO number for SD card D0 pin
extern const int SD_D1_PIN;  // GPIO number for SD card D1 pin
extern const int SD_D2_PIN;  // GPIO number for SD card D2
extern const int SD_D3_PIN;  // GPIO number for SD card D3 pin

// Task Stack Sizes
extern const int SENSOR_TASK_STACK_SIZE;
extern const int GPS_TASK_STACK_SIZE;
extern const int VARIOMETER_TASK_STACK_SIZE;
extern const int IMAGE_MATRIX_TASK_STACK_SIZE;
extern const int BUTTON_TASK_STACK_SIZE; // New: Stack size for button monitoring task
extern const int TOUCH_TASK_STACK_SIZE; // New: Stack size for touch monitoring task

// Function to draw a Jpeg image from SD card
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

  initSensorTask();     // Initialize the sensor task components
  initGPSTask();        // Initialize the GPS task components
  initVariometerTask(); // Initialize the variometer task components
  initButtonMonitorTask(); // Initialize the button monitor task components
  initTouchMonitorTask(); // Initialize the touch monitor task components
  initSoundButton();     // Initialize the sound button components

  xSensorMutex = xSemaphoreCreateMutex();     // Initialize the sensor mutex
  xGPSMutex = xSemaphoreCreateMutex();        // Initialize the GPS mutex
  xPositionMutex = xSemaphoreCreateMutex();   // Initialize the position mutex
  xGuiUpdateEventGroup = xEventGroupCreate(); // Initialize the GUI update event group

  M5.Display.setTextSize(3);              // change text size
  M5.Display.print("Hello World!!!");     // display Hello World! and one line is displayed on the screen
  ESP_LOGI("main.cpp", "Hello World!!!"); // display Hello World! and one line on the serial monitor

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
      SENSOR_TASK_STACK_SIZE,             // Stack size (bytes)
      NULL,             // Parameter to pass to function
      1,                // Task priority (0 to configMAX_PRIORITIES - 1)
      NULL,             // Task handle
      APP_CPU_NUM);     // Core where the task should run (APP_CPU_NUM or PRO_CPU_NUM)

  // Create and start the GPS reading task
  xTaskCreatePinnedToCore(
      gpsReadTask,   // Task function
      "GPSReadTask", // Name of task
      GPS_TASK_STACK_SIZE,          // Stack size (bytes)
      NULL,          // Parameter to pass to function
      1,             // Task priority (0 to configMAX_PRIORITIES - 1)
      NULL,             // Task handle
      APP_CPU_NUM);     // Core where the task should run (APP_CPU_NUM or PRO_CPU_NUM)

  // Create and start the variometer audio task
  xTaskCreatePinnedToCore(
      variometerTask,   // Task function
      "VariometerTask", // Name of task
      VARIOMETER_TASK_STACK_SIZE,                  // Stack size (bytes)
      NULL,                  // Parameter to pass to function
      1,                     // Task priority (0 to configMAX_PRIORITIES - 1)
      NULL,             // Task handle
      APP_CPU_NUM);     // Core where the task should run (APP_CPU_NUM or PRO_CPU_NUM)

  // Create and start the button monitoring task
  xTaskCreatePinnedToCore(
      buttonMonitorTask,   // Task function
      "ButtonMonitorTask", // Name of task
      BUTTON_TASK_STACK_SIZE,             // Stack size (bytes)
      NULL,             // Parameter to pass to function
      1,                // Task priority (0 to configMAX_PRIORITIES - 1)
      NULL,             // Task handle
      APP_CPU_NUM);     // Core where the task should run (APP_CPU_NUM or PRO_CPU_NUM)

  // Create and start the touch monitoring task
  xTaskCreatePinnedToCore(
      touchMonitorTask,   // Task function
      "TouchMonitorTask", // Name of task
      TOUCH_TASK_STACK_SIZE,             // Stack size (bytes)
      NULL,             // Parameter to pass to function
      1,                // Task priority (0 to configMAX_PRIORITIES - 1)
      NULL,             // Task handle
      APP_CPU_NUM);     // Core where the task should run (APP_CPU_NUM or PRO_CPU_NUM)

  // Create and start the image drawing task
  xTaskCreatePinnedToCore(
      drawImageMatrixTask,   // Task function
      "ImageMatrixTask", // Name of task
      IMAGE_MATRIX_TASK_STACK_SIZE,             // Stack size (bytes)
      NULL,             // Parameter to pass to function
      1,                // Task priority (0 to configMAX_PRIORITIES - 1)
      NULL,             // Task handle
      APP_CPU_NUM);     // Core where the task should run (APP_CPU_NUM or PRO_CPU_NUM)
}

// loop function is executed repeatedly for as long as it is running.
// loop function acquires values from sensors, rewrites the screen, etc.
void loop()
{
  vTaskDelay(1000 / portTICK_PERIOD_MS);
}