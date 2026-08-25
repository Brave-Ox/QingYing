# 轻映 QingYing — 开发进度（PROGRESS）

> 本文记录当前开发进度快照：已完成且可验证 / 进行中 / 待办 / 下一步建议。
> 功能规划与验收指标见 [轻映-QingYing-开发清单.md](../轻映-QingYing-开发清单.md)，本文与之互补，不重复规划。
> 更新日期：2026-08-25

---

## 1. 当前状态总览

| 阶段 | 状态 | 说明 |
|---|---|---|
| 工程骨架（CMake / MSVC / C++17 / 多 static lib / 单 EXE） | ✅ 完成 | Release 单 EXE 可构建 |
| P0 主路径「截一下」 | ✅ 基本闭环 | 热键 → 框选 → 截图 → 复制 / 保存 PNG / 钉图 |
| F1 自定义截图区域 | ✅ 核心完成 | 八点调整、移动选区、虚拟桌面和物理像素坐标已接入 |
| F4 导出 | ✅ 完成 | 剪贴板复制和 PNG 保存 |
| F5 钉图 | 🔶 基本完成，待手工验收 | 置顶、多 Pin、拖动、关闭、等比例缩放、截图排除 Pin、Pin 独立复制 / 保存已接入 |
| F7 托盘与热键 | ✅ 基本完成 | 单实例、托盘、全局热键和冲突提示 |
| 单元测试 | ✅ 62/62 绿 | 12 个测试套件，gtest |

**一句话**：当前已打通「**热键 → 全屏遮罩 → 框选 / 调整 → 截图 → 复制 / 保存 / 钉图**」主路径；下一步是标注（F3）。

---

## 2. 已完成（含验证方式）

### 2.1 截图与选区主链路

| 能力 | 文件 | 验证方式 |
|---|---|---|
| Per-Monitor V2 DPI 感知 | `src/app/main.cpp` | 应用启动时设置 DPI awareness |
| 虚拟桌面遮罩 | `src/overlay/selection_overlay.cpp` | 覆盖所有显示器，支持负坐标屏幕 |
| 半透明遮罩渲染 | `src/overlay/mask_renderer.cpp` | 单元测试通过 |
| 鼠标拖拽框选 | `src/overlay/selection_controller.cpp` | 创建 / 更新 / 确认 / 取消状态机测试通过 |
| 八点调整和移动选区 | `src/overlay/selection_handles.cpp` / `selection_controller.cpp` | 命中、调整、移动和边界测试通过 |
| 方向光标反馈 | `src/overlay/selection_overlay.cpp` | 边、角、内部和创建状态使用对应光标 |
| 坐标转换 | `src/overlay/coordinate_transform.cpp` | 虚拟桌面、DPI 缩放和客户区坐标测试通过 |
| 区域截图 | `src/capture/capture_engine.cpp` | GDI BitBlt、虚拟桌面裁剪和真实集成测试通过 |
| 截图 → 剪贴板 | `src/export/export_service.cpp` | CF_DIB 编码测试及手动粘贴验证 |
| 截图 → PNG 保存 | `src/export/export_service.cpp` | WIC PNG 输出测试通过 |
| 选区操作条 | `src/overlay/selection_overlay.cpp` / `src/app/application.cpp` | 框选后显示复制、下载、编辑、钉图入口 |

### 2.2 Pin 与 Application 接入

- `PinWindow`：无原生标题栏、红色高亮边框、按比例显示、拖动、边缘 / 四角缩放和单窗口关闭；
- `PinWindow`：右键菜单支持复制、保存图片和关闭，复制 / 保存使用当前 Pin 自身的 `Image`；
- `PinManager`：多 Pin、全部关闭、数量统计、窗口生命周期清理；
- 截图保护作用域：截图前临时隐藏可见 Pin，等待桌面合成后执行截图，完成或失败后恢复 Pin 并保持置顶；
- `ActionType::Pin` 已注册到 `ActionDispatcher`；
- Application 从 `CaptureSession` 取得当前图片并分发 Pin；
- 操作条“钉图”按钮已启用；
- Application 通过 `ExportService` 为 Pin 提供复制 / 保存回调，避免多个 Pin 之间串图；
- Pin 单元测试和 Handler 集成测试已通过，完整 Release 测试 62/62 通过。

