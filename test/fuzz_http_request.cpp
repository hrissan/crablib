// Copyright (c) 2007-2023, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <iostream>

#include <crab/crab.hpp>

namespace http = crab::http;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	http::RequestParser req;
	req.max_total_length = 3000;  // So that default -max_len=4096 can hit a wall
	try {
		req.parse(data, data + size);
	} catch (const std::runtime_error &) {
	}

	return 0;
}
