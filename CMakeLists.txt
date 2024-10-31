cmake_minimum_required(VERSION 3.19)

# Crash and burn if IMPORTED_LOCATION is set to NOT-FOUND -- 3.19
cmake_policy(SET CMP0111 NEW)
# Sanitize RPATHs et al -- 3.9
cmake_policy(SET CMP0068 NEW)

set(CMAKE_CONFIGURATION_TYPES Debug Release)

project(gstcef)

if (APPLE)
  enable_language(CXX OBJCXX)
  set(CMAKE_OBJCXX_STANDARD 14)
  set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
endif()

set_property(GLOBAL PROPERTY OS_FOLDERS ON)

find_package(PkgConfig REQUIRED)

pkg_check_modules(PC_GLib glib-2.0 REQUIRED IMPORTED_TARGET)

if(POLICY CMP0074)
    #policy for <PackageName>_ROOT variables
    cmake_policy(SET CMP0074 NEW)
endif()

if (NOT DEFINED CEF_VERSION)
  set(CEF_VERSION "122.1.13+gde5b724+chromium-122.0.6261.130")
endif()

STRING(REGEX REPLACE "\\+" "%2B" CEF_ESCAPED_VERSION ${CEF_VERSION})

# Setting an option prior to it existing is verboten.
# https://cmake.org/cmake/help/latest/policy/CMP0077.html
# https://gitlab.kitware.com/cmake/cmake/-/issues/20312
# https://cmake.org/cmake/help/latest/module/FetchContent.html#overriding-where-to-find-cmakelists-txt
set(USE_SANDBOX OFF CACHE INTERNAL "")
# Determine the platform.
if (APPLE)
  set(USE_SANDBOX ON CACHE INTERNAL "")
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm")
    set(CEF_PLATFORM "macosarm64")
  else()
    set(CEF_PLATFORM "macosx64")
  endif()
elseif("${CMAKE_SYSTEM_NAME}" STREQUAL "Linux")
  if(CMAKE_SIZEOF_VOID_P MATCHES 8)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm" OR CMAKE_SYSTEM_PROCESSOR MATCHES "aarch")
      set(CEF_PLATFORM "linuxarm64")
      # cef_variables doesn't handle linux/arm64 case yet. See
      # https://bitbucket.org/chromiumembedded/cef/issues/3417/project-arch-detection-support-is-missing
      set(PROJECT_ARCH "arm64")
    else()
      set(CEF_PLATFORM "linux64")
    endif()
  else()
    set(CEF_PLATFORM "linux32")
  endif()
elseif("${CMAKE_SYSTEM_NAME}" STREQUAL "Windows")
  if(CMAKE_SIZEOF_VOID_P MATCHES 8)
    set(CEF_PLATFORM "windows64")
  else()
    set(CEF_PLATFORM "windows32")
  endif()
endif()

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(DownloadCEF)
DownloadCEF("${CEF_PLATFORM}" "${CEF_VERSION}" "${CEF_ESCAPED_VERSION}" "${CMAKE_SOURCE_DIR}/third_party/cef")
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CEF_ROOT}/cmake")

find_package(CEF REQUIRED)
add_subdirectory(${CEF_LIBCEF_DLL_WRAPPER_PATH} libcef_dll_wrapper)

set(GSTCEF_SRCS
  gstcef.cc
  gstcefsrc.cc
  gstcefdemux.cc
  gstcefbin.cc
  gstcefaudiometa.cc
  gstcefloader.cc
)

set(GSTCEFSUBPROCESS_SRCS
  gstcefsubprocess.cc
)

if (APPLE)
list(APPEND GSTCEF_SRCS gstcefloader.cc)
list(APPEND GSTCEFSUBPROCESS_SRCS gstcefloader.cc)
endif()

pkg_check_modules(GST REQUIRED gstreamer-1.0
                  gstreamer-video-1.0
                  gstreamer-audio-1.0)

if(USE_SANDBOX)
  message("Using the CEF sandbox.")
  set(CMAKE_MACOSX_RPATH 1)
  set(CMAKE_INSTALL_NAME_DIR "@rpath")
  add_compile_definitions("-DGST_CEF_USE_SANDBOX")
endif()
if (USE_SANDBOX AND (NOT DEFINED OS_LINUX))
  ADD_LOGICAL_TARGET("libcef_lib" "${CEF_SANDBOX_LIB_DEBUG}" "${CEF_SANDBOX_LIB_RELEASE}")
else()
  ADD_LOGICAL_TARGET("libcef_lib" "${CEF_LIB_DEBUG}" "${CEF_LIB_RELEASE}")
endif()

SET_CEF_TARGET_OUT_DIR()

link_directories(${GST_LIBRARY_DIRS})

add_library("gstcef" SHARED ${GSTCEF_SRCS})
SET_LIBRARY_TARGET_PROPERTIES("gstcef")
add_dependencies("gstcef" libcef_dll_wrapper)
target_link_libraries("gstcef" libcef_dll_wrapper ${CEF_STANDARD_LIBS} ${GST_LIBRARIES})
if (CMAKE_BUILD_TYPE STREQUAL "Release" AND MSVC)
  target_link_options("gstcef" PRIVATE "/DEBUG")
