#include "cwxeditor_src/gui/cwx/editor/gui/dwt/flagtable.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/customtext.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/messageutils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dskin.hpp"
// UIの実装は一つに統一し、
// 別経路の実装を設けない。
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/core/cwx/versioninfo.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dutils.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"
#include "cwxeditor_src/lib/threading.hpp"
#include "cwxeditor_src/lib/ui_window_lifecycle.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <numeric>
#include <span>
#include <string>
#include <type_traits>
#include <utility>

namespace cwxeditor::cwx::editor::gui::dwt::flagtable {

namespace {

// D版と同様、フラグとステップのダイアログで特殊文字メニューとプレビューの規則を共有する。
messageutils::Skin::Ptr variablePreviewSkin(const dprops::Props& prop,
                                           const Summary* summary) {
    if (summary && !summary->type().empty())
        if (auto skin = dskin::findSkin2(prop, summary->type(), summary->skinName()))
            return skin;
    return dskin::findSkin2(prop, prop.var().defaultSkin.value,
                           prop.var().defaultSkinName.value);
}


void showVariableWarnings(const std::shared_ptr<lib::Label>& label,
                          const std::vector<String>& warnings) {
    if (!label) return;
    label->text.clear();
    for (const auto& warning : warnings) {
        if (!label->text.empty()) label->text += "\n";
        label->text += warning;
    }
    label->set_visible(!warnings.empty());
}

std::vector<String> variableWarnings(const Commons& comm, const Summary* summary,
    const FlagDirPtr& dir, StringView name, const std::vector<String>& values,
    bool step, bool local, bool expand, types::VariableInitialization initialization,
    std::size_t step_max) {
    std::vector<String> result;
    auto* prop = comm.editorProps ? comm.editorProps() : nullptr;
    if (!prop) return result;
    const auto& msgs = prop->requiredMsgs();
    const auto append = [&](const auto& warnings) {
        result.insert(result.end(), warnings.begin(), warnings.end());
    };
    if (prop->sys().isSystemVar(name))
        result.push_back(utils::tryFormat(msgs.warningSystemVarName.value,
                                         prop->sys().prefixSystemVarName()));
    if (step && summary && summary->legacy() && values.size() != step_max)
        result.push_back(utils::tryFormat(msgs.warningStepCount.value, step_max));
    if (expand && !prop->isTargetVersion(summary, "2"))
        result.push_back(msgs.warningExpandSPChars.value);
    if (expand) {
        auto skin = variablePreviewSkin(*prop, summary);
        if (!skin) skin = std::make_shared<messageutils::Skin>(prop->parent(), "");
        const auto counter = dir ? dir->useCounter() : comm.useCounter;
        std::vector<String> seen;
        for (const auto& value : values) {
            std::vector<String> flags, steps, variants, fonts;
            std::vector<char> colors;
            msgutils::textUseItems(dutils::wrapReturnCode(value), flags, steps, variants, fonts, colors);
            fonts.clear(); colors.clear();
            lib::Dictionary<String,bool> wf, ws, wv, wfont;
            lib::Dictionary<char,bool> wc;
            const auto warnings = warning::textWarnings(prop->parent(), *skin, summary,
                counter.get(), summary && summary->legacy(),
                summary ? summary->dataVersion() : versioninfo::LATEST_VERSION,
                prop->var().targetVersion.value, value, flags, steps, variants,
                fonts, colors, wf, ws, wv, wfont, wc);
            for (const auto& item : warnings.all)
                if (std::find(seen.begin(), seen.end(), item) == seen.end()) {
                    seen.push_back(item); result.push_back(item);
                }
        }
    }
    if (!local && !types::isDefaultVariableInitialization(initialization) &&
        !prop->isTargetVersion(summary, "4"))
        result.push_back(msgs.warningVariableInitialization.value);
    append(warning::sjisWarnings(prop->parent(), summary, name,
        step ? msgs.dlgLblStepName.value : msgs.dlgLblFlagName.value));
    for (std::size_t i=0; i<values.size(); ++i)
        append(warning::sjisWarnings(prop->parent(), summary, values[i],
            step ? msgs.stepValueForWarning.value : i == 0 ? msgs.flagOnValue.value : msgs.flagOffValue.value));
    return result;
}

void guardVariableMenu(std::vector<lib::MenuItem>& items, const std::function<bool()>& live) {
    for (auto& item : items) {
        if (item.action) {
            const auto action = item.action;
            item.action = [live, action] { if (live()) action(); };
        }
        if (item.open_children) {
            const auto open = item.open_children;
            item.open_children = [live, open] {
                if (!live()) return std::vector<lib::MenuItem>{};
                auto children = open();
                guardVariableMenu(children, live);
                return children;
            };
        }
        guardVariableMenu(item.children, live);
    }
}

dmenu::TextMenuModifyPtr attachVariableSpecialMenu(
    const Commons& comm, const std::shared_ptr<Summary>& summary,
    const UseCounterPtr& counter, dmenu::TMM target,
    std::weak_ptr<int> life, std::function<bool()> expanded) {
    dmenu::Commons menu_comm;
    menu_comm.services = comm.services;
    menu_comm.presentation.menuText = comm.menuText;
    menu_comm.presentation.menuImage = comm.menuImage;
    if (auto* props = comm.editorProps ? comm.editorProps() : nullptr) {
        menu_comm.presentation.textTabs = static_cast<std::size_t>(
            std::max<std::int32_t>(0, props->var().textTabs.value));
        menu_comm.undoMaxEtc = static_cast<std::size_t>(
            std::max<std::int32_t>(0, props->var().undoMaxEtc.value));
    }
    menu_comm.undoLimit = [provider = comm.editorProps, life] {
        auto* props = !life.expired() && provider ? provider() : nullptr;
        return props ? static_cast<std::size_t>(std::max<std::int32_t>(0,
            props->var().undoMaxEtc.value)) : std::size_t{};
    };
    menu_comm.subscribeUndoMax = [signal = comm.refUndoMax](lib::Action handler) mutable {
        const auto token = signal.connect(std::move(handler));
        return lib::Action{[signal, token]() mutable { signal.remove(token); }};
    };
    lib::WidgetPtr widget = target.textWidget();
    if (!widget) widget = target.comboWidget();
    auto owner = dmenu::textMenuOf(widget);
    if (!owner) owner = dmenu::createTextMenu(menu_comm, std::move(target));
    if (!owner) return owner;
    lib::MenuItem item;
    item.text = comm.menuText ? comm.menuText(types::MenuID::PutSPChar)
                              : String{"特殊文字の挿入"};
    item.command = "put-special";
    const auto live = [life, provider = comm.editorProps] {
        return !life.expired() && provider && provider() != nullptr;
    };
    const auto insert = [weak = std::weak_ptr<dmenu::TextMenuModify>(owner), live](StringView text) {
        if (!live()) return;
        const auto menu = weak.lock();
        if (!menu || menu->text().readOnly()) return;
        auto target = menu->text();
        auto content = target.getText();
        const auto selection = target.getSelection();
        const auto start = static_cast<std::size_t>(std::clamp<float>(
            std::min(selection.x, selection.y), 0.0F, static_cast<float>(content.size())));
        const auto end = static_cast<std::size_t>(std::clamp<float>(
            std::max(selection.x, selection.y), static_cast<float>(start), static_cast<float>(content.size())));
        content.replace(start, end - start, text);
        target.setText(content);
        target.setSelection({static_cast<float>(start + text.size()),
                             static_cast<float>(start + text.size())});
    };
    item.open_children = [comm, summary, counter, live, expanded, insert] {
        if (!live()) return std::vector<lib::MenuItem>{};
        messageutils::MessageContext context;
        context.services = comm.services;
        context.props = comm.editorProps();
        context.summary = summary.get();
        context.summary_owner = summary;
        const auto skin = [provider = comm.editorProps, summary, live] {
            return live() ? variablePreviewSkin(*provider(), summary.get())
                          : messageutils::Skin::Ptr{};
        };
        auto items = messageutils::setupSPCharsMenu(context, summary.get(), skin,
            counter.get(), {}, false, true, [live, expanded] {
                return live() && expanded();
            }, insert)->items;
        guardVariableMenu(items, live);
        return items;
    };
    owner->menu()->items.push_back(std::move(item));
    const auto previous = owner->menu()->on_open;
    owner->menu()->on_open = [weak = std::weak_ptr<dmenu::TextMenuModify>(owner),
                              previous, live, expanded] {
        if (previous) previous();
        if (auto menu = weak.lock()) for (auto& entry : menu->menu()->items)
            if (entry.command == "put-special") entry.enabled = live() && expanded();
    };
    return owner;
}

template <typename Component>
[[nodiscard]] std::shared_ptr<Component> makeComponent(const Commons& comm,
                                                        lib::ComponentKind kind) {
    if (comm.services.gui) {
        if (auto value = std::dynamic_pointer_cast<Component>(
                comm.services.gui->create_component(kind))) {
            return value;
        }
    }
    return std::make_shared<Component>();
}

[[nodiscard]] std::shared_ptr<Summary> borrowSummary(Summary* value) {
    if (!value) return {};
    return std::shared_ptr<Summary>(value, [](Summary*) {});
}

// D版の数値編集は、数字の前後どちらの小数点も受け付ける。 式言語
// の構文解析は厳密なままにし、このGUI入力欄だけで正規化する。
[[nodiscard]] expression::VariantVal numericEditorValue(StringView input) {
    String value(input);
    const auto first = !value.empty() && (value.front() == '-' || value.front() == '+') ? 1U : 0U;
    if (std::none_of(value.begin(), value.end(), [](unsigned char c) { return c >= '0' && c <= '9'; }))
        return expression::VariantVal::invalidValue();
    if (first < value.size() && value[first] == '.') value.insert(first, "0");
    if (!value.empty() && value.back() == '.') value += '0';
    return expression::variantValueFromText(value);
}

[[nodiscard]] String initializationText(
    const types::VariableInitialization value, const flagspane::Messages& messages = {}) {
    switch (value) {
    case types::VariableInitialization::BattleStart: return messages.variableInitializationNameBattleStart;
    case types::VariableInitialization::BattleEnd: return messages.variableInitializationNameBattleEnd;
    case types::VariableInitialization::EventExit: return messages.variableInitializationNameEventExit;
    case types::VariableInitialization::Leave: return messages.variableInitializationNameLeave;
    case types::VariableInitialization::None: return messages.variableInitializationNameNone;
    default: return types::fromVariableInitialization(value);
    }
}

template <typename T>
void place(const std::shared_ptr<T>& control, std::size_t row,
           std::size_t column, std::size_t row_span = 1,
           std::size_t column_span = 1, float weight = 0.0F,
           lib::Size preferred = {}, lib::Align vertical = lib::Align::Center) {
    if (!control) return;
    control->set_layout_item(lib::LayoutItem{
        row, column, row_span, column_span, lib::Align::Stretch,
        vertical, {}, preferred, {}, weight});
}

[[nodiscard]] std::shared_ptr<lib::Panel> makeInitializationPanel(
    const Commons& comm, const Parent& parent,
    bool read_only, std::vector<CheckBoxPtr>& checks, CheckBoxPtr& none,
    types::VariableInitialization& value, const Props* props = nullptr,
    lib::Action modified = {}) {
    auto group = makeComponent<lib::Panel>(comm, lib::ComponentKind::Panel);
    auto layout = std::make_shared<lib::GridLayout>();
    layout->columns = 3;
    layout->spacing = 4.0F;
    group->set_layout(layout);

    auto label = makeComponent<lib::Label>(comm, lib::ComponentKind::Label);
    label->text = props ? props->msgs.variableInitialization
                        : String{"初期化タイミング"};
    place(label, 0, 0, 1, 3);
    group->add(label);

    const auto changed = [&checks, &none, &value,
                          modified](const CheckBoxPtr& source) {
        normalizeInitializationSelection(checks, none, source);
        value = selectedInitialization(checks, none);
        if (modified) modified();
    };
    for (std::size_t index = 0; index < INIT_TIMINGS.size(); ++index) {
        auto check = createInitTimingCheck(comm, {}, INIT_TIMINGS[index],
                                           read_only, changed);
        if (props) check->text = initializationText(INIT_TIMINGS[index], props->msgs);
        place(check, 1 + index / 3, index % 3);
        group->add(check);
        checks.push_back(std::move(check));
    }
    none = createInitTimingCheck(comm, {}, types::VariableInitialization::None,
                                 read_only, changed);
    if (props) none->text = initializationText(types::VariableInitialization::None, props->msgs);
    place(none, 2, 1);
    group->add(none);
    setInitializationSelection(checks, none, value);
    if (parent) parent->add(group);
    return group;
}

struct DialogChrome {
    std::shared_ptr<lib::Button> ok;
    std::shared_ptr<lib::Button> cancel;
    std::shared_ptr<lib::Button> apply;
};

[[nodiscard]] DialogChrome addDialogChrome(
    const Commons& comm, const Props& props,
    const std::shared_ptr<lib::Dialog>& dialog,
    const std::shared_ptr<lib::Panel>& body, bool read_only) {
    auto root = std::make_shared<lib::GridLayout>();
    root->columns = 1;
    root->spacing = 0.0F;
    root->padding = {};
    dialog->set_layout(root);
    // フォームはフッターより上のクライアント領域を埋める。
    // 全体を中央寄せすると、 名前の上に空白が生じ、ウィ
    // ンドウを広げてもステップ一覧の高さが増えなくなる。
    place(body, 0, 0, 1, 1, 1.0F, {}, lib::Align::Stretch);
    dialog->add(body);

    auto separator = makeComponent<lib::Separator>(
        comm, lib::ComponentKind::Separator);
    place(separator, 1, 0, 1, 1, 0.0F, {1.0F, 1.0F});
    dialog->add(separator);

    auto footer = makeComponent<lib::Panel>(comm, lib::ComponentKind::Panel);
    auto footer_layout = std::make_shared<lib::GridLayout>();
    footer_layout->columns = 2U;
    footer_layout->spacing = 0.0F;
    footer_layout->padding = {6.0F, 5.0F, 6.0F, 5.0F};
    footer->set_layout(footer_layout);
    place(footer, 2, 0);
    dialog->add(footer);

    // SWT のボタンバーと同様、先頭の余白を伸縮させ、ボタン群
    // を等幅にする。 この構造を明示し、ネイティブのグリッド配
    // 置が余分な幅をすべて適用ボタンへ割り当てないようにする。
    auto leading = makeComponent<lib::Spacer>(comm, lib::ComponentKind::Spacer);
    place(leading, 0, 0, 1, 1, 1.0F);
    footer->add(leading);
    auto buttons = makeComponent<lib::Panel>(comm, lib::ComponentKind::Panel);
    auto buttons_layout = std::make_shared<lib::GridLayout>();
    buttons_layout->columns = read_only ? 1U : 3U;
    buttons_layout->equal_width = true;
    buttons_layout->spacing = 7.0F;
    buttons->set_layout(buttons_layout);
    // 現在のフォントを含め、ラベルからボタン群全体の寸法を求める。
    // ボタン群と各ボタンを、余白のあるフッター行の中央に配置する。
    place(buttons, 0, 1);
    footer->add(buttons);

    const auto add_button = [&](String text, std::size_t column) {
        auto button = makeComponent<lib::Button>(comm, lib::ComponentKind::Button);
        button->text = std::move(text);
        button->set_layout_item({.row = 0U, .column = column,
                                 .vertical = lib::Align::Center,
                                 .minimum = {98.0F, 0.0F},
                                 .preferred = {0.0F, 24.0F}});
        buttons->add(button);
        return button;
    };
    DialogChrome result;
    result.ok = add_button(read_only ? props.msgs.dialogClose : props.msgs.dialogOK, 0U);
    result.ok->default_button = true;
    if (!read_only) {
        result.cancel = add_button(props.msgs.dialogCancel, 1U);
        result.cancel->cancel_button = true;
        result.apply = add_button(props.msgs.dialogApply, 2U);
        result.apply->set_enabled(false);
    }
    return result;
}

void configureDialog(const std::shared_ptr<lib::Dialog>& dialog,
                     const std::shared_ptr<lib::Window>& owner,
                     String title, lib::ResourceId icon, lib::Size size,
                     const lib::Size minimum_size, const bool modal = false) {
    // D版と同様、まだ開いていないウィンドウを生成する。表示の開始は表示側が管理する。
    dialog->set_visible(false);
    dialog->title = std::move(title);
    dialog->icon = icon;
    dialog->resizable = true;
    dialog->minimizable = true;
    dialog->maximizable = true;
    // 組み込み先のフロントエンドが最上位の所有者を共通の Window
    // として渡せない場合も、 中央配置の方針を明示しておく。
    lib::configure_owned_window(dialog, owner,
        modal ? lib::OwnedWindowKind::Modal : lib::OwnedWindowKind::Modeless,
        lib::OwnedWindowCenter::OwnerOrHost);
    // 古い保存サイズによって初期化欄やフッターの行が再び欠けないようにする。
    // 共通のウィンドウ寸法処理でも、フォントの実測による最小サイズを守る。
    dialog->minimum_size = minimum_size;
    size.width = std::max(minimum_size.width, size.width);
    size.height = std::max(minimum_size.height, size.height);
    if (size.width > 0.0F && size.height > 0.0F)
        dialog->set_bounds({0.0F, 0.0F, size.width, size.height});
}

void presentVariableDialog(std::shared_ptr<lib::GuiBackend> gui,
                           std::shared_ptr<lib::Dialog> dialog,
                           std::function<void(lib::DialogResult)> completed) {
    if (!gui || !dialog) return;
    if (dialog->visible()) {
        gui->show(dialog);
        return;
    }
    dialog->result = lib::DialogResult::None;
    dialog->set_visible(true);
    if (!dialog->modal) {
        gui->show(dialog);
        return;
    }
    // すべてのホストで同じモーダル入力制御を使う。begin_modal に未
    // 対応の同期表示側では、 従来の show_modal の境界を使用する。
    if (gui->begin_modal(dialog, dialog->owner.lock())) return;
    const auto result = gui->show_modal(dialog, dialog->owner.lock());
    if (result != lib::DialogResult::None && dialog->visible() && completed)
        completed(result);
}

void retireModalVariableDialog(std::shared_ptr<lib::GuiBackend> gui,
                               std::shared_ptr<lib::Dialog> dialog) {
    if (!gui || !dialog || !dialog->modal || !dialog->visible()) return;
    dialog->on_close_requested = {};
    dialog->result = lib::DialogResult::Cancel;
    dialog->set_visible(false);
    gui->end_modal(dialog);
    gui->close(dialog);
}

void persistDialogSize(Props& props, Props* source, const lib::Rect bounds,
                       lib::Size Props::Variables::* field) {
    if (!std::isfinite(bounds.width) || !std::isfinite(bounds.height) ||
        bounds.width <= 0.0F || bounds.height <= 0.0F)
        return;
    const lib::Size size{bounds.width, bounds.height};
    props.var.*field = size;
    if (source) source->var.*field = size;
}

[[nodiscard]] String numberedValue(String pattern, const std::size_t index) {
    const auto number = std::to_string(index);
    if (const auto marker = pattern.find("{N}"); marker != String::npos)
        pattern.replace(marker, 3U, number);
    else if (const auto marker = pattern.find("$N"); marker != String::npos)
        pattern.replace(marker, 2U, number);
    else
        pattern += number;
    return pattern;
}

void refreshStepRows(const std::shared_ptr<lib::TableView>& table,
                     const std::vector<String>& values,
                     std::vector<std::size_t> selection) {
    if (!table) return;
    std::erase_if(selection, [&values](const std::size_t index) {
        return index >= values.size();
    });
    std::vector<lib::TableRow> rows;
    rows.reserve(values.size());
    for (std::size_t index = 0; index < values.size(); ++index) {
        static std::atomic<std::uint64_t> next_row_identity{0};
        rows.push_back(lib::TableRow{{
            lib::ListItem{std::to_string(index),
                          std::to_string(++next_row_identity)},
            lib::ListItem{values[index]}}});
    }
    (void)lib::replace_table_rows(table, std::move(rows),
                                  std::move(selection));
}

[[nodiscard]] std::uint32_t clampSelection(const std::uint32_t value,
                                           const std::vector<String>& values) {
    if (values.empty()) return 0;
    return std::min<std::uint32_t>(value,
                                   static_cast<std::uint32_t>(values.size() - 1));
}

[[nodiscard]] String pathOf(const VariableObject& object) {
    return std::visit(
        [](const auto& value) -> String { return value ? value->path() : String{}; },
        object);
}

[[nodiscard]] String joinPaths(const std::vector<String>& paths) {
    String result;
    for (const auto& path : paths) {
        if (path.empty()) continue;
        if (!result.empty()) result += '\n';
        result += path;
    }
    return result;
}

template <typename T>
void appendIfPresent(std::vector<T>& target, const std::vector<T>& source) {
    target.insert(target.end(), source.begin(), source.end());
}

}  // namespace

CheckBoxPtr createInitTimingCheck(
    const Commons& comm, const Parent& parent,
    const types::VariableInitialization timing, const bool readOnly,
    std::function<void(const CheckBoxPtr&)> changed) {
    auto result = makeComponent<lib::CheckBox>(comm, lib::ComponentKind::CheckBox);
    result->text = initializationText(timing);
    result->set_enabled(!readOnly);
    if (changed) {
        result->on_changed = [result, changed = std::move(changed)](const bool) {
            changed(result);
        };
    }
    if (parent) parent->add(result);
    return result;
}

types::VariableInitialization selectedInitialization(
    const std::vector<CheckBoxPtr>& checks, const CheckBoxPtr& none) {
    if (none && none->checked) return types::VariableInitialization::None;
    auto value = types::VariableInitialization::None;
    for (std::size_t index = 0; index < checks.size() && index < INIT_TIMINGS.size();
         ++index) {
        if (checks[index] && checks[index]->checked) {
            value = static_cast<types::VariableInitialization>(
                static_cast<std::int32_t>(value) |
                static_cast<std::int32_t>(INIT_TIMINGS[index]));
        }
    }
    return value == types::VariableInitialization::None
               ? types::VariableInitialization::Leave
               : value;
}

void setInitializationSelection(
    const std::vector<CheckBoxPtr>& checks, const CheckBoxPtr& none,
    const types::VariableInitialization value) {
    const auto raw = static_cast<std::int32_t>(value);
    if (none) none->checked = value == types::VariableInitialization::None;
    for (std::size_t index = 0; index < checks.size() && index < INIT_TIMINGS.size();
         ++index) {
        if (!checks[index]) continue;
        checks[index]->checked = value != types::VariableInitialization::None &&
                                 types::hasVariableInitialization(
                                     value, INIT_TIMINGS[index]);
    }
    (void)raw;
}

void normalizeInitializationSelection(
    const std::vector<CheckBoxPtr>& checks, const CheckBoxPtr& none,
    const CheckBoxPtr& changed) {
    if (!none) return;
    if (changed.get() == none.get() && none->checked) {
        for (const auto& check : checks)
            if (check) check->checked = false;
        return;
    }
    if (changed && changed.get() != none.get() && changed->checked)
        none->checked = false;

    const bool selected = std::any_of(
        checks.begin(), checks.end(), [](const auto& check) {
            return check && check->checked;
        });
    if (!none->checked && !selected) {
        for (std::size_t index = 0;
             index < checks.size() && index < INIT_TIMINGS.size(); ++index) {
            if (checks[index])
                checks[index]->checked =
                    INIT_TIMINGS[index] == types::VariableInitialization::Leave;
        }
    }
}

StepEditDialog::StepEditDialog(Commons comm, std::shared_ptr<Summary> summary,
                               std::shared_ptr<lib::Window> shell,
                               const bool readOnly, const bool local,
                               FlagDirPtr dir, StepPtr step)
    : comm_(std::move(comm)), summary_(std::move(summary)),
      shell_(std::move(shell)), readOnly_(readOnly), local_(local),
      dir_(std::move(dir)), step_(std::move(step)) {
    if (step_) {
        name_ = step_->name();
        values_ = step_->values();
        selection_ = step_->select();
        initialization_ = step_->initialization();
        expand_special_characters_ = step_->expandSPChars();
    } else {
        name_.clear();
        values_.clear();
        values_.reserve(props_.looks.stepMaxCount);
        for (std::size_t index = 0; index < props_.looks.stepMaxCount; ++index)
            values_.push_back(numberedValue(props_.etc.stepValueName, index));
        if (values_.empty()) values_.push_back({});
        selection_ = static_cast<std::uint32_t>(std::clamp<std::int32_t>(
            props_.etc.stepInitValue, 0,
            static_cast<std::int32_t>(values_.size() - 1U)));
    }
    value_cache_ = values_;
    undo_max_token_ = comm_.refUndoMax.connect(
        [this, life = std::weak_ptr<int>(undo_lifetime_)] {
            if (!life.expired()) refValueUndoMax();
        });
    live_root_ = dir_ ? dir_->root() : FlagDirPtr{};
    was_attached_ = step_ && step_->parent();
    watchDeletion();
    version_token_ = comm_.refDataVersion.connect(
        [this, life = std::weak_ptr<int>(delete_lifetime_)] {
            if (!life.expired()) { refreshCompatibility(); refreshSpecialPreviews(); }
        });
    const auto refresh_preview = [this, life = std::weak_ptr<int>(delete_lifetime_)](
        const FlagList&, const StepList&, const VariantList&) {
        if (!life.expired()) refreshSpecialPreviews();
    };
    preview_token_ = comm_.refFlagAndStep.connect(refresh_preview);
    preview_delete_token_ = comm_.delFlagAndStep.connect(refresh_preview);
    preview_refresh_token_ = comm_.refSpecialPreview.connect(
        [this, life = std::weak_ptr<int>(delete_lifetime_)] {
            if (!life.expired()) refreshSpecialPreviews();
        });
}

StepEditDialog::~StepEditDialog() {
    comm_.refFlagAndStep.remove(preview_token_);
    comm_.delFlagAndStep.remove(preview_delete_token_);
    comm_.refSpecialPreview.remove(preview_refresh_token_);
    delete_lifetime_.reset();
    comm_.refDataVersion.remove(version_token_);
    if (auto disconnect = std::exchange(stop_watching_deletion_, {}))
        disconnect();
    undo_lifetime_.reset();
    comm_.refUndoMax.remove(undo_max_token_);
    retireModalVariableDialog(comm_.services.gui, dialog_);
}

StepEditDialog::StepEditDialog(Commons comm, Summary* summary,
                               std::shared_ptr<lib::Window> shell,
                               const bool readOnly, const bool local,
                               FlagDirPtr dir, StepPtr step)
    : StepEditDialog(std::move(comm), borrowSummary(summary), std::move(shell),
                     readOnly, local, std::move(dir), std::move(step)) {}

void StepEditDialog::watchDeletion() {
    if (readOnly_) return;
    stop_watching_deletion_ = comm_.observeVariableDeletion(
        [this, life = std::weak_ptr<int>(delete_lifetime_)](const FlagList& flags, const StepList& steps, const VariantList& variants) {
            if (life.expired() || !step_ ||
                std::find(steps.begin(), steps.end(), step_) == steps.end()) return;
            deleted_ = true;
            draft_dirty_ = false;
            if (ok_button_) ok_button_->set_enabled(false);
            if (apply_button_) apply_button_->set_enabled(false);
            if (auto disconnect = std::exchange(stop_watching_deletion_, {}))
                disconnect();
            close(lib::DialogResult::Cancel);
        });
}

void StepEditDialog::presentation(Props& value) {
    presentation(static_cast<const Props&>(value));
    props_source_ = &value;
}

void StepEditDialog::presentation(const Props& value) {
    auto copied = value;
    copied.synchronize();
    props_ = std::move(copied);
    refValueUndoMax();
    props_source_ = nullptr;
    if (!step_ && !component_) {
        values_.clear();
        values_.reserve(props_.looks.stepMaxCount);
        for (std::size_t index = 0; index < props_.looks.stepMaxCount; ++index)
            values_.push_back(numberedValue(props_.etc.stepValueName, index));
        if (values_.empty()) values_.push_back({});
        selection_ = static_cast<std::uint32_t>(std::clamp<std::int32_t>(
            props_.etc.stepInitValue, 0,
            static_cast<std::int32_t>(values_.size() - 1U)));
        value_cache_ = values_;
    }
}

StepEditDialog::ValueSnapshot StepEditDialog::valueSnapshot() const {
    return {values_,
            values_control_ ? values_control_->selection
                            : std::vector<std::size_t>{}};
}

void StepEditDialog::restoreValueSnapshot(ValueSnapshot value) {
    values_ = std::move(value.values);
    if (values_.empty()) values_.push_back({});
    // D版と同様、値の履歴を戻しても、その後に独立して選んだ初期値は戻さない。
    selection_ = clampSelection(selection_, values_);
    refreshValueControls(std::move(value.row_selection));
    markModified();
}

void StepEditDialog::refValueUndoMax() {
    if (props_.etc.undoMaxEtcProvider)
        props_.etc.undoMaxEtc = props_.etc.undoMaxEtcProvider();
    const auto maximum = props_.etc.undoMaxEtc;
    // D版の UndoManager::max と同様、適用済み履歴だけを削り、保留中のやり直し履歴は残す。
    if (undo_values_.size() > maximum)
        undo_values_.erase(undo_values_.begin(),
                           undo_values_.end() - maximum);
    refreshValueActionButtons();
}

void StepEditDialog::storeValueUndo() {
    // D版の UndoManager::add と同様、上限がゼロなら既存の履歴も保留中のやり直しも変更しない。
    if (props_.etc.undoMaxEtc == 0U) return;
    undo_values_.push_back(valueSnapshot());
    const auto maximum = props_.etc.undoMaxEtc;
    if (undo_values_.size() > maximum)
        undo_values_.erase(undo_values_.begin(),
                           undo_values_.end() - maximum);
    redo_values_.clear();
}

void StepEditDialog::syncValueCache() {
    if (value_cache_.size() < values_.size())
        value_cache_.resize(values_.size());
    std::copy(values_.begin(), values_.end(), value_cache_.begin());
}

void StepEditDialog::refreshValueControls(
    std::optional<std::vector<std::size_t>> rowSelection) {
    syncValueCache();
    selection_ = clampSelection(selection_, values_);
    if (values_control_) {
        auto rows = rowSelection ? std::move(*rowSelection)
                                 : values_control_->selection;
        refreshStepRows(values_control_, values_, std::move(rows));
    }
    if (initial_control_) {
        std::vector<lib::ChoiceItem> items;
        items.reserve(values_.size());
        for (const auto& value : values_)
            items.push_back({value, value});
        (void)lib::replace_combo_items(initial_control_, std::move(items),
                                       selection_, initial_control_->edit_text);
    }
    if (count_control_)
        count_control_->value = static_cast<double>(values_.size());
    refreshValueActionButtons();
}

bool StepEditDialog::canMoveValuesUp() const noexcept {
    if (readOnly_ || !values_control_ || values_control_->selection.empty())
        return false;
    return std::all_of(values_control_->selection.begin(),
                       values_control_->selection.end(),
                       [this](const std::size_t index) {
                           return index > 0U && index < values_.size();
                       });
}

bool StepEditDialog::canMoveValuesDown() const noexcept {
    if (readOnly_ || !values_control_ || values_control_->selection.empty())
        return false;
    return std::all_of(values_control_->selection.begin(),
                       values_control_->selection.end(),
                       [this](const std::size_t index) {
                           return index + 1U < values_.size();
                       });
}

bool StepEditDialog::canCreateFollowingValues() const noexcept {
    if (readOnly_ || !values_control_ || values_control_->selection.empty())
        return false;
    const auto index = *std::min_element(values_control_->selection.begin(),
                                         values_control_->selection.end());
    return index + 1U < values_.size();
}

void StepEditDialog::refreshValueActionButtons() {
    if (!value_menu_) return;
    auto items = value_menu_->items;
    for (auto& item : items) {
        if (item.command == "undo") item.enabled = !readOnly_ && canUndo();
        else if (item.command == "redo") item.enabled = !readOnly_ && canRedo();
        else if (item.command == "up") item.enabled = canMoveValuesUp();
        else if (item.command == "down") item.enabled = canMoveValuesDown();
        else if (item.command == "create-following")
            item.enabled = canCreateFollowingValues();
        else if (item.command == "copy") {
            item.enabled = values_control_ && !values_control_->selection.empty();
        } else if (item.command == "select-all") {
            item.enabled = values_control_ && !values_.empty() &&
                           values_control_->selection.size() != values_.size();
        }
    }
    (void)lib::replace_menu_items(value_menu_, std::move(items));
}

bool StepEditDialog::finishValueEdit() {
    const std::weak_ptr<int> life = delete_lifetime_;
    const auto gui = comm_.services.gui;
    if (gui && values_control_ && gui->programmatic_inline_edit_active(values_control_))
        (void)gui->finish_programmatic_inline_edit(values_control_, true);
    return !life.expired();
}

void StepEditDialog::restartValueEdit(const std::size_t index) {
    const auto gui = comm_.services.gui;
    const auto table = values_control_;
    if (!gui || !table || index >= values_.size()) return;
    const std::weak_ptr<int> life = delete_lifetime_;
    const auto selected = table->selection;
    const auto revision = table->row_order_revision;
    lib::ProgrammaticInlineEditRequest request;
    request.table = table;
    const auto identity = table->rows[index].cells.front().value;
    request.matches_stable_row = [weak = std::weak_ptr<lib::TableView>(table), revision, identity](std::size_t row) {
        const auto value = weak.lock();
        return value && value->row_order_revision == revision &&
            row < value->rows.size() && !value->rows[row].cells.empty() &&
            value->rows[row].cells.front().value == identity;
    };
    request.focus_scope = dialog_ ? std::static_pointer_cast<lib::Widget>(dialog_)
                                  : std::static_pointer_cast<lib::Widget>(component_);
    request.row_height = lib::scaled_ui_metric(lib::kEditorUiMetrics.row_height);
    (void)gui->request_programmatic_inline_edit(request);
    if (!life.expired() && table->row_order_revision == revision)
        (void)lib::select_table_rows(*table, selected);
}

void StepEditDialog::moveValuesUp() {
    if (!canMoveValuesUp()) return;
    if (!finishValueEdit() || !canMoveValuesUp()) return;
    storeValueUndo();
    auto indices = values_control_->selection;
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    for (auto& index : indices) {
        std::swap(values_[index - 1U], values_[index]);
        --index;
    }
    const auto edit_index = indices.front();
    refreshValueControls(std::move(indices));
    markModified();
    restartValueEdit(edit_index);
}

void StepEditDialog::moveValuesDown() {
    if (!canMoveValuesDown()) return;
    if (!finishValueEdit() || !canMoveValuesDown()) return;
    storeValueUndo();
    auto indices = values_control_->selection;
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    for (auto it = indices.rbegin(); it != indices.rend(); ++it) {
        std::swap(values_[*it], values_[*it + 1U]);
        ++*it;
    }
    const auto edit_index = indices.back();
    refreshValueControls(std::move(indices));
    markModified();
    restartValueEdit(edit_index);
}

void StepEditDialog::createFollowingValues() {
    if (!canCreateFollowingValues()) return;
    const auto index = *std::min_element(values_control_->selection.begin(),
                                         values_control_->selection.end());
    auto updated = values_;
    auto last = values_[index];
    const auto editor = value_editor_.lock();
    const bool active_seed = editor && value_editor_row_ == index &&
        value_editor_revision_ == values_control_->row_order_revision &&
        comm_.services.gui &&
        comm_.services.gui->programmatic_inline_edit_active(values_control_);
    if (active_seed) last = editor->text;
    for (std::size_t current = index + 1U; current < updated.size(); ++current) {
        last = core::utils::createNewName(
            last, [&last](String candidate) {
                return candidate.empty() || candidate != last;
            });
        updated[current] = last;
    }
    // D版と同様、生成した値が少なくとも一つ変わった場合だけ履歴を記録する。
    if (updated == values_) return;
    storeValueUndo();
    values_ = std::move(updated);
    if (active_seed) {
        // D版と同様、後続の行だけを更新し、生成元の編集欄は未適用の入力を残した
        // まま開いておく。 行を置き換えると、その編集状態が無効になってしまう。
        syncValueCache();
        for (std::size_t row = index + 1U; row < values_.size(); ++row)
            values_control_->rows[row].cells[1].text = values_[row];
        ++values_control_->row_update_revision;
        if (initial_control_) {
            std::vector<lib::ChoiceItem> items;
            for (const auto& value : values_) items.push_back({value, value});
            (void)lib::replace_combo_items(initial_control_, std::move(items),
                                           selection_, initial_control_->edit_text);
        }
        refreshValueActionButtons();
        if (comm_.services.gui) comm_.services.gui->request_redraw(values_control_);
    } else {
        refreshValueControls();
    }
    markModified();
}

void StepEditDialog::undo() {
    if (!canUndo()) return;
    redo_values_.push_back(valueSnapshot());
    auto value = std::move(undo_values_.back());
    undo_values_.pop_back();
    restoreValueSnapshot(std::move(value));
}

void StepEditDialog::redo() {
    if (!canRedo()) return;
    undo_values_.push_back(valueSnapshot());
    auto value = std::move(redo_values_.back());
    redo_values_.pop_back();
    restoreValueSnapshot(std::move(value));
    // D版と同様、やり直しではカーソルを進めてから、適用済み履歴を上限まで削る。
    refValueUndoMax();
}

PanelPtr StepEditDialog::setup(const Parent& parent) {
    if (component_) {
        if (parent) parent->add(component_);
        return component_;
    }
    component_ = makeComponent<lib::Panel>(comm_, lib::ComponentKind::Panel);
    auto layout = std::make_shared<lib::GridLayout>();
    layout->columns = 1;
    layout->spacing = 5.0F;
    layout->padding = {5.0F, 4.0F, 4.0F, 5.0F};
    component_->set_layout(layout);

    top_split_ = std::make_shared<splitpane::SplitPane>(splitpane::Horizontal);
    top_split_->setWeights(
        {props_.etc.stepTopSashL, props_.etc.stepTopSashR});
    place(top_split_, 0, 0, 1, 1, 0.0F, {385.0F, 0.0F});
    (void)top_split_->addSelectionListener([this](auto&) {
        const auto weights = top_split_->getWeights();
        if (weights.size() < 2U) return;
        props_.etc.stepTopSashL = weights[0];
        props_.etc.stepTopSashR = weights[1];
        props_.synchronize();
        if (props_source_) {
            props_source_->etc.stepTopSashL = weights[0];
            props_source_->etc.stepTopSashR = weights[1];
            props_source_->synchronize();
        }
    });
    top_split_->on_weights_changed = [this](const std::vector<float>& weights) {
        if (weights.size() < 2U || !std::isfinite(weights[0]) ||
            !std::isfinite(weights[1]))
            return;
        const auto left = static_cast<std::int32_t>(
            std::max(1.0F, std::round(weights[0])));
        const auto right = static_cast<std::int32_t>(
            std::max(1.0F, std::round(weights[1])));
        props_.etc.stepTopSashL = left;
        props_.etc.stepTopSashR = right;
        props_.synchronize();
        if (props_source_) {
            props_source_->etc.stepTopSashL = left;
            props_source_->etc.stepTopSashR = right;
            props_source_->synchronize();
        }
    };
    component_->add(top_split_);

    auto top_left = makeComponent<lib::Panel>(comm_, lib::ComponentKind::Panel);
    auto left_layout = std::make_shared<lib::GridLayout>();
    left_layout->columns = 2;
    left_layout->spacing = 5.0F;
    top_left->set_layout(left_layout);
    auto top_right = makeComponent<lib::Panel>(comm_, lib::ComponentKind::Panel);
    auto right_layout = std::make_shared<lib::GridLayout>();
    right_layout->columns = 4;
    right_layout->spacing = 5.0F;
    top_right->set_layout(right_layout);
    top_split_->add(top_left);
    top_split_->add(top_right);
    auto addTopLabel = [&](const Parent& target, String text,
                           std::size_t column) {
        auto label = makeComponent<lib::Label>(comm_, lib::ComponentKind::Label);
        label->text = std::move(text);
        place(label, 0, column);
        target->add(label);
    };

    addTopLabel(top_left, props_.msgs.stepName, 0);
    name_control_ = makeComponent<lib::TextEdit>(
        comm_, lib::ComponentKind::TextEdit);
    name_control_->text = name_;
    name_control_->read_only = readOnly_;
    name_control_->selection_start = 0;
    name_control_->selection_length = name_control_->text.size();
    name_control_->on_changed = [this](std::string_view text) {
        name_ = String{text};
        markModified();
    };
    place(name_control_, 0, 1, 1, 1, 1.0F,
          {std::max(1.0F, props_.etc.flagNameWidth), 24.0F});
    top_left->add(name_control_);

    addTopLabel(top_right, props_.msgs.stepInitial, 0);
    initial_control_ = makeComponent<lib::ComboBox>(
        comm_, lib::ComponentKind::ComboBox);
    initial_control_->editable = false;
    initial_control_->set_enabled(!readOnly_);
    for (const auto& value : values_)
        initial_control_->items.push_back({value, value});
    initial_control_->selected_index = clampSelection(selection_, values_);
    initial_control_->on_selection_changed = [this](std::optional<std::size_t> index) {
        if (index) {
            selection_ = static_cast<std::uint32_t>(*index);
            markModified();
        }
    };
    place(initial_control_, 0, 1, 1, 1, 1.0F,
          {std::max(1.0F, props_.etc.flagInitWidth), 24.0F});
    top_right->add(initial_control_);

    addTopLabel(top_right, props_.msgs.stepCount, 2);
    count_control_ = makeComponent<lib::NumberEdit>(
        comm_, lib::ComponentKind::NumberEdit);
    count_control_->minimum = 1;
    count_control_->maximum = static_cast<double>(
        std::max<std::size_t>(1U, props_.etc.stepCountMax));
    count_control_->value = static_cast<double>(
        std::max<std::size_t>(1, values_.size()));
    count_control_->read_only = readOnly_;
    count_control_->set_enabled(!readOnly_);
    // ALL の分割された NumberEdit には減算・加算ボタンが別にあるた
    // め、 文字倍率100%でも 43px では共通コントロールの幅が足りない。
    place(count_control_, 0, 3, 1, 1, 0.0F, {59.0F, 24.0F});
    top_right->add(count_control_);

    values_control_ = makeComponent<lib::TableView>(
        comm_, lib::ComponentKind::TableView);
    // D版と同様、この二列の値の表では setHeaderVisible(true)
    // を指定しない。 両列の見出しは空なので、ネイティブのヘッダーを
    // 出すと、 操作欄と最初のステップ値の間に空行が入るだけになる。
    values_control_->header_visible = false;
    values_control_->columns = {
        {"", std::max(1.0F, props_.etc.valueNumberColumn),
         lib::TextAlignment::Start, false, false},
        {"", 320.0F, lib::TextAlignment::Start, true, false}};
    values_control_->on_column_resized = [this](std::size_t column, float width) {
        if (column != 0U || !std::isfinite(width) || width <= 0.0F) return;
        props_.etc.valueNumberColumn = width;
        props_.synchronize();
        if (props_source_) {
            props_source_->etc.valueNumberColumn = width;
            props_source_->synchronize();
        }
    };
    values_control_->multi_select = true;
    values_control_->full_row_selection = true;
    values_control_->inline_edit_column = 1U;
    const std::weak_ptr<int> value_life = delete_lifetime_;
    values_control_->on_inline_editor_created =
        [this, value_life](std::size_t row, std::size_t column,
                           const std::shared_ptr<lib::TextEdit>& editor,
                           const std::shared_ptr<lib::ComboBox>&) {
            if (value_life.expired() || column != 1U || !editor) return;
            special_text_menus_.clear();
            value_editor_ = editor;
            value_editor_row_ = row;
            value_editor_revision_ = values_control_->row_order_revision;
            if (!readOnly_) {
                auto special_menu = attachVariableSpecialMenu(comm_, summary_,
                    dir_ ? dir_->useCounter() : comm_.useCounter, dmenu::TMM(editor), value_life,
                    [this, value_life] { return !value_life.expired() && expand_special_characters_; });
                if (special_menu) {
                    // D版と同様、表とインライン編集欄の両メニューに CreateStepValues を追加する。
                    const auto menu = special_menu->menu();
                    if (menu && std::none_of(menu->items.begin(), menu->items.end(),
                            [](const lib::MenuItem& item) {
                                return item.command == "create-following";
                            })) {
                        lib::MenuItem separator;
                        separator.separator = true;
                        menu->items.push_back(std::move(separator));
                        lib::MenuItem create_following;
                        create_following.text = comm_.menuText
                            ? comm_.menuText(types::MenuID::CreateStepValues)
                            : String{"後続値を自動生成"};
                        create_following.command = "create-following";
                        if (comm_.menuImage)
                            create_following.icon = comm_.menuImage(
                                types::MenuID::CreateStepValues);
                        menu->items.push_back(std::move(create_following));

                        const auto previous_open = menu->on_open;
                        const std::weak_ptr<lib::Menu> weak_menu = menu;
                        menu->on_open = [this, value_life, weak_menu, previous_open] {
                            if (previous_open) previous_open();
                            if (value_life.expired()) return;
                            if (const auto current = weak_menu.lock()) {
                                for (auto& item : current->items)
                                    if (item.command == "create-following")
                                        item.enabled = canCreateFollowingValues();
                                ++current->item_update_revision;
                            }
                        };
                        const auto previous_command = menu->on_command;
                        menu->on_command = [this, value_life, previous_command](
                                const StringView command) {
                            if (command == "create-following") {
                                if (!value_life.expired()) createFollowingValues();
                                return;
                            }
                            if (previous_command) previous_command(command);
                        };
                    }
                    special_text_menus_.push_back(std::move(special_menu));
                }
            }
            const auto previous_changed = editor->on_changed;
            editor->on_changed = [this, value_life, previous_changed](std::string_view text) {
                if (previous_changed) previous_changed(text);
                if (!value_life.expired()) refreshSpecialPreviews();
            };
            refreshSpecialPreviews();
            editor->on_inline_edit_finished = [this, value_life,
                finished = std::weak_ptr<lib::TextEdit>(editor)](bool) {
                if (!value_life.expired() && value_editor_.lock() == finished.lock()) {
                    value_editor_.reset();
                    special_text_menus_.clear();
                    refreshSpecialPreviews();
                }
            };
        };
    values_control_->quick_edit_arrow_target =
        [this, value_life](std::size_t row, std::uint64_t revision,
                           std::string_view row_identity, lib::Key key)
            -> lib::TableView::QuickEditArrowResolver {
            if (value_life.expired()) return {};
            const auto table = values_control_;
            if (readOnly_ || deleted_ || !table ||
                row != value_editor_row_ || revision != value_editor_revision_ ||
                revision != table->row_order_revision || row >= values_.size() ||
                table->rows.size() != values_.size() ||
                table->rows[row].cells.empty() ||
                table->rows[row].cells.front().value != row_identity)
                return {};
            if (key == lib::Key::Up) {
                if (row == 0U) return {};
            } else if (key != lib::Key::Down) return {};
            const auto target = key == lib::Key::Up ? row - 1U : row + 1U;
            if (target >= values_.size()) return {};
            const auto expected_values = values_;
            const auto expected_selection = table->selection;
            const std::weak_ptr<lib::TableView> weak_table = table;
            return [this, value_life, weak_table, row, target,
                    expected_values, expected_selection](std::string_view committed_text)
                -> std::optional<std::size_t> {
                if (value_life.expired()) return {};
                const auto current = weak_table.lock();
                if (!current || current != values_control_ || readOnly_ || deleted_ ||
                    row >= expected_values.size() || target >= expected_values.size() ||
                    values_.size() != expected_values.size() ||
                    current->rows.size() != values_.size() ||
                    current->selection != expected_selection) return {};
                for (std::size_t index = 0; index < values_.size(); ++index) {
                    const auto expected = index == row
                        ? std::string(committed_text) : expected_values[index];
                    if (values_[index] != expected || current->rows[index].cells.size() < 2U ||
                        current->rows[index].cells[1].text != expected) return {};
                }
                return target;
            };
        };
    refreshStepRows(values_control_, values_, {});
    // D版の TableTextEdit.quickStart=Quick と
    // 同様、全体の改名遅延設定とは独立して、 選択時やフォーカスを受けたときに
    // 値の編集を始める。 この入力動作は ALL でも共通に保つ。ALL が
    // 変えるのは画面構成だけであり、 D版互換の即時編集経路を無効にしない。
    const auto quick_start_busy = std::make_shared<bool>(false);
    const auto begin_value_edit = [this, value_life, quick_start_busy](std::size_t row) {
        if (value_life.expired() || *quick_start_busy || readOnly_ || deleted_ ||
            !values_control_ ||
            row >= values_.size()) return;
        const auto gui = comm_.services.gui;
        if (!gui) return;
        if (gui->programmatic_inline_edit_active(values_control_) &&
            !value_editor_.expired() && value_editor_row_ == row &&
            value_editor_revision_ == values_control_->row_order_revision) return;
        *quick_start_busy = true;
        if (finishValueEdit() && !deleted_ && row < values_.size())
            restartValueEdit(row);
        *quick_start_busy = false;
    };
    values_control_->on_selection_changed = [this, value_life, begin_value_edit](const auto& rows) {
        if (value_life.expired()) return;
        const auto first = rows.empty() ? std::optional<std::size_t>{} : std::optional<std::size_t>{rows.front()};
        refreshValueActionButtons();
        if (first) begin_value_edit(*first);
    };
    values_control_->on_focus = [this, value_life, begin_value_edit] {
        if (value_life.expired() || readOnly_ || deleted_) return;
        const auto start = [this, value_life, begin_value_edit] {
            if (value_life.expired() || !values_control_ || !comm_.services.gui ||
                comm_.services.gui->focused_widget() != values_control_ || values_.empty()) return;
            if (values_control_->selection.empty())
                (void)lib::select_table_rows(*values_control_, {0U}, true, false);
            begin_value_edit(values_control_->selection.front());
        };
        if (comm_.services.dispatcher) comm_.services.dispatcher->post(start);
        else start();
    };
    values_control_->on_cell_edited = [this](std::size_t row,
                                              std::size_t column,
                                              std::string text) {
        if (readOnly_ || column != 1 || row >= values_.size()) return;
        if (values_[row] == text) return;
        storeValueUndo();
        values_[row] = std::move(text);
        refreshValueControls();
        markModified();
    };
    value_menu_ = std::make_shared<lib::Menu>(true);
    if (readOnly_) {
        value_menu_->items.push_back({"コピー", "copy"});
        lib::MenuItem separator;
        separator.separator = true;
        value_menu_->items.push_back(std::move(separator));
        value_menu_->items.push_back({"すべて選択", "select-all"});
    } else {
        value_menu_->items.push_back({"元に戻す", "undo"});
        value_menu_->items.push_back({"やり直す", "redo"});
        lib::MenuItem separator1;
        separator1.separator = true;
        value_menu_->items.push_back(std::move(separator1));
        value_menu_->items.push_back({"上へ", "up"});
        value_menu_->items.push_back({"下へ", "down"});
        lib::MenuItem separator2;
        separator2.separator = true;
        value_menu_->items.push_back(std::move(separator2));
        value_menu_->items.push_back(
            {"後続値を自動生成", "create-following"});
    }
    value_menu_->on_open = [this] { refreshValueActionButtons(); };
    value_menu_->on_command = [this](StringView command) {
        if (command == "undo") undo();
        else if (command == "redo") redo();
        else if (command == "up") moveValuesUp();
        else if (command == "down") moveValuesDown();
        else if (command == "create-following") createFollowingValues();
        else if (command == "copy" && comm_.services.clipboard &&
                 values_control_) {
            auto indices = values_control_->selection;
            std::sort(indices.begin(), indices.end());
            String text;
            bool has_selected_row = false;
            for (const auto index : indices) {
                if (index >= values_.size()) continue;
                if (has_selected_row) text.push_back('\n');
                text += values_[index];
                has_selected_row = true;
            }
            if (has_selected_row) {
                lib::ClipboardData data;
                data.text = std::move(text);
                comm_.services.clipboard->write(data);
            }
        } else if (command == "select-all" && values_control_) {
            std::vector<std::size_t> selection(values_.size());
            std::iota(selection.begin(), selection.end(), 0U);
            (void)lib::select_table_rows(*values_control_,
                                         std::move(selection));
        }
        refreshValueActionButtons();
    };
    values_control_->on_pointer = [this, life = std::weak_ptr<int>(delete_lifetime_)](lib::PointerEvent& event) {
        if (life.expired()) return;
        preview_hover_row_ = lib::hit_test_table_row(*values_control_, event.position,
            values_control_->row_height > 0.0F ? values_control_->row_height : 24.0F);
        if (preview_hover_row_) refreshSpecialPreviews();
        else values_control_->set_tooltip({});
        if (event.button != lib::PointerButton::Secondary ||
            event.action != lib::PointerAction::Up || !comm_.services.gui)
            return;
        refreshValueActionButtons();
        comm_.services.gui->show_context_menu(value_menu_, values_control_,
                                               event.position);
        event.handled = true;
    };
    if (!readOnly_) {
        dutils::installClickSelectedAction(values_control_, comm_.services,
            begin_value_edit, [] { return true; }, {}, 1U);
    }
    place(values_control_, 1, 0, 1, 1, 1.0F, {385.0F, 224.0F}, lib::Align::Stretch);
    component_->add(values_control_);

    count_control_->on_changed = [this](double raw) {
        if (readOnly_) return;
        const auto wanted = static_cast<std::size_t>(
            std::clamp(raw, 1.0, static_cast<double>(
                std::max<std::size_t>(1U, props_.etc.stepCountMax))));
        const auto old = values_.size();
        if (wanted == old) return;
        storeValueUndo();
        syncValueCache();
        auto last = value_cache_.empty() ? String{} : value_cache_.back();
        for (std::size_t index = value_cache_.size(); index < wanted; ++index) {
            last = core::utils::createNewName(
                last, [&last](String candidate) {
                    return candidate.empty() || candidate != last;
                });
            value_cache_.push_back(last);
        }
        values_.assign(value_cache_.begin(), value_cache_.begin() + wanted);
        refreshValueControls();
        markModified();
    };

    auto bottom = makeComponent<lib::Panel>(comm_, lib::ComponentKind::Panel);
    auto bottom_layout = std::make_shared<lib::GridLayout>();
    bottom_layout->columns = 2;
    bottom_layout->spacing = 6.0F;
    bottom->set_layout(bottom_layout);
    place(bottom, 2, 0, 1, 1, 0.0F, {385.0F, 0.0F});
    component_->add(bottom);
    auto expand = makeComponent<lib::CheckBox>(comm_, lib::ComponentKind::CheckBox);
    expand_control_ = expand;
    expand->text = props_.msgs.expandSpecialCharacters;
    expand->checked = expand_special_characters_;
    expand->set_enabled(!readOnly_);
    expand->on_changed = [this](bool value) {
        expand_special_characters_ = value;
        markModified();
    };
    place(expand, 0, 0);
    bottom->add(expand);

    auto init_panel = makeInitializationPanel(
        comm_, {}, readOnly_, initialization_checks_,
        initialization_none_, initialization_, &props_,
        [this] { markModified(); });
    refreshCompatibility();
    place(init_panel, 0, 1, 1, 1, 1.0F);
    bottom->add(init_panel);
    refreshValueActionButtons();
    warning_control_ = makeComponent<lib::Label>(comm_, lib::ComponentKind::Label);
    warning_control_->wrap = true;
    place(warning_control_, 5, 0, 1, 1, 1.0F, {320.0F, 18.0F});
    component_->add(warning_control_);
    refreshWarning();
    if (parent) parent->add(component_);
    return component_;
}

void StepEditDialog::refreshWarning() {
    auto values = values_;
    if (const auto editor = value_editor_.lock(); editor && value_editor_row_ < values.size())
        values[value_editor_row_] = editor->text;
    showVariableWarnings(warning_control_, variableWarnings(comm_, summary_.get(), dir_, name_,
        values, true, local_, expand_special_characters_, initialization_, props_.looks.stepMaxCount));
}

void StepEditDialog::refreshCompatibility() {
    refreshWarning();
    const bool legacy = summary_ && summary_->legacy();
    if (count_control_)
        count_control_->set_enabled(!readOnly_ &&
            (!legacy || values_.size() != props_.looks.stepMaxCount));
    if (expand_control_)
        expand_control_->set_enabled(!readOnly_ && (!legacy || expand_control_->checked));
    for (const auto& check : initialization_checks_)
        if (check) check->set_enabled(!readOnly_ && (local_ || !legacy || check->checked));
    if (initialization_none_)
        initialization_none_->set_enabled(!readOnly_ &&
            (local_ || !legacy || initialization_none_->checked));
}

void StepEditDialog::markModified() {
    refreshCompatibility();
    refreshSpecialPreviews();
    if (!readOnly_) draft_dirty_ = true;
    const bool valid = !flag::FlagDir::validName(name_).empty();
    if (ok_button_) ok_button_->set_enabled(readOnly_ || valid);
    if (apply_button_ && !readOnly_) apply_button_->set_enabled(valid);
}

std::shared_ptr<lib::Dialog> StepEditDialog::setupDialog(
    const std::shared_ptr<lib::Window>& owner) {
    if (dialog_) return dialog_;
    dialog_ = std::make_shared<lib::Dialog>();
    configureDialog(dialog_, owner ? owner : shell_, props_.msgs.stepDialogTitle,
                     props_.stepImage, props_.var.stepDlg, {520.0F, 400.0F}, readOnly_);
    const auto chrome = addDialogChrome(comm_, props_, dialog_, setup({}), readOnly_);
    ok_button_ = chrome.ok;
    // D版の StepEditDialog と同様、enterClose = readOnly とする。
    ok_button_->default_button = readOnly_;
    cancel_button_ = chrome.cancel;
    apply_button_ = chrome.apply;
    ok_button_->set_enabled(readOnly_ ||
                            !flag::FlagDir::validName(name_).empty());
    ok_button_->on_click = [this] {
        if (readOnly_ || apply()) close(lib::DialogResult::OK);
    };
    if (cancel_button_)
        cancel_button_->on_click = [this] { close(lib::DialogResult::Cancel); };
    if (apply_button_)
        apply_button_->on_click = [this] { (void)apply(); };
    dialog_->on_close_requested = [this] { close(lib::DialogResult::Cancel); };
    return dialog_;
}

void StepEditDialog::show(const std::shared_ptr<lib::Window>& owner) {
    const auto value = setupDialog(owner);
    presentVariableDialog(comm_.services.gui, value,
        [this, life = std::weak_ptr<int>(delete_lifetime_)](const auto result) {
            if (!life.expired()) close(result);
        });
}

void StepEditDialog::close(const lib::DialogResult result) {
    if (!dialog_ || !dialog_->visible()) return;
    const auto gui = comm_.services.gui;
    const auto value = dialog_;
    const auto closed = closed_;
    persistDialogSize(props_, props_source_, dialog_->bounds(),
                      &Props::Variables::stepDlg);
    value->result = result;
    value->set_visible(false);
    if (gui) {
        if (value->modal) gui->end_modal(value);
        gui->close(value);
    }
    if (closed) closed(result);
}

bool StepEditDialog::apply() {
    if (deleted_ || (was_attached_ &&
        (!step_ || step_->parent() != dir_ || step_->root() != live_root_)))
        return false;
    if (readOnly_ || !dir_) return false;
    const bool attached = step_ && step_->parent();
    const auto old_name = attached ? step_->name() : String{};
    name_ = dir_->createNewStepName(flag::FlagDir::validName(name_), old_name);
    if (values_.empty()) values_ = {"0", "1"};
    syncValueCache();
    selection_ = clampSelection(selection_, values_);
    if (applying_) applying_();
    if (!attached) {
        if (!step_)
            step_ = std::make_shared<flag::Step>(name_, values_, selection_);
        else {
            (void)step_->name(name_);
            step_->setValues(values_, static_cast<std::int32_t>(selection_));
        }
        step_->initialization(initialization_);
        if (!dir_->add(step_)) return false;
    } else {
        if (!step_->name(name_)) return false;
        step_->setValues(values_, static_cast<std::int32_t>(selection_));
        step_->initialization(initialization_);
    }
    step_->expandSPChars(expand_special_characters_);
    comm_.refFlagAndStep.call({}, {step_}, {});
    if (applied_) applied_();
    was_attached_ = true;
    draft_dirty_ = false;
    if (apply_button_) apply_button_->set_enabled(false);
    return true;
}

FlagEditDialog::FlagEditDialog(Commons comm, std::shared_ptr<Summary> summary,
                               std::shared_ptr<lib::Window> shell,
                               const bool readOnly, const bool local,
                               FlagDirPtr dir, FlagPtr value)
    : comm_(std::move(comm)), summary_(std::move(summary)),
      shell_(std::move(shell)), readOnly_(readOnly), local_(local),
      dir_(std::move(dir)), flag_(std::move(value)) {
    if (flag_) {
        name_ = flag_->name();
        on_ = flag_->on();
        off_ = flag_->off();
        onOff_ = flag_->onOff();
        initialization_ = flag_->initialization();
        expand_special_characters_ = flag_->expandSPChars();
    } else {
        name_.clear();
        on_ = props_.etc.flagTrues.empty() ? String{} : props_.etc.flagTrues.front();
        off_ = props_.etc.flagFalses.empty() ? String{} : props_.etc.flagFalses.front();
        onOff_ = props_.etc.flagInitValue;
    }
    live_root_ = dir_ ? dir_->root() : FlagDirPtr{};
    was_attached_ = flag_ && flag_->parent();
    watchDeletion();
    version_token_ = comm_.refDataVersion.connect(
        [this, life = std::weak_ptr<int>(delete_lifetime_)] {
            if (!life.expired()) { refreshCompatibility(); refreshSpecialPreviews(); }
        });
    const auto refresh_preview = [this, life = std::weak_ptr<int>(delete_lifetime_)](
        const FlagList&, const StepList&, const VariantList&) {
        if (!life.expired()) refreshSpecialPreviews();
    };
    preview_token_ = comm_.refFlagAndStep.connect(refresh_preview);
    preview_delete_token_ = comm_.delFlagAndStep.connect(refresh_preview);
    preview_refresh_token_ = comm_.refSpecialPreview.connect(
        [this, life = std::weak_ptr<int>(delete_lifetime_)] {
            if (!life.expired()) refreshSpecialPreviews();
        });
}

FlagEditDialog::~FlagEditDialog() {
    comm_.refFlagAndStep.remove(preview_token_);
    comm_.delFlagAndStep.remove(preview_delete_token_);
    comm_.refSpecialPreview.remove(preview_refresh_token_);
    delete_lifetime_.reset();
    comm_.refDataVersion.remove(version_token_);
    if (auto disconnect = std::exchange(stop_watching_deletion_, {}))
        disconnect();
    retireModalVariableDialog(comm_.services.gui, dialog_);
}

FlagEditDialog::FlagEditDialog(Commons comm, Summary* summary,
                               std::shared_ptr<lib::Window> shell,
                               const bool readOnly, const bool local,
                               FlagDirPtr dir, FlagPtr value)
    : FlagEditDialog(std::move(comm), borrowSummary(summary), std::move(shell),
                     readOnly, local, std::move(dir), std::move(value)) {}

void FlagEditDialog::watchDeletion() {
    if (readOnly_) return;
    stop_watching_deletion_ = comm_.observeVariableDeletion(
        [this, life = std::weak_ptr<int>(delete_lifetime_)](const FlagList& flags, const StepList& steps, const VariantList& variants) {
            if (life.expired() || !flag_ ||
                std::find(flags.begin(), flags.end(), flag_) == flags.end()) return;
            deleted_ = true;
            draft_dirty_ = false;
            if (ok_button_) ok_button_->set_enabled(false);
            if (apply_button_) apply_button_->set_enabled(false);
            if (auto disconnect = std::exchange(stop_watching_deletion_, {}))
                disconnect();
            close(lib::DialogResult::Cancel);
        });
}

void FlagEditDialog::presentation(Props& value) {
    presentation(static_cast<const Props&>(value));
    props_source_ = &value;
}

void FlagEditDialog::presentation(const Props& value) {
    auto copied = value;
    if (copied.etc.flagDefaultsProvider) {
        const auto defaults = copied.etc.flagDefaultsProvider();
        copied.etc.flagInitValue = defaults.initial;
        copied.etc.flagTrues = defaults.trues;
        copied.etc.flagFalses = defaults.falses;
    }
    copied.synchronize();
    props_ = std::move(copied);
    props_source_ = nullptr;
    if (!flag_ && !component_) {
        on_ = props_.etc.flagTrues.empty() ? String{}
                                           : props_.etc.flagTrues.front();
        off_ = props_.etc.flagFalses.empty() ? String{}
                                              : props_.etc.flagFalses.front();
        onOff_ = props_.etc.flagInitValue;
    }
}

PanelPtr FlagEditDialog::setup(const Parent& parent) {
    if (component_) {
        if (parent) parent->add(component_);
        return component_;
    }
    component_ = makeComponent<lib::Panel>(comm_, lib::ComponentKind::Panel);
    auto layout = std::make_shared<lib::GridLayout>();
    layout->columns = 1;
    layout->spacing = 2.0F;
    layout->padding = {4.0F, 3.0F, 3.0F, 4.0F};
    component_->set_layout(layout);

    top_split_ = std::make_shared<splitpane::SplitPane>(splitpane::Horizontal);
    top_split_->setWeights(
        {props_.etc.flagTopSashL, props_.etc.flagTopSashR});
    place(top_split_, 0, 0, 1, 1, 0.0F, {367.0F, 0.0F});
    (void)top_split_->addSelectionListener([this](auto&) {
        const auto weights = top_split_->getWeights();
        if (weights.size() < 2U) return;
        props_.etc.flagTopSashL = weights[0];
        props_.etc.flagTopSashR = weights[1];
        props_.synchronize();
        if (props_source_) {
            props_source_->etc.flagTopSashL = weights[0];
            props_source_->etc.flagTopSashR = weights[1];
            props_source_->synchronize();
        }
    });
    top_split_->on_weights_changed = [this](const std::vector<float>& weights) {
        if (weights.size() < 2U || !std::isfinite(weights[0]) ||
            !std::isfinite(weights[1]))
            return;
        const auto left = static_cast<std::int32_t>(
            std::max(1.0F, std::round(weights[0])));
        const auto right = static_cast<std::int32_t>(
            std::max(1.0F, std::round(weights[1])));
        props_.etc.flagTopSashL = left;
        props_.etc.flagTopSashR = right;
        props_.synchronize();
        if (props_source_) {
            props_source_->etc.flagTopSashL = left;
            props_source_->etc.flagTopSashR = right;
            props_source_->synchronize();
        }
    };
    component_->add(top_split_);

    auto top_left = makeComponent<lib::Panel>(comm_, lib::ComponentKind::Panel);
    auto left_layout = std::make_shared<lib::GridLayout>();
    left_layout->columns = 2;
    left_layout->spacing = 5.0F;
    top_left->set_layout(left_layout);
    auto top_right = makeComponent<lib::Panel>(comm_, lib::ComponentKind::Panel);
    auto right_layout = std::make_shared<lib::GridLayout>();
    right_layout->columns = 2;
    right_layout->spacing = 5.0F;
    top_right->set_layout(right_layout);
    top_split_->add(top_left);
    top_split_->add(top_right);
    auto addTopLabel = [&](const Parent& target, String text,
                           std::size_t column) {
        auto label = makeComponent<lib::Label>(comm_, lib::ComponentKind::Label);
        label->text = std::move(text);
        place(label, 0, column);
        target->add(label);
    };

    addTopLabel(top_left, props_.msgs.flagEditName, 0);
    name_control_ = makeComponent<lib::TextEdit>(
        comm_, lib::ComponentKind::TextEdit);
    name_control_->text = name_;
    name_control_->read_only = readOnly_;
    name_control_->selection_start = 0;
    name_control_->selection_length = name_control_->text.size();
    name_control_->on_changed = [this](std::string_view text) {
        name_ = String{text};
        markModified();
    };
    place(name_control_, 0, 1, 1, 1, 1.0F,
          {std::max(1.0F, props_.etc.flagNameWidth), 24.0F});
    top_left->add(name_control_);

    addTopLabel(top_right, props_.msgs.flagInitial, 0);
    initial_control_ = makeComponent<lib::ComboBox>(
        comm_, lib::ComponentKind::ComboBox);
    initial_control_->items = {{on_, "true"}, {off_, "false"}};
    initial_control_->selected_index = onOff_ ? 0U : 1U;
    initial_control_->set_enabled(!readOnly_);
    initial_control_->on_selection_changed = [this](std::optional<std::size_t> index) {
        if (index) {
            onOff_ = *index == 0;
            markModified();
        }
    };
    place(initial_control_, 0, 1, 1, 1, 1.0F,
          {std::max(1.0F, props_.etc.flagInitWidth), 24.0F});
    top_right->add(initial_control_);

    auto separator1 = makeComponent<lib::Separator>(
        comm_, lib::ComponentKind::Separator);
    place(separator1, 1, 0, 1, 1, 0.0F, {367.0F, 1.0F});
    component_->add(separator1);

    auto values_panel = makeComponent<lib::Panel>(
        comm_, lib::ComponentKind::Panel);
    auto values_layout = std::make_shared<lib::GridLayout>();
    values_layout->columns = 2;
    values_layout->spacing = 5.0F;
    values_layout->padding = {3.0F, 42.0F, 3.0F, 42.0F};
    values_panel->set_layout(values_layout);
    // 二つの値の行をD版のようにまとめて中央寄せするが
    // 、 両編集欄を従来の高さ 43 に制限しない。
    place(values_panel, 2, 0, 1, 1, 1.0F, {367.0F, 0.0F});
    component_->add(values_panel);
    auto addValueLabel = [&](String text, std::size_t row) {
        auto label = makeComponent<lib::Label>(comm_, lib::ComponentKind::Label);
        label->text = std::move(text);
        place(label, row, 0);
        values_panel->add(label);
    };

    const auto add_unique = [](const std::shared_ptr<lib::ComboBox>& combo,
                               const String& value) {
        if (value.empty()) return;
        if (std::none_of(combo->items.begin(), combo->items.end(),
                         [&value](const auto& item) { return item.text == value; }))
            combo->items.push_back({value, value});
    };
    addValueLabel(props_.msgs.flagTrue, 0);
    on_control_ = makeComponent<lib::ComboBox>(
        comm_, lib::ComponentKind::ComboBox);
    on_control_->editable = true;
    add_unique(on_control_, on_);
    for (const auto& value : props_.etc.flagTrues) add_unique(on_control_, value);
    on_control_->edit_text = on_;
    on_control_->set_enabled(!readOnly_);
    on_control_->on_text_changed = [this](std::string_view text) {
        on_ = String{text};
        if (initial_control_ && !initial_control_->items.empty()) {
            auto items = initial_control_->items;
            items[0].text = on_;
            (void)lib::replace_combo_items(initial_control_, std::move(items),
                                           initial_control_->selected_index,
                                           initial_control_->edit_text);
        }
        markModified();
    };
    place(on_control_, 0, 1, 1, 1, 1.0F,
          {std::max(1.0F, props_.etc.flagValueWidth), 24.0F});
    values_panel->add(on_control_);

    addValueLabel(props_.msgs.flagFalse, 1);
    off_control_ = makeComponent<lib::ComboBox>(
        comm_, lib::ComponentKind::ComboBox);
    off_control_->editable = true;
    add_unique(off_control_, off_);
    for (const auto& value : props_.etc.flagFalses) add_unique(off_control_, value);
    off_control_->edit_text = off_;
    off_control_->set_enabled(!readOnly_);
    off_control_->on_text_changed = [this](std::string_view text) {
        off_ = String{text};
        if (initial_control_ && initial_control_->items.size() > 1) {
            auto items = initial_control_->items;
            items[1].text = off_;
            (void)lib::replace_combo_items(initial_control_, std::move(items),
                                           initial_control_->selected_index,
                                           initial_control_->edit_text);
        }
        markModified();
    };
    place(off_control_, 1, 1, 1, 1, 1.0F,
          {std::max(1.0F, props_.etc.flagValueWidth), 24.0F});
    values_panel->add(off_control_);

    auto separator2 = makeComponent<lib::Separator>(
        comm_, lib::ComponentKind::Separator);
    place(separator2, 3, 0, 1, 1, 0.0F, {367.0F, 1.0F});
    component_->add(separator2);

    auto bottom = makeComponent<lib::Panel>(comm_, lib::ComponentKind::Panel);
    auto bottom_layout = std::make_shared<lib::GridLayout>();
    bottom_layout->columns = 2;
    bottom_layout->spacing = 6.0F;
    bottom->set_layout(bottom_layout);
    place(bottom, 4, 0, 1, 1, 0.0F, {367.0F, 0.0F});
    component_->add(bottom);
    auto expand = makeComponent<lib::CheckBox>(comm_, lib::ComponentKind::CheckBox);
    expand_control_ = expand;
    expand->text = props_.msgs.expandSpecialCharacters;
    expand->checked = expand_special_characters_;
    expand->set_enabled(!readOnly_);
    expand->on_changed = [this](bool value) {
        expand_special_characters_ = value;
        markModified();
    };
    place(expand, 0, 0);
    bottom->add(expand);
    auto init_panel = makeInitializationPanel(
        comm_, {}, readOnly_, initialization_checks_,
        initialization_none_, initialization_, &props_,
        [this] { markModified(); });
    refreshCompatibility();
    place(init_panel, 0, 1, 1, 1, 1.0F);
    bottom->add(init_panel);
    if (!readOnly_) {
        const auto counter = dir_ ? dir_->useCounter() : comm_.useCounter;
        const auto life = std::weak_ptr<int>(delete_lifetime_);
        const auto expanded = [this, life] { return !life.expired() && expand_special_characters_; };
        special_text_menus_.push_back(attachVariableSpecialMenu(comm_, summary_, counter,
            dmenu::TMM(on_control_), life, expanded));
        special_text_menus_.push_back(attachVariableSpecialMenu(comm_, summary_, counter,
            dmenu::TMM(off_control_), life, expanded));
    }
    refreshSpecialPreviews();
    warning_control_ = makeComponent<lib::Label>(comm_, lib::ComponentKind::Label);
    warning_control_->wrap = true;
    place(warning_control_, 5, 0, 1, 1, 1.0F, {320.0F, 18.0F});
    component_->add(warning_control_);
    refreshWarning();
    if (parent) parent->add(component_);
    return component_;
}


String FlagEditDialog::specialPreview(String text) const {
    auto* prop = comm_.editorProps ? comm_.editorProps() : nullptr;
    if (!prop || !expand_special_characters_) return {};
    const auto counter = dir_ ? dir_->useCounter() : comm_.useCounter;
    return messageutils::createSPCharPreview(*prop, summary_.get(),
        variablePreviewSkin(*prop, summary_.get()), counter.get(), std::move(text), true,
        [this, counter](const String& path) -> core::msgutils::VarValue {
            const auto found = flag::findVar<flag::Flag>(summary_ ? summary_->flagDirRoot() : FlagDirPtr{}, counter, path);
            return found && found == flag_
                ? core::msgutils::VarValue{true, onOff_ ? on_ : off_, expand_special_characters_}
                : core::msgutils::VarValue{};
        });
}
// 共通のツールチップは文字列をそのまま表示するため、SWT と異なり & を二重にしない。
void FlagEditDialog::refreshSpecialPreviews() {
    refreshWarning();
    if (on_control_) on_control_->set_tooltip(specialPreview(on_control_->edit_text));
    if (off_control_) off_control_->set_tooltip(specialPreview(off_control_->edit_text));
}
String StepEditDialog::specialPreview(String text) const {
    auto* prop = comm_.editorProps ? comm_.editorProps() : nullptr;
    if (!prop || !expand_special_characters_) return {};
    const auto counter = dir_ ? dir_->useCounter() : comm_.useCounter;
    return messageutils::createSPCharPreview(*prop, summary_.get(),
        variablePreviewSkin(*prop, summary_.get()), counter.get(), std::move(text), true, {},
        [this, counter](const String& path) -> core::msgutils::VarValue {
            const auto found = flag::findVar<flag::Step>(summary_ ? summary_->flagDirRoot() : FlagDirPtr{}, counter, path);
            if (!found || found != step_) return {};
            String value = selection_ < values_.size() ? values_[selection_] : String{};
            if (const auto editor = value_editor_.lock(); editor && value_editor_row_ == selection_)
                value = editor->text;
            return {true, std::move(value), expand_special_characters_};
        });
}
void StepEditDialog::refreshSpecialPreviews() {
    refreshWarning();
    if (const auto editor = value_editor_.lock()) editor->set_tooltip(specialPreview(editor->text));
    if (values_control_ && preview_hover_row_ && *preview_hover_row_ < values_.size()) {
        String text = values_[*preview_hover_row_];
        if (const auto editor = value_editor_.lock(); editor && value_editor_row_ == *preview_hover_row_)
            text = editor->text;
        values_control_->set_tooltip(specialPreview(std::move(text)));
    }
}
void FlagEditDialog::refreshWarning() {
    showVariableWarnings(warning_control_, variableWarnings(comm_, summary_.get(), dir_, name_,
        {on_, off_}, false, local_, expand_special_characters_, initialization_, 0));
}

void FlagEditDialog::refreshCompatibility() {
    refreshWarning();
    const bool legacy = summary_ && summary_->legacy();
    if (expand_control_)
        expand_control_->set_enabled(!readOnly_ && (!legacy || expand_control_->checked));
    for (const auto& check : initialization_checks_)
        if (check) check->set_enabled(!readOnly_ && (local_ || !legacy || check->checked));
    if (initialization_none_)
        initialization_none_->set_enabled(!readOnly_ &&
            (local_ || !legacy || initialization_none_->checked));
}

void FlagEditDialog::markModified() {
    refreshCompatibility();
    refreshSpecialPreviews();
    if (!readOnly_) draft_dirty_ = true;
    const bool valid = !flag::FlagDir::validName(name_).empty();
    if (ok_button_) ok_button_->set_enabled(readOnly_ || valid);
    if (apply_button_ && !readOnly_) apply_button_->set_enabled(valid);
}

std::shared_ptr<lib::Dialog> FlagEditDialog::setupDialog(
    const std::shared_ptr<lib::Window>& owner) {
    if (dialog_) return dialog_;
    dialog_ = std::make_shared<lib::Dialog>();
    configureDialog(dialog_, owner ? owner : shell_, props_.msgs.flagDialogTitle,
                     props_.flagImage, props_.var.flagDlg, {480.0F, 260.0F}, readOnly_);
    const auto chrome = addDialogChrome(comm_, props_, dialog_, setup({}), readOnly_);
    ok_button_ = chrome.ok;
    cancel_button_ = chrome.cancel;
    apply_button_ = chrome.apply;
    ok_button_->set_enabled(readOnly_ ||
                            !flag::FlagDir::validName(name_).empty());
    ok_button_->on_click = [this] {
        if (readOnly_ || apply()) close(lib::DialogResult::OK);
    };
    if (cancel_button_)
        cancel_button_->on_click = [this] { close(lib::DialogResult::Cancel); };
    if (apply_button_)
        apply_button_->on_click = [this] { (void)apply(); };
    dialog_->on_close_requested = [this] { close(lib::DialogResult::Cancel); };
    return dialog_;
}

void FlagEditDialog::show(const std::shared_ptr<lib::Window>& owner) {
    const auto value = setupDialog(owner);
    presentVariableDialog(comm_.services.gui, value,
        [this, life = std::weak_ptr<int>(delete_lifetime_)](const auto result) {
            if (!life.expired()) close(result);
        });
}

void FlagEditDialog::close(const lib::DialogResult result) {
    if (!dialog_ || !dialog_->visible()) return;
    const auto gui = comm_.services.gui;
    const auto value = dialog_;
    const auto closed = closed_;
    persistDialogSize(props_, props_source_, dialog_->bounds(),
                      &Props::Variables::flagDlg);
    value->result = result;
    value->set_visible(false);
    if (gui) {
        if (value->modal) gui->end_modal(value);
        gui->close(value);
    }
    if (closed) closed(result);
}

bool FlagEditDialog::apply() {
    if (deleted_ || (was_attached_ &&
        (!flag_ || flag_->parent() != dir_ || flag_->root() != live_root_)))
        return false;
    if (readOnly_ || !dir_) return false;
    const bool attached = flag_ && flag_->parent();
    const auto old_name = attached ? flag_->name() : String{};
    name_ = dir_->createNewFlagName(flag::FlagDir::validName(name_), old_name);
    if (applying_) applying_();
    if (!attached) {
        if (!flag_)
            flag_ = std::make_shared<flag::Flag>(name_, on_, off_, onOff_);
        else {
            (void)flag_->name(name_);
            flag_->on(on_);
            flag_->off(off_);
            flag_->onOff(onOff_);
        }
        flag_->initialization(initialization_);
        if (!dir_->add(flag_)) return false;
    } else {
        if (!flag_->name(name_)) return false;
        flag_->on(on_);
        flag_->off(off_);
        flag_->onOff(onOff_);
        flag_->initialization(initialization_);
    }
    flag_->expandSPChars(expand_special_characters_);
    comm_.refFlagAndStep.call({flag_}, {}, {});
    if (applied_) applied_();
    was_attached_ = true;
    draft_dirty_ = false;
    if (apply_button_) apply_button_->set_enabled(false);
    return true;
}

VariantEditDialog::VariantEditDialog(
    Commons comm, std::shared_ptr<Summary> summary,
    std::shared_ptr<lib::Window> shell, const bool readOnly, const bool local,
    FlagDirPtr dir, VariantPtr value)
    : comm_(std::move(comm)), summary_(std::move(summary)),
      shell_(std::move(shell)), readOnly_(readOnly), local_(local),
      dir_(std::move(dir)), variant_(std::move(value)) {
    if (variant_) {
        name_ = variant_->name();
        value_ = expression::VariantVal(*variant_);
        initialization_ = variant_->initialization();
    } else {
        name_.clear();
        value_ = expression::VariantVal::numValue(0.0);
    }
    live_root_ = dir_ ? dir_->root() : FlagDirPtr{};
    was_attached_ = variant_ && variant_->parent();
    watchDeletion();
}

VariantEditDialog::~VariantEditDialog() {
    delete_lifetime_.reset();
    comm_.refDataVersion.remove(version_token_);
    if (auto disconnect = std::exchange(stop_watching_deletion_, {}))
        disconnect();
}

VariantEditDialog::VariantEditDialog(
    Commons comm, Summary* summary, std::shared_ptr<lib::Window> shell,
    const bool readOnly, const bool local, FlagDirPtr dir, VariantPtr value)
    : VariantEditDialog(std::move(comm), borrowSummary(summary), std::move(shell),
                        readOnly, local, std::move(dir), std::move(value)) {}

void VariantEditDialog::watchDeletion() {
    if (readOnly_) return;
    stop_watching_deletion_ = comm_.observeVariableDeletion(
        [this, life = std::weak_ptr<int>(delete_lifetime_)](const FlagList& flags, const StepList& steps, const VariantList& variants) {
            if (life.expired() || !variant_ ||
                std::find(variants.begin(), variants.end(), variant_) == variants.end()) return;
            deleted_ = true;
            draft_dirty_ = false;
            if (ok_button_) ok_button_->set_enabled(false);
            if (apply_button_) apply_button_->set_enabled(false);
            if (auto disconnect = std::exchange(stop_watching_deletion_, {}))
                disconnect();
            close(lib::DialogResult::Cancel);
        });
}

void VariantEditDialog::presentation(Props& value) {
    presentation(static_cast<const Props&>(value));
    props_source_ = &value;
}

void VariantEditDialog::presentation(const Props& value) {
    auto copied = value;
    copied.synchronize();
    props_ = std::move(copied);
    props_source_ = nullptr;
}

types::VariantType VariantEditDialog::type() const noexcept { return value_.type; }

expression::VariantVal VariantEditDialog::value() const { return value_; }

void VariantEditDialog::value(expression::VariantVal value) {
    if (value.valid) value_ = std::move(value);
}

PanelPtr VariantEditDialog::setup(const Parent& parent) {
    if (component_) {
        if (parent) parent->add(component_);
        return component_;
    }
    component_ = makeComponent<lib::Panel>(comm_, lib::ComponentKind::Panel);
    auto layout = std::make_shared<lib::GridLayout>();
    layout->columns = 1;
    layout->spacing = 3.0F;
    layout->padding = {5.0F, 4.0F, 4.0F, 5.0F};
    component_->set_layout(layout);

    auto name_panel = makeComponent<lib::Panel>(comm_, lib::ComponentKind::Panel);
    auto name_layout = std::make_shared<lib::GridLayout>();
    name_layout->columns = 2;
    name_layout->spacing = 5.0F;
    name_panel->set_layout(name_layout);
    place(name_panel, 0, 0, 1, 1, 0.0F, {322.0F, 0.0F});
    component_->add(name_panel);
    auto name_label = makeComponent<lib::Label>(comm_, lib::ComponentKind::Label);
    name_label->text = props_.msgs.variantName;
    place(name_label, 0, 0);
    name_panel->add(name_label);
    name_control_ = makeComponent<lib::TextEdit>(
        comm_, lib::ComponentKind::TextEdit);
    name_control_->text = name_;
    name_control_->read_only = readOnly_;
    name_control_->selection_start = 0;
    name_control_->selection_length = name_control_->text.size();
    name_control_->on_changed = [this](std::string_view text) {
        name_ = String{text};
        markModified();
    };
    place(name_control_, 0, 1, 1, 1, 1.0F, {270.0F, 24.0F});
    name_panel->add(name_control_);

    auto separator1 = makeComponent<lib::Separator>(
        comm_, lib::ComponentKind::Separator);
    place(separator1, 1, 0, 1, 1, 0.0F, {322.0F, 1.0F});
    component_->add(separator1);

    auto values_panel = makeComponent<lib::Panel>(
        comm_, lib::ComponentKind::Panel);
    auto values_layout = std::make_shared<lib::GridLayout>();
    values_layout->columns = 2;
    values_layout->spacing = 5.0F;
    values_panel->set_layout(values_layout);
    // 現在のフォントとUI倍率で、三つすべての型・値の行が収まるようにする。
    place(values_panel, 2, 0, 1, 1, 1.0F, {322.0F, 0.0F});
    component_->add(values_panel);

    number_radio_ = makeComponent<lib::RadioButton>(
        comm_, lib::ComponentKind::RadioButton);
    string_radio_ = makeComponent<lib::RadioButton>(
        comm_, lib::ComponentKind::RadioButton);
    boolean_radio_ = makeComponent<lib::RadioButton>(
        comm_, lib::ComponentKind::RadioButton);
    number_radio_->text = props_.msgs.numberValue;
    string_radio_->text = props_.msgs.stringValue;
    boolean_radio_->text = props_.msgs.booleanValue;
    number_radio_->group = string_radio_->group = boolean_radio_->group =
        "variant-type";
    number_radio_->checked = value_.type == types::VariantType::Number;
    string_radio_->checked = value_.type == types::VariantType::String;
    boolean_radio_->checked = value_.type == types::VariantType::Boolean;
    if (!number_radio_->checked && !string_radio_->checked &&
        !boolean_radio_->checked)
        number_radio_->checked = true;
    number_radio_->set_enabled(!readOnly_);
    string_radio_->set_enabled(!readOnly_);
    boolean_radio_->set_enabled(!readOnly_);

    // D版と共通の数値編集欄を使い、キー入力と貼り付けに同じフィルターを適用する。
    customtext::Commons number_comm;
    number_comm.services = comm_.services;
    number_comm.refreshToolBar = comm_.refreshToolBar;
    number_comm.undoMaxEtc = props_.etc.undoMaxEtc;
    number_control_ = customtext::createNumberEditor(
        std::move(number_comm), static_cast<customtext::Parent>(nullptr), 0);
    number_control_->text = value_.type == types::VariantType::Number
                                ? expression::variantValueToText(value_)
                                : String{"0"};
    number_control_->read_only = readOnly_;
    string_control_ = makeComponent<lib::TextEdit>(
        comm_, lib::ComponentKind::TextEdit);
    string_control_->text = value_.type == types::VariantType::String
                                ? value_.strVal
                                : String{};
    string_control_->read_only = readOnly_;
    boolean_control_ = makeComponent<lib::ComboBox>(
        comm_, lib::ComponentKind::ComboBox);
    boolean_control_->items = {{"TRUE", "true"}, {"FALSE", "false"}};
    boolean_control_->selected_index =
        value_.type == types::VariantType::Boolean && !value_.boolVal ? 1U : 0U;

    const auto update_enabled = [this] {
        number_control_->set_enabled(!readOnly_ && number_radio_->checked);
        string_control_->set_enabled(!readOnly_ && string_radio_->checked);
        boolean_control_->set_enabled(!readOnly_ && boolean_radio_->checked);
    };
    number_radio_->on_changed = [this, update_enabled](bool checked) {
        if (!checked) return;
        string_radio_->checked = false;
        boolean_radio_->checked = false;
        const auto parsed = numericEditorValue(number_control_->text);
        if (parsed.valid && parsed.type == types::VariantType::Number)
            value_ = parsed;
        update_enabled();
        refreshWarning();
        markModified();
    };
    string_radio_->on_changed = [this, update_enabled](bool checked) {
        if (!checked) return;
        number_radio_->checked = false;
        boolean_radio_->checked = false;
        value_ = expression::VariantVal::strValue(string_control_->text);
        update_enabled();
        refreshWarning();
        markModified();
    };
    boolean_radio_->on_changed = [this, update_enabled](bool checked) {
        if (!checked) return;
        number_radio_->checked = false;
        string_radio_->checked = false;
        value_ = expression::VariantVal::boolValue(
            boolean_control_->selected_index.value_or(0) == 0);
        update_enabled();
        refreshWarning();
        markModified();
    };
    number_control_->on_changed = [this](std::string_view number_value) {
        if (number_radio_->checked) {
            const auto parsed = numericEditorValue(number_value);
            if (parsed.valid && parsed.type == types::VariantType::Number)
                value_ = parsed;
        }
        refreshWarning();
        markModified();
    };
    string_control_->on_changed = [this](std::string_view string_value) {
        if (string_radio_->checked)
            value_ = expression::VariantVal::strValue(String{string_value});
        markModified();
    };
    boolean_control_->on_selection_changed = [this](std::optional<std::size_t> index) {
        if (boolean_radio_->checked && index)
            value_ = expression::VariantVal::boolValue(*index == 0);
        markModified();
    };
    place(number_radio_, 0, 0, 1, 1, 0.0F, {69.0F, 24.0F});
    place(number_control_, 0, 1, 1, 1, 0.0F, {118.0F, 24.0F});
    place(string_radio_, 1, 0, 1, 1, 0.0F, {69.0F, 24.0F});
    place(string_control_, 1, 1, 1, 1, 1.0F, {239.0F, 24.0F});
    place(boolean_radio_, 2, 0, 1, 1, 0.0F, {69.0F, 24.0F});
    place(boolean_control_, 2, 1, 1, 1, 0.0F, {61.0F, 24.0F});
    values_panel->add(number_radio_);
    values_panel->add(number_control_);
    values_panel->add(string_radio_);
    values_panel->add(string_control_);
    values_panel->add(boolean_radio_);
    values_panel->add(boolean_control_);
    update_enabled();

    warning_control_ = makeComponent<lib::Label>(comm_, lib::ComponentKind::Label);
    place(warning_control_, 3, 0, 1, 1, 1.0F, {322.0F, 18.0F});
    warning_control_->wrap = true;
    {
        auto item = warning_control_->layout_item();
        // 警告は折り返し可能な状態表示行であり、一続きの文言に合
        // わせてダイアログ全体を広げるものではない。 D版の表示
        // 幅を維持し、翻訳文が二行になる場合は行の高さを増やす。
        item.constrain_minimum_to_preferred = true;
        warning_control_->set_layout_item(item);
    }
    warning_control_->set_visible(false);
    component_->add(warning_control_);

    auto separator2 = makeComponent<lib::Separator>(
        comm_, lib::ComponentKind::Separator);
    place(separator2, 4, 0, 1, 1, 0.0F, {322.0F, 1.0F});
    component_->add(separator2);
    auto init_panel = makeInitializationPanel(
        comm_, {}, readOnly_, initialization_checks_,
        initialization_none_, initialization_, &props_,
        [this] { markModified(); });
    // チェックボックスの二行を両方含める。固定の高さでは最後のタイミングの選択肢が隠れてしまう。
    place(init_panel, 5, 0, 1, 1, 0.0F, {322.0F, 0.0F});
    component_->add(init_panel);
    if (!readOnly_) {
        // D版の編集可能な VariantEditDialog と同様、データバージョンが変わったら警告を更新する。
        version_token_ = comm_.refDataVersion.connect(
            [this, life = std::weak_ptr<int>(delete_lifetime_)] {
                if (!life.expired()) refreshWarning();
            });
    }
    refreshWarning();
    if (parent) parent->add(component_);
    return component_;
}

void VariantEditDialog::refreshWarning() {
    warnings_.clear();
    auto* prop = comm_.editorProps ? comm_.editorProps() : nullptr;
    if (prop && !prop->isTargetVersion(summary_.get(), "4"))
        warnings_.push_back(prop->requiredMsgs().warningVariant.value);
    if (number_radio_ && number_radio_->checked && number_control_) {
        auto text = number_control_->text;
        text.erase(std::remove_if(text.begin(), text.end(), [](const char value) {
            return value == '.' || value == '-' ||
                   std::isspace(static_cast<unsigned char>(value));
        }), text.end());
        text.erase(text.begin(), std::find_if(text.begin(), text.end(),
                                              [](const char value) {
                                                  return value != '0';
                                              }));
        if (text.size() > props_.etc.limitForNumberOfVariant)
            warnings_.push_back(props_.msgs.warningLimitForNumberOfVariant);
    }
    if (prop) {
        auto append = [&](const auto& values) { warnings_.insert(warnings_.end(), values.begin(), values.end()); };
        append(warning::sjisWarnings(prop->parent(), summary_.get(), name_, prop->requiredMsgs().variantName.value));
        append(warning::sjisWarnings(prop->parent(), summary_.get(),
            string_control_ ? string_control_->text : String{}, prop->requiredMsgs().stringValue.value));
    }
    showVariableWarnings(warning_control_, warnings_);
}

bool VariantEditDialog::validDraft() const {
    if (deleted_ || flag::FlagDir::validName(name_).empty()) return false;
    if (number_radio_ && number_radio_->checked && number_control_) {
        const auto value = numericEditorValue(number_control_->text);
        return value.valid && value.type == types::VariantType::Number;
    }
    return true;
}

void VariantEditDialog::markModified() {
    refreshWarning();
    if (!readOnly_) draft_dirty_ = true;
    const bool valid = validDraft();
    if (ok_button_) ok_button_->set_enabled(readOnly_ || valid);
    if (apply_button_ && !readOnly_) apply_button_->set_enabled(valid);
}

std::shared_ptr<lib::Dialog> VariantEditDialog::setupDialog(
    const std::shared_ptr<lib::Window>& owner) {
    if (dialog_) return dialog_;
    dialog_ = std::make_shared<lib::Dialog>();
    configureDialog(dialog_, owner ? owner : shell_, props_.msgs.variantDialogTitle,
                     props_.variantImage, props_.var.variantDlg, {400.0F, 320.0F});
    const auto chrome = addDialogChrome(comm_, props_, dialog_, setup({}), readOnly_);
    ok_button_ = chrome.ok;
    cancel_button_ = chrome.cancel;
    apply_button_ = chrome.apply;
    ok_button_->set_enabled(readOnly_ || validDraft());
    if (apply_button_) apply_button_->set_enabled(!readOnly_ && draft_dirty_ && validDraft());
    ok_button_->on_click = [this] {
        if (readOnly_ || apply()) close(lib::DialogResult::OK);
    };
    if (cancel_button_)
        cancel_button_->on_click = [this] { close(lib::DialogResult::Cancel); };
    if (apply_button_)
        apply_button_->on_click = [this] { (void)apply(); };
    dialog_->on_close_requested = [this] { close(lib::DialogResult::Cancel); };
    return dialog_;
}

void VariantEditDialog::show(const std::shared_ptr<lib::Window>& owner) {
    const auto value = setupDialog(owner);
    value->set_visible(true);
    if (comm_.services.gui) comm_.services.gui->show(value);
}

void VariantEditDialog::close(const lib::DialogResult result) {
    if (!dialog_ || !dialog_->visible()) return;
    persistDialogSize(props_, props_source_, dialog_->bounds(),
                      &Props::Variables::variantDlg);
    dialog_->result = result;
    dialog_->set_visible(false);
    if (comm_.services.gui) comm_.services.gui->close(dialog_);
    if (closed_) closed_(result);
}

bool VariantEditDialog::apply() {
    if (deleted_ || (was_attached_ &&
        (!variant_ || variant_->parent() != dir_ || variant_->root() != live_root_)))
        return false;
    if (readOnly_ || !dir_ || !value_.valid) return false;
    if (number_radio_ && number_radio_->checked && number_control_) {
        const auto parsed = numericEditorValue(number_control_->text);
        if (!parsed.valid || parsed.type != types::VariantType::Number)
            return false;
        value_ = parsed;
    } else if (string_radio_ && string_radio_->checked && string_control_) {
        value_ = expression::VariantVal::strValue(string_control_->text);
    } else if (boolean_radio_ && boolean_radio_->checked && boolean_control_) {
        value_ = expression::VariantVal::boolValue(
            boolean_control_->selected_index.value_or(0) == 0);
    }
    const bool attached = variant_ && variant_->parent();
    const auto old_name = attached ? variant_->name() : String{};
    name_ = dir_->createNewVariantName(flag::FlagDir::validName(name_), old_name);
    if (applying_) applying_();
    if (!attached) {
        switch (value_.type) {
        case types::VariantType::Number:
            if (!variant_)
                variant_ = std::make_shared<flag::Variant>(name_, value_.numVal);
            else {
                (void)variant_->name(name_);
                variant_->value(value_.numVal);
            }
            break;
        case types::VariantType::String:
            if (!variant_)
                variant_ = std::make_shared<flag::Variant>(name_, value_.strVal);
            else {
                (void)variant_->name(name_);
                variant_->value(value_.strVal);
            }
            break;
        case types::VariantType::Boolean:
            if (!variant_)
                variant_ = std::make_shared<flag::Variant>(name_, value_.boolVal);
            else {
                (void)variant_->name(name_);
                variant_->value(value_.boolVal);
            }
            break;
        case types::VariantType::List:
            if (!variant_)
                variant_ = std::make_shared<flag::Variant>(name_, value_.listVal);
            else {
                (void)variant_->name(name_);
                variant_->value(value_.listVal);
            }
            break;
        case types::VariantType::Structure:
            if (!variant_)
                variant_ = std::make_shared<flag::Variant>(
                    name_, value_.structName, value_.structVal);
            else {
                (void)variant_->name(name_);
                variant_->setStructValue(value_.structName, value_.structVal);
            }
            break;
        }
        variant_->initialization(initialization_);
        if (!dir_->add(variant_)) return false;
    } else {
        if (!variant_->name(name_)) return false;
        switch (value_.type) {
        case types::VariantType::Number: variant_->value(value_.numVal); break;
        case types::VariantType::String: variant_->value(value_.strVal); break;
        case types::VariantType::Boolean: variant_->value(value_.boolVal); break;
        case types::VariantType::List: variant_->value(value_.listVal); break;
        case types::VariantType::Structure:
            variant_->setStructValue(value_.structName, value_.structVal);
            break;
        }
        variant_->initialization(initialization_);
    }
    comm_.refFlagAndStep.call({}, {}, {variant_});
    if (applied_) applied_();
    was_attached_ = true;
    draft_dirty_ = false;
    if (apply_button_) apply_button_->set_enabled(false);
    return true;
}

FlagTable::FlagTable(Commons comm, Props prop, UndoManager& undo,
                     const bool local, const bool readOnly)
    : comm_(comm), prop_(prop), undo_(&undo),
      impl_(std::move(comm), std::move(prop), undo, local, readOnly) {
    createToken_ = impl_.createEvent.connect([this] { createEvent.call(); });
    deleteToken_ = impl_.deleteEvent.connect([this] { deleteEvent.call(); });
}

FlagTable::~FlagTable() {
    impl_.createEvent.remove(createToken_);
    impl_.deleteEvent.remove(deleteToken_);
}

PanelPtr FlagTable::createControl(const Parent& parent,
                                  const Parent& incSearchParent,
                                  lib::Action gotFocus) {
    return impl_.createControl(parent, incSearchParent, std::move(gotFocus));
}

void FlagTable::updateIncSearchParent(const Parent& parent) {
    impl_.updateIncSearchParent(parent);
}

PanelPtr FlagTable::widget() const noexcept { return impl_.widget(); }

const std::shared_ptr<lib::TableView>& FlagTable::table() const noexcept {
    return impl_.table();
}

void FlagTable::syncDragSelection() const {
    dragFlags_.clear();
    dragSteps_.clear();
    dragVariants_.clear();
    for (const auto& value : impl_.selectionFlags()) {
        if (value && value->parent())
            dragFlags_[value->parent()->indexOf(*value)] = value;
    }
    for (const auto& value : impl_.selectionSteps()) {
        if (value && value->parent())
            dragSteps_[value->parent()->indexOf(*value)] = value;
    }
    for (const auto& value : impl_.selectionVariants()) {
        if (value && value->parent())
            dragVariants_[value->parent()->indexOf(*value)] = value;
    }
}

std::unordered_map<std::ptrdiff_t, FlagPtr> FlagTable::dragFlags() const {
    syncDragSelection();
    return dragFlags_;
}

std::unordered_map<std::ptrdiff_t, StepPtr> FlagTable::dragSteps() const {
    syncDragSelection();
    return dragSteps_;
}

std::unordered_map<std::ptrdiff_t, VariantPtr> FlagTable::dragVariants() const {
    syncDragSelection();
    return dragVariants_;
}

const String& FlagTable::statusLine() const noexcept { return impl_.statusLine(); }

void FlagTable::refresh() { impl_.refresh(); }

void FlagTable::refresh(const std::vector<VariableObject>& selected) {
    impl_.refresh(selected);
}

const std::vector<std::size_t>& FlagTable::selected() const noexcept {
    return impl_.selected();
}

std::vector<std::size_t> FlagTable::selectedItems(
    FlagList& flags, StepList& steps, VariantList& variants) const {
    return impl_.selectedItems(flags, steps, variants);
}

FlagList FlagTable::selectionFlags() const { return impl_.selectionFlags(); }

StepList FlagTable::selectionSteps() const { return impl_.selectionSteps(); }

VariantList FlagTable::selectionVariants() const {
    return impl_.selectionVariants();
}

std::vector<std::ptrdiff_t> FlagTable::selectionFlagIndices() const {
    return impl_.selectionFlagIndices();
}

std::vector<std::ptrdiff_t> FlagTable::selectionStepIndices() const {
    return impl_.selectionStepIndices();
}

std::vector<std::ptrdiff_t> FlagTable::selectionVariantIndices() const {
    return impl_.selectionVariantIndices();
}

void FlagTable::createFlag() { impl_.createFlag(); }

void FlagTable::createStep() { impl_.createStep(); }

void FlagTable::createVariant() { impl_.createVariant(); }

void FlagTable::edit() { impl_.edit(); }

bool FlagTable::canEdit() const noexcept { return impl_.canEdit(); }

void FlagTable::edit(const FlagPtr& value) { impl_.edit(value); }

void FlagTable::edit(const StepPtr& value) { impl_.edit(value); }

void FlagTable::edit(const VariantPtr& value) { impl_.edit(value); }

void FlagTable::setDir(const FlagDirPtr& dir, const bool forceRefresh) {
    impl_.setDir(dir, forceRefresh);
}

FlagDirPtr FlagTable::dir() const noexcept { return impl_.dir(); }

void FlagTable::select(const FlagPtr& value, const bool deselect) {
    impl_.select(value, deselect);
}

void FlagTable::select(const StepPtr& value, const bool deselect) {
    impl_.select(value, deselect);
}

void FlagTable::select(const VariantPtr& value, const bool deselect) {
    impl_.select(value, deselect);
}

void FlagTable::deselectAll() { impl_.deselectAll(); }

void FlagTable::selectAll() {
    for (const auto& row : impl_.rows()) {
        std::visit(
            [this](const auto& value) {
                using Value = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Value, FlagPtr>) impl_.select(value);
                else if constexpr (std::is_same_v<Value, StepPtr>) impl_.select(value);
                else impl_.select(value);
            },
            row.object);
    }
    flagsSelected();
}

