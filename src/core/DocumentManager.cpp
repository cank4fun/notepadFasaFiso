#include "notepadFasaFiso/core/DocumentManager.hpp"

namespace nff::core {

DocumentId DocumentManager::createUntitled() {
    const auto id = allocateId();
    documents_.emplace(id, std::make_unique<Document>());
    return id;
}

OpenDocumentResult DocumentManager::createNewFile(const std::filesystem::path& path) {
    if (path.empty()) {
        return {{}, std::make_error_code(std::errc::invalid_argument), false};
    }
    if (const auto existing = findByPath(path)) {
        return {*existing, {}, true};
    }

    std::error_code statusError;
    const auto status = std::filesystem::status(path, statusError);
    if (!statusError && std::filesystem::exists(status)) {
        return {{}, std::make_error_code(std::errc::file_exists), false};
    }
    if (statusError && statusError != std::errc::no_such_file_or_directory) {
        return {{}, statusError, false};
    }

    auto document = std::make_unique<Document>();
    document->prepareNewFile(path);
    const auto id = allocateId();
    documents_.emplace(id, std::move(document));
    return {id, {}, false};
}

OpenDocumentResult DocumentManager::open(const std::filesystem::path& path,
                                         const std::size_t maximumBytes) {
    return openRouted(path, InspectOptions{}, maximumBytes);
}

OpenDocumentResult DocumentManager::openRouted(const std::filesystem::path& path,
                                               const InspectOptions& inspectOptions,
                                               const std::size_t maximumBytes) {
    if (const auto existing = findByPath(path)) {
        return {*existing, {}, true};
    }

    const auto inspection = FileSniffer::inspect(path, inspectOptions);
    if (!inspection) {
        return {{}, inspection.error, false};
    }

    return openPrepared(path, inspection.profile, maximumBytes);
}

OpenDocumentResult DocumentManager::openPrepared(const std::filesystem::path& path,
                                                 DocumentProfile profile,
                                                 const std::size_t maximumBytes) {
    if (const auto existing = findByPath(path)) {
        return {*existing, {}, true};
    }

    if (path.empty()) {
        return {{}, std::make_error_code(std::errc::invalid_argument), false};
    }
    profile.path = path;

    auto document = std::make_unique<Document>();
    std::error_code error;
    if (profile.recommendedMode == OpenMode::Editor) {
        error = document->loadInspected(path, profile, maximumBytes);
    } else {
        document->attachReference(path, profile);
    }
    if (error) {
        return {{}, error, false};
    }

    const auto id = allocateId();
    documents_.emplace(id, std::move(document));
    return {id, {}, false};
}

std::error_code DocumentManager::reloadRouted(const DocumentId id,
                                              const InspectOptions& inspectOptions,
                                              const std::size_t maximumBytes) {
    auto* document = get(id);
    if (document == nullptr || document->path().empty()) {
        return std::make_error_code(std::errc::invalid_argument);
    }

    const auto path = document->path();
    const auto inspection = FileSniffer::inspect(path, inspectOptions);
    if (!inspection) {
        return inspection.error;
    }

    if (inspection.profile.recommendedMode == OpenMode::Editor) {
        return document->loadInspected(path, inspection.profile, maximumBytes);
    }
    document->attachReference(path, inspection.profile);
    return {};
}

std::error_code DocumentManager::reloadAsEncoding(
    const DocumentId id,
    const encoding::Encoding sourceEncoding,
    const std::size_t maximumBytes) {
    auto* document = get(id);
    if (document == nullptr || document->path().empty()) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    if (document->modified()) {
        return std::make_error_code(std::errc::text_file_busy);
    }
    if (sourceEncoding == encoding::Encoding::Unknown8Bit ||
        sourceEncoding == encoding::Encoding::Binary ||
        sourceEncoding == encoding::Encoding::Ascii) {
        return std::make_error_code(std::errc::invalid_argument);
    }

    const auto path = document->path();
    const auto inspection = FileSniffer::inspect(path);
    if (!inspection) {
        return inspection.error;
    }

    const auto& detected = inspection.profile.encoding;
    const bool hasMatchingBom = detected.hasBom && detected.encoding == sourceEncoding;
    if (inspection.profile.fileSize > static_cast<std::uintmax_t>(maximumBytes)) {
        auto profile = inspection.profile;
        profile.encoding = {sourceEncoding, hasMatchingBom,
                            sourceEncoding == encoding::Encoding::Utf8, false};
        profile.recommendedMode = OpenMode::Viewer;
        document->attachReference(path, profile);
        return {};
    }

    return document->loadAs(path, sourceEncoding, hasMatchingBom, maximumBytes);
}

std::error_code DocumentManager::materializeForEdit(
    const DocumentId id,
    const std::size_t maximumBytes) {
    auto* document = get(id);
    if (document == nullptr || document->path().empty()) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    if (document->profile().encoding.binaryLike) {
        return std::make_error_code(std::errc::operation_not_supported);
    }
    if (document->textBufferLoaded()) {
        return {};
    }

    const auto sourceEncoding = document->profile().encoding.encoding;
    if (sourceEncoding == encoding::Encoding::Unknown8Bit) {
        return std::make_error_code(std::errc::operation_not_supported);
    }
    return document->loadAs(document->path(),
                            sourceEncoding,
                            document->profile().encoding.hasBom,
                            maximumBytes);
}

std::error_code DocumentManager::dematerializeForViewer(
    const DocumentId id,
    const InspectOptions& inspectOptions,
    const bool allowDiscardModified) {
    auto* document = get(id);
    if (document == nullptr || document->path().empty()) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    if (document->modified() && !allowDiscardModified) {
        return std::make_error_code(std::errc::text_file_busy);
    }

    const auto inspection = FileSniffer::inspect(document->path(), inspectOptions);
    if (!inspection) {
        return inspection.error;
    }
    if (inspection.profile.encoding.binaryLike ||
        inspection.profile.encoding.encoding == encoding::Encoding::Unknown8Bit) {
        return std::make_error_code(std::errc::operation_not_supported);
    }

    auto profile = inspection.profile;
    profile.recommendedMode = OpenMode::Viewer;
    document->attachReference(document->path(), profile);
    return {};
}

Document* DocumentManager::get(const DocumentId id) noexcept {
    const auto iterator = documents_.find(id);
    return iterator == documents_.end() ? nullptr : iterator->second.get();
}

const Document* DocumentManager::get(const DocumentId id) const noexcept {
    const auto iterator = documents_.find(id);
    return iterator == documents_.end() ? nullptr : iterator->second.get();
}

std::optional<DocumentId> DocumentManager::findByPath(const std::filesystem::path& path) const {
    if (path.empty()) {
        return std::nullopt;
    }

    for (const auto& [id, document] : documents_) {
        if (!document->path().empty() && pathsReferToSameFile(document->path(), path)) {
            return id;
        }
    }
    return std::nullopt;
}

bool DocumentManager::close(const DocumentId id) noexcept {
    return documents_.erase(id) != 0;
}

std::size_t DocumentManager::size() const noexcept {
    return documents_.size();
}

bool DocumentManager::empty() const noexcept {
    return documents_.empty();
}

std::vector<DocumentId> DocumentManager::ids() const {
    std::vector<DocumentId> result;
    result.reserve(documents_.size());
    for (const auto& [id, document] : documents_) {
        static_cast<void>(document);
        result.push_back(id);
    }
    return result;
}

DocumentId DocumentManager::allocateId() noexcept {
    return DocumentId{nextId_++};
}

bool DocumentManager::pathsReferToSameFile(const std::filesystem::path& left,
                                           const std::filesystem::path& right) {
    std::error_code error;
    if (std::filesystem::equivalent(left, right, error) && !error) {
        return true;
    }

    error.clear();
    auto normalizedLeft = std::filesystem::weakly_canonical(left, error);
    if (error) {
        error.clear();
        normalizedLeft = std::filesystem::absolute(left, error).lexically_normal();
        if (error) {
            normalizedLeft = left.lexically_normal();
        }
    }

    error.clear();
    auto normalizedRight = std::filesystem::weakly_canonical(right, error);
    if (error) {
        error.clear();
        normalizedRight = std::filesystem::absolute(right, error).lexically_normal();
        if (error) {
            normalizedRight = right.lexically_normal();
        }
    }

    return normalizedLeft == normalizedRight;
}

}
