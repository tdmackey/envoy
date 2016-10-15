#include "common.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/enum_to_int.h"
#include "common/common/utility.h"
#include "common/http/headers.h"
#include "common/http/message_impl.h"
#include "common/http/utility.h"

namespace Grpc {

const std::string Common::GRPC_CONTENT_TYPE{"application/grpc"};

void Common::chargeStat(Stats::Store& store, const std::string& cluster,
                        const std::string& grpc_service, const std::string& grpc_method,
                        bool success) {
  store.counter(fmt::format("cluster.{}.grpc.{}.{}.{}", cluster, grpc_service, grpc_method,
                            success ? "success" : "failure")).inc();
  store.counter(fmt::format("cluster.{}.grpc.{}.{}.total", cluster, grpc_service, grpc_method))
      .inc();
}

Buffer::InstancePtr Common::serializeBody(const google::protobuf::Message& message) {
  // http://www.grpc.io/docs/guides/wire.html
  Buffer::InstancePtr body(new Buffer::OwnedImpl());
  uint8_t compressed = 0;
  body->add(&compressed, sizeof(compressed));
  uint32_t size = htonl(message.ByteSize());
  body->add(&size, sizeof(size));
  body->add(message.SerializeAsString());

  return body;
}

Http::MessagePtr Common::prepareHeaders(const std::string& upstream_cluster,
                                        const std::string& service_full_name,
                                        const std::string& method_name) {
  Http::MessagePtr message(new Http::RequestMessageImpl());
  message->headers().Scheme().value(Http::Headers::get().SchemeValues.Http);
  message->headers().Method().value(Http::Headers::get().MethodValues.Post);
  message->headers().Path().value(fmt::format("/{}/{}", service_full_name, method_name));
  message->headers().Host().value(upstream_cluster);
  message->headers().ContentType().value(Common::GRPC_CONTENT_TYPE);

  return message;
}

void Common::checkForHeaderOnlyError(Http::Message& http_response) {
  // First check for grpc-status in headers. If it is here, we have an error.
  const Http::HeaderEntry& grpc_status_header = http_response.headers().GrpcStatus();
  if (!grpc_status_header.present()) {
    return;
  }

  uint64_t grpc_status_code;
  if (!StringUtil::atoul(grpc_status_header.value().c_str(), grpc_status_code)) {
    throw Exception(Optional<uint64_t>(), "bad grpc-status header");
  }

  // fixfix
  Http::HeaderString grpc_status_message = http_response.headers().GrpcMessage().value();
  throw Exception(grpc_status_code, grpc_status_message.c_str());
}

void Common::validateResponse(Http::Message& http_response) {
  if (Http::Utility::getResponseStatus(http_response.headers()) != enumToInt(Http::Code::OK)) {
    throw Exception(Optional<uint64_t>(), "non-200 response code");
  }

  checkForHeaderOnlyError(http_response);

  // Check for existence of trailers.
  if (!http_response.trailers()) {
    throw Exception(Optional<uint64_t>(), "no response trailers");
  }

  // fixfix
  Http::HeaderString grpc_status_header = http_response.trailers()->GrpcStatus().value();
  Http::HeaderString grpc_status_message = http_response.trailers()->GrpcMessage().value();
  uint64_t grpc_status_code;
  if (!StringUtil::atoul(grpc_status_header.c_str(), grpc_status_code)) {
    throw Exception(Optional<uint64_t>(), "bad grpc-status trailer");
  }

  if (grpc_status_code != 0) {
    throw Exception(grpc_status_code, grpc_status_message.c_str());
  }
}

} // Grpc
