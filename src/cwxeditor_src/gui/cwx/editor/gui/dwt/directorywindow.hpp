#pragma once

// cwx/editor/gui/dwt/directorywindow.d
// のC++20移植。 元の二分割のファイル画面の
// 選択・編集動作を、Editor共通のGUI部
// 品で実現する。 ネイティブウィンドウのハンド
// ルは持たず、TreeViewとTableModel
// に表示状態を保ち、 スキン・シェル・アーカ
// イブの処理はホストのコールバックへ渡す。

#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/usecounter.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/customtable.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/incsearch.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/namewindow.hpp"
#include "cwxeditor_src/lib/clipboard.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/drag_drop.hpp"
#include "cwxeditor_src/lib/events.hpp"
#include "cwxeditor_src/lib/portable.hpp"
#include "cwxeditor_src/lib/services.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::areaviewutils {
class PileImage;
class Preview;
}
namespace cwxeditor::cwx::editor::gui::dwt::dprops {
class Props;
}

namespace cwxeditor::cwx::editor::gui::dwt::directorywindow {

namespace lib = ::cwxeditor::lib;
namespace customtable = ::cwxeditor::cwx::editor::gui::dwt::customtable;
namespace incsearch = ::cwxeditor::cwx::editor::gui::dwt::incsearch;
namespace namewindow = ::cwxeditor::cwx::editor::gui::dwt::namewindow;
namespace summary = ::cwxeditor::cwx::summary;
namespace usecounter = ::cwxeditor::cwx::usecounter;

using String = lib::String;
using StringView = lib::StringView;
using Path = std::filesystem::path;
using Parent = lib::Container*;
using Shell = lib::Container*;
using Widget = lib::Widget;
using WidgetPtr = lib::WidgetPtr;
using Container = lib::Container;
using ContainerPtr = std::shared_ptr<Container>;
using Services = lib::Services;
using ResourceId = lib::ResourceId;
using TreeItemId = lib::TreeItemId;
using Summary = summary::Summary;
using SummaryPtr = lib::Owner<Summary>;
using PathId = usecounter::PathId;
using Commons = namewindow::Commons;
using ClipboardData = lib::ClipboardData;
using TableModel = customtable::TableModel;
using TableModelPtr = customtable::TableModelPtr;
using TreeView = lib::TreeView;
using TreeViewPtr = std::shared_ptr<TreeView>;
using Menu = lib::Menu;
using MenuPtr = std::shared_ptr<Menu>;
using TextEdit = lib::TextEdit;
using TextEditPtr = std::shared_ptr<TextEdit>;

/// 共通の変更監視で使う、時刻付きのパス。
struct FC {
    String path;
    std::filesystem::file_time_type time{};

    [[nodiscard]] bool opEquals(const FC& value) const noexcept {
        return path == value.path && time == value.time;
    }
    [[nodiscard]] int opCmp(const FC& value) const noexcept {
        if (path < value.path) return -1;
        if (path > value.path) return 1;
        if (time < value.time) return -1;
        if (time > value.time) return 1;
        return 0;
    }
    [[nodiscard]] String toString() const { return path; }
    FC& operator=(const FC&) noexcept = default;
    FC& opAssign(const FC& value) noexcept {
        path = value.path;
        time = value.time;
        return *this;
    }
};

/// directorywindow.dが使うstructs.OuterToolに対応する共通型。
struct OuterTool {
    String name;
    String command;
    String workDir;
    String mnemonic;
    String hotkey;
    ResourceId icon{};
};

struct DirectoryImages {
    ResourceId folder{};
    ResourceId cards{};
    ResourceId backs{};
    ResourceId bgm{};
    ResourceId se{};
    ResourceId text{};
    ResourceId unknown{};
    ResourceId skeletonFolder{};
    ResourceId skeletonCards{};
    ResourceId skeletonBacks{};
    ResourceId skeletonBgm{};
    ResourceId skeletonSe{};
    ResourceId skeletonText{};
    ResourceId skeletonUnknown{};
    ResourceId window{};
    ResourceId warning{};
};

struct DirectoryMessages {
    String title{"素材ファイル"};
    String fileName{"名前"};
    String fileExt{"拡張子"};
    String fileCount{"使用数"};
    String newFolder{"新しいフォルダ"};
    String dirStatus{"%1% ファイル、%2%"};
    String dirStatusSelected{"%1% ファイル、%2%、%3% 件を選択"};
    String errorExec{"外部ツールを実行できません: %1%"};
};

enum class FileConflictDecision : std::uint8_t { Yes, No, Cancel };

struct DestructiveFilePlan {
    std::vector<String> candidates;
    bool recycle{};
};

/// 従来dprops.Propsから読んでいた値。OS依存の操作はすべてコール
/// バックにし、 ディレクトリビューをヘッドレスでも安全に使えるようにする。
struct DirectoryProperties {
    bool logicalSort{true};
    bool saveInnerImagePath{};
    bool autoUpdateJpy1File{true};
    bool recycleDelete{true};
    bool directorySashVertical{};
    bool maskCardImagePreview{};
    dprops::Props* editorProps{};
    summary::FileSync* fileSync{};
    std::function<bool()> canTraceRefresh;
    bool traceChanges{true};
    std::chrono::milliseconds traceInterval{500};
    int filesSortColumn{};
    int filesSortDirection{};  // 0・1は昇順、2は降順。
    float directorySashLeft{2.0F};
    float directorySashRight{5.0F};
    float fileNameColumn{150.0F};
    float fileExtColumn{60.0F};
    float fileCountColumn{60.0F};
    std::function<void(std::size_t, float)> storeFileColumnWidth;

