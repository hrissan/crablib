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
#include "types.hpp"

namespace crab { namespace http {

class ErrorAuthorization : public std::runtime_error {
public:
	explicit ErrorAuthorization(std::string realm)
	    : std::runtime_error("Error Authorization Required"), realm(std::move(realm)) {}
	std::string realm;
};

class Server;

class Client : protected Connection {  // So the type is opaque for users
public:
	using Connection::get_peer_address;
	void write(ResponseBody &&response);
	void write(WebMessage &&wm) { Connection::write(std::move(wm)); }
	using Connection::web_socket_upgrade;

private:
	friend class Server;
};

class Server {
public:
	typedef std::function<bool(Client *who, RequestBody &&, ResponseBody &)> R_handler;
	// return false if no reply should be sent (long poll, deferred serving).
	// remember client and later call Server::write() to finish request
	typedef std::function<void(Client *who)> D_handler;
	// remove remembered client/websocket. Do not do anything with who, except using as an opaque key

	typedef std::function<void(Client *who, WebMessage &&)> W_handler;
	// only messages with opcodes text and binary are sent to handler, other processed automatically

	explicit Server(const Address &address);
	explicit Server(uint16_t port) : Server(Address("0.0.0.0", port)) {}
	~Server();
	// Unlike other parts of crab, you must not destroy server in handlers
	// when destroying server, remove all Client * you remembered

	static const std::string &get_date();

	R_handler r_handler;  // Request
	D_handler d_handler;  // Disconnect
	W_handler w_handler;  // Web Message
private:
	struct TimeCache {
		std::chrono::system_clock::time_point cached_time_point{};
		std::string cached_date;
	};
	using CurrentTimeCache = crab::details::StaticHolderTL<TimeCache>;

	TCPAcceptor la_socket;

	std::list<Client> clients;

	void on_client_handler(std::list<Client>::iterator it);
	void on_client_disconnected(std::list<Client>::iterator it);
	void accept_all();

	void on_client_handle_request(Client *who, RequestBody &&);
	void on_client_handle_message(Client *who, WebMessage &&);
};

}}  // namespace crab::http
