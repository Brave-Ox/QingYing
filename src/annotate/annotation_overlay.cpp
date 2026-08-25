#include "qingying/annotate/annotation_overlay.hpp"

#include <algorithm>
#include <utility>

#include "qingying/annotate/annotation_editor_session.hpp"

namespace qingying {

namespace {

const wchar_t kOverlayClassName[] = L"QingYingAnnotationOverlay";

constexpr UINT kButtonConfirmId = 1;
constexpr UINT kButtonCancelId = 2;
constexpr int kButtonWidth = 88;
constexpr int kButtonHeight = 30;
constexpr int kButtonGap = 8;
constexpr int kBarPadding = 6;

// 底部操作条高度：按钮加上下内边距。
int toolbarHeight()
{
  return kButtonHeight + kBarPadding * 2;
}

// 编辑器状态挂在窗口的 GWLP_USERDATA 上，避免全局可变量。
struct EditorWindowData
{
  AnnotationEditorSession session;
  AnnotationCallback callback;
  HWND overlay{nullptr};
  bool confirmed{false};
};

// BeginPaint / EndPaint 配对释放。
class PaintGuard
{
 public:
  explicit PaintGuard(HWND hwnd) : m_hwnd(hwnd)
  {
    m_dc = BeginPaint(hwnd, &m_paint);
  }

  ~PaintGuard()
  {
    if (m_dc != nullptr)
    {
      EndPaint(m_hwnd, &m_paint);
    }
  }

  PaintGuard(const PaintGuard&) = delete;
  PaintGuard& operator=(const PaintGuard&) = delete;

  HDC dc() const
  {
    return m_dc;
  }

 private:
  HWND m_hwnd{nullptr};
  PAINTSTRUCT m_paint{};
  HDC m_dc{nullptr};
};

// 把源图按 1:1 画到客户区左上角。Task 5 只显示原图，尚无标注预览。
void drawSource(HDC hdc, const Image& image)
{
  if (hdc == nullptr || image.empty())
  {
    return;
  }

  BITMAPINFO bmi{};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = image.width;
  bmi.bmiHeader.biHeight = -image.height;  // 顶向下，与 Image 行序一致
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  // 返回值为实际写入的扫描行数；绘制失败只影响这一帧显示，
  // 不改变编辑状态，故此处不做分支处理。
  (void)SetDIBitsToDevice(hdc, 0, 0, static_cast<DWORD>(image.width),
                          static_cast<DWORD>(image.height), 0, 0, 0,
                          static_cast<UINT>(image.height),
                          image.pixels.data(), &bmi, DIB_RGB_COLORS);
}

bool createButtons(HWND hwnd, const Image& source)
{
  const HINSTANCE instance = GetModuleHandleW(nullptr);
  const DWORD style = WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON;
  const int y = source.height + kBarPadding;

  const auto createButton = [&](const wchar_t* label, UINT id, int x) -> HWND
  {
    return CreateWindowExW(0, L"BUTTON", label, style, x, y, kButtonWidth,
                           kButtonHeight, hwnd,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           instance, nullptr);
  };

  const int total = kButtonWidth * 2 + kButtonGap;
  const int first_x = (std::max)(kBarPadding, source.width - total - kBarPadding);

  const HWND confirm = createButton(L"完成", kButtonConfirmId, first_x);
  const HWND cancel = createButton(L"取消", kButtonCancelId,
                                   first_x + kButtonWidth + kButtonGap);
  return confirm != nullptr && cancel != nullptr;
}

void requestClose(EditorWindowData* data, bool confirmed)
{
  if (data == nullptr || data->overlay == nullptr)
  {
    return;
  }
  data->confirmed = confirmed;
  PostMessageW(data->overlay, WM_CLOSE, 0, 0);
}

// 窗口销毁时统一收尾：产出结果并回调。取消路径下 session 已经
// 保证 rendered_image 为空图。
void finishAndNotify(EditorWindowData* data)
{
  if (data == nullptr)
  {
    return;
  }

  AnnotationFinishResult result;
  if (data->confirmed)
  {
    if (!data->session.finishConfirmed(result))
    {
      // 合成失败时退回取消语义，避免把半成品图交给调用方。
      result.cancelled = true;
      result.rendered_image = Image{};
    }
  }
  else
  {
    // 未开始或已结束时返回 false，result 保持默认的 cancelled 空结果。
    (void)data->session.finishCancelled(result);
  }

  if (data->callback)
  {
    data->callback(result);
  }
}

LRESULT CALLBACK editorWndProc(HWND hwnd, UINT msg, WPARAM wparam,
                               LPARAM lparam)
{
  EditorWindowData* data = reinterpret_cast<EditorWindowData*>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));

