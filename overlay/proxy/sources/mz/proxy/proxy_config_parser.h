// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MZ_PROXY_PROXY_CONFIG_PARSER_H_
#define MZ_PROXY_PROXY_CONFIG_PARSER_H_

#include <optional>
#include <string>
#include <vector>

#include "net/base/auth.h"
#include "net/base/host_port_pair.h"
#include "net/proxy_resolution/proxy_config.h"

namespace mz {

struct ProxyEndpoint {
  ProxyEndpoint();
  ProxyEndpoint(const ProxyEndpoint&);
  ProxyEndpoint& operator=(const ProxyEndpoint&);
  ~ProxyEndpoint();

  bool operator<(const ProxyEndpoint& other) const;
  bool operator==(const ProxyEndpoint& other) const;

  std::string scheme;
  net::HostPortPair host_port_pair;
};

struct ParsedProxyAuthEntry {
  ParsedProxyAuthEntry();
  ParsedProxyAuthEntry(const ParsedProxyAuthEntry&);
  ParsedProxyAuthEntry& operator=(const ParsedProxyAuthEntry&);
  ~ParsedProxyAuthEntry();

  ProxyEndpoint proxy_endpoint;
  net::AuthCredentials credentials;
};

struct ParsedProxyConfig {
  ParsedProxyConfig();
  ParsedProxyConfig(const ParsedProxyConfig&);
  ParsedProxyConfig& operator=(const ParsedProxyConfig&);
  ~ParsedProxyConfig();

  bool HasAuthentication() const;

  std::string mode;
  std::string server;
  std::string bypass_list;
  net::ProxyConfig config;
  std::vector<ParsedProxyAuthEntry> auth_entries;
};

// Parser for proxy configuration payloads sent over MZ IPC.
class ProxyConfigParser {
 public:
  static std::optional<ParsedProxyConfig> ParseProxyConfig(
      const std::string& json);

  static std::optional<net::ProxyConfig> ParseFromJson(
      const std::string& json);

  static bool ValidateConfig(const net::ProxyConfig& config);
};

}  // namespace mz

#endif  // MZ_PROXY_PROXY_CONFIG_PARSER_H_
