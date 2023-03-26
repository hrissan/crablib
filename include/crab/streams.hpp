// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include "util.hpp"

namespace crab {

class IStream {
public:
	virtual ~IStream()                                   = default;
	virtual size_t read_some(uint8_t *val, size_t count) = 0;
	void read(uint8_t *val, size_t count);

	// We do not wish to use void* due to unsafe conversions, we wish all 3 common byte types
	size_t read_some(char *val, size_t count) { return read_some(uint8_cast(val), count); }
	void read(char *val, size_t count) { read(uint8_cast(val), count); }
#if __cplusplus >= 201703L
	size_t read_some(std::byte *val, size_t count) { return read_some(uint8_cast(val), count); }
	void read(std::byte *val, size_t count) { read(uint8_cast(val), count); }
#endif
};

class OStream {
public:
	virtual ~OStream()                                          = default;
	virtual size_t write_some(const uint8_t *val, size_t count) = 0;
	void write(const uint8_t *val, size_t count);
	void write_byte(uint8_t val) { write(&val, 1); }  // name prevents dangerous conversions

	// We do not wish to use void* due to unsafe conversions, we wish all 3 common byte types
	size_t write_some(const char *val, size_t count) { return write_some(uint8_cast(val), count); }
	void write(const char *val, size_t count) { write(uint8_cast(val), count); }
#if __cplusplus >= 201703L
	size_t write_some(const std::byte *val, size_t count) { return write_some(uint8_cast(val), count); }
	void write(const std::byte *val, size_t count) { write(uint8_cast(val), count); }
#endif
};

class IFiniteStream : public IStream {
public:
	virtual size_t size() const = 0;
	bool empty() const { return size() == 0; }

	virtual size_t write_to(OStream &out, size_t max_count) = 0;
	// return bytes written, this method is not necessary, but saves copy to buffer/from buffer
	size_t write_to(OStream &out) { return write_to(out, std::numeric_limits<size_t>::max()); }
};

// Classic circular buffer
class Buffer final : public IFiniteStream, public OStream {
	bdata impl;
	size_t read_pos;   // 0..impl.size-1
	size_t write_pos;  // read_pos..read_pos + impl.size
public:
	explicit Buffer(size_t si) : impl(si), read_pos(0), write_pos(0) {}
	size_t capacity() const { return impl.size(); }
	size_t read_some(uint8_t *val, size_t count) override;
	using IStream::read_some;  // Version for other char types
	size_t size() const override {
		return write_pos - read_pos;  // Same as read_count() + read_count2();
	}
	size_t write_some(const uint8_t *val, size_t count) override;
	using OStream::write_some;  // Version for other char types

	void clear() { read_pos = write_pos = 0; }
	void clear(size_t new_size) {
		clear();
		impl.resize(new_size);
	}

	bool full() const { return read_pos + impl.size() == write_pos; }

	size_t read_count() const { return write_pos < impl.size() ? write_pos - read_pos : impl.size() - read_pos; }
	const uint8_t *read_ptr() const { return impl.data() + read_pos; }
	size_t write_count() const { return write_pos < impl.size() ? impl.size() - write_pos : read_pos - (write_pos - impl.size()); }
	uint8_t *write_ptr() { return write_pos < impl.size() ? impl.data() + write_pos : impl.data() + write_pos - impl.size(); }

	void did_write(size_t count) {
		write_pos += count;
		invariant(write_pos <= read_pos + impl.size(), "Writing past end of Buffer");
	}
	void did_write_undo(size_t count) {
		invariant(write_pos >= read_pos + count, "Writing undo past read_pos");
		write_pos -= count;
	}
	void did_read(size_t count) {
		read_pos += count;
		invariant(read_pos <= write_pos, "Reading past end of Buffer");
		if (read_pos >= impl.size()) {  // could did read from 2 parts of circular buffer
			read_pos -= impl.size();
			write_pos -= impl.size();
		}
		if (read_pos == write_pos)
			read_pos = write_pos = 0;  // Increases chance of single-chunk reading
	}

	// circular buffer has maximum 2 parts. this gives second part
	size_t read_count2() const { return write_pos < impl.size() ? 0 : write_pos - impl.size(); }
	const uint8_t *read_ptr2() const { return impl.data(); }
	size_t write_count2() const { return write_pos < impl.size() ? read_pos : 0; }
	uint8_t *write_ptr2() { return impl.data(); }

	size_t read_from(IStream &in);                             // returns # read
	size_t write_to(OStream &out, size_t max_count) override;  // returns # written
	using IFiniteStream::write_to;

