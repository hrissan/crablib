// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <ifaddrs.h>
#include <iostream>
#include <set>

#include <crab/crab.hpp>

class FairServerApp {
public:
	explicit FairServerApp(uint16_t port)
	    : la_socket("0.0.0.0", port, [&]() { accept_all(); }), idle([&]() { on_idle(); }) {}

private:
	crab::TCPAcceptor la_socket;

	struct Client {
		std::unique_ptr<crab::BufferedTCPSocket> socket;
		crab::Buffer socket_buffer;
		crab::IntrusiveNode<Client> fair_queue;

		Client() : socket_buffer(4096) {}
	};
	using ClientList = std::list<Client>;
	ClientList clients;

	crab::IntrusiveList<Client, &Client::fair_queue> fair_queue;
	// clients in fair_queue are considered low-priority and served in on_idle
	// callbacks for such clients are ignored
	// client is put in fair_queue if it has more than 1 request pending (sending batch requests)

	// if bunch of clients is in fair_queue, request from fresh client will have low latency

	crab::Idle idle;

	uint64_t seqnum = 0;
	enum { MSG_SIZE = 1 };

	void on_idle() {
		const size_t MAX_COUNTER = 100;
		// we will call epoll() once per MAX_COUNTER messages, trading latency for throughput
		size_t counter = 0;
		while (!fair_queue.empty() && ++counter < MAX_COUNTER) {
			Client &c = *fair_queue.begin();
			c.fair_queue.unlink();
			if (process_single_request(c))
				continue;
			fair_queue.push_back(c);
		}
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
		if (client.socket->get_total_buffer_size() != 0)
			return true;  // Do not read from client with too much responses in queue
		if (client.socket_buffer.size() < MSG_SIZE) {
			// Must have at least capacity of 2 * MSG_SIZE for the logic to work correctly
			client.socket_buffer.read_from(*client.socket);
			if (client.socket_buffer.size() < MSG_SIZE)
				return true;  // No more requests
		}
		client.socket_buffer.did_read(MSG_SIZE);  // Skip
		busy_sleep_microseconds(40);              // Simulate processing latency
		seqnum += 1;
		client.socket->write(reinterpret_cast<const uint8_t *>(&seqnum), sizeof(uint64_t));
		return client.socket_buffer.size() < MSG_SIZE;  // No more requests
	}
	void on_client_handler(ClientList::iterator it) {
		if (it->fair_queue.in_list())  // clients in fair_queue wait for their turn
			return;
		// We respond immediately to the first request
		if (process_single_request(*it))
			return;  // No more requests
		// Then, if more requests requests pending, add client into fair_queue
		fair_queue.push_back(*it);  // Will have at least MSG_SIZE in buffer
	}
	void on_client_disconnected(ClientList::iterator it) {
		clients.erase(it);  // automatically unlinks from fair_queue
		std::cout << "HTTP Client disconnected #=" << clients.size() << std::endl;
	}
	void accept_all() {
		while (la_socket.can_accept()) {  // && clients.size() < max_incoming_connections &&
			clients.emplace_back();
			auto it = --clients.end();
			clients.back().socket.reset(new crab::BufferedTCPSocket(
			    [this, it]() { on_client_handler(it); }, [this, it]() { on_client_disconnected(it); }));
			std::string addr;
			clients.back().socket->accept(la_socket, &addr);
			std::cout << "HTTP Client accepted #=" << clients.size() << " addr=" << addr << std::endl;

			// Before login, clients are assigned low-priority
			// In actual fair server, there would be separate queue for not-yet-logged in clients
			// so that server can select ratio between processing logged-in versus not logged-in clients

			// also actual fair server will ensure that 2 connections from the same login are either not allowed
			// or at least occupy single slot in fair_queue, and have timeouts for connections
			fair_queue.push_back(clients.back());
		}
	}
};

int main(int argc, char *argv[]) {
	std::cout << "This server responds to requests from bunch of fair_client via TCP in fair manner - each one gets"
	          << std::endl;
	crab::RunLoop runloop;

	FairServerApp app(7000);

	runloop.run();
	return 0;
}
