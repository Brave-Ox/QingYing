# 轻映 QingYing — AnnotationOverlay 按职责拆分（方案 B）

> 状态：待确认（2026-08-31）
> 形态：**公开 API 冻结 + 内部按职责组合，不改标注业务语义**
> 依据：`src/annotate/annotation_overlay.cpp`（约 4100 行）、已拆出的 Session / Engine / Controller / Layout、`docs/架构如何调整.md` 阶段 A 的 Overlay 拆分先例
> 前置约束：仓库提交由团队成员手动执行；本设计落地前先评审本文，再写实施计划

关联文档：[架构如何调整](../../架构如何调整.md)、[技术方案讨论提纲](../../技术方案讨论提纲.md)、[模块协作设计](./2026-08-24-module-collaboration-design.md)

---

## 1. 背景与目标

### 1.1 现状

标注模块的纯逻辑层已经拆开，且可单测：

| 单元 | 职责 | Win32 |
|------|------|--------|
| `AnnotationEditorSession` | 一轮编辑的源图副本与确认/取消 | 无 |
| `AnnotationEngine` / `AnnotationDocument` | 对象栈、撤销 | 无 |
| `AnnotationInteractionController` | 画布拖拽预览 | 无 |
| `AnnotationRenderer` | 栅格化 | 有 GDI，但独立编译单元 |
| `annotation_editor_layout.hpp` | 尺寸、命中、钳制、就地放置 | 无 `Windows.h` |

窗口层仍全部堆在 `annotation_overlay.cpp` 的匿名命名空间：`EditorWindowData`（约 70 个字段）+ `editorWndProc` + 文字 / 主栏 / 二级栏 / 描边弹层 / 绘制 / 生命周期。第三人只依赖 `annotation_overlay.hpp`，这条边界是对的，但实现已无法按规范阅读、测试和评审。

规范缺口（必须在拆分中收口，而不是「先搬家再补」）：

- 一个编译单元承担多组无关 UI 职责
- `EditorWindowData` 字段无 `m_` 前缀，也不是类
- `showInPlace` 对 `unique_ptr` 调用 `release()`，`WM_NCDESTROY` 使用裸 `delete data`
- GDI 对象大量 `Create*` / `DeleteObject` 成对，早退路径依赖人工配对
- `handleEditorKeyDown` 等处未使用 Allman 花括号
- 可测逻辑已抽走后，剩余 cpp 无法在不打开全文的情况下理解生命周期

### 1.2 目标

1. **公开契约不变**：`AnnotationOverlay` 的 `show` / `showInPlace` / `hide` / `closeSilently` / `isVisible` 签名、返回值、回调时机与 `AnnotationFinishResult` 语义保持现状。
2. **按职责组合**：用几个有所有权的类替换上帝结构体，每个文件能回答「做什么、怎么用、依赖谁」。
3. **对齐规范**：RAII 持有窗口状态与 GDI；成员 `m_`；Allman；新魔法数进 layout 头；禁止 annotate 实现调用 Dispatcher / Export / Capture / Pin。
4. **可分阶段合入**：每一步可编译、现有测试绿、可回滚；不把 4100 行一次性改完。

### 1.3 非目标

- 不改 `CaptureWorkflow` 接线，不改选区 Overlay。
- 不把工具栏 / 二级栏 / 文字框改成独立顶层或子 HWND（那是方案 C）。
- 不重写标注交互（文字双击、拖栏不带动截图框、layered color-key 等行为保持）。
- 不把 `annotation_editor_layout.hpp` 再拆文件。
- 不给 `WndProc` 写模拟消息的脆弱单测。
- 不把 `annotation_renderer.cpp` 里已有的 `GdiObjectGuard` 强制合并进本次（允许 overlay 先用新头；renderer 迁移另开）。
- 不引入 Qt、智能指针共享所有权（`shared_ptr`）或第二套消息循环。

### 1.4 已确认决策

