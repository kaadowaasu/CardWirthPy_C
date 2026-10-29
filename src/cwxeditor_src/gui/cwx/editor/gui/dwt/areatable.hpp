#pragma once

// エリア表の移植可能なC++20実装。異なる種
// 類の項目を持つ表とディレクトリツリーを扱う。
// 元と同じ編集モデルを維持し、Editor
// の具体的なコア型と共通部品だけを公開する。

#include "cwxeditor_src/core/cwx/area.hpp"
#include "cwxeditor_src/core/cwx/path.hpp"
#include "cwxeditor_src/core/cwx/skin.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/areawindow.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/comment.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/undo.hpp"
#include "cwxeditor_src/lib/clipboard.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/layout.hpp"
#include "cwxeditor_src/lib/portable.hpp"
#include "cwxeditor_src/lib/services.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::absdialog { class AbsDialog; }
namespace cwxeditor::cwx::editor::gui::dwt::commons { class Commons; }
namespace cwxeditor::cwx::editor::gui::dwt::incsearch { class IncSearch; }
namespace cwxeditor::cwx::editor::gui::dwt::summarydialog { class SummaryDialog; }

namespace cwxeditor::cwx::editor::gui::dwt::areatable {

namespace area = ::cwxeditor::cwx::area;
namespace card = ::cwxeditor::cwx::card;
namespace event = ::cwxeditor::cwx::event;
namespace lib = ::cwxeditor::lib;
namespace path = ::cwxeditor::cwx::path;
namespace summary = ::cwxeditor::cwx::summary;
namespace skin = ::cwxeditor::cwx::skin;
namespace system = ::cwxeditor::cwx::system;
namespace types = ::cwxeditor::cwx::types;
namespace usecounter = ::cwxeditor::cwx::usecounter;
namespace xml = ::cwxeditor::cwx::xml;
namespace areawindow = ::cwxeditor::cwx::editor::gui::dwt::areawindow;
namespace absdialog = ::cwxeditor::cwx::editor::gui::dwt::absdialog;
namespace undo = ::cwxeditor::cwx::editor::gui::dwt::undo;
namespace comment = ::cwxeditor::cwx::editor::gui::dwt::comment;

using Props = absdialog::Props;
using Summary = summary::Summary;
using AbstractArea = area::AbstractArea;
using Area = area::Area;
using Battle = area::Battle;
using Package = area::Package;
using AbstractAreaPtr = std::shared_ptr<AbstractArea>;
using AreaTableSelectionEvent = areawindow::SelectionEvent;

enum class AreaTableType : std::uint8_t { Summary, Area, Battle, Package };
enum class AreaTableSortColumn : std::uint8_t { Id, Name, UseCount };
enum class AreaTableSortDirection : std::uint8_t { Ascending, Descending };

/// DのCommonsデリゲートに代わるホストのコールバック。 ヘッ
/// ドレステストや取り込み画面でも使えるよう、すべて省略可能とする。
struct AreaTableCallbacks {
    std::function<void(AbstractArea*)> ref_area;
    std::function<void(AbstractArea*)> del_area;
    std::function<void(Summary*)> ref_scenario;
    std::function<void()> ref_scenario_name;
    std::function<void()> ref_undo_max;
    std::function<void()> refresh_toolbar;
    std::function<void()> refresh_flags;
    std::function<void()> edit_summary;
    /// D形式のモードレスコメント画面の生成処
    /// 理をまだ持たないホスト向けの互換処理。
    std::function<void()> write_comment;
    std::function<std::shared_ptr<comment::CommentDialog>(
        const AbstractAreaPtr&, std::string_view)> make_comment_dialog;
    std::function<std::string(const AbstractAreaPtr&)> comment_title;
    std::function<lib::Action(std::function<void(int, int)>)> subscribe_import_sort;
    std::function<void(int, int)> publish_import_sort;
    std::function<lib::Action(lib::Action)> subscribe_use_count;
    std::function<void()> publish_use_count;
    std::function<std::size_t(Summary*, const AbstractArea*)> use_count_from;
    std::function<bool()> stop_open; // 複数行を開く合間の、DのCommons.stopOpen相当の処理。
    std::function<void(AbstractArea*, bool, bool)> open_scene;
    std::function<void(AbstractArea*, bool, bool)> open_event;
    std::function<void(AbstractArea*, bool, bool)> open_package;
    /// Dは表の元のSummaryを、各画面を開くコールバック
    /// へ渡す。 取り込み・外部シナリオの表を、IDが同じとい
    /// う理由だけで現在編集中のSummaryへ解決しない。
    std::function<void(Summary*, AbstractArea*, bool, bool)> open_scene_from;
    std::function<void(Summary*, AbstractArea*, bool, bool)> open_event_from;
    std::function<void(Summary*, AbstractArea*, bool, bool)> open_package_from;
    std::function<void(std::uint64_t, AreaTableType)> replace_id;
    std::function<void(Summary*, Summary*, const std::vector<std::string>&)> do_import;
    std::function<bool(std::string_view)> create_directory;
    std::function<std::size_t(const AbstractArea*)> use_count;
    std::function<void(std::string_view)> status;
    std::function<void(std::string_view)> ref_paths;
    std::function<bool(const xml::XNode&,
                       const std::vector<AbstractAreaPtr>&)> confirm_material_copy;
};

/// AreaTableで使う共有サービスと、具体的なareawindow
/// への接続。 AreaWindowContextから作ると、
/// Commonsなしで既存の型付きエリア通知にも接続できる。
struct AreaTableContext {
    lib::Services services{};
    std::shared_ptr<lib::Window> dialog_owner{};
    // 取り込んだSummaryのダイアログにも、Editor共通のスキン解決が必要。
    // 表がホストより長く残っても、弱いトークンで借用Commonポインターを保護する。
    ::cwxeditor::cwx::editor::gui::dwt::commons::Commons* editor_commons{};
    std::weak_ptr<const int> editor_commons_lifetime{};
    AreaTableCallbacks callbacks{};
    /// メイン画面のメニュー表示・実行への接続。選択対象はAreaTable
    /// が所有し、 MenuIDの翻訳とメニューバー・ツ
    /// ールバー共通のコマンドはホストだけが管理する。
    std::function<lib::String(types::MenuID)> menu_text{};
    std::function<void(types::MenuID)> menu_action{};
    std::function<bool(types::MenuID)> menu_enabled{};
    areawindow::AreaWindowContext* area_window{};
    areawindow::MenuActionRegistry* menu_registry{};
    lib::ResourceId summary_image{};
    lib::ResourceId area_dir_image{};
    lib::ResourceId area_image{};
    lib::ResourceId start_area_image{};
    lib::ResourceId battle_image{};
    lib::ResourceId package_image{};
    const skin::Skin* skin{};

