#pragma once

// cwx/editor/gui/dwt/commons.d
// の移植可能なC++20実装。 Editor
// 全体の通知とウィンドウ調整を行う役割を
// 保ち、SWTの部品をlibの部品で表す
// 。 所有権を明示し、ネイティブ部品や生
// 成済みの互換型はこの境界へ渡さない。

#include "cwxeditor_src/core/cwx/area.hpp"
#include "cwxeditor_src/core/cwx/card.hpp"
#include "cwxeditor_src/core/cwx/event.hpp"
#include "cwxeditor_src/core/cwx/filesync.hpp"
#include "cwxeditor_src/core/cwx/flag.hpp"
#include "cwxeditor_src/core/cwx/importutils.hpp"
#include "cwxeditor_src/core/cwx/path.hpp"
#include "cwxeditor_src/core/cwx/skin.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/core/cwx/usecounter.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dmenu.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dprops.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dskin.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/xmlbytestransfer.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/replacedialog.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/events.hpp"
#include "cwxeditor_src/lib/graphics.hpp"
#include "cwxeditor_src/lib/services.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::smalldialogs { class EventTemplateDialog; }

namespace cwxeditor::cwx::editor::gui::dwt::commons {

namespace lib = ::cwxeditor::lib;
namespace area = ::cwxeditor::cwx::area;
namespace card = ::cwxeditor::cwx::card;
namespace dmenu = ::cwxeditor::cwx::editor::gui::dwt::dmenu;
namespace dprops = ::cwxeditor::cwx::editor::gui::dwt::dprops;
namespace dskin = ::cwxeditor::cwx::editor::gui::dwt::dskin;
namespace event = ::cwxeditor::cwx::event;
namespace filesync = ::cwxeditor::cwx::filesync;
namespace flag = ::cwxeditor::cwx::flag;
namespace path = ::cwxeditor::cwx::path;
namespace skin = ::cwxeditor::cwx::skin;
namespace summary = ::cwxeditor::cwx::summary;
namespace structs = ::cwxeditor::cwx::structs;
namespace types = ::cwxeditor::cwx::types;
namespace usecounter = ::cwxeditor::cwx::usecounter;
namespace xmlbytestransfer =
    ::cwxeditor::cwx::editor::gui::dwt::xmlbytestransfer;

using String = lib::String;
using StringView = lib::StringView;
using Props = dprops::Props;
using FileSync = filesync::FileSync;
using Summary = summary::Summary;
using SummaryPtr = lib::Owner<Summary>;
using Skin = skin::Skin;
using SkinPtr = skin::Skin::Ptr;
using MenuID = types::MenuID;
using CType = types::CType;
using SelectionEvent = dmenu::SelectionEvent;
using WindowPtr = std::shared_ptr<lib::Window>;
using DialogPtr = std::shared_ptr<lib::Dialog>;
using ContainerPtr = std::shared_ptr<lib::Container>;
using PanelPtr = std::shared_ptr<lib::Panel>;
using WidgetPtr = lib::WidgetPtr;
using ToolBarPtr = std::shared_ptr<lib::ToolBar>;
using CardTableColumn = std::int32_t;
using OpenHistory = structs::OpenHistory;
using ClipData = xmlbytestransfer::ClipData;
using Image = lib::ResourceId;

using CastCardPtr = lib::Owner<card::CastCard>;
using SkillCardPtr = lib::Owner<card::SkillCard>;
using ItemCardPtr = lib::Owner<card::ItemCard>;
using BeastCardPtr = lib::Owner<card::BeastCard>;
using InfoCardPtr = lib::Owner<card::InfoCard>;
using EffectCardPtr = lib::Owner<card::EffectCard>;
// 手札ペインは、読み取り専用の取り込み元も含め、コピー元の同一性を保持する。
struct HandPaneRequest {
    SummaryPtr source;
    CastCardPtr card;
    SummaryPtr destination;
};
// 使用時イベントの要求には、正確なカードの識別と、その所有元の両方が必要。
// C++モデルではカードだけを保持してもSummaryは保持されない。
struct UseEventRequest {
    SummaryPtr source;
    EffectCardPtr card;
};
using AreaPtr = lib::Owner<area::Area>;
using BattlePtr = lib::Owner<area::Battle>;
using PackagePtr = lib::Owner<area::Package>;
using FlagDirPtr = lib::Owner<flag::FlagDir>;
using EventTreePtr = event::EventTreePtr;
using ContentPtr = event::ContentPtr;
using CWXPathPtr = lib::Owner<path::CWXPath>;
using FlagList = std::vector<FlagDirPtr>;

/// Dのシグナル・デリゲートに対応する共通
/// のクラス。 通知前にハンドラー一覧を取
/// 得し、通知中の自身の解除・新規追加でDlg
/// と同じ動作を保つ。 Dのデリゲート指定
/// による解除に加え、接続トークンで関数
/// の同一性に頼らずラムダを解除できる。
template <typename... Args>
class Dlg final {
public:
    using Handler = std::function<void(Args...)>;
    using SenderHandler = std::function<void(void*, Args...)>;
    using Connection = std::uint64_t;

    class NoS final {
    public:
        Handler dlg;
        void call(void*, Args... args) {
            if (dlg) dlg(std::forward<Args>(args)...);
        }
    };

    Dlg() = default;
    Dlg(const Dlg&) = delete;
    Dlg& operator=(const Dlg&) = delete;
    Dlg(Dlg&&) = delete;
    Dlg& operator=(Dlg&&) = delete;

