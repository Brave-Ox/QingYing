#pragma once

#include "qingying/action/image.hpp"
#include "qingying/action/types.hpp"
#include "qingying/longshot/longshot_profile.hpp"
#include "qingying/longshot/longshot_profile_registry.hpp"

#include <cstdint>
#include <functional>
#include <memory>

namespace qingying {

class CaptureEngine;

// 一次长截图的安全限制。初始帧对已经占用两帧，因此 max_frames 至少为 2。
struct LongShotLimits {
  int max_frames{30};
  int max_output_height{30000};

  bool valid() const {
    return max_frames >= 2 && max_output_height > 0;
  }
};

// 第 4 步的原始结果：对完全相同的屏幕矩形，在一次滚动输入前后各捕获一帧。
// 该结果还不是最终拼接出的长图。
struct LongShotFramePair {
  Image first_frame;
  Image second_frame;

  bool valid() const;
  void clear();
};

using LongShotProgressCallback = std::function<void(const Image&)>;
using LongShotContinueCallback = std::function<bool()>;

class LongShotEngine {
 public:
  explicit LongShotEngine(CaptureEngine& capture, LongShotLimits limits = {});
  LongShotEngine(CaptureEngine& capture, LongShotProfileRegistry profiles,
                 LongShotLimits limits = {});
  ~LongShotEngine();

  LongShotEngine(const LongShotEngine&) = delete;
  LongShotEngine& operator=(const LongShotEngine&) = delete;

  // 通过解析出的应用 profile 滚动 request.owner_window，并反复捕获
  // request.{x,y,width,height} 指定的固定区域。遇到整帧重叠、滚动位置稳定
  // 或达到安全限制时停止。
  ActionResult captureSelection(const LongShotRequest& request, Image& out);

  // 交互版本：每捕获一帧就报告当前累计图像；should_continue 返回 false
  // 时正常停止。
  ActionResult captureSelection(const LongShotRequest& request, Image& out,
                                LongShotProgressCallback on_progress,
                                LongShotContinueCallback should_continue);

  // 围绕一次滚轮输入准确捕获两帧原始图像。保留这个分阶段接口，便于独立
  // 验证第一次滚动，而不依赖最终循环。
  ActionResult captureInitialPair(const LongShotRequest& request,
                                  LongShotFramePair& out);

  // 外部来源的长截图结果由 longshot 接收；应用只转发通用完成消息。
  bool startExternalResultReceiver(std::uintptr_t notification_window);
  void stopExternalResultReceiver() noexcept;
  bool takeExternalResult(Image& image);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // qingying 命名空间
