// Licensed under the Unlicense. See https://github.com/983/SHA1/blob/master/LICENSE for details.

// Copyright (c) 2007-2023, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <string.h>
#include "sha1.hpp"

namespace crab {

namespace details {

inline uint32_t sha1_rol32(uint32_t x, uint32_t n) { return (x << n) | (x >> (32 - n)); }

inline uint32_t sha1_make_word(const uint8_t *p) {
	return ((uint32_t)p[0] << 3 * 8) | ((uint32_t)p[1] << 2 * 8) | ((uint32_t)p[2] << 1 * 8) | ((uint32_t)p[3] << 0 * 8);
}

inline void sha1_put_word(uint8_t *p, uint32_t w) {
	p[3] = uint8_t(w);
	p[2] = uint8_t(w >> 8);
	p[1] = uint8_t(w >> 16);
	p[0] = uint8_t(w >> 24);
}

}  // namespace details

CRAB_INLINE sha1 &sha1::add(const uint8_t *data, size_t n) {
	size_t index = static_cast<size_t>(size % 64);
	size += n;
	auto partLen = 64 - index;

	if (n < partLen) {
		memcpy(buffer + index, data, n);
		return *this;
	}
	memcpy(buffer + index, data, partLen);
	process_block(buffer);
	index += partLen;
	data += partLen;
	n -= partLen;
	while (n >= 64) {
		process_block(data);
		data += 64;
		n -= 64;
	}
	std::memcpy(buffer, data, n);
	return *this;
}

CRAB_INLINE void sha1::finalize(uint8_t *result) {
	// hashed text ends with 0x80, some padding 0x00 and the length in bits
	static const uint8_t PADDING[] = {0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

	size_t offset = static_cast<size_t>(size % 64);
	size_t padLen = offset < 56 ? 56 - offset : (56 + 64) - offset;

	const auto original_size = size;
	add(PADDING, padLen);
	details::sha1_put_word(buffer + 56, static_cast<uint32_t>((original_size * 8) >> 32));
	details::sha1_put_word(buffer + 60, static_cast<uint32_t>(original_size * 8));
	process_block(buffer);

	for (size_t i = 0; i != 5; ++i)
		details::sha1_put_word(result + 4 * i, state[i]);
}

CRAB_INLINE sha1::~sha1() { memzero(buffer, sizeof(buffer)); }

CRAB_INLINE void sha1::process_block(const uint8_t *ptr) {
	constexpr uint32_t c0 = 0x5a827999;
	constexpr uint32_t c1 = 0x6ed9eba1;
	constexpr uint32_t c2 = 0x8f1bbcdc;
	constexpr uint32_t c3 = 0xca62c1d6;

#define SHA1_LOAD(i) w[i & 15] = details::sha1_rol32(w[(i + 13) & 15] ^ w[(i + 8) & 15] ^ w[(i + 2) & 15] ^ w[i & 15], 1);
#define SHA1_ROUND_0(v, u, x, y, z, i)                                     \
	z += ((u & (x ^ y)) ^ y) + w[i & 15] + c0 + details::sha1_rol32(v, 5); \
	u = details::sha1_rol32(u, 30);
#define SHA1_ROUND_1(v, u, x, y, z, i)                                                  \
	SHA1_LOAD(i) z += ((u & (x ^ y)) ^ y) + w[i & 15] + c0 + details::sha1_rol32(v, 5); \
	u = details::sha1_rol32(u, 30);
#define SHA1_ROUND_2(v, u, x, y, z, i)                                          \
	SHA1_LOAD(i) z += (u ^ x ^ y) + w[i & 15] + c1 + details::sha1_rol32(v, 5); \
	u = details::sha1_rol32(u, 30);
#define SHA1_ROUND_3(v, u, x, y, z, i)                                                        \
	SHA1_LOAD(i) z += (((u | x) & y) | (u & x)) + w[i & 15] + c2 + details::sha1_rol32(v, 5); \
	u = details::sha1_rol32(u, 30);
#define SHA1_ROUND_4(v, u, x, y, z, i)                                          \
	SHA1_LOAD(i) z += (u ^ x ^ y) + w[i & 15] + c3 + details::sha1_rol32(v, 5); \
	u = details::sha1_rol32(u, 30);

	uint32_t a = state[0];
	uint32_t b = state[1];
	uint32_t c = state[2];
	uint32_t d = state[3];
	uint32_t e = state[4];

	uint32_t w[16];

	for (int i = 0; i < 16; i++)
		w[i] = details::sha1_make_word(ptr + i * 4);

	SHA1_ROUND_0(a, b, c, d, e, 0);
	SHA1_ROUND_0(e, a, b, c, d, 1);
	SHA1_ROUND_0(d, e, a, b, c, 2);
	SHA1_ROUND_0(c, d, e, a, b, 3);
	SHA1_ROUND_0(b, c, d, e, a, 4);
	SHA1_ROUND_0(a, b, c, d, e, 5);
	SHA1_ROUND_0(e, a, b, c, d, 6);
	SHA1_ROUND_0(d, e, a, b, c, 7);
	SHA1_ROUND_0(c, d, e, a, b, 8);
	SHA1_ROUND_0(b, c, d, e, a, 9);
	SHA1_ROUND_0(a, b, c, d, e, 10);
	SHA1_ROUND_0(e, a, b, c, d, 11);
	SHA1_ROUND_0(d, e, a, b, c, 12);
	SHA1_ROUND_0(c, d, e, a, b, 13);
	SHA1_ROUND_0(b, c, d, e, a, 14);
	SHA1_ROUND_0(a, b, c, d, e, 15);
	SHA1_ROUND_1(e, a, b, c, d, 16);
	SHA1_ROUND_1(d, e, a, b, c, 17);
	SHA1_ROUND_1(c, d, e, a, b, 18);
	SHA1_ROUND_1(b, c, d, e, a, 19);
	SHA1_ROUND_2(a, b, c, d, e, 20);
	SHA1_ROUND_2(e, a, b, c, d, 21);
	SHA1_ROUND_2(d, e, a, b, c, 22);
	SHA1_ROUND_2(c, d, e, a, b, 23);
	SHA1_ROUND_2(b, c, d, e, a, 24);
	SHA1_ROUND_2(a, b, c, d, e, 25);
	SHA1_ROUND_2(e, a, b, c, d, 26);
	SHA1_ROUND_2(d, e, a, b, c, 27);
	SHA1_ROUND_2(c, d, e, a, b, 28);
	SHA1_ROUND_2(b, c, d, e, a, 29);
	SHA1_ROUND_2(a, b, c, d, e, 30);
	SHA1_ROUND_2(e, a, b, c, d, 31);
	SHA1_ROUND_2(d, e, a, b, c, 32);
	SHA1_ROUND_2(c, d, e, a, b, 33);
	SHA1_ROUND_2(b, c, d, e, a, 34);
	SHA1_ROUND_2(a, b, c, d, e, 35);
	SHA1_ROUND_2(e, a, b, c, d, 36);
	SHA1_ROUND_2(d, e, a, b, c, 37);
	SHA1_ROUND_2(c, d, e, a, b, 38);
	SHA1_ROUND_2(b, c, d, e, a, 39);
	SHA1_ROUND_3(a, b, c, d, e, 40);
	SHA1_ROUND_3(e, a, b, c, d, 41);
	SHA1_ROUND_3(d, e, a, b, c, 42);
	SHA1_ROUND_3(c, d, e, a, b, 43);
	SHA1_ROUND_3(b, c, d, e, a, 44);
	SHA1_ROUND_3(a, b, c, d, e, 45);
	SHA1_ROUND_3(e, a, b, c, d, 46);
	SHA1_ROUND_3(d, e, a, b, c, 47);
	SHA1_ROUND_3(c, d, e, a, b, 48);
	SHA1_ROUND_3(b, c, d, e, a, 49);
	SHA1_ROUND_3(a, b, c, d, e, 50);
	SHA1_ROUND_3(e, a, b, c, d, 51);
	SHA1_ROUND_3(d, e, a, b, c, 52);
	SHA1_ROUND_3(c, d, e, a, b, 53);
	SHA1_ROUND_3(b, c, d, e, a, 54);
	SHA1_ROUND_3(a, b, c, d, e, 55);
	SHA1_ROUND_3(e, a, b, c, d, 56);
	SHA1_ROUND_3(d, e, a, b, c, 57);
	SHA1_ROUND_3(c, d, e, a, b, 58);
	SHA1_ROUND_3(b, c, d, e, a, 59);
	SHA1_ROUND_4(a, b, c, d, e, 60);
	SHA1_ROUND_4(e, a, b, c, d, 61);
	SHA1_ROUND_4(d, e, a, b, c, 62);
	SHA1_ROUND_4(c, d, e, a, b, 63);
	SHA1_ROUND_4(b, c, d, e, a, 64);
	SHA1_ROUND_4(a, b, c, d, e, 65);
	SHA1_ROUND_4(e, a, b, c, d, 66);
	SHA1_ROUND_4(d, e, a, b, c, 67);
	SHA1_ROUND_4(c, d, e, a, b, 68);
	SHA1_ROUND_4(b, c, d, e, a, 69);
	SHA1_ROUND_4(a, b, c, d, e, 70);
	SHA1_ROUND_4(e, a, b, c, d, 71);
	SHA1_ROUND_4(d, e, a, b, c, 72);
	SHA1_ROUND_4(c, d, e, a, b, 73);
	SHA1_ROUND_4(b, c, d, e, a, 74);
	SHA1_ROUND_4(a, b, c, d, e, 75);
	SHA1_ROUND_4(e, a, b, c, d, 76);
	SHA1_ROUND_4(d, e, a, b, c, 77);
	SHA1_ROUND_4(c, d, e, a, b, 78);
	SHA1_ROUND_4(b, c, d, e, a, 79);

	state[0] += a;
	state[1] += b;
	state[2] += c;
	state[3] += d;
	state[4] += e;

	memzero(w, sizeof(w));

#undef SHA1_LOAD
#undef SHA1_ROUND_0
#undef SHA1_ROUND_1
#undef SHA1_ROUND_2
#undef SHA1_ROUND_3
#undef SHA1_ROUND_4
}

}  // namespace crab
