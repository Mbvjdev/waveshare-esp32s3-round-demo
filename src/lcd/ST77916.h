#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lcd_st77916.h"

#define EXAMPLE_LCD_WIDTH                   (360)
#define EXAMPLE_LCD_HEIGHT                  (360)
#define EXAMPLE_LCD_COLOR_BITS              (16)

#define ESP_PANEL_HOST_SPI_ID_DEFAULT       (SPI2_HOST)
#define ESP_PANEL_LCD_SPI_MODE              (0)
#define ESP_PANEL_LCD_SPI_CLK_HZ            (80 * 1000 * 1000)
#define ESP_PANEL_LCD_SPI_TRANS_QUEUE_SZ    (10)
#define ESP_PANEL_LCD_SPI_CMD_BITS          (32)
#define ESP_PANEL_LCD_SPI_PARAM_BITS        (8)

#define ESP_PANEL_LCD_SPI_IO_TE             (18)
#define ESP_PANEL_LCD_SPI_IO_SCK            (40)
#define ESP_PANEL_LCD_SPI_IO_DATA0          (46)
#define ESP_PANEL_LCD_SPI_IO_DATA1          (45)
#define ESP_PANEL_LCD_SPI_IO_DATA2          (42)
#define ESP_PANEL_LCD_SPI_IO_DATA3          (41)
#define ESP_PANEL_LCD_SPI_IO_CS             (21)
#define EXAMPLE_LCD_PIN_NUM_RST             (-1)
#define EXAMPLE_LCD_PIN_NUM_BK_LIGHT        (5)
#define EXAMPLE_LCD_BK_LIGHT_ON_LEVEL       (1)
#define EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL      (!EXAMPLE_LCD_BK_LIGHT_ON_LEVEL)
#define ESP_PANEL_HOST_SPI_MAX_TRANSFER_SIZE (2048)

#define LEDC_HS_TIMER          LEDC_TIMER_0
#define LEDC_LS_MODE           LEDC_LOW_SPEED_MODE
#define LEDC_HS_CH0_GPIO       EXAMPLE_LCD_PIN_NUM_BK_LIGHT
#define LEDC_HS_CH0_CHANNEL    LEDC_CHANNEL_0
#define LEDC_ResolutionRatio   LEDC_TIMER_13_BIT
#define LEDC_MAX_Duty          ((1 << LEDC_ResolutionRatio) - 1)
#define Backlight_MAX          100

#define TCA9554_EXIO2          0x02

#ifdef __cplusplus
extern "C" {
#endif

extern esp_lcd_panel_handle_t panel_handle;
extern uint8_t LCD_Backlight;

// Implemented by the board-power layer in main.cpp.
void Set_EXIO(uint8_t pin, bool state);

void ST77916_Init(void);
void LCD_Init(void);
void LCD_addWindow(uint16_t x_start, uint16_t y_start,
                   uint16_t x_end, uint16_t y_end, uint16_t *color);
void Backlight_Init(void);
void Set_Backlight(uint8_t light);

#ifdef __cplusplus
}
#endif
