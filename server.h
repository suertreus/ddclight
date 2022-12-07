#ifndef JJARO_SERVER_H_
#define JJARO_SERVER_H_ 1
#include <sdbus-c++/sdbus-c++.h>

#include <algorithm>
#include <cstdint>
#include <deque>
#include <string>
#include <utility>

#include "absl/synchronization/mutex.h"
#include "ddclight-server-glue.h"
#include "enumerate.h"
#include "state.h"
#include "thread.h"

namespace jjaro {

class DDCLight final
    : public sdbus::AdaptorInterfaces<org::jjaro::DDCLight_adaptor> {
 public:
  DDCLight(sdbus::IConnection& connection, std::string objectPath)
      : AdaptorInterfaces(connection, std::move(objectPath)) {
    registerAdaptor();
    poke();
  }
  ~DDCLight() { unregisterAdaptor(); }

 private:
  int64_t get() override {
    absl::MutexLock l(&state_.lock);
    return state_.desired_percentage;
  }
  int64_t poke() override {
    {
      absl::MutexLock l(&lock_);
      threads_.clear();
      auto nodes = GetDevnodes();
      if (nodes.ok()) {
        for (auto& node : *nodes) {
          auto ddc = DDCDevice::Open(std::move(node));
          if (!ddc.ok()) continue;
          if (!state_.init) {
            if (auto pct = ddc->GetBrightnessPercent(); pct.ok()) {
              absl::MutexLock l(&state_.lock);
              state_.desired_percentage = *pct;
              emitWatch(state_.desired_percentage);
              state_.init = true;
            }
          }
          threads_.emplace_back(*std::move(ddc), &state_);
        }
      }
    }
    absl::MutexLock l(&state_.lock);
    if (!state_.init) {
      state_.desired_percentage = 50;
      emitWatch(state_.desired_percentage);
      state_.init = true;
    }
    return state_.desired_percentage;
  }
  int64_t set(const int64_t& percentage) override {
    const int real_percentage =
        std::clamp(percentage, int64_t{0}, int64_t{100});
    absl::MutexLock l(&state_.lock);
    if (state_.desired_percentage == real_percentage)
      return state_.desired_percentage;
    state_.desired_percentage = real_percentage;
    emitWatch(state_.desired_percentage);
    return state_.desired_percentage;
  }
  int64_t increment(const int64_t& percentage) override {
    const int real_percentage =
        std::clamp(percentage, int64_t{0}, int64_t{100});
    absl::MutexLock l(&state_.lock);
    if (real_percentage == 0) return state_.desired_percentage;
    if (state_.desired_percentage == 100) return state_.desired_percentage;
    state_.desired_percentage =
        std::min(int64_t{100}, state_.desired_percentage + percentage);
    emitWatch(state_.desired_percentage);
    return state_.desired_percentage;
  }
  int64_t decrement(const int64_t& percentage) override {
    const int real_percentage =
        std::clamp(percentage, int64_t{0}, int64_t{100});
    absl::MutexLock l(&state_.lock);
    if (real_percentage == 0) return state_.desired_percentage;
    if (state_.desired_percentage == 0) return state_.desired_percentage;
    state_.desired_percentage =
        std::max(int64_t{0}, state_.desired_percentage - percentage);
    emitWatch(state_.desired_percentage);
    return state_.desired_percentage;
  }

  State state_;
  bool init_;
  absl::Mutex lock_;
  std::deque<Thread> threads_ ABSL_GUARDED_BY(lock_);
};

}  // namespace jjaro
#endif  // JJARO_SERVER_H_
