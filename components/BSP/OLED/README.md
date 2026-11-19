# 0.96 寸 OLED（SSD1306，128×64，I2C）

驱动位于本目录的 `ssd1306.c`、`ssd1306.h`，字模在 `ssd1306_font.h`。
界面排版在 `main/display.c`，由 `main/environment.c` 的采集任务每 500 ms 刷新一次。

## 接线

| OLED 模块 | ESP32 |
| --- | --- |
| GND | GND |
| VCC | 3V3 |
| SCL | GPIO22 |
| SDA | GPIO21 |

模块背面丝印顺序就是 `GND VCC SCL SDA`。与 DS3231（`0x68`）、SHT30（`0x44`）、
BH1750（`0x23`）共用 I2C0，地址 `0x3C`（有些模块是 `0x3D`，对应头文件里的
`SSD1306_ADDRESS_ALTERNATE`）。不要单独再建一条总线。

## 屏幕内容

192×64 分成 4 行、每行 16 个字符（8×16 字模）：

```text
05-01 12:34 AUTO      ← 月-日 时:分 + 模式（AUTO/MAN）；无 RTC 时显示 NO RTC
23.5C 41.5%RH         ← SHT30 温湿度；不可用时显示 NO CLIMATE DATA
1235lx RAIN:NO        ← BH1750 光照 + 雨滴状态（RAIN:YES / RAIN:NO / RAIN:--）
RACK:RETRACT W        ← 衣架状态 + Wi-Fi 已连接标记（W）
```

`RACK:` 后可能是 `UNKNOWN`、`EXTEND`、`RETRACT`、`MOV:EXT`、`MOV:RET`；
光照超过 99999 lx 时按 99999 显示。刷新失败（屏幕掉线）会释放设备，
5 秒后重新初始化，不影响采集和按键控制。

## 驱动接口

```c
static ssd1306_t display;               /* 约 1 KB，不要放任务栈 */
ESP_ERROR_CHECK(ssd1306_init(&display, bus, SSD1306_ADDRESS_DEFAULT));

ssd1306_clear(&display);
ssd1306_draw_text(&display, 0, 0, "05-01 12:34 AUTO");   /* line 0..3, column 0..15 */
ssd1306_flush(&display);                                   /* 8 页 × 128 字节 */
```

- `ssd1306_init()`：添加 I2C 设备（400 kHz），等待 100 ms 上电稳定，下发初始化序列，
  清屏并刷新一次。失败时尝试删除设备，可直接重试。
- `ssd1306_draw_text()`：只改显存，不访问 I2C；超出 16 列的字符被截断，
  非可打印字符显示为 `?`。
- `ssd1306_flush()`：按页写整屏，共 8 次命令 + 8 次 128 字节数据传输。
- 初始化序列与本项目原来 STM32 工程一致，但**修正了顺序**：原来的代码先 `0xAF` 开显示、
  才 `0x8D/0x14` 开电荷泵，3.3 V 供电时可能黑屏；这里先开电荷泵再开显示。

## 字模

`ssd1306_font.h` 的 8×16 点阵取自你原来的 STM32 工程
`E:\STM32project\4-1LED显示屏\Hardware\OLED_Font.h`（江协科技字模库），
只保留 0x20–0x7E 的 ASCII 部分，共 95 个字符 ×16 字节。

## 验证

```powershell
python tests/run_ssd1306_tests.py --cc 'D:/Dev-Cpp/MinGW64/bin/gcc.exe'
python tests/run_display_tests.py --cc 'D:/Dev-Cpp/MinGW64/bin/gcc.exe'
```

13 个驱动测试用 mock I2C 检查设备配置、完整初始化命令序列、8 页刷新的命令与数据分帧、
非法参数、添加/删除失败后的重试、字模写入位置、16 列截断和 `?` 替换；
8 个排版测试检查 4 行文本在各种状态下的内容，以及完整渲染路径（清屏→绘制→刷新）。
产物在已忽略的 `build/ssd1306_host_tests/`、`build/display_host_tests/`。

主机测试不能验证真实对比度、供电和 I2C 时序，仍需上板：上电后 4 行文字应清晰无重影，
温度、光照数字随环境变化，按 GPIO25 后第 4 行状态随之改变。若黑屏，先确认 VCC 是 3.3 V、
SDA/SCL 没有接反、地址是不是 `0x3D`。
