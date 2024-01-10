#include "output.h"

#include <absl/functional/function_ref.h>
#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <absl/strings/numbers.h>
#include <absl/strings/str_cat.h>
#include <absl/strings/str_format.h>
#include <absl/strings/string_view.h>
#include <absl/strings/strip.h>
#include <absl/synchronization/mutex.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <unistd.h>
#include <wayland-util.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>
#include <tuple>
#include <utility>

#include "control-ddc-i2c.h"

namespace jjaro {
namespace {
absl::StatusOr<I2CDDCControl> DDCForName(absl::string_view name) {
  std::string dir("/sys/class/drm");
  std::string devnode("/dev/");
  {
    std::unique_ptr<DIR, Deleter<closedir>> drm(opendir(dir.c_str()));
    if (!drm)
      return absl::NotFoundError(
          absl::StrCat("diropen(\"", dir, "\") failed: ", strerror(errno)));
    while (true) {
      errno = 0;
      const struct dirent *const ent = readdir(drm.get());
      if (!ent && errno)
        return absl::NotFoundError(
            absl::StrCat("readdir(\"", dir, "\") failed: ", strerror(errno)));
      if (!ent)
        return absl::NotFoundError(absl::StrCat("no output directory matching ",
                                                dir, "/card*-", name));
      if (ent->d_type != DT_LNK) continue;
      absl::string_view ent_name(ent->d_name);
      if (!absl::ConsumePrefix(&ent_name, "card")) continue;
      if (!absl::ConsumeSuffix(&ent_name, name)) continue;
      if (!absl::ConsumeSuffix(&ent_name, "-")) continue;
      uint64_t card_num;
      if (!absl::SimpleAtoi(ent_name, &card_num)) continue;
      absl::StrAppend(&dir, "/", ent->d_name, "/ddc/i2c-dev");
      break;
    }
  }
  {
    std::unique_ptr<DIR, Deleter<closedir>> i2cdev(opendir(dir.c_str()));
    if (!i2cdev)
      return absl::NotFoundError(
          absl::StrCat("diropen(\"", dir, "\") failed: ", strerror(errno)));
    while (true) {
      errno = 0;
      const struct dirent *const ent = readdir(i2cdev.get());
      if (!ent && errno)
        return absl::NotFoundError(
            absl::StrCat("readdir(\"", dir, "\") failed: ", strerror(errno)));
      if (!ent)
        return absl::NotFoundError(
            absl::StrCat("no i2c-* directory found under ", dir, "/"));
      if (ent->d_type != DT_DIR) continue;
      absl::string_view ent_name(ent->d_name);
      if (!absl::ConsumePrefix(&ent_name, "i2c-")) continue;
      uint64_t dev_num;
      if (!absl::SimpleAtoi(ent_name, &dev_num)) continue;
      absl::StrAppend(&dir, "/", ent->d_name, "/dev");
      absl::StrAppend(&devnode, ent->d_name);
      break;
    }
  }
  int fd = open(devnode.c_str(), O_RDWR);
  if (fd == -1)
    return absl::NotFoundError(
        absl::StrCat("open(\"", devnode, "\") failed: ", strerror(errno)));
  struct stat statbuf;
  int ret = fstat(fd, &statbuf);
  if (ret == -1)
    return absl::NotFoundError(
        absl::StrCat("fstat(\"", devnode, "\") failed: ", strerror(errno)));
  if (!(statbuf.st_mode & S_IFCHR))
    return absl::NotFoundError(
        absl::StrCat(devnode, " is not a character device node"));
  {
    std::unique_ptr<FILE, Deleter<fclose>> devfp(fopen(dir.c_str(), "r"));
    if (!devfp)
      return absl::NotFoundError(
          absl::StrCat("fopen(\"", dir, "\") failed: ", strerror(errno)));
    std::array<char, (std::numeric_limits<mode_t>::digits10 + 1) * 2 + 2> buf;
    const size_t sret = fread(buf.data(), 1, buf.size(), devfp.get());
    if (sret <= 0)
      return absl::NotFoundError(
          absl::StrCat("fread(\"", dir, "\") failed: ", strerror(errno)));
    if (absl::StrCat(major(statbuf.st_rdev), ":", minor(statbuf.st_rdev),
                     "\n") != absl::string_view(buf.data(), sret))
      return absl::NotFoundError(
          absl::StrCat(devnode, " major/minor don't match ", dir));
  }
  auto ddc = I2CDDCControl::Open(std::move(devnode), fd);
  if (!ddc.ok()) close(fd);
  return ddc;
}
}  // namespace

Output::Output(State *state, const Enumerator *enumerator, uint32_t name,
               uint32_t version)
    : wayland_name_(name), state_(state) {
  output_.reset(static_cast<struct wl_output *>(wl_registry_bind(
      enumerator->registry(), name, &wl_output_interface,
      std::min<uint32_t>(wl_output_interface.version, version))));
  if (!output_) {
    absl::FPrintF(stderr,
                  "Unable to bind output %d to Wayland registry; no outputs "
                  "will be adjusted.\n",
                  name);
  }
  if (const int ret =
          wl_output_add_listener(output_.get(), &kOutputListener, this);
      ret) {
    absl::FPrintF(stderr,
                  "Unable to listen to Wayland registry for output %d; no "
                  "outputs will be adjusted (%d).\n",
                  name, ret);
  }
}
// This is either run from the `Enumerator::WaylandThreadLoop` thread or from
// the main thread after that thread has been joined, so there's no race on
// `thread_` nor any concern about clearning `cancel_` between the set here and
// the read inside `thread_`.
Output::~Output() {
  if (!thread_) return;
  {
    absl::MutexLock l(&state_->lock);
    cancel_.store(true, std::memory_order_relaxed);
  }
  thread_->join();
}

void Output::ThreadLoop(Output *that) {
  int last_desired_percentage;
  {
    absl::MutexLock l(&that->state_->lock);
    if (!that->state_->desired_percentage.has_value()) {
      int try_count = 0;
      that->state_->desired_percentage =
          that->control_
              ->GetBrightnessPercent(
                  [&try_count]() -> bool { return try_count++; })
              .value_or(50);
    }
    last_desired_percentage = that->state_->desired_percentage.value_or(50);
  }
  while (true) {
    const auto ss = that->control_->SetBrightnessPercent(
        last_desired_percentage,
        [that] { return that->cancel_.load(std::memory_order_relaxed); });
    absl::MutexLock l(&that->state_->lock);
    if (ss.ok()) {
      if (that->WaitForNewTargetOrCancel(absl::Minutes(1))) return;
      last_desired_percentage = that->state_->desired_percentage.value_or(50);
    } else {
#ifndef NDEBUG
      absl::FPrintF(stderr,
                    "Failed to set brightness to %d on output %s (%s:%s) %s: "
                    "%s\nWill retry in %v.\n",
                    last_desired_percentage, that->name_, that->make_,
                    that->model_, that->control_->name(), ss.ToString(),
                    absl::Minutes(1));
#endif
      if (that->WaitForDurationOrCancel(absl::Minutes(1))) return;
    }
  }
}

bool Output::WaitForNewTargetOrCancel(absl::Duration d) {
  const auto current_percent = control_->cached_brightness_percent();
  if (current_percent.ok()) {
    auto cond = [this, old = *current_percent] {
      return cancel_.load(std::memory_order_relaxed) ||
             state_->desired_percentage != old;
    };
    state_->lock.Await(absl::Condition(&cond));
    return cancel_.load(std::memory_order_relaxed);
  } else {
#ifndef NDEBUG
    absl::FPrintF(stderr,
                  "Failed to get brightness on output %s (%s:%s) %s: %s\nWill "
                  "retry in %v.\n",
                  name_, make_, model_, control_->name(),
                  current_percent.status().ToString(), d);
#endif
    return WaitForDurationOrCancel(d);
  }
}

bool Output::WaitForDurationOrCancel(absl::Duration d) {
  auto cond = [this] { return cancel_.load(std::memory_order_relaxed); };
  state_->lock.AwaitWithTimeout(absl::Condition(&cond), d);
  return cancel_.load(std::memory_order_relaxed);
}

void Output::HandleGeometry(void *output, struct wl_output *, int32_t, int32_t,
                            int32_t, int32_t, int32_t, const char *make,
                            const char *model, int32_t) {
  auto that = static_cast<Output *>(output);
  that->new_make_.assign(make);
  that->new_model_.assign(model);
}
void Output::HandleMode(void *, struct wl_output *, uint32_t, int32_t, int32_t,
                        int32_t) {}
void Output::HandleDone(void *output, struct wl_output *) {
  auto that = static_cast<Output *>(output);
  if (std::tie(that->make_, that->model_, that->name_) ==
      std::tie(that->new_make_, that->new_model_, that->new_name_))
    return;
  if (that->thread_) {
    {
      absl::MutexLock l(&that->state_->lock);
      that->cancel_.store(true, std::memory_order_relaxed);
    }
    that->thread_->join();
  }
  that->make_ = std::move(that->new_make_);
  that->model_ = std::move(that->new_model_);
  that->name_ = std::move(that->new_name_);
  if (absl::StatusOr<I2CDDCControl> ddc = DDCForName(that->name_); ddc.ok()) {
    that->control_ = std::make_unique<I2CDDCControl>(*std::move(ddc));
    {
      absl::MutexLock l(&that->state_->lock);
      that->cancel_.store(false, std::memory_order_relaxed);
    }
    that->thread_.emplace(ThreadLoop, that);
    absl::FPrintF(stderr, "Watching DDC devnode for output %s (%s:%s) %s.\n",
                  that->name_, that->make_, that->model_,
                  that->control_->name());
  } else {
    absl::FPrintF(stderr,
                  "Failed to infer DDC devnode for output %s (%s:%s); won't "
                  "adjust: %s.\n",
                  that->name_, that->make_, that->model_,
                  ddc.status().ToString());
    that->control_.reset();
    that->thread_.reset();
  }
}
void Output::HandleScale(void *, struct wl_output *, int32_t) {}
void Output::HandleName(void *output, struct wl_output *, const char *name) {
  auto that = static_cast<Output *>(output);
  that->new_name_ = std::string(name);
}
void Output::HandleDescription(void *, struct wl_output *, const char *) {}
}  // namespace jjaro
