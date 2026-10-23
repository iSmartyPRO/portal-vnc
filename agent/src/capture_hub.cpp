#include "capture_hub.h"

#include <chrono>

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

bool CaptureHub::followInputDesktop() {
  HDESK in = OpenInputDesktop(0, FALSE,
                              GENERIC_READ | DESKTOP_CREATEWINDOW | DESKTOP_WRITEOBJECTS |
                                  DESKTOP_READOBJECTS | DESKTOP_HOOKCONTROL);
  if (!in) return false;

  wchar_t name[256] = {0};
  DWORD need = 0;
  GetUserObjectInformationW(in, UOI_NAME, name, sizeof(name), &need);
  std::wstring newName(name);

  if (newName == deskName_ && desk_) {
    CloseDesktop(in);
    return false;  // unchanged
  }

  if (SetThreadDesktop(in) == 0) {
    CloseDesktop(in);
    return false;  // cannot switch (e.g. not enough privilege); keep current
  }
  if (desk_) CloseDesktop(desk_);
  desk_ = in;
  deskName_ = newName;
  return true;
}

void CaptureHub::loop() {
  // All GDI/capture/input happens on this thread so SetThreadDesktop applies.
  while (running_.load()) {
    bool changed = followInputDesktop();
    if (changed || !cap_) {
      // Recreate capture objects bound to the (possibly new) desktop.
      cap_.reset(new GdiCapture());
    }

    // Apply queued input on this desktop-attached thread.
    std::vector<InputEvent> batch;
    {
      std::lock_guard<std::mutex> lk(inMu_);
      batch.swap(inQueue_);
    }
    if (!batch.empty()) {
      if (!input_ || input_->width() != cap_->width() || input_->height() != cap_->height())
        input_.reset(new InputInjector(cap_->width(), cap_->height()));
      for (const auto& e : batch) {
        if (e.isKey) input_->key(e.keysym, e.down);
        else input_->pointer(e.buttons, e.x, e.y);
      }
    }

    auto f = std::make_shared<Frame>();
    if (cap_ && cap_->grab(*f)) {
      std::lock_guard<std::mutex> lk(mu_);
      current_ = std::move(f);  // publish an immutable snapshot; readers share it
      ++version_;
      cv_.notify_all();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
  }
  if (desk_) { CloseDesktop(desk_); desk_ = nullptr; }
}

std::shared_ptr<const Frame> CaptureHub::latest() {
  std::unique_lock<std::mutex> lk(mu_);
  cv_.wait_for(lk, std::chrono::seconds(2), [this] { return version_ > 0; });
  return current_;  // atomic refcount bump only; no pixel copy under the lock
}

int CaptureHub::width() {
  std::lock_guard<std::mutex> lk(mu_);
  return current_ ? current_->width : 0;
}
int CaptureHub::height() {
  std::lock_guard<std::mutex> lk(mu_);
  return current_ ? current_->height : 0;
}

void CaptureHub::enqueuePointer(uint8_t buttons, int x, int y) {
  InputEvent e;
  e.isKey = false; e.buttons = buttons; e.x = x; e.y = y;
  std::lock_guard<std::mutex> lk(inMu_);
  inQueue_.push_back(e);
}
void CaptureHub::enqueueKey(uint32_t keysym, bool down) {
  InputEvent e;
  e.isKey = true; e.keysym = keysym; e.down = down;
  std::lock_guard<std::mutex> lk(inMu_);
  inQueue_.push_back(e);
}

}  // namespace pvnc
