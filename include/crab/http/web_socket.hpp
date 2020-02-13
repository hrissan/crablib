// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#pragma once

#include <deque>
#include "../network.hpp"
#include "../streams.hpp"
#include "connection.hpp"

namespace crab { namespace http {

class WebSocket : private Connection {
public:
	explicit WebSocket(Handler &&r_handler, Handler &&d_handler);

	using Connection::close;
	// after close you are guaranteed that no handlers will be called
	using Connection::is_open;

	bool connect(const std::string &address, uint16_t port, const RequestHeader &rh);
	// At list fill host, uri, authorization
	// either returns false or returns true and will call r_handler or d_handler in future

	void write(WebMessage &&wm) { Connection::write(std::move(wm)); }
	bool read_next(WebMessage &wm) { return Connection::read_next(wm); }

private:
};

}}  // namespace crab::http