### 2.3 工程与基础

- 多 static lib + 单 EXE，`qingying_action` 为统一业务入口；
- 系统托盘、全局热键 `Ctrl+Shift+Q`、单实例和自启动入口；
- `ActionType`、`ActionResult`、共享 `Image` 和 `CaptureSession` 契约；
- Application 通过 `ActionDispatcher` 组装 Capture / Copy / Save / Pin；
- Release 构建和全量测试通过。

---

## 3. 进行中 / 待办

### 3.1 功能状态（对照开发清单 F1～F9）

- [x] **F1**：自由框选、八点调整、移动选区、取消、边界限制、多屏和 DPI 坐标转换；
- [ ] **F2**：窗口检测、悬停高亮和窗口吸附；
- [ ] **F3**：矩形、椭圆、箭头、画笔、文字、马赛克和撤销 / 重做；
- [x] **F4**：剪贴板复制和 PNG 保存；
- [~] **F5**：置顶、多 Pin、拖动、关闭、等比例缩放、截图排除自身 Pin、Pin 独立复制 / 保存和操作条接线已完成；待手工验证多 Pin 操作及隐藏 / 恢复时的视觉体验；
- [ ] **F6**：记事本、资源管理器、Edge 长截图；
- [x] **F7 基础能力**：托盘、全局热键、单实例、冲突提示；
- [ ] **F8**：本地口令表和自然语言截图入口；
- [ ] **F9**：MCP Bridge、Named Pipe 和标准 Tool 集。

### 3.2 技术待办和已知边界

- [ ] 将 `capture_engine.cpp` 的裸 `new/delete` 改为 `std::unique_ptr`，完成 PIMPL 规范整改；
- [ ] 在双屏、125% / 150% / 200% 和混合 DPI 环境下完成手动冒烟验收；
- [ ] 当前全虚拟桌面 Overlay 的 UI 尺寸仍基于系统 DPI，后续视混合 DPI 验收结果决定是否改为按显示器管理 Overlay；
- [ ] Pin 后续增强：多 Pin 自动排布、标注同步、自动保存和长截图；
- [ ] 标注完成后仍需把最终 `Image` 写回 `CaptureSession`，再统一回到 Copy / Save / Pin。

---

## 4. 质量与验证基线

- **单元测试**：62/62 通过；
- **测试套件**：`action_dispatcher` / `action_handlers` / `capture_engine` / `capture_session` / `dib_encoder` / `export_service` / `mask_renderer` / `selection_controller` / `selection_handles` / `coordinate_transform` / `f1_selection_integration` / `pin_manager`；
- **集成验证**：F1 选区 → 物理像素坐标 → `CaptureEngine` 区域截图已覆盖；`CaptureSession` → Pin Handler → PinManager → PinWindow 已覆盖；截图保护作用域和 Pin 独立复制 / 保存代码路径已接入；
- **构建**：`build.bat Release` 和 `build.bat Release test` 已验证；
- **资源管理**：GDI、窗口和系统资源使用 RAII 或明确生命周期清理；
- **待补验证**：F5 多 Pin 手工操作、截图时 Pin 隐藏 / 恢复的视觉体验、不同显示器 DPI 下的真实人工操作，以及 F2/F3/F6/F8/F9 功能验收。

---

## 5. 下一步建议

1. 优先实现 F3 标注，打通「截图 → 标注 → 复制 / 保存 / 钉图」主演示路径；
2. 实现 F2 窗口检测和吸附；
3. 完成 F5 多 Pin 和截图隐藏 / 恢复的手工验收，必要时优化视觉闪烁；
4. 完成双屏、混合 DPI 的人工冒烟测试，并根据结果完善 Per-Monitor UI 尺寸；
5. 再进入 F6 长截图和 F8/F9 Agent 能力。

---

*本文随开发过程持续更新；架构边界见 [docs/architecture.md](architecture.md)，完整功能目标见 [轻映-QingYing-开发清单.md](../轻映-QingYing-开发清单.md)。*
