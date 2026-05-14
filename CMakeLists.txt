cmake_minimum_required(VERSION 3.14)

project(uvpp
  VERSION 2.0.0
  DESCRIPTION "Header-only C++20 wrapper around libuv"
  LANGUAGES CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(GNUInstallDirs)

# ──── Interface library ───────────────────────────────────────────────────────

add_library(uvpp INTERFACE)
add_library(uvpp::uvpp ALIAS uvpp)

target_compile_features(uvpp INTERFACE cxx_std_20)

target_include_directories(uvpp INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

find_package(LibUV REQUIRED)
find_package(Threads REQUIRED)

target_link_libraries(uvpp INTERFACE LibUV::LibUV Threads::Threads)

# ──── Installation ────────────────────────────────────────────────────────────

include(CMakePackageConfigHelpers)

install(
  TARGETS uvpp
  EXPORT uvppTargets
)

install(
  DIRECTORY include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
  EXPORT uvppTargets
  NAMESPACE uvpp::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/uvpp
)

configure_package_config_file(
  cmake/uvppConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/uvppConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/uvpp
)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/uvppConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)

install(
  FILES
    ${CMAKE_CURRENT_BINARY_DIR}/uvppConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/uvppConfigVersion.cmake
    cmake/FindLibUV.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/uvpp
)
