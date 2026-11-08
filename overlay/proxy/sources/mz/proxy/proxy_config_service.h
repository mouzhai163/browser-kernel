// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MZ_PROXY_PROXY_CONFIG_SERVICE_H_
#define MZ_PROXY_PROXY_CONFIG_SERVICE_H_

#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/sequence_checker.h"
#include "net/proxy_resolution/proxy_config_service.h"
#include "net/proxy_resolution/proxy_config_with_annotation.h"

namespace mz {

// Custom ProxyConfigService for MZ browser that allows dynamic
// proxy configuration updates via IPC.
class MzProxyConfigService : public net::ProxyConfigService {
 public:
  MzProxyConfigService();
  ~MzProxyConfigService() override;

  MzProxyConfigService(const MzProxyConfigService&) = delete;
  MzProxyConfigService& operator=(const MzProxyConfigService&) =
      delete;

  // ProxyConfigService implementation
  void AddObserver(Observer* observer) override;
  void RemoveObserver(Observer* observer) override;
  ConfigAvailability GetLatestProxyConfig(
      net::ProxyConfigWithAnnotation* config) override;

  // Update proxy configuration (called from IPC handler)
  void UpdateProxyConfig(const net::ProxyConfig& config);

  // Clear proxy configuration (use direct connection). Only for environments that are explicitly
  // configured without a proxy.
  void ClearProxyConfig();

  // Block all non-loopback traffic. Used whenever a proxy is expected but missing/invalid:
  // the environment must lose its network instead of silently connecting directly.
  void BlockAllConnections();

 private:
  // Notify all observers that configuration has changed
  void NotifyObservers();

  // Current proxy configuration
  net::ProxyConfigWithAnnotation current_config_;

  // Configuration availability status
  ConfigAvailability availability_;

  // Observer list
  base::ObserverList<Observer>::Unchecked observers_;

  // Sequence checker
  SEQUENCE_CHECKER(sequence_checker_);

  base::WeakPtrFactory<MzProxyConfigService> weak_factory_{this};
};

}  // namespace mz

#endif  // MZ_PROXY_PROXY_CONFIG_SERVICE_H_
