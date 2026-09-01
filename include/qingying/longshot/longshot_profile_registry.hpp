#pragma once

#include "qingying/longshot/longshot_profile.hpp"

#include <memory>
#include <vector>

namespace qingying {

// 持有受支持的应用 profile，并解析第一个接受已记录 owner window 和选区的 profile。
class LongShotProfileRegistry {
 public:
  LongShotProfileRegistry();
  explicit LongShotProfileRegistry(
      std::vector<std::unique_ptr<LongShotProfile>> profiles);

  LongShotProfileRegistry(const LongShotProfileRegistry&) = delete;
  LongShotProfileRegistry& operator=(const LongShotProfileRegistry&) = delete;
  LongShotProfileRegistry(LongShotProfileRegistry&&) noexcept = default;
  LongShotProfileRegistry& operator=(LongShotProfileRegistry&&) noexcept =
      default;

  const LongShotProfile* resolve(const LongShotRequest& request,
                                 LongShotProfileResult& out) const;

 private:
  std::vector<std::unique_ptr<LongShotProfile>> profiles_;
};

}  // qingying 命名空间