    AreaTableContext() = default;
    explicit AreaTableContext(areawindow::AreaWindowContext& host) noexcept
        : services(host.services), area_window(&host),
          menu_registry(host.menu_registry), area_image(host.area_image),
          battle_image(host.battle_image) {}
};

class AreaTable final {
public:
    struct Row {
        AreaTableType type{AreaTableType::Summary};
        AbstractAreaPtr area;
        std::uint64_t id{};
        std::string name;
        std::size_t use_count{};
        lib::ResourceId image{};
    };

    /// ディレクトリノードの親は生の借用参照とし、所有権の循環を防
    /// ぐ。 子は共有所有のままにし、ツリーを編集しやすくする。
    class DirTree final {
    public:
        DirTree* parent{};
        std::string name;
        std::vector<std::shared_ptr<DirTree>> subDirs;

        explicit DirTree(DirTree* parent = nullptr, std::string name = {},
                         std::vector<std::shared_ptr<DirTree>> subDirs = {});
        [[nodiscard]] std::string path() const;
        [[nodiscard]] std::shared_ptr<DirTree> dup(DirTree* new_parent = nullptr) const;
    };

    AreaTable(AreaTableContext& context, Props& prop, bool read_only,
              Summary* import_target = nullptr);
    AreaTable(areawindow::AreaWindowContext& context, Props& prop,
              bool read_only, Summary* import_target = nullptr);
    AreaTable(const AreaTable&) = delete;
    AreaTable& operator=(const AreaTable&) = delete;
    ~AreaTable();

