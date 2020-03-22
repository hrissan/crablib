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

enum BufferOptions { WRITE, BUFFER_ONLY };

class BufferedTCPSocket : public IStream, private Nocopy {
public:
	explicit BufferedTCPSocket(Handler &&rwd_handler);
	void set_handler(Handler &&rwd_handler) { this->rwd_handler = std::move(rwd_handler); }

	void close();  // after close you are guaranteed that no handlers will be called
	bool is_open() const { return sock.is_open(); }

	bool connect(const Address &address) { return sock.connect(address); }
	void accept(TCPAcceptor &acceptor, Address *accepted_addr = nullptr) {
		return sock.accept(acceptor, accepted_addr);
	}

	size_t read_some(uint8_t *val, size_t count) override;
	using IStream::read_some;  // Version for other char types

	// Efficient direct-to-socket interface
	size_t write_some(const uint8_t *val, size_t count);
	size_t write_some(const char *val, size_t count) {
		return write_some(reinterpret_cast<const uint8_t *>(val), count);
	}
#if __cplusplus >= 201703L
	size_t write_some(const std::byte *val, size_t count) {
		return write_some(reinterpret_cast<const uint8_t *>(val), count);
	}
#endif
	bool can_write() const { return total_buffer_size == 0 && sock.can_write(); }

	// Write into socket, all data that did not fit are store in a buffer and sent later
	void write(const uint8_t *val, size_t count, BufferOptions buffer_options = WRITE);
	void write(const char *val, size_t count, BufferOptions buffer_options = WRITE);
#if __cplusplus >= 201703L
	void write(const std::byte *val, size_t count, BufferOptions buffer_options = WRITE);
#endif

	void write(std::string &&ss, BufferOptions buffer_options = WRITE);

	void write_shutdown();

	size_t get_total_buffer_size() const { return total_buffer_size; }

protected:
	std::deque<StringStream> data_to_write;
	size_t total_buffer_size  = 0;
	bool write_shutdown_asked = false;

	void write();
	void sock_handler();
	void buffer(const uint8_t *val, size_t count);
	void buffer(const char *val, size_t count);
	void buffer(std::string &&ss);

	Handler rwd_handler;

	TCPSocket sock;
};

namespace http {
class Server;
class Client;

class Connection : private Nocopy {
public:
	using StreamHandler = std::function<void(uint64_t body_position, uint64_t body_length)>;

	explicit Connection() : Connection(empty_handler, empty_handler) {}
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

	void write(Response &&resp);
	void write(Request &&resp);
	void write(WebMessage &&);
	bool read_next(Request &request);
	bool read_next(Response &request);
	bool read_next(WebMessage &);
	void web_socket_upgrade();  // Will throw if not upgradable

	void write(ResponseHeader &&resp, BufferOptions buffer_options = WRITE);  // Write header now, body later
	void write(const uint8_t *val, size_t count, BufferOptions buffer_options = WRITE);  // Write body chunk
	void write(const char *val, size_t count, BufferOptions buffer_options = WRITE);     // Write body chunk
#if __cplusplus >= 201703L
	void write(const std::byte *val, size_t count, BufferOptions buffer_options = WRITE);
#endif
	void write(std::string &&ss, BufferOptions buffer_options = WRITE);  // Write body chunk
	void write_last_chunk();                                             // for chunk encoding, finishes body

	// Experimental, efficient direct-to-socket unbuffered interface
	void write(ResponseHeader &&resp, StreamHandler &&w_handler);  // Call when can_write in socket buffer
	size_t write_some(const uint8_t *val, size_t count);           // Write into socket buffer
	size_t write_some(const char *val, size_t count) {
		return write_some(reinterpret_cast<const uint8_t *>(val), count);
	}
#if __cplusplus >= 201703L
	size_t write_some(const std::byte *val, size_t count) {
		return write_some(reinterpret_cast<const uint8_t *>(val), count);
	}
#endif

	enum State {
		REQUEST_HEADER,                 // Server side
		REQUEST_BODY,                   // Server side
		REQUEST_READY,                  // Server side
		WAITING_WRITE_RESPONSE_HEADER,  // Server side
		WAITING_WRITE_RESPONSE_BODY,    // Server side

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

	uint64_t body_content_length = 0;  // for WAITING_WRITE_RESPONSE_BODY state, -1 for chunked
	uint64_t body_position       = 0;  // for WAITING_WRITE_RESPONSE_BODY state
	StreamHandler w_handler;           // for WAITING_WRITE_RESPONSE_BODY state, -1 for chunked

	void sock_handler();
	void advance_state(bool called_from_runloop);

	Handler r_handler;
	Handler d_handler;

	BufferedTCPSocket sock;

	State state;
	Address peer_address;
};

}  // namespace http
}  // namespace crab