| 决策点 | 结论 |
|--------|------|
| 拆分策略 | **方案 B**：职责类组合，公开 API 冻结 |
| 方案 A | 拒绝：只搬家不改上帝对象，规范缺口仍在 |
| 方案 C | 拒绝：独立 HWND 会重写 layered / color-key，回归面过大 |
| GDI / `delete data` | **与拆分同期做**，不另开「纯搬家」PR |
| 公开头 | `include/qingying/annotate/annotation_overlay.hpp` 冻结 |
| 新类型头文件 | 默认放 `src/annotate/`（内部头），不扩大第三人 include 面 |
| 布局常量 | 继续只进 `annotation_editor_layout.hpp` |

---

## 2. 目标结构

```text
公开
  include/qingying/annotate/annotation_overlay.hpp

内部（src/annotate/）
  annotation_overlay.cpp              Overlay 外观：show/hide + 持有 Host
  annotation_editor_host.hpp/.cpp     窗口类注册、USERDATA、消息分发、回调交付
  annotation_editor_chrome.hpp/.cpp  主栏 + 二级栏 + 拖栏 + 工具命令
  annotation_stroke_popup.hpp/.cpp   描边芯片弹层（独立 WndProc）
  annotation_inline_text.hpp/.cpp    就地文字：EDIT、手势、选中、提交
  annotation_editor_paint.hpp/.cpp    画布 / 外框 / 缓冲合成
  gdi_raii.hpp                        HFONT/HPEN/HBRUSH/HDC/BeginPaint 守卫
```

依赖只允许向下，禁止反向 include：

```text
AnnotationOverlay
  └── AnnotationEditorHost
        ├── AnnotationEditorChrome ──► AnnotationStrokePopup
        ├── AnnotationInlineText
        ├── AnnotationEditorPaint
        ├── AnnotationEditorSession
        ├── AnnotationInteractionController
        ├── AnnotationRenderer          （仅 Host/Paint 在合成时使用）
        └── gdi_raii.hpp
Chrome / InlineText / Paint ──► annotation_editor_layout.hpp
annotate 实现 ──x──► ActionDispatcher / ExportService / CaptureEngine / PinManager
```

`CMakeLists.txt` 的 annotate 目标追加上述 `.cpp`，不新增 lib 目标。

行数目标（实施后，不是机械切刀）：

| 文件 | 目标上限 | 说明 |
|------|----------|------|
| `annotation_overlay.cpp` | 400 | 只留公开方法 |
| `annotation_editor_host.cpp` | 600 | WndProc 分发 + 生命周期 |
| `annotation_inline_text.cpp` | 1000 | 现约 322–1380 行迁入 |
| `annotation_editor_chrome.cpp` | 900 | 含命令与二级栏绘制 |
| `annotation_stroke_popup.cpp` | 450 | 含滑条与 EDIT |
| `annotation_editor_paint.cpp` | 500 | 含 blit 与 layered 合成 |

超过上限说明职责又混了，应再切，而不是继续往同一文件堆。

---

## 3. 组件职责与接口形状

以下是设计契约，不是要求一次写完的头文件终稿。命名遵循：类大驼峰、函数小驼峰、成员 `m_`。

### 3.1 AnnotationOverlay（外观，公开）

保持现有头文件。私有成员从「HWND + 两个 bool」改为同时持有 Host：

- `HWND m_hwnd`
- `bool m_visible`
- `bool m_suppress_callback`
- `std::unique_ptr<AnnotationEditorHost> m_host`

`showInPlace` 创建 Host、创建窗口、成功则 `m_host` 持有对象；失败则 `unique_ptr` 析构回收，**禁止** `release()` + 裸 `delete` 的失败补偿。`hide` / `closeSilently` 语义不变：同线程 `SendMessageW(kMsgCancelFromBackdrop)`，跨线程 `PostMessageW`。

析构继续调用 `closeSilently()`。Host 的析构不得再次 `DestroyWindow`（窗口可能已在消息里拆掉）。

### 3.2 AnnotationEditorHost（窗口生命周期）

持有：

