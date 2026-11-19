# 自动晾晒收衣系统（Auto Drying System）

基于 **ESP32 + ESP-IDF v5.4.4 + FreeRTOS** 的低成本自动晾晒系统：采集温湿度、光照和雨滴信息，
按「下雨立即收回 / 每天 18:00 收回 / 按键手动切换」的规则驱动 SG90 舵机，
用舵机模拟衣架的伸出与收回。不涉及真实衣架承重和机械传动。

- 目标芯片：ESP32（D0WD 系列，240 MHz 双核，Flash 16 MB）
- 框架：ESP-IDF v5.4.4，FreeRTOS tick 1000 Hz
- 构建状态：`idf.py build` 通过
- 实物状态：**尚未上板验证**（烧录、滴水、舵机限位、RTC 后备电池均未实测）

## 功能现状

| 功能 | 状态 | 说明 |
| --- | --- | --- |
| 舵机驱动（50 Hz PWM，1° 步进） | ✅ 已完成 | 45° 收回 / 90° 中位 / 135° 伸出 |
| GPIO25 按键切换收回/伸出 | ✅ 已完成 | 30 ms 消抖，长按不重复，运动中可反向 |
| 雨滴自动收回 | ✅ 已完成 | 连续 3 次采样确认，持续有雨时禁止按键伸出 |
| DS3231 每天 18:00 自动收回 | ✅ 已完成 | 当天只触发一次；已收回或正在收回时跳过 |
| SHT30 温湿度采集 | ✅ 已完成 | 通过 `environment_get_climate()` 提供给网页 |
| BH1750 光照采集 | ✅ 已完成 | 通过 `environment_get_lux()` 参与自动推出判断 |
| 串口校时 `rtc_set` / `rtc_get` | ✅ 已完成 | 115200 波特率，`drying>` 提示符 |
| 按「光照 + 时间」自动推出 | ✅ 已完成 | 08:00–17:00 内 ≥2000 lx 持续 5 分钟、无雨且已收回，每天一次 |
| 局域网网页（状态 / 模式 / 手动控制） | ✅ 已完成 | Wi-Fi STA + HTTP，SSID 在 `idf.py menuconfig` 里配置 |
| OLED 显示（SSD1306 128×64） | ✅ 已完成 | 4 行 ×16 列，2 Hz 刷新，复用 I2C0 |
| 数据存储（AT24C32 / FAT / SPIFFS） | ❌ 未实现 | 分区表已预留 10 MB FAT + 4 MB SPIFFS |

## 硬件与接线

| 外设 | ESP32 引脚 | 说明 |
| --- | --- | --- |
| SG90 舵机信号 | GPIO18 | LEDC 低速模式，定时器 0 / 通道 0，50 Hz，1000–2000 µs |
| 收回 / 伸出按键 | GPIO25 | GPIO25 → 常开按键 → GND，内部上拉，低电平按下 |
| 雨滴模块 DO | GPIO27 | 低电平表示有雨；模块 AO 未使用；3.3 V 供电并共地 |
| DS3231 RTC | SCL GPIO22 / SDA GPIO21 | I2C0，100 kHz，地址 `0x68`，SQW/32K 不接 |
| SHT30 温湿度 | 同上 | 地址 `0x44`（ADDR 接高电平为 `0x45`） |
| BH1750 光照 | 同上 | 地址 `0x23`（ADDR 接高电平为 `0x5C`） |
| OLED 显示屏 | 同上 | 地址 `0x3C`（部分模块 `0x3D`），丝印 GND/VCC/SCL/SDA |

DS3231、SHT30、BH1750、SSD1306 共用一条 I2C0 总线和一个采集任务，不能各自重复创建总线。
SDA/SCL 必须上拉到 3.3 V（可复用模块自带的上拉），不能上拉到 5 V。

引脚集中定义在两处，改接线时同步修改：

- `main/main.c`：`SERVO_GPIO`、`KEY_GPIO`、`RAIN_SENSOR_GPIO`、`RAIN_SENSOR_ACTIVE_LEVEL`、
  `SERVO_RETRACT_ANGLE`、`SERVO_EXTEND_ANGLE`
- `main/environment.c`：`SENSOR_SDA_GPIO`、`SENSOR_SCL_GPIO`、`SHT30_ADDRESS`、`BH1750_ADDRESS`

## 系统结构

```
app_main
 ├── environment 任务（栈 4 KB，优先级 5）
 │     └── 持有共享 I2C0：每秒读 RTC、每 2 秒读 SHT30 / BH1750、每 500 ms 刷新 OLED；
 │         维护 3 秒内有效的 RTC 时间快照；注册串口 rtc_get / rtc_set
 ├── clothes_control 任务（栈 4 KB，优先级 5）
 │     └── 每 10 ms 轮询按键与雨滴（100 ms 采样、3 次确认）；
 │         调用 clothes_control 策略，按 20 ms/1° 平滑下发舵机角度；
 │         每 200 ms 发布一次网页状态快照
 └── web_portal 任务（栈 6 KB，优先级 4）
       └── 启动 Wi-Fi STA 和 HTTP 服务后自身退出；Wi-Fi 与 httpd 各自在独立任务中运行
```

