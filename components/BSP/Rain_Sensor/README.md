# 雨滴传感器（DO 数字检测）

驱动在本目录的 `rain_sensor.c` 和 `rain_sensor.h` 中，已加入 BSP 编译。
应用示例在 `main/main.c` 的 `rain_sensor_task()` 中。

## 默认接线

以下是暂定配置，需与实际模块和接线核对。

| 模块接口 | ESP32 接口 |
| --- | --- |
| VCC | 3V3（先确认模块支持 3.3V 供电） |
| GND | GND |
| DO | GPIO27 |
| AO | 本驱动不使用 |

DO 输入电平需兼容 ESP32 的 3.3V GPIO，不能直接接入 5V 输出。
默认低电平表示有雨；如实际模块相反，将 `main/main.c` 的
`RAIN_SENSOR_ACTIVE_LEVEL` 改为 `1`。引脚由 `RAIN_SENSOR_GPIO` 修改。

## 接口和行为

```c
ESP_ERROR_CHECK(rain_sensor_init(GPIO_NUM_27, 0));
bool raining;
ESP_ERROR_CHECK(rain_sensor_read(&raining));
```

- `rain_sensor_init()`：配置一个 DO 输入；重复初始化返回 `ESP_ERR_INVALID_STATE`。
- `rain_sensor_read()`：读取即时状态，`true` 表示有雨；先初始化再读取。
- 示例任务每 100ms 采样，连续 3 次一致才确认，首次确认和状态改变时打印日志。
- `Rain detected` 表示有雨，`No rain` 表示无雨。阈值由模块上的电位器调整（若模块配有）。
- DO 只能表示是否达到检测阈值，不是降雨量或湿度数值；本实现不读取 AO。
- 当前舵机由两个按键控制伸出/收回，尚未连接雨滴状态控制舵机。
- 输入断线与无雨无法仅靠默认 DO 电平区分，当前接口不诊断断线。

当前 ESP32 的 GPIO34～39 没有内部上下拉，如改用这些输入引脚，
需由模块或外部电阻提供稳定电平。参见
[ESP-IDF GPIO 文档](https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32/api-reference/peripherals/gpio.html)。

## 实物检查

在 ESP-IDF 终端执行 `idf.py build`，再按实际串口烧录和打开串口监视器。
保持探测板干燥，确认输出 `No rain`；滴水后应输出 `Rain detected`；
擦干后应恢复 `No rain`。若始终不变，检查 DO/AO 是否接错、共地、有效电平和模块阈值。
目前只做软件编译验证，未烧录或进行实物滴水测试。
