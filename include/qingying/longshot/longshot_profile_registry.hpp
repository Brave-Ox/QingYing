#pragma once

#include "qingying/longshot/longshot_profile.hpp"

#include <memory>
#include <vector>

namespace qingying {

// 持有已注册的应用 profile，并解析第一个接受已记录 owner window 和选区的 profile。
// 具体注册哪些 profile 由应用组合根决定，Registry 本身不依赖任何具体应用。
class LongShotProfileRegistry {
 public:
  LongShotProfileRegistry() = default;
  explicit LongShotProfileRegistry(
      std::vector<std::unique_ptr<LongShotProfile>> profiles);

  LongShotProfileRegistry(const LongShotProfileRegistry&) = delete;
  LongShotProfileRegistry& operator=(const LongShotProfileRegistry&) = delete;
  LongShotProfileRegistry(LongShotProfileRegistry&&) noexcept = default;
  LongShotProfileRegistry& operator=(LongShotProfileRegistry&&) noexcept =
      default;

  // Registry 接管 profile 的所有权。传入 nullptr 时忽略。
  void add(std::unique_ptr<LongShotProfile> profile);

  const LongShotProfile* resolve(const LongShotRequest& request,
                                 LongShotProfileResult& out) const;

 private:
  std::vector<std::unique_ptr<LongShotProfile>> profiles_;
};

}  // qingying 命名空间
