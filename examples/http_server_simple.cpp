// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <iostream>

#include <crab/crab.hpp>

namespace http = crab::http;

#include <limits>
#include <type_traits>

// some macros to help with branch prediction
#if defined(__GNUC__) || defined(__GNUG__)
#define ATOI_LIKELY(v) __builtin_expect(!!(v), 1)
#define ATOI_UNLIKELY(v) __builtin_expect(!!(v), 0)
#else
#define ATOI_LIKELY(v) v
#define ATOI_UNLIKELY(v) v
#endif

namespace jsteemann {

// low-level worker function to convert the string value between p
// (inclusive) and e (exclusive) into a negative number value of type T,
// without validation of the input string - use this only for trusted input!
//
// the input string will always be interpreted as a base-10 number.
// expects the input string to contain only the digits '0' to '9'.
// there is no validation of the input string, and overflow or underflow
// of the result value will not be detected.
// this function will not modify errno.
template<typename T>
inline T atoi_negative_unchecked(char const *p, char const *e) noexcept {
	T result = 0;
	while (p != e) {
		result = (result << 1) + (result << 3) - (*(p++) - '0');
	}
	return result;
}

// low-level worker function to convert the string value between p
// (inclusive) and e (exclusive) into a positive number value of type T,
// without validation of the input string - use this only for trusted input!
//
// the input string will always be interpreted as a base-10 number.
// expects the input string to contain only the digits '0' to '9'.
// there is no validation of the input string, and overflow or underflow
// of the result value will not be detected.
// this function will not modify errno.
template<typename T>
inline T atoi_positive_unchecked(char const *p, char const *e) noexcept {
	T result = 0;
	while (p != e) {
		result = (result << 1) + (result << 3) + *(p++) - '0';
	}

	return result;
}

// function to convert the string value between p
// (inclusive) and e (exclusive) into a number value of type T, without
// validation of the input string - use this only for trusted input!
//
// the input string will always be interpreted as a base-10 number.
// expects the input string to contain only the digits '0' to '9'. an
// optional '+' or '-' sign is allowed too.
// there is no validation of the input string, and overflow or underflow
// of the result value will not be detected.
// this function will not modify errno.
template<typename T>
inline T atoi_unchecked(char const *p, char const *e) noexcept {
	if (ATOI_UNLIKELY(p == e)) {
		return T();
	}

	if (*p == '-') {
		if (!std::is_signed<T>::value) {
			return T();
		}
		return atoi_negative_unchecked<T>(++p, e);
	}
	if (ATOI_UNLIKELY(*p == '+')) {
		++p;
	}

	return atoi_positive_unchecked<T>(p, e);
}

// low-level worker function to convert the string value between p
// (inclusive) and e (exclusive) into a negative number value of type T
//
// the input string will always be interpreted as a base-10 number.
// expects the input string to contain only the digits '0' to '9'.
// if any other character is found, the output parameter "valid" will
// be set to false. if the parsed value is less than what type T can
// store without truncation, "valid" will also be set to false.
// this function will not modify errno.
template<typename T>
inline T atoi_negative(char const *p, char const *e, bool &valid) noexcept {
	if (ATOI_UNLIKELY(p == e)) {
		valid = false;
		return T();
	}

	constexpr T cutoff    = (std::numeric_limits<T>::min)() / 10;
	constexpr char cutlim = -((std::numeric_limits<T>::min)() % 10);
	T result              = 0;

	do {
		char c = *p;
		// we expect only '0' to '9'. everything else is unexpected
		if (ATOI_UNLIKELY(c < '0' || c > '9')) {
			valid = false;
			return result;
		}

		c -= '0';
		// we expect the bulk of values to not hit the bounds restrictions
		if (ATOI_UNLIKELY(result < cutoff || (result == cutoff && c > cutlim))) {
			valid = false;
			return result;
		}
		result *= 10;
		result -= c;
	} while (++p < e);

	valid = true;
	return result;
}

// low-level worker function to convert the string value between p
// (inclusive) and e (exclusive) into a positive number value of type T
//
// the input string will always be interpreted as a base-10 number.
// expects the input string to contain only the digits '0' to '9'.
// if any other character is found, the output parameter "valid" will
// be set to false. if the parsed value is greater than what type T can
// store without truncation, "valid" will also be set to false.
// this function will not modify errno.
template<typename T>
inline T atoi_positive(char const *p, char const *e, bool &valid) noexcept {
	if (ATOI_UNLIKELY(p == e)) {
		valid = false;
		return T();
	}

	constexpr T cutoff    = (std::numeric_limits<T>::max)() / 10;
	constexpr char cutlim = (std::numeric_limits<T>::max)() % 10;
	T result              = 0;

	do {
		char c = *p;

		// we expect only '0' to '9'. everything else is unexpected
		if (ATOI_UNLIKELY(c < '0' || c > '9')) {
			valid = false;
			return result;
		}

		c -= '0';
		// we expect the bulk of values to not hit the bounds restrictions
		if (ATOI_UNLIKELY(result > cutoff || (result == cutoff && c > cutlim))) {
			valid = false;
			return result;
		}
		result *= 10;
		result += c;
	} while (++p < e);

	valid = true;
	return result;
}

// function to convert the string value between p
// (inclusive) and e (exclusive) into a number value of type T
//
// the input string will always be interpreted as a base-10 number.
// expects the input string to contain only the digits '0' to '9'. an
// optional '+' or '-' sign is allowed too.
// if any other character is found, the output parameter "valid" will
// be set to false. if the parsed value is less or greater than what
// type T can store without truncation, "valid" will also be set to
// false.
// this function will not modify errno.
template<typename T>
inline typename std::enable_if<std::is_signed<T>::value, T>::type atoi(
    char const *p, char const *e, bool &valid) noexcept {
	if (ATOI_UNLIKELY(p == e)) {
		valid = false;
		return T();
	}

	if (*p == '-') {
		return atoi_negative<T>(++p, e, valid);
	}
	if (ATOI_UNLIKELY(*p == '+')) {
		++p;
	}

	return atoi_positive<T>(p, e, valid);
}

template<typename T>
inline typename std::enable_if<std::is_unsigned<T>::value, T>::type atoi(
    char const *p, char const *e, bool &valid) noexcept {
	if (ATOI_UNLIKELY(p == e)) {
		valid = false;
		return T();
	}

	if (*p == '-') {
		valid = false;
		return T();
	}
	if (ATOI_UNLIKELY(*p == '+')) {
		++p;
	}

	return atoi_positive<T>(p, e, valid);
}

}  // namespace jsteemann

