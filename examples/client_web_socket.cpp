// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <algorithm>
#include <iostream>
#include <set>
#include <sstream>

#include <crab/crab.hpp>

using namespace crab;

class ClientWebSocketApp {
public:
	ClientWebSocketApp(const crab::Address &address, const std::string &host)
	    : ws([&]() { on_ws_data(); }, [&]() { on_ws_closed(); })
	    , reconnect_timer([&]() { connect(); })
	    , send_timer([&]() { send_message(); })
	    , address(address)
	    , host(host) {
		connect();
	}

private:
	void on_ws_data() {
		http::WebMessage wm;
		while (ws.read_next(wm)) {
			if (wm.is_binary()) {
				std::cout << "Client Got Binary Message: " << crab::to_hex(wm.body.data(), wm.body.size())
				          << std::endl;
			} else {
				std::cout << "Client Got Message: " << wm.body << std::endl;
			}
		}
	}
	void on_ws_closed() {
		reconnect_timer.once(1);
		send_timer.cancel();
		std::cout << "Upstream socket disconnected" << std::endl;
	}
	void connect() {
		http::RequestHeader req;
		req.host = host;
		req.path = "/ws";
		if (!ws.connect(address, req)) {
			reconnect_timer.once(1);
		} else {
			std::cout << "Upstream socket connection attempt started..." << std::endl;
			message_counter = 0;
			send_timer.once(1);
		}
	}
	void send_message() {
		std::cout << "Sending message " << message_counter << std::endl;
		ws.write(http::WebMessage{http::WebMessage::OPCODE_TEXT, "Message " + std::to_string(message_counter)});
		message_counter += 1;
		send_timer.once(1);
	}
	http::WebSocket ws;
	Timer reconnect_timer;
	Timer send_timer;
	size_t message_counter = 0;
	const crab::Address address;
	const std::string host;
};

int main(int argc, char *argv[]) {
	std::cout << "This client send web socket request to http_server_complex" << std::endl;
	if (argc < 3) {
		std::cout << "Usage: client_web_socket <ip>:<port> host" << std::endl;
		return 0;
	}

	crab::RunLoop runloop;

	ClientWebSocketApp app(crab::Address(argv[1]), argv[2]);

	runloop.run();

	return 0;
}
