// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <algorithm>
#include <array>
#include <iostream>
#include "../network.hpp"
#include "request_parser.hpp"
#include "server.hpp"

// to test
// httperf --port 8090 --num-calls 100000 --uri /index.html
// curl -s "http://127.0.0.1:8888/?[1-10000]"
// USELESS siege -b -r 1000 -c 100 http://127.0.0.1:8888/index.html
// USELESS ab -n 100000 -c 50 -k localhost:8090/index.html
// wrk -t2 -c20 -d5s http://127.0.0.1:8888/index.html
// gobench -t 5 -c 128 -u http://127.0.0.1:7000/index.html

namespace crab { namespace http {

namespace details {
CRAB_INLINE bool empty_r_handler(Client *, RequestBody &&, ResponseBody &) { return true; }
CRAB_INLINE void empty_d_handler(Client *) {}
CRAB_INLINE void empty_w_handler(Client *, WebMessage &&) {}
}  // namespace details

CRAB_INLINE void Client::write(ResponseBody &&response) {
	// HTTP message length design is utter crap, we should conform better...
	// https://www.w3.org/Protocols/rfc2616/rfc2616-sec4.html#sec4.4
	if (response.r.date.empty())
		response.r.date = Server::get_date();
	Connection::write(std::move(response));
}

CRAB_INLINE Server::Server(const std::string &address, uint16_t port)
    : r_handler(details::empty_r_handler)
    , d_handler(details::empty_d_handler)
    , w_handler(details::empty_w_handler)
    , la_socket{address, port, std::bind(&Server::accept_all, this)} {}

CRAB_INLINE Server::~Server() = default;  // we use incomplete types

CRAB_INLINE const std::string &Server::get_date() {
	auto &inst = CurrentTimeCache::instance;
	auto now   = std::chrono::system_clock::now();
	if (std::chrono::duration_cast<std::chrono::seconds>(now - inst.cached_time_point).count() > 500) {
		inst.cached_time_point = now;
		std::time_t end_time   = std::chrono::system_clock::to_time_t(now);
		struct ::tm tm {};
		gmtime_r(&end_time, &tm);
		char buf[64]{};  // "Wed, 16 Oct 2019 16:68:22 GMT"
		strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S %Z", &tm);
		inst.cached_date = buf;
	}
	return inst.cached_date;
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
		it->accept(la_socket);
		//        std::cout << "HTTP Client accepted=" << cid << " addr=" << (*it)->get_peer_address() << std::endl;
	}
}

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
		who->write(std::move(response));
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
