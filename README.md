# Puck2Switch2

A Steam Controller 2 that behaves as a Switch 2 Pro Controller.

Plug in an ESP32-S3, pair your Steam Controller 2 over Bluetooth, and the Switch 2
sees a normal Pro Controller.

## Features

- **1:1 mimic of a Switch 2 Pro Controller** — the console believes it is one
- Native pairing with the Switch 2, no console modification
- Wakes the console from sleep
- Full motion controls (gyro + accelerometer)
- All buttons, both sticks, and analog triggers, and paddles
- Battery level reported to the console
- Bluetooth only — no dongle, no receiver, no cables to the console

## Requirements

- ESP32-S3 board
- Steam Controller 2 in Bluetooth mode
- A Nintendo Switch 2

## Build

Requires [ESP-IDF v6.1](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/).

```sh
./tools/setup.sh
idf.py -C firmware/esp32s3 build
idf.py -C firmware/esp32s3 -p /dev/ttyACM0 flash
```

## Pairing

1. Put the Steam Controller 2 in Bluetooth mode: from powered off, hold
   **B + R1 + Steam** until the light double-pulses.
2. On the Switch 2, pair the adapter the same way you pair a Pro Controller.
3. Press any button on the Steam Controller to wake the console.

After the first pairing, both devices reconnect automatically.

## Credits

Motion encoding is derived from [notsosaelin/PicoSwitch2](https://github.com/notsosaelin/PicoSwitch2)
(Apache-2.0). Switch 2 emulation is built on [esp-cpp/espp](https://github.com/esp-cpp/espp) (MIT).
