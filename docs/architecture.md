# 轻映 QingYing — 架构说明（方案 B）

> 状态：已确认  
> 形态：**一个 EXE + 多个 static lib**  
> 硬约束：① Win32 消息循环只在 `app`；② 业务能力只经 `ActionDispatcher`

关联文档：[开发清单](../轻映-QingYing-开发清单.md)

---

## 1. 目标与非目标

### 目标

- Windows 绿色单文件，C++17 + Win32（不用 Qt / Electron）
- 主路径：热键 → 框选 → 标注 → 复制/保存（约 15 秒可演示）
- 指标：体积 ≤ 20 MB，常驻 ≤ 40 MB，热键→遮罩 ≤ 300 ms
- GUI / 本地口令 / MCP 共用同一套本地截图能力

### 非目标（架构层）

- 不做插件式多 DLL 分发
- 不做第二套截图引擎给 MCP
- 模型不进安装包；主路径不依赖网络

---

## 2. 总体结构

```text
┌──────────────────────────────────────────────────┐
│  qingying.exe                                      │
│  src/app —— 单实例 / 托盘 / 热键 / Win32 消息循环   │
└─────────────────────┬────────────────────────────┘
                      │ 只调用 ActionDispatcher
                      ▼
┌──────────────────────────────────────────────────┐
│  qingying_action（稳定契约）                        │
│  ActionType / ActionRequest / ActionResult         │
│  ActionDispatcher + IActionHandler                 │
└──────┬──────────┬──────────┬──────────┬──────────┘
       ▼          ▼          ▼          ▼
   capture    overlay     annotate     pin
   longshot   export      command      mcp
```

**两条硬规则：**

1. 只有 `app` 拥有消息循环（`GetMessage` / 托盘 / `RegisterHotKey`）。
2. 业务能力必须注册为 Handler，经 `ActionDispatcher::dispatch` 执行；禁止 MCP/口令直接调用 DXGI。

---

## 3. 目录与 CMake 目标

```text
qingying/
  CMakeLists.txt
  docs/architecture.md
  include/qingying/          # 对外头文件（稳定接口）
  src/
    app/                     # EXE
    action/
    capture/
    overlay/
    annotate/
    pin/
    longshot/
    export/
    command/
    mcp/
```

| CMake 目标 | 类型 | 职责 | 建议 Owner |
|---|---|---|---|
| `qingying_action` | static | Action 契约、Dispatcher | 架构/集成 |
| `qingying_capture` | static | DXGI/GDI 捕获、DPI 物理像素 | 捕获 |
| `qingying_overlay` | static | 全屏遮罩、框选、调区、窗口吸附 | 交互 |
| `qingying_annotate` | static | 矢量标注 + 撤销 | 交互 |
| `qingying_pin` | static | 钉图窗口 | 增强 |
| `qingying_longshot` | static | 记事本/资源管理器/Edge 长截 | 增强 |
| `qingying_export` | static | 剪贴板 / 存 PNG | 导出 |
| `qingying_command` | static | 本地口令 → ActionRequest | Agent |
| `qingying_mcp` | static | Named Pipe + Tool ↔ Action | Agent |
| `qingying` | **EXE** | 组装、注册 Handler、托盘热键 | 架构/集成 |

### 允许的依赖方向

```text
app → action, overlay, command, mcp, …（组装用）
mcp / command → action          （禁止 → capture）
overlay → action + capture 公共头
annotate / pin / longshot / export → action（及各自需要的 Image 类型）
capture → （仅系统库 / 本模块 Impl）
```

**禁止：**

- `mcp` / `command` include DXGI 或 `*Impl.hpp`
- 引擎模块反向依赖 `app`
- 跨模块 include 对方 `.cpp` 旁的私有头（除本模块 `*_p.h`）

---

## 4. Action 契约（第一周冻结）

### 4.1 类型

| ActionType | 含义 | 主实现模块 | 阶段 |
|---|---|---|---|
| `Status` | 进程是否在托盘运行 | app / action | P0 |
| `CaptureRegion` | 按物理像素矩形截取 | capture | P0 |
| `CaptureWindow` | 按窗口名/句柄截取 | capture | P1/P3 |
| `CropCenter` | 裁画面中央 w×h | capture | P3 |
| `Copy` | 当前结果 → 剪贴板 | export | P0 |
| `Save` | 当前结果 → PNG | export | P0 |
| `Pin` | 当前结果钉桌面 | pin | P2 |
| `LongShotForeground` | 前台页长截（三应用） | longshot | P2 |
| `SuggestName` | 建议文件名（可选云） | command/mcp | P4 |

### 4.2 约定

- **坐标**：进入 Capture 的矩形一律为**物理像素**；Overlay 负责逻辑坐标 ↔ 物理像素换算。
- **调用模型**：P0～P3 默认**同步** `dispatch`；长截若阻塞 UI，后续再加“进行中”状态，不先上线程池复杂度。
- **错误**：`ActionResult.ok == false` 时必须填稳定 `error_code` + 可读 `message`（供 MCP 返回）。
- **图片载荷**：`capture` 产物统一为共享类型 `Image`（`include/qingying/action/image.hpp`，BGRA32、行优先、物理像素）。`CaptureRegion` 等 Handler 将结果存入 `CaptureSession`，`Copy/Save` 从 Session 取图交给 `export`；MCP/口令不直接碰像素。
- **返回值**：`ActionResult.data` 为可选 UTF-8 载荷（如保存路径、MCP 返回值），不引 JSON。
- **三条入口**：

