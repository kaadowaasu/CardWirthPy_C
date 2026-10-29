#pragma once

#include "events.hpp"
#include "types.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace cwxeditor::lib {

// 文字位置はUTF-8のコード単位のオフセット。プラットフォ
// ーム境界でこの契約を明確にし、ネイティブ側が内部でUTF-16
// を使っていても、Editor側には常に同じ表現を渡す。
struct TextSelection {
    std::size_t start{};
    std::size_t length{};

    [[nodiscard]] constexpr std::size_t end() const noexcept {
        return start + length;
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return length == 0U; }

    friend constexpr bool operator==(TextSelection, TextSelection) noexcept = default;
};

// 分類は具体的なウィンドウシステムやブラウザの型から独立させる。Platform::Windows/MacOS/Linux
// はいずれもDesktopになり得る。ブラウザ向けバックエン
// ドは、ホストオブジェクトを公開せずにWebを報告できる。
enum class TextInputPlatform { Desktop, Web, Mobile, Headless };

enum class TextInputCapability : std::uint32_t {
    None = 0U,
    Text = 1U << 0U,
    Composition = 1U << 1U,
    Selection = 1U << 2U,
    SurroundingText = 1U << 3U,
    SoftKeyboard = 1U << 4U,
    Multiline = 1U << 5U,
    Password = 1U << 6U,
};

constexpr TextInputCapability operator|(TextInputCapability left,
                                         TextInputCapability right) noexcept {
    return static_cast<TextInputCapability>(static_cast<std::uint32_t>(left) |
                                             static_cast<std::uint32_t>(right));
}

constexpr TextInputCapability operator&(TextInputCapability left,
                                         TextInputCapability right) noexcept {
    return static_cast<TextInputCapability>(static_cast<std::uint32_t>(left) &
                                             static_cast<std::uint32_t>(right));
}

constexpr TextInputCapability& operator|=(TextInputCapability& left,
                                           TextInputCapability right) noexcept {
    left = left | right;
    return left;
}

[[nodiscard]] constexpr bool has_text_input_capability(
    TextInputCapability value, TextInputCapability required) noexcept {
    return (value & required) == required;
}

struct TextInputCapabilities {
    TextInputPlatform platform{TextInputPlatform::Headless};
    TextInputCapability features{TextInputCapability::None};

    [[nodiscard]] constexpr bool supports(TextInputCapability value) const noexcept {
        return has_text_input_capability(features, value);
    }

    [[nodiscard]] static constexpr TextInputCapabilities desktop() noexcept {
        return {TextInputPlatform::Desktop,
                TextInputCapability::Text | TextInputCapability::Composition |
                    TextInputCapability::Selection | TextInputCapability::SurroundingText |
                    TextInputCapability::Multiline | TextInputCapability::Password};
    }

    [[nodiscard]] static constexpr TextInputCapabilities web() noexcept {
        return {TextInputPlatform::Web,
                TextInputCapability::Text | TextInputCapability::Composition |
                    TextInputCapability::Selection | TextInputCapability::SurroundingText |
                    TextInputCapability::Multiline | TextInputCapability::Password};
    }

    [[nodiscard]] static constexpr TextInputCapabilities mobile() noexcept {
        return {TextInputPlatform::Mobile,
                TextInputCapability::Text | TextInputCapability::Composition |
                    TextInputCapability::Selection | TextInputCapability::SurroundingText |
                    TextInputCapability::SoftKeyboard | TextInputCapability::Multiline |
                    TextInputCapability::Password};
    }

    [[nodiscard]] static constexpr TextInputCapabilities headless() noexcept {
        return {TextInputPlatform::Headless, TextInputCapability::None};
    }
};

struct TextComposition {
    // composingの文字列は現在のIME未確定文字列。選択範囲は
    // この文字列内の位置で、カーソルもそのUTF-8オフセットを表す。
    std::string text;
    TextSelection selection{};
    std::size_t cursor{};
};

struct TextInputRequest {
    std::string text;
    TextSelection selection{};
    bool multiline{};
    bool password{};
    bool read_only{};
    std::string language;
};

struct TextInputState {
    std::string text;
    TextSelection selection{};
    std::optional<TextComposition> composition;
    bool active{};
    bool soft_keyboard_visible{};
};

enum class TextInputEventKind {
    CompositionStart,
    CompositionUpdate,
    Commit,
    CompositionCancel,
    SelectionChanged,
    SoftKeyboardChanged,
};

