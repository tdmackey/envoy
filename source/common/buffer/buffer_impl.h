#pragma once

#include "envoy/buffer/buffer.h"

#include "common/event/libevent.h"

namespace Buffer {

/**
 * Wraps an allocated and owned evbuffer.
 */
class OwnedImpl : public Instance {
public:
  OwnedImpl();
  OwnedImpl(const std::string& data);
  OwnedImpl(const Instance& data);
  OwnedImpl(const void* data, uint64_t size);

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
  int read(int fd, uint64_t max_length) override;
  ssize_t search(const void* data, uint64_t size, size_t start) const override;
  int write(int fd) override;

private:
  Event::Libevent::BufferPtr buffer_;
};

} // Buffer
