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
inline T integer_cast_is_integral(const S &arg, std::true_type, std::false_type) {
	return integer_cast_impl<T, S>(arg, std::is_unsigned<T>{}, std::is_unsigned<S>{});
}

inline bool is_integer_space(char c) { return isspace(c); }  //  c == ' ' || c == '\t';

// C++ committee cannot do anything of quality...
// std::stoull will parse -5 to binary representation without exception, uh-oh
// there is not way to convert to (u)short, (u)char with overflow checks

// Parser focus here is on correctness, not raw speed. We allow +0 and -0, but not 00 or 000012
// PEG
// Ws          <- (SPACE / TAB)
// NonZeroBody <- [1 - 9] [0 - 9]*
// Body        <- 0 / NonZeroBody
// Value       <- Ws* (+ / -)? Body Ws*

template<typename T>
std::pair<T, const char *> integer_parse_impl(const char *begin, const char *end) {
	while (begin != end && is_integer_space(*begin))
		begin += 1;
	T value = 0;
	if (begin == end)
		return {value, "Number must start from sign or digit "};
	if (*begin == '-') {
		if (std::is_unsigned<T>::value)  // No template magic, optimizer will remove excess code for unsigned
			return {value, "Unsigned Number cannot be negative "};
		begin += 1;
		if (begin == end)
			return {value, "Number must start from sign or digit "};
		if (!isdigit(*begin))
			return {value, "Number must start from sign or digit "};
		value                            = static_cast<T>('0' - *begin);  // reverse for no warning for unsigned
		constexpr size_t max_safe_digits = sizeof(T) * 2;                 // Approximate
		auto safe_end                    = std::min(end, begin + max_safe_digits);
		begin += 1;
		if (value != 0) {  // Disallow digits after 0
			// Convert as much as possible without overflow checks
			while (begin != safe_end) {
				if (!isdigit(*begin))
					break;
				value = value * 10 - (*begin - '0');
				begin += 1;
			}
			// Convert the rest with strict overflow checks
			while (begin != end) {
				if (!isdigit(*begin))
					break;
				int digit              = *begin - '0';
				constexpr T last_value = std::numeric_limits<T>::min() / 10;
				// Note: rounding of negative / and % is implementation defined prior to C++11
				constexpr int last_digit = -static_cast<int>(std::numeric_limits<T>::min() % 10);
				if (value < last_value || (value == last_value && digit > last_digit))
					return {value, "Number underflow "};
				value = value * 10 - digit;
				begin += 1;
			}
		}
	} else {
		if (*begin == '+')
			begin += 1;
		if (begin == end)
			return {value, "Number must start from sign or digit "};
		if (!isdigit(*begin))
			return {value, "Number must start from sign or digit "};
		value                            = static_cast<T>(*begin - '0');
		constexpr size_t max_safe_digits = sizeof(T) * 2;  // Approximate
		auto safe_end                    = std::min(end, begin + max_safe_digits);
		begin += 1;
		if (value != 0) {  // Disallow digits after 0
			// Convert as much as possible without overflow checks
			while (begin != safe_end) {
				if (!isdigit(*begin))
					break;
				value = value * 10 + *begin - '0';
				begin += 1;
			}
			// Convert the rest with strict overflow checks
			while (begin != end) {
				if (!isdigit(*begin))
					break;
				int digit                = *begin - '0';
				constexpr T last_value   = std::numeric_limits<T>::max() / 10;
				constexpr int last_digit = static_cast<int>(std::numeric_limits<T>::max() % 10);
				if (value > last_value || (value == last_value && digit > last_digit))
					return {value, "Number overflow "};
				value = value * 10 + digit;
				begin += 1;
			}
		}
	}
	while (begin != end && is_integer_space(*begin))
		begin += 1;
	if (begin != end) {
		return {value, "Number must contain only whitespaces after digits, and must not have excess leading zeroes "};
	}
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
inline T integer_cast_is_integral(const std::string &arg, std::false_type, std::false_type) {
	return integer_parse<T>(arg.data(), arg.data() + arg.size());
}
template<typename T>
inline T integer_cast_is_integral(const char *arg, std::false_type, std::false_type) {
	return integer_parse<T>(arg, arg + strlen(arg));
}

template<typename T, typename S>  // source floating point
inline T integer_cast_is_integral(const S &arg, std::false_type, std::true_type) {
	if (arg > std::numeric_limits<T>::max())
		throw_out_of_range<T>(arg);
	if (arg < std::numeric_limits<T>::min())
		throw_out_of_range<T>(arg);
	return static_cast<T>(arg);
}

template<typename T>
constexpr size_t max_to_string_length_impl(T value) {
	return (value >= 0 && value < 10)
	           ? 1
	           : (std::is_signed<T>::value && value < 0 && value > -10) ? 2
	                                                                    : 1 + max_to_string_length_impl(value / 10);
}

}  // namespace details

template<typename T, typename S>
inline T integer_cast(const S &arg) {
	static_assert(std::is_integral<T>::value, "Target type must be integral");
	return details::integer_cast_is_integral<T>(arg, std::is_integral<S>{}, std::is_floating_point<S>{});
}

template<typename T>
inline T integer_cast(const char *data, size_t size) {
	static_assert(std::is_integral<T>::value, "Target type must be integral");
	return details::integer_parse<T>(data, data + size);
}

template<typename T>
details::optional<T> safe_add_opt(T a, T b) {
	if (b >= 0) {
		if (a > std::numeric_limits<T>::max() - b)
			return {};
	} else {
		if (a < std::numeric_limits<T>::min() - b)
			return {};
	}
	return a + b;
}

template<typename T>
details::optional<T> safe_sub_opt(T a, T b) {
	if (b <= 0) {
		if (a > std::numeric_limits<T>::max() + b)
			return {};
	} else {
		if (a < std::numeric_limits<T>::min() + b)
			return {};
	}
	return a - b;
}

template<typename T>
T safe_add(T a, T b) {
    if (b >= 0) {
        if (a > std::numeric_limits<T>::max() - b)
            throw std::out_of_range("add overflow");
    } else {
        if (a < std::numeric_limits<T>::min() - b)
            throw std::out_of_range("add underflow");
    }
    return a + b;
}

template<typename T>
T safe_sub(T a, T b) {
    if (b <= 0) {
        if (a > std::numeric_limits<T>::max() + b)
            throw std::out_of_range("sub overflow");
    } else {
        if (a < std::numeric_limits<T>::min() + b)
            throw std::out_of_range("sub underflow");
    }
    return a - b;
}

template<typename T>
constexpr typename std::make_unsigned<T>::type safe_abs(T value) {
	// https://stackoverflow.com/questions/32676417/how-to-implement-unsigned-absint
	using US = typename std::make_unsigned<T>::type;
	return value < 0 ? 0 - static_cast<US>(value) : static_cast<US>(value);
}

template<typename T>
constexpr size_t max_to_string_length() {
	return std::max(  // comment for formatting
	    details::max_to_string_length_impl(std::numeric_limits<T>::max()),
	    details::max_to_string_length_impl(std::numeric_limits<T>::min()));
}

}  // namespace crab
