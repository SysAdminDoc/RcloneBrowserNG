cmake_minimum_required(VERSION 3.16)

project(rclone-browser)
include(CTest)

if(WIN32)
  cmake_policy(SET CMP0020 NEW)
endif()

# Source builds floor at 6.4, which is what the README badge claims and what
# the oldest distro the project supports ships (Ubuntu 24.04 LTS: 6.4.2).
# Release packaging is stricter and separately enforced by
# scripts/validate_qt_version.py; see SECURITY.md.
set(RCLONE_BROWSER_QT_MINIMUM 6.4)
find_package(QT ${RCLONE_BROWSER_QT_MINIMUM} NAMES Qt6 REQUIRED COMPONENTS Core Widgets)
find_package(Qt${QT_VERSION_MAJOR} ${RCLONE_BROWSER_QT_MINIMUM} REQUIRED COMPONENTS Core Widgets Network Test)
if(Qt${QT_VERSION_MAJOR}_VERSION VERSION_LESS RCLONE_BROWSER_QT_MINIMUM)
  message(FATAL_ERROR
    "Qt ${Qt${QT_VERSION_MAJOR}_VERSION} is too old. Rclone Browser NG needs "
    "Qt ${RCLONE_BROWSER_QT_MINIMUM} or newer; point CMAKE_PREFIX_PATH at a "
    "newer Qt.")
endif()

if(WIN32)
  set_property(GLOBAL PROPERTY USE_FOLDERS OFF)

  add_definitions("-D_UNICODE -DUNICODE -D_SCL_SECURE_NO_DEPRECATE -D_CRT_SECURE_NO_DEPRECATE")

  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS} /GF /Gy /GS /GR- /GL /guard:cf")

  set(CMAKE_EXE_LINKER_FLAGS "/INCREMENTAL:NO /NXCOMPAT /DYNAMICBASE")
  set(CMAKE_EXE_LINKER_FLAGS_DEBUG "/DEBUG")
  set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/LTCG /OPT:ICF /OPT:REF /CETCOMPAT /guard:cf")
endif()

file(READ "VERSION" RCLONE_BROWSER_VERSION)
string(STRIP "${RCLONE_BROWSER_VERSION}" RCLONE_BROWSER_VERSION)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${rclone-browser_BINARY_DIR}/build")

add_subdirectory(src)

