#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>

namespace uv::fs {

// Converts a C++ filesystem path into the owned byte string accepted by
// libuv filesystem APIs. This is an encoding conversion only: it does not
// normalize, resolve, or access the filesystem.
[[nodiscard]] inline std::string path_argument(const std::filesystem::path& path) {
  const auto& native = path.native();
  if (native.find(std::filesystem::path::value_type{}) != std::filesystem::path::string_type::npos) {
    throw std::invalid_argument("filesystem path must not contain NUL");
  }

#if defined(_WIN32)
  const auto encoded = path.u8string();
  std::string result;
  result.reserve(encoded.size());
  for (const auto byte : encoded) {
    result.push_back(static_cast<char>(byte));
  }
  return result;
#else
  return native;
#endif
}

} // namespace uv::fs
