# 轻映 QingYing — 标注调色板（PixPin 布局）

> 状态：待确认（2026-09-02）
> 形态：**属性栏增加当前色入口 + 自绘「选择颜色」弹层；所有改颜色的 UI 共用这一套**
> 依据：现有二级栏 8 色快捷色、`ColorBgra` / `AnnotationInteractionController::setColor`、描边 popup 的 owned 窗口模式、用户提供的 PixPin 参考图
> 前置约束：仓库提交由团队成员手动执行；本设计落地前先评审本文，再写实施计划

关联文档：[AnnotationOverlay 拆分](./2026-08-31-annotation-overlay-split-design.md)、[模块协作设计](./2026-08-24-module-collaboration-design.md)

---

## 1. 背景与目标

### 1.1 现状

标注颜色只有属性栏上的 8 个固定色块。点击后立刻：

```text
controller.setColor(AnnotationStylePresetColors[i])
→ applyLiveTextStyle（若正在编文字）
→ 绘制中则刷新预览
```

会显示颜色的工具：矩形、椭圆、箭头、画笔、文字（`annotationEditorPropertyBarShowsColor`）。马赛克没有颜色。填充是开/关，填充色就是同一支 `style.color`，没有第二套取色。

`ColorBgra` 已有 `a`，渲染 `packBgra` 会带上 alpha，但 `setPixel` 直接覆盖，半透明看不出来。`AnnotationStyle.opacity` 没有调用方。

没有自定义色、没有 HEX/RGB/HSV 输入、没有吸管、没有独立调色窗口。

### 1.2 目标

1. 参考 PixPin「选择颜色」：SV 面板、色相条、透明度条、吸管、Hex / RGB / HSV 切换、确认 / 取消。
2. **所有会改标注颜色的入口都走这一套**，禁止再做一个只含固定色的平行面板。
3. 8 个快捷色保留，一键设置；自定义色通过「当前色块」打开调色板。
4. 调色窗口可用鼠标拖拽在虚拟屏内自由移动。
5. `AnnotationOverlay` 公开 API 不变。

### 1.3 非目标

- 不引入 Qt、Win32 `ChooseColor`、系统色板。
- 不拆描边色 / 填充色为两支颜色。
- 不给马赛克加颜色。
- 不做最近使用色、用户自定义色盘持久化、注册表记住格式。
- 不做全桌面吸管（编辑器窗外的桌面像素）。v1 只从已截画布取样。
- 不把调色板做成 `annotation_overlay.hpp` 的公开类型。
- 不改选区 Overlay、托盘、导出、钉图。

### 1.4 已确认决策

| 决策点 | 结论 |
|--------|------|
| 实现形态 | 自绘 owned popup，PixPin 布局 |
| 快捷色 | 保留 8 个，立刻生效 |
| 打开入口 | 属性栏「当前色块」；所有 `ShowsColor` 的工具都有 |
| 提交时机 | 确认才 `setColor`；取消 / Esc / 点弹层外丢弃草稿 |
| 颜色存储 | 继续只用 `ColorBgra`；透明度以 `color.a` 为准 |
| `AnnotationStyle.opacity` | 本期不使用、不同步 |
| 窗口移动 | 标题栏拖拽，虚拟屏内钳制，子控件跟随 |
| 吸管 | v1 只采样画布像素，RGB 来自像素，`a` 置 255 |
| 半透明 | 调色板写 `color.a`；渲染 `a<255` 做 src-over |
| 公开头 | `annotation_overlay.hpp` 冻结 |

---

## 2. 交互

### 2.1 属性栏

所有 `annotationEditorPropertyBarShowsColor(tool)==true` 的工具，二级栏颜色区变为：

```text
[当前色块] [快捷色×8] [原有描边芯片 / 字号…]
```

- **当前色块**：显示 `controller.style().color`（不含透明度棋盘也可，v1 填纯色即可）。若当前色与 8 个预设都不相等（RGB 全等比较，忽略 `a` 是否与预设相同），画彩虹描边，表示自定义色。点击打开调色板。
- **快捷色**：行为与现在相同，立刻 `setColor`（`a=255`）。若调色板开着，先按取消关闭再设色。
- 马赛克：仍不出现颜色区。

属性栏宽度按「当前色块 + 间距 + 原 8 色宽度」重算，现有 `annotationEditorPropertyBarWidth` 必须改，否则栏会被裁。

### 2.2 调色窗口内容（自上而下）

1. **标题栏**：左侧标题与现有标注 UI 一样用中文宽字符（「选择颜色」），右侧关闭按钮。关闭 = 取消。本期不引入 Qt `tr()`。
2. **SV 面板**：当前色相下的饱和度（横）× 明度（纵）；圆形游标。
3. **吸管**（左）+ **色相条**、**透明度条**（右，透明度条棋盘底）。
4. **格式行**：
   - 下拉：`Hex` / `RGB` / `HSV`
   - Hex：一个 `#RRGGBB`（也接受 `#RGB`），右侧透明度 `100%`
   - RGB：R、G、B 三个 0–255，右侧透明度
   - HSV：H 0–360、S 0–100、V 0–100，右侧透明度
