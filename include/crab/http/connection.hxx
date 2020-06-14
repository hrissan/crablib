// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <algorithm>
#include <iostream>
#include <sstream>
#include "connection.hpp"

namespace crab {

CRAB_INLINE BufferedTCPSocket::BufferedTCPSocket(Handler &&rwd_handler)
    : rwd_handler(std::move(rwd_handler))
    , sock([this]() { sock_handler(); })
    , shutdown_timer([this]() { shutdown_timer_handler(); }) {}

CRAB_INLINE void BufferedTCPSocket::close() {
	data_to_write.clear();
	total_data_to_write  = 0;
	write_shutdown_asked = false;
	sock.close();
	shutdown_timer.cancel();
}

CRAB_INLINE size_t BufferedTCPSocket::read_some(uint8_t *val, size_t count) {
	if (write_shutdown_asked)
		return 0;
	return sock.read_some(val, count);
}

CRAB_INLINE size_t BufferedTCPSocket::write_some(const uint8_t *val, size_t count) {
	if (write_shutdown_asked || !data_to_write.empty())
		return 0;
	return sock.write_some(val, count);
}

CRAB_INLINE void BufferedTCPSocket::buffer(const uint8_t *val, size_t count) {
	if (!sock.is_open() || write_shutdown_asked || count == 0)
		return;
	total_data_to_write += count;
	if (!data_to_write.empty() && data_to_write.back().size() < 1024 && count < 1024)  // TODO - constant
		data_to_write.back().write(val, count);
	else
		data_to_write.emplace_back(std::string(reinterpret_cast<const char *>(val), count));
}

CRAB_INLINE void BufferedTCPSocket::buffer(std::string &&ss) {
	if (!sock.is_open() || write_shutdown_asked || ss.empty())
		return;
	total_data_to_write += ss.size();
	if (!data_to_write.empty() && data_to_write.back().size() < 1024 && ss.size() < 1024)  // TODO - constant
		data_to_write.back().write(ss.data(), ss.size());
	else
		data_to_write.emplace_back(std::move(ss));
}

CRAB_INLINE void BufferedTCPSocket::write(const uint8_t *val, size_t count, BufferOptions buffer_options) {
	if (buffer_options == BUFFER_ONLY)
		return buffer(val, count);
	if (!sock.is_open() || write_shutdown_asked)
		return;
	if (data_to_write.empty()) {
		size_t wr = sock.write_some(val, count);
		val += wr;
		count -= wr;
	}
	buffer(val, count);
	write();
}

CRAB_INLINE void BufferedTCPSocket::write(std::string &&ss, BufferOptions buffer_options) {
	buffer(std::move(ss));
	if (buffer_options != BUFFER_ONLY)
		write();
}

CRAB_INLINE void BufferedTCPSocket::write_shutdown() {
	if (!sock.is_open() || write_shutdown_asked)
		return;
	write_shutdown_asked = true;
	if (data_to_write.empty()) {
		sock.write_shutdown();
		shutdown_timer.once(WM_SHUTDOWN_TIMEOUT_SEC);
	}
}

CRAB_INLINE void BufferedTCPSocket::write() {
	bool was_empty = data_to_write.empty();
	while (!data_to_write.empty()) {
		total_data_to_write -= data_to_write.front().write_to(sock);
		if (!data_to_write.front().empty())
			break;
		data_to_write.pop_front();
	}
	if (write_shutdown_asked && data_to_write.empty() && !was_empty) {
		sock.write_shutdown();
		shutdown_timer.once(WM_SHUTDOWN_TIMEOUT_SEC);
	}
}

CRAB_INLINE void BufferedTCPSocket::sock_handler() {
	if (sock.is_open()) {
		write();
		if (write_shutdown_asked && data_to_write.empty()) {
			// Now after FIN sent, we consume and discard a bit of received data.
			// We cannot loop here, because client can send gigabytes of data instead of FIN.
			// If client sends FIN, connection will close gracefully, if not, RST in shutdown_timer_handler
			uint8_t buffer[4096];  // Uninitialized
			sock.read_some(buffer, sizeof(buffer));
		}
	} else {
		close();
	}
	rwd_handler();
}

CRAB_INLINE void BufferedTCPSocket::shutdown_timer_handler() {
	close();
	rwd_handler();
}

namespace http {

CRAB_INLINE ClientConnection::ClientConnection(Handler &&rwd_handler)
    : read_buffer(8192)
    , rwd_handler(std::move(rwd_handler))
    , dns([this](const std::vector<Address> &names) { dns_handler(names); })
    , sock([this]() { sock_handler(); })
    , state(RESOLVING_HOST) {}  // Never compared in closed state

CRAB_INLINE bool ClientConnection::connect(const std::string &h, uint16_t p, const std::string &pr) {
	close();
	if (pr != Literal{"http"} && pr != Literal{"https"})
		throw std::runtime_error("ClientConnection unsupported protocol");
	dns.resolve(h, p, true, false);
	host     = h;
	port     = p;
	protocol = pr;
	state    = RESOLVING_HOST;
	return true;
}

CRAB_INLINE bool ClientConnection::connect(const Address &address) {
	close();
	if (!sock.connect(address))
		return false;
	peer_address = address;
	host         = address.get_address();
	port         = address.get_port();
	protocol     = "http";
	state        = WAITING_WRITE_REQUEST;
	return true;
}

CRAB_INLINE void ClientConnection::close() {
	state = RESOLVING_HOST;
	read_buffer.clear();
	dns.cancel();
	waiting_request.reset();
	sock.close();
	peer_address = Address();
	protocol.clear();
	host.clear();
	port = 0;
}

CRAB_INLINE bool ClientConnection::read_next(Response &req) {
	if (state != RESPONSE_READY)
		return false;
	req.body   = http_body_parser.body.clear();
	req.header = std::move(response_parser.req);
	state      = WAITING_WRITE_REQUEST;
	advance_state(false);
	return true;
}

CRAB_INLINE bool ClientConnection::read_next(WebMessage &message) {
	if (state != WEB_MESSAGE_READY)
		return false;
	message.body     = wm_body_parser.body.clear();
	message.opcode   = wm_header_parser.req.opcode;
	wm_header_parser = MessageHeaderParser{};
	wm_body_parser   = MessageBodyParser{};
	state            = WEB_MESSAGE_HEADER;
	advance_state(false);
	return true;
}

CRAB_INLINE void ClientConnection::write(Request &&req) {
	if (!is_open())
		return;  // This NOP simplifies state machines of connection users
	if (state == RESOLVING_HOST) {
		invariant(!waiting_request, "Duplicate write RESOLVING_HOST state");
		waiting_request = std::move(req);
		return;
	}
	req.header.host = host;
	invariant(state == WAITING_WRITE_REQUEST, "Connection unexpected write");
	invariant(req.header.http_version_major && !req.header.method.empty() && !req.header.path.empty(),
	    "Someone forgot to set version, method or path");

	invariant(!req.header.transfer_encoding_chunked, "As the whole body is sent, makes no sense");
	sock.buffer(req.header.to_string());
	sock.write(std::move(req.body));

	response_parser = ResponseParser{};
	if (req.header.is_websocket_upgrade()) {
		state             = WEB_UPGRADE_RESPONSE_HEADER;
		sec_websocket_key = req.header.sec_websocket_key;
	} else {
		state = RESPONSE_HEADER;
	}
}

CRAB_INLINE void ClientConnection::write(WebMessage &&message) {
	if (!is_open())
		return;  // This NOP simplifies state machines of connection users
	invariant(state == WEB_MESSAGE_HEADER || state == WEB_MESSAGE_BODY || state == WEB_MESSAGE_READY ||
	              state == WEB_UPGRADE_RESPONSE_HEADER,
	    "Connection unexpected write");

	uint32_t masking_key = rnd.pod<uint32_t>();
	uint8_t frame_buffer[MessageHeaderParser::MESSAGE_FRAME_BUFFER_SIZE];  // Uninitialized
	auto frame_buffer_len = MessageHeaderParser::write_message_frame(frame_buffer, message, masking_key);
	MessageHeaderParser::mask_data(0, &message.body[0], message.body.size(), masking_key);

	sock.buffer(frame_buffer, frame_buffer_len);
	sock.write(std::move(message.body));
	if (message.opcode == WebMessage::OPCODE_CLOSE) {
		// We will not attempt to read response. Client logic should not depend on it.
		sock.write_shutdown();
	}
}

CRAB_INLINE void ClientConnection::web_socket_upgrade(const RequestHeader &rh) {
	Request req;
	req.header                       = rh;
	req.header.http_version_major    = 1;
	req.header.http_version_minor    = 1;
	req.header.connection_upgrade    = true;
	req.header.method                = "GET";
	req.header.upgrade_websocket     = true;
	req.header.sec_websocket_version = "13";

	uint8_t rdata[16]{};
	rnd.bytes(rdata, sizeof(rdata));
	req.header.sec_websocket_key = base64::encode(rdata, sizeof(rdata));

	write(std::move(req));
}

CRAB_INLINE void ClientConnection::dns_handler(const std::vector<Address> &names) {
	if (names.empty()) {
		close();
		rwd_handler();
		return;
	}
	peer_address = names[rnd.pod<size_t>() % names.size()];  // non-zero chance with even single server up
	if (protocol == Literal{"http"}) {
		if (!sock.connect(peer_address)) {
			close();
			rwd_handler();
			return;
		}
	} else {  // https, check is in connect
		if (!sock.connect_tls(peer_address, host)) {
			close();
			rwd_handler();
			return;
		}
	}
	state = WAITING_WRITE_REQUEST;
	if (waiting_request)
		write(std::move(*waiting_request));
	waiting_request.reset();
}

CRAB_INLINE void ClientConnection::sock_handler() {
	if (!sock.is_open()) {
		close();
		rwd_handler();
		return;
	}
	advance_state(true);
}

CRAB_INLINE void ClientConnection::advance_state(bool called_from_runloop) {
	// do not process new request if too much data waiting to be sent
	if (sock.get_total_buffer_size() > 65536)  // TODO - constant
		return;
	try {
		while (true) {
			if (read_buffer.empty() && read_buffer.read_from(sock) == 0)
				return;
			switch (state) {
			case RESPONSE_HEADER:
				response_parser.parse(read_buffer);
				if (!response_parser.is_good())
					continue;
				if (response_parser.req.is_websocket_upgrade())
					throw std::runtime_error("Unexpected web upgrade header");
				http_body_parser =
				    BodyParser{response_parser.req.content_length, response_parser.req.transfer_encoding_chunked};
				state = RESPONSE_BODY;
				// Fall through (to correctly handle zero-length body). Next line is understood by GCC
				// Fall through
			case RESPONSE_BODY:
				http_body_parser.parse(read_buffer);
				if (!http_body_parser.is_good())
					continue;
				state = RESPONSE_READY;
				if (called_from_runloop)
					rwd_handler();
				return;
			case WEB_UPGRADE_RESPONSE_HEADER:
				response_parser.parse(read_buffer);
				if (!response_parser.is_good())
					continue;
				if (!response_parser.req.is_websocket_upgrade())
					throw std::runtime_error("Expecting web upgrade header");
				if (response_parser.req.content_length || response_parser.req.transfer_encoding_chunked)
					throw std::runtime_error("Web upgrade reponse cannot have body");
				if (response_parser.req.sec_websocket_accept !=
				    ResponseHeader::generate_sec_websocket_accept(sec_websocket_key))
					throw std::runtime_error("Wrong value of 'Sec-WebSocket-Accept' header");
				wm_header_parser = MessageHeaderParser{};
				wm_body_parser   = MessageBodyParser{};
				state            = WEB_MESSAGE_HEADER;
				continue;
			case WEB_MESSAGE_HEADER:
				wm_header_parser.parse(read_buffer);
				if (!wm_header_parser.is_good())
					continue;
				wm_body_parser.add_chunk(wm_header_parser.req);
				state = WEB_MESSAGE_BODY;
				// Fall through (to correctly handle zero-length body). Next line is understood by GCC
				// Fall through
			case WEB_MESSAGE_BODY:
				wm_body_parser.parse(read_buffer);
				if (!wm_body_parser.is_good())
					continue;
				if (!wm_header_parser.req.fin) {
					wm_header_parser = MessageHeaderParser{wm_header_parser.req.opcode};
					state            = WEB_MESSAGE_HEADER;
					continue;
				}
				state = WEB_MESSAGE_READY;
				if (wm_header_parser.req.opcode == WebMessage::OPCODE_CLOSE) {
					WebMessage nop;
					invariant(read_next(nop), "WebSocket read_next OPCODE_CLOSE did no succeed");
					write(std::move(nop));  // We echo reason back. sock.shutdown will happen inside write()
					return;
				}
				if (wm_header_parser.req.opcode == WebMessage::OPCODE_PING) {
					WebMessage nop;
					invariant(read_next(nop), "WebSocket read_next OPCODE_PING did no succeed");
					nop.opcode = WebMessage::OPCODE_PONG;
					write(std::move(nop));
					continue;
				}
				if (wm_header_parser.req.opcode == WebMessage::OPCODE_PONG) {
					WebMessage nop;
					invariant(read_next(nop), "WebSocket read_next OPCODE_PONG did no succeed");
					continue;
				}
				if (called_from_runloop)
					rwd_handler();
				return;
			default:  // waiting write, closing, etc
				return;
			}
		}
	} catch (const std::exception &) {
		close();
		if (called_from_runloop)
			rwd_handler();
	}
}

CRAB_INLINE ServerConnection::ServerConnection(Handler &&rwd_handler)
    : read_buffer(8192)
    , wm_ping_timer([&]() { on_wm_ping_timer(); })
    , rwd_handler(std::move(rwd_handler))
    , sock([this]() { sock_handler(); })
    , state(REQUEST_HEADER) {}

CRAB_INLINE void ServerConnection::accept(TCPAcceptor &acceptor) {
	close();
	sock.accept(acceptor, &peer_address);
	request_parser = RequestParser{};
	state          = REQUEST_HEADER;
}

CRAB_INLINE void ServerConnection::close() {
	state = REQUEST_HEADER;
	wm_ping_timer.cancel();
	read_buffer.clear();
	sock.close();
	peer_address = Address();
}

CRAB_INLINE bool ServerConnection::read_next(Request &req) {
	if (state != REQUEST_READY)
		return false;
	req.body   = http_body_parser.body.clear();
	req.header = request_parser.req;
	// We do not move req, because we must remember params for response
	state = WAITING_WRITE_RESPONSE_HEADER;
	advance_state(false);
	return true;
}

CRAB_INLINE bool ServerConnection::read_next(WebMessage &message) {
	if (state != WEB_MESSAGE_READY)
		return false;
	message.body     = wm_body_parser.body.clear();
	message.opcode   = wm_header_parser.req.opcode;
	wm_header_parser = MessageHeaderParser{};
	wm_body_parser   = MessageBodyParser{};
	state            = WEB_MESSAGE_HEADER;
	advance_state(false);
	return true;
}

CRAB_INLINE void ServerConnection::web_socket_upgrade() {
	if (!request_parser.req.is_websocket_upgrade())
		throw std::runtime_error("Attempt to upgrade non-upgradable connection");

	Response response;

	response.header.connection_upgrade = request_parser.req.connection_upgrade;
	response.header.upgrade_websocket  = request_parser.req.upgrade_websocket;
	response.header.sec_websocket_accept =
	    ResponseHeader::generate_sec_websocket_accept(request_parser.req.sec_websocket_key);
	response.header.status = 101;

	write(std::move(response));
}

CRAB_INLINE void ServerConnection::write(Response &&resp) {
	if (!is_open())
		return;  // This NOP simplifies state machines of connection users
	const bool is_websocket_upgrade      = resp.header.is_websocket_upgrade();
	const bool transfer_encoding_chunked = resp.header.transfer_encoding_chunked;
	write(std::move(resp.header), BUFFER_ONLY);
	write(std::move(resp.body), BUFFER_ONLY);
	if (transfer_encoding_chunked)
		write_last_chunk();  // Otherwise, state is already switched into RECEIVE_HEADER
	// shutdown is already written during switch to RECEIVE_HEADER
	if (is_websocket_upgrade) {  // TODO - better logic here
		wm_header_parser = MessageHeaderParser{};
		wm_body_parser   = MessageBodyParser{};
		state            = WEB_MESSAGE_HEADER;
		wm_ping_timer.once(WM_PING_TIMEOUT_SEC);  // Always server-side
	}
}

CRAB_INLINE void ServerConnection::write(WebMessage &&message) {
	if (!is_open())
		return;  // This NOP simplifies state machines of connection users
	invariant(state == WEB_MESSAGE_HEADER || state == WEB_MESSAGE_BODY || state == WEB_MESSAGE_READY,
	    "Connection unexpected write");

	uint8_t frame_buffer[MessageHeaderParser::MESSAGE_FRAME_BUFFER_SIZE];  // Uninitialized
	auto frame_buffer_len = MessageHeaderParser::write_message_frame(frame_buffer, message, {});
	// Server-side uses no masking key

	sock.buffer(frame_buffer, frame_buffer_len);
	sock.write(std::move(message.body));
	if (message.opcode == WebMessage::OPCODE_CLOSE) {
		wm_ping_timer.cancel();
		sock.write_shutdown();
	} else {
		wm_ping_timer.once(WM_PING_TIMEOUT_SEC);
	}
}

CRAB_INLINE void ServerConnection::write(ResponseHeader &&resp, BufferOptions buffer_options) {
	if (!is_open())
		return;  // This NOP simplifies state machines of connection users
	invariant(state == WAITING_WRITE_RESPONSE_HEADER, "Connection unexpected write");

	resp.http_version_major = request_parser.req.http_version_major;
	resp.http_version_minor = request_parser.req.http_version_minor;
	resp.keep_alive         = request_parser.req.keep_alive;

	invariant(resp.is_websocket_upgrade() || resp.transfer_encoding_chunked || resp.content_length,
	    "Please set either chunked encoding or content_length");
	remaining_body_content_length = resp.content_length;
	sock.write(resp.to_string(), buffer_options);
	state = WAITING_WRITE_RESPONSE_BODY;
}

CRAB_INLINE void ServerConnection::write(const uint8_t *val, size_t count, BufferOptions buffer_options) {
	invariant(state == WAITING_WRITE_RESPONSE_BODY, "Connection unexpected write");
	if (remaining_body_content_length) {
		invariant(count <= *remaining_body_content_length, "Overshoot content-length");
		*remaining_body_content_length -= count;
		sock.write(val, count, buffer_options);
		if (*remaining_body_content_length == 0) {
			sock.write(std::string{});           // if everything was buffered, flush
			if (!request_parser.req.keep_alive)  // We sent it in our response header
				sock.write_shutdown();
			request_parser = RequestParser{};
			state          = REQUEST_HEADER;
		}
		return;
	}
	if (count == 0)
		return;  // Empty chunk is terminator
	char buf[64]{};
	int buf_n = std::sprintf(buf, "%llx\r\n", (unsigned long long)count);
	invariant(buf_n > 0, "sprintf error (unexpected)");
	sock.buffer(buf, buf_n);
	sock.buffer(val, count);
	sock.write("\r\n", 2, buffer_options);
}

CRAB_INLINE void ServerConnection::write(std::string &&ss, BufferOptions buffer_options) {
	invariant(state == WAITING_WRITE_RESPONSE_BODY, "Connection unexpected write");
	if (remaining_body_content_length) {
		invariant(ss.size() <= *remaining_body_content_length, "Overshoot content-length");
		*remaining_body_content_length -= ss.size();
		sock.write(std::move(ss), buffer_options);
		if (*remaining_body_content_length == 0) {
			sock.write(std::string{});           // if everything was buffered, flush
			if (!request_parser.req.keep_alive)  // We sent it in our response header
				sock.write_shutdown();
			request_parser = RequestParser{};
			state          = REQUEST_HEADER;
		}
		return;
	}
	if (ss.empty())
		return;  // Empty chunk is terminator
	char buf[64]{};
	int buf_n = std::sprintf(buf, "%llx\r\n", (unsigned long long)ss.size());
	invariant(buf_n > 0, "sprintf error (unexpected)");
	sock.buffer(buf, buf_n);
	sock.buffer(std::move(ss));
	sock.write("\r\n", 2, buffer_options);
}

CRAB_INLINE void ServerConnection::write_last_chunk() {
	invariant(state == WAITING_WRITE_RESPONSE_BODY, "Connection unexpected write");
	invariant(!remaining_body_content_length, "write_last_chunk is for chunked encoding only");
	sock.write(std::string{"0\r\n\r\n"});
	if (!request_parser.req.keep_alive)  // We sent it in our response header
		sock.write_shutdown();
	request_parser = RequestParser{};
	state          = REQUEST_HEADER;
}

CRAB_INLINE void ServerConnection::sock_handler() {
	if (!sock.is_open()) {
		close();
		rwd_handler();
		return;
	}
	if (state == WAITING_WRITE_RESPONSE_BODY) {
		rwd_handler();  // So body streaming will work
		// This follows usual async pull socket pattern, when after finishing writing body client will
		// call read_next, which will call advance_state, and so on
		return;
	}
	if (state == WEB_MESSAGE_HEADER || state == WEB_MESSAGE_BODY || state == WEB_MESSAGE_READY) {
		wm_ping_timer.once(WM_PING_TIMEOUT_SEC);
	}
	advance_state(true);
}

CRAB_INLINE void ServerConnection::on_wm_ping_timer() {
	if (sock.get_total_buffer_size() == 0)
		write(WebMessage{WebMessage::OPCODE_PING, std::string{}});
	wm_ping_timer.once(WM_PING_TIMEOUT_SEC);
}

CRAB_INLINE void ServerConnection::advance_state(bool called_from_runloop) {
	// do not process new request if too much data waiting to be sent
	if (sock.get_total_buffer_size() > 65536)  // TODO - constant
		return;
	try {
		while (true) {
			if (read_buffer.empty() && read_buffer.read_from(sock) == 0)
				return;
			switch (state) {
			case REQUEST_HEADER:
				request_parser.parse(read_buffer);
				if (!request_parser.is_good())
					continue;
				http_body_parser =
				    BodyParser{request_parser.req.content_length, request_parser.req.transfer_encoding_chunked};
				request_parser.req.transfer_encoding_chunked = false;  // Hide from clients
				state                                        = REQUEST_BODY;
				// Fall through (to correctly handle zero-length body). Next line is understood by GCC
				// Fall through
			case REQUEST_BODY:
				http_body_parser.parse(read_buffer);
				if (!http_body_parser.is_good())
					continue;
				state = REQUEST_READY;
				if (called_from_runloop)
					rwd_handler();
				return;
			case WEB_MESSAGE_HEADER:
				wm_header_parser.parse(read_buffer);
				if (!wm_header_parser.is_good())
					continue;
				if (!wm_header_parser.req.mask)
					throw std::runtime_error("Web Socket Client must use masking");
				wm_body_parser.add_chunk(wm_header_parser.req);
				state = WEB_MESSAGE_BODY;
				// Fall through (to correctly handle zero-length body). Next line is understood by GCC
				// Fall through
			case WEB_MESSAGE_BODY:
				wm_body_parser.parse(read_buffer);
				if (!wm_body_parser.is_good())
					continue;
				if (!wm_header_parser.req.fin) {
					wm_header_parser = MessageHeaderParser{wm_header_parser.req.opcode};
					state            = WEB_MESSAGE_HEADER;
					continue;
				}
				state = WEB_MESSAGE_READY;
				if (wm_header_parser.req.opcode == WebMessage::OPCODE_CLOSE) {
					WebMessage nop;
					invariant(read_next(nop), "WebSocket read_next OPCODE_CLOSE did no succeed");
					write(std::move(nop));  // We echo reason back. sock.shutdown will happen inside write()
					return;
				}
				if (wm_header_parser.req.opcode == WebMessage::OPCODE_PING) {
					WebMessage nop;
					invariant(read_next(nop), "WebSocket read_next OPCODE_PING did no succeed");
					nop.opcode = WebMessage::OPCODE_PONG;
					write(std::move(nop));
					continue;
				}
				if (wm_header_parser.req.opcode == WebMessage::OPCODE_PONG) {
					WebMessage nop;
					invariant(read_next(nop), "WebSocket read_next OPCODE_PONG did no succeed");
					continue;
				}
				if (called_from_runloop)
					rwd_handler();
				return;
			default:  // waiting write, closing, etc
				return;
			}
		}
	} catch (const std::exception &) {
		close();
		if (called_from_runloop)
			rwd_handler();
	}
}

}  // namespace http
}  // namespace crab