    void dispose() noexcept { clear(); }

    [[nodiscard]] Connection add(Handler handler) {
        if (!handler) return 0;
        const auto target = targetKey(handler);
        return addEntry(
            [handler = std::move(handler)](void*, Args... args) {
                handler(std::forward<Args>(args)...);
            },
            target);
    }

    [[nodiscard]] Connection add(SenderHandler handler) {
        if (!handler) return 0;
        return addEntry(std::move(handler));
    }

    template <typename F>
        requires (!std::is_same_v<std::remove_cvref_t<F>, Handler> &&
                   !std::is_same_v<std::remove_cvref_t<F>, SenderHandler> &&
                   std::is_invocable_r_v<void, F&, Args...>)
    [[nodiscard]] Connection add(F&& handler) {
        return add(Handler(std::forward<F>(handler)));
    }

    template <typename F>
        requires std::is_invocable_r_v<void, F&, void*, Args...>
    [[nodiscard]] Connection addSender(F&& handler) {
        return add(SenderHandler(std::forward<F>(handler)));
    }

    void remove(Connection connection) noexcept {
        if (!connection) return;
        entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                      [connection](const Entry& entry) {
                                          return entry.connection == connection;
                                      }),
                       entries_.end());
    }

    void remove(const Handler& handler) noexcept {
        if (!handler) return;
        // 関数ポインターはstd::function内でも固定の呼び出し先
        // を持つ。 ラムダの解除にはConnectionトークンを使う。
        entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                      [&handler](const Entry& entry) {
                                          return sameTarget(entry, handler);
                                      }),
                       entries_.end());
    }

    void clear() noexcept { entries_.clear(); }
    [[nodiscard]] std::size_t count() const noexcept { return entries_.size(); }

    void call(Args... args) const {
        call(nullptr, std::forward<Args>(args)...);
    }

    void call(void* sender, Args... args) const {
        const auto snapshot = entries_;
        for (const auto& entry : snapshot) {
            if (entry.handler) entry.handler(sender, args...);
        }
    }

private:
    struct Entry {
        Connection connection{};
        SenderHandler handler{};
        std::uintptr_t target{};
    };

    Connection next_connection_{1};
    std::vector<Entry> entries_;

    [[nodiscard]] Connection addEntry(SenderHandler handler,
                                      const std::uintptr_t target = 0) {
        const auto connection = next_connection_++;
        entries_.push_back({connection, std::move(handler), target});
        return connection;
    }

    static std::uintptr_t targetKey(const Handler& handler) noexcept {
        using Function = void (*)(Args...);
        const auto* target = handler.template target<Function>();
        return target ? reinterpret_cast<std::uintptr_t>(*target) : 0U;
    }

    static bool sameTarget(const Entry& left,
                           const Handler& right) noexcept {
        // 型消去された任意の関数オブジェクトには、共通の等価比
        // 較がない。 この版は通常の関数ポインター用に残し、状
        // 態付きの通知はadd()が返すトークンで解除する。
        const auto target = targetKey(right);
        return target != 0U && left.target == target;
    }
};

/// DのSWT Composite/Shellに依存しない、最上位のパネル。
struct ViewProjection {
    enum class Kind { None, Area, Battle, Event };
    Kind kind{Kind::None};
    CWXPathPtr owner;
    PanelPtr view;
    PanelPtr treeView;
    ContainerPtr logicalShell;
    bool useCards{}, useBacks{}, readOnly{};
    std::function<EventTreePtr()> selectedTree;
    // 選択ツリーを持つ他のビューの共通検索で使う、EventWindowの弱参照。
    lib::Action refreshEventTreeView;
    // EventTreeの行内編集は、そのEventWindowを通して確
    // 定する。 他のビューの独立したUndoManagerは結合しない。
    std::function<bool(const EventTreePtr&)> finishEventTreeEdit;
    std::function<PanelPtr()> toolbox;
    std::function<PanelPtr()> ensureToolbox;
    std::function<bool()> toolboxSingleton;
    std::function<std::shared_ptr<void>()> releaseToolbox;
    std::function<PanelPtr(std::shared_ptr<void>)> adoptToolbox;
    lib::Action rebuildToolbox;
    std::function<bool()> debugMode; 
};

class TopLevelPanel {
public:
    using MenuAction = std::function<void(const SelectionEvent&)>;
    using MenuEnabled = std::function<bool()>;
    using MenuChecked = std::function<bool()>;
    using OpenPath = std::function<bool(String, bool)>;

    TopLevelPanel();
    explicit TopLevelPanel(ContainerPtr shell);
    virtual ~TopLevelPanel() = default;
    void retireHostBindings() noexcept;
    void hostRetirementHandler(lib::Action value) { if (lifetime_.token) host_retirement_ = std::move(value); }

    [[nodiscard]] virtual String title() const;
    [[nodiscard]] virtual Image image() const noexcept;
    [[nodiscard]] virtual ContainerPtr shell() const noexcept;
    [[nodiscard]] virtual WidgetPtr focusControl() const noexcept;

    void title(String value);
    void image(Image value) noexcept;
    void shell(ContainerPtr value);
    void focusControl(WidgetPtr value) noexcept;
    void statusText(std::function<void(String)> value);
    [[nodiscard]] std::vector<String> openedCWXPath() const;
    void openedCWXPathProvider(std::function<std::vector<String>()> value) { if (lifetime_.token) opened_paths_provider_ = std::move(value); }

