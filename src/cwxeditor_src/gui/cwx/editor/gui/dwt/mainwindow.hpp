#pragma once

// cwx/editor/gui/dwt/mainwindow.d
// のC++20移植。 シェル・ドック
// ・メニュー・履歴・実行プロセスを
// 調整する役割を、共通の部品・サー
// ビス層で保つ。 ネイティブハンド
// ルやABI固有の型は公開しない。

#include "cwxeditor_src/gui/cwx/editor/gui/dwt/commons.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/cardwindow.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/comment.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/areatable.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/flagspane.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/directorywindow.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/system.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dmenu.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dprops.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dskin.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/namewindow.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/loader.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/events.hpp"
#include "cwxeditor_src/lib/ipc.hpp"
#include "cwxeditor_src/lib/services.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <type_traits>
#include <vector>

namespace cwxeditor::lib {
class EditorRuntimeContext;
class FileSaveTransaction;
}

namespace cwxeditor::cwx::editor::gui::dwt::replacedialog {
class ReplaceDialog;
}
namespace cwxeditor::cwx::editor::gui::dwt::dockingfolder {
class DockingFolder;
}
namespace cwxeditor::cwx::editor::gui::dwt::image {
class Images;
}
namespace cwxeditor::cwx::editor::gui::dwt::eventwindow {
class EventWindow;
}
namespace cwxeditor::cwx::editor::gui::dwt::settingsdialog {
class SettingsDialog;
struct Callbacks;
}
namespace cwxeditor::cwx::editor::gui::dwt::summarydialog {
class SummaryDialog;
}
namespace cwxeditor::cwx::editor::gui::dwt::history {
class ScenarioHistoryDialog;
}
namespace cwxeditor::cwx::editor::gui::dwt::partyhistory {
class ExecutedPartyHistoryDialog;
}
namespace cwxeditor::cwx::editor::gui::dwt::customtoolbar {
class ToolBarCustomDialog;
}
namespace cwxeditor::cwx::editor::gui::dwt::castcarddialog {
class CastCardDialog;
}
namespace cwxeditor::cwx::editor::gui::dwt::smalldialogs {
class CreateScenarioDialog;
class ErrorDialog;
}

namespace cwxeditor::cwx::editor::gui::dwt::mainwindow {

[[nodiscard]] bool handleWarningState(bool current, std::uint32_t count,
                                      std::uint32_t warning,
                                      std::uint32_t lift) noexcept;

namespace replacedialog_ns = ::cwxeditor::cwx::editor::gui::dwt::replacedialog;
namespace dockingfolder_ns = ::cwxeditor::cwx::editor::gui::dwt::dockingfolder;
namespace image_ns = ::cwxeditor::cwx::editor::gui::dwt::image;
namespace cardwindow_ns = ::cwxeditor::cwx::editor::gui::dwt::cardwindow;
namespace areatable_ns = ::cwxeditor::cwx::editor::gui::dwt::areatable;
namespace flagspane_ns = ::cwxeditor::cwx::editor::gui::dwt::flagspane;
namespace directorywindow_ns = ::cwxeditor::cwx::editor::gui::dwt::directorywindow;
namespace history_ns = ::cwxeditor::cwx::editor::gui::dwt::history;
namespace partyhistory_ns = ::cwxeditor::cwx::editor::gui::dwt::partyhistory;
namespace namewindow_ns = ::cwxeditor::cwx::editor::gui::dwt::namewindow;
namespace comment_ns = ::cwxeditor::cwx::editor::gui::dwt::comment;
namespace customtoolbar_ns =
    ::cwxeditor::cwx::editor::gui::dwt::customtoolbar;

namespace castcarddialog_ns =
    ::cwxeditor::cwx::editor::gui::dwt::castcarddialog;
namespace summarydialog_ns =
    ::cwxeditor::cwx::editor::gui::dwt::summarydialog;
namespace smalldialogs_ns =
    ::cwxeditor::cwx::editor::gui::dwt::smalldialogs;

namespace lib = ::cwxeditor::lib;
namespace commons = ::cwxeditor::cwx::editor::gui::dwt::commons;
namespace dmenu = ::cwxeditor::cwx::editor::gui::dwt::dmenu;
namespace dprops = ::cwxeditor::cwx::editor::gui::dwt::dprops;
namespace dskin = ::cwxeditor::cwx::editor::gui::dwt::dskin;
namespace filesync = ::cwxeditor::cwx::filesync;
namespace skin = ::cwxeditor::cwx::skin;
namespace structs = ::cwxeditor::cwx::structs;
namespace summary_ns = ::cwxeditor::cwx::summary;
namespace system_ns = ::cwxeditor::cwx::system;
namespace types = ::cwxeditor::cwx::types;

using String = lib::String;
using StringView = lib::StringView;
using MenuID = types::MenuID;
using Props = dprops::Props;
using Summary = summary_ns::Summary;
using SummaryPtr = summary_ns::Summary::Owner;
using SaveOption = summary_ns::SaveOption;
using LoadOption = summary_ns::LoadOption;

// メイン画面の二つのデータペインで共用するラベルと資源ID。
// 設定値だけを持ち、動作はAreaTableとFlagsPane
// に置く。廃止したdatawindowの状態管理は使わない。
struct DataPaneProps {
    String scenarioView{"Scenario"};
    String variableView{"Variables"};
    String dataTabName{"Data"};
    String areasTabName{"Areas"};
    String flagTabName{"Flags"};
    String areaId{"ID"};
    String areaName{"Name"};
    String areaCount{"Use count"};
    String flagName{"Name"};
    String flagInitialValue{"Initial value"};
    String flagInitialization{"Initialization"};
    lib::ResourceId tableViewImage{};
    lib::ResourceId variableViewImage{};
    bool readOnly{};
    bool showAreaDirectoryTree{true};
};
using FileSync = filesync::FileSync;
using ExecutionParty = structs::ExecutionParty;
using LaunchOption = structs::LaunchOption;
using OpenHistory = structs::OpenHistory;
using SelectionEvent = dmenu::SelectionEvent;
using MenuPtr = std::shared_ptr<dmenu::Menu>;
using ToolBarPtr = std::shared_ptr<dmenu::ToolBar>;
using WindowPtr = commons::WindowPtr;
using ContainerPtr = commons::ContainerPtr;
using PanelPtr = commons::PanelPtr;
using WidgetPtr = commons::WidgetPtr;
using SkinPtr = commons::SkinPtr;

/// Editorの調整処理でなく、アプリの外枠が担当する操作のホ
/// スト通知。 すべて省略可能で、ヘッドレスでも安全に使える。
struct MainWindowContext {
    lib::EditorRuntimeContext* runtimeContext{};
    std::function<bool(const String&, const std::vector<String>&,
                       const std::filesystem::path&)> executeEngine;
    std::function<void(String)> status;
    std::function<void()> redraw;
    std::function<void()> settings;
    std::function<void()> versionInfo;
    std::function<void()> customizeToolBar;
    std::function<void(String)> selectFile;
    std::function<bool()> confirmClose;
    std::function<void()> requestHome;
    std::function<void()> requestEngine;
    std::function<void()> requestData;
};

/// Dのドック管理オブジェクトに対応する共通型。
struct DockState {
    std::vector<String> panes;
    String selected;
    bool moving{};
    bool canClose{true};

