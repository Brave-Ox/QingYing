#pragma once

#include <Windows.h>

#include "qingying/action/image.hpp"
#include "qingying/annotate/annotation_result.hpp"

namespace qingying {

// 标注编辑器窗口。窗口采用非模态语义：show 创建窗口后立即返回，
// 用户确认或取消时再通过回调交付结果，由应用自己的消息循环持续驱动。
//
// 边界：只建窗口、只处理本窗口消息。不调用 ActionDispatcher /
// ExportService / CaptureEngine / PinManager，也不写剪贴板或文件；
// 复制、保存、钉图仍由第三人在拿到结果后经 Dispatcher 执行。
class AnnotationOverlay
{
 public:
  AnnotationOverlay() = default;
  ~AnnotationOverlay();

  AnnotationOverlay(const AnnotationOverlay&) = delete;
  AnnotationOverlay& operator=(const AnnotationOverlay&) = delete;

  // owner 仅用于窗口归属，允许为 nullptr。source 会被拷贝一份，
  // 调用方后续改动原图不影响编辑结果。
  //
  // 返回 false 且**不触发回调**：source 为空、已有编辑器在显示、
  // 或窗口类注册/创建失败。
  bool show(HWND owner, const Image& source, AnnotationCallback callback);

  // 就地编辑弹层：图片原点钉在屏幕 (screen_x, screen_y)，外侧绘制选区框。
  // screen_x < 0 时居中（测试/兜底）。
  bool showInPlace(HWND owner, const Image& source, int screen_x, int screen_y,
                    AnnotationCallback callback);

  // 请求关闭当前编辑器，等效于用户点取消，并交付一次取消回调。
  void hide();

  // 应用退出或工作流中止时静默销毁窗口，不交付取消回调。
  void closeSilently();

  bool isVisible() const;

 private:
  HWND m_hwnd{nullptr};
  bool m_visible{false};
  bool m_suppress_callback{false};
};

}  // namespace qingying
