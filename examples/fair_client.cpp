// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <ifaddrs.h>
#include <iostream>
#include <set>

#include <crab/crab.hpp>

class FairClientApp {
public:
	explicit FairClientApp(const std::string &address, uint16_t port)
	    : address(address)
	    , port(port)
	    , socket([&]() { on_socket_data(); }, [&]() { on_socket_closed(); })
	    , socket_buffer(4096)
	    , reconnect_timer([&]() { connect(); }) {
		connect();
	}

private:
	void on_socket_data() {
		/*        while (true) {
		            if (socket_buffer.size() < Msg::size)
		                socket_buffer.read_from(socket);
		            const size_t max_count = MAX_DATAGRAM_SIZE / Msg::size;
		            size_t count           = std::min(max_count, upstream_socket_buffer.size() / Msg::size);
		            if (count == 0)
		                break;
		            crab::VectorStream vs;
		            upstream_socket_buffer.write_to(vs, count * Msg::size);
		            if (udp_a.write_datagram(vs.get_buffer().data(), vs.get_buffer().size()) !=
		   vs.get_buffer().size()) { std::cout << "UDP retransmission buffer full, dropping message" << std::endl;
		            }
		            while (vs.size() >= Msg::size) {
		                Msg msg;
		                msg.read(&vs);
		                message_handler(msg);
		            }
		        }*/
	}
	void on_socket_closed() {
		socket_buffer.clear();
		reconnect_timer.once(1);
		std::cout << "Upstream socket disconnected" << std::endl;
	}
	void connect() {
		if (!socket.connect(address, port)) {
			reconnect_timer.once(1);
		} else {
			std::cout << "Upstream socket connection attempt started..." << std::endl;
		}
	}

	const std::string address;
	const uint16_t port;

	crab::TCPSocket socket;
	crab::Buffer socket_buffer;

	crab::Timer reconnect_timer;
};

int main(int argc, char *argv[]) {
	std::cout
	    << "This client send requests (1 byte) via TCP to fair_server and measures latency of responses (1 uint64_t)"
	    << std::endl;
	crab::RunLoop runloop;

	FairClientApp app("127.0.0.1", 7000);

	runloop.run();
	return 0;
}
