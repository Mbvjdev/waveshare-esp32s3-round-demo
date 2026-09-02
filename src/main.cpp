#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <WiFi.h>
#include <SD_MMC.h>
#include <math.h>
#include <time.h>

// Waveshare ESP32-S3-LCD-1.85 (non-touch)
// Display: ST77916, 360x360, QSPI
// Reference: https://www.waveshare.com/wiki/ESP32-S3-LCD-1.85

namespace Pins {
constexpr int LCD_CS = 21;
constexpr int LCD_SCK = 40;
constexpr int LCD_D0 = 46;
constexpr int LCD_D1 = 45;
constexpr int LCD_D2 = 42;
constexpr int LCD_D3 = 41;
constexpr int LCD_BACKLIGHT = 5;
constexpr int BACKLIGHT_CHANNEL = 0;
constexpr int LCD_TE = 18;
constexpr int I2C_SDA = 11;
constexpr int I2C_SCL = 10;
constexpr int BATTERY_ADC = 8;
constexpr int BOOT_BUTTON = 0;
constexpr int SD_CLK = 14;
constexpr int SD_CMD = 17;
constexpr int SD_D0 = 16;
}

namespace I2CAddress {
constexpr uint8_t TCA9554 = 0x20;
constexpr uint8_t RTC = 0x51;
constexpr uint8_t AUDIO_LOW = 0x18;
constexpr uint8_t AUDIO_HIGH = 0x19;
constexpr uint8_t QMI8658_LOW = 0x6A;
constexpr uint8_t QMI8658_HIGH = 0x6B;
}

namespace TCARegister {
constexpr uint8_t OUTPUT_REG = 0x01;
constexpr uint8_t CONFIG = 0x03;
constexpr uint8_t LCD_RESET = 2; // EXIO2, one-indexed in Waveshare's driver
}

constexpr uint16_t COLOR_BG = 0x0861;
constexpr uint16_t COLOR_PANEL = 0x1104;
constexpr uint16_t COLOR_PANEL_2 = 0x18C5;
constexpr uint16_t COLOR_TEXT = 0xFFFF;
constexpr uint16_t COLOR_MUTED = 0x9CF3;
constexpr uint16_t COLOR_CYAN = 0x07FF;
constexpr uint16_t COLOR_GOLD = 0xFEA0;
constexpr uint16_t COLOR_GREEN = 0x07E0;
constexpr uint16_t COLOR_RED = 0xF800;
constexpr uint16_t COLOR_BLUE = 0x451F;

Arduino_DataBus *qspiBus = new Arduino_ESP32QSPI(
    Pins::LCD_CS,
    Pins::LCD_SCK,
    Pins::LCD_D0,
    Pins::LCD_D1,
    Pins::LCD_D2,
    Pins::LCD_D3,
    false);
Arduino_GFX *gfx = new Arduino_ST77916(qspiBus, GFX_NOT_DEFINED, 0, true, 360, 360);

struct MotionSample {
  float ax = 0;
  float ay = 0;
  float az = 0;
  float gx = 0;
  float gy = 0;
  float gz = 0;
  bool valid = false;
};

struct RtcSample {
  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  bool valid = false;
};

bool tcaPresent = false;
bool rtcPresent = false;
bool rtcReadable = false;
bool audioCodecPresent = false;
bool qmiPresent = false;
bool sdPresent = false;
uint8_t qmiAddress = 0;
uint8_t i2cDevices[16] = {};
size_t i2cDeviceCount = 0;
uint64_t sdCardSizeMb = 0;
int wifiCount = -2;
bool wifiScanned = false;
float batteryVoltage = 0;
int batteryRaw = 0;
MotionSample motion;
RtcSample rtc;
int page = 0;
uint8_t brightness = 72;
uint32_t lastDraw = 0;
uint32_t lastSensorRead = 0;
uint32_t lastBatteryRead = 0;
bool bootWasDown = false;
uint32_t buttonDownAt = 0;