    void select(String key) { selected = std::move(key); }
    void close(StringView key) {
        std::erase(panes, String(key));
        if (selected == key) selected.clear();
    }
    void add(String key) {
        if (std::find(panes.begin(), panes.end(), key) == panes.end())
            panes.push_back(std::move(key));
    }
};

/// 置換ダイアログは、サービスに依存しない状態オブジェクトで表す。
struct ReplaceDialogState {
    bool open{};
    String text;
    void show() noexcept { open = true; }
    void close() noexcept { open = false; }
};

class MainWindow final : public commons::TopLevelPanel {
public:
    enum class QSaveType { open, create, reload, close, home };
    enum class DockDirection { north, east, south, west };

    class RefreshTitle {
    public:
        explicit RefreshTitle(MainWindow* owner = nullptr) : owner_(owner) {}
        void run();
        MainWindow* owner_{};
    };

    class DTListener {
    public:
        explicit DTListener(MainWindow* owner = nullptr) : owner_(owner) {}
        void dragEnter(const std::vector<String>& paths);
        void drop(const std::vector<String>& paths);
        MainWindow* owner_{};
    };

    class SListener {
    public:
        explicit SListener(MainWindow* owner = nullptr) : owner_(owner) {}
        void shellClosed(bool& doit);
        MainWindow* owner_{};
    };

    class DListener {
    public:
        explicit DListener(MainWindow* owner = nullptr) : owner_(owner) {}
        void widgetDisposed();
        MainWindow* owner_{};
    };

    class Hist : public std::enable_shared_from_this<Hist> {
    public:
        enum class Type { Open, Import };
        Hist(MainWindow* owner, Type type, int number, String hist);
        void append(const MenuPtr& menu);
        void run();
        void delHist();
        static String cuthist(String hist, int cut);
        [[nodiscard]] bool unittest() const;

        MainWindow* owner_{};
        Type type_{Type::Open};
        String hist_;
        int number_{};
        std::weak_ptr<std::atomic_bool> owner_lifetime_;
        [[nodiscard]] bool ownerAlive() const noexcept;
    };

    class OpenCWXPath {
    public:
        explicit OpenCWXPath(MainWindow* owner = nullptr) : owner_(owner) {}
        String path;
        void run();
        MainWindow* owner_{};
    };
    class ReloadSettings {
    public:
        explicit ReloadSettings(MainWindow* owner = nullptr) : owner_(owner) {}
        void run();
        MainWindow* owner_{};
    };
    class SelectFile {
    public:
        explicit SelectFile(MainWindow* owner = nullptr) : owner_(owner) {}
        String path;
        void run();
        MainWindow* owner_{};
    };

    class KeyDownFilter {
    public:
        explicit KeyDownFilter(MainWindow* owner = nullptr) : owner_(owner) {}
        void handleEvent(lib::KeyEvent& event);
        MainWindow* owner_{};
    };
    class SwitchTab {
    public:
        explicit SwitchTab(MainWindow* owner = nullptr) : owner_(owner) {}
        template <typename TabFolder, typename Tab>
        [[nodiscard]] bool switchTab(const TabFolder&, const Tab&,
                                     const lib::WheelEvent&) {
            if (owner_) owner_->redrawAll();
            return true;
        }
        void spinUpDown(const lib::WheelEvent& event, const WidgetPtr& spinner);
        void handleEvent(lib::WheelEvent& event);
        MainWindow* owner_{};
        WidgetPtr oldFocus_;
    };
    class TabfPaint {
    public:
        explicit TabfPaint(MainWindow* owner = nullptr) : owner_(owner) {}
        void paintControl(lib::Rect bounds);
        MainWindow* owner_{};
    };
    class TabMenu {
    public:
        TabMenu(MainWindow* owner, String paneKey);
        [[nodiscard]] bool canCreateSubWindow() const;
        [[nodiscard]] bool canWalkTab() const;
        [[nodiscard]] bool canClose() const;
        [[nodiscard]] bool canCloseEtc() const;
        [[nodiscard]] bool canCloseLeft() const;
        [[nodiscard]] bool canCloseRight() const;
        void createSubWindow(const SelectionEvent& event);
        void leftTab(const SelectionEvent& event);
        void rightTab(const SelectionEvent& event);
        void close(const SelectionEvent& event);
        void closeEtc(const SelectionEvent& event);
        void closeLeft(const SelectionEvent& event);
        void closeRight(const SelectionEvent& event);
        void closeAll(const SelectionEvent& event);

    private:
        [[nodiscard]] String controlKey() const;
        void rebuildMenu();

    public:
        MainWindow* owner_{};
        String paneKey_;
        std::shared_ptr<lib::Menu> menu_{};
    };
    class MenuShown {
    public:
        explicit MenuShown(MainWindow* owner = nullptr) : owner_(owner) {}
        void menuShown();
        MainWindow* owner_{};
    };

    template <typename T>
    class RadioGroupAndSet {
    public:
        std::vector<T> group;
        std::unordered_set<MenuID> menuIDs;
    };
    template <typename... Args>
    struct MenuAction { MenuID id{MenuID::None}; };
    template <typename... Args>
    struct ToolAction { MenuID id{MenuID::None}; };
    template <typename... Args>
    struct NewCard { MenuID id{MenuID::None}; };

    using MenuTable = std::map<MenuID, bool>;
    using SelectionHandler = std::function<void(const SelectionEvent&)>;
    using MessageHandler = std::function<String(String)>;
    using ContinueHandler = std::function<bool()>;

    MainWindow(String appPath, const system_ns::System& sys, Props& prop,
               LaunchOption opt = {}, lib::Services services = {},
               MainWindowContext context = {});
    explicit MainWindow(Props& prop, LaunchOption opt = {},
                        lib::Services services = {},
                        MainWindowContext context = {});
    ~MainWindow() override;

    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    void refreshExecEngine();
    [[nodiscard]] String origScenarioPath() const;
    [[nodiscard]] String refreshExecEngineImpl(const MenuPtr& menu,
                                               const MenuPtr& withParty,
                                               bool autoSelect);
    [[nodiscard]] bool canDelNotExistsParty() const;
    void delNotExistsParty();
    void refExecutedParties();
    void refImportHistory();
    void refreshOuterTools();
    void refreshOuterToolsImpl(const MenuPtr& menu);
    void refreshTitle();
    void updateChangeCount();
    void backupThr();
    void backupThr(std::chrono::steady_clock::time_point now);
    // Dと同じく、共通のUIイベントの入口で配送処理の失敗を捕捉す
    // る。 起動・保存状態の取得・終了は、この復旧経路の外で扱う。
    void dispatchUiEvent(const lib::Action& event);
    // 所有者側のUI処理一回の後に呼ぶ。ネイティブの埋め込み配送も含む。
    void uiDispatchComplete() noexcept { caught_changing_ = false; }
    [[nodiscard]] bool createBackup();
    void openScenarioNewWin();
    void createScenarioNewWin();
    void createScenario();
    void finishCreateScenario(smalldialogs_ns::CreateScenarioDialog& dialog);
    [[nodiscard]] bool createScenarioFromLaunchOption();
    void dropFiles(const std::vector<String>& paths);

