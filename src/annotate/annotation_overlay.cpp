#include "qingying/annotate/annotation_overlay.hpp"

#include <algorithm>
#include <utility>

#include "qingying/annotate/annotation_editor_layout.hpp"
#include "qingying/annotate/annotation_editor_session.hpp"
#include "qingying/annotate/annotation_interaction_controller.hpp"
#include "qingying/annotate/annotation_renderer.hpp"

namespace qingying {

namespace {

const wchar_t kOverlayClassName[] = L"QingYingAnnotationOverlay";

constexpr UINT kButtonConfirmId = 1;
constexpr UINT kButtonCancelId = 2;
constexpr UINT kButtonRectId = 3;
constexpr UINT kButtonArrowId = 4;
constexpr UINT kButtonPenId = 5;
constexpr UINT kButtonUndoId = 6;

struct EditorWindowData
{
  AnnotationEditorSession session;
  AnnotationInteractionController controller;
  AnnotationRenderer renderer;
  AnnotationCallback callback;
  HWND overlay{nullptr};
  bool confirmed{false};
  int client_width{0};
};

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

void blitImage(HDC hdc, const Image& image)
{
  if (hdc == nullptr || image.empty())
  {
    return;
  }

  BITMAPINFO bmi{};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = image.width;
  bmi.bmiHeader.biHeight = -image.height;
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  (void)SetDIBitsToDevice(hdc, 0, 0, static_cast<DWORD>(image.width),
                          static_cast<DWORD>(image.height), 0, 0, 0,
                          static_cast<UINT>(image.height),
                          image.pixels.data(), &bmi, DIB_RGB_COLORS);
}

void invalidateImageArea(HWND hwnd, const Image& source)
{
  if (hwnd == nullptr || source.empty())
  {
    return;
  }
  RECT rect{0, 0, source.width, source.height};
  InvalidateRect(hwnd, &rect, FALSE);
}

void paintEditor(EditorWindowData* data, HDC hdc)
{
  if (data == nullptr || hdc == nullptr)
  {
    return;
  }

  Image composed;
  const Annotation* preview =
      data->controller.hasPreview() ? &data->controller.preview() : nullptr;
  if (!data->renderer.rasterize(data->session.source(),
                                data->session.engine().document(), preview,
                                composed))
  {
    return;
  }
  blitImage(hdc, composed);
}

HWND createChildButton(HWND parent, const wchar_t* label, UINT id, int x,
                       int y)
{
  return CreateWindowExW(
      0, L"BUTTON", label, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, x, y,
      AnnotationEditorButtonWidth, AnnotationEditorButtonHeight, parent,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
      GetModuleHandleW(nullptr), nullptr);
}

bool createButtons(HWND hwnd, const Image& source, int client_width)
{
  const int y = source.height + AnnotationEditorBarPadding;
  int x = AnnotationEditorBarPadding;

  const HWND rect = createChildButton(hwnd, L"矩形", kButtonRectId, x, y);
  x += AnnotationEditorButtonWidth + AnnotationEditorButtonGap;
  const HWND arrow = createChildButton(hwnd, L"箭头", kButtonArrowId, x, y);
  x += AnnotationEditorButtonWidth + AnnotationEditorButtonGap;
  const HWND pen = createChildButton(hwnd, L"画笔", kButtonPenId, x, y);
  x += AnnotationEditorButtonWidth + AnnotationEditorButtonGap;
  const HWND undo = createChildButton(hwnd, L"撤销", kButtonUndoId, x, y);

  const int action_total =
      AnnotationEditorButtonWidth * 2 + AnnotationEditorButtonGap;
  const int confirm_x =
      (std::max)(x + AnnotationEditorButtonWidth + AnnotationEditorButtonGap,
                 client_width - action_total - AnnotationEditorBarPadding);
  const HWND confirm =
      createChildButton(hwnd, L"完成", kButtonConfirmId, confirm_x, y);
  const HWND cancel =
      createChildButton(hwnd, L"取消", kButtonCancelId,
                        confirm_x + AnnotationEditorButtonWidth +
                            AnnotationEditorButtonGap,
                        y);

  return rect != nullptr && arrow != nullptr && pen != nullptr &&
         undo != nullptr && confirm != nullptr && cancel != nullptr;
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
      result.cancelled = true;
      result.rendered_image = Image{};
    }
  }
  else
  {
    (void)data->session.finishCancelled(result);
  }

