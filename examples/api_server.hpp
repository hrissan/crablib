// Copyright (c) 2007-2023, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#pragma once

// Toy API protocol

// header is
struct ApiHeader {
	uint32_t body_len = 0;
	uint32_t kind     = 0;
	uint64_t rid      = 0;
};

static_assert(sizeof(ApiHeader) == 16, "On exotic platform, please add pragma pack or similar");

// body follows immediately after
