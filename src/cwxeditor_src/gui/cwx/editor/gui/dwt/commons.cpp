#include <tuple>
#include "commons.hpp"
#include "cwxeditor_src/lib/ui_window_stack.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/history.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/smalldialogs.hpp"

#include "cwxeditor_src/core/cwx/importutils.hpp"
#include "cwxeditor_src/core/cwx/menu.hpp"

#include <array>
#include <cassert>
#include <filesystem>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <deque>
#include <unordered_set>

namespace cwxeditor::cwx::editor::gui::dwt::commons {

// 履歴の文字列は空も含め明示的な値。通常の呼び出しは、既
// 存のSummaryから継承する簡易APIを維持する。
static SkinPtr findSkinSelection(Commons&, Props&, const Summary*, bool,
    String, String, String, String, bool, bool, bool);

namespace {

std::mutex registry_mutex;
struct RegistryEntry {
    std::weak_ptr<lib::Widget> root;
    std::weak_ptr<TLPData> data;
};
std::unordered_map<const lib::Widget*, RegistryEntry> registry;

// SWTは親リンクを提供するが、共通ウィジェットは子を保持する。同
// じ有効な構造から最寄りの登録済み祖先を解決する。この情報取得に
// は、フォーカスと独立して非表示・非アクティブなページも含める。
std::optional<std::size_t> metadataDistance(const WidgetPtr& root,
                                            const lib::Widget* target) {
    std::deque<std::pair<WidgetPtr, std::size_t>> pending{{root, 0}};
    std::unordered_set<const lib::Widget*> visited;
    while (!pending.empty()) {
        auto [widget, depth] = std::move(pending.front()); pending.pop_front();
        if (!widget || !visited.insert(widget.get()).second) continue;
        if (widget.get() == target) return depth;
        if (const auto tabs = std::dynamic_pointer_cast<lib::TabView>(widget))
            for (const auto& page : tabs->pages) pending.emplace_back(page.content, depth + 1);
        if (const auto container = std::dynamic_pointer_cast<lib::Container>(widget))
            for (const auto& child : container->children()) pending.emplace_back(child, depth + 1);
    }
    return std::nullopt;
}

constexpr std::array<StringView, 14> auxiliary_keys = {
    "data", "flag", "castCard", "skillCard", "itemCard", "beastCard",
    "infoCard", "file", "coupon", "gossip", "completeStamp", "keyCode",
    "cellName", "cardGroup"};

[[nodiscard]] String appendDuplicate(String key, std::size_t index) {
    return key + '#' + std::to_string(index);
}

[[nodiscard]] bool isEmpty(const String& value) noexcept { return value.empty(); }

[[nodiscard]] bool projectionVisible(const ViewProjection& value) {
    return value.logicalShell ? lib::widget_is_visible_in(value.logicalShell, value.treeView.get())
                             : value.treeView && value.treeView->visible();
}

void applyImportedMaterials(const importutils::ImportResult& result) {
    for (const auto& file : result.materials) {
        if (file.dst.empty() || file.src.empty()) continue;
        const auto destination = structs::pathFromUtf8(file.dst);
        std::error_code error;
        std::filesystem::create_directories(destination.parent_path(), error);
        if (error) continue;
        if (utils::isBinImg(file.src)) {
            const auto bytes = utils::strToBImg(file.src);
            std::ofstream stream(destination, std::ios::binary | std::ios::trunc);
            if (stream)
                stream.write(reinterpret_cast<const char*>(bytes.data()),
                             static_cast<std::streamsize>(bytes.size()));
            continue;
        }
        const auto source = structs::pathFromUtf8(file.src);
        std::filesystem::copy_file(source, destination,
            file.overwrite ? std::filesystem::copy_options::overwrite_existing
                           : std::filesystem::copy_options::skip_existing,
            error);
    }
}

}  // namespace

TopLevelPanel::TopLevelPanel() : shell_(std::make_shared<lib::Panel>()) {}

TopLevelPanel::TopLevelPanel(ContainerPtr shell)
    : shell_(shell ? std::move(shell) : std::make_shared<lib::Panel>()) {}

String TopLevelPanel::title() const { return title_; }

Image TopLevelPanel::image() const noexcept { return image_; }

ContainerPtr TopLevelPanel::shell() const noexcept { return shell_; }

WidgetPtr TopLevelPanel::focusControl() const noexcept {
    return focus_control_ ? focus_control_ : shell_;
}

void TopLevelPanel::title(String value) {
    title_ = std::move(value);
    if (auto window = std::dynamic_pointer_cast<lib::Window>(shell_)) {
        window->title = title_;
    }
}

void TopLevelPanel::image(const Image value) noexcept { image_ = value; }

void TopLevelPanel::shell(ContainerPtr value) {
    shell_ = value ? std::move(value) : std::make_shared<lib::Panel>();
}

void TopLevelPanel::focusControl(WidgetPtr value) noexcept {
    focus_control_ = std::move(value);
}

void TopLevelPanel::statusText(std::function<void(String)> value) {
    status_text_ = std::move(value);
}

void TopLevelPanel::retireHostBindings() noexcept {
    // SWTの終了は、ホストの依存先が生存中に実際の編集部を破棄する
    // 。保持された共通パネル・ウィジェットのコールバックによって、
    // Mainの状態や登録表の解放後まで編集部の破棄を遅らせない。
    if (!lifetime_.token) return;
    lifetime_.token.reset();
    const auto retire = std::move(host_retirement_);
    const auto bindings = std::tuple{std::move(open_path_),
        std::move(opened_paths_provider_), std::move(projection_),
        std::move(menus_), std::move(status_text_)};
    if (retire) retire();
}

std::vector<String> TopLevelPanel::openedCWXPath() const {
    if (!lifetime_.token) return {};
    const auto provider = opened_paths_provider_;
    // Dは現在の具体的な編集部のパスを返す。コールバックをコピーして値
    // を直接返し、外部プロバイダー呼び出し後は受信先にアクセスしない。
    return provider ? provider() : opened_paths_;
}

bool TopLevelPanel::openCWXPath(String cwxPath, const bool shellActivate) {
    if (!lifetime_.token) return false;
    if (cwxPath.empty()) return false;
    // DのTopLevelPanelではこの操作は
    // 抽象メソッド。汎用ラッパーはパスを記憶するだ
    // けで成功とせず、具体的な編集部へ転送する。
    const auto callback = open_path_;
    const std::weak_ptr<const int> life = lifetime_.token;
    if (!callback || !callback(cwxPath, shellActivate) || life.expired()) return false;
    if (projection_.owner) {
        const auto root = projection_.owner->cwxPath(true);
        if (cwxPath != root && !cwxPath.starts_with(root + "/"))
            cwxPath = root + "/" + cwxPath;
    }
    rememberCWXPath(std::move(cwxPath));
    return true;
}

void TopLevelPanel::rememberCWXPath(String path) {
    if (path.empty() ||
        std::find(opened_paths_.begin(), opened_paths_.end(), path) !=
            opened_paths_.end()) {
        return;
    }
    opened_paths_.push_back(std::move(path));
}

void TopLevelPanel::setOpenedCWXPath(std::vector<String> paths) {
    opened_paths_ = std::move(paths);
}

void TopLevelPanel::putMenuAction(const MenuID menuID, lib::Action action,
                                  MenuEnabled enabled) {
    // Dのvoidデリゲートは、最初の操作とその判定の両方を保持する。
    if (menus_.contains(menuID)) return;
    putMenuAction(menuID,
                  [action = std::move(action)](const SelectionEvent&) {
                      if (action) action();
                  },
                  std::move(enabled));
}

void TopLevelPanel::putMenuAction(const MenuID menuID, MenuAction action,
                                  MenuEnabled enabled) {
    if (!lifetime_.token) return;
    const bool initial = !menus_.contains(menuID);
    auto& entry = menus_[menuID];
    if (initial) entry.action = std::move(action);
    entry.enabled = std::move(enabled);
}

void TopLevelPanel::putMenuChecked(const MenuID menuID, lib::Action action,
                                   MenuChecked checked, MenuEnabled enabled) {
    putMenuChecked(menuID,
                   [action = std::move(action)](const SelectionEvent&) {
                       if (action) action();
                   },
                   std::move(checked), std::move(enabled));
}

void TopLevelPanel::putMenuChecked(const MenuID menuID, MenuAction action,
                                   MenuChecked checked, MenuEnabled enabled) {
    if (!lifetime_.token) return;
    const bool initial = !menus_.contains(menuID);
    auto& entry = menus_[menuID];
    if (initial) {
        entry.action = std::move(action);
        entry.enabled = std::move(enabled);
    }
    // Dの別の_chkマップは、未登録と登録済みのnull取得関数を区別する。
    if (!entry.checked_registered) {
        entry.checked = std::move(checked);
        entry.checked_registered = true;
    }
}

TopLevelPanel::MenuAction TopLevelPanel::menuAction(const MenuID menuID) const {
    if (!lifetime_.token) return {};
    const auto it = menus_.find(menuID);
    return it == menus_.end() ? MenuAction{} : it->second.action;
}

TopLevelPanel::MenuChecked TopLevelPanel::menuChecked(const MenuID menuID) const {
    if (!lifetime_.token) return {};
    const auto it = menus_.find(menuID);
    return it == menus_.end() ? MenuChecked{} : it->second.checked;
}

TopLevelPanel::MenuEnabled TopLevelPanel::menuEnabled(const MenuID menuID) const {
    if (!lifetime_.token) return {};
    const auto it = menus_.find(menuID);
    return it == menus_.end() ? MenuEnabled{} : it->second.enabled;
}

bool TopLevelPanel::doMenu(Commons& comm, const int keyCode,
                           const std::uint32_t character,
                           const std::uint32_t stateMask,
                           std::function<SelectionEvent()> event) {
    const std::weak_ptr<const int> panel_life = lifetime_.token;
    const auto comm_life = comm.lifetimeToken();
    const auto snapshot = menus_;
    for (const auto& [menu, entry] : snapshot) {
        if (!entry.action || menu == MenuID::None || ::cwxeditor::cwx::menu::isLocalMenu(menu)) continue;
        if (!comm.prop()) continue;
        const auto accelerator = dmenu::convertAccelerator(comm.prop()->buildMenu(menu));
        if (!dmenu::eqAcc(static_cast<int>(accelerator), keyCode, character, stateMask)) continue;
        const bool enabled = !entry.enabled || entry.enabled();
        if (panel_life.expired() || comm_life.expired()) return false;
        if (!enabled) continue;
        comm.isMenuAccelerating(static_cast<int>(accelerator));
        struct Reset {
            Commons* comm;
            std::weak_ptr<const int> life;
            ~Reset() { if (!life.expired()) comm->isMenuAccelerating(0); }
        } reset{&comm, comm_life};
        // Dのscope(exit)は操作が例外を出してもフラグを戻す。C++
        // のコールバックは受信先を破棄し得るため、復元前にトークンを確認する。
        SelectionEvent selection;
        if (event) selection = event();
        if (panel_life.expired() || comm_life.expired()) return false;
        selection.accelerator_modifiers = dmenu::acceleratorModifiers(accelerator);
        selection.is_accelerator = true;
        entry.action(selection);
        return true;
    }
    return false;
}

String TopLevelPanel::statusLine() const { return status_; }

void TopLevelPanel::statusLine(String value) {
    status_ = std::move(value);
    if (status_text_) status_text_(status_);
}

void attachTLPData(const WidgetPtr& control, std::shared_ptr<TLPData> data) {
    if (!control) return;
    std::lock_guard lock(registry_mutex);
    if (data) {
        registry[control.get()] = {control, std::move(data)};
    } else {
        registry.erase(control.get());
    }
}

std::shared_ptr<TLPData> tlpData(const WidgetPtr& control) {
    if (!control) return {};
    std::vector<std::pair<WidgetPtr, std::shared_ptr<TLPData>>> entries;
    {
        std::lock_guard lock(registry_mutex);
        for (auto it = registry.begin(); it != registry.end();) {
            auto root = it->second.root.lock(); auto data = it->second.data.lock();
            if (!root || !data) { it = registry.erase(it); continue; }
            if (root == control) return data;
            entries.emplace_back(std::move(root), std::move(data)); ++it;
        }
    }
    std::optional<std::size_t> nearest;
    std::shared_ptr<TLPData> result;
    for (const auto& [root, data] : entries) {
        const auto distance = metadataDistance(root, control.get());
        if (distance && (!nearest || *distance < *nearest)) {
            nearest = distance; result = data;
        }
    }
    // 既存の共通呼び出し側は、これをnullを許容する検索として使う。Dは未接続のコン
    // トロールで例外を出すが、この省略可能なAPIは維持する。getTopLevelPanel
    // はDでもnullを許容する。両方とも同じ最寄りの祖先の解決処理を使う。
    return result;
}

std::shared_ptr<TopLevelPanel> getTopLevelPanel(const WidgetPtr& control) {
    if (const auto data = tlpData(control)) return data->tlp;
    return {};
}

Commons::Commons(Props& prop, FileSync& sync) : Commons(&prop, &sync) {}

Commons::Commons(Props* prop, FileSync* sync)
    : prop_(prop), sync_(sync),
      services_(prop ? prop->images().services() : lib::Services{}),
      clipboard_(services_.clipboard) {
    initializeSignals();
}

Commons::~Commons() { dispose(); }

void Commons::summary(SummaryPtr value) {
    if (summary_ == value) return;
    if (event_template_editor_ && event_template_summary_.lock() != value) {
        const auto life = lifetimeToken();
        auto editor = std::exchange(event_template_editor_, {});
        event_template_summary_.reset();
        editor->forceCancel();
        if (life.expired()) return;
    }
    // Dは変更済みの状態での変更も含め、各変更を転送する。別の公
    // 開トークンで、入れ子のA→B→Aの再接続中に実行中のコール
    // バックも無効にする。解除可能なノードで配送を安定させる。
    summary_change_publication_.reset();
    summary_change_subscription_.reset();
    summary_ = std::move(value);
    if (!summary_ || !lifetime_.token) return;
    summary_change_publication_ = std::make_shared<const int>(0);
    const std::weak_ptr<const int> publication = summary_change_publication_;
    summary_change_subscription_ = summary_->observeChanges([this, publication] {
        if (publication.expired()) return;
        changed.call();
    });
}

void Commons::initializeSignals() {
    (void)refScenario.add([this](SummaryPtr value) {
        clearFlagDirExpandedS(value);
        clearStepDirExpandedS(value);
        clearVariantDirExpandedS(value);
        clearAreaDirExpandedS(value);
        clearBattleDirExpandedS(value);
        clearPackageDirExpandedS(value);
    });
    (void)refVarSelectStyle.add([this] {
        clearFlagDirExpanded();
        clearStepDirExpanded();
        clearVariantDirExpanded();
        clearAreaDirExpanded();
        clearBattleDirExpanded();
        clearPackageDirExpanded();
    });
    (void)refContentsToolBoxStyle.add(
        [this] { reconstructContentsToolBox(); });
}

void Commons::clearExpanded() {
    flagDirExpanded.clear();
    stepDirExpanded.clear();
    variantDirExpanded.clear();
    flagAreaExpanded.clear();
    flagBattleExpanded.clear();
    flagPackageExpanded.clear();
}

void Commons::clearFlagDirExpandedS(const SummaryPtr&) { flagDirExpanded.clear(); }
void Commons::clearStepDirExpandedS(const SummaryPtr&) { stepDirExpanded.clear(); }
void Commons::clearVariantDirExpandedS(const SummaryPtr&) { variantDirExpanded.clear(); }
void Commons::clearAreaDirExpandedS(const SummaryPtr&) { flagAreaExpanded.clear(); }
void Commons::clearBattleDirExpandedS(const SummaryPtr&) { flagBattleExpanded.clear(); }
void Commons::clearPackageDirExpandedS(const SummaryPtr&) { flagPackageExpanded.clear(); }
void Commons::clearFlagDirExpanded() { flagDirExpanded.clear(); }
void Commons::clearStepDirExpanded() { stepDirExpanded.clear(); }
void Commons::clearVariantDirExpanded() { variantDirExpanded.clear(); }
void Commons::clearAreaDirExpanded() { flagAreaExpanded.clear(); }
void Commons::clearBattleDirExpanded() { flagBattleExpanded.clear(); }
void Commons::clearPackageDirExpanded() { flagPackageExpanded.clear(); }

void Commons::dispose() noexcept {
    if (auto editor = std::exchange(event_template_editor_, {})) {
        event_template_summary_.reset();
        try { editor->forceCancel(); } catch (...) {}
    }
    summary_change_publication_.reset();
    summary_change_subscription_.reset();
    closeAll();
    lifetime_.token.reset();
    ++toolbar_request_generation_;
    ++toolbar_refresh_revision_;
    main_toolbars_ = {};
    toolbars_refreshed_ = {};
    external_changes_ = {};
    import_publication_ = {};
    path_replacement_ = {};
    id_replacement_ = {};
    file_selection_ = {};
    name_selection_ = {};
    for (auto& widget : toolbars_) {
        if (widget) widget->set_visible(false);
    }
    toolbars_.clear();
    toolbar_enabled_.clear();
    windows_.clear();
    pane_factories_.clear();
    metadata_.clear();
    changed.dispose();
    refToolsEnabled.dispose();
    refImageScale.dispose();
    refSwitchFixedWithCheckBox.dispose();
    refImagePaneSelectionFilter.dispose();
    save.dispose();
    saved.dispose();
    refHistories.dispose();
    refImportHistory.dispose();
    refHistoryMax.dispose();
    refSearchHistories.dispose();
    refExecutedParties.dispose();
    refPartyHistoryMax.dispose();
    refScenario.dispose();
    refScenarioName.dispose();
    refScenarioPath.dispose();
    refSkin.dispose();
    refClassicSkin.dispose();
    refBgImageSettings.dispose();
    refSettingsWithSkinTypes.dispose();
    refDataVersion.dispose();
    refTargetVersion.dispose();
    refStandardSelections.dispose();
    refStandardKeyCodes.dispose();
    refOuterTools.dispose();
    refEventTemplates.dispose();
    refPath.dispose();
    refPaths.dispose();
    delPaths.dispose();
    replPath.dispose();
    refUseCount.dispose();
    refAreaTable.dispose();
    refCast.dispose();
    delCast.dispose();
    refSkill.dispose();
    delSkill.dispose();
    refItem.dispose();
    delItem.dispose();
    refBeast.dispose();
    delBeast.dispose();
    refInfo.dispose();
    delInfo.dispose();
    refFlagDir.dispose();
    delFlagDir.dispose();
    refFlagAndStep.dispose();
    delFlagAndStep.dispose();
    replText.dispose();
    replID.dispose();
    refIgnorePaths.dispose();
    refCardState.dispose();
    refWallpaper.dispose();
    refSortCondition.dispose();
    refCardTableColumnWidth.dispose();
    refShowCardListHeader.dispose();
    refShowCardListTitle.dispose();
    refUndoMax.dispose();
    refMenu.dispose();
    refSoundType.dispose();
    refShowToolBar.dispose();
    refCardImageStatus.dispose();
    refEventTreeStyle.dispose();
    refRadarStyle.dispose();
    refCastCardParameterEditStyle.dispose();
    refShowAptitudes.dispose();
    refVarSelectStyle.dispose();
    refEventTreeViewStyle.dispose();
    refEventEditorStyle.dispose();
    refContentsToolBoxStyle.dispose();
    refEventTreeSlope.dispose();
    refFloatMessagePreview.dispose();
    refUseMessageWindowColorInTextContentDialog.dispose();
    refUpdateMotionBarStyle.dispose();
    refMenuCardAndBgImageList.dispose();
    refKeyCodesByFeatures.dispose();
    refElementOverrides.dispose();
    refTableViewStyle.dispose();
    refArea.dispose();
    delArea.dispose();
    refBattle.dispose();
    delBattle.dispose();
    refPackage.dispose();
    delPackage.dispose();
    refMainCardsSort.dispose();
    refHandCardsSort.dispose();
    refImportCardsSort.dispose();
    refImportHandCardsSort.dispose();
    refImportAreasSort.dispose();
    addMenuCard.dispose();
    refMenuCard.dispose();
    delMenuCard.dispose();
    upMenuCard.dispose();
    downMenuCard.dispose();
    refMenuCardIndices.dispose();
    addBgImage.dispose();
    refBgImage.dispose();
    delBgImage.dispose();
    upBgImage.dispose();
    downBgImage.dispose();
    selContentTool.dispose();
    refEventTree.dispose();
    delEventTree.dispose();
    refContent.dispose();
    delContent.dispose();
    refContentText.dispose();
    refPreviewValues.dispose();
    refCoupons.dispose();
    refGossips.dispose();
    refCompleteStamps.dispose();
    refKeyCodes.dispose();
    refCellNames.dispose();
    refCardGroups.dispose();
    closeAdds.dispose();
}

void Commons::put(const ToolBarPtr& bar) {
    if (!bar) return;
    if (std::find_if(toolbars_.begin(), toolbars_.end(),
                     [&bar](const WidgetPtr& value) {
                         return value.get() == bar.get();
                     }) == toolbars_.end()) {
        toolbars_.push_back(bar);
    }
}

void Commons::put(const WidgetPtr& widget, std::function<bool()> enabled) {
    if (!widget) return;
    if (enabled) {
        toolbar_enabled_[widget.get()] = std::move(enabled);
        widget->set_enabled(toolbar_enabled_[widget.get()]());
    }
    if (std::find_if(toolbars_.begin(), toolbars_.end(),
                     [&widget](const WidgetPtr& value) {
                         return value.get() == widget.get();
                     }) == toolbars_.end()) {
        toolbars_.push_back(widget);
    }
}

void Commons::removeToolBar(const WidgetPtr& widget) noexcept {
    if (!widget) return;
    toolbar_enabled_.erase(widget.get());
    std::erase_if(toolbars_, [&widget](const WidgetPtr& value) {
        return value.get() == widget.get();
    });
}

void Commons::clearToolBars() {
    ++toolbar_request_generation_;
    ++toolbar_refresh_revision_;
    toolbars_.clear();
    toolbar_enabled_.clear();
    refresh_toolbar_arg_.reset();
    refresh_toolbar_pending_ = false;
}

WidgetPtr Commons::lastFocusEditor() const noexcept {
    return last_focus_editor_;
}

void Commons::refreshToolBar() { refreshToolBar({}); }

void Commons::refreshToolBar(const WidgetPtr& focus) {
    if (!lifetime_.token) return;
    refresh_toolbar_arg_ = focus;
    if (refresh_toolbar_pending_) return;
    refresh_toolbar_pending_ = true;
    const auto request = ++toolbar_request_generation_;
    const std::weak_ptr<const int> life = lifetime_.token;
    // SWTのasyncExecは、閉じるウィンドウより長く残る場合がある。全
    // バックエンドで同じ遅延処理の規則を使い、異なるのは配送の基本処理だけ。
    if (services_.dispatcher) {
        services_.dispatcher->post([this, life, request] {
            if (life.expired() || request != toolbar_request_generation_) return;
            applyToolBarRefresh();
        });
        return;
    }
    applyToolBarRefresh();
}

void Commons::applyToolBarRefresh() {
    refresh_toolbar_pending_ = false;
    const auto life = lifetimeToken();
    const auto request = toolbar_request_generation_;
    const auto current = [this, life, request] {
        return !life.expired() && request == toolbar_request_generation_;
    };
    auto focus = refresh_toolbar_arg_;
    const auto gui = services_.gui;
    const auto main = main_;
    if (!focus && gui) focus = gui->focused_widget();
    if (!current()) return;
    WidgetPtr text_focus;
    std::map<MenuID, std::function<bool()>> overrides;
    if (focus && main && lib::window_contains_widget(main, focus.get())) {
        if (const auto text = dmenu::textMenuOf(focus)) {
            text_focus = focus;
            // Dは実際の各項目に結び付いた操作可否だけを評価する。省略可能なク
            // リップボード・履歴のコールバックも、項目ごとの例外捕捉内に置く。
            overrides = {{MenuID::Undo, [text] { return text->canUndo(); }},
                {MenuID::Redo, [text] { return text->canRedo(); }},
                {MenuID::Copy, [text] { return text->canCopy(); }},
                {MenuID::Cut, [text] { return text->canCut(); }},
                {MenuID::Paste, [text] { return text->canPaste(); }},
                {MenuID::Delete, [text] { return text->canDelete(); }},
                {MenuID::SelectAll, [text] { return text->canSelectAll(); }}};
        }
    }
    // フォーカスやクリップボードのプロバイダーが、この要求を失効・置換させる場合がある。
    if (!current()) return;
    last_focus_editor_ = std::move(text_focus);
    refreshToolBarCapabilities(overrides);
}

void Commons::refreshToolBarItems(const ToolBarOverrides& overrides) {
    std::map<MenuID, std::function<bool()>> capabilities;
    for (const auto& [id, value] : overrides) capabilities[id] = [value] { return value; };
    refreshToolBarCapabilities(capabilities);
}

void Commons::refreshToolBarCapabilities(const std::map<MenuID, std::function<bool()>>& overrides) {
    if (!lifetime_.token) return;
    const auto life = lifetimeToken();
    const auto revision = ++toolbar_refresh_revision_;
    const auto current = [this, life, revision] {
        return !life.expired() && toolbar_refresh_revision_ == revision;
    };
    auto widgets = toolbars_;
    const auto enabled = toolbar_enabled_;
    auto main_bars = main_toolbars_;
    std::vector<ToolBarPtr> owned_main_bars;
    auto finished = toolbars_refreshed_;
    if (main_bars) {
        owned_main_bars = main_bars();
        if (!current()) return;
        for (const auto& bar : owned_main_bars)
            if (bar && std::ranges::none_of(widgets, [&](const auto& w) { return w == bar; }))
                widgets.push_back(bar);
    }
    // 今回の呼び出しのコントロールとコールバックだけを保持する。通知が同期的
    // にCommonsを終了させたり、ツールバーの世代を置換したりし得る。
    for (const auto& widget : widgets) {
        if (!widget) continue;
        const auto bar=std::dynamic_pointer_cast<dmenu::ToolBar>(widget);
        if(bar && std::ranges::find(owned_main_bars,bar)==owned_main_bars.end()) {
            bool visible=bar->visible();
            // SWTのisVisibleは祖先と選択タブページも考慮する。可
            // 視だが無効なバーも、再度有効にできるよう更新する必要がある。
            if(main_) {
                visible=lib::widget_is_visible_in(main_,bar.get());
                if(!visible) for(const auto& shell:auxiliary_windows_)
                    if(lib::widget_is_visible_in(shell,bar.get())) {visible=true;break;}
            }
            if(!visible)continue;
        }
        if (const auto found = enabled.find(widget.get()); found != enabled.end() && found->second) {
            auto callback = found->second;
            try {
                const bool value = callback();
                if (!current()) return;
                widget->set_enabled(value);
            } catch (...) {
                if (!current()) return;
                utils::debugln("toolbar control enabled callback failed");
            }
        }
        // Dは非表示ペインのバーを除外する。一方MainWindowは、切り離
        // されたホストの操作部も含め、設定済みの全コマンドモデルを更新する。
        if(bar) bar->refreshItems(overrides,current);
        // 単純なlibのツールバーはMenuDataを持たない。親の有
        // 効状態で上書きせず、指定された各項目の操作可否を維持する。
        if (!current()) return;
    }
    if (finished) finished();
    if (!current()) return;
    refToolsEnabled.call();
}

void Commons::updateToolBarText() {
    if (!prop_ || !lifetime_.token) return;
    const auto life = lifetimeToken();
    auto widgets = toolbars_;
    auto provider = main_toolbars_;
    if (provider) {
        const auto bars = provider();
        if (life.expired()) return;
        for (const auto& bar : bars)
            if (bar && std::ranges::none_of(widgets, [&](const auto& w) { return w == bar; }))
                widgets.push_back(bar);
    }
    for (const auto& widget : widgets) {
        const auto bar = std::dynamic_pointer_cast<dmenu::ToolBar>(widget);
        if (!bar) continue;
        for (std::size_t index = 0; index < bar->size(); ++index) {
            const auto entry = bar->entry(index);
            if (!entry || entry->data.id == MenuID::None) continue;
            const auto text = prop_->buildTool(entry->data.id);
            if (life.expired()) return;
            dmenu::ToolItemRef item(bar.get(), index);
            if (item.native().icon) item.setTooltip(text);
            else item.setText(text);
        }
    }
}

void Commons::baseShell(WindowPtr main, WindowPtr tableWin, WindowPtr flagWin,
                        WindowPtr castWin, WindowPtr skillWin,
                        WindowPtr itemWin, WindowPtr beastWin,
                        WindowPtr infoWin, WindowPtr dirWin,
                        WindowPtr couponWin, WindowPtr gossipWin,
                        WindowPtr completeStampWin, WindowPtr keyCodeWin,
                        WindowPtr cellNameWin, WindowPtr cardGroupWin) {
    main_ = std::move(main);
    auxiliary_windows_ = {std::move(tableWin), std::move(flagWin),
                          std::move(castWin), std::move(skillWin),
                          std::move(itemWin), std::move(beastWin),
                          std::move(infoWin), std::move(dirWin),
                          std::move(couponWin), std::move(gossipWin),
                          std::move(completeStampWin), std::move(keyCodeWin),
                          std::move(cellNameWin), std::move(cardGroupWin)};
    clipboard_ = ClipData(services_.clipboard);
}

void Commons::retainPanelMetadata(
    const std::shared_ptr<TopLevelPanel>& panel, std::shared_ptr<void> owner) {
    if (!panel) return;
    const auto shell = panel->shell();
    if (!shell) return;
    auto data = std::make_shared<TLPData>(panel);
    data->main = std::move(owner);
    metadata_[shell.get()] = data;
    attachTLPData(shell, std::move(data));
}

void Commons::clearPanelMetadata(
    const std::shared_ptr<TopLevelPanel>& panel) noexcept {
    if (!panel) return;
    const auto shell = panel->shell();
    if (!shell) return;
    metadata_.erase(shell.get());
    attachTLPData(shell, {});
}

void Commons::forgetRegisteredPane(const StringView key) noexcept {
    if (key.empty()) return;
    const String name(key);
    const auto found = registered_panes_.find(name);
    if (found == registered_panes_.end()) return;

    if (const auto panel = found->second.lock()) {
        const auto shell = panel->shell();
        const bool opened = shell && std::any_of(
            windows_.begin(), windows_.end(), [&](const auto& entry) {
                return entry.second && entry.second->shell() == shell;
            });
        // 開いているウィンドウは、通常の終了経路で解除されるまでメタデータを保持する。
        if (!opened) clearPanelMetadata(panel);
    }
    registered_panes_.erase(found);
}

void Commons::registerPaneFactory(String key, PaneFactory factory) {
    if (key.empty()) return;
    if (!factory) {
        unregisterPaneFactory(key);
        return;
    }
    forgetRegisteredPane(key);
    pane_factories_[std::move(key)] = std::move(factory);
}

void Commons::registerPane(String key, std::shared_ptr<TopLevelPanel> panel) {
    if (key.empty()) return;
    if (!panel) {
        unregisterPaneFactory(key);
        return;
    }
    const auto registeredKey = key;
    registerPaneFactory(
        std::move(key),
        [panel](String, bool, const std::shared_ptr<void>&) { return panel; });
    retainPanelMetadata(panel);
    registered_panes_[registeredKey] = panel;
}

void Commons::unregisterPaneFactory(const StringView key) noexcept {
    if (key.empty()) return;
    forgetRegisteredPane(key);
    pane_factories_.erase(String(key));
}

Commons::PaneFactory Commons::paneFactoryFor(const StringView key) const {
    if (key.empty()) return {};
    if (const auto exact = pane_factories_.find(String(key));
        exact != pane_factories_.end()) {
        return exact->second;
    }

    // パスを指定した要求は、その系統の登録に対して解決する。area.scene
    // とarea.scene:fooを両方登録しても曖
    // 昧にならないよう、役立つ最長の接頭辞を先に扱う。
    auto consider = [this](const StringView candidate) -> PaneFactory {
        if (candidate.empty()) return {};
        if (const auto it = pane_factories_.find(String(candidate));
            it != pane_factories_.end()) {
            return it->second;
        }
        return {};
    };
    const auto duplicate = key.find('#');
    const auto without_duplicate =
        duplicate == StringView::npos ? key : key.substr(0, duplicate);
    if (const auto factory = consider(without_duplicate)) return factory;
    const auto separator = without_duplicate.find(':');
    if (separator != StringView::npos) {
        if (const auto factory = consider(without_duplicate.substr(0, separator))) {
            return factory;
        }
    }
    return {};
}

std::shared_ptr<TopLevelPanel> Commons::restoreRegisteredPane(String key) {
    return openRegisteredPanel(std::move(key), {}, false, {});
}

bool Commons::hasPaneFactory(const StringView key) const noexcept {
    return static_cast<bool>(paneFactoryFor(key));
}

std::shared_ptr<TopLevelPanel> Commons::openRegisteredPanel(
    String key, String title, const bool shellActivate,
    std::shared_ptr<void> owner) {
    if (key.empty()) return {};
    if (const auto it = windows_.find(key); it != windows_.end()) {
        return it->second;
    }
    const auto factory = paneFactoryFor(key);
    if (!factory) return {};
    const auto life = lifetimeToken();
    auto panel = factory(key, shellActivate, owner);
    if (life.expired() || !panel || !panel->shell()) return {};
    if (panel->title().empty() && !title.empty()) panel->title(title);
    retainPanelMetadata(panel, std::move(owner));
    windows_.emplace(key, panel);
    return panel;
}

bool Commons::isChanged() const {
    const auto current = summary_;
    const auto check_external = external_changes_;
    return current && (current->isChanged() || (check_external && check_external()));
}

void Commons::closeAll() noexcept {
    for (const auto& entry : metadata_) {
        const auto& data = entry.second;
        if (data && data->tlp) attachTLPData(data->tlp->shell(), {});
    }
    metadata_.clear();
    registered_panes_.clear();
    auto panels = std::move(windows_);
    windows_.clear();
    auxiliary_windows_.fill({});
    for (const auto& [key, panel] : panels) if (panel) {
        attachTLPData(panel->shell(), {});
        panel->retireHostBindings();
    }
    // 可視性の通知は外部コールバックなので、その後は切り離したローカル
    // 状態だけを使う。ホストへ通知する前に、全制御部の接続を解除済み。
    for (const auto& [key, panel] : panels)
        if (panel) if (auto shell = panel->shell()) shell->set_visible(false);
}

void Commons::closeScenarioPanes() noexcept {
    std::vector<std::shared_ptr<TopLevelPanel>> panels;
    for (auto it = windows_.begin(); it != windows_.end();) {
        // C++の意図的な拡張として、固定の補助ペインをシナリオ変更
        // 後も保持する。refScenarioで再接続し、旧編集状態
        // を消す。Dはペインを閉じ、後の再表示用に位置を記憶する。
        if (std::find(auxiliary_keys.begin(), auxiliary_keys.end(),
                      StringView(it->first)) != auxiliary_keys.end()) {
            ++it; continue;
        }
        if (const auto panel = it->second) {
            if (const auto shell = panel->shell()) metadata_.erase(shell.get());
            panels.push_back(panel);
        }
        it = windows_.erase(it);
    }
    for (const auto& panel : panels) {
        attachTLPData(panel->shell(), {});
        panel->retireHostBindings();
    }
    for (const auto& panel : panels)
        if (auto shell = panel->shell()) shell->set_visible(false);
}

SkinPtr Commons::findSkinFromHistory(const Summary* summ) const {
    OpenHistory history;
    return findSkinFromHistory(summ, history);
}

SkinPtr Commons::findSkinFromHistory(const Summary* summ,
                                     OpenHistory& history) const {
    history = {};
    if (!prop_) return {};
    const auto hist = ::cwxeditor::cwx::editor::gui::dwt::history::createHistString(summ);
    history = ::cwxeditor::cwx::editor::gui::dwt::history::findHistory(
        prop_->var().scenarioBookmarks.value,
        prop_->var().openHistories.value, hist, services_.filesystem.get());
    // Dは記録済みのClassicのEngine・種別で、旧Summary.wsm
    // の不完全な情報を補う。検索を省くと、外部Classic
    // カードや場面のメニューカードがプロセスの既定スキンへ解
    // 決され、元画像の読み込みに成功しても空表示になり得る。
    if (!history.path.empty() &&
        (!history.skinType.empty() || !history.skinEngine.empty())) {
        if (!lib::editor_window_host_options().all_mode) {
            const String scenario_path = summ ? (!summ->readOnlyPath().empty()
                ? summ->readOnlyPath() : (summ->useTemp() ? summ->origZipName()
                                                         : summ->scenarioPath())) : String{};
            return findSkinSelection(const_cast<Commons&>(*this), *prop_, summ,
                summ && summ->legacy(), scenario_path, history.skinType,
                history.skinName, history.skinEngine, true, true, true);
        }
        return findSkin(const_cast<Commons&>(*this), *prop_, summ,
                        history.skinType, history.skinName,
                        history.skinEngine);
    }
    return findSkin(const_cast<Commons&>(*this), *prop_, summ);
}

void Commons::updateSkinMaterialsExtension(
    const usecounter::CounterOwner& counter, SkinPtr oldSkin, SkinPtr newSkin,
    SkinMaterialJournal* journal) {
    if (!counter || !prop_ || !newSkin) return;
    if (!oldSkin) {
        oldSkin = dskin::findSkin2(
            *prop_, static_cast<String>(prop_->var().defaultSkin),
            static_cast<String>(prop_->var().defaultSkinName));
    }
    if (!oldSkin || oldSkin == newSkin) return;

    const auto process = [this, &counter, journal](const lib::Array<String>& oldDirs,
                                          const lib::Array<String>& newDirs,
                                          const lib::Array<String>& extensions) {
        std::error_code error;
        for (const auto& old_dir : oldDirs) {
            const auto old_directory = structs::pathFromUtf8(old_dir);
            if (!std::filesystem::is_directory(old_directory, error)) return;
            const bool follow_links = !lib::editor_window_host_options().all_mode;
            const auto options = follow_links
                ? std::filesystem::directory_options::follow_directory_symlink
                : std::filesystem::directory_options::none;
            std::vector<std::filesystem::path> ancestors;
            if (follow_links) {
                ancestors.push_back(std::filesystem::canonical(old_directory, error));
                if (error) return;
            }
            for (std::filesystem::recursive_directory_iterator it(old_directory, options, error), end;
                 it != end; ++it) {
                const auto& entry = *it;
                if (follow_links) {
                    // DのdirEntriesはディレクトリのリンクをたどる。別
                    // 名のパスは維持するが、無限再帰せず祖先への循環で停止する。
                    const auto depth = static_cast<std::size_t>(it.depth());
                    if (ancestors.size() > depth + 1U) ancestors.resize(depth + 1U);
                    std::error_code directory_error;
                    if (entry.is_directory(directory_error)) {
                        const auto resolved = std::filesystem::canonical(entry.path(), directory_error);
                        bool cycle = false;
                        if (!directory_error) for (const auto& ancestor : ancestors) {
                            if (std::filesystem::equivalent(resolved, ancestor, directory_error)) {
                                cycle = true;
                                break;
                            }
                            if (directory_error) break;
                        }
                        if (directory_error || cycle) it.disable_recursion_pending();
                        else ancestors.push_back(resolved);
                    } else if (directory_error) {
                        it.disable_recursion_pending();
                    }
                    error.clear();
                }
                if (error || !entry.is_regular_file(error)) continue;
                const auto relative = follow_links ? entry.path().lexically_relative(old_directory)
                    : std::filesystem::relative(entry.path(), old_directory, error);
                if (error) continue;
                const auto old_relative = structs::pathToUtf8(relative);
                const auto old_id = usecounter::toPathId(old_relative);
                if (counter->get(old_id) == 0) continue;
                for (const auto& new_dir : newDirs) {
                    const auto new_directory = structs::pathFromUtf8(new_dir);
                    auto candidate = new_directory / relative;
                    if (std::filesystem::exists(candidate, error)) continue;
                    bool found = false;
                    for (const auto& extension : extensions) {
                        candidate.replace_extension(extension);
                        if (std::filesystem::exists(candidate, error)) {
                            const auto new_relative = follow_links
                                ? candidate.lexically_relative(new_directory)
                                : std::filesystem::relative(candidate, new_directory, error);
                            if (!error) {
                                const auto new_relative_utf8 =
                                    structs::pathToUtf8(new_relative);
                                const auto new_id = usecounter::toPathId(new_relative_utf8);
                                // 同じ製品側の変更から到達した下位カウンターの
                                // 履歴利用者も含め、各利用者を独立に保持する。
                                auto users = counter->values(old_id);
                                if (const auto sub = counter->sub()) {
                                    const auto retained = sub->values(old_id);
                                    users.insert(users.end(), retained.begin(), retained.end());
                                }
                                std::vector<std::uint64_t> beforeRevisions;
                                if (journal) {
                                    journal->reserve(journal->size() + users.size());
                                    beforeRevisions.reserve(users.size());
                                    for (const auto& user : users)
                                        beforeRevisions.push_back(user ? user->pathRevision() : 0);
                                }
                                const auto record = [&] {
                                    if (journal) for (std::size_t i = 0; i < users.size(); ++i) {
                                        const auto& user = users[i];
                                        if (user && user->path() == new_relative_utf8)
                                            journal->push_back({user, old_id, new_id,
                                                user->pathRevision(), beforeRevisions[i]});
                                    }
                                };
                                try { counter->change(old_id, new_id); }
                                catch (...) { record(); throw; }
                                record();
                                refPath.call(old_relative, new_relative_utf8, false);
                            }
                            found = true;
                            break;
                        }
                    }
                    if (found) break;
                }
            }
        }
    };
    process(oldSkin->tableDirs(), newSkin->tableDirs(), oldSkin->extImage());
    process(oldSkin->bgmDirs(), newSkin->bgmDirs(), oldSkin->extBgm());
    process(oldSkin->seDirs(), newSkin->seDirs(), oldSkin->extSound());
}

void Commons::restoreSkinMaterials(SkinMaterialJournal& journal) {
    // 正確な書き込みだけを逆に戻す。無関係な既存の新パス利用者や、こ
    // のトランザクションの受理後に編集された所有元を書き換えない。
    struct RestoredChain {
        std::uint64_t currentRevision{};
        std::uint64_t previousRevision{};
        bool blocked{};
    };
    std::unordered_map<usecounter::PathUser*, RestoredChain> restored;
    for (auto it = journal.rbegin(); it != journal.rend(); ++it) {
        const auto user = it->user.lock();
        if (!user) continue;
        const auto expected = restored.find(user.get());
        // 元の順方向のリビジョンで連続性を確認する。逆操作で生成した新しいリビ
        // ジョンは、コールバックが利用者を編集していないことの確認だけに使う
        // 。二項目の間の独立した書き込みを、連続したものとして結び付けない。
        if (expected != restored.end() &&
            (expected->second.blocked || it->revision != expected->second.previousRevision)) {
            expected->second.blocked = true;
            continue;
        }
        const auto revision = expected == restored.end() ? it->revision
                                                         : expected->second.currentRevision;
        if (user->pathRevision() != revision || user->path() != String(it->after)) {
            restored[user.get()].blocked = true;
            continue;
        }
        const auto route = user->useCounter();
        user->removeUseCounter();
        bool accepted{};
        try { accepted = user->change(it->before); }
        catch (...) {
            if (!user->useCounter()) user->setUseCounter(route);
            throw;
        }
        if (!user->useCounter()) user->setUseCounter(route);
        if (accepted) {
            // change()は仮想関数で、JPY/JPDCは指定されたファイル名を更新す
            // るために上書きする。id()だけでは、その基底の設定処理を迂回してしまう。
            restored[user.get()] = {user->pathRevision(), it->beforeRevision, false};
            refPath.call(String(it->after), String(it->before), false);
        } else restored[user.get()].blocked = true;
    }
    journal.clear();
}

void Commons::activate(const std::shared_ptr<TopLevelPanel>& panel,
                       const bool shellActivate) {
    if (!panel || !panel->shell()) return;
    const auto life = lifetimeToken();
    panel->shell()->set_visible(true);
    if (life.expired()) return;
    if (shellActivate && services_.gui) services_.gui->focus(panel->focusControl());
    if (life.expired()) return;

}

void Commons::openPanel(const std::shared_ptr<TopLevelPanel>& panel,
                        String key, const bool shellActivate,
                        const StringView paneFamily) {
    if (!panel) return;
    windows_[key] = panel;
    const auto life = lifetimeToken();
    const auto present = pane_presenter_;
    if (present && !paneFamily.empty()) present(key, panel, paneFamily, shellActivate);
    if (life.expired()) return;
    activate(panel, shellActivate);
}

PanelPtr Commons::panelFor(const String key, const bool shellActivate) {
    if (const auto panel = openRegisteredPanel(key, key, shellActivate)) {
        // DのopenMain!(key, "data",
        // Dir.N)は、タブがなければ固定の補助Editor
        // を左上のデータペインへ組み込む。空の系統を渡すとモ
        // デルのキャッシュ・アクティブ化だけになり、DockingFolder
        // が作られず、任意の表示が開かない。クーポ
        // ンだけは既定タブなので影響を受けにくい。
        openPanel(panel, key, shellActivate, "data");
        return std::dynamic_pointer_cast<lib::Panel>(panel->shell());
    }

    // 元のbaseShell契約との互換性を維持する。型付きファ
    // クトリーの登録前に、構築済みのネイティブShellを渡せる
    // 。非nullのShellが渡された場合だけこの経路を使い
    // 、ペインがなければ空パネルを作らずnullptrを返す。
    const auto found = std::find(auxiliary_keys.begin(), auxiliary_keys.end(),
                                 StringView(key));
    if (found == auxiliary_keys.end()) return {};
    const auto index = static_cast<std::size_t>(
        std::distance(auxiliary_keys.begin(), found));
    if (index >= auxiliary_windows_.size() || !auxiliary_windows_[index]) {
        return {};
    }
    auto panel = std::make_shared<TopLevelPanel>(auxiliary_windows_[index]);
    panel->title(key);
    retainPanelMetadata(panel);
    openPanel(panel, key, shellActivate, "data");
    return std::dynamic_pointer_cast<lib::Panel>(panel->shell());
}

bool Commons::inheritOpenedPaths(const std::shared_ptr<TopLevelPanel>& panel,
                                const std::shared_ptr<TopLevelPanel>& base) {
    if (!panel || !base) return true;
    const auto life = lifetimeToken();
    const auto paths = base->openedCWXPath();
    if (life.expired()) return false;
    const auto owner = base->viewProjection().owner;
    if (!owner) return true;
    const auto ownerPath = owner->cwxPath(true);
    for (const auto& value : paths) {
        const auto relative = path::cprel(value, ownerPath);
        if (relative.empty()) continue;
        (void)panel->openCWXPath(relative, false);
        if (life.expired()) return false;
    }
    return true;
}

std::shared_ptr<TopLevelPanel> Commons::openBoundPanel(
    String key, String path_value, std::shared_ptr<void> source,
    const bool shellActivate, std::shared_ptr<TopLevelPanel> duplicateBase) {
    const auto life = lifetimeToken();
    if (duplicateBase) {
        const auto prefix = key;
        auto suffix = windows_.size() + 1U;
        do { key = appendDuplicate(prefix, suffix++); } while (windows_.contains(key));
    }
    auto panel = openRegisteredPanel(key, path_value, shellActivate, std::move(source));
    if (life.expired() || !panel) return {};
    panel->rememberCWXPath(path_value);
    if (!inheritOpenedPaths(panel, duplicateBase) || life.expired()) return {};
    openPanel(panel, key, shellActivate, "work");
    return life.expired() ? std::shared_ptr<TopLevelPanel>{} : panel;
}

std::shared_ptr<TopLevelPanel> Commons::duplicatePanel(
    std::shared_ptr<TopLevelPanel> base, const bool shellActivate) {
    if (!base) return {};
    // ウィンドウの複製は、同じ入力元を通常のファクトリーで開く二つ目の表示。
    // モデルの複製や印だけのパネルでは、現在開いているパスを提供できない。
    String key;
    for (const auto& [candidate, panel] : windows_) if (panel == base) { key = candidate; break; }
    if (key.empty()) return {};
    const auto data = tlpData(base->shell());
    const auto owner = base->viewProjection().owner;
    if (!data || !owner) return {};
    return openBoundPanel(std::move(key), owner->cwxPath(true), data->main,
                          shellActivate, std::move(base));
}

std::shared_ptr<TopLevelPanel> Commons::openAreaScene(
    Props&, Summary* summary, AreaPtr area, const bool shellActivate,
    std::shared_ptr<TopLevelPanel> duplicateBase) {
    if (!area) return {};
    std::shared_ptr<void> source;
    if (summary) source = summary->weak_from_this().lock();
    if (!duplicateBase) {
        if (auto existing = sceneWindowFrom(
                "area.scene:", area.get(),
                source ? source.get() : static_cast<const void*>(area.get()),
                shellActivate))
            return existing;
    }
    String path_value = area->cwxPath(true);
    String key = "area.scene:" + path_value;
    if (summary)
        key += "#source" + std::to_string(reinterpret_cast<std::uintptr_t>(summary));
    return openBoundPanel(std::move(key), std::move(path_value),
                          source ? source : std::static_pointer_cast<void>(area), shellActivate, std::move(duplicateBase));
}

std::shared_ptr<TopLevelPanel> Commons::openAreaEvent(
    Props&, Summary* summary, AreaPtr area, const bool shellActivate,
    std::shared_ptr<TopLevelPanel> duplicateBase) {
    if (!area) return {};
    // DのrOpenは、複製も含め同じ所有元の残存EventWindowを再利用する。
    if (!duplicateBase) {
        if (const auto existing = eventWindowFrom(summary, static_cast<const area::AbstractArea*>(area.get()))) {
            for (const auto& [key, panel] : windows_) if (panel == existing) {
                const auto selected_key = key;
                openPanel(existing, selected_key, shellActivate, "work");
                return existing;
            }
        }
    }
    String path_value = area->cwxPath(true);
    String key = "area.event:" + path_value;
    std::shared_ptr<void> source;
    if (summary) {
        source = summary->weak_from_this().lock();
        key += "#source" + std::to_string(reinterpret_cast<std::uintptr_t>(summary));
    }
    return openBoundPanel(std::move(key), std::move(path_value),
                          source ? source : std::static_pointer_cast<void>(area), shellActivate, std::move(duplicateBase));
}

std::shared_ptr<TopLevelPanel> Commons::openAreaScene(
    Props&, Summary* summary, BattlePtr area, const bool shellActivate,
    std::shared_ptr<TopLevelPanel> duplicateBase) {
    if (!area) return {};
    std::shared_ptr<void> source;
    if (summary) source = summary->weak_from_this().lock();
    if (!duplicateBase) {
        if (auto existing = sceneWindowFrom(
                "battle.scene:", area.get(),
                source ? source.get() : static_cast<const void*>(area.get()),
                shellActivate))
            return existing;
    }
    String path_value = area->cwxPath(true);
    String key = "battle.scene:" + path_value;
    if (summary)
        key += "#source" + std::to_string(reinterpret_cast<std::uintptr_t>(summary));
    return openBoundPanel(std::move(key), std::move(path_value),
                          source ? source : std::static_pointer_cast<void>(area), shellActivate, std::move(duplicateBase));
}

std::shared_ptr<TopLevelPanel> Commons::openAreaEvent(
    Props&, Summary* summary, BattlePtr area, const bool shellActivate,
    std::shared_ptr<TopLevelPanel> duplicateBase) {
    if (!area) return {};
    // DのrOpenは、複製も含め同じ所有元の残存EventWindowを再利用する。
    if (!duplicateBase) {
        if (const auto existing = eventWindowFrom(summary, area.get())) {
            for (const auto& [key, panel] : windows_) if (panel == existing) {
                const auto selected_key = key;
                openPanel(existing, selected_key, shellActivate, "work");
                return existing;
            }
        }
    }
    String path_value = area->cwxPath(true);
    String key = "battle.event:" + path_value;
    std::shared_ptr<void> source;
    if (summary) {
        source = summary->weak_from_this().lock();
        key += "#source" + std::to_string(reinterpret_cast<std::uintptr_t>(summary));
    }
    return openBoundPanel(std::move(key), std::move(path_value),
                          source ? source : std::static_pointer_cast<void>(area), shellActivate, std::move(duplicateBase));
}

std::shared_ptr<TopLevelPanel> Commons::openArea(
    Props&, Summary* summary, PackagePtr area, const bool shellActivate,
    std::shared_ptr<TopLevelPanel> duplicateBase) {
    if (!area) return {};
    // DのrOpenは、複製も含め同じ所有元の残存EventWindowを再利用する。
    if (!duplicateBase) {
        if (const auto existing = eventWindowFrom(summary, area.get())) {
            for (const auto& [key, panel] : windows_) if (panel == existing) {
                const auto selected_key = key;
                openPanel(existing, selected_key, shellActivate, "work");
                return existing;
            }
        }
    }
    String path_value = area->cwxPath(true);
    String key = "package:" + path_value;
    std::shared_ptr<void> source;
    if (summary) {
        source = summary->weak_from_this().lock();
        key += "#source" + std::to_string(reinterpret_cast<std::uintptr_t>(summary));
    }
    return openBoundPanel(std::move(key), std::move(path_value),
                          source ? source : std::static_pointer_cast<void>(area), shellActivate, std::move(duplicateBase));
}

std::shared_ptr<TopLevelPanel> Commons::openHands(
    Props& prop, Summary* summ, CastCardPtr card_value, const bool shellActivate) {
    return openAddHands(prop, summ, std::move(card_value), nullptr, shellActivate);
}

std::shared_ptr<TopLevelPanel> Commons::openAddHands(
    Props& prop, Summary* summ, CastCardPtr card_value, Summary* toc,
    const bool shellActivate) {
    (void)prop;
    if (!summ || !card_value) return {};
    auto request = std::make_shared<HandPaneRequest>();
    request->source = summ->weak_from_this().lock();
    request->card = card_value;
    if (toc) request->destination = toc->weak_from_this().lock();
    if (!request->source || (toc && !request->destination)) return {};
    // ドック復元用の保存パスキーは保持する。ただし番号
    // 変更後のカードパスから新キーを作る前に、安定した
    // CastCard所有元で有効な手札ホストを探す。
    for (const auto& [existing_key, existing] : windows_) {
        if (!existing || !existing_key.starts_with("hands:") ||
            existing->viewProjection().owner.get() != card_value.get())
            continue;
        const auto data = tlpData(existing->shell());
        if (!data) continue;
        if (data->main) {
            const auto existing_request =
                std::static_pointer_cast<HandPaneRequest>(data->main);
            if (existing_request->source != request->source ||
                existing_request->card != request->card ||
                existing_request->destination != request->destination)
                continue;
        } else if (toc || summary_.get() != summ) {
            // 復元された通常の手札には、開く要求のメタデータがない。現在
            // のSummaryだけに属し、取り込みペインを推測しない。
            continue;
        }
        existing->rememberCWXPath(card_value->cwxPath(true));
        openPanel(existing, existing_key, shellActivate, "side");
        return existing;
    }
    String key = "hands:" + card_value->cwxPath(true);
    if (lib::editor_window_host_options().all_mode) {
        if (toc) key += "#import:" + toc->cwxPath(true);
    } else {
        const auto identity = [](const auto* value) {
            return std::to_string(reinterpret_cast<std::uintptr_t>(value));
        };
        key += "#identity:" + identity(request->source.get()) + ":" +
            identity(request->card.get());
        if (toc) key += "#destination:" + identity(request->destination.get());
    }
    auto panel = openRegisteredPanel(key, key, shellActivate, request);
    if (!panel) return {};
    auto projection = panel->viewProjection();
    projection.owner = card_value;
    panel->viewProjection(std::move(projection));
    panel->rememberCWXPath(card_value->cwxPath(true));
    // Dは編集可能な手札と取り込み元の手札の両方を、側面ペインに表示する。
    openPanel(panel, key, shellActivate, "side");
    return panel;
}

std::shared_ptr<TopLevelPanel> Commons::openUseEvents(
    Props&, Summary* source, EffectCardPtr card_value, const bool shellActivate,
    std::shared_ptr<TopLevelPanel> duplicateBase) {
    if (!source || !card_value) return {};
    auto request = std::make_shared<UseEventRequest>();
    request->source = source->weak_from_this().lock();
    request->card = card_value;
    if (!request->source) return {};
    // DのrOpenは、変更される数値の資源パスではなく、有効な
    // カードを比較する。元の画面が閉じた後の複製も再利用し、復元
    // ホストも同じ検索に参加できるよう、有効な登録一覧を調べる。
    if (!duplicateBase) {
        for (const auto& [existing_key, panel] : windows_) {
            if (existing_key.starts_with("use:") && panel &&
                panel->viewProjection().owner.get() == card_value.get()) {
                auto result = panel;
                const auto selected_key = existing_key;
                openPanel(result, selected_key, shellActivate, "work");
                return result;
            }
        }
    }
    String key = "use:" + card_value->cwxPath(true) + "#owner" +
        std::to_string(reinterpret_cast<std::uintptr_t>(card_value.get()));
    return openBoundPanel(std::move(key), card_value->cwxPath(true), request,
                          shellActivate, std::move(duplicateBase));
}

void Commons::openDataWin(const bool shellActivate) {
    (void)panelFor("data", shellActivate);
}

PanelPtr Commons::openFlagWin(const bool shellActivate) {
    return panelFor("flag", shellActivate);
}

PanelPtr Commons::openCardPane(const std::int32_t cardType,
                               const bool shellActivate) {
    static constexpr std::array<StringView, 5> names = {
        "castCard", "skillCard", "itemCard", "beastCard", "infoCard"};
    // Dは網羅されたCardTypeを受け取るが、こ
    // のC++境界は基底の整数を受け取る。列挙範囲外
    // では、黙ってCastペインを開かず拒否する。
    if (cardType < 0 || cardType >= static_cast<int>(names.size())) return {};
    return panelFor(String(names[static_cast<std::size_t>(cardType)]),
                    shellActivate);
}

PanelPtr Commons::openCastWin(const bool shellActivate) {
    return panelFor("castCard", shellActivate);
}
PanelPtr Commons::openSkillWin(const bool shellActivate) {
    return panelFor("skillCard", shellActivate);
}
PanelPtr Commons::openItemWin(const bool shellActivate) {
    return panelFor("itemCard", shellActivate);
}
PanelPtr Commons::openBeastWin(const bool shellActivate) {
    return panelFor("beastCard", shellActivate);
}
PanelPtr Commons::openInfoWin(const bool shellActivate) {
    return panelFor("infoCard", shellActivate);
}
void Commons::openDirWin(const bool shellActivate) {
    (void)panelFor("file", shellActivate);
}
void Commons::openCouponWin(const bool shellActivate) {
    (void)panelFor("coupon", shellActivate);
}
void Commons::openGossipWin(const bool shellActivate) {
    (void)panelFor("gossip", shellActivate);
}
void Commons::openCompleteStampWin(const bool shellActivate) {
    (void)panelFor("completeStamp", shellActivate);
}
void Commons::openKeyCodeWin(const bool shellActivate) {
    (void)panelFor("keyCode", shellActivate);
}
void Commons::openCellNameWin(const bool shellActivate) {
    (void)panelFor("cellName", shellActivate);
}
void Commons::openCardGroupWin(const bool shellActivate) {
    (void)panelFor("cardGroup", shellActivate);
}

std::shared_ptr<TopLevelPanel> Commons::areaWindowFrom(
    const StringView cwxPath, const bool shellActivate) {
    static_cast<void>(shellActivate); // DはSceneウィンドウをアクティブ化せず問い合わせる。
    if (!summary_) return {};
    const auto owner = summary_->findCWXPath(cwxPath);
    if (!owner) return {};
    const auto windows = windows_;
    for (const auto& [key, panel] : windows) {
        if (!panel || (key.rfind("area.scene:", 0) != 0 &&
                       key.rfind("battle.scene:", 0) != 0)) continue;
        const auto& projection = panel->viewProjection();
        if (projection.owner == owner &&
            (projection.kind == ViewProjection::Kind::Area ||
             projection.kind == ViewProjection::Kind::Battle)) return panel;
    }
    return {};
}

std::shared_ptr<TopLevelPanel> Commons::sceneWindowFrom(
    const StringView keyPrefix, const path::CWXPath* owner,
    const void* sourceIdentity, const bool shellActivate) {
    if (!owner) return {};
    const auto windows = windows_;
    for (const auto& [key, panel] : windows) {
        if (!panel || !key.starts_with(keyPrefix)) continue;
        if (panel->viewProjection().owner.get() != owner) continue;
        const auto data = tlpData(panel->shell());
        if (!data || !data->main || data->main.get() != sourceIdentity)
            continue;
        panel->rememberCWXPath(panel->viewProjection().owner->cwxPath(true));
        openPanel(panel, key, shellActivate, "work");
        return panel;
    }
    return {};
}

std::shared_ptr<TopLevelPanel> Commons::eventWindowFrom(
    const Summary* source, const path::CWXPath* owner) const {
    if (!source || !owner) return {};
    for (const auto& [key, panel] : windows_) {
        if (!panel || (!key.starts_with("area.event:") &&
                       !key.starts_with("battle.event:") &&
                       !key.starts_with("package:"))) continue;
        const auto& projection = panel->viewProjection();
        const auto data = tlpData(panel->shell());
        // retireHostBindingsがこの投影を解除する。入力元を
        // 持つペイン情報はSummaryを保存し、異なる取り込みの同一パスを
        // 区別する。復元した主シナリオのペインには入力元所有者の情報がなく、
        // ファクトリーはこのCommonsの現在のSummaryだけを解決
        // する。取り込みは明示的な入力元情報を保持し、この代替を使わない。
        const bool same_source = data && (data->main
            ? data->main.get() == source : summary_.get() == source);
        if (projection.kind == ViewProjection::Kind::Event && projection.view &&
            projection.owner.get() == owner && same_source)
            return panel;
    }
    return {};
}

std::shared_ptr<TopLevelPanel> Commons::eventWindowFrom(
    const StringView cwxPath, const bool shellActivate) {
    const auto life = lifetimeToken();
    const auto windows = windows_;
    for (const auto& [key, panel] : windows) {
        if (key.find("event:") == String::npos &&
            key.find("use:") == String::npos) {
            continue;
        }
        const auto paths = panel ? panel->openedCWXPath() : std::vector<String>{};
        if (life.expired()) return {};
        if (panel && std::find(paths.begin(), paths.end(), cwxPath) != paths.end()) {
            activate(panel, shellActivate);
            return panel;
        }
    }
    return {};
}

std::vector<PanelPtr> Commons::eventViewsFrom(const CWXPathPtr& owner,
                                             const bool shellActivate) {
    (void)shellActivate; // Dの問い合わせは、フォーカスを変えずペインも開かない。
    std::vector<PanelPtr> result;
    if (!owner) return result;
    for (const auto& [key, panel] : windows_) {
        if (!panel) continue;
        const auto& p = panel->viewProjection();
        if (p.kind == ViewProjection::Kind::Event && p.owner && p.view &&
            dynamic_cast<void*>(p.owner.get()) == dynamic_cast<void*>(owner.get()))
            result.push_back(p.view);
    }
    return result;
}

bool Commons::sameEventTreeIdentity(const EventTreePtr& left,
                                    const EventTreePtr& right) noexcept {
    if (!left || !right) return !left && !right;
    return left == right || (!left->objectId().empty() &&
                             left->objectId() == right->objectId());
}

CWXPathPtr Commons::canonicalEventOwner(event::EventTreeOwner* owner) {
    return owner ? owner->findCWXPath(StringView{}) : CWXPathPtr{};
}

bool Commons::sameOwnerIdentity(const CWXPathPtr& left,
                                const CWXPathPtr& right) noexcept {
    return left && right &&
           dynamic_cast<void*>(left.get()) == dynamic_cast<void*>(right.get());
}

std::vector<PanelPtr> Commons::eventTreeViewFrom(
    const CWXPathPtr& owner, const EventTreePtr& tree, const bool shellActivate) {
    (void)shellActivate;
    std::vector<ViewProjection> projections;
    for (const auto& [key, panel] : windows_)
        if (panel && panel->viewProjection().kind == ViewProjection::Kind::Event)
            projections.push_back(panel->viewProjection());
    std::vector<PanelPtr> result;
    CWXPathPtr targetOwner;
    if (tree) {
        auto* const treeOwner = tree->owner();
        if (!treeOwner) return result;
        // EventWindowのカードアダプターは、空パスを実際のカードへ解決する
        // 。Area、Battle、Packageは自身の正規の所有元へ解決する。
        targetOwner = canonicalEventOwner(treeOwner);
    } else {
        // Dがパス引数を使うのは、選択ツリーの表示が空の場合だけ。
        targetOwner = owner;
    }
    if (!targetOwner) return result;
    const auto life = lifetimeToken();
    for (const auto& p : projections) {
        if (!p.treeView || !p.selectedTree || !p.owner ||
            !sameOwnerIdentity(p.owner, targetOwner)) continue;
        const auto selected = p.selectedTree();
        // 投影用コールバックが共通ホストを同期的に終了させる場合がある。
        if (life.expired()) return {};
        if (sameEventTreeIdentity(selected, tree)) result.push_back(p.treeView);
    }
    return result;
}

std::shared_ptr<TopLevelPanel> Commons::handCardWindowFrom(
    Props& prop, Summary* summ, CastCardPtr card_value, const bool open,
    const bool shellActivate) {
    if (!card_value) return {};
    // DのCommons.handCardWindowFromは、変更可能なID由来のドック
    // キーとは独立に、正確なCastCard所有元で有効なCardWindowを照合する。
    for (const auto& [key, panel] : windows_) {
        if (!panel || !key.starts_with("hands:") ||
            panel->viewProjection().owner.get() != card_value.get()) continue;
        return panel;
    }
    return open ? openHands(prop, summ, std::move(card_value), shellActivate)
                : std::shared_ptr<TopLevelPanel>{};
}
PanelPtr Commons::getContentsToolBox(const PanelPtr& treeView) {
    if (!treeView || *palette_transfer_active_) return {};
    const auto transfer = palette_transfer_active_;
    *transfer = true;
    struct TransferGuard {
        std::shared_ptr<bool> active;
        ~TransferGuard() { *active = false; }
    } guard{transfer};
    std::vector<ViewProjection> projections;
    for (const auto& [key, panel] : windows_)
        if (panel && panel->viewProjection().kind == ViewProjection::Kind::Event)
            projections.push_back(panel->viewProjection());
    const auto it = std::find_if(projections.begin(), projections.end(),
        [&](const auto& p) { return p.treeView == treeView; });
    if (it == projections.end() || !it->toolbox) return {};
    const auto recipient = *it;
    const auto life = lifetimeToken();
    const auto registered = [this](const ViewProjection& projection) {
        return std::any_of(windows_.begin(), windows_.end(), [&](const auto& entry) {
            const auto current = entry.second ? entry.second->viewProjection() : ViewProjection{};
            return current.kind == ViewProjection::Kind::Event &&
                current.treeView == projection.treeView && current.logicalShell == projection.logicalShell;
        });
    };
    const bool reusable = prop_ && (prop_->var().contentsFloat.value ||
                                    prop_->var().contentsAutoHide.value);
    if (reusable && recipient.adoptToolbox) {
        for (const auto& donor : projections) {
            if (donor.treeView == treeView || donor.logicalShell != recipient.logicalShell ||
                !donor.toolbox || !donor.releaseToolbox) continue;
            const auto box = donor.toolbox();
            if (life.expired()) return {};
            const bool singleton = donor.toolboxSingleton && donor.toolboxSingleton();
            if (life.expired()) return {};
            if (!box || (!singleton && projectionVisible(donor))) continue;
            if (!registered(donor) || !registered(recipient)) return {};
            auto payload = donor.releaseToolbox();
            if (life.expired() || !registered(recipient)) return {};
            // 削除された受信先が、解放済みのホストを再取得してはならない
            // 。再入による終了が先なら、保持したデータは自身を閉じる。
            const auto adopted = payload ? recipient.adoptToolbox(std::move(payload)) : PanelPtr{};
            if (life.expired() || !registered(recipient)) return {};
            return adopted;
        }
    }
    const auto box = recipient.ensureToolbox ? recipient.ensureToolbox() : recipient.toolbox();
    if (life.expired() || !registered(recipient)) return {};
    return box;
}

bool Commons::poolContentsToolBox(const PanelPtr& box) {
    if (!box || *palette_transfer_active_) return false;
    const auto transfer = palette_transfer_active_;
    *transfer = true;
    struct TransferGuard {
        std::shared_ptr<bool> active;
        ~TransferGuard() { *active = false; }
    } guard{transfer};
    std::vector<ViewProjection> projections;
    for (const auto& [key, panel] : windows_)
        if (panel && panel->viewProjection().kind == ViewProjection::Kind::Event)
            projections.push_back(panel->viewProjection());
    const auto life = lifetimeToken();
    const auto registered = [this](const ViewProjection& projection) {
        return std::any_of(windows_.begin(), windows_.end(), [&](const auto& entry) {
            const auto current = entry.second ? entry.second->viewProjection() : ViewProjection{};
            return current.kind == ViewProjection::Kind::Event && !current.readOnly &&
                current.treeView == projection.treeView && current.logicalShell == projection.logicalShell;
        });
    };
    std::optional<ViewProjection> donor;
    for (const auto& p : projections) {
        if (!p.toolbox) continue;
        const auto current = p.toolbox();
        if (life.expired()) return false;
        if (current == box) { donor = p; break; }
    }
    if (!donor || !donor->releaseToolbox) return false;
    for (const auto& recipient : projections) {
        if (recipient.treeView == donor->treeView || recipient.readOnly ||
            recipient.logicalShell != donor->logicalShell || !recipient.toolbox ||
            !recipient.adoptToolbox) continue;
        const auto current = recipient.toolbox();
        if (life.expired()) return false;
        if (current) continue;
        if (!registered(*donor) || !registered(recipient)) return false;
        auto payload = donor->releaseToolbox();
        if (life.expired() || !payload || !registered(recipient)) return false;
        const auto adopted = recipient.adoptToolbox(std::move(payload));
        if (life.expired() || !adopted || !registered(recipient)) return false;
        const auto installed = recipient.toolbox();
        if (life.expired()) return false;
        return installed == adopted && registered(recipient);
    }
    return false;
}

void Commons::eventTreeHost(const PanelPtr& treeView, const ContainerPtr& shell) {
    if (!treeView || !shell) return;
    for (const auto& [key, panel] : windows_) {
        if (!panel) continue;
        auto projection = panel->viewProjection();
        if (projection.kind != ViewProjection::Kind::Event || projection.treeView != treeView) continue;
        projection.logicalShell = shell;
        panel->viewProjection(std::move(projection));
    }
}

void Commons::reconstructContentsToolBox() {
    std::vector<ViewProjection> projections;
    for (const auto& [key, panel] : windows_)
        if (panel && panel->viewProjection().kind == ViewProjection::Kind::Event)
            projections.push_back(panel->viewProjection());
    const auto life = lifetimeToken();
    const bool singleton = prop_ && (prop_->var().contentsFloat.value ||
                                     prop_->var().contentsAutoHide.value);
    // Dはすべてのボックスを破棄してから、可視で非浮動の一式を再構築する。
    for (const auto& p : projections) {
        if (p.releaseToolbox) (void)p.releaseToolbox();
        if (life.expired()) return;
    }
    for (const auto& p : projections) {
        if (p.treeView && (projectionVisible(p) || !singleton))
            (void)getContentsToolBox(p.treeView);
        if (life.expired()) return;
    }
}

void Commons::addScenario(Props& prop) {
    static_cast<void>(prop);
    if (!summary_) return;
    refScenario.call(summary_);
}

void Commons::addScenario(Props& prop, const std::vector<String>& paths,
                          lib::Action failure) {
    static_cast<void>(prop);
    selected_files_ = paths;
    if (paths.empty()) {
        if (failure) failure();
        return;
    }
    refPaths.call(paths.front());
    for (std::size_t i = 1; i < paths.size(); ++i) refPaths.call(paths[i]);
}

void Commons::doImport(Summary* to, Summary* from,
                       const std::vector<String>& resourcePaths) {
    selected_files_ = resourcePaths;
    if (!to || !from || !prop_ || resourcePaths.empty()) return;
    const auto publish = import_publication_;
    if (!publish) return;
    const auto life = lifetimeToken();

    // Dはどちらのシナリオも変更する前に、二つのモーダルダイアログ
    // を開く。一つ目で取り込み規則を保存し、二つ目で計算済みの計
    // 画をチェックした資源に絞る。取消では対象に変更を見せない。
    smalldialogs::Callbacks callbacks;
    callbacks.services = services_;
    smalldialogs::ImportOptionDialog optionDialog(*prop_, mainShell(), to->legacy(),
                                                   callbacks, services_);
    if (!optionDialog.open()) return;
    if (life.expired()) return;
    const auto planned = importutils::importResource(
        prop_->parent(), *to, *from, resourcePaths, optionDialog.option());
    smalldialogs::ImportResultDialog resultDialog(*prop_, to, mainShell(), planned,
                                                   callbacks, services_, skin_);
    if (!resultDialog.open()) return;
    if (life.expired()) return;
    const auto result = resultDialog.checkedResult();

    applyImportedMaterials(result);
    // 通常の型付きペインを通じて公開する。共通の追加コマンド
    // が、履歴、選択、順序付き挿入、モデル通知を管理する。
    publish(to, result);
    if (life.expired()) return;
    refreshToolBar();
}

DialogPtr Commons::editScEventTemplate(WindowPtr shell) {
    const auto life = lifetimeToken();
    if (!prop_ || !summary_ || life.expired()) return {};
    if (event_template_editor_) {
        const auto existing = event_template_editor_;
        if (event_template_summary_.lock() == summary_ && existing->dialog() &&
            existing->dialog()->visible()) {
            existing->active();
            return existing->dialog();
        }
        event_template_editor_.reset();
        event_template_summary_.reset();
        existing->forceCancel();
        if (life.expired()) return {};
    }
    const std::weak_ptr<Summary> source = summary_;
    smalldialogs::Callbacks callbacks;
    callbacks.services = services_;
    callbacks.scenarioNameChanged = [this, life, source] {
        if (!life.expired() && source.lock() == summary_) refEventTemplates.call();
    };
    callbacks.subscribeScenarioName = [this, life](lib::Action handler) {
        const auto token = refScenarioName.add(std::move(handler));
        return lib::Action([this, life, token] {
            if (!life.expired()) refScenarioName.remove(token);
        });
    };
    callbacks.subscribeScenario = [this, life](std::function<void(SummaryPtr)> handler) {
        const auto token = refScenario.add(std::move(handler));
        return lib::Action([this, life, token] {
            if (!life.expired()) refScenario.remove(token);
        });
    };
    auto editor = std::make_shared<smalldialogs::EventTemplateDialog>(
        *prop_, summary_.get(), std::move(shell), summary_->eventTemplates(),
        std::move(callbacks), services_);
    event_template_editor_ = editor;
    event_template_summary_ = summary_;
    editor->closeEvent.push_back([this, life, weak = std::weak_ptr(editor)] {
        if (!life.expired() && event_template_editor_ == weak.lock()) {
            event_template_editor_.reset();
            event_template_summary_.reset();
        }
    });
    const auto dialog = editor->dialog();
    (void)editor->open();
    return dialog;
}

void Commons::setTitle(const ContainerPtr& component, String text) {
    if (!component) return;
    if (auto window = std::dynamic_pointer_cast<lib::Window>(component)) {
        window->title = text;
    }
    for (auto& [key, panel] : windows_) {
        static_cast<void>(key);
        if (panel && panel->shell().get() == component.get()) panel->title(text);
    }
}

void Commons::close(const ContainerPtr& component) noexcept {
    if (!component) return;
    const auto closing = closing_components_;
    if (std::find(closing->begin(), closing->end(), component.get()) != closing->end()) return;
    closing->push_back(component.get());
    struct CloseGuard {
        std::shared_ptr<std::vector<const lib::Widget*>> closing;
        const lib::Widget* widget;
        ~CloseGuard() { std::erase(*closing, widget); }
    } guard{closing, component.get()};
    const auto life = lifetimeToken();
    std::vector<std::shared_ptr<TopLevelPanel>> panels;
    for (const auto& [key, panel] : windows_)
        if (panel && panel->shell().get() == component.get()) panels.push_back(panel);
    // Dは所有元が登録されている間、保持したパレットを再利用用に保管する
    // 。未登録ならホスト終了時に、最後の所有パレットを通常どおり閉じる。
    for (const auto& panel : panels) {
        const auto projection = panel->viewProjection();
        if (projection.kind != ViewProjection::Kind::Event || !projection.toolbox) continue;
        try {
            const auto box = projection.toolbox();
            if (life.expired()) return;
            if (box) (void)poolContentsToolBox(box);
        } catch (...) {
            // 終了処理はnoexceptを維持する。再利用用に保管しなかったボックスは、終了中の所有元が破棄する。
        }
        if (life.expired()) return;
    }
    for (auto it = windows_.begin(); it != windows_.end();) {
        if (std::find(panels.begin(), panels.end(), it->second) != panels.end())
            it = windows_.erase(it);
        else ++it;
    }
    metadata_.erase(component.get());
    attachTLPData(component, {});
    for (const auto& panel : panels) {
        panel->retireHostBindings();
        if (life.expired()) return;
    }
    component->set_visible(false);
}

void Commons::setStatusLine(const WidgetPtr& base, String status,
                            const bool refMain) {
    const auto life = lifetimeToken();
    const auto main = main_;
    const auto gui = services_.gui;
    const auto data = tlpData(base);
    // C++の意図的な拡張。CommonsにネイティブMainWindowが接続さ
    // れていなくても、組み込みホストはペイン内の状態表示を保持できる。ペインが公開
    // 済みの状態を受け取るため、status_text_を再度呼ばずにキャッシュ
    // する。ImportSourceの転送先はCardWindow::setStatusLine
    // を経てCommonsへ戻るので、再呼び出しは再帰を起こす。
    if (data && data->tlp) data->tlp->status_ = status;
    if (life.expired() || !base || !refMain || !main) return;

    // DのControl.isVisible()は、選択タブと可視Shellも考慮する。仮想GUI
    // への問い合わせでCommonsが終了しても安全なよう、ホストのサービスを取得して保持する。
    const auto visible = [main, gui](const WidgetPtr& control) {
        if (!control || !control->visible()) return false;
        if (lib::widget_is_visible_in(main, control.get())) return true;
        if (lib::contains_widget(main, control.get())) return false;
        if (gui) {
            if (const auto shell = gui->window_owner(control))
                return lib::widget_is_visible_in(shell, control.get());
        }
        return control->visible();
    };
    if (!visible(base) || life.expired()) return;
    const auto focus = gui ? gui->focused_widget() : WidgetPtr{};
    if (life.expired() || !visible(focus) || life.expired()) return;
    const auto focused_data = tlpData(focus);
    if (focused_data && focused_data != data) return;

    status_line_ = status;
    main->accessibility_description = status_line_;
    const auto publish = main_status_publisher_;
    if (publish) publish(std::move(status));
}

String Commons::statusLine() const { return status_line_; }

bool Commons::openCWXPath(String path_value, bool shellActivate) {
    if (path_value.empty()) return false;
    // DはCommonsの登録ペインへの代替より先に、MainWindowを経由する。
    const auto router = cwx_path_router_;
    if (router) {
        if (const auto routed = router(path_value, shellActivate))
            return *routed;
        shellActivate = shellActivate &&
            !path::cphasattr(path_value, "nofocus");
    }
    return openCWXPathFallback(std::move(path_value), shellActivate);
}

bool Commons::openCWXPathFallback(String path_value,
                                  const bool shellActivate) {
    if (path_value.empty()) return false;
    if (const auto area_panel = areaWindowFrom(path_value, false)) {
        activate(area_panel, shellActivate);
        return static_cast<bool>(area_panel);
    }
    auto panel = openRegisteredPanel("path:" + path_value, path_value,
                                     shellActivate);
    if (!panel) return false;
    if (!panel->openCWXPath(path_value, shellActivate)) return false;
    openPanel(panel, "path:" + path_value, shellActivate);
    return true;
}

bool Commons::openFilePath(String path_value, const bool shellActivate,
                           const bool deselectEtc) {
    const auto life = lifetimeToken();
    const auto select = file_selection_;
    openDirWin(shellActivate);
    if (life.expired() || !select) return false;
    const bool result = select(path_value, deselectEtc);
    if (life.expired()) return result;
    if (result) {
        if (deselectEtc) selected_files_.clear();
        if (std::ranges::find(selected_files_, path_value) == selected_files_.end())
            selected_files_.push_back(std::move(path_value));
    }
    return result;
}

void Commons::replacePath(String from, const bool start) {
    const auto callback = path_replacement_;
    if (callback) callback(std::move(from), start);
}

void Commons::selectName(String key, std::vector<String> ids, const bool shellActivate) {
    const auto life = lifetimeToken();
    const auto select = name_selection_;
    if (!select || !panelFor(key, shellActivate) || life.expired()) return;
    const auto count = ids.size();
    // DのGCは受信先を保持する。C++はアクティブ化通知でこの調
    // 整役が終了したら停止し、選択の後に受信先へアクセスしない。
    selected_id_count_ = count;
    select(std::move(key), std::move(ids));
}

bool Commons::selectSummary(const bool shellActivate) {
    openDataWin(shellActivate);
    return static_cast<bool>(summary_);
}

std::uint64_t Commons::createPackage(const EventTreePtr& baseTree,
                                     String name, const bool shellActivate) {
    // Dataペインを開く処理が再入してCommonsを終了し得る。作成処理
    // を読む前にトークンを確認し、そのコールバックのローカルコピーを呼ぶ。
    const auto life = lifetimeToken();
    openDataWin(shellActivate);
    if (life.expired()) return 0U;
    const auto create_package = package_tree_creator_;
    if (!create_package) return 0U;
    return create_package(baseTree, std::move(name), shellActivate);
}

std::uint64_t Commons::createPackage(const ContentPtr& baseStart, String name,
                                     const bool shellActivate) {
    // Contentの経路も、EventTreeと同じ保護された経路に置く。
    const auto life = lifetimeToken();
    openDataWin(shellActivate);
    if (life.expired()) return 0U;
    const auto create_package = package_content_creator_;
    if (!create_package) return 0U;
    return create_package(baseStart, std::move(name), shellActivate);
}

void Commons::refreshWallpaper(Props& prop) {
    const auto value = static_cast<String>(prop.var().wallpaper);
    if (value.empty()) {
        wallpaper_ = {};
        refWallpaper.call();
        return;
    }
    auto path_value = structs::pathFromUtf8(value);
    if (path_value.is_relative()) {
        path_value = structs::pathFromUtf8(prop.parent().appPath()).parent_path() /
                     path_value;
    }
    wallpaper_ = prop.images().load(path_value);
    refWallpaper.call();
}

bool Commons::updateJpy1Files(const bool forceUpdate) {
    if (!summary_ || !prop_ || !sync_) return false;
    return summary_->updateJpy1Files(prop_->parent(),
                                     static_cast<bool>(prop_->var().autoUpdateJpy1File) ||
                                         forceUpdate,
                                     *sync_);
}

void Commons::warningHandleCount(const bool value) {
    if (warning_handle_count_ == value) return;
    warning_handle_count_ = value;
    if (!value) return;

    stop_open_ = true;
    if (!prop_ || !services_.dialogs) return;

    // Dはこの遷移をDWTMessageBoxで示す。C++では共通DialogBackend
    // を使い、先に状態を更新して一度だけ通知する規則を維持する。
    const auto& messages = prop_->requiredMsgs();
    lib::MessageDialogRequest request;
    request.title = messages.dlgTitWarning.value;
    request.message = messages.warningWindowsHandleCount.value;
    request.severity = lib::MessageSeverity::Warning;
    request.buttons = lib::MessageButtons::OK;
    (void)services_.dialogs->message(request);
}

SkinPtr findSkin(Commons& comm, Props& prop, const Summary* summ,
                 std::optional<String> type, std::optional<String> name,
                 String legacyEngine,
                 const bool appendClassicSkin) {
    const bool legacy = summ && summ->legacy();
    const String scenario_path =
        summ ? (!summ->readOnlyPath().empty()
                    ? summ->readOnlyPath()
                    : (summ->useTemp() ? summ->origZipName()
                                       : summ->scenarioPath()))
             : String{};
    // Dのnullと空の区別に合わせる。省略された値だけがSummary
    // から継承し、存在する空文字列は明示的な選択として残す。
    String type_value = type ? std::move(*type)
                             : (summ ? summ->type() : String{});
    String name_value = name ? std::move(*name)
                             : (summ ? summ->skinName() : String{});
    return findSkin(comm, prop, summ, legacy, scenario_path,
                    std::move(type_value),
                    std::move(name_value), std::move(legacyEngine),
                    appendClassicSkin);
}

SkinPtr findSkin(Commons& comm, Props& prop, const Summary* summ,
                 const bool legacy, String sPath, String type,
                 std::optional<String> name,
                 String legacyEngine, const bool appendClassicSkin) {
    // 共通の解決経路でも、Dの名前の既定値と明示指定の区別を維持する。
    const bool preserve_explicit_empty_name = name.has_value();
    String name_value = name ? std::move(*name) : String{};
    return findSkinSelection(comm, prop, summ, legacy, std::move(sPath),
        std::move(type), std::move(name_value), std::move(legacyEngine),
        appendClassicSkin, true, preserve_explicit_empty_name);
}

static SkinPtr findSkinSelection(Commons& comm, Props& prop, const Summary* summ,
                 const bool legacy, String sPath, String type, String name,
                 String legacyEngine, const bool appendClassicSkin,
                 const bool preserveExplicitEmptyType,
                 const bool preserveExplicitEmptyName) {
    if (summ) (void)dskin::findCWPy(prop, sPath);
    const auto& settings = prop.var();
    const auto classic_engines =
        static_cast<const lib::Array<structs::ClassicEngine>&>(
            settings.classicEngines);
    const auto enginePath = [&prop](const String& value) {
        return structs::pathFromUtf8(prop.toAppAbs(value));
    };
    const auto createClassic = [&prop](const structs::ClassicEngine& engine) {
        return dskin::createClassicSkin(prop, engine);
    };
    if (!summ) {
        // DのfindSkinは、Summaryがなければオーバーロー
        // ドに明示的な種別・名前が渡されても、設定された既定値を使う。
        return dskin::findSkin2(prop, String(settings.defaultSkin),
                                String(settings.defaultSkinName));
    }
    if (legacy && !type.empty()) {
        for (const auto& engine : classic_engines) {
            if (engine.type != type) continue;
            if (!std::filesystem::exists(enginePath(engine.enginePath))) continue;
            if (auto result = createClassic(engine)) return result;
        }
        // Dは空でないClassic種別の検索に失敗すると、記録され
        // た正確なEngineを検討する前にSummaryの種別へ戻
        // す。最初から明示的に空の履歴種別なら、この分岐に入らない。
        if (!lib::editor_window_host_options().all_mode) type = summ->type();
        else type.clear();
    }
    if (legacy && type.empty() && !legacyEngine.empty()) {
        const auto requested = enginePath(legacyEngine);
        for (const auto& engine : classic_engines) {
            const auto candidate = enginePath(engine.enginePath);
            if (!std::filesystem::exists(candidate)) continue;
            if (utils::cfnmatch(structs::pathToUtf8(candidate), structs::pathToUtf8(requested))) {
                if (auto result = createClassic(engine)) return result;
            }
        }
    }
    const auto& etc = prop.var();
    const String classic_engine_regex =
        static_cast<String>(etc.classicEngineRegex);
    const String classic_data_regex =
        static_cast<String>(etc.classicDataDirRegex);
    const String classic_match_key = static_cast<String>(etc.classicMatchKey);
    const String default_skin = static_cast<String>(etc.defaultSkin);
    if (!preserveExplicitEmptyType && type.empty()) type = summ->type();
    if (!preserveExplicitEmptyName && name.empty()) name = summ->skinName();
    try {
        auto result = Skin::find(prop.parent(), prop.enginePath(), type, sPath,
                                 legacy, classic_engine_regex,
                                 classic_data_regex, classic_match_key,
                                 classic_engines, default_skin);
        if (legacy && type.empty()) {
            if (result && !result->legacyEngine().empty()) {
                if (appendClassicSkin && settings.addNewClassicEngine) {
                    const auto discovered = enginePath(result->legacyEngine());
                    for (const auto& engine : classic_engines) {
                        const auto candidate = enginePath(engine.enginePath);
                        if (std::filesystem::exists(candidate) &&
                            utils::cfnmatch(structs::pathToUtf8(candidate), structs::pathToUtf8(discovered)))
                            return createClassic(engine);
                    }
                    structs::ClassicEngine added;
                    added.name = structs::pathToUtf8(discovered.stem());
                    added.enginePath = structs::pathToUtf8(discovered);
                    added.dataDirName = structs::pathToUtf8(
                        structs::pathFromUtf8(result->legacyDataPath()).lexically_relative(discovered.parent_path()));
                    added.type = skin::skinTypeByClassicEngine(
                        static_cast<const lib::Array<structs::SkinTypeByClassicEngine>&>(settings.skinTypesByClassicEngines),
                        added.enginePath, default_skin);
                    auto engines = classic_engines;
                    engines.push_back(std::move(added));
                    const auto life = comm.lifetimeToken();
                    prop.var().classicEngines = std::move(engines);
                    // プロパティの監視通知がC++のホストを終了し得る。DのデリゲートはGCが保持する。
                    if (!life.expired()) comm.refClassicSkin.call();
                }
                return result;
            }
            for (const auto& engine : classic_engines) {
                if (std::filesystem::exists(enginePath(engine.enginePath)))
                    if (auto fallback = createClassic(engine)) return fallback;
            }
            // DはEngineのない旧形式候補を捨て、明示的
            // な空種別では設定された既定スキンへ切り替える。
            if (result && result->legacyEngine().empty()) result.reset();
        }
        // 従来のSkin::findは種別だけで選ぶ。MedievalFantasy
        // の複数スキンがある場合、Classicを明示したシナリオ
        // へ最初に列挙された別のスキン（BloodWirthなど）
        // を渡してはならない。実際のClassic Engineの
        // 検索結果は保持し、それ以外はシナリオのスキン名で絞る。Summary
        // が旧形式というだけで名前による絞り込みを省かず、
        // 名前付きXMLスキンは同種別候補からさらに選ぶ。
        if (result && (result->legacy() || name.empty() || result->name() == name))
            return result;
        if (auto named = dskin::findSkin2(prop, type, name)) return named;
        return result;
    } catch (...) {
        return {};
    }
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::commons
