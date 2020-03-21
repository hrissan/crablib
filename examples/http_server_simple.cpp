// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <iostream>

#include <crab/crab.hpp>

namespace http = crab::http;

/*
class FunnyServerApp {
public:
    explicit FunnyServerApp(const crab::Address &bind_address)
        : la_socket(bind_address, [&]() { accept_all(); })
        , stat_timer([&]() { print_stats(); }) {
        print_stats();
    }

private:
    crab::TCPAcceptor la_socket;

    struct Client {
        crab::BufferedTCPSocket socket;
        crab::Buffer socket_buffer;

        Client() : socket(crab::empty_handler), socket_buffer(4096) {}
    };
    using ClientList = std::list<Client>;
    ClientList clients;

    crab::Timer stat_timer;

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
        // Read from socket here
    }
    void on_client_disconnected(ClientList::iterator it) {
        clients.erase(it);
    }
    void accept_all() {
        std::cout << "accept socket event, current number of clients is=" << clients.size() << std::endl;
        while(la_socket.can_accept()){
            clients.emplace_back();
            auto it = --clients.end();
            clients_accepted += 1;
            clients.back().client_id = clients_accepted;
            clients.back().socket.set_handler([this, it]() { on_client_handler(it); });
            crab::Address addr;
            clients.back().socket.accept(la_socket, &addr);
        }
    }
    void print_stats() {
        stat_timer.once(1);
        std::cout << "requests processed (during last second)=" << requests_processed << std::endl;
        for (const client : clients) {
            client.socket.write(, , );
        }
    }
};*/

int main() {
	std::cout << "This is simple HTTP server on port 7000" << std::endl;
	crab::RunLoop runloop;

	http::Server server(7000);

	server.r_handler = [&](http::Client *who, http::RequestBody &&request) {
		http::ResponseBody response;
		response.r.status = 200;
		response.r.set_content_type("text/plain", "charset=utf-8");
		response.set_body("Hello, Crab!");
		who->write(std::move(response));
	};

	runloop.run();
	return 0;
}
