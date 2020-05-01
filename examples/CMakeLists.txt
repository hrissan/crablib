cmake_minimum_required(VERSION 3.0)

project(crablib
	VERSION "0.8"
	DESCRIPTION "Experiments to create cross-platform wrapper for Runloop/Timers/Sockets/ITC with lowest possible overhead on UNIX platforms"
	HOMEPAGE_URL "https://github.com/hrissan/crablib"
	LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 11)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.11")
endif()

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
   set(CMAKE_BUILD_TYPE Release)
endif()
message("CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")

message("-DCRAB_FUZZ=1 builds binaries for fuzzing. Disabled by default because builds only on Linux and Mac OSX (with additional dependencies)")
option(CRAB_FUZZ "builds binaries for fuzzing" OFF)

message("-DCRAB_IMPL_LIBEV=1 builds examples using libev as an implementation")
option(CRAB_IMPL_LIBEV "builds examples using libev as an implementation" OFF)

if(CRAB_IMPL_LIBEV)
	message("-DCRAB_IMPL_LIBEV=1")
	include_directories(/usr/local/include)
	link_directories(/usr/local/lib)
	link_libraries(ev)
	add_definitions(-DCRAB_IMPL_LIBEV=1)
endif()

message("-DCRAB_IMPL_BOOST=1 builds examples using boost as an implementation")
option(CRAB_IMPL_BOOST "builds examples using boost as an implementation" OFF)

if(CRAB_IMPL_BOOST)
	message("-DCRAB_IMPL_BOOST=1")
	add_definitions(-DCRAB_IMPL_BOOST=1)
endif()

message("-DCRAB_IMPL_CF=1 builds examples using Core Foundation as an implementation")
option(CRAB_IMPL_CF "builds examples using Core Foundation as an implementation" OFF)

if(CRAB_IMPL_CF)
	message("-DCRAB_IMPL_CF=1")
	add_definitions(-DCRAB_IMPL_CF=1)
endif()

message("-DCRAB_CRAB_TLS=1 builds example clients with TLS support")
option(CRAB_TLS "builds example clients with TLS support" OFF)

if(CRAB_TLS)
	message("-DCRAB_TLS=1")
	add_definitions(-DCRAB_TLS=1)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin")

if(CRAB_IMPL_BOOST) # Must be before all executables
	set(Boost_USE_STATIC_LIBS ON)
	add_definitions(-DBOOST_BIND_NO_PLACEHOLDERS=1 -DBOOST_CONFIG_SUPPRESS_OUTDATED_MESSAGE=1) # boost::_1 conflicts with std::_1
	add_definitions(-DBOOST_ERROR_CODE_HEADER_ONLY=1 -DBOOST_SYSTEM_NO_DEPRECATED=1) # required for header-only compilation
	add_definitions(-DBOOST_DATE_TIME_NO_LIB=1 -DBOOST_SYSTEM_NO_LIB=1 -DBOOST_REGEX_NO_LIB=1) # required for header-only compilation
	set(Boost_USE_MULTITHREADED OFF) # all boost libraries are multithreaded since some version
	find_package(Boost 1.65)
	if(Boost_FOUND)
		message( STATUS "Boost found by find_boost, Boost_INCLUDE_DIRS: " ${Boost_INCLUDE_DIRS})
		include_directories(${Boost_INCLUDE_DIRS})
	else()
		if(NOT EXISTS "${PARENT_DIR}/boost/boost/version.hpp")
			message(FATAL_ERROR "Boost not found, please download and unpack boost into ${PARENT_DIR}/boost")
		endif()
		set(Boost_INCLUDE_DIRS ${PARENT_DIR}/boost)
		message( STATUS "Using boost from local folder, Boost_INCLUDE_DIRS: " ${Boost_INCLUDE_DIRS})
		include_directories(${Boost_INCLUDE_DIRS})
	endif()
endif()

add_subdirectory ( .. EXCLUDE_FROM_ALL ${CMAKE_CURRENT_BINARY_DIR}/crablib)

link_libraries(crablib::crablib)

find_package (Threads)
link_libraries (Threads::Threads)

if(MSVC)
else()
    add_compile_options(-Wno-noexcept-type)
endif()

if(APPLE)
    link_libraries(-dead_strip  "-framework Foundation -framework CFNetwork")
endif()

