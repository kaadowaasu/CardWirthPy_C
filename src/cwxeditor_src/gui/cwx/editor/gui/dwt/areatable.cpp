#include "cwxeditor_src/gui/cwx/editor/gui/dwt/summarydialog.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/xmlbytestransfer.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/incsearch.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dutils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/areatable.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/commons.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/scene_flag_compare.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/core/cwx/warning.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/comment.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/smalldialogs.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"
#include "cwxeditor_src/lib/ui_table_selection.hpp"

#include "cwxeditor_src/gui/cwx/editor/gui/dwt/resourcehistory.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace cwxeditor::cwx::editor::gui::dwt::areatable {

namespace {

struct ResourceIDs {
    std::vector<AbstractAreaPtr> areas, battles, packages;
    std::vector<std::uint64_t> area_ids, battle_ids, package_ids;
    std::shared_ptr<Area> start;
    explicit ResourceIDs(Summary* summary) {
        const auto capture = [](const auto& source, auto& values, auto& ids) {
            for (const auto& value : source) { values.push_back(value); ids.push_back(value->id()); }
        };
        capture(summary->areas(), areas, area_ids);
        capture(summary->battles(), battles, battle_ids);
        capture(summary->packages(), packages, package_ids);
        start = summary->area(summary->startArea());
    }
};

template <typename T>
std::shared_ptr<T> makeComponent(const lib::Services& services,
                                 lib::ComponentKind kind) {
    if (services.gui) {
        if (auto value = std::dynamic_pointer_cast<T>(
                services.gui->create_component(kind))) {
            return value;
        }
    }
    return std::make_shared<T>();
}

std::string trim(std::string_view value) {
    std::size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) {
        ++first;
    }
    std::size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1U]))) {
        --last;
    }
    return std::string(value.substr(first, last - first));
}

std::string escapeField(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const char character : value) {
        if (character == '\\' || character == '|') result.push_back('\\');
        if (character == '\n') {
            result += "\\n";
        } else if (character == '\r') {
            result += "\\r";
        } else {
            result.push_back(character);
        }
    }
    return result;
}

std::string unescapeField(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    bool escaped = false;
    for (const char character : value) {
        if (escaped) {
            result.push_back(character == 'n' ? '\n' : character == 'r' ? '\r' : character);
            escaped = false;
        } else if (character == '\\') {
            escaped = true;
        } else {
            result.push_back(character);
        }
    }
    if (escaped) result.push_back('\\');
    return result;
}

std::vector<std::string> splitFields(std::string_view line) {
    std::vector<std::string> result;
    std::string field;
    bool escaped = false;
    for (const char character : line) {
        if (escaped) {
            field.push_back('\\');
            field.push_back(character);
            escaped = false;
        } else if (character == '\\') {
            escaped = true;
        } else if (character == '|') {
            result.push_back(unescapeField(field));
            field.clear();
        } else {
            field.push_back(character);
        }
    }
    result.push_back(unescapeField(field));
    return result;
}

std::uint64_t parseId(std::string_view value) {
    try {
        std::size_t consumed = 0;
        const auto result = std::stoull(std::string(value), &consumed, 10);
        return consumed == value.size() ? result : 0U;
    } catch (...) {
        return 0U;
    }
}

bool isAreaTableClipboardPayload(std::string_view text) noexcept {
    if (text.empty()) return false;
    try {
        const auto root = xml::XNode::parse(std::string(text));
        if (root.name() == Area::XML_NAME || root.name() == Battle::XML_NAME ||
            root.name() == Package::XML_NAME) return true;
        if (root.name() != "Table" &&
            (root.name() != "CWXAreaClipboard" ||
             root.attr<std::string>("format", false, std::string{}) != "2")) {
            return false;
        }
        return std::ranges::any_of(root.element_children(), [](const xml::XNode& child) {
            return child.name() == Area::XML_NAME || child.name() == Battle::XML_NAME ||
                   child.name() == Package::XML_NAME ||
                   (child.name() == "TablePath" && !child.value().empty());
        });
    } catch (...) {
        return false;
    }
}

struct AreaTableClipboardFallback {
    std::weak_ptr<lib::ClipboardBackend> backend;
    std::string text;
};

std::mutex area_table_clipboard_fallback_mutex;
std::vector<AreaTableClipboardFallback> area_table_clipboard_fallbacks;

void setAreaTableClipboardFallback(
    const std::shared_ptr<lib::ClipboardBackend>& backend,
    const std::string_view text) {
    if (!backend) return;
    std::scoped_lock lock(area_table_clipboard_fallback_mutex);
    auto& fallbacks = area_table_clipboard_fallbacks;
    fallbacks.erase(
        std::remove_if(fallbacks.begin(), fallbacks.end(),
                       [&backend](const AreaTableClipboardFallback& entry) {
                           const auto owner = entry.backend.lock();
                           return !owner || owner.get() == backend.get();
                       }),
        fallbacks.end());
    if (!text.empty()) fallbacks.push_back({backend, std::string{text}});
}

std::string areaTableClipboardFallback(
    const std::shared_ptr<lib::ClipboardBackend>& backend) {
    if (!backend) return {};
    std::scoped_lock lock(area_table_clipboard_fallback_mutex);
    for (auto it = area_table_clipboard_fallbacks.rbegin();
         it != area_table_clipboard_fallbacks.rend(); ++it) {
        const auto owner = it->backend.lock();
        if (owner && owner.get() == backend.get()) return it->text;
    }
    return {};
}

std::string typeName(AreaTableType type) {
    switch (type) {
    case AreaTableType::Summary: return "Summary";
    case AreaTableType::Area: return "Area";
    case AreaTableType::Battle: return "Battle";
    case AreaTableType::Package: return "Package";
    }
    return {};
}

template <typename Use>
std::string uniqueDirName(std::string base, Use&& use) {
    if (use(base)) return base;
    std::size_t first_digit = base.size();
    while (first_digit > 0U &&
           std::isdigit(static_cast<unsigned char>(base[first_digit - 1U])))
        --first_digit;
    std::string left;
    std::string right;
    std::uint64_t number = 2U;
    std::size_t width{};
    if (first_digit != base.size()) {
        const auto digits = std::string_view(base).substr(first_digit);
        try {
            number = std::stoull(std::string(digits));
            width = digits.size();
            left = base.substr(0U, first_digit);
        } catch (...) {
            left = base + " (";
            right = ")";
            number = 2U;
        }
    } else {
        left = base + " (";
        right = ")";
    }
    for (;;) {
        std::ostringstream candidate;
        candidate << left;
        if (width) candidate << std::setw(static_cast<int>(width)) << std::setfill('0');
        candidate << number << right;
        if (use(candidate.str())) return candidate.str();
        if (number == std::numeric_limits<std::uint64_t>::max()) return base;
        ++number;
    }
}

}  // namespace

class AreaTable::LambdaUndo final : public undo::Undo {
public:
    LambdaUndo(std::function<void()> undo_action, std::function<void()> redo_action)
        : undo_action_(std::move(undo_action)), redo_action_(std::move(redo_action)) {}

    void undo() override { const auto action=undo_action_; if (action) action(); }
    void redo() override { const auto action=redo_action_; if (action) action(); }
    void dispose() override {
        undo_action_ = {};
        redo_action_ = {};
    }

private:
    std::function<void()> undo_action_;
    std::function<void()> redo_action_;
};

AreaTable::DirTree::DirTree(DirTree* parent_value, std::string name_value,
                            std::vector<std::shared_ptr<DirTree>> children)
    : parent(parent_value), name(std::move(name_value)),
      subDirs(std::move(children)) {
    for (const auto& child : subDirs) if (child) child->parent = this;
}

std::string AreaTable::DirTree::path() const {
    if (!parent || parent == this) return name;
    const auto prefix = parent->path();
    return prefix.empty() ? name : prefix + '\\' + name;
}

std::shared_ptr<AreaTable::DirTree> AreaTable::DirTree::dup(
    DirTree* new_parent) const {
    auto result = std::make_shared<DirTree>(new_parent, name);
    result->subDirs.reserve(subDirs.size());
    for (const auto& child : subDirs) {
        if (child) result->subDirs.push_back(child->dup(result.get()));
    }
    return result;
}

AreaTable::AreaTable(AreaTableContext& context, Props& prop, const bool read_only,
                     Summary* import_target)
    : context_(&context), prop_(&prop), import_target_(import_target),
      read_only_(read_only),
      // Dはこの設定を、編集用と取り込み元の両方の表へ適用する。
      directory_mode_(prop.var().showAreaDirTree.value),
      undo_(std::make_shared<undo::UndoManager>(static_cast<std::size_t>(
          std::max<std::int32_t>(0, prop.var().undoMaxMainView.value)))) {
    const auto column = read_only ? prop.var().importAreasSortColumn.value
                                  : prop.var().areasSortColumn.value;
    const auto direction = read_only ? prop.var().importAreasSortDirection.value
                                     : prop.var().areasSortDirection.value;
    sort_column_ = column == 1 ? AreaTableSortColumn::Name
                               : column == 2 ? AreaTableSortColumn::UseCount
                                             : AreaTableSortColumn::Id;
    sort_direction_ = direction == 2 ? AreaTableSortDirection::Descending
                                     : AreaTableSortDirection::Ascending;
    captureOpeningSkin();
    connectSignals();
}

AreaTable::AreaTable(areawindow::AreaWindowContext& context, Props& prop,
                     const bool read_only, Summary* import_target)
    : owned_context_(context), context_(&owned_context_), prop_(&prop),
      import_target_(import_target), read_only_(read_only),
      // Dはこの設定を、編集用と取り込み元の両方の表へ適用する。
      directory_mode_(prop.var().showAreaDirTree.value),
      undo_(std::make_shared<undo::UndoManager>(static_cast<std::size_t>(
          std::max<std::int32_t>(0, prop.var().undoMaxMainView.value)))) {
    const auto column = read_only ? prop.var().importAreasSortColumn.value
                                  : prop.var().areasSortColumn.value;
    const auto direction = read_only ? prop.var().importAreasSortDirection.value
                                     : prop.var().areasSortDirection.value;
    sort_column_ = column == 1 ? AreaTableSortColumn::Name
                               : column == 2 ? AreaTableSortColumn::UseCount
                                             : AreaTableSortColumn::Id;
    sort_direction_ = direction == 2 ? AreaTableSortDirection::Descending
                                     : AreaTableSortDirection::Ascending;
    captureOpeningSkin();
    connectSignals();
}

const skin::Skin* AreaTable::summarySkin() const noexcept {
    if (read_only_ && opening_skin_) return opening_skin_.get();
    return context_ ? context_->skin : nullptr;
}

void AreaTable::captureOpeningSkin() {
    if (!read_only_ || !import_target_ || !context_ || !context_->editor_commons ||
        context_->editor_commons_lifetime.expired()) return;
    // Dはこの読み取り専用の制御部を開くときに、取り込み先のSkinを解決する。
    opening_skin_ = ::cwxeditor::cwx::editor::gui::dwt::commons::findSkin(
        *context_->editor_commons, *prop_, import_target_);
}

AreaTable::~AreaTable() { dispose(); }

void AreaTable::connectSignals() {
    if (context_) {
        const std::weak_ptr<int> life = renumber_lifetime_;
        if (read_only_ && context_->callbacks.subscribe_import_sort)
            import_sort_disconnect_ = context_->callbacks.subscribe_import_sort(
                [this, life](int column, int direction) {
                    if (life.expired() || column < 0 || column > 2 ||
                        (direction != 1 && direction != 2)) return;
                    applySort(static_cast<AreaTableSortColumn>(column),
                        direction == 2 ? AreaTableSortDirection::Descending
                                       : AreaTableSortDirection::Ascending, false);
                });
        if (!read_only_ && context_->callbacks.subscribe_use_count)
            use_count_disconnect_ = context_->callbacks.subscribe_use_count([this, life] {
                if (!life.expired()) refreshUseCount();
            });
    }
    if (!context_ || !context_->area_window) return;
    auto& signals = context_->area_window->signals;
    area_ref_connection_ = signals.ref_area.connect(
        [this, life = std::weak_ptr<int>(renumber_lifetime_)](const Area* value) {
            if (life.expired() || !value) return;
            refreshCommentDialog(value, false);
            if (!life.expired()) refresh();
        });
    area_del_connection_ = signals.del_area.connect(
        [this, life = std::weak_ptr<int>(renumber_lifetime_)](const Area* value) {
            if (life.expired() || !value) return;
            refreshCommentDialog(value, true);
            if (!life.expired()) refresh();
        });
    battle_ref_connection_ = signals.ref_battle.connect(
        [this, life = std::weak_ptr<int>(renumber_lifetime_)](const Battle* value) {
            if (life.expired() || !value) return;
            refreshCommentDialog(value, false);
            if (!life.expired()) refresh();
        });
    battle_del_connection_ = signals.del_battle.connect(
        [this, life = std::weak_ptr<int>(renumber_lifetime_)](const Battle* value) {
            if (life.expired() || !value) return;
            refreshCommentDialog(value, true);
            if (!life.expired()) refresh();
        });
    package_ref_connection_ = signals.ref_package.connect(
        [this, life = std::weak_ptr<int>(renumber_lifetime_)](const Package* value) {
            if (life.expired() || !value) return;
            refreshCommentDialog(value, false);
            if (!life.expired()) refresh();
        });
    package_del_connection_ = signals.del_package.connect(
        [this, life = std::weak_ptr<int>(renumber_lifetime_)](const Package* value) {
            if (life.expired() || !value) return;
            refreshCommentDialog(value, true);
            if (!life.expired()) refresh();
        });
    text_connection_ = signals.repl_text.connect([this, life = std::weak_ptr<int>(renumber_lifetime_)] {
        if (life.expired()) return;
        replText();
    });
    undo_connection_ = signals.ref_undo_max.connect([this] {
        if (!disposed_ && undo_) {
            undo_->max(static_cast<std::size_t>(std::max<std::int32_t>(
                0, prop_->var().undoMaxMainView.value)));
        }
    });
}

void AreaTable::disconnectSignals() {
    auto sort_disconnect = std::move(import_sort_disconnect_);
    auto count_disconnect = std::move(use_count_disconnect_);
    if (sort_disconnect) sort_disconnect();
    if (count_disconnect) count_disconnect();
    if (!context_ || !context_->area_window) return;
    auto& signals = context_->area_window->signals;
    signals.ref_area.disconnect(area_ref_connection_);
    signals.del_area.disconnect(area_del_connection_);
    signals.ref_battle.disconnect(battle_ref_connection_);
    signals.del_battle.disconnect(battle_del_connection_);
    signals.ref_package.disconnect(package_ref_connection_);
    signals.del_package.disconnect(package_del_connection_);
    signals.repl_text.disconnect(text_connection_);
    signals.ref_undo_max.disconnect(undo_connection_);
    area_ref_connection_ = area_del_connection_ = 0U;
    battle_ref_connection_ = battle_del_connection_ = 0U;
    package_ref_connection_ = package_del_connection_ = 0U;
    text_connection_ = undo_connection_ = 0U;
}

void AreaTable::construct(const std::shared_ptr<lib::Container>& parent,
                          std::function<void()> refresh_flags) {
    if (disposed_) return;
    refresh_flags_ = std::move(refresh_flags);
    configureWidgets(parent);
    configureMenu();
    refresh();
}

