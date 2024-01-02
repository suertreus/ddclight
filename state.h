#ifndef JJARO_STATE_H_
#define JJARO_STATE_H_ 1
#include <absl/base/thread_annotations.h>
#include <absl/synchronization/mutex.h>

namespace jjaro {
struct State {
  absl::Mutex lock;
  int desired_percentage ABSL_GUARDED_BY(lock);
  bool init ABSL_GUARDED_BY(lock) = false;
};
}  // namespace jjaro
#endif  // JJARO_STATE_H_
