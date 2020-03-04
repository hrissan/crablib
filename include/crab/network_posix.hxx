// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include "network.hpp"

#if CRAB_SOCKET_KEVENT || CRAB_SOCKET_EPOLL

#include <algorithm>
#include <iostream>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <sys/ptrace.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#if CRAB_SOCKET_KEVENT
#include <sys/event.h>
#include <sys/time.h>
#include <sys/types.h>
#endif

#if CRAB_SOCKET_EPOLL
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>
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

CRAB_INLINE void setsockopt_1(int fd, int level, int optname) {
	int set;
	check(setsockopt(fd, level, optname, &set, sizeof(set)) >= 0, "crab::setsockopt failed");
}

CRAB_INLINE void set_nonblocking(int fd) {
	int flags = fcntl(fd, F_GETFL, 0);
	check(flags >= 0, "crab::set_nonblocking get flags failed");
	flags |= O_NONBLOCK;
	check(fcntl(fd, F_SETFL, flags) >= 0, "crab::set_nonblocking set flags failed");
}

CRAB_INLINE int stop_signals_enable(bool enable) {
	sigset_t mask;
	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGTERM);
	return sigprocmask(enable ? SIG_UNBLOCK : SIG_BLOCK, &mask, nullptr);
}

}  // namespace details

CRAB_INLINE bool SignalStop::running_under_debugger() {
	// Solution from https://forum.juce.com/t/detecting-if-a-process-is-being-run-under-a-debugger/2098
	//	static bool isCheckedAlready = false;
	static bool underDebugger = false;
	//	if (!isCheckedAlready) {
	//		isCheckedAlready = true;
	//		if (ptrace(PTRACE_TRACEME, 0, 0, 0) < 0) {
	//			underDebugger = true;
	//			std::cout << "crab running under debugger" << std::endl;
	//		} else {
	//			ptrace(PTRACE_DETACH, 0, 0, 0);
	//		}
	//	}
	return underDebugger;
}

#if CRAB_SOCKET_KEVENT

namespace details {

constexpr int CRAB_MSG_NOSIGNAL  = 0;
constexpr int EVFILT_USER_WAKEUP = 111;

}  // namespace details

CRAB_INLINE RunLoop::RunLoop()
    : efd(kqueue(), "crab::RunLoop kqeueu failed"), wake_callable([this]() { links.trigger_called_watchers(); }) {
	if (CurrentLoop::instance)
		throw std::runtime_error("RunLoop::RunLoop Only single RunLoop per thread is allowed");
	struct kevent changeLst {
		details::EVFILT_USER_WAKEUP, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, nullptr
	};
	details::check(kevent(efd.get_value(), &changeLst, 1, 0, 0, NULL) >= 0, "crab::RunLoopBase kevent_modify failed");
	CurrentLoop::instance = this;
}

CRAB_INLINE void RunLoop::impl_kevent(struct kevent *changelist, int nchangesnchanges) {
	details::check(
	    kevent(efd.get_value(), changelist, nchangesnchanges, 0, 0, NULL) >= 0, "crab::RunLoop impl_kevent failed");
}

CRAB_INLINE void RunLoop::impl_kevent(int fd, Callable *callable, uint16_t flags, int16_t filter1, int16_t filter2) {
	struct kevent changelist[] = {
	    {uintptr_t(fd), filter1, flags, 0, 0, callable}, {uintptr_t(fd), filter2, flags, 0, 0, callable}};
	impl_kevent(changelist, filter2 == 0 ? 1 : 2);
}

CRAB_INLINE RunLoop::~RunLoop() { CurrentLoop::instance = nullptr; }

CRAB_INLINE void RunLoop::wakeup() {
	struct kevent changeLst {
		details::EVFILT_USER_WAKEUP, EVFILT_USER, 0, NOTE_TRIGGER, 0, &wake_callable
	};
	details::check(kevent(efd.get_value(), &changeLst, 1, 0, 0, NULL) >= 0, "crab::RunLoop::wakeup");
}

