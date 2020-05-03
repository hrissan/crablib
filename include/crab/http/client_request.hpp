// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#pragma once

#include <list>
#include <map>
#include "connection.hpp"

namespace crab { namespace http {

// Creates and maintains a single connection per request
class ClientRequestSimple {
public:
	typedef std::function<void(Response &&)> R_handler;
	typedef std::function<void(std::string &&)> E_handler;

	explicit ClientRequestSimple(R_handler &&r_handler, E_handler &&e_handler);

	void send(Request &&request, uint16_t port, const std::string &protocol);
	void send_get(const std::string &uri_str, Request &&request = Request{});

	void cancel();  // after cancel you are guaranteed that no handlers will be called
	bool is_open() const { return requesting; }

	enum { keep_connection_timeout_sec = 10 };  // To keep connection after receiving response
private:
	void on_connection_close();
	void on_connection();
	void on_timeout_timer();

	R_handler r_handler;
	E_handler e_handler;

	ClientConnection connection;
	Timer timeout_timer;  // connection is not closed immediately, anticipating more requests
	bool requesting = false;
};

// WebBrowser-like, creates and maintains (for small period) predefined # of connections to each host
class ClientRequestPooled {
public:
	struct HostPortProtocol {
		std::string host;
		uint16_t port = 0;
		std::string protocol;

		bool operator<(const HostPortProtocol &other) const {
			return std::tie(host, port, protocol) < std::tie(other.host, other.port, other.protocol);
		}
	};
	class Pool {
	public:
		Pool() {}
		~Pool() {}

	private:
		struct Entry {
			ClientConnection connection;
			Timer timeout_timer;  // connection is not closed immediately, anticipating more requests
		};
		std::map<HostPortProtocol, std::list<Entry>> entries;
	};
	ClientRequestPooled(
	    Pool *pool, ClientRequestSimple::R_handler &&r_handler, ClientRequestSimple::E_handler &&e_handler) {}
	~ClientRequestPooled() {}

private:
};

}}  // namespace crab::http
