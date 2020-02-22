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
#include <sys/socket.h>
#include <unistd.h>

#if CRAB_SOCKET_KEVENT
#include <sys/event.h>
#include <sys/time.h>
#include <sys/types.h>
#endif

#if CRAB_SOCKET_EPOLL
#include <signal.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#endif

namespace crab {

namespace details {

CRAB_INLINE void check(bool cond, const char *msg) {
	if (!cond)
		throw std::runtime_error(msg + std::to_string(errno));
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

}  // namespace details

#if CRAB_SOCKET_KEVENT

namespace details {

constexpr int RECV_SEND_FLAGS    = MSG_DONTWAIT;
constexpr int EVFILT_USER_WAKEUP = 111;

}  // namespace details

CRAB_INLINE RunLoop::RunLoop() : efd(kqueue(), "crab::RunLoop kqeueu failed") {
	if (CurrentLoop::instance)
		throw std::runtime_error("RunLoop::RunLoop Only single RunLoop per thread is allowed");
	//	    signal(SIGINT, SIG_IGN);
	//	    struct kevent changeLst{SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, impl};
	//        kevent_modify(efd.get_value(), &changeLst);
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
		details::EVFILT_USER_WAKEUP, EVFILT_USER, 0, NOTE_TRIGGER, 0, static_cast<Callable *>(this)
	};
	details::check(kevent(efd.get_value(), &changeLst, 1, 0, 0, NULL) >= 0, "crab::RunLoop::wakeup");
}

CRAB_INLINE void RunLoop::on_runloop_call() { links.trigger_called_watchers(); }

CRAB_INLINE void RunLoop::step(int timeout_ms) {
	struct kevent events[details::MAX_EVENTS];
	struct timespec tmout = {timeout_ms / 1000, (timeout_ms % 1000) * 1000 * 1000};
	int n                 = kevent(efd.get_value(), 0, 0, events, details::MAX_EVENTS, &tmout);
	if (n < 0) {  // SIGNAL or error
		n = errno;
		if (n != EINTR)
			std::cout << "RunLoop::step kevent error=" << n << std::endl;
		return;
	}
	if (n)
		push_record("kevent", n);
	details::StaticHolder<PerformanceStats>::instance.EPOLL_count += 1;
	details::StaticHolder<PerformanceStats>::instance.EPOLL_size += n;
	for (int i = 0; i != n; ++i) {
		Callable *impl  = static_cast<Callable *>(events[i].udata);
		impl->can_read  = impl->can_read || (events[i].filter == EVFILT_READ);
		impl->can_write = impl->can_write || (events[i].filter == EVFILT_WRITE);
		links.add_triggered_callables(impl);
	}
}

#elif CRAB_SOCKET_EPOLL
namespace details {

constexpr int RECV_SEND_FLAGS = MSG_DONTWAIT | MSG_NOSIGNAL;
constexpr auto EPOLLIN_TCP    = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLRDHUP | EPOLLOUT;

}  // namespace details

CRAB_INLINE RunLoop::RunLoop() : efd(epoll_create1(0)), wake_fd(eventfd(0, EFD_NONBLOCK)) {
	if (CurrentLoop::instance)
		throw std::runtime_error("RunLoop::RunLoop Only single RunLoop per thread is allowed");
	details::check(efd.is_valid(), "crab::RunLoop epoll_create1 failed");
	details::check(wake_fd.is_valid(), "crab::RunLoop eventfd failed");
	impl_epoll_ctl(wake_fd.get_value(), this, EPOLL_CTL_ADD, EPOLLIN | EPOLLET);
	/*      Experimental code to handle Ctrl~C, code interferes with debugger operations, though
	        TODO - create crab::SignalHandler to process UNIX signals
	        on Windows use https://stackoverflow.com/questions/18291284/handle-ctrlc-on-win32
	        sigset_t mask;
	        sigemptyset(&mask);
	        sigaddset(&mask, SIGINT);
	        if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1)
	            throw std::runtime_error("RunLoop::RunLoop sigprocmask failed");
	        signal_fd.reset( signalfd(-1, &mask, 0) );
	        if (signal_fd.get_value() == -1)
	            throw std::runtime_error("RunLoop::RunLoop signalfd failed");
	        if( !add_epoll_callable(signal_fd.get_value(), EPOLLIN, this))
	            throw std::runtime_error("RunLoop::RunLoop add_epoll_callable signal failed");*/
	CurrentLoop::instance = this;
}

CRAB_INLINE RunLoop::~RunLoop() { CurrentLoop::instance = nullptr; }

CRAB_INLINE void RunLoop::impl_epoll_ctl(int fd, Callable *callable, int op, uint32_t events) {
	epoll_event event = {events, {.ptr = callable}};
	check(epoll_ctl(efd.get_value(), op, fd, &event) >= 0, "crab::add_epoll_callable failed");
}

CRAB_INLINE void RunLoop::on_runloop_call() {
	eventfd_t value = 0;
	eventfd_read(wake_fd.get_value(), &value);
	// TODO - check error

	links.trigger_called_watchers();
	//        struct signalfd_siginfo info;
	//        size_t bytes = read(signal_fd.get_value(), &info, sizeof(info));
	//        if( bytes == sizeof(info) && info.ssi_pid == 0) // From terminal
	//        quit = true;
}

CRAB_INLINE void RunLoop::step(int timeout_ms) {
	epoll_event events[details::MAX_EVENTS];
	int n = epoll_wait(efd.get_value(), events, details::MAX_EVENTS, timeout_ms);
	if (n < 0) {
		n = errno;
		if (n != EINTR)
			std::cout << "RunLoop::step epoll_wait error=" << n << std::endl;
		return;
	}
	if (n)
		push_record("epoll_wait", n);
	details::StaticHolder<PerformanceStats>::instance.EPOLL_count += 1;
	details::StaticHolder<PerformanceStats>::instance.EPOLL_size += n;
	for (int i = 0; i != n; ++i) {
		auto impl       = static_cast<Callable *>(events[i].data.ptr);
		impl->can_read  = impl->can_read || (events[i].events & details::EPOLLIN_PLUS);
		impl->can_write = impl->can_write || (events[i].events & EPOLLOUT);
		links.add_triggered_callables(impl);
	}
}

CRAB_INLINE void RunLoop::wakeup() {
	eventfd_write(wake_fd.get_value(), 1);
	// TODO - check errors here
}

#endif

CRAB_INLINE void RunLoop::cancel() { links.quit = true; }

CRAB_INLINE Address::Address() { addr.ss_family = AF_INET; }

CRAB_INLINE bool Address::parse(Address &address, const std::string &numeric_host, uint16_t port) {
	Address tmp;
	auto ap6 = reinterpret_cast<sockaddr_in6 *>(tmp.impl_get_sockaddr());
	if (inet_pton(AF_INET6, numeric_host.c_str(), &ap6->sin6_addr) == 1) {
		tmp.addr.ss_family = AF_INET6;
		ap6->sin6_port     = htons(port);
		address            = tmp;
		return true;
	}
	auto ap = reinterpret_cast<sockaddr_in *>(tmp.impl_get_sockaddr());
	if (inet_pton(AF_INET, numeric_host.c_str(), &ap->sin_addr) == 1) {
		tmp.addr.ss_family = AF_INET;
		ap->sin_port       = htons(port);
		address            = tmp;
		return true;
	}
	return false;
}

CRAB_INLINE std::string Address::get_address() const {
	char addr_buf[INET6_ADDRSTRLEN] = {};
	switch (addr.ss_family) {
	case AF_INET: {
		auto ap = reinterpret_cast<const sockaddr_in *>(impl_get_sockaddr());
		inet_ntop(AF_INET, &ap->sin_addr, addr_buf, sizeof(addr_buf));
		return addr_buf;
	}
	case AF_INET6: {
		auto ap = reinterpret_cast<const sockaddr_in6 *>(impl_get_sockaddr());
		inet_ntop(AF_INET6, &ap->sin6_addr, addr_buf, sizeof(addr_buf));
		return addr_buf;
	}
	default:
		return "<UnknownFamily" + std::to_string(addr.ss_family) + ">";
	}
}

CRAB_INLINE uint16_t Address::get_port() const {
	switch (addr.ss_family) {
	case AF_INET: {
		auto ap = reinterpret_cast<const sockaddr_in *>(impl_get_sockaddr());
		return ntohs(ap->sin_port);
	}
	case AF_INET6: {
		auto ap = reinterpret_cast<const sockaddr_in6 *>(impl_get_sockaddr());
		return ntohs(ap->sin6_port);
	}
	default:
		return 0;
	}
}

CRAB_INLINE size_t Address::impl_get_sockaddr_length() const {
	switch (addr.ss_family) {
	case AF_INET: {
		return sizeof(sockaddr_in);
	}
	case AF_INET6: {
		return sizeof(sockaddr_in6);
	}
	default:
		return 0;
	}
}

CRAB_INLINE bool Address::is_multicast_group() const {
	switch (addr.ss_family) {
	case AF_INET: {
		auto ap                = reinterpret_cast<const sockaddr_in *>(impl_get_sockaddr());
		const uint8_t highbyte = *reinterpret_cast<const uint8_t *>(&ap->sin_addr);
		return (highbyte & 0xf0U) == 0xe0U;
	}
	case AF_INET6: {
		auto ap                = reinterpret_cast<const sockaddr_in6 *>(impl_get_sockaddr());
		const uint8_t highbyte = *reinterpret_cast<const uint8_t *>(&ap->sin6_addr);
		return highbyte == 0xff;
	}
	default:
		return false;
	}
}

CRAB_INLINE std::vector<Address> DNSResolver::sync_resolve(const std::string &host_name,
    uint16_t port,
    bool ipv4,
    bool ipv6) {
	std::vector<Address> names;
	if (!ipv4 && !ipv6)
		return names;
	addrinfo hints = {};
	struct AddrinfoHolder {
		struct addrinfo *result = nullptr;
		~AddrinfoHolder() { freeaddrinfo(result); }
	} holder;

	hints.ai_family   = (ipv4 && ipv6) ? AF_UNSPEC : ipv4 ? AF_INET : AF_INET6;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags    = AI_V4MAPPED | AI_ADDRCONFIG;  // AI_NUMERICHOST

	auto service = std::to_string(port);

	if (getaddrinfo(host_name.c_str(), service.c_str(), &hints, &holder.result) != 0)
		return names;

	for (struct addrinfo *rp = holder.result; rp != nullptr; rp = rp->ai_next) {
		if (rp->ai_family != AF_INET && rp->ai_family != AF_INET6)
			continue;
		if (rp->ai_addrlen > sizeof(sockaddr_storage))
			continue;
		names.emplace_back();
		std::memcpy(names.back().impl_get_sockaddr(), rp->ai_addr, rp->ai_addrlen);
	}
	return names;
}

CRAB_INLINE void TCPSocket::close() {
	cancel_callable();
	fd.reset();
	can_read  = false;
	can_write = false;
}

CRAB_INLINE void TCPSocket::write_shutdown() {
	if (!fd.is_valid() || !can_write)
		return;
	::shutdown(fd.get_value(), SHUT_WR);
}

CRAB_INLINE bool TCPSocket::is_open() const { return fd.is_valid() || is_pending_callable(); }

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
		RunLoop::current()->impl_kevent(tmp.get_value(), this, EV_ADD | EV_CLEAR, EVFILT_READ, EVFILT_WRITE);
#else
		RunLoop::current()->impl_epoll_ctl(tmp.get_value(), this, EPOLL_CTL_ADD, EPOLLIN_TCP | EPOLLET);
#endif
		if (connect_result >= 0) {
			// On some systems if localhost socket is connected right away, no epoll happens
			RunLoop::current()->links.add_triggered_callables(this);
			can_read = can_write = true;
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
		RunLoop::current()->impl_kevent(fd.get_value(), this, EV_ADD | EV_CLEAR, EVFILT_READ, EVFILT_WRITE);
#else
		RunLoop::current()->impl_epoll_ctl(fd.get_value(), this, EPOLL_CTL_ADD, EPOLLIN_TCP | EPOLLET);
#endif
	} catch (const std::exception &) {
		// We cannot add to epoll/kevent in TCPAcceptor because we do not have
		// TCPSocket yet, so we design accept to always succeeds, but trigger event,
		// so that for use it will appear as socket was immediately disconnected
		fd.reset();
		RunLoop::current()->links.add_triggered_callables(this);
		return;
	}
}

