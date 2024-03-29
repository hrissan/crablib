// Copyright (c) 2007-2023, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>

#include <crab/crab.hpp>
#include "gate_message.hpp"

// TODO - rewrite gate examples for clarity

using namespace crab;

int test_proxy(int num, uint16_t port, uint16_t upstream_port) {
	RunLoop runloop;

	crab::Random rnd;

	std::unique_ptr<Timer> stat_timer;
	std::unique_ptr<http::ClientConnection> rws;

	std::map<http::Client *, std::string> connected_sockets;
	std::map<std::string, http::Client *> connected_sockets_inv;
	http::Server server(port);

	server.r_handler = [&](http::Client *who, http::Request &&request) {
		if (request.header.path == "/latency") {
			who->web_socket_upgrade([&, who](http::WebMessage &&message) {
				if (message.is_close()) {
					auto it = connected_sockets.find(who);
					if (it == connected_sockets.end())
						return;
					connected_sockets_inv.erase(it->second);
					connected_sockets.erase(it);
					return;
				}
				LatencyMessage lm;
				if (!lm.parse(message.body))
					return;
				auto it = connected_sockets.find(who);
				if (it == connected_sockets.end())
					return;
				lm.add_lat("proxy_recv_client", std::chrono::steady_clock::now());
				//        std::cout << lm.save(&it->second) << std::endl;
				rws->write(http::WebMessage(lm.save(&it->second)));

				runloop.stats.print_records(std::cout);
			});
			std::string id = rnd.printable_string(16);

			connected_sockets.emplace(who, id);
			connected_sockets_inv.emplace(id, who);
			return;
		}
		who->write(http::Response::simple_html(404));
	};

	// size_t message_counter = 0;
	// auto message_start     = std::chrono::high_resolution_clock::now();

	rws.reset(new http::ClientConnection([&]() {
		if (!rws->is_open()) {
			std::cout << std::endl << "test_disconnect" << std::endl;
			return;
		}
		http::WebMessage wm;
		while (rws->read_next(wm)) {
			LatencyMessage lm;
			std::string id2;
			if (!lm.parse(wm.body, &id2))
				continue;
			auto it = connected_sockets_inv.find(id2);
			if (it == connected_sockets_inv.end())
				continue;
			lm.id.clear();
			lm.add_lat("proxy_recv_upstrean", std::chrono::steady_clock::now());
			//                    std::cout << lm.save() << std::endl;

			http::WebMessage reply;
			reply.opcode = http::WebMessageOpcode::TEXT;
			reply.body   = lm.save();
			it->second->write(std::move(reply));
			runloop.stats.print_records(std::cout);
		}
	}));

	http::RequestHeader req;
	req.path = "/ws";
	rws->connect(crab::Address("127.0.0.1", upstream_port));
	rws->web_socket_upgrade(req);

	stat_timer.reset(new Timer([&]() { runloop.stats.print_records(std::cout); }));
	stat_timer->once(1);

	runloop.run();
	return 1;
}

int main(int argc, char *argv[]) {
	std::cout << "crablib version " << crab::version_string() << std::endl;

	return test_proxy(0, 7000, 7001);
}
