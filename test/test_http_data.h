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
extern const struct Message requests[];
/* * R E S P O N S E S * */
extern const struct Message responses[];
