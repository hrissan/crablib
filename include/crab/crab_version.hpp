// Copyright (c) 2007-2023, Grigory Buteyko aka Hrissan
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
    #define CRAB_IMPL_STRING "libev"
    #include <ev++.h>
#elif CRAB_IMPL_BOOST  // Define in CMakeLists to select this impl
    #define CRAB_IMPL_STRING "boost::asio"
    #include <boost/asio.hpp>
    #include <boost/circular_buffer.hpp>
#elif CRAB_IMPL_CF
    #define CRAB_IMPL_STRING "Core Foundation"
    #include <CFNetwork/CFNetwork.h>
    #include <CoreFoundation/CoreFoundation.h>
#elif defined(__MACH__)
    #define CRAB_IMPL_KEVENT 1
    #define CRAB_IMPL_STRING "kevent"
#elif defined(__linux__)
    #define CRAB_IMPL_EPOLL 1
    #define CRAB_IMPL_STRING "epoll"
#elif defined(_WIN32)
    #define CRAB_IMPL_WINDOWS 1
    #define CRAB_IMPL_STRING "Overlapped I/O"
#else
    #error "Sorry, No socket implementation for your platform"
#endif

// crap-crap-crap
#if CRAB_COMPILE
    #define CRAB_COMPILE_STRING "Compiled"
    #define CRAB_INLINE
#else
    #define CRAB_COMPILE_STRING "Header-Only"
    #define CRAB_INLINE inline
#endif

#define CRAB_BRANCH "(dev branch)"
// No tricks, we wish to be easily included in header-only mode

#define CRAB_VERSION "0.9.3"
// Not in cmake, we wish to be easily included in header-only mode
#if CRAB_TLS
#define CRAB_TLS_STRING "with TLS"
#else
#define CRAB_TLS_STRING "no TLS"
#endif

namespace crab {

inline std::string version_string() { return CRAB_VERSION ", " CRAB_IMPL_STRING ", " CRAB_COMPILE_STRING ", " CRAB_TLS_STRING CRAB_BRANCH; }

}  // namespace crab

// clang-format on
