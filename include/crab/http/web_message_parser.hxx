// Copyright (c) 2003-2017 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <sstream>
#include "web_message_parser.hpp"

namespace crab { namespace http {

CRAB_INLINE void MessageChunkParser::parse(Buffer &buf) {
	auto ptr = parse(buf.read_ptr(), buf.read_ptr() + buf.read_count());
	buf.did_read(ptr - buf.read_ptr());
}

CRAB_INLINE MessageChunkParser::State MessageChunkParser::consume(uint8_t input) {
	switch (state) {
	case MESSAGE_BYTE_0:
		if (input & 0x70)
			throw std::runtime_error("Invalid reserved bits in first byte");
		req.fin    = (input & 0x80) != 0;
		req.opcode = (input & 0x0F);
		if (!WebMessage::is_good_opcode(req.opcode))
			throw std::runtime_error("Invalid opcode");
		if (previous_opcode != 0 && req.opcode != 0)
			throw std::runtime_error("Non-continuation in the subsequent chunk");
		if (req.opcode == 0) {
			if (previous_opcode == 0)
				throw std::runtime_error("Continuation in the first chunk");
			req.opcode = previous_opcode;
		}
		return MESSAGE_BYTE_1;
	case MESSAGE_BYTE_1:
		req.mask        = (input & 0x80) != 0;
		req.masking_key = 0;
		req.payload_len = (input & 0x7F);
		if (req.payload_len == 126) {
			req.payload_len       = 0;
			remaining_field_bytes = 2;
			return MESSAGE_LENGTH;
		}
		if (req.payload_len == 127) {
			req.payload_len       = 0;
			remaining_field_bytes = 8;
			return MESSAGE_LENGTH;
		}
		if (req.mask) {
			remaining_field_bytes = 4;
			return MASKING_KEY;
		}
		return GOOD;
	case MESSAGE_LENGTH:
		req.payload_len = (req.payload_len << 8) + input;
		remaining_field_bytes -= 1;
		if (remaining_field_bytes != 0)
			return MESSAGE_LENGTH;
		if (req.mask) {
			remaining_field_bytes = 4;
			return MASKING_KEY;
		}
		return GOOD;
	case MASKING_KEY:
		req.masking_key = (req.masking_key << 8) + input;
		remaining_field_bytes -= 1;
		if (remaining_field_bytes != 0)
			return MASKING_KEY;
		return GOOD;
	default:
		throw std::logic_error("Invalid web message parser state");
	}
}

CRAB_INLINE size_t MessageChunkParser::write_message_frame(
    uint8_t buffer[32], const WebMessage &message, bool mask, uint32_t masking_key) {
	if (message.opcode == 0 || !WebMessage::is_good_opcode(message.opcode))
		throw std::logic_error("Invalid web message opcode " + std::to_string(message.opcode));
	uint8_t *ptr    = buffer;
	*ptr++          = 0x80 | message.opcode;
	size_t rem_size = message.body.size();
	if (rem_size < 126) {
		*ptr++ = static_cast<uint8_t>(rem_size | (mask ? 0x80 : 0));
	} else if (rem_size < 65536) {
		*ptr++ = 126 | (mask ? 0x80 : 0);
		for (size_t i = 2; i-- > 0;)
			*ptr++ = static_cast<uint8_t>(rem_size >> i * 8);
	} else {
		*ptr++ = 127 | (mask ? 0x80 : 0);
		for (size_t i = 8; i-- > 0;)
			*ptr++ = static_cast<uint8_t>(rem_size >> i * 8);
	}
	if (mask)
		for (size_t i = 4; i-- > 0;)
			*ptr++ = masking_key >> i * 8;
	return ptr - buffer;
}

CRAB_INLINE std::string MessageChunkParser::write_message_frame(
    const WebMessage &message, bool mask, uint32_t masking_key) {
	uint8_t buffer[32];
	size_t buffer_len = write_message_frame(buffer, message, mask, masking_key);

	return std::string(reinterpret_cast<const char *>(buffer), buffer_len);
}

// TODO - help compiler by splitting into start, mid, finish
// start will advance data and rotate masking_key until aligned with uint64_t

namespace details {
CRAB_INLINE uint32_t local_rol(uint32_t mask, size_t shift) {
	shift = shift & 31U;
	return (mask << shift) | (mask >> (32 - shift));
}
}  // namespace details

CRAB_INLINE void MessageChunkParser::mask_data(size_t masking_shift, char *data, size_t size, uint32_t masking_key) {
	uint32_t x2 = details::local_rol(masking_key, 8 + 8 * masking_shift);
	for (size_t i = 0; i != size; ++i) {
		data[i] ^= x2;
		x2 = details::local_rol(x2, 8);
	}
}

CRAB_INLINE void MessageBodyParser::parse(Buffer &buf) {
	auto ptr = parse(buf.read_ptr(), buf.read_ptr() + buf.read_count());
	buf.did_read(ptr - buf.read_ptr());
}

CRAB_INLINE void MessageBodyParser::add_chunk(const WebMessageChunk &chunk) {
	remaining_bytes += chunk.payload_len;
	if (chunk.fin && chunk.payload_len < 1024 * 1024)  // Good optimization for common single-fragment messages
		body.get_buffer().reserve(body.get_buffer().size() + chunk.payload_len);
	masking_key   = chunk.masking_key;
	masking_shift = 0;
	state         = (remaining_bytes == 0) ? GOOD : BODY;
}

CRAB_INLINE const uint8_t *MessageBodyParser::consume(const uint8_t *begin, const uint8_t *end) {
	if (state == BODY) {
		size_t wr = static_cast<size_t>(std::min<uint64_t>(end - begin, remaining_bytes));
		body.write(begin, wr);

		if (masking_key) {
			auto &buf = body.get_buffer();
			MessageChunkParser::mask_data(masking_shift, &buf[buf.size() - wr], wr, masking_key);
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
