// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <iostream>
#include <set>

#include <crab/crab.hpp>

#include "gate_message.hpp"

class MDClientApp {
public:
	explicit MDClientApp(const MDSettings &settings)
	    : settings(settings)
	    , udp_a(settings.md_gate_udp_a_group, settings.md_gate_udp_a_port, [&]() { on_udp_a(); }) {}

private:
	void on_udp_a() {
		uint8_t buffer[crab::UDPReceiver::MAX_DATAGRAM_SIZE];
		while (true) {
			size_t si = 0;
			if (!udp_a.read_datagram(buffer, &si))
				break;
			if (si != Msg::size) {
				std::cout << "Wrong message size, skipping" << std::endl;
				continue;
			}
			Msg msg;
			crab::IMemoryStream is(buffer, si);
			msg.read(&is);
			std::cout << "Msg with seq=" << msg.seqnum << std::endl;
		}
	}
	const MDSettings settings;

	crab::UDPReceiver udp_a;
};

int main(int argc, char *argv[]) {
	std::cout << "This client listens to financial messages via UDP multicast and requests retransmits via TCP"
	          << std::endl;
	crab::RunLoop runloop;

	MDSettings settings;
	MDClientApp app(settings);

	runloop.run();
	return 0;
}
