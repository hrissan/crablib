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

namespace http = crab::http;

// Defintions from https://github.com/nodejs/http-parser/blob/master/http_parser.h

#define HTTP_METHOD_MAP(XX)          \
	XX(0, DELETE, DELETE)            \
	XX(1, GET, GET)                  \
	XX(2, HEAD, HEAD)                \
	XX(3, POST, POST)                \
	XX(4, PUT, PUT)                  \
	/* pathological */               \
	XX(5, CONNECT, CONNECT)          \
	XX(6, OPTIONS, OPTIONS)          \
	XX(7, TRACE, TRACE)              \
	/* WebDAV */                     \
	XX(8, COPY, COPY)                \
	XX(9, LOCK, LOCK)                \
	XX(10, MKCOL, MKCOL)             \
	XX(11, MOVE, MOVE)               \
	XX(12, PROPFIND, PROPFIND)       \
	XX(13, PROPPATCH, PROPPATCH)     \
	XX(14, SEARCH, SEARCH)           \
	XX(15, UNLOCK, UNLOCK)           \
	XX(16, BIND, BIND)               \
	XX(17, REBIND, REBIND)           \
	XX(18, UNBIND, UNBIND)           \
	XX(19, ACL, ACL)                 \
	/* subversion */                 \
	XX(20, REPORT, REPORT)           \
	XX(21, MKACTIVITY, MKACTIVITY)   \
	XX(22, CHECKOUT, CHECKOUT)       \
	XX(23, MERGE, MERGE)             \
	/* upnp */                       \
	XX(24, MSEARCH, M - SEARCH)      \
	XX(25, NOTIFY, NOTIFY)           \
	XX(26, SUBSCRIBE, SUBSCRIBE)     \
	XX(27, UNSUBSCRIBE, UNSUBSCRIBE) \
	/* RFC-5789 */                   \
	XX(28, PATCH, PATCH)             \
	XX(29, PURGE, PURGE)             \
	/* CalDAV */                     \
	XX(30, MKCALENDAR, MKCALENDAR)   \
	/* RFC-2068, section 19.6.1.2 */ \
	XX(31, LINK, LINK)               \
	XX(32, UNLINK, UNLINK)           \
	/* icecast */                    \
	XX(33, SOURCE, SOURCE)

enum http_method {
#define XX(num, name, string) HTTP_##name = num,
	HTTP_METHOD_MAP(XX)
#undef XX
};

enum http_parser_type { HTTP_REQUEST, HTTP_RESPONSE };

enum flags {
	F_CHUNKED               = 1 << 0,
	F_CONNECTION_KEEP_ALIVE = 1 << 1,
	F_CONNECTION_CLOSE      = 1 << 2,
	F_CONNECTION_UPGRADE    = 1 << 3,
	F_TRAILING              = 1 << 4,
	F_UPGRADE               = 1 << 5,
	F_SKIPBODY              = 1 << 6,
	F_CONTENTLENGTH         = 1 << 7,
	F_TRANSFER_ENCODING     = 1 << 8
};

#define MAX_HEADERS 10
#define MAX_ELEMENT_SIZE 500

struct Message {
	const char *name;  // for debugging purposes
	const char *raw;
	enum http_parser_type type;
	char method[MAX_ELEMENT_SIZE];
	int status_code;
	char request_path[MAX_ELEMENT_SIZE];
	char request_uri[MAX_ELEMENT_SIZE];
	char fragment[MAX_ELEMENT_SIZE];
	char query_string[MAX_ELEMENT_SIZE];
	char body[MAX_ELEMENT_SIZE];
	int num_headers;
	enum { NONE = 0, FIELD, VALUE } last_header_element;
	char headers[MAX_HEADERS][2][MAX_ELEMENT_SIZE];
	int should_keep_alive;

	int message_begin_cb_called;
	int headers_complete_cb_called;
	int message_complete_cb_called;
};

