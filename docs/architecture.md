# 轻映 QingYing — 当前架构说明（方案 B）

> 状态：以当前代码为准
> 提交基线：`a21f2ac4b4e04f8f777daccb4d355146fee9b069`
> 同步日期：2026-09-14
> 形态：**一个 EXE + 分层 static lib / interface target + 3 个受控 longshot plugin DLL**

关联文档：

- [开发清单](../轻映-QingYing-开发清单.md)
- [开发进度](PROGRESS.md)
- [项目理解](项目理解.md)
- [架构如何调整](架构如何调整.md)
- [系统架构与耦合度分析](系统架构与耦合度分析.md)

---

## 1. 目标与非目标

### 目标

- Windows 绿色交付包，C++17 + Win32，不使用 Qt / Electron；当前包由 EXE 和受控的长截图插件 DLL 组成；
- 主路径：热键 → 框选 / 吸附 / 调区 → 标注 → 复制 / 保存；
- 目标指标：体积 ≤ 20 MB，常驻 ≤ 40 MB，热键到遮罩 ≤ 300 ms；
- GUI、本地口令与 MCP 复用同一套本地能力，不实现第二套截图引擎；
- 图片统一使用 BGRA32、行优先、物理像素的 `Image` 值对象。

### 非目标

- 不做 WPS / Qt 式任意插件宿主或多进程业务架构；长截图只通过受控 ABI 加载内置 profile DLL；
- 不让 MCP / 口令直接调用 GDI、DXGI 或模块私有实现；
- 不把模型装入安装包，F1～F7 不依赖网络；
- 不支持任意应用长截图或通用桌面自动化。

---

## 2. 当前总体结构

```text
┌──────────────────────────────────────────────────────┐
│ qingying.exe / Application                           │
│ 单实例、托盘、全局热键、顶层消息循环、依赖组装         │
└───────────────┬──────────────────────┬───────────────┘
                │ 单步命令             │ 多步 GUI 工作流
                ▼                      ▼
┌────────────────────────────┐   ┌──────────────────────────┐
│ qingying_action            │   │ qingying_workflow        │
│ Request / Result /         │   │ CaptureWorkflow         │
│ Dispatcher / Handler       │   │ Selection/Annotate/F6   │
└──────────────┬─────────────┘   └────────────┬─────────────┘
               │                            │
      ┌────────┼────────┐          ┌────────┴─────────┐
      ▼        ▼        ▼          ▼                  ▼
   capture   export    pin      longshot           annotate
```

当前 HEAD 的实际接入还包括：qingying_window 提供基础窗口目录、检测和解析；qingying_automation_contract 提供中立自动化契约；qingying_automation 负责操作注册、UI 调度和 endpoint；qingying_ipc 提供 Named Pipe；qingying_mcp 提供 MCP 会话、stdio transport 和工具目录；qingying_app_runtime 在 app 组合根中连接这些接入层。qingying_overlay 当前仍直接编译 SmartRegion 的多份 window 实现源文件，这是已确认的结构性技术债。

当前架构不是“所有 GUI 细节都必须变成 Action”。正确边界是：

1. `ActionDispatcher` 负责可复用的单步业务命令；
2. `CaptureWorkflow` 负责选区、标注和结果动作，`LongShotController` 负责交互式长截图的异步生命周期，`Application` 只负责组合与消息转发；
3. 未来 `command` / `mcp` 只能进入 Dispatcher 或受控 Workflow，不能直接调用引擎；
4. UI 背景快照、文件选择框、长截图预览等表现层行为可以留在工作流，但必须明确，不应写成“全部已经经过 Dispatcher”。

---

## 3. CMake 目标与实现状态

