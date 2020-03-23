// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <iostream>

#include <crab/crab.hpp>

namespace http = crab::http;

int main() {
	std::cout << "This is simple HTTP server on port 7000" << std::endl;
	crab::RunLoop runloop;

	http::Server server(7000);

	server.r_handler = [&](http::Client *who, http::Request &&request) {
		who->write(http::Response::simple_text(200, "Hello, Crab!"));
	};

	runloop.run();
	return 0;
}
