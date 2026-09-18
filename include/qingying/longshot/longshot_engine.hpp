#pragma once

#include "qingying/action/image.hpp"
#include "qingying/action/action_result.hpp"
#include "qingying/longshot/longshot_profile.hpp"
#include "qingying/longshot/longshot_profile_registry.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

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
using LongShotCaptureCallback =
    std::function<ActionResult(const ScreenPhysicalRect&, Image&)>;

// Stable diagnostic stages for the synchronous long-shot engine. The value is
// exposed as a string through ActionResult so the generic action layer does
// not depend on long-shot headers or plugin ABI details.
enum class LongShotFailureStage : std::uint8_t {
  None,
  RequestValidation,
  SafetyLimit,
  ProfileResolution,
  ScrollInput,
  ScrollSettle,
  InitialCapture,
  FrameCapture,
  FrameValidation,
  OverlapDetection,
  Stitching,
};

const char* longShotFailureStageName(LongShotFailureStage stage) noexcept;

// 图像质量和停止原因相互独立：失败也可能留下可靠图像。
enum class LongShotResultQuality : std::uint8_t {
  None,
  SingleFrame,
  VerifiedComposite,
};

enum class LongShotStopReason : std::uint8_t {
  NotStarted,
  RequestRejected,
  ReachedBottom,
  NoProgress,
  UserStopped,
  LimitReached,
  MatchFailed,
  InputUnavailable,
  TargetInvalid,
  CaptureFailed,
  StitchFailed,
  Cancelled,
};

// 内部 C++ 完成载荷，不属于 profile DLL 的 C ABI。image 只能包含已接受的
// 帧；accepted_frames 不计重采或拒绝的帧。尺寸直接取 image，避免重复元数据。
// C02 将捕获循环接入此模型，旧 ActionResult + Image 入口继续兼容。
struct LongShotOutcome {
  Image image;
  int accepted_frames{0};
  LongShotStopReason stop_reason{LongShotStopReason::NotStarted};
  std::string strategy;
  LongShotFailureStage failure_stage{LongShotFailureStage::None};
  ActionResult diagnostic;

  // 根据有效像素载荷和接受帧数推导质量，不根据 diagnostic.ok 推断。
  LongShotResultQuality quality() const noexcept;
  // 取消、未启动和请求被拒绝都不能交付结果，即使仍持有内部图像。
  bool hasExportableResult() const noexcept;
  // 只有确定到底才称为完整；无进展、主动停止和限额结束都不证明完整。
  bool isComplete() const noexcept;
  // 只有首帧时仍称为普通截图，不称为部分长图。
  bool isPartial() const noexcept;
};

class LongShotEngine {
 public:
  explicit LongShotEngine(CaptureEngine& capture, LongShotLimits limits = {});
  LongShotEngine(CaptureEngine& capture, LongShotProfileRegistry profiles,
                 LongShotLimits limits = {});
  LongShotEngine(LongShotCaptureCallback capture,
                 LongShotProfileRegistry profiles, LongShotLimits limits = {});
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

  // Uses the supplied limits for exactly this capture. The engine does not
  // retain the reference after this synchronous call returns.
  ActionResult captureSelection(const LongShotRequest& request, Image& out,
                                LongShotProgressCallback on_progress,
                                LongShotContinueCallback should_continue,
                                const LongShotLimits& limits);

  // Requests cooperative cancellation of the active profile callback. Old
  // profiles may ignore it; LongShotController still enforces its join
  // deadline and keeps the owner context alive if the callback is stuck.
  void cancel() noexcept;
  std::string activeProfileName() const;

  // 围绕一次滚轮输入准确捕获两帧原始图像。保留这个分阶段接口，便于独立
  // 验证第一次滚动，而不依赖最终循环。
  ActionResult captureInitialPair(const LongShotRequest& request,
                                  LongShotFramePair& out);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // qingying 命名空间
