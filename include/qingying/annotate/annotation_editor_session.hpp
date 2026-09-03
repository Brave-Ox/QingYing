#pragma once

#include "qingying/action/image.hpp"
#include "qingying/annotate/annotation_engine.hpp"
#include "qingying/annotate/annotation_result.hpp"

namespace qingying {

// 标注编辑器的纯逻辑内核：不含任何 Win32 依赖，可脱离窗口单测。
// AnnotationOverlay 只负责建窗口和处理消息，把用户操作翻译成对本类的调用。
class AnnotationEditorSession
{
 public:
  // 开始一轮编辑：拷贝源图，并清空上一轮遗留的标注与撤销栈。
  // 源图为空、或已有一轮编辑在进行中时返回 false。
  bool begin(const Image& source);

  // 用户确认：产出「源图 + 标注」的合成图并结束本轮。
  // 未在编辑中时返回 false 且不改写 result。
  bool finishConfirmed(AnnotationFinishResult& result);

  // 用户取消：产出 cancelled 结果并结束本轮。
  bool finishCancelled(AnnotationFinishResult& result);

  // 释放本轮编辑持有的源图与标注数据，并回到初始状态。
  void reset() noexcept;

  bool isActive() const;

  // 会话自己持有的源图副本，调用方后续改动原图不会影响它。
  const Image& source() const;

  AnnotationEngine& engine();
  const AnnotationEngine& engine() const;

 private:
  AnnotationEngine m_engine;
  Image m_source;
  bool m_active{false};
};

}  // namespace qingying
