extern "C"
{
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
}
#include "esp_io.hpp"

#include <cerrno>
#include <fcntl.h>

// Static implementation
namespace
{
    // this will attached to each gpio handle
    struct device
    {
        bool is_open = false;    // track if the device is open or not
        esp_io::io_config cfg{}; // configuration of io
        int flags = 0;

        // interrupt isr Callback Support
        uint32_t slot = 0; // slot stores the device's array index. fd 3 to 8
        // For example, if GPIO 5 uses devices[2]:
        // slot = 2 ;
        uint32_t generation = 0; // Is this event from the current use of that entry?
        bool handler_attached = false;
        esp_io::gpio_callback_t callback = nullptr;
    };

    //
    struct gpio_event
    {
        uint32_t slot;
        uint32_t generation;
    };

    // tracks down each gpio
    device devices[esp_io::MAX_DEVICES]{};
    /*
    this is standard convention
    0	stdin — standard input
    1	stdout — standard output
    2	stderr — standard error
    3 and above	Available for your GPIO driver
    */
    constexpr int FIRST_FD = 3; // first file descriptor will start from 3

    QueueHandle_t gpio_evt_queue = nullptr;
    TaskHandle_t gpio_task_h = nullptr;

    static void IRAM_ATTR gpio_isr_handler(void *arg)
    {
        BaseType_t task_woken = pdFALSE;

        device *dev = static_cast<device *>(arg);
        gpio_event event{};

        // store the slot and generation here
        event.slot = dev->slot;
        event.generation = dev->generation;

        if (gpio_evt_queue != nullptr)
        {
            // send the event data from isr
            xQueueSendFromISR(gpio_evt_queue, &event, &task_woken);
        }

        if (task_woken == pdTRUE)
        {
            portYIELD_FROM_ISR();
        }
    }

    // this is the worker task that will call our Application level task

    static void gpio_task(void *arg)
    {
        (void)arg;
        // the data to recieve
        gpio_event event{};
        for (;;)
        {

            if (xQueueReceive(gpio_evt_queue, &event, portMAX_DELAY) != pdTRUE)
            {
                continue;
            }

            esp_io::gpio_callback_t callback = nullptr;
            gpio_num_t pin = GPIO_NUM_NC;
            int fd = -1;

            if (event.slot < esp_io::MAX_DEVICES)
            {
                // the slot will
                device &dev = devices[event.slot];

                if (dev.is_open && dev.handler_attached &&
                    dev.generation == event.generation)
                {
                    callback = dev.callback;
                    pin = dev.cfg.pin;

                    //
                    fd = FIRST_FD + static_cast<int>(event.slot);
                }
            }

            // Call application code outside the critical section.
            if (callback != nullptr)
            {
                callback(fd, pin, gpio_get_level(pin));
            }
        }
    }

    // setting
    static esp_err_t set_isr_sys()
    {
        // Intended for normal task context. Concurrent first-time calls to
        // open() should be serialized by the caller.
        if (gpio_evt_queue != nullptr)
        {
            return ESP_OK;
        }

        QueueHandle_t queue = xQueueCreate(10, sizeof(gpio_event));
        if (queue == nullptr)
        {
            return ESP_ERR_NO_MEM;
        }

        // which allows per pin interrupt handler
        esp_err_t err = gpio_install_isr_service(0);
        if (err != ESP_OK)
        {
            vQueueDelete(queue);
            // ESP_ERR_INVALID_STATE means another component already installed
            // the service. This driver does not assume ownership of that service.
            return err;
        }

        // Publish the queue handle BEFORE starting the task.
        gpio_evt_queue = queue;

        TaskHandle_t gpio_task_ = nullptr;

        if (xTaskCreate(gpio_task, "gpio_task_", 3072, nullptr, 10,
                        &gpio_task_) != pdPASS)
        {
            gpio_evt_queue = nullptr;
            gpio_uninstall_isr_service();
            vQueueDelete(queue);
            return ESP_ERR_NO_MEM;
        }

        gpio_task_h = gpio_task_;

        return ESP_OK;
    }

    // look down which access mode OUTPUT INPUT
    bool check_access_mode(int flags)
    {
        // This is posix style
        return flags == O_RDONLY || flags == O_WRONLY;
    }

    // return device  using file descriptor number
    device *get_device(int fd)
    {
        if (fd < FIRST_FD || fd >= FIRST_FD + esp_io::MAX_DEVICES)
        {
            errno = EBADF; // this is posix error tracking variable
            return nullptr;
        }

        // get the device
        device *dev = &(devices[fd - FIRST_FD]);
        if (!dev->is_open)
        {
            errno = EBADF;
            return nullptr;
        }

        return dev;
    }
};

namespace esp_io
{

