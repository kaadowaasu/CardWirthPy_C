#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <utility>

struct SDL_Window;

namespace cardwirth::explorer {

// 選択結果は型で区別する。特に文書選択が未
// 実装の環境では、 空のパス一覧を「何も
// 選択しなかった正常終了」と解釈しない。
enum class TransferStatus {
    Success,
    Cancelled,
    Unsupported,
    Failed,
};

struct ExternalDocument {
    // NativePathは、現在のデスクトップ用FileSystem
    // が使う互換表現。 モバイル・Webでは、文書ハンドル
    // に対応する実装ができるまで display_name
    // だけを設定し、native_pathを空にできる。
    std::filesystem::path native_path;
    std::string display_name;
};

struct TransferResult {
    TransferStatus status{TransferStatus::Failed};
    std::vector<ExternalDocument> documents;
    std::string error;

    [[nodiscard]] bool ok() const noexcept {
        return status == TransferStatus::Success;
    }
    [[nodiscard]] bool cancelled() const noexcept {
        return status == TransferStatus::Cancelled;
    }
};

struct NativePathSelection {
    TransferStatus status{TransferStatus::Failed};
    std::vector<std::filesystem::path> paths;
    std::string error;
    bool cancelled{};
    std::optional<std::size_t> selected_filter;

    [[nodiscard]] bool ok() const noexcept {
        return status == TransferStatus::Success;
    }
};
[[nodiscard]] bool supports_direct_folder_download() noexcept;
// AndroidのSAFとAppleの文書提供元への書き出しは、まずアプリ専用の
// 一時領域へ保存し、 Explorerの処理が成功してから公開する。その他のデス
// クトップでは何も行わず、 共通シェルからは常に同じ呼び出しを使えるようにする。
// 選択画面の公開APIはすべてのOSでこれに統一する。各OSの処理は即時・ワー
// カー・ ネイティブのメインループのいずれで完了してもよいが、呼び出し元は常に
// 同じコールバックで受け取り、同期的にネイティブパスを返すAPIには依存しない。
using NativePathSelectionCallback = std::function<void(NativePathSelection)>;
using FinalizeDownloadCallback = std::function<void(bool, std::string)>;

class TransferRequest final {
public:
    TransferRequest() : cancelled_(std::make_shared<std::atomic_bool>(false)) {}
    void cancel() const noexcept { cancelled_->store(true, std::memory_order_release); }
    [[nodiscard]] bool cancelled() const noexcept {
        return cancelled_->load(std::memory_order_acquire);
    }
    [[nodiscard]] const std::shared_ptr<std::atomic_bool>& cancellation_state() const noexcept {
        return cancelled_;
    }
private:
    std::shared_ptr<std::atomic_bool> cancelled_;
};

struct UploadFileOptions {
    bool multiple{true};
    std::vector<std::string> extensions; // 接頭の記号を付けない拡張子。空なら全ファイルを許可する。
    std::vector<std::pair<std::string,std::vector<std::string>>> filters;
};
TransferRequest choose_upload_files_async(SDL_Window* parent,
                                          NativePathSelectionCallback callback,
                                          const UploadFileOptions& options = {});
// マウント済みのUserData内を移動するWeb Editor用の選択画面
// 。 ホストのファイルの入出力や、別のディレクトリハンドルの取得は行わない。
struct StoredPathRequest {
    std::string title;
    std::string initial_location;
    std::string extensions; // セミコロンで区切った候補。空なら全ファイルを対象とする。
    bool directory{};
    bool save{};
    bool allow_multiple{};
    bool confirm_overwrite{true};
    std::vector<std::string> filter_labels;
    std::vector<std::string> filter_extensions;
    std::optional<std::size_t> selected_filter;
};
TransferRequest choose_stored_paths_async(const StoredPathRequest& options,
                                         NativePathSelectionCallback callback);
TransferRequest choose_upload_folders_async(SDL_Window* parent,
                                            NativePathSelectionCallback callback);
TransferRequest choose_download_path_async(SDL_Window* parent,
                                           const std::filesystem::path& suggested_name,
                                           bool directory,
                                           NativePathSelectionCallback callback);
TransferRequest choose_download_folder_async(SDL_Window* parent,
                                             NativePathSelectionCallback callback);
void finalize_download_path_async(const std::filesystem::path& path, bool success,
                                  FinalizeDownloadCallback callback);
// Explorerが選択項目をUserDataへコピーした後に、
// 提供元のアップロード一時領域を解放する。 Editorの明示的
// な取り込み完了時にも使う。解放できるのは登録済みのパスだけで、
// 共有ディレクトリは最後の項目が完了するまで残す。デスクトップ
// のパスは変更しない。 選択画面を閉じただけでは呼び出さない。
void release_upload_paths(const std::vector<std::filesystem::path>& paths);
void cleanup_stale_transfers();

}  // namespace cardwirth::explorer
