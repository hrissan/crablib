cmake_minimum_required(VERSION 3.0)
project(crablib)

if(WIN32)
#	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Ot")
    add_definitions(-D_SCL_SECURE_NO_WARNINGS=1 -D_CRT_SECURE_NO_WARNINGS=1 -D_WIN32_WINNT=0x0601)
    add_compile_options(/we4715) # not all paths return value is error
else()
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Werror=return-type")
	set(CMAKE_CXX_FLAGS_DEBUG "-O0")
	set(CMAKE_CXX_FLAGS_RELEASE "-O3")
	set(CMAKE_C_COMPILER_WORKS 1)
	set(CMAKE_CXX_COMPILER_WORKS 1)
endif()

set(CMAKE_CXX_STANDARD 11)

include_directories(SYSTEM
		include)

#set(HEADERS

set(SOURCE_FILES
		include/crab/crab.cpp
		include/crab/crab_version.hpp
		include/crab/crab.hpp
		include/crab/intrusive_list.hpp
		include/crab/intrusive_heap.hpp
		include/crab/network.hpp
		include/crab/network.hxx
		include/crab/network_base.hpp
		include/crab/network_boost.hxx
		include/crab/network_posix.hxx
		include/crab/network_win.hxx
		include/crab/streams.hpp
		include/crab/streams.hxx
		include/crab/util.hpp
		include/crab/util.hxx

		include/crab/crypto/base64.hpp
		include/crab/crypto/sha1.hpp

		include/crab/http/connection.hpp
		include/crab/http/connection.hxx
		include/crab/http/request_parser.hpp
		include/crab/http/request_parser.hxx
		include/crab/http/response_parser.hpp
		include/crab/http/response_parser.hxx
		include/crab/http/server.hpp
		include/crab/http/server.hxx
		include/crab/http/types.hpp
		include/crab/http/types.hxx
		include/crab/http/web_message_parser.hpp
		include/crab/http/web_message_parser.hxx
		include/crab/http/web_socket.hpp
		include/crab/http/web_socket.hxx
		)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin")

if(WIN32)
    link_libraries(ws2_32 wsock32)
else()
	link_libraries(pthread)
endif(WIN32)

add_executable(dns_resolve ${SOURCE_FILES} examples/dns_resolve.cpp)
add_executable(watcher_latency ${SOURCE_FILES} examples/watcher_latency.cpp)

add_executable(http_server_simple ${SOURCE_FILES} examples/http_server_simple.cpp)
add_executable(http_server_longpoll ${SOURCE_FILES} examples/http_server_longpoll.cpp)
add_executable(http_server_multi ${SOURCE_FILES} examples/http_server_multi.cpp)
add_executable(http_server_complex ${SOURCE_FILES} examples/http_server_complex.cpp)

add_executable(fair_client ${SOURCE_FILES} examples/fair_client.cpp)
add_executable(fair_server ${SOURCE_FILES} examples/fair_server.cpp)

add_executable(md_tcp_source ${SOURCE_FILES} examples/md_tcp_source.cpp)
add_executable(md_gate ${SOURCE_FILES} examples/md_gate.cpp)
add_executable(md_client ${SOURCE_FILES} examples/md_client.cpp)

# low-level benchmarks and tests
add_executable(udp_eagain ${SOURCE_FILES} examples/lowlevel/udp_eagain.cpp)
add_executable(benchmark_atomic ${SOURCE_FILES} examples/lowlevel/benchmark_atomic.cpp)
add_executable(benchmark_chrono ${SOURCE_FILES} examples/lowlevel/benchmark_chrono.cpp)
add_executable(benchmark_map ${SOURCE_FILES} examples/lowlevel/benchmark_map.cpp)
add_executable(benchmark_getifaddrs ${SOURCE_FILES} examples/lowlevel/benchmark_getifaddrs.cpp)

# Examples below are of very low quality and must be rewritten
add_executable(client_simple ${SOURCE_FILES} examples/client_simple.cpp)
add_executable(gate_client_simple ${SOURCE_FILES} examples/gate_client_simple.cpp)
add_executable(gate_server_simple ${SOURCE_FILES} examples/gate_server_simple.cpp)
add_executable(gate_proxy_simple ${SOURCE_FILES} examples/gate_proxy_simple.cpp)
