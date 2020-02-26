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

#include "test_http_data.h"

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
        {.name = 0} /* sentinel */
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
    {.name = 0} /* sentinel */
};
