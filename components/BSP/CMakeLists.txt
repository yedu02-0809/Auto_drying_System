idf_component_register(
    SRCS "Servo/servo.c"
    INCLUDE_DIRS "Servo"
    REQUIRES esp_driver_gpio
    PRIV_REQUIRES esp_driver_ledc
)