5. **确认 / 取消**

弹层内拖动 SV / 滑条 / 改数字只更新**草稿**，不写 `controller`。确认后：

```text
controller.setColor(draft)
→ applyLiveTextStyle
→ 绘制中则 invalidate 画布
→ 刷新属性栏当前色
```

Enter 确认，Esc 取消。打开调色板时关掉描边 popup，二者互斥。

格式选择在本轮标注编辑会话内记住；关掉 overlay 后下次从 Hex 开始。

非法 Hex / 越界数字：失焦或 Enter 前校验失败则回退到该框上一合法值，不改草稿色。

Hex 只表示 RGB，不写 `#AARRGGBB`。透明度只走滑条和 `%` 框。

### 2.3 窗口位置与拖拽

- **首次打开**：锚在当前色块附近（优先色块上方，空间不够则右侧 / 下方），整窗钳制在虚拟屏内。使用与工具栏相同的 `SM_*VIRTUALSCREEN` 边界。
- **可自由移动**：按住**标题栏空白**（不含关闭按钮、不含 SV/滑条/输入/按钮）拖拽。拖的是整个调色窗口，不是工具栏。
- **拖拽时**：子控件（下拉、编辑框、按钮）作为窗口子控件，随父窗口移动，不必单独 `SetWindowPos` 每个 EDIT。
- **钳制**：拖动过程中窗口不得完全离开虚拟屏；至少标题栏可见。复用 `annotationEditorClampRectOrigin` 的语义。
- **与工具栏独立**：拖功能栏不带动调色窗口；调色窗口用屏幕坐标，不跟 chrome offset。
- **记住位置**：本次 overlay 生命周期内，用户拖过之后再打开，用上次屏幕位置（仍钳制）；未拖过则每次按「色块附近」放置。
- **拖拽与「点外面取消」**：拖拽的按下/移动/松开不视为点外面。只有在调色窗口与其子控件之外按下，才取消。
- **不要**用系统 `WS_CAPTION` / `WS_THICKFRAME`：外观跟参考图走，标题栏自绘。拖拽对齐现有功能栏：标题空白处 `WM_LBUTTONDOWN` → `SetCapture`，`WM_MOUSEMOVE` 按屏幕光标增量 `SetWindowPos`，松开 `ReleaseCapture`。禁止点到 SV 面板、滑条、输入框、按钮、关闭时开始拖。

### 2.4 吸管

进入吸管后，光标在**画布客户区**按下：把该点映射到 `Image` 像素，草稿 RGB 取该像素，`a=255`。点在画布外、工具栏、调色窗口上：不取样，保持吸管直到点到画布或按 Esc 退出吸管（Esc 只退出吸管，不关调色板）。

不采样屏幕 DC，避免取到分层窗 color-key、工具栏白色或调色板自己。

---

## 3. 架构

公开 API 不动。新代码按职责拆文件，不把对话框堆进 `annotation_editor_chrome.cpp`。

```text
公开
  include/qingying/annotate/annotation_overlay.hpp    （冻结）
  include/qingying/annotate/annotation_types.hpp      （ColorBgra 不变）
  include/qingying/annotate/color_convert.hpp         （纯换算，无 Windows.h）

内部（src/annotate/）
  annotation_color_picker.hpp/.cpp    窗口、绘制、拖拽、子控件、确认取消
  annotation_editor_chrome.cpp        只：当前色块布局 / hit-test / 打开关闭
  annotation_renderer.cpp            setPixel 增加 src-over
  annotation_editor_layout.hpp       调色板与当前色块的尺寸常量
```

依赖只允许向下：

```text
AnnotationEditorChrome
  └── ColorPickerWindow
        └── ColorPickerState
              └── color_convert.hpp / ColorBgra

AnnotationRenderer
  └── ColorBgra（src-over 用 a）
```

`CMakeLists.txt` 的 `qingying_annotate` 追加 `annotation_color_picker.cpp`（若 `color_convert` 有 `.cpp` 也追加）。不新增 lib。

调色窗口是**普通（非 layered）** `WS_POPUP`，owner 为 overlay。这样 EDIT / ComboBox / Button 可以做真子控件，拖父窗时一起走。描边 popup 因为 layered 才把数字框做成独立 popup，调色板不要复制那套。

行数目标：

| 文件 | 上限 | 说明 |
|------|------|------|
| `color_convert.hpp` | 250 | 换算 + 解析，可 header-only |
| `annotation_color_picker.cpp` | 900 | 窗口过程 + 绘制 + 拖拽 |
| `annotation_editor_chrome.cpp` | 现有 + 约 80 行接线 | 禁止把 SV 绘制写进来 |

---

## 4. 数据与换算

### 4.1 `ColorPickerState`

