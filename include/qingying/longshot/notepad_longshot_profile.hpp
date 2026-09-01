#pragma once

#include "qingying/longshot/longshot_profile.hpp"

namespace qingying {

class NotepadLongShotProfile final : public LongShotProfile {
 public:
  const char* name() const noexcept override { return "notepad"; }

  bool resolve(const LongShotRequest& request,
               LongShotProfileResult& out) const override;
  bool scrollDown(const LongShotRequest& request,
                  const LongShotProfileResult& profile) const override;
  bool queryScrollState(const LongShotProfileResult& profile,
                        LongShotScrollState& out) const override;
};

// 只解析传入的顶层窗口。该函数不会查询前台窗口，也不会替调用方选择捕获矩形。
bool resolveNotepadProfile(std::uintptr_t owner_window,
                           LongShotProfileResult& out);

// 读取已解析目标的垂直滚动条。目标没有提供可查询的滚动范围时返回 false；
// 此时调用方必须使用其他停止条件，不能将其误判为已经到底。
bool queryNotepadScrollAtBottom(const LongShotProfileResult& profile,
                                bool& at_bottom);

}  // qingying 命名空间
