add_executable(nff_core_tests
    core/CoreTests.cpp
)

target_link_libraries(nff_core_tests PRIVATE nff_core)
target_compile_features(nff_core_tests PRIVATE cxx_std_23)

if(MSVC)
    target_compile_options(nff_core_tests PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(nff_core_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

add_test(NAME nff_core_tests COMMAND nff_core_tests)

add_executable(nff_workspace_tests
    workspace/WorkspaceTests.cpp
)

target_link_libraries(nff_workspace_tests PRIVATE nff_core)
target_compile_features(nff_workspace_tests PRIVATE cxx_std_23)

if(MSVC)
    target_compile_options(nff_workspace_tests PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(nff_workspace_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

add_test(NAME nff_workspace_tests COMMAND nff_workspace_tests)

add_executable(nff_recovery_tests
    recovery/RecoveryTests.cpp
)

target_link_libraries(nff_recovery_tests PRIVATE nff_core)
target_compile_features(nff_recovery_tests PRIVATE cxx_std_23)

if(MSVC)
    target_compile_options(nff_recovery_tests PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(nff_recovery_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

add_test(NAME nff_recovery_tests COMMAND nff_recovery_tests)

add_executable(nff_search_tests
    search/SearchTests.cpp
)

target_link_libraries(nff_search_tests PRIVATE nff_core)
target_compile_features(nff_search_tests PRIVATE cxx_std_23)

if(MSVC)
    target_compile_options(nff_search_tests PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(nff_search_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

add_test(NAME nff_search_tests COMMAND nff_search_tests)

add_executable(nff_viewer_tests
    viewer/ViewerTests.cpp
)

target_link_libraries(nff_viewer_tests PRIVATE nff_core)
target_compile_features(nff_viewer_tests PRIVATE cxx_std_23)

if(MSVC)
    target_compile_options(nff_viewer_tests PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(nff_viewer_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

add_test(NAME nff_viewer_tests COMMAND nff_viewer_tests)

add_executable(nff_format_tests
    formats/FormatTests.cpp
)

target_link_libraries(nff_format_tests PRIVATE nff_core)
target_compile_features(nff_format_tests PRIVATE cxx_std_23)

if(MSVC)
    target_compile_options(nff_format_tests PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(nff_format_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

add_test(NAME nff_format_tests COMMAND nff_format_tests)

add_executable(nff_transform_tests
    formats/TransformTests.cpp
)

target_link_libraries(nff_transform_tests PRIVATE nff_core)
target_compile_features(nff_transform_tests PRIVATE cxx_std_23)

if(MSVC)
    target_compile_options(nff_transform_tests PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(nff_transform_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

add_test(NAME nff_transform_tests COMMAND nff_transform_tests)

add_executable(nff_app_tests
    app/LaunchTests.cpp
)

target_link_libraries(nff_app_tests PRIVATE nff_core)
target_compile_features(nff_app_tests PRIVATE cxx_std_23)

if(MSVC)
    target_compile_options(nff_app_tests PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(nff_app_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

add_test(NAME nff_app_tests COMMAND nff_app_tests)

add_executable(nff_persistence_tests
    persistence/PersistenceTests.cpp
)

target_link_libraries(nff_persistence_tests PRIVATE nff_core)
target_compile_features(nff_persistence_tests PRIVATE cxx_std_23)

if(MSVC)
    target_compile_options(nff_persistence_tests PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(nff_persistence_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

add_test(NAME nff_persistence_tests COMMAND nff_persistence_tests)

add_executable(nff_utility_tests
    utilities/UtilityTests.cpp
)

target_link_libraries(nff_utility_tests PRIVATE nff_core)
target_compile_features(nff_utility_tests PRIVATE cxx_std_23)

if(MSVC)
    target_compile_options(nff_utility_tests PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(nff_utility_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

add_test(NAME nff_utility_tests COMMAND nff_utility_tests)

add_executable(nff_history_tests
    history/RecentFilesTests.cpp
)

target_link_libraries(nff_history_tests PRIVATE nff_core)
target_compile_features(nff_history_tests PRIVATE cxx_std_23)

if(MSVC)
    target_compile_options(nff_history_tests PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(nff_history_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

add_test(NAME nff_history_tests COMMAND nff_history_tests)

add_executable(nff_filewatch_tests
    storage/FileWatcherTests.cpp
)

target_link_libraries(nff_filewatch_tests PRIVATE nff_core)
target_compile_features(nff_filewatch_tests PRIVATE cxx_std_23)

if(MSVC)
    target_compile_options(nff_filewatch_tests PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(nff_filewatch_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

add_test(NAME nff_filewatch_tests COMMAND nff_filewatch_tests)

add_executable(nff_appstate_tests
    app/ApplicationStateTests.cpp
)

target_link_libraries(nff_appstate_tests PRIVATE nff_core)
target_compile_features(nff_appstate_tests PRIVATE cxx_std_23)

if(MSVC)
    target_compile_options(nff_appstate_tests PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(nff_appstate_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

add_test(NAME nff_appstate_tests COMMAND nff_appstate_tests)

add_executable(nff_session_restore_tests
    session/SessionRestoreTests.cpp
)

target_link_libraries(nff_session_restore_tests PRIVATE nff_core)
target_compile_features(nff_session_restore_tests PRIVATE cxx_std_23)

if(MSVC)
    target_compile_options(nff_session_restore_tests PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(nff_session_restore_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

add_test(NAME nff_session_restore_tests COMMAND nff_session_restore_tests)

add_executable(nff_command_tests
    commands/CommandTests.cpp
)

target_link_libraries(nff_command_tests PRIVATE nff_core)
target_compile_features(nff_command_tests PRIVATE cxx_std_23)

if(MSVC)
    target_compile_options(nff_command_tests PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(nff_command_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

add_test(NAME nff_command_tests COMMAND nff_command_tests)


add_executable(nff_file_search_tests
    filesearch/FileSearchTests.cpp
)

target_link_libraries(nff_file_search_tests PRIVATE nff_core)
target_compile_features(nff_file_search_tests PRIVATE cxx_std_23)

if(MSVC)
    target_compile_options(nff_file_search_tests PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(nff_file_search_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

add_test(NAME nff_file_search_tests COMMAND nff_file_search_tests)

add_executable(nff_link_detector_tests
    links/LinkDetectorTests.cpp
)

target_link_libraries(nff_link_detector_tests PRIVATE nff_core)
target_compile_features(nff_link_detector_tests PRIVATE cxx_std_23)

if(MSVC)
    target_compile_options(nff_link_detector_tests PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(nff_link_detector_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

add_test(NAME nff_link_detector_tests COMMAND nff_link_detector_tests)

add_executable(nff_font_tests
    fonts/FontTests.cpp
)

target_link_libraries(nff_font_tests PRIVATE nff_core)
target_compile_features(nff_font_tests PRIVATE cxx_std_23)

if(MSVC)
    target_compile_options(nff_font_tests PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(nff_font_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

add_test(NAME nff_font_tests COMMAND nff_font_tests)

add_executable(nff_presentation_tests
    presentation/PresentationTests.cpp
)

target_link_libraries(nff_presentation_tests PRIVATE nff_core)
target_compile_features(nff_presentation_tests PRIVATE cxx_std_23)

if(MSVC)
    target_compile_options(nff_presentation_tests PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(nff_presentation_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

add_test(NAME nff_presentation_tests COMMAND nff_presentation_tests)

add_executable(nff_gui_shell_tests
    gui/GuiShellTests.cpp
)

target_link_libraries(nff_gui_shell_tests PRIVATE nff_core)
target_compile_features(nff_gui_shell_tests PRIVATE cxx_std_23)

if(MSVC)
    target_compile_options(nff_gui_shell_tests PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(nff_gui_shell_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

add_test(NAME nff_gui_shell_tests COMMAND nff_gui_shell_tests)

add_executable(nff_gui_runtime_tests
    gui/GuiRuntimeTests.cpp
)

target_link_libraries(nff_gui_runtime_tests PRIVATE nff_core)
target_compile_features(nff_gui_runtime_tests PRIVATE cxx_std_23)

if(MSVC)
    target_compile_options(nff_gui_runtime_tests PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(nff_gui_runtime_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

add_test(NAME nff_gui_runtime_tests COMMAND nff_gui_runtime_tests)

add_executable(nff_lifecycle_tests
    app/ApplicationLifecycleTests.cpp
)

target_link_libraries(nff_lifecycle_tests PRIVATE nff_core)
target_compile_features(nff_lifecycle_tests PRIVATE cxx_std_23)

if(MSVC)
    target_compile_options(nff_lifecycle_tests PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(nff_lifecycle_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

add_test(NAME nff_lifecycle_tests COMMAND nff_lifecycle_tests)

add_executable(nff_diagnostics_tests
    diagnostics/DiagnosticsTests.cpp
)

target_link_libraries(nff_diagnostics_tests PRIVATE nff_core)
target_compile_features(nff_diagnostics_tests PRIVATE cxx_std_23)

if(MSVC)
    target_compile_options(nff_diagnostics_tests PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(nff_diagnostics_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

add_test(NAME nff_diagnostics_tests COMMAND nff_diagnostics_tests)

add_executable(nff_content_routing_tests
    routing/ContentRoutingTests.cpp
)

target_link_libraries(nff_content_routing_tests PRIVATE nff_core)
target_compile_features(nff_content_routing_tests PRIVATE cxx_std_23)

if(MSVC)
    target_compile_options(nff_content_routing_tests PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(nff_content_routing_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

add_test(NAME nff_content_routing_tests COMMAND nff_content_routing_tests)

add_executable(nff_gui_automation_tests
    gui/GuiAutomationTests.cpp
)

target_link_libraries(nff_gui_automation_tests PRIVATE nff_gui_automation_common)
target_compile_features(nff_gui_automation_tests PRIVATE cxx_std_23)

if(MSVC)
    target_compile_options(nff_gui_automation_tests PRIVATE /W4 /permissive- /EHsc /utf-8)
else()
    target_compile_options(nff_gui_automation_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

add_test(NAME nff_gui_automation_tests COMMAND nff_gui_automation_tests)

add_executable(nff_security_hardening_tests
    security/SecurityHardeningTests.cpp
)

target_link_libraries(nff_security_hardening_tests PRIVATE nff_core)
target_compile_features(nff_security_hardening_tests PRIVATE cxx_std_23)

if(MSVC)
    target_compile_options(nff_security_hardening_tests PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(nff_security_hardening_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

add_test(NAME nff_security_hardening_tests COMMAND nff_security_hardening_tests)

add_test(
    NAME nff_portable_release_contract
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/release/PortableReleaseContract.cmake
)
add_test(
    NAME nff_release_tree_contract
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/release/ReleaseTreeContract.cmake
)
add_test(
    NAME nff_security_release_contract
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/release/SecurityReleaseContractAll.cmake
)
