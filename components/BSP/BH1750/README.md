# BH1750 光照传感器

BH1750 测量光照强度，输出单位为 lux（lx），不能测量湿度。
项目中的温度和相对湿度由 SHT30 采集。驱动位于本目录的 `bh1750.c`、`bh1750.h`。

## 默认接线

| BH1750 模块 | ESP32 |
| --- | --- |
| VCC | 3V3 |
| GND | GND |
| SDA | GPIO21 |
| SCL | GPIO22 |
| ADDR（若引出） | 按模块说明配置低电平，地址为 `0x23` |

ADDR 高电平时地址为 `0x5C`，同时将 `main/environment.c` 中的 `BH1750_ADDRESS`
改为 `BH1750_I2C_ADDRESS_ALTERNATE`。检查模块自带的 ADDR 电阻或焊盘配置。
这里针对已包含必要外围电路的传感器模块；若使用裸芯片，应按数据手册设计 DVI 上电复位电路。

BH1750、SHT30（`0x44`）、DS3231（`0x68`）共用 I2C0 和同一个采集任务，
不重复创建总线。SDA/SCL 需要上拉到 3.3V，可复用合适的模块上拉；不能上拉到 5V。
总线引脚通过 `SENSOR_SDA_GPIO`、`SENSOR_SCL_GPIO` 修改。

## 采集行为

- 初始化以 100kHz 添加 7 位 I2C 设备，设置测量时间寄存器 MTreg 为 69。
- 使用 `0x20` 单次高分辨率测量，等待至少 180ms 后读取两个字节。
  等待已考虑 FreeRTOS tick 的取整和边界，不会把典型的 120ms 当作最长转换时间。
- 将高字节、低字节拼成 16 位原始值，按 `raw / 1.2` 换算为 lux。
  固定 MTreg=69 时，原始值 `0xFFFF` 换算约为 54612.5 lx；强光下可能达到饱和。
- 单次测量完成后芯片自动进入 Power Down；每次读取都会重新发起测量。
- 应用在每次采样后间隔 2 秒再采样，成功时输出 `Light: 125.50 lx` 一类日志。
  通信失败只输出错误，在下次采样时重试，不将旧值当成新结果输出。
- 光照目前用于采集和日志显示。GPIO25 单键切换、雨滴收回和每天 18:00 收回规则照常运行。
  光照转换的等待发生在传感器任务，按键/舵机由独立任务处理。

## 接口

```c
bh1750_t sensor = {0};
esp_err_t err = bh1750_init(&sensor, bus, BH1750_I2C_ADDRESS_DEFAULT);
if (err == ESP_OK) {
    float illuminance_lux;
    err = bh1750_read_lux(&sensor, &illuminance_lux);
    // 仅当 err == ESP_OK 时使用 illuminance_lux。
}
// 使用完后 bh1750_deinit(&sensor)；总线仍由调用者管理。
```

调用者须先创建 ESP-IDF 新版 I2C master 总线。三个接口应由同一任务串行调用，
读取包含转换等待，不能在中断中调用。出错时保持调用者的光照输出不变。
初始化失败会尝试删除设备；若删除也失败，则保留句柄，先重试 `bh1750_deinit()` 再初始化。
释放设备不会删除 SHT30、RTC 共用的总线。

## 验证

测试运行方法见 `tests/README.md`，完整固件使用 `idf.py build` 编译。
上板后观察 `environment` 日志：遮住模块时数值应降低，照亮时应升高；
断开模块应报告通信错误，重新连接后应恢复光照读数，并确认 RTC、温湿度和按键仍正常。
主机测试不代替实际接线、传感器精度和机械运动验证。

协议依据：[ROHM BH1750FVI 数据手册 Rev.D（Mouser 托管）](https://www.mouser.com/datasheet/2/348/bh1750fvi-e-186247.pdf)。