| 目标 | 类型 | 当前职责 | 当前状态 |
|---|---|---|---|
| `qingying_action` | static | Action 契约、Dispatcher、Handler 接口 | 已实现 |
| `qingying_action_compatibility` | static | 旧 all-fields 请求到类型化 ActionRequest 的迁移适配器 | 兼容层；仅测试/迁移使用，生产 target 禁止依赖 |
| `qingying_window` | static | 顶层窗口目录、检测、解析与候选基础过滤 | 已实现；SmartRegion 实现暂编入 overlay |
| `qingying_capture` | static | GDI 区域截图；CaptureWindow / CropCenter 的底层兼容接口 | 区域截图已实现；底层兼容方法仍为桩，现行入口由 CaptureService 实现 |
| `qingying_automation_contract` | interface | 中立 Action / operation / automation wire 契约 | 已实现 |
| `qingying_automation` | static | Operation registry、UIActionScheduler、AutomationEndpoint | 已实现；当前私有依赖 qingying_workflow，待进一步倒置 |
| `qingying_ipc` | static | Named Pipe、帧编解码、身份校验、客户端/服务端 | 已实现 |
| `qingying_export` | static | CF_DIB 剪贴板与 WIC PNG | 已实现 |
| `qingying_ui` | static | 选区条 / 标注底栏共用的 ModernToolbar、GDI+ 与 SVG 路径图标 | 已实现 |
| `qingying_overlay` | static | 桌面快照遮罩、框选、调区、窗口吸附、SelectionToolbar、OverlayPhase、OverlayRenderer、长截图预览 | SelectionOverlay 已非模态化；Renderer 已拆出 |
| `qingying_annotate` | static | 标注文档、引擎、渲染器、编辑会话和编辑 Overlay | 代码已接线，窗口冒烟仍单列 |
| `qingying_pin` | static | 多 Pin、排布、缩放、独立导出、捕获排除 | 代码基本完成 |
| `qingying_longshot` | static | 通用长截图运行时、profile 注册表、插件宿主与拼接 | Notepad / Explorer / Chromium profile 已接入；3 个 DLL 已部署，真实窗口验收待做 |
| `qingying_workflow` | static | 选区、标注、结果动作与交互式长截图编排 | CaptureWorkflow 状态机与 LongShotController 已接入 |
| `qingying_app_handlers` | static | 生产 Action Handler 与应用层结果动作适配 | 已从 EXE / 测试中独立出来 |
| `qingying_command` | static | 本地口令 → Action | Stub |
| `qingying_mcp` | static | MCP Bridge、协议 session、stdio transport、工具目录 | 代码已实现；真实安装包/客户端验收仍需补 |
| `qingying_app_runtime` | static | AutomationRuntime、MCP stdio runner、运行时设置 | 已接线 |
| `qingying` | EXE | 组合根、托盘、热键、顶层消息泵和 Workflow 消息转发 | 组合根已使用 PIMPL；F9 已接入本地 Pipe/MCP 主链 |

目录：

```text
include/qingying/          对外契约
src/app/                   EXE 与组合根
src/action/                命令分发
src/action/compatibility/  旧 Action 请求迁移适配器（非现行主路径）
src/capture/               屏幕捕获
src/export/                剪贴板 / PNG
src/overlay/               选区 UI
src/window/                基础窗口检测；SmartRegion 实现当前暂编入 overlay target
src/annotate/              标注
src/pin/                   钉图
src/longshot/              长截图
src/command/               本地口令
src/mcp/                   MCP 协议、工具目录和 stdio bridge
plugins/longshot/          Notepad / Explorer / Chromium profile DLL
```

---

## 4. 当前依赖方向

```text
action    ← capture
action    ← export
action    ← pin
action    ← annotate
action    ← command
action    ← mcp
qingying_action_compatibility → qingying_action（仅测试/迁移 target）
automation_contract ← automation
workflow  ← automation（当前具体依赖，待倒置）
automation_contract ← ipc
automation_contract + ipc ← mcp
automation + ipc + mcp ← app_runtime
action + capture ← longshot
action + ui ← overlay   （CMake：overlay 已不链接 capture / annotate）
ui        （ModernToolbar 独立于 action，由 app/targets 组合）
action + overlay + capture + annotate + export + pin + longshot ← workflow
longshot runtime ← controlled profile plugin DLLs

app → workflow + 上述服务 + app_runtime（Composition Root）
```

约束：

