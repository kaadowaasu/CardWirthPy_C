#pragma once

// Editor用の保存領域の別実装は持たない。このヘッダーは既存の
// Editorコード向けの互換窓口であり、実際の契約とネイティブ実
// 装はplatform/storage/storage.hppに
// 置き、Engine、Explorer、Editorで共有する。

#include "../../platform/storage/storage.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cwxeditor::lib {
class FileSystemBackend;
struct Services;
}  // namespace cwxeditor::lib

namespace cwxeditor::lib::editor_storage {

using StoragePath = ::cardwirth::platform::StoragePath;
using StorageBytes = std::vector<std::byte>;
using StorageEntryType = ::cardwirth::platform::StorageEntryType;
using StorageStat = ::cardwirth::platform::StorageStat;
using StorageEntry = ::cardwirth::platform::StorageEntry;
using StorageReadHandle = ::cardwirth::platform::StorageReadHandle;
using StorageWriteHandle = ::cardwirth::platform::StorageWriteHandle;
using PersistenceState = ::cardwirth::platform::PersistenceState;
using PersistenceInfo = ::cardwirth::platform::PersistenceInfo;

// 既存のUTF-8論理パスの検証関数を提供する。返す文字列は区切
// りに「/」を使い、ネイティブのパスの文字コードに依存しない。実
// 際のI/Oは共通のplatform Storageへ委ねる。
[[nodiscard]] std::optional<std::string> normalize_storage_path(
    std::string_view path, std::string* error = nullptr);

using Storage = ::cardwirth::platform::Storage;
using NativeStorage = ::cardwirth::platform::NativeStorage;

// 明示的に選択したネイティブファイルや一時保存先パス用のアダプター。不透明なプロバ
// イダーURIや、設定済みStorageに属する論理パスには使わない。一時ファイル
// ・フラッシュ・置換はNativeStorageへ委ね、対象を先に切り詰めない。呼
// び出し側は戻るまでデータを保持し、プロバイダーへの公開はFileSaveTransaction
// で別途完了させる。公開の失敗はファイルのロールバックではない。
[[nodiscard]] bool write_native_file_atomic(
    const std::filesystem::path& target, std::span<const std::byte> data,
    std::string& error, bool create_parent = true);

// 既存のServices/FileSystemBackendオーバーロードの
// ソース互換性を保つ。永続化書き込みの実装は共通のplatform storage
// が所有し、Editor固有の監視バックエンドはここに重複実装しない。
[[nodiscard]] std::unique_ptr<Storage> make_native_storage(
    std::string root_utf8,
    std::shared_ptr<::cwxeditor::lib::FileSystemBackend> filesystem,
    bool create_root = true);

[[nodiscard]] std::unique_ptr<Storage> make_native_storage(
    std::string root_utf8,
    const ::cwxeditor::lib::Services& services,
    bool create_root = true);

}  // namespace cwxeditor::lib::editor_storage

namespace cwxeditor::lib {
namespace storage_api = ::cwxeditor::lib::editor_storage;
using EditorStorage = ::cardwirth::platform::Storage;
using NativeEditorStorage = ::cardwirth::platform::NativeStorage;
using EditorStoragePath = ::cardwirth::platform::StoragePath;
using EditorPersistenceState = ::cardwirth::platform::PersistenceState;
using EditorPersistenceInfo = ::cardwirth::platform::PersistenceInfo;
}  // namespace cwxeditor::lib
