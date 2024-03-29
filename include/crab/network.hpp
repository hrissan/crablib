// Copyright (c) 2007-2023, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#pragma once

#include <condition_variable>
#include <iosfwd>
#include <memory>
#include <mutex>
#include <vector>

#include "network_base.hpp"
#include "streams.hpp"

namespace crab {

class Timer {
public:
	explicit Timer(Handler &&cb);
	void set_handler(Handler &&cb) { a_handler = std::move(cb); }
	~Timer();

	// Timer is one-shot, semantic of calling once() on set timer is cancel+once, but it takes only
	// 2 integer comparisons + 1 integer assignment to advance not yet fired timer further into
	// future, so calling once(timeout) on each incoming TCP packet million times per second, while
	// having 100000 timers set is absolutely OK.
	// once(double) is recommended for most uses
	// once(steady_clock::duration) is for performance-critical code paths
	void once(double delay_seconds);
	void once(steady_clock::duration delay);
	void once_at(steady_clock::time_point time_point);

	bool is_set() const;
	void cancel();

private:
	Handler a_handler;

#if CRAB_IMPL_KEVENT || CRAB_IMPL_EPOLL || CRAB_IMPL_WINDOWS
	struct HeapPred {
		bool operator()(const Timer &a, const Timer &b) { return a.fire_time > b.fire_time; }
	};
	IntrusiveHeapIndex heap_index;
	steady_clock::time_point fire_time;  // Part of heap invariant, must not change, while timer is set
	steady_clock::time_point moved_fire_time;
	friend struct details::RunLoopLinks;
#elif CRAB_IMPL_LIBEV
	ev::timer impl;
	void io_cb(ev::timer &, int) { a_handler(); }
#elif CRAB_IMPL_CF
	CFRunLoopTimerRef impl = nullptr;
	static void static_cb(CFRunLoopTimerRef, void *);
#else
	std::unique_ptr<TimerImpl> impl;
	friend struct TimerImpl;
#endif
};

class Watcher {
public:
	explicit Watcher(Handler &&cb);
	void set_handler(Handler &&cb) { a_handler.handler = std::move(cb); }
	~Watcher();

	void cancel();
	// after cancel no callback is guaranteed till next time call() is called
	void call();
	// The only method to be called from other threads when work is ready
private:
	RunLoop *loop = nullptr;  // Will need when calling from the other threads
	Callable a_handler;

#if CRAB_IMPL_KEVENT || CRAB_IMPL_EPOLL || CRAB_IMPL_WINDOWS
	IntrusiveNode<Watcher> fired_objects_node;  // protected by RunLoop mutex
	friend struct details::RunLoopLinks;
#elif CRAB_IMPL_LIBEV
	ev::async impl;
	void io_cb(ev::async &, int) { a_handler.handler(); }
#elif CRAB_IMPL_CF
	CFRunLoopRef loop_loop  = nullptr;
	CFRunLoopSourceRef impl = nullptr;
	static void static_cb(void *);
#else
	std::unique_ptr<WatcherImpl> impl;
	friend struct WatcherImpl;
#endif
};

class Idle {
public:
	explicit Idle(Handler &&cb);
	// Starts in active state, handlers of all active idles are called in fair manner. If
	// several are activated between runloop iterations, last one activated will be last one called
	void set_handler(Handler &&cb) { a_handler = std::move(cb); }
	// Active after construction, only handlers of active Idles will run

	void set_active(bool a);
	bool is_active() const;

private:
	Handler a_handler;
#if CRAB_IMPL_LIBEV
	ev::idle impl;
	void io_cb(ev::idle &, int) { a_handler(); }
#else
	IntrusiveNode<Idle> idle_node;
	friend class RunLoop;
#endif
};

// Handler of POSIX signals, if you need to do something on Ctrl-C

// Very platform-dependent, must be created in main thread before other threads
// are started, due to signal masks being per thread and inherited

// When signals array is empty, SIGINT and SIGTERM are detected on POSIX, while
// on Windows Ctrl-C in terminal is detected, so empty list of signals is sort-of portable

// Does not prevent SIGTRIP (sudden loss of power :) though, beware

// Common approach is call RunLoop::current()->cancel() in handler, and make
// application components destructors to close/flush/commit all held resources.
class Signal {
public:
	explicit Signal(Handler &&cb, std::vector<int> signals = std::vector<int>{});
	void set_handler(Handler &&cb) { a_handler.handler = std::move(cb); }
	~Signal();

