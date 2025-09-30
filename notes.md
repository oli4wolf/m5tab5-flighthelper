# Ideas for next step #
Partial Updates: Only redraw the parts of the screen that have changed. This significantly reduces the rendering time and improves perceived responsiveness.<br>
Watchdog Timers: Enable and properly configure the FreeRTOS and system watchdog timers. If a task gets stuck, the watchdog will reset the ESP32, preventing a complete freeze.<br>
A sensor task reads temperature and humidity, then sends a struct containing this data to a queue. The UI task reads from the queue and updates the display.<br>