bool FlagTable::canCopyVariablePath() const noexcept {
    return impl_.canCopyVariablePath();
}

void FlagTable::writeClipboard(const transfer::ClipboardData& data) {
    fallbackClipboard_ = data;
    if (const auto clipboard = comm_.clipboard()) clipboard->write(data);
}

transfer::ClipboardData FlagTable::readClipboard() const {
    if (const auto clipboard = comm_.clipboard()) return transfer::ClipData(*clipboard).getContents();
    return fallbackClipboard_.value_or(transfer::ClipboardData{});
}

void FlagTable::writeXML(const StringView xml) {
    if (xml.empty()) return;
    const auto xmlCopy = comm_.xmlCopyProvider && comm_.xmlCopyProvider();
    writeClipboard(xmlCopy ? transfer::makeTextPayload(xml) : transfer::makeXMLPayload(xml));
}

bool FlagTable::readXML(String& xml) const {
    const auto data = readClipboard();
    return transfer::extractXML(data, xml);
}

void FlagTable::copyVariablePath() {
    if (!canCopyVariablePath()) return;
    std::vector<String> paths;
    for (const auto& row : impl_.rows()) {
        if (!row.selected) continue;
        paths.push_back(pathOf(row.object));
    }
    transfer::ClipboardData data;
    data.text = joinPaths(paths);
    writeClipboard(data);
    if (comm_.refreshToolBar) comm_.refreshToolBar();
}

