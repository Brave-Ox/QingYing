# 轻映 QingYing — 开发进度（PROGRESS）

> 当前基线：`master` / `b91dbe21`（本地标注与远端 F1/钉图/长截图已合并）
> 更新日期：2026-08-31
> 判定规则：实现、接线、自动测试和人工验收分别记录；提交标题只作佐证。

功能范围见 [开发清单](../轻映-QingYing-开发清单.md)，当前结构见 [architecture.md](architecture.md)，整改顺序见 [架构如何调整.md](架构如何调整.md)。

---

## 1. 当前状态总览

| 范围 | 状态 | 当前结论 |
|---|---|---|
| 工程骨架 | **完成** | CMake / MSVC / C++17；1 个 EXE + 10 个 static lib（含 ui / annotate / pin / longshot） |
| 普通截图主链路 | **基本闭环** | 热键 → 桌面快照遮罩 → 框选 / 吸附 / 调区 → 复制 / 保存 / Pin |
| F1 自定义区域 | **代码完成** | 自由框选、八点调整、移动、取消、虚拟桌面、物理像素转换 |
| F2 窗口吸附 | **代码完成，待人工验收** | 候选过滤、悬停高亮、点击吸附、DWM 边框修正 |
| F3 标注 | **代码已接线，最小闭环** | 矩形/椭圆/箭头/画笔/马赛克/文字、样式二级栏、撤销；选区「编辑」就地标注后回写并复制 |
| F4 导出 | **完成** | CF_DIB 剪贴板和 WIC PNG |
| F5 Pin | **代码基本完成，待人工验收** | 多 Pin、自动避让、缩放、独立导出、捕获排除 |
| F6 长截图 | **记事本代码路径已接入** | 固定选区拼接、预览、暂停 / 继续 / 停止、失败清理；另两应用未实现 |
| F7 托盘热键 | **完成** | 单实例、托盘、热键、冲突提示、开机自启开关 |
| F8 / F9 | **Stub** | `CommandParser` / `McpBridge` 仅骨架；截窗与中央裁切也是桩 |

一句话：普通截图、窗口吸附、Pin、记事本长截图与标注就地编辑已经形成代码链路；下一步是双端功能的真实环境验收，以及标注完成后回到完整 Save / Pin 操作条。

---

## 2. 已实现并接线

### 2.1 Application 与基础设施

- Named Mutex 单实例；
- 托盘图标、退出菜单和开机自启开关；
- 全局热键 `Ctrl+Shift+Q` 与冲突提示；
- `WM_QINGYING_BEGIN_CAPTURE` 将热键处理延后到 UI 消息流；
- `ActionDispatcher` 已注册 `Status / CaptureRegion / Copy / Save / Pin`；
- `qingying_ui` 提供选区条 / 标注底栏共用的白色圆角 `ModernToolbar`、GDI+ 绘制和 SVG 路径图标；
- 共享 `Image` 与 `CaptureSession` 已打通区域截图、导出、Pin 与标注结果回写；
- 应用退出时停止长截图 worker、关闭 Overlay 并回收线程。

### 2.2 F1 区域选择与桌面遮罩

| 能力 | 主要文件 | 自动验证 |
|---|---|---|
| PMv2 DPI 感知 | `src/app/main.cpp` | 启动代码核对 |
| 虚拟桌面与负坐标 | `coordinate_transform.*` | 坐标测试 |
| 桌面截图背景 + 遮罩合成 | `mask_renderer.*` / `application.cpp` | 背景合成测试 |
| 自由框选与反向归一化 | `selection_controller.*` | 状态机测试 |
| 八点调区、移动与边界钳制 | `selection_handles.*` / `selection_controller.*` | 命中和几何测试 |
| Esc / 右键取消 | `selection_overlay.cpp` | 代码路径；人工体验待持续回归 |
| 物理像素区域截图 | `capture_engine.cpp` | GDI 实图测试与 F1 集成测试 |

桌面背景快照由 `Application` 在 Overlay 出现前获取，并临时隐藏 Pin。Overlay 使用 `WS_EX_NOACTIVATE`，避免原前台窗口的 owned popup 因失活而消失。

### 2.3 F2 窗口吸附

