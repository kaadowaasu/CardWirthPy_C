#pragma once

#include "cw/lib/ui_surface.hpp"

#include "cw/lib/compat.hpp"
#include "cw/lib/documents.hpp"
#include "cw/dialog/scroll_gesture.hpp"

#include <cstdint>
#include <chrono>
#include <atomic>
#include <memory>
#include <optional>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace cw::frame { class Frame; struct ScreenshotWindows; }
union SDL_Event;

struct SDL_Renderer;

namespace cw::dialog {

struct DialogPresentationInputState;
class DialogPresentationInputGuard;

// ALL と同じプロバイダーを単体表示で使うための UI スレッド専用の受け渡
// し。アクティブ・一時ダイアログだけでなく、モードレスウィンドウも処理する。
void pump_standalone_presentations(const std::shared_ptr<::cw::frame::Frame>& frame);
void close_standalone_presentations();
bool dispatch_standalone_presentation_event(const SDL_Event& event);
// UI スレッドのウィンドウ・クライアント画像の基本処理。スクリーン
// ショット内容の選別や Frame の配置・保存方針は実装しない。
::cw::frame::ScreenshotWindows capture_presentation_windows(
    const ::cw::frame::Frame& frame, bool capture_clients);
// ダイアログ操作を所有元の Frame に遅延して渡す。入れ子のモーダルの上
// 書きも含む。共通のホスト Frame がない場合だけ false を返す。
bool post_common_dialog_action(::cw::lib::Callback callback);
// 入力による遅延更新は同じ UI キューを使い、非同期の入力利用権は保持しない。
bool post_common_dialog_input_update(::cw::lib::Callback callback);
// バックエンドのコールバックは別スレッドに届く場合があるため、ここで所有元 Frame を保持する。
std::shared_ptr<::cw::lib::documents::Ticket> request_presentation_document(
    const ::cw::lib::documents::Request& request,::cw::lib::documents::Completion completion);
std::shared_ptr<::cw::lib::documents::Ticket> write_presentation_document(
    const ::cw::lib::documents::Document& target,std::vector<std::uint8_t> bytes,
    ::cw::lib::documents::Completion completion);

// 共通 Engine ダイアログ自身が完全な表示を公開する
// 。OS のホストは倍率適用、中央配置、合成、ポインター・
// 文字入力の変換だけを行い、簡略化した配置へ置き換えない。
enum class DialogPresentationAction {
    Accept,
    Cancel,
    Select,
    Previous,
    Next,
    PreviousGroup,
    NextGroup,
    Toggle,
    Create,
    Auxiliary,
    // Python の TouchTools 操作。共通 Presentation
    // の装飾処理で組み込み、ダイアログ固有の操作分岐より先に処理する。
    Screenshot,
    ScreenshotCards,
    CopyDetail,
    // ダブルクリック・Enter による起動は選択と区別する。
    // ホストは転送だけを行い、起動の意味は共通モデルが決める。
    Activate,
};

enum class DialogPresentationKey {
    Left,
    Right,
    Up,
    Down,
    Enter,
    Space,
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
    B,
    D,
    E,
    I,
    L,
    O,
    R,
    S,
    T,
    X,
    A,
    C,
    Digit1, Digit2, Digit3, Digit4, Digit5, Digit6, Digit7, Digit8, Digit9,
    V, Home, End, Delete, Tab, F, Backspace,
    PageUp, PageDown,
    // 既存のホスト・デバッグ用コマンドの値を振り直さず、末尾に追加する。
    G, H, J, K, M, N, P, Q, U, W, Y, Z, Digit0,
    Escape,
    Insert,
    ContextMenu,
    Underscore, Minus,
};

// SDL の受け渡しは単体版と ALL で共有する。英字・ア
// クセスキーを一つの表にまとめ、ホスト別のキー表を作らない。
[[nodiscard]] std::optional<DialogPresentationKey> presentation_key_from_sdl(std::uint32_t key) noexcept;
[[nodiscard]] char presentation_key_ascii(DialogPresentationKey key) noexcept;

using DialogPresentationModifiers = std::uint32_t;
inline constexpr DialogPresentationModifiers DialogPresentationModifierShift = 1U;
inline constexpr DialogPresentationModifiers DialogPresentationModifierControl = 2U;
inline constexpr DialogPresentationModifiers DialogPresentationModifierAlt = 4U;

[[nodiscard]] std::chrono::milliseconds dialog_page_repeat_interval() noexcept;

enum class DialogCursorShape { Arrow, Back, Forward, Finger };
struct DialogPresentationCursor {
    DialogCursorShape shape{DialogCursorShape::Arrow};
    std::string resource{};
    bool operator==(const DialogPresentationCursor&) const = default;
};
// モデル側だけの検索。ネイティブカーソルの作成は UI ホストだけが行う。
[[nodiscard]] DialogPresentationCursor resolve_presentation_cursor(
    DialogCursorShape shape,const std::string& skin_directory={},
    const std::string& base_directory="Data/SkinBase");

// CardWirthPy_C の Engine ダイアログ固有の拡張
// 。すべてのダイアログの下部バーとその押しボタンを、Python の
// 配置よりこの論理画素数だけ高くする。全ダイアログで一つの値を使う。
inline constexpr ::cw::lib::Integer kDialogFooterExtraHeight = 4;

struct DialogPresentationControl {
    DialogPresentationAction action{DialogPresentationAction::Accept};
    int index{-1};
    float x{};
    float y{};
    float width{};
    float height{};
    bool enabled{true};
    // スライダーや色帯などの連続操作コントロールは、共通プロバ
    // イダーの規則で押下・ドラッグ・解放の座標を受け取る。ホ
    // ストがコントロールの状態遷移を別途実装してはならない。
    bool continuous_pointer{};
    // デバッガーなどの永続的なツール画面用の任意の意味情報。ホ
    // ストはアクセシビリティ用テキストやツールチップとして公開
    // できるが、別のコントロール配置を作るためには使わない。
    std::string label{};
    std::string tooltip{};
    // サイズ変更の区切り用の追加動作。ドラッグを先に終了し、移動のないダブル
    // クリックで操作を実行する。通常のスライダーは解放時だけの動作を保つ。
    bool activate_on_double_click{};
    DialogPresentationCursor cursor{};
    // 押しボタンは共通のホバー表示を選択できる。Python/wx
    // と同様にマウスが重なると色を変える。一覧、入力欄
    // 、大きなヒット領域は false のままにする。
    bool hover_highlight{};
    // 連続操作中もタッチ長押しのコンテキスト操作（右クリック）を受けるかを
    // 指定する。スピン・スライダーは保持中の編集が取り消されるため false
    // 。カード枠は true とし、長押しでカード情報を表示する。
    bool long_press_context{};

