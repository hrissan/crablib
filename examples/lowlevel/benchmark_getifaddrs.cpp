// Copyright (c) 2007-2023, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

// This code is based on work of https://gist.github.com/alessandro40/7e24df0a17803b71bbdf

// Answers the following question - will filling UDP buffer return EAGAIN from sendto (expected, allows 100% channel
// utilisation) Or will simply drop packets (would be disastrous for design of UDP streaming libraries)

// Turns out, on both Linux, behaviour is correct
// On Mac OSX, no EAGAIN returned, WireShark must be used to investigate dropped packets.
// Probably crab with CF backend must be recommended on Mac, unfortunately it is least developed.
// TODO - test on Windows

#if defined(_WIN32)
int main() { return 0; }
#else
#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <chrono>

int get_once() {
	struct ifaddrs *ifaddr, *ifa;
	if (getifaddrs(&ifaddr) == -1) {
		perror("getifaddrs");
		exit(EXIT_FAILURE);
	}
	int result = 0;  // Prevent optimizations
	for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr == NULL)
			continue;
		result += ifa->ifa_addr->sa_family;
	}
	freeifaddrs(ifaddr);
	return result;
}

int main() {
	// Listen to interface changes on Linux
	// https://github.com/angt/ipevent/blob/e0a4c4dfe8ac193345315d55f320ab212dbda784/ipevent.c

	// Multicast with IPv6
	// https://linux.die.net/man/3/if_nametoindex
	// https://stackoverflow.com/questions/53309453/sending-packet-to-interface-via-multicast?noredirect=1&lq=1

	struct ifaddrs *ifaddr, *ifa;
	int family, s, n;
	char host[NI_MAXHOST];

	if (getifaddrs(&ifaddr) == -1) {
		perror("getifaddrs");
		exit(EXIT_FAILURE);
	}

	/* Walk through linked list, maintaining head pointer so we
	   can free list later */

	printf("%-12s %-8s family\n", "name", "idx");

	for (ifa = ifaddr, n = 0; ifa != NULL; ifa = ifa->ifa_next, n++) {
		if (ifa->ifa_addr == NULL)
			continue;

		family = ifa->ifa_addr->sa_family;

		/* Display interface name and family (including symbolic
		   form of the latter for the common families) */

		unsigned idx = if_nametoindex(ifa->ifa_name);

		printf("%-12s %-8u %s (%d)\n", ifa->ifa_name, idx, (family == AF_INET) ? "AF_INET" : (family == AF_INET6) ? "AF_INET6" : "???",
		    family);

		/* For an AF_INET* interface address, display the address */

		if (family == AF_INET || family == AF_INET6) {
			s = getnameinfo(ifa->ifa_addr, (family == AF_INET) ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6), host,
			    NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
			if (s != 0) {
				printf("getnameinfo() failed: %s\n", gai_strerror(s));
				exit(EXIT_FAILURE);
			}

			printf("\t\taddress: <%s>\n", host);
		}
	}

	freeifaddrs(ifaddr);

	const size_t MAX_COUNT = 10000;
	printf("Benchmarking %d counts\n", int(MAX_COUNT));
	auto start = std::chrono::steady_clock::now();
	int result = 0;
	for (size_t i = 0; i != MAX_COUNT; ++i)
		result += get_once();
	auto now = std::chrono::steady_clock::now();
	printf("Result %f microseconds per call\n",
	    float(std::chrono::duration_cast<std::chrono::microseconds>(now - start).count()) / MAX_COUNT);
	return result;
}
#endif