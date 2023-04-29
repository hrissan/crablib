## Release Notes

### (Next Release)

### 0.9.3

- close with event for `TCPSocket` is now publicly accessible
- `RunLoop` instance now contains public instance of `Random` with the name `rnd`
- some linter warnings removed from code
- CRC32 and CRC32C added to crypto algorithms

### 0.9.2

- fixed bug when HTTP Time header was cached by server for 500 seconds, not milliseconds
- example of fair resource-bound API server. TODO - improve example
- `SignalStop` renamed to `Signal` and now accepts list of signals. Empty list means to detect generic `Ctrl-C` and sorta works on `Linux`, `Mac` and `Windows`.
- `crab::Thread` to wrap creation of `std::thread` with RunLoop and perform raceless cancellation
- Recommended `Random` seeding
- Web Socket masking is now 8x faster
- MD5 added to crypto algorithms

*Bugs fixed*
- `sigprocmask` was changed `pthread_sigmask`, so that `Signal` work in multithreaded apps

### 0.9.1

*Low level*
- `optional` moved out of `details` namespace, tiny fix in optional

*Bugs fixed*
- When setting socket options to 1, they were set to uninitialized value instead. Worked because luck plus `true = anything non-zero`.  

### 0.9.0

*Low level*
- Experimental TLS code (not for use in a wild yet)
- Simple crab::optional for using older C++ standard
- Random is adapted to std containers 

*HTTP*
- Streaming of HTTP bodies (both upload and download)  
- New HTTP URI parser by AK

*Bugs fixed*
- WebSockets now support control frames between message chunks

*Incompatible changes*
- New callbacks design for WebSockets and postponing normal HTTP responses
- Stricter integer parser might break existing products

### 0.8.0

*Low level*
- New expeimental low-level implementation added, `CRAB_IMPL_LIBEV` over `libev` (requires `sudo apt install libev-dev`).
- `CFRunLoop` low-level implementaion merged (partial support for `TLS` for now, full support is expected in near future).
- `crab::Random` now uses PCG32 instead of `std::mt19337`. Slow construction and number generation of `std::mt19337` consumed too much CPU for lots of very short Web Messages.

*HTTP*
- To avoid some `NATs` disconnecting Web Sockets, `OPCODE_PING` is sent every 45 seconds (Timeout selected to be a bit less than TCP keepalive, which is 50 seconds for major browsers)
- New HTTP Query parser by AK
- New HTTP Cookie parser by AK

*Bugs fixed*
- HTTP socket shutdown was performed incorrectly for some `HTTP/1.0` clients

*Incompatible API changes*
- sha1 and base64 moved into `crab` namespace

### Before 0.8.0

`ReleaseNotes.md` is new in version 0.8.0. Before that, commit history served as a release notes. 
