// Copyright (c) 2007-2023, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <chrono>
#include <functional>
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

// Assembly commented, because otherwise examples do not compile for ARM.
// There is no common macro to detect x86 for all compilers we wish to use.

inline uint64_t rdtscp_begin() {
	unsigned lo = 0, hi = 0;
//	    asm volatile(
//	        "CPUID\n\t" // Only for old CPUs
//	        "RDTSC\n\t"
//	        "mov %%edx, %0\n\t"
//	        "mov %%eax, %1\n\t"
//	        : "=r"(hi), "=r"(lo)::"%rax", "%rbx", "%rcx", "%rdx");
	return (static_cast<uint64_t>(lo) | ((static_cast<uint64_t>(hi)) << 32U));
}

inline uint64_t rdtscp_end() {
	unsigned lo = 0, hi = 0;
//	    asm volatile(
//	        "RDTSCP\n\t"
//	        "mov %%edx, %0\n\t"
//	        "mov %%eax, %1\n\t"
//	        : "=r"(hi), "=r"(lo)::"%rax", "%rbx", "%rcx", "%rdx");
	return (static_cast<uint64_t>(lo) | ((static_cast<uint64_t>(hi)) << 32U));
}

constexpr size_t COUNT = 1000000;

void benchmark2(const std::string &label, std::function<int()> &&fun) {
	auto start = std::chrono::high_resolution_clock::now();
	int result = 0;
	for (size_t i = 0; i <= COUNT; i++) {
		result += fun();
	}
	auto stop = std::chrono::high_resolution_clock::now();
	std::cout << "Time for " << COUNT << "x " << label << " ns=" << std::chrono::duration<double>(stop - start).count()*1000000000 / COUNT
	          << " mksec=" << std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count() << " result=" << result
	          << std::endl;
}

int main() {
	std::cout << "std::chrono::steady_clock::";
	benchmark<std::chrono::steady_clock>();
	std::cout << "std::chrono::high_resolution_clock::";
	benchmark<std::chrono::high_resolution_clock>();
	benchmark2("RDTSC", []() {
		int result = rdtscp_begin();
		return result - rdtscp_end();
	});
	benchmark2("steady_clock", []() {
		return std::chrono::steady_clock::now().time_since_epoch().count() + std::chrono::steady_clock::now().time_since_epoch().count();
	});
	benchmark2("system_clock", []() {
		return std::chrono::system_clock::now().time_since_epoch().count() + std::chrono::system_clock::now().time_since_epoch().count();
	});
	benchmark2("std::time", []() {
		return std::chrono::seconds(std::time(NULL)).count() + std::chrono::seconds(std::time(NULL)).count();
	});
	benchmark2("high_resolution_clock", []() {
		return std::chrono::high_resolution_clock::now().time_since_epoch().count() +
		       std::chrono::high_resolution_clock::now().time_since_epoch().count();
	});
	return 0;
}