- `WindowDetector::detectAt` 根据鼠标位置定位顶层窗口；
- 过滤自身进程、最小化窗口、工具窗口、DWM cloaked 窗口、桌面外壳和无效矩形；
- 使用 DWM 扩展边框修正阴影导致的吸附偏移；
- Overlay 绘制候选高亮，点击后调用 `SelectionController::setSelection`；
- 吸附后仍可八点调整或重新自由框选；
- 自动测试覆盖矩形基础、屏外点、自身窗口和空窗口过滤。

### 2.4 F3 标注

| 能力 | 文件 | 验证方式 |
|---|---|---|
| 文档模型 + 撤销/重做 | `annotation_document.*` | `annotation_document_test` |
| 矩形/箭头/画笔/椭圆/文字/马赛克栅格化 | `annotation_renderer.*` | `annotation_renderer_test` |
| 引擎组装 | `annotation_engine.*` | `annotation_engine_test` |
| 纯逻辑会话（确认/取消） | `annotation_editor_session.*` | `annotation_editor_session_test` |
| 拖拽交互（工具/预览/入栈） | `annotation_interaction_controller.*` | `annotation_interaction_controller_test` |
| 就地编辑 Overlay | `annotation_overlay.*` | 自动布局 / 交互逻辑 + 1 个 `DISABLED_` 窗口冒烟 |
| 标注工具条 | `modern_toolbar` | 主栏工具 + 颜色/线宽或字号二级栏；SVG 图标 |

第三人接线：选区操作条「编辑」→ `AnnotationOverlay::showInPlace` → 合成图写入 `SelectionResult::annotated_image` → Application 回写 Session 并复制。

### 2.5 F4 导出

- `DibEncoder` 将 BGRA32 `Image` 编码为底向上 CF_DIB；
- `ExportService::copyToClipboard` 正确转移 `HGLOBAL` 所有权；
- `ExportService::savePng` 使用 WIC；
- 空图、空路径和 PNG 输出已有自动测试；
- GUI 保存对话框目前仍在 `Application::saveImage`，普通 Save 尚未完全复用 `SaveHandler`。

### 2.6 F5 Pin

- 无原生标题栏的置顶 `PinWindow`；
- 保持宽高比显示，支持拖动、边缘 / 四角等比缩放和关闭；
- 右键复制 / 保存使用各自 Pin 的 `Image`，不会串到最新 Session；
- 多 Pin、关闭全部、数量统计与生命周期清理；
- 新 Pin 从虚拟桌面右侧开始自动寻找不重叠位置；
- `CaptureGuard` 支持嵌套隐藏 / 恢复，普通截图和背景快照均排除 Pin；
- 自动测试覆盖空图拒绝、多窗口、析构清理和不重叠排布。

### 2.7 F6 记事本长截图

已实现：

- `LongShotRequest` 固定使用用户选中的物理像素矩形；
- Application 在 Overlay 前记录原前台顶层窗口；
- Notepad profile 校验目标进程、编辑子窗口、内容区和滚动条；
- 首帧、滚动后帧、重叠查找与追加拼接；
- 到底、无新增内容、最大 30 帧和最大 30000 像素停止；
- 长截图 worker 与 UI 线程完成消息；
- Overlay 保持选区孔洞透传并显示累计预览；
- 暂停 / 继续、停止，以及暂停后选择复制 / 保存 / Pin 的收尾行为；
- 失败时结束 worker、关闭或恢复 Overlay，并保留上一张有效 Session 结果。

尚未完成：

- 记事本真实长文手工闭环记录；
- 资源管理器 profile；
- Edge profile；
- 三应用完整验收。

---

## 3. 未实现或仍为 Stub

### F3 标注后续（不是 Stub）

- 标注完成后当前回写 Session 并自动 Copy，尚未回到完整操作条（Save / Pin）；
- 重做 UI 仍待补；
- 就地标注与八点调区 / 多屏的组合冒烟待验收。

### F8 本地口令

- `CommandParser::tryParse` 固定返回 `false`；
- `CaptureEngine::captureWindow` 与 `cropCenter` 返回 `kNotImplemented`；
- 没有口令表、歧义处理或 GUI 降级入口。

### F9 MCP

- `McpBridge::start` 固定返回 `false`；
- 没有 Named Pipe、JSON / schema、Tool 映射和主线程投递。

---

