#include "notepadFasaFiso/app/LaunchRequest.hpp"
#include "notepadFasaFiso/app/LaunchRouter.hpp"
#include "notepadFasaFiso/core/FileSniffer.hpp"
#include "notepadFasaFiso/storage/FileReader.hpp"
#include "notepadFasaFiso/storage/FileWriter.hpp"
#include "notepadFasaFiso/platform/Platform.hpp"

#include <cstddef>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] std::vector<std::byte> bytes(const std::string_view text) {
    const auto* begin = reinterpret_cast<const std::byte*>(text.data());
    return {begin, begin + text.size()};
}

[[nodiscard]] std::filesystem::path freshTempDirectory(const std::string_view name) {
    const auto root = std::filesystem::temp_directory_path() / std::string(name);
    std::error_code error;
    std::filesystem::remove_all(root, error);
    error.clear();
    std::filesystem::create_directories(root, error);
    expect(!error, "create temporary app test directory");
    return root;
}

[[nodiscard]] nff::app::LaunchParseResult parse(
    const std::initializer_list<std::string_view> arguments) {
    return nff::app::LaunchRequestParser::parse(
        std::span<const std::string_view>(arguments.begin(), arguments.size()));
}

void testParser() {
    const auto parsed = parse({"--view", "alpha.txt:42:7", "--edit", "+9:3", "beta.log"});
    expect(static_cast<bool>(parsed), "parse multiple routed files");
    if (!parsed) {
        return;
    }

    expect(parsed.request.targets.size() == 2U, "two launch targets");
    if (parsed.request.targets.size() != 2U) {
        return;
    }

    const auto& first = parsed.request.targets[0];
    expect(first.path == std::filesystem::path("alpha.txt"), "file:line:column path split");
    expect(first.position.hasLine && first.position.line == 42U, "file suffix line");
    expect(first.position.hasColumn && first.position.column == 7U, "file suffix column");
    expect(first.modePreference == nff::app::OpenModePreference::Viewer,
           "mode preference applies to target");

    const auto& second = parsed.request.targets[1];
    expect(second.path == std::filesystem::path("beta.log"), "+line target path");
    expect(second.position.hasLine && second.position.line == 9U, "+line parsed");
    expect(second.position.hasColumn && second.position.column == 3U, "+column parsed");
    expect(second.modePreference == nff::app::OpenModePreference::Editor,
           "mode preference can change between targets");

    const auto windowsPath = parse({R"(C:\Work\notes.txt:123:4)"});
    expect(static_cast<bool>(windowsPath), "Windows path with location parse");
    if (windowsPath && !windowsPath.request.targets.empty()) {
        expect(windowsPath.request.targets.front().position.line == 123U,
               "Windows path line suffix");
        expect(windowsPath.request.targets.front().position.column == 4U,
               "Windows path column suffix");
    }

    const auto explicitLocation = parse({"--line", "12", "--column", "8", "notes.txt"});
    expect(static_cast<bool>(explicitLocation), "explicit line and column parse");
    if (explicitLocation && !explicitLocation.request.targets.empty()) {
        expect(explicitLocation.request.targets.front().position.line == 12U,
               "explicit line value");
        expect(explicitLocation.request.targets.front().position.column == 8U,
               "explicit column value");
    }

    expect(!parse({"--line", "0", "notes.txt"}), "line zero rejected");
    expect(!parse({"--column", "3", "notes.txt"}), "column without line rejected");
    expect(!parse({"-", "-"}), "stdin cannot be consumed twice");
    expect(!parse({"--wat", "notes.txt"}), "unknown option rejected");

    const auto dashedPath = parse({"--", "--literal-file"});
    expect(static_cast<bool>(dashedPath) && dashedPath.request.targets.size() == 1U,
           "option terminator accepts dashed path");

    const auto literalDash = parse({"--", "-"});
    expect(static_cast<bool>(literalDash) && literalDash.request.targets.size() == 1U,
           "option terminator treats dash as a file");
    if (literalDash && !literalDash.request.targets.empty()) {
        expect(literalDash.request.targets.front().inputKind == nff::app::LaunchInputKind::File,
               "literal dash classified as file");
    }
}

