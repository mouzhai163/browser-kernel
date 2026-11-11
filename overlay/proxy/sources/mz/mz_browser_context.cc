// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mz/mz_browser_context.h"

#include "base/memory/raw_ptr.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/run_loop.h"
#include "base/process/process.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "mz/config/fingerprint_config_storage.h"
#include "mz/eventbus/eventbus.h"
#include "mz/proxy/network_context_configurator.h"
#include "mz/proxy/proxy_config_service.h"
#include "mz/proxy/proxy_message_handler.h"

namespace mz {

class MzBrowserContext::WaitState {
 public:
  WaitState() = default;
  ~WaitState() = default;

  raw_ptr<base::RunLoop> run_loop = nullptr;
};

// static
MzBrowserContext* MzBrowserContext::GetInstance() {
  static base::NoDestructor<MzBrowserContext> instance;
  return instance.get();
}

MzBrowserContext::MzBrowserContext() = default;

MzBrowserContext::~MzBrowserContext() = default;

MzProxyConfigService* MzBrowserContext::GetOrCreateProxyConfigService() {
  if (!proxy_config_service_) {
    proxy_config_service_ = std::make_unique<MzProxyConfigService>();
  }
  return proxy_config_service_.get();
}

void MzBrowserContext::Initialize() {
  if (initialized_) {
    LOG(WARNING) << "MzBrowserContext already initialized";
    return;
  }

  LOG(INFO) << "Initializing MzBrowserContext";

  // Create proxy configuration service
  GetOrCreateProxyConfigService();
  network_context_configurator_ = std::make_unique<NetworkContextConfigurator>();

  // Create proxy message handler
  proxy_message_handler_ = std::make_unique<ProxyMessageHandler>(
      proxy_config_service_.get(), network_context_configurator_.get());
  wait_state_ = std::make_unique<WaitState>();
  launch_config_processed_ = false;

  initialized_ = true;
}

void MzBrowserContext::RegisterEventBusHandlers() {
  if (!initialized_) {
    LOG(WARNING)
        << "MzBrowserContext not initialized, cannot register handlers";
    return;
  }

  if (handlers_registered_) {
    return;
  }

  auto& eventbus = eventbus::EventBus::GetInstance();
  if (!eventbus.IsInitialized()) {
    LOG(WARNING) << "EventBus not initialized, proxy handler not registered";
    return;
  }

  eventbus.RegisterHandler(std::move(proxy_message_handler_));
  handlers_registered_ = true;
  LOG(INFO) << "Proxy message handler registered with EventBus";
}

bool MzBrowserContext::IsStartupConfigReady() const {
  return launch_config_processed_ &&
         (!expect_fingerprint_config_ || mz::config::IsFingerprintApplied());
}

void MzBrowserContext::QuitWaitIfReady() {
  if (IsStartupConfigReady() && wait_state_ && wait_state_->run_loop) {
    wait_state_->run_loop->Quit();
  }
}

void MzBrowserContext::MarkLaunchConfigProcessed() {
  if (launch_config_processed_) {
    return;
  }

  launch_config_processed_ = true;
  LOG(INFO) << "Initial MZ LaunchConfig processed (fingerprint config expected: "
            << expect_fingerprint_config_ << ")";
  QuitWaitIfReady();
}

void MzBrowserContext::WaitForInitialLaunchConfig(base::TimeDelta timeout) {
  if (IsStartupConfigReady()) {
    return;
  }

  LOG(INFO) << "Waiting for initial MZ LaunchConfig";

  // The fingerprint config arrives right after the LaunchConfig; allow it this long before
  // giving up. Continuing without it would create renderers with a default fingerprint, which
  // breaks environment isolation, so in that case the browser exits instead.
  constexpr base::TimeDelta kFingerprintApplyTimeout = base::Seconds(15);

  base::RunLoop run_loop(base::RunLoop::Type::kNestableTasksAllowed);
  if (wait_state_) {
    wait_state_->run_loop = &run_loop;
  }

  const base::TimeTicks start = base::TimeTicks::Now();
  base::TimeTicks launch_config_seen;
  bool timed_out = false;
  base::RepeatingTimer poll_timer;
  poll_timer.Start(
      FROM_HERE, base::Milliseconds(10),
      base::BindRepeating(
          [](MzBrowserContext* self, base::RunLoop* loop, base::TimeTicks start,
             base::TimeDelta timeout, base::TimeDelta fingerprint_timeout,
             base::TimeTicks* launch_config_seen, bool* timed_out) {
            if (self->IsStartupConfigReady()) {
              loop->Quit();
              return;
            }
            const base::TimeTicks now = base::TimeTicks::Now();
            if (!self->launch_config_processed_) {
              if (now - start > timeout) {
                LOG(WARNING) << "Timed out waiting for initial MZ LaunchConfig";
                loop->Quit();
              }
              return;
            }
            if (launch_config_seen->is_null()) {
              *launch_config_seen = now;
            }
            if (now - *launch_config_seen > fingerprint_timeout) {
              *timed_out = true;
              loop->Quit();
            }
          },
          base::Unretained(this), &run_loop, start, timeout, kFingerprintApplyTimeout,
          &launch_config_seen, &timed_out));
  run_loop.Run();
  poll_timer.Stop();
  if (wait_state_) {
    wait_state_->run_loop = nullptr;
  }

  if (timed_out && expect_fingerprint_config_ && !mz::config::IsFingerprintApplied()) {
    LOG(ERROR) << "Fingerprint config was not applied within "
               << kFingerprintApplyTimeout.InSeconds()
               << "s of the LaunchConfig; exiting instead of running with a default fingerprint";
    base::Process::TerminateCurrentProcessImmediately(1);
  }
}

void MzBrowserContext::Shutdown() {
  if (!initialized_) {
    return;
  }

  LOG(INFO) << "Shutting down MzBrowserContext";

  proxy_message_handler_.reset();
  // proxy_config_service_ is deliberately kept: ProxyConfigServiceWrapper instances owned by
  // the profiles' ProxyConfigMonitors unregister themselves from it when the profiles are
  // destroyed, which happens after this Shutdown().
  network_context_configurator_.reset();
  wait_state_.reset();
  launch_config_processed_ = false;
  handlers_registered_ = false;

  initialized_ = false;
}

}  // namespace mz
