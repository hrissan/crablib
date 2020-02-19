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
#include <thread>
#include <vector>

#include "network_base.hpp"
#include "streams.hpp"

namespace crab {

struct PerformanceRecord {
	std::chrono::steady_clock::time_point tm;
	const char *event_type = nullptr;  // (Only literals, please)
	size_t count           = 0;        // bytes, bytes, events
	PerformanceRecord()    = default;  // brace-initializer does not work with C++11, hence constructors
	PerformanceRecord(std::chrono::steady_clock::time_point tm, const char *event_type, size_t count)
	    : tm(tm), event_type(event_type), count(count) {}
};

struct PerformanceStats {
	std::atomic<size_t> RECV_count{};
	std::atomic<size_t> RECV_size{};
	std::atomic<size_t> SEND_count{};
	std::atomic<size_t> SEND_size{};
	std::atomic<size_t> EPOLL_count{};
	std::atomic<size_t> EPOLL_size{};
	std::atomic<size_t> UDP_RECV_count{};
	std::atomic<size_t> UDP_RECV_size{};
	std::atomic<size_t> UDP_SEND_count{};
	std::atomic<size_t> UDP_SEND_size{};
};

class Timer : private RunLoopCallable {
public:
	explicit Timer(Handler &&a_handler) : a_handler(std::move(a_handler)) {}
	~Timer() { cancel(); }

	void once(float after_seconds);  // cancels previous once first
	bool is_set() const;
	void cancel();

private:
	void on_runloop_call() override { a_handler(); }
	Handler a_handler;

#if CRAB_SOCKET_KEVENT || CRAB_SOCKET_EPOLL || CRAB_SOCKET_WINDOWS
#if CRAB_INTRUSIVE_SET
	IntrusiveNode<Timer> active_timers_node;
#else
	bool set = false;
	friend struct details::LessTimerPtr;
#endif
	std::chrono::steady_clock::time_point fire_time;
	friend struct details::RunLoopLinks;
#else
	std::unique_ptr<TimerImpl> impl;
	friend struct TimerImpl;
#endif
};

#if CRAB_SOCKET_KEVENT || CRAB_SOCKET_EPOLL || CRAB_SOCKET_WINDOWS
#if !CRAB_INTRUSIVE_SET
inline bool details::LessTimerPtr::operator()(Timer *a, Timer *b) const { return a->fire_time < b->fire_time; }
#endif
#endif

class Watcher : private RunLoopCallable {
public:
	explicit Watcher(Handler &&a_handler);
	~Watcher() override { cancel(); }

	void cancel();
	// after cancel no callback is guaranted till next time call() is called
	void call();
	// The only method to be called from other threads when work is ready
private:
	void on_runloop_call() override { a_handler(); }

	RunLoop *loop = nullptr;  // Will need when calling from the other threads
	Handler a_handler;

#if CRAB_SOCKET_KEVENT || CRAB_SOCKET_EPOLL || CRAB_SOCKET_WINDOWS
	IntrusiveNode<Watcher> fired_objects_node;  // protected by runloop mutex
	friend struct details::RunLoopLinks;
#else
	std::unique_ptr<WatcherImpl> impl;
	friend struct WatcherImpl;
#endif
};

class Idle : private RunLoopCallable {
public:
	explicit Idle(Handler &&cb);
	// Active after construction, only handlers of active Idles will run

	void set_active(bool a);
	bool is_active() { return idle_node.in_list(); }

private:
	void on_runloop_call() override;

	IntrusiveNode<Idle> idle_node;
	Handler a_handler;

	friend class RunLoop;
	friend struct details::RunLoopLinks;
};

// socket is not RAII because it can go to disconnected state by external interaction
class TCPSocket : public IStream, public OStream, private RunLoopCallable {
public:
	explicit TCPSocket(Handler &&rw_handler, Handler &&d_handler)
	    : rw_handler(std::move(rw_handler)), d_handler(std::move(d_handler)) {}
	void set_handlers(Handler &&rw_handler, Handler &&d_handler);

	~TCPSocket() override { close(); }
	void close();
	// after close you are guaranteed that no handlers will be called
	bool is_open() const;  // Connecting or connected