void testUnicodeSpaceAndUncParsingAudit() {
    constexpr std::u8string_view unicodeArgumentUtf8 =
        u8"folder with spaces/Türkçe not.txt:8:2";
    const std::string_view unicodeArgument{
        reinterpret_cast<const char*>(unicodeArgumentUtf8.data()), unicodeArgumentUtf8.size()};
    const auto unicode = parse({unicodeArgument});
    expect(static_cast<bool>(unicode) && unicode.request.targets.size() == 1U,
           "Unicode path with spaces and location parses as one target");
    if (unicode && !unicode.request.targets.empty()) {
        expect(unicode.request.targets.front().position.line == 8U &&
                   unicode.request.targets.front().position.column == 2U,
               "Unicode path preserves line and column suffix");
        expect(unicode.request.targets.front().path.filename().generic_u8string() ==
                   std::u8string_view{u8"Türkçe not.txt"},
               "Unicode filename bytes survive parser conversion");
    }

    const auto unc = parse({R"(\\server\share\notes with spaces.txt:5:1)"});
    expect(static_cast<bool>(unc) && unc.request.targets.size() == 1U,
           "UNC-looking path with spaces parses without option confusion");
    if (unc && !unc.request.targets.empty()) {
        expect(unc.request.targets.front().position.line == 5U &&
                   unc.request.targets.front().position.column == 1U,
               "UNC-looking path preserves line and column suffix");
    }
}

void testPlatformCommandLine() {
    char program[] = "notepadFasaFiso";
    char first[] = "one.txt";
    char second[] = "two.txt:9:2";
    char* arguments[] = {program, first, second};
    const auto result = nff::platform::commandLineArguments(3, arguments);
    expect(static_cast<bool>(result), "platform command line extraction");
    if (result) {
        expect(result.arguments.size() == 2U, "platform command line excludes executable");
        if (result.arguments.size() == 2U) {
            expect(result.arguments[0] == "one.txt", "platform first argument preserved");
            expect(result.arguments[1] == "two.txt:9:2", "platform second argument preserved");
        }
    }
}

void testExistingAndNewFileRouting() {
    const auto root = freshTempDirectory("nff-launch-routing-tests");
    const auto existing = root / "small.txt";
    auto error = nff::storage::FileWriter::writeAtomically(existing, bytes("one\ntwo\n"));
    expect(!error, "write routing fixture");

    nff::app::LaunchRequest request;
    request.targets.push_back({nff::app::LaunchInputKind::File,
                               existing,
                               {},
                               nff::app::OpenModePreference::Automatic});
    request.targets.push_back({nff::app::LaunchInputKind::File,
                               root / "new-file.anything",
                               {},
                               nff::app::OpenModePreference::Viewer});

    std::istringstream unused;
    auto plan = nff::app::LaunchRouter::route(request, unused);
    expect(!plan.hasErrors(), "existing and new file routing succeeds");
    expect(plan.targets().size() == 2U, "two routed targets");
    if (plan.targets().size() == 2U) {
        expect(plan.targets()[0].inputKind == nff::app::RoutedInputKind::ExistingFile,
               "existing file classified");
        expect(plan.targets()[0].profile.has_value(), "existing file inspected");
        expect(plan.targets()[0].openMode == nff::core::OpenMode::Editor,
               "small text defaults to editor");

        expect(plan.targets()[1].inputKind == nff::app::RoutedInputKind::NewFile,
               "missing path classified as new file");
        expect(!plan.targets()[1].profile.has_value(), "new file has no disk profile yet");
        expect(plan.targets()[1].openMode == nff::core::OpenMode::Editor,
               "new file always starts editable");
    }

    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
}

