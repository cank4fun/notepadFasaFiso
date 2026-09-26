#include "notepadFasaFiso/platform/Platform.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <dwrite.h>

#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace nff::platform {

namespace {

[[nodiscard]] bool writeRegistryString(const HKEY root,
                                       const std::wstring_view subkey,
                                       const wchar_t* valueName,
                                       const std::wstring_view value) noexcept {
    HKEY key = nullptr;
    if (::RegCreateKeyExW(root,
                          std::wstring(subkey).c_str(),
                          0,
                          nullptr,
                          REG_OPTION_NON_VOLATILE,
                          KEY_SET_VALUE,
                          nullptr,
                          &key,
                          nullptr) != ERROR_SUCCESS) {
        return false;
    }

    const std::wstring owned(value);
    const auto bytes = static_cast<DWORD>((owned.size() + 1U) * sizeof(wchar_t));
    const auto status = ::RegSetValueExW(
        key,
        valueName,
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(owned.c_str()),
        bytes);
    ::RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

[[nodiscard]] bool writeRegistryNone(const HKEY root,
                                     const std::wstring_view subkey,
                                     const wchar_t* valueName) noexcept {
    HKEY key = nullptr;
    if (::RegCreateKeyExW(root,
                          std::wstring(subkey).c_str(),
                          0,
                          nullptr,
                          REG_OPTION_NON_VOLATILE,
                          KEY_SET_VALUE,
                          nullptr,
                          &key,
                          nullptr) != ERROR_SUCCESS) {
        return false;
    }

    const auto status = ::RegSetValueExW(key, valueName, 0, REG_NONE, nullptr, 0);
    ::RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

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

void refreshFileAssociations() noexcept {
    std::wstring executable(32768U, L'\0');
    const DWORD length =
        ::GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (length == 0U || length >= executable.size()) {
        return;
    }
    executable.resize(static_cast<std::size_t>(length));

    const std::wstring quotedExecutable = L"\"" + executable + L"\"";
    const std::wstring command = quotedExecutable + L" \"%1\"";
    const std::wstring icon = quotedExecutable + L",0";

    constexpr std::wstring_view applicationKey =
        L"Software\\Classes\\Applications\\notepadFasaFiso.exe";
    constexpr std::wstring_view progIdKey =
        L"Software\\Classes\\notepadFasaFiso.txt";

    static_cast<void>(writeRegistryString(
        HKEY_CURRENT_USER, applicationKey, L"FriendlyAppName", L"notepadFasaFiso"));
    static_cast<void>(writeRegistryString(
        HKEY_CURRENT_USER,
        L"Software\\Classes\\Applications\\notepadFasaFiso.exe\\DefaultIcon",
        nullptr,
        icon));
    static_cast<void>(writeRegistryString(
        HKEY_CURRENT_USER,
        L"Software\\Classes\\Applications\\notepadFasaFiso.exe\\shell\\open\\command",
        nullptr,
        command));
    static_cast<void>(writeRegistryString(
        HKEY_CURRENT_USER,
        L"Software\\Classes\\Applications\\notepadFasaFiso.exe\\SupportedTypes",
        L".txt",
        L""));

    static_cast<void>(writeRegistryString(
        HKEY_CURRENT_USER, progIdKey, nullptr, L"notepadFasaFiso Text Document"));
    static_cast<void>(writeRegistryString(
        HKEY_CURRENT_USER,
        L"Software\\Classes\\notepadFasaFiso.txt\\DefaultIcon",
        nullptr,
        icon));
    static_cast<void>(writeRegistryString(
        HKEY_CURRENT_USER,
        L"Software\\Classes\\notepadFasaFiso.txt\\shell\\open\\command",
        nullptr,
        command));
    static_cast<void>(writeRegistryNone(
        HKEY_CURRENT_USER,
        L"Software\\Classes\\.txt\\OpenWithProgids",
        L"notepadFasaFiso.txt"));

    constexpr std::wstring_view capabilitiesKey =
        L"Software\\Classes\\Applications\\notepadFasaFiso.exe\\Capabilities";
    static_cast<void>(writeRegistryString(
        HKEY_CURRENT_USER, capabilitiesKey, L"ApplicationName", L"notepadFasaFiso"));
    static_cast<void>(writeRegistryString(
        HKEY_CURRENT_USER,
        capabilitiesKey,
        L"ApplicationDescription",
        L"Fast native text editor and file viewer"));
    static_cast<void>(writeRegistryString(
        HKEY_CURRENT_USER,
        L"Software\\Classes\\Applications\\notepadFasaFiso.exe\\Capabilities\\FileAssociations",
        L".txt",
        L"notepadFasaFiso.txt"));
    static_cast<void>(writeRegistryString(
        HKEY_CURRENT_USER,
        L"Software\\RegisteredApplications",
        L"notepadFasaFiso",
        L"Software\\Classes\\Applications\\notepadFasaFiso.exe\\Capabilities"));

    ::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
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
