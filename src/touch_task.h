#ifndef TOUCH_TASK_H
#define TOUCH_TASK_H

#ifdef __cplusplus
extern "C" {
#endif

void initTouchMonitorTask();
void touchMonitorTask(void *pvParameters);
void handleSoundButtonPress(int x, int y);
void handleHikeButtonPress(int x, int y);
void handleBikeButtonPress(int x, int y);

#ifdef __cplusplus
}
#endif

#endif // TOUCH_TASK_H