void testModeOverridesAndDirectoryError() {
    const auto root = freshTempDirectory("nff-launch-mode-tests");
    const auto textPath = root / "text.log";
    const auto binaryPath = root / "binary.dat";
    auto error = nff::storage::FileWriter::writeAtomically(textPath, bytes("a\nb\nc\n"));
    expect(!error, "write text mode fixture");
    const std::vector<std::byte> binary{std::byte{'A'}, std::byte{0x00}, std::byte{0x01}};
    error = nff::storage::FileWriter::writeAtomically(binaryPath, binary);
    expect(!error, "write binary mode fixture");

    nff::app::LaunchRequest request;
    request.targets.push_back({nff::app::LaunchInputKind::File,
                               textPath,
                               {},
                               nff::app::OpenModePreference::Viewer});
    request.targets.push_back({nff::app::LaunchInputKind::File,
                               binaryPath,
                               {},
                               nff::app::OpenModePreference::Editor});
    request.targets.push_back({nff::app::LaunchInputKind::File,
                               root,
                               {},
                               nff::app::OpenModePreference::Automatic});

    std::istringstream unused;
    auto plan = nff::app::LaunchRouter::route(request, unused);
    expect(plan.targets().size() == 3U, "mode test target count");
    if (plan.targets().size() == 3U) {
        expect(plan.targets()[0].openMode == nff::core::OpenMode::Viewer,
               "viewer override honored");
        expect(plan.targets()[1].openMode == nff::core::OpenMode::BinaryPreview,
               "editor override cannot force binary into text editor");
        expect(static_cast<bool>(plan.targets()[2].error), "directory routing rejected");
    }

    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
}

void testAutomaticLargeFileRouting() {
    const auto root = freshTempDirectory("nff-launch-large-tests");
    const auto path = root / "large.log";
    auto error = nff::storage::FileWriter::writeAtomically(path, bytes("1234567890\n"));
    expect(!error, "write large routing fixture");

    nff::app::LaunchRequest request;
    request.targets.push_back({nff::app::LaunchInputKind::File,
                               path,
                               {},
                               nff::app::OpenModePreference::Automatic});

    nff::app::LaunchRouteOptions options;
    options.inspect.viewerSizeThreshold = 4U;
    std::istringstream unused;
    auto plan = nff::app::LaunchRouter::route(request, unused, options);
    expect(!plan.hasErrors(), "automatic large routing succeeds");
    if (!plan.targets().empty()) {
        expect(plan.targets().front().openMode == nff::core::OpenMode::Viewer,
               "size threshold routes to viewer");
    }

    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
}

void testStandardInputSpoolingAndLifetime() {
    std::filesystem::path temporaryPath;
    {
        nff::app::LaunchRequest request;
        nff::app::TextPosition position;
        position.line = 2U;
        position.column = 3U;
        position.hasLine = true;
        position.hasColumn = true;
        request.targets.push_back({nff::app::LaunchInputKind::StandardInput,
                                   {},
                                   position,
                                   nff::app::OpenModePreference::Automatic});

        std::istringstream input("hello\nfrom stdin\n");
        nff::app::LaunchRouteOptions options;
        options.stdinChunkBytes = 3U;
        auto plan = nff::app::LaunchRouter::route(request, input, options);
        expect(!plan.hasErrors(), "stdin routing succeeds");
        expect(plan.targets().size() == 1U, "stdin target count");
        if (plan.targets().empty()) {
            return;
        }

        const auto& target = plan.targets().front();
        expect(target.inputKind == nff::app::RoutedInputKind::StandardInput,
               "stdin target classified");
        expect(target.temporary, "stdin target backed by temporary file");
        expect(target.profile.has_value(), "stdin spool inspected");
        expect(target.requested.position.line == 2U && target.requested.position.column == 3U,
               "stdin target preserves requested location");

        temporaryPath = target.backingPath;
        expect(std::filesystem::exists(temporaryPath), "stdin backing file lives with plan");
        const auto read = nff::storage::FileReader::readAll(temporaryPath, 1024U);
        expect(static_cast<bool>(read), "read stdin backing file");
        if (read) {
            const std::string text(reinterpret_cast<const char*>(read.bytes.data()), read.bytes.size());
            expect(text == "hello\nfrom stdin\n", "stdin bytes preserved exactly");
        }
    }

    expect(!temporaryPath.empty() && !std::filesystem::exists(temporaryPath),
           "stdin backing file removed with launch plan");
}

}

int main() {
    testParser();
    testUnicodeSpaceAndUncParsingAudit();
    testPlatformCommandLine();
    testExistingAndNewFileRouting();
    testModeOverridesAndDirectoryError();
    testAutomaticLargeFileRouting();
    testStandardInputSpoolingAndLifetime();

    if (failures != 0) {
        std::cerr << failures << " app test(s) failed\n";
        return 1;
    }
    return 0;
}
