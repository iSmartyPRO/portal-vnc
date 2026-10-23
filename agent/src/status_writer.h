#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

#include "sessions.h"

namespace pvnc {

// Periodically writes <dir>\status-<port>.json from the session registry so a
// local tool (pvnc-admin) can read status without authenticating.
class StatusWriter {
 public:
  StatusWriter(SessionRegistry& reg, uint16_t port, std::wstring dir);
  ~StatusWriter();
  void start();
  void stop();

 private:
  void loop();
  void writeOnce();

  SessionRegistry& reg_;
  uint16_t port_;
  std::wstring dir_;
  uint64_t startedAt_;
  std::thread thread_;
  std::atomic<bool> running_{false};
};

}  // namespace pvnc
