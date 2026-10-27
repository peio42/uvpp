cmake_minimum_required(VERSION 3.14)

project(uvpp_external_consumer LANGUAGES CXX)

find_package(uvpp CONFIG REQUIRED)

# Optional package-validation guard used by CI after relocating the installation.
# Normal consumers only need find_package() and the public target below.
if(DEFINED UVPP_EXPECTED_INSTALL_PREFIX)
  get_filename_component(expected_prefix "${UVPP_EXPECTED_INSTALL_PREFIX}" REALPATH)
  get_filename_component(package_dir "${uvpp_DIR}" REALPATH)
  get_target_property(uvpp_imported uvpp::uvpp IMPORTED)
  if(NOT uvpp_imported)
    message(FATAL_ERROR "uvpp::uvpp must come from the installed package")
  endif()
  get_target_property(uvpp_includes uvpp::uvpp INTERFACE_INCLUDE_DIRECTORIES)
  foreach(path IN ITEMS "${package_dir}" ${uvpp_includes})
    get_filename_component(absolute_path "${path}" REALPATH)
    file(RELATIVE_PATH relative_path "${expected_prefix}" "${absolute_path}")
    if(IS_ABSOLUTE "${relative_path}" OR relative_path MATCHES "^[.][.](/|$)")
      message(FATAL_ERROR "uvpp package path is outside the installation: ${path}")
    endif()
  endforeach()
endif()

add_executable(uvpp_external_consumer main.cpp)
target_link_libraries(uvpp_external_consumer PRIVATE uvpp::uvpp)

if(WIN32)
  # Application-local Windows header policy; no repository validation target.
  target_compile_definitions(uvpp_external_consumer PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
