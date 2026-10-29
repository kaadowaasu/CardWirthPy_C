#pragma once

#include "ui_actions.hpp"
#include "ui_combo_edit.hpp"
#include "ui_number_edit.hpp"

namespace cwxeditor::lib {

// 有効な編集セッションはネイティブのウィジェットツールキットではなく、一つ
// の表示側に属する。フックは計測済みの文字配置、実際の配置、既存の共通フォ
// ーカストランザクションを提供し、コントロールの意味上の動作は決めない。
struct ControlActivationContext {
    InlineEditStateMachine& inline_edit;
    ComboPopupStateMachine& combo_popup;
    ComboEditSession& combo_edit;
    NumberEditSession& number_edit;
    MenuPopupStateMachine& menu_popup;
    float row_height;
    std::size_t combo_visible_rows;
    std::function<bool(const WidgetPtr&)> focus;
    std::function<void(const std::shared_ptr<TextEdit>&, Point, Modifiers)> text_selection;
    std::function<void(const WidgetPtr&)> relayout;
    std::function<std::optional<Color>(Color)> pick_color;
    // 設定されていればこちらを優先する。入れ子のモーダルループはブラ
    // ウザ内のEditorを停止させるため使わず、後から完了する。
    AsyncColorPicker pick_color_async;
};

// ポインターとキーボードは同じ選択トランザクションを使う。プロバイダーはモ
// ーダルループを実行し得るため、コールバックと対象を保持し、対象の切り離し
// ・無効化や外部からの値変更があれば結果を拒否する。liveは所有する表示
// 側の寿命を確認し、モーダル後に戻り得るフォーカスのリビジョンは使わない。
// trueは取消も含め選択画面を実行したことを表し、例外は握りつぶさない。
bool activate_color_selection(WidgetPtr root, std::shared_ptr<ColorWell> well,
    std::function<std::optional<Color>(Color)> pick_color,
    std::function<bool()> live = {});
// 同じトランザクションの非同期版。選択画面
// の完了時に、同じ対象の失効確認を行う。
bool activate_color_selection_async(WidgetPtr root, std::shared_ptr<ColorWell> well,
    AsyncColorPicker pick_color, std::function<bool()> live = {});

// 主入力のコールバックが既定操作を消費しなかった場合だけ呼ぶ。確定・フォー
// カス・値変更のコールバックで、対象が非表示または切り離される場合がある。
void activate_control(const WidgetPtr& root, const WidgetPtr& widget,
                      Point point, Modifiers modifiers, ControlActivationContext& context,
                      std::function<bool()> live = {},
                      PointerButton button = PointerButton::Primary,
                      std::optional<bool> physical_control = std::nullopt,
                      bool suppress_default_item_view_selection = false);
void activate_control_double(const WidgetPtr& root, const WidgetPtr& widget,
                             Point point, ControlActivationContext& context,
                             Modifiers modifiers = Modifiers::None);

} // namespace cwxeditor::lib
