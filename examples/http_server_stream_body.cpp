// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <crab/crab.hpp>
#include <iostream>
#include <set>

namespace http = crab::http;

class ServerStreamBodyApp {
public:
	explicit ServerStreamBodyApp(uint16_t port) : server(port), timer([&]() { on_timer(); }) {
		server.r_handler = [&](http::Client *who, http::Request &&request) {
			if (request.header.path == "/chat") {
				std::cout << "Streaming client added" << std::endl;
				waiting_clients.emplace(who);
				http::ResponseHeader header;
				header.status                    = 200;
				header.transfer_encoding_chunked = true;
				header.set_content_type("text/html", "charset=utf-8");
				who->write(std::move(header), crab::BUFFER_ONLY);
				who->write(body_so_far.c_str(), body_so_far.size());
				return;
			}
			if (request.header.path == "/download") {
				http::ResponseHeader header;
				header.status         = 200;
				header.content_length = 1 * 1000 * 1000 * 1000;
				header.set_content_type("application/octet-stream", "");
				who->write(std::move(header), [who](uint64_t pos, crab::details::optional<uint64_t> len) {
					char buffer[65536]{};
					auto to_write = static_cast<size_t>(std::min<uint64_t>(*len - pos, sizeof(buffer)));
					auto wr       = who->write_some(buffer, to_write);
					if (wr != to_write) {
						std::cout << "Downloader buffer full, will continue writing later" << std::endl;
					}
					if (wr + pos == *len) {
						std::cout << "Downloader finished" << std::endl;
					}
				});
				return;
			}
			who->write(http::Response::simple_html(404));
		};
		server.d_handler = [&](http::Client *who) {
			if (waiting_clients.erase(who))
				std::cout << "Streaming client disconnected" << std::endl;
		};
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

	ServerStreamBodyApp app(7000);

	runloop.run();
	return 0;
}
