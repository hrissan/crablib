// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <string.h>
#include <algorithm>
#include <iostream>
#include <limits>
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
CRAB_INLINE std::string invariant_violated(const char *expr, const char *file, int line, const std::string &msg) {
	std::stringstream str;
	str << "Invariant " << std::string(expr) << " violated at " << file << " " << line << " " << msg;
	return str.str();
}
}  // namespace details

CRAB_INLINE Random::Random() {
	std::random_device rd;
	std::seed_seq seq{rd(), rd(), rd(), rd(), rd(), rd(), rd(), rd()};
	// 256+ bit of entropy if rd() returns 32+ bit, which is common
	// See also https://stackoverflow.com/questions/35935895/how-can-i-know-the-correct-size-of-a-stdseed-seq
	// Generally, C++ random is crap, should be replaced with good impl in future.
	// why engine cannot be seeded by passing std::random_device directly is beyond my comprehension
	mt.seed(seq);
}

CRAB_INLINE void Random::bytes(uint8_t *buffer, size_t size) {
	size_t i = 0;
	for (; i + sizeof(VT) <= size; i += sizeof(VT)) {
		VT value = static_cast<VT>(mt());
		std::memcpy(buffer + i, &value, sizeof(VT));
	}
	if (i == size)
		return;
	auto value = mt();
	for (; i < size; i += 1) {
		buffer[i] = value;
		value >>= 8U;
	}
}

CRAB_INLINE bdata Random::data(size_t size) {
	bdata result(size);
	bytes(result.data(), result.size());
	return result;
}

namespace details {
template<typename T>
static constexpr T power(T value, size_t pow, T collect = T{1}) {
	return pow <= 0 ? collect : power(value, pow - 1, collect * value);
}
}  // namespace details

CRAB_INLINE std::string Random::printable_string(size_t size) {
	static const char alphabet[]   = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
	constexpr size_t ALPHABET_SIZE = sizeof(alphabet) - 1;
	constexpr size_t BITS          = 6;

	static_assert((1U << BITS) >= ALPHABET_SIZE, "Too little bits");
	constexpr size_t CHARS_PER_MT = sizeof(VT) * 8 / BITS;
	constexpr VT TAIL             = details::power(ALPHABET_SIZE, CHARS_PER_MT);
	constexpr VT LONG_TAIL        = (std::numeric_limits<VT>::max() - TAIL + 1) / TAIL * TAIL + TAIL - 1;
	// We want ideal distribution, without calling mt() too often and without dividing too much
	// std::uniform_int_distribution<VT> distr(0, TAIL); is 2x slower
	// std::uniform_int_distribution<VT> distr(0, ALPHABET_SIZE - 1); per symbol is 10x slower

	std::string result(size, '\0');
	size_t i = 0;
	for (; i != size;) {
		VT value = mt();
		while (value > LONG_TAIL)
			value = mt();
		for (size_t j = 0; j != CHARS_PER_MT; ++j) {
			result[i++] = alphabet[value % ALPHABET_SIZE];
			value /= ALPHABET_SIZE;
			if (i == size)
				return result;
		}
	}
	return result;
};

}  // namespace crab
