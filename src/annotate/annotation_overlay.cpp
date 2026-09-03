#include "qingying/annotate/annotation_overlay.hpp"

#include <algorithm>
#include <memory>
#include <utility>

#include "annotate/annotation_editor_host.hpp"
#include "qingying/annotate/annotation_editor_layout.hpp"
#include "qingying/annotate/annotation_types.hpp"

namespace qingying {

AnnotationOverlay::AnnotationOverlay() = default;

AnnotationOverlay::~AnnotationOverlay()
{
  closeSilently();
}

bool AnnotationOverlay::show(HWND owner, const Image& source,
                             AnnotationCallback callback)
{
  return showInPlace(owner, source, -1, -1, std::move(callback));
}

bool AnnotationOverlay::showInPlace(HWND owner, const Image& source,
                                    int screen_x, int screen_y,
                                    AnnotationCallback callback)
{
  if (m_visible)
  {
    return false;
  }

  if (data_ != nullptr)
  {
    if (!data_->window().m_window_destroyed)
    {
      return false;
    }
    data_.reset();
  }

  auto data = std::make_unique<AnnotationEditorHost>();
  if (!data->core().m_session.begin(source))
  {
    return false;
  }
  data->window().m_client_width = annotationEditorClientWidth(source.width);
  data->window().m_image_origin_x = AnnotationEditorFrameInsetPx;
  data->window().m_image_origin_y = annotationEditorTopInset();
  data->window().m_image_screen_x = data->window().m_image_origin_x;
  data->window().m_image_screen_y = data->window().m_image_origin_y;
  data->core().m_controller.setCanvasSize(source.width, source.height);
  data->core().m_controller.setTool(AnnotationTool::None);
  data->window().m_callback = std::move(callback);
  data->window().m_owner_hwnd = &m_hwnd;
  data->window().m_owner_visible = &m_visible;
  data->window().m_owner_suppress_callback = &m_suppress_callback;

  const HINSTANCE instance = GetModuleHandleW(nullptr);
  if (!registerEditorClass(instance))
  {
    return false;
  }

  const int desk_left = GetSystemMetrics(SM_XVIRTUALSCREEN);
  const int desk_top = GetSystemMetrics(SM_YVIRTUALSCREEN);
  const int desk_width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
  const int desk_height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
  if (screen_x < 0 || screen_y < 0)
  {
    data->window().m_image_screen_x =
        (std::max)(0, (GetSystemMetrics(SM_CXSCREEN) - source.width) / 2);
    data->window().m_image_screen_y =
        (std::max)(0, (GetSystemMetrics(SM_CYSCREEN) - source.height) / 2);
  }
  else
  {
    data->window().m_image_screen_x = screen_x;
    data->window().m_image_screen_y = screen_y;
  }
  const AnnotationEditorVirtualDesktopPlacement place =
      annotationEditorVirtualDesktopPlacement(
          data->window().m_image_screen_x, data->window().m_image_screen_y, desk_left, desk_top,
          desk_width, desk_height);
  const int x = place.window_x;
  const int y = place.window_y;
  const int window_width = place.window_width;
  const int window_height = place.window_height;
  data->window().m_image_origin_x = place.image_origin_x;
  data->window().m_image_origin_y = place.image_origin_y;
  data->window().m_client_width = place.window_width;

  const DWORD style = WS_POPUP | WS_VISIBLE;
  const HWND hwnd = CreateWindowExW(
      WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kOverlayClassName, L"",
      style, x, y, window_width, window_height, owner, nullptr, instance,
      data.get());
  if (hwnd == nullptr)
  {
    return false;
  }
  data_ = std::move(data);
  m_hwnd = hwnd;
  m_visible = true;

  ShowWindow(hwnd, SW_SHOW);
  SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
  SetForegroundWindow(hwnd);
  SetFocus(hwnd);
  return true;
}

void AnnotationOverlay::hide()
{
  if (m_hwnd != nullptr)
  {
    m_suppress_callback = false;
    const DWORD window_thread = GetWindowThreadProcessId(m_hwnd, nullptr);
    if (window_thread == GetCurrentThreadId())
    {
      SendMessageW(m_hwnd, WM_QINGYING_ANNOTATION_CANCEL, 0, 0);
    }
    else
    {
      PostMessageW(m_hwnd, WM_QINGYING_ANNOTATION_CANCEL, 0, 0);
    }
  }
}

void AnnotationOverlay::closeSilently()
{
  if (m_hwnd != nullptr)
  {
    m_suppress_callback = true;
    const DWORD window_thread = GetWindowThreadProcessId(m_hwnd, nullptr);
    if (window_thread == GetCurrentThreadId())
    {
      SendMessageW(m_hwnd, WM_QINGYING_ANNOTATION_CANCEL, 0, 0);
    }
    else
    {
      PostMessageW(m_hwnd, WM_QINGYING_ANNOTATION_CANCEL, 0, 0);
    }
  }
}

bool AnnotationOverlay::isVisible() const
{
  return m_visible;
}

}  // namespace qingying
