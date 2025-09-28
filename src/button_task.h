#ifndef BUTTON_TASK_H
#define BUTTON_TASK_H

#ifdef __cplusplus
extern "C" {
#endif

void buttonMonitorTask(void *pvParameters);
void initButtonMonitorTask();

#ifdef __cplusplus
}
#endif

#endif // BUTTON_TASK_H