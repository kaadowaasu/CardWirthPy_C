#pragma once

#include "components.hpp"
#include "ui_metrics.hpp"
#include "ui_modal_operation.hpp"
#include "ui_window_attributes.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cwxeditor::lib {

namespace detail {

class DispatchObserverRegistry final {
    struct State final {
        std::mutex mutex;
        std::uint64_t next_id{1U};
        std::unordered_map<std::uint64_t, std::function<void()>> listeners;
    };

public:
    [[nodiscard]] std::function<void()> subscribe(std::function<void()> listener) {
        if (!listener) return {};
        const auto state = state_;
        std::uint64_t id{};
        {
            std::scoped_lock lock(state->mutex);
            id = state->next_id++;
            if (id == 0U) id = state->next_id++;
            state->listeners.emplace(id, std::move(listener));
        }
        const std::weak_ptr<State> weak = state;
        return [weak, id] {
            if (const auto current = weak.lock()) {
                std::scoped_lock lock(current->mutex);
                current->listeners.erase(id);
            }
        };
    }

    void notify() const {
        std::vector<std::uint64_t> ids;
        const auto state = state_;
        {
            std::scoped_lock lock(state->mutex);
            ids.reserve(state->listeners.size());
            for (const auto& [id, _] : state->listeners) ids.push_back(id);
        }
        for (const auto id : ids) {
            std::function<void()> listener;
            {
                std::scoped_lock lock(state->mutex);
                if (const auto found = state->listeners.find(id);
                    found != state->listeners.end())
                    listener = found->second;
            }
            if (listener) listener();
        }
    }

private:
    std::shared_ptr<State> state_{std::make_shared<State>()};
};

}  // namespace detail

enum class ModalMode { None, Window, Application, System };
struct ProgrammaticInlineEditRequest;
struct AccessibilityActionRequest;

// 操作前の状態を完全に復元できた場合に限り送出する
// 。未知の例外を回復可能なUI障害として扱わない。
class RecoverableUiError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// 表示側の既存のモーダル入力制限下で、独立した処理を実行する。モー
// ダルの所有元が消失した場合も含め、戻る前や例外送出前に必ずワーカ
// ーの終了を待つ。処理中のUIやモデルを読み書きしてはならない。
void run_background_io_operation(
    const std::function<bool(ExternalDialogOperation)>& wait, Action work);

// 各GuiBackend::showがウィンドウを表示する直前に呼ぶ。
// モーダルダイアログもshowを経由する。Editorはここで各コント
// ロールの標準テキストメニュー（元に戻す・やり直しの履歴）を設定する。
using WindowShownHook = std::function<void(const std::shared_ptr<Window>&)>;
void set_window_shown_hook(WindowShownHook hook);
void notify_window_shown(const std::shared_ptr<Window>& window);