add_executable(dns_resolve ${SOURCE_FILES} dns_resolve.cpp)
add_executable(watcher_latency ${SOURCE_FILES} watcher_latency.cpp)

add_executable(http_server_simple ${SOURCE_FILES} http_server_simple.cpp)
add_executable(http_server_longpoll ${SOURCE_FILES} http_server_longpoll.cpp)
add_executable(http_server_stream_body ${SOURCE_FILES} http_server_stream_body.cpp)
add_executable(http_server_multi ${SOURCE_FILES} http_server_multi.cpp)
add_executable(http_server_complex ${SOURCE_FILES} http_server_complex.cpp)

add_executable(client_web_socket ${SOURCE_FILES} client_web_socket.cpp)

add_executable(fair_client ${SOURCE_FILES} fair_client.cpp)
add_executable(fair_server ${SOURCE_FILES} fair_server.cpp)

add_executable(md_tcp_source ${SOURCE_FILES} md_tcp_source.cpp)
add_executable(md_gate ${SOURCE_FILES} md_gate.cpp)
add_executable(md_client ${SOURCE_FILES} md_client.cpp)

# low-level benchmarks and tests
add_executable(reuse_addr ${SOURCE_FILES} lowlevel/reuse_addr.c)
add_executable(udp_eagain ${SOURCE_FILES} lowlevel/udp_eagain.cpp)
add_executable(benchmark_atomic ${SOURCE_FILES} lowlevel/benchmark_atomic.cpp)
add_executable(benchmark_chrono ${SOURCE_FILES} lowlevel/benchmark_chrono.cpp)
add_executable(benchmark_map ${SOURCE_FILES} lowlevel/benchmark_map.cpp)
add_executable(benchmark_getifaddrs ${SOURCE_FILES} lowlevel/benchmark_getifaddrs.cpp)
add_executable(benchmark_atoi ${SOURCE_FILES} lowlevel/benchmark_atoi.cpp)
add_executable(benchmark_random ${SOURCE_FILES} lowlevel/benchmark_random.cpp)

# tests
add_executable(test_atoi ${SOURCE_FILES} ../test/test_atoi.cpp)
add_executable(test_http_parsers ${SOURCE_FILES} ../test/test_http_parsers.cpp ../test/test_http_data.c)

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

	add_executable(fuzz_http_request ${SOURCE_FILES} ../test/fuzz_http_request.cpp)
	SET_TARGET_PROPERTIES(fuzz_http_request PROPERTIES COMPILE_FLAGS "${CRAB_FUZZ_PROPS}")
	SET_TARGET_PROPERTIES(fuzz_http_request PROPERTIES LINK_FLAGS "${CRAB_FUZZ_PROPS}")

	add_executable(fuzz_http_response ${SOURCE_FILES} ../test/fuzz_http_response.cpp)
	SET_TARGET_PROPERTIES(fuzz_http_response PROPERTIES COMPILE_FLAGS "${CRAB_FUZZ_PROPS}")
	SET_TARGET_PROPERTIES(fuzz_http_response PROPERTIES LINK_FLAGS "${CRAB_FUZZ_PROPS}")

	add_executable(fuzz_http_body ${SOURCE_FILES} ../test/fuzz_http_body.cpp)
	SET_TARGET_PROPERTIES(fuzz_http_body PROPERTIES COMPILE_FLAGS "${CRAB_FUZZ_PROPS}")
	SET_TARGET_PROPERTIES(fuzz_http_body PROPERTIES LINK_FLAGS "${CRAB_FUZZ_PROPS}")

	add_executable(fuzz_web_message ${SOURCE_FILES} ../test/fuzz_web_message.cpp)
	SET_TARGET_PROPERTIES(fuzz_web_message PROPERTIES COMPILE_FLAGS "${CRAB_FUZZ_PROPS}")
	SET_TARGET_PROPERTIES(fuzz_web_message PROPERTIES LINK_FLAGS "${CRAB_FUZZ_PROPS}")
endif()

# Examples below are of very low quality and must be rewritten
add_executable(client_simple ${SOURCE_FILES} client_simple.cpp)
add_executable(gate_client_simple ${SOURCE_FILES} gate_client_simple.cpp)
add_executable(gate_server_simple ${SOURCE_FILES} gate_server_simple.cpp)
add_executable(gate_proxy_simple ${SOURCE_FILES} gate_proxy_simple.cpp)
