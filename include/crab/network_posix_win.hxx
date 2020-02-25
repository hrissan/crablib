// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include "network_posix.hxx"
#include "network_win.hxx"

#if CRAB_SOCKET_KEVENT || CRAB_SOCKET_EPOLL || CRAB_SOCKET_WINDOWS

// Surprisngly, some code compiles without changes on all 3 systems

namespace crab {

CRAB_INLINE Address::Address() { addr.ss_family = AF_INET; }

CRAB_INLINE bool Address::parse(Address &address, const std::string &numeric_host, uint16_t port) {
	Address tmp;
	auto ap6 = reinterpret_cast<sockaddr_in6 *>(tmp.impl_get_sockaddr());
	if (inet_pton(AF_INET6, numeric_host.c_str(), &ap6->sin6_addr) == 1) {
		tmp.addr.ss_family = AF_INET6;
		ap6->sin6_port     = htons(port);
		address            = tmp;
		return true;
	}
	auto ap = reinterpret_cast<sockaddr_in *>(tmp.impl_get_sockaddr());
	if (inet_pton(AF_INET, numeric_host.c_str(), &ap->sin_addr) == 1) {
		tmp.addr.ss_family = AF_INET;
		ap->sin_port       = htons(port);
		address            = tmp;
		return true;
	}
	return false;
}

CRAB_INLINE std::string Address::get_address() const {
	char addr_buf[INET6_ADDRSTRLEN] = {};
	switch (addr.ss_family) {
	case AF_INET: {
		auto ap = reinterpret_cast<const sockaddr_in *>(impl_get_sockaddr());
		inet_ntop(AF_INET, &ap->sin_addr, addr_buf, sizeof(addr_buf));
		return addr_buf;
	}
	case AF_INET6: {
		auto ap = reinterpret_cast<const sockaddr_in6 *>(impl_get_sockaddr());
		inet_ntop(AF_INET6, &ap->sin6_addr, addr_buf, sizeof(addr_buf));
		return addr_buf;
	}
	default:
		return "<UnknownFamily" + std::to_string(addr.ss_family) + ">";
	}
}

CRAB_INLINE uint16_t Address::get_port() const {
	switch (addr.ss_family) {
	case AF_INET: {
		auto ap = reinterpret_cast<const sockaddr_in *>(impl_get_sockaddr());
		return ntohs(ap->sin_port);
	}
	case AF_INET6: {
		auto ap = reinterpret_cast<const sockaddr_in6 *>(impl_get_sockaddr());
		return ntohs(ap->sin6_port);
	}
	default:
		return 0;
	}
}

CRAB_INLINE int Address::impl_get_sockaddr_length() const {
	switch (addr.ss_family) {
	case AF_INET: {
		return sizeof(sockaddr_in);
	}
	case AF_INET6: {
		return sizeof(sockaddr_in6);
	}
	default:
		return 0;
	}
}

CRAB_INLINE bool Address::is_multicast_group() const {
	switch (addr.ss_family) {
	case AF_INET: {
		auto ap                = reinterpret_cast<const sockaddr_in *>(impl_get_sockaddr());
		const uint8_t highbyte = *reinterpret_cast<const uint8_t *>(&ap->sin_addr);
		return (highbyte & 0xf0U) == 0xe0U;
	}
	case AF_INET6: {
		auto ap                = reinterpret_cast<const sockaddr_in6 *>(impl_get_sockaddr());
		const uint8_t highbyte = *reinterpret_cast<const uint8_t *>(&ap->sin6_addr);
		return highbyte == 0xff;
	}
	default:
		return false;
	}
}

CRAB_INLINE std::vector<Address> DNSResolver::sync_resolve(const std::string &host_name,
    uint16_t port,
    bool ipv4,
    bool ipv6) {
	std::vector<Address> names;
	if (!ipv4 && !ipv6)
		return names;
	addrinfo hints = {};
	struct AddrinfoHolder {
		struct addrinfo *result = nullptr;
		~AddrinfoHolder() { freeaddrinfo(result); }
	} holder;

	hints.ai_family   = (ipv4 && ipv6) ? AF_UNSPEC : ipv4 ? AF_INET : AF_INET6;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags    = AI_V4MAPPED | AI_ADDRCONFIG;  // AI_NUMERICHOST

	auto service = std::to_string(port);

	if (getaddrinfo(host_name.c_str(), service.c_str(), &hints, &holder.result) != 0)
		return names;

	for (struct addrinfo *rp = holder.result; rp != nullptr; rp = rp->ai_next) {
		if (rp->ai_family != AF_INET && rp->ai_family != AF_INET6)
			continue;
		if (rp->ai_addrlen > sizeof(sockaddr_storage))
			continue;
		names.emplace_back();
		std::memcpy(names.back().impl_get_sockaddr(), rp->ai_addr, rp->ai_addrlen);
	}
	return names;
}

}  // namespace crab

#endif