#pragma once
#include "pvnc/tls.h"

namespace pvnc {

// Serves one authenticated control-mode connection: a request/response loop
// for system info and file transfer (see pvnc/control.h). Returns when the
// client disconnects.
void serveControl(TlsConn& conn);

}  // namespace pvnc
