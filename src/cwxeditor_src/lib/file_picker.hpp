#pragma once

#include "cwxeditor_src/lib/dialogs.hpp"
#include "platform/storage/storage.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace cwxeditor::lib {

// ブラウザーはEditorへOSパスを渡せないため、WebBlob/WebVirtualPath
// は不透明な値とする。 Web側はFile/Blob・File
// System Access API・メモリ上のURLなど
// で実現でき、 その詳細をEditorコアへ持ち込まない。
struct WebBlob {
    std::string name;
    std::string mime_type{"application/octet-stream"};
    std::vector<std::byte> bytes;

    [[nodiscard]] bool empty() const noexcept { return bytes.empty(); }
};

struct WebVirtualPath {
    // 表示側が所有する不透明な識別子。 コア
    // で絶対ファイルパスとして解釈しない。
    std::string id;
    std::string name;
    bool directory{};
};

class FilePickerPath final {
public:
    enum class Kind { Native, WebVirtual, WebBlob };
    using Value = std::variant<std::filesystem::path, WebVirtualPath, WebBlob>;

    FilePickerPath() = default;
    explicit FilePickerPath(std::filesystem::path path);
    explicit FilePickerPath(WebVirtualPath path);
    explicit FilePickerPath(WebBlob blob);

    [[nodiscard]] static FilePickerPath native(std::filesystem::path path);
    [[nodiscard]] static FilePickerPath web_virtual(WebVirtualPath path);
    [[nodiscard]] static FilePickerPath web_blob(WebBlob blob);

    [[nodiscard]] Kind kind() const noexcept;
    [[nodiscard]] bool is_native() const noexcept { return kind() == Kind::Native; }
    [[nodiscard]] bool is_web_virtual() const noexcept { return kind() == Kind::WebVirtual; }
    [[nodiscard]] bool is_web_blob() const noexcept { return kind() == Kind::WebBlob; }

    // ネイティブパスはNativeの値だけが持つ。 Webでは不透
    // 明IDをstd::filesystem::pathへ変換せず
    // 、web_virtual_path()/blob()を使う。
    [[nodiscard]] std::optional<std::filesystem::path> native_path() const;
    [[nodiscard]] const WebVirtualPath* web_virtual_path() const noexcept;
    [[nodiscard]] const WebBlob* blob() const noexcept;
    [[nodiscard]] const Value& value() const noexcept { return value_; }

    [[nodiscard]] std::string display_name() const;

private:
    Value value_;
};

enum class FilePickerOperation {
    OpenFile,
    OpenFiles,
    SelectDirectory,
    SaveFile,

    // ダイアログモードでなく操作として扱う
    // 呼び出し元向けの、読みやすい別名。
    Open = OpenFile,
    Multiple = OpenFiles,
    Directory = SelectDirectory,
    Save = SaveFile,
};
using FilePickerMode = FilePickerOperation;

enum class FilePickerCapability : std::uint32_t {
    None = 0,
    OpenFile = 1u << 0,
    OpenFiles = 1u << 1,
    SelectDirectory = 1u << 2,
    SaveFile = 1u << 3,
    Import = 1u << 4,
    Export = 1u << 5,
    All = OpenFile | OpenFiles | SelectDirectory | SaveFile | Import | Export,
};
using FilePickerCapabilities = FilePickerCapability;

constexpr FilePickerCapability operator|(FilePickerCapability left,
                                         FilePickerCapability right) noexcept {
    using Underlying = std::underlying_type_t<FilePickerCapability>;
    return static_cast<FilePickerCapability>(static_cast<Underlying>(left) |
                                             static_cast<Underlying>(right));
}

constexpr FilePickerCapability operator&(FilePickerCapability left,
                                         FilePickerCapability right) noexcept {
    using Underlying = std::underlying_type_t<FilePickerCapability>;
    return static_cast<FilePickerCapability>(static_cast<Underlying>(left) &
                                             static_cast<Underlying>(right));
}

constexpr FilePickerCapability& operator|=(FilePickerCapability& left,
                                           FilePickerCapability right) noexcept {
    left = left | right;
    return left;
}

