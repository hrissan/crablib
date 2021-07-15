// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <sstream>
#include "network.hpp"

#if CRAB_IMPL_KEVENT || CRAB_IMPL_EPOLL || CRAB_IMPL_LIBEV

#include <algorithm>
#include <iostream>

#include <arpa/inet.h>
#include <fcntl.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <sys/ptrace.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#if defined(__MACH__)
#include <sys/event.h>
#include <sys/time.h>
#include <sys/types.h>

namespace crab { namespace details {
constexpr int CRAB_MSG_NOSIGNAL = 0;
}}  // namespace crab::details

#endif

#if defined(__linux__)
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>

namespace crab { namespace details {
constexpr int CRAB_MSG_NOSIGNAL = MSG_NOSIGNAL;
}}  // namespace crab::details

#endif

#if CRAB_IMPL_LIBEV
#endif

namespace crab {

namespace details {

CRAB_INLINE void check(bool cond, const char *msg) {
	if (!cond) {
		//	    if (errno == EADDRINUSE)
		//            throw std::runtime_error(std::string(msg) + " errno= Address In Use");
		throw std::runtime_error(std::string(msg) + " errno=" + std::to_string(errno) + ", " + strerror(errno));
	}
}

CRAB_INLINE FileDescriptor::FileDescriptor(int value, const char *throw_if_invalid_message) : value(value) {
	check(is_valid(), throw_if_invalid_message);
}

CRAB_INLINE void FileDescriptor::reset(int new_value) {
	if (is_valid())
		close(value);
	value = new_value;
}

constexpr int MAX_EVENTS = 512;

CRAB_INLINE void setsockopt_int(int fd, int level, int optname, int value) {
	check(setsockopt(fd, level, optname, &value, sizeof(value)) >= 0, "crab::setsockopt failed");
}

CRAB_INLINE void set_nonblocking(int fd) {
	int flags = fcntl(fd, F_GETFL, 0);
	check(flags >= 0, "crab::set_nonblocking get flags failed");
	flags |= O_NONBLOCK;
	check(fcntl(fd, F_SETFL, flags) >= 0, "crab::set_nonblocking set flags failed");
}

CRAB_INLINE ip_mreqn fill_ip_mreqn(const std::string &adapter) {
	ip_mreqn mreq{};
	mreq.imr_address.s_addr = htonl(INADDR_ANY);
	if (adapter.empty())
		return mreq;  // Default adapter
	mreq.imr_ifindex = static_cast<int>(if_nametoindex(adapter.c_str()));
	if (mreq.imr_ifindex != 0)
		return mreq;  // By network adapter name
	Address adapter_address;
	if (!Address::parse(adapter_address, adapter, 0))
		throw std::runtime_error(
		    "Multicast Adapter must be specified either by interface name or by interface ip-address");
	if (adapter_address.impl_get_sockaddr()->sa_family != AF_INET)
		throw std::runtime_error("IPv6 multicast not supported yet");
	auto adapter_sa  = reinterpret_cast<const sockaddr_in *>(adapter_address.impl_get_sockaddr());
	mreq.imr_address = adapter_sa->sin_addr;
	return mreq;
}

CRAB_INLINE bool write_datagram(const FileDescriptor & fd, Callable & rw_handler, const uint8_t *data, size_t count, const Address *peer_addr) {
	if (!fd.is_valid() || !rw_handler.can_write)
		return false;
	RunLoop::current()->stats.UDP_SEND_count += 1;
	RunLoop::current()->stats.push_record("sendto", fd.get_value(), int(count));
	auto addr = peer_addr ? peer_addr->impl_get_sockaddr() : 0;
	auto addr_len = peer_addr ? peer_addr->impl_get_sockaddr_length() : 0;
	ssize_t result = ::sendto(fd.get_value(), data, count, details::CRAB_MSG_NOSIGNAL, addr, addr_len);
	RunLoop::current()->stats.push_record("R(sendto)", fd.get_value(), int(result));
	if (result < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
#if CRAB_IMPL_LIBEV
			io_write.start(fd.get_value(), ev::WRITE);
#endif
			rw_handler.can_write = false;
			return false;  // Will fire on_epoll_call in future automatically
		}
		// If no one is listening on the other side, after receiving ICMP report, error 111 is returned on Linux
		// Error may also indicate MTU size of path too small, that will also change
		// We will ignore all errors here, in hope they will disappear soon
		return true;
	}
	RunLoop::current()->stats.UDP_SEND_size += result;
	return true;
}

CRAB_INLINE optional<size_t> read_datagram(const FileDescriptor & fd, Callable & rw_handler, uint8_t *data, size_t count, Address *peer_addr) {
	if (!fd.is_valid() || !rw_handler.can_read)
		return {};
	Address in_addr;
	socklen_t in_len = sizeof(sockaddr_storage);
	RunLoop::current()->stats.UDP_RECV_count += 1;
	RunLoop::current()->stats.push_record("recvfrom", fd.get_value(), int(count));
	// On some Linux system, passing 0 to recvfrom results in EINVAL without reading datagram
	// (while correct behaviour is reading, truncating to 0, returning EMSGSIZE).
	// We protect clients semantic by reading into our own small buffer
	uint8_t workaround_buffer[1];  // Uninitialized
	ssize_t result = recvfrom(fd.get_value(),
							  count ? data : workaround_buffer,
							  count ? count : sizeof(workaround_buffer),
							  details::CRAB_MSG_NOSIGNAL,
							  in_addr.impl_get_sockaddr(),
							  &in_len);
	if (result > static_cast<ssize_t>(count))  // Can only happen when reading into workaround_buffer
		result = count;
	RunLoop::current()->stats.push_record("R(recvfrom)", fd.get_value(), int(result));
	if (result < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
#if CRAB_IMPL_LIBEV
			io_read.start(fd.get_value(), ev::READ);
#endif
			rw_handler.can_read = false;
			return {};  // Will fire on_epoll_call in future automatically
		}
		if (errno != EMSGSIZE) {
			// Sometimes (for example during adding/removing network adapters), errors could be returned on Linux
			// We will ignore all errors here, in hope they will disappear soon
			// TODO - if we get here, we will not be woke up by epoll any more, so
			// we need to classify all errors here, like in TCPAcceptor
			return {};
		}
		// Truncation is not an error, return true so clients continue reading
		result = count;
	}
	if (peer_addr) {
		*peer_addr = in_addr;
	}
	RunLoop::current()->stats.UDP_RECV_size += result;
	return result;
}

}  // namespace details