	bool connect(const std::string &address, uint16_t port);
	// either returns false or returns true and will call rw_handler or d_handler in future

	void accept(TCPAcceptor &acceptor, std::string *accepted_addr = nullptr);
	// throws if acceptor.can_accept() is false

	size_t read_some(uint8_t *val, size_t count) override;
	// reads 0..count-1, if returns 0 (incoming buffer empty) would
	// fire rw_handler or d_handler in future
	size_t write_some(const uint8_t *val, size_t count) override;
	// writes 0..count-1, if returns 0 (outgoing buffer full) will fire rw_handler or
	// d_handler in future
	void write_shutdown();
	// will fire d_handler only after all sent data is acknowledged or disconnect happens
	// receiving operations perform as usual
private:
	void on_runloop_call() override;

	Handler rw_handler;
	Handler d_handler;

#if CRAB_SOCKET_KEVENT || CRAB_SOCKET_EPOLL
	details::FileDescriptor fd;
#else
	std::unique_ptr<TCPSocketImpl> impl;
	friend struct TCPSocketImpl;
#endif
};

class TCPAcceptor : private RunLoopCallable {
public:
	explicit TCPAcceptor(const std::string &address, uint16_t port, Handler &&a_handler);

	bool can_accept();

private:
	friend class TCPSocket;

	void on_runloop_call() override { a_handler(); }

	Handler a_handler;

#if CRAB_SOCKET_KEVENT || CRAB_SOCKET_EPOLL
	details::FileDescriptor fd;

	// We actually accept in can_accept, so that TCPSocket::accept never fails
	details::FileDescriptor accepted_fd;
	std::string accepted_addr;
#else
	std::unique_ptr<TCPAcceptorImpl> impl;
	friend struct TCPAcceptorImpl;
#endif
};

// Abstracts UDP outgoing buffer with event on buffer space available
class UDPTransmitter : private RunLoopCallable {
public:
	explicit UDPTransmitter(const std::string &address, uint16_t port, Handler &&r_handler);

	size_t write_datagram(const uint8_t *data, size_t count);
	// either returns count (if written into buffer) or 0 (if buffer is full or a error occurs)

private:
	void on_runloop_call() override { r_handler(); }

	Handler r_handler;

#if CRAB_SOCKET_KEVENT || CRAB_SOCKET_EPOLL
	details::FileDescriptor fd;
#else
//  TODO - implement on other platforms
//  std::unique_ptr<UDPTransmitterImpl> impl;
//  friend struct UDPTransmitterImpl;
#endif
};

class UDPReceiver : private RunLoopCallable {
public:
	explicit UDPReceiver(const std::string &address, uint16_t port, Handler &&r_handler);
	// address must be either local adapter address (127.0.0.1, 0.0.0.0) or multicast group address

	static constexpr size_t MAX_DATAGRAM_SIZE = 65536;
	bool read_datagram(uint8_t *data, size_t *size, std::string *peer_addr = nullptr);
	// data must point to buffer of at least MAX_DATAGRAM_SIZE size
	// either returns false (if buffer is empty), or returns true, fills buffer and sets *size
	// cannot return size_t, because datagrams of zero size are valid

private:
	void on_runloop_call() override { r_handler(); }

	Handler r_handler;

#if CRAB_SOCKET_KEVENT || CRAB_SOCKET_EPOLL
	details::FileDescriptor fd;
#else
	//  TODO - implement on other platforms
//  std::unique_ptr<UDPReceiverImpl> impl;
//  friend struct UDPReceiverImpl;
#endif
};

#if CRAB_SOCKET_KEVENT || CRAB_SOCKET_EPOLL || CRAB_SOCKET_WINDOWS

namespace details {
struct RunLoopLinks : private Nocopy {  // Common structure when implementing over low-level interface
#if CRAB_INTRUSIVE_SET
	IntrusiveList<Timer, &Timer::active_timer_node> active_timers;
#else
	std::set<Timer *, details::LessTimerPtr> active_timers;
#endif
	IntrusiveList<RunLoopCallable, &RunLoopCallable::triggered_callables_node> triggered_callables;
	IntrusiveList<Idle, &Idle::idle_node> idle_handlers;
	bool quit = true;

