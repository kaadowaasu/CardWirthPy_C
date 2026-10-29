#include "ui_accessibility.hpp"
#include "ui_actions.hpp"
#include "ui_combo_edit.hpp"
#include "ui_number_edit.hpp"
#include <algorithm>
#include <cmath>

namespace cwxeditor::lib {
namespace {
std::optional<AccessibilityRange> numeric_range(double value,double minimum,double maximum,double step) {
    if (!std::isfinite(value) || !std::isfinite(minimum) || !std::isfinite(maximum) || minimum>maximum)
        return {};
    return AccessibilityRange{std::clamp(value,minimum,maximum),minimum,maximum,
        std::isfinite(step) ? std::max(0.0,step) : 0.0};
}
template<class Index>
void append_unique(std::vector<Index>& values,Index value) {
    if (std::find(values.begin(),values.end(),value)==values.end()) values.push_back(value);
}
}

void AccessibilityActionDispatcher::bind(Handler handler) {
    // 引数の構築は成功済みか、失敗しても通知処理を変更
    // していない。 交換で新しいハンドラーを一度に公開
    // し、 publish() が明示的に再有効化す
    // るまで、古いネイティブ画面をすべて無効とする。
    handler_.swap(handler);
    active_surfaces_.clear();
}

bool AccessibilityActionDispatcher::activate(const std::uint64_t surface) noexcept {
    if (!surface || !handler_) return false;
    try {
        active_surfaces_.insert(surface);
        return true;
    } catch (...) {
        // ネイティブの連携処理がこの画面トークンを保持できなければ、安
        // 全のため拒否する。 特に、設定途中のコールバックを残さない。
        return false;
    }
}

void AccessibilityActionDispatcher::retire(const std::uint64_t surface) noexcept {
    if (surface) active_surfaces_.erase(surface);
}

void AccessibilityActionDispatcher::invalidate() noexcept {
    active_surfaces_.clear();
    handler_ = {};
}

bool AccessibilityActionDispatcher::dispatch(
    const std::uint64_t surface, const AccessibilityActionRequest& request) const noexcept {
    Handler handler;
    try {
        if (!surface || !active_surfaces_.contains(surface) || !handler_) return false;
        // 今回の呼び出し用にコールバックを所有する。 操作後の状
        // 態反映で、通知処理を無効化・再接続する場合があるため。
        handler = handler_;
    } catch (...) {
        return false;
    }
    try {
        return handler(surface, request);
    } catch (...) {
        // ネイティブの有効化・押下からC++例外を安全に外へ出す
        // ことはできない。 ホストの失敗は未処理の操作として扱
        // い、別のOS固有の選択実装へ切り替える理由にしない。
        return false;
    }
}

void PlatformAccessibilityBackend::set_host_action_dispatcher(
    std::weak_ptr<AccessibilityActionDispatcher> dispatcher) noexcept {
    host_action_dispatcher_ = std::move(dispatcher);
}

bool PlatformAccessibilityBackend::dispatch_host_action(
    const std::uint64_t surface, const AccessibilityActionRequest& request) const noexcept {
    const auto dispatcher = host_action_dispatcher_.lock();
    return dispatcher && dispatcher->dispatch(surface, request);
}

bool PlatformAccessibilityBackend::activate_host_action_surface(
    const std::uint64_t surface) const noexcept {
    const auto dispatcher = host_action_dispatcher_.lock();
    return dispatcher && dispatcher->activate(surface);
}

void PlatformAccessibilityBackend::retire_host_action_surface(
    const std::uint64_t surface) const noexcept {
    if (const auto dispatcher = host_action_dispatcher_.lock()) dispatcher->retire(surface);
}

void retire_inactive_accessibility_surfaces(
    PlatformAccessibilityBackend& backend, const std::span<const std::uint64_t> surfaces,
    const std::optional<std::uint64_t> active_surface) noexcept {
    // 埋め込みの子モデルは、一つのネイティブSDLウィンドウを共有でき
    // る。 フォーカス遷移の同期で、同じ実体の要素を二度切り離さない。
    for (std::size_t index = 0; index < surfaces.size(); ++index) {
        const auto surface = surfaces[index];
        if (!surface || (active_surface && surface == *active_surface)) continue;
        if (std::find(surfaces.begin(), surfaces.begin() + index, surface) !=
            surfaces.begin() + index) continue;
        backend.retire(surface);
    }
}

bool dispatch_accessibility_action(const WidgetPtr& root,
                                   const AccessibilityActionRequest& request) {
    const auto& target = request.target;
    if (!root || !target || !widget_in_input_scope(root, target)) return false;
    switch (request.action) {
    case AccessibilityAction::Invoke:
        if (const auto button = std::dynamic_pointer_cast<Button>(target))
            return activate_button_control(root, button);
        if (const auto link = std::dynamic_pointer_cast<Link>(target))
            return activate_link(*link);
        if (const auto list = std::dynamic_pointer_cast<ListView>(target))
            return request.index && activate_list_item(*list, *request.index);
        if (const auto table = std::dynamic_pointer_cast<TableView>(target))
            return request.index && activate_table_cell(*table, *request.index, 0U);
        if (const auto tree = std::dynamic_pointer_cast<TreeView>(target))
            return request.tree_item && activate_tree_item(*tree, *request.tree_item);
        return false;
    case AccessibilityAction::Select:
        // 通常の選択解除とは異なり、アクセシビリティの Select
        // 要求は、存在する項目を一つ指定する必要がある。 古いネイ
        // ティブの位置・IDを、汎用処理で空の選択へ正規化しない。
        if (const auto combo = std::dynamic_pointer_cast<ComboBox>(target))
            return request.index && *request.index < combo->items.size() &&
                   combo->items[*request.index].enabled &&
                   select_combo_item(*combo, *request.index);
        if (const auto list = std::dynamic_pointer_cast<ListView>(target))
            return request.index && *request.index < list->items.size() &&
                   list->items[*request.index].enabled &&
                   select_list_rows(*list, {*request.index});
        if (const auto table = std::dynamic_pointer_cast<TableView>(target))
            return request.index && *request.index < table->rows.size() &&
                   select_table_rows(*table, {*request.index});
        if (const auto tree = std::dynamic_pointer_cast<TreeView>(target))
            return request.tree_item && std::ranges::any_of(tree->nodes,
                [&](const TreeNode& node) { return node.id == *request.tree_item; }) &&
                   select_tree_items(*tree, {*request.tree_item});
        if (const auto tabs = std::dynamic_pointer_cast<TabView>(target))
            return request.index && *request.index < tabs->pages.size() &&
                   select_tab_page(*tabs, *request.index);
        return false;
    case AccessibilityAction::Toggle:
        if (const auto radio = std::dynamic_pointer_cast<RadioButton>(target))
            return select_radio_button(*radio, radio_button_group(root, *radio));
        if (const auto toggle = std::dynamic_pointer_cast<ToggleButton>(target))
            return set_toggle_checked(*toggle, !toggle->checked);
        return false;
    case AccessibilityAction::SetValue:
        if (const auto edit = std::dynamic_pointer_cast<TextEdit>(target))
            return request.text && !edit->read_only && set_text_value(*edit, *request.text);
        if (const auto number = std::dynamic_pointer_cast<NumberEdit>(target))
            return request.number && !number->read_only && set_number_value(*number, *request.number);
        if (const auto slider = std::dynamic_pointer_cast<Slider>(target))
            return request.number && set_slider_value(*slider, *request.number);
        if (const auto combo = std::dynamic_pointer_cast<ComboBox>(target)) {
            if (request.index) return select_combo_item(*combo, *request.index);
            return request.text && combo->editable && set_combo_edit_text(*combo, *request.text);
        }
        return false;
    }
    return false;
}

AccessibilityPresentation prepare_widget_accessibility(const WidgetPtr& widget,const WidgetPtr& focused) {
    if (!widget || !widget->visible()) return {};
    AccessibilityPresentation result{widget->accessibility_name,widget->accessibility_description};
    result.kind=widget->kind(); result.visible=true; result.enabled=widget->enabled();
    result.focused=widget==focused;
    if (const auto toggle=std::dynamic_pointer_cast<ToggleButton>(widget)) {
        result.checked=toggle->checked;
        if (const auto check=std::dynamic_pointer_cast<CheckBox>(widget)) result.indeterminate=check->indeterminate;
    }
    if (const auto edit=std::dynamic_pointer_cast<TextEdit>(widget)) {
        result.read_only=edit->read_only; result.password=edit->password; result.multiline=edit->multiline;
        if (!edit->password) {
            result.value=edit->text;
            const auto start=clamp_utf8_boundary(edit->text,edit->selection_start);
            const auto end=clamp_utf8_boundary(edit->text,start+std::min(edit->selection_length,edit->text.size()-start));
            result.text_selection=AccessibilityTextSelection{start,end-start};
        }
    } else if (const auto number=std::dynamic_pointer_cast<NumberEdit>(widget)) {
        result.read_only=number->read_only;
        result.range=numeric_range(number->value,number->minimum,number->maximum,number->step);
        if (result.range) result.value=format_number_value(*number);
    } else if (const auto slider=std::dynamic_pointer_cast<Slider>(widget)) {
        result.range=numeric_range(slider->value,slider->minimum,slider->maximum,slider->step);
        result.orientation=slider->orientation;
    } else if (const auto progress=std::dynamic_pointer_cast<ProgressBar>(widget)) {
        result.read_only=true; result.indeterminate=progress->indeterminate;
        if (!progress->indeterminate) result.range=numeric_range(progress->value,progress->minimum,progress->maximum,0);
    } else if (const auto combo=std::dynamic_pointer_cast<ComboBox>(widget)) {
        result.read_only=!combo->editable; result.value=combo_display_text(*combo); result.item_count=combo->items.size();
        if (combo->selected_index && *combo->selected_index<combo->items.size())
            result.selected_indices.push_back(*combo->selected_index);
    } else if (const auto tree=std::dynamic_pointer_cast<TreeView>(widget)) {
        result.multi_select=tree->multi_select; result.item_count=tree->nodes.size();
        for (auto id:tree->selection)
            if (std::any_of(tree->nodes.begin(),tree->nodes.end(),[&](const auto& node) {return node.id==id;}))
                append_unique(result.selected_tree_items,id);
    } else if (const auto table=std::dynamic_pointer_cast<TableView>(widget)) {
        result.multi_select=table->multi_select; result.item_count=table->rows.size(); result.column_count=table->columns.size();
        for (auto index:table->selection) if (index<table->rows.size()) append_unique(result.selected_indices,index);
    } else if (const auto list=std::dynamic_pointer_cast<ItemView>(widget)) {
        result.multi_select=list->multi_select; result.item_count=list->items.size();
        for (auto index:list->selection) if (index<list->items.size()) append_unique(result.selected_indices,index);
    } else if (const auto tabs=std::dynamic_pointer_cast<TabView>(widget)) {
        result.item_count=tabs->pages.size();
        if (tabs->selected_index && *tabs->selected_index<tabs->pages.size()) {
            result.selected_indices.push_back(*tabs->selected_index);
            result.value=tabs->pages[*tabs->selected_index].title;
        }
    } else if (const auto tools=std::dynamic_pointer_cast<ToolBar>(widget)) result.item_count=tools->items.size();
    else if (const auto menu=std::dynamic_pointer_cast<Menu>(widget)) result.item_count=menu->items.size();
    else if (const auto expand=std::dynamic_pointer_cast<ExpandBar>(widget)) result.item_count=expand->children().size();
    if (!result.name.empty()) return result;
    if (const auto edit=std::dynamic_pointer_cast<TextEdit>(widget); edit && edit->password)
        return result;
    if (const auto window=std::dynamic_pointer_cast<Window>(widget)) result.name=window->title;
    else if (const auto group=std::dynamic_pointer_cast<GroupBox>(widget)) result.name=group->title;
    else if (const auto text=std::dynamic_pointer_cast<TextWidget>(widget)) result.name=text->text;
    else if (const auto combo=std::dynamic_pointer_cast<ComboBox>(widget)) result.name=combo_display_text(*combo);
    else if (const auto number=std::dynamic_pointer_cast<NumberEdit>(widget)) result.name=format_number_value(*number);
    else if (const auto tabs=std::dynamic_pointer_cast<TabView>(widget)) {
        if (tabs->selected_index && *tabs->selected_index<tabs->pages.size())
            result.name=tabs->pages[*tabs->selected_index].title;
    } else if (const auto tree=std::dynamic_pointer_cast<TreeView>(widget)) {
        for (const auto id:tree->selection)
            for (const auto& node:tree->nodes) if (node.id==id) {
                if (!result.name.empty()) result.name+='\n';
                result.name+=node.text;
                break;
            }
    } else if (const auto table=std::dynamic_pointer_cast<TableView>(widget)) {
        for (const auto index:table->selection) if (index<table->rows.size()) {
            std::string row;
            for (const auto& cell:table->rows[index].cells) {
                if (!row.empty()) row+='\t';
                row+=cell.text;
            }
            if (!result.name.empty()) result.name+='\n';
            result.name+=row;
        }
    } else if (const auto list=std::dynamic_pointer_cast<ItemView>(widget)) {
        for (const auto index:list->selection) if (index<list->items.size()) {
            if (!result.name.empty()) result.name+='\n';
            result.name+=list->items[index].text;
        }
    }
    return result;
}
bool AccessibilityAnnouncementStateMachine::update(const WidgetPtr& widget,
    const AccessibilityPresentation& presentation) {
    if (!widget || (presentation.name.empty() && presentation.description.empty())) {
        owner_.reset(); previous_={}; return false;
    }
    if (owner_.lock()==widget && previous_==presentation) return false;
    owner_=widget; previous_=presentation; return true;
}
} // namespace cwxeditor::lib
