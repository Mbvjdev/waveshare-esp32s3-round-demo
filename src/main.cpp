#include "lcd/ST77916.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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

// RGB565 values are byte-swapped because esp_lcd transmits the framebuffer
// bytes in memory order on the quad-SPI bus.
constexpr uint16_t kBlack = 0x0000;
constexpr uint16_t kWhite = 0xFFFF;
constexpr uint16_t kYellow = 0xFFE0;
constexpr uint16_t kCyan = 0x07FF;
constexpr uint16_t kGreen = 0x07E0;
constexpr uint16_t kMagenta = 0xF81F;
constexpr uint16_t kRed = 0xF800;
constexpr uint16_t kBlue = 0x001F;

i2c_master_bus_handle_t i2c_bus = nullptr;
i2c_master_dev_handle_t tca_device = nullptr;
uint8_t tca_output_state = 0;

uint16_t bus_color(uint16_t rgb565) {
  return static_cast<uint16_t>((rgb565 << 8) | (rgb565 >> 8));
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
}

extern "C" void Set_EXIO(uint8_t pin, bool state) {
  if (pin == 0 || pin > 8 || tca_device == nullptr) {
    return;
  }
  const uint8_t mask = static_cast<uint8_t>(1u << (pin - 1));
  if (state) {
    tca_output_state |= mask;
  } else {
    tca_output_state &= static_cast<uint8_t>(~mask);
  }
  write_tca_output();
}

static uint16_t pattern_color(int y, int x) {
  if (y < 216) {
    constexpr uint16_t bars[] = {kWhite, kYellow, kCyan, kGreen,
                                 kMagenta, kRed, kBlue};
    const int bar = (x * 7) / 360;
    return bars[bar < 7 ? bar : 6];
  }
  if (y < 288) {
    constexpr uint16_t gray[] = {kBlack, 0x2104, 0x4208, 0x630C,
                                 0x8410, 0xA514, 0xC618, kWhite};
    return gray[(x / 45) < 8 ? (x / 45) : 7];
  }
  constexpr uint16_t bottom[] = {kBlue, kBlack, kMagenta, kCyan, kWhite};
  const int block = x / 72;
  return bottom[block < 5 ? block : 4];
}

void draw_smpte_pattern() {
  uint16_t *buffer = static_cast<uint16_t *>(
      heap_caps_malloc(EXAMPLE_LCD_WIDTH * kLinesPerTransfer * sizeof(uint16_t),
                       MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  if (buffer == nullptr) {
    printf("[lcd] DMA buffer allocation failed\n");
    return;
  }

  for (int y = 0; y < EXAMPLE_LCD_HEIGHT; y += kLinesPerTransfer) {
    const int y_end = (y + kLinesPerTransfer < EXAMPLE_LCD_HEIGHT)
                          ? y + kLinesPerTransfer
                          : EXAMPLE_LCD_HEIGHT;
    const int lines = y_end - y;
    for (int line = 0; line < lines; ++line) {
      for (int x = 0; x < EXAMPLE_LCD_WIDTH; ++x) {
        buffer[line * EXAMPLE_LCD_WIDTH + x] = bus_color(pattern_color(y + line, x));
      }
    }
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(
        panel_handle, 0, y, EXAMPLE_LCD_WIDTH, y_end, buffer));
  }
  free(buffer);
  printf("[lcd] native SMPTE frame sent once\n");
}

extern "C" void app_main(void) {
  printf("\n=== Waveshare Round Lab / native LCD smoke test ===\n");
  printf("ST77916 | 360x360 | native esp_lcd QSPI | 80 MHz\n");

  init_board_i2c();
  tca_output_state = 0;
  write_tca_output();
  printf("[board] TCA9554 ready, LCD reset controlled through EXIO2\n");

  LCD_Init();
  if (panel_handle == nullptr) {
    printf("[lcd] native ST77916 init failed\n");
    while (true) {
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }

  printf("[lcd] native ST77916 init complete\n");
  draw_smpte_pattern();

  while (true) {
    // Deliberately do not redraw. This is the anti-flicker baseline.
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
