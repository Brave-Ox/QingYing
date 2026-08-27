#pragma once

#include "qingying/annotate/annotation_engine.hpp"
#include "qingying/annotate/annotation_types.hpp"

namespace qingying {

// 标注编辑器的拖拽交互状态机：无 Win32 依赖，可单测。
// Overlay 只负责把客户区坐标与快捷键翻译成对本类的调用。
//
// 生命周期：setTool → beginStroke → updateStroke* → endStroke | cancelStroke
// 拖拽期间只维护预览对象，不写入引擎；松开才尝试 add。
class AnnotationInteractionController
{
 public:
  // 画布尺寸（Image 物理像素）。落在画布外的 beginStroke 会被拒绝。
  void setCanvasSize(int width, int height);

  void setTool(AnnotationTool tool);
  AnnotationTool tool() const;

  void setColor(const ColorBgra& color);
  void setStrokeWidth(float width);
  void setFontSize(int font_size);
  void setFilled(bool filled);
  void setLineStyle(AnnotationLineStyle line_style);
  void setMosaicBlockSize(int block_size);
  int mosaicBlockSize() const;
  const AnnotationStyle& style() const;

  // 开始一笔。工具为 None、或点在画布外时返回 false。
  bool beginStroke(float x, float y);

  // 拖动中更新预览；未在绘制中时为空操作。
  void updateStroke(float x, float y);

  // 松手：若几何合法则 add 到引擎并返回 true；否则仅清除预览并返回 false。
  bool endStroke(AnnotationEngine& engine);

  // 放弃当前拖拽预览，不入栈。
  void cancelStroke();

  bool isDrawing() const;
  bool hasPreview() const;
  const Annotation& preview() const;

  // 撤销最近一次已提交标注；拖拽中时先取消预览再撤销。
  bool undo(AnnotationEngine& engine);

 private:
  void rebuildPreview(float x, float y);
  void refreshPreviewStyle();
  bool isInsideCanvas(float x, float y) const;

  AnnotationTool m_tool{AnnotationTool::None};
  AnnotationStyle m_style{};
  int m_mosaic_block_size{DefaultMosaicBlockSize};
  int m_canvas_width{0};
  int m_canvas_height{0};
  bool m_drawing{false};
  float m_start_x{0.0f};
  float m_start_y{0.0f};
  float m_last_x{0.0f};
  float m_last_y{0.0f};
  Annotation m_preview{};
  bool m_has_preview{false};
};

}  // namespace qingying
