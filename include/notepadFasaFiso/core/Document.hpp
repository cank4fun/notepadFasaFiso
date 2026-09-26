#pragma once

#include "notepadFasaFiso/core/FileSniffer.hpp"
#include "notepadFasaFiso/core/SaveOptions.hpp"
#include "notepadFasaFiso/core/TextAnalysis.hpp"
#include "notepadFasaFiso/storage/FileState.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace nff::core {

class DocumentManager;

class Document final {
public:
    static constexpr std::size_t defaultEditorLoadLimit = 256ULL * 1024ULL * 1024ULL;

    [[nodiscard]] std::error_code load(const std::filesystem::path& path,
                                       std::size_t maximumBytes = defaultEditorLoadLimit);
    [[nodiscard]] std::error_code loadAs(const std::filesystem::path& path,
                                         encoding::Encoding sourceEncoding,
                                         bool hasBom = false,
                                         std::size_t maximumBytes = defaultEditorLoadLimit);
    [[nodiscard]] std::error_code save();
    [[nodiscard]] std::error_code save(const SaveOptions& options);
    [[nodiscard]] std::error_code saveAs(const std::filesystem::path& path);
    [[nodiscard]] std::error_code saveAs(const std::filesystem::path& path,
                                         const SaveOptions& options);
    [[nodiscard]] std::error_code saveCopy(const std::filesystem::path& path);
    [[nodiscard]] std::error_code saveCopy(const std::filesystem::path& path,
                                           const SaveOptions& options);

    [[nodiscard]] const std::filesystem::path& path() const noexcept;
    [[nodiscard]] std::string_view text() const noexcept;
    [[nodiscard]] const DocumentProfile& profile() const noexcept;
    [[nodiscard]] const storage::FileState* trackedFileState() const noexcept;
    [[nodiscard]] encoding::Encoding saveEncoding() const noexcept;
    [[nodiscard]] bool writesBom() const noexcept;
    [[nodiscard]] std::error_code setSaveEncoding(encoding::Encoding encoding);
    [[nodiscard]] std::error_code setWritesBom(bool writeBom);
    [[nodiscard]] bool modified() const noexcept;
    [[nodiscard]] bool textBufferLoaded() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;
    [[nodiscard]] storage::FileChangeState externalChangeState() const noexcept;
    [[nodiscard]] bool requiresExplicitOverwrite() const noexcept;

    [[nodiscard]] std::error_code restoreRecovered(std::filesystem::path path,
                                                   std::string text,
                                                   encoding::Encoding saveEncoding,
                                                   bool writeBom);

    [[nodiscard]] std::error_code applyEdit(std::size_t offset,
                                             std::size_t eraseBytes,
                                             std::string_view insertedText);
    void replaceText(std::string text);
    void markModified() noexcept;
    void markClean() noexcept;
    void clear() noexcept;

private:
    friend class DocumentManager;

    [[nodiscard]] std::error_code loadInspected(const std::filesystem::path& path,
                                                const DocumentProfile& profile,
                                                std::size_t maximumBytes);
    void prepareNewFile(const std::filesystem::path& path);
    void attachReference(const std::filesystem::path& path, const DocumentProfile& profile);
    [[nodiscard]] std::error_code loadDecoded(const std::filesystem::path& path,
                                              const DocumentProfile& profile,
                                              encoding::Encoding sourceEncoding,
                                              bool hasBom,
                                              std::size_t maximumBytes);
    [[nodiscard]] std::error_code writeTo(const std::filesystem::path& path,
                                          const SaveOptions& options,
                                          bool adoptPath,
                                          bool commitDocumentState,
                                          bool checkExternalState);
    void refreshTextProfile(std::size_t encodedSize) noexcept;
    void setTextStatistics(const TextStatistics& statistics) noexcept;
    void updateTextStatisticsForEdit(const TextStatistics& oldWindowStatistics,
                                     std::size_t windowStart,
                                     std::size_t oldWindowEnd,
                                     std::size_t erasedBytes,
                                     std::size_t insertedBytes);

    std::filesystem::path path_;
    std::string text_;
    DocumentProfile profile_{};
    TextStatistics textStatistics_{};
    encoding::Encoding saveEncoding_{encoding::Encoding::Utf8};
    bool writesBom_{false};
    bool modified_{false};
    bool textBufferLoaded_{true};
    std::uint64_t revision_{0};
    storage::FileState diskState_{};
    bool tracksDiskState_{false};
    bool requiresExplicitOverwrite_{false};
};

}