  switch (msg)
  {
    case WM_NCCREATE:
    {
      const CREATESTRUCTW* cs = reinterpret_cast<const CREATESTRUCTW*>(lparam);
      EditorWindowData* create_data =
          static_cast<EditorWindowData*>(cs->lpCreateParams);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                        reinterpret_cast<LONG_PTR>(create_data));
      if (create_data != nullptr)
      {
        create_data->overlay = hwnd;
      }
      return TRUE;
    }
    case WM_CREATE:
    {
      if (data == nullptr || !createButtons(hwnd, data->session.source()))
      {
        return -1;
      }
      return 0;
    }
    case WM_PAINT:
    {
      const PaintGuard paint(hwnd);
      if (data != nullptr)
      {
        drawSource(paint.dc(), data->session.source());
      }
      return 0;
    }
    case WM_ERASEBKGND:
    {
      // 图片区自绘，避免整窗擦除造成闪烁。
      return 1;
    }
    case WM_COMMAND:
    {
      if (data == nullptr || HIWORD(wparam) != BN_CLICKED)
      {
        return 0;
      }
      if (LOWORD(wparam) == kButtonConfirmId)
      {
        requestClose(data, true);
      }
      else if (LOWORD(wparam) == kButtonCancelId)
      {
        requestClose(data, false);
      }
      return 0;
    }
    case WM_CLOSE:
    {
      DestroyWindow(hwnd);
      return 0;
    }
    case WM_DESTROY:
    {
      finishAndNotify(data);
      PostQuitMessage(0);
      return 0;
    }
    default:
      break;
  }

  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

bool registerEditorClass(HINSTANCE instance)
{
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(WNDCLASSEXW);
  wc.lpfnWndProc = editorWndProc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
  wc.lpszClassName = kOverlayClassName;

  return RegisterClassExW(&wc) != 0 ||
         GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

}  // namespace

bool AnnotationOverlay::show(HWND owner, const Image& source,
                             AnnotationCallback callback)
{
  if (m_visible)
  {
    return false;
  }

  EditorWindowData data;
  if (!data.session.begin(source))
  {
    return false;  // 源图为空
  }
  data.callback = std::move(callback);

  const HINSTANCE instance = GetModuleHandleW(nullptr);
  if (!registerEditorClass(instance))
  {
    return false;
  }

  const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
  RECT rect{0, 0, source.width, source.height + toolbarHeight()};
  if (AdjustWindowRect(&rect, style, FALSE) == FALSE)
  {
    return false;
  }

  const int window_width = rect.right - rect.left;
  const int window_height = rect.bottom - rect.top;
  const int x =
      (std::max)(0, (GetSystemMetrics(SM_CXSCREEN) - window_width) / 2);
  const int y =
      (std::max)(0, (GetSystemMetrics(SM_CYSCREEN) - window_height) / 2);

  const HWND hwnd =
      CreateWindowExW(WS_EX_TOPMOST, kOverlayClassName, L"标注编辑", style, x,
                      y, window_width, window_height, owner, nullptr, instance,
                      &data);
  if (hwnd == nullptr)
  {
    return false;
  }

  m_hwnd = hwnd;
  m_visible = true;

  ShowWindow(hwnd, SW_SHOW);
  SetForegroundWindow(hwnd);

  // 模态循环：与 SelectionOverlay 一致，直到窗口销毁（WM_DESTROY →
  // PostQuitMessage）才返回。Esc 在此拦截，保证焦点在按钮上时同样生效。
  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0)
  {
    if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE)
    {
      requestClose(&data, false);
      continue;
    }
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  m_visible = false;
  m_hwnd = nullptr;
  return true;
}

void AnnotationOverlay::hide()
{
  if (m_hwnd != nullptr)
  {
    PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
  }
}

bool AnnotationOverlay::isVisible() const
{
  return m_visible;
}

}  // namespace qingying