CRAB_INLINE size_t TCPSocket::read_some(uint8_t *data, size_t count) {
	if (!fd.is_valid() || !can_read)
		return 0;
	details::StaticHolder<PerformanceStats>::instance.RECV_count += 1;
	RunLoop::current()->push_record("recv", int(count));
	ssize_t result = ::recv(fd.get_value(), data, count, details::RECV_SEND_FLAGS);
	RunLoop::current()->push_record("R(recv)", int(result));
	if (result != count)
		can_read = false;
	if (result == 0) {  // remote closed
		close();
		RunLoop::current()->links.add_triggered_callables(this);
		return 0;
	}
	if (result < 0) {
		int err = errno;
		if (err != EAGAIN && err != EWOULDBLOCK) {  // some REAL error
			close();
			RunLoop::current()->links.add_triggered_callables(this);
			return 0;
		}
		return 0;  // Will fire on_epoll_call in future automatically
	}
	details::StaticHolder<PerformanceStats>::instance.RECV_size += result;
	return result;
}

CRAB_INLINE size_t TCPSocket::write_some(const uint8_t *data, size_t count) {
	if (!fd.is_valid() || !can_write)
		return 0;
	details::StaticHolder<PerformanceStats>::instance.SEND_count += 1;
	RunLoop::current()->push_record("send", int(count));
	ssize_t result = ::send(fd.get_value(), data, count, details::RECV_SEND_FLAGS);
	RunLoop::current()->push_record("R(send)", int(result));
	if (result != count)
		can_write = false;
	if (result < 0) {
		int err = errno;
		if (err != EAGAIN && err != EWOULDBLOCK) {  // some REAL error
			close();
			RunLoop::current()->links.add_triggered_callables(this);
			return 0;
		}
		return 0;  // Will fire on_epoll_call in future automatically
	}
	details::StaticHolder<PerformanceStats>::instance.SEND_size += result;
	return result;
}