- `AnnotationEditorSession m_session`
- `AnnotationInteractionController m_controller`
- `AnnotationCallback m_callback`
- `AnnotationEditorChrome m_chrome`
- `AnnotationInlineText m_text`
- 调用 `AnnotationEditorPaint` 的静态方法合成一帧（无跨消息状态，对标 `OverlayRenderer`）
- 图片原点、虚拟屏放置、`m_confirmed`、回写到 Overlay 的观察指针（`HWND*` / `bool*`，不拥有 Overlay）

职责：

1. 注册 `QingYingAnnotationOverlay` 窗口类（已存在则忽略 `ERROR_CLASS_ALREADY_EXISTS`）。
2. `editorWndProc` 只做：取 Host 观察指针 → `switch(msg)` 调子模块 → 默认 `DefWindowProcW`。
3. `finishAndNotify`：确认则 `session.finishConfirmed`，取消则 `finishCancelled`；`m_suppress_callback` 为真时不调回调。
4. 坐标换算 `canvasFromClient` 留在 Host（Chrome 与 Text 都要用）。

`GWLP_USERDATA` 只存 `AnnotationEditorHost*` 观察指针，不承担删除职责。

### 3.3 AnnotationInlineText

迁入现有：`beginInlineText` / `beginOrEditTextAt` / `commitInlineText` / `cancelInlineText` / 手势 / 选中 / 双击 / `inlineEditSubclassProc` / `inlineEditHostWndProc` / 测量与命中。

对外（由 Host 调用）至少包括：

- `beginOrEdit` / `beginAtIndex`
- `commit` / `cancel`
- `hitTest` / `deleteSelected`
- `updateGesture` / `finishGesture` / `tryPromoteToDrag`
- `paintSelection` / `paintLive`（若仍由本模块画选中框）
- `handleKey` 中与文字相关的 Esc / Delete 分支可由 Host 询问「是否已消费」

`AnnotationEditorInlineCommitGuard` 已在 layout 头，继续使用，禁止在 commit 路径上去掉重入保护（`EN_KILLFOCUS` 在 `DestroyWindow` 时会重入，去掉会把同一条文案 `add` 两次）。

### 3.4 AnnotationEditorChrome

迁入：主栏布局、二级栏、tooltip、工具命令、几何切换、字号芯片、拖栏。

拖栏契约冻结：窗口已铺满虚拟屏，拖栏只改 `m_chrome_offset_*` 与栏 RECT，**禁止**为塞栏而 `SetWindowPos` 移动宿主窗（现有注释：会导致截图框抖动）。

工具命令仍只改 `controller` / `session`，不写剪贴板、不调 Dispatcher。完成/取消仍由 Host `requestClose`。

### 3.5 AnnotationStrokePopup

独立窗口类 `QingYingStrokePopup` 与现有 WndProc 一起迁入。Chrome 在命中描边芯片时 `show`，Host 在销毁时 `destroy`。Popup 不持有 Session，只通过回调或 Chrome 引用写回线宽。

### 3.6 AnnotationEditorPaint

迁入：`blitImage`、`paintEditorFrame`、`paintEditor`、`paintEditorBuffered`。实现为带静态方法的类（与 `OverlayRenderer::render` 相同形态），不持有 HWND 所有权。只读 Host 提供的视图：源图、引擎文档、预览对象、图片原点、chrome 的栏 RECT、文字选中框。禁止在 Paint 里改 `controller` 或提交标注。

工具栏圆角壳仍走现有 `drawToolbarBarOnArgbBits`；Paint 负责 color-key 与分层提交，不重写 ModernToolbar。

### 3.7 消息命中顺序（行为契约，禁止调换）

`WM_LBUTTONDOWN` 必须保持：

1. 主栏拖动手柄（Move）→ `beginChromeDrag`
2. 其它主栏按钮 → `handleToolbarItemClick`
3. 命中 chrome 条（含二级栏）→ `handlePropertyBarClick`
4. 命中已有文字 → 切到文字工具并 `beginOrEdit`
5. 点在图片外 → 忽略
6. 当前是文字工具 → `beginOrEdit`
7. 否则清文字选中，`controller.beginStroke`