    [[nodiscard]] LoadOption loadOption(const Summary* old) const;
    void reload();
    void reloadCurrentScenario();
    void openScenarioM();
    static int cmp(StringView a, StringView b);
    static int ncmp(StringView a, StringView b);
    void openScenario(SummaryPtr summ, const std::vector<String>& errorFiles,
                      const std::vector<loader::ScenarioLoadIssue>& issues = {});
    void clearScenarioPanes();
    void resetOpt();
    void openScenarioImpl(SummaryPtr summ, const std::vector<String>& errorFiles,
                          std::vector<String> openPaths = {},
                          String selectFile = {},
                          const std::vector<loader::ScenarioLoadIssue>& issues = {});
    void openScenario();
    void openScenario(String fname, std::function<void()> failure, Summary::ReloadOperation reload = {},
                      std::optional<LoadOption> options = {});
    void playSavedSound();
    void saveScenario();
    void savec(WindowPtr shell);
    [[nodiscard]] SaveOption createSaveOpt(bool isClassic, bool initial) const;
    [[nodiscard]] bool beforeSave();
    [[nodiscard]] bool save(WindowPtr shell, bool backupSave = false);
    void saveScenarioA();
    [[nodiscard]] bool saveScenarioAImpl(WindowPtr shell);
    // 保存先の選択後に行う「名前を付けて保存」。同期処理とWeb版ALLの非同期処理で使う。
    bool performSaveAs(std::filesystem::path destination, bool classic,
                       bool export_sc, bool old_classic);

    void updateExecEngineWithPartyName();
    void updateExecEngineWithPartyNameMI();
    void updateExecEngineWithPartyNameTI();
    [[nodiscard]] String execEngineWithLastPartyText() const;
    void execEngineP(String path, String scenario = {}, ExecutionParty ep = {});
    [[nodiscard]] bool canExecEngine() const;
    [[nodiscard]] bool canExecEngineWithLastParty() const;
    [[nodiscard]] bool canExecEngineWithLastParty2() const;
    [[nodiscard]] bool canExecEngineWithPartyMenu() const;
    [[nodiscard]] bool canExecEngineWithParty() const;
    void execEngine();
    void execEngineWithLastParty();
    [[nodiscard]] bool canExecClassic() const;
    [[nodiscard]] String execEnginePath() const;

    void openDataWindow();
    void openFlagWindow();
    void openDirWindow();
    void openCouponWindow();
    void openGossipWindow();
    void openCompleteStampWindow();
    void openKeyCodeWindow();
    void openCellNameWindow();
    void openCardGroupWindow();
    void exitAll();
    // ネイティブウィンドウがあり、ALLがGUI・Storageを終了す
    // る前に設定を取得する。 シナリオ内容の保存や確認画面は行わない。
    bool saveSessionStateForShutdown() noexcept;
    void cancelShutdownAfterStorageFailure();
    // 起動・イベントループの致命的失敗は通常終了と区別し、 不完全
    // なウィンドウ・ドックの状態で最後の正常な設定を置き換えない。
    void suppressSessionStatePersistence() noexcept;
    // UIの致命的失敗では、最後に整合性が確認されたシナリオを上
    // 書きしない。 現在のメモリ上のXML構造を通常のバックア
    // ップの隣へ残し、作成できた場合は復旧ディレクトリを返す。
    [[nodiscard]] String preserveFatalRecoverySnapshot() noexcept;
    void replaceText();
    void clipboardToXML();
    [[nodiscard]] bool qSave(QSaveType type);
    [[nodiscard]] bool qSaveAsync(QSaveType type,
                                  std::function<void(bool)> completed);
    // ALLのExplorerは同じシナリオStorage
    // を共有する。 Editorを覆う前に通常の保存・破棄
    // ・中止を確認し、戻ったときに保持中の文書を読み直す。
    [[nodiscard]] bool prepareExternalNavigation();
    // ブラウザー内のALLでは、SDL/Pthreadのフレームを同期確認ループ
    // で止められない。 これらの版はダイアログのディスパッチャー経由で完了する。
    [[nodiscard]] bool prepareHomeReturnAsync(std::function<void(bool)> completed);
    [[nodiscard]] bool prepareExternalNavigationAsync(
        std::function<void(bool)> completed);
    void reloadAfterExternalNavigation();
    // Data Explorerへ出るときに取るシナリオファイルの状態。
    // 戻ったときは、その間にファイルが実際に変わった場合だけ読み直す。
    [[nodiscard]] String scenarioFilesFingerprint() const;
    std::optional<String> external_navigation_fingerprint_;
    bool external_navigation_discarded_{};
    void refShowMainToolBar();

    [[nodiscard]] bool sendReloadPropsAndSave();
    void sendReloadProps();
    void reloadProps();
    [[nodiscard]] settingsdialog::Callbacks settingsCallbacks();
    void settings();
    [[nodiscard]] bool canEditScenarioHistory() const;
    void editScenarioHistory();
    [[nodiscard]] bool canEditImportHistory() const;
    void editImportHistory();
    [[nodiscard]] bool canDelNotExistsScenario() const;
    void delNotExistsScenario();
    [[nodiscard]] bool canEditExecutedPartyHistory() const;
    void editExecutedPartyHistory();
    void setHistSkin();
    void setHistSkin(std::vector<OpenHistory>& history);
    [[nodiscard]] String findFullHist(StringView hist) const;
    [[nodiscard]] String createFullHistString() const;
    void writeDock();
    void writeDock(std::vector<OpenHistory>& history);
    void delHist(StringView fullHist);
    void delHist(StringView fullHist, std::vector<OpenHistory>& history);
    void addHistory();
    [[nodiscard]] bool canSaveOverwrite() const;
    void createFileMenu();
    void createImportHistoryMenu(const MenuPtr& menu, bool withSelectScenario);

    void pipeThr();
    [[nodiscard]] String createPipeName();
    void sendToPipe(MessageHandler sendRecv, ContinueHandler next);
    void sendPipeThr();
    void sendToPipeImpl(MessageHandler sendRecv, ContinueHandler next);
    void refSkin();
    void refScenario(SummaryPtr summ);
    void refSoundType();