	void add_triggered_callables(RunLoopCallable *callable);
	bool process_timer(const std::chrono::steady_clock::time_point &now, int &timeout_ms);

	// protected queueu, below can be accessed from other threads
	std::mutex mutex;
	IntrusiveList<Watcher, &Watcher::fired_objects_node> fired_objects;

	void call_watcher(Watcher *watcher);  // from other threads
	void cancel_called_watcher(Watcher *watcher);
	void trigger_called_watchers();
};
}  // namespace details
#endif

class RunLoop : private RunLoopCallable {
public:
	RunLoop();
	~RunLoop() override;

	static RunLoop *current() { return CurrentLoop::instance; }

	void run();  // run until cancel
	void cancel();
	// do not call from other threads, use active object in that case

	// Performance monitoring
	void push_record(const char *event_type, size_t count);
	std::vector<PerformanceRecord> pop_records();
	void print_records();

	static const PerformanceStats &get_stats() { return details::StaticHolder<PerformanceStats>::instance; }

	enum { MAX_SLEEP_MS = 60 * 60 * 1000 };  // Arbitrary max poll wait time
private:
	void step(int timeout_ms = MAX_SLEEP_MS);
	void wakeup();
	void on_runloop_call() override;

	friend class Timer;
	friend class Idle;
	friend class TCPSocket;
	friend class TCPAcceptor;
	friend class UDPTransmitter;
	friend class UDPReceiver;
	friend class Watcher;

	friend struct TimerImpl;
	friend struct WatcherImpl;
	friend struct TCPSocketImpl;
	//    friend struct UDPTransmitterImpl;
	//    friend struct UDPReceiverImpl;
	friend struct TCPAcceptorImpl;

	using CurrentLoop = details::StaticHolderTL<RunLoop *>;

#if CRAB_SOCKET_KEVENT || CRAB_SOCKET_EPOLL || CRAB_SOCKET_WINDOWS
	details::RunLoopLinks links;
#endif
#if CRAB_SOCKET_KEVENT || CRAB_SOCKET_EPOLL
	details::FileDescriptor efd;
#if CRAB_SOCKET_EPOLL
	details::FileDescriptor wake_fd;
#endif
#else
	std::unique_ptr<RunLoopImpl> impl;
#endif

	std::vector<PerformanceRecord> performance;
};

class DNSResolver;

// If using DNSResolver, create single DNSWorker instance in your main
class DNSWorker {
public:
	DNSWorker();
	~DNSWorker();
	static std::vector<std::string> sync_resolve(const std::string &fullname, bool ipv4, bool ipv6);

private:
	using StaticWorker = details::StaticHolder<DNSWorker *>;
	//	static DNSWorker *instance;
	friend class DNSResolver;
	bool quit = false;
	std::list<DNSResolver *> work_queue;
	std::mutex dns_mutex;
	std::condition_variable cond;
	std::thread dns_thread;

	void worker_fun();
};

class DNSResolver {
public:
	typedef std::function<void(const std::vector<std::string> &names)> DNS_handler;

	explicit DNSResolver(DNS_handler &&handler);
	~DNSResolver() { cancel(); }

	void resolve(const std::string &full_name, bool ipv4, bool ipv6);  // will call handler once
	void cancel();

	// returns 4 or 16 bytes depending on family, 0 bytes to indicate a error
	static bool parse_ipaddress(const std::string &str, bdata *result);
	static bdata parse_ipaddress(const std::string &str);  // throws on error

	static bool is_multicast(const bdata &data);
	static std::string print_ipaddress(const bdata &data);

private:
	friend class DNSWorker;
	Watcher ab;
	DNS_handler dns_handler;
	void on_handler();

	bool resolving = false;
	// vars below are protected by mutex in worker
	std::string full_name;
	bool ipv4 = false;
	bool ipv6 = false;
	std::vector<std::string> names;
	DNSResolver **executing_request = nullptr;
};

}  // namespace crab