bool i2cProbe(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

bool i2cWriteRegister(uint8_t address, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool i2cReadRegister(uint8_t address, uint8_t reg, uint8_t *data, size_t length) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  size_t received = Wire.requestFrom(static_cast<int>(address), static_cast<int>(length));
  if (received != length) {
    while (Wire.available()) {
      Wire.read();
    }
    return false;
  }
  for (size_t i = 0; i < length; ++i) {
    data[i] = Wire.read();
  }
  return true;
}

void configureTca9554() {
  tcaPresent = i2cProbe(I2CAddress::TCA9554);
  if (!tcaPresent) {
    return;
  }

  // Match Waveshare's driver: all EXIO pins are outputs, LCD reset starts low.
  i2cWriteRegister(I2CAddress::TCA9554, TCARegister::OUTPUT_REG, 0x00);
  i2cWriteRegister(I2CAddress::TCA9554, TCARegister::CONFIG, 0x00);
  delay(10);
  i2cWriteRegister(I2CAddress::TCA9554, TCARegister::OUTPUT_REG, 1u << (TCARegister::LCD_RESET - 1));
  delay(50);
}

void scanI2cBus() {
  i2cDeviceCount = 0;
  for (uint8_t address = 0x08; address <= 0x77; ++address) {
    if (i2cProbe(address) && i2cDeviceCount < sizeof(i2cDevices)) {
      i2cDevices[i2cDeviceCount++] = address;
    }
  }
  rtcPresent = i2cProbe(I2CAddress::RTC);
  audioCodecPresent = i2cProbe(I2CAddress::AUDIO_LOW) || i2cProbe(I2CAddress::AUDIO_HIGH);
}

void initQmi8658() {
  const uint8_t candidates[] = {I2CAddress::QMI8658_HIGH, I2CAddress::QMI8658_LOW};
  for (uint8_t candidate : candidates) {
    uint8_t whoAmI = 0;
    if (i2cReadRegister(candidate, 0x00, &whoAmI, 1) && whoAmI == 0x05) {
      qmiAddress = candidate;
      qmiPresent = true;
      // CTRL1: I2C + automatic register address increment.
      i2cWriteRegister(qmiAddress, 0x02, 0x60);
      // CTRL2: +/-8g, 1000Hz. CTRL3: +/-512dps, 1000Hz.
      i2cWriteRegister(qmiAddress, 0x03, 0x23);
      i2cWriteRegister(qmiAddress, 0x04, 0x43);
      // CTRL7: enable accelerometer and gyroscope.
      i2cWriteRegister(qmiAddress, 0x08, 0x03);
      return;
    }
  }
}

int bcdToDec(uint8_t value) {
  return ((value >> 4) * 10) + (value & 0x0F);
}

bool readRtc() {
  if (!rtcPresent) {
    return false;
  }
  uint8_t raw[7] = {};
  if (!i2cReadRegister(I2CAddress::RTC, 0x04, raw, sizeof(raw))) {
    return false;
  }

  rtc.second = bcdToDec(raw[0] & 0x7F);
  rtc.minute = bcdToDec(raw[1] & 0x7F);
  rtc.hour = bcdToDec(raw[2] & 0x3F);
  rtc.day = bcdToDec(raw[3] & 0x3F);
  rtc.month = bcdToDec(raw[5] & 0x1F);
  rtc.year = 1970 + bcdToDec(raw[6]); // Matches Waveshare's PCF85063 example.
  rtc.valid = rtc.year >= 2020 && rtc.year <= 2100 && rtc.month >= 1 && rtc.month <= 12 && rtc.day >= 1 && rtc.day <= 31;
  return true;
}

void readMotion() {
  if (!qmiPresent) {
    motion.valid = false;
    return;
  }

  uint8_t raw[12] = {};
  if (!i2cReadRegister(qmiAddress, 0x35, raw, sizeof(raw))) {
    motion.valid = false;
    return;
  }

  auto signed16 = [](uint8_t low, uint8_t high) -> int16_t {
    return static_cast<int16_t>((static_cast<uint16_t>(high) << 8) | low);
  };
  const int16_t ax = signed16(raw[0], raw[1]);
  const int16_t ay = signed16(raw[2], raw[3]);
  const int16_t az = signed16(raw[4], raw[5]);
  const int16_t gx = signed16(raw[6], raw[7]);
  const int16_t gy = signed16(raw[8], raw[9]);
  const int16_t gz = signed16(raw[10], raw[11]);

  // +/-8g = 4096 LSB/g, +/-512dps = 64 LSB/dps.
  motion.ax = (ax * 1000.0f) / 4096.0f;
  motion.ay = (ay * 1000.0f) / 4096.0f;
  motion.az = (az * 1000.0f) / 4096.0f;
  motion.gx = gx / 64.0f;
  motion.gy = gy / 64.0f;
  motion.gz = gz / 64.0f;
  motion.valid = true;
}

void readBattery() {
  batteryRaw = analogRead(Pins::BATTERY_ADC);
  const uint32_t millivolts = analogReadMilliVolts(Pins::BATTERY_ADC);
  batteryVoltage = (millivolts * 3.0f) / 1000.0f;
}

void initSdCard() {
  if (!SD_MMC.setPins(Pins::SD_CLK, Pins::SD_CMD, Pins::SD_D0)) {
    return;
  }
  if (!SD_MMC.begin("/sdcard", true)) {
    return;
  }
  if (SD_MMC.cardType() == CARD_NONE) {
    return;
  }
  sdCardSizeMb = SD_MMC.cardSize() / (1024 * 1024);
  sdPresent = true;
}

void scanWifi() {
  Serial.println("[wifi] scanning...");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, false);
  delay(50);
  wifiCount = WiFi.scanNetworks(false, true);
  wifiScanned = true;
  Serial.printf("[wifi] networks found: %d\n", wifiCount);
}