    [[nodiscard]] bool createMainToolBar(bool saveOrder);
    void createExecEngineTI(const ToolBarPtr& bar);
    void createExecEngineWithPartyTI(const ToolBarPtr& bar);
    void createOuterToolsTI(const ToolBarPtr& bar);
    void createOpenImportSourceTI(const ToolBarPtr& bar);
    void customizeToolBar();
    [[nodiscard]] bool updateMainToolBar(bool saveOrder = true);
    template <typename... Args>
    void doMenu(MenuID id, const SelectionEvent& event) {
        const auto action = commons::TopLevelPanel::menuAction(id);
        if (action) action(event);
        menuActionAfter(id);
        static_cast<void>(sizeof...(Args));
    }
    [[nodiscard]] dmenu::MenuItemRef getMenu(MenuID id) const;
    [[nodiscard]] bool canDoMenu(MenuID id) const;
    void raiseEvent(MenuID id, const SelectionEvent& event);
    template <typename Event>
    void raiseEvent(MenuID id, const Event&) {
        raiseEvent(id, SelectionEvent{});
    }
    void redrawAll();
    [[nodiscard]] bool canAddScenario() const;
    void addScenario();
    void addScenario(const std::vector<String>& paths,
                     lib::Action failure = {});
    void openImportSources(const loader::LoadResults& results);
    void openCast();
    void openSkill();
    void openItem();
    void openBeast();
    void openInfo();
    void refreshAll(const SelectionEvent& event = {});

    void createPaneEvent(const ContainerPtr& parent, String paneKey);
    void configureWallpaperForPane(StringView paneKey);
    class TabMenu* tabMenu(String paneKey);
    [[nodiscard]] bool canOpenDirectory() const;
    void openDirectory();
    [[nodiscard]] bool canOpenBackupDirectory() const;
    void openBackupDirectory();
    [[nodiscard]] bool isMainCardWin(const PanelPtr& panel) const;
    void showCardLife(const SelectionEvent& event);
    void showCardList(const SelectionEvent& event);
    void showCardTable(const SelectionEvent& event);
    [[nodiscard]] bool canNewCard() const;
    using canNewCast = std::function<bool()>;
    using canNewSkill = std::function<bool()>;
    using canNewItem = std::function<bool()>;
    using canNewInfo = std::function<bool()>;
    using canNewBeast = std::function<bool()>;
    void newCast();
    void newSkill();
    void newItem();
    void newBeast();
    void newInfo();
    void versionInfo();

    [[nodiscard]] std::vector<ToolBarPtr> toolBars() const;
    void refreshToolBar(const MenuTable& cMenuTbl);
    void setupMenuListener(const MenuPtr& menu);
    template <typename T>
    void menuActionAfterImpl(MenuID id, const T&, const std::vector<MenuID>& = {}) {
        menuActionAfter(id);
    }
    void menuAction(MenuID id, const SelectionEvent& event);
    [[nodiscard]] SelectionHandler menuActionDlg(MenuID id);
    void menuActionAfter(MenuID id);
    void setupMenu(const std::map<MenuID, dmenu::MenuItemRef*>& menus);
    template <typename M>
    void setupMenu(const M& menus) {
        for (const auto& entry : menus) {
            const auto id = entry.first;
            auto item = getMenu(id);
            if (item) item.setEnabled(canDoMenu(id));
        }
    }

    [[nodiscard]] std::vector<String> dockSelect(String key);
    void dockShellClosedEvent();
    [[nodiscard]] bool dockCanMove(StringView ctrlKey, StringView dropPaneKey) const;
    [[nodiscard]] bool dockCanVanish(const DockState* dock, StringView key) const;
    [[nodiscard]] bool dockCanVanish(StringView key) const;
    [[nodiscard]] bool dockCloseCtrl(StringView key);
    [[nodiscard]] std::vector<String> dockMovingShellEvent(String key);
    void dockMoveShellEvent(WindowPtr before, String key);
    [[nodiscard]] String dockNewPaneName(StringView ctrlKey, StringView basePane,
                                          DockDirection direction) const;
    [[nodiscard]] bool dockFirstResize(StringView paneKey);
    /// ホストのサイズ変更時も、ALL上部のデータ・キャスト領域のピクセル高さを保つ。
    void hostedCanvasResized();

