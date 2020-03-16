// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <iostream>

#include <crab/crab.hpp>
#include "gate_message.hpp"

class FairClientApp {
public:
	explicit FairClientApp(const crab::Address &address, size_t max_requests)
	    : address(address)
	    , max_requests(max_requests)
	    , socket([&]() { socket_handler(); })
	    , socket_buffer(4096)
	    , reconnect_timer([&]() { connect(); })
	    , stat_timer([&]() { print_stats(); }) {
		connect();
		print_stats();
	}

private:
	void socket_handler() {
		auto now = std::chrono::steady_clock::now();
		if (!socket.is_open())
			return on_socket_closed();
		while (true) {
			if (socket_buffer.size() < Msg::size)
				socket_buffer.read_from(socket);
			size_t count = socket_buffer.size() / Msg::size;
			if (count == 0)
				break;
			if (count > send_time.size())
				throw std::logic_error("count > requests_in_transit");
			for (size_t i = 0; i != count; ++i) {
				auto mksec = std::chrono::duration_cast<std::chrono::microseconds>(now - send_time.front());
				send_time.pop_front();
				latency_sum_mksec += mksec;
				latency_max_mksec = std::max(latency_max_mksec, mksec);
			}
			requests_received += count;
			crab::VectorStream vs;
			socket_buffer.write_to(vs, count * Msg::size);
		}
		send_more_requests();
	}
	void send_more_requests() {
		if (send_time.size() > max_requests / 2)
			return;
		size_t count = max_requests - send_time.size();
		socket.write(std::string(count, '1'));
		auto now = std::chrono::steady_clock::now();
		for (size_t i = 0; i != count; ++i)
			send_time.push_back(now);
	}
	void on_socket_closed() {
		socket_buffer.clear();
		reconnect_timer.once(1);
		std::cout << "Upstream socket disconnected" << std::endl;
	}
	void connect() {
		if (!socket.connect(address)) {
			reconnect_timer.once(1);
		} else {
			std::cout << "Upstream socket connection attempt started..." << std::endl;
			send_time.clear();
			requests_received = 0;
			latency_sum_mksec = std::chrono::microseconds{};
			latency_max_mksec = std::chrono::microseconds{};
			send_more_requests();
		}
	}
	void print_stats() {
		stat_timer.once(1);
		std::cout << "responses received (during last second)=" << requests_received
		          << ", requests in transit=" << send_time.size();
		if (requests_received != 0) {
			double average_lat = double(latency_sum_mksec.count()) / requests_received;
			std::cout << " lat(av)=" << average_lat << " lat(max)=" << latency_max_mksec.count() << std::endl;
		} else
			std::cout << std::endl;
		requests_received = 0;
		latency_sum_mksec = std::chrono::microseconds{};
		latency_max_mksec = std::chrono::microseconds{};
		if (max_requests == 0 && send_time.empty() && socket.is_open()) {
			send_time.push_back(std::chrono::steady_clock::now());
			socket.write(std::string(1, '1'));
		}
	}
	const crab::Address address;
	const size_t max_requests;

	crab::BufferedTCPSocket socket;
	crab::Buffer socket_buffer;

	crab::Timer reconnect_timer;

	size_t requests_received = 0;
	std::chrono::microseconds latency_sum_mksec{};
	std::chrono::microseconds latency_max_mksec{};
	std::deque<std::chrono::steady_clock::time_point> send_time;  // Also # of requests in transit
	crab::Timer stat_timer;
};

int main(int argc, char *argv[]) {
	std::cout
	    << "This client send requests (1 byte) via TCP to fair_server and measures latency of responses (16 bytes)"
	    << std::endl;
	if (argc < 2) {
		std::cout << "Usage: fair_client <ip>:<port> <requests> <instances> [Default: 20000 1]" << std::endl;
		std::cout << "    fair_client will keep that number of requests in transit to server" << std::endl;
		std::cout << "    if <requests> is 0, will send request per second and measure latency" << std::endl;
		return 0;
	}
	crab::RunLoop runloop;

	std::list<FairClientApp> apps;
	const size_t requests = argc < 3 ? 20000 : crab::integer_cast<size_t>(argv[2]);
	const size_t count    = argc < 4 ? 1 : crab::integer_cast<size_t>(argv[3]);

	for (size_t i = 0; i != count; ++i)
		apps.emplace_back(crab::Address(argv[1]), requests);

	runloop.run();
	return 0;
}
