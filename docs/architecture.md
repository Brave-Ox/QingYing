# 轻映 QingYing — 当前架构说明（方案 B）

> 状态：以当前代码为准
> 基线：`master` / `b91dbe21`
> 同步日期：2026-08-31
> 形态：**一个 EXE + 10 个 static lib**

关联文档：

- [开发清单](../轻映-QingYing-开发清单.md)
- [开发进度](PROGRESS.md)
- [架构如何调整](架构如何调整.md)
- [系统架构与耦合度分析](系统架构与耦合度分析.md)

---

## 1. 目标与非目标

### 目标

- Windows 绿色单文件，C++17 + Win32，不使用 Qt / Electron；
- 主路径：热键 → 框选 / 吸附 / 调区 → 标注 → 复制 / 保存；
- 目标指标：体积 ≤ 20 MB，常驻 ≤ 40 MB，热键到遮罩 ≤ 300 ms；
- GUI、本地口令与 MCP 复用同一套本地能力，不实现第二套截图引擎；
- 图片统一使用 BGRA32、行优先、物理像素的 `Image` 值对象。

### 非目标

- 不做 WPS / Qt 式插件 DLL 或多进程宿主架构；
- 不让 MCP / 口令直接调用 GDI、DXGI 或模块私有实现；
- 不把模型装入安装包，F1～F7 不依赖网络；
- 不支持任意应用长截图或通用桌面自动化。

---

## 2. 当前总体结构

```text
┌──────────────────────────────────────────────────────┐
│ qingying.exe / src/app                               │
│ 单实例、托盘、全局热键、顶层消息循环、交互工作流      │
└───────────────┬──────────────────────┬───────────────┘
                │ 单步命令             │ 多步 GUI 工作流
                ▼                      ▼
┌────────────────────────────┐   ┌──────────────────────┐
│ qingying_action            │   │ SelectionOverlay     │
│ Request / Result /         │   │ LongShot worker      │
│ Dispatcher / Handler       │   │ Annotation（已接线） │
└──────────────┬─────────────┘   └──────────┬───────────┘
               │                            │
      ┌────────┼────────┐          ┌────────┴─────────┐
      ▼        ▼        ▼          ▼                  ▼
   capture   export    pin      longshot           annotate
```

当前架构不是“所有 GUI 细节都必须变成 Action”。正确边界是：

1. `ActionDispatcher` 负责可复用的单步业务命令；
2. `Application` 负责选区、标注、交互式长截图等多步工作流；
3. 未来 `command` / `mcp` 只能进入 Dispatcher 或受控 Workflow，不能直接调用引擎；
4. UI 背景快照、文件选择框、长截图预览等表现层行为可以留在工作流，但必须明确，不应写成“全部已经经过 Dispatcher”。

---

## 3. CMake 目标与实现状态

| 目标 | 类型 | 当前职责 | 当前状态 |
|---|---|---|---|
| `qingying_action` | static | Action 契约、Dispatcher、Handler 接口 | 已实现 |
| `qingying_capture` | static | GDI 区域截图；DXGI / 截窗 / 中央裁切接口位置 | 区域截图已实现，其余为桩 |
| `qingying_export` | static | CF_DIB 剪贴板与 WIC PNG | 已实现 |
| `qingying_ui` | static | 选区条 / 标注底栏共用的 ModernToolbar、GDI+ 与 SVG 路径图标 | 已实现 |
| `qingying_overlay` | static | 桌面快照遮罩、框选、调区、窗口吸附、ModernToolbar、长截图预览、就地标注入口 | 已实现但文件职责偏重 |
| `qingying_annotate` | static | 标注文档、引擎、渲染器、编辑会话和编辑 Overlay | 代码已接线，窗口冒烟仍单列 |
| `qingying_pin` | static | 多 Pin、排布、缩放、独立导出、捕获排除 | 代码基本完成 |
| `qingying_longshot` | static | 记事本选区滚动、拼接和停止条件 | 记事本路径已实现 |
| `qingying_command` | static | 本地口令 → Action | Stub |
| `qingying_mcp` | static | MCP Bridge / 后续 Named Pipe | Stub |
| `qingying` | EXE | 组合根、工作流、托盘、热键和线程收口 | 已实现并持续增长 |

目录：

```text
include/qingying/          对外契约
src/app/                   EXE 与组合根
src/action/                命令分发
src/capture/               屏幕捕获
src/export/                剪贴板 / PNG
src/overlay/               选区 UI
src/window/                窗口检测源码（当前编入 overlay target）
src/annotate/              标注
src/pin/                   钉图
src/longshot/              长截图
src/command/               本地口令
src/mcp/                   MCP
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
action + capture ← longshot
action + capture + annotate + ui ← overlay   （CMake：capture 为公开链接，annotate/ui 为私有链接）
ui        （ModernToolbar 独立于 action，由 app/targets 组合）

app → 上述所有模块（Composition Root）
```

