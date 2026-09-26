#pragma once

#include "notepadFasaFiso/core/Document.hpp"
#include "notepadFasaFiso/core/DocumentManager.hpp"
#include "notepadFasaFiso/core/TextAnalysis.hpp"
#include "notepadFasaFiso/metadata/MetadataStore.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <system_error>
#include <vector>

namespace nff::recovery {

struct RecoverySnapshot {
    core::DocumentId documentId{};
    std::filesystem::path originalPath;
    std::string text;
    encoding::Encoding encoding{encoding::Encoding::Utf8};
    core::LineEnding lineEnding{core::LineEnding::Unknown};
    bool writeBom{false};
    std::uint64_t capturedUnixMilliseconds{0};
    metadata::TextAppearanceMap appearance;
};

struct RecoveryLoadResult {
    RecoverySnapshot snapshot{};
    std::error_code error{};

    [[nodiscard]] explicit operator bool() const noexcept { return !error; }
};

class RecoveryManager final {
public:
    using AppearanceProvider =
        std::function<const metadata::TextAppearanceMap*(core::DocumentId)>;

    static constexpr std::size_t defaultMaximumSnapshotBytes = 512ULL * 1024ULL * 1024ULL;

    explicit RecoveryManager(std::filesystem::path root, std::uint64_t sessionId = 0);

    [[nodiscard]] std::uint64_t sessionId() const noexcept;
    [[nodiscard]] const std::filesystem::path& root() const noexcept;
    void setAppearanceProvider(AppearanceProvider provider);
    [[nodiscard]] std::filesystem::path snapshotPath(core::DocumentId documentId) const;

    [[nodiscard]] std::error_code checkpoint(
        core::DocumentId documentId,
        const core::Document& document,
        const metadata::TextAppearanceMap* appearance = nullptr);
    [[nodiscard]] std::error_code discard(core::DocumentId documentId) noexcept;
    [[nodiscard]] std::error_code discardSnapshot(const std::filesystem::path& path) noexcept;
    [[nodiscard]] RecoveryLoadResult load(const std::filesystem::path& snapshotPath,
                                          std::size_t maximumBytes =
                                              defaultMaximumSnapshotBytes) const;
    [[nodiscard]] std::vector<std::filesystem::path> list(std::error_code& error) const;

private:
    [[nodiscard]] static std::uint64_t generateSessionId() noexcept;

    std::filesystem::path root_;
    std::uint64_t sessionId_{0};
    AppearanceProvider appearanceProvider_{};
};

}