    virtual bool openCWXPath(String cwxPath, bool shellActivate);
    void openCWXPathHandler(OpenPath value) { if (lifetime_.token) open_path_ = std::move(value); }
    void rememberCWXPath(String path);
    void setOpenedCWXPath(std::vector<String> paths);
    [[nodiscard]] const ViewProjection& viewProjection() const noexcept { return projection_; }
    void viewProjection(ViewProjection value) { if (lifetime_.token) projection_ = std::move(value); }

    void putMenuAction(MenuID menuID, lib::Action action,
                       MenuEnabled enabled = {});
    void putMenuAction(MenuID menuID, MenuAction action,
                       MenuEnabled enabled = {});
    void putMenuChecked(MenuID menuID, lib::Action action,
                        MenuChecked checked, MenuEnabled enabled = {});
    void putMenuChecked(MenuID menuID, MenuAction action,
                        MenuChecked checked, MenuEnabled enabled = {});

    [[nodiscard]] MenuAction menuAction(MenuID menuID) const;
    [[nodiscard]] MenuChecked menuChecked(MenuID menuID) const;
    [[nodiscard]] MenuEnabled menuEnabled(MenuID menuID) const;
    bool doMenu(class Commons& comm, int keyCode, std::uint32_t character,
                std::uint32_t stateMask,
                std::function<SelectionEvent()> event = {});

    [[nodiscard]] String statusLine() const;
    void statusLine(String value);

private:
    friend class Commons;
    struct MenuEntry {
        MenuAction action;
        MenuEnabled enabled;
        MenuChecked checked;
        bool checked_registered{};
    };

    String title_;
    Image image_{};
    ContainerPtr shell_;
    WidgetPtr focus_control_;
    std::function<void(String)> status_text_;
    std::map<MenuID, MenuEntry> menus_;
    String status_;
    std::vector<String> opened_paths_;
    OpenPath open_path_;
    lib::Action host_retirement_;
    std::function<std::vector<String>()> opened_paths_provider_;
    lib::WidgetOperationLifetime lifetime_;
    ViewProjection projection_;
};

class SashPanel {
public:
    virtual ~SashPanel() = default;
};

class TLPData final {
public:
    std::shared_ptr<TopLevelPanel> tlp;
    std::shared_ptr<void> main;

    explicit TLPData(std::shared_ptr<TopLevelPanel> panel = {})
        : tlp(std::move(panel)) {}
};

/// 共通部品へ最上位の情報を登録・取得する。lib::Widget
/// は親・データの格納先を持たないため、 この登録表でSWTのgetData()/getParent()
/// による探索に対応する。
void attachTLPData(const WidgetPtr& control, std::shared_ptr<TLPData> data);
[[nodiscard]] std::shared_ptr<TLPData> tlpData(const WidgetPtr& control);
[[nodiscard]] std::shared_ptr<TopLevelPanel>
getTopLevelPanel(const WidgetPtr& control);

class Commons final {
public:
    /// 具体的な編集ペイン用に、MainWindowが渡す生
    /// 成処理。 ペインごとにコンストラクターが異なるため
    /// 、Commonsは遷移とキャッシュだけを管理し、
    /// 未登録の型付きペインを空の代替画面へ置き換えない。
    using PaneFactory = std::function<std::shared_ptr<TopLevelPanel>(
        String key, bool shellActivate, const std::shared_ptr<void>& owner)>;
    /// 動的に作ったエディターをドックへ接続するホストの通知。 DのCommons.open()
    /// と同じく、ShellでないTopLevelPanelを指定のペ
    /// イン群（通常はwork）へ追加する。 CommonsをDockingFolder
    /// へ依存させず、この通知で同じ規則を保つ。
    using PanePresenter = std::function<void(
        StringView key, const std::shared_ptr<TopLevelPanel>& panel,
        StringView paneFamily, bool select)>;
    // 資源パスの完全な振り分けはMainWindowが所
    // 有する。 ホストが認識しないパスには未処理を返し、
    // Commonsの登録済みペインによる代替を残す。
    using CwxPathRouter = std::function<std::optional<bool>(String, bool)>;
    using PackageTreeCreator =
        std::function<std::uint64_t(const EventTreePtr&, String, bool)>;
    using PackageContentCreator =
        std::function<std::uint64_t(const ContentPtr&, String, bool)>;

