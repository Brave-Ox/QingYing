# 标注调色板 Implementation Plan

> **For agentic workers:** 本会话按 executing-plans 在当前仓库直接实现。用户要求**不 git commit**。

**Goal:** 属性栏增加当前色入口，弹出 PixPin 式「选择颜色」窗（HEX/RGB/HSV、透明度、吸管、标题栏可拖），所有改颜色的工具共用这一套。

**Architecture:** 纯换算与 `ColorPickerState` 放公开头、可单测；Win32 弹层单独编译单元；chrome 只做当前色块接线；renderer 对 `a<255` 做 src-over。

**Tech Stack:** C++17、Win32（非 layered `WS_POPUP` + 子控件）、GoogleTest、现有 `GdiObject` RAII。

**Spec:** `docs/superpowers/specs/2026-09-02-color-picker-design.md`

## Global Constraints

- 公开 `AnnotationOverlay` API 冻结；不引入 Qt / `ChooseColor`。
- 命名：类大驼峰、函数小驼峰、成员 `m_`；Allman；禁止裸 `new`/`delete` 与不成对 `DeleteObject`。
- 源码 UI 字符串与现有标注模块一致（中文宽字符），不引入 `tr()`。
- 透明度以 `ColorBgra.a` 为准；`AnnotationStyle.opacity` 不用。
- 本轮任务全部**跳过 git commit**。

## File map

| 文件 | 职责 |
|------|------|
| Create: `include/qingying/annotate/color_convert.hpp` | HSV/RGB/Hex/alpha 解析、src-over |
| Create: `include/qingying/annotate/color_picker_state.hpp` | 草稿/格式/色相保持、画布取样 |
| Create: `src/annotate/annotation_color_picker.hpp/.cpp` | 调色窗口、拖拽、子控件、吸管 |
| Create: `tests/color_convert_test.cpp` | 换算与混合 |
| Create: `tests/color_picker_state_test.cpp` | 状态机 |
| Modify: `annotation_editor_layout.hpp` | 当前色块宽度、弹层尺寸、放置/命中 |
| Modify: `annotation_renderer.cpp` | `setPixel` src-over |
| Modify: `annotation_editor_chrome.cpp` / `host` | 当前色块、打开关闭、销毁 |
| Modify: `CMakeLists.txt` / `tests/CMakeLists.txt` | 加源与测试 |

---

### Task 1: color_convert

**Produces:** `hsvToRgb`, `rgbToHsv`, `parseHex`, `formatHex`, `parseChannel255`, `parsePercent`, `parseHue`, `blendSrcOver`, `packColorBgra`, `unpackColorBgra`, `colorsMatchRgb`

- [ ] 写失败测试 `tests/color_convert_test.cpp`
- [ ] 实现 `include/qingying/annotate/color_convert.hpp`
- [ ] 跑通 `ColorConvert*`（本轮不 commit）

### Task 2: ColorPickerState + 布局

**Produces:** `ColorPickerState`；`annotationEditorColorSwatchesWidth`；`annotationEditorPlaceColorPicker`；`annotationEditorHitColorPicker`

- [ ] 写 `tests/color_picker_state_test.cpp` 与 overlay 布局断言
- [ ] 实现 state 与 layout 常量
- [ ] 跑通对应测试

### Task 3: renderer src-over

- [ ] 半透明矩形叠白底测试
- [ ] 改 `setPixel`；旧 `a==255` 测试保持绿

### Task 4: Win32 调色窗 + chrome

- [ ] `annotation_color_picker.cpp`：标题拖拽、SV/色相/透明度、格式行、确认取消、画布吸管
- [ ] 属性栏当前色块；快捷色先关弹层；马赛克关弹层；overlay 销毁时销毁弹层
- [ ] 编译 `qingying` + 跑标注相关测试
