#pragma once

#include "storage.hpp"

#include <filesystem>
#include <memory>
#include <string>

namespace cardwirth::platform {

// この公開インターフェースは常に存在する。通常ビルドではスタブを
// リンクし、 暗号処理やlibsodiumへの依存を含めない。
enum class EncryptionAvailability {
    NotCompiled,
    Compiled,
};

enum class UserStorageMode {
    Plain,
    Encrypted,
};

enum class EngineEncryptionError {
    None,
    EncryptionNotCompiled,
    InvalidVault,
    KeyUnavailable,
    OpenFailed,
};

struct EngineUserStorageRequest {
    UserStorageMode mode{UserStorageMode::Plain};
    // Storage::root()から公開する論理ルート。平文モードでは
    // ネイティブの保存先にも使うが、 暗号化モードではこのパスを作らない。
    std::filesystem::path root{};
    // 物理的な暗号化コンテナー。空ならアプリの隣のUserData.data
    // を選ぶ。 通常は呼び出し元で既定値を明示的に解決する。
    std::filesystem::path encrypted_data{};
    // 空なら<encrypted_data>.keyを使う。鍵を分離して
    // おくことで、 コンテナーの受け渡し時に鍵まで暗黙に書き出さない。
    std::filesystem::path key_file{};
    bool create_plain_root{true};
};

struct EngineUserStorageResult {
    std::unique_ptr<Storage> storage{};
    EngineEncryptionError error{EngineEncryptionError::None};
    std::string message{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return storage != nullptr && error == EngineEncryptionError::None;
    }
};

[[nodiscard]] EncryptionAvailability engine_encryption_availability() noexcept;
[[nodiscard]] bool engine_encryption_compiled() noexcept;

// 識別では公開の形式マーカーだけを読む。 鍵の復
// 元は試みず、ファイルやディレクトリも作らない。
[[nodiscard]] bool looks_like_encrypted_user_storage(
    const std::filesystem::path& path) noexcept;

// UserData.dataは実行ファイル・アプリのディレクトリを基準
// に解決し、 変更され得るプロセスの作業ディレクトリを基準にしない。
[[nodiscard]] std::filesystem::path default_encrypted_user_storage_path(
    const std::filesystem::path& application_directory);

// 平文モードは既存のネイティブ実装を使う。暗号化が組み込まれてい
// なければ 必ずEncryptionNotCompiledを返し
// 、保管庫のパスを通常のディレクトリと解釈して処理を続けない。
[[nodiscard]] EngineUserStorageResult open_engine_user_storage(
    const EngineUserStorageRequest& request);

}  // namespace cardwirth::platform
