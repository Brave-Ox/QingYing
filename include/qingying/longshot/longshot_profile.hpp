#pragma once

#include "qingying/geometry/rect_types.h"

#include <cstdint>

namespace qingying {

// 用户选中的屏幕矩形。owner window 在选区遮罩接管桌面之前记录。
struct LongShotRequest : ScreenPhysicalRect {
  std::uintptr_t owner_window{0};

  constexpr LongShotRequest() = default;

  // Compatibility constructor for existing plugin and application callers.
  // New code should pass a ScreenPhysicalRect explicitly.
  constexpr LongShotRequest(std::uintptr_t owner_window_in, int x_in, int y_in,
                            int width_in, int height_in)
      : ScreenPhysicalRect{x_in, y_in, width_in, height_in},
        owner_window(owner_window_in) {}

  constexpr LongShotRequest(std::uintptr_t owner_window_in,
                            ScreenPhysicalRect selection)
      : ScreenPhysicalRect(selection), owner_window(owner_window_in) {}

  bool valid() const {
    return owner_window != 0 && ScreenPhysicalRect::valid();
  }

  constexpr ScreenPhysicalRect selectionRect() const noexcept {
    return ScreenPhysicalRect{x, y, width, height};
  }
};

// profile 负责解析实际可滚动控件及其可见内容边界。捕获时仍以请求中的
// 屏幕矩形为准；内容边界只用于判断当前 profile 是否能够处理该选区。
struct LongShotProfileResult {
  std::uintptr_t scroll_target{0};
  ScreenPhysicalRect content{};

  constexpr LongShotProfileResult() = default;

  // Compatibility constructor for profile implementations that still return
  // four raw coordinates while migrating to the named rectangle.
  constexpr LongShotProfileResult(std::uintptr_t scroll_target_in, int x_in,
                                  int y_in, int width_in, int height_in)
      : scroll_target(scroll_target_in),
        content{x_in, y_in, width_in, height_in} {}

  constexpr LongShotProfileResult(std::uintptr_t scroll_target_in,
                                  ScreenPhysicalRect content_in)
      : scroll_target(scroll_target_in), content(content_in) {}

  bool valid() const;
  bool containsSelection(const ScreenPhysicalRect& selection) const;
  bool containsSelection(int x, int y, int width, int height) const;
};

// 某些 profile 可能无法查询滚动状态（例如自绘控件）。此时 valid 为 false，
// LongShotEngine 使用图像重叠作为安全的兜底停止条件。
struct LongShotScrollState {
  std::int64_t position{0};
  std::int64_t last_position{0};
  bool valid{false};

  bool atBottom() const noexcept {
    return valid && position >= last_position;
  }
};

// 通用长截图循环所需的应用专属行为。
// profile 只能操作 request.owner_window 及其子孙窗口，不能向无关窗口广播输入。
class LongShotProfile {
 public:
  virtual ~LongShotProfile() = default;

  virtual const char* name() const noexcept = 0;

  // 为当前屏幕选区解析滚动目标。返回 true 也表示 profile 接受该选区的几何范围；
  // profile 可以允许列表表头或滚动条等少量不可滚动边缘，但仍应拒绝跨越无关窗格的选区。
  virtual bool resolve(const LongShotRequest& request,
                       LongShotProfileResult& out) const = 0;

  // 向已解析的目标发送一次向下滚动输入。
  virtual bool scrollDown(const LongShotRequest& request,
                          const LongShotProfileResult& profile) const = 0;

  // 在应用提供垂直滚动状态时读取目标状态。返回 false 表示引擎必须使用
  // 基于图像的兜底判断。
  virtual bool queryScrollState(const LongShotProfileResult& profile,
                                LongShotScrollState& out) const = 0;
};

}  // qingying 命名空间
