# SHT30 温湿度传感器

本目录沿用项目预建的 `SHT20` 文件夹名，实际代码是 `sht30.c` 和 `sht30.h`，
使用 **SHT30 / SHT3x-DIS** 协议，不适用于 SHT20。

## 默认接线与配置

| SHT30 模块 | ESP32 |
| --- | --- |
| VCC / VDD | 3V3 |
| GND / VSS | GND |
| SDA | GPIO21 |
| SCL | GPIO22 |
| ADDR（若引出） | GND，对应 7 位地址 `0x44` |

SDA、SCL 需要上拉到 3.3V；可使用模块自带的上拉，没有时可各接一只 4.7kΩ 电阻。
示例启用内部弱上拉，但它不能替代合适的外部 I2C 上拉。
ADDR 接高电平时地址为 `0x45`，不可悬空；有些模块已经固定了 ADDR。
使用模块前核对其引脚标签及板载电路。

在 `main/main.c` 修改 `SHT30_SDA_GPIO`、`SHT30_SCL_GPIO`、`SHT30_ADDRESS`
即可调整接线。当前使用 I2C0，设备时钟为 100kHz，每次读取后等待 2 秒再采样。
SHT30 任务只输出温湿度，原有雨滴检测与舵机往返测试仍独立运行。

## 驱动接口

调用者通过 ESP-IDF 新版 `i2c_new_master_bus()` 创建总线，再传入驱动：

```c
sht30_t sensor = {0};
esp_err_t err = sht30_init(&sensor, bus, SHT30_I2C_ADDRESS_DEFAULT);
if (err == ESP_OK) {
    float temperature_c;
    float humidity_percent;
    err = sht30_read(&sensor, &temperature_c, &humidity_percent);
    // 只有 err == ESP_OK 时，才使用本次温湿度结果。
}
// 使用完后调用 sht30_deinit(&sensor)，再由总线所有者释放总线。
```

- `sht30_init()`：添加 I2C 设备，等待上电稳定，发送 `0x30A2` 软复位并等待完成。
- `sht30_read()`：发送 `0x2400`，执行高重复度单次测量；等待至少 20ms 后读取 6 字节。
- 温度与湿度各校验一次 CRC-8（多项式 `0x31`、初值 `0xFF`），两者都通过才更新输出。
- 温度为摄氏度，相对湿度为 `%RH`。CRC 错误返回 `ESP_ERR_INVALID_CRC`，通信错误直接返回底层错误。
- 初始化失败会清理设备；若清理也失败，会保留句柄，需 `sht30_deinit()` 成功后再初始化。
- 对同一对象应在同一任务内调用；调用是同步的，不能用于中断或并发读写。
- 总线归调用者管理。后续其他传感器可复用该总线，不要在同一 I2C 端口重复创建总线，
  也不要混用新版 `i2c_master.h` 与旧版 `i2c.h` 驱动。

主程序在设备未接入或读数失败时记录错误并在下个采样周期重试，不因传感器故障主动重启 ESP32。
若 I2C 总线创建失败，则记录错误并退出 SHT30 任务。

## 验证

软件测试运行方法见项目 `tests/README.md`，测试编译并执行真实的 `sht30.c`，
以模拟 I2C 和 FreeRTOS 接口验证协议和错误处理。完整固件在 ESP-IDF 终端执行 `idf.py build`。

实物验收：正确接线、烧录并打开串口监视器后，应看到 `sht30` 输出温度和湿度。
检查读数是否合理；断开传感器应输出通信错误，重新连接后应恢复读取。
软件测试不能验证实际接线、上拉、地址选择和传感器精度，仍需上板确认。

协议依据：[Sensirion SHT3x-DIS 数据手册](https://sensirion.com/media/documents/213E6A3B/63A5A569/Datasheet_SHT3x_DIS.pdf)，
第 7、9、10、12～14 页；I2C 接口依据本机 ESP-IDF v5.4.4 的 `driver/i2c_master.h`。
