// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#pragma once

#include <cctype>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "util.hpp"

namespace crab {

namespace details {

template<typename T>
void throw_out_of_range(const std::string &arg, const char *msg) {
	throw std::out_of_range(msg + arg + " to range [" + std::to_string(std::numeric_limits<T>::min()) + ".." +
	                        std::to_string(std::numeric_limits<T>::max()) + "]");
}
template<typename T, typename S>
void throw_out_of_range(const S &arg) {
	throw_out_of_range<T>(std::to_string(arg), "Out of range during integer_cast of ");
}

template<typename T, typename S>
inline T integer_cast_impl(S arg, std::true_type, std::true_type) {
	// both unsigned
	if (arg > std::numeric_limits<T>::max())
		throw_out_of_range<T>(arg);
	return static_cast<T>(arg);
}

template<typename T, typename S>
inline T integer_cast_impl(const S &arg, std::false_type, std::false_type) {
	// both signed
	if (arg > std::numeric_limits<T>::max())
		throw_out_of_range<T>(arg);
	if (arg < std::numeric_limits<T>::min())
		throw_out_of_range<T>(arg);
	return static_cast<T>(arg);
}

template<typename T, typename S>
inline T integer_cast_impl(const S &arg, std::true_type, std::false_type) {
	// signed to unsigned
	using US = typename std::make_unsigned<S>::type;
	if (arg < 0)
		throw_out_of_range<T>(arg);
	if (static_cast<US>(arg) > std::numeric_limits<T>::max())
		throw_out_of_range<T>(arg);
	return static_cast<T>(arg);
}

template<typename T, typename S>
inline T integer_cast_impl(const S &arg, std::false_type, std::true_type) {
	// unsigned to signed
	using UT = typename std::make_unsigned<T>::type;
	if (arg > static_cast<UT>(std::numeric_limits<T>::max()))
		throw_out_of_range<T>(arg);
	return static_cast<T>(arg);
}

template<typename T, typename S>  // source integral
inline T integer_cast_is_integral(const S &arg, std::true_type) {
	return integer_cast_impl<T, S>(arg, std::is_unsigned<T>{}, std::is_unsigned<S>{});
}

inline bool is_integer_space(char c) { return c == ' ' || c == '\t'; }

// C++ committee cannot do anything of quality...
// std::stoull will parse -5 to binary representation without exception, uh-oh
// there is not way to convert to (u)short, (u)char with overflow checks
// We allow may leading 0, so 000000 is valid

template<typename T>
std::pair<T, const char *> integer_parse_impl(const char *begin, const char *end) {
	while (begin != end && is_integer_space(*begin))
		begin += 1;
	T value = 0;
	if (begin == end)
		return {value, "Number must start from sign or digit "};
	if (*begin == '-') {
		if (std::is_unsigned<T>::value)
			return {value, "Unsigned Number cannot be negative "};
		begin += 1;
		if (begin == end)
			return {value, "Number must start from sign or digit "};
		int digit = from_digit(*begin);
		if (digit < 0)
			return {value, "Number must start from sign or digit "};
		value = -static_cast<T>(digit);
		begin += 1;
		constexpr size_t max_safe_digits = sizeof(T) * 2;  // Approximate
		auto safe_end                    = std::min(end, begin + max_safe_digits);
		// Convert as much as possible without overflow checks
		while (begin != safe_end) {
			int digit = from_digit(*begin);
			if (digit < 0)
				break;
			value = value * 10 - digit;
			begin += 1;
		}
		// Convert the rest with strict overflow checks
		while (begin != end) {
			int digit      = from_digit(*begin);
			constexpr T mi = std::numeric_limits<T>::min();
			// remainder of negative num is implementation defined
			constexpr int cutlim = static_cast<int>(mi / 10 * 10 - mi);
			if (digit < 0)
				break;
			if (value < mi / 10 || (value == mi / 10 && digit > cutlim))
				return {value, "Number underflow "};
			value = value * 10 - digit;
			begin += 1;
		}
	} else {
		if (*begin == '+')
			begin += 1;
		if (begin == end)
			return {value, "Number must start from sign or digit "};
		int digit = from_digit(*begin);
		if (digit < 0)
			return {value, "Number must start from sign or digit "};
		value = static_cast<T>(digit);
		begin += 1;
		constexpr size_t max_safe_digits = sizeof(T) * 2;  // Approximate
		auto safe_end                    = std::min(end, begin + max_safe_digits);
		// Convert as much as possible without overflow checks
		while (begin != safe_end) {
			int digit = from_digit(*begin);
			if (digit < 0)
				break;
			value = value * 10 + digit;
			begin += 1;
		}
		// Convert the rest with strict overflow checks
		while (begin != end) {
			int digit            = from_digit(*begin);
			const T ma           = std::numeric_limits<T>::max();
			constexpr int cutlim = static_cast<int>(ma % 10);
			if (digit < 0)
				break;
			if (value > ma / 10 || (value == ma / 10 && digit > cutlim))
				return {value, "Number overflow "};
			value = value * 10 + digit;
			begin += 1;
		}
	}
	while (begin != end && is_integer_space(*begin))
		begin += 1;
	if (begin != end)
		return {value, "Number must contain only whitespaces after digits "};
	return {value, nullptr};
}

template<typename T>
T integer_parse(const char *begin, const char *end) {
	auto res = integer_parse_impl<T>(begin, end);
	if (res.second)
		throw_out_of_range<T>(std::string(begin, end), res.second);
	return res.first;
}

template<typename T>
struct IntegerParser {
public:
	void parse(const char *begin, const char *end) {
		while (begin != end)
			state = consume(*begin++);
		if (state < DIGITS)
			throw std::out_of_range("Number must start from sign or digit");
	}
	T get_value() const { return value; }

private:
	enum State {
		LEADING_WS,
		FIRST_DIGIT,
		FIRST_NEGATIVE_DIGIT,
		DIGITS,
		NEGATIVE_DIGITS,
		TRAILING_WS
	} state = LEADING_WS;

