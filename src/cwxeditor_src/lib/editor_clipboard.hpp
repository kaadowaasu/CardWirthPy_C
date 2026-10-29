#pragma once

#include "clipboard.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cwxeditor::lib {

// イベント編集がXML転送に使うMIME型。 ここで表記
// を共通化してWebのClipboardItemへ登録
// しつつ、任意のClipboardData形式も許す。
inline constexpr std::string_view kEditorClipboardXmlMimeType =
    "application/x-cardwirth-event+xml";

// ブラウザー・OSアダプターがMIMEを正規
// 化した場合に受け付ける共通の別名。 新しく
// 作るXMLには、常に上の正式な値を使う。
inline constexpr std::string_view kEditorClipboardXmlMime =
    kEditorClipboardXmlMimeType;

enum class ClipboardPermission {
    Unknown,
    Prompt,
    Granted,
    Denied,
};

// 任意の非同期接続（通常はWeb Clipboard API）の状
// 態。 Unavailableは通知が未接続という意味だけで、その
// 場合も同期ClipboardBackendは使えることがある。
enum class ClipboardBridgeState {
    Unavailable,
    Idle,
    Ready = Idle,
    PendingRead,
    PendingWrite,
    Failed,
};

using ClipboardPermissionState = ClipboardPermission;
using ClipboardAsyncState = ClipboardBridgeState;

struct ClipboardBridgeStatus {
    ClipboardPermission permission{ClipboardPermission::Unknown};
    ClipboardBridgeState state{ClipboardBridgeState::Unavailable};
    std::uint64_t operation{};
    bool custom_formats_supported{};
    std::string error;

    [[nodiscard]] bool ready() const noexcept {
        return state == ClipboardBridgeState::Idle &&
               permission != ClipboardPermission::Denied;
    }
};

// 完了は値で受け渡す。ブラウザーのPromise通知
// から任意のスレッドで完了しても、 C++オブジェ
// クトの領域を借用しない。書き込み時のdataは空
// 、 読み込み成功時にはブラウザーの結果を含める。
struct ClipboardBridgeResult {
    bool success{};
    ClipboardData data;
    ClipboardBridgeStatus status;
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept { return success; }
};

using ClipboardAsyncResult = ClipboardBridgeResult;
using ClipboardCompletion = std::function<void(ClipboardBridgeResult)>;

// ブラウザーClipboard APIやモバイル
// との接続用コールバック。 write/read
// は即時完了でも、Promise・イベントで後
// から完了してもよく、 ラッパーは特定のループ
// を前提とせず状態遷移を直列化する。 supports_custom_format
// がなければ、独自形式は内部に保持する
// 。 trueを返す実装は、ClipboardData::formats
// を接続先自身で保持
// することを示す。
struct WebClipboardCallbacks {
    using Write = std::function<void(const ClipboardData&, ClipboardCompletion)>;
    using Read = std::function<void(ClipboardCompletion)>;
    using Permission = std::function<ClipboardPermission()>;
    using SupportsCustomFormat = std::function<bool(std::string_view)>;

    Write write;
    Read read;
    Permission permission;
    SupportsCustomFormat supports_custom_format;

    // 説明的な長い別名も受け付け、set_web_callbacks()
    // で上の四つの正式項目へ揃える。 別のアダプター型を作らず
    // 、JS・モバイル側の構築内容を分かりやすく指定できる。
    Write write_async;
    Read read_async;
    Permission query_permission;
    SupportsCustomFormat supports_custom_formats;

    [[nodiscard]] explicit operator bool() const noexcept {
        return static_cast<bool>(write) || static_cast<bool>(read) ||
               static_cast<bool>(write_async) || static_cast<bool>(read_async);
    }
};

using WebClipboardWriteCallback = WebClipboardCallbacks::Write;
using WebClipboardReadCallback = WebClipboardCallbacks::Read;
using WebClipboardPermissionCallback = WebClipboardCallbacks::Permission;

using ClipboardWebCallbacks = WebClipboardCallbacks;
using WebClipboardBridge = WebClipboardCallbacks;
using WebClipboardBridgeCallbacks = WebClipboardCallbacks;

// Editor共通のクリップボード窓口。OS側
// が表せる内容は常にそちらを正式な状態とする。
// Webなどで独自MIMEを扱えなければ同
// じデータをプロセス内へ保持し、 通常の文字
// ・ファイル内容がこの窓口から書いた値のまま
// である場合だけ統合して戻す。 XMLのコピ
// ー・貼り付けを保ちつつ、外部で内容が変わっ
// た後に古い独自データを混ぜないようにする。
class EditorClipboard final : public ClipboardBackend {
public:
    // 窓口自体の破棄後も非同期通知が実装状
    // 態を保持できるよう、前方宣言にする。
    struct SharedState;