// ネイティブアダプターが相互変換するイベント形式は、このパ
// ケットに統一する。ネイティブやホスト固有のハンドルは含め
// ない。バックエンドは任意のスレッドから通知先を呼べる。
struct TextInputEventPacket {
    TextInputEventKind kind{TextInputEventKind::CompositionCancel};
    std::string text;
    TextSelection selection{};
    std::size_t cursor{};
    bool visible{};
};

using TextInputEventSink = std::function<void(const TextInputEventPacket&)>;
using CompositionStartHandler = std::function<void()>;
using CompositionUpdateHandler = std::function<void(const TextComposition&)>;
using TextCommitHandler = std::function<void(std::string_view)>;
using CompositionCancelHandler = std::function<void()>;
using SelectionChangedHandler = std::function<void(TextSelection)>;
using SoftKeyboardHandler = std::function<void(bool)>;
using TextInputStateHandler = std::function<void(const TextInputState&)>;

// コールバックはサービスのロック内でコピーし、ロック解除後に呼ぶ。
// これにより、自身の接続解除、別コールバックの登録、TextInputService
// への再入で入力スレッドがデッドロックしない。
struct TextInputCallbacks {
    // Widget::on_text_input向けの互換コールバック。未確定文字列の更新
    // はcomposing=true、確定文字列はcomposing=falseで届く。
    TextInputHandler on_text_input;
    CompositionStartHandler on_composition_start;
    CompositionUpdateHandler on_composition_update;
    TextCommitHandler on_commit;
    CompositionCancelHandler on_composition_cancel;
    SelectionChangedHandler on_selection_changed;
    SoftKeyboardHandler on_soft_keyboard;
    TextInputStateHandler on_state_changed;
};

using TextInputCallbackId = std::uint64_t;

// コールバック登録への、ムーブ専用でスレッドセーフな接続。
// 入力コールバックの配送中に破棄しても安全。実行中のコー
// ルバックは完了してよいが、以後の配送対象には含めない。
class TextInputSubscription {
public:
    TextInputSubscription() noexcept = default;
    ~TextInputSubscription();

    TextInputSubscription(const TextInputSubscription&) = delete;
    TextInputSubscription& operator=(const TextInputSubscription&) = delete;
    TextInputSubscription(TextInputSubscription&& other) noexcept;
    TextInputSubscription& operator=(TextInputSubscription&& other) noexcept;

    void disconnect() noexcept;
    [[nodiscard]] bool connected() const noexcept;
    [[nodiscard]] TextInputCallbackId id() const noexcept { return id_; }

private:
    friend class TextInputService;

    TextInputSubscription(std::function<void()> disconnect,
                          TextInputCallbackId id,
                          std::shared_ptr<std::atomic_bool> connected);

    std::function<void()> disconnect_;
    TextInputCallbackId id_{};
    std::shared_ptr<std::atomic_bool> connected_;
};

// プラットフォームアダプターの契約。Desktop、Web、
// Mobile、テスト用のいずれでも実装でき、このインターフ
// ェースを通じてネイティブオブジェクトを公開する必要はない。
class TextInputBackend {
public:
    virtual ~TextInputBackend() = default;

    [[nodiscard]] virtual TextInputCapabilities capabilities() const noexcept = 0;

    // 開始・終了はGUIのフォーカスと分ける。EditorがWidget
    // モデルの所有権を保ったまま、バックエンドは不可視の入力
    // 先、HTML入力欄、IMEコンテキストを作成できる。
    virtual bool begin(const TextInputRequest& request, std::string& error) = 0;
    virtual void end() noexcept = 0;

    // Editorの周辺テキストと選択範囲をネイティ
    // ブ入力コンテキストへ同期する。確定イベントだ
    // けを提供するアダプターでは既定実装を使える。
    virtual bool set_surrounding_text(std::string_view text,
                                      TextSelection selection,
                                      std::string& error) {
        (void)text;
        (void)selection;
        error.clear();
        return true;
    }

    virtual bool set_selection(TextSelection selection, std::string& error) {
        (void)selection;
        error.clear();
        return true;
    }

    virtual bool request_soft_keyboard(bool visible, std::string& error) {
        (void)visible;
        error = "this backend does not provide a soft keyboard";
        return false;
    }

    // 省略可能なイベント接続。サービスはプラットフォー
    // ム非依存の通知先を渡す。別経路でイベントを受け取
    // るアダプターは、何もしない既定実装のままでよい。
    virtual void set_event_sink(TextInputEventSink sink) { (void)sink; }
};

