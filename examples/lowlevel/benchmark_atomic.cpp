// Copyright (c) 2007-2023, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <atomic>
#include <chrono>
#include <iostream>

std::atomic<size_t> global_counter;

int main() {
	auto start = std::chrono::high_resolution_clock::now();
	std::cout << "Sampling atomic += 1 speed..." << std::endl;
	for (size_t i = 0; i != 1000000; ++i) {
		global_counter += 1;
	}
	auto mksec = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start).count();
	std::cout << "1 million atomic adds mksec=" << mksec << std::endl;
	return 0;
}
