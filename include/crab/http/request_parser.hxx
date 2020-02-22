// Copyright (c) 2003-2017 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <cctype>
#include <stdexcept>
#include "request_parser.hpp"

namespace crab { namespace http {

CRAB_INLINE void RequestParser::parse(Buffer &buf) {
	auto ptr = parse(buf.read_ptr(), buf.read_ptr() + buf.read_count());
	buf.did_read(ptr - buf.read_ptr());
}

CRAB_INLINE RequestParser::State RequestParser::consume(char input) {
	CRAB_LITERAL(lowcase_connection, "connection");
	CRAB_LITERAL(lowcase_transfer_encoding, "transfer-encoding");
	if (++total_length > max_total_length)
		throw std::runtime_error("HTTP Header too long - security violation");

	switch (state) {
	case METHOD_START:
		if (!is_char(input) || is_ctl(input) || is_tspecial(input))
			throw std::runtime_error("Invalid character at method start");
		req.method.push_back(input);
		return METHOD;
	case METHOD:
		if (input == ' ')
			return URI;
		if (!is_char(input) || is_ctl(input) || is_tspecial(input))
			throw std::runtime_error("Invalid character in method");
		req.method.push_back(input);
		return METHOD;
	case URI:
		if (input == ' ')
			return HTTP_VERSION_H;
		if (is_ctl(input))
			throw std::runtime_error("Invalid (control) character in uri");
		req.uri.push_back(input);
		return URI;
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
		if (input == '\r') {
			req.keep_alive = req.http_version_major == 1 && req.http_version_minor == 1;
			return NEWLINE_N1;
		}
		if (!is_digit(input))
			throw std::runtime_error("Invalid http version minor, must be digit");
		req.http_version_minor = req.http_version_minor * 10 + input - '0';
		return HTTP_VERSION_MINOR;
	case NEWLINE_N1:
		if (input != '\n')
			throw std::runtime_error("Newline is expected");
		req.headers.reserve(20);
		return HEADER_LINE_START;
	case HEADER_LINE_START:
		if (input == '\r')
			return NEWLINE_N3;
		if (input == ' ' || input == '\t')
			return HEADER_LWS;  // Leading non-standard whitespace
		if (!is_char(input) || is_ctl(input) || is_tspecial(input))
			throw std::runtime_error("Invalid character at header line start");
		header.name.push_back(input);
		lowcase_name.push_back(std::tolower(input));
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
		lowcase_name.push_back(std::tolower(input));
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
		throw std::logic_error("Invalid request parser state");
	}
}

CRAB_INLINE void RequestParser::process_ready_header() {
	// We have no backtracking, so cheat here
	while (!header.value.empty() && (header.value.back() == ' ' || header.value.back() == '\t'))
		header.value.pop_back();
	CRAB_LITERAL(lowcase_content_length, "content-length");
	CRAB_LITERAL(lowcase_content_type, "content-type");
	CRAB_LITERAL(lowcase_transfer_encoding, "transfer-encoding");
	CRAB_LITERAL(lowcase_chunked, "chunked");
	CRAB_LITERAL(lowcase_host, "host");
	CRAB_LITERAL(lowcase_origin, "origin");
	CRAB_LITERAL(lowcase_connection, "connection");
	CRAB_LITERAL(lowcase_authorization, "authorization");
	CRAB_LITERAL(lowcase_basic, "basic");
	CRAB_LITERAL(lowcase_close, "close");
	CRAB_LITERAL(lowcase_keep_alive, "keep-alive");
	CRAB_LITERAL(lowcase_upgrade, "upgrade");
	CRAB_LITERAL(lowcase_websocket, "websocket");
	CRAB_LITERAL(lowcase_sec_websocket_key, "sec-websocket-key");
	CRAB_LITERAL(lowcase_sec_websocket_version, "sec-websocket-version");
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
	if (lowcase_name == lowcase_host) {
		req.host = header.value;
		return;
	}
	if (lowcase_name == lowcase_origin) {
		req.origin = header.value;
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
	if (lowcase_name == lowcase_authorization) {
		const std::string &value = header.value;
		if (value.size() < lowcase_basic.size || lowcase_basic.compare_lowcase(value.data(), lowcase_basic.size) != 0)
			return;
		size_t start = lowcase_basic.size;
		while (start < value.size() && (value[start] == ' ' || value[start] == '\t'))
			start += 1;
		req.basic_authorization = value.substr(start);
		return;
	}
	if (lowcase_name == lowcase_upgrade) {
		if (lowcase_websocket.compare_lowcase(header.value) == 0) {
			req.upgrade_websocket = true;
			return;
		}
		throw std::runtime_error("Invalid 'upgrade' header value");
	}
	if (lowcase_name == lowcase_sec_websocket_key) {
		req.sec_websocket_key = header.value;  // Copy is better here
		return;
	}
	if (lowcase_name == lowcase_sec_websocket_version) {
		req.sec_websocket_version = header.value;  // Copy is better here
		return;
	}
	req.headers.emplace_back(header);  // Copy is better here
}

CRAB_INLINE BodyParser::BodyParser(size_t content_length, bool chunked) {
	if (content_length != std::numeric_limits<size_t>::max()) {
		if (chunked)
			throw std::runtime_error("Body cannot have both Content-Length and chunked encoding");
		remaining_bytes = content_length;
	}
	state = chunked ? CHUNK_SIZE_START : (remaining_bytes == 0) ? GOOD : CONTENT_LENGTH_BODY;
}

CRAB_INLINE void BodyParser::parse(Buffer &buf) {
	auto ptr = parse(buf.read_ptr(), buf.read_ptr() + buf.read_count());
	buf.did_read(ptr - buf.read_ptr());
}

CRAB_INLINE const uint8_t *BodyParser::consume(const uint8_t *begin, const uint8_t *end) {
	switch (state) {
	case CONTENT_LENGTH_BODY: {
		size_t wr = std::min<size_t>(end - begin, remaining_bytes);
		body.write(begin, wr);
		begin += wr;
		remaining_bytes -= wr;
		if (remaining_bytes == 0)
			state = GOOD;
		return begin;
	}
	case CHUNK_BODY: {
		size_t wr = std::min<size_t>(end - begin, remaining_bytes);
		body.write(begin, wr);
		begin += wr;
		remaining_bytes -= wr;
		if (remaining_bytes == 0)
			state = NEWLINE_R2;
		return begin;
	}
	default:
		break;
	}
	if (chunk_header_total_length > max_chunk_header_total_length)
		throw std::runtime_error("HTTP Chunk Header too long - security violation");
	if (trailers_total_length > max_trailers_total_length)
		throw std::runtime_error("HTTP Trailer too long - security violation");
	state = consume(*begin++);
	return begin;
}

CRAB_INLINE BodyParser::State BodyParser::consume(uint8_t input) {
	switch (state) {
	case CHUNK_SIZE_START:
		chunk_header_total_length += 1;
		if (input == ' ')
			return CHUNK_SIZE_START;
		if (from_hex_digit(input) < 0)
			throw std::runtime_error("Chunk size must start with hex digit");
		chunk_size.push_back(input);
		return CHUNK_SIZE;
	case CHUNK_SIZE:
		chunk_header_total_length += 1;
		if (input == ' ' || input == ';')
			return CHUNK_SIZE_PADDING;
		if (input == '\r')
			return NEWLINE_N1;
		if (from_hex_digit(input) < 0)
			throw std::runtime_error("Chunk size must be hex number");
		if (chunk_size.size() >= sizeof(size_t) * 2)
			throw std::runtime_error("Chunk size too big");
		chunk_size.push_back(input);
		return CHUNK_SIZE;
	case CHUNK_SIZE_PADDING:
		chunk_header_total_length += 1;
		// Actual grammar here is complicated, we just skip to \r
		if (input == '\r')
			return NEWLINE_N1;
		return CHUNK_SIZE_PADDING;
	case NEWLINE_N1:
		chunk_header_total_length += 1;
		if (input != '\n')
			throw std::runtime_error("Newline is expected");
		// We already checked that chunk_size contains only hex digits and is not empty
		remaining_bytes = 0;
		for (auto c : chunk_size)
			remaining_bytes = remaining_bytes * 16 + from_hex_digit(c);
		chunk_size.clear();
		chunk_header_total_length = 0;
		if (remaining_bytes == 0)
			return TRAILER_LINE_START;
		return CHUNK_BODY;
	case TRAILER_LINE_START:
		trailers_total_length += 1;
		if (input == '\r')
			return NEWLINE_N3;
		if (!is_char(input) || is_ctl(input) || is_tspecial(input))
			throw std::runtime_error("Invalid character at header line start");
		return TRAILER;
	case TRAILER:
		trailers_total_length += 1;
		if (input == '\r')
			return NEWLINE_N1;
		return TRAILER;
	case NEWLINE_R2:
		trailers_total_length += 1;
		if (input != '\r')
			throw std::runtime_error("Newline is expected");
		return NEWLINE_N2;
	case NEWLINE_N2:
		trailers_total_length += 1;
		if (input != '\n')
			throw std::runtime_error("Newline is expected");
		return CHUNK_SIZE_START;
	case NEWLINE_R3:
		trailers_total_length += 1;
		if (input != '\r')
			throw std::runtime_error("Newline is expected");
		return NEWLINE_N3;
	case NEWLINE_N3:
		trailers_total_length += 1;
		if (input != '\n')
			throw std::runtime_error("Newline is expected");
		return GOOD;
	default:
		throw std::logic_error("Invalid chunked body parser state");
	}
}

}}  // namespace crab::http
