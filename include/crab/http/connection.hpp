// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#pragma once

#include <deque>
#include "../network.hpp"
#include "../streams.hpp"
#include "crab_tls.hpp"
#include "request_parser.hpp"
#include "response_parser.hpp"
#include "types.hpp"
#include "web_message_parser.hpp"

namespace crab {

enum BufferOptions { WRITE, BUFFER_ONLY };

class BufferedTCPSocket : public IStream, private Nocopy {
public:
	explicit BufferedTCPSocket(Handler &&rwd_handler);
	void set_handler(Handler &&cb) { rwd_handler = std::move(cb); }

	void close();  // after close you are guaranteed that no handlers will be called
	bool is_open() const { return sock.is_open(); }

	bool connect(const Address &address) { return sock.connect(address); }
	bool connect_tls(const Address &address, const std::string &host) { return sock.connect_tls(address, host); }
	void accept(TCPAcceptor &acceptor, Address *accepted_addr = nullptr) { sock.accept(acceptor, accepted_addr); }

	size_t read_some(uint8_t *val, size_t count) override;
	using IStream::read_some;  // Version for other char types

	// Efficient direct-to-socket interface
	size_t write_some(const uint8_t *val, size_t count);
	size_t write_some(const char *val, size_t count) { return write_some(uint8_cast(val), count); }
#if __cplusplus >= 201703L
	size_t write_some(const std::byte *val, size_t count) { return write_some(uint8_cast(val), count); }
#endif
	bool can_write() const { return sock.can_write(); }
	// does not depend on get_total_buffer_size(), so that clients can use simple can_write() to push data to socket
	// even if data was buffered by buffer() calls

	// Write into socket, all data that did not fit are store in a buffer and sent later
	void write(const uint8_t *val, size_t count, BufferOptions bo = WRITE);
	void buffer(const uint8_t *val, size_t count);
	void write(const char *val, size_t count, BufferOptions bo = WRITE) { write(uint8_cast(val), count, bo); }
	void buffer(const char *val, size_t count) { buffer(uint8_cast(val), count); }
#if __cplusplus >= 201703L
	void write(const std::byte *val, size_t count, BufferOptions bo = WRITE) { write(uint8_cast(val), count, bo); }
	void buffer(const std::byte *val, size_t count) { buffer(uint8_cast(val), count); }
#endif

	void write(std::string &&ss, BufferOptions bo = WRITE);
	void buffer(std::string &&ss);

	void write_shutdown();

	size_t get_total_buffer_size() const { return total_data_to_write; }

	enum { WM_SHUTDOWN_TIMEOUT_SEC = 15 };

protected:
	std::deque<StringStream> data_to_write;
	size_t total_data_to_write = 0;
	bool write_shutdown_asked  = false;

	void write();
	void sock_handler();
	void shutdown_timer_handler();

	Handler rwd_handler;

	TCPSocketTLS sock;
	Timer shutdown_timer;
};

namespace http {
class Server;
class Client;

class ClientConnection : private Nocopy {
public:
	explicit ClientConnection() : ClientConnection(empty_handler) {}
	explicit ClientConnection(Handler &&rwd_handler);
	void set_handler(Handler &&cb) { rwd_handler = std::move(cb); }

	bool connect(const Address &address);                                                        // http-only
	bool connect(const std::string &host, uint16_t port, const std::string &protocol = "http");  // or https

	void close();
	bool is_open() const { return dns.is_open() || sock.is_open(); }
	const std::string &get_protocol() const { return protocol; }
	const std::string &get_host() const { return host; }
	uint16_t get_port() const { return port; }
	const Address &get_peer_address() const { return peer_address; }

	void write(Request &&resp);
	void write(WebMessage &&);
	void web_socket_upgrade(const RequestHeader &rh);  // rh must contain at least path, optionally auth info, etc.
	bool read_next(Response &request);
	bool read_next(WebMessage &);

	enum State {
		RESOLVING_HOST,
		WAITING_WRITE_REQUEST,  // Client side
		RESPONSE_HEADER,        // Client side
		RESPONSE_BODY,          // Client side
		RESPONSE_READY,         // Client side

		WEB_UPGRADE_RESPONSE_HEADER,  // Client side of Web Socket
		WEB_MESSAGE_HEADER,           // Both side of Web Socket
		WEB_MESSAGE_BODY,             // Both side of Web Socket
		WEB_MESSAGE_READY,            // Both side of Web Socket
	};

