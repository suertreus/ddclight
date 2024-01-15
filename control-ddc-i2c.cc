#include "control-ddc-i2c.h"

#include <absl/functional/function_ref.h>
#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <absl/strings/str_cat.h>
#include <absl/strings/string_view.h>
#include <absl/strings/ascii.h>
#include <absl/strings/match.h>
#include <absl/strings/numbers.h>
#include <cstdint>
#include <memory>
#include <absl/time/clock.h>
#include <absl/time/time.h>
#include <absl/types/span.h>
#include <dirent.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <unistd.h>
#include <array>
#include <cerrno>
#include <cstddef>
#include <list>
#include <string>

#include "deleter.h"
#include "fd-holder.h"
#include "misc.h"

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

absl::StatusOr<dev_t> ReadDev(int fd) {
  std::array<char, 64> buf;
  while (true) {
    const ssize_t rret = read(fd, buf.data(), buf.size());
    if (rret < 0 && errno == EINTR) continue;
    if (rret < 0) return absl::ErrnoToStatus(errno, "read failed");
    if (rret == buf.size()) return absl::InternalError("long read");
    const auto dev_str =
        absl::StripAsciiWhitespace(absl::string_view(buf.data(), rret));
    const size_t colon = dev_str.find(':');
    if (colon == dev_str.npos)
      return absl::InternalError(
          absl::StrCat("unrecognized dev value: \"", dev_str, "\""));
    unsigned int maj, min;
    const auto maj_str = dev_str.substr(0, colon);
    if (!absl::SimpleAtoi(maj_str, &maj))
      return absl::InternalError(
          absl::StrCat("unrecognized major value: \"", maj_str, "\""));
    const auto min_str = dev_str.substr(colon + 1);
    if (!absl::SimpleAtoi(min_str, &min))
      return absl::InternalError(
          absl::StrCat("unrecognized minor value: \"", min_str, "\""));
    return makedev(maj, min);
  }
}

absl::StatusOr<dev_t> StatDev(int fd) {
  struct stat statbuf;
  while (true) {
    const int ret = fstat(fd, &statbuf);
    if (ret == -1 && errno == EINTR) continue;
    if (ret == -1) return absl::ErrnoToStatus(errno, "fstat failed");
    if (!(statbuf.st_mode & S_IFCHR))
      return absl::InternalError("not a character device node");
    return statbuf.st_rdev;
  }
}
}  // namespace

absl::StatusOr<std::optional<I2CDDCControl>> I2CDDCControl::Probe(
    const absl::string_view output, const absl::string_view output_dir) {
  std::unique_ptr<DIR, Deleter<closedir>> output_dirp;
  while (true) {
    output_dirp.reset(opendir(std::string(output_dir).c_str()));
    if (!output_dirp && errno == EINTR) continue;
    if (!output_dirp)
      return absl::ErrnoToStatus(errno,
                                 absl::StrCat("opendir failed for ", output));
    break;
  }
  std::list<std::pair<absl::string_view, std::string>> devs;
  while (true) {
    errno = 0;
    const struct dirent *const ent = readdir(output_dirp.get());
    if (!ent && errno == EINTR) continue;
    if (!ent && errno)
      return absl::ErrnoToStatus(errno,
                                 absl::StrCat("readdir failed for ", output));
    if (!ent) break;
    if (ent->d_type != DT_DIR && ent->d_type != DT_LNK) continue;
    if (absl::string_view(ent->d_name) == "ddc") {
      auto link = Readlink(absl::StrCat(output_dir, "/", ent->d_name).c_str());
      if (!link.ok())
        return absl::Status(
            link.status().code(),
            absl::StrCat(output, " ddc: ", link.status().message()));
      if (*link) {
        const size_t slash = (*link)->rfind('/');
        if (slash == (*link)->npos) {
          devs.emplace_front("ddc", **std::move(link));
        } else {
          devs.emplace_front("ddc",
                             absl::string_view(**link).substr(slash + 1));
        }
      }
    }
    if (absl::StartsWith(ent->d_name, "i2c")) {
      auto &p = devs.emplace_back("", ent->d_name);
      p.first = p.second;
    }
  }
  if (devs.empty()) return std::nullopt;
  for (auto &devp : devs) {
    const auto dev_nums_fd = Open(absl::StrCat(output_dir, "/", devp.first,
                                               "/i2c-dev/", devp.second, "/dev")
                                      .c_str(),
                                  O_RDONLY);
    if (!dev_nums_fd.ok())
      return absl::Status(
          dev_nums_fd.status().code(),
          absl::StrCat(output, " ", devp.second,
                       " could not read device number from sysfs: ",
                       dev_nums_fd.status().message()));
    const auto sysfs_dev_nums = ReadDev(dev_nums_fd->get());
    if (!sysfs_dev_nums.ok())
      return absl::Status(
          sysfs_dev_nums.status().code(),
          absl::StrCat(output, " ", devp.second,
                       " could not read device number from sysfs: ",
                       sysfs_dev_nums.status().message()));
    auto dev_fd = Open(absl::StrCat("/dev/", devp.second).c_str(), O_RDWR);
    const auto devfs_dev_nums = StatDev(dev_fd->get());
    if (!devfs_dev_nums.ok())
      return absl::Status(
          devfs_dev_nums.status().code(),
          absl::StrCat(output, " ", devp.second,
                       " could not read device number from sysfs: ",
                       devfs_dev_nums.status().message()));
    if (*sysfs_dev_nums != *devfs_dev_nums)
      return absl::InternalError(absl::StrCat(
          "/dev/", devp.second, " device number ", major(*devfs_dev_nums), ":",
          minor(*devfs_dev_nums), " doesn't match sysfs ",
          major(*sysfs_dev_nums), ":", minor(*sysfs_dev_nums)));
    while (true) {
      const int ret = ioctl(dev_fd->get(), I2C_SLAVE, kDeviceBusAddr);
      if (ret != 0 && errno == EINTR) continue;
      if (ret != 0)
        return absl::ErrnoToStatus(
            errno, absl::StrCat(output, " ", devp.second,
                                " failed to set I2C_SLAVE address 0x",
                                absl::Hex(kDeviceBusAddr)));
      break;
    }
    if (I2CDDCControl ddc(std::move(devp.second), *std::move(dev_fd));
        ddc.GetBrightnessPercent().ok())
      return ddc;
  }
  return std::nullopt;
}