    Dlg<> changed;
    Dlg<> refToolsEnabled;
    Dlg<> refImageScale;
    Dlg<> refSwitchFixedWithCheckBox;
    Dlg<> refImagePaneSelectionFilter;
    Dlg<WindowPtr> save;
    Dlg<> saved;
    Dlg<> refHistories;
    Dlg<> refImportHistory;
    Dlg<> refHistoryMax;
    Dlg<> refSearchHistories;
    Dlg<> refExecutedParties;
    Dlg<> refPartyHistoryMax;
    Dlg<SummaryPtr> refScenario;
    Dlg<> refScenarioName;
    Dlg<> refScenarioPath;
    Dlg<> refSkin;
    Dlg<> refClassicSkin;
    Dlg<> refBgImageSettings;
    Dlg<> refSettingsWithSkinTypes;
    Dlg<> refDataVersion;
    Dlg<> refTargetVersion;
    Dlg<> refStandardSelections;
    Dlg<> refStandardKeyCodes;
    Dlg<> refOuterTools;
    Dlg<> refEventTemplates;
    Dlg<String, String, bool> refPath;
    Dlg<String> refPaths;
    Dlg<> delPaths;
    Dlg<String, String> replPath;
    Dlg<> refUseCount;
    Dlg<> refAreaTable;
    Dlg<CastCardPtr> refCast;
    Dlg<CastCardPtr> delCast;
    Dlg<SkillCardPtr> refSkill;
    Dlg<CWXPathPtr, SkillCardPtr> delSkill;
    Dlg<ItemCardPtr> refItem;
    Dlg<CWXPathPtr, ItemCardPtr> delItem;
    Dlg<BeastCardPtr> refBeast;
    Dlg<CWXPathPtr, BeastCardPtr> delBeast;
    Dlg<InfoCardPtr> refInfo;
    Dlg<InfoCardPtr> delInfo;
    Dlg<FlagList> refFlagDir;
    Dlg<FlagList> delFlagDir;
    // Dのcommons.d:390と同じく、包含ディレクトリでなく変更された変数を渡す。
    Dlg<flag::FlagList, flag::StepList, flag::VariantList> refFlagAndStep;
    Dlg<flag::FlagList, flag::StepList, flag::VariantList> delFlagAndStep;
    Dlg<> replText;
    Dlg<> replID;
    Dlg<> refIgnorePaths;
    Dlg<> refCardState;
    Dlg<> refWallpaper;
    Dlg<> refSortCondition;
    Dlg<CardTableColumn, int> refCardTableColumnWidth;
    Dlg<> refShowCardListHeader;
    Dlg<> refShowCardListTitle;
    Dlg<> refUndoMax;
    Dlg<MenuID> refMenu;
    Dlg<> refSoundType;
    Dlg<> refShowToolBar;
    Dlg<> refCardImageStatus;
    Dlg<> refEventTreeStyle;
    Dlg<> refRadarStyle;
    Dlg<> refCastCardParameterEditStyle;
    Dlg<> refShowAptitudes;
    Dlg<> refVarSelectStyle;
    Dlg<> refEventTreeViewStyle;
    Dlg<> refEventEditorStyle;
    Dlg<> refContentsToolBoxStyle;
    Dlg<int> refEventTreeSlope;
    Dlg<> refFloatMessagePreview;
    Dlg<> refUseMessageWindowColorInTextContentDialog;
    Dlg<> refUpdateMotionBarStyle;
    Dlg<> refMenuCardAndBgImageList;
    Dlg<> refKeyCodesByFeatures;
    Dlg<> refElementOverrides;
    Dlg<> refTableViewStyle;
    Dlg<AreaPtr> refArea;
    Dlg<AreaPtr> delArea;
    Dlg<BattlePtr> refBattle;
    Dlg<BattlePtr> delBattle;
    Dlg<PackagePtr> refPackage;
    Dlg<PackagePtr> delPackage;
    Dlg<CardTableColumn, int> refMainCardsSort;
    Dlg<CardTableColumn, int> refHandCardsSort;
    Dlg<CardTableColumn, int> refImportCardsSort;
    Dlg<CardTableColumn, int> refImportHandCardsSort;
    Dlg<int, int> refImportAreasSort;
    Dlg<String> addMenuCard;
    Dlg<String> refMenuCard;
    Dlg<String> delMenuCard;
    Dlg<String, std::vector<int>, int> upMenuCard;
    Dlg<String, std::vector<int>, int> downMenuCard;
    Dlg<std::shared_ptr<const area::AbstractArea>,
        std::vector<std::pair<std::size_t, std::size_t>>> refMenuCardIndices;
    Dlg<String> addBgImage;
    Dlg<String> refBgImage;
    Dlg<String> delBgImage;
    Dlg<String, std::vector<int>, int> upBgImage;
    Dlg<String, std::vector<int>, int> downBgImage;
    Dlg<bool, CType, MenuID, bool, bool> selContentTool;
    Dlg<EventTreePtr> refEventTree;
    Dlg<EventTreePtr> delEventTree;
    Dlg<ContentPtr> refContent;
    Dlg<ContentPtr> delContent;
    Dlg<> refContentText;
    Dlg<> refPreviewValues;
    Dlg<> refCoupons;
    Dlg<> refGossips;
    Dlg<> refCompleteStamps;
    Dlg<> refKeyCodes;
    Dlg<> refCellNames;
    Dlg<> refCardGroups;
    Dlg<SummaryPtr> closeAdds;

    std::mutex saveSync;

    explicit Commons(Props& prop, FileSync& sync);
    explicit Commons(Props* prop = nullptr, FileSync* sync = nullptr);
    ~Commons();

    Commons(const Commons&) = delete;
    Commons& operator=(const Commons&) = delete;
    Commons(Commons&&) = delete;
    Commons& operator=(Commons&&) = delete;

    std::unordered_map<String, bool> flagDirExpanded;
    std::unordered_map<String, bool> stepDirExpanded;
    std::unordered_map<String, bool> variantDirExpanded;
    std::unordered_map<String, bool> flagAreaExpanded;
    std::unordered_map<String, bool> flagBattleExpanded;
    std::unordered_map<String, bool> flagPackageExpanded;
    void clearExpanded();

    [[nodiscard]] Props* prop() noexcept { return prop_; }
    [[nodiscard]] const Props* prop() const noexcept { return prop_; }
    [[nodiscard]] FileSync* sync() noexcept { return sync_; }
    [[nodiscard]] const FileSync* sync() const noexcept { return sync_; }
    void dispose() noexcept;

