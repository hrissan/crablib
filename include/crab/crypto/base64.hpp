/*
   base64.cpp and base64.h

   base64 encoding and decoding with C++.

   Version: 1.01.00

   Copyright (C) 2004-2017 René Nyffenegger

   This source code is provided 'as-is', without any express or implied
   warranty. In no event will the author be held liable for any damages
   arising from the use of this software.

   Permission is granted to anyone to use this software for any purpose,
   including commercial applications, and to alter it and redistribute it
   freely, subject to the following restrictions:

   1. The origin of this source code must not be misrepresented; you must not
      claim that you wrote the original source code. If you use this source code
      in a product, an acknowledgment in the product documentation would be
      appreciated but is not required.

   2. Altered source versions must be plainly marked as such, and must not be
      misrepresented as being the original source code.

   3. This notice may not be removed or altered from any source distribution.

   René Nyffenegger rene.nyffenegger@adp-gmbh.ch

*/

// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#pragma once

#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace crab { namespace base64 {

inline size_t encoded_size(size_t in_len) { return 4 * ((in_len + 2) / 3); }

inline size_t max_decoded_size(const char *, size_t in_len) {
	// In case of concatenated base64, actual count can be much less
	return 3 * ((in_len + 3) / 4);
}

inline void encode(char *out, size_t out_len, const uint8_t *in, size_t in_len) {
	const uint8_t *in_start = in;
	char *out_start         = out;
	const char *const tab   = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

	for (auto n = in_len / 3; n--;) {
		int inc = (in[0] << 16) + (in[1] << 8) + in[2];
		*out++  = tab[(inc >> 18)];
		*out++  = tab[(inc >> 12) & 0x3f];
		*out++  = tab[(inc >> 6) & 0x3f];
		*out++  = tab[(inc >> 0) & 0x3f];
		in += 3;
	}

	switch (in_start + in_len - in) {
	case 2: {
		int inc = (in[0] << 16) + (in[1] << 8);
		*out++  = tab[(inc >> 18)];
		*out++  = tab[(inc >> 12) & 0x3f];
		*out++  = tab[(inc >> 6) & 0x3f];
		*out++  = '=';
		break;
	}
	case 1: {
		int inc = (in[0] << 16);
		*out++  = tab[(inc >> 18)];
		*out++  = tab[(inc >> 12) & 0x3f];
		*out++  = '=';
		*out++  = '=';
		break;
	}
	default:
		break;
	}
	if (out != out_start + out_len)
		throw std::logic_error("base64 encode size mismatch");
}

static constexpr size_t bad_decode = std::numeric_limits<size_t>::max();

inline size_t decode(uint8_t *out, size_t out_len, const char *in, std::size_t in_len) {
	uint8_t *out_start = out;

	const uint8_t *uin       = reinterpret_cast<const uint8_t *>(in);
	const uint8_t *uin_start = uin;

	static const char inverse[] = {
	    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,  //   0-15
	    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,  //  16-31
	    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64, 64, 63,  //  32-47
	    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64,  //  48-63
	    64, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,            //  64-79
	    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 64,  //  80-95
	    64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,  //  96-111
	    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64,  // 112-127
	    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,  // 128-143
	    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,  // 144-159
	    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,  // 160-175
	    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,  // 176-191
	    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,  // 192-207
	    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,  // 208-223
	    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,  // 224-239
	    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64   // 240-255
	};

	for (size_t n = in_len / 4; n--; uin += 4) {
		int c0 = inverse[uin[0]];
		int c1 = inverse[uin[1]];
		int c2 = inverse[uin[2]];
		int c3 = inverse[uin[3]];
		if (uin[3] == '=') {
			if (uin[2] == '=') {
				if ((c0 | c1) & 64)
					return bad_decode;
				int v  = (c0 << 18) + (c1 << 12);
				*out++ = uint8_t(v >> 16);
				continue;
			}
			if ((c0 | c1 | c2) & 64)
				return bad_decode;
			int v  = (c0 << 18) + (c1 << 12) + (c2 << 6);
			*out++ = uint8_t(v >> 16);
			*out++ = uint8_t(v >> 8);
			continue;
		}
		if ((c0 | c1 | c2 | c3) & 64)
			return bad_decode;
		int v  = (c0 << 18) + (c1 << 12) + (c2 << 6) + c3;
		*out++ = uint8_t(v >> 16);
		*out++ = uint8_t(v >> 8);
		*out++ = uint8_t(v);
	}
	switch (uin_start + in_len - uin) {
	case 3: {
		int c0 = inverse[*uin++];
		int c1 = inverse[*uin++];
		int c2 = inverse[*uin++];
		if ((c0 | c1 | c2) & 64)
			return bad_decode;
		int v  = (c0 << 18) + (c1 << 12) + (c2 << 6);
		*out++ = uint8_t(v >> 16);
		*out++ = uint8_t(v >> 8);
		break;
	}
	case 2: {
		int c0 = inverse[*uin++];
		int c1 = inverse[*uin++];
		if ((c0 | c1) & 64)
			return bad_decode;
		int v  = (c0 << 18) + (c1 << 12);
		*out++ = uint8_t(v >> 16);
		break;
	}
	case 1:
		return bad_decode;
	}
	if (static_cast<size_t>(out - out_start) > out_len)
		throw std::logic_error("base64 decode size mismatch");
	return out - out_start;
}

inline std::string encode(const uint8_t *in, size_t in_len) {
	std::string result(encoded_size(in_len), '?');
	encode(&result[0], result.size(), in, in_len);
	return result;
}

inline bool decode(std::vector<uint8_t> *result, const std::string &data) {
	std::vector<uint8_t> tmp(max_decoded_size(data.data(), data.size()));
	auto actual_size = decode(tmp.data(), tmp.size(), data.data(), data.size());
	if (actual_size == bad_decode)
		return false;
	tmp.resize(actual_size);
	result->swap(tmp);
	return true;
}

}}  // namespace crab::base64
