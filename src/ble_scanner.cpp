#include "ble_scanner.h"

#include <cstring>
#include <string>

#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"

namespace {
constexpr const char *kTag = "ble-scanner";

BleDevice g_devices[16];
int g_deviceCount = 0;
bool g_started = false;
bool g_synced = false;
float g_current_yaw = 0.0f;

// We only want advertisements that are likely to be a real nearby object
// (phones, tags, earbuds). We ignore empty/unknown payloads with a weak RSSI.
int8_t kWorstRssiToKeep = -95;

void parseLocalName(const uint8_t *data, uint8_t length, BleDevice *dev) {
  if (data == nullptr || length == 0) {
    return;
  }
  const uint8_t *p = data;
  const uint8_t *end = data + length;
  while (p < end) {
    const uint8_t len = *p++;
    if (len == 0) {
      break;
    }
    if (p + len - 1 > end) {
      break;
    }
    const uint8_t type = *p++;
    // 0x08 = shortened local name, 0x09 = complete local name.
    if (type == 0x08 || type == 0x09) {
      const uint8_t name_len = len - 1;
      if (name_len > 0 && name_len < sizeof(dev->name)) {
        std::memcpy(dev->name, p, name_len);
        dev->name[name_len] = '\0';
        dev->hasName = true;
      }
      return;
    }
    p += (len - 1);
  }
}

// Returns index of existing entry for addr, or -1.
int findDevice(uint64_t addr) {
  for (int i = 0; i < g_deviceCount; ++i) {
    if (g_devices[i].addr == addr) {
      return i;
    }
  }
  return -1;
}

int on_gap_events(struct ble_gap_event *event, void *arg) {
  (void)arg;
  if (event->type == BLE_GAP_EVENT_DISC) {
    const uint64_t addr =
        (static_cast<uint64_t>(event->disc.addr.val[5]) << 40) |
        (static_cast<uint64_t>(event->disc.addr.val[4]) << 32) |
        (static_cast<uint64_t>(event->disc.addr.val[3]) << 24) |
        (static_cast<uint64_t>(event->disc.addr.val[2]) << 16) |
        (static_cast<uint64_t>(event->disc.addr.val[1]) << 8) |
        static_cast<uint64_t>(event->disc.addr.val[0]);

    const int8_t rssi = event->disc.rssi;
    if (rssi < kWorstRssiToKeep) {
      return 0;
    }

    int idx = findDevice(addr);
    if (idx < 0) {
      if (g_deviceCount >= kMaxBleDevices) {
        // Drop the weakest entry to make room.
        int weakest = 0;
        for (int i = 1; i < g_deviceCount; ++i) {
          if (g_devices[i].rssi < g_devices[weakest].rssi) {
            weakest = i;
          }
        }
        if (g_devices[weakest].rssi < rssi) {
          g_devices[weakest] = BleDevice{};
          idx = weakest;
        } else {
          return 0;
        }
      }
      if (idx < 0) {
        idx = g_deviceCount++;
      }
      g_devices[idx] = BleDevice{};
      g_devices[idx].addr = addr;
      g_devices[idx].addrType = event->disc.addr.type;
    }

    g_devices[idx].rssi = rssi;
    g_devices[idx].lastSeenMs = 0;  // Not used; RSSI-only range.
    // Direction finding: remember the board heading where this device was
    // strongest, so the radar can point the blip toward it.
    if (rssi > g_devices[idx].bestRssi) {
      g_devices[idx].bestRssi = rssi;
      g_devices[idx].bestYaw = g_current_yaw;
    }
    if (!g_devices[idx].hasName) {
      parseLocalName(event->disc.data, event->disc.length_data, &g_devices[idx]);
    }
    if (g_devices[idx].hasName) {
      ESP_LOGI(kTag, "rssi=%d dBm name=%s", rssi, g_devices[idx].name);
    } else {
      ESP_LOGI(kTag, "rssi=%d dBm addr=%02x%02x%02x%02x%02x%02x", rssi,
               event->disc.addr.val[5], event->disc.addr.val[4],
               event->disc.addr.val[3], event->disc.addr.val[2],
               event->disc.addr.val[1], event->disc.addr.val[0]);
    }
  } else if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
    // Re-arm the scan so the radar keeps seeing moving devices.
    struct ble_gap_disc_params disc_params = {0};
    disc_params.filter_policy = 0;
    disc_params.passive = 0;
    disc_params.filter_duplicates = 0;
    disc_params.itvl = 0x0010;   // 10 ms
    disc_params.window = 0x0010;
    ble_gap_disc(BLE_ADDR_PUBLIC, 3000, &disc_params, on_gap_events, nullptr);
  }
  return 0;
}

void run_scan() {
  struct ble_gap_disc_params disc_params = {0};
  disc_params.filter_policy = 0;
  disc_params.passive = 0;
  disc_params.filter_duplicates = 0;
  disc_params.itvl = 0x0010;
  disc_params.window = 0x0010;
  const int rc = ble_gap_disc(BLE_ADDR_PUBLIC, 3000, &disc_params, on_gap_events, nullptr);
  if (rc == 0) {
    ESP_LOGI(kTag, "BLE scan started");
  } else {
    ESP_LOGE(kTag, "ble_gap_disc failed: %d", rc);
  }
}

void on_sync(void) {
  g_synced = true;
  run_scan();
}

void on_reset(int reason) {
  g_synced = false;
  ESP_LOGE(kTag, "BLE host reset: %d", reason);
}

void host_task(void *param) {
  (void)param;
  nimble_port_run();
  nimble_port_freertos_deinit();
}
}  // namespace

void BleScanner::init() {
  if (g_started) {
    return;
  }
  g_started = true;

  ESP_ERROR_CHECK(nimble_port_init());
  ble_hs_cfg.reset_cb = on_reset;
  ble_hs_cfg.sync_cb = on_sync;
  nimble_port_freertos_init(host_task);

  const uint8_t own_addr_type = BLE_OWN_ADDR_PUBLIC;
  ble_hs_util_ensure_addr(own_addr_type);
}

void BleScanner::poll() {
  // NimBLE runs on its own FreeRTOS task; nothing to do here per tick.
}

void BleScanner::set_current_yaw(float yaw_deg) { g_current_yaw = yaw_deg; }

int BleScanner::snapshot(BleDevice *out, int capacity) {
  int n = g_deviceCount < capacity ? g_deviceCount : capacity;
  for (int i = 0; i < n; ++i) {
    out[i] = g_devices[i];
  }
  return g_deviceCount;
}
