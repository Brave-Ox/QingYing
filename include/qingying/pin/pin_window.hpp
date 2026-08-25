#pragma once

#include "qingying/action/image.hpp"

#include <Windows.h>

#include <functional>

namespace qingying {

class PinWindow {
 public:
  using ClosedCallback = std::function<void(PinWindow*)>;

  explicit PinWindow(Image image);
  ~PinWindow();

  PinWindow(const PinWindow&) = delete;
  PinWindow& operator=(const PinWindow&) = delete;

  bool show();
  void close();

  HWND hwnd() const { return hwnd_; }
  const Image& image() const { return image_; }

  void setClosedCallback(ClosedCallback callback);

 private:
  static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wparam,
                                     LPARAM lparam);
  static bool registerWindowClass();

  void paint(HDC dc);
  void handleDestroyed();
  void handleSizing(WPARAM edge, RECT* window_rect) const;
  LRESULT hitTest(POINT point) const;
  RECT closeButtonRect() const;

  Image image_;
  HWND hwnd_{nullptr};
  bool closing_{false};
  ClosedCallback closed_callback_;
};

}  // namespace qingying
