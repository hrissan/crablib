// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#pragma once

#include <cctype>
#include <string>
#include <vector>

namespace crab { namespace base64 {

inline size_t encoded_size(size_t in_len) { return 4 * ((in_len + 2) / 3); }

inline size_t max_decoded_size(const char *, size_t in_len) {
	// In case of concatenated base64, actual count can be much less
	return 3 * ((in_len + 3) / 4);
}

void encode(char *out, size_t out_len, const uint8_t *in, size_t in_len);

size_t decode(uint8_t *out, size_t out_len, const char *in, std::size_t in_len);

std::string encode(const uint8_t *in, size_t in_len);

bool decode(std::vector<uint8_t> *result, const std::string &data);

}}  // namespace crab::base64