void FlagTable::dispose() noexcept { impl_.dispose(); }

void FlagTable::useCounter(const UseCounterPtr& value) {
    impl_.useCounter(value);
}

UseCounterPtr FlagTable::useCounter() const noexcept {
    return impl_.useCounter();
}

String FlagTable::selectedXML() const {
    const auto directory = dir();
    if (!directory || selected().empty()) return {};
    FlagList flags;
    StepList steps;
    VariantList variants;
    (void)selectedItems(flags, steps, variants);
    return flag::getXML(directory,
                        std::span<const FlagPtr>(flags.data(), flags.size()),
                        std::span<const StepPtr>(steps.data(), steps.size()),
                        std::span<const VariantPtr>(variants.data(), variants.size()));
}

void FlagTable::copy(SelectionEvent& event) {
    const auto xml = selectedXML();
    if (xml.empty()) return;
    writeXML(xml);
    event.handled = true;
    if (comm_.refreshToolBar) comm_.refreshToolBar();
}

void FlagTable::cut(SelectionEvent& event) {
    if (impl_.readOnly()) return;
    copy(event);
    if (event.handled) impl_.del(event);
}

bool FlagTable::pasteXML(const StringView xml) {
    // 取り消し・やり直し用の変更スナップショットはペインの実装が管理する
    // 。 そこを迂回すると、貼り付けたステップが履歴に入らず、次の取り
    // 消しで 古い表の選択状態に対して以前のコマンドを復元してしまう。
    return impl_.pasteXML(xml);
}

