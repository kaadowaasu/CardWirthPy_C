#include "ui_edit_input.hpp"
#include "cwxeditor_src/lib/ui_edit_preferences.hpp"
#include "ui_activation.hpp"
#include "ui_inline_edit.hpp"
#include "ui_mouse_input.hpp"
#include "ui_scroll.hpp"
#include "window_host.hpp"
#include <cmath>
#include <limits>

namespace cwxeditor::lib {

bool menu_in_input_scope(const MenuPopupStateMachine& menu, const WidgetPtr& scope) {
    return menu.is_active() && widget_in_input_scope(scope, menu.owner());
}

TextEditInteractionState& TextEditStateStore::state(const std::shared_ptr<TextEdit>& edit) {
    auto found = entries_.find(edit.get());
    if (found == entries_.end() || found->second.owner.lock() != edit) {
        prune_expired();
        found = entries_.insert_or_assign(edit.get(), Entry{edit, {}}).first;
    }
    synchronize_text_edit_state(*edit, found->second.state);
    return found->second.state;
}

void TextEditStateStore::prune_expired() {
    std::erase_if(entries_, [](const auto& entry) { return entry.second.owner.expired(); });
}

const TextEditInteractionState* TextEditStateStore::find(const std::shared_ptr<TextEdit>& edit) const noexcept {
    const auto found=entries_.find(edit.get());
    return found!=entries_.end() && found->second.owner.lock()==edit ? &found->second.state : nullptr;
}

TextEditInteractionState& edit_input_text_state(EditInputContext context,
    const std::shared_ptr<TextEdit>& edit) {
    auto& state = edit == context.editing.number_edit.editor() ? context.editing.number_edit.state()
        : edit == context.editing.combo_edit.editor() ? context.editing.combo_edit.state()
        : edit == context.editing.inline_edit.editor() ? context.editing.inline_edit.text_state()
        : context.text_states.state(edit);
    synchronize_text_edit_state(*edit,state);
    if (state.composition && (state.composition->focus_revision!=context.editing.focus.revision() ||
        edit_input_owner(context,edit)!=context.editing.focus.focused())) state.cancel_composition();
    return state;
}

WidgetPtr edit_input_owner(EditInputContext context, const std::shared_ptr<TextEdit>& edit) {
    if (!edit) return {};
    const auto focused = context.editing.focus.focused();
    if (edit == context.editing.number_edit.editor())
        return focused && context.editing.number_edit.active_for(focused) ? focused : WidgetPtr{};
    if (edit == context.editing.combo_edit.editor())
        return focused && context.editing.combo_edit.active_for(focused) ? focused : WidgetPtr{};
    if (edit == context.editing.inline_edit.editor())
        return context.editing.inline_edit.active() ? context.editing.inline_edit.owner() : WidgetPtr{};
    return edit;
}

ScrollTextAccess prepare_edit_scroll_access(EditInputContext context,
    const std::shared_ptr<TextEdit>& source, WidgetPtr scope) {
    const auto edit=source;
    if (!edit || !context.metrics || (context.live && !context.live())) return {};
    const auto owner=edit_input_owner(context,edit);
    const auto focus_revision=context.editing.focus.revision();
    const auto live=[context,owner,edit,scope,focus_revision] {
        return (!context.live || context.live()) && owner && owner->enabled() && owner->visible() &&
            (!scope || (scope->enabled() && scope->visible() && widget_in_input_scope(scope,owner))) &&
            context.editing.focus.revision()==focus_revision && edit_input_owner(context,edit)==owner;
    };
    if (!live()) return {};
    auto* state=&edit_input_text_state(context,edit);
    const auto current=[context,edit,state,live] {
        if (!live()) return false;
        const TextEditInteractionState* active=nullptr;
        if (edit==context.editing.number_edit.editor()) active=&context.editing.number_edit.state();
        else if (edit==context.editing.combo_edit.editor()) active=&context.editing.combo_edit.state();
        else if (edit==context.editing.inline_edit.editor()) active=&context.editing.inline_edit.text_state();
        else active=context.text_states.find(edit);
        return active==state;
    };
    const TextEditSnapshot before(*edit,*state);
    const auto metrics=context.metrics(edit);
    // 寸法取得のコールバックがエディタを閉じる・状態を消す・編集
    // 中の対象を置き換える場合、 古い生ポインターを返さない。
    if (!current() || !before.matches(*edit,*state)) return {nullptr,{},0,[]{return false;}};
    return {state,metrics.measure,metrics.line_height,current};
}

namespace {
bool update_pointer_text_selection(EditInputContext context, std::shared_ptr<TextEdit> edit,
    Point point, Modifiers modifiers, bool drag) {
    if (!edit || !edit->enabled() || !edit->visible() || !context.metrics ||
        !std::isfinite(point.x) || !std::isfinite(point.y)) return false;
    const auto owner = edit_input_owner(context, edit);
    const auto revision = context.editing.focus.revision();
    const auto valid = [&] {
        return (!context.live || context.live()) && owner && owner->enabled() && owner->visible() &&
            context.editing.focus.revision() == revision && context.editing.focus.focused() == owner &&
            edit_input_owner(context, edit) == owner;
    };
    if (!valid()) return false;
    auto& state = edit_input_text_state(context, edit);
    const TextEditSnapshot before(*edit, state);
    const auto current = [&] { return valid() && before.matches(*edit, state); };
    const auto metrics = context.metrics(edit);
    if (!current() || !metrics.measure || !std::isfinite(metrics.line_height) || metrics.line_height <= 0)
        return false;
    auto prepared = before;
    if (!drag) prepared.state.cancel_composition();
    const auto inner = text_edit_inner_bounds(prepared.edit);
    const auto lines = layout_text_lines(prepared.edit, inner.width, metrics.measure);
    if (drag) {
        drag_text_selection_to(prepared.edit, prepared.state, lines, inner, metrics.line_height, point, metrics.measure);
    } else {
        const auto position = text_edit_hit(prepared.edit, lines, inner, metrics.line_height,
            prepared.state, point, metrics.measure);
        if ((static_cast<std::uint32_t>(modifiers) & static_cast<std::uint32_t>(Modifiers::Shift)) == 0)
            prepared.state.anchor = position;
        prepared.state.caret = position; prepared.state.preferred_x = -1;
        commit_text_edit_state(prepared.edit, prepared.state);
        reveal_text_edit_caret(prepared.edit, prepared.state, lines, inner, metrics.line_height, metrics.measure);
    }
    if (!current()) return false;
    state = std::move(prepared.state);
    commit_text_edit_state(*edit, state);
    if (edit == context.editing.combo_edit.editor()) context.editing.combo_edit.publish_selection();
    if (edit == context.editing.inline_edit.editor()) context.editing.inline_edit.publish_choice_selection();
    return true;
}
} // namespace

bool select_edit_text_at(EditInputContext context, const std::shared_ptr<TextEdit>& edit,
    Point point, Modifiers modifiers) {
    return update_pointer_text_selection(context, edit, point, modifiers, false);
}

void drag_edit_text_to(EditInputContext context, const std::shared_ptr<TextEdit>& edit, Point point) {
    (void)update_pointer_text_selection(context, edit, point, Modifiers::None, true);
}

namespace {
struct FocusStamp {
    FocusStateMachine& focus;
    WidgetPtr scope;
    std::function<bool()> live;
    MenuPopupStateMachine* menu{};
    WidgetPtr target{focus.focused()};
    std::uint64_t revision{focus.revision()};
    std::uint64_t menu_revision{menu ? menu->revision() : 0};
    bool valid() const {
        // フォーカスのない有効な範囲も、Tab・メニューショートカット・既定/キ
        // ャンセルキーは受け付ける。 古い非nullの対象には受け付けさせない。
        return (!live || live()) && scope && scope->visible() && scope->enabled() &&
               (!menu || menu->revision() == menu_revision) &&
               focus.revision() == revision && focus.focused() == target &&
               (!target || focus_target_in_scope(scope, target) == target);
    }
};

std::optional<EditInputResult> prepare_keyboard_dispatch(EditInputContext context,
    const WidgetPtr& scope) {
    if (!scope || !scope->visible() || !scope->enabled() || (context.live && !context.live()))
        return EditInputResult{true};
    const auto focused = context.editing.focus.focused();
    const auto valid = focus_target_in_scope(scope, focused);
    if (focused == valid) return {};
    // タブ切り替えやモデルの除去で、ネイティブのフォーカス通知なしに編集
    // 欄が無効になる場合がある。 フォーカスを外す前に未確定入力を捨て、
    // そのコールバックが選んだ置換先へ、古いキー・文字を再送しない。
    context.editing.inline_edit.cancel();
    context.editing.number_edit.discard();
    context.editing.combo_edit.finish();
    context.editing.combo_popup.close();
    set_edit_focus(context.editing, valid, scope);
    return EditInputResult{true, true};
}

bool dispatch_menu_key(EditInputContext context, const WidgetPtr& scope,
    const KeyEvent& key, bool pressed) {
    if (!context.menu) return false;
    const bool owned = menu_in_input_scope(*context.menu, scope);
    const auto combo_revision = context.editing.combo_popup.revision();
    const bool handled = context.menu->handle_key_event(scope, key, pressed);
    if (owned || handled) {
        // コマンドが別のダイアログとそのコンボを開く場合がある。 古いキー
        // 処理を終了する際、そのコマンドが作った新しい操作状態を閉じない。
        if (context.editing.combo_popup.revision() == combo_revision &&
            (owned || menu_in_input_scope(*context.menu, scope)))
            context.editing.combo_popup.close();
        return true;
    }
    return false;
}

bool modifier(Modifiers value, Modifiers flag) {
    return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0;
}

std::size_t popup_rows(EditInputContext context, const WidgetPtr& scope,
                       const std::shared_ptr<ComboBox>& combo) {
    if (!combo) return context.combo_visible_rows;
    const auto requested = combo->visible_item_count != 0U
        ? combo->visible_item_count : context.combo_visible_rows;
    return scope ? layout_combo_popup(*combo, scope->bounds(), requested).visible_rows
                 : requested;
}

std::optional<EditInputResult> dispatch_retained_focus_popup(EditInputContext context,
    const WidgetPtr& scope, const KeyEvent* key, bool pressed) {
    if (!scope || !scope->visible() || !scope->enabled() || (context.live && !context.live()))
        return EditInputResult{true};
    if (context.menu && menu_in_input_scope(*context.menu, scope)) {
        // モードレスのポップアップは、保持中の編集フォーカスとは別のウィンドウに属
        // する場合がある。 最初のキーを振り分ける前に、その編集欄を破棄しない。
        if (key) (void)dispatch_menu_key(context, scope, *key, pressed);
        return EditInputResult{true, key != nullptr};
    }
    const auto combo = context.editing.combo_popup.combo();
    auto& inline_edit = context.editing.inline_edit;
    if (!combo || !combo_in_scope(scope, combo, inline_edit)) return {};
    const bool inline_choice = combo == inline_edit.choice();
    const WidgetPtr owner = inline_choice ? inline_edit.owner() : combo;
    if (context.editing.focus.focused() == owner) return {}; // 通常のフォーカス中の編集欄への経路。
    if (!key || !pressed) return EditInputResult{true};
    bool changed{};
    if (inline_choice) {
        if (context.metrics) {
            // このポップアップは意図的に別ウィンドウへフォーカスを残す。 focus
            // == owner を要求せず、そのフォーカスの識別情報を維持し、
            // フォント・クリップボードの提供処理中は古い表示用データを保持する。
            const auto editor = inline_edit.editor();
            if (!editor) return EditInputResult{true};
            const auto focused = context.editing.focus.focused();
            const auto focus_revision = context.editing.focus.revision();
            const auto number_revision = context.editing.number_edit.revision();
            const auto combo_revision = context.editing.combo_edit.revision();
            const auto menu_revision = context.menu ? context.menu->revision() : 0;
            const auto live = [&] {
                return (!context.live || context.live()) && scope->visible() && scope->enabled() &&
                    owner && owner->visible() && owner->enabled() &&
                    editor->visible() && editor->enabled() &&
                    context.editing.focus.focused() == focused &&
                    context.editing.focus.revision() == focus_revision &&
                    context.editing.number_edit.revision() == number_revision &&
                    context.editing.combo_edit.revision() == combo_revision &&
                    (!context.menu || context.menu->revision() == menu_revision) &&
                    inline_edit.active() && inline_edit.owner() == owner &&
                    inline_edit.editor() == editor && inline_edit.choice() == combo &&
                    combo_in_scope(scope, combo, inline_edit);
            };
            if (!live()) return EditInputResult{true};
            const TextEditSnapshot before(*editor, inline_edit.text_state());
            const auto revision = context.editing.combo_popup.revision();
            const auto metrics = context.metrics(editor);
            if (!live() || !before.matches(*editor, inline_edit.text_state()) ||
                context.editing.combo_popup.revision() != revision ||
                !metrics.measure || !std::isfinite(metrics.line_height) || metrics.line_height <= 0)
                return EditInputResult{true, true};
            // ポップアップの世代は通知前だけ確認する。 移動・確
            // 定によって、共通の状態管理内で正当に変わるため。
            changed = dispatch_inline_edit_key(inline_edit, context.editing.combo_popup,
                key->key, key->modifiers, context.combo_visible_rows,
                metrics.line_height, metrics.measure, context.clipboard, live);
        }
    } else {
        changed = context.editing.combo_popup.handle_key(combo, key->key,
            modifier(key->modifiers, Modifiers::Alt), popup_rows(context, scope, combo),
            modifier(key->modifiers, Modifiers::Control | Modifiers::Meta));
    }
    // コマンドがフォーカス・ポップアップ・画面を置き換える場合がある。
    // その新しい操作状態に、後続の既定操作やフォーカス解除を適用しない。
    return EditInputResult{true, changed};
}

bool popup_suspends_text_input(EditInputContext context) {
    if (context.menu && context.menu->is_active()) return true;
    const auto combo = context.editing.combo_popup.combo();
    if (!combo) return false;
    const WidgetPtr owner = combo == context.editing.inline_edit.choice()
        ? context.editing.inline_edit.owner() : combo;
    return context.editing.focus.focused() != owner;
}

EditInputResult finish_input(EditInputContext context, const FocusStamp& stamp,
    EditInputResult result) {
    if (!stamp.valid()) {
        result.handled = result.redraw = true;
        result.editor.reset();
        return result;
    }
    if (result.editor) {
        const auto current = [&] {
            return stamp.valid() && edit_input_owner(context, result.editor) == stamp.target;
        };
        if (!current()) {result.handled = result.redraw = true;result.editor.reset();return result;}
        // フォント計測も入力処理を進める場合がある。 所有する表示用データから
        // スクロールを計算し、編集欄・選択が変わっていない場合だけ更新する。
        auto& state = edit_input_text_state(context, result.editor);
        const TextEditSnapshot before(*result.editor, state);
        const auto metrics = context.metrics(result.editor);
        if (!current()) {result.handled = result.redraw = true;result.editor.reset();return result;}
        if (!before.matches(*result.editor, state) || !metrics.measure ||
            !std::isfinite(metrics.line_height) || metrics.line_height <= 0) return result;
        auto revealed = before.state;
        const auto inner = text_edit_inner_bounds(before.edit);
        const auto lines = layout_text_lines(before.edit, inner.width, metrics.measure);
        reveal_text_edit_caret(before.edit, revealed, lines, inner,
                               metrics.line_height, metrics.measure);
        if (!current()) {result.handled = result.redraw = true;result.editor.reset();return result;}
        if (before.matches(*result.editor, state)) {
            state.scroll_x = revealed.scroll_x;
            state.scroll_y = revealed.scroll_y;
        }
    }
    return result;
}
// 文書全体を計測せず、論理的な編集欄を解決する。 キーボ
// ードの状態管理でも使うため、通常キーでIMEが入力を管
// 理しているか確認するだけなら、追加の配置処理は不要。
std::shared_ptr<TextEdit> resolve_input_editor(EditInputContext context, const FocusStamp& stamp,
    const bool semantic_committed_input=false) {
    if (!stamp.valid() || !stamp.target || popup_suspends_text_input(context)) return {};
    std::shared_ptr<TextEdit> edit;
    auto& inline_edit = context.editing.inline_edit;
    if (inline_edit.active() && inline_edit.owner() == stamp.target) {
        // 表示範囲外へスクロールしたセルに、古いIMEの表示位置を残さない。
        if (!inline_edit.layout(context.row_height)) return {};
        edit = inline_edit.editor();
    } else if (const auto text = std::dynamic_pointer_cast<TextEdit>(stamp.target)) {
        edit = text;
    } else if (const auto number = std::dynamic_pointer_cast<NumberEdit>(stamp.target);
               number && !number->read_only) {
        context.editing.number_edit.project(number);
        edit = context.editing.number_edit.editor();
    } else if (const auto combo = std::dynamic_pointer_cast<ComboBox>(stamp.target);
               combo && combo->editable) {
        context.editing.combo_edit.begin(combo);
        edit = context.editing.combo_edit.editor();
    }
    // 読み取り専用のコンボ表示も、D版の IncSearch 用
    // の確定入力を受け取る。 論理コントロールをコールバックか
    // ら隠さず、IME表示と標準編集の側で変更可否を制限する。
    if (!stamp.valid() || !edit || !edit->visible() || !edit->enabled() ||
        (edit->read_only && !semantic_committed_input) ||
        edit_input_owner(context,edit)!=stamp.target) return {};
    return edit;
}
}  // namespace

std::optional<TextInputPresentation> prepare_text_input(
    EditInputContext context, const WidgetPtr& scope) {
    const FocusStamp stamp{context.editing.focus, scope, context.live, context.menu};
    const auto edit=resolve_input_editor(context,stamp);
    if (!edit && stamp.valid() && stamp.target && stamp.target->visible() &&
        stamp.target->enabled()) {
        const auto registration = stamp.target->committed_text_input.registration;
        if (!registration || registration->owner_lifetime.expired() ||
            registration->parent_lifetime.expired() || !registration->anchor ||
            !registration->on_text_input || !registration->on_key_down ||
            (registration->current && !registration->current()) ||
            registration->parent_lifetime.lock() != stamp.target->committed_text_input.lifetime)
            return {};
        const auto anchor = registration->anchor;
        const auto bounds = anchor();
        if (!stamp.valid() || stamp.target->committed_text_input.registration != registration ||
            registration->owner_lifetime.expired() || registration->parent_lifetime.expired() ||
            (registration->current && !registration->current()) ||
            !bounds || bounds->width <= 0.0F || bounds->height <= 0.0F)
            return {};
        return TextInputPresentation{stamp.target, {}, *bounds, stamp.revision, 0, false, registration};
    }
    const auto valid = [&] {
        return stamp.valid() && edit && edit->visible() && edit->enabled() &&
               !edit->read_only && edit_input_owner(context, edit) == stamp.target;
    };
    if (!valid() || !context.metrics) return {};
    auto& state = edit_input_text_state(context, edit);
    const TextEditSnapshot snapshot(*edit, state);
    const auto metrics = context.metrics(edit);
    if (!valid() || !snapshot.matches(*edit, state) || !metrics.measure || !std::isfinite(metrics.line_height) ||
        metrics.line_height <= 0) return {};
    const auto inner = text_edit_inner_bounds(snapshot.edit);
    if (inner.width <= 0 || inner.height <= 0) return {};
    const auto lines = layout_text_lines(snapshot.edit, inner.width, metrics.measure);
    const auto caret = text_edit_caret_bounds(snapshot.edit, lines, inner,
        metrics.line_height, snapshot.state, metrics.measure);
    auto display_caret=caret;
    if (auto view=project_text_composition(snapshot.edit,snapshot.state)) {
        const auto visual_lines=layout_text_lines(view->edit,inner.width,metrics.measure);
        display_caret=text_edit_caret_bounds(view->edit,visual_lines,inner,metrics.line_height,view->state,metrics.measure);
    }
    if (!valid() || !snapshot.matches(*edit, state) || !std::isfinite(display_caret.x) || !std::isfinite(display_caret.y) ||
        !std::isfinite(display_caret.width) || !std::isfinite(display_caret.height)) return {};
    return TextInputPresentation{stamp.target, edit, display_caret, stamp.revision,state.composition_reset_serial};
}

bool text_input_is_current(EditInputContext context, const WidgetPtr& scope,
                           const TextInputPresentation& input) {
    if ((context.live && !context.live()) ||
        popup_suspends_text_input(context)) return false;
    if (context.editing.focus.revision()!=input.focus_revision ||
        context.editing.focus.focused()!=input.owner || !scope ||
        !scope->visible() || !scope->enabled() || !input.owner ||
        !input.owner->visible() || !input.owner->enabled() ||
        focus_target_in_scope(scope,input.owner)!=input.owner ||
        input.editor_required != static_cast<bool>(input.editor)) return false;
    if (!input.editor) {
        const auto registration = input.recipient_registration.lock();
        if (!registration || !registration->on_text_input || !registration->on_key_down ||
            registration != input.owner->committed_text_input.registration ||
            registration->owner_lifetime.expired() || registration->parent_lifetime.expired() ||
            (registration->current && !registration->current()) ||
            registration->parent_lifetime.lock() != input.owner->committed_text_input.lifetime ||
            !registration->anchor) return false;
        const auto anchor = registration->anchor;
        const auto bounds = anchor();
        return bounds && bounds->x == input.caret_bounds.x &&
            bounds->y == input.caret_bounds.y &&
            bounds->width == input.caret_bounds.width &&
            bounds->height == input.caret_bounds.height &&
            registration == input.owner->committed_text_input.registration &&
            !registration->owner_lifetime.expired() && !registration->parent_lifetime.expired() &&
            (!registration->current || registration->current()) &&
            context.editing.focus.revision() == input.focus_revision &&
            context.editing.focus.focused() == input.owner;
    }
    if (!input.editor->visible() || !input.editor->enabled() || input.editor->read_only ||
        edit_input_owner(context,input.editor)!=input.owner) return false;
    if (edit_input_text_state(context,input.editor).composition_reset_serial !=
        input.composition_reset_serial) return false;
    if (const auto number=std::dynamic_pointer_cast<NumberEdit>(input.owner)) return !number->read_only;
    if (const auto combo=std::dynamic_pointer_cast<ComboBox>(input.owner)) return combo->editable;
    return true;
}

std::optional<TextInputArea> map_text_input_area(Rect caret, const TextInputPointMap& map) {
    const auto finite=[](Point p) { return std::isfinite(p.x) && std::isfinite(p.y); };
    if (!map || !finite({caret.x,caret.y}) || !std::isfinite(caret.width) ||
        !std::isfinite(caret.height) || caret.width<=0 || caret.height<=0) return {};
    const auto a=map({caret.x,caret.y});
    const auto b=map({caret.x+caret.width,caret.y+caret.height});
    if (!a || !b || !finite(*a) || !finite(*b) || b->x<=a->x || b->y<=a->y) return {};
    const double x=std::floor(double(a->x)), y=std::floor(double(a->y));
    const double width=std::ceil(double(b->x))-x, height=std::ceil(double(b->y))-y;
    constexpr double low=std::numeric_limits<int>::min(), high=std::numeric_limits<int>::max();
    if (x<low || y<low || x>high || y>high || width>high || height>high ||
        x+width>high || y+height>high) return {};
    return TextInputArea{int(x),int(y),int(width),int(height)};
}

static EditInputResult dispatch_edit_text_impl(EditInputContext context,
    WidgetPtr scope, TextInputEvent input, const TextComposition* composition) {
    if (const auto popup = dispatch_retained_focus_popup(context, scope, nullptr, false)) return *popup;
    if (const auto blocked = prepare_keyboard_dispatch(context, scope)) return *blocked;
    const bool preedit = input.composing;
    const FocusStamp stamp{context.editing.focus, scope, context.live, context.menu};
    if (!stamp.valid() || !stamp.target) return {true};
    auto& inline_edit = context.editing.inline_edit;
    // 編集欄の解決はIME表示位置の要求ではない。 ここで文書全体を計測
    // すると、入力前に提供処理が走り、別の未確定入力へ対象が変わる場合が
    // ある。 計測は、完成した入力を見える位置へ移すときに一度だけ行う。
    const auto active_editor=resolve_input_editor(context,stamp,!preedit);
    const bool number_input=context.editing.number_edit.active_for(stamp.target);
    const bool combo_input=context.editing.combo_edit.active_for(stamp.target);
    const auto number_revision=context.editing.number_edit.revision();
    const auto combo_revision=context.editing.combo_edit.revision();
    const auto editor_current=[&] {
        return stamp.valid() && active_editor && active_editor->enabled() && active_editor->visible() &&
            !active_editor->read_only && !popup_suspends_text_input(context) &&
            edit_input_owner(context,active_editor)==stamp.target &&
            (!number_input || context.editing.number_edit.revision()==number_revision) &&
            (!combo_input || context.editing.combo_edit.revision()==combo_revision);
    };
    if (!stamp.valid()) return {true,true};
    bool removed_composition{};
    if (!preedit && active_editor) {
        auto& state=edit_input_text_state(context,active_editor);
        removed_composition=state.composition.has_value();
        state.composition.reset();
    }
    if (!input.composing && inline_edit.active()) {
        if (inline_edit.owner() != stamp.target || !widget_in_input_scope(scope, inline_edit.owner())) {
            context.editing.combo_popup.close(); inline_edit.cancel();
            return {true, true};
        }
        if(!active_editor)return {true,true};
        const auto choice=inline_edit.choice();
        const auto logical=choice ? std::static_pointer_cast<Widget>(choice) : std::static_pointer_cast<Widget>(active_editor);
        const TextEditSnapshot before(*active_editor,inline_edit.text_state());
        if(logical && logical->on_text_input) {
            const auto notify=logical->on_text_input;
            notify(input);
            // 読み取り専用のコンボ検索には、テキスト編集の変更可否検査よ
            // り前に確定文字を渡す。 コールバックで置換先のインライン編
            // 集へ入力を向け直したり、古いバッファを復活させたりしない。
            if(!stamp.valid() || !inline_edit.active() || inline_edit.editor()!=active_editor ||
                inline_edit.choice()!=choice)return {true,true};
            if(input.handled)return {true,true};
            if(!before.matches(*active_editor,inline_edit.text_state()))return {true,true};
        }
        if (!editor_current()) return {true,removed_composition};
        const auto popup_revision=context.editing.combo_popup.revision();
        const bool changed = inline_edit.insert(input.text);
        if (changed && context.editing.combo_popup.revision()==popup_revision)
            context.editing.combo_popup.close();
        return finish_input(context, stamp, {true, changed || removed_composition, active_editor});
    }
    std::optional<TextEditSnapshot> before_notification;
    if (active_editor) before_notification.emplace(*active_editor,edit_input_text_state(context,active_editor));
    if (stamp.target->on_text_input) {
        const auto notify = stamp.target->on_text_input;
        notify(input);
    }
    if (!stamp.valid()) return {true, true};
    if (!input.handled && !active_editor && stamp.target) {
        const auto registration = stamp.target->committed_text_input.registration;
        if (registration && !registration->owner_lifetime.expired() &&
            !registration->parent_lifetime.expired() && registration->on_text_input &&
            (!registration->current || registration->current()) &&
            registration->parent_lifetime.lock() == stamp.target->committed_text_input.lifetime) {
            const auto notify = registration->on_text_input;
            if (notify(input)) input.handled = true;
        }
    }
    if (!stamp.valid()) return {true, true};
    if (input.handled) return {true,removed_composition};
    if (!editor_current()) return {true,true};
    // 通知がフォーカスを変えずに値を置き換える場合がある。同期処理はコ
    // ールバックを呼ばず、 新しい値・未確定入力を保ち、古いイベントを
    // 追加しない。 input.text の変換は引き続き利用できる。
    if (context.editing.number_edit.active_for(stamp.target)) context.editing.number_edit.synchronize();
    if (context.editing.combo_edit.active_for(stamp.target)) context.editing.combo_edit.synchronize();
    if (!editor_current() || (before_notification &&
        !before_notification->matches(*active_editor,edit_input_text_state(context,active_editor)))) return {true,true};
    if (preedit || input.composing) {
        auto value=composition ? *composition : TextComposition{input.text,{},input.text.size()};
        value.text=input.text; // コールバックによる置換を尊重するが、確定はしない。
        auto& state=edit_input_text_state(context,active_editor);
        const TextEditSnapshot before(*active_editor,state);
        auto prepared=before;
        const auto current=[&] {
            return editor_current() && before.matches(*active_editor,state);
        };
        update_text_composition(prepared.edit,prepared.state,std::move(value),stamp.revision);
        if (auto view=project_text_composition(prepared.edit,prepared.state)) {
            const auto metrics=context.metrics(active_editor);
            if (!current() || !metrics.measure || !std::isfinite(metrics.line_height) || metrics.line_height<=0)
                return {true,true};
            const auto inner=text_edit_inner_bounds(prepared.edit);
            const auto lines=layout_text_lines(view->edit,inner.width,metrics.measure);
            reveal_text_edit_caret(view->edit,view->state,lines,inner,metrics.line_height,metrics.measure);
            prepared.state.scroll_x=view->state.scroll_x; prepared.state.scroll_y=view->state.scroll_y;
        }
        if (!current()) return {true,true};
        state=std::move(prepared.state);
        return {true,true,active_editor};
    }
    EditInputResult result{true};
    if (const auto number = std::dynamic_pointer_cast<NumberEdit>(stamp.target)) {
        result.redraw = context.editing.number_edit.insert(input.text);
        result.editor = active_editor;
    } else if (const auto edit = std::dynamic_pointer_cast<TextEdit>(stamp.target)) {
        result.redraw = replace_text_selection(*edit, edit_input_text_state(context, edit), input.text);
        result.editor = edit;
    } else if (const auto combo = std::dynamic_pointer_cast<ComboBox>(stamp.target); combo && combo->editable) {
        context.editing.combo_popup.close();
        result.redraw = context.editing.combo_edit.insert(input.text);
        result.editor = active_editor;
    }
    // 確定が拒否・無変更でも、表示中の未確定文字は終了する。
    // 別のイベントを待つと、古い変換表示が画面に残ってしまう。
    result.redraw = result.redraw || removed_composition;
    return finish_input(context, stamp, result);
}

static EditInputResult dispatch_edit_key_impl(EditInputContext context, WidgetPtr scope, KeyEvent key) {
    if (const auto popup = dispatch_retained_focus_popup(context, scope, &key, true)) return *popup;
    if (const auto blocked = prepare_keyboard_dispatch(context, scope)) return *blocked;
    const FocusStamp stamp{context.editing.focus, scope, context.live, context.menu};
    if (dispatch_menu_key(context, scope, key, true)) return {true, true};
    if (!stamp.valid()) return {true};
    auto& inline_edit = context.editing.inline_edit;
    auto& popup = context.editing.combo_popup;
    const auto active_editor = resolve_input_editor(context,stamp);
    if (active_editor) {
        const auto& editor = active_editor;
        auto& state=edit_input_text_state(context,editor);
        if (state.composition) {
            if (key.key==Key::Escape) { state.cancel_composition(); return {true,true,editor}; }
            // 変換・移動キーはIMEが管理する。 改行挿入、セ
            // ルの確定、ダイアログのボタン操作には使わない。
            const bool command=modifier(key.modifiers,Modifiers::Control|Modifiers::Meta|Modifiers::Alt);
            if (!command) return {true};
            state.cancel_composition();
        }
    }
    if (!stamp.valid()) return {true,true};
    if (inline_edit.active()) {
        if (inline_edit.owner() != stamp.target || !widget_in_input_scope(scope, inline_edit.owner())) {
            popup.close(); inline_edit.cancel(); return {true, true};
        }
        const auto editor = inline_edit.editor();
        const auto number_revision = context.editing.number_edit.revision();
        const auto combo_revision = context.editing.combo_edit.revision();
        const auto live = [&] {
            return stamp.valid() && context.editing.number_edit.revision()==number_revision &&
                context.editing.combo_edit.revision()==combo_revision;
        };
        if (editor->on_key_down) {
            const auto notify = editor->on_key_down;
            notify(key);
            if (!live() || !inline_edit.active() || inline_edit.editor() != editor) return {true, true};
            if (key.handled) return finish_input(context, stamp, {true, true, editor});
        }
        const TextEditSnapshot before(*editor, inline_edit.text_state());
        const auto popup_revision = popup.revision();
        const auto metrics = context.metrics(editor);
        if (!live() || !inline_edit.active() || inline_edit.editor() != editor ||
            !before.matches(*editor, inline_edit.text_state()) || popup.revision() != popup_revision ||
            !metrics.measure || !std::isfinite(metrics.line_height) || metrics.line_height <= 0)
            return {true, true};
        const bool handled = dispatch_inline_edit_key(inline_edit, popup, key.key, key.modifiers,
            context.combo_visible_rows, metrics.line_height, metrics.measure, context.clipboard, live);
        if (!stamp.valid()) return {true, true};
        if (handled) return finish_input(context, stamp,
            {true, true, inline_edit.active() && inline_edit.editor()==editor ? editor : nullptr});
    }
    const auto combo = std::dynamic_pointer_cast<ComboBox>(stamp.target);
    if (combo && popup.is_open_for(combo) && popup.handle_key(combo, key.key,
        modifier(key.modifiers, Modifiers::Alt), popup_rows(context, scope, combo),
        modifier(key.modifiers, Modifiers::Control | Modifiers::Meta))) {
        if (!stamp.valid()) return {true, true};
        context.editing.combo_edit.synchronize();
        // 履歴の選択を確定した後も、所有する複合部品のEnterフィ
        // ルターへ渡す。 D版の表示フィルターはコンボの子孫にも適
        // 用されるため。 既定ボタンやコンボのキー処理は再実行しな
        // い。選択コールバックが別のポップアップを開く場合もある。
        if (key.key == Key::Enter && !popup.combo()) {
            const auto ancestors = input_scope_path(scope, stamp.target);
            for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it) {
                if (!stamp.valid()) return {true, true};
                const auto filter = (*it)->on_descendant_key_down;
                if (filter) filter(key);
                if (!stamp.valid() || key.handled) return {true, true};
                // 同じ範囲内の親の変更では、フォーカスは変わら
                // ない。 古い元の祖先へこのキーを送らない。
                if (input_scope_path(scope, stamp.target) != ancestors)
                    return {true, true};
            }
        }
        return {true, true};
    }
    if (stamp.target && stamp.target->on_key_down) {
        const auto notify = stamp.target->on_key_down;
        notify(key);
    }
    if (!stamp.valid()) return {true, true};
    if (!key.handled && !active_editor && stamp.target) {
        const auto registration = stamp.target->committed_text_input.registration;
        if (registration && !registration->owner_lifetime.expired() &&
            !registration->parent_lifetime.expired() && registration->on_key_down &&
            (!registration->current || registration->current()) &&
            registration->parent_lifetime.lock() == stamp.target->committed_text_input.lifetime) {
            const auto notify = registration->on_key_down;
            if (notify(key)) key.handled = true;
        }
    }
    if (!stamp.valid()) return {true, true};
    if (key.handled) return {true};
    if (key.key == Key::Escape && context.drag_drop && context.drag_drop->active()) {
        const auto session = context.drag_drop->session_id();
        if (context.mouse) context.mouse->cancel_for();
        // 捕捉した接触のキャンセルで、ドラッグがすでに完了する場合
        // がある。 完了・キャンセルのコールバックが新しい操作を始
        // めても、古いEscape処理でそれをキャンセルしない。
        if (context.drag_drop->session_id() == session) context.drag_drop->cancel();
        return {true, true};
    }
    // D版の表示フィルターと同様、所有する複合部品の子孫に適用する。 フォ
    // ーカス中の子・メニューの処理を先に行い、共通の有効範囲の経路を使う。
    const auto ancestors=input_scope_path(scope,stamp.target);
    for(auto it=ancestors.rbegin();it!=ancestors.rend();++it) {
        if(!stamp.valid())return {true,true};
        const auto filter=(*it)->on_descendant_key_down;
        if(filter)filter(key);
        if(!stamp.valid())return {true,true};
        if(key.handled)return {true,true};
        if(input_scope_path(scope,stamp.target)!=ancestors)return {true,true};
    }
    if (context.before_default_key && context.before_default_key(key.key, key.modifiers)) return {true, true};
    if (!stamp.valid()) return {true, true};
    if (const auto toolbar = std::dynamic_pointer_cast<ToolBar>(stamp.target);
        toolbar && handle_tool_bar_key(*toolbar, key.key, key.modifiers))
        return finish_input(context, stamp, {true, true});
    if (!stamp.valid()) return {true, true};
    const auto edit_column = [&stamp] {
        const auto table = std::dynamic_pointer_cast<TableView>(stamp.target);
        return table ? table->inline_edit_column.value_or(0U) : 0U;
    }();
    if (key.key == Key::F2 &&
        begin_selected_item_edit(inline_edit, stamp.target, context.row_height,
                                 edit_column)) {
        if (!stamp.valid()) return {true, true};
        // D版の EditEnd.setFocus と同様、ALL固有の上書きなしで comboListVisible を適用する。
        if (combo_edit_popup_enabled()) {
            if (const auto choice = inline_edit.choice())
                (void)popup.open(choice, popup_rows(context, scope, choice));
        }
        return finish_input(context, stamp, {true, true, inline_edit.editor()});
    }
    if (const auto well=std::dynamic_pointer_cast<ColorWell>(stamp.target);
        well && (key.key==Key::Enter || key.key==Key::Space) &&
        !modifier(key.modifiers,Modifiers::Control|Modifiers::Meta|Modifiers::Alt)) {
        // モーダル開始によるフォーカス変更は正当なもの。
        // 共通処理内で表示・値を検証し、通常の入力完了経路
        // で現在のフォーカスを反映する。 このキーの自動反
        // 復でOKを押したり選択画面を開き直したりしない。
        if (key.repeat) return {true};
        const bool opened=context.pick_color_async
            ? activate_color_selection_async(scope,well,context.pick_color_async,context.live)
            : activate_color_selection(scope,well,context.pick_color,context.live);
        return {true,opened};
    }
    EditInputResult result;
    if (const auto number = std::dynamic_pointer_cast<NumberEdit>(stamp.target)) {
        context.editing.number_edit.begin(number);
        if (!stamp.valid()) return {true, true};
        const auto metrics = context.metrics(number);
        if (!stamp.valid() || !context.editing.number_edit.active_for(number)) return {true, true};
        result.handled = context.editing.number_edit.key(key.key, key.modifiers,
            metrics.line_height, metrics.measure, context.clipboard, [&] {return stamp.valid();});
        result.editor = context.editing.number_edit.editor();
    } else if (const auto edit = std::dynamic_pointer_cast<TextEdit>(stamp.target)) {
        const auto metrics = context.metrics(edit);
        if (!stamp.valid()) return {true, true};
        result.handled = dispatch_text_edit_key(*edit, edit_input_text_state(context, edit),
            key.key, key.modifiers, text_edit_inner_bounds(*edit), metrics.line_height,
            metrics.measure, context.clipboard, {[&] {return stamp.valid();}, {}});
        result.editor = edit;
    } else if (combo) {
        if (combo->editable) {
            context.editing.combo_edit.begin(combo);
            const auto metrics = context.metrics(combo);
            if (!stamp.valid() || !context.editing.combo_edit.active_for(combo)) return {true, true};
            const auto rows = popup_rows(context, scope, combo);
            if (!stamp.valid() || !context.editing.combo_edit.active_for(combo)) return {true, true};
            result.handled = context.editing.combo_edit.key(key.key, key.modifiers, popup,
                rows, metrics.line_height, metrics.measure, context.clipboard, [&] {return stamp.valid();});
            result.editor = context.editing.combo_edit.editor();
        } else result.handled = popup.handle_key(combo, key.key, modifier(key.modifiers, Modifiers::Alt),
            popup_rows(context, scope, combo), modifier(key.modifiers, Modifiers::Control | Modifiers::Meta));
    } else {
        const auto item = handle_item_view_key(stamp.target, key.key,
            item_view_visible_row_count(stamp.target, context.row_height), key.modifiers);
        result.handled = item.handled;
        if (item.layout_changed) result.changed_tabs = std::dynamic_pointer_cast<TabView>(stamp.target);
    }
    if (!stamp.valid()) return {true, true};
    if (!result.handled) result.handled = handle_control_key(scope, stamp.target, key.key);
    if (!stamp.valid()) return {true, true};
    if (!result.handled) result.handled = dispatch_menu_shortcuts(scope, key.key, key.modifiers, {}, key.physical_control);
    if (!stamp.valid()) return {true, true};
    if (!result.handled) {
        const auto focus = dispatch_edit_focus_key(context.editing, scope, key.key, key.modifiers);
        result.handled = focus.handled;
        if (focus.changed_tabs) result.changed_tabs = focus.changed_tabs;
    }
    if (!stamp.valid()) { result.handled = result.redraw = true; result.editor.reset(); return result; }
    if (!result.handled) result.handled = handle_dialog_key(scope, key.key, key.modifiers);
    result.redraw = result.handled;
    return finish_input(context, stamp, result);
}

