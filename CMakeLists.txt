cmake_minimum_required(VERSION 3.0)
project(crablib)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
   set(CMAKE_BUILD_TYPE Release)
endif()
message("CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")

message("-DCRAB_FUZZ=1 builds binaries for fuzzing. Disabled by default because builds only on Linux and Mac OSX (with additional dependencies)")
option(CRAB_FUZZ "builds binaries for fuzzing" OFF)

# add_definitions(-DCRAB_COMPILE=1)
# add_definitions(-DCRAB_IMPL_BOOST=1)
add_definitions(-DCRAB_IMPL_LIBEV=1)
link_directories(/usr/local/lib)
link_libraries(ev)

if(WIN32)
#	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Ot")
    add_definitions(-D_SCL_SECURE_NO_WARNINGS=1 -D_CRT_SECURE_NO_WARNINGS=1 -D_WIN32_WINNT=0x0601)
    add_compile_options(/we4715) # not all paths return value is error
else()
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wall -Wextra -Werror=return-type -Wno-unused-parameter")
	# Also usefull flags -Wold-style-cast -Wconversion
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
		include/crab/integer_cast.hpp
		include/crab/intrusive_list.hpp
		include/crab/intrusive_heap.hpp
		include/crab/network.hpp
		include/crab/network.hxx
		include/crab/network_base.hpp
		include/crab/network_boost.hxx
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

set(Boost_USE_STATIC_LIBS ON)
add_definitions(-DBOOST_BIND_NO_PLACEHOLDERS=1 -DBOOST_CONFIG_SUPPRESS_OUTDATED_MESSAGE=1) # boost::_1 conflicts with std::_1
add_definitions(-DBOOST_ERROR_CODE_HEADER_ONLY=1 -DBOOST_SYSTEM_NO_DEPRECATED=1) # required for header-only compilation
add_definitions(-DBOOST_DATE_TIME_NO_LIB=1 -DBOOST_SYSTEM_NO_LIB=1 -DBOOST_REGEX_NO_LIB=1) # required for header-only compilation
set(Boost_USE_MULTITHREADED OFF) # all boost libraries are multithreaded since some version
find_package(Boost 1.65)
if(Boost_FOUND)
	message( STATUS "Boost found by find_boost, Boost_INCLUDE_DIRS: " ${Boost_INCLUDE_DIRS})
else()
	if(NOT EXISTS "${PARENT_DIR}/boost/boost/version.hpp")
		message(FATAL_ERROR "Boost not found, please download and unpack boost into ${PARENT_DIR}/boost")
	endif()
	set(Boost_INCLUDE_DIRS ${PARENT_DIR}/boost)
	message( STATUS "Using boost from local folder, Boost_INCLUDE_DIRS: " ${Boost_INCLUDE_DIRS})
endif()
include_directories(${Boost_INCLUDE_DIRS})

if(WIN32)
	# set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /INCREMENTAL:NO")
else()
	link_libraries(pthread)
endif(WIN32)

add_executable(dns_resolve ${SOURCE_FILES} examples/dns_resolve.cpp)
add_executable(watcher_latency ${SOURCE_FILES} examples/watcher_latency.cpp)

add_executable(http_server_simple ${SOURCE_FILES} examples/http_server_simple.cpp)
add_executable(http_server_longpoll ${SOURCE_FILES} examples/http_server_longpoll.cpp)
add_executable(http_server_stream_body ${SOURCE_FILES} examples/http_server_stream_body.cpp)
add_executable(http_server_multi ${SOURCE_FILES} examples/http_server_multi.cpp)
add_executable(http_server_complex ${SOURCE_FILES} examples/http_server_complex.cpp)

add_executable(fair_client ${SOURCE_FILES} examples/fair_client.cpp)
add_executable(fair_server ${SOURCE_FILES} examples/fair_server.cpp)

add_executable(md_tcp_source ${SOURCE_FILES} examples/md_tcp_source.cpp)
add_executable(md_gate ${SOURCE_FILES} examples/md_gate.cpp)
add_executable(md_client ${SOURCE_FILES} examples/md_client.cpp)

