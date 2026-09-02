#pragma once

#include <cstdint>
#include "esp_err.h"

// QMI8658 6-axis IMU over the shared I2C bus. This board has NO magnetometer,
// so heading comes from integrating gyro-Z (radar direction-finding by rotation).
class Gyro {
 public:
  // Probe WHO_AM_I and configure accel/gyro; returns true if present.
  static bool begin();

  // Raw gyro-Z in deg/s (already scaled from LSB).
  static float z_dps();

  // Latest integrated yaw in degrees, unwrapped (can exceed +/-180).
  static float yaw_deg();

  // True if the QMI8658 was detected and configured.
  static bool is_present();

  // Read sensors and accumulate yaw; call this each loop tick.
  static void tick(float dt_seconds);

 private:
  static bool present_;
  static float yaw_;
  static int32_t last_z_;  // last raw LSB for integration
  static uint32_t last_us_;
};
