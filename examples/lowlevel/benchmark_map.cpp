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

		const size_t height = std::min<size_t>(LEVELS, 1 + count_zeroes(random.rnd()) / 3);       // keybuf[0]
		Item *new_item      = (Item *)malloc(sizeof(Item) - (LEVELS - height) * sizeof(Item *));  // new Item{};
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

struct jsw_node {
	size_t weight = 0;
	struct jsw_node *link[2]{};
	uint64_t data = 0;
};

struct jsw_node *make_node(uint64_t data) {
	struct jsw_node *rn = new jsw_node;

	rn->data    = data;
	rn->link[0] = rn->link[1] = NULL;
	rn->weight = 1;

	return rn;
}

size_t jsw_insert2(struct jsw_node **tree, uint64_t data) {
	for (;;) {
		if (*tree == NULL) {
			*tree = make_node(data);
			return 1;
		}
		struct jsw_node *it = *tree;
		if (it->data == data)
			return 0;
		int dir = it->data < data;
		tree    = &it->link[dir];
	}
	// it->link[dir] = make_node(data);
	// return 1;
}

static size_t jsw_found_counter     = 0;
static size_t jsw_not_found_counter = 0;

size_t jsw_find1(struct jsw_node *it, uint64_t data) {
	//	size_t sk = 0;
	while (it != NULL) {
		//		sk += 1;
        if (it->data == data) {
			//			jsw_found_counter += sk;
			return 1;
		}
		int dir = it->data < data;

		it = it->link[dir];
	}
	//    jsw_not_found_counter += sk;
	return 0;
}

std::pair<jsw_node *, size_t> jsw_find(struct jsw_node *it, uint64_t data) {
	struct jsw_node *__result = nullptr;
	size_t depth = 0;
	while (it != nullptr) {
        depth += 1;
		if (it->data >= data) {
			__result = it;
			it       = it->link[0];
		} else
			it = it->link[1];
	}
	if (__result && data >= __result->data) {
        return {__result, depth};
    }
	return {nullptr, 0};
}

size_t jsw_insert(struct jsw_node *it, uint64_t data) {
    struct jsw_node * found = jsw_find(it, data).first;
    if (found)
        return 0;
    for (;;) {
        it->weight += 1;
        int dir = it->data < data;
        if (it->link[dir] == NULL) {
            it->link[dir] = make_node(data);
            return 1;
        }
        it = it->link[dir];
    }
}

size_t jsw_find3(struct jsw_node *it, uint64_t data) {
	while (__builtin_expect(it != NULL, 1)) {
		if (__builtin_expect(data < it->data, 1)) {
			it = it->link[0];
			continue;
		}
		if (__builtin_expect(data > it->data, 1)) {
			it = it->link[1];
			continue;
		}
		return 1;
	}
	return 0;
}

// returns node and pointer to where node is stored in parent
void jsw_pop_front(struct jsw_node **root) {
    auto it = *root;
    while (it->link[0]) {
        it->weight -= 1;
        root = &it->link[0];
        it = it->link[0];
    }
    *root = it->link[1];
    delete it;
}

size_t jsw_remove(struct jsw_node **tree, uint64_t data) {
    struct jsw_node * found = jsw_find(*tree, data).first;
    if (!found)
        return 0;
	struct jsw_node head = {};
	struct jsw_node *it  = &head;
	struct jsw_node *p = *tree, *f = NULL;
	int dir = 1;

	it->link[1] = *tree;

	while (it->link[dir] != NULL) {
		p   = it;
		it  = it->link[dir];
		dir = it->data <= data;
        it->weight -= 1;

		if (it->data == data) {
			f = it;
		}
	}

	f->data                   = it->data;
	p->link[p->link[1] == it] = it->link[it->link[0] == NULL];
	delete it;
//    while (p) {
//        if (p->weight == 0)
//            throw std::logic_error("Weight update wrong");
//        p->weight -= 1;
//        p = p->parent;
//    }
	*tree = head.link[1];
	return 1;
}

