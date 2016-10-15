#pragma once

#include "envoy/http/header_map.h"

#include "headers.h"

namespace Http {

#define DEFINE_INLINE_HEADER_IMPL(name)                                                            \
public:                                                                                            \
  const HeaderEntry& name() const override { return name##_; }                                     \
  HeaderEntry& name() override { return name##_; }                                                 \
                                                                                                   \
private:                                                                                           \
  StaticHeaderEntryImpl name##_{*this, Headers::get().name.get()};

/**
 * Implementation of Http::HeaderMap. FIXFIX
 */
class HeaderMapImpl : public HeaderMap {
public:
  HeaderMapImpl() { allocated_headers_.reserve(10); }
  HeaderMapImpl(const std::initializer_list<std::pair<LowerCaseString, std::string>>& values);
  HeaderMapImpl(const HeaderMap& rhs);

  /**
   * For testing. Equality is based on equality of the backing list.
   */
  bool operator==(const HeaderMapImpl& rhs) const;

  // Http::HeaderMap
  void addLowerCase(const std::string& key, const std::string& value) override;
  uint64_t byteSize() const override;
  HeaderString get(const LowerCaseString& key) const override;
  bool has(const LowerCaseString& key) const override;
  void iterate(ConstIterateCb cb, void* context) const override;
  void remove(const LowerCaseString& key) override;

private:
  struct HeaderEntryImplBase : public HeaderEntry {
    HeaderEntryImplBase(HeaderMapImpl& parent);

    void maybeInsert();

    // HeaderEntry
    bool present() const override { return parent_.first_ == this || prev_; }
    void remove() override;
    void value(const char* value, uint32_t size) override;
    void value(const std::string& value) override;
    void value(uint64_t value) override;
    void value(const HeaderEntry& header) override;
    HeaderString value() const override;

    HeaderMapImpl& parent_;
    char value_[128];
    uint32_t value_size_{};
    HeaderEntryImplBase* prev_{};
    HeaderEntryImplBase* next_{};
  };

  struct StaticHeaderEntryImpl : public HeaderEntryImplBase {
    StaticHeaderEntryImpl(HeaderMapImpl& parent, const std::string& key)
        : HeaderEntryImplBase(parent), key_(key) {}

    // HeaderEntry
    HeaderString key() const override;

    const std::string& key_;
  };

  struct DynamicHeaderEntryImpl : public HeaderEntryImplBase {
    DynamicHeaderEntryImpl(HeaderMapImpl& parent, const std::string& key);

    // HeaderEntry
    HeaderString key() const override;

    char key_[64];
    uint32_t key_size_{};
  };

  struct StaticLookupTable {
    StaticLookupTable();

    typedef StaticHeaderEntryImpl& (*EntryCb)(HeaderMapImpl&);
    std::unordered_map<std::string, EntryCb> map_;
  };

  static const StaticLookupTable static_lookup_table_;
  HeaderEntryImplBase* first_{};
  HeaderEntryImplBase* last_{};
  std::vector<std::unique_ptr<HeaderEntryImplBase>> allocated_headers_;

  ALL_INLINE_HEADERS(DEFINE_INLINE_HEADER_IMPL)
};

} // Http
