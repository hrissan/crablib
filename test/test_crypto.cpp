// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <array>
#include <crab/crab.hpp>
#include <iostream>

template<typename T>
void check_digest(const std::string &src, const std::string &digest) {
	uint8_t result[T::hash_size]{};
	T{}.add(src.data(), src.size()).finalize(result);
	invariant(crab::to_hex(result, sizeof(result)) == digest, "Digest test failed");

	for (size_t i = 0; i != std::min<size_t>(100, src.size()); ++i) {
		uint8_t result2[T::hash_size]{};
		T{}.add(src.data(), i).add(src.data() + i, src.size() - i).finalize(result2);
		invariant(crab::to_hex(result2, sizeof(result2)) == digest, "Digest split test failed");
	}
}

CRAB_INLINE void mask_data_slow(size_t masking_shift, char *data, size_t size, uint32_t masking_key) {
	auto mask = crab::rol(masking_key, 8 * masking_shift);
	for (size_t i = 0; i != size; ++i) {
		mask = crab::rol(mask, 8);
		data[i] ^= mask;
	}
}

void test_websocket_mask() {
	crab::Random r1(1);
	const auto cdata = r1.data(117);
	std::array<uint32_t, 5> masking_keys{0x01020304, 0xd41d8cd9, 0x1, 0xFFFFFFFF, 0};

	for (auto masking_key : masking_keys)
		for (size_t s = 0; s != 10; ++s)
			for (size_t d = 0; d != 10; ++d)
				for (size_t i = 0; i != cdata.size() - d; ++i) {
					auto data   = cdata;
					size_t size = cdata.size() - d;
					auto ptr    = reinterpret_cast<char *>(data.data()) + d;
					crab::http::WebMessageHeaderParser::mask_data(s, ptr, i, masking_key);
					crab::http::WebMessageHeaderParser::mask_data(s + i, ptr + i, size - i, masking_key);
					auto data2 = cdata;
					auto ptr2  = reinterpret_cast<char *>(data2.data()) + d;
					mask_data_slow(s, ptr2, size, masking_key);
					invariant(data == data2, "test_websocket_mask failed");
				}
}

int main() {
	test_websocket_mask();

	check_digest<crab::md5>("", "d41d8cd98f00b204e9800998ecf8427e");
	check_digest<crab::md5>("a", "0cc175b9c0f1b6a831c399e269772661");
	check_digest<crab::md5>("abc", "900150983cd24fb0d6963f7d28e17f72");
	check_digest<crab::md5>("message digest", "f96b697d7cb7938d525a2f31aaf161d0");
	check_digest<crab::md5>("abcdefghijklmnopqrstuvwxyz", "c3fcd3d76192e4007dfb496cca67e13b");
	check_digest<crab::md5>("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", "d174ab98d277d9f5a5611c2c9f419d9f");
	check_digest<crab::md5>(
	    "12345678901234567890123456789012345678901234567890123456789012345678901234567890", "57edf4a22be3c955ac49da2e2107b67a");

	check_digest<crab::sha1>("", "da39a3ee5e6b4b0d3255bfef95601890afd80709");
	check_digest<crab::sha1>("abc", "a9993e364706816aba3e25717850c26c9cd0d89d");
	check_digest<crab::sha1>("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", "84983e441c3bd26ebaae4aa1f95129e5e54670f1");

	check_digest<crab::sha1>(
	    "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu",
	    "a49b2446a02c645bf419f995b67091253a04a259");
	check_digest<crab::sha1>(std::string(1000000, 'a'), "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
	return 0;
}
