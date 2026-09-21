# 轻映 QingYing

Windows 原生轻量截图工具（C++17 + Win32，以 EXE 为主并带受控长截图插件），也规划作为 Agent 的本地截图能力提供者（本地口令 / MCP）。

> 当前提交基线：`master` / `40d22e21`（104 条提交）
> 文档同步日期：2026-09-07
> 当前 Release 构建目录的 CTest：382 个用例执行，378 个通过，4 个为当前环境下既有 `BitBlt` 失败；本次重新运行 `build.bat Release test` 在 MSBuild `FileTracker` 阶段因环境权限 `E_ACCESSDENIED` 未进入编译

## 当前能力

- 已打通：全局热键 → 桌面快照遮罩 → 自由框选 / 八点调整 / 窗口吸附 → 区域截图 → 复制 / PNG 保存 / 钉图。
- 已实现：多 Pin、自动避让排布、拖动与等比例缩放、独立复制 / 保存、截图时临时隐藏 Pin。
- 已接入：选区“编辑”进入就地标注，支持矩形、椭圆、箭头、画笔、文字、马赛克、撤销；确认后自动复制，并恢复同一选区的结果操作条，可继续保存、钉图或再次编辑。
- 已调整：选区条抽为 `SelectionToolbar`，交互阶段由 `OverlayPhase` 显式管理；底层仍复用白色圆角 `ModernToolbar`、GDI+、SVG 路径图标和中文 Tooltip；Selection / Annotation Overlay 已改为非模态窗口。
- 已接入：记事本、文件资源管理器和 Chromium 浏览器通用长截图 profile，支持固定选区滚动拼接、实时预览、暂停 / 继续、停止和失败清理；Chrome / Edge / Brave 仍需真实窗口验收。
- 已接入：F8 本地口令窗口，支持 `Ctrl+Alt+K` 或托盘“口令…”入口；可执行窗口截图、中心裁剪、复制、钉图、状态和带路径保存，窗口截图可串接“并复制 / 并钉图”。F9 MCP 仍需真实安装包与客户端验收；F3 仍需真实窗口 / DPI 人工验收。
- 待验收：Chromium 浏览器、资源管理器、双屏 / 混合 DPI、Pin 和长截图的真实人工闭环。

## 文档

- [开发进度](./docs/PROGRESS.md) — 当前代码、测试和人工验收状态
- [开发清单](./轻映-QingYing-开发清单.md) — F1～F9 范围、指标和验收项
- [当前架构](./docs/architecture.md) — 已落地的模块、调用链和已知例外
- [架构如何调整](./docs/架构如何调整.md) — 不推倒重来的分阶段整改方案
- [系统架构与耦合度分析](./docs/系统架构与耦合度分析.md) — 基于当前 HEAD 的耦合审计
- [F9 MCP 架构与实施方案](./docs/F9-MCP-架构与实施方案.md) — MCP 工具、结果租约、UI 调度和遮罩可见性方案
- [F1 / F2 / F3 / Pin / 长截图分工指南](./docs/F1与标注与钉图初版分工指南.md) — 协作边界和集成契约

## 架构要点

- **一个 EXE + 12 个 static lib + 3 个长截图插件 DLL**，按 action / capture / export / ui / overlay / annotate / pin / longshot / workflow / app_handlers / command / mcp 切分；插件部署到 `plugins/longshot`。
- `app/Application` 是组合根，拥有托盘、全局热键和顶层消息循环；`CaptureWorkflow` 编排选区、标注与结果动作，`LongShotController` 管理交互式长截图生命周期。
- 对外可调用的业务动作以 `ActionDispatcher` 为统一入口；GUI 多步流程统一进入 `CaptureWorkflow`。
- `Image` 统一为 BGRA32、行优先、物理像素；`ResultStore` 是当前结果的唯一发布入口，`ResultActionService` 统一 Copy / Save / Pin，`CaptureSession` 只保留为兼容门面。
- 当前区域捕获使用 GDI `BitBlt`；DXGI 仅保留链接和后续实现位置。
- 已完成第一批架构收敛：`SelectionToolbar`、`OverlayPhase`、`OverlayRenderer`、`CaptureWorkflow`、`LongShotController`、编辑源图上移、`overlay → capture / annotate` 依赖移除、PIMPL RAII、ResultStore / ResultActionService、类型化 Action、命名坐标协议、长截图插件 ABI 和 Overlay 非模态消息循环。后续重点是 F8/F9 外部契约与真实环境验收。

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

2026-09-07 当前 Release 构建目录结果：

- `qingying.exe`：313,856 字节（约 0.30 MiB，仅指当前 EXE 文件）；另有 3 个长截图插件 DLL，完整交付包仍需复测；
- CTest 执行 382 个用例：378 个通过，4 个当前环境下既有 `BitBlt` 用例失败；当前测试发现中没有显式禁用的窗口用例；
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
src/app             EXE 组合根、托盘、热键，以及 qingying_workflow 实现
src/action          Action 契约与 Dispatcher
src/capture         GDI 区域捕获（DXGI / 截窗 / 中央裁切待实现）
src/overlay         桌面快照遮罩、框选、调区、窗口吸附、选区工具栏、阶段状态、OverlayRenderer、长截图预览
src/ui              选区条 / 标注底栏共用的 ModernToolbar 与 SVG 路径图标
src/annotate        标注文档、引擎、渲染器和就地编辑 Overlay
src/pin             多钉图窗口与捕获排除
src/longshot        通用长截图引擎、profile registry、插件 host 与 Chromium / Explorer / Notepad 适配
src/export          剪贴板与 WIC PNG
src/command         本地口令解析与 Action 计划
src/mcp             MCP Bridge 桩，F9 待实现
tests/              GoogleTest（当前 382 个用例，4 个环境相关 BitBlt 失败）
docs/               架构、进度、分工与整改文档
```
