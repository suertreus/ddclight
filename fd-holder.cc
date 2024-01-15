#include "fd-holder.h"

#include <cerrno>
#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <unistd.h>

namespace jjaro {
absl::StatusOr<FDHolder> FDHolder::Dup(int fd) {
  if (fd == -1) return FDHolder(-1);
  while (true) {
    const int ret = dup(fd);
    if (ret == -1 && errno == EINTR) continue;
    if (ret == -1) return absl::ErrnoToStatus(errno, "failed to dup fd");
    return FDHolder(ret);
  }
}
FDHolder::FDHolder() : FDHolder(-1) {}
FDHolder::FDHolder(int fd) : fd_(fd) {}
FDHolder::FDHolder(FDHolder &&that) : fd_(that.fd_) { that.fd_ = -1; }
FDHolder &FDHolder::operator=(FDHolder &&that) {
  if (this == &that) return *this;
  Close().IgnoreError();
  fd_ = that.fd_;
  that.fd_ = -1;
  return *this;
}
FDHolder::~FDHolder() { Close().IgnoreError(); }
int FDHolder::get() const { return fd_; }
int FDHolder::release() {
  const int fd = fd_;
  fd_ = -1;
  return fd;
}
absl::Status FDHolder::Close() {
  if (fd_ == -1) return absl::OkStatus();
  while (true) {
    const int ret = close(fd_);
    if (ret && errno != EINTR) return absl::ErrnoToStatus(errno, "failed to close fd");
#ifndef __linux__
    // On Linux, the descriptor is closed even when `EINTR` is returned.
    if (!ret)
#endif
      break;
  }
  fd_ = -1;
  return absl::OkStatus();
}
absl::StatusOr<FDHolder> FDHolder::Dup() const {
  return Dup(fd_);
}
}  // namespace jjaro
