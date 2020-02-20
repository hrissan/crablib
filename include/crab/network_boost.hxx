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

RunLoop::RunLoop() {
	if (current_loop != 0)
		throw std::runtime_error("RunLoop::RunLoop Only single RunLoop per thread is allowed");
	current_loop = this;
}

RunLoop::~RunLoop() { current_loop = 0; }

void RunLoop::run() { io_service.run(); }

class Timermpl {
public:
	explicit TimerImpl(Timer *owner) : owner(owner), pending_wait(false), timer(EventLoop::current()->io()) {}
	Timer *owner;
	bool pending_wait;
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
	void start_timer(float after_seconds) {
		// assert(pending_wait == false);
		pending_wait = true;
		timer.expires_from_now(boost::posix_time::milliseconds(
		    static_cast<int>(after_seconds * 1000)));  // int because we do not know exact type
		timer.async_wait(std::bind(&Impl::handle_timeout, owner->impl, _1));
	}
};

Timer::~Timer() { cancel(); }

void Timer::cancel() {
	if (impl)
		impl->close();
}

bool Timer::is_set() const { return impl && impl->pending_wait; }

void Timer::once(float after_seconds) {
	cancel();
	if (!impl)
		impl = std::make_unique<TimerImpl>(this);
	impl->start_timer(after_seconds / get_time_multiplier_for_tests());
}

class TCPSocketImpl {
public:
	TCPSocket *owner;
	explicit TCPSocketImpl(TCPSocket *owner)
	    : owner(owner), socket(RunLoop::current()->io()), incoming_buffer(8192), outgoing_buffer(8192) {}
	bool connected = false;

	boost::asio::ip::tcp::socket socket;
	Buffer incoming_buffer;
	Buffer outgoing_buffer;
	bool pending_connect = false;
	bool pending_read    = false;
	bool pending_write   = false;
	bool asked_shutdown  = false;

	void close(bool called_from_run_loop) {
		socket.close();
		connected       = false;
		asked_shutdown  = false;
		pending_connect = false;
		pending_read    = false;
		pending_write   = false;
		incoming_buffer.reset();
		outgoing_buffer.reset();
		TCPSocket *was_owner = owner;
		if (pending_write || pending_read || pending_connect) {
			owner           = nullptr;
			was_owner->impl = std::make_shared<Impl>(was_owner);
		}
		if (was_owner && called_from_run_loop)
			was_owner->d_handler();
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
				owner->rw_handler(true, true);
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
		socket.async_read_some(bufs, boost::bind(&Impl::handle_read, owner->impl, boost::asio::placeholders::error,
		                                 boost::asio::placeholders::bytes_transferred));
	}

	void handle_read(const boost::system::error_code &e, std::size_t bytes_transferred) {
		pending_read = false;
		if (!e) {
			incoming_buffer.did_write(bytes_transferred);
			start_read();
			if (owner)
				owner->rw_handler(true, true);
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
				start_shutdown();
			return;
		}
		pending_write = true;
		boost::array<boost::asio::const_buffer, 2> bufs{
		    boost::asio::buffer(outgoing_buffer.read_ptr(), outgoing_buffer.read_count()),
		    boost::asio::buffer(outgoing_buffer.read_ptr2(), outgoing_buffer.read_count2())};
		socket.async_write_some(bufs, boost::bind(&Impl::handle_write, owner->impl, boost::asio::placeholders::error,
		                                  boost::asio::placeholders::bytes_transferred));
	}

	void handle_write(const boost::system::error_code &e, std::size_t bytes_transferred) {
		pending_write = false;
		if (!e) {
			outgoing_buffer.did_read(bytes_transferred);
			start_write();
			if (owner)
				owner->rw_handler(true, true);
			return;
		}
		if (e != boost::asio::error::operation_aborted) {
			close(true);
		}
	}
};

TCPSocket::TCPSocket(RW_handler rw_handler, D_handler d_handler)
    : rw_handler(rw_handler), d_handler(d_handler), impl(std::make_shared<Impl>(this)) {}

TCPSocket::~TCPSocket() { close(); }

void TCPSocket::close() { impl->close(false); }