void FlagTable::paste(SelectionEvent& event) {
    if (impl_.readOnly()) return;
    String xml;
    if (!readXML(xml) || !pasteXML(xml)) return;
    event.handled = true;
}

void FlagTable::del(SelectionEvent& event) { impl_.del(event); }

void FlagTable::clone(SelectionEvent& event) {
    if (impl_.readOnly()) return;
    // D版と同様、複製には内部のクリップボードメモリを使い、外部の転送内容を変更しない。
    const auto xml = selectedXML();
    event.handled = !xml.empty() && pasteXML(xml);
}

bool FlagTable::canDoTCPD() const noexcept { return impl_.canDoTCPD(); }

bool FlagTable::canDoT() const noexcept { return impl_.canDoT(); }

bool FlagTable::canDoC() const noexcept { return impl_.canDoC(); }

bool FlagTable::canDoP() const noexcept {
    if (impl_.readOnly() || !dir()) return false;
    const auto summary = comm_.summary.lock();
    if (!summary || (impl_.local() && summary->legacy())) return false;
    String xml;
    if (!readXML(xml) || xml.empty()) return false;
    try {
        return core::xml::XNode::parse(xml).name() ==
               flag::XML_ROOT_FLAGS_AND_STEPS;
    } catch (...) {
        return false;
    }
}

