// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_NEW_TAB_PAGE_MZ_IP_MZ_IP_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_NEW_TAB_PAGE_MZ_IP_MZ_IP_HANDLER_H_

#include <memory>
#include <optional>
#include <string>

#include "chrome/browser/ui/webui/new_tab_page/mz_ip/mz_ip.mojom.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "services/network/public/cpp/simple_url_loader.h"

class Profile;

namespace network {
class SharedURLLoaderFactory;
}  // namespace network

// Handles IP detection requests sent from the NTP JS.
// Uses multiple IP detection APIs for redundancy.
class MzIpHandler : public mz_ip::mojom::MzIpHandler {
 public:
  MzIpHandler(
      mojo::PendingReceiver<mz_ip::mojom::MzIpHandler> handler,
      Profile* profile);
  ~MzIpHandler() override;

  MzIpHandler(const MzIpHandler&) = delete;
  MzIpHandler& operator=(const MzIpHandler&) = delete;

  // mz_ip::mojom::MzIpHandler:
  void GetIpInfo(GetIpInfoCallback callback) override;

 private:
  // Tries the next IP API in the list.
  void TryNextApi();

  // Callback when an IP fetch completes.
  void OnIpFetchComplete(std::optional<std::string> response_body);

  mojo::Receiver<mz_ip::mojom::MzIpHandler> handler_;
  raw_ptr<Profile> profile_;
  std::unique_ptr<network::SimpleURLLoader> url_loader_;

  // Current API index being tried.
  size_t current_api_index_ = 0;

  // Pending callback to return results.
  GetIpInfoCallback pending_callback_;
};

#endif  // CHROME_BROWSER_UI_WEBUI_NEW_TAB_PAGE_MZ_IP_MZ_IP_HANDLER_H_
