// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <crab/crab.hpp>
#include <iostream>
#include <set>

static const char HTML[] = R"zzz(
<!DOCTYPE HTML>
<html><head>
</head><body>
      <div>
         Chat is streaming where body is appended every second <a href = "/chat">Chat</a>
      </div>
      <div>
         Download is where 1 GB body is generated on the fly <a href = "/download">Download (With Content-Length)</a>
      </div>
      <div>
         Chunked is the same, but with chunked transfer encoding <a href = "/chunked">Download (chunked)</a>
      </div>
   </body></html>
)zzz";

namespace http = crab::http;

class ServerStreamBodyApp {
public:
	explicit ServerStreamBodyApp(uint16_t port) : server(port), timer([&]() { on_timer(); }) {
		server.r_handler = [&](http::Client *who, http::Request &&request) {
			if (request.header.path == "/") {
				http::Response response;
				response.header.status = 200;
				response.header.set_content_type("text/html", "charset=utf-8");
				response.set_body(HTML);
				who->write(std::move(response));
				return;
			}
			if (request.header.path == "/chat") {
				std::cout << "Streaming client added" << std::endl;
				waiting_clients.push_back(who);
				auto it = --waiting_clients.end();
				http::ResponseHeader header;
				header.status                    = 200;
				header.transfer_encoding_chunked = true;
				header.set_content_type("text/html", "charset=utf-8");
				who->start_write_stream(std::move(header), [this, it, who]() {
					if (!who->is_open()) {
						std::cout << "Streaming clinet disconnected" << std::endl;
						waiting_clients.erase(it);
					}
				});
				who->write(body_so_far.data(), body_so_far.size());
				return;
			}
			if (request.header.path == "/download") {
				uint64_t len = 1 * 1000 * 1000 * 1000;
				http::ResponseHeader header;
				header.status         = 200;
				header.content_length = len;
				header.set_content_type("application/octet-stream", "");
				who->start_write_stream(std::move(header), [who, len]() { write_stream_data(who, len, false); });
				return;
			}
			if (request.header.path == "/chunked") {
				uint64_t len = 1 * 1000 * 1000 * 1000;
				http::ResponseHeader header;
				header.status                    = 200;
				header.transfer_encoding_chunked = true;
				header.set_content_type("application/octet-stream", "");
				who->start_write_stream(std::move(header), [who, len]() { write_stream_data(who, len, true); });
				return;
			}
			who->write(http::Response::simple_html(404));
		};
		start_session();
		timer.once(1);
	}

private:
	static void write_stream_data(http::Client *who, uint64_t len, bool transfer_encoding_chunked) {
		if (!who->is_open()) {
			std::cout << "Client disconnected in the middle of transfer" << std::endl;
			return;
		}
		char buffer[65536]{};
		while (who->can_write() && who->get_body_position() != len) {
			auto to_write = static_cast<size_t>(std::min<uint64_t>(len - who->get_body_position(), sizeof(buffer)));
			who->write(buffer, to_write);
		}
		if (who->get_body_position() == len) {
			if (transfer_encoding_chunked)
				who->write_last_chunk();
			std::cout << "Downloader finished" << std::endl;
		} else {
			std::cout << "Downloader buffer full, will continue writing later position=" << who->get_body_position()
			          << std::endl;
		}
	}
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
		const size_t total_lines = 25;
		timer.once(1);

		ticks_counter += 1;
		std::string next_line =
		    "Next line is " + std::to_string(ticks_counter) + " out of " + std::to_string(total_lines) + "<br/>";
		body_so_far += next_line;
		for (auto who : waiting_clients) {
			who->write(std::string(next_line));
		}
		if (ticks_counter >= total_lines)
			start_session();
	}
	http::Server server;
	crab::Timer timer;
	size_t ticks_counter = 0;
	std::list<http::Client *> waiting_clients;
	std::string body_so_far;
};

int main(int argc, char *argv[]) {
	std::cout << "crablib version " << crab::version_string() << std::endl;

	std::cout << "This server slowly streams long body to clients" << std::endl;
	crab::RunLoop runloop;

	ServerStreamBodyApp app(7000);

	runloop.run();
	return 0;
}
