#pragma once

#include <cstdint>

// Receives GPS coordinates from the iPhone over a NimBLE GATT server.
// The iPhone connects, writes an 8-byte payload (float lat, float lon,
// little-endian, scaled) to the GPS characteristic, and the radar uses the
// last valid position.
class GpsReceiver {
 public:
  // Register the GATT service/characteristic and start advertising (call
  // after BleScanner::init / NimBLE is up).
  static void init();

  // True once a valid GPS fix has been written by the iPhone.
  static bool has_fix();

  static float latitude();
  static float longitude();
  static uint32_t last_update_ms();  // uptime ms of last fix

  // Distance in metres between two lat/lon points (haversine).
  static float distance_m(float lat1, float lon1, float lat2, float lon2);
  // Initial bearing (deg, 0 = north, clockwise) from point 1 to point 2.
  static float bearing_deg(float lat1, float lon1, float lat2, float lon2);
};
