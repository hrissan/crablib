// Copyright (c) 2007-2023, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <algorithm>
#include <iostream>
#include <sstream>
#include "crab_tls.hpp"

#if CRAB_TLS

namespace crab {

void CRAB_INLINE details::TLSInit::init_tls() { static TLSInit impl; }

CRAB_INLINE details::TLSInit::TLSInit() {
	// What the hell, why no single init function?
	SSL_library_init();
	SSL_load_error_strings();
	ERR_load_BIO_strings();
	OpenSSL_add_all_algorithms();
}

CRAB_INLINE details::TLSInit::~TLSInit() {
	// What the hell, why no single deinit function?
	//        ERR_remove_state(0);
	//        ENGINE_cleanup();
	//        CONF_modules_unload(1);
	ERR_free_strings();
	EVP_cleanup();
	//        sk_SSL_COMP_free(SSL_COMP_get_compression_methods());
	CRYPTO_cleanup_all_ex_data();
}

CRAB_INLINE details::TLSEngine::TLSEngine(const std::string &host)
    : ctx(SSL_CTX_new(TLS_client_method())), ssl(SSL_new(ctx)), bio_in(BIO_new(BIO_s_mem())), bio_out(BIO_new(BIO_s_mem())) {
	SSL_set_bio(ssl, bio_in, bio_out);

	details::add_tls_root_certificates(ctx);
	SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);
	SSL_set_tlsext_host_name(ssl, host.c_str());
	SSL_set_connect_state(ssl);
	SSL_do_handshake(ssl);
}

CRAB_INLINE details::TLSEngine::~TLSEngine() {
	SSL_free(ssl);
	ssl     = nullptr;
	bio_in  = nullptr;  // Owned by ssl
	bio_out = nullptr;  // Owned by ssl
	SSL_CTX_free(ctx);
	ctx = nullptr;
}

CRAB_INLINE void details::TLSEngine::write_to_socket(OStream *sock) {
	while (true) {
		while (outgoing_buffer.read_count() != 0) {
			auto wr = sock->write_some(outgoing_buffer.read_ptr(), outgoing_buffer.read_count());
			if (wr == 0)
				break;
			outgoing_buffer.did_read(wr);
		}
		if (outgoing_buffer.write_count() == 0)
			break;
		int avail = BIO_read(bio_out, outgoing_buffer.write_ptr(), outgoing_buffer.write_count());
		if (avail <= 0)
			break;
		outgoing_buffer.did_write(static_cast<size_t>(avail));
	}
}
CRAB_INLINE void details::TLSEngine::read_from_socket(IStream *sock) {
	while (incoming_buffer.write_count() != 0) {
		int result = SSL_read(ssl, incoming_buffer.write_ptr(), incoming_buffer.write_count());
		if (result > 0) {
			incoming_buffer.did_write(static_cast<size_t>(result));
			continue;
		}
		uint8_t buffer[65536];
		size_t rd = sock->read_some(buffer, sizeof(buffer));
		if (rd == 0)
			break;
		invariant(BIO_write(bio_in, buffer, rd) == int(rd), "BIO_write could not consume all data");
	}
}

}  // namespace crab

#endif