`kMsgCancelFromBackdrop`：取消就地文字、复位手势、清选中、`confirmed = false`、`DestroyWindow`。  
`hide()` 不置 `m_suppress_callback`；`closeSilently()` 置 `true`。这两条不得在拆分时互换。

---

## 4. 所有权与窗口生命周期

### 4.1 问题

当前：`std::make_unique<EditorWindowData>()` → `CreateWindowExW(..., data.get())` → 成功则 `data.release()`，失败且创建过程中已销毁则也 `release()`，最后在 `WM_NCDESTROY` `delete data`。这是典型的 HWND 拥有裸指针，违反禁止裸 `delete`。

### 4.2 目标协议

1. Overlay 始终是 Host 的唯一所有者（`unique_ptr`）。
2. `WM_NCCREATE` 把 `lpCreateParams` 写入 `GWLP_USERDATA`（观察指针）。
3. 创建失败：不 `release()`。若 `WM_NCDESTROY` 已在创建期间跑过，Host 必须能从 Overlay 侧被 `reset`，且 `unique_ptr` 不得二次释放。实施时用「创建期 `destroyed_during_create` 标志」保持现有防双删语义，但删除路径改为 `unique_ptr::reset`，源码中不出现 `delete data`。
4. `WM_DESTROY`：销毁 tooltip / 文字 / 描边弹层、`DeleteObject` 改为成员 RAII 析构、`finishAndNotify`、回写 Overlay 的 `m_hwnd` / `m_visible` / `m_suppress_callback`。
5. `WM_NCDESTROY`：`GWLP_USERDATA = 0`，通知 Overlay `m_host.reset()`。WndProc 必须是自由函数或静态函数，以便 `reset` 发生在 Host 成员调用栈之外或调用栈的最后一步之后。`reset` 之后不得再读 Host 字段。

Host / Chrome / InlineText 析构顺序由成员声明顺序保证：先拆弹层和 EDIT，再拆 GDI 成员。禁止在析构里 `PostMessage` 回自己。

---

## 5. GDI RAII

新建内部头 `src/annotate/gdi_raii.hpp`（不进公开 include）：

- `GdiObject`：持有 `HGDIOBJ`，析构 `DeleteObject`；空句柄为 no-op
- `DeviceContext` / `WindowDc`：`ReleaseDC` / `DeleteDC`
- `PaintGuard`：从 overlay.cpp 原样迁入（`BeginPaint` / `EndPaint`）
- `SelectGuard`：`SelectObject` 并在析构选回旧对象

规则：

- 栈上临时笔/刷/字用守卫，禁止「Create 后多处 return 再 Delete」
- 生命周期跨消息的资源（`combo_font`、`inline_edit_font`、`inline_edit_key_brush`）成为类成员 `GdiObject`
- 不封装全部 Win32；只包本次 overlay 已用到的类型
- 不把 `annotation_renderer.cpp` 的 `GdiObjectGuard` 纳入本任务的必改名单

---

## 6. 规范对照（拆分完成时必须满足）

| 条款 | 做法 |
|------|------|
| 类大驼峰 / 函数小驼峰 / 成员 `m_` | 新类全部遵守；不再保留公开字段的 `EditorWindowData` |
| Allman | 迁出函数按 Allman 重排，包括 `handleEditorKeyDown` |
| 禁止裸 `new`/`delete` | Host 只经 `unique_ptr`；NCDESTROY 用 `reset` |
| GDI RAII | 见第 5 节 |
| 禁止魔法数字 | 新的像素与颜色进 `annotation_editor_layout.hpp` 的 `constexpr`；迁出时顺手把已重复的字面量搬进 layout，不借机改数值 |
| `#pragma once` / 前置声明 | 内部头互相前置；`Windows.h` 尽量停在 `.cpp`，Host/Chrome 因 HWND 可在头中包含 |
| 不调用其它业务模块 | 维持 overlay.hpp 注释中的边界 |
| 返回值处理 | `RegisterClassExW` / `CreateWindowExW` / GDI 创建失败路径保持现有「失败则不显示、不回调」 |

