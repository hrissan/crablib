// Copyright (c) 2003-2017 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#pragma once

#include "../streams.hpp"
#include "types.hpp"

namespace crab { namespace http {

class RequestParser {
	enum State {
		METHOD_START,
		METHOD_START_LF,
		METHOD,
		URI_START,
		URI,
		URI_PERCENT1,
		URI_PERCENT2,
		URI_QUERY_STRING,
		URI_ANCHOR,  // empty # is allowed by standard
		HTTP_VERSION_H,
		HTTP_VERSION_HT,
		HTTP_VERSION_HTT,
		HTTP_VERSION_HTTP,
		HTTP_VERSION_SLASH,
		HTTP_VERSION_MAJOR_START,
		HTTP_VERSION_MAJOR,
		HTTP_VERSION_MINOR_START,
		HTTP_VERSION_MINOR,
		STATUS_LINE_CR,
		STATUS_LINE_LF,
		FIRST_HEADER_LINE_START,
		HEADER_LINE_START,
		HEADER_NAME,
		HEADER_COLON,
		SPACE_BEFORE_HEADER_VALUE,
		HEADER_VALUE,
		HEADER_LF,
		FINAL_LF,
		GOOD
	} state = METHOD_START;

public:
	size_t max_total_length = 8192;

	RequestHeader req;

	template<typename InputIterator>
	InputIterator parse(InputIterator begin, InputIterator end) {
		while (begin != end && state != GOOD)
			state = consume(*begin++);
		return begin;
	}

	bool is_good() const { return state == GOOD; }
	void parse(Buffer &buf);

private:
	void process_ready_header();
	Header header;
	bool header_cms_list   = false;
	int percent1_hex_digit = 0;
	size_t total_length    = 0;
	State consume(char input);
};

struct BodyParser {
	enum State {
		CONTENT_LENGTH_BODY,
		CHUNK_SIZE_START,
		CHUNK_SIZE,
		CHUNK_SIZE_EXTENSION,
		CHUNK_SIZE_LF,
		CHUNK_BODY,
		CHUNK_BODY_CR,
		CHUNK_BODY_LF,
		TRAILER_LINE_START,
		TRAILER,
		TRAILER_LF,
		FINAL_LF,
		GOOD
	} state = GOOD;

public:
	BodyParser() = default;
	BodyParser(details::optional<uint64_t> content_length, bool chunked);

	StringStream body;
	size_t max_chunk_header_total_length = 256;
	size_t max_trailers_total_length     = 4096;

	const uint8_t *parse(const uint8_t *begin, const uint8_t *end) {
		while (begin != end && state != GOOD)
			begin = consume(begin, end);
		return begin;
	}

	bool is_good() const { return state == GOOD; }
	void parse(Buffer &buf);

private:
	uint64_t remaining_bytes         = 0;
	size_t chunk_header_total_length = 0;
	size_t trailers_total_length     = 0;
	const uint8_t *consume(const uint8_t *begin, const uint8_t *end);
	State consume(uint8_t input);
};

}}  // namespace crab::http
