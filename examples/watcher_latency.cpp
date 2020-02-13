// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <algorithm>
#include <iostream>
#include <mutex>
#include <thread>

#include <crab/crab.hpp>

using steady_clock = std::chrono::steady_clock;
using namespace crab;

class TestAsyncCallsApp {
public:
	TestAsyncCallsApp() : ab([&]() { on_call(); }), th([&]() { thread_run(); }) {}
	~TestAsyncCallsApp() { th.join(); }

private:
	void on_call() {
		auto no = steady_clock::now();
		std::vector<steady_clock::time_point> ct;
		{
			std::unique_lock<std::mutex> lock(mut);
			call_times.swap(ct);
		}
		std::cout << "on_call, " << ct.size() << " calls in queue" << std::endl;
		for (const auto &t : ct) {
			std::cout << "latency: " << std::chrono::duration_cast<std::chrono::microseconds>(no - t).count()
			          << " mksec" << std::endl;
		}
	}
	void thread_run() {
		RunLoop r2;
		std::unique_ptr<Timer> t2;
		t2.reset(new Timer([&]() {
			{
				std::unique_lock<std::mutex> lock(mut);
				call_times.push_back(steady_clock::now());
			}
			ab.call();
			t2->once(1);
		}));
		t2->once(1);
		r2.run();
	}
	std::mutex mut;
	std::vector<steady_clock::time_point> call_times;
	Watcher ab;

	std::thread th;
};

int main(int argc, char *argv[]) {
	RunLoop runloop;
	TestAsyncCallsApp app;
	std::unique_ptr<Idle> idle;
	if (argc == 2 && std::string(argv[1]) == "--idle") {
		std::cout << "Testing with on_idle, use thread pinning for best results" << std::endl;
		idle.reset(new Idle(([]() {})));
	}
	runloop.run();
	return 0;
}
