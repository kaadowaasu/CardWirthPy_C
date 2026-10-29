#pragma once

#include "ui_text_layout.hpp"

namespace cwxeditor::lib {

// 表示側はフォント計測とテキスト編集部の既存状態を提供する。ジ
// ェスチャーの単位、軸の選択、範囲制限、配置は共通処理で扱う。
struct ScrollTextAccess {
    TextEditInteractionState* state{};
    TextWidthMeasure measure;
    float line_height{};
    // プロバイダーを呼んだ後、状態を参照する前に確認する。セッショ
    // ンやホストの所有元が提供し、省略時は呼び出し側が状態の寿命を
    // 保証する。文字の計測や編集部・状態の変更は行ってはならない。
    std::function<bool()> current;
};
using ScrollTextProvider = std::function<ScrollTextAccess(const std::shared_ptr<TextEdit>&)>;
// ホストの状態ポインターを保持せず、イベントごとに編集部の状態を解決する。
[[nodiscard]] ScrollBarAccess text_scrollbar_access(
    const std::shared_ptr<TextEdit>& edit, ScrollTextProvider provider);

class UiScrollStateMachine {
public:
    // ホイールの差分は意味上の刻み数で、正は上・左。パンの差分は配置
    // 後の画素単位のポインター変位で、UI文字倍率には依存しない。
    bool wheel(const WidgetPtr& target, Point delta, Modifiers modifiers,
               float row_height, const ScrollTextProvider& text = {});
    bool pan(const WidgetPtr& target, Point displacement, float row_height,
             const ScrollTextProvider& text = {});
    // 各軸の操作を、元のコントロールから有効な祖先へ順に配送する。行未満
    // の端数を保持するリストは、再描画前でもその移動を処理済みとする。
    bool wheel_in_scope(const WidgetPtr& scope, const WidgetPtr& target, Point delta,
                        Modifiers modifiers, float row_height, const ScrollTextProvider& text = {});
    bool pan_in_scope(const WidgetPtr& scope, const WidgetPtr& target, Point displacement,
                      float row_height, const ScrollTextProvider& text = {});
private:
    bool apply_in_scope(const WidgetPtr& scope, const WidgetPtr& target, Point delta,
                        bool wheel, float row_height, const ScrollTextProvider& text);
    bool apply(const WidgetPtr& target, Point delta, bool wheel,
               float row_height, const ScrollTextProvider& text, bool* invalidated = nullptr);
    ItemViewScrollStateMachine items_;
};

// 閉じたコンボの選択には、キー入力と同じ有効項目の規則を適用する。
[[nodiscard]] bool wheel_combo_selection(const std::shared_ptr<ComboBox>& combo,
                                          float delta);

} // namespace cwxeditor::lib
