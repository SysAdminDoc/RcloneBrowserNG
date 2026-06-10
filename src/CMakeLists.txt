cmake_minimum_required(VERSION 3.16)

if (NOT ${CMAKE_VERSION} VERSION_LESS "3.0.0")
	cmake_policy(SET CMP0028 NEW)
endif()
if (NOT ${CMAKE_VERSION} VERSION_LESS "2.8.11")
	cmake_policy(SET CMP0020 NEW)
endif()

if(WIN32)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4 /WX /wd4100 /wd4189 /wd4996")
else()
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  add_definitions("-pedantic -Wall -Wextra -Werror -Wno-deprecated-declarations")
endif()

if (APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0)
endif()

project(rclone-browser)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Widgets Network)

if (WIN32 AND QT_VERSION_MAJOR EQUAL 5)
  find_package(Qt5WinExtras REQUIRED)
elseif(APPLE AND QT_VERSION_MAJOR EQUAL 5)
  find_package(Qt5MacExtras REQUIRED)
endif()

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
  export_dialog.cpp
  progress_dialog.cpp
  job_widget.cpp
  mount_widget.cpp
  stream_widget.cpp
  preferences_dialog.cpp
  icon_cache.cpp
  item_model.cpp
  utils.cpp
  job_options.cpp
  list_of_job_options.cpp
)

set(OTHER
  pch.h
  utils.h
  job_options.h
  main_window.h
  remote_widget.h
  transfer_dialog.h
  export_dialog.h
  progress_dialog.h
  job_widget.h
  mount_widget.h
  stream_widget.h
  preferences_dialog.h
  icon_cache.h
  list_of_job_options.h
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
  if(QT_VERSION_MAJOR EQUAL 5)
    target_link_libraries(RcloneBrowser Qt5::WinExtras)
  endif()
elseif(APPLE)
  find_library(COCOA_LIB Cocoa REQUIRED)
  set_source_files_properties(icon.icns PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
  add_executable(rclone-browser MACOSX_BUNDLE ${SOURCE} ${OTHER} ${UI} ${QRC} icon.icns)
  target_link_libraries(rclone-browser Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network ${COCOA_LIB})
  if(QT_VERSION_MAJOR EQUAL 5)
    target_link_libraries(rclone-browser Qt5::MacExtras)
  endif()
  set_target_properties(rclone-browser PROPERTIES MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist")
else()
  add_executable(rclone-browser ${SOURCE} ${OTHER} ${UI} ${QRC})
  target_link_libraries(rclone-browser Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network)

  install(TARGETS rclone-browser RUNTIME DESTINATION bin)
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../assets/rclone-browser.svg" DESTINATION "share/icons/hicolor/scalable/apps" RENAME "rclone-browser.svg")
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../assets/rclone-browser-32x32.png" DESTINATION "share/icons/hicolor/32x32/apps" RENAME "rclone-browser.png")
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../assets/rclone-browser-64x64.png" DESTINATION "share/icons/hicolor/64x64/apps" RENAME "rclone-browser.png")
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../assets/rclone-browser-128x128.png" DESTINATION "share/icons/hicolor/128x128/apps" RENAME "rclone-browser.png")
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../assets/rclone-browser-256x256.png" DESTINATION "share/icons/hicolor/256x256/apps" RENAME "rclone-browser.png")
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../assets/rclone-browser-512x512.png" DESTINATION "share/icons/hicolor/512x512/apps" RENAME "rclone-browser.png")
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../assets/rclone-browser.desktop" DESTINATION "share/applications")
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../assets/rclone-browser.appdata.xml" DESTINATION "share/metainfo")
endif()