void setBrightness(uint8_t value) {
  brightness = constrain(value, static_cast<uint8_t>(5), static_cast<uint8_t>(100));
  ledcWrite(Pins::BACKLIGHT_CHANNEL, brightness * 1023 / 100);
}

void initBacklight() {
  ledcSetup(Pins::BACKLIGHT_CHANNEL, 20000, 10);
  ledcAttachPin(Pins::LCD_BACKLIGHT, Pins::BACKLIGHT_CHANNEL);
  setBrightness(brightness);
}

void text(int16_t x, int16_t y, const String &value, uint16_t color = COLOR_TEXT, uint8_t size = 1) {
  gfx->setTextColor(color);
  gfx->setTextSize(size);
  gfx->setCursor(x, y);
  gfx->print(value);
}

void text(int16_t x, int16_t y, const char *value, uint16_t color = COLOR_TEXT, uint8_t size = 1) {
  text(x, y, String(value), color, size);
}

void card(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t fill = COLOR_PANEL) {
  gfx->fillRoundRect(x, y, w, h, 12, fill);
  gfx->drawRoundRect(x, y, w, h, 12, COLOR_PANEL_2);
}

void header(const char *title, uint16_t accent) {
  gfx->fillScreen(COLOR_BG);
  gfx->fillCircle(180, 180, 171, COLOR_BG);
  gfx->drawCircle(180, 180, 171, accent);
  text(46, 45, title, COLOR_TEXT, 2);
  text(276, 49, String(page + 1) + "/4", COLOR_MUTED, 1);
  gfx->drawFastHLine(46, 72, 268, COLOR_PANEL_2);
}

void statusDot(int16_t x, int16_t y, bool ok, const char *label) {
  gfx->fillCircle(x, y + 5, 5, ok ? COLOR_GREEN : COLOR_RED);
  text(x + 12, y, label, COLOR_MUTED, 1);
}

void drawBattery(int16_t x, int16_t y) {
  const bool plausible = batteryVoltage > 2.8f && batteryVoltage < 4.5f;
  const int percent = plausible ? constrain(static_cast<int>((batteryVoltage - 3.3f) * 111.0f), 0, 100) : 0;
  gfx->drawRoundRect(x, y, 38, 17, 4, COLOR_MUTED);
  gfx->fillRect(x + 38, y + 5, 3, 7, COLOR_MUTED);
  if (plausible) {
    gfx->fillRoundRect(x + 3, y + 3, max(1, percent * 32 / 100), 11, 2, percent > 20 ? COLOR_GREEN : COLOR_RED);
  }
}

void drawHome() {
  header("ESP32-S3", COLOR_CYAN);
  text(48, 89, "WAVESHARE ROUND LAB", COLOR_GOLD, 1);

  card(45, 116, 270, 52);
  text(61, 128, "ST77916 LCD", COLOR_TEXT, 1);
  text(61, 146, "360 x 360  |  QSPI", COLOR_MUTED, 1);
  statusDot(262, 128, gfx != nullptr, "OK");

  card(45, 178, 130, 78);
  text(60, 190, "IMU", COLOR_MUTED, 1);
  text(60, 211, qmiPresent ? "QMI8658" : "not found", qmiPresent ? COLOR_GREEN : COLOR_RED, 1);
  text(60, 232, qmiPresent ? "6-axis" : "check bus", COLOR_MUTED, 1);

  card(185, 178, 130, 78);
  text(200, 190, "BATTERY", COLOR_MUTED, 1);
  char batteryText[16];
  snprintf(batteryText, sizeof(batteryText), "%.2f V", batteryVoltage);
  text(200, 211, batteryText, COLOR_TEXT, 1);
  drawBattery(200, 233);

  card(45, 266, 270, 52);
  text(61, 278, "PERIPHERALS", COLOR_MUTED, 1);
  statusDot(61, 300, rtcPresent, "RTC");
  statusDot(135, 300, audioCodecPresent, "AUDIO");
  statusDot(231, 300, sdPresent, "SD");

  card(45, 330, 270, 42, COLOR_BLUE);
  text(61, 344, "BOOT: next page   HOLD: Wi-Fi scan", COLOR_TEXT, 1);
  text(48, 394, "USB serial: /dev/cu.usbmodem101", COLOR_MUTED, 1);
}