if(BUILD_TESTING)
  set(CMAKE_AUTOMOC ON)

  add_executable(export_list_writer_test
    tests/export_list_writer_test.cpp
    src/export_list_writer.cpp
  )
  target_include_directories(export_list_writer_test PRIVATE src)
  target_link_libraries(export_list_writer_test Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Test)
  add_test(NAME export_list_writer_test COMMAND export_list_writer_test)

  add_executable(vfs_upload_state_test
    tests/vfs_upload_state_test.cpp
    src/vfs_upload_state.cpp
  )
  target_include_directories(vfs_upload_state_test PRIVATE src)
  target_link_libraries(vfs_upload_state_test Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Test)
  add_test(NAME vfs_upload_state_test COMMAND vfs_upload_state_test)

  add_executable(remote_path_test
    tests/remote_path_test.cpp
    src/remote_path.cpp
  )
  target_include_directories(remote_path_test PRIVATE src)
  target_link_libraries(remote_path_test Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Test)
  add_test(NAME remote_path_test COMMAND remote_path_test)

  add_executable(app_log_test
    tests/app_log_test.cpp
    src/app_log.cpp
    src/rclone_capabilities.cpp
    src/utils.cpp
  )
  target_include_directories(app_log_test PRIVATE src)
  target_compile_definitions(app_log_test PRIVATE RCLONE_BROWSER_VERSION="${RCLONE_BROWSER_VERSION}")
  target_link_libraries(app_log_test Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Test)
  if(WIN32)
    target_link_libraries(app_log_test advapi32)
  endif()
  add_test(NAME app_log_test COMMAND app_log_test)
  set_tests_properties(app_log_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  add_executable(rclone_security_floor_test
    tests/rclone_security_floor_test.cpp
    src/rclone_security_floor.cpp
    src/utils.cpp
  )
  target_include_directories(rclone_security_floor_test PRIVATE src)
  target_link_libraries(rclone_security_floor_test Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Test)
  if(WIN32)
    target_link_libraries(rclone_security_floor_test advapi32)
  endif()
  add_test(NAME rclone_security_floor_test COMMAND rclone_security_floor_test)

  add_executable(selection_arguments_test
    tests/selection_arguments_test.cpp
    src/selection_arguments.cpp
  )
  target_include_directories(selection_arguments_test PRIVATE src)
  target_link_libraries(selection_arguments_test Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Test)
  add_test(NAME selection_arguments_test COMMAND selection_arguments_test)

  add_executable(exit_code_test
    tests/exit_code_test.cpp
    src/rclone_exit_code.cpp
  )
  target_include_directories(exit_code_test PRIVATE src)
  target_link_libraries(exit_code_test Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Test)
  add_test(NAME exit_code_test COMMAND exit_code_test)

  add_executable(filter_pattern_test
    tests/filter_pattern_test.cpp
    src/filter_pattern.cpp
  )
  target_include_directories(filter_pattern_test PRIVATE src)
  target_link_libraries(filter_pattern_test Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Test)
  add_test(NAME filter_pattern_test COMMAND filter_pattern_test)

  add_executable(sync_preview_test
    tests/sync_preview_test.cpp
    src/sync_preview.cpp
  )
  target_include_directories(sync_preview_test PRIVATE src)
  target_link_libraries(sync_preview_test Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Test)
  add_test(NAME sync_preview_test COMMAND sync_preview_test)

  add_executable(remote_list_parser_test
    tests/remote_list_parser_test.cpp
    src/remote_list_parser.cpp
  )
  target_include_directories(remote_list_parser_test PRIVATE src)
  target_link_libraries(remote_list_parser_test Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Test)
  add_test(NAME remote_list_parser_test COMMAND remote_list_parser_test)

  add_executable(remote_provider_test
    tests/remote_provider_test.cpp
    src/remote_provider.cpp
  )
  target_include_directories(remote_provider_test PRIVATE src)
  target_link_libraries(remote_provider_test Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Test)
  add_test(NAME remote_provider_test COMMAND remote_provider_test)

  add_executable(job_options_store_test
    tests/job_options_store_test.cpp
    src/job_options_store.cpp
    src/job_options.cpp
    src/utils.cpp
  )
  target_include_directories(job_options_store_test PRIVATE src)
  target_link_libraries(job_options_store_test Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Test)
  if(WIN32)
    target_link_libraries(job_options_store_test advapi32)
  endif()
  add_test(NAME job_options_store_test COMMAND job_options_store_test)

  add_executable(job_history_test
    tests/job_history_test.cpp
    src/job_history.cpp
    src/rclone_capabilities.cpp
    src/utils.cpp
  )
  target_include_directories(job_history_test PRIVATE src)
  target_compile_definitions(job_history_test PRIVATE RCLONE_BROWSER_VERSION="${RCLONE_BROWSER_VERSION}")
  target_link_libraries(job_history_test Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Test)
  if(WIN32)
    target_link_libraries(job_history_test advapi32)
  endif()
  add_test(NAME job_history_test COMMAND job_history_test)

  add_executable(folder_compare_test
    tests/folder_compare_test.cpp
    src/folder_compare.cpp
    src/interface_polish.cpp
    src/utils.cpp
  )
  target_include_directories(folder_compare_test PRIVATE src)
  target_link_libraries(folder_compare_test Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Test)
  if(WIN32)
    target_link_libraries(folder_compare_test advapi32)
  endif()
  add_test(NAME folder_compare_test COMMAND folder_compare_test)

  add_executable(interface_polish_test
    tests/interface_polish_test.cpp
    src/interface_polish.cpp
  )
  target_include_directories(interface_polish_test PRIVATE src)
  target_link_libraries(interface_polish_test Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Test)
  add_test(NAME interface_polish_test COMMAND interface_polish_test)

  add_executable(mount_backend_test
    tests/mount_backend_test.cpp
    src/mount_backend.cpp
    src/mount_health.cpp
    src/mount_options.cpp
    src/utils.cpp
  )
  target_include_directories(mount_backend_test PRIVATE src)
  target_link_libraries(mount_backend_test Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Test)
  add_test(NAME mount_backend_test COMMAND mount_backend_test)

  # Links the shipped decoders. This target used to compile only its own copy
  # of both parsers, so a regression in src/ could not turn it red.
  add_executable(parsing_regression_test
    tests/parsing_regression_test.cpp
    tests/parsing_regression_test.h
    src/lsjson_parser.cpp
    src/job_stats.cpp
    src/remote_path.cpp
  )
  target_include_directories(parsing_regression_test PRIVATE src)
  target_link_libraries(parsing_regression_test Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Test)
  add_test(NAME parsing_regression_test COMMAND parsing_regression_test)

  # Stands in for the rclone binary: starts, ignores its arguments, sleeps
  # past the RC startup budget. A blocking implementation cannot pass.
  add_executable(sleepy_stub tests/sleepy_stub.cpp)
  set_target_properties(sleepy_stub PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

  add_executable(async_helpers_test
    tests/async_helpers_test.cpp
    src/rclone_rc_engine.cpp
    src/schedule_manager.cpp
    src/utils.cpp
  )
  add_dependencies(async_helpers_test sleepy_stub)
  target_compile_definitions(async_helpers_test PRIVATE
    SLEEPY_STUB_PATH="$<TARGET_FILE:sleepy_stub>")
  target_include_directories(async_helpers_test PRIVATE src)
  target_link_libraries(async_helpers_test Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Test)
  if(WIN32)
    target_link_libraries(async_helpers_test advapi32)
  endif()
  add_test(NAME async_helpers_test COMMAND async_helpers_test)
  set_tests_properties(async_helpers_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 300)

  add_executable(schedule_golden_test
    tests/schedule_golden_test.cpp
    src/schedule_manager.cpp
  )
  target_include_directories(schedule_golden_test PRIVATE src)
  target_link_libraries(schedule_golden_test Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Test)
  add_test(NAME schedule_golden_test COMMAND schedule_golden_test)

  add_executable(staged_transfer_test
    tests/staged_transfer_test.cpp
    src/staged_transfer.cpp
    src/job_options_store.cpp
    src/job_options.cpp
    src/utils.cpp
  )
  target_include_directories(staged_transfer_test PRIVATE src)
  target_link_libraries(staged_transfer_test Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Test)
  if(WIN32)
    target_link_libraries(staged_transfer_test advapi32)
  endif()
  add_test(NAME staged_transfer_test COMMAND staged_transfer_test)

  add_executable(backend_features_test
    tests/backend_features_test.cpp
    src/rclone_capabilities.cpp
    src/utils.cpp
  )
  target_include_directories(backend_features_test PRIVATE src)
  target_compile_definitions(backend_features_test PRIVATE RCLONE_BROWSER_VERSION="${RCLONE_BROWSER_VERSION}")
  target_link_libraries(backend_features_test Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Test)
  if(WIN32)
    target_link_libraries(backend_features_test advapi32)
  endif()
  add_test(NAME backend_features_test COMMAND backend_features_test)

  add_executable(dryrun_contract_test
    tests/dryrun_contract_test.cpp
    src/job_options.cpp
    src/job_options_store.cpp
    src/utils.cpp
  )
  target_include_directories(dryrun_contract_test PRIVATE src)
  target_link_libraries(dryrun_contract_test Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Test)
  if(WIN32)
    target_link_libraries(dryrun_contract_test advapi32)
  endif()
  add_test(NAME dryrun_contract_test COMMAND dryrun_contract_test)

  add_executable(rc_auth_regression_test
    tests/rc_auth_regression_test.cpp
  )
  target_include_directories(rc_auth_regression_test PRIVATE src)
  target_link_libraries(rc_auth_regression_test Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Test)
  add_test(NAME rc_auth_regression_test COMMAND rc_auth_regression_test)

  add_executable(preferences_layout_test
    tests/preferences_layout_test.cpp
    src/preferences_dialog.cpp
    src/app_log.cpp
    src/rclone_capabilities.cpp
    src/preferences_dialog.ui
    src/interface_polish.cpp
    src/mount_options.cpp
    src/utils.cpp
  )
  # AUTOUIC is enabled directory-scoped in src/; this target compiles the
  # dialog out-of-tree, so it needs uic for preferences_dialog.ui itself.
  set_target_properties(preferences_layout_test PROPERTIES AUTOUIC ON)
  target_include_directories(preferences_layout_test PRIVATE src tests)
  target_compile_definitions(preferences_layout_test PRIVATE RCLONE_BROWSER_VERSION="${RCLONE_BROWSER_VERSION}")
  target_link_libraries(preferences_layout_test Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Test)
  if(WIN32)
    target_link_libraries(preferences_layout_test advapi32)
  endif()
  add_test(NAME preferences_layout_test COMMAND preferences_layout_test)

  set(ACCESSIBILITY_SMOKE_SOURCES
    src/pch.cpp
    src/main_window.cpp
    src/remote_widget.cpp
    src/transfer_dialog.cpp
    src/interface_polish.cpp
    src/export_dialog.cpp
    src/export_list_writer.cpp
    src/folder_compare.cpp
    src/remote_provider.cpp
    src/mount_backend.cpp
    src/mount_health.cpp
    src/mount_options.cpp
    src/rclone_rc_engine.cpp
    src/app_log.cpp
    src/filter_pattern.cpp
    src/job_stats.cpp
    src/lsjson_parser.cpp
    src/rclone_capabilities.cpp
    src/rclone_exit_code.cpp
    src/rclone_security_floor.cpp
    src/rc_job_widget.cpp
    src/remote_list_parser.cpp
    src/remote_path.cpp
    src/selection_arguments.cpp
    src/sync_preview.cpp
    src/vfs_upload_state.cpp
    src/progress_dialog.cpp
    src/job_widget.cpp
    src/mount_widget.cpp
    src/stream_widget.cpp
    src/preferences_dialog.cpp
    src/icon_cache.cpp
    src/job_history.cpp
    src/item_model.cpp
    src/utils.cpp
    src/job_options.cpp
    src/job_options_store.cpp
    src/list_of_job_options.cpp
    src/schedule_manager.cpp
    src/schedule_dialog.cpp
    src/staged_transfer.cpp
    src/cross_remote_search.cpp
    src/main_window.ui
    src/remote_widget.ui
    src/transfer_dialog.ui
    src/export_dialog.ui
    src/progress_dialog.ui
    src/job_widget.ui
    src/mount_widget.ui
    src/stream_widget.ui
    src/preferences_dialog.ui
    src/resources.qrc
  )
  if(APPLE)
    list(APPEND ACCESSIBILITY_SMOKE_SOURCES src/osx_helper.mm)
  endif()
  add_executable(accessibility_smoke_test
    tests/accessibility_smoke_test.cpp
    ${ACCESSIBILITY_SMOKE_SOURCES}
  )
  set_target_properties(accessibility_smoke_test PROPERTIES AUTOUIC ON)
  target_include_directories(accessibility_smoke_test PRIVATE src)
  target_compile_definitions(accessibility_smoke_test PRIVATE RCLONE_BROWSER_VERSION="${RCLONE_BROWSER_VERSION}")
  target_link_libraries(accessibility_smoke_test Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Test)
  if(WIN32)
    target_link_libraries(accessibility_smoke_test version advapi32)
  elseif(APPLE)
    find_library(ACCESSIBILITY_COCOA_LIBRARY Cocoa REQUIRED)
    target_link_libraries(accessibility_smoke_test ${ACCESSIBILITY_COCOA_LIBRARY})
  endif()
  add_test(NAME accessibility_smoke_test COMMAND accessibility_smoke_test)
  set_tests_properties(accessibility_smoke_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  add_executable(layout_regression_test
    tests/layout_regression_test.cpp
    ${ACCESSIBILITY_SMOKE_SOURCES}
  )
  set_target_properties(layout_regression_test PROPERTIES AUTOUIC ON)
  target_include_directories(layout_regression_test PRIVATE src tests)
  target_compile_definitions(layout_regression_test PRIVATE RCLONE_BROWSER_VERSION="${RCLONE_BROWSER_VERSION}")
  target_link_libraries(layout_regression_test Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Test)
  if(WIN32)
    target_link_libraries(layout_regression_test version advapi32)
  elseif(APPLE)
    find_library(LAYOUT_REGRESSION_COCOA_LIBRARY Cocoa REQUIRED)
    target_link_libraries(layout_regression_test ${LAYOUT_REGRESSION_COCOA_LIBRARY})
  endif()
  add_test(NAME layout_regression_test COMMAND layout_regression_test)
  set_tests_properties(layout_regression_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  add_executable(remotes_list_test
    tests/remotes_list_test.cpp
    ${ACCESSIBILITY_SMOKE_SOURCES}
  )
  set_target_properties(remotes_list_test PROPERTIES AUTOUIC ON)
  target_include_directories(remotes_list_test PRIVATE src tests)
  target_compile_definitions(remotes_list_test PRIVATE RCLONE_BROWSER_VERSION="${RCLONE_BROWSER_VERSION}")
  target_link_libraries(remotes_list_test Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Test)
  if(WIN32)
    target_link_libraries(remotes_list_test version advapi32)
  elseif(APPLE)
    find_library(REMOTES_LIST_COCOA_LIBRARY Cocoa REQUIRED)
    target_link_libraries(remotes_list_test ${REMOTES_LIST_COCOA_LIBRARY})
  endif()
  add_test(NAME remotes_list_test COMMAND remotes_list_test)
  set_tests_properties(remotes_list_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 600)
endif()