void AreaTable::configureWidgets(const std::shared_ptr<lib::Container>& parent) {
    if (!panel_) panel_ = makeComponent<lib::Panel>(context_->services,
                                                     lib::ComponentKind::Panel);
    if (!table_) table_ = makeComponent<lib::TableView>(context_->services,
                                                        lib::ComponentKind::TableView);
    auto panel_layout = std::make_shared<lib::LinearLayout>(lib::Orientation::Vertical);
    panel_layout->spacing = 0.0F;
    panel_->set_layout(std::move(panel_layout));

    // Dは編集用のエリア表をSWT.SINGLEで作り、読み
    // 取り専用の取り込み表だけにSWT.MULTIを使う。
    table_->multi_select = read_only_;
    table_->full_row_selection = true;
    // DのTableTextEditはmouseDoubleClickで保留中の編集を取
    // り消す。ダブルクリックはエリアを開く操作に属し、編集は別のジェスチャーとして扱う。
    table_->edit_on_double_click = false;
    table_->inline_edit_column = 1U;
    const bool all_mode = lib::editor_window_host_options().all_mode;
    const auto id_column_width = read_only_ ? prop_->var().importAreaIdColumn.value
                                            : prop_->var().areaIdColumn.value;
    const auto name_column_width = read_only_ ? prop_->var().importAreaNameColumn.value
                                              : prop_->var().areaNameColumn.value;
    const auto count_column_width = read_only_ ? prop_->var().importAreaCountColumn.value
                                               : prop_->var().areaCountColumn.value;
    table_->columns = {
        {prop_->requiredMsgs().areaId.value,
         all_mode ? 50.0F : static_cast<float>(std::max<std::int32_t>(1, id_column_width)),
         lib::TextAlignment::Start, true, true},
        {prop_->requiredMsgs().areaName.value,
         all_mode ? 162.0F : static_cast<float>(std::max<std::int32_t>(1, name_column_width)),
         lib::TextAlignment::Start, true, true},
        {all_mode ? "利用" : prop_->requiredMsgs().areaCount.value,
         // ALLでは使用数の列を22px狭くし、名前の列を12px広げる。
         all_mode ? 28.0F : static_cast<float>(std::max<std::int32_t>(1, count_column_width)),
         lib::TextAlignment::Start, true, true},
    };
    // DのsaveColumnWidthは、その後のSWTの列幅変更も
    // すべて保存する。ALLは上記で初期表示幅を変えるが、列幅変更は
    // 同じ保存経路を共有し、読み取り専用かどうかでプロパティを選ぶ。
    const std::weak_ptr<int> life = renumber_lifetime_;
    table_->on_column_resized = [this, life](const std::size_t index,
                                             const float width) {
        if (life.expired() || disposed_ || !prop_ || !table_ ||
            index >= table_->columns.size()) return;
        auto& column = table_->columns[index];
        column.width = width;
        if (read_only_) {
            if (index == 0U) dutils::saveColumnWidth(
                prop_, column, prop_->var().importAreaIdColumn.value);
            else if (index == 1U) dutils::saveColumnWidth(
                prop_, column, prop_->var().importAreaNameColumn.value);
            else if (index == 2U) dutils::saveColumnWidth(
                prop_, column, prop_->var().importAreaCountColumn.value);
        } else {
            if (index == 0U) dutils::saveColumnWidth(
                prop_, column, prop_->var().areaIdColumn.value);
            else if (index == 1U) dutils::saveColumnWidth(
                prop_, column, prop_->var().areaNameColumn.value);
            else if (index == 2U) dutils::saveColumnWidth(
                prop_, column, prop_->var().areaCountColumn.value);
        }
    };
    table_->on_selection_changed = [this](const std::vector<std::size_t>& value) {
        updateSelection(value);
    };
    table_->highlighted_column = static_cast<std::size_t>(sort_column_);
    // DのTableSorter.doSort()に合わせ、別の列をク
    // リックしたら昇順にし、現在昇順の列をクリックしたら逆順にする。
    table_->on_sort_requested = [this](const std::size_t index) {
        const auto column = index == 1U ? AreaTableSortColumn::Name
                          : index == 2U ? AreaTableSortColumn::UseCount
                                        : AreaTableSortColumn::Id;
        const auto direction =
            sort_column_ == column && sort_direction_ == AreaTableSortDirection::Ascending
                ? AreaTableSortDirection::Descending
                : AreaTableSortDirection::Ascending;
        setSort(column, direction);
    };
    table_->on_cell_activate_with_modifiers = [this](std::size_t row, std::size_t,
                                                     lib::Modifiers modifiers) {
        if (row >= rows_.size()) return;
        if (rows_[row].type == AreaTableType::Summary) {
            editSummary();
            return;
        }
        if (!rows_[row].area) return;

        bool open_event =
            (static_cast<std::uint32_t>(modifiers) &
             static_cast<std::uint32_t>(lib::Modifiers::Shift)) != 0U;
        if (prop_->var().clickIsOpenEvent.value) {
            open_event = !open_event;
        }
        // DはEnter・ダブルクリックで現在の選択全体を
        // 開く。入力配送側が操作行を現在の行に設定済み。
        if (open_event) openAreaEvent(true, false);
        else openAreaScene(true, false);
    };
    table_->on_cell_edited = [this](std::size_t row, std::size_t column,
                                    std::string value) {
        editCell(row, column, std::move(value));
    };
    table_->on_pointer = [this](lib::PointerEvent& event) {
        // DのSWT MouseEvent.button==2
        // は右ボタンではなく中央ボタン。Secondaryと同
        // 一視すると、通常のコンテキストクリックでメニューを
        // 出す前に反対側の場面・イベントタブが開いてしまう。
        if (event.action != lib::PointerAction::Up ||
            (event.button != lib::PointerButton::Middle &&
             event.button != lib::PointerButton::Secondary)) return;
        const bool middle_open = event.button == lib::PointerButton::Middle;
        if (event.item_hit_test_available && event.item_index >= 0 &&
            static_cast<std::size_t>(event.item_index) < rows_.size()) {
            const auto row = static_cast<std::size_t>(event.item_index);
            if (!middle_open) {
                selectIndex(row, false);
            } else if (rows_[row].type == AreaTableType::Summary) {
                editSummary();
            } else if (rows_[row].area) {
                selectIndex(row, false);
                if (prop_->var().clickIsOpenEvent.value) {
                    openAreaScene(rows_[row].area.get(), true, false);
                } else {
                    openAreaEvent(rows_[row].area.get(), true, false);
                }
            }
        } else if (middle_open && !selection_.empty()) {
            // Dは空白部分では選択済みAbstractAreaを使う。選択された
            // Summaryはヒット行ではなく、シナリオ情報を開いてはならない。
            if (prop_->var().clickIsOpenEvent.value) openAreaScene(true, false);
            else openAreaEvent(true, false);
        }
        if (!middle_open && menu_ && context_ && context_->services.gui) {
            if (menu_->on_open) menu_->on_open();
            context_->services.gui->show_context_menu(menu_, table_, event.position);
        }
        event.handled = true;
    };
    table_->on_focus = [this] { last_focus_tree_ = false; };
    if (!read_only_) {
        // DのTableTextEditは、選択済み行を押すと名前を編集する。
        ::cwxeditor::cwx::editor::gui::dwt::dutils::installClickSelectedEdit(table_, context_->services, panel_,
            [this] {
                return prop_ && prop_->var().editTriggerType.value ==
                    static_cast<std::int32_t>(::cwxeditor::cwx::structs::EditTrigger::Quick);
            });
    }

    if (directory_mode_) {
        if (!split_) split_ = makeComponent<lib::SplitView>(
            context_->services, lib::ComponentKind::SplitView);
        if (!tree_) tree_ = makeComponent<lib::TreeView>(context_->services,
                                                          lib::ComponentKind::TreeView);
        // ディレクトリのルートは構造上のもので、ユーザーが隠せるフ
        // ォルダーではない。Treeの端にそろえ、展開ボタンを付け
        // ないことで、SWTの最上位ディレクトリ項目にも合わせる。
        tree_->flush_root_left = true;
        split_->orientation = (read_only_ ? prop_->var().importAreaSashV.value
                                          : prop_->var().areaSashV.value)
                                  ? lib::Orientation::Vertical
                                  : lib::Orientation::Horizontal;
        // Dは読み取り専用の外部シナリオ表に、別の分割位置を保
        // 持する。ここでEditorのareaSashL/Rを
        // 使うと、上下分割の取り込みTableタブにメインウ
        // ィンドウの左右比率が適用され、ディレクトリツリーや
        // ID・名前・使用数の表が細い帯へ潰れる場合がある。
        const auto left_weight = read_only_ ? prop_->var().importAreaSashL.value
                                            : prop_->var().areaSashL.value;
        const auto right_weight = read_only_ ? prop_->var().importAreaSashR.value
                                             : prop_->var().areaSashR.value;
        split_->weights = {
            static_cast<float>(std::max<std::int32_t>(1, left_weight)),
            static_cast<float>(std::max<std::int32_t>(1, right_weight)),
        };
        split_->on_weights_changed = [this](const std::vector<float>& weights) {
            if (!prop_ || weights.size() < 2U) return;
            const auto first = static_cast<std::int32_t>(
                std::max(1.0F, std::round(weights[0])));
            const auto second = static_cast<std::int32_t>(
                std::max(1.0F, std::round(weights[1])));
            if (read_only_) {
                prop_->var().importAreaSashL.value = first;
                prop_->var().importAreaSashR.value = second;
            } else {
                prop_->var().areaSashL.value = first;
                prop_->var().areaSashR.value = second;
            }
        };
        tree_->on_selection_changed = [this](const std::vector<lib::TreeItemId>& value) {
            updateTreeSelection(value);
        };
        tree_->on_item_edited = [this](const lib::TreeItemId id, std::string value) {
            editTreeItem(id, std::move(value));
        };
        tree_->on_pointer = [this](lib::PointerEvent& event) {
            if (event.button != lib::PointerButton::Secondary ||
                event.action != lib::PointerAction::Up) return;
            // Dはディレクトリツリーとエリア表に同じポップアップメニューを
            // 付ける。ツリーのヒットは行番号ではなく安定したTreeItemId
            // を返すため、先に選択してメニューを指したフォルダーへ向
            // ける。ツリーのコンテキストクリックでは場面を開かない。
            if (event.item_hit_test_available && event.item_index >= 0) {
                const auto hit = static_cast<lib::TreeItemId>(event.item_index);
                const auto found = std::find_if(
                    tree_->nodes.begin(), tree_->nodes.end(),
                    [hit](const lib::TreeNode& node) { return node.id == hit; });
                if (found != tree_->nodes.end())
                    (void)lib::select_tree_items(*tree_, {hit});
            }
            if (menu_ && context_ && context_->services.gui) {
                if (menu_->on_open) menu_->on_open();
                context_->services.gui->show_context_menu(menu_, tree_, event.position);
            }
            event.handled = true;
        };
        tree_->on_focus = [this] { last_focus_tree_ = true; };
        if (!read_only_) {
            // DのTreeEdit（TextEditMFListener）は、選択済み
            // フォルダーを押すと名前を変更する。シナリオのルートフォルダーは変更不可。
            ::cwxeditor::cwx::editor::gui::dwt::dutils::installClickSelectedEdit(tree_, context_->services, panel_,
                [this] {
                    return prop_ && prop_->var().editTriggerType.value ==
                        static_cast<std::int32_t>(::cwxeditor::cwx::structs::EditTrigger::Quick);
                },
                [this](const lib::TreeItemId id) {
                    const auto node = std::find_if(tree_->nodes.begin(), tree_->nodes.end(),
                        [id](const lib::TreeNode& value) { return value.id == id; });
                    if (node == tree_->nodes.end()) return false;
                    if (node->value == "__summary__") return summary_ != nullptr;
                    const auto dir = findDirTree(node->value);
                    return dir && dir->parent;
                });
        }
        tree_->on_activate = [this](const lib::TreeItemId id) {
            const auto node = std::find_if(
                tree_->nodes.begin(), tree_->nodes.end(),
                [id](const lib::TreeNode& value) { return value.id == id; });
            if (node != tree_->nodes.end() && node->value == "__summary__") {
                editSummary();
            }
        };
        const auto previous_tree_key = tree_->on_key_down;
        tree_->on_key_down = [this, previous_tree_key,
                             life = std::weak_ptr<int>(renumber_lifetime_)](lib::KeyEvent& event) {
            if (life.expired()) return;
            if (previous_tree_key) previous_tree_key(event);
            if (life.expired() || event.handled) return;
            // DのTreeKListenerはAltに関係なくEnterを受け付ける
            // 。汎用の項目表示ハンドラーは、Altを他のコントロール用に確保する。
            if (event.key == lib::Key::Enter &&
                (static_cast<std::uint32_t>(event.modifiers) &
                 static_cast<std::uint32_t>(lib::Modifiers::Alt)) != 0U &&
                summarySelected()) {
                event.handled = true;
                editSummary();
            }
        };
        if (lib::editor_window_host_options().all_mode) {
            // ALLでは、フォルダー名や深さに応じてツリーの最
            // 小幅を増やさない。フォルダー作成でペインの最小
            // サイズが変わらないよう、固定の幅ヒントを使う。
            auto tree_item = tree_->layout_item();
            tree_item.preferred.width = 84.0F;
            tree_->set_layout_item(tree_item);
        }
        split_->clear();
        split_->add(tree_);
        split_->add(table_);
        split_->set_layout_item(
            {0U, 0U, 1U, 1U, lib::Align::Stretch, lib::Align::Stretch,
             {0.0F, 40.0F}, {}, {}, 1.0F});
        panel_->clear();
        panel_->add(split_);
    } else {
        table_->set_layout_item(
            {0U, 0U, 1U, 1U, lib::Align::Stretch, lib::Align::Stretch,
             {0.0F, 40.0F}, {}, {}, 1.0F});
        panel_->clear();
        panel_->add(table_);
    }
    if (parent) parent->add(panel_);
    const std::weak_ptr<int> search_life = renumber_lifetime_;
    std::vector<incsearch::AdditionMatcher> matchers;
    for (const auto& entry : std::vector<std::pair<std::string, AreaTableType>>{
             {prop_->requiredMsgs().area.value, AreaTableType::Area},
             {prop_->requiredMsgs().battle.value, AreaTableType::Battle},
             {prop_->requiredMsgs().cwPackage.value, AreaTableType::Package}})
        matchers.push_back({entry.first, [type = entry.second](const std::any& data) {
            const auto value = std::any_cast<AreaTableType>(&data);
            return value && *value == type;
        }});
    incsearch::IncSearchSettings settings;
    settings.incrementalSearchType = prop_->var().incrementalSearchType.value;
    settings.incrementalSearchBoxWidth = prop_->var().incrementalSearchBoxWidth.value;
    settings.startIncrementalSearchWhenKeyDown = prop_->var().startIncrementalSearchWhenKeyDown.value;
    settings.startIncrementalSearchWhenKeyDownProvider = [this, search_life] {
        return !search_life.expired() && prop_ &&
            prop_->var().startIncrementalSearchWhenKeyDown.value;
    };
    // Dの有効な登録範囲に合わせる。SashはディレクトリTreeとTable
    // を含み、表だけのモードはTableだけを登録する。ツールバーの文字入力で
    // 誤って検索を始めないため。Sashオブジェクトは向きを変えても維持する。
    auto* const search_root = directory_mode_ && split_
        ? static_cast<lib::Widget*>(split_.get())
        : static_cast<lib::Widget*>(table_.get());
    inc_search_ = std::make_shared<incsearch::IncSearch>(context_->services, search_root,
        [this, search_life] { return !search_life.expired() && !disposed_ &&
            summary_ && (areaCount() || battleCount() || packageCount()); },
        std::move(matchers), settings);
    inc_search_->modEvent.push_back([this, search_life] {
        if (!search_life.expired() && !disposed_) refresh();
    });
    inc_search_->searchTypeChanged = [this, search_life](incsearch::SearchType type) {
        if (!search_life.expired() && prop_)
            prop_->var().incrementalSearchType.value = static_cast<std::int32_t>(type);
    };
    table_->on_key_down = [this, search_life](lib::KeyEvent& event) {
        if (search_life.expired() || disposed_) return;
        const auto modifiers = static_cast<std::uint32_t>(event.modifiers);
        if (event.key == lib::Key::Enter) {
            if (!selection_.empty() && selection_.front() < rows_.size()) {
                const auto row = selection_.front();
                if (rows_[row].type == AreaTableType::Summary) {
                    editSummary();
                } else if (rows_[row].area) {
                    bool open_event =
                        (modifiers & static_cast<std::uint32_t>(lib::Modifiers::Shift)) != 0U;
                    if (prop_->var().clickIsOpenEvent.value) open_event = !open_event;
                    if (open_event) openAreaEvent(true, false);
                    else openAreaScene(true, false);
                } else {
                    event.handled = true;
                    return;
                }
            }
            // Dは表のカーソルだけが残る場合も、AreaTable
            // のEnterを処理する。ここで消費し、選択解除後に汎
            // 用の表の実行処理がカーソル行を開かないようにする。
            event.handled = true;
            return;
        }
        if (event.key == lib::Key::I && event.modifiers == lib::Modifiers::Control) {
            incSearch(); event.handled = true; return;
        }
        if (inc_search_ && inc_search_->handleHostKeyDown(event, table_.get()))
            event.handled = true;
    };
    table_->on_text_input = [this, search_life](lib::TextInputEvent& event) {
        if (!search_life.expired() && !disposed_ && inc_search_ &&
            inc_search_->handleHostTextInput(event, table_.get())) event.handled = true;
    };
    bindDragDrop();
}

void AreaTable::incSearch() {
    const auto search = inc_search_;
    const auto table = table_;
    const auto summary = summary_;
    const auto binding = summary_binding_generation_;
    const std::weak_ptr<int> life = renumber_lifetime_;
    if (!search || !table || disposed_) return;

    // DのAreaTable::incSearchは、Shellをアクティブに
    // して一覧へ強制フォーカスしてから逐次検索を開く。ホストの復元がUIコード
    // へ再入し得るため、フォーカス操作の間はServicesの値を保持する。
    const auto services = context_ ? context_->services : lib::Services{};
    const auto gui = services.gui;
    if (services.gui) dutils::forceFocus(table, true, services);

    if (life.expired()) return;
    if (disposed_ || table_ != table || inc_search_ != search || summary_ != summary ||
        summary_binding_generation_ != binding ||
        (context_ && context_->services.gui != gui))
        return;
    // 共通の登録ルートはコンテナーだが、Dは検索前にTableへ強制フォ
    // ーカスする。Tableの編集トランザクションの所有元を維持する。
    if (search->parent() != table.get()) search->preserveInlineEditFocus(table);
    search->startIncSearch();
}

void AreaTable::bindDragDrop() {
    if (!table_ || !context_ || !context_->services.drag_drop) return;

    const auto pane_id = std::string("area-table:") +
        std::to_string(reinterpret_cast<std::uintptr_t>(this));
    lib::DragSourceBinding source;
    source.source = table_;
    source.allowed = lib::DropOperation::Copy | lib::DropOperation::Move;
    source.provide_data = [this] {
        lib::ClipboardData data;
        // Dは複数選択の取り込み一覧でも、最初の選択行を出力する。
        if (!prop_ || !summary_ || selection_.empty() ||
            (lib::editor_window_host_options().all_mode && selection_.size() != 1U) ||
            !getSelectionArea())
            return data;
        card::XMLOption option(prop_->sys(), summary_->dataVersion());
        auto root = getSelectionArea()->toNode(option);
        root.newAttr("scenarioPath", utils::nabs(std::string_view{summary_->scenarioPath()}));
        root.newAttr("scenarioName", summary_->scenarioName());
        root.newAttr("scenarioAuthor", summary_->author());
        data.text = root.text();
        data.formats.emplace("application/x-cwxeditor-area-table",
                             std::vector<std::byte>{});
        return data;
    };
    source.provide_origin = [this, pane_id] {
        lib::DragOrigin origin;
        origin.pane_id = pane_id;
        if (selection_.size() == 1U && selection_.front() < rows_.size()) {
            origin.cursor_index = selection_.front();
            const auto& row = rows_[selection_.front()];
            origin.selected_keys.push_back(std::to_string(static_cast<unsigned>(row.type)) +
                                           ":" + std::to_string(row.id));
        }
        origin.selected_keys.push_back("scenario:" + std::to_string(reinterpret_cast<std::uintptr_t>(summary_)));
        return origin;
    };
    source.provide_visual = [this]() -> std::optional<lib::DragVisual> {
        if (!table_ || selection_.size() != 1U || selection_.front() >= rows_.size())
            return std::nullopt;
        const auto bounds = table_->bounds();
        const float row_height = table_->row_height > 0.0F ? table_->row_height : 20.0F;
        const float header = table_->header_height > 0.0F ? table_->header_height : row_height;
        return lib::DragVisual{{bounds.x, bounds.y + header +
            row_height * static_cast<float>(selection_.front()),
            bounds.width, row_height}};
    };
    source.completed = [weak = std::weak_ptr<lib::TableView>(table_)](lib::DropOperation) {
        if (const auto table = weak.lock()) table->set_cursor(lib::CursorKind::Default);
    };
    context_->services.drag_drop->bind_source(std::move(source));

    lib::DropTargetBinding target;
    target.target = table_;
    target.accepted_formats = {"application/x-cwxeditor-area-table"};
    target.pane_id = pane_id;
    target.accepts_target_index = [this](const std::optional<std::size_t> index) {
        return !read_only_ && (!index || *index <= rows_.size());
    };
    const auto feedback = [this](lib::DragEvent& event) {
        // Dはフォルダーモードで現在のディレクトリだけを数える。非表示の兄弟
        // フォルダーによって、表示中の未絞り込みの表を不完全と判定しない。
        const auto expected = static_cast<std::size_t>(areaCount() + battleCount() + packageCount()) +
                              ((!directory_mode_ && showSummary()) ? 1U : 0U);
        const bool complete = !read_only_ && rows_.size() == expected;
        const auto operation = event.same_pane() ? lib::DropOperation::Move : lib::DropOperation::Copy;
        const bool allowed = (static_cast<std::uint32_t>(event.allowed) &
                              static_cast<std::uint32_t>(operation)) != 0U;
        event.selected = complete && allowed ? operation : lib::DropOperation::None;
        event.handled = true;
        if (table_) table_->set_cursor(event.selected != lib::DropOperation::None
                                          ? lib::CursorKind::Move
                                          : lib::CursorKind::Forbidden);
    };
    target.on_enter = feedback;
    target.on_over = feedback;
    target.on_leave = [weak = std::weak_ptr<lib::TableView>(table_)] {
        if (const auto table = weak.lock()) table->set_cursor(lib::CursorKind::Default);
    };
    target.on_drop = [this, feedback](lib::DragEvent& event) {
        feedback(event);
        if (table_) table_->set_cursor(lib::CursorKind::Default);
        if (event.selected == lib::DropOperation::None || !summary_ || !prop_ ||
            (event.target_index && *event.target_index > rows_.size())) {
            event.handled = false;
            return;
        }
        xml::XNode node;
        AreaTableType source_type;
        try {
            node = xml::XNode::parse(event.data.text);
            if (node.name() == Area::XML_NAME) source_type = AreaTableType::Area;
            else if (node.name() == Battle::XML_NAME) source_type = AreaTableType::Battle;
            else if (node.name() == Package::XML_NAME) source_type = AreaTableType::Package;
            else { event.handled = false; return; }
        } catch (...) { event.handled = false; return; }
        const auto* destination = event.target_index && *event.target_index < rows_.size()
            ? &rows_[*event.target_index] : nullptr;
        const auto boundary = [&](const auto& values) -> std::ptrdiff_t {
            // DのDropArea.getIndexに合わせる。前の種別なら先頭、後の
            // 種別や空白なら末尾、同じ種別ならそのオブジェクトのモデル上の境界を使う。
            if (!destination) return static_cast<std::ptrdiff_t>(values.size());
            if (destination->type == AreaTableType::Summary || destination->type < source_type) return 0;
            if (destination->type > source_type) return static_cast<std::ptrdiff_t>(values.size());
            const auto found = std::find_if(values.begin(), values.end(), [&](const auto& value) {
                return value.get() == destination->area.get();
            });
            return found == values.end() ? -1 : std::distance(values.begin(), found);
        };
        const auto to = source_type == AreaTableType::Area ? boundary(summary_->areas())
            : source_type == AreaTableType::Battle ? boundary(summary_->battles())
            : boundary(summary_->packages());
        if (to < 0) { event.handled = false; return; }
        // Dは正規化パスで同じシナリオを識別する。ローカルの識
        // 別情報では、パスのない未保存ドキュメントも区別する。
        const auto source_path = node.attr("scenarioPath", false);
        const bool same_scenario = std::find(event.origin.selected_keys.begin(), event.origin.selected_keys.end(),
                "scenario:" + std::to_string(reinterpret_cast<std::uintptr_t>(summary_))) != event.origin.selected_keys.end() ||
            (!source_path.empty() && !summary_->scenarioPath().empty() &&
             utils::cfnmatch(utils::nabs(std::string_view{summary_->scenarioPath()}), source_path));
        if (same_scenario) {
            if (sort_column_ != AreaTableSortColumn::Id) { event.handled = false; return; }
            // Dはここで受信側Tableの選択行を読む。同じ保存パスでもImportSource
            // ペインからのドロップでは選択が異なるため、この書き込み可能
            // なSummary内でドラッグしたXMLのIdを解決する。
            std::uint64_t id{};
            try { id = node.child("Property", true).child("Id", true).valueTo<std::uint64_t>(); }
            catch (...) { event.handled = false; return; }
            const auto source = areaForId(id, source_type);
            const auto owner = summary_;
            const auto binding = summary_binding_generation_;
            const std::weak_ptr<int> life = renumber_lifetime_;
            if (!source || !select(source)) { event.handled = false; return; }
            if (life.expired() || summary_ != owner || summary_binding_generation_ != binding) {
                event.handled = false;
                return;
            }
            moveSelectedByInsertion(to);
            event.handled = true;
        } else {
            event.handled = importClipboardPayload(event.data.text, directory_,
                                                    static_cast<std::size_t>(to));
        }
    };
    context_->services.drag_drop->bind_target(std::move(target));
    bindDirectoryDragDrop();
}

void AreaTable::bindDirectoryDragDrop() {
    if (!tree_ || !context_ || !context_->services.drag_drop) return;
    const auto pane = std::string("area-directory:") + std::to_string(reinterpret_cast<std::uintptr_t>(this));
    const auto targetPath = [this](std::optional<std::size_t> id) -> std::optional<std::string> {
        if (!id || !tree_ || read_only_) return std::nullopt;
        const auto found = std::find_if(tree_->nodes.begin(), tree_->nodes.end(),
            [&](const auto& node) { return node.id == static_cast<lib::TreeItemId>(*id); });
        if (found == tree_->nodes.end() || found->value == "__summary__" || !findDirTree(found->value))
            return std::nullopt;
        return found->value;
    };
    lib::DragSourceBinding source;
    source.source = tree_;
    source.allowed = lib::DropOperation::Copy | lib::DropOperation::Move;
    source.provide_data = [this] {
        lib::ClipboardData data;
        if (!summary_ || !last_focus_tree_ || !tree_ || tree_->selection.empty()) return data;
        const auto selected = std::find_if(tree_->nodes.begin(), tree_->nodes.end(),
            [this](const auto& value) { return value.id == tree_->selection.front(); });
        if (selected == tree_->nodes.end() || selected->value == "__summary__") return data;
        const auto dir = findDirTree(selected->value);
        if (!dir || !dir->parent) return data;
        auto root = xml::XNode::create("TablePath", std::string_view{dir->path()});
        root.newAttr("scenarioPath", utils::nabs(std::string_view{summary_->scenarioPath()}));
        root.newAttr("scenarioName", summary_->scenarioName());
        root.newAttr("scenarioAuthor", summary_->author());
        data.text = root.text();
        data.formats.emplace("application/x-cwxeditor-scene-directory", std::vector<std::byte>{});
        return data;
    };
    source.provide_origin = [this, pane] {
        lib::DragOrigin origin;
        origin.pane_id = pane;
        origin.selected_keys = {directory_};
        origin.selected_keys.push_back("scenario:" + std::to_string(reinterpret_cast<std::uintptr_t>(summary_)));
        return origin;
    };
    context_->services.drag_drop->bind_source(std::move(source));
    lib::DropTargetBinding target;
    target.target = tree_;
    target.pane_id = pane;
    target.accepted_formats = {"application/x-cwxeditor-scene-directory", "application/x-cwxeditor-area-table"};
    target.accepts_target_index = [targetPath](std::optional<std::size_t> id) { return targetPath(id).has_value(); };
    const auto feedback = [targetPath](lib::DragEvent& event) {
        const auto allowed = static_cast<std::uint32_t>(event.allowed);
        event.selected = !targetPath(event.target_index) ? lib::DropOperation::None
            : (allowed & static_cast<std::uint32_t>(lib::DropOperation::Move)) ? lib::DropOperation::Move
            : (allowed & static_cast<std::uint32_t>(lib::DropOperation::Copy)) ? lib::DropOperation::Copy
            : lib::DropOperation::None;
        event.handled = event.selected != lib::DropOperation::None;
    };
    target.on_enter = feedback;
    target.on_over = feedback;
    target.on_drop = [this, targetPath, feedback](lib::DragEvent& event) {
        feedback(event);
        const auto destination = targetPath(event.target_index);
        event.handled = false;
        if (!destination || !summary_ || event.selected == lib::DropOperation::None) return;
        xml::XNode node;
        try { node = xml::XNode::parse(event.data.text); } catch (...) { return; }
        const auto owner = summary_;
        const auto hold = owner->weak_from_this().lock();
        const auto binding = summary_binding_generation_;
        const std::weak_ptr<int> life = renumber_lifetime_;
        const auto current = [this, life, owner, binding] {
            return !life.expired() && summary_ == owner && summary_binding_generation_ == binding;
        };
        const auto sourcePath = node.attr("scenarioPath", false);
        const bool same = std::find(event.origin.selected_keys.begin(), event.origin.selected_keys.end(),
                "scenario:" + std::to_string(reinterpret_cast<std::uintptr_t>(owner))) != event.origin.selected_keys.end() ||
            (!sourcePath.empty() && !owner->scenarioPath().empty() &&
             utils::cfnmatch(utils::nabs(std::string_view{owner->scenarioPath()}), sourcePath));
        if (!same) {
            // 別シナリオのTablePathは拒否する。別シナリオの資源行は、元を変
            // 更せず、ポインター下のディレクトリへ通常の素材コピー・取り込みを行う。
            if (node.name() != Area::XML_NAME && node.name() != Battle::XML_NAME && node.name() != Package::XML_NAME) return;
            event.handled = importClipboardPayload(event.data.text, *destination);
            return;
        }
        struct Change { std::uint64_t id; AreaTableType type; std::string before, after; };
        std::vector<Change> changes;
        const auto beforeTree = dirs_ ? dirs_->dup() : std::shared_ptr<DirTree>{};
        auto afterTree = beforeTree ? beforeTree->dup() : std::shared_ptr<DirTree>{};
        if (!afterTree) return;
        if (node.name() == "TablePath") {
            const auto from = node.value();
            const auto old = findDirTree(from);
            const auto to = findDirTree(*destination);
            const auto prefix = lower(from + '\\');
            if (from.empty() || !old || !old->parent || !to || lower(*destination) == lower(from) ||
                lower(*destination).starts_with(prefix)) return;
            for (const auto& child : to->subDirs)
                if (child && lower(child->name) == lower(old->name)) return;
            const auto newRoot = destination->empty() ? old->name : *destination + '\\' + old->name;
            const auto collect = [&](const auto& values, AreaTableType type) {
                for (const auto& value : values) {
                    if (!value) continue;
                    const auto name = value->name();
                    if (lower(name).starts_with(prefix))
                        changes.push_back({value->id(), type, name, newRoot + '\\' + name.substr(from.size() + 1U)});
                    else if (lower(value->dirName()) == lower(from))
                        changes.push_back({value->id(), type, name, destination->empty() ? value->baseName() : *destination + '\\' + value->baseName()});
                }
            };
            collect(owner->areas(), AreaTableType::Area);
            collect(owner->battles(), AreaTableType::Battle);
            collect(owner->packages(), AreaTableType::Package);
            const auto find = [&](const auto& self, const std::shared_ptr<DirTree>& root, const std::string& path) -> std::shared_ptr<DirTree> {
                if (lower(root->path()) == lower(path)) return root;
                for (const auto& child : root->subDirs) if (child) if (auto result = self(self, child, path)) return result;
                return {};
            };
            const auto moved = find(find, afterTree, from);
            const auto parent = find(find, afterTree, *destination);
            if (!moved || !moved->parent || !parent) return;
            std::erase(moved->parent->subDirs, moved);
            moved->parent = parent.get();
            parent->subDirs.push_back(moved);
        } else {
            // DのDropDirも、出力先AreaTableのgetSelectionArea()
            // を呼ぶ。別のImportSource表から同じパスのXML行が来た
            // 場合は、書き込み可能な対象SummaryでデータのIdを解決する。
            const auto type = node.name() == Area::XML_NAME ? AreaTableType::Area
                : node.name() == Battle::XML_NAME ? AreaTableType::Battle
                : node.name() == Package::XML_NAME ? AreaTableType::Package : AreaTableType::Summary;
            if (type == AreaTableType::Summary) return;
            std::uint64_t id{};
            try { id = node.child("Property", true).child("Id", true).valueTo<std::uint64_t>(); } catch (...) { return; }
            const auto value = areaForId(id, type);
            if (!value || lower(value->dirName()) == lower(*destination)) return;
            changes.push_back({id, type, value->name(), destination->empty() ? value->baseName() : *destination + '\\' + value->baseName()});
        }
        const auto selection = captureUndoSelection();
        const auto apply = [this, current, changes, beforeTree, afterTree](bool after) {
            if (!current()) return;
            std::vector<AbstractAreaPtr> values;
            for (const auto& change : changes) {
                const auto value = areaForId(change.id, change.type);
                if (!value) return;
                values.push_back(value);
            }
            for (std::size_t i = 0; i < changes.size(); ++i) {
                values[i]->name(after ? changes[i].after : changes[i].before);
                if (!current()) return;
            }
            dirs_ = (after ? afterTree : beforeTree)->dup();
            for (const auto& value : values) { callRefArea(value.get()); if (!current()) return; }
            refresh();
        };
        pushUndo([apply] { apply(false); }, [apply] { apply(true); }, selection);
        if (!current()) return;
        apply(true);
        event.handled = true;
    };
    context_->services.drag_drop->bind_target(std::move(target));
}

