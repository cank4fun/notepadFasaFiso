#pragma once

#include "notepadFasaFiso/core/SaveOptions.hpp"

#include <string>
#include <string_view>

namespace nff::core {

class TextTransform final {
public:
    [[nodiscard]] static std::string normalizeLineEndings(std::string_view text,
                                                          LineEndingPolicy policy);
};

}
