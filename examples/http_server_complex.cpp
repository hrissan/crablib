// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <iostream>
#include <set>

#include <crab/crab.hpp>

static const char HTML[] = R"zzz(
<!DOCTYPE HTML>
<html><head>
      <script type = "text/javascript">
         function WebSocketTest() {
            if ("WebSocket" in window) {
              var url = new URL('/ws', window.location.href);
              url.protocol = url.protocol.replace('http', 'ws');
              console.log("WebSocket is supported by your Browser! Connecting to", url.href);
              var ws = new WebSocket(url.href);
              ws.onopen = function() {
                  ws.send("Message to send");
                  console.log("Message is sent...");
               };
               ws.onmessage = function (evt) {
                  var received_msg = evt.data;
                  console.log("Message is received...", received_msg);
               };
               ws.onclose = function() {
                  console.log("Connection is closed...");
               };
            } else {
               console.log("WebSocket NOT supported by your Browser!");
            }
         }
      </script>
   </head><body>
      <div id = "sse">
         Open JavaScript Console first, then <a href = "javascript:WebSocketTest()">Run WebSocket Test</a>
      </div>
   </body></html>
)zzz";

namespace http = crab::http;

class ServerComplexApp {
public:
	explicit ServerComplexApp(uint16_t port) : server(port), stat_timer([&]() { on_stat_timer(); }) {
		server.r_handler = [&](http::Client *who, http::Request &&request) {
			req_counter += 1;
			if (request.header.path == "/ws") {
				connected_sockets.push_back(who);
				auto it = --connected_sockets.end();
				who->web_socket_upgrade(
				    [who](http::WebMessage &&message) {
					    //		std::cout << "Server Got Message: " << message.body << std::endl;
					    if (message.is_binary()) {  // Echo binary messages back AS IS
						    who->write(std::move(message));
					    } else {
						    who->write(http::WebMessage("Echo from Crab: " + message.body));
					    }
					    crab::RunLoop::current()->stats.print_records(std::cout);
				    },
				    [this, it]() { connected_sockets.erase(it); });
				who->write(http::WebMessage("Server-initiated on connect message!"));
				return;
			}
			if (request.header.path == "/") {
				http::Response response;
				response.header.status = 200;
				response.header.set_content_type("text/html", "charset=utf-8");
				response.set_body(HTML);
				who->write(std::move(response));
				return;
			}
			if (request.header.path == "/quit") {
				crab::RunLoop::current()->cancel();
				who->write(http::Response::simple_html(200, "Server is stoped"));
				return;
			}
			who->write(http::Response::simple_html(404));
		};
		stat_timer.once(1);
	}

private:
	void on_stat_timer() {
		stat_timer.once(1);

		const auto &st = crab::RunLoop::current()->stats;
		std::cout << " ---- req_counter=" << req_counter << " EPOLL_count=" << st.EPOLL_count
		          << " EPOLL_size=" << st.EPOLL_size << std::endl;
		std::cout << "RECV_count=" << st.RECV_count << " RECV_size=" << st.RECV_size << std::endl;
		std::cout << "SEND_count=" << st.SEND_count << " SEND_size=" << st.SEND_size << std::endl;
		for (auto *who : connected_sockets)
			who->write(http::WebMessage("RECV_count=" + std::to_string(st.RECV_count) +
			                            " connected_clients=" + std::to_string(connected_sockets.size())));
	}
	http::Server server;
	crab::Timer stat_timer;
	size_t req_counter = 0;
	std::list<http::Client *> connected_sockets;
};

int main(int argc, char *argv[]) {
	std::cout << "This server has echo web-socket responder built-in. Open '/' in browser to play" << std::endl;
	crab::RunLoop runloop;

	ServerComplexApp app(7000);

	runloop.run();
	return 0;
}
