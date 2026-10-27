#pragma once

#include <charconv>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>

struct AP_Bounce;

namespace thps3_ap {

void InitializeDeathLink(std::string slot_name);
bool ReceiveDeathLink(const AP_Bounce &bounce, std::string_view own_slot);
void SendDeathLink(std::string_view source);

class DeathLinkState {
public:
  void SetEnabled(bool enabled) {
    const std::lock_guard lock(mutex_);
    if (enabled_ != enabled)
      Reset();
    enabled_ = enabled;
  }
  void SetConnected(bool connected) {
    const std::lock_guard lock(mutex_);
    if (connected_ != connected)
      Reset();
    connected_ = connected;
  }
  bool SetAmnesty(std::string_view text) {
    std::uint32_t value = 0;
    const auto [end, error] =
        std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value == 0)
      return false;
    const std::lock_guard lock(mutex_);
    amnesty_ = value;
    bails_ = 0;
    return true;
  }
  std::uint32_t Amnesty() {
    const std::lock_guard lock(mutex_);
    return amnesty_;
  }
  void ObserveBail(bool bailing, bool in_game) {
    const std::lock_guard lock(mutex_);
    if (!enabled_ || !connected_ || !in_game) {
      was_bailing_ = bailing;
      remote_bail_ = false;
      return;
    }
    if (bailing && !was_bailing_) {
      if (remote_bail_)
        remote_bail_ = false;
      else if (++bails_ >= amnesty_) {
        bails_ = 0;
        ++sends_;
      }
    }
    was_bailing_ = bailing;
  }
  bool Receive() {
    const std::lock_guard lock(mutex_);
    if (!enabled_ || !connected_)
      return false;
    incoming_ = true;
    return true;
  }
  bool BeginReceivedBail(bool already_bailing) {
    const std::lock_guard lock(mutex_);
    if (!incoming_)
      return false;
    incoming_ = false;
    if (already_bailing || remote_bail_)
      return false;
    remote_bail_ = true;
    return true;
  }
  bool TakeSend() {
    const std::lock_guard lock(mutex_);
    if (!sends_)
      return false;
    --sends_;
    return true;
  }

private:
  void Reset() {
    bails_ = sends_ = 0;
    incoming_ = remote_bail_ = was_bailing_ = false;
  }
  std::mutex mutex_;
  bool enabled_ = false, connected_ = false;
  bool incoming_ = false, remote_bail_ = false, was_bailing_ = false;
  std::uint32_t amnesty_ = 1, bails_ = 0, sends_ = 0;
};

inline DeathLinkState &DeathLink() {
  static DeathLinkState state;
  return state;
}

} // namespace thps3_ap
