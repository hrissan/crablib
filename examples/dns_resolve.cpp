// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <iostream>

#include <crab/crab.hpp>

int main(int argc, char *argv[]) {
	crab::DNSWorker dns_worker;
	crab::RunLoop runloop;

	crab::DNSResolver res([](const std::vector<crab::Address> &result) {
		std::cout << "names resolved" << std::endl;
		for (const auto &na : result) {
			std::cout << " name resolved=" << na.get_address() << ":" << na.get_port() << std::endl;
		}
		crab::RunLoop::current()->cancel();
	});

	//  Uncomment next 3 lines for some async dance
	res.resolve("alawar.com", 80, true, true);
	res.cancel();
	std::this_thread::sleep_for(std::chrono::seconds(1));

	res.resolve("google.com", 80, true, true);

	runloop.run();

	return 0;
}
