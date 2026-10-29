#pragma once

// cwx/editor/gui/dwt/flagtable.d
// のC++20移植。 元のSWT表と三つの
// 編集画面のモデル処理はflagspane
// と共有する。 このアダプターはXMLの
// イベントツリー操作・ドラッグ対応・コメ
// ント・初期化操作を共通形式で公開し、
// ネイティブ部品のハンドルは渡さない。

#include "cwxeditor_src/core/cwx/event.hpp"
#include "cwxeditor_src/core/cwx/flag.hpp"
#include "cwxeditor_src/core/cwx/path.hpp"
#include "cwxeditor_src/core/cwx/system.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/core/cwx/usecounter.hpp"
#include "cwxeditor_src/core/cwx/versioninfo.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/flagspane.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dmenu.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/editdialogsession.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/xmlbytestransfer.hpp"
#include "cwxeditor_src/lib/clipboard.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/drag_drop.hpp"
#include "cwxeditor_src/lib/events.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::summary {
class Summary;
}

namespace cwxeditor::cwx::editor::gui::dwt::flagtable {

namespace core = ::cwxeditor::cwx;
namespace event = ::cwxeditor::cwx::event;
namespace expression = ::cwxeditor::cwx::expression;
namespace flag = ::cwxeditor::cwx::flag;
namespace path = ::cwxeditor::cwx::path;
namespace system = ::cwxeditor::cwx::system;
namespace types = ::cwxeditor::cwx::types;
namespace usecounter = ::cwxeditor::cwx::usecounter;
namespace lib = ::cwxeditor::lib;
namespace pane = ::cwxeditor::cwx::editor::gui::dwt::flagspane;
namespace splitpane = ::cwxeditor::cwx::editor::gui::dwt::splitpane;
namespace transfer = ::cwxeditor::cwx::editor::gui::dwt::xmlbytestransfer;

using String = lib::String;
using StringView = lib::StringView;
using Parent = pane::Parent;
using PanelPtr = pane::PanelPtr;
using Commons = pane::Commons;
using Props = pane::Props;
using UndoManager = pane::UndoManager;
using SelectionEvent = pane::SelectionEvent;
using FlagPtr = flag::FlagPtr;
using StepPtr = flag::StepPtr;
using VariantPtr = flag::VariantPtr;
using FlagDirPtr = flag::FlagDirPtr;
using UseCounterPtr = flag::UseCounterPtr;
using FlagList = flag::FlagList;
using StepList = flag::StepList;
using VariantList = flag::VariantList;
using VariableKind = pane::VariableKind;
using VariableObject = pane::VariableObject;
using VariableRow = pane::VariableRow;
using Signal = pane::Signal<void()>;
using CwxPathPtr = flag::CwxPathPtr;
using Summary = ::cwxeditor::cwx::summary::Summary;

inline constexpr std::array<types::VariableInitialization, 4> INIT_TIMINGS{
    types::VariableInitialization::BattleStart,
    types::VariableInitialization::BattleEnd,
    types::VariableInitialization::EventExit,
    types::VariableInitialization::Leave,
};

using CheckBoxPtr = std::shared_ptr<lib::CheckBox>;

/// 共通の部品ツリーに、初期化指定のチェック欄を一つ作る。
[[nodiscard]] CheckBoxPtr createInitTimingCheck(
    const Commons& comm, const Parent& parent,
    types::VariableInitialization timing, bool readOnly,
    std::function<void(const CheckBoxPtr&)> changed = {});

[[nodiscard]] types::VariableInitialization selectedInitialization(
    const std::vector<CheckBoxPtr>& checks, const CheckBoxPtr& none);

void setInitializationSelection(
    const std::vector<CheckBoxPtr>& checks, const CheckBoxPtr& none,
    types::VariableInitialization value);

void normalizeInitializationSelection(
    const std::vector<CheckBoxPtr>& checks, const CheckBoxPtr& none,
    const CheckBoxPtr& changed);

/// 変数の共通ダイアログはDに合わせ、編集可能ならモードレス、 読み取り専用のStep/Flag
/// はモーダルにする。Variantはモードレスのままにする。
class StepEditDialog : public editdialogsession::EditDialogSession {
public:
    using Applied = std::function<void()>;
    using Closed = std::function<void(lib::DialogResult)>;

