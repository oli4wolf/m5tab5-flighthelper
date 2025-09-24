#ifndef VARIOMETER_TASK_H
#define VARIOMETER_TASK_H

#include <Arduino.h>

#ifdef __cplusplus
extern "C" {
#endif

void initVariometerTask();
void variometerTask(void *pvParameters);
void updateDisplayWithTelemetry(float pressure, float temperature, float baroAltitude, float verticalSpeed);

#ifdef __cplusplus
}
#endif

#endif // VARIOMETER_TASK_H