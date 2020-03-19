// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#pragma once

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace crab {

// uint8_t is our character of choice for all binary reading and writing
typedef std::vector<uint8_t> bdata;
inline void append(bdata &result, const bdata &other) {  // We do this op too often
	result.insert(result.end(), other.begin(), other.end());
}

std::string to_hex(const uint8_t *data, size_t count);
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

	static constexpr size_t length(const char *str) { return *str ? 1U + length(str + 1) : 0U; }
	constexpr int compare(const char *b, size_t bs) const {
		return (size == bs) ? std::memcmp(value, b, size) : size > bs ? 1 : -1;
	}
	constexpr int compare(const std::string &b) const { return compare(b.data(), b.size()); }
};

inline constexpr bool operator==(const std::string &a, const Literal &b) { return b.compare(a) == 0; }
inline constexpr bool operator!=(const std::string &a, const Literal &b) { return !(a == b); }
inline constexpr bool operator==(const Literal &a, const std::string &b) { return a.compare(b) == 0; }
inline constexpr bool operator!=(const Literal &a, const std::string &b) { return !(a == b); }

#define CRAB_LITERAL(value) \
	Literal { value, Literal::length(value) }
// (CRAB_LITERAL == std::string) compile into very little # of instructions
// and they have trivial constructor so no overhead on static initializer per function.
// literals in C++ must be like CRAB_LITERAL, but they are not
// bool compare_with_content_type(const std::string & str){
//     return str == CRAB_LITERAL("content-type");
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
	void set_deterministic(uint32_t seed = 0) { mt.seed(seed); }
	// For tests. Not a constructor, to simplify normal interface (when actual random is required)

	void bytes(uint8_t *buffer, size_t size);
	void bytes(char *buffer, size_t size) { bytes(reinterpret_cast<uint8_t *>(buffer), size); }
	bdata data(size_t size);

	std::string printable_string(size_t size);

	template<typename T>
	T pod() {
		static_assert(std::is_standard_layout<T>::value, "T must be Standard Layout");
		T result{};
		bytes(reinterpret_cast<uint8_t *>(&result), sizeof(result));
		return result;
	}

private:
	// Intent - do not select 64-bit version on 32-bit platforms
	using MT = std::conditional<sizeof(void *) >= 8, std::mt19937_64, std::mt19937>::type;
	using VT = std::conditional<sizeof(void *) >= 8, uint64_t, uint32_t>::type;
	// Too hard to get right type from MT (it uses uint32_fast natively, which can be larger)
	MT mt;
};

}  // namespace crab
