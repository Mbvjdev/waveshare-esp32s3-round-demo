#pragma once

// NimBLE-BASED BLE DEVICE SCANNER FOR THE DRAGON BALL RADAR
// Pulls real nearby BLE devices via RSSI and exposes them to the radar UI.

#include <cstddef>
#include <cstdint>

// One detected BLE advertisement source.
struct BleDevice {
  uint64_t addr = 0;      // 48-bit MAC as integer (LSB first)
  int8_t rssi = -127;     // current RSSI (dBm), strongest reading this scan
  int8_t smoothRssi = -127;  // low-pass filtered RSSI, drives live radius
  int8_t bestRssi = -127; // strongest RSSI seen (bearing anchor)
  float bestYaw = 0.0f;   // board yaw (deg) at which bestRssi occurred
  uint8_t addrType = 0;   // 0 = public, 1 = random
  bool hasName = false;
  char name[32] = {0};
  uint32_t lastSeenMs = 0;
};

// Number of device slots the radar UI can render.
constexpr int kMaxBleDevices = 16;

class BleScanner {
 public:
  // Registered in app_main before the scan starts.
  static void init();
  // Refresh the cached device table (non-blocking, tasks in background).
  static void poll();
  // Feed the current board yaw (deg) so BLE DFs can tag bearings.
  static void set_current_yaw(float yaw_deg);

  // Copy of the current device table + count. Thread-safe snapshot.
  static int snapshot(BleDevice *out, int capacity);

 private:
  static BleDevice devices_[kMaxBleDevices];
  static int deviceCount_;
  static bool started_;
};
