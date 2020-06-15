// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

// *Really* minimal PCG32 code / (c) 2014 M.E. O'Neill / pcg-random.org
// Licensed under Apache License 2.0 (NO WARRANTY, etc. see website)

// Copyright (c) 2008-2010 Bjoern Hoehrmann <bjoern@hoehrmann.de>
// See http://bjoern.hoehrmann.de/utf-8/decoder/dfa/ for details.
// Included with tiny modifcations

#include <string.h>
#include <algorithm>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include "util.hpp"

namespace crab {

CRAB_INLINE std::string to_hex(const uint8_t *data, size_t count) {
	static const char hexdigits[] = "0123456789abcdef";

	std::string result(count * 2, char());
	for (size_t i = 0; i != count; ++i) {
		uint8_t ch        = data[i];
		result[i * 2]     = hexdigits[(ch >> 4) & 0xf];
		result[i * 2 + 1] = hexdigits[ch & 0xf];
	}
	return result;
}

CRAB_INLINE int from_hex_digit(char sym) {
	if (sym >= '0' && sym <= '9')
		return sym - '0';
	if (sym >= 'a' && sym <= 'f')
		return sym - 'a' + 10;
	if (sym >= 'A' && sym <= 'F')
		return sym - 'A' + 10;
	return -1;
}

CRAB_INLINE bool from_hex(bdata &data, const std::string &str) {
	if (str.size() % 2 != 0)
		return false;
	bdata result;
	result.reserve(str.size() / 2);
	for (size_t i = 0; i != str.size(); i += 2) {
		auto d0 = from_hex_digit(str[i]);
		auto d1 = from_hex_digit(str[i + 1]);
		if (d0 < 0 || d1 < 0)
			return false;
		result.push_back(static_cast<uint8_t>(d0 * 16 + d1));
	}
	data = std::move(result);
	return true;
}

namespace details {

enum { UTF8_ACCEPT = 0, UTF8_REJECT = 12 };

uint32_t inline utf8_decode(uint32_t *state, uint32_t *codep, uint32_t byte) {
	// clang-format off
    static const uint8_t utf8d[] = {
            // The first part of the table maps bytes to character classes that
            // to reduce the size of the transition table and create bitmasks.
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
            1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,  9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
            7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
            8,8,2,2,2,2,2,2,2,2,2,2,2,2,2,2,  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
            10,3,3,3,3,3,3,3,3,3,3,3,3,4,3,3, 11,6,6,6,5,8,8,8,8,8,8,8,8,8,8,8,

            // The second part is a transition table that maps a combination
            // of a state of the automaton and a character class to a state.
            0,12,24,36,60,96,84,12,12,12,48,72, 12,12,12,12,12,12,12,12,12,12,12,12,
            12, 0,12,12,12,12,12, 0,12, 0,12,12, 12,24,12,12,12,12,12,24,12,24,12,12,
            12,12,12,12,12,12,12,24,12,12,12,12, 12,24,12,12,12,12,12,12,12,24,12,12,
            12,12,12,12,12,12,12,36,12,36,12,12, 12,36,12,12,12,12,12,36,12,36,12,12,
            12,36,12,12,12,12,12,12,12,12,12,12,
    };

    uint32_t type = utf8d[byte];

    *codep = (*state != UTF8_ACCEPT) ?
             (byte & 0x3fu) | (*codep << 6) :
             (0xff >> type) & (byte);

    *state = utf8d[256 + *state + type];
    return *state;
	// clang-format on
}

}  // namespace details

bool is_valid_utf8(const uint8_t *data, size_t count) {
	uint32_t codepoint, state = details::UTF8_ACCEPT;

	for (size_t pos = 0; pos != count; ++pos)
		details::utf8_decode(&state, &codepoint, data[pos]);

	return state == details::UTF8_ACCEPT;
}

namespace details {
CRAB_INLINE std::string invariant_violated(const char *expr, const char *file, int line, const std::string &msg) {
	std::stringstream str;
	str << "Invariant " << std::string(expr) << " violated at " << file << " " << line << " " << msg;
	return str.str();
}
}  // namespace details

CRAB_INLINE Random::Random() {
	std::random_device rd;
	std::uniform_int_distribution<uint32_t> dist;  // random_device has unspecified min and max

	inc   = (uint64_t(dist(rd)) << 32U) ^ dist(rd);
	state = (uint64_t(dist(rd)) << 32U) ^ dist(rd);
}

CRAB_INLINE Random::Random(uint32_t seed) { state = uint64_t(seed) << 32U; }

CRAB_INLINE uint32_t Random::pcg32_random_r() {
	uint64_t oldstate = state;
	// Advance internal state
	state = uint64_t(oldstate * 6364136223846793005ULL + (inc | 1));
	// Calculate output function (XSH RR), uses old state for max ILP
	uint32_t xorshifted = uint32_t(((oldstate >> 18u) ^ oldstate) >> 27u);
	uint32_t rot        = uint32_t(oldstate >> 59u);
	return (xorshifted >> rot) | (xorshifted << ((32 - rot) & 31));
}

CRAB_INLINE void Random::bytes(uint8_t *buffer, size_t size) {
	size_t i = 0;
	for (; i + sizeof(uint32_t) <= size; i += sizeof(uint32_t)) {
		uint32_t value = pcg32_random_r();
		std::memcpy(buffer + i, &value, sizeof(uint32_t));
	}
	if (i == size)
		return;
	auto value = pcg32_random_r();
	for (; i < size; i += 1) {
		buffer[i] = static_cast<uint8_t>(value);
		value >>= 8U;
	}
}

CRAB_INLINE bdata Random::data(size_t size) {
	bdata result(size);
	bytes(result.data(), result.size());
	return result;
}

CRAB_INLINE std::string Random::printable_string(size_t size) {
	static const char alphabet[]   = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
	constexpr size_t ALPHABET_SIZE = sizeof(alphabet) - 1;
	constexpr uint32_t LONG_TAIL   = uint32_t(0x100000000ULL - 0x100000000ULL % ALPHABET_SIZE - 1);

	std::string result(size, '\0');
	for (size_t i = 0; i != size; ++i) {
		uint32_t value = 0;
		do {
			value = pcg32_random_r();
		} while (value > LONG_TAIL);  // Repeats very rarely, but results in perfect distribution
		result[i] = alphabet[value % ALPHABET_SIZE];
	}
	return result;
};

CRAB_INLINE double Random::double_value() {
	auto hipart = uint64_t(pcg32_random_r()) << 31U;  // leave top bit zero for faster ldexp() on some platforms
	return ldexp(hipart ^ pcg32_random_r(), -63);
}

}  // namespace crab
