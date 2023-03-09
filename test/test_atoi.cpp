// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
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

void must_fail(std::function<void()> &&fun) {
	try {
		fun();
	} catch (const std::exception &) {
		return;
	}
	throw std::logic_error("must_fail did not fail");
}

template<class T>
void test_type(size_t range, std::string lead, std::string trail) {
	T value  = 0;
	T value2 = 0;
	crab::Random rnd;
	std::cout << rnd.printable_string(32) << std::endl;
	for (size_t i = 0; i != range; ++i) {
		value  = rnd.pod<T>();
		value2 = crab::integer_cast<T>(std::to_string(value));
		invariant(value == value2, "");
	}
	for (size_t i = 0; i != range; ++i) {
		value  = std::numeric_limits<T>::min() + T(i);
		value2 = crab::integer_cast<T>(std::to_string(value));
		invariant(value == value2, "");
		if (value > 0)
			value2 = crab::integer_cast<T>("+" + std::to_string(value));
		invariant(value == value2, "");
		if (value > 0)
			must_fail([&] { crab::integer_cast<T>("000" + std::to_string(value)); });
		value  = std::numeric_limits<T>::max() - T(i);
		value2 = crab::integer_cast<T>(std::to_string(value));
		invariant(value == value2, "");
		if (value > 0)
			value2 = crab::integer_cast<T>("+" + std::to_string(value));
		invariant(value == value2, "");
		if (value > 0)
			must_fail([&] { crab::integer_cast<T>("000" + std::to_string(value)); });
	}
	value = std::numeric_limits<T>::max();
	must_fail([&] { value2 = crab::integer_cast<T>(std::to_string(value) + "0"); });
	for (size_t i = 0; i != 10; ++i) {
		std::string ma = std::to_string(value);
		if (ma.back() >= char('0' + i))
			continue;
		ma.back() = char('0' + i);
		must_fail([&] { value2 = crab::integer_cast<T>(ma); });
	}
	value = std::numeric_limits<T>::min();
	if (value != 0) {
		must_fail([&] { value2 = crab::integer_cast<T>(std::to_string(value) + "0"); });
		for (size_t i = 0; i != 10; ++i) {
			std::string ma = std::to_string(value);
			if (ma.back() >= char('0' + i))
				continue;
			ma.back() = char('0' + i);
			must_fail([&] { value2 = crab::integer_cast<T>(ma); });
		}
	}
}

void test_safe_math() {
	for (int i = std::numeric_limits<int8_t>::min(); i <= std::numeric_limits<int8_t>::max(); ++i) {
		for (int j = std::numeric_limits<int8_t>::min(); j <= std::numeric_limits<int8_t>::max(); ++j) {
			int plus        = i + j;
			bool good_plus  = (plus >= std::numeric_limits<int8_t>::min()) && (plus <= std::numeric_limits<int8_t>::max());
			int minus       = i - j;
			bool good_minus = (minus >= std::numeric_limits<int8_t>::min()) && (minus <= std::numeric_limits<int8_t>::max());
			auto safe_plus  = crab::safe_add_opt<int8_t>(i, j);
			auto safe_minus = crab::safe_sub_opt<int8_t>(i, j);
			invariant(!!safe_plus == good_plus, "");
			invariant(!!safe_minus == good_minus, "");
			invariant(!safe_plus || plus == *safe_plus, "");
			invariant(!safe_minus || minus == *safe_minus, "");
			if (safe_plus)
				crab::safe_add<int8_t>(i, j);
			else
				must_fail([&] { crab::safe_add<int8_t>(i, j); });
			if (safe_minus)
				crab::safe_sub<int8_t>(i, j);
			else
				must_fail([&] { crab::safe_sub<int8_t>(i, j); });
		}
	}
}

int main() {
	test_safe_math();
	invariant(crab::integer_cast<int>("-123456789") == -123456789, "");
	invariant(crab::integer_cast<unsigned>("987654321") == 987654321, "");

	test_type<char>(128, "", "");
	test_type<uint8_t>(256, "", "");
	test_type<short>(32768, "", "");
	test_type<uint16_t>(65536, "", "");
	size_t COUNT = 1000000;
	test_type<int>(COUNT, "", "");
	test_type<unsigned>(COUNT, "", "");
	test_type<long>(COUNT, "", "");
	test_type<unsigned long>(COUNT, "", "");
	test_type<long long>(COUNT, "", "");
	test_type<unsigned long long>(COUNT, "", "");
	test_type<intmax_t>(COUNT, "", "");
	test_type<uintmax_t>(COUNT, "", "");

	return 0;
}
