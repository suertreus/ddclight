#ifndef JJARO_FD_HOLDER_H_
#define JJARO_FD_HOLDER_H_ 1
#include <absl/status/status.h>
#include <absl/status/statusor.h>

namespace jjaro {
class FDHolder {
 public:
  static absl::StatusOr<FDHolder> Dup(int fd);
  explicit FDHolder();
  explicit FDHolder(int fd);
  FDHolder(FDHolder &&);
  FDHolder &operator=(FDHolder &&);
  ~FDHolder();
  int get() const;
  [[nodiscard]] int release();
  absl::Status Close();
  absl::StatusOr<FDHolder> Dup() const;

 private:
  int fd_;
};
}  // namespace jjaro
#endif  // JJARO_FD_HOLDER_H_
