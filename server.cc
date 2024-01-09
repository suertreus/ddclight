#include "server.h"

#include <absl/functional/any_invocable.h>
#include <absl/synchronization/mutex.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>

#include "output.h"
#include "state.h"

namespace jjaro {

DDCLight::DDCLight(sdbus::IConnection& connection, std::string objectPath)
    : AdaptorInterfaces(connection, std::move(objectPath)),
      enumerator_(
          [this](uint32_t name, uint32_t version) { AddOutput(name, version); },
          [this](uint32_t name) { RemoveOutput(name); }) {
  registerAdaptor();
}
DDCLight::~DDCLight() { unregisterAdaptor(); }

void DDCLight::AddOutput(uint32_t name, uint32_t version) {
  outputs_.emplace_back(&state_, &enumerator_, name, version);
}
void DDCLight::RemoveOutput(uint32_t name) {
  for (auto it = outputs_.cbegin(); it != outputs_.cend(); ++it) {
    if (it->wayland_name() != name) continue;
    outputs_.erase(it);
  }
}

int64_t DDCLight::get() {
  absl::MutexLock l(&state_.lock);
  return state_.desired_percentage.value_or(50);
}
int64_t DDCLight::poke() {
  emitWatch(state_.desired_percentage.value_or(50));
  return state_.desired_percentage.value_or(50);
}
int64_t DDCLight::set(const int64_t& percentage) {
  const int real_percentage = std::clamp(percentage, int64_t{0}, int64_t{100});
  absl::MutexLock l(&state_.lock);
  if (state_.desired_percentage.has_value() &&
      *state_.desired_percentage == real_percentage)
    return *state_.desired_percentage;
  state_.desired_percentage = real_percentage;
  emitWatch(*state_.desired_percentage);
  return *state_.desired_percentage;
}
int64_t DDCLight::increment(const int64_t& percentage) {
  const int real_percentage = std::clamp(percentage, int64_t{0}, int64_t{100});
  absl::MutexLock l(&state_.lock);
  if (real_percentage == 0) return state_.desired_percentage.value_or(50);
  if (state_.desired_percentage.has_value() &&
      *state_.desired_percentage == 100)
    return *state_.desired_percentage;
  state_.desired_percentage = std::min(
      int64_t{100}, state_.desired_percentage.value_or(50) + percentage);
  emitWatch(*state_.desired_percentage);
  return *state_.desired_percentage;
}
int64_t DDCLight::decrement(const int64_t& percentage) {
  const int real_percentage = std::clamp(percentage, int64_t{0}, int64_t{100});
  absl::MutexLock l(&state_.lock);
  if (real_percentage == 0) return state_.desired_percentage.value_or(50);
  if (state_.desired_percentage.has_value() && *state_.desired_percentage == 0)
    return *state_.desired_percentage;
  state_.desired_percentage =
      std::max(int64_t{0}, state_.desired_percentage.value_or(50) - percentage);
  emitWatch(*state_.desired_percentage);
  return *state_.desired_percentage;
}

}  // namespace jjaro
