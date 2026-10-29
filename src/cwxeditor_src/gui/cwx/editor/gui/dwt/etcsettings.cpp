#include "cwxeditor_src/gui/cwx/editor/gui/dwt/etcsettings.hpp"
// UIの実装は一つに統一し、
// 別経路の実装を設けない。

#include "cwxeditor_src/core/cwx/settings.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"
#include "cwxeditor_src/lib/ui_layout.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <type_traits>
#include <utility>
#include <variant>

namespace cwxeditor::cwx::editor::gui::dwt::etcsettings {

namespace {

using CoreProps = EtcSettings::Props;
using BoolProperty =
    ::cwxeditor::cwx::settings::Prop<bool, false, false> CoreProps::*;
using IntProperty =
    ::cwxeditor::cwx::settings::Prop<std::int32_t, false, false> CoreProps::*;
using Property = std::variant<BoolProperty, IntProperty>;

struct SettingDescriptor {
    const char* key;
    const char* category;
    Property property;
};

// この表はetcsettings.dと同じ順に保つ。D
// はOSXのXOR選択肢を省くが、共通モデルには残す。
// 他のバックエンドでは意味があり、表示側で隠せるため。
const SettingDescriptor kSettings[] = {
    {"showImagePreview", "common", BoolProperty{&CoreProps::showImagePreview}},
    {"maskCardImagePreview", "common", BoolProperty{&CoreProps::maskCardImagePreview}},
    {"switchTabWheel", "common", BoolProperty{&CoreProps::switchTabWheel}},
    {"closeTabWithMiddleClick", "common", BoolProperty{&CoreProps::closeTabWithMiddleClick}},
    {"openTabAtRightOfCurrentTab", "common", BoolProperty{&CoreProps::openTabAtRightOfCurrentTab}},
    {"showCloseButtonAllTab", "common", BoolProperty{&CoreProps::showCloseButtonAllTab}},
    {"comboListVisible", "common", BoolProperty{&CoreProps::comboListVisible}},
    {"showCurrentValueOnTopAlways", "common", BoolProperty{&CoreProps::showCurrentValueOnTopAlways}},
    {"editTriggerType", "common", IntProperty{&CoreProps::editTriggerType}},
    {"xmlCopy", "common", BoolProperty{&CoreProps::xmlCopy}},
    {"logicalSort", "common", BoolProperty{&CoreProps::logicalSort}},
    {"canVanishWorkAreaInMainWindow", "common", BoolProperty{&CoreProps::canVanishWorkAreaInMainWindow}},
    {"useCoolBar", "common", BoolProperty{&CoreProps::useCoolBar}},

    {"saveSkinName", "load", BoolProperty{&CoreProps::saveSkinName}},
    {"saveNeedChanged", "load", BoolProperty{&CoreProps::saveNeedChanged}},
    {"applyDialogsBeforeSave", "load", BoolProperty{&CoreProps::applyDialogsBeforeSave}},
    {"doubleIO", "load", BoolProperty{&CoreProps::doubleIO}},
    {"cautionToScenarioLoadErrors", "load", BoolProperty{&CoreProps::cautionToScenarioLoadErrors}},
    {"archiveInNewThread", "load", BoolProperty{&CoreProps::archiveInNewThread}},
    {"saveChangedOnlyWsn", "load", BoolProperty{&CoreProps::saveChangedOnlyWsn}},
    {"saveChangedOnly", "load", BoolProperty{&CoreProps::saveChangedOnly}},
    {"xmlFileNameIsIDOnly", "load", BoolProperty{&CoreProps::xmlFileNameIsIDOnly}},
    {"expandXMLs", "load", BoolProperty{&CoreProps::expandXMLs}},
    {"replaceClassicResourceExtension", "load", BoolProperty{&CoreProps::replaceClassicResourceExtension}},
    {"saveInnerImagePath", "load", BoolProperty{&CoreProps::saveInnerImagePath}},
    {"addNewClassicEngine", "load", BoolProperty{&CoreProps::addNewClassicEngine}},
    {"openLastScenario", "load", BoolProperty{&CoreProps::openLastScenario}},
    {"reconstruction", "load", BoolProperty{&CoreProps::reconstruction}},

    {"traceDirectories", "file", BoolProperty{&CoreProps::traceDirectories}},
    {"autoUpdateJpy1File", "file", BoolProperty{&CoreProps::autoUpdateJpy1File}},

    {"cautionBeforeReplace", "find", BoolProperty{&CoreProps::cautionBeforeReplace}},
    {"startIncrementalSearchWhenKeyDown", "find", BoolProperty{&CoreProps::startIncrementalSearchWhenKeyDown}},

    {"showAreaDirTree", "table", BoolProperty{&CoreProps::showAreaDirTree}},
    {"showSummaryInAreaTable", "table", BoolProperty{&CoreProps::showSummaryInAreaTable}},
    {"clickIsOpenEvent", "table", BoolProperty{&CoreProps::clickIsOpenEvent}},
    {"incrementNewAreaName", "table", BoolProperty{&CoreProps::incrementNewAreaName}},

    {"switchFixedWithCheckBox", "scene", BoolProperty{&CoreProps::switchFixedWithCheckBox}},
    {"resizingImageWithRatio", "scene", BoolProperty{&CoreProps::resizingImageWithRatio}},
    {"showSceneViewSelectionFilter", "scene", BoolProperty{&CoreProps::showSceneViewSelectionFilter}},
    {"drawXORSelectionLine", "scene", BoolProperty{&CoreProps::drawXORSelectionLine}},
    {"smoothingCard", "scene", BoolProperty{&CoreProps::smoothingCard}},
    {"ignoreBackgroundInRange", "scene", BoolProperty{&CoreProps::ignoreBackgroundInRange}},
    {"copyDesc", "scene", BoolProperty{&CoreProps::copyDesc}},
    {"showItemNumberOfSceneAndEventView", "scene", BoolProperty{&CoreProps::showItemNumberOfSceneAndEventView}},
    {"showCardStatusUnderPointer", "scene", BoolProperty{&CoreProps::showCardStatusUnderPointer}},

    {"showContentsGroupName", "event", BoolProperty{&CoreProps::showContentsGroupName}},
    {"showEventContentDescription", "event", BoolProperty{&CoreProps::showEventContentDescription}},
    {"contentsFloat", "event", BoolProperty{&CoreProps::contentsFloat}},
    {"contentsAutoHide", "event", BoolProperty{&CoreProps::contentsAutoHide}},
    {"straightEventTreeView", "event", BoolProperty{&CoreProps::straightEventTreeView}},
    {"restorePositionOfEventTreeView", "event", BoolProperty{&CoreProps::restorePositionOfEventTreeView}},
    {"forceIndentBranchContent", "event", BoolProperty{&CoreProps::forceIndentBranchContent}},
    {"showTerminalMark", "event", BoolProperty{&CoreProps::showTerminalMark}},
    {"showTargetStartLineNumber", "event", BoolProperty{&CoreProps::showTargetStartLineNumber}},
    {"classicStyleTree", "event", BoolProperty{&CoreProps::classicStyleTree}},
    {"clickIconIsStartEdit", "event", BoolProperty{&CoreProps::clickIconIsStartEdit}},
    {"adjustContentName", "event", BoolProperty{&CoreProps::adjustContentName}},
    {"showVariableValuesInEventText", "event", BoolProperty{&CoreProps::showVariableValuesInEventText}},
    {"editSelectionWithCombo", "event", BoolProperty{&CoreProps::editSelectionWithCombo}},
    {"refCardsAtEditBgImage", "event", BoolProperty{&CoreProps::refCardsAtEditBgImage}},
    {"floatMessagePreview", "event", BoolProperty{&CoreProps::floatMessagePreview}},
    {"useMessageWindowColorInTextContentDialog", "event", BoolProperty{&CoreProps::useMessageWindowColorInTextContentDialog}},
    {"selectVariableWithTree", "event", BoolProperty{&CoreProps::selectVariableWithTree}},
    {"useNamesAfterStandard", "event", BoolProperty{&CoreProps::useNamesAfterStandard}},
    {"expandChooserItems", "event", BoolProperty{&CoreProps::expandChooserItems}},

    {"showHandMark", "card", BoolProperty{&CoreProps::showHandMark}},
    {"showEventTreeMark", "card", BoolProperty{&CoreProps::showEventTreeMark}},
    {"ignoreEmptyStart", "card", BoolProperty{&CoreProps::ignoreEmptyStart}},
    {"showCardListHeader", "card", BoolProperty{&CoreProps::showCardListHeader}},
    {"showCardListTitle", "card", BoolProperty{&CoreProps::showCardListTitle}},
    {"showSkillCardLevel", "card", BoolProperty{&CoreProps::showSkillCardLevel}},
    {"showSpNature", "card", BoolProperty{&CoreProps::showSpNature}},
    {"showMotionDescription", "card", BoolProperty{&CoreProps::showMotionDescription}},
    {"radarStyleParams", "card", BoolProperty{&CoreProps::radarStyleParams}},
    {"showAptitudeOnCastCardEditor", "card", BoolProperty{&CoreProps::showAptitudeOnCastCardEditor}},
    {"linkCard", "card", BoolProperty{&CoreProps::linkCard}},
};

const char* defaultCategoryLabel(std::string_view key) {
    if (key == "common") return "General";
    if (key == "load") return "Load and save";
    if (key == "file") return "Files";
    if (key == "find") return "Find and replace";
    if (key == "table") return "Tables";
    if (key == "scene") return "Scene";
    if (key == "event") return "Event";
    if (key == "card") return "Cards";
    return nullptr;
}

}  // namespace

std::string EtcSettings::Presentation::category(std::string_view key) const {
    const auto found = categories.find(std::string(key));
    if (found != categories.end() && !found->second.empty()) return found->second;
    if (const auto* fallback = defaultCategoryLabel(key)) return fallback;
    return std::string(key);
}

std::string EtcSettings::Presentation::label(std::string_view key) const {
    const auto found = labels.find(std::string(key));
    if (found != labels.end() && !found->second.empty()) return found->second;
    if (message) {
        auto value = message(key);
        if (!value.empty()) return value;
    }
    return std::string(key);
}

EtcSettings::EtcSettings(Services services, std::shared_ptr<Widget> parent,
                         Props& properties, Presentation presentation)
    : services_(std::move(services)), parent_(std::move(parent)),
      properties_(properties), presentation_(std::move(presentation)) {
    if (const auto owner = std::dynamic_pointer_cast<Container>(parent_))
        setup(*owner);
    else
        setup();
}

EtcSettings::~EtcSettings() {
    lifetime_.reset();
    if(inc_search_) {inc_search_->modEvent.clear();inc_search_->searchTypeChanged={};inc_search_->close();}
    if(search_menu_)search_menu_->on_command={};
    if (search_edit_) search_edit_->on_changed = {};
    if (expand_bar_) expand_bar_->on_expansion_changed = {};
    for (auto& binding : bindings_)
        if (binding.control.checkbox) binding.control.checkbox->on_changed = {};
}

std::shared_ptr<EtcSettings::Panel> EtcSettings::makePanel() const {
    if (services_.gui) {
        auto component = services_.gui->create_component(
            ::cwxeditor::lib::ComponentKind::Panel);
        if (auto panel = std::dynamic_pointer_cast<Panel>(std::move(component)))
            return panel;
    }
    return std::make_shared<Panel>();
}

std::shared_ptr<EtcSettings::ScrollView> EtcSettings::makeScrollView() const {
    if (services_.gui) {
        auto component = services_.gui->create_component(
            ::cwxeditor::lib::ComponentKind::ScrollView);
        if (auto scroll = std::dynamic_pointer_cast<ScrollView>(std::move(component)))
            return scroll;
    }
    return std::make_shared<ScrollView>();
}

std::shared_ptr<EtcSettings::ExpandBar> EtcSettings::makeExpandBar() const {
    if (services_.gui) {
        auto component = services_.gui->create_component(
            ::cwxeditor::lib::ComponentKind::ExpandBar);
        if (auto bar = std::dynamic_pointer_cast<ExpandBar>(std::move(component)))
            return bar;
    }
    return std::make_shared<ExpandBar>();
}

std::shared_ptr<EtcSettings::CheckBox> EtcSettings::makeCheckBox(
    std::string text) const {
    if (services_.gui) {
        auto component = services_.gui->create_component(
            ::cwxeditor::lib::ComponentKind::CheckBox);
        if (auto check = std::dynamic_pointer_cast<CheckBox>(std::move(component))) {
            check->text = std::move(text);
            return check;
        }
    }
    return std::make_shared<CheckBox>(std::move(text));
}

void EtcSettings::setup(Container& area) {
    buildControls(area);
}

void EtcSettings::setup() {
    if (!panel_) panel_ = makePanel();
    buildControls(*panel_);
}

void EtcSettings::buildControls(Container& area) {
    if (!panel_) panel_ = makePanel();
    if (!panel_->layout()) panel_->set_layout(std::make_shared<::cwxeditor::lib::FillLayout>());

    if (&area != panel_.get()) {
        if (!area.layout()) area.set_layout(std::make_shared<::cwxeditor::lib::FillLayout>());
        const auto& children = area.children();
        if (std::find(children.begin(), children.end(), panel_) == children.end())
            area.add(panel_);
    }

    if (!inc_search_) {
        inc_search_=std::make_shared<incsearch::IncSearch>(services_,panel_.get(),
            incsearch::IncSearch::Enabled{},std::vector<incsearch::AdditionMatcher>{},presentation_.searchSettings);
        const auto life=std::weak_ptr<int>(lifetime_);
        const auto search=std::weak_ptr<incsearch::IncSearch>(inc_search_);
        inc_search_->searchTypeChanged=[life,persist=presentation_.searchTypeChanged](auto type){if(!life.expired()&&persist)persist(type);};
        inc_search_->modEvent.push_back([this,life,search]{
            if(life.expired())return;
            if(const auto current=search.lock())search_query_=current->searchText();
            ++search_revision_;showCheckBoxes();
        });
        search_menu_=std::make_shared<lib::Menu>(true);
        search_menu_->items.push_back({presentation_.searchSettings.windowTitle,"search",presentation_.searchShortcut});
        search_menu_->on_command=[this,life](auto command){if(!life.expired()&&command=="search")incSearch();};
        // Dは常時表示の入力欄ではなく、同じ補助IncSearchを使う。
    }
    if (!scroll_) {
        scroll_ = makeScrollView();
        scroll_->vertical_scroll = true;
        scroll_->horizontal_scroll = false;
        scroll_->set_layout(std::make_shared<::cwxeditor::lib::FillLayout>());
        panel_->add(scroll_);
    }
    if (!expand_bar_) {
        expand_bar_ = makeExpandBar();
        expand_bar_->on_expansion_changed = [this](std::size_t, bool) { scSize(); };
        expand_bar_->set_layout(std::make_shared<::cwxeditor::lib::LinearLayout>(
            ::cwxeditor::lib::Orientation::Vertical));
        scroll_->add(expand_bar_);

        std::unordered_map<std::string, std::shared_ptr<Panel>> groups;
        for (const auto& descriptor : kSettings) {
            auto [found, inserted] = groups.emplace(descriptor.category, nullptr);
            if (inserted) {
                // DはExpandItemの見出しと通常のComposite本体を使う。
                // ここでGroupBoxを使うと、見出しの余白を二重に確保してしまう。
                found->second = makePanel();
                found->second->accessibility_name = presentation_.category(descriptor.category);
                found->second->set_layout(
                    std::make_shared<::cwxeditor::lib::LinearLayout>(
                        ::cwxeditor::lib::Orientation::Vertical));
                if (const auto layout =
                        std::dynamic_pointer_cast<::cwxeditor::lib::LinearLayout>(
                            found->second->layout())) {
                    layout->padding = {5.0F, 5.0F, 5.0F, 5.0F};
                    layout->spacing = 5.0F;
                }
                category_boxes_.push_back(found->second);
                category_keys_.emplace_back(descriptor.category);
                expand_bar_->add(found->second);
                expand_bar_->expanded.push_back(true);
            }

            const auto label = presentation_.label(descriptor.key);
            std::function<bool()> read;
            std::function<void(bool)> write;
            std::visit(
                [this, &read, &write](auto member) {
                    using Member = decltype(member);
                    if constexpr (std::is_same_v<Member, BoolProperty>) {
                        read = [this, member] {
                            return (properties_.*member).value;
                        };
                        write = [this, member](bool value) {
                            (properties_.*member).value = value;
                        };
                    } else {
                        read = [this, member] {
                            return (properties_.*member).value == static_cast<
                                       std::int32_t>(::cwxeditor::cwx::structs::EditTrigger::Quick);
                        };
                        write = [this, member](bool value) {
                            (properties_.*member).value = static_cast<std::int32_t>(
                                value ? ::cwxeditor::cwx::structs::EditTrigger::Quick
                                      : ::cwxeditor::cwx::structs::EditTrigger::Slow);
                        };
                    }
                },
                descriptor.property);
            buildBinding(descriptor.key, descriptor.category, label, std::move(read),
                         std::move(write), found->second);
        }
    }

    if (!initialized_) {
        captureCancelValues();
        initialized_ = true;
    }
    updateDependencies();
    showCheckBoxes();
    if(!search_bound_){bindSearch(panel_);search_bound_=true;}
    scSize();
}

void EtcSettings::buildBinding(std::string_view key, std::string_view category,
                               std::string_view label,
                               std::function<bool()> read,
                               std::function<void(bool)> write,
                               std::shared_ptr<Panel>& group) {
    auto checkbox = makeCheckBox(std::string(label));
    checkbox->checked = read();
    checkbox->accessibility_name = std::string(key);
    checkbox->accessibility_description = std::string(category);

    Binding binding{{std::string(key), std::string(category), std::string(label), checkbox},
                    std::move(read), std::move(write)};
    group->add(checkbox);
    bindings_.push_back(std::move(binding));
    installListeners(bindings_.back());
    controls_.push_back(bindings_.back().control);
}

void EtcSettings::installListeners(Binding& binding) {
    const std::string key = binding.control.key;
    const auto checkbox = binding.control.checkbox;
    checkbox->on_changed = [this,key,checkbox,life=std::weak_ptr<int>(lifetime_)](bool value) {
        if(life.expired())return;
        checkbox->checked = value;
        enforceExclusiveOptions(key);
        updateDependencies();
        markModified(key);
        if(!life.expired())showCheckBoxes();
    };
}

void EtcSettings::incSearch() {
    const auto search=inc_search_;const std::weak_ptr<int> life=lifetime_;
    if(!search)return;
    search->startIncSearch(search_query_);
    if(life.expired())return;
    search_edit_=search->textEdit();
    if(search_edit_) {
        search_edit_->accessibility_name="設定の絞り込み検索";
        search_edit_->accessibility_description="設定名を入力して絞り込みます";
    }
    showCheckBoxes();
}
void EtcSettings::incSearch(std::string query) {setSearchQuery(std::move(query));incSearch();}
void EtcSettings::setSearchQuery(std::string query) {
    const std::weak_ptr<int> life=lifetime_;
    search_query_=std::move(query);++search_revision_;
    if(inc_search_ && inc_search_->isOpen()) {
        const auto search=inc_search_;search->setSearchText(search_query_);
        if(life.expired())return;
    }
    showCheckBoxes();
}
void EtcSettings::clearSearch() {setSearchQuery({});}
void EtcSettings::closeSearch() {
    const auto search=inc_search_;
    if(search)search->close(); // リセットの公開でこのホストが終了し得るため、その後は処理を続けない。
}
void EtcSettings::bindSearch(const lib::WidgetPtr& widget) {
    if(!widget)return;
    const std::weak_ptr<int> life=lifetime_;
    const std::weak_ptr<lib::Widget> target=widget;
    const auto search=std::weak_ptr<incsearch::IncSearch>(inc_search_);
    const auto menu=search_menu_;const auto gui=services_.gui;
    const auto pointer=widget->on_pointer;
    widget->on_pointer=[pointer,life,target,menu,gui](lib::PointerEvent& event){
        if(pointer)pointer(event);
        if(life.expired()||event.handled||!gui||event.action!=lib::PointerAction::Down||event.button!=lib::PointerButton::Secondary)return;
        const auto current=target.lock();if(!current||!current->enabled())return;
        event.handled=true;gui->show_context_menu(menu,current,event.position);
    };
    const auto container=std::dynamic_pointer_cast<lib::Container>(widget);
    // キー検索は設定のコントロールに属する。配置コンテナーに接
    // 続すると、受動的な入れ物が意図しないTab巡回先になる。
    if(!container) {
        const auto key=widget->on_key_down;
        widget->on_key_down=[key,life,search,target,menu](lib::KeyEvent& event){
            if(key)key(event);if(life.expired()||event.handled)return;
            if(lib::dispatch_menu_shortcut(menu,event.key,event.modifiers,event.printable)){event.handled=true;return;}
            const auto current=search.lock();const auto owner=target.lock();
            if(!current||!owner||!owner->enabled()||
               current->ownsCommittedTextInputRegistration(owner.get()))return;
            // DWTのDisplayフィルターは、フォーカス中コントロールのSpace/Enter
            // 処理を取り消さずに検索を始める。ネイティブの項目表示
            // は、上記の共通登録経路からIncSearchへ進む。
            (void)current->handleHostKeyDown(event,owner.get());
        };
        const auto text=widget->on_text_input;
        widget->on_text_input=[text,life,search,target](lib::TextInputEvent& event){
            if(life.expired()||event.handled)return;
            const auto current=search.lock();const auto owner=target.lock();
            if(current&&owner&&owner->enabled()&&current->handleHostTextInput(event,owner.get())){event.handled=true;return;}
            if(text)text(event);
        };
        const std::weak_ptr<lib::ScrollView> viewport=scroll_;
        const auto focus=widget->on_focus;widget->on_focus=[focus,life,search,target,viewport]{
            if(focus)focus();if(life.expired())return;
            const auto owner=target.lock();
            if(const auto current_viewport=viewport.lock();current_viewport&&owner)
                lib::reveal_window_control(current_viewport,owner);
            if(life.expired())return;
            const auto current=search.lock();if(current&&owner)current->handleFocusChanged(owner.get());
        };
        const auto blur=widget->on_blur;widget->on_blur=[blur,life,search,gui]{
            if(blur)blur();if(life.expired()||!gui)return;
            const auto current=search.lock();const auto focused=gui->focused_widget();if(!life.expired()&&current)current->handleFocusChanged(focused.get());
        };
    }
    if(container)
        for(const auto& child:container->children())bindSearch(child);

}

std::string EtcSettings::fold(std::string_view value) const {
    if (services_.text) return services_.text->case_fold(value);
    std::string result;
    result.reserve(value.size());
    for (const unsigned char character : value)
        result.push_back(static_cast<char>(std::tolower(character)));
    return result;
}

bool EtcSettings::matches(std::string_view value) const {
    if(inc_search_ && inc_search_->isSearching())return inc_search_->match(value);
    if (search_query_.empty()) return true;
    return fold(value).find(fold(search_query_)) != std::string::npos;
}

void EtcSettings::showCheckBoxes() {
    const std::weak_ptr<int> life=lifetime_;
    const auto revision=search_revision_;
    visible_count_ = 0;
    for (std::size_t group_index = 0; group_index < category_boxes_.size();
         ++group_index) {
        auto& group = category_boxes_[group_index];
        const auto& category_key = category_keys_[group_index];
        for (const auto& binding : bindings_) {
            if (binding.control.category != category_key) continue;
            const bool visible = matches(binding.control.label);
            const auto checkbox=binding.control.checkbox;
            checkbox->set_visible(visible);
            if(life.expired() || search_revision_!=revision)return;
            if (visible) ++visible_count_;
        }
        // Dが絞り込むのはコントロールでありExpandItemではない。検
        // 索に一致する設定がなくても、カテゴリ見出しと展開状態を維持する。
        const auto current=group;
        current->set_visible(true);
        if(life.expired() || search_revision_!=revision)return;
    }
    scSize();
}

void EtcSettings::scSize() {
    if (!expand_bar_ || !scroll_) return;
    // 共通の計測は文字倍率、グループ余白、展開・絞り込
    // み済み区画を把握している。親から割り当てた表示領
    // 域を保ち、別の固定画素の文書計算で置き換えない。
    const std::weak_ptr<int> life=lifetime_;
    const auto revision=search_revision_;
    const auto scroll=scroll_;const auto gui=services_.gui;
    lib::arrange_container(scroll, scroll->bounds());
    if(life.expired() || search_revision_!=revision)return;
    if (gui) gui->request_redraw(scroll);
}

void EtcSettings::syncFromProperties() {
    for (auto& binding : bindings_) binding.control.checkbox->checked = binding.read();
    enforceExclusiveOptions({});
    updateDependencies();
    showCheckBoxes();
}

void EtcSettings::enforceExclusiveOptions(std::string_view changed_key) {
    auto* floating = findBinding("contentsFloat");
    auto* auto_hide = findBinding("contentsAutoHide");
    if (!floating || !auto_hide) return;
    if (!floating->control.checkbox->checked || !auto_hide->control.checkbox->checked)
        return;

    if (changed_key == "contentsAutoHide")
        floating->control.checkbox->checked = false;
    else
        auto_hide->control.checkbox->checked = false;
}

void EtcSettings::updateDependencies() {
    const auto* straight = findBinding("straightEventTreeView");
    if (!straight) return;
    const bool selected = straight->control.checkbox->checked;
    if (auto* binding = findBinding("forceIndentBranchContent"))
        binding->control.checkbox->set_enabled(selected);
    if (auto* binding = findBinding("classicStyleTree"))
        binding->control.checkbox->set_enabled(!selected);
    if (auto* binding = findBinding("showTerminalMark"))
        binding->control.checkbox->set_enabled(selected);
    if (auto* binding = findBinding("showTargetStartLineNumber"))
        binding->control.checkbox->set_enabled(selected);
    if (auto* binding = findBinding("restorePositionOfEventTreeView"))
        binding->control.checkbox->set_enabled(selected);
}

void EtcSettings::markModified(std::string_view) {
    if (applying_) return;
    const std::weak_ptr<void> life=lifetime_;
    const auto callbacks=modEvent;
    for (const auto& listener : callbacks) {
        if(life.expired())return;
        if (listener) listener();
    }
}

EtcSettings::Binding* EtcSettings::findBinding(std::string_view key) {
    const auto found = std::find_if(
        bindings_.begin(), bindings_.end(),
        [key](const Binding& binding) { return binding.control.key == key; });
    return found == bindings_.end() ? nullptr : &*found;
}

const EtcSettings::Binding* EtcSettings::findBinding(std::string_view key) const {
    const auto found = std::find_if(
        bindings_.begin(), bindings_.end(),
        [key](const Binding& binding) { return binding.control.key == key; });
    return found == bindings_.end() ? nullptr : &*found;
}

std::optional<bool> EtcSettings::checked(std::string_view key) const {
    const auto* binding = findBinding(key);
    if (!binding) return std::nullopt;
    return binding->control.checkbox->checked;
}

std::shared_ptr<EtcSettings::CheckBox> EtcSettings::control(
    std::string_view key) const {
    const auto* binding = findBinding(key);
    return binding ? binding->control.checkbox : nullptr;
}

bool EtcSettings::setChecked(std::string_view key, bool checked_value) {
    auto* binding = findBinding(key);
    if (!binding) return false;
    if (!binding->control.checkbox->on_changed) {
        if (binding->control.checkbox->checked == checked_value) return false;
        binding->control.checkbox->checked = checked_value;
        markModified(key);
        return true;
    }
    return lib::set_toggle_checked(*binding->control.checkbox, checked_value);
}

std::vector<std::string> EtcSettings::validationErrors() const {
    std::vector<std::string> errors;
    const auto* floating = findBinding("contentsFloat");
    const auto* auto_hide = findBinding("contentsAutoHide");
    if (floating && auto_hide && floating->control.checkbox->checked &&
        auto_hide->control.checkbox->checked)
        errors.emplace_back("contentsFloat and contentsAutoHide cannot both be enabled");
    for (const auto& binding : bindings_) {
        if (!binding.control.checkbox)
            errors.emplace_back("missing checkbox for " + binding.control.key);
    }
    return errors;
}

bool EtcSettings::validate() const {
    return validationErrors().empty();
}

void EtcSettings::captureCancelValues() {
    cancel_values_.clear();
    for (const auto& binding : bindings_)
        cancel_values_.emplace(binding.control.key, binding.read());
}

bool EtcSettings::apply(const bool commit) {
    enforceExclusiveOptions({});
    if (!validate()) return false;
    applying_ = true;
    for (auto& binding : bindings_)
        binding.write(binding.control.checkbox->checked);
    applying_ = false;
    if (commit) commitAppliedValues();
    return true;
}

void EtcSettings::commitAppliedValues() {
    captureCancelValues();
    for (const auto& listener : apply_events_)
        if (listener) listener();
}

void EtcSettings::cancel() {
    applying_ = true;
    for (auto& binding : bindings_) {
        const auto found = cancel_values_.find(binding.control.key);
        if (found == cancel_values_.end()) continue;
        binding.control.checkbox->checked = found->second;
        binding.write(found->second);
    }
    applying_ = false;
    enforceExclusiveOptions({});
    updateDependencies();
    showCheckBoxes();
    for (const auto& listener : cancel_events_)
        if (listener) listener();
}

bool EtcSettings::finish(DialogResult result) {
    if (result == DialogResult::OK || result == DialogResult::Apply)
        return apply();
    cancel();
    return result == DialogResult::Cancel || result == DialogResult::None;
}

void EtcSettings::addApplyListener(Action listener) {
    if (listener) apply_events_.push_back(std::move(listener));
}

void EtcSettings::addCancelListener(Action listener) {
    if (listener) cancel_events_.push_back(std::move(listener));
}

bool EtcSettings::modified() const noexcept {
    for (const auto& binding : bindings_) {
        const auto found = cancel_values_.find(binding.control.key);
        if (found != cancel_values_.end() &&
            found->second != binding.control.checkbox->checked)
            return true;
    }
    return false;
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::etcsettings
