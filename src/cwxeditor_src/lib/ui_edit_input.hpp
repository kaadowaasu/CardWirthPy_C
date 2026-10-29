#pragma once

#include "ui_edit_focus.hpp"
#include <unordered_map>

namespace cwxeditor::lib {
class MouseInputStateMachine;
class DragDropStateMachine;
struct ScrollTextAccess;

// キャレット、選択、スクロールは、ネイティブコントロールではなく論理的な編
// 集部ごとに保持する。新しい編集部を扱うときに、失効した編集部を破棄する。
class TextEditStateStore {
public:
    TextEditInteractionState& state(const std::shared_ptr<TextEdit>& edit);
    [[nodiscard]] const TextEditInteractionState* find(const std::shared_ptr<TextEdit>& edit) const noexcept;
    void prune_expired();
private:
    struct Entry {
        std::weak_ptr<TextEdit> owner;
        TextEditInteractionState state;
    };
    std::unordered_map<TextEdit*, Entry> entries_;
};

struct EditInputMetrics {
    float line_height{};
    TextWidthMeasure measure;
};

struct EditInputContext {
    EditFocusContext editing;
    TextEditStateStore& text_states;
    std::function<EditInputMetrics(const WidgetPtr&)> metrics;
    TextClipboardAccess clipboard;
    float row_height{};
    std::size_t combo_visible_rows{};
    // 省略可能な、アプリケーションのジェスチャー取消処理（保留中のドラッグなど）。
    std::function<bool(Key, Modifiers)> before_default_key;
    // メニューモードでは論理フォーカスを維持するが、キーボードとIMEの入力を所有する。ホスト
    // は描画面の寿命とモーダル上の利用可否だけを提供し、コントロールへの入力経路は決めない。
    MenuPopupStateMachine* menu{};
    std::function<bool()> live;
    // ポインター配送と同じ意味上のドラッグ・ポインターセッションを使う。Escape
    // による取消は共通のキー操作規則であり、ネイティブのフックではない。
    MouseInputStateMachine* mouse{};
    std::shared_ptr<DragDropStateMachine> drag_drop;
    // ポインター配送とフレーム準備で共有する。フォーカス中のコントロー
    // ルがキーを消費しない場合も、キー入力でホバーヘルプを終了する。
    HoverStateMachine* hover{};
    // ポインターによる実行と同じ共通の値入力ダイアログプロバイダー。ここで
    // はネイティブコントロールやキー操作専用の色選択ダイアログを作らない。
    std::function<std::optional<Color>(Color)> pick_color;
    AsyncColorPicker pick_color_async;
};

struct EditInputResult {
    bool handled{};
    bool redraw{};
    std::shared_ptr<TextEdit> editor;
    std::shared_ptr<TabView> changed_tabs;
};

// 共通の表示側が有効な論理編集部と候補表示の基準位置を選ぶ
// 。ネイティブホストはこの矩形をOS座標へ変換するだけ。
struct TextInputPresentation {
    WidgetPtr owner;
    std::shared_ptr<TextEdit> editor;
    Rect caret_bounds;
    std::uint64_t focus_revision{};
    std::uint64_t composition_reset_serial{};
    bool editor_required{true};
    // 通常の編集部では空。編集部以外の受信先では、このフレームがネイティ
    // ブ入力を公開するときも同じIncSearch登録である必要がある。
    std::weak_ptr<CommittedTextInputRegistration> recipient_registration;
};
struct TextInputArea {
    int x{}, y{}, width{}, height{};
};
// ホストが表示側の座標をOS単位に変換した後でだけ、外側へ丸める。変換がない、
// または不正な場合に、古い位置やオーバーフローした位置を公開してはならない。
using TextInputPointMap = CoordinatePointMap;
[[nodiscard]] std::optional<TextInputArea> map_text_input_area(
    Rect caret, const TextInputPointMap& map);
[[nodiscard]] bool text_input_is_current(EditInputContext context,
    const WidgetPtr& scope, const TextInputPresentation& input);
[[nodiscard]] std::optional<TextInputPresentation> prepare_text_input(
    EditInputContext context, const WidgetPtr& scope);

TextEditInteractionState& edit_input_text_state(EditInputContext context,
    const std::shared_ptr<TextEdit>& edit);
// 通常・インライン・コンボ・数値の状態解決を、キー入力とIME入
// 力で共有する。返す寿命判定は、文字の計測や正規化を行わない。
[[nodiscard]] ScrollTextAccess prepare_edit_scroll_access(EditInputContext context,
    const std::shared_ptr<TextEdit>& edit, WidgetPtr scope = {});
// 投影された数値・コンボ・表の編集部は、ウィジェットツリー上の子では
// ない。切り離されたものと見なさず、実際の論理的な所有元を検証する。
[[nodiscard]] WidgetPtr edit_input_owner(EditInputContext context,
    const std::shared_ptr<TextEdit>& edit);
// falseはプロバイダーの処理中に入力が無効になったことを表す。ポ
// インター処理側は、その結果から選択キャプチャを開始してはならない。
bool select_edit_text_at(EditInputContext context, const std::shared_ptr<TextEdit>& edit,
    Point point, Modifiers modifiers);
void drag_edit_text_to(EditInputContext context, const std::shared_ptr<TextEdit>& edit,
    Point point);
[[nodiscard]] EditInputResult dispatch_edit_text(EditInputContext context,
    WidgetPtr scope, TextInputEvent input, const TextComposition* composition = nullptr);
[[nodiscard]] EditInputResult dispatch_edit_key(EditInputContext context,
    WidgetPtr scope, KeyEvent key);
EditInputResult dispatch_edit_key_up(EditInputContext context, WidgetPtr scope, KeyEvent key);
[[nodiscard]] bool menu_in_input_scope(const MenuPopupStateMachine& menu,
    const WidgetPtr& scope);

}  // namespace cwxeditor::lib