    StepEditDialog(Commons comm, std::shared_ptr<Summary> summary,
                   std::shared_ptr<lib::Window> shell, bool readOnly,
                   bool local, FlagDirPtr dir, StepPtr step = {});
    StepEditDialog(Commons comm, Summary* summary,
                   std::shared_ptr<lib::Window> shell, bool readOnly,
                   bool local, FlagDirPtr dir, StepPtr step = {});

    ~StepEditDialog() override;

    [[nodiscard]] StepPtr step() const noexcept { return step_; }
    [[nodiscard]] const String& name() const noexcept { return name_; }
    void name(String value) { name_ = flag::FlagDir::validName(std::move(value)); }
    [[nodiscard]] const std::vector<String>& values() const noexcept {
        return values_;
    }
    void values(std::vector<String> value) {
        values_ = std::move(value);
        value_cache_ = values_;
    }
    [[nodiscard]] std::uint32_t selection() const noexcept { return selection_; }
    void selection(std::uint32_t value) noexcept { selection_ = value; }
    [[nodiscard]] types::VariableInitialization initialization() const noexcept {
        return initialization_;
    }
    void initialization(types::VariableInitialization value) noexcept {
        initialization_ = value;
    }
    [[nodiscard]] bool readOnly() const noexcept { return readOnly_; }
    [[nodiscard]] bool local() const noexcept { return local_; }
    [[nodiscard]] PanelPtr setup(const Parent& parent = {});
    void presentation(Props& value);
    void presentation(const Props& value);
    [[nodiscard]] std::shared_ptr<lib::Dialog> setupDialog(
        const std::shared_ptr<lib::Window>& owner = {});
    void show(const std::shared_ptr<lib::Window>& owner = {});
    void close(lib::DialogResult result = lib::DialogResult::Cancel);
    [[nodiscard]] const std::shared_ptr<lib::Dialog>& dialog() const noexcept {
        return dialog_;
    }
    [[nodiscard]] const std::shared_ptr<lib::Button>& okButton() const noexcept {
        return ok_button_;
    }
    [[nodiscard]] const std::shared_ptr<lib::Button>& cancelButton() const noexcept {
        return cancel_button_;
    }
    [[nodiscard]] const std::shared_ptr<lib::Button>& applyButton() const noexcept {
        return apply_button_;
    }
    [[nodiscard]] const std::shared_ptr<splitpane::SplitPane>& topSplit() const
        noexcept { return top_split_; }
    [[nodiscard]] const std::shared_ptr<lib::TableView>& valuesControl() const
        noexcept { return values_control_; }
    [[nodiscard]] const std::shared_ptr<lib::NumberEdit>& countControl() const
        noexcept { return count_control_; }
    [[nodiscard]] const std::shared_ptr<Summary>& summary() const noexcept {
        return summary_;
    }
    [[nodiscard]] bool apply();
    [[nodiscard]] lib::Window* editDialogWindow() const noexcept override {
        return dialog_.get();
    }
    [[nodiscard]] bool editDialogPending() const noexcept override {
        return draft_dirty_;
    }
    [[nodiscard]] bool applyEditDialog() override { return apply(); }
    [[nodiscard]] bool canMoveValuesUp() const noexcept;
    [[nodiscard]] bool canMoveValuesDown() const noexcept;
    [[nodiscard]] bool canCreateFollowingValues() const noexcept;
    void moveValuesUp();
    void moveValuesDown();
    void createFollowingValues();
    [[nodiscard]] bool canUndo() const noexcept { return !undo_values_.empty(); }
    [[nodiscard]] bool canRedo() const noexcept { return !redo_values_.empty(); }
    [[nodiscard]] const std::shared_ptr<lib::Menu>& valueContextMenu() const
        noexcept { return value_menu_; }
    void undo();
    void redo();
    void onApplying(Applied callback) { applying_ = std::move(callback); }
    void onApplied(Applied callback) { applied_ = std::move(callback); }
    void onClosed(Closed callback) { closed_ = std::move(callback); }

private:
    struct ValueSnapshot {
        std::vector<String> values;
        std::vector<std::size_t> row_selection;
    };