CRAB_INLINE void RunLoop::step(int timeout_ms) {
	struct kevent events[details::MAX_EVENTS];
	struct timespec tmout = {timeout_ms / 1000, (timeout_ms % 1000) * 1000 * 1000};
	int n                 = kevent(efd.get_value(), 0, 0, events, details::MAX_EVENTS, &tmout);
	if (n < 0) {  // SIGNAL or error
		if (errno != EINTR)
			std::cout << "RunLoop::step kevent errno=" << errno << std::endl;
		return;
	}
	if (n)
		push_record("kevent", n);
	details::StaticHolder<PerformanceStats>::instance.EPOLL_count += 1;
	details::StaticHolder<PerformanceStats>::instance.EPOLL_size += n;
	for (int i = 0; i != n; ++i) {
		auto &ev       = events[i];
		Callable *impl = static_cast<Callable *>(ev.udata);
		impl->add_pending_callable(ev.filter == EVFILT_READ, ev.filter == EVFILT_WRITE);
	}
}

CRAB_INLINE SignalStop::SignalStop(Handler &&cb) : a_handler(std::move(cb)) {
	signal(SIGINT, SIG_IGN);
	signal(SIGTERM, SIG_IGN);
	//	details::check(details::stop_signals_enable(false) >= 0, "crab::Signal sigprocmask failed");

	struct kevent changeLst[] = {
	    {SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, &a_handler}, {SIGTERM, EVFILT_SIGNAL, EV_ADD, 0, 0, &a_handler}};
	RunLoop::current()->impl_kevent(changeLst, 2);
}

CRAB_INLINE SignalStop::~SignalStop() {
	// We do not remember if signals were enabled
	signal(SIGINT, SIG_DFL);
	signal(SIGTERM, SIG_DFL);
	//	if (details::stop_signals_enable(true) < 0)
	//		std::cout << "crab::Signal sigprocmask failed" << std::endl;
}

#elif CRAB_SOCKET_EPOLL
namespace details {

constexpr int CRAB_MSG_NOSIGNAL = MSG_NOSIGNAL;

}  // namespace details

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
	impl_epoll_ctl(wake_fd.get_value(), &wake_callable, EPOLL_CTL_ADD, EPOLLIN | EPOLLET);
	CurrentLoop::instance = this;
}

CRAB_INLINE RunLoop::~RunLoop() { CurrentLoop::instance = nullptr; }

CRAB_INLINE void RunLoop::impl_epoll_ctl(int fd, Callable *callable, int op, uint32_t events) {
	epoll_event event = {events, {.ptr = callable}};
	details::check(epoll_ctl(efd.get_value(), op, fd, &event) >= 0, "crab::add_epoll_callable failed");
}

CRAB_INLINE void RunLoop::step(int timeout_ms) {
	epoll_event events[details::MAX_EVENTS];
	int n = epoll_wait(efd.get_value(), events, details::MAX_EVENTS, timeout_ms);
	if (n < 0) {
		if (errno != EINTR)
			std::cout << "RunLoop::step epoll_wait errno=" << errno << std::endl;
		return;
	}
	if (n)
		push_record("epoll_wait", n);
	details::StaticHolder<PerformanceStats>::instance.EPOLL_count += 1;
	details::StaticHolder<PerformanceStats>::instance.EPOLL_size += n;
	for (int i = 0; i != n; ++i) {
		auto &ev               = events[i];
		auto impl              = static_cast<Callable *>(ev.data.ptr);
		const auto read_events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLRDHUP;
		// Those events will trigger socket close after recv() returns -1 or 0
		impl->add_pending_callable(ev.events & read_events, ev.events & EPOLLOUT);
	}
}

CRAB_INLINE void RunLoop::wakeup() {
	// Returns error on counter overflow, as we reset counter to 0 on every read, error is extremely unlikely
	details::check(eventfd_write(wake_fd.get_value(), 1) >= 0, "crab::RunLoop wake_fd counter overflow");
}

