// Copyright (c) 2007-2019, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#pragma once

#include <chrono>
#include <iostream>
#include <sstream>
#include <string>

struct LatencyMessage {
public:
	LatencyMessage() = default;
	explicit LatencyMessage(const std::chrono::steady_clock::time_point &now) {
		creation_tp = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
	}
	std::string id;
	long long creation_tp = 0;
	std::string lat;
	std::string body;
	bool parse(const std::string &data, std::string *id2 = nullptr) {
		size_t pos1 = data.find('\n');
		if (pos1 == std::string::npos)
			return false;
		size_t pos2 = data.find('\n', pos1 + 1);
		if (pos2 == std::string::npos)
			return false;
		size_t pos3 = data.find('\n', pos2 + 1);
		if (pos3 == std::string::npos)
			return false;
		id          = data.substr(0, pos1);
		creation_tp = std::stoll(data.substr(pos1 + 1, pos2 - pos1 - 1));
		lat         = data.substr(pos2 + 1, pos3 - pos2 - 1);
		body        = data.substr(pos3 + 1);
		if (id2) {
			size_t pos4 = id.rfind('|');
			if (pos4 == std::string::npos)
				return false;
			*id2 = id.substr(pos4 + 1);
			id   = id.substr(0, pos4);
		}
		return true;
	}
	void add_lat(const std::string &who, const std::chrono::steady_clock::time_point &now) {
		if (!lat.empty())
			lat += "|";
		auto tp = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
		tp -= creation_tp;
		lat += who + ":" + std::to_string(tp);
	}
	std::string save(const std::string *id2 = nullptr) const {
		std::string result = id;
		if (id2)
			result += "|" + *id2;
		return result + "\n" + std::to_string(creation_tp) + "\n" + lat + "\n" + body;
	}
};
