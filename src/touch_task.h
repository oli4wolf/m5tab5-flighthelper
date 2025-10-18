#ifndef TOUCH_TASK_H
#define TOUCH_TASK_H

#ifdef __cplusplus
extern "C" {
#endif

void initTouchMonitorTask();
void touchMonitorTask(void *pvParameters);

#ifdef __cplusplus
}
#endif

#endif // TOUCH_TASK_H