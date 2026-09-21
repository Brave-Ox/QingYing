# 轻映 QingYing — 开发进度（PROGRESS）

> 当前提交基线：`e2c4264`（已合并 F8 CommandParser 与远端长截图更新）
> 更新日期：2026-09-21
> 判定规则：实现、接线、自动测试和人工验收分别记录；提交标题只作佐证。

功能范围见 [开发清单](../轻映-QingYing-开发清单.md)，当前结构见 [architecture.md](architecture.md)，交接摘要见 [项目理解.md](项目理解.md)，整改顺序见 [架构如何调整.md](架构如何调整.md)。

2026-09-20 F6 自动长截图 C11：在现有 profile 优先、通用滚轮回退和保守拼接链路上保留总时长、同位置重采、滚动稳定轮询、输出尺寸和工作内存预算；帧数与滚轮输入次数不设硬上限，输入次数只作诊断。超限前使用检查算术，已有可靠图像按 `LimitReached` 进入确认态。完成载荷记录首帧耗时、总耗时、最慢帧、峰值工作内存和预览发布次数。自动回归覆盖预算、部分结果恢复、停止响应及 latest-wins 预览；真实 Notepad / Explorer / Chromium、Qt / Electron、混合 DPI 和多屏仍待人工验收，不能据代码与 mock 测试宣称兼容。

---

## 1. 当前状态总览

| 范围 | 状态 | 当前结论 |
|---|---|---|
| 工程骨架 | **完成** | CMake / MSVC / C++17；1 个 EXE + 分层 static lib / interface target，并部署 3 个受控 longshot profile DLL |
| 普通截图主链路 | **基本闭环** | 热键 → 桌面快照遮罩 → 框选 / 吸附 / 调区 → 复制 / 保存 / Pin |
| F1 自定义区域 | **代码完成** | 自由框选、八点调整、移动、取消、虚拟桌面、物理像素转换 |
| F2 窗口吸附 | **代码完成，待人工验收** | 候选过滤、悬停高亮、点击吸附、DWM 边框修正 |
| F3 标注 | **代码动作闭环，待人工验收** | 六类工具、样式二级栏、撤销/重做；非模态编辑器确认后自动复制并恢复结果操作条，可继续保存 / Pin / 再编辑 |
| F4 导出 | **完成** | CF_DIB 剪贴板和 WIC PNG |
| F5 Pin | **代码基本完成，待人工验收** | 多 Pin、自动避让、缩放、独立导出、捕获排除 |
| F6 长截图 | **自动链路与预算回归完成，待真实桌面验收** | profile 优先并带通用安全回退；固定选区、稳定帧检测、保守拼接、部分结果恢复、固定边缘显式配置、暂停 / 继续 / 停止和资源预算已接线；内置 profile 为受控 DLL 插件 |
| F7 托盘热键 | **完成** | 单实例、托盘、热键、冲突提示、开机自启开关 |
| F8 本地口令 | **解析器完成，待入口接线** | 已解析窗口截取、中心裁剪、复制、钉图、状态与保存；尚无 GUI 口令输入与多动作调度入口 |
| F9 MCP | **代码已接线，待完整验收** | MCP protocol session、stdio、Named Pipe、AutomationEndpoint、CaptureWindow / CropCenter / Copy / Save / Pin 主链已实现；安装包和真实桌面验收仍需补 |

一句话：普通截图、窗口吸附、Pin、长截图自动尝试、SmartRegion 候选链和标注结果 Copy / Save / Pin 已形成代码链路，多步编排已从 Application 收口到 `CaptureWorkflow`，长截图异步生命周期已收口到 `LongShotController`，Selection / Annotation Overlay 已改为非模态；下一步是已知与未知应用的真实长截图验收、F9 安装包验收和 P0 架构风险收敛。

---

## 2. 已实现并接线

### 2.1 Application 与基础设施

