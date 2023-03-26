// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#pragma once

#include <cctype>

namespace crab {

uint32_t crc32(uint32_t crc, const uint8_t *data, size_t size);
uint32_t crc32c(uint32_t crc, const uint8_t *data, size_t size);

}  // namespace crab
