#include "input.h"

#include <windows.h>

namespace pvnc {

namespace {

// Absolute mouse coordinates are 0..65535 across the primary screen.
void sendMouse(DWORD flags, int absX = 0, int absY = 0, DWORD mouseData = 0) {
  INPUT in{};
  in.type = INPUT_MOUSE;
  in.mi.dwFlags = flags;
  in.mi.dx = absX;
  in.mi.dy = absY;
  in.mi.mouseData = mouseData;
  SendInput(1, &in, sizeof(in));
}

void sendKey(WORD vk, bool down) {
  INPUT in{};
  in.type = INPUT_KEYBOARD;
  in.ki.wVk = vk;
  in.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
  SendInput(1, &in, sizeof(in));
}

// Maps an X11 keysym to a Windows virtual-key code. Covers the common set used
// interactively; printable ASCII is handled via VkKeyScan.
WORD keysymToVk(uint32_t ks) {
  switch (ks) {
    case 0xFF08: return VK_BACK;
    case 0xFF09: return VK_TAB;
    case 0xFF0D: return VK_RETURN;
    case 0xFF1B: return VK_ESCAPE;
    case 0xFF63: return VK_INSERT;
    case 0xFFFF: return VK_DELETE;
    case 0xFF50: return VK_HOME;
    case 0xFF57: return VK_END;
    case 0xFF55: return VK_PRIOR;
    case 0xFF56: return VK_NEXT;
    case 0xFF51: return VK_LEFT;
    case 0xFF52: return VK_UP;
    case 0xFF53: return VK_RIGHT;
    case 0xFF54: return VK_DOWN;
    case 0xFFE1: case 0xFFE2: return VK_SHIFT;
    case 0xFFE3: case 0xFFE4: return VK_CONTROL;
    case 0xFFE9: case 0xFFEA: return VK_MENU;
    case 0xFFE7: case 0xFFE8: return VK_LWIN;
    case 0xFF8D: return VK_RETURN;  // KP_Enter
    case 0x0020: return VK_SPACE;
    default: break;
  }
  if (ks >= 0xFFBE && ks <= 0xFFC9) return (WORD)(VK_F1 + (ks - 0xFFBE));  // F1..F12
  if (ks < 0x100) {
    SHORT s = VkKeyScanW((wchar_t)ks);
    if (s != -1) return (WORD)(s & 0xFF);
  }
  return 0;
}

}  // namespace

InputInjector::InputInjector(int w, int h) : w_(w > 0 ? w : 1), h_(h > 0 ? h : 1) {}

void InputInjector::pointer(uint8_t buttons, int x, int y) {
  int absX = (int)((long long)x * 65535 / w_);
  int absY = (int)((long long)y * 65535 / h_);
  sendMouse(MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE, absX, absY);

  uint8_t changed = buttons ^ lastButtons_;
  if (changed & 0x01) sendMouse((buttons & 0x01) ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP);
  if (changed & 0x02) sendMouse((buttons & 0x02) ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP);
  if (changed & 0x04) sendMouse((buttons & 0x04) ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP);
  if (buttons & 0x08) sendMouse(MOUSEEVENTF_WHEEL, 0, 0, (DWORD)WHEEL_DELTA);
  if (buttons & 0x10) sendMouse(MOUSEEVENTF_WHEEL, 0, 0, (DWORD)(-WHEEL_DELTA));
  lastButtons_ = buttons;
}

void InputInjector::key(uint32_t keysym, bool down) {
  WORD vk = keysymToVk(keysym);
  if (vk) sendKey(vk, down);
}

}  // namespace pvnc
