// Copyright (c) 2003-2017 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <sstream>
#include "web_message_parser.hpp"

namespace crab { namespace http {

CRAB_INLINE WebMessageHeaderSaver::WebMessageHeaderSaver(bool fin, int opcode, uint64_t payload_len, optional<uint32_t> masking_key) {
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

CRAB_INLINE void WebMessageHeaderParser::mask_data(size_t masking_shift, char *data, size_t size, uint32_t masking_key) {
	auto mask = crab::rol(masking_key, 8 * masking_shift);

	void *next_aligned = data;
	if (!std::align(alignof(uint64_t), sizeof(uint64_t), next_aligned, size)) {
		// There is no such a pointer inside data:size that aligned uint64_t would fit
		for (size_t i = 0; i != size; ++i) {
			mask = rol(mask, 8);
			data[i] ^= mask;
		}
		return;
	}
	while (data != next_aligned) {
		mask = rol(mask, 8);
		*data ^= mask;
		data++;
	}
	// We originally interpreted mask bytes ABCD as a big-endian number (A << 24)+(B << 16)+(C << 8) + D
	// We need ^A for the first byte, ^B for the second one, etc. or convert BE to native
	uint8_t mask_bytes[4]{uint8_t(mask >> 24), uint8_t(mask >> 16), uint8_t(mask >> 8), uint8_t(mask)};
	uint32_t mask_native;
	std::memcpy(&mask_native, &mask_bytes, 4);
	const uint64_t mask64_native = (uint64_t(mask_native) << 32U) | mask_native;

	while (size >= sizeof(uint64_t)) {
		*reinterpret_cast<uint64_t *>(data) ^= mask64_native;  // Cast is safe, because data is aligned
		data += sizeof(uint64_t);
		size -= sizeof(uint64_t);
	}
	for (size_t i = 0; i != size; ++i) {
		mask = rol(mask, 8);
		data[i] ^= mask;
	}
}

CRAB_INLINE void WebMessageBodyParser::parse(Buffer &buf) {
	auto ptr = parse(buf.read_ptr(), buf.read_ptr() + buf.read_count());
	buf.did_read(ptr - buf.read_ptr());
}

CRAB_INLINE WebMessageBodyParser::WebMessageBodyParser(uint64_t payload_len, optional<uint32_t> mk)
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
