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

CRAB_INLINE int from_digit(char sym) {
	auto ud = static_cast<unsigned>(sym - '0');
	return ud > 9 ? -1 : static_cast<int>(ud);
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

}  // namespace crab
