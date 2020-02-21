// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <algorithm>
#include <iostream>
#include "network.hpp"

#if CRAB_SOCKET_WINDOWS

#include <WS2tcpip.h>
#include <WinSock2.h>
#include <fcntl.h>
#include <mswsock.h>
#include <windows.h>
#include <atomic>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "wsock32.lib")
#pragma comment(lib, "ntdll.lib")

#ifndef AI_ADDRCONFIG  // Instead of tinkering with WIN_VER we just repeat declarations here
#define AI_ADDRCONFIG 0x00000400
#endif
#ifndef AI_V4MAPPED
#define AI_V4MAPPED 0x00000800
#endif

namespace crab {

constexpr size_t OVERLAPPED_BUFFER_SIZE = 8192;
constexpr int OverlappedCallableKey     = 113;  // We use arbitrary value to distinguish our overlapped calls

struct OverlappedCallable : public OVERLAPPED {
	explicit OverlappedCallable() : OVERLAPPED{} {}
	virtual ~OverlappedCallable() = default;
	void zero_overlapped() { *static_cast<OVERLAPPED *>(this) = OVERLAPPED{}; }
	virtual void on_overlapped_call(DWORD bytes, bool result) = 0;
};

class SocketDescriptor : private Nocopy {
	SOCKET value;  // Very important that it is not mere int, because of sizeof() for setsockopt
public:
	SOCKET get_value() const { return value; }
	explicit SocketDescriptor(SOCKET value = -1) : value(value) {}
	~SocketDescriptor() { reset(); }
	HANDLE handle_value() const { return reinterpret_cast<HANDLE>(value); }
	void reset() {
		if (value != -1)
			closesocket(value);
		value = -1;
	}
	void swap(SocketDescriptor &other) { std::swap(value, other.value); }
};

class AutoHandle : private Nocopy {
public:
	HANDLE value;

	AutoHandle() : value(INVALID_HANDLE_VALUE) {}
	~AutoHandle() { reset(); }
	void reset() {
		if (value != INVALID_HANDLE_VALUE)
			CloseHandle(value);
		value = INVALID_HANDLE_VALUE;
	}
};

constexpr int MAX_EVENTS = 512;

constexpr bool DETAILED_DEBUG = false;

struct RunLoopImpl : public OverlappedCallable {
	RunLoop *owner;
	AutoHandle completion_queue;
	std::atomic<size_t> pending_counter{0};
	size_t impl_counter = 0;