class TextInputService {
public:
    explicit TextInputService(std::shared_ptr<TextInputBackend> backend = {});
    ~TextInputService();

    TextInputService(const TextInputService&) = delete;
    TextInputService& operator=(const TextInputService&) = delete;
    TextInputService(TextInputService&&) noexcept;
    TextInputService& operator=(TextInputService&&) noexcept;

    void set_backend(std::shared_ptr<TextInputBackend> backend);
    [[nodiscard]] std::shared_ptr<TextInputBackend> backend() const;
    [[nodiscard]] TextInputCapabilities capabilities() const noexcept;
    [[nodiscard]] bool supports(TextInputCapability value) const noexcept;

    // 入力セッションを開始・停止する。エラー文字列を受け取らないオ
    // ーバーロードはイベント処理用。バックエンドの詳細なエラーは
    // std::string&を取るオーバーロードで取得できる。
    [[nodiscard]] bool begin(TextInputRequest request, std::string& error);
    [[nodiscard]] bool begin(TextInputRequest request);
    [[nodiscard]] bool start(TextInputRequest request, std::string& error) {
        return begin(std::move(request), error);
    }
    [[nodiscard]] bool start(TextInputRequest request) { return begin(std::move(request)); }
    void end() noexcept;
    void stop() noexcept { end(); }
    [[nodiscard]] bool active() const noexcept;

    // 周辺のドキュメントのテキストはEditorが所有する。これらのメソ
    // ッドはネイティブIMEコンテキストの同期と監視先への状態公開だけ
    // を行い、commit()はドキュメントに文字列を二重挿入しない。
    [[nodiscard]] bool set_surrounding_text(std::string text,
                                            TextSelection selection,
                                            std::string& error);
    void set_surrounding_text(std::string text, TextSelection selection = {});
    void set_text(std::string text, TextSelection selection = {});
    [[nodiscard]] TextInputState state() const;

    // ネイティブアダプターは変換中の文字列や入力のイベントを受け取ると、これらのメソッドを呼ぶ
    // 。ワーカー、プラットフォームのイベント処理、メインのいずれのスレッドからも安全に呼べる。
    void composition_start();
    void composition_update(TextComposition composition);
    void commit(std::string text);
    void composition_commit(std::string text) { commit(std::move(text)); }
    void composition_cancel();
    void selection_changed(TextSelection selection);
    void soft_keyboard_changed(bool visible);
    void handle_event(const TextInputEventPacket& event);
    void handle_text_input(const TextInputEvent& event);

    // 返された通知先は配送中のサービスを生存さ
    // せ、サービスの破棄後は何もしなくなる。
    [[nodiscard]] TextInputEventSink event_sink() const;

    [[nodiscard]] bool set_selection(TextSelection selection, std::string& error);
    void set_selection(TextSelection selection);

    [[nodiscard]] bool request_soft_keyboard(bool visible, std::string& error);
    [[nodiscard]] bool request_soft_keyboard(bool visible = true);
    [[nodiscard]] bool show_soft_keyboard(std::string& error);
    [[nodiscard]] bool hide_soft_keyboard(std::string& error);
    [[nodiscard]] bool show_soft_keyboard();
    [[nodiscard]] bool hide_soft_keyboard();

    // 別スレッドがイベントを配送中でも安全にコール
    // バックを登録できる。subscribe()は
    // 登録順を維持し、UIの動作順を一定に保つ。
    [[nodiscard]] TextInputSubscription subscribe(TextInputCallbacks callbacks);
    [[nodiscard]] TextInputSubscription subscribe(TextInputHandler handler) {
        TextInputCallbacks callbacks;
        callbacks.on_text_input = std::move(handler);
        return subscribe(std::move(callbacks));
    }
    [[nodiscard]] TextInputSubscription connect(TextInputCallbacks callbacks) {
        return subscribe(std::move(callbacks));
    }
    void unsubscribe(TextInputCallbackId id) noexcept;
    void set_callbacks(TextInputCallbacks callbacks);
    void clear_callbacks() noexcept;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

// controller/bridgeという名前で扱うコ
// ード向けに短い別名を提供し、実装と契約は一つに保つ。
using TextInputController = TextInputService;
using TextInputBridge = TextInputService;
using TextInputConnection = TextInputSubscription;

}  // namespace cwxeditor::lib