    bool operator==(const DialogPresentationControl&) const = default;
};

[[nodiscard]] inline std::optional<DialogPresentationAction> dialog_release_action(
    const DialogPresentationControl& pressed,const DialogPresentationControl* released,
    float x,float y,int clicks) {
    if(!released || !pressed.enabled || !released->enabled ||
       pressed.action!=released->action || pressed.index!=released->index) return {};
    if(pressed.continuous_pointer) {
        if(clicks<=1 || !pressed.activate_on_double_click || !released->activate_on_double_click ||
           x<pressed.x || y<pressed.y || x>=pressed.x+pressed.width || y>=pressed.y+pressed.height) return {};
        return DialogPresentationAction::Activate;
    }
    return clicks>1 && pressed.action==DialogPresentationAction::Select
        ?DialogPresentationAction::Activate:pressed.action;
}

// Presentation の共通の更新時刻の規則。静的なプロバイダーは
// ゼロを要求する。アニメーション・非同期のプロバイダーは、追加入力がなく
// ても、モバイル・Web を含む全ホストで素材や設定の更新期限を保つ。
[[nodiscard]] constexpr std::uint32_t platform_refresh_interval(
    const std::uint32_t requested_ms) noexcept {
    return requested_ms;
}

[[nodiscard]] constexpr bool platform_refresh_throttling() noexcept {
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
    return true;
#else
    return false;
#endif
}

// SDL の start/length は未確定文字列内の文字位置であ
// り、確定済み文字列の置換位置ではない。全ホストでそのまま保持する。
struct DialogTextEditingEvent {
    std::string text;
    bool composition{};
    int start{-1};
    int length{};
};

struct DialogPresentation {
    std::string route{};
    std::string title{};
    std::shared_ptr<const ::cw::lib::Surface> surface{};
    std::vector<DialogPresentationControl> controls{};
    bool wants_text_input{};
    std::string input_text{};
    std::string composition{};
    int composition_start{-1}, composition_length{};
    std::uint32_t refresh_interval_ms{};
    bool throttle_refresh{platform_refresh_throttling()};
    // 読み取り専用の一覧・選択欄は物理キーボード・IME の文字を移動
    // に使うが、タッチ専用端末でソフトウェアキーボードは要求しない。
    bool text_input_hardware_only{};
    // 描画面・コントロール座標での配置サイズ変更を選択可能にする。デバ
    // ッガーを含め既定は固定で、既存のフォント画像を拡大縮小しない。
    bool resizable{};
    ::cw::lib::Size minimum_size{};
    // 単体版 SDL ウィンドウは縦横比を保ち、タイト
    // ルバーと状態・タスクバーを除いた作業領域の高さに
    // クライアント高を制限できる。ALL 内のダイアロ
    // グも、配置処理で同じ利用可能高への調整を行う。
    bool fit_work_area_height{};
    // 固定の高 DPI 画像を作業領域に収めるため小数倍率で縮
    // 小する場合、元の画素を丸ごと落とさず、最終表示にフィル
    // ターを適用する。プロバイダーの文字画像生成は一度だけ。
    bool smooth_fitted_scaling{};
    // デバッガーのウィンドウ・ダイアログは自身の DPI を使
    // い、通常のモーダル用 TouchTools を付けない。
    bool debugger{};
    // 共通の保持型コンポーネントが渡す、画素単位の更新
    // 領域。空なら受信側は全面のフレームとして扱う。
    std::vector<::cw::lib::Rect> damage{};
    std::uint64_t surface_revision{};
};

// ホストが公開済みの共通 Presentation のコントロールを、
// そのまま読み取り専用で自動操作側に提供する。登録処理はダイアログを
// 再構築せず、別の UI 経路や遠隔検査のための再描画を追加しない。
struct DialogDebugSnapshot {
    std::uintptr_t owner{};
    std::string route{};
    std::string title{};
    ::cw::lib::Size surface_size{};
    std::vector<DialogPresentationControl> controls{};
};
void publish_dialog_debug_snapshot(const void* owner,
                                   const DialogPresentation& presentation) noexcept;
void clear_dialog_debug_snapshot(const void* owner) noexcept;
void clear_dialog_debug_snapshots() noexcept;
[[nodiscard]] std::vector<DialogDebugSnapshot> dialog_debug_snapshots();

[[nodiscard]] constexpr bool dialog_presentation_can_upload_damage(
    bool texture_recreated, bool tooltip_composed, bool previous_tooltip_composed,
    std::uint64_t surface_revision, std::uint64_t uploaded_surface_revision,
    std::size_t damage_count) noexcept {
    return !texture_recreated && !tooltip_composed && !previous_tooltip_composed &&
           surface_revision > uploaded_surface_revision && damage_count > 0;
}

// 単体版ホストと検査で使う純粋な配置計算。
// 利用可能高はタイトル・上部バーと状態・タ
// スクバーを除いたクライアント作業領域。
[[nodiscard]] ::cw::lib::Size fit_dialog_presentation_to_height(
    const DialogPresentation& presentation, ::cw::lib::Size source,
    ::cw::lib::Integer available_height,
    ::cw::lib::Integer available_width = 0) noexcept;

// UIの実装は一つに統一し、
// 別経路の実装を設けない。
[[nodiscard]] DialogPresentationCursor dialog_cursor_at(
    const std::vector<DialogPresentationControl>& controls,float x,float y);
// 単体版と ALL で共有する、UI スレッドの寿命・所有関係。検査では最終的
// な適用操作を注入する。このオブジェクトの構築では SDL を初期化しない。
class DialogPresentationCursorHost {
public:
    explicit DialogPresentationCursorHost(
        std::function<void(const DialogPresentationCursor&)> apply={});
    ~DialogPresentationCursorHost();
    DialogPresentationCursorHost(const DialogPresentationCursorHost&)=delete;
    DialogPresentationCursorHost& operator=(const DialogPresentationCursorHost&)=delete;
    // マウスが重なるボタン（hover_highlight）が変わ
    // ったら true を返し、ホストに強調表示を描き直させる。
    bool update(const std::vector<DialogPresentationControl>& controls,float x,float y);
    void clear();
    // マウス位置にある有効な hover_highlight コントロールを描画面座標で表す。
    // 両ホストとも render_dialog_hover_highlight() で描く。
    [[nodiscard]] const std::optional<DialogPresentationControl>& hovered_button() const noexcept {
        return hovered_;
    }
private:
    std::function<void(const DialogPresentationCursor&)> apply_;
    DialogPresentationCursor current_;
    std::uint64_t generation_{};
    std::optional<DialogPresentationControl> hovered_;
};
// 表示済みダイアログのテクスチャーの上に、押しボタンの共通
// ホバー表示を描く。origin/scale で描画面座標
// を描画座標に変換し、単体版と ALL で同じ実装を使う。
void render_dialog_hover_highlight(SDL_Renderer* renderer, const DialogPresentationControl& control,
                                   float origin_x, float origin_y, float scale_x, float scale_y);
class DialogSideNavigation {
public:
    void reset() noexcept {ready_=false;}
    void append(DialogPresentation& view,const ::cw::lib::Rect& panel,bool enabled,int index);
private:
    bool ready_{};
    std::uint64_t generation_{};
    DialogPresentationCursor back_,forward_;
};

[[nodiscard]] constexpr bool dialog_text_input_enabled(
    bool requested, bool hardware_only, bool keyboard_connected) noexcept {
    return requested && (!hardware_only || keyboard_connected);
}

// 説明のヒット判定・合成を共有する。ホストはホバーの寿命だけを持ち、
// 単体版も ALL も別のツールチップ描画器や判定配置を作らない。
class DialogPresentationTooltip {
public:
    bool update(const DialogPresentation& presentation, float x, float y);
    bool clear() noexcept;
    [[nodiscard]] bool active() const noexcept { return !text_.empty(); }
    [[nodiscard]] std::shared_ptr<const ::cw::lib::Surface> compose(const DialogPresentation& presentation) const;
private:
    std::string text_;
    float x_{}, y_{};
};

// 一つの論理 Presentation をまとめて最終化する。
// 画像の画素とコントロールのヒット領域には常に同じウィンドウ倍
// 率を適用し、OS 別・コントロール別の倍率経路を作らない。
enum class DialogPresentationScalePolicy {
    GameWindow,
    // Python のデバッガーダイアログは、Engine の二倍・四倍のカード画面倍率ではなく ppis()/dpi_level
    // を使う。すべての OS で同じ共通 Presentation を保つ。
    DebuggerDpi,
};
void finalize_logical_presentation(
    DialogPresentation& presentation,
    const std::shared_ptr<::cw::lib::Surface>& logical_surface,
    DialogPresentationScalePolicy policy = DialogPresentationScalePolicy::GameWindow);

// 共通 Presentation に論理的なコントロールを一つ描画・登録する
// 。全ホストが同じ画素とヒット領域を受け取り、最終のウィンドウ倍率は finalize_logical_presentation()
// でだけ適用する。
void add_presentation_control(
    DialogPresentation& presentation,
    const std::shared_ptr<::cw::lib::Surface>& logical_surface,
    const ::cw::lib::String& label, const ::cw::lib::Rect& rect,
    DialogPresentationAction action, int index, bool enabled = true,
    const ::cw::lib::String& font_role = "button",
    bool center_vertically = false);

// 完全な共通描画面を所有するダイアログモデルが実装する。ホ
// ストの外枠はこのインターフェースだけを使い、具象ダイアロ
// グ型で分岐してフィールドやコントロールを再作成しない。
class DialogPresentationProvider {
public:
    // ALL仕様: 子・孫ダイアログへ引き継ぐ配置属性。ダイアログクラスは共通。
    bool debugger_descendant{};
    DialogPresentationProvider();
    DialogPresentationProvider(const DialogPresentationProvider& other);
    DialogPresentationProvider& operator=(const DialogPresentationProvider& other);
    DialogPresentationProvider(DialogPresentationProvider&& other) noexcept;
    DialogPresentationProvider& operator=(DialogPresentationProvider&& other) noexcept;
    virtual ~DialogPresentationProvider();
    // すべてのホストはこの一つの入口からプロバイダーを
    // 呼ぶ。再入・並行入力を拒否し、処理中に登録した非
    // 同期コールバックが終了するまで遮断する。描画、ア
    // ニメーション、音声通知はこの制限を取得しない。
    [[nodiscard]] bool try_begin_dialog_presentation_input() noexcept;
    void end_dialog_presentation_input() noexcept;
    [[nodiscard]] bool dialog_presentation_input_blocked() const noexcept;
    [[nodiscard]] std::shared_ptr<void> hold_dialog_presentation_input() noexcept;
    [[nodiscard]] virtual DialogPresentation dialog_presentation() = 0;
    virtual void dialog_presentation_action(DialogPresentationAction action,
                                            int index) = 0;
    // 入力の修飾キーは受け渡し用の情報であり、OS ごとの選択 UI ではない。
    virtual void dialog_presentation_modifiers(DialogPresentationModifiers modifiers) {
        (void)modifiers;
    }
    // ホストのフォーカスは、ポインター捕捉の取り消しと区別する。モードレス編集画面
    // は、別のネイティブコントロール経路なしで、ここから編集中の値を確定できる。
    virtual void dialog_presentation_focus(bool focused) {
        if(!focused) dialog_presentation_pointer_cancel();
    }
    virtual void dialog_presentation_text(const std::string& text,
                                          bool composition) {
        (void)text;
        (void)composition;
    }
    virtual void dialog_presentation_text_event(const DialogTextEditingEvent& event) {
        m_last_text_event=event;
        dialog_presentation_text(event.text,event.composition);
    }
    [[nodiscard]] const DialogTextEditingEvent& last_dialog_text_event() const noexcept {return m_last_text_event;}
    // 全ダイアログ共通（Py/wxと意図的に異なる）: 開いた直後の初期フォーカスで
    // 文字入力を開始しない。入力欄に初期フォーカスがあるとスマホでソフト
    // キーボードが開いてしまうため、そのダイアログをユーザーが押す、または
    // キーを押すまでは wants_text_input を無視する。ハードウェアキーボード
    // 専用の先行入力（text_input_hardware_only）はキーボードを開かないので対象外。
    void note_dialog_presentation_user_input() noexcept {m_presentation_user_input=true;}
    [[nodiscard]] bool dialog_presentation_text_input_requested(
        const DialogPresentation& presentation) const noexcept {
        return presentation.wants_text_input &&
            (presentation.text_input_hardware_only || m_presentation_user_input);
    }
    // ドロップは UTF-8 パスの一群として扱い、文字入力やファイルご
    // との起動にはしない。外部・埋め込みの両ホストが同じ操作を転送する。
    virtual void dialog_presentation_drop_files(const std::vector<std::string>& paths) {
        (void)paths;
    }
    virtual void dialog_presentation_escape() {
        dialog_presentation_action(DialogPresentationAction::Cancel, -1);
    }
    virtual void dialog_presentation_middle(float x, float y, bool pressed) {
        (void)x; (void)y; (void)pressed;
    }
    virtual void dialog_presentation_pointer(DialogPresentationAction action,
                                             int index, float x, float y,
                                             bool pressed) {
        (void)x;
        (void)y;
        if (!pressed) dialog_presentation_action(action, index);
    }
    // 捕捉の取り消しは解放や Cancel ボタンの起動ではない。編
    // 集中の値・選択を保ち、ドラッグ・リピートの捕捉だけを解除する。
    virtual void dialog_presentation_pointer_cancel() {}
    virtual void dialog_presentation_resize(::cw::lib::Size size) { (void)size; }
    // 描画面内のポインター入力はモデルが管理する。ホストはすべての右クリックを
    // Cancel に変換したり、カード固有のヒット判定を実装したりしない。
    virtual void dialog_presentation_hover(float x, float y) {
        (void)x; (void)y;
    }
    virtual void dialog_presentation_context(float x, float y) {
        (void)x; (void)y;
        dialog_presentation_action(DialogPresentationAction::Cancel, -1);
    }
    virtual void dialog_presentation_key(DialogPresentationKey key,
                                         bool pressed,
                                         DialogPresentationModifiers modifiers) {
        (void)key;
        (void)pressed;
        (void)modifiers;
    }
    // ツリーなどでは Enter と主ボタンのクリックは異なる。意味
    // はモデルが決め、単体版とホスト内表示の両方がこのフックを使う。
    virtual void dialog_presentation_enter(DialogPresentationModifiers modifiers) {
        (void)modifiers;
        dialog_presentation_action(DialogPresentationAction::Accept, -1);
    }
    // ホイール・トラックパッド入力はモデル操作として渡す。
    // 座標はコントロール領域と同じ Presentation
    // の描画面内、lines は上方向を負とする。既定
    // では単純なダイアログの従来のページ選択動作を保つ。
    virtual void dialog_presentation_scroll(float x, float y, int lines) {
        (void)x;
        (void)y;
        if (lines < 0)
            dialog_presentation_action(DialogPresentationAction::Previous, -1);
        else if (lines > 0)
            dialog_presentation_action(DialogPresentationAction::Next, -1);
    }
    [[nodiscard]] virtual bool dialog_presentation_finished() const noexcept = 0;
    // 長時間動く共通ダイアログは、入力イベントなしでもワーカーの
    // 結果を受け取り得る。ホストはモデルが持つ更新番号を使い、OS
    // 固有のタイマーやダイアログ階層の再構築を追加しない。
    [[nodiscard]] virtual bool dialog_presentation_needs_refresh() const noexcept;
    // フレーム合成の直前に呼び、そのフレームに含められるモデル更
    // 新番号を取得する。dialog_presentation()
    // の実行中に届いた通知は、次のフレーム用に保留する。
    void begin_dialog_presentation_refresh() noexcept;
    virtual void dialog_presentation_refreshed() noexcept;
    // UI スレッドのモデルコールバックは、削除済み OS コントロールの無効化
    // の代わりにこれを使う。特に非同期 Add は終了時に再描画する必要がある。
    void request_dialog_presentation_refresh() noexcept {
        m_presentation_refresh_revision.fetch_add(1, std::memory_order_release);
    }
    // 各共通ダイアログは同じ保持型の描画領域コンポーネント
    // を持つ。具象ダイアログは更新矩形だけを宣言し、確保、
    // 外枠の復元、配置全体の無効化は一つの実装にまとめる。
    [[nodiscard]] ::cw::lib::ui::RetainedSurface& dialog_retained_surface();
    [[nodiscard]] const ::cw::lib::ui::RetainedSurface& dialog_retained_surface() const;
    void invalidate_dialog_presentation() noexcept;
    void invalidate_dialog_presentation(::cw::lib::Rect rect);
    void define_dialog_component(::cw::lib::String key, ::cw::lib::Rect rect);
    bool invalidate_dialog_component(const ::cw::lib::String& key);
    virtual void dialog_presentation_complete() = 0;
    // Python は copy_detail を実装するダイアロ
    // グだけに TouchTools の三番目の項目を出す。共通の
    // 装飾処理は具象型を知らずに、この機能の有無を問い合わせる。
    [[nodiscard]] virtual bool dialog_presentation_can_copy_detail() const noexcept { return false; }
    virtual void dialog_presentation_copy_detail() {}
private:
    std::shared_ptr<DialogPresentationInputState> m_presentation_input_state;
    // 進捗・DB・メディアの完了通知はホストの描画中に
    // も届く。更新番号の照合で、描画中の要求が前のフ
    // レームの確認応答によって消されないようにする。
    std::atomic_uint64_t m_presentation_refresh_revision{};
    mutable std::atomic_uint64_t m_presentation_refresh_observed{};
    std::atomic_uint64_t m_presentation_refresh_acknowledged{};
    // モデルが最新でも、タイマーやホストによって描画が発生
    // する場合がある。描画前の取得を毎回別に記録し、refreshed()
    // が古い更新番号を再利用したり、描画中に届
    // いたワーカー通知を確認済みにしたりしない。
    std::atomic_uint64_t m_presentation_refresh_observation_sequence{};
    std::atomic_uint64_t m_presentation_refresh_acknowledged_observation_sequence{};
    DialogTextEditingEvent m_last_text_event;
    bool m_presentation_user_input{};
};

// 単体版、ALL、ネイティブ Presentation アダプ
// ターが使う RAII の入口。内部のキーから操作への委譲は一
// つのガード内に収め、別の利用者操作と誤認しないようにする。
class DialogPresentationInputGuard final {
public:
    explicit DialogPresentationInputGuard(DialogPresentationProvider& provider) noexcept;
    ~DialogPresentationInputGuard();
    DialogPresentationInputGuard(const DialogPresentationInputGuard&) = delete;
    DialogPresentationInputGuard& operator=(const DialogPresentationInputGuard&) = delete;
    [[nodiscard]] explicit operator bool() const noexcept { return acquired_; }
private:
    DialogPresentationProvider* provider_{};
    DialogPresentationProvider* previous_{};
    bool acquired_{};
};

// ガードされた利用者操作中にキューの境界で呼ぶ。返す
// トークンで、コールバック終了まで元のダイアログの操
// 作を止める。音声通知など変更を伴わない通知は、ゲー
// ムスレッド待ちの間に後続入力を抑制しない。このスレ
// ッド内のスコープは自動的なキュー保持だけに影響し
// 、入力ガードや明示的な所有トークンは変更しない。
class ScopedDialogInputRetention final {
public:
    explicit ScopedDialogInputRetention(bool retain) noexcept;
    ~ScopedDialogInputRetention();
    ScopedDialogInputRetention(const ScopedDialogInputRetention&)=delete;
    ScopedDialogInputRetention& operator=(const ScopedDialogInputRetention&)=delete;
private:
    bool previous_{};
};

[[nodiscard]] std::shared_ptr<void> capture_current_dialog_input_lease() noexcept;
[[nodiscard]] bool current_dialog_input_operation_active() noexcept;
[[nodiscard]] bool current_dialog_input_from_debugger() noexcept;
[[nodiscard]] ::cw::lib::Callback retain_current_dialog_input(
    ::cw::lib::Callback callback);

struct DialogTouchToolsOptions {
    bool enabled{};
    bool party{};
};

// 最終画素単位の完全な Presentation に、Python の show_tiles
// に対応する付属表示を加える。単体版と ALL が同じ装飾処理を
// 使い、OS の小ウィンドウや別のコントロールツリーは作らない。
void decorate_dialog_touch_tools(DialogPresentation& presentation,
                                 const DialogPresentationProvider& provider);
// 検査用の純粋な方針の入口。製品側は CWPy か
// ら二つの状態を一度取得し、同じ合成処理に渡す。
void decorate_dialog_touch_tools(DialogPresentation& presentation,
                                 const DialogPresentationProvider& provider,
                                 DialogTouchToolsOptions options);
// 具象ダイアログの操作分岐より先に、共通の補助コントロ
// ールを処理する。操作を消費したら true を返す。
bool dispatch_dialog_utility_action(DialogPresentationProvider& provider,
                                    DialogPresentationAction action);

bool dispatch_dialog_utility_pointer(DialogPresentationProvider& provider,
    DialogPresentationAction action,float x,float y,bool pressed);
void update_dialog_utility_hover(DialogPresentationProvider& provider,
    const DialogPresentation& presentation,float x,float y);
void cancel_dialog_utility_pointer(DialogPresentationProvider& provider);

// SDL ウィンドウを作らず、共通ホストのサイズ変更の検証・取り消しを行う。
bool resize_dialog_presentation(DialogPresentationProvider& provider,
    const DialogPresentation& view, ::cw::lib::Size size);

// ネイティブマウスと直接のタッチ入力で共有するポインター操作。座標
// とヒット対象は同じ共通 Presentation から取得する。
class DialogPointerCapture {
public:
    using PointerRelay = std::function<void(
        const DialogPresentationControl&, float, float, bool)>;
    using ActionRelay = std::function<void(DialogPresentationAction, int)>;

