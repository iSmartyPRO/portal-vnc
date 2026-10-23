#include "paths.h"

#include <windows.h>

namespace pvnc {

std::wstring exePathW() {
  wchar_t buf[MAX_PATH];
  DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  return std::wstring(buf, n);
}

std::wstring exeDirW() {
  std::wstring w = exePathW();
  auto slash = w.find_last_of(L"\\/");
  return (slash == std::wstring::npos) ? L"." : w.substr(0, slash);
}

std::string exeDirUtf8() {
  std::wstring dir = exeDirW();
  int len = WideCharToMultiByte(CP_UTF8, 0, dir.data(), (int)dir.size(), nullptr, 0, nullptr, nullptr);
  std::string s(len, '\0');
  WideCharToMultiByte(CP_UTF8, 0, dir.data(), (int)dir.size(), s.data(), len, nullptr, nullptr);
  return s;
}

}  // namespace pvnc
