// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <algorithm>
#include <iostream>
#include <sstream>
#include "client_request.hpp"

namespace crab { namespace http {

CRAB_INLINE ClientRequestSimple::ClientRequestSimple() : ClientRequestSimple([](Response &&) {}, [](std::string &&) {}) {}

CRAB_INLINE ClientRequestSimple::ClientRequestSimple(R_handler &&r_handler, E_handler &&e_handler)
    : r_handler(std::move(r_handler))
    , e_handler(std::move(e_handler))
    , connection([this]() { on_connection(); })
    , timeout_timer([this]() { on_timeout_timer(); }) {}

CRAB_INLINE void ClientRequestSimple::set_handlers(R_handler &&r_h, E_handler &&e_h) {
	r_handler = std::move(r_h);
	e_handler = std::move(e_h);
}

CRAB_INLINE void ClientRequestSimple::send(Request &&request, uint16_t port, const std::string &protocol) {
	if (connection.is_open() &&
	    (request.header.host != connection.get_host() || port != connection.get_port() || protocol != connection.get_protocol()))
		connection.close();
	timeout_timer.cancel();
	if (!connection.is_open())
		connection.connect(request.header.host, port, protocol);
	connection.write(std::move(request));
	requesting = true;
}

CRAB_INLINE void ClientRequestSimple::send(const std::string &uri_str, Request &&request) {
	URI uri                     = parse_uri(uri_str);
	request.header.host         = uri.host;
	request.header.path         = uri.path;
	request.header.query_string = uri.query;
	if (!uri.user_info.empty())
		request.header.basic_authorization = base64::encode(uint8_cast(uri.user_info.data()), uri.user_info.size());
	if (uri.port.empty()) {
		if (uri.scheme == string_view{"http"})
			uri.port = "80";
		else if (uri.scheme == string_view{"https"})
			uri.port = "443";
		else
			throw std::runtime_error("port is empty, while scheme unknown - impossible to guess");
	}
	send(std::move(request), integer_cast<uint16_t>(uri.port), uri.scheme);
}

CRAB_INLINE void ClientRequestSimple::get(const std::string &uri_str, Request &&request) {
	request.header.method = "GET";
	send(uri_str, std::move(request));
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
	if (!connection.is_open())
		return on_connection_close();
	Response response;
	if (!connection.read_next(response))
		return;
	if (!requesting)
		return;  // Should not be possible
	requesting = false;
	timeout_timer.once(keep_connection_timeout_sec);
	r_handler(std::move(response));
}

CRAB_INLINE void ClientRequestSimple::on_timeout_timer() { connection.close(); }

}}  // namespace crab::http
