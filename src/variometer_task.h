#ifndef VARIOMETER_TASK_H
#define VARIOMETER_TASK_H

#include <Arduino.h>

#ifdef __cplusplus
extern "C" {
#endif

void initVariometerTask();
void variometerAudioTask(void *pvParameters);

#ifdef __cplusplus
}
#endif

#endif // VARIOMETER_TASK_H