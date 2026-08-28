#pragma once

#include "iot/python/python_application.h"

#include <cstddef>
#include <filesystem>

namespace iot {
namespace python {

/* Loads a single-file Python application already stored on disk. */
class PythonApplicationLoader {
public:
  /*
   * maximumSourceSizeInBytes is the maximum number of bytes allowed in the
   * Python entry-point file, such as main.py. The loader rejects an application
   * when its entry-point file exceeds this limit, before reading the complete
   * file into memory. This prevents an oversized or damaged application from
   * consuming too much memory.
   */
  explicit PythonApplicationLoader(std::size_t maximumSourceSizeInBytes);

  /*
   * Reads app.json and the entry-point file from applicationDirectory.
   *
   * app.json must contain id, name, and entry_point. The entry point must be a
   * normal file below the application directory. Absolute paths and paths that
   * enter .. are rejected.
   */
  PythonApplication load(const std::filesystem::path &applicationDirectory) const;

private:
  std::size_t m_maximumSourceSizeInBytes{0};
};

} // namespace python
} // namespace iot
