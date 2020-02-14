// Copyright (c) 2007-2019, Grigory Buteyko aka Hrissan
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

namespace crab {

// Abstracts UDP outgoing buffer with event on buffer space available
class UDPTransmitter {
public:
	explicit UDPTransmitter(const std::string &addr, uint16_t port, Handler &&r_handler)
	    : r_handler(std::move(r_handler)) {}

	size_t write_datagram(const void *data, size_t size) { return 0; }

	~UDPTransmitter() = default;

private:
	Handler &&r_handler;
};

class UDPReceiver {
public:
	typedef std::function<void(const std::string &addr, const unsigned char *data, size_t size)> P_handler;
	UDPReceiver(const std::string &addr, uint16_t port, P_handler &&p_handler) {}
	~UDPReceiver() = default;

private:
};

}  // namespace crab

class LowLatencyReceiver {
public:
	LowLatencyReceiver(const MDSettings &settings, std::function<void(Msg msg)> &&message_handler)
	    : settings(settings)
	    , message_handler(std::move(message_handler))
	    , incoming_socket([&]() { on_incoming_socket_data(); }, [&]() { on_incoming_socket_closed(); })
	    , incoming_socket_buffer(4096)
	    , udp_a(settings.md_gate_udp_a_address, settings.md_gate_udp_a_port,
	          [&]() {})  // We just skip packets if buffer is full in UDP line A
	    , reconnect_timer([&]() { connect(); }) {
		connect();
	}

private:
	void on_incoming_socket_data() {
		incoming_socket_buffer.read_from(incoming_socket_buffer);
		size_t count = incoming_socket_buffer.size() / Msg::size;
		crab::VectorStream vs;
		incoming_socket_buffer.write_to(vs, count * Msg::size);
		while (incoming_socket_buffer.size() >= Msg::size) {
			uint8_t buffer[Msg::size];
			incoming_socket_buffer.read(buffer, Msg::size);
			if (!udp_a.write_datagram(buffer, Msg::size)) {
				std::cout << "UDP retransmission buffer full, dropping message" << std::endl;
			}
			crab::IMemoryStream ms(buffer, Msg::size);
			Msg msg;
			msg.read(&ms);
			message_handler(msg);
		}
	}
	void on_incoming_socket_closed() {
		incoming_socket_buffer.clear();
		reconnect_timer.once(1);
	}
	void connect() {
		if (!incoming_socket.connect(settings.upstream_address, settings.upstream_tcp_port))
			reconnect_timer.once(1);
	}
	const MDSettings settings;

	std::function<void(Msg msg)> message_handler;
	crab::TCPSocket incoming_socket;
	crab::Buffer incoming_socket_buffer;

	crab::UDPTransmitter udp_a;
	crab::Timer reconnect_timer;
};

class MDGate {
public:
	explicit MDGate(const MDSettings &settings)
	    : settings(settings)
	    , server("0.0.0.0", settings.md_gate_http_port)
	    , udp_ra(settings.md_gate_udp_ra_address, settings.md_gate_udp_ra_port, [&]() { broadcast_retransmission(); })
	    , ab([&]() { on_fast_queue_changed(); })
	    , th(&MDGate::generator_thread, this)
	    , http_client([&]() { on_http_client_data(); }, [&]() { on_http_client_closed(); })
	    , reconnect_timer([&]() { connect(); }) {
		connect();
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
			waiting_clients.emplace(who, req.end);
			waiting_clients_inv.emplace(req.end, std::make_pair(req, who));
			return false;
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
			messages.push_back(msg);
			return;
		}
		if (chunks.empty()) {
			auto next_seq = messages.back().seqnum + 1;
			if (msg.seqnum < next_seq)
				return;
			if (msg.seqnum == next_seq) {
				messages.push_back(msg);
				// We could close the gap to the first chunk
				if (!chunks.empty() && chunks.front().front().seqnum == msg.seqnum + 1) {
					messages.insert(messages.end(), chunks.front().begin(), chunks.front().end());
					chunks.pop_front();
				}
				return;
			}
			chunks.emplace_back();
			chunks.back().push_back(msg);
			return;
		}
		// each chunk is also not empty
		auto next_seq = chunks.back().back().seqnum + 1;
		if (msg.seqnum < next_seq)
			return;
		if (msg.seqnum > next_seq)
			chunks.emplace_back();
		chunks.back().push_back(msg);
	}
	void broadcast_retransmission() {
		if (http_client.get_state() == http::Connection::WAITING_WRITE_REQUEST && !chunks.empty()) {
			MDRequest req;
			req.begin = messages.back().seqnum + 1;
			req.end   = chunks.front().front().seqnum;
			crab::StringStream os;
			req.write(&os);

			http::RequestBody request(settings.upstream_address, "GET", "/messages");
			request.set_body(std::move(os.get_buffer()));
			http_client.write(std::move(request));
		}
	}
	void on_http_client_data() {
		http::ResponseBody response;
		while (http_client.read_next(response)) {
			if (response.r.status == 200) {
				crab::IStringStream is(&response.body);
				while (is.size() > Msg::size) {
					Msg msg;
					msg.read(&is);
					add_message_from_any_source(msg);
				}
			}
		}
		broadcast_retransmission();
	}
	void on_http_client_closed() { reconnect_timer.once(1); }
	void connect() {
		if (!http_client.connect(settings.upstream_address, settings.upstream_http_port)) {
			reconnect_timer.once(1);
		} else {
			broadcast_retransmission();
		}
	}
	void generator_thread() {
		crab::RunLoop runloop;

		LowLatencyReceiver gen(settings, [&](Msg msg) { add_message(msg); });

		runloop.run();
	}

	const MDSettings settings;

	http::Server server;
	crab::UDPTransmitter udp_ra;

	crab::Watcher ab;

	std::mutex mutex;
	std::deque<Msg> fast_queue;  // intermediate queue, it will be locked for very short time

	std::deque<std::vector<Msg>> chunks;
	std::deque<Msg> messages;  // continuous stream

	http::Connection http_client;
	crab::Timer reconnect_timer;

	std::thread th;
};

int main(int argc, char *argv[]) {
	std::cout
	    << "This gate connects to running instance of md_tcp_source, and broadcasts data via UDP, with support of retransmission requests via HTTP"
	    << std::endl;
	crab::RunLoop runloop;

	MDSettings settings;

	MDGate app(settings);

	runloop.run();
	return 0;
}
