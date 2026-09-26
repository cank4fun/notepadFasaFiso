#include "notepadFasaFiso/core/FileSniffer.hpp"

#include "notepadFasaFiso/encoding/TextCodec.hpp"
#include "notepadFasaFiso/formats/FormatDetector.hpp"
#include "notepadFasaFiso/storage/FileReader.hpp"

#include <algorithm>
#include <cstddef>
#include <string>

namespace nff::core {

InspectResult FileSniffer::inspect(const std::filesystem::path& path,
                                         const InspectOptions& options) {
    InspectResult result;
    result.profile.path = path;

    std::error_code error;
    result.profile.fileSize = std::filesystem::file_size(path, error);
    if (error) {
        result.error = error;
        return result;
    }

    auto requestedSample = static_cast<std::uintmax_t>(options.sampleBytes);
    if (result.profile.fileSize >= options.viewerSizeThreshold) {
        constexpr std::size_t minimumLargeFileSample = 256U * 1024U;
        const auto usefulLargeFileSample =
            std::max(minimumLargeFileSample, options.longLineThreshold);
        requestedSample = std::min<std::uintmax_t>(
            requestedSample, static_cast<std::uintmax_t>(usefulLargeFileSample));
    }
    const auto sampleSize =
        static_cast<std::size_t>(std::min(result.profile.fileSize, requestedSample));

    auto sample = storage::FileReader::readPrefix(path, sampleSize);
    if (!sample) {
        result.error = sample.error;
        return result;
    }

    result.profile.sampledEntireFile = result.profile.fileSize <= requestedSample;
    result.profile.encoding = encoding::EncodingDetector::detect(sample.bytes);

    TextStatistics statistics;
    std::string decodedSample;
    bool sampleDecodable = true;
    if (result.profile.encoding.encoding == encoding::Encoding::Unknown8Bit) {
        sampleDecodable = false;
        statistics = TextAnalysis::analyzeAsciiCompatible(sample.bytes);
    } else if (!result.profile.encoding.binaryLike) {
        encoding::DecodeOptions decodeOptions;
        decodeOptions.allowTruncatedTail = !result.profile.sampledEntireFile;
        const auto decoded =
            encoding::TextCodec::decode(sample.bytes, result.profile.encoding, decodeOptions);
        if (decoded) {
            decodedSample = decoded.text;
            statistics = TextAnalysis::analyzeUtf8(decodedSample);
        } else {
            sampleDecodable = false;
        }
    }

    if (!result.profile.encoding.binaryLike && sampleDecodable) {
        result.profile.format = formats::FormatDetector::detect(
            path, decodedSample, result.profile.sampledEntireFile);
    }

    result.profile.lineEnding = statistics.lineEnding;
    result.profile.longestSampledLine = statistics.longestLineBytes;

    if (result.profile.encoding.binaryLike || !sampleDecodable) {
        result.profile.recommendedMode = OpenMode::BinaryPreview;
    } else if (result.profile.fileSize >= options.viewerSizeThreshold ||
               result.profile.longestSampledLine >= options.longLineThreshold) {
        result.profile.recommendedMode = OpenMode::Viewer;
    } else {
        result.profile.recommendedMode = OpenMode::Editor;
    }

    return result;
}

}
