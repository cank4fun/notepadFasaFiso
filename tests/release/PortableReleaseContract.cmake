cmake_minimum_required(VERSION 3.24)
get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

function(require_contains path needle)
    file(READ "${path}" content)
    string(FIND "${content}" "${needle}" pos)
    if(pos EQUAL -1)
        message(FATAL_ERROR "${path} does not contain required text: ${needle}")
    endif()
endfunction()

function(require_not_contains path needle)
    file(READ "${path}" content)
    string(FIND "${content}" "${needle}" pos)
    if(NOT pos EQUAL -1)
        message(FATAL_ERROR "${path} still contains forbidden text: ${needle}")
    endif()
endfunction()

require_contains("${ROOT}/CMakeLists.txt" "NFF_PORTABLE_WINDOWS")
require_contains("${ROOT}/CMakeLists.txt" "MSVC_RUNTIME_LIBRARY")
require_contains("${ROOT}/CMakeLists.txt" "x64-windows-static")
require_contains("${ROOT}/CMakeLists.txt" "NFF_PORTABLE_WINDOWS requires NFF_BUILD_WX_GUI=ON")
require_contains("${ROOT}/CMakePresets.json" "windows-portable")
require_contains("${ROOT}/CMakePresets.json" "linux-appimage")
require_contains("${ROOT}/CMakeLists.txt" "NffEmbeddedTrayPng")
require_not_contains("${ROOT}/CMakeLists.txt" "copy_if_different\n            ${CMAKE_CURRENT_SOURCE_DIR}/assets/nff-tray.png")

foreach(required
    "tools/release/package-portable-windows.ps1"
    "tools/release/verify-portable-windows.ps1"
    "tools/release/package-appimage.sh"
    "tools/release/verify-appimage.sh"
    "tools/release/make-release-tree.ps1"
    "tools/release/verify-release-tree.ps1"
    "packaging/vcpkg/vcpkg.json"
    "packaging/vcpkg/vcpkg-configuration.json"
)
    if(NOT EXISTS "${ROOT}/${required}")
        message(FATAL_ERROR "Missing release file: ${required}")
    endif()
endforeach()
