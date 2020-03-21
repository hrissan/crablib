// Copyright (c) 2003-2017 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <cctype>
#include <stdexcept>
#include "../integer_cast.hpp"
#include "request_parser.hpp"

namespace crab { namespace http {

CRAB_INLINE void RequestParser::parse(Buffer &buf) {
	auto ptr = parse(buf.read_ptr(), buf.read_ptr() + buf.read_count());
	buf.did_read(ptr - buf.read_ptr());
}

// We tolerate \n instead of \r\n according to recomendation
// https://www.w3.org/Protocols/rfc2616/rfc2616-sec19.html#sec19.3
CRAB_INLINE RequestParser::State RequestParser::consume(char input) {
	;
	if (++total_length > max_total_length)
		throw std::runtime_error("HTTP Header too long - security violation");

	switch (state) {
	case METHOD_START:
		// Skip empty lines https://tools.ietf.org/html/rfc2616#section-4.1
		if (input == '\r')
			return METHOD_START_LF;
		if (input == '\n')
			return METHOD;
		if (!is_char(input) || is_ctl(input) || is_tspecial(input))
			throw std::runtime_error("Invalid character at method start");
		req.method.push_back(input);
		return METHOD;
	case METHOD_START_LF:
		if (input != '\n')
			throw std::runtime_error("Invalid LF at method start");
		return METHOD_START;
	case METHOD:
		if (is_sp(input))
			return URI_START;
		if (!is_char(input) || is_ctl(input) || is_tspecial(input))
			throw std::runtime_error("Invalid character in method");
		req.method.push_back(input);
		return METHOD;
	case URI_START:
		if (is_sp(input))
			return URI_START;
		if (is_ctl(input))
			throw std::runtime_error("Invalid (control) character at uri start");
		if (input == '#')
			throw std::runtime_error("Invalid '#' character at uri start");
		if (input == '?')
			throw std::runtime_error("Invalid '?' character at uri start");
		if (input == '%')
			return URI_PERCENT1;
		req.path.push_back(input);
		return URI;
	case URI:
		if (is_sp(input))
			return HTTP_VERSION_H;
		if (is_ctl(input))
			throw std::runtime_error("Invalid (control) character in uri");
		if (input == '#')
			return URI_ANCHOR;
		if (input == '?')
			return URI_QUERY_STRING;
		if (input == '%')
			return URI_PERCENT1;
		req.path.push_back(input);
		return URI;
	case URI_PERCENT1:
		percent1_hex_digit = from_hex_digit(input);
		if (percent1_hex_digit < 0)
			throw std::runtime_error("URI percent-encoding invalid first hex digit");
		return URI_PERCENT2;
	case URI_PERCENT2: {
		int digit2 = from_hex_digit(input);
		if (digit2 < 0)
			throw std::runtime_error("URI percent-encoding invalid second hex digit");
		req.path.push_back(static_cast<uint8_t>(percent1_hex_digit * 16 + digit2));
		return URI;
	}
	case URI_QUERY_STRING:
		if (is_sp(input))
			return HTTP_VERSION_H;
		if (is_ctl(input))
			throw std::runtime_error("Invalid (control) character in uri");
		if (input == '#')
			return URI_ANCHOR;
		if (input == '%')
			return URI_QUERY_STRING_PERCENT1;
		req.query_string.push_back(input);
		return URI_QUERY_STRING;
	case URI_QUERY_STRING_PERCENT1:
		percent1_hex_digit = from_hex_digit(input);
		if (percent1_hex_digit < 0)
			throw std::runtime_error("URI percent-encoding invalid first hex digit");
		return URI_QUERY_STRING_PERCENT2;
	case URI_QUERY_STRING_PERCENT2: {
		int digit2 = from_hex_digit(input);
		if (digit2 < 0)
			throw std::runtime_error("URI percent-encoding invalid second hex digit");
		req.query_string.push_back(static_cast<uint8_t>(percent1_hex_digit * 16 + digit2));
		return URI_QUERY_STRING;
	}
	case URI_ANCHOR:
		if (is_sp(input))
			return HTTP_VERSION_H;
		if (is_ctl(input))
			throw std::runtime_error("Invalid (control) character in uri");
		return URI_ANCHOR;
	case HTTP_VERSION_H:
		if (is_sp(input))
			return HTTP_VERSION_H;
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
		if (input == '\r')
			return STATUS_LINE_LF;
		if (input == '\n')
			return FIRST_HEADER_LINE_START;
		if (is_sp(input))
			return STATUS_LINE_CR;
		if (!isdigit(input))
			throw std::runtime_error("Invalid http version minor, must be digit");
		req.http_version_minor = req.http_version_minor * 10 + input - '0';
		if (req.http_version_minor > 99)
			throw std::runtime_error("Invalid http version minor, too big");
		return HTTP_VERSION_MINOR;
	case STATUS_LINE_CR:
		if (is_sp(input))
			return STATUS_LINE_CR;
		if (input != '\n')
			throw std::runtime_error("Newline is expected");
		return STATUS_LINE_LF;
	case STATUS_LINE_LF:
		if (input != '\n')
			throw std::runtime_error("Newline is expected");
		return FIRST_HEADER_LINE_START;
	case FIRST_HEADER_LINE_START:  // Cannot contain LWS
		req.keep_alive = req.http_version_major == 1 && req.http_version_minor >= 1;
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
		header_cms_list = (header.name == Literal{"connection"}) || (header.name == Literal{"transfer-encoding"});
		return SPACE_BEFORE_HEADER_VALUE;
	case SPACE_BEFORE_HEADER_VALUE:
		if (is_sp(input))
			return SPACE_BEFORE_HEADER_VALUE;
		// Fall Throught
	case HEADER_VALUE:
		if (input == '\r')
			return HEADER_LF;
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
		throw std::logic_error("Invalid request parser state");
	}
}

CRAB_INLINE void RequestParser::process_ready_header() {
	// We have no backtracking, so cheat here
	trim_right(header.value);
	if (header_cms_list && header.value.empty())
		return;  // Empty is NOP in CMS list, like "  ,,keep-alive"
	// Those comparisons are by size first so very fast
	if (header.name == Literal{"content-length"}) {
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
	if (header.name == Literal{"transfer-encoding"}) {
		tolower(header.value);
		if (header.value == Literal{"chunked"}) {
			if (!req.transfer_encodings.empty())
				throw std::runtime_error("chunk encoding must be applied last");
			req.transfer_encoding_chunked = true;
			return;
		}
		if (header.value == Literal{"identity"}) {
			return;  // like chunked, it is transparent to user
		}
		req.transfer_encodings.push_back(header.value);
		return;
	}
	if (header.name == Literal{"host"}) {
		req.host = header.value;
		return;
	}
	if (header.name == Literal{"origin"}) {
		req.origin = header.value;
		return;
	}
	if (header.name == Literal{"content-type"}) {
		parse_content_type_value(header.value, req.content_type_mime, req.content_type_suffix);
		return;
	}
	if (header.name == Literal{"connection"}) {
		tolower(header.value);
		if (header.value == Literal{"close"}) {
			req.keep_alive = false;
			return;
		}
		if (header.value == Literal{"keep-alive"}) {
			req.keep_alive = true;
			return;
		}
		if (header.value == Literal{"upgrade"}) {
			req.connection_upgrade = true;
			return;
		}
		throw std::runtime_error("Invalid 'connection' header value");
	}
	if (header.name == Literal{"authorization"}) {
		parse_authorization_basic(header.value, req.basic_authorization);
		return;
	}
	if (header.name == Literal{"upgrade"}) {
		tolower(header.value);
		if (header.value == Literal{"websocket"}) {
			req.upgrade_websocket = true;
			return;
		}
		throw std::runtime_error("Invalid 'upgrade' header value");
	}
	if (header.name == Literal{"sec-websocket-key"}) {
		req.sec_websocket_key = header.value;  // Copy is better here
		return;
	}
	if (header.name == Literal{"sec-websocket-version"}) {
		req.sec_websocket_version = header.value;  // Copy is better here
		return;
	}
	req.headers.emplace_back(header);  // Copy is better here
}

CRAB_INLINE BodyParser::BodyParser(uint64_t content_length, bool chunked) {
	if (chunked) {
		// ignore content_length, even if set. Motivation - if client did not use
		// chunked encoding, will throw in chunk header parser
		// but if client did, we will correctly parse body
		state = CHUNK_SIZE_START;
		return;
	}
	if (content_length != std::numeric_limits<uint64_t>::max()) {
		// If content_length not set, we presume request with no body.
		// Rules about which requests and responses should and should not have body
		// are complicated.
		remaining_bytes = content_length;
	}
	state = (remaining_bytes == 0) ? GOOD : CONTENT_LENGTH_BODY;
}

CRAB_INLINE void BodyParser::parse(Buffer &buf) {
	auto ptr = parse(buf.read_ptr(), buf.read_ptr() + buf.read_count());
	buf.did_read(ptr - buf.read_ptr());
}

CRAB_INLINE const uint8_t *BodyParser::consume(const uint8_t *begin, const uint8_t *end) {
	switch (state) {
	case CONTENT_LENGTH_BODY: {
		size_t wr = static_cast<size_t>(std::min<uint64_t>(end - begin, remaining_bytes));
		body.write(begin, wr);
		begin += wr;
		remaining_bytes -= wr;
		if (remaining_bytes == 0)
			state = GOOD;
		return begin;
	}
	case CHUNK_BODY: {
		size_t wr = static_cast<size_t>(std::min<uint64_t>(end - begin, remaining_bytes));
		body.write(begin, wr);
		begin += wr;
		remaining_bytes -= wr;
		if (remaining_bytes == 0) {
			chunk_header_total_length = 0;
			state                     = CHUNK_BODY_CR;
		}
		return begin;
	}
	default:
		break;
	}
	state = consume(*begin++);
	return begin;
}

CRAB_INLINE BodyParser::State BodyParser::consume(uint8_t input) {
	if (chunk_header_total_length > max_chunk_header_total_length)
		throw std::runtime_error("HTTP Chunk Header too long - security violation");
	if (trailers_total_length > max_trailers_total_length)
		throw std::runtime_error("HTTP Trailer too long - security violation");
	switch (state) {
	case CHUNK_BODY_CR:
		chunk_header_total_length += 1;
		if (is_sp(input))
			return CHUNK_BODY_CR;
		if (input == '\r')
			return CHUNK_BODY_LF;
		if (input == '\n')
			return CHUNK_SIZE_START;
		throw std::runtime_error("CR is expected after chunk body");
	case CHUNK_BODY_LF:
		chunk_header_total_length += 1;
		if (input != '\n')
			throw std::runtime_error("LF is expected after chunk body");
		return CHUNK_SIZE_START;
	case CHUNK_SIZE_START: {
		chunk_header_total_length += 1;
		if (is_sp(input))
			return CHUNK_SIZE_START;
		int digit = from_hex_digit(input);
		if (digit < 0)
			throw std::runtime_error("Chunk size must start with hex digit");
		remaining_bytes = digit;
		return CHUNK_SIZE;
	}
	case CHUNK_SIZE: {
		chunk_header_total_length += 1;
		if (is_sp(input) || input == ';')
			return CHUNK_SIZE_EXTENSION;
		if (input == '\r')
			return CHUNK_SIZE_LF;
		if (input == '\n')
			return (remaining_bytes == 0) ? TRAILER_LINE_START : CHUNK_BODY;
		int digit = from_hex_digit(input);
		if (digit < 0)
			throw std::runtime_error("Chunk size must be hex number");
		if (remaining_bytes > (std::numeric_limits<uint64_t>::max() - 15) / 16)
			throw std::runtime_error("Chunk size too big");
		remaining_bytes = remaining_bytes * 16 + digit;
		return CHUNK_SIZE;
	}
	case CHUNK_SIZE_EXTENSION:
		chunk_header_total_length += 1;
		// Actual grammar here is complicated, we just skip to newline
		if (input == '\r')
			return CHUNK_SIZE_LF;
		if (input == '\n')
			return (remaining_bytes == 0) ? TRAILER_LINE_START : CHUNK_BODY;
		return CHUNK_SIZE_EXTENSION;
	case CHUNK_SIZE_LF:
		chunk_header_total_length += 1;
		if (input != '\n')
			throw std::runtime_error("Newline is expected");
		return (remaining_bytes == 0) ? TRAILER_LINE_START : CHUNK_BODY;
	case TRAILER_LINE_START:
		trailers_total_length += 1;
		if (input == '\r')
			return FINAL_LF;
		if (input == '\n')
			return GOOD;
		if (!is_char(input) || is_ctl(input) || is_tspecial(input))
			throw std::runtime_error("Invalid character at header line start");
		return TRAILER;
	case TRAILER:
		trailers_total_length += 1;
		if (input == '\r')
			return TRAILER_LF;
		return TRAILER;
	case TRAILER_LF:
		trailers_total_length += 1;
		if (input != '\n')
			throw std::runtime_error("Newline is expected");
		return TRAILER_LINE_START;
	case FINAL_LF:
		trailers_total_length += 1;
		if (input != '\n')
			throw std::runtime_error("Newline is expected");
		return GOOD;
	default:
		throw std::logic_error("Invalid chunked body parser state");
	}
}

}}  // namespace crab::http