    [[nodiscard]] ValueSnapshot valueSnapshot() const;
    void restoreValueSnapshot(ValueSnapshot value);
    void storeValueUndo();
    void refValueUndoMax();
    void syncValueCache();
    void refreshValueControls(
        std::optional<std::vector<std::size_t>> rowSelection = {});
    void refreshValueActionButtons();
    bool finishValueEdit();
    void restartValueEdit(std::size_t index);
    void refreshCompatibility();
    void refreshWarning();
    std::shared_ptr<lib::Label> warning_control_{};
    void markModified();

    Commons comm_{};
    std::shared_ptr<Summary> summary_{};
    std::shared_ptr<lib::Window> shell_{};
    bool readOnly_{};
    bool local_{};
    FlagDirPtr dir_{};
    StepPtr step_{};
    String name_{};
    std::vector<String> values_{};
    // D版の_valueCacheと同じく、項目数を一時的に減らしても隠れた値を
    // 保持し、再び増やした際に復元する。
    std::vector<String> value_cache_{};
    std::uint32_t selection_{};
    types::VariableInitialization initialization_{
        types::VariableInitialization::Leave};
    bool expand_special_characters_{};
    CheckBoxPtr expand_control_{};
    std::vector<CheckBoxPtr> initialization_checks_{};
    CheckBoxPtr initialization_none_{};
    Props props_{};
    Props* props_source_{};
    PanelPtr component_{};
    std::shared_ptr<splitpane::SplitPane> top_split_{};
    std::shared_ptr<lib::TextEdit> name_control_{};
    std::shared_ptr<lib::ComboBox> initial_control_{};
    std::shared_ptr<lib::NumberEdit> count_control_{};
    std::shared_ptr<lib::TableView> values_control_{};
    std::weak_ptr<lib::TextEdit> value_editor_{};
    std::optional<std::size_t> preview_hover_row_{};
    std::size_t value_editor_row_{};
    std::uint64_t value_editor_revision_{};
    std::shared_ptr<lib::Menu> value_menu_{};
    std::shared_ptr<int> undo_lifetime_{std::make_shared<int>(0)};
    std::size_t undo_max_token_{};
    std::vector<ValueSnapshot> undo_values_{};
    std::vector<ValueSnapshot> redo_values_{};
    std::shared_ptr<lib::Dialog> dialog_{};
    std::shared_ptr<lib::Button> ok_button_{};
    std::shared_ptr<lib::Button> cancel_button_{};
    std::shared_ptr<lib::Button> apply_button_{};
    bool draft_dirty_{};
    bool was_attached_{};
    FlagDirPtr live_root_;
    bool deleted_{};
    std::shared_ptr<int> delete_lifetime_{std::make_shared<int>(0)};
    lib::Action stop_watching_deletion_{};
    std::size_t version_token_{};
    void watchDeletion();
    void refreshSpecialPreviews();
    String specialPreview(String text) const;
    std::size_t preview_token_{};
    std::size_t preview_delete_token_{};
    std::size_t preview_refresh_token_{};
    std::vector<dmenu::TextMenuModifyPtr> special_text_menus_{};
    Applied applying_{};
    Applied applied_{};
    Closed closed_{};
};

class FlagEditDialog : public editdialogsession::EditDialogSession {
public:
    using Applied = std::function<void()>;
    using Closed = std::function<void(lib::DialogResult)>;

    FlagEditDialog(Commons comm, std::shared_ptr<Summary> summary,
                   std::shared_ptr<lib::Window> shell, bool readOnly,
                   bool local, FlagDirPtr dir, FlagPtr value = {});
    FlagEditDialog(Commons comm, Summary* summary,
                   std::shared_ptr<lib::Window> shell, bool readOnly,
                   bool local, FlagDirPtr dir, FlagPtr value = {});

    ~FlagEditDialog() override;

