// Copyright (c) 2007-2023, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <condition_variable>
#include <iostream>
#include <set>

#include <crab/crab.hpp>
#include "api_server.hpp"

const bool debug = true;

class ApiNetworkNaive {
public:
	explicit ApiNetworkNaive(const crab::Address &bind_address, const crab::TCPAcceptor::Settings &settings)
	    : la_socket(
	          bind_address,
	          [&]() { accept_all(); },
	          settings)
	    , idle([&]() { on_idle(); })
	    , stat_timer([&]() { print_stats(); }) {
		print_stats();
	}

private:
	crab::TCPAcceptor la_socket;
	crab::Idle idle;

	// Client states
	// - waiting for client responses to be sent, so it will continue reading header
	// - waiting for memory for requests to appear, so it will continue reading body

	struct Client {
		crab::IntrusiveNode<Client> disconnected_node;

		size_t client_id = 0;
		crab::TCPSocket socket{crab::empty_handler};
		crab::Buffer read_buffer{4096};
		crab::optional<ApiHeader> request_header;
		std::deque<crab::Buffer> requests;
		std::deque<crab::Buffer> responses;
		size_t total_read       = 0;
		size_t total_written    = 0;
		size_t requests_in_work = 0;
		crab::IntrusiveNode<Client> read_body_queue_node;  // Waiting turn to read request body
	};

	size_t max_clients = 128 * 1024;

	size_t clients_accepted = 0;           // We assign client_id from this counter on client accept
	std::deque<Client> allocated_clients;  // We need container that grows and does not invalidate references
	crab::IntrusiveList<Client, &Client::disconnected_node> disconnected_queue;

	crab::IntrusiveList<Client, &Client::read_body_queue_node> read_body_queue;

	crab::Timer stat_timer;
	size_t requests_received = 0;
	size_t responses_sent    = 0;

	void on_idle() {
		const size_t MAX_COUNTER = 1;
		//		// we will call epoll() once per MAX_COUNTER messages, trading latency for throughput
		size_t counter = 0;
		while (!read_body_queue.empty() && counter++ < MAX_COUNTER) {
			Client &c = *read_body_queue.begin();
			read_header(c);  // Will unlink and if not finished, link back
		}
		//			accept_single();
	}
	static void busy_sleep_microseconds(int msec) {
		auto start = std::chrono::steady_clock::now();
		while (true) {
			auto now = std::chrono::steady_clock::now();
			if (std::chrono::duration_cast<std::chrono::microseconds>(now - start).count() > msec)
				break;
		}
	}
	void process_request(Client &client, const ApiHeader &header) {
		client.responses.push_back(crab::Buffer(sizeof(ApiHeader) + header.body_len));
		client.responses.back().write(reinterpret_cast<const uint8_t *>(&header), sizeof(header));
		client.responses.back().did_write(header.body_len);  // TODO - security issue, uninitialized memory
		send_responses(client);
	}
	void read_header(Client &client) {
		client.read_body_queue_node.unlink();
		while (true) {
			ApiHeader header;
			if (client.read_buffer.peek(reinterpret_cast<uint8_t *>(&header), sizeof(header))) {
				if (client.read_buffer.size() >= sizeof(ApiHeader) + header.body_len) {
					client.read_buffer.did_read(sizeof(header));
					client.read_buffer.did_read(header.body_len);
					process_request(client, header);
					if (client.read_buffer.size() != 0) {
						read_body_queue.push_back(client);
						return;
					}
					continue;
				}
			}
			if (client.read_buffer.read_from(client.socket) == 0)
				break;
		}
	}
	void send_responses(Client &client) {
		//		if (!client.socket.can_write()) // TODO - check if this optimization is useful
		//			return;
		while (!client.responses.empty()) {
			client.total_written += client.responses.front().write_to(client.socket);
			if (!client.responses.front().empty())
				break;
			//			if (debug)
			//				std::cout << "send_responses sent complete response " << std::endl;
			client.responses.pop_front();
		}
	}
	void on_client_handler(Client &client) {
		if (!client.socket.is_open())
			return on_client_disconnected(client);
		send_responses(client);
		if (client.read_body_queue_node.in_list())
			return;
		read_header(client);
	}
	void on_client_disconnected(Client &client) {
		client.requests_in_work = 0;
		client.responses.clear();
		client.client_id = 0;
		client.socket.close();
		client.read_buffer.clear();
		client.total_read    = 0;
		client.total_written = 0;
		client.read_body_queue_node.unlink();
		disconnected_queue.push_back(client);

		accept_all();
	}
	void accept_all() {
		while (accept_single()) {
		}
	}
	bool accept_single() {
		if (!la_socket.can_accept())
			return false;
		if (disconnected_queue.empty()) {
			if (allocated_clients.size() >= max_clients)
				return false;
			allocated_clients.emplace_back();
			Client *it = &allocated_clients.back();
			allocated_clients.back().socket.set_handler([this, it]() { on_client_handler(*it); });
			disconnected_queue.push_back(allocated_clients.back());
		}
		Client &client = disconnected_queue.back();
		client.disconnected_node.unlink();
		clients_accepted += 1;
		client.client_id = clients_accepted;
		crab::Address addr;
		client.socket.accept(la_socket, &addr);
		return true;
	}
	void print_stats() {
		stat_timer.once(1);
		std::cout << "requests received/responses sent (during last second)=" << requests_received << "/" << responses_sent << std::endl;
		//		if (!clients.empty()) {
		//			std::cout << "Client.front read=" << clients.front().total_read
		//			          << " written=" << clients.front().total_written << std::endl;
		//		}
		requests_received = 0;
		responses_sent    = 0;
	}
};

