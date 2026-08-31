#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace qingying {

// 持有 GDI 对象，析构时 DeleteObject。空句柄为 no-op。
class GdiObject
{
 public:
  GdiObject() = default;

  explicit GdiObject(HGDIOBJ obj)
      : m_obj(obj)
  {
  }

  ~GdiObject()
  {
    reset();
  }

  GdiObject(GdiObject&& other) noexcept
      : m_obj(other.m_obj)
  {
    other.m_obj = nullptr;
  }

  GdiObject& operator=(GdiObject&& other) noexcept
  {
    if (this != &other)
    {
      reset();
      m_obj = other.m_obj;
      other.m_obj = nullptr;
    }
    return *this;
  }

  GdiObject(const GdiObject&) = delete;
  GdiObject& operator=(const GdiObject&) = delete;

  void reset(HGDIOBJ obj = nullptr)
  {
    if (m_obj != nullptr)
    {
      DeleteObject(m_obj);
    }
    m_obj = obj;
  }

  HGDIOBJ get() const
  {
    return m_obj;
  }

  explicit operator bool() const
  {
    return m_obj != nullptr;
  }

  HFONT asFont() const
  {
    return static_cast<HFONT>(m_obj);
  }

  HBRUSH asBrush() const
  {
    return static_cast<HBRUSH>(m_obj);
  }

  HPEN asPen() const
  {
    return static_cast<HPEN>(m_obj);
  }

 private:
  HGDIOBJ m_obj{nullptr};
};

class PaintGuard
{
 public:
  explicit PaintGuard(HWND hwnd)
      : m_hwnd(hwnd)
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

class SelectGuard
{
 public:
  SelectGuard(HDC hdc, HGDIOBJ obj)
      : m_hdc(hdc), m_old(nullptr)
  {
    if (hdc != nullptr && obj != nullptr)
    {
      m_old = SelectObject(hdc, obj);
    }
  }

  ~SelectGuard()
  {
    if (m_hdc != nullptr && m_old != nullptr)
    {
      SelectObject(m_hdc, m_old);
    }
  }

  SelectGuard(const SelectGuard&) = delete;
  SelectGuard& operator=(const SelectGuard&) = delete;

 private:
  HDC m_hdc{nullptr};
  HGDIOBJ m_old{nullptr};
};

}  // namespace qingying
