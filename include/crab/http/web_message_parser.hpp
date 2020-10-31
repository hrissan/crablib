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

class WebMessageHeaderSaver {
public:
	WebMessageHeaderSaver(bool fin, int opcode, uint64_t payload_len, optional<uint32_t> masking_key);

	const uint8_t *data() const { return buffer; }
	size_t size() const { return pos; }

private:
	uint8_t buffer[16];  // Uninitialized, according to spec, actual max size should be 14
	size_t pos = 0;
};

class WebMessageHeaderParser {
public:
	bool fin             = false;
	int opcode           = 0;
	uint64_t payload_len = 0;
	optional<uint32_t> masking_key;

	template<typename InputIterator>
	InputIterator parse(InputIterator begin, InputIterator end) {
		while (begin != end && state != GOOD)
			state = consume(*begin++);
		return begin;
	}

	bool is_good() const { return state == GOOD; }
	void parse(Buffer &buf);

	static void mask_data(size_t masking_shift, char *data, size_t size, uint32_t masking_key);

	static bool is_opcode_supported(int opcode) {
		switch (opcode) {
		case static_cast<int>(WebMessageOpcode::TEXT):
		case static_cast<int>(WebMessageOpcode::BINARY):
		case static_cast<int>(WebMessageOpcode::CLOSE):
		case static_cast<int>(WebMessageOpcode::PING):
		case static_cast<int>(WebMessageOpcode::PONG):
			return true;
		default:
			break;
		}
		return false;
	}

private:
	enum State { MESSAGE_BYTE_0, MESSAGE_BYTE_1, MESSAGE_LENGTH, MASKING_KEY, GOOD } state = MESSAGE_BYTE_0;

	size_t remaining_field_bytes = 0;
	State consume(uint8_t input);
};

class WebMessageBodyParser {
public:
	StringStream body;

	WebMessageBodyParser() = default;
	explicit WebMessageBodyParser(uint64_t payload_len, optional<uint32_t> mk);

	const uint8_t *parse(const uint8_t *begin, const uint8_t *end) {
		while (begin != end && state != GOOD)
			begin = consume(begin, end);
		return begin;
	}

	bool is_good() const { return state == GOOD; }
	void parse(Buffer &buf);

private:
	enum State { BODY, GOOD } state = GOOD;

	uint64_t remaining_bytes = 0;
	uint32_t masking_key     = 0;
	size_t masking_shift     = 0;

	const uint8_t *consume(const uint8_t *begin, const uint8_t *end);
};

}}  // namespace crab::http
