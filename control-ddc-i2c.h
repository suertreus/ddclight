#ifndef JJARO_CONTROL_DDC_I2C_H_
#define JJARO_CONTROL_DDC_I2C_H_ 1
#include <absl/functional/function_ref.h>
#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <absl/strings/string_view.h>
#include <absl/types/span.h>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>

#include "control.h"
#include "fd-holder.h"

namespace jjaro {
class I2CDDCControl : public Control {
 public:
  static absl::StatusOr<std::optional<I2CDDCControl>> Probe(
      absl::string_view output, absl::string_view output_dir);
  I2CDDCControl(I2CDDCControl &&) = default;
  I2CDDCControl &operator=(I2CDDCControl &&) = default;
  ~I2CDDCControl() override = default;

 private:
  I2CDDCControl(std::string dev, FDHolder fd)
      : Control(std::move(dev)), fd_(std::move(fd)) {}
  absl::StatusOr<int> GetBrightnessPercentImpl(
      absl::FunctionRef<bool()> cancel) override;
  absl::Status SetBrightnessPercentImpl(
      int percent, absl::FunctionRef<bool()> cancel) override;
  absl::Status TryWrite(absl::Span<const std::byte> buf,
                        absl::string_view error);
  absl::Status TryRead(absl::Span<std::byte> buf, absl::string_view error);
  static absl::Status ValidateBrightnessResp(absl::Span<const std::byte> buf,
                                             absl::string_view error);
  static absl::StatusOr<std::string> ReadEDID(int fd);

  FDHolder fd_;
  int max_brightness_;
};
}  // namespace jjaro
#endif  // JJARO_CONTROL_DDC_I2C_H_
