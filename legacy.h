#pragma once
#include <cstdint>

using i16 = std::int16_t;

#if defined(_WIN32)
  #include <windows.h>
  using WindowHandle = HWND;
  using DeviceContextHandle = HDC;
#else
  using WindowHandle = void*;
  using DeviceContextHandle = void*;
#endif

extern WindowHandle g_hdc2;
extern DeviceContextHandle g_hwnd;

struct RectI16 {
  i16 left;
  i16 top;
  i16 right;
  i16 bottom;
};

#if defined(_WIN32)
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>

  using WindowHandle = HWND;

  inline void INVALIDATERECT(WindowHandle hwnd, const RectI16* rc, i16 erase) {
    const RECT* wrc = nullptr;
    RECT tmp{};
    if (rc) {
      tmp.left = rc->left;
      tmp.top = rc->top;
      tmp.right = rc->right;
      tmp.bottom = rc->bottom;
      wrc = &tmp;
    }
    ::InvalidateRect(hwnd, wrc, erase != 0);
  }

  inline void UPDATEWINDOW(WindowHandle hwnd) {
    ::UpdateWindow(hwnd);
  }
#else
  using WindowHandle = void*;
  extern bool g_needsRedraw;

  inline void INVALIDATERECT(WindowHandle, const RectI16*, i16) {
    g_needsRedraw = true;
  }

  inline void UPDATEWINDOW(WindowHandle) {}
#endif
