// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <algorithm>
#include <iostream>
#include <set>
#include <sstream>

#include <crab/crab.hpp>

//#include <sys/time.h>

static const char HTML[] = R"zzz(

<!DOCTYPE HTML>

<html>
   <head>
      
      <script type = "text/javascript">
         function WebSocketTest() {
            
            if ("WebSocket" in window) {
               
               // Let us open a web socket
				var url = new URL('/ws', window.location.href);
				url.protocol = url.protocol.replace('http', 'ws');
               console.log("WebSocket is supported by your Browser! Connecting to", url.href);
               var ws = new WebSocket(url.href);
				
               ws.onopen = function() {
                  
                  // Web Socket is connected, send data using send()
                  ws.send("Message to send");
                  console.log("Message is sent...");
               };
				
               ws.onmessage = function (evt) {
                  var received_msg = evt.data;
                  console.log("Message is received...", received_msg);
               };
				
               ws.onclose = function() {
                  
                  // websocket is closed.
                  console.log("Connection is closed...");
               };
            } else {
              
               // The browser doesn't support WebSocket
               console.log("WebSocket NOT supported by your Browser!");
            }
         }
      </script>
		
   </head>
   
   <body>
      <div id = "sse">
         <a href = "javascript:WebSocketTest()">Run WebSocket</a>
      </div>
      
   </body>
</html>


)zzz";

using namespace crab;

int test_aha() {
	http::ResponseBody response;
	response.set_body("Good");
	response.r.status             = 200;
	response.r.http_version_minor = response.r.http_version_major = 1;
	int si                                                        = 0;
	for (size_t i = 0; i != 1000000; ++i) {
		auto x = response.r.to_string();
		for (const auto a : x)
			si += a;
	}
	return si;
}

int test_http(size_t num, uint16_t port) {
	RunLoop runloop;
	int req_counter = 0;
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
			http::Server::web_socket_upgrade(who, std::move(request));
			connected_sockets.insert(who);
			http::Server::write(who, http::WebMessage{1, "Server first!"});
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
			http::WebMessage reply;
			reply.opcode = http::WebMessage::OPCODE_TEXT;
			reply.body   = "Echo from Crab: " + message.body;
			http::Server::write(who, std::move(reply));
		}
		runloop.print_records();
	};

	std::unique_ptr<Timer> stat_timer;
	stat_timer = std::make_unique<Timer>([&]() {
		const auto &st = RunLoop::get_stats();
		std::cout << num << " ---- req_counter=" << req_counter << " EPOLL_count=" << st.EPOLL_count
		          << " EPOLL_size=" << st.EPOLL_size << std::endl;
		std::cout << "RECV_count=" << st.RECV_count << " RECV_size=" << st.RECV_size << std::endl;
		std::cout << "SEND_count=" << st.SEND_count << " SEND_size=" << st.SEND_size << std::endl;
		for (auto *who : connected_sockets)
			http::Server::write(who,
			    http::WebMessage{1, "RECV_count=" + std::to_string(st.RECV_count) +
			                            " connected_clients=" + std::to_string(connected_sockets.size())});
		stat_timer->once(1);
	});
	stat_timer->once(1);

	runloop.run();
	return 0;
}

struct TestClient {};

int test_client(int num, uint16_t port) {
	RunLoop runloop;

	std::unique_ptr<Timer> stat_timer;
	std::unique_ptr<http::WebSocket> rws;

	size_t message_counter = 0;
	auto message_start     = std::chrono::high_resolution_clock::now();

	rws.reset(new http::WebSocket(
	    [&]() {
		    http::WebMessage wm;
		    while (rws->read_next(wm)) {
			    runloop.push_record("OnWebMessage", message_counter);
			    const auto idea_ms = std::chrono::duration_cast<std::chrono::microseconds>(
			        std::chrono::high_resolution_clock::now() - message_start);
			    runloop.print_records();
			    if (wm.is_binary()) {
				    std::cout << "Client Got Message: <Binary message> time=" << idea_ms.count() << " mks"
				              << std::endl;
			    } else {
				    std::cout << "Client Got Message: " << wm.body << " time=" << idea_ms.count() << " mks"
				              << std::endl;
			    }
			    stat_timer->once(1);
		    }
	    },
	    [&]() { std::cout << std::endl
		                  << "test_disconnect" << std::endl; }));

	http::RequestHeader req;
	req.host = "127.0.0.1";
	req.uri  = "/ws";
	rws->connect("127.0.0.1", port, req);

	stat_timer = std::make_unique<Timer>([&]() {
		message_counter += 1;
		http::WebMessage wm;
		wm.opcode     = http::WebMessage::OPCODE_TEXT;
		wm.body       = "Message " + std::to_string(message_counter);
		message_start = std::chrono::high_resolution_clock::now();
		runloop.push_record("SendWebMessage", message_counter);
		//		crab::add_performance('C', message_counter);
		//		crab::add_performance('D', message_counter);
		//		std::cout << "Send time" << tofd_cstr() << std::endl;
		rws->write(std::move(wm));
	});
	stat_timer->once(1);

	runloop.run();
	return 1;
}