#if CRAB_IMPL_KEVENT

namespace details {

constexpr int EVFILT_USER_WAKEUP = 111;

}  // namespace details

CRAB_INLINE RunLoop::RunLoop()
    : efd(kqueue(), "crab::RunLoop kqeueu failed"), wake_callable([this]() { links.trigger_called_watchers(); }) {
	if (CurrentLoop::instance)
		throw std::runtime_error("RunLoop::RunLoop Only single RunLoop per thread is allowed");
	struct kevent changes {
		details::EVFILT_USER_WAKEUP, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, nullptr
	};
	details::check(kevent(efd.get_value(), &changes, 1, 0, 0, NULL) >= 0, "crab::RunLoopBase kevent_modify failed");
	CurrentLoop::instance = this;
}

CRAB_INLINE void RunLoop::impl_add_callable_fd(int fd, Callable *callable, bool read, bool write) {
	struct kevent changes[] = {{uintptr_t(fd), EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, callable},
	    {uintptr_t(fd), EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, callable}};
	const int count         = (read ? 1 : 0) + (write ? 1 : 0);
	details::check(kevent(efd.get_value(), changes + (read ? 0 : 1), count, 0, 0, NULL) >= 0,
	    "crab::RunLoop impl_kevent failed");
}

CRAB_INLINE RunLoop::~RunLoop() { CurrentLoop::instance = nullptr; }

CRAB_INLINE void RunLoop::wakeup() {
	struct kevent changeLst {
		details::EVFILT_USER_WAKEUP, EVFILT_USER, 0, NOTE_TRIGGER, 0, &wake_callable
	};
	// TODO - check if EV_ONESHOT should be added
	details::check(kevent(efd.get_value(), &changeLst, 1, 0, 0, NULL) >= 0, "crab::RunLoop::wakeup");
}

CRAB_INLINE void RunLoop::step(int timeout_ms) {
	struct kevent events[details::MAX_EVENTS];
	struct timespec tmout = {timeout_ms / 1000, (timeout_ms % 1000) * 1000 * 1000};
	int n                 = kevent(efd.get_value(), 0, 0, events, details::MAX_EVENTS, &tmout);
	if (n < 0) {
		// We expect only EINTR here
		details::check(errno == EINTR, "RunLoop::step kevent unexpected error");
		return;
	}
	stats.push_record("kevent", efd.get_value(), n);
	stats.EPOLL_count += 1;
	stats.EPOLL_size += n;
	for (int i = 0; i != n; ++i) {
		auto &ev       = events[i];
		Callable *impl = static_cast<Callable *>(ev.udata);
		stats.push_record("  event", ev.data, ev.filter);
		impl->add_pending_callable(ev.filter == EVFILT_READ, ev.filter == EVFILT_WRITE);
	}
}

CRAB_INLINE Signal::Signal(Handler &&cb, const std::vector<int> &ss) : a_handler(std::move(cb)), signals(ss) {
	if (signals.empty()) {
		signals.push_back(SIGINT);
		signals.push_back(SIGTERM);
	}
	for (auto s : this->signals)
		signal(s, SIG_IGN);

	struct kevent changes[] = {
	    {SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, &a_handler}, {SIGTERM, EVFILT_SIGNAL, EV_ADD, 0, 0, &a_handler}};
	details::check(
	    kevent(RunLoop::current()->efd.get_value(), changes, 2, 0, 0, NULL) >= 0, "crab::Signal impl_kevent failed");
}

CRAB_INLINE Signal::~Signal() {
	// We do not remember if signals were enabled
	for (auto s : signals)
		signal(s, SIG_DFL);
}

CRAB_INLINE bool Signal::running_under_debugger() { return false; }

#elif CRAB_IMPL_EPOLL

