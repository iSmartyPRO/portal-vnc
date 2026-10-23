#pragma once
#include <string>

namespace pvnc {

// Collects technical and OS information about this machine as a JSON string
// (UTF-8). Used by the control channel for the viewer's "Remote computer info".
std::string collectSystemInfoJson();

}  // namespace pvnc
