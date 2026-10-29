#pragma once

#include "cwxeditor_src/core/cwx/xml.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/layout.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::dockingfolder {

namespace lib = ::cwxeditor::lib;
using XNode = ::cwxeditor::cwx::xml::XNode;
using String = std::string;
using StringView = std::string_view;
using Image = lib::ResourceId;
using Control = lib::WidgetPtr;
using Composite = std::shared_ptr<lib::Container>;
using Pane = std::shared_ptr<lib::Panel>;
using TabF = std::shared_ptr<lib::TabView>;
using SplitPane = std::shared_ptr<lib::SplitView>;
using Canvas = std::shared_ptr<lib::Canvas>;
using Shell = std::shared_ptr<lib::Window>;
using Menu = std::shared_ptr<lib::Menu>;

enum class Dir {
    N,
    E,
    S,
    W,
};

[[nodiscard]] String dirToString(Dir dir);
[[nodiscard]] Dir stringToDir(StringView value);

enum class NewCtrlLocation {
    Right,
    Last,
};

struct CtrlMemory {
    String pane{};
    String pairPane{};
    Dir dir{Dir::N};
    int lWeight{1};
    int rWeight{1};
};

struct PaneMemory {
    String pairPane{};
    Dir dir{Dir::N};
    int lWeight{1};
    int rWeight{1};
    bool isSubWindow{};
    String subPaneKey{};
    int x{-1};
    int y{-1};
    int width{-1};
    int height{-1};
};

class DockingFolderShell final {
public:
    DockingFolderShell() = default;
};

/// Panel・SplitView・TabViewを使う、共通のドックモデル。
class DockingFolder final {
public:
    using TabHandler = std::function<void(StringView)>;
    // ドックモデルはタブの識別と選択を、表示側は実
    // 際のキーボードフォーカスを管理する。 選択通
    // 知が完了し、選択ページの再検証が済んでから呼
    // ぶ。 対象を再確認せずに保持し続けないこと。
    using FocusHandler = std::function<void(Control)>;
    using CloseHandler = std::function<bool(StringView)>;
    using PaneHandler = std::function<void(Composite, StringView)>;
    using ShellHandler = std::function<void()>;
    using MovingShellHandler = std::function<void(StringView)>;
    using MoveShellHandler = std::function<void(Shell, StringView)>;
    using CreateControl = std::function<Control(Composite, StringView)>;
    using CanVanish = std::function<bool(const DockingFolder&, StringView)>;
    using ContextMenuHandler = std::function<void(Menu, Control, lib::Point)>;

    enum class DPos {
        N,
        E,
        S,
        W,
        C,
        NONE,
    };
    inline static constexpr StringView HORIZONTAL{"horizontal"};
    inline static constexpr StringView VERTICAL{"vertical"};

    struct TabRecord {
        String key{};
        String text{};
        Image image{};
        Control control{};
        String paneKey{};
        String restoreKey{};
        bool closable{true};
    };

    using Tab = TabRecord;

    struct DropEvent {
        float x{};
        float y{};
        String data{};
        bool accepted{};
    };

    struct PaintEvent {
        lib::Rect bounds{};
    };

    DockingFolder(Composite parent, int style, bool createTabf,
                  String firstPaneKey = {});
    explicit DockingFolder(Composite parent = {}, int style = 0,
                           String firstPaneKey = {});
    ~DockingFolder();

    DockingFolder(const DockingFolder&) = delete;
    DockingFolder& operator=(const DockingFolder&) = delete;
    DockingFolder(DockingFolder&&) = delete;
    DockingFolder& operator=(DockingFolder&&) = delete;

    [[nodiscard]] Composite component() const noexcept { return _comp; }
    [[nodiscard]] Composite area() const noexcept { return _comp; }
    [[nodiscard]] const Canvas& canvas() const noexcept { return _canvas; }

    static String prefix(String key);
    [[nodiscard]] String newPaneKey(String prefixValue) const;
    [[nodiscard]] String newCtrlKey(String prefixValue) const;
    void updateCloseButtons();

