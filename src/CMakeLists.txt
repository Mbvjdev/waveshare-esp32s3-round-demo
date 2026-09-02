idf_component_register(
    SRCS "main.cpp" "ble_scanner.cpp" "gyro.cpp" "board.cpp" "lcd/ST77916.c" "lcd/esp_lcd_st77916.c"
    INCLUDE_DIRS "." "lcd"
    REQUIRES driver esp_lcd bt
)
