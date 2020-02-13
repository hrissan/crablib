// Copyright (c) 2007-2019, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <algorithm>
#include <iostream>
#include <set>
#include <sstream>

#include <crab/crab.hpp>

using namespace crab;

int main(int argc, char *argv[]) {
	DNSWorker dns_worker;
	RunLoop runloop;

	DNSResolver res([](std::vector<std::string> result) {
		std::cout << "names resolved" << std::endl;
		for (auto na : result) {
			std::cout << " name resolved=" << na << std::endl;
		}
		RunLoop::current()->cancel();
	});

	//  Uncomment for some async dance
	res.resolve("alawar.com", true, true);
	res.cancel();
	std::this_thread::sleep_for(std::chrono::seconds(1));

	res.resolve("google.com", true, true);

	runloop.run();

	return 0;
}
