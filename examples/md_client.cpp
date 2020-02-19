// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <ifaddrs.h>
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

// Listen to interface changes on Linux
// https://github.com/angt/ipevent/blob/e0a4c4dfe8ac193345315d55f320ab212dbda784/ipevent.c

void print_interfaces() {
	struct ifaddrs *ifaddr, *ifa;
	int family, s, n;
	char host[NI_MAXHOST];

	if (getifaddrs(&ifaddr) == -1) {
		perror("getifaddrs");
		exit(EXIT_FAILURE);
	}

	/* Walk through linked list, maintaining head pointer so we
	   can free list later */

	for (ifa = ifaddr, n = 0; ifa != NULL; ifa = ifa->ifa_next, n++) {
		if (ifa->ifa_addr == NULL)
			continue;

		family = ifa->ifa_addr->sa_family;

		/* Display interface name and family (including symbolic
		   form of the latter for the common families) */

		printf("%-8s %s (%d)\n",
		    ifa->ifa_name,
		    (family == AF_INET) ? "AF_INET" : (family == AF_INET6) ? "AF_INET6" : "???",
		    family);

		/* For an AF_INET* interface address, display the address */

		if (family == AF_INET || family == AF_INET6) {
			s = getnameinfo(ifa->ifa_addr,
			    (family == AF_INET) ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6), host, NI_MAXHOST,
			    NULL, 0, NI_NUMERICHOST);
			if (s != 0) {
				printf("getnameinfo() failed: %s\n", gai_strerror(s));
				exit(EXIT_FAILURE);
			}

			printf("\t\taddress: <%s>\n", host);
		}
	}

	freeifaddrs(ifaddr);
}

int main(int argc, char *argv[]) {
	print_interfaces();
	std::cout << "This client listens to financial messages via UDP multicast and requests retransmits via TCP"
	          << std::endl;
	crab::RunLoop runloop;

	MDSettings settings;
	MDClientApp app(settings);

	runloop.run();
	return 0;
}
