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

using namespace crab;

int test_client(int num, const std::string &host, uint16_t port) {
	RunLoop runloop;
	//    Idle idle([](){});

	std::unique_ptr<Timer> stat_timer;
	std::unique_ptr<http::WebSocket> rws;

	int message_counter = 0;
	auto message_start  = std::chrono::high_resolution_clock::now();

	rws.reset(new http::WebSocket(
	    [&]() {
		    http::WebMessage wm;
		    while (rws->read_next(wm)) {
			    runloop.stats.push_record("OnWebMessage", 0, message_counter);
			    //			    const auto idea_ms = std::chrono::duration_cast<std::chrono::microseconds>(
			    //			        std::chrono::high_resolution_clock::now() - message_start);
			    //			    runloop.print_records();
			    //			    if (wm.is_binary()) {
			    //				    std::cout << "Client Got Message: <Binary message> time=" << idea_ms.count() << "
			    // mks"
			    //				              << std::endl;
			    //			    } else {
			    //				    std::cout << "Client Got Message: " << wm.body << " time=" << idea_ms.count() << "
			    // mks"
			    //				              << std::endl;
			    //			    }
			    LatencyMessage lm;
			    if (lm.parse(wm.body)) {
				    lm.add_lat("client_receive", std::chrono::steady_clock::now());
				    std::cout << lm.save() << std::endl;
			    }
		    }
		    stat_timer->once(1.0);
		    runloop.stats.print_records(std::cout);
	    },
	    [&]() { std::cout << std::endl
		                  << "test_disconnect" << std::endl; }));

	http::RequestHeader req;
	req.host = host;
	req.path = "/latency";
	rws->connect(crab::Address(host, port), req);

	stat_timer.reset(new Timer([&]() {
		message_counter += 1;

		LatencyMessage lm(std::chrono::steady_clock::now());
		lm.id   = "Client";
		lm.body = "Message " + std::to_string(message_counter);

		http::WebMessage wm;
		wm.opcode     = http::WebMessage::OPCODE_TEXT;
		wm.body       = lm.save();
		message_start = std::chrono::high_resolution_clock::now();
		runloop.stats.push_record("SendWebMessage", 0, message_counter);
		rws->write(std::move(wm));
	}));
	stat_timer->once(1);

	runloop.run();
	return 1;
}

int test_watcher() {
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
		t2.reset(new Timer([&]() {
			{
				std::unique_lock<std::mutex> lock(mut);
				call_times.push_back(std::chrono::steady_clock::now());
			}
			ab.call();
			t2->once(1);
		}));
		t2->once(1);
		r2.run();
	});
	runloop.run();
	return 1;
}

int test_parsing() {
	http::Response response;
	response.set_body("Good");
	response.header.status             = 200;
	response.header.http_version_minor = response.header.http_version_major = 1;
	int si                                                                  = 0;
	for (size_t i = 0; i != 1000000; ++i) {
		auto x = response.header.to_string();
		for (const auto a : x)
			si += a;
	}
	return si;
}

int main(int argc, char *argv[]) {
	if (argc == 2 && std::string(argv[1]) == "--watcher")
		return test_watcher();
	if (argc != 3) {
		std::cout << "Usage: client <IP4address> <port>" << std::endl;
		return 0;
	}
	return test_client(0, argv[1], crab::integer_cast<uint16_t>(argv[2]));
}
