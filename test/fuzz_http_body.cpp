// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <iostream>

#include <crab/crab.hpp>

namespace http = crab::http;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	if (size < 3)
		return 0;
	bool chunked          = (data[0] != 0);
	size_t content_length = chunked ? std::numeric_limits<size_t>::max() : (size_t(data[1]) << 8) + data[2];
	http::BodyParser req{content_length, chunked};
	req.max_trailers_total_length = 2000;  // So that default -max_len=4096 can hit a wall
	try {
		req.parse(data + 3, data + size);
	} catch (const std::runtime_error &) {
	}

	return 0;
}