    void construct(const std::shared_ptr<lib::Container>& parent,
                   std::function<void()> refresh_flags = {});
    void construct(const std::shared_ptr<lib::Panel>& parent,
                   std::function<void()> refresh_flags = {}) {
        construct(std::static_pointer_cast<lib::Container>(parent),
                  std::move(refresh_flags));
    }

    [[nodiscard]] bool canChangeVH() const noexcept;
    void changeVHSide();
    [[nodiscard]] bool canCreateDir() const noexcept;
    void createDir(std::string name = {});
    void edit();
    [[nodiscard]] bool canEdit() const noexcept;
    [[nodiscard]] bool canReNumbering() const noexcept;
    void reNumberingAll();
    void reNumberingArea(int index, std::uint64_t new_id);
    void reNumberingBattle(int index, std::uint64_t new_id);
    void reNumberingPackage(int index, std::uint64_t new_id);
    void reNumbering();

    void editSummary(const std::shared_ptr<lib::Container>& parent = {});
    [[nodiscard]] const std::shared_ptr<lib::TableView>& table() const noexcept {
        return table_;
    }
    [[nodiscard]] const std::shared_ptr<lib::Panel>& panel() const noexcept {
        return panel_;
    }
    [[nodiscard]] std::string statusLine() const { return status_line_; }
    void refresh();
    void replText();
    void summary(Summary* value);
    void skin(const skin::Skin* value) noexcept {
        owned_context_.skin = value;
        if (context_) context_->skin = value;
    }
    [[nodiscard]] Summary* summary() const noexcept { return summary_; }
    void cancelEdit();

    void createArea();
    void createBattle();
    void createPackage();
    std::uint64_t createPackage(const std::shared_ptr<event::EventTree>& base_tree,
                                std::string name);
    std::uint64_t createPackage(const std::shared_ptr<event::Content>& base_start,
                                std::string name);

    void selectSummary();
    [[nodiscard]] bool summarySelected() const noexcept;
    [[nodiscard]] lib::WidgetPtr summarySelectionControl() const noexcept;
    [[nodiscard]] bool select(AbstractArea* value);
    [[nodiscard]] bool select(const AbstractAreaPtr& value) { return select(value.get()); }
    [[nodiscard]] bool canOpenAreaScene() const noexcept;
    [[nodiscard]] bool canOpenAreaEvent() const noexcept;
    void openAreaScene(bool shell_activate, bool can_duplicate = false);
    void openAreaScene(int x, int y, bool shell_activate, bool can_duplicate = false);
    void openAreaEvent(int x, int y, bool shell_activate, bool can_duplicate = false);
    void openAreaEvent(bool shell_activate, bool can_duplicate = false);
    void openAreaScene(AbstractArea* value, bool shell_activate,
                       bool can_duplicate = false);
    void openAreaEvent(AbstractArea* value, bool shell_activate,
                       bool can_duplicate = false);
    void openAreaScene(const AbstractAreaPtr& value, bool shell_activate,
                       bool can_duplicate = false) {
        openAreaScene(value.get(), shell_activate, can_duplicate);
    }
    void openAreaEvent(const AbstractAreaPtr& value, bool shell_activate,
                       bool can_duplicate = false) {
        openAreaEvent(value.get(), shell_activate, can_duplicate);
    }
    void openAreaScene(std::uint64_t id, bool shell_activate,
                       bool can_duplicate = false);
    void openAreaEvent(std::uint64_t id, bool shell_activate,
                       bool can_duplicate = false);
    void openBattleScene(std::uint64_t id, bool shell_activate,
                         bool can_duplicate = false);
    void openBattleEvent(std::uint64_t id, bool shell_activate,
                         bool can_duplicate = false);
    void openPackage(std::uint64_t id, bool shell_activate,
                     bool can_duplicate = false);

