// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MZ_MZ_BROWSER_CONTEXT_H_
#define MZ_MZ_BROWSER_CONTEXT_H_

#include <memory>

#include "base/no_destructor.h"
#include "base/time/time.h"

namespace mz {

class ProxyMessageHandler;
class NetworkContextConfigurator;
class MzProxyConfigService;

// Global context for MZ browser features
class MzBrowserContext {
 public:
  static MzBrowserContext* GetInstance();

  MzBrowserContext(const MzBrowserContext&) = delete;
  MzBrowserContext& operator=(const MzBrowserContext&) = delete;

  // Initialize the context (called during browser startup)
  void Initialize();

  // Register handlers with EventBus after it is initialized.
  void RegisterEventBusHandlers();

  void MarkLaunchConfigProcessed();

  // The LaunchConfig says whether the app will also send a fingerprint config
  // (Topic::kFingerprintApply). If it does, startup must not continue until that config has
  // been applied, otherwise the first renderers would be created with a default fingerprint.
  void SetExpectFingerprintConfig(bool expect) { expect_fingerprint_config_ = expect; }
  void WaitForInitialLaunchConfig(base::TimeDelta timeout);

  // Shutdown the context (called during browser shutdown)
  void Shutdown();

  // Get the proxy configuration service
  MzProxyConfigService* proxy_config_service() {
    return proxy_config_service_.get();
  }

  // Get the proxy message handler
  ProxyMessageHandler* proxy_message_handler() {
    return proxy_message_handler_.get();
  }

  NetworkContextConfigurator* network_context_configurator() {
    return network_context_configurator_.get();
  }

 private:
  friend class base::NoDestructor<MzBrowserContext>;

  class WaitState;

  MzBrowserContext();
  ~MzBrowserContext();

  std::unique_ptr<MzProxyConfigService> proxy_config_service_;
  std::unique_ptr<NetworkContextConfigurator> network_context_configurator_;
  std::unique_ptr<ProxyMessageHandler> proxy_message_handler_;
  std::unique_ptr<WaitState> wait_state_;
  bool IsStartupConfigReady() const;
  void QuitWaitIfReady();

  bool launch_config_processed_ = false;
  bool expect_fingerprint_config_ = false;
  bool handlers_registered_ = false;
  bool initialized_ = false;
};

}  // namespace mz

#endif  // MZ_MZ_BROWSER_CONTEXT_H_