bool FlagTable::canDoD() const noexcept { return impl_.canDoD(); }

bool FlagTable::canDoClone() const noexcept {
    return impl_.canDoClone();
}

void FlagTable::undo() { impl_.undo(); }

void FlagTable::redo() { impl_.redo(); }

void FlagTable::cancelEdit() noexcept { impl_.cancelEdit(); }

void FlagTable::enterEdit() noexcept { impl_.enterEdit(); }

void FlagTable::replaceID() { impl_.replaceID(); }

bool FlagTable::canReplaceID() const noexcept { return impl_.canReplaceID(); }

bool FlagTable::openCWXPath(String path, const bool shellActivate) {
    return impl_.openCWXPath(std::move(path), shellActivate);
}

std::vector<String> FlagTable::openedCWXPath() const { return impl_.openedCWXPath(); }

bool FlagTable::canWriteComment() const noexcept {
    return !impl_.readOnly() && !impl_.selected().empty();
}

void FlagTable::applyComment(const String& value) {
    if (!canWriteComment()) return;
    if (undo_ && dir()) undo_->beginSnapshot(dir()->root());
    FlagList flags;
    StepList steps;
    VariantList variants;
    (void)selectedItems(flags, steps, variants);
    for (const auto& item : flags)
        if (item) item->comment(value);
    for (const auto& item : steps)
        if (item) item->comment(value);
    for (const auto& item : variants)
        if (item) item->comment(value);
    if (undo_ && dir()) undo_->commitSnapshot(dir()->root());
    comm_.refFlagAndStep.call(flags, steps, variants);
    refresh();
    if (comm_.refreshToolBar) comm_.refreshToolBar();
}

