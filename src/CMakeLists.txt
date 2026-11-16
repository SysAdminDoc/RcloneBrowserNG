cmake_minimum_required(VERSION 3.16)

if (NOT ${CMAKE_VERSION} VERSION_LESS "3.0.0")
	cmake_policy(SET CMP0028 NEW)
endif()
if (NOT ${CMAKE_VERSION} VERSION_LESS "2.8.11")
	cmake_policy(SET CMP0020 NEW)
endif()

if(WIN32)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4 /WX /wd4100 /wd4189 /wd4996")
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /GS")
else()
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  add_definitions("-pedantic -Wall -Wextra -Werror -Wno-deprecated-declarations")
  add_compile_options(-fstack-protector-strong)
  # -fcf-protection is x86 only. Apple Silicon and ARM Linux compilers refuse
  # it outright, which stopped the build there.
  include(CheckCXXCompilerFlag)
  check_cxx_compiler_flag(-fcf-protection=full RCLONE_BROWSER_HAVE_CF_PROTECTION)
  if(RCLONE_BROWSER_HAVE_CF_PROTECTION)
    add_compile_options(-fcf-protection=full)
  endif()
  add_definitions(-D_FORTIFY_SOURCE=3)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
  # RELRO is an ELF feature. Apple's linker has no -z options.
  if(NOT APPLE)
    add_link_options(-Wl,-z,relro,-z,now)
  endif()
endif()

if (APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0)
endif()

project(rclone-browser)

find_package(QT NAMES Qt6 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Widgets Network)
find_package(Qt${QT_VERSION_MAJOR} OPTIONAL_COMPONENTS LinguistTools)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(UI
  main_window.ui
  remote_widget.ui
  transfer_dialog.ui
  export_dialog.ui
  progress_dialog.ui
  job_widget.ui
  mount_widget.ui
  stream_widget.ui
  preferences_dialog.ui
)

set(SOURCE
  pch.cpp
  main.cpp
  main_window.cpp
  remote_widget.cpp
  transfer_dialog.cpp
  interface_polish.cpp
  export_dialog.cpp
  export_list_writer.cpp
  folder_compare.cpp
  remote_provider.cpp
  mount_backend.cpp
  mount_health.cpp
  mount_options.cpp
  rclone_rc_engine.cpp
  rclone_capabilities.cpp
  rc_delete_batch.cpp
  rc_job_result.cpp
  rc_job_widget.cpp
  adaptive_bandwidth.cpp
  adaptive_bandwidth_driver.cpp
  run_conditions.cpp
  remote_list_parser.cpp
  remote_path.cpp
  app_log.cpp
  filter_pattern.cpp
  bwlimit_timetable.cpp
  job_stats.cpp
  rc_sync_request.cpp
  lsjson_parser.cpp
  rclone_exit_code.cpp
  rclone_installer.cpp
  big_uint.cpp
  pgp_clearsign.cpp
  task_notify.cpp
  rclone_security_floor.cpp
  winfsp_floor.cpp
  selection_arguments.cpp
  sync_preview.cpp
  dry_run_check.cpp
  schedule_dialog.cpp
  vfs_upload_state.cpp
  vss_snapshot.cpp
  remote_daemon.cpp
  rc_certificate.cpp
  progress_dialog.cpp
  job_widget.cpp
  mount_widget.cpp
  stream_widget.cpp
  preferences_dialog.cpp
  icon_cache.cpp
  job_history.cpp
  item_model.cpp
  utils.cpp
  job_options.cpp
  rclone_number.cpp
  job_options_store.cpp
  identity_migration.cpp
  list_of_job_options.cpp
  schedule_manager.cpp
  staged_transfer.cpp
  cross_remote_search.cpp
  combine_remote.cpp
  combine_remote_dialog.cpp
)

set(OTHER
  pch.h
  utils.h
  job_options.h
  job_options_store.h
  app_identity.h
  identity_migration.h
  main_window.h
  remote_widget.h
  transfer_dialog.h
  interface_polish.h
  export_dialog.h
  export_list_writer.h
  folder_compare.h
  remote_provider.h
  mount_backend.h
  mount_health.h
  mount_options.h
  rclone_rc_engine.h
  rclone_capabilities.h
  rc_delete_batch.h
  rc_job_result.h
  rc_job_widget.h
  adaptive_bandwidth.h
  adaptive_bandwidth_driver.h
  run_conditions.h
  remote_path.h
  schedule_dialog.h
  vfs_upload_state.h
  vss_snapshot.h
  rclone_installer.h
  big_uint.h
  pgp_clearsign.h
  winfsp_floor.h
  remote_daemon.h
  rc_certificate.h
  progress_dialog.h
  job_widget.h
  mount_widget.h
  stream_widget.h
  preferences_dialog.h
  icon_cache.h
  job_history.h
  list_of_job_options.h
  schedule_manager.h
  staged_transfer.h
  cross_remote_search.h
  combine_remote.h
  combine_remote_dialog.h
  item_model.h
)

