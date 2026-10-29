#include "engine_encryption.hpp"

#include <array>
#include <fstream>

namespace cardwirth::platform {
namespace {

constexpr std::array<char, 8> kVaultV1Header{'C', 'W', 'V', 'H', '0', '0', '0', '1'};
constexpr std::array<char, 8> kDirectV2Header{'C', 'W', 'U', 'S', '0', '0', '0', '2'};

bool has_magic(const std::filesystem::path& path,
               const std::array<char, 8>& expected) noexcept {
    try {
        std::ifstream input(path, std::ios::binary);
        std::array<char, 8> actual{};
        return input.read(actual.data(), static_cast<std::streamsize>(actual.size())) &&
               actual == expected;
    } catch (...) {
        return false;
    }
}

}  // namespace

EncryptionAvailability engine_encryption_availability() noexcept {
    return EncryptionAvailability::NotCompiled;
}

bool engine_encryption_compiled() noexcept {
    return false;
}

bool looks_like_encrypted_user_storage(const std::filesystem::path& path) noexcept {
    try {
        if (std::filesystem::is_directory(path))
            return has_magic(path / "vault-header.bin", kVaultV1Header);
        return has_magic(path, kDirectV2Header);
    } catch (...) {
        return false;
    }
}

std::filesystem::path default_encrypted_user_storage_path(
    const std::filesystem::path& application_directory) {
    return application_directory / "UserData.data";
}

EngineUserStorageResult open_engine_user_storage(
    const EngineUserStorageRequest& request) {
    if (request.mode == UserStorageMode::Encrypted) {
        return {{}, EngineEncryptionError::EncryptionNotCompiled,
                "Encrypted UserData support is not compiled into this Engine"};
    }
    if (looks_like_encrypted_user_storage(request.root)) {
        return {{}, EngineEncryptionError::EncryptionNotCompiled,
                "The selected UserData is encrypted, but encryption support is not compiled"};
    }
    try {
        auto storage = make_native_storage(request.root, request.create_plain_root);
        if (!storage) {
            return {{}, EngineEncryptionError::OpenFailed,
                    "Failed to create the native UserData storage"};
        }
        return {std::move(storage), EngineEncryptionError::None, {}};
    } catch (const std::exception& error) {
        return {{}, EngineEncryptionError::OpenFailed, error.what()};
    } catch (...) {
        return {{}, EngineEncryptionError::OpenFailed,
                "Unknown error while opening native UserData storage"};
    }
}

}  // namespace cardwirth::platform