约束：

- `command` / `mcp` 只能依赖稳定契约，不得 include DXGI、GDI 或 `*Impl`；
- 引擎模块不得反向依赖 `app`；
- 跨模块不 include 对方 `.cpp` 旁的私有头；
- `window_detector.cpp` 目前编入 `qingying_overlay`，需要被 F6/F8 复用时再拆 `qingying_window`；
- `qingying_overlay` 现在确实在就地编辑前直接使用 `CaptureEngine` 重新抓取选区；若改为由 Application 传入源图，再考虑删除该公开链接。

---

## 5. Action 契约

### 5.1 当前注册状态

| ActionType | 含义 | Handler 状态 | 数据去向 |
|---|---|---|---|
| `Status` | 查询进程能力 | 已注册 | 直接返回 `ActionResult` |
| `CaptureRegion` | 按物理像素矩形截图 | 已注册 | 成功后写 `CaptureSession` |
| `Copy` | 当前结果写剪贴板 | 已注册 | 从 `CaptureSession` 取图 |
| `Save` | 当前结果保存 PNG | 已注册 | 要求 `save_path`，从 Session 取图 |
| `Pin` | 当前结果钉图 | 已注册 | 从 Session 取图交给 `PinManager` |
| `CaptureWindow` | 按名称 / 句柄截窗 | 未注册；Capture 方法为桩 | F8/F9 待实现 |
| `CropCenter` | 中央裁切 | 未注册；Capture 方法为桩 | F8/F9 待实现 |
| `LongShotRegion` | 长截图动作占位 | 未注册 | 当前 GUI 直接由 Application 编排 |
| `SuggestName` | 可选命名能力 | 未实现 | P4 |

### 5.2 契约约定

- 捕获矩形一律为物理屏幕像素；
- `Image` 为 BGRA32、行优先；
- `ActionResult.ok == false` 时必须给稳定 `error_code` 和可读 `message`；
- `ActionResult.data` 只放轻量 UTF-8 结果，不传像素；
- `CaptureSession` 当前承载“最近一张有效结果”的 UI 语义；
- 失败不得用空图覆盖上一张有效长截图结果。

`ActionRequest` 目前是包含所有动作字段的统一结构体。F8/F9 扩展前应迁移为类型安全 payload，见 [架构如何调整](架构如何调整.md)。

---

## 6. 普通截图工作流

```text
Ctrl+Shift+Q
  → HotkeyManager 向托盘窗口投递 WM_QINGYING_BEGIN_CAPTURE
  → Application 记录原前台顶层窗口（供长截图）
  → PinManager::CaptureGuard 临时隐藏可见 Pin
  → CaptureEngine 直接抓取虚拟桌面，生成 Overlay 背景快照
  → SelectionOverlay::show(background, callback)
  → 用户自由框选 / 窗口吸附 / 八点调区
  → ModernToolbar（Copy / Save / LongShot / Edit / Pin）
  → SelectionResult（物理像素 + SelectionAction）
  → Copy / Save / Pin：dispatch(CaptureRegion) → CaptureSession → 消费结果
  → Edit：隐藏遮罩 → Overlay 直接 CaptureEngine 重抓选区
           → AnnotationOverlay（就地）→ rendered Image → Application 写回 Session 并自动 Copy
```

说明：

- 桌面背景快照是 UI 表现层输入，因此当前直接调用 `CaptureEngine`，不会写 `CaptureSession`；
- 普通保存路径当前由 `Application::saveImage` 直接调用 `ExportService`，尚未完全复用 `SaveHandler`；
- 操作条“编辑”按钮已启用并接入 `SelectionAction::Edit`；当前标注完成后写回 Session 并自动 Copy，尚未返回完整 Save / Pin 操作条；
- 选区条和标注底栏共用白色圆角 `ModernToolbar`，悬停 / 选中使用浅灰状态，图标含 SVG 路径实现；
- `AnnotationOverlay` 的源图在就地编辑时钉在选区左上角，编辑期间父遮罩冻结显示；
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

---

## 8. F5 Pin 工作流

- `PinHandler` 从 `CaptureSession` 获取当前 `Image`；
- `PinManager` 为每张图创建独立 `PinWindow`；
- 新窗口从虚拟桌面右侧开始自动寻找不重叠位置；
- Pin 支持拖动、边缘 / 四角等比例缩放、关闭、右键复制和保存；
- `CaptureGuard` 在普通截图和背景快照前隐藏可见 Pin，结束后恢复并保持置顶；
- Pin 的独立复制 / 保存针对该窗口自身的 `Image`，不能错误读取最新 Session。

