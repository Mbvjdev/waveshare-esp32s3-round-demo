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
constexpr uint8_t kRegGyroX = 0x35 + 6;  // gyro starts after 6 accel bytes

// CTRL2: +/-8g, 800Hz, 8ms. CTRL3: +/-512dps, 800Hz.
constexpr uint8_t kCtrl2Value = 0x03;
constexpr uint8_t kCtrl3Value = 0x43;
constexpr float kGyroScale = 64.0f;  // LSB per dps at +/-512dps

// Drift rejection: only integrate when the (bias-corrected) rate clearly
// exceeds the stationary noise floor.
constexpr float kRateDeadband = 1.5f;

i2c_master_dev_handle_t g_gyro_device = nullptr;
bool g_present = false;
float g_yaw = 0.0f;
float g_bias = 0.0f;

bool read_byte(uint8_t reg, uint8_t *val) {
  return i2c_master_transmit_receive(g_gyro_device, &reg, 1, val, 1, 100) == ESP_OK;
}

void write_byte(uint8_t reg, uint8_t val) {
  const uint8_t data[] = {reg, val};
  i2c_master_transmit(g_gyro_device, data, 2, 100);
}

float read_z_dps_raw() {
  if (!g_present || g_gyro_device == nullptr) return 0.0f;
  uint8_t raw[2] = {0, 0};
  const uint8_t reg = kRegGyroX;
  if (i2c_master_transmit_receive(g_gyro_device, &reg, 1, raw, 2, 100) != ESP_OK) {
    return 0.0f;
  }
  int16_t value = static_cast<int16_t>((static_cast<uint16_t>(raw[1]) << 8) | raw[0]);
  return static_cast<float>(value) / kGyroScale;
}
}  // namespace

bool Gyro::present_ = false;
float Gyro::bias_ = 0.0f;
float Gyro::yaw_ = 0.0f;

bool Gyro::begin(float calibration_seconds) {
  if (g_present) return true;
  i2c_master_dev_handle_t dev = nullptr;
  uint8_t who = 0;
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
  write_byte(kRegCtrl1, 0x60);
  write_byte(kRegCtrl2, kCtrl2Value);
  write_byte(kRegCtrl3, kCtrl3Value);
  write_byte(kRegCtrl7, 0x03);
  vTaskDelay(pdMS_TO_TICKS(30));

  // Calibrate gyro-Z bias: average the reading while the board is assumed
  // stationary. This is the key to removing false "I'm turning" drift.
  const int samples = static_cast<int>(calibration_seconds * 50);
  float sum = 0.0f;
  int count = 0;
  for (int i = 0; i < samples; ++i) {
    sum += read_z_dps_raw();
    ++count;
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  if (count > 0) {
    g_bias = sum / static_cast<float>(count);
  }
  ESP_LOGI(kTag, "gyro-Z bias %.3f deg/s (hold still)", g_bias);
  bias_ = g_bias;
  return true;
}

float Gyro::z_dps() {
  float z = read_z_dps_raw() - g_bias;
  // Reject tiny residual noise so a still board does not integrate.
  if (z > -kRateDeadband && z < kRateDeadband) return 0.0f;
  return z;
}

void Gyro::tick(float dt) {
  const float z = z_dps();
  if (z == 0.0f) return;
  g_yaw += z * dt;
  if (g_yaw > 360.0f) g_yaw -= 360.0f;
  if (g_yaw < -360.0f) g_yaw += 360.0f;
  yaw_ = g_yaw;
}

float Gyro::yaw_deg() { return g_yaw; }

float Gyro::yaw_norm() {
  float v = g_yaw;
  while (v < 0.0f) v += 360.0f;
  while (v >= 360.0f) v -= 360.0f;
  return v;
}

bool Gyro::is_present() { return g_present; }
