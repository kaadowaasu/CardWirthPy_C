#pragma once
#include "ui_edit_input.hpp"
#include "ui_passive_input.hpp"
#include "ui_pointer_input.hpp"
#include "ui_window_stack.hpp"
#include <span>
#include <unordered_map>

namespace cwxeditor::lib {
// 論理的な処理順序はここで管理する。アダプターは結果のフォーカス・IME
// ・アクセシビリティ状態を反映し、描画面の再描画を要求するだけ。
struct InputPublicationContext {
    EditInputContext edit;
    WindowStackStateMachine& stack;
    const ModalStateMachine& modal;
    std::function<bool(const std::shared_ptr<Window>&)> live;
    std::function<void()> publish_focus;
    std::function<void(const std::shared_ptr<Window>&)> redraw;
    // 変更された非フォーカス項目をネイティブのアクセシビリティへ反映する省略可能
    // な処理。対象として有効かは別のネイティブ表示側ではなく、ここで判定する。
    std::function<void(const WidgetPtr&)> publish_control;
};
bool publish_edit_focus(InputPublicationContext context,const WidgetPtr& target);
// ポインターとキーボードによるコントロール実行で共有するフォーカス規則。文
// 字入力以外のコントロールは編集フォーカスを得ずに実行できる。フォーカス可
// 能なコントロールは、現在の共通フォーカスが確定した場合だけ成功とする。
bool activate_control_focus(InputPublicationContext context,const WidgetPtr& target);
// プログラムからの表・ツリーのインライン編集は、キー操作以外のボタン実行と異な
// り、実際の編集フォーカスを必須とする。表示側は判定を重複実装せず、既存のProgrammaticInlineEdit
// のフォーカスコールバックを通す。
bool publish_and_require_edit_focus(InputPublicationContext context,const WidgetPtr& target);
// 完了時は、入力範囲の終了後の現在の全体フォーカスも公開する。コール
// バックが失敗しても後続の後処理を省略せず、最初の失敗を再送出する。
bool complete_edit_input(InputPublicationContext context,
    std::shared_ptr<Window> scope,const EditInputResult& result);
// 意味上の入力の入口。既存のウィジェットや編集の状態機械を再利用し、コ
// ールバックが例外を送出しても配送と完了処理を一組として扱う。live
// はscopeについて入力元のネイティブ世代を検証し、ほかのウィンド
// ウについては現在のネイティブの寿命を検証する。参照するすべての状態
// 機械は、再入も含めてこの同期呼び出しの終了まで生存する必要がある。
[[nodiscard]] EditInputResult dispatch_and_publish_edit_key(InputPublicationContext context,
    std::shared_ptr<Window> scope,KeyEvent key,bool down);
[[nodiscard]] EditInputResult dispatch_and_publish_edit_text(InputPublicationContext context,
    std::shared_ptr<Window> scope,TextInputEvent input,const TextComposition* composition=nullptr);
[[nodiscard]] PassiveInputResult dispatch_and_publish_wheel(InputPublicationContext context,
    PassiveInputContext passive,std::shared_ptr<Window> scope,WheelEvent event);

// 座標、終了、カーソルの判断は共通の完了処理で行う。これらのフックはネイティ
// ブ資源への反映、または既存のウィンドウ終了サービスの呼び出しだけを行う。
struct PointerPublicationContext {
    InputPublicationContext input;
    std::function<void(const std::shared_ptr<Window>&,Rect)> realize_bounds;
    std::function<void(CursorKind)> realize_cursor;
    std::function<void(const std::shared_ptr<Window>&)> request_close;
    // ALLのタイトル部の最小化ボタン。組み込みのモードレスダイアログだけが対象。
    std::function<void(const std::shared_ptr<Window>&)> request_minimize;
    // ALLのタイトル部の元に戻す（false）・やり直し（true）ボタン。
    // Ctrl+Z/Ctrl+Yのショートカットと同じ方法でウィンドウへ渡す。
    std::function<void(const std::shared_ptr<Window>&, bool redo)> request_history;
};
// コールバックの前に、タイトル部の矩形、論理的な所有元、文字
// 倍率の世代をコピーする。配送後は呼び出し側のネイティブRect
// を保持・参照しない。確定した矩形をフックへ渡すのは、その
// 表示状態が最新の間だけ。参照する状態機械と取得したサー
// ビスプロバイダーも、その生存確認コールバックを保持する
// ポインター接触やキャプチャより長く生存する必要がある。
[[nodiscard]] PointerInputResult dispatch_and_publish_pointer(PointerPublicationContext context,
    PointerInputContext pointer,std::shared_ptr<Window> scope,PointerEvent event,
    bool primary_down=false,unsigned clicks=1);
[[nodiscard]] PointerInputResult advance_and_publish_pointer(PointerPublicationContext context,
    PointerInputContext pointer,std::shared_ptr<Window> scope);
// アダプターはネイティブ世代の状態と資源操作のフックを渡す。共通コー
// ドは各論理範囲を一度ずつ進め、失敗後も続行してから最初の失敗を再送
// 出する。コールバックで作成されたウィンドウは次の更新周期まで待つ。
struct PointerTimerTarget {
    std::shared_ptr<Window> scope;
    PointerInputContext pointer;
    PointerPublicationContext publication;
};
void advance_and_publish_pointers(std::span<const PointerTimerTarget> targets);

// ネイティブ側の状態だけを表す。複数の論理ウィンドウが同じ入力面を共有できる。
struct TextInputSurface {
    std::shared_ptr<Window> scope;
    std::uintptr_t surface{};
};
struct TextInputSurfacePlan {
    std::shared_ptr<Window> active_scope;
    // 非アクティブな物理入力面ごとに、有効な代表を一つ保持する。ネイテ
    // ィブ入力セッションをまだ停止していない非表示ウィンドウも含む。
    std::vector<TextInputSurface> stop;
};
[[nodiscard]] TextInputSurfacePlan plan_text_input_surfaces(
    const WindowStackStateMachine& stack,const ModalStateMachine& modal,
    const WidgetPtr& focused,std::span<const TextInputSurface> surfaces);

enum class TextInputPublicationKind { Ignore, Stop, Activate };
struct NativeTextInputObservation {
    bool active{};
    std::optional<TextInputArea> area;
};
struct TextInputPublicationContext {
    EditInputContext edit;
    const WindowStackStateMachine& stack;
    const ModalStateMachine& modal;
    std::shared_ptr<Window> scope;
    std::uintptr_t surface{};
    std::uint64_t focus_revision{};
    std::function<bool()> live;
    TextInputPointMap map;
};
struct TextInputPublicationPlan {
    TextInputPublicationKind kind{TextInputPublicationKind::Ignore};
    std::uintptr_t surface{};
    std::uint64_t ticket{};
    bool clear_composition{},start{},update_area{};
    TextInputArea area{};
    std::weak_ptr<Window> scope;
    std::weak_ptr<TextEdit> editor;
    std::weak_ptr<Widget> recipient;
    std::weak_ptr<CommittedTextInputRegistration> recipient_registration;
    std::uint64_t focus_revision{},reset_serial{};
    bool editor_required{true};
    // 優先する入力モードは、入力セッションの開始時だけ復元する。再描
    // 画、キャレット移動、未確定文字列のリセット時には復元しない。
    bool restore_ime_mode{};
};
// IMEセッションはALLの論理ウィンドウごとではなく、物理入力面ごとに一つ
// とする。計画は再入をまたいで有効とはせず、確定時は最新のチケットと成功し
// たネイティブ操作だけを受け付ける。失敗した操作は次の公開時に再試行する。
class TextInputPublicationStateMachine {
public:
    [[nodiscard]] TextInputPublicationPlan prepare(TextInputPublicationContext context,
        std::optional<TextInputPresentation> input,NativeTextInputObservation native);
    bool commit(const TextInputPublicationPlan& plan,bool succeeded);
    void retire(std::uintptr_t surface);
private:
    struct Session {
        std::uint64_t ticket{};
        std::weak_ptr<Window> scope;
        std::weak_ptr<TextEdit> editor;
        std::weak_ptr<Widget> recipient;
        std::weak_ptr<CommittedTextInputRegistration> recipient_registration;
        std::uint64_t focus_revision{},reset_serial{};
        bool editor_required{true};
    };
    std::unordered_map<std::uintptr_t,Session> sessions_;
    std::uint64_t next_ticket_{1};
};
} // namespace cwxeditor::lib
