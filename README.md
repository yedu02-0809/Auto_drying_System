# Auto_drying_System
# 🌤️ 自动晾晒收衣系统 | Automatic Clothes Drying & Retrieval System

## 中文介绍

本项目基于 ESP32 和 FreeRTOS，旨在实现一套低成本的自动晾晒收衣系统。通过采集温湿度、光照和雨滴信息，结合定时设置，自动判断晾晒与收衣时机，并计划提供手机、电脑均可访问的局域网网页控制界面。

当前使用 SG90 舵机模拟衣架的推出与收回，不涉及真实衣架承重和机械传动。

### 计划功能

- 使用 SHT30、BH1750 和雨滴传感器采集环境信息。
- 检测到下雨时自动收回，满足光照和时间条件时自动推出。
- 使用 DS3231 实现定时收衣。
- 通过局域网网页查看状态、切换模式及手动控制。
- 使用 FreeRTOS 管理采集、控制、显示和通信任务。

### 当前进度

已完成舵机驱动模块和 FreeRTOS 测试任务，实现 PWM 控制与目标角度渐变，已通过 ESP-IDF 5.4.4 编译验证，待实物测试。其余功能正在开发中。

## English Introduction

This project aims to build a low-cost automatic clothes drying and retrieval system using ESP32 and FreeRTOS. It will combine temperature, humidity, light, and rain sensing with scheduled control to determine when to extend or retract a clothes rack. A local web interface is planned for access from phones and computers.

The current prototype uses an SG90 servo to simulate rack movement. It does not carry actual laundry or include a full mechanical drive.

### Planned Features

- Environmental sensing with SHT30, BH1750, and a rain sensor.
- Automatic retraction when rain is detected, and extension when light and scheduling conditions permit.
- Scheduled retrieval using a DS3231 real-time clock.
- Status monitoring, mode switching, and manual control through a local web interface.
- FreeRTOS tasks for sensing, control, display, and communication.

### Current Status

The servo driver and FreeRTOS test task have been implemented, providing PWM control and gradual target-angle changes. The project builds successfully with ESP-IDF 5.4.4; hardware testing is pending. Other features are under development.
