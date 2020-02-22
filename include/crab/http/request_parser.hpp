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
		METHOD,
		URI,
		HTTP_VERSION_H,
		HTTP_VERSION_T_1,
		HTTP_VERSION_T_2,
		HTTP_VERSION_P,
		HTTP_VERSION_SLASH,
		HTTP_VERSION_MAJOR_START,
		HTTP_VERSION_MAJOR,
		HTTP_VERSION_MINOR_START,
		HTTP_VERSION_MINOR,
		NEWLINE_N1,
		HEADER_LINE_START,
		HEADER_LWS,
		HEADER_NAME,
		SPACE_BEFORE_HEADER_VALUE,
		SPACE_BEFORE_HEADER_VALUE_COMMA_SEPARATED,
		HEADER_VALUE,
		HEADER_VALUE_COMMA_SEPARATED,
		NEWLINE_N2,
		NEWLINE_N3,
		GOOD
	} state = METHOD_START;

public:
	RequestHeader req;
	size_t max_total_length = 8192;

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
	std::string lowcase_name;
	size_t total_length = 0;
	State consume(char input);
};

struct BodyParser {
	enum State {
		CONTENT_LENGTH_BODY,
		CHUNK_SIZE_START,
		CHUNK_SIZE,
		CHUNK_SIZE_PADDING,
		NEWLINE_N1,
		CHUNK_BODY,
		NEWLINE_R2,
		NEWLINE_N2,
		TRAILER_LINE_START,
		TRAILER,
		NEWLINE_R3,
		NEWLINE_N3,
		GOOD
	} state = GOOD;

public:
	BodyParser() = default;
	BodyParser(size_t content_length, bool chunked);

	StringStream body;
	static constexpr size_t max_chunk_header_total_length = 256;
	static constexpr size_t max_trailers_total_length     = 4096;

	const uint8_t *parse(const uint8_t *begin, const uint8_t *end) {
		while (begin != end && state != GOOD)
			begin = consume(begin, end);
		return begin;
	}

	bool is_good() const { return state == GOOD; }
	void parse(Buffer &buf);

private:
	std::string chunk_size;
	size_t remaining_bytes           = 0;
	size_t chunk_header_total_length = 0;
	size_t trailers_total_length     = 0;
	const uint8_t *consume(const uint8_t *begin, const uint8_t *end);
	State consume(uint8_t input);
};

}}  // namespace crab::http
