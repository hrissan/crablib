// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#pragma once

#include <deque>
#include <random>
#include "../network.hpp"
#include "../streams.hpp"
#include "request_parser.hpp"
#include "response_parser.hpp"
#include "types.hpp"
#include "web_message_parser.hpp"

namespace crab {

class BufferedTCPSocket : public IStream, private Nocopy {
public:
	explicit BufferedTCPSocket(Handler &&r_handler, Handler &&d_handler);

	void close();  // after close you are guaranteed that no handlers will be called
	bool is_open() const { return sock.is_open(); }

	bool connect(const Address &address) { return sock.connect(address); }
	void accept(TCPAcceptor &acceptor, Address *accepted_addr = nullptr) {
		return sock.accept(acceptor, accepted_addr);
	}

	size_t read_some(uint8_t *val, size_t count) override;
	void write(const uint8_t *val, size_t count, bool buffer_only = false);
	void write(std::string &&ss, bool buffer_only = false);

	void write_shutdown();

	size_t get_total_buffer_size() const { return total_buffer_size; }

protected:
	std::deque<StringStream> data_to_write;
	size_t total_buffer_size  = 0;
	bool write_shutdown_asked = false;

	void write();
	void on_rw_handler();
	void on_disconnect();

	Handler r_handler;
	Handler d_handler;

	TCPSocket sock;
};

namespace http {
class Server;

class Connection : private Nocopy {
public:
	explicit Connection();
	explicit Connection(Handler &&r_handler, Handler &&d_handler);
	void set_handlers(Handler &&r_handler, Handler &&d_handler) {
		this->r_handler = std::move(r_handler);
		this->d_handler = std::move(d_handler);
	}

	bool connect(const Address &address);
	void accept(TCPAcceptor &acceptor);

	void close();
	bool is_open() const { return sock.is_open(); }
	const Address &get_peer_address() const { return peer_address; }

	void write(ResponseBody &&resp);
	void write(RequestBody &&resp);
	void write(WebMessage &&);
	bool read_next(RequestBody &request);
	bool read_next(ResponseBody &request);
	bool read_next(WebMessage &);
	void web_socket_upgrade();  // Will throw if not upgradable

	enum State {
		REQUEST_HEADER,          // Server side
		REQUEST_BODY,            // Server side
		REQUEST_READY,           // Server side
		WAITING_WRITE_RESPONSE,  // Server side

		WAITING_WRITE_REQUEST,  // Client side
		RESPONSE_HEADER,        // Client side
		RESPONSE_BODY,          // Client side
		RESPONSE_READY,         // Client side

		WEB_UPGRADE_RESPONSE_HEADER,  // Client side of Web Socket
		WEB_MESSAGE_HEADER,           // Both side of Web Socket
		WEB_MESSAGE_BODY,             // Both side of Web Socket
		WEB_MESSAGE_READY,            // Both side of Web Socket
		SHUTDOWN                      // Both side of Web Socket
	};

	// TODO - fix SHUTDOWN logic, add additional state for Connection: close

	State get_state() const { return state; }

protected:
	crab::Buffer read_buffer;

	RequestParser request_parser;
	ResponseParser response_parser;
	BodyParser http_body_parser;

	MessageChunkParser wm_header_parser;
	MessageBodyParser wm_body_parser;  // Single per several chunks
	std::string sec_websocket_key;
	std::mt19937 masking_key_random;
	bool client_side   = false;  // Web Socket encryption is one-sided
	bool wm_close_sent = false;

	void advance_state(bool called_from_runloop);

	void on_disconnect();

	Handler r_handler;
	Handler d_handler;

	BufferedTCPSocket sock;

	State state;
	Address peer_address;
};

}  // namespace http
}  // namespace crab