endif()
if (APPLE)
  target_sources("gstcef" PRIVATE gstcefnsapplication.mm)
endif()
target_link_libraries("gstcef" libcef_lib PkgConfig::PC_GLib)
target_include_directories("gstcef" PUBLIC ${GST_INCLUDE_DIRS})
set_target_properties("gstcef" PROPERTIES INSTALL_RPATH "$ORIGIN")
set_target_properties("gstcef" PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE)
set_target_properties("gstcef" PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CEF_TARGET_OUT_DIR})

install(
  TARGETS "gstcef"
  DESTINATION .
)

if (NOT APPLE)
  add_executable("gstcefsubprocess" ${GSTCEFSUBPROCESS_SRCS})
  SET_EXECUTABLE_TARGET_PROPERTIES("gstcefsubprocess")
  target_link_libraries("gstcefsubprocess" libcef_dll_wrapper ${CEF_STANDARD_LIBS})
  target_link_libraries("gstcefsubprocess" libcef_lib PkgConfig::PC_GLib)
  set_target_properties("gstcefsubprocess" PROPERTIES INSTALL_RPATH "$ORIGIN")
  target_include_directories("gstcefsubprocess" PUBLIC ${GST_INCLUDE_DIRS})
  set_target_properties("gstcefsubprocess" PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE)
  set_target_properties("gstcefsubprocess" PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CEF_TARGET_OUT_DIR})

  install(
    TARGETS "gstcefsubprocess"
    DESTINATION .
  )
  COPY_FILES("gstcef" "${CEF_BINARY_FILES}" "${CEF_BINARY_DIR}" "${CEF_TARGET_OUT_DIR}")
  COPY_FILES("gstcef" "${CEF_RESOURCE_FILES}" "${CEF_RESOURCE_DIR}" "${CEF_TARGET_OUT_DIR}")
  install(
    DIRECTORY "${CEF_RESOURCE_DIR}" "${CEF_BINARY_DIR}"
    DESTINATION .
    USE_SOURCE_PERMISSIONS
  )
else()
  # Unlike in other OSes, CEF on macOS relies on several independent "helper"
  # app bundles to call up different sandboxed aspects of the CEF process.
  # These are:
  # - the main subprocess (gstcefsubprocess)
  # - "Alerts"
  # - "Renderer"
  # - "GPU"
  # This is cleverly (un)documented in e.g.
  # https://github.com/chromiumembedded/cef/blob/master/tests/cefsimple/CMakeLists.txt.in#L131-L132
  # Without these residing in *at least* the same folder as `gstcefsubprocess.app`, CEF will refuse to run.
  # The logging severity was also modified so that this is visible in the
  # GStreamer logs.
  foreach(_suffix_list ${CEF_HELPER_APP_SUFFIXES})
    # Convert to a list and extract the suffix values.
    string(REPLACE ":" ";" _suffix_list ${_suffix_list})
    list(GET _suffix_list 0 _name_suffix)
    list(GET _suffix_list 1 _target_suffix)
    list(GET _suffix_list 2 _plist_suffix)

    # Define Helper target and output names.
    set(_helper_target "gstcefsubprocess${_target_suffix}")
    set(_helper_output_name "gstcefsubprocess${_name_suffix}")

    # Create Helper executable target.
    add_executable(${_helper_target} ${GSTCEFSUBPROCESS_SRCS})
    SET_EXECUTABLE_TARGET_PROPERTIES(${_helper_target})
    add_dependencies(${_helper_target} libcef_dll_wrapper)
    target_link_libraries(${_helper_target} libcef_dll_wrapper ${CEF_STANDARD_LIBS})
    target_link_options(${_helper_target} PRIVATE "${GST_LDFLAGS}")
    set_target_properties(${_helper_target} PROPERTIES
      MACOSX_BUNDLE TRUE
      MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/gstcefinfo.plist.in"
      OUTPUT_NAME ${_helper_output_name}
      MACOSX_BUNDLE_BUNDLE_NAME ${_helper_output_name}
      MACOSX_BUNDLE_GUI_IDENTIFIER "org.gstreamer.gstcefsubprocess${_plist_suffix}"
      )

    target_link_libraries(${_helper_target} libcef_lib PkgConfig::PC_GLib)

    # Add the Helper as a dependency of the main executable target.
    add_dependencies(gstcef "${_helper_target}")

    install(
      TARGETS "${_helper_target}"
      DESTINATION .
    )
  endforeach()

  # Copy framework for testing
  COPY_FILES("gstcef" "Chromium\ Embedded\ Framework.framework" "${CEF_BINARY_DIR}" "${CEF_TARGET_OUT_DIR}")
  install(
    DIRECTORY "${CEF_BINARY_DIR}/Chromium Embedded Framework.framework" 
    DESTINATION .
    USE_SOURCE_PERMISSIONS
  )
endif()