    void setStatusLine(String status);
    [[nodiscard]] String title() const override;
    [[nodiscard]] lib::ResourceId image() const noexcept override;
    [[nodiscard]] ContainerPtr shell() const noexcept override;
    [[nodiscard]] std::function<void(String)> statusText() const;
    [[nodiscard]] DockState* dock() noexcept;
    [[nodiscard]] const dockingfolder_ns::DockingFolder* dockingFolder() const noexcept {
        return docking_folder_.get();
    }
    [[nodiscard]] SummaryPtr summary() const noexcept;
    /// メインメニューモデルをテスト用に参照する。
    /// ALLは可視の部品ツリーからメニューを外し
    /// 、このモデルからホスト側の選択部品を描く。
    [[nodiscard]] std::vector<lib::MenuItem> debugMainMenuItems() const;
    /// メニューの契約テストでも、MainWindowが所有する同じMenuID画像管理を使う。
    [[nodiscard]] lib::ResourceId debugMenuImage(MenuID id) const {
        return menuImage(id);
    }
    /// 埋め込みの契約テストから、登録済みのメインメニュー操作を実行する。
    [[nodiscard]] bool debugInvokeMenuAction(MenuID id, const SelectionEvent& event);
    [[nodiscard]] bool saveInProgress() const noexcept { return in_saving_; }
    [[nodiscard]] bool debugSave() { return save(window_); }
    [[nodiscard]] bool debugSelectCard(StringView type, std::size_t index);
    [[nodiscard]] bool debugSelectVariable(StringView type, std::size_t index);
    [[nodiscard]] bool debugTouchCard(StringView type, std::size_t index);
    [[nodiscard]] bool debugOpenCard(StringView type, std::size_t index);
    [[nodiscard]] bool debugOpenArea(std::size_t index);
    [[nodiscard]] bool debugOpenBattle(std::size_t index);
    [[nodiscard]] bool debugOpenAreaEvent(std::size_t index);
    [[nodiscard]] bool debugOpenBattleEvent(std::size_t index);
    [[nodiscard]] bool debugOpenUseEvent(lib::StringView type,
                                         std::size_t index);
    [[nodiscard]] bool debugOpenPackage(std::size_t index);
    /// 製品の完了経路を使い、シナリオを同期的に置き換えるテス
    /// ト用の入口。 省略するのはファイル選択と読み込みだけ。
    void debugOpenScenario(SummaryPtr summary);
    [[nodiscard]] bool debugImportScenario(StringView path);
    [[nodiscard]] bool debugOpenEventContent(std::size_t visibleIndex);
    [[nodiscard]] bool debugPlaceEventContent(types::CType type, bool autoOpen,
                                              std::size_t visibleIndex = 0U);
    [[nodiscard]] bool debugApplyEventContentDialog();
    [[nodiscard]] bool debugCreateSceneCard();
    [[nodiscard]] bool debugApplySceneCardDialog();
    /// 作成済みの製品用の場面ラッパーを、読み取り専用で参照する。
    [[nodiscard]] std::shared_ptr<areawindow::AreaSceneWindow>
    debugAreaSceneWindow(const WidgetPtr& shell) const;
    /// 作成済みの製品用のバトル場面ラッパーを、読み取り専用で参照する。
    [[nodiscard]] std::shared_ptr<areawindow::BattleSceneWindow>
    debugBattleSceneWindow(const WidgetPtr& shell) const;
    [[nodiscard]] std::shared_ptr<cardwindow_ns::CardWindow>
    debugSelectedCardWindow() const { return selectedCardWindow(); }
    [[nodiscard]] std::shared_ptr<eventwindow::EventWindow>
    debugSelectedEventWindow() const { return selectedEventWindow(); }
    [[nodiscard]] const std::shared_ptr<areatable_ns::AreaTable>&
    debugAreaTable() const noexcept { return area_table_; }
    [[nodiscard]] String debugCardState(StringView type) const;
    [[nodiscard]] String debugVariableState() const;
    /// 別経路を作らず、製品の変数ペインを参照する。
    [[nodiscard]] const std::shared_ptr<flagspane_ns::FlagsPane>&
    debugFlagsPane() const noexcept { return flags_pane_; }
    [[nodiscard]] String debugToolbarCustomizerState() const;
    // 通常のコマンドの実際のモデルを参照する。 テスト
    // は別の設定変更経路を作らず、この公開部品を使う。
    [[nodiscard]] auto debugToolbarCustomizer() const {
        return toolbar_custom_dialog_;
    }
    void debugOpenSettings();
    [[nodiscard]] String debugSettingsState() const;
    [[nodiscard]] bool debugSelectSettingsTab(std::size_t index);
    [[nodiscard]] bool debugCloseSettings();
    [[nodiscard]] bool debugOpenDialog(StringView kind);
    [[nodiscard]] String debugDialogState() const;
    [[nodiscard]] std::shared_ptr<lib::Window> debugDialogWindow() const;
    [[nodiscard]] std::shared_ptr<lib::Window>
    debugUiDispatchErrorWindow() const;
    /// モードレスの検索・置換画面をテスト用に参照する。 製品のダ
    /// イアログを作成・表示・フォーカス・変更する操作は行わない。
    [[nodiscard]] std::shared_ptr<lib::Window> debugReplaceDialogWindow() const;
    [[nodiscard]] const std::shared_ptr<replacedialog_ns::ReplaceDialog>&
    debugReplaceDialog() const noexcept { return actual_replace_dialog_; }
    [[nodiscard]] bool debugCloseReplaceDialog();
    [[nodiscard]] std::shared_ptr<lib::Window> debugHistoryDialogWindow() const;
    [[nodiscard]] bool debugCloseHistoryDialog();
    [[nodiscard]] std::shared_ptr<lib::Window> debugImportHistoryDialogWindow() const;
    [[nodiscard]] bool debugCloseImportHistoryDialog();
    /// 開いているD形式の履歴画面を操作するテスト用の入口。
    /// Propsを書き換えず、ダイアログ自身の操作を呼ぶ。
    [[nodiscard]] bool debugHistoryDialogAction(bool importHistory, StringView action,
                                                std::size_t index = 0U,
                                                bool checked = true);
    [[nodiscard]] std::shared_ptr<lib::Window> debugSettingsWindow() const;
    [[nodiscard]] bool debugSelectDialogTab(std::size_t index);
    [[nodiscard]] bool debugSetCastField(StringView field, StringView value);
    [[nodiscard]] bool debugApplyCastDialog();
    [[nodiscard]] bool debugApplyCardDialog(StringView type);
    [[nodiscard]] std::size_t debugAreaDirectoryCount() const;
    [[nodiscard]] auto debugDirectoryWindow() const { return directory_window_; }
    [[nodiscard]] auto debugCouponWindow() const { return coupon_window_; }
    [[nodiscard]] auto debugKeyCodeWindow() const { return key_code_window_; }
    [[nodiscard]] auto debugExecutedPartyHistoryDialog() const {
        return executed_party_history_dialog_;
    }
    [[nodiscard]] bool debugModifyAndApplyToolbarCustomizer();
    [[nodiscard]] std::size_t debugImportSourceCount() const;
    [[nodiscard]] bool debugCloseDialog();
    template <typename Range>
    void reNumberingHands(const Range& arr) {
        std::uint64_t number = 1;
        for (auto& item : arr) {
            if (item && item->id() != number) {
                item->id(number);
                if (comm_) {
                    using T = typename Range::value_type::element_type;
                    if constexpr (std::is_base_of_v<::cwxeditor::cwx::card::SkillCard, T>)
                        comm_->refSkill.call(item);
                    else if constexpr (std::is_base_of_v<::cwxeditor::cwx::card::ItemCard, T>)
                        comm_->refItem.call(item);
                    else if constexpr (std::is_base_of_v<::cwxeditor::cwx::card::BeastCard, T>)
                        comm_->refBeast.call(item);
                }
            }
            ++number;
        }
    }
    void reNumberingAll();
    [[nodiscard]] std::shared_ptr<ReplaceDialogState> openReplWin();
    [[nodiscard]] bool openCWXPath(String path, bool shellActivate) override;
    [[nodiscard]] std::vector<String> openedCWXPath() const;
    void doCWX();