	T value = 0;
	State consume(char input) {
		switch (state) {
		case LEADING_WS: {
			if (std::isspace(input))
				return LEADING_WS;
			if (input == '-' && !std::is_unsigned<T>::value) {
				return FIRST_NEGATIVE_DIGIT;
			}
			if (input == '+')
				return FIRST_DIGIT;
			int digit = from_digit(input);
			if (digit < 0)
				throw std::out_of_range("Number must start from sign or digit");
			value = digit;
			return DIGITS;
		}
		case FIRST_DIGIT: {
			int digit = from_digit(input);
			if (digit < 0)
				throw std::out_of_range("Number must start from sign or digit");
			value = digit;
			return DIGITS;
		}
		case FIRST_NEGATIVE_DIGIT: {
			int digit = from_digit(input);
			if (digit < 0)
				throw std::out_of_range("Number must start from sign or digit");
			value = -digit;
			return NEGATIVE_DIGITS;
		}
		case DIGITS: {
			if (std::isspace(input))
				return TRAILING_WS;
			int digit  = from_digit(input);
			const T ma = std::numeric_limits<T>::max();
			if (digit < 0)
				throw std::out_of_range("Number must continue with digits");
			if (value > ma / 10)
				throw std::runtime_error("Number overflow");
			value *= 10;
			if (value > ma - digit)
				throw std::runtime_error("Number overflow");
			value += digit;
			return DIGITS;
		}
		case NEGATIVE_DIGITS: {
			if (std::isspace(input))
				return TRAILING_WS;
			int digit  = from_digit(input);
			const T mi = std::numeric_limits<T>::min();
			if (digit < 0)
				throw std::out_of_range("Number must continue with digits");
			if (value < mi / 10)
				throw std::runtime_error("Number underflow");
			value *= 10;
			if (value < mi + digit)
				throw std::runtime_error("Number underflow");
			value -= digit;
			return NEGATIVE_DIGITS;
		}
		case TRAILING_WS:
			if (std::isspace(input))
				return TRAILING_WS;
			throw std::out_of_range("Number must contain only whitespaces after digits");
		default:
			throw std::logic_error("Invalid numbers parser state");
		}
	}
};

template<typename T>
inline T integer_cast_is_integral(const std::string &arg, std::false_type) {
	return integer_parse<T>(arg.data(), arg.data() + arg.size());
}
template<typename T>
inline T integer_cast_is_integral(const char *arg, std::false_type) {
	return integer_parse<T>(arg, arg + strlen(arg));
}

}  // namespace details

template<typename T, typename S>
inline T integer_cast(const S &arg) {
	static_assert(std::is_integral<T>::value, "Target type must be integral");
	return details::integer_cast_is_integral<T>(arg, std::is_integral<S>{});
}

template<typename T>
inline T integer_cast(const char *data, size_t size) {
	static_assert(std::is_integral<T>::value, "Target type must be integral");
	return details::integer_parse<T>(data, data + size);
}

}  // namespace crab
