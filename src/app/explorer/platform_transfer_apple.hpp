#pragma once

#include "platform_transfer.hpp"
#include <cstddef>

namespace cardwirth::explorer::apple {

// 即時取り込みの設定。既定値ではExplorer・Editorの動作を維持する。
// Engineはタイトル、複数選択の可否、ファイルごとの読み込み上限を指定する。
struct ImportOptions {
    std::string title;
    std::vector<std::string> extensions;
    bool multiple{true};
    std::size_t max_file_bytes{};
    std::function<bool()> cancelled;
};
void choose_import_files(SDL_Window* parent, ImportOptions options,
                         NativePathSelectionCallback callback);
void choose_upload_files(SDL_Window* parent, NativePathSelectionCallback callback);
void choose_upload_folders(SDL_Window* parent, NativePathSelectionCallback callback);
void choose_download_path(SDL_Window* parent,
                          const std::filesystem::path& suggested_name,
                          bool directory,
                          NativePathSelectionCallback callback);
void choose_download_folder(SDL_Window* parent, NativePathSelectionCallback callback);
void finalize_download_path(const std::filesystem::path& path, bool success,
                            FinalizeDownloadCallback callback);
void release_upload_paths(const std::vector<std::filesystem::path>& paths);
// iOSではサンドボックス内のファイルURLを外部のデスクトップアプリへ渡せないた
// め、 自前で管理するAVPlayer画面を表示し、明示的な終了操作を用意する。
[[nodiscard]] bool play_preview_video(const std::filesystem::path& path);
void stop_preview_video() noexcept;
// 以前の異常終了で残ったUUID名の一時ディレクトリを削除する
// 。 UIキューの外で処理し、実行中の書き出しには触れない。
void cleanup_stale_transfers();

}  // namespace cardwirth::explorer::apple
