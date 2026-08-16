#pragma once

#include <filesystem>

namespace iot {
namespace python {

/** Returns true for a non-empty relative path that never enters `..`. */
bool isSafeRelativePath(const std::filesystem::path &path);

} // namespace python
} // namespace iot
