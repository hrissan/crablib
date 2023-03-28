// Copyright (c) 2007-2023, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <algorithm>
#include <atomic>
#include <iostream>
#include "network.hpp"

#if CRAB_IMPL_WINDOWS

#include <WS2tcpip.h>
#include <WinSock2.h>
#include <fcntl.h>
#include <mswsock.h>
#include <windows.h>
#undef ERROR
#undef min
#undef max

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

namespace details {

constexpr size_t OVERLAPPED_BUFFER_SIZE = 8192;
constexpr int OverlappedKey             = 113;  // We use arbitrary value to distinguish our overlapped calls

struct Overlapped : public OVERLAPPED {
	using Handler = std::function<void(DWORD bytes, bool result)>;
	explicit Overlapped(Handler &&cb) : OVERLAPPED{}, handler(std::move(cb)) {}
	void zero_overlapped() { *static_cast<OVERLAPPED *>(this) = OVERLAPPED{}; }

	Handler handler;
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

}  // namespace details

struct RunLoopImpl {
	details::Overlapped wake_handler;
	details::AutoHandle completion_queue;
	size_t pending_counter = 0;
	size_t impl_counter    = 0;

	explicit RunLoopImpl(Handler &&cb) : wake_handler([mcb = std::move(cb)](DWORD bytes, bool result) { mcb(); }) {
		completion_queue.value = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
		if (completion_queue.value == NULL)  // Microsoft URODI, this fun returns NULL on error, not an INVALID_HANDLE
			throw std::runtime_error{"RunLoop::RunLoop CreateIoCompletionPort failed"};
		WSADATA wsaData;
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
			throw std::runtime_error{"RunLoop::RunLoop WSAStartup failed"};
	}
	~RunLoopImpl() { WSACleanup(); }
};

CRAB_INLINE RunLoop::RunLoop() : impl(new RunLoopImpl([this]() { links.trigger_called_watchers(); })) {
	if (CurrentLoop::instance)
		throw std::runtime_error{"RunLoop::RunLoop Only single RunLoop per thread is allowed"};
	CurrentLoop::instance = this;
}

CRAB_INLINE RunLoop::~RunLoop() {
	while (impl->pending_counter != 0) {  // This cleanup cannot be in ~Impl because there would be no current_loop then
		std::cout << "RunLoop::~Impl() stepping through pending_counter=" << impl->pending_counter << std::endl;
		step();
	}
	if (impl->impl_counter != 0)
		std::cout << "RunLoop::~Impl() global_impl_counter=" << impl->impl_counter << std::endl;
	CurrentLoop::instance = nullptr;
}

CRAB_INLINE void RunLoop::step(int timeout_ms) {
	if (details::MAX_EVENTS <= 1) {
		OVERLAPPED *ovl         = nullptr;
		DWORD transferred       = 0;
		ULONG_PTR completionKey = 0;
		bool result             = GetQueuedCompletionStatus(impl->completion_queue.value, &transferred, &completionKey, &ovl, timeout_ms);
		DWORD last              = GetLastError();
		if (ovl == nullptr)  // General problem with queue
		{
			if (last != ERROR_TIMEOUT && last != STATUS_TIMEOUT)  // Microsoft URODI
				throw std::runtime_error{"GetQueuedCompletionStatus error"};
			return;  // Timeout is ok
		}
		if (completionKey != details::OverlappedKey)
			return;  // not our's overlapped call, TODO - remove to speep up
		details::Overlapped *our_ovl = static_cast<details::Overlapped *>(ovl);
		our_ovl->handler(transferred, result);
		return;
	}
	OVERLAPPED_ENTRY events[details::MAX_EVENTS];
	DWORD n     = 0;
	bool result = GetQueuedCompletionStatusEx(impl->completion_queue.value, events, details::MAX_EVENTS, &n, timeout_ms, false);
	if (!result) {
		DWORD last = GetLastError();
		if (last != ERROR_TIMEOUT && last != STATUS_TIMEOUT)  // Microsoft URODI
			throw std::runtime_error{"GetQueuedCompletionStatusEx error"};
		return;  // n is garbage in this case
	}
	stats.push_record("GetQueuedCompletionStatusEx", 0, n);
	stats.EPOLL_count += 1;
	stats.EPOLL_size += n;
	for (int i = 0; i != n; ++i) {
		if (events[i].lpCompletionKey != details::OverlappedKey)
			continue;
		// TODO - push_record each overlapped
		details::Overlapped *our_ovl = static_cast<details::Overlapped *>(events[i].lpOverlapped);
		our_ovl->handler(events[i].dwNumberOfBytesTransferred, true);
	}
}

CRAB_INLINE void RunLoop::wakeup() {
	if (PostQueuedCompletionStatus(impl->completion_queue.value, 0, details::OverlappedKey, &impl->wake_handler) == 0)
		throw std::runtime_error{"crab::Watcher::call PostQueuedCompletionStatus failed"};
}

CRAB_INLINE void RunLoop::cancel() {
	links.quit = true;
	wakeup();
}

CRAB_INLINE bool SignalStop::running_under_debugger() { return false; }

CRAB_INLINE Signal::Signal(Handler &&cb, const std::vector<int> &) : a_handler(std::move(cb)) {}

CRAB_INLINE Signal::~Signal() = default;

struct TCPSocketImpl {
	explicit TCPSocketImpl(TCPSocket *owner)
	    : read_overlapped([&](DWORD b, bool r) { on_overlapped_read(b, r); })
	    , write_overlapped([&](DWORD b, bool r) { on_overlapped_write(b, r); })
	    , owner(owner)
	    , tcp_id(++tcp_id_counter)
	    , loop(RunLoop::current())
	    , read_buf(details::OVERLAPPED_BUFFER_SIZE)
	    , write_buf(details::OVERLAPPED_BUFFER_SIZE) {
		loop->get_impl()->impl_counter += 1;
	}
	~TCPSocketImpl() { loop->get_impl()->impl_counter -= 1; }

