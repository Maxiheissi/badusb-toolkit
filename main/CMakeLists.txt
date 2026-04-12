idf_component_register(
    SRCS "badUSB_main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_driver_gpio
    )

littlefs_create_partition_image(storage ../flash_data FLASH_IN_PROJECT)    