    explicit EditorClipboard(std::shared_ptr<ClipboardBackend> backend = {});
    explicit EditorClipboard(ClipboardBackend& backend) noexcept;
    ~EditorClipboard() override;

    EditorClipboard(const EditorClipboard&) = delete;
    EditorClipboard& operator=(const EditorClipboard&) = delete;

    void clear() override;
    void write(const ClipboardData& data) override;
    [[nodiscard]] ClipboardData read() const override;
    [[nodiscard]] ClipboardData read_formats(std::span<const std::string_view> requested) const override;
    [[nodiscard]] bool contains(std::string_view mime_type) const override;

    // 内側のバックエンドを置き換える。参照版は所有せず、 shared_ptr
    // 版はServices::clipboardと同じく寿命を保持する。
    void set_backend(std::shared_ptr<ClipboardBackend> backend);
    void set_backend(ClipboardBackend& backend) noexcept;
    [[nodiscard]] std::shared_ptr<ClipboardBackend> backend() const;

    // Web・モバイルの非同期接続を設定・解除する。 設定し
    // ても、同期バックエンドと局所の代替データは捨てない。
    void set_web_callbacks(WebClipboardCallbacks callbacks);
    void set_web_bridge(WebClipboardCallbacks callbacks) {
        set_web_callbacks(std::move(callbacks));
    }
    void clear_web_callbacks();
    [[nodiscard]] WebClipboardCallbacks web_callbacks() const;
    void set_web_write_callback(WebClipboardWriteCallback callback);
    void set_web_read_callback(WebClipboardReadCallback callback);
    void set_web_permission_callback(WebClipboardPermissionCallback callback);

    // ネイティブホスト・テスト向けに権限を明示できる。 権限の確認関数が登
    // 録されていれば、refresh_permission()で評価する。
    void set_permission(ClipboardPermission permission);
    [[nodiscard]] ClipboardPermission permission() const noexcept;
    [[nodiscard]] ClipboardPermission refresh_permission() const;

    // WebのClipboardItemの独自MIME対
    // 応は、接続側全体またはホストごとに指定できる。 両
    // 方ある場合は、MIMEごとの問い合わせを優先する。
    void set_custom_formats_supported(bool supported) noexcept;
    [[nodiscard]] bool custom_formats_supported() const noexcept;

    [[nodiscard]] ClipboardBridgeStatus bridge_status() const;
    [[nodiscard]] ClipboardBridgeState bridge_state() const noexcept;
    [[nodiscard]] std::uint64_t pending_operation() const noexcept;

    // 接続先の操作を始める。Webの許可が拒否され
    // てもコピー・貼り付けを使えるよう、 書き込
    // み開始前に同期側を更新する。対応する通知が
    // 未登録なら、同期の値ですぐに完了通知する。
    void write_async(const ClipboardData& data, ClipboardCompletion completion = {});
    void read_async(ClipboardCompletion completion = {});

    // Editorの文字・XML・ファイル部品が使う補助操作。
    void write_text(std::string text);
    void write_xml(std::string xml,
                   std::string mime_type = std::string(kEditorClipboardXmlMimeType));
    void write_files(std::vector<std::filesystem::path> files);
    [[nodiscard]] std::string text() const;
    [[nodiscard]] std::optional<std::string> xml() const;
    [[nodiscard]] std::vector<std::filesystem::path> files() const;

    // プロセス内の表現を確認・消去する。 fallback()はスナ
    // ップショットなので、ブラウザーの非同期通知中も安全に使える。
    [[nodiscard]] bool has_internal_fallback() const noexcept;
    [[nodiscard]] ClipboardData fallback() const;
    void clear_internal_fallback() noexcept;

private:
    std::shared_ptr<SharedState> state_;

    [[nodiscard]] ClipboardData read_locked_or_backend() const;
    void finish_async(std::uint64_t operation, ClipboardBridgeResult result,
                      ClipboardCompletion completion, bool is_read);
};

// 既存のWindowsClipboardBackend・MemoryClipboard
// と並べて見つけやすい名前の別名。
using EditorClipboardBackend = EditorClipboard;
using EditorClipboardFacade = EditorClipboard;

// 具体的なラッパーに呼び出し元を依存させない、小さなファクトリー。 make_platform_services()
// からの導入にも使える。
[[nodiscard]] std::shared_ptr<ClipboardBackend> make_editor_clipboard(
    std::shared_ptr<ClipboardBackend> backend = {});

[[nodiscard]] std::shared_ptr<EditorClipboard> make_editor_clipboard_backend(
    std::shared_ptr<ClipboardBackend> backend = {});

}  // namespace cwxeditor::lib
