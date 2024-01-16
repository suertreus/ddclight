#ifndef JJARO_MISC_H_
#define JJARO_MISC_H_ 1

#include <absl/status/statusor.h>

#include <optional>
#include <string>

#include "fd-holder.h"

namespace jjaro {
absl::StatusOr<FDHolder> Open(const char *pathname, int flags);
inline absl::StatusOr<FDHolder> Open(const std::string &pathname, int flags) {
  return Open(pathname.c_str(), flags);
}
absl::StatusOr<std::optional<std::string>> Readlink(const char *pathname);
inline absl::StatusOr<std::optional<std::string>> Readlink(const std::string &pathname) {
  return Readlink(pathname.c_str());
}
}  // namespace jjaro
#endif  // JJARO_MISC_H_
