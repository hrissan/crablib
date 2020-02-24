// Copyright (c) 2003-2017 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <cstring>
#include <sstream>
#include "../crypto/base64.hpp"
#include "../crypto/sha1.hpp"
#include "types.hpp"

namespace crab { namespace http {

CRAB_INLINE const std::string &status_to_string(int status) {
	struct smapping {
		int code;
		std::string text;
	};
	static const smapping smappings[]       = {{101, "Switching Protocols"}, {200, "OK"}, {400, "Bad request"},
        {401, "Unauthorized"}, {403, "Forbidden"}, {404, "Not found"}, {422, "Unprocessable Entity"},
        {500, "Internal Error"}, {501, "Not implemented"}, {502, "Service temporarily overloaded"},
        {503, "Gateway timeout"}};
	static const std::string unknown_status = "Unknown";

	for (const auto &m : smappings)
		if (m.code == status)
			return m.text;
	return unknown_status;
}

CRAB_INLINE bool is_sp(int c) { return c == ' ' || c == '\t'; }

CRAB_INLINE bool is_char(int c) { return c >= 0 && c <= 127; }

CRAB_INLINE bool is_ctl(int c) { return (c >= 0 && c <= 31) || (c == 127); }

CRAB_INLINE bool is_tspecial(int c) {
	switch (c) {
	case '(':
	case ')':
	case '<':
	case '>':
	case '@':
	case ',':
	case ';':
	case ':':
	case '\\':
	case '"':
	case '/':
	case '[':
	case ']':
	case '?':
	case '=':
	case '{':
	case '}':
	case ' ':
	case '\t':
		return true;
	default:
		return false;
	}
}

CRAB_INLINE bool is_digit(int c) { return c >= '0' && c <= '9'; }

CRAB_INLINE void trim_right(std::string &str) {
	// We have no backtracking, so cheat here
	while (!str.empty() && is_sp(str.back()))
		str.pop_back();
}

CRAB_INLINE std::string RequestHeader::content_mime_type() {
	std::string result = content_type.substr(0, content_type.find(';'));
	for (auto &c : result)
		c = std::tolower(c);
	return result;
}

CRAB_INLINE bool RequestHeader::is_websocket_upgrade() const {
	return method == "GET" && connection_upgrade && upgrade_websocket && !sec_websocket_key.empty() &&
	       sec_websocket_version == "13";
}

CRAB_INLINE void RequestHeader::set_uri(const std::string &uri) {
	auto pos = uri.find('?');
	if (pos == std::string::npos) {
		path = uri;
	} else {
		path         = uri.substr(0, pos);
		query_string = uri.substr(pos + 1);
	}
}

CRAB_INLINE std::string RequestHeader::get_uri() const {
	return query_string.empty() ? query_string : path + "?" + query_string;
}

CRAB_INLINE std::string RequestHeader::to_string() const {
	std::stringstream ss;
	ss << method << " " << path;  // TODO - uri-encode
	if (!query_string.empty())
		ss << "?" << query_string;  // TODO - uri-encode
	ss << " "
	   << "HTTP/" << http_version_major << "." << http_version_minor << "\r\n";
	if (!host.empty())
		ss << "Host: " << host << "\r\n";
	if (!origin.empty())
		ss << "Origin: " << origin << "\r\n";
	for (auto &&h : headers)
		ss << h.name << ": " << h.value << "\r\n";
	if (!basic_authorization.empty())
		ss << "Authorization: Basic " << basic_authorization << "\r\n";
	if (http_version_major == 1 && http_version_minor == 0 && keep_alive) {
		ss << "Connection: keep-alive\r\n";
	} else if (connection_upgrade && upgrade_websocket) {
		ss << "Connection: upgrade\r\n";
		ss << "Upgrade: websocket\r\n";
		if (!sec_websocket_key.empty())
			ss << "Sec-WebSocket-Key: " << sec_websocket_key << "\r\n";
		if (!sec_websocket_version.empty())
			ss << "Sec-WebSocket-Version: " << sec_websocket_version << "\r\n";
	}
	if (!content_type.empty())
		ss << "Content-Type: " << content_type << "\r\n";
	if (has_content_length()) {
		ss << "Content-Length: " << content_length << "\r\n\r\n";
	} else {
		ss << "\r\n";
	}
	if (!transfer_encoding.empty() && transfer_encoding_chunked)
		ss << "Transfer-Encoding: " << transfer_encoding << ", chunked\r\n";
	if (transfer_encoding.empty() && transfer_encoding_chunked)
		ss << "Transfer-Encoding: chunked\r\n";
	if (!transfer_encoding.empty() && !transfer_encoding_chunked)
		ss << "Transfer-Encoding: " << transfer_encoding << "\r\n";
	return ss.str();
}

CRAB_INLINE std::string ResponseHeader::content_mime_type() {
	std::string result = content_type.substr(0, content_type.find(';'));
	for (auto &c : result)
		c = std::tolower(c);
	return result;
}

CRAB_INLINE bool ResponseHeader::is_websocket_upgrade() const {
	return status == 101 && connection_upgrade && upgrade_websocket && !sec_websocket_accept.empty();
}

CRAB_INLINE std::string ResponseHeader::to_string() const {
	std::stringstream ss;
	ss << "HTTP/" << http_version_major << "." << http_version_minor << " " << status << " "
	   << (status_text.empty() ? status_to_string(status) : status_text) << "\r\n";
	for (auto &&h : headers)
		ss << h.name << ": " << h.value << "\r\n";
	if (http_version_major == 1 && http_version_minor == 0 && keep_alive) {
		ss << "Connection: keep-alive\r\n";
	} else if (connection_upgrade && upgrade_websocket) {
		ss << "Connection: upgrade\r\n";
		ss << "Upgrade: websocket\r\n";
		if (!sec_websocket_accept.empty())
			ss << "Sec-WebSocket-Accept: " << sec_websocket_accept << "\r\n";
	}
	if (!content_type.empty())
		ss << "Content-Type: " << content_type << "\r\n";
	if (!date.empty())
		ss << "Date: " << date << "\r\n";
	if (has_content_length()) {
		ss << "Content-Length: " << content_length << "\r\n\r\n";
	} else {
		ss << "\r\n";
	}
	if (!transfer_encoding.empty() && transfer_encoding_chunked)
		ss << "Transfer-Encoding: " << transfer_encoding << ", chunked\r\n";
	if (transfer_encoding.empty() && transfer_encoding_chunked)
		ss << "Transfer-Encoding: chunked\r\n";
	if (!transfer_encoding.empty() && !transfer_encoding_chunked)
		ss << "Transfer-Encoding: " << transfer_encoding << "\r\n";
	return ss.str();
}

CRAB_INLINE std::string ResponseHeader::generate_sec_websocket_accept(const std::string &sec_websocket_key) {
	static const char guid[] = {"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"};
	uint8_t result[sha1::hash_size]{};
	sha1 hash;
	hash.add(sec_websocket_key.data(), sec_websocket_key.size());
	hash.add(guid, sizeof(guid) - 1);
	hash.finalize(result);

	return base64::encode(result, sha1::hash_size);
}

}}  // namespace crab::http