CRAB_INLINE TCPAcceptor::TCPAcceptor(const Address &address, Handler &&a_handler) : a_handler(std::move(a_handler)) {
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
	RunLoop::current()->impl_kevent(tmp.get_value(), this, EV_ADD | EV_CLEAR, EVFILT_READ);
#else
	RunLoop::current()->impl_epoll_ctl(tmp.get_value(), this, EPOLL_CTL_ADD, EPOLLIN | EPOLLET);
#endif
	fd.swap(tmp);
}

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
			if (!sd.is_valid())
				return false;
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

CRAB_INLINE UDPTransmitter::UDPTransmitter(const Address &address, Handler &&r_handler)
    : r_handler(std::move(r_handler)) {
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
	RunLoop::current()->impl_kevent(tmp.get_value(), this, EV_ADD | EV_CLEAR, EVFILT_WRITE);
#else
	RunLoop::current()->impl_epoll_ctl(tmp.get_value(), this, EPOLL_CTL_ADD, EPOLLOUT | EPOLLET);
#endif
	if (connect_result >= 0) {
		// On some systems if socket is connected right away, no epoll happens
		RunLoop::current()->links.add_triggered_callables(this);
		can_write = true;
	}
	fd.swap(tmp);
}

CRAB_INLINE size_t UDPTransmitter::write_datagram(const uint8_t *data, size_t count) {
	if (!fd.is_valid() || !can_write)
		return 0;
	details::StaticHolder<PerformanceStats>::instance.UDP_SEND_count += 1;
	RunLoop::current()->push_record("sendto", int(count));
	ssize_t result = ::sendto(fd.get_value(), data, count, details::RECV_SEND_FLAGS, nullptr, 0);
	RunLoop::current()->push_record("R(sendto)", int(result));
	if (result != count)
		can_write = false;
	if (result < 0) {
		// If no one is listening on the other side, after receiving ICMP report, error 111 is returned on Linux
		// We will ignore all errors here, in hope they will disappear soon
		return 0;  // Will fire on_epoll_call in future automatically
	}
	details::StaticHolder<PerformanceStats>::instance.UDP_SEND_size += result;
	return result;
}

CRAB_INLINE UDPReceiver::UDPReceiver(const Address &address, Handler &&r_handler) : r_handler(std::move(r_handler)) {
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
	RunLoop::current()->impl_kevent(tmp.get_value(), this, EV_ADD | EV_CLEAR, EVFILT_READ);
#else
	RunLoop::current()->impl_epoll_ctl(tmp.get_value(), this, EPOLL_CTL_ADD, EPOLLIN | EPOLLET);
#endif
	fd.swap(tmp);
}

CRAB_INLINE bool UDPReceiver::read_datagram(uint8_t *data, size_t *size, Address *peer_addr) {
	if (!fd.is_valid() || !can_read)
		return false;
	Address in_addr;
	socklen_t in_len = sizeof(sockaddr_storage);
	details::StaticHolder<PerformanceStats>::instance.UDP_RECV_count += 1;
	RunLoop::current()->push_record("recvfrom", int(MAX_DATAGRAM_SIZE));
	ssize_t result = recvfrom(
	    fd.get_value(), data, MAX_DATAGRAM_SIZE, details::RECV_SEND_FLAGS, in_addr.impl_get_sockaddr(), &in_len);
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
