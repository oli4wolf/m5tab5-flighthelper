#pragma once

#include <cstddef> // For size_t

// Device Constants M5Stack Tab5
const int TILE_SIZE = 256; // Standard size for map tiles (e.g., Swisstopo, OpenStreetMap)
const int SCREEN_WIDTH = 720; // Width of the M5Stack screen
const int SCREEN_HEIGHT = 1280; // Height of the M5Stack screen

// Configuration
const bool SPEAKER_ENABLED = false; // Set to false to disable speaker functionality
const bool USE_TESTDATA = true; // Set to true to use test GPS data when no valid GPS fix is available
extern bool globalSoundEnabled; // Global variable to control sound output at runtime
extern bool globalTwoFingerGestureActive; // New: Flag for active two-finger gesture
extern int globalManualZoomLevel; // New: Manually set zoom level
extern bool globalManualMapMode; // New: Flag to indicate if map is in manual drag mode

// Zoom Constants
const int MIN_ZOOM_LEVEL = 1;
const int DEFAULT_MAP_ZOOM_LEVEL = 15;
const int MAX_ZOOM_LEVEL = 19;
const int ZOOM_THRESHOLD = 50; // Pixels for distance change to trigger zoom
const int TOUCH_TASK_STACK_SIZE = 4096; // Stack size for touch monitoring task
const int TOUCH_TASK_DELAY_MS = 20;    // Delay for touch monitoring task
const int DOUBLE_TAP_THRESHOLD_MS = 300; // Time in ms to detect a double tap

// SD Card variables
const int SD_CMD_PIN = 44; // GPIO number for SD card CMD pin
const int SD_CLK_PIN = 43; // GPIO number for SD card CLK pin
const int SD_D0_PIN = 39;  // GPIO number for SD card D0 pin
const int SD_D1_PIN = 40;  // GPIO number for SD card D1 pin
const int SD_D2_PIN = 41;  // GPIO number for SD card D2
const int SD_D3_PIN = 42;  // GPIO number for SD card D3 pin

// Task Stack Sizes
const int SENSOR_TASK_STACK_SIZE = 8192;
const int GPS_TASK_STACK_SIZE = 4096;
const int VARIOMETER_TASK_STACK_SIZE = 4096;
const int IMAGE_MATRIX_TASK_STACK_SIZE = 8192;
const int BUTTON_TASK_STACK_SIZE = 2048; // New: Stack size for button monitoring task
const int BUTTON_TASK_DELAY_MS = 50;    // New: Delay for button monitoring task

// GUI Constants
const size_t TILE_CACHE_SIZE_BYTES = 1 * 1024 * 1024; // 1MB cache
const int SCREEN_BUFFER_TILE_DIMENSION = 4;
const int TILE_PATH_MAX_LENGTH = 128;
const int SCREEN_BUFFER_CENTER_OFFSET = 1;
const int DRAW_GRID_DIMENSION = 3;
const int DRAW_GRID_CENTER_OFFSET = 1;
const int DRAW_IMAGE_TASK_DELAY_MS = 2000;
const int GPS_FIX_CIRCLE_RADIUS = 5;

// Arrow Constants
const int ARROW_HEAD_LENGTH = 10;
const int ARROW_HEAD_WIDTH = 10;
const int ARROW_SHAFT_LENGTH = 15;

// Direction Icon Constants
const int DIRECTION_ICON_RADIUS = 14;
const int DIRECTION_ARROW_LENGTH = 10;
const int DIRECTION_ARROW_WIDTH = 6;

const int DIR_ICON_R = DIRECTION_ICON_RADIUS;
const int DIR_ICON_EDGE_WIDTH = 3; // Example value, adjust as needed
const float DIR_ICON_ANGLE = M_PI / 6; // Example value, adjust as needed (30 degrees)

const uint16_t DIR_ICON_TRANS_COLOR = TFT_TRANSPARENT;
const uint16_t DIR_ICON_BG_COLOR = TFT_WHITE;
const uint16_t DIR_ICON_COLOR_INACTIVE = TFT_DARKGREY;
const uint16_t DIR_ICON_COLOR_ACTIVE = TFT_DARKGREEN; // For active state, if needed

const uint8_t dir_icon_palette_id_trans = 0;
const uint8_t dir_icon_palette_id_bg = 1;
const uint8_t dir_icon_palette_id_fg = 2;

// Text Zone Constants (for GPS data display)
const int TEXT_ZONE_HEIGHT = 30;
const int TEXT_ZONE_WIDTH = 106; // Approximately M5.Display.width() / 3 for a 320px wide screen
const int TEXT_ZONE_LAT_LON_X = 0;
const int TEXT_ZONE_LAT_LON_Y = 0;
const int TEXT_ZONE_SPEED_X = TEXT_ZONE_WIDTH;
const int TEXT_ZONE_SPEED_Y = 0;
const int TEXT_ZONE_ALTITUDE_X = TEXT_ZONE_WIDTH * 2;
const int TEXT_ZONE_ALTITUDE_Y = 0;

// Variometer Constants
const float STANDARD_SEA_LEVEL_PRESSURE_HPA = 1013.25;
const float ALTITUDE_CONSTANT_A = 44330.0;
const float ALTITUDE_CONSTANT_B = 5.255;
const int SPEAKER_DEFAULT_VOLUME = 64;
const unsigned long VARIOMETER_UPDATE_INTERVAL_MS = 200;
const float ALTITUDE_CHANGE_THRESHOLD_MPS = 0.5;
const int RISING_TONE_BASE_FREQ_HZ = 1000;
const int RISING_TONE_MULTIPLIER_HZ_PER_MPS = 50;
const int TONE_DURATION_MS = 50;
const int SINKING_TONE_BASE_FREQ_HZ = 500;
const int SINKING_TONE_MULTIPLIER_HZ_PER_MPS = 50;
const int MIN_TONE_FREQ_HZ = 100;
const int VARIOMETER_TASK_DELAY_MS = 50;

// Altitude Filter
const int ALTITUDE_FILTER_SIZE = 10; // Number of samples for moving average filter

// GPS Constants
const int GPS_TASK_DELAY_MS = 1000;
const int GPS_INIT_DELAY_MS = 2000;
const int GPS_FIX_TIMEOUT_MS = 10000; // 10 seconds
const int GPS_SERIAL_BAUD_RATE = 115200; //115200bps@8N1
const int GPS_SERIAL_RX_PIN = 17; // GPIO17
const int GPS_SERIAL_TX_PIN = 16; // GPIO16
const int GPS_SERIAL_MODE   = SERIAL_8N1;
const int GPS_UART = 1; // Use UART1 for GPS