    [[nodiscard]] FlagPtr flag() const noexcept { return flag_; }
    [[nodiscard]] const String& name() const noexcept { return name_; }
    void name(String value) { name_ = flag::FlagDir::validName(std::move(value)); }
    [[nodiscard]] const String& on() const noexcept { return on_; }
    void on(String value) { on_ = std::move(value); }
    [[nodiscard]] const String& off() const noexcept { return off_; }
    void off(String value) { off_ = std::move(value); }
    [[nodiscard]] bool onOff() const noexcept { return onOff_; }
    void onOff(bool value) noexcept { onOff_ = value; }
    [[nodiscard]] types::VariableInitialization initialization() const noexcept {
        return initialization_;
    }
    void initialization(types::VariableInitialization value) noexcept {
        initialization_ = value;
    }
    [[nodiscard]] bool readOnly() const noexcept { return readOnly_; }
    [[nodiscard]] bool local() const noexcept { return local_; }
    [[nodiscard]] PanelPtr setup(const Parent& parent = {});
    void presentation(Props& value);
    void presentation(const Props& value);
    [[nodiscard]] std::shared_ptr<lib::Dialog> setupDialog(
        const std::shared_ptr<lib::Window>& owner = {});
    void show(const std::shared_ptr<lib::Window>& owner = {});
    void close(lib::DialogResult result = lib::DialogResult::Cancel);
    [[nodiscard]] const std::shared_ptr<lib::Dialog>& dialog() const noexcept {
        return dialog_;
    }
    [[nodiscard]] const std::shared_ptr<lib::Button>& okButton() const noexcept {
        return ok_button_;
    }
    [[nodiscard]] const std::shared_ptr<lib::Button>& cancelButton() const noexcept {
        return cancel_button_;
    }
    [[nodiscard]] const std::shared_ptr<lib::Button>& applyButton() const noexcept {
        return apply_button_;
    }
    [[nodiscard]] const std::shared_ptr<splitpane::SplitPane>& topSplit() const
        noexcept { return top_split_; }
    [[nodiscard]] const std::shared_ptr<Summary>& summary() const noexcept {
        return summary_;
    }
    [[nodiscard]] bool apply();
    [[nodiscard]] lib::Window* editDialogWindow() const noexcept override {
        return dialog_.get();
    }
    [[nodiscard]] bool editDialogPending() const noexcept override {
        return draft_dirty_;
    }
    [[nodiscard]] bool applyEditDialog() override { return apply(); }
    void onApplying(Applied callback) { applying_ = std::move(callback); }
    void onApplied(Applied callback) { applied_ = std::move(callback); }
    void onClosed(Closed callback) { closed_ = std::move(callback); }

private:
    Commons comm_{};
    std::shared_ptr<Summary> summary_{};
    std::shared_ptr<lib::Window> shell_{};
    bool readOnly_{};
    bool local_{};
    FlagDirPtr dir_{};
    FlagPtr flag_{};
    String name_{};
    String on_{};
    String off_{};
    bool onOff_{};
    types::VariableInitialization initialization_{
        types::VariableInitialization::Leave};
    bool expand_special_characters_{};
    CheckBoxPtr expand_control_{};
    std::vector<CheckBoxPtr> initialization_checks_{};
    CheckBoxPtr initialization_none_{};
    Props props_{};
    Props* props_source_{};
    PanelPtr component_{};
    std::shared_ptr<splitpane::SplitPane> top_split_{};
    std::shared_ptr<lib::TextEdit> name_control_{};
    std::shared_ptr<lib::ComboBox> initial_control_{};
    std::shared_ptr<lib::ComboBox> on_control_{};
    std::shared_ptr<lib::ComboBox> off_control_{};
    std::shared_ptr<lib::Dialog> dialog_{};
    std::shared_ptr<lib::Button> ok_button_{};
    std::shared_ptr<lib::Button> cancel_button_{};
    std::shared_ptr<lib::Button> apply_button_{};
    bool draft_dirty_{};
    bool was_attached_{};
    FlagDirPtr live_root_;
    bool deleted_{};
    std::shared_ptr<int> delete_lifetime_{std::make_shared<int>(0)};
    lib::Action stop_watching_deletion_{};
    std::size_t version_token_{};
    void watchDeletion();
    void refreshSpecialPreviews();
    String specialPreview(String text) const;
    std::size_t preview_token_{};
    std::size_t preview_delete_token_{};
    std::size_t preview_refresh_token_{};
    std::vector<dmenu::TextMenuModifyPtr> special_text_menus_{};
    void refreshCompatibility();
    void refreshWarning();
    std::shared_ptr<lib::Label> warning_control_{};
    void markModified();
    Applied applying_{};
    Applied applied_{};
    Closed closed_{};
};

class VariantEditDialog : public editdialogsession::EditDialogSession {
public:
    using Applied = std::function<void()>;
    using Closed = std::function<void(lib::DialogResult)>;

