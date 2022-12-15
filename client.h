#ifndef JJARO_CLIENT_H_
#define JJARO_CLIENT_H_ 1
#include <sdbus-c++/sdbus-c++.h>

#include <cstdint>
#include <functional>
#include <string>
#include <utility>

#include "ddclight-client-glue.h"

namespace jjaro {

class DDCLightProxy final
    : public sdbus::ProxyInterfaces<org::jjaro::DDCLight_proxy> {
 public:
  DDCLightProxy(
      sdbus::IConnection& connection, std::string destination,
      std::string objectPath,
      std::function<void(int64_t)> watch = [](int64_t) {})
      : ProxyInterfaces(connection, std::move(destination),
                        std::move(objectPath)),
        watch_(std::move(watch)) {
    registerProxy();
  }

  ~DDCLightProxy() { unregisterProxy(); }

 private:
  void onWatch(const int64_t& percentage) override { watch_(percentage); }

  std::function<void(int64_t)> watch_;
};

}  // namespace jjaro
#endif  // JJARO_CLIENT_H_
