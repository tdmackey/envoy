#include "connection_impl.h"

#include "envoy/event/timer.h"
#include "envoy/common/exception.h"
#include "envoy/network/filter.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/event/dispatcher_impl.h"
#include "common/network/utility.h"

//#include "event2/buffer.h"
//#include "event2/bufferevent.h"
//#include "event2/event.h"
//#include "event2/util.h"

namespace Network {

std::atomic<uint64_t> ConnectionImpl::next_global_id_;
const evbuffer_cb_func ConnectionImpl::read_buffer_cb_ =
    [](evbuffer*, const evbuffer_cb_info* info, void* arg) -> void {
      static_cast<ConnectionImpl*>(arg)->onBufferChange(ConnectionBufferType::Read, info);
    };

const evbuffer_cb_func ConnectionImpl::write_buffer_cb_ =
    [](evbuffer*, const evbuffer_cb_info* info, void* arg) -> void {
      static_cast<ConnectionImpl*>(arg)->onBufferChange(ConnectionBufferType::Write, info);
    };

// ConnectionImpl::ConnectionImpl(Event::DispatcherImpl& dispatcher)
//    : ConnectionImpl(dispatcher, "") {}

ConnectionImpl::ConnectionImpl(Event::DispatcherImpl& dispatcher, int fd,
                               const std::string& remote_address)
    : dispatcher_(dispatcher), fd_(fd), remote_address_(remote_address), id_(++next_global_id_),
      filter_manager_(*this, *this),
      redispatch_read_event_(dispatcher.createTimer([this]() -> void { onRead(); })),
      do_write_event_(dispatcher.createTimer([this]() -> void { onDoWrite(); })) {

  ASSERT(fd_ != -1);
  file_event_ = dispatcher_.createFileEvent(fd, [this]() -> void { onReadReady(); },
                                            [this]() -> void { onWriteReady(); });

  // enableCallbacks(true, false, true);
  // bufferevent_enable(bev_.get(), EV_READ | EV_WRITE);

  // evbuffer_add_cb(bufferevent_get_input(bev_.get()), read_buffer_cb_, this);
  // evbuffer_add_cb(bufferevent_get_output(bev_.get()), write_buffer_cb_, this);
}

ConnectionImpl::~ConnectionImpl() { ASSERT(fd_ == -1); }

void ConnectionImpl::addWriteFilter(WriteFilterPtr filter) {
  filter_manager_.addWriteFilter(filter);
}

void ConnectionImpl::addFilter(FilterPtr filter) { filter_manager_.addFilter(filter); }

void ConnectionImpl::addReadFilter(ReadFilterPtr filter) { filter_manager_.addReadFilter(filter); }

void ConnectionImpl::close(ConnectionCloseType type) {
  if (fd_ == -1) {
    return;
  }

  uint64_t data_to_write = write_buffer_.length();
  conn_log_debug("closing data_to_write={}", *this, data_to_write);
  if (data_to_write == 0 || type == ConnectionCloseType::NoFlush) {
    closeNow();
  } else {
    ASSERT(false);
    ASSERT(type == ConnectionCloseType::FlushWrite);
    closing_with_flush_ = true;
    read_enabled_ = false;
  }
}

Connection::State ConnectionImpl::state() {
  if (fd_ == -1) {
    return State::Closed;
  } else if (closing_with_flush_) {
    return State::Closing;
  } else {
    return State::Open;
  }
}

void ConnectionImpl::closeBev() {
  ASSERT(fd_ != -1);
  conn_log_debug("destroying bev", *this);

  // Drain input and output buffers so that callbacks get fired. This does not happen automatically
  // as part of destruction.
  /*fakeBufferDrain(ConnectionBufferType::Read, bufferevent_get_input(bev_.get()));
  evbuffer_remove_cb(bufferevent_get_input(bev_.get()), read_buffer_cb_, this);

  fakeBufferDrain(ConnectionBufferType::Write, bufferevent_get_output(bev_.get()));
  evbuffer_remove_cb(bufferevent_get_output(bev_.get()), write_buffer_cb_, this);*/

  file_event_.reset();
  ::close(fd_);
  fd_ = -1;
  redispatch_read_event_->disableTimer();
}

void ConnectionImpl::closeNow() {
  conn_log_debug("closing now", *this);
  closeBev();

  // We expect our owner to deal with freeing us in whatever way makes sense. We raise an event
  // to kick that off.
  raiseEvents(ConnectionEvent::LocalClose);
}

Event::Dispatcher& ConnectionImpl::dispatcher() { return dispatcher_; }

/*void ConnectionImpl::enableCallbacks(bool, bool, bool) {
  ASSERT(false);
  read_enabled_ = false;
  bufferevent_data_cb read_cb = nullptr;
  bufferevent_data_cb write_cb = nullptr;
  bufferevent_event_cb event_cb = nullptr;

  if (read) {
    read_enabled_ = true;
    read_cb = [](bufferevent*, void* ctx) -> void { static_cast<ConnectionImpl*>(ctx)->onRead(); };
  }

  if (write) {
    write_cb =
        [](bufferevent*, void* ctx) -> void { static_cast<ConnectionImpl*>(ctx)->onWrite(); };
  }

  if (event) {
    event_cb = [](bufferevent*, short events, void* ctx)
                   -> void { static_cast<ConnectionImpl*>(ctx)->onEvent(events); };
  }

  bufferevent_setcb(bev_.get(), read_cb, write_cb, event_cb, this);
}*/

void ConnectionImpl::fakeBufferDrain(ConnectionBufferType, evbuffer*) {
  /*if (evbuffer_get_length(buffer) > 0) {
    evbuffer_cb_info info;
    info.n_added = 0;
    info.n_deleted = evbuffer_get_length(buffer);
    info.orig_size = evbuffer_get_length(buffer);

    onBufferChange(type, &info);
  }*/
}

void ConnectionImpl::noDelay(bool enable) {
  // There are cases where a connection to localhost can immediately fail (e.g., if the other end
  // does not have enough fds, reaches a backlog limit, etc.). Because we run with deferred error
  // events, the calling code may not yet know that the connection has failed. This is one call
  // where we go outside of libevent and hit the fd directly and this case can fail if the fd is
  // invalid. For this call instead of plumbing through logic that will immediately indicate that a
  // connect failed, we will just ignore the noDelay() call if the socket is invalid since error is
  // going to be raised shortly anyway and it makes the calling code simpler.
  if (fd_ == -1) {
    return;
  }

  // Don't set NODELAY for unix domain sockets
  sockaddr addr;
  socklen_t len = sizeof(addr);
  int rc = getsockname(fd_, &addr, &len);
  RELEASE_ASSERT(rc == 0);

  if (addr.sa_family == AF_UNIX) {
    return;
  }

  // Set NODELAY
  int new_value = enable;
  rc = setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &new_value, sizeof(new_value));
  RELEASE_ASSERT(0 == rc);
  UNREFERENCED_PARAMETER(rc);
}

