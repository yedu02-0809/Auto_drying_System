#!/usr/bin/env python3
"""生成自动晾晒系统的接线示意图（PNG）。

左侧 ESP32，右侧按顺序排列的 7 个模块，逐引脚彩色连线；
输出 docs/wiring/breadboard-wiring.png。
"""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "docs" / "wiring" / "breadboard-wiring.png"

FONT_PATH = "C:/Windows/Fonts/msyh.ttc"
W = 1600
BG = (252, 252, 250)

COLOR = {
    "3v3": (214, 48, 49),
    "gnd": (34, 34, 34),
    "5v": (41, 98, 255),
    "sda": (196, 140, 0),
    "scl": (32, 148, 84),
    "pwm": (232, 112, 20),
    "key": (232, 112, 20),
    "rain": (232, 112, 20),
}
NET_NAME = {
    "3v3": "3V3  红", "gnd": "GND  黑", "5v": "5V  蓝",
    "sda": "SDA(GPIO21) 黄", "scl": "SCL(GPIO22) 绿", "sig": "GPIO18/25/27 橙",
}

ESP_PINS = [
    ("3V3", "3v3"),
    ("GND", "gnd"),
    ("5V / VIN", "5v"),
    ("GPIO21  SDA", "sda"),
    ("GPIO22  SCL", "scl"),
    ("GPIO18  PWM", "pwm"),
    ("GPIO25  KEY", "key"),
    ("GPIO27  RAIN", "rain"),
]

MODULES = [
    ("OLED 0.96\" SSD1306", "GND VCC SCL SDA",
     ["GND", "VCC", "SCL", "SDA"],
     [("GND", "gnd"), ("VCC", "3v3"), ("SCL", "scl"), ("SDA", "sda")]),
    ("DS3231 时钟模块", "32K / SQW 不接",
     ["32K", "SQW", "SCL", "SDA", "VCC", "GND"],
     [("SCL", "scl"), ("SDA", "sda"), ("VCC", "3v3"), ("GND", "gnd")]),
    ("SHT30 温湿度", "VCC GND SCL SDA",
     ["VCC", "GND", "SCL", "SDA"],
     [("VCC", "3v3"), ("GND", "gnd"), ("SCL", "scl"), ("SDA", "sda")]),
    ("BH1750 光照", "ADDR 接地 = 0x23",
     ["VCC", "GND", "SCL", "SDA", "ADDR"],
     [("VCC", "3v3"), ("GND", "gnd"), ("SCL", "scl"), ("SDA", "sda"), ("ADDR", "gnd")]),
    ("按键（GPIO25）", "按下把 GPIO25 拉到 GND",
     ["K1", "K2"],
     [("K1", "key"), ("K2", "gnd")]),
    ("雨滴模块", "AO 不接",
     ["VCC", "GND", "DO", "AO"],
     [("VCC", "3v3"), ("GND", "gnd"), ("DO", "rain")]),
    ("SG90 舵机", "VCC 接 5V，不要接 3V3",
     ["GND", "VCC", "PWM"],
     [("GND", "gnd"), ("VCC", "5v"), ("PWM", "pwm")]),
]

PIN_STEP = 24
TOP = 200
GAP = 30


def font(size):
    return ImageFont.truetype(FONT_PATH, size)


def net_color(kind):
    return COLOR.get(kind, COLOR["pwm"])


def box_height(count):
    return 64 + count * PIN_STEP + 14


def main():
    heights = [box_height(len(m[2])) for m in MODULES]
    total = sum(heights) + GAP * (len(MODULES) - 1)
    H = TOP + total + 90

    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)
    f_title, f_sub = font(38), font(18)
    f_name, f_note = font(20), font(15)
    f_pin, f_small = font(16), font(14)

    d.text((56, 34), "自动晾晒收衣系统 · 接线图", font=f_title, fill=(28, 28, 28))
    d.text((56, 86), "左侧 ESP32，右侧每个模块的引脚按颜色接到对应 GPIO；"
                     "同色线在面包板上可以并到同一列", font=f_sub, fill=(96, 92, 88))

    lx, ly = 56, 126
    for kind in ("3v3", "gnd", "5v", "sda", "scl", "sig"):
        d.line([lx, ly + 9, lx + 40, ly + 9], fill=net_color(kind), width=5)
        d.text((lx + 50, ly), NET_NAME[kind], font=f_small, fill=(70, 66, 62))
        lx += 250

    esp = [120, 330, 560, 330 + 170 + (len(ESP_PINS) - 1) * 66 + 52]
    d.rounded_rectangle(esp, radius=14, fill=(34, 46, 52), outline=(18, 26, 30), width=2)
    d.text((esp[0] + 22, esp[1] + 18), "ESP32 DevKit V1", font=f_name, fill=(255, 255, 255))
    d.text((esp[0] + 22, esp[1] + 48), "（30 针，按丝印找 GPIO）", font=f_note, fill=(170, 195, 205))
    d.text((esp[0] + 22, esp[1] + 78), "3V3 只给传感器，", font=f_note, fill=(170, 195, 205))
    d.text((esp[0] + 22, esp[1] + 100), "舵机单独走 5V。", font=f_note, fill=(170, 195, 205))

    esp_pin_y = {}
    for i, (label, kind) in enumerate(ESP_PINS):
        y = esp[1] + 170 + i * 66
        d.text((esp[2] - 26 - d.textlength(label, font=f_pin), y - 10), label, font=f_pin,
               fill=(240, 248, 252))
        d.ellipse([esp[2] - 7, y - 7, esp[2] + 7, y + 7], fill=net_color(kind))
        esp_pin_y[kind] = y

    mod_x0, mod_x1 = 1060, 1540
    y = TOP
    for m_index, (name, note, pins, links) in enumerate(MODULES):
        h = heights[m_index]
        d.rounded_rectangle([mod_x0, y, mod_x1, y + h], radius=10,
                            fill=(232, 241, 248), outline=(96, 140, 172), width=2)
        d.text((mod_x0 + 16, y + 10), name, font=f_name, fill=(24, 60, 92))
        d.text((mod_x0 + 16, y + 40), note, font=f_note, fill=(92, 112, 128))

        pin_y = {}
        for i, pin in enumerate(pins):
            py = y + 68 + i * PIN_STEP
            pin_y[pin] = py
            d.ellipse([mod_x0 - 6, py - 6, mod_x0 + 6, py + 6], fill=(96, 140, 172))
            d.text((mod_x0 + 14, py - 9), pin, font=f_pin, fill=(30, 60, 90))

        for order, (pin, kind) in enumerate(links):
            py = pin_y[pin]
            target_y = esp_pin_y[kind]
            lane = 600 + order * 24 + m_index * 3
            d.line([(mod_x0 - 6, py), (lane, py), (lane, target_y), (esp[2] + 7, target_y)],
                   fill=net_color(kind), width=4, joint="curve")
        y += h + GAP

    img.save(OUT)
    print(f"written {OUT} ({W}x{H})")


if __name__ == "__main__":
    main()