[[nodiscard]] constexpr bool has_capability(FilePickerCapabilities capabilities,
                                            FilePickerCapability capability) noexcept {
    return (capabilities & capability) != FilePickerCapability::None;
}

enum class FilePickerStatus { Success, Cancelled, Error };

enum class FilePickerError {
    None,
    Unsupported,
    InvalidRequest,
    BackendFailure,
    ReadFailure,
    WriteFailure,
    StorageFailure,
};

// 拡張子を提示できる選択画面向けの、控えめな共通
// の候補指定。 安全性の規則でも選択結果の検証で
// もない。FileFilterを損失なく拡張子で
// 表せなければ unrestrictedをtrue
// にし、元の要求で選べるファイルを隠さない。
struct FileExtensionFilter {
    std::vector<std::string> extensions;
    bool unrestricted{true};
};

[[nodiscard]] FileExtensionFilter make_file_extension_filter(
    std::span<const FileFilter> filters);

// OSハンドルや固有のフィルター構文を含まない、準備済みのネイティブ
// 選択情報。 最初に表示したフィルターしか初期選択できない実装でも
// 、source_indexで元の要求を識別する。 拡張子は候補の
// 案内で選択結果の検証には使わず、文字列はすべてUTF-8で渡す。
struct NativeFileDialogFilter {
    std::size_t source_index{};
    std::string label;
    FileExtensionFilter extensions;
};
struct NativeFileDialogPlan {
    std::string title;
    std::string description;
    std::string initial_location_utf8;
    std::vector<NativeFileDialogFilter> filters;
    // SAF/UIDocumentPickerなど形式を選べない提供元に
    // は、 初期選択だけでなく、元の要求順のフィルターの和集合を渡す。
    FileExtensionFilter combined_extensions;
    bool allow_multiple{};
    // falseでもOSが確認を要求する場合がある。falseはアダプター自身の
    // 上書きを許可するものではない。 書き込みの確定はStorageが担当する。
    bool confirm_overwrite{true};
};
// 存在確認・ディレクトリ作成・拡張子追加を行わない準備処理。 不正なモード
// ・フィルター添字・NULにはinvalid_argumentを投げる。
[[nodiscard]] NativeFileDialogPlan plan_native_file_dialog(
    FilePickerOperation operation, const FileDialogRequest& request);
// 空一覧は中止だが、一覧内の空・NUL入り項目はバックエンドの
// エラー。 content://や不透明なブラウザーハンドル
// は対象外で、モバイル側は一時領域のサービスで先に解決する。
[[nodiscard]] std::vector<std::filesystem::path> native_file_dialog_paths(
    const NativeFileDialogPlan& plan, std::span<const std::string> paths_utf8);
// アダプターが示したフィルターの位置を、元の要求
// の添字へ戻す。 ネイティブ位置がない・範囲外な
// ら結果未提供とし、呼び出し元の初期選択を保つ。
[[nodiscard]] std::optional<std::size_t> native_file_dialog_source_filter(
    const NativeFileDialogPlan& plan, int native_filter_index,
    std::optional<std::size_t> fallback = {}) noexcept;

struct FilePickerRequest {
    FilePickerOperation operation{FilePickerOperation::OpenFile};
    std::string title;
    // ディレクトリ選択の説明。ファイル名とは分ける。
    std::string description;
    // ネイティブはディレクトリとして使う。Webでは表
    // 示・移動先の案内に限り、OSパスへ変換しない。
    std::filesystem::path initial_directory;
    std::string initial_name;
    std::vector<FileFilter> filters;
    std::optional<std::size_t> selected_filter;
    bool allow_multiple{};
    bool confirm_overwrite{true};
    // import_files()が設定し、汎用の開く画
    // 面とは別の取り込み機能として公開できるようにする。
    bool importing{};
};

struct FilePickerExportRequest {
    FilePickerRequest picker;
    std::string suggested_name;
    std::string mime_type{"application/octet-stream"};
    std::vector<std::byte> bytes;
};

using FilePickerImportRequest = FilePickerRequest;
using FilePickerExport = FilePickerExportRequest;

