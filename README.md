# How to include in your project

## If using cmake

```
add_subdirectory (some_path/crablib)

link_libraries(crablib::crablib)
```

## plain and simple

```
$myproject> g++ -Isome_path/crablib/include -std=c++11 mydemo.cpp -o mydemo
```

You only need a path to `crablib/include`, for example to compile simple http demo
```
$crablib> g++ -Iinclude -std=c++11 examples/http_server_simple.cpp -o http_server_simple

```

# How to build and run examples

```
$crablib/examples> mkdir build
$crablib/examples> cd build
$crablib/examples/build> cmake ..
$crablib/examples/build> time make -j4
$crablib/examples/build> ../bin/http_server_simple
```

In some other console
```
$crablib> curl http://127.0.0.1:7000/
Hello, Crab!
```

By default, crab is header-only and depends only on `std` and system headers. If you wish faster build time, you can define `CRAB_COMPILE=1` and add crab.cpp to your project.

Crab compiles with C++11, crab is mostly used in production on Linux, developed on Mac OSX, and occasionally checked to still be working on Windows. 

# What is crab

Crab is a more or less classic implementation of runloop, with timers and network designed to be as thin as possible wrapper around UNIX sockets, while still providing the same interface on Windows via native implememntation or boost::asio.

At some point, crab predecessor was used for all network/async code built by small software development company I worked for.

Then it was completely redesigned/rewritten, when lambdas, `thread_local`, concurrency were added to C++ standard.

A project using crab was successfully ported to iOS using CFRunLoopTimer, CFStreamCreatePairWithSocketToCFHost, etc at system interface level (Hope to merge that branch to master some day).

A project using crab derivative was successfully ported to Web Assembly, using emscripten_async_call for timers, EM_ASM_INT to interface to JS http requests and Web Workers. This was a bit of stretch, because required customizing at the level above network, but it worked.

# Which project types crab was/is succesfully used in

Very fast HTTP to custom protocol proxy.

Json RPC API server (multiple projects).

Implementation of popular P2P protocol network node.

Web application backend which used long-polling (before web sockets were widely supported on iOS/Android) to implement stream of updates from the server.   

Lower-than-average latency bridges/gates with fairness requirements.


# What crab is not

Crab is in no way generic. For now it does not have built-in SSL, neither server-side, nor client-side. 

When used as a HTTP-server, it was always set up behind SSL termination by nginx or Amazon load-balancer.

There was custom protocol-specific encryption/certification when it was used as a P2P node.

It was used in protected environment, when acting as a bridge/gate.

Crab focus is not on HTTP server, but on low-level network and timers. HTTP server was implemented as a test of concept, then suddenly started to be used in actual projects.

There is currently a private crab branch with SSL support via openssl, but it adds dependency and requires platform-specific code to look for certification chains (On Linux, certificates can exist in bunch of places). Project using crab with SSL is just compiled with a list of popular certification centres as trusted roots, I have to investigate all pros and cons of this approach before merging SSL into master.  

## Design

# multithreading

Crab was designed to have a runloop per thread, and pointer to per thread runloop instance is stored in `thread_local` variable, objects are created in that runloop automatically. You can access them only from that thread (the only exception is Watcher::call).

I personally prefer multi-threaded environments with tight thread incapsulation and limited state sharing between threads (similar to Web Workers).  

If some heavy processing tasks are required, black box with a thread loop inside is created, work is added to queue protected with `std::mutex`, then result readiness is communicated to callers via `Watcher`.

Most cases are with single caller/multiple workers, like in `crab_heavy_lifting.cpp` (TODO - rewrite and add this example to repository)

When multiple callers are required, code is a bit more complicated, for an example, see `details::DNSWorker` in crab sources.
 

# shared_from_this, using shared pointers, lifetimes

Unlike `boost::asio`, `uv_lib`, crab has much simpler object lifetime rules regarding callbacks.

If you call cancel() on some object, you will not get any callbacks from it, independent of the state it was in.

There is no "callbacks in flight" that you cannot cancel.

All objects fully cancel all asyncronous ops in destructor, so you can have unique_ptr or just object instance itself, and destroy it as soon as you do not need it.

