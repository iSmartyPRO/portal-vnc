#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include "capture_gdi.h"

using namespace pvnc;

TEST_CASE("grab returns a frame matching screen metrics", "[capture]") {
  GdiCapture cap;
  int sw = GetSystemMetrics(SM_CXSCREEN);
  int sh = GetSystemMetrics(SM_CYSCREEN);
  CHECK(cap.width() == sw);
  CHECK(cap.height() == sh);

  Frame f;
  REQUIRE(cap.grab(f));
  CHECK(f.width == sw);
  CHECK(f.height == sh);
  CHECK(f.stride == sw * 4);
  CHECK(f.bgra.size() == static_cast<size_t>(f.stride) * sh);

  // A real desktop capture is not entirely zero.
  bool anyNonZero = false;
  for (size_t i = 0; i < f.bgra.size(); i += 997) {
    if (f.bgra[i] != 0) { anyNonZero = true; break; }
  }
  CHECK(anyNonZero);
}
