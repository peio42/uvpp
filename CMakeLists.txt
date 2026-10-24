cmake_minimum_required(VERSION 3.14)

include(cmake/uvppVersion.cmake)
uvpp_resolve_version("${CMAKE_CURRENT_SOURCE_DIR}")

message(STATUS "uvpp package version: ${UVPP_PACKAGE_VERSION} (${UVPP_VERSION_SOURCE})")
message(STATUS "uvpp build identity: ${UVPP_BUILD_IDENTITY}")
if(UVPP_GIT_DESCRIBE)
  message(STATUS "uvpp Git describe: ${UVPP_GIT_DESCRIBE}")
endif()

project(uvpp
  VERSION ${UVPP_PACKAGE_VERSION}
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

# Repository validation is opt-in: consumers need neither Google Test nor examples.
option(UVPP_BUILD_TESTS "Build the repository test suites" OFF)
option(UVPP_BUILD_EXAMPLES "Build the repository examples" OFF)

if(UVPP_BUILD_TESTS)
  enable_testing()
  find_package(GTest CONFIG REQUIRED)
  file(GLOB UVPP_TEST_SOURCES CONFIGURE_DEPENDS tests/test-*.cpp)
  add_executable(uvpp-tests tests/main.cpp ${UVPP_TEST_SOURCES})
  add_executable(uvpp-udp-allocation tests/main.cpp tests/udp-allocation.cpp)
  foreach(target uvpp-tests uvpp-udp-allocation)
    target_link_libraries(${target} PRIVATE uvpp::uvpp GTest::gtest)
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES TIMEOUT 180)
  endforeach()
endif()

if(UVPP_BUILD_EXAMPLES)
  file(GLOB UVPP_EXAMPLE_SOURCES CONFIGURE_DEPENDS examples/*.cpp)
  foreach(source IN LISTS UVPP_EXAMPLE_SOURCES)
    get_filename_component(name "${source}" NAME_WE)
    add_executable(uvpp-example-${name} "${source}")
    target_link_libraries(uvpp-example-${name} PRIVATE uvpp::uvpp)
  endforeach()
endif()

if(MSVC)
  # Apply conforming C++20 parsing to repository binaries, without imposing
  # warning or conformance flags on downstream users of the interface target.
  get_directory_property(uvpp_targets BUILDSYSTEM_TARGETS)
  foreach(target IN LISTS uvpp_targets)
    if(NOT target STREQUAL "uvpp")
      target_compile_options(${target} PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
      target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    endif()
  endforeach()
endif()
