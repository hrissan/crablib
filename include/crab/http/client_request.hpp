// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#pragma once

#include "connection.hpp"

namespace crab { namespace http {

class ClientRequestSimple {
public:
	typedef std::function<void(Response &&)> R_handler;
	typedef std::function<void(std::string &&)> E_handler;

	explicit ClientRequestSimple(R_handler &&r_handler, E_handler &&e_handler);

	void send(Request &&request, uint16_t port, const std::string &protocol);
	void cancel();  // after cancel you are guaranteed that no handlers will be called
	bool is_open() const { return requesting; }

protected:
	void on_connection_close();
	void on_connection();
	void on_timeout_timer();

	R_handler r_handler;
	E_handler e_handler;

	ClientConnection connection;
	Timer timeout_timer;  // connection is not closed immediately, anticipating more requests
	bool requesting = false;
};

}}  // namespace crab::http