    std::function<bool(StringView)> hasCloseButton;
    std::function<bool(StringView, StringView)> canMove;
    std::function<String(StringView, StringView, Dir)> newPaneName;
    std::function<bool(StringView)> firstResize;
    // MainWindowが画面情報を通じて、DWTのDisplay.getClientArea相当の領域を渡す。
    std::function<lib::Rect()> displayClientArea;
    std::function<bool(StringView)> closeTabWithMiddleClick;
    CanVanish canVanish;
    std::function<bool(StringView)> memoryControl;
    std::function<bool(StringView)> memoryPane;
    // ホストは実際にポップアップを表示するだけ。 先にDockingFolder
    // がタブを解決して選択を通知し、ペインのメニューを渡す。
    ContextMenuHandler showContextMenu;

    [[nodiscard]] String newTabfKey(String prefixValue = "t") const;
    [[nodiscard]] std::vector<Pane> panes() const;
    [[nodiscard]] std::vector<String> paneKeys() const;
    [[nodiscard]] Pane pane(StringView key, Shell priorityShell = {}) const;
    [[nodiscard]] String key(const Composite& pane) const;
    [[nodiscard]] String selectedCtrl(StringView paneKey) const;
    [[nodiscard]] String control(StringView paneKey, float x, float y) const;
    [[nodiscard]] Control control(StringView key) const;
    [[nodiscard]] String keyFromCtrl(const Control& control) const;
    [[nodiscard]] std::vector<Control> allControls() const;
    [[nodiscard]] std::vector<String> controlKeys() const;
    [[nodiscard]] std::vector<Control> controls(StringView paneKey) const;
    [[nodiscard]] std::vector<String> controlKeys(StringView paneKey) const;
    [[nodiscard]] int controlCount(StringView paneKey) const;
    [[nodiscard]] std::vector<Control> showingControls() const;
    [[nodiscard]] Tab* tab(StringView key);
    [[nodiscard]] const Tab* tab(StringView key) const;
    [[nodiscard]] TabF tabView(StringView key) const;
    bool setTabText(StringView key, String text);
    bool setRestoreKey(StringView key, String restoreKey);
    [[nodiscard]] String getTabText(StringView key) const;
    bool tabImage(StringView key, Image image);
    [[nodiscard]] Image tabText(StringView key) const;
    [[nodiscard]] Pane first() const;
    [[nodiscard]] String firstKey() const;
    /// 切り離したペインで使っているネイティブのウィンドウモ
    /// デルを返す。 ドックが保持するのと同じ共有オブジェク
    /// トなので、呼び出し元からOSのGUI実装へ渡せる。
    [[nodiscard]] std::vector<Shell> subWindows() const;
    [[nodiscard]] bool isVanishable(StringView key) const;
    [[nodiscard]] bool vanish(StringView key) const { return isVanishable(key); }

    [[nodiscard]] Pane addPane(StringView base, Dir dir, int lWeight = 1,
                               int rWeight = 1, String paneKey = {});
    [[nodiscard]] Pane addPane(const Composite& base, Dir dir, int lWeight = 1,
                               int rWeight = 1, String paneKey = {});
    /// 横並びのドックペインを兄弟に対する比率で拡縮する。 0.2
    /// なら、対象の幅を主となる兄弟ペインの5分の1にする。
    [[nodiscard]] bool setPaneWidthToMainRatio(StringView paneKey,
                                                float paneToMainRatio);
    /// 現在のドックのルートツリーの外側へペインを追加する。
    /// 古い保存配置に必要なシステムペインがない場合に使
    /// う。任意の末端を分割すると 既存ツリーの片側だけに
    /// なるため、Dと同じく下側全体へ作業領域を復元する。
    [[nodiscard]] Pane addPaneAtRoot(Dir dir, int lWeight = 1,
                                     int rWeight = 1, String paneKey = {});
    // 保存配置の検索が分割境界を返す場合もあるため、結果型をDと揃える。
    [[nodiscard]] Composite addPaneFromMemory(StringView base1, StringView base2,
                                         Dir dir, int lWeight, int rWeight,
                                         String paneKey, bool useDefaultWeights = true);
    [[nodiscard]] Composite addPaneFromCtrlMemory(StringView base, Dir dir,
                                             int lWeight, int rWeight,
                                             String paneKey, String ctrlKey);

