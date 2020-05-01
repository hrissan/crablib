// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#pragma once

#include <string>

// #define CRAB_COMPILE 1 <- Set this in project settings to select compiled version of lib
// #define CRAB_TLS 1     <- Set this in project settings to add TLS support (via OpenSSL or native platform support)

// #define CRAB_IMPL_LIBEV 1 <- Set this in project settings to make crab a wrapper around libev
// #define CRAB_IMPL_BOOST 1 <- Set this in project settings to make crab a wrapper around boost::asio
// #define CRAB_IMPL_CF 1    <- Set this in project settings to make crab a wrapper around CFRunLoop (mostly for iOS)

// Our selector of low-level implementation

// clang-format off

#if CRAB_IMPL_LIBEV  // Define in CMakeLists to select this impl
    #include <ev++.h>
#elif CRAB_IMPL_BOOST  // Define in CMakeLists to select this impl
    #include <boost/asio.hpp>
    #include <boost/circular_buffer.hpp>
#elif CRAB_IMPL_CF
    #include <CFNetwork/CFNetwork.h>
    #include <CoreFoundation/CoreFoundation.h>
#elif defined(__MACH__)
    #define CRAB_IMPL_KEVENT 1
#elif defined(__linux__)
    #define CRAB_IMPL_EPOLL 1
#elif defined(_WIN32)
    #define CRAB_IMPL_WINDOWS 1
#else
    #error "Sorry, No socket implementation for your platform"
#endif

// crap-crap-crap
#if CRAB_COMPILE
    #define CRAB_INLINE
#else
    #define CRAB_INLINE inline
#endif

#define CRAB_VERSION "0.8.0"
// Not in cmake, we wish to be easily includable in header-only mode

namespace crab {

#if CRAB_IMPL_LIBEV
	inline std::string version_string() { return CRAB_VERSION " (libev)"; }
#elif CRAB_IMPL_BOOST
	inline std::string version_string() { return CRAB_VERSION " (boost::asio)"; }
#elif CRAB_IMPL_CF
	inline std::string version_string() { return CRAB_VERSION " (Core Foundation)"; }
#elif CRAB_IMPL_KEVENT
	inline std::string version_string() { return CRAB_VERSION " (kevent)"; }
#elif CRAB_IMPL_EPOLL
	inline std::string version_string() { return CRAB_VERSION " (epoll)"; }
#elif CRAB_IMPL_WINDOWS
	inline std::string version_string() { return CRAB_VERSION " (Overlapped I/O)"; }
#else
	#error "Please add appropriate version string here"
#endif

}  // namespace crab

// clang-format on
