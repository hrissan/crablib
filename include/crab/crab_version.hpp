// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#pragma once

#include <string>

// #define CRAB_COMPILE 1 <- Set this in project settings to select compiled version of lib
// #define CRAB_SOCKET_BOOST 1 <- Set this to make crab a wrapper around boost::asio

// Our selector of network implementation
#if CRAB_SOCKET_BOOST  // Define in CMakeLists to select this impl
#include <boost/asio.hpp>
#include <boost/circular_buffer.hpp>
#elif defined(__MACH__)
#define CRAB_SOCKET_KEVENT 1
#elif defined(__linux__)
#define CRAB_SOCKET_EPOLL 1
#elif defined(_WIN32)
#define CRAB_SOCKET_WINDOWS 1
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

#if CRAB_SOCKET_KEVENT
inline std::string version_string() { return CRAB_VERSION " (kevent)"; }
#elif CRAB_SOCKET_EPOLL
inline std::string version_string() { return CRAB_VERSION " (epoll)"; }
#elif CRAB_SOCKET_WINDOWS
inline std::string version_string() { return CRAB_VERSION " (windows)"; }
#elif CRAB_SOCKET_BOOST
inline std::string version_string() { return CRAB_VERSION " (boost)"; }
#else
#error "Please add appropriate version string here"
#endif

}  // namespace crab