static EditInputResult dispatch_edit_key_up_impl(EditInputContext context, WidgetPtr scope, KeyEvent key) {
    if (const auto popup = dispatch_retained_focus_popup(context, scope, &key, false)) return *popup;
    if (const auto blocked = prepare_keyboard_dispatch(context, scope)) return *blocked;
    const FocusStamp stamp{context.editing.focus, scope, context.live, context.menu};
    if (dispatch_menu_key(context, scope, key, false)) return {true, true};
    if (stamp.valid() && stamp.target && stamp.target->on_key_up) {
        const auto notify = stamp.target->on_key_up;
        notify(key);
        return {true, !stamp.valid()};
    }
    return {};
}

namespace {
template<class Dispatch>
EditInputResult dispatch_with_hover_dismissal(EditInputContext context,
    const WidgetPtr& scope, Dispatch dispatch) {
    if (!scope || !scope->visible() || !scope->enabled() || (context.live && !context.live()))
        return {true};
    // アプリのコールバックより先に古い案内を終了する。 コールバックが新し
    // いウィンドウ・ホバー状態に入っても、後から今回の通知処理で閉じない。
    const bool dismissed=context.hover && context.hover->tooltip().dismiss_for(scope);
    auto result=dispatch();
    result.redraw=result.redraw || dismissed;
    return result;
}
}
EditInputResult dispatch_edit_text(EditInputContext context,
    WidgetPtr scope, TextInputEvent input, const TextComposition* composition) {
    return dispatch_with_hover_dismissal(context,scope,[&] {
        return dispatch_edit_text_impl(context,scope,input,composition);
    });
}
EditInputResult dispatch_edit_key(EditInputContext context, WidgetPtr scope, KeyEvent key) {
    return dispatch_with_hover_dismissal(context,scope,[&] {
        return dispatch_edit_key_impl(context,scope,key);
    });
}
EditInputResult dispatch_edit_key_up(EditInputContext context, WidgetPtr scope, KeyEvent key) {
    return dispatch_with_hover_dismissal(context,scope,[&] {
        return dispatch_edit_key_up_impl(context,scope,key);
    });
}

}  // namespace cwxeditor::lib
