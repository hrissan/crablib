// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
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

enum { MAX_RESPONSE_COUNT = 10000, MICROSECONDS_PER_MESSAGE = 50 };

// This class uses on_idle so that messages will be generated/sent with
// as little jitter as possible. It contains small TCP server, so that clients connect
// directly and get as little latency as possible. Expected to be used with very limited
// number of low-latency clients, other must be connected via retransmitters

class MDGenerator {
public:
	explicit MDGenerator(uint16_t port, std::function<void(Msg msg)> &&message_handler)
	    : message_handler(std::move(message_handler))
	    , la_socket("0.0.0.0", port, [&]() { accept_all(); })
	    , idle([&]() { on_idle(); }) {}

	void on_idle() {
		auto now     = std::chrono::steady_clock::now();
		size_t ticks = std::chrono::duration_cast<std::chrono::microseconds>(now - last_tick).count();
		if (ticks == 0)
			return;
		last_tick = now;
		total_ticks += ticks;
		for (; total_ticks >= MICROSECONDS_PER_MESSAGE; total_ticks -= MICROSECONDS_PER_MESSAGE) {
			seqnum += 1;
			Msg msg{seqnum, (uint64_t(mt()) << 32U) + mt()};

			uint8_t buffer[Msg::size];
			crab::OMemoryStream os(buffer, Msg::size);
			msg.write(&os);

			for (auto it = clients.begin(); it != clients.end();) {
				if ((*it)->write_some(buffer, sizeof(buffer)) != sizeof(buffer)) {
					std::cout << "HTTP Client disconnected (or buffer full) #=" << clients.size() << std::endl;
					it = clients.erase(it);
				} else {
					++it;
				}
			}
			message_handler(msg);
		}
	}

private:
	std::function<void(Msg msg)> message_handler;

	crab::TCPAcceptor la_socket;
	using ClientList = std::list<std::unique_ptr<crab::TCPSocket>>;
	ClientList clients;

	crab::Idle idle;

	std::chrono::steady_clock::time_point last_tick = std::chrono::steady_clock::now();
	uint64_t total_ticks                            = 0;

	std::mt19937 mt;
	uint64_t seqnum = 0;

	void on_client_handler(ClientList::iterator it) {
		// If socket buffer is filled, we disconnect
		// And we do not read anything, so just empty handler
	}
	void on_client_disconnected(ClientList::iterator it) {
		clients.erase(it);
		std::cout << "HTTP Client disconnected #=" << clients.size() << std::endl;
	}
	void accept_all() {
		while (la_socket.can_accept()) {  // && clients.size() < max_incoming_connections &&
			clients.emplace_back();
			auto it = --clients.end();
			clients.back().reset(new crab::TCPSocket(
			    [this, it]() { on_client_handler(it); }, [this, it]() { on_client_disconnected(it); }));
			std::string addr;
			clients.back()->accept(la_socket, &addr);
			std::cout << "HTTP Client accepted #=" << clients.size() << " addr=" << addr << std::endl;
		}
	}
};

class MDSourceApp {
public:
	explicit MDSourceApp(const MDSettings &settings)
	    : settings(settings)
	    , server("0.0.0.0", settings.upstream_http_port)
	    , ab([&]() { on_fast_queue_changed(); })
	    , th(&MDSourceApp::generator_thread, this) {
		server.r_handler = [&](http::Client *who, http::RequestBody &&request, http::ResponseBody &response) -> bool {
			if (request.r.uri != "/messages")
				return true;  // Default "not found"
			MDRequest req;
			crab::IStringStream is(&request.body);
			req.read(&is);
			if (req.end <= req.begin) {
				response.r.status       = 400;
				response.r.content_type = "text/plain; charset=utf-8";
				response.set_body("Invalid request range - inverted or empty!");
				return true;
			}
			if (req.end - req.begin > MAX_RESPONSE_COUNT)
				req.end = req.begin + MAX_RESPONSE_COUNT;
			if (create_response(response, req.begin, req.end))
				return true;
			// If client requests range which is not available yet, we add them to long-poll
			// waiting_clients_inv is sorted by req.end so once sequence number reaches begin(),
			// we can send response to that waiting client
			waiting_clients.emplace(who, req.end);
			waiting_clients_inv.emplace(req.end, std::make_pair(req, who));
			return false;
		};
		server.d_handler = [&](http::Client *who) {
			auto wit = waiting_clients.find(who);
			if (wit == waiting_clients.end())
				return;
			waiting_clients_inv.erase(wit->second);
			waiting_clients.erase(wit);
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
			waiting_clients.erase(who);

			http::ResponseBody response;
			create_response(response, req.begin, req.end);
			who->write(std::move(response));
		}
	}
	bool create_response(http::ResponseBody &response, uint64_t begin, uint64_t end) {
		if (messages.empty() || begin < messages.front().seqnum) {
			response.r.status       = 400;
			response.r.content_type = "text/plain; charset=utf-8";
			response.set_body("Invalid request range - before start!");
			return true;
		}
		if (end > messages.back().seqnum + 1) {
			return false;  // Those requests will be added to long poll
		}
		const auto messages_start = messages.front().seqnum;

		crab::StringStream body;
		body.get_buffer().reserve((end - begin) * Msg::size);
		for (auto s = begin; s != end; ++s) {
			messages.at(s - messages_start).write(&body);
		}
		response.r.status       = 200;
		response.r.content_type = "text/plain; charset=utf-8";
		response.set_body(std::move(body.get_buffer()));
		return true;
	}
	void generator_thread() {
		// Separate thread for MDGenerator. Any variables in this thread are inaccessible from outside
		// while it communicates with MDSourceApp via single point - add_message()
		crab::RunLoop runloop;

		MDGenerator gen(settings.upstream_tcp_port, [&](Msg msg) { add_message(msg); });

		runloop.run();
	}

	const MDSettings settings;

	std::deque<Msg> messages;  // In real system, messages will be stored in some DB

	http::Server server;
	std::map<http::Client *, uint64_t> waiting_clients;
	std::map<uint64_t, std::pair<MDRequest, http::Client *>> waiting_clients_inv;

	crab::Watcher ab;            // Signals about changes in fast_queue
	std::mutex mutex;            // Protects fast_queue
	std::deque<Msg> fast_queue;  // intermediate queue, it will be locked for very short time

	std::thread th;
};

int main(int argc, char *argv[]) {
	std::cout
	    << "This server generates stream of 'financial messages' and makes it available via TCP (transmissions, low latency) and HTTP (retransmissions)"
	    << std::endl;
	crab::RunLoop runloop;

	MDSettings settings;

	MDSourceApp app(settings);

	runloop.run();
	return 0;
}
