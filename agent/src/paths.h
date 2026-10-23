#pragma once
#include <string>

namespace pvnc {

// Full path to the running executable.
std::wstring exePathW();

// Directory containing the running executable, as UTF-8.
std::string exeDirUtf8();

}  // namespace pvnc
