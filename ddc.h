#ifndef JJARO_DDC_H_
#define JJARO_DDC_H_ 1
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"

namespace jjaro {
class DDCDevice {
 public:
  static absl::StatusOr<DDCDevice> Open(std::string devnode);
  DDCDevice(DDCDevice &&);
  DDCDevice &operator=(DDCDevice &&);
  ~DDCDevice();
  absl::StatusOr<int> GetBrightnessPercent(absl::FunctionRef<bool()> cancel =
                                               [] { return false; });
  absl::Status SetBrightnessPercent(
      int percent, absl::FunctionRef<bool()> cancel = [] { return false; });
  absl::StatusOr<int> cached_brightness_percent() const;
  std::string_view devnode() const { return devnode_; }

 private:
  DDCDevice(std::string devnode, int fd)
      : devnode_(std::move(devnode)), fd_(fd) {}
  absl::Status TryWrite(absl::Span<const std::byte> buf,
                        std::string_view error);
  absl::Status TryRead(absl::Span<std::byte> buf, std::string_view error);
  static absl::Status ValidateBrightnessResp(absl::Span<const std::byte> buf,
                                             std::string_view error);

  std::string devnode_;
  int fd_;
  std::optional<uint16_t> cached_brightness_;
  std::optional<uint16_t> cached_max_brightness_;
};
}  // namespace jjaro
#endif  // JJARO_DDC_H_