int naive_atoi(const char *begin) {
	int num = 0;
	int neg = 0;
	while (isspace(*begin))
		begin++;
	if (*begin == '-') {
		neg = 1;
		begin++;
	}
	while (true) {
		int digit = crab::from_digit(*begin);
		if (digit < 0)
			break;
		num = 10 * num + digit;
		begin++;
	}
	if (neg)
		num = -num;
	return num;
}

int naive_atoi_end(const char *begin, const char *end) {
	int num = 0;
	int neg = 0;
	while (begin != end && isspace(*begin))
		begin++;
	if (begin == end)
		return num;
	if (*begin == '-') {
		neg = 1;
		begin++;
		if (begin == end)
			return num;
	}
	while (true) {
		int digit = crab::from_digit(*begin);
		if (digit < 0)
			break;
		num = 10 * num + digit;
		begin++;
		if (begin == end)
			break;
	}
	if (neg)
		num = -num;
	return num;
}

void benchmark_fun(const std::vector<std::string> &strs, int sum, const std::string &msg,
    std::function<int(const std::string &s)> &&fun) {
	//	std::cout << "Parsing integers... fun=" << msg << std::endl;
	auto start = std::chrono::high_resolution_clock::now();
	int result = 0;
	for (const auto &s : strs)
		result += fun(s);
	auto mksec =
	    std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start)
	        .count();
	std::cout << msg << " mksec=" << mksec << " sum=" << sum << std::endl;
	if (result != sum)
		std::cout << msg << " wrong sum, error while parsing" << std::endl;
}

int benchmark() {
	std::vector<int> ints;
	std::vector<std::string> strs;
	int sum = 0;
	for (size_t i = 0; i != 10000000; ++i) {
		int value = int(rand() * rand() + rand());
		ints.push_back(value);
		sum += ints.back();
		strs.push_back(std::to_string(ints.back()));
		if (i % 3 == 0)
			strs.back() += "  ";
		if (i % 7 == 0)
			strs.back() = "  " + strs.back();
	}
	std::cout << "U Benchmarking atoi count=" << strs.size() << std::endl;
	benchmark_fun(strs, sum, "naive_atoi", [](const std::string &s) { return naive_atoi(s.c_str()); });
	benchmark_fun(strs, sum, "std::stoi", [](const std::string &s) { return std::stoi(s); });
	benchmark_fun(strs, sum, "std::atoi", [](const std::string &s) { return std::atoi(s.c_str()); });
	benchmark_fun(strs, sum, "crab", [](const std::string &s) { return crab::integer_cast<int>(s); });
	benchmark_fun(strs, sum, "naive_atoi_end",
	    [](const std::string &s) { return naive_atoi_end(s.c_str(), s.c_str() + s.size()); });
	benchmark_fun(strs, sum, "jsteemann", [](const std::string &s) {
		bool valid = false;
		;
		return jsteemann::atoi<int>(s.data(), s.data() + s.size(), valid);
	});
	//		result += crab::integer_cast<int>(strs[i]);
	//		result += crab::details::integer_parse<int>(strs[i].data(), strs[i].data() + strs[i].size());
	//		result += std::stoi(strs[i]);
	//		result += std::atoi(strs[i].c_str());
	//		result += naive_atoi(strs[i].data(), strs[i].data() + strs[i].size());
	//		bool valid = false;
	//		if (ints[i] != jsteemann::atoi<int>(strs[i].data(), strs[i].data() + strs[i].size(), valid))
	//			throw std::logic_error("Error while parsing");
	//		result += ints[i];
	//	}
	return 0;
}

int main() {
	return benchmark();
	std::cout << "This is simple HTTP server" << std::endl;
	std::cout << crab::integer_cast<uint16_t>("  +65535  ") << std::endl;
	//	std::cout << crab::integer_cast<uint16_t>("-5") << std::endl;
	std::cout << crab::integer_cast<int16_t>("-327 69") << std::endl;
	crab::RunLoop runloop;

	http::Server server(7000);

	server.r_handler = [&](http::Client *who, http::RequestBody &&request, http::ResponseBody &response) -> bool {
		response.r.status = 200;
		response.r.set_content_type("text/plain", "charset=utf-8");
		response.set_body("Hello, Crab!");
		return true;
	};

	runloop.run();
	return 0;
}