bool TCPSocket::connect(const std::string &addr, uint16_t port) {
	close();

	try {
		impl->pending_connect = true;
		boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::address::from_string(addr), port);
		impl->socket.async_connect(
		    endpoint, boost::bind(&TCPSocket::Impl::handle_connect, impl, boost::asio::placeholders::error));
	} catch (...) {
		return false;
	}
	return true;
}

size_t TCPSocket::read_some(uint8_t *data, size_t size) {
	size_t rc = impl->incoming_buffer.read_some(data, size);
	impl->start_read();
	return rc;
}

size_t TCPSocket::write_some(const uint8_t *data, size_t size) {
	if (impl->asked_shutdown)
		return 0;
	size_t wc = impl->outgoing_buffer.write_some(data, size);
	impl->start_write();
	return wc;
}

void TCPSocket::write_shutdown() {
	if (impl->asked_shutdown)
		return;
	impl->asked_shutdown = true;
	if (impl->connected && !impl->pending_write)
		impl->start_shutdown();
}

class TCPAcceptor::Impl {
public:
	TCPAcceptor *owner;
	explicit Impl(TCPAcceptor *owner)
	    : owner(owner)
	    , pending_accept(false)
	    , acceptor(RunLoop::current()->io())
	    , socket_being_accepted(RunLoop::current()->io())
	    , socket_ready(false) {}
	boost::asio::ip::tcp::acceptor acceptor;
	boost::asio::ip::tcp::socket socket_being_accepted;
	bool socket_ready;
	bool pending_accept;

	void close() {
		acceptor.close();
		TCPAcceptor *was_owner = owner;
		owner                  = nullptr;
		if (pending_accept) {
			was_owner->impl.reset();  // We do not reuse LA Sockets
		}
	}
	void start_accept() {
		if (!owner)
			return;
		pending_accept = true;
		acceptor.async_accept(
		    socket_being_accepted, boost::bind(&Impl::handle_accept, owner->impl, boost::asio::placeholders::error));
	}
	void handle_accept(const boost::system::error_code &e) {
		pending_accept = false;
		if (!e) {
			socket_ready = true;
			if (owner)
				owner->a_handler();
		}
		if (e != boost::asio::error::operation_aborted) {
			// some nasty problem with socket, say so to the client
		}
	}
};

TCPAcceptor::TCPAcceptor(const std::string &addr, uint16_t port, A_handler a_handler)
    : a_handler(a_handler), impl(std::make_shared<Impl>(this)) {
	boost::asio::ip::tcp::resolver resolver(RunLoop::current()->io());
	boost::asio::ip::tcp::resolver::query query(addr, port);
	boost::asio::ip::tcp::endpoint endpoint = *resolver.resolve(query);
	impl->acceptor.open(endpoint.protocol());
	impl->acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
	impl->acceptor.bind(endpoint);
	impl->acceptor.listen();

	impl->start_accept();
}

TCPAcceptor::~TCPAcceptor() { impl->close(); }

bool TCPAcceptor::accept(TCPSocket &socket, std::string &accepted_addr) {
	if (!impl->socket_ready)
		return false;
	impl->socket_ready = false;
	socket.close();
	std::swap(socket.impl->socket, impl->socket_being_accepted);

	socket.impl->connected = true;
	accepted_addr          = socket.impl->socket.remote_endpoint().address().to_string();
	socket.impl->start_read();
	impl->start_accept();
	return true;
}

class DNSResolver::Impl {
public:
	DNSResolver *owner;
	explicit Impl(DNSResolver *owner) : owner(owner), resolver(RunLoop::current()->io()) {}
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

std::vector<std::string> DNSResolver::sync_resolve(const std::string &host_name, bool ipv4, bool ipv6) {
	boost::asio::ip::tcp::resolver resolver(RunLoop::current()->io());
	boost::asio::ip::tcp::resolver::query query(host_name, "http");
	boost::asio::ip::tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);
	std::vector<std::string> names;
	for (; endpoint_iterator != boost::asio::ip::tcp::resolver::iterator(); ++endpoint_iterator) {
		names.push_back(endpoint_iterator->endpoint().address().to_string());
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