    void add(Control control, String tabText, String key, bool select = false,
             NewCtrlLocation location = NewCtrlLocation::Last);
    void add(Control control, String tabText, Image tabImage, String key,
             bool select = false,
             NewCtrlLocation location = NewCtrlLocation::Last);
    void addImpl(Control control, String tabText, Image tabImage, String key,
                 bool select = false,
                 NewCtrlLocation location = NewCtrlLocation::Last,
                 bool loading = false);
    void addFromMemory(const CreateControl& createControl, String tabText,
                       Image tabImage, String key, String pane,
                       String pairPane, Dir dir, bool select = false,
                       NewCtrlLocation location = NewCtrlLocation::Last);

    void setMenu(StringView paneKey, Menu menu);
    [[nodiscard]] Menu getMenu(StringView paneKey) const;
    [[nodiscard]] String paneKeyFromCtrlKey(StringView key) const;
    [[nodiscard]] std::vector<String> findPane(StringView prefixValue,
                                                bool includeSubShells) const;
    [[nodiscard]] Composite findPane2(StringView prefixValue) const;
    [[nodiscard]] std::vector<String> findCtrl(StringView prefixValue,
                                               bool includeSubShells) const;
    [[nodiscard]] std::vector<Tab*> getTabs(const Composite& area);

    std::vector<ShellHandler> shellClosedEvent;
    void createSubWindow(Shell& shell, Composite& area);
    void createSubWindow(StringView tabfKey, Shell& shell, Pane& pane);
    void createSubWindow(StringView key);
    std::vector<MoveShellHandler> moveShellEvent;
    std::vector<MovingShellHandler> movingShellEvent;
    [[nodiscard]] bool isSubWindowPane(StringView paneKey) const;
    [[nodiscard]] bool isSubWindowCtrl(StringView key) const;
    [[nodiscard]] bool isSingleWindow(StringView key) const;
    [[nodiscard]] bool isSinglePane(StringView paneKey) const;

    // 表示コマンドは、既に選択中のペインにもフォーカスを要求できる。
    bool select(StringView key, bool requestFocus = false);
    bool walkLeft(StringView key);
    bool walkRight(StringView key);
    bool close(StringView key);
    /// 対話的な終了確認を通さず部品を取り除く。シナリオ
    /// 置き換え時に、 モデルが終了済みのパス依存ペイン
    /// へ使い、ユーザーのタブ終了設定では拒否させない。
    bool discard(StringView key);
    [[nodiscard]] bool hasEtc(StringView key) const;
    void closeEtc(StringView key);
    [[nodiscard]] bool hasLeft(StringView key) const;
    void closeLeft(StringView key);
    [[nodiscard]] bool hasRight(StringView key) const;
    void closeRight(StringView key);
    void closeAll(StringView key);

    std::vector<PaneHandler> createPaneEvent;
    void addCreatePaneEvent(PaneHandler handler);
    std::vector<TabHandler> selectEvent;
    FocusHandler selectFocusEvent;
    // DのDisplayのFocusInフィルターに対応する。 所有者から確定した全フ
    // ォーカス変更をここへ送る（lib::add_focus_observer参照）。
    void focusIn(const lib::WidgetPtr& focused) { FocusL{this}.focusIn(focused); }
    std::vector<CloseHandler> closeCtrlEvent;

    [[nodiscard]] TabF newTabf(const Composite& parent, String key);
    void removeTabf(const TabF& tabf, bool memory);

    struct DListener {
        DockingFolder* owner{};
        void handleEvent();
    };

    struct CTFL {
        DockingFolder* owner{};
        void close(StringView key);
    };
    [[nodiscard]] String pairKey(const TabF& tabf, Dir& dir, int& lWeight,
                                 int& rWeight) const;
    void close(Tab* tab);
    [[nodiscard]] bool updateSubShellTitle(const Shell& shell);
    [[nodiscard]] String newSashKey() const;
    void putSashTable(const SplitPane& sash, String key);
    void reconstruct(const Control& area);
    void drawDropMark(const Canvas& canvas, float x, float y, float width,
                      float height);
    [[nodiscard]] bool canDrop(const TabF& tabf) const;

