cmake_minimum_required(VERSION 3.0)

# On mac, if SSL installed via "brew install openssl"
# Parent project must pass option
# cmake -DOPENSSL_ROOT_DIR=/usr/local/opt/openssl ..

# CRAB_TLS=1 uses openssl, install it with
# sudo apt-get install libssl-dev
# brew install openssl

project(crablib
	VERSION "0.8.0"
	DESCRIPTION "Experiments to create cross-platform wrapper for Runloop/Timers/Sockets/ITC with lowest possible overhead on UNIX platforms"
	HOMEPAGE_URL "https://github.com/hrissan/crablib"
	LANGUAGES C CXX)

if(MSVC)
	# TODO - change to target_compile_definitions, otherwise caller will get errors in our headers
    add_definitions(-D_SCL_SECURE_NO_WARNINGS=1 -D_CRT_SECURE_NO_WARNINGS=1) #  -D_WIN32_WINNT=0x0601
    add_compile_options(/we4715) # not all paths return value is error
else()
    add_compile_options(-Wno-noexcept-type)
endif()

# when crc32c instructions are enabled, uncomment
# set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -msse4.2")
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse4.2")

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
		include/crab/crypto/base64.hxx
		include/crab/crypto/crc32.hpp
		include/crab/crypto/crc32.hxx
		include/crab/crypto/md5.hpp
		include/crab/crypto/md5.hxx
		include/crab/crypto/sha1.hpp
		include/crab/crypto/sha1.hxx

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


if(CRAB_TLS)
    message( STATUS "crablib:Crab will support TLS via OpenSSL" )
	target_compile_definitions("${PROJECT_NAME}" PUBLIC -DCRAB_TLS=1)
	target_compile_definitions("${PROJECT_NAME}-header-only" INTERFACE -DCRAB_TLS=1)

	set(OPENSSL_USE_STATIC_LIBS YES)
	find_package(OpenSSL REQUIRED)
	message(STATUS "Using OpenSSL ${OPENSSL_VERSION}")

	target_include_directories("${PROJECT_NAME}" PUBLIC "${OPENSSL_INCLUDE_DIRS}")
	target_include_directories("${PROJECT_NAME}-header-only" INTERFACE "${OPENSSL_INCLUDE_DIRS}")
	target_link_libraries("${PROJECT_NAME}" PUBLIC OpenSSL::SSL OpenSSL::Crypto ${CMAKE_DL_LIBS})
	target_link_libraries("${PROJECT_NAME}-header-only" INTERFACE OpenSSL::SSL OpenSSL::Crypto ${CMAKE_DL_LIBS})
endif()

if(CRAB_IMPL_LIBEV) # Same order as in crab_version.hpp
	message(STATUS "crablib:Crab will use libev impl")
	target_compile_definitions("${PROJECT_NAME}" PUBLIC -DCRAB_IMPL_LIBEV=1)
	target_compile_definitions("${PROJECT_NAME}-header-only" INTERFACE -DCRAB_IMPL_LIBEV=1)
	target_include_directories("${PROJECT_NAME}" PUBLIC "/usr/local/include")
	target_include_directories("${PROJECT_NAME}-header-only" INTERFACE "$/usr/local/include")
	target_link_directories("${PROJECT_NAME}" PUBLIC "/usr/local/lib")
	target_link_libraries("${PROJECT_NAME}" PUBLIC ev)
elseif(CRAB_IMPL_BOOST)
	message(STATUS "crablib:Crab will use boost::asio impl, parent project must have boost already set up - headers, link libraries, etc.")
	target_compile_definitions("${PROJECT_NAME}" PUBLIC -DCRAB_IMPL_BOOST=1)
	target_compile_definitions("${PROJECT_NAME}-header-only" INTERFACE -DCRAB_IMPL_BOOST=1)
elseif(CRAB_IMPL_CF)
	message(STATUS "crablib:Crab will use Core Foundation impl, will work on Apple platforms only")
	target_compile_definitions("${PROJECT_NAME}" PUBLIC -DCRAB_IMPL_CF=1)
	target_compile_definitions("${PROJECT_NAME}-header-only" INTERFACE -DCRAB_IMPL_CF=1)
    target_link_libraries("${PROJECT_NAME}" PUBLIC -dead_strip  "-framework Foundation -framework CFNetwork")
    target_link_libraries("${PROJECT_NAME}-header-only" INTERFACE -dead_strip  "-framework Foundation -framework CFNetwork")
else()
	message(STATUS "crablib:Crab will use native impl - kevent on OS X, epoll on Linux, Overlapped I/O on Windows")
endif()

target_compile_definitions("${PROJECT_NAME}" PUBLIC -DCRAB_COMPILE=1)

target_include_directories("${PROJECT_NAME}" PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories("${PROJECT_NAME}-header-only" INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")

find_package (Threads)
target_link_libraries ("${PROJECT_NAME}" PUBLIC Threads::Threads)
target_link_libraries ("${PROJECT_NAME}-header-only" INTERFACE Threads::Threads)
