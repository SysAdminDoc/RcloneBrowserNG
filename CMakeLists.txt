cmake_minimum_required(VERSION 3.16)

project(rclone-browser)
include(CTest)

if(WIN32)
  cmake_policy(SET CMP0020 NEW)
endif()

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Widgets Network)

if(WIN32 AND QT_VERSION_MAJOR EQUAL 5)
  find_package(Qt5WinExtras REQUIRED)
elseif(APPLE AND QT_VERSION_MAJOR EQUAL 5)
  find_package(Qt5MacExtras REQUIRED)
  find_library(COCOA_LIB Cocoa REQUIRED)
endif()

if(WIN32)
  set_property(GLOBAL PROPERTY USE_FOLDERS OFF)

  add_definitions("-D_UNICODE -DUNICODE -D_SCL_SECURE_NO_DEPRECATE -D_CRT_SECURE_NO_DEPRECATE")

  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS} /GF /Gy /GS- /GR- /GL")

  set(CMAKE_EXE_LINKER_FLAGS "/INCREMENTAL:NO")
  set(CMAKE_EXE_LINKER_FLAGS_DEBUG "/DEBUG")
  set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/LTCG /OPT:ICF /OPT:REF")
endif()

file(READ "VERSION" RCLONE_BROWSER_VERSION)
string(STRIP "${RCLONE_BROWSER_VERSION}" RCLONE_BROWSER_VERSION)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${rclone-browser_BINARY_DIR}/build")

add_subdirectory(src)

if(BUILD_TESTING)
  add_executable(export_list_writer_test
    tests/export_list_writer_test.cpp
    src/export_list_writer.cpp
  )
  target_include_directories(export_list_writer_test PRIVATE src)
  target_link_libraries(export_list_writer_test Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network)
  add_test(NAME export_list_writer_test COMMAND export_list_writer_test)

  add_executable(vfs_upload_state_test
    tests/vfs_upload_state_test.cpp
    src/vfs_upload_state.cpp
  )
  target_include_directories(vfs_upload_state_test PRIVATE src)
  target_link_libraries(vfs_upload_state_test Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network)
  add_test(NAME vfs_upload_state_test COMMAND vfs_upload_state_test)

  add_executable(remote_path_test
    tests/remote_path_test.cpp
    src/remote_path.cpp
  )
  target_include_directories(remote_path_test PRIVATE src)
  target_link_libraries(remote_path_test Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network)
  add_test(NAME remote_path_test COMMAND remote_path_test)

  add_executable(remote_provider_test
    tests/remote_provider_test.cpp
    src/remote_provider.cpp
  )
  target_include_directories(remote_provider_test PRIVATE src)
  target_link_libraries(remote_provider_test Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network)
  add_test(NAME remote_provider_test COMMAND remote_provider_test)

  add_executable(job_options_store_test
    tests/job_options_store_test.cpp
    src/job_options_store.cpp
    src/job_options.cpp
    src/utils.cpp
  )
  target_include_directories(job_options_store_test PRIVATE src)
  target_link_libraries(job_options_store_test Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network)
  if(WIN32)
    target_link_libraries(job_options_store_test advapi32)
  endif()
  add_test(NAME job_options_store_test COMMAND job_options_store_test)
endif()
