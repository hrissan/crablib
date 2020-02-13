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

CRAB_INLINE void FileDescriptor::reset(int new_value) {
	if (is_valid())
		close(value);
	value = new_value;
}

CRAB_INLINE void check(bool cond, const char *msg) {
	if (!cond)
		throw std::runtime_error(msg + std::to_string(errno));
}
constexpr int MAX_EVENTS = 512;
}  // namespace details

#if CRAB_SOCKET_KEVENT

constexpr int EVFILT_USER_WAKEUP = 111;

CRAB_INLINE RunLoop::RunLoop() : efd(kqueue()) {
	details::check(efd.is_valid(), "crab::RunLoop kqeueu failed");
	//	    signal(SIGINT, SIG_IGN);
	//	    struct kevent changeLst{SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, impl};
	//        kevent_modify(efd.get_value(), &changeLst);
	struct kevent changeLst {
		EVFILT_USER_WAKEUP, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, nullptr
	};
	details::check(kevent(efd.get_value(), &changeLst, 1, 0, 0, NULL) >= 0, "crab::RunLoopBase kevent_modify failed");
	if (CurrentLoop::instance)
		throw std::runtime_error("RunLoop::RunLoop Only single RunLoop per thread is allowed");
	CurrentLoop::instance = this;
}

CRAB_INLINE RunLoop::~RunLoop() { CurrentLoop::instance = nullptr; }

CRAB_INLINE void RunLoop::wakeup() {
	struct kevent changeLst {
		EVFILT_USER_WAKEUP, EVFILT_USER, 0, NOTE_TRIGGER, 0, static_cast<RunLoopCallable *>(this)
	};
	details::check(kevent(efd.get_value(), &changeLst, 1, 0, 0, NULL) >= 0, "crab::RunLoop::wakeup");
}

CRAB_INLINE void RunLoop::on_runloop_call() { links.trigger_called_watchers(); }

namespace details {
CRAB_INLINE bool add_rw_socket_callable(const FileDescriptor &efd, const FileDescriptor &fd, RunLoopCallable *impl) {
	// If call fail, socket will be closed and auto cleared from queue
	struct kevent changeLst[] = {{uintptr_t(fd.get_value()), EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, impl},
	    {uintptr_t(fd.get_value()), EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, impl}};
	return kevent(efd.get_value(), changeLst, 2, 0, 0, NULL) >= 0;
}

CRAB_INLINE bool add_la_socket_callable(const FileDescriptor &efd, const FileDescriptor &fd, RunLoopCallable *impl) {
	struct kevent changeLst {
		uintptr_t(fd.get_value()), EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, impl
	};
	return kevent(efd.get_value(), &changeLst, 1, 0, 0, NULL) >= 0;
}
}  // namespace details

CRAB_INLINE void RunLoop::step(int timeout_ms) {
	struct kevent events[details::MAX_EVENTS];
	struct timespec tmout = {timeout_ms / 1000, (timeout_ms % 1000) * 1000 * 1000};
	//	struct timespec tmout = {0, 0}; // Poll-like experiments
	int n = kevent(efd.get_value(), 0, 0, events, details::MAX_EVENTS, &tmout);
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
		RunLoopCallable *impl = static_cast<RunLoopCallable *>(events[i].udata);
		impl->can_read        = impl->can_read || (events[i].filter == EVFILT_READ);
		impl->can_write       = impl->can_write || (events[i].filter == EVFILT_WRITE);
		links.add_triggered_callables(impl);
	}
}

#elif CRAB_SOCKET_EPOLL
namespace details {
CRAB_INLINE bool add_epoll_callable(int efd, int fd, uint32_t events, RunLoopCallable *impl) {
	epoll_event event = {events, {.ptr = impl}};
	return epoll_ctl(efd, EPOLL_CTL_ADD, fd, &event) >= 0;
}
}  // namespace details

