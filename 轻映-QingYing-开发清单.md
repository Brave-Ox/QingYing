# 轻映 QingYing — 开发清单与技术要点

> 依据立项文档与架构说明书整理，便于开发落地对照。  
> 来源：  
> - [轻映-QingYing（立项）](https://365.kdocs.cn/l/cg3MggX6b0hl)  
> - [轻映 QingYing《技术选型与架构说明书》](https://365.kdocs.cn/l/cjyNzW0HlWuo)  
> - 仓库落地架构：[docs/architecture.md](./docs/architecture.md)（方案 B：多 static lib + ActionDispatcher）

---

## 1. 一句话目标

用不超过 **20 MB** 的 Windows **绿色单文件**，打通「**框选 → 标注 → 钉图对照 → 长页拼接**」全链路；并支持通过 **自然语言口令** 与 **MCP / Skill**，为 Agent 提供**本地截图能力**。

| 项 | 内容 |
|---|---|
| 产品名 | 轻映 QingYing（轻 = 可核对的轻量；映 = 钉在桌面对照） |
| Slogan | 截得轻，钉得住，长页一次成 |
| 平台 | Windows only |
| GUI | **不用 Qt / Electron**；C++17 + Win32 原生 |
| 主路径 | 热键 → 框选/调区 → 标注 → 复制/保存（约 **15 秒**可演示） |
| 周期参考 | 约 30 天交付可运行绿色程序 |

---

## 2. 硬指标（验收门槛）

| 指标 | 目标 | 测量方式 |
|---|---|---|
| 发布体积 | ≤ **20 MB** | 发布目录单文件（或约定交付包） |
| 常驻内存 | ≤ **40 MB** | 仅托盘常驻、未打开截图界面时的工作集 |
| 唤起时延 | ≤ **300 ms** | 热键按下 → 全屏遮罩可见 |
| 主路径演示 | ≈ 15 s | 热键 → 框选 → 标注 → 复制 |

**约束**：F1～F7 **不依赖网络**；模型不进安装包；主路径本地闭环。

---

## 3. 功能清单（F1～F9）

| ID | 功能 | 用户价值 | 验收要点 |
|---|---|---|---|
| **F1** | 自定义截图区域 | 自由框选 + 八点调区 | 热键唤起后可框选/调区；Esc / 右键取消 |
| **F2** | 窗口吸附 | 悬停高亮窗口，降低框选偏差 | 常见顶层窗口可吸附；可切回自由框选 |
| **F3** | 截图标注 | 矩形/椭圆/箭头/画笔/文字/马赛克 + 撤销 | 走通「截取—标注—复制/保存」 |
| **F4** | 导出 | 剪贴板 + 另存 PNG | 快捷键复制与另存为 |
| **F5** | 钉图（Pin） | 置顶对照，可拖拽/复制/保存/关闭 | ≥2 枚并存；截图排除自身窗口 |
| **F6** | 长截图 | 选区自动滚动拼接 | **记事本 / 资源管理器 / Edge** 各至少一次 |
| **F7** | 托盘与热键 | 托盘常驻 + 全局热键 | 单实例；热键冲突有提示 |
| **F8** | 口令截图 | 自然语言：中央裁切 / 按名截窗 / 长截 / 命名保存 | 本地口令表可演示；未命中可离线退回框选；云解析可选 |
| **F9** | Agent / MCP | 标准 Tool 调用本地能力 | 至少：`status`、`capture_window`、`crop_center`、`longshot_foreground`、`save`、`copy`、`pin` |

### F1～F9 ↔ 技术模块

| 功能 | 技术模块 |
|---|---|
| F1 | SelectionOverlay + CaptureEngine |
| F2 | WindowDetector + SelectionOverlay |
| F3 | AnnotationEngine |
| F4 | ClipboardManager + FileManager |
| F5 | PinManager |
| F6 | LongShotEngine |
| F7 | TrayController + HotkeyManager |
| F8 | CommandParser + ActionDispatcher |
| F9 | MCP Bridge + Named Pipe + ActionDispatcher |

---

## 4. 范围边界（本期明确不做）

- 屏幕录制 / GIF 产品化  
- 云同步图床、账号体系、AI 改图  
- 完整 OCR 产品化；用模型“看屏幕找按钮/弹窗再截”  
- 跨平台；**Qt / Electron** 等重型 GUI  
- 整屏或逐帧交给模型查看  
- 标注/调区等指针操作的 MCP 自动化  
- DXGI 等内部接口直接暴露给 Agent  
- **任意应用**长截图；自动点击 / 自动滚动  
- 自主操作屏幕的“截图 Agent”  
- 本地大模型打进安装包  

---

## 5. 开发优先级（按架构说明书）

> **重要**：Action Dispatcher 应在 **F1～F4 阶段就建立**，不要等到 MCP 再设计，以便 GUI / 口令 / MCP 共用同一套 Action。

```
P0（主路径骨架）
 ├── 单实例
 ├── 托盘
 ├── 全局热键
 ├── 全屏遮罩
 ├── 框选
 ├── CaptureEngine
 ├── Clipboard
 └── Save
        ↓
P1（交互完善）
 ├── 调区
 ├── 标注
 ├── 窗口吸附
 └── DPI / 多屏
        ↓
P2（增强能力）
 ├── Pin（钉图）
 └── LongShot（长截图）
        ↓
P3（可编程入口）
 ├── Local Command（本地口令）
 ├── Action Dispatcher（统一调度）
 └── MCP Bridge
        ↓
P4（可选增强）
 ├── Skill 说明
 └── Cloud LLM fallback（口令未命中 / 建议文件名）
```

### 建议任务拆分 Checklist

#### P0 — 可演示的「截一下」
- [ ] 工程骨架：CMake / MSVC，C++17，Release 单 EXE  
- [ ] `SingleInstanceGuard`（Named Mutex）  
- [ ] 系统托盘 + 退出/关于  
- [ ] `RegisterHotKey` 全局热键；冲突提示  
- [ ] 全屏 `SelectionOverlay`（半透明遮罩）  
- [ ] 鼠标拖拽框选；Esc / 右键取消  
- [ ] `CaptureEngine`：DXGI Desktop Duplication 主路径 + GDI fallback  
- [ ] 复制到剪贴板（PNG/位图）  
- [x] 另存为 PNG
- [ ] **埋下** `ActionDispatcher` 空壳与标准 Action 枚举  

#### P1 — 可用的「截—标」
- [ ] 八点调区 / 移动选区  
- [ ] 窗口嗅探与吸附高亮  
- [ ] 标注：矩形、椭圆、箭头、画笔、文字、马赛克  
- [ ] 矢量对象列表 + 撤销栈；导出时栅格化  
- [ ] Per-Monitor DPI：捕获用物理像素，UI 做坐标换算  
- [ ] 双屏 + 125% / 150% DPI 冒烟  

#### P2 — 「钉得住 / 长页一次成」
- [ ] 独立钉图窗口：置顶、拖拽、复制、保存、关闭  
- [ ] 多枚钉图并存；捕获时排除/临时隐藏自身  
- [ ] 长截图：滚轮投递到正确子窗口 + 帧重叠拼接  
- [ ] 长截验收限定：**记事本、资源管理器、Edge**  

#### P3 — 「口令 + Agent」
- [ ] 本地口令表 → 标准 Action  
- [ ] Action 与 GUI 共用同一 Dispatch  
- [ ] Named Pipe：MCP Bridge ↔ 托盘主进程  
- [ ] 实现 F9 Tool 集（见下节）  
- [ ] 失败返回明确错误，不自动重试  

#### P4 — 可选
- [ ] Skill 文档（场景、参数、失败后改热键框选）  
- [ ] 云端 LLM：仅解析**一句文本**为 Action（不读屏、不执行截图）  
- [ ] `suggest_name`：用户确认后才上传当前一张结果图  

---

## 6. 技术选型（开发要用的东西）

| 领域 | 选型 | 说明 |
|---|---|---|
| 语言 | **C++17** | 体积 / 性能 / 原生能力 |
| GUI | **Win32** | 不用 Qt / Electron |
| 绘制 | Win32 + **Direct2D / GDI** | 遮罩、标注、钉图 |
| 主捕获 | **DXGI Desktop Duplication** | 高效取帧 |
| 兼容捕获 | **GDI** | fallback |
| 多屏 | Win32 Monitor API | 显示器列表 |
| DPI | **Per-Monitor DPI Awareness** | 125% / 150% |
| 剪贴板 | Win32 Clipboard | 复制 |
| 热键 | `RegisterHotKey` | 全局热键 |
| 单实例 | Named Mutex | 防多开 |
| IPC | **Named Pipe** | MCP ↔ 主进程 |
| MCP | 内置 MCP Bridge | 不另装运行时 |
| JSON | 轻量 JSON 库 | Action / MCP 参数 |
| 发布 | 单 EXE | 绿色分发 |

### 建议核心类 / 模块

| 模块 | 职责 |
|---|---|
| Application / TrayController | 启动、托盘、生命周期 |
| HotkeyManager | 全局热键 |
| SingleInstanceGuard | 单实例 |
| CaptureEngine | 全屏/显示器/窗口/区域捕获 |
| SelectionOverlay | 遮罩、框选、调区、吸附、工具栏（显式状态机） |
| AnnotationEngine | 矢量标注 + 撤销 |
| PinManager | 多钉图窗口 |
| LongShotEngine | 滚动拼接（三应用） |
| ClipboardManager / FileManager | 复制与保存 |
| CommandParser | 本地口令表 |
| ActionDispatcher | 统一执行入口 |
| McpBridge | 本机 MCP Tool ↔ ActionRequest |

### Action Dispatch（统一执行层）

三条入口汇入同一层：

```
GUI / 热键鼠标 ─┐
自然语言口令   ─┼→ Action Dispatch → Capture / Crop / LongShot / Save / Copy / Pin / Status
MCP Tool       ─┘
```

原则：

1. **主路径本地优先** — AI 不得绑架主截图流程  
2. **模型只理解、不执行** — 不读屏、不截图、不拼接  
3. **Agent 调用已有能力** — MCP 不再实现第二套截图引擎  
4. **模块化但不引入重型框架**  

---

## 7. MCP / Skill 封装清单

### 定位

| | 含义 |
|---|---|
| **MCP** | 标准 Tool：一次调用一项动作；参数结构化；与口令共用本地 Dispatch；仅本机连接 |
| **Skill** | Agent 使用说明：场景、参数、失败后改热键框选；**本身不截屏、不拼图** |

### Tool 一览

| Tool | 作用 | 依赖云 |
|---|---|---|
| `status()` | 查询托盘是否运行 | 否 |
| `capture_window(query)` | 按窗口名截取；多匹配返回歧义 | 否 |
| `crop_center(width, height)` | 裁取画面中央 | 否 |
| `longshot_foreground()` | 前台页长截（仅三应用） | 否 |
| `save(path, name)` | 按名保存 | 否 |
| `copy()` / `pin()` | 复制 / 钉住当前结果 | 否 |
| `suggest_name()` | 建议文件名（需确认） | **是，可选** |

### 不纳入 MCP

- 整屏/逐帧给模型看  
- 标注与调区等指针操作  
- DXGI 等内部接口  
- 任意应用长截  
- 自动点击或自动滚动  

### 调用前提

轻映已在本机托盘运行；MCP **仅本机**，不接受远程网络调用。

---

## 8. 关键验收用例（评审可演示）

- [ ] 主路径：热键 → 框选/调区 → 标注 → 复制，约 15 秒  
- [ ] 轻量：体积 ≤20 MB；常驻 ≤40 MB；热键→遮罩 ≤300 ms  
- [ ] 双屏 + 125%/150% DPI：选区与成像无明显偏移  
- [ ] 钉图：≥2 枚并存、可拖拽对照；新截图不含自身钉图窗  
- [ ] 长截：记事本 / 资源管理器 / Edge 各 ≥1 次自动拼接  
- [ ] 本地口令：现场 ≥2 类（如按名截窗、指定尺寸/命名保存）；无密钥/无网不影响 F1～F7  
- [ ] MCP：至少成功调用 **2** 项标准 Tool，或返回明确错误  
- [ ] 交付物：可运行绿色 EXE + 架构说明 + Skill/MCP 接口说明 + 立项计划书  

---

## 9. 技术风险与应对

| 风险 | 等级 | 应对 |
|---|---|---|
| DPI 坐标错误 | 高 | 统一物理像素 + DPI 转换层 |
| 长截图拼接失败 | 高 | 限定三类应用 |
| 自身钉图被截入 | 中 | 捕获前排除 / 临时隐藏 |
| Win32 自绘复杂 | 中 | 模块化控件 + 显式状态机 |
| MCP ↔ 主进程通信 | 中 | Named Pipe |
| MCP 参数异常 | 中 | Schema + Action Validation |
| 云端不可用 | 低 | 本地口令 fallback |
| 体积/内存超标 | 中 | 禁重型框架与模型；Bitmap 按需分配 |

### 降级策略摘要

| 场景 | 行为 |
|---|---|
| 无网络 / 无云密钥 | F1～F7 正常；F8 仅本地口令 |
| MCP 未启动 | `status` → NotRunning；提示先启动托盘 |
| 长截不支持的应用 | 返回 `LONGSHOT_UNSUPPORTED`，不盲目尝试 |

---

## 10. 性能设计要点

**≤300 ms 热键路径只做**：唤醒 → 创建 Overlay → 显示遮罩。  
**不要**在该路径上做：云请求、图片压缩、长截初始化、MCP 初始化、模型加载。

托盘常驻只保留：Tray / Hotkey / ActionDispatcher / CaptureManager 等轻量对象；大 Bitmap **按需分配、用完释放**。

体积控制：不用 Qt/Electron/Python Runtime/本地模型；少第三方库；优先系统 API；Release；去掉无关资源。

---

## 11. 目标用户与场景（开发时别跑偏）

| 类型 | 说明 |
|---|---|
| 主用户 | 高频截图的办公/产研；需要在 Agent 工作流里调本地截图的人 |
| 非目标 | 要云同步、录屏剪辑、跨平台统一客户端的人（本期不做） |
| 日常办公 | 框选复制进文档/IM；口令截窗或命名保存 |
| 研发测试 | 钉图做缺陷/UI 对照；MCP 调截图免写临时脚本 |
| 文档 | 步骤标注；三应用长页一次截 |

---

## 12. 交付物清单

- [ ] 可运行绿色单文件 EXE（满足体积/内存/时延）  
- [ ] 架构说明（可引用本仓库对本说明书的落地实现）  
- [ ] Skill / MCP 接口说明（Tool、参数、错误码）  
- [ ] 立项计划书 / Demo 脚本（15 秒主路径 + 钉图 + 长截 + 口令/MCP）  

---

## 13. 架构一句话

```
输入适配层（热键GUI / 口令 / MCP）
        → Action Dispatch
        → 本地能力引擎（Capture / Annotation / Pin / LongShot / Clipboard / File）
```

Agent **不直接操控屏幕**，只通过 MCP 调用轻映已具备的本地截图能力。

---

*整理日期：2026-08-23*
)
