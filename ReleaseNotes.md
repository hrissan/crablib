## Release Notes

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