## 4. 自动验证基线

当前基线命令：

```bat
build.bat Release test
```

2026-08-31 Release 结果：CTest 发现 **252** 个用例，实际执行 **251** 个且全部通过；
`AnnotationOverlayTest.DISABLED_SmokeConfirmReturnsSourceCopy` 为显式禁用的窗口冒烟测试。24 个测试源文件已纳入构建。

自动测试不能替代：

- 双屏与混合 DPI 的实际视觉 / 输入验证；
- F2 在真实应用上的候选边界；
- 多 Pin 的交互和隐藏 / 恢复闪烁；
- 真实长文滚动拼接；
- 就地标注与选区 Overlay 的组合验收。

---

## 5. Git 进度证据

| 日期 | 能力 | 代表提交 |
|---|---|---|
| 2026-08-24 | 主路径、图片契约、区域捕获、剪贴板、PNG | `141d9707`、`5d5a3f5a`、`22f807cb`、`0d42a1c6`、`9c12c132` |
| 2026-08-25 | F1 调区、DPI 修复、Pin 与捕获排除 | `c40de0da`、`252f451d`、`726e8528`、`68e69337`、`0a5ad00e` |
| 2026-08-26 | F2 窗口吸附和边框修正 | `d1316dda`、`5de31936` |
| 2026-08-26 | 标注文档/渲染/引擎/Overlay 与就地编辑 | 本地 15 提交（`c26b62b5`～`cfa74978`） |
| 2026-08-27 | owned popup 修复与桌面快照背景遮罩 | `615c2f2a`、`1c9ac4d0` |
| 2026-08-28 | Pin 自动避让 | `d8998e02` |
| 2026-08-26～28 | F6 请求、profile、采集、拼接、停止条件、Application 接线 | `b1ee85bf`～`9607c013` |
| 2026-08-28 | F6 预览、暂停停止和失败清理 | `7b66b2ae`、`1aeb4f3a`、`68f0e740` |
| 2026-08-30 | 合并标注链路与 F1 / Pin / F6 | `d9f7c0dc` |
| 2026-08-31 | 选区 / 标注共用圆角 ModernToolbar 与 SVG 图标 | `b91dbe21` |

---

## 6. 技术债与架构偏差

- `selection_overlay.cpp` 当前包含窗口、渲染、ModernToolbar、F2、长截图预览与就地标注入口（约 1,325 行）；
- `annotation_overlay.cpp` 已形成完整标注窗口和样式交互，但仍采用同线程模态消息循环（约 3,296 行）；
- 工具栏按钮 ID、创建、启用条件和命令 switch 为硬编码；
- `SelectionOverlay::show` 拥有嵌套模态 `GetMessage`，与原“消息循环只在 app”的文档表述不完全一致；
- `Application` 直接执行桌面背景捕获、长截图与部分保存，原“业务能力只经 Dispatcher”过于绝对；
- `LongShotRegion` 枚举存在但没有 Handler；
- `qingying_overlay` 在 CMake 中公开链接 `qingying_capture`；就地标注会先隐藏遮罩，再在 Overlay 内直接调用 `CaptureEngine` 重新抓取选区；
- `CaptureEngine`、`LongShotEngine`、`McpBridge` 仍以裸 `Impl*` 管理 PIMPL；
- `ActionRequest` 会随 F8/F9 继续膨胀，缺少类型安全 payload；
- `CaptureSession` 是有意保留的“最近结果”状态，但 Handler 间数据流仍具有隐式时序依赖。

整改方案见 [架构如何调整.md](架构如何调整.md)。

---

## 7. 下一步建议

1. 对最新合并版本人工走一遍截图 / 标注 / 钉图 / 长截图；
2. 标注完成后回到完整操作条（Save / Pin，Copy 已自动执行）；
3. 完成 F1/F2/F5 的双屏、混合 DPI 和多 Pin 人工验收；
4. 完成记事本真实长截图闭环，再增加资源管理器和 Edge profile；
5. 收紧 `overlay → capture` 依赖，PIMPL 改 `unique_ptr`，明确 Dispatcher 与 Workflow 边界；
6. 完成上述契约后再进入 F8/F9。

---

本文随代码持续更新。提交标题用于定位，功能完成度最终以源码、自动测试和人工验收记录共同判定。