void AreaTable::configureMenu() {
    if (!menu_) menu_ = makeComponent<lib::Menu>(context_->services,
                                                  lib::ComponentKind::ContextMenu);
    menu_->items.clear();
    menu_actions_.clear();
    menu_enabled_.clear();
    menu_->on_command = [this](std::string_view command) {
        const auto key = std::string(command);
        if (const auto found = menu_actions_.find(key); found != menu_actions_.end()) {
            const auto enabled = menu_enabled_.find(key);
            if (enabled == menu_enabled_.end() || !enabled->second || enabled->second())
                if (found->second) found->second();
            return;
        }
        AreaTableSelectionEvent event{std::string(command), false};
        if (command == "copy") copy(event);
        else if (command == "cut") cut(event);
        else if (command == "paste") paste(event);
        else if (command == "delete") del(event);
        else if (command == "clone") clone(event);
        else if (command == "undo") undo();
        else if (command == "redo") redo();
        else if (command == "select-all") selectAll();
        else if (command == "new-area") createArea();
        else if (command == "new-battle") createBattle();
        else if (command == "new-package") createPackage();
        else if (command == "new-directory") createDir();
        else if (command == "up") up();
        else if (command == "down") down();
        else if (command == "start-area") setStartArea();
        else if (command == "comment") writeComment();
        else if (command == "edit-scene") openAreaScene(true);
        else if (command == "edit-event") openAreaEvent(true);
        else if (command == "edit-summary") editSummary();
        else if (command == "find-id") replaceID();
        else if (command == "renumber") reNumbering();
        else if (command == "inc-search") {
            incSearch();
        }
    };
    const auto text = [this](const types::MenuID id, const lib::StringView fallback) {
        if (context_ && context_->menu_text) {
            if (const auto value = context_->menu_text(id); !value.empty()) return value;
        }
        return lib::String(fallback);
    };
    const auto command = [this, &text](lib::String key, const types::MenuID id, lib::Action action,
                                std::function<bool()> enabled) {
        lib::MenuItem item{};
        item.text = text(id, key);
        item.command = key;
        item.enabled = !enabled || enabled();
        item.action = std::move(action);
        menu_->items.push_back(std::move(item));
        menu_enabled_[std::move(key)] = std::move(enabled);
    };
    const auto separator = [this] { lib::MenuItem item{}; item.separator = true; menu_->items.push_back(std::move(item)); };
    const auto external = [this](const types::MenuID id) {
        return [this, id] { if (context_ && context_->menu_action) context_->menu_action(id); };
    };
    const auto external_enabled = [this](const types::MenuID id) {
        return [this, id] { return context_ && context_->menu_enabled && context_->menu_enabled(id); };
    };

    // areatable.d:2062-2075/2112-2145に対応。実際の
    // ポップアップ所有元に合わせて再構築し、ディレクトリと表のメニューを区別する。
    if (last_focus_tree_) {
        if (read_only_) {
            command("import", types::MenuID::Import, [this] { doImport(); }, [this] { return canDoImport(); });
            separator();
            command("copy", types::MenuID::Copy, [this] { AreaTableSelectionEvent e{"copy", false}; copy(e); }, [this] { return canDoC(); });
        } else {
            command("new-directory", types::MenuID::NewAreaDir, [this] { createDir(); }, [this] { return canCreateDir(); });
            separator();
            command("undo", types::MenuID::Undo, [this] { undo(); }, [this] { return canUndo(); });
            command("redo", types::MenuID::Redo, [this] { redo(); }, [this] { return canRedo(); });
            separator();
            command("cut", types::MenuID::Cut, [this] { AreaTableSelectionEvent e{"cut", false}; cut(e); }, [this] { return canDoT(); });
            command("copy", types::MenuID::Copy, [this] { AreaTableSelectionEvent e{"copy", false}; copy(e); }, [this] { return canDoC(); });
            command("paste", types::MenuID::Paste, [this] { AreaTableSelectionEvent e{"paste", false}; paste(e); }, [this] { return canDoP(); });
            command("delete", types::MenuID::Delete, [this] { AreaTableSelectionEvent e{"delete", false}; del(e); }, [this] { return canDoD(); });
            command("clone", types::MenuID::Clone, [this] { AreaTableSelectionEvent e{"clone", false}; clone(e); }, [this] { return canDoClone(); });
        }
    } else {
        command("inc-search", types::MenuID::IncSearch, [this] { incSearch(); },
                [this] { return summary_ && (areaCount() || battleCount() || packageCount()); });
        separator();
        if (read_only_) {
            command("import", types::MenuID::Import, [this] { doImport(); }, [this] { return canDoImport(); });
            separator();
        }
        command("edit-scene", types::MenuID::EditScene, [this] { openAreaScene(true); }, [this] { return canOpenAreaScene(); });
        command("edit-event", types::MenuID::EditEvent, [this] { openAreaEvent(true); }, [this] { return canOpenAreaEvent(); });
        separator();
        command("edit-summary", types::MenuID::EditSummary, [this] { editSummary(); }, [this] { return summary_ != nullptr; });
        separator();
        if (!read_only_) {
            command("new-area", types::MenuID::NewArea, [this] { createArea(); }, [this] { return summary_ != nullptr; });
            command("new-battle", types::MenuID::NewBattle, [this] { createBattle(); }, [this] { return summary_ != nullptr; });
            command("new-package", types::MenuID::NewPackage, [this] { createPackage(); }, [this] { return summary_ != nullptr; });
            separator();
            command("start-area", types::MenuID::SetStartArea, [this] { setStartArea(); }, [this] { return canSetStartArea(); });
            separator();
            command("comment", types::MenuID::Comment, [this] { writeComment(); }, [this] { return canWriteComment(); });
            separator();
            command("undo", types::MenuID::Undo, [this] { undo(); }, [this] { return canUndo(); });
            command("redo", types::MenuID::Redo, [this] { redo(); }, [this] { return canRedo(); });
            separator();
            command("cut", types::MenuID::Cut, [this] { AreaTableSelectionEvent e{"cut", false}; cut(e); }, [this] { return canDoT(); });
            command("copy", types::MenuID::Copy, [this] { AreaTableSelectionEvent e{"copy", false}; copy(e); }, [this] { return canDoC(); });
            command("paste", types::MenuID::Paste, [this] { AreaTableSelectionEvent e{"paste", false}; paste(e); }, [this] { return canDoP(); });
            command("delete", types::MenuID::Delete, [this] { AreaTableSelectionEvent e{"delete", false}; del(e); }, [this] { return canDoD(); });
            command("clone", types::MenuID::Clone, [this] { AreaTableSelectionEvent e{"clone", false}; clone(e); }, [this] { return canDoClone(); });
            separator();
            command("find-id", types::MenuID::FindID, [this] { replaceID(); }, [this] { return canReplaceID(); });
            separator();
            command("renumber", types::MenuID::ReNumbering, [this] { reNumbering(); }, [this] { return canReNumbering(); });
        } else {
            command("copy", types::MenuID::Copy, [this] { AreaTableSelectionEvent e{"copy", false}; copy(e); }, [this] { return canDoC(); });
            separator();
            command("select-all", types::MenuID::SelectAll, [this] { selectAll(); }, [this] { return canSelectAll(); });
        }
    }
    ++menu_->item_update_revision;
    ++menu_->item_order_revision;
    // 共通メニューは表とディレクトリツリーで共有する。
    // 表示直前に内容を設定し、直前のフォーカス先の古
    // い項目ではなく、所有元に応じたDの階層を渡す。
    menu_->on_open = [this] { configureMenu(); };
}

void AreaTable::registerMenuCommand(std::string command, lib::Action action,
                                    std::function<bool()> enabled) {
    if (!menu_) return;
    menu_->items.push_back({command, command, {}, {}, true, false, false, false, {}});
    menu_actions_[command] = std::move(action);
    if (enabled) menu_enabled_[command] = std::move(enabled);
}

void AreaTable::replText() {
    if (read_only_ || disposed_) return;
    const std::weak_ptr<int> life = renumber_lifetime_;
    const auto owner = summary_;
    const auto binding = summary_binding_generation_;
    // Dは置換後に古いパスや空パスを捨てるが、_dirは維持する。
    constructDirTree(false);
    refreshCommentTitles();
    if (life.expired() || disposed_ || summary_ != owner ||
        summary_binding_generation_ != binding) return;
    refresh();
}

void AreaTable::refresh() {
    if (disposed_ || in_refresh_) return;
    in_refresh_ = true;
    ++table_render_revision_;
    // 使用数や画像の準備に使うプロバイダーは、モデル処理へ再入し得る。エ
    // リアの並べ替えや編集ではSummaryポインターは変わらないため、
    // 両方の表示で、プロバイダー呼び出し前の正確な対象状態を保持する。
    const auto domain = captureDomainSnapshot();
    saveSelections();
    refreshRows();
    refreshTree(domain);
    restoreSelections();
    refreshTable(domain);
    refreshStatusLine();
    if (menu_) {
        auto items = menu_->items;
        for (auto& item : items) {
            const auto enabled = menu_enabled_.find(item.command);
            if (enabled != menu_enabled_.end() && enabled->second)
                item.enabled = enabled->second();
        }
        (void)lib::replace_menu_items(menu_, std::move(items));
    }
    in_refresh_ = false;
    // 行、ツリーノード、選択はモデル側の集合。メインメニュ
    // ー操作でネイティブ通知なしに変わる場合があるため、
    // AreaTableの部分木全体を直ちに整合させる。
    if (context_ && context_->services.gui && panel_)
        context_->services.gui->request_redraw(panel_);
}

void AreaTable::refreshRows() {
    rows_.clear();
    if (!summary_) return;
    if (!directory_mode_ && showSummary()) {
        rows_.push_back({AreaTableType::Summary, {}, 0U, summary_->scenarioName(),
                         0U, context_ ? context_->summary_image : 0U});
    }
    const auto add = [this](const auto& values, const AreaTableType type,
                            const lib::ResourceId image) {
        for (const auto& value : values) {
            if (!value) continue;
            if (directory_mode_ && lower(value->dirName()) != lower(directory_)) continue;
            const std::any kind = type;
            if (inc_search_ && !inc_search_->match(
                    directory_mode_ ? value->baseName() : value->name(), kind)) continue;
            auto abstract = std::static_pointer_cast<AbstractArea>(value);
            auto row_image = image;
            if (type == AreaTableType::Area && summary_ &&
                value->id() == summary_->startArea() && context_ &&
                context_->start_area_image != 0U) {
                row_image = context_->start_area_image;
            }
            // Dのディレクトリ表示では、左のツリーにフォルダーを表
            // 示済み。名前列にフォルダーを含めるのは一覧表示だけ。
            rows_.push_back({type, abstract, value->id(),
                             directory_mode_ ? value->baseName() : value->name(),
                             0U, row_image});
        }
    };
    add(summary_->areas(), AreaTableType::Area, context_ ? context_->area_image : 0U);
    add(summary_->battles(), AreaTableType::Battle,
        context_ ? context_->battle_image : 0U);
    add(summary_->packages(), AreaTableType::Package,
        context_ ? context_->package_image : 0U);
    // ネイティブプロバイダーは共有モデルの項目を並べ替え得る。呼
    // び出し前に全行の同一性と文字列を取得し、フレームを拒否した
    // 場合も、次の更新で選択を復元するための整合したキーを残す。
    for (auto& row : rows_)
        if (row.area) row.use_count = useCount(row.area);
    const auto first = !directory_mode_ && showSummary() ? rows_.begin() + 1
                                                         : rows_.begin();
    std::stable_sort(first, rows_.end(), [this](const Row& left, const Row& right) {
        return before(left, right);
    });
}

void AreaTable::refreshTable(const DomainSnapshot& domain) {
    if (!table_) return;
    // selection_は以前の表示順のインデックス。モデルのキー（
    // 種別・ID、通常行ではRow::areaも同じ同一性）を保持し、
    // refreshRowsが新しい並べ替え順を作った後に解決する。
    const auto selection_keys = currentSelectionKeys();
    const auto resolve_selection = [](const std::vector<Row>& rows,
                                      const std::vector<SelectionKey>& keys) {
        std::vector<std::size_t> result;
        for (std::size_t index = 0; index < rows.size(); ++index) {
            const auto& row = rows[index];
            if (std::any_of(keys.begin(), keys.end(), [&](const SelectionKey& key) {
                    return key.type == row.type && key.id == row.id;
                }))
                result.push_back(index);
        }
        return result;
    };
    const auto expected_selection = selection_;
    const auto next_selection = resolve_selection(rows_, selection_keys);
    std::vector<lib::TableRow> next_rows;
    next_rows.reserve(rows_.size());
    const auto warning_image = prop_->images().load(lib::StringView{"warning.png"});
    const auto model_warnings = [this](const ::cwxeditor::cwx::path::CWXPath& path) {
        const auto* const resolved_skin = summarySkin();
        if (!resolved_skin || !summary_) return std::vector<std::string>{};
        return ::cwxeditor::cwx::warning::warnings(
            prop_->parent(), *resolved_skin, summary_, path, summary_->legacy(),
            summary_->dataVersion(), prop_->var().targetVersion.value);
    };
    for (const auto& row : rows_) {
        lib::TableRow value;
        value.cells = {
            {rowText(row, 0), std::to_string(row.id), row.image, false, true},
            {rowText(row, 1), row.name, 0U, false, true},
            {rowText(row, 2), std::to_string(row.use_count), 0U, false, true},
        };
        if (row.type == AreaTableType::Summary && summary_)
            value.warnings = model_warnings(*summary_);
        else if (row.area)
            value.warnings = model_warnings(*row.area);
        if (!value.warnings.empty()) value.warning_icon = warning_image;
        next_rows.push_back(std::move(value));
    }
    const auto source_summary = summary_;
    const auto revision = table_render_revision_;
    const auto reveal = next_selection.empty()
        ? std::optional<std::size_t>{} : std::optional<std::size_t>{next_selection.front()};
    auto publication = lib::prepare_table_rows(
        table_, std::move(next_rows), next_selection, reveal,
        [this, source_summary, revision, domain, expected_selection, selection_keys,
          next_selection, resolve_selection] {
            if (disposed_ || summary_ != source_summary ||
                table_render_revision_ != revision || selection_ != expected_selection ||
                !domainMatches(domain))
                return false;
            const auto current_keys = currentSelectionKeys();
            if (current_keys.size() != selection_keys.size() ||
                !std::equal(current_keys.begin(), current_keys.end(), selection_keys.begin(),
                            [](const SelectionKey& left, const SelectionKey& right) {
                                return left.type == right.type && left.id == right.id;
                            }))
                return false;
            return resolve_selection(rows_, current_keys) == next_selection;
        });
    if (publication.commit()) selection_ = next_selection;
}

void AreaTable::refreshTree(const DomainSnapshot& domain) {
    if (!directory_mode_ || !tree_ || !summary_) return;
    const bool keepSummarySelection = summarySelected();
    constructDirTree(true);
    // DのrefreshDirTreeは、折りたたまれたディレクトリのパ
    // スだけを記録し、他のノードは展開状態へ戻す。下で再生成する一時的な
    // TreeNode IDではなく、対象のパスでその状態を保持する。
    std::unordered_set<lib::TreeItemId> parents_with_children;
    for (const auto& node : tree_->nodes) parents_with_children.insert(node.parent);
    std::unordered_set<std::string> collapsed_paths;
    for (const auto& node : tree_->nodes)
        if (!node.expanded && node.value != "__summary__" &&
            parents_with_children.contains(node.id)) collapsed_paths.insert(node.value);
    const auto expanded = [&collapsed_paths](const std::string_view path) {
        return !collapsed_paths.contains(std::string(path));
    };
    std::vector<lib::TreeNode> next_nodes;
    std::vector<lib::TreeItemId> next_selection;
    std::uint64_t next_id = 0U;
    const auto warning_image = prop_->images().load(lib::StringView{"warning.png"});
    if (showSummary()) {
        lib::TreeNode node{++next_id, 0U, summary_->scenarioName(),
                           "__summary__", context_ ? context_->summary_image : 0U,
                           false, false, false};
        if (const auto* const resolved_skin = summarySkin()) {
            node.warnings = ::cwxeditor::cwx::warning::warnings(
                prop_->parent(), *resolved_skin, summary_, *summary_,
                summary_->legacy(), summary_->dataVersion(),
                prop_->var().targetVersion.value);
            if (!node.warnings.empty()) node.warning_icon = warning_image;
        }
        next_nodes.push_back(std::move(node));
    }
    const auto root_name = prop_->requiredMsgs().areaDirRoot.value.empty()
                               ? std::string("Scenario")
                               : prop_->requiredMsgs().areaDirRoot.value;
    const auto root_id = ++next_id;
    next_nodes.push_back({root_id, 0U, root_name, {},
                           context_ ? context_->area_dir_image : 0U,
                           true, false, false, true});
    const auto append = [&](const auto& self, const std::shared_ptr<DirTree>& dir,
                            const std::uint64_t parent_id) -> void {
        if (!dir) return;
        std::vector<std::shared_ptr<DirTree>> children = dir->subDirs;
        std::stable_sort(children.begin(), children.end(), [](const auto& left,
                                                              const auto& right) {
            return left && right && left->name < right->name;
        });
        for (const auto& child : children) {
            if (!child) continue;
            const auto id = ++next_id;
            lib::TreeNode node{id, parent_id, child->name, child->path(),
                               context_ ? context_->area_dir_image : 0U,
                               expanded(child->path()), false, false};
            node.warnings = ::cwxeditor::cwx::warning::sjisWarnings(
                prop_->parent(), summary_->legacy(), child->name,
                prop_->requiredMsgs().areaDirName.value);
            if (!node.warnings.empty()) node.warning_icon = warning_image;
            next_nodes.push_back(std::move(node));
            self(self, child, id);
        }
    };
    append(append, dirs_, root_id);
    const auto selected = findDirTree(directory_);
    if (keepSummarySelection && showSummary()) {
        for (const auto& node : next_nodes)
            if (node.value == "__summary__") next_selection.push_back(node.id);
    } else if (selected) {
        for (const auto& node : next_nodes) {
            if (node.value == selected->path()) next_selection.push_back(node.id);
        }
    } else {
        next_selection.push_back(root_id);
    }
    const auto source_summary = summary_;
    const auto source_directory = directory_;
    (void)lib::replace_tree_nodes(
        tree_, std::move(next_nodes), std::move(next_selection), {},
        [this, source_summary, source_directory, domain] {
            return summary_ == source_summary && directory_ == source_directory &&
                   domainMatches(domain);
        });
}

void AreaTable::refreshStatusLine() {
    status_line_.clear();
    const auto append = [this](const std::string& name, const int count) {
        if (count == 0) return;
        if (!status_line_.empty()) status_line_ += " ";
        status_line_ += ::cwxeditor::cwx::utils::tryFormat(prop_->requiredMsgs().areaStatus.value, name, count);
    };
    if (summary_) {
        append(prop_->requiredMsgs().area.value, areaCount());
        append(prop_->requiredMsgs().battle.value, battleCount());
        append(prop_->requiredMsgs().cwPackage.value, packageCount());
    }
    if (context_ && context_->callbacks.status) context_->callbacks.status(status_line_);
    if (context_ && context_->callbacks.refresh_toolbar) context_->callbacks.refresh_toolbar();
}

void AreaTable::saveSelections() {
    if (restoring_undo_selection_) return;
    // Summaryが表の行になるのは一覧表示だけ。Dは更新時に、
    // ディレクトリ内エリアの選択とは独立して現在の行を保持する。
    saved_summary_selected_ = !directory_mode_ && showSummary() && summarySelected();
    if (!prop_->var().saveTableViewSelection.value) {
        saved_selection_.clear();
        return;
    }
    // Dは現在のフォルダー・絞り込みで見えている行だけを更新す
    // る。別フォルダーの選択キーは無関係な更新でも保持する。
    for (std::size_t index = 0; index < rows_.size(); ++index) {
        const auto& row = rows_[index];
        if (row.type == AreaTableType::Summary) continue;
        const auto match = std::find_if(saved_selection_.begin(), saved_selection_.end(),
            [&](const SelectionKey& key) { return key.type == row.type && key.id == row.id; });
        const bool selected = std::find(selection_.begin(), selection_.end(), index) != selection_.end();
        if (selected && match == saved_selection_.end())
            saved_selection_.push_back({row.type, row.id});
        else if (!selected && match != saved_selection_.end())
            saved_selection_.erase(match);
    }
    std::erase_if(saved_selection_, [this](const SelectionKey& key) {
        if (!summary_) return true;
        // ネイティブプロバイダーは、更新前に可変な共有項目を並べ
        // 替え得る。公開の保護がその一時的な順序を拒否している間
        // 、選択の維持をID整列前提の検索APIに依存させない。
        const auto contains = [&key](const auto& values) {
            return std::any_of(values.begin(), values.end(), [&key](const auto& value) {
                return value && value->id() == key.id;
            });
        };
        switch (key.type) {
        case AreaTableType::Area: return !contains(summary_->areas());
        case AreaTableType::Battle: return !contains(summary_->battles());
        case AreaTableType::Package: return !contains(summary_->packages());
        default: return true;
        }
    });
}

