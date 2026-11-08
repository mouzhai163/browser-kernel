// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mz/proxy/proxy_config_service.h"

#include "base/check.h"
#include "net/base/proxy_server.h"
#include "net/traffic_annotation/network_traffic_annotation.h"

namespace mz {

namespace {

// Traffic annotation for proxy configuration
constexpr net::NetworkTrafficAnnotationTag kProxyConfigTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("mz_proxy_config", R"(
      semantics {
        sender: "MZ Proxy Configuration"
        description:
          "MZ browser uses custom proxy configuration provided via IPC "
          "to route network traffic through specified proxy servers."
        trigger: "User configures proxy settings in MZ application."
        data: "Network requests routed through configured proxy."
        destination: OTHER
        destination_other: "User-configured proxy server"
      }
      policy {
        cookies_allowed: YES
        cookies_store: "user"
        setting: "Users can configure proxy settings in MZ application."
      })");

}  // namespace

// A proxy config that sends everything to a closed loopback port, so every request fails with
// ERR_PROXY_CONNECTION_FAILED. Fixed proxy rules never fall back to DIRECT.
namespace {
net::ProxyConfig MakeBlockedConfig() {
  net::ProxyConfig config;
  config.proxy_rules().ParseFromString("http://127.0.0.1:1");
  return config;
}
}  // namespace

MzProxyConfigService::MzProxyConfigService()
    : current_config_(MakeBlockedConfig(), kProxyConfigTrafficAnnotation),
      availability_(CONFIG_VALID) {
  // Fail closed: until the app tells us what to use, nothing may reach the network
  // (CONFIG_UNSET would be treated as DIRECT).
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

MzProxyConfigService::~MzProxyConfigService() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

void MzProxyConfigService::AddObserver(Observer* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  observers_.AddObserver(observer);
}

void MzProxyConfigService::RemoveObserver(Observer* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  observers_.RemoveObserver(observer);
}

net::ProxyConfigService::ConfigAvailability
MzProxyConfigService::GetLatestProxyConfig(
    net::ProxyConfigWithAnnotation* config) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(config);

  if (availability_ == CONFIG_VALID) {
    *config = current_config_;
  }

  return availability_;
}

void MzProxyConfigService::UpdateProxyConfig(
    const net::ProxyConfig& config) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Check if configuration actually changed
  if (availability_ == CONFIG_VALID && current_config_.value().Equals(config)) {
    return;
  }

  // Update configuration
  current_config_ = net::ProxyConfigWithAnnotation(
      config, kProxyConfigTrafficAnnotation);
  availability_ = CONFIG_VALID;

  // Notify observers
  NotifyObservers();
}

void MzProxyConfigService::ClearProxyConfig() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Create direct connection config
  net::ProxyConfig direct_config = net::ProxyConfig::CreateDirect();
  if (availability_ == CONFIG_VALID && current_config_.value().Equals(direct_config)) {
    return;
  }

  current_config_ = net::ProxyConfigWithAnnotation(
      direct_config, kProxyConfigTrafficAnnotation);
  availability_ = CONFIG_VALID;

  // Notify observers
  NotifyObservers();
}

void MzProxyConfigService::BlockAllConnections() {
  UpdateProxyConfig(MakeBlockedConfig());
}

void MzProxyConfigService::NotifyObservers() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  for (Observer& observer : observers_) {
    observer.OnProxyConfigChanged(current_config_, availability_);
  }
}

}  // namespace mz