    VariantEditDialog(Commons comm, std::shared_ptr<Summary> summary,
                      std::shared_ptr<lib::Window> shell, bool readOnly,
                      bool local, FlagDirPtr dir, VariantPtr value = {});
    VariantEditDialog(Commons comm, Summary* summary,
                      std::shared_ptr<lib::Window> shell, bool readOnly,
                      bool local, FlagDirPtr dir, VariantPtr value = {});

    ~VariantEditDialog() override;

    [[nodiscard]] VariantPtr variant() const noexcept { return variant_; }
    [[nodiscard]] const String& name() const noexcept { return name_; }
    void name(String value) { name_ = flag::FlagDir::validName(std::move(value)); }
    [[nodiscard]] types::VariantType type() const noexcept;
    [[nodiscard]] expression::VariantVal value() const;
    void value(expression::VariantVal value);
    [[nodiscard]] types::VariableInitialization initialization() const noexcept {
        return initialization_;
    }
    void initialization(types::VariableInitialization value) noexcept {
        initialization_ = value;
    }
    [[nodiscard]] bool readOnly() const noexcept { return readOnly_; }
    [[nodiscard]] bool local() const noexcept { return local_; }
    [[nodiscard]] PanelPtr setup(const Parent& parent = {});
    void presentation(Props& value);
    void presentation(const Props& value);
    [[nodiscard]] std::shared_ptr<lib::Dialog> setupDialog(
        const std::shared_ptr<lib::Window>& owner = {});
    void show(const std::shared_ptr<lib::Window>& owner = {});
    void close(lib::DialogResult result = lib::DialogResult::Cancel);
    [[nodiscard]] const std::shared_ptr<lib::Dialog>& dialog() const noexcept {
        return dialog_;
    }
    [[nodiscard]] const std::shared_ptr<lib::Button>& okButton() const noexcept {
        return ok_button_;
    }
    [[nodiscard]] const std::shared_ptr<lib::Button>& cancelButton() const noexcept {
        return cancel_button_;
    }
    [[nodiscard]] const std::shared_ptr<lib::Button>& applyButton() const noexcept {
        return apply_button_;
    }
    [[nodiscard]] const std::vector<String>& warnings() const noexcept {
        return warnings_;
    }
    [[nodiscard]] const std::shared_ptr<lib::TextEdit>& numberControl() const noexcept {
        return number_control_;
    }
    [[nodiscard]] const std::shared_ptr<Summary>& summary() const noexcept {
        return summary_;
    }
    [[nodiscard]] bool apply();
    [[nodiscard]] lib::Window* editDialogWindow() const noexcept override {
        return dialog_.get();
    }
    [[nodiscard]] bool editDialogPending() const noexcept override {
        return draft_dirty_;
    }
    [[nodiscard]] bool applyEditDialog() override { return apply(); }
    void onApplying(Applied callback) { applying_ = std::move(callback); }
    void onApplied(Applied callback) { applied_ = std::move(callback); }
    void onClosed(Closed callback) { closed_ = std::move(callback); }

private:
    Commons comm_{};
    std::shared_ptr<Summary> summary_{};
    std::shared_ptr<lib::Window> shell_{};
    bool readOnly_{};
    bool local_{};
    FlagDirPtr dir_{};
    VariantPtr variant_{};
    String name_{};
    expression::VariantVal value_{};
    types::VariableInitialization initialization_{
        types::VariableInitialization::Leave};
    std::vector<CheckBoxPtr> initialization_checks_{};
    CheckBoxPtr initialization_none_{};
    Props props_{};
    Props* props_source_{};
    PanelPtr component_{};
    std::shared_ptr<lib::TextEdit> name_control_{};
    std::shared_ptr<lib::RadioButton> number_radio_{};
    std::shared_ptr<lib::RadioButton> string_radio_{};
    std::shared_ptr<lib::RadioButton> boolean_radio_{};
    std::shared_ptr<lib::TextEdit> number_control_{};
    std::shared_ptr<lib::TextEdit> string_control_{};
    std::shared_ptr<lib::ComboBox> boolean_control_{};
    std::shared_ptr<lib::Label> warning_control_{};
    std::vector<String> warnings_{};
    std::shared_ptr<lib::Dialog> dialog_{};
    std::shared_ptr<lib::Button> ok_button_{};
    std::shared_ptr<lib::Button> cancel_button_{};
    std::shared_ptr<lib::Button> apply_button_{};
    bool draft_dirty_{};
    bool was_attached_{};
    FlagDirPtr live_root_;
    bool deleted_{};
    std::shared_ptr<int> delete_lifetime_{std::make_shared<int>(0)};
    lib::Action stop_watching_deletion_{};
    std::size_t version_token_{};
    void watchDeletion();
    [[nodiscard]] bool validDraft() const;
    void markModified();
    void refreshWarning();
    Applied applying_{};
    Applied applied_{};
    Closed closed_{};
};

/// 移植した補助Undo記録が使う、共通の取り消し操作の基底。
class FTVUndo {
public:
    virtual ~FTVUndo() = default;
    virtual void undo() = 0;
    virtual void redo() = 0;
    virtual void dispose() noexcept {}
};

/// 具体的な表。モデルはペイン側が渡し、 このクラス
/// はflagtable.d固有のAPIを追加する。
class FlagTable final {
public:
    static constexpr std::ptrdiff_t NAME = 0;
    static constexpr std::ptrdiff_t VALUE = 1;
    static constexpr std::ptrdiff_t INIT = 2;
    static constexpr std::ptrdiff_t UC = 3;

