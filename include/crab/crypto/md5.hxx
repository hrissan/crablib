/* Copyright (C) 1991-2, RSA Data Security, Inc. Created 1991. All
rights reserved.

License to copy and use this software is granted provided that it
is identified as the "RSA Data Security, Inc. MD5 Message-Digest
Algorithm" in all material mentioning or referencing this software
or this function.

License is also granted to make and use derivative works provided
that such works are identified as "derived from the RSA Data
Security, Inc. MD5 Message-Digest Algorithm" in all material
mentioning or referencing the derived work.

RSA Data Security, Inc. makes no representations concerning either
the merchantability of this software or the suitability of this
software for any particular purpose. It is provided "as is"
without express or implied warranty of any kind.

These notices must be retained in any copies of any part of this
documentation and/or software.
*/

// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

// Code based on https://tools.ietf.org/html/rfc1321

#include "md5.hpp"

namespace crab {

namespace details {

inline uint32_t md5_make_word(const uint8_t *p) {
	return ((uint32_t)p[3] << 3 * 8) | ((uint32_t)p[2] << 2 * 8) | ((uint32_t)p[1] << 1 * 8) | ((uint32_t)p[0] << 0 * 8);
}

inline void md5_put_word(uint8_t *p, uint32_t w) {
	p[0] = uint8_t(w);
	p[1] = uint8_t(w >> 8);
	p[2] = uint8_t(w >> 16);
	p[3] = uint8_t(w >> 24);
}

}  // namespace details

CRAB_INLINE md5 &md5::add(const uint8_t *data, size_t n) {
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

CRAB_INLINE void md5::finalize(uint8_t *result) {
	static const uint8_t PADDING[] = {0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

	size_t offset = static_cast<size_t>(size % 64);
	size_t padLen = offset < 56 ? 56 - offset : (56 + 64) - offset;

	const auto original_size = size;
	add(PADDING, padLen);
	details::md5_put_word(buffer + 56, static_cast<uint32_t>(original_size * 8));
	details::md5_put_word(buffer + 60, static_cast<uint32_t>((original_size * 8) >> 32));
	process_block(buffer);

	for (size_t i = 0; i != 4; ++i)
		details::md5_put_word(result + 4 * i, state[i]);
}

CRAB_INLINE md5::~md5() { memzero(buffer, sizeof(buffer)); }

CRAB_INLINE void md5::process_block(const uint8_t *input) {
	constexpr uint32_t S11 = 7;
	constexpr uint32_t S12 = 12;
	constexpr uint32_t S13 = 17;
	constexpr uint32_t S14 = 22;
	constexpr uint32_t S21 = 5;
	constexpr uint32_t S22 = 9;
	constexpr uint32_t S23 = 14;
	constexpr uint32_t S24 = 20;
	constexpr uint32_t S31 = 4;
	constexpr uint32_t S32 = 11;
	constexpr uint32_t S33 = 16;
	constexpr uint32_t S34 = 23;
	constexpr uint32_t S41 = 6;
	constexpr uint32_t S42 = 10;
	constexpr uint32_t S43 = 15;
	constexpr uint32_t S44 = 21;

#define F(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define G(x, y, z) (((x) & (z)) | ((y) & (~z)))
#define H(x, y, z) ((x) ^ (y) ^ (z))
#define I(x, y, z) ((y) ^ ((x) | (~z)))

#define ROTATE_LEFT(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

#define FF(a, b, c, d, x, s, ac)                        \
	{                                                   \
		(a) += F((b), (c), (d)) + (x) + (uint32_t)(ac); \
		(a) = ROTATE_LEFT((a), (s));                    \
		(a) += (b);                                     \
	}
#define GG(a, b, c, d, x, s, ac)                        \
	{                                                   \
		(a) += G((b), (c), (d)) + (x) + (uint32_t)(ac); \
		(a) = ROTATE_LEFT((a), (s));                    \
		(a) += (b);                                     \
	}
#define HH(a, b, c, d, x, s, ac)                        \
	{                                                   \
		(a) += H((b), (c), (d)) + (x) + (uint32_t)(ac); \
		(a) = ROTATE_LEFT((a), (s));                    \
		(a) += (b);                                     \
	}
#define II(a, b, c, d, x, s, ac)                        \
	{                                                   \
		(a) += I((b), (c), (d)) + (x) + (uint32_t)(ac); \
		(a) = ROTATE_LEFT((a), (s));                    \
		(a) += (b);                                     \
	}

	uint32_t a = state[0];
	uint32_t b = state[1];
	uint32_t c = state[2];
	uint32_t d = state[3];
	uint32_t x[16];
	for (size_t i = 0; i != 16; i++)
		x[i] = details::md5_make_word(input + i * 4);

	/* Round 1 */
	FF(a, b, c, d, x[0], S11, 0xd76aa478);  /* 1 */
	FF(d, a, b, c, x[1], S12, 0xe8c7b756);  /* 2 */
	FF(c, d, a, b, x[2], S13, 0x242070db);  /* 3 */
	FF(b, c, d, a, x[3], S14, 0xc1bdceee);  /* 4 */
	FF(a, b, c, d, x[4], S11, 0xf57c0faf);  /* 5 */
	FF(d, a, b, c, x[5], S12, 0x4787c62a);  /* 6 */
	FF(c, d, a, b, x[6], S13, 0xa8304613);  /* 7 */
	FF(b, c, d, a, x[7], S14, 0xfd469501);  /* 8 */
	FF(a, b, c, d, x[8], S11, 0x698098d8);  /* 9 */
	FF(d, a, b, c, x[9], S12, 0x8b44f7af);  /* 10 */
	FF(c, d, a, b, x[10], S13, 0xffff5bb1); /* 11 */
	FF(b, c, d, a, x[11], S14, 0x895cd7be); /* 12 */
	FF(a, b, c, d, x[12], S11, 0x6b901122); /* 13 */
	FF(d, a, b, c, x[13], S12, 0xfd987193); /* 14 */
	FF(c, d, a, b, x[14], S13, 0xa679438e); /* 15 */
	FF(b, c, d, a, x[15], S14, 0x49b40821); /* 16 */

	/* Round 2 */
	GG(a, b, c, d, x[1], S21, 0xf61e2562);  /* 17 */
	GG(d, a, b, c, x[6], S22, 0xc040b340);  /* 18 */
	GG(c, d, a, b, x[11], S23, 0x265e5a51); /* 19 */
	GG(b, c, d, a, x[0], S24, 0xe9b6c7aa);  /* 20 */
	GG(a, b, c, d, x[5], S21, 0xd62f105d);  /* 21 */
	GG(d, a, b, c, x[10], S22, 0x2441453);  /* 22 */
	GG(c, d, a, b, x[15], S23, 0xd8a1e681); /* 23 */
	GG(b, c, d, a, x[4], S24, 0xe7d3fbc8);  /* 24 */
	GG(a, b, c, d, x[9], S21, 0x21e1cde6);  /* 25 */
	GG(d, a, b, c, x[14], S22, 0xc33707d6); /* 26 */
	GG(c, d, a, b, x[3], S23, 0xf4d50d87);  /* 27 */
	GG(b, c, d, a, x[8], S24, 0x455a14ed);  /* 28 */
	GG(a, b, c, d, x[13], S21, 0xa9e3e905); /* 29 */
	GG(d, a, b, c, x[2], S22, 0xfcefa3f8);  /* 30 */
	GG(c, d, a, b, x[7], S23, 0x676f02d9);  /* 31 */
	GG(b, c, d, a, x[12], S24, 0x8d2a4c8a); /* 32 */

	/* Round 3 */
	HH(a, b, c, d, x[5], S31, 0xfffa3942);  /* 33 */
	HH(d, a, b, c, x[8], S32, 0x8771f681);  /* 34 */
	HH(c, d, a, b, x[11], S33, 0x6d9d6122); /* 35 */
	HH(b, c, d, a, x[14], S34, 0xfde5380c); /* 36 */
	HH(a, b, c, d, x[1], S31, 0xa4beea44);  /* 37 */
	HH(d, a, b, c, x[4], S32, 0x4bdecfa9);  /* 38 */
	HH(c, d, a, b, x[7], S33, 0xf6bb4b60);  /* 39 */
	HH(b, c, d, a, x[10], S34, 0xbebfbc70); /* 40 */
	HH(a, b, c, d, x[13], S31, 0x289b7ec6); /* 41 */
	HH(d, a, b, c, x[0], S32, 0xeaa127fa);  /* 42 */
	HH(c, d, a, b, x[3], S33, 0xd4ef3085);  /* 43 */
	HH(b, c, d, a, x[6], S34, 0x4881d05);   /* 44 */
	HH(a, b, c, d, x[9], S31, 0xd9d4d039);  /* 45 */
	HH(d, a, b, c, x[12], S32, 0xe6db99e5); /* 46 */
	HH(c, d, a, b, x[15], S33, 0x1fa27cf8); /* 47 */
	HH(b, c, d, a, x[2], S34, 0xc4ac5665);  /* 48 */

	/* Round 4 */
	II(a, b, c, d, x[0], S41, 0xf4292244);  /* 49 */
	II(d, a, b, c, x[7], S42, 0x432aff97);  /* 50 */
	II(c, d, a, b, x[14], S43, 0xab9423a7); /* 51 */
	II(b, c, d, a, x[5], S44, 0xfc93a039);  /* 52 */
	II(a, b, c, d, x[12], S41, 0x655b59c3); /* 53 */
	II(d, a, b, c, x[3], S42, 0x8f0ccc92);  /* 54 */
	II(c, d, a, b, x[10], S43, 0xffeff47d); /* 55 */
	II(b, c, d, a, x[1], S44, 0x85845dd1);  /* 56 */
	II(a, b, c, d, x[8], S41, 0x6fa87e4f);  /* 57 */
	II(d, a, b, c, x[15], S42, 0xfe2ce6e0); /* 58 */
	II(c, d, a, b, x[6], S43, 0xa3014314);  /* 59 */
	II(b, c, d, a, x[13], S44, 0x4e0811a1); /* 60 */
	II(a, b, c, d, x[4], S41, 0xf7537e82);  /* 61 */
	II(d, a, b, c, x[11], S42, 0xbd3af235); /* 62 */
	II(c, d, a, b, x[2], S43, 0x2ad7d2bb);  /* 63 */
	II(b, c, d, a, x[9], S44, 0xeb86d391);  /* 64 */

	state[0] += a;
	state[1] += b;
	state[2] += c;
	state[3] += d;

	memzero(x, sizeof(x));

#undef F
#undef G
#undef H
#undef I

#undef ROTATE_LEFT

#undef FF
#undef GG
#undef HH
#undef II
}

}  // namespace crab
