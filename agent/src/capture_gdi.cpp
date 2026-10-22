#include "capture_gdi.h"

namespace pvnc {

GdiCapture::GdiCapture() {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  screenDc_ = GetDC(nullptr);
  memDc_ = CreateCompatibleDC(screenDc_);
  ensureSized();
}

GdiCapture::~GdiCapture() {
  if (dib_) DeleteObject(dib_);
  if (memDc_) DeleteDC(memDc_);
  if (screenDc_) ReleaseDC(nullptr, screenDc_);
}

void GdiCapture::ensureSized() {
  int w = GetSystemMetrics(SM_CXSCREEN);
  int h = GetSystemMetrics(SM_CYSCREEN);
  if (w == width_ && h == height_ && dib_) return;

  width_ = w;
  height_ = h;
  if (dib_) {
    DeleteObject(dib_);
    dib_ = nullptr;
    bits_ = nullptr;
  }
  BITMAPINFO bmi{};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = width_;
  bmi.bmiHeader.biHeight = -height_;  // top-down
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;
  dib_ = CreateDIBSection(memDc_, &bmi, DIB_RGB_COLORS, &bits_, nullptr, 0);
}

bool GdiCapture::grab(Frame& out) {
  ensureSized();
  if (!dib_ || !bits_) return false;

  HGDIOBJ old = SelectObject(memDc_, dib_);
  BOOL ok = BitBlt(memDc_, 0, 0, width_, height_, screenDc_, 0, 0, SRCCOPY | CAPTUREBLT);
  // Overlay the cursor so the operator sees where the pointer is.
  if (ok) {
    CURSORINFO ci{};
    ci.cbSize = sizeof(ci);
    if (GetCursorInfo(&ci) && (ci.flags & CURSOR_SHOWING)) {
      ICONINFO ii{};
      if (GetIconInfo(ci.hCursor, &ii)) {
        DrawIconEx(memDc_, ci.ptScreenPos.x - ii.xHotspot, ci.ptScreenPos.y - ii.yHotspot,
                   ci.hCursor, 0, 0, 0, nullptr, DI_NORMAL);
        if (ii.hbmColor) DeleteObject(ii.hbmColor);
        if (ii.hbmMask) DeleteObject(ii.hbmMask);
      }
    }
  }
  SelectObject(memDc_, old);
  if (!ok) return false;

  out.width = width_;
  out.height = height_;
  out.stride = width_ * 4;
  out.bgra.resize(static_cast<size_t>(out.stride) * height_);
  memcpy(out.bgra.data(), bits_, out.bgra.size());
  return true;
}

}  // namespace pvnc
