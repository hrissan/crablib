// Copyright (c) 2007-2023, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <string.h>
#include <iostream>
#include <map>
#include <set>

#include <crab/crab.hpp>

#include "gate_message.hpp"

// This app generates "financial messages" which are defined as
// uint64_t with seq number plus uint64_t payload

// Clients connect and listen to messages. If socket buffer fills up for particular client,
// it is disconnected immediately.
// In real world, it will reconnect soon, getting missing part via HTTP request
// QOS must be setup so that traffic via TCP port has higher priority than via HTTP port

namespace http = crab::http;

enum { MAX_RESPONSE_COUNT = 10000, MICROSECONDS_PER_MESSAGE = 500000 };

// This class uses on_idle so that messages will be generated/sent with
// as little jitter as possible. It contains small TCP server, so that clients connect
// directly and get as little latency as possible. Expected to be used with very limited
// number of low-latency clients, other must be connected via retransmitters

class MDGenerator {
public:
	explicit MDGenerator(const MDSettings &settings, std::function<void(Msg msg)> &&message_handler)
	    : message_handler(std::move(message_handler))
	    , la_socket(settings.upsteam_tcp_bind(), [&]() { accept_all(); })
	    , udp_a(settings.md_gate_udp_a(), [&]() {})
	    , idle([&]() { on_idle(); }) {}

private:
	using ClientList = std::list<crab::TCPSocket>;

	void on_idle() {
		auto now      = std::chrono::steady_clock::now();
		int64_t ticks = std::chrono::duration_cast<std::chrono::microseconds>(now - last_tick).count();
		if (ticks == 0)
			return;
		last_tick = now;
		total_ticks += ticks;
		for (; total_ticks >= MICROSECONDS_PER_MESSAGE; total_ticks -= MICROSECONDS_PER_MESSAGE) {
			seqnum += 1;
			Msg msg{seqnum, rnd.pod<uint64_t>()};

			uint8_t buffer[Msg::size];
			crab::OMemoryStream os(buffer, Msg::size);
			msg.write(&os);

			udp_a.write_datagram(buffer, Msg::size);  // Ignore `buffer full` errors
			for (auto it = clients.begin(); it != clients.end();) {
				if (it->write_some(buffer, sizeof(buffer)) != sizeof(buffer)) {
					std::cout << "HTTP Client disconnected (or buffer full) #=" << clients.size() << std::endl;
					it = clients.erase(it);
				} else {
					++it;
				}
			}
			message_handler(msg);
		}
	}

	void on_client_handler(ClientList::iterator it) {
		if (!it->is_open())
			return on_client_disconnected(it);
		// If socket buffer is filled, we disconnect
		// And we do not read anything, so just empty handler
	}
	void on_client_disconnected(ClientList::iterator it) {
		clients.erase(it);
		std::cout << "Client disconnected #=" << clients.size() << std::endl;
	}
	void accept_all() {
		while (la_socket.can_accept()) {                // && clients.size() < max_incoming_connections &&
			clients.emplace_back(crab::empty_handler);  // We do not know iterator at this point
			auto it = --clients.end();
			clients.back().set_handler([this, it]() { on_client_handler(it); });
			crab::Address addr;
			clients.back().accept(la_socket, &addr);
			std::cout << "Client accepted #=" << clients.size() << " addr=" << addr.get_address() << ":" << addr.get_port() << std::endl;
		}
	}

	std::function<void(Msg msg)> message_handler;

	crab::TCPAcceptor la_socket;
	ClientList clients;
	crab::UDPTransmitter udp_a;

	crab::Idle idle;

	std::chrono::steady_clock::time_point last_tick = std::chrono::steady_clock::now();
	int64_t total_ticks                             = 0;

	crab::Random rnd;
	uint64_t seqnum = 0;
};

