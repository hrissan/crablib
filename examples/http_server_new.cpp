// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <iostream>

#include <crab/crab.hpp>

namespace crab {

namespace http2 {


class Client : protected ServerConnection {  // So the type is opaque for users
public:
    using WS_handler = std::function<void(WebMessage &&)>;

    using ServerConnection::get_peer_address;

    // You can either postpone response (do not forget to write later)
    void postpone_response(Handler &&dcb);

    // Upgrade to web socket
    void web_socket_upgrade(WS_handler &&cb);

    // write the whole response
    void write(Response &&);
    void write(WebMessage &&wm);

    // start streaming response (scb will be called in socket-like fashion on all events)
    // Will fill response date (if empty), version, keep_alive
    void start_write_stream(ResponseHeader &response, Handler &&scb);
    void start_write_stream(WebMessageOpcode opcode, Handler &&scb);

    // when streaming, check if space in write buffer available, is connection closed and get write position
    using ServerConnection::can_write;
    using ServerConnection::is_open;
    uint64_t get_body_position() const { return body_position; }

    // when streaming, write data
    void write(const uint8_t *val, size_t count, BufferOptions bo = WRITE);
    void write(const char *val, size_t count, BufferOptions bo = WRITE) { write(uint8_cast(val), count, bo); }
#if __cplusplus >= 201703L
    void write(const std::byte *val, size_t count, BufferOptions bo = WRITE) { write(uint8_cast(val), count, bo); }
#endif
    void write(std::string &&ss, BufferOptions bo = WRITE);

    // finish streaming (should not be used if ResponseHeader contains Content-Length)
    void write_last_chunk(BufferOptions bo = WRITE);

private:
    WS_handler ws_handler;
    Handler d_handler;
    Handler rwd_handler;

    uint64_t body_position      = 0;
    bool web_message_close_sent = false;
    friend class Server;
};

struct Header {
    string_view name;
    string_view value;
};

struct RequestResponseHeader {
    // Common for requests and responses
    int http_version_major = 1;
    int http_version_minor = 1;
    bool keep_alive = true;
    optional<uint64_t> content_length;

    std::vector<Header> headers;  // names are lower-case, array is sorted

    bool transfer_encoding_chunked = false;
    std::vector<string_view> transfer_encodings;  // lower-case, other than chunked, identity

    bool connection_upgrade = false;
    bool upgrade_websocket = false;  // Upgrade: WebSocket

    string_view content_type_mime;    // lower-case
    string_view content_type_suffix;  // after ";"
};

struct RequestHeader : public RequestResponseHeader {
    string_view method;
    string_view path;          // URL-decoded automatically on parse, encoded on send
    string_view query_string;  // not URL-decoded (would otherwise lose separators)

    string_view basic_authorization;
    string_view host;
    string_view origin;

    string_view sec_websocket_key;
    string_view sec_websocket_version;
};

class ServerConnection {
public:
    ServerConnection()
        :incoming_buffer(4096)
        , sock([this]() { sock_handler(); }) {}
        
	const RequestHeader & get_request() const { return req; }
	std::pair<const uint8_t *, size_t> read_next();
	
private:
    Buffer incoming_buffer; // Will be modified during parsing
    RequestHeader req; // Views are into incoming_buffer

    TCPSocket sock;
    std::deque<Buffer> outgoing_buffer;
    size_t outgoing_buffer_size = 0;

    Handler rwd_handler;

    void sock_handler() {

    }
};

class Server {
public:
    using Settings  = http::Server::Settings;
    using R_handler = std::function<void(ServerConnection *conn)>;
    // TODO - document new server interface
    // if you did not write response and exception is thrown, 422 will be returned
    // if you did not write full response and no exception is thrown, you are expected
    // to remember who and later call Client::write() to finish serving request

    explicit Server(const Address &address, const Settings &settings = Settings{})
            : settings{settings}, acceptor{address, std::bind(&Server::accept_all, this), settings} {}

    ~Server(){}
    // Unlike other parts of crab, you must not destroy server in handlers
    // when destroying server, remove all Client * you remembered

    R_handler r_handler = [](ServerConnection *) {};  // TODO - rename to request_handler

private:
    Settings settings;
    TCPAcceptor acceptor;

    std::list<ServerConnection> clients;

    void on_client_handler(std::list<ServerConnection>::iterator it) {

    }
    void on_client_disconnected(std::list<ServerConnection>::iterator it){

    }

    void accept_all() {
        while (acceptor.can_accept() && clients.size() < settings.max_connections) {
            clients.emplace_back();
            auto it = --clients.end();
            it->set_handler([this, it]() { on_client_handler(it); });
            it->accept(acceptor);
            //        std::cout << "HTTP Client accepted=" << cid << " addr=" << (*it)->get_peer_address() <<
std::endl;
        }
    }
};

using Rope = std::deque<Buffer>;

}  // namespace http
}  // namespace crab


namespace http = crab::http;
namespace http2 = crab::http2;

struct LimitedBody : public RequestHandler {
	virtual void on_body_read() {
		auto chunk = who->read_body();
	}
	virtual void on_body_finished()=0;
};

struct LongPollProcessor : public LimitedBody {
public:
	virtual void on_body_finished() {
		if (have_data()) {
			write_status();
			write_content_type();
			write_body();
		}else{
			// Add to data structure
			postpone_response();
		}
	}
	~LongPollProcessor() override {
		// Remove from data structure
	}
};

struct FileUploader : public RequestHandler {
public:
	virtual void on_body_read() {
		auto chunk = who->read_body(remained);
		file.write(chunk);
	}
	virtual void on_body_finished() {
		// Finish file
		write_status();
		write_content_type();
		write_body();
	}
	~FileUploader() override {
		// Remove file in not finished
	}
};

struct FileDownloader : public LimitedBody {
public:
	virtual void on_body_finished(Rope rope) {
		write_status();
		write_content_type();
		write_content_length();
	}
	virtual void on_body_write(size_t pos, optional<size_t> remains) {
		write();
	}
};


int main() {
	std::cout << "crablib version " << crab::version_string() << std::endl;

	std::cout << "This is new HTTP server on port 7000" << std::endl;

	crab::RunLoop runloop;

	http2::Server server(7000);

	server.r_handler = [&](http2::ServerConnection *who) {
		if (who->get_request().path.substr(0, 5) == "/long") {
			who->postpone_response([&](){
			
			});
		}
		if (who->get_request().path.substr(0, 5) == "/meow") {
		
		}
		bool cond = false;
		//		std::cout << "Request" << std::endl;
		for (const auto &q : request.parse_query_params()) {
			//			std::cout << "    '" << q.first << "' => '" << q.second << "'" << std::endl;
			if (q.first == crab::string_view{"query"})
				cond = true;
		}
		//		std::cout << "Cookies" << std::endl;
		//		for (const auto &q : request.parse_cookies())
		//			std::cout << "    '" << q.first << "' => '" << q.second << "'" << std::endl;
		http::Response response;
		response.header.status = 200;
		response.header.set_content_type("text/plain", "charset=utf-8");
		response.set_body(cond ? "Hello, Cond!" : "Hello, Crab!");
		who->write(std::move(response));

		// Or for even simpler code paths, like error messages
		// who->write(http::Response::simple_text(200, "Hello, Crab!"));
	};

	runloop.run();
	return 0;
}
