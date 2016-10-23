#include "buffer_impl.h"

#include "common/common/assert.h"

#include "event2/buffer.h"

namespace Buffer {

void OwnedImpl::add(const void* data, uint64_t size) { evbuffer_add(buffer_.get(), data, size); }

void OwnedImpl::add(const std::string& data) {
  evbuffer_add(buffer_.get(), data.c_str(), data.size());
}

void OwnedImpl::add(const Instance& data) {
  uint64_t num_slices = data.getRawSlices(nullptr, 0);
  RawSlice slices[num_slices];
  data.getRawSlices(slices, num_slices);
  for (RawSlice& slice : slices) {
    add(slice.mem_, slice.len_);
  }
}

void OwnedImpl::drain(uint64_t size) {
  ASSERT(size <= length());
  int rc = evbuffer_drain(buffer_.get(), size);
  ASSERT(rc == 0);
  UNREFERENCED_PARAMETER(rc);
}

uint64_t OwnedImpl::getRawSlices(RawSlice* out, uint64_t out_size) const {
  evbuffer_iovec iovecs[out_size];
  uint64_t needed_size = evbuffer_peek(buffer_.get(), -1, nullptr, iovecs, out_size);
  for (uint64_t i = 0; i < std::min(out_size, needed_size); i++) {
    out[i].mem_ = iovecs[i].iov_base;
    out[i].len_ = iovecs[i].iov_len;
  }
  return needed_size;
}

uint64_t OwnedImpl::length() const { return evbuffer_get_length(buffer_.get()); }

void* OwnedImpl::linearize(uint32_t size) {
  ASSERT(size <= length());
  return evbuffer_pullup(buffer_.get(), size);
}

void OwnedImpl::move(Instance& rhs) {
  // We do the static cast here because in practice we only have one buffer implementation right
  // now and this is safe. Using the evbuffer move routines require having access to both evbuffers.
  // This is a reasonable compromise in a high performance path where we want to maintain an
  // abstraction in case we get rid of evbuffer later.
  int rc = evbuffer_add_buffer(buffer_.get(), static_cast<OwnedImpl&>(rhs).buffer_.get());
  ASSERT(rc == 0);
  UNREFERENCED_PARAMETER(rc);
}

void OwnedImpl::move(Instance& rhs, uint64_t length) {
  // See move() above for why we do the static cast.
  int rc =
      evbuffer_remove_buffer(static_cast<OwnedImpl&>(rhs).buffer_.get(), buffer_.get(), length);
  ASSERT(static_cast<uint64_t>(rc) == length);
  UNREFERENCED_PARAMETER(rc);
}

int OwnedImpl::read(int fd, uint64_t max_length) {
  return evbuffer_read(buffer_.get(), fd, max_length);
}

ssize_t OwnedImpl::search(const void* data, uint64_t size, size_t start) const {
  evbuffer_ptr start_ptr;
  if (-1 == evbuffer_ptr_set(buffer_.get(), &start_ptr, start, EVBUFFER_PTR_SET)) {
    return -1;
  }

  evbuffer_ptr result_ptr =
      evbuffer_search(buffer_.get(), static_cast<const char*>(data), size, &start_ptr);
  return result_ptr.pos;
}

int OwnedImpl::write(int fd) { return evbuffer_write(buffer_.get(), fd); }

OwnedImpl::OwnedImpl() : buffer_(evbuffer_new()) {}

OwnedImpl::OwnedImpl(const std::string& data) : OwnedImpl() { add(data); }

OwnedImpl::OwnedImpl(const Instance& data) : OwnedImpl() { add(data); }

OwnedImpl::OwnedImpl(const void* data, uint64_t size) : OwnedImpl() { add(data, size); }

} // Buffer
