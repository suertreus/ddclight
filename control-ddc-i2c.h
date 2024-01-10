#ifndef JJARO_CONTROL_DDC_I2C_H_
#define JJARO_CONTROL_DDC_I2C_H_ 1
#include <absl/functional/function_ref.h>
#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <absl/strings/string_view.h>
#include <absl/types/span.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "control.h"

namespace jjaro {
class I2CDDCControl : public Control {
 public:
  static absl::StatusOr<I2CDDCControl> Open(std::string devnode, int fd = -1);
  I2CDDCControl(I2CDDCControl &&);
  I2CDDCControl &operator=(I2CDDCControl &&);
  ~I2CDDCControl() override;

 private:
  I2CDDCControl(std::string devnode, int fd)
      : Control(std::move(devnode)), fd_(fd) {}
  absl::StatusOr<int> GetBrightnessPercentImpl(absl::FunctionRef<bool()> cancel) override;
  absl::Status SetBrightnessPercentImpl(int percent, absl::FunctionRef<bool()> cancel) override;
  absl::Status TryWrite(absl::Span<const std::byte> buf,
                        absl::string_view error);
  absl::Status TryRead(absl::Span<std::byte> buf, absl::string_view error);
  static absl::Status ValidateBrightnessResp(absl::Span<const std::byte> buf,
                                             absl::string_view error);

  int fd_;
  std::optional<uint16_t> max_brightness_;
};
}  // namespace jjaro
#endif  // JJARO_CONTROL_DDC_I2C_H_