int test_async_calls() {
	RunLoop runloop;
	std::mutex mut;
	std::vector<std::chrono::steady_clock::time_point> call_times;

	Watcher ab([&]() {
		auto no = std::chrono::steady_clock::now();
		std::vector<std::chrono::steady_clock::time_point> ct;
		{
			std::unique_lock<std::mutex> lock(mut);
			call_times.swap(ct);
		}
		for (const auto &t : ct) {
			std::cout << "call: "
			          << std::chrono::duration_cast<std::chrono::microseconds>(no - t).count() % 1000000000
			          << std::endl;
		}
		std::cout << "on_call: "
		          << std::chrono::duration_cast<std::chrono::microseconds>(no.time_since_epoch()).count() % 1000000000
		          << std::endl;
	});
	std::thread th([&]() {
		RunLoop r2;
		std::unique_ptr<Timer> t2;
		t2 = std::make_unique<Timer>([&]() {
			{
				std::unique_lock<std::mutex> lock(mut);
				call_times.push_back(std::chrono::steady_clock::now());
			}
			ab.call();
			t2->once(1);
		});
		t2->once(1);
		r2.run();
	});
	runloop.run();
	return 1;
}

int main(int argc, char *argv[]) {
	return test_async_calls();
	if (argc == 2 && std::string(argv[1]) == "--client")
		return test_client(0, 7000);
	return test_http(0, 7000);
	std::vector<std::thread> ths;
	for (size_t i = 0; i != 8; ++i)
		ths.emplace_back(&test_http, i, uint16_t(7000));
	while (!ths.empty()) {
		ths.back().join();
		ths.pop_back();
	}
	return 1;
	DNSWorker dns_worker;

	RunLoop runloop;

	Buffer ms(1024);
	char getstr[] = "GET / HTTP/1.1\r\nConnection: keep-alive\r\n\r\n";
	ms.write(getstr, sizeof(getstr) - 1);

	std::unique_ptr<TCPSocket> rws;

	rws.reset(new TCPSocket(
	    [&]() {
		    ms.write_to(*rws);
		    while (true) {
			    uint8_t buf[512] = {};
			    size_t cou       = rws->read_some(buf, sizeof(buf));
			    std::cout << std::string(reinterpret_cast<char *>(buf), cou);
			    if (cou != sizeof(buf))
				    break;
		    }
	    },
	    [&]() { std::cout << std::endl
		                  << "test_disconnect" << std::endl; }));
	//	auto na = DNSResolver::sync_resolve("google.com", true, false);
	//	if( !na.empty() )
	//		rws->connect(na.front(), "80");
	rws->connect("74.125.131.101", 80);

	//	auto google_names = DNSResolver::sync_resolve("google.ru", true, false);
	//	        for(auto na : google_names){
	//	            std::cout << " google name resolved=" << na << std::endl;
	//	        }

	DNSResolver res([](std::vector<std::string> result) {
		std::cout << "names resolved" << std::endl;
		for (auto na : result) {
			std::cout << " name resolved=" << na << std::endl;
		}
		RunLoop::current()->cancel();
	});

	res.resolve("alawar.com", true, true);
	res.cancel();
	std::this_thread::sleep_for(std::chrono::seconds(1));
	res.resolve("google.com", true, true);

	//	TCPSocket rws;
	//	TCPAcceptor server("127.0.0.1", 8700, [&]{
	//		std::string rs;
	//		server.accept(rws, str)
	//	});

	runloop.run();

	return 0;
}
