#ifndef JJARO_SERVER_H_
#define JJARO_SERVER_H_ 1

#include <absl/base/thread_annotations.h>
#include <absl/synchronization/mutex.h>
#include <sdbus-c++/AdaptorInterfaces.h>
#include <sdbus-c++/IConnection.h>

#include <cstdint>
#include <list>
#include <string>

#include "ddclight-server-glue.h"
#include "enumerate.h"
#include "output.h"
#include "state.h"

namespace jjaro {

class DDCLight final
    : public sdbus::AdaptorInterfaces<org::jjaro::DDCLight_adaptor> {
 public:
  DDCLight(sdbus::IConnection& connection, sdbus::ObjectPath objectPath);
  ~DDCLight();

 private:
  void AddOutput(uint32_t name, uint32_t version);
  void RemoveOutput(uint32_t name);
  int64_t get() override;
  int64_t poke() override;
  int64_t set(const int64_t& percentage) override;
  int64_t increment(const int64_t& percentage) override;
  int64_t decrement(const int64_t& percentage) override;

  State state_;
  absl::Mutex lock_;
  std::list<Output> outputs_ ABSL_GUARDED_BY(lock_);
  Enumerator enumerator_;
};

}  // namespace jjaro
#endif  // JJARO_SERVER_H_