    struct PL {
        DockingFolder* owner{};
        void paintControl(const PaintEvent& event);
    };
    struct DSL {
        DockingFolder* owner{};
        TabF tabf{};
        void dragStart(StringView key);
        void dragSetData(StringView key);
        void dragFinished(bool moved);
    };
    static std::vector<Control> afters(const Control& target);
    static void addAfters(const std::vector<Control>& controls);
    static void setInsertMark(const TabF& tabf, Tab* tab, bool after);
    static Tab* selected(const TabF& tabf);
    [[nodiscard]] lib::Rect boundsOnDisplay(const Control& control) const;
    [[nodiscard]] lib::Rect boundsOnCanvas(const Canvas& canvas,
                                           const Control& control) const;
    void newTab(const TabF& tabf, Tab* tab, std::ptrdiff_t index,
                bool updateCloseStyle = false);

    struct DTL {
        DockingFolder* owner{};
        Canvas canvas{};
        void dragEnter(DropEvent& event);
        void dragLeave(DropEvent& event);
        [[nodiscard]] TabF getTabf(float x, float y) const;
        void dragOver(DropEvent& event);
        void drop(DropEvent& event);
    };
    struct SelTab {
        DockingFolder* owner{};
        void widgetSelected(const TabF& tabf);
    };
    struct ClickTab {
        DockingFolder* owner{};
        void mouseDown(const TabF& tabf);
        void mouseUp(const TabF& tabf, float x, float y,
                     lib::PointerButton button);
    };
    struct FocusL {
        DockingFolder* owner{};
        void handleEvent();
        // DのFocusL.control()と同じく、新たにフォーカスを
        // 得た部品を含むページのタブを選ぶ。 ホストのツールバー・ALLの
        // 操作欄・ダイアログなど、全ドックの外の部品なら選択を変えない。
        void focusIn(const lib::WidgetPtr& focused);
    };

    String oldSelection() const { return _oldSel; }
    // FocusLはrequestFocus=falseを使う。 既に
    // ページ内の部品へフォーカスがあるため、タブ選択で再移動させない。
    void selectTab(const TabF& tabf, bool requestFocus = true);
    [[nodiscard]] TabF newSash(const Composite& target,
                               lib::Orientation orientation, bool before,
                               int lWeight, int rWeight, String key);
    void setFirstResize(const SplitPane& sash);

    struct Tabi {
        String key{};
        String name{};
        String restoreKey{};
    };
    struct Tabf {
        String key{};
        int select{-1};
        std::vector<Tabi> tabs{};
    };
    struct Sashf {
        String key{};
        bool vertical{};
        int lWeight{1};
        int rWeight{1};
        std::shared_ptr<Sashf> lSash{};
        std::shared_ptr<Tabf> lTabf{};
        std::shared_ptr<Sashf> rSash{};
        std::shared_ptr<Tabf> rTabf{};
    };
    struct SubWindow;
    struct Area {
        std::shared_ptr<Sashf> sash{};
        std::shared_ptr<Tabf> tabf{};
        std::vector<SubWindow> subShells{};
    };
    struct SubWindow {
        Area area{};
        int x{-1};
        int y{-1};
        int width{-1};
        int height{-1};
    };
    std::shared_ptr<Area> _tree{};

    void saveTree();
    void saveTree(const Control& control, std::shared_ptr<Sashf>& sash,
                  std::shared_ptr<Tabf>& tabf);
    [[nodiscard]] XNode toNode(const std::vector<String>& exclude = {}) const;
    XNode toNode(XNode& parent,
                 const std::vector<String>& exclude = {}) const;
    void toNodeImpl(XNode& node, const std::vector<String>& exclude) const;
    [[nodiscard]] XNode toNodeImpl(XNode& parent,
                                   const std::shared_ptr<Sashf>& sash,
                                   const std::vector<String>& exclude) const;
    [[nodiscard]] XNode toNodeImpl(XNode& parent,
                                   const std::shared_ptr<Tabf>& tabf,
                                   const std::vector<String>& exclude) const;