  if (data->callback)
  {
    data->callback(result);
  }
}

bool pointInImageArea(const Image& source, int x, int y)
{
  return x >= 0 && y >= 0 && x < source.width && y < source.height;
}

void handleToolCommand(EditorWindowData* data, UINT id)
{
  if (data == nullptr)
  {
    return;
  }

  switch (id)
  {
    case kButtonRectId:
      data->controller.setTool(AnnotationTool::Rectangle);
      break;
    case kButtonArrowId:
      data->controller.setTool(AnnotationTool::Arrow);
      break;
    case kButtonPenId:
      data->controller.setTool(AnnotationTool::Pen);
      break;
    case kButtonUndoId:
      if (data->controller.undo(data->session.engine()))
      {
        invalidateImageArea(data->overlay, data->session.source());
      }
      break;
    default:
      break;
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
      if (data == nullptr ||
          !createButtons(hwnd, data->session.source(), data->client_width))
      {
        return -1;
      }
      return 0;
    }
    case WM_PAINT:
    {
      const PaintGuard paint(hwnd);
      paintEditor(data, paint.dc());
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_LBUTTONDOWN:
    {
      if (data == nullptr)
      {
        return 0;
      }
      const int x = static_cast<int>(static_cast<short>(LOWORD(lparam)));
      const int y = static_cast<int>(static_cast<short>(HIWORD(lparam)));
      if (!pointInImageArea(data->session.source(), x, y))
      {
        return 0;
      }
      if (data->controller.beginStroke(static_cast<float>(x),
                                       static_cast<float>(y)))
      {
        SetCapture(hwnd);
        invalidateImageArea(hwnd, data->session.source());
      }
      return 0;
    }
    case WM_MOUSEMOVE:
    {
      if (data == nullptr || !data->controller.isDrawing())
      {
        return 0;
      }
      const int x = static_cast<int>(static_cast<short>(LOWORD(lparam)));
      const int y = static_cast<int>(static_cast<short>(HIWORD(lparam)));
      data->controller.updateStroke(static_cast<float>(x),
                                    static_cast<float>(y));
      invalidateImageArea(hwnd, data->session.source());
      return 0;
    }
    case WM_LBUTTONUP:
    {
      if (data == nullptr || !data->controller.isDrawing())
      {
        return 0;
      }
      const int x = static_cast<int>(static_cast<short>(LOWORD(lparam)));
      const int y = static_cast<int>(static_cast<short>(HIWORD(lparam)));
      data->controller.updateStroke(static_cast<float>(x),
                                    static_cast<float>(y));
      (void)data->controller.endStroke(data->session.engine());
      ReleaseCapture();
      invalidateImageArea(hwnd, data->session.source());
      return 0;
    }
    case WM_COMMAND:
    {
      if (data == nullptr || HIWORD(wparam) != BN_CLICKED)
      {
        return 0;
      }
      const UINT id = LOWORD(wparam);
      if (id == kButtonConfirmId)
      {
        requestClose(data, true);
      }
      else if (id == kButtonCancelId)
      {
        requestClose(data, false);
      }
      else
      {
        handleToolCommand(data, id);
      }
      return 0;
    }
    case WM_CLOSE:
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      finishAndNotify(data);
      PostQuitMessage(0);
      return 0;
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
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
  wc.lpszClassName = kOverlayClassName;

  return RegisterClassExW(&wc) != 0 ||
         GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

bool isCtrlZ(const MSG& msg)
{
  if (msg.message != WM_KEYDOWN || msg.wParam != 'Z')
  {
    return false;
  }
  return (GetKeyState(VK_CONTROL) & 0x8000) != 0;
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
    return false;
  }
  data.client_width = annotationEditorClientWidth(source.width);
  data.controller.setCanvasSize(source.width, source.height);
  data.controller.setTool(AnnotationTool::Rectangle);
  data.callback = std::move(callback);

  const HINSTANCE instance = GetModuleHandleW(nullptr);
  if (!registerEditorClass(instance))
  {
    return false;
  }

  const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
  RECT rect{0, 0, data.client_width,
            source.height + annotationEditorToolbarHeight()};
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

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0)
  {
    if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE)
    {
      requestClose(&data, false);
      continue;
    }
    if (isCtrlZ(msg))
    {
      if (data.controller.undo(data.session.engine()))
      {
        invalidateImageArea(hwnd, data.session.source());
      }
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
