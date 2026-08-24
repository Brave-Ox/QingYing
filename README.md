# 轻映 QingYing

Windows 原生轻量截图工具（C++17 + Win32，单 EXE）。  
也作为 Agent 的本地截图能力提供者（MCP / Skill）。

## 文档

- [开发进度](./docs/PROGRESS.md) — 当前进度快照（已完成 / 待办 / 下一步建议）
- [开发清单](./轻映-QingYing-开发清单.md) — 功能、指标、验收
- [架构说明（方案 B）](./docs/architecture.md) — 多 static lib + ActionDispatcher

## 架构要点

- **一个 EXE**，内部多个 static lib，按模块分工
- **Win32 消息循环只在 `app`**
- **业务能力只走 `ActionDispatcher`**（GUI / 口令 / MCP 同源）
- 引擎类（Capture / LongShot / MCP）建议 **PIMPL**；Action 契约不用 PIMPL

## 构建

双击或在项目根目录执行：

```bat
build.bat
build.bat Debug
build.bat Release clean
build.bat test          :: 编译并跑 GoogleTest
build.bat notest        :: 不编测试
```

产物：`build/bin/Release/qingying.exe`  
测试：`build/bin/Release/qingying_tests.exe`

GoogleTest 使用仓库同级目录的 vcpkg（本地依赖，不入库）：

```text
../thirdparty_install/vcpkg
```

（即仓库父目录下 `thirdparty_install/vcpkg`，需已 `vcpkg install gtest:x64-windows`；
vcpkg 位于仓库同级目录，不入版本控制，clone 后需自行准备）

也可手动：

```bat
cmake -S . -B build -G "Visual Studio 16 2019" -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE=../thirdparty_install/vcpkg/scripts/buildsystems/vcpkg.cmake ^
  -DQINGYING_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## 目录

```text
include/qingying/   对外头文件
src/app             EXE：托盘 / 热键 / 组装
src/action          Action 契约与 Dispatcher
src/capture         屏幕捕获（PIMPL）
src/overlay         框选遮罩
src/annotate        标注
src/pin             钉图
src/longshot        长截图
src/export          剪贴板 / 存盘
src/command         本地口令
src/mcp             MCP Bridge（PIMPL）
tests/              GoogleTest 单元测试
docs/               架构文档
```
