// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#pragma once

#include <cctype>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace crab {

// uint8_t is our character of choice for all binary reading and writing
typedef std::vector<uint8_t> bdata;
inline void append(bdata &result, const bdata &other) {  // We do this op too often
	result.insert(result.end(), other.begin(), other.end());
}

std::string to_hex(const uint8_t *data, size_t count);
inline std::string to_hex(const bdata &data) { return to_hex(data.data(), data.size()); }

int from_hex_digit(char sym);
bool from_hex(bdata &data, const std::string &str);

namespace details {
// Header-only libs cannot contain statics, we use workaround
template<typename T>
struct StaticHolder {
	static T instance;
};

template<typename T>
T StaticHolder<T>::instance{};

template<typename T>
struct StaticHolderTL {
	static thread_local T instance;
};

template<typename T>
thread_local T StaticHolderTL<T>::instance{};

std::string invariant_violated(const char *expr, const char *file, int line, const std::string &msg);

}  // namespace details

class Nocopy {
public:
	Nocopy()               = default;
	Nocopy(const Nocopy &) = delete;
	Nocopy &operator=(const Nocopy &) = delete;
};

#define invariant(expr, msg)                                                                           \
	do {                                                                                               \
		if (!(expr))                                                                                   \
			throw std::logic_error(crab::details::invariant_violated(#expr, __FILE__, __LINE__, msg)); \
	} while (0)

struct Literal {
	const char *value;
	size_t size;

	static constexpr size_t length(const char *str) { return *str ? 1U + length(str + 1) : 0U; }
	int compare(const char *b, size_t bs) const {
		if (size != bs)
			return static_cast<int>(size) - static_cast<int>(bs);
		return std::memcmp(value, b, size);
	}
	int compare(const std::string &b) const { return compare(b.data(), b.size()); }
};

inline bool operator==(const std::string &a, const Literal &b) { return b.compare(a) == 0; }
inline bool operator!=(const std::string &a, const Literal &b) { return !(a == b); }
inline bool operator==(const Literal &a, const std::string &b) { return a.compare(b) == 0; }
inline bool operator!=(const Literal &a, const std::string &b) { return !(a == b); }

#define CRAB_LITERAL(name, value) static const Literal name{value, Literal::length(value)};

class Random {
public:
	Random();

	void bytes(uint8_t *buffer, size_t size);
	void bytes(char *buffer, size_t size) { bytes(reinterpret_cast<uint8_t *>(buffer), size); }

	std::string printable_string(size_t size);

	template<typename T>
	T pod() {
		static_assert(std::is_standard_layout<T>::value, "T must be Standard Layout");
		T result{};
		bytes(reinterpret_cast<uint8_t *>(&result), sizeof(result));
		return result;
	}

private:
	using MT = std::mt19937;
	using VT = uint32_t;  // Too hard to get right type from MT so that memcpy is optimized
	MT mt;
};

}  // namespace crab
