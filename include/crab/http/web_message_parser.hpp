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

class MessageHeaderParser {
public:
	explicit MessageHeaderParser(int previous_opcode = 0) : previous_opcode(previous_opcode) {}
	// We pass 0 for first chunk, req.opcode is filled with actual opcode
	// We pass req.opcode for subsequent chunks, req.opcode will be filled with it

	WebMessageHeader req;

	template<typename InputIterator>
	InputIterator parse(InputIterator begin, InputIterator end) {
		while (begin != end && state != GOOD)
			state = consume(*begin++);
		return begin;
	}

	bool is_good() const { return state == GOOD; }
	void parse(Buffer &buf);

	enum { MESSAGE_FRAME_BUFFER_SIZE = 16 };  // According to spec, actual max size should be 14
	static size_t write_message_frame(uint8_t buffer[MESSAGE_FRAME_BUFFER_SIZE], const WebMessage &message,
	    details::optional<uint32_t> masking_key);
	static void mask_data(size_t masking_shift, char *data, size_t size, uint32_t masking_key);

private:
	enum State { MESSAGE_BYTE_0, MESSAGE_BYTE_1, MESSAGE_LENGTH, MASKING_KEY, GOOD } state = MESSAGE_BYTE_0;

	int previous_opcode          = 0;
	size_t remaining_field_bytes = 0;
	State consume(uint8_t input);
};

class MessageBodyParser {
public:
	StringStream body;

	void add_chunk(const WebMessageHeader &chunk);

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
