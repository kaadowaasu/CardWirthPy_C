#include "ui_external_drop.hpp"
#include "ui_actions.hpp"
#include "ui_metrics.hpp"

#include <cmath>
#include <iterator>
#include <utility>

namespace cwxeditor::lib {

void ExternalDropStateMachine::retire(WindowStackStateMachine::SurfaceId surface) noexcept {
    batches_.erase(surface);
}
bool ExternalDropStateMachine::pending(WindowStackStateMachine::SurfaceId surface) const noexcept {
    return batches_.contains(surface);
}

ExternalDropResult ExternalDropStateMachine::receive(ExternalDropEvent event,
                                                    ExternalDropContext context) {
    if (!event.surface) return {};
    if (event.phase == ExternalDropPhase::Cancel) {
        retire(event.surface);
        return {true};
    }
    if (event.phase == ExternalDropPhase::Begin) {
        // 重複するBeginは、内部ドラッグではなく未完了の一括転送を置き換える。
        batches_.insert_or_assign(event.surface, Batch{});
        return {true};
    }
    const auto found = batches_.find(event.surface);
    // 終了・完了した転送を、遅れて来たデータで復活させない。
    if (found == batches_.end()) return {};
    auto& batch = found->second;
    if (event.position) {
        const auto point = *event.position;
        batch.position = std::isfinite(point.x) && std::isfinite(point.y)
            ? event.position : std::nullopt;
    }
    if (event.phase == ExternalDropPhase::Data) {
        batch.data.text += event.data.text;
        batch.data.files.insert(batch.data.files.end(),
            std::make_move_iterator(event.data.files.begin()),
            std::make_move_iterator(event.data.files.end()));
        for (auto& [format, bytes] : event.data.formats)
            batch.data.formats.insert_or_assign(std::move(format), std::move(bytes));
        return {true};
    }
    if (event.phase != ExternalDropPhase::Complete) return {true};
    // コールバックをまたいで batches_ 内のイテレーター・参照を保持し
    // ない。 同じ実際の画面上で、直ちに別の一括転送が始まる場合があるため。
    auto completed = std::move(batch);
    batches_.erase(found);
    if (!completed.position || context.drag.active()) return {true};
    const auto point = *completed.position;
    const auto scope = context.stack.input_window(event.surface, point, context.modal);
    if (!scope) return {true};
    const auto alive = [context, scope, surface = event.surface, point] {
        return context.stack.input_window(surface, point, context.modal) == scope &&
               (!context.live || context.live(scope));
    };
    if (!alive()) return {true};
    const auto hit = hit_test_widget(scope, point,
        scaled_ui_metric(kEditorUiMetrics.tab_height),
        scaled_ui_metric(kEditorUiMetrics.expand_header_height));
    const auto target = context.drag.target_for(hit, scope, completed.data);
    if (!target) return {true};
    // 再入したコールバックで完了が遅延する場合があるため、 セ
    // ッションのコールバックにスタック上の参照を取り込まない。
    const auto operation = std::make_shared<DropOperation>(DropOperation::None);
    const auto refresh = [&] {
        if (context.redraw && alive()) context.redraw(scope);
    };
    try {
        if (context.drag.begin_external(std::move(completed.data), alive,
                [operation](DropOperation value) { *operation = value; }))
            context.drag.finish(target, point, hit_test_item_view(target, point), scope,
                                DropOperation::Copy);
    } catch (...) {
        refresh();
        throw;
    }
    refresh();
    return {true, scope, *operation};
}

} // namespace cwxeditor::lib
