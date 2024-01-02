#ifndef JJARO_ENUMERATE_H_
#define JJARO_ENUMERATE_H_ 1

#include <wayland-client.h>

#include <absl/functional/any_invocable.h>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include "deleter.h"

namespace jjaro {
class Enumerator {
 public:
  Enumerator(
      absl::AnyInvocable<void(uint32_t name, uint32_t version)> add_output,
      absl::AnyInvocable<void(uint32_t name)> remove_output);
  ~Enumerator();

  struct wl_registry *registry() const { return registry_.get(); }

 private:
  static void WaylandThreadLoop(struct wl_display *display);
  static void HandleGlobal(void *enumerator, struct wl_registry *,
                           uint32_t name, const char *interface,
                           uint32_t version);
  static void HandleGlobalRemove(void *enumerator, struct wl_registry *,
                                 uint32_t name);

  static constexpr struct wl_registry_listener kRegistryListener {
    .global = HandleGlobal, .global_remove = HandleGlobalRemove
  };
  absl::AnyInvocable<void(uint32_t name, uint32_t version)> add_output_;
  absl::AnyInvocable<void(uint32_t name)> remove_output_;
  std::unique_ptr<struct wl_display, Deleter<wl_display_disconnect>> display_;
  std::unique_ptr<struct wl_registry, Deleter<wl_registry_destroy>> registry_;
  std::vector<uint32_t> output_names_;
  std::thread thread_;
};
}  // namespace jjaro
#endif  // JJARO_ENUMERATE_H_
