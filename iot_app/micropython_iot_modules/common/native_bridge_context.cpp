#include "native_bridge_context.h"

#include <stdexcept>
#include <string>

namespace iot {
namespace python {
namespace internal {

MicroPythonApplicationContext &requireActiveApplicationContext(const char *moduleName) {
  auto *activeContext = MicroPythonApplicationContext::active();
  if (activeContext == nullptr) {
    const std::string readableModuleName = moduleName == nullptr || moduleName[0] == '\0' ? "native" : moduleName;
    throw std::logic_error("Python " + readableModuleName + " module is not connected to the application runtime");
  }
  return *activeContext;
}

} // namespace internal
} // namespace python
} // namespace iot
