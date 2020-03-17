// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <algorithm>
#include <iostream>
#include "network.hpp"

#if CRAB_SOCKET_BOOST

#include <boost/array.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/circular_buffer.hpp>

namespace crab {

CRAB_INLINE bool Address::parse(Address &address, const std::string &ip, uint16_t port) {
	//    boost::asio::ip::tcp::endpoint endpoint( boost::asio::ip::address::from_string(addr), port);

	boost::system::error_code ec;
	address.addr = boost::asio::ip::address::from_string(ip, ec);
	address.port = port;
	return !ec;
}

CRAB_INLINE std::string Address::get_address() const {
	boost::system::error_code ec;
	return addr.to_string(ec) + ":" + std::to_string(get_port());
}

CRAB_INLINE uint16_t Address::get_port() const { return port; }

CRAB_INLINE bool Address::is_multicast_group() const { return addr.is_multicast(); }

struct RunLoopImpl {
	boost::asio::io_service io;
	size_t pending_counter = 0;
	size_t impl_counter    = 0;
};

CRAB_INLINE RunLoop::RunLoop() : impl(new RunLoopImpl()) {
	if (CurrentLoop::instance)
		throw std::runtime_error("RunLoop::RunLoop Only single RunLoop per thread is allowed");
	CurrentLoop::instance = this;
}

CRAB_INLINE RunLoop::~RunLoop() { CurrentLoop::instance = this; }

CRAB_INLINE void RunLoop::run() { impl->io.run(); }

CRAB_INLINE steady_clock::time_point RunLoop::now() { return steady_clock::now(); }

class TimerImpl {
public:
	explicit TimerImpl(Timer *owner) : owner(owner), timer(RunLoop::current()->get_impl()->io) {}
	Timer *owner;
	bool pending_wait = false;
	boost::asio::deadline_timer timer;

	void close() {
		if (pending_wait) {
			owner->impl.release();
			owner = nullptr;
			boost::system::error_code ec;
			timer.cancel(ec);  // Prevent exceptions
		}
	}
	void handle_timeout(const boost::system::error_code &e) {
		pending_wait = false;
		if (!owner) {
			delete this;
		}
		if (!e) {
			owner->a_handler();
			return;
		}
		if (e != boost::asio::error::operation_aborted) {
		}
	}
	void start_timer(double after_seconds) {
		// assert(pending_wait == false);
		pending_wait = true;
		timer.expires_from_now(boost::posix_time::milliseconds(
		    static_cast<int>(after_seconds * 1000)));  // int because we do not know exact type
		timer.async_wait([&](boost::system::error_code e) { handle_timeout(e); });
	}
};

// CRAB_INLINE Timer::~Timer() { cancel(); }

CRAB_INLINE void Timer::cancel() {
	if (impl)
		impl->close();
}

CRAB_INLINE bool Timer::is_set() const { return impl && impl->pending_wait; }

CRAB_INLINE void Timer::once(double after_seconds) {
	cancel();
	if (!impl)
		impl.reset(new TimerImpl(this));
	impl->start_timer(after_seconds);
}

class WatcherImpl {};

class TCPSocketImpl {
public:
	explicit TCPSocketImpl(TCPSocket *owner) : owner(owner), socket(RunLoop::current()->get_impl()->io) {}
	TCPSocket *owner;
	bool connected = false;  // TODO - simplify state machine

	boost::asio::ip::tcp::socket socket;
	Buffer incoming_buffer{8192};
	Buffer outgoing_buffer{8192};
	bool pending_connect = false;
	bool pending_read    = false;
	bool pending_write   = false;
	bool asked_shutdown  = false;

	void close(bool called_from_run_loop) {
		boost::system::error_code ec;
		socket.close(ec);  // Prevent exceptions
		connected       = false;
		asked_shutdown  = false;
		pending_connect = false;
		pending_read    = false;
		pending_write   = false;
		incoming_buffer.clear();
		outgoing_buffer.clear();
		TCPSocket *was_owner = owner;
		if (pending_write || pending_read || pending_connect) {
			owner->impl.release();
			owner = nullptr;
		}
		if (was_owner && called_from_run_loop)
			was_owner->rwd_handler.handler();
	}
	void write_shutdown() {
		boost::system::error_code ignored_ec;
		socket.shutdown(boost::asio::ip::tcp::socket::shutdown_send, ignored_ec);
	}

	void handle_connect(const boost::system::error_code &e) {
		pending_connect = false;
		if (!e) {
			connected = true;
			start_read();
			start_write();
			if (owner)
				owner->rwd_handler.handler();
			return;
		}
		if (e != boost::asio::error::operation_aborted) {
			close(true);
		}
	}
	void start_read() {
		if (incoming_buffer.full() || pending_read || !connected || !owner)
			return;
		pending_read = true;
		boost::array<boost::asio::mutable_buffer, 2> bufs{
		    boost::asio::buffer(incoming_buffer.write_ptr(), incoming_buffer.write_count()),
		    boost::asio::buffer(incoming_buffer.write_ptr2(), incoming_buffer.write_count2())};
		socket.async_read_some(bufs, [&](boost::system::error_code e, std::size_t b) { handle_read(e, b); });
	}

