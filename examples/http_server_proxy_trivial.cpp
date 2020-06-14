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
			auto it      = --waiting_requests.end();
			auto counter = ++next_counter;
			it->set_handlers(
			    [this, who, it, counter](http::Response &&resp) {
				    std::cout << "Success " << counter << std::endl;
				    who->write(std::move(resp));
				    waiting_requests.erase(it);
			    },
			    [this, who, it, counter](std::string &&err) {
				    std::cout << "Error " << counter << std::endl;
				    who->write(http::Response::simple_text(503, std::move(err)));
				    waiting_requests.erase(it);
			    });
			std::cout << "Sending request " << counter << std::endl;
			request.header.host = "www.alawar.com";
			it->send(std::move(request), 443, "https");
			who->postpone_response([this, it, counter]() {
				std::cout << "Disconnect " << counter << std::endl;
				waiting_requests.erase(it);
			});
		};
	}

private:
	http::Server server;
	int next_counter = 0;
	std::list<http::ClientRequestSimple> waiting_requests;
};

class ServerProxyTrivial2 {
public:
	explicit ServerProxyTrivial2(uint16_t port) : server(port) {
		server.r_handler = [&](http::Client *who, http::Request &&request) {
			auto req     = std::make_shared<http::ClientRequestSimple>();
			auto counter = ++next_counter;
			req->set_handlers(
			    [who, counter](http::Response &&resp) {
				    std::cout << "Success " << counter << std::endl;
				    who->write(std::move(resp));
			    },
			    [who, counter](std::string &&err) {
				    std::cout << "Error " << counter << std::endl;
				    who->write(http::Response::simple_text(503, std::move(err)));
			    });
			std::cout << "Sending request " << counter << std::endl;
			request.header.host = "www.alawar.com";
			req->send(std::move(request), 443, "https");
			who->postpone_response([req, counter]() {  // Owns req
				std::cout << "Disconnect " << counter << std::endl;
			});
		};
	}

private:
	http::Server server;
	int next_counter = 0;
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
	ServerProxyTrivial2 app2(7001);

	runloop.run();
	return 0;
}
