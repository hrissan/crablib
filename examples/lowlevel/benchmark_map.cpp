// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <chrono>
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

template<typename T>
void benchmark() {
	T storage;
	std::mt19937 rnd;
	std::vector<std::string> to_insert;
	std::vector<std::string> to_search;
	const size_t COUNT = 1000000;
	for (size_t i = 0; i != COUNT; ++i) {
		to_insert.push_back(std::to_string(rnd() % COUNT) + std::string("SampleSampleSampleSampleSampleSample"));
		to_search.push_back(std::to_string(rnd() % COUNT) + std::string("SampleSampleSampleSampleSampleSample"));
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
			auto bc = BucketsGetter<T>::bucket_count(storage);
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

int main() {
	std::cout << "Testing std::map" << std::endl;
	benchmark<std::map<std::string, size_t>>();
	std::cout << "Testing std::unordered" << std::endl;
	benchmark<std::unordered_map<std::string, size_t>>();
	return 0;
}