- `command` / `mcp` 只能依赖稳定契约，不得 include DXGI、GDI 或 `*Impl`；
- `qingying_action` 不包含 `LegacyActionRequest`；兼容 target 只能被显式的迁移调用方或兼容回归测试链接；
- 引擎模块不得反向依赖 `app`；
- 跨模块不 include 对方 `.cpp` 旁的私有头；
- `window_detector.cpp`、`window_catalog.cpp` 和 `window_resolver.cpp` 已在 `qingying_window`；SmartRegion 的定位器和查询 worker 当前仍编入 `qingying_overlay`，应后续建立独立 target；
- 编辑源图已由 `CaptureWorkflow` 在 `SelectionOverlay` 返回 Edit 意图后抓取；`qingying_overlay` 不再 include 或链接 capture / annotate。
- 长截图 profile 只能通过 `LongShotPluginHost` 的受控 ABI 接入，插件不得反向依赖 `app` 或 GUI 实现。
- 当前没有发现 CMake target 环，但 `qingying_automation` 私有依赖 `qingying_workflow`，属于层次反向依赖；后续应通过 `ActionExecutor`/应用适配器解除。

---

## 5. Action 契约

### 5.1 当前注册状态

| ActionType | 含义 | Handler 状态 | 数据去向 |
|---|---|---|---|
| `Status` | 查询进程能力 | 已注册 | 直接返回 `ActionResult` |
| `CaptureRegion` | 按物理像素矩形截图 | 已注册 | 成功后发布到 `ResultStore` |
| `Copy` | 当前结果写剪贴板 | 已注册 | 通过 `ResultActionService` 从 `ResultStore` 取图 |
| `Save` | 当前结果保存 PNG | 已注册 | 要求 `save_path`，通过 `ResultActionService` 取图 |
| `Pin` | 当前结果钉图 | 已注册 | 通过 `ResultActionService` 取图交给 `PinManager` |
| `CaptureWindow` | 按名称 / 进程 ID 截窗 | 已注册；由 CaptureService 解析并复核窗口 | 成功后发布到作用域 ResultStore |
| `CropCenter` | 主显示器中央裁切 | 已注册；由 CaptureService 计算物理像素矩形 | 成功后发布到作用域 ResultStore |
| `LongShotRegion` | 长截图动作占位 | 未注册 | 当前 GUI 由 CaptureWorkflow 编排 |
| `SuggestName` | 可选命名能力 | 未实现 | P4 |

普通 CaptureRegion/CaptureWindow/CropCenter 的 Dispatcher submit 和 CaptureWorkflow 使用专用 CaptureExecutor。UI 保留准入、pin 排除、窗口状态与 ResultStore 发布；窗口目录查询、GDI 和预览合成在后台，取消/deadline 丢弃结果，关闭采用共享截止时间。GUI 对话框后由 ExportExecutor 持有结果 lease 并编码 PNG，完成通过 token 消息回到 UI；同步捕获方法仅作为兼容入口保留。

### 5.2 契约约定

- 捕获矩形一律为物理屏幕像素；
- `Image` 为 BGRA32、行优先；
- `ActionResult.ok == false` 时必须给稳定 `error_code` 和可读 `message`；
- `ActionResult.data` 只放轻量 UTF-8 结果，不传像素；
- `ResultStore` 当前承载“最近一张有效结果”的 UI 语义；`CaptureSession` 仅作为兼容门面保留；
- 新截图开始时释放上一张结果；失败后不恢复旧结果，避免常驻进程长期持有大块像素缓冲。

`ActionRequest` 已使用 `std::variant` 类型化 payload，并带 request / operation id、取消、超时和 `ResultSelection`。F9 的本地 Pipe/MCP 主链已接入，仍需继续验证异步操作上下文、类型化输出、外部结果租约和安装包边界。

---

### 5.3 兼容层边界与迁移规则

`LegacyActionRequest` 和 `adaptLegacyActionRequest()` 位于
`include/qingying/action/compatibility/`，实现位于
`src/action/compatibility/`，由 `qingying_action_compatibility` 单独编译。它们只保留旧调用方的字段到类型化 payload 的映射，不是新的业务分派路径。

