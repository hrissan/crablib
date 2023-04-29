// Copyright (c) 2007-2023, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

// Approach to removing arbitrary element from heap by Jim Mischel
// https://stackoverflow.com/questions/8705099/how-to-delete-in-a-heap-data-structure

#pragma once

#include <algorithm>
#include "util.hpp"

namespace crab {

// Intrusive heap has O(ln2(N)) complexity of pop_front, insert, delete
// and coefficient is small
// for critical latency apps reserve() must be called to avoid reallocation

// Intrusive heap stores index in stored object field
// index == 0 is special value and means "not in a heap"
// Unlike intrusive list, it does not allow unlinking object without reference to heap

// This is max heap (like in std::), so if less is used, front will be the largest element

class IntrusiveHeapIndex : public Nocopy {
public:
	bool in_heap() const { return heap_index != 0; }

private:
	size_t heap_index = 0;  // Protect against accidental modification
	template<typename T, IntrusiveHeapIndex T::*, typename Pred, bool health_checks>
	friend class IntrusiveHeap;
};

template<typename T, IntrusiveHeapIndex T::*Index, typename Pred = std::less<T>, bool health_checks = false>
class IntrusiveHeap {
public:
	void reserve(size_t count) { storage.reserve(count + 1); }
	bool empty() const { return storage.size() == 1; }

	T &front() {
		if (health_checks && (storage.size() <= 1 || (at(1)->*Index).heap_index != 1))
			throw std::logic_error{"Heap Index corrupted at front()"};
		return *at(1);
	}

	bool insert(T &node) {
		if ((node.*Index).heap_index != 0)
			return false;
		storage.push_back(&node);
		move_up(storage.size() - 1);
		check_heap();
		return true;
	}
	size_t erase(T &node) {
		size_t ind = (node.*Index).heap_index;
		if (ind == 0)
			return 0;
		if (health_checks && ind >= storage.size())
			throw std::logic_error{"Heap Index corrupted at erase() 1"};
		if (health_checks && at(ind) != &node)
			throw std::logic_error{"Heap Index corrupted at erase() 2"};
		(node.*Index).heap_index = 0;
		at(ind)                  = storage.back();
		storage.pop_back();
		if (ind < storage.size())
			adjust(ind);
		check_heap();
		return 1;
	}
	void pop_front() {
		size_t ind = (at(1)->*Index).heap_index;
		if (health_checks && ind != 1)
			throw std::logic_error{"Heap Index corrupted at pop_front() 1"};
		if (health_checks && 1 >= storage.size())
			throw std::logic_error{"Heap Index corrupted at pop_front() 2"};
		(at(1)->*Index).heap_index = 0;
		at(1)                      = storage.back();
		storage.pop_back();
		if (1 < storage.size())
			move_down(1);
		check_heap();
	}

private:
	void check_heap() {
		if (!health_checks)
			return;
		if (storage.at(0))
			throw std::logic_error{"Heap Violation 1"};
		if (!std::is_heap(storage.begin() + 1, storage.end(), [](T *a, T *b) -> bool { return Pred{}(*a, *b); }))
			throw std::logic_error{"Heap Violation 2"};
		for (size_t ind = 1; ind < storage.size(); ++ind)
			if ((storage.at(ind)->*Index).heap_index != ind)
				throw std::logic_error{"Heap Violation 3"};
	}
	T *&at(size_t ind) { return health_checks ? storage.at(ind) : storage[ind]; }
	void adjust(size_t ind) {
		if (ind > 1 && !Pred{}(*at(ind), *at(ind / 2)))
			move_up(ind);
		else
			move_down(ind);
	}
	void move_down(size_t ind) {
		const size_t size = storage.size();
		T *data           = at(ind);

		while (true) {
			size_t lc = ind * 2;

			if (lc >= size)
				break;

			if (lc + 1 < size && Pred{}(*at(lc), *at(lc + 1)))
				lc += 1;

			if (Pred{}(*at(lc), *data))
				break;

			at(ind)                      = at(lc);
			(at(ind)->*Index).heap_index = ind;

			ind = lc;
		}

		at(ind)                      = data;
		(at(ind)->*Index).heap_index = ind;
	}
	void move_up(size_t ind) {
		T *data = at(ind);

		while (true) {
			size_t p = ind / 2;

			if (p == 0 || Pred{}(*data, *at(p)))
				break;

			at(ind)                      = at(p);
			(at(ind)->*Index).heap_index = ind;
			ind                          = p;
		}

		at(ind)                      = data;
		(at(ind)->*Index).heap_index = ind;
	}

	std::vector<T *> storage{1, nullptr};
};

}  // namespace crab
