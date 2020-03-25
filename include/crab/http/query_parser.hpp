// Copyright (c) 2003-2017 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#pragma once

#include <unordered_map>
#include "../streams.hpp"
#include "types.hpp"

namespace crab { namespace http {

class QueryParser {
public:
	template<typename InputIterator>
	void parse(InputIterator begin, InputIterator end) {
		while (begin != end)
			state = consume(*begin++);
	}
	// This grammar has no stop state, has to be stopped manually
	void parse_end() { state = consume_end(); }

	void parse(const std::string &str) { parse(str.data(), str.data() + str.size()); }
	std::unordered_map<std::string, std::string> parsed;

private:
	enum State {
		KEY,
		KEY_PERCENT1,
		KEY_PERCENT2,
		KEY_PERCENT3,
		VALUE,
		VALUE_PERCENT1,
		VALUE_PERCENT2,
		VALUE_PERCENT3
	} state = KEY;

	char percent1_hex_sym = char{};
	std::string key_;    // key being parsed
	std::string value_;  // value being parsed

	State consume(char input);
	State consume_end();
	void persist_pair();
};

std::unordered_map<std::string, std::string> parse_query_string(const std::string &str);

class CookieParser {
public:
	CookieParser() = default;

	template<typename InputIterator>
	void parse(InputIterator begin, InputIterator end) {
		while (begin != end)
			state = consume(*begin++);
	}
	// This grammar has no stop state, has to be stopped manually
	void parse_end() { state = consume_end(); }

	void parse(const std::string &str) { parse(str.data(), str.data() + str.size()); }
	std::unordered_map<std::string, std::string> parsed;

private:
	enum State { KEY_WS_BEFORE, KEY, VALUE_WS_BEFORE, VALUE } state = KEY_WS_BEFORE;

	std::string key_;    // key being parsed
	std::string value_;  // value being parsed

	State consume(char input);
	State consume_end();
	void persist_pair();
};

std::unordered_map<std::string, std::string> parse_cookie_string(const std::string &str);

}}  // namespace crab::http
