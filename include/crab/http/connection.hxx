// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <algorithm>
#include <iostream>
#include <random>
#include <sstream>
#include "connection.hpp"

namespace crab {

CRAB_INLINE BufferedTCPSocket::BufferedTCPSocket(Handler &&r_handler, Handler &&d_handler)
    : r_handler(std::move(r_handler))
    , d_handler(std::move(d_handler))
    , sock([this]() { on_rw_handler(); }, [this]() { on_disconnect(); }) {}

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

// TODO - do not allow to write when not connected (underlying socket has it as a NOP)
CRAB_INLINE void BufferedTCPSocket::write(const uint8_t *val, size_t count, bool buffer_only) {
	if (buffer_only) {
		if (count == 0)
			return;
		total_buffer_size += count;
		if (!data_to_write.empty() && data_to_write.size() < 1024)
			data_to_write.back().write(val, count);
		else
			data_to_write.emplace_back(std::string(reinterpret_cast<const char *>(val), count));
		return;
	}
	if (data_to_write.empty()) {
		size_t wr = sock.write_some(val, count);
		val += wr;
		count -= wr;
	}
	if (count == 0)
		return;
	total_buffer_size += count;
	if (!data_to_write.empty() && data_to_write.size() < 1024)
		data_to_write.back().write(val, count);
	else
		data_to_write.emplace_back(std::string(reinterpret_cast<const char *>(val), count));
	write();
}

CRAB_INLINE void BufferedTCPSocket::write(std::string &&ss, bool buffer_only) {
	if (write_shutdown_asked || (ss.empty() && buffer_only))
		return;  // Even if ss.empty(), we must call write() if buffer_only == false
	total_buffer_size += ss.size();
	if (!data_to_write.empty() && data_to_write.size() < 1024)
		data_to_write.back().write(ss.data(), ss.size());
	else
		data_to_write.emplace_back(std::move(ss));
	if (!buffer_only)
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

CRAB_INLINE void BufferedTCPSocket::on_rw_handler() {
	write();
	r_handler();
}

CRAB_INLINE void BufferedTCPSocket::on_disconnect() {
	data_to_write.clear();
	write_shutdown_asked = false;
	total_buffer_size    = 0;
	d_handler();
}

namespace http {

CRAB_INLINE Connection::Connection()
    : read_buffer(8192)
    , sock([this]() { advance_state(true); }, std::bind(&Connection::on_disconnect, this))
    , state(SHUTDOWN) {}

CRAB_INLINE Connection::Connection(Handler &&r_handler, Handler &&d_handler)
    : read_buffer(8192)
    , r_handler(std::move(r_handler))
    , d_handler(std::move(d_handler))
    , sock([this]() { advance_state(true); }, std::bind(&Connection::on_disconnect, this))
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
}

CRAB_INLINE bool Connection::read_next(RequestBody &req) {
	if (state != REQUEST_READY)
		return false;
	req.body = http_body_parser.body.clear();
	req.r    = request_parser.req;
	// We do not move req, because we must remember params for response
	state = WAITING_WRITE_RESPONSE;
	advance_state(false);
	return true;
}

CRAB_INLINE bool Connection::read_next(ResponseBody &req) {
	if (state != RESPONSE_READY)
		return false;
	req.body = http_body_parser.body.clear();
	req.r    = std::move(response_parser.req);
	state    = WAITING_WRITE_REQUEST;
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

	ResponseBody response;

	response.r.connection_upgrade = request_parser.req.connection_upgrade;
	response.r.upgrade_websocket  = request_parser.req.upgrade_websocket;
	response.r.sec_websocket_accept =
	    ResponseHeader::generate_sec_websocket_accept(request_parser.req.sec_websocket_key);
	response.r.status = 101;

	write(std::move(response));
}

CRAB_INLINE void Connection::write(RequestBody &&req) {
	if (state == SHUTDOWN)
		return;  // This NOP simplifies state machines of connection users
	invariant(state == WAITING_WRITE_REQUEST, "Connection unexpected write");
	invariant(req.r.http_version_major, "Someone forgot to set version, method, status or url");

	invariant(!req.r.transfer_encoding_chunked, "As the whole body is sent, makes no sense");
	sock.write(req.r.to_string(), true);
	sock.write(std::move(req.body));

	if (req.r.is_websocket_upgrade()) {
		response_parser   = ResponseParser{};
		state             = WEB_UPGRADE_RESPONSE_HEADER;
		sec_websocket_key = req.r.sec_websocket_key;
	} else {
		if (req.r.keep_alive) {
			response_parser = ResponseParser{};
			state           = RESPONSE_HEADER;
		} else {
			sock.write_shutdown();
			state = SHUTDOWN;
		}
	}
}

CRAB_INLINE void Connection::write(ResponseBody &&resp) {
	if (state == SHUTDOWN)
		return;  // This NOP simplifies state machines of connection users
	invariant(state == WAITING_WRITE_RESPONSE, "Connection unexpected write");

	resp.r.http_version_major = request_parser.req.http_version_major;
	resp.r.http_version_minor = request_parser.req.http_version_minor;
	resp.r.keep_alive         = request_parser.req.keep_alive;

	invariant(!resp.r.transfer_encoding_chunked, "As the whole body is sent, makes no sense");
	sock.write(resp.r.to_string(), true);
	sock.write(std::move(resp.body));
	if (resp.r.is_websocket_upgrade()) {
		wm_header_parser = MessageChunkParser{};
		wm_body_parser   = MessageBodyParser{};
		state            = WEB_MESSAGE_HEADER;
	} else {
		if (resp.r.keep_alive) {
			request_parser = RequestParser{};
			state          = REQUEST_HEADER;
		} else {
			sock.write_shutdown();
			state = SHUTDOWN;
		}
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

	//	data_to_write.write(frame_buffer, frame_buffer_len);
	//	data_to_write.write(message.body.data(), message.body.size());

	sock.write(frame_buffer, frame_buffer_len, true);
	sock.write(std::move(message.body));
	//	if (data_to_write.back().get_buffer().size() < 1024 && message.body.size() < 1024)
	//		data_to_write.back().write(message.body.data(), message.body.size());
	//	else
	//		data_to_write.emplace_back(std::move(message.body));
	if (message.opcode == WebMessage::OPCODE_CLOSE)
		wm_close_sent = true;
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
	} catch (const std::exception &ex) {
		sock.write_shutdown();
		state = SHUTDOWN;
	}
}

CRAB_INLINE void Connection::on_disconnect() {
	close();
	d_handler();
}

}  // namespace http
}  // namespace crab
