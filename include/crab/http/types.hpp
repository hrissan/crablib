// Copyright (c) 2003-2017 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#pragma once

#include <limits>
#include <string>
#include <vector>

namespace crab { namespace http {

struct Header {
	std::string name;
	std::string value;
};

struct RequestHeader {
	std::string method;
	std::string uri;
	int http_version_major = 0;
	int http_version_minor = 0;

	std::vector<Header> headers;
	std::string basic_authorization;
	std::string host;
	std::string origin;

	bool keep_alive                = true;
	size_t content_length          = std::numeric_limits<size_t>::max();
	bool transfer_encoding_chunked = false;
	std::string transfer_encoding;  // Other than chunked

	std::string content_type;
	std::string content_mime_type();  // lowercase content_type, with part after ; stripped

	bool connection_upgrade = false;
	bool upgrade_websocket  = false;  // Upgrade: WebSocket
	std::string sec_websocket_key;
	std::string sec_websocket_version;

	void set_firstline(const std::string &m, const std::string &u, int ma, int mi) {
		method             = m;
		uri                = u;
		http_version_major = ma;
		http_version_minor = mi;
	}
	bool has_content_length() const { return content_length != std::numeric_limits<size_t>::max(); }
	bool is_websocket_upgrade() const;
	std::string to_string() const;
};

struct WebMessageChunk {
	bool fin             = false;
	bool mask            = false;
	int opcode           = 0;
	uint32_t masking_key = 0;
	size_t payload_len   = 0;
};

struct WebMessage {
	static constexpr int OPCODE_TEXT   = 1;
	static constexpr int OPCODE_BINARY = 2;
	static constexpr int OPCODE_CLOSE  = 8;
	static constexpr int OPCODE_PING   = 9;
	static constexpr int OPCODE_PONG   = 0xA;
	int opcode                         = 0;
	std::string body;

	static bool is_good_opcode(int opcode) {
		if (opcode >= 3 && opcode <= 7)
			return false;
		return opcode >= 0 && opcode <= 0xA;
	}
	bool is_binary() const { return opcode == OPCODE_BINARY; }
	bool is_text() const { return opcode == OPCODE_TEXT; }
	WebMessage() = default;
	WebMessage(std::string body) : opcode(OPCODE_TEXT), body(std::move(body)) {}
	WebMessage(int opcode, std::string body) : opcode(opcode), body(std::move(body)) {}
};

struct ResponseHeader {
	int http_version_major = 0;
	int http_version_minor = 0;

	int status = 0;
	std::string status_text;
	std::vector<Header> headers;

	bool keep_alive                = true;
	size_t content_length          = std::numeric_limits<size_t>::max();
	bool transfer_encoding_chunked = false;
	std::string transfer_encoding;  // Other than chunked

	bool connection_upgrade = false;
	bool upgrade_websocket  = false;  // Upgrade: WebSocket
	std::string sec_websocket_accept;

	static std::string generate_sec_websocket_accept(const std::string &sec_websocket_key);

	bool has_content_length() const { return content_length != std::numeric_limits<size_t>::max(); }
	bool is_websocket_upgrade() const;

	std::string content_type;
	std::string content_mime_type();  // lowercase content_type, with part after ; stripped
	std::string date;

	std::string to_string() const;

	void add_headers_nocache() {
		headers.push_back(Header{"Cache-Control", "no-cache, no-store, must-revalidate"});
		headers.push_back(Header{"Expires", "0"});
	}
};

struct RequestBody {
	http::RequestHeader r;
	std::string body;

    RequestBody() = default;
    RequestBody(const std::string & host, const std::string & method, const std::string & uri) {
        r.http_version_major = 1;
        r.http_version_minor = 1;
        r.method = method;
        r.uri = uri;
        r.host = host;
    }
	void set_body(std::string &&b) {
		body             = std::move(b);
		r.content_length = body.size();
	}
};

struct ResponseBody {
	http::ResponseHeader r;
	std::string body;

	void set_body(std::string &&b) {
		body             = std::move(b);
		r.content_length = body.size();
	}
};

bool is_char(int c);
bool is_ctl(int c);
bool is_tspecial(int c);
bool is_digit(int c);

const std::string &status_to_string(int status);

}}  // namespace crab::http
