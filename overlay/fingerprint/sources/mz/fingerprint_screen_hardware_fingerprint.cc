// Copyright 2024 Simprint. All rights reserved.
// 屏幕与硬件指纹修改实现

#include "mz/fingerprint/screen_hardware_fingerprint.h"

#include "mz/fingerprint/fingerprint_config.h"
#include "mz/config/fingerprint_config_storage.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"

#include <sstream>

#define MZ_LOG(msg) LOG(INFO) << msg

namespace mz {
namespace fingerprint {

// static
void ScreenHardwareFingerprint::ApplyResolution(const FingerprintConfig& config) {
  if (!config.resolution.has_value()) {
    MZ_LOG("Resolution 配置未设置，跳过");
    return;
  }

  mz::config::SetResolution(config.resolution.value());
  MZ_LOG("Resolution 配置已应用: " << config.resolution.value());
}

// static
void ScreenHardwareFingerprint::ApplyColorDepth(const FingerprintConfig& config) {
  if (!config.color_depth.has_value()) {
    MZ_LOG("ColorDepth 配置未设置，跳过");
    return;
  }

  mz::config::SetColorDepth(config.color_depth.value());
  MZ_LOG("ColorDepth 配置已应用: " << config.color_depth.value());
}

// static
void ScreenHardwareFingerprint::ApplyDevicePixelRatio(const FingerprintConfig& config) {
  if (!config.device_pixel_ratio.has_value()) {
    MZ_LOG("DevicePixelRatio 配置未设置，跳过");
    return;
  }

  mz::config::SetDevicePixelRatio(config.device_pixel_ratio.value());
  MZ_LOG("DevicePixelRatio 配置已应用: " << config.device_pixel_ratio.value());
}

// static
void ScreenHardwareFingerprint::ApplyMaxTouchPoints(const FingerprintConfig& config) {
  if (!config.max_touch_points.has_value()) {
    MZ_LOG("MaxTouchPoints 配置未设置，跳过");
    return;
  }

  mz::config::SetMaxTouchPoints(config.max_touch_points.value());
  MZ_LOG("MaxTouchPoints 配置已应用: " << config.max_touch_points.value());
}

// static
void ScreenHardwareFingerprint::ApplyHardwareConcurrency(const FingerprintConfig& config) {
  if (!config.hardware_concurrency.has_value()) {
    MZ_LOG("HardwareConcurrency 配置未设置，跳过");
    return;
  }

  mz::config::SetHardwareConcurrency(config.hardware_concurrency.value());
  MZ_LOG("HardwareConcurrency 配置已应用: " << config.hardware_concurrency.value());
}

// static
void ScreenHardwareFingerprint::ApplyDeviceMemory(const FingerprintConfig& config) {
  if (!config.device_memory.has_value()) {
    MZ_LOG("DeviceMemory 配置未设置，跳过");
    return;
  }

  mz::config::SetDeviceMemory(config.device_memory.value());
  MZ_LOG("DeviceMemory 配置已应用: " << config.device_memory.value());
}

// static
std::string ScreenHardwareFingerprint::GetConfiguredResolution() {
  return mz::config::GetResolution();
}

// static
int ScreenHardwareFingerprint::GetConfiguredScreenWidth() {
  int width, height;
  mz::config::GetResolutionWidthHeight(&width, &height);
  return width;
}

// static
int ScreenHardwareFingerprint::GetConfiguredScreenHeight() {
  int width, height;
  mz::config::GetResolutionWidthHeight(&width, &height);
  return height;
}

// static
int ScreenHardwareFingerprint::GetConfiguredColorDepth() {
  return mz::config::GetColorDepth();
}

// static
double ScreenHardwareFingerprint::GetConfiguredDevicePixelRatio() {
  return mz::config::GetDevicePixelRatio();
}

// static
int ScreenHardwareFingerprint::GetConfiguredMaxTouchPoints() {
  return mz::config::GetMaxTouchPoints();
}

// static
int ScreenHardwareFingerprint::GetConfiguredHardwareConcurrency() {
  return mz::config::GetHardwareConcurrency();
}

// static
double ScreenHardwareFingerprint::GetConfiguredDeviceMemory() {
  return mz::config::GetClampedDeviceMemory();
}

}  // namespace fingerprint
}  // namespace mz
