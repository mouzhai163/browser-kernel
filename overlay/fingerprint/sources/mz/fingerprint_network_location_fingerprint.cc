// Copyright 2024 Simprint. All rights reserved.
// 网络与定位指纹修改实现

#include "mz/fingerprint/network_location_fingerprint.h"

#include "mz/fingerprint/fingerprint_config.h"
#include "mz/config/fingerprint_config_storage.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"

#include <sstream>

#define MZ_LOG(msg) LOG(INFO) << msg

namespace mz {
namespace fingerprint {

// static
void NetworkLocationFingerprint::ApplyLanguage(const FingerprintConfig& config) {
  LOG(INFO) << "[MZ Language] ApplyLanguage called, has_value: "
            << config.language.has_value();

  if (config.language.has_value()) {
    LOG(INFO) << "[MZ Language] ApplyLanguage setting language to: "
              << config.language.value();
    mz::config::SetLanguage(config.language.value());
    MZ_LOG("Language 配置已应用: " << config.language.value());
  } else {
    LOG(INFO) << "[MZ Language] ApplyLanguage - language not set in config, skipping";
  }

  // 注意: navigator.language 的拦截已在 navigator_language.cc 中实现
  // interface_language 主要影响浏览器 UI 语言，可能需要在启动时通过命令行参数设置
}

// static
void NetworkLocationFingerprint::ApplyTimezone(const FingerprintConfig& config) {
  if (!config.timezone.has_value()) {
    MZ_LOG("Timezone 配置未设置，跳过");
    return;
  }

  mz::config::SetTimezone(config.timezone.value());
  MZ_LOG("Timezone 配置已应用: " << config.timezone.value());

  // TODO: 需要设置 ICU 时区
  // 实现位置: base/i18n/icu_util.cc 或通过环境变量 TZ
}

// static
void NetworkLocationFingerprint::ApplyGeolocation(const FingerprintConfig& config) {
  if (!config.geolocation.has_value()) {
    MZ_LOG("Geolocation 配置未设置，跳过");
    return;
  }

  const std::string& geo_config = config.geolocation.value();
  mz::config::SetGeolocation(geo_config);
  MZ_LOG("Geolocation 配置已应用: " << geo_config);

  // 应用 geolocationPrompt 配置
  if (config.geolocation_prompt.has_value()) {
    mz::config::SetGeolocationPrompt(config.geolocation_prompt.value());
    MZ_LOG("Geolocation Prompt 配置已应用: " << config.geolocation_prompt.value());
  }
}

// static
void NetworkLocationFingerprint::ApplyWebRTC(const FingerprintConfig& config) {
  if (!config.webrtc.has_value()) {
    MZ_LOG("WebRTC 配置未设置，跳过");
    return;
  }

  mz::config::SetWebRTCMode(config.webrtc.value());
  MZ_LOG("WebRTC 配置已应用: " << config.webrtc.value());

  // WebRTC IP 泄露防护已在 rtc_peer_connection_handler.cc 中实现
  // 可能的模式: "disable_non_proxied" (只允许relay), "disable" (完全禁用), "real" (真实)
}

// static
void NetworkLocationFingerprint::ApplyPlatform(const FingerprintConfig& config) {
  if (config.platform_version.has_value()) {
    mz::config::SetPlatformVersion(config.platform_version.value());
    MZ_LOG("PlatformVersion 配置已应用: " << config.platform_version.value());
  }
  if (!config.platform.has_value()) {
    MZ_LOG("Platform 配置未设置，跳过");
    return;
  }

  mz::config::SetPlatform(config.platform.value());
  MZ_LOG("Platform 配置已应用: " << config.platform.value());
}

// static
void NetworkLocationFingerprint::ApplyUserAgent(const FingerprintConfig& config) {
  if (!config.user_agent.has_value()) {
    MZ_LOG("UserAgent 配置未设置，跳过");
    return;
  }

  mz::config::SetUserAgent(config.user_agent.value());
  MZ_LOG("UserAgent 配置已应用: " << config.user_agent.value());
}

// static
std::string NetworkLocationFingerprint::GetConfiguredLanguage() {
  return mz::config::GetLanguage();
}

// static
std::string NetworkLocationFingerprint::GetConfiguredTimezone() {
  return mz::config::GetTimezone();
}

// static
std::string NetworkLocationFingerprint::GetConfiguredNavigatorPlatform() {
  // 配置里存的是操作系统名（Windows / macOS / Linux，与 Sec-CH-UA-Platform 相同），
  // navigator.platform 用的是另一套取值。
  const std::string platform = base::ToLowerASCII(mz::config::GetPlatform());
  if (platform == "windows" || platform == "win32") {
    return "Win32";
  }
  if (platform == "macos" || platform == "mac" || platform == "macintel") {
    return "MacIntel";
  }
  if (platform == "linux" || platform == "linux x86_64") {
    return "Linux x86_64";
  }
  return std::string();
}

// static
std::string NetworkLocationFingerprint::GetConfiguredUserAgent() {
  std::string ua = mz::config::GetUserAgent();
  return ua;
}

// static
bool NetworkLocationFingerprint::GetConfiguredGeolocation(double* latitude, double* longitude) {
  if (!latitude || !longitude) {
    return false;
  }

  std::string geo_config = mz::config::GetGeolocation();
  if (geo_config.empty()) {
    return false;
  }

  // 解析格式: "latitude,longitude"
  size_t comma_pos = geo_config.find(',');
  if (comma_pos != std::string::npos) {
    std::string lat_str = geo_config.substr(0, comma_pos);
    std::string lon_str = geo_config.substr(comma_pos + 1);

    double lat, lon;
    if (base::StringToDouble(lat_str, &lat) && base::StringToDouble(lon_str, &lon)) {
      *latitude = lat;
      *longitude = lon;
      return true;
    }
  }

  return false;
}

// static
std::string NetworkLocationFingerprint::GetConfiguredWebRTCMode() {
  return mz::config::GetWebRTCMode();
}

// static
std::string NetworkLocationFingerprint::GetConfiguredGeolocationPrompt() {
  return mz::config::GetGeolocationPrompt();
}

}  // namespace fingerprint
}  // namespace mz
