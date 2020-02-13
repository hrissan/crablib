// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>

#include <crab/crab.hpp>

#include "gate_message.hpp"

// TODO - rewrite gate examples for clarity

static const char HTML[] = R"zzz(

<!DOCTYPE HTML>
<html>
   <head>
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
   </head>
   <body>
      <div id = "sse"><a href = "javascript:WebSocketTest()">Run WebSocket</a></div>
   </body>
</html>


)zzz";

using namespace crab;

int test_http(size_t num, uint16_t port) {
	RunLoop runloop;
	int req_counter = 0;
	//	Idle idle([](){});
	std::set<http::Client *> connected_sockets;
	http::Server server("0.0.0.0", port);
	server.r_handler = [&](http::Client *who, http::RequestBody &&request, http::ResponseBody &response) -> bool {
		if (request.r.uri == "/") {
			response.r.status       = 200;
			response.r.content_type = "text/html; charset=utf-8";
			response.set_body(HTML);
			return true;
		}
		if (request.r.uri == "/quit") {
			crab::RunLoop::current()->cancel();
			return true;
		}
		if (request.r.uri == "/ws") {
			server.web_socket_upgrade(who, std::move(request));
			connected_sockets.insert(who);
			http::Server::write(who, http::WebMessage("Server first!"));
			return false;
		}
		if (request.r.uri == "/latency") {
			server.web_socket_upgrade(who, std::move(request));
			connected_sockets.insert(who);
			return false;
		}
		response.r.status       = 200;
		response.r.content_type = "text/plain; charset=utf-8";
		response.set_body("Hello, Crab!");
		req_counter += 1;
		return true;
	};
	server.d_handler = [&](http::Client *who) { connected_sockets.erase(who); };
	server.w_handler = [&](http::Client *who, http::WebMessage &&message) {
		//		std::cout << "Server Got Message: " << message.body << std::endl;
		if (message.is_binary()) {
			http::Server::write(who, std::move(message));
		} else {
			LatencyMessage lm;
			if (lm.parse(message.body)) {
				lm.add_lat("server", std::chrono::steady_clock::now());
				std::cout << lm.save() << std::endl;
				http::WebMessage reply;
				reply.opcode = http::WebMessage::OPCODE_TEXT;
				reply.body   = lm.save();
				http::Server::write(who, std::move(reply));
			} else {
				http::WebMessage reply;
				reply.opcode = http::WebMessage::OPCODE_TEXT;
				reply.body   = "Echo from Crab: " + message.body;
				http::Server::write(who, std::move(reply));
			}
		}
		runloop.print_records();
	};

	std::unique_ptr<Timer> stat_timer;
	stat_timer.reset(new Timer([&]() {
		const auto &st = RunLoop::get_stats();
		std::cout << num << " ---- req_counter=" << req_counter << " EPOLL_count=" << st.EPOLL_count
		          << " EPOLL_size=" << st.EPOLL_size << std::endl;
		std::cout << "RECV_count=" << st.RECV_count << " RECV_size=" << st.RECV_size << std::endl;
		std::cout << "SEND_count=" << st.SEND_count << " SEND_size=" << st.SEND_size << std::endl;
		runloop.print_records();
		//		for (auto *who : connected_sockets)
		//			http::Server::write(who,
		//			    http::WebMessage("RECV_count=" + std::to_string(st.RECV_count) +
		//			                            " connected_clients=" + std::to_string(connected_sockets.size())));
		stat_timer->once(1);
	}));
	stat_timer->once(1);

	runloop.run();
	return 0;
}

int main(int argc, char *argv[]) {
	if (argc != 2) {
		std::cout << "Usage: server <port>" << std::endl;
		return 0;
	}
	return test_http(0, std::stoi(argv[1]));
}
