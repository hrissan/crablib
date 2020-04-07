// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <algorithm>
#include <iostream>
#include <random>
#include <sstream>
#include "connection.hpp"

namespace crab {

CRAB_INLINE BufferedTCPSocket::BufferedTCPSocket(Handler &&rwd_handler)
    : rwd_handler(std::move(rwd_handler)), sock([this]() { sock_handler(); }) {}

CRAB_INLINE void BufferedTCPSocket::close() {
	data_to_write.clear();
	total_buffer_size    = 0;
	write_shutdown_asked = false;
	sock.close();
}

CRAB_INLINE size_t BufferedTCPSocket::read_some(uint8_t *val, size_t count) {
	if (!write_shutdown_asked)
		return sock.read_some(val, count);
	if (!data_to_write.empty())  // Do nothing until we write everything, including FIN
		return 0;
	while (true) {
		// Now after FIN send, consume and discard all received data till EOF
		// TODO if peer never sends FIN, we probably should close after a timeout...
		uint8_t buffer[4096];
		size_t rd = sock.read_some(buffer, sizeof(buffer));
		if (rd == 0)
			return 0;
	}
}

CRAB_INLINE size_t BufferedTCPSocket::write_some(const uint8_t *val, size_t count) {
	if (!data_to_write.empty())
		return 0;
	return sock.write_some(val, count);
}

CRAB_INLINE void BufferedTCPSocket::buffer(const char *val, size_t count) {
	buffer(reinterpret_cast<const uint8_t *>(val), count);
}

CRAB_INLINE void BufferedTCPSocket::buffer(const uint8_t *val, size_t count) {
	if (write_shutdown_asked || count == 0)
		return;
	total_buffer_size += count;
	if (!data_to_write.empty() && data_to_write.size() < 1024)
		data_to_write.back().write(val, count);
	else
		data_to_write.emplace_back(std::string(reinterpret_cast<const char *>(val), count));
}

CRAB_INLINE void BufferedTCPSocket::buffer(std::string &&ss) {
	if (write_shutdown_asked || ss.empty())
		return;
	total_buffer_size += ss.size();
	if (!data_to_write.empty() && data_to_write.size() < 1024)
		data_to_write.back().write(ss.data(), ss.size());
	else
		data_to_write.emplace_back(std::move(ss));
}

CRAB_INLINE void BufferedTCPSocket::write(const char *val, size_t count, BufferOptions buffer_options) {
	write(reinterpret_cast<const uint8_t *>(val), count, buffer_options);
}

#if __cplusplus >= 201703L
CRAB_INLINE void BufferedTCPSocket::write(const std::byte *val, size_t count, BufferOptions buffer_options) {
	write(reinterpret_cast<const uint8_t *>(val), count, buffer_options);
}
#endif

// TODO - do not allow to write when not connected (underlying socket has it as a NOP)
CRAB_INLINE void BufferedTCPSocket::write(const uint8_t *val, size_t count, BufferOptions buffer_options) {
	if (write_shutdown_asked)
		return;
	if (buffer_options == BUFFER_ONLY) {
		buffer(val, count);
		return;
	}
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
	if (write_shutdown_asked)
		return;
	if (sock.is_open())
		return;
	write_shutdown_asked = true;
	if (data_to_write.empty())
		sock.write_shutdown();
}

CRAB_INLINE void BufferedTCPSocket::write() {
	bool was_empty = data_to_write.empty();
	while (!data_to_write.empty()) {
		total_buffer_size -= data_to_write.front().write_to(sock);
		if (!data_to_write.front().empty())
			break;
		data_to_write.pop_front();
	}
	if (write_shutdown_asked && data_to_write.empty() && !was_empty) {
		sock.write_shutdown();
	}
}

CRAB_INLINE void BufferedTCPSocket::sock_handler() {
	if (sock.is_open()) {
		write();
	} else {
		data_to_write.clear();
		write_shutdown_asked = false;
		total_buffer_size    = 0;
	}
	rwd_handler();
}

namespace http {

CRAB_INLINE Connection::Connection(Handler &&r_handler, Handler &&d_handler)
    : read_buffer(8192)
    , r_handler(std::move(r_handler))
    , d_handler(std::move(d_handler))
    , sock([this]() { sock_handler(); })
    , state(SHUTDOWN) {}

CRAB_INLINE bool Connection::connect(const Address &address) {
	close();
	if (!sock.connect(address))
		return false;
	peer_address = address;
	client_side  = true;
	state        = WAITING_WRITE_REQUEST;
	return true;
}

CRAB_INLINE void Connection::accept(TCPAcceptor &acceptor) {
	close();
	sock.accept(acceptor, &peer_address);
	client_side    = false;
	request_parser = RequestParser{};
	state          = REQUEST_HEADER;
}

CRAB_INLINE void Connection::close() {
	state = SHUTDOWN;
	read_buffer.clear();
	sock.close();
	peer_address = Address();
	w_handler    = StreamHandler{};
}

CRAB_INLINE bool Connection::read_next(Request &req) {
	if (state != REQUEST_READY)
		return false;
	req.body   = http_body_parser.body.clear();
	req.header = request_parser.req;
	// We do not move req, because we must remember params for response
	state = WAITING_WRITE_RESPONSE_HEADER;
	advance_state(false);
	return true;
}

CRAB_INLINE bool Connection::read_next(Response &req) {
	if (state != RESPONSE_READY)
		return false;
	req.body   = http_body_parser.body.clear();
	req.header = std::move(response_parser.req);
	state      = WAITING_WRITE_REQUEST;
	advance_state(false);
	return true;
}

CRAB_INLINE bool Connection::read_next(WebMessage &message) {
	if (state != WEB_MESSAGE_READY)
		return false;
	message.body     = wm_body_parser.body.clear();
	message.opcode   = wm_header_parser.req.opcode;
	wm_header_parser = MessageChunkParser{};
	wm_body_parser   = MessageBodyParser{};
	state            = WEB_MESSAGE_HEADER;
	advance_state(false);
	return true;
}

CRAB_INLINE void Connection::web_socket_upgrade() {
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

CRAB_INLINE void Connection::write(Request &&req) {
	if (state == SHUTDOWN)
		return;  // This NOP simplifies state machines of connection users
	invariant(state == WAITING_WRITE_REQUEST, "Connection unexpected write");
	invariant(req.header.http_version_major, "Someone forgot to set version, method, status or url");

	invariant(!req.header.transfer_encoding_chunked, "As the whole body is sent, makes no sense");
	sock.write(req.header.to_string(), BUFFER_ONLY);
	sock.write(std::move(req.body));

	if (req.header.is_websocket_upgrade()) {
		response_parser   = ResponseParser{};
		state             = WEB_UPGRADE_RESPONSE_HEADER;
		sec_websocket_key = req.header.sec_websocket_key;
	} else {
		if (req.header.keep_alive) {
			response_parser = ResponseParser{};
			state           = RESPONSE_HEADER;
		} else {
			sock.write_shutdown();
			state = SHUTDOWN;
		}
	}
}

CRAB_INLINE void Connection::write(Response &&resp) {
	if (state == SHUTDOWN)
		return;  // This NOP simplifies state machines of connection users
	const bool is_websocket_upgrade      = resp.header.is_websocket_upgrade();
	const bool transfer_encoding_chunked = resp.header.transfer_encoding_chunked;
	write(std::move(resp.header), BUFFER_ONLY);
	write(std::move(resp.body), BUFFER_ONLY);
	if (transfer_encoding_chunked)
		write_last_chunk();      // Otherwise, state is already switched into RECEIVE_HEADER
	if (is_websocket_upgrade) {  // TODO - better logic here
		wm_header_parser = MessageChunkParser{};
		wm_body_parser   = MessageBodyParser{};
		state            = WEB_MESSAGE_HEADER;
	}
}

CRAB_INLINE void Connection::write(WebMessage &&message) {
	if (state == SHUTDOWN)
		return;  // This NOP simplifies state machines of connection users
	invariant(state == WEB_MESSAGE_HEADER || state == WEB_MESSAGE_BODY || state == WEB_MESSAGE_READY ||
	              state == WEB_UPGRADE_RESPONSE_HEADER,
	    "Connection unexpected write");

	uint32_t masking_key = client_side ? static_cast<uint32_t>(masking_key_random()) : 0;
	uint8_t frame_buffer[32];
	auto frame_buffer_len = MessageChunkParser::write_message_frame(frame_buffer, message, client_side, masking_key);
	if (client_side)
		MessageChunkParser::mask_data(0, &message.body[0], message.body.size(), masking_key);

	sock.write(frame_buffer, frame_buffer_len, BUFFER_ONLY);
	sock.write(std::move(message.body));
	if (message.opcode == WebMessage::OPCODE_CLOSE)
		wm_close_sent = true;
}

CRAB_INLINE void Connection::write(http::ResponseHeader &&resp, BufferOptions buffer_options) {
	if (state == SHUTDOWN)
		return;  // This NOP simplifies state machines of connection users
	invariant(state == WAITING_WRITE_RESPONSE_HEADER, "Connection unexpected write");

	resp.http_version_major = request_parser.req.http_version_major;
	resp.http_version_minor = request_parser.req.http_version_minor;
	resp.keep_alive         = request_parser.req.keep_alive;

	invariant(resp.is_websocket_upgrade() || resp.transfer_encoding_chunked || resp.has_content_length(),
	    "Please set either chunked encoding or content_length");
	body_content_length = resp.content_length;
	body_position       = 0;
	sock.write(resp.to_string(), buffer_options);
	state = WAITING_WRITE_RESPONSE_BODY;
}

CRAB_INLINE void Connection::write(const char *val, size_t count, BufferOptions buffer_options) {
	write(reinterpret_cast<const uint8_t *>(val), count, buffer_options);
}

#if __cplusplus >= 201703L
CRAB_INLINE void Connection::write(const std::byte *val, size_t count, BufferOptions buffer_options) {
	write(reinterpret_cast<const uint8_t *>(val), count, buffer_options);
}
#endif

CRAB_INLINE void Connection::write(const uint8_t *val, size_t count, BufferOptions buffer_options) {
	invariant(state == WAITING_WRITE_RESPONSE_BODY, "Connection unexpected write");
	if (body_content_length != std::numeric_limits<uint64_t>::max()) {
		invariant(body_position + count <= body_content_length, "Overshoot content-length");
		body_position += count;
		sock.write(val, count, buffer_options);
		write_last_chunk();
		return;
	}
	if (count == 0)
		return;  // Empty chunk is terminator
	char buf[64]{};
	int buf_n = std::sprintf(buf, "%llx\r\n", (unsigned long long)count);
	invariant(buf_n > 0, "sprintf error (unexpected)");
	sock.write(buf, buf_n, BUFFER_ONLY);
	sock.write(val, count, BUFFER_ONLY);
	sock.write("\r\n", 2, buffer_options);
}

CRAB_INLINE void Connection::write(std::string &&ss, BufferOptions buffer_options) {
	invariant(state == WAITING_WRITE_RESPONSE_BODY, "Connection unexpected write");
	if (body_content_length != std::numeric_limits<uint64_t>::max()) {
		invariant(body_position + ss.size() <= body_content_length, "Overshoot content-length");
		body_position += ss.size();
		sock.write(std::move(ss), buffer_options);
		write_last_chunk();
		return;
	}
	if (ss.empty())
		return;  // Empty chunk is terminator
	char buf[64]{};
	int buf_n = std::sprintf(buf, "%llx\r\n", (unsigned long long)ss.size());
	invariant(buf_n > 0, "sprintf error (unexpected)");
	sock.write(buf, buf_n, BUFFER_ONLY);
	sock.write(std::move(ss), BUFFER_ONLY);
	sock.write("\r\n", 2, buffer_options);
}

CRAB_INLINE void Connection::write_last_chunk() {
	invariant(state == WAITING_WRITE_RESPONSE_BODY, "Connection unexpected write");
	if (body_content_length != std::numeric_limits<uint64_t>::max()) {
		if (body_position != body_content_length)
			return;
		sock.write(std::string{});  // if everything was buffered, flush
	} else {
		sock.write(std::string{"0\r\n\r\n"});
	}
	w_handler = StreamHandler{};
	if (request_parser.req.keep_alive) {  // We sent it in our response header
		request_parser = RequestParser{};
		state          = REQUEST_HEADER;
	} else {
		sock.write_shutdown();
		state = SHUTDOWN;
	}
}

CRAB_INLINE void Connection::write(ResponseHeader &&resp, StreamHandler &&cb) {
	write(std::move(resp), WRITE);
	w_handler = std::move(cb);
	// This interface experimental, so no understanding if Client and Connection be merged
	while (state == WAITING_WRITE_RESPONSE_BODY && body_position < body_content_length && sock.can_write()) {
		w_handler(body_position, body_content_length);
	}
}

CRAB_INLINE size_t Connection::write_some(const uint8_t *val, size_t count) {
	invariant(state == WAITING_WRITE_RESPONSE_BODY, "Connection unexpected write");
	if (body_content_length != std::numeric_limits<uint64_t>::max()) {
		invariant(body_position + count <= body_content_length, "Overshoot content-length");
		auto wr = sock.write_some(val, count);
		body_position += wr;
		write_last_chunk();
		return wr;
	}
	// For chunked encoding, less efficient algo that will use some buffering
	if (sock.get_total_buffer_size() != 0)
		return 0;
	char buf[64]{};
	int buf_n = std::sprintf(buf, "%llx\r\n", (unsigned long long)count);
	invariant(buf_n > 0, "sprintf error (unexpected)");
	sock.write(buf, buf_n, BUFFER_ONLY);
	sock.write(val, count, BUFFER_ONLY);
	sock.write("\r\n", 2);
	return count;
}

CRAB_INLINE void Connection::sock_handler() {
	if (!sock.is_open()) {
		close();
		d_handler();
		return;
	}
	if (w_handler) {
		while (state == WAITING_WRITE_RESPONSE_BODY && body_position < body_content_length && sock.can_write()) {
			w_handler(body_position, body_content_length);
		}
	}
	advance_state(true);
}

CRAB_INLINE void Connection::advance_state(bool called_from_runloop) {
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
			// Fall through (to correctly handle zero-length body)
			case REQUEST_BODY:
				http_body_parser.parse(read_buffer);
				if (!http_body_parser.is_good())
					continue;
				state = REQUEST_READY;
				if (called_from_runloop)
					r_handler();
				return;
			case RESPONSE_HEADER:
				response_parser.parse(read_buffer);
				if (!response_parser.is_good())
					continue;
				if (response_parser.req.is_websocket_upgrade())
					throw std::runtime_error("Unexpected web upgrade header");
				http_body_parser =
				    BodyParser{response_parser.req.content_length, response_parser.req.transfer_encoding_chunked};
				response_parser.req.transfer_encoding_chunked = false;  // Hide from clients
				state                                         = RESPONSE_BODY;
			// Fall through (to correctly handle zero-length body)
			case RESPONSE_BODY:
				http_body_parser.parse(read_buffer);
				if (!http_body_parser.is_good())
					continue;
				state = RESPONSE_READY;
				if (called_from_runloop)
					r_handler();
				return;
			case WEB_UPGRADE_RESPONSE_HEADER:
				response_parser.parse(read_buffer);
				if (!response_parser.is_good())
					continue;
				if (!response_parser.req.is_websocket_upgrade())
					throw std::runtime_error("Expecting web upgrade header");
				if (response_parser.req.has_content_length() || response_parser.req.transfer_encoding_chunked)
					throw std::runtime_error("Web upgrade reponse cannot have body");
				if (response_parser.req.sec_websocket_accept !=
				    ResponseHeader::generate_sec_websocket_accept(sec_websocket_key))
					throw std::runtime_error("Wrong value of 'Sec-WebSocket-Accept' header");
				wm_header_parser = MessageChunkParser{};
				wm_body_parser   = MessageBodyParser{};
				state            = WEB_MESSAGE_HEADER;
				continue;
			case WEB_MESSAGE_HEADER:
				wm_header_parser.parse(read_buffer);
				if (!wm_header_parser.is_good())
					continue;
				if (!client_side && !wm_header_parser.req.mask)
					throw std::runtime_error("Web Socket Client must use masking");
				wm_body_parser.add_chunk(wm_header_parser.req);
				state = WEB_MESSAGE_BODY;
			// Fall through (to correctly handle zero-length body)
			case WEB_MESSAGE_BODY:
				wm_body_parser.parse(read_buffer);
				if (!wm_body_parser.is_good())
					continue;
				if (!wm_header_parser.req.fin) {
					wm_header_parser = MessageChunkParser{wm_header_parser.req.opcode};
					state            = WEB_MESSAGE_HEADER;
					continue;
				}
				state = WEB_MESSAGE_READY;
				if (wm_header_parser.req.opcode == WebMessage::OPCODE_CLOSE) {
					WebMessage nop;
					invariant(read_next(nop), "WebSocket read_next OPCODE_CLOSE did no succeed");
					if (!wm_close_sent) {
						write(std::move(nop));  // We echo reason back
					}
					sock.write_shutdown();
					state = SHUTDOWN;
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
					r_handler();
				return;
			default:  // waiting write, closing, etc
				return;
			}
		}
	} catch (const std::exception &) {
		sock.write_shutdown();
		state = SHUTDOWN;
	}
}

}  // namespace http
}  // namespace crab
