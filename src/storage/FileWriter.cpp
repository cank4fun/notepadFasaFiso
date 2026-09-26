#include "notepadFasaFiso/storage/FileWriter.hpp"

#include "notepadFasaFiso/storage/AtomicSave.hpp"

namespace nff::storage {

std::error_code FileWriter::writeAtomically(const std::filesystem::path& path,
                                            const std::span<const std::byte> bytes) {
    return AtomicSave::write(path, bytes);
}

}
