// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <algorithm>
#include <iostream>
#include <sstream>
#include "client_request.hpp"

namespace crab { namespace http {

CRAB_INLINE ClientRequestSimple::ClientRequestSimple(R_handler &&r_handler, E_handler &&e_handler)
    : r_handler(std::move(r_handler))
    , e_handler(std::move(e_handler))
    , connection([this]() { on_connection(); }, [this]() { on_connection_close(); })
    , timeout_timer([this]() { on_timeout_timer(); }) {}

CRAB_INLINE void ClientRequestSimple::send(Request &&request, uint16_t port, const std::string &protocol) {
	if (connection.is_open() && (request.header.host != connection.get_host() || port != connection.get_port() ||
	                                protocol != connection.get_protocol()))
		connection.close();
	timeout_timer.cancel();
	if (!connection.is_open())
		connection.connect(request.header.host, port, protocol);
	connection.write(std::move(request));
	requesting = true;
}

CRAB_INLINE void ClientRequestSimple::cancel() {
	if (!requesting)
		return;
	requesting = false;
	// Have to close connection
	connection.close();
	timeout_timer.cancel();
}

CRAB_INLINE void ClientRequestSimple::on_connection_close() {
	timeout_timer.cancel();
	if (!requesting)
		return;
	requesting = false;
	e_handler("disconnect");
}

CRAB_INLINE void ClientRequestSimple::on_connection() {
	Response response;
	if (!connection.read_next(response))
		return;
	if (!requesting)
		return;  // Should not be possible
	requesting = false;
	r_handler(std::move(response));
	timeout_timer.once(10);  // TODO constant
}

CRAB_INLINE void ClientRequestSimple::on_timeout_timer() { connection.close(); }

}}  // namespace crab::http
