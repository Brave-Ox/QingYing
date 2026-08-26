#include "qingying/app/application.hpp"

#include <Windows.h>

#include <memory>

namespace {

using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
using SetProcessDpiAwarenessFn = HRESULT(WINAPI*)(int);

// LoadLibraryW 取得的模块句柄必须 FreeLibrary 归还。
struct ModuleFreer
{
  void operator()(HMODULE module) const noexcept
  {
    if (module != nullptr)
    {
      FreeLibrary(module);
    }
  }
};

// 必须在创建任何窗口之前调用，否则窗口会沿用进程默认的 DPI 虚拟化。
//
// 缺少该声明时（系统缩放 150% 等场景）：鼠标与窗口坐标是被虚拟化的逻辑像素，
// 而屏幕 DC 的 BitBlt 按物理像素取样，选区与实际截到的区域会整体偏移。
// Image 契约要求物理像素，因此这里统一声明 Per-Monitor V2。
void enablePerMonitorDpiAwareness()
{
  const HMODULE user32 = GetModuleHandleW(L"user32.dll");
  if (user32 != nullptr)
  {
    const auto set_context =
        reinterpret_cast<SetProcessDpiAwarenessContextFn>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
    if (set_context != nullptr &&
        set_context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != FALSE)
    {
      return;
    }
  }

  // Win10 1703 之前：退回 Shcore 的 per-monitor 声明。
  constexpr int ProcessPerMonitorDpiAware = 2;
  const std::unique_ptr<HINSTANCE__, ModuleFreer> shcore(
      LoadLibraryW(L"shcore.dll"));
  if (shcore != nullptr)
  {
    const auto set_awareness = reinterpret_cast<SetProcessDpiAwarenessFn>(
        GetProcAddress(shcore.get(), "SetProcessDpiAwareness"));
    if (set_awareness != nullptr && SUCCEEDED(set_awareness(
                                        ProcessPerMonitorDpiAware)))
    {
      return;
    }
  }

  // Win8.1 之前：只能声明系统级 DPI 感知，仍优于完全虚拟化。
  (void)SetProcessDPIAware();
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/,
                    PWSTR /*lpCmdLine*/, int /*nCmdShow*/) {
  enablePerMonitorDpiAwareness();

  qingying::Application app(hInstance);
  return app.run();
}