uint64_t ConnectionImpl::id() { return id_; }

void ConnectionImpl::onBufferChange(ConnectionBufferType, const evbuffer_cb_info*) {
  // We don't run callbacks deferred so we should only get deleted or added.
  /*ASSERT(info->n_deleted ^ info->n_added);
  for (ConnectionCallbacks* callbacks : callbacks_) {
    callbacks->onBufferChange(type, info->orig_size, info->n_added - info->n_deleted);
  }*/
}

void ConnectionImpl::onEvent(short) {
  /*uint32_t normalized_events = 0;
  if ((events & BEV_EVENT_EOF) || (events & BEV_EVENT_ERROR)) {
    normalized_events |= ConnectionEvent::RemoteClose;
    closeBev();
  }

  if (events & BEV_EVENT_CONNECTED) {
    normalized_events |= ConnectionEvent::Connected;
  }

  ASSERT(normalized_events != 0);
  conn_log_debug("event: {}", *this, normalized_events);
  raiseEvents(normalized_events);*/
}

void ConnectionImpl::onRead() {
  // Cancel the redispatch event in case we raced with a network event.
  ASSERT(read_enabled_);
  redispatch_read_event_->disableTimer();
  if (read_buffer_.length() == 0) {
    return;
  }

  filter_manager_.onRead();
}

void ConnectionImpl::onWrite() {
  conn_log_debug("write flush complete", *this);
  closeNow();
}

void ConnectionImpl::readDisable(bool disable) {
  bool read_enabled = readEnabled();
  UNREFERENCED_PARAMETER(read_enabled);
  conn_log_trace("readDisable: enabled={} disable={}", *this, read_enabled, disable);

  // We do not actually disable reading from the socket. We just stop firing read callbacks.
  // This allows us to still detect remote close in a timely manner. In practice there is a chance
  // that a bad client could send us a large amount of data on a HTTP/1.1 connection while we are
  // processing the current request.
  // TODO: Add buffered data stats and potentially fail safe processing that disconnects or
  //       applies back pressure to bad HTTP/1.1 clients.
  if (disable) {
    ASSERT(read_enabled);
    read_enabled_ = false;
  } else {
    ASSERT(!read_enabled);
    read_enabled_ = true;
    if (read_buffer_.length() > 0) {
      redispatch_read_event_->enableTimer(std::chrono::milliseconds::zero());
    }
  }
}

