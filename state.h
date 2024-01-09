#ifndef JJARO_STATE_H_
#define JJARO_STATE_H_ 1
#include <absl/base/thread_annotations.h>
#include <absl/synchronization/mutex.h>

#include <optional>

namespace jjaro {
struct State {
  absl::Mutex lock;
  std::optional<int> desired_percentage ABSL_GUARDED_BY(lock);
};
}  // namespace jjaro
#endif  // JJARO_STATE_H_