    [[nodiscard]] int isMenuAccelerating() const noexcept {
        return is_menu_accelerating_;
    }
    void isMenuAccelerating(int mask) noexcept {
        is_menu_accelerating_ = mask;
    }

    void put(const ToolBarPtr& bar);
    void put(const WidgetPtr& widget, std::function<bool()> enabled = {});
    void removeToolBar(const WidgetPtr& widget) noexcept;
    void clearToolBars();
    [[nodiscard]] WidgetPtr lastFocusEditor() const noexcept;
    void refreshToolBar();
    void refreshToolBar(const WidgetPtr& focus);
    using ToolBarOverrides = std::map<MenuID, bool>;
    using MainToolBars = std::function<std::vector<ToolBarPtr>()>;
    void mainToolBars(MainToolBars provider) { main_toolbars_ = std::move(provider); }
    void toolBarsRefreshed(lib::Action callback) { toolbars_refreshed_ = std::move(callback); }
    // MainWindowとCommonsの遅延更新で、同じ型付き項目の規則を使う。
    void refreshToolBarItems(const ToolBarOverrides& overrides = {});
    [[nodiscard]] std::weak_ptr<const int> lifetimeToken() const noexcept { return lifetime_.token; }
    void updateToolBarText();
    void baseShell(WindowPtr main, WindowPtr tableWin = {},
                   WindowPtr flagWin = {}, WindowPtr castWin = {},
                   WindowPtr skillWin = {}, WindowPtr itemWin = {},
                   WindowPtr beastWin = {}, WindowPtr infoWin = {},
                   WindowPtr dirWin = {}, WindowPtr couponWin = {},
                   WindowPtr gossipWin = {}, WindowPtr completeStampWin = {},
                   WindowPtr keyCodeWin = {}, WindowPtr cellNameWin = {},
                   WindowPtr cardGroupWin = {});

    /// ホストが所有する型付きペインのアダプターを登録する。 キ
    /// ーはarea.scene:<path>などの個別ID、ま
    /// たはarea.sceneなどの種類IDを使える。 パス別
    /// のオープンには種類IDを使う。登録を明示し、欠けたペイン
    /// を空のTopLevelPanelへ黙って縮退させない。
    void registerPaneFactory(String key, PaneFactory factory);
    void registerPane(String key, std::shared_ptr<TopLevelPanel> panel);
    void unregisterPaneFactory(StringView key) noexcept;
    [[nodiscard]] bool hasPaneFactory(StringView key) const noexcept;
    [[nodiscard]] PaneFactory paneFactoryFor(StringView key) const;
    /// 保存されたドックキーからペインを復元し、
    /// 通常のオープンと同じ登録表へ公開する。
    [[nodiscard]] std::shared_ptr<TopLevelPanel> restoreRegisteredPane(String key);
    void panePresenter(PanePresenter presenter) {
        pane_presenter_ = std::move(presenter);
    }
    void pathRouter(CwxPathRouter router) {
        cwx_path_router_ = std::move(router);
    }
    void packageCreators(PackageTreeCreator treeCreator,
                         PackageContentCreator contentCreator) {
        package_tree_creator_ = std::move(treeCreator);
        package_content_creator_ = std::move(contentCreator);
    }

    [[nodiscard]] WindowPtr mainWin() const noexcept { return main_; }
    [[nodiscard]] WindowPtr mainShell() const noexcept { return main_; }
    [[nodiscard]] ClipData& clipboard() noexcept { return clipboard_; }
    [[nodiscard]] const ClipData& clipboard() const noexcept { return clipboard_; }
    [[nodiscard]] SummaryPtr summary() const noexcept { return summary_; }
    void summary(SummaryPtr value);
    [[nodiscard]] bool isChanged() const;
    void externalChanges(std::function<bool()> callback) { external_changes_ = std::move(callback); }
    using ImportPublication = std::function<void(Summary*, const importutils::ImportResult&)>;
    void importPublication(ImportPublication callback) { import_publication_ = std::move(callback); }
    void closeAll() noexcept;
    /// 再利用可能なデータペインを隠さずに、文書へ結び付いたビューだけを終了する。
    void closeScenarioPanes() noexcept;

    void skin(SkinPtr value) noexcept { skin_ = std::move(value); }
    [[nodiscard]] SkinPtr skin() const noexcept { return skin_; }
    [[nodiscard]] SkinPtr findSkinFromHistory(const Summary* summ) const;
    [[nodiscard]] SkinPtr findSkinFromHistory(const Summary* summ,
                                               OpenHistory& history) const;
    struct SkinMaterialChange {
        std::weak_ptr<usecounter::PathUser> user;
        usecounter::PathId before, after;
        std::uint64_t revision{};
        std::uint64_t beforeRevision{};
    };
    using SkinMaterialJournal = std::vector<SkinMaterialChange>;
    void updateSkinMaterialsExtension(const usecounter::CounterOwner& counter,
                                      SkinPtr oldSkin, SkinPtr newSkin,
                                      SkinMaterialJournal* journal = nullptr);
    void restoreSkinMaterials(SkinMaterialJournal& journal);

