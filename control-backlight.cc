#include "control-backlight.h"

#include <absl/functional/function_ref.h>
#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <absl/strings/ascii.h>
#include <absl/strings/match.h>
#include <absl/strings/numbers.h>
#include <absl/strings/str_cat.h>
#include <absl/strings/string_view.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <array>
#include <memory>
#include <cerrno>
#include <string>

#include "deleter.h"
#include "misc.h"
// TODO
// output
//   open /sys/class and search for card*-$name, or fail
//   try controls in order
//     backlight
//       open output dir and search for directory with /subsystem link pointing
//       to backlight
//         open /brightness, /max_brightness, and /actual_brightness
//     i2cddc
//       open output dir and collect ddc and i2c-* in that order, then iterate
//         find devnode with matching name maj min (look in dir/i2c-dev/dir/dev)
//           see if it works & get max_brightness

namespace jjaro {
namespace {
absl::StatusOr<int> ReadInt(int fd) {
  std::array<char, 64> buf;
  while (true) {
    const ssize_t rret = read(fd, buf.data(), buf.size());
    if (rret < 0 && errno == EINTR) continue;
    if (rret < 0) return absl::ErrnoToStatus(errno, "read failed");
    if (rret == buf.size()) return absl::InternalError("long read");
    int val;
    if (const auto num_str =
            absl::StripAsciiWhitespace(absl::string_view(buf.data(), rret));
        !absl::SimpleAtoi(num_str, &val))
      return absl::InternalError(
          absl::StrCat("read a non-integer: \"", num_str, "\""));
    return val;
  }
}
}  // namespace

absl::StatusOr<std::optional<BacklightControl>> BacklightControl::Probe(
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
  while (true) {
    errno = 0;
    const struct dirent *const ent = readdir(output_dirp.get());
    if (!ent && errno == EINTR) continue;
    if (!ent && errno)
      return absl::ErrnoToStatus(errno,
                                 absl::StrCat("readdir failed for ", output));
    if (!ent) return std::nullopt;
    if (ent->d_type != DT_DIR) continue;
    if (const auto link = Readlink(
            absl::StrCat(output_dir, "/", ent->d_name, "/subsystem").c_str());
        !link.ok()) {
      return absl::Status(link.status().code(),
                          absl::StrCat(output, " ", ent->d_name, "/subsystem ",
                                       link.status().message()));
    } else if (!*link || !absl::EndsWith(**link, "/class/backlight")) {
      continue;
    }
    const auto max_brightness_fd = Open(
        absl::StrCat(output_dir, "/", ent->d_name, "/max_brightness").c_str(),
        O_RDONLY);
    if (!max_brightness_fd.ok())
      return absl::Status(
          max_brightness_fd.status().code(),
          absl::StrCat(output, " ", ent->d_name, "/max_brightness ",
                       max_brightness_fd.status().message()));
    auto max_brightness = ReadInt(max_brightness_fd->get());
    if (!max_brightness.ok())
      return absl::Status(
          max_brightness.status().code(),
          absl::StrCat("couldn't get ", output, " ", ent->d_name,
                       "/max_brightness: ", max_brightness.status().message()));
    auto brightness_fd =
        Open(absl::StrCat(output_dir, "/", ent->d_name, "/brightness").c_str(),
             O_WRONLY);
    if (!brightness_fd.ok())
      return absl::Status(brightness_fd.status().code(),
                          absl::StrCat(output, " ", ent->d_name, "/brightness ",
                                       brightness_fd.status().message()));
    auto actual_brightness_fd =
        Open(absl::StrCat(output_dir, "/", ent->d_name, "/actual_brightness")
                 .c_str(),
             O_RDONLY);
    if (!actual_brightness_fd.ok())
      return absl::Status(
          actual_brightness_fd.status().code(),
          absl::StrCat(output, " ", ent->d_name, "/actual_brightness ",
                       actual_brightness_fd.status().message()));
    return BacklightControl(ent->d_name, *std::move(brightness_fd),
                            *std::move(actual_brightness_fd), *max_brightness);
  }
}

absl::StatusOr<int> BacklightControl::GetBrightnessPercentImpl(
    absl::FunctionRef<bool()> cancel) {
  auto actual_brightness = ReadInt(actual_brightness_fd_.get());
  if (!actual_brightness.ok())
    return absl::Status(
        actual_brightness.status().code(),
        absl::StrCat("couldn't get ", name(), " actual_brightness: ",
                     actual_brightness.status().message()));
  return 100 * *actual_brightness / max_brightness_;
}

absl::Status BacklightControl::SetBrightnessPercentImpl(
    int percent, absl::FunctionRef<bool()> cancel) {
  const auto val = absl::StrCat(percent * max_brightness_ / 100);
  while (true) {
    const ssize_t wret = write(brightness_fd_.get(), val.data(), val.size());
    if (wret < 0 && errno == EINTR) continue;
    if (wret < 0)
      return absl::ErrnoToStatus(
          errno, absl::StrCat("SetBrightness ", name(), " write failed"));
    if (wret < val.size())
      return absl::InternalError(
          absl::StrCat("SetBrightness ", name(), " short write"));
    return absl::OkStatus();
  }
}
}  // namespace jjaro