void AreaTable::restoreSelections() {
    if (!prop_->var().saveTableViewSelection.value) {
        selection_.erase(std::remove_if(selection_.begin(), selection_.end(),
            [this](const std::size_t index) { return index >= rows_.size(); }),
            selection_.end());
        return;
    }
    selection_.clear();
    for (std::size_t index = 0; index < rows_.size(); ++index) {
        const auto& row = rows_[index];
        if (row.type == AreaTableType::Summary) {
            if (saved_summary_selected_) selection_.push_back(index);
            continue;
        }
        const auto match = std::find_if(saved_selection_.begin(), saved_selection_.end(),
            [&](const SelectionKey& key) { return key.type == row.type && key.id == row.id; });
        if (match != saved_selection_.end()) selection_.push_back(index);
    }
}

void AreaTable::updateSelection(const std::vector<std::size_t>& value) {
    if (in_refresh_ || disposed_) return;
    last_focus_tree_ = false;
    selection_.clear();
    for (const auto index : value) if (index < rows_.size()) selection_.push_back(index);
    saveSelections();
    refreshStatusLine();
}

void AreaTable::updateTreeSelection(const std::vector<lib::TreeItemId>& value) {
    if (in_refresh_ || disposed_ || !tree_) return;
    last_focus_tree_ = true;
    bool directory_changed = false;
    if (!value.empty()) {
        const auto it = std::find_if(tree_->nodes.begin(), tree_->nodes.end(),
            [&](const lib::TreeNode& node) { return node.id == value.front(); });
        if (it != tree_->nodes.end() && it->value != "__summary__" &&
            directory_ != it->value) {
            directory_ = it->value;
            directory_changed = true;
        }
    }
    // プログラムによるインライン編集は選択後に対象を解決する。こ
    // こで未変更のツリーを再公開すると、選択とbegin()の
    // 間で安定したノードの世代を無効にするため、再公開しない。
    if (directory_changed) {
        const std::weak_ptr<int> life = renumber_lifetime_;
        const auto source_summary = summary_;
        const auto source_binding = summary_binding_generation_;
        const auto source_directory = directory_;
        const auto current = [this, life, source_summary, source_binding,
                              source_directory] {
            return !life.expired() && !disposed_ && summary_ == source_summary &&
                summary_binding_generation_ == source_binding &&
                directory_ == source_directory;
        };
        closeCommentDialogs();
        if (current()) {
            refresh();
            if (!current()) return;
            // DのupdateDirSelは、保存済みの表の選択
            // に新しいディレクトリの資源がなければ、行0を選ぶ。
            if (prop_ &&
                prop_->var().saveTableViewSelection.value && selection_.empty() &&
                !rows_.empty()) {
                // 代替行の選択で、フォーカスしていたウィジェット
                // を変えない。Dはツリーから更新を始めた場合、_lastFocus
                // をディレクトリツリーに保つ。
                const bool tree_focus = last_focus_tree_;
                selectIndex(0U, false);
                // 表の選択やツールバーのコールバックが再入し、別のSummary
                // やディレクトリへ接続する場合がある。上で更新した対象への
                // 接続が維持されている場合だけ、ツリーへフォーカスを戻す。
                if (!current()) return;
                last_focus_tree_ = tree_focus;
            }
        }
    } else refreshStatusLine();
}

void AreaTable::beginCreatedNameEdit(const AreaTableType type,
                                     const std::uint64_t id) {
    if (!context_ || !context_->services.gui || !table_ || !panel_) return;
    lib::ProgrammaticInlineEditRequest request;
    request.table = table_;
    request.focus_scope = panel_;
    request.row_height =
        lib::scaled_ui_metric(lib::kEditorUiMetrics.row_height);
    request.matches_stable_row = [this, type, id](const std::size_t row) {
        return row < rows_.size() && rows_[row].type == type &&
               rows_[row].id == id;
    };
    (void)context_->services.gui->request_programmatic_inline_edit(request);
}

void AreaTable::beginCreatedDirEdit(std::string path) {
    if (!context_ || !context_->services.gui || !tree_ || !panel_ || path.empty()) return;
    lib::ProgrammaticInlineEditRequest request;
    request.tree = tree_;
    request.focus_scope = panel_;
    request.row_height = lib::scaled_ui_metric(lib::kEditorUiMetrics.row_height);
    request.matches_stable_tree_node = [path = std::move(path)](const lib::TreeNode& node) {
        return node.value == path;
    };
    (void)context_->services.gui->request_programmatic_inline_edit(request);
}

void AreaTable::editCell(const std::size_t row, const std::size_t column,
                         std::string value) {
    if (read_only_ || row >= rows_.size() || column != 1U) return;
    // DのTableTextEditは、確定した空の名
    // 前をシナリオ・エリアのモデルへ渡す前に拒否する。
    if (value.empty()) return;
    auto& selected = rows_[row];
    const auto old = selected.name;
    if (selected.type == AreaTableType::Summary) {
        if (!summary_ || old == value) return;
        const auto new_name = value;
        pushUndo([this, old] {
                     if (!summary_) return;
                     summary_->scenarioName(old);
                     notifySummaryChanged();
                     refresh();
                 },
                 [this, new_name] {
                     if (!summary_) return;
                     summary_->scenarioName(new_name);
                     notifySummaryChanged();
                     refresh();
                 });
        summary_->scenarioName(new_name);
        notifySummaryChanged();
        refresh();
    } else if (selected.area) {
        auto area_value = selected.area;
        const auto old_name = area_value->name();
        if (directory_mode_) {
            // Dは空・同値の検査前に、セル入力からすべての区切りを除く。toBaseName
            // は意図的にパス末尾を返す契約のままとする。
            value.erase(std::remove(value.begin(), value.end(), '\\'), value.end());
            if (value.empty() || area_value->baseName() == value) return;
        } else if (old_name == value) return;
        // AbstractArea::baseNameと同様に、先頭の区切りも
        // 含め元の接頭部を正確に維持する。取り消し用には旧名全体を保持する。
        const auto separator = old_name.find_last_of('\\');
        const auto new_name = directory_mode_ && separator != std::string::npos
            ? old_name.substr(0, separator + 1U) + value : value;
        pushUndo([this, area_value, old_name] { area_value->name(old_name); callRefArea(area_value.get()); refresh(); },
                 [this, area_value, new_name] { area_value->name(new_name); callRefArea(area_value.get()); refresh(); });
        area_value->name(new_name);
        callRefArea(area_value.get());
        // DのeditEndは型付きref*の公開前に表を更新する。ヘッドレ
        // スや組み込みホストが、そのコールバックを返すことを前提にしない。
        refresh();
    }
}

void AreaTable::editTreeItem(const lib::TreeItemId id, std::string value) {
    if (read_only_ || disposed_ || !tree_) return;
    const auto node = std::find_if(tree_->nodes.begin(), tree_->nodes.end(),
        [id](const lib::TreeNode& candidate) { return candidate.id == id; });
    if (node == tree_->nodes.end()) return;
    const auto restore_text = [&](std::string text) { node->text = std::move(text); };
    // DのdirEditEndは、シナリオとディレクトリの両ラベルからバックスラッシュを除く。
    value.erase(std::remove(value.begin(), value.end(), '\\'), value.end());
    if (value.empty()) {
        if (node->value == "__summary__" && summary_) restore_text(summary_->scenarioName());
        else if (const auto dir = findDirTree(node->value)) restore_text(dir->name);
        return;
    }
    if (node->value == "__summary__") {
        if (!summary_ || summary_->scenarioName() == value) {
            if (summary_) restore_text(summary_->scenarioName());
            return;
        }
        const auto old = summary_->scenarioName();
        pushUndo(
            [this, old] {
                if (!summary_) return;
                summary_->scenarioName(old);
                notifySummaryChanged();
                refresh();
            },
            [this, value] {
                if (!summary_) return;
                summary_->scenarioName(value);
                notifySummaryChanged();
                refresh();
            });
        summary_->scenarioName(value);
        notifySummaryChanged();
        refresh();
        return;
    }
    const auto dir = findDirTree(node->value);
    if (!dir || !dir->parent) {
        if (dir) restore_text(dir->name);
        return;
    }
    const auto old_name = dir->name;
    const auto new_name = uniqueDirName(value, [dir](const std::string_view candidate) {
        return std::all_of(dir->parent->subDirs.begin(), dir->parent->subDirs.end(),
            [dir, candidate](const auto& sibling) {
                return !sibling || sibling.get() == dir.get() ||
                       AreaTable::lower(sibling->name) != AreaTable::lower(candidate);
            });
    });
    if (old_name == new_name) {
        restore_text(old_name);
        return;
    }
    const auto undo_selection_before = captureUndoSelection();
    const auto old_path = dir->path();
    dir->name = new_name;
    const auto new_path = dir->path();
    dir->name = old_name;
    // DのUndoRenameDirは、取り消し・やり直しごとに現在の対象部分木を再対応付
    // けする。ツリー全体の保存状態を戻すと、無関係な検索・置換の編集を上書きしてしまう。
    const auto owner = summary_;
    const auto binding = summary_binding_generation_;
    const std::weak_ptr<int> life = renumber_lifetime_;
    const auto apply = [this, owner, binding, life](const std::string& from,
                                                   const std::string& to,
                                                   const std::string& name) {
        const auto current = [this, owner, binding, life] {
            return !life.expired() && !disposed_ && summary_ == owner &&
                   summary_binding_generation_ == binding;
        };
        if (!current()) return false;
        const auto owner_hold = owner ? owner->weak_from_this().lock() : nullptr;
        const auto from_folded = lower(from);
        const auto prefix = from_folded + '\\';
        const auto remapped = [&](const std::string& path) -> std::optional<std::string> {
            const auto folded = lower(path);
            if (folded == from_folded) return to;
            if (folded.rfind(prefix, 0U) == 0U) return to + path.substr(from.size());
            return std::nullopt;
        };
        std::vector<AbstractAreaPtr> changed;
        const auto update = [&](const auto& values) {
            for (const auto& area : values) {
                if (!area) continue;
                if (const auto next = remapped(area->dirName())) {
                    area->dirName(*next);
                    changed.push_back(std::static_pointer_cast<AbstractArea>(area));
                }
            }
        };
        // 置換通知がdirs_を再構築した可能性があるため再解決する。findDirTree
        // はパスがなければ祖先を返し得るので、完全一致を確認する。
        const auto node = findDirTree(from);
        if (node && node->parent && lower(node->path()) == from_folded)
            node->name = name;
        if (owner) {
            update(owner->areas());
            update(owner->battles());
            update(owner->packages());
        }
        if (const auto next = remapped(directory_)) directory_ = *next;
        sortDirTree(dirs_);
        // コールバックがこの表示を終了・再接続し得るため、通知前に変更を完了する。
        for (const auto& area : changed) {
            callRefArea(area.get());
            if (!current()) return false;
        }
        refresh();
        return current();
    };
    if (!apply(old_path, new_path, new_name)) return;
    pushUndo([apply, old_path, new_path, old_name] {
                 (void)apply(new_path, old_path, old_name);
             },
             [apply, old_path, new_path, new_name] {
                 (void)apply(old_path, new_path, new_name);
             }, undo_selection_before);
}

void AreaTable::selectIndex(const std::size_t index, const bool additive) {
    if (index >= rows_.size()) return;
    if (!additive) selection_.clear();
    if (std::find(selection_.begin(), selection_.end(), index) == selection_.end()) {
        selection_.push_back(index);
    }
    if (table_) (void)lib::select_table_rows(*table_, selection_, true);
    refreshStatusLine();
}

AreaTable::UndoSelectionSnapshot AreaTable::captureUndoSelection() {
    saveSelections();
    UndoSelectionSnapshot result{currentSelectionKeys(), saved_selection_, directory_, {}, last_focus_tree_};
    if (tree_) for (const auto id : tree_->selection) {
        const auto node = std::find_if(tree_->nodes.begin(), tree_->nodes.end(),
            [id](const auto& value) { return value.id == id; });
        if (node != tree_->nodes.end()) result.tree_paths.push_back(node->value);
    }
    return result;
}

void AreaTable::restoreUndoSelection(const UndoSelectionSnapshot& value) {
    // DのATUndoは更新前に保存済み集合を戻し、その後で明示的な
    // 型付きの現在選択を戻す。更新でそれらを古い行に置き換えない。
    const std::weak_ptr<int> life = renumber_lifetime_;
    const auto owner = summary_;
    const auto binding = summary_binding_generation_;
    restoring_undo_selection_ = true;
    // DのgetSelectionInfoはTableの最初の行のAbstractArea
    // だけを記録する。汎用ATUndoはSummaryや行なしの状態では_dirを戻さず、
    // selectFromInfoのTreeへの代替もupdateDirSelを呼ばない。
    const bool typed = !value.current.empty() &&
                       value.current.front().type != AreaTableType::Summary;
    if (typed && directory_mode_ && summary_) {
        const auto key = value.current.front();
        const auto resolve = [&](const auto& objects) {
            for (const auto& object : objects)
                if (object && object->id() == key.id) {
                    directory_ = object->dirName();
                    break;
                }
        };
        switch (key.type) {
        case AreaTableType::Area: resolve(summary_->areas()); break;
        case AreaTableType::Battle: resolve(summary_->battles()); break;
        case AreaTableType::Package: resolve(summary_->packages()); break;
        default: break;
        }
    }
    last_focus_tree_ = value.tree_focus;
    saved_selection_ = prop_->var().saveTableViewSelection.value
        ? value.saved : std::vector<SelectionKey>{};
    selection_.clear();
    if (tree_) tree_->selection.clear();
    refresh();
    if (life.expired()) return;
    restoring_undo_selection_ = false;
    if (summary_ != owner || summary_binding_generation_ != binding) return;
    selection_.clear();
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if (std::any_of(value.current.begin(), value.current.end(), [&](const auto& key) {
            return rows_[i].type == key.type && rows_[i].id == key.id;
        })) selection_.push_back(i);
    if (!typed && !directory_mode_ && !rows_.empty()) selection_ = {0U};
    if (table_) (void)lib::select_table_rows(*table_, selection_, false);
    if (tree_ && directory_mode_) {
        std::vector<lib::TreeItemId> ids;
        if (!typed) {
            // Dは項目0を選ぶ。Summaryが表示されていればそれを、なければルー
            // トを選ぶ。Treeハンドラーへ通知せず、現在のディレクトリも変えない。
            if (!tree_->nodes.empty()) ids.push_back(tree_->nodes.front().id);
        } else {
            for (const auto& node : tree_->nodes)
                if (lower(node.value) == lower(directory_)) {
                    ids.push_back(node.id);
                    break;
                }
        }
        (void)lib::select_tree_items(*tree_, ids, false, false);
    }
    saveSelections();
    refreshStatusLine();
}

void AreaTable::pushUndo(std::function<void()> undo_action,
                         std::function<void()> redo_action,
                         std::optional<UndoSelectionSnapshot> before) {
    const auto manager = undo_;
    const auto owner = summary_;
    const auto binding = summary_binding_generation_;
    const std::weak_ptr<int> life = renumber_lifetime_;
    const auto refresh_toolbar = context_ ? context_->callbacks.refresh_toolbar : lib::Action{};
    if (manager && (undo_action || redo_action)) {
        // Dのudb/udaと同様に、履歴をたどる時点で反対側の現在状
        // 態を取得する。コマンド後の選択変更も、やり直しへ反映する。
        auto state = std::make_shared<UndoSelectionSnapshot>(before ? std::move(*before) : captureUndoSelection());
        const auto wrap = [this, life, owner, binding, state](std::function<void()> action) {
            return [this, life, owner, binding, state, action = std::move(action)] {
                if (life.expired() || summary_ != owner || summary_binding_generation_ != binding || disposed_) return;
                auto opposite = captureUndoSelection();
                // DのATUndo::udbは、後続コマンドをエリア一覧へ向ける。
                if (context_ && context_->services.gui && table_)
                    context_->services.gui->focus(table_);
                if (life.expired() || summary_ != owner || summary_binding_generation_ != binding || disposed_) return;
                if (action) action();
                if (life.expired() || summary_ != owner || summary_binding_generation_ != binding || disposed_) return;
                restoreUndoSelection(*state);
                if (life.expired() || summary_ != owner || summary_binding_generation_ != binding || disposed_) return;
                last_focus_tree_ = false;
                *state = std::move(opposite);
            };
        };
        manager->add(std::make_shared<LambdaUndo>(wrap(std::move(undo_action)), wrap(std::move(redo_action))));
    }
    if (life.expired() || summary_ != owner || summary_binding_generation_ != binding) return;
    if (refresh_toolbar) refresh_toolbar();
}

void AreaTable::storeSummaryEdit(std::function<void()> swap_state) {
    if (read_only_ || !summary_ || !swap_state) return;
    auto redo = swap_state;
    pushUndo(std::move(swap_state), std::move(redo));
}

void AreaTable::notifyChanged(AbstractArea* value, const bool deleted) {
    if (!value || !context_) return;
    const auto owner = summary_;
    const auto owner_hold = owner ? owner->weak_from_this().lock() : nullptr;
    const auto binding = summary_binding_generation_;
    const std::weak_ptr<int> life = renumber_lifetime_;
    const auto context = *context_;
    const auto current = [this, life, owner, binding] {
        return !life.expired() && summary_ == owner && summary_binding_generation_ == binding;
    };
    refreshCommentDialog(value, deleted);
    if (!current()) return;
    if (deleted && context.callbacks.del_area) context.callbacks.del_area(value);
    if (!deleted && context.callbacks.ref_area) context.callbacks.ref_area(value);
    if (!current() || !context.area_window) return;
    if (auto area_value = dynamic_cast<Area*>(value)) {
        if (deleted) context.area_window->signals.del_area.emit(area_value);
        else context.area_window->signals.ref_area.emit(area_value);
    } else if (auto battle_value = dynamic_cast<Battle*>(value)) {
        if (deleted) context.area_window->signals.del_battle.emit(battle_value);
        else context.area_window->signals.ref_battle.emit(battle_value);
    } else if (auto package_value = dynamic_cast<Package*>(value)) {
        if (deleted) context.area_window->signals.del_package.emit(package_value);
        else context.area_window->signals.ref_package.emit(package_value);
    }
}

void AreaTable::notifySummaryChanged() {
    if (context_ && context_->callbacks.ref_scenario) context_->callbacks.ref_scenario(summary_);
    if (context_ && context_->callbacks.ref_scenario_name) context_->callbacks.ref_scenario_name();
    if (context_ && context_->callbacks.refresh_flags) context_->callbacks.refresh_flags();
}

void AreaTable::applyIdsCollisionSafe(
    const std::vector<AbstractAreaPtr>& source,
    const std::vector<std::uint64_t>& desired, const std::size_t first) {
    if (first >= source.size() || desired.size() != source.size()) return;
    // モデルとカウンターのトランザクション全体を、表示から独立して所有する。ど
    // ちらの段階でも、カウンター利用者の公開やモデル変更ハンドラーが表示を終了
    // ・再接続し得る。一時的なUINT64のIDをモデルに残してはならない。
    const auto values = source;
    const auto ids = desired;
    if (std::any_of(values.begin(),values.end(),[](const auto& value){return !value;})) return;
    if (std::equal(values.begin()+static_cast<std::ptrdiff_t>(first), values.end(),
                   ids.begin()+static_cast<std::ptrdiff_t>(first),
                   [](const auto& value, const auto id) { return value->id() == id; })) return;
    const auto owner = summary_;
    const auto owner_hold = owner ? owner->weak_from_this().lock() : nullptr;
    const auto counter = owner ? owner->useCounter() : nullptr;
    const auto binding = summary_binding_generation_;
    const std::weak_ptr<int> life = renumber_lifetime_;
    std::vector<std::uint64_t> old_ids;
    for (const auto& value : values) old_ids.push_back(value->id());
    const auto set_id = [counter](const AbstractAreaPtr& value, const std::uint64_t id) {
        const auto old = value->id();
        if (old == id) return;
        if (counter) {
            if (dynamic_cast<Area*>(value.get())) counter->change(usecounter::AreaId(old),usecounter::AreaId(id));
            else if (dynamic_cast<Battle*>(value.get())) counter->change(usecounter::BattleId(old),usecounter::BattleId(id));
            else if (dynamic_cast<Package*>(value.get())) counter->change(usecounter::PackageId(old),usecounter::PackageId(id));
        }
        value->id(id);
    };
    const auto count = values.size() - first;
    const auto temporary_base = std::numeric_limits<std::uint64_t>::max() -
                                static_cast<std::uint64_t>(count) + 1U;
    // 二段階全体で扱うのは所有されたモデルデータであり、thisではない。
    for (std::size_t i=first;i<values.size();++i) set_id(values[i],temporary_base+(i-first));
    for (std::size_t i=first;i<values.size();++i) set_id(values[i],ids[i]);
    const auto current = [this,life,owner,binding] {
        return !life.expired() && summary_==owner && summary_binding_generation_==binding;
    };
    if (!current()) return;
    // DのrefreshIDs(false)は、同じTableItemの選択を維
    // 持する。共通の全行更新が新IDを見る前に、保持した型付きキーを変換する。
    const auto type_of = [](const AbstractAreaPtr& value) {
        return dynamic_cast<Area*>(value.get()) ? AreaTableType::Area
            : dynamic_cast<Battle*>(value.get()) ? AreaTableType::Battle : AreaTableType::Package;
    };
    for (auto& key : saved_selection_) {
        for (std::size_t i = first; i < values.size(); ++i)
            if (key.type == type_of(values[i]) && key.id == old_ids[i]) {
                key.id = ids[i]; break;
            }
    }
    for (auto& row : rows_)
        if (row.area && std::any_of(values.begin() + static_cast<std::ptrdiff_t>(first), values.end(),
                [&](const auto& value) { return value.get() == row.area.get(); }))
            row.id = row.area->id();
    for (std::size_t i=first;i<values.size();++i) {
        if (old_ids[i]!=ids[i]) callRefArea(values[i].get());
        if (!current()) return;
    }
}

void AreaTable::renumberList(const AreaTableType type, const std::uint64_t requested_id,
                             const std::size_t index) {
    if (read_only_ || !summary_) return;
    std::vector<AbstractAreaPtr> values;
    if (type == AreaTableType::Area)
        values.assign(summary_->areas().begin(), summary_->areas().end());
    else if (type == AreaTableType::Battle)
        values.assign(summary_->battles().begin(), summary_->battles().end());
    else if (type == AreaTableType::Package)
        values.assign(summary_->packages().begin(), summary_->packages().end());
    if (index >= values.size() || requested_id == 0U) return;
    if (index > 0U && values[index - 1U]->id() >= requested_id) return;

    std::vector<std::uint64_t> old_ids;
    std::vector<std::uint64_t> new_ids;
    old_ids.reserve(values.size());
    new_ids.reserve(values.size());
    for (const auto& value : values) old_ids.push_back(value->id());
    new_ids = old_ids;
    auto next = requested_id;
    for (std::size_t i = index; i < new_ids.size(); ++i) {
        new_ids[i] = next;
        if (next != std::numeric_limits<std::uint64_t>::max()) ++next;
    }
    if (old_ids == new_ids) return;
    pushUndo(
        [this, values, old_ids, index] {
            applyIdsCollisionSafe(values, old_ids, index);
            refresh();
        },
        [this, values, new_ids, index] {
            applyIdsCollisionSafe(values, new_ids, index);
            refresh();
        });
    applyIdsCollisionSafe(values, new_ids, index);
    refresh();
}