    void incSearch();
    [[nodiscard]] bool canUp() const;
    [[nodiscard]] bool canDown() const;
    void up();
    void down();
    [[nodiscard]] bool canSetStartArea() const;
    void setStartArea();

    [[nodiscard]] bool qAreasMaterialCopy(
        const xml::XNode& node, const std::vector<AbstractAreaPtr>& values);
    [[nodiscard]] bool canWriteComment() const noexcept;
    void writeComment();

    void cut(AreaTableSelectionEvent& event);
    void copy(AreaTableSelectionEvent& event);
    void paste(AreaTableSelectionEvent& event);
    void del(AreaTableSelectionEvent& event);
    void clone(AreaTableSelectionEvent& event);
    void copy();
    void paste();
    void del();

    [[nodiscard]] bool canDoTCPD() const;
    [[nodiscard]] bool canDoT() const;
    [[nodiscard]] bool canDoC() const;
    [[nodiscard]] bool canDoP() const;
    [[nodiscard]] bool canDoClone() const;
    [[nodiscard]] bool canDoD() const;
    void addAreas(const std::vector<AbstractAreaPtr>& values,
                  const std::vector<std::string>& directories = {});
    [[nodiscard]] bool canUndo() const noexcept;
    [[nodiscard]] bool canRedo() const noexcept;
    void undo();
    void redo();
    void setUndoLimit(std::size_t value);
    // Summaryの編集にも、資源コマンドと同じデータペインの履歴を使う。
    void storeSummaryEdit(std::function<void()> swap_state);
    void replaceID();
    [[nodiscard]] bool canReplaceID() const;
    void doImport();
    [[nodiscard]] bool canDoImport() const;
    [[nodiscard]] bool isSelected() const noexcept;
    void selectAll();
    [[nodiscard]] bool canSelectAll() const noexcept;
    [[nodiscard]] std::vector<std::string> openedCWXPath() const;

    void setSort(AreaTableSortColumn column, AreaTableSortDirection direction);
    [[nodiscard]] AreaTableSortColumn sortColumn() const noexcept { return sort_column_; }
    [[nodiscard]] AreaTableSortDirection sortDirection() const noexcept {
        return sort_direction_;
    }
    [[nodiscard]] const std::vector<Row>& rows() const noexcept { return rows_; }
    [[nodiscard]] const std::vector<std::size_t>& selection() const noexcept {
        return selection_;
    }
    [[nodiscard]] const std::shared_ptr<DirTree>& directoryTree() const noexcept {
        return dirs_;
    }
    [[nodiscard]] const std::string& directory() const noexcept { return directory_; }
    void setDirectory(std::string value);
    void setDirectoryMode(bool value);
    [[nodiscard]] bool directoryMode() const noexcept { return directory_mode_; }
    [[nodiscard]] std::vector<AbstractAreaPtr> selectedAreas() const;
    [[nodiscard]] std::pair<std::uint64_t, AreaTableType> getInfo(
        std::size_t index) const;
    [[nodiscard]] std::pair<std::uint64_t, AreaTableType> getSelectionInfo() const;
    [[nodiscard]] AbstractAreaPtr getSelectionArea() const;
    [[nodiscard]] int areaCount() const noexcept;
    [[nodiscard]] int battleCount() const noexcept;
    [[nodiscard]] int packageCount() const noexcept;
    [[nodiscard]] int countAllAreas() const noexcept;
    [[nodiscard]] bool showSummary() const noexcept;
    [[nodiscard]] bool disposed() const noexcept { return disposed_; }
    void dispose();

private:
    [[nodiscard]] const skin::Skin* summarySkin() const noexcept;
    void captureOpeningSkin();
    [[nodiscard]] std::vector<AbstractAreaPtr> directoryAreas() const;
    [[nodiscard]] std::vector<AbstractAreaPtr> importAreas() const;
    void refreshCommentDialog(const AbstractArea* value, bool deleted, bool titleOnly = false);
    void refreshCommentTitles();
    void applySort(AreaTableSortColumn column, AreaTableSortDirection direction, bool publish);
    lib::Action import_sort_disconnect_;
    lib::Action use_count_disconnect_;
    struct SelectionKey {
        AreaTableType type{};
        std::uint64_t id{};
    };

