#pragma once

#include "qingying/action/image.hpp"
#include "qingying/action/types.hpp"

#include <Windows.h>

#include <functional>

namespace qingying {

class PinWindow {
 public:
  using ClosedCallback = std::function<void(PinWindow*)>;
  using ImageActionCallback = std::function<ActionResult(const Image&)>;

  explicit PinWindow(Image image);
  ~PinWindow();

  PinWindow(const PinWindow&) = delete;
  PinWindow& operator=(const PinWindow&) = delete;

  bool show(int x, int y);
  void close();

  HWND hwnd() const { return hwnd_; }
  const Image& image() const { return image_; }

  // 按图片比例计算钉图初始客户区尺寸（max 800x600 / min 160x120，保持宽高比）。
  // 供 PinManager 在创建前估算窗口尺寸以计算不重叠的摆放位置。
  static void computeInitialClientSize(const Image& image, int& width,
                                       int& height);

  void setClosedCallback(ClosedCallback callback);
  void setActionCallbacks(ImageActionCallback copy_callback,
                          ImageActionCallback save_callback);

 private:
  static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wparam,
                                     LPARAM lparam);
  static bool registerWindowClass();

  void paint(HDC dc);
  void handleDestroyed();
  void handleSizing(WPARAM edge, RECT* window_rect) const;
  void handleMouseWheel(short delta);
  LRESULT hitTest(POINT point) const;
  RECT closeButtonRect() const;
  void showContextMenu(POINT screen_point);
  void handleImageAction(const ImageActionCallback& callback,
                         const wchar_t* error_title);

  Image image_;
  HWND hwnd_{nullptr};
  bool closing_{false};
  ClosedCallback closed_callback_;
  ImageActionCallback copy_callback_;
  ImageActionCallback save_callback_;
};

}  // namespace qingying
