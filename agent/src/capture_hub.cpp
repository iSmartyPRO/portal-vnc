#include "capture_hub.h"

namespace pvnc {

CaptureHub::CaptureHub() = default;

CaptureHub::~CaptureHub() { stop(); }

void CaptureHub::start() {
  if (running_.exchange(true)) return;
  thread_ = std::thread([this] { loop(); });
}

void CaptureHub::stop() {
  if (!running_.exchange(false)) return;
  if (thread_.joinable()) thread_.join();
}

void CaptureHub::loop() {
  // ~20 fps. The capture object lives on this thread.
  while (running_.load()) {
    Frame f;
    if (capture_.grab(f)) {
      std::lock_guard<std::mutex> lk(mu_);
      current_ = std::move(f);
      ++version_;
      cv_.notify_all();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
}

uint64_t CaptureHub::latest(Frame& out) {
  std::unique_lock<std::mutex> lk(mu_);
  cv_.wait_for(lk, std::chrono::seconds(2), [this] { return version_ > 0; });
  out = current_;
  return version_;
}

int CaptureHub::width() {
  std::lock_guard<std::mutex> lk(mu_);
  return current_.width;
}
int CaptureHub::height() {
  std::lock_guard<std::mutex> lk(mu_);
  return current_.height;
}

}  // namespace pvnc