void ConnectionImpl::raiseEvents(uint32_t events) {
  for (ConnectionCallbacks* callback : callbacks_) {
    callback->onEvent(events);
  }
}

bool ConnectionImpl::readEnabled() { return read_enabled_; }

void ConnectionImpl::addConnectionCallbacks(ConnectionCallbacks& cb) { callbacks_.push_back(&cb); }

void ConnectionImpl::write(Buffer::Instance& data) {
  // NOTE: This is kind of a hack, but currently we don't support restart/continue on the write
  //       path, so we just pass around the buffer passed to us in this function. If we ever support
  //       buffer/restart/continue on the write path this needs to get more complicated.
  current_write_buffer_ = &data;
  FilterStatus status = filter_manager_.onWrite();
  current_write_buffer_ = nullptr;

  if (FilterStatus::StopIteration == status) {
    return;
  }

  if (data.length() > 0) {
    conn_log_trace("writing {} bytes", *this, data.length());
    write_buffer_.move(data);
    do_write_event_->enableTimer(std::chrono::milliseconds::zero());
  }
}

void ConnectionImpl::onDoWrite() {
  if (!connecting_) {
    onWriteReady();
  }
}

void ConnectionImpl::onReadReady() {
  ASSERT(!connecting_);

  bool raise_close = false;
  int rc;
  do {
    rc = read_buffer_.read(fd_, 4096);
    conn_log_trace("read returns: {}", *this, rc);
    if (rc == 0) {
      raise_close = true;
      break;
    }
    if (rc == -1) {
      conn_log_trace("read error: {}", *this, errno);
      if (errno == EAGAIN) {
        break;
      }
      ASSERT(false);
    }
  } while (true);

  onRead();

  if (raise_close && fd_ != -1) {
    conn_log_trace("remote close: {}", *this, errno);
    closeBev();
    raiseEvents(ConnectionEvent::RemoteClose);
  }
}

void ConnectionImpl::onWriteReady() {
  conn_log_trace("write ready", *this);

  if (connecting_) {
    // fixfix check for error.
    conn_log_trace("connected", *this);
    raiseEvents(ConnectionEvent::Connected);
    connecting_ = false;
  }

  do {
    if (write_buffer_.length() == 0) {
      return;
    }

    int rc = write_buffer_.write(fd_);
    conn_log_trace("write returns: {}", *this, rc);
    if (rc == -1) {
      conn_log_trace("write error: {}", *this, errno);
      if (errno == EAGAIN) {
        return;
      }
      ASSERT(false);
    }
  } while (true);
}

ClientConnectionImpl::ClientConnectionImpl(Event::DispatcherImpl& dispatcher, int fd,
                                           const std::string& url)
    : ConnectionImpl(dispatcher, fd, url) {}

Network::ClientConnectionPtr ClientConnectionImpl::create(Event::DispatcherImpl& dispatcher,
                                                          const std::string& url) {
  if (url.find(Network::Utility::TCP_SCHEME) == 0) {
    return Network::ClientConnectionPtr{new Network::TcpClientConnectionImpl(dispatcher, url)};
  } else if (url.find(Network::Utility::UNIX_SCHEME) == 0) {
    return Network::ClientConnectionPtr{new Network::UdsClientConnectionImpl(dispatcher, url)};
  } else {
    throw EnvoyException(fmt::format("malformed url: {}", url));
  }
}

TcpClientConnectionImpl::TcpClientConnectionImpl(Event::DispatcherImpl& dispatcher,
                                                 const std::string& url)
    : ClientConnectionImpl(dispatcher, socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0), url) {}

void TcpClientConnectionImpl::connect() {
  AddrInfoPtr addr_info = Utility::resolveTCP(Utility::hostFromUrl(remote_address_),
                                              Utility::portFromUrl(remote_address_));
  int rc = ::connect(fd_, addr_info->ai_addr, addr_info->ai_addrlen);
  ASSERT(rc == -1 && errno == EINPROGRESS);
  UNREFERENCED_PARAMETER(rc);
  connecting_ = true;
}

UdsClientConnectionImpl::UdsClientConnectionImpl(Event::DispatcherImpl& dispatcher,
                                                 const std::string& url)
    : ClientConnectionImpl(dispatcher, socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0), url) {}

void UdsClientConnectionImpl::connect() {
  // sockaddr_un addr = Utility::resolveUnixDomainSocket(Utility::pathFromUrl(remote_address_));
  ASSERT(false); // bufferevent_socket_connect(bev_.get(), reinterpret_cast<sockaddr*>(&addr),
                 // sizeof(sockaddr_un));
}

} // Network
