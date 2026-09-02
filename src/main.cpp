#include "lcd/ST77916.h"
#include "ble_scanner.h"
#include "board.h"
#include "gyro.h"

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
constexpr uint16_t kRadarBg = 0x0000;
constexpr uint16_t kRadarGrid = 0x01E0;
constexpr uint16_t kRadarSweep = 0x07FF;
constexpr uint16_t kRadarSweepDim = 0x0280;
constexpr uint16_t kBlipStrong = 0x07E0;
constexpr uint16_t kBlipMid = 0x4EC0;
constexpr uint16_t kBlipWeak = 0x0400;
constexpr uint16_t kHeadingArrow = 0xFEA0;  // gold heading marker

i2c_master_dev_handle_t g_tca_device = nullptr;
uint8_t g_tca_output_state = 0;
uint16_t *g_frame = nullptr;
float g_yaw = 0.0f;

uint16_t swap16(uint16_t v) { return static_cast<uint16_t>((v << 8) | (v >> 8)); }

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
  for (int y = y0; y < y1; ++y)
    for (int x = x0; x < x1; ++x) g_frame[y * kWidth + x] = color;
}

void draw_hline(int x0, int x1, int y, uint16_t color) {
  if (y < 0 || y >= kHeight) return;
  if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
  if (x0 < 0) x0 = 0;
  if (x1 >= kWidth) x1 = kWidth - 1;
  for (int x = x0; x <= x1; ++x) g_frame[y * kWidth + x] = color;
}

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

// Map a relative bearing (deg, -180..180, 0 = ahead) to a pixel along a ray.
void blit_frame() {
  uint16_t *chunk = static_cast<uint16_t *>(
      heap_caps_malloc(kWidth * kLinesPerTransfer * sizeof(uint16_t),
                       MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  if (!chunk) return;
  for (int y = 0; y < kHeight; y += kLinesPerTransfer) {
    int lines = (y + kLinesPerTransfer < kHeight) ? kLinesPerTransfer : (kHeight - y);
    for (int l = 0; l < lines; ++l)
      for (int x = 0; x < kWidth; ++x)
        chunk[l * kWidth + x] = swap16(g_frame[(y + l) * kWidth + x]);
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
  ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &g_i2c_bus));
  board_i2c_add(kTcaAddress, &g_tca_device);
}

void write_tca_output() {
  const uint8_t payload[] = {0x01, g_tca_output_state};
  ESP_ERROR_CHECK(i2c_master_transmit(g_tca_device, payload, sizeof(payload), 1000));
}
}  // namespace

extern "C" void Set_EXIO(uint8_t pin, bool state) {
  if (pin == 0 || pin > 8 || g_tca_device == nullptr) return;
  const uint8_t mask = static_cast<uint8_t>(1u << (pin - 1));
  if (state) g_tca_output_state |= mask;
  else g_tca_output_state &= static_cast<uint8_t>(~mask);
  write_tca_output();
}

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

void draw_radar_frame(float sweep_deg) {
  const int cx = kWidth / 2, cy = kHeight / 2;
  const int max_r = 165;

  fill_rect(0, 0, kWidth, kHeight, kRadarBg);

  // Concentric range rings.
  for (int r = max_r; r > 0; r -= 33) draw_circle(cx, cy, r, kRadarGrid);
  draw_hline(cx - max_r, cx + max_r, cy, kRadarGrid);
  for (int y = 0; y < kHeight; ++y) put_pixel(cx, y, kRadarGrid);
  fill_circle(cx, cy, 4, kRadarSweep);

  // Sweep beam with fading trailing edge (always rotates).
  for (int i = 0; i < 46; ++i) {
    float a = (sweep_deg - i * 1.6f) * 0.01745329252f;
    float co = cosf(a), si = sinf(a);
    uint16_t c = (i < 8) ? kRadarSweep : kRadarSweepDim;
    for (int r = 4; r <= max_r - 2; r += 2) {
      put_pixel(cx + static_cast<int>(r * co), cy + static_cast<int>(r * si), c);
    }
  }

  // Heading marker: gold triangle pinning "ahead" (board forward).
  for (int i = 0; i < 12; ++i) {
    float a = (90.0f - 6.0f + i) * 0.01745329252f;  // small arc at top
    put_pixel(cx + static_cast<int>(8 * cosf(a)),
              cy - static_cast<int>(8 * sinf(a)), kHeadingArrow);
  }

  // Device blips with true direction finding: the blip is placed at the
  // bearing where this device was strongest, relative to the board's current
  // heading. Point the board at a device and it rises straight up (ahead).
  BleDevice devices[16];
  int n = BleScanner::snapshot(devices, 16);
  int shown = 0;
  for (int i = 0; i < n && shown < 8; ++i) {
    BleDevice &d = devices[i];
    if (d.bestRssi <= -90) continue;
    float strength = (static_cast<float>(d.bestRssi) + 90.0f) / 55.0f;
    strength = clampf(strength, 0.02f, 1.0f);
    int radius = static_cast<int>(max_r * (1.0f - strength));  // near = inner
    // Relative bearing: device bearing minus current board heading.
    float rel = d.bestYaw - g_yaw;
    while (rel > 180.0f) rel -= 360.0f;
    while (rel < -180.0f) rel += 360.0f;
    float rad = rel * 0.01745329252f;
    int bx = cx + static_cast<int>(radius * sinf(rad));
    int by = cy - static_cast<int>(radius * cosf(rad));
    uint16_t color = (strength > 0.72f) ? kBlipStrong :
                     (strength > 0.45f) ? kBlipMid : kBlipWeak;
    int sz = (strength > 0.72f) ? 5 : 4;
    fill_circle(bx, by, sz + 2, 0x0180);
    fill_circle(bx, by, sz, color);
    shown++;
  }
}

extern "C" void app_main(void) {
  printf("\n=== Waveshare Round Lab / Dragon Ball Radar ===\n");
  printf("ST77916 | native esp_lcd QSPI | 80 MHz | BLE tracker + heading\n");

  g_frame = static_cast<uint16_t *>(
      heap_caps_malloc(kWidth * kHeight * sizeof(uint16_t), MALLOC_CAP_SPIRAM));
  if (!g_frame) printf("[radar] frame buffer allocation failed\n");

  init_board_i2c();
  g_tca_output_state = 0;
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

  Gyro::begin();
  printf("[radar] gyro heading: %s\n", Gyro::is_present() ? "QMI8658 OK" : "not detected");

  float sweep = 0.0f;
  uint32_t last = esp_log_timestamp();
  while (true) {
    uint32_t now = esp_log_timestamp();
    float dt = (now - last) / 1000.0f;
    last = now;
    if (dt < 0.001f) dt = 0.001f;
    if (dt > 0.1f) dt = 0.1f;

    Gyro::tick(dt);
    g_yaw = Gyro::yaw_deg();
    BleScanner::set_current_yaw(g_yaw);

    draw_radar_frame(sweep);
    if (g_frame) blit_frame();
    sweep += 10.0f;
    if (sweep >= 360.0f) sweep -= 360.0f;
    vTaskDelay(pdMS_TO_TICKS(40));
  }
}