    void down(DialogPresentationProvider&, const DialogPresentation&, float x, float y);
    bool move(DialogPresentationProvider&, float x, float y, bool pressed);
    void up(DialogPresentationProvider&, const DialogPresentation&, float x, float y, int clicks);
    void cancel(DialogPresentationProvider&);

    // ホスト内の描画面向けの、受け渡し方式に依存しない経路。共通
    // の捕捉処理がヒット判定と押下・ドラッグ・解放を管理し、ホス
    // トは描画面座標の変換と意味付きイベントの転送だけを行う。
    void down(const std::vector<DialogPresentationControl>&, float x, float y,
              const PointerRelay&);
    bool move(float x, float y, bool pressed, const PointerRelay&);
    void up(const std::vector<DialogPresentationControl>&, float x, float y,
            int clicks, const PointerRelay&, const ActionRelay&);
    void reset() noexcept { pressed_.reset(); }
    bool active() const noexcept { return pressed_.has_value(); }
    bool continuous() const noexcept {
        return pressed_.has_value() && pressed_->continuous_pointer;
    }
    bool long_press_context() const noexcept {
        return pressed_.has_value() && pressed_->long_press_context;
    }
private:
    std::optional<DialogPresentationControl> pressed_;
};

// 完全なパレット・独自色、HSL 平面、明度帯、RGB/HSL の数値
// 編集、確定・取り消しを持つ共通の色選択サービス。単体版と ALL は
// ホストだけが異なり、アルファは呼び出し側のコントロールが所有する。
class ColourSelectionDialog final : public ::cw::lib::Object, public DialogPresentationProvider {
public:
    explicit ColourSelectionDialog(::cw::lib::Color initial,
        std::function<std::string()> read_clipboard={},
        std::function<void(const std::string&)> write_clipboard={});
    ~ColourSelectionDialog() override;
    [[nodiscard]] DialogPresentation dialog_presentation() override;
    void dialog_presentation_action(DialogPresentationAction action,int index) override;
    void dialog_presentation_text(const std::string& text,bool composition) override;
    void dialog_presentation_text_event(const DialogTextEditingEvent& event) override;
    void dialog_presentation_key(DialogPresentationKey key,bool pressed,DialogPresentationModifiers modifiers) override;
    void dialog_presentation_pointer(DialogPresentationAction action,int index,float x,float y,bool pressed) override;
    void dialog_presentation_enter(DialogPresentationModifiers modifiers) override;
    void dialog_presentation_modifiers(DialogPresentationModifiers modifiers) override;
    void dialog_presentation_pointer_cancel() override;
    void dialog_presentation_hover(float x,float y) override;
    void dialog_presentation_context(float x,float y) override;
    [[nodiscard]] bool dialog_presentation_finished() const noexcept override { return finished_; }
    void dialog_presentation_complete() override {}
    [[nodiscard]] bool accepted() const noexcept { return accepted_; }
    [[nodiscard]] ::cw::lib::Color colour() const noexcept { return colour_; }
private:
    ::cw::lib::Color colour_{};
    double hue_{}, saturation_{}, luminance_{};
    bool finished_{}, accepted_{};
    int edit_{-1}, focus_{300}, basic_slot_{}, custom_slot_{};
    struct EditorState;
    std::unique_ptr<EditorState> editor_;
    void focus_control(int id);
    void from_rgb();
    void from_hsl();
    int component(int index) const;
    void set_component(int index,int value);
    bool commit();
};

// アクティブな Frame がホスト内表示で、指定モデルがその
// Frame の現在の表示プロバイダーそのものなら true。ア
// プリケーションの UI バックエンドはこの OS 非依存の条
// 件を使い、同じモデル用の別のネイティブダイアログを作らない。
[[nodiscard]] bool is_hosted_dialog_presentation(
    const ::cw::lib::Object* model) noexcept;

// 子モデルを公開する前でも、アクティブな Frame が
// ホスト内表示なら true。ダイアログの生成処理はこの
// 共通のモード値を使い、ホスト判定を個別に実装しない。
[[nodiscard]] bool is_hosted_presentation_host() noexcept;

// 進捗など一時的な子の表示も通常のモーダルと同じ共通モデルを
// 使うが、所有元のダイアログを置き換えず、その上に重ねる。
void publish_transient_dialog_presentation(
    std::shared_ptr<::cw::lib::Object> model) noexcept;
void clear_transient_dialog_presentation(
    const ::cw::lib::Object* model) noexcept;

// 現在のダイアログを任意の完全な共通 Presentation
// で一時的に覆い、終了後に以前のモデルへ戻す。全 OS ホスト共
// 通の入れ子モーダル処理。ワーカーからは所有元 Frame の
// UI キューへ同期送信し、ゲーム・I/O ワーカーでネイティブ
// のモーダルループを回さない。送信失敗時は呼び出し元へ代替せず
// false を返し、UI からの呼び出しはその場で実行する。
[[nodiscard]] bool show_common_modal_presentation(
    std::shared_ptr<::cw::lib::Object> model) noexcept;

[[nodiscard]] bool show_common_modal_presentation_with_owner(
    std::shared_ptr<::cw::lib::Object> model,
    std::shared_ptr<::cw::lib::Window> owner) noexcept;

// g_cwpy がアクティブな Frame を公開する前、特に初
// 回のスキン変換で必要な表示用の入口。通常と同じ Presentation
// と NativeFrameAdapter を使う。
[[nodiscard]] bool show_common_modal_presentation(
    std::shared_ptr<::cw::frame::Frame> frame,
    std::shared_ptr<::cw::lib::Object> model,
    std::shared_ptr<::cw::lib::Window> owner = {}) noexcept;

// ホストのサービスだけを提供し、編集欄とその選択はモデルが管理する。
[[nodiscard]] std::string presentation_clipboard_text();
void presentation_set_clipboard_text(const std::string& text);

}  // namespace cw::dialog
