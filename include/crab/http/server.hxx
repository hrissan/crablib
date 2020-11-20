// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
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

CRAB_INLINE void Client::write(Response &&response) {
	// HTTP message length design is utter crap, we should conform better...
	// https://www.w3.org/Protocols/rfc2616/rfc2616-sec4.html#sec4.4
	if (response.header.date.empty())
		response.header.date = Server::get_date();  // We avoid pointer to server
	if (response.header.server.empty())
		response.header.server = "crab";
	ServerConnection::write(std::move(response));
	d_handler = nullptr;
}

CRAB_INLINE void Client::write(WebMessage &&wm) {
	if (wm.is_close())
		web_message_close_sent = true;
	ServerConnection::write(std::move(wm));
}

CRAB_INLINE void Client::write(const uint8_t *val, size_t count, BufferOptions buffer_options) {
	ServerConnection::write(val, count, buffer_options);
	body_position += count;
	if (!is_writing_body()) {
		rwd_handler = nullptr;
	}
}

CRAB_INLINE void Client::write(std::string &&ss, BufferOptions buffer_options) {
	ServerConnection::write(std::move(ss), buffer_options);
	body_position += ss.size();
	if (!is_writing_body()) {
		rwd_handler = nullptr;
	}
}

CRAB_INLINE void Client::write_last_chunk(BufferOptions bo) {
	ServerConnection::write_last_chunk(bo);
	rwd_handler = nullptr;
}

CRAB_INLINE void Client::web_socket_upgrade(WS_handler &&cb) {
	ServerConnection::web_socket_upgrade();
	d_handler              = nullptr;
	ws_handler             = std::move(cb);
	web_message_close_sent = false;
}

CRAB_INLINE void Client::postpone_response(Handler &&cb) {
	invariant(!is_state_websocket(), "After web socket upgrade you can use only web socket functions");
	d_handler = std::move(cb);
}

CRAB_INLINE void Client::start_write_stream(ResponseHeader &response, Handler &&cb) {
	if (response.date.empty())
		response.date = Server::get_date();  // We avoid pointer to server
	if (response.server.empty())
		response.server = "crab";
	ServerConnection::write(response);
	d_handler     = nullptr;
	rwd_handler   = std::move(cb);
	body_position = 0;
	rwd_handler();  // Some data might are ready, so we call handler immediately. TODO - investigate consequences
}

CRAB_INLINE void Client::start_write_stream(WebMessageOpcode opcode, Handler &&scb) {
	ServerConnection::write(opcode);
	rwd_handler   = std::move(scb);
	body_position = 0;
	rwd_handler();  // Some data might are ready, so we call handler immediately. TODO - investigate consequences
}

CRAB_INLINE Server::Server(const Address &address, const Settings &settings)
    : settings{settings}, acceptor{address, std::bind(&Server::accept_all, this), settings} {}

CRAB_INLINE Server::~Server() = default;  // we use incomplete types

CRAB_INLINE const std::string &Server::get_date() {
	using namespace std::chrono;
	auto &inst       = CurrentTimeCache::instance;
	auto now         = RunLoop::current()->now();
	const auto delta = duration_cast<steady_clock::duration>(milliseconds(500));  // Approximate
	if (now > inst.cached_time_point + delta) {
		inst.cached_time_point = now;
		std::time_t end_time   = system_clock::to_time_t(system_clock::now());
		struct ::tm tm {};
#if defined(_WIN32)
		gmtime_s(&tm, &end_time);
#else
		gmtime_r(&end_time, &tm);
#endif
		char buf[64]{};  // "Wed, 16 Oct 2019 16:68:22 GMT"
		strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S %Z", &tm);
		inst.cached_date = buf;
	}
	return inst.cached_date;
}

CRAB_INLINE void Server::on_client_handler(std::list<Client>::iterator it) {
	Client *who = &*it;
	if (!who->is_open())
		return on_client_disconnected(it);
	WebMessage message;
	Request request;
	if (who->rwd_handler)
		who->rwd_handler();
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
	while (acceptor.can_accept() && clients.size() < settings.max_connections) {
		clients.emplace_back();
		auto it = --clients.end();
		it->set_handler([this, it]() { on_client_handler(it); });
		it->accept(acceptor);
		//        std::cout << "HTTP Client accepted=" << cid << " addr=" << (*it)->get_peer_address() << std::endl;
	}
}

CRAB_INLINE void Server::on_client_disconnected(std::list<Client>::iterator it) {
	Client *who = &*it;
	if (who->d_handler)
		who->d_handler();
	if (who->rwd_handler)
		who->rwd_handler();
	if (who->ws_handler)
		who->ws_handler(WebMessage{WebMessageOpcode::CLOSE, {},
		    who->web_message_close_sent ? WebMessage::CLOSE_STATUS_NORMAL : WebMessage::CLOSE_STATUS_DISCONNECT});
	clients.erase(it);
	accept_all();  // In case we were over limit
}

// Big TODO - reverse r_handler logic, so that user must explicitly call
// save_for_longpoll(), and if they do not, 404 response will be sent

CRAB_INLINE void Server::on_client_handle_request(Client *who, Request &&request) {
	try {
		if (r_handler)
			r_handler(who, std::move(request));
	} catch (const ErrorAuthorization &ex) {
		Response response;
		response.header.headers.push_back(
		    {"WWW-Authenticate", "Basic realm=\"" + ex.realm + "\", charset=\"UTF-8\""});
		response.header.status = 401;
		who->write(std::move(response));
		return;
	} catch (const std::exception &ex) {
		if (who->get_state() == ServerConnection::RESPONSE_HEADER) {
			// TODO - hope we do not leak security messages there
			// TODO - error handler, so that error can be written to log
			who->write(Response::simple_text(422, ex.what()));
		}
		return;
	}
	if (who->get_state() == ServerConnection::RESPONSE_HEADER && !who->d_handler)
		throw std::logic_error("r_handler must either write response, call postpone_response or web_socket_upgrade");
}

CRAB_INLINE void Server::on_client_handle_message(Client *who, WebMessage &&message) {
	try {
		auto opcode = message.opcode;
		if (who->ws_handler)
			who->ws_handler(std::move(message));
		if (opcode == WebMessageOpcode::CLOSE)
			who->ws_handler = nullptr;  // So we will not send second CLOSE on disconnect
	} catch (const std::exception &ex) {
		std::cout << "HTTP socket request leads to throw/catch, what=" << ex.what() << std::endl;
		who->write(WebMessage{WebMessageOpcode::CLOSE, ex.what(), WebMessage::CLOSE_STATUS_ERROR});
		// TODO - hope we do not leak security messages there
	}
}

}}  // namespace crab::http
