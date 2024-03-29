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

// Copyright (c) 2007-2023, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <assert.h>
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

void print_params(const std::unordered_map<std::string, std::string> &params, std::string name) {
	std::cout << name << ":\n";
	for (auto q : params) {
		std::cout << "'" << q.first << "' => '" << q.second << "'\n";
	}
	std::cout << "-----\n\n";
}

void test_query_parser() {
	auto p0  = http::parse_query_string("simple=test&oh=mygod&it=works");
	auto p1  = http::parse_query_string("simple=&=mygod");
	auto p2  = http::parse_query_string("test=mega=giga&=&&&");
	auto p3  = http::parse_query_string("x=y&x=z&вася=ма%5ша&коля=ник%41а&%1%1%1%");
	auto p4  = http::parse_query_string("hren&mega");
	auto p5  = http::parse_query_string("Fran%C3%A7ois=%D1%82%D0%B5%D1%81%D1%82+123+%D0%BD%D0%B0%D1%84%D0%B8%D0%B3");
	auto p6  = http::parse_query_string("end_on_%=bruh%");
	auto p7  = http::parse_query_string("end_on_%f=bruh%a");
	auto p8  = http::parse_query_string("end_on_%fz=bruh%az&valid%41=ok%41");
	auto p9  = http::parse_query_string("end_on_%");
	auto p10 = http::parse_query_string("end_on_%f");
	auto p11 = http::parse_query_string("end_on_%41");

	assert(p0.count("simple"));
	assert(!p0.count("session"));
	assert(p1.count("simple"));
	assert(p1.count(""));

	assert(p0.at("oh") == "mygod");
	assert(p1.at("simple") == "");
	assert(p1.at("") == "mygod");

	// check access via index operator
	assert(p0["simple"] == "test");

	print_params(p0, "query p0");
	print_params(p1, "query p1");
	print_params(p2, "query p2");
	print_params(p3, "query p3");
	print_params(p4, "query p4");
	print_params(p5, "query p5");
	print_params(p6, "query p6");
	print_params(p7, "query p7");
	print_params(p8, "query p8");
	print_params(p9, "query p9");
	print_params(p10, "query p10");
	print_params(p11, "query p11");
}

void test_cookie_parser() {
	auto p0 = http::parse_cookie_string("simple=test;oh=my=god;it=works");
	auto p1 = http::parse_cookie_string("_session=lqJlEC9ypWiEX3OB;another=value;=");
	auto p2 = http::parse_cookie_string("  _session  =  lqJlEC9ypWiEX3OB  ; another = value  ;keyonly =  ;=valueonly");
	auto p3 = http::parse_cookie_string(" _se$$ss1 n = lqJlEC, 9y,pWi , EX3OB ; another = v=a,l! #$ue  ;hren,123; last key with spaces ");
	auto p4 = http::parse_cookie_string(" test =  last value with spaces   ");

	assert(p1.count("_session"));
	assert(p2.count("_session"));
	assert(p3.count("_se$$ss1 n"));

	assert(p2.count("keyonly"));
	assert(p2.count(""));

	assert(p1.count("another"));
	assert(p2.count("another"));
	assert(p3.count("another"));

	assert(p1.at("_session") == "lqJlEC9ypWiEX3OB");
	assert(p2.at("_session") == "lqJlEC9ypWiEX3OB");
	assert(p3.at("_se$$ss1 n") == "lqJlEC, 9y,pWi , EX3OB");
	assert(p3.at("another") == "v=a,l! #$ue");

	assert(p1.at("") == "");
	assert(p2.at("keyonly") == "");
	assert(p2.at("") == "valueonly");

	// NOTE: despite what the rfc says, we store a standalone value as a key instead
	// this is so that multiple standalone values can actually be still recovered from the map.
	assert(p3.at("hren,123") == "");
	assert(p3.at("last key with spaces") == "");

	// check access via index operator
	assert(p1["_session"] == "lqJlEC9ypWiEX3OB");
	assert(p3["_se$$ss1 n"] == "lqJlEC, 9y,pWi , EX3OB");

	assert(p4["test"] == "last value with spaces");

	print_params(p0, "cookies p0");
	print_params(p1, "cookies p1");
	print_params(p2, "cookies p2");
	print_params(p3, "cookies p3");
	print_params(p4, "cookies p4");
}

