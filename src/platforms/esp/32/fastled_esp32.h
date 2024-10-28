#pragma once

#include "fastpin_esp32.h"

#ifdef FASTLED_ALL_PINS_HARDWARE_SPI
#include "fastspi_esp32.h"
#endif

#ifdef FASTLED_ESP32_I2S
#include "clockless_i2s_esp32.h"
#ifdef FASTLED_EXPERIMENTAL_S3
#include "s3_clockless_and_clocked_driver.hpp"
#else
#include "clockless_rmt_esp32.h"
#endif
#endif
