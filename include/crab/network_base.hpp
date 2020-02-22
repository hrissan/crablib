// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include "crab_version.hpp"
#include "intrusive_heap.hpp"
#include "intrusive_list.hpp"
#include "streams.hpp"
#include "util.hpp"

// We use address_storage structucre in crab::Address
#if CRAB_SOCKET_WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#undef ERROR
#undef min
#undef max
#elif CRAB_SOCKET_EPOLL
#include <sys/socket.h>
#elif CRAB_SOCKET_KEVENT
#include <sys/event.h>
#include <sys/socket.h>
#endif

namespace crab {

typedef std::function<void()> Handler;
CRAB_INLINE void empty_handler() {}

class RunLoop;
class Timer;
class Idle;
class Watcher;
class TCPSocket;
class TCPAcceptor;

namespace details {
class DNSWorker;
}

#if CRAB_SOCKET_KEVENT || CRAB_SOCKET_EPOLL || CRAB_SOCKET_WINDOWS

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
struct LessTimerPtr {
	bool operator()(Timer *a, Timer *b) const;
};
struct RunLoopLinks;
}  // namespace details

#else

using Callable = Handler;

#endif

#if CRAB_SOCKET_KEVENT || CRAB_SOCKET_EPOLL

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
}  // namespace details
#elif CRAB_SOCKET_WINDOWS

struct RunLoopImpl;
struct TimerImpl;
struct ActiveObjectImpl;
struct TCPSocketImpl;
struct TCPAcceptorImpl;

#endif

}  // namespace crab
