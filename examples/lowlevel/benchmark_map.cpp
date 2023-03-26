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

#include <crab/crab.hpp>

static size_t count_zeroes(uint64_t val) {
	for (size_t i = 0; i != sizeof(val) * 8; ++i)
		if ((val & (uint64_t(1) << i)) != 0)
			return i;
	return sizeof(val) * 8;
}

struct Random {
	explicit Random(uint64_t random_seed = 0) : random_seed(random_seed) {}
	uint64_t rnd() {  // MMIX by Donald Knuth
		random_seed = 6364136223846793005 * random_seed + 1442695040888963407;
		return random_seed;
	}

private:
	uint64_t random_seed;
};

static constexpr size_t LEVELS = 10;

template<class T>
class SkipList {
public:
	struct Item {  // We allocate only part of it
		T value;
		Item *prev;
		size_t height;
		Item *s_nexts[LEVELS];
		Item *&nexts(size_t i) {
			if (i >= height)
				throw std::logic_error("out of nexts");
			return s_nexts[i];
		}
	};
	struct InsertPtr {
		std::array<Item *, LEVELS> previous_levels{};
		Item *next() const { return previous_levels.at(0)->nexts(0); }
	};
	SkipList() {
		tail_head.value  = T{};
		tail_head.prev   = &tail_head;
		tail_head.height = LEVELS;
		for (size_t i = 0; i != LEVELS; ++i)
			tail_head.nexts(i) = &tail_head;
	}
	~SkipList() {
		while (tail_head.prev != &tail_head) {
			erase_begin();
			//			print();
		}
	}
	int lower_bound(const T &value, InsertPtr *insert_ptr) {
		Item *curr            = &tail_head;
		size_t current_height = LEVELS - 1;
		int hops              = 0;
		Item **p_levels       = insert_ptr->previous_levels.data();
		while (true) {
			hops += 1;
			Item *next_curr = curr->s_nexts[current_height];
			if (next_curr == &tail_head || next_curr->value >= value) {
				p_levels[current_height] = curr;
				if (current_height == 0)
					break;
				current_height -= 1;
				continue;
			}
			curr = next_curr;
		}
		return hops;
	}
	int count(const T &value) {
		InsertPtr insert_ptr;
		lower_bound(value, &insert_ptr);
		Item *del_item = insert_ptr.next();
		if (del_item == &tail_head || del_item->value != value)
			return 0;
		return 1;
	}
	std::pair<Item *, bool> insert(const T &value) {
		InsertPtr insert_ptr;
		lower_bound(value, &insert_ptr);
		Item *next_curr = insert_ptr.next();
		if (next_curr != &tail_head && next_curr->value == value)
			return std::make_pair(next_curr, false);
		//		static uint64_t keybuf[4] = {};
		//		auto ctx = blake2b_ctx{};
		//		blake2b_init(&ctx, 32, nullptr, 0);
		//		blake2b_update(&ctx, &keybuf, sizeof(keybuf));
		//		blake2b_final(&ctx, &keybuf);

		const size_t height = std::min<size_t>(LEVELS, 1 + count_zeroes(random.rnd()) / 3);                         // keybuf[0]
		Item *new_item      = reinterpret_cast<Item *>(malloc(sizeof(Item) - (LEVELS - height) * sizeof(Item *)));  // new Item{};
		new_item->prev      = insert_ptr.previous_levels.at(0);
		next_curr->prev     = new_item;
		new_item->height    = height;
		size_t i            = 0;
		for (; i != height; ++i) {
			new_item->nexts(i)                         = insert_ptr.previous_levels.at(i)->nexts(i);
			insert_ptr.previous_levels.at(i)->nexts(i) = new_item;
		}
		//		for(; i != LEVELS; ++i)
		//			new_item->nexts(i) = nullptr;
		new_item->value = value;
		return std::make_pair(new_item, true);
	}
	bool erase(const T &value) {
		InsertPtr insert_ptr;
		lower_bound(value, &insert_ptr);
		Item *del_item = insert_ptr.next();
		if (del_item == &tail_head || del_item->value != value)
			return false;
		del_item->nexts(0)->prev = del_item->prev;
		del_item->prev           = nullptr;
		for (size_t i = 0; i != del_item->height; ++i)
			if (del_item->nexts(i)) {
				insert_ptr.previous_levels.at(i)->nexts(i) = del_item->nexts(i);
				del_item->nexts(i)                         = nullptr;
			}
		free(del_item);  // delete del_item;
		return true;
	}
	void erase_begin() {
		Item *del_item = tail_head.nexts(0);
		if (del_item == &tail_head)
			throw std::logic_error("deleting head_tail");
		Item *prev_item          = del_item->prev;
		del_item->nexts(0)->prev = prev_item;
		del_item->prev           = nullptr;
		for (size_t i = 0; i != del_item->height; ++i) {
			prev_item->nexts(i) = del_item->nexts(i);
			del_item->nexts(i)  = nullptr;
		}
		free(del_item);  // delete del_item;
	}
	bool empty() const { return tail_head.prev == &tail_head; }
	Item *end(const T &v);
	void print() {
		Item *curr = &tail_head;
		std::array<size_t, LEVELS> level_counts{};
		std::cerr << "---- list ----" << std::endl;
		while (true) {
			if (curr == &tail_head)
				std::cerr << std::setw(4) << "end"
				          << " | ";
			else
				std::cerr << std::setw(4) << curr->value << " | ";
			for (size_t i = 0; i != curr->height; ++i) {
				level_counts[i] += 1;
				if (curr == &tail_head || curr->nexts(i) == &tail_head)
					std::cerr << std::setw(4) << "end"
					          << " ";
				else
					std::cerr << std::setw(4) << curr->nexts(i)->value << " ";
			}
			for (size_t i = curr->height; i != LEVELS; ++i)
				std::cerr << std::setw(4) << "_"
				          << " ";
			if (curr->prev == &tail_head)
				std::cerr << "| " << std::setw(4) << "end" << std::endl;
			else
				std::cerr << "| " << std::setw(4) << curr->prev->value << std::endl;
			if (curr == tail_head.prev)
				break;
			curr = curr->nexts(0);
		}
		std::cerr << "  #"
		          << " | ";
		for (size_t i = 0; i != LEVELS; ++i) {
			std::cerr << std::setw(4) << level_counts[i] << " ";
		}
		std::cerr << "| " << std::endl;
	}

private:
	Item tail_head;
	Random random;
};

