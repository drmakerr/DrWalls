# DrWalls

### See motion through Wi-Fi with an ESP32.

DrWalls turns an ESP32 into a Wi-Fi motion sensor using **Channel State Information (CSI)**.

It detects changes in the Wi-Fi signal caused by movement in the environment.

**No camera. No PIR sensor. No additional sensing hardware.**

---

## 🚀 Install DrWalls

### [👉 INSTALL DRWALLS ON YOUR ESP32](https://drmakerr.github.io/DrWalls/)

No ESP-IDF, VS Code, Python, or compilation required.

Just connect your ESP32 to your computer with USB and install DrWalls directly from your browser.

> Use Google Chrome or Microsoft Edge on desktop for Web Serial support.

---

## What You Need

- ESP32
- USB cable
- Wi-Fi router
- Chrome or Edge on a computer

That's it.

---

## How It Works

Wi-Fi signals travel through the environment between your router and ESP32.

When a person moves through that environment, they affect the wireless signal.

DrWalls uses **Wi-Fi Channel State Information (CSI)** to analyze those changes and determine whether movement is occurring.

---

## Setup

After installing DrWalls:

1. The ESP32 creates a Wi-Fi network called **`DrWalls-Setup`**.
2. Connect to it using your phone or computer.
3. The setup portal should open automatically.
4. Enter your Wi-Fi credentials.
5. The ESP32 restarts and connects to your Wi-Fi.
6. Open:

```text
http://drwalls.local
```

You will see the **Dr. Maker CSI Radar**.

---

## CSI Radar

The web interface displays:

**MOTION DETECTED**

or

**NO MOTION**

in real time.

You can access it from another device connected to the same local network.

---

## Reset Wi-Fi

If you want to connect DrWalls to another Wi-Fi network, use:

**RESET WI-FI**

from the CSI Radar interface.

The ESP32 will erase the saved Wi-Fi credentials, restart, and return to `DrWalls-Setup` mode.

---

## Privacy

Your Wi-Fi credentials are entered through the local DrWalls setup portal and stored in the ESP32's NVS storage.

No personal Wi-Fi credentials are included in this repository or in the public DrWalls firmware.

---

## Build From Source

DrWalls is built using **ESP-IDF**.

```bash
git clone https://github.com/drmakerr/DrWalls.git
cd DrWalls

idf.py set-target esp32
idf.py build
idf.py flash monitor
```

Required managed components are downloaded automatically by the ESP-IDF Component Manager.

---

## Important

DrWalls is an experimental maker project.

Motion-detection performance can vary depending on:

- ESP32 placement
- Router placement
- Distance
- Room layout
- Walls and objects
- Wi-Fi interference
- Surrounding movement

Experiment with the ESP32 and router positions for the best results.

---

## Project Origin

DrWalls is based on and extends Espressif's **ESP-CSI** `wifi_sensing_demo`.

DrWalls adds an end-user workflow including:

- Browser-based firmware installation
- First-time Wi-Fi provisioning
- Captive setup portal
- Local CSI Radar web interface
- `drwalls.local` mDNS access
- Wi-Fi reset functionality
- Mobile-friendly motion visualization

ESP-CSI and the original Wi-Fi sensing implementation are developed by Espressif Systems.

---

## Author

Created by **Dr. Maker**.

Made with Love <3 for all makers.

---

## License

DrWalls is released under the **Apache License 2.0**.

Original Espressif source files retain their respective copyright and SPDX license notices.