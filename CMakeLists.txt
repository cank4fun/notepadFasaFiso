cmake_minimum_required(VERSION 3.24)

project(notepadFasaFiso LANGUAGES CXX)

option(NFF_BUILD_TESTS "Build notepadFasaFiso tests" ON)
option(NFF_BUILD_BENCHMARKS "Build notepadFasaFiso benchmarks" OFF)
option(NFF_BUILD_WX_GUI "Build the wxWidgets desktop GUI" OFF)
option(NFF_BUILD_GUI_DRIVER "Build the Windows GUI automation driver" OFF)
option(NFF_ENABLE_GUI_AUTOMATION "Enable the in-process GUI automation bridge" OFF)
option(NFF_PORTABLE_WINDOWS "Build the Windows GUI as a standalone static-runtime release" OFF)
option(NFF_APPIMAGE_BUILD "Configure the Linux GUI for AppImage packaging" OFF)

if(NFF_BUILD_GUI_DRIVER AND NOT WIN32)
    message(FATAL_ERROR "NFF_BUILD_GUI_DRIVER is supported on Windows only")
endif()

if(NFF_ENABLE_GUI_AUTOMATION AND (NOT WIN32 OR NOT NFF_BUILD_WX_GUI))
    message(FATAL_ERROR "NFF_ENABLE_GUI_AUTOMATION requires Windows and NFF_BUILD_WX_GUI=ON")
endif()

if(NFF_PORTABLE_WINDOWS AND NFF_ENABLE_GUI_AUTOMATION)
    message(FATAL_ERROR "NFF_PORTABLE_WINDOWS AND NFF_ENABLE_GUI_AUTOMATION must never be enabled together")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NFF_PORTABLE_WINDOWS)
    if(NOT WIN32 OR NOT MSVC)
        message(FATAL_ERROR "NFF_PORTABLE_WINDOWS requires Windows with MSVC")
    endif()
    if(NOT NFF_BUILD_WX_GUI)
        message(FATAL_ERROR "NFF_PORTABLE_WINDOWS requires NFF_BUILD_WX_GUI=ON")
    endif()
    if(NOT DEFINED VCPKG_TARGET_TRIPLET OR NOT VCPKG_TARGET_TRIPLET STREQUAL "x64-windows-static")
        message(FATAL_ERROR "NFF_PORTABLE_WINDOWS requires VCPKG_TARGET_TRIPLET=x64-windows-static")
    endif()
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set(wxWidgets_USE_STATIC ON)
endif()

if(NFF_APPIMAGE_BUILD AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "NFF_APPIMAGE_BUILD is supported on Linux only")
endif()

add_library(nff_core STATIC
    src/app/ApplicationLifecycle.cpp
    src/app/ApplicationStateStore.cpp
    src/app/CommandSystem.cpp
    src/app/DocumentFileMonitor.cpp
    src/app/LaunchRequest.cpp
    src/app/LaunchRouter.cpp
    src/app/PresentationModel.cpp
    src/core/Document.cpp
    src/core/FileSniffer.cpp
    src/core/DocumentManager.cpp
    src/core/ExportService.cpp
    src/core/LinkDetector.cpp
    src/core/TextAnalysis.cpp
    src/core/TextTransform.cpp
    src/core/TextUtilities.cpp
    src/diagnostics/LocalDiagnostics.cpp
    src/encoding/EncodingDetector.cpp
    src/encoding/TextCodec.cpp
    src/fonts/FontManager.cpp
    src/fonts/SfntFontReader.cpp
    src/formats/FormatDetector.cpp
    src/formats/FormatTransform.cpp
    src/gui/FileDrop.cpp
    src/gui/ColorWheelModel.cpp
    src/gui/GuiRuntime.cpp
    src/gui/GuiPolishModel.cpp
    src/gui/SelectionAppearanceModel.cpp
    src/gui/GuiShell.cpp
    src/gui/ShellLayout.cpp
    src/gui/StartupSplash.cpp
    src/history/RecentFiles.cpp
    src/history/Breadcrumbs.cpp
    src/metadata/MetadataStore.cpp
    src/persistence/BinaryCodec.cpp
    src/recovery/AutoSaveManager.cpp
    src/recovery/RecoveryManager.cpp
    src/search/FileSearch.cpp
    src/search/SearchSource.cpp
    src/search/TextSearch.cpp
    src/session/SessionRestorer.cpp
    src/session/SessionStore.cpp
    src/settings/AppSettings.cpp
    src/settings/SettingsStore.cpp
    src/storage/AtomicSave.cpp
    src/storage/FileReader.cpp
    src/storage/FileState.cpp
    src/storage/FileWriter.cpp
    src/storage/FileWatcher.cpp
    src/viewer/HexPreview.cpp
    src/viewer/LargeFileViewer.cpp
    src/viewer/LiveFileFollower.cpp
    src/viewer/TextPresentation.cpp
    src/viewer/LineIndex.cpp
    src/viewer/ViewCache.cpp
    src/workspace/WorkspaceModel.cpp
    src/platform/Platform.cpp
)

