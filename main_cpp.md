/**
 * @file main.cpp
 * @author your name (you@domain.com)
 * @brief Flight-Helper library with maps and aviation obstacles.
 * @version 0.1
 * @date 2023-01-30
 *
 * @copyright Copyright (c) 2023
 *
 * Basic Principles
 * 1. First we initialise a 3x3 sprite for the 256px x 256px tiles.
 * 2. Depending on the coordinates shift, we will load the necessary 3 rows of tiles.
 *
 */
#include <SD.h>
#include <M5Unified.h>
#include <M5GFX.h>

#include "coordStruct.h"
#include "calcCoords.h"
#include "shiftTileCache.h"
#include "loadTiles.h"
#include "display.h"
#include "climb.h"
#include "gps.h"

#include "globalVariables.h"

M5GFX lcd;
static constexpr const gpio_num_t SDCARD_CSPIN = GPIO_NUM_4;

// ZOOM Level
const int min_zoom_level = 12;
const int max_zoom_level = 16;

// Initial Coordinates
st_idx_coords curr_gps_idx_coords = {0, 0};
st_idx_coords display_center_idx_coords = {0, 0};

// 1. Initialize the Canvas cache for 3x3 tiles
const int tile_size = 256; // Global
const int n_sprite_x = 3;
const int n_sprite_y = 3;
const int n_sprite = n_sprite_x * n_sprite_y;
int zoom = 15;            // Global
LGFX_Sprite canvas(&lcd); // screen buffer // Global

sprite_struct *tile_cache[n_sprite];

// Async. Variables
#define LEN_QUEUE n_sprite * 2
QueueHandle_t update_tile_queue;

/**
 * @brief Method used to draw the tiles on the screen with the correct offset and only semaphoreto block if tile is not locked.
 * Unclear why semaphore...
 *
 * @param tile_cache
 * @param idx_coords
 */
void pushTileCache(sprite_struct *tile_cache[], st_idx_coords &idx_coords)
{
  int i, offset_x, offset_y;
#ifdef __printTileCache__
  ESP_LOGD("pushTileCache", "idx_coords=(%d,%d) which is tile=(%d,%d), idx_on_tile=(%d,%d)\n", idx_coords.idx_x, idx_coords.idx_y, idx_coords.idx_x / tile_size, idx_coords.idx_y / tile_size, idx_coords.idx_x % tile_size, idx_coords.idx_y % tile_size);
#endif
  for (int i_y = 0; i_y < n_sprite_y; i_y++) // Loop over the height of tiles (3)
  {
    for (int i_x = 0; i_x < n_sprite_x; i_x++) // Loop over the width of tiles (3)
    {
      i = n_sprite_x * i_y + i_x;

      offset_x = tile_cache[i]->tile_x * tile_size - idx_coords.idx_x + lcd.width() / 2;  // Take tile x (0 coord) - coord x (bigger than 0) and Add width -> Offset_x
      offset_y = tile_cache[i]->tile_y * tile_size - idx_coords.idx_y + lcd.height() / 2; // Take tile y (0 coord) - coord y(bigger than 0) and Add height -> Offset_y
#ifdef __printTileCache__
      ESP_LOGD("pushTileCache", "  i:%i, tile=(%d,%d), offset=(%d,%d)", i, tile_cache[i]->tile_x, tile_cache[i]->tile_y, offset_x, offset_y);
#endif
      if (xSemaphoreTake(tile_cache[i]->mutex, pdMS_TO_TICKS(0)) == pdTRUE)
      {
#ifdef __printTileCache__
        ESP_LOGD("pushTileCache", "xSmaphoreTake PushSprite %d", i);
#endif
        tile_cache[i]->sprite->pushSprite(offset_x, offset_y);

        // unlock the tile
        if (xSemaphoreGive(tile_cache[i]->mutex) != pdTRUE)
        {
          ESP_LOGW("pushTileCache", ": Error in xSemaphoreGive() i=%d, zoom=%d, tile=(%d,%d)", i, tile_cache[i]->zoom, tile_cache[i]->tile_x, tile_cache[i]->tile_y);
        }
      }
      else
      {
        ESP_LOGI("pushTileCache", ": passed because the tile was locked. i=%d, zoom=%d, tile=(%d,%d)", i, tile_cache[i]->zoom, tile_cache[i]->tile_x, tile_cache[i]->tile_y);
      }
    }
  }
}

