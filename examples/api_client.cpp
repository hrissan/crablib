// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <iostream>

#include <crab/crab.hpp>
#include "api_server.hpp"

class ApiClientApp {
public:
	explicit ApiClientApp(const crab::Address &address, int64_t &external_duration, size_t &external_count, int64_t &external_max_latency)
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
		auto dur = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - *send_time);
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
				invariant(socket_incoming_buffer.peek(reinterpret_cast<uint8_t *>(&header), sizeof(header)), "Peek should succeed");
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

class ApiClientAppUDP {
public:
	explicit ApiClientAppUDP(
	    const crab::Address &address, int64_t &external_duration, size_t &external_count, int64_t &external_max_latency)
	    : external_duration(external_duration)
	    , external_count(external_count)
	    , external_max_latency(external_max_latency)
	    , address(address)
	    , socket(address, [&]() { socket_handler(); })
	    , receive_timeout_timer([&]() { on_receive_timeout(); }) {
		print_stats();
		send_request();
	}

private:
	bool process_request(const ApiHeader &header) {
		if (!send_time) {
			std::cout << "Unexpected response" << std::endl;
			return false;
		}
		if (header.rid != sent_id) {
			std::cout << "Response id is different from request id" << std::endl;
			return false;
		}
		auto dur = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - *send_time);
		receive_timeout_timer.cancel();
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
		return true;
	}
	void socket_handler() {
		uint8_t data[crab::UDPReceiver::MAX_DATAGRAM_SIZE];  // uninitialized
		while (auto a = socket.read_datagram(data, sizeof(data))) {
			//			auto data_len = *a;
			ApiHeader header;
			memcpy(&header, data, sizeof(header));
			if (process_request(header)) {
				send_request();
			}
		}
	}
	void send_request() {
		ApiHeader header;
		header.body_len = 17;  // TODO constant
		sent_id         = rnd.pod<uint64_t>();
		header.rid      = sent_id;
		uint8_t data[crab::UDPReceiver::MAX_DATAGRAM_SIZE];  // uninitialized
		memcpy(data, &header, sizeof(header));
		memset(data + sizeof(header), 0, header.body_len);  // TODO - check range

		if (!socket.write_datagram(data, sizeof(header) + header.body_len)) {
			std::cout << "socket.write_datagram failed" << std::endl;
		}
		send_time = std::chrono::high_resolution_clock::now();
		messages_sent += 1;
		receive_timeout_timer.once(1);
	}
	void on_receive_timeout() {
		send_time.reset();
		send_request();
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

	crab::UDPTransmitter socket;
	crab::Timer receive_timeout_timer;

	size_t messages_sent  = 0;
	size_t bytes_sent     = 0;
	size_t bytes_received = 0;
	std::deque<uint64_t> durations;
	uint64_t total_duration = 0;

	crab::optional<std::chrono::high_resolution_clock::time_point> send_time;
	uint64_t sent_id = 0;

	crab::Random rnd;
};

int main(int argc, char *argv[]) {
	std::cout << "crablib version " << crab::version_string() << std::endl;

	std::cout << "This client send requests via TCP to api_server and reads and discards responses" << std::endl;
	if (argc < 3) {
		std::cout << "Usage: api_client protocol <ip>:<port> <instances> [Default: 1]" << std::endl;
		std::cout << "    api_client will send request sone by one and measure latencies" << std::endl;
		return 0;
	}
	crab::RunLoop runloop;

	std::list<ApiClientApp> apps;
	std::list<ApiClientAppUDP> udp_apps;
	const size_t count = argc < 4 ? 1 : crab::integer_cast<size_t>(argv[3]);

	int64_t external_duration    = 0;
	size_t external_count        = 0;
	int64_t external_max_latency = 0;

	if (std::string(argv[1]) == "udp") {
		for (size_t i = 0; i != count; ++i)
			udp_apps.emplace_back(crab::Address(argv[2]), external_duration, external_count, external_max_latency);
	} else {
		for (size_t i = 0; i != count; ++i)
			apps.emplace_back(crab::Address(argv[2]), external_duration, external_count, external_max_latency);
	}
	crab::Timer t0([&] {
		uint64_t lat = 0;
		if (external_count != 0) {
			lat = external_duration / external_count;
		}
		std::cout << "msg sent/avg latency/max latency=" << external_count << "/" << lat << " microsec/" << external_max_latency
		          << " microsec" << std::endl;
		external_duration    = 0;
		external_count       = 0;
		external_max_latency = 0;
		t0.once(1);
	});
	t0.once(1);

	runloop.run();
	return 0;
}