absl::StatusOr<int> I2CDDCControl::GetBrightnessPercentImpl(
    absl::FunctionRef<bool()> cancel) {
  const auto error = absl::StrCat("GetBrightness ", name());
  std::array<std::byte, 6> req{kDeviceWriteAddr, kHostWriteAddr, LengthByte(2),
                               kOpCodeGetVCPReq, kVCPBrightness, std::byte{0}};
  req.back() = Checksum(req);
  for (int i = kTries; i; i--) {
    if (cancel()) return absl::CancelledError("GetBrightness cancelled");
    auto ws = TryWrite(absl::MakeSpan(req).subspan(1), error);
    if (!ws.ok() && i == 1) return ws;
    if (!ws.ok()) continue;
    break;
  }
  absl::SleepFor(absl::Milliseconds(40));
  std::array<std::byte, 12> resp{kHostReadAddr, std::byte{0}};
  for (int i = kTries; i; i--) {
    if (cancel()) return absl::CancelledError("GetBrightness cancelled");
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
  return 100 * brightness / max_brightness_;
}

absl::Status I2CDDCControl::SetBrightnessPercentImpl(
    int percent, absl::FunctionRef<bool()> cancel) {
  const auto error = absl::StrCat("SetBrightness ", name());
  uint16_t val = percent * max_brightness_ / 100;
  std::array<std::byte, 8> req{kDeviceWriteAddr,
                               kHostWriteAddr,
                               LengthByte(4),
                               kOpCodeSetVCPReq,
                               kVCPBrightness,
                               static_cast<std::byte>(val >> 8),
                               static_cast<std::byte>(val)};
  req.back() = Checksum(req);
  for (int i = kTries; i; i--) {
    if (cancel()) return absl::CancelledError("SetBrightness cancelled");
    auto ws = TryWrite(absl::MakeSpan(req).subspan(1), error);
    if (!ws.ok() && !i) return ws;
    if (!ws.ok()) continue;
    break;
  }
  return absl::OkStatus();
}

absl::Status I2CDDCControl::TryWrite(absl::Span<const std::byte> buf,
                                     absl::string_view error) {
  while (true) {
    const ssize_t wret = write(fd_.get(), buf.data(), buf.size());
    if (wret < 0 && errno == EINTR) continue;
    if (wret < 0)
      return absl::ErrnoToStatus(errno, absl::StrCat(error, " write failed"));
    if (wret != static_cast<ssize_t>(buf.size()))
      return absl::ErrnoToStatus(errno, absl::StrCat(error, " short write"));
    return absl::OkStatus();
  }
}
absl::Status I2CDDCControl::TryRead(absl::Span<std::byte> buf,
                                    absl::string_view error) {
  while (true) {
    const ssize_t rret = read(fd_.get(), buf.data(), buf.size());
    if (rret < 0 && errno == EINTR) continue;
    if (rret < 0)
      return absl::ErrnoToStatus(errno, absl::StrCat(error, " read failed"));
    if (rret != static_cast<ssize_t>(buf.size()))
      return absl::ErrnoToStatus(errno, absl::StrCat(error, " short read"));
    return absl::OkStatus();
  }
}
absl::Status I2CDDCControl::ValidateBrightnessResp(
    absl::Span<const std::byte> buf, absl::string_view error) {
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
