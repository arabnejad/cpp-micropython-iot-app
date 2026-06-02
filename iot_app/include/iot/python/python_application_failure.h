#pragma once

#include <string>

namespace iot {
namespace python {

/** Details of the latest Python error shown by the emergency screen. */
struct PythonApplicationFailure {
  std::string applicationName;
  std::string phase;
  std::string timestamp;
  std::string traceback;
};

} // namespace python
} // namespace iot