    Signal createEvent;
    Signal deleteEvent;

    FlagTable(Commons comm, Props prop, UndoManager& undo,
              bool local = false, bool readOnly = false);
    ~FlagTable();

    FlagTable(const FlagTable&) = delete;
    FlagTable& operator=(const FlagTable&) = delete;
    FlagTable(FlagTable&&) = delete;
    FlagTable& operator=(FlagTable&&) = delete;

    [[nodiscard]] PanelPtr createControl(const Parent& parent,
                                         const Parent& incSearchParent = {},
                                         lib::Action gotFocus = {});
    void updateIncSearchParent(const Parent& parent);
    [[nodiscard]] PanelPtr widget() const noexcept;
    [[nodiscard]] const std::shared_ptr<lib::TableView>& table() const noexcept;

    [[nodiscard]] std::unordered_map<std::ptrdiff_t, FlagPtr> dragFlags() const;
    [[nodiscard]] std::unordered_map<std::ptrdiff_t, StepPtr> dragSteps() const;
    [[nodiscard]] std::unordered_map<std::ptrdiff_t, VariantPtr> dragVariants() const;
    [[nodiscard]] const String& statusLine() const noexcept;

    void refresh();
    void refresh(const std::vector<VariableObject>& selected);
    [[nodiscard]] const std::vector<std::size_t>& selected() const noexcept;
    [[nodiscard]] std::vector<std::size_t> selectedItems(
        FlagList& flags, StepList& steps, VariantList& variants) const;
    [[nodiscard]] FlagList selectionFlags() const;
    [[nodiscard]] StepList selectionSteps() const;
    [[nodiscard]] VariantList selectionVariants() const;
    [[nodiscard]] std::vector<std::ptrdiff_t> selectionFlagIndices() const;
    [[nodiscard]] std::vector<std::ptrdiff_t> selectionStepIndices() const;
    [[nodiscard]] std::vector<std::ptrdiff_t> selectionVariantIndices() const;

    void createFlag();
    void createStep();
    void createVariant();
    void edit();
    [[nodiscard]] bool canEdit() const noexcept;
    void edit(const FlagPtr& value);
    void edit(const StepPtr& value);
    void edit(const VariantPtr& value);
    void setDir(const FlagDirPtr& dir, bool forceRefresh = false);
    [[nodiscard]] FlagDirPtr dir() const noexcept;
    void select(const FlagPtr& value, bool deselect = false);
    void select(const StepPtr& value, bool deselect = false);
    void select(const VariantPtr& value, bool deselect = false);
    void deselectAll();
    void selectAll();

    [[nodiscard]] bool canCopyVariablePath() const noexcept;
    void copyVariablePath();
    void dispose() noexcept;
    void useCounter(const UseCounterPtr& value);
    [[nodiscard]] UseCounterPtr useCounter() const noexcept;

