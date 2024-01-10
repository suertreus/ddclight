#include "control-ddc-i2c.h"

#include <absl/functional/function_ref.h>
#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <absl/strings/str_cat.h>
#include <absl/strings/string_view.h>
#include <absl/time/clock.h>
#include <absl/time/time.h>
#include <absl/types/span.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <string>

namespace jjaro {
namespace {
constexpr int kTries = 10;
constexpr long kDeviceBusAddr{0x37};
constexpr std::byte kDeviceWriteAddr{0x6e};
constexpr std::byte kHostWriteAddr{0x51};
constexpr std::byte kHostReadAddr{0x50};
constexpr std::byte LengthByte(size_t sz) {
  return static_cast<std::byte>(0x80 + sz);
}
constexpr std::byte kOpCodeGetVCPReq{0x01};
constexpr std::byte kOpCodeGetVCPResp{0x02};
constexpr std::byte kOpCodeSetVCPReq{0x03};
constexpr std::byte kVCPBrightness{0x10};
constexpr std::byte Checksum(absl::Span<const std::byte> buf) {
  std::byte cksum{0};
  for (std::byte b : buf) cksum ^= b;
  return cksum;
}
}  // namespace

absl::StatusOr<I2CDDCControl> I2CDDCControl::Open(std::string devnode, int fd) {
  if (fd == -1) {
    fd = open(devnode.c_str(), O_RDWR);
    if (fd == -1)
      return absl::InternalError(
          absl::StrCat("open ", devnode, " failed: ", strerror(errno)));
  }
  if (const int iret = ioctl(fd, I2C_SLAVE, kDeviceBusAddr); iret != 0) {
    close(fd);
    return absl::InternalError(absl::StrCat("ioctl ", devnode, " I2C_SLAVE 0x",
                                            absl::Hex(kDeviceBusAddr),
                                            " failed: ", strerror(errno)));
  }
  return I2CDDCControl(std::move(devnode), fd);
}
I2CDDCControl::I2CDDCControl(I2CDDCControl &&that)
    : Control(std::move(that)),
      fd_(that.fd_),
      max_brightness_(that.max_brightness_) {
  that.fd_ = -1;
}
I2CDDCControl &I2CDDCControl::operator=(I2CDDCControl &&that) {
  if (&that == this) return *this;
  Control::operator=(std::move(that));
  if (fd_ >= 0) close(fd_);
  fd_ = that.fd_;
  max_brightness_ = that.max_brightness_;
  that.fd_ = -1;
  return *this;
}
I2CDDCControl::~I2CDDCControl() {
  if (fd_ >= 0) close(fd_);
}

absl::StatusOr<int> I2CDDCControl::GetBrightnessPercentImpl(
    absl::FunctionRef<bool()> cancel) {
  if (fd_ == -1)
    return absl::FailedPreconditionError("GetBrightness moved-from I2CDDCControl");
  const auto error = absl::StrCat("GetBrightness ", name());
  std::array<std::byte, 6> req{kDeviceWriteAddr, kHostWriteAddr, LengthByte(2),
                               kOpCodeGetVCPReq, kVCPBrightness, std::byte{0}};
  req.back() = Checksum(req);
  for (int i = kTries; i; i--) {
    auto ws = TryWrite(absl::MakeSpan(req).subspan(1), error);
    if (!ws.ok() && i == 1) return ws;
    if (!ws.ok()) continue;
    break;
  }
  absl::SleepFor(absl::Milliseconds(40));
  std::array<std::byte, 12> resp{kHostReadAddr, std::byte{0}};
  for (int i = kTries; i && !cancel(); i--) {
    auto rs = TryRead(absl::MakeSpan(resp).subspan(1), error);
    if (!rs.ok() && i == 1) return rs;
    if (!rs.ok()) continue;
    auto vs = ValidateBrightnessResp(resp, error);
    if (!vs.ok() && i == 1) return vs;
    if (!vs.ok()) continue;
    break;
  }
  const int brightness =
      static_cast<uint16_t>(resp[9]) << 8 | static_cast<uint16_t>(resp[10]);
  max_brightness_ =
      static_cast<uint16_t>(resp[7]) << 8 | static_cast<uint16_t>(resp[8]);
  return 100 * brightness / *max_brightness_;
}

absl::Status I2CDDCControl::SetBrightnessPercentImpl(int percent,
                                             absl::FunctionRef<bool()> cancel) {
  if (fd_ == -1)
    return absl::FailedPreconditionError("SetBrightness moved-from I2CDDCControl");
  if (!max_brightness_.has_value())
    if (auto gbs = GetBrightnessPercent(cancel); !gbs.ok()) return gbs.status();
  const auto error = absl::StrCat("SetBrightness ", name());
  uint16_t val = percent * *max_brightness_ / 100;
  std::array<std::byte, 8> req{kDeviceWriteAddr,
                               kHostWriteAddr,
                               LengthByte(4),
                               kOpCodeSetVCPReq,
                               kVCPBrightness,
                               static_cast<std::byte>(val >> 8),
                               static_cast<std::byte>(val)};
  req.back() = Checksum(req);
  for (int i = kTries; i && !cancel(); i--) {
    auto ws = TryWrite(absl::MakeSpan(req).subspan(1), error);
    if (!ws.ok() && !i) return ws;
    if (!ws.ok()) continue;
    break;
  }
  return absl::OkStatus();
}

absl::Status I2CDDCControl::TryWrite(absl::Span<const std::byte> buf,
                                 absl::string_view error) {
  const ssize_t wret = write(fd_, buf.data(), buf.size());
  if (wret < 0)
    return absl::InternalError(
        absl::StrCat(error, " write failed: ", strerror(errno)));
  if (wret != static_cast<ssize_t>(buf.size()))
    return absl::InternalError(
        absl::StrCat(error, " short write: ", strerror(errno)));
  return absl::OkStatus();
}
absl::Status I2CDDCControl::TryRead(absl::Span<std::byte> buf,
                                absl::string_view error) {
  const ssize_t rret = read(fd_, buf.data(), buf.size());
  if (rret < 0)
    return absl::InternalError(
        absl::StrCat(error, " read failed: ", strerror(errno)));
  if (rret != static_cast<ssize_t>(buf.size()))
    return absl::InternalError(
        absl::StrCat(error, " short read: ", strerror(errno)));
  return absl::OkStatus();
}
absl::Status I2CDDCControl::ValidateBrightnessResp(absl::Span<const std::byte> buf,
                                               absl::string_view error) {
  if (buf[1] != kDeviceWriteAddr)
    return absl::InternalError(absl::StrCat(
        error, " unexpected source address 0x", absl::Hex(buf[1])));
  if (buf[2] != LengthByte(8))
    return absl::InternalError(
        absl::StrCat(error, " unexpected length 0x", absl::Hex(buf[2])));
  if (buf[3] != kOpCodeGetVCPResp)
    return absl::InternalError(
        absl::StrCat(error, " unexpected resp opcode 0x", absl::Hex(buf[3])));
  if (buf[4] != std::byte{0})
    return absl::InternalError(
        absl::StrCat(error, " resp error 0x", absl::Hex(buf[4])));
  if (buf[5] != kVCPBrightness)
    return absl::InternalError(absl::StrCat(
        error, " unexpected resp req opcode 0x", absl::Hex(buf[5])));
  if (buf[6] != std::byte{0})
    return absl::InternalError(
        absl::StrCat(error, " unexpected resp type 0x", absl::Hex(buf[6])));
  if (buf[7] == std::byte{0} && buf[8] == std::byte{0})
    return absl::InternalError(absl::StrCat(error, " zero max brightness"));
  if (Checksum(buf) != std::byte{0})
    return absl::InternalError(absl::StrCat(error, " bad resp checksum"));
  return absl::OkStatus();
}
}  // namespace jjaro