void drawMotion() {
  header("MOTION", COLOR_GREEN);
  if (!motion.valid) {
    text(78, 145, "QMI8658 not detected", COLOR_RED, 2);
    text(88, 180, "Check the I2C bus", COLOR_MUTED, 1);
    return;
  }

  const float roll = atan2f(motion.ax, motion.az) * 57.29578f;
  const float pitch = atan2f(motion.ay, sqrtf(motion.ax * motion.ax + motion.az * motion.az)) * 57.29578f;
  const int horizonOffset = constrain(static_cast<int>(roll * 1.7f), -72, 72);
  const int horizonY = 196;
  gfx->drawCircle(180, horizonY, 86, COLOR_PANEL_2);
  gfx->drawLine(101, horizonY - horizonOffset, 259, horizonY + horizonOffset, COLOR_CYAN);
  gfx->drawFastVLine(180, horizonY - 78, 156, COLOR_MUTED);
  gfx->fillCircle(180, horizonY, 5, COLOR_GOLD);

  char value[32];
  snprintf(value, sizeof(value), "ROLL  %+.1f deg", roll);
  text(61, 105, value, COLOR_TEXT, 1);
  snprintf(value, sizeof(value), "PITCH %+.1f deg", pitch);
  text(190, 105, value, COLOR_TEXT, 1);

  card(45, 302, 270, 70);
  snprintf(value, sizeof(value), "A  %+.0f  %+.0f  %+.0f mg", motion.ax, motion.ay, motion.az);
  text(60, 314, value, COLOR_MUTED, 1);
  snprintf(value, sizeof(value), "G  %+.1f  %+.1f  %+.1f dps", motion.gx, motion.gy, motion.gz);
  text(60, 335, value, COLOR_MUTED, 1);
  text(60, 356, "Move the board and watch the horizon", COLOR_GOLD, 1);
}

void drawWifi() {
  header("WI-FI SCAN", COLOR_GOLD);
  if (!wifiScanned) {
    text(86, 155, "Press BOOT to scan", COLOR_TEXT, 2);
    text(85, 192, "or hold BOOT", COLOR_MUTED, 1);
    return;
  }

  char countText[32];
  snprintf(countText, sizeof(countText), "%d networks", wifiCount < 0 ? 0 : wifiCount);
  text(63, 101, countText, COLOR_GOLD, 2);
  text(63, 130, "2.4 GHz 802.11 b/g/n", COLOR_MUTED, 1);

  const int visible = min(wifiCount < 0 ? 0 : wifiCount, 5);
  for (int i = 0; i < visible; ++i) {
    const int y = 164 + i * 34;
    card(45, y, 270, 27, i == 0 ? COLOR_BLUE : COLOR_PANEL);
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) {
      ssid = "<hidden>";
    }
    if (ssid.length() > 23) {
      ssid = ssid.substring(0, 23);
    }
    text(57, y + 8, ssid, COLOR_TEXT, 1);
    text(259, y + 8, String(WiFi.RSSI(i)), COLOR_MUTED, 1);
  }
  if (visible == 0) {
    text(96, 190, "No networks found", COLOR_MUTED, 1);
  }
  text(65, 356, "BOOT = rescan", COLOR_GOLD, 1);
}

void drawSystem() {
  header("SYSTEM", COLOR_CYAN);
  card(45, 95, 270, 72);
  text(60, 108, "FIRMWARE", COLOR_MUTED, 1);
  text(60, 129, "round-lab demo", COLOR_TEXT, 1);
  text(60, 148, String(ESP.getCpuFreqMHz()) + " MHz  |  " + String(ESP.getFlashChipSize() / (1024 * 1024)) + " MB flash", COLOR_MUTED, 1);

  card(45, 178, 270, 72);
  text(60, 191, "RTC / STORAGE", COLOR_MUTED, 1);
  if (rtcReadable && rtc.valid) {
    char value[32];
    snprintf(value, sizeof(value), "%04d-%02d-%02d %02d:%02d:%02d", rtc.year, rtc.month, rtc.day, rtc.hour, rtc.minute, rtc.second);
    text(60, 212, value, COLOR_GREEN, 1);
  } else {
    text(60, 212, rtcPresent ? "RTC present, time unset" : "RTC not found", rtcPresent ? COLOR_GOLD : COLOR_RED, 1);
  }
  text(60, 231, sdPresent ? String("TF card  ") + sdCardSizeMb + " MB" : "TF card not mounted", sdPresent ? COLOR_GREEN : COLOR_MUTED, 1);

  card(45, 262, 270, 92);
  text(60, 275, "I2C DEVICES", COLOR_MUTED, 1);
  String addresses;
  for (size_t i = 0; i < i2cDeviceCount; ++i) {
    if (i > 0) {
      addresses += "  ";
    }
    char addr[8];
    snprintf(addr, sizeof(addr), "0x%02X", i2cDevices[i]);
    addresses += addr;
  }
  if (addresses.length() == 0) {
    addresses = "none detected";
  }
  text(60, 298, addresses, COLOR_TEXT, 1);
  text(60, 324, String("brightness  ") + brightness + "%", COLOR_GOLD, 1);
  text(60, 342, "BOOT = next page", COLOR_MUTED, 1);
}

