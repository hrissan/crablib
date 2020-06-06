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
	case KEY:
		if (!key_.empty())
			persist_pair();
		return KEY;
	case VALUE:
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
		// note: fallthru to KEY. Next line is understood by GCC
		// Fall through
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
		// note: fallthru to VALUE. Next line is understood by GCC
		// Fall through
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
	return std::move(p.parsed);
}

CRAB_INLINE std::string URI::to_string() const {
	std::stringstream ss;
	ss << scheme << "://";
	if (!user_info.empty())
		ss << url_encode(user_info) << "@";

	ss << host;

	if (!port.empty())
		ss << ":" << port;

	ss << url_encode(path, true);

	if (!query.empty())
		ss << "?" << query;
	return ss.str();
}

CRAB_INLINE void URIParser::persist_path_component() {
	if (cur_path_ == Literal{".."}) {
		// up tree from / is NOP -  https://tools.ietf.org/html/rfc3986#section-5.3
		if (!path_components_.empty())
			path_components_.pop_back();
	} else if (cur_path_ != Literal{"."}) {  // ignore this special case
		path_components_.push_back(std::move(cur_path_));
	}
	cur_path_.clear();
}

CRAB_INLINE URIParser::State URIParser::consume(char input) {
	switch (state) {
	case SCHEME:
		if (input == ':')
			return SCHEME_SEP1;
		uri.scheme.push_back(input);
		return SCHEME;
	case SCHEME_SEP1:
		if (input != '/')
			throw std::runtime_error("Invalid URI parser state: '/' expected after scheme");
		return SCHEME_SEP2;
	case SCHEME_SEP2:
		if (input != '/')
			throw std::runtime_error("Invalid URI parser state: '//' expected after scheme");
		return HOST;
	case HOST:
		if (input == '@') {
			if (user_info_assigned)
				throw std::runtime_error("URI parser - second @ is prohibited");
			user_info_assigned = true;
			uri.user_info      = url_decode(uri.host);
			uri.host.clear();
			return HOST;
		}
		if (input == '/')
			return PATH;
		if (input == ':')
			return PORT;
		uri.host.push_back(input);
		return HOST;
	case PORT:
		if (input == '@') {
			if (user_info_assigned)
				throw std::runtime_error("URI parser - second @ is prohibited");
			user_info_assigned = true;
			uri.user_info      = url_decode(uri.host + ':' + uri.port);
			uri.host.clear();
			uri.port.clear();
			return HOST;
		}
		if (input == '/')
			return PATH;
		uri.port.push_back(input);
		return PORT;
	case PATH:
		if (input == '%')
			return PATH_HEX1;
		if (input == '?')
			return QUERY;
		if (input == '/')
			persist_path_component();
		else
			cur_path_.push_back(input);
		return PATH;
	case PATH_HEX1: {
		if (input == '?') {
			cur_path_.push_back('%');
			return QUERY;
		}
		int digit1 = from_hex_digit(input);
		if (digit1 < 0) {
			cur_path_.push_back('%');
			cur_path_.push_back(input);
			return PATH;
		}
		percent1_hex_sym = input;
		return PATH_HEX2;
	}
	case PATH_HEX2: {
		if (input == '?') {
			cur_path_.push_back('%');
			cur_path_.push_back(percent1_hex_sym);
			return QUERY;
		}
		int digit2 = from_hex_digit(input);
		if (digit2 < 0) {
			cur_path_.push_back('%');
			cur_path_.push_back(percent1_hex_sym);
			cur_path_.push_back(input);
		} else
			cur_path_.push_back(static_cast<uint8_t>(from_hex_digit(percent1_hex_sym) * 16 + digit2));
		return PATH;
	}
	case QUERY:
		uri.query.push_back(input);
		return QUERY;
	default:
		throw std::logic_error("Invalid URI parser state at main consume");
	}
}

CRAB_INLINE URIParser::State URIParser::consume_end() {
	if (state == PATH_HEX1) {
		cur_path_.push_back('%');
		state = PATH;
	} else if (state == PATH_HEX2) {
		cur_path_.push_back('%');
		cur_path_.push_back(percent1_hex_sym);
		state = PATH;
	}

	for (auto &&pc : path_components_)
		uri.path.append('/' + pc);
	uri.path.append('/' + cur_path_);

	// anything less than a parsed host is invalid
	switch (state) {
	case HOST:
	case PORT:
	case PATH:
	case QUERY:
		return GOOD;
	default:
		throw std::runtime_error("Invalid URI parser state at consume_last");
	}
}

CRAB_INLINE URI parse_uri(const std::string &str) {
	URIParser p;
	p.parse(str);
	return std::move(p.uri);
}

CRAB_INLINE std::string url_decode(const std::string &str) {
	size_t pos = 0;
	std::string result;
	while (pos != str.size()) {
		if (str[pos] != '%' || pos + 3 > str.size()) {
			result += str[pos];
			pos += 1;
			continue;
		}
		int digit1 = from_hex_digit(str[pos + 1]);
		int digit2 = from_hex_digit(str[pos + 2]);
		if (digit1 < 0 || digit2 < 0) {
			result += str[pos];
			pos += 1;
			continue;
		}
		result.push_back(static_cast<uint8_t>(digit1 * 16 + digit2));
		pos += 3;
	}
	return result;
}

CRAB_INLINE std::string url_encode(const std::string &str, bool path) {
	static const char hexdigits[] = "0123456789abcdef";
	std::string result;
	for (auto c : str) {
		if (!is_uri_reserved(c) || (path && c == '/')) {
			result += c;
			continue;
		}
		result += '%';
		uint8_t ch = c;
		result += hexdigits[(ch >> 4) & 0xf];
		result += hexdigits[ch & 0xf];
	}
	return result;
}

}}  // namespace crab::http