AbstractAreaPtr AreaTable::movementNeighbor(const int delta) const {
    if (!summary_ || read_only_ || last_focus_tree_ ||
        sort_column_ != AreaTableSortColumn::Id || selection_.size() != 1U) return {};
    // Dはディレクトリモードで現在のディレクトリの資源を数える。この完
    // 全な一覧で、非表示の兄弟フォルダーによって移動を無効にしない。
    const auto visible_resources = std::count_if(rows_.begin(), rows_.end(),
        [](const Row& row) { return static_cast<bool>(row.area); });
    if (visible_resources != areaCount() + battleCount() + packageCount()) return {};
    const auto selected = selection_.front();
    if (selected >= rows_.size()) return {};
    const auto target = static_cast<std::ptrdiff_t>(selected) + delta;
    if (target < 0 || target >= static_cast<std::ptrdiff_t>(rows_.size())) return {};
    const auto& from = rows_[selected];
    const auto& to = rows_[static_cast<std::size_t>(target)];
    if (!from.area || !to.area || from.type != to.type) return {};
    return to.area;
}

void AreaTable::moveSelected(const int delta) {
    const auto other = movementNeighbor(delta);
    const auto value = getSelectionArea();
    if (!other || !value) return;
    const auto type = getSelectionInfo().second;
    const auto owner = summary_;
    const auto binding = summary_binding_generation_;
    const std::weak_ptr<int> alive = renumber_lifetime_;
    // DのudImplに合わせ、ID降順も含め、表示上の隣接行で
    // 対象の組を決める。履歴はどちらの向きでも同じ組を交換する。
    const auto swap = [this, alive, owner, binding, value, other, type] {
        if (alive.expired() || summary_ != owner || summary_binding_generation_ != binding) return;
        const auto swapIn = [&](const auto& list, auto apply) {
            const auto first = std::find_if(list.begin(), list.end(),
                [&](const auto& item) { return item.get() == value.get(); });
            const auto second = std::find_if(list.begin(), list.end(),
                [&](const auto& item) { return item.get() == other.get(); });
            if (first == list.end() || second == list.end()) return;
            apply(static_cast<std::ptrdiff_t>(first - list.begin()),
                  static_cast<std::ptrdiff_t>(second - list.begin()));
        };
        if (type == AreaTableType::Area)
            swapIn(owner->areas(), [&](auto a, auto b) { owner->swapArea(a, b); });
        else if (type == AreaTableType::Battle)
            swapIn(owner->battles(), [&](auto a, auto b) { owner->swapBattle(a, b); });
        else if (type == AreaTableType::Package)
            swapIn(owner->packages(), [&](auto a, auto b) { owner->swapPackage(a, b); });
    };
    pushUndo(swap, swap);
    if (alive.expired() || summary_ != owner || summary_binding_generation_ != binding) return;
    swap();
    if (alive.expired() || summary_ != owner || summary_binding_generation_ != binding) return;
    refresh();
    if (alive.expired() || summary_ != owner || summary_binding_generation_ != binding) return;
    static_cast<void>(select(value));
}


void AreaTable::moveSelectedByInsertion(const std::ptrdiff_t insertion_index) {
    const auto value = getSelectionArea();
    if (!summary_ || read_only_ || !value || sort_column_ != AreaTableSortColumn::Id)
        return;
    const auto owner = summary_;
    const auto binding = summary_binding_generation_;
    const std::weak_ptr<int> life = renumber_lifetime_;
    const auto apply = [&](const auto& resource) {
        if (!resource) return;
        const auto from = owner->indexOf(resource);
        if (from < 0 || insertion_index < 0) return;
        const auto final_index = insertion_index - (from < insertion_index ? 1 : 0);
        if (from == final_index) return;
        // Summary::insertはDのDropAreaと同様に、削除前の境
        // 界を受け取る。履歴は最終位置を記録し、たどるたびに元の境界へ変換する。
        const auto move_to = [this, life, owner, binding, resource](std::ptrdiff_t final) {
            if (life.expired() || summary_ != owner || summary_binding_generation_ != binding) return;
            const auto current = owner->indexOf(resource);
            if (current < 0) return;
            owner->insert(final + (current < final ? 1 : 0), resource);
            if (life.expired() || summary_ != owner || summary_binding_generation_ != binding) return;
            callRefArea(resource.get());
            if (life.expired() || summary_ != owner || summary_binding_generation_ != binding) return;
            refresh();
            if (life.expired() || summary_ != owner || summary_binding_generation_ != binding) return;
            static_cast<void>(select(resource));
        };
        pushUndo([move_to, from] { move_to(from); },
                 [move_to, final_index] { move_to(final_index); });
        if (life.expired() || summary_ != owner || summary_binding_generation_ != binding) return;
        move_to(final_index);
    };
    if (const auto area = std::dynamic_pointer_cast<Area>(value)) apply(area);
    else if (const auto battle = std::dynamic_pointer_cast<Battle>(value)) apply(battle);
    else if (const auto package = std::dynamic_pointer_cast<Package>(value)) apply(package);
}

void AreaTable::removeArea(const AbstractAreaPtr& value, const bool notify) {
    if (!summary_ || !value) return;
    const auto owner = summary_;
    const auto owner_hold = owner->weak_from_this().lock();
    const auto binding = summary_binding_generation_;
    const std::weak_ptr<int> life = renumber_lifetime_;
    if (auto typed = std::dynamic_pointer_cast<Area>(value)) owner->remove(typed);
    else if (auto typed = std::dynamic_pointer_cast<Battle>(value)) owner->remove(typed);
    else if (auto typed = std::dynamic_pointer_cast<Package>(value)) owner->remove(typed);
    if (life.expired() || summary_ != owner || summary_binding_generation_ != binding) return;
    if (notify) notifyChanged(value.get(), true);
}

void AreaTable::insertArea(const AbstractAreaPtr& value, const std::size_t index,
                           const AreaTableType type, const bool force_new_id, const bool notify) {
    if (!summary_ || !value) return;
    const auto owner = summary_;
    const auto owner_hold = owner->weak_from_this().lock();
    const auto binding = summary_binding_generation_;
    const std::weak_ptr<int> life = renumber_lifetime_;
    if (const auto typed = std::dynamic_pointer_cast<Area>(value)) {
        if (force_new_id) owner->add(typed, true);
        else if (index == owner->areas().size()) owner->add(typed, false);
        else owner->insert(static_cast<std::ptrdiff_t>(index), typed);
    } else if (const auto typed = std::dynamic_pointer_cast<Battle>(value)) {
        if (force_new_id) owner->add(typed, true);
        else if (index == owner->battles().size()) owner->add(typed, false);
        else owner->insert(static_cast<std::ptrdiff_t>(index), typed);
    } else if (const auto typed = std::dynamic_pointer_cast<Package>(value)) {
        if (force_new_id) owner->add(typed, true);
        else if (index == owner->packages().size()) owner->add(typed, false);
        else owner->insert(static_cast<std::ptrdiff_t>(index), typed);
    }
    static_cast<void>(type);
    if (life.expired() || summary_ != owner || summary_binding_generation_ != binding) return;
    if (notify) notifyChanged(value.get());
}

AbstractAreaPtr AreaTable::areaAt(const std::size_t index) const {
    return index < rows_.size() ? rows_[index].area : AbstractAreaPtr{};
}

AbstractAreaPtr AreaTable::areaForId(const std::uint64_t id, const AreaTableType type) const {
    if (!summary_) return {};
    if (type == AreaTableType::Area) return std::static_pointer_cast<AbstractArea>(summary_->area(id));
    if (type == AreaTableType::Battle) return std::static_pointer_cast<AbstractArea>(summary_->battle(id));
    if (type == AreaTableType::Package) return std::static_pointer_cast<AbstractArea>(summary_->cwPackage(id));
    return {};
}

std::size_t AreaTable::useCount(const AbstractAreaPtr& value) const {
    if (context_ && context_->callbacks.use_count_from)
        return context_->callbacks.use_count_from(summary_, value.get());
    if (context_ && context_->callbacks.use_count) return context_->callbacks.use_count(value.get());
    return 0U;
}

std::string AreaTable::rowText(const Row& row, const std::size_t column) const {
    if (column == 0U) {
        return row.type == AreaTableType::Summary ? std::string{} : std::to_string(row.id);
    }
    if (column == 1U) return row.name;
    return std::to_string(row.use_count);
}

int AreaTable::typeRank(const AreaTableType type) noexcept {
    switch (type) {
    case AreaTableType::Summary: return 0;
    case AreaTableType::Area: return 1;
    case AreaTableType::Battle: return 2;
    case AreaTableType::Package: return 3;
    }
    return 4;
}

std::string AreaTable::lower(const std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](const unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return result;
}

int AreaTable::naturalCompare(const std::string_view first,
                              const std::string_view second) {
    // Dのareatable.dのcompNameは、共通の任意長の数
    // 字列比較を使う。同値の場合の数字の表記順（02 < 2）も含む。
    return ::cwxeditor::cwx::utils::incmp(first, second);
}

bool AreaTable::before(const Row& first, const Row& second) const {
    if (typeRank(first.type) != typeRank(second.type)) {
        return typeRank(first.type) < typeRank(second.type);
    }
    int comparison = 0;
    if (sort_column_ == AreaTableSortColumn::Id) {
        comparison = first.id < second.id ? -1 : first.id > second.id ? 1 : 0;
    } else if (sort_column_ == AreaTableSortColumn::UseCount) {
        comparison = first.use_count < second.use_count ? -1 : first.use_count > second.use_count ? 1 : 0;
    } else {
        comparison = prop_->var().logicalSort.value
                         ? naturalCompare(first.name, second.name)
                         : scene_flags::compare(first.name, second.name, false);
    }
    if (comparison == 0) comparison = first.id < second.id ? -1 : first.id > second.id ? 1 : 0;
    return sort_direction_ == AreaTableSortDirection::Ascending ? comparison < 0 : comparison > 0;
}

std::string AreaTable::uniqueName(std::string value, const AreaTableType type) const {
    if (value.empty()) {
        if (prop_) value = type == AreaTableType::Area ? prop_->requiredMsgs().areaNew.value
                         : type == AreaTableType::Battle ? prop_->requiredMsgs().battleNew.value
                                                         : prop_->requiredMsgs().packageNew.value;
        else value = typeName(type);
    }
    if (!summary_ || !prop_ || !prop_->var().incrementNewAreaName.value) return value;
    const auto available = [&](std::string candidate) {
        const auto full = lower(directory_mode_ && !directory_.empty()
            ? directory_ + '\\' + candidate : candidate);
        const auto used = [&](const auto& list) {
            return std::any_of(list.begin(), list.end(), [&](const auto& entry) {
                return entry && lower(entry->name()) == full;
            });
        };
        if (type == AreaTableType::Area) return !used(summary_->areas());
        if (type == AreaTableType::Battle) return !used(summary_->battles());
        return !used(summary_->packages());
    };
    return ::cwxeditor::cwx::utils::createNewName(std::move(value), available, true);
}

std::vector<AreaTable::SelectionKey> AreaTable::currentSelectionKeys() const {
    std::vector<SelectionKey> result;
    for (const auto index : selection_) {
        if (index >= rows_.size()) continue;
        result.push_back({rows_[index].type, rows_[index].id});
    }
    return result;
}

AreaTable::DomainSnapshot AreaTable::captureDomainSnapshot() const {
    DomainSnapshot result;
    result.summary = summary_;
    if (!summary_) return result;
    result.scenario_name = summary_->scenarioName();
    result.start_area = summary_->startArea();
    result.entries.reserve(summary_->areas().size() + summary_->battles().size() +
                           summary_->packages().size());
    const auto append = [&result](const auto& values, const AreaTableType type) {
        for (const auto& value : values) {
            if (!value) {
                result.entries.push_back({type, nullptr, 0U, {}, {}});
                continue;
            }
            result.entries.push_back({type, value.get(), value->id(), value->name(),
                                      value->dirName()});
        }
    };
    append(summary_->areas(), AreaTableType::Area);
    append(summary_->battles(), AreaTableType::Battle);
    append(summary_->packages(), AreaTableType::Package);
    return result;
}

bool AreaTable::domainMatches(const DomainSnapshot& value) const {
    if (summary_ != value.summary) return false;
    if (!summary_) return value.entries.empty() && value.scenario_name.empty() &&
                              value.start_area == 0U;
    if (summary_->scenarioName() != value.scenario_name ||
        summary_->startArea() != value.start_area)
        return false;
    std::size_t index{};
    const auto matches = [&value, &index](const auto& values, const AreaTableType type) {
        for (const auto& item : values) {
            if (index >= value.entries.size()) return false;
            const auto& expected = value.entries[index++];
            if (expected.type != type) return false;
            if (!item) {
                if (expected.area != nullptr || expected.id != 0U ||
                    !expected.name.empty() || !expected.directory.empty())
                    return false;
                continue;
            }
            if (expected.area != item.get() || expected.id != item->id() ||
                expected.name != item->name() || expected.directory != item->dirName())
                return false;
        }
        return true;
    };
    return matches(summary_->areas(), AreaTableType::Area) &&
           matches(summary_->battles(), AreaTableType::Battle) &&
           matches(summary_->packages(), AreaTableType::Package) &&
           index == value.entries.size();
}

bool AreaTable::hasSelectionType(const AreaTableType type) const {
    return std::any_of(selection_.begin(), selection_.end(), [&](const auto index) {
        return index < rows_.size() && rows_[index].type == type;
    });
}

bool AreaTable::isSelected(const AbstractArea* value) const {
    return std::any_of(selection_.begin(), selection_.end(), [&](const auto index) {
        return index < rows_.size() && rows_[index].area.get() == value;
    });
}

bool AreaTable::canChangeVH() const noexcept {
    return directory_mode_ && split_ && !disposed_;
}

void AreaTable::changeVHSide() {
    if (!canChangeVH()) return;
    split_->orientation = split_->orientation == lib::Orientation::Horizontal
                              ? lib::Orientation::Vertical
                              : lib::Orientation::Horizontal;
    if (read_only_) prop_->var().importAreaSashV.value =
        split_->orientation == lib::Orientation::Vertical;
    else prop_->var().areaSashV.value =
        split_->orientation == lib::Orientation::Vertical;
}

bool AreaTable::canCreateDir() const noexcept {
    return directory_mode_ && !read_only_ && summary_ && !disposed_;
}

void AreaTable::createDir(std::string name) {
    if (!canCreateDir()) return;
    const std::weak_ptr<int> life = renumber_lifetime_;
    const auto source_summary = summary_;
    const auto search = inc_search_;
    // Dは新しいフォルダーを作成・編集する前に、絞り込みを解除する。
    if (search) search->close();
    if (life.expired() || summary_ != source_summary || inc_search_ != search ||
        !canCreateDir()) return;
    if (name.empty()) {
        name = trim(prop_->requiredMsgs().areaDirNew.value).empty()
                   ? std::string("New Folder")
                   : trim(prop_->requiredMsgs().areaDirNew.value);
    }
    name = trim(name);
    if (name.empty() || name.find_first_of("\\/") != std::string::npos) return;
    const auto parent = findDirTree(directory_);
    if (!parent) return;
    name = uniqueDirName(std::move(name), [&](const std::string_view candidate) {
        return std::none_of(parent->subDirs.begin(), parent->subDirs.end(),
            [&](const auto& child) {
                return child && lower(child->name) == lower(candidate);
            });
    });
    if (context_ && context_->callbacks.create_directory &&
        !context_->callbacks.create_directory(name)) return;
    const auto undo_selection_before = captureUndoSelection();
    const auto parent_path = directory_;
    const auto path = parent_path.empty() ? name : parent_path + '\\' + name;
    const auto before_tree = dirs_ ? dirs_->dup() : std::shared_ptr<DirTree>{};
    putDir(path);
    if (tree_) tree_->selection.clear();
    directory_ = path;
    const auto after_tree = dirs_ ? dirs_->dup() : std::shared_ptr<DirTree>{};
    const auto restore = [this](const std::shared_ptr<DirTree>& tree,
                                const std::string& directory) {
        dirs_ = tree ? tree->dup() : std::shared_ptr<DirTree>{};
        directory_ = directory;
        if (tree_) tree_->selection.clear();
        refresh();
    };
    pushUndo(
        [restore, before_tree, parent_path] { restore(before_tree, parent_path); },
        [restore, after_tree, path] { restore(after_tree, path); }, undo_selection_before);
    refresh();
    beginCreatedDirEdit(path);
}

void AreaTable::edit() {
    if (!canEdit()) return;
    if (summarySelected()) {
        editSummary(panel_);
        return;
    }
    const auto row = selection_.front();
    if (row >= rows_.size()) return;
    if (rows_[row].type == AreaTableType::Summary) editSummary(panel_);
    else if (rows_[row].area) openAreaScene(rows_[row].area.get(), true, false);
}

bool AreaTable::canEdit() const noexcept {
    // Dのプロパティメニューの判定はTableの選択を使う。TreeのEnter
    // は別のeditSummary経路でSummaryを開く。
    return summary_ && !selection_.empty() && selection_.front() < rows_.size();
}

bool AreaTable::canReNumbering() const noexcept {
    return !read_only_ && summary_ && !selection_.empty() &&
           std::any_of(selection_.begin(), selection_.end(), [&](const auto index) {
               return index < rows_.size() && rows_[index].type != AreaTableType::Summary;
           });
}

void AreaTable::reNumberingAll() {
    if (read_only_ || !summary_) return;
    const auto areas = summary_->areas();
    const auto battles = summary_->battles();
    const auto packages = summary_->packages();
    std::vector<AbstractAreaPtr> values;
    for (const auto& value : areas) values.push_back(value);
    for (const auto& value : battles) values.push_back(value);
    for (const auto& value : packages) values.push_back(value);
    std::vector<std::uint64_t> old_ids;
    std::vector<std::uint64_t> new_ids;
    old_ids.reserve(values.size());
    new_ids.reserve(values.size());
    std::uint64_t area_id = 1U;
    std::uint64_t battle_id = 1U;
    std::uint64_t package_id = 1U;
    for (const auto& value : values) {
        old_ids.push_back(value->id());
        if (std::dynamic_pointer_cast<Area>(value)) new_ids.push_back(area_id++);
        else if (std::dynamic_pointer_cast<Battle>(value)) new_ids.push_back(battle_id++);
        else new_ids.push_back(package_id++);
    }
    if (old_ids == new_ids) return;
    pushUndo([this, values, old_ids] { applyIdsCollisionSafe(values, old_ids, 0U); refresh(); },
              [this, values, new_ids] { applyIdsCollisionSafe(values, new_ids, 0U); refresh(); });
    applyIdsCollisionSafe(values, new_ids, 0U);
    refresh();
}

void AreaTable::reNumberingArea(const int index, const std::uint64_t new_id) {
    renumberList(AreaTableType::Area, new_id, static_cast<std::size_t>(std::max(index, 0)));
}

void AreaTable::reNumberingBattle(const int index, const std::uint64_t new_id) {
    renumberList(AreaTableType::Battle, new_id, static_cast<std::size_t>(std::max(index, 0)));
}

void AreaTable::reNumberingPackage(const int index, const std::uint64_t new_id) {
    renumberList(AreaTableType::Package, new_id, static_cast<std::size_t>(std::max(index, 0)));
}

void AreaTable::reNumbering() {
    if (!canReNumbering() || renumber_dialog_) return;
    const auto [id, type] = getSelectionInfo();
    const auto value = getSelectionArea();
    if (!value || !id) return;
    // Dは最初の新IDを問い合わせ、選択位置から番号を付け直
    // す。取消時は既存の衝突を避けるモデル操作を呼ばない。
    const auto ask = [this, &value](const auto& values, auto apply) {
        const auto found = std::find_if(values.begin(), values.end(),
            [&value](const auto& item) { return item.get() == value.get(); });
        if (found == values.end()) return;
        const auto index = static_cast<std::size_t>(found - values.begin());
        const auto minimum = index ? values[index - 1U]->id() + 1U : 1U;
        if (minimum > prop_->looks().idMax()) return;
        using Object = std::remove_reference_t<decltype(**found)>;
        auto dialog = std::make_shared<smalldialogs::ReNumDialog<Object>>(
            *prop_, context_->dialog_owner, summary_, **found, minimum, context_->services);
        renumber_dialog_ = dialog;
        const std::weak_ptr<int> life = renumber_lifetime_;
        const auto source = summary_;
        const auto selected = *found;
        // このダイアログの保護には、独立した項目のコピーが必要。モデル取得
        // はD形式の共有スライスを返し、表示中にその項目が変わり得るため。
        std::vector<typename std::remove_cvref_t<decltype(values)>::value_type> snapshot;
        for (const auto& item : values) snapshot.push_back(item);
        const auto unchanged = [this, snapshot = std::move(snapshot)] {
            if constexpr (std::is_same_v<Object, Area>) return std::ranges::equal(summary_->areas(), snapshot);
            else if constexpr (std::is_same_v<Object, Battle>) return std::ranges::equal(summary_->battles(), snapshot);
            else return std::ranges::equal(summary_->packages(), snapshot);
        };
        const bool presented = smalldialogs::presentReNumbering<Object>(dialog,
            [this, life, source, selected, index, apply, unchanged](bool accepted, std::uint64_t next) {
                if (life.expired()) return;
                auto keep = std::exchange(renumber_dialog_, {});
                if (accepted && !disposed_ && summary_ == source &&
                    unchanged() && getSelectionArea().get() == selected.get())
                    apply(static_cast<int>(index), next);
                if (context_->callbacks.refresh_toolbar) context_->callbacks.refresh_toolbar();
            });
        if (!presented) renumber_dialog_.reset();
    };
    if (type == AreaTableType::Area)
        ask(summary_->areas(), [this](int index, std::uint64_t next) { reNumberingArea(index, next); });
    else if (type == AreaTableType::Battle)
        ask(summary_->battles(), [this](int index, std::uint64_t next) { reNumberingBattle(index, next); });
    else if (type == AreaTableType::Package)
        ask(summary_->packages(), [this](int index, std::uint64_t next) { reNumberingPackage(index, next); });
    if (context_->callbacks.refresh_toolbar) context_->callbacks.refresh_toolbar();
}

void AreaTable::editSummary(const std::shared_ptr<lib::Container>&) {
    if (!summary_ || !context_ || !prop_) return;
    if (read_only_) {
        // Dは読み取り専用のプロパティ画面を取り込み元へ接続する。MainWindow
        // で現在編集可能なシナリオへは接続しない。
        if (readonly_summary_dialog_ && readonly_summary_dialog_->dialog() &&
            readonly_summary_dialog_->dialog()->visible()) {
            readonly_summary_dialog_->active();
            return;
        }
        auto* const common = context_->editor_commons &&
                !context_->editor_commons_lifetime.expired()
            ? context_->editor_commons : nullptr;
        readonly_summary_dialog_ = std::make_shared<summarydialog::SummaryDialog>(
            *prop_, context_->dialog_owner, summary_, true, context_->services,
            common);
        (void)readonly_summary_dialog_->open();
        return;
    }
    if (context_->callbacks.edit_summary) context_->callbacks.edit_summary();
}

