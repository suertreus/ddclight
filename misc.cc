#include "misc.h"

#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <optional>
#include <string>

#include "fd-holder.h"

namespace jjaro {
absl::StatusOr<FDHolder> Open(const char *pathname, int flags) {
  while (true) {
    const int ret = open(pathname, flags);
    if (ret == -1 && errno == EINTR) continue;
    if (ret == -1) return absl::ErrnoToStatus(errno, "open failed");
    return FDHolder(ret);
  }
}
absl::StatusOr<std::optional<std::string>> Readlink(const char *pathname) {
  std::string buf(256, '0');
  while (true) {
    const ssize_t ret = readlink(pathname, buf.data(), buf.size());
    if (ret < 0 && errno == EINTR) continue;
    if (ret < 0 && errno == ENOENT) return std::nullopt;
    if (ret < 0) return absl::ErrnoToStatus(errno, "readlinkat failed");
    if (ret < buf.size()) {
      buf.resize(ret);
      return buf;
    }
    buf = std::string(buf.size() * 2, '0');
  }
}
}  // namespace jjaro
