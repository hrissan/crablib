// Copyright (c) 2007-2019, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <iostream>
#include <map>

#include <crab/crab.hpp>

namespace http = crab::http;

class ServerLongPollApp {
public:
	explicit ServerLongPollApp(uint16_t port) : server("0.0.0.0", port), timer([&]() { on_timer(); }) {
		server.r_handler = [&](http::Client *who, http::RequestBody &&request, http::ResponseBody &response) -> bool {
			waiting_clients.emplace(who, ticks_counter + 5);
			waiting_clients_inv.emplace(ticks_counter + 5, who);
			return false;  // We did not respond immediately
		};
		server.d_handler = [&](http::Client *who) {
			auto wit = waiting_clients.find(who);
			if (wit == waiting_clients.end())
				return;
			waiting_clients_inv.erase(wit->second);
			waiting_clients.erase(wit);
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
			waiting_clients.erase(who);

			http::ResponseBody response;
			response.r.status       = 200;
			response.r.content_type = "text/plain; charset=utf-8";
			response.set_body("Hello, Crab " + std::to_string(ticks_counter) + "!");
			who->write(std::move(response));
		}
	}
	http::Server server;
	crab::Timer timer;
	size_t ticks_counter = 0;
	std::map<http::Client *, size_t> waiting_clients;
	std::map<size_t, http::Client *> waiting_clients_inv;
};

int main(int argc, char *argv[]) {
	std::cout << "This server responds to requests approximately after 5 seconds" << std::endl;
	crab::RunLoop runloop;

	ServerLongPollApp app(7000);

	runloop.run();
	return 0;
}
