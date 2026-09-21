# Resolve the numeric CMake package version separately from the richer build
# identity.  CMake's project(VERSION) and package-version helpers only accept
# numeric version components, whereas a checkout may carry prerelease and Git
# revision information.
function(uvpp_resolve_version source_dir)
  set(_uvpp_candidate "")
  set(_uvpp_source "")
  set(_uvpp_git_describe "")

  if(DEFINED UVPP_VERSION AND NOT "${UVPP_VERSION}" STREQUAL "")
    set(_uvpp_candidate "${UVPP_VERSION}")
    set(_uvpp_source "UVPP_VERSION")
  elseif(DEFINED ENV{UVPP_VERSION} AND NOT "$ENV{UVPP_VERSION}" STREQUAL "")
    set(_uvpp_candidate "$ENV{UVPP_VERSION}")
    set(_uvpp_source "UVPP_VERSION environment")
  elseif(EXISTS "${source_dir}/.git")
    find_program(_uvpp_git_command git)
    if(_uvpp_git_command)
      execute_process(
        COMMAND "${_uvpp_git_command}" -C "${source_dir}" describe --tags --long --dirty --match "v[0-9]*"
        RESULT_VARIABLE _uvpp_git_result
        OUTPUT_VARIABLE _uvpp_git_describe
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
      )

      if(_uvpp_git_result EQUAL 0)
        string(REGEX MATCH "^v([0-9]+\\.[0-9]+\\.[0-9]+)(-[0-9A-Za-z][0-9A-Za-z.-]*)?-([0-9]+)-g([0-9a-f]+)(-dirty)?$"
          _uvpp_git_version_match "${_uvpp_git_describe}")
        if(_uvpp_git_version_match)
          set(_uvpp_package_version "${CMAKE_MATCH_1}")
          set(_uvpp_tag_suffix "${CMAKE_MATCH_2}")
          set(_uvpp_commit_count "${CMAKE_MATCH_3}")
          set(_uvpp_revision "${CMAKE_MATCH_4}")
          set(_uvpp_dirty_suffix "${CMAKE_MATCH_5}")

          if(_uvpp_commit_count EQUAL 0)
            set(_uvpp_candidate "${_uvpp_package_version}${_uvpp_tag_suffix}")
            if(_uvpp_dirty_suffix)
              string(APPEND _uvpp_candidate "+dirty")
            endif()
          else()
            set(_uvpp_candidate "${_uvpp_package_version}-dev.${_uvpp_commit_count}+${_uvpp_revision}")
            if(_uvpp_dirty_suffix)
              string(APPEND _uvpp_candidate ".dirty")
            endif()
          endif()
          set(_uvpp_source "Git")
        endif()
      endif()

      if(NOT _uvpp_candidate)
        execute_process(
          COMMAND "${_uvpp_git_command}" -C "${source_dir}" rev-parse --short HEAD
          RESULT_VARIABLE _uvpp_revision_result
          OUTPUT_VARIABLE _uvpp_revision
          OUTPUT_STRIP_TRAILING_WHITESPACE
          ERROR_QUIET
        )
        if(_uvpp_revision_result EQUAL 0)
          set(_uvpp_candidate "0.0.0-dev.0+${_uvpp_revision}")
          set(_uvpp_source "Git (no matching version tag)")
        endif()
      endif()
    endif()
  endif()

  if(NOT _uvpp_candidate AND EXISTS "${source_dir}/VERSION")
    file(READ "${source_dir}/VERSION" _uvpp_candidate)
    string(STRIP "${_uvpp_candidate}" _uvpp_candidate)
    set(_uvpp_source "VERSION")
  endif()

  if(NOT _uvpp_candidate)
    set(_uvpp_candidate "unknown")
    set(_uvpp_source "fallback")
  endif()

  string(REGEX MATCH "^v?([0-9]+\\.[0-9]+\\.[0-9]+)([-+][0-9A-Za-z][0-9A-Za-z.+-]*)?$"
    _uvpp_version_match "${_uvpp_candidate}")
  if(_uvpp_version_match)
    set(_uvpp_package_version "${CMAKE_MATCH_1}")
  elseif(_uvpp_source STREQUAL "fallback")
    set(_uvpp_package_version "0.0.0")
  else()
    message(FATAL_ERROR
      "Invalid uvpp version from ${_uvpp_source}: '${_uvpp_candidate}'. "
      "Expected MAJOR.MINOR.PATCH with optional prerelease or build metadata.")
  endif()

  set(UVPP_PACKAGE_VERSION "${_uvpp_package_version}" PARENT_SCOPE)
  set(UVPP_BUILD_IDENTITY "${_uvpp_candidate}" PARENT_SCOPE)
  set(UVPP_VERSION_SOURCE "${_uvpp_source}" PARENT_SCOPE)
  set(UVPP_GIT_DESCRIBE "${_uvpp_git_describe}" PARENT_SCOPE)
endfunction()