    void cut(SelectionEvent& event);
    void copy(SelectionEvent& event);
    void paste(SelectionEvent& event);
    void del(SelectionEvent& event);
    void clone(SelectionEvent& event);
    [[nodiscard]] bool canDoTCPD() const noexcept;
    [[nodiscard]] bool canDoT() const noexcept;
    [[nodiscard]] bool canDoC() const noexcept;
    [[nodiscard]] bool canDoP() const noexcept;
    [[nodiscard]] bool canDoD() const noexcept;
    [[nodiscard]] bool canDoClone() const noexcept;

    void undo();
    void redo();
    void cancelEdit() noexcept;
    void enterEdit() noexcept;
    void replaceID();
    [[nodiscard]] bool canReplaceID() const noexcept;
    [[nodiscard]] bool openCWXPath(String path, bool shellActivate);
    [[nodiscard]] std::vector<String> openedCWXPath() const;

    [[nodiscard]] bool canWriteComment() const noexcept;
    void setCommentText(String value) { commentText_ = std::move(value); }
    [[nodiscard]] const String& commentText() const noexcept { return commentText_; }
    void writeComment();

    void copyFlagTree(bool onOff);
    void copyStepTree(std::uint32_t value);
    void copyInitTree();
    void copyFlagReverseTree();
    void copyStepUpTree();
    void copyStepDownTree();

    void setSearchText(String value);
    [[nodiscard]] const String& searchText() const noexcept;
    [[nodiscard]] const std::vector<VariableRow>& rows() const noexcept;
    [[nodiscard]] const VariableObject& editing() const noexcept;
    void flagsSelected();
    void incSearch();
    void selectFlagIndices(const std::vector<std::ptrdiff_t>& indices);
    void selectStepIndices(const std::vector<std::ptrdiff_t>& indices);
    void selectVariantIndices(const std::vector<std::ptrdiff_t>& indices);

    [[nodiscard]] bool renameSelected(String value);
    [[nodiscard]] bool setFlagState(const FlagPtr& value, bool on);
    [[nodiscard]] bool setStepSelection(const StepPtr& value,
                                        std::uint32_t selection);
    [[nodiscard]] bool setVariantValue(const VariantPtr& value,
                                       const expression::VariantVal& valueData);

    [[nodiscard]] static std::ptrdiff_t indexOf(const FlagDirPtr& dir,
                                                const CwxPathPtr& value);
    [[nodiscard]] static CwxPathPtr fromIndex(const FlagDirPtr& dir,
                                              std::ptrdiff_t index);
    [[nodiscard]] static std::ptrdiff_t toFlagIndex(const FlagDirPtr& dir,
                                                    std::ptrdiff_t index);
    [[nodiscard]] static std::ptrdiff_t toStepIndex(const FlagDirPtr& dir,
                                                    std::ptrdiff_t index);
    [[nodiscard]] static std::ptrdiff_t toVariantIndex(const FlagDirPtr& dir,
                                                       std::ptrdiff_t index);
    [[nodiscard]] static std::vector<types::VariableInitialization>
    selectableInitialization(bool local);

private:
    void writeClipboard(const transfer::ClipboardData& data);
    [[nodiscard]] transfer::ClipboardData readClipboard() const;
    void writeXML(StringView xml);
    [[nodiscard]] bool readXML(String& xml) const;
    [[nodiscard]] String selectedXML() const;
    [[nodiscard]] String eventTreeXML(event::CType flagType,
                                      event::CType stepType,
                                      event::CType variantType,
                                      bool flagValue,
                                      std::uint32_t stepValue,
                                      bool useValues) const;
    [[nodiscard]] bool pasteXML(StringView xml);
    void notifyCreate();
    void notifyDelete();
    void notifyRefresh();
    void applyComment(const String& value);
    void selectIndices(const std::vector<std::ptrdiff_t>& indices,
                       VariableKind kind);
    void syncDragSelection() const;

    Commons comm_{};
    Props prop_{};
    UndoManager* undo_{};
    pane::FlagTable impl_;
    mutable std::unordered_map<std::ptrdiff_t, FlagPtr> dragFlags_{};
    mutable std::unordered_map<std::ptrdiff_t, StepPtr> dragSteps_{};
    mutable std::unordered_map<std::ptrdiff_t, VariantPtr> dragVariants_{};
    std::optional<transfer::ClipboardData> fallbackClipboard_{};
    String commentText_{};
    std::size_t createToken_{};
    std::size_t deleteToken_{};
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::flagtable
