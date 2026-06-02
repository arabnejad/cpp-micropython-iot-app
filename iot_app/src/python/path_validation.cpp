#include "iot/python/path_validation.h"

namespace iot {
namespace python {

bool isSafeRelativePath(const std::filesystem::path &path) {
  if (path.empty() || path.is_absolute()) {
    return false;
  }
  for (const auto &part : path) {
    if (part == "..") {
      return false;
    }
  }
  return true;
}

} // namespace python
} // namespace iot
