#include "conn_manager_utility.h"

#include "common/http/headers.h"
#include "common/http/utility.h"
#include "common/network/utility.h"
#include "common/runtime/uuid_util.h"
#include "common/tracing/http_tracer_impl.h"

namespace Http {

void ConnectionManagerUtility::mutateRequestHeaders(Http::HeaderMap& request_headers,
                                                    Network::Connection& connection,
                                                    ConnectionManagerConfig& config,
                                                    Runtime::RandomGenerator& random,
                                                    Runtime::Loader& runtime) {
  // Clean proxy headers.
  request_headers.Connection().remove();
  request_headers.EnvoyInternalRequest().remove();
  request_headers.KeepAlive().remove();
  request_headers.ProxyConnection().remove();
  request_headers.TransferEncoding().remove();
  request_headers.Upgrade().remove();
  request_headers.Version().remove();

  // If we are "using remote address" this means that we create/append to XFF with our immediate
  // peer. Cases where we don't "use remote address" include trusted double proxy where we expect
  // our peer to have already properly set XFF, etc.
  if (config.useRemoteAddress()) {
    if (Network::Utility::isLoopbackAddress(connection.remoteAddress().c_str())) {
      Utility::appendXff(request_headers, config.localAddress());
    } else {
      Utility::appendXff(request_headers, connection.remoteAddress());
    }
    request_headers.ForwardedProto().value(connection.ssl() ? Headers::get().SchemeValues.Https
                                                            : Headers::get().SchemeValues.Http);
  }

  // If we didn't already replace x-forwarded-proto because we are using the remote address, and
  // remote hasn't set it (trusted proxy), we set it, since we then use this for setting scheme.
  if (!request_headers.ForwardedProto().present()) {
    request_headers.ForwardedProto().value(connection.ssl() ? Headers::get().SchemeValues.Https
                                                            : Headers::get().SchemeValues.Http);
  }

  request_headers.Scheme().value(request_headers.ForwardedProto());

  // At this point we can determine whether this is an internal or external request. This is done
  // via XFF, which was set above or we trust.
  bool internal_request = Utility::isInternalRequest(request_headers);

  // Edge request is the request from external clients to front Envoy.
  // Request from front Envoy to the internal service will be treated as not edge request.
  bool edge_request = !internal_request && config.useRemoteAddress();

  // If internal request, set header and do other internal only modifications.
  if (internal_request) {
    request_headers.EnvoyInternalRequest().value(Headers::get().EnvoyInternalRequestValues.True);
  } else {
    if (edge_request) {
      request_headers.EnvoyDownstreamServiceCluster().remove();
    }

    request_headers.EnvoyRetryOn().remove();
    request_headers.EnvoyUpstreamAltStatName().remove();
    request_headers.EnvoyUpstreamRequestTimeoutMs().remove();
    request_headers.EnvoyUpstreamRequestPerTryTimeoutMs().remove();
    request_headers.EnvoyExpectedRequestTimeoutMs().remove();
    request_headers.EnvoyForceTrace().remove();

    for (const Http::LowerCaseString& header : config.routeConfig().internalOnlyHeaders()) {
      request_headers.remove(header);
    }
  }

  if (config.userAgent().valid()) {
    request_headers.EnvoyDownstreamServiceCluster().value(config.userAgent().value());
    if (!request_headers.UserAgent().present()) {
      request_headers.UserAgent().value(config.userAgent().value());
    }
  }

  // If we are an external request, AND we are "using remote address" (see above), we set
  // x-envoy-external-address since this is our first ingress point into the trusted network.
  if (edge_request) {
    request_headers.EnvoyExternalAddress().value(connection.remoteAddress());
  }

  // Generate x-request-id for all edge requests, or if there is none.
  if (config.generateRequestId() && (edge_request || !request_headers.RequestId().present())) {
    std::string uuid = "";

    try {
      uuid = random.uuid();
    } catch (const EnvoyException&) {
      // We could not generate uuid, not a big deal.
      config.stats().named_.failed_generate_uuid_.inc();
    }

    if (!uuid.empty()) {
      request_headers.RequestId().value(uuid);
    }
  }

  if (config.tracingConfig().valid()) {
    Tracing::HttpTracerUtility::mutateHeaders(request_headers, runtime);
  }
}

void ConnectionManagerUtility::mutateResponseHeaders(Http::HeaderMap& response_headers,
                                                     const Http::HeaderMap& request_headers,
                                                     ConnectionManagerConfig& config) {
  response_headers.Connection().remove();
  response_headers.TransferEncoding().remove();
  response_headers.Version().remove();

  for (const Http::LowerCaseString& to_remove : config.routeConfig().responseHeadersToRemove()) {
    response_headers.remove(to_remove);
  }

  for (const std::pair<Http::LowerCaseString, std::string>& to_add :
       config.routeConfig().responseHeadersToAdd()) {
    response_headers.addLowerCase(to_add.first.get(), to_add.second);
  }

  if (request_headers.EnvoyForceTrace().present()) {
    response_headers.RequestId().value(request_headers.RequestId());
  }
}

bool ConnectionManagerUtility::shouldTraceRequest(
    const Http::AccessLog::RequestInfo& request_info,
    const Optional<TracingConnectionManagerConfig>& config) {
  if (!config.valid()) {
    return false;
  }

  switch (config.value().tracing_type_) {
  case Http::TracingType::All:
    return true;
  case Http::TracingType::UpstreamFailure:
    return request_info.failureReason() != Http::AccessLog::FailureReason::None;
  }

  // Compiler enforces switch above to cover all the cases and it's impossible to be here,
  // but compiler complains on missing return statement, this is to make compiler happy.
  NOT_IMPLEMENTED;
}

} // Http
