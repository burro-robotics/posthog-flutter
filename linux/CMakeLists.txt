# Project-level configuration.
cmake_minimum_required(VERSION 3.10)
project(posthog_flutter_plugin LANGUAGES CXX)

# Define the plugin name here. The name format is strict.
#
# * Must match the name used in the Dart plugin registration.
# * Must match the name given in the `pubspec.yaml` `pluginClass` field.
# * Is case-sensitive. Use lower_case_with_underscores.
#
# This is passed to Flutter's plugin registration to find the plugin.
set(PLUGIN_NAME "posthog_flutter_plugin")

# Any new source files that you add to your plugin should be added here.
list(APPEND PLUGIN_SOURCES
  "posthog_flutter_plugin.cc"
  "http_client.cc"
  "storage_manager.cc"
  "feature_flags_manager.cc"
  "session_replay_manager.cc"
  "posthog_logger.cc"
)

# Any new header files that you add to your plugin should be added here.
list(APPEND PLUGIN_HEADERS
  "posthog_flutter_plugin.h"
  "http_client.h"
  "storage_manager.h"
  "feature_flags_manager.h"
  "session_replay_manager.h"
  "posthog_models.h"
  "posthog_logger.h"
)

# List of absolute paths to libraries that should be bundled with the plugin.
# This list could contain prebuilt libraries, or library helper functions
# should be added here.
list(APPEND PLUGIN_BUNDLED_LIBRARIES
)

# The name of the plugin archive. It is also the name of the plugin's CMake
# target. The name format is strict.
#
# * Must match the name of the directory containing this `CMakeLists.txt`.
# * Must match the name given in the `pubspec.yaml` `pluginClass` field.
# * Is case-sensitive. Use snake_case.
add_library(${PLUGIN_NAME} STATIC
  "${PLUGIN_SOURCES}"
  "${PLUGIN_HEADERS}"
)

# Apply a standard set of build settings. This can be removed for plugins that
# want full control over build settings.
apply_standard_settings(${PLUGIN_NAME})

# Set C++17 standard for modern C++ features (structured bindings, etc.)
set_property(TARGET ${PLUGIN_NAME} PROPERTY CXX_STANDARD 17)
set_property(TARGET ${PLUGIN_NAME} PROPERTY CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(CURL REQUIRED libcurl)
pkg_check_modules(SQLITE3 REQUIRED sqlite3)

# Optional JPEG compression support (requires both libpng and libjpeg-turbo)
# Falls back gracefully if not available
pkg_check_modules(PNG libpng)
pkg_check_modules(JPEG libjpeg-turbo)
if(PNG_FOUND AND JPEG_FOUND)
  add_definitions(-DHAVE_JPEG)
  message(STATUS "JPEG compression enabled (libpng and libjpeg-turbo found)")
else()
  if(NOT PNG_FOUND)
    message(STATUS "JPEG compression disabled (libpng not found, will use PNG)")
  endif()
  if(NOT JPEG_FOUND)
    message(STATUS "JPEG compression disabled (libjpeg-turbo not found, will use PNG)")
  endif()
endif()

# Include directories
# Set up include path so that <posthog_flutter/posthog_flutter_plugin.h> resolves correctly
target_include_directories(${PLUGIN_NAME} PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/.."
  "${CMAKE_CURRENT_SOURCE_DIR}/include"  # For nlohmann/json.hpp
  ${CURL_INCLUDE_DIRS}
  ${SQLITE3_INCLUDE_DIRS}
)

if(PNG_FOUND)
  target_include_directories(${PLUGIN_NAME} PUBLIC ${PNG_INCLUDE_DIRS})
endif()
if(JPEG_FOUND)
  target_include_directories(${PLUGIN_NAME} PUBLIC ${JPEG_INCLUDE_DIRS})
endif()

# Link libraries
target_link_libraries(${PLUGIN_NAME} PUBLIC
  flutter
  ${CURL_LIBRARIES}
  ${SQLITE3_LIBRARIES}
  pthread
)

if(PNG_FOUND)
  target_link_libraries(${PLUGIN_NAME} PUBLIC ${PNG_LIBRARIES})
  target_compile_options(${PLUGIN_NAME} PUBLIC ${PNG_CFLAGS_OTHER})
endif()
if(JPEG_FOUND)
  target_link_libraries(${PLUGIN_NAME} PUBLIC ${JPEG_LIBRARIES})
  target_compile_options(${PLUGIN_NAME} PUBLIC ${JPEG_CFLAGS_OTHER})
endif()

target_compile_options(${PLUGIN_NAME} PUBLIC
  ${CURL_CFLAGS_OTHER}
  ${SQLITE3_CFLAGS_OTHER}
)

# List of absolute paths to libraries that should be bundled with the plugin.
# This list could contain prebuilt libraries, or library helper functions
# should be added here.
list(APPEND PLUGIN_BUNDLED_LIBRARIES
)

# The name of the Flutter app that should be used when running this plugin via
# `flutter run`. This is optional and is provided for convenience in testing.
# When this is set, `flutter run` will forward all arguments directly to the
# specified app.
list(APPEND PLUGIN_DISPLAY_NAME "PostHog Flutter Plugin")