	static size_t tcp_id_counter;

	details::SocketDescriptor fd;
	details::Overlapped read_overlapped;
	details::Overlapped write_overlapped;
	TCPSocket *owner;
	const size_t tcp_id;
	RunLoop *loop;
	Buffer read_buf;
	Buffer write_buf;
	bool pending_read   = false;
	bool pending_write  = false;
	bool connected      = false;  // We reuse OVERLAPPED for read and connect, this flag is false while connecting
	bool asked_shutdown = false;

	void on_overlapped_write(DWORD bytes, bool result) {
		pending_write = false;
		loop->get_impl()->pending_counter -= 1;
		if (details::DETAILED_DEBUG)
			std::cout << "tcp_id=" << tcp_id << " pending_write=" << pending_write
			          << " pending_counter=" << loop->get_impl()->pending_counter << std::endl;
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
		owner->rwd_handler.add_pending_callable(false, true);
		start_write();
	}
	void on_overlapped_read(DWORD bytes, bool result) {
		pending_read = false;
		loop->get_impl()->pending_counter -= 1;
		if (details::DETAILED_DEBUG)
			std::cout << "tcp_id=" << tcp_id << " pending_read=" << pending_read
			          << " pending_counter=" << loop->get_impl()->pending_counter << std::endl;
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
				if (details::DETAILED_DEBUG)
					std::cout << "tcp_id=" << tcp_id << " setsockopt SO_UPDATE_CONNECT_CONTEXT error" << WSAGetLastError() << std::endl;
				close(true);
				return;
			}
			connected = true;
			owner->rwd_handler.add_pending_callable(true, true);
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
		owner->rwd_handler.add_pending_callable(true, false);
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
			read_overlapped.zero_overlapped();
			if (WSARecv(fd.get_value(), pWsaBuf, write_buf.write_count2() != 0 ? 2 : 1, NULL, &dwFlags, &read_overlapped, nullptr) == 0 ||
			    (last = WSAGetLastError()) == ERROR_IO_PENDING) {
				pending_read = true;
				loop->get_impl()->pending_counter += 1;
				if (details::DETAILED_DEBUG)
					std::cout << "tcp_id=" << tcp_id << " pending_read=" << pending_read
					          << " pending_counter=" << loop->get_impl()->pending_counter << std::endl;
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
			write_overlapped.zero_overlapped();
			if (WSASend(fd.get_value(), pWsaBuf, write_buf.read_count2() != 0 ? 2 : 1, NULL, 0, &write_overlapped, nullptr) == 0 ||
			    (last = WSAGetLastError()) == ERROR_IO_PENDING) {
				pending_write = true;
				loop->get_impl()->pending_counter += 1;
				if (details::DETAILED_DEBUG)
					std::cout << "tcp_id=" << tcp_id << " pending_write=" << pending_write
					          << " pending_counter=" << loop->get_impl()->pending_counter << std::endl;
				return;
			}
			close(true);
		}
	}
	void close(bool from_runloop) {
		if (fd.get_value() == -1)
			return;
		if (details::DETAILED_DEBUG)
			std::cout << "tcp_id=" << tcp_id << " close=" << from_runloop << std::endl;
		fd.reset();  // Will automatically call CancelIo( impl->fd.handle_value() );
		if (from_runloop)
			owner->rwd_handler.add_pending_callable(true, false);
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

size_t TCPSocketImpl::tcp_id_counter = 0;

CRAB_INLINE TCPSocket::TCPSocket(Handler &&cb) : rwd_handler(std::move(cb)) {}

CRAB_INLINE TCPSocket::~TCPSocket() { close(); }

CRAB_INLINE void TCPSocket::close() {
	rwd_handler.cancel_callable();
	if (!impl || impl->fd.get_value() == -1)
		return;
	impl->close(false);
}

CRAB_INLINE void TCPSocket::write_shutdown() {
	if (!impl || impl->fd.get_value() == -1 || impl->asked_shutdown)
		return;
	impl->asked_shutdown = true;
	if (impl->write_buf.empty()) {
		impl->write_shutdown();
	}
}

CRAB_INLINE bool TCPSocket::is_open() const { return (impl && impl->fd.get_value() >= 0) || rwd_handler.is_pending_callable(); }

CRAB_INLINE bool TCPSocket::connect(const Address &address, const Settings &) {
	close();
	if (!impl)
		impl.reset(new TCPSocketImpl(this));
	details::SocketDescriptor tmp(socket(address.impl_get_sockaddr()->sa_family, SOCK_STREAM, IPPROTO_TCP));
	if (tmp.get_value() == -1)
		return false;
	if (CreateIoCompletionPort(tmp.handle_value(), RunLoop::current()->get_impl()->completion_queue.value, details::OverlappedKey, 0) ==
	    NULL)
		return false;
	// TODO - implement settings
	impl->read_overlapped.zero_overlapped();
	DWORD numBytes              = 0;
	DWORD last                  = 0;
	GUID guid                   = WSAID_CONNECTEX;
	LPFN_CONNECTEX ConnectExPtr = NULL;
	if (::WSAIoctl(tmp.get_value(), SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid), &ConnectExPtr, sizeof(ConnectExPtr),
	        &numBytes, NULL, NULL) != 0) {
		last = WSAGetLastError();
		return false;
	}
	// https://stackoverflow.com/questions/13598530/connectex-requires-the-socket-to-be-initially-bound-but-to-what
	Address any("0.0.0.0", 0);
	if (address.impl_get_sockaddr()->sa_family == AF_INET6)
		any = Address("::", 0);
	// TODO - other protocols (M$ URODI)
	if (bind(tmp.get_value(), any.impl_get_sockaddr(), any.impl_get_sockaddr_length()) != 0)
		return false;

	if (ConnectExPtr(tmp.get_value(), address.impl_get_sockaddr(), address.impl_get_sockaddr_length(), NULL, 0, NULL,
	        &impl->read_overlapped) == 0 &&
	    WSAGetLastError() != ERROR_IO_PENDING) {
		return false;
	}
	impl->fd.swap(tmp);
	impl->pending_read = true;
	RunLoop::current()->get_impl()->pending_counter += 1;
	return true;
}

CRAB_INLINE size_t TCPSocket::read_some(uint8_t *data, size_t count) {
	if (!impl || impl->fd.get_value() == -1)
		return 0;
	size_t result = impl->read_buf.read_some(data, count);
	impl->start_read();
	return result;
}

CRAB_INLINE size_t TCPSocket::write_some(const uint8_t *data, size_t count) {
	if (!impl || impl->fd.get_value() == -1)
		return 0;
	size_t result = impl->write_buf.write_some(data, count);
	impl->start_write();
	return result;
}

struct TCPAcceptorImpl {
	explicit TCPAcceptorImpl(TCPAcceptor *owner)
	    : accept_overlapped([&](DWORD b, bool r) { on_overlapped_call(b, r); })
	    , read_buf(details::OVERLAPPED_BUFFER_SIZE)
	    , owner(owner)
	    , loop(RunLoop::current()) {
		loop->get_impl()->impl_counter += 1;
	}
	~TCPAcceptorImpl() { loop->get_impl()->impl_counter -= 1; }
	details::Overlapped accept_overlapped;
	details::SocketDescriptor fd;
	details::SocketDescriptor accepted_fd;  // AcceptEx requires created socket
	Address accepted_addr;
	int ai_family = 0, ai_socktype = 0, ai_protocol = 0;  // for next accepted_fd
	Buffer read_buf;
	TCPAcceptor *owner;
	RunLoop *loop;
	bool pending_accept = false;
	void on_overlapped_call(DWORD bytes, bool result) {
		pending_accept = false;
		loop->get_impl()->pending_counter -= 1;
		if (details::DETAILED_DEBUG)
			std::cout << "pending_accept=" << pending_accept << " pending_counter=" << loop->get_impl()->pending_counter << std::endl;
		if (!owner) {
			delete this;  // Muhaha, last pending overlapped call
			return;
		}
		if (CreateIoCompletionPort(
		        accepted_fd.handle_value(), RunLoop::current()->get_impl()->completion_queue.value, details::OverlappedKey, 0) == NULL)
			throw std::runtime_error{"crab::TCPAcceptor::TCPAcceptor CreateIoCompletionPort failed"};

		DWORD last = 0;
		SOCKET val = fd.get_value();
		if (setsockopt(
		        accepted_fd.get_value(), SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, reinterpret_cast<const char *>(&val), sizeof(val)) != 0) {
			last = WSAGetLastError();
		}
		sockaddr *ap = nullptr;
		int aplen    = 0;
		sockaddr *la = nullptr;
		int lalen    = 0;
		GetAcceptExSockaddrs(read_buf.write_ptr(), 0, sizeof(sockaddr_in6) + 16, sizeof(sockaddr_in6) + 16, &la, &lalen, &ap, &aplen);
		if (aplen <= sizeof(sockaddr_storage)) {
			std::memcpy(accepted_addr.impl_get_sockaddr(), ap, aplen);
		}
		/*        if( WSAAddressToString(ap, aplen, nullptr, addr_buf, &addr_buf_len ) != 0){
		            DWORD last = WSAGetLastError();
		            return;
		        }*/
		owner->a_handler.add_pending_callable(true, false);
	}
	void start_accept() {
		if (pending_accept)
			return;
		details::SocketDescriptor tmp(socket(ai_family, ai_socktype, ai_protocol));
		if (tmp.get_value() == -1)
			throw std::runtime_error{"crab::TCPAcceptor::TCPAcceptor afd = socket failed"};
		accepted_fd.swap(tmp);
		DWORD dwBytesRecvd = 0;
		pending_accept     = true;
		loop->get_impl()->pending_counter += 1;
		if (details::DETAILED_DEBUG)
			std::cout << "pending_accept=" << pending_accept << " pending_counter=" << loop->get_impl()->pending_counter << std::endl;
		while (true) {
			accept_overlapped.zero_overlapped();
			if (AcceptEx(fd.get_value(),
			        accepted_fd.get_value(),
			        read_buf.write_ptr(),
			        0,
			        sizeof(sockaddr_in6) + 16,
			        sizeof(sockaddr_in6) + 16,
			        &dwBytesRecvd,
			        &accept_overlapped)) {
				if (details::DETAILED_DEBUG)
					std::cout << "Accept immediate success="
					          << " pending_counter=" << loop->get_impl()->pending_counter << std::endl;
				return;  // Success
			}
			DWORD last = WSAGetLastError();
			if (last == WSAECONNRESET)  // Skip already disconnected socket, read another one
				continue;               // TODO - test wether async call will be made in this situation
			if (last == ERROR_IO_PENDING)
				return;  // Success
			throw std::runtime_error{"crab::TCPAcceptor::TCPAcceptor AcceptEx failed"};
		}
	}
};

CRAB_INLINE void TCPSocket::accept(TCPAcceptor &acceptor, Address *accepted_addr) {
	if (acceptor.impl->pending_accept)
		throw std::logic_error{"TCPAcceptor::accept error, forgot if(can_accept())?"};
	close();
	if (!impl)
		impl.reset(new TCPSocketImpl(this));
	if (details::DETAILED_DEBUG)
		std::cout << "tcp_id=" << impl->tcp_id << " Accepted from addr=" << acceptor.impl->accepted_addr.get_address() << ":"
		          << acceptor.impl->accepted_addr.get_port() << std::endl;
	if (accepted_addr)
		*accepted_addr = acceptor.impl->accepted_addr;
	acceptor.impl->accepted_addr = Address();
	acceptor.impl->accepted_fd.swap(impl->fd);
	impl->connected = true;
	impl->start_read();
	acceptor.impl->start_accept();
}

CRAB_INLINE TCPAcceptor::TCPAcceptor(const Address &address, Handler &&a_handler, const Settings &settings)
    : a_handler(std::move(a_handler)), impl(new TCPAcceptorImpl(this)) {
	details::SocketDescriptor tmp(socket(address.impl_get_sockaddr()->sa_family, SOCK_STREAM, IPPROTO_TCP));
	if (tmp.get_value() == -1)
		std::runtime_error{"crab::TCPAcceptor::TCPAcceptor socket() failed"};
	// TODO - implement more settings
	int set = 1;  // Microsoft URODI
	if (settings.reuse_addr &&
	    setsockopt(tmp.get_value(), SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&set), sizeof(set)) != 0) {
		throw std::runtime_error{"crab::TCPAcceptor::TCPAcceptor setsockopt SO_REUSEADDR failed"};
	}
	impl->ai_family   = address.impl_get_sockaddr()->sa_family;
	impl->ai_socktype = SOCK_STREAM;
	impl->ai_protocol = IPPROTO_TCP;
	if (bind(tmp.get_value(), address.impl_get_sockaddr(), address.impl_get_sockaddr_length()) != 0)
		throw std::runtime_error{"crab::TCPAcceptor::TCPAcceptor bind(s) failed"};
	if (CreateIoCompletionPort(tmp.handle_value(), RunLoop::current()->get_impl()->completion_queue.value, details::OverlappedKey, 0) ==
	    NULL)
		throw std::runtime_error{"crab::TCPAcceptor::TCPAcceptor CreateIoCompletionPort failed"};
	if (listen(tmp.get_value(), SOMAXCONN) == -1)
		throw std::runtime_error{"crab::TCPAcceptor::TCPAcceptor listen failed"};
	tmp.swap(impl->fd);
	impl->start_accept();
}

CRAB_INLINE TCPAcceptor::~TCPAcceptor() {
	impl->owner = nullptr;
	impl->fd.reset();            // Will call IOCancel automatically
	if (impl->pending_accept) {  // Pending overlapped operation
		impl.release();
	}
}

CRAB_INLINE bool TCPAcceptor::can_accept() { return !impl->pending_accept; }

CRAB_INLINE UDPTransmitter::UDPTransmitter(const Address &address, Handler &&cb, const std::string &adapter) : w_handler(std::move(cb)) {
	throw std::runtime_error{"UDPTransmitter not yet implemented on Windows"};
}

CRAB_INLINE bool UDPTransmitter::write_datagram(const uint8_t *data, size_t count) { return 0; }

CRAB_INLINE UDPReceiver::UDPReceiver(const Address &address, Handler &&cb, const std::string &adapter) : r_handler(std::move(cb)) {
	throw std::runtime_error{"UDPReceiver not yet implemented on Windows"};
}

CRAB_INLINE optional<size_t> UDPReceiver::read_datagram(uint8_t *data, size_t count, Address *peer_addr) { return {}; }

}  // namespace crab

#endif