CRAB_INLINE RunLoop::RunLoop()
    : efd(epoll_create1(0)), wake_fd(eventfd(0, EFD_NONBLOCK)), wake_callable([this]() {
	    eventfd_t value = 0;
	    eventfd_read(wake_fd.get_value(), &value);
	    // TODO - check error

	    links.trigger_called_watchers();
    }) {
	if (CurrentLoop::instance)
		throw std::runtime_error("RunLoop::RunLoop Only single RunLoop per thread is allowed");
	details::check(efd.is_valid(), "crab::RunLoop epoll_create1 failed");
	details::check(wake_fd.is_valid(), "crab::RunLoop eventfd failed");
	impl_add_callable_fd(wake_fd.get_value(), &wake_callable, true, false);
	CurrentLoop::instance = this;
}

CRAB_INLINE RunLoop::~RunLoop() { CurrentLoop::instance = nullptr; }

CRAB_INLINE void RunLoop::impl_add_callable_fd(int fd, Callable *callable, bool read, bool write) {
	const uint32_t events = (read ? EPOLLIN : EPOLLET) | (write ? EPOLLOUT : EPOLLET) | EPOLLET;
	epoll_event event     = {events, {.ptr = callable}};
	details::check(epoll_ctl(efd.get_value(), EPOLL_CTL_ADD, fd, &event) >= 0, "crab::add_epoll_callable failed");
}

CRAB_INLINE void RunLoop::step(int timeout_ms) {
	epoll_event events[details::MAX_EVENTS];
	int n = epoll_wait(efd.get_value(), events, details::MAX_EVENTS, timeout_ms);
	if (n < 0) {
		// We expect only EINTR here
		details::check(errno == EINTR, "RunLoop::step epoll_wait unexpected error");
		return;
	}
	stats.push_record("epoll_wait", efd.get_value(), n);
	stats.EPOLL_count += 1;
	stats.EPOLL_size += n;
	for (int i = 0; i != n; ++i) {
		auto &ev               = events[i];
		auto impl              = static_cast<Callable *>(ev.data.ptr);
		const auto read_events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLRDHUP;
		stats.push_record("  event", ev.data.fd, ev.events);
		// Those events will trigger socket close after recv() returns -1 or 0
		impl->add_pending_callable(ev.events & read_events, ev.events & EPOLLOUT);
	}
}

CRAB_INLINE void RunLoop::wakeup() {
	// Returns error on counter overflow, as we reset counter to 0 on every read, error is extremely unlikely
	details::check(eventfd_write(wake_fd.get_value(), 1) >= 0, "crab::RunLoop wake_fd counter overflow");
}

CRAB_INLINE Signal::Signal(Handler &&cb, const std::vector<int> &ss)
    : a_handler([&, cb]() {  // cb = std::move(cb) is C++14, we will keep C++11 compatibility for some time
	    signalfd_siginfo info{};
	    while (true) {
		    // Several signals can be merged, we read all of them
		    ssize_t bytes = read(fd.get_value(), &info, sizeof(info));
		    if (bytes < 0)
			    break;
		    // Condition (bytes >= sizeof(info) && info.ssi_pid == 0) is true if called from terminal
	    }
	    cb();
    })
    , signals(ss) {
	if (signals.empty()) {
		signals.push_back(SIGINT);
		signals.push_back(SIGTERM);
	}

	sigset_t mask;
	sigemptyset(&mask);
	for (auto s : signals)
		sigaddset(&mask, s);
	details::check(pthread_sigmask(SIG_BLOCK, &mask, nullptr) >= 0, "crab::Signal pthread_sigmask failed");
	fd.reset(signalfd(-1, &mask, 0));
	details::check(fd.get_value() >= 0, "crab::Signal signalfd failed");
	details::set_nonblocking(fd.get_value());
	RunLoop::current()->impl_add_callable_fd(fd.get_value(), &this->a_handler, true, false);
}

CRAB_INLINE Signal::~Signal() {
	// We do not remember if signals were enabled
	sigset_t mask;
	sigemptyset(&mask);
	for (auto s : signals)
		sigaddset(&mask, s);
	if (pthread_sigmask(SIG_UNBLOCK, &mask, nullptr) < 0)
		std::cout << "crab::~Signal restoring pthread_sigmask failed" << std::endl;
}

CRAB_INLINE bool Signal::running_under_debugger() {
	// https://forum.juce.com/t/detecting-if-a-process-is-being-run-under-a-debugger/2098
	static int underDebugger = 2;
	if (underDebugger == 2) {
		if (ptrace(PTRACE_TRACEME, 0, 0, 0) >= 0) {
			underDebugger = 0;
			ptrace(PTRACE_DETACH, 0, 0, 0);
		} else {
			underDebugger = 1;
		}
	}
	return underDebugger != 0;
}

#endif

#if CRAB_IMPL_LIBEV
CRAB_INLINE TCPSocket::TCPSocket(Handler &&cb)
    : rwd_handler(std::move(cb))
    , io_read(RunLoop::current()->get_impl())
    , io_write(RunLoop::current()->get_impl())
    , closed_event([&] { rwd_handler.handler(); }) {
	io_read.set<TCPSocket, &TCPSocket::io_cb_read>(this);
	io_write.set<TCPSocket, &TCPSocket::io_cb_write>(this);
}
#else
CRAB_INLINE TCPSocket::TCPSocket(Handler &&cb) : rwd_handler(std::move(cb)) {}
#endif

