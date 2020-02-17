// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <chrono>
#include <functional>
#include <iostream>
#include <map>
#include <random>
#include <unordered_map>

template<typename T>
struct BucketsGetter {
	static size_t bucket_count(const T &) { return 0; }
};

template<>
struct BucketsGetter<std::unordered_map<std::string, size_t>> {
	static size_t bucket_count(const std::unordered_map<std::string, size_t> &v) { return v.bucket_count(); }
};

constexpr size_t COUNT = 1000000;

template<typename T, typename S>
void benchmark(std::function<T(size_t)> items_gen) {
	S storage;
	std::mt19937 rnd;
	std::vector<T> to_insert;
	std::vector<T> to_search;
	for (size_t i = 0; i != COUNT; ++i) {
		to_insert.push_back(items_gen(rnd() % COUNT));
		to_search.push_back(items_gen(rnd() % COUNT));
	}
	auto tp        = std::chrono::high_resolution_clock::now();
	auto start     = tp;
	size_t counter = 0;
	struct Sample {
		int mksec;
		size_t counter;
		size_t buckets;
	};
	std::vector<Sample> long_samples;
	long_samples.reserve(to_insert.size());
	for (const auto &key : to_insert) {
		storage.emplace(key, ++counter);
		auto now   = std::chrono::high_resolution_clock::now();
		auto mksec = std::chrono::duration_cast<std::chrono::microseconds>(now - tp).count();
		if (mksec > 100) {
			auto bc = BucketsGetter<S>::bucket_count(storage);
			long_samples.emplace_back(Sample{int(mksec), counter, bc});
		}
		tp = now;
	}
	auto now   = std::chrono::high_resolution_clock::now();
	auto mksec = std::chrono::duration_cast<std::chrono::microseconds>(now - start).count();
	std::cout << "inserted " << storage.size() << ", mksec=" << mksec << std::endl;
	for (const auto &p : long_samples)
		std::cout << "mksec=" << p.mksec << " counter=" << p.counter << " buckets=" << p.buckets << std::endl;
	start   = now;
	counter = 0;
	for (const auto &key : to_search)
		counter += storage.count(key);
	now   = std::chrono::high_resolution_clock::now();
	mksec = std::chrono::duration_cast<std::chrono::microseconds>(now - start).count();
	std::cout << "searched " << to_search.size() << ", found=" << counter << ", mksec=" << mksec << std::endl;
}

std::string string_gen(size_t c) {
	return std::to_string(c % COUNT) + std::string("SampleSampleSampleSampleSampleSample");
}

int int_gen(size_t c) { return int(c); }

size_t small_int_gen(size_t c) { return c % 256; }

int main() {
	std::cout << "Testing std::map<std::string> count=" << COUNT << std::endl;
	benchmark<std::string, std::map<std::string, size_t>>(string_gen);
	std::cout << "Testing std::unordered<std::string> count=" << COUNT << std::endl;
	benchmark<std::string, std::unordered_map<std::string, size_t>>(string_gen);
	std::cout << "----" << std::endl;

	std::cout << "Testing std::map<int> count=" << COUNT << std::endl;
	benchmark<int, std::map<int, size_t>>(int_gen);
	std::cout << "Testing std::unordered<int> count=" << COUNT << std::endl;
	benchmark<int, std::unordered_map<int, size_t>>(int_gen);
	std::cout << "----" << std::endl;

	std::cout << "Testing small std::map<int> count=" << COUNT << std::endl;
	benchmark<int, std::map<int, size_t>>(small_int_gen);
	std::cout << "Testing small std::unordered<int> count=" << COUNT << std::endl;
	benchmark<int, std::unordered_map<int, size_t>>(small_int_gen);
	std::cout << "----" << std::endl;
	return 0;
}
