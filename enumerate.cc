#include "enumerate.h"

#include <dirent.h>
#include <sys/utsname.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace jjaro {
namespace {
using UniqueFile = std::unique_ptr<FILE, absl::AnyInvocable<void(FILE *)>>;
using UniqueDir = std::unique_ptr<DIR, absl::AnyInvocable<void(DIR *)>>;
UniqueFile OpenUniqueFile(const char *__restrict__ path,
                          const char *__restrict__ mode) {
  return UniqueFile(fopen(path, mode), [](FILE *fp) { fclose(fp); });
}
UniqueDir OpenUniqueDir(const char *path) {
  return UniqueDir(opendir(path), [](DIR *dp) { closedir(dp); });
}
absl::StatusOr<std::string> Hostname() {
  const char *hostname_env = getenv("HOSTNAME");
  if (hostname_env) return hostname_env;
  struct utsname uts;
  const int iret = uname(&uts);
  if (iret == 0) return uts.nodename;
  return absl::FailedPreconditionError("No hostname set");
}

namespace tycho {
absl::StatusOr<std::string> GetCenterDevnode() {
  constexpr const char *center_sysfs = "/sys/class/drm/card0-DP-1";
  auto dp = OpenUniqueDir(center_sysfs);
  if (!dp)
    return absl::InternalError(
        absl::StrCat("diropen ", center_sysfs, " failed: ", strerror(errno)));
  while (true) {
    errno = 0;
    const struct dirent *const ent = readdir(dp.get());
    if (!ent && errno)
      return absl::InternalError(
          absl::StrCat("readdir failed: ", strerror(errno)));
    if (!ent)
      return absl::NotFoundError(
          absl::StrCat("no i2c-* entry in ", center_sysfs));
    if (ent->d_type != DT_DIR) continue;
    if (absl::StartsWith(ent->d_name, "i2c-"))
      return absl::StrCat("/dev/", ent->d_name);
  }
}

absl::StatusOr<std::vector<std::string>> GetDevnodes() {
  constexpr const char *igpu_sysfs =
      "/sys/devices/pci0000:00/0000:00:08.1/0000:07:00.0";
  std::vector<std::string> ret;
  ret.reserve(3);
  auto center = GetCenterDevnode();
  if (center.ok()) ret.push_back(*std::move(center));
  auto dp = OpenUniqueDir(igpu_sysfs);
  if (!dp)
    return absl::InternalError(
        absl::StrCat("diropen ", igpu_sysfs, " failed: ", strerror(errno)));
  while (true) {
    errno = 0;
    const struct dirent *const ent = readdir(dp.get());
    if (!ent && errno)
      return absl::InternalError(
          absl::StrCat("readdir failed: ", strerror(errno)));
    if (!ent) return ret;
    if (ent->d_type != DT_DIR) continue;
    if (!absl::StartsWith(ent->d_name, "i2c-")) continue;
    auto fp = OpenUniqueFile(
        absl::StrCat(igpu_sysfs, "/", ent->d_name, "/name").c_str(), "r");
    if (!fp) continue;
    std::array<char, 6> buf;
    const size_t sret = fread(buf.data(), 1, buf.size(), fp.get());
    if (sret != buf.size()) continue;
    if (absl::string_view(buf.data(), buf.size()) != "DPMST\n") continue;
    ret.push_back(absl::StrCat("/dev/", ent->d_name));
  }
}
}  // namespace tycho
}  // namespace

absl::StatusOr<std::vector<std::string>> GetDevnodes() {
  auto hostname = Hostname();
  if (!hostname.ok()) return hostname.status();
  if (*hostname == "tycho") {
    return tycho::GetDevnodes();
  }
  return absl::FailedPreconditionError(
      absl::StrCat("Unrecognized hostname ", *hostname));
}

}  // namespace jjaro
