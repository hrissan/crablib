// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <iostream>
#include <set>

#include <crab/crab.hpp>

#include "gate_message.hpp"

// This app connects to md_tcp_source and listens to "financial messages".
// If it is disconnected, it reconnects, then requests retransmission of skipped messages

// Stream of messages is broadcast via UDP group (A) with low latency

// Also skipped messages can be requested for retransmission via HTTP
// Retransmitted messages are broadcast in different UDP group (rA) in fair way
// So that each connected client gets proportional % of available channel bandwidth

// QOS must be setup so that traffic via UDP group (A) has higher priority than UDP group (rA)
// Also, rate of incoming IP packets per second must be limited for HTTP port

namespace http = crab::http;

enum { MAX_DATAGRAM_SIZE = 508 };

// Connects to TCP, reads messages from upstream_socket, immediately retransmits them to udp_a
// and sends to message_handler
class LowLatencyRetransmitter {
public:
	LowLatencyRetransmitter(const MDSettings &settings, std::function<void(Msg msg)> &&message_handler)
	    : settings(settings)
	    , upstream_socket([&]() { upstream_socket_handler(); })
	    , upstream_socket_buffer(4096)
	    , message_handler(std::move(message_handler))
	    , udp_a(settings.md_gate_udp_a(), [&]() {})  // We just skip packets if buffer is full in UDP line A
	    , reconnect_timer([&]() { connect(); })
	    , simulated_disconnect_timer([&]() { on_simulated_disconnect_timer(); }) {
		connect();
		simulated_disconnect_timer.once(1);
	}

private:
	void simulated_disconnect() {
		std::cout << "Simulated disconnected" << std::endl;
		upstream_socket.close();
		upstream_socket_buffer.clear();
		reconnect_timer.once(2);
	}
	void on_simulated_disconnect_timer() {
		simulated_disconnect_timer.once(1);
		if (reconnect_timer.is_set())
			return;  // Already disconnected
		if (rand() % 10 == 0)
			simulated_disconnect();
	}
	void upstream_socket_handler() {
		if (!upstream_socket.is_open())
			return on_upstream_socket_closed();
		while (true) {
			if (upstream_socket_buffer.size() < Msg::size)
				upstream_socket_buffer.read_from(upstream_socket);
			const size_t max_count = MAX_DATAGRAM_SIZE / Msg::size;
			size_t count           = std::min(max_count, upstream_socket_buffer.size() / Msg::size);
			if (count == 0)
				break;
			crab::VectorStream vs;
			upstream_socket_buffer.write_to(vs, count * Msg::size);
			if (!udp_a.write_datagram(vs.get_buffer().data(), vs.get_buffer().size())) {
				std::cout << "UDP retransmission buffer full, dropping message" << std::endl;
			}
			while (vs.size() >= Msg::size) {
				Msg msg;
				msg.read(&vs);
				message_handler(msg);
			}
		}
	}
	void on_upstream_socket_closed() {
		upstream_socket_buffer.clear();
		reconnect_timer.once(1);
		std::cout << "Upstream socket disconnected" << std::endl;
	}
	void connect() {
		if (!upstream_socket.connect(settings.upsteam_tcp())) {
			reconnect_timer.once(1);
		} else {
			std::cout << "Upstream socket connection attempt started..." << std::endl;
		}
	}
	const MDSettings settings;

	crab::TCPSocket upstream_socket;
	crab::Buffer upstream_socket_buffer;

	std::function<void(Msg msg)> message_handler;

	crab::UDPTransmitter udp_a;
	crab::Timer reconnect_timer;
	crab::Timer simulated_disconnect_timer;
};

