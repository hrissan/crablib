// Copyright (c) 2007-2023, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

// This code is based on work of https://gist.github.com/alessandro40/7e24df0a17803b71bbdf

// Answers the following question - will filling UDP buffer return EAGAIN from sendto (expected, allows 100% channel
// utilisation) Or will simply drop packets (would be disastrous for design)

// Turns out, on both Linux, behaviour is correct
// On Mac OSX, no EAGAIN returned, WireShark must be used to investigate dropped packets
// TODO - test on Windows

#if defined(_WIN32)
int main() { return 0; }
#else
#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define BUF_SIZE 1200

//#define ADDRESS "192.168.0.108"
//#define ADDRESS "10.60.35.25"
#define ADDRESS "239.195.14.121"

int main() {
	sockaddr_storage salocal{};
	sockaddr_in *sin = reinterpret_cast<sockaddr_in *>(&salocal);
	sin->sin_family  = AF_INET;
	sin->sin_port    = htons(54321);

	int fd           = -1;
	size_t count     = 0;
	size_t packet_id = 0;

	if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		printf("socket() failed\n");
		return 1;
	}
	if (bind(fd, reinterpret_cast<sockaddr *>(sin), sizeof(*sin)) < 0) {
		printf("bind() failed\n");
		return 1;
	}

	char buf[BUF_SIZE];
	memset(buf, 'x', BUF_SIZE);

	sin->sin_port = htons(12345);
	auto result   = inet_pton(AF_INET, ADDRESS, &sin->sin_addr);
	if (result != 1) {
		printf("inet_pton() failed\n");
		return 1;
	}

	while (true) {
		memcpy(buf, &packet_id, sizeof(packet_id));
		auto res = sendto(fd, buf, BUF_SIZE, MSG_DONTWAIT, reinterpret_cast<sockaddr *>(sin), sizeof(*sin));
		if (res < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				printf("sendto() returned EAGAIN after %d\n", int(count));
				sleep(1);
				continue;
			}
			printf("sendto() failed\n");
			return 1;
		}
		count += res;
		packet_id += 1;
	}
	return 0;
}
#endif