void FlagTable::writeComment() { applyComment(commentText_); }

void FlagTable::notifyCreate() { createEvent.call(); }

void FlagTable::notifyDelete() { deleteEvent.call(); }

void FlagTable::notifyRefresh() {
    refresh();
    comm_.refUseCount.call();
    if (comm_.refreshToolBar) comm_.refreshToolBar();
}

String FlagTable::eventTreeXML(const event::CType flagType,
                               const event::CType stepType,
                               const event::CType variantType,
                               const bool flagValue,
                               const std::uint32_t stepValue,
                               const bool useValues) const {
    event::ContentPtr root;
    event::ContentPtr tail;
    const auto append = [&root, &tail](const event::ContentPtr& value) {
        if (!value) return;
        if (!root) root = value;
        else tail->add(value);
        tail = value;
    };
    if (flagType != event::CType::Start) {
        for (const auto& value : selectionFlags()) {
            if (!value) continue;
            auto content = std::make_shared<event::Content>(flagType);
            content->flag(value->path());
            if (useValues) content->flagValue(flagValue);
            append(content);
        }
    }
    if (stepType != event::CType::Start) {
        for (const auto& value : selectionSteps()) {
            if (!value) continue;
            auto content = std::make_shared<event::Content>(stepType);
            content->step(value->path());
            if (useValues) content->stepValue(static_cast<int>(stepValue));
            append(content);
        }
    }
    if (variantType != event::CType::Start) {
        for (const auto& value : selectionVariants()) {
            if (!value) continue;
            auto content = std::make_shared<event::Content>(variantType);
            content->variant(value->path());
            if (useValues)
                content->expression(
                    expression::variantValueToText(expression::VariantVal(*value)));
            append(content);
        }
    }
    return root ? root->toXML() : String{};
}

