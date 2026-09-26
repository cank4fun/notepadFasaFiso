#pragma once

#include "notepadFasaFiso/encoding/EncodingDetector.hpp"

#include <cstdint>
#include <optional>

namespace nff::core {

enum class LineEndingPolicy : std::uint8_t {
    Preserve,
    LF,
    CRLF,
    CR
};

struct SaveOptions {
    std::optional<encoding::Encoding> encoding;
    std::optional<bool> writeBom;
    LineEndingPolicy lineEnding{LineEndingPolicy::Preserve};
    bool allowExternalOverwrite{false};
};

}