void drawPage() {
  switch (page) {
    case 0:
      drawHome();
      break;
    case 1:
      drawMotion();
      break;
    case 2:
      drawWifi();
      break;
    default:
      drawSystem();
      break;
  }
}

void handleButton() {
  const bool down = digitalRead(Pins::BOOT_BUTTON) == LOW;
  if (down && !bootWasDown) {
    buttonDownAt = millis();
  }
  if (!down && bootWasDown) {
    const uint32_t heldMs = millis() - buttonDownAt;
    if (heldMs >= 1200) {
      page = 2;
      scanWifi();
    } else {
      page = (page + 1) % 4;
      if (page == 2) {
        scanWifi();
      }
    }
    Serial.printf("[ui] page=%d held=%lums\n", page, static_cast<unsigned long>(heldMs));
    drawPage();
  }
  bootWasDown = down;
}

void handleSerial() {
  while (Serial.available()) {
    const char command = static_cast<char>(Serial.read());
    if (command == 'n' || command == 'N') {
      page = (page + 1) % 4;
      if (page == 2) {
        scanWifi();
      }
      drawPage();
    } else if (command == 's' || command == 'S') {
      scanWifi();
      page = 2;
      drawPage();
    } else if (command == '+' && brightness < 100) {
      setBrightness(brightness + 10);
      drawPage();
    } else if (command == '-' && brightness > 15) {
      setBrightness(brightness - 10);
      drawPage();
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.println();
  Serial.println("=== Waveshare ESP32-S3-LCD-1.85 / round-lab demo ===");
  Serial.println("non-touch | ST77916 | 360x360 | QSPI");

  pinMode(Pins::BOOT_BUTTON, INPUT_PULLUP);
  analogReadResolution(12);
  analogSetPinAttenuation(Pins::BATTERY_ADC, ADC_11db);
  Wire.begin(Pins::I2C_SDA, Pins::I2C_SCL, 400000);

  configureTca9554();
  initBacklight();
  if (!gfx->begin(40000000)) {
    Serial.println("[lcd] init failed");
  }
  gfx->setRotation(0);
  gfx->setTextWrap(false);

  scanI2cBus();
  initQmi8658();
  readBattery();
  rtcReadable = readRtc();
  initSdCard();
  setBrightness(brightness);

  Serial.printf("[i2c] TCA9554=%s RTC=%s audio=%s QMI8658=%s SD=%s\n",
                tcaPresent ? "yes" : "no",
                rtcPresent ? "yes" : "no",
                audioCodecPresent ? "yes" : "no",
                qmiPresent ? "yes" : "no",
                sdPresent ? "yes" : "no");
  Serial.printf("[battery] raw=%d voltage=%.3fV\n", batteryRaw, batteryVoltage);
  Serial.printf("[system] chip=%s cpu=%uMHz flash=%uMB psram=%uMB\n",
                ESP.getChipModel(), ESP.getCpuFreqMHz(),
                ESP.getFlashChipSize() / (1024 * 1024),
                ESP.getPsramSize() / (1024 * 1024));
  Serial.println("[ui] BOOT: next page | hold BOOT: Wi-Fi scan | serial n/s/+/-");

  drawPage();
}

void loop() {
  handleButton();
  handleSerial();

  const uint32_t now = millis();
  if (now - lastSensorRead >= 120) {
    lastSensorRead = now;
    readMotion();
  }
  if (now - lastBatteryRead >= 1000) {
    lastBatteryRead = now;
    readBattery();
    rtcReadable = readRtc();
  }
  if (now - lastDraw >= 350) {
    lastDraw = now;
    drawPage();
  }
  delay(4);
}
