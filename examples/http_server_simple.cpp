// Copyright (c) 2007-2023, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <iostream>

#include <crab/crab.hpp>

namespace http = crab::http;

int main() {
	std::cout << "crablib version " << crab::version_string() << std::endl;

	std::cout << "This is simple HTTP server on port 7000" << std::endl;

	crab::RunLoop runloop;

	http::Server server(7000);

	server.r_handler = [&](http::Client *who, http::Request &&request) {
		bool cond = false;
		std::cout << "Request" << std::endl;
		for (const auto &q : request.parse_query_params()) {
			std::cout << "    '" << q.first << "' => '" << q.second << "'" << std::endl;
			if (q.first == crab::string_view{"query"})
				cond = true;
		}
		std::cout << "Cookies" << std::endl;
		for (const auto &q : request.parse_cookies())
			std::cout << "    '" << q.first << "' => '" << q.second << "'" << std::endl;
		http::Response response;
		response.header.status = 200;
		response.header.set_content_type("text/plain", "charset=utf-8");
		response.set_body(cond ? "Hello, Cond!" : "Hello, Crab!");
		who->write(std::move(response));

		// Or for even simpler code paths, like error messages
		// who->write(http::Response::simple_text(200, "Hello, Crab!"));
	};

	runloop.run();
	return 0;
}
