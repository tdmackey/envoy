#pragma once

#include "envoy/common/pure.h"

namespace Http {

/**
 * Wrapper for a lower case string used in header operations to generally avoid needless case
 * insensitive compares.
 */
class LowerCaseString {
public:
  explicit LowerCaseString(LowerCaseString&& rhs) : string_(std::move(rhs.string_)) {}
  explicit LowerCaseString(const LowerCaseString& rhs) : string_(rhs.string_) {}
  explicit LowerCaseString(const std::string& new_string) : string_(new_string) { lower(); }
  explicit LowerCaseString(std::string&& new_string, bool convert = true)
      : string_(std::move(new_string)) {
    if (convert) {
      lower();
    }
  }

  LowerCaseString(const char* new_string) : string_(new_string) { lower(); }

  const std::string& get() const { return string_; }
  bool operator==(const LowerCaseString& rhs) const { return string_ == rhs.string_; }

private:
  void lower() { std::transform(string_.begin(), string_.end(), string_.begin(), tolower); }

  std::string string_;
};

/**
 *
 */
class HeaderString {
public:
  HeaderString(const char* c_str, uint32_t size) : c_str_(c_str), size_(size) {}

  const char* c_str() const { return c_str_; }
  bool empty() const { return size_ == 0; }
  bool find(const char* str) { return nullptr != strstr(c_str_, str); }
  uint64_t size() const { return size_; }
  bool operator==(const char* rhs) const { return 0 == strcmp(c_str_, rhs); }
  bool operator!=(const char* rhs) const { return 0 != strcmp(c_str_, rhs); }

private:
  const char* c_str_;
  uint32_t size_;
};

/**
 *
 */
class HeaderEntry {
public:
  virtual ~HeaderEntry() {}

  virtual HeaderString key() const PURE;
  virtual bool present() const PURE;
  virtual void remove() PURE;
  virtual void value(const char* value, uint32_t size) PURE;
  virtual void value(const std::string& value) PURE;
  virtual void value(uint64_t value) PURE;
  virtual void value(const HeaderEntry& header) PURE;
  virtual HeaderString value() const PURE;

private:
  void value(const char*); // Do not allow auto conversion to std::string
};

#define ALL_INLINE_HEADERS(HEADER_FUNC)                                                            \
  HEADER_FUNC(Authorization)                                                                       \
  HEADER_FUNC(Connection)                                                                          \
  HEADER_FUNC(ContentLength)                                                                       \
  HEADER_FUNC(ContentType)                                                                         \
  HEADER_FUNC(Cookie)                                                                              \
  HEADER_FUNC(Date)                                                                                \
  HEADER_FUNC(EnvoyDownstreamServiceCluster)                                                       \
  HEADER_FUNC(EnvoyExpectedRequestTimeoutMs)                                                       \
  HEADER_FUNC(EnvoyExternalAddress)                                                                \
  HEADER_FUNC(EnvoyForceTrace)                                                                     \
  HEADER_FUNC(EnvoyUpstreamHealthCheckedCluster)                                                   \
  HEADER_FUNC(EnvoyInternalRequest)                                                                \
  HEADER_FUNC(EnvoyMaxRetries)                                                                     \
  HEADER_FUNC(EnvoyOriginalPath)                                                                   \
  HEADER_FUNC(EnvoyProtocolVersion)                                                                \
  HEADER_FUNC(EnvoyRetryOn)                                                                        \
  HEADER_FUNC(EnvoyUpstreamAltStatName)                                                            \
  HEADER_FUNC(EnvoyUpstreamCanary)                                                                 \
  HEADER_FUNC(EnvoyUpstreamRequestPerTryTimeoutMs)                                                 \
  HEADER_FUNC(EnvoyUpstreamRequestTimeoutMs)                                                       \
  HEADER_FUNC(EnvoyUpstreamServiceTime)                                                            \
  HEADER_FUNC(Expect)                                                                              \
  HEADER_FUNC(ForwardedFor)                                                                        \
  HEADER_FUNC(ForwardedProto)                                                                      \
  HEADER_FUNC(GrpcStatus)                                                                          \
  HEADER_FUNC(GrpcMessage)                                                                         \
  HEADER_FUNC(Host)                                                                                \
  HEADER_FUNC(KeepAlive)                                                                           \
  HEADER_FUNC(Method)                                                                              \
  HEADER_FUNC(Path)                                                                                \
  HEADER_FUNC(ProxyConnection)                                                                     \
  HEADER_FUNC(RequestId)                                                                           \
  HEADER_FUNC(Scheme)                                                                              \
  HEADER_FUNC(Server)                                                                              \
  HEADER_FUNC(Status)                                                                              \
  HEADER_FUNC(TransferEncoding)                                                                    \
  HEADER_FUNC(Upgrade)                                                                             \
  HEADER_FUNC(UserAgent)                                                                           \
  HEADER_FUNC(Version);

#define DEFINE_INLINE_HEADER(name)                                                                 \
  virtual const HeaderEntry& name() const PURE;                                                    \
  virtual HeaderEntry& name() PURE;

/**
 * Wraps a set of HTTP headers.
 */
class HeaderMap {
public:
  virtual ~HeaderMap() {}

  ALL_INLINE_HEADERS(DEFINE_INLINE_HEADER)

  /**
   * Move a key/value into the map.
   * @param key supplies the key to move in.
   * @param value supplies the value to move in.
   */
  virtual void addLowerCase(const std::string& key, const std::string& value) PURE;

  /**
   * @return uint64_t the approximate size of the header map in bytes.
   */
  virtual uint64_t byteSize() const PURE;

  /**
   * Get a header value by key.
   * @param key supplies the header key.
   * @return the header value or the empty string if the header has no value or does not exist.
   */
  virtual HeaderString get(const LowerCaseString& key) const PURE;

  /**
   * @return whether the map has a specific header (even if it contains an empty value).
   */
  virtual bool has(const LowerCaseString& key) const PURE;

  /**
   * Callback when calling iterate() over a const header map.
   * @param key supplies the header key.
   * @param value supplies header value.
   */
  typedef void (*ConstIterateCb)(const HeaderEntry& header, void* context);

  /**
   * Iterate over a constant header map.
   * @param cb supplies the iteration callback.
   */
  virtual void iterate(ConstIterateCb cb, void* context) const PURE;

  /**
   * Remove all instances of a header by key.
   * @param key supplies the header key to remove.
   */
  virtual void remove(const LowerCaseString& key) PURE;
};

typedef std::unique_ptr<HeaderMap> HeaderMapPtr;

} // Http
