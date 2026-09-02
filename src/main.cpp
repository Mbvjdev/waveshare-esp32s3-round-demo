#include "lcd/ST77916.h"
#include "ble_scanner.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "driver/i2c_master.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr gpio_num_t kI2cSda = GPIO_NUM_11;
constexpr gpio_num_t kI2cScl = GPIO_NUM_10;
constexpr uint8_t kTcaAddress = 0x20;
constexpr int kLinesPerTransfer = 16;
constexpr int kWidth = 360;
constexpr int kHeight = 360;

// Dragon Ball radar palette (green sweep display).
constexpr uint16_t kRadarBg = 0x0000;         // black background
constexpr uint16_t kRadarGrid = 0x01E0;       // dim green ring/grid
constexpr uint16_t kRadarSweep = 0x07FF;      // bright cyan-green sweep edge
constexpr uint16_t kRadarSweepDim = 0x0280;   // sweep tail
constexpr uint16_t kBlipStrong = 0x07E0;      // near + strong = bright green
constexpr uint16_t kBlipMid = 0x4EC0;         // mid = yellow-green
constexpr uint16_t kBlipWeak = 0x0400;        // far = dim green
constexpr uint16_t kTextGreen = 0x07E0;

i2c_master_bus_handle_t i2c_bus = nullptr;
i2c_master_dev_handle_t tca_device = nullptr;
uint8_t tca_output_state = 0;

// Full-frame physics buffer in PSRAM; blitted to the panel in 16-line DMA chunks.
uint16_t *g_frame = nullptr;

uint16_t swap16(uint16_t v) { return static_cast<uint16_t>((v << 8) | (v >> 8)); }

struct Pt { int x; int y; };

void put_pixel(int x, int y, uint16_t color) {
  if (x < 0 || x >= kWidth || y < 0 || y >= kHeight) return;
  g_frame[y * kWidth + x] = color;
}

void fill_rect(int x0, int y0, int w, int h, uint16_t color) {
  int x1 = x0 + w, y1 = y0 + h;
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > kWidth) x1 = kWidth;
  if (y1 > kHeight) y1 = kHeight;
  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) {
      g_frame[y * kWidth + x] = color;
    }
  }
}

void draw_hline(int x0, int x1, int y, uint16_t color) {
  if (y < 0 || y >= kHeight) return;
  if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
  if (x0 < 0) x0 = 0;
  if (x1 >= kWidth) x1 = kWidth - 1;
  for (int x = x0; x <= x1; ++x) g_frame[y * kWidth + x] = color;
}

// Midpoint circle outline.
void draw_circle(int cx, int cy, int r, uint16_t color) {
  int x = r, y = 0, err = 1 - r;
  while (x >= y) {
    put_pixel(cx + x, cy + y, color);
    put_pixel(cx - x, cy + y, color);
    put_pixel(cx + x, cy - y, color);
    put_pixel(cx - x, cy - y, color);
    put_pixel(cx + y, cy + x, color);
    put_pixel(cx - y, cy + x, color);
    put_pixel(cx + y, cy - x, color);
    put_pixel(cx - y, cy - x, color);
    y++;
    if (err < 0) err += 2 * y + 1;
    else { x--; err += 2 * (y - x) + 1; }
  }
}

void fill_circle(int cx, int cy, int r, uint16_t color) {
  for (int dy = -r; dy <= r; ++dy) {
    int xspan = static_cast<int>(sqrtf(static_cast<float>(r * r - dy * dy)));
    draw_hline(cx - xspan, cx + xspan, cy + dy, color);
  }
}

// Distance from RSSI: classic free-space log model calibrated ~-45 dBm @ 1m, n=2.
float rssi_to_distance(int8_t rssi) {
  const float n = 2.2f;
  const float a = -40.0f;  // path loss at 1 m
  return powf(10.0f, (a - rssi) / (10.0f * n));
}

// Stable pseudo-angle (0..360) from a device MAC so a given object keeps its bearing.
uint32_t mac_hash_angle(uint64_t addr) {
  uint64_t v = addr * 0x9E3779B97F4A7C15ULL;
  return static_cast<uint32_t>((v ^ (v >> 33)) % 360u);
}

void blit_frame() {
  uint16_t *chunk = static_cast<uint16_t *>(
      heap_caps_malloc(kWidth * kLinesPerTransfer * sizeof(uint16_t),
                       MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  if (!chunk) return;
  for (int y = 0; y < kHeight; y += kLinesPerTransfer) {
    int lines = (y + kLinesPerTransfer < kHeight) ? kLinesPerTransfer : (kHeight - y);
    for (int l = 0; l < lines; ++l) {
      for (int x = 0; x < kWidth; ++x) {
        chunk[l * kWidth + x] = swap16(g_frame[(y + l) * kWidth + x]);
      }
    }
    esp_lcd_panel_draw_bitmap(panel_handle, 0, y, kWidth, y + lines, chunk);
  }
  free(chunk);
}

void init_board_i2c() {
  const i2c_master_bus_config_t bus_config = {
      .i2c_port = I2C_NUM_0,
      .sda_io_num = kI2cSda,
      .scl_io_num = kI2cScl,
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .glitch_ignore_cnt = 7,
      .flags = {.enable_internal_pullup = true},
  };
  ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &i2c_bus));
  const i2c_device_config_t device_config = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = kTcaAddress,
      .scl_speed_hz = 400000,
  };
  ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &device_config, &tca_device));
}