struct FilePickerResult {
    FilePickerStatus status{FilePickerStatus::Cancelled};
    FilePickerError error{FilePickerError::None};
    std::string message;
    std::vector<FilePickerPath> selections;
    // 元の要求のフィルター識別子。中止時や形式選択のない実装でも保持し、D
    // 版と同様に「名前を付けて保存」の呼び出し元が形式を選べるようにする。
    std::optional<std::size_t> selected_filter;
    // Webの書き出しは生成Blobを表示側へ残す。 ネイ
    // ティブでは、選んだパスをselectionsへ返す。
    std::optional<WebBlob> exported_blob;

    [[nodiscard]] bool succeeded() const noexcept {
        return status == FilePickerStatus::Success;
    }
    [[nodiscard]] bool cancelled() const noexcept {
        return status == FilePickerStatus::Cancelled;
    }
    [[nodiscard]] bool failed() const noexcept {
        return status == FilePickerStatus::Error;
    }
    explicit operator bool() const noexcept { return succeeded(); }

    [[nodiscard]] static FilePickerResult success(
        std::vector<FilePickerPath> selections = {},
        std::optional<std::size_t> selected_filter = {});
    [[nodiscard]] static FilePickerResult cancelled_result(
        std::optional<std::size_t> selected_filter = {});
    [[nodiscard]] static FilePickerResult failure(FilePickerError error,
                                                    std::string message,
                                                    std::optional<std::size_t> selected_filter = {});
};

class FilePicker {
public:
    using Completion = std::function<void(FilePickerResult)>;

    virtual ~FilePicker() = default;

    virtual void set_document_root(std::filesystem::path) {}

    [[nodiscard]] virtual FilePickerCapabilities capabilities() const noexcept = 0;
    [[nodiscard]] virtual FilePickerResult pick(const FilePickerRequest& request) = 0;
    // 非同期の選択画面は、ブラウザーの保存ファイル画
    // 面内で止まらず、外側のイベントループへ戻れる。
    // 旧提供元は、この既定処理で同期動作を保つ。
    [[nodiscard]] virtual bool pick_async(const FilePickerRequest& request,
                                          Completion completed);
    [[nodiscard]] virtual FilePickerResult export_blob(
        const FilePickerExportRequest& request) = 0;

    // 利用者が使い終わった後だけ、取り込み資源の寿命を終える。 通常
    // の選択・オープンやサービスの破棄では呼ばない。既定のBlob・
    // ネイティブデスクトップには外部後処理は不要。 ネイティブ側は登
    // 録済みの提供元の利用権だけを解放し、任意のパスは削除しない。
    virtual void release_imports(const FilePickerResult&) noexcept {}

    // 呼び出し元を具体的な画面実装から独立させつつ
    // 、同じ要求・結果の契約を保つ補助メソッド。
    [[nodiscard]] FilePickerResult open_file(FilePickerRequest request);
    [[nodiscard]] FilePickerResult open_files(FilePickerRequest request);
    [[nodiscard]] FilePickerResult select_directory(FilePickerRequest request);
    [[nodiscard]] bool open_file_async(FilePickerRequest request,
                                       Completion completed);
    [[nodiscard]] bool open_files_async(FilePickerRequest request,
                                        Completion completed);
    [[nodiscard]] bool select_directory_async(FilePickerRequest request,
                                              Completion completed);
    [[nodiscard]] FilePickerResult save_file(FilePickerRequest request);
    [[nodiscard]] bool save_file_async(FilePickerRequest request,
                                       Completion completed);
    [[nodiscard]] FilePickerResult export_file(FilePickerExportRequest request);
    [[nodiscard]] FilePickerResult import_files(FilePickerRequest request);
};

// 既存のネイティブDialogBackendのアダプター。空
// の配列・optionalはユーザーの中止として扱い、 例外
// やファイル書き込み失敗は診断付きのErrorとして返す。
class NativeFilePicker final : public FilePicker {
public:
    // 借用する提供元は、選択部品を破棄するコールバックも含め、全処理が
    // 終わるまで有効である必要がある。 下の所有権付きの版を優先する。
    explicit NativeFilePicker(
        DialogBackend& dialogs,
        FilePickerCapabilities capabilities = FilePickerCapability::All);
    // 借用Hostサービスから作った選択部品のために、ダイアログア
    // ダプターを保持する。 既存の呼び出し元向けに参照版も残す。
    explicit NativeFilePicker(
        std::shared_ptr<DialogBackend> dialogs,
        FilePickerCapabilities capabilities = FilePickerCapability::All);