if(WIN32)
  set(OTHER ${OTHER} resources.rc)
elseif(APPLE)
  set(OTHER ${OTHER} osx_helper.h)
  set(SOURCE ${SOURCE} osx_helper.mm)
endif()

set(QRC resources.qrc)

add_definitions(-DRCLONE_BROWSER_VERSION="${RCLONE_BROWSER_VERSION}")

if(WIN32)
  add_executable(RcloneBrowser WIN32 ${SOURCE} ${OTHER} ${UI} ${QRC})
  target_link_libraries(RcloneBrowser Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network)
  # iphlpapi: network interface counters for adaptive bandwidth.
  target_link_libraries(RcloneBrowser version advapi32 iphlpapi wbemuuid ole32 oleaut32)
  add_executable(RcloneBrowserPassword rclone_password_helper.cpp utils.cpp
    identity_migration.cpp job_options_store.cpp job_options.cpp
    bwlimit_timetable.cpp rclone_number.cpp)
  target_link_libraries(RcloneBrowserPassword Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network advapi32)
elseif(APPLE)
  find_library(COCOA_LIB Cocoa REQUIRED)
  set_source_files_properties(icon.icns PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
  add_executable(rclone-browser MACOSX_BUNDLE ${SOURCE} ${OTHER} ${UI} ${QRC} icon.icns)
  target_link_libraries(rclone-browser Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network ${COCOA_LIB})
  set_target_properties(rclone-browser PROPERTIES MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist")
else()
  add_executable(rclone-browser ${SOURCE} ${OTHER} ${UI} ${QRC})
  target_link_libraries(rclone-browser Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network)

  install(TARGETS rclone-browser RUNTIME DESTINATION bin)
  set(APPID "io.github.sysadmindoc.rclonebrowserng")
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../assets/rclone-browser.svg" DESTINATION "share/icons/hicolor/scalable/apps" RENAME "${APPID}.svg")
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../assets/rclone-browser-32x32.png" DESTINATION "share/icons/hicolor/32x32/apps" RENAME "${APPID}.png")
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../assets/rclone-browser-64x64.png" DESTINATION "share/icons/hicolor/64x64/apps" RENAME "${APPID}.png")
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../assets/rclone-browser-128x128.png" DESTINATION "share/icons/hicolor/128x128/apps" RENAME "${APPID}.png")
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../assets/rclone-browser-256x256.png" DESTINATION "share/icons/hicolor/256x256/apps" RENAME "${APPID}.png")
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../assets/rclone-browser-512x512.png" DESTINATION "share/icons/hicolor/512x512/apps" RENAME "${APPID}.png")
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../assets/${APPID}.desktop" DESTINATION "share/applications")
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../assets/${APPID}.metainfo.xml" DESTINATION "share/metainfo")
endif()

# cmake -G Ninja -DRCLONE_BROWSER_CLANG_TIDY=ON ... lints every app source as
# it compiles. Needs a Makefile or Ninja generator.
if(RCLONE_BROWSER_CLANG_TIDY)
  find_program(RCLONE_BROWSER_CLANG_TIDY_EXE NAMES clang-tidy)
  if(NOT RCLONE_BROWSER_CLANG_TIDY_EXE)
    message(FATAL_ERROR "RCLONE_BROWSER_CLANG_TIDY is ON but clang-tidy wasn't found. "
      "Put it on PATH or set RCLONE_BROWSER_CLANG_TIDY_EXE.")
  endif()
  # Findings fail the build.
  set(_rb_tidy "${RCLONE_BROWSER_CLANG_TIDY_EXE}" "--warnings-as-errors=*")
  if(MSVC)
    # clang-tidy parses the compile line CMake hands it in gcc mode before
    # switching to clang-cl, and drops every /option as a file name on the
    # way. Without /EHsc, clang-cl rejects every try and throw.
    list(APPEND _rb_tidy "--extra-arg=/EHsc")
  endif()
  if(WIN32)
    set(_rb_tidy_targets RcloneBrowser RcloneBrowserPassword)
  else()
    set(_rb_tidy_targets rclone-browser)
  endif()
  set_target_properties(${_rb_tidy_targets} PROPERTIES CXX_CLANG_TIDY "${_rb_tidy}")
endif()