    [[nodiscard]] std::shared_ptr<TopLevelPanel>
    openAreaScene(Props&, Summary*, AreaPtr area, bool shellActivate,
                  std::shared_ptr<TopLevelPanel> duplicateBase = {});
    [[nodiscard]] std::shared_ptr<TopLevelPanel>
    openAreaEvent(Props&, Summary*, AreaPtr area, bool shellActivate,
                  std::shared_ptr<TopLevelPanel> duplicateBase = {});
    [[nodiscard]] std::shared_ptr<TopLevelPanel>
    openAreaScene(Props&, Summary*, BattlePtr area, bool shellActivate,
                  std::shared_ptr<TopLevelPanel> duplicateBase = {});
    [[nodiscard]] std::shared_ptr<TopLevelPanel>
    openAreaEvent(Props&, Summary*, BattlePtr area, bool shellActivate,
                  std::shared_ptr<TopLevelPanel> duplicateBase = {});
    [[nodiscard]] std::shared_ptr<TopLevelPanel>
    openArea(Props&, Summary*, PackagePtr area, bool shellActivate,
             std::shared_ptr<TopLevelPanel> duplicateBase = {});
    [[nodiscard]] std::shared_ptr<TopLevelPanel>
    openHands(Props&, Summary*, CastCardPtr card, bool shellActivate);
    [[nodiscard]] std::shared_ptr<TopLevelPanel>
    openAddHands(Props&, Summary*, CastCardPtr card, Summary* toc,
                 bool shellActivate);
    [[nodiscard]] std::shared_ptr<TopLevelPanel>
    openUseEvents(Props&, Summary*, EffectCardPtr card, bool shellActivate,
                  std::shared_ptr<TopLevelPanel> duplicateBase = {});

    [[nodiscard]] std::shared_ptr<TopLevelPanel> duplicatePanel(
        std::shared_ptr<TopLevelPanel> base, bool shellActivate);

    void openDataWin(bool shellActivate);
    [[nodiscard]] PanelPtr openFlagWin(bool shellActivate);
    [[nodiscard]] PanelPtr openCardPane(std::int32_t cardType,
                                        bool shellActivate);
    [[nodiscard]] PanelPtr openCastWin(bool shellActivate);
    [[nodiscard]] PanelPtr openSkillWin(bool shellActivate);
    [[nodiscard]] PanelPtr openItemWin(bool shellActivate);
    [[nodiscard]] PanelPtr openBeastWin(bool shellActivate);
    [[nodiscard]] PanelPtr openInfoWin(bool shellActivate);
    void openDirWin(bool shellActivate);
    void openCouponWin(bool shellActivate);
    void openGossipWin(bool shellActivate);
    void openCompleteStampWin(bool shellActivate);
    void openKeyCodeWin(bool shellActivate);
    void openCellNameWin(bool shellActivate);
    void openCardGroupWin(bool shellActivate);

    [[nodiscard]] std::shared_ptr<TopLevelPanel>
    areaWindowFrom(StringView cwxPath, bool shellActivate);
    [[nodiscard]] std::shared_ptr<TopLevelPanel>
    eventWindowFrom(StringView cwxPath, bool shellActivate);
    // 元のシナリオを区別するArea/Battle/Packageイベントペインの、同一性だけの問い合わせ。
    [[nodiscard]] std::shared_ptr<TopLevelPanel>
    eventWindowFrom(const Summary* source, const path::CWXPath* owner) const;

    // DのEventViewと同じく、一致する開いている全BattleカードビューのdebugModeを論理和でまとめる。
    [[nodiscard]] bool battlePreviewDebugMode(const event::EventTreeOwner* owner) const {
        std::vector<std::function<bool()>> readers;
        for (const auto& [key, window] : windows_) {
            if (!window) continue;
            const auto& p = window->viewProjection();
            const auto battle = std::dynamic_pointer_cast<area::Battle>(p.owner);
            if (p.kind == ViewProjection::Kind::Battle && p.useCards && !p.useBacks &&
                battle && battle.get() == owner && p.debugMode) readers.push_back(p.debugMode);
        }
        // 通知でウィンドウの登録表が変わる場合があるため、コピーしたコールバックだけを走査する。
        for (const auto& read : readers) if (read()) return true;
        return false;
    }

    template <typename A, typename C, bool UseCards, bool UseBacks>
    [[nodiscard]] std::vector<PanelPtr> areaViewsFrom(StringView cwxPath,
                                                       bool shellActivate) {
        std::vector<PanelPtr> result;
        static_cast<void>(shellActivate); // Dと同じく、問い合わせではウィンドウをアクティブ化しない。
        if constexpr ((std::is_base_of_v<area::Area, A> && !std::is_same_v<C, area::MenuCard>) ||
                      (std::is_base_of_v<area::Battle, A> && !std::is_same_v<C, area::EnemyCard>))
            return result;
        if (!summary_) return result;
        const auto owner = std::dynamic_pointer_cast<A>(summary_->findCWXPath(cwxPath));
        if (!owner) return result;
        for (const auto& [key, window] : windows_) {
            if (!window) continue;
            const auto& projection = window->viewProjection();
            if (projection.kind != ViewProjection::Kind::Area &&
                projection.kind != ViewProjection::Kind::Battle) continue;
            if (projection.useCards != UseCards || projection.useBacks != UseBacks) continue;
            const auto candidate = std::dynamic_pointer_cast<A>(projection.owner);
            if (candidate == owner && projection.view) result.push_back(projection.view);
        }
        return result;
    }