This greatly simplifies design of application components, because you can directly go from any state to any other state. If you have a queue of socket instances and you no more need one, you can just remove it from queue, destroying immediately, and so on. Application built from such components also automatically inherits this property.

Experience of writing complex asynchronous logic using crab (like multi-peer P2P downloader) prove this design choice is right.  

When calling every callback, each crab lower level object expects that it or any other object will be destroyed in callback invocation. That is why there is no loops calling callbacks inside crab lower level.

This design is very good for lower-level components, but sometimes leads to less than perfect API for higher-level components, so for example HTTP server violates this, has a loop inside handler, and does not allow to destroy HTTP server itself from the callback.

Also, exception is runloop itself. You cannot destroy runloop while some objects created in its thread are still live. This is usually accomplished by creating runloop instance at the start of `main` and all thread functions.

When crab is implemented over boost::asio or windows overlapped IO, which do not allow to immediately cancel asynchronous operation, crab uses `trampolines` with pointer to original object, owned by both object and pending asynchronous operations (see `network_win.hxx`, for example). If object is destroyed, pointer in trampoline is set to nullptr, then when the last synchronous operation eventually completes, trampoline is also destroyed.

Trampolines use simple and fast `unique_ptr`, not slow (due to `atomic` counter) `shared_ptr`. Runloop waits for all trampolines to be destroyed in its destructor, so there is no memory leaks if runloop itself is destroyed before operations are completed.

# on design of callbacks

In general, crab callbacks only signal, but do not pass data. For example, socket just tells that there is data/buffer space available, but does not read data for you.

This is a design choice. crab is often used to implement various pipelines, where you must apply back pressure to control rate of data. So if system is heavy loaded, socket data tend to stay in system network buffers, throttling senders due to design of TCP window.

Same principle applies to HTTP connection, for example. In socket callback is reads amount of data from the socket enough to construct only single request, then calls handler. More data will be read from the socket buffer only when handler finishes processing this request and calls `read_request()` for the next one.   

# repeating timers

Crab does not have repeating timers, you just call once() in timer callback if you need to.

Arguably this design leads to less logic in application components, than more common approach with `repeat()`, then `cancel()` when timer is no more needed.

This design (together with rounding up runloop sleep time) leads to timer creep (less than requested average ticks per second), This can be addressed by advancing timer clock relative to previous value, instead of `now()`, if `once()` called from callback. The problem with stable timers, though, that they tend to stress system, if set to low value. I prefer to use `Timer` as a signalling mechanism only. If I need to measure time, I use clock.

You can of course `cancel()` or destroy crab timer any time you wish, and use `is_set` in an application logic. 

# Memory efficiency

Crab uses intrusive lists to track objects to minimize allocations.

Timers must be sorted, so they use intrusive binary heap. 

Crab stored timers in skip list for years, then in `std::set`. Benchmarks show that heap is 4x faster than `std::set` which is 5x faster than skiplist. There is rumors that 4-way or 8-way heap is even faster, may be I should look into it.

Crab timers also have very useful optimization for the case when set timer is moved further into future (for example, some timeout is reset on every incoming packet).
In this case new fire_time is remembered as a `moved_fire_time`, but heap is not touched. Instead, when timer naturally moves to the front of the heap and is ready to fire (`fire_time <= now`), 
if `moved_fire_time > now` timer is rescheduled, otherwise fires normally. With this optimization timers reach speed of 100 million resets into future per second on modern PC. 
 

# syscall efficiency

Crab reads lots (512 for now) of signalled objects from `epoll()` and `kevent()` at once, adds them all to an (intrusive) list, then repeatedly pops list head and calls its callback until list is empty.

As each object is removed from all intrusive lists in destructor, this leads to clean and fast implementation, each callback can destroy any subset of objects, including those read from `epoll` in the same batch after current one.

I have seen some popular libraries just `SEGFAULT`ing in this case, some other libraries make `epoll()` call per signalled object, greatly degrading performance when using lots of sockets at once.

# busy polling, tricks to reduce latency

Because callbacks in crab only signal, but do not carry information, a user can theoretically set an `Idle` handler and use it to continously read a subset of sockets (Internals must be tweaked for that to ignore `can_read` flags).

When experimenting on generic hardware, no latency drop was observed in this mode. Author has no access to SolarFlare-like hardware to play with, so the question adapting crab to busy-polling remains open. 

