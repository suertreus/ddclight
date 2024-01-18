#ifndef JJARO_CONTROL_H_
#define JJARO_CONTROL_H_ 1
#include <absl/functional/function_ref.h>
#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <absl/strings/string_view.h>

#include <memory>
#include <optional>
#include <string>

namespace jjaro {
class Control {
 public:
  static absl::StatusOr<std::unique_ptr<Control>> Probe(
      absl::string_view output);
  virtual ~Control() = default;
  absl::StatusOr<int> GetBrightnessPercent(absl::FunctionRef<bool()> cancel =
                                               [] { return false; }) {
    auto ret = GetBrightnessPercentImpl(cancel);
    if (ret.ok()) cached_brightness_percent_ = *ret;
    return ret;
  }
  absl::Status SetBrightnessPercent(
      int percent, absl::FunctionRef<bool()> cancel = [] { return false; }) {
    auto ret = SetBrightnessPercentImpl(percent, cancel);
    if (ret.ok()) cached_brightness_percent_ = percent;
    return ret;
  }
  absl::StatusOr<int> cached_brightness_percent() const {
    if (!cached_brightness_percent_)
      return absl::FailedPreconditionError("uninitialized brightness");
    return *cached_brightness_percent_;
  }
  absl::string_view name() const { return name_; }

 protected:
  Control(absl::string_view name) : name_(name) {}
  Control(const Control &) = default;
  Control(Control &&) = default;
  Control &operator=(const Control &) = default;
  Control &operator=(Control &&) = default;

 private:
  virtual absl::StatusOr<int> GetBrightnessPercentImpl(
      absl::FunctionRef<bool()> cancel) = 0;
  virtual absl::Status SetBrightnessPercentImpl(
      int percent, absl::FunctionRef<bool()> cancel) = 0;

  std::string name_;
  std::optional<int> cached_brightness_percent_;
};
}  // namespace jjaro
#endif  // JJARO_CONTROL_H_