/* * R E Q U E S T S * */
const struct Message requests[] =
#define CURL_GET 0
    {
        {.name    = "curl get",
            .type = HTTP_REQUEST,
            .raw =
                "GET /test HTTP/1.1\r\n"
                "User-Agent: curl/7.18.0 (i486-pc-linux-gnu) libcurl/7.18.0 OpenSSL/0.9.8g zlib/1.2.3.3 libidn/1.1\r\n"
                "Host: 0.0.0.0=5000\r\n"
                "Accept: */*\r\n"
                "\r\n",
            .should_keep_alive = 1,
            .method            = "GET",
            .query_string      = "",
            .fragment          = "",
            .request_path      = "/test",
            .request_uri       = "/test",
            .num_headers       = 3,
            .headers           = {{"User-Agent",
                            "curl/7.18.0 (i486-pc-linux-gnu) libcurl/7.18.0 OpenSSL/0.9.8g zlib/1.2.3.3 libidn/1.1"},
                {"Host", "0.0.0.0=5000"}, {"Accept", "*/*"}},
            .body              = ""}

#define FIREFOX_GET 1
        ,
        {.name    = "firefox get",
            .type = HTTP_REQUEST,
            .raw  = "GET /favicon.ico HTTP/1.1\r\n"
                   "Host: 0.0.0.0=5000\r\n"
                   "User-Agent: Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9) Gecko/2008061015 Firefox/3.0\r\n"
                   "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n"
                   "Accept-Language: en-us,en;q=0.5\r\n"
                   "Accept-Encoding: gzip,deflate\r\n"
                   "Accept-Charset: ISO-8859-1,utf-8;q=0.7,*;q=0.7\r\n"
                   "Keep-Alive: 300\r\n"
                   "Connection: keep-alive\r\n"
                   "\r\n",
            .should_keep_alive = 1,
            .method            = "GET",
            .query_string      = "",
            .fragment          = "",
            .request_path      = "/favicon.ico",
            .request_uri       = "/favicon.ico",
            .num_headers       = 8,
            .headers           = {{"Host", "0.0.0.0=5000"},
                {"User-Agent", "Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9) Gecko/2008061015 Firefox/3.0"},
                {"Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8"},
                {"Accept-Language", "en-us,en;q=0.5"}, {"Accept-Encoding", "gzip,deflate"},
                {"Accept-Charset", "ISO-8859-1,utf-8;q=0.7,*;q=0.7"}, {"Keep-Alive", "300"},
                {"Connection", "keep-alive"}},
            .body              = ""}

#define DUMBFUCK 2
        ,
        {.name    = "dumbfuck",
            .type = HTTP_REQUEST,
            .raw  = "GET /dumbfuck HTTP/1.1\r\n"
                   "aaaaaaaaaaaaa:++++++++++\r\n"
                   "\r\n",
            .should_keep_alive = 1,
            .method            = "GET",
            .query_string      = "",
            .fragment          = "",
            .request_path      = "/dumbfuck",
            .request_uri       = "/dumbfuck",
            .num_headers       = 1,
            .headers           = {{"aaaaaaaaaaaaa", "++++++++++"}},
            .body              = ""}

#define FRAGMENT_IN_URI 3
        ,
        {.name    = "fragment in uri",
            .type = HTTP_REQUEST,
            .raw  = "GET /forums/1/topics/2375?page=1#posts-17408 HTTP/1.1\r\n"
                   "\r\n",
            .should_keep_alive = 1,
            .method            = "GET",
            .query_string      = "page=1",
            .fragment          = "posts-17408",
            .request_path      = "/forums/1/topics/2375"
            /* XXX request uri does not include fragment? */
            ,
            .request_uri = "/forums/1/topics/2375?page=1",
            .num_headers = 0,
            .body        = ""}

#define GET_NO_HEADERS_NO_BODY 4
        ,
        {.name    = "get no headers no body",
            .type = HTTP_REQUEST,
            .raw  = "GET /get_no_headers_no_body/world HTTP/1.1\r\n"
                   "\r\n",
            .should_keep_alive = 1,
            .method            = "GET",
            .query_string      = "",
            .fragment          = "",
            .request_path      = "/get_no_headers_no_body/world",
            .request_uri       = "/get_no_headers_no_body/world",
            .num_headers       = 0,
            .body              = ""}

#define GET_ONE_HEADER_NO_BODY 5
        ,
        {.name    = "get one header no body",
            .type = HTTP_REQUEST,
            .raw  = "GET /get_one_header_no_body HTTP/1.1\r\n"
                   "Accept: */*\r\n"
                   "\r\n",
            .should_keep_alive = 1,
            .method            = "GET",
            .query_string      = "",
            .fragment          = "",
            .request_path      = "/get_one_header_no_body",
            .request_uri       = "/get_one_header_no_body",
            .num_headers       = 1,
            .headers           = {{"Accept", "*/*"}},
            .body              = ""}

#define GET_FUNKY_CONTENT_LENGTH 6
        ,
        {.name    = "get funky content length body hello",
            .type = HTTP_REQUEST,
            .raw  = "GET /get_funky_content_length_body_hello HTTP/1.0\r\n"
                   "conTENT-Length: 5\r\n"
                   "\r\n"
                   "HELLO",
            .should_keep_alive = 0,
            .method            = "GET",
            .query_string      = "",
            .fragment          = "",
            .request_path      = "/get_funky_content_length_body_hello",
            .request_uri       = "/get_funky_content_length_body_hello",
            .num_headers       = 1,
            .headers           = {{"conTENT-Length", "5"}},
            .body              = "HELLO"}

#define POST_IDENTITY_BODY_WORLD 7
        ,
        {.name    = "post identity body world",
            .type = HTTP_REQUEST,
            .raw  = "POST /post_identity_body_world?q=search#hey HTTP/1.1\r\n"
                   "Accept: */*\r\n"
                   "Transfer-Encoding: identity\r\n"
                   "Content-Length: 5\r\n"
                   "\r\n"
                   "World",
            .should_keep_alive = 1,
            .method            = "POST",
            .query_string      = "q=search",
            .fragment          = "hey",
            .request_path      = "/post_identity_body_world",
            .request_uri       = "/post_identity_body_world?q=search",
            .num_headers       = 3,
            .headers           = {{"Accept", "*/*"}, {"Transfer-Encoding", "identity"}, {"Content-Length", "5"}},
            .body              = "World"}

#define POST_CHUNKED_ALL_YOUR_BASE 8
        ,
        {.name    = "post - chunked body: all your base are belong to us",
            .type = HTTP_REQUEST,
            .raw  = "POST /post_chunked_all_your_base HTTP/1.1\r\n"
                   "Transfer-Encoding: chunked\r\n"
                   "\r\n"
                   "1e\r\nall your base are belong to us\r\n"
                   "0\r\n"
                   "\r\n",
            .should_keep_alive = 1,
            .method            = "POST",
            .query_string      = "",
            .fragment          = "",
            .request_path      = "/post_chunked_all_your_base",
            .request_uri       = "/post_chunked_all_your_base",
            .num_headers       = 1,
            .headers           = {{"Transfer-Encoding", "chunked"}},
            .body              = "all your base are belong to us"}

#define TWO_CHUNKS_MULT_ZERO_END 9
        ,
        {.name    = "two chunks ; triple zero ending",
            .type = HTTP_REQUEST,
            .raw  = "POST /two_chunks_mult_zero_end HTTP/1.1\r\n"
                   "Transfer-Encoding: chunked\r\n"
                   "\r\n"
                   "5\r\nhello\r\n"
                   "6\r\n world\r\n"
                   "000\r\n"
                   "\r\n",
            .should_keep_alive = 1,
            .method            = "POST",
            .query_string      = "",
            .fragment          = "",
            .request_path      = "/two_chunks_mult_zero_end",
            .request_uri       = "/two_chunks_mult_zero_end",
            .num_headers       = 1,
            .headers           = {{"Transfer-Encoding", "chunked"}},
            .body              = "hello world"}

#define CHUNKED_W_TRAILING_HEADERS 10
        ,
        {.name    = "chunked with trailing headers. blech.",
            .type = HTTP_REQUEST,
            .raw  = "POST /chunked_w_trailing_headers HTTP/1.1\r\n"
                   "Transfer-Encoding: chunked\r\n"
                   "\r\n"
                   "5\r\nhello\r\n"
                   "6\r\n world\r\n"
                   "0\r\n"
                   "Vary: *\r\n"
                   "Content-Type: text/plain\r\n"
                   "\r\n",
            .should_keep_alive = 1,
            .method            = "POST",
            .query_string      = "",
            .fragment          = "",
            .request_path      = "/chunked_w_trailing_headers",
            .request_uri       = "/chunked_w_trailing_headers",
            .num_headers       = 1,
            .headers           = {{"Transfer-Encoding", "chunked"}},
            .body              = "hello world"}

#define CHUNKED_W_BULLSHIT_AFTER_LENGTH 11
        ,
        {.name    = "with bullshit after the length",
            .type = HTTP_REQUEST,
            .raw  = "POST /chunked_w_bullshit_after_length HTTP/1.1\r\n"
                   "Transfer-Encoding: chunked\r\n"
                   "\r\n"
                   "5; ihatew3;whatthefuck=aretheseparametersfor\r\nhello\r\n"
                   "6; blahblah; blah\r\n world\r\n"
                   "0\r\n"
                   "\r\n",
            .should_keep_alive = 1,
            .method            = "POST",
            .query_string      = "",
            .fragment          = "",
            .request_path      = "/chunked_w_bullshit_after_length",
            .request_uri       = "/chunked_w_bullshit_after_length",
            .num_headers       = 1,
            .headers           = {{"Transfer-Encoding", "chunked"}},
            .body              = "hello world"}

        ,
        {.name = NULL} /* sentinel */
};

