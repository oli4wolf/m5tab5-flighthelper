#ifndef SENSOR_TASK_H
#define SENSOR_TASK_H

#include <Arduino.h> // Required for Wire.h and other Arduino types
#include <Wire.h>    // Required for I2C communication

#ifdef __cplusplus
extern "C" {
#endif

void initSensorTask();
void sensorReadTask(void *pvParameters);

#ifdef __cplusplus
}
#endif

#endif // SENSOR_TASK_H