	bool read_enough_data(IStream &in, size_t count);  // returns true, if size() after reading >= count
	bool peek(uint8_t *val, size_t count) const;
};

/*class Rope : public IFiniteStream, public OStream {
    std::deque<Buffer> rope;
    size_t chunk_size;
    size_t rope_size = 0;
public:
    explicit Rope(size_t chunk_size);
    size_t read_some(uint8_t *val, size_t count) override;
    size_t size() const override { return rope_size; }
    size_t write_some(const uint8_t *val, size_t count) override;

    void clear();

    size_t read_count() const { return rope.front().read_count(); }
    const uint8_t *read_ptr() { return rope.front().read_ptr(); }
    size_t write_count() const { return rope.back().write_count(); }
    uint8_t *write_ptr() { return rope.back().write_ptr(); }

    void did_write(size_t count);
    void did_read(size_t count);

    size_t read_from(IStream &in);                             // returns # read
    size_t write_to(OStream &out, size_t max_count) override;  // returns # written
    using IFiniteStream::write_to;
};*/

class IMemoryStream : public IFiniteStream {
protected:
	const uint8_t *data = nullptr;
	size_t si           = 0;

public:
	IMemoryStream() = default;
	explicit IMemoryStream(const uint8_t *data, size_t size) : data(data), si(size) {}
	size_t read_some(uint8_t *val, size_t count) override;
	using IStream::read_some;
	size_t size() const override { return si; }
	size_t write_to(OStream &out, size_t max_count) override;
	using IFiniteStream::write_to;
};

class OMemoryStream : public OStream {
protected:
	uint8_t *data = nullptr;
	size_t si     = 0;

public:
	OMemoryStream() = default;
	explicit OMemoryStream(uint8_t *data, size_t size) : data(data), si(size) {}
	size_t write_some(const uint8_t *val, size_t count) override;
	using OStream::write_some;
};

class IVectorStream : public IFiniteStream {
protected:
	const bdata *rimpl;
	size_t read_pos;

public:
	IVectorStream() : rimpl(nullptr), read_pos(0) {}
	explicit IVectorStream(const bdata *rimpl) : rimpl(rimpl), read_pos(0) {}
	size_t read_some(uint8_t *val, size_t count) override;
	using IStream::read_some;
	size_t size() const override { return rimpl->size() - read_pos; }
	size_t write_to(OStream &out, size_t max_count) override;
	using IFiniteStream::write_to;
};

class OVectorStream : public OStream {
protected:
	bdata *wimpl;

public:
	OVectorStream() : wimpl(nullptr) {}
	explicit OVectorStream(bdata *wimpl) : wimpl(wimpl) {}
	size_t write_some(const uint8_t *val, size_t count) override;
	using OStream::write_some;
};

// minimum stream implementation
class VectorStream : public IVectorStream, public OVectorStream {
	bdata impl;

public:
	VectorStream() : IVectorStream(&impl), OVectorStream(&impl) {}
	//	explicit VectorStream(const bdata &data) : IVectorStream(&impl), OVectorStream(&impl), impl(data) {}
	explicit VectorStream(bdata data) : IVectorStream(&impl), OVectorStream(&impl), impl(std::move(data)) {}
	VectorStream(VectorStream &&other) noexcept : IVectorStream(&impl), OVectorStream(&impl), impl(std::move(other.impl)) {
		read_pos = other.read_pos;
	}
	VectorStream &operator=(VectorStream &&other) noexcept {
		impl     = std::move(other.impl);
		rimpl    = &impl;
		wimpl    = &impl;
		read_pos = other.read_pos;
		return *this;
	}
	VectorStream(const VectorStream &other) = delete;
	VectorStream &operator=(const VectorStream &other) = delete;
	bdata clear() {
		bdata result;
		impl.swap(result);
		read_pos = 0;
		return result;
	}
	bdata &get_buffer() { return impl; }
	const bdata &get_buffer() const { return impl; }
};

class IStringStream : public IFiniteStream {
protected:
	const std::string *rimpl;
	size_t read_pos;

public:
	IStringStream() : rimpl(nullptr), read_pos(0) {}
	explicit IStringStream(const std::string *rimpl) : rimpl(rimpl), read_pos(0) {}
	size_t read_some(uint8_t *val, size_t count) override;
	using IStream::read_some;
	size_t size() const override { return rimpl->size() - read_pos; }
	size_t write_to(OStream &out, size_t max_count) override;
	using IFiniteStream::write_to;
};

class OStringStream : public OStream {
protected:
	std::string *wimpl;

public:
	OStringStream() : wimpl(nullptr) {}
	explicit OStringStream(std::string *wimpl) : wimpl(wimpl) {}
	size_t write_some(const uint8_t *val, size_t count) override;
	using OStream::write_some;
};

// minimum stream implementation
class StringStream : public IStringStream, public OStringStream {
	std::string impl;

public:
	StringStream() : IStringStream(&impl), OStringStream(&impl) {}
	//	explicit StringStream(const std::string &data) : IStringStream(&impl), OStringStream(&impl), impl(data) {}
	explicit StringStream(std::string data) : IStringStream(&impl), OStringStream(&impl), impl(std::move(data)) {}
	StringStream(StringStream &&other) noexcept : IStringStream(&impl), OStringStream(&impl), impl(std::move(other.impl)) {
		read_pos = other.read_pos;
	}
	StringStream &operator=(StringStream &&other) noexcept {
		impl     = std::move(other.impl);
		rimpl    = &impl;
		wimpl    = &impl;
		read_pos = other.read_pos;
		return *this;
	}
	StringStream(const StringStream &other) = delete;
	StringStream &operator=(const StringStream &other) = delete;
	std::string clear() {
		std::string result;
		impl.swap(result);
		read_pos = 0;
		return result;
	}

	std::string &get_buffer() { return impl; }
	const std::string &get_buffer() const { return impl; }
};

class CombinedIStream : public IFiniteStream {
	std::unique_ptr<IFiniteStream> a, b;

public:
	explicit CombinedIStream(std::unique_ptr<IFiniteStream> &&a, std::unique_ptr<IFiniteStream> &&b) : a(std::move(a)), b(std::move(b)) {}
	size_t size() const override { return a->size() + b->size(); }
	size_t read_some(uint8_t *val, size_t count) override {
		if (!a->empty())
			return a->read_some(val, count);
		return b->read_some(val, count);
	}
	using IStream::read_some;
	size_t write_to(OStream &out, size_t max_count) override {
		if (!a->empty()) {
			size_t count = a->write_to(out, max_count);
			max_count -= count;
			if (!a->empty())
				return count;
		}
		return b->write_to(out, max_count);
	}
	using IFiniteStream::write_to;
};
}  // namespace crab
