# SHT30 主机测试

运行实际的 `components/BSP/SHT20/sht30.c`，仅用 mock 替换 ESP-IDF I2C 和 FreeRTOS 接口。无需连接开发板，不依赖额外 Python 包。每次在 100 Hz 和 1000 Hz 两种 FreeRTOS tick 配置下运行 15 个测试，共 30 项。

```powershell
python tests/run_sht30_tests.py
```

Windows 需要 64 位 Python，以及 MinGW-w64 GCC 或支持 x86-64 的普通 clang/LLD。
脚本依次使用 `--cc`、`CC` 或 PATH 中的编译器。
**Espressif 自带的 clang 仅支持嵌入式目标，不能用于 Windows 主机测试。**
本机使用已安装的 Dev-C++ MinGW-w64：

```powershell
& 'E:/Espressif/python_env/idf5.4_py3.11_env/Scripts/python.exe' tests/run_sht30_tests.py --cc 'D:/Dev-Cpp/MinGW64/bin/gcc.exe'
```

使用 clang 时，`--linker` 可以指定 `ld.lld.exe`，默认使用 clang 同目录内的文件；
GCC 自行调用配套链接器，无需此参数。Linux 可使用系统 cc/gcc/clang。
所有编译产物位于已被 Git 忽略的 `build/sht30_host_tests/`。

覆盖内容：

- 默认和备选地址、7 位地址及 100 kHz 配置；非法参数、重复初始化、未初始化读取。
- 软复位与单次测量命令、调用顺序、100 ms 通信超时，以及考虑 tick 边界后的最短等待时间。
- 官方 CRC 向量 `BE EF -> 92`、温湿度换算上下边界、温度和湿度各自 CRC 错误。
- 各类失败均不覆盖调用者的输出值；添加设备、发送、接收、移除设备的错误返回和重试。
- 复位失败后的释放，释放失败时保留句柄，重新释放及初始化。

测试失败时会输出用例名及 `tests/test_sht30.c` 中的断言行号，并以非零状态退出。主机测试验证驱动逻辑；实际引脚配置、ESP-IDF 接口兼容性须通过固件编译验证，接线和真实传感器响应须上板验证。
