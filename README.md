# Waveshare ESP32-S3 Round Lab

A small, self-contained hardware demo for the **Waveshare ESP32-S3-LCD-1.85** non-touch board.

The board is the device sold under Amazon ASIN `B0DK9GSG2Q`. On the tested Mac it enumerates as an Espressif USB JTAG/serial device at `/dev/cu.usbmodem101`.

## Verified on the connected board

The firmware was built, flashed, rebooted and exercised on the physical board:

```text
ESP32-S3 @ 240 MHz
16 MB flash / 8 MB embedded PSRAM
TCA9554=yes  RTC=yes  QMI8658=yes  SD=yes
battery=4.119 V
Wi-Fi scan=21 networks found
```

The LCD initialized without an error. The ES8311 audio codec did not answer on I²C in this test, so audio playback is documented but intentionally not enabled in this lightweight demo.

## What the demo exercises

- ST77916 360×360 round LCD over QSPI
- LCD backlight on GPIO 5
- QMI8658 6-axis IMU over I²C, if present
- Battery ADC on GPIO 8 using Waveshare's 3× divider
- I²C bus discovery, including TCA9554, PCF85063 RTC and ES8311 audio codec
- PCF85063 RTC readout
- TF/micro-SD card in 1-bit SDMMC mode
- 2.4 GHz Wi-Fi network scan
- BOOT button navigation
- USB CDC serial logging and commands

The board is the **non-touch** variant. The demo therefore intentionally does not include CST816 touch support.

`Arduino_GFX` is vendored under `lib/Arduino_GFX` so the project does not depend on PlatformIO registry resolution. Its upstream `license.txt` is preserved. One unused RGB-panel source file is disabled because it targets a newer ESP-IDF API than the Arduino-core release used by Waveshare's examples; the active QSPI/ST77916 path is unchanged.

## Controls

- Short press **BOOT**: next page
- Long press **BOOT**: run Wi-Fi scan and show the Wi-Fi page
- Serial `n`: next page
- Serial `s`: Wi-Fi scan
- Serial `+` / `-`: change backlight brightness

Do not hold BOOT while resetting unless you intentionally want download/bootloader mode.

## Build and flash

Requirements:

- macOS
- PlatformIO Core (`uv tool install platformio`)
- USB-C cable

```bash
pio run
pio run --target upload
pio device monitor -b 115200
```

If the port changes, update `upload_port` and `monitor_port` in `platformio.ini`, or override them on the command line:

```bash
pio run --target upload --upload-port /dev/cu.usbmodemXXXX
pio device monitor --port /dev/cu.usbmodemXXXX -b 115200
```

## Hardware notes

The pin definitions are based on Waveshare's own Arduino/ESP-IDF examples and the product wiki:

| Function | Pin/address |
|---|---:|
| LCD QSPI CS/SCK/D0/D1/D2/D3 | 21 / 40 / 46 / 45 / 42 / 41 |
| LCD backlight | GPIO 5 |
| I²C SDA/SCL | GPIO 11 / GPIO 10 |
| Battery ADC | GPIO 8 |
| SDMMC CLK/CMD/D0 | GPIO 14 / GPIO 17 / GPIO 16 |
| QMI8658 | I²C 0x6A or 0x6B |
| PCF85063 RTC | I²C 0x51 |
| TCA9554 power/reset expander | I²C 0x20 |
| ES8311 audio codec | I²C 0x18 or 0x19 |

The included speaker connects to the board's SH1.0 audio connector. This first demo identifies the audio codec but leaves audio playback out so the display/IMU/battery/network test remains lightweight. The I²S pins documented by Waveshare are GPIO 2 (MCLK), 48 (BCLK), 38 (LRCK), 47 (DOUT), and 39 (DIN).

## Sources

- [Waveshare product wiki](https://www.waveshare.com/wiki/ESP32-S3-LCD-1.85)
- [Waveshare 1.85C reference repository](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-1.85C)
- [Arduino_GFX](https://github.com/moononournation/Arduino_GFX)
- [QMI8658 datasheet](https://qstcorp.com/upload/pdf/202202/QMI8658C%20datasheet%20rev%200.9.pdf)
