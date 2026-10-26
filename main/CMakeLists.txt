idf_component_register(SRCS "main.cpp" 
                       INCLUDE_DIRS "."
                       REQUIRES  driver  "esp_io" 
                       )

target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++17)