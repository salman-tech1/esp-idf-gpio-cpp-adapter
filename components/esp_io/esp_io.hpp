
// include the header once
#pragma once

extern "C"
{
#include "driver/gpio.h"
}

#include <fcntl.h>
#include <cstddef> // standard luibrary headers
#include "driver/gpio.h"

namespace esp_io
{
    // function pointer file handler
    using gpio_callback_t = void (*)(int fd, gpio_num_t pin, int level);

    struct io_config
    {
        gpio_num_t pin;
        gpio_pull_mode_t pull;
        gpio_drive_cap_t drive;
        gpio_int_type_t intr;
        gpio_mode_t mode;
    };

    // Maximum number of allowed io's
    constexpr int MAX_DEVICES = 8;

    // POSIX-style access modes use the standard O_RDONLY,
    // O_WRONLY and O_RDWR flags from <fcntl.h>.

    // Returns a non-negative handle on success, -1 on error.
    // Check for NULL as well
    // return fd
    int open(io_config cfg, int flags, gpio_callback_t callback);

    // Process one ASCII character: '0' or '1'.
    // Returns 1 on success, -1 on error.
    int read(int fd, void *buffer, size_t count);
    int write(int fd, const void *buffer, size_t count);

    // Returns 0 on success, -1 on error.
    int close(int fd);
}