void AreaTable::summary(Summary* value) {
    if (summary_ == value) return;
    ++summary_binding_generation_;
    if (inc_search_) inc_search_->close();
    if (renumber_dialog_) renumber_dialog_->forceCancel();
    renumber_dialog_.reset();
    if (readonly_summary_dialog_) readonly_summary_dialog_->forceCancel();
    readonly_summary_dialog_.reset();
    closeCommentDialogs();
    summary_ = value;
    // Dのsummary設定関数は、ディレクトリのキャ
    // ッシュ全体を捨ててから新シナリオで構築する。残
    // すと旧シナリオのフォルダーが次の表に混ざり、行
    // 選択の解除後も不正なTree選択を復元し得る。
    dirs_.reset();
    directory_.clear();
    selection_.clear();
    saved_selection_.clear();
    // 一覧表示のSummary行は一つの接続だけに属する。更
    // 新中の再接続も含め、置換後のシナリオへ持ち越さない。
    saved_summary_selected_ = false;
    last_focus_tree_ = false;
    if (undo_) undo_->reset();
    refresh();
}

void AreaTable::cancelEdit() {
    const auto source = summary_;
    const auto binding = summary_binding_generation_;
    const auto context = context_;
    const auto gui = context ? context->services.gui : nullptr;
    const auto table = table_;
    const auto tree = tree_;
    const std::weak_ptr<int> life = renumber_lifetime_;
    const auto current = [this, life, source, binding, context, gui, table, tree] {
        return !life.expired() && !disposed_ && summary_ == source &&
               summary_binding_generation_ == binding && context_ == context &&
               table_ == table && tree_ == tree &&
               (!context_ || context_->services.gui == gui);
    };
    if (!current() || !gui) return;
    if (table && gui->programmatic_inline_edit_active(table)) {
        (void)gui->finish_programmatic_inline_edit(table, false);
        if (!current()) return;
    }
    if (tree && gui->programmatic_inline_edit_active(tree)) {
        (void)gui->finish_programmatic_inline_edit(tree, false);
    }
}

void AreaTable::createArea() {
    if (read_only_ || !summary_) return;
    auto value = std::make_shared<Area>(summary_->newAreaId(),
                                        uniqueName(prop_->requiredMsgs().areaNew.value,
                                                    AreaTableType::Area));
    if (directory_mode_) value->dirName(directory_);
    if (context_ && context_->skin) {
        const auto defaults = ::cwxeditor::cwx::editor::gui::dwt::dprops::arraySnapshot(
            prop_->bgImagesDefault(context_->skin->type()));
        for (auto& image : skin::createBgImages(*context_->skin, defaults))
            if (image) value->append(std::move(image));
    }
    auto enter = std::make_shared<event::EventTree>(prop_->requiredMsgs().enterTree.value);
    enter->enter(true);
    value->add(std::move(enter));
    addAreas({value});
    beginCreatedNameEdit(AreaTableType::Area, value->id());
}

void AreaTable::createBattle() {
    if (read_only_ || !summary_) return;
    const auto music = context_ && context_->skin
                           ? context_->skin->defBattle(summary_->scenarioPath(),
                                                       summary_->dataVersion())
                           : std::string{};
    auto value = std::make_shared<Battle>(summary_->newBattleId(),
                                          uniqueName(prop_->requiredMsgs().battleNew.value,
                                                      AreaTableType::Battle), music);
    if (directory_mode_) value->dirName(directory_);
    addAreas({value});
    beginCreatedNameEdit(AreaTableType::Battle, value->id());
}

void AreaTable::createPackage() {
    static_cast<void>(createPackage(std::shared_ptr<event::EventTree>{},
                                    prop_->requiredMsgs().packageNew.value));
}

std::uint64_t AreaTable::createPackage(
    const std::shared_ptr<event::EventTree>& base_tree, std::string name) {
    if (read_only_ || !summary_) return 0U;
    auto value = std::make_shared<Package>(summary_->newPackageId(),
                                           uniqueName(std::move(name),
                                                      AreaTableType::Package));
    if (directory_mode_) value->dirName(directory_);
    if (base_tree) {
        auto tree = base_tree->dup();
        tree->name(prop_->requiredMsgs().packageTree.value);
        value->add(std::move(tree));
    } else {
        value->add(std::make_shared<event::EventTree>(
            prop_->requiredMsgs().packageTree.value));
    }
    const auto owner = summary_;
    const auto binding = summary_binding_generation_;
    const std::weak_ptr<int> life = renumber_lifetime_;
    addAreas({value});
    if (!life.expired() && !disposed_ && summary_ == owner &&
        summary_binding_generation_ == binding)
        beginCreatedNameEdit(AreaTableType::Package, value->id());
    return value->id();
}

std::uint64_t AreaTable::createPackage(
    const std::shared_ptr<event::Content>& base_start, std::string name) {
    // 切り離された入力も含め、一時イベントを作る前に拒否する。
    if (read_only_ || !summary_) return 0U;
    if (!base_start) return createPackage(std::shared_ptr<event::EventTree>{}, std::move(name));
    return createPackage(std::make_shared<event::EventTree>(base_start), std::move(name));
}

lib::WidgetPtr AreaTable::summarySelectionControl() const noexcept {
    return directory_mode_ ? std::static_pointer_cast<lib::Widget>(tree_)
                           : std::static_pointer_cast<lib::Widget>(table_);
}
bool AreaTable::summarySelected() const noexcept {
    if (!summary_ || !showSummary()) return false;
    if (directory_mode_) {
        if (!tree_) return false;
        for (const auto id : tree_->selection)
            for (const auto& node : tree_->nodes)
                if (node.id == id && node.value == "__summary__") return true;
        return false;
    }
    return std::any_of(selection_.begin(), selection_.end(), [this](const auto index) {
        return index < rows_.size() && rows_[index].type == AreaTableType::Summary;
    });
}
void AreaTable::selectSummary() {
    if (!summary_ || !showSummary()) return;
    if (directory_mode_) {
        if (!tree_) return;
        const auto found = std::find_if(tree_->nodes.begin(), tree_->nodes.end(),
            [](const lib::TreeNode& node) { return node.value == "__summary__"; });
        if (found == tree_->nodes.end()) return;
        (void)lib::select_tree_items(*tree_, {found->id}, true);
        last_focus_tree_ = true;
        refreshStatusLine();
        return;
    }
    const auto it = std::find_if(rows_.begin(), rows_.end(), [](const Row& row) {
        return row.type == AreaTableType::Summary;
    });
    selection_.clear();
    if (it != rows_.end()) selection_.push_back(static_cast<std::size_t>(it - rows_.begin()));
    if (table_) (void)lib::select_table_rows(*table_, selection_, true);
    last_focus_tree_ = false;
    refreshStatusLine();
}

bool AreaTable::select(AbstractArea* value) {
    if (!value || disposed_) return false;
    const std::weak_ptr<int> life = renumber_lifetime_;
    const auto owner = summary_;
    const auto binding = summary_binding_generation_;
    // Dのselect(AbstractArea)は、先に対
    // 象のディレクトリを選び、その結果の絞り込み表示から行を
    // 解決する。省くと、現在のディレクトリ外の項目を開く・
    // 選ぶ際に、表とツリーが別のモデルを示したままになる。
    if (directory_mode_ && (directory_ != value->dirName() || summarySelected())) {
        // 資源への明示的な移動では、保持していたSummary選択を置き換える。
        if (tree_) (void)lib::select_tree_items(*tree_, {}, false, false);
        directory_ = value->dirName();
        refresh();
        if (life.expired()) return false;
        if (disposed_ || summary_ != owner || summary_binding_generation_ != binding) return false;
    }
    const auto it = std::find_if(rows_.begin(), rows_.end(),
        [&](const Row& row) { return row.area.get() == value; });
    if (it == rows_.end()) return false;
    const auto index = static_cast<std::size_t>(it - rows_.begin());
    selection_ = {index};
    const auto table = table_;
    const auto revision = table_render_revision_;
    const auto order = table ? table->row_order_revision : 0U;
    if (table) {
        (void)lib::select_table_rows(*table, selection_, true);
        if (life.expired()) return false;
        if (disposed_ || summary_ != owner || summary_binding_generation_ != binding ||
            table_ != table || table_render_revision_ != revision ||
            table->row_order_revision != order || index >= rows_.size() ||
            rows_[index].area.get() != value ||
            selection_ != std::vector<std::size_t>{index}) return false;
        // Dのselect(AbstractArea)は、行を選んでからshowSelection
        // を呼ぶ。資源への移動では、表示領域外の結果も見える位置へ移す。
        (void)lib::reveal_table_row(*table, index);
    }
    last_focus_tree_ = false;
    saveSelections();
    refreshStatusLine();
    return true;
}

bool AreaTable::canOpenAreaScene() const noexcept {
    // DWTのWindows版は、フォーカスを持つ選択行を
    // 返す。フォーカス行が選択外なら、最初の選択行を返す。
    const auto first = table_ ? lib::table_selection_index(*table_)
        : (selection_.empty() ? std::optional<std::size_t>{}
                              : std::optional<std::size_t>{
                                    *std::min_element(selection_.begin(), selection_.end())});
    if (!first) return false;
    const auto summary_offset = !directory_mode_ && showSummary() ? 1U : 0U;
    const auto has_openable_resource = std::any_of(
        selection_.begin(), selection_.end(), [&](const auto index) {
            return index < rows_.size() &&
                   (rows_[index].type == AreaTableType::Area ||
                    rows_[index].type == AreaTableType::Battle ||
                    rows_[index].type == AreaTableType::Package);
        });
    return *first < rows_.size() && *first >= summary_offset && has_openable_resource;
}

bool AreaTable::canOpenAreaEvent() const noexcept {
    // Dは両コマンドで、同じ最初の選択行の判定を使う。
    return canOpenAreaScene();
}

void AreaTable::openAreaScene(const bool shell_activate, const bool can_duplicate) {
    const std::weak_ptr<int> life = renumber_lifetime_;
    const auto source = summary_;
    const auto binding = summary_binding_generation_;
    // Dはキーボードフォーカスがツリーにあっても、表の選択行を開く。
    // フォルダー全体の選択は、他のディレクトリ操作で引き続き使える。
    std::vector<AbstractAreaPtr> values;
    for (const auto index : selection_)
        if (index < rows_.size() && rows_[index].area) values.push_back(rows_[index].area);
    const auto stop = context_ ? context_->callbacks.stop_open : std::function<bool()>{};
    for (const auto& value : values) {
        if (life.expired()) return;
        if (summary_ != source || summary_binding_generation_ != binding || disposed_) return;
        if (stop && stop()) return;
        if (life.expired()) return;
        if (summary_ != source || summary_binding_generation_ != binding || disposed_) return;
        openAreaScene(value.get(), shell_activate, can_duplicate);
    }
}

void AreaTable::openAreaScene(const int x, const int y, const bool shell_activate,
                              const bool can_duplicate) {
    // Dの中央クリックは、クリック前の選択行ではなく、マウ
    // ス下の行を解決して開く。座標はSWTのMouseEvent.x/y
    // と同じくTableView内の座標。
    static_cast<void>(x);
    if (table_) {
        const float row_height = table_->row_height > 0.0F ? table_->row_height : 20.0F;
        const float header = table_->header_height > 0.0F ? table_->header_height
                                                           : row_height;
        if (y >= static_cast<int>(header)) {
            const auto row = static_cast<std::size_t>(
                static_cast<float>(y - static_cast<int>(header)) / row_height);
            if (row < rows_.size() && rows_[row].area) {
                selectIndex(row, false);
                openAreaScene(rows_[row].area.get(), shell_activate, can_duplicate);
                return;
            }
        }
    }
    openAreaScene(shell_activate, can_duplicate);
}

void AreaTable::openAreaEvent(const int x, const int y, const bool shell_activate,
                              const bool can_duplicate) {
    static_cast<void>(x);
    if (table_) {
        const float row_height = table_->row_height > 0.0F ? table_->row_height : 20.0F;
        const float header = table_->header_height > 0.0F ? table_->header_height
                                                           : row_height;
        if (y >= static_cast<int>(header)) {
            const auto row = static_cast<std::size_t>(
                static_cast<float>(y - static_cast<int>(header)) / row_height);
            if (row < rows_.size() && rows_[row].area) {
                selectIndex(row, false);
                openAreaEvent(rows_[row].area.get(), shell_activate, can_duplicate);
                return;
            }
        }
    }
    openAreaEvent(shell_activate, can_duplicate);
}

void AreaTable::openAreaEvent(const bool shell_activate, const bool can_duplicate) {
    const std::weak_ptr<int> life = renumber_lifetime_;
    const auto source = summary_;
    const auto binding = summary_binding_generation_;
    // Dはキーボードフォーカスがツリーにあっても、表の選択行を開く。
    // フォルダー全体の選択は、他のディレクトリ操作で引き続き使える。
    std::vector<AbstractAreaPtr> values;
    for (const auto index : selection_)
        if (index < rows_.size() && rows_[index].area) values.push_back(rows_[index].area);
    const auto stop = context_ ? context_->callbacks.stop_open : std::function<bool()>{};
    for (const auto& value : values) {
        if (life.expired()) return;
        if (summary_ != source || summary_binding_generation_ != binding || disposed_) return;
        if (stop && stop()) return;
        if (life.expired()) return;
        if (summary_ != source || summary_binding_generation_ != binding || disposed_) return;
        openAreaEvent(value.get(), shell_activate, can_duplicate);
    }
}

void AreaTable::openAreaScene(AbstractArea* value, const bool shell_activate,
                              const bool can_duplicate) {
    if (!value || !context_) return;
    if (const auto package = dynamic_cast<Package*>(value)) {
        openPackage(package->id(), shell_activate, can_duplicate);
        return;
    }
    if (context_->callbacks.open_scene_from)
        context_->callbacks.open_scene_from(summary_, value, shell_activate,
                                             can_duplicate);
    else if (context_->callbacks.open_scene)
        context_->callbacks.open_scene(value, shell_activate, can_duplicate);
}

void AreaTable::openAreaEvent(AbstractArea* value, const bool shell_activate,
                              const bool can_duplicate) {
    if (!value || !context_) return;
    if (const auto package = dynamic_cast<Package*>(value)) {
        openPackage(package->id(), shell_activate, can_duplicate);
        return;
    }
    if (context_->callbacks.open_event_from)
        context_->callbacks.open_event_from(summary_, value, shell_activate,
                                             can_duplicate);
    else if (context_->callbacks.open_event)
        context_->callbacks.open_event(value, shell_activate, can_duplicate);
}

void AreaTable::openAreaScene(const std::uint64_t id, const bool shell_activate,
                              const bool can_duplicate) {
    if (const auto value = areaForId(id, AreaTableType::Area)) openAreaScene(value.get(), shell_activate, can_duplicate);
    else if (const auto battle_value = areaForId(id, AreaTableType::Battle)) openAreaScene(battle_value.get(), shell_activate, can_duplicate);
}

void AreaTable::openAreaEvent(const std::uint64_t id, const bool shell_activate,
                              const bool can_duplicate) {
    if (const auto value = areaForId(id, AreaTableType::Area)) openAreaEvent(value.get(), shell_activate, can_duplicate);
}

void AreaTable::openBattleScene(const std::uint64_t id, const bool shell_activate,
                                const bool can_duplicate) {
    if (const auto value = areaForId(id, AreaTableType::Battle)) openAreaScene(value.get(), shell_activate, can_duplicate);
}

void AreaTable::openBattleEvent(const std::uint64_t id, const bool shell_activate,
                                const bool can_duplicate) {
    if (const auto value = areaForId(id, AreaTableType::Battle)) openAreaEvent(value.get(), shell_activate, can_duplicate);
}

void AreaTable::openPackage(const std::uint64_t id, const bool shell_activate,
                            const bool can_duplicate) {
    if (!context_) return;
    if (const auto value = areaForId(id, AreaTableType::Package)) {
        if (context_->callbacks.open_package_from)
            context_->callbacks.open_package_from(summary_, value.get(),
                                                   shell_activate, can_duplicate);
        else if (context_->callbacks.open_package)
            context_->callbacks.open_package(value.get(), shell_activate, can_duplicate);
        else if (context_->callbacks.open_event_from)
            context_->callbacks.open_event_from(summary_, value.get(),
                                                 shell_activate, can_duplicate);
        else if (context_->callbacks.open_event) context_->callbacks.open_event(value.get(), shell_activate, can_duplicate);
    }
}

bool AreaTable::canUp() const { return static_cast<bool>(movementNeighbor(-1)); }

bool AreaTable::canDown() const { return static_cast<bool>(movementNeighbor(1)); }

void AreaTable::up() {
    if (!canUp()) return;
    const auto source = summary_;
    const auto binding = summary_binding_generation_;
    const auto context = context_;
    const auto gui = context ? context->services.gui : nullptr;
    const auto table = table_;
    const auto tree = tree_;
    const std::weak_ptr<int> life = renumber_lifetime_;
    cancelEdit();
    if (life.expired() || disposed_ || summary_ != source ||
        summary_binding_generation_ != binding || context_ != context ||
        table_ != table || tree_ != tree ||
        (context_ && context_->services.gui != gui)) return;
    moveSelected(-1);
}
void AreaTable::down() {
    if (!canDown()) return;
    const auto source = summary_;
    const auto binding = summary_binding_generation_;
    const auto context = context_;
    const auto gui = context ? context->services.gui : nullptr;
    const auto table = table_;
    const auto tree = tree_;
    const std::weak_ptr<int> life = renumber_lifetime_;
    cancelEdit();
    if (life.expired() || disposed_ || summary_ != source ||
        summary_binding_generation_ != binding || context_ != context ||
        table_ != table || tree_ != tree ||
        (context_ && context_->services.gui != gui)) return;
    moveSelected(1);
}

bool AreaTable::canSetStartArea() const {
    const auto value = getSelectionArea();
    return !read_only_ && summary_ && selection_.size() == 1U &&
           hasSelectionType(AreaTableType::Area) && value &&
           summary_->startArea() != value->id();
}

void AreaTable::setStartArea() {
    if (!canSetStartArea() || !summary_) return;
    const auto value = getSelectionArea();
    if (!value) return;
    const auto old = summary_->startArea();
    const auto current = value->id();
    if (old == current) return;
    pushUndo([this, old] { if (summary_) summary_->startArea(old); refresh(); },
             [this, current] { if (summary_) summary_->startArea(current); refresh(); });
    summary_->startArea(current);
    notifySummaryChanged();
    refresh();
}

bool AreaTable::qAreasMaterialCopy(const xml::XNode& node,
                                   const std::vector<AbstractAreaPtr>& values) {
    if (context_ && context_->callbacks.confirm_material_copy) {
        // 確認処理が同期的にホストとコールバック所有元を終了させる場合がある。
        const auto confirm = context_->callbacks.confirm_material_copy;
        return confirm(node, values);
    }
    if (!context_ || !summary_ || !prop_) return true;
    const auto source = node.attr("scenarioPath", false);
    if (source.empty() || utils::cfnmatch(utils::nabs(std::string_view{source}),
                                         utils::nabs(std::string_view{summary_->scenarioPath()}))) return true;
    const auto owner = summary_;
    const auto owner_hold = owner->weak_from_this().lock();
    const auto binding = summary_binding_generation_;
    const std::weak_ptr<int> life = renumber_lifetime_;
    const auto current = [this, life, owner, binding] {
        return !life.expired() && summary_ == owner &&
               summary_binding_generation_ == binding && !read_only_;
    };
    // AbstractAreaのカウンターフックは仮想関数ではない。Dの全参照関係の
    // 巡回に合わせ、Areaでは背景・カード、Battleでは音楽・カードも含める。
    const auto bind = [](const AbstractAreaPtr& value,
                         const usecounter::CounterOwner& counter) {
        if (auto area = std::dynamic_pointer_cast<Area>(value)) area->setUseCounter(counter);
        else if (auto battle = std::dynamic_pointer_cast<Battle>(value)) battle->setUseCounter(counter);
        else if (value) value->setUseCounter(counter);
    };
    struct DetachCounters {
        std::vector<AbstractAreaPtr> values;
        ~DetachCounters() {
            for (const auto& value : values) {
                if (auto area = std::dynamic_pointer_cast<Area>(value)) area->removeUseCounter();
                else if (auto battle = std::dynamic_pointer_cast<Battle>(value)) battle->removeUseCounter();
                else if (value) value->removeUseCounter();
            }
        }
    } detach{values};
    const auto counter = std::make_shared<usecounter::UseCounter>();
    for (const auto& value : values) bind(value, counter);
    dutils::EditorContext context;
    context.prop = prop_;
    context.skin = context_->skin;
    context.summary = owner;
    context.services = context_->services;
    const auto refresh_paths = context_->callbacks.ref_paths;
    const auto material = context.skin ? context.skin->materialPath() : std::string{"Material"};
    const auto model_props = &prop_->parent();
    const auto source_name = node.attr("scenarioName", false);
    const auto source_author = node.attr("scenarioAuthor", false);
    bool copied{};
    const bool accepted = dutils::qGraphMaterialCopy(context, counter, owner->scenarioPath(), source,
        copied, owner->legacy(),
        [model_props, source_name, source_author](const usecounter::PathUser& user) {
            return summary::pathUserToExportedImageName(*model_props, source_name, source_author, user);
        }, current);
    if (!accepted || !current()) return false;
    if (copied && refresh_paths) refresh_paths(material);
    return current();
}

bool AreaTable::canWriteComment() const noexcept {
    return !read_only_ && std::any_of(selection_.begin(), selection_.end(),
        [this](const std::size_t index) {
            return index < rows_.size() && static_cast<bool>(rows_[index].area);
        });
}

void AreaTable::writeComment() {
    const std::weak_ptr<int> life = renumber_lifetime_;
    const auto source = summary_;
    const auto binding = summary_binding_generation_;
    const auto current = [this, life, source, binding] {
        return !life.expired() && !disposed_ && summary_ == source &&
            summary_binding_generation_ == binding;
    };
    if (!current() || !canWriteComment()) return;
    // Dは選択行を読む前にTableTextEditを確定する。確定は共通GUI
    // 表示側が担当し、そのコールバックがこの表示を再構築・再接続し得る。
    if (context_ && context_->services.gui && table_)
        (void)context_->services.gui->finish_programmatic_inline_edit(table_, true);
    if (!current() || !canWriteComment()) return;
    std::vector<AbstractAreaPtr> values;
    for (const auto index : selection_)
        if (index < rows_.size() && rows_[index].area)
            values.push_back(rows_[index].area);
    if (context_ && context_->callbacks.make_comment_dialog) {
        for (const auto& value : values) {
            if (!current()) return;
            writeCommentImpl(value);
            if (!current()) return;
        }
    } else if (context_ && context_->callbacks.write_comment) {
        const auto callback = context_->callbacks.write_comment;
        if (!current()) return;
        callback();
    }
}

