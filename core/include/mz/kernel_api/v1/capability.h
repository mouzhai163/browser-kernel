#ifndef MZ_KERNEL_API_V1_CAPABILITY_H_
#define MZ_KERNEL_API_V1_CAPABILITY_H_

#include <cstdint>

namespace mz::kernel_api::v1 {

enum class Capability : std::uint32_t {
  kRendererConfigSnapshot = 1,
  kRendererConfigUpdate = 2,
  kNetworkConfigSnapshot = 3,
  kRuntimeProxyUpdate = 4,
};

class CapabilityProvider {
 public:
  virtual ~CapabilityProvider() = default;
  virtual bool Supports(Capability capability) const = 0;
};

}  // namespace mz::kernel_api::v1

#endif  // MZ_KERNEL_API_V1_CAPABILITY_H_
