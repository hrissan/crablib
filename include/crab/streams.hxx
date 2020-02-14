// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <algorithm>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include "streams.hpp"

namespace crab {

CRAB_INLINE void IStream::read(uint8_t *val, size_t count) {
	while (count != 0) {
		size_t rc = read_some(val, count);
		if (rc == 0)
			throw std::runtime_error("crab::IStream reading from empty stream");
		val += rc;
		count -= rc;
	}
}

CRAB_INLINE void IStream::read(char *val, size_t count) { read(reinterpret_cast<uint8_t *>(val), count); }

CRAB_INLINE void OStream::write(const uint8_t *val, size_t count) {
	while (count != 0) {
		size_t wc = write_some(val, count);
		if (wc == 0)
			throw std::runtime_error("crab::OStream writing to full stream");
		val += wc;
		count -= wc;
	}
}

CRAB_INLINE void OStream::write(const char *val, size_t count) {
	write(reinterpret_cast<const uint8_t *>(val), count);
}

CRAB_INLINE size_t Buffer::read_some(uint8_t *val, size_t count) {
	size_t rc = std::min(count, read_count());
	std::memcpy(val, read_ptr(), rc);
	did_read(rc);
	return rc;
}

CRAB_INLINE size_t Buffer::write_some(const uint8_t *val, size_t count) {
	size_t rc = std::min(count, write_count());
	std::memcpy(write_ptr(), val, rc);
	did_write(rc);
	return rc;
}

CRAB_INLINE size_t Buffer::read_from(IStream &in) {
	size_t total_count = 0;
	while (true) {
		size_t wc = write_count();
		if (wc == 0)
			break;
		size_t count = in.read_some(write_ptr(), wc);
		did_write(count);
		total_count += count;
		if (count == 0)
			break;
	}
	return total_count;
}

CRAB_INLINE size_t Buffer::write_to(OStream &out, size_t max_count) {
	size_t total_count = 0;
	while (true) {
		size_t rc = std::min(read_count(), max_count);
		if (rc == 0)
			break;
		size_t count = out.write_some(read_ptr(), rc);
		did_read(count);
		max_count -= count;
		total_count += count;
		if (count == 0)
			break;
	}
	return total_count;
}

/*
// Invariant - at least 1 chunk in rope container to avoid ifs
Rope::Rope(size_t chunk_size) : chunk_size(chunk_size) {
    rope.emplace_back(chunk_size);
}

void Rope::clear() {
    if (rope.size() >= 1) {
        Buffer buf = std::move(rope.front());
        rope.clear();
        rope.push_back(std::move(buf));
    } else {
        rope.clear();
        rope.emplace_back(chunk_size);
    }
    rope_size = 0;
}

void Rope::did_write(size_t count) {
    rope_size += count;
    rope.back().did_write(count);
    if (rope.back().full())
        rope.emplace_back(chunk_size);
}

void Rope::did_read(size_t count) {
    invariant(count <= rope_size, "Rope underflow");
    rope_size -= count;
    rope.front().did_read(count);
    if (rope.front().empty()) {
        rope.pop_front();
        if (rope.empty())
            rope.emplace_back(chunk_size);
    }
}

size_t Rope::read_from(IStream &in) {
    size_t total_count = 0;
    while (true) {
        size_t wc = write_count();
        if (wc == 0)
            break;
        size_t count = in.read_some(write_ptr(), wc);
        did_write(count);
        total_count += count;
        if (count == 0)
            break;
    }
    return total_count;
}

size_t Rope::write_to(OStream &out, size_t max_count) {
    size_t total_count = 0;
    while (true) {
        size_t rc = std::min(read_count(), max_count);
        if (rc == 0)
            break;
        size_t count = out.write_some(read_ptr(), rc);
        did_read(count);
        max_count -= count;
        total_count += count;
        if (count == 0)
            break;
    }
    return total_count;
}*/

/*void Buffer::write(const uint8_t *val, size_t count) {
    size_t rc = std::min(count, write_count());
    std::memcpy(write_ptr(), val, rc);
    did_write(rc);
    val += rc;
    count -= rc;
    if (count == 0)
        return;
    rc = std::min(count, write_count());
    std::memcpy(write_ptr(), val, rc);
    did_write(rc);
    val += rc;
    count -= rc;
    if (count == 0)
        return;
    throw std::runtime_error("Buffer overflow");
}

void Buffer::write(const char *val, size_t count) {
    write(reinterpret_cast<const uint8_t *>(val), count);
}*/

CRAB_INLINE size_t IMemoryStream::read_some(uint8_t *val, size_t count) {
    size_t rc = std::min(count, si);
    std::memcpy(val, data, rc);
    data += rc;
    si -= rc;
    return rc;
}

CRAB_INLINE size_t IMemoryStream::write_to(OStream &out, size_t max_count) {
    size_t total_count = 0;
    while (true) {
        size_t rc = std::min(size(), max_count);
        if (rc == 0)
            break;
        size_t count = out.write_some(data, rc);
        data += count;
        si -= rc;
        max_count -= count;
        total_count += count;
        if (count == 0)
            break;
    }
    return total_count;
}

CRAB_INLINE size_t OMemoryStream::write_some(const uint8_t *val, size_t count) {
    size_t rc = std::min(count, si);
    std::memcpy(data, val, rc);
    data += rc;
    si -= rc;
    return rc;
}

CRAB_INLINE size_t IVectorStream::read_some(uint8_t *val, size_t count) {
	size_t rc = std::min(count, rimpl->size() - read_pos);
	std::memcpy(val, rimpl->data() + read_pos, rc);
	read_pos += rc;
	return rc;
}

CRAB_INLINE size_t IVectorStream::write_to(OStream &out, size_t max_count) {
	size_t total_count = 0;
	while (true) {
		size_t rc = std::min(size(), max_count);
		if (rc == 0)
			break;
		size_t count = out.write_some(rimpl->data() + read_pos, rc);
		read_pos += count;
		max_count -= count;
		total_count += count;
		if (count == 0)
			break;
	}
	return total_count;
}


CRAB_INLINE size_t OVectorStream::write_some(const uint8_t *val, size_t count) {
    wimpl->insert(wimpl->end(), val, val + count);
    return count;
}

CRAB_INLINE size_t IStringStream::read_some(uint8_t *val, size_t count) {
	size_t rc = std::min(count, rimpl->size() - read_pos);
	std::memcpy(val, rimpl->data() + read_pos, rc);
	read_pos += rc;
	return rc;
}

CRAB_INLINE size_t IStringStream::write_to(OStream &out, size_t max_count) {
	size_t total_count = 0;
	while (true) {
		size_t rc = std::min(size(), max_count);
		if (rc == 0)
			break;
		size_t count = out.write_some(reinterpret_cast<const uint8_t *>(rimpl->data()) + read_pos, rc);
		read_pos += count;
		max_count -= count;
		total_count += count;
		if (count == 0)
			break;
	}
	return total_count;
}


CRAB_INLINE size_t OStringStream::write_some(const uint8_t *val, size_t count) {
    wimpl->insert(wimpl->end(), val, val + count);
    return count;
}

/*

// Some experimental code
// Inspirated by boost::streams

class IStreamBuffer {
    static constexpr size_t SMALL_READ  = 256;
    static constexpr size_t BUFFER_SIZE = SMALL_READ * 4;
    uint8_t buffer_storage[BUFFER_SIZE];
    uint8_t *buffer    = buffer_storage;
    size_t buffer_size = 0;

    IStreamRaw *raw_stream;

public:
    void read(uint8_t *data, size_t size) {
        if (size <= buffer_size) {  // Often, also when zero size
            memcpy(data, buffer, size);
            buffer += size;
            buffer_size -= size;
            return;
        }
        // size > 0 here
        memcpy(data, buffer, buffer_size);
        buffer      = buffer_storage;
        buffer_size = 0;
        size -= buffer_size;
        data += buffer_size;
        if (size < BUFFER_SIZE / 4) {  // Often
            buffer_size = raw_stream->read_some(buffer, BUFFER_SIZE);
            if (buffer_size == 0)
                throw std::runtime_error("IStreamRaw underflow");
            memcpy(data, buffer, size);
            buffer += size;
            buffer_size -= size;
            return;
        }
        // size > 0 here
        while (true) {
            size_t cou = raw_stream->read_some(data, size);
            if (cou == 0)
                throw std::runtime_error("IStreamRaw underflow");
            size -= cou;
            data += cou;
            if (size == 0)
                return;
        }
    };
};
*/

}  // namespace crab