    int open(io_config cfg, int flags, gpio_callback_t callback)
    {
        // check if interrupt are enabled or not
        const bool interrupt_enabled = (cfg.intr != GPIO_INTR_DISABLE);

        if (!GPIO_IS_VALID_GPIO(cfg.pin))
        {
            errno = EINVAL; // posix invalid argument
            return -1;      // for error
        }

        // Read or Write : input or output
        if (!check_access_mode(flags))
        {
            errno = EINVAL;
            return -1;
        }
        else
        {

            if (flags == O_RDONLY)
            {
                if (cfg.mode != GPIO_MODE_INPUT ||
                    (interrupt_enabled && callback == nullptr) ||
                    (!interrupt_enabled && callback != nullptr))
                {
                    errno = EINVAL;
                    return -1;
                }
            }
            else if (flags == O_WRONLY)
            {
                if (cfg.mode != GPIO_MODE_OUTPUT || interrupt_enabled ||
                    callback != nullptr || !GPIO_IS_VALID_OUTPUT_GPIO(cfg.pin))
                {
                    errno = EINVAL;
                    return -1;
                }
            }
        }

        if (interrupt_enabled)
        {
            // set interrupt
            esp_err_t err = set_isr_sys();
            if (err != ESP_OK)
            {
                errno = (err == ESP_ERR_NO_MEM) ? ENOMEM : EIO;
                return -1;
            }
        }

        int index = -1; // track look for the available index and return it
        // find empty device handler : check if the device is already initialized
        for (const device &dev : devices)
        {
            //
            if (dev.is_open && dev.cfg.pin == cfg.pin)
            {
                errno = EBUSY; // posix busy flag
                return -1;
            }
        }

        for (int i = 0; i < MAX_DEVICES; ++i)
        {
            // check if the device is not opened
            if (!devices[i].is_open)
            {
                index = i;
                // get the handler temporary
                device &dev = devices[i];
                dev.flags = flags;
                dev.is_open = true;
                dev.cfg = cfg; // store the configuration
                // isr setting
                dev.generation++;
                dev.slot = static_cast<uint32_t>(i);
                dev.callback = callback;
                dev.handler_attached = false;
                break;
            }
        }

        if (index < 0)
        {
            errno = EMFILE;
            return -1;
        }

        // Now configure everything underlining GPIO ESP-idf
        device &dev = devices[index];

        //// ESP-IDF GPIO configuration ///////////////////////
        gpio_config_t config{};
        config.pin_bit_mask = 1ULL << static_cast<unsigned>(cfg.pin);
        config.mode = cfg.mode; // GPIO MODE
        config.intr_type = GPIO_INTR_DISABLE;
        config.pull_up_en = (cfg.pull == GPIO_PULLUP_ONLY)
                                ? GPIO_PULLUP_ENABLE
                                : GPIO_PULLUP_DISABLE;
        config.pull_down_en = (cfg.pull == GPIO_PULLDOWN_ONLY)
                                  ? GPIO_PULLDOWN_ENABLE
                                  : GPIO_PULLDOWN_DISABLE;
        bool handler_added = false; // check handler
        bool gpio_configured = false;

        esp_err_t err = gpio_config(&config);
        if (err == ESP_OK)
        {
            gpio_configured = true;

            // Add the drive capability
            if (flags == O_WRONLY)
            {
                // Set drive capability of gpio : current
                err = gpio_set_drive_capability(cfg.pin, cfg.drive);
            }
        }

        if (err == ESP_OK && interrupt_enabled)
        {
            // pas dev which contain every parameter in the struct
            // store the handle for the specific gpio
            err = gpio_isr_handler_add(cfg.pin, gpio_isr_handler, &dev);
            if (err == ESP_OK)
            {
                handler_added = true;
                dev.handler_attached = true;
                err = gpio_set_intr_type(cfg.pin, cfg.intr);
            }
        }

        if (err != ESP_OK)
        {
            // check if handler added remove it
            if (handler_added)
            {
                gpio_set_intr_type(cfg.pin, GPIO_INTR_DISABLE);
                gpio_isr_handler_remove(cfg.pin);
            }
            // Reset only if GPIO configuration succeeded.
            if (gpio_configured)
            {
                gpio_reset_pin(cfg.pin);
            }
            dev.handler_attached = false;
            dev.callback = nullptr;
            dev.is_open = false;
            errno = EIO;
            return -1;
        }

        return FIRST_FD + index;
    }

    // Close the opened deveice
    int close(int fd)
    {
        // get the device handler
        device *dev = get_device(fd);

        if (dev == nullptr)
        {
            return -1;
        }
        gpio_num_t pin = dev->cfg.pin;

        if (dev->handler_attached)
        {
            // disable interrupt
            gpio_set_intr_type(pin, GPIO_INTR_DISABLE);

            // remove handler from this pin
            esp_err_t err = gpio_isr_handler_remove(pin);

            if (err != ESP_OK)
            {
                errno = EIO;
                return -1;
            }
        }

        // increment generation so that the same instance is not reused
        dev->generation++;
        dev->is_open = false;
        dev->handler_attached = false;
        dev->callback = nullptr;
        dev->flags = 0;

        esp_err_t err = gpio_reset_pin(pin);

        if (err != ESP_OK)
        {
            errno = EIO;
            return -1;
        }

        return 0;
    }
    int read(int fd, void *buffer, size_t count)
    {
        device *dev = get_device(fd);

        if (dev == nullptr)
        {
            return -1;
        }

        if (dev->flags == O_WRONLY)
        {
            errno = EBADF;
            return -1;
        }

        if (buffer == nullptr || count < 1)
        {
            errno = EINVAL;
            return -1;
        }

        int level = gpio_get_level(dev->cfg.pin);

        if (level < 0)
        {
            errno = EIO;
            return -1;
        }

        // store the level you read
        static_cast<char *>(buffer)[0] = level ? '1' : '0';

        return 1;
    }

    int write(int fd, const void *buffer, size_t count)
    {
        device *dev = get_device(fd);

        if (dev == nullptr)
        {
            return -1;
        }
        // check for th read mode
        if (dev->flags == O_RDONLY)
        {
            errno = EBADF;
            return -1;
        }

        //
        if (buffer == nullptr || count < 1)
        {
            errno = EINVAL;
            return -1;
        }

        // get the ascii
        const char level = static_cast<const char *>(buffer)[0];

        if (level != '0' && level != '1')
        {
            errno = EINVAL; // return error
            return -1;
        }

        // esp-idf specific Functions
        esp_err_t err = gpio_set_level(
            dev->cfg.pin,
            (level == '1') ? 1 : 0);

        if (err != ESP_OK)
        {
            errno = EIO;
            return -1;
        }

        // returns the number of bytes written in this case 1
        return 1;
    }

};
