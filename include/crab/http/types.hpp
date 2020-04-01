// Copyright (c) 2003-2017 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#pragma once

#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

// TODO - cookie https://stackoverflow.com/questions/1969232/what-are-allowed-characters-in-cookies
// TODO - query string

namespace crab { namespace http {

struct Header {
	std::string name;
	std::string value;
};

struct RequestResponseHeader {
	int http_version_major = 1;
	int http_version_minor = 1;

	std::vector<Header> headers;  // names are lower-case

	bool keep_alive         = true;
	uint64_t content_length = std::numeric_limits<uint64_t>::max();
	bool has_content_length() const { return content_length != std::numeric_limits<uint64_t>::max(); }

	bool transfer_encoding_chunked = false;
	std::vector<std::string> transfer_encodings;  // lower-case, other than chunked, identity

	bool connection_upgrade = false;
	bool upgrade_websocket  = false;  // Upgrade: WebSocket

	std::string content_type_mime;    // lower-case
	std::string content_type_suffix;  // after ";"
	void set_content_type(const std::string &content_type);
	void set_content_type(const std::string &mime, const std::string &suffix);
};

struct RequestHeader : public RequestResponseHeader {
	std::string method;
	std::string path;          // URL-decoded automatically on parse, encoded on send
	std::string query_string;  // not URL-decoded (would otherwise lose separators)

	std::string basic_authorization;
	std::string host;
	std::string origin;

	std::string sec_websocket_key;
	std::string sec_websocket_version;

	bool is_websocket_upgrade() const;

	void set_uri(const std::string &uri);
	std::string get_uri() const;

	std::string to_string() const;
};

struct WebMessageChunk {
	bool fin             = false;
	bool mask            = false;
	int opcode           = 0;
	uint32_t masking_key = 0;
	uint64_t payload_len = 0;
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

struct ResponseHeader : public RequestResponseHeader {
	int status = 0;
	std::string status_text;

	std::string sec_websocket_accept;

	static std::string generate_sec_websocket_accept(const std::string &sec_websocket_key);

	bool is_websocket_upgrade() const;

	std::string date;

	std::string to_string() const;

	void add_headers_nocache() {
		headers.push_back(Header{"cache-control", "no-cache, no-store, must-revalidate"});
		headers.push_back(Header{"expires", "0"});
	}
};

struct Request {
	http::RequestHeader header;
	std::string body;

	Request() = default;
	Request(const std::string &host, const std::string &method, const std::string &uri) {
		header.http_version_major = 1;
		header.http_version_minor = 1;
		header.method             = method;
		header.set_uri(uri);
		header.host = host;
	}
	void set_body(std::string &&b) {
		body                  = std::move(b);
		header.content_length = body.size();
	}
	std::unordered_map<std::string, std::string> parse_query_params() const;
	std::unordered_map<std::string, std::string> parse_cookies() const;
};

struct Response {
	http::ResponseHeader header;
	std::string body;

	void set_body(std::string &&b) {
		body                  = std::move(b);
		header.content_length = body.size();
	}

	static Response simple(int status, const std::string &content_type, std::string &&body);

	static Response simple_html(int status);
	static Response simple_html(int status, std::string &&text);
	static Response simple_text(int status);
	static Response simple_text(int status, std::string &&text);
};

bool is_sp(int c);
bool is_char(int c);
bool is_ctl(int c);
bool is_tspecial(int c);
void trim_right(std::string &str);
void tolower(std::string &str);

void parse_content_type_value(const std::string &value, std::string &mime, std::string &suffix);
bool parse_authorization_basic(const std::string &value, std::string &auth);

const std::string &status_to_string(int status);

}}  // namespace crab::http
