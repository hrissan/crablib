// Copyright (c) 2003-2017 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <sstream>
#include "response_parser.hpp"

namespace crab { namespace http {

CRAB_INLINE void ResponseParser::parse(Buffer &buf) {
	auto ptr = parse(buf.read_ptr(), buf.read_ptr() + buf.read_count());
	buf.did_read(ptr - buf.read_ptr());
}

CRAB_INLINE ResponseParser::State ResponseParser::consume(char input) {
	CRAB_LITERAL(lowcase_connection, "connection");
	CRAB_LITERAL(lowcase_transfer_encoding, "transfer-encoding");

	switch (state) {
	case HTTP_VERSION_H:
		if (input != 'H')
			throw std::runtime_error("Invalid http version, 'H' is expected");
		return HTTP_VERSION_T_1;
	case HTTP_VERSION_T_1:
		if (input != 'T')
			throw std::runtime_error("Invalid http version, 'T' is expected");
		return HTTP_VERSION_T_2;
	case HTTP_VERSION_T_2:
		if (input != 'T')
			throw std::runtime_error("Invalid http version, 'T' is expected");
		return HTTP_VERSION_P;
	case HTTP_VERSION_P:
		if (input != 'P')
			throw std::runtime_error("Invalid http version, 'P' is expected");
		return HTTP_VERSION_SLASH;
	case HTTP_VERSION_SLASH:
		if (input != '/')
			throw std::runtime_error("Invalid http version, '/' is expected");
		return HTTP_VERSION_MAJOR_START;
	case HTTP_VERSION_MAJOR_START:
		if (!is_digit(input))
			throw std::runtime_error("Invalid http version major start, must be digit");
		req.http_version_major = req.http_version_major * 10 + input - '0';
		return HTTP_VERSION_MAJOR;
	case HTTP_VERSION_MAJOR:
		if (input == '.')
			return HTTP_VERSION_MINOR_START;
		if (!is_digit(input))
			throw std::runtime_error("Invalid http version major, must be digit");
		req.http_version_major = req.http_version_major * 10 + input - '0';
		return HTTP_VERSION_MAJOR;
	case HTTP_VERSION_MINOR_START:
		if (!is_digit(input))
			throw std::runtime_error("Invalid http version minor start, must be digit");
		req.http_version_minor = req.http_version_minor * 10 + input - '0';
		return HTTP_VERSION_MINOR;
	case HTTP_VERSION_MINOR:
		if (input == ' ') {
			req.keep_alive = req.http_version_major == 1 && req.http_version_minor == 1;
			return STATUS_CODE_1;
		}
		if (!is_digit(input))
			throw std::runtime_error("Invalid http version minor, must be digit");
		req.http_version_minor = req.http_version_minor * 10 + input - '0';
		return HTTP_VERSION_MINOR;
	case STATUS_CODE_1:
		if (!is_digit(input))
			throw std::runtime_error("Invalid http status code symbol 1, must be digit");
		req.status = req.status * 10 + input - '0';
		return STATUS_CODE_2;
	case STATUS_CODE_2:
		if (!is_digit(input))
			throw std::runtime_error("Invalid http status code symbol 2, must be digit");
		req.status = req.status * 10 + input - '0';
		return STATUS_CODE_3;
	case STATUS_CODE_3:
		if (!is_digit(input))
			throw std::runtime_error("Invalid http status code symbol 3, must be digit");
		req.status = req.status * 10 + input - '0';
		return STATUS_CODE_SPACE;
	case STATUS_CODE_SPACE:
		if (input != ' ')
			throw std::runtime_error("Invalid http status code, space expected");
		return STATUS_TEXT;
	case STATUS_TEXT:
		if (input == '\r')
			return NEWLINE_N1;
		if (is_ctl(input))
			throw std::runtime_error("Invalid character (control) in status text");
		req.status_text.push_back(input);
		return STATUS_TEXT;
	case NEWLINE_N1:
		if (input != '\n')
			throw std::runtime_error("Newline is expected");
		req.headers.reserve(20);
		return HEADER_LINE_START;
	case HEADER_LINE_START:
		if (input == '\r')
			return NEWLINE_N3;
		if (input == ' ' || input == '\t')
			return HEADER_LWS;
		if (!is_char(input) || is_ctl(input) || is_tspecial(input))
			throw std::runtime_error("Invalid character at header line start");
		header.name.push_back(input);
		lowcase_name.push_back(tolower(input));
		return HEADER_NAME;
	case HEADER_LWS:
		if (input == '\r')
			return NEWLINE_N2;
		if (input == ' ' || input == '\t')
			return HEADER_LWS;
		if (is_ctl(input))
			throw std::runtime_error("Invalid character at header name");
		header.value.push_back(input);
		return HEADER_VALUE;
	case HEADER_NAME:
		if (input == ':') {
			// We will add other comma-separated headers if we need them later
			if (lowcase_name == lowcase_connection)
				return SPACE_BEFORE_HEADER_VALUE_COMMA_SEPARATED;
			if (lowcase_name == lowcase_transfer_encoding)
				return SPACE_BEFORE_HEADER_VALUE_COMMA_SEPARATED;
			return SPACE_BEFORE_HEADER_VALUE;
		}
		if (!is_char(input) || is_ctl(input) || is_tspecial(input))
			throw std::runtime_error("Invalid character at header name");
		header.name.push_back(input);
		lowcase_name.push_back(tolower(input));
		return HEADER_NAME;
	case SPACE_BEFORE_HEADER_VALUE:
		if (input == ' ' || input == '\t')
			return SPACE_BEFORE_HEADER_VALUE;
		// Fall Throught
	case HEADER_VALUE:
		if (input == '\r') {
			process_ready_header();
			header.name.clear();
			header.value.clear();
			lowcase_name.clear();
			return NEWLINE_N2;
		}
		if (is_ctl(input))
			throw std::runtime_error("Invalid character (control) in header value");
		header.value.push_back(input);
		return HEADER_VALUE;
	case SPACE_BEFORE_HEADER_VALUE_COMMA_SEPARATED:
		if (input == ' ' || input == '\t')
			return SPACE_BEFORE_HEADER_VALUE_COMMA_SEPARATED;
		// Fall Throught
	case HEADER_VALUE_COMMA_SEPARATED:
		if (input == '\r') {
			process_ready_header();
			header.name.clear();
			header.value.clear();
			lowcase_name.clear();
			return NEWLINE_N2;
		}
		if (input == ',') {
			process_ready_header();
			header.value.clear();
			return SPACE_BEFORE_HEADER_VALUE_COMMA_SEPARATED;
		}
		if (is_ctl(input))
			throw std::runtime_error("Invalid character (control) in header value");
		header.value.push_back(input);
		return HEADER_VALUE_COMMA_SEPARATED;
	case NEWLINE_N2:
		if (input != '\n')
			throw std::runtime_error("Expecting newline");
		return HEADER_LINE_START;
	case NEWLINE_N3:
		if (input != '\n')
			throw std::runtime_error("Expecting final newline");
		return GOOD;
	default:
		throw std::logic_error("Invalid response parser state");
	}
}

CRAB_INLINE void ResponseParser::process_ready_header() {
	// We have no backtracking, so cheat here
	while (!header.value.empty() && (header.value.back() == ' ' || header.value.back() == '\t'))
		header.value.pop_back();
	CRAB_LITERAL(lowcase_content_length, "content-length");
	CRAB_LITERAL(lowcase_content_type, "content-type");
	CRAB_LITERAL(lowcase_transfer_encoding, "transfer-encoding");
	CRAB_LITERAL(lowcase_chunked, "chunked");
	CRAB_LITERAL(lowcase_connection, "connection");
	CRAB_LITERAL(lowcase_close, "close");
	CRAB_LITERAL(lowcase_keep_alive, "keep-alive");
	CRAB_LITERAL(lowcase_upgrade, "upgrade");
	CRAB_LITERAL(lowcase_websocket, "websocket");
	CRAB_LITERAL(lowcase_sec_websocket_accept, "sec-websocket-accept");
	CRAB_LITERAL(lowcase_date, "date");
	// Those comparisons are by size first so very fast
	if (lowcase_name == lowcase_content_length) {
		try {
			req.content_length = std::stoull(header.value);
			// common::integer_cast<decltype(req.content_length)>(lowcase.value);
		} catch (const std::exception &) {
			std::throw_with_nested(std::runtime_error("Content length is not a number"));
		}
		return;
	}
	if (lowcase_name == lowcase_transfer_encoding) {
		if (lowcase_chunked.compare_lowcase(header.value) == 0) {
			req.transfer_encoding_chunked = true;
			return;
		}
		req.transfer_encoding = header.value;
		return;
	}
	if (lowcase_name == lowcase_content_type) {
		req.content_type = header.value;
		return;
	}
	if (lowcase_name == lowcase_connection) {
		if (lowcase_close.compare_lowcase(header.value) == 0) {
			req.keep_alive = false;
			return;
		}
		if (lowcase_keep_alive.compare_lowcase(header.value) == 0) {
			req.keep_alive = true;
			return;
		}
		if (lowcase_upgrade.compare_lowcase(header.value) == 0) {
			req.connection_upgrade = true;
			return;
		}
		throw std::runtime_error("Invalid 'connection' header value");
	}
	if (lowcase_name == lowcase_upgrade) {
		if (lowcase_websocket.compare_lowcase(header.value) == 0) {
			req.upgrade_websocket = true;
			return;
		}
		throw std::runtime_error("Invalid 'upgrade' header value");
	}
	if (lowcase_name == lowcase_sec_websocket_accept) {
		req.sec_websocket_accept = header.value;  // Copy is better here
		return;
	}
	if (lowcase_name == lowcase_date) {
		req.date = header.value;
		return;
	}
	req.headers.emplace_back(header);  // Copy is better here
}

}}  // namespace crab::http
