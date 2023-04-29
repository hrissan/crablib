// Copyright (c) 2007-2023, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <array>
#include <chrono>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include <limits>
#include <type_traits>

#include <crab/crab.hpp>

// Code from https://github.com/jsteemann/atoi used as a benchmark reference

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
inline typename std::enable_if<std::is_signed<T>::value, T>::type atoi(char const *p, char const *e, bool &valid) noexcept {
	//	while (p != e && is_integer_space(*begin))
	//		p += 1;
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
inline typename std::enable_if<std::is_unsigned<T>::value, T>::type atoi(char const *p, char const *e, bool &valid) noexcept {
	//	while (p != e && is_integer_space(*begin))
	//		p += 1;
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
		if (!isdigit(*begin))
			break;
		num = 10 * num + *begin - '0';
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
		if (!isdigit(*begin))
			break;
		num = 10 * num + *begin - '0';
		begin++;
		if (begin == end)
			break;
	}
	if (neg)
		num = -num;
	return num;
}

void benchmark_fun(
    const std::vector<std::string> &strs, int sum, const std::string &msg, std::function<int(const std::string &s)> &&fun) {
	auto start = std::chrono::high_resolution_clock::now();
	int result = 0;
	for (const auto &s : strs)
		result += fun(s);
	auto mksec = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start).count();
	std::cout << msg << " mksec=" << mksec << " sum=" << result << std::endl;
	if (result != sum)
		std::cout << msg << " wrong sum, error while parsing" << std::endl;
}

template<class T, class U>
U test(char const *label, U count) {
	using namespace std::chrono;
	T gen(100);

	U result = 0;

	auto start = high_resolution_clock::now();
	for (U i = 0; i < count; i++) {
		auto val = ((gen() >> 8) * 62) >> 24;
		result ^= val;  // % 62
	}
	auto stop = high_resolution_clock::now();
	std::cout << "Time for " << std::left << std::setw(12) << label << " count=" << count
	          << " mksec=" << duration_cast<microseconds>(stop - start).count() << std::endl;
	return result;
}

int main() {
	crab::Random rnd;
	std::cout << rnd.printable_string(32) << std::endl;
	std::cout << rnd.printable_string(32) << std::endl;
	std::cout << rnd.printable_string(32) << std::endl;
	std::cout << rnd.printable_string(32) << std::endl;
	unsigned long long limit = 1000000000;

	auto result1 = test<std::mt19937>("mt19937: ", limit);
	auto result2 = test<std::mt19937_64>("mt19937_64: ", limit);

	std::cout << "Ignore results: " << result1 << ", " << result2 << "\n";

	std::vector<int> ints;
	std::vector<std::string> strs;
	int sum      = 0;
	size_t COUNT = 40000000;
	std::cout << "Preparing atoi benchmark count=" << COUNT << std::endl;
	ints.reserve(COUNT);
	strs.reserve(COUNT);
	for (size_t i = 0; i != COUNT; ++i) {
		int value = int(rand() * rand() + rand());
		sum += value;
		ints.push_back(value);
		strs.push_back(std::to_string(value));
		if (rand() % 3 == 0)
			strs.back() += "  ";
		if (rand() % 5 == 0)
			strs.back() = "  " + strs.back();
	}
	benchmark_fun(strs, sum, "naive_atoi", [](const std::string &s) { return naive_atoi(s.c_str()); });
	benchmark_fun(strs, sum, "std::stoi", [](const std::string &s) { return std::stoi(s); });
	benchmark_fun(strs, sum, "std::atoi", [](const std::string &s) { return std::atoi(s.c_str()); });
	benchmark_fun(strs, sum, "crab", [](const std::string &s) { return crab::integer_cast<int>(s); });
	benchmark_fun(strs, sum, "naive_atoi_end", [](const std::string &s) { return naive_atoi_end(s.c_str(), s.c_str() + s.size()); });
	benchmark_fun(strs, sum, "jsteemann", [](const std::string &s) {
		bool valid = false;
		return jsteemann::atoi<int>(s.data(), s.data() + s.size(), valid);
	});
	return 0;
}
