#pragma once

#include "qingying/longshot/longshot_profile.hpp"

namespace qingying {

class ExplorerLongShotProfile final : public LongShotProfile {
 public:
  const char* name() const noexcept override { return "explorer"; }

  bool resolve(const LongShotRequest& request,
               LongShotProfileResult& out) const override;
  bool scrollDown(const LongShotRequest& request,
                  const LongShotProfileResult& profile) const override;
  bool queryScrollState(const LongShotProfileResult& profile,
                        LongShotScrollState& out) const override;
};

// 解析文件资源管理器中选中的内容窗格。使用请求的屏幕矩形在导航窗格和
// 内容窗格之间进行选择。
bool resolveExplorerProfile(const LongShotRequest& request,
                            LongShotProfileResult& out);

}  // qingying 命名空间
