// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#pragma once

#include <deque>
#include "../network.hpp"
#include "../streams.hpp"
#include "../util.hpp"
#include "response_parser.hpp"
#include "types.hpp"
#include "web_message_parser.hpp"

#if CRAB_TLS
#include <openssl/bio.h>
#include <openssl/dtls1.h>
#include <openssl/err.h>
#include <openssl/pkcs12.h>
#include <openssl/ssl.h>
#include <openssl/tls1.h>
#include <openssl/x509.h>

// This implementation is created from boost::asio branch P.P.Sh. is running.
// Unfortunately, boost::asio::ssl::rfc2818_verification(host) is not a wrapper, but large piecec of logic
// TODO - investigate if our implementation actually catches various bad certificates
// Experimental! Do not use in critical infrastructure until checked

// This implementation does not contain server side, because in practice all servers running crab
// are behind nginx or load balancer with https termination

namespace crab {
namespace details {

void add_tls_root_certificates(SSL_CTX *ctx);

class TLSInit : private Nocopy {
public:
	static void init_tls();

private:
	TLSInit();
	~TLSInit();
};

class TLSEngine : private Nocopy {
public:
	explicit TLSEngine(const std::string &host);
	~TLSEngine();

	// Encrypted socket-facing side
	void write_to_socket(OStream *sock);
	void read_from_socket(IStream *sock);

	// Unencrypted client-facing side
	size_t read_some(uint8_t *val, size_t count) { return incoming_buffer.read_some(val, count); }
	size_t write_some(const uint8_t *val, size_t count) {
		int result = SSL_write(ssl, val, count);
		return result < 0 ? 0 : static_cast<size_t>(result);
	}
	void write_shutdown() { SSL_shutdown(ssl); }

private:
	Buffer incoming_buffer{65536};
	Buffer outgoing_buffer{65536};

	SSL_CTX *ctx = nullptr;
	SSL *ssl     = nullptr;
	BIO *bio_in  = nullptr;
	BIO *bio_out = nullptr;
};

}  // namespace details

class TCPSocketTLS : public IStream, public OStream {
public:
	explicit TCPSocketTLS(Handler &&cb) : sock([&]() { on_sock(); }), rwd_handler(std::move(cb)) {}
	void set_handler(Handler &&cb) { rwd_handler = std::move(cb); }

	~TCPSocketTLS() override { close(); }
	void close() {
		sock.close();
		tls_engine.reset();
	}
	bool is_open() const { return sock.is_open(); }

	bool connect(const Address &address) {
		close();
		return sock.connect(address);
	}
	bool connect_tls(const Address &address, const std::string &host) {
		close();
		if (!sock.connect(address))
			return false;
		details::TLSInit::init_tls();
		tls_engine.reset(new details::TLSEngine(host));
		tls_engine->write_to_socket(&sock);
		return true;
	}
	void accept(TCPAcceptor &acceptor, Address *accepted_addr = nullptr) {
		// Always without encryption
		close();
		sock.accept(acceptor, accepted_addr);
	}
	size_t read_some(uint8_t *val, size_t count) override {
		if (!tls_engine)
			return sock.read_some(val, count);
		tls_engine->read_from_socket(&sock);
		return tls_engine->read_some(val, count);
	}
	using IStream::read_some;  // Version for other char types
	size_t write_some(const uint8_t *val, size_t count) override {
		if (!tls_engine)
			return sock.write_some(val, count);
		if (!sock.can_write())  // Prevent uncontrolled data collection inside BIO
			return 0;
		auto result = tls_engine->write_some(val, count);
		tls_engine->write_to_socket(&sock);  // Even if no user bytes accepted, engine will start running
		return result;
	}
	using OStream::write_some;  // Version for other char types
	bool can_write() const { return sock.can_write(); }
	void write_shutdown() {
		if (!tls_engine)
			return sock.write_shutdown();
		tls_engine->write_shutdown();
		tls_engine->write_to_socket(&sock);  // TODO - check correctness
	}

private:
	TCPSocket sock;
	Handler rwd_handler;
	std::unique_ptr<details::TLSEngine> tls_engine;

	void on_sock() {
		if (tls_engine) {
			tls_engine->read_from_socket(&sock);
			tls_engine->write_to_socket(&sock);
		}
		rwd_handler();
	}
};

}  // namespace crab

#else

namespace crab {

class TCPSocketTLS : public TCPSocket {
public:
	using TCPSocket::TCPSocket;
	bool connect_tls(const Address &address, const std::string &host) {
		throw std::runtime_error("crablib was build without TLS support");
	}
};

}  // namespace crab

#endif