- Named Mutex 单实例；
- 托盘图标、退出菜单和开机自启开关；
- 全局热键 `Ctrl+Shift+Q` 与冲突提示；
- `WM_QINGYING_BEGIN_CAPTURE` 将热键处理延后到 UI 消息流；
- `qingying_workflow` 统一编排 Selection / Annotation / LongShot 与结果动作，Application 只负责组合根和消息转发；
- `ActionDispatcher` 已注册 `Status / CaptureRegion / Copy / Save / Pin`；`ActionRequest` 已使用 `std::variant` 类型化 payload，并带 request / operation id、取消、超时和结果选择；
- `qingying_action_compatibility` 已从 `qingying_action` 核心拆出，仅保留 deprecated 的旧请求适配器；生产 EXE、workflow、automation、MCP 和插件 target 不依赖它；
- `qingying_app_handlers` 还注册 `CaptureWindow / CropCenter`，由 `CaptureService` 负责 WindowResolver、物理矩形计算、复核和 ResultStore 发布；
- `qingying_automation_contract`、`qingying_automation`、`qingying_ipc`、`qingying_mcp` 和 `qingying_app_runtime` 已形成本地自动化接入链；当前 automation 对具体 CaptureWorkflow 仍有一处反向依赖，见整改文档；
- `ApplicationShutdownCoordinator` 已接入组合根，按四阶段编排自动化接入、业务生产者、Workflow 和回调清理；ExportExecutor、LongShotController、UIA query worker 和 PipeServer 已支持共享 deadline 的 `joinUntil`，超时会停止后续销毁并保留应用对象图到进程结束；插件 v1 ABI 还支持可选 cooperative cancel，旧 DLL 仍可加载；
- `qingying_ui` 提供选区条 / 标注底栏共用的白色圆角 `ModernToolbar`、GDI+ 绘制和 SVG 路径图标；
- `SelectionToolbar` 独立管理选区操作条 HWND、命令与阶段映射，`OverlayPhase` 集中校验选区 / 长截图 / 关闭阶段；
- `SelectionOverlay` 与 `AnnotationOverlay` 创建后立即返回，窗口消息统一由 Application 顶层消息泵处理；Workflow 通过阶段消息续接选区和标注；
- `ResultStore` 与 `ResultActionService` 已打通区域截图、导出、Pin 与标注结果回写；`CaptureSession` 仅作为兼容门面保留；
- `ScreenPhysicalRect`、`OverlayClientRect`、`ImagePixelRect` 等中立坐标类型已接入，跨线程 UI payload 由 `UiMessageChannel` 持有；
- 应用退出时由 `LongShotController` 停止并回收长截图 worker，同时关闭 Overlay。

### 2.2 F1 区域选择与桌面遮罩

| 能力 | 主要文件 | 自动验证 |
|---|---|---|
| PMv2 DPI 感知 | `src/app/main.cpp` | 启动代码核对 |
| 虚拟桌面与负坐标 | `coordinate_transform.*` | 坐标测试 |
| 桌面截图背景 + 遮罩合成 | `mask_renderer.*` / `capture_workflow.cpp` | 背景合成测试 |
| 自由框选与反向归一化 | `selection_controller.*` | 状态机测试 |
| 八点调区、移动与边界钳制 | `selection_handles.*` / `selection_controller.*` | 命中和几何测试 |
| Esc / 右键取消 | `selection_overlay.cpp` | 代码路径；人工体验待持续回归 |
| 物理像素区域截图 | `capture_engine.cpp` | GDI 实图测试与 F1 集成测试 |

桌面背景快照由 `CaptureWorkflow` 在 Overlay 出现前获取，并临时隐藏 Pin。SelectionOverlay 使用 `WS_EX_NOACTIVATE`，避免原前台窗口的 owned popup 因失活而消失；两个 Overlay 均不再自建消息循环。

### 2.3 F2 窗口吸附

- `WindowDetector::detectAt` 根据鼠标位置定位顶层窗口；
- 过滤自身进程、最小化窗口、工具窗口、DWM cloaked 窗口、桌面外壳和无效矩形；
- 使用 DWM 扩展边框修正阴影导致的吸附偏移；
- Overlay 绘制候选高亮，点击后调用 `SelectionController::setSelection`；
- 吸附后仍可八点调整或重新自由框选；
- 自动测试覆盖矩形基础、屏外点、自身窗口和空窗口过滤。

#### SmartRegion 当前实现与验证边界

- SmartRegionDetector 已形成窗口级快速回退、已知内容定位、视觉候选和 UIA/MSAA 详细候选的组合路径；
- UIA/MSAA 查询由 latest-wins worker 异步执行，SelectionOverlay 在 UI 线程按 root HWND、矩形、generation 和结果年龄校验后合并；手动框选优先于迟到结果；
- VisualLocator 已覆盖 Electron 工作台、Chromium 外壳、小控件、弱边界和有限候选缓存；Chromium Tab、按钮、书签/扩展入口以及受支持浏览器所属 WS_POPUP 临时弹窗已有代码和回归测试；
- 当前仍需真实 Chrome / Edge / Brave、VS Code/Cursor 和混合 DPI 桌面复测；普通 Window/known/visual 路径的同步耗时、UIA/MSAA provider 阻塞和 SmartRegion target 边界列入架构整改；
- SmartRegion 的专项自动测试已有多批回归；当前完整验证统一以第 4 节记录的 `build.bat Release test` 结果为准。

