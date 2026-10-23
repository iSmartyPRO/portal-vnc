#include "status_writer.h"

#include <windows.h>

#include <chrono>
#include <fstream>

namespace pvnc {

StatusWriter::StatusWriter(SessionRegistry& reg, uint16_t port, std::wstring dir)
    : reg_(reg), port_(port), dir_(std::move(dir)), startedAt_(nowUnix()) {}

StatusWriter::~StatusWriter() { stop(); }

void StatusWriter::start() {
  if (running_.exchange(true)) return;
  thread_ = std::thread([this] { loop(); });
}
void StatusWriter::stop() {
  if (!running_.exchange(false)) return;
  if (thread_.joinable()) thread_.join();
  // Remove our status file on clean shutdown.
  DeleteFileW((dir_ + L"\\status-" + std::to_wstring(port_) + L".json").c_str());
}

void StatusWriter::writeOnce() {
  std::string json = sessionsStatusJson(reg_, port_, startedAt_);
  std::wstring path = dir_ + L"\\status-" + std::to_wstring(port_) + L".json";
  std::wstring tmp = path + L".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) return;
    f.write(json.data(), (std::streamsize)json.size());
  }
  MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}

void StatusWriter::loop() {
  while (running_.load()) {
    writeOnce();
    for (int i = 0; i < 25 && running_.load(); ++i)
      std::this_thread::sleep_for(std::chrono::milliseconds(200));  // ~5s, responsive stop
  }
}

}  // namespace pvnc
