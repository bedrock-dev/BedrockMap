#include "leveldbxor.h"

#include <array>
#include <filesystem>
#include <fstream>

#include "config.h"

namespace {

    constexpr std::array<unsigned char, 4> kXorMagic{0x80, 0x1d, 0x30, 0x01};

    [[nodiscard]] bool hasXorMagic(const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) return false;

        std::array<unsigned char, kXorMagic.size()> header{};
        file.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
        if (file.gcount() != static_cast<std::streamsize>(header.size())) return false;
        return header == kXorMagic;
    }

}  // namespace

namespace leveldb_xor {

    bool isEncrypted(const std::string& world_root) {
        namespace fs = std::filesystem;
        const fs::path db_path = fs::path(world_root) / "db";
        std::error_code error;
        if (!fs::is_directory(db_path, error)) return false;

        for (fs::directory_iterator it(db_path, error), end; !error && it != end; it.increment(error)) {
            const auto& entry = *it;
            std::error_code entry_error;
            if (!entry.is_regular_file(entry_error) || entry_error) continue;
            // The mcne format deliberately leaves log files unencrypted.
            if (entry.path().extension() == ".log") continue;
            if (hasXorMagic(entry.path())) return true;
        }
        return false;
    }

    std::string keyForLevel(const std::string& world_root) {
        const auto& settings = setting::current();
        switch (settings.LEVELDB_XOR_MODE) {
            case 0:  // No
                return {};
            case 1:  // Yes
                return settings.LEVELDB_XOR_KEY.toStdString();
            case 2:  // Auto
            default:
                return isEncrypted(world_root) ? settings.LEVELDB_XOR_KEY.toStdString() : std::string{};
        }
    }

}  // namespace leveldb_xor