CRAB_INLINE TCPSocket::~TCPSocket() { close(); }

CRAB_INLINE void TCPSocket::close() {
#if CRAB_IMPL_LIBEV
	io_read.stop();
	io_write.stop();
	closed_event.cancel();
#endif
	rwd_handler.cancel_callable();
	fd.reset();
}

CRAB_INLINE void TCPSocket::write_shutdown() {
	if (!fd.is_valid() || !rwd_handler.can_write)
		return;
	::shutdown(fd.get_value(), SHUT_WR);
}

CRAB_INLINE bool TCPSocket::is_open() const {
#if CRAB_IMPL_LIBEV
	return fd.is_valid() || closed_event.is_set();
#else
	return fd.is_valid() || rwd_handler.is_pending_callable();
#endif
}

CRAB_INLINE bool TCPSocket::can_write() const { return rwd_handler.can_write; }

#if CRAB_IMPL_LIBEV
CRAB_INLINE void TCPSocket::io_cb_read(ev::io &, int) {
	io_read.stop();
	rwd_handler.can_read = true;
	rwd_handler.handler();
}
CRAB_INLINE void TCPSocket::io_cb_write(ev::io &, int) {
	io_write.stop();
	rwd_handler.can_write = true;
	rwd_handler.handler();
}
#endif

CRAB_INLINE bool TCPSocket::connect(const Address &address, const Settings &settings) {
	close();
	try {
		details::FileDescriptor tmp(::socket(address.impl_get_sockaddr()->sa_family, SOCK_STREAM, IPPROTO_TCP),
		    "crab::connect socket() failed");
#if defined(__MACH__)
		details::setsockopt_int(tmp.get_value(), SOL_SOCKET, SO_NOSIGPIPE, 1);
#endif
		// SO_RCVBUF has effects on window negotiations, so set buffer sizes before connect
		if (settings.sndbuf_size)
			details::setsockopt_int(tmp.get_value(), SOL_SOCKET, SO_SNDBUF, integer_cast<int>(settings.sndbuf_size));
		if (settings.rcvbuf_size)
			details::setsockopt_int(tmp.get_value(), SOL_SOCKET, SO_RCVBUF, integer_cast<int>(settings.rcvbuf_size));
		details::set_nonblocking(tmp.get_value());
		int connect_result =
		    ::connect(tmp.get_value(), address.impl_get_sockaddr(), address.impl_get_sockaddr_length());
		if (connect_result < 0 && errno != EINPROGRESS)
			return false;
		if (!settings.tcp_delay)  // For compatibility, set after connect
			details::setsockopt_int(tmp.get_value(), IPPROTO_TCP, TCP_NODELAY, 1);
#if CRAB_IMPL_LIBEV
		io_read.start(tmp.get_value(), ev::READ);
		io_write.start(tmp.get_value(), ev::WRITE);
#else
		RunLoop::current()->impl_add_callable_fd(tmp.get_value(), &rwd_handler, true, true);
		if (connect_result >= 0) {
			// On some systems if localhost socket is connected right away, no epoll happens
			rwd_handler.add_pending_callable(true, true);
		}
#endif
		fd.swap(tmp);
		return true;
	} catch (const std::exception &) {
		// During network adapter reconfigurations, connect may return strange error
		// Trying in a second usually solves problems
	}
	return false;
}

CRAB_INLINE void TCPSocket::accept(TCPAcceptor &acceptor, Address *accepted_addr) {
	if (!acceptor.accepted_fd.is_valid())
		throw std::logic_error("TCPAcceptor::accept error, forgot if(can_accept())?");
	close();
	if (accepted_addr)
		*accepted_addr = acceptor.accepted_addr;
	acceptor.accepted_addr = Address();
	fd.swap(acceptor.accepted_fd);
#if CRAB_IMPL_LIBEV
	io_read.start(fd.get_value(), ev::READ);
	io_write.start(fd.get_value(), ev::WRITE);
#else
	try {
		RunLoop::current()->impl_add_callable_fd(fd.get_value(), &rwd_handler, true, true);
	} catch (const std::exception &) {
		// We cannot add to epoll/kevent in TCPAcceptor because we do not have
		// TCPSocket yet, so we design accept to always succeeds, but trigger event,
		// so that for use it will appear as socket was immediately disconnected
		fd.reset();
		rwd_handler.add_pending_callable(true, false);
		return;
	}
#endif
}

CRAB_INLINE size_t TCPSocket::read_some(uint8_t *data, size_t count) {
	if (!fd.is_valid() || !rwd_handler.can_read)
		return 0;
	RunLoop::current()->stats.RECV_count += 1;
	RunLoop::current()->stats.push_record("recv", fd.get_value(), int(count));
	// TODO - decide what happens when count is 0
	ssize_t result = ::recv(fd.get_value(), data, count, details::CRAB_MSG_NOSIGNAL);
	RunLoop::current()->stats.push_record("R(recv)", fd.get_value(), int(result));
	if (result == 0) {  // remote closed
		close();
#if CRAB_IMPL_LIBEV
		closed_event.once(0);
#else
		rwd_handler.add_pending_callable(true, false);
#endif
		return 0;
	}
	if (result < 0) {
		if (errno != EAGAIN && errno != EWOULDBLOCK) {  // some REAL error
			close();
#if CRAB_IMPL_LIBEV
			closed_event.once(0);
#else
			rwd_handler.add_pending_callable(true, false);
#endif
			return 0;
		}
		rwd_handler.can_read = false;
#if CRAB_IMPL_LIBEV
		io_read.start(fd.get_value(), ev::READ);
#endif
		return 0;  // Will fire on_epoll_call in future automatically
	}
	RunLoop::current()->stats.RECV_size += result;
	return result;
}

