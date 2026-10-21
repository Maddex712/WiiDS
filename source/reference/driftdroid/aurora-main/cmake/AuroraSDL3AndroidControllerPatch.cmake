include_guard(GLOBAL)

# Keep the fetched SDL fix reproducible without storing a fork or relying on
# edits in a generated build tree. Reverse-check makes reconfiguration harmless.
function(aurora_sdl3_patch_android_controller_poll source_dir)
  find_package(Git REQUIRED)
  set(_patch "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/patches/sdl3-android-controller-jni-thread.patch")
  set(_source "${source_dir}/src/core/android/SDL_android.c")
  # Explicit --git-dir prevents Git discovering an enclosing repository and
  # silently skipping paths when SDL lives in its ignored build subdirectory.
  # --no-index permits the normal FetchContent tarball without a .git directory.
  set(_git_apply "${GIT_EXECUTABLE}" "--git-dir=${source_dir}/.git" apply --no-index)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_patch}")
  execute_process(
    COMMAND ${_git_apply} --reverse --check "${_patch}"
    WORKING_DIRECTORY "${source_dir}"
    RESULT_VARIABLE _already_patched
    OUTPUT_QUIET ERROR_QUIET
  )
  if (_already_patched EQUAL 0)
    file(READ "${_source}" _source_text)
    if (NOT _source_text MATCHES "controller enumeration uses a dedicated JNI thread")
      message(FATAL_ERROR "SDL Android JNI reverse check passed without the expected source marker")
    endif ()
    return()
  endif ()
  execute_process(
    COMMAND ${_git_apply} --check "${_patch}"
    WORKING_DIRECTORY "${source_dir}"
    RESULT_VARIABLE _check_result
    ERROR_VARIABLE _check_error
  )
  if (NOT _check_result EQUAL 0)
    message(FATAL_ERROR "SDL Android controller JNI patch no longer matches this SDL source: ${_check_error}")
  endif ()
  execute_process(
    COMMAND ${_git_apply} "${_patch}"
    WORKING_DIRECTORY "${source_dir}"
    RESULT_VARIABLE _apply_result
    ERROR_VARIABLE _apply_error
  )
  if (NOT _apply_result EQUAL 0)
    message(FATAL_ERROR "Could not apply SDL Android controller JNI patch: ${_apply_error}")
  endif ()
  file(READ "${_source}" _source_text)
  if (NOT _source_text MATCHES "controller enumeration uses a dedicated JNI thread")
    message(FATAL_ERROR "SDL Android JNI patch reported success without changing the target source")
  endif ()
  message(STATUS "aurora: Patched SDL Android controller polling to use a normal JNI thread")
endfunction()