	void handle_read(const boost::system::error_code &e, std::size_t bytes_transferred) {
		pending_read = false;
		if (!e) {
			incoming_buffer.did_write(bytes_transferred);
			start_read();
			if (owner)
				owner->rwd_handler.handler();
			return;
		}
		if (e != boost::asio::error::operation_aborted) {
			close(true);
		}
	}

	void start_write() {
		if (pending_write || !connected || !owner)
			return;
		if (outgoing_buffer.empty()) {
			if (asked_shutdown)
				write_shutdown();
			return;
		}
		pending_write = true;
		boost::array<boost::asio::const_buffer, 2> bufs{
		    boost::asio::buffer(outgoing_buffer.read_ptr(), outgoing_buffer.read_count()),
		    boost::asio::buffer(outgoing_buffer.read_ptr2(), outgoing_buffer.read_count2())};
		socket.async_write_some(bufs, [&](boost::system::error_code e, std::size_t b) { handle_write(e, b); });
	}

	void handle_write(const boost::system::error_code &e, std::size_t bytes_transferred) {
		pending_write = false;
		if (!e) {
			outgoing_buffer.did_read(bytes_transferred);
			start_write();
			if (owner)
				owner->rwd_handler.handler();
			return;
		}
		if (e != boost::asio::error::operation_aborted) {
			close(true);
		}
	}
};

CRAB_INLINE void TCPSocket::close() {
	if (impl)
		impl->close(false);
}

CRAB_INLINE bool TCPSocket::is_open() const { return impl && impl->connected; }

CRAB_INLINE bool TCPSocket::connect(const Address &address) {
	close();
	if (!impl)
		impl.reset(new TCPSocketImpl(this));
	impl->pending_connect = true;
	boost::asio::ip::tcp::endpoint endpoint(address.get_addr(), address.get_port());
	auto i = impl.get();
	impl->socket.async_connect(endpoint, [i](boost::system::error_code e) { i->handle_connect(e); });
	return true;
}

CRAB_INLINE size_t TCPSocket::read_some(uint8_t *data, size_t size) {
	if (!impl)
		return 0;
	size_t rc = impl->incoming_buffer.read_some(data, size);
	impl->start_read();
	return rc;
}

CRAB_INLINE size_t TCPSocket::write_some(const uint8_t *data, size_t size) {
	if (!impl || impl->asked_shutdown)
		return 0;
	size_t wc = impl->outgoing_buffer.write_some(data, size);
	impl->start_write();
	return wc;
}

CRAB_INLINE bool TCPSocket::can_write() const { return impl && !impl->outgoing_buffer.full(); }

// CRAB_INLINE bool UDPTransmitter::can_write() const { return true; } // TODO

CRAB_INLINE void TCPSocket::write_shutdown() {
	if (impl->asked_shutdown)
		return;
	impl->asked_shutdown = true;
	if (impl->connected && !impl->pending_write)
		impl->write_shutdown();
}

class TCPAcceptorImpl {
public:
	TCPAcceptor *owner;
	explicit TCPAcceptorImpl(TCPAcceptor *owner)
	    : owner(owner)
	    , acceptor(RunLoop::current()->get_impl()->io)
	    , socket_being_accepted(RunLoop::current()->get_impl()->io) {}
	boost::asio::ip::tcp::acceptor acceptor;
	boost::asio::ip::tcp::socket socket_being_accepted;
	bool socket_ready   = false;
	bool pending_accept = false;