class MDSourceApp {
public:
	explicit MDSourceApp(const MDSettings &settings)
	    : settings(settings)
	    , server(settings.upsteam_http())
	    , ab([&]() { on_fast_queue_changed(); })
	    , th(&MDSourceApp::generator_thread, this) {
		server.r_handler = [&](http::Client *who, http::Request &&request) {
			if (request.header.path != "/messages")
				return who->write(http::Response::simple_html(404));
			MDRequest req;
			crab::IStringStream is(&request.body);
			req.read(&is);
			if (req.end <= req.begin)
				return who->write(http::Response::simple_html(400, "Invalid request range - inverted or empty!"));
			if (req.end - req.begin > MAX_RESPONSE_COUNT)
				req.end = req.begin + MAX_RESPONSE_COUNT;
			http::Response response;
			if (create_response(response, req.begin, req.end))
				return who->write(std::move(response));
			// If client requests range which is not available yet, we add them to long-poll
			// waiting_clients_inv is sorted by req.end so once sequence number reaches begin(),
			// we can send response to that waiting client
			auto res = waiting_clients_inv.emplace(req.end, std::make_pair(req, who));
			who->postpone_response([this, res]() { waiting_clients_inv.erase(res); });
		};
	}

private:
	void add_message(const Msg &msg) {  // Called from other thread
		std::unique_lock<std::mutex> lock(mutex);
		fast_queue.push_back(msg);
		ab.call();
	}
	void on_fast_queue_changed() {
		std::deque<Msg> fq;
		{
			// We lock fast_queue for as little time as possible, so that
			// latency of add_message() above is not affected
			std::unique_lock<std::mutex> lock(mutex);
			fast_queue.swap(fq);
		}
		for (; !fq.empty(); fq.pop_front()) {
			if (!messages.empty() && fq.front().seqnum != messages.back().seqnum + 1)
				throw std::logic_error("Invariant violated");
			messages.push_back(fq.front());
		}
		while (!waiting_clients_inv.empty() && waiting_clients_inv.begin()->first <= messages.back().seqnum) {
			auto who = waiting_clients_inv.begin()->second.second;
			auto req = waiting_clients_inv.begin()->second.first;
			waiting_clients_inv.erase(waiting_clients_inv.begin());

			http::Response response;
			create_response(response, req.begin, req.end);
			who->write(std::move(response));
		}
	}
	bool create_response(http::Response &response, uint64_t begin, uint64_t end) {
		if (messages.empty() || begin < messages.front().seqnum) {
			response.header.status = 400;
			response.header.set_content_type("text/plain", "charset=utf-8");
			response.set_body("Invalid request range - before start!");
			return true;
		}
		if (end > messages.back().seqnum + 1) {
			return false;  // Those requests will be added to long poll
		}
		const auto messages_start = messages.front().seqnum;

		crab::StringStream body;
		body.get_buffer().reserve(static_cast<size_t>(end - begin) * Msg::size);
		for (auto s = begin; s != end; ++s) {
			messages.at(static_cast<size_t>(s - messages_start)).write(&body);
		}
		response.header.status = 200;
		response.header.set_content_type("text/plain", "charset=utf-8");
		response.set_body(std::move(body.get_buffer()));
		return true;
	}
	void generator_thread() {
		// Separate thread for MDGenerator. Any variables in this thread are inaccessible from outside
		// while it communicates with MDSourceApp via single point - add_message()
		crab::RunLoop runloop;

		MDGenerator gen(settings, [&](Msg msg) { add_message(msg); });

		runloop.run();
	}

	const MDSettings settings;

	std::deque<Msg> messages;  // In real system, messages will be stored in some DB

	http::Server server;
	std::multimap<uint64_t, std::pair<MDRequest, http::Client *>> waiting_clients_inv;

	crab::Watcher ab;            // Signals about changes in fast_queue
	std::mutex mutex;            // Protects fast_queue
	std::deque<Msg> fast_queue;  // intermediate queue, it will be locked for very short time

	std::thread th;
};

int main(int argc, char *argv[]) {
	std::cout << "crablib version " << crab::version_string() << std::endl;

	std::cout
	    << "This server generates stream of 'financial messages' and makes it available via TCP (transmissions, low latency) and HTTP (retransmissions)"
	    << std::endl;
	crab::RunLoop runloop;

	MDSettings settings;

	MDSourceApp app(settings);

	runloop.run();
	return 0;
}
