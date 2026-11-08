// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MZ_PROXY_PROXY_CONFIG_SERVICE_WRAPPER_H_
#define MZ_PROXY_PROXY_CONFIG_SERVICE_WRAPPER_H_

#include "base/memory/raw_ptr.h"
#include "net/proxy_resolution/proxy_config_service.h"
#include "net/proxy_resolution/proxy_config_with_annotation.h"

namespace mz {

class MzProxyConfigService;

// Wrapper for MzProxyConfigService that allows multiple observers
// without taking ownership of the underlying service.
// This is needed because ProxyConfigMonitor expects to own the service,
// but we want to share the same MzProxyConfigService instance
// across multiple profiles.
class ProxyConfigServiceWrapper : public net::ProxyConfigService,
                                   public net::ProxyConfigService::Observer {
 public:
  explicit ProxyConfigServiceWrapper(
      MzProxyConfigService* proxy_service);
  ~ProxyConfigServiceWrapper() override;

  ProxyConfigServiceWrapper(const ProxyConfigServiceWrapper&) = delete;
  ProxyConfigServiceWrapper& operator=(const ProxyConfigServiceWrapper&) =
      delete;

  // ProxyConfigService implementation
  void AddObserver(Observer* observer) override;
  void RemoveObserver(Observer* observer) override;
  ConfigAvailability GetLatestProxyConfig(
      net::ProxyConfigWithAnnotation* config) override;

  // ProxyConfigService::Observer implementation
  void OnProxyConfigChanged(
      const net::ProxyConfigWithAnnotation& config,
      ConfigAvailability availability) override;

 private:
  // Pointer to the shared proxy service (not owned)
  raw_ptr<MzProxyConfigService> proxy_service_;

  // Our own observer to forward notifications
  raw_ptr<Observer> observer_ = nullptr;
};

}  // namespace mz

#endif  // MZ_PROXY_PROXY_CONFIG_SERVICE_WRAPPER_H_
