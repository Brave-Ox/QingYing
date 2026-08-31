# 轻映 QingYing — 开发清单与技术要点

> 依据立项文档、仓库架构和当前代码整理。
> 当前提交基线：`master` / `a4e400bc`（共 63 条提交；本文同时反映当前待提交架构调整）；同步日期：2026-08-31。
> 状态判断同时参考实现、测试和 Git 提交；“代码完成”不等于“真实环境人工验收完成”。

来源：

- [轻映-QingYing（立项）](https://365.kdocs.cn/l/cg3MggX6b0hl)
- [轻映 QingYing《技术选型与架构说明书》](https://365.kdocs.cn/l/cjyNzW0HlWuo)
- [当前架构](./docs/architecture.md)
- [架构调整方案](./docs/架构如何调整.md)
- [当前进度](./docs/PROGRESS.md)

---

## 1. 一句话目标

用不超过 **20 MB** 的 Windows 绿色单文件，打通“框选 → 标注 → 钉图对照 → 长页拼接”全链路；并通过本地口令与 MCP，为 Agent 提供可控的本地截图能力。

| 项 | 内容 |
|---|---|
| 产品名 | 轻映 QingYing（轻 = 可核对的轻量；映 = 钉在桌面对照） |
| Slogan | 截得轻，钉得住，长页一次成 |
| 平台 | Windows only |
| GUI | C++17 + Win32；不用 Qt / Electron |
| 主路径 | 热键 → 框选 / 调区 → 标注 → 复制 / 保存，目标约 15 秒 |
| 当前主链路 | 热键 → 桌面快照遮罩 → 框选 / 吸附 / 调区 → 编辑标注 → 自动复制 → 恢复结果操作条 → 保存 / Pin / 再编辑 |

---

## 2. 硬指标与当前验证

| 指标 | 目标 | 当前状态 | 证据 / 下一步 |
|---|---:|---|---|
| 发布体积 | ≤ 20 MB | 当前 EXE 达标 | 2026-08-31 Release `qingying.exe` 为 218,624 字节；仍需按最终交付包复测 |
| 常驻内存 | ≤ 40 MB | 未测 | 托盘空闲状态记录工作集与峰值 |
| 唤起时延 | ≤ 300 ms | 未测 | 记录热键消息到 Overlay 首帧完成的时间 |
| 主路径演示 | 约 15 秒 | 代码闭环 | F3 标注后自动复制并可继续 Save / Pin / 再编辑；完整人工 Demo 待记录 |
| 自动测试 | 专项全绿 | 已验证 | 2026-08-31 Release：CTest 发现 277 个，276 个执行；272 个通过，4 个当前环境下既有 `BitBlt` 失败，1 个窗口冒烟测试显式禁用 |

约束：F1～F7 不依赖网络；模型不进入安装包；主截图路径必须本地闭环。

---

## 3. 功能清单（F1～F9）

| ID | 功能 | 当前状态 | 已实现范围 | 剩余验收 / 开发 |
|---|---|---|---|---|
| **F1** | 自定义截图区域 | **代码完成** | 自由框选、反向归一化、八点调区、移动、取消、虚拟桌面与物理像素转换 | 双屏和混合 DPI 人工冒烟 |
| **F2** | 窗口吸附 | **代码完成，待人工验收** | 悬停检测、高亮、点击吸附、自身 / 工具 / 最小化 / 桌面窗口过滤、DWM 边框修正 | 记事本、资源管理器、Edge 与混合 DPI 实测 |
| **F3** | 截图标注 | **代码动作闭环，待人工验收** | 文档 / 引擎 / 渲染器、六类工具、样式二级栏、撤销；编辑结果回写 Session、自动复制并恢复 Save / Pin / 再编辑操作条 | 重做 UI、真实窗口 / DPI 交互验收 |
| **F4** | 导出 | **已实现** | CF_DIB 剪贴板、WIC PNG 保存、输入校验 | UI 错误文案和最终人工回归 |
| **F5** | 钉图 | **代码基本完成，待人工验收** | 置顶、多 Pin、自动避让、拖动、等比缩放、关闭、独立复制 / 保存、捕获时隐藏恢复 | 多 Pin、缩放、隐藏恢复的视觉体验实测 |
| **F6** | 长截图 | **记事本路径已接入** | 固定选区、记事本内容区校验、滚轮驱动、重叠拼接、到底 / 无新增 / 上限停止、预览、暂停 / 继续 / 停止、失败清理 | 记事本真实闭环；资源管理器与 Edge profile；三应用验收 |
| **F7** | 托盘与热键 | **已实现** | 单实例、托盘、退出、开机自启开关、`Ctrl+Shift+Q`、冲突提示 | 重启 Explorer、开机自启和长期驻留人工验证 |
| **F8** | 口令截图 | **Stub** | `CommandParser` 接口存在 | 本地口令表；`CaptureWindow` / `CropCenter` 实现；未命中降级 |
| **F9** | Agent / MCP | **Stub** | `McpBridge` PIMPL 骨架存在 | Named Pipe、协议、Tool 映射、主线程调度、鉴权边界 |

### F1～F9 与技术模块

| 功能 | 当前调用链 / 模块 |
|---|---|
| F1 | `SelectionOverlay` → `SelectionResult` → `Application` → `ActionDispatcher(CaptureRegion)` → `CaptureEngine` |
| F2 | `WindowDetector` + `SelectionOverlay` + `SelectionController::setSelection` |
| F3 | `SelectionOverlay(Edit intent)` → `Application + CaptureEngine` → `AnnotationOverlay / Engine / Renderer` → Session + 自动 Copy → `composeCapturePreview` → 恢复结果操作条 |
| F4 | `ActionDispatcher(Copy/Save)` + `ExportService`；GUI 文件对话框目前由 `Application` 管理 |
| F5 | `ActionDispatcher(Pin)` + `CaptureSession` + `PinManager / PinWindow` |
| F6 | `Application` 工作流 + `SelectionOverlay` + `LongShotEngine` + `CaptureEngine` + `ImageStitcher` |
| F7 | `Application / TrayController / HotkeyManager / SingleInstanceGuard / AutostartSettings` |
| F8 | `CommandParser` → `ActionRequest`（待实现） |
| F9 | `McpBridge` → `ActionDispatcher / Workflow`（待实现） |

---

## 4. 范围边界（本期明确不做）

- 屏幕录制 / GIF 产品化；
- 云同步图床、账号体系、AI 改图；
- 完整 OCR 产品化，或让模型读整屏寻找按钮后再截图；
- 跨平台，以及 Qt / Electron 等重型 GUI；
- 标注 / 调区等指针操作的 MCP 自动化；
- 向 Agent 暴露 DXGI / GDI 等内部接口；
- 任意应用长截图、自动点击和通用 UI 自动化；
- 本地大模型打入安装包。

---

## 5. 分阶段开发清单

### P0 — 可演示的“截一下”

- [x] CMake / MSVC / C++17 / Release 单 EXE；
- [x] 10 个 static lib 与 `ActionDispatcher` 骨架；
- [x] `SingleInstanceGuard`（Named Mutex）；
- [x] 系统托盘、退出和开机自启开关；
- [x] `RegisterHotKey` 全局热键及冲突提示；
- [x] 全虚拟桌面 `SelectionOverlay`；
- [x] 鼠标拖拽框选、Esc / 右键取消；
- [x] `CaptureEngine::captureRegion` 的 GDI `BitBlt` 实现；
- [x] 复制到剪贴板（CF_DIB）；
- [x] WIC PNG 保存；
- [x] `CaptureRegion / Copy / Save / Pin / Status` Handler 注册；
- [x] `ModernToolbar` 共用选区条 / 标注底栏，GDI+ 与 SVG 路径图标；
- [x] `SelectionToolbar`、`OverlayPhase` 与 `OverlayRenderer`，选区命令、顶层阶段和像素合成从 `selection_overlay.cpp` 抽离；
- [ ] DXGI Desktop Duplication；当前只链接 `d3d11/dxgi`，没有实现 DXGI 捕获路径。

### P1 — 可用的“截—标”

- [x] 八点调区和移动选区；
- [x] 窗口嗅探、候选过滤、悬停高亮和点击吸附；
- [x] PMv2 进程感知、虚拟桌面与基础 DPI 坐标转换；
- [ ] 双屏、125% / 150% / 200% 与混合 DPI 人工冒烟；
- [x] 标注对象模型和工具状态；
- [x] 矩形、椭圆、箭头、画笔、文字、马赛克；
- [x] 撤销 / 重做内核和离屏栅格化；
- [x] `AnnotationOverlay` 与编辑完成回流到 Session 并自动复制；
- [x] 标注后自动 Copy，并恢复 Save / Pin / 再编辑操作条；
- [ ] 重做按钮和真实交互验收；

### P2 — “钉得住 / 长页一次成”

- [x] 独立 Pin：置顶、拖动、缩放、复制、保存、关闭；
- [x] 多 Pin 并存和右侧自动避让排布；
- [x] 捕获前临时隐藏 Pin，结束或失败后恢复；
- [x] 记事本长截图代码闭环：固定选区、滚动、拼接、预览、暂停 / 停止；
- [x] 长截图安全限制：最大 30 帧、最大 30000 像素、到底和无新增停止；
- [ ] Pin 与记事本长截图真实人工闭环；
- [ ] 资源管理器长截图 profile；
- [ ] Edge 长截图 profile；
- [ ] 三应用各至少一次验收。

### P3 — “口令 + Agent”

- [x] GUI 主路径已有 `ActionDispatcher`；
- [ ] `CaptureWindow` 和 `CropCenter` 从桩升级为真实实现；
- [ ] 本地口令表 → 类型安全 Action；
- [ ] 明确交互式动作的异步工作流契约；
- [ ] Named Pipe：MCP Bridge ↔ 托盘主进程；
- [ ] 实现 F9 Tool 集；
- [ ] 失败返回明确错误，不自动操作屏幕或无限重试。

### P4 — 可选增强

- [ ] Skill 文档（场景、参数、失败后改用热键框选）；
- [ ] 云端 LLM 仅把一句文本解析为 Action，不读屏、不执行截图；
- [ ] `suggest_name`：明确告知并经用户确认后才处理当前图片。

---

## 6. 当前技术选型与规划差异

| 领域 | 当前实现 | 规划 / 说明 |
|---|---|---|
| 语言 / GUI | C++17 + Win32 | 已落地；不用 Qt / Electron |
| Overlay 绘制 | `OverlayRenderer`：GDI DIB + `UpdateLayeredWindow` | Direct2D 尚未使用 |
| 共用工具栏 | `qingying_ui`：GDI+ 圆角条、SVG 路径图标、中文 Tooltip | 当前选区条和标注底栏已接入 |
| 区域捕获 | GDI `BitBlt` | DXGI Desktop Duplication 待实现 |
| 标注 | `qingying_annotate`：Document / Engine / Renderer / EditorSession / Overlay | 动作回流已接线；重做 UI 与人工验收待补 |
| 窗口 / 中央裁切 | 方法存在但返回 `kNotImplemented` | F8/F9 前实现 |
| 多屏 / DPI | PMv2 + Virtual Screen + 坐标工具 | 混合 DPI 人工验收待完成 |
| 剪贴板 | Win32 Clipboard / CF_DIB | 已实现 |
| PNG | WIC | 已实现 |
| 热键 / 单实例 | `RegisterHotKey` / Named Mutex | 已实现 |
| 长截图 | Win32 滚轮消息 + GDI 区域帧 + 像素重叠 | 当前仅记事本 profile |
| IPC / MCP | 仅骨架 | Named Pipe 和协议未实现 |
| JSON | 未引入 | 等 MCP 参数结构确定后再选型 |
| 发布 | 单 EXE | 最终包、运行库依赖仍需交付验收 |

---

## 7. Action 与交互工作流

当前已经注册的 Action：

```text
Status / CaptureRegion / Copy / Save / Pin
```

枚举存在但尚未注册或实现的 Action：

```text
CaptureWindow / CropCenter / LongShotRegion / SuggestName
```

当前真实调用关系：

```text
GUI 热键
  → Application 交互工作流
  → SelectionOverlay
  → 普通动作：dispatch(CaptureRegion)
  → CaptureSession
  → dispatch(Copy / Pin)

标注动作
  → SelectionOverlay 返回 Edit 意图
  → Application 捕获源图并打开 AnnotationOverlay
  → 自动 Copy + 恢复结果操作条
  → Save / Pin / 再编辑

交互式长截图
  → Application 启动 worker
  → LongShotEngine
  → 完成消息回 UI 线程
  → CaptureSession
  → dispatch(Copy)
```

架构原则调整为：

1. 外部入口（口令 / MCP）不得直接调用引擎；
2. 单步业务命令统一走 `ActionDispatcher`；
3. 选区、标注、交互式长截图属于多步工作流，由 `Application` 或后续 `CaptureWorkflow` 编排；
4. UI 预览快照等表现层基础设施必须明确标注例外，不得假装已经经过 Dispatcher；
5. 具体整改顺序见 [架构如何调整](./docs/架构如何调整.md)。

---

## 8. MCP / Skill 规划清单

| Tool | 作用 | 当前状态 |
|---|---|---|
| `status()` | 查询托盘是否运行 | Handler 已有；MCP 未接 |
| `capture_window(query)` | 按窗口名截取，多匹配返回歧义 | Capture 方法为桩 |
| `crop_center(width, height)` | 裁取画面中央 | Capture 方法为桩 |
| `longshot_select()` | 打开长截图选框，由用户确认区域 | GUI 记事本路径已有；MCP 工作流未设计 |
| `save(path, name)` | 按名保存当前结果 | Save Handler 已有；MCP 未接 |
| `copy()` / `pin()` | 操作当前结果 | Handler 已有；MCP 未接 |
| `suggest_name()` | 建议文件名 | 未实现，可选云能力 |

MCP 只允许本机连接；不得远程暴露桌面截图能力，不得直接访问 DXGI/GDI 或模拟任意鼠标键盘操作。

---

## 9. 关键验收用例

- [ ] 主路径：热键 → 框选 / 调区 → 标注 → 复制，约 15 秒；
- [ ] 常驻内存 ≤ 40 MB，热键到 Overlay 首帧 ≤ 300 ms；
- [ ] 双屏 + 125% / 150% / 200% 与混合 DPI 无明显偏移；
- [ ] F2：记事本、资源管理器、Edge 的吸附边界与最终截图一致；
- [ ] F5：至少 2 枚 Pin 不重叠、可拖拽缩放，且新截图不含 Pin；
- [ ] F6：记事本 / 资源管理器 / Edge 各至少一次自动拼接；
- [ ] F8：现场演示至少两类本地口令；
- [ ] F9：至少成功调用两项 Tool，错误时返回稳定错误码；
- [x] Release 构建；277 个测试已发现，276 个执行，其中 272 个通过、4 个为当前环境下既有 `BitBlt` 失败，1 个 `DISABLED_` 窗口冒烟测试；
- [ ] 最终交付包满足单文件、运行库和体积约束。

---

## 10. 风险与应对

| 风险 | 当前等级 | 当前措施 | 下一步 |
|---|---|---|---|
| 混合 DPI 坐标错误 | 高 | 物理像素契约、PMv2、坐标单测 | 真实多屏冒烟；必要时按显示器管理 Overlay |
| 长截图拼接失败 | 高 | 限记事本、重叠匹配、到底 / 无新增 / 上限停止 | 真实长文验证，再扩展两个 profile |
| Overlay 状态膨胀 | 中 | SelectionController、SelectionToolbar、OverlayPhase、OverlayRenderer 已抽出 | 在 Workflow 阶段非模态化 |
| 自身 Pin 被截入 | 中 | RAII CaptureGuard 隐藏 / 恢复 | 人工验证视觉闪烁和异常路径 |
| Dispatcher 与工作流边界不清 | 中 | 单步动作已有 Handler | 引入 `CaptureWorkflow`，修正文档与依赖 |
| PIMPL 所有权 | 低 | Capture / LongShot / MCP 已改为 `std::unique_ptr<Impl>` | 继续检查跨线程消息所有权 |
| MCP ↔ 主进程通信 | 中 | 尚未实现 | 本机 Named Pipe、主线程投递、参数校验 |
| 云端不可用 | 低 | F1～F7 全本地 | F8 本地口令作为默认路径 |

---

## 11. Git 实现证据

| 能力 | 代表提交 |
|---|---|
| P0 主链路、图片契约与导出 | `141d9707`、`5d5a3f5a`、`22f807cb`、`0d42a1c6`、`9c12c132` |
| F1 调区与 DPI 修复 | `c40de0da`、`252f451d`、`4137175e` |
| F2 窗口吸附与边框修正 | `d1316dda`、`5de31936` |
| 桌面快照遮罩与浮层兼容 | `615c2f2a`、`1c9ac4d0` |
| F5 Pin、捕获排除、独立导出和自动排布 | `726e8528`、`6f927b01`、`68e69337`、`0a5ad00e`、`d8998e02` |
| F6 基础、profile、拼接、接线与交互控制 | `b1ee85bf`～`9607c013`、`7b66b2ae`、`1aeb4f3a`、`68f0e740` |
| F3 标注文档、渲染、编辑器与 Application 接线 | `c26b62b5`～`cfa74978`、`d9f7c0dc` |
| 选区 / 标注共用 ModernToolbar 与 SVG 图标 | `b91dbe21` |
| SelectionToolbar / OverlayPhase、编辑源图上移与结果动作回流 | 当前工作区，待提交 |

提交标题用于定位，最终完成度以当前源码和测试结果为准。

---

## 12. 交付物清单

- [x] 当前可运行 Release EXE；
- [x] 当前架构、进度、耦合分析和架构调整文档；
- [ ] F3 Copy / Save / Pin / 再编辑的完整 15 秒人工 Demo 记录；
- [ ] F5 / F6 / DPI 人工验收记录；
- [ ] Skill / MCP 接口说明；
- [ ] 最终绿色交付包及体积 / 内存 / 时延报告；
- [ ] 立项计划书和完整 Demo 脚本。

---

## 13. 架构一句话

```text
入口适配（热键 GUI / 本地口令 / MCP）
        → CaptureWorkflow（多步交互）或 ActionDispatcher（单步命令）
        → 本地能力模块（Capture / Annotation / Pin / LongShot / Export）
```

Agent 不直接操控屏幕，只通过青影公开的本地 Action / Workflow 使用已有能力。
