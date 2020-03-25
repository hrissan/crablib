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

CRAB_INLINE void trim_right(std::string &str) {
	// We have no backtracking, so cheat here
	while (!str.empty() && is_sp(str.back()))
		str.pop_back();
}

CRAB_INLINE void tolower(std::string &str) {
	for (auto &c : str)
		c = std::tolower(c);
}

CRAB_INLINE void parse_content_type_value(const std::string &value, std::string &mime, std::string &suffix) {
	size_t start = value.find_first_of("; \t", 0, 3);
	mime         = value.substr(0, start);
	tolower(mime);
	while (start < value.size() && is_sp(value[start]))
		start += 1;
	if (start < value.size() && value[start] == ';')
		start += 1;  // We simply allow whitespaces instead of ;
	while (start < value.size() && is_sp(value[start]))
		start += 1;
	suffix = (start == std::string::npos) ? std::string{} : value.substr(start);
}

CRAB_INLINE bool parse_authorization_basic(const std::string &value, std::string &auth) {
	if (value.size() < 6 || std::tolower(value[0]) != 'b' || std::tolower(value[1]) != 'a' ||
	    std::tolower(value[2]) != 's' || std::tolower(value[3]) != 'i' || std::tolower(value[4]) != 'c' ||
	    !is_sp(value[5]))
		return false;
	size_t start = 6;
	while (start < value.size() && is_sp(value[start]))
		start += 1;
	auth = value.substr(start);
	return true;
}

CRAB_INLINE void RequestResponseHeader::set_content_type(const std::string &content_type) {
	parse_content_type_value(content_type, content_type_mime, content_type_suffix);
}

CRAB_INLINE void RequestResponseHeader::set_content_type(const std::string &mime, const std::string &suffix) {
	content_type_mime   = mime;
	content_type_suffix = suffix;
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

namespace details {

CRAB_INLINE void to_string_common(const RequestResponseHeader &req, std::stringstream &ss) {
	if (!req.content_type_mime.empty()) {
		ss << "content-type: " << req.content_type_mime;
		if (!req.content_type_suffix.empty())
			ss << "; " << req.content_type_suffix;
		ss << "\r\n";
	}
	if (req.has_content_length()) {
		ss << "content-length: " << req.content_length << "\r\n";
	}
	if (req.http_version_major == 1 && req.http_version_minor == 0 && req.keep_alive) {
		ss << "connection: keep-alive\r\n";
	} else if (req.http_version_major == 1 && req.http_version_minor == 1 && !req.keep_alive) {
		ss << "connection: close\r\n";
	} else if (req.connection_upgrade && req.upgrade_websocket) {
		ss << "connection: upgrade\r\n";
		ss << "upgrade: websocket\r\n";
	}
	if (!req.transfer_encodings.empty() || req.transfer_encoding_chunked) {
		ss << "transfer-encoding:";
		size_t pos = 0;
		for (const auto &te : req.transfer_encodings)
			ss << (pos++ ? ", " : " ") << te;
		if (req.transfer_encoding_chunked)
			ss << (pos++ ? ", chunked" : " chunked");
		ss << "\r\n";
	}
	for (auto &&h : req.headers)
		ss << h.name << ": " << h.value << "\r\n";
}

CRAB_INLINE std::string simple_response(int status, const char *pf, const std::string *body, const char *sf) {
	std::stringstream ss;
	ss << pf;
	if (body)
		ss << *body;
	else
		ss << status << " " << status_to_string(status);
	ss << sf;
	return ss.str();
}

}  // namespace details

CRAB_INLINE std::string RequestHeader::to_string() const {
	std::stringstream ss;
	ss << method << " " << path;  // TODO - uri-encode path
	if (!query_string.empty())
		ss << "?" << query_string;  // query_string must be already encoded
	ss << " HTTP/" << http_version_major << "." << http_version_minor << "\r\n";
	if (!host.empty())
		ss << "host: " << host << "\r\n";
	if (!origin.empty())
		ss << "origin: " << origin << "\r\n";
	if (!basic_authorization.empty())
		ss << "authorization: basic " << basic_authorization << "\r\n";
	details::to_string_common(*this, ss);
	if (!sec_websocket_key.empty())
		ss << "sec-websocket-key: " << sec_websocket_key << "\r\n";
	if (!sec_websocket_version.empty())
		ss << "sec-websocket-version: " << sec_websocket_version << "\r\n";
	ss << "\r\n";
	return ss.str();
}

CRAB_INLINE bool ResponseHeader::is_websocket_upgrade() const {
	return status == 101 && connection_upgrade && upgrade_websocket && !sec_websocket_accept.empty();
}

CRAB_INLINE std::string ResponseHeader::to_string() const {
	std::stringstream ss;
	ss << "HTTP/" << http_version_major << "." << http_version_minor << " " << status << " "
	   << (status_text.empty() ? status_to_string(status) : status_text) << "\r\n";
	if (!date.empty())
		ss << "date: " << date << "\r\n";
	details::to_string_common(*this, ss);
	if (!sec_websocket_accept.empty())
		ss << "sec-websocket-accept: " << sec_websocket_accept << "\r\n";
	ss << "\r\n";
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

CRAB_INLINE std::unordered_map<std::string, std::string> Request::parse_query_params() const {
	QueryParser p;
	p.parse(header.query_string);
	p.parse_end();
	if (header.method == Literal{"GET"})  // All other methods support params in body
		return std::move(p.parsed);
	// TODO - multipart data
	if (header.content_type_mime != Literal{"application/x-www-form-urlencoded"})
		return;
	p.parse(body);
	p.parse_end();
	return std::move(p.parsed);
}

CRAB_INLINE std::unordered_map<std::string, std::string> Request::parse_cookies() const {
	CookieParser p;
	for (const auto &h : header.headers)
		if (h.name == Literal{"cookie"}) {
			p.parse(h.value);
			p.parse_end();
		}
	return std::move(p.parsed);
}

CRAB_INLINE Response Response::simple(int status, const std::string &content_type, std::string &&body) {
	Response response;
	response.header.add_headers_nocache();
	response.header.status = status;
	response.header.set_content_type(content_type);
	response.set_body(std::move(body));
	return response;
}

CRAB_INLINE Response Response::simple_html(int status, std::string &&text) {
	return simple(status,
	    "text/html; charset=utf-8",
	    details::simple_response(status, "<html><body>", &text, "</body></html>"));
}

CRAB_INLINE Response Response::simple_html(int status) {
	return simple(status,
	    "text/html; charset=utf-8",
	    details::simple_response(status, "<html><body>", nullptr, "</body></html>"));
}

CRAB_INLINE Response Response::simple_text(int status, std::string &&text) {
	return simple(status, "text/plain; charset=utf-8", details::simple_response(status, "", &text, ""));
}

CRAB_INLINE Response Response::simple_text(int status) {
	return simple(status, "text/plain; charset=utf-8", details::simple_response(status, "", nullptr, ""));
}

}}  // namespace crab::http