class ApiServerNaiveApp {
public:
	static crab::TCPAcceptor::Settings setts() {
		crab::TCPAcceptor::Settings result;
		result.reuse_addr = true;
		result.reuse_port = true;
		result.tcp_delay  = false;
		return result;
	}
	explicit ApiServerNaiveApp(const crab::Address &bind_address, size_t threads)
	    : stop([&]() { stop_network(); }), network(bind_address, setts()) {
		for (size_t i = 1; i < threads; ++i) {
			network_threads.emplace_back([bind_address]() {
				ApiNetworkNaive network2(bind_address, setts());

				crab::RunLoop::current()->run();
			});
		}
	}

private:
	void stop_network() {
		std::cout << "Signal Stop Received" << std::endl;
		for (auto &th : network_threads)
			th.cancel();
		crab::RunLoop::current()->cancel();
	}
	crab::Signal stop;  // Must be created before other threads
	ApiNetworkNaive network;
	std::list<crab::Thread> network_threads;
};

class ApiNetworkUDPNaive {
public:
	static crab::UDPReceiver::Settings settings() {
		crab::UDPReceiver::Settings result;
		result.rcvbuf_size = result.sndbuf_size = 50 << 20;
		return result;
	}
	explicit ApiNetworkUDPNaive(const crab::Address &bind_address)
	    : socket(
	          bind_address,
	          [&]() { socket_handler(); },
	          settings())
	    , stat_timer([&]() { print_stats(); }) {
		print_stats();
	}

private:
	crab::UDPReceiver socket;
	size_t total_read    = 0;
	size_t total_written = 0;

	crab::Timer stat_timer;
	size_t requests_received = 0;
	size_t responses_sent    = 0;

	static void busy_sleep_microseconds(int msec) {
		auto start = std::chrono::steady_clock::now();
		while (true) {
			auto now = std::chrono::steady_clock::now();
			if (std::chrono::duration_cast<std::chrono::microseconds>(now - start).count() > msec)
				break;
		}
	}
	void socket_handler() {
		uint8_t data[crab::UDPReceiver::MAX_DATAGRAM_SIZE];  // uninitialized
		crab::Address peer_addr;
		while (auto a = socket.read_datagram(data, sizeof(data), &peer_addr)) {
			auto data_len = *a;
			ApiHeader header;
			memcpy(&header, data, sizeof(header));
			if (!socket.write_datagram(data, data_len, peer_addr)) {
				std::cout << "socket.write_datagram failed" << std::endl;
			}
		}
	}
	void print_stats() {
		stat_timer.once(1);
		std::cout << "requests received/responses sent (during last second)=" << requests_received << "/" << responses_sent << std::endl;
		//		if (!clients.empty()) {
		//			std::cout << "Client.front read=" << clients.front().total_read
		//			          << " written=" << clients.front().total_written << std::endl;
		//		}
		requests_received = 0;
		responses_sent    = 0;
	}
};

int main(int argc, char *argv[]) {
	std::cout << "crablib version " << crab::version_string() << std::endl;

	std::cout << "This naive server responds to requests from bunch of api_client via TCP -" << std::endl;
	std::cout << "    clients are served without fairness" << std::endl;
	std::cout << "    there is no limit on resources server uses" << std::endl;
	if (argc < 2) {
		std::cout << "Usage: api_server <port>" << std::endl;
		return 0;
	}
	{
		crab::RunLoop runloop;

		ApiNetworkUDPNaive udp(crab::Address("0.0.0.0", crab::integer_cast<uint16_t>(argv[1])));

		//		ApiServerNaiveApp app(crab::Address("0.0.0.0", crab::integer_cast<uint16_t>(argv[1])), 1);

		runloop.run();
	}
	std::cout << "Good Bye" << std::endl;
	return 0;
}