	static bool running_under_debugger();
	// Sometimes signals interfere with debugger. Use this fun to conditionally create Signal
private:
	Callable a_handler;
#if CRAB_IMPL_KEVENT || CRAB_IMPL_EPOLL
	std::vector<int> signals;
	details::FileDescriptor fd;
#elif CRAB_IMPL_LIBEV
	// TODO
#elif CRAB_IMPL_CF
	// TODO
#else
	// on Windows we can use https://stackoverflow.com/questions/18291284/handle-ctrlc-on-win32
#endif
};

typedef Signal CRAB_DEPRECATED SignalStop;  // use single-argument constructor of Signal instead

class Address {
public:
	Address() = default;  // unspecified family
	Address(const std::string &ip, uint16_t port);
	explicit Address(const std::string &ip_port);

	static bool parse(Address &address, const std::string &ip, uint16_t port);
	static bool parse(Address &address, const std::string &ip_port);

	std::string get_address() const;
	uint16_t get_port() const;
	std::string to_string() const { return get_address() + ":" + std::to_string(get_port()); }
	bool is_multicast() const;
	bool is_local() const;
	uint32_t get_ip4() const;

#if CRAB_IMPL_KEVENT || CRAB_IMPL_EPOLL || CRAB_IMPL_LIBEV || CRAB_IMPL_WINDOWS || CRAB_IMPL_CF
	const sockaddr *impl_get_sockaddr() const { return reinterpret_cast<const sockaddr *>(&addr); }
	sockaddr *impl_get_sockaddr() { return reinterpret_cast<sockaddr *>(&addr); }
	int impl_get_sockaddr_length() const;

private:
	sockaddr_storage addr = {};
#elif CRAB_IMPL_BOOST
	explicit Address(boost::asio::ip::address &&address, uint16_t port) : addr(std::move(address)), port(port) {}
	const boost::asio::ip::address &get_addr() const { return addr; }

private:
	boost::asio::ip::address addr;
	uint16_t port                 = 0;
#else

#endif
};

std::ostream &operator<<(std::ostream &os, const Address &msg);

namespace details {
// We have to declare settings outside, due to
// https://stackoverflow.com/questions/17430377/error-when-using-in-class-initialization-of-non-static-data-member-and-nested-cl
struct UDPSocketSettings {
	std::string adapter;
	size_t sndbuf_size = 0;  // 0 is do not set
	size_t rcvbuf_size = 0;  // 0 is do not set
};

struct TCPSocketSettings {
	bool tcp_delay     = false;
	size_t sndbuf_size = 0;  // 0 is do not set
	size_t rcvbuf_size = 0;  // 0 is do not set
};

struct TCPAcceptorSettings : public TCPSocketSettings {
	// TCPAcceptor will set TCPSocketSettings for all accepted sockets (through listen socket or manualy)
	bool reuse_addr = false;
	bool reuse_port = false;
};

}  // namespace details

// socket is not RAII because it can go to disconnected state by external interaction
class TCPSocket : public IStream, public OStream {
public:
	using Settings = details::TCPSocketSettings;

	explicit TCPSocket(Handler &&cb);
	// cb is called when read or write is possible or socket closed from other side
	// in your handler, first check for is_open(), if false, socket was closed
	void set_handler(Handler &&cb) { rwd_handler.handler = std::move(cb); }

	~TCPSocket() override;
	void close(bool with_events = false);
	// after close(false), you are guaranteed that no handlers will be called
	// close(true) is for paths with exceptions and errors, will call handler single time in the near future

	bool is_open() const;  // Connecting, connected or closed, but close event not delivered yet

	bool connect(const Address &address, const Settings &settings = Settings{});
	// either returns false or returns true and will call rwd_handler in future
	// We need settings here, because we combine socket() and connect() in one call
	// and at least SO_RCVBUF must be set before connect

	void accept(TCPAcceptor &acceptor, Address *accepted_addr = nullptr);
	// throws if acceptor.can_accept() is false, so check can_accept() before

	size_t read_some(uint8_t *val, size_t count) override;
	// reads 0..count-1, if returns 0 (incoming buffer empty) would
	// fire rwd_handler in future
	using IStream::read_some;  // Version for other char types
	size_t write_some(const uint8_t *val, size_t count) override;
	// writes 0..count-1, if returns 0 (outgoing buffer full) will
	// fire rwd_handler in future
	using OStream::write_some;  // Version for other char types
	bool can_write() const;
	// write_some will return 0 if cannot write, but this is too late for clients
	// who wish to work without buffer and need to prepare data,
	// there is no can_read(), calling read_some is perfect

