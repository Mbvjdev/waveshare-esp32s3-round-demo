from pathlib import Path

ROOT = Path(__file__).parents[1]


def test_platformio_targets_waveshare_board():
    config = (ROOT / "platformio.ini").read_text()
    assert "board = esp32-s3-devkitc-1" in config
    assert "framework = espidf" in config
    assert "platformio/framework-espidf @ 3.50301.0" in config


def test_firmware_uses_native_st77916_headers():
    source = (ROOT / "src" / "main.cpp").read_text()
    for token in (
        "LCD_Init()",
        "panel_handle",
        "esp_lcd_panel_draw_bitmap",
    ):
        assert token in source


def test_lcd_files_are_present():
    assert (ROOT / "src" / "lcd" / "ST77916.c").exists()
    assert (ROOT / "src" / "lcd" / "esp_lcd_st77916.c").exists()
    assert (ROOT / "src" / "lcd" / "ST77916.h").exists()
    assert (ROOT / "src" / "lcd" / "esp_lcd_st77916.h").exists()


def test_firmware_contains_verified_board_pinmap():
    source = (ROOT / "src" / "lcd" / "ST77916.h").read_text()
    for token in (
        "ESP_PANEL_LCD_SPI_IO_DATA0          (46)",
        "ESP_PANEL_LCD_SPI_IO_DATA1          (45)",
        "ESP_PANEL_LCD_SPI_IO_DATA2          (42)",
        "ESP_PANEL_LCD_SPI_IO_DATA3          (41)",
        "ESP_PANEL_LCD_SPI_IO_SCK            (40)",
        "ESP_PANEL_LCD_SPI_IO_CS             (21)",
        "EXAMPLE_LCD_PIN_NUM_BK_LIGHT        (5)",
    ):
        assert token in source


if __name__ == "__main__":
    test_platformio_targets_waveshare_board()
    test_firmware_uses_native_st77916_headers()
    test_lcd_files_are_present()
    test_firmware_contains_verified_board_pinmap()
    print("project file checks passed")