static void test_uri(std::string uri_str, std::string scheme, std::string user_info, std::string host, std::string port, std::string path,
    std::string query = "") {
	crab::http::URI uri = crab::http::parse_uri(uri_str);
	invariant(uri.scheme == scheme && uri.user_info == user_info && uri.host == host && uri.port == port && uri.path == path &&
	              uri.query == query,
	    "");
	std::cout << "<-- " << uri_str << std::endl;
	std::cout << "--> " << uri.to_string() << std::endl;
	crab::http::URI uri2 = crab::http::parse_uri(uri.to_string());
	invariant(uri2.scheme == scheme && uri2.user_info == user_info && uri2.host == host && uri2.port == port && uri2.path == path &&
	              uri2.query == query,
	    "");
}

static void test_bad_uri(std::string uri_str) {
	try {
		crab::http::parse_uri(uri_str);
	} catch (const std::runtime_error &) {
		std::cout << "bad " << uri_str << std::endl;
		return;
	}
	throw std::logic_error("Bad uri parsed successfully");
}

void test_uri_parser() {
	test_uri("http://crab.com/", "http", "", "crab.com", "", "/");
	test_uri("http://crab.com/chat", "http", "", "crab.com", "", "/chat");
	test_bad_uri("https://getschwifty.ltd/.././../hello");
	test_uri("https://getschwifty.ltd/mega/giga/../hello/test/../ok", "https", "", "getschwifty.ltd", "", "/mega/hello/ok");
	test_bad_uri("");
	test_uri("http://getschwifty.ltd:8080/test?Fran%C3%A7ois=%D1%82%D0%B5%D1%81%D1%82+123+%D0%BD%D0%B0%D1%84%D0%B8%D0%B3", "http", "",
	    "getschwifty.ltd", "8080", "/test", "Fran%C3%A7ois=%D1%82%D0%B5%D1%81%D1%82+123+%D0%BD%D0%B0%D1%84%D0%B8%D0%B3");
	test_uri("https://192.168.0.1/Fran%C3%A7ois/%D1%82%D0%B5%D1%81%D1%82%1", "https", "", "192.168.0.1", "", "/François/тест%1");
	test_uri("https://192.168.0.1:8080/%hello%/world?mega=123", "https", "", "192.168.0.1", "8080", "/%hello%/world", "mega=123");
	test_uri("https://test.com:8090", "https", "", "test.com", "8090", "/");
	test_uri("https://https://Fran%C3%A7ois:%D1%82%D0%B5%D1%81%D1%82+123+%D0%BD%D0%B0%D1%84%D0%B8%D0%B3@192.168.0.1:8090", "https", "",
	    "https", "", "//François:тест+123+нафиг@192.168.0.1:8090");
	test_uri("https://https:Fran%C3%A7ois:%D1%82%D0%B5%D1%81%D1%82+123+%D0%BD%D0%B0%D1%84%D0%B8%D0%B3@192.168.0.1:8090", "https",
	    "https:François:тест+123+нафиг", "192.168.0.1", "8090", "/");
}

void message_eq(const Message &msg, const http::ResponseHeader &req, const std::string &body) {
	invariant(msg.status_code == req.status, "");
	invariant(msg.body == body, "");
	invariant(msg.should_keep_alive == int(req.keep_alive), "");
}

int main() {
	test_uri_parser();
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
		auto pos2 = bp.parse(crab::uint8_cast(pos), crab::uint8_cast(end));
		if (!bp.is_good())
			throw std::logic_error("Body failed to parse");
		message_eq(msg, req.req, bp.body.get_buffer());
		if (pos2 - crab::uint8_cast(pos) == 0)
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
		auto pos2 = bp.parse(crab::uint8_cast(pos), crab::uint8_cast(end));
		if (!bp.is_good())
			throw std::logic_error("Body failed to parse");
		message_eq(msg, req.req, bp.body.get_buffer());
		if (pos2 - crab::uint8_cast(pos) == 0)
			continue;
		// Code used to create initial corpus for fuzzing
		//		std::ofstream fbody("resp_body" + std::to_string(request_count) + ".txt");
		//		char corpus_header[3]{ req.req.transfer_encoding_chunked, char((req.req.content_length >> 8) & 0xffU),
		// char(req.req.content_length & 0xffU)}; 		fbody.write(corpus_header, 3); 		fbody.write(pos, pos2 -
		// (const uint8_t
		//*)pos);
	}
	test_query_parser();
	test_cookie_parser();
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

	auto start    = std::chrono::steady_clock::now();
	size_t result = 0;
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
	std::cout << total / (1024 * 1024) << " mb | " << bw / (1024 * 1024) << " mb/s | " << iterations / elapsed << " req/sec | " << elapsed
	          << " s" << std::endl;
	return static_cast<int>(result);
}
