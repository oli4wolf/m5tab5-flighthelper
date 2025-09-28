#include <M5Unified.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "button_task.h"
#include "config.h" // For BUTTON_TASK_DELAY_MS and globalSoundEnabled

extern bool globalSoundEnabled; // Declare global sound enable flag

void buttonMonitorTask(void *pvParameters) {
    ESP_LOGI("buttonMonitorTask", "Task started.");
    while (true) {
        M5.update(); // Update M5Unified internal states for button presses
        if (M5.BtnC.wasPressed()) { // Assuming BtnC is on the right side
            globalSoundEnabled = !globalSoundEnabled;
            ESP_LOGI("buttonMonitorTask", "Physical button C pressed. globalSoundEnabled: %s", globalSoundEnabled ? "true" : "false");
        }
        vTaskDelay(pdMS_TO_TICKS(BUTTON_TASK_DELAY_MS)); // Small delay to yield to other tasks
    }
}

void initButtonMonitorTask() {
    // Any specific initialization for the button task can go here if needed.
    // For now, it's mainly about creating the FreeRTOS task.
    ESP_LOGI("initButtonMonitorTask", "Button monitor task initialized.");
}