`environment.h` 导出三个非阻塞查询：`environment_get_time()`（RTC 快照，超过 3 秒无效）、
`environment_get_climate()` 和 `environment_get_lux()`（传感器快照，超过 5 秒或最近一次
读取失败即返回 `false`）。任一传感器不可用时只影响对应的自动规则，按键、雨滴和网页控制照常工作。

## 控制规则

- **收回优先**：同一轮同时出现收回和伸出请求时先收回，被忽略的伸出不排队。
- **18:00 定时收回**：当地时间到 18:00 当天首次检查触发收回；已收回或正在收回时只记录跳过。
  08:00 之后手动再次伸出，当晚不会被重复收回，次日 18:00 重新检查。
- **白天自动推出**：08:00–17:00 内、无雨、衣架当前已收回，且光照连续 5 分钟不低于
  2000 lx 时推出；每天最多自动推出一次，条件中断（变暗 / 下雨 / 中途被收回）会重置计时。
  阈值是 `clothes_control.h` 里的 `CLOTHES_AUTO_EXTEND_*` 宏，需按实地朝向标定。
- **雨滴**：连续 3 次采样一致才确认；确认有雨后立即收回，并禁止伸出。
  雨停后不自动伸出，需要按键或网页命令。读取失败保留最后确认状态，不把错误当成雨停。
- **按键**：按当前状态切换——初始未知或已收回时先收回，已伸出时收回，运动中按下可反向。
- **网页**：可查看全部状态、手动收回/伸出、切换自动/手动模式。手动模式只停用
  18:00 和自动推出两条定时规则，**雨天收回始终生效**。
- **失败处理**：舵机指令出错时标记位置未知、停止继续步进，等下一次按键或新的自动事件重试，
  不把失败当成收回成功。

「已收回 / 已伸出」只是软件记录的**指令位置**，SG90 没有位置反馈，无法检测堵转或断电。
位置和当天已处理日期只保存在运行内存中，重启后位置重新视为未知。

## 目录结构

```
├── main/
│   ├── main.c                任务编排：按键、雨滴采样、舵机步进、网页状态发布
│   ├── environment.c/.h      共享 I2C0 采集、RTC/温湿度/光照快照、串口校时
│   ├── web_portal.c/.h       Wi-Fi STA + HTTP 服务、网页命令队列、自动/手动模式
│   ├── web_status.c/.h       与硬件无关的 JSON 状态生成（可主机测试）
│   ├── display.c/.h          OLED 四行排版与渲染（可主机测试）
│   └── Kconfig.projbuild     Wi-Fi SSID / 密码配置项
├── components/
│   ├── BSP/                  Servo、Key、Rain_Sensor、SHT20(SHT30)、RTC(DS3231)、BH1750、OLED 驱动
│   └── Middlewares/
│       └── clothes_control/  与硬件无关的收回/伸出策略状态机
├── tests/                    主机测试：编译真实驱动，mock 掉 ESP-IDF 接口
├── partitions-16MiB.csv      自定义分区表（app 2 MB + FAT 10 MB + SPIFFS 4 MB）
└── sdkconfig.defaults        构建默认配置，新克隆时生效
```

## 构建与烧录

前置条件：已安装 ESP-IDF v5.4.4 并执行 `export.ps1` / `. export.sh`；
模组 Flash 为 **16 MB**（`sdkconfig.defaults` 按 16 MB 配置，并引用 `partitions-16MiB.csv`）。
若板子只有 4 MB，请把 `CONFIG_ESPTOOLPY_FLASHSIZE_16MB` 改为 4 MB 并换回默认分区表，
否则烧录会因分区越界失败。

```powershell
idf.py set-target esp32
idf.py menuconfig      # Auto drying system -> Wi-Fi: 填 SSID 和密码
idf.py build
idf.py -p COM3 flash monitor
```

Wi-Fi 只支持 **2.4 GHz**；SSID 留空时网页功能自动关闭（固件也会更小），
按键、雨滴和定时控制不受影响。密码会以明文写进固件，建议开一个单独的访客网络。

串口 115200、8N1。启动后应看到 `environment`、`clothes_control` 和 `web_portal` 的日志，
以及 `drying>` 提示符。连上 Wi-Fi 后会打印 `Web portal ready at http://192.168.x.x/`，
手机或电脑连同一个网络打开这个地址即可看到网页。首次使用需要校时
（RTC 失电后 OSF 置位，定时收回被禁用）：

```text
rtc_set 2026-05-01 17:59:00
rtc_get
```

看到 `RTC set: ...` 才表示写入成功；出现 `RTC set failed` 时按提示检查模块、日期和接线。
详见 [RTC/README.md](components/BSP/RTC/README.md)。

网页提供：时间/温湿度/光照/雨滴/衣架状态、Wi-Fi 地址、收回与伸出按钮、自动/手动模式切换。
对应的 HTTP 接口是 `GET /api/status`、`POST /api/retract`、`POST /api/extend`、
`POST /api/mode?value=auto|manual`，方便用脚本或 Home Assistant 调用。

