#pragma once

#if defined(_WIN32)

#include "GuiDriverWindows.hpp"

#include <windows.h>

#include <expected>
#include <filesystem>

namespace nff::gui::automation::windows {

[[nodiscard]] std::expected<void, DriverError>
captureWindowPng(HWND window, const std::filesystem::path& outputPath);

}

#endif
