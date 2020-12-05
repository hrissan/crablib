// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

// *Really* minimal PCG32 code / (c) 2014 M.E. O'Neill / pcg-random.org
// Licensed under Apache License 2.0 (NO WARRANTY, etc. see website)

// Copyright (c) 2008-2010 Bjoern Hoehrmann <bjoern@hoehrmann.de>
// See http://bjoern.hoehrmann.de/utf-8/decoder/dfa/ for details.

#pragma once

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#if __cplusplus >= 201703L
#include <optional>
namespace crab {
template<typename T>
using optional    = std::optional<T>;
using string_view = std::string_view;
}  // namespace crab
#else
// Simplified, only works for value types
namespace crab {
template<typename T>
class optional {
	T impl;
	bool valid = false;

public:
	optional() = default;
	optional(const optional<T> &other) : impl(other.impl), valid(other.valid) {}
	optional(optional<T> &&other) : impl(std::move(other.impl)), valid(other.valid) {}
	optional(const T &other) : impl(other), valid(true) {}
	optional(T &&other) : impl(std::move(other)), valid(true) {}
	optional<T> &operator=(const optional<T> &other) {
		impl  = other.impl;
		valid = other.valid;
		return *this;
	}
	optional<T> &operator=(optional<T> &&other) {
		impl  = std::move(other.impl);
		valid = other.valid;
		return *this;
	}
	optional<T> &operator=(const T &other) {
		impl  = other;
		valid = true;
		return *this;
	}
	optional<T> &operator=(T &&other) {
		impl  = std::move(other);
		valid = true;
		return *this;
	}
	template<class... Args>
	T &emplace(Args &&... args) {
		impl  = T{std::forward<Args>(args)...};
		valid = true;
		return impl;
	}
	void reset() noexcept { valid = false; }
	constexpr explicit operator bool() const noexcept { return valid; }
	constexpr bool has_value() const noexcept { return valid; }
	T &operator*() { return impl; }
	const T &operator*() const { return impl; }
	T *operator->() { return &impl; }
	const T *operator->() const { return &impl; }
	T &value() { return !valid ? throw std::bad_cast() : impl; }
	const T &value() const { return !valid ? throw std::bad_cast() : impl; }
};

// Simplified subset
class string_view {
public:
	string_view() = default;
	explicit string_view(const char *value) : d(value), s(std::strlen(value)) {}
	constexpr explicit string_view(const char *value, size_t size) : d(value), s(size - 1) {}

	int compare(const char *value, size_t size) const {
		return (s == size) ? std::memcmp(d, value, s) : s > size ? 1 : -1;
	}
	int compare(const std::string &b) const { return compare(b.data(), b.size()); }
	string_view substr(size_t pos = 0, size_t count = std::string::npos) const {
		if (pos > s)
			throw std::out_of_range("string_view::substr pos out of range");
		return string_view(d + pos, std::min(s - pos, count));
	}
	void remove_prefix(size_t count) {
		s -= count;
		d += count;
	}
	void remove_suffix(size_t count) { s -= count; }

	const char *data() const { return d; }
	size_t size() const { return s; }

private:
	const char *d = nullptr;  // We could save on initializing if count = 0, but seems dangerous
	size_t s      = 0;
};

inline bool operator==(const std::string &a, const string_view &b) { return b.compare(a) == 0; }
inline bool operator!=(const std::string &a, const string_view &b) { return !(a == b); }
inline bool operator==(const string_view &a, const std::string &b) { return a.compare(b) == 0; }
inline bool operator!=(const string_view &a, const std::string &b) { return !(a == b); }

// ==, != of string_view and std::string compile into very little # of instructions
// and they have trivial constructor so no overhead on static initializer per function.
// literals in C++ must be like string_view, but they are not
// bool compare_with_content_type(const std::string & str){
//     return str == string_view{"content-type"};
// }
// cmp     qword ptr [rdi + 8], 12
// jne     .LBB0_1
//         mov     rax, qword ptr [rdi]
// movabs  rcx, 3275364211029340003
// xor     rcx, qword ptr [rax]
// mov     eax, dword ptr [rax + 8]
// xor     rax, 1701869940
// or      rax, rcx
//         sete    al
//         ret
// .LBB0_1:
// xor     eax, eax
//         ret

}  // namespace crab
#endif

