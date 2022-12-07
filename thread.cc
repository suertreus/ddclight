#include "thread.h"

#include <atomic>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "ddc.h"
#include "state.h"

namespace jjaro {
Thread::Thread(DDCDevice ddc, State *state)
    : ddc_(std::move(ddc)),
      state_(state),
      cancel_{false},
      thread_([](Thread *t) { t->Loop(); }, this) {}
Thread::~Thread() {
  {
    absl::MutexLock l(&state_->lock);
    cancel_.store(true, std::memory_order_relaxed);
  }
  thread_.join();
}

bool Thread::WaitForNewTargetOrCancel(absl::Duration d) {
  const auto current_percent = ddc_.cached_brightness_percent();
  if (current_percent.ok()) {
    auto cond = [this, old = *current_percent] {
      return cancel_.load(std::memory_order_relaxed) ||
             state_->desired_percentage != old;
    };
    state_->lock.Await(absl::Condition(&cond));
    return cancel_.load(std::memory_order_relaxed);
  } else {
    return WaitForDurationOrCancel(d);
  }
}

bool Thread::WaitForDurationOrCancel(absl::Duration d) {
  auto cond = [this] { return cancel_.load(std::memory_order_relaxed); };
  state_->lock.AwaitWithTimeout(absl::Condition(&cond), d);
  return cancel_.load(std::memory_order_relaxed);
}

void Thread::Loop() {
  int last_desired_percentage;
  {
    absl::MutexLock l(&state_->lock);
    last_desired_percentage = state_->desired_percentage;
  }
  while (true) {
    const auto ss = ddc_.SetBrightnessPercent(last_desired_percentage, [this] {
      return cancel_.load(std::memory_order_relaxed);
    });
    absl::MutexLock l(&state_->lock);
    if (ss.ok()) {
      if (WaitForNewTargetOrCancel(absl::Minutes(1))) return;
      last_desired_percentage = state_->desired_percentage;
    } else {
      if (WaitForDurationOrCancel(absl::Minutes(1))) return;
    }
  }
}
}  // namespace jjaro