void AreaTable::refreshCommentDialog(const AbstractArea* value, const bool deleted,
                                     const bool titleOnly) {
    if (!value || disposed_) return;
    const auto found = comment_dialogs_.find(const_cast<AbstractArea*>(value));
    if (found == comment_dialogs_.end() || !found->second) return;
    const auto dialog = found->second;
    if (deleted || (!titleOnly && value->dirName() != directory_)) {
        dialog->forceCancel();
        return;
    }
    const std::weak_ptr<int> life = renumber_lifetime_;
    const auto source = summary_;
    const auto binding = summary_binding_generation_;
    const auto title = context_ ? context_->callbacks.comment_title
                               : std::function<std::string(const AbstractAreaPtr&)>{};
    if (!title) return;
    const auto owner = std::const_pointer_cast<AbstractArea>(
        std::dynamic_pointer_cast<const AbstractArea>(value->shared_from_this()));
    if (!owner) return;
    const auto next = title(owner);
    if (life.expired() || summary_ != source || summary_binding_generation_ != binding) return;
    const auto current = comment_dialogs_.find(owner.get());
    if (current != comment_dialogs_.end() && current->second == dialog) dialog->title = next;
}

void AreaTable::refreshCommentTitles() {
    const std::weak_ptr<int> life = renumber_lifetime_;
    const auto owner = summary_;
    const auto binding = summary_binding_generation_;
    std::vector<AbstractAreaPtr> owners;
    for (const auto& [value, dialog] : comment_dialogs_)
        if (value && dialog)
            owners.push_back(std::dynamic_pointer_cast<AbstractArea>(value->shared_from_this()));
    for (const auto& value : owners) {
        if (life.expired()) return;
        refreshCommentDialog(value.get(), false, true);
        if (life.expired() || disposed_ || summary_ != owner ||
            summary_binding_generation_ != binding) return;
    }
}

void AreaTable::writeCommentImpl(const AbstractAreaPtr& value) {
    if (!value || read_only_ || !context_ || !context_->callbacks.make_comment_dialog) return;
    const auto key = value.get();
    if (const auto found = comment_dialogs_.find(key); found != comment_dialogs_.end()) {
        const auto dialog = found->second;
        if (dialog) dialog->active();
        return;
    }
    const std::weak_ptr<int> life = renumber_lifetime_;
    const auto source = summary_;
    const auto binding = summary_binding_generation_;
    const auto current = [this, life, source, binding] {
        return !life.expired() && !disposed_ && summary_ == source &&
            summary_binding_generation_ == binding;
    };
    const auto factory = context_->callbacks.make_comment_dialog;
    const auto title = context_->callbacks.comment_title;
    auto dialog = factory(value, value->comment());
    if (!current() || !dialog) return;
    if (title) {
        const auto text = title(value);
        if (!current()) return;
        dialog->title = text;
    }
    comment_dialogs_[key] = dialog;
    const std::weak_ptr<comment::CommentDialog> weakDialog = dialog;
    dialog->applied = [this, value, current, weakDialog](const std::string_view next) {
        if (!current()) return;
        const auto dialog = weakDialog.lock();
        const auto found = comment_dialogs_.find(value.get());
        if (!dialog || found == comment_dialogs_.end() || found->second != dialog ||
            value->comment() == next) return;
        const auto before = value->comment();
        const std::string after(next);
        const auto apply = [this, value, current](const std::string& text) {
            if (!current()) return;
            value->comment(text);
            notifyChanged(value.get());
            if (current()) refresh();
        };
        pushUndo([apply, before] { apply(before); }, [apply, after] { apply(after); });
        if (current()) apply(after);
    };
    dialog->closed = [this, key, life, weakDialog] {
        if (life.expired()) return;
        const auto found = comment_dialogs_.find(key);
        if (found != comment_dialogs_.end() && found->second == weakDialog.lock())
            comment_dialogs_.erase(found);
    };
    (void)dialog->open();
}

void AreaTable::closeCommentDialogs() {
    auto dialogs = std::move(comment_dialogs_);
    comment_dialogs_.clear();
    for (const auto& [_, dialog] : dialogs) {
        if (dialog) {
            dialog->applied = {};
            dialog->forceCancel();
        }
    }
}

void AreaTable::copy(AreaTableSelectionEvent& event) {
    copy();
    event.handled = !clipboard_text_.empty();
}

std::string AreaTable::serializeClipboardSelection() const {
    if (!prop_ || !summary_) return {};
    const auto selected = selectedAreas();
    const auto folder = last_focus_tree_ && directory_mode_ && tree_ &&
                            !tree_->selection.empty() && !summarySelected()
                            ? findDirTree(directory_) : std::shared_ptr<DirTree>{};
    if (selected.empty() && !folder) return {};
    auto root = xml::XNode::create("CWXAreaClipboard");
    root.newAttr("format", "2");
    // これらの名前は選択ディレクトリからの相対名。Tree選択
    // は自身のルート名を保持し、表の選択は出力先から始める。こ
    // の属性がない旧format-2データは絶対名のまま扱う。
    root.newAttr("pathMode", directory_mode_ ? "relative" : "absolute");
    root.newAttr("sourceDirectory", directory_mode_ ? directory_ : std::string{});
    // DのSummary.areasToNodeは、素材転送用に入力元の属性情報を保持する。
    root.newAttr("scenarioPath", utils::nabs(std::string_view{summary_->scenarioPath()}));
    root.newAttr("scenarioName", summary_->scenarioName());
    root.newAttr("scenarioAuthor", summary_->author());
    const auto parent_path = folder ? folder->name : std::string{};
    const auto cut_path = directory_mode_ ? directory_ : std::string{};
    card::XMLOption option(prop_->sys(), summary_->dataVersion());
    for (const auto& value : selected)
        if (value) static_cast<void>(value->toNode(root, option, parent_path, cut_path));
    if (folder) {
        // 資源名だけでは空ディレクトリを表せない。選択ル
        // ートと空の子孫も含め、全ディレクトリを含める。
        const auto append = [&](const auto& self, const std::shared_ptr<DirTree>& dir,
                                const std::string& relative) -> void {
            root.newElement("TablePath", std::string_view(relative));
            for (const auto& child : dir->subDirs)
                if (child) self(self, child, relative.empty() ? child->name
                    : relative + '\\' + child->name);
        };
        append(append, folder, folder->name);
    }
    return root.text();
}

void AreaTable::copy() {
    if (!prop_ || !summary_) return;
    clipboard_text_ = serializeClipboardSelection();
    if (clipboard_text_.empty()) {
        if (context_) setAreaTableClipboardFallback(context_->services.clipboard, {});
        return;
    }
    if (context_)
        setAreaTableClipboardFallback(context_->services.clipboard, clipboard_text_);
    if (context_ && context_->services.clipboard) {
        try {
            xmlbytestransfer::ClipData data(*context_->services.clipboard);
            // DのXMLtoCBは、現在のxmlCopy設定に応じて
            // 通常のテキストまたは登録されたXML形式を公開する。
            xmlbytestransfer::XMLtoCB(prop_->var().xmlCopy.value, data, clipboard_text_);
        } catch (...) {
            // プラットフォームのクリップボードが利用できない、または非同期の
            // 内容が古い場合も、バックエンドごとの代替により、このEditor
            // の全AreaTableでコピー・貼り付けを使えるようにする。
        }
    }
}

void AreaTable::cut(AreaTableSelectionEvent& event) {
    if (!canDoT()) return;
    copy();
    del();
    event.handled = true;
}

void AreaTable::paste(AreaTableSelectionEvent& event) {
    event.handled = pasteClipboard();
}

void AreaTable::paste() {
    static_cast<void>(pasteClipboard());
}

bool AreaTable::pasteClipboard() {
    if (!canDoP()) return false;
    std::string text;
    const auto clipboard = context_ ? context_->services.clipboard : nullptr;
    if (clipboard) {
        try {
            auto external = xmlbytestransfer::CBtoXML(*clipboard);
            if (!isAreaTableClipboardPayload(external)) return false;
            text = std::move(external);
        } catch (...) {
            // クリップボードの権限・読み取りの失敗で、Editor内の最新
            // のArea/Package/Battleのコピーを捨てない。
        }
    }
    if (text.empty()) {
        auto shared = areaTableClipboardFallback(clipboard);
        if (isAreaTableClipboardPayload(shared)) text = std::move(shared);
    }
    if (text.empty() && isAreaTableClipboardPayload(clipboard_text_))
        text = clipboard_text_;
    return importClipboardPayload(text, directory_);
}

bool AreaTable::importClipboardPayload(const std::string& text,
                                       const std::string& destination,
                                       const std::optional<std::size_t> insertion) {
    if (disposed_ || read_only_ || !prop_ || !summary_ || text.empty()) return false;
    const auto owner = summary_;
    const auto owner_hold = owner->weak_from_this().lock();
    const auto binding = summary_binding_generation_;
    const std::weak_ptr<int> life = renumber_lifetime_;
    const auto current = [this, life, owner, binding] {
        return !life.expired() && summary_ == owner &&
               summary_binding_generation_ == binding && !read_only_;
    };
    std::vector<AbstractAreaPtr> values;
    std::vector<std::string> directories;
    xml::XNode root;
    try {
        root = xml::XNode::parse(text);
        const bool direct = root.name() == Area::XML_NAME ||
                            root.name() == Battle::XML_NAME || root.name() == Package::XML_NAME;
        if (!direct && root.name() != "Table" &&
            (root.name() != "CWXAreaClipboard" || root.attr<std::string>("format", false, {}) != "2"))
            return false;
        const system::XMLInfo version(prop_->sys(), summary_->dataVersion());
        const auto load = [&](xml::XNode child) {
            if (child.name() == Area::XML_NAME)
                values.push_back(Area::createFromNode(child, version));
            else if (child.name() == Battle::XML_NAME)
                values.push_back(Battle::createFromNode(child, version));
            else if (child.name() == Package::XML_NAME)
                values.push_back(Package::createFromNode(child, version));
            else if (child.name() == "TablePath" && !child.value().empty())
                directories.push_back(child.value());
        };
        if (direct) load(root);
        else for (auto child : root.element_children()) load(child);
    } catch (...) {
        return false;
    }
    if (values.empty() && directories.empty()) return false;
    // この表の経路は入力元のメタデータを直接使う。従来のcreateAreasFromNode
    // のsameSummaryフラグは再利用しない。D由来の判定は出
    // 力先パスを自身と比較するため、別の入力を同一と誤判定し得る。
    const auto source_path = root.attr("scenarioPath", false);
    const auto destination_path = owner->scenarioPath();
    const bool same_summary = !source_path.empty() && !destination_path.empty() &&
        utils::cfnmatch(utils::nabs(std::string_view{destination_path}),
                        utils::nabs(std::string_view{source_path}));
    if (!qAreasMaterialCopy(root, values) || !current()) return false;
    // DのTable出力は、pathMode属性なしで相対フォルダーを保持する。
    if (directory_mode_ && (root.name() == "Table" ||
        root.attr<std::string>("pathMode", false, {}) == "relative")) {
        std::unordered_set<std::string> existing_roots;
        if (const auto parent = findDirTree(destination)) {
            for (const auto& child : parent->subDirs)
                if (child) existing_roots.insert(lower(child->name));
        }
        // 論理的な入力元ルートごとに一度だけ名前を変え、大文字小文字の
        // 表記が違っても、全子孫・資源を同じ兄弟フォルダー内に保つ。
        std::unordered_map<std::string, std::string> renamed_roots;
        const auto rebase = [&](const std::string& relative) {
            if (relative.empty()) return destination;
            const auto separator = relative.find('\\');
            const auto first = relative.substr(0U, separator);
            const auto key = lower(first);
            auto found = renamed_roots.find(key);
            if (found == renamed_roots.end()) {
                auto renamed = uniqueDirName(first, [&](const std::string_view candidate) {
                    return !existing_roots.contains(lower(candidate));
                });
                existing_roots.insert(lower(renamed));
                found = renamed_roots.emplace(key, std::move(renamed)).first;
            }
            auto result = found->second;
            if (separator != std::string::npos) result += relative.substr(separator);
            return destination.empty() ? result : destination + '\\' + result;
        };
        for (auto& directory : directories) directory = rebase(directory);
        for (const auto& value : values)
            if (value) value->dirName(rebase(value->dirName()));
    } else if (directory_mode_ && (root.name() == Area::XML_NAME ||
               root.name() == Battle::XML_NAME || root.name() == Package::XML_NAME)) {
        // Dの場面ルートを直接渡した場合は、選択した出力先フォルダーへ貼り付ける。
        for (const auto& value : values)
            if (value) value->dirName(destination);
    }
    if (!directory_mode_) directories.clear();
    if (!insertion && prop_->var().incrementNewAreaName.value) {
        // DはArea、Battle、Packageごとに、大文字小文字
        // を区別しない完全名を管理する。新しい貼り付け名を直ちに追加し、
        // 一つのクリップボードデータ内の重複行にも順次接尾辞を付ける。
        std::unordered_set<std::string> area_names;
        std::unordered_set<std::string> battle_names;
        std::unordered_set<std::string> package_names;
        const auto collect = [](const auto& source, auto& destination) {
            for (const auto& value : source)
                if (value) destination.insert(AreaTable::lower(value->name()));
        };
        collect(summary_->areas(), area_names);
        collect(summary_->battles(), battle_names);
        collect(summary_->packages(), package_names);
        for (const auto& value : values) {
            if (!value) continue;
            auto* names = dynamic_cast<Area*>(value.get()) ? &area_names
                        : dynamic_cast<Battle*>(value.get()) ? &battle_names
                        : dynamic_cast<Package*>(value.get()) ? &package_names
                                                             : nullptr;
            if (!names) continue;
            const auto directory = value->dirName();
            const auto full_name = [&directory](const std::string_view base) {
                return AreaTable::lower(directory.empty()
                    ? std::string(base)
                    : directory + '\\' + std::string(base));
            };
            const auto base = value->baseName();
            auto candidate = base;
            if (names->contains(full_name(candidate))) {
                candidate = ::cwxeditor::cwx::utils::createNewName(
                    base, [&names, &full_name](std::string name) {
                        return !names->contains(full_name(name));
                    });
                value->baseName(candidate);
            }
            names->insert(full_name(candidate));
        }
    }
    if (values.empty() && directories.empty()) return false;
    addAreasAt(values, directories, insertion, same_summary);
    return true;
}

void AreaTable::del(AreaTableSelectionEvent& event) {
    del();
    event.handled = true;
}

void AreaTable::del() {
    if (deletion_operation_active_ || !canDoD() || !summary_) return;
    const auto owner = summary_;
    const auto owner_hold = owner->weak_from_this().lock();
    const auto binding = summary_binding_generation_;
    const std::weak_ptr<int> life = renumber_lifetime_;
    const auto current = [this, life, owner, binding] {
        return !life.expired() && summary_ == owner && summary_binding_generation_ == binding;
    };
    struct Finish { lib::Action action; ~Finish() { if (action) action(); } };
    deletion_operation_active_ = true;
    Finish finish{[this, life] { if (!life.expired()) deletion_operation_active_ = false; }};
    const auto undo_selection_before = captureUndoSelection();
    const bool delete_directory = last_focus_tree_ && directory_mode_ && !directory_.empty();
    const auto directory_before = directory_;
    const auto before_tree = dirs_ ? dirs_->dup() : std::shared_ptr<DirTree>{};
    const auto values = selectedAreas();
    struct IDs {
        std::vector<std::uint64_t> areas, battles, packages;
        explicit IDs(Summary* summary) {
            for (const auto& v : summary->areas()) areas.push_back(v->id());
            for (const auto& v : summary->battles()) battles.push_back(v->id());
            for (const auto& v : summary->packages()) packages.push_back(v->id());
        }
    };
    struct Entry {
        AbstractAreaPtr live, saved;
        std::size_t index{};
        AreaTableType type{};
        bool was_start{};
    };
    struct State {
        std::vector<Entry> entries;
        IDs ids;
        std::shared_ptr<DirTree> dirs;
        std::string directory;
        std::shared_ptr<resourcehistory::References> references;
        State(Summary* owner) : ids(owner), references(std::make_shared<resourcehistory::References>(owner->useCounter())) {}
    };
    auto state = std::make_shared<State>(owner);
    state->dirs = before_tree; state->directory = directory_before;
    const auto indexOf = [owner](const AbstractAreaPtr& value, const AreaTableType type) {
        const auto find = [&](const auto& list) {
            const auto i=std::find_if(list.begin(),list.end(),[&](const auto& p){return p.get()==value.get();});
            return i==list.end() ? std::numeric_limits<std::size_t>::max() : static_cast<std::size_t>(i-list.begin());
        };
        if (type==AreaTableType::Area) return find(owner->areas());
        if (type==AreaTableType::Battle) return find(owner->battles());
        return find(owner->packages());
    };
    // 外部の削除監視先を呼ぶ前に、削除対象の全参照関係を固定する。
    for (const auto& value : values) {
        const auto type = dynamic_cast<Area*>(value.get()) ? AreaTableType::Area
            : dynamic_cast<Battle*>(value.get()) ? AreaTableType::Battle : AreaTableType::Package;
        state->entries.push_back({value, value->dup(), 0U, type,
            type==AreaTableType::Area && owner->startArea()==value->id()});
    }
    for (auto& entry : state->entries) {
        entry.index = indexOf(entry.live,entry.type);
        if (entry.index==std::numeric_limits<std::size_t>::max()) return;
        state->references->hold(entry.saved);
        removeArea(entry.live,false);
        if (!current()) return;
    }
    if (delete_directory) {
        deleteDirTree(directory_before);
        const auto separator=directory_before.rfind('\\');
        directory_=separator==std::string::npos ? std::string{} : directory_before.substr(0U,separator);
    }
    const auto traverse = [this, life, owner, owner_hold, binding, state, delete_directory, current, indexOf](const bool insert) {
        if (!current() || deletion_operation_active_) return;
        deletion_operation_active_=true;
        Finish finish{[this, life] { if (!life.expired()) deletion_operation_active_=false; }};
        const auto desired_ids=state->ids;
        state->ids=IDs(owner); // DのATUndo.udbは、履歴をたどるたびに交換・取得する。
        const auto desired_dirs=state->dirs;
        const auto desired_directory=state->directory;
        state->dirs=dirs_ ? dirs_->dup() : std::shared_ptr<DirTree>{};
        state->directory=directory_;
        std::vector<AbstractAreaPtr> changed;
        if (insert) {
            for (auto it=state->entries.rbegin();it!=state->entries.rend();++it) {
                if (!it->saved) return;
                state->references->release(it->saved.get());
                it->live=std::move(it->saved);
                insertArea(it->live,it->index,it->type,false,false);
                if (!current()) return;
                if (it->was_start) owner->startArea(it->live->id());
                if (!current()) return;
                it->was_start=false;
                changed.push_back(it->live);
            }
        } else {
            for (auto& entry : state->entries) {
                if (!entry.live || indexOf(entry.live,entry.type)==std::numeric_limits<std::size_t>::max()) return;
                entry.saved=entry.live->dup();
                entry.was_start=entry.type==AreaTableType::Area && owner->startArea()==entry.live->id();
            }
            for (auto& entry : state->entries) {
                entry.index=indexOf(entry.live,entry.type);
                if (entry.index==std::numeric_limits<std::size_t>::max()) return;
                state->references->hold(entry.saved);
                changed.push_back(entry.live);
                removeArea(entry.live,false);
                if (!current()) return;
                entry.live.reset();
            }
        }
        // DのresetIDは、現在の型付き一覧を位置で参照する。別の削除記
        // 録が、今回の対象群以外の資源の同一性を置き換えている場合がある。
        if (owner->areas().size()!=desired_ids.areas.size() || owner->battles().size()!=desired_ids.battles.size() ||
            owner->packages().size()!=desired_ids.packages.size()) return;
        const auto restore=[&](const auto& source,const auto& ids) {
            std::vector<AbstractAreaPtr> live(source.begin(),source.end());
            applyIdsCollisionSafe(live,ids,0U);
        };
        restore(owner->areas(),desired_ids.areas);if (!current()) return;
        restore(owner->battles(),desired_ids.battles);if (!current()) return;
        restore(owner->packages(),desired_ids.packages);if (!current()) return;
        if (delete_directory) {
            dirs_=desired_dirs ? desired_dirs->dup() : std::shared_ptr<DirTree>{};
            directory_=desired_directory;
        }
        for (const auto& value : changed) {
            notifyChanged(value.get(),!insert);
            if (!current()) return;
        }
        selection_.clear();refresh();
    };
    pushUndo([traverse] { traverse(true); },[traverse] { traverse(false); }, undo_selection_before);
    if (!current()) return;
    for (const auto& value : values) {
        notifyChanged(value.get(),true);
        if (!current()) return;
    }
    for (auto& entry : state->entries) entry.live.reset();
    selection_.clear();refresh();
}

void AreaTable::clone(AreaTableSelectionEvent& event) {
    if (!canDoClone()) return;
    // 複製では、受け渡し処理も公開コピーの二つの代替処理も使わない。更新で
    // メニューを再構築する間、付随する貼り付け可否の読み取りを抑止する。
    struct RestoreFlag {
        bool& flag;
        bool before;
        std::weak_ptr<int> life;
        ~RestoreFlag() { if (!life.expired()) flag = before; }
    } restore{private_clone_in_progress_, private_clone_in_progress_, renumber_lifetime_};
    private_clone_in_progress_ = true;
    const auto text = serializeClipboardSelection();
    auto destination = directory_;
    if (last_focus_tree_ && directory_mode_) {
        const auto source = findDirTree(directory_);
        if (source && source->parent) destination = source->parent->path();
    }
    event.handled = importClipboardPayload(text, destination);
}

bool AreaTable::canDoTCPD() const {
    return summary_ && !disposed_ &&
           (!last_focus_tree_ || (directory_mode_ && tree_));
}

bool AreaTable::canDoT() const { return !read_only_ && canDoC() && canDoD(); }

bool AreaTable::canDoC() const {
    if (!canDoTCPD()) return false;
    if (last_focus_tree_) return directory_mode_ && tree_ &&
        !tree_->selection.empty() && !summarySelected() &&
        static_cast<bool>(findDirTree(directory_));
    return !selectedAreas().empty();
}

bool AreaTable::canDoP() const {
    const auto publish = [this](bool enabled) {
        last_paste_enabled_ = enabled;
        return enabled;
    };
    if (read_only_ || !summary_) return publish(false);
    // 複製は非公開のデータを直接取り込む。付随するツールバー再構築では、
    // I/Oを行わず、公開済みの実クリップボードの利用可否を維持する。
    if (private_clone_in_progress_) return last_paste_enabled_;
    if (context_ && context_->services.clipboard) {
        try {
            // 読み取り成功時は、通常テキストや空のクリップボードも含め、その結
            // 果を正とする。以前の公開コピーを復活させない。Dは認識できるXML
            // なら貼り付けを有効にし、取り込み時に場面データを検証する。
            return publish(!xmlbytestransfer::CBtoXML(*context_->services.clipboard).empty());
        } catch (...) {
            return publish(isAreaTableClipboardPayload(
                areaTableClipboardFallback(context_->services.clipboard)));
        }
    }
    return publish(isAreaTableClipboardPayload(clipboard_text_));
}

bool AreaTable::canDoClone() const { return !read_only_ && canDoC(); }

bool AreaTable::canDoD() const {
    if (read_only_ || !canDoTCPD()) return false;
    if (last_focus_tree_) return directory_mode_ && tree_ &&
        !tree_->selection.empty() && !summarySelected() && !directory_.empty() &&
        static_cast<bool>(findDirTree(directory_));
    return !selectedAreas().empty();
}

void AreaTable::addAreas(const std::vector<AbstractAreaPtr>& values,
                         const std::vector<std::string>& directories) {
    addAreasAt(values, directories, std::nullopt);
}

