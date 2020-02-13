// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

// Approach from
// https://stackoverflow.com/questions/34134886/how-to-implement-an-intrusive-linked-list-that-avoids-undefined-behavior
// is not perfect, because requires reference to list root to unlink nodes
// we use a bit of UB to fix that

#pragma once

#include "util.hpp"

namespace crab {

// Intrusive containers have O(1) complexity without new/delete

template<class T>
class IntrusiveNode : public Nocopy {
public:
	bool in_list() const { return prev != nullptr; }
	void unlink() {
		if (prev) {
			next->prev = prev;
			prev->next = next;
		}
		next = prev = nullptr;
	}
	~IntrusiveNode() { unlink(); }

private:
	IntrusiveNode<T> *next = nullptr;
	IntrusiveNode<T> *prev = nullptr;

	template<typename S, IntrusiveNode<S> S::*>
	friend class IntrusiveList;
	template<typename S, IntrusiveNode<S> S::*>
	friend class IntrusiveIterator;
};

template<typename T, IntrusiveNode<T> T::*Link>
class IntrusiveIterator {
public:
	explicit IntrusiveIterator(IntrusiveNode<T> *current) : current(current) {}
	T &operator*() { return *operator->(); }
	T *operator->() {
		// offsetof is undefined for non-standard layout types.
		alignas(T) char buffer[sizeof(T)];
		IntrusiveNode<T> *link_ptr = &(reinterpret_cast<T *>(buffer)->*Link);
		auto delta                 = reinterpret_cast<char *>(link_ptr) - buffer;
		return reinterpret_cast<T *>(reinterpret_cast<char *>(current->next) - delta);
	}
	bool operator==(IntrusiveIterator const &other) const { return current == other.current; }
	bool operator!=(IntrusiveIterator const &other) const { return !operator==(other); }
	IntrusiveIterator &operator++() {
		current = current->next;
		return *this;
	}
	IntrusiveIterator operator++(int) {
		IntrusiveIterator rc(*this);
		this->operator++();
		return rc;
	}
	IntrusiveIterator &operator--() {
		current = current->prev;
		return *this;
	}
	IntrusiveIterator operator--(int) {
		IntrusiveIterator rc(*this);
		this->operator--();
		return rc;
	}

private:
	template<typename S, IntrusiveNode<S> S::*>
	friend class IntrusiveList;

	IntrusiveNode<T> *current;
};

template<typename T, IntrusiveNode<T> T::*Link>
class IntrusiveList {
public:
	IntrusiveList() {
		content.prev = &content;
		content.next = &content;
	}
	IntrusiveIterator<T, Link> begin() { return IntrusiveIterator<T, Link>(&this->content); }
	IntrusiveIterator<T, Link> end() { return IntrusiveIterator<T, Link>(this->content.prev); }

	T &front() { return *content.next; }
	T &back() { return *content.prev; }
	bool empty() const { return &content == content.prev; }
	void push_back(T &node) { insert(end(), node); }
	void push_front(T &node) { insert(begin(), node); }
	void insert(IntrusiveIterator<T, Link> pos, T &node) {
		IntrusiveNode<T> *other = &(node.*Link);
		if (other->in_list())
			return;
		other->next             = pos.current->next;
		other->prev             = pos.current;
		pos.current->next->prev = other;
		pos.current->next       = other;
	}

private:
	IntrusiveNode<T> content;
};

}  // namespace crab