OLED 屏幕内容是 4 行：`时间 + 模式`、`温湿度`、`光照 + 雨滴`、`衣架状态 + Wi-Fi 标记`，
每 500 ms 刷新，细节见 [OLED/README.md](components/BSP/OLED/README.md)。

## 主机测试

11 套主机测试编译并运行**真实源码**，只 mock 掉 ESP-IDF 与 FreeRTOS 接口，
不需要开发板，也不需要额外 Python 包（Windows 需要 64 位 Python + MinGW-w64 GCC）：

```powershell
python tests/run_servo_tests.py --cc 'D:/Dev-Cpp/MinGW64/bin/gcc.exe'
python tests/run_rain_sensor_tests.py --cc 'D:/Dev-Cpp/MinGW64/bin/gcc.exe'
python tests/run_sht30_tests.py --cc 'D:/Dev-Cpp/MinGW64/bin/gcc.exe'
python tests/run_key_tests.py --cc 'D:/Dev-Cpp/MinGW64/bin/gcc.exe'
python tests/run_rtc_tests.py --cc 'D:/Dev-Cpp/MinGW64/bin/gcc.exe'
python tests/run_clothes_control_tests.py --cc 'D:/Dev-Cpp/MinGW64/bin/gcc.exe'
python tests/run_environment_tests.py --cc 'D:/Dev-Cpp/MinGW64/bin/gcc.exe'
python tests/run_bh1750_tests.py --cc 'D:/Dev-Cpp/MinGW64/bin/gcc.exe'
python tests/run_web_status_tests.py --cc 'D:/Dev-Cpp/MinGW64/bin/gcc.exe'
python tests/run_ssd1306_tests.py --cc 'D:/Dev-Cpp/MinGW64/bin/gcc.exe'
python tests/run_display_tests.py --cc 'D:/Dev-Cpp/MinGW64/bin/gcc.exe'
```

覆盖范围见 [tests/README.md](tests/README.md)。产物写入已被忽略的 `build/*_host_tests/`。
主机测试只验证驱动与策略逻辑，不能替代 `idf.py build`、真实接线和机械动作验证。

## 尚未完成

1. **数据存储**：AT24C32（DS3231 模块上的 EEPROM）、FAT、SPIFFS 均未使用。
2. **测试缺口**：`main/main.c` 里的雨滴确认滤波、1° 步进、命令分发和网页状态组装
   没有主机测试（写在应用层，无法被主机测试引用；建议后续抽成独立模块）。
3. **网页与 OLED 未上板验证**：`web_portal.c` 只验证到编译与链接，没有在真实电脑/手机上
   走通 HTTP；`ssd1306.c` 用 mock I2C 验证协议，真实屏幕的对比度和供电仍需实测。
4. **实物验证**：未烧录、未滴水测试，舵机方向/限位、RTC 走时与后备电池能力均待实测。

## 相关文档

- [tests/README.md](tests/README.md)：主机测试环境与覆盖范围
- [components/BSP/Servo/README.md](components/BSP/Servo/README.md)：舵机驱动
- [components/BSP/Key/README.md](components/BSP/Key/README.md)：按键驱动与单键切换规则
- [components/BSP/Rain_Sensor/README.md](components/BSP/Rain_Sensor/README.md)：雨滴传感器
- [components/BSP/SHT20/README.md](components/BSP/SHT20/README.md)：SHT30 温湿度
- [components/BSP/RTC/README.md](components/BSP/RTC/README.md)：DS3231 与 18:00 收回规则
- [components/BSP/BH1750/README.md](components/BSP/BH1750/README.md)：BH1750 光照
- [components/BSP/OLED/README.md](components/BSP/OLED/README.md)：SSD1306 显示与字模
- [components/Middlewares/clothes_control/README.md](components/Middlewares/clothes_control/README.md)：收回/伸出策略

## English

A low-cost automatic clothes drying rack controller built on ESP32 with ESP-IDF v5.4.4 and FreeRTOS.
It samples temperature/humidity (SHT30), illuminance (BH1750), rain (digital rain sensor) and
time (DS3231 RTC), then drives an SG90 servo to simulate extending or retracting the rack:
retract immediately on rain, retract daily at 18:00, extend when it is bright enough during
the day, and toggle manually with a single button on GPIO25 or from the web page.

Implemented: servo, button, rain retraction, daily 18:00 retraction, daylight extension
(08:00-17:00, >=2000 lx for 5 minutes, dry and already retracted, once per day), a local
Wi-Fi web page with status, manual extend/retract and mode switching, a 4-line SSD1306 OLED
dashboard refreshed at 2 Hz, plus eleven host test suites that compile the real sources
against mocked ESP-IDF APIs. Not implemented yet: persistent storage.

Hardware: servo signal GPIO18, button GPIO25, rain DO GPIO27, shared I2C0 on GPIO21/22
(DS3231 `0x68`, SHT30 `0x44`, BH1750 `0x23`, SSD1306 `0x3C`). Build with `idf.py build` after
setting the Wi-Fi SSID in `idf.py menuconfig`; the default configuration targets a 16 MB flash
module with the custom partition table in `partitions-16MiB.csv`.
