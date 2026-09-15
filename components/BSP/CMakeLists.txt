idf_component_register(
    SRCS "Servo/servo.c" "Rain_Sensor/rain_sensor.c"
    INCLUDE_DIRS "Servo" "Rain_Sensor"
    REQUIRES esp_driver_gpio
    PRIV_REQUIRES esp_driver_ledc
)
