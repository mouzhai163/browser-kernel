// Copyright 2024 Simprint. All rights reserved.
// 指纹配置管理实现

#include "mz/fingerprint/fingerprint_config.h"
#include "mz/fingerprint/network_location_fingerprint.h"
#include "mz/fingerprint/screen_hardware_fingerprint.h"
#include "mz/fingerprint/browser_fingerprint.h"
#include "mz/fingerprint/behavior_device_settings.h"
#include "mz/fingerprint/window_settings.h"
#include "mz/config/fingerprint_config_storage.h"

#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "third_party/icu/source/i18n/unicode/timezone.h"
#include "third_party/icu/source/common/unicode/unistr.h"

#include <sstream>

// 统一输出到 Chromium 日志 + 当前标签页 DevTools Console
#define MZ_LOG(msg) LOG(INFO) << msg

namespace mz {
namespace fingerprint {

namespace {

bool IsSimpleLocaleTag(const std::string& tag) {
  if (tag.empty() || tag.size() > 16) {
    return false;
  }
  for (char c : tag) {
    if (!base::IsAsciiAlpha(c) && !base::IsAsciiDigit(c) && c != '-' && c != '_') {
      return false;
    }
  }
  return true;
}

bool IsKnownTimeZone(const std::string& name) {
  if (name.empty() || name.size() > 64) {
    return false;
  }
  std::unique_ptr<icu::TimeZone> zone(
      icu::TimeZone::createTimeZone(icu::UnicodeString::fromUTF8(name)));
  icu::UnicodeString id;
  zone->getID(id);
  // ICU maps unknown names to "Etc/Unknown".
  return id != icu::UnicodeString("Etc/Unknown");
}

}  // namespace

bool ValidateImportedConfig(std::string* error) {
  auto fail = [error](const std::string& message) {
    if (error) {
      *error = message;
    }
    return false;
  };

  const std::string language = mz::config::GetLanguage();
  if (!IsSimpleLocaleTag(language) || base::EqualsCaseInsensitiveASCII(language, "ip")) {
    return fail("语言无效或未解析: '" + language + "'");
  }

  const std::string timezone = mz::config::GetTimezone();
  if (!IsKnownTimeZone(timezone)) {
    return fail("时区无效或未解析: '" + timezone + "'");
  }

  if (mz::config::GetUserAgent().empty()) {
    return fail("缺少 User-Agent");
  }

  // 地理位置：配置了就必须是合法的 "纬度,经度"（未解析的 "ip" 也会在这里被拒绝）。
  const std::string geolocation = mz::config::GetGeolocation();
  if (!geolocation.empty()) {
    const size_t comma = geolocation.find(',');
    double latitude = 0;
    double longitude = 0;
    if (comma == std::string::npos ||
        !base::StringToDouble(geolocation.substr(0, comma), &latitude) ||
        !base::StringToDouble(geolocation.substr(comma + 1), &longitude) ||
        latitude < -90 || latitude > 90 || longitude < -180 || longitude > 180) {
      return fail("地理位置无效或未解析: '" + geolocation + "'");
    }
  }

  // 配置了就必须在合理范围内；没配置（0）由内核默认处理。
  int width = 0;
  int height = 0;
  mz::config::GetResolutionWidthHeight(&width, &height);
  if ((width != 0 || height != 0) && (width < 200 || width > 16384 || height < 200 || height > 16384)) {
    return fail("屏幕分辨率超出范围: " + base::NumberToString(width) + "x" + base::NumberToString(height));
  }
  const double dpr = mz::config::GetDevicePixelRatio();
  if (dpr > 8) {
    return fail("devicePixelRatio 超出范围: " + base::NumberToString(dpr));
  }
  const int cores = mz::config::GetHardwareConcurrency();
  if (cores > 128) {
    return fail("hardwareConcurrency 超出范围: " + base::NumberToString(cores));
  }

  // 系统版本必须是 "主.次.补丁" 三段数字（如 10.0.0、15.0.0），写错了不如直接报错。
  // 注意：这里读的是已经导入到存储里的值，所以和 platform_version 的 Apply 顺序无关。
  const std::string platform_version = mz::config::GetPlatformVersion();
  if (!platform_version.empty()) {
    int dots = 0;
    bool valid = platform_version.size() <= 16;
    for (char c : platform_version) {
      if (c == '.') {
        ++dots;
      } else if (c < '0' || c > '9') {
        valid = false;
      }
    }
    if (!valid || dots != 2 || platform_version.front() == '.' || platform_version.back() == '.' ||
        platform_version.find("..") != std::string::npos) {
      return fail("platform_version 无效: '" + platform_version + "'");
    }
  }

  // 未知的 WebRTC 模式会被内核按"禁用"处理，但配置本身写错了也应该让用户知道。
  const std::string webrtc = mz::config::GetWebRTCMode();
  if (!webrtc.empty() && webrtc != "real" && webrtc != "disable" &&
      webrtc != "disable_non_proxied" && webrtc != "replace") {
    return fail("WebRTC 模式无效: '" + webrtc + "'");
  }
  return true;
}

std::string BuildEffectiveConfigSummary() {
  int width = 0;
  int height = 0;
  mz::config::GetResolutionWidthHeight(&width, &height);
  base::DictValue summary;
  summary.Set("language", mz::config::GetLanguage());
  summary.Set("timezone", mz::config::GetTimezone());
  summary.Set("geolocation", mz::config::GetGeolocation());
  summary.Set("user_agent", mz::config::GetUserAgent());
  summary.Set("resolution", base::NumberToString(width) + "x" + base::NumberToString(height));
  return base::WriteJson(summary).value_or(std::string());
}

// FingerprintConfig 构造函数和析构函数
FingerprintConfig::FingerprintConfig() = default;
FingerprintConfig::~FingerprintConfig() = default;

// FingerprintConfigManager 构造函数和析构函数
FingerprintConfigManager::FingerprintConfigManager() = default;
FingerprintConfigManager::~FingerprintConfigManager() = default;

// static
FingerprintConfigManager* FingerprintConfigManager::GetInstance() {
  static base::NoDestructor<FingerprintConfigManager> instance;
  return instance.get();
}

// static
FingerprintMode FingerprintConfigManager::ParseFingerprintMode(
    const std::string& mode_str) {
  if (mode_str == "random") return FingerprintMode::kRandom;
  if (mode_str == "real") return FingerprintMode::kReal;
  if (mode_str == "custom") return FingerprintMode::kCustom;
  if (mode_str == "system") return FingerprintMode::kSystem;
  return FingerprintMode::kSystem;  // 默认
}

// static
WebRTCMode FingerprintConfigManager::ParseWebRTCMode(
    const std::string& mode_str) {
  if (mode_str == "replace") return WebRTCMode::kReplace;
  if (mode_str == "real") return WebRTCMode::kReal;
  if (mode_str == "disable") return WebRTCMode::kDisable;
  return WebRTCMode::kDisable;  // 默认
}

// static
WebGPUMode FingerprintConfigManager::ParseWebGPUMode(
    const std::string& mode_str) {
  if (mode_str == "webgl-match") return WebGPUMode::kWebGLMatch;
  if (mode_str == "real") return WebGPUMode::kReal;
  if (mode_str == "disable") return WebGPUMode::kDisable;
  return WebGPUMode::kWebGLMatch;  // 默认
}

bool FingerprintConfigManager::ApplyConfigFromJson(const std::string& json_str) {
  MZ_LOG("开始解析指纹配置...");

  // 首先导入配置到全局存储（供 Font Access API 等使用）
  mz::config::ImportAllConfigFromJson(json_str);

  auto value = base::JSONReader::Read(json_str, base::JSON_PARSE_RFC);
  if (!value || !value->is_dict()) {
    MZ_LOG("错误: 无法解析 JSON 配置");
    return false;
  }

  const base::DictValue& dict = value->GetDict();

  // 解析所有字段
  if (const std::string* val = dict.FindString("language"))
    config_.language = *val;
  if (const std::string* val = dict.FindString("interface_language"))
    config_.interface_language = *val;
  if (const std::string* val = dict.FindString("timezone"))
    config_.timezone = *val;
  if (const std::string* val = dict.FindString("geolocation_prompt"))
    config_.geolocation_prompt = *val;
  if (const std::string* val = dict.FindString("geolocation"))
    config_.geolocation = *val;
  if (const std::string* val = dict.FindString("platform"))
    config_.platform = *val;
  if (const std::string* val = dict.FindString("platform_version"))
    config_.platform_version = *val;
  if (const std::string* val = dict.FindString("user_agent"))
    config_.user_agent = *val;

  if (std::optional<bool> val = dict.FindBool("sound"))
    config_.sound = *val;
  if (std::optional<bool> val = dict.FindBool("images"))
    config_.images = *val;
  if (std::optional<bool> val = dict.FindBool("video"))
    config_.video = *val;
  if (std::optional<bool> val = dict.FindBool("do_not_track"))
    config_.do_not_track = *val;

  if (const std::string* val = dict.FindString("window_size"))
    config_.window_size = *val;
  if (std::optional<int> val = dict.FindInt("window_width"))
    config_.window_width = *val;
  if (std::optional<int> val = dict.FindInt("window_height"))
    config_.window_height = *val;
  if (const std::string* val = dict.FindString("window_position"))
    config_.window_position = *val;
  if (std::optional<int> val = dict.FindInt("window_x"))
    config_.window_x = *val;
  if (std::optional<int> val = dict.FindInt("window_y"))
    config_.window_y = *val;

  // 解析 resolution 对象格式 {"width": 1920, "height": 1080}
  if (const base::Value* resolution_obj = dict.Find("resolution")) {
    if (resolution_obj->is_dict()) {
      const base::DictValue& res_dict = resolution_obj->GetDict();
      if (auto width = res_dict.FindInt("width")) {
        if (auto height = res_dict.FindInt("height")) {
          // 转换为 "widthxheight" 格式
          config_.resolution = std::to_string(*width) + "x" + std::to_string(*height);
          MZ_LOG("Resolution 解析: " << *config_.resolution);
        }
      }
    }
  }

  if (std::optional<int> val = dict.FindInt("color_depth"))
    config_.color_depth = *val;
  if (std::optional<double> val = dict.FindDouble("device_pixel_ratio"))
    config_.device_pixel_ratio = *val;
  if (std::optional<int> val = dict.FindInt("max_touch_points"))
    config_.max_touch_points = *val;
  if (std::optional<int> val = dict.FindInt("hardware_concurrency"))
    config_.hardware_concurrency = *val;
  if (std::optional<int> val = dict.FindInt("device_memory"))
    config_.device_memory = *val;

  if (const std::string* val = dict.FindString("canvas"))
    config_.canvas = *val;
  if (const std::string* val = dict.FindString("webgl_image"))
    config_.webgl_image = *val;
  if (const std::string* val = dict.FindString("webgl_info"))
    config_.webgl_info = *val;
  if (const std::string* val = dict.FindString("webgl_vendor"))
    config_.webgl_vendor = *val;
  if (const std::string* val = dict.FindString("webgl_renderer"))
    config_.webgl_renderer = *val;
  if (const std::string* val = dict.FindString("webgpu"))
    config_.webgpu = *val;
  if (const std::string* val = dict.FindString("font_fingerprint"))
    config_.font_fingerprint = *val;

  // 解析 font_list 对象格式 {"mode": "random", "fonts": ["Arial", "Calibri", ...]}
  if (const base::Value* font_list_obj = dict.Find("font_list")) {
    if (font_list_obj->is_dict()) {
      const base::DictValue& font_dict = font_list_obj->GetDict();

      // 解析 mode
      if (const std::string* mode = font_dict.FindString("mode")) {
        config_.font_list_mode = *mode;
      }

      // 解析 fonts 数组
      if (const base::ListValue* fonts_list = font_dict.FindList("fonts")) {
        std::vector<std::string> fonts;
        for (const auto& font_value : *fonts_list) {
          if (font_value.is_string()) {
            fonts.push_back(font_value.GetString());
          }
        }
        config_.font_list = fonts;
        MZ_LOG("Font list 解析: mode=" << (config_.font_list_mode ? *config_.font_list_mode : "未设置")
                     << ", fonts=" << fonts.size());
      }
    }
  }

  if (const std::string* val = dict.FindString("audio_context"))
    config_.audio_context = *val;
  if (const std::string* val = dict.FindString("speech_voices"))
    config_.speech_voices = *val;
  if (const std::string* val = dict.FindString("client_rects"))
    config_.client_rects = *val;
  if (const std::string* val = dict.FindString("media_devices"))
    config_.media_devices = *val;
  if (const std::string* val = dict.FindString("webrtc"))
    config_.webrtc = *val;

  if (const std::string* val = dict.FindString("device_name"))
    config_.device_name = *val;
  if (std::optional<bool> val = dict.FindBool("device_name_random"))
    config_.device_name_random = *val;
  if (const std::string* val = dict.FindString("mac_address"))
    config_.mac_address = *val;
  if (const std::string* val = dict.FindString("mac_address_mode"))
    config_.mac_address_mode = *val;
  if (std::optional<bool> val = dict.FindBool("ssl_fingerprint"))
    config_.ssl_fingerprint = *val;
  if (std::optional<bool> val = dict.FindBool("port_scan_protection"))
    config_.port_scan_protection = *val;
  if (const std::string* val = dict.FindString("scan_whitelist"))
    config_.scan_whitelist = *val;
  if (std::optional<bool> val = dict.FindBool("hardware_acceleration"))
    config_.hardware_acceleration = *val;
  if (std::optional<bool> val = dict.FindBool("disable_sandbox"))
    config_.disable_sandbox = *val;
  if (const std::string* val = dict.FindString("startup_parameters"))
    config_.startup_parameters = *val;

  if (std::optional<bool> val = dict.FindBool("random_fingerprint_on_launch"))
    config_.random_fingerprint_on_launch = *val;

  config_loaded_ = true;

  MZ_LOG("指纹配置解析完成");
  MZ_LOG("  - Canvas: " << (config_.canvas ? *config_.canvas : "未设置"));
  MZ_LOG("  - WebGL Vendor: " << (config_.webgl_vendor ? *config_.webgl_vendor : "未设置"));
  MZ_LOG("  - Hardware Concurrency: " << (config_.hardware_concurrency ? std::to_string(*config_.hardware_concurrency) : "未设置"));

  // 应用所有指纹配置
  MZ_LOG("开始应用指纹配置...");

  // 2.2 网络与定位指纹
  NetworkLocationFingerprint::ApplyLanguage(config_);
  NetworkLocationFingerprint::ApplyTimezone(config_);
  NetworkLocationFingerprint::ApplyGeolocation(config_);
  NetworkLocationFingerprint::ApplyWebRTC(config_);
  NetworkLocationFingerprint::ApplyPlatform(config_);
  NetworkLocationFingerprint::ApplyUserAgent(config_);

  // 2.3 屏幕与硬件指纹
  ScreenHardwareFingerprint::ApplyResolution(config_);
  ScreenHardwareFingerprint::ApplyColorDepth(config_);
  ScreenHardwareFingerprint::ApplyDevicePixelRatio(config_);
  ScreenHardwareFingerprint::ApplyMaxTouchPoints(config_);
  ScreenHardwareFingerprint::ApplyHardwareConcurrency(config_);
  ScreenHardwareFingerprint::ApplyDeviceMemory(config_);

  // 2.4 浏览器指纹
  BrowserFingerprint::ApplyCanvas(config_);
  BrowserFingerprint::ApplyWebGLImage(config_);
  BrowserFingerprint::ApplyWebGLInfo(config_);
  BrowserFingerprint::ApplyWebGLVendorRenderer(config_);
  BrowserFingerprint::ApplyWebGPU(config_);
  BrowserFingerprint::ApplyFontFingerprint(config_);
  BrowserFingerprint::ApplyFontList(config_);
  BrowserFingerprint::ApplyAudioContext(config_);
  BrowserFingerprint::ApplySpeechVoices(config_);
  BrowserFingerprint::ApplyClientRects(config_);
  BrowserFingerprint::ApplyMediaDevices(config_);

  // 2.5 浏览器行为
  BehaviorDeviceSettings::ApplyDoNotTrack(config_);
  BehaviorDeviceSettings::StoreMediaPermissions(config_);  // 存储配置，稍后在 HostContentSettingsMap 创建时应用
  BehaviorDeviceSettings::ApplySSLFingerprint(config_);
  BehaviorDeviceSettings::ApplyPortScanProtection(config_);
  BehaviorDeviceSettings::ApplyHardwareAcceleration(config_);

  // 2.6 设备设置
  BehaviorDeviceSettings::ApplyDeviceName(config_);
  BehaviorDeviceSettings::ApplyMacAddress(config_);
  BehaviorDeviceSettings::ApplyStartupParameters(config_);
  BehaviorDeviceSettings::ApplySandboxSettings(config_);

  // 2.7 窗口设置
  WindowSettings::ApplyWindowSize(config_);
  WindowSettings::ApplyWindowPosition(config_);

  MZ_LOG("所有指纹配置应用完成");

  return true;
}

}  // namespace fingerprint
}  // namespace mz
