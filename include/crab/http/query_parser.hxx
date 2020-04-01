// Copyright (c) 2003-2017 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <cctype>
#include <stdexcept>
#include "query_parser.hpp"

namespace crab { namespace http {

/*
    Relevant part from RFC 3986
    ---------------------------

    pchar         = unreserved / pct-encoded / sub-delims / ":" / "@"

    query         = *( pchar / "/" / "?" )

    fragment      = *( pchar / "/" / "?" )

    pct-encoded   = "%" HEXDIG HEXDIG

    unreserved    = ALPHA / DIGIT / "-" / "." / "_" / "~"
    reserved      = gen-delims / sub-delims
    gen-delims    = ":" / "/" / "?" / "#" / "[" / "]" / "@"
    sub-delims    = "!" / "$" / "&" / "'" / "(" / ")"
                    / "*" / "+" / "," / ";" / "="
*/

CRAB_INLINE void QueryParser::persist_pair() {
	parsed[key_] = value_;  // If identical, second one wins
	key_.clear();
	value_.clear();
}

CRAB_INLINE QueryParser::State QueryParser::consume_end() {
	switch (state) {
	case VALUE:
	case KEY:
		persist_pair();
		return KEY;
	case KEY_PERCENT1:
		key_.push_back('%');
		persist_pair();
		return KEY;
	case KEY_PERCENT2:
		key_.push_back('%');
		key_.push_back(percent1_hex_sym);
		persist_pair();
		return KEY;
	case VALUE_PERCENT1:
		value_.push_back('%');
		persist_pair();
		return KEY;
	case VALUE_PERCENT2:
		value_.push_back('%');
		value_.push_back(percent1_hex_sym);
		persist_pair();
		return KEY;
	default:
		throw std::logic_error("Invalid query parser state");
	}
}

CRAB_INLINE QueryParser::State QueryParser::consume(char input) {
	switch (state) {
	case KEY:
		if (input == '&') {
			if (!key_.empty())
				persist_pair();
			return KEY;
		}
		if (input == '%')
			return KEY_PERCENT1;
		if (input == '=')
			return VALUE;
		if (input == '+')
			key_.push_back(' ');
		else
			key_.push_back(input);
		return KEY;
	case KEY_PERCENT1: {
		if (input == '=') {
			key_.push_back('%');
			return VALUE;
		}
		int digit1 = from_hex_digit(input);
		if (digit1 < 0) {
			key_.push_back('%');
			key_.push_back(input);
			return KEY;
		}
		percent1_hex_sym = input;
		return KEY_PERCENT2;
	}
	case KEY_PERCENT2: {
		if (input == '=') {
			key_.push_back('%');
			key_.push_back(percent1_hex_sym);
			return VALUE;
		}
		int digit2 = from_hex_digit(input);
		if (digit2 < 0) {
			key_.push_back('%');
			key_.push_back(percent1_hex_sym);
			key_.push_back(input);
		} else
			key_.push_back(static_cast<char>(from_hex_digit(percent1_hex_sym) * 16 + digit2));
		return KEY;
	}
	case VALUE:
		if (input == '&') {
			persist_pair();
			return KEY;
		}
		if (input == '%')
			return VALUE_PERCENT1;
		if (input == '+')
			value_.push_back(' ');
		else
			value_.push_back(input);
		return VALUE;
	case VALUE_PERCENT1: {
		if (input == '&') {
			value_.push_back('%');
			return KEY;
		}
		int digit1 = from_hex_digit(input);
		if (digit1 < 0) {
			value_.push_back('%');
			value_.push_back(input);
			return VALUE;
		}
		percent1_hex_sym = input;
		return VALUE_PERCENT2;
	}
	case VALUE_PERCENT2: {
		if (input == '=') {
			value_.push_back('%');
			value_.push_back(percent1_hex_sym);
			return VALUE;
		}
		int digit2 = from_hex_digit(input);
		if (digit2 < 0) {
			value_.push_back('%');
			value_.push_back(percent1_hex_sym);
			value_.push_back(input);
		} else
			value_.push_back(static_cast<char>(from_hex_digit(percent1_hex_sym) * 16 + digit2));
		return VALUE;
	}
	default:
		throw std::logic_error("Invalid query parser state");
	}
}

CRAB_INLINE std::unordered_map<std::string, std::string> parse_query_string(const std::string &str) {
	QueryParser p;
	p.parse(str);
	p.parse_end();
	return std::move(p.parsed);
}

CRAB_INLINE void CookieParser::persist_pair() {
	trim_right(key_);
	trim_right(value_);
	parsed[key_] = value_;  // If identical, second one wins
	key_.clear();
	value_.clear();
}

CRAB_INLINE CookieParser::State CookieParser::consume_end() {
	if (state != KEY_WS_BEFORE)
		persist_pair();
	return KEY_WS_BEFORE;
}

CRAB_INLINE CookieParser::State CookieParser::consume(char input) {
	switch (state) {
	case KEY_WS_BEFORE:
		if (is_sp(input))
			return KEY_WS_BEFORE;
		if (input == ';')
			return KEY_WS_BEFORE;
		// note: fallthru to KEY
	case KEY:
		if (input == ';') {
			persist_pair();
			return KEY_WS_BEFORE;
		}
		if (input == '=')
			return VALUE_WS_BEFORE;
		key_.push_back(input);
		return KEY;
	case VALUE_WS_BEFORE:
		if (is_sp(input))
			return VALUE_WS_BEFORE;
		// note: fallthru to VALUE
	case VALUE:
		if (input == ';') {
			persist_pair();
			return KEY_WS_BEFORE;
		}
		value_.push_back(input);
		return VALUE;
	default:
		throw std::logic_error("Invalid cookie parser state");
	}
}

CRAB_INLINE std::unordered_map<std::string, std::string> parse_cookie_string(const std::string &str) {
	CookieParser p;
	p.parse(str);
	p.parse_end();
	return std::move(p.parsed);
}

}}  // namespace crab::http
