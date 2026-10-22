#include <catch2/catch_test_macros.hpp>

#include "capture_gdi.h"
#include "rfb_server.h"

using namespace pvnc;

static Frame solid(int w, int h, uint8_t v) {
  Frame f;
  f.width = w; f.height = h; f.stride = w * 4;
  f.bgra.assign(size_t(w) * h * 4, v);
  return f;
}

TEST_CASE("computeDirtyRect: identical frames are empty", "[rfb]") {
  auto a = solid(64, 48, 0x11);
  auto b = solid(64, 48, 0x11);
  CHECK(computeDirtyRect(a, b).empty);
}

TEST_CASE("computeDirtyRect: size change dirties whole frame", "[rfb]") {
  auto a = solid(32, 32, 0);
  auto b = solid(64, 48, 0);
  auto d = computeDirtyRect(a, b);
  REQUIRE_FALSE(d.empty);
  CHECK(d.x == 0); CHECK(d.y == 0);
  CHECK(d.w == 64); CHECK(d.h == 48);
}

TEST_CASE("computeDirtyRect: a single changed pixel yields a 1x1 box", "[rfb]") {
  auto a = solid(64, 48, 0x20);
  auto b = a;
  // change pixel (x=10, y=5)
  size_t off = (size_t(5) * b.stride) + 10 * 4;
  b.bgra[off] = 0x99;
  auto d = computeDirtyRect(a, b);
  REQUIRE_FALSE(d.empty);
  CHECK(d.x == 10); CHECK(d.y == 5);
  CHECK(d.w == 1); CHECK(d.h == 1);
}

TEST_CASE("computeDirtyRect: bounding box spans changed region", "[rfb]") {
  auto a = solid(100, 100, 0);
  auto b = a;
  auto set = [&](int x, int y) { b.bgra[size_t(y) * b.stride + x * 4] = 0xFF; };
  set(20, 30);
  set(60, 70);
  auto d = computeDirtyRect(a, b);
  REQUIRE_FALSE(d.empty);
  CHECK(d.x == 20); CHECK(d.y == 30);
  CHECK(d.w == 41);  // 60-20+1
  CHECK(d.h == 41);  // 70-30+1
}
