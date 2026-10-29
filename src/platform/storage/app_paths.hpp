#pragma once

#include "storage.hpp"

namespace cardwirth::platform {
// 共通の文字列上のパス規則。明示されたルートは作業ディレクトリを参照しない
// 。 下のネイティブ用ファクトリーが、ホストとの境界で既定値を解決する。
[[nodiscard]] StoragePath normalize_explicit_root(StoragePath root);
[[nodiscard]] StoragePath normalize_relative_path(StoragePath relative);
[[nodiscard]] bool path_contains(const StoragePath& root, const StoragePath& candidate) noexcept;
[[nodiscard]] StoragePath resolve_below_root(const StoragePath& root, StoragePath relative);

// ここでは名前だけを保持する。ルートがネイティブのディレクトリ、 Emscripten
// の/appや/user、仮想ツリーのいずれかはバックエンドが決める。
enum class LogicalRoot {
    App,
    User,
    Data,
    // 従来のEngineはUserData基準でDataを解決するが
    // 、正式な変更不可資産はapp_root/Dataにある。 移
    // 行処理が両者を混同しないよう、互換用コピーを別の名前で示す。
    UserData,
    Skin,
    Scenario,
    Yado,
    Config,
    Backup,
    Database,
    Temp,
    Cache,
    Trash,
};

struct AppPaths {
    StoragePath app_root;
    StoragePath user_root;
    StoragePath data_root;
    StoragePath user_data_root;
    StoragePath skin_root;
    StoragePath scenario_root;
    StoragePath yado_root;
    StoragePath config_root;
    StoragePath backup_root;
    StoragePath database_root;
    StoragePath temp_root;
    StoragePath cache_root;
    StoragePath trash_root;

    // rootを絶対パスにするのは、このアプリ境界だけにする。コア
    // は生成されたパスを 対応するStorageへ渡し、自分でcurrent_path()
    // を呼ばない。 data_rootは正式な読み取り専用
    // 資産、user_data_rootは 現在の作業ディ
    // レクトリ基準のEngineが使う互換用コピーを表す。
    [[nodiscard]] static AppPaths from_root(StoragePath root);
    [[nodiscard]] static AppPaths from_roots(StoragePath app_root,
                                             StoragePath user_root);
    [[nodiscard]] static AppPaths from_explicit_roots(StoragePath app_root, StoragePath user_root);
    [[nodiscard]] static AppPaths defaults();

    [[nodiscard]] const StoragePath& root(LogicalRoot value) const;
    [[nodiscard]] StoragePath resolve(LogicalRoot value,
                                      StoragePath relative = {}) const;
};

}  // namespace cardwirth::platform