void FlagTable::copyFlagTree(const bool onOff) {
    const auto xml = eventTreeXML(event::CType::SetFlag, event::CType::Start,
                                  event::CType::Start, onOff, 0, true);
    if (!xml.empty()) writeXML(xml);
}

void FlagTable::copyStepTree(const std::uint32_t value) {
    const auto xml = eventTreeXML(event::CType::Start, event::CType::SetStep,
                                  event::CType::Start, false, value, true);
    if (!xml.empty()) writeXML(xml);
}

void FlagTable::copyInitTree() {
    const auto xml = eventTreeXML(event::CType::SetFlag, event::CType::SetStep,
                                  event::CType::SetVariant, false, 0, true);
    if (!xml.empty()) writeXML(xml);
}

void FlagTable::copyFlagReverseTree() {
    const auto xml = eventTreeXML(event::CType::ReverseFlag, event::CType::Start,
                                  event::CType::Start, false, 0, false);
    if (!xml.empty()) writeXML(xml);
}

void FlagTable::copyStepUpTree() {
    const auto xml = eventTreeXML(event::CType::Start, event::CType::SetStepUp,
                                  event::CType::Start, false, 0, false);
    if (!xml.empty()) writeXML(xml);
}

void FlagTable::copyStepDownTree() {
    const auto xml = eventTreeXML(event::CType::Start, event::CType::SetStepDown,
                                  event::CType::Start, false, 0, false);
    if (!xml.empty()) writeXML(xml);
}