    [[nodiscard]] Props* prop() noexcept { return prop_; }
    [[nodiscard]] const Props* prop() const noexcept { return prop_; }
    [[nodiscard]] commons::Commons* commons() noexcept { return comm_; }
    [[nodiscard]] const MainWindowContext& context() const noexcept { return context_; }
    void context(MainWindowContext value) { context_ = std::move(value); }
    [[nodiscard]] bool quitRequested() const noexcept { return quit_; }
    [[nodiscard]] bool startupForwarded() const noexcept { return startup_forwarded_; }
    void requestQuit() noexcept { quit_ = true; }
    // 中止したI/Oが終了するまで、ホストがセッションとPropsを保
    // 持する。 通常の文書置き換え・終了では、UI上でjoinしない。
    void pollBackgroundLoads();
    // 受理済みの非同期保存の書き出しを完了する。 同じ文書と編
    // 集世代が現在も有効な場合だけ、変更済み状態を解除する。
    void pollStoragePersistence();
    [[nodiscard]] bool stopBackgroundLoads();
    [[nodiscard]] bool hasBackgroundLoads() const;

private:
    struct SceneCommentCallbacks final {
        std::function<std::shared_ptr<comment_ns::CommentDialog>(StringView)> make_dialog;
        std::function<lib::Action(lib::Action)> subscribe_updates;
    };
    [[nodiscard]] SceneCommentCallbacks sceneCommentCallbacks();
    std::shared_ptr<comment_ns::CommentDialog> createCommentDialog(StringView text);
    void deliverLoadResult(lib::Action delivery);
    void addScenarioImpl(const std::vector<String>& paths, lib::Action failure,
                         bool choose_files);
    // Dはネイティブ画面が最後の位置・寸法を通知した後、SWT.Dispose
    // でmainWindowの配置を保存する。 同じ終了段階を使い、
    // プラットフォームのループ終了用にデストラクターでの代替も残す。
    void persistMainWindowState() noexcept;
    // ペインが持つ状態表示も、Commonsと同じ表示可否・フォーカスの振り分けを使う。
    void publishPaneStatusLine(const WidgetPtr& owner, String status);
    void finishCloseAfterLoads();
    void updateSaveAsPickerDirectory();
    void ensureReplaceDialog();
    void configureReplaceDialog(std::function<void(replacedialog_ns::ReplaceDialog&)> request);
    void initializeSignals();
    void initializeTypedPanes();
    void configureDockingFolder();
    void mountPaneControls(StringView paneKey);
    void presentDockPanel(StringView resourceKey,
                          const std::shared_ptr<commons::TopLevelPanel>& panel,
                          StringView paneFamily, bool select);
    void mountDefaultDock();
    void loadDockState();
    bool saveDockState() const;
    void rebuildMenus();
    [[nodiscard]] std::optional<bool> routeCWXPath(String path,
                                                    bool shellActivate);
    void retireOwnedMenuBindings();
    [[nodiscard]] bool raiseEventImpl(
        MenuID id, const SelectionEvent& event, StringView controlKey = {},
        std::optional<std::uint32_t> accelerator = std::nullopt);
    [[nodiscard]] String focusedControlKey() const;
    [[nodiscard]] String focusedControlKey(const lib::WidgetPtr& focused) const;
    [[nodiscard]] bool canDispatchFocusedIncSearch() const;
    [[nodiscard]] bool dispatchFocusedIncSearch(const SelectionEvent& event);
    [[nodiscard]] bool dispatchConfiguredPaneShortcut(const lib::KeyEvent& event);
    [[nodiscard]] bool dispatchSelectedMenu(
        MenuID id, const SelectionEvent& event, StringView controlKey = {});
    [[nodiscard]] bool canDispatchSelectedMenu(
        MenuID id, StringView controlKey = {}) const;
    [[nodiscard]] std::shared_ptr<cardwindow_ns::CardWindow>
        selectedCardWindow(StringView controlKey = {}) const;
    [[nodiscard]] std::shared_ptr<eventwindow::EventWindow>
        selectedEventWindow(StringView controlKey = {}) const;
    void retireEventHost(const std::shared_ptr<eventwindow::EventWindow>& editor, bool discardDock);
    [[nodiscard]] std::shared_ptr<areawindow::MenuActionRegistry>
        selectedSceneMenuRegistry(StringView controlKey = {}) const;
    void openSettings(bool automation_hidden);
    void refreshSelectionChrome(StringView key = {});
    void refreshSelectedEventContentsToolBox(StringView key);
    void configureMainDockLayout();
    void refreshAllCompactSelectors(StringView key = {});
    void refreshAllHistory();
    void refreshToolBarHostState();
    void refreshAllViewToolbar(StringView key = {});
    [[nodiscard]] std::shared_ptr<lib::Panel> createAllCompactNavigation();
    void composeRoot(bool preserve_toolbar_wraps = true);
    void rememberToolbarWraps();
    void registerWindow(const String& key, const String& title);
    void dispatchMainThread(std::function<void()> action) const;
    void reportUiDispatchError(String message);
    void releaseClosedCardDialog(const cardwindow_ns::Card* key,
                                 const std::shared_ptr<void>& owner);
    [[nodiscard]] bool pathExists(StringView path) const;
    [[nodiscard]] String trimHistory(String value, std::size_t limit) const;
    [[nodiscard]] bool existsParty(const ExecutionParty& party) const;
    [[nodiscard]] String menuText(MenuID id) const;
    [[nodiscard]] String toolText(MenuID id) const;
    [[nodiscard]] lib::ResourceId menuImage(MenuID id) const;
    [[nodiscard]] lib::ResourceId toolImage(MenuID id) const;
    [[nodiscard]] String scenarioExtension() const;
    [[nodiscard]] lib::ImageData renderCardImage(
        const cardwindow_ns::SummaryPtr& source,
        const cardwindow_ns::CardPtr& card,
        bool show_life = false, bool* has_caption = nullptr) const;