    [[nodiscard]] std::vector<PanelPtr>
    eventViewsFrom(const CWXPathPtr& owner, bool shellActivate);
    [[nodiscard]] std::vector<PanelPtr>
    eventTreeViewFrom(const CWXPathPtr& owner, const EventTreePtr& tree,
                      bool shellActivate);
    // EventTreeの履歴復元では、論理的な行を保ちながらスナップショットのポイ
    // ンターが変わる場合がある。 他のビューを探す代替処理でも、同じ識別規則を使う。
    [[nodiscard]] static bool sameEventTreeIdentity(
        const EventTreePtr& left, const EventTreePtr& right) noexcept;
    // 同じ資源でもEventWindowごとにカードアダプターが
    // 異なる場合がある。 比較前に正式な所有者パスで解決する。
    [[nodiscard]] static CWXPathPtr canonicalEventOwner(
        event::EventTreeOwner* owner);
    [[nodiscard]] static bool sameOwnerIdentity(
        const CWXPathPtr& left, const CWXPathPtr& right) noexcept;
    [[nodiscard]] std::shared_ptr<TopLevelPanel>
    handCardWindowFrom(Props&, Summary*, CastCardPtr card, bool open,
                       bool shellActivate);

    [[nodiscard]] PanelPtr getContentsToolBox(const PanelPtr& eventTreeView);
    bool poolContentsToolBox(const PanelPtr& box);
    void eventTreeHost(const PanelPtr& treeView, const ContainerPtr& shell);

    void addScenario(Props& prop);
    void addScenario(Props& prop, const std::vector<String>& paths,
                     lib::Action failure = {});
    void doImport(Summary* to, Summary* from,
                  const std::vector<String>& resourcePaths);
    [[nodiscard]] DialogPtr editScEventTemplate(WindowPtr shell);

    void setTitle(const ContainerPtr& component, String text);
    void close(const ContainerPtr& component) noexcept;
    void setStatusLine(const WidgetPtr& base, String status,
                       bool refMain = true);
    void mainStatusPublisher(std::function<void(String)> value) {
        main_status_publisher_ = std::move(value);
    }
    [[nodiscard]] String statusLine() const;
    bool openCWXPath(String path, bool shellActivate);
    /// MainWindowの完全な振り分けで未処理となったパ
    /// スに使い、 ホストの振り分けへ再帰して戻るのを避ける。
    bool openCWXPathFallback(String path, bool shellActivate);
    bool openFilePath(String path, bool shellActivate,
                      bool deselectEtc = false);
    [[nodiscard]] const std::vector<String>& selectedFiles()const noexcept{return selected_files_;}
    void replacePath(String from, bool start);
    void pathReplacement(std::function<void(String, bool)> value) { path_replacement_ = std::move(value); }
    void fileSelection(std::function<bool(String, bool)> value) { file_selection_ = std::move(value); }
    void nameSelection(std::function<void(String, std::vector<String>)> value) { name_selection_ = std::move(value); }

    using ReplaceIDRequest = std::function<void(replacedialog::ReplaceDialog&)>;
    void idReplacement(std::function<void(ReplaceIDRequest)> value) {
        id_replacement_ = std::move(value);
    }
    template <typename ID>
    void replaceID(const ID& id, bool start) {
        const auto callback = id_replacement_;
        if (callback) callback([id, start](replacedialog::ReplaceDialog& dialog) {
            dialog.replaceID(id, start);
        });
    }

    template <typename ID>
    void selectIDName(const std::vector<ID>& ids, bool shellActivate) {
        if (ids.empty()) return;
        String key;
        if constexpr (std::is_same_v<ID, usecounter::CouponId>) key = "coupon";
        else if constexpr (std::is_same_v<ID, usecounter::GossipId>) key = "gossip";
        else if constexpr (std::is_same_v<ID, usecounter::CompleteStampId>) key = "completeStamp";
        else if constexpr (std::is_same_v<ID, usecounter::KeyCodeId>) key = "keyCode";
        else if constexpr (std::is_same_v<ID, usecounter::CellNameId>) key = "cellName";
        else if constexpr (std::is_same_v<ID, usecounter::CardGroupId>) key = "cardGroup";
        else static_assert(std::is_same_v<ID, void>, "unsupported name ID type");
        std::vector<String> values;
        values.reserve(ids.size());
        for (const auto& id : ids) values.emplace_back(static_cast<String>(id));
        selectName(std::move(key), std::move(values), shellActivate);
    }

    bool selectSummary(bool shellActivate);
    [[nodiscard]] std::uint64_t createPackage(const EventTreePtr& baseTree,
                                              String name,
                                              bool shellActivate);
    [[nodiscard]] std::uint64_t createPackage(const ContentPtr& baseStart,
                                              String name,
                                              bool shellActivate);

    [[nodiscard]] Image wallpaper() const noexcept { return wallpaper_; }
    void refreshWallpaper(Props& prop);
    bool updateJpy1Files(bool forceUpdate = false);
    [[nodiscard]] bool inReplaceText() const noexcept { return in_replace_text_; }
    void inReplaceText(bool value) noexcept { in_replace_text_ = value; }
    [[nodiscard]] bool stopOpen() const noexcept { return stop_open_; }
    void stopOpen(bool value) noexcept { stop_open_ = value; }
    [[nodiscard]] bool warningHandleCount() const noexcept {
        return warning_handle_count_;
    }
    void warningHandleCount(bool value);

private:
    Props* prop_{};
    FileSync* sync_{};
    lib::Services services_{};
    int is_menu_accelerating_{};