	void close() {
		boost::system::error_code ignored_ec;
		acceptor.close(ignored_ec);
		if (pending_accept) {
			owner->impl.release();
			owner = nullptr;
		}
	}
	void start_accept() {
		pending_accept = true;
		acceptor.async_accept(socket_being_accepted, [&](boost::system::error_code ec) { handle_accept(ec); });
	}
	void handle_accept(const boost::system::error_code &e) {
		pending_accept = false;
		if (!e) {
			socket_ready = true;
			if (owner)
				owner->a_handler.handler();
		}
		if (e != boost::asio::error::operation_aborted) {
			// some nasty problem with socket, say so to the client
		}
	}
};

CRAB_INLINE TCPAcceptor::TCPAcceptor(const Address &address, Handler &&cb)
    : a_handler(std::move(cb)), impl(new TCPAcceptorImpl(this)) {
	boost::asio::ip::tcp::resolver resolver(RunLoop::current()->get_impl()->io);
	boost::asio::ip::tcp::endpoint endpoint(address.get_addr(), address.get_port());
	impl->acceptor.open(endpoint.protocol());
	impl->acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
	impl->acceptor.bind(endpoint);
	impl->acceptor.listen();

	impl->start_accept();
}

CRAB_INLINE bool TCPAcceptor::can_accept() { return impl->socket_ready; }

CRAB_INLINE void TCPSocket::accept(TCPAcceptor &acceptor, Address *accepted_addr) {
	if (!acceptor.impl->socket_ready)
		throw std::logic_error("TCPAcceptor::accept error, forgot if(can_accept())?");
	acceptor.impl->socket_ready = false;
	close();
	if (!impl)
		impl.reset(new TCPSocketImpl(this));
	std::swap(impl->socket, acceptor.impl->socket_being_accepted);

	impl->connected = true;
	if (accepted_addr) {
		//        *accepted_addr = Address(impl->socket.remote_endpoint().address().to_string());
	}
	impl->start_read();
	acceptor.impl->start_accept();
}

CRAB_INLINE TCPAcceptor::~TCPAcceptor() { impl->close(); }

/*class DNSResolver::Impl {
public:
    DNSResolver *owner;
    explicit Impl(DNSResolver *owner) : owner(owner), resolver(RunLoop::current()->get_impl()->io) {}
    boost::asio::ip::tcp::resolver resolver;
    bool pending_resolve;

    void close() {
        resolver.cancel();
        DNSResolver *was_owner = owner;
        if (pending_resolve) {
            owner           = nullptr;
            was_owner->impl = std::make_shared<Impl>(was_owner);  // We do not reuse LA Sockets
        }
    }
    void handle_resolve(const boost::system::error_code &err,
        boost::asio::ip::tcp::resolver::iterator endpoint_iterator) {
        pending_resolve = false;
        if (!err) {
            if (owner) {
                std::vector<std::string> names;

                for (; endpoint_iterator != boost::asio::ip::tcp::resolver::iterator(); ++endpoint_iterator) {
                    names.push_back(endpoint_iterator->endpoint().address().to_string());
                }
                owner->dns_handler(names);
            }
        } else {
            std::cout << "Error: " << err.message() << "\n";
        }
    }
};


DNSResolver::DNSResolver(DNS_handler handler) : dns_handler(handler), impl(std::make_shared<Impl>(this)) {}

DNSResolver::~DNSResolver() { impl->close(); }

void DNSResolver::resolve(const std::string &host_name, bool ipv4, bool ipv6) {
    impl->close();
    boost::asio::ip::tcp::resolver::query query(host_name, "http");
    impl->pending_resolve = true;
    impl->resolver.async_resolve(query,
        boost::bind(
            &Impl::handle_resolve, impl, boost::asio::placeholders::error, boost::asio::placeholders::iterator));
}

void DNSResolver::cancel() { impl->close(); }

bdata DNSResolver::parse_ipaddress(const std::string &str) {
    if (str.empty())
        return bdata();
    try {
        boost::asio::ip::address address = boost::asio::ip::address::from_string(str);
        if (address.is_v4()) {
            auto bytes = address.to_v4().to_bytes();
            return bdata(bytes.begin(), bytes.end());
        }
        if (address.is_v6()) {
            auto bytes = address.to_v6().to_bytes();
            return bdata(bytes.begin(), bytes.end());
        }
    } catch (...) {
    }
    return bdata();
}
*/

CRAB_INLINE std::vector<Address> DNSResolver::sync_resolve(
    const std::string &host_name, uint16_t port, bool ipv4, bool ipv6) {
	boost::system::error_code ec;

	boost::asio::ip::tcp::resolver resolver(RunLoop::current()->get_impl()->io);
	auto service = std::to_string(port);

	boost::asio::ip::tcp::resolver::query query(host_name, service);
	boost::asio::ip::tcp::resolver::iterator endpoint_iterator = resolver.resolve(query, ec);
	std::vector<Address> names;
	if (ec)
		return names;
	for (; endpoint_iterator != boost::asio::ip::tcp::resolver::iterator(); ++endpoint_iterator) {
		names.emplace_back(endpoint_iterator->endpoint().address(), endpoint_iterator->endpoint().port());
	}
	return names;
}

/*void test_network() {
    boost::asio::io_service io_service;
    System::EventLoop event_loop(io_service);

    Buffer ms(1024);
    char getstr[]="GET / HTTP/1.1\r\n\r\n";
    ms.writeSome(getstr, sizeof(getstr) - 1);

    std::unique_ptr<TCPSocket> rws;

    rws.reset( new TCPSocket( [&](){
        auto written = rws->writeSome(ms.read_ptr(), ms.read_count());
        ms.did_read(written);
        if( ms.empty() )
            rws->write_shutdown();

        uint8_t buf[512]={};
        size_t cou = rws->readSome(buf, sizeof(buf));
        std::cout << std::string(reinterpret_cast<char *>(buf), cou);
    }, [&](){
        std::cout << std::endl << "test_disconnect" << std::endl;
    }));

    boost::asio::ip::tcp::resolver resolver(EventLoop::io());
    boost::asio::ip::tcp::resolver::query query("google.com", "http");
    resolver.async_resolve(query, [&](const boost::system::error_code& err, boost::asio::ip::tcp::resolver::iterator
endpoint_iterator){ if (!err)
        {
            boost::asio::ip::tcp::endpoint ep = *endpoint_iterator;
            std::cout << "Connecting to: " << ep.address().to_string() << "\n";
            rws->connect(ep.address().to_string(), 80);
        }
        else
        {
            std::cout << "Error: " << err.message() << "\n";
        }
    });

    EventLoop::io().run();
}*/

}  // namespace crab

#endif
