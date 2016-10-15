#include "header_map_impl.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/utility.h"

namespace Http {

HeaderMapImpl::HeaderEntryImplBase::HeaderEntryImplBase(HeaderMapImpl& parent) : parent_(parent) {}

void HeaderMapImpl::HeaderEntryImplBase::maybeInsert() {
  if (!present()) {
    if (!parent_.last_) {
      ASSERT(!parent_.first_);
      parent_.first_ = this;
      parent_.last_ = this;
    } else {
      parent_.last_->next_ = this;
      prev_ = parent_.last_;
      parent_.last_ = this;
    }
  }
}

void HeaderMapImpl::HeaderEntryImplBase::remove() {
  if (present()) {
    if (parent_.first_ == this) {
      parent_.first_ = next_;
    }
    if (parent_.last_ == this) {
      parent_.last_ = prev_;
    }
    if (prev_) {
      prev_->next_ = next_;
    }
    if (next_) {
      next_->prev_ = prev_;
    }

    prev_ = nullptr;
    next_ = nullptr;
    value_size_ = 0;
  }
}

void HeaderMapImpl::HeaderEntryImplBase::value(const char* value, uint32_t size) {
  maybeInsert();
  ASSERT(size < sizeof(value_) - 1); // fixfix
  memcpy(value_, value, size);
  value_[size] = 0;
  value_size_ = size;
}

void HeaderMapImpl::HeaderEntryImplBase::value(const std::string& value) {
  this->value(value.c_str(), static_cast<uint32_t>(value.size()));
}

void HeaderMapImpl::HeaderEntryImplBase::value(uint64_t value) {
  maybeInsert();
  value_size_ = StringUtil::itoa(value_, 0, value); // fixfix
}

void HeaderMapImpl::HeaderEntryImplBase::value(const HeaderEntry& header) {
  value(header.value().c_str(), header.value().size());
}

HeaderString HeaderMapImpl::HeaderEntryImplBase::value() const {
  ASSERT(present());
  ASSERT(value_size_ > 0);
  return HeaderString(value_, value_size_);
}

HeaderString HeaderMapImpl::StaticHeaderEntryImpl::key() const {
  ASSERT(present());
  return HeaderString(key_.c_str(), key_.size());
}

HeaderMapImpl::DynamicHeaderEntryImpl::DynamicHeaderEntryImpl(HeaderMapImpl& parent,
                                                              const std::string& key)
    : HeaderEntryImplBase(parent) {
  ASSERT(key.size() < sizeof(key_) - 1); // fixfix
  memcpy(key_, key.c_str(), key.size());
  key_[key.size()] = 0;
  key_size_ = key.size();
}

HeaderString HeaderMapImpl::DynamicHeaderEntryImpl::key() const {
  ASSERT(present());
  ASSERT(key_size_ > 0);
  return HeaderString(key_, key_size_);
}

#define INLINE_HEADER_STATIC_MAP_ENTRY(name)                                                       \
  map_[Headers::get().name.get()] =                                                                \
      [](HeaderMapImpl & h) -> StaticHeaderEntryImpl& { return h.name##_; };

const HeaderMapImpl::StaticLookupTable HeaderMapImpl::static_lookup_table_;

HeaderMapImpl::StaticLookupTable::StaticLookupTable() {
  ALL_INLINE_HEADERS(INLINE_HEADER_STATIC_MAP_ENTRY)

  // Special case where we may a legacy host header to :authority.
  map_[Headers::get().HostLegacy.get()] =
      [](HeaderMapImpl& h) -> StaticHeaderEntryImpl& { return h.Host_; };
}

HeaderMapImpl::HeaderMapImpl(const HeaderMap&) {
  ASSERT(false);
  // rhs.iterate([&](const LowerCaseString& key, const std::string& value)
  //                -> void { addViaCopy(key, value); });
}

HeaderMapImpl::HeaderMapImpl(
    const std::initializer_list<std::pair<LowerCaseString, std::string>>& values) {
  for (auto& value : values) {
    addLowerCase(value.first.get(), value.second);
  }
}

bool HeaderMapImpl::operator==(const HeaderMapImpl&) const {
  ASSERT(false);
  return false;
}

void HeaderMapImpl::addLowerCase(const std::string& key, const std::string& value) {
  auto static_entry = static_lookup_table_.map_.find(key);
  HeaderEntryImplBase* entry;
  if (static_entry != static_lookup_table_.map_.end()) {
    // fixfix duplicate headers.
    entry = &static_entry->second(*this);
  } else {
    entry = new DynamicHeaderEntryImpl(*this, key);
    allocated_headers_.emplace_back(entry);
  }

  entry->value(value);
}

uint64_t HeaderMapImpl::byteSize() const {
  uint64_t byte_size = 0;
  HeaderEntryImplBase* current = first_;
  while (current) {
    byte_size += current->key().size();
    byte_size += current->value().size();
    current = current->next_;
  }

  return byte_size;
}

HeaderString HeaderMapImpl::get(const LowerCaseString& key) const {
  HeaderEntryImplBase* current = first_;
  while (current) {
    if (current->key() == key.get().c_str()) {
      return current->value();
    }

    current = current->next_;
  }

  return HeaderString(EMPTY_STRING.c_str(), EMPTY_STRING.size());
}

bool HeaderMapImpl::has(const LowerCaseString& key) const {
  HeaderEntryImplBase* current = first_;
  while (current) {
    if (current->key() == key.get().c_str()) {
      return true;
    }

    current = current->next_;
  }

  return false;
}

void HeaderMapImpl::iterate(ConstIterateCb cb, void* context) const {
  HeaderEntryImplBase* current = first_;
  while (current) {
    cb(*current, context);
    current = current->next_;
  }
}

void HeaderMapImpl::remove(const LowerCaseString& key) {
  HeaderEntryImplBase* current = first_;
  while (current) {
    if (current->key() == key.get().c_str()) {
      current->remove();
    }

    current = current->next_;
  }
}

} // Http
