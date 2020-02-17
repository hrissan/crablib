// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <algorithm>
#include <iostream>
#include <random>
#include <sstream>
#include "../crypto/base64.hpp"
#include "web_socket.hpp"

namespace crab { namespace http {

CRAB_INLINE WebSocket::WebSocket(Handler &&r_handler, Handler &&d_handler) {
	set_handlers(std::move(r_handler), std::move(d_handler));

	std::random_device rd;
	std::seed_seq sd{rd(), rd(), rd(), rd(), rd(), rd(), rd(), rd()};
	masking_key_random.seed(sd);
}

CRAB_INLINE bool WebSocket::connect(const std::string &address, uint16_t port, const RequestHeader &rh) {
	close();
	http::RequestBody req;
	req.r                       = rh;
	req.r.http_version_major    = 1;
	req.r.http_version_minor    = 1;
	req.r.connection_upgrade    = true;
	req.r.method                = "GET";
	req.r.upgrade_websocket     = true;
	req.r.sec_websocket_version = "13";
	//	std::independent_bits_engine<std::random_device, 8, uint32_t> rde;
	// uint8_t is not allowed by MSVC
	uint8_t rdata[16]{};
	for (size_t i = 0; i != sizeof(rdata); ++i)
		rdata[i] = masking_key_random();
	req.r.sec_websocket_key = base64::encode(rdata, sizeof(rdata));

	//	data_to_write.write(rd.data(), rd.size());
	if (!Connection::connect(address, port))
		return false;
	Connection::write(std::move(req));
	return true;
}

}}  // namespace crab::http