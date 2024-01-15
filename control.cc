#include "control.h"

#include <absl/strings/strip.h>
#include <dirent.h>
#include <absl/strings/numbers.h>
#include <absl/strings/str_cat.h>
#include <errno.h>
#include <cstdint>
#include <utility>

#include "control-backlight.h"
#include "control-ddc-i2c.h"
#include "deleter.h"

namespace jjaro {
absl::StatusOr<std::unique_ptr<Control>> Control::Probe(
    const absl::string_view output) {
  std::unique_ptr<DIR, Deleter<closedir>> drm_dir;
  while (true) {
    drm_dir.reset(opendir("/sys/class/drm"));
    if (!drm_dir && errno == EINTR) continue;
    if (!drm_dir)
      return absl::ErrnoToStatus(errno, "opendir failed for /sys/class/drm");
    break;
  }
  while (true) {
    errno = 0;
    const struct dirent *const ent = readdir(drm_dir.get());
    if (!ent && errno == EINTR) continue;
    if (!ent && errno)
      return absl::ErrnoToStatus(errno, "readdir failed for /sys/class/drm");
    if (!ent)
      return absl::NotFoundError(
          absl::StrCat("no drm output directory found for ", output));
    if (ent->d_type != DT_LNK) continue;
    absl::string_view ent_name(ent->d_name);
    if (!absl::ConsumePrefix(&ent_name, "card")) continue;
    if (!absl::ConsumeSuffix(&ent_name, output)) continue;
    if (!absl::ConsumeSuffix(&ent_name, "-")) continue;
    uint64_t card_num;
    if (!absl::SimpleAtoi(ent_name, &card_num)) continue;
    const auto output_dir = absl::StrCat("/sys/class/drm/", ent->d_name);
    auto bl = BacklightControl::Probe(output, output_dir);
    if (!bl.ok())
      return absl::Status(bl.status().code(),
                          absl::StrCat("failed to probe backlight control for ",
                                       output, ": ", bl.status().message()));
    if (*bl) return std::make_unique<BacklightControl>(std::move(**bl));
    auto ddc = I2CDDCControl::Probe(output, output_dir);
    if (!ddc.ok())
      return absl::Status(ddc.status().code(),
                          absl::StrCat("failed to probe DDC I2C control for ",
                                       output, ": ", ddc.status().message()));
    if (*ddc) return std::make_unique<I2CDDCControl>(std::move(**ddc));
    return absl::NotFoundError(absl::StrCat("no control found for ", output));
  }
}
}  // namespace jjaro
