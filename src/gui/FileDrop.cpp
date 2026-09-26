#include "notepadFasaFiso/gui/FileDrop.hpp"

#include <system_error>

namespace nff::gui {

FileDropSelection prepareFileDropPaths(
    const std::span<const std::filesystem::path> candidates) {
    FileDropSelection result;
    result.files.reserve(candidates.size());

    for (const auto& candidate : candidates) {
        if (candidate.empty()) {
            ++result.ignoredEntries;
            continue;
        }

        std::error_code error;
        const bool regular = std::filesystem::is_regular_file(candidate, error);
        if (error || !regular) {
            ++result.ignoredEntries;
            continue;
        }
        result.files.push_back(candidate.lexically_normal());
    }
    return result;
}

}
