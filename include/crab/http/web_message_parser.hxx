// Copyright (c) 2003-2017 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <sstream>
#include "web_message_parser.hpp"

namespace crab { namespace http {

CRAB_INLINE WebMessageHeaderSaver::WebMessageHeaderSaver(
    bool fin, int opcode, uint64_t payload_len, details::optional<uint32_t> masking_key) {
	buffer[pos++] = (fin ? 0x80 : 0) | static_cast<int>(opcode);
	if (payload_len < 126) {
		buffer[pos++] = static_cast<uint8_t>(payload_len | (masking_key ? 0x80 : 0));
	} else if (payload_len < 65536) {
		buffer[pos++] = 126 | (masking_key ? 0x80 : 0);
		for (size_t i = 2; i-- > 0;)
			buffer[pos++] = static_cast<uint8_t>(payload_len >> i * 8);
	} else {
		buffer[pos++] = 127 | (masking_key ? 0x80 : 0);
		for (size_t i = 8; i-- > 0;)
			buffer[pos++] = static_cast<uint8_t>(payload_len >> i * 8);
	}
	if (masking_key)
		for (size_t i = 4; i-- > 0;)
			buffer[pos++] = *masking_key >> i * 8;
	invariant(pos <= sizeof(buffer), "Message frame buffer overflow");
}

CRAB_INLINE void WebMessageHeaderParser::parse(Buffer &buf) {
	auto ptr = parse(buf.read_ptr(), buf.read_ptr() + buf.read_count());
	buf.did_read(ptr - buf.read_ptr());
}

CRAB_INLINE WebMessageHeaderParser::State WebMessageHeaderParser::consume(uint8_t input) {
	switch (state) {
	case MESSAGE_BYTE_0:
		if (input & 0x70)
			throw std::runtime_error("Invalid reserved bits in first byte");
		fin    = (input & 0x80) != 0;
		opcode = (input & 0x0F);
		if (!is_opcode_supported(opcode))
			throw std::runtime_error("Invalid opcode");
		return MESSAGE_BYTE_1;
	case MESSAGE_BYTE_1:
		if ((input & 0x80) != 0)
			masking_key = 0;
		payload_len = (input & 0x7F);
		if ((opcode & 0x08) != 0 && payload_len > 125)
			throw std::runtime_error("Control frame with payload_len > 125");
		if ((opcode & 0x08) != 0 && !fin)
			throw std::runtime_error("Control frame must not be fragmented");
		if (payload_len == 126) {
			payload_len           = 0;
			remaining_field_bytes = 2;
			return MESSAGE_LENGTH;
		}
		if (payload_len == 127) {
			payload_len           = 0;
			remaining_field_bytes = 8;
			return MESSAGE_LENGTH;
		}
		if (masking_key) {
			remaining_field_bytes = 4;
			return MASKING_KEY;
		}
		return GOOD;
	case MESSAGE_LENGTH:
		payload_len = (payload_len << 8) + input;
		remaining_field_bytes -= 1;
		if (remaining_field_bytes != 0)
			return MESSAGE_LENGTH;
		if (masking_key) {
			remaining_field_bytes = 4;
			return MASKING_KEY;
		}
		return GOOD;
	case MASKING_KEY:
		*masking_key = (*masking_key << 8) + input;
		remaining_field_bytes -= 1;
		if (remaining_field_bytes != 0)
			return MASKING_KEY;
		return GOOD;
	default:
		throw std::logic_error("Invalid web message parser state");
	}
}

// TODO - help compiler by splitting into start, mid, finish
// start will advance data and rotate masking_key until aligned with uint64_t

CRAB_INLINE void WebMessageHeaderParser::mask_data(
    size_t masking_shift, char *data, size_t size, uint32_t masking_key) {
	auto x2 = rol(masking_key, 8 + 8 * masking_shift);
	for (size_t i = 0; i != size; ++i) {
		data[i] ^= x2;
		x2 = rol(x2, 8);
	}
}

CRAB_INLINE void WebMessageBodyParser::parse(Buffer &buf) {
	auto ptr = parse(buf.read_ptr(), buf.read_ptr() + buf.read_count());
	buf.did_read(ptr - buf.read_ptr());
}

CRAB_INLINE WebMessageBodyParser::WebMessageBodyParser(uint64_t payload_len, details::optional<uint32_t> mk)
    : state((payload_len == 0) ? GOOD : BODY), remaining_bytes(payload_len), masking_key(mk ? *mk : 0) {
	if (payload_len < 65536)  // TODO - constant
		body.get_buffer().reserve(static_cast<size_t>(payload_len));
}

CRAB_INLINE const uint8_t *WebMessageBodyParser::consume(const uint8_t *begin, const uint8_t *end) {
	if (state == BODY) {
		size_t wr = static_cast<size_t>(std::min<uint64_t>(end - begin, remaining_bytes));
		body.write(begin, wr);

		if (masking_key) {
			auto &buf = body.get_buffer();
			WebMessageHeaderParser::mask_data(masking_shift, &buf[buf.size() - wr], wr, masking_key);
			masking_shift += wr;
		}

		begin += wr;
		remaining_bytes -= wr;
		if (remaining_bytes == 0)
			state = GOOD;
		return begin;
	}
	return begin;
}

}}  // namespace crab::http
