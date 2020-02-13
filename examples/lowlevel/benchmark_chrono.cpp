// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <chrono>
#include <iostream>

template<typename T>
void benchmark() {
	auto start = T::now();
	while (true) {
		auto now = T::now();
		if (std::chrono::duration_cast<std::chrono::microseconds>(now - start).count() != 0) {
			start = now;
			break;
		}
	}
	size_t counter = 0;
	while (true) {
		counter += 1;
		auto now = T::now();
		if (std::chrono::duration_cast<std::chrono::microseconds>(now - start).count() != 0)
			break;
	}
	std::cout << "now() calls per microsecond counter=" << counter << std::endl;
}

int main() {
	std::cout << "std::chrono::steady_clock::";
	benchmark<std::chrono::steady_clock>();
	std::cout << "std::chrono::high_resolution_clock::";
	benchmark<std::chrono::high_resolution_clock>();
	return 0;
}
