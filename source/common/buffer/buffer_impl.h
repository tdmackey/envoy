#pragma once

#include "envoy/buffer/buffer.h"

#include "common/event/libevent.h"

namespace Buffer {

/**
 * Base implementation for libevent backed buffers.
 */
class ImplBase : public Instance {
public:
  // Instance
  void add(const void* data, uint64_t size) override;
  void add(const std::string& data) override;
  void add(const Instance& data) override;
  void drain(uint64_t size) override;
  uint64_t getRawSlices(RawSlice* out, uint64_t out_size) const override;
  uint64_t length() const override;
  void* linearize(uint32_t size) override;
  void move(Instance& rhs) override;
  void move(Instance& rhs, uint64_t length) override;
  uint64_t reserve(uint64_t length, RawSlice* iovecs, uint64_t num_iovecs) override;
  void commit(RawSlice* iovecs, uint64_t num_iovecs) override;
  ssize_t search(const void* data, uint64_t size, size_t start) const override;

protected:
  ImplBase(evbuffer* buffer) : buffer_(buffer) {}

  evbuffer* buffer_;

private:
  void add(const char* data); // fixfix
};

/**
 * Wraps a non-owned evbuffer.
 */
class WrappedImpl : public ImplBase {
public:
  WrappedImpl(evbuffer* buffer) : ImplBase(buffer) {}
};

/**
 * Wraps an allocated and owned evbuffer.
 */
class OwnedImpl : public ImplBase {
public:
  OwnedImpl();
  OwnedImpl(const std::string& data);
  OwnedImpl(const Instance& data);
  OwnedImpl(const void* data, uint64_t size);
  ~OwnedImpl();
};

} // Buffer