| legacy API | 保留原因 | 替代 API | 删除条件 | owner |
|---|---|---|---|---|
| `LegacyActionRequest` | 迁移旧的 all-fields 调用方 | `ActionRequest` + `makeActionRequest()` | 支持调用方不再 include 兼容头，兼容回归测试删除 | action/automation 维护者 |
| `adaptLegacyActionRequest()` | 保留旧字段映射的单一转换点 | 直接构造对应 typed payload | 无调用方依赖，且不再需要旧字段兼容 | action/automation 维护者 |

两个 API 都标记为编译期 deprecated。新的 GUI、workflow、automation、MCP 和插件代码不得 include 兼容头；需要新增动作时必须直接使用类型化 payload。

---

## 6. 普通截图工作流

```text
Ctrl+Shift+Q
  → HotkeyManager 向托盘窗口投递 WM_QINGYING_BEGIN_CAPTURE
  → Application 转发给 CaptureWorkflow，由 Workflow 记录原前台顶层窗口
  → PinManager::CaptureGuard 临时隐藏可见 Pin
  → CaptureEngine 直接抓取虚拟桌面，生成 Overlay 背景快照
  → SelectionOverlay::show(background, callback[, initial_selection])
  → 用户自由框选 / 窗口吸附 / 八点调区
  → SelectionToolbar（Copy / Save / LongShot / Edit / Pin）
  → SelectionResult（物理像素 + SelectionAction）
  → Copy / Save / Pin：dispatch(CaptureRegion) → ResultStore → ResultActionService 消费结果
  → Edit：SelectionOverlay 返回 → CaptureWorkflow 捕获选区源图
  → AnnotationOverlay（非模态）→ rendered Image → 写回 ResultStore 并自动 Copy
           → composeCapturePreview → 恢复同一选区与结果操作条
           → Save / Pin / 再次 Edit
```

说明：

- 桌面背景快照是 UI 表现层输入，因此当前直接调用 `CaptureEngine`，不会写 `ResultStore`；
- GUI 保存路径已通过 `ResultActionService` 统一处理；文件选择对话框仍由结果服务持有 owner window，MCP 的无对话框保存走带路径的同步接口；
- 操作条“编辑”只上报 `SelectionAction::Edit`；标注完成后 CaptureWorkflow 写回 ResultStore、自动 Copy，并恢复完整 Save / Pin / Edit 结果操作条；
- 恢复结果操作条时选区保持只读，避免移动 / 缩放后让物理矩形与既有标注栅格失配；开始长截图会显式清除该标注结果；
- 选区条和标注底栏共用白色圆角 `ModernToolbar`，悬停 / 选中使用浅灰状态，图标含 SVG 路径实现；
- `SelectionOverlay` 和 `AnnotationOverlay` 均在创建后立即返回；完成 / 取消通过回调和 `WM_QINGYING_WORKFLOW_CONTINUE` 续接 CaptureWorkflow，确认时把标注栅格贴回新桌面快照，取消时不覆盖 ResultStore；
- Overlay 当前使用 `WS_EX_NOACTIVATE`，以避免遮罩出现后使原窗口的 owned popup 消失。

---

## 7. F2 窗口吸附

```text
鼠标位置
  → WindowDetector::detectAt
  → 顶层窗口提升和候选过滤
  → DWM 扩展边框矩形修正
  → Overlay 高亮
  → 点击后 SelectionController::setSelection
  → 仍可继续八点调整或重新自由框选
```

过滤范围包括 QingYing 自身、最小化窗口、工具窗口、DWM cloaked 窗口、桌面外壳和屏外 / 空矩形窗口。

浏览器所属且具有 WS_POPUP 的顶层临时弹窗只有在 owner root 为受支持的 Chrome / Edge / Brave 主窗口时才放行；其他应用工具窗口仍保持过滤。

### 7.1 SmartRegion 当前实现