namespace crab {

// uint8_t is our character of choice for all binary reading and writing
typedef std::vector<uint8_t> bdata;

// C++ lacks common byte type, funs below are safe and shorten common code
// We do not use void *, because that silently allows very unsafe conversions (like passing &std::string)
inline uint8_t *uint8_cast(uint8_t *val) { return val; }
inline const uint8_t *uint8_cast(const uint8_t *val) { return val; }
inline uint8_t *uint8_cast(char *val) { return reinterpret_cast<uint8_t *>(val); }
inline const uint8_t *uint8_cast(const char *val) { return reinterpret_cast<const uint8_t *>(val); }
#if __cplusplus >= 201703L
inline uint8_t *uint8_cast(std::byte *val) { return reinterpret_cast<uint8_t *>(val); }
inline const uint8_t *uint8_cast(const std::byte *val) { return reinterpret_cast<const uint8_t *>(val); }
#endif

inline void append(bdata &result, const bdata &other) {  // We do this op too often
	result.insert(result.end(), other.begin(), other.end());
}

std::string to_hex(const uint8_t *data, size_t count);
inline std::string to_hex(const char *data, size_t count) { return to_hex(uint8_cast(data), count); }
#if __cplusplus >= 201703L
inline std::string to_hex(std::byte *val, size_t count) { return to_hex(uint8_cast(val), count); }
#endif
inline std::string to_hex(const bdata &data) { return to_hex(data.data(), data.size()); }

int from_hex_digit(char sym);
bool from_hex(bdata &data, const std::string &str);

bool is_valid_utf8(const uint8_t *data, size_t count);
inline bool is_valid_utf8(const std::string &str) { return is_valid_utf8(uint8_cast(str.data()), str.size()); }

template<typename T>
T rol(T mask, size_t shift) {
	shift = shift & (sizeof(T) * 8 - 1);
	return (mask << shift) | (mask >> (sizeof(T) * 8 - shift));
}

namespace details {
// Header-only libs cannot contain statics, we use workaround
template<typename T>
struct StaticHolder {
	static T instance;
};

template<typename T>
T StaticHolder<T>::instance{};

template<typename T>
struct StaticHolderTL {
	static thread_local T instance;
};

template<typename T>
thread_local T StaticHolderTL<T>::instance{};

std::string invariant_violated(const char *expr, const char *file, int line, const std::string &msg);

}  // namespace details

class Nocopy {
public:
	Nocopy()               = default;
	Nocopy(const Nocopy &) = delete;
	Nocopy &operator=(const Nocopy &) = delete;
};

#define invariant(expr, msg)                                                                           \
	do {                                                                                               \
		if (!(expr))                                                                                   \
			throw std::logic_error(crab::details::invariant_violated(#expr, __FILE__, __LINE__, msg)); \
	} while (0)

class Random {
public:
	Random();
	explicit Random(uint64_t seed);  // For tests.

	void bytes(uint8_t *buffer, size_t size);
	void bytes(char *buffer, size_t size) { bytes(uint8_cast(buffer), size); }
#if __cplusplus >= 201703L
	void bytes(std::byte *buffer, size_t size) { bytes(uint8_cast(buffer), size); }
#endif
	bdata data(size_t size);

	std::string printable_string(size_t size);
	double double_value();  // [0..1)

	template<typename T>
	T pod() {
		static_assert(std::is_standard_layout<T>::value, "T must be Standard Layout");
		T result{};
		bytes(reinterpret_cast<uint8_t *>(&result), sizeof(result));
		return result;
	}

	// Adapter for C++ std::shuffle, std::uniform_int_distribution, etc.
	typedef uint32_t result_type;
	constexpr static uint32_t min() { return std::numeric_limits<uint32_t>::min(); }
	constexpr static uint32_t max() { return std::numeric_limits<uint32_t>::max(); }
	uint32_t operator()() { return pcg32_random_r(); }

private:
	uint32_t pcg32_random_r();

	uint64_t state = 0;
	uint64_t inc   = 0;
};

#if defined(__GNUC__) || defined(__clang__)
#define CRAB_DEPRECATED __attribute__((deprecated))
#elif defined(_MSC_VER)
#define CRAB_DEPRECATED __declspec(deprecated)
#else
// No deprecation warnings on other compilers
#define CRAB_DEPRECATED
#endif

class scope_exit : private Nocopy {
public:
	explicit scope_exit(std::function<void()> &&cb) : callback(std::move(cb)) {}
	~scope_exit() { callback(); }  // if callback throws, we will exit die to implicit nothrow()
private:
	std::function<void()> callback;
};

void memzero(void *data, size_t len);

}  // namespace crab
