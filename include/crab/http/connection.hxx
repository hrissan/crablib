// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <algorithm>
#include <iostream>
#include <sstream>
#include "connection.hpp"

namespace crab {

namespace details {

CRAB_INLINE std::string web_message_create_close_body(const std::string &reason, uint16_t close_code) {
	std::string result;
	if (reason.empty() && close_code == http::WebMessage::CLOSE_STATUS_NO_CODE)
		return result;
	if (close_code < 1000)  // Spec is silent on what to do in this case
		close_code = 1000;
	result.reserve(2 + reason.size());
	result.push_back(static_cast<char>(close_code >> 8));
	result.push_back(static_cast<char>(close_code & 0xFF));
	result += reason;
	return result;
}

}  // namespace details

CRAB_INLINE BufferedTCPSocket::BufferedTCPSocket(Handler &&rwd_handler)
    : rwd_handler(std::move(rwd_handler)), sock([this]() { sock_handler(); }), shutdown_timer([this]() { shutdown_timer_handler(); }) {}

CRAB_INLINE void BufferedTCPSocket::close(bool with_event) {
	shutdown_timer.cancel();
	data_to_write.clear();
	total_data_to_write  = 0;
	write_shutdown_asked = false;
	sock.close(with_event);
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

CRAB_INLINE void BufferedTCPSocket::write(const uint8_t *val, size_t count, BufferOptions bo) {
	if (bo == BUFFER_ONLY)
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

CRAB_INLINE void BufferedTCPSocket::write(std::string &&ss, BufferOptions bo) {
	buffer(std::move(ss));
	if (bo != BUFFER_ONLY)
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
	if (pr != string_view{"http"} && pr != string_view{"https"})
		throw std::runtime_error{"ClientConnection unsupported protocol"};
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
	advance_state();
	return true;
}

CRAB_INLINE bool ClientConnection::read_next(WebMessage &message) {
	if (state != WEB_MESSAGE_READY)
		return false;
	message = std::move(*web_message);
	web_message.reset();
	wm_header_parser = WebMessageHeaderParser{};
	state            = WEB_MESSAGE_HEADER;
	advance_state();
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
	invariant(
	    state == WEB_MESSAGE_HEADER || state == WEB_MESSAGE_BODY || state == WEB_MESSAGE_READY || state == WEB_UPGRADE_RESPONSE_HEADER,
	    "Connection unexpected write");

	auto masking_key = RunLoop::current()->rnd.pod<uint32_t>();
	if (message.opcode == WebMessageOpcode::TEXT || message.opcode == WebMessageOpcode::BINARY) {
		// invariant(!writing_web_message_body, "Sending new message before previous one finished"); Future streaming
		// plug
	} else {
		// Control messages can be sent between frames of ordinary messages
		// https://tools.ietf.org/html/rfc6455#section-5.5
		if (message.opcode == WebMessageOpcode::CLOSE)
			message.body = details::web_message_create_close_body(message.body, message.close_code);
		// We truncate body instead of throw, because it can be generated from (for example) exception text. It would
		// be unwise to expect users to get so deep into procotol details
		if (message.body.size() > 125)
			message.body.resize(125);
	}
	WebMessageHeaderSaver header{true, static_cast<int>(message.opcode), message.body.size(), masking_key};
	WebMessageHeaderParser::mask_data(0, &message.body[0], message.body.size(), masking_key);

	sock.buffer(header.data(), header.size());
	sock.write(std::move(message.body));
	if (message.opcode == WebMessageOpcode::CLOSE) {
		// We will not attempt to read response. Client logic should not depend on it.
		read_buffer.clear();
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
	RunLoop::current()->rnd.bytes(rdata, sizeof(rdata));
	req.header.sec_websocket_key = base64::encode(rdata, sizeof(rdata));

	write(std::move(req));
}

CRAB_INLINE void ClientConnection::dns_handler(const std::vector<Address> &names) {
	if (names.empty()) {
		close();
		rwd_handler();
		return;
	}
	peer_address = names[RunLoop::current()->rnd.pod<size_t>() % names.size()];  // non-zero chance with even single server up
	if (protocol == string_view{"http"}) {
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
	if (advance_state())
		rwd_handler();
}

CRAB_INLINE bool ClientConnection::advance_state() {
	// do not process new request if data waiting to be sent
	if (sock.get_total_buffer_size() != 0)
		return false;
	try {
		while (true) {
			if (read_buffer.empty() && read_buffer.read_from(sock) == 0)
				return false;
			switch (state) {
			case RESPONSE_HEADER:
				response_parser.parse(read_buffer);
				if (!response_parser.is_good())
					continue;
				if (response_parser.req.is_websocket_upgrade())
					throw std::runtime_error{"Unexpected web upgrade header"};
				http_body_parser = BodyParser{response_parser.req.content_length, response_parser.req.transfer_encoding_chunked};
				state            = RESPONSE_BODY;
				// Fall through (to correctly handle zero-length body). Next line is understood by GCC
				// Fall through
			case RESPONSE_BODY:
				http_body_parser.parse(read_buffer);
				if (!http_body_parser.is_good())
					continue;
				state = RESPONSE_READY;
				return true;
			case WEB_UPGRADE_RESPONSE_HEADER:
				response_parser.parse(read_buffer);
				if (!response_parser.is_good())
					continue;
				if (!response_parser.req.is_websocket_upgrade())
					throw std::runtime_error{"Expecting web upgrade header"};
				if (response_parser.req.content_length || response_parser.req.transfer_encoding_chunked)
					throw std::runtime_error{"Web upgrade reponse cannot have body"};
				if (response_parser.req.sec_websocket_accept != ResponseHeader::generate_sec_websocket_accept(sec_websocket_key))
					throw std::runtime_error{"Wrong value of 'Sec-WebSocket-Accept' header"};
				wm_header_parser = WebMessageHeaderParser{};
				wm_body_parser   = WebMessageBodyParser{};
				state            = WEB_MESSAGE_HEADER;
				continue;
			case WEB_MESSAGE_HEADER:
				wm_header_parser.parse(read_buffer);
				if (!wm_header_parser.is_good())
					continue;
				wm_body_parser = WebMessageBodyParser{wm_header_parser.payload_len, wm_header_parser.masking_key};
				state          = WEB_MESSAGE_BODY;
				// Fall through (to correctly handle zero-length body). Next line is understood by GCC
				// Fall through
			case WEB_MESSAGE_BODY:
				wm_body_parser.parse(read_buffer);
				if (!wm_body_parser.is_good())
					continue;
				// Control frames are allowed between message fragments
				if (wm_header_parser.opcode == static_cast<int>(WebMessageOpcode::PING)) {
					WebMessage nop(WebMessageOpcode::PONG, wm_body_parser.body.clear());
					wm_header_parser = WebMessageHeaderParser{};
					state            = WEB_MESSAGE_HEADER;
					write(std::move(nop));
					continue;
				}
				if (wm_header_parser.opcode == static_cast<int>(WebMessageOpcode::PONG)) {
					wm_header_parser = WebMessageHeaderParser{};
					state            = WEB_MESSAGE_HEADER;
					continue;
				}
				if (wm_header_parser.opcode == static_cast<int>(WebMessageOpcode::CLOSE)) {
					auto body = wm_body_parser.body.clear();
					web_message.emplace(WebMessageOpcode::CLOSE);
					if (body.size() >= 2) {
						web_message->close_code = (uint8_cast(body.data())[0] << 8) + uint8_cast(body.data())[1];
						web_message->body       = body.substr(2);
						if (!is_valid_utf8(web_message->body))
							web_message->body.clear();  // No way to tell user about such an error
					}
					state = WEB_MESSAGE_READY;
					return true;
				}
				if (!web_message) {
					if (wm_header_parser.opcode == 0)
						throw std::runtime_error{"Continuation in the first chunk"};
					web_message.emplace(static_cast<WebMessageOpcode>(wm_header_parser.opcode), wm_body_parser.body.clear());
				} else {
					if (wm_header_parser.opcode != 0)
						throw std::runtime_error{"Non-continuation in the subsequent chunk"};
					web_message->body += wm_body_parser.body.clear();
				}
				if (!wm_header_parser.fin) {
					wm_header_parser = WebMessageHeaderParser{};
					state            = WEB_MESSAGE_HEADER;
					continue;
				}
				if (web_message->is_text() && !is_valid_utf8(web_message->body)) {
					web_message.reset();
					wm_header_parser = WebMessageHeaderParser{};
					state            = WEB_MESSAGE_HEADER;
					WebMessage nop(WebMessageOpcode::CLOSE, {}, WebMessage::CLOSE_STATUS_NOT_UTF8);
					write(std::move(nop));
					return false;
				}
				state = WEB_MESSAGE_READY;
				return true;
			default:  // waiting write, closing, etc
				return false;
			}
		}
	} catch (const std::exception &) {
		read_buffer.clear();
		sock.write_shutdown();
		return true;
	}
}

CRAB_INLINE ServerConnection::ServerConnection(Handler &&rwd_handler)
    : read_buffer(8192)
    , wm_ping_timer([&]() { on_wm_ping_timer(); })
    , rwd_handler(std::move(rwd_handler))
    , sock([this]() { sock_handler(); }) {}

CRAB_INLINE void ServerConnection::accept(TCPAcceptor &acceptor) {
	close();
	sock.accept(acceptor, &peer_address);
	request_parser = RequestParser{};
	state          = REQUEST_HEADER;
}

CRAB_INLINE void ServerConnection::close() {
	read_buffer.clear();
	wm_ping_timer.cancel();
	sock.close();
	state                    = REQUEST_HEADER;
	writing_web_message_body = false;
	peer_address             = Address();
}

CRAB_INLINE bool ServerConnection::read_next(Request &req) {
	if (state != REQUEST_READY)
		return false;
	req.body   = http_body_parser.body.clear();
	req.header = std::move(request_parser.req);
	// We move req, but remember params for response. Hopefully compiler will optimize some assignments
	request_parser.req.method                = req.header.method;
	request_parser.req.http_version_major    = req.header.http_version_major;
	request_parser.req.http_version_minor    = req.header.http_version_minor;
	request_parser.req.connection_upgrade    = req.header.connection_upgrade;
	request_parser.req.keep_alive            = req.header.keep_alive;
	request_parser.req.sec_websocket_key     = req.header.sec_websocket_key;
	request_parser.req.sec_websocket_version = req.header.sec_websocket_version;
	request_parser.req.upgrade_websocket     = req.header.upgrade_websocket;
	state                                    = RESPONSE_HEADER;
	advance_state();
	return true;
}

CRAB_INLINE bool ServerConnection::read_next(WebMessage &message) {
	if (state != WEB_MESSAGE_READY || writing_web_message_body)
		return false;  // We can start streaming message at any state, but must not allow reading until finished
	message = std::move(*web_message);
	web_message.reset();
	wm_header_parser = WebMessageHeaderParser{};
	state            = WEB_MESSAGE_HEADER;
	advance_state();
	return true;
}

CRAB_INLINE void ServerConnection::web_socket_upgrade() {
	if (!is_open())
		return;  // This NOP simplifies state machines of connection users
	invariant(state == RESPONSE_HEADER, "Connection unexpected write");
	if (!request_parser.req.is_websocket_upgrade())
		throw std::runtime_error{"Attempt to upgrade non-upgradable connection"};

	ResponseHeader response;  // HTTP/1.1, keep-alive

	response.connection_upgrade   = true;
	response.upgrade_websocket    = true;
	response.sec_websocket_accept = ResponseHeader::generate_sec_websocket_accept(request_parser.req.sec_websocket_key);
	response.status               = 101;

	sock.write(response.to_string());

	wm_header_parser = WebMessageHeaderParser{};
	wm_body_parser   = WebMessageBodyParser{};
	state            = WEB_MESSAGE_HEADER;
	wm_ping_timer.once(WM_PING_TIMEOUT_SEC);  // Always server-side
}

CRAB_INLINE void ServerConnection::write(Response &&resp) {
	if (!is_open())
		return;  // This NOP simplifies state machines of connection users
	const bool transfer_encoding_chunked = resp.header.transfer_encoding_chunked;
	write(resp.header, BUFFER_ONLY);
	write(std::move(resp.body), transfer_encoding_chunked ? BUFFER_ONLY : WRITE);
	if (transfer_encoding_chunked)
		write_last_chunk();  // Otherwise, state is already switched into RECEIVE_HEADER
		                     // shutdown is already written during switch to RECEIVE_HEADER
}

CRAB_INLINE void ServerConnection::write(ResponseHeader &resp, BufferOptions bo) {
	// TODO - if this fun is used instead of
	if (!is_open())
		return;  // This NOP simplifies state machines of connection users
	invariant(state == RESPONSE_HEADER, "Connection unexpected write");
	invariant(!resp.is_websocket_upgrade(), "Please use web_socket_upgrade() function for web socket upgrade");
	invariant(resp.transfer_encoding_chunked || resp.content_length, "Please set either chunked encoding or content_length");

	resp.http_version_major = request_parser.req.http_version_major;
	resp.http_version_minor = request_parser.req.http_version_minor;
	resp.keep_alive         = request_parser.req.keep_alive;

	remaining_body_content_length = resp.content_length;
	sock.write(resp.to_string(), bo);
	state = RESPONSE_BODY;
}

CRAB_INLINE void ServerConnection::write(WebMessage &&message, BufferOptions bo) {
	if (!is_open())
		return;  // This NOP simplifies state machines of connection users
	invariant(is_state_websocket(), "Connection unexpected write");

	if (message.opcode == WebMessageOpcode::TEXT || message.opcode == WebMessageOpcode::BINARY) {
		invariant(!writing_web_message_body, "Sending new message before previous one finished");
	} else {
		// Control messages can be sent between frames of ordinary messages
		// https://tools.ietf.org/html/rfc6455#section-5.5
		if (message.opcode == WebMessageOpcode::CLOSE)
			message.body = details::web_message_create_close_body(message.body, message.close_code);
		// We truncate body instead of throw, because it can be generated from (for example) exception text. It would
		// be unwise to expect users to get so deep into procotol details
		if (message.body.size() > 125)
			message.body.resize(125);
	}
	WebMessageHeaderSaver header{true, static_cast<int>(message.opcode), message.body.size(), {}};
	sock.buffer(header.data(), header.size());
	sock.write(std::move(message.body), bo);
	if (message.opcode == WebMessageOpcode::CLOSE) {
		wm_ping_timer.cancel();
		read_buffer.clear();
		sock.write_shutdown();
		return;
	}
	if (bo == BufferOptions::WRITE)
		wm_ping_timer.once(WM_PING_TIMEOUT_SEC);
}

CRAB_INLINE void ServerConnection::write(WebMessageOpcode opcode) {
	if (!is_open())
		return;  // This NOP simplifies state machines of connection users
	invariant(is_state_websocket(), "Connection unexpected write");
	invariant(opcode == WebMessageOpcode::TEXT || opcode == WebMessageOpcode::BINARY, "Control frames must not be fragmented");
	invariant(!writing_web_message_body, "Sending new message before previous one finished");
	writing_web_message_body = true;

	WebMessageHeaderSaver header{false, static_cast<int>(opcode), 0, {}};
	// Server-side uses no masking key
	// We will write 0-length !FIN frame immediately, then
	// write single !FIN frame per write call, then write single 0-lenght FIN frame in write_last_chunk()
	sock.buffer(header.data(), header.size());
}

CRAB_INLINE void ServerConnection::write(const uint8_t *val, size_t count, BufferOptions bo) {
	invariant(is_writing_body(), "Connection unexpected write");
	if (writing_web_message_body) {
		if (count == 0)
			return;
		WebMessageHeaderSaver header{false, 0, count, {}};
		sock.buffer(header.data(), header.size());
		sock.write(val, count, bo);
		if (bo == BufferOptions::WRITE)
			wm_ping_timer.once(WM_PING_TIMEOUT_SEC);
		return;
	}
	if (remaining_body_content_length) {
		invariant(count <= *remaining_body_content_length, "Overshoot content-length");
		*remaining_body_content_length -= count;
		sock.write(val, count, bo);
		if (*remaining_body_content_length == 0) {
			if (!request_parser.req.keep_alive) {  // We sent it in our response header
				read_buffer.clear();
				sock.write_shutdown();
			}
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
	sock.write("\r\n", 2, bo);
}

CRAB_INLINE void ServerConnection::write(std::string &&ss, BufferOptions bo) {
	invariant(is_writing_body(), "Connection unexpected write");
	if (writing_web_message_body) {
		if (ss.size() == 0)
			return;
		WebMessageHeaderSaver header{false, 0, ss.size(), {}};
		sock.buffer(header.data(), header.size());
		sock.write(std::move(ss), bo);
		if (bo == BufferOptions::WRITE)
			wm_ping_timer.once(WM_PING_TIMEOUT_SEC);
		return;
	}
	if (remaining_body_content_length) {
		invariant(ss.size() <= *remaining_body_content_length, "Overshoot content-length");
		*remaining_body_content_length -= ss.size();
		sock.write(std::move(ss), bo);
		if (*remaining_body_content_length == 0) {
			if (!request_parser.req.keep_alive) {  // We sent it in our response header
				read_buffer.clear();
				sock.write_shutdown();
			}
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
	sock.write("\r\n", 2, bo);
}

CRAB_INLINE void ServerConnection::write_last_chunk(BufferOptions bo) {
	invariant(is_writing_body(), "Connection unexpected write");
	if (writing_web_message_body) {
		WebMessageHeaderSaver header{true, 0, 0, {}};
		sock.write(header.data(), header.size(), bo);
		if (bo == BufferOptions::WRITE)
			wm_ping_timer.once(WM_PING_TIMEOUT_SEC);
		writing_web_message_body = false;
		return;
	}
	invariant(state == RESPONSE_BODY, "Connection unexpected write");
	invariant(!remaining_body_content_length, "write_last_chunk is for chunked encoding only");
	sock.write(std::string{"0\r\n\r\n"}, bo);
	if (!request_parser.req.keep_alive) {  // We sent it in our response header
		read_buffer.clear();
		sock.write_shutdown();
	}
	request_parser = RequestParser{};
	state          = REQUEST_HEADER;
}

CRAB_INLINE void ServerConnection::sock_handler() {
	if (!sock.is_open()) {
		close();
		rwd_handler();
		return;
	}
	if (is_writing_body()) {
		rwd_handler();  // So body streaming will work
		// This follows usual async pull socket pattern, when after finishing writing body client will
		// call read_next, which will call advance_state, and so on
		return;
	}
	if (advance_state())
		rwd_handler();
}

CRAB_INLINE void ServerConnection::on_wm_ping_timer() {
	if (sock.get_total_buffer_size() != 0)
		sock.write(std::string{});  // Flushing buffer is enough
	else
		write(WebMessage{WebMessageOpcode::PING, std::string{}});
	wm_ping_timer.once(WM_PING_TIMEOUT_SEC);
}

CRAB_INLINE bool ServerConnection::advance_state() {
	// do not process new request if data waiting to be sent
	if (sock.get_total_buffer_size() != 0)
		return false;
	try {
		while (true) {
			if (read_buffer.empty() && read_buffer.read_from(sock) == 0)
				return false;
			switch (state) {
			case REQUEST_HEADER:
				request_parser.parse(read_buffer);
				if (!request_parser.is_good())
					continue;
				http_body_parser = BodyParser{request_parser.req.content_length, request_parser.req.transfer_encoding_chunked};
				request_parser.req.transfer_encoding_chunked = false;  // Hide from clients
				state                                        = REQUEST_BODY;
				// Fall through (to correctly handle zero-length body). Next line is understood by GCC
				// Fall through
			case REQUEST_BODY:
				http_body_parser.parse(read_buffer);
				if (!http_body_parser.is_good())
					continue;
				state = REQUEST_READY;
				return true;
			case WEB_MESSAGE_HEADER:
				wm_header_parser.parse(read_buffer);
				if (!wm_header_parser.is_good())
					continue;
				wm_body_parser = WebMessageBodyParser{wm_header_parser.payload_len, wm_header_parser.masking_key};
				state          = WEB_MESSAGE_BODY;
				// Fall through (to correctly handle zero-length body). Next line is understood by GCC
				// Fall through
			case WEB_MESSAGE_BODY:
				wm_body_parser.parse(read_buffer);
				if (!wm_body_parser.is_good())
					continue;
				// Control frames are allowed between message fragments
				if (wm_header_parser.opcode == static_cast<int>(WebMessageOpcode::PING)) {
					WebMessage nop(WebMessageOpcode::PONG, wm_body_parser.body.clear());
					wm_header_parser = WebMessageHeaderParser{};
					state            = WEB_MESSAGE_HEADER;
					write(std::move(nop));
					continue;
				}
				if (wm_header_parser.opcode == static_cast<int>(WebMessageOpcode::PONG)) {
					wm_header_parser = WebMessageHeaderParser{};
					state            = WEB_MESSAGE_HEADER;
					continue;
				}
				if (wm_header_parser.opcode == static_cast<int>(WebMessageOpcode::CLOSE)) {
					auto body = wm_body_parser.body.clear();
					web_message.emplace(WebMessageOpcode::CLOSE);
					if (body.size() >= 2) {
						web_message->close_code = (uint8_cast(body.data())[0] << 8) + uint8_cast(body.data())[1];
						web_message->body       = body.substr(2);
						if (!is_valid_utf8(web_message->body))
							web_message->body.clear();  // No way to tell user about such an error
					}
					state = WEB_MESSAGE_READY;
					return true;
				}
				if (!web_message) {
					if (wm_header_parser.opcode == 0)
						throw std::runtime_error{"Continuation in the first chunk"};
					web_message.emplace(static_cast<WebMessageOpcode>(wm_header_parser.opcode), wm_body_parser.body.clear());
				} else {
					if (wm_header_parser.opcode != 0)
						throw std::runtime_error{"Non-continuation in the subsequent chunk"};
					web_message->body += wm_body_parser.body.clear();
				}
				if (!wm_header_parser.fin) {
					wm_header_parser = WebMessageHeaderParser{};
					state            = WEB_MESSAGE_HEADER;
					continue;
				}
				if (web_message->is_text() && !is_valid_utf8(web_message->body)) {
					web_message.reset();
					wm_header_parser = WebMessageHeaderParser{};
					state            = WEB_MESSAGE_HEADER;
					WebMessage nop(WebMessageOpcode::CLOSE, {}, WebMessage::CLOSE_STATUS_NOT_UTF8);
					write(std::move(nop));
					return false;
				}
				state = WEB_MESSAGE_READY;
				return true;
			default:  // waiting write, closing, etc
				return false;
			}
		}
	} catch (const std::exception &) {
		wm_ping_timer.cancel();
		read_buffer.clear();
		sock.write_shutdown();
		return true;
	}
}

}  // namespace http
}  // namespace crab
