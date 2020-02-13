// Copyright (c) 2007-2019, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <iostream>

#include <crab/crab.hpp>

namespace http = crab::http;

int main() {
	std::cout << "This is simple HTTP server" << std::endl;
	crab::RunLoop runloop;

	http::Server server("0.0.0.0", 7000);

	server.r_handler = [&](http::Client *who, http::RequestBody &&request, http::ResponseBody &response) -> bool {
		response.r.status       = 200;
		response.r.content_type = "text/plain; charset=utf-8";
		response.set_body("Hello, Crab!");
		return true;
	};

	runloop.run();
	return 0;
}