# TCP_NODELAY

crab enables this by default on all sockets. Motivation - TCP delay is not a solution if user fails to prepare data for sending in bulk quantities

# shutdown versus close

Correctly implementing HTTP `connection: close` is surprisingly difficult. shutdown() is platform-dependent, and can discard data which did not reach client.

crab TCPSocket write_shutdown() is just a wrapper around lower-level function, while http::Connection implements all state changes/checks necessary to wait for all data to be written to socket buffer, shutdown, then wait until client closes socket.

Contract is that client sends no more data during wait, so if it does, connection will be immediately closed.

# HTTP Server conformance - unknown, probably mediocre

HTTP standard is utter crap, every tiny feature is broken.

Example - there is a bunch of rarely used features, like trailers.

Example - many HTTP headers have unique parser grammar.

Example - you cannot easily know if request/response has body or not (depends on lots of factors).

Parts that were working for some time can suddenly stop working. For example, Chrome always sent 'connection: upgrade' when connecting to web sockets, but since some version started sending 'connection: upgrade, keep-alive' (WHAAT?), so web socket stopped working until patched.

Server was endlessly tweaked in the past and will be tweaked in foreseeable future.

# HTTP Server security - redesign in progress

HTTP parsers were fuzzed for some time, no crashes/undefined behaviour was found.

Crab HTTP was never used to download, upload arbitrary large files, etc, being used for Json RPC API and proxying of small messages.

So there was just set a limit on # of connections and on every connection no request was read before response was sent. Each request header and body size was also limited.

For longpoll app backend limit on # of connections was 200K, while request size was limited to 4KB.

For Json RPC API limit on # of connections was 1K, while request size was limited to 1MB.

For other projects, limit were also customized directly in crab sources, with some tweaks.

During HTTP server redesign, some of those simple checks were removed.

Now the limits are slowly added back again, with goal to limit total memory used by the server (500 MB for example), with customization of this and other limits.

In regards to connections left behind by clients without FIN, there were experiments with both application-level timeouts and TCP keep-alives.

Application-level timeouts worked always, but were less efficient, than TCP keep-alives, which did not work when closing MacBook lid though :(:(:(. Currently this is a big TODO.

Some ideas are described here (Linux-specific)) https://blog.cloudflare.com/when-tcp-sockets-refuse-to-die/

# HTTP Server - streaming, long-polling, WebSocket etc.

Currently, the server reads the whole request body, then creates the whole response body. This is good for small requests and responses, but cannot be used to upload/download large files, for example.

The plans are to tweak HTTP server interface to allow both request body and response body streaming. (Experimental response streaming is added in `dev` branch)

HTTP server is currently very good for long-polling, if you choose to delay response, you just save `Client *` to your data structure, then either client disconnects and you remove it from your data structure in 'd_handler', or you become ready to send a response and you just do it. (See `http_server_longpoll.cpp` in examples).

WebSockets are very recent experimental implementation, and currently not being used in any production environment. 

# exceptions in callbacks - redesign in progress

Crab is in process of rework in regard to exception handling in callbacks. Early versions required that users handle all exceptions, otherwise it will fly up from crab runloop run() method.

The code to disconnect socket in case handler has thrown exception was removed, because it violated "you can destroy any object in any handler" principle. If client has already destroyed socket, we cannot schedule on close callback, and scheduling callback before handler call and removing afterward (if soecket was not destroyed) is slow and unnecessary in 100% cases for most applications.

Then, there was code to catch and ignore all exceptions from handlers, this code is also removed for now.

For now, clients should use try-block in handlers, otherwise the exception will be thrown out of `RunLoop::run()` function.

# UDP

For the long time, UDP was only used to make local service discoveries. With stateful receiver automatically listening on all interfaces and stateless transmitter (static function), transmitting each packet also on all interfaces.

This old-style UDP class is removed from main branches for now, and UDP will slowly be improved to offer more high-speed implementation with interface similar to TCPSocket.

# Other async - Files, etc

Crab has low-level plugs into RunLoop, as a common interface to OS async mechanism used. Unfortunately it is platform-specific.

Probably, an example of asynchronous file reader should be added to examples.

# Commit history is very short

Secrets were accidentally committed.

So repository had to be wiped, then added again.

