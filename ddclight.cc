#include <absl/functional/any_invocable.h>
#include <absl/strings/numbers.h>
#include <absl/strings/str_format.h>
#include <absl/strings/string_view.h>
#include <sdbus-c++/IConnection.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>

#include "client.h"
#include "server.h"

int main(int argc, char** argv) {
  if (argc == 2 && argv && argv[1] && absl::string_view(argv[1]) == "get") {
    auto connection = sdbus::createSessionBusConnection();
    absl::PrintF("%d ddclight\n",
                 jjaro::DDCLightProxy(*connection, sdbus::ServiceName("org.jjaro.ddclight"),
                                      sdbus::ObjectPath("/org/jjaro/ddclight"))
                     .get());
    return EXIT_SUCCESS;
  } else if (argc == 2 && argv && argv[1] &&
             absl::string_view(argv[1]) == "poke") {
    auto connection = sdbus::createSessionBusConnection();
    absl::PrintF("%d ddclight\n",
                 jjaro::DDCLightProxy(*connection, sdbus::ServiceName("org.jjaro.ddclight"),
                                      sdbus::ObjectPath("/org/jjaro/ddclight"))
                     .poke());
    return EXIT_SUCCESS;
  } else if (argc == 3 && argv && argv[1] &&
             absl::string_view(argv[1]) == "set") {
    int64_t arg;
    if (argv[2] && absl::SimpleAtoi(argv[2], &arg)) {
      auto connection = sdbus::createSessionBusConnection();
      absl::PrintF("%d ddclight\n",
                 jjaro::DDCLightProxy(*connection, sdbus::ServiceName("org.jjaro.ddclight"),
                                      sdbus::ObjectPath("/org/jjaro/ddclight"))
                       .set(arg));
      return EXIT_SUCCESS;
    }
  } else if (argc == 3 && argv && argv[1] &&
             absl::string_view(argv[1]) == "increment") {
    int64_t arg;
    if (argv[2] && absl::SimpleAtoi(argv[2], &arg)) {
      auto connection = sdbus::createSessionBusConnection();
      absl::PrintF("%d ddclight\n",
                 jjaro::DDCLightProxy(*connection, sdbus::ServiceName("org.jjaro.ddclight"),
                                      sdbus::ObjectPath("/org/jjaro/ddclight"))
                       .increment(arg));
      return EXIT_SUCCESS;
    }
  } else if (argc == 3 && argv && argv[1] &&
             absl::string_view(argv[1]) == "decrement") {
    int64_t arg;
    if (argv[2] && absl::SimpleAtoi(argv[2], &arg)) {
      auto connection = sdbus::createSessionBusConnection();
      absl::PrintF("%d ddclight\n",
                 jjaro::DDCLightProxy(*connection, sdbus::ServiceName("org.jjaro.ddclight"),
                                      sdbus::ObjectPath("/org/jjaro/ddclight"))
                       .decrement(arg));
      return EXIT_SUCCESS;
    }
  } else if (argc == 2 && argv && argv[1] &&
             absl::string_view(argv[1]) == "daemon") {
    const sdbus::ServiceName svc("org.jjaro.ddclight");
    auto connection = sdbus::createSessionBusConnection(svc);
    jjaro::DDCLight ddc(*connection, sdbus::ObjectPath("/org/jjaro/ddclight"));
    connection->enterEventLoop();
    return EXIT_SUCCESS;
  } else if (argc == 2 && argv && argv[1] &&
             absl::string_view(argv[1]) == "watch") {
    (void)setvbuf(stdout, nullptr, _IOLBF, 0);
    auto connection = sdbus::createSessionBusConnection();
    jjaro::DDCLightProxy client(
        *connection, sdbus::ServiceName("org.jjaro.ddclight"), sdbus::ObjectPath("/org/jjaro/ddclight"),
        [](int64_t percentage) { absl::PrintF("%d\n", percentage); });
    absl::PrintF("%d\n", client.get());
    connection->enterEventLoop();
    return EXIT_SUCCESS;
  }
  absl::FPrintF(stderr,
                "Usage:\n"
                "  %1$s get\n"
                "  %1$s poke\n"
                "  %1$s watch\n"
                "  %1$s set <percentage>\n"
                "  %1$s increment <percentage>\n"
                "  %1$s decrement <percentage>\n"
                "  %1$s daemon\n",
                argc >= 1 && argv && argv[0] ? argv[0] : "ddclight");
  return EXIT_FAILURE;
}
