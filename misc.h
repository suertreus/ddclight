#ifndef JJARO_MISC_H_
#define JJARO_MISC_H_ 1

#include <absl/status/statusor.h>

#include <optional>
#include <string>

#include "fd-holder.h"

namespace jjaro {
absl::StatusOr<FDHolder> Open(const char *pathname, int flags);
absl::StatusOr<std::optional<std::string>> Readlink(const char *pathname);
}  // namespace jjaro
#endif  // JJARO_MISC_H_