	void write_shutdown();
	// will disconnect only after all sent data is acknowledged and FIN is acknowledged
	// receiving operations perform as usual

	// write_shutdown is not perfect yet, only implemented to support HTTP/1.0 connection: close
	// there is a catch on processing events after write_shutdown
	// either you do not read, and you never know about received FIN and wait forever
	// or you read() and discard, and client can transfer gigabytes of data and make you wait forever
	// or you close() immediately after shutdown() and get all various behaviours described in
	// https://www.nybek.com/blog/2015/03/05/cross-platform-testing-of-so_linger/

	// Turns out, the contract is if client sets connection: close, it promises to send nothing more
	// so the most portable solution would be to read() on event, and close if any data is received
	// We decided to implement it in http::Connection which has all information needed
	// Implementing in TCPSocket would require tracking state and additional checks for users, who
	// do not use write_shutdown at all

	// TODO - set individual settings after connect/accept

	// Experimental - will be changed in future
	// TODO - we need to switch back to FastData rope or implement this method for all containers
	size_t read_some(uint8_t *val, size_t count, uint8_t *val2, size_t count2);
	size_t write_some(std::deque<Buffer> &data);

	Address local_address() const;
	Address remote_address() const;

private:
	Callable rwd_handler;

#if CRAB_IMPL_KEVENT || CRAB_IMPL_EPOLL || CRAB_IMPL_LIBEV
	details::FileDescriptor fd;
#if CRAB_IMPL_LIBEV
	ev::io io_read;
	ev::io io_write;
	void io_cb_read(ev::io &, int);
	void io_cb_write(ev::io &, int);
	Timer closed_event;
#endif
#elif CRAB_IMPL_CF
	CFReadStreamRef read_stream   = nullptr;
	CFWriteStreamRef write_stream = nullptr;
	bool finish_connect();
	static void read_cb(CFReadStreamRef, CFStreamEventType, void *);
	static void write_cb(CFWriteStreamRef, CFStreamEventType, void *);
	Timer closed_event;
#else
	std::unique_ptr<TCPSocketImpl> impl;
	friend struct TCPSocketImpl;
#endif
};

class TCPAcceptor {
public:
	using Settings = details::TCPAcceptorSettings;
	explicit TCPAcceptor(const Address &address, Handler &&cb, const Settings &settings = Settings{});
	void set_handler(Handler &&cb) { a_handler.handler = std::move(cb); }
	~TCPAcceptor();

	bool can_accept();  // Very fast if nothing to accept

private:
	friend class TCPSocket;  // accept needs access

	Callable a_handler;

#if CRAB_IMPL_KEVENT || CRAB_IMPL_EPOLL || CRAB_IMPL_LIBEV
	details::FileDescriptor fd;

	// We actually accept in can_accept, so that TCPSocket::accept never fails
	details::FileDescriptor accepted_fd;
	Address accepted_addr;

	Timer fd_limit_timer;
	// In Linux/BSD, when accept() returns EMFILE, ENFILE, ENOBUFS, ENOMEM, entry remains
	// in backlog, and fd will not be placed in signalled state again when descriptors are freed.
	// Best approach for us would be to drop() backlog, alas there is no such functions
	// We cannot close/reopen accept fd (due to bunch of possible problems)
	// Also we cannot count file descriptors.
	// So, if the user-code limits (for example in HTTP-server, etc) are not set, and
	// we are out of file descriptors, we will simply set this timer to 1 second, and retry
	// accept.
#if CRAB_IMPL_LIBEV
	ev::io io_read;
	void io_cb_read(ev::io &, int);
#endif
#elif CRAB_IMPL_CF
	CFRunLoopSourceRef socket_source = nullptr;
	std::deque<int> accepted_sockets;
	static void accept_cb(CFSocketRef, CFSocketCallBackType, CFDataRef, const void *data, void *info);
#else
	std::unique_ptr<TCPAcceptorImpl> impl;
	friend struct TCPAcceptorImpl;
#endif
};

// Abstracts UDP outgoing buffer with event on buffer space available
// Also good for client, talking to single server
class UDPTransmitter {
public:
	explicit UDPTransmitter(const Address &address, Handler &&cb, const std::string &adapter = std::string{});
	// If multicast group address is used, receiver will transmit on specified or default adapter
	void set_handler(Handler &&cb) { rw_handler.handler = std::move(cb); }

	bool write_datagram(const uint8_t *data, size_t count);
	// returns false if buffer is full or a error occurs
	// cannot return size_t, because datagrams of zero size are valid
	bool can_write() const;
	// write_datagram will return false if cannot write, but this is too late for clients
	// who wish to work without buffer and need to prepare data,

