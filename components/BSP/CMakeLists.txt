idf_component_register(
    SRCS "Servo/servo.c" "Rain_Sensor/rain_sensor.c" "SHT20/sht30.c" "Key/key.c" "RTC/rtc.c"
    INCLUDE_DIRS "Servo" "Rain_Sensor" "SHT20" "Key" "RTC"
    REQUIRES esp_driver_gpio esp_driver_i2c
    PRIV_REQUIRES esp_driver_ledc freertos esp_timer
)
