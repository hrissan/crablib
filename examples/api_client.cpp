// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <iostream>

#include <crab/crab.hpp>
#include "api_server.hpp"

class ApiClientApp {
public:
	explicit ApiClientApp(const crab::Address &address, int64_t &external_duration, size_t &external_count,
	    int64_t &external_max_latency)
	    : external_duration(external_duration)
	    , external_count(external_count)
	    , external_max_latency(external_max_latency)
	    , address(address)
	    , socket([&]() { socket_handler_greedy(); })
	    , socket_incoming_buffer(65536)
	    , socket_outgoing_buffer(65536)
	    , reconnect_timer([&]() { connect(); })
	    , stat_timer([&]() { print_stats(); }) {
		connect();
		print_stats();
	}

private:
	void process_request(const ApiHeader &header) {
		if (!send_time) {
			std::cout << "Unexpected response" << std::endl;
			return;
		}
		auto dur = std::chrono::duration_cast<std::chrono::microseconds>(
		    std::chrono::high_resolution_clock::now() - *send_time);
		send_time.reset();
		external_duration += dur.count();
		external_count += 1;
		if (dur.count() > external_max_latency)
			external_max_latency = dur.count();
		//		if (durations.size() > 1024) {
		//			total_duration -= durations.front();
		//			durations.pop_front();
		//		}
		total_duration += dur.count();
		durations.push_back(dur.count());
		if (header.rid != sent_id) {
			std::cout << "Response id is different from request id" << std::endl;
			return;
		}
	}
	void socket_handler_greedy() {
		if (!socket.is_open())
			return on_socket_closed();
		while (true) {
			if (socket_incoming_buffer.size() >= sizeof(ApiHeader)) {
				ApiHeader header;
				invariant(socket_incoming_buffer.peek(reinterpret_cast<uint8_t *>(&header), sizeof(header)),
				    "Peek should succeed");
				if (socket_incoming_buffer.size() >= sizeof(ApiHeader) + header.body_len) {
					socket_incoming_buffer.did_read(sizeof(header));
					socket_incoming_buffer.did_read(header.body_len);
					bytes_received += sizeof(header) + header.body_len;
					process_request(header);
					send_more_requests();
					continue;
				}
			}
			if (socket_incoming_buffer.read_from(socket) == 0)
				break;
		}
		send_more_requests();
	}
	void send_more_requests() {
		if (!send_time) {
			ApiHeader header;
			header.body_len = 17;  // TODO constant
			sent_id         = rnd.pod<uint64_t>();
			header.rid      = sent_id;
			socket_outgoing_buffer.write(reinterpret_cast<const uint8_t *>(&header), sizeof(header));
			socket_outgoing_buffer.did_write(header.body_len);  // TODO - security issue, uninitialized memory
			send_time = std::chrono::high_resolution_clock::now();
			messages_sent += 1;
		}
		while (true) {
			auto w = socket_outgoing_buffer.write_to(socket);
			bytes_sent += w;
			if (w == 0)
				break;
		}
	}
	void on_socket_closed() {
		send_time.reset();
		socket_incoming_buffer.clear();
		socket_outgoing_buffer.clear();
		reconnect_timer.once(1);
		//		std::cout << "Upstream socket disconnected" << std::endl;
	}
	void connect() {
		if (!socket.connect(address)) {
			reconnect_timer.once(1);
		} else {
			//			std::cout << "Upstream socket connection attempt started..." << std::endl;
			clear_stats();
			send_more_requests();
		}
	}
	void clear_stats() {
		messages_sent  = 0;
		bytes_sent     = 0;
		bytes_received = 0;
		durations.clear();
		total_duration = 0;
	}
	void print_stats() {
		/*		stat_timer.once(1);
		        uint64_t lat = 0;
		        if (!durations.empty()) {
		            lat = total_duration / durations.size();
		        }
		            std::cout << "msg sent/bytes sent/received/avg latency=" << messages_sent << "/" << bytes_sent <<
		   "/"
		                      << bytes_received << "/" << lat << " microsec" << std::endl;
		*/
		clear_stats();
	}
	int64_t &external_duration;
	size_t &external_count;
	int64_t &external_max_latency;
	const crab::Address address;

	crab::TCPSocket socket;
	crab::Buffer socket_incoming_buffer;
	crab::Buffer socket_outgoing_buffer;

	crab::Timer reconnect_timer;

	size_t messages_sent  = 0;
	size_t bytes_sent     = 0;
	size_t bytes_received = 0;
	crab::Timer stat_timer;
	std::deque<uint64_t> durations;
	uint64_t total_duration = 0;

	crab::optional<std::chrono::high_resolution_clock::time_point> send_time;
	uint64_t sent_id = 0;

	crab::Random rnd;
};

int main(int argc, char *argv[]) {
	std::cout << "crablib version " << crab::version_string() << std::endl;

	std::cout << "This client send requests via TCP to api_server and reads and discards responses" << std::endl;
	if (argc < 2) {
		std::cout << "Usage: api_client <ip>:<port> <instances> [Default: 1]" << std::endl;
		std::cout << "    api_client will send request sone by one and measure latencies" << std::endl;
		return 0;
	}
	crab::RunLoop runloop;

	std::list<ApiClientApp> apps;
	const size_t count = argc < 3 ? 1 : crab::integer_cast<size_t>(argv[2]);

	int64_t external_duration    = 0;
	size_t external_count        = 0;
	int64_t external_max_latency = 0;

	for (size_t i = 0; i != count; ++i)
		apps.emplace_back(crab::Address(argv[1]), external_duration, external_count, external_max_latency);

	crab::Timer t0([&] {
		uint64_t lat = 0;
		if (external_count != 0) {
			lat = external_duration / external_count;
		}
		std::cout << "msg sent/avg latency/max latency=" << external_count << "/" << lat << " microsec/"
		          << external_max_latency << " microsec" << std::endl;
		external_duration    = 0;
		external_count       = 0;
		external_max_latency = 0;
		t0.once(1);
	});
	t0.once(1);

	runloop.run();
	return 0;
}