    String app_path_;
    const system_ns::System* system_{};
    Props* prop_{};
    // DはMainWindowの終了中もプロパティ所有者を保持する。C++では借
    // 用なので、 寿命トークンで終了処理が破棄済みPropsへ触れるのを防ぐ。
    std::weak_ptr<void> prop_lifetime_;
    lib::Services services_{};
    std::unique_ptr<image_ns::Images> images_;
    MainWindowContext context_{};
    LaunchOption opt_{};
    FileSync sync_;
    std::unique_ptr<commons::Commons> owned_comm_;
    commons::Commons* comm_{};
    WindowPtr window_;
    ContainerPtr root_;
    std::shared_ptr<lib::CoolBar> toolbar_host_;
    WidgetPtr toolbar_surface_;
    std::shared_ptr<lib::Panel> all_compact_navigation_;
    std::shared_ptr<lib::Panel> all_history_toolbar_;
    std::shared_ptr<lib::Button> all_undo_button_;
    std::shared_ptr<lib::Button> all_redo_button_;
    std::shared_ptr<lib::Panel> all_view_toolbar_host_;
    WidgetPtr all_view_toolbar_active_;
    // ALLの操作欄に場面・イベントのツールバーを置いている部品
    // のキー。 dock_.selectedと独立に保持し、デ
    // ータ・側面ペインへ移っても場面操作が消えないようにする。
    String all_view_toolbar_key_;
    std::optional<float> web_all_upper_pane_baseline_ratio_;
    std::optional<float> web_all_last_canvas_width_;
    std::optional<float> web_all_last_canvas_height_;
    std::shared_ptr<lib::ComboBox> all_menu_selector_;
    std::shared_ptr<lib::ComboBox> all_left_selector_;
    std::shared_ptr<lib::ComboBox> all_right_selector_;
    std::shared_ptr<lib::ComboBox> all_scene_selector_;
    std::shared_ptr<lib::ComboBox> all_side_selector_;
    std::vector<String> all_left_selector_keys_;
    std::vector<String> all_right_selector_keys_;
    std::vector<String> all_scene_selector_keys_;
    std::vector<String> all_side_selector_keys_;
    std::unordered_map<String, std::function<WidgetPtr()>>
        all_view_toolbar_factories_;
    std::shared_ptr<lib::StatusBar> status_bar_;
    WidgetPtr focus_;
    DockState dock_;
    std::shared_ptr<dockingfolder_ns::DockingFolder> docking_folder_;
    std::shared_ptr<lib::Panel> typed_host_;
    DataPaneProps data_prop_{};
    areatable_ns::AreaTableContext area_table_context_{};
    std::shared_ptr<areatable_ns::AreaTable> area_table_;
    flagspane_ns::Commons flag_comm_{};
    flagspane_ns::Props flag_props_{};
    std::shared_ptr<flagspane_ns::FlagsPane> flags_pane_;
    cardwindow_ns::Commons card_comm_{};
    cardwindow_ns::Props card_prop_{};
    comment_ns::Commons comment_comm_{};
    std::shared_ptr<cardwindow_ns::CardWindow> cast_window_;
    std::shared_ptr<cardwindow_ns::CardWindow> skill_window_;
    std::shared_ptr<cardwindow_ns::CardWindow> item_window_;
    std::shared_ptr<cardwindow_ns::CardWindow> beast_window_;
    std::shared_ptr<cardwindow_ns::CardWindow> info_window_;
    namewindow_ns::Commons name_comm_{};
    std::vector<lib::Action> name_signal_cleanup_;
    directorywindow_ns::Props directory_prop_{};
    std::shared_ptr<directorywindow_ns::DirectoryWindow> directory_window_;
    std::shared_ptr<namewindow_ns::CouponWindow> coupon_window_;
    std::shared_ptr<namewindow_ns::GossipWindow> gossip_window_;
    std::shared_ptr<namewindow_ns::CompleteStampWindow> complete_stamp_window_;
    std::shared_ptr<namewindow_ns::KeyCodeWindow> key_code_window_;
    std::shared_ptr<namewindow_ns::CellNameWindow> cell_name_window_;
    std::shared_ptr<namewindow_ns::CardGroupWindow> card_group_window_;
    // パス別の場面・イベントウィンドウと、通知用のコンテキストを
    // 所有する。 画面の外枠はCommonsがキャッシュし、こ
    // れらはMainWindowの寿命中に編集動作を保持する。
    std::vector<std::shared_ptr<void>> dynamic_panes_;
    // パスに結び付くArea/Battle/Event・手札・取り込みペインは、シナ
    // リオ切り替えでまとめて終了する。 一時的なモードレス画面とは分けて管理する。
    std::vector<std::shared_ptr<void>> scenario_panes_;
    struct ActiveCardDialog {
        std::shared_ptr<void> owner;
        std::shared_ptr<lib::Window> window;
        cardwindow_ns::CardType type{cardwindow_ns::CardType::Cast};
        lib::Action apply;
        std::function<bool(StringView, bool)> openPath;
    };
    // Dは同じモデルを指す複製ビューでも、ビューごとにモード
    // レス編集画面を所有する。 両方の識別子をキーにし、別ビ
    // ューの編集で先のビューの画面を再開しないようにする。
    struct SceneDialogKey {
        const void* view{};
        const void* target{};
        [[nodiscard]] bool operator==(const SceneDialogKey& other) const noexcept {
            return view == other.view && target == other.target;
        }
    };
    struct SceneDialogKeyHash {
        [[nodiscard]] std::size_t operator()(const SceneDialogKey& key) const noexcept {
            const auto view_hash = std::hash<const void*>{}(key.view);
            const auto target_hash = std::hash<const void*>{}(key.target);
            return view_hash ^ (target_hash << 1U);
        }
    };
    std::unordered_map<const cardwindow_ns::Card*, ActiveCardDialog>
        active_card_dialogs_;
    std::weak_ptr<castcarddialog_ns::CastCardDialog> debug_cast_dialog_;
    std::unordered_map<SceneDialogKey, ActiveCardDialog, SceneDialogKeyHash>
        active_background_dialogs_;
    std::unordered_map<SceneDialogKey, ActiveCardDialog, SceneDialogKeyHash>
        active_scene_card_dialogs_;
    // 弱参照の登録表。共通の履歴位置はMainWindowでなくペインが所有する。 両方の識別子
    // を組み合わせ、取り込み・読み取り専用のSummaryやアドレス再利用で履歴が混ざらないよう
    // にする。 Dのcommons.dのopenAreaSEと同じく、場面とイベントで共有する。
    struct SceneHistoryEntry {
        std::weak_ptr<Summary> summary;
        std::weak_ptr<void> owner;
        std::weak_ptr<undo::UndoManager> manager;
    };
    std::vector<SceneHistoryEntry> scene_histories_;
    std::shared_ptr<undo::UndoManager> sharedSceneHistory(
        const std::shared_ptr<Summary>& summary, const std::shared_ptr<void>& owner);
    std::unordered_map<String, std::weak_ptr<eventwindow::EventWindow>> event_windows_;
    std::unordered_map<String, lib::Action> debug_scene_create_card_;
    std::unordered_map<String, std::weak_ptr<areawindow::AreaSceneWindow>> debug_area_scene_windows_;
    std::unordered_map<String, std::weak_ptr<areawindow::BattleSceneWindow>> debug_battle_scene_windows_;
    std::unordered_map<String, lib::Action> scene_open_event_;
    std::unordered_map<String, lib::Action> scene_scale_refresh_;
    std::unordered_map<String, std::weak_ptr<cardwindow_ns::CardWindow>>
        dynamic_card_windows_;
    std::unordered_map<String, std::shared_ptr<areawindow::MenuActionRegistry>>
        scene_menu_registries_;
    // 編集中のSummaryごとに共有し、複製したArea/Battle
    // ビューのモデル・配置更新を 同じシナリオの他のビューだけへ送る。
    std::unordered_map<const Summary*,
                       std::shared_ptr<areawindow::SceneViewRegistry>>
        scene_view_registries_;
    std::shared_ptr<lib::Window> debug_dialog_window_{};
    String dock_state_file_;
    String pending_dock_state_;
    // ホスト内のモーダル画面は、メニュー通知が戻った後も完了まで残る。 ネイテ
    // ィブのcreateScenario()は従来のスタック上の窓口を保つ。
    std::shared_ptr<smalldialogs_ns::CreateScenarioDialog>
        create_scenario_dialog_;
    std::shared_ptr<ReplaceDialogState> replace_dialog_;
    std::shared_ptr<replacedialog_ns::ReplaceDialog> actual_replace_dialog_;
    std::shared_ptr<settingsdialog::SettingsDialog> settings_dialog_;
    std::shared_ptr<history_ns::ScenarioHistoryDialog> scenario_history_dialog_;
    std::shared_ptr<history_ns::ScenarioHistoryDialog> import_history_dialog_;
    std::shared_ptr<partyhistory_ns::ExecutedPartyHistoryDialog>
        executed_party_history_dialog_;
    // DのSummaryDialogはモードレスなので、閉じるまで保持する。
    // スタック上だけに置くとopen()から戻ってすぐ破棄されてしまう。
    std::shared_ptr<summarydialog_ns::SummaryDialog> summary_dialog_;
    // Dと同じく、モードレスの配送エラー画面を終了通知まで一つ保持する。
    std::shared_ptr<smalldialogs_ns::ErrorDialog> ui_dispatch_error_dialog_;
    // Dと同じく、ツールバー編集画面を閉じるまで保持する。
    // スタック上だけに置くとcustomizeToolBar()
    // が戻った時点でネイティブの通知先が無効になる。
    std::shared_ptr<customtoolbar_ns::ToolBarCustomDialog>
        toolbar_custom_dialog_;
    std::shared_ptr<loader::LoadControl> scenario_load_control_;
    std::shared_ptr<lib::Dialog> previous_scenario_loading_;
    bool scenario_loading_modal_{};
    struct StandaloneArrayImportProgress final {
        std::shared_ptr<loader::LoadControl> control;
        std::weak_ptr<lib::Dialog> dialog;
        std::uint64_t scenario_generation{};
        std::weak_ptr<Summary> expected_summary;
    };
    std::vector<StandaloneArrayImportProgress> standalone_array_import_progress_;
    // 読み込み通知の本文と段階表示。 シナリオ読
    // み込みの完了済み・実行中・次の段階を示す。
    std::shared_ptr<lib::Label> previous_scenario_loading_label_;
    String previous_scenario_loading_message_;
    lib::TimerId scenario_load_timeout_timer_{};
    void cancelScenarioLoadTimeout();
    void cancelScenarioLoading();
    void dismissPreviousScenarioLoading();
    void showScenarioLoading(String message,
                             std::shared_ptr<lib::Dialog>* created_dialog = nullptr);
    // 読み込み通知が表示中でなければ何もしない。
    void setScenarioLoadingPhase(String finished, String current, String next);
    void showScenarioLoadError(const String& message, lib::Action after_dismiss = {});
    std::vector<std::shared_ptr<loader::LoadControl>> background_loads_;
    std::uint64_t load_generation_{};
    std::uint64_t edit_generation_{};
    std::weak_ptr<Summary> pending_persist_summary_;
    std::uint64_t pending_persist_generation_{};
    bool pending_persist_backup_{};
    bool pending_overwrite_notification_{};
    // ブラウザーの永続化が未完了の保存を待つ
    // 呼び出し元向けの通知。 保存後の遷移確
    // 認などで使い、最終結果を一度だけ渡す。
    std::vector<std::function<void(bool)>> pending_persist_waiters_;
    void notifyPendingPersistWaiters(bool saved);
    std::shared_ptr<lib::Dialog> overwrite_save_dialog_;
    std::shared_ptr<lib::Label> overwrite_save_message_;
    std::shared_ptr<lib::Button> overwrite_save_close_;
    std::vector<std::function<void()>> overwrite_save_ack_waiters_;
    void showOverwriteSaveProgress();
    void finishOverwriteSaveProgress(bool success, const String& detail = {});
    void afterOverwriteSaveAcknowledged(std::function<void()> completed);
    struct PendingSaveAs {
        SummaryPtr summary;
        summary_ns::Summary::SaveAsCheckpoint checkpoint;
        std::unique_ptr<lib::FileSaveTransaction> destination;
        SkinPtr oldSkin;
        SkinPtr saveSkin;
        std::uint64_t generation{};
        bool exportScenario{};
        bool oldClassic{};
        commons::Commons::SkinMaterialJournal skinJournal;
    };
    std::unique_ptr<PendingSaveAs> pending_save_as_;
    std::vector<lib::Action> pending_load_deliveries_;
    bool close_pending_{};
    // ホームへ戻る確認が済んだら、後続のホストによる終了でも同
    // じ破棄の判断を使う。 二つ目の保存・終了確認は開かない。
    bool home_return_confirmed_{};
    std::shared_ptr<std::atomic_bool> lifecycle_alive_ =
        std::make_shared<std::atomic_bool>(true);
    std::function<void()> unsubscribe_successful_native_event_dispatch_{};
    std::function<void()> unsubscribe_modal_pump_iteration_{};
    std::function<void()> unsubscribe_successful_task_dispatch_{};
    // DのDockingFolder.FocusLと同じく、最後にフォーカスを得た部品を含
    // むペインを選択する。 メニュー・ツールバー・ALLのUndo/Redoの対象となる。
    std::uint64_t focus_observer_id_{};
    // DのCommons.lastFocusEditor相当。メイン画面内で最後にフォーカスを持った、文
    // 字列メニュー付きの編集部品。 編集メニューとALLのUndo/Redoは、まずこれを対象にする。
    std::weak_ptr<lib::Widget> last_focus_editor_;
    void trackFocusForMenus(const lib::WidgetPtr& focused);
    [[nodiscard]] dmenu::TextMenuModifyPtr lastFocusTextMenu() const;
    [[nodiscard]] bool canDoTextMenu(MenuID id) const;
    bool doTextMenu(MenuID id);
    SummaryPtr summary_;
    SkinPtr skin_;
    bool quit_{};
    bool saveSessionStateForShutdownImpl(const std::optional<String>* frozenHistory) noexcept;
    bool main_window_state_saved_{};
    bool session_state_persistence_suppressed_{};
    bool in_saving_{};
    bool is_changed_{};
    bool caught_changing_{};
    bool backup_active_{};
    bool backup_after_save_{};
    std::weak_ptr<Summary> backup_after_save_summary_;
    std::uint64_t change_count_{};
    std::chrono::steady_clock::time_point last_backup_{};
    std::chrono::steady_clock::time_point last_handle_check_{};
    std::weak_ptr<Summary> last_backup_summary_;
    String old_backup_fingerprint_;
    String pipe_name_;
    bool startup_forwarded_{};
    bool send_pipe_quit_{};
    std::vector<std::pair<MessageHandler, ContinueHandler>> send_queue_;
    mutable std::mutex send_mutex_;
    std::vector<OpenHistory> histories_;
    std::vector<String> opened_paths_;
    std::vector<String> selected_files_;
    std::vector<ExecutionParty> execution_entries_;
    std::vector<ToolBarPtr> tool_bars_;
    // 現在のネイティブCoolBarの順序をmainToolBar.tools
    // へ戻す。 Windows側がドラッグ中に子部品を並べ替える
    // ため、この対応順で保存するtoolsOrderを同期する。
    std::vector<std::size_t> tool_bar_group_order_;
    std::unordered_map<MenuID, SelectionHandler> menu_actions_;
    std::unordered_map<MenuID, std::function<bool()>> menu_enabled_;
    std::unordered_map<MenuID, std::function<bool()>> menu_checked_;
    std::unordered_set<MenuID> no_summary_menu_;
    std::unordered_set<MenuID> main_menu_;
    MenuPtr menu_bar_;
    MenuPtr menu_file_;
    MenuPtr menu_exec_engine_;
    MenuPtr menu_exec_party_;
    MenuPtr tool_exec_engine_;
    MenuPtr tool_exec_party_;
    MenuPtr menu_outer_tools_;
    // ALLのナビゲーション欄に置く、最小化したダイアログの一覧。
    std::shared_ptr<lib::ComboBox> all_dialog_selector_;
    std::vector<std::weak_ptr<lib::Window>> all_dialog_selector_items_;
    void refreshAllDialogSelector();
    bool searchResultSymbols(const ::cwxeditor::cwx::path::CWXPath& object, bool desc,
                             String& text, lib::ResourceId& image,
                             const ::cwxeditor::cwx::summary::Summary* symbols_summary = nullptr);
    MenuPtr tool_outer_tools_;
    MenuPtr menu_open_import_;
    MenuPtr tool_open_import_;
    std::map<MenuID, dmenu::MenuItemRef> menu_items_;
    std::map<MenuID, dmenu::ToolItemRef> tool_items_;
    std::vector<RadioGroupAndSet<dmenu::MenuItemRef>> menu_radio_groups_;
    std::vector<RadioGroupAndSet<dmenu::ToolItemRef>> tool_radio_groups_;
    std::vector<std::unique_ptr<TabMenu>> tab_menus_;
    std::unique_ptr<lib::IpcServer> ipc_server_;
    std::shared_ptr<lib::TaskHandle> ipc_broadcast_task_;
    RefreshTitle refresh_title_;
    DTListener drop_listener_;
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::mainwindow
