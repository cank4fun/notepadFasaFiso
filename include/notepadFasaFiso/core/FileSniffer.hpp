#pragma once

#include "notepadFasaFiso/core/TextAnalysis.hpp"
#include "notepadFasaFiso/encoding/EncodingDetector.hpp"
#include "notepadFasaFiso/formats/FormatDetector.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <system_error>

namespace nff::core {

enum class OpenMode : std::uint8_t {
    Editor,
    Viewer,
    BinaryPreview
};

struct InspectOptions {
    std::uintmax_t viewerSizeThreshold{64ULL * 1024ULL * 1024ULL};
    std::size_t longLineThreshold{1024ULL * 1024ULL};
    std::size_t sampleBytes{4ULL * 1024ULL * 1024ULL};
};

struct DocumentProfile {
    std::filesystem::path path;
    std::uintmax_t fileSize{0};
    encoding::DetectionResult encoding{};
    formats::DetectionResult format{};
    LineEnding lineEnding{LineEnding::Unknown};
    std::size_t longestSampledLine{0};
    bool sampledEntireFile{false};
    OpenMode recommendedMode{OpenMode::Editor};
};

struct InspectResult {
    DocumentProfile profile{};
    std::error_code error{};

    [[nodiscard]] explicit operator bool() const noexcept { return !error; }
};

class FileSniffer final {
public:
    [[nodiscard]] static InspectResult inspect(const std::filesystem::path& path,
                                               const InspectOptions& options = {});
};

}