SmartRegionDetector 采用“窗口级快速回退 + 已知内容定位 + 视觉候选 + UIA/MSAA 详细候选”的组合。内容/视觉与 UIA/MSAA 分别通过独立 latest-wins Discovery、Accessibility 通道（复用 UiaRegionQueryWorker）异步返回，SelectionOverlay 在 UI 线程校验 root HWND、矩形、generation 和结果年龄后再合并候选；手动框选优先于迟到的智能结果。VisualLocator 对 Electron 工作台、Chromium 外壳、小控件和弱边界有专用策略及有限缓存。

当前 target 边界仍需收敛：overlay 直接编译 SmartRegion 的 window 实现，见 P1-1。普通 hover 仅在 UI 读取有界轻量窗口快照，其他候选发现由后台执行，generation 随窗口上下文/会话变化；worker 仅将仍命中的近期局部结果重绑当前 request ID，保留原始 deadline。完成消息立即唤醒 UI，提交严格校验 request/generation、HWND/PID、矩形和 deadline；UI 合并两条通道结果，窗口回退不会重置局部候选稳定计时。无法中断 provider 的 deadline 只控制接纳/诊断，UI 仍保留窗口级回退。

---

## 8. F5 Pin 工作流

- `PinHandler` 通过 `ResultActionService` 从 `ResultStore` 获取当前 `Image`；
- `PinManager` 为每张图创建独立 `PinWindow`；
- 新窗口从虚拟桌面右侧开始自动寻找不重叠位置；
- Pin 支持拖动、边缘 / 四角等比例缩放、关闭、右键复制和保存；
- `CaptureGuard` 在普通截图和背景快照前隐藏可见 Pin，结束后恢复并保持置顶；
- Pin 的独立复制 / 保存针对该窗口自身的 `Image`，不能错误读取最新 `ResultStore`。

---

## 9. F6 交互式长截图

```text
CaptureWorkflow 预先记录 owner_window
  → Overlay 产生固定物理像素选区
  → 用户点击“长截图”
  → Overlay 保留在屏幕上，选区孔洞切换为捕获透传
  → LongShotController 启动 longshot worker
  → LongShotProfileRegistry 选择 Notepad / Explorer / Chromium profile，校验 owner / 内容区 / 滚动目标
  → CaptureEngine 重复截取同一矩形
  → ImageStitcher 去重拼接
  → worker 向 Overlay 投递实时预览
  → 用户可暂停 / 继续 / 停止
  → 完成消息回到托盘 UI 线程
  → 成功图写入 ResultStore 并自动 dispatch(Copy)
```

当前停止条件：

- 已到垂直滚动条底部；
- 新帧与累计图完全重合 / 没有新增内容；
- 达到 `max_frames = 30`；
- 达到 `max_output_height = 30000`；
- 用户停止或应用退出；
- 目标窗口、选区或捕获失败。

当前已有 Notepad、Explorer 和 Chromium profile 的插件代码；Chrome / Edge / Brave 真实窗口仍需人工验收，资源管理器也需完成真实窗口回归。

---

## 10. 线程、消息循环与生命周期

| 项 | 当前实现 |
|---|---|
| 顶层进程消息循环 | `Application::run()` 中的 `GetMessage` |
| Overlay | `SelectionOverlay::show()` 创建后立即返回，由 Application 顶层消息泵驱动 |
| 标注编辑器 | `AnnotationOverlay::showInPlace()` 创建后立即返回，键盘 / 鼠标消息在窗口过程处理 |
| 长截图 | `LongShotController` 创建单个 worker；暂停 / 停止使用 atomic 标志 |
| 跨线程预览 | worker 复制预览图后写入 `UiMessageChannel`，Windows 消息只携带 token |
| 完成回收 | `LongShotController` 将拥有 token 的完成消息交给 `UiMessageChannel`，UI 线程取出后接管并 join |
| Action 异步提交 | `ActionDispatcher::submit()` 负责准入与 at-most-once completion；`IAsyncActionHandler` 使用注入的 `ActionExecutor`，不把 worker 生命周期交给调用方 |
| SmartRegion 查询 | UI 线程提交 latest-wins 请求；UIA/MSAA worker 返回带 root/generation/age 的结果，Overlay 在 UI 线程丢弃迟到结果 |
| 自动化 / IPC | Pipe worker 负责传输，Application 消息泵 drain 后进入 AutomationEndpoint；MCP stdio 只做协议边界，不直接调用捕获引擎 |
| 应用退出 | `ApplicationShutdownCoordinator` 在组合根按四阶段停止接入、拒绝新任务、取消业务生产者并清理回调；Capture / Export / LongShot / Region / PipeServer 按共享 deadline 等待，超时保留应用对象图 |
| 单实例 | Named Mutex |

