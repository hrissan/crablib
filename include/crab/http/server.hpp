// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#pragma once

#include <chrono>
#include <deque>
#include <functional>
#include <list>
#include <memory>
#include <stdexcept>
#include "../network.hpp"
#include "../util.hpp"
#include "connection.hpp"
#include "query_parser.hpp"
#include "types.hpp"

namespace crab { namespace http {

class ErrorAuthorization : public std::runtime_error {
public:
	explicit ErrorAuthorization(std::string realm)
	    : std::runtime_error("Error Authorization Required"), realm(std::move(realm)) {}
	std::string realm;
};

class Server;

class Client : protected ServerConnection {  // So the type is opaque for users
public:
	using WS_handler = std::function<void(WebMessage &&)>;

	using ServerConnection::get_peer_address;

	// You can either postpone response (do not forget to write later)
	void postpone_response(Handler &&dcb);

	// Upgrade to web socket
	void web_socket_upgrade(WS_handler &&cb);

	// write the whole response
	void write(Response &&);
	void write(WebMessage &&wm);

	// start streaming response (scb will be called in socket-like fashion on all events)
	// Will fill response date (if empty), version, keep_alive
	void start_write_stream(ResponseHeader &response, Handler &&scb);
	void start_write_stream(WebMessageOpcode opcode, Handler &&scb);

	// when streaming, check if space in write buffer available, is connection closed and get write position
	using ServerConnection::can_write;
	using ServerConnection::is_open;
	uint64_t get_body_position() const { return body_position; }

	// when streaming, write data
	void write(const uint8_t *val, size_t count, BufferOptions bo = WRITE);
	void write(const char *val, size_t count, BufferOptions bo = WRITE) { write(uint8_cast(val), count, bo); }
#if __cplusplus >= 201703L
	void write(const std::byte *val, size_t count, BufferOptions bo = WRITE) { write(uint8_cast(val), count, bo); }
#endif
	void write(std::string &&ss, BufferOptions bo = WRITE);

	// finish streaming (should not be used if ResponseHeader contains Content-Length)
	void write_last_chunk(BufferOptions bo = WRITE);

private:
	WS_handler ws_handler;
	Handler d_handler;
	Handler rwd_handler;

	uint64_t body_position      = 0;
	bool web_message_close_sent = false;
	friend class Server;
};

class Server {
public:
	typedef std::function<void(Client *who, Request &&)> R_handler;
	// TODO - document new server interface
	// if you did not write response and exception is thrown, 422 will be returned
	// if you did not write full response and no exception is thrown, you are expected
	// to remember who and later call Client::write() to finish serving request

	explicit Server(const Address &address);
	explicit Server(uint16_t port) : Server(Address("0.0.0.0", port)) {}
	~Server();
	// Unlike other parts of crab, you must not destroy server in handlers
	// when destroying server, remove all Client * you remembered

	static const std::string &get_date();

	R_handler r_handler = [](Client *, Request &&) {};  // TODO - rename to request_handler

private:
	struct TimeCache {
		std::chrono::steady_clock::time_point cached_time_point{};
		std::string cached_date;
	};
	using CurrentTimeCache = details::StaticHolderTL<TimeCache>;

	TCPAcceptor la_socket;

	std::list<Client> clients;

	void on_client_handler(std::list<Client>::iterator it);
	void on_client_disconnected(std::list<Client>::iterator it);
	void accept_all();

	void on_client_handle_request(Client *who, Request &&);
	void on_client_handle_message(Client *who, WebMessage &&);
};

}}  // namespace crab::http