struct HeapElement {
	crab::IntrusiveHeapIndex heap_index;
	uint64_t value = 0;

	bool operator<(const HeapElement &other) const { return value < other.value; }
};

struct HeapElementSteadyTimer {
	crab::IntrusiveHeapIndex heap_index;
	std::chrono::steady_clock::time_point value{};

	bool operator<(const HeapElementSteadyTimer &other) const { return value < other.value; }
};

// typical benchmark
// skiplist insert of 1000000 hashes, inserted 632459, seconds=1.486
// skiplist get of 1000000 hashes, hops 37.8428, seconds=1.428
// skiplist delete of 1000000 hashes, found 400314, seconds=1.565
// std::set insert of 1000000 hashes, inserted 632459, seconds=0.782
// std::set get of 1000000 hashes, found_counter 1000000, seconds=0.703
// std::set delete of 1000000 hashes, found 400314, seconds=0.906

std::vector<uint64_t> fill_random(uint64_t seed, size_t count) {
	Random random(seed);
	std::vector<uint64_t> result;
	for (size_t i = 0; i != count; ++i)
		result.push_back(random.rnd() % count);
	return result;
}

void benchmark_timers() {
	constexpr size_t COUNT      = 1000000;
	constexpr size_t COUNT_MOVE = 100;
	crab::RunLoop runloop;

	Random random(12345);

	std::vector<std::chrono::steady_clock::duration> durs;
	std::list<crab::Timer> timers;
	for (size_t i = 0; i != COUNT; ++i) {
		timers.emplace_back(crab::empty_handler);
		std::chrono::duration<double> delay(random.rnd() % COUNT);
		durs.push_back(std::chrono::duration_cast<std::chrono::steady_clock::duration>(delay));
	}
	auto idea_start                     = std::chrono::high_resolution_clock::now();
	std::list<crab::Timer>::iterator it = timers.begin();
	for (size_t i = 0; it != timers.end(); ++i, ++it) {
		it->once(durs[i]);
	}
	auto idea_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - idea_start);
	std::cout << "Set Timers (random delay)"
	          << " count=" << COUNT << ", seconds=" << double(idea_ms.count()) / 1000 << std::endl;
	idea_start = std::chrono::high_resolution_clock::now();
	for (size_t j = 0; j != COUNT_MOVE; ++j) {
		it = timers.begin();
		for (size_t i = 0; it != timers.end(); ++i, ++it) {
			it->once(durs[i] + std::chrono::steady_clock::duration{1 + j});
		}
	}
	idea_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - idea_start);
	std::cout << "Moving Timers to the future"
	          << " count=" << COUNT_MOVE << "*" << COUNT << ", seconds=" << double(idea_ms.count()) / 1000 << std::endl;
	idea_start = std::chrono::high_resolution_clock::now();
	it         = timers.begin();
	for (size_t i = 0; it != timers.end(); ++i, ++it) {
		it->cancel();
	}
	idea_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - idea_start);
	std::cout << "Cancel Timers"
	          << " count=" << COUNT << ", seconds=" << double(idea_ms.count()) / 1000 << std::endl;
}

