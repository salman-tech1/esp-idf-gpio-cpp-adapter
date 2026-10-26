idf_component_register(SRCS  "esp_io.cpp"
                       INCLUDE_DIRS "."
                        REQUIRES  driver  "esp_driver_gpio"   
                       )

target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++17)