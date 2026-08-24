# 轻映 QingYing — 开发进度（PROGRESS）

> 本文记录**当前开发进度快照**：已完成且可验证 / 进行中 / 待办 / 下一步建议。
> 功能规划与验收指标见 [轻映-QingYing-开发清单.md](../轻映-QingYing-开发清单.md)（本文与之互补，不重复规划）。
> 更新日期：2026-08-24

---

## 1. 当前状态总览

| 阶段 | 状态 | 说明 |
|---|---|---|
| 工程骨架（CMake / MSVC / C++17 / 10 个 static lib / 单 EXE） | ✅ 完成 | Release 单 EXE 可构建 |
| P0 主路径「截一下」 | 🔶 基本闭环 | 热键 → 框选 → 截图 → 复制 / 保存 PNG |
| 单元测试 | ✅ 32/32 绿 | 8 个测试套件，gtest |

**一句话**：当前已打通「**热键 → 全屏遮罩 → 左键框选 → 松开自动截图 → 复制到剪贴板 / 保存 PNG**」主链路；下一步是标注（F3）。

---

## 2. 已完成（含验证方式）

### 2.1 截图与剪贴板主链路（本次里程碑）

| 能力 | 文件 | 验证方式 |
|---|---|---|
| 全屏半透明遮罩（WS_EX_LAYERED + UpdateLayeredWindow，35% 黑 + 4px 橙框） | `src/overlay/selection_overlay.cpp` | 手动：Ctrl+Shift+Q 出现全屏遮罩 |
| 鼠标拖拽框选（显式状态机：begin/update/confirm/cancel） | `src/overlay/selection_controller.cpp` | 单测 `selection_controller_test`（全绿） |
| 遮罩像素渲染（遮罩 + 选中区清空 + 边框） | `src/overlay/mask_renderer.cpp` | 单测 `mask_renderer_test`（全绿） |
| 区域截图（GDI BitBlt 实装，替代原 stub） | `src/capture/capture_engine.cpp` | 单测 `capture_engine_test`；手动演示闭环 |
| 截图 → 剪贴板（CF_DIB）复制 | `src/export/export_service.cpp` | 手动：截图后 Ctrl+V 粘贴成功 |
| DIB 编码器（Image → CF_DIB 纯函数，底向上） | `src/export/dib_encoder.cpp` | 单测 `dib_encoder_test`（3 用例全绿） |
| 截图 → PNG 保存（Windows WIC） | `src/export/export_service.cpp` | 单测 `export_service_test`（3 用例全绿） |
| 选区操作条保存最近截图 | `src/overlay/selection_overlay.cpp` / `src/app/application.cpp` | 框选后 → 下载图片 → 原生保存对话框 |

**手动验收通过**：`Ctrl+Shift+Q` → 左键框选 → 松开 → 打开记事本/画图 `Ctrl+V` 粘贴出截图 ✅

### 2.2 工程与基础

- 10 个 static lib 目标 + 单 EXE；`qingying_action`（ActionDispatcher）为唯一业务入口
- 托盘 / 全局热键（Ctrl+Shift+Q）/ 单实例 / 自启动（`src/app/`）
- Action 契约：`ActionType` + `ErrorCode` + 共享 `Image` 载荷（`src/action/`）
- 架构落地为方案 B（多 static lib + ActionDispatcher），见 `docs/architecture.md`

---

## 3. 进行中 / 待办

### 3.1 代码待办

- [ ] **提交当前改动**：DIB 编码 + copyToClipboard 已实现并验证，尚未 commit/push（工作区有未提交改动）
- [ ] **PIMPL 重构 capture_engine.cpp**：当前内部用裸 `new Impl` / `delete impl_`，违反规范「禁止裸 new/delete」红线，建议改 `std::unique_ptr`（[CLAUDE.md 红线四](../.claude/CLAUDE.md)）

### 3.2 功能待办（对照开发清单 P0→P1）

- [ ] **P1**：标注（矩形/椭圆/箭头/画笔/文字/马赛克 + 撤销）——F3
- [ ] **P1**：八点调区 / 移动选区；窗口吸附（F2）
- [ ] **P1**：Per-Monitor DPI / 双屏 + 125% / 150% 冒烟
- [ ] **P2**：钉图（Pin，F5）；长截图（F6，限定记事本/资源管理器/Edge）
- [ ] **P3**：本地口令（F8）；MCP Bridge（F9）

> 完整 F1～F9 与验收用例见 [开发清单](../轻映-QingYing-开发清单.md)。

---

## 4. 质量与验证基线

- **单元测试**：32/32 绿（`action_dispatcher` / `action_handlers` / `capture_engine` / `capture_session` / `dib_encoder` / `export_service` / `mask_renderer` / `selection_controller`）
- **TDD**：核心逻辑均先写失败测试再实现（SelectionController / MaskRenderer / encodeDib）
- **规范**：遵守 CLAUDE.md（m_ 前缀、Allman、RAII 管 GDI/HANDLE、`.at()` 防越界、宽字符 API、错误码不抛异常）；UI/系统资源壳（Overlay、Clipboard）为已批准的 TDD 例外
- **构建**：`build.bat Release` / `build.bat test`（gtest via vcpkg，仓库同级 `../thirdparty_install/vcpkg`）

---

## 5. 下一步建议（供评审/分工参考）

1. **先落提交**：把当前 6 个文件的剪贴板改动 commit + push（避免进度漂移在本地）
2. **PIMPL 化 capture_engine**：消除裸 new/delete，达标红线
3. **进入 P1 标注**：这是演示主路径「截—标」的核心增值点，建议优先
4. **DPI / 多屏冒烟**：在双屏 + 125%/150% 下验证选区与成像无偏移（高风险项，早验证）

---

*进度文件由开发过程持续维护；与开发清单、架构文档联动更新。*