	explicit RunLoopImpl(RunLoop *owner) : owner(owner) {
		completion_queue.value = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
		if (completion_queue.value == NULL)  // Microsoft URODI, this fun returns NULL on error, not an INVALID_HANDLE
			throw std::runtime_error("RunLoop::RunLoop CreateIoCompletionPort failed");
		WSADATA wsaData;
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
			throw std::runtime_error("RunLoop::RunLoop WSAStartup failed");
	}
	~RunLoopImpl() { WSACleanup(); }
	virtual void on_overlapped_call(DWORD bytes, bool result) override { owner->trigger_called_watchers(); }
};

RunLoop::RunLoop() : impl(new RunLoopImpl(this)) {
	if (current_loop)
		throw std::runtime_error("RunLoop::RunLoop Only single RunLoop per thread is allowed");
	current_loop = this;
}

RunLoop::~RunLoop() {
	while (
	    impl->pending_counter != 0) {  // This cleanup cannot be in ~Impl because there would be no current_loop then
		std::cout << "RunLoop::~Impl() stepping through pending_counter=" << impl->pending_counter << std::endl;
		step();
	}
	if (impl->impl_counter != 0)
		std::cout << "RunLoop::~Impl() global_impl_counter=" << impl->impl_counter << std::endl;
	current_loop = nullptr;
}

void RunLoop::step(int timeout_ms) {
	if (MAX_EVENTS <= 1) {
		OVERLAPPED *ovl         = nullptr;
		DWORD transferred       = 0;
		ULONG_PTR completionKey = 0;
		bool result =
		    GetQueuedCompletionStatus(impl->completion_queue.value, &transferred, &completionKey, &ovl, timeout_ms);
		DWORD last = GetLastError();
		if (ovl == nullptr)  // General problem with queue
		{
			if (last != ERROR_TIMEOUT && last != STATUS_TIMEOUT)  // Microsoft URODI
				throw std::runtime_error("GetQueuedCompletionStatus error");
			return;  // Timeout is ok
		}
		if (completionKey != OverlappedCallableKey)
			return;  // not our's overlapped call, TODO - remove to speep up
		OverlappedCallable *our_ovl = static_cast<OverlappedCallable *>(ovl);
		our_ovl->on_overlapped_call(transferred, result);
		return;
	}
	OVERLAPPED_ENTRY events[MAX_EVENTS];
	DWORD n     = 0;
	bool result = GetQueuedCompletionStatusEx(
	    impl->completion_queue.value, events, MAX_EVENTS, &n, timeout_ms == -1 ? INFINITE : timeout_ms, false);
	if (!result) {
		DWORD last = GetLastError();
		if (last != ERROR_TIMEOUT && last != STATUS_TIMEOUT)  // Microsoft URODI
			throw std::runtime_error("GetQueuedCompletionStatusEx error");
		return;  // n is garbage in this case
	}
	if (n)
		push_record("GetQueuedCompletionStatusEx", n);
	StaticHolder<PerformanceStats>::instance.EPOLL_count += 1;
	StaticHolder<PerformanceStats>::instance.EPOLL_size += n;
	for (int i = 0; i != n; ++i) {
		if (events[i].lpCompletionKey != OverlappedCallableKey)
			continue;
		OverlappedCallable *our_ovl = static_cast<OverlappedCallable *>(events[i].lpOverlapped);
		our_ovl->on_overlapped_call(events[i].dwNumberOfBytesTransferred, true);
	}
}

void RunLoop::wakeup() {
	if (PostQueuedCompletionStatus(impl->completion_queue.value, 0, OverlappedCallableKey, impl.get()) == 0)
		throw std::runtime_error("crab::Watcher::call PostQueuedCompletionStatus failed");
}

void RunLoop::cancel() { quit = true; }

static std::string good_inet_ntop(const sockaddr *addr) {
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

static int address_from_string(const std::string &str, crab::bdata &result) {
	bdata tmp(16, 0);
	if (inet_pton(AF_INET6, str.c_str(), tmp.data()) == 1) {
		result = tmp;
		return AF_INET6;
	}
	tmp.resize(4);
	if (inet_pton(AF_INET, str.c_str(), tmp.data()) == 1) {
		result = tmp;
		return AF_INET;
	}
	return 0;
}

bool DNSResolver::parse_ipaddress(const std::string &str, bdata *result) {
	return address_from_string(str, *result) != 0;
}

std::vector<Address> DNSWorker::sync_resolve(const std::string &host_name, uint16_t port, bool ipv4, bool ipv6) {
	std::vector<Address> names;
	if (!ipv4 && !ipv6)
		return names;
	addrinfo hints = {};
	struct AddrinfoHolder {
		struct addrinfo *result = nullptr;
		~AddrinfoHolder() { freeaddrinfo(result); }
	} holder;

	hints.ai_family    = ipv4 && ipv6 ? AF_UNSPEC : ipv4 ? AF_INET : AF_INET6;
	hints.ai_socktype  = SOCK_STREAM;
	hints.ai_flags     = AI_V4MAPPED | AI_ADDRCONFIG;  // AI_NUMERICHOST
	const auto service = std::to_string(port);

	if (getaddrinfo(host_name.c_str(), service.c_str(), &hints, &holder.result) != 0)
		return names;
	for (struct addrinfo *rp = holder.result; rp != nullptr; rp = rp->ai_next) {
		// TODO
		names.push_back(good_inet_ntop(rp->ai_addr));
	}
	return names;
}

static int tcp_id_counter = 0;

struct TCPSocketImpl : public OverlappedCallable {
	struct WriteOverlapped : public OverlappedCallable {
		// Microsoft URODI, need 2 OVERLAPPED structures in single object
		TCPSocketImpl *owner;

		explicit WriteOverlapped(TCPSocketImpl *owner) : owner(owner) {}
		virtual void on_overlapped_call(DWORD bytes, bool result) override {
			owner->on_overlapped_write_call(bytes, result);
		}
	};
	explicit TCPSocketImpl(TCPSocket *owner)
	    : write_overlapped(this)
	    , owner(owner)
	    , tcp_id(++tcp_id_counter)
	    , loop(RunLoop::current())
	    , read_buf(OVERLAPPED_BUFFER_SIZE)
	    , write_buf(OVERLAPPED_BUFFER_SIZE) {
		loop->impl->impl_counter += 1;
	}
	~TCPSocketImpl() override { loop->impl->impl_counter -= 1; }

	SocketDescriptor fd;
	WriteOverlapped write_overlapped;
	TCPSocket *owner;
	int tcp_id;
	RunLoop *loop;
	Buffer read_buf;
	Buffer write_buf;
	bool pending_read   = false;
	bool pending_write  = false;
	bool connected      = false;  // We reuse OVERLAPPED for read and connect, this flag is false while connecting
	bool asked_shutdown = false;

	void on_overlapped_write_call(DWORD bytes, bool result) {
		pending_write = false;
		loop->impl->pending_counter -= 1;
		if (DETAILED_DEBUG)
			std::cout << "tcp_id=" << tcp_id << " pending_write=" << pending_write
			          << " pending_counter=" << loop->impl->pending_counter << std::endl;
		if (!owner) {
			if (!pending_read && !pending_write)
				delete this;  // Muhaha, last pending overlapped call
			return;
		}
		write_buf.did_read(bytes);
		if (!result) {
			close(true);
			return;
		}
		owner->can_write = true;
		loop->links.add_triggered_callables(owner);
		start_write();
	}
	virtual void on_overlapped_call(DWORD bytes, bool result) override {
		pending_read = false;
		loop->impl->pending_counter -= 1;
		if (DETAILED_DEBUG)
			std::cout << "tcp_id=" << tcp_id << " pending_read=" << pending_read
			          << " pending_counter=" << loop->impl->pending_counter << std::endl;
		if (!owner) {
			if (!pending_read && !pending_write)
				delete this;  // Muhaha, last pending overlapped call
			return;
		}
		if (!connected) {
			if (!result) {
				close(true);
				return;
			}
			if (setsockopt(fd.get_value(), SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0) != 0) {
				if (DETAILED_DEBUG)
					std::cout << "tcp_id=" << tcp_id << " setsockopt SO_UPDATE_CONNECT_CONTEXT error"
					          << WSAGetLastError() << std::endl;
				close(true);
				return;
			}
			connected        = true;
			owner->can_read  = true;  // Not sure
			owner->can_write = true;
			loop->links.add_triggered_callables(owner);
			start_read();
			start_write();
			return;
		}
		read_buf.did_write(bytes);
		if (!result || bytes == 0) {  // RST || FIN
			close(true);
			return;
		}
		start_read();
		owner->can_read = true;
		loop->links.add_triggered_callables(owner);
	}
	void start_read() {
		if (pending_read || !connected)
			return;
		if (!read_buf.full()) {
			WSABUF pWsaBuf[2] = {};
			pWsaBuf[0].buf    = reinterpret_cast<char *>(read_buf.write_ptr());
			pWsaBuf[0].len    = static_cast<ULONG>(read_buf.write_count());
			pWsaBuf[1].buf    = reinterpret_cast<char *>(read_buf.write_ptr2());
			pWsaBuf[1].len    = static_cast<ULONG>(read_buf.write_count2());
			DWORD dwFlags     = 0;
			DWORD last        = 0;
			zero_overlapped();
			if (WSARecv(fd.get_value(), pWsaBuf, write_buf.write_count2() != 0 ? 2 : 1, NULL, &dwFlags, this,
			        nullptr) == 0 ||
			    (last = WSAGetLastError()) == ERROR_IO_PENDING) {
				pending_read = true;
				loop->impl->pending_counter += 1;
				if (DETAILED_DEBUG)
					std::cout << "tcp_id=" << tcp_id << " pending_read=" << pending_read
					          << " pending_counter=" << loop->impl->pending_counter << std::endl;
				return;
			}
			close(true);
		}
	}
	void start_write() {
		if (pending_write || !connected)
			return;
		if (write_buf.empty() && asked_shutdown) {
			write_shutdown();
			return;
		}
		if (!write_buf.empty()) {
			WSABUF pWsaBuf[2] = {};
			pWsaBuf[0].buf    = const_cast<char *>(reinterpret_cast<const char *>(write_buf.read_ptr()));
			pWsaBuf[0].len    = static_cast<ULONG>(write_buf.read_count());
			pWsaBuf[1].buf    = const_cast<char *>(reinterpret_cast<const char *>(write_buf.read_ptr2()));
			pWsaBuf[1].len    = static_cast<ULONG>(write_buf.read_count2());
			DWORD last        = 0;
			zero_overlapped();
			if (WSASend(fd.get_value(), pWsaBuf, write_buf.read_count2() != 0 ? 2 : 1, NULL, 0, &write_overlapped,
			        nullptr) == 0 ||
			    (last = WSAGetLastError()) == ERROR_IO_PENDING) {
				pending_write = true;
				loop->impl->pending_counter += 1;
				if (DETAILED_DEBUG)
					std::cout << "tcp_id=" << tcp_id << " pending_write=" << pending_write
					          << " pending_counter=" << loop->impl->pending_counter << std::endl;
				return;
			}
			close(true);
		}
	}
	void close(bool from_runloop) {
		if (fd.get_value() == -1)
			return;
		if (DETAILED_DEBUG)
			std::cout << "tcp_id=" << tcp_id << " close=" << from_runloop << std::endl;
		fd.reset();  // Will automatically call CancelIo( impl->fd.handle_value() );
		if (from_runloop)
			loop->links.add_triggered_callables(owner);
		if (pending_read || pending_write) {  // Pending overlapped operation
			owner->impl.release();            // It will be deleted on IO completion
			owner = nullptr;
		} else {
			connected      = false;
			asked_shutdown = false;
			read_buf.clear();
			write_buf.clear();
		}
	}
	void write_shutdown() {
		if (::shutdown(fd.get_value(), SD_SEND) != 0) {  // for situation like http "connection: close"
			                                             // DWORD last = GetLastError();
			                                             // impl->close();
			                                             // RunLoop::current()->remove_disconnecting_socket(this);
		}
	}
};

void TCPSocket::on_runloop_call() {
	if (!impl || impl->fd.get_value() == -1)
		return d_handler();
	try {
		rw_handler();
	} catch (const std::exception &ex) {
		impl->close(true);
	}
}

void TCPSocket::close() {
	cancel_callable();
	if (!impl || impl->fd.get_value() == -1)
		return;
	impl->close(false);
	can_read  = false;
	can_write = false;
}

void TCPSocket::write_shutdown() {
	if (impl->fd.get_value() == -1 && impl->asked_shutdown)
		return;
	impl->asked_shutdown = true;
	if (impl->write_buf.empty()) {
		impl->write_shutdown();
	}
}

bool TCPSocket::is_open() const { return impl && impl->fd.get_value() >= 0; }

bool TCPSocket::connect(const std::string &addr, uint16_t port) {
	close();
	if (!impl)
		impl.reset(new TCPSocketImpl(this));
	bdata addrdata;
	int family = address_from_string(addr, addrdata);
	if (family != AF_INET && family != AF_INET6)
		return false;
	SocketDescriptor tmp(socket(family, SOCK_STREAM, IPPROTO_TCP));
	if (tmp.get_value() == -1)
		return false;
	if (CreateIoCompletionPort(
	        tmp.handle_value(), RunLoop::current()->impl->completion_queue.value, OverlappedCallableKey, 0) == NULL)
		return false;
	impl->zero_overlapped();
	DWORD numBytes              = 0;
	DWORD last                  = 0;
	GUID guid                   = WSAID_CONNECTEX;
	LPFN_CONNECTEX ConnectExPtr = NULL;
	if (::WSAIoctl(tmp.get_value(), SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid), &ConnectExPtr,
	        sizeof(ConnectExPtr), &numBytes, NULL, NULL) != 0) {
		last = WSAGetLastError();
		return false;
	}
	if (family == AF_INET) {
		sockaddr_in addr{};
		addr.sin_family      = AF_INET;
		addr.sin_addr.s_addr = INADDR_ANY;
		if (bind(tmp.get_value(), reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0)
			return false;
		addr.sin_port = htons(port);
		std::copy(addrdata.begin(), addrdata.end(), reinterpret_cast<uint8_t *>(&addr.sin_addr));
		if (ConnectExPtr(
		        tmp.get_value(), reinterpret_cast<sockaddr *>(&addr), sizeof(addr), NULL, 0, NULL, impl.get()) == 0 &&
		    WSAGetLastError() != ERROR_IO_PENDING)
			return false;
	} else if (family == AF_INET6) {
		sockaddr_in6 addr{};
		addr.sin6_family = AF_INET6;
		addr.sin6_addr   = IN6ADDR_ANY_INIT;
		if (bind(tmp.get_value(), reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0)
			return false;
		addr.sin6_port = htons(port);
		std::copy(addrdata.begin(), addrdata.end(), reinterpret_cast<uint8_t *>(&addr.sin6_addr));
		if (ConnectExPtr(
		        tmp.get_value(), reinterpret_cast<sockaddr *>(&addr), sizeof(addr), NULL, 0, NULL, impl.get()) == 0 &&
		    WSAGetLastError() != ERROR_IO_PENDING)
			return false;
	}
	impl->fd.swap(tmp);
	impl->pending_read = true;
	RunLoop::current()->impl->pending_counter += 1;
	return true;
}

size_t TCPSocket::read_some(uint8_t *data, size_t count) {
	if (!impl || impl->fd.get_value() == -1)
		return 0;
	size_t result = impl->read_buf.read_some(data, count);
	impl->start_read();
	return result;
}

size_t TCPSocket::write_some(const uint8_t *data, size_t count) {
	if (!impl || impl->fd.get_value() == -1)
		return 0;
	size_t result = impl->write_buf.write_some(data, count);
	impl->start_write();
	return result;
}

struct TCPAcceptorImpl : public OverlappedCallable {
	explicit TCPAcceptorImpl(TCPAcceptor *owner)
	    : read_buf(OVERLAPPED_BUFFER_SIZE), owner(owner), loop(RunLoop::current()) {
		loop->impl->impl_counter += 1;
	}
	~TCPAcceptorImpl() override { loop->impl->impl_counter -= 1; }
	SocketDescriptor fd;
	SocketDescriptor accepted_fd;  // AcceptEx requires created socket
	std::string accepted_addr;
	int ai_family = 0, ai_socktype = 0, ai_protocol = 0;  // for next accepted_fd
	Buffer read_buf;
	TCPAcceptor *owner;
	RunLoop *loop;
	bool pending_accept = false;
	virtual void on_overlapped_call(DWORD bytes, bool result) override {
		pending_accept = false;
		loop->impl->pending_counter -= 1;
		if (DETAILED_DEBUG)
			std::cout << "pending_accept=" << pending_accept << " pending_counter=" << loop->impl->pending_counter
			          << std::endl;
		if (!owner) {
			delete this;  // Muhaha, last pending overlapped call
			return;
		}
		if (CreateIoCompletionPort(accepted_fd.handle_value(), RunLoop::current()->impl->completion_queue.value,
		        OverlappedCallableKey, 0) == NULL)
			throw std::runtime_error("crab::TCPAcceptor::TCPAcceptor CreateIoCompletionPort failed");

		DWORD last = 0;
		SOCKET val = fd.get_value();
		if (setsockopt(accepted_fd.get_value(), SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
		        reinterpret_cast<const char *>(&val), sizeof(val)) != 0) {
			last = WSAGetLastError();
		}
		sockaddr *ap = nullptr;
		int aplen    = 0;
		sockaddr *la = nullptr;
		int lalen    = 0;
		GetAcceptExSockaddrs(
		    read_buf.write_ptr(), 0, sizeof(sockaddr_in6) + 16, sizeof(sockaddr_in6) + 16, &la, &lalen, &ap, &aplen);
		char addr_buf[INET6_ADDRSTRLEN] = {};
		DWORD addr_buf_len              = sizeof(addr_buf);
		if (getnameinfo(ap, aplen, addr_buf, addr_buf_len, nullptr, 0, NI_NUMERICHOST) != 0) {
			last = WSAGetLastError();
		}
		/*        if( WSAAddressToString(ap, aplen, nullptr, addr_buf, &addr_buf_len ) != 0){
		            DWORD last = WSAGetLastError();
		            return;
		        }*/
		accepted_addr = addr_buf;
		loop->links.add_triggered_callables(owner);
	}
	void start_accept() {
		if (pending_accept)
			return;
		SocketDescriptor tmp(socket(ai_family, ai_socktype, ai_protocol));
		if (tmp.get_value() == -1)
			throw std::runtime_error("crab::TCPAcceptor::TCPAcceptor afd = socket failed");
		accepted_fd.swap(tmp);
		DWORD dwBytesRecvd = 0;
		pending_accept     = true;
		loop->impl->pending_counter += 1;
		if (DETAILED_DEBUG)
			std::cout << "pending_accept=" << pending_accept << " pending_counter=" << loop->impl->pending_counter
			          << std::endl;
		while (true) {
			zero_overlapped();
			if (AcceptEx(fd.get_value(),
			        accepted_fd.get_value(),
			        read_buf.write_ptr(),
			        0,
			        sizeof(sockaddr_in6) + 16,
			        sizeof(sockaddr_in6) + 16,
			        &dwBytesRecvd,
			        this)) {
				if (DETAILED_DEBUG)
					std::cout << "Accept immediate success="
					          << " pending_counter=" << loop->impl->pending_counter << std::endl;
				return;  // Success
			}
			DWORD last = WSAGetLastError();
			if (last == WSAECONNRESET)  // Skip already disconnected socket, read another one
				continue;               // TODO - test wether async call will be made in this situation
			if (last == ERROR_IO_PENDING)
				return;  // Success
			throw std::runtime_error("crab::TCPAcceptor::TCPAcceptor AcceptEx failed");
		}
	}
};

void TCPSocket::accept(TCPAcceptor &acceptor, std::string *accepted_addr) {
	if (acceptor.impl->pending_accept)
		throw std::logic_error("TCPAcceptor::accept error, forgot if(can_accept())?");
	close();
	if (DETAILED_DEBUG)
		std::cout << "tcp_id=" << impl->tcp_id << " Accepted from addr=" << acceptor.impl->accepted_addr << std::endl;
	if (accepted_addr)
		accepted_addr->swap(acceptor.impl->accepted_addr);
	acceptor.impl->accepted_addr.clear();
	acceptor.impl->accepted_fd.swap(impl->fd);
	impl->connected = true;
	impl->start_read();
	acceptor.impl->start_accept();
}

TCPAcceptor::TCPAcceptor(const std::string &address, uint16_t port, Handler &&a_handler)
    : a_handler(a_handler), impl(new TCPAcceptorImpl(this)) {
	bdata addrdata;
	int family = address_from_string(address, addrdata);
	if (family != AF_INET && family != AF_INET6) {
		throw std::runtime_error("crab::TCPAcceptor::TCPAcceptor should provide valid ip address");
	}
	SocketDescriptor tmp(socket(family, SOCK_STREAM, IPPROTO_TCP));
	if (tmp.get_value() == -1)
		std::runtime_error("crab::TCPAcceptor::TCPAcceptor socket() failed");
	int set = 1;  // Microsoft URODI
	if (setsockopt(tmp.get_value(), SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&set), sizeof(set)) !=
	    0) {
		throw std::runtime_error("crab::TCPAcceptor::TCPAcceptor setsockopt SO_REUSEADDR failed");
	}
	impl->ai_family   = family;
	impl->ai_socktype = SOCK_STREAM;
	impl->ai_protocol = IPPROTO_TCP;

	if (family == AF_INET) {
		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_port   = htons(port);
		std::copy(addrdata.begin(), addrdata.end(), reinterpret_cast<uint8_t *>(&addr.sin_addr));
		if (bind(tmp.get_value(), reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0)
			throw std::runtime_error("crab::TCPAcceptor::TCPAcceptor bind(s) failed");
	} else if (family == AF_INET6) {
		sockaddr_in6 addr{};
		addr.sin6_family = AF_INET6;
		addr.sin6_port   = htons(port);
		std::copy(addrdata.begin(), addrdata.end(), reinterpret_cast<uint8_t *>(&addr.sin6_addr));
		if (bind(tmp.get_value(), reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0)
			throw std::runtime_error("crab::TCPAcceptor::TCPAcceptor bind(s) failed");
	}
	if (CreateIoCompletionPort(
	        tmp.handle_value(), RunLoop::current()->impl->completion_queue.value, OverlappedCallableKey, 0) == NULL)
		throw std::runtime_error("crab::TCPAcceptor::TCPAcceptor CreateIoCompletionPort failed");
	if (listen(tmp.get_value(), SOMAXCONN) == -1)
		throw std::runtime_error("crab::TCPAcceptor::TCPAcceptor listen failed");
	tmp.swap(impl->fd);
	impl->start_accept();
}

TCPAcceptor::~TCPAcceptor() {
	impl->owner = nullptr;
	impl->fd.reset();            // Will call IOCancel automatically
	if (impl->pending_accept) {  // Pending overlapped operation
		impl.release();
	}
}

bool TCPAcceptor::can_accept() { return !impl->pending_accept; }

}  // namespace crab

#endif