template<class T, class Op>
void benchmark_op(const char *str, const std::vector<T> &samples, Op op) {
	size_t found_counter = 0;
	auto idea_start      = std::chrono::high_resolution_clock::now();
	for (const auto &sample : samples)
		found_counter += op(sample);
	auto idea_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - idea_start);
	std::cout << str << " count=" << samples.size() << " hits=" << found_counter << ", seconds=" << double(idea_ms.count()) / 1000
	          << std::endl;
}

void benchmark_sets() {
	size_t count                    = 1000000;
	std::vector<uint64_t> to_insert = fill_random(1, count);
	std::vector<uint64_t> to_count  = fill_random(2, count);
	std::vector<uint64_t> to_erase  = fill_random(3, count);

	std::vector<HeapElement *> el_to_insert;
	std::vector<HeapElement *> el_to_count;
	std::vector<HeapElement *> el_to_erase;

	std::map<uint64_t, HeapElement> heap_storage;
	for (auto s : to_insert) {
		auto &v = heap_storage[s];
		v.value = s;
		el_to_insert.push_back(&v);
	}
	for (auto s : to_count) {
		auto &v = heap_storage[s];
		v.value = s;
		el_to_count.push_back(&v);
	}
	for (auto s : to_erase) {
		auto &v = heap_storage[s];
		v.value = s;
		el_to_erase.push_back(&v);
	}

	crab::IntrusiveHeap<HeapElement, &HeapElement::heap_index, std::less<HeapElement>> int_heap;
	int_heap.reserve(1000000);
	benchmark_op("OurHeap insert ", el_to_insert, [&](HeapElement *sample) -> size_t { return int_heap.insert(*sample); });
	benchmark_op("OurHeap erase ", el_to_erase, [&](HeapElement *sample) -> size_t { return int_heap.erase(*sample); });
	benchmark_op("OurHeap pop_front ", el_to_insert, [&](HeapElement *sample) -> size_t {
		if (int_heap.empty())
			return 0;
		int_heap.pop_front();
		return 1;
	});

	std::vector<HeapElementSteadyTimer *> el_to_insert_st;
	std::vector<HeapElementSteadyTimer *> el_to_count_st;
	std::vector<HeapElementSteadyTimer *> el_to_erase_st;

	std::map<uint64_t, HeapElementSteadyTimer> heap_storage_st;
	for (auto s : to_insert) {
		auto &v = heap_storage_st[s];
		v.value = std::chrono::steady_clock::time_point{std::chrono::steady_clock::duration{s}};
		el_to_insert_st.push_back(&v);
	}
	for (auto s : to_count) {
		auto &v = heap_storage_st[s];
		v.value = std::chrono::steady_clock::time_point{std::chrono::steady_clock::duration{s}};
		el_to_count_st.push_back(&v);
	}
	for (auto s : to_erase) {
		auto &v = heap_storage_st[s];
		v.value = std::chrono::steady_clock::time_point{std::chrono::steady_clock::duration{s}};
		el_to_erase_st.push_back(&v);
	}

	crab::IntrusiveHeap<HeapElementSteadyTimer, &HeapElementSteadyTimer::heap_index, std::less<HeapElementSteadyTimer>> steady_timer_heap;
	int_heap.reserve(1000000);
	benchmark_op("OurHeap ST insert ", el_to_insert_st,
	    [&](HeapElementSteadyTimer *sample) -> size_t { return steady_timer_heap.insert(*sample); });
	benchmark_op(
	    "OurHeap ST erase ", el_to_erase_st, [&](HeapElementSteadyTimer *sample) -> size_t { return steady_timer_heap.erase(*sample); });
	benchmark_op("OurHeap ST pop_front ", el_to_insert_st, [&](HeapElementSteadyTimer *sample) -> size_t {
		if (steady_timer_heap.empty())
			return 0;
		steady_timer_heap.pop_front();
		return 1;
	});

	std::set<uint64_t> test_set;
	benchmark_op("std::set insert ", to_insert, [&](uint64_t sample) -> size_t { return test_set.insert(sample).second; });
	benchmark_op("std::set count ", to_count, [&](uint64_t sample) -> size_t { return test_set.count(sample); });
	benchmark_op("std::set erase ", to_erase, [&](uint64_t sample) -> size_t { return test_set.erase(sample); });
	benchmark_op("std::set pop_front ", to_insert, [&](uint64_t sample) -> size_t {
		if (!test_set.empty()) {
			test_set.erase(test_set.begin());
			return 1;
		}
		return 0;
	});

	std::unordered_set<uint64_t> test_uset;
	benchmark_op("std::uset insert ", to_insert, [&](uint64_t sample) -> size_t { return test_uset.insert(sample).second; });
	benchmark_op("std::uset count ", to_count, [&](uint64_t sample) -> size_t { return test_uset.count(sample); });
	benchmark_op("std::uset erase ", to_erase, [&](uint64_t sample) -> size_t { return test_uset.erase(sample); });
	benchmark_op("std::uset pop_front ", to_insert, [&](uint64_t sample) -> size_t {
		if (!test_uset.empty()) {
			test_uset.erase(test_uset.begin());
			return 1;
		}
		return 0;
	});

	SkipList<uint64_t> skip_list;
	benchmark_op("skip_list insert ", to_insert, [&](uint64_t sample) -> size_t { return skip_list.insert(sample).second; });
	benchmark_op("skip_list count ", to_count, [&](uint64_t sample) -> size_t { return skip_list.count(sample); });
	benchmark_op("skip_list erase ", to_erase, [&](uint64_t sample) -> size_t { return skip_list.erase(sample); });

	//	immer::set<uint64_t> immer_set;
	//	benchmark_op("immer insert ", to_insert, [&](uint64_t sample)->size_t{
	//		size_t was_size = immer_set.size();
	//		immer_set = immer_set.insert(sample);
	//		return immer_set.size() - was_size;
	//	});
	//	benchmark_op("immer count ", to_count, [&](uint64_t sample)->size_t{ return immer_set.count(sample); });
	//	benchmark_op("immer erase ", to_count, [&](uint64_t sample)->size_t{
	//		size_t was_size = immer_set.size();
	//		immer_set = immer_set.erase(sample);
	//		return was_size - immer_set.size();
	//	});
	//    const auto v0 = immer::vector<int>{};
	//    const auto v1 = v0.push_back(13);
	//    assert(v0.size() == 0 && v1.size() == 1 && v1[0] == 13);
	//
	//    const auto v2 = v1.set(0, 42);
	//    assert(v1[0] == 13 && v2[0] == 42);
}

