cmake_minimum_required(VERSION 3.0)

project(crablib
	VERSION "0.8.0"
	DESCRIPTION "Experiments to create cross-platform wrapper for Runloop/Timers/Sockets/ITC with lowest possible overhead on UNIX platforms"
	HOMEPAGE_URL "https://github.com/hrissan/crablib"
	LANGUAGES C CXX)

if(CRAB_IMPL_LIBEV)
	message("-DCRAB_IMPL_LIBEV=1")
	add_definitions(-DCRAB_IMPL_LIBEV=1)
endif()

if(CRAB_IMPL_BOOST)
	message("-DCRAB_IMPL_BOOST=1")
	message("Boost_INCLUDE_DIRS=${Boost_INCLUDE_DIRS}")
	add_definitions(-DCRAB_IMPL_BOOST=1)
endif()

if(CRAB_IMPL_CF)
	message("-DCRAB_IMPL_CF=1")
	add_definitions(-DCRAB_IMPL_CF=1)
endif()


if(CRAB_TLS)
	if(NOT WIN32)
		link_libraries(ssl crypto)
	endif()
endif()


if(MSVC)
    add_definitions(-D_SCL_SECURE_NO_WARNINGS=1 -D_CRT_SECURE_NO_WARNINGS=1) #  -D_WIN32_WINNT=0x0601
    add_compile_options(/we4715) # not all paths return value is error
else()
    add_compile_options(-Wno-noexcept-type)
#	add_compile_options(-g -Wall -Wextra -Werror=return-type -Wno-unused-parameter)
endif()


set(SOURCE_FILES
		include/crab/crab_version.hpp
		include/crab/crab.hpp
		include/crab/integer_cast.hpp
		include/crab/intrusive_list.hpp
		include/crab/intrusive_heap.hpp
		include/crab/network.hpp
		include/crab/network.hxx
		include/crab/network_base.hpp
		include/crab/network_boost.hxx
		include/crab/network_cf.hxx
		include/crab/network_libev.hxx
		include/crab/network_posix.hxx
		include/crab/network_win.hxx
		include/crab/network_posix_win.hxx
		include/crab/streams.hpp
		include/crab/streams.hxx
		include/crab/util.hpp
		include/crab/util.hxx

		include/crab/crypto/base64.hpp
		include/crab/crypto/sha1.hpp

		include/crab/http/client_request.hpp
		include/crab/http/client_request.hxx
		include/crab/http/connection.hpp
		include/crab/http/connection.hxx
		include/crab/http/crab_tls.hpp
		include/crab/http/crab_tls.hxx
		include/crab/http/request_parser.hpp
		include/crab/http/request_parser.hxx
		include/crab/http/query_parser.hpp
		include/crab/http/query_parser.hxx
		include/crab/http/response_parser.hpp
		include/crab/http/response_parser.hxx
		include/crab/http/server.hpp
		include/crab/http/server.hxx
		include/crab/http/types.hpp
		include/crab/http/types.hxx
		include/crab/http/web_message_parser.hpp
		include/crab/http/web_message_parser.hxx
		)


add_library("${PROJECT_NAME}" STATIC "${SOURCE_FILES}" src/crab.cpp ) # INTERFACE
add_library("${PROJECT_NAME}-header-only" INTERFACE)

add_library( crablib::crablib ALIAS "${PROJECT_NAME}" )
add_library( crablib::crablib-header-only ALIAS "${PROJECT_NAME}-header-only" )

target_compile_definitions("${PROJECT_NAME}" PUBLIC -DCRAB_COMPILE=1)

target_include_directories ( "${PROJECT_NAME}"
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_include_directories ( "${PROJECT_NAME}-header-only"
    INTERFACE  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

find_package (Threads)
target_link_libraries ("${PROJECT_NAME}" Threads::Threads)
