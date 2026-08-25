// 必须在包含 <Windows.h> 之前定义，确保 DPI 感知相关 API 可用。
#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00
#define NTDDI_VERSION 0x0A000003  // Windows 10 1703（RS2）

#include "qingying/app/application.hpp"

#include <Windows.h>

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/,
                    PWSTR /*lpCmdLine*/, int /*nCmdShow*/) {
  // 声明 Per-Monitor V2 DPI 感知：让遮罩/鼠标与截图统一使用物理像素，
  // 避免高 DPI（125%/150%/200%）下选框与截图内容错位。
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  qingying::Application app(hInstance);
  return app.run();
}