template<typename T>
struct BucketsGetter {
	static size_t bucket_count(const T &) { return 0; }
};

template<>
struct BucketsGetter<std::unordered_map<std::string, size_t>> {
	static size_t bucket_count(const std::unordered_map<std::string, size_t> &v) { return v.bucket_count(); }
};

constexpr size_t COUNT = 1000000;

template<typename K, typename T, size_t max_key>
class ArrayAdapter {  // Array large enough to index by K
public:
	void emplace(K k, T t) {
		std::pair<T, bool> &pa = storage.at(k);
		if (pa.second)
			return;
		storage_size += 1;
		pa = std::make_pair(t, true);
	}
	size_t size() const { return 1000; }
	size_t count(K k) const { return storage.at(k).second ? 1 : 0; }

private:
	size_t storage_size = 0;
	std::array<std::pair<T, bool>, max_key + 1> storage;
};

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

std::string string_gen(size_t c) { return std::to_string(c % COUNT) + std::string("SampleSampleSampleSampleSampleSample"); }

int int_gen(size_t c) { return int(c); }

int small_int_gen(size_t c) { return int(c % 256); }

struct OrderId {
	uint64_t arr[4] = {};

	bool operator==(const OrderId &b) const {
		return arr[0] == b.arr[0] && arr[1] == b.arr[1] && arr[2] == b.arr[2] && arr[3] == b.arr[3];
	}
	bool operator!=(const OrderId &b) const { return !operator==(b); }
};

