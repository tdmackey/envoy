#include "sds.h"

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/http/codes.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/health_checker.h"

#include "common/common/enum_to_int.h"
#include "common/http/headers.h"
#include "common/http/message_impl.h"
#include "common/http/utility.h"
#include "common/json/json_loader.h"
#include "common/network/utility.h"

namespace Upstream {

SdsClusterImpl::SdsClusterImpl(const Json::Object& config, Runtime::Loader& runtime,
                               Stats::Store& stats, Ssl::ContextManager& ssl_context_manager,
                               const SdsConfig& sds_config, ClusterManager& cm,
                               Event::Dispatcher& dispatcher, Runtime::RandomGenerator& random)
    : BaseDynamicClusterImpl(config, runtime, stats, ssl_context_manager), cm_(cm),
      sds_config_(sds_config), service_name_(config.getString("service_name")), random_(random),
      refresh_timer_(dispatcher.createTimer([this]() -> void { refreshHosts(); })) {}

SdsClusterImpl::~SdsClusterImpl() {}

void SdsClusterImpl::onSuccess(Http::MessagePtr&& response) {
  uint64_t response_code = Http::Utility::getResponseStatus(response->headers());
  if (response_code != enumToInt(Http::Code::OK)) {
    onFailure(Http::AsyncClient::FailureReason::Reset);
    return;
  }

  try {
    parseSdsResponse(*response);
  } catch (EnvoyException& e) {
    onFailure(Http::AsyncClient::FailureReason::Reset);
    return;
  }

  stats_.update_success_.inc();
  requestComplete();
}

void SdsClusterImpl::onFailure(Http::AsyncClient::FailureReason) {
  log_debug("sds refresh failure for cluster: {}", name_);
  stats_.update_failure_.inc();
  requestComplete();
}

void SdsClusterImpl::parseSdsResponse(Http::Message& response) {
  Json::StringLoader json(response.bodyAsString());
  std::vector<HostPtr> new_hosts;
  for (const Json::Object& host : json.getObjectArray("hosts")) {
    bool canary = false;
    uint32_t weight = 1;
    std::string zone = "";
    if (host.hasObject("tags")) {
      canary = host.getObject("tags").getBoolean("canary", canary);
      weight = host.getObject("tags").getInteger("load_balancing_weight", weight);
      zone = host.getObject("tags").getString("az", zone);
    }

    new_hosts.emplace_back(new HostImpl(
        *this, Network::Utility::urlForTcp(host.getString("ip_address"), host.getInteger("port")),
        canary, weight, zone));
  }

  HostVectorPtr current_hosts_copy(new std::vector<HostPtr>(hosts()));
  std::vector<HostPtr> hosts_added;
  std::vector<HostPtr> hosts_removed;
  if (updateDynamicHostList(new_hosts, *current_hosts_copy, hosts_added, hosts_removed,
                            health_checker_ != nullptr)) {
    log_debug("sds hosts changed for cluster: {} ({})", name_, hosts().size());
    HostVectorPtr local_zone_hosts(new std::vector<HostPtr>());
    if (!sds_config_.local_zone_name_.empty()) {
      for (HostPtr host : *current_hosts_copy) {
        if (host->zone() == sds_config_.local_zone_name_) {
          local_zone_hosts->push_back(host);
        }
      }
    }

    updateHosts(current_hosts_copy, createHealthyHostList(*current_hosts_copy), local_zone_hosts,
                createHealthyHostList(*local_zone_hosts), hosts_added, hosts_removed);

    if (initialize_callback_ && health_checker_ && pending_health_checks_ == 0) {
      pending_health_checks_ = hosts().size();
      ASSERT(pending_health_checks_ > 0);
      health_checker_->addHostCheckCompleteCb([this](HostPtr, bool) -> void {
        if (pending_health_checks_ > 0 && --pending_health_checks_ == 0) {
          initialize_callback_();
          initialize_callback_ = nullptr;
        }
      });
    }
  }
}

const std::string Host = "sds";

void SdsClusterImpl::refreshHosts() {
  log_debug("starting sds refresh for cluster: {}", name_);
  stats_.update_attempt_.inc();

  Http::MessagePtr message(new Http::RequestMessageImpl());
  message->headers().Scheme().value(Http::Headers::get().SchemeValues.Http);
  message->headers().Method().value(Http::Headers::get().MethodValues.Get);
  message->headers().Path().value("/v1/registration/" + service_name_);
  message->headers().Host().value(Host);
  active_request_ = cm_.httpAsyncClientForCluster(sds_config_.sds_cluster_name_)
                        .send(std::move(message), *this,
                              Optional<std::chrono::milliseconds>(std::chrono::milliseconds(1000)));
}

void SdsClusterImpl::requestComplete() {
  log_debug("sds refresh complete for cluster: {}", name_);
  // If we didn't setup to initialize when our first round of health checking is complete, just
  // do it now.
  if (initialize_callback_ && pending_health_checks_ == 0) {
    initialize_callback_();
    initialize_callback_ = nullptr;
  }

  active_request_ = nullptr;

  // Add refresh jitter based on the configured interval.
  std::chrono::milliseconds final_delay =
      sds_config_.refresh_delay_ +
      std::chrono::milliseconds(random_.random() % sds_config_.refresh_delay_.count());

  refresh_timer_->enableTimer(final_delay);
}

void SdsClusterImpl::shutdown() {
  if (active_request_) {
    active_request_->cancel();
    active_request_ = nullptr;
  }

  refresh_timer_.reset();
}

} // Upstream