CRAB_INLINE SignalStop::SignalStop(Handler &&cb)
    : a_handler([&, cb = std::move(cb)]() {
	    signalfd_siginfo info{};
	    while (true) {
		    // Several signals can be merged, we read all of them
		    ssize_t bytes = read(fd.get_value(), &info, sizeof(info));
		    if (bytes < 0)
			    break;
		    // Condition (bytes >= sizeof(info) && info.ssi_pid == 0) is true if called from terminal
	    }
	    cb();
    }) {
	sigset_t mask;
	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGTERM);
	details::check(sigprocmask(SIG_BLOCK, &mask, nullptr) >= 0, "crab::Signal sigprocmask failed");
	fd.reset(signalfd(-1, &mask, 0));
	details::check(fd.get_value() >= 0, "crab::Signal signalfd failed");
	details::set_nonblocking(fd.get_value());
	RunLoop::current()->impl_epoll_ctl(fd.get_value(), &this->a_handler, EPOLL_CTL_ADD, EPOLLIN | EPOLLET);
}

CRAB_INLINE SignalStop::~SignalStop() {
	// We do not remember if signals were enabled
	if (details::stop_signals_enable(true) < 0)
		std::cout << "crab::Signal sigprocmask failed" << std::endl;
}

#endif

CRAB_INLINE void RunLoop::cancel() { links.quit = true; }

CRAB_INLINE void TCPSocket::close() {
	rwd_handler.cancel_callable();
	fd.reset();
}

CRAB_INLINE void TCPSocket::write_shutdown() {
	if (!fd.is_valid() || !rwd_handler.can_write)
		return;
	::shutdown(fd.get_value(), SHUT_WR);
}

CRAB_INLINE bool TCPSocket::is_open() const { return fd.is_valid() || rwd_handler.is_pending_callable(); }

