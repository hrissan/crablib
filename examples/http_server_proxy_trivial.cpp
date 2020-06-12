// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <iostream>
#include <map>

#include <crab/crab.hpp>

namespace http = crab::http;

class ServerProxyTrivial {
public:
	explicit ServerProxyTrivial(uint16_t port) : server(port) {
		server.r_handler = [&](http::Client *who, http::Request &&request) {
			waiting_requests.emplace_back();
			auto it = --waiting_requests.end();
			waiting_counters.emplace_back(++next_counter);
			auto wit = --waiting_counters.end();
			it->set_handlers(
			    [this, who, it, wit](http::Response &&resp) {
				    std::cout << "Success " << *wit << std::endl;
				    who->write(std::move(resp));
				    waiting_counters.erase(wit);
				    waiting_requests.erase(it);
			    },
			    [this, who, it, wit](std::string &&err) {
				    std::cout << "Error " << *wit << std::endl;
				    who->write(http::Response::simple_text(503, std::move(err)));
				    waiting_counters.erase(wit);
				    waiting_requests.erase(it);
			    });
			std::cout << "Sending request " << *wit << std::endl;
			request.header.host = "www.alawar.com";
			it->send(std::move(request), 443, "https");
			who->start_long_poll([this, it, wit]() {
				std::cout << "Disconnect " << *wit << std::endl;
				waiting_counters.erase(wit);
				waiting_requests.erase(it);
			});
		};
	}

private:
	http::Server server;
	int next_counter = 0;
	std::list<int> waiting_counters;
	std::list<http::ClientRequestSimple> waiting_requests;
};

int main(int argc, char *argv[]) {
	std::cout << "crablib version " << crab::version_string() << std::endl;

	std::cout << "This server proxies requests to another server" << std::endl;
	crab::RunLoop runloop;

	crab::SignalStop sig([&]() {
		std::cout << "Good bye" << std::endl;
		crab::RunLoop::current()->cancel();
	});

	ServerProxyTrivial app(7000);

	runloop.run();
	return 0;
}
