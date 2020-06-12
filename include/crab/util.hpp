// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

// *Really* minimal PCG32 code / (c) 2014 M.E. O'Neill / pcg-random.org
// Licensed under Apache License 2.0 (NO WARRANTY, etc. see website)

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
namespace crab { namespace details {
template<typename T>
using optional = std::optional<T>;
}}  // namespace crab::details
#else
// Simplified, only works for value types
namespace crab { namespace details {
template<typename T>
class optional {
	T impl;
	bool valid = false;

public:
	optional() noexcept = default;
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
}}  // namespace crab::details
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

struct Literal {
	const char *value;
	size_t size;

	template<size_t size>
	constexpr explicit Literal(const char (&value)[size]) : value(value), size(size - 1) {}
	int compare(const char *b, size_t bs) const {
		return (size == bs) ? std::memcmp(value, b, size) : size > bs ? 1 : -1;
	}
	int compare(const std::string &b) const { return compare(b.data(), b.size()); }
};

inline bool operator==(const std::string &a, const Literal &b) { return b.compare(a) == 0; }
inline bool operator!=(const std::string &a, const Literal &b) { return !(a == b); }
inline bool operator==(const Literal &a, const std::string &b) { return a.compare(b) == 0; }
inline bool operator!=(const Literal &a, const std::string &b) { return !(a == b); }

// ==, != of Literal and std::string compile into very little # of instructions
// and they have trivial constructor so no overhead on static initializer per function.
// literals in C++ must be like Literal, but they are not
// bool compare_with_content_type(const std::string & str){
//     return str == Literal{"content-type"};
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

class Random {
public:
	Random();
	explicit Random(uint32_t seed);  // For tests. Hopefully, 2^32 test patterns is enough

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

}  // namespace crab
