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

	bool keep_alive = true;
	details::optional<uint64_t> content_length;

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

enum class WebMessageOpcode { TEXT = 1, BINARY = 2, CLOSE = 8, PING = 9, PONG = 0xA };

struct WebMessage {
	// Use WebMessageOpcode::TEXT, WebMessageOpcode::BINARY, WebMessageOpcode::CLOSE instead
	CRAB_DEPRECATED static constexpr WebMessageOpcode OPCODE_TEXT   = WebMessageOpcode::TEXT;
	CRAB_DEPRECATED static constexpr WebMessageOpcode OPCODE_BINARY = WebMessageOpcode::BINARY;
	CRAB_DEPRECATED static constexpr WebMessageOpcode OPCODE_CLOSE  = WebMessageOpcode::CLOSE;

	// according to https://tools.ietf.org/html/rfc6455#section-5.5.3
	// if CLOSE contains body, it must contain code (uint16_t BE) in the first 2 bytes of message
	// more status codes available in RFC
	static constexpr uint16_t CLOSE_STATUS_NORMAL          = 1000;
	static constexpr uint16_t CLOSE_STATUS_NO_CODE         = 1005;
	static constexpr uint16_t CLOSE_STATUS_DISCONNECT      = 1006;
	static constexpr uint16_t CLOSE_STATUS_NOT_UTF8        = 1007;
	static constexpr uint16_t CLOSE_STATUS_MESSAGE_TOO_BIG = 1009;
	static constexpr uint16_t CLOSE_STATUS_ERROR           = 1011;

	WebMessage() = default;
	explicit WebMessage(std::string body) : opcode(WebMessageOpcode::TEXT), body(std::move(body)) {}
	explicit WebMessage(WebMessageOpcode opcode) : opcode(opcode) {}
	WebMessage(WebMessageOpcode opcode, std::string body) : opcode(opcode), body(std::move(body)) {}
	WebMessage(WebMessageOpcode opcode, std::string body, uint16_t close_code)
	    : opcode(opcode), body(std::move(body)), close_code(close_code) {}

	WebMessageOpcode opcode = WebMessageOpcode::CLOSE;
	std::string body;
	uint16_t close_code = CLOSE_STATUS_NO_CODE;

	bool is_binary() const { return opcode == WebMessageOpcode::BINARY; }
	bool is_text() const { return opcode == WebMessageOpcode::TEXT; }
	bool is_close() const { return opcode == WebMessageOpcode::CLOSE; }
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
bool is_uri_reserved(int c);
void trim_right(std::string &str);
void tolower(std::string &str);

void parse_content_type_value(const std::string &value, std::string &mime, std::string &suffix);
bool parse_authorization_basic(const std::string &value, std::string &auth);

const std::string &status_to_string(int status);

}}  // namespace crab::http
