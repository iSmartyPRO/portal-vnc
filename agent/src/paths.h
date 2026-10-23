#pragma once
#include <string>

namespace pvnc {

// Full path to the running executable.
std::wstring exePathW();

// Directory containing the running executable, as UTF-8.
std::string exeDirUtf8();

// Directory containing the running executable, as a wide path (no trailing slash).
std::wstring exeDirW();

}  // namespace pvnc
