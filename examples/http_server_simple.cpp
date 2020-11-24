// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <iostream>

#include <crab/crab.hpp>

namespace http = crab::http;

class NaiveParser {
	enum State {
		HEADER_TEXT,
		HEADER_START_LF,
		HEADER_START,
		SECOND_LF,
		GOOD
	} state = HEADER_TEXT;

public:
	size_t max_total_length = 8192;


	template<typename InputIterator>
	InputIterator parse(InputIterator begin, InputIterator end) {
		while (begin != end && state != GOOD)
			state = consume(*begin++);
		return begin;
	}

	bool is_good() const { return state == GOOD; }
	void parse(crab::Buffer &buf) {
		auto ptr = parse(buf.read_ptr(), buf.read_ptr() + buf.read_count());
		buf.did_read(ptr - buf.read_ptr());
	}
	State consume(char input) {
		if (++total_length > max_total_length)
			throw std::runtime_error("HTTP Header too long - security violation");

		switch (state) {
		case HEADER_TEXT:
			// Skip empty lines https://tools.ietf.org/html/rfc2616#section-4.1
			if (input == '\r')
				return HEADER_START_LF;
			if (input == '\n')
				return HEADER_START;
			return HEADER_TEXT;
		case HEADER_START_LF:
			if (input != '\n')
				throw std::runtime_error("Invalid LF at method start");
			return HEADER_START;
		case HEADER_START:
			if (input == '\r')
				return SECOND_LF;
			if (input == '\n')
				return GOOD;
			return HEADER_TEXT;
		// TODO - collapse ./ and ../
		case SECOND_LF:
			if (input != '\n')
				throw std::runtime_error("Invalid LF at method start");
			return GOOD;

		default:
			throw std::logic_error("Invalid request parser state");
		}
	}
	size_t total_length    = 0;
};

class FastServerApp {
public:
	explicit FastServerApp(const crab::Address &bind_address)
	    : la_socket(bind_address, [&]() { accept_all(); }, get_settings())
	    , stat_timer([&]() { print_stats(); }) {
		print_stats();
		http::Response response;
		response.header.status = 200;
		response.header.set_content_type("text/plain", "charset=utf-8");
		response.header.server = "crab";
		response.header.date = http::Server::get_date();
		response.set_body("Hello, Crab!");
		response_text = response.header.to_string() + response.body;
		std::cout << "response_text: " << response_text << std::endl;
	}

private:
	crab::TCPAcceptor::Settings get_settings() {
		crab::TCPAcceptor::Settings result;
		result.reuse_addr = true;
		result.tcp_delay = true;
		return result;
	}
	crab::TCPAcceptor la_socket;
	std::string response_text;

	struct Client {
		size_t client_id = 0;
		crab::TCPSocket socket;
		crab::Buffer incoming_buffer;
		crab::Buffer outgoing_buffer;
		
		NaiveParser parser;

		Client() : socket(crab::empty_handler), incoming_buffer(4096), outgoing_buffer(4096) {}
	};
	using ClientList = std::list<Client>;
	ClientList clients;

	crab::Timer stat_timer;
	size_t requests_processed = 0;
	size_t clients_accepted   = 0;

	void on_client_handler(ClientList::iterator it) {
		if (!it->socket.is_open())
			return on_client_disconnected(it);
		Client & client = *it;
		while(true) {
			if (!client.outgoing_buffer.empty()) {
				client.outgoing_buffer.write_to(client.socket);
				if (!client.outgoing_buffer.empty())
					return;
			}
			// Here outgoing_buffer is empty
			if (client.parser.is_good()) {
				crab::IStringStream is(&response_text);
				if (client.outgoing_buffer.read_from(is) != response_text.size())
					throw std::logic_error("response_text did not fit into buffer");
				client.parser = NaiveParser{};
				continue;
			}
			client.incoming_buffer.read_from(client.socket);
			if (client.incoming_buffer.size() == 0)
				return;
			client.parser.parse(client.incoming_buffer);
			client.parser.parse(client.incoming_buffer); // TODO - remove
		}
	}
	void on_client_disconnected(ClientList::iterator it) {
		clients.erase(it);  // automatically unlinks from fair_queue
		                    //		std::cout << "Fair Client " << clients.back().client_id
		//		          << " disconnected, current number of clients is=" << clients.size() << std::endl;
	}
	void accept_all() {
		while (accept_single())
			;
	}
	bool accept_single() {
		if (!la_socket.can_accept())  // || clients.size() >= max_incoming_connections &&
			return false;
		clients.emplace_back();
		auto it = --clients.end();
		clients_accepted += 1;
		clients.back().client_id = clients_accepted;
		clients.back().socket.set_handler([this, it]() { on_client_handler(it); });
		crab::Address addr;
		clients.back().socket.accept(la_socket, &addr);
		return true;
	}
	void print_stats() {
		stat_timer.once(1);
//		std::cout << "requests processed (during last second)=" << requests_processed << std::endl;
//		if (!clients.empty()) {
//			std::cout << "Client.front read=" << clients.front().total_read
//			          << " written=" << clients.front().total_written << std::endl;
//			//            if (requests_processed == 0 && clients.front().total_written > 2000) {
//			//                uint8_t buf[100]{};
//			//                clients.front().socket.write(buf, sizeof(buf));
//			//                std::cout << "Written 100 bytes" << std::endl;
//			//            }
//		}
		requests_processed = 0;
	}
};

/*
int main(int argc, const char *argv[]) {
	std::cout << "crablib version " << crab::version_string() << std::endl;

	std::cout << "This is probably fastest possible HTTP server using crablib" << std::endl;
	if (argc < 2) {
		std::cout << "Usage: simple_http_server <port>" << std::endl;
		return 0;
	}
	crab::RunLoop runloop;

	FastServerApp app(crab::Address("0.0.0.0", crab::integer_cast<uint16_t>(argv[1])));

	runloop.run();
	return 0;
}
*/

int main(int argc, const char *argv[]) {
	std::cout << "crablib version " << crab::version_string() << std::endl;

	std::cout << "This is simple HTTP server" << std::endl;
	if (argc < 2) {
		std::cout << "Usage: simple_http_server <port>" << std::endl;
		return 0;
	}

	crab::RunLoop runloop;

	http::Server server(crab::Address("0.0.0.0", crab::integer_cast<uint16_t>(argv[1])));

	server.r_handler = [&](http::Client *who, http::Request &&request) {
		bool cond = false;
		//		std::cout << "Request" << std::endl;
		for (const auto &q : request.parse_query_params()) {
			//			std::cout << "    '" << q.first << "' => '" << q.second << "'" << std::endl;
			if (q.first == crab::string_view{"query"})
				cond = true;
		}
		//		std::cout << "Cookies" << std::endl;
		//		for (const auto &q : request.parse_cookies())
		//			std::cout << "    '" << q.first << "' => '" << q.second << "'" << std::endl;
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

