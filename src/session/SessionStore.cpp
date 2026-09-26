#include "notepadFasaFiso/session/SessionStore.hpp"

#include "notepadFasaFiso/persistence/BinaryCodec.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <set>
#include <utility>

namespace nff::session {
namespace {

constexpr persistence::Magic magic{
    std::byte{'N'}, std::byte{'F'}, std::byte{'F'}, std::byte{'S'},
    std::byte{'E'}, std::byte{'S'}, std::byte{'0'}, std::byte{'1'}};
constexpr std::size_t maximumPathBytes = 1024U * 1024U;
constexpr std::uint64_t maximumDocuments = 1ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t maximumPanes = 1ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t maximumViews = 4ULL * 1024ULL * 1024ULL;
constexpr std::size_t maximumLayoutDepth = 256U;
constexpr std::size_t maximumFontNameBytes = 16U * 1024U;
constexpr std::size_t minimumDocumentRecordBytes = 8U + 8U + 8U + 1U + 1U;
constexpr std::size_t minimumPaneRecordBytes = 8U + 8U + 1U;
constexpr std::size_t minimumViewRecordBytes = 8U * 5U;

[[nodiscard]] bool countFitsRemaining(const std::uint64_t count,
                                      const std::size_t remaining,
                                      const std::size_t minimumRecordBytes) noexcept {
    return minimumRecordBytes != 0U &&
           count <= static_cast<std::uint64_t>(remaining / minimumRecordBytes);
}

template <typename Enum>
[[nodiscard]] bool enumWithin(const std::uint8_t value, const Enum maximum) noexcept {
    return value <= static_cast<std::uint8_t>(maximum);
}

void writeLayoutNode(persistence::BinaryWriter& writer,
                     const workspace::WorkspaceSnapshotNode& node) {
    writer.writeU8(static_cast<std::uint8_t>(node.kind));
    if (node.kind == workspace::WorkspaceNode::Kind::Pane) {
        writer.writeU64(node.pane.value);
        return;
    }
    writer.writeU64(node.split.value);
    writer.writeU8(static_cast<std::uint8_t>(node.orientation));
    writer.writeDouble(node.ratio);
    writeLayoutNode(writer, *node.first);
    writeLayoutNode(writer, *node.second);
}

[[nodiscard]] std::unique_ptr<workspace::WorkspaceSnapshotNode> readLayoutNode(
    persistence::BinaryReader& reader,
    const std::size_t depth,
    std::uint64_t& nodeCount) {
    if (depth > maximumLayoutDepth || nodeCount >= maximumPanes * 2U) {
        return {};
    }
    ++nodeCount;

    std::uint8_t kindValue = 0;
    if (!reader.readU8(kindValue) ||
        !enumWithin(kindValue, workspace::WorkspaceNode::Kind::Split)) {
        return {};
    }

    auto node = std::make_unique<workspace::WorkspaceSnapshotNode>();
    node->kind = static_cast<workspace::WorkspaceNode::Kind>(kindValue);
    if (node->kind == workspace::WorkspaceNode::Kind::Pane) {
        if (!reader.readU64(node->pane.value) || !node->pane) {
            return {};
        }
        return node;
    }

    std::uint8_t orientation = 0;
    if (!reader.readU64(node->split.value) || !node->split || !reader.readU8(orientation) ||
        !enumWithin(orientation, workspace::SplitOrientation::Vertical) ||
        !reader.readDouble(node->ratio)) {
        return {};
    }
    node->orientation = static_cast<workspace::SplitOrientation>(orientation);
    node->first = readLayoutNode(reader, depth + 1U, nodeCount);
    node->second = readLayoutNode(reader, depth + 1U, nodeCount);
    if (!node->first || !node->second) {
        return {};
    }
    return node;
}

[[nodiscard]] bool stateDocumentsMatchWorkspace(const SessionState& state) {
    std::set<core::DocumentId> documents;
    for (const auto& document : state.documents) {
        if (!document.id || !documents.insert(document.id).second ||
            document.untitled != document.path.empty()) {
            return false;
        }
    }
    for (const auto& view : state.workspace.views) {
        if (!documents.contains(view.document)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool readViewPreferences(persistence::BinaryReader& reader,
                                       workspace::ViewState& view) {
    bool hasWordWrap = false;
    bool hasLineNumbers = false;
    bool hasFontFamily = false;
    bool hasFontPointSize = false;
    bool boolValue = false;
    std::string fontFamily;
    double fontPointSize = 0.0;

    if (!reader.readBool(hasWordWrap)) {
        return false;
    }
    if (hasWordWrap) {
        if (!reader.readBool(boolValue)) {
            return false;
        }
        view.wordWrapOverride = boolValue;
    }

    if (!reader.readBool(hasLineNumbers)) {
        return false;
    }
    if (hasLineNumbers) {
        if (!reader.readBool(boolValue)) {
            return false;
        }
        view.lineNumbersOverride = boolValue;
    }

    if (!reader.readBool(hasFontFamily)) {
        return false;
    }
    if (hasFontFamily) {
        if (!reader.readString(fontFamily, maximumFontNameBytes)) {
            return false;
        }
        view.fontFamilyOverride = std::move(fontFamily);
    }

    if (!reader.readBool(hasFontPointSize)) {
        return false;
    }
    if (hasFontPointSize) {
        if (!reader.readDouble(fontPointSize)) {
            return false;
        }
        view.fontPointSizeOverride = fontPointSize;
    }
    return true;
}

void writeViewPreferences(persistence::BinaryWriter& writer,
                          const workspace::ViewState& view) {
    writer.writeBool(view.wordWrapOverride.has_value());
    if (view.wordWrapOverride) {
        writer.writeBool(*view.wordWrapOverride);
    }
    writer.writeBool(view.lineNumbersOverride.has_value());
    if (view.lineNumbersOverride) {
        writer.writeBool(*view.lineNumbersOverride);
    }
    writer.writeBool(view.fontFamilyOverride.has_value());
    if (view.fontFamilyOverride) {
        writer.writeString(*view.fontFamilyOverride);
    }
    writer.writeBool(view.fontPointSizeOverride.has_value());
    if (view.fontPointSizeOverride) {
        writer.writeDouble(*view.fontPointSizeOverride);
    }
}

[[nodiscard]] SessionLoadResult loadVersion(const std::span<const std::byte> payload,
                                            const bool hasViewPreferences) {
    persistence::BinaryReader reader(payload);
    SessionState state;
    std::uint64_t documentCount = 0;
    if (!reader.readU64(documentCount) || documentCount > maximumDocuments) {
        return {{}, std::make_error_code(std::errc::illegal_byte_sequence)};
    }
    if (!countFitsRemaining(documentCount, reader.remaining(), minimumDocumentRecordBytes)) {
        return {{}, std::make_error_code(std::errc::value_too_large)};
    }

    state.documents.reserve(static_cast<std::size_t>(documentCount));
    for (std::uint64_t index = 0; index < documentCount; ++index) {
        SessionDocumentState document;
        if (!reader.readU64(document.id.value) || !document.id ||
            !reader.readPath(document.path, maximumPathBytes) ||
            !reader.readPath(document.recoverySnapshot, maximumPathBytes) ||
            !reader.readBool(document.modified) || !reader.readBool(document.untitled)) {
            return {{}, std::make_error_code(std::errc::illegal_byte_sequence)};
        }
        state.documents.push_back(std::move(document));
    }

    auto& snapshot = state.workspace;
    std::uint64_t paneCount = 0;
    std::uint64_t viewCount = 0;
    if (!reader.readU64(snapshot.primaryPane.value) || !reader.readU64(snapshot.activePane.value) ||
        !reader.readU64(snapshot.nextPaneId) || !reader.readU64(snapshot.nextViewId) ||
        !reader.readU64(snapshot.nextSplitId) || !reader.readU64(paneCount) ||
        paneCount == 0U || paneCount > maximumPanes) {
        return {{}, std::make_error_code(std::errc::illegal_byte_sequence)};
    }
    if (!countFitsRemaining(paneCount, reader.remaining(), minimumPaneRecordBytes)) {
        return {{}, std::make_error_code(std::errc::value_too_large)};
    }

    snapshot.panes.reserve(static_cast<std::size_t>(paneCount));
    for (std::uint64_t paneIndex = 0; paneIndex < paneCount; ++paneIndex) {
        workspace::PaneState pane;
        std::uint64_t paneViewCount = 0;
        bool hasActive = false;
        if (!reader.readU64(pane.id.value) || !pane.id || !reader.readU64(paneViewCount) ||
            paneViewCount > maximumViews) {
            return {{}, std::make_error_code(std::errc::illegal_byte_sequence)};
        }
        if (!countFitsRemaining(paneViewCount, reader.remaining(), sizeof(std::uint64_t))) {
            return {{}, std::make_error_code(std::errc::value_too_large)};
        }
        pane.views.reserve(static_cast<std::size_t>(paneViewCount));
        for (std::uint64_t viewIndex = 0; viewIndex < paneViewCount; ++viewIndex) {
            workspace::ViewId view;
            if (!reader.readU64(view.value) || !view) {
                return {{}, std::make_error_code(std::errc::illegal_byte_sequence)};
            }
            pane.views.push_back(view);
        }
        if (!reader.readBool(hasActive)) {
            return {{}, std::make_error_code(std::errc::illegal_byte_sequence)};
        }
        if (hasActive) {
            workspace::ViewId active;
            if (!reader.readU64(active.value) || !active) {
                return {{}, std::make_error_code(std::errc::illegal_byte_sequence)};
            }
            pane.activeView = active;
        }
        snapshot.panes.push_back(std::move(pane));
    }

    if (!reader.readU64(viewCount) || viewCount > maximumViews) {
        return {{}, std::make_error_code(std::errc::illegal_byte_sequence)};
    }
    if (!countFitsRemaining(viewCount, reader.remaining(), minimumViewRecordBytes)) {
        return {{}, std::make_error_code(std::errc::value_too_large)};
    }
    snapshot.views.reserve(static_cast<std::size_t>(viewCount));
    for (std::uint64_t index = 0; index < viewCount; ++index) {
        workspace::ViewState view;
        std::uint64_t caret = 0;
        std::uint64_t anchor = 0;
        std::uint64_t firstLine = 0;
        if (!reader.readU64(view.id.value) || !reader.readU64(view.document.value) ||
            !reader.readU64(caret) || !reader.readU64(anchor) || !reader.readU64(firstLine) ||
            !view.id || !view.document ||
            caret > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
            anchor > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
            firstLine > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
            return {{}, std::make_error_code(std::errc::illegal_byte_sequence)};
        }
        view.caretOffset = static_cast<std::size_t>(caret);
        view.anchorOffset = static_cast<std::size_t>(anchor);
        view.firstVisibleLine = static_cast<std::size_t>(firstLine);
        if (hasViewPreferences && !readViewPreferences(reader, view)) {
            return {{}, std::make_error_code(std::errc::illegal_byte_sequence)};
        }
        snapshot.views.push_back(view);
    }

    std::uint64_t nodeCount = 0;
    snapshot.root = readLayoutNode(reader, 0U, nodeCount);
    if (!snapshot.root || !reader.empty() || !stateDocumentsMatchWorkspace(state)) {
        return {{}, std::make_error_code(std::errc::illegal_byte_sequence)};
    }

    workspace::WorkspaceModel validator;
    if (!validator.restore(snapshot)) {
        return {{}, std::make_error_code(std::errc::illegal_byte_sequence)};
    }
    return {std::move(state), {}};
}

[[nodiscard]] SessionLoadResult loadV1(const std::span<const std::byte> payload) {
    return loadVersion(payload, false);
}

[[nodiscard]] SessionLoadResult loadV2(const std::span<const std::byte> payload) {
    return loadVersion(payload, true);
}

}

SessionState SessionStore::capture(const core::DocumentManager& documents,
                                   const workspace::WorkspaceModel& workspace,
                                   const recovery::RecoveryManager* recovery) {
    SessionState state;
    const auto ids = documents.ids();
    state.documents.reserve(ids.size());
    for (const auto id : ids) {
        const auto* document = documents.get(id);
        if (document == nullptr) {
            continue;
        }
        SessionDocumentState saved;
        saved.id = id;
        saved.path = document->path();
        saved.modified = document->modified();
        saved.untitled = document->path().empty();
        if (saved.modified && recovery != nullptr) {
            const auto candidate = recovery->snapshotPath(id);
            std::error_code error;
            if (std::filesystem::is_regular_file(candidate, error) && !error) {
                saved.recoverySnapshot = candidate;
            }
        }
        state.documents.push_back(std::move(saved));
    }
    state.workspace = workspace.snapshot();
    return state;
}

std::error_code SessionStore::save(const std::filesystem::path& path, const SessionState& state) {
    if (!stateDocumentsMatchWorkspace(state) || !state.workspace.root) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    workspace::WorkspaceModel validator;
    if (!validator.restore(state.workspace)) {
        return std::make_error_code(std::errc::invalid_argument);
    }

    persistence::BinaryWriter writer;
    writer.writeU64(static_cast<std::uint64_t>(state.documents.size()));
    for (const auto& document : state.documents) {
        writer.writeU64(document.id.value);
        writer.writePath(document.path);
        writer.writePath(document.recoverySnapshot);
        writer.writeBool(document.modified);
        writer.writeBool(document.untitled);
    }

    const auto& snapshot = state.workspace;
    writer.writeU64(snapshot.primaryPane.value);
    writer.writeU64(snapshot.activePane.value);
    writer.writeU64(snapshot.nextPaneId);
    writer.writeU64(snapshot.nextViewId);
    writer.writeU64(snapshot.nextSplitId);
    writer.writeU64(static_cast<std::uint64_t>(snapshot.panes.size()));
    for (const auto& pane : snapshot.panes) {
        writer.writeU64(pane.id.value);
        writer.writeU64(static_cast<std::uint64_t>(pane.views.size()));
        for (const auto view : pane.views) {
            writer.writeU64(view.value);
        }
        writer.writeBool(pane.activeView.has_value());
        if (pane.activeView) {
            writer.writeU64(pane.activeView->value);
        }
    }

    writer.writeU64(static_cast<std::uint64_t>(snapshot.views.size()));
    for (const auto& view : snapshot.views) {
        writer.writeU64(view.id.value);
        writer.writeU64(view.document.value);
        writer.writeU64(static_cast<std::uint64_t>(view.caretOffset));
        writer.writeU64(static_cast<std::uint64_t>(view.anchorOffset));
        writer.writeU64(static_cast<std::uint64_t>(view.firstVisibleLine));
        writeViewPreferences(writer, view);
    }
    writeLayoutNode(writer, *snapshot.root);
    return persistence::writeEnvelope(path, magic, currentSchemaVersion, writer.bytes());
}

SessionLoadResult SessionStore::load(const std::filesystem::path& path,
                                     const std::size_t maximumBytes) {
    const auto envelope = persistence::readEnvelope(path, magic, maximumBytes);
    if (!envelope) {
        return {{}, envelope.error};
    }
    if (envelope.schemaVersion > currentSchemaVersion) {
        return {{}, std::make_error_code(std::errc::protocol_not_supported)};
    }
    switch (envelope.schemaVersion) {
    case 1U:
        return loadV1(envelope.payload);
    case 2U:
        return loadV2(envelope.payload);
    default:
        return {{}, std::make_error_code(std::errc::protocol_not_supported)};
    }
}

}
