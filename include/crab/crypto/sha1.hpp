// Licensed under the Unlicense. See https://github.com/983/SHA1/blob/master/LICENSE for details.

// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#pragma once

#include <stdint.h>

namespace crab {

class sha1 {
public:
	sha1 &add(uint8_t x);
	sha1 &add(const uint8_t *ptr, size_t n);
	sha1 &add(const char *ptr, size_t n) { return add(reinterpret_cast<const uint8_t *>(ptr), n); }
	void finalize(uint8_t *result);
	~sha1();

	static constexpr size_t hash_size = 20;

private:
	uint64_t n_bits = 0;
	uint32_t state[5]{0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
	uint8_t buf[64];  // Uninitialized
	uint32_t i = 0;

	void process_block(const uint8_t *ptr);
	void add_byte_dont_count_bits(uint8_t x);
};

}  // namespace crab
