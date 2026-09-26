#include "notepadFasaFiso/app/LaunchRequest.hpp"
#include "notepadFasaFiso/app/LaunchRouter.hpp"
#include "notepadFasaFiso/encoding/EncodingDetector.hpp"
#include "notepadFasaFiso/platform/Platform.hpp"

#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace {

[[nodiscard]] std::string_view modeName(const nff::core::OpenMode mode) noexcept {
    switch (mode) {
    case nff::core::OpenMode::Editor:
        return "editor";
    case nff::core::OpenMode::Viewer:
        return "viewer";
    case nff::core::OpenMode::BinaryPreview:
        return "binary-preview";
    }
    return "unknown";
}

[[nodiscard]] std::string_view inputName(const nff::app::RoutedInputKind kind) noexcept {
    switch (kind) {
    case nff::app::RoutedInputKind::ExistingFile:
        return "file";
    case nff::app::RoutedInputKind::NewFile:
        return "new-file";
    case nff::app::RoutedInputKind::StandardInput:
        return "stdin";
    }
    return "unknown";
}

}

int main(const int argc, char** argv) {
    const auto nativeArguments = nff::platform::nativeCommandLineArguments(argc, argv);
    if (!nativeArguments) {
        std::cerr << "notepadFasaFiso: " << nativeArguments.error.message() << '\n';
        return 2;
    }

    std::vector<std::string_view> arguments;
    arguments.reserve(nativeArguments.arguments.size());
    for (const auto& argument : nativeArguments.arguments) {
        arguments.emplace_back(argument);
    }

    const auto parsed = nff::app::LaunchRequestParser::parse(arguments);
    if (!parsed) {
        std::cerr << "notepadFasaFiso: " << parsed.error << '\n';
        return 2;
    }
    if (parsed.request.showHelp) {
        std::cout << nff::app::LaunchRequestParser::usage();
        return 0;
    }
    if (parsed.request.targets.empty()) {
        return 0;
    }

    auto plan = nff::app::LaunchRouter::route(parsed.request, std::cin);
    for (const auto& target : plan.targets()) {
        if (!target) {
            std::cerr << target.backingPath.string() << ": " << target.error.message() << '\n';
            continue;
        }

        std::cout << "input=" << inputName(target.inputKind)
                  << " mode=" << modeName(target.openMode);
        if (!target.backingPath.empty()) {
            std::cout << " path=" << target.backingPath.string();
        }
        if (target.requested.position.hasLine) {
            std::cout << " line=" << target.requested.position.line;
        }
        if (target.requested.position.hasColumn) {
            std::cout << " column=" << target.requested.position.column;
        }
        if (target.profile) {
            std::cout << " encoding="
                      << nff::encoding::EncodingDetector::name(target.profile->encoding.encoding);
        }
        std::cout << '\n';
    }

    return plan.hasErrors() ? 1 : 0;
}