    struct UndoSelectionSnapshot {
        std::vector<SelectionKey> current, saved;
        std::string directory;
        std::vector<std::string> tree_paths;
        bool tree_focus{};
    };
    [[nodiscard]] UndoSelectionSnapshot captureUndoSelection();
    void restoreUndoSelection(const UndoSelectionSnapshot& value);
    bool restoring_undo_selection_{};

    // 行の表示データを準備している間に、同じSummary内で行が並
    // べ替え・編集される場合がある。 Summaryの同一性だけで
    // なく、提供元を呼ぶ前に対象モデルの状態を取得し、公開時に再検
    // 査する。 選択と表示位置の指定は、型とIDをキーに維持する。
    struct DomainEntry {
        AreaTableType type{};
        const AbstractArea* area{};
        std::uint64_t id{};
        std::string name;
        std::string directory;
    };
    struct DomainSnapshot {
        Summary* summary{};
        std::string scenario_name;
        std::uint64_t start_area{};
        std::vector<DomainEntry> entries;
    };

    class LambdaUndo;

    AreaTableContext owned_context_{};
    AreaTableContext* context_{};
    Props* prop_{};
    Summary* summary_{};
    Summary* import_target_{};
    bool read_only_{};
    // Dの読み取り専用の取り込み表は、生成時に解決したスキンを保持する。 表を開いたま
    // まMainWindowのスキンが切り替わる場合があるため、所有参照で保持する。
    std::shared_ptr<const skin::Skin> opening_skin_{};
    bool directory_mode_{};
    bool disposed_{};
    bool in_refresh_{};
    bool last_focus_tree_{};
    std::string directory_;
    std::string status_line_;
    std::shared_ptr<incsearch::IncSearch> inc_search_;
    std::string clipboard_text_;
    bool private_clone_in_progress_{};
    mutable bool last_paste_enabled_{};
    std::function<void()> refresh_flags_;
    std::unordered_map<std::string, lib::Action> menu_actions_;
    std::unordered_map<std::string, std::function<bool()>> menu_enabled_;

    std::shared_ptr<lib::Panel> panel_;
    std::shared_ptr<lib::SplitView> split_;
    std::shared_ptr<lib::TableView> table_;
    std::shared_ptr<lib::TreeView> tree_;
    std::shared_ptr<lib::Menu> menu_;
    std::shared_ptr<absdialog::AbsDialog> renumber_dialog_;
    std::shared_ptr<summarydialog::SummaryDialog> readonly_summary_dialog_;
    std::shared_ptr<int> renumber_lifetime_{std::make_shared<int>(0)};
    std::shared_ptr<DirTree> dirs_;
    std::vector<Row> rows_;
    std::vector<std::size_t> selection_;
    std::vector<SelectionKey> saved_selection_;
    bool saved_summary_selected_{};
    std::map<AbstractArea*, std::shared_ptr<comment::CommentDialog>>
        comment_dialogs_;
    // 確定前に同じSummaryを新しいAreaTable更
    // 新が再構成していたら、準備済みの表データを破棄する。
    std::uint64_t table_render_revision_{};
    std::uint64_t summary_binding_generation_{};
    AreaTableSortColumn sort_column_{AreaTableSortColumn::Id};
    AreaTableSortDirection sort_direction_{AreaTableSortDirection::Ascending};
    std::shared_ptr<undo::UndoManager> undo_;
    bool deletion_operation_active_{};

    areawindow::Signal<const Area*>::Connection area_ref_connection_{};
    areawindow::Signal<const Area*>::Connection area_del_connection_{};
    areawindow::Signal<const Battle*>::Connection battle_ref_connection_{};
    areawindow::Signal<const Battle*>::Connection battle_del_connection_{};
    areawindow::Signal<const Package*>::Connection package_ref_connection_{};
    areawindow::Signal<const Package*>::Connection package_del_connection_{};
    areawindow::Signal<>::Connection text_connection_{};
    areawindow::Signal<>::Connection undo_connection_{};

