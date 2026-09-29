# DrWalls

Turn an ESP32 into a Wi-Fi motion sensor using Channel State Information (CSI).

DrWalls detects movement by analyzing changes in Wi-Fi signals — no camera, PIR sensor, or additional sensing hardware required.

## Features

- Wi-Fi CSI-based motion detection
- Works with a single ESP32 and Wi-Fi router
- Dr. Maker CSI Radar web interface
- Live `MOTION DETECTED` / `NO MOTION` status
- Access locally using `http://drwalls.local`
- Automatic first-time Wi-Fi setup
- `DrWalls-Setup` captive portal
- Wi-Fi credentials stored locally on the ESP32
- Reset Wi-Fi directly from the web interface
- Mobile-friendly interface

## How It Works

On first boot, DrWalls creates a Wi-Fi network called:

`DrWalls-Setup`

Connect to it and the setup portal should open automatically.

Enter your Wi-Fi network credentials.

The ESP32 saves the credentials locally, restarts, and connects to your router.

Once connected, open:

`http://drwalls.local`

to access the **Dr. Maker CSI Radar**.

## Hardware

- ESP32
- USB cable
- Wi-Fi router

No additional sensing hardware is required.

## Building

This project is developed with ESP-IDF.

Clone the repository and run:

```bash
idf.py set-target esp32
idf.py build
idf.py flash monitor
```

ESP-IDF Component Manager will download the required managed components automatically.

## Privacy

Wi-Fi credentials entered during setup are stored locally in the ESP32's NVS storage.

No Wi-Fi credentials are included in this repository.

## Project Origin

DrWalls is based on and extends Espressif's ESP-CSI `wifi_sensing_demo`.

The project adds a simplified end-user experience including Wi-Fi provisioning, captive portal setup, local web interface, mDNS access, and Wi-Fi reset functionality.

ESP-CSI and the original example are developed by Espressif Systems.

## Author

**Dr. Maker**

## License

This project contains and builds upon code from Espressif Systems.

Original Espressif source files retain their respective copyright and SPDX license notices.

See the project license and individual source files for applicable licensing information.