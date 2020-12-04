// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#pragma once

#include <stdint.h>

namespace crab {

class md5 {
public:
	md5 &add(const uint8_t *data, size_t n);
	md5 &add(const char *ptr, size_t n) { return add(reinterpret_cast<const uint8_t *>(ptr), n); }
	void finalize(uint8_t *result);
	~md5();

	static constexpr size_t hash_size = 16;

private:
	uint64_t size = 0;
	uint32_t state[4]{0x67452301U, 0xefcdab89U, 0x98badcfeU, 0x10325476U};  // A, B, C, D
	uint8_t buffer[64];                                                     // Uninitialized

	void process_block(const uint8_t *input);
};

}  // namespace crab