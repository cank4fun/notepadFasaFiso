#pragma once

#include "notepadFasaFiso/core/Document.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <system_error>
#include <vector>

namespace nff::core {

struct DocumentId {
    std::uint64_t value{0};

    [[nodiscard]] explicit operator bool() const noexcept { return value != 0; }
    friend bool operator==(DocumentId, DocumentId) = default;
    friend auto operator<=>(DocumentId, DocumentId) = default;
};

struct OpenDocumentResult {
    DocumentId id{};
    std::error_code error{};
    bool reusedExisting{false};

    [[nodiscard]] explicit operator bool() const noexcept { return !error && static_cast<bool>(id); }
};

class DocumentManager final {
public:
    [[nodiscard]] DocumentId createUntitled();
    [[nodiscard]] OpenDocumentResult createNewFile(const std::filesystem::path& path);
    [[nodiscard]] OpenDocumentResult open(
        const std::filesystem::path& path,
        std::size_t maximumBytes = Document::defaultEditorLoadLimit);
    [[nodiscard]] OpenDocumentResult openRouted(
        const std::filesystem::path& path,
        const InspectOptions& inspectOptions,
        std::size_t maximumBytes = Document::defaultEditorLoadLimit);
    [[nodiscard]] OpenDocumentResult openPrepared(
        const std::filesystem::path& path,
        DocumentProfile profile,
        std::size_t maximumBytes = Document::defaultEditorLoadLimit);
    [[nodiscard]] std::error_code reloadRouted(
        DocumentId id,
        const InspectOptions& inspectOptions = {},
        std::size_t maximumBytes = Document::defaultEditorLoadLimit);
    [[nodiscard]] std::error_code reloadAsEncoding(
        DocumentId id,
        encoding::Encoding sourceEncoding,
        std::size_t maximumBytes = Document::defaultEditorLoadLimit);
    [[nodiscard]] std::error_code materializeForEdit(
        DocumentId id,
        std::size_t maximumBytes);
    [[nodiscard]] std::error_code dematerializeForViewer(
        DocumentId id,
        const InspectOptions& inspectOptions = {},
        bool allowDiscardModified = false);

    [[nodiscard]] Document* get(DocumentId id) noexcept;
    [[nodiscard]] const Document* get(DocumentId id) const noexcept;
    [[nodiscard]] std::optional<DocumentId> findByPath(const std::filesystem::path& path) const;

    [[nodiscard]] bool close(DocumentId id) noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::vector<DocumentId> ids() const;

private:
    [[nodiscard]] DocumentId allocateId() noexcept;
    [[nodiscard]] static bool pathsReferToSameFile(const std::filesystem::path& left,
                                                   const std::filesystem::path& right);

    std::map<DocumentId, std::unique_ptr<Document>> documents_;
    std::uint64_t nextId_{1};
};

}
