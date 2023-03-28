// Copyright (c) 2007-2023, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <iostream>

#include <crab/crab.hpp>

namespace http = crab::http;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	if (size == 0)
		return 0;
	int previous_opcode = data[0];

	http::MessageChunkParser chunk(previous_opcode);
	http::MessageBodyParser body;
	try {
		auto pos = chunk.parse(data + 1, data + size);
		if (!chunk.is_good())
			return 0;
		body.add_chunk(chunk.req);
		pos = body.parse(pos, data + size);
	} catch (const std::runtime_error &) {
	}

	return 0;
}