CRAB_INLINE RunLoop::RunLoop() : efd(epoll_create1(0)), wake_fd(eventfd(0, EFD_NONBLOCK)) {
	details::check(efd.is_valid(), "crab::RunLoop epoll_create1 failed");
	details::check(wake_fd.is_valid(), "crab::RunLoop eventfd failed");
	if (!details::add_epoll_callable(efd.get_value(), wake_fd.get_value(), EPOLLIN | EPOLLET, this))
		throw std::runtime_error("crab::Watcher::Impl add_epoll_callable failed");
	/*      Expereimental code to handle Ctrl~C, code interferes with debugger operations, though
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
	if (CurrentLoop::instance)
		throw std::runtime_error("RunLoop::RunLoop Only single RunLoop per thread is allowed");
	CurrentLoop::instance = this;
}

CRAB_INLINE RunLoop::~RunLoop() { CurrentLoop::instance = nullptr; }

CRAB_INLINE void RunLoop::on_runloop_call() {
	eventfd_t value = 0;
	eventfd_read(wake_fd.get_value(), &value);

	links.trigger_called_watchers();
	//        struct signalfd_siginfo info;
	//        size_t bytes = read(signal_fd.get_value(), &info, sizeof(info));
	//        if( bytes == sizeof(info) && info.ssi_pid == 0) // From terminal
	//        quit = true;
}

namespace details {
constexpr auto EPOLLIN_PLUS = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLRDHUP;

CRAB_INLINE bool add_rw_socket_callable(const FileDescriptor &efd, const FileDescriptor &fd, RunLoopCallable *impl) {
	return add_epoll_callable(efd.get_value(), fd.get_value(), EPOLLIN_PLUS | EPOLLOUT | EPOLLET, impl);
}

CRAB_INLINE bool add_la_socket_callable(const FileDescriptor &efd, const FileDescriptor &fd, RunLoopCallable *impl) {
	return add_epoll_callable(efd.get_value(), fd.get_value(), EPOLLIN | EPOLLET, impl);
}
}  // namespace details

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
		auto impl       = static_cast<RunLoopCallable *>(events[i].data.ptr);
		impl->can_read  = impl->can_read || (events[i].events & details::EPOLLIN_PLUS);
		impl->can_write = impl->can_write || (events[i].events & EPOLLOUT);
		links.add_triggered_callables(impl);
	}
}

CRAB_INLINE void RunLoop::wakeup() { eventfd_write(wake_fd.get_value(), 1); }

#endif

CRAB_INLINE void RunLoop::cancel() { links.quit = true; }

CRAB_INLINE void TCPSocket::on_runloop_call() {
	if (!fd.is_valid())
		return d_handler();
	try {
		rw_handler();
	} catch (const std::exception &ex) {
		close();
		RunLoop::current()->links.add_triggered_callables(this);
	}
}

namespace details {
CRAB_INLINE bool set_nonblocking(int fd) {
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0)
		return false;
	flags |= O_NONBLOCK;
	return fcntl(fd, F_SETFL, flags) >= 0;
}
CRAB_INLINE std::string good_inet_ntop(const sockaddr *addr) {
	char addr_buf[INET6_ADDRSTRLEN] = {};
	switch (addr->sa_family) {
	case AF_INET: {
		const sockaddr_in *ap = reinterpret_cast<const sockaddr_in *>(addr);
		inet_ntop(AF_INET, &ap->sin_addr, addr_buf, sizeof(addr_buf));
		break;
	}
	case AF_INET6: {
		const sockaddr_in6 *ap = reinterpret_cast<const sockaddr_in6 *>(addr);
		inet_ntop(AF_INET6, &ap->sin6_addr, addr_buf, sizeof(addr_buf));
		break;
	}
	}
	return std::string(addr_buf);
}
}  // namespace details

CRAB_INLINE bdata DNSResolver::parse_ipaddress(const std::string &str) {
	bdata tmp(16, 0);
	if (inet_pton(AF_INET6, str.c_str(), tmp.data()) == 1) {
		return tmp;
	}
	tmp.resize(4);
	if (inet_pton(AF_INET, str.c_str(), tmp.data()) == 1) {
		return tmp;
	}
	return bdata{};
}

CRAB_INLINE std::string DNSResolver::print_ipaddress(const bdata &data) {
	char addr_buf[INET6_ADDRSTRLEN] = {};
	if (data.size() == 4) {
		inet_ntop(AF_INET, data.data(), addr_buf, sizeof(addr_buf));
		return std::string(addr_buf);
	}
	if (data.size() == 16) {
		inet_ntop(AF_INET6, data.data(), addr_buf, sizeof(addr_buf));
		return std::string(addr_buf);
	}
	return data.empty() ? "<Empty Address>" : "<Invalid IP Address Length>";
}

CRAB_INLINE std::vector<std::string> DNSWorker::sync_resolve(const std::string &fullname, bool ipv4, bool ipv6) {
	std::vector<std::string> names;
	if (!ipv4 && !ipv6)
		return names;
	addrinfo hints          = {};
	struct addrinfo *result = nullptr;

	hints.ai_family   = (ipv4 && ipv6) ? AF_UNSPEC : ipv4 ? AF_INET : AF_INET6;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags    = AI_V4MAPPED | AI_ADDRCONFIG;  // AI_NUMERICHOST

	if (getaddrinfo(fullname.c_str(), "80", &hints, &result) != 0)
		return names;

	for (struct addrinfo *rp = result; rp != nullptr; rp = rp->ai_next) {
		names.push_back(details::good_inet_ntop(rp->ai_addr));
	}
	freeaddrinfo(result);
	result = nullptr;
	return names;
}

CRAB_INLINE void TCPSocket::close() {
	cancel_callable();
	fd.reset();
	can_read  = false;
	can_write = false;
}

CRAB_INLINE void TCPSocket::write_shutdown() {
	if (!fd.is_valid())
		return;
	::shutdown(fd.get_value(), SHUT_WR);
}

CRAB_INLINE bool TCPSocket::is_open() const { return fd.is_valid() || is_pending_callable(); }

CRAB_INLINE bool TCPSocket::connect(const std::string &address, uint16_t port) {
	close();

	bdata addrdata = DNSResolver::parse_ipaddress(address);
	int family     = addrdata.size() == 4 ? AF_INET : addrdata.size() == 16 ? AF_INET6 : 0;
	if (family == 0)
		return false;
	details::FileDescriptor temp(socket(family, SOCK_STREAM, IPPROTO_TCP));
	int set = 1;
#if CRAB_SOCKET_KEVENT
	setsockopt(temp.get_value(), SOL_SOCKET, SO_NOSIGPIPE, &set, sizeof(set));
#endif

	if (!temp.is_valid() || !details::set_nonblocking(temp.get_value()))
		return false;
	int connect_result = -1;
	if (family == AF_INET) {
		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_port   = htons(port);
		std::copy(addrdata.begin(), addrdata.end(), reinterpret_cast<uint8_t *>(&addr.sin_addr));
		connect_result = ::connect(temp.get_value(), reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
	} else if (family == AF_INET6) {
		sockaddr_in6 addr{};
		addr.sin6_family = AF_INET6;
		addr.sin6_port   = htons(port);
		std::copy(addrdata.begin(), addrdata.end(), reinterpret_cast<uint8_t *>(&addr.sin6_addr));
		connect_result = ::connect(temp.get_value(), reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
	}
	if (connect_result < 0 && errno != EINPROGRESS)
		return false;
	if (setsockopt(temp.get_value(), IPPROTO_TCP, TCP_NODELAY, &set, sizeof(set)) < 0)
		return false;
	if (!details::add_rw_socket_callable(RunLoop::current()->efd, temp, this))
		return false;
	if (connect_result >= 0) {
		//	 On some systems if socket is connected right away, no epoll happens
		can_read = can_write = true;
		RunLoop::current()->links.add_triggered_callables(this);
	}
	fd.swap(temp);
	return true;
}

namespace details {
#if CRAB_SOCKET_KEVENT
const int RECV_SEND_FLAGS = MSG_DONTWAIT;
#elif CRAB_SOCKET_EPOLL
const int RECV_SEND_FLAGS = MSG_DONTWAIT | MSG_NOSIGNAL;
#endif
}  // namespace details

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

CRAB_INLINE void TCPSocket::accept(TCPAcceptor &acceptor, std::string *accepted_addr) {
	if (!acceptor.accepted_fd.is_valid())
		throw std::logic_error("TCPAcceptor::accept error, forgot if(can_accept())?");
	close();
	if (accepted_addr)
		accepted_addr->swap(acceptor.accepted_addr);
	acceptor.accepted_addr.clear();
	if (!details::add_rw_socket_callable(RunLoop::current()->efd, acceptor.accepted_fd, this)) {
		acceptor.accepted_fd.reset();
		RunLoop::current()->links.add_triggered_callables(this);  // Socket close will be fired
		return;
	}
	fd.swap(acceptor.accepted_fd);
}

CRAB_INLINE TCPAcceptor::TCPAcceptor(const std::string &address, uint16_t port, Handler &&a_handler)
    : a_handler(std::move(a_handler)) {
	bdata addrdata = DNSResolver::parse_ipaddress(address);
	int family     = addrdata.size() == 4 ? AF_INET : addrdata.size() == 16 ? AF_INET6 : 0;
	details::check(family != 0, "crab::TCPAcceptor ip address invalid");
	details::FileDescriptor tmp(socket(family, SOCK_STREAM, IPPROTO_TCP));
	details::check(tmp.is_valid(), "crab::TCPAcceptor socket() failed");
	int set = 1;
#if CRAB_SOCKET_KEVENT
	setsockopt(tmp.get_value(), SOL_SOCKET, SO_NOSIGPIPE, &set, sizeof(set));
#endif
	setsockopt(tmp.get_value(), SOL_SOCKET, SO_REUSEADDR, &set, sizeof(set));
	setsockopt(tmp.get_value(), SOL_SOCKET, SO_REUSEPORT, &set, sizeof(set));

	if (family == AF_INET) {
		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_port   = htons(port);
		std::copy(addrdata.begin(), addrdata.end(), reinterpret_cast<uint8_t *>(&addr.sin_addr));
		details::check(bind(tmp.get_value(), reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0,
		    "crab::TCPAcceptor bind failed");
	} else if (family == AF_INET6) {
		sockaddr_in6 addr{};
		addr.sin6_family = AF_INET6;
		addr.sin6_port   = htons(port);
		std::copy(addrdata.begin(), addrdata.end(), reinterpret_cast<uint8_t *>(&addr.sin6_addr));
		details::check(bind(tmp.get_value(), reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0,
		    "crab::TCPAcceptor bind failed");
	}
	details::check(details::set_nonblocking(tmp.get_value()), "crab::TCPAcceptor fcntl set nonblocking failed");
	details::check(listen(tmp.get_value(), SOMAXCONN) >= 0, "crab::TCPAcceptor listen failed");
	details::check(details::add_la_socket_callable(RunLoop::current()->efd, tmp, this),
	    "crab::TCPAcceptor could not add epoll_ctl");
	fd.swap(tmp);
}

CRAB_INLINE bool TCPAcceptor::can_accept() {
	if (accepted_fd.is_valid())
		return true;
	uint8_t in_addr[sizeof(sockaddr_in)] = {};
	socklen_t in_len                     = sizeof(in_addr);
	const int set                        = 1;
	while (true) {
#if CRAB_SOCKET_KEVENT
		details::FileDescriptor sd(::accept(fd.get_value(), reinterpret_cast<sockaddr *>(&in_addr), &in_len));
		// On FreeBSD non-blocking flag is inherited automatically - very smart :)
		if (!sd.is_valid())
			return false;
		if (setsockopt(sd.get_value(), SOL_SOCKET, SO_NOSIGPIPE, &set, sizeof(set)) < 0)
			continue;  // on error, accept next
#else                  // CRAB_SOCKET_EPOLL
		details::FileDescriptor sd(
		    ::accept4(fd.get_value(), reinterpret_cast<sockaddr *>(&in_addr), &in_len, SOCK_NONBLOCK));
		if (!sd.is_valid())
			return false;
#endif
		if (setsockopt(sd.get_value(), IPPROTO_TCP, TCP_NODELAY, &set, sizeof(set)) < 0)
			continue;  // on error, accept next
		accepted_fd.swap(sd);
		accepted_addr = details::good_inet_ntop(reinterpret_cast<sockaddr *>(&in_addr));
		return true;
	}
}

}  // namespace crab

#endif
