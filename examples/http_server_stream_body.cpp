// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <iostream>
#include <set>

#include <crab/crab.hpp>

namespace http = crab::http;

class ServerLongPollApp {
public:
	explicit ServerLongPollApp(uint16_t port) : server(port), timer([&]() { on_timer(); }) {
		server.r_handler = [&](http::Client *who, http::RequestBody &&request) {
			if (request.r.path == "/chat") {
				waiting_clients.emplace(who);
				http::ResponseHeader r;
				r.status                    = 200;
				r.transfer_encoding_chunked = true;
				r.set_content_type("text/html", "charset=utf-8");
				who->write(std::move(r), true);
				who->write(body_so_far.c_str(), body_so_far.size());
				return;
			}
			who->write(http::ResponseBody::simple_html(404));
		};
		server.d_handler = [&](http::Client *who) { waiting_clients.erase(who); };
		start_session();
		timer.once(1);
	}

private:
	void start_session() {
		for (auto who : waiting_clients) {
			who->write(std::string{"</body></html>"});
			who->write_last_chunk();
		}
		waiting_clients.clear();
		body_so_far   = "<html><head></head><body>";
		ticks_counter = 0;
	}
	void on_timer() {
		timer.once(1);

		ticks_counter += 1;
		std::string next_line = "Next line is " + std::to_string(ticks_counter) + "<br/>";
		body_so_far += next_line;
		for (auto who : waiting_clients) {
			who->write(std::string(next_line));
		}
		if (ticks_counter == 50)
			start_session();
	}
	http::Server server;
	crab::Timer timer;
	size_t ticks_counter = 0;
	std::set<http::Client *> waiting_clients;
	std::string body_so_far;
};

int main(int argc, char *argv[]) {
	std::cout << "This server slowly streams long body to clients" << std::endl;
	crab::RunLoop runloop;

	ServerLongPollApp app(7000);

	runloop.run();
	return 0;
}
