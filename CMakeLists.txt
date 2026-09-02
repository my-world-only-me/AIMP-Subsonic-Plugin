cmake_minimum_required(VERSION 3.21)

project(AimpSubsonicPlugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
  message(FATAL_ERROR "AIMP plugins can only be built for Windows.")
endif()

set(AIMP_SDK_DIR "" CACHE PATH "Path to the extracted AIMP SDK root, for example C:/SDK/AIMP_v5.40")
set(AIMP_SDK_AUTO_DOWNLOAD ON CACHE BOOL "Download the official AIMP SDK v5.40 if it is not found locally")

set(_aimp_sdk_candidates
  "${AIMP_SDK_DIR}"
  "$ENV{AIMP_SDK_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/aimp_sdk"
  "${CMAKE_BINARY_DIR}/_deps/aimp_sdk_v540"
  "$ENV{TEMP}/aimp_sdk_v540"
  "$ENV{TMP}/aimp_sdk_v540"
)

foreach(_candidate IN LISTS _aimp_sdk_candidates)
  if(_candidate AND EXISTS "${_candidate}/Sources/Cpp/apiPlugin.h")
    set(AIMP_SDK_DIR "${_candidate}" CACHE PATH "Path to the extracted AIMP SDK root" FORCE)
    break()
  endif()
endforeach()

if((NOT AIMP_SDK_DIR OR NOT EXISTS "${AIMP_SDK_DIR}/Sources/Cpp/apiPlugin.h") AND AIMP_SDK_AUTO_DOWNLOAD)
  set(_aimp_sdk_zip "${CMAKE_BINARY_DIR}/_deps/aimp_sdk_v540.zip")
  set(_aimp_sdk_extract "${CMAKE_BINARY_DIR}/_deps/aimp_sdk_v540")
  file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/_deps")
  message(STATUS "Downloading AIMP SDK v5.40...")
  # aimp.ru republishes the SDK zip in place from time to time; when the hash
  # check fails with a consistent "actual" value across machines, verify the
  # archive still contains Sources/Cpp/apiPlugin.h and update the pin.
  file(DOWNLOAD
    "https://aimp.ru/?do=download.file&id=15"
    "${_aimp_sdk_zip}"
    EXPECTED_HASH SHA256=7f880c935fccd486b8fe12fa452617e630411a5adfdcac7b07d9a9bdaa5a2b63
    SHOW_PROGRESS
  )
  file(ARCHIVE_EXTRACT INPUT "${_aimp_sdk_zip}" DESTINATION "${_aimp_sdk_extract}")
  if(EXISTS "${_aimp_sdk_extract}/Sources/Cpp/apiPlugin.h")
    set(AIMP_SDK_DIR "${_aimp_sdk_extract}" CACHE PATH "Path to the extracted AIMP SDK root" FORCE)
  endif()
endif()

if(NOT AIMP_SDK_DIR OR NOT EXISTS "${AIMP_SDK_DIR}/Sources/Cpp/apiPlugin.h")
  message(FATAL_ERROR "AIMP SDK was not found. Set AIMP_SDK_DIR to the extracted AIMP SDK root or enable AIMP_SDK_AUTO_DOWNLOAD.")
endif()

set(AIMP_SDK_CPP_DIR "${AIMP_SDK_DIR}/Sources/Cpp")
message(STATUS "Using AIMP SDK: ${AIMP_SDK_DIR}")

add_library(aimp_subsonic SHARED
  src/AimpPlugin.cpp
  src/AimpString.cpp
  src/ComBase.cpp
  src/Config.cpp
  src/Diagnostics.cpp
  src/DllMain.cpp
  src/PlaybackTracker.cpp
  src/PlaylistBridge.cpp
  src/SimpleJson.cpp
  src/CoverArtCache.cpp
  src/SubsonicAlbumArtProvider.cpp
  src/SubsonicClient.cpp
  src/SubsonicFileInfoProvider.cpp
  src/SubsonicLibraryNodes.cpp
  src/SubsonicMusicLibrary.cpp
  src/SubsonicOptionsFrame.cpp
  src/SubsonicRepository.cpp
  src/Url.cpp
  src/i18n.cpp
)

target_include_directories(aimp_subsonic PRIVATE
  "${AIMP_SDK_CPP_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_definitions(aimp_subsonic PRIVATE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  UNICODE
  _UNICODE
)

target_link_libraries(aimp_subsonic PRIVATE
  winhttp
  advapi32
  crypt32
  shell32
  ole32
)

set_target_properties(aimp_subsonic PROPERTIES
  OUTPUT_NAME "aimp_subsonic"
)

if(MSVC)
  target_compile_options(aimp_subsonic PRIVATE /W4 /permissive- /EHsc /utf-8)
  target_compile_options(aimp_subsonic PRIVATE /wd4100)
endif()

add_executable(aimp_subsonic_node_tests EXCLUDE_FROM_ALL
  tests/SubsonicLibraryNodesTests.cpp
  src/SubsonicLibraryNodes.cpp
)

target_include_directories(aimp_subsonic_node_tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

if(MSVC)
  target_compile_options(aimp_subsonic_node_tests PRIVATE /W4 /permissive- /EHsc /utf-8)
endif()

add_executable(aimp_subsonic_json_tests EXCLUDE_FROM_ALL
  tests/SimpleJsonNavidromeTests.cpp
  src/SimpleJson.cpp
  src/Url.cpp
)

target_include_directories(aimp_subsonic_json_tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(aimp_subsonic_json_tests PRIVATE
  advapi32
)

if(MSVC)
  target_compile_options(aimp_subsonic_json_tests PRIVATE /W4 /permissive- /EHsc)
endif()

add_executable(aimp_subsonic_security_tests EXCLUDE_FROM_ALL
  tests/SecurityTests.cpp
  src/AimpString.cpp
  src/ComBase.cpp
  src/Config.cpp
  src/Diagnostics.cpp
  src/SimpleJson.cpp
  src/Url.cpp
)

target_include_directories(aimp_subsonic_security_tests PRIVATE
  "${AIMP_SDK_CPP_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(aimp_subsonic_security_tests PRIVATE
  advapi32
  crypt32
  user32
)

if(MSVC)
  target_compile_options(aimp_subsonic_security_tests PRIVATE /W4 /permissive- /EHsc /utf-8)
endif()
