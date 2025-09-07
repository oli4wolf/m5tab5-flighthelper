#ifndef GPS_TASK_H
#define GPS_TASK_H

#include <Arduino.h>

#ifdef __cplusplus
extern "C" {
#endif

extern double globalLatitude;
extern double globalLongitude;
extern double globalAltitude;
extern unsigned long globalSatellites;
extern unsigned long globalHDOP;
extern double globalDirection;
extern double globalSpeed; // Declared extern for GPS speed
extern uint32_t globalTime;
extern SemaphoreHandle_t xGPSMutex;

void initGPSTask();
void gpsReadTask(void *pvParameters);

#ifdef __cplusplus
}
#endif

#endif // GPS_TASK_H