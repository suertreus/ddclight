#ifndef JJARO_CONTROL_BACKLIGHT_H_
#define JJARO_CONTROL_BACKLIGHT_H_ 1
#include <absl/functional/function_ref.h>
#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <absl/strings/string_view.h>

#include <optional>
#include <string>
#include <utility>

#include "control.h"
#include "fd-holder.h"

namespace jjaro {
class BacklightControl : public Control {
 public:
  static absl::StatusOr<std::optional<BacklightControl>> Probe(
      absl::string_view output, absl::string_view output_dir);
  static absl::StatusOr<std::optional<BacklightControl>> ProbeDevice(
      absl::string_view output, absl::string_view device);
  BacklightControl(BacklightControl &&) = default;
  BacklightControl &operator=(BacklightControl &&) = default;
  ~BacklightControl() override = default;

 private:
  BacklightControl(std::string name, FDHolder brightness_fd,
                   FDHolder actual_brightness_fd, int max_brightness)
      : Control(std::move(name)),
        brightness_fd_(std::move(brightness_fd)),
        actual_brightness_fd_(std::move(actual_brightness_fd)),
        max_brightness_(max_brightness) {}
  absl::StatusOr<int> GetBrightnessPercentImpl(
      absl::FunctionRef<bool()> cancel) override;
  absl::Status SetBrightnessPercentImpl(
      int percent, absl::FunctionRef<bool()> cancel) override;

  FDHolder brightness_fd_, actual_brightness_fd_;
  int max_brightness_;
};
}  // namespace jjaro
#endif  // JJARO_CONTROL_BACKLIGHT_H_
