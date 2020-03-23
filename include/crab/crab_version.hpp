// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#pragma once

#include <string>

// #define CRAB_COMPILE 1 <- Set this in project settings to select compiled version of lib

// #define CRAB_IMPL_BOOST 1 <- Set this to make crab a wrapper around boost::asio
// #define CRAB_IMPL_LIBEV 1 <- Set this to make crab a wrapper around libev

namespace crab {

inline std::string version() { return "0.4"; }

}  // namespace crab

// Our selector of low-level implementation

// clang-format off

#if CRAB_IMPL_LIBEV  // Define in CMakeLists to select this impl
    #include <ev++.h>
#elif CRAB_IMPL_BOOST  // Define in CMakeLists to select this impl
    #include <boost/asio.hpp>
    #include <boost/circular_buffer.hpp>
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

// clang-format on