---

## 7. 分阶段落地

每阶段结束必须：annotate 相关测试通过；公开 API 不变；可单独回滚该阶段。

| 阶段 | 内容 | 行为变化 |
|------|------|----------|
| 0 | 确认基线：`annotation_overlay_test`、session / controller / engine / layout 相关测试 | 无 |
| 1 | `gdi_raii.hpp` + Overlay 持有 `unique_ptr<Host>` 雏形（可先是改名后的 `EditorWindowData` 包进 Host），去掉裸 `delete` | 无 |
| 2 | 抽出 `AnnotationInlineText` | 无 |
| 3 | 抽出 `AnnotationEditorChrome` + `AnnotationStrokePopup` | 无 |
| 4 | 抽出 `AnnotationEditorPaint`，WndProc 只分发 | 无 |
| 5 | 删除残留上帝结构体；`annotation_overlay.cpp` 只留公开方法 | 无 |

阶段 1 允许 Host 仍暂时内嵌原结构体，目的是先把所有权改对，再按字段群搬走。禁止在阶段 2–4 改命中顺序、回调抑制、拖栏 `SetWindowPos` 策略。

---

## 8. 测试

**守门（每阶段必跑）**

- `annotation_overlay_test`：未显示、空图拒绝且不回调、idle `hide` 安全、show 立即返回、hide 取消回调
- `annotation_editor_session_test` / `annotation_interaction_controller_test` / `annotation_engine_test` / `annotation_document_test`
- layout 相关断言（含 overlay_test 里对 layout 的用例）

**允许新增**

- 仅当从 Chrome/Text 再抽出无 HWND 的纯函数时补单测（例如命中矩形、滑条数值）。layout 头已覆盖的不要重复造测试文件。

**不自动化**

- 真实 EDIT 输入、layered 合成观感、DPI / 多屏、拖栏不抖动 —— 继续走现有手工冒烟（开发清单 / F1 分工指南中的人工验收项），不为本拆分新增长篇测试文档。

---

## 9. 风险与回归点

| 风险 | 为何出现 | 约束 |
|------|----------|------|
| 创建失败双释放 | 现有 `destroyed_during_create` + `release()` | 阶段 1 必须用测试或代码路径保持「只释放一次」 |
| `EN_KILLFOCUS` 重入 | `DestroyWindow` 同步触发 commit | 保留 `AnnotationEditorInlineCommitGuard` |
| `SendMessage` 同步销毁 | `hide` 在同一线程销毁窗口 | Overlay 在消息返回后不得使用已 `reset` 的 Host |
| 拖栏抖动 | 误对宿主 `SetWindowPos` | 代码与注释一起搬走，禁止「优化」窗口移动 |
| color-key 打孔 | Paint 与工具栏合成顺序敏感 | Paint 整段迁出，不拆成多次半迁移 |

---

## 10. 成功标准

1. `annotation_overlay.hpp` diff 不含公开方法增删改（允许私有成员变为 `unique_ptr`，这不改变第三人编译依赖若其只含公开头）。
2. 源码中 `annotation_overlay.cpp` 不再出现 `delete data`，不再定义 `EditorWindowData` 上帝结构体。
3. 新编译单元职责与第 2 节一致，行数不超过第 2 节上限。
4. 第 8 节守门测试通过；无新增 DISABLED 窗口测试。
5. `CaptureWorkflow` 无需修改即可链接（若因私有成员变化导致 overlay.cpp 以外的文件必须改，视为设计失败，应缩回）。

---

## 11. 自检

- 无 TBD / TODO 占位。
- 方案 A/C 已拒绝并写明原因。
- 公开 API、命中顺序、回调抑制、拖栏策略均已写成不可含糊的契约。
- 范围限于 annotate 窗口层；不包含 renderer 全面 RAII、不包含 Workflow。
- 下一步：本文确认后，再写 `docs/superpowers/plans/2026-08-31-annotation-overlay-split.md` 实施计划；确认前不改业务代码。
