#pragma once

#include <cstdint>
#include "esp_err.h"

// QMI8658 6-axis IMU over the shared I2C bus. This board has NO magnetometer,
// so heading comes from integrating gyro-Z (radar direction-finding by rotation).
//
// The QMI8658 has a small constant gyro-Z bias (offset). If that is integrated
// naively, the reported yaw "walks" even when the board is held perfectly
// still. We calibrate the offset at boot while the board is stationary, then
// subtract it from every reading before integrating.
class Gyro {
 public:
  // Probe WHO_AM_I, configure accel/gyro, and calibrate the gyro-Z bias while
  // the board is assumed stationary. Returns true if present.
  static bool begin(float calibration_seconds = 1.0f);

  // Raw gyro-Z in deg/s (bias already subtracted).
  static float z_dps();

  // Latest integrated yaw in degrees, unwrapped (can exceed +/-180).
  static float yaw_deg();
  static float yaw_norm();  // wrapped to 0..360

  // True if the QMI8658 was detected and configured.
  static bool is_present();

  // Read sensors and accumulate yaw; call this each loop tick.
  static void tick(float dt_seconds);

 private:
  static bool present_;
  static float bias_;   // gyro-Z offset in deg/s
  static float yaw_;
};
