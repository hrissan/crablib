// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <iostream>
#include <map>

#include <crab/crab.hpp>

namespace http = crab::http;

class ServerLongPollApp {
public:
	explicit ServerLongPollApp(uint16_t port) : server(port), timer([&]() { on_timer(); }) {
		server.r_handler = [&](http::Client *who, http::Request &&request) {
			auto res = waiting_clients_inv.emplace(ticks_counter + 5, who);
			who->postpone_response([this, res]() { waiting_clients_inv.erase(res); });
		};
		timer.once(1);
	}

private:
	void on_timer() {
		timer.once(1);

		ticks_counter += 1;
		while (!waiting_clients_inv.empty() && waiting_clients_inv.begin()->first <= ticks_counter) {
			auto who = waiting_clients_inv.begin()->second;
			waiting_clients_inv.erase(waiting_clients_inv.begin());

			http::Response response;
			response.header.status = 200;
			response.header.set_content_type("text/plain", "charset=utf-8");
			response.set_body("Hello, Crab " + std::to_string(ticks_counter) + "!");
			who->write(std::move(response));
		}
	}
	http::Server server;
	crab::Timer timer;
	size_t ticks_counter = 0;
	std::multimap<size_t, http::Client *> waiting_clients_inv;
};

int main(int argc, char *argv[]) {
	std::cout << "crablib version " << crab::version_string() << std::endl;

	std::cout << "This server responds to requests approximately after 5 seconds" << std::endl;
	crab::RunLoop runloop;

	crab::Signal sig([&]() {
		std::cout << "Good bye" << std::endl;
		crab::RunLoop::current()->cancel();
	});

	ServerLongPollApp app(7000);

	runloop.run();
	return 0;
}