class GuiBackend {
public:
    virtual ~GuiBackend() = default;
    [[nodiscard]] virtual Platform platform() const noexcept = 0;
    // モデルの構築方法はすべてのホストで共通。ネイティブ資
    // 源はバックエンドがモデルを表示するときだけ確保する。
    virtual std::shared_ptr<Window> create_window();
    virtual std::shared_ptr<Dialog> create_dialog();
    virtual WidgetPtr create_component(ComponentKind kind);
    // モデルの所有関係を問い合わせる。フォーカスによる代替判定ではない。共通ウィンドウス
    // タックを持つ表示側は、そのスタックから組み込み済みコントロールの所有元を解決する。
    [[nodiscard]] virtual std::shared_ptr<Window> window_owner(const WidgetPtr& widget) const {
        return std::dynamic_pointer_cast<Window>(widget);
    }
    virtual void show(const std::shared_ptr<Window>& window) = 0;
    virtual DialogResult show_modal(const std::shared_ptr<Dialog>& dialog,
                                    const std::shared_ptr<Window>& owner) = 0;
    // 呼び出し側をブロックせずにモーダルダイアログを開始
    // ・終了する。ブラウザでは初回描画後にイベントループ
    // へ戻る必要がある。同期表示側は既定の未対応実装を
    // 使い、従来のshow_modal()を維持する。
    [[nodiscard]] virtual bool begin_modal(
        const std::shared_ptr<Dialog>&, const std::shared_ptr<Window>&) {
        return false;
    }
    virtual void end_modal(const std::shared_ptr<Dialog>&) {}
    // 非同期モーダルを終了し、セッションの解放完了を通知する。従来の
    // 同期表示側はend_modal()が戻る前に完了する。非同期バ
    // ックエンドはこのオーバーロードを実装し、ネイティブのモーダル
    // スタックが実際にセッションを解放するまで完了通知を遅らせる。
    virtual void end_modal(const std::shared_ptr<Dialog>& dialog, Action ended) {
        end_modal(dialog);
        if (ended) ended();
    }
    virtual bool run_external_dialog(const std::shared_ptr<Window>&,
                                     ExternalDialogOperation) { return false; }
    // ヘッドレスでは同期実行の意味を保つ。ネイティブ表示側は独立したフ
    // ァイルI/Oの実行中も描画とイベント処理を続け、編集は停止する。
    virtual void run_background_io(const std::shared_ptr<Window>&, Action work) {
        if (work) work();
    }
    // ネイティブ資源を破棄せずにモデルウィンドウを一時的に非表
    // 示にする。ホバー表示はポインターの遷移ごとに使用する。
    virtual void hide(const std::shared_ptr<Window>& window) {
        if (window) window->set_visible(false);
    }
    // ALLでは組み込みのモードレスダイアログとその所有ウィンドウを閉じず
    // に隠す。restore_window()で再表示し、最前面に戻す。
    virtual void minimize_window(const std::shared_ptr<Window>& window) {
        if (!window) return;
        hide(window);
        window->minimized = true;
        notify_minimized_changed();
    }
    virtual void restore_window(const std::shared_ptr<Window>& window) {
        if (!window) return;
        window->minimized = false;
        show(window);
        notify_minimized_changed();
    }
    // ダイアログの最小化、復元、または最小化中の終了後に呼ぶ。ALL
    // は最小化中のダイアログ一覧をナビゲーション欄に保持する。
    std::function<void()> on_minimized_changed;
    void notify_minimized_changed() {
        const auto callback = on_minimized_changed;
        if (callback) callback();
    }
    // 開いている組み込みダイアログ（表示中または最小化中）を背面から前面の順に返す。
    [[nodiscard]] virtual std::vector<std::shared_ptr<Window>> hosted_dialogs() const {
        return {};
    }
    virtual void close(const std::shared_ptr<Window>& window) = 0;
    // ホスト内で動くフロントエンドは、ネイティブ子ウィンドウをホスト内に配
    // 置できる。共通実装とヘッドレス実装ではモデルの矩形だけを保持する。
    virtual void set_window_bounds(const std::shared_ptr<Window>& window, Rect bounds) {
        if (window) window->set_bounds(bounds);
    }
    // 最上位のWindow::boundsはローカルなクライアント領域
    // の配置にも使うため、原点が0,0の場合がある。ネイティブバックエ
    // ンドは位置の保存用に実際のデスクトップ上の矩形を別途公開する。
    [[nodiscard]] virtual Rect window_bounds(const std::shared_ptr<Window>& window) const {
        return window ? window->bounds() : Rect{};
    }
    virtual void constrain_window_to_host(const std::shared_ptr<Window>&) {}
    // 実行時のアダプターの結果。Pendingには未生成のウィンドウも
    // 含む。未対応の属性を示すものであり、見た目の検証結果ではない。
    [[nodiscard]] virtual WindowAttributeReport window_attribute_report(
        const std::shared_ptr<Window>&) const { return {}; }
    // 関連するモデル変更をまとめ、新しい状態がすべて準備でき
    // るまでネイティブ側で旧画面を維持できるようにする。既
    // 定実装でも、終了時に全変更を反映した一度の再描画を要
    // 求するため、非ネイティブのバックエンドで利用できる。
    virtual void set_redraw(const WidgetPtr& widget, bool enabled) {
        if (enabled && widget) request_redraw(widget);
    }
    virtual void request_redraw(const WidgetPtr& widget, std::optional<Rect> dirty = std::nullopt) = 0;
    // 通常のEditorの操作部やフォームの文字にだけ適用する。明示的なFontSpec
    // と独自Canvas描画は指定されたサイズを維持する。
    virtual void set_ui_text_scale(float value) {
        set_editor_ui_text_scale(value);
    }
    virtual void focus(const WidgetPtr& widget) = 0;
    // 処理側は表示側の入力状態にアクセスせず、イン
    // ライン編集の開始を要求する。その表示機能を持
    // たないバックエンドでは安全に要求を拒否する。
    [[nodiscard]] virtual bool request_programmatic_inline_edit(
        const ProgrammaticInlineEditRequest&) { return false; }
    // 編集したモデルの再構築や再適用の前に、編集中の内容を確定または取り消
    // す必要がある（DのTableEdit/TreeEditの確定・取消処
    // 理）。InlineEditStateMachineは表示側だけが所
    // 有し、呼び出し側は対象の所有元を指定して確定または取消を要求する。
    [[nodiscard]] virtual bool finish_programmatic_inline_edit(
        const WidgetPtr&, bool) { return false; }
    // owner上で表示側が所有するインライン編集が開いている間はtrue。こ
    // の間、owner自身のキー・ポインターハンドラーは入力を編集部に任せる。
    [[nodiscard]] virtual bool programmatic_inline_edit_active(
        const WidgetPtr&) const { return false; }
    // ネイティブのアクセシビリティ連携は、正規化済みの操作を
    // 所有元の表示側に渡す。ヘッドレスや操作連携を持たない環
    // 境では、別のコントロールモデルを作らず安全に拒否する。
    [[nodiscard]] virtual bool request_accessibility_action(
        const AccessibilityActionRequest&) { return false; }
    // 現在キーボードフォーカスを持つネイティブコントロールのモデルを返す。ネイテ
    // ィブのフォーカスを公開しないバックエンドは、既定の空の結果を使ってよい。
    [[nodiscard]] virtual WidgetPtr focused_widget() const { return {}; }
    // 現在ポインター入力を所有する論理ウィンドウ。遠隔操作ではネイテ
    // ィブのHWND/SDL_Windowではなくこれを使い、モーダ
    // ルダイアログと単一描画面のホストに共通の入力規則を適用する。
    [[nodiscard]] virtual std::shared_ptr<Window> active_input_window() const {
        return window_owner(focused_widget());
    }
    // DのforceFocusは、指定された場合に対象のShellをアクテ
    // ィブにしてからフォーカスを移す。それ以外では、そのShellがすでに
    // アクティブな入力先の場合だけ移す。この規則を共通GUI層に集約する。
    [[nodiscard]] bool focus_control(const WidgetPtr& widget,
                                    const bool shell_activate) {
        if (!widget) return false;
        const auto owner = window_owner(widget);
        if (shell_activate) {
            if (owner) {
                restore_window(owner);
                // ウィンドウの復元はホスト側への通知を伴う場合がある。そのコールバックで切
                // り離されたり置き換えられたりしたコントロールにはフォーカスを戻さない。
                if (window_owner(widget) != owner) return false;
            }
        } else if (owner != active_input_window()) {
            return false;
        }
        focus(widget);
        return true;
    }
    // 単調増加する共通フォーカスのリビジョン。操作中にプロバ
    // イダーやモーダルの再入コールバックが別のコントロールを
    // 選択した場合、古いフォーカスを復元しないために使う。
    [[nodiscard]] virtual std::uint64_t focus_revision() const noexcept { return 0U; }
    // 一つの論理ウィンドウが所有する一時的な入力状態をすべて停止す
    // る。保持中のEditorページが覆われた場合や、外部モーダル
    // が入力を遮る場合にホストが呼ぶ。共通GUI層に集約してWindows/Web/Android
    // ごとに取消経路を分けない。追加のポインターや
    // IME状態を持つ表示側は実装を上書きする。
    virtual void suspend_input(const std::shared_ptr<Window>& window) {
        if (window && window_owner(focused_widget()) == window) focus({});
    }
    // メニュー操作には、ネイティブのメニューコマンド送出時点
    // の修飾キー状態が必要（Shiftはイベントテンプレート
    // の挿入を置換に変えるなど）。未対応のバックエンドは無修
    // 飾の状態を維持する。各プラットフォームで実装し、Editor
    // 側にはネイティブのキーAPIを持ち込まない。
    [[nodiscard]] virtual Modifiers current_modifiers() const noexcept {
        return Modifiers::None;
    }
    // DはMainWindowのKeyDownFilterをDisplay
    // 全体に設定する。共通表示側では、まずフォーカス中のテキスト
    // とコントロールのハンドラーを実行し、未処理のキーをこのコー
    // ルバックへ渡した後、範囲内のメニューによる代替処理へ進む。
    void set_application_key_filter(
        std::function<bool(Key, Modifiers)> filter) {
        application_key_filter_ = std::move(filter);
    }
    // 物理的なホストイベントと同じ通知経路へ、正規化済みのキーボードイベン
    // トを注入する。通信/UIテストでは、メニューコマンドの直接実行では
    // なくこれを使い、フォーカスの振り分けとアクセラレーターを検証する。
    [[nodiscard]] virtual bool dispatch_key_event(
        const std::shared_ptr<Window>&, KeyEvent, bool = true) {
        return false;
    }
    virtual void capture_pointer(const WidgetPtr& widget, bool capture) = 0;
    // 物理的なマウス・タッチ入力と同じ共通状態機械へ、正
    // 規化済みのポインターイベントを一つ注入する。座標は
    // 選択した論理ウィンドウのクライアント座標。ポインタ
    // ー入力を通知できないバックエンドは安全に拒否する。
    [[nodiscard]] virtual bool dispatch_pointer_event(
        const std::shared_ptr<Window>&, PointerEvent, bool = false,
        unsigned = 1U) { return false; }
    virtual void show_context_menu(const std::shared_ptr<Menu>& menu, const WidgetPtr& owner, Point position) = 0;
    virtual bool pump_events(bool wait) = 0;
    // 入れ子のモーダル待機の共通処理。イベント処理後の通知もこの経路
    // で行い、ホストと決定的なテストの処理が分かれないようにする。
    [[nodiscard]] bool pump_modal_iteration() {
        const bool alive = pump_events(true);
        if (alive) modal_pump_iteration_completed();
        return alive;
    }
    virtual void wake_event_loop() = 0;
    // 最外層のネイティブイベントの配送成功を購読する。返された関数で
    // 購読を解除する。ネイティブ配送のないバックエンドは完了フック
    // を呼ばない。空のポーリングやタスクキューの消化では呼ばない。
    [[nodiscard]] virtual std::function<void()>
    subscribe_successful_native_event_dispatch(std::function<void()> listener) {
        return native_event_dispatch_observers_.subscribe(std::move(listener));
    }
    // 共通の入れ子モーダルループの各反復を、空のポーリングも含めて購読す
    // る。モーダルがイベントループを占有している間も動く必要があるタイマ
    // ーは、コールバックをUIタスクとして扱わずにこの通知を利用できる。
    [[nodiscard]] virtual std::function<void()>
    subscribe_modal_pump_iteration(std::function<void()> listener) {
        return modal_pump_iteration_observers_.subscribe(std::move(listener));
    }
    // プラットフォームのイベントループと描画を所有するALLホストは、これ
    // らのフックでネイティブイベントを一つ配送し、フレーム表示前にEditor
    // を合成する。単体版やネイティブバックエンドは既定実装を使う。
    virtual bool dispatch_native_event(const void*) { return false; }
    virtual void render_hosted() {}
    // 単一描画面のホストから借用した論理キャンバスのサイズを変更する
    // 。既定では何もしない。SDL組み込みALLでは、UIを横に引
    // き伸ばさずにEditorの幅に合わせて再配置するために使う。
    virtual void set_hosted_canvas_size(Size) {}
    // 最終的なテクスチャ配置はホストが管理する。バックエンドで拡大縮
    // 小や移動を重複計算せず、入力元からレンダラーへの座標変換をネイ
    // ティブ文字入力と共有する。空を指定するとレンダラー座標を直接使
    // う。配置変更後のイベント配送や描画の前にUIスレッドで呼ぶ。
    void set_host_coordinate_map(CoordinatePointMap map) {
        host_coordinate_map_ = std::move(map);
    }
    // 同期モーダルは、借用した描画面の所有ホストへ一反復分の処理を委
    // ねる。ホストは通常の座標変換でネイティブイベントを配送し、アプ
    // リケーション全体を合成・表示する。falseは終了を要求し、
    // 入れ子のダイアログは入れ子の反復を呼べる。設定・置換はUIス
    // レッドだけで行い、空を指定すると単体版のイベント処理に戻る。
    void set_host_iteration(std::function<bool()> iteration) {
        host_iteration_ = std::move(iteration);
    }
    // 最後に完成したクライアント領域のフレームを読み出す。ウィンドウの表示・
    // アクティブ化や再描画は行わない。UIスレッドで呼ぶ。別の場所が合成を管
    // 理するホストなど、フレームを取得できない場合はnulloptを返す。
    [[nodiscard]] virtual std::optional<ImageFrame> completed_frame(
        const std::shared_ptr<Window>&) { return std::nullopt; }
    [[nodiscard]] virtual std::vector<ScreenInfo> screens() const = 0;
protected:
    // 製品のイベントループとホストのテスト用構成は同じ通知登録先を共有し、
    // APIテストでMainWindowが使う監視通知の境界を検証する。
    void successful_native_event_dispatch_completed() {
        native_event_dispatch_observers_.notify();
    }
    virtual void modal_pump_iteration_completed() {
        modal_pump_iteration_observers_.notify();
    }
    CoordinatePointMap host_coordinate_map_;
    std::function<bool()> host_iteration_;
    std::function<bool(Key, Modifiers)> application_key_filter_;
    [[nodiscard]] std::optional<bool> iterate_host() {
        const auto callback=host_iteration_; // コールバックは自身を置換または解除する場合がある。
        return callback ? std::optional<bool>{callback()} : std::nullopt;
    }
private:
    detail::DispatchObserverRegistry native_event_dispatch_observers_;
    detail::DispatchObserverRegistry modal_pump_iteration_observers_;
};

// モデルの準備失敗や元のウィジェットの親変更があっても、再描画の
// 停止を必ず解除する。終了処理が済むまで両端の所有権を保持する。
class ScopedRedraw final {
public:
    ScopedRedraw(std::shared_ptr<GuiBackend> gui, WidgetPtr widget);
    ~ScopedRedraw() noexcept;
    ScopedRedraw(const ScopedRedraw&) = delete;
    ScopedRedraw& operator=(const ScopedRedraw&) = delete;
private:
    std::shared_ptr<GuiBackend> gui_;
    WidgetPtr widget_;
};

}  // namespace cwxeditor::lib
