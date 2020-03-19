// Copyright (c) 2003-2017 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <sstream>
#include "../integer_cast.hpp"
#include "response_parser.hpp"

namespace crab { namespace http {

CRAB_INLINE void ResponseParser::parse(Buffer &buf) {
	auto ptr = parse(buf.read_ptr(), buf.read_ptr() + buf.read_count());
	buf.did_read(ptr - buf.read_ptr());
}

// We tolerate \n instead of \r\n according to recomendation
// https://www.w3.org/Protocols/rfc2616/rfc2616-sec19.html#sec19.3
CRAB_INLINE ResponseParser::State ResponseParser::consume(char input) {
	if (++total_length > max_total_length)
		throw std::runtime_error("HTTP Header too long - security violation");

	switch (state) {
	case HTTP_VERSION_H:
		if (input != 'H')
			throw std::runtime_error("Invalid http version, 'H' is expected");
		return HTTP_VERSION_HT;
	case HTTP_VERSION_HT:
		if (input != 'T')
			throw std::runtime_error("Invalid http version, 'T' is expected");
		return HTTP_VERSION_HTT;
	case HTTP_VERSION_HTT:
		if (input != 'T')
			throw std::runtime_error("Invalid http version, 'T' is expected");
		return HTTP_VERSION_HTTP;
	case HTTP_VERSION_HTTP:
		if (input != 'P')
			throw std::runtime_error("Invalid http version, 'P' is expected");
		return HTTP_VERSION_SLASH;
	case HTTP_VERSION_SLASH:
		if (input != '/')
			throw std::runtime_error("Invalid http version, '/' is expected");
		return HTTP_VERSION_MAJOR_START;
	case HTTP_VERSION_MAJOR_START:
		if (!isdigit(input))
			throw std::runtime_error("Invalid http version major start, must be digit");
		req.http_version_major = input - '0';
		return HTTP_VERSION_MAJOR;
	case HTTP_VERSION_MAJOR:
		if (input == '.')
			return HTTP_VERSION_MINOR_START;
		if (!isdigit(input))
			throw std::runtime_error("Invalid http version major, must be digit");
		req.http_version_major = req.http_version_major * 10 + input - '0';
		if (req.http_version_major > 1)
			throw std::runtime_error("Unsupported http version");
		return HTTP_VERSION_MAJOR;
	case HTTP_VERSION_MINOR_START:
		if (!isdigit(input))
			throw std::runtime_error("Invalid http version minor start, must be digit");
		req.http_version_minor = input - '0';
		return HTTP_VERSION_MINOR;
	case HTTP_VERSION_MINOR:
		if (is_sp(input)) {
			req.keep_alive = req.http_version_major == 1 && req.http_version_minor >= 1;
			return STATUS_CODE_1;
		}
		if (!isdigit(input))
			throw std::runtime_error("Invalid http version minor, must be digit");
		req.http_version_minor = req.http_version_minor * 10 + input - '0';
		if (req.http_version_minor > 99)
			throw std::runtime_error("Invalid http version minor, too big");
		return HTTP_VERSION_MINOR;
	case STATUS_CODE_1:
		if (is_sp(input))
			return STATUS_CODE_1;
		if (!isdigit(input))
			throw std::runtime_error("Invalid http status code symbol 1, must be digit");
		req.status = req.status * 10 + input - '0';
		return STATUS_CODE_2;
	case STATUS_CODE_2:
		if (!isdigit(input))
			throw std::runtime_error("Invalid http status code symbol 2, must be digit");
		req.status = req.status * 10 + input - '0';
		return STATUS_CODE_3;
	case STATUS_CODE_3:
		if (!isdigit(input))
			throw std::runtime_error("Invalid http status code symbol 3, must be digit");
		req.status = req.status * 10 + input - '0';
		return STATUS_CODE_SPACE;
	case STATUS_CODE_SPACE:
		// Empty status text is allowed
		if (input == '\r')
			return STATUS_LINE_LF;
		if (input == '\n')
			return FIRST_HEADER_LINE_START;
		if (!is_sp(input))
			throw std::runtime_error("Invalid http status code, space expected");
		return STATUS_TEXT_START;
	case STATUS_TEXT_START:
		if (is_sp(input))
			return STATUS_TEXT_START;
		// Fall Throught
	case STATUS_TEXT:
		if (input == '\r')
			return STATUS_LINE_LF;
		if (input == '\n')
			return FIRST_HEADER_LINE_START;
		if (is_ctl(input))
			throw std::runtime_error("Invalid character (control) in status text");
		req.status_text.push_back(input);
		return STATUS_TEXT;
	case STATUS_LINE_LF:
		if (input != '\n')
			throw std::runtime_error("Newline is expected");
		return FIRST_HEADER_LINE_START;
	case FIRST_HEADER_LINE_START:  // Cannot contain LWS
		trim_right(req.status_text);
		req.headers.reserve(20);
		if (input == '\r')
			return FINAL_LF;
		if (input == '\n')
			return GOOD;
		if (!is_char(input) || is_ctl(input) || is_tspecial(input))
			throw std::runtime_error("Invalid character at header line start");
		header.name.push_back(std::tolower(input));
		return HEADER_NAME;
	case HEADER_LINE_START:
		if (is_sp(input)) {
			header.value.push_back(input);
			return HEADER_VALUE;  // value continuation
		}
		process_ready_header();
		header.name.clear();
		header.value.clear();
		if (input == '\r')
			return FINAL_LF;
		if (input == '\n')
			return GOOD;
		if (!is_char(input) || is_ctl(input) || is_tspecial(input))
			throw std::runtime_error("Invalid character at header line start");
		header.name.push_back(std::tolower(input));
		return HEADER_NAME;
	case HEADER_NAME:
		// We relax https://tools.ietf.org/html/rfc7230#section-3.2.4
		if (is_sp(input))
			return HEADER_COLON;
		if (input != ':') {
			if (!is_char(input) || is_ctl(input) || is_tspecial(input))
				throw std::runtime_error("Invalid character at header name");
			header.name.push_back(std::tolower(input));
			return HEADER_NAME;
		}
		// Fall Throught
	case HEADER_COLON:
		if (is_sp(input))
			return HEADER_COLON;
		if (input != ':')
			throw std::runtime_error("':' expected");
		// We will add other comma-separated headers if we need them later
		header_cms_list =
		    (header.name == CRAB_LITERAL("connection")) || (header.name == CRAB_LITERAL("transfer-encoding"));
		return SPACE_BEFORE_HEADER_VALUE;
	case SPACE_BEFORE_HEADER_VALUE:
		if (is_sp(input))
			return SPACE_BEFORE_HEADER_VALUE;
		// Fall Throught
	case HEADER_VALUE:
		if (input == '\r') {
			return HEADER_LF;
		}
		if (input == '\n')
			return HEADER_LINE_START;
		if (is_ctl(input))
			throw std::runtime_error("Invalid character (control) in header value");
		if (header_cms_list && input == ',') {
			process_ready_header();
			header.value.clear();
			return SPACE_BEFORE_HEADER_VALUE;
		}
		header.value.push_back(input);
		return HEADER_VALUE;
	case HEADER_LF:
		if (input != '\n')
			throw std::runtime_error("Expecting newline");
		return HEADER_LINE_START;
	case FINAL_LF:
		if (input != '\n')
			throw std::runtime_error("Expecting final newline");
		return GOOD;
	default:
		throw std::logic_error("Invalid response parser state");
	}
}

CRAB_INLINE void ResponseParser::process_ready_header() {
	// We have no backtracking, so cheat here
	trim_right(header.value);
	if (header_cms_list && header.value.empty())
		return;  // Empty is NOP in CMS list, like "  ,,keep-alive"
	// Those comparisons are by size first so very fast
	if (header.name == CRAB_LITERAL("content-length")) {
		if (req.has_content_length())
			throw std::runtime_error("content length specified more than once");
		try {
			req.content_length = crab::integer_cast<uint64_t>(header.value);
		} catch (const std::exception &) {
			std::throw_with_nested(std::runtime_error("Content length is not a number"));
		}
		if (!req.has_content_length())
			throw std::runtime_error("content length of 2^64-1 is not allowed");
		return;
	}
	if (header.name == CRAB_LITERAL("transfer-encoding")) {
		tolower(header.value);
		if (header.value == CRAB_LITERAL("chunked")) {
			if (!req.transfer_encodings.empty())
				throw std::runtime_error("chunk encoding must be applied last");
			req.transfer_encoding_chunked = true;
			return;
		}
		if (header.value == CRAB_LITERAL("identity")) {
			return;  // like chunked, it is transparent to user
		}
		req.transfer_encodings.push_back(header.value);
		return;
	}
	if (header.name == CRAB_LITERAL("content-type")) {
		parse_content_type_value(header.value, req.content_type_mime, req.content_type_suffix);
		return;
	}
	if (header.name == CRAB_LITERAL("connection")) {
		tolower(header.value);
		if (header.value == CRAB_LITERAL("close")) {
			req.keep_alive = false;
			return;
		}
		if (header.value == CRAB_LITERAL("keep-alive")) {
			req.keep_alive = true;
			return;
		}
		if (header.value == CRAB_LITERAL("upgrade")) {
			req.connection_upgrade = true;
			return;
		}
		throw std::runtime_error("Invalid 'connection' header value");
	}
	if (header.name == CRAB_LITERAL("upgrade")) {
		tolower(header.value);
		if (header.value == CRAB_LITERAL("websocket")) {
			req.upgrade_websocket = true;
			return;
		}
		throw std::runtime_error("Invalid 'upgrade' header value");
	}
	if (header.name == CRAB_LITERAL("sec-websocket-accept")) {
		req.sec_websocket_accept = header.value;  // Copy is better here
		return;
	}
	if (header.name == CRAB_LITERAL("date")) {
		req.date = header.value;
		return;
	}
	req.headers.emplace_back(header);  // Copy is better here
}

}}  // namespace crab::http