    [[nodiscard]] FilePickerCapabilities capabilities() const noexcept override {
        return capabilities_;
    }
    [[nodiscard]] FilePickerResult pick(const FilePickerRequest& request) override;
    [[nodiscard]] bool pick_async(const FilePickerRequest& request,
                                  Completion completed) override;
    void set_document_root(std::filesystem::path root) override;
    void release_imports(const FilePickerResult& selection) noexcept override;
    [[nodiscard]] FilePickerResult export_blob(
        const FilePickerExportRequest& request) override;

private:
    std::shared_ptr<DialogBackend> owned_dialogs_;
    DialogBackend& dialogs_;
    FilePickerCapabilities capabilities_;
};
using NativeFilePickerAdapter = NativeFilePicker;

struct WebFilePickerCallbacks {
    // ブラウザー固有なのはこの通知だけ。取り込みではBlobを持つFilePickerPath
    // を返し、 CancelledまたはErrorも返せる。
    std::function<FilePickerResult(const FilePickerRequest&)> pick;
    // ブラウザーのダウンロード、またはFile System Access
    // APIの書き込みを行う。 推奨名を含む完全なBlobを受け取る。
    std::function<FilePickerResult(const FilePickerExportRequest&)> export_blob;
};

class WebFilePicker final : public FilePicker {
public:
    explicit WebFilePicker(WebFilePickerCallbacks callbacks = {},
                           FilePickerCapabilities capabilities = FilePickerCapability::None);

    [[nodiscard]] FilePickerCapabilities capabilities() const noexcept override {
        return capabilities_;
    }
    [[nodiscard]] FilePickerResult pick(const FilePickerRequest& request) override;
    [[nodiscard]] FilePickerResult export_blob(
        const FilePickerExportRequest& request) override;

private:
    WebFilePickerCallbacks callbacks_;
    FilePickerCapabilities capabilities_;
};
using WebFilePickerAdapter = WebFilePicker;

struct StorageImportRequest {
    cardwirth::platform::StoragePath destination_directory;
    bool overwrite{};
    bool preserve_directories{true};
    // 保存先へ書く前に実行する、任意の命名規則。available
    // はStorageと同じ一括処理で予約済みの名前を確認する。
    // 両通知は同期的で、availableを保持したり、保存先
    // へ再入・変更したりしないこと。 元の親ディレクトリ内の名前
    // を返し、空・逸脱・重複・例外なら取り込み全体を失敗にする。
    using Available = std::function<bool(const cardwirth::platform::StoragePath&)>;
    std::function<cardwirth::platform::StoragePath(
        const cardwirth::platform::StoragePath&, const Available&)> resolve_destination;
};

struct StorageImportResult {
    FilePickerStatus status{FilePickerStatus::Error};
    FilePickerError error{FilePickerError::StorageFailure};
    std::string message;
    std::vector<cardwirth::platform::StoragePath> imported_paths;

    [[nodiscard]] bool succeeded() const noexcept {
        return status == FilePickerStatus::Success;
    }
    [[nodiscard]] bool cancelled() const noexcept {
        return status == FilePickerStatus::Cancelled;
    }
    [[nodiscard]] bool failed() const noexcept {
        return status == FilePickerStatus::Error;
    }
    explicit operator bool() const noexcept { return succeeded(); }
};

// 入力と旧内容を事前
// に保持し、失敗した
// 現在の項目を含め、
// 書き込みを試みた
// 項目を逆順に復元
// する。置換後の失
// 敗もあり得るため
// 、失敗先が未変更
// とは仮定しない。
[[nodiscard]] StorageImportResult import_to_storage(
    const FilePickerResult& selection,
    cardwirth::platform::Storage& storage,
    const StorageImportRequest& request);

[[nodiscard]] StorageImportResult pick_and_import_to_storage(
    FilePicker& picker,
    cardwirth::platform::Storage& storage,
    FilePickerRequest picker_request,
    StorageImportRequest storage_request);

}  // namespace cwxeditor::lib