class MDGate {
public:
	explicit MDGate(const MDSettings &settings)
	    : settings(settings)
	    , server(settings.md_gate_http())
	    , udp_ra(settings.md_gate_udp_ra(), [&]() { broadcast_retransmission(); })
	    , stat_timer([&]() { on_stat_timer(); })
	    , ab([&]() { on_fast_queue_changed(); })
	    , http_client([&]() { on_http_client_data(); })
	    , reconnect_timer([&]() { connect(); })
	    , th(&MDGate::retransmitter_thread, this) {
		connect();
		stat_timer.once(1);
		server.r_handler = [&](http::Client *who, http::Request &&request) {
			if (request.header.path != "/messages")
				return who->write(http::Response::simple_html(404));
			MDRequest req;
			crab::IStringStream is(&request.body);
			req.read(&is);
			if (req.end <= req.begin)
				return who->write(http::Response::simple_html(400, "Invalid request range - inverted or empty!"));
			// TODO - add to data structure here
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
			add_message_from_any_source(fq.front());
		}
		broadcast_retransmission();
	}
	void add_message_from_any_source(const Msg &msg) {
		if (messages.empty()) {
			std::cout << "First! " << msg.seqnum << std::endl;
			messages.push_back(msg);
			return;
		}
		auto next_seq = messages.back().seqnum + 1;
		if (msg.seqnum < next_seq)
			return;
		if (msg.seqnum == next_seq) {
			//			std::cout << "Normal " << msg.seqnum << std::endl;
			messages.push_back(msg);
			// We could close the gap to the first chunk
			if (!chunks.empty() && chunks.front().front().seqnum == msg.seqnum + 1) {
				std::cout << "Closing gap  .." << chunks.front().back().seqnum << "]" << std::endl;
				messages.insert(messages.end(), chunks.front().begin(), chunks.front().end());
				chunks.pop_front();
			}
			return;
		}
		if (chunks.empty()) {
			//			std::cout << "Created first chunk [" << msg.seqnum << ".." << std::endl;
			chunks.emplace_back();
			chunks.back().push_back(msg);
			return;
		}
		// each chunk is also not empty
		next_seq = chunks.back().back().seqnum + 1;
		if (msg.seqnum < next_seq)
			return;
		if (msg.seqnum > next_seq) {
			//			std::cout << "Created chunk [" << msg.seqnum << ".." << std::endl;
			chunks.emplace_back();
		} else {
			//			std::cout << "Added to last chunk " << msg.seqnum << std::endl;
		}
		chunks.back().push_back(msg);
	}
	void on_stat_timer() {
		if (!messages.empty())
			std::cout << "[" << messages.front().seqnum << ".." << messages.back().seqnum << "]";
		for (auto &c : chunks)
			std::cout << " <--> [" << c.front().seqnum << ".." << c.back().seqnum << "]";
		std::cout << std::endl;
		stat_timer.once(1);
	}
	void broadcast_retransmission() {
		if (http_client.get_state() == http::ClientConnection::WAITING_WRITE_REQUEST && !chunks.empty()) {
			MDRequest req;
			req.begin = messages.back().seqnum + 1;
			req.end   = chunks.front().front().seqnum;
			std::cout << "Sending request for [" << req.begin << ".." << req.end << ")" << std::endl;
			crab::StringStream os;
			req.write(&os);

			http::Request request(settings.upstream_address, "GET", "/messages");
			request.set_body(std::move(os.get_buffer()));
			http_client.write(std::move(request));
		}
	}
	void on_http_client_data() {
		if (!http_client.is_open())
			return on_http_client_closed();
		http::Response response;
		while (http_client.read_next(response)) {
			if (response.header.status == 200) {
				crab::IStringStream is(&response.body);
				while (is.size() >= Msg::size) {
					Msg msg;
					msg.read(&is);
					add_message_from_any_source(msg);
				}
			}
		}
		broadcast_retransmission();
	}
	void on_http_client_closed() {
		std::cout << "Incoming http connect closed" << std::endl;
		reconnect_timer.once(1);
	}
	void connect() {
		if (!http_client.connect(settings.upsteam_http())) {
			reconnect_timer.once(1);
		} else {
			std::cout << "Incoming http connect started" << std::endl;
			broadcast_retransmission();
		}
	}
	void retransmitter_thread() {
		// Separate thread for Retransmitter. Any variables in this thread are inaccessible from outside
		// while it communicates with MDGate via single point - add_message()
		crab::RunLoop runloop;

		LowLatencyRetransmitter gen(settings, [&](Msg msg) { add_message(msg); });

		runloop.run();
	}

	const MDSettings settings;

	http::Server server;          // requests for retransmits are received here
	crab::UDPTransmitter udp_ra;  // and broadcasted in fair manner via this UDP multicast group

	crab::Timer stat_timer;

	crab::Watcher ab;            // Signals about changes in fast_queue
	std::mutex mutex;            // Protects fast_queue
	std::deque<Msg> fast_queue;  // intermediate queue, it will be locked for very short time

	std::deque<Msg> messages;             // continuous stream, with optional non-empty gap to chunks
	std::deque<std::vector<Msg>> chunks;  // non-overlapping chunks with non-empty gaps between them

	http::ClientConnection http_client;  // We keep connection connected all the time
	crab::Timer reconnect_timer;

	std::thread th;
};

int main(int argc, char *argv[]) {
	std::cout << "crablib version " << crab::version_string() << std::endl;

	std::cout
	    << "This gate connects to running instance of md_tcp_source, and broadcasts data via UDP, with support of retransmission requests via HTTP"
	    << std::endl;
	crab::RunLoop runloop;

	MDSettings settings;

	MDGate app(settings);

	runloop.run();
	return 0;
}
