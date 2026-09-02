# Hardware verification

Tested on the physical board connected over USB-C to macOS.

## USB identification

```text
Manufacturer: Espressif
Product: USB JTAG/serial debug unit
VID:PID: 303a:1001
Serial port: /dev/cu.usbmodem101
```

## Flash/upload verification

`pio run -e waveshare-esp32s3 --target upload --upload-port /dev/cu.usbmodem101` completed successfully. esptool identified:

```text
Chip: ESP32-S3 (QFN56), revision v0.2
Features: WiFi, BLE, Embedded PSRAM 8MB (AP_3v3)
Crystal: 40MHz
USB mode: USB-Serial/JTAG
Flash write: hash verified
```

## Runtime verification

The firmware rebooted and reported:

```text
TCA9554=yes RTC=yes audio=no QMI8658=yes SD=yes
battery=4.119V
chip=ESP32-S3 cpu=240MHz flash=16MB psram=7MB
wifi networks found: 21
```

The LCD initialization produced no error. The ES8311 audio codec did not answer on I²C in this test; audio is therefore listed as detected/not detected rather than claimed as working.
