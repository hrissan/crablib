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
	ClientWebSocketApp(const std::string &host, uint16_t port)
	    : ws([&]() { on_ws_data(); }, [&]() { on_ws_closed(); })
	    , reconnect_timer([&]() { connect(); })
	    , send_timer([&]() { send_message(); })
	    , host(host)
	    , port(port) {
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
		req.path = "/ws";
		ws.connect(host, port);
		ws.web_socket_upgrade(req);
		std::cout << "Upstream socket connection attempt started..." << std::endl;
		message_counter = 0;
		send_timer.once(1);
	}
	void send_message() {
		std::cout << "Sending message " << message_counter << std::endl;
		ws.write(http::WebMessage{http::WebMessage::OPCODE_TEXT, "Message " + std::to_string(message_counter)});
		message_counter += 1;
		send_timer.once(1);
	}
	http::ClientConnection ws;
	Timer reconnect_timer;
	Timer send_timer;
	size_t message_counter = 0;
	const std::string host;
	uint16_t port = 0;
};

int main(int argc, char *argv[]) {
	std::cout << "This client send web socket request to http_server_complex" << std::endl;
	if (argc < 3) {
		std::cout << "Usage: client_web_socket host <port>" << std::endl;
		return 0;
	}

	crab::RunLoop runloop;

	ClientWebSocketApp app(argv[1], crab::integer_cast<uint16_t>(argv[2]));

	runloop.run();

	return 0;
}
