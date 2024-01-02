#ifndef JJARO_DELETER_H_
#define JJARO_DELETER_H_ 1

#include <type_traits>

namespace jjaro {
template <auto F>
using Deleter = std::integral_constant<decltype(F), F>;
}  // namespace jjaro
#endif  // JJARO_DELETER_H_