	State get_state() const { return state; }

	size_t get_total_buffer_size() const { return sock.get_total_buffer_size(); }

protected:
	Buffer read_buffer;

	ResponseParser response_parser;
	BodyParser http_body_parser;

	WebMessageHeaderParser wm_header_parser;    // Chunk header
	WebMessageBodyParser wm_body_parser;        // Chunk body
	details::optional<WebMessage> web_message;  // Built from chunks
	std::string sec_websocket_key;
	Random rnd;  // for masking_key, dns name selection, web socket secret key

	void dns_handler(const std::vector<Address> &names);
	void sock_handler();
	bool advance_state();

	Handler rwd_handler;

	DNSResolver dns;
	BufferedTCPSocket sock;
	details::optional<Request> waiting_request;  // We allow sending single request in RESOLVING_HOST state

	State state;
	std::string protocol, host;
	uint16_t port = 0;
	Address peer_address;
};

class ServerConnection : private Nocopy {
public:
	explicit ServerConnection() : ServerConnection(empty_handler) {}
	explicit ServerConnection(Handler &&rwd_handler);
	void set_handler(Handler &&cb) { rwd_handler = std::move(cb); }

	void accept(TCPAcceptor &acceptor);

	void close();
	bool is_open() const { return sock.is_open(); }
	const Address &get_peer_address() const { return peer_address; }

	bool read_next(Request &req);
	bool read_next(WebMessage &);

	void write(Response &&resp);
	void write(WebMessage &&, BufferOptions bo = WRITE);  // Any opcode, except Pong
	void web_socket_upgrade();                            // Will throw if not upgradable

	// Streaming protocol, first write response/web message header, then stream data
	void write(ResponseHeader &&resp, BufferOptions bo = WRITE);  // Write header now, body later
	void write(WebMessageOpcode opcode);                          // Buffer header now, body later

	void write(const uint8_t *val, size_t count, BufferOptions bo = WRITE);  // Write body chunk
	void write(const char *val, size_t count, BufferOptions bo = WRITE) { write(uint8_cast(val), count, bo); }
#if __cplusplus >= 201703L
	void write(const std::byte *val, size_t count, BufferOptions bo = WRITE) { write(uint8_cast(val), count, bo); }
#endif
	void write(std::string &&ss, BufferOptions bo = WRITE);  // Write body chunk
	void write_last_chunk(BufferOptions bo = WRITE);  // for chunk encoding and multiframe web messages, finishes body

	enum State {
		REQUEST_HEADER,   // Reading header
		REQUEST_BODY,     // Reading body
		REQUEST_READY,    // Waiting read_next() call
		RESPONSE_HEADER,  // Waiting write of header
		RESPONSE_BODY,    // Waiting write of all body chunks

		WEB_MESSAGE_HEADER,  // Reading header
		WEB_MESSAGE_BODY,    // Reading body
		WEB_MESSAGE_READY,   // Waiting read_next() call
	};

	// TODO - fix SHUTDOWN logic, add additional state for Connection: close

	State get_state() const { return state; }

	bool can_write() const { return sock.can_write(); }
	size_t get_total_buffer_size() const { return sock.get_total_buffer_size(); }
	bool is_writing_body() const { return writing_web_message_body || state == RESPONSE_BODY; }

	enum { WM_PING_TIMEOUT_SEC = 45 };
	// Slightly less than default TCP keep-alive of 50 sec

protected:
	Buffer read_buffer;

	RequestParser request_parser;
	BodyParser http_body_parser;

	WebMessageHeaderParser wm_header_parser;    // Chunk header
	WebMessageBodyParser wm_body_parser;        // Chunk body
	details::optional<WebMessage> web_message;  // Built from chunks
	Timer wm_ping_timer;
	// Server-side ping required for some NATs to keep port open
	// TCP keep-alive is set by most browsers, but surprisingly it is not enough.
	// We reset this timer on write() only

	details::optional<uint64_t> remaining_body_content_length;  // empty for chunked

	void sock_handler();
	void on_wm_ping_timer();
	bool advance_state();

	Handler rwd_handler;

	BufferedTCPSocket sock;

	State state                   = REQUEST_HEADER;
	bool writing_web_message_body = false;  // Web Sockets are sockets, you can write in the middle of reading
	Address peer_address;

	bool is_state_websocket() const { return state >= WEB_MESSAGE_HEADER; }
};

}  // namespace http
}  // namespace crab
