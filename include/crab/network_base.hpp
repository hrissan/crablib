// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "crab_version.hpp"
#include "intrusive_heap.hpp"
#include "intrusive_list.hpp"
#include "streams.hpp"
#include "util.hpp"

// We use address_storage structucre in crab::Address
#if CRAB_IMPL_WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#undef ERROR
#undef min
#undef max
#elif CRAB_IMPL_LIBEV || CRAB_IMPL_EPOLL || CRAB_IMPL_CF
#include <sys/socket.h>
#elif CRAB_IMPL_KEVENT
#include <sys/event.h>
#include <sys/socket.h>
#endif

namespace crab {

// using steady_clock = std::conditional<std::chrono::high_resolution_clock::is_steady,
//        std::chrono::high_resolution_clock,
//        std::chrono::steady_clock>::type;

using steady_clock = std::chrono::steady_clock;

typedef std::function<void()> Handler;
inline void empty_handler() {}

class RunLoop;
class Timer;
class Idle;
class Watcher;
class SignalStop;
class TCPSocket;
class TCPAcceptor;

namespace details {
class DNSWorker;
}

struct PerformanceRecord {
	steady_clock::time_point tm;
	const char *event_type = nullptr;  // Only literals, so recording is very fast
	int fd                 = 0;        // fd or user object identifier
	int count              = 0;        // bytes, events or mask
	PerformanceRecord()    = default;  // brace-initializer does not work with C++11, hence constructors
	PerformanceRecord(steady_clock::time_point tm, const char *event_type, int fd, int count)
	    : tm(tm), event_type(event_type), fd(fd), count(count) {}
};

class PerformanceStats {
public:
	PerformanceStats();

	// If you do not clear records periodically, push_record() becomes very fast NOP
	// after MAX_PERFORMANCE_RECORDS events recorded.
	enum { MAX_PERFORMANCE_RECORDS = 100000 };  // Arbitrary constant

	void push_record(const char *event_type_literal, int fd, int count);  // Pass only literals here
	const std::vector<PerformanceRecord> &get_records() const { return performance; }
	void clear_records() { performance.clear(); }
	void print_records(std::ostream &out);  // Also clears

	size_t RECV_count     = 0;
	size_t RECV_size      = 0;
	size_t SEND_count     = 0;
	size_t SEND_size      = 0;
	size_t EPOLL_count    = 0;
	size_t EPOLL_size     = 0;
	size_t UDP_RECV_count = 0;
	size_t UDP_RECV_size  = 0;
	size_t UDP_SEND_count = 0;
	size_t UDP_SEND_size  = 0;

private:
	std::vector<PerformanceRecord> performance;
};

#if CRAB_IMPL_KEVENT || CRAB_IMPL_EPOLL || CRAB_IMPL_LIBEV || CRAB_IMPL_WINDOWS

struct Callable : private Nocopy {
	explicit Callable(Handler &&handler) : handler(handler) {}
	Handler handler;
	IntrusiveNode<Callable> triggered_callables_node;
	bool can_read  = false;
	bool can_write = false;

	void cancel_callable() {
		triggered_callables_node.unlink();
		can_read  = false;
		can_write = false;
	}
	bool is_pending_callable() const { return triggered_callables_node.in_list(); }
	void add_pending_callable(bool can_read, bool can_write);
};

namespace details {
struct RunLoopLinks;
}  // namespace details

#else

struct Callable : private Nocopy {
	explicit Callable(Handler &&handler) : handler(handler) {}
	Handler handler;
};

#endif

#if CRAB_IMPL_KEVENT || CRAB_IMPL_EPOLL || CRAB_IMPL_LIBEV

namespace details {
class FileDescriptor : private Nocopy {
public:
	explicit FileDescriptor(int value = -1) : value(value) {}
	explicit FileDescriptor(int value, const char *throw_if_invalid_message);
	~FileDescriptor() { reset(); }
	void reset(int new_value = -1);
	int get_value() const { return value; }
	bool is_valid() const { return value >= 0; }
	void swap(FileDescriptor &other) { std::swap(value, other.value); }

private:
	int value;
};

void set_nonblocking(int fd);

}  // namespace details
#elif CRAB_IMPL_CF

namespace details {
// TODO - Ref holder
}

#else  // CRAB_IMPL_WINDOWS, CRAB_IMPL_BOOST

struct RunLoopImpl;
struct TimerImpl;
struct WatcherImpl;
struct ActiveObjectImpl;
struct TCPSocketImpl;
struct TCPAcceptorImpl;

#endif

}  // namespace crab
