// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <iostream>

#include <crab/crab.hpp>

class ApiClientApp {
public:
	explicit ApiClientApp(
	    const crab::Address &address, crab::bdata request_body)
	    : address(address)
		, request_bytes(65536)
	    , socket([&]() { socket_handler_greedy(); })
		, socket_incoming_buffer(65536)
		, socket_outgoing_buffer(65536)
	    , reconnect_timer([&]() { connect(); })
	    , stat_timer([&]() { print_stats(); }) {
		uint8_t header_padding[HEADER_SIZE - 4]{};
		size_t len = request_body.size();
		while (request_bytes.capacity() >= request_bytes.size() + HEADER_SIZE + request_body.size()) {
			request_bytes.write(reinterpret_cast<const uint8_t *>(&len), 4);
			request_bytes.write(header_padding, sizeof(header_padding));
			request_bytes.write(request_body.data(), request_body.size());
		}
		if (request_bytes.empty())
			throw std::runtime_error("request_body too big");
		connect();
		print_stats();
	}

	enum { HEADER_SIZE = 16 };

private:
	void socket_handler_greedy() {
		if (!socket.is_open())
			return on_socket_closed();
		while (socket_incoming_buffer.read_from(socket) != 0) {
			bytes_received += socket_incoming_buffer.size();
			socket_incoming_buffer.did_read(socket_incoming_buffer.size());  // skip data
		}
		send_more_requests();
	}
	void send_more_requests() {
		while (true) {
			if (socket_outgoing_buffer.empty())
				socket_outgoing_buffer.write(request_bytes.read_ptr(), request_bytes.read_count());
			auto wr = socket_outgoing_buffer.write_to(socket);
			if (wr == 0)
				break;
			bytes_sent += wr;
		}
	}
	void on_socket_closed() {
		socket_incoming_buffer.clear();
		socket_outgoing_buffer.clear();
		reconnect_timer.once(1);
		std::cout << "Upstream socket disconnected" << std::endl;
	}
	void connect() {
		if (!socket.connect(address)) {
			reconnect_timer.once(1);
		} else {
			std::cout << "Upstream socket connection attempt started..." << std::endl;
			bytes_sent = 0;
			bytes_received = 0;
			send_more_requests();
		}
	}
	void print_stats() {
		stat_timer.once(1);
		std::cout << "bytes sent/received (during last second)=" << bytes_sent << "/" << bytes_received << std::endl;
//		bytes_sent = 0;
//		bytes_received = 0;
	}
	const crab::Address address;
	crab::Buffer request_bytes;

	crab::TCPSocket socket;
	crab::Buffer socket_incoming_buffer;
	crab::Buffer socket_outgoing_buffer;

	crab::Timer reconnect_timer;

	size_t bytes_sent = 0;
	size_t bytes_received = 0;
	crab::Timer stat_timer;
};

int main(int argc, char *argv[]) {
	std::cout << "crablib version " << crab::version_string() << std::endl;

	std::cout << "This client send requests via TCP to api_server and reads and discards responses" << std::endl;
	if (argc < 2) {
		std::cout << "Usage: api_client <ip>:<port> <instances> [Default: 20000 1]" << std::endl;
		std::cout << "    fair_client will keep that number of requests in transit to server" << std::endl;
		std::cout << "    if <requests> is 0, will send request per second and measure latency" << std::endl;
		return 0;
	}
	crab::RunLoop runloop;

	std::list<ApiClientApp> apps;
	const size_t count    = argc < 3 ? 1 : crab::integer_cast<size_t>(argv[2]);

	for (size_t i = 0; i != count; ++i)
		apps.emplace_back(crab::Address(argv[1]), crab::bdata{1, 2, 3, 4});

	runloop.run();
	return 0;
}
