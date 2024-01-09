#include "enumerate.h"

#include <absl/functional/any_invocable.h>
#include <absl/strings/str_format.h>
#include <absl/strings/string_view.h>
#include <wayland-util.h>

#include <cstdio>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

namespace jjaro {
Enumerator::Enumerator(
    absl::AnyInvocable<void(uint32_t name, uint32_t version)> add_output,
    absl::AnyInvocable<void(uint32_t name)> remove_output)
    : add_output_(std::move(add_output)),
      remove_output_(std::move(remove_output)),
      display_(nullptr),
      registry_(nullptr) {
  display_.reset(wl_display_connect(nullptr));
  if (!display_) {
    fputs(
        "Unable to connect to Wayland display; no outputs will be adjusted.\n",
        stderr);
    return;
  }
  thread_ = std::thread(WaylandThreadLoop, display_.get());
  registry_.reset(wl_display_get_registry(display_.get()));
  if (!registry_) {
    fputs(
        "Unable to connect to Wayland registry; no outputs will be adjusted.\n",
        stderr);
    return;
  }
  if (const int ret =
          wl_registry_add_listener(registry_.get(), &kRegistryListener, this);
      ret) {
    absl::FPrintF(stderr,
                  "Unable to listen to Wayland registry; no outputs will be "
                  "adjusted (%d).\n",
                  ret);
  }
}

Enumerator::~Enumerator() {
  registry_.reset();
  display_.reset();
  thread_.join();
}

void Enumerator::WaylandThreadLoop(struct wl_display *display) {
  while (wl_display_dispatch(display) != -1)
    ;
}
void Enumerator::HandleGlobal(void *enumerator, struct wl_registry *,
                              uint32_t name, const char *interface,
                              uint32_t version) {
  auto that = static_cast<Enumerator *>(enumerator);
  if (absl::string_view(interface) != wl_output_interface.name) return;
  that->output_names_.push_back(name);
  that->add_output_(name, version);
}
void Enumerator::HandleGlobalRemove(void *enumerator, struct wl_registry *,
                                    uint32_t name) {
  auto that = static_cast<Enumerator *>(enumerator);
  for (auto it = that->output_names_.cbegin(); it != that->output_names_.cend();
       ++it) {
    if (*it != name) continue;
    that->output_names_.erase(it);
    that->remove_output_(name);
    return;
  }
}
}  // namespace jjaro