void AreaTable::addAreasAt(const std::vector<AbstractAreaPtr>& values,
                          const std::vector<std::string>& directories,
                          const std::optional<std::size_t> insertion,
                          const bool migrate_same_summary_references) {
    if (read_only_ || !summary_) return;
    const auto owner = summary_;
    const auto owner_hold = owner->weak_from_this().lock();
    const auto binding = summary_binding_generation_;
    const std::weak_ptr<int> life = renumber_lifetime_;
    const auto current = [this, life, owner, binding] {
        return !life.expired() && summary_ == owner && summary_binding_generation_ == binding;
    };
    const auto undo_selection_before = captureUndoSelection();
    const auto before_tree = dirs_ ? dirs_->dup() : std::shared_ptr<DirTree>{};
    const auto before_directory = directory_;
    const auto before_tree_focus = last_focus_tree_;
    const auto before_ids = ResourceIDs(summary_);
    const auto references = std::make_shared<resourcehistory::References>(summary_->useCounter());
    const auto restore_ids = [this](const ResourceIDs& state) {
        summary_->startArea(state.start ? state.start->id() : 0U);
        applyIdsCollisionSafe(state.areas, state.area_ids, 0U);
        applyIdsCollisionSafe(state.battles, state.battle_ids, 0U);
        applyIdsCollisionSafe(state.packages, state.package_ids, 0U);
    };
    struct Entry { AbstractAreaPtr value; std::size_t index{}; AreaTableType type{}; };
    std::vector<Entry> entries;
    for (const auto& value : values) {
        if (!value) continue;
        const auto type = std::dynamic_pointer_cast<Area>(value) ? AreaTableType::Area
                         : std::dynamic_pointer_cast<Battle>(value) ? AreaTableType::Battle
                                                                      : AreaTableType::Package;
        const auto size = type == AreaTableType::Area ? summary_->areas().size()
                          : type == AreaTableType::Battle ? summary_->battles().size()
                                                          : summary_->packages().size();
        const auto index = insertion ? std::min(*insertion, size) : size;
        const auto old_id = value->id();
        if (insertion) {
            // DのDropAreaは末尾でも型付きinsertを使う。add(force=true)
            // では末尾追加になり、要求された挿入境界を捨ててしまう。
            if (const auto area = std::dynamic_pointer_cast<Area>(value))
                summary_->insert(static_cast<std::ptrdiff_t>(index), area);
            else if (const auto battle = std::dynamic_pointer_cast<Battle>(value))
                summary_->insert(static_cast<std::ptrdiff_t>(index), battle);
            else if (const auto package = std::dynamic_pointer_cast<Package>(value))
                summary_->insert(static_cast<std::ptrdiff_t>(index), package);
            if (!current()) return;
            notifyChanged(value.get());
            if (!current()) return;
        } else insertArea(value, index, type, true);
        if (!current()) return;
        // DのpasteImplは、同じSummary内で存在し
        // ないIDへの参照を、新しく割り当てたIDへ移す。IDが
        // まだ存在するならコピーなので、参照は元の資源に残す。
        if (migrate_same_summary_references && old_id != 0U &&
            old_id != value->id() && owner->useCounter()) {
            const bool old_id_exists = type == AreaTableType::Area
                ? owner->hasAreaId(old_id)
                : type == AreaTableType::Battle ? owner->hasBattleId(old_id)
                                                : owner->hasPackageId(old_id);
            if (!old_id_exists) {
                const auto new_id = value->id();
                const auto counter = owner->useCounter();
                if (type == AreaTableType::Area)
                    counter->change(usecounter::toAreaId(old_id), usecounter::toAreaId(new_id));
                else if (type == AreaTableType::Battle)
                    counter->change(usecounter::toBattleId(old_id), usecounter::toBattleId(new_id));
                else
                    counter->change(usecounter::toPackageId(old_id), usecounter::toPackageId(new_id));
                if (!current()) return;
            }
        }
        entries.push_back({value, index, type});
    }
    if (entries.empty() && directories.empty()) return;
    if (directory_mode_) {
        constructDirTree(true);
        for (const auto& directory : directories) putDir(directory);
        // 明示的な取り込みでは、操作前にSummaryを選択
        // していても、挿入した資源・フォルダーへ移動する。
        if (tree_) tree_->selection.clear();
        if (!entries.empty()) directory_ = entries.back().value->dirName();
        else if (!directories.empty()) directory_ = directories.back();
    }
    last_focus_tree_ = entries.empty() ? before_tree_focus : false;
    const auto after_ids = ResourceIDs(summary_);
    const auto after_tree = dirs_ ? dirs_->dup() : std::shared_ptr<DirTree>{};
    const auto after_directory = directory_;
    const auto after_tree_focus = last_focus_tree_;
    const auto restore_tree = [this](const std::shared_ptr<DirTree>& tree,
                                     const std::string& directory, const bool tree_focus) {
        dirs_ = tree ? tree->dup() : std::shared_ptr<DirTree>{};
        directory_ = directory;
        last_focus_tree_ = tree_focus;
        if (tree_) tree_->selection.clear();
        selection_.clear();
    };
    pushUndo(
        [this, entries, references, restore_ids, before_ids, restore_tree, before_tree, before_directory, before_tree_focus] {
            for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
                removeArea(it->value); references->hold(it->value);
            }
            restore_ids(before_ids);
            restore_tree(before_tree, before_directory, before_tree_focus);
            refresh();
        },
        [this, entries, references, restore_ids, after_ids, restore_tree, after_tree, after_directory, after_tree_focus] {
            for (const auto& entry : entries) {
                references->release(entry.value.get());
                insertArea(entry.value, entry.index, entry.type, false);
            }
            restore_ids(after_ids);
            restore_tree(after_tree, after_directory, after_tree_focus);
            refresh();
            if (!entries.empty()) static_cast<void>(select(entries.back().value));
        }, undo_selection_before);
    refresh();
    if (!current()) return;
    if (!entries.empty()) {
        static_cast<void>(select(entries.back().value));
        if (!current()) return;
    }
    if (!entries.empty() && context_) {
        const auto refresh_flags = refresh_flags_
            ? refresh_flags_ : context_->callbacks.refresh_flags;
        const auto publish_use_count = context_->callbacks.publish_use_count;
        if (refresh_flags) refresh_flags();
        if (!current()) return;
        if (publish_use_count) publish_use_count();
    }
}

bool AreaTable::canUndo() const noexcept { return undo_ && undo_->canUndo(); }
bool AreaTable::canRedo() const noexcept { return undo_ && undo_->canRedo(); }

void AreaTable::undo() {
    if (read_only_ || deletion_operation_active_) return;
    const auto source = summary_;
    const auto source_binding = summary_binding_generation_;
    const auto source_context = context_;
    const auto source_gui = source_context ? source_context->services.gui : nullptr;
    const auto source_table = table_;
    const auto source_tree = tree_;
    const std::weak_ptr<int> source_life = renumber_lifetime_;
    cancelEdit();
    if (source_life.expired() || disposed_ || summary_ != source ||
        summary_binding_generation_ != source_binding || context_ != source_context ||
        table_ != source_table || tree_ != source_tree ||
        (context_ && context_->services.gui != source_gui)) return;
    const auto manager=undo_;const auto owner=summary_;const auto binding=summary_binding_generation_;
    const std::weak_ptr<int> life=renumber_lifetime_;
    // DのATUndo::udaは最終選択を戻す前に更新済み。ここで再更
    // 新すると、nullの型に対応するTreeへの代替選択が置き換わる。
    if (manager && manager->undo() && !life.expired() && summary_==owner && summary_binding_generation_==binding)
        if (context_ && context_->callbacks.refresh_toolbar) context_->callbacks.refresh_toolbar();
}
void AreaTable::redo() {
    if (read_only_ || deletion_operation_active_) return;
    const auto source = summary_;
    const auto source_binding = summary_binding_generation_;
    const auto source_context = context_;
    const auto source_gui = source_context ? source_context->services.gui : nullptr;
    const auto source_table = table_;
    const auto source_tree = tree_;
    const std::weak_ptr<int> source_life = renumber_lifetime_;
    cancelEdit();
    if (source_life.expired() || disposed_ || summary_ != source ||
        summary_binding_generation_ != source_binding || context_ != source_context ||
        table_ != source_table || tree_ != source_tree ||
        (context_ && context_->services.gui != source_gui)) return;
    const auto manager=undo_;const auto owner=summary_;const auto binding=summary_binding_generation_;
    const std::weak_ptr<int> life=renumber_lifetime_;
    // DのATUndo::udaは最終選択を戻す前に更新済み。ここで再更
    // 新すると、nullの型に対応するTreeへの代替選択が置き換わる。
    if (manager && manager->redo() && !life.expired() && summary_==owner && summary_binding_generation_==binding)
        if (context_ && context_->callbacks.refresh_toolbar) context_->callbacks.refresh_toolbar();
}
void AreaTable::setUndoLimit(const std::size_t value) {
    if (undo_) undo_->max(value);
}

void AreaTable::replaceID() {
    if (!canReplaceID() || !context_ || selection_.empty()) return;
    const auto [id, type] = getSelectionInfo();
    if (context_->callbacks.replace_id) context_->callbacks.replace_id(id, type);
}

bool AreaTable::canReplaceID() const {
    return !read_only_ && summary_ && selection_.size() == 1U &&
           !hasSelectionType(AreaTableType::Summary);
}

void AreaTable::doImport() {
    if (!canDoImport() || !context_ || !context_->callbacks.do_import) return;
    std::vector<std::string> paths;
    for (const auto& value : importAreas()) {
        if (value) paths.push_back(value->cwxPath(true));
    }
    if (!paths.empty())
        context_->callbacks.do_import(import_target_, summary_, paths);
}

bool AreaTable::canDoImport() const {
    return read_only_ && import_target_ && !importAreas().empty();
}

bool AreaTable::isSelected() const noexcept { return !selection_.empty(); }

void AreaTable::selectAll() {
    if (!canSelectAll()) return;
    const std::weak_ptr<int> life = renumber_lifetime_;
    std::vector<std::size_t> requested;
    requested.reserve(rows_.size());
    for (std::size_t index = 0; index < rows_.size(); ++index) requested.push_back(index);
    // Dはインデックス配列全体をSWTのTable.setSelection
    // へ渡す。編集用のSWT.SINGLE表では、複数インデックスの要
    // 求をSWTが無視して選択解除する。共通モデルも同じ状態にする。
    if (table_ && !table_->multi_select && requested.size() > 1U) requested.clear();
    selection_ = requested;
    if (table_) (void)lib::select_table_rows(*table_, selection_, true);
    if (!life.expired()) refreshStatusLine();
}

bool AreaTable::canSelectAll() const noexcept {
    // Dの公開selectAll判定は件数に基づく。読み取り専用設定はメニ
    // ュー構築を制御するが、編集の一括操作が使う直接の選択は制御しない。
    return !rows_.empty() && selection_.size() != rows_.size();
}

std::vector<std::string> AreaTable::openedCWXPath() const {
    std::vector<std::string> result;
    for (const auto index : selection_) {
        if (index >= rows_.size()) continue;
        const auto& row = rows_[index];
        if (row.type == AreaTableType::Summary) {
            if (summary_) result.push_back(summary_->cwxPath(true));
        } else if (row.area) {
            result.push_back(path::cpaddattr(row.area->cwxPath(true), "shallow"));
        }
    }
    return result;
}

void AreaTable::setSort(const AreaTableSortColumn column,
                        const AreaTableSortDirection direction) {
    applySort(column, direction, true);
}

void AreaTable::applySort(const AreaTableSortColumn column,
                          const AreaTableSortDirection direction, const bool publish) {
    if (disposed_) return;
    const std::weak_ptr<int> life = renumber_lifetime_;
    const auto notify = publish && read_only_ && context_
        ? context_->callbacks.publish_import_sort : std::function<void(int, int)>{};
    if (sort_column_ == column && sort_direction_ == direction) return;
    saveSelections();
    sort_column_ = column;
    sort_direction_ = direction;
    // コンストラクターの読み戻しに合わせ、DのSortDir（Up=1、Down=2
    // ）で保存する。列挙型自身の0/1を使うと、昇順に復元されてしまう。
    const auto stored_direction = static_cast<std::int32_t>(
        direction == AreaTableSortDirection::Descending ? structs::SortDir::Down
                                                        : structs::SortDir::Up);
    if (read_only_) {
        prop_->var().importAreasSortColumn.value = static_cast<std::int32_t>(column);
        prop_->var().importAreasSortDirection.value = stored_direction;
    } else {
        prop_->var().areasSortColumn.value = static_cast<std::int32_t>(column);
        prop_->var().areasSortDirection.value = stored_direction;
    }
    if (table_) table_->highlighted_column = static_cast<std::size_t>(column);
    refresh();
    if (!life.expired() && sort_column_ == column && sort_direction_ == direction && notify)
        notify(static_cast<int>(column), stored_direction);
}

void AreaTable::setDirectory(std::string value) {
    // プログラムによる移動では、ユーザーのTree選択コールバッ
    // クを発生させない。第三引数はカーソル制御、通知は第四引数。
    if (tree_) (void)lib::select_tree_items(*tree_, {}, false, false);
    const bool changed = directory_ != value;
    directory_ = std::move(value);
    const std::weak_ptr<int> life = renumber_lifetime_;
    if (changed) closeCommentDialogs();
    if (!life.expired()) refresh();
}

void AreaTable::setDirectoryMode(const bool value) {
    if (directory_mode_ == value) return;
    directory_mode_ = value;
    // 分割部がなくなれば有効なTreeのコマンド対象も
    // ない。旧Treeのフォーカスを残すと、Dの表だ
    // けの表示で表のコピー・削除が誤って無効になる。
    if (!directory_mode_) last_focus_tree_ = false;
    prop_->var().showAreaDirTree.value = value;
    // Dの設定は分割部そのものの構築を制御する。保持したウィ
    // ジェットを組み直し、実行中にfalseになれば古い分
    // 割内のTreeを隠すだけでなく、表だけの表示にする。
    if (panel_) configureWidgets({});
    refresh();
}

std::vector<AbstractAreaPtr> AreaTable::directoryAreas() const {
    std::vector<AbstractAreaPtr> result;
    if (!summary_) return result;
    // DのAreaTable.dirAreasに合わせ、現在のディ
    // レクトリと全子孫の資源を含め、指定された資源名を維持する。
    const auto folded_directory = lower(directory_);
    const auto prefix = folded_directory + '\\';
    const auto put = [&](const auto& values) {
        for (const auto& value : values) {
            if (!value) continue;
            const auto name = lower(value->dirName());
            if (folded_directory.empty() || name == folded_directory ||
                name.rfind(prefix, 0) == 0U) {
                result.push_back(std::static_pointer_cast<AbstractArea>(value));
            }
        }
    };
    put(summary_->areas());
    put(summary_->battles());
    put(summary_->packages());
    return result;
}

std::vector<AbstractAreaPtr> AreaTable::importAreas() const {
    if (last_focus_tree_ && directory_mode_ && summary_) {
        // DはディレクトリTreeがフォーカスを持つ場合、Summary行
        // の選択中やTreeの選択なしの場合もdirAreasを取り込む。
        return directoryAreas();
    }
    return selectedAreas();
}

std::vector<AbstractAreaPtr> AreaTable::selectedAreas() const {
    if (last_focus_tree_ && directory_mode_ && summary_) {
        if (!tree_ || tree_->selection.empty() || summarySelected()) return {};
        return directoryAreas();
    }
    std::vector<AbstractAreaPtr> result;
    for (const auto index : selection_) {
        if (index < rows_.size() && rows_[index].area) result.push_back(rows_[index].area);
    }
    return result;
}

std::pair<std::uint64_t, AreaTableType> AreaTable::getInfo(const std::size_t index) const {
    if (index >= rows_.size()) return {0U, AreaTableType::Summary};
    return {rows_[index].id, rows_[index].type};
}

std::pair<std::uint64_t, AreaTableType> AreaTable::getSelectionInfo() const {
    if (selection_.empty()) return {0U, AreaTableType::Summary};
    return getInfo(selection_.front());
}

AbstractAreaPtr AreaTable::getSelectionArea() const {
    if (selection_.empty()) return {};
    return areaAt(selection_.front());
}

int AreaTable::areaCount() const noexcept {
    if (!summary_) return 0;
    const auto& values = summary_->areas();
    return static_cast<int>(std::count_if(values.begin(), values.end(), [this](const auto& value) {
        return value && (!directory_mode_ || lower(value->dirName()) == lower(directory_));
    }));
}

int AreaTable::battleCount() const noexcept {
    if (!summary_) return 0;
    const auto& values = summary_->battles();
    return static_cast<int>(std::count_if(values.begin(), values.end(), [this](const auto& value) {
        return value && (!directory_mode_ || lower(value->dirName()) == lower(directory_));
    }));
}

int AreaTable::packageCount() const noexcept {
    if (!summary_) return 0;
    const auto& values = summary_->packages();
    return static_cast<int>(std::count_if(values.begin(), values.end(), [this](const auto& value) {
        return value && (!directory_mode_ || lower(value->dirName()) == lower(directory_));
    }));
}

int AreaTable::countAllAreas() const noexcept {
    // このC++補助処理は、ドラッグ・ドロップの全体がそろっているかの
    // 判定。現在のディレクトリに限定したDの状態表示用件数ではない。
    return summary_ ? static_cast<int>(summary_->areas().size() +
        summary_->battles().size() + summary_->packages().size()) : 0;
}

bool AreaTable::showSummary() const noexcept {
    // Dは編集用と読み取り専用・取り込み用の表に同じ設定を使う。
    // 後者でも、確認用に入力元のシナリオ行を公開する必要がある。
    return summary_ && prop_ &&
           prop_->var().showSummaryInAreaTable.value;
}

void AreaTable::constructDirTree(const bool add) {
    if (!add || !dirs_) dirs_ = std::make_shared<DirTree>(nullptr, "");
    if (summary_) {
        for (const auto& value : summary_->areas()) if (value) putDir(value->dirName());
        for (const auto& value : summary_->battles()) if (value) putDir(value->dirName());
        for (const auto& value : summary_->packages()) if (value) putDir(value->dirName());
    }
    sortDirTree(dirs_);
}

void AreaTable::putDir(const std::string& value) {
    if (!dirs_ || value.empty()) return;
    std::size_t start = 0;
    DirTree* parent = dirs_.get();
    while (start <= value.size()) {
        const auto end = value.find('\\', start);
        const auto part = value.substr(start, end == std::string::npos ? end : end - start);
        // DのconstructDirTreeは空の完全パスにルートを
        // 設定するが、区切り後の空の構成要素は末尾も含めて保持する。
        if (start != 0 || !part.empty()) {
            auto it = std::find_if(parent->subDirs.begin(), parent->subDirs.end(),
                [&](const auto& child) { return child && lower(child->name) == lower(part); });
            if (it == parent->subDirs.end()) {
                parent->subDirs.push_back(std::make_shared<DirTree>(parent, part));
                parent = parent->subDirs.back().get();
            } else {
                parent = it->get();
            }
        }
        if (end == std::string::npos) break;
        start = end + 1U;
    }
}

void AreaTable::sortDirTree(const std::shared_ptr<DirTree>& value) {
    if (!value) return;
    std::stable_sort(value->subDirs.begin(), value->subDirs.end(), [this](const auto& left,
                                                                       const auto& right) {
        if (!left || !right) return bool(left) && !right;
        return prop_->var().logicalSort.value
            ? naturalCompare(left->name, right->name) < 0
            : lower(left->name) < lower(right->name);
    });
    for (const auto& child : value->subDirs) sortDirTree(child);
}

std::shared_ptr<AreaTable::DirTree> AreaTable::findDirTree(
    const std::string_view value) const {
    if (!dirs_ || value.empty()) return dirs_;
    std::size_t start = 0;
    auto current = dirs_;
    while (start <= value.size()) {
        const auto end = value.find('\\', start);
        const auto part = value.substr(start, end == std::string::npos ? end : end - start);
        const auto it = std::find_if(current->subDirs.begin(), current->subDirs.end(),
            [&](const auto& child) { return child && lower(child->name) == lower(part); });
        if (it != current->subDirs.end()) current = *it;
        // DのfindDirTreeは、構成要素に一致する子がなけ
        // れば現在の項目を維持して続行する。これにより、データを正
        // 規化せず、繰り返す先頭区切りが表すルートの別名も扱う。
        if (end == std::string::npos) break;
        start = end + 1U;
    }
    return current;
}

void AreaTable::deleteDirTree(const std::string_view value) {
    const auto target = findDirTree(value);
    if (!target || !target->parent) return;
    auto& children = target->parent->subDirs;
    std::erase_if(children, [&](const auto& child) { return child.get() == target.get(); });
}

void AreaTable::callRefArea(AbstractArea* value) {
    if (!value) return;
    notifyChanged(value);
}

void AreaTable::refreshUseCount() {
    if (disposed_ || !summary_ || !table_) return;
    const std::weak_ptr<int> life = renumber_lifetime_;
    const auto source = summary_;
    const auto binding = summary_binding_generation_;
    const auto revision = table_render_revision_;
    const auto table = table_;
    const auto snapshot = rows_;
    const auto domain = captureDomainSnapshot();
    const auto gui = context_ ? context_->services.gui : nullptr;
    std::vector<std::size_t> counts;
    counts.reserve(snapshot.size());
    for (const auto& row : snapshot) {
        const auto count = row.area ? useCount(row.area) : row.use_count;
        if (life.expired()) return;
        if (disposed_ || summary_ != source || summary_binding_generation_ != binding ||
            table_render_revision_ != revision || !domainMatches(domain)) return;
        counts.push_back(count);
    }
    if (table_ != table || rows_.size() != snapshot.size() ||
        table->rows.size() != snapshot.size()) return;
    // DのrefUseCountは表示セルだけを変え、再ソートや選択・
    // スクロールの移動はしない。後の明示的な並べ替えは別操作とする。
    ++table_render_revision_;
    ++table->row_update_revision;
    for (std::size_t i = 0; i < snapshot.size(); ++i) {
        rows_[i].use_count = counts[i];
        if (table->rows[i].cells.size() > 2U) {
            table->rows[i].cells[2].text = std::to_string(counts[i]);
            table->rows[i].cells[2].value = std::to_string(counts[i]);
        }
    }
    if (gui) gui->request_redraw(table);
}

void AreaTable::dispose() {
    if (disposed_) return;
    disposed_ = true;
    renumber_lifetime_.reset();
    inc_search_.reset();
    if (renumber_dialog_) renumber_dialog_->forceCancel();
    renumber_dialog_.reset();
    if (readonly_summary_dialog_) readonly_summary_dialog_->forceCancel();
    readonly_summary_dialog_.reset();
    closeCommentDialogs();
    disconnectSignals();
    if (table_ && context_ && context_->services.drag_drop)
        context_->services.drag_drop->unbind(table_);
    if (tree_ && context_ && context_->services.drag_drop)
        context_->services.drag_drop->unbind(tree_);
    if (table_) {
        table_->on_selection_changed = {};
        table_->on_cell_activate = {};
        table_->on_cell_activate_with_modifiers = {};
        table_->on_cell_edited = {};
        table_->on_pointer = {};
        table_->on_focus = {};
    }
    if (tree_) {
        tree_->on_selection_changed = {};
        tree_->on_pointer = {};
        tree_->on_focus = {};
        tree_->on_activate = {};
        tree_->on_item_edited = {};
    }
    if (menu_) menu_->on_command = {};
    if (undo_) undo_->dispose();
    selection_.clear();
    rows_.clear();
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::areatable
