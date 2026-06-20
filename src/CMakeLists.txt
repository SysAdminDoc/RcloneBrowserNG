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
  add_compile_options(-fstack-protector-strong -fcf-protection=full)
  add_definitions(-D_FORTIFY_SOURCE=3)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
  add_link_options(-Wl,-z,relro,-z,now)
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
  rclone_rc_engine.cpp
  rclone_capabilities.cpp
  rc_job_widget.cpp
  remote_path.cpp
  vfs_upload_state.cpp
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
  job_options_store.cpp
  list_of_job_options.cpp
  schedule_manager.cpp
  cross_remote_search.cpp
)

set(OTHER
  pch.h
  utils.h
  job_options.h
  job_options_store.h
  main_window.h
  remote_widget.h
  transfer_dialog.h
  interface_polish.h
  export_dialog.h
  export_list_writer.h
  folder_compare.h
  remote_provider.h
  mount_backend.h
  rclone_rc_engine.h
  rclone_capabilities.h
  rc_job_widget.h
  remote_path.h
  vfs_upload_state.h
  progress_dialog.h
  job_widget.h
  mount_widget.h
  stream_widget.h
  preferences_dialog.h
  icon_cache.h
  job_history.h
  list_of_job_options.h
  schedule_manager.h
  cross_remote_search.h
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
  target_link_libraries(RcloneBrowser version advapi32)
  add_executable(RcloneBrowserPassword rclone_password_helper.cpp utils.cpp)
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
