# FindLibUV
# ---------
# Finds the libuv C library and defines the imported target LibUV::LibUV.
#
# Result variables
#   LibUV_FOUND           - true if the library was found
#   LibUV_VERSION         - version string reported by pkg-config (if available)
#
# Imported targets
#   LibUV::LibUV          - the libuv library

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(PC_LibUV QUIET libuv)
endif()

find_path(LibUV_INCLUDE_DIR
  NAMES uv.h
  HINTS ${PC_LibUV_INCLUDE_DIRS}
)

find_library(LibUV_LIBRARY
  NAMES uv libuv
  HINTS ${PC_LibUV_LIBRARY_DIRS}
)

set(LibUV_VERSION "${PC_LibUV_VERSION}")

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibUV
  REQUIRED_VARS LibUV_LIBRARY LibUV_INCLUDE_DIR
  VERSION_VAR LibUV_VERSION
)

if(LibUV_FOUND AND NOT TARGET LibUV::LibUV)
  add_library(LibUV::LibUV UNKNOWN IMPORTED)
  set_target_properties(LibUV::LibUV PROPERTIES
    IMPORTED_LOCATION "${LibUV_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${LibUV_INCLUDE_DIR}"
  )
endif()

mark_as_advanced(LibUV_INCLUDE_DIR LibUV_LIBRARY)
