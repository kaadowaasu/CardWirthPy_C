#include "ui_window_lifecycle.hpp"
#include "ui_window_stack.hpp"
#include "ui_metrics.hpp"
#include "ui_passive_input.hpp"
#include <algorithm>
#include <cmath>
#include <exception>
#include <type_traits>
#include <unordered_map>

namespace cwxeditor::lib {

void configure_owned_window(Window& window,
    const std::shared_ptr<Window>& owner, const OwnedWindowKind kind,
    const OwnedWindowCenter center) {
    window.owner = owner;
    window.center_on_owner = center == OwnedWindowCenter::OwnerOrHost ||
        (center == OwnedWindowCenter::Owner && static_cast<bool>(owner));
    if (auto* dialog = dynamic_cast<Dialog*>(&window)) {
        dialog->modal = kind == OwnedWindowKind::Modal;
    }
}

void configure_owned_window(const std::shared_ptr<Window>& window,
    const std::shared_ptr<Window>& owner, const OwnedWindowKind kind,
    const OwnedWindowCenter center) {
    if (window) configure_owned_window(*window, owner, kind, center);
}

bool WindowTextScaleStateMachine::apply(float value,WindowTextScaleContext context) {
    if(!std::isfinite(value)) return false;
    const auto revision=++revision_;
    auto generation=editor_ui_text_scale_generation();
    const auto current=[&] {return revision==revision_ && generation==editor_ui_text_scale_generation();};
    std::exception_ptr failure;
    const auto attempt=[&](const auto& action) {
        try {action();return true;}
        catch(...) {if(!failure) failure=std::current_exception();return false;}
    };
    const auto perform=[&] {
        // Leave/Cancelが新しい入力を作る前に、古い接触を取得す
        // る。 配置の変更では、未確定編集・選択・フォーカスを捨てない。
        const auto cancel_mouse=context.input.mouse.prepare_cancel_for();
        const auto cancel_touch=context.input.touch.prepare_cancel_all();
        const auto drag_session=context.input.drag_drop?context.input.drag_drop->session_id():0;
        context.input.caption.cancel();
        context.input.chrome.reset();
        context.input.menu.close();
        context.input.edit.combo_popup.close();
        attempt([&] {context.input.hover.update({}, {}, {});});
        if(!current()) return;
        attempt([&] {
            if(context.input.drag_drop && context.input.drag_drop->session_id()==drag_session)
                context.input.drag_drop->cancel();
        });
        if(!current()) return;
        attempt(cancel_touch);
        if(!current()) return;
        attempt(cancel_mouse);
        if(!current() || failure) return;
        set_editor_ui_text_scale(value);
        generation=editor_ui_text_scale_generation();
        attempt([&] {if(context.refresh_metrics) context.refresh_metrics();});
        if(!current()) return;
        const auto surfaces=context.capture?context.capture():std::vector<WindowTextScaleSurface>{};
        if(!current()) return;
        std::vector<std::shared_ptr<Window>> owners;
        owners.reserve(surfaces.size());
        for(const auto& surface:surfaces) owners.push_back(surface.window?surface.window->owner.lock():nullptr);
        const auto plans=plan_window_text_scale_changes(surfaces);
        if(!current()) return;
        std::vector<bool> ordered(surfaces.size());
        std::vector<bool> rejected(surfaces.size());
        std::vector<std::size_t> order;
        const auto parent_index=[&](std::size_t i) {
            for(std::size_t j=0;owners[i] && j<surfaces.size();++j)
                if(surfaces[j].window==owners[i]) return j;
            return surfaces.size();
        };
        std::size_t remaining=surfaces.size();
        while(remaining) {
            bool progressed=false;
            for(std::size_t i=0;i<surfaces.size();++i) {
                if(ordered[i]) continue;
                const auto parent=parent_index(i);
                if(parent<surfaces.size() && !ordered[parent]) continue;
                ordered[i]=true;--remaining;progressed=true;order.push_back(i);
            }
            if(!progressed) {
                // 不正な循環では、取得済みの配置を維持し、一度だけ処理する。
                for(std::size_t i=0;i<surfaces.size();++i) if(!ordered[i]) {
                    ordered[i]=true;--remaining;order.push_back(i);
                }
            }
        }
        const auto live=[&](std::size_t i) {
            // 子の中央配置の計画は、取得済みの祖先に依存する。
            // 先のコールバックで祖先が失敗・終了・実体の世
            // 代変更・親変更した場合、その計画を適用しない。
            for(std::size_t visited=0;i<surfaces.size() && visited<surfaces.size();++visited) {
                const auto& window=surfaces[i].window;
                if(rejected[i] || !window || context.stack.owner_of(window)!=window ||
                   window->owner.lock()!=owners[i]) return false;
                if(context.live && !context.live(window)) return false;
                if(!current() || context.stack.owner_of(window)!=window || window->owner.lock()!=owners[i])
                    return false;
                i=parent_index(i);
            }
            return current();
        };
        for(const auto i:order) {
            if(!current()) return;
            const bool completed=attempt([&] {
                if(!live(i)) {rejected[i]=true;return;}
                const auto& window=surfaces[i].window;
                const bool realized=!context.realize || context.realize(window,plans[i]);
                if(!current()) return;
                if(!realized || !live(i)) {rejected[i]=true;return;}
                if(plans[i]) {
                    auto bounds=plans[i]->bounds;
                    if(!surfaces[i].change.embedded) bounds.x=bounds.y=0;
                    context.stack.set_bounds(window,bounds);
                }
                if(context.redraw) context.redraw(window);
            });
            if(!completed) rejected[i]=true;
            // 実体化が失敗した親は、計画した基準位置を提
            // 供できない。 独立したウィンドウと入力の反
            // 映を処理した後で、最初の例外を報告する。
            if(!current()) return;
        }
    };
    attempt(perform);
    if(current()) attempt([&] {if(context.publish_input) context.publish_input();});
    if(failure) std::rethrow_exception(failure);
    return current();
}

WindowInputRoute route_window_input(WindowInputLifecycleContext context,
    WindowStackStateMachine& stack, const ModalStateMachine& modal,
    const WindowInputRequest& request) {
    const bool keyboard = request.kind == WindowInputKind::Keyboard;
    const bool touch = request.kind == WindowInputKind::Touch;
    if (!keyboard) {
        const auto old_chrome = context.chrome.owner();
        context.chrome.discard_stale();
        // 無効なつかみ領域を終了させたイベントを、下のコン
        // トロールや別の組み込みウィンドウへ再送しない。
        if (old_chrome && !context.chrome.owner()) return {};
        // タッチで枠を操作中のマウス入力は、ウィンドウを前面に出す前に拒否する。
        if (!touch && context.chrome.touch_active()) return {};
    }
    const auto popup_owner = [&]() -> std::shared_ptr<Window> {
        if (context.menu.is_active()) return stack.owner_of(context.menu.owner());
        if (const auto combo = context.edit.combo_popup.combo()) {
            if (auto owner = stack.owner_of(combo)) return owner;
            if (combo == context.edit.inline_edit.choice())
                return stack.owner_of(context.edit.inline_edit.owner());
        }
        return {};
    };
    std::shared_ptr<Window> captured;
    if (keyboard) {
        // モードレスウィンドウは、編集フォーカスを移さずにメニューを開ける。 移動キー
        // を管理するのは、そのポップアップであり、別ウィンドウに残った編集欄ではない。
        captured = popup_owner();
        if (!captured) captured = stack.owner_of(context.edit.focus.focused());
    }
    else {
        captured = context.caption.owner();
        if (!captured) captured = context.chrome.scope();
        if (!captured && !touch) captured = context.mouse.owner();
        if (!captured && touch) captured = context.touch.owner(request.pointer);
        // 外側のクリックは、ポップアップ自身のウィンドウで閉じるた
        // めに使う。 先に背後の無関係なウィンドウを有効化しない。
        if (!captured) captured = popup_owner();
    }
    WindowInputRoute result;
    result.window = stack.input_window(request.surface, request.position, modal, captured);
    if (result.window && !keyboard && request.pressed) {
        stack.raise(result.window, &modal);
        result.raised = true;
    }
    return result;
}

void dispatch_window_notification(WindowNotificationContext context, WindowNotification notification) {
    if (!notification.surface) return;
    const auto members=context.stack.surface_windows(notification.surface);
    const auto present=[&](const std::shared_ptr<Window>& window) {
        if (!window || std::find(members.begin(),members.end(),window)==members.end() ||
            context.close.disposing(window) || !window->visible()) return false;
        const auto current=context.stack.surface_windows(notification.surface);
        return std::find(current.begin(),current.end(),window)!=current.end() &&
            (!context.live || context.live(window));
    };
    std::exception_ptr error;
    const auto attempt=[&](const auto& operation) {
        try { operation(); } catch (...) { if (!error) error=std::current_exception(); }
    };
    const auto redraw=[&](const std::shared_ptr<Window>& window) {
        attempt([&] { if (present(window) && context.input.redraw) context.input.redraw(window); });
    };
    if (notification.kind==WindowNotificationKind::Exposed ||
        notification.kind==WindowNotificationKind::GeometryChanged) {
        // 実際の画面の再描画要求は、前面ダイアログだけでなく共有画面全体に影響する。 所
        // 属のスナップショットには、コールバックで新しく作られたウィンドウを含めない。
        for (const auto& window:members) redraw(window);
    } else if (notification.kind==WindowNotificationKind::PointerLeft) {
        auto targets=context.stack.surface_windows(notification.surface);
        const auto hover_owner=context.stack.owner_of(context.input.hover.hovered());
        const bool hover_owner_on_surface = hover_owner &&
            std::find(members.begin(),members.end(),hover_owner)!=members.end();
        // 別の実体の画面かどうかは、所属だけで判定する。 この画面の所
        // 有者が非表示・失効している場合の、既存の代替処理は維持する。
        if (hover_owner && !hover_owner_on_surface) return;
        if (present(hover_owner)) targets={hover_owner};
        auto pointer=notification.pointer;
        pointer.pointer_type=PointerType::Mouse;
        pointer.action=PointerAction::Leave;
        // SDLは共通のホバー状態の最後のウィンドウ内座標を渡すが
        // 、 通知前にパレットが古い対象を隠す場合がある（MouseUp
        // で表示する場合など）。 owner_of(hovered())
        // が空でも、ポインターの範囲がこの画面に接続されてい
        // れば、 現在の範囲へ通知し、表示全体のMouseTrack
        // 監視にもネイティブのLeaveを届ける。 監視先が
        // 新しいポインター入力を同期送信したら、ホバーの変更
        // 番号が進んだ時点で古いLeaveの通知を止める。
        const auto hover_revision=context.input.hover.revision();
        for (const auto& target:targets) {
            if (!present(target)) continue;
            if (context.input.hover.revision()!=hover_revision) break;
            attempt([&] { notify_window_pointer_observers(target,pointer,{}); });
            if (context.input.hover.revision()!=hover_revision) break;
        }
        if (context.input.hover.revision()==hover_revision) {
            for (const auto& target:targets) {
                if (!present(target)) continue;
                attempt([&] { context.input.hover.clear_for(target,pointer); });
                if (context.input.hover.revision()!=hover_revision) break;
            }
        }
        for (const auto& target:targets) redraw(target);
    } else {
        const auto window=notification.kind==WindowNotificationKind::CloseRequested && notification.physical_owner
            ? notification.physical_owner
            : context.stack.notification_window(notification.surface,context.input.edit.focus.focused());
        if (!present(window)) return;
        if (notification.kind==WindowNotificationKind::CloseRequested) {
            // OS上の親の終了を、組み込みモーダルや受動的なプレビューの終
            // 了に読み替えない。 既存の終了・拒否・無効化の方針に任せる。
            context.close.request(window,context.dispose);
            return;
        }
        if (notification.kind==WindowNotificationKind::Activated)
            attempt([&] { activate_window(window); });
        else
            attempt([&] { deactivate_window(context.input,window,notification.pointer,[&] { return present(window); }); });
        attempt([&] { if (context.publish_input) context.publish_input(); });
        redraw(window);
    }
    if (error) std::rethrow_exception(error);
}

void suspend_window_input(WindowInputLifecycleContext context,
    std::shared_ptr<Window> window, PointerEvent pointer,
    const std::function<bool()>& live) {
    if (!window || (live && !live())) return;
    const auto revision=context.edit.focus.revision();
    const auto combo_revision=context.edit.combo_popup.revision();
    const auto menu_revision=context.menu.revision();
    const auto hover_revision=context.hover.revision();
    const auto drag_session=context.drag_drop ? context.drag_drop->session_id() : 0;
    // 終了コールバックより先に、すべての識別情報を取得
    // する。 フレーム完了中などにネイティブの世代が消
    // えても、古いモデルの操作状態は終了する必要があ
    // る。 下の各完了処理で取得済みの識別情報を確認
    // し、同じWindowの新しい画面・接触を残す。
    const auto cancel_chrome=context.chrome.prepare_cancel_for(window,true);
    const auto cancel_mouse=context.mouse.prepare_cancel_for(window,pointer.cancel_reason);
    const auto cancel_touch=context.touch.prepare_cancel_for(window,pointer.cancel_reason);
    std::exception_ptr failure;
    const auto attempt=[&](const auto& operation) {
        try { operation(); } catch (...) { if (!failure) failure=std::current_exception(); }
    };
    const auto finish=[&] {
        if (failure) std::rethrow_exception(failure);
    };
    attempt([&] { context.caption.cancel_for(window); });
    // 無効化の通知前に、古い仕切りのプレビューを終える。 コールバックが画面
    // を置き換えたり、別ウィンドウへフォーカスを移したりする場合があるため。
    if (cancel_chrome) attempt(cancel_chrome);
    const auto combo=context.edit.combo_popup.combo();
    if (context.edit.combo_popup.revision()==combo_revision && combo && (contains_widget(window,combo.get()) ||
        (combo==context.edit.inline_edit.choice() &&
         contains_widget(window,context.edit.inline_edit.owner().get()))))
        attempt([&] { context.edit.combo_popup.close(); });
    if (context.menu.revision()==menu_revision)
        attempt([&] { context.menu.cancel_for(window); });
    attempt(cancel_mouse);
    attempt(cancel_touch);
    if (context.drag_drop && context.drag_drop->session_id()==drag_session)
        attempt([&] { context.drag_drop->cancel_for(window); });
    if (context.hover.revision()==hover_revision)
        attempt([&] { context.hover.clear_for(window,pointer); });
    if (context.edit.focus.revision()==revision &&
        contains_widget(window,context.edit.focus.focused().get()))
        attempt([&] { set_edit_focus(context.edit,{}); });
    finish();
}

void deactivate_window(WindowInputLifecycleContext context,
    std::shared_ptr<Window> window, PointerEvent pointer,
    const std::function<bool()>& live) {
    if(!window || (live && !live())) return;
    pointer.cancel_reason = PointerCancelReason::WindowDeactivated;
    suspend_window_input(context,window,pointer,live);
    const auto current=[&] { return !live || live(); };
    if (!current()) return;
    // 通知は最後に行う。 そこで新しく開いたポップアップ・フォ
    // ーカス・捕捉は、終了対象ではなく新しい操作として扱う。
    const auto notify=window->on_deactivated;
    if (notify) notify();
}

void activate_window(std::shared_ptr<Window> window) {
    if (!window || !window->visible()) return;
    const auto notify=window->on_activated;
    if (notify) notify();
}

std::optional<WindowShowPlan> WindowCloseStateMachine::show(WindowInputLifecycleContext context,
    WindowStackStateMachine& stack, const ModalStateMachine& modal,
    std::shared_ptr<Window> window, WindowShowSurface surface) {
    if(!window || !surface.id || disposing(window)) return {};
    const bool activate=window->activate_on_show && !window->overlay_surface &&
        window->enabled() && modal.allows(window);
    const bool visible=!surface.embedded && !window_presentation_hidden(window);
    // ここでは一時的な準備だけを行い、確保を伴う走査・整列がすべて成
    // 功するまで正式なスタックを変えない。 特に stable_partition
    // は項目を動かした後に所有関係の循環検出で例外を出す場
    // 合があるため、 公開済みのスタック上では実行しない。
    auto next_stack=stack;
    next_stack.attach(window,surface.id,surface.bounds,surface.embedded);
    next_stack.raise(window,&modal);
    auto next_returns=focus_returns_;
    std::erase_if(next_returns,[&](const auto& entry) {
        return entry.window.expired() || (!window->visible() && entry.window.lock()==window);
    });
    if(activate) {
        const auto found=std::find_if(next_returns.begin(),next_returns.end(),
            [&](const auto& entry) {return entry.window.lock()==window;});
        if(found==next_returns.end()) {
            const auto owner=window->owner.lock();
            if(owner && owner!=window) {
                const auto focused=context.edit.focus.focused();
                next_returns.push_back({window,owner,
                    window_contains_widget(owner,focused.get()) ? focused : WidgetPtr{}});
            }
        }
    }
    static_assert(std::is_nothrow_move_assignable_v<WindowStackStateMachine>);
    stack=std::move(next_stack);
    focus_returns_.swap(next_returns);
    if(activate) ++activation_revision_;
    window->set_visible(true);
    if(activate && context.caption.owner()!=window) context.caption.cancel();
    return WindowShowPlan{visible,visible && activate};
}

void WindowCloseStateMachine::request(std::shared_ptr<Window> window,
    const std::function<void(const std::shared_ptr<Window>&)>& dispose) {
    if (!window || disposing(window) || !requesting_.insert(window.get()).second) return;
    struct Reset {
        std::unordered_set<const Window*>& requests;
        const Window* window;
        ~Reset() { requests.erase(window); }
    } reset{requesting_,window.get()};
    const auto notify=window->on_close_requested;
    if (notify) notify();
    else if (dispose) dispose(window);
}

void WindowCloseStateMachine::request_all(WindowStackStateMachine& stack,
    const std::function<void(const std::shared_ptr<Window>&)>& dispose) {
    if(requesting_all_) return;
    requesting_all_=true;
    struct Reset { bool& value; ~Reset() {value=false;} } reset{requesting_all_};
    const auto windows=stack.windows();
    std::unordered_map<const Window*,std::size_t> original;
    for(std::size_t i=0;i<windows.size();++i) original.emplace(windows[i].get(),i);
    std::unordered_set<const Window*> representatives;
    for(const auto& window:windows) {
        auto root=window;
        std::vector<std::shared_ptr<Window>> path;
        std::unordered_map<const Window*,std::size_t> visited;
        for(auto current=window;current;current=current->owner.lock()) {
            const auto [found,inserted]=visited.emplace(current.get(),path.size());
            if(!inserted) {
                // 不正な所有関係の循環にはルートがないため、元の
                // 一要素を一定の規則で選ぶ。 各要素・子孫へ独立
                // に終了を求め、その要素の終了拒否を迂回しない。
                auto first=windows.size();
                for(auto i=found->second;i<path.size();++i) {
                    const auto member=original.find(path[i].get());
                    if(member!=original.end() && member->second<first) {
                        first=member->second;root=path[i];
                    }
                }
                break;
            }
            path.push_back(current);
            if(original.contains(current.get())) root=current;
        }
        representatives.insert(root.get());
    }
    for(auto it=windows.rbegin();it!=windows.rend();++it) {
        if(!representatives.contains(it->get())) continue;
        const auto current=stack.windows();
        if(std::find(current.begin(),current.end(),*it)!=current.end()) request(*it,dispose);
    }
}

bool WindowCloseStateMachine::disposing(std::shared_ptr<Window> window) const {
    std::unordered_set<const Window*> visited;
    for (; window && visited.insert(window.get()).second; window=window->owner.lock()) {
        if(disposing_.contains(window.get()) || std::any_of(pending_retirements_.begin(),
            pending_retirements_.end(),[&](const auto& pending) {return pending.window==window;})) return true;
    }
    return false;
}

bool WindowCloseStateMachine::active_disposing(std::shared_ptr<Window> window) const {
    std::unordered_set<const Window*> visited;
    for (; window && visited.insert(window.get()).second; window=window->owner.lock())
        if (disposing_.contains(window.get())) return true;
    return false;
}

void WindowCloseStateMachine::dispose(WindowInputLifecycleContext context,
    WindowStackStateMachine& stack, std::shared_ptr<Window> window,
    std::function<void(const std::shared_ptr<Window>&)> release,std::function<void()> publish) {
    retire(context,stack,std::move(window),std::move(release),true,std::move(publish));
}

void WindowCloseStateMachine::hide(WindowInputLifecycleContext context,
    WindowStackStateMachine& stack, std::shared_ptr<Window> window,
    std::function<void(const std::shared_ptr<Window>&)> realize,std::function<void()> publish) {
    retire(context,stack,std::move(window),std::move(realize),false,std::move(publish));
}

void WindowCloseStateMachine::retire(WindowInputLifecycleContext context,
    WindowStackStateMachine& stack, std::shared_ptr<Window> window,
    std::function<void(const std::shared_ptr<Window>&)> release, bool remove,
    std::function<void()> publish, bool from_queue,
    std::shared_ptr<RetirementFocus> inherited_focus) {
    if (!window || active_disposing(window)) return;
    // コールバックをまたいで現在のスタックを走査しない。 常に最前面のプ
    // レビューなどがあるため、子孫が重なり順で隣接しているとは限らない。
    const auto ordered=stack.close_order(window);
    const auto pending=std::find_if(pending_retirements_.begin(),pending_retirements_.end(),
        [&](const auto& item) {return item.window==window;});
    const bool overlaps=std::any_of(ordered.begin(),ordered.end(),
        [&](const auto& item) {return disposing_.contains(item.get());});
    if(!from_queue && (pending!=pending_retirements_.end() || overlaps || disposing(window))) {
        // 明示的な終了要求は、保留中の非表示を終了へ強める。 祖
        // 先が先に非表示予定だっただけで、破棄要求を失わない。
        if(pending!=pending_retirements_.end() && (pending->remove || !remove)) return;
        auto inherited=pending!=pending_retirements_.end() ? pending->focus : nullptr;
        if(pending==pending_retirements_.end()) {
            for(auto it=retirement_focus_.rbegin();it!=retirement_focus_.rend();++it) {
                const auto& token=*it;
                if(token->restore && token->expected_revision==context.edit.focus.revision() &&
                   token->activation_revision==activation_revision_ &&
                   std::find(ordered.begin(),ordered.end(),token->root.lock())!=ordered.end()) {
                    inherited=token;break;
                }
            }
        }
        const bool registered=stack.owner_of(window)==window;
        auto perform=[this,context,&stack,window,registered,inherited,release=std::move(release),
                      publish=std::move(publish)](bool erase) mutable {
            // 先に処理済みの祖先が、このモデルを破棄している
            // 場合がある。 ネイティブ資源を二度解放しない。
            if(registered && stack.owner_of(window)!=window) {
                if(publish) publish();
                return;
            }
            retire(context,stack,window,std::move(release),erase,std::move(publish),true,inherited);
        };
        if(pending!=pending_retirements_.end()) {
            pending->remove=remove;pending->perform=std::move(perform);
        } else pending_retirements_.push_back({window,remove,std::move(perform),std::move(inherited)});
        // 追加の入力・表示は直ちに拒否するが、ネイティブの解放は遅延する。
        for(const auto& item:ordered) item->set_visible(false);
        return;
    }
    const auto closing=[&](const std::shared_ptr<Window>& candidate) {
        return candidate && std::find(ordered.begin(),ordered.end(),candidate)!=ordered.end();
    };
    const auto bookmark=std::find_if(focus_returns_.begin(),focus_returns_.end(),
        [&](const auto& entry) {return entry.window.lock()==window;});
    const auto return_to=bookmark!=focus_returns_.end() ? *bookmark : FocusReturn{};
    const bool modal_owns_return=context.modal && std::any_of(
        context.modal->sessions().begin(),context.modal->sessions().end(),
        [&](const auto& session) {return closing(session.dialog);});
    const auto original_focus_revision=context.edit.focus.revision();
    bool inherited_return=false;
    if(inherited_focus && inherited_focus->restore && !context.edit.focus.focused() &&
       inherited_focus->expected_revision==original_focus_revision &&
       inherited_focus->activation_revision==activation_revision_) {
        // 子はすでにスタックを離れている場合がある。 まだ有効な
        // 所有者の経路で確認し、親変更や不正な循環を拒否する。
        std::unordered_set<const Window*> visited;
        for(auto root=inherited_focus->root.lock();root && visited.insert(root.get()).second;
            root=root->owner.lock()) if(root==window) {inherited_return=true;break;}
    }
    auto focus_token=std::make_shared<RetirementFocus>(RetirementFocus{window,
        original_focus_revision,activation_revision_,!modal_owns_return &&
        (inherited_return || closing(stack.owner_of(context.edit.focus.focused())))});
    retirement_focus_.push_back(focus_token);
    struct FocusReset {
        std::vector<std::shared_ptr<RetirementFocus>>& tokens;
        std::shared_ptr<RetirementFocus> token;
        void release() {std::erase(tokens,token);}
        ~FocusReset() {release();}
    } focus_reset{retirement_focus_,focus_token};
    std::erase_if(focus_returns_,[&](const auto& entry) {
        return entry.window.expired() || closing(entry.window.lock());
    });
    struct Reset {
        std::unordered_set<const Window*>& closing;
        std::vector<const Window*> inserted;
        void release() {for(const auto* w:inserted) closing.erase(w);inserted.clear();}
        ~Reset() {release();}
    } reset{disposing_,{}};
    for (const auto& w:ordered) {
        // 再入して祖先を閉じる処理で、まだ戻っていない外側
        // の子孫の終了処理が所有するガードを解放しない。
        if(disposing_.insert(w.get()).second) reset.inserted.push_back(w.get());
        w->set_visible(false);
    }
    std::exception_ptr error;
    const auto cleanup=[&](auto&& action) {
        try { action(); } catch(...) { if(!error) error=std::current_exception(); }
    };
    for (const auto& w:ordered) {
        const auto belongs=[&](const WidgetPtr& widget) { return contains_widget(w,widget.get()); };
        const auto combo=context.edit.combo_popup.combo();
        if (belongs(combo) || (combo && combo==context.edit.inline_edit.choice() &&
            belongs(context.edit.inline_edit.owner()))) cleanup([&] {context.edit.combo_popup.close();});
        if (belongs(context.edit.inline_edit.owner())) cleanup([&] {context.edit.inline_edit.cancel();});
        if (belongs(context.edit.number_edit.owner())) cleanup([&] {context.edit.number_edit.discard();});
        if (belongs(context.edit.combo_edit.owner())) cleanup([&] {context.edit.combo_edit.finish();});
        cleanup([&] {context.caption.cancel_for(w);});
        cleanup([&] {context.chrome.cancel_for(w);});
        cleanup([&] {context.menu.cancel_for(w);});
        cleanup([&] {context.mouse.cancel_for(w);});
        cleanup([&] {context.touch.cancel_for(w);});
        if (context.drag_drop) cleanup([&] {context.drag_drop->cancel_for(w);});
        // この部分ツリーは上で非表示にしてある。残っているホバー
        // に対応するLeaveを一度送り、 隠れたコントロールに
        // 通常入力を許可せず、重ね表示を閉じられるようにする。
        cleanup([&] {context.hover.clear_for_retirement(w,{});});
        if (belongs(context.edit.focus.focused())) {
            // 期待するトークンを進めるのは、この処理自身の解除だけ。 フォーカス喪
            // 失・キャンセルのコールバックによる解除・再フォーカス・新しい表示を優
            // 先し、 最終的なフォーカスポインターが再びnullでも上書きしない。
            if(context.edit.focus.revision()==focus_token->expected_revision) ++focus_token->expected_revision;
            cleanup([&] {context.edit.focus.clear();});
        }
        // 解放コールバックはネイティブの処理だけを行う。 論理的な所有
        // 関係とフォーカス・編集のキャンセルは、単独版・ALLで同じに
        // する。 アプリのコールバックが失敗しても所有者の残りの部分ツ
        // リーを放置せず、 全終了処理の後に最初のエラーを報告する。
        if (release) cleanup([&] {release(w);});
        if (remove) stack.remove(w);
    }
    if(focus_token->restore && !context.edit.focus.focused() &&
       context.edit.focus.revision()==focus_token->expected_revision &&
       focus_token->activation_revision==activation_revision_) {
        const auto owner=return_to.owner.lock();
        const auto registered=stack.windows();
        if(owner && window->owner.lock()==owner && !disposing(owner) &&
           owner->visible() && owner->enabled() && !owner->overlay_surface &&
           std::find(registered.begin(),registered.end(),owner)!=registered.end() &&
           (!context.modal || context.modal->allows(owner))) {
            // 既存の走査規則で、除去された部品・非選択タブを
            // 扱う。 下のネイティブの有効化は、共通の論理的
            // なフォーカス復元がまだ最新の場合だけ許可する。
            cleanup([&] {set_edit_focus(context.edit,
                focus_target_in_scope(owner,return_to.target.lock(),true),owner);});
            // ネイティブの子の破棄後は、論理フォーカスだけでは
            // 前面の HWND が決まらず、 Windowsが
            // 背後の無関係なアプリを有効化する場合がある。 復
            // 元が変更されず完了した場合だけ前面に出し、 フォ
            // ーカス通知が開いた・選んだ別の対象を優先する。
            if(context.activate_native &&
               focus_token->activation_revision==activation_revision_ &&
               stack.owner_of(context.edit.focus.focused())==owner)
                cleanup([&] {context.activate_native(owner);});
        }
    }
    const auto restored_owner=stack.owner_of(context.edit.focus.focused());
    if(publish) cleanup([&] {publish();});
    if(context.redraw && context.edit.focus.revision()!=original_focus_revision) {
        const auto actual_owner=stack.owner_of(context.edit.focus.focused());
        for(const auto& owner:{restored_owner,actual_owner==restored_owner ? nullptr : actual_owner}) {
            if(owner && owner->visible() && !disposing(owner) && stack.owner_of(owner)==owner)
                cleanup([&] {context.redraw(owner);});
        }
    }
    reset.release();
    focus_reset.release();
    if(disposing_.empty() && !draining_retirements_) {
        draining_retirements_=true;
        struct DrainReset {bool& draining;~DrainReset() {draining=false;}} drain_reset{draining_retirements_};
        while(!pending_retirements_.empty()) {
            auto deferred=std::move(pending_retirements_.front());
            pending_retirements_.erase(pending_retirements_.begin());
            cleanup([&] {deferred.perform(deferred.remove);});
        }
    }
    if(error) std::rethrow_exception(error);
}

} // namespace cwxeditor::lib
