# 轻映 QingYing

Windows 原生轻量截图工具（C++17 + Win32，单 EXE），也规划作为 Agent 的本地截图能力提供者（本地口令 / MCP）。

> 当前提交基线：`master` / `a4e400bc`（63 条提交；本文同时反映当前待提交的架构调整）
> 文档同步日期：2026-08-31
> Release 验证：构建成功，262 个测试已发现，261 个执行并全部通过，1 个窗口冒烟测试显式禁用

## 当前能力

- 已打通：全局热键 → 桌面快照遮罩 → 自由框选 / 八点调整 / 窗口吸附 → 区域截图 → 复制 / PNG 保存 / 钉图。
- 已实现：多 Pin、自动避让排布、拖动与等比例缩放、独立复制 / 保存、截图时临时隐藏 Pin。
- 已接入：选区“编辑”进入就地标注，支持矩形、椭圆、箭头、画笔、文字、马赛克、撤销；确认后自动复制，并恢复同一选区的结果操作条，可继续保存、钉图或再次编辑。
- 已调整：选区条抽为 `SelectionToolbar`，交互阶段由 `OverlayPhase` 显式管理；底层仍复用白色圆角 `ModernToolbar`、GDI+、SVG 路径图标和中文 Tooltip。
- 已接入：记事本选区长截图，支持固定选区滚动拼接、实时预览、暂停 / 继续、停止和失败清理。
- 待实现：F8 本地口令、F9 MCP；`CaptureWindow` 与 `CropCenter` 仍为桩。F3 仍需重做按钮和真实窗口 / DPI 人工验收。
- 待扩展：资源管理器与 Edge 长截图适配，以及双屏 / 混合 DPI、Pin、长截图的真实人工验收。

## 文档

- [开发进度](./docs/PROGRESS.md) — 当前代码、测试和人工验收状态
- [开发清单](./轻映-QingYing-开发清单.md) — F1～F9 范围、指标和验收项
- [当前架构](./docs/architecture.md) — 已落地的模块、调用链和已知例外
- [架构如何调整](./docs/架构如何调整.md) — 不推倒重来的分阶段整改方案
- [系统架构与耦合度分析](./docs/系统架构与耦合度分析.md) — 基于当前 HEAD 的耦合审计
- [F1 / F2 / F3 / Pin / 长截图分工指南](./docs/F1与标注与钉图初版分工指南.md) — 协作边界和集成契约

## 架构要点

- **一个 EXE + 10 个 static lib**，按 action / capture / export / ui / overlay / annotate / pin / longshot / command / mcp 切分。
- `app/Application` 是组合根，拥有托盘、全局热键、顶层消息循环和交互工作流。
- 对外可调用的业务动作以 `ActionDispatcher` 为统一入口；GUI 的选区、标注和交互式长截图属于多步工作流，由 Application 编排。
- `Image` 统一为 BGRA32、行优先、物理像素；当前结果经 `CaptureSession` 在 Capture / Copy / Save / Pin 之间流转。
- 当前区域捕获使用 GDI `BitBlt`；DXGI 仅保留链接和后续实现位置。
- 已完成第一批架构收敛：`SelectionToolbar`、`OverlayPhase`、编辑源图上移，以及 `overlay → capture / annotate` 依赖移除。后续见 [架构如何调整](./docs/架构如何调整.md)：`CaptureWorkflow`、`LongShotController`、非模态消息循环、Renderer 拆分和 PIMPL RAII。

## 构建与测试

双击或在项目根目录执行：

```bat
build.bat                 :: Release 构建，编译测试但不运行
build.bat Debug
build.bat Release clean
build.bat test            :: Release 构建并运行全部测试
build.bat Debug test
build.bat notest          :: 不编译测试
```

产物：`build/bin/Release/qingying.exe`
测试：`build/bin/Release/qingying_tests.exe`

2026-08-31 本机 Release 结果：

- `qingying.exe`：214,528 字节（约 0.20 MiB，仅指当前 EXE 文件）；
- CTest 发现 262 个用例，其中 261 个执行并全部通过，`AnnotationOverlayTest.DISABLED_SmokeConfirmReturnsSourceCopy` 显式禁用；
- 常驻内存、热键唤起时延和完整绿色交付包体积仍需专项测量，不能由编译结果代替。

GoogleTest 使用仓库同级目录的 vcpkg（本地依赖，不入库）：

```text
../thirdparty_install/vcpkg
```

需预先安装：

```bat
vcpkg install gtest:x64-windows
```

也可手动构建：

```bat
cmake -S . -B build -G "Visual Studio 16 2019" -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE=../thirdparty_install/vcpkg/scripts/buildsystems/vcpkg.cmake ^
  -DQINGYING_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## 目录

```text
include/qingying/   对外头文件与稳定契约
src/app             EXE：组合根、托盘、热键、交互工作流
src/action          Action 契约与 Dispatcher
src/capture         GDI 区域捕获（DXGI / 截窗 / 中央裁切待实现）
src/overlay         桌面快照遮罩、框选、调区、窗口吸附、选区工具栏、阶段状态、长截图预览
src/ui              选区条 / 标注底栏共用的 ModernToolbar 与 SVG 路径图标
src/annotate        标注文档、引擎、渲染器和就地编辑 Overlay
src/pin             多钉图窗口与捕获排除
src/longshot        记事本选区滚动拼接
src/export          剪贴板与 WIC PNG
src/command         本地口令桩，F8 待实现
src/mcp             MCP Bridge 桩，F9 待实现
tests/              GoogleTest（当前 262 个已发现用例，1 个显式禁用）
docs/               架构、进度、分工与整改文档
```
