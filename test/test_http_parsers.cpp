/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <fstream>
#include <iostream>

#include <crab/crab.hpp>
extern "C" {
#include "test_http_data.h"
}

namespace http = crab::http;

void message_eq(const Message &msg, const http::RequestHeader &req, const std::string &body) {
	invariant(msg.method == req.method, "");
	invariant(msg.body == body, "");
	invariant(msg.request_path == req.path, "");
	invariant(msg.query_string == req.query_string, "");
	invariant(msg.should_keep_alive == int(req.keep_alive), "");

	//	std::vector<Header> headers;
	//	std::string basic_authorization;
	//	std::string host;
	//	std::string origin;

	//	transfer_encodings;

	//	std::string content_type;

	//	bool connection_upgrade = false;
	//	bool upgrade_websocket  = false;  // Upgrade: WebSocket
	//	std::string sec_websocket_key;
	//	std::string sec_websocket_version;
}

void message_eq(const Message &msg, const http::ResponseHeader &req, const std::string &body) {
	invariant(msg.status_code == req.status, "");
	invariant(msg.body == body, "");
	invariant(msg.should_keep_alive == int(req.keep_alive), "");
}

int main() {
	for (size_t request_count = 0; requests[request_count].name; request_count++) {
		auto msg = requests[request_count];
		http::RequestParser req;
		auto end = msg.raw + strlen(msg.raw);
		auto pos = req.parse(msg.raw, end);
		if (!req.is_good())
			throw std::logic_error("Header failed to parse");
		// Code used to create initial corpus for fuzzing
		//		std::ofstream freq("req" + std::to_string(request_count) + ".txt");
		//		freq.write(msg.raw, pos - msg.raw);
		http::BodyParser bp{req.req.content_length, req.req.transfer_encoding_chunked};
		auto pos2 = bp.parse(reinterpret_cast<const uint8_t *>(pos), reinterpret_cast<const uint8_t *>(end));
		if (!bp.is_good())
			throw std::logic_error("Body failed to parse");
		message_eq(msg, req.req, bp.body.get_buffer());
		if (pos2 - reinterpret_cast<const uint8_t *>(pos) == 0)
			continue;
		// Code used to create initial corpus for fuzzing
		//		std::ofstream fbody("req_body" + std::to_string(request_count) + ".txt");
		//		char corpus_header[3]{ req.req.transfer_encoding_chunked, char((req.req.content_length >> 8) & 0xffU),
		// char(req.req.content_length & 0xffU)}; 		fbody.write(corpus_header, 3); 		fbody.write(pos, pos2 -
		// (const uint8_t
		//*)pos);
	}
	for (size_t request_count = 0; responses[request_count].name; request_count++) {
		auto msg = responses[request_count];
		http::ResponseParser req;
		auto end = msg.raw + strlen(msg.raw);
		auto pos = req.parse(msg.raw, end);
		if (!req.is_good())
			throw std::logic_error("Header failed to parse");
		// Code used to create initial corpus for fuzzing
		//		std::ofstream freq("resp" + std::to_string(request_count) + ".txt");
		//		freq.write(msg.raw, pos - msg.raw);
		http::BodyParser bp{req.req.content_length, req.req.transfer_encoding_chunked};
		auto pos2 = bp.parse(reinterpret_cast<const uint8_t *>(pos), reinterpret_cast<const uint8_t *>(end));
		if (!bp.is_good())
			throw std::logic_error("Body failed to parse");
		message_eq(msg, req.req, bp.body.get_buffer());
		if (pos2 - reinterpret_cast<const uint8_t *>(pos) == 0)
			continue;
		// Code used to create initial corpus for fuzzing
		//		std::ofstream fbody("resp_body" + std::to_string(request_count) + ".txt");
		//		char corpus_header[3]{ req.req.transfer_encoding_chunked, char((req.req.content_length >> 8) & 0xffU),
		// char(req.req.content_length & 0xffU)}; 		fbody.write(corpus_header, 3); 		fbody.write(pos, pos2 -
		// (const uint8_t
		//*)pos);
	}
	return 0;
}

/* 1 gb */
static const int64_t kBytes = 1LL << 30;

static const char data[] =
    "POST /joyent/http-parser HTTP/1.1\r\n"
    "Host: github.com\r\n"
    "DNT: 1\r\n"
    "Accept-Encoding: gzip, deflate, sdch\r\n"
    "Accept-Language: ru-RU,ru;q=0.8,en-US;q=0.6,en;q=0.4\r\n"
    "User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_10_1) "
    "AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/39.0.2171.65 Safari/537.36\r\n"
    "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,"
    "image/webp,*/*;q=0.8\r\n"
    "Referer: https://github.com/joyent/http-parser\r\n"
    "Connection: keep-alive\r\n"
    "Transfer-Encoding: chunked\r\n"
    "Cache-Control: max-age=0\r\n\r\nb\r\nhello world\r\n0\r\n";

static const size_t data_len = sizeof(data) - 1;

const size_t iterations = kBytes / int64_t(data_len);

int main2() {
	std::cout << "Running benchmark..." << std::endl;

	auto start = std::chrono::steady_clock::now();
	int result = 0;
	http::RequestParser req;
	for (size_t i = 0; i < iterations; i++) {
		req = http::RequestParser{};

		auto end = req.parse(data, data + data_len);
		result += end - data;
	}
	auto now = std::chrono::steady_clock::now();
	std::cout << req.req.to_string() << std::endl;

	double elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() / 1000.0;
	double total   = double(iterations) * data_len;
	double bw      = total / elapsed;

	std::cout << "---" << std::endl << "Benchmark result:" << std::endl;
	std::cout << total / (1024 * 1024) << " mb | " << bw / (1024 * 1024) << " mb/s | " << iterations / elapsed
	          << " req/sec | " << elapsed << " s" << std::endl;
	return result;
}