/* * R E S P O N S E S * */
const struct Message responses[] = {
    {.name    = "google 301",
        .type = HTTP_RESPONSE,
        .raw  = "HTTP/1.1 301 Moved Permanently\r\n"
               "Location: http://www.google.com/\r\n"
               "Content-Type: text/html; charset=UTF-8\r\n"
               "Date: Sun, 26 Apr 2009 11:11:49 GMT\r\n"
               "Expires: Tue, 26 May 2009 11:11:49 GMT\r\n"
               "Cache-Control: public, max-age=2592000\r\n"
               "Server: gws\r\n"
               "Content-Length: 219\r\n"
               "\r\n"
               "<HTML><HEAD><meta http-equiv=\"content-type\" content=\"text/html;charset=utf-8\">\n"
               "<TITLE>301 Moved</TITLE></HEAD><BODY>\n"
               "<H1>301 Moved</H1>\n"
               "The document has moved\n"
               "<A HREF=\"http://www.google.com/\">here</A>.\r\n"
               "</BODY></HTML>\r\n",
        .should_keep_alive = 1,
        .status_code       = 301,
        .num_headers       = 7,
        .headers           = {{"Location", "http://www.google.com/"}, {"Content-Type", "text/html; charset=UTF-8"},
            {"Date", "Sun, 26 Apr 2009 11:11:49 GMT"}, {"Expires", "Tue, 26 May 2009 11:11:49 GMT"},
            {"Cache-Control", "public, max-age=2592000"}, {"Server", "gws"}, {"Content-Length", "219"}},
        .body              = "<HTML><HEAD><meta http-equiv=\"content-type\" content=\"text/html;charset=utf-8\">\n"
                "<TITLE>301 Moved</TITLE></HEAD><BODY>\n"
                "<H1>301 Moved</H1>\n"
                "The document has moved\n"
                "<A HREF=\"http://www.google.com/\">here</A>.\r\n"
                "</BODY></HTML>\r\n"}

    ,
    {.name                 = "404 no headers no body",
        .type              = HTTP_RESPONSE,
        .raw               = "HTTP/1.1 404 Not Found\r\n\r\n",
        .should_keep_alive = 1,
        .status_code       = 404,
        .num_headers       = 0,
        .headers           = {},
        .body              = ""}

    ,
    {.name                 = "301 no response phrase",
        .type              = HTTP_RESPONSE,
        .raw               = "HTTP/1.1 301\r\n\r\n",
        .should_keep_alive = 1,
        .status_code       = 301,
        .num_headers       = 0,
        .headers           = {},
        .body              = ""}

    ,
    {.name = NULL} /* sentinel */
};

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
		auto pos2 = bp.parse((const uint8_t *)pos, (const uint8_t *)end);
		if (!bp.is_good())
			throw std::logic_error("Body failed to parse");
		message_eq(msg, req.req, bp.body.get_buffer());
		if (pos2 - (const uint8_t *)pos == 0)
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
		auto pos2 = bp.parse((const uint8_t *)pos, (const uint8_t *)end);
		if (!bp.is_good())
			throw std::logic_error("Body failed to parse");
		message_eq(msg, req.req, bp.body.get_buffer());
		if (pos2 - (const uint8_t *)pos == 0)
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

const size_t iterations = kBytes / (int64_t)data_len;

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
	std::cout << total / (1024 * 1024) << " mb | " << bw / (1024 * 1024) << " mb/s | " << iterations / elapsed
	          << " req/sec | " << elapsed << " s" << std::endl;
	return result;
}