struct AVLMinusTree {
	jsw_node * root = nullptr;
public:
	std::pair<int, bool> insert(uint64_t value) {
		if (root == NULL) {
            root = make_node(value);
			return {1, true};
		}
		auto result = jsw_insert(root, value);
		return {1, result > 0};
	}
    size_t count(uint64_t value) const { return jsw_find(root, value).first ? 1 : 0; }
    size_t depth(uint64_t value) const { return jsw_find(root, value).second; }
	size_t erase(uint64_t value) {
	    return jsw_remove(&root, value);
	}
    size_t pop_front() {
	    if (!root)
	        return 0;
	    jsw_pop_front(&root);
	    return 1;
	}
    void print() {
	    if (root) {
            std::cout << "root weight=" << root->weight << std::endl;
            if (root->link[0])
                std::cout << "root->link[0] weight=" << root->link[0]->weight << std::endl;
            if (root->link[1])
                std::cout << "root->link[1] weight=" << root->link[1]->weight << std::endl;
        }
	}
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

template<class Op>
void benchmark_op(const char *str, const std::vector<uint64_t> &samples, Op op) {
	size_t found_counter = 0;
	auto idea_start      = std::chrono::high_resolution_clock::now();
	for (const auto &sample : samples)
		found_counter += op(sample);
	auto idea_ms =
	    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - idea_start);
	std::cout << str << " count=" << samples.size() << " hits=" << found_counter
	          << ", seconds=" << double(idea_ms.count()) / 1000 << std::endl;
}

void benchmark_sets() {
	size_t count                    = 1000000;
	std::vector<uint64_t> to_insert = fill_random(1, count);
	std::vector<uint64_t> to_count  = fill_random(2, count);
	std::vector<uint64_t> to_erase  = fill_random(3, count);

	AVLMinusTree test_avl;
	benchmark_op(
	    "avl-- insert ", to_insert, [&](uint64_t sample) -> size_t { return test_avl.insert(sample).second; });
    benchmark_op("avl-- count ", to_count, [&](uint64_t sample) -> size_t { return test_avl.count(sample); });
    std::map<size_t, size_t> tree_structure;
    benchmark_op("avl-- depth ", to_insert, [&](uint64_t sample) -> size_t { return tree_structure[test_avl.depth(sample)] += 1; });
    size_t sum_depth = 0;
    size_t sum_items = 0;
    for(const auto & s : tree_structure) {
        std::cout << s.first << " -> " << s.second << std::endl;
        sum_depth += s.first * s.second;
        sum_items += s.second;
    }
    std::cout << "Average depth=" << double(sum_depth)/sum_items << std::endl;
    test_avl.print();
    benchmark_op("avl-- erase ", to_erase, [&](uint64_t sample) -> size_t { return test_avl.erase(sample); });
    benchmark_op("avl-- pop_front ", to_insert, [&](uint64_t sample) -> size_t { return test_avl.pop_front(); });

    std::set<uint64_t> test_set;
    benchmark_op(
            "std::set insert ", to_insert, [&](uint64_t sample) -> size_t { return test_set.insert(sample).second; });
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
	benchmark_op(
	    "std::uset insert ", to_insert, [&](uint64_t sample) -> size_t { return test_uset.insert(sample).second; });
	benchmark_op("std::uset count ", to_count, [&](uint64_t sample) -> size_t { return test_uset.count(sample); });
	benchmark_op("std::uset erase ", to_count, [&](uint64_t sample) -> size_t { return test_uset.erase(sample); });

	SkipList<uint64_t> skip_list;
	benchmark_op(
	    "skip_list insert ", to_insert, [&](uint64_t sample) -> size_t { return skip_list.insert(sample).second; });
	benchmark_op("skip_list count ", to_count, [&](uint64_t sample) -> size_t { return skip_list.count(sample); });
	benchmark_op("skip_list erase ", to_count, [&](uint64_t sample) -> size_t { return skip_list.erase(sample); });

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
	benchmark_sets();
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
