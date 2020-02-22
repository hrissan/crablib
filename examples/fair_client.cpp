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
		if (!socket.is_open())
			return on_socket_closed();
		while (true) {
			if (socket_buffer.size() < Msg::size)
				socket_buffer.read_from(socket);
			size_t count = socket_buffer.size() / Msg::size;
			if (count == 0)
				break;
			if (count > requests_in_transit)
				throw std::logic_error("count > requests_in_transit");
			requests_in_transit -= count;
			requests_received += count;
			crab::VectorStream vs;
			socket_buffer.write_to(vs, count * Msg::size);
			if (max_requests == 0) {
				auto mksec = std::chrono::duration_cast<std::chrono::microseconds>(
				    std::chrono::steady_clock::now() - single_send_time)
				                 .count();
				std::cout << "Single request received, latency(mksec)=" << mksec << std::endl;
			}
		}
		send_more_requests();
	}
	void send_more_requests() {
		if (requests_in_transit >= max_requests / 2)
			return;
		size_t count = max_requests - requests_in_transit;
		socket.write(std::string(count, '1'));
		requests_in_transit += count;
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
			requests_in_transit = 0;
			requests_received   = 0;
			send_more_requests();
		}
	}
	void print_stats() {
		stat_timer.once(1);
		if (max_requests != 0)
			std::cout << "responses received (during last second)=" << requests_received
			          << ", requests in transit=" << requests_in_transit << std::endl;
		requests_received = 0;
		if (max_requests == 0 && requests_in_transit == 0 && socket.is_open()) {
			single_send_time = std::chrono::steady_clock::now();
			socket.write(std::string(1, '1'));
			requests_in_transit += 1;
		}
	}
	const crab::Address address;
	const size_t max_requests;

	crab::BufferedTCPSocket socket;
	crab::Buffer socket_buffer;

	crab::Timer reconnect_timer;

	size_t requests_in_transit = 0;
	size_t requests_received   = 0;
	std::chrono::steady_clock::time_point single_send_time;
	crab::Timer stat_timer;
};

int main(int argc, char *argv[]) {
	std::cout
	    << "This client send requests (1 byte) via TCP to fair_server and measures latency of responses (16 bytes)"
	    << std::endl;
	std::cout << "Usage: fair_client <requests> <instances> [Default: 20000 1]" << std::endl;
	std::cout << "    fair_client will keep that number of requests in transit to server" << std::endl;
	std::cout << "    if <requests> is 0, will send request per second and measure latency" << std::endl;
	crab::RunLoop runloop;

	std::list<FairClientApp> apps;
	const size_t requests = argc < 2 ? 20000 : std::stoull(argv[1]);
	const size_t count    = argc < 3 ? 1 : std::stoull(argv[2]);

	for (size_t i = 0; i != count; ++i)
		apps.emplace_back(crab::Address("127.0.0.1", 7000), requests);

	runloop.run();
	return 0;
}
