#include "output.h"

#include <absl/functional/function_ref.h>
#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <absl/strings/str_format.h>
#include <absl/synchronization/mutex.h>
#include <wayland-util.h>

#include <algorithm>
#include <cstdio>
#include <optional>
#include <tuple>
#include <utility>

namespace jjaro {
Output::Output(State *state, const Enumerator *enumerator, uint32_t name,
               uint32_t version)
    : wayland_name_(name), state_(state) {
  output_.reset(static_cast<struct wl_output *>(wl_registry_bind(
      enumerator->registry(), name, &wl_output_interface,
      std::min<uint32_t>(wl_output_interface.version, version))));
  if (!output_) {
    absl::FPrintF(stderr,
                  "Unable to bind output %d to Wayland registry; no outputs "
                  "will be adjusted.\n",
                  name);
  }
  if (const int ret =
          wl_output_add_listener(output_.get(), &kOutputListener, this);
      ret) {
    absl::FPrintF(stderr,
                  "Unable to listen to Wayland registry for output %d; no "
                  "outputs will be adjusted (%d).\n",
                  name, ret);
  }
}
// This is either run from the `Enumerator::WaylandThreadLoop` thread or from
// the main thread after that thread has been joined, so there's no race on
// `thread_` nor any concern about clearning `cancel_` between the set here and
// the read inside `thread_`.
Output::~Output() {
  if (!thread_) return;
  {
    absl::MutexLock l(&state_->lock);
    cancel_.store(true, std::memory_order_relaxed);
  }
  thread_->join();
}

void Output::ThreadLoop(Output *that) {
  int last_desired_percentage;
  {
    absl::MutexLock l(&that->state_->lock);
    if (!that->state_->desired_percentage.has_value()) {
      int try_count = 0;
      that->state_->desired_percentage =
          that->control_
              ->GetBrightnessPercent(
                  [&try_count]() -> bool { return try_count++; })
              .value_or(50);
    }
    last_desired_percentage = that->state_->desired_percentage.value_or(50);
  }
  while (true) {
    const auto ss = that->control_->SetBrightnessPercent(
        last_desired_percentage,
        [that] { return that->cancel_.load(std::memory_order_relaxed); });
    absl::MutexLock l(&that->state_->lock);
    if (ss.ok()) {
      if (that->WaitForNewTargetOrCancel(absl::Minutes(1))) return;
      last_desired_percentage = that->state_->desired_percentage.value_or(50);
    } else {
#ifndef NDEBUG
      absl::FPrintF(stderr,
                    "Failed to set brightness to %d on output %s (%s:%s) %s: "
                    "%s\nWill retry in %v.\n",
                    last_desired_percentage, that->name_, that->make_,
                    that->model_, that->control_->name(), ss.ToString(),
                    absl::Minutes(1));
#endif
      if (that->WaitForDurationOrCancel(absl::Minutes(1))) return;
    }
  }
}

bool Output::WaitForNewTargetOrCancel(absl::Duration d) {
  const auto current_percent = control_->cached_brightness_percent();
  if (current_percent.ok()) {
    auto cond = [this, old = *current_percent] {
      return cancel_.load(std::memory_order_relaxed) ||
             state_->desired_percentage != old;
    };
    state_->lock.Await(absl::Condition(&cond));
    return cancel_.load(std::memory_order_relaxed);
  } else {
#ifndef NDEBUG
    absl::FPrintF(stderr,
                  "Failed to get brightness on output %s (%s:%s) %s: %s\nWill "
                  "retry in %v.\n",
                  name_, make_, model_, control_->name(),
                  current_percent.status().ToString(), d);
#endif
    return WaitForDurationOrCancel(d);
  }
}

bool Output::WaitForDurationOrCancel(absl::Duration d) {
  auto cond = [this] { return cancel_.load(std::memory_order_relaxed); };
  state_->lock.AwaitWithTimeout(absl::Condition(&cond), d);
  return cancel_.load(std::memory_order_relaxed);
}

void Output::HandleGeometry(void *output, struct wl_output *, int32_t, int32_t,
                            int32_t, int32_t, int32_t, const char *make,
                            const char *model, int32_t) {
  auto that = static_cast<Output *>(output);
  that->new_make_.assign(make);
  that->new_model_.assign(model);
}
void Output::HandleMode(void *, struct wl_output *, uint32_t, int32_t, int32_t,
                        int32_t) {}
void Output::HandleDone(void *output, struct wl_output *) {
  auto that = static_cast<Output *>(output);
  if (std::tie(that->make_, that->model_, that->name_) ==
      std::tie(that->new_make_, that->new_model_, that->new_name_))
    return;
  if (that->thread_) {
    {
      absl::MutexLock l(&that->state_->lock);
      that->cancel_.store(true, std::memory_order_relaxed);
    }
    that->thread_->join();
  }
  that->make_ = std::move(that->new_make_);
  that->model_ = std::move(that->new_model_);
  that->name_ = std::move(that->new_name_);
  if (auto ctrl = Control::Probe(that->name_); ctrl.ok()) {
    that->control_ = *std::move(ctrl);
    {
      absl::MutexLock l(&that->state_->lock);
      that->cancel_.store(false, std::memory_order_relaxed);
    }
    that->thread_.emplace(ThreadLoop, that);
    absl::FPrintF(stderr, "Watching controls for output %s (%s:%s) %s.\n",
                  that->name_, that->make_, that->model_,
                  that->control_->name());
  } else {
    absl::FPrintF(stderr,
                  "Failed to find brightness control for output %s (%s:%s); won't "
                  "adjust: %s.\n",
                  that->name_, that->make_, that->model_,
                  ctrl.status().ToString());
    that->control_.reset();
    that->thread_.reset();
  }
}
void Output::HandleScale(void *, struct wl_output *, int32_t) {}
void Output::HandleName(void *output, struct wl_output *, const char *name) {
  auto that = static_cast<Output *>(output);
  that->new_name_ = std::string(name);
}
void Output::HandleDescription(void *, struct wl_output *, const char *) {}
}  // namespace jjaro
