# 收回 / 伸出策略状态机

与硬件无关的纯逻辑模块，位于本目录的 `clothes_control.c`、`clothes_control.h`。
它把「雨滴、按键、18:00 定时、当前衣架位置」转换成一条**唯一的**运动命令，
避免多个事件源各自驱动舵机造成状态错乱。整模块不包含任何 ESP-IDF 头文件，
因此可以直接在主机上编译测试。

## 职责边界

| 由本模块负责 | 不负责 |
| --- | --- |
| 去重：同一命令不重复下发 | 采样与消抖（在 `main.c` / 各驱动里） |
| 优先级：收回优先于伸出 | 舵机步进与角度换算（在 `main.c`） |
| 记录指令位置（未知/伸出/收回/运动中） | 实际位置反馈（SG90 没有） |
| 记录当天 18:00 是否已处理 | 时间读取（由 `environment_get_time()` 提供） |

## 输入与输出

```c
clothes_control_t control;
clothes_control_init(&control);

clothes_inputs_t inputs = {
    .time_valid = true,
    .date_key = 20260501,          // YYYYMMDD
    .seconds_of_day = 18 * 3600,   // 0..86399
    .rain_valid = true,
    .raining = false,
    .manual_toggle = true,         // 一次已消抖的按键
};
clothes_command_t cmd = clothes_control_update(&control, &inputs);
// CLOTHES_CMD_RETRACT / CLOTHES_CMD_EXTEND / CLOTHES_CMD_NONE
```

调用者拿到命令后驱动舵机，并在**所有步进成功下发完毕**后调用
`clothes_control_complete(&control, cmd)`；任一步失败则调用
`clothes_control_failed(&control)`。

## 判定规则

1. **雨滴上升沿**：`raining` 由假变真记一次 `rain_started`，持续有雨不重复触发。
   `rain_valid == false` 时完全忽略雨滴输入，保持上一次的已知状态。
2. **18:00**：`seconds_of_day >= 18:00` 且 `date_key > last_evening_date` 时触发，
   并**立即把当天标记为已处理**——即使此时已经收回、或舵机随后失败，当天也不会重复检查。
   日期回拨到更早的日期不会重新触发。
3. **按键**：`manual_toggle` 按当前指令位置取反——未知、已收回、正在收回 → 伸出；
   已伸出、正在伸出 → 收回。
4. **优先级**：`收回请求（按键收回 / 雨滴上升沿 / 18:00）` 优先于 `按键伸出`；
   有雨时（`raining == true`）按键伸出被忽略，且不缓存。
5. **去重**：请求与 `active_command` 相同，或请求收回而位置已是「已收回」，
   或请求伸出而位置已是「已伸出」时，返回 `CLOTHES_CMD_NONE`。

### 白天自动推出

- 条件全部满足才计时：`mode == AUTO`、时间在 `[08:00, 17:00)`、`light_valid` 且
  `lux >= 2000`、无雨、当前位置为「已收回」。
- 连续满足 `CLOTHES_AUTO_EXTEND_DWELL_SECONDS`（默认 300 s）后推出；
  任何一个条件中断、跨天或当天秒数回绕都会清零计时，必须重新累计。
- 每个日期最多自动推出一次（`last_extend_date`）；同一天手动收回后不会再自动推出，
  避免和用户操作打架。舵机失败时该日期同样视为已消耗。

阈值和窗口是头文件里的宏，需要按实际安装位置、朝向和季节标定：

```c
#define CLOTHES_AUTO_EXTEND_START_SECONDS (8U * 60U * 60U)
#define CLOTHES_AUTO_EXTEND_END_SECONDS   (17U * 60U * 60U)
#define CLOTHES_AUTO_EXTEND_LUX           2000.0f
#define CLOTHES_AUTO_EXTEND_DWELL_SECONDS 300U
```

### 自动 / 手动模式

`inputs.mode` 为 `CLOTHES_MODE_MANUAL` 时停用 18:00 和白天自动推出两条定时规则，
按键与网页命令照常工作。**雨天收回在两种模式下都生效**，因为它是产品的安全功能。

## 状态

`clothes_position_t` 描述**指令位置**：

| 值 | 含义 |
| --- | --- |
| `UNKNOWN` | 上电初始值，或上次舵机指令失败 |
| `EXTENDED` / `RETRACTED` | 对应命令的所有步进已成功下发 |
| `MOVING_EXTEND` / `MOVING_RETRACT` | 命令已下发、尚未走完 |

`clothes_control_complete()` 只接受与 `active_command` 相同的命令，防止过期的完成回调
把状态改错；`clothes_control_failed()` 把位置重置为 `UNKNOWN` 并清空 `active_command`，
下一次按键会重新从「收回」开始。位置和日期都只保存在内存中，重启后归零。

## 测试

```powershell
python tests/run_clothes_control_tests.py --cc 'D:/Dev-Cpp/MinGW64/bin/gcc.exe'
```

50 个场景，覆盖 18:00 边界（17:59:59 / 18:00:00 / 23:59:59）、提前收回后跳过、
运动中不重复、伸出后可再次自动收回、跨天/跨年、无效时间与时钟回拨、
雨滴与按键优先级、雨天锁定伸出、完成回调匹配、失败后的状态与重试规则，
以及白天自动推出的完整条件（满 5 分钟、变暗重置、下雨阻断、窗口边界、跨天重新累计、
非有限光照、失败后当天不再重试）和自动/手动模式差异。
命令与覆盖说明见 [tests/README.md](../../../tests/README.md)。