    WindowPtr main_;
    std::function<void(String)> main_status_publisher_;
    std::array<WindowPtr, 14> auxiliary_windows_{};
    ClipData clipboard_;
    std::vector<WidgetPtr> toolbars_;
    std::unordered_map<const lib::Widget*, std::function<bool()>>
        toolbar_enabled_;
    WidgetPtr last_focus_editor_;
    bool refresh_toolbar_pending_{};
    // パレットの通知で、有効化へ同期的に再入したりCommonsが終了したりする場合がある。
    std::shared_ptr<bool> palette_transfer_active_{std::make_shared<bool>(false)};
    std::shared_ptr<std::vector<const lib::Widget*>> closing_components_{
        std::make_shared<std::vector<const lib::Widget*>>()};
    WidgetPtr refresh_toolbar_arg_;
    MainToolBars main_toolbars_;
    lib::Action toolbars_refreshed_;
    std::function<bool()> external_changes_;
    ImportPublication import_publication_;
    std::uint64_t toolbar_request_generation_{};
    std::uint64_t toolbar_refresh_revision_{};
    lib::WidgetOperationLifetime lifetime_;
    SummaryPtr summary_;
    std::shared_ptr<smalldialogs::EventTemplateDialog> event_template_editor_;
    std::weak_ptr<Summary> event_template_summary_;
    std::shared_ptr<const int> summary_change_publication_;
    Summary::ChangeSubscription summary_change_subscription_;
    SkinPtr skin_;
    Image wallpaper_{};
    String status_line_;
    bool in_replace_text_{};
    bool stop_open_{};
    bool warning_handle_count_{};
    std::size_t selected_id_count_{};
    std::unordered_map<String, std::shared_ptr<TopLevelPanel>> windows_;
    std::unordered_map<String, PaneFactory> pane_factories_;
    // 初回オープン前に情報を付けた固定ペイン用の、所有権を持たない索引。
    std::unordered_map<String, std::weak_ptr<TopLevelPanel>> registered_panes_;
    PanePresenter pane_presenter_;
    CwxPathRouter cwx_path_router_;
    PackageTreeCreator package_tree_creator_;
    PackageContentCreator package_content_creator_;
    std::unordered_map<const lib::Widget*, std::shared_ptr<TLPData>> metadata_;
    std::vector<String> selected_files_;
    [[nodiscard]] std::shared_ptr<TopLevelPanel> openBoundPanel(
        String key, String path, std::shared_ptr<void> sourceOwner,
        bool shellActivate, std::shared_ptr<TopLevelPanel> duplicateBase);
    /// Dと同じく、可変のパスIDが変わっても所有者の同一性で有効な場面を再利用する。
    [[nodiscard]] std::shared_ptr<TopLevelPanel> sceneWindowFrom(
        StringView keyPrefix, const path::CWXPath* owner,
        const void* sourceIdentity, bool shellActivate);
    bool inheritOpenedPaths(const std::shared_ptr<TopLevelPanel>& panel,
                            const std::shared_ptr<TopLevelPanel>& base);
    std::function<void(String, bool)> path_replacement_;
    std::function<void(ReplaceIDRequest)> id_replacement_;
    std::function<bool(String, bool)> file_selection_;
    std::function<void(String, std::vector<String>)> name_selection_;
    void selectName(String key, std::vector<String> ids, bool shellActivate);
    void retainPanelMetadata(const std::shared_ptr<TopLevelPanel>& panel,
                             std::shared_ptr<void> owner = {});
    void clearPanelMetadata(const std::shared_ptr<TopLevelPanel>& panel) noexcept;
    void forgetRegisteredPane(StringView key) noexcept;

    void initializeSignals();
    void applyToolBarRefresh();
    void refreshToolBarCapabilities(const std::map<MenuID, std::function<bool()>>& overrides);
    void clearFlagDirExpandedS(const SummaryPtr&);
    void clearStepDirExpandedS(const SummaryPtr&);
    void clearVariantDirExpandedS(const SummaryPtr&);
    void clearAreaDirExpandedS(const SummaryPtr&);
    void clearBattleDirExpandedS(const SummaryPtr&);
    void clearPackageDirExpandedS(const SummaryPtr&);
    void clearFlagDirExpanded();
    void clearStepDirExpanded();
    void clearVariantDirExpanded();
    void clearAreaDirExpanded();
    void clearBattleDirExpanded();
    void clearPackageDirExpanded();
    void reconstructContentsToolBox();
    void activate(const std::shared_ptr<TopLevelPanel>& panel,
                  bool shellActivate);
    void openPanel(const std::shared_ptr<TopLevelPanel>& panel,
                   String key, bool shellActivate,
                   StringView paneFamily = {});
    [[nodiscard]] PanelPtr panelFor(String key, bool shellActivate);
    [[nodiscard]] std::shared_ptr<TopLevelPanel>
    openRegisteredPanel(String key, String title, bool shellActivate,
                        std::shared_ptr<void> owner = {});
};

[[nodiscard]] SkinPtr findSkin(Commons& comm, Props& prop, const Summary* summ,
                               std::optional<String> type = std::nullopt,
                               std::optional<String> name = std::nullopt,
                               String legacyEngine = {},
                               bool appendClassicSkin = true);
[[nodiscard]] SkinPtr findSkin(Commons& comm, Props& prop, const Summary* summ,
                               bool legacy, String sPath, String type,
                               std::optional<String> name = std::nullopt,
                               String legacyEngine = {},
                               bool appendClassicSkin = true);

}  // namespace cwxeditor::cwx::editor::gui::dwt::commons
