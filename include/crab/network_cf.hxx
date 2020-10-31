// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <sys/socket.h>
#include <algorithm>
#include <iostream>
#include "network.hpp"

#if CRAB_IMPL_CF

#include <arpa/inet.h>

namespace crab {

CRAB_INLINE void Timer::static_cb(CFRunLoopTimerRef, void *info) { reinterpret_cast<Timer *>(info)->a_handler(); }

CRAB_INLINE Timer::Timer(Handler &&cb) : a_handler(std::move(cb)) {}

CRAB_INLINE Timer::~Timer() { cancel(); }

CRAB_INLINE void Timer::cancel() {
	if (!impl)
		return;
	//    CFRunLoopRemoveTimer(CFRunLoopGetCurrent(), impl, kCFRunLoopDefaultMode);
	CFRunLoopTimerInvalidate(impl);
	CFRelease(impl);
	impl = nullptr;
}

CRAB_INLINE bool Timer::is_set() const { return impl; }

CRAB_INLINE void Timer::once(double after_seconds) {
	cancel();
	CFRunLoopTimerContext TimerContext = {0, this, nullptr, nullptr, nullptr};
	CFAbsoluteTime FireTime            = CFAbsoluteTimeGetCurrent() + after_seconds;
	impl = CFRunLoopTimerCreate(kCFAllocatorDefault, FireTime, 0, 0, 0, &Timer::static_cb, &TimerContext);
	CFRunLoopAddTimer(CFRunLoopGetCurrent(), impl, kCFRunLoopDefaultMode);
}

CRAB_INLINE void Timer::once(steady_clock::duration delay) {
	double after_seconds = std::chrono::duration_cast<std::chrono::duration<double>>(delay).count();
	once(after_seconds);
}

CRAB_INLINE void Timer::once_at(steady_clock::time_point time_point) { once(time_point - RunLoop::current()->now()); }

CRAB_INLINE void Watcher::static_cb(void *info) { reinterpret_cast<Watcher *>(info)->a_handler.handler(); }

CRAB_INLINE Watcher::Watcher(Handler &&a_handler)
    : loop(RunLoop::current()), a_handler(std::move(a_handler)), loop_loop(CFRunLoopGetCurrent()) {
	CFRunLoopSourceContext SourceContext = {0, this, 0, 0, 0, 0, 0, 0, 0, &Watcher::static_cb};
	impl                                 = CFRunLoopSourceCreate(kCFAllocatorDefault, 0, &SourceContext);
	CFRunLoopAddSource(CFRunLoopGetCurrent(), impl, kCFRunLoopDefaultMode);
}

CRAB_INLINE Watcher::~Watcher() { cancel(); }

CRAB_INLINE void Watcher::call() {  // TODO
	CFRunLoopSourceSignal(impl);
	CFRunLoopWakeUp(loop_loop);
}

CRAB_INLINE void Watcher::cancel() {  // TODO
	CFRunLoopSourceInvalidate(impl);
	CFRelease(impl);
	impl                                 = nullptr;
	CFRunLoopSourceContext SourceContext = {0, this, 0, 0, 0, 0, 0, 0, 0, &Watcher::static_cb};
	impl                                 = CFRunLoopSourceCreate(kCFAllocatorDefault, 0, &SourceContext);
	CFRunLoopAddSource(CFRunLoopGetCurrent(), impl, kCFRunLoopDefaultMode);
}

CRAB_INLINE Idle::Idle(Handler &&cb) : a_handler(std::move(cb)) { set_active(true); }

CRAB_INLINE void Idle::set_active(bool a) {
	auto loop = RunLoop::current();
	if (a) {
		loop->idle_handlers.push_back(*this);
	} else {
		idle_node.unlink();
	}
	if (loop->idle_handlers.empty() && loop->idle_observer) {
		CFRunLoopObserverInvalidate(loop->idle_observer);
		CFRelease(loop->idle_observer);
		loop->idle_observer = nullptr;
	}
	if (!loop->idle_handlers.empty() && !loop->idle_observer) {
		CFRunLoopObserverContext context{0, loop, 0, 0, 0};
		loop->idle_observer = CFRunLoopObserverCreate(
		    kCFAllocatorDefault, kCFRunLoopBeforeWaiting, true, 0, &RunLoop::on_idle_observer, &context);
		CFRunLoopAddObserver(CFRunLoopGetCurrent(), loop->idle_observer, kCFRunLoopDefaultMode);
	}
}

CRAB_INLINE SignalStop::SignalStop(Handler &&cb) : a_handler(std::move(cb)) {}

CRAB_INLINE SignalStop::~SignalStop() {}

CRAB_INLINE bool SignalStop::running_under_debugger() { return false; }

CRAB_INLINE RunLoop::RunLoop() {
	if (CurrentLoop::instance)
		throw std::runtime_error("RunLoop::RunLoop Only single RunLoop per thread is allowed");
	CurrentLoop::instance = this;
}

CRAB_INLINE RunLoop::~RunLoop() {
	CurrentLoop::instance = this;
	CFRunLoopObserverInvalidate(idle_observer);
	CFRelease(idle_observer);
	idle_observer = nullptr;
}

CRAB_INLINE void RunLoop::run() { CFRunLoopRun(); }

CRAB_INLINE void RunLoop::cancel() { CFRunLoopStop(CFRunLoopGetCurrent()); }

CRAB_INLINE void RunLoop::on_idle_observer(CFRunLoopObserverRef, CFRunLoopActivity activity, void *info) {
	auto loop = reinterpret_cast<RunLoop *>(info);
	// idle_handlers must not be empty here, but check is cheap
	if (activity == kCFRunLoopBeforeWaiting && !loop->idle_handlers.empty()) {
		Idle &idle = loop->idle_handlers.front();
		// Rotate round-robin
		idle.idle_node.unlink();
		loop->idle_handlers.push_back(idle);
		idle.a_handler();
		CFRunLoopWakeUp(CFRunLoopGetCurrent());
	}
}

CRAB_INLINE steady_clock::time_point RunLoop::now() {
	return steady_clock::now();  // TODO
}

/*CRAB_INLINE bool Address::parse(Address &address, const std::string &ip, uint16_t port) {
    address.addr = ip;
    address.port = port;
    return true;
}

CRAB_INLINE std::string Address::get_address() const {
    return addr;
}

CRAB_INLINE uint16_t Address::get_port() const { return port; }

CRAB_INLINE bool Address::is_multicast() const { return false; } // TODO
*/

CRAB_INLINE TCPSocket::TCPSocket(Handler &&cb)
    : rwd_handler(std::move(cb)), closed_event([&] { rwd_handler.handler(); }) {}

CRAB_INLINE TCPSocket::~TCPSocket() { close(); }

CRAB_INLINE void TCPSocket::close() {
	closed_event.cancel();
	if (read_stream) {
		CFReadStreamClose(read_stream);
		CFRelease(read_stream);
		read_stream = nullptr;
	}
	if (write_stream) {
		CFWriteStreamClose(write_stream);
		CFRelease(write_stream);
		write_stream = nullptr;
	}
}

CRAB_INLINE bool TCPSocket::is_open() const { return read_stream || closed_event.is_set(); }

CRAB_INLINE bool TCPSocket::finish_connect() {
	CFStreamClientContext my_context{0, this, 0, 0, 0};
	if (!CFReadStreamSetClient(read_stream,
	        kCFStreamEventHasBytesAvailable | kCFStreamEventErrorOccurred | kCFStreamEventEndEncountered,
	        &TCPSocket::read_cb, &my_context)) {
		close();
		return false;
	}
	if (!CFWriteStreamSetClient(write_stream,
	        kCFStreamEventCanAcceptBytes | kCFStreamEventErrorOccurred | kCFStreamEventEndEncountered,
	        &TCPSocket::write_cb, &my_context)) {
		close();
		return false;
	}
	CFReadStreamScheduleWithRunLoop(read_stream, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
	CFWriteStreamScheduleWithRunLoop(write_stream, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
	CFReadStreamOpen(read_stream);
	CFWriteStreamOpen(write_stream);
	return true;
}

CRAB_INLINE bool TCPSocket::connect(const Address &address) {
	close();
	CFStringRef hname =
	    CFStringCreateWithCString(kCFAllocatorDefault, address.get_address().c_str(), kCFStringEncodingUTF8);
	CFHostRef host = CFHostCreateWithName(kCFAllocatorDefault, hname);
	CFRelease(hname);
	hname = nullptr;
	CFStreamCreatePairWithSocketToCFHost(kCFAllocatorDefault, host, address.get_port(), &read_stream, &write_stream);
	CFRelease(host);
	host = nullptr;
	return finish_connect();
}

CRAB_INLINE size_t TCPSocket::read_some(uint8_t *val, size_t count) {
	if (!read_stream || !CFReadStreamHasBytesAvailable(read_stream))
		return 0;
	CFIndex bytes_read = CFReadStreamRead(read_stream, reinterpret_cast<unsigned char *>(val), count);
	if (bytes_read <= 0) {  // error or end of stream
		return 0;
	}
	return bytes_read;
}

CRAB_INLINE bool TCPSocket::can_write() const { return write_stream && CFWriteStreamCanAcceptBytes(write_stream); }

CRAB_INLINE size_t TCPSocket::write_some(const uint8_t *val, size_t count) {
	if (!write_stream || !CFWriteStreamCanAcceptBytes(write_stream))
		return 0;
	CFIndex bytes_written = CFWriteStreamWrite(write_stream, val, count);
	if (bytes_written <= 0) {  // error or end of stream
		return 0;
	}
	return bytes_written;
}

CRAB_INLINE void TCPSocket::write_shutdown() {
	if (!is_open())
		return;
	CFDataRef da =
	    static_cast<CFDataRef>(CFWriteStreamCopyProperty(write_stream, kCFStreamPropertySocketNativeHandle));
	if (!da)
		return;
	CFSocketNativeHandle handle;
	CFDataGetBytes(da, CFRangeMake(0, sizeof(CFSocketNativeHandle)), reinterpret_cast<unsigned char *>(&handle));
	CFRelease(da);
	::shutdown(handle, SHUT_WR);
}

CRAB_INLINE void TCPSocket::read_cb(CFReadStreamRef stream, CFStreamEventType event, void *info) {
	auto owner = reinterpret_cast<TCPSocket *>(info);
	switch (event) {
	case kCFStreamEventHasBytesAvailable:
		owner->rwd_handler.handler();
		break;
	case kCFStreamEventErrorOccurred:
	case kCFStreamEventEndEncountered:
		owner->close();
		owner->closed_event.once(0);
		break;
	}
}

CRAB_INLINE void TCPSocket::write_cb(CFWriteStreamRef stream, CFStreamEventType event, void *info) {
	auto owner = reinterpret_cast<TCPSocket *>(info);
	switch (event) {
	case kCFStreamEventCanAcceptBytes:
		owner->rwd_handler.handler();
		break;
	case kCFStreamEventErrorOccurred:
	case kCFStreamEventEndEncountered:
		owner->close();
		owner->closed_event.once(0);
		break;
	}
}

CRAB_INLINE TCPAcceptor::TCPAcceptor(const Address &address, Handler &&cb) : a_handler(std::move(cb)) {
	CFSocketContext context = {0, this, 0, 0, 0};
	CFSocketRef impl        = CFSocketCreate(kCFAllocatorDefault, address.impl_get_sockaddr()->sa_family, SOCK_STREAM,
        IPPROTO_TCP, kCFSocketAcceptCallBack, accept_cb, &context);

	CFDataRef sincfd = CFDataCreate(kCFAllocatorDefault,
	    reinterpret_cast<const uint8_t *>(address.impl_get_sockaddr()),
	    address.impl_get_sockaddr_length());

	CFSocketError sockErr = CFSocketSetAddress(impl, sincfd);
	if (sockErr != kCFSocketSuccess)
		throw std::runtime_error("crab::TCPAcceptor error");
	CFRelease(sincfd);
	sincfd = nullptr;

	socket_source = CFSocketCreateRunLoopSource(kCFAllocatorDefault, impl, 0);
	CFRelease(impl);
	impl = nullptr;
	CFRunLoopAddSource(CFRunLoopGetCurrent(), socket_source, kCFRunLoopDefaultMode);
}

CRAB_INLINE bool TCPAcceptor::can_accept() { return !accepted_sockets.empty(); }

CRAB_INLINE void TCPSocket::accept(TCPAcceptor &acceptor, Address *accepted_addr) {
	if (acceptor.accepted_sockets.empty())
		throw std::logic_error("TCPAcceptor::accept error, forgot if(can_accept())?");
	close();
	int fd = acceptor.accepted_sockets.front();
	acceptor.accepted_sockets.pop_front();
	CFStreamCreatePairWithSocket(kCFAllocatorDefault, fd, &read_stream, &write_stream);
	if (!finish_connect()) {
		closed_event.once(0);
		return;
	}
	CFReadStreamSetProperty(read_stream, kCFStreamPropertyShouldCloseNativeSocket, kCFBooleanTrue);
	CFWriteStreamSetProperty(write_stream, kCFStreamPropertyShouldCloseNativeSocket, kCFBooleanTrue);
}

CRAB_INLINE TCPAcceptor::~TCPAcceptor() {
	for (auto fd : accepted_sockets)
		close(fd);  // Close not yet connected
	accepted_sockets.clear();
	CFRunLoopSourceInvalidate(socket_source);
	CFRelease(socket_source);
	socket_source = nullptr;
}

CRAB_INLINE void TCPAcceptor::accept_cb(
    CFSocketRef, CFSocketCallBackType type, CFDataRef address, const void *data, void *info) {
	if (type != kCFSocketAcceptCallBack)
		return;
	auto *owner = reinterpret_cast<TCPAcceptor *>(info);
	int fd      = *reinterpret_cast<const int *>(data);
	owner->accepted_sockets.push_back(fd);
	if (owner->accepted_sockets.size() == 1)  // We are edge-triggered
		owner->a_handler.handler();
}

CRAB_INLINE std::vector<Address> DNSResolver::sync_resolve(
    const std::string &host_name, uint16_t port, bool ipv4, bool ipv6) {
	// TODO DNS lookups
	return std::vector<Address>{};
}

CRAB_INLINE UDPTransmitter::UDPTransmitter(const Address &address, Handler &&cb, const std::string &adapter)
    : w_handler(std::move(cb)) {
	throw std::runtime_error("UDPTransmitter not yet implemented on Windows");
}

CRAB_INLINE bool UDPTransmitter::write_datagram(const uint8_t *data, size_t count) { return 0; }

CRAB_INLINE UDPReceiver::UDPReceiver(const Address &address, Handler &&cb, const std::string &adapter)
    : r_handler(std::move(cb)) {
	throw std::runtime_error("UDPReceiver not yet implemented on Windows");
}

CRAB_INLINE optional<size_t> UDPReceiver::read_datagram(uint8_t *data, size_t count, Address *peer_addr) {
	return {};
}

}  // namespace crab

#endif
