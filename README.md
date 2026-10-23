# 1D Pong Game v2.2 (ESP32-S3)

A fast-paced, competitive one-dimensional arcade Pong game built for the **ESP32-S3** microcontroller. The game uses a single **WS2812B LED strip** as the game track, an **SH1106 OLED display** for live scoring/menus, a synchronized buzzer for audio feedback, and dual-button setups for both players, featuring illuminated arcade buttons controlled via the **Adafruit NeoPixel** library.

This project is open-source and licensed under the **GNU GPLv3**.

---

## 🎮 Game Rules & Mechanics

### Core Gameplay
* **The Ball:** A colored illuminated segment that travels across the 1D LED track.
* **Hit Zones:** Located at both ends of the track, spanning approximately 10% of the total track length (dynamically calculated based on your `LED_COUNT`). Players must press their primary hit button **only** when the ball enters their designated zone.
* **Foul / Early Hit:** Pressing the hit button prematurely while the ball is still traveling towards the *opponent* is penalized as a foul. The opponent is immediately awarded a point.
* **Perfect Timing Bonus:** Hitting the ball within the last 3 LEDs of the track grants an explosive speed boost, making the return significantly harder to catch.

### Ultimate Smash Power-Up 🚀
Each player has access to **one Ultimate Smash per match**:
1. **Arming the Smash:** While the ball is traveling towards you (outside your hit zone), you can press your dedicated Power-Up button to "arm" the Smash. Your button indicator will signal it is ready.
2. **Execution:** Once the ball enters your hit zone, pressing the primary hit button fires a devastating, ultra-fast projectil (`SMASH_SPEED`) towards your opponent.
3. **The "Hot Potato" Dynamic:** If the opponent successfully reflexes and hits your Smash back, the ball retains its maximum velocity! The rally turns into a hyper-fast exchange until someone misses.
4. **Miss Penalty:** If configured (`PENALIZE_MISSED_SMASH = true`), letting the ball pass after arming a Smash will waste your Power-Up charge for the rest of the match.

---

## 🛠️ Hardware Requirements & Pinout

### Components
* **MCU:** ESP32-S3 (e.g., ESP32-S3 module with built-in internal RGB LEDs)
* **Main Track:** WS2812B Addressable LED Strip (Configured for 150 LEDs, utilizing ESP32 RMT peripheral)
* **Button LEDs:** Chained WS2812B LEDs embedded inside the buttons (8 LEDs for Player 1, 8 LEDs for Player 2 connected on a single pin)
* **Display:** I2C SH1106 OLED Display (128x64 resolution)
* **Audio:** 5V Passive/Active Buzzer

### ESP32-S3 Wiring Diagram

| Component | Function / Label | ESP32-S3 Pin | Notes |
| :--- | :--- | :--- | :--- |
| **Main LED Strip** | Track Signal (RMT Channel 0) | **GPIO 38** | Data Input (DI) of the track |
| **Button LED Strip** | Button Illumination Chain | **GPIO 14** | 16 LEDs total (0-7: P1, 8-15: P2) |
| **OLED Display** | I2C SDA | **GPIO 11** | Data Line |
| **OLED Display** | I2C SCL | **GPIO 12** | Clock Line |
| **Buzzer** | Audio Tone Output | **GPIO 47** | Plays hit, smash, and score tones |
| **Left Button (P1)** | Primary Hit & Mode Select | **GPIO 7** | Active LOW (Internal Pullup) |
| **Left Power (P1)** | Smash Arming Button | **GPIO 8** | Active LOW (Internal Pullup) |
| **Right Button (P2)** | Primary Hit Button | **GPIO 5** | Active LOW (Internal Pullup) |
| **Right Power (P2)** | Smash Arming Button | **GPIO 6** | Active LOW (Internal Pullup) |
| **Internal RGB** | Blue Diagnostic LED | **GPIO 45** | Onboard ESP32-S3 module light |
| **Internal RGB** | Red Diagnostic LED | **GPIO 48** | Onboard ESP32-S3 module light |

---

## ⚙️ Software Architecture & Features

* **Non-Blocking Logic:** The game loop relies completely on `millis()` timers for ball movement and rendering, ensuring highly responsive button edge-detections and preventing ghost triggers.
* **Persistent All-Time Records:** Utilizing the native ESP32 `Preferences` flash memory layer, the game securely saves the highest rally count (successful consecutive hits) ever achieved on the device. This **All-Time Record** is proudly displayed on the OLED screen during the `IDLE` state.
* **Dynamic Track Scaling:** The engine automatically scales starting speeds, maximum velocity limits, and zone dimensions proportionally if you change the `LED_COUNT` define (tested seamlessly across lengths like 60, 120, and 150 LEDs).
* **Immersive Arcade Audio:** Features custom tone intervals for normal hits (`1000 Hz`), ultimate smash triggers (`1800 Hz`), and point conversions (`400 Hz`).

---

## 🚀 Getting Started

1. Install **Arduino IDE** and make sure you have the **ESP32 Core** boards package installed.
2. Download and include the following dependencies into your libraries folder:
   * `Freenove_WS2812_Lib_for_ESP32` (Optimized for RMT timings)
   * `Adafruit_NeoPixel`
   * `Adafruit_GFX` & `Adafruit_SH110X`
3. Open `1D_PONG_GAME_v2_2.ino`, select your ESP32-S3 board variant, compile, and upload!
4. **Controls at Startup (IDLE State):**
   * **Short Press Left Button (< 1.5s):** Activates Singleplayer Mode (vs. calibrated AI).
   * **Long Hold Left Button (> 2.0s):** Activates Multiplayer 2-Player Mode (Enables random ball serving seed based on button millisecond state).

---

## ⚖️ License

Distributed under the **GNU General Public License v3**. See the `LICENSE` file for more details. 

*Developed by Srecko Nagy, 2026.*