“Win32 消息循环只在 app”现已落实：Overlay 只负责窗口过程和生命周期回调，CaptureWorkflow 通过阶段状态机续接交互。

---

## 11. PIMPL 与资源管理

| 类 | 当前形式 | 调整方向 |
|---|---|---|
| `CaptureEngine` | `std::unique_ptr<Impl>` | 已完成；析构在 `.cpp` 定义 |
| `LongShotEngine` | `std::unique_ptr<Impl>` | 已完成；析构在 `.cpp` 定义 |
| `McpBridge` | `std::unique_ptr<Impl>`，外覆 MCP protocol session | 已完成；通过本地 Pipe 接入 automation，仍需补安装包和真实客户端验收 |
| `LongShotController` | `std::unique_ptr<Impl>` | 已完成；持有 worker 与消息生命周期 |
| `ActionDispatcher` / Request / Result | 透明契约 | 不使用 PIMPL |
| GDI 资源 | 多数为 unique_ptr + 自定义 deleter | 保持 |
| Pin 隐藏恢复 | RAII `CaptureGuard` | 保持 |

PIMPL 只作为模块级编译防火墙，不给每个小类型套 `Impl`。

当前需要优先补齐的不是更多 PIMPL，而是线程和资源的显式拥有关系：`ApplicationShutdownCoordinator` 已统一 stop/cancel/join 的调用顺序、共享 deadline 和诊断；LongShotController、ExportExecutor、UIA 查询 worker、PipeServer 已能在截止时间内返回或保留上下文，旧插件可通过可选 cancel 协作退出；阻塞式旧 ABI 插件、导出回调和 UIA 查询故障注入已覆盖，PipeAutomationClient 的 reader 所有权和 WM_ENDSESSION 验收仍需收口，结果图像则应把 retained 与 in-flight 峰值纳入同一预算。

---

## 12. 错误码

| code | 常量 | 含义 |
|---:|---|---|
| 0 | `kOk` | 成功 |
| 1 | `kUnknown` | 未知错误 |
| 10 | `kNotReady` | 未运行 / 未就绪 / 无当前图片 |
| 20 | `kInvalidArgument` | 参数非法 |
| 30 | `kCaptureFailed` | 捕获失败 |
| 40 | `kWindowNotFound` | 窗口未找到 |
| 41 | `kWindowAmbiguous` | 窗口匹配歧义 |
| 50 | `kLongShotUnsupported` | 长截不支持目标或选区 |
| 60 | `kExportFailed` | 保存 / 剪贴板失败 |
| 70 | `kCommandUnmatched` | 口令未匹配 |
| 100 | `kNotImplemented` | 尚未实现 |

---

## 13. 功能映射与完成度

| 功能 | 主要模块 | 当前状态 |
|---|---|---|
| F1 自定义区域 | overlay → workflow → action → capture | 代码完成，人工混合 DPI 待验收 |
| F2 窗口吸附 | window + overlay | 代码完成，真实应用待验收 |
| F3 标注 | workflow + capture + AnnotationOverlay + overlay | 六类工具与撤销可用，Copy / Save / Pin / 再编辑代码回流已接通；重做 UI 和人工验收待补 |
| F4 导出 | export + action | 已实现 |
| F5 钉图 | pin + action | 代码基本完成，人工验收待做 |
| F6 长截图 | workflow + overlay + longshot + capture | profile registry、Notepad / Explorer / Chromium 插件与 LongShotController 已接入；真实浏览器窗口待验收 |
| F7 托盘热键 | app | 已实现 |
| F8 口令 | command → action / workflow | Parser 仍为 Stub |
| F9 MCP | mcp → ipc → automation → action / workflow | 协议、Named Pipe、工具目录和 CaptureWindow / CropCenter / Copy / Save / Pin 主链已实现；真实安装包与桌面验收待补 |

