#ifndef JJARO_THREAD_H_
#define JJARO_THREAD_H_ 1

#include <atomic>
#include <thread>

#include "absl/time/time.h"
#include "ddc.h"
#include "state.h"

namespace jjaro {
class Thread {
 public:
  Thread(DDCDevice ddc, State *state);
  Thread(const Thread &) = delete;
  Thread &operator=(const Thread &) = delete;
  ~Thread();

 private:
  bool WaitForNewTargetOrCancel(absl::Duration d);
  bool WaitForDurationOrCancel(absl::Duration d);
  void Loop();

  DDCDevice ddc_;
  State *state_;
  std::atomic<bool> cancel_;  // Writes guarded by `state_->lock`.
  std::thread thread_;
};
}  // namespace jjaro
#endif  // JJARO_THREAD_H_