target_include_directories(nff_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(nff_core PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(nff_core PRIVATE /W4 /permissive- /EHsc /utf-8)
else()
    target_compile_options(nff_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

if(WIN32)
    target_sources(nff_core PRIVATE
        src/platform/windows/WindowsPlatform.cpp
        src/diagnostics/WindowsCrashReporter.cpp
        src/storage/RandomAccessFileWindows.cpp
    )
    target_link_libraries(nff_core PRIVATE Shell32 Dwrite Dbghelp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(nff_core PRIVATE
        src/platform/linux/LinuxPlatform.cpp
        src/diagnostics/CrashReporterStub.cpp
        src/storage/RandomAccessFilePosix.cpp
    )
else()
    message(FATAL_ERROR "notepadFasaFiso supports Windows and Linux only")
endif()

add_executable(notepadFasaFiso src/main.cpp)
target_link_libraries(notepadFasaFiso PRIVATE nff_core)

if(NFF_BUILD_TESTS OR NFF_BUILD_GUI_DRIVER OR NFF_ENABLE_GUI_AUTOMATION)
    add_library(nff_gui_automation_common STATIC
        src/gui/automation/AutomationCommandLine.cpp
        src/gui/automation/AutomationProtocol.cpp
        src/gui/automation/AutomationScenario.cpp
    )
    target_include_directories(nff_gui_automation_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_features(nff_gui_automation_common PUBLIC cxx_std_23)
    if(MSVC)
        target_compile_options(nff_gui_automation_common PRIVATE /W4 /permissive- /EHsc /utf-8)
    else()
        target_compile_options(nff_gui_automation_common PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
    endif()
endif()

if(NFF_BUILD_GUI_DRIVER)
    add_executable(nff_gui_driver
        src/gui/automation/driver/GuiDriverMain.cpp
        src/gui/automation/driver/ScenarioRunner.cpp
        src/gui/automation/windows/AutomationPipeClient.cpp
        src/gui/automation/windows/GuiDriverWindows.cpp
        src/gui/automation/windows/WindowCapture.cpp
    )
    target_include_directories(nff_gui_driver PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/automation/windows
        ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/automation/driver
    )
    target_link_libraries(nff_gui_driver PRIVATE
        nff_gui_automation_common
        User32
        Gdi32
        Ole32
        Windowscodecs
    )
    target_compile_features(nff_gui_driver PRIVATE cxx_std_23)
    if(MSVC)
        target_compile_definitions(nff_gui_driver PRIVATE NOMINMAX)
        target_compile_options(nff_gui_driver PRIVATE /W4 /permissive- /EHsc /utf-8)
    else()
        target_compile_options(nff_gui_driver PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
        )
    endif()
endif()

if(NFF_BUILD_WX_GUI)
    if(NFF_PORTABLE_WINDOWS)
        find_package(wxWidgets CONFIG REQUIRED COMPONENTS base core stc)
    else()
        find_package(wxWidgets 3.2 REQUIRED COMPONENTS base core stc)
        include(UsewxWidgets)
    endif()

    set(NFF_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
    file(MAKE_DIRECTORY "${NFF_GENERATED_DIR}")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/nff-tray.png" NFF_TRAY_PNG_HEX HEX)
    string(REGEX REPLACE "([0-9A-Fa-f][0-9A-Fa-f])" "0x\\1," NFF_TRAY_PNG_BYTES "${NFF_TRAY_PNG_HEX}")
    file(WRITE "${NFF_GENERATED_DIR}/NffEmbeddedTrayPng.hpp"
        "#pragma once\n#include <cstddef>\nnamespace nff::gui::wxbackend::embedded {\n"
        "inline constexpr unsigned char kTrayPng[] = {${NFF_TRAY_PNG_BYTES}};\n"
        "inline constexpr std::size_t kTrayPngSize = sizeof(kTrayPng);\n}\n")

    if(WIN32)
        add_executable(notepadFasaFiso_gui WIN32
            src/gui/wx/WxApp.cpp
            src/gui/wx/WxEditorHost.cpp
            src/gui/wx/WxMainFrame.cpp
            src/gui/wx/WxStartupSplash.cpp
            src/gui/wx/WxTheme.cpp
            src/gui/wx/WxTitleBar.cpp
            src/gui/wx/WxTrayIcon.cpp
        )
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)

        add_executable(notepadFasaFiso_gui
            src/gui/wx/WxApp.cpp
            src/gui/wx/WxEditorHost.cpp
            src/gui/wx/WxMainFrame.cpp
            src/gui/wx/WxStartupSplash.cpp
            src/gui/wx/WxTheme.cpp
            src/gui/wx/WxTitleBar.cpp
            src/gui/wx/WxTrayIcon.cpp
        )
    endif()

    target_link_libraries(notepadFasaFiso_gui PRIVATE nff_core ${wxWidgets_LIBRARIES})
    if(NFF_ENABLE_GUI_AUTOMATION)
        target_sources(notepadFasaFiso_gui PRIVATE
            src/gui/wx/WxAutomationSnapshot.cpp
            src/gui/automation/windows/AutomationPipeServer.cpp
        )
        target_compile_definitions(notepadFasaFiso_gui PRIVATE NFF_ENABLE_GUI_AUTOMATION=1)
        target_include_directories(notepadFasaFiso_gui PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/automation/windows
        )
        target_link_libraries(notepadFasaFiso_gui PRIVATE nff_gui_automation_common Advapi32)
    endif()
    if(WIN32)
        target_link_libraries(notepadFasaFiso_gui PRIVATE Dwmapi)
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(notepadFasaFiso_gui PRIVATE PkgConfig::GTK3)
    endif()
    target_include_directories(notepadFasaFiso_gui PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/wx
        ${NFF_GENERATED_DIR}
    )
    target_compile_features(notepadFasaFiso_gui PRIVATE cxx_std_23)

    if(MSVC)
        target_compile_options(notepadFasaFiso_gui PRIVATE /W4 /permissive- /EHsc /utf-8)
        if(NFF_PORTABLE_WINDOWS)
            target_link_options(notepadFasaFiso_gui PRIVATE "/DEPENDENTLOADFLAG:0x800" "/Brepro")
        endif()
    else()
        target_compile_options(notepadFasaFiso_gui PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
        )
    endif()
endif()

if(NFF_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(NFF_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
