#include "gyro.h"

#include <initializer_list>

#include "board.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr const char *kTag = "gyro";
constexpr uint8_t kQmiAddrLow = 0x6A;
constexpr uint8_t kQmiAddrHigh = 0x6B;

// QMI8658 registers.
constexpr uint8_t kRegWhoAmI = 0x00;
constexpr uint8_t kRegCtrl1 = 0x02;
constexpr uint8_t kRegCtrl2 = 0x03;
constexpr uint8_t kRegCtrl3 = 0x04;
constexpr uint8_t kRegCtrl7 = 0x08;
constexpr uint8_t kRegStatus = 0x02;
constexpr uint8_t kRegGyroX = 0x35 + 6;  // gyro starts after 6 accel bytes

// CTRL2: +/-8g, 800Hz, 8ms. CTRL3: +/-512dps, 800Hz.
constexpr uint8_t kCtrl2Value = 0x03;
constexpr uint8_t kCtrl3Value = 0x43;
constexpr float kGyroScale = 64.0f;  // LSB per dps at +/-512dps

i2c_master_dev_handle_t g_gyro_device = nullptr;
bool g_present = false;
float g_yaw = 0.0f;

bool read_byte(uint8_t reg, uint8_t *val) {
  return i2c_master_transmit_receive(g_gyro_device, &reg, 1, val, 1, 100) == ESP_OK;
}

void write_byte(uint8_t reg, uint8_t val) {
  const uint8_t data[] = {reg, val};
  i2c_master_transmit(g_gyro_device, data, 2, 100);
}
}  // namespace

bool Gyro::present_ = false;
float Gyro::yaw_ = 0.0f;
int32_t Gyro::last_z_ = 0;
uint32_t Gyro::last_us_ = 0;

bool Gyro::begin() {
  if (g_present) return true;
  i2c_master_dev_handle_t dev = nullptr;
  uint8_t who = 0;
  // Probe both possible addresses.
  for (uint8_t addr : {kQmiAddrLow, kQmiAddrHigh}) {
    if (board_i2c_add(addr, &dev) != ESP_OK) continue;
    g_gyro_device = dev;
    if (read_byte(kRegWhoAmI, &who) && who == 0x05) {
      g_present = true;
      break;
    }
    g_gyro_device = nullptr;
  }
  if (!g_present) {
    ESP_LOGW(kTag, "QMI8658 not detected");
    return false;
  }
  ESP_LOGI(kTag, "QMI8658 detected, WHO_AM_I=0x%02x", who);
  write_byte(kRegCtrl1, 0x60);      // I2C, auto-increment, addr range.
  write_byte(kRegCtrl2, kCtrl2Value);  // accel config
  write_byte(kRegCtrl3, kCtrl3Value);  // gyro config
  write_byte(kRegCtrl7, 0x03);      // enable accel + gyro
  vTaskDelay(pdMS_TO_TICKS(20));
  present_ = true;
  return true;
}

float Gyro::z_dps() {
  if (!g_present || g_gyro_device == nullptr) return 0.0f;
  uint8_t raw[2] = {0, 0};
  const uint8_t reg = kRegGyroX;
  if (i2c_master_transmit_receive(g_gyro_device, &reg, 1, raw, 2, 100) != ESP_OK) {
    return 0.0f;
  }
  int16_t value = static_cast<int16_t>((static_cast<uint16_t>(raw[1]) << 8) | raw[0]);
  return static_cast<float>(value) / kGyroScale;
}

void Gyro::tick(float dt) {
  const float z = z_dps();
  // Integrate. Ignore tiny zero-drift noise.
  if (z > -0.5f && z < 0.5f) return;
  g_yaw += z * dt;
  if (g_yaw > 360.0f) g_yaw -= 360.0f;
  if (g_yaw < -360.0f) g_yaw += 360.0f;
  yaw_ = g_yaw;
}

float Gyro::yaw_deg() { return g_yaw; }

bool Gyro::is_present() { return g_present; }
