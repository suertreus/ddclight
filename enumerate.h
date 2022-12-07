#ifndef JJARO_ENUMERATE_H_
#define JJARO_ENUMERATE_H_ 1

#include <string>
#include <vector>

#include "absl/status/statusor.h"

namespace jjaro {
absl::StatusOr<std::vector<std::string>> GetDevnodes();
}  // namespace jjaro
#endif  // JJARO_ENUMERATE_H_
