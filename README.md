# Serene

## Intro

Serene is a custom desktop clock and macro pad built around a Seeed XIAO ESP32C3, an ST7789 TFT display, and mechanical keyboard switches. It handles timekeeping via NTP, sounds alarms with a piezo buzzer, and packs a custom interface with multiple modes.

---

## PCB

Designed in KiCad.

<img width="1093" height="671" alt="PCBRT" src="https://github.com/user-attachments/assets/8e6fe643-c85a-40f5-be16-1bf3c355a81e" />

<img width="832" height="504" alt="PCB" src="https://github.com/user-attachments/assets/aae33536-b545-43c3-bb1b-34855fccce9d" />

---

## CAD

Enclosure and structural parts modeled in Fusion 360.

V1 (Had a few mistakes) 

<img width="1920" height="722" alt="caseRender1" src="https://github.com/user-attachments/assets/ec842513-048b-4a69-bca0-22babaff7916" />

<img width="1920" height="722" alt="caseRender2" src="https://github.com/user-attachments/assets/2320554a-1fe2-45a2-9f1a-231ff411f3fa" />

<img width="1920" height="722" alt="caseRender3" src="https://github.com/user-attachments/assets/494e1dff-1661-4d60-bf9c-7e042fc85e8d" />

V2 (Fixed Buttons not going down, USB port too small, added display holder and added screw threads)

<img width="1920" height="722" alt="caseNew1" src="https://github.com/user-attachments/assets/9ed767ab-3358-49d9-a8dc-1dbd6f5acaf3" />

<img width="1920" height="722" alt="casenew2" src="https://github.com/user-attachments/assets/ab55fee0-577c-42e7-b2fa-8fed49eff75b" />

---

## Firmware

Written in C++ using the Arduino framework. It manages multi-state UI rendering on the ST7789 display, button polling with debounce, time synchronization over Wi-Fi, and real-time audio playback for alarms and timers.

---

## Features

- Real-time digital clock display with NTP synchronization over Wi-Fi
- Customizable alarm system with snooze and repetition modes
- Built-in stopwatch and countdown timer
- Interactive menu system navigable via tactile physical keys
- Adjustable screen brightness and customizable display orientation

---

## BOM

- 1x Seeed XIAO ESP32C3[cite: 6]
- 4x MX-Style Keyboard Switches (customizable layout)[cite: 6]
- 4x White Blank DSA Keycaps[cite: 6]
- 4x Through-Hole 1N4148 Diodes[cite: 6]
- 1x 2.25in TFT Screen[cite: 6]
- 1x 3.3V Piezo Buzzer[cite: 6]
- 1x 2.54mm 8 Pin Male Header[cite: 6]
- 8x 20cm Female-Female Jumper Wires[cite: 6]
- 8x M3x5x4 Heatset Inserts[cite: 6]
- 4x M3x8mm Screws[cite: 6]
- 4x M3x16mm Screws[cite: 6]