### 2.4 F3 标注

| 能力 | 文件 | 验证方式 |
|---|---|---|
| 文档模型 + 撤销/重做 | `annotation_document.*` | `annotation_document_test` |
| 矩形/箭头/画笔/椭圆/文字/马赛克栅格化 | `annotation_renderer.*` | `annotation_renderer_test` |
| 引擎组装 | `annotation_engine.*` | `annotation_engine_test` |
| 纯逻辑会话（确认/取消） | `annotation_editor_session.*` | `annotation_editor_session_test` |
| 拖拽交互（工具/预览/入栈） | `annotation_interaction_controller.*` | `annotation_interaction_controller_test` |
| 就地编辑 Overlay | `annotation_overlay.*` | 自动布局 / 交互逻辑；当前没有显式 Disabled 用例 |
| 标注工具条 | `modern_toolbar` | 主栏工具 + 颜色/线宽或字号二级栏；SVG 图标 |

当前接线：`SelectionOverlay` 只返回 Edit 意图 → `CaptureWorkflow` 抓取编辑源图并非模态打开 `AnnotationOverlay` → 合成图写入 `ResultStore` 并自动 Copy → `composeCapturePreview` 将结果贴回桌面快照 → 同一选区恢复结果操作条，可继续 Save / Pin / Edit。

### 2.5 F4 导出

- `DibEncoder` 将 BGRA32 `Image` 编码为底向上 CF_DIB；
- `ExportService::copyToClipboard` 正确转移 `HGLOBAL` 所有权；
- `ExportService::savePng` 使用 WIC；
- 空图、空路径和 PNG 输出已有自动测试；
- GUI 保存路径已通过 `ResultActionService` 统一处理；文件选择对话框由结果服务持有 owner window，带路径的 Save 走无对话框接口。

### 2.6 F5 Pin

- 无原生标题栏的置顶 `PinWindow`；
- 保持宽高比显示，支持拖动、边缘 / 四角等比缩放和关闭；
- 右键复制 / 保存使用各自 Pin 的 `Image`，不会串到最新 `ResultStore`；
- 多 Pin、关闭全部、数量统计与生命周期清理；
- 新 Pin 从虚拟桌面右侧开始自动寻找不重叠位置；
- `CaptureGuard` 支持嵌套隐藏 / 恢复，普通截图和背景快照均排除 Pin；
- 自动测试覆盖空图拒绝、多窗口、析构清理和不重叠排布。

### 2.7 F6 应用 profile 长截图

已实现：

- `LongShotProfile` / `LongShotProfileRegistry` 将应用识别、滚动控件、滚动输入和滚动状态查询与通用截图拼接循环分离；
- 记事本、文件资源管理器和 Chromium profile 已接入；资源管理器按选区定位 `DirectUIHWND`、`SysListView32` 或 `SysTreeView32`，浏览器按 Chromium 窗口类定位内容区，不向无关窗口广播滚轮；

- `LongShotRequest` 固定使用用户选中的物理像素矩形；
- CaptureWorkflow 在 Overlay 前记录原前台顶层窗口；
- Notepad / Explorer / Chromium profile 校验目标窗口、选区所在内容控件、内容区和滚动状态；
- 首帧、滚动后帧、重叠查找与追加拼接；
- 到底、无新增内容和最大 30000 像素停止；总时长、同位置重采、滚动状态轮询和工作内存也有独立预算，帧数与滚轮输入次数不设硬上限；
- 尺寸与工作集计算使用检查算术，预计拼接峰值超限时在新输出分配前停止，并保留上一可靠图像；
- `LongShotOutcome` 记录具体预算原因、首帧/总耗时、最慢帧、峰值工作内存与预览发布次数；
- `LongShotController` 管理长截图 worker、暂停 / 停止 token 与 UI 线程完成消息；
- Overlay 保持选区孔洞透传并显示累计预览；
- 暂停 / 继续、停止，以及暂停后选择复制 / 保存 / Pin 的收尾行为；
- 失败或预算结束时进入非模态恢复；可靠部分需用户确认后发布，取消不发布，避免常驻进程保留上一张大图。

尚未完成：

- 记事本真实长文手工闭环记录；
- 资源管理器真实长文手工闭环记录；
- 三应用完整验收。
- Qt / Electron 等未知应用使用通用回退的真实桌面记录，以及混合 DPI、多屏、权限边界和干扰场景验收。

---

## 3. 未实现或仍为 Stub

### F3 标注后续（不是 Stub）