    void connectSignals();
    void disconnectSignals();
    void configureWidgets(const std::shared_ptr<lib::Container>& parent);
    void bindDragDrop();
    void bindDirectoryDragDrop();
    void configureMenu();
    void refreshRows();
    void refreshTable(const DomainSnapshot& domain);
    void refreshTree(const DomainSnapshot& domain);
    void refreshStatusLine();
    void saveSelections();
    void restoreSelections();
    void updateSelection(const std::vector<std::size_t>& value);
    void updateTreeSelection(const std::vector<lib::TreeItemId>& value);
    void beginCreatedNameEdit(AreaTableType type, std::uint64_t id);
    void beginCreatedDirEdit(std::string path);
    void editCell(std::size_t row, std::size_t column, std::string value);
    void editTreeItem(lib::TreeItemId id, std::string value);
    void selectIndex(std::size_t index, bool additive);
    void writeCommentImpl(const AbstractAreaPtr& value);
    void closeCommentDialogs();
    [[nodiscard]] std::string serializeClipboardSelection() const;
    [[nodiscard]] bool pasteClipboard();
    void addAreasAt(const std::vector<AbstractAreaPtr>& values,
                    const std::vector<std::string>& directories,
                    std::optional<std::size_t> insertion,
                    bool migrate_same_summary_references = false);
    [[nodiscard]] bool importClipboardPayload(const std::string& text,
                                               const std::string& destination,
                                               std::optional<std::size_t> insertion = std::nullopt);
    void pushUndo(std::function<void()> undo_action, std::function<void()> redo_action,
                  std::optional<UndoSelectionSnapshot> before = std::nullopt);
    void notifyChanged(AbstractArea* value, bool deleted = false);
    void notifySummaryChanged();
    void applyIdsCollisionSafe(const std::vector<AbstractAreaPtr>& values,
                               const std::vector<std::uint64_t>& ids,
                               std::size_t first);
    void renumberList(AreaTableType type, std::uint64_t requested_id,
                      std::size_t index);
    [[nodiscard]] AbstractAreaPtr movementNeighbor(int delta) const;
    void moveSelected(int delta);
    void moveSelectedByInsertion(std::ptrdiff_t insertion_index);
    void removeArea(const AbstractAreaPtr& value, bool notify = true);
    void insertArea(const AbstractAreaPtr& value, std::size_t index,
                    AreaTableType type, bool force_new_id, bool notify = true);
    [[nodiscard]] AbstractAreaPtr areaAt(std::size_t index) const;
    [[nodiscard]] AbstractAreaPtr areaForId(std::uint64_t id, AreaTableType type) const;
    [[nodiscard]] std::size_t useCount(const AbstractAreaPtr& value) const;
    [[nodiscard]] std::string rowText(const Row& row, std::size_t column) const;
    [[nodiscard]] static int typeRank(AreaTableType type) noexcept;
    [[nodiscard]] bool before(const Row& first, const Row& second) const;
    [[nodiscard]] static std::string lower(std::string_view value);
    [[nodiscard]] static int naturalCompare(std::string_view first,
                                             std::string_view second);
    [[nodiscard]] std::string uniqueName(std::string value,
                                         AreaTableType type) const;
    [[nodiscard]] std::vector<SelectionKey> currentSelectionKeys() const;
    [[nodiscard]] DomainSnapshot captureDomainSnapshot() const;
    [[nodiscard]] bool domainMatches(const DomainSnapshot& value) const;
    [[nodiscard]] bool hasSelectionType(AreaTableType type) const;
    [[nodiscard]] bool isSelected(const AbstractArea* value) const;
    void registerMenuCommand(std::string command, lib::Action action,
                             std::function<bool()> enabled = {});
    void constructDirTree(bool add);
    void putDir(const std::string& value);
    void sortDirTree(const std::shared_ptr<DirTree>& value);
    [[nodiscard]] std::shared_ptr<DirTree> findDirTree(std::string_view value) const;
    void deleteDirTree(std::string_view value);
    void callRefArea(AbstractArea* value);
    void refreshUseCount();
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::areatable
