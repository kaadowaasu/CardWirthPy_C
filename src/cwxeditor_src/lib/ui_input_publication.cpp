#include "ui_input_publication.hpp"
#include "ui_layout.hpp"
#include "ui_metrics.hpp"
#include <algorithm>
#include <exception>

namespace cwxeditor::lib {
namespace {
bool attached(const InputPublicationContext& context,const std::shared_ptr<Window>& scope) {
    const auto present=[&] {return scope && scope->visible() && context.stack.owner_of(scope)==scope;};
    if(!present() || (context.live && !context.live(scope))) return false;
    return present(); // ネイティブの寿命確認の提供処理が、再入したり範囲を切り離したりする場合がある。
}
bool available(const InputPublicationContext& context,const std::shared_ptr<Window>& scope) {
    return attached(context,scope) && scope->enabled() && !scope->overlay_surface && context.modal.allows(scope);
}
template<class Action>
void attempt(std::exception_ptr& failure,Action&& action) {
    try {action();} catch(...) {if(!failure) failure=std::current_exception();}
}
void redraw_once(const InputPublicationContext& context,const std::shared_ptr<Window>& scope,
    std::vector<std::shared_ptr<Window>>& requested,std::exception_ptr& failure) {
    if(!scope || std::find(requested.begin(),requested.end(),scope)!=requested.end()) return;
    attempt(failure,[&] {
        requested.push_back(scope);
        // モーダルで入力禁止・無効な所有者でも、古い強調表示は消す必要がある。
        if(context.redraw && attached(context,scope)) context.redraw(scope);
    });
}
bool same_area(TextInputArea a,TextInputArea b) {
    return a.x==b.x && a.y==b.y && a.width==b.width && a.height==b.height;
}
}
bool publish_edit_focus(InputPublicationContext context,const WidgetPtr& target) {
    const auto scope=context.stack.owner_of(target);
    if(target && !available(context,scope)) return false;
    const auto previous=context.stack.owner_of(context.edit.editing.focus.focused());
    std::exception_ptr failure;
    attempt(failure,[&] {set_edit_focus(context.edit.editing,target,scope);});
    const auto changed=context.stack.owner_of(context.edit.editing.focus.focused());
    attempt(failure,[&] {if(context.publish_focus) context.publish_focus();});
    const auto actual=context.stack.owner_of(context.edit.editing.focus.focused());
    std::vector<std::shared_ptr<Window>> requested;
    for(const auto& window : {previous,changed,actual}) redraw_once(context,window,requested,failure);
    if(failure) std::rethrow_exception(failure);
    return true;
}
bool activate_control_focus(InputPublicationContext context,const WidgetPtr& target) {
    if (!target) return false;
    // キーボード操作しないコントロールの有効化動作は保ち、
    // フォーカス遷移自体は一つの共通の反映経路にまとめる。
    (void)publish_edit_focus(context,target);
    return !accepts_keyboard_focus(target) || context.edit.editing.focus.focused()==target;
}
bool publish_and_require_edit_focus(InputPublicationContext context,const WidgetPtr& target) {
    if (!target) return false;
    (void)publish_edit_focus(context,target);
    return context.edit.editing.focus.focused()==target;
}
bool complete_edit_input(InputPublicationContext context,
    std::shared_ptr<Window> scope,const EditInputResult& result) {
    std::exception_ptr failure;
    bool accepted{};
    attempt(failure,[&] {
        accepted=available(context,scope);
        if(accepted && result.changed_tabs && widget_in_input_scope(scope,result.changed_tabs))
            arrange_widget(result.changed_tabs,result.changed_tabs->bounds());
    });
    attempt(failure,[&] {
        if(result.redraw && context.redraw && attached(context,scope)) context.redraw(scope);
    });
    // 通知・配置が範囲を無効化しても、現在のフォーカス・IMEを反映する。
    // 閉じた・置換されたネイティブ世代の入力スナップショットは使わない。
    attempt(failure,[&] {if(context.publish_focus) context.publish_focus();});
    if(failure) std::rethrow_exception(failure);
    return accepted;
}
namespace {
template<class Dispatch>
EditInputResult dispatch_and_publish(InputPublicationContext context,
    const std::shared_ptr<Window>& scope,Dispatch&& dispatch,const std::function<void()>& finalized={}) {
    const auto previous=context.stack.owner_of(context.edit.editing.focus.focused());
    const auto revision=context.edit.editing.focus.revision();
    EditInputResult result{true};
    std::exception_ptr failure;
    try {
        if(!available(context,scope)) return result;
        // 呼び出し元の任意の適格性検査は維持し、その後で所有関係・モーダル状態・
        // ネイティブの寿命を再検証する。 マップの項目を取り込んで保持しない。
        const auto original_live=context.edit.live;
        auto edit=context.edit;
        edit.live=[context,scope,original_live] {
            return (!original_live || original_live()) && available(context,scope);
        };
        result=dispatch(edit);
    } catch(...) {
        failure=std::current_exception();
        // コールバックが、再描画必要の結果を返す前にモデル変更・案内の
        // 終了を行い、例外を出す場合がある。 この範囲だけを更新する。
        result={true,true};
    }
    attempt(failure,[&] {(void)complete_edit_input(context,scope,result);});
    attempt(failure,[&] {if(finalized) finalized();});
    if(context.edit.editing.focus.revision()!=revision) {
        std::vector<std::shared_ptr<Window>> requested;
        // 要求されていれば、完了処理がこの範囲の再描画を試行済み。
        if(result.redraw) attempt(failure,[&] {requested.push_back(scope);});
        const auto current=context.stack.owner_of(context.edit.editing.focus.focused());
        redraw_once(context,previous,requested,failure);
        redraw_once(context,current,requested,failure);
    }
    if(failure) std::rethrow_exception(failure);
    return result;
}
}
EditInputResult dispatch_and_publish_edit_key(InputPublicationContext context,
    std::shared_ptr<Window> scope,KeyEvent key,bool down) {
    return dispatch_and_publish(context,scope,[&](EditInputContext edit) {
        return down ? dispatch_edit_key(edit,scope,key) : dispatch_edit_key_up(edit,scope,key);
    });
}
EditInputResult dispatch_and_publish_edit_text(InputPublicationContext context,
    std::shared_ptr<Window> scope,TextInputEvent input,const TextComposition* composition) {
    return dispatch_and_publish(context,scope,[&](EditInputContext edit) {
        return dispatch_edit_text(edit,scope,input,composition);
    });
}
PassiveInputResult dispatch_and_publish_wheel(InputPublicationContext context,
    PassiveInputContext passive,std::shared_ptr<Window> scope,WheelEvent event) {
    PassiveInputResult result{true};
    (void)dispatch_and_publish(context,scope,[&](EditInputContext edit) {
        const auto passive_live=passive.live;
        passive.live=[passive_live,live=edit.live] {
            return (!passive_live || passive_live()) && (!live || live());
        };
        result=dispatch_wheel_input(passive,scope,event);
        return EditInputResult{result.consumed,result.redraw};
    },[&] {
        // スクロールがカーソルを移動したり、モーダルを閉じたり、項目を置き換えたり
        // する場合がある。 先に現在のフォーカスを反映し、古い項目を通知しない。
        if(result.changed_control && context.publish_control && available(context,scope) &&
            widget_in_input_scope(scope,result.changed_control)) context.publish_control(result.changed_control);
    });
    return result;
}
namespace {
bool same_bounds(Rect a,Rect b) {
    return a.x==b.x && a.y==b.y && a.width==b.width && a.height==b.height;
}
template<class Dispatch>
PointerInputResult publish_pointer_dispatch(PointerPublicationContext context,
    PointerInputContext pointer,const std::shared_ptr<Window>& scope,Dispatch&& dispatch) {
    // ホバー・選択のコールバックが、ネイティブのマップ項目を破棄・置換する場合があ
    // る。 見出しの判定には値だけを使い、ネイティブの Rect を借用しない。
    std::optional<Rect> caption_bounds;
    if(pointer.surface && pointer.surface->bounds) {
        caption_bounds=*pointer.surface->bounds;
        pointer.surface->bounds=&*caption_bounds;
    }
    const auto initial_bounds=caption_bounds;
    auto& input=context.input;
    // ポインターのコールバックが、ホストを同期的に閉じる・親を
    // 変更する・倍率を変える場合がある。 取得済みのネイティ
    // ブ寿命検査で画面の置換を検出し、所有者・倍率も共通の論
    // 理スナップショットに含める。 いずれかが変わった後は、
    // 古い表示の配置・カーソル・再描画・IMEを反映しない。
    const auto owner=scope ? scope->owner.lock() : std::shared_ptr<Window>{};
    const auto scale_generation=editor_ui_text_scale_generation();
    const auto current=[input,scope,owner,scale_generation] {
        return available(input,scope) && (!scope || scope->owner.lock()==owner) &&
            scale_generation==editor_ui_text_scale_generation();
    };
    const auto previous=input.stack.owner_of(input.edit.editing.focus.focused());
    const auto revision=input.edit.editing.focus.revision();
    PointerInputResult result;
    std::exception_ptr failure;
    try {
        if(!current()) {result.handled=true;return result;}
        const auto bind_live=[&](std::function<bool()> original) {
            return [original,current] {
                return (!original || original()) && current();
            };
        };
        pointer.live=bind_live(pointer.live);
        pointer.editing.live=bind_live(pointer.editing.live);
        pointer.passive.live=bind_live(pointer.passive.live);
        result=dispatch(pointer);
    } catch(...) {
        failure=std::current_exception();result.handled=true;result.redraw=true;
        // 見出しの移動は、例外を出し得るホバーの後処理より先に行う。
        // 適用済みの移動を保ち、新しい再入による配置を上書きしない。
        if(caption_bounds && initial_bounds && !same_bounds(*caption_bounds,*initial_bounds))
            result.window_bounds=caption_bounds;
    }
    attempt(failure,[&] {
        if(result.window_bounds && current() &&
            same_bounds(scope->bounds(),*result.window_bounds)) {
            input.stack.set_bounds(scope,*result.window_bounds);
            if(context.realize_bounds) context.realize_bounds(scope,*result.window_bounds);
        }
    });
    bool close_dispatched{};
    attempt(failure,[&] {
        if(result.close_requested && current() && context.request_close) {
            // ホストが実際の破棄を遅らせても、終了要求でこの入力
            // スナップショットの処理は終わる。 その後に古いフ
            // ォーカス・IME・カーソル・再描画を反映しない。
            close_dispatched=true;
            context.request_close(scope);
        }
    });
    attempt(failure,[&] {
        // 最小化も終了と同様にウィンドウを隠すため、 その後
        // に古いフォーカス・カーソル・再描画を反映しない。
        if(!close_dispatched && result.minimize_requested && current() &&
           context.request_minimize) {
            close_dispatched=true;
            context.request_minimize(scope);
        }
    });
    attempt(failure,[&] {
        // 見出しの取り消し・やり直しは、終了操作と同様、入力の捕捉を解放してから行う。
        if(!close_dispatched && current() && context.request_history &&
           (result.undo_requested || result.redo_requested)) {
            context.request_history(scope,result.redo_requested);
            result.redraw=true;
        }
    });
    std::vector<std::shared_ptr<Window>> requested;
    if(!close_dispatched && (result.redraw || failure) && current())
        redraw_once(input,scope,requested,failure);
    attempt(failure,[&] {
        if(!close_dispatched && result.cursor && current() && context.realize_cursor)
            context.realize_cursor(*result.cursor);
    });
    // マウスのホバーで、毎フレーム全IMEセッションを開始・通知しない。
    // フォーカス要求、実際の変更、終了、失敗、無効化のときに反映する。
    attempt(failure,[&] {
        const auto focus_owner=input.stack.owner_of(input.edit.editing.focus.focused());
        // 無効になった元の範囲が、別の有効な範囲へフォーカス
        // を移している場合がある。 現在の所有者は反映するが
        // 、終了中の範囲のIME・フォーカスを再開しない。
        const bool focus_current=!focus_owner || focus_owner!=scope || current();
        if(focus_current && (result.refresh_focus || failure ||
            input.edit.editing.focus.revision()!=revision || !attached(input,scope)) && input.publish_focus)
            input.publish_focus();
    });
    if(input.edit.editing.focus.revision()!=revision) {
        redraw_once(input,previous,requested,failure);
        redraw_once(input,input.stack.owner_of(input.edit.editing.focus.focused()),requested,failure);
    }
    if(failure) std::rethrow_exception(failure);
    return result;
}
}
PointerInputResult dispatch_and_publish_pointer(PointerPublicationContext context,
    PointerInputContext pointer,std::shared_ptr<Window> scope,PointerEvent event,bool primary_down,unsigned clicks) {
    return publish_pointer_dispatch(context,pointer,scope,[&](PointerInputContext current) {
        return dispatch_pointer_input(current,scope,event,primary_down,clicks);
    });
}
PointerInputResult advance_and_publish_pointer(PointerPublicationContext context,
    PointerInputContext pointer,std::shared_ptr<Window> scope) {
    // タイマーは見出しを移動できない。以前の対象の処理で、 この対象のネイティブ
    // マップ項目が消えている場合もあるため、借用した Rect を参照しない。
    pointer.surface.reset();
    return publish_pointer_dispatch(context,pointer,scope,[&](PointerInputContext current) {
        return advance_pointer_input(current,scope);
    });
}
void advance_and_publish_pointers(std::span<const PointerTimerTarget> targets) {
    // 再入で呼び出し元のベクターが置き換わる場合があるため、列挙内容を所有して保持する。
    const std::vector<PointerTimerTarget> snapshot(targets.begin(),targets.end());
    std::vector<std::shared_ptr<Window>> visited;
    std::exception_ptr failure;
    for(const auto& target:snapshot) attempt(failure,[&] {
        if(!target.scope || std::find(visited.begin(),visited.end(),target.scope)!=visited.end()) return;
        visited.push_back(target.scope);
        if(!pointer_timers_pending(target.pointer.mouse,target.pointer.touch)) return;
        (void)advance_and_publish_pointer(target.publication,target.pointer,target.scope);
    });
    if(failure) std::rethrow_exception(failure);
}
TextInputSurfacePlan plan_text_input_surfaces(const WindowStackStateMachine& stack,
    const ModalStateMachine& modal,const WidgetPtr& focused,std::span<const TextInputSurface> surfaces) {
    TextInputSurfacePlan plan;
    const auto owner=stack.owner_of(focused);
    std::uintptr_t active_surface{};
    if(owner && owner->visible() && owner->enabled() && !owner->overlay_surface &&
        focused && focused->enabled() && modal.allows(owner) && widget_in_input_scope(owner,focused)) {
        for(const auto& surface:surfaces) if(surface.scope==owner && surface.surface) {
            plan.active_scope=owner;active_surface=surface.surface;break;
        }
    }
    for(const auto& surface:surfaces) {
        if(!surface.scope || !surface.surface || surface.surface==active_surface ||
            stack.owner_of(surface.scope)!=surface.scope) continue;
        if(std::none_of(plan.stop.begin(),plan.stop.end(),[&](const auto& old) {
            return old.surface==surface.surface;
        })) plan.stop.push_back(surface);
    }
    return plan;
}

TextInputPublicationPlan TextInputPublicationStateMachine::prepare(TextInputPublicationContext context,
    std::optional<TextInputPresentation> input,NativeTextInputObservation native) {
    const auto current=[&] {
        return context.surface && context.scope && context.scope->visible() &&
            context.edit.editing.focus.revision()==context.focus_revision &&
            context.stack.owner_of(context.scope)==context.scope && context.modal.allows(context.scope) &&
            (!context.live || context.live());
    };
    if(!current()) return {};
    const auto focused=context.stack.owner_of(context.edit.editing.focus.focused());
    if(focused && focused!=context.scope) return {}; // 古い ALL ウィンドウから、兄弟のIMEを停止しない。
    const auto ticket=next_ticket_++;
    sessions_[context.surface].ticket=ticket;
    TextInputPublicationPlan plan;
    plan.surface=context.surface;plan.ticket=ticket;plan.kind=TextInputPublicationKind::Stop;
    if(!input || !text_input_is_current(context.edit,context.scope,*input))
        return current()?plan:TextInputPublicationPlan{};
    const auto area=map_text_input_area(input->caret_bounds,context.map);
    if(!current() || !text_input_is_current(context.edit,context.scope,*input)) return {};
    // 寿命確認のコールバックが反映処理へ再入し、sessions_ を再ハッシュ
    // する場合がある。 イテレーターは最後のコールバックが戻ってから取得する。
    const auto found=sessions_.find(context.surface);
    if(found==sessions_.end() || found->second.ticket!=ticket) return {};
    if(!area) return plan;
    const auto& session=found->second;
    plan.kind=TextInputPublicationKind::Activate;
    plan.clear_composition=native.active && (session.scope.lock()!=context.scope ||
        session.recipient.lock()!=input->owner || session.editor.lock()!=input->editor ||
        session.recipient_registration.lock()!=input->recipient_registration.lock() ||
        session.focus_revision!=input->focus_revision || session.editor_required!=input->editor_required ||
        session.reset_serial!=input->composition_reset_serial);
    plan.start=!native.active;
    plan.restore_ime_mode=plan.start || session.scope.lock()!=context.scope ||
        session.recipient.lock()!=input->owner || session.editor.lock()!=input->editor ||
        session.recipient_registration.lock()!=input->recipient_registration.lock() ||
        session.focus_revision!=input->focus_revision || session.editor_required!=input->editor_required;
    plan.update_area=plan.start || !native.area || !same_area(*native.area,*area);
    plan.area=*area;plan.scope=context.scope;plan.editor=input->editor;
    plan.recipient=input->owner;plan.recipient_registration=input->recipient_registration;
    plan.focus_revision=input->focus_revision;plan.reset_serial=input->composition_reset_serial;
    plan.editor_required=input->editor_required;
    return plan;
}
bool TextInputPublicationStateMachine::commit(const TextInputPublicationPlan& plan,bool succeeded) {
    const auto found=sessions_.find(plan.surface);
    if(!succeeded || plan.kind==TextInputPublicationKind::Ignore ||
        found==sessions_.end() || found->second.ticket!=plan.ticket) return false;
    if(plan.kind==TextInputPublicationKind::Stop) {
        const auto previous=found->second.recipient.lock();
        const auto previous_registration=found->second.recipient_registration.lock();
        if(previous && previous_registration &&
            previous->committed_text_input.published_registration.lock()==previous_registration)
            previous->committed_text_input.published_registration.reset();
        sessions_.erase(found);return true;
    }
    const auto recipient=plan.recipient.lock();
    const auto editor=plan.editor.lock();
    const auto registration=plan.recipient_registration.lock();
    if(plan.scope.expired() || !recipient || plan.editor_required != static_cast<bool>(editor) ||
        (!editor &&
        (!registration || recipient->committed_text_input.registration!=registration ||
         registration->owner_lifetime.expired() || registration->parent_lifetime.expired() ||
         (registration->current && !registration->current()) ||
         registration->parent_lifetime.lock()!=recipient->committed_text_input.lifetime))) return false;
    const auto previous=found->second.recipient.lock();
    const auto previous_registration=found->second.recipient_registration.lock();
    if(previous && previous_registration && previous->committed_text_input.published_registration.lock()==previous_registration)
        previous->committed_text_input.published_registration.reset();
    if(registration) recipient->committed_text_input.published_registration=registration;
    else recipient->committed_text_input.published_registration.reset();
    found->second={plan.ticket,plan.scope,plan.editor,plan.recipient,
        plan.recipient_registration,plan.focus_revision,plan.reset_serial,plan.editor_required};
    return true;
}
void TextInputPublicationStateMachine::retire(std::uintptr_t surface) {
    const auto found=sessions_.find(surface);
    if(found==sessions_.end())return;
    const auto recipient=found->second.recipient.lock();
    const auto registration=found->second.recipient_registration.lock();
    if(recipient && registration && recipient->committed_text_input.published_registration.lock()==registration)
        recipient->committed_text_input.published_registration.reset();
    sessions_.erase(found);
}
} // namespace cwxeditor::lib
