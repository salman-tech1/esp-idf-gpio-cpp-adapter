

extern "C"
{
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
}
#include <cstdio> // C++ is reverse compatible c api's works

#include "esp_io.hpp"

static const char *TAG = "MAIN";

static bool is_button_pressed = false;

//
void button_handler(int fd, gpio_num_t pin, int level)
{
    //
    std::printf("file descriptor : %d  Pin : %d  Level : %d \n", fd, pin, level);

    // button_pressed to true
    is_button_pressed = !is_button_pressed;
}

extern "C" void app_main()
{
    // Led Configuration :
    esp_io::io_config led_cfg =
        {
            .pin = GPIO_NUM_19,
            .pull = GPIO_FLOATING,
            .drive = GPIO_DRIVE_CAP_3,
            .intr = GPIO_INTR_DISABLE,
            .mode = GPIO_MODE_OUTPUT,

    };
    // this will return the file descriptor or handler number
    int led = esp_io::open(led_cfg, O_WRONLY, nullptr);

    if (led == -1)
    {
        std::printf("GPIO open failed\n");
    }

    // button configuration
    esp_io::io_config button_cfg =
        {
            .pin = GPIO_NUM_20,
            .pull = GPIO_PULLUP_ONLY,
            .drive = GPIO_DRIVE_CAP_DEFAULT,
            .intr = GPIO_INTR_NEGEDGE,
            .mode = GPIO_MODE_INPUT,
    };

    // this will return the file descriptor or handler number :
    int button = esp_io::open(button_cfg, O_RDONLY, button_handler);

    if (button == -1)
    {
        std::printf("GPIO open failed\n");
    }

    while (true)
    {
        if (is_button_pressed == true)
        {
            gpio_set_level(GPIO_NUM_19, 1);
        }
        else
        {
            gpio_set_level(GPIO_NUM_19, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}