	// read_datagram will read replies from server, if replies are sent.
	// expect any garbage from any host, as usual with UDP
	optional<size_t> read_datagram(uint8_t *data, size_t count, Address *peer_addr = nullptr);

	void set_multicast_ttl(int ttl);

private:
	Callable rw_handler;

#if CRAB_IMPL_KEVENT || CRAB_IMPL_EPOLL || CRAB_IMPL_LIBEV
	details::FileDescriptor fd;
#if CRAB_IMPL_LIBEV
	ev::io io_read;
	void io_cb_read(ev::io &, int);
	ev::io io_write;
	void io_cb_write(ev::io &, int);
#endif
#elif CRAB_IMPL_CF
#else
//  TODO - implement on other platforms
//  std::unique_ptr<UDPTransmitterImpl> impl;
//  friend struct UDPTransmitterImpl;
#endif
};

class UDPReceiver {
public:
	using Settings = details::UDPSocketSettings;

	explicit UDPReceiver(const Address &address, Handler &&cb, const Settings &settings = Settings{});
	// address must be either local adapter address (127.0.0.1, 0.0.0.0) or multicast group address
	// If multicast group address is used, receiver will join group on specified or default adapter
	void set_handler(Handler &&cb) { rw_handler.handler = std::move(cb); }

	static constexpr size_t MAX_DATAGRAM_SIZE = 65507;  // https://stackoverflow.com/questions/42609561/udp-maximum-packet-size/42610200
	optional<size_t> read_datagram(uint8_t *data, size_t count, Address *peer_addr = nullptr);
	// returns () if buffer is empty
	// returns (datagram_size) if datagram was read, even if it was truncated
	// We do not consider truncation as an error here. Any sane protocol will detect truncated
	// message in its own higher-level logic. We return true so clients state machine will be simpler

	bool write_datagram(const uint8_t *data, size_t count, const Address &peer_addr);
	// for sending replies
	// returns false if buffer is full or a error occurs
	// cannot return size_t, because datagrams of zero size are valid
	bool can_write() const;
	// write_datagram will return false if cannot write, but this is too late for clients
	// who wish to work without buffer and need to prepare data,

	// Experimental API
	struct DatagramBuffer {
		uint8_t data[MAX_DATAGRAM_SIZE];  // Uninitialized, be careful
		size_t count = 0;
		Address peer_addr;
		// TODO - add per datagram adapter information so replies can be sent via correct interfaces
	};
	size_t read_datagrams(DatagramBuffer *buffer, size_t buffer_len);

private:
	Callable rw_handler;

#if CRAB_IMPL_KEVENT || CRAB_IMPL_EPOLL || CRAB_IMPL_LIBEV
	details::FileDescriptor fd;
#if CRAB_IMPL_LIBEV
	ev::io io_read;
	void io_cb_read(ev::io &, int);
	ev::io io_write;
	void io_cb_write(ev::io &, int);
#endif
#elif CRAB_IMPL_CF
#else
	//  TODO - implement on other platforms
//  std::unique_ptr<UDPReceiverImpl> impl;
//  friend struct UDPReceiverImpl;
#endif
};

#if CRAB_IMPL_KEVENT || CRAB_IMPL_EPOLL || CRAB_IMPL_WINDOWS

namespace details {
struct RunLoopLinks : private Nocopy {  // Common structure when implementing over low-level interface
	IntrusiveHeap<Timer, &Timer::heap_index, Timer::HeapPred> active_timers;

	IntrusiveList<Callable, &Callable::triggered_callables_node> triggered_callables;
	steady_clock::time_point now = steady_clock::now();
	std::atomic<bool> quit{false};

	bool process_timer(int &timeout_ms);

	// protected queue below can be accessed in call_watcher from other threads
	std::mutex mutex;
	IntrusiveList<Watcher, &Watcher::fired_objects_node> fired_objects;

	void call_watcher(Watcher *watcher);  // from other threads
	void cancel_called_watcher(Watcher *watcher);
	void trigger_called_watchers();
};
}  // namespace details
#endif

class RunLoop : private Nocopy {
public:
#if CRAB_IMPL_LIBEV
	// TODO - remove after KITTEN is cured
	enum DefaultLoop{};
	explicit RunLoop(DefaultLoop);
#endif
	RunLoop();
	~RunLoop();

	static RunLoop *current() { return CurrentLoop::instance; }