---

## 14. 质量基线

- 2026-09-14：仓库静态检索约有 851 个 TEST / TEST_F 宏，覆盖 Action、CaptureWorkflow、ResultStore、导出、F1/F2、SmartRegion、UIA/MSAA 夹具、F3、Pin、长截图、IPC/MCP、进程级流程和关闭协调器；
- 2026-09-14 已通过 CMake 重新生成 CTest discovery，当前发现 851 个产品/单元用例；`build.bat Release test` 的 Release 编译和链接成功，851/851 个用例通过；
- 关闭协调器的阶段顺序、重复关闭、异常诊断、阻塞式旧 ABI 插件、导出回调、UIA 查询、截止等待和插件取消已有自动测试；仍未被自动测试替代的项目：真实混合 DPI、窗口视觉交互、多 Pin 体验、Notepad / Explorer / Chrome / Edge / Brave 真实长截、插件篡改拒绝、WM_ENDSESSION、真实桌面关闭和端到端内存峰值。

---

## 15. 修订记录与 Git 证据

| 日期 | 说明 | 代表提交 |
|---|---|---|
| 2026-08-23 | 确认方案 B，建立多 static lib 与架构文档 | `cd8361de` |
| 2026-08-24 | 接入 app、Action、Image、区域捕获与导出主链路 | `141d9707`～`9c12c132` |
| 2026-08-25 | 完成 F1 调区和 Pin 初版 | `c40de0da`～`0a5ad00e` |
| 2026-08-26 | 完成 F2 窗口吸附与边框修正 | `d1316dda`、`5de31936` |
| 2026-08-27 | 改为桌面截图背景遮罩，修复 owned popup 消失 | `615c2f2a`、`1c9ac4d0` |
| 2026-08-28 | 增加多 Pin 自动排布 | `d8998e02` |
| 2026-08-26～28 | 接通记事本 F6，并补预览、暂停停止与失败清理 | `b1ee85bf`～`68f0e740` |
| 2026-08-30 | 合并标注链路与 F1 / Pin / F6 | `d9f7c0dc` |
| 2026-08-31 | 选区 / 标注共用圆角 ModernToolbar 与 SVG 图标 | `b91dbe21` |
| 2026-08-31 | SelectionToolbar / OverlayPhase、编辑源图上移与结果操作回流 | `a5f4cb97` |
| 2026-08-31 | Capture / LongShot / MCP PIMPL 改为 `unique_ptr` | `3e8f854e` |
| 2026-08-31 | OverlayRenderer 拆出像素合成与分层窗口呈现 | `8c3166ed` |
| 2026-08-31 | CaptureWorkflow 收口交互编排，Application 回归组合根 | `778cd336` |
| 2026-08-31 | LongShotController 收口 worker、控制 token 与完成回收 | `adeb7c60` |
| 2026-09-03 | 结果动作服务、失败契约、消息所有权与 Overlay 载荷释放 | `1e3a7318`、`c7d40d21`、`2f63c92b`、`667563d0`、`65eb2c52` |
| 2026-09-03 | AnnotationEditorHost 组件拆分 | `57004322` |
| 2026-09-04 | 类型化 Action、结果选择、中立坐标与编译边界收口 | `ddc00c23`、`7e61cc07`、`6892a0bf` |
| 2026-09-04 | Notepad / Explorer / Chromium 长截图插件与拼接稳定性 | `ce8fad57`、`4a1ed058` |
| 2026-09-04 | 截图结果生命周期收口，空闲时释放像素 | `40d22e21` |
| 2026-09-09～14 | SmartRegion UIA/MSAA/视觉候选、异步 latest-wins、浏览器外壳/Tab/临时弹窗与诊断回归 | `6446d60a`～`a21f2ac4` |
