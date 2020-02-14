// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <algorithm>
#include <array>
#include <iostream>
#include "../network.hpp"
#include "request_parser.hpp"
#include "server.hpp"

#include "connection.hpp"

// to test
// httperf --port 8090 --num-calls 100000 --uri /index.html
// curl -s "http://127.0.0.1:8888/?[1-10000]"
// USELESS siege -b -r 1000 -c 100 http://127.0.0.1:8888/index.html
// USELESS ab -n 100000 -c 50 -k localhost:8090/index.html
// wrk -t2 -c20 -d5s http://127.0.0.1:8888/index.html
// gobench -t 5 -c 128 -u http://127.0.0.1:7000/index.html

namespace crab { namespace http {

class Client : public Connection {  // So the type is opaque for users
public:
	Client() = default;
};

namespace details {
CRAB_INLINE bool empty_r_handler(Client *, RequestBody &&, ResponseBody &) { return true; }
CRAB_INLINE void empty_d_handler(Client *) {}
CRAB_INLINE void empty_w_handler(Client *, WebMessage &&) {}
}  // namespace details

CRAB_INLINE Server::Server(const std::string &address, uint16_t port)
    : r_handler(details::empty_r_handler)
    , d_handler(details::empty_d_handler)
    , w_handler(details::empty_w_handler)
    , la_socket{address, port, std::bind(&Server::accept_all, this)} {
	set_date(std::chrono::system_clock::now());
}

CRAB_INLINE Server::~Server() = default;  // we use incomplete types

CRAB_INLINE const std::string &Server::get_date() const {
	auto now = std::chrono::system_clock::now();
	if (std::chrono::duration_cast<std::chrono::seconds>(now - cached_time_point).count() > 500)
		const_cast<Server *>(this)->set_date(now);
	return cached_date;
}

CRAB_INLINE void Server::set_date(const std::chrono::system_clock::time_point &now) {
	cached_time_point    = now;
	std::time_t end_time = std::chrono::system_clock::to_time_t(now);
	struct ::tm tm {};
	gmtime_r(&end_time, &tm);
	char buf[64]{};  // "Wed, 16 Oct 2019 16:68:22 GMT"
	strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S %Z", &tm);
	cached_date = buf;
}

CRAB_INLINE void Server::on_client_handler(std::list<Client>::iterator it) {
	Client *who = &*it;
	WebMessage message;
	RequestBody request;
	while (true) {
		if (who->read_next(message)) {
			on_client_handle_message(who, std::move(message));
		} else if (who->read_next(request)) {
			on_client_handle_request(who, std::move(request));
		} else
			break;
	}
}

CRAB_INLINE void Server::accept_all() {
	while (la_socket.can_accept()) {  // && clients.size() < max_incoming_connections &&
		clients.emplace_back();
		auto it = --clients.end();
		it->set_handlers([this, it]() { on_client_handler(it); }, [this, it]() { on_client_disconnected(it); });
		std::string addr;
		it->accept(la_socket, &addr);
		//        std::cout << "HTTP Client accepted=" << cid << " addr=" << addr << std::endl;
	}
}

CRAB_INLINE void Server::write(Client *who, ResponseBody &&response) {
	// HTTP message length design is utter crap, we should conform better...
	// https://www.w3.org/Protocols/rfc2616/rfc2616-sec4.html#sec4.4
	if (response.r.date.empty())
		response.r.date = get_date();
	who->write(std::move(response));
}

CRAB_INLINE void Server::web_socket_upgrade(Client *who, RequestBody &&request) {
	if (!request.r.is_websocket_upgrade())
		throw std::runtime_error("Attempt to upgrade non-upgradable connection");

	ResponseBody response;

	response.r.connection_upgrade   = request.r.connection_upgrade;
	response.r.upgrade_websocket    = request.r.upgrade_websocket;
	response.r.sec_websocket_accept = ResponseHeader::generate_sec_websocket_accept(request.r.sec_websocket_key);
	response.r.status               = 101;

	http::Server::write(who, std::move(response));
}

CRAB_INLINE void Server::write(Client *who, WebMessage &&message) { who->write(std::move(message)); }

CRAB_INLINE void Server::on_client_disconnected(std::list<Client>::iterator it) {
	Client *who = &*it;
	clients.erase(it);
	d_handler(who);
}

// Big TODO - reverse r_handler logic, so that user must explicitly call
// save_for_longpoll(), and if they do not, 404 response will be sent

CRAB_INLINE void Server::on_client_handle_request(Client *who, RequestBody &&request) {
	ResponseBody response;
	response.r.status = 404;
	bool result       = true;
	try {
		if (r_handler)
			result = r_handler(who, std::move(request), response);
	} catch (const ErrorAuthorization &ex) {
		// std::cout << "HTTP unauthorized request" << std::endl;
		response.r.headers.push_back({"WWW-Authenticate", "Basic realm=\"" + ex.realm + "\", charset=\"UTF-8\""});
		response.r.status = 401;
	} catch (const std::exception &ex) {
		// TODO - hope we do not leak security messages there
		// std::cout << "HTTP request leads to throw/catch, what=" << ex.what() << std::endl;
		// TODO - error handler, so that error can be written to log
		response.r.status = 422;
		response.set_body(ex.what());
	}
	if (result) {
		if (response.r.status == 404 && !response.r.has_content_length()) {
			response.r.content_type = "text/plain; charset=utf-8";
			response.set_body("404 not found");
		}
		write(who, std::move(response));
	}
}

CRAB_INLINE void Server::on_client_handle_message(Client *who, WebMessage &&message) {
	try {
		if (w_handler)
			w_handler(who, std::move(message));
	} catch (const std::exception &ex) {
		std::cout << "HTTP socket request leads to throw/catch, what=" << ex.what() << std::endl;
		who->write(WebMessage(WebMessage::OPCODE_CLOSE, ex.what()));
		// TODO - hope we do not leak security messages there
	}
}

}}  // namespace crab::http
