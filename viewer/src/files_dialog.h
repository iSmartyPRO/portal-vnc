#pragma once
#include <windows.h>

#include "rfb_client.h"

namespace pvnc {

// Shows a modal file-transfer window: browse the remote filesystem and
// download/upload files over the control channel.
void showFiles(HWND parent, RfbClient& client);

}  // namespace pvnc
