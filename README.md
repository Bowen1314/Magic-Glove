# Magic Glove 🪄 (XIAO ESP32-S3 BLE Air Mouse & Keyboard)

*[🇨🇳 简体中文阅读 (Read in Chinese)](README_zh.md)*

Magic Glove is a wearable Bluetooth Low Energy (BLE) Human Interface Device (HID) built on the Seeed Studio XIAO ESP32-S3. It transforms your hand into a 3D air mouse and a wireless keyboard utilizing an MPU-6050 (GY-521) Gyroscope.

## Features ✨
* **Hybrid-Acceleration Air Mouse**: Uses an advanced dual-dynamic engine (Linear + Quadratic mapping) driven by Gyroscope angular velocity. This ensures pixel-perfect precision when moving slowly, and screen-crossing snap capability when turning rapidly.
* **Dual Profiles (Mode Switching)**: Long press to switch between Air Mouse (Mode A) and Full Keyboard (Mode B).
* **Zero-Drift Auto Calibration**: Captures ambient noise and positional offset the instant you engage the mouse trigger, automatically snapping the exact zero point.
* **Thread-Safe BLE Stack**: Ported directly over to `NimBLE-Arduino`, completely eliminating memory corruption and `Core 0 panics` commonly found in traditional ESP32 BLE libraries.
* **Smart Audio Feedback**: Built-in support for low-level trigger MH-FMD buzzers to audibly indicate connection status and profile changes.

## Pin Mapping 🔌
| Pin | Component / Function |
| --- | --- |
| **I2C SCL** | `D5` (To MPU-6050) |
| **I2C SDA** | `D4` (To MPU-6050) |
| **D0**| [Mode A] Mouse Left Click / [Mode B] Key 'a' |
| **D1**| [Mode A] Mouse Right Click / [Mode B] Key 's' |
| **D2**| [Mode A] **Hold to activate Air Mouse** / [Mode B] Key 'k' |
| **D3**| Key 'l' (Both modes) |
| **D6**| MH-FMD Buzzer (I/O Signal Pin) |
| **D10**| **Hold 2s** to toggle Mode A/B |

## Quick Start 🚀
1. Clone this repository.
2. In Arduino IDE, ensure the provided `libraries/` directory is merged/installed into your Arduino workspace to use the custom thread-safe `NimBLE_Combo_Keyboard_Mouse` library.
3. Compile and upload `glove/glove.ino` to your XIAO ESP32-S3.
4. Pair with "Magic Glove CCC" via Bluetooth on your PC/Mac.