OrderId order_id_gen(size_t c) {
	OrderId id;
	id.arr[0] = 12345678;
	id.arr[1] = 87654321;
	id.arr[2] = c % COUNT;
	id.arr[3] = 88888888;
	return id;
}

template<typename T>
inline void hash_combine(std::size_t &seed, T const &v) {
	seed ^= std::hash<T>()(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
	//    seed ^= std::hash<T>()(v) + 0x9e3779b9 * seed;
}

namespace std {

template<>
struct hash<OrderId> {
	std::size_t operator()(const OrderId &w) const {
		size_t hash = 0;
		hash_combine(hash, w.arr[0]);
		hash_combine(hash, w.arr[1]);
		hash_combine(hash, w.arr[2]);
		hash_combine(hash, w.arr[3]);
		return hash;
	}
};

}  // namespace std

int main() {
	benchmark_timers();
	benchmark_sets();
	std::cout << "Testing small std::map<int> count=" << COUNT << std::endl;
	benchmark<int, std::map<int, size_t>>(small_int_gen);
	std::cout << "Testing small std::unordered<int> count=" << COUNT << std::endl;
	benchmark<int, std::unordered_map<int, size_t>>(small_int_gen);
	std::cout << "Testing small ArrayAdapter count=" << COUNT << std::endl;
	benchmark<int, ArrayAdapter<int, size_t, 2000>>(small_int_gen);
	std::cout << "----" << std::endl;

	std::cout << "Testing std::map<std::string> count=" << COUNT << std::endl;
	benchmark<std::string, std::map<std::string, size_t>>(string_gen);
	std::cout << "Testing std::unordered<std::string> count=" << COUNT << std::endl;
	benchmark<std::string, std::unordered_map<std::string, size_t>>(string_gen);
	std::cout << "Testing std::unordered<OrderId> count=" << COUNT << std::endl;
	benchmark<OrderId, std::unordered_map<OrderId, size_t>>(order_id_gen);
	std::cout << "----" << std::endl;

	std::cout << "Testing std::map<int> count=" << COUNT << std::endl;
	benchmark<int, std::map<int, size_t>>(int_gen);
	std::cout << "Testing std::unordered<int> count=" << COUNT << std::endl;
	benchmark<int, std::unordered_map<int, size_t>>(int_gen);
	std::cout << "----" << std::endl;

	return 0;
}
