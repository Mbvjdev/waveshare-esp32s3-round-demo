#include "gps_receiver.h"

#include <math.h>

#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "services/gap/ble_svc_gap.h"

namespace {
constexpr const char *kTag = "gps";

// Custom 128-bit service UUID for the board-to-phone GPS link.
// 6e400001-b5a3-f393-e0a9-e50e24dcca9e
const ble_uuid128_t kSvcUuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa5, 0xb3, 0x01, 0x00, 0x00, 0x6e);
// 6e400002-b5a3-f393-e0a9-e50e24dcca9e (write GPS from phone)
const ble_uuid128_t kGpsChrUuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa5, 0xb3, 0x02, 0x00, 0x00, 0x6e);

float g_lat = 0.0f;
float g_lon = 0.0f;
bool g_has_fix = false;
uint32_t g_last_ms = 0;
float g_last_lat = 0.0f;
float g_last_lon = 0.0f;
uint16_t g_gps_handle = 0;

ble_gap_event_fn *g_adv_cb = nullptr;
void *g_adv_arg = nullptr;
}  // namespace

static int gps_gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)conn_handle;
  (void)attr_handle;
  (void)arg;
  if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
    uint8_t buf[8];
    uint16_t len = 8;
    const int rc = ble_hs_mbuf_to_flat(ctxt->om, buf, len, &len);
    if (rc == 0 && len == 8) {
      // Payload: two 32-bit floats, little-endian (Android/iOS packed).
      float lat = 0.0f, lon = 0.0f;
      memcpy(&lat, buf, 4);
      memcpy(&lon, buf + 4, 4);
      // Sanity: valid lat/lon range.
      if (lat >= -90.0f && lat <= 90.0f && lon >= -180.0f && lon <= 180.0f) {
        g_lat = lat;
        g_lon = lon;
        g_last_lat = lat;
        g_last_lon = lon;
        g_has_fix = true;
        g_last_ms = esp_log_timestamp();
        ESP_LOGI(kTag, "GPS fix: %.6f, %.6f", lat, lon);
      }
    }
    return 0;
  }
  return BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_chr_def kGpsChars[] = {
    {
        .uuid = &kGpsChrUuid.u,
        .access_cb = gps_gatt_access,
        .arg = nullptr,
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
        .min_key_size = 0,
        .val_handle = &g_gps_handle,
        .cpfd = nullptr,
    },
    {0},
};

static const struct ble_gatt_svc_def kGpsSvc[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &kSvcUuid.u,
        .includes = nullptr,
        .characteristics = kGpsChars,
    },
    {0},
};

static void advertise() {
  struct ble_hs_adv_fields fields = {0};
  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  fields.name = (uint8_t *)"RadarGPS";
  fields.name_len = strlen((char *)fields.name);
  fields.name_is_complete = 1;
  ble_gap_adv_set_fields(&fields);

  struct ble_gap_adv_params params = {0};
  params.conn_mode = BLE_GAP_CONN_MODE_UND;
  params.disc_mode = BLE_GAP_DISC_MODE_GEN;
  ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, nullptr, 0, &params, g_adv_cb, g_adv_arg);
}

void GpsReceiver::init() {
  const int rc_svcs = ble_gatts_count_cfg(kGpsSvc);
  if (rc_svcs != 0) {
    ESP_LOGE(kTag, "ble_gatts_count_cfg failed: %d", rc_svcs);
    return;
  }
  const int rc_add = ble_gatts_add_svcs(kGpsSvc);
  if (rc_add != 0) {
    ESP_LOGE(kTag, "ble_gatts_add_svcs failed: %d", rc_add);
    return;
  }
}

// Callback hook so advertising starts after the GAP sync; registered by the
// NimBLE sync path in the app before GpsReceiver is used.
void gps_register_adv(ble_gap_event_fn *cb, void *arg) {
  g_adv_cb = cb;
  g_adv_arg = arg;
}

bool GpsReceiver::has_fix() { return g_has_fix; }
float GpsReceiver::latitude() { return g_lat; }
float GpsReceiver::longitude() { return g_lon; }
uint32_t GpsReceiver::last_update_ms() { return g_last_ms; }

float GpsReceiver::distance_m(float lat1, float lon1, float lat2, float lon2) {
  const float r = 6371000.0f;
  float p1 = lat1 * 3.141592653589793f / 180.0f;
  float p2 = lat2 * 3.141592653589793f / 180.0f;
  float dp = (lat2 - lat1) * 3.141592653589793f / 180.0f;
  float dl = (lon2 - lon1) * 3.141592653589793f / 180.0f;
  float a = sinf(dp / 2) * sinf(dp / 2) +
            cosf(p1) * cosf(p2) * sinf(dl / 2) * sinf(dl / 2);
  float c = 2 * atan2f(sqrtf(a), sqrtf(1 - a));
  return r * c;
}

float GpsReceiver::bearing_deg(float lat1, float lon1, float lat2, float lon2) {
  float p1 = lat1 * 3.141592653589793f / 180.0f;
  float p2 = lat2 * 3.141592653589793f / 180.0f;
  float dl = (lon2 - lon1) * 3.141592653589793f / 180.0f;
  float y = sinf(dl) * cosf(p2);
  float x = cosf(p1) * sinf(p2) - sinf(p1) * cosf(p2) * cosf(dl);
  float brng = atan2f(y, x) * 180.0f / 3.141592653589793f;
  if (brng < 0) brng += 360.0f;
  return brng;
}
