// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <iostream>
#include <set>

#include <crab/crab.hpp>
#include "gate_message.hpp"

class FairServerApp {
public:
	explicit FairServerApp(const crab::Address &bind_address, bool sleep_thread = false)
	    : sleep_thread(sleep_thread)
	    , la_socket(bind_address, [&]() { accept_all(); })
	    , idle([&]() { on_idle(); })
	    , stat_timer([&]() { print_stats(); }) {
		print_stats();
	}

private:
	const bool sleep_thread;
	crab::TCPAcceptor la_socket;

	struct Client {
		size_t client_id = 0;
		crab::BufferedTCPSocket socket;
		crab::Buffer socket_buffer;
		crab::IntrusiveNode<Client> fair_queue_node;
		size_t total_read    = 0;
		size_t total_written = 0;

		Client() : socket(crab::empty_handler), socket_buffer(4096) {}
	};
	using ClientList = std::list<Client>;
	ClientList clients;

	crab::IntrusiveList<Client, &Client::fair_queue_node> fair_queue;
	// clients in fair_queue are considered low-priority and served in on_idle
	// callbacks for such clients are ignored
	// client is put in fair_queue if it has more than 1 request pending (sending batch requests)

	// if bunch of clients is in fair_queue, request from fresh client will have low latency

	// IntrusiveList is good as a queue due to O(1) removal cost and auto-remove in Node destructor

	crab::Idle idle;

	crab::Timer stat_timer;
	size_t requests_processed = 0;
	size_t clients_accepted   = 0;

	uint64_t seqnum = 0;
	enum { REQUEST_SIZE = 1 };

	void on_idle() {
		const size_t MAX_COUNTER = 1;
		// we will call epoll() once per MAX_COUNTER messages, trading latency for throughput
		size_t counter = 0;
		while (!fair_queue.empty() && counter++ < MAX_COUNTER) {
			Client &c = *fair_queue.begin();
			c.fair_queue_node.unlink();
			if (process_single_request(c)) {
				continue;
			}
			fair_queue.push_back(c);
		}
		accept_single();
		if (sleep_thread)
			idle.set_active(!fair_queue.empty() || la_socket.can_accept());
	}
	static void busy_sleep_microseconds(int msec) {
		auto start = std::chrono::steady_clock::now();
		while (true) {
			auto now = std::chrono::steady_clock::now();
			if (std::chrono::duration_cast<std::chrono::microseconds>(now - start).count() > msec)
				break;
		}
	}
	bool process_single_request(Client &client) {
		if (client.socket.get_total_buffer_size() != 0) {
			std::cout << "Write buffer full=" << client.socket.get_total_buffer_size() << std::endl;
			return true;  // Remove from fair_queue until buffer clears
		}
		if (client.socket_buffer.size() < REQUEST_SIZE) {
			// Must have at least capacity of 2 * REQUEST_SIZE for the logic to work correctly
			client.total_read += client.socket_buffer.read_from(client.socket);
			if (client.socket_buffer.size() < REQUEST_SIZE) {
				return true;  // No more requests
			}
		}
		client.socket_buffer.did_read(REQUEST_SIZE);  // Skip
		busy_sleep_microseconds(5);                   // Simulate processing latency
		seqnum += 1;
		client.socket.write(reinterpret_cast<const uint8_t *>(&seqnum), sizeof(uint64_t), true);
		client.socket.write(reinterpret_cast<const uint8_t *>(&seqnum), sizeof(uint64_t));
		client.total_written += 2 * sizeof(uint64_t);
		requests_processed += 1;
		return false;  // client.socket_buffer.size() < REQUEST_SIZE;  // No more requests
	}
	void on_client_handler(ClientList::iterator it) {
		if (!it->socket.is_open())
			return on_client_disconnected(it);
		if (it->fair_queue_node.in_list())  // clients in fair_queue wait for their turn
			return;
		if (it->socket.get_total_buffer_size() != 0) {
			// do not process requests for clients not reading their responses
			// We respond immediately to the first request
			return;
		}
		if (process_single_request(*it))
			return;  // No more requests
		// Then, if more requests requests pending, add client into fair_queue
		fair_queue.push_back(*it);  // Will have at least REQUEST_SIZE in buffer
		if (sleep_thread)
			idle.set_active(!fair_queue.empty() || la_socket.can_accept());
	}
	void on_client_disconnected(ClientList::iterator it) {
		clients.erase(it);  // automatically unlinks from fair_queue
		                    //		std::cout << "Fair Client " << clients.back().client_id
		//		          << " disconnected, current number of clients is=" << clients.size() << std::endl;
	}
	void accept_all() {
		std::cout << "accept socket event, current number of clients is=" << clients.size() << std::endl;
		accept_single();
		if (sleep_thread)
			idle.set_active(!fair_queue.empty() || la_socket.can_accept());
	}
	void accept_single() {
		if (!la_socket.can_accept())  // || clients.size() >= max_incoming_connections &&
			return;
		clients.emplace_back();
		auto it = --clients.end();
		clients_accepted += 1;
		clients.back().client_id = clients_accepted;
		clients.back().socket.set_handler([this, it]() { on_client_handler(it); });
		crab::Address addr;
		clients.back().socket.accept(la_socket, &addr);
		//            std::cout << "Fair Client " << clients.back().client_id
		//                      << " accepted, current number of clients is=" << clients.size()
		//                      << " addr=" << addr.get_address() << ":" << addr.get_port() << std::endl;

		// Before login, clients are assigned low-priority
		// In actual fair server, there would be separate queue for not-yet-logged in clients
		// so that server can select ratio between processing logged-in versus not logged-in clients

		// also actual fair server will ensure that 2 connections from the same login are either not allowed
		// or at least occupy single slot in fair_queue, and have timeouts for connections
		fair_queue.push_back(clients.back());
	}
	void print_stats() {
		stat_timer.once(1);
		std::cout << "requests processed (during last second)=" << requests_processed << std::endl;
		if (!clients.empty()) {
			std::cout << "Client.front read=" << clients.front().total_read
			          << " written=" << clients.front().total_written << std::endl;
			//            if (requests_processed == 0 && clients.front().total_written > 2000) {
			//                uint8_t buf[100]{};
			//                clients.front().socket.write(buf, sizeof(buf));
			//                std::cout << "Written 100 bytes" << std::endl;
			//            }
		}
		requests_processed = 0;
	}
};

int main(int argc, char *argv[]) {
	std::cout << "This server responds to requests from bunch of fair_client via TCP in fair manner -" << std::endl;
	std::cout << "    clients who send batches are served in round-robin fashion, while those" << std::endl;
	std::cout << "    who send single requests are served immediately" << std::endl;
	if (argc < 2) {
		std::cout << "Usage: fair_server <port>" << std::endl;
		return 0;
	}
	crab::RunLoop runloop;

	FairServerApp app(crab::Address("0.0.0.0", std::stoi(argv[1])));

	runloop.run();
	return 0;
}
