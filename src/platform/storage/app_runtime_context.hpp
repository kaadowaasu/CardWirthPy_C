#pragma once

#include "app_paths.hpp"

#include <functional>
#include <string_view>
#include <utility>

namespace cardwirth::platform {

// 共通の実行環境の境界。音声・選択画面・クリップボード・文字入力など
// のサービスを、 Engine・Explorer・Editorのコ
// ンストラクターを変えずに追加できる。 二つのStorage参照で
// インストール資産と書き込み可能なユーザーデータを区別しつつ、 従
// 来の単一Storageのコンストラクターとのソース互換性を保つ。
struct AppRuntimeContext {
    // 二つのマウントへ分割する前の呼び出し元向けの互換名。
    // 以前のALLがUserDataからStorageを作っ
    // ていたことに合わせ、ユーザーストレージを指す。 このメ
    // ンバー経由の書き込みが引き続き可能であることを保つ。
    Storage& storage;
    Storage& asset_storage;
    Storage& user_storage;
    AppPaths paths;

    // グローバルなマルチメディアサービスはALLホストが所有
    // する。 埋め込みEngineはホストの準備完了後に借用
    // し、SDL・TTF・音声を独自に初期化・終了しない。
    bool sdl_initialized{};
    bool ttf_initialized{};
    bool audio_initialized{};
    std::function<void(std::string_view)> report_error{};

    // 従来の単一マウント構成。両方の論理マウントが同じ
    // バックエンドを参照し、 アプリとユーザーデータの
    // ルートを同じにするポータブル配布形式を維持する。
    AppRuntimeContext(Storage& value, AppPaths app_paths)
        : storage(value), asset_storage(value), user_storage(value),
          paths(std::move(app_paths)) {}

    // インストール先とユーザーデータを分離したホスト構成。
    // storageはソース互換用にユーザーマウントの別名と
    // して残し、新しい処理は下の明示的なアクセサーを使う。
    AppRuntimeContext(Storage& assets, Storage& user, AppPaths app_paths)
        : storage(user), asset_storage(assets), user_storage(user),
          paths(std::move(app_paths)) {}

    [[nodiscard]] Storage& storage_ref() const noexcept { return storage; }
    [[nodiscard]] Storage& asset_storage_ref() const noexcept { return asset_storage; }
    [[nodiscard]] Storage& user_storage_ref() const noexcept { return user_storage; }
};

}  // namespace cardwirth::platform
