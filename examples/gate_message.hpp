// Copyright (c) 2007-2019, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#pragma once

#include <chrono>
#include <iostream>
#include <sstream>
#include <string>

#include <crab/crab.hpp>

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

struct MDSettings {
    std::string upstream_address = "127.0.0.1";
    uint16_t upstream_tcp_port = 7000;
    uint16_t upstream_http_port = 7001;

    std::string md_gate_address = "127.0.0.1";
    uint16_t md_gate_udp_a_port = 7002;
    std::string md_gate_udp_a_address = "239.195.13.117";
    uint16_t md_gate_udp_ra_port = 7003;
    std::string md_gate_udp_ra_address = "239.195.14.117";
    uint16_t md_gate_http_port = 7004;
};

struct Msg {
    uint64_t seqnum = 0;
    uint64_t payload = 0;
    Msg() = default;
    Msg(uint64_t seqnum, uint64_t payload) : seqnum(seqnum), payload(payload) {}
    void write(crab::OStream * os) const {
        os->write(reinterpret_cast<const uint8_t *>(&seqnum), sizeof(uint64_t));
        os->write(reinterpret_cast<const uint8_t *>(&payload), sizeof(uint64_t));
    }
    void read(crab::IStream * is) {
        is->read(reinterpret_cast<uint8_t *>(&seqnum), sizeof(uint64_t));
        is->read(reinterpret_cast<uint8_t *>(&payload), sizeof(uint64_t));
    }

    static constexpr size_t size = sizeof(uint64_t) * 2;
};

struct MDRequest {
    uint64_t begin = 0;
    uint64_t end = 0;
    MDRequest() = default;
    MDRequest(uint64_t begin, uint64_t end) : begin(begin), end(end) {}
    void write(crab::OStream * os) const {
        os->write(reinterpret_cast<const uint8_t *>(&begin), sizeof(uint64_t));
        os->write(reinterpret_cast<const uint8_t *>(&end), sizeof(uint64_t));
    }
    void read(crab::IStream * is) {
        is->read(reinterpret_cast<uint8_t *>(&begin), sizeof(uint64_t));
        is->read(reinterpret_cast<uint8_t *>(&end), sizeof(uint64_t));
    }
    static constexpr size_t size = sizeof(uint64_t) * 2;
};