| 字段 | 含义 |
|------|------|
| `committed` | 打开时的 `controller.style().color`，取消时丢弃草稿用 |
| `draft` | 正在编的 `ColorBgra` |
| `format` | `Hex` / `Rgb` / `Hsv` |
| `hue_degrees` | 0–360，避免纯黑/纯白时 SV 面板丢失色相 |

改 SV / 色相 / RGB / Hex 时同步 `draft` 与 `hue_degrees`。纯黑（V=0）或纯白（S=0）改明度时，色相条保持 `hue_degrees`，不要跳回红。

切换格式只改输入框形态，不改 `draft`。

### 4.2 `color_convert.hpp` 契约

- `hsvToRgb` / `rgbToHsv`：H 用度为单位 `[0, 360)`，S/V 显示用 0–100，内部可用 0–1，对外钳制。
- `formatHex`：始终输出小写 `#rrggbb`（6 位）。
- `parseHex`：`#RGB` / `#RRGGBB`，大小写均可；失败返回 false。
- `parseChannel255` / `parsePercent` / `parseHue`：越界钳制到合法范围；空串或非数字返回 false。
- 禁止在换算里改 alpha，除非调用方显式传入。

全部无 Win32，单测覆盖往返与非法输入。

### 4.3 确认写回

`setColor` 已存在，签名不变。自定义色与快捷色都走它。选中已有文字时仍走 `applyLiveTextStyle`。

比较「是否自定义色」：只比 RGB，不比 `a`。预设色带透明度时仍算命中该预设，当前色块可以不画彩虹边。

---

## 5. 渲染半透明

`setPixel` 在写入前：

- `src.a == 255`：与现在完全相同，直接覆盖（现有单测像素值不变）。
- `src.a == 0`：不写该像素。
- 否则对 RGB 做 src-over，画布当作不透明底：

```text
out.c = (src.c * src.a + dst.c * (255 - src.a) + 127) / 255
out.a = 255
```

马赛克、未带标注色的路径不走这套。`AnnotationStyle.opacity` 本期仍忽略。

文字就地编辑用 GDI `SetTextColor`，Win32 不吃 `ColorBgra.a`。v1 接受：提交后的栅格化文字走 renderer 混合；编辑框预览仍是不透明字形。不在本期给 EDIT 做分层透明字。

---

## 6. 错误与生命周期

- 创建调色窗口失败：不改颜色，属性栏保持可点快捷色；不崩溃。
- Overlay `hide` / `closeSilently` / `WM_NCDESTROY`：销毁调色窗口，丢草稿。
- 切换工具到马赛克：关闭调色板（取消）。
- 确认过程中 overlay 已销毁：与现有 `m_destroyed_during_create` 同类，确认回调前检查 Host 仍有效。
- 输入框同步设 `m_syncing` 标志，避免 `EN_CHANGE` 回写抖动（同描边 popup）。

GDI 画 SV / 滑条用现有 `GdiObject` / `PaintGuard` / `SelectGuard`，禁止裸 `DeleteObject` 漏配对。

---

## 7. 测试

| 层 | 覆盖 |
|----|------|
| `color_convert` | RGB↔HSV 往返（允许 1 级量化误差）、Hex 解析/格式化、非法串、通道钳制 |
| `ColorPickerState` | 切换格式不丢色；取消等于 `committed`；确认等于 `draft`；纯黑改 V 保持 hue |
| Overlay 布局 | 有颜色的工具栏宽度含当前色块；马赛克仍 `ShowsColor==false` |
| Renderer | `a==255` 像素与旧测试一致；`a==0` 不改底；半透明红叠白底可算出混合值 |
| 不测 | 真实鼠标拖窗、吸管对着真屏；这些靠手工 |

不写模拟整段 `WndProc` 的脆弱单测。拖拽钳制若能抽成「给定虚拟屏与窗口尺寸 → 新原点」的纯函数，则对该函数单测。

---

## 8. 实施顺序（计划阶段再拆任务）

1. `color_convert` + 单测。
2. Renderer src-over + 单测。
3. 属性栏当前色块布局 / 绘制 / 点击（先可打开空窗口）。
4. 调色窗口：SV、色相、透明度、格式行、确认取消。
5. 标题栏拖拽 + 虚拟屏钳制 + 与点外面取消的互斥。
6. 画布吸管。
7. 接线 `setColor` / 文字实时样式 / 与描边 popup 互斥 / overlay 销毁。

---

## 9. 验收

- 矩形、椭圆、箭头、画笔、文字都能打开同一调色板；马赛克没有。
- 快捷色仍一键生效；当前色块能编出预设之外的 HEX/RGB/HSV 色。
- 确认后新描边 / 文字用该色；取消后颜色不变。
- 调色窗口能按标题栏拖到虚拟屏内任意位置，子控件跟着走，不会拖丢。
- 透明度小于 100% 的笔迹叠在截图上能看出半透明。
- 现有标注相关单测保持绿色。
