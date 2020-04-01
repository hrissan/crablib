// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <array>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <string>

template<typename T>
T local_rol(T mask, size_t shift) {
	shift = shift & (sizeof(T) * 8 - 1);
	return (mask << shift) | (mask >> (sizeof(T) * 8 - shift));
}

static constexpr size_t power(size_t value, size_t pow, size_t collect = 1) {
	return pow <= 0 ? collect : power(value, pow - 1, collect * value);
}

template<int Alg>
void check_uniformity(const std::string &label) {
	constexpr size_t TAIL      = power(62, 5);
	constexpr size_t LONG_TAIL = (std::numeric_limits<uint32_t>::max() - TAIL + 1) / TAIL * TAIL + TAIL - 1;
	std::cout << "TAIL=" << TAIL << " LONG_TAIL=" << LONG_TAIL << std::endl;
	std::uniform_int_distribution<uint32_t> distr_char(0, 61);
	std::uniform_int_distribution<uint32_t> distr(0, TAIL);
	std::uniform_int_distribution<uint32_t> distr_long(0, LONG_TAIL);

	using namespace std::chrono;
	std::array<std::vector<size_t>, 5> indexes;
	for (auto &arr : indexes)
		arr.resize(62);
	for (uint64_t i = 0; i <= std::numeric_limits<uint32_t>::max(); i++) {
		if (Alg == 0) {
			uint32_t value = static_cast<uint32_t>(i);
			for (size_t a = 0; a != 5; ++a) {
				uint32_t result = static_cast<uint32_t>((uint64_t(value) * 62) >> 32);
				value           = local_rol(value, 6);
				indexes[a][result] += 1;
			}
		} else if (Alg == 1) {
			uint32_t value = static_cast<uint32_t>(i);  // distr(mt); //mt(); // Simulate rnd()
			for (size_t a = 0; a != 5; ++a) {
				uint32_t result = static_cast<uint32_t>(((value >> 6) * 62) >> 26);
				value           = local_rol(value, 6);
				indexes[a][result] += 1;
			}
		} else if (Alg == 2) {
			uint32_t value = static_cast<uint32_t>(i);  // distr(mt); //mt(); // Simulate rnd()
			if (value > LONG_TAIL)
				continue;
			for (size_t a = 0; a != 5; ++a) {
				uint32_t result = value % 62;
				value           = value / 62;
				indexes[a][result] += 1;
			}
		}
	}
	if (Alg <= 2) {
		for (auto &arr : indexes) {
			for (const auto &v : arr)
				std::cout << " " << v;
			std::cout << std::endl;
		}
	}
	std::mt19937 mt;
	constexpr size_t SPEED_UP = 20;
	auto start                = high_resolution_clock::now();
	for (size_t i = 0; i <= std::numeric_limits<uint32_t>::max() / SPEED_UP; i++) {
		if (Alg == 0) {
			uint32_t value = mt();
			for (size_t a = 0; a != 5; ++a) {
				uint32_t result = (uint64_t(value) * 62) >> 32;
				value           = local_rol(value, 6);
				indexes[a][result] += 1;
			}
		} else if (Alg == 1) {
			uint32_t value = mt();
			for (size_t a = 0; a != 5; ++a) {
				uint32_t result = ((value >> 6) * 62) >> 26;
				value           = local_rol(value, 6);
				indexes[a][result] += 1;
			}
		} else if (Alg == 2) {
			uint32_t value = mt();
			if (value > LONG_TAIL)
				continue;
			for (size_t a = 0; a != 5; ++a) {
				uint32_t result = value % 62;
				value           = value / 62;
				indexes[a][result] += 1;
			}
		} else if (Alg == 3) {
			uint32_t value = distr(mt);  // mt(); // Simulate rnd()
			for (size_t a = 0; a != 5; ++a) {
				uint32_t result = value % 62;
				value           = value / 62;
				indexes[a][result] += 1;
			}
		} else if (Alg == 4) {
			uint32_t value = distr_long(mt);  // mt(); // Simulate rnd()
			for (size_t a = 0; a != 5; ++a) {
				uint32_t result = value % 62;
				value           = value / 62;
				indexes[a][result] += 1;
			}
		} else if (Alg == 5) {
			for (size_t a = 0; a != 5; ++a) {
				uint32_t result = distr_char(mt);
				indexes[a][result] += 1;
			}
		}
	}
	auto stop = high_resolution_clock::now();
	std::cout << "Time for " << std::left << std::setw(12) << label
	          << " mksec=" << duration_cast<microseconds>(stop - start).count() * SPEED_UP << std::endl;
}

void long_tail(uint32_t ma, uint32_t TAIL) {
	const size_t LONG_TAIL = (ma - TAIL + 1) / TAIL * TAIL + TAIL - 1;
	std::cout << "For TAIL=" << TAIL << " LONG_TAIL=" << LONG_TAIL << " q=" << LONG_TAIL / TAIL
	          << " r=" << LONG_TAIL % TAIL << std::endl;
}

int main() {
	long_tail(std::numeric_limits<uint32_t>::max(), 62 * 62 * 62 * 62 * 62);
	long_tail(std::numeric_limits<uint32_t>::max(), 0x80000000U);
	long_tail(std::numeric_limits<uint32_t>::max(), 2);
	long_tail(std::numeric_limits<uint32_t>::max(), 1);
	check_uniformity<5>("mt() per char");
	check_uniformity<4>("distr_long");
	check_uniformity<3>("distr");
	check_uniformity<2>("div, rem");
	check_uniformity<1>("mul, rol");
	check_uniformity<0>("Exact mul, rol");
	return 0;
}