void initTileCache()
{
  ESP_LOGD("initTileCache", "heap_caps_get_free_size(MALLOC_CAP_DMA):   %8d",
           heap_caps_get_free_size(MALLOC_CAP_DMA));
  ESP_LOGD("initTileCache", "heap_caps_get_free_size(MALLOC_CAP_SPIRAM):%8d",
           heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

  initTileScreen();

  for (int i = 0; i < n_sprite; i++)
  {
    tile_cache[i] = (sprite_struct *)ps_malloc(sizeof(sprite_struct));

    tile_cache[i]->zoom = 0;
    tile_cache[i]->tile_x = 0;
    tile_cache[i]->tile_y = 0;

    tile_cache[i]->sprite = new LGFX_Sprite(&canvas);
    tile_cache[i]->sprite->setPsram(true);
    tile_cache[i]->sprite->createSprite(tile_size, tile_size);
    tile_cache[i]->sprite->fillSprite(TFT_GREEN);

    tile_cache[i]->mutex = xSemaphoreCreateMutex();

    // release mutex
    xSemaphoreGive(tile_cache[i]->mutex);
  }

  ESP_LOGI("initTileCache", "tile_cache was allocated.");
  ESP_LOGD("initTileCache", "heap_caps_get_free_size(MALLOC_CAP_DMA):   %8d",
           heap_caps_get_free_size(MALLOC_CAP_DMA));
  ESP_LOGD("initTileCache", "heap_caps_get_free_size(MALLOC_CAP_SPIRAM):%8d",
           heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

void updateTileCache(sprite_struct *tile_cache[],
                     int zoom, st_idx_coords &idx_coords,
                     int n_sprite_x, int n_sprite_y,
                     bool reset_gps_state_cursor = true)
{
  int i, tile_x, tile_y;
  st_updateTileQueueData queueData;

  st_tile_coords tile_coords;
  calcCoordsIdx2Tile(tile_coords, idx_coords, tile_size);

  int center_tile_x = tile_coords.tile_x;
  int center_tile_y = tile_coords.tile_y;

  int i_center = n_sprite_x * n_sprite_y / 2;
  int center_cache_tile_x = tile_cache[i_center]->tile_x;
  int center_cache_tile_y = tile_cache[i_center]->tile_y;

  int tile_shift_x = center_cache_tile_x - center_tile_x;
  int tile_shift_y = center_cache_tile_y - center_tile_y;

  if (abs(tile_shift_x) < n_sprite_x)
  {
    shiftTileCacheX(tile_cache, tile_shift_x, n_sprite_x, n_sprite_y);
  }
  if (abs(tile_shift_y) < n_sprite_y)
  {
    shiftTileCacheY(tile_cache, tile_shift_y, n_sprite_x,
                    n_sprite_y);
  }

  ESP_LOGI("updateTileCache", "updateTileCache()");
#ifdef __printTileCache__
  ESP_LOGD("updateTileCache", "  idx_coords=(%d,%d), center_tile=(%d,%d), tile_shift=(%d,%d)", idx_coords.idx_x, idx_coords.idx_y, center_tile_x, center_tile_y, tile_shift_x, tile_shift_y);
#endif
  for (int i_x = 0; i_x < n_sprite_x; i_x++)
  {
    for (int i_y = 0; i_y < n_sprite_y; i_y++)
    {
      i = n_sprite_x * i_y + i_x;
      tile_x = center_tile_x + i_x - n_sprite_x / 2;
      tile_y = center_tile_y + i_y - n_sprite_y / 2;
#ifdef __printTileCache__
      ESP_LOGD("updateTileCache", "    tile (%d,%d,%d[%d],%d[%d]) -> ", i, zoom, tile_cache[i]->tile_x, tile_x, tile_cache[i]->tile_y, tile_y);
#endif
      if (!(tile_cache[i]->zoom == zoom && tile_cache[i]->tile_x == tile_x &&
            tile_cache[i]->tile_y == tile_y))
      {
        ESP_LOGI("updateTileCache", "changed, update required. ");

        queueData.p_sprite_struct = tile_cache[i];
        queueData.zoom = zoom;
        queueData.tile_x = tile_x;
        queueData.tile_y = tile_y;

        if (uxQueueSpacesAvailable(update_tile_queue))
        {
          if (xQueueSend(update_tile_queue, &queueData, 0) == errQUEUE_FULL)
          {
            ESP_LOGE("updateTileCache", "faild to send a queue! The queue is full.");
          }
          else
          {
            ESP_LOGI("updateTileCache", "sent a queue");
          }
        }
      }
      else
      {
#ifdef __printTileCache__
        ESP_LOGD("updateTileCache", "no change, nothing to do.");
#endif
      }
    }
  }
}

void initMapVariables()
{
  ESP_LOGI("initMapVariables", "initPoint file was not detected.");
  ESP_LOGI("initMapVariables", " Default point was loaded.");

  calcCoords2CoordsIdx(curr_gps_idx_coords, 7.42911,
                       46.93647, zoom, tile_size);

  ESP_LOGI("initMapVariables", "zoom:%d, curr_gps_idx_coords idx_x:%d, idx_y:%d", zoom, curr_gps_idx_coords.idx_x, curr_gps_idx_coords.idx_y);

  display_center_idx_coords = curr_gps_idx_coords;
}

// ================================================================================
// Async tile loading
// ================================================================================
void initUpdateTileQueue()
{
  update_tile_queue = xQueueCreate(LEN_QUEUE, sizeof(st_updateTileQueueData));

  if (update_tile_queue == NULL)
  {
    ESP_LOGE("initUpdateTileQueue", "Failed in initializing update_tile_queue!");
  }
  else
  {
    ESP_LOGI("initUpdateTileQueue", "update_tile_queue was initialized.");
  }
}

void updateTileTask(void *arg)
{
  BaseType_t xStatus;
  st_updateTileQueueData queueData;
  sprite_struct *p_sprite_struct;
  int zoom, tile_x, tile_y;
  const TickType_t xTicksToWait = 50U; // about 100ms

  while (1)
  {
    // The replacement logic does not care if it was a success or not. TODO: Exception handling.
    if (xSemaphoreTake(semDrawScreen, (TickType_t)0) == pdTRUE)
    {
      // loop in xTicksToWait and
      // when data is received, this func is triggerred
      xStatus = xQueueReceive(update_tile_queue, &queueData, xTicksToWait);

      if (xStatus == pdPASS) // successful receiving
      {
        p_sprite_struct = queueData.p_sprite_struct;
        zoom = queueData.zoom;
        tile_x = queueData.tile_x;
        tile_y = queueData.tile_y;

        if (zoom == p_sprite_struct->zoom && tile_x == p_sprite_struct->tile_x && tile_y == p_sprite_struct->tile_y)
        {
#ifdef __printTileCache__
          ESP_LOGD("updateTileTask", "updateTileTask(): queue was skipped because the target tile was already up-to-date. p_sprite_struct=%d, z=%d, tile_x=%d, tile_y=%d\n", p_sprite_struct, zoom, tile_x, tile_y);
#endif
        }
        else
        {
          // HACK lock the tile
          if (xSemaphoreTake(p_sprite_struct->mutex, (TickType_t)0) == pdTRUE)
          {
            // We now have the semaphore and can access the shared resource.
            loadTile(p_sprite_struct->sprite, zoom, tile_x, tile_y);
            loadObstaclesPoints(p_sprite_struct->sprite, zoom, tile_x, tile_y);
            loadObstaclesLines(p_sprite_struct->sprite, zoom, tile_x, tile_y);

            p_sprite_struct->zoom = zoom;
            p_sprite_struct->tile_x = tile_x;
            p_sprite_struct->tile_y = tile_y;

            // unlock the tile
            if (xSemaphoreGive(p_sprite_struct->mutex) != pdTRUE)
            {
              ESP_LOGW("updateTileTask", "Error in xSemaphoreGive() p_sprite_struct=%d, z=%d, tile_x=%d, tile_y=%d\n", p_sprite_struct, zoom, tile_x, tile_y);
            }
          }
          else
          {
            ESP_LOGW("updateTileTask", "queue was passed because the tile was locked. p_sprite_struct=%d, z=%d, tile_x=%d, tile_y=%d\n", p_sprite_struct, zoom, tile_x, tile_y);
            xQueueSend(update_tile_queue, &queueData, 0);
          }
        }
      }
      xSemaphoreGive(semDrawScreen);
    }
    delay(50);
  }
}

void changeZoomLevel()
{
  int zoom_prev = zoom;
  zoom++;

  if (zoom > max_zoom_level)
  {
    zoom = min_zoom_level;
  }

  ESP_LOGD("Zoom Level", "Zoom level changed from %d to %d. ", zoom_prev, zoom);
  ESP_LOGD("Zoom Level", "display_center_idx_coords (%d,%d) -> ", display_center_idx_coords.idx_x, display_center_idx_coords.idx_y);

  // Update curr_gps_idx_coords for the new zoom level
  if (gps.location.isValid())
  {
    calcCoords2CoordsIdx(curr_gps_idx_coords, gps.location.lng(),
                         gps.location.lat(), zoom, tile_size);
  }
  else
  {
    curr_gps_idx_coords.idx_x = (int)(curr_gps_idx_coords.idx_x * pow(2.0, (double)(zoom - zoom_prev)));
    curr_gps_idx_coords.idx_y = (int)(curr_gps_idx_coords.idx_y * pow(2.0, (double)(zoom - zoom_prev)));
  }

  // Update display_center_idx_coords for the new zoom level
  display_center_idx_coords.idx_x = (int)(display_center_idx_coords.idx_x * pow(2.0, (double)(zoom - zoom_prev)));
  display_center_idx_coords.idx_y = (int)(display_center_idx_coords.idx_y * pow(2.0, (double)(zoom - zoom_prev)));

  ESP_LOGD("Zoom Level", "(%d,%d)", display_center_idx_coords.idx_x, display_center_idx_coords.idx_y);
}

void zoomHandler()
{
  // change zoom level
  ESP_LOGD("Zoom Level", "zoomHandler()");
  changeZoomLevel();

  updateTileCache(tile_cache, zoom, display_center_idx_coords, n_sprite_x,
                  n_sprite_y);
  pushTileCache(tile_cache, display_center_idx_coords);
}

void setup()
{
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  cfg.internal_imu = true;  // default=true. use internal IMU.
  cfg.internal_rtc = true;  // default=true. use internal RTC.
  cfg.internal_spk = true;  // default=true. use internal speaker.
  cfg.internal_mic = false; // default=true. use internal microphone.
  cfg.external_imu = false; // default=false. use Unit Accel & Gyro.
  cfg.external_rtc = false; // default=false. use Unit RTC.

  M5.begin(cfg);
  lcd.init();
  initGPS();
  M5.In_I2C.release();
// Wire.begin(32,33); // Port A for Pressure sensor. (32, 33) but with default env in pio config.

// Barometric Sensor (Climb Rate)
#ifdef __climb__
  init_PressureSensor();
  initClimbTimer(); // This creates a timer to read the pressure sensor into an array.
  initClimbTask();  // This creates a Timer to calculate the climb rate.
#endif

  // Initialise RTC
  M5.Rtc.setDateTime({{2022, 01, 12}, {19, 01, 02}});

  // Initialize SD Card
  if (!SD.begin(SDCARD_CSPIN, SPI, 20000000))
  { // Initialize the SD card.
    M5.Lcd.println(
        "Card failed, or not present");
    while (1)
      ;
  }
  ESP_LOGI("Setup()", "TF card initialized.");

  semDrawScreen = xSemaphoreCreateMutex();

  // Draw something to show it started and getting time for Debug action 5 Seconds.
  startupScreen();

  initTileCache();
  initUpdateTileQueue();
  xTaskCreatePinnedToCore(updateTileTask, "updateTileTask", 8192, &update_tile_queue, 1, NULL, 1);

  // Set initial map variables (The tokyo station)
  initMapVariables();

  updateTileCache(tile_cache, zoom, display_center_idx_coords, n_sprite_x,
                  n_sprite_y, false);

  pushTileCache(tile_cache, display_center_idx_coords);
}

void loop()
{
  loopGPSIDX();

  display_center_idx_coords = curr_gps_idx_coords;

  updateTileCache(tile_cache, zoom, display_center_idx_coords, n_sprite_x,
                  n_sprite_y);
  pushTileCache(tile_cache, display_center_idx_coords);

  // Draw climbing rate. Order seems to matter.
  drawClimbInfo();

  // Draw GPS Info
  drawGPSInfo();

  // Draw Everything in the sprites.
  drawCanvas();

  // TODO Improve
  M5.update();
  if (M5.BtnA.wasPressed())
  {
    ESP_LOGI("Zoom Level", "Button pressed");
    changeZoomLevel();
  }

  gpsSmartDelay(1000);
}