CRAB_INLINE size_t TCPSocket::read_some(uint8_t *val, size_t count, uint8_t *val2, size_t count2) {
	if (!fd.is_valid() || !rwd_handler.can_read)
		return 0;
	RunLoop::current()->stats.RECV_count += 1;
	RunLoop::current()->stats.push_record("recv", fd.get_value(), int(count));
	struct iovec iovec[2];
	auto iovec_count = 0;
	if (count) {
		iovec[iovec_count].iov_base = val;
		iovec[iovec_count].iov_len  = count;
		iovec_count += 1;
	}
	if (count2) {
		iovec[iovec_count].iov_base = val2;
		iovec[iovec_count].iov_len  = count2;
		iovec_count += 1;
	}
	// TODO - decide what happens when iovec_count is 0
	struct msghdr msg {};
	msg.msg_iov    = iovec;
	msg.msg_iovlen = iovec_count;
	ssize_t result = ::recvmsg(fd.get_value(), &msg, details::CRAB_MSG_NOSIGNAL);
	RunLoop::current()->stats.push_record("R(recv)", fd.get_value(), int(result));
	if (result == 0) {  // remote closed
		close();
#if CRAB_IMPL_LIBEV
		closed_event.once(0);
#else
		rwd_handler.add_pending_callable(true, false);
#endif
		return 0;
	}
	if (result < 0) {
		if (errno != EAGAIN && errno != EWOULDBLOCK) {  // some REAL error
			close();
#if CRAB_IMPL_LIBEV
			closed_event.once(0);
#else
			rwd_handler.add_pending_callable(true, false);
#endif
			return 0;
		}
		rwd_handler.can_read = false;
#if CRAB_IMPL_LIBEV
		io_read.start(fd.get_value(), ev::READ);
#endif
		return 0;  // Will fire on_epoll_call in future automatically
	}
	RunLoop::current()->stats.RECV_size += result;
	return result;
}

CRAB_INLINE size_t TCPSocket::write_some(const uint8_t *data, size_t count) {
	if (!fd.is_valid() || !rwd_handler.can_write)
		return 0;
	RunLoop::current()->stats.SEND_count += 1;
	RunLoop::current()->stats.push_record("send", fd.get_value(), int(count));
	ssize_t result = ::send(fd.get_value(), data, count, details::CRAB_MSG_NOSIGNAL);
	RunLoop::current()->stats.push_record("R(send)", fd.get_value(), int(result));
	if (result < 0) {
		if (errno != EAGAIN && errno != EWOULDBLOCK) {  // some REAL error
			close();
#if CRAB_IMPL_LIBEV
			closed_event.once(0);
#else
			rwd_handler.add_pending_callable(true, false);
#endif
			return 0;
		}
		rwd_handler.can_write = false;
#if CRAB_IMPL_LIBEV
		io_write.start(fd.get_value(), ev::WRITE);
#endif
		return 0;  // Will fire on_epoll_call in future automatically
	}
	RunLoop::current()->stats.SEND_size += result;
	return result;
}

CRAB_INLINE size_t TCPSocket::write_some(std::deque<Buffer> &data) {
	if (!fd.is_valid() || !rwd_handler.can_write || data.empty())
		return 0;
	enum { IOVEC_COUNT = 8 };
	struct iovec iovec[IOVEC_COUNT];
	auto iovec_count = 0;
	for (const auto &d : data) {
		if (d.read_count()) {
			iovec[iovec_count].iov_base = const_cast<uint8_t *>(d.read_ptr());  // sendmsg promises not to modify data
			iovec[iovec_count].iov_len  = d.read_count();
			iovec_count += 1;
		}
		if (d.read_count2()) {
			iovec[iovec_count].iov_base =
			    const_cast<uint8_t *>(d.read_ptr2());  // sendmsg promises not to modify data
			iovec[iovec_count].iov_len = d.read_count2();
			iovec_count += 1;
		}
		if (iovec_count == IOVEC_COUNT)
			break;
	}
	struct msghdr msg {};
	msg.msg_iov    = iovec;
	msg.msg_iovlen = iovec_count;
	//	RunLoop::current()->stats.SEND_count += 1;
	//	RunLoop::current()->stats.push_record("send", fd.get_value(), int(count));
	ssize_t result = ::sendmsg(fd.get_value(), &msg, details::CRAB_MSG_NOSIGNAL);
	//	RunLoop::current()->stats.push_record("R(send)", fd.get_value(), int(result));
	if (result < 0) {
		if (errno != EAGAIN && errno != EWOULDBLOCK) {  // some REAL error
			close();
#if CRAB_IMPL_LIBEV
			closed_event.once(0);
#else
			rwd_handler.add_pending_callable(true, false);
#endif
			return 0;
		}
		rwd_handler.can_write = false;
#if CRAB_IMPL_LIBEV
		io_write.start(fd.get_value(), ev::WRITE);
#endif
		return 0;  // Will fire on_epoll_call in future automatically
	}
	RunLoop::current()->stats.SEND_size += result;
	return result;
}

