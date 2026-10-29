#pragma once

#include "ui_chrome_input.hpp"
#include "ui_edit_focus.hpp"
#include "ui_mouse_input.hpp"
#include "ui_touch_input.hpp"
#include "window_host.hpp"
#include <unordered_set>

namespace cwxeditor::lib {
class WindowStackStateMachine;

enum class OwnedWindowKind { Modeless, Modal };
enum class OwnedWindowCenter { Never, Owner, OwnerOrHost };

// 単体版、ALL、全ネイティブアダプターで共通の、モデル上の所有元
// ・配置の契約。呼び出し側はshow()前に論理ウィンドウを設定し
// 、アダプターは決定済みの所有元と配置を反映するだけ。所有元がなけ
// ればcenter_on_ownerは自動的にfalseとなる。保
// 存済みの座標を通常の中央配置より優先する場合もfalseを渡す。
void configure_owned_window(
    Window& window,
    const std::shared_ptr<Window>& owner,
    OwnedWindowKind kind = OwnedWindowKind::Modeless,
    OwnedWindowCenter center = OwnedWindowCenter::OwnerOrHost);

void configure_owned_window(
    const std::shared_ptr<Window>& window,
    const std::shared_ptr<Window>& owner,
    OwnedWindowKind kind = OwnedWindowKind::Modeless,
    OwnedWindowCenter center = OwnedWindowCenter::OwnerOrHost);

struct WindowInputLifecycleContext {
    EditFocusContext edit;
    HostedCaptionStateMachine& caption;
    ChromeInputStateMachine& chrome;
    MouseInputStateMachine& mouse;
    TouchInputStateMachine& touch;
    MenuPopupStateMachine& menu;
    HoverStateMachine& hover;
    std::shared_ptr<DragDropStateMachine> drag_drop;
    // 入力配送と同じモーダル判定を使い、バックエンドごとに別のモーダル/
    // フォーカススタックは持たない。nullは独立した非モーダル環境用。
    const ModalStateMachine* modal{};
    std::function<void(const std::shared_ptr<Window>&)> redraw;
    // モードレスの子が、生存する所有元へ論理フォーカスを正常
    // に復元した後だけ呼ぶ。ネイティブホストは所有元を前面に
    // 戻し、OSに無関係なウィンドウを選ばせないために使う。
    std::function<void(const std::shared_ptr<Window>&)> activate_native;
};

struct WindowTextScaleContext {
    WindowInputLifecycleContext input;
    WindowStackStateMachine& stack;
    std::function<void()> refresh_metrics;
    std::function<std::vector<WindowTextScaleSurface>()> capture;
    // アダプターは取得済みのネイティブ世代を検証し、矩形と制限だけを
    // 反映する。このコールバックではUI規則や子の順序を扱わない。
    std::function<bool(const std::shared_ptr<Window>&,
        const std::optional<WindowTextScalePlan>&)> realize;
    std::function<void(const std::shared_ptr<Window>&)> redraw;
    std::function<void()> publish_input;
    // 反映・再描画のコールバック後も含め、上で取得した物理資源の
    // 世代を検証する。論理的な所有関係は共通操作で確認する。アダ
    // プターに別のフォーカス・モーダル規則を設けるものではない。
    std::function<bool(const std::shared_ptr<Window>&)> live;
};
class WindowTextScaleStateMachine {
public:
    // 再入した最新の要求を優先する。入れ子の倍率変更後に、古い呼び出し
    // が後続ウィンドウのサイズを変えたり、フォーカスを再公開したりして
    // はならない。コールバックが失敗しても古い入力の後処理と最終入力の
    // 公開を済ませ、最初の例外を再送出する。不正な数値の要求には、ポ
    // ップアップやキャプチャの取消も含め、一切の副作用を起こさない。
    bool apply(float value,WindowTextScaleContext context);
private:
    std::uint64_t revision_{};
};

enum class WindowInputKind { Keyboard, Mouse, Touch };
struct WindowInputRequest {
    std::uintptr_t surface{};
    WindowInputKind kind{WindowInputKind::Keyboard};
    std::optional<Point> position; // 論理ウィンドウへの変換前の、描画面の画素座標。
    std::uint64_t pointer{};
    bool pressed{};
};
struct WindowInputRoute {
    std::shared_ptr<Window> window;
    bool raised{};
};
// 単体版とALLで共有する論理ウィンドウの入力先調整。ネイティブホスト
// はイベントの識別情報と座標を変換し、返された対象を反映するだけ。ポッ
// プアップの終了とキャプチャ処理は、ヒット判定や前面化より先に行う。
[[nodiscard]] WindowInputRoute route_window_input(WindowInputLifecycleContext context,
    WindowStackStateMachine& stack, const ModalStateMachine& modal,
    const WindowInputRequest& request);

// 意味上のウィンドウ通知はキー・ポインター入力ではないため、モーダ
// ルの入力制限で捨てない。ホストはlive()で描画面の世代を確認
// し、結果のフォーカス・IME・再描画状態を反映する。モーダル開始
// 時も同じ入力終了処理を使うが、ALLは一つのOSウィンドウを共有
// し得るため、ネイティブのウィンドウ非アクティブ化通知は送らない。
void suspend_window_input(WindowInputLifecycleContext context,
    std::shared_ptr<Window> window, PointerEvent pointer = {},
    const std::function<bool()>& live = {});
void deactivate_window(WindowInputLifecycleContext context,
    std::shared_ptr<Window> window, PointerEvent pointer = {},
    const std::function<bool()>& live = {});
void activate_window(std::shared_ptr<Window> window);

struct WindowShowSurface {
    std::uintptr_t id{};
    Rect bounds;
    bool embedded{};
};
struct WindowShowPlan {
    bool native_visible{},native_activate{};
};

class WindowCloseStateMachine {
public:
    // 既存の論理スタックと終了処理の保護を使う。アダプターは有効な
    // 描画面を準備済みで、ネイティブの可視性とアクティブ化だけを
    // 返す。受動的なプレビューで所有元のフレーム操作を取り消さな
    // い。可視状態やキャプチャを変える前に、所属・順序・フォーカ
    // スの復元位置を準備する。確保の失敗時は元の状態を維持する。
    [[nodiscard]] std::optional<WindowShowPlan> show(WindowInputLifecycleContext context,
        WindowStackStateMachine& stack, const ModalStateMachine& modal,
        std::shared_ptr<Window> window, WindowShowSurface surface);
    // コールバックは終了を拒否したり、自身を置換したり、ダイアログを開いたり、同じウィンドウ
    // の終了を再帰的に要求したりできる。ハンドラーのない要求だけがホストの破棄処理を使う。
    void request(std::shared_ptr<Window> window,
                 const std::function<void(const std::shared_ptr<Window>&)>& dispose);
    // OSの終了要求は、元の各所有関係のルートへの要求であり、ルー
    // トが拒否した後に子を閉じてよいという意味ではない。コールバッ
    // クで作成されたウィンドウは次の操作に属し、この要求では処理
    // しない。不正な所有関係の循環には、一定の規則で選んだ代表へ
    // 一度要求する。その拒否は循環を含む連結部分全体を保護する。
    void request_all(WindowStackStateMachine& stack,
                     const std::function<void(const std::shared_ptr<Window>&)>& dispose);
    // 終了が受理された場合、コールバックの前に所有する部分木全体を無効にす
    // る。編集中の値を取り消し、親のネイティブ描画面より先に子を解放する。
    void dispose(WindowInputLifecycleContext context, WindowStackStateMachine& stack,
                 std::shared_ptr<Window> window,
                 std::function<void(const std::shared_ptr<Window>&)> release,
                 std::function<void()> publish = {});
    // 非表示時も同様に入力を終了するが、後の明示的なshowで再利
    // 用できるように、モデルとネイティブ描画面の登録は保持する。
    void hide(WindowInputLifecycleContext context, WindowStackStateMachine& stack,
              std::shared_ptr<Window> window,
              std::function<void(const std::shared_ptr<Window>&)> realize,
              std::function<void()> publish = {});
    // publishは後処理のコールバックが失敗しても、最終的なフォーカス・IME
    // 状態を出力する。コールバックは再入や自身の置換をまたいで値として保持する。
    [[nodiscard]] bool disposing(std::shared_ptr<Window> window) const;
private:
    // 完了トークンだけを扱い、別のアクティブフォーカススタックは持たない。遅
    // 延した祖先の処理は子孫自身の解除を引き継いでよいが、より新しいアプリ
    // ケーションのフォーカス遷移やウィンドウのアクティブ化は引き継がない。
    struct RetirementFocus {
        std::weak_ptr<Window> root;
        std::uint64_t expected_revision{},activation_revision{};
        bool restore{};
    };
    void retire(WindowInputLifecycleContext context, WindowStackStateMachine& stack,
                std::shared_ptr<Window> window,
                std::function<void(const std::shared_ptr<Window>&)> realize, bool remove,
                std::function<void()> publish, bool from_queue = false,
                std::shared_ptr<RetirementFocus> inherited_focus = {});
    [[nodiscard]] bool active_disposing(std::shared_ptr<Window> window) const;
    struct PendingRetirement {
        std::shared_ptr<Window> window;
        bool remove{};
        std::function<void(bool)> perform;
        std::shared_ptr<RetirementFocus> focus;
    };
    // 再入した祖先の終了処理は、処理中の子孫が完了するまで借用レンダラー
    // を解放できない。外側の終了処理が戻る前に同期的に処理を終え、ライフ
    // サイクルのコンテキストとスタックはその呼び出しより長く生存させる。
    std::vector<PendingRetirement> pending_retirements_;
    std::vector<std::shared_ptr<RetirementFocus>> retirement_focus_;
    bool draining_retirements_{};
    std::unordered_set<const Window*> requesting_;
    bool requesting_all_{};
    std::unordered_set<const Window*> disposing_;
    struct FocusReturn {
        std::weak_ptr<Window> window;
        std::weak_ptr<Window> owner;
        std::weak_ptr<Widget> target;
    };
    // 移動先の記録であり、現在のフォーカスではない。閉じたダイアログやコントロー
    // ルを保持したり、新しいコールバックのフォーカス遷移を上書きしたりしない。
    std::vector<FocusReturn> focus_returns_;
    std::uint64_t activation_revision_{};
};

enum class WindowNotificationKind {
    Activated, Deactivated, PointerLeft, CloseRequested, Exposed, GeometryChanged
};
struct WindowNotification {
    WindowNotificationKind kind{};
    std::uintptr_t surface{};
    PointerEvent pointer;
    // OSが物理ウィンドウの終了を要求した場合の、実際のネイティブ所有元
    // 。外部のALLホストは所有する論理ウィンドウがないため空にする。
    std::shared_ptr<Window> physical_owner;
};
struct WindowNotificationContext {
    WindowInputLifecycleContext input;
    WindowStackStateMachine& stack;
    WindowCloseStateMachine& close;
    // 配送前に描画面のネイティブ世代を取得する。新し
    // く生成された資源へ、古い通知の残りを渡さない。
    std::function<bool(const std::shared_ptr<Window>&)> live;
    std::function<void(const std::shared_ptr<Window>&)> dispose;
    std::function<void()> publish_input;
};
// OS通知はモーダル入力制限を通さない。露出・座標の変更は描画面の可視な
// 全メンバーに適用し、フォーカスは既存の論理フォーカスに従う。ポインター
// 離脱は実際のホバー対象に属し、その上のプレビューには渡さない。SDLの
// 列挙値やハンドル、別のフォーカススタック、ネイティブUIは導入しない。
// 通知コールバックが例外を送出しても最終的な入力・再描画状態を出力し、後
// 処理の後に最初の例外を報告する。コールバックはこの呼び出しが所有する。
void dispatch_window_notification(WindowNotificationContext context, WindowNotification notification);

} // namespace cwxeditor::lib
