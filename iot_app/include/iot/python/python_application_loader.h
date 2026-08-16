#pragma once

#include "iot/python/python_application.h"

#include <cstddef>
#include <filesystem>

namespace iot {
namespace python {

/** Loads a single-file Python application from disk. */
class PythonApplicationLoader {
public:
  /**
   * Sets the largest Python entry-point file this loader will accept.
   *
   * The limit stops a bad package from filling the device's memory.
   */
  explicit PythonApplicationLoader(std::size_t maximumSourceSizeInBytes);

  /**
   * Reads `app.json` and the entry-point file from `applicationDirectory`.
   *
   * `app.json` must contain `id`, `name`, and `entry_point`. The
   * entry point must be a normal file inside the app directory. Absolute paths
   * and paths containing `..` are rejected.
   */
  PythonApplication load(const std::filesystem::path &applicationDirectory) const;

private:
  std::size_t maximumSourceSizeInBytes_{0};
};

} // namespace python
} // namespace iot