CRAB_INLINE Address TCPSocket::local_address() const {
	Address in_addr;
	socklen_t in_len = sizeof(sockaddr_storage);
	::getsockname(
	    fd.get_value(), in_addr.impl_get_sockaddr(), &in_len);  // Ignore errors, socket can be disconnected, etc.
	return in_addr;
}

CRAB_INLINE Address TCPSocket::remote_address() const {
	Address in_addr;
	socklen_t in_len = sizeof(sockaddr_storage);
	::getpeername(
	    fd.get_value(), in_addr.impl_get_sockaddr(), &in_len);  // Ignore errors, socket can be disconnected, etc.
	return in_addr;
}

#if CRAB_IMPL_LIBEV
CRAB_INLINE void TCPAcceptor::io_cb_read(ev::io &, int) {
	io_read.stop();
	a_handler.can_read = true;
	a_handler.handler();
}
#endif

CRAB_INLINE TCPAcceptor::TCPAcceptor(const Address &address, Handler &&cb, const Settings &settings)
    : a_handler(std::move(cb))
    , fd_limit_timer([&]() { a_handler.handler(); })
#if CRAB_IMPL_LIBEV
    , io_read(RunLoop::current()->get_impl()) {
	io_read.set<TCPAcceptor, &TCPAcceptor::io_cb_read>(this);
#else
{
#endif
	details::FileDescriptor tmp(::socket(address.impl_get_sockaddr()->sa_family, SOCK_STREAM, IPPROTO_TCP),
	    "crab::TCPAcceptor socket() failed");
#if defined(__MACH__)
	details::setsockopt_int(tmp.get_value(), SOL_SOCKET, SO_NOSIGPIPE, 1);
#endif
	if (settings.reuse_addr)
		details::setsockopt_int(tmp.get_value(), SOL_SOCKET, SO_REUSEADDR, 1);
	if (settings.reuse_port)
		details::setsockopt_int(tmp.get_value(), SOL_SOCKET, SO_REUSEPORT, 1);
	// Settings below are inherited by accepted sockets. TODO - check TCP_NODELAY
	if (!settings.tcp_delay)
		details::setsockopt_int(tmp.get_value(), IPPROTO_TCP, TCP_NODELAY, 1);
	if (settings.sndbuf_size)
		details::setsockopt_int(tmp.get_value(), SOL_SOCKET, SO_SNDBUF, integer_cast<int>(settings.sndbuf_size));
	if (settings.rcvbuf_size)
		details::setsockopt_int(tmp.get_value(), SOL_SOCKET, SO_RCVBUF, integer_cast<int>(settings.rcvbuf_size));

	if (::bind(tmp.get_value(), address.impl_get_sockaddr(), address.impl_get_sockaddr_length()) < 0) {
		std::stringstream ss;
		ss << "crab::TCPAcceptor bind failed, errno=" << errno << ", " << strerror(errno)
		   << ", address=" << address.get_address() << ":" << address.get_port();
		throw std::runtime_error(ss.str());
	}
	details::set_nonblocking(tmp.get_value());
	// Specifying 0 as a second param leads to RST to client on some systems when lots of clients rush in.
	details::check(listen(tmp.get_value(), SOMAXCONN) >= 0, "crab::TCPAcceptor listen failed");
#if CRAB_IMPL_LIBEV
	io_read.start(tmp.get_value(), ev::READ);
#else
	RunLoop::current()->impl_add_callable_fd(tmp.get_value(), &a_handler, true, false);
#endif
	fd.swap(tmp);
}

CRAB_INLINE TCPAcceptor::~TCPAcceptor() = default;

CRAB_INLINE bool TCPAcceptor::can_accept() {
	if (accepted_fd.is_valid())
		return true;
	if (!a_handler.can_read)
		return false;
	Address in_addr;
	while (true) {
		try {
			socklen_t in_len = sizeof(sockaddr_storage);
#if defined(__MACH__)
			details::FileDescriptor sd(::accept(fd.get_value(), in_addr.impl_get_sockaddr(), &in_len));
			// On FreeBSD non-blocking flag is inherited automatically - very smart :)
			// Even relatively modern OS X has no accept4 function, so code below cannot be used
#else  // defined(__linux__)
			details::FileDescriptor sd(
			    ::accept4(fd.get_value(), in_addr.impl_get_sockaddr(), &in_len, SOCK_NONBLOCK));
#endif
			if (!sd.is_valid()) {
				if (errno == EAGAIN || errno == EWOULDBLOCK) {
					a_handler.can_read = false;
#if CRAB_IMPL_LIBEV
					io_read.start(fd.get_value(), ev::READ);
#endif
					return false;
				}
				// All real errors are divided into 2 classes - those that remove entry from backlog
				// and those that do not. If we hit any system limit, entry remains in backlog
				// and we must attempt accepting later, when hopefully resources will be freed
				// First we check errors that remove entry from backlog plus EINTR
				if (errno == ECONNABORTED || errno == EPERM || errno == EINTR) {
					continue;  // repeat accept()
				}
				// Then we check errors that definitely leave entry in backlog
				// Printing message is not a security risk, will not be printed more than once/sec
				if (errno == EMFILE || errno == ENFILE || errno == ENOBUFS || errno == ENOMEM || errno == ENOSR) {
					std::cout << "TCPAcceptor accept() call hit system limits, errno=" << errno
					          << ", please increase system limits or set lower limits in user code" << std::endl;
				} else {
					// Kernels can return multitude of different errors during network adapter reconfiguration
					// And for other reasons. Those errors tend to disappear, we wish to resume normal work after
					// that, so we log them and retry
					std::cout << "TCPAcceptor accept() call returns unexpected error, errno=" << errno
					          << ", will retry accept() in 1 one second" << std::endl;
				}
				fd_limit_timer.once(1);
				return false;
			}
#if defined(__MACH__)
			details::setsockopt_int(sd.get_value(), SOL_SOCKET, SO_NOSIGPIPE, 1);  // Surprisingly, not inherited
#endif
			accepted_fd.swap(sd);
			accepted_addr = in_addr;
			return true;
		} catch (const std::exception &) {
			// on error, accept next
			// various error can happen if client is already disconnected at the point of accept
		}
	}
}

CRAB_INLINE bool UDPTransmitter::can_write() const { return rw_handler.can_write; }

CRAB_INLINE void UDPTransmitter::set_multicast_ttl(int ttl) {
	details::check(setsockopt(fd.get_value(), IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) >= 0,
	    "crab::UDPTransmitter::set_multicast_ttl failed");
}

#if CRAB_IMPL_LIBEV
CRAB_INLINE void UDPTransmitter::io_cb_write(ev::io &, int) {
	io_write.stop();
	rw_handler.can_write = true;
	rw_handler.handler();
}
#endif

CRAB_INLINE UDPTransmitter::UDPTransmitter(const Address &address, Handler &&cb, const std::string &adapter)
    : rw_handler(std::move(cb))
#if CRAB_IMPL_LIBEV
    , io_write(RunLoop::current()->get_impl()) {
	io_write.set<UDPTransmitter, &UDPTransmitter::io_cb_write>(this);
#else
{
#endif
	details::FileDescriptor tmp(::socket(address.impl_get_sockaddr()->sa_family, SOCK_DGRAM, IPPROTO_UDP),
	    "crab::UDPTransmitter socket() failed");
	details::set_nonblocking(tmp.get_value());

	if (address.is_multicast()) {
		details::setsockopt_int(tmp.get_value(), SOL_SOCKET, SO_BROADCAST, 1);
		auto mreq = details::fill_ip_mreqn(adapter);
		details::check(setsockopt(tmp.get_value(), IPPROTO_IP, IP_MULTICAST_IF, &mreq, sizeof(mreq)) >= 0,
		    "crab::UDPTransmitter: Failed to select multicast adapter");
		// On multiadapter system, we should select adapter to send multicast to,
		// unlike unicast, where adapter is selected based on routing table.
		// If performance is not important (service discovery, etc), we could loop all adapters in send_datagram
		// But if performance is important, we should have UDPTransmitter per adapter.
		// TODO - add methods to set/get an adapter for multicast sending
	}
	int connect_result = ::connect(tmp.get_value(), address.impl_get_sockaddr(), address.impl_get_sockaddr_length());
	details::check(connect_result >= 0 || errno == EINPROGRESS, "crab::UDPTransmitter connect() failed");
#if CRAB_IMPL_LIBEV
	io_write.start(tmp.get_value(), ev::WRITE);
#else
	RunLoop::current()->impl_add_callable_fd(tmp.get_value(), &rw_handler, true, true);
	if (connect_result >= 0) {
		// On some systems if socket is connected right away, no epoll happens
		rw_handler.add_pending_callable(true, true);
	}
#endif
	fd.swap(tmp);
}

CRAB_INLINE bool UDPTransmitter::write_datagram(const uint8_t *data, size_t count) {
	return details::write_datagram(fd, rw_handler, data, count, 0);
}

CRAB_INLINE optional<size_t> UDPTransmitter::read_datagram(uint8_t *data, size_t count, Address *peer_addr) {
	return details::read_datagram(fd, rw_handler, data, count, peer_addr);
}


#if CRAB_IMPL_LIBEV
CRAB_INLINE void UDPReceiver::io_cb_read(ev::io &, int) {
	io_read.stop();
	r_handler.can_read = true;
	r_handler.handler();
}
#endif

CRAB_INLINE UDPReceiver::UDPReceiver(const Address &address, Handler &&cb, const Settings & settings)
    : rw_handler(std::move(cb))
#if CRAB_IMPL_LIBEV
    , io_read(RunLoop::current()->get_impl()) {
	io_read.set<UDPReceiver, &UDPReceiver::io_cb_read>(this);
#else
{
#endif
	// On Linux & Mac OSX we can bind either to 0.0.0.0, adapter address or multicast group
	// TODO - on some other systems, when using multicast, we must bind exactly to 0.0.0.0,
	// On Linux, at least, when multicast socket is bound to 0.0.0.0 and other process joins another group
	// both will receive packets from wrong groups if ports are the same, which is wrong
	// Discussion:
	// https://stackoverflow.com/questions/10692956/what-does-it-mean-to-bind-a-multicast-udp-socket
	// https://www.reddit.com/r/networking/comments/7nketv/proper_use_of_bind_for_multicast_receive_on_linux/
	details::FileDescriptor tmp(::socket(address.impl_get_sockaddr()->sa_family, SOCK_DGRAM, IPPROTO_UDP),
	    "crab::UDPReceiver socket() failed");
	if (settings.sndbuf_size)
		details::setsockopt_int(tmp.get_value(), SOL_SOCKET, SO_SNDBUF, integer_cast<int>(settings.sndbuf_size));
	if (settings.rcvbuf_size)
		details::setsockopt_int(tmp.get_value(), SOL_SOCKET, SO_RCVBUF, integer_cast<int>(settings.rcvbuf_size));

	if (address.is_multicast()) {
		// TODO - check flag combination on Mac
		details::setsockopt_int(tmp.get_value(), SOL_SOCKET, SO_REUSEADDR, 1);
		details::setsockopt_int(tmp.get_value(), SOL_SOCKET, SO_REUSEPORT, 1);
	}
	details::set_nonblocking(tmp.get_value());

	details::check(::bind(tmp.get_value(), address.impl_get_sockaddr(), address.impl_get_sockaddr_length()) >= 0,
	    "crab::UDPReceiver bind() failed");
	if (address.is_multicast()) {
		if (address.impl_get_sockaddr()->sa_family != AF_INET)
			throw std::runtime_error("IPv6 multicast not supported yet");
		auto sa = reinterpret_cast<const sockaddr_in *>(address.impl_get_sockaddr());
		// TODO - handle IPv6 multicast
		// On Linux, multicast is a bit broken. INADDR_ANY does not mean "any adapter", but "default one"
		// So, to listen to all adapters, we must call setsockopt per adapter.
		// And then listen to changes of adapters list (how?), and call setsockopt on each new adapter.
		// Compare to TCP or UDP unicast, where INADDR_ANY correctly means listening on all adapter.
		// Sadly, same on Mac OSX
		// So, for DDNS-like discovery apps we need UDPReceiverMultiAdapter, which automatically does that
		auto mreq          = details::fill_ip_mreqn(settings.adapter);
		mreq.imr_multiaddr = sa->sin_addr;
		details::check(setsockopt(tmp.get_value(), IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) >= 0,
		    "crab::UDPReceiver: Failed to join multicast group");
	}
#if CRAB_IMPL_LIBEV
	io_read.start(tmp.get_value(), ev::READ);
#else
	RunLoop::current()->impl_add_callable_fd(tmp.get_value(), &rw_handler, true, true);
#endif
	fd.swap(tmp);
}

CRAB_INLINE bool UDPReceiver::write_datagram(const uint8_t *data, size_t count, const Address &peer_addr) {
	return details::write_datagram(fd, rw_handler, data, count, &peer_addr);
}

CRAB_INLINE bool UDPReceiver::can_write() const { return rw_handler.can_write; }

CRAB_INLINE optional<size_t> UDPReceiver::read_datagram(uint8_t *data, size_t count, Address *peer_addr) {
	return details::read_datagram(fd, rw_handler, data, count, peer_addr);
}

CRAB_INLINE size_t UDPReceiver::read_datagrams(DatagramBuffer *buffer, size_t buffer_len) {
	if (!fd.is_valid() || !rw_handler.can_read)
		return {};
	Address in_addr;
	socklen_t in_len = sizeof(sockaddr_storage);
	RunLoop::current()->stats.UDP_RECV_count += 1;
	RunLoop::current()->stats.push_record("recvfrom", fd.get_value(), int(count));
	// On some Linux system, passing 0 to recvfrom results in EINVAL without reading datagram
	// (while correct behaviour is reading, truncating to 0, returning EMSGSIZE).
	// We protect clients semantic by reading into our own small buffer
	uint8_t workaround_buffer[1];  // Uninitialized
	ssize_t result = recvfrom(fd.get_value(),
							  count ? data : workaround_buffer,
							  count ? count : sizeof(workaround_buffer),
							  details::CRAB_MSG_NOSIGNAL,
							  in_addr.impl_get_sockaddr(),
							  &in_len);
	if (result > static_cast<ssize_t>(count))  // Can only happen when reading into workaround_buffer
		result = count;
	RunLoop::current()->stats.push_record("R(recvfrom)", fd.get_value(), int(result));
	if (result < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
#if CRAB_IMPL_LIBEV
			io_read.start(fd.get_value(), ev::READ);
#endif
			rw_handler.can_read = false;
			return {};  // Will fire on_epoll_call in future automatically
		}
		if (errno != EMSGSIZE) {
			// Sometimes (for example during adding/removing network adapters), errors could be returned on Linux
			// We will ignore all errors here, in hope they will disappear soon
			// TODO - if we get here, we will not be woke up by epoll any more, so
			// we need to classify all errors here, like in TCPAcceptor
			return {};
		}
		// Truncation is not an error, return true so clients continue reading
		result = count;
	}
	if (peer_addr) {
		*peer_addr = in_addr;
	}
	RunLoop::current()->stats.UDP_RECV_size += result;
	return result;
}

}  // namespace crab

#endif
