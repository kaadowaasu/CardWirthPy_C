#pragma once

#include "types.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace cwxeditor::lib {

enum class MessageSeverity { Information, Question, Warning, Error };
enum class MessageButtons { OK, OKCancel, YesNo, YesNoCancel, RetryCancel, AbortRetryIgnore };
enum class MessageResult { None, OK, Cancel, Yes, No, Retry, Abort, Ignore };

struct MessageDialogRequest {
    std::string title;
    std::string message;
    std::string details;
    MessageSeverity severity{MessageSeverity::Information};
    MessageButtons buttons{MessageButtons::OK};
};

using MessageCompletion = std::function<void(MessageResult)>;
using ColorCompletion = std::function<void(std::optional<Color>)>;

struct FileDialogRequest {
    std::string title;
    // DのDirectoryDialog.setMessage相当の説明
    // 。 対応する選択画面で表示できるよう、タイトル・パスとは分ける。
    std::string description;
    // 開く・保存するの両方で使う、ネイティブパスとUTF-8のファイル名・移動先の指
    // 定。 ディレクトリ選択はinitial_nameを無視する。フィルターはUI上
    // の案内であり、 ファイルの選択自体は内容の書き込み・公開の許可を意味しない。
    std::filesystem::path initial_directory;
    std::string initial_name;
    std::vector<FileFilter> filters;
    std::optional<std::size_t> selected_filter;
    bool allow_multiple{};
    bool confirm_overwrite{true};
};

// 保存画面では初期フィルターの提示後に形式を変えられる
// 。 selected_filterはアダプター内部
// の表示位置ではなく、要求元のフィルター添字で返す。
struct FileDialogSaveResult {
    std::optional<std::filesystem::path> path;
    std::optional<std::size_t> selected_filter;
};

using FileDialogOpenCompletion =
    std::function<void(std::vector<std::filesystem::path>)>;
using FileDialogDirectoryCompletion =
    std::function<void(std::optional<std::filesystem::path>)>;
using FileDialogSaveCompletion = std::function<void(FileDialogSaveResult)>;

class DialogBackend {
public:
    virtual ~DialogBackend() = default;
    // 保存領域内・モバイルの選択画面で使う、アプリの移動可能範囲
    // を設定する。 デスクトップのネイティブ画面は無視してよい
    // 。OS分岐ではなくホストの方針とし、 ALLも単独起動も
    // 同じFilePicker APIで解決済みルートを渡す。
    virtual void set_document_root(std::filesystem::path) {}
    [[nodiscard]] virtual MessageResult message(const MessageDialogRequest& request) = 0;
    // ブラウザー内のEditorは、外側のSDL/Pthread
    // フレーム中に同期的な入れ子ループへ入らない。 非同
    // 期のモーダルセッションを表示できる実装はこれを上
    // 書きし、 テスト・旧実装は既定の同期契約を使う。
    [[nodiscard]] virtual bool supports_message_async() const noexcept { return false; }
    [[nodiscard]] virtual bool message_async(
        const MessageDialogRequest&, MessageCompletion) { return false; }
    // 空選択は中止を表す。ネイティブ提供元の失敗は中止と偽らず例
    // 外を投げる。 FilePickerがErrorへ変換する。
    [[nodiscard]] virtual std::vector<std::filesystem::path> open_files(const FileDialogRequest& request) = 0;
    // ブラウザーの保存ファイル選択では、描画・操作の前に外
    // 側のイベントループへ戻る必要がある場合がある。 旧
    // 実装は、この既定の完了アダプターで同期動作を保つ。
    [[nodiscard]] virtual bool open_files_async(
        const FileDialogRequest& request, FileDialogOpenCompletion completed) {
        if (!completed) return false;
        completed(open_files(request));
        return true;
    }
    [[nodiscard]] virtual std::optional<std::filesystem::path> save_file(const FileDialogRequest& request) = 0;
    // 互換性を保つ保存完了通知。形式選択のない
    // 実装は初期フィルターを返し、 選択可能な
    // 実装は上書きして実際の選択を報告する。
    [[nodiscard]] virtual FileDialogSaveResult save_file_result(
        const FileDialogRequest& request) {
        return {save_file(request), request.selected_filter};
    }
    [[nodiscard]] virtual bool save_file_result_async(
        const FileDialogRequest& request, FileDialogSaveCompletion completed) {
        if (!completed) return false;
        completed(save_file_result(request));
        return true;
    }
    [[nodiscard]] virtual std::optional<std::filesystem::path> select_directory(const FileDialogRequest& request) = 0;
    [[nodiscard]] virtual bool select_directory_async(
        const FileDialogRequest& request,
        FileDialogDirectoryCompletion completed) {
        if (!completed) return false;
        completed(select_directory(request));
        return true;
    }

    // 文書提供元からの取り込みを明示的に完了する。Storage
    // へコピー済みなど、 選択パスの利用者が残っていない場合
    // だけ呼ぶ。未知・ネイティブのパスには触れない。 選択画
    // 面やサービスを閉じただけで、編集データを捨ててはなら
    // ない。 再呼び出しは安全にし、後処理の例外で元の取り
    // 込み失敗を隠さない。UIや新しい許可要求は行わない。
    virtual void release_import_file(const std::filesystem::path&) {}

    // 通常のデスクトップ保存画面は最終パスを返し、追加
    // 処理は不要。文書提供元を使う環境は、 アプリ専
    // 用の一時パスを返し、書き手が閉じてから公開する
    // 。完了処理をダイアログAPIに置き、 D由来の
    // 同期的なパス処理を保ちながらUIKit/SAF
    // の詳細をモデルへ漏らさない。 公開の失敗・例外
    // 時は一時処理が残る場合があり、呼び出し元がsuccess=false
    // を一度送って解放する。 中止対象は未完了
    // の利用権だけとし、完了済み・未知のパス
    // から公開先や既存ファイルを削除しない。
    virtual bool finalize_save_file(const std::filesystem::path&, bool success,
                                    std::string& error) {
        error.clear();
        return success;
    }
    [[nodiscard]] virtual std::optional<Color> select_color(Color initial) = 0;
    // 非ブロックの色選択。ブラウザー内で同期モーダルを待つ
    // と停止するため、 SDLはモーダルセッションを表示し
    // て後で完了する。他の実装はこの同期アダプターを使う。
    [[nodiscard]] virtual bool select_color_async(Color initial,
                                                  ColorCompletion completed) {
        if (!completed) return false;
        completed(select_color(initial));
        return true;
    }
    [[nodiscard]] virtual std::optional<FontSpec> select_font(const FontSpec& initial) = 0;
};

}  // namespace cwxeditor::lib
