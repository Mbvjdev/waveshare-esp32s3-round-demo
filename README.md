# Waveshare ESP32-S3 Round Lab (native)

A small, self-contained firmware demo for the **Waveshare ESP32-S3-LCD-1.85** non-touch
round display, built on **ESP-IDF 5.3.1** with Waveshare's **native `esp_lcd` ST77916/QSPI
driver**.

The board is the device sold under Amazon ASIN `B0DK9GSG2Q`. On the tested Mac it enumerates as
an Espressif USB JTAG/serial device at `/dev/cu.usbmodem101`.

## Why native ESP-IDF (not Arduino_GFX)

The initial version drove the ST77916 through `Arduino_GFX`'s ESP32-QSPI data bus. It built and
flashed, but the panel showed vertical bands / noise. The vendor reference firmware (which showed a
clean image) uses ESP-IDF's native `esp_lcd` driver instead, with a QSPI configuration that
Arduino_GFX's QSPI shim does not reproduce:

- SPI2 host, 80 MHz, mode 0
- 32-bit command words, 8-bit parameters
- `esp_lcd_panel_io` in QSPI mode (`quad_mode = 1`)
- RGB565 framebuffer transferred with `esp_lcd_panel_draw_bitmap()`
- LCD reset through the **TCA9554 EXIO2** expander
- Backlight (LEDC on GPIO 5) started after panel init

This project now drives the panel exactly the way the working vendor firmware does, and our own
application code (SMPTE test pattern, sensor/system pages) sits on top.

## Verified on the connected board

Firmware built, flashed and booted on the physical board:

```text
ESP-IDF 5.3.1 2nd stage bootloader
SPI Mode       : QIO
SPI Flash Size : 8MB  (physical flash is 16MB; image header uses 8MB)
octal_psram: Found 8MB PSRAM device
The SPI initialization succeeded.
LCD communication parameters are set successfully -- SPI
Install LCD driver of st77916
Register 0x04 data: 00 02 7f 7f
Vendor-specific initialization for case 2.
[lcd] native ST77916 init complete
[lcd] native SMPTE frame sent once
```

The panel displays a local SMPTE-style color-bar test pattern. The test frame is drawn **once** at
boot and is not periodically redrawn, so there is no refresh-induced flicker.

## Current content

- ST77916 360×360 round LCD over native QSPI (ESP-IDF 5.3.1)
- SMPTE-style test pattern (color bars + grayscale + primary-color blocks), generated locally
- TCA9554-based LCD reset (EXIO2) and backlight
- The board boots straight to the test pattern

Planned next layer (sensor/system pages) will be added on top of this native driver.

## Layout

```
platformio.ini            PlatformIO config (espidf 5.3.1)
sdkconfig.defaults        ESP-IDF Kconfig (QIO, 240 MHz, 16MB flash, 8MB PSRAM)
partitions.csv            Minimal partition table (nvs + factory)
src/main.cpp              app_main: panel init + test pattern
src/CMakeLists.txt        ESP-IDF component registration
src/lcd/ST77916.c/.h      Waveshare native ST77916 panel driver
src/lcd/esp_lcd_st77916.c/.h  Vendor QSPI init sequence
test/test_project_files.py  Static project checks
```

## Build and flash

```bash
pio run                                # build
pio run --target upload --upload-port /dev/cu.usbmodem101
pio run --target monitor -b 115200     # or: pio device monitor -b 115200
```

If the port changes, update `upload_port` / `monitor_port` in `platformio.ini`, or override on the
command line.

## Notes

- `lib/` is intentionally empty: this project does **not** vendor Arduino_GFX. The panel is driven by
  the native ESP-IDF `esp_lcd` component.
- The bundled `sdkconfig.waveshare-esp32s3` is a generated build artifact and is git-ignored.

## Sources

- [Waveshare product wiki](https://www.waveshare.com/wiki/ESP32-S3-LCD-1.85)
- [Waveshare 1.85C reference repository](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-1.85C)
- [ESP-IDF esp_lcd ST77916 component](https://github.com/espressif/esp-idf)
