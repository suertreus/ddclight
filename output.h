#ifndef JJARO_OUTPUT_H_
#define JJARO_OUTPUT_H_ 1

#include <wayland-client-protocol.h>

#include <absl/time/time.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "ddc.h"
#include "deleter.h"
#include "enumerate.h"
#include "state.h"

namespace jjaro {
class Output {
 public:
  Output(State *state, const Enumerator *enumerator, uint32_t name,
         uint32_t version);
  ~Output();
  uint32_t wayland_name() const { return wayland_name_; }

 private:
  static void ThreadLoop(Output *that);
  static void HandleGeometry(void *output, struct wl_output *, int32_t, int32_t,
                             int32_t, int32_t, int32_t, const char *make,
                             const char *model, int32_t);
  static void HandleMode(void *, struct wl_output *, uint32_t, int32_t, int32_t,
                         int32_t);
  static void HandleDone(void *output, struct wl_output *);
  static void HandleScale(void *, struct wl_output *, int32_t);
  static void HandleName(void *output, struct wl_output *, const char *name);
  static void HandleDescription(void *, struct wl_output *, const char *);
  bool WaitForNewTargetOrCancel(absl::Duration d);
  bool WaitForDurationOrCancel(absl::Duration d);

  static constexpr struct wl_output_listener kOutputListener {
    .geometry = HandleGeometry, .mode = HandleMode, .done = HandleDone,
    .scale = HandleScale, .name = HandleName, .description = HandleDescription
  };
  uint32_t wayland_name_;
  std::unique_ptr<struct wl_output, Deleter<wl_output_destroy>> output_;
  // TODO: accept per-make/model configuration of brightness curves.
  std::string make_, model_, name_;
  std::string new_make_, new_model_, new_name_;
  State *state_;
  // Writes to `cancel_` are guarded by `state_->lock`.  This is necessary to
  // use that lock to wait on changes to this variable.
  std::atomic<bool> cancel_;
  std::optional<DDCDevice> ddc_;
  std::optional<std::thread> thread_;
};
}  // namespace jjaro
#endif  // JJARO_OUTPUT_H_
