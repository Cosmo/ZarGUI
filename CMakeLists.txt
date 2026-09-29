cmake_minimum_required(VERSION 3.20)
project(ZarGUI LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(FetchContent)
# Keep every download inside the project's build directory.
set(FETCHCONTENT_BASE_DIR "${CMAKE_BINARY_DIR}/_deps")

FetchContent_Declare(zstd
  URL https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz
  URL_HASH SHA256=eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SOURCE_SUBDIR build/cmake)
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZSTD_LEGACY_SUPPORT OFF CACHE BOOL "" FORCE)
if(MSVC)
  set(ZSTD_USE_STATIC_RUNTIME ON CACHE BOOL "" FORCE)
endif()

# ZArchive's own CMake wants a system zstd and builds a CLI we don't need,
# so fetch only its sources and build them here.
FetchContent_Declare(zarchive_src
  GIT_REPOSITORY https://github.com/Exzap/ZArchive.git
  GIT_TAG 965b66c8d67b6b7e30fd63b3b75aa91a99ff303b
  SOURCE_SUBDIR does-not-exist)

FetchContent_MakeAvailable(zstd zarchive_src)

# Applied on every configure (the script is idempotent), so changes to it take effect.
set(ZARCHIVE_PATCH ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch-zarchive.cmake)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${ZARCHIVE_PATCH})
execute_process(COMMAND ${CMAKE_COMMAND} -DSRC=${zarchive_src_SOURCE_DIR} -P ${ZARCHIVE_PATCH}
                RESULT_VARIABLE patch_result)
if(NOT patch_result EQUAL 0)
  message(FATAL_ERROR "Patching ZArchive failed")
endif()

set(ZARPACK_SOURCES
  core/src/common.cpp
  core/src/pack.cpp
  core/src/archive.cpp
  core/src/extract.cpp
  ${zarchive_src_SOURCE_DIR}/src/zarchivewriter.cpp
  ${zarchive_src_SOURCE_DIR}/src/zarchivereader.cpp
  ${zarchive_src_SOURCE_DIR}/src/sha_256.c)

add_library(zarpack STATIC ${ZARPACK_SOURCES})
target_include_directories(zarpack
  PUBLIC core/include
  PRIVATE ${zarchive_src_SOURCE_DIR}/include ${zstd_SOURCE_DIR}/lib)
target_link_libraries(zarpack PRIVATE libzstd_static)

# Windows app: one self-contained ZarGUI.exe (Win32, core linked statically).
if(WIN32)
  add_executable(ZarGUI WIN32
    windows/src/main.cpp
    windows/src/common.cpp
    windows/src/window.cpp
    windows/src/settings.cpp
    windows/src/settings_dialog.cpp
    windows/src/job.cpp
    windows/src/archive.cpp
    windows/src/drag_drop.cpp
    windows/src/main_window.cpp
    windows/src/archive_window.cpp
    windows/ZarGUI.rc
    windows/ZarGUI.manifest)
  target_compile_definitions(ZarGUI PRIVATE UNICODE _UNICODE NOMINMAX)
  target_link_libraries(ZarGUI PRIVATE zarpack libzstd_static
    comctl32 shlwapi uxtheme ole32 shell32 uuid)
  if(MSVC)
    target_compile_options(ZarGUI PRIVATE /W4 /utf-8)
  endif()
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(zarpack_test core/tests/zarpack_test.cpp)
  target_include_directories(zarpack_test PRIVATE ${zarchive_src_SOURCE_DIR}/include)
  target_link_libraries(zarpack_test PRIVATE zarpack libzstd_static)
  add_test(NAME zarpack_test COMMAND zarpack_test)
endif()