- 标注后的 Copy / Save / Pin / 再编辑代码回流已接通，仍需真实窗口与混合 DPI 联合验收；
- 重做按钮与 `Ctrl+Y` / `Ctrl+Shift+Z` 已补；仍需真实窗口与混合 DPI 联合验收；
- 就地标注与八点调区 / 多屏的组合冒烟待验收。

### F8 本地口令

- `CommandParser::tryParse` 已支持窗口截取、中心裁剪、复制、钉图、状态与保存；窗口截取会剥离“并复制 / 并保存 / 并钉图”后缀，避免把后续动作误作窗口标题；
- 仍没有 GUI 口令输入、歧义交互或多动作调度入口。

兼容路径说明：

- `LegacyActionRequest` 与 `adaptLegacyActionRequest()` 已移入 `qingying_action_compatibility`，头文件位于 `include/qingying/action/compatibility/`，实现位于 `src/action/compatibility/`；两者只用于迁移/兼容回归测试，均标记为 deprecated；
- 新代码使用 `ActionRequest` + typed payload；删除条件是支持调用方不再 include 兼容头并删除兼容回归测试，owner 为 action/automation 维护者；
- `CaptureEngine::captureWindow` 与 `cropCenter` 仍返回 `kNotImplemented`，这是旧底层接口；
- 现行 `CaptureWindow` / `CropCenter` 已由 `CaptureService` 完成窗口解析、重验证、物理矩形计算和结果发布，并由 Action Handler 注册。

### F9 MCP

- `McpBridge` 已转发 MCP protocol session；
- `stdio_transport`、Named Pipe server/client、wire codec、peer identity、operation 查询 / 取消 / release_result 和 tool catalog 已实现；
- `capture_window`、`crop_center`、`copy`、`save`、`pin` 已进入 AutomationEndpoint / Action 主链；
- 仍需补发布包插件信任校验、真实客户端/安装包验收、阻塞 provider 和关闭竞态验收。

---

## 4. 自动验证基线

当前基线命令：

```bat
build.bat Release test
```

2026-09-20 移除帧数与输入次数硬上限后的原有长截图定向测试 34/34 通过；随后新增连续接受 31 帧和发送 65 次滚轮输入两项回归，两项均通过；`build.bat Release test` 的 Release 配置、编译和链接成功，1047/1047 个测试通过（47.02 秒）。
`build.bat Release test` 是当前验证入口。测试源和生产 Handler 已通过同一 CMake target 接入。

自动测试不能替代：

- 双屏与混合 DPI 的实际视觉 / 输入验证；
- F2 在真实应用上的候选边界；
- 多 Pin 的交互和隐藏 / 恢复闪烁；
- 真实长文滚动拼接；
- 就地标注与选区 Overlay 的组合验收（当前非模态状态机已接线，仍需真实窗口验证）。
- 插件目录篡改拒绝、线程阻塞关闭、UIA/MSAA provider 阻塞，以及包含驱动/DWM/目标应用自身缓存的进程级内存峰值。

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
| 2026-08-31 | SelectionToolbar / OverlayPhase、编辑源图上移、标注结果操作回流 | `a5f4cb97` |
| 2026-08-31 | PIMPL RAII 与 OverlayRenderer 拆分 | `3e8f854e`、`8c3166ed` |
| 2026-08-31 | CaptureWorkflow 收口交互编排，Application 回归组合根 | `778cd336` |
| 2026-08-31 | LongShotController 收口 worker、控制 token 与完成回收 | `adeb7c60` |
| 2026-08-31 | Selection / Annotation Overlay 非模态化，顶层消息泵续接 Workflow | `adeb7c60` |
| 2026-09-03 | ResultStore / ResultActionService、失败契约、消息所有权与 Overlay 载荷释放 | `1e3a7318`、`c7d40d21`、`2f63c92b`、`667563d0`、`65eb2c52` |
| 2026-09-03 | AnnotationEditorHost 组件拆分 | `57004322` |
| 2026-09-04 | 类型化 Action、结果选择、中立坐标与编译边界收口 | `ddc00c23`、`7e61cc07`、`6892a0bf` |
| 2026-09-04 | Notepad / Explorer / Chromium 长截图插件与拼接稳定性 | `ce8fad57`、`4a1ed058` |
| 2026-09-04 | 截图结果生命周期收口，空闲时释放像素 | `40d22e21` |
| 2026-09-09～14 | SmartRegion UIA/MSAA/视觉候选、异步 latest-wins、浏览器外壳/Tab/临时弹窗与诊断回归 | `6446d60a`～`a21f2ac4` |

---

## 6. 技术债与架构偏差