```text
热键/GUI ──┐
本地口令 ──┼→ ActionDispatcher → Handler → 引擎
MCP Tool ──┘
```

### 4.3 Handler 注册（app 组装）

```text
main / Application::init
  ├── CaptureEngine (PIMPL)
  ├── register CaptureRegion / CaptureWindow / CropCenter
  ├── register Copy / Save
  ├── register Pin / LongShotForeground（可后挂）
  ├── Tray + Hotkey → 打开 Overlay
  └── Overlay 完成选区 → dispatch(CaptureRegion) → dispatch(Copy)
```

---

## 5. 模块职责摘要

| 模块 | 做什么 | 不做什么 |
|---|---|---|
| app | 单实例、托盘、热键、消息循环、注册 Handler | 不写 DXGI、不写拼接算法 |
| action | 契约与分发 | 不碰屏幕像素 |
| capture | 取帧、窗口区域、DPI | 不做标注 UI |
| overlay | 遮罩状态机、框选/调区/吸附 | 不直接链接 DXGI 实现细节 |
| annotate | 矢量对象 + 撤销 + 栅格化 | 不负责捕获 |
| pin | 多枚置顶窗、捕获时排除自身 | 不实现 MCP |
| longshot | 滚动 + 重叠拼接（仅三应用） | 不支持任意应用硬试 |
| export | Clipboard / 文件 | 不解析口令 |
| command | 口令表 → ActionRequest | 不执行截图 |
| mcp | Pipe、JSON、Tool 映射 | 不实现第二套引擎；仅本机 |

---

## 6. PIMPL 使用约定

| 类 | PIMPL | 说明 |
|---|---|---|
| `CaptureEngine` | **是** | 隐藏 DXGI/COM，降低重编与头文件污染 |
| `LongShotEngine` | **是** | 实现易变 |
| `McpBridge` / `McpServer` | **是** | 协议与解析细节内聚 |
| `ActionDispatcher`、`ActionRequest/Result` | **否** | 契约需透明，便于全员联调 |
| Overlay 窗口过程薄封装 | 可选 | **状态机比 PIMPL 更重要** |

原则：PIMPL 做**模块级编译防火墙**，禁止“每个小类都套 unique_ptr&lt;Impl&gt;”。

---

## 7. 线程与生命周期

| 项 | 约定 |
|---|---|
| UI / 热键 / Overlay | 主线程 |
| `dispatch` | P0 同步在主线程调用 |
| 托盘常驻 | 不持有大 Bitmap；截图结果用完释放或交给 Pin/文件 |
| 单实例 | Named Mutex；第二实例退出或把焦点交给已有实例 |
| MCP | 本机 Named Pipe；可读线程投递到主线程再 `dispatch`（实现期再定，接口先同步） |

---

## 8. 错误码（初稿，可扩展不可乱改语义）

| code | 含义 |
|---|---|
| 0 | 成功 |
| 1 | 未知错误 |
| 10 | 未运行 / 未就绪 |
| 20 | 参数非法 |
| 30 | 捕获失败 |
| 40 | 窗口未找到 |
| 41 | 窗口匹配歧义 |
| 50 | 长截不支持当前前台应用 |
| 60 | 保存/剪贴板失败 |
| 70 | 口令未匹配（无云或云不可用） |

---

## 9. 里程碑与分工接口

### P0（架构搭好的验收）

- [x] 多 static lib 链出单个 `qingying.exe`
- [x] `ActionDispatcher` 可注册并执行 `CaptureRegion` / `Copy` / `Save` / `Status`（`registerAppHandlers`）
- [x] 集成层：`HotkeyManager`（Ctrl+Shift+Q）→ `Overlay` → `dispatch(CaptureRegion)` → `dispatch(Copy)` 已接线
- [ ] 端到端演示：待 `overlay` 遮罩 UI 与 `capture` 区域截图落地后可跑通
- [x] 集成层不跨模块依赖 `*Impl` / DXGI 私有头

### 建议人力切分

| 角色 | 目标 | P0 交付 |
|---|---|---|
| 架构/集成 | app + action | Dispatcher、托盘、热键、组装 |
| 捕获 | capture | 区域截图 + 物理像素 |
| 交互 | overlay（+ annotate） | 框选闭环到 Copy |
| 增强 | pin / longshot | P2 |
| Agent | command / mcp | P3，可先对 Dispatcher 打桩 |

---

## 10. 与功能 F1～F9 的映射

| 功能 | 主要模块 |
|---|---|
| F1 自定义区域 | overlay + capture |
| F2 窗口吸附 | overlay（WindowDetector） |
| F3 标注 | annotate |
| F4 导出 | export |
| F5 钉图 | pin |
| F6 长截图 | longshot |
| F7 托盘热键 | app |
| F8 口令 | command → action |
| F9 MCP | mcp → action |

---

## 11. 修订记录

| 日期 | 说明 |
|---|---|
| 2026-08-23 | 确认方案 B；初稿入库 |
| 2026-08-24 | 完成 app 集成层：托盘、单实例、热键、Handler 注册、主路径接线 |
| 2026-08-24 | 契约冻结：新增共享 `Image` 类型与 `ActionResult.data` 槽；`CaptureEngine` 以 `Image&` 出参、`ExportService` 收 `Image`，经 `CaptureSession` 打通 capture→export 图片链路 |