void FlagTable::setSearchText(String value) { impl_.setSearchText(std::move(value)); }

const String& FlagTable::searchText() const noexcept { return impl_.searchText(); }

const std::vector<VariableRow>& FlagTable::rows() const noexcept { return impl_.rows(); }

const VariableObject& FlagTable::editing() const noexcept { return impl_.editing(); }

void FlagTable::flagsSelected() {
    refresh();
    if (comm_.refreshToolBar) comm_.refreshToolBar();
}

void FlagTable::incSearch() {
    if (impl_.rows().empty()) return;
    setSearchText(searchText());
}

void FlagTable::selectIndices(const std::vector<std::ptrdiff_t>& indices,
                              const VariableKind kind) {
    const auto directory = dir();
    if (!directory) return;
    for (const auto index : indices) {
        if (index < 0) continue;
        const auto i = static_cast<std::size_t>(index);
        if (kind == VariableKind::Flag && i < directory->flags().size())
            select(directory->flags()[i]);
        else if (kind == VariableKind::Step && i < directory->steps().size())
            select(directory->steps()[i]);
        else if (kind == VariableKind::Variant && i < directory->variants().size())
            select(directory->variants()[i]);
    }
}

void FlagTable::selectFlagIndices(const std::vector<std::ptrdiff_t>& indices) {
    selectIndices(indices, VariableKind::Flag);
}

void FlagTable::selectStepIndices(const std::vector<std::ptrdiff_t>& indices) {
    selectIndices(indices, VariableKind::Step);
}

void FlagTable::selectVariantIndices(const std::vector<std::ptrdiff_t>& indices) {
    selectIndices(indices, VariableKind::Variant);
}

bool FlagTable::renameSelected(String value) { return impl_.renameSelected(std::move(value)); }

bool FlagTable::setFlagState(const FlagPtr& value, const bool on) {
    return impl_.setFlagState(value, on);
}

bool FlagTable::setStepSelection(const StepPtr& value,
                                 const std::uint32_t selection) {
    return impl_.setStepSelection(value, selection);
}

bool FlagTable::setVariantValue(const VariantPtr& value,
                                const expression::VariantVal& valueData) {
    return impl_.setVariantValue(value, valueData);
}

std::ptrdiff_t FlagTable::indexOf(const FlagDirPtr& directory,
                                  const CwxPathPtr& value) {
    if (!directory || !value) return -1;
    std::ptrdiff_t index = 0;
    for (const auto& item : directory->variants()) {
        if (item.get() == value.get()) return index;
        ++index;
    }
    for (const auto& item : directory->steps()) {
        if (item.get() == value.get()) return index;
        ++index;
    }
    for (const auto& item : directory->flags()) {
        if (item.get() == value.get()) return index;
        ++index;
    }
    return -1;
}

CwxPathPtr FlagTable::fromIndex(const FlagDirPtr& directory,
                                const std::ptrdiff_t index) {
    if (!directory || index < 0) return {};
    auto remaining = static_cast<std::size_t>(index);
    if (remaining < directory->variants().size())
        return std::static_pointer_cast<path::CWXPath>(directory->variants()[remaining]);
    remaining -= directory->variants().size();
    if (remaining < directory->steps().size())
        return std::static_pointer_cast<path::CWXPath>(directory->steps()[remaining]);
    remaining -= directory->steps().size();
    if (remaining < directory->flags().size())
        return std::static_pointer_cast<path::CWXPath>(directory->flags()[remaining]);
    return {};
}

std::ptrdiff_t FlagTable::toFlagIndex(const FlagDirPtr& directory,
                                      const std::ptrdiff_t index) {
    return directory ? index - static_cast<std::ptrdiff_t>(
                                   directory->variants().size() +
                                   directory->steps().size())
                     : -1;
}

std::ptrdiff_t FlagTable::toStepIndex(const FlagDirPtr& directory,
                                      const std::ptrdiff_t index) {
    return directory ? index - static_cast<std::ptrdiff_t>(directory->variants().size())
                     : -1;
}

std::ptrdiff_t FlagTable::toVariantIndex(const FlagDirPtr&,
                                         const std::ptrdiff_t index) {
    return index;
}

std::vector<types::VariableInitialization>
FlagTable::selectableInitialization(const bool) {
    return {types::VariableInitialization::BattleStart,
            types::VariableInitialization::BattleEnd,
            types::VariableInitialization::EventExit,
            types::VariableInitialization::Leave,
            types::VariableInitialization::None};
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::flagtable