    struct Proc {
        DockingFolder* owner{};
        Composite parent{};
        CreateControl create{};
        std::function<void(StringView, const TabF&)> addRemoveList{};
        std::function<bool(StringView)> firstResize{};
        int x{-1};
        int y{-1};
        int width{-1};
        int height{-1};
        Shell shell{};
        void createSub();
        void sash(const XNode& node);
        void tabs(const XNode& node);
    };

    [[nodiscard]] static std::shared_ptr<DockingFolder> fromNode(
        const XNode& node, Composite parent, int style,
        std::function<bool(StringView)> hasCloseButton,
        CanVanish canVanish, CreateControl create,
        std::function<bool(StringView)> firstResize = {});

private:
    struct PaneState {
        String key{};
        Pane pane{};
        TabF tabs{};
        Menu menu{};
        Shell shell{};
        std::vector<String> tabKeys{};
    };
    struct ControlState {
        String key{};
        Control control{};
        String paneKey{};
    };
    struct SubWindowState {
        Shell shell{};
        Composite area{};
        Canvas canvas{};
        std::vector<String> paneKeys{};
    };

    int _style{};
    Composite _comp{};
    Pane _area{};
    Canvas _canvas{};
    bool _canSave{true};
    bool _onNewTab{};
    void bindTabInputs(const TabF& tabs);
    std::shared_ptr<void> _lifetime = std::make_shared<int>(0);
    std::unordered_map<String, bool> _closingControls;
    DPos _dropPos{DPos::NONE};
    DPos _drawPos{DPos::NONE};
    Tab* _dragItm{};
    bool _dragMoved{};
    float _dragStartX{};
    float _dragStartY{};
    TabF _drawTabf{};
    String _oldSel{};
    std::vector<String> _paneOrder{};
    std::vector<String> _controlOrder{};
    std::vector<String> _sashOrder{};
    std::unordered_map<String, PaneState> _panes{};
    std::unordered_map<String, ControlState> _controls{};
    std::unordered_map<const lib::Widget*, String> _controlKeys{};
    std::unordered_map<String, std::unique_ptr<TabRecord>> _tabs{};
    std::unordered_map<String, SplitPane> _sashes{};
    std::unordered_map<const lib::Widget*, Composite> _parents{};
    std::vector<SubWindowState> _subWindows{};
    std::unordered_map<String, CtrlMemory> _cMemories{};
    std::unordered_map<String, PaneMemory> _pMemories{};

    [[nodiscard]] Canvas createCanvas(const Composite& parent);
    [[nodiscard]] Canvas dropCanvas(const TabF& tabf) const;
    void hideDropCanvases();
    void resetDockDrag();
    void updateDockDrag(lib::Point screenPoint);
    void finishDockDrag(lib::Point screenPoint, bool commit);
    void updateDropIndicator(const TabF& tabf);
    [[nodiscard]] Pane createArea(const Composite& parent);
    [[nodiscard]] Pane createPane(const Composite& parent, String key,
                                  bool notifyCreated = true);
    [[nodiscard]] bool notifyPaneCreated(const Pane& pane);
    [[nodiscard]] Composite getArea(const Control& control) const;
    [[nodiscard]] Pane paneForControl(const Control& control) const;
    [[nodiscard]] std::size_t pageIndex(const PaneState& state,
                                         StringView key) const;
    [[nodiscard]] std::size_t childIndex(const Composite& parent,
                                         const Control& child) const;
    [[nodiscard]] Composite parentOf(const Control& child) const;
    [[nodiscard]] Shell hostWindow() const;
    void registerParent(const Composite& parent, const Control& child);
    void replaceChild(const Composite& parent, const Control& oldChild,
                      const Control& newChild);
    void collapseSplit(const SplitPane& split);
    void removeControl(StringView key, bool invokeEvents);
    void setFirstResize(const SplitPane& sash, bool resizeFirst);
    void restoreNode(const XNode& node, const Composite& parent,
                     const std::function<void(const XNode&, const TabF&)>&
                         emptyPane);
    void readMemories(const XNode& node);
    [[nodiscard]] TabRecord* tabOwner(StringView key) const;
};

using DockingFolderCTC = DockingFolder;

}  // namespace cwxeditor::cwx::editor::gui::dwt::dockingfolder
