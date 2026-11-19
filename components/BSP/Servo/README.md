# SG90 舵机驱动

驱动位于本目录的 `servo.c`、`servo.h`，使用 ESP-IDF 的 LEDC 外设产生 50 Hz PWM，
用脉宽表示目标角度。占用**低速模式、定时器 0、通道 0**，整个固件只能初始化一次。

## 接线

| SERVO | ESP32 |
| --- | --- |
| 信号（橙） | GPIO18 |
| VCC（红） | 独立 5 V（与 ESP32 共地） |
| GND（棕） | GND |

SG90 堵转电流可达数百 mA，不要直接从 ESP32 的 3V3 或开发板 5 V 引脚取电，
否则容易引起复位或 Wi-Fi 异常。信号线是 3.3 V 电平，SG90 可以正常识别。
改引脚时修改 `main/main.c` 的 `SERVO_GPIO`。

## 角度与脉宽

| 项目 | 值 |
| --- | --- |
| PWM 频率 / 周期 | 50 Hz / 20000 µs |
| 脉宽范围 | 1000 µs（0°）～ 2000 µs（180°），线性映射 |
| 占空比精度 | 14 bit（`LEDC_TIMER_14_BIT`） |
| 初始化占空比 | 90°，即 1500 µs 中位 |

`SERVO_MIN_PULSE_US` / `SERVO_MAX_PULSE_US` 特意保守：先按 1000–2000 µs 验证，
**不要未经测试就改成 500–2500 µs**，否则容易顶到机械限位、烧齿轮或堵转。
实际角度必须按机构标定，应用层只使用三个角度：

- `SERVO_RETRACT_ANGLE = 45`（收回）
- `SERVO_CENTER_ANGLE = 90`（上电中位）
- `SERVO_EXTEND_ANGLE = 135`（伸出）

## 接口

```c
servo_t servo = {0};                        // 首次使用必须清零
ESP_ERROR_CHECK(servo_init(&servo, GPIO_NUM_18));   // 失败时不产生 PWM
ESP_ERROR_CHECK(servo_set_angle(&servo, 135.0f));   // 目标角度，0..180
```

- `servo_init()`：配置 LEDC 定时器和通道，输出 90° 中位。重复初始化返回
  `ESP_ERR_INVALID_STATE`；非法的 GPIO 编号返回 `ESP_ERR_INVALID_ARG`。
  失败时对象保持未初始化状态，可以直接重试。
- `servo_set_angle()`：设置占空比并立即更新。未初始化返回 `ESP_ERR_INVALID_STATE`；
  角度非有限值（NaN/Inf）或超出 0..180 返回 `ESP_ERR_INVALID_ARG`。
- 没有 `servo_deinit()`：LEDC 通道在运行期一直持有，释放后重复初始化没有实际收益。
- 驱动不阻塞、不等待舵机转到位；`servo_set_angle()` 返回 `ESP_OK` 只表示
  PWM 已下发，**不代表舵机已到达该角度**。

## 使用约束

- **单实例**：LEDC 定时器 0 / 通道 0 是全局资源，同一时刻只能有一个已初始化的 `servo_t`。
- **单任务**：`clothes_control` 任务是唯一调用者；不要在中断或其他任务里调用。
- **平滑步进**：应用层每 20 ms 改变 1°，从 45° 到 135° 约需 1.8 s，
  避免瞬间大角度跳变造成的冲击电流。
- **无位置反馈**：驱动和 SG90 都不提供实际位置，软件里的角度只是最后一次成功下发的目标值；
  堵转、断电、齿轮打滑都无法检测。需要准确位置时必须外加限位开关或位置传感器。

## 验证

```powershell
python tests/run_servo_tests.py --cc 'D:/Dev-Cpp/MinGW64/bin/gcc.exe'
idf.py build
```

12 个主机测试用 mock 的 LEDC 接口运行真实 `servo.c`，检查 50 Hz / 14 bit 定时器配置、
通道与初始 90° 占空比（1229）、0/45/90/135/180 度的占空比换算、非法 GPIO 与非法角度、
重复初始化、定时器/通道配置失败后的重试，以及 `ledc_set_duty` / `ledc_update_duty` 的
调用顺序与错误传播。运行方法见 [tests/README.md](../../../tests/README.md)。

主机测试不能验证真实 PWM 波形和机械动作，仍需上板：上电应停在中位 90°；按键后缓慢转到
45° / 135°；若方向相反，交换 `SERVO_RETRACT_ANGLE` 和 `SERVO_EXTEND_ANGLE` 的数值；
若有抖动或复位，优先检查独立供电和共地。
