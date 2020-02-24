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
	std::set<http::Client *> connected_sockets;
	http::Server server(port);
	server.r_handler = [&](http::Client *who, http::RequestBody &&request, http::ResponseBody &response) -> bool {
		if (request.r.path == "/latency") {
			who->web_socket_upgrade();
			connected_sockets.insert(who);
			return false;
		}
		return true;
	};
	server.d_handler = [&](http::Client *who) { connected_sockets.erase(who); };
	server.w_handler = [&](http::Client *who, http::WebMessage &&message) {
		//		std::cout << "Server Got Message: " << message.body << std::endl;
		LatencyMessage lm;
		if (message.is_binary() || !lm.parse(message.body)) {
			who->write(http::WebMessage(http::WebMessage::OPCODE_CLOSE, "Error, expecting Latency Message"));
			return;
		}
		lm.add_lat("server", std::chrono::steady_clock::now());
		std::cout << lm.save() << std::endl;
		who->write(http::WebMessage(lm.save()));
	};

	std::unique_ptr<Timer> stat_timer;
	stat_timer.reset(new Timer([&]() {
		const auto &st = RunLoop::get_stats();
		std::cout << num << " ---- EPOLL_count=" << st.EPOLL_count << " EPOLL_size=" << st.EPOLL_size << std::endl;
		std::cout << "RECV_count=" << st.RECV_count << " RECV_size=" << st.RECV_size << std::endl;
		std::cout << "SEND_count=" << st.SEND_count << " SEND_size=" << st.SEND_size << std::endl;
		runloop.print_records();
		stat_timer->once(1);
	}));
	stat_timer->once(1);

	runloop.run();
	return 0;
}

int main(int argc, char *argv[]) {
	if (argc != 2) {
		std::cout << "Usage: server <port>" << std::endl;
		return 0;
	}
	return test_http(0, std::stoi(argv[1]));
}