void write_tca_output() {
  const uint8_t payload[] = {0x01, tca_output_state};
  ESP_ERROR_CHECK(i2c_master_transmit(tca_device, payload, sizeof(payload), 1000));
}
}  // namespace

extern "C" void Set_EXIO(uint8_t pin, bool state) {
  if (pin == 0 || pin > 8 || tca_device == nullptr) return;
  const uint8_t mask = static_cast<uint8_t>(1u << (pin - 1));
  if (state) tca_output_state |= mask;
  else tca_output_state &= static_cast<uint8_t>(~mask);
  write_tca_output();
}

void draw_radar_frame(float sweep_deg) {
  const int cx = kWidth / 2, cy = kHeight / 2;
  const int max_r = 165;

  fill_rect(0, 0, kWidth, kHeight, kRadarBg);

  // Concentric range rings.
  for (int r = max_r; r > 0; r -= 33) {
    draw_circle(cx, cy, r, kRadarGrid);
  }
  // Crosshair ticks.
  draw_hline(cx - max_r, cx + max_r, cy, kRadarGrid);
  draw_hline(cx, cx, cy - max_r, kRadarGrid);
  for (int y = 0; y < kHeight; ++y) put_pixel(cx, y, kRadarGrid);  // vertical
  // Center origin dot.
  fill_circle(cx, cy, 4, kRadarSweep);

  // Sweep beam: draw a fading trail behind the leading edge.
  for (int i = 0; i < 46; ++i) {
    const float a = (sweep_deg - i * 1.6f) * 0.01745329252f;  // radians
    const float co = cosf(a), si = sinf(a);
    uint16_t c = (i < 8) ? kRadarSweep : kRadarSweepDim;
    for (int r = 4; r <= max_r - 2; r += 2) {
      int x = cx + static_cast<int>(r * co);
      int y = cy + static_cast<int>(r * si);
      put_pixel(x, y, c);
    }
  }

  // Device blips: stable bearing from MAC, range from RSSI.
  BleDevice devices[16];
  int n = BleScanner::snapshot(devices, 16);
  int shown = 0;
  for (int i = 0; i < n && shown < 8; ++i) {
    BleDevice &d = devices[i];
    if (d.rssi <= -90) continue;  // too far to matter
    // Clamp display radius: strong near centre, weak near outer ring.
    float strength = (static_cast<float>(d.rssi) + 90.0f) / 55.0f;  // -90..-35
    if (strength < 0.02f) strength = 0.02f;
    if (strength > 1.0f) strength = 1.0f;
    int radius = static_cast<int>(max_r * (1.0f - strength));
    uint32_t ang = mac_hash_angle(d.addr);
    float rad = ang * 0.01745329252f;
    int bx = cx + static_cast<int>(radius * cosf(rad));
    int by = cy + static_cast<int>(radius * sinf(rad));
    uint16_t color = (strength > 0.72f) ? kBlipStrong :
                     (strength > 0.45f) ? kBlipMid : kBlipWeak;
    int sz = (strength > 0.72f) ? 5 : 4;
    fill_circle(bx, by, sz, color);
    fill_circle(bx, by, sz + 2, 0x0180);  // halo
    fill_circle(bx, by, sz, color);
    shown++;
  }
}

extern "C" void app_main(void) {
  printf("\n=== Waveshare Round Lab / Dragon Ball Radar ===\n");
  printf("ST77916 | native esp_lcd QSPI | 80 MHz | BLE tracker\n");

  g_frame = static_cast<uint16_t *>(
      heap_caps_malloc(kWidth * kHeight * sizeof(uint16_t), MALLOC_CAP_SPIRAM));
  if (!g_frame) {
    printf("[radar] frame buffer allocation failed\n");
  }

  init_board_i2c();
  tca_output_state = 0;
  write_tca_output();
  printf("[board] TCA9554 ready, LCD reset through EXIO2\n");

  LCD_Init();
  if (panel_handle == nullptr) {
    printf("[lcd] native ST77916 init failed\n");
    while (true) vTaskDelay(pdMS_TO_TICKS(1000));
  }
  printf("[lcd] native ST77916 init complete\n");

  BleScanner::init();
  printf("[radar] BLE scanner started\n");

  float sweep = 0.0f;
  while (true) {
    draw_radar_frame(sweep);
    if (g_frame) blit_frame();
    sweep += 10.0f;
    if (sweep >= 360.0f) sweep -= 360.0f;
    vTaskDelay(pdMS_TO_TICKS(40));  // ~25 fps sweep
  }
}