# low-level benchmarks and tests
add_executable(reuse_addr ${SOURCE_FILES} examples/lowlevel/reuse_addr.c)
add_executable(udp_eagain ${SOURCE_FILES} examples/lowlevel/udp_eagain.cpp)
add_executable(benchmark_atomic ${SOURCE_FILES} examples/lowlevel/benchmark_atomic.cpp)
add_executable(benchmark_chrono ${SOURCE_FILES} examples/lowlevel/benchmark_chrono.cpp)
add_executable(benchmark_map ${SOURCE_FILES} examples/lowlevel/benchmark_map.cpp)
add_executable(benchmark_getifaddrs ${SOURCE_FILES} examples/lowlevel/benchmark_getifaddrs.cpp)
add_executable(benchmark_atoi ${SOURCE_FILES} examples/lowlevel/benchmark_atoi.cpp)
add_executable(benchmark_random ${SOURCE_FILES} examples/lowlevel/benchmark_random.cpp)

# tests
add_executable(test_atoi ${SOURCE_FILES} test/test_atoi.cpp)
add_executable(test_http_parsers ${SOURCE_FILES} test/test_http_parsers.cpp test/test_http_data.c)

if(CRAB_FUZZ)
	# fuzzing
	# to compile with fuzzing On Mac OSX, run cmake after
	# brew install llvm
	# export CC=/usr/local/opt/llvm/bin/clang
	# export CXX=/usr/local/opt/llvm/bin/clang++
	# to run
	# ../bin/fuzz_http_request ../test/HTTP_REQUEST_CORPUS/ -dict=../test/HTTP_DICT.txt -jobs=8 -workers=8
	# ../bin/fuzz_http_response ../test/HTTP_RESPONSE_CORPUS/ -dict=../test/HTTP_DICT.txt -jobs=8 -workers=8
	# ../bin/fuzz_http_body ../test/HTTP_BODY_CORPUS/ -dict=../test/HTTP_DICT.txt -jobs=8 -workers=8
	# ../bin/fuzz_web_message ../test/WEB_MESSAGE_CORPUS/ -dict=../test/HTTP_DICT.txt -jobs=8 -workers=8
	# to reduce file set, move HTTP_BODY_CORPUS content into MERGE, then run
	# ../bin/fuzz_http_body ../test/HTTP_BODY_CORPUS/ ../test/MERGE -merge=1
	set(CRAB_FUZZ_PROPS "-fno-omit-frame-pointer -fsanitize=fuzzer,address,undefined")

	add_executable(fuzz_http_request ${SOURCE_FILES} test/fuzz_http_request.cpp)
	SET_TARGET_PROPERTIES(fuzz_http_request PROPERTIES COMPILE_FLAGS "${CRAB_FUZZ_PROPS}")
	SET_TARGET_PROPERTIES(fuzz_http_request PROPERTIES LINK_FLAGS "${CRAB_FUZZ_PROPS}")

	add_executable(fuzz_http_response ${SOURCE_FILES} test/fuzz_http_response.cpp)
	SET_TARGET_PROPERTIES(fuzz_http_response PROPERTIES COMPILE_FLAGS "${CRAB_FUZZ_PROPS}")
	SET_TARGET_PROPERTIES(fuzz_http_response PROPERTIES LINK_FLAGS "${CRAB_FUZZ_PROPS}")

	add_executable(fuzz_http_body ${SOURCE_FILES} test/fuzz_http_body.cpp)
	SET_TARGET_PROPERTIES(fuzz_http_body PROPERTIES COMPILE_FLAGS "${CRAB_FUZZ_PROPS}")
	SET_TARGET_PROPERTIES(fuzz_http_body PROPERTIES LINK_FLAGS "${CRAB_FUZZ_PROPS}")

	add_executable(fuzz_web_message ${SOURCE_FILES} test/fuzz_web_message.cpp)
	SET_TARGET_PROPERTIES(fuzz_web_message PROPERTIES COMPILE_FLAGS "${CRAB_FUZZ_PROPS}")
	SET_TARGET_PROPERTIES(fuzz_web_message PROPERTIES LINK_FLAGS "${CRAB_FUZZ_PROPS}")
endif()

# Examples below are of very low quality and must be rewritten
add_executable(client_simple ${SOURCE_FILES} examples/client_simple.cpp)
add_executable(gate_client_simple ${SOURCE_FILES} examples/gate_client_simple.cpp)
add_executable(gate_server_simple ${SOURCE_FILES} examples/gate_server_simple.cpp)
add_executable(gate_proxy_simple ${SOURCE_FILES} examples/gate_proxy_simple.cpp)