    String appPath;
    String materialPath;
    String archivePath;
    String selectedArchiveFilter{".zip"};
    std::vector<String> ignorePaths;
    std::vector<String> plainTextFileExtensions{".txt", ".ini", ".sli"};
    std::vector<OuterTool> outerTools;
    DirectoryImages images;
    DirectoryMessages messages;

    std::function<bool(StringView, bool)> isSystemFile;
    std::function<bool(StringView)> isMaterial;
    std::function<bool(StringView)> isCard;
    std::function<bool(StringView)> isBackgroundImage;
    std::function<bool(StringView)> isBGM;
    std::function<bool(StringView)> isSE;
    std::function<ResourceId(StringView, bool)> classifyImage;
    // 拡張子とディレクトリの情報を保った、完全なファイルパスを受け取る。
    std::function<std::vector<String>(StringView)> warnings;
    std::function<std::vector<String>()> notUsedFiles;
    // ホストの確認と変更処理は分ける。 ファイルへ触れる前
    // に、完成した不変の計画全体を確認コールバックへ渡す。
    std::function<FileConflictDecision(const DestructiveFilePlan&)> confirmOverwrite;
    std::function<bool(const DestructiveFilePlan&)> confirmDeleteUnused;
    std::function<String(StringView)> createName;

    std::function<void(StringView)> launch;
    std::function<void(StringView)> openFolder;
    std::function<void()> openDirectoryWindow;
    // 保存の中止・失敗時にはアーカイブ作成を止める。 Dと同じ
    // く、保存を選んだ後で保存に失敗した場合は先へ進まない。
    std::function<bool()> saveScenario;
    std::function<void(StringView, StringView, bool)> refPath;
    std::function<void(StringView, bool)> replacePath;
    // 監視側の表示更新だけを行い、モデルの参照文字列は消去しない。
    std::function<void()> deletePathReferences;
    std::function<void(StringView)> refPaths;
    std::function<void(StringView)> statusMessage;
    std::function<bool(StringView, StringView)> createArchive;
    std::function<bool(StringView, StringView, StringView,
                       const std::vector<String>&)> executeTool;
};

using Props = DirectoryProperties;

struct PreviewData {
    lib::ImageData data{};
    bool transparent{};
};

class DirectoryWindow final
    : public std::enable_shared_from_this<DirectoryWindow> {
public:
    /// TreeNode::valueとTableRowState::dataに保持する値オブジェクト。
    class FileNameObj {
    public:
        FileNameObj() = default;
        explicit FileNameObj(DirectoryWindow& owner, String fullPath);
        FileNameObj(DirectoryWindow& owner, String parent, String basename);
        explicit FileNameObj(String fullPath);

        String array;
        String basename;
        String relPath;
        String ext;
        String extl;
        PathId pathId;
        bool dir{};
        bool material{};
        std::uintmax_t size{};

    private:
        void initialise(DirectoryWindow* owner);
    };

    class DirsSelection {
    public:
        explicit DirsSelection(DirectoryWindow* owner = nullptr) : owner_(owner) {}
        void widgetSelected();

    private:
        DirectoryWindow* owner_{};
    };

    class FilesSelection {
    public:
        explicit FilesSelection(DirectoryWindow* owner = nullptr) : owner_(owner) {}
        void widgetSelected();

    private:
        DirectoryWindow* owner_{};
    };

    class FileSelect {
    public:
        explicit FileSelect(DirectoryWindow* owner = nullptr) : owner_(owner) {}
        void select();
        void mouseUp();
        void mouseDown();
        void mouseDoubleClick();
        void keyPressed(lib::KeyEvent& event);

    private:
        DirectoryWindow* owner_{};
    };

    template <typename Control = Widget>
    class FilesDrag {
    public:
        explicit FilesDrag(DirectoryWindow* owner = nullptr) : owner_(owner) {}
        void dragStart(lib::DragEvent& event) const;
        void dragSetData(lib::DragEvent& event) const;
        void dragFinished(lib::DragEvent& event) const;

    private:
        DirectoryWindow* owner_{};
    };

    template <typename Control = Widget>
    class FilesDrop {
    public:
        explicit FilesDrop(DirectoryWindow* owner = nullptr) : owner_(owner) {}
        void dragOver(lib::DragEvent& event) const;
        void drop(lib::DragEvent& event) const;

    private:
        DirectoryWindow* owner_{};
    };

    class ClosePreview {
    public:
        explicit ClosePreview(DirectoryWindow* owner = nullptr) : owner_(owner) {}
        void widgetSelected();

    private:
        DirectoryWindow* owner_{};
    };

    class PreviewTrigger {
    public:
        explicit PreviewTrigger(DirectoryWindow* owner = nullptr) : owner_(owner) {}
        void mouseExit();
        void mouseMove(float x, float y);

    private:
        DirectoryWindow* owner_{};
    };

    class FDListener {
    public:
        explicit FDListener(DirectoryWindow* owner = nullptr) : owner_(owner) {}
        void widgetDisposed();

    private:
        DirectoryWindow* owner_{};
    };

    class DListener {
    public:
        explicit DListener(DirectoryWindow* owner = nullptr) : owner_(owner) {}
        void widgetDisposed();

    private:
        DirectoryWindow* owner_{};
    };

    class SDListener {
    public:
        explicit SDListener(DirectoryWindow* owner = nullptr) : owner_(owner) {}
        void widgetDisposed();

    private:
        DirectoryWindow* owner_{};
    };

    class RefreshThr {
    public:
        explicit RefreshThr(DirectoryWindow* owner = nullptr) : owner_(owner) {}
        void run();

    private:
        DirectoryWindow* owner_{};
    };

    DirectoryWindow(Commons comm, Props prop = {}, Parent parent = nullptr);
    ~DirectoryWindow();
    void retireHost() noexcept;
    DirectoryWindow(const DirectoryWindow&) = delete;
    DirectoryWindow& operator=(const DirectoryWindow&) = delete;

    void reconstruct(Parent parent);
    [[nodiscard]] const ContainerPtr& widget() const noexcept { return window_; }
    [[nodiscard]] ContainerPtr& widget() noexcept { return window_; }
    [[nodiscard]] const TreeViewPtr& dirs() const noexcept { return dirs_; }
    [[nodiscard]] const TableModelPtr& files() const noexcept { return files_; }
    [[nodiscard]] const MenuPtr& menu() const noexcept { return menu_; }
    [[nodiscard]] const std::shared_ptr<incsearch::IncSearch>& incSearchControl() const noexcept {
        return inc_search_;
    }
    [[nodiscard]] const Commons& commons() const noexcept { return comm_; }
    [[nodiscard]] const Props& properties() const noexcept { return prop_; }
    [[nodiscard]] SummaryPtr summary() const noexcept { return summ_; }

    void removeFiles(const std::vector<String>& files, bool recycle);
    [[nodiscard]] bool canDeleteUnuse() const;
    void deleteUnuse();
    void deleteUnuse(lib::Modifiers modifiers);
    void deleteUnuse(bool recycle);

    [[nodiscard]] ContainerPtr shell() const noexcept { return window_; }
    [[nodiscard]] ResourceId image() const noexcept { return prop_.images.window; }
    [[nodiscard]] String title() const { return prop_.messages.title; }
    [[nodiscard]] std::function<void(StringView)> statusText() const;
    [[nodiscard]] const String& statusLine() const noexcept { return status_line_; }

    void copyFilePath();
    void replace();
    void replaceID();
    [[nodiscard]] bool canReplaceID() const;

    void setTraceEnabled(bool enabled);
    void pollTraceRefresh();
    void stopTrace();
    void resumeTrace();
    void pauseTrace();
    void updateDisplaySettings(bool logicalSort, std::vector<String> ignorePaths);
    void refresh();
    void refresh(SummaryPtr summ);
    [[nodiscard]] bool isChanged();

    [[nodiscard]] bool canOpenDirectory() const;
    void openDirectory();
    [[nodiscard]] bool canCreateNewFolder() const;
    void createNewFolder();
    [[nodiscard]] bool select(String path, bool deselectEtc);
    void quitTrace();
    [[nodiscard]] bool canCreateArchive() const;
    void createArchive();
    void changeVHSide();

    void cut();
    void copy();
    void paste();
    void del();
    void del(lib::Modifiers modifiers);
    void del(bool recycle);
    void clone();
    [[nodiscard]] bool canDoTCPD() const;
    [[nodiscard]] bool canDoT() const;
    [[nodiscard]] bool canDoC() const;
    [[nodiscard]] bool canDoP() const;
    [[nodiscard]] bool canDoD() const;
    [[nodiscard]] bool canDoClone() const;

    [[nodiscard]] bool openCWXPath(String path, bool shellActivate);
    [[nodiscard]] std::vector<String> openedCWXPath() const;

    [[nodiscard]] String selDirPath() const;
    [[nodiscard]] std::vector<String> selFiles() const;
    [[nodiscard]] bool hasFile() const noexcept { return has_file_; }
    [[nodiscard]] bool traceRunning() const noexcept { return trace_running_; }
    [[nodiscard]] bool tracePaused() const noexcept { return trace_paused_; }

    void refreshDirs(String sel = {});
    void refreshFiles(const std::vector<String>& sels = {});
    void updateIcons();
    [[nodiscard]] bool addp(TreeItemId parent, String path, String sel,
                             String top, TreeItemId& topItem);
    void filesSelected();
    void closePreview();
    void previewTrigger(float x, float y, lib::Point screenPosition);
    [[nodiscard]] std::optional<PreviewData> previewImage(StringView path,
                                                           bool& transparent) const;
    [[nodiscard]] String pathRename(String path, String newName);
    [[nodiscard]] String pathRename(TreeItemId item, String newName);
    void updatePaths(StringView from, StringView to);
    void updatePathTree(StringView from, StringView to, bool directory);
    [[nodiscard]] PathId counterPathId(StringView path) const;
    void dirsEditEnd(TreeItemId item, String newText);
    [[nodiscard]] TextEditPtr dirsCreateEditor(TreeItemId item);
    void filesEditEnd(std::size_t item, std::size_t column, String newText);
    void refPaths(const void* sender, StringView parent);
    void refreshUseCount();
    void refreshTitle();
    void refreshStatusLine();
    void delPaths(const void* sender);
    void saveScenario();
    void incSearch();
    void selectAll();
    void createFilesMenu();
    [[nodiscard]] String createDir();
    void createDirDirs();
    void createDirFiles();
    void beginDirectoryNameEdit(const String& path, bool tree);
    void replaceImpl(StringView selected);
    void storeSortParams();

    [[nodiscard]] bool isCutted(StringView file) const;
    [[nodiscard]] String toRelPath(StringView file) const;
    [[nodiscard]] ResourceId fimage(ResourceId image) const;
    [[nodiscard]] ResourceId sfimage(ResourceId image) const;
    [[nodiscard]] ResourceId fimage(StringView file) const;
    [[nodiscard]] ResourceId sfimage(StringView file) const;
    [[nodiscard]] ResourceId fimage(StringView file, bool directory) const;
    [[nodiscard]] bool isCard(StringView file) const;
    [[nodiscard]] std::vector<String> getWarnings(std::size_t row) const;
    [[nodiscard]] std::vector<String> getWarnings(const lib::TableRow& row) const;

    [[nodiscard]] std::vector<String> copyImpl() const;
    [[nodiscard]] bool copy2();
    [[nodiscard]] bool pasteImpl(StringView target,
                                 const std::vector<String>& files,
                                 bool move, bool& fromOut);
    void pasteImpl(const std::vector<String>& files);

private:
    enum class Focus { None, Directories, Files };
    enum class SortColumn { Name, Extension, Count };
    enum class SortDirection { Up, Down };

    void construct(Parent parent);
    void installCallbacks();
    void disconnectCallbacks() noexcept;
    void notifyToolbar() const;
    void setFocus(Focus focus) noexcept { last_focus_ = focus; }
    [[nodiscard]] bool isDef(StringView path, bool directory) const;
    [[nodiscard]] bool isIgnore(StringView path) const;
    [[nodiscard]] bool isScenarioRoot(StringView path) const;
    [[nodiscard]] std::uint32_t useCount(const FileNameObj& file) const;
    [[nodiscard]] PathId toUCPath(const FileNameObj& file) const;
    [[nodiscard]] int comp(StringView a, StringView b) const;
    [[nodiscard]] bool compDirName(StringView a, StringView b) const;
    [[nodiscard]] bool compFullPath(StringView a, StringView b) const;
    [[nodiscard]] bool compFNameS(StringView a, StringView b,
                                  bool ad, bool bd) const;
    [[nodiscard]] bool compFName(const FileNameObj& a, const FileNameObj& b) const;
    [[nodiscard]] bool revCompFName(const FileNameObj& a, const FileNameObj& b) const;
    [[nodiscard]] bool compFExt(const FileNameObj& a, const FileNameObj& b) const;
    [[nodiscard]] bool revCompFExt(const FileNameObj& a, const FileNameObj& b) const;
    [[nodiscard]] bool compFCount(const FileNameObj& a, const FileNameObj& b) const;
    [[nodiscard]] bool revCompFCount(const FileNameObj& a, const FileNameObj& b) const;
    [[nodiscard]] ResourceId classifyImage(StringView file, bool directory) const;
    struct PreparedDirectories {
        lib::TreeView tree;
        std::unordered_map<TreeItemId, FileNameObj> objects;
        TreeItemId next_id{};
    };
    struct PreparedFiles {
        std::vector<FileNameObj> objects;
        std::vector<lib::TableRow> rows;
        std::vector<customtable::TableRowState> states;
        std::vector<std::size_t> selection;
        bool has_file{};
        bool reset_top{};
    };
    [[nodiscard]] TreeItemId appendDirectory(PreparedDirectories& prepared,
                                              TreeItemId parent, StringView path,
                                              StringView selected,
                                              const std::unordered_map<String, bool>& expanded,
                                              const std::unordered_map<String, TreeItemId>& identities,
                                              const std::function<bool()>& current);
    [[nodiscard]] std::optional<PreparedFiles> prepareFiles(
        const String& directory, const std::vector<String>& selections,
        const std::function<bool()>& current);
    [[nodiscard]] bool publishFiles(PreparedFiles& prepared, const std::function<bool()>& current);
    void refreshFilesImpl(const std::vector<String>& selections, bool publish_status);
    void finishFilePublication(std::uint64_t revision, bool publish_status = true);
    [[nodiscard]] const lib::TreeNode* findNode(TreeItemId id) const;
    [[nodiscard]] lib::TreeNode* findNode(TreeItemId id);
    [[nodiscard]] const FileNameObj* rowObject(std::size_t row) const;
    [[nodiscard]] FileNameObj* rowObject(std::size_t row);
    [[nodiscard]] String uniqueTarget(const Path& parent, String name,
                                      bool directory) const;
    [[nodiscard]] bool copyTree(const Path& source, const Path& target,
                                std::error_code& error) const;
    [[nodiscard]] bool moveTree(const Path& source, const Path& target,
                                std::error_code& error) const;
    void syncTraceWatch();
    void requestTraceRefresh();
    void synchronizeJpyFiles(bool reload);
    void closeTraceHandle();
    void closeTraceHandleImpl();
    [[nodiscard]] String formatStatus(std::size_t file_count,
                                      std::uintmax_t bytes,
                                      std::size_t selected) const;
    [[nodiscard]] ContainerPtr dlgParShl() const noexcept { return window_; }
    [[nodiscard]] bool selectedDirectoryIsRoot() const;
    void updateIconsImpl(bool update_tree_icons);
    void clearCut();
    ContainerPtr window_;
    std::shared_ptr<lib::SplitView> split_;
    TreeViewPtr dirs_;
    TableModelPtr files_;
    MenuPtr menu_;
    std::unique_ptr<areaviewutils::Preview> preview_;
    lib::Owner<areaviewutils::PileImage> preview_image_;
    std::shared_ptr<incsearch::IncSearch> inc_search_;
    // 保持された部品やコピー済みの通知群により、UIのコールバックは
    // ビューより長く残る場合がある。 提供元を呼ぶ前に無効化する。
    struct CallbackLifetime {};
    std::shared_ptr<CallbackLifetime> callback_lifetime_{std::make_shared<CallbackLifetime>()};
    std::uint64_t file_selection_listener_{};
    std::uint64_t file_edit_listener_{};
    std::uint64_t file_pointer_listener_{};
    bool callbacks_disconnected_{};

    bool has_file_{};
    Focus last_focus_{Focus::Directories};
    SortColumn sort_column_{SortColumn::Name};
    SortDirection sort_direction_{SortDirection::Up};
    std::vector<FileNameObj> file_objects_;
    std::uint64_t file_refresh_revision_{};
    std::unordered_map<TreeItemId, FileNameObj> directory_objects_;
    std::unordered_set<String> cuts_;
    TreeItemId next_tree_id_{1};
    String status_line_;
    String preview_path_;
    std::uint64_t preview_revision_{};

    Props prop_;
    SummaryPtr summ_;
    Commons comm_;

    mutable std::recursive_mutex mutex_;
    bool trace_running_{};
    bool trace_paused_{};
    bool trace_starting_{};
    bool trace_refresh_requested_{};
    bool trace_refresh_pending_{};
    std::uint64_t trace_generation_{};
    String trace_root_;
    std::weak_ptr<Summary> trace_summary_;
    std::unique_ptr<lib::FileWatchHandle> trace_watch_;
};

class Exec {
public:
    Exec(DirectoryWindow& dirWin, MenuPtr menu, OuterTool tool, int index = 0);
    void run();

private:
    DirectoryWindow* dir_win_{};
    OuterTool tool_;
};

template <typename Control>
void DirectoryWindow::FilesDrag<Control>::dragStart(lib::DragEvent& event) const {
    if (!owner_) return;
    event.data = ClipboardData{};
    const auto values = owner_->copyImpl();
    for (const auto& value : values) event.data.files.emplace_back(value);
    event.selected = lib::DropOperation::Copy | lib::DropOperation::Move;
}

template <typename Control>
void DirectoryWindow::FilesDrag<Control>::dragSetData(lib::DragEvent& event) const {
    dragStart(event);
}

template <typename Control>
void DirectoryWindow::FilesDrag<Control>::dragFinished(lib::DragEvent& event) const {
    if (owner_ && event.selected == lib::DropOperation::Move) owner_->clearCut();
}

template <typename Control>
void DirectoryWindow::FilesDrop<Control>::dragOver(lib::DragEvent& event) const {
    if (!owner_) return;
    event.handled = !event.data.files.empty() || !event.data.text.empty();
}

template <typename Control>
void DirectoryWindow::FilesDrop<Control>::drop(lib::DragEvent& event) const {
    if (!owner_) return;
    std::vector<String> values;
    values.reserve(event.data.files.size());
    for (const auto& value : event.data.files)
        values.push_back(::cwxeditor::cwx::structs::pathToUtf8(value));
    bool from_out{};
    if (owner_->pasteImpl(owner_->selDirPath(), values,
                          event.selected == lib::DropOperation::Move, from_out)) {
        event.handled = true;
    }
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::directorywindow
