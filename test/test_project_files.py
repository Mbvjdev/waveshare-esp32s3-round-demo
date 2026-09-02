from pathlib import Path

ROOT = Path(__file__).parents[1]


def test_platformio_targets_waveshare_board():
    config = (ROOT / "platformio.ini").read_text()
    assert "board = esp32-s3-devkitc-1" in config
    assert "board_build.flash_size = 16MB" in config
    assert "board_build.arduino.memory_type = qio_opi" in config


def test_firmware_contains_verified_board_pinmap():
    source = (ROOT / "src" / "main.cpp").read_text()
    for token in (
        "LCD_CS = 21",
        "LCD_SCK = 40",
        "I2C_SDA = 11",
        "I2C_SCL = 10",
        "BATTERY_ADC = 8",
    ):
        assert token in source


if __name__ == "__main__":
    test_platformio_targets_waveshare_board()
    test_firmware_contains_verified_board_pinmap()
    print("project file checks passed")
