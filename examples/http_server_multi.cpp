// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <iostream>

#include <crab/crab.hpp>

namespace http = crab::http;

int test_http(size_t num, uint16_t port) {
	std::string body = "Hello, Crab " + std::to_string(num) + "!";
	crab::RunLoop runloop;

	http::Server server(port);
	server.r_handler = [&](http::Client *who, http::RequestBody &&request) {
		http::ResponseBody response;
		response.r.status = 200;
		response.r.set_content_type("text/plain", "charset=utf-8");
		response.set_body(std::string(body));
		who->write(std::move(response));
	};

	runloop.run();
	return 0;
}

int main(int argc, char *argv[]) {
	auto th_count = std::thread::hardware_concurrency();
	std::cout << "This server uses " << th_count
	          << " threads, your system must support binding several TCP acceptors to the same port" << std::endl;

	std::vector<std::thread> ths;
	for (size_t i = 1; i < th_count; ++i)
		ths.emplace_back(&test_http, i, uint16_t(7000));
	test_http(0, 7000);
	while (!ths.empty()) {
		ths.back().join();
		ths.pop_back();
	}
	return 0;
}
