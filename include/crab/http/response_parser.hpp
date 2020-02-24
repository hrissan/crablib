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

class ResponseParser {
	enum State {
		HTTP_VERSION_H,
		HTTP_VERSION_HT,
		HTTP_VERSION_HTT,
		HTTP_VERSION_HTTP,
		HTTP_VERSION_SLASH,
		HTTP_VERSION_MAJOR_START,
		HTTP_VERSION_MAJOR,
		HTTP_VERSION_MINOR_START,
		HTTP_VERSION_MINOR,
		STATUS_CODE_1,
		STATUS_CODE_2,
		STATUS_CODE_3,
		STATUS_CODE_SPACE,
		STATUS_TEXT_START,
		STATUS_TEXT,
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
	} state = HTTP_VERSION_H;

public:
	size_t max_total_length = 8192;

	ResponseHeader req;

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
	bool header_cms_list = false;
	size_t total_length  = 0;
	State consume(char input);
};

}}  // namespace crab::http