- `selection_overlay.cpp` 当前约 1,500 行；选区工具栏、阶段状态、遮罩 / 预览渲染和标注捕获 / 编辑职责已有拆分，但窗口消息、交互状态和 SmartRegion 结果合并仍集中在同一 Win32 壳中；
- `SelectionToolbar` 已以 PIMPL + `unique_ptr` 独立管理 HWND，`OverlayPhase` 已替代选区 / 长截图 / 关闭阶段的互斥布尔组合；
- 标注窗口、AnnotationEditorHost、工具栏和渲染职责已有拆分，窗口消息现由主循环驱动；后续重点是收紧 UI 状态 owner，而不是继续堆叠回调；
- `SelectionOverlay::show` 与 `AnnotationOverlay::showInPlace` 均创建后立即返回，Workflow 以 `WM_QINGYING_WORKFLOW_CONTINUE` 续接状态；
- `Application` 负责依赖组装、托盘 / 热键、消息转发和运行时关闭；`CaptureWorkflow` 已接管选区 → 标注 → 结果操作条的阶段状态；`LongShotController` 独立管理长截图运行态；
- `LongShotRegion` 枚举存在但没有 Handler；
- `qingying_overlay` 已移除对 `qingying_capture` / `qingying_annotate` 的链接；编辑源图由 CaptureWorkflow 在 SelectionOverlay 返回后产生；
- `CaptureEngine`、`LongShotEngine`、`LongShotController`、`McpBridge` 已改为 `std::unique_ptr<Impl>`；`OverlayRenderer` 已接收不可变渲染状态并提供离屏像素合成；`LongShotController` 已集中跨线程预览 / 完成消息生命周期，两个 Overlay 的窗口状态也已由顶层消息泵收口；
- `ActionRequest` 的类型化 payload、请求 / 操作 ID、取消、超时和结果选择已完成；`ActionResult` 已有类型化 output，F9 通过作用域、ResultId 和 opaque handle 管理外部结果；
- legacy action 已隔离到 `qingying_action_compatibility`；新业务 target 不包含兼容头，旧 API 的替代 API、删除条件和 owner 已写入架构文档；
- `ResultStore` 仍是每个 scope 一个 current 槽位，`CaptureSession` 是兼容门面；后续需要把 retained、in-flight、preview、编码和 wire copy 的内存峰值纳入统一预算；
- `McpBridge`、Named Pipe、UI 调度、WindowResolver 和外部操作注册表已接线；`CaptureWindow` / `CropCenter` 由 `CaptureService` 实现，底层 `CaptureEngine` 兼容方法仍保留 Stub；
- SmartRegion 的 UIA/MSAA、视觉定位和异步查询已形成较完整链路，但重路径仍有 UI 线程同步执行，且实现源码暂编入 overlay target；
- LongShotPluginHost 当前会扫描并加载插件目录 DLL，发布版的 manifest、签名/哈希和目录 ACL 信任校验尚未在宿主代码中强制；
- `Application` 已由 `ApplicationShutdownCoordinator` 按四阶段统一编排关闭，并为参与者记录耗时、预算超时和本地快照；LongShotController、ExportExecutor、UIA query worker 和 PipeServer 已改为可截止等待并保留超时上下文，LongShot 插件已有可选取消信号；阻塞式旧 ABI 插件、导出回调和 UIA 查询故障注入已覆盖，PipeAutomationClient 的 detached reader、WM_ENDSESSION 和真实桌面关机验收仍待收口。
- 长截图的 Notepad / Explorer / Chromium 插件代码已落地，真实 Chrome / Edge / Brave 窗口验收仍缺。

整改方案见 [架构如何调整.md](架构如何调整.md)。

---

## 7. 下一步建议

1. 完成插件信任校验、PipeAutomationClient reader 所有权、WM_ENDSESSION 与真实桌面关闭验收，再收口 UI 重路径隔离；
2. 完成 SmartRegion target 边界、真实浏览器/编辑器和混合 DPI 验收；
3. 修复干净构建目录的 CTest discovery，再接入四层异步/进程/桌面测试；
4. 完成 Notepad / Explorer / Chrome / Edge / Brave 真实长截图闭环记录；
5. 为 ResultStore、LongShot、Export 和 get/release 统一核算图像内存峰值；
6. 为标注编辑器补重做按钮，并记录 Copy / Save / Pin / 再编辑的完整 GUI 验收；
7. 在上述基础上收敛 ActionDescriptor 和 AutomationSession，并在兼容调用方迁移完成后删除 legacy action target。

---

本文随代码持续更新。提交标题用于定位，功能完成度最终以源码、自动测试和人工验收记录共同判定。