CRAB_INLINE bool TCPSocket::connect(const Address &address) {
	close();
	try {
		details::FileDescriptor tmp(::socket(address.impl_get_sockaddr()->sa_family, SOCK_STREAM, IPPROTO_TCP),
		    "crab::connect socket() failed");
#if CRAB_SOCKET_KEVENT
		details::setsockopt_1(tmp.get_value(), SOL_SOCKET, SO_NOSIGPIPE);
#endif
		details::set_nonblocking(tmp.get_value());
		int connect_result =
		    ::connect(tmp.get_value(), address.impl_get_sockaddr(), address.impl_get_sockaddr_length());
		if (connect_result < 0 && errno != EINPROGRESS)
			return false;
		details::setsockopt_1(tmp.get_value(), IPPROTO_TCP, TCP_NODELAY);
#if CRAB_SOCKET_KEVENT
		RunLoop::current()->impl_kevent(tmp.get_value(), &rwd_handler, EV_ADD | EV_CLEAR, EVFILT_READ, EVFILT_WRITE);
#else
		RunLoop::current()->impl_epoll_ctl(
		    tmp.get_value(), &rwd_handler, EPOLL_CTL_ADD, EPOLLRDHUP | EPOLLIN | EPOLLOUT | EPOLLET);
#endif
		if (connect_result >= 0) {
			// On some systems if localhost socket is connected right away, no epoll happens
			rwd_handler.add_pending_callable(true, true);
		}
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
	try {
#if CRAB_SOCKET_KEVENT
		RunLoop::current()->impl_kevent(fd.get_value(), &rwd_handler, EV_ADD | EV_CLEAR, EVFILT_READ, EVFILT_WRITE);
#else
		RunLoop::current()->impl_epoll_ctl(
		    fd.get_value(), &rwd_handler, EPOLL_CTL_ADD, EPOLLRDHUP | EPOLLIN | EPOLLOUT | EPOLLET);
#endif
	} catch (const std::exception &) {
		// We cannot add to epoll/kevent in TCPAcceptor because we do not have
		// TCPSocket yet, so we design accept to always succeeds, but trigger event,
		// so that for use it will appear as socket was immediately disconnected
		fd.reset();
		rwd_handler.add_pending_callable(true, false);
		return;
	}
}

CRAB_INLINE size_t TCPSocket::read_some(uint8_t *data, size_t count) {
	if (!fd.is_valid() || !rwd_handler.can_read)
		return 0;
	details::StaticHolder<PerformanceStats>::instance.RECV_count += 1;
	RunLoop::current()->push_record("recv", int(count));
	ssize_t result = ::recv(fd.get_value(), data, count, details::CRAB_MSG_NOSIGNAL);
	RunLoop::current()->push_record("R(recv)", int(result));
	if (result == 0) {  // remote closed
		close();
		rwd_handler.add_pending_callable(true, false);
		return 0;
	}
	if (result < 0) {
		if (errno != EAGAIN && errno != EWOULDBLOCK) {  // some REAL error
			close();
			rwd_handler.add_pending_callable(true, false);
			return 0;
		}
		rwd_handler.can_read = false;
		return 0;  // Will fire on_epoll_call in future automatically
	}
	details::StaticHolder<PerformanceStats>::instance.RECV_size += result;
	return result;
}

CRAB_INLINE size_t TCPSocket::write_some(const uint8_t *data, size_t count) {
	if (!fd.is_valid() || !rwd_handler.can_write)
		return 0;
	details::StaticHolder<PerformanceStats>::instance.SEND_count += 1;
	RunLoop::current()->push_record("send", int(count));
	ssize_t result = ::send(fd.get_value(), data, count, details::CRAB_MSG_NOSIGNAL);
	RunLoop::current()->push_record("R(send)", int(result));
	if (result < 0) {
		if (errno != EAGAIN && errno != EWOULDBLOCK) {  // some REAL error
			close();
			rwd_handler.add_pending_callable(true, false);
			return 0;
		}
		rwd_handler.can_write = false;
		return 0;  // Will fire on_epoll_call in future automatically
	}
	details::StaticHolder<PerformanceStats>::instance.SEND_size += result;
	return result;
}

CRAB_INLINE TCPAcceptor::TCPAcceptor(const Address &address, Handler &&cb)
    : a_handler(std::move(cb)), fd_limit_timer([&]() { a_handler.handler(); }) {
	details::FileDescriptor tmp(::socket(address.impl_get_sockaddr()->sa_family, SOCK_STREAM, IPPROTO_TCP),
	    "crab::TCPAcceptor socket() failed");
#if CRAB_SOCKET_KEVENT
	details::setsockopt_1(tmp.get_value(), SOL_SOCKET, SO_NOSIGPIPE);
#endif
	details::setsockopt_1(tmp.get_value(), SOL_SOCKET, SO_REUSEADDR);

	details::check(::bind(tmp.get_value(), address.impl_get_sockaddr(), address.impl_get_sockaddr_length()) >= 0,
	    "crab::TCPAcceptor bind failed");
	details::set_nonblocking(tmp.get_value());
	details::check(listen(tmp.get_value(), SOMAXCONN) >= 0, "crab::TCPAcceptor listen failed");
#if CRAB_SOCKET_KEVENT
	RunLoop::current()->impl_kevent(tmp.get_value(), &a_handler, EV_ADD | EV_CLEAR, EVFILT_READ);
#else
	RunLoop::current()->impl_epoll_ctl(tmp.get_value(), &a_handler, EPOLL_CTL_ADD, EPOLLIN | EPOLLET);
#endif
	fd.swap(tmp);
}

CRAB_INLINE TCPAcceptor::~TCPAcceptor() = default;

CRAB_INLINE bool TCPAcceptor::can_accept() {
	if (accepted_fd.is_valid())
		return true;
	Address in_addr;
	while (true) {
		try {
			socklen_t in_len = sizeof(sockaddr_storage);
#if CRAB_SOCKET_KEVENT
			details::FileDescriptor sd(::accept(fd.get_value(), in_addr.impl_get_sockaddr(), &in_len));
			// On FreeBSD non-blocking flag is inherited automatically - very smart :)
#else  // CRAB_SOCKET_EPOLL
			details::FileDescriptor sd(
			    ::accept4(fd.get_value(), in_addr.impl_get_sockaddr(), &in_len, SOCK_NONBLOCK));
#endif
			if (!sd.is_valid()) {
				// All errors are divided into 2 classes - those that remove entry from backlog
				// and those that do not. If we hit any system limit, entry remains in backlog
				if (errno == EMFILE || errno == ENFILE || errno == ENOBUFS || errno == ENOMEM) {
					// This message is not a security risk, will not be printed more than once/sec
					std::cout << "TCPAcceptor accept() call hit system limits, errno=" << errno
					          << ", please increase system limits or set lower limits in user code" << std::endl;
					fd_limit_timer.once(1);
				}
				return false;
			}
#if CRAB_SOCKET_KEVENT
			details::setsockopt_1(sd.get_value(), SOL_SOCKET, SO_NOSIGPIPE);
#endif
			details::setsockopt_1(sd.get_value(), IPPROTO_TCP, TCP_NODELAY);
			accepted_fd.swap(sd);
			accepted_addr = in_addr;
			return true;
		} catch (const std::exception &) {
			// on error, accept next
			// various error can happen if client is already disconnected at the point of accept
		}
	}
}

CRAB_INLINE UDPTransmitter::UDPTransmitter(const Address &address, Handler &&cb) : w_handler(std::move(cb)) {
	details::FileDescriptor tmp(::socket(address.impl_get_sockaddr()->sa_family, SOCK_DGRAM, IPPROTO_UDP),
	    "crab::UDPTransmitter socket() failed");
	details::set_nonblocking(tmp.get_value());

	if (address.is_multicast_group()) {
		details::setsockopt_1(tmp.get_value(), SOL_SOCKET, SO_BROADCAST);
		// details::setsockopt_1(tmp.get_value(), IPPROTO_IP, IP_MULTICAST_LOOP);

		// On multiadapter system, we should select adapter to send multicast to,
		// unlike unicast, where adapter is selected based on routing table.
		// If performance is not important (service discovery, etc), we could loop all adapters in send_datagram
		// But if performance is important, we should have UDPTransmitter per adapter.
		// TODO - add methods to set/get an adapter for multicast sending
	}
	int connect_result = ::connect(tmp.get_value(), address.impl_get_sockaddr(), address.impl_get_sockaddr_length());
	details::check(connect_result >= 0 || errno == EINPROGRESS, "crab::UDPTransmitter connect() failed");
#if CRAB_SOCKET_KEVENT
	RunLoop::current()->impl_kevent(tmp.get_value(), &w_handler, EV_ADD | EV_CLEAR, EVFILT_WRITE);
#else
	RunLoop::current()->impl_epoll_ctl(tmp.get_value(), &w_handler, EPOLL_CTL_ADD, EPOLLOUT | EPOLLET);
#endif
	if (connect_result >= 0) {
		// On some systems if socket is connected right away, no epoll happens
		w_handler.add_pending_callable(false, true);
	}
	fd.swap(tmp);
}

CRAB_INLINE size_t UDPTransmitter::write_datagram(const uint8_t *data, size_t count) {
	if (!fd.is_valid() || !w_handler.can_write)
		return 0;
	details::StaticHolder<PerformanceStats>::instance.UDP_SEND_count += 1;
	RunLoop::current()->push_record("sendto", int(count));
	ssize_t result = ::sendto(fd.get_value(), data, count, details::CRAB_MSG_NOSIGNAL, nullptr, 0);
	RunLoop::current()->push_record("R(sendto)", int(result));
	if (result < 0) {
		if (errno != EAGAIN && errno != EWOULDBLOCK) {  // some REAL error
			// If no one is listening on the other side, after receiving ICMP report, error 111 is returned on Linux
			// We will ignore all errors here, in hope they will disappear soon
			return 0;
		}
		w_handler.can_write = false;
		return 0;  // Will fire on_epoll_call in future automatically
	}
	details::StaticHolder<PerformanceStats>::instance.UDP_SEND_size += result;
	return result;
}

CRAB_INLINE UDPReceiver::UDPReceiver(const Address &address, Handler &&cb) : r_handler(std::move(cb)) {
	// On Linux & Mac OSX we can bind either to 0.0.0.0, adapter address or multicast group
	details::FileDescriptor tmp(::socket(address.impl_get_sockaddr()->sa_family, SOCK_DGRAM, IPPROTO_UDP),
	    "crab::UDPReceiver socket() failed");
	if (address.is_multicast_group())
		details::setsockopt_1(tmp.get_value(), SOL_SOCKET, SO_REUSEADDR);
	details::set_nonblocking(tmp.get_value());

	details::check(::bind(tmp.get_value(), address.impl_get_sockaddr(), address.impl_get_sockaddr_length()) >= 0,
	    "crab::UDPReceiver bind() failed");
	if (address.is_multicast_group()) {
		if (address.impl_get_sockaddr()->sa_family != AF_INET)
			throw std::runtime_error("IPv6 multicast not supported yet");
		const sockaddr_in *sa = reinterpret_cast<const sockaddr_in *>(address.impl_get_sockaddr());
		// TODO - handle IPv6 multicast
		// On Linux, multicast is broken. INADDR_ANY does not mean "any adapter", but "default one"
		// So, to listen to all adapters, we must call setsockopt per adapter.
		// And then listen to changes of adapters list (how?), and call setsockopt on each new adapter.
		// Compare to TCP or UDP unicast, where INADDR_ANY correctly means listening on all adapter.
		// Sadly, same on Mac OSX

		// TODO - handle multiple adapters, check adapter configuration changes with simple crab::Timer
		ip_mreq mreq{};
		mreq.imr_multiaddr        = sa->sin_addr;
		mreq.imr_interface.s_addr = htonl(INADDR_ANY);
		details::check(setsockopt(tmp.get_value(), IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) >= 0,
		    "crab::UDPReceiver: Failed to join multicast group");
	}
#if CRAB_SOCKET_KEVENT
	RunLoop::current()->impl_kevent(tmp.get_value(), &r_handler, EV_ADD | EV_CLEAR, EVFILT_READ);
#else
	RunLoop::current()->impl_epoll_ctl(tmp.get_value(), &r_handler, EPOLL_CTL_ADD, EPOLLIN | EPOLLET);
#endif
	fd.swap(tmp);
}

CRAB_INLINE bool UDPReceiver::read_datagram(uint8_t *data, size_t *size, Address *peer_addr) {
	if (!fd.is_valid() || !r_handler.can_read)
		return false;
	Address in_addr;
	socklen_t in_len = sizeof(sockaddr_storage);
	details::StaticHolder<PerformanceStats>::instance.UDP_RECV_count += 1;
	RunLoop::current()->push_record("recvfrom", int(MAX_DATAGRAM_SIZE));
	ssize_t result = recvfrom(
	    fd.get_value(), data, MAX_DATAGRAM_SIZE, details::CRAB_MSG_NOSIGNAL, in_addr.impl_get_sockaddr(), &in_len);
	RunLoop::current()->push_record("R(recvfrom)", int(result));
	if (result < 0) {
		// Sometimes (for example during adding/removing network adapters), errors could be returned on Linux
		// We will ignore all errors here, in hope they will disappear soon
		return false;  // Will fire on_epoll_call in future automatically
	}
	if (peer_addr) {
		*peer_addr = in_addr;
	}
	details::StaticHolder<PerformanceStats>::instance.UDP_RECV_size += result;
	*size = result;
	return true;
}

}  // namespace crab

#endif
