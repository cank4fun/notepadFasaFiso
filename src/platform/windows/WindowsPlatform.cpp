#include "notepadFasaFiso/platform/Platform.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <shellapi.h>
#include <dwrite.h>

#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace nff::platform {

namespace {

[[nodiscard]] std::string wideToUtf8(const std::wstring_view text, std::error_code& error) {
    if (text.empty()) {
        error.clear();
        return {};
    }
    if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        error = std::make_error_code(std::errc::value_too_large);
        return {};
    }

    const auto length = static_cast<int>(text.size());
    const int required = ::WideCharToMultiByte(CP_UTF8,
                                                WC_ERR_INVALID_CHARS,
                                                text.data(),
                                                length,
                                                nullptr,
                                                0,
                                                nullptr,
                                                nullptr);
    if (required <= 0) {
        error = {static_cast<int>(::GetLastError()), std::system_category()};
        return {};
    }

    std::string result(static_cast<std::size_t>(required), '\0');
    if (::WideCharToMultiByte(CP_UTF8,
                              WC_ERR_INVALID_CHARS,
                              text.data(),
                              length,
                              result.data(),
                              required,
                              nullptr,
                              nullptr) != required) {
        error = {static_cast<int>(::GetLastError()), std::system_category()};
        return {};
    }

    error.clear();
    return result;
}

}

CommandLineResult commandLineArguments(const int argc, char* const* argv) {
    CommandLineResult result;
    if (argc <= 1 || argv == nullptr) {
        return result;
    }

    result.arguments.reserve(static_cast<std::size_t>(argc - 1));
    for (int index = 1; index < argc; ++index) {
        if (argv[index] == nullptr) {
            return {{}, std::make_error_code(std::errc::invalid_argument)};
        }
        result.arguments.emplace_back(argv[index]);
    }
    return result;
}

CommandLineResult nativeCommandLineArguments(const int, char* const*) {
    int argumentCount = 0;
    wchar_t** wideArguments = ::CommandLineToArgvW(::GetCommandLineW(), &argumentCount);
    if (wideArguments == nullptr) {
        return {{}, {static_cast<int>(::GetLastError()), std::system_category()}};
    }

    CommandLineResult result;
    if (argumentCount > 1) {
        result.arguments.reserve(static_cast<std::size_t>(argumentCount - 1));
    }

    for (int index = 1; index < argumentCount; ++index) {
        std::error_code error;
        auto argument = wideToUtf8(wideArguments[index], error);
        if (error) {
            ::LocalFree(wideArguments);
            return {{}, error};
        }
        result.arguments.push_back(std::move(argument));
    }

    ::LocalFree(wideArguments);
    return result;
}

bool replaceFile(const std::filesystem::path& temporary,
                 const std::filesystem::path& target,
                 std::error_code& error) noexcept {
    const DWORD attributes = ::GetFileAttributesW(target.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        if (::ReplaceFileW(target.c_str(),
                           temporary.c_str(),
                           nullptr,
                           REPLACEFILE_WRITE_THROUGH,
                           nullptr,
                           nullptr) != 0) {
            error.clear();
            return true;
        }

        const DWORD replaceError = ::GetLastError();
        if (replaceError != ERROR_FILE_NOT_FOUND && replaceError != ERROR_PATH_NOT_FOUND) {
            error = std::error_code(static_cast<int>(replaceError), std::system_category());
            return false;
        }
    }

    if (::MoveFileExW(temporary.c_str(), target.c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0) {
        error.clear();
        return true;
    }

    error = std::error_code(static_cast<int>(::GetLastError()), std::system_category());
    return false;
}

std::uint64_t availablePhysicalMemoryBytes() noexcept {
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (::GlobalMemoryStatusEx(&status) == 0) {
        return 0U;
    }
    return static_cast<std::uint64_t>(status.ullAvailPhys);
}

std::filesystem::path applicationDataDirectory() {
    DWORD required = ::GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (required == 0U) {
        required = ::GetEnvironmentVariableW(L"USERPROFILE", nullptr, 0);
        if (required == 0U) {
            return {};
        }
        std::wstring buffer(static_cast<std::size_t>(required), L'\0');
        const auto written = ::GetEnvironmentVariableW(L"USERPROFILE", buffer.data(), required);
        if (written == 0U || written >= required) {
            return {};
        }
        buffer.resize(static_cast<std::size_t>(written));
        return std::filesystem::path(buffer) / L"AppData" / L"Local" / L"notepadFasaFiso";
    }

    std::wstring buffer(static_cast<std::size_t>(required), L'\0');
    const auto written = ::GetEnvironmentVariableW(L"LOCALAPPDATA", buffer.data(), required);
    if (written == 0U || written >= required) {
        return {};
    }
    buffer.resize(static_cast<std::size_t>(written));
    return std::filesystem::path(buffer) / L"notepadFasaFiso";
}

FontEnumerationResult systemFontFamilies() {
    IDWriteFactory* factory = nullptr;
    const HRESULT factoryResult = ::DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(&factory));
    if (FAILED(factoryResult) || factory == nullptr) {
        return {{}, std::error_code(static_cast<int>(factoryResult), std::system_category())};
    }

    IDWriteFontCollection* collection = nullptr;
    const HRESULT collectionResult = factory->GetSystemFontCollection(&collection, TRUE);
    if (FAILED(collectionResult) || collection == nullptr) {
        factory->Release();
        return {{}, std::error_code(static_cast<int>(collectionResult), std::system_category())};
    }

    FontEnumerationResult result;
    const UINT32 familyCount = collection->GetFontFamilyCount();
    result.families.reserve(static_cast<std::size_t>(familyCount));

    for (UINT32 familyIndex = 0U; familyIndex < familyCount; ++familyIndex) {
        IDWriteFontFamily* family = nullptr;
        if (FAILED(collection->GetFontFamily(familyIndex, &family)) || family == nullptr) {
            continue;
        }

        IDWriteLocalizedStrings* names = nullptr;
        if (FAILED(family->GetFamilyNames(&names)) || names == nullptr) {
            family->Release();
            continue;
        }

        UINT32 stringIndex = 0U;
        BOOL exists = FALSE;
        if (FAILED(names->FindLocaleName(L"en-us", &stringIndex, &exists)) || exists == FALSE) {
            stringIndex = 0U;
        }

        UINT32 length = 0U;
        if (SUCCEEDED(names->GetStringLength(stringIndex, &length))) {
            std::wstring wide(static_cast<std::size_t>(length) + 1U, L'\0');
            if (SUCCEEDED(names->GetString(stringIndex, wide.data(), length + 1U))) {
                wide.resize(static_cast<std::size_t>(length));
                std::error_code conversionError;
                auto utf8 = wideToUtf8(wide, conversionError);
                if (!conversionError && !utf8.empty()) {
                    result.families.push_back(std::move(utf8));
                }
            }
        }

        names->Release();
        family->Release();
    }

    collection->Release();
    factory->Release();
    return result;
}

}
