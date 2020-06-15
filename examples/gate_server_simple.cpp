// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <unordered_map>

#include <crab/crab.hpp>

#include "gate_message.hpp"

// TODO - rewrite gate examples for clarity

using namespace crab;

int test_http(size_t num, uint16_t port) {
	RunLoop runloop;
	//	Idle idle([](){});
	std::list<http::Client *> connected_sockets;
	http::Server server(port);
	server.r_handler = [&](http::Client *who, http::Request &&request) {
		if (request.header.path == "/latency") {
			connected_sockets.push_back(who);
			auto it = --connected_sockets.end();
			who->web_socket_upgrade([&connected_sockets, it, who](http::WebMessage &&message) {
				if (message.is_close()) {
					std::cout << "Server Got Close Message: " << message.body << std::endl;
					connected_sockets.erase(it);
					return;
				}
				std::cout << "Server Got Message: " << message.body << std::endl;
				LatencyMessage lm;
				if (message.is_binary() || !lm.parse(message.body)) {
					who->write(http::WebMessage{http::WebMessageOpcode::CLOSE, "Error, expecting Latency Message",
					    http::WebMessage::CLOSE_STATUS_ERROR});
					return;
				}
				lm.add_lat("server", std::chrono::steady_clock::now());
				std::cout << lm.save() << std::endl;
				who->write(http::WebMessage(lm.save()));
			});
			return;
		}
		who->write(http::Response::simple_html(404));
	};

	std::unique_ptr<Timer> stat_timer;
	stat_timer.reset(new Timer([&]() {
		const auto &st = RunLoop::current()->stats;
		std::cout << num << " ---- EPOLL_count=" << st.EPOLL_count << " EPOLL_size=" << st.EPOLL_size << std::endl;
		std::cout << "RECV_count=" << st.RECV_count << " RECV_size=" << st.RECV_size << std::endl;
		std::cout << "SEND_count=" << st.SEND_count << " SEND_size=" << st.SEND_size << std::endl;
		runloop.stats.print_records(std::cout);
		stat_timer->once(1);
	}));
	stat_timer->once(1);

	runloop.run();
	return 0;
}

int main(int argc, char *argv[]) {
	std::cout << "crablib version " << crab::version_string() << std::endl;

	if (argc != 2) {
		std::cout << "Usage: server <port>" << std::endl;
		return 0;
	}
	return test_http(0, crab::integer_cast<uint16_t>(argv[1]));
}
