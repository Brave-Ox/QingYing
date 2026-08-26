# 轻映 QingYing — 开发进度（PROGRESS）

> 本文记录**当前开发进度快照**：已完成且可验证 / 进行中 / 待办 / 下一步建议。
> 功能规划与验收指标见 [轻映-QingYing-开发清单.md](../轻映-QingYing-开发清单.md)（本文与之互补，不重复规划）。
> 更新日期：2026-08-25

---

## 1. 当前状态总览

| 阶段 | 状态 | 说明 |
|---|---|---|
| 工程骨架（CMake / MSVC / C++17 / 10 个 static lib / 单 EXE） | ✅ 完成 | Release 单 EXE 可构建 |
| P0 主路径「截一下」 | 🔶 基本闭环 | 热键 → 框选 → 截图 → 复制 / 保存 PNG |
| P1 标注（第二人 Checkpoint B） | ✅ 模块可交付 | 矩形/椭圆/箭头/画笔/马赛克/文字 + 撤销 + Overlay |
| 单元测试 | ✅ 全绿 | annotate 相关套件已接入；以本地 `qingying_tests` 为准 |

**一句话**：截图主链路已通；标注模块已到 **Checkpoint B**（第三人可只依赖 `annotation_overlay.hpp` 接线）。椭圆、文字、画笔式马赛克已实现。

---

## 2. 已完成（含验证方式）

### 2.1 截图与剪贴板主链路

| 能力 | 文件 | 验证方式 |
|---|---|---|
| 全屏半透明遮罩 | `src/overlay/selection_overlay.cpp` | 手动：Ctrl+Shift+Q |
| 鼠标拖拽框选 | `src/overlay/selection_controller.cpp` | `selection_controller_test` |
| 遮罩像素渲染 | `src/overlay/mask_renderer.cpp` | `mask_renderer_test` |
| 区域截图 | `src/capture/capture_engine.cpp` | `capture_engine_test`；手动闭环 |
| 剪贴板 CF_DIB / PNG 保存 | `src/export/` | 单测 + 手动粘贴 |

### 2.2 标注模块（第二人，Checkpoint B）

| 能力 | 文件 | 验证方式 |
|---|---|---|
| 文档模型 + 撤销/重做 | `annotation_document.*` | `annotation_document_test` |
| 矩形/箭头/画笔栅格化 | `annotation_renderer.*` | `annotation_renderer_test` |
| 引擎组装 | `annotation_engine.*` | `annotation_engine_test` |
| 纯逻辑会话（确认/取消） | `annotation_editor_session.*` | `annotation_editor_session_test` |
| 拖拽交互（工具/预览/入栈） | `annotation_interaction_controller.*` | `annotation_interaction_controller_test` |
| 编辑器窗口（完成/取消） | `annotation_overlay.*` | 自动分支 + `DISABLED_` 手工冒烟；框选后「编辑」为选区上就地无边框标注 |
| 标注底栏 | `annotation_overlay` + `modern_toolbar` | 同视觉：图标工具 + 字号 + 撤销/完成/取消 |
| 椭圆描边 | `annotation_renderer` + Overlay「椭圆」 | 轮廓像素 / 不填充 / 交互归一化 |
| 文字标注 | `annotation_renderer` + Overlay「文字」 | 就地 EDIT；字号；单击选中+Delete；双击改字；拖拽改位 |
| 画笔式马赛克 | `annotation_renderer` + Overlay「马赛克」 | 折线笔刷打码；块均值；撤销整笔 |

**第三人接线**：`#include "qingying/annotate/annotation_overlay.hpp"` → `show(owner, image, callback)`。

### 2.3 工程与基础

- 10 个 static lib + 单 EXE；ActionDispatcher 为业务入口
- 托盘 / 热键 / 单实例 / 自启动；架构见 `docs/architecture.md`

---

## 3. 进行中 / 待办

### 3.1 代码待办

- [ ] **提交标注相关改动**（若尚未 commit）
- [ ] **PIMPL 重构 capture_engine.cpp**（裸 new/delete → `unique_ptr`）

### 3.2 功能待办

- [x] **P1 标注初版（第二人）**：矩形 + 椭圆 + 箭头 + 画笔 + 撤销 + Overlay —— Checkpoint B
- [x] **P1**：Edit → AnnotationOverlay → 回写 Session → 复制（最小接线）
- [x] **P1 标注文字**：图上就地输入 + 字号下拉；单击选中 / 双击编辑 / Delete 删除；拖拽改位
- [x] **P1 画笔式马赛克**：折线笔刷 + 固定块大小；工具条按钮
- [x] **就地编辑区对齐选区**：禁止夹屏平移图片；编辑期间隐藏遮罩
- [ ] **P1 标注后续**：颜色/线宽面板；重做 UI；马赛克块大小调节
- [ ] **P1**：钉图接线；标注完成后回到完整操作条（Copy/Save/Pin）
- [ ] **P1**：八点调区 / DPI / 多屏冒烟
- [ ] **P2**：钉图；长截图
- [ ] **P3**：本地口令；MCP Bridge

---

## 4. 质量与验证基线

- **单元测试**：`build\bin\Release\qingying_tests.exe` 全绿；1 条 `DISABLED_` Overlay 冒烟
- **构建**：`build.bat Release` / `build.bat test`

---

## 5. 下一步建议

1. **钉图接线** 或 **颜色/线宽面板** / 重做 UI
2. **DPI / 多屏冒烟**
3. **标注完成后回到完整操作条**

---

*进度文件由开发过程持续维护；与开发清单、架构文档联动更新。*