---

## 9. F6 交互式长截图

```text
Application 预先记录 owner_window
  → Overlay 产生固定物理像素选区
  → 用户点击“长截图”
  → Overlay 保留在屏幕上，选区孔洞切换为捕获透传
  → Application 启动 longshot worker
  → Notepad profile 校验 owner / 内容区 / 滚动目标
  → CaptureEngine 重复截取同一矩形
  → ImageStitcher 去重拼接
  → worker 向 Overlay 投递实时预览
  → 用户可暂停 / 继续 / 停止
  → 完成消息回到托盘 UI 线程
  → 成功图写入 CaptureSession 并自动 dispatch(Copy)
```

当前停止条件：

- 已到垂直滚动条底部；
- 新帧与累计图完全重合 / 没有新增内容；
- 达到 `max_frames = 30`；
- 达到 `max_output_height = 30000`；
- 用户停止或应用退出；
- 目标窗口、选区或捕获失败。

当前只实现记事本 profile；资源管理器和 Edge 尚未实现。

---

## 10. 线程、消息循环与生命周期

| 项 | 当前实现 |
|---|---|
| 顶层进程消息循环 | `Application::run()` 中的 `GetMessage` |
| Overlay | `SelectionOverlay::show()` 当前另有同线程嵌套模态 `GetMessage` 循环 |
| 标注编辑器 | `AnnotationOverlay::showInPlace()` 也采用同线程嵌套模态 `GetMessage` 循环 |
| 长截图 | `Application` 创建 worker thread；暂停 / 停止使用 atomic 标志 |
| 跨线程预览 | worker 复制预览图后 `PostMessage` 给 Overlay |
| 完成回收 | worker 将 `LongShotCompletion*` 投递给托盘窗口，UI 线程接管并 join |
| 应用退出 | 设置 stop、隐藏 Overlay、保留并重新投递 `WM_QUIT`，避免嵌套循环残留 |
| 单实例 | Named Mutex |

“Win32 消息循环只在 app”目前只对顶层循环成立；Overlay 的嵌套循环是已知偏差，后续应改为非模态窗口状态机。

---

## 11. PIMPL 与资源管理

| 类 | 当前形式 | 调整方向 |
|---|---|---|
| `CaptureEngine` | `Impl*` + 手写 new/delete | `std::unique_ptr<Impl>` |
| `LongShotEngine` | `Impl*` + 手写 new/delete | `std::unique_ptr<Impl>` |
| `McpBridge` | `Impl*` + 手写 new/delete，且为 Stub | F9 实现时一并 RAII 化 |
| `ActionDispatcher` / Request / Result | 透明契约 | 不使用 PIMPL |
| GDI 资源 | 多数为 unique_ptr + 自定义 deleter | 保持 |
| Pin 隐藏恢复 | RAII `CaptureGuard` | 保持 |

PIMPL 只作为模块级编译防火墙，不给每个小类型套 `Impl`。

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
| F1 自定义区域 | overlay → app → action → capture | 代码完成，人工混合 DPI 待验收 |
| F2 窗口吸附 | window + overlay | 代码完成，真实应用待验收 |
| F3 标注 | annotate + AnnotationOverlay + overlay + app | 代码已接线，六类工具与撤销可用；重做 UI、Save / Pin 回流和人工验收待补 |
| F4 导出 | export + action | 已实现 |
| F5 钉图 | pin + action | 代码基本完成，人工验收待做 |
| F6 长截图 | app + overlay + longshot + capture | 记事本路径已接入；另外两应用未实现 |
| F7 托盘热键 | app | 已实现 |
| F8 口令 | command → action / workflow | Stub |
| F9 MCP | mcp → action / workflow | Stub |

---

## 14. 质量基线

- 2026-08-31：`build.bat Release test` 成功；
- CTest 发现 252 个用例，实际执行 251 个且全部通过；`AnnotationOverlayTest.DISABLED_SmokeConfirmReturnsSourceCopy` 显式禁用；
- Release EXE：212,480 字节；
- 自动测试覆盖 Action、区域捕获、Session、导出、F1、F2 基础过滤、F3 文档 / 引擎 / 渲染 / 编辑器布局、ModernToolbar、Pin、拼接、记事本 profile 和长截图停止条件；
- 未被自动测试替代的项目：真实混合 DPI、窗口视觉交互、多 Pin 体验、真实记事本长截、资源管理器 / Edge 长截、内存与唤起时延。

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