	void run();
	// run until cancel. quit flag is not reset, so subsequent runs will quit immediately. This is to avoid race with cancel.

	void cancel();
	// The only fun allowed to be called from different threads (except Watcher::call)
	// to call it the other thread will need RunLoop pointer, you can use crab::Thread for that

	steady_clock::time_point now() const;
	// will update max 1 per loop iteration. This saves a lot on syscalls, when moving 500
	// timers of tcp socket per iteration under heavy load

	Random rnd;  // Only small penalty of reading from system random device

	PerformanceStats stats;  // User stats can also be recorded here

	enum { MAX_SLEEP_MS = 30 * 60 * 1000 };
	// 30 minutes
	// On some systems, epoll_wait() timeouts greater than 35.79 minutes are treated as infinity.
	// Spurious wakeup once every 30 minutes is harmless, timeout can be reduced further if needed.

#if CRAB_IMPL_KEVENT || CRAB_IMPL_EPOLL
	void impl_add_callable_fd(int fd, Callable *callable, bool read, bool write);
#elif CRAB_IMPL_LIBEV
	ev::loop_ref &get_impl() { return *impl.get(); }
#elif CRAB_IMPL_CF
#else
	RunLoopImpl *get_impl() const { return impl.get(); }
#endif

private:
	void step(int timeout_ms = MAX_SLEEP_MS);
	void wakeup();

	friend class Timer;
	friend class Idle;
	friend struct Callable;
	friend class Watcher;
	friend class Signal;

	using CurrentLoop = details::StaticHolderTL<RunLoop *>;

#if !CRAB_IMPL_LIBEV
	IntrusiveList<Idle, &Idle::idle_node> idle_handlers;  // None of our impls have idles
#endif

#if CRAB_IMPL_KEVENT || CRAB_IMPL_EPOLL || CRAB_IMPL_WINDOWS
	details::RunLoopLinks links;
#endif
#if CRAB_IMPL_KEVENT || CRAB_IMPL_EPOLL
	details::FileDescriptor efd;
#if CRAB_IMPL_EPOLL
	details::FileDescriptor wake_fd;
#endif
	Callable wake_callable;
#elif CRAB_IMPL_CF
	CFRunLoopObserverRef idle_observer = nullptr;
	static void on_idle_observer(CFRunLoopObserverRef, CFRunLoopActivity activity, void *info);
#elif CRAB_IMPL_LIBEV
	// TODO - change back after KITTEN is cured
	std::unique_ptr<ev::loop_ref> impl;
//	ev::dynamic_loop impl;
#else
	std::unique_ptr<RunLoopImpl> impl;
#endif
};

// Experimental, safe creation and cancellation of other threads with RunLoops
// Not movable/copyable so use std::list<Thread> instead of std::vector
class Thread : private Nocopy {
public:
	// forwarding to thread constructor requires strong magic, we use simple lambda for now
	// TODO: https://stackoverflow.com/questions/41757938/pass-parameters-to-stdthread-wrapper
	explicit Thread(std::function<void()> &&fun);  // waits for RunLoop constructor in the started thread
	void cancel();  // It is faster to first cancel a bunch of Threads, then join them one by one in ~Thread()
	~Thread();      // cancels RunLoop, then joins thread
private:
	RunLoop *run_loop_ptr = nullptr;
	bool started          = false;  // We need separate condition, because we have 3 states
	std::mutex mu;
	std::condition_variable cond;
	std::thread th;
};

class DNSResolver {
public:
	typedef std::function<void(const std::vector<Address> &names)> DNS_handler;

	explicit DNSResolver(DNS_handler &&handler);
	~DNSResolver() { cancel(); }

	void resolve(const std::string &host_name, uint16_t port, bool ipv4, bool ipv6);  // will call handler once
	bool is_open() const { return resolving; }
	void cancel();

	static std::vector<Address> sync_resolve(const std::string &host_name, uint16_t port, bool ipv4, bool ipv6);

	static Address sync_resolve_single(const std::string &host_name, uint16_t port);
	// Convenience method - resolve synchronously, prefer IPv4, return first address or throw

private:
	friend class details::DNSWorker;

	Watcher ab;
	DNS_handler dns_handler;
	void on_handler();

	bool resolving = false;
	// vars of resolving NDSResolvers are protected by mutex in worker
	// because worker can access them any time it takes a new job
	std::string host_name;
	uint16_t port = 0;
	bool ipv4     = false;
	bool ipv6     = false;
	std::vector<Address> names;
	IntrusiveNode<DNSResolver> work_queue_node;
};

}  // namespace crab
