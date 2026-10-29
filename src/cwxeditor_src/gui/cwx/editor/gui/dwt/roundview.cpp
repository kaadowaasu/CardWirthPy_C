#include "cwxeditor_src/gui/cwx/editor/gui/dwt/roundview.hpp"
#include "cwxeditor_src/core/cwx/perf.hpp"

#include "cwxeditor_src/core/cwx/event.hpp"
#include "cwxeditor_src/lib/clipboard.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"
#include "cwxeditor_src/lib/ui_window_lifecycle.hpp"
#include "cwxeditor_src/lib/window_host.hpp"

#include <algorithm>
#include <charconv>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace cwxeditor::cwx::editor::gui::dwt::roundview {
namespace {

inline constexpr StringView kEditorXmlMime = "application/x-cwxeditor-xml";

[[nodiscard]] std::vector<std::byte> bytesFromText(const StringView text) {
    std::vector<std::byte> result;
    result.reserve(text.size());
    for (const unsigned char value : text)
        result.push_back(static_cast<std::byte>(value));
    return result;
}

[[nodiscard]] String textFromBytes(const std::vector<std::byte>& bytes) {
    String result;
    result.reserve(bytes.size());
    for (const auto value : bytes)
        result.push_back(static_cast<char>(std::to_integer<unsigned char>(value)));
    return result;
}

void setMenuCommandEnabled(::cwxeditor::lib::Menu& menu,
                           const StringView command, const bool enabled) {
    const auto found = std::find_if(
        menu.items.begin(), menu.items.end(),
        [command](const auto& item) { return item.command == command; });
    if (found != menu.items.end()) found->enabled = enabled;
}

[[nodiscard]] Round clampRound(const Round value, const Round maximum) noexcept {
    if (maximum == 0U) return 0U;
    return std::clamp(value, 1U, maximum);
}

[[nodiscard]] std::string roundText(const Round value) {
    return std::to_string(value);
}

[[nodiscard]] bool parseRound(StringView text, Round& result) noexcept {
    if (text.empty()) return false;
    Round parsed{};
    const auto* first = text.data();
    const auto* last = first + text.size();
    const auto converted = std::from_chars(first, last, parsed);
    if (converted.ec != std::errc{} || converted.ptr != last) return false;
    result = parsed;
    return true;
}

[[nodiscard]] String formatRangeHint(String format, const Round maximum) {
    const String first = "1";
    const String second = std::to_string(maximum);
    const String replacements[] = {first, second};
    std::size_t replacement = 0U;
    for (std::size_t pos = 0U; pos + 1U < format.size() && replacement < 2U;) {
        if (format[pos] == '%' &&
            (format[pos + 1U] == 'u' || format[pos + 1U] == 'd' || format[pos + 1U] == 's')) {
            format.replace(pos, 2U, replacements[replacement++]);
            pos += replacements[replacement - 1U].size();
        } else {
            ++pos;
        }
    }
    return format;
}

}  // namespace

RoundView::RoundView(::cwxeditor::lib::Container* parent,
                     const std::uint32_t style,
                     RoundViewConfig config,
                     ::cwxeditor::lib::Callback<bool()> catchMod)
    : parent_(parent),
      style_(style),
      config_(std::move(config)),
      catch_mod_(std::move(catchMod)),
      read_only_(config_.readOnly || (style_ & STYLE_READ_ONLY) != 0U) {
    config_.readOnly = read_only_;
    initializeTable();
    updateCanAddRound();
}

RoundView::RoundView(RoundViewConfig config,
                     ::cwxeditor::lib::Container* parent,
                     const std::uint32_t style,
                     ::cwxeditor::lib::Callback<bool()> catchMod)
    : RoundView(parent, style, std::move(config), std::move(catchMod)) {}

RoundView::~RoundView() {
    callback_lifetime_.reset();
    if(table_) {
        table_->on_selection_changed={};
        table_->on_cell_activate={};
        table_->on_cell_edited={};
        table_->on_inline_edit_cancelled={};
        table_->on_pointer={};
    }
    if(toolbar_) toolbar_->on_command={};
    if(popup_menu_) {
        popup_menu_->on_command={};
        popup_menu_->on_open={};
    }
}

void RoundView::initializeTable() {
    const std::weak_ptr<int> lifetime=callback_lifetime_;
    set_layout(std::make_shared<::cwxeditor::lib::LinearLayout>(
        ::cwxeditor::lib::Orientation::Vertical));

    toolbar_ = std::make_shared<ToolBar>();
    {
        toolbar_->items.push_back({config_.resources.addRoundText, "add", config_.resources.addRoundImage});
        toolbar_->items.push_back({config_.resources.delRoundText, "delete", config_.resources.delRoundImage});
        toolbar_->on_command = [this,lifetime](StringView command) {
            if(lifetime.expired()) return;
            if (command == "add") {
                (void)addRound();
            } else if (command == "delete") {
                (void)deleteSelection();
            }
        };
        toolbar_->set_enabled(!read_only_ && enabled());
        add(toolbar_);
    }

    table_ = std::make_shared<Table>();
    table_->multi_select = true;
    table_->full_row_selection = true;
    table_->virtualized = true;
    table_->columns.push_back({});
    table_->header_visible = false;
    table_->inline_edit_column = 0U;
    table_->commit_unchanged_inline_edits = true;
    table_->on_inline_edit_cancelled = [this, lifetime](const std::size_t row,
                                                       const std::size_t column) {
        if (lifetime.expired() || column != 0U || !editing_index_ ||
            *editing_index_ != row) return;
        cancelEdit();
    };
    table_->on_selection_changed = [this,lifetime](const std::vector<std::size_t>& indices) {
        if(lifetime.expired()) return;
        selection_ = indices;
        normalizeSelection();
        updateCanAddRound();
        notifyRefreshToolBar();
    };
    table_->on_cell_activate = [this,lifetime](const std::size_t row, const std::size_t column) {
        if(lifetime.expired()) return;
        if (column == 0U) (void)beginEdit(row);
    };
    table_->on_cell_edited = [this,lifetime](const std::size_t row, const std::size_t column,
                                    String text) {
        if(lifetime.expired()) return;
        if (column != 0U || read_only_) return;
        Round parsed{};
        if (parseRound(text, parsed)) {
            const auto revision = render_revision_;
            const bool edited = editValue(row, std::clamp(parsed, Round{1}, std::max(Round{1}, config_.roundMax)));
            if (lifetime.expired()) return;
            // editValue は成功すると表を再構築し、通知がモデ
            // ルを置き換える場合もある。 古い編集を再び反映しない。
            if (edited || revision != render_revision_) return;
        }
        // 共通のインライン編集は、コールバックより先に文字列を反映する
        // 。 不正・重複した数値入力の後は、正規の表示文字列を戻す。
        rebuildTable(false);
    };
    popup_menu_ = std::make_shared<::cwxeditor::lib::Menu>(true);
    if (!read_only_) {
        popup_menu_->items.push_back({config_.resources.undoText, "undo"});
        popup_menu_->items.push_back({config_.resources.redoText, "redo"});
        popup_menu_->items.push_back({{}, {}, {}, 0, true, false, false, true});
        popup_menu_->items.push_back({config_.resources.cutText, "cut"});
    }
    popup_menu_->items.push_back({config_.resources.copyText, "copy"});
    if (!read_only_) {
        popup_menu_->items.push_back({config_.resources.pasteText, "paste"});
        popup_menu_->items.push_back({config_.resources.deleteText, "delete"});
    }
    popup_menu_->items.push_back({{}, {}, {}, 0, true, false, false, true});
    popup_menu_->items.push_back({config_.resources.selectAllText, "select-all"});
    if (!read_only_) {
        popup_menu_->items.push_back({{}, {}, {}, 0, true, false, false, true});
        popup_menu_->items.push_back({config_.resources.manyRoundsText, "add-many",
                                      {}, config_.resources.addManyRoundsImage});
    }
    popup_menu_->on_command = [this,lifetime](StringView command) {
        if(lifetime.expired()) return;
        if (command == "undo") (void)undo();
        else if (command == "redo") (void)redo();
        else if (command == "cut") (void)cutSelection();
        else if (command == "copy") (void)copySelection();
        else if (command == "paste") (void)pasteClipboard();
        else if (command == "delete") (void)deleteSelection();
        else if (command == "select-all") selectAll();
        else if (command == "add-many") (void)addManyRounds();
        refreshPopupMenu();
    };
    popup_menu_->on_open = [this,lifetime] {
        if (!lifetime.expired()) refreshPopupMenu();
    };
    table_->on_pointer = [this,lifetime](::cwxeditor::lib::PointerEvent& event) {
        if (lifetime.expired() ||
            event.button != ::cwxeditor::lib::PointerButton::Secondary ||
            event.action != ::cwxeditor::lib::PointerAction::Up ||
            !popup_menu_ || !config_.services.gui) return;
        refreshPopupMenu();
        config_.services.gui->show_context_menu(popup_menu_, table_, event.position);
        event.handled = true;
    };
    add(table_);
}

void RoundView::addModifyListener(ModifyHandler handler) {
    if (handler) modEvent.push_back(std::move(handler));
}

void RoundView::setReadOnly(const bool value) {
    read_only_ = value;
    config_.readOnly = value;
    if (read_only_) {
        new_item_index_.reset();
        editing_index_.reset();
    }
    updateEditors();
    if (toolbar_) toolbar_->set_enabled(!read_only_ && enabled());
    updateCanAddRound();
}

void RoundView::setEnabled(const bool value) {
    ::cwxeditor::lib::Widget::set_enabled(value);
    if (table_) table_->set_enabled(value);
    if (toolbar_) toolbar_->set_enabled(value && !read_only_);
    updateEditors();
}

void RoundView::setRoundMax(const Round value) {
    config_.roundMax = value;
    updateEditors();
    updateCanAddRound();
}

Round RoundView::value(const std::size_t index) const {
    if (index >= values_.size()) throw std::out_of_range("RoundView::value index");
    return values_[index];
}

void RoundView::setValues(const Rounds& values) {
    for (const auto& editor : editors_) {
        if (editor) editor->on_changed = {};
    }
    values_ = values;
    sortValues();
    selection_.clear();
    new_item_index_.reset();
    editing_index_.reset();
    clearUndo();
    rebuildTable(true);
}

void RoundView::setValue(const std::size_t index, const Round newValue) {
    if (index >= values_.size()) throw std::out_of_range("RoundView::setValue index");
    if (values_[index] == newValue) return;
    (void)editValue(index, newValue);
}

void RoundView::setSelection(const ::cwxeditor::lib::Array<std::size_t>& indices, const bool reveal) {
    selection_ = indices;
    normalizeSelection();
    if (table_) {
        (void)lib::select_table_rows(*table_, selection_, false, false);
        if (reveal && !lib::editor_window_host_options().all_mode && !selection_.empty())
            (void)lib::reveal_table_row(*table_, selection_.front());
    }
    updateCanAddRound();
    notifyRefreshToolBar();
}

void RoundView::clearSelection() {
    selection_.clear();
    if (table_) (void)lib::select_table_rows(*table_, {}, false, false);
    updateCanAddRound();
    notifyRefreshToolBar();
}

bool RoundView::canSelectAll() const noexcept {
    return !values_.empty() && selection_.size() != values_.size();
}

void RoundView::selectAll() {
    if (!canSelectAll()) return;
    selection_.resize(values_.size());
    for (std::size_t i = 0; i < selection_.size(); ++i) selection_[i] = i;
    if (table_) (void)lib::select_table_rows(*table_, selection_, false, false);
    updateCanAddRound();
    notifyRefreshToolBar();
}

void RoundView::updateCanAddRound() noexcept {
    ::cwxeditor::cwx::perf::TraceScope trace_updateCanAddRound(__FILE__, __LINE__);
    can_add_round_ = !read_only_;
    if (can_add_round_ && values_.size() == static_cast<std::size_t>(config_.roundMax)) {
        // D版と同様、ここでは equal(expected, actual)
        // を否定する。 上限まである一覧でも重複・欠番があれば追加
        // を許可するが、 実際の挿入は後の最大数の検査で拒否される。
        can_add_round_ = false;
        for (std::size_t i = 0; i < values_.size(); ++i) {
            if (values_[i] != static_cast<Round>(i + 1U)) {
                can_add_round_ = true;
                break;
            }
        }
    } else if (can_add_round_ && config_.roundMax == 0U && values_.empty()) {
        can_add_round_ = false;
    }
    if (toolbar_ && toolbar_->items.size() >= 2U) {
        auto items = toolbar_->items;
        items[0].enabled = can_add_round_;
        items[1].enabled = !read_only_ && !selection_.empty();
        (void)lib::replace_tool_bar_items(toolbar_, std::move(items));
    }
    if (popup_menu_ && popup_menu_->items.size() >= 2U) {
        auto items = popup_menu_->items;
        items[1].enabled = can_add_round_;
        (void)lib::replace_menu_items(popup_menu_, std::move(items));
    }
}

Round RoundView::createNewValue(std::int32_t& index, bool& cancel) const noexcept {
    ::cwxeditor::cwx::perf::TraceScope trace_createNewValue(__FILE__, __LINE__);
    index = -1;
    cancel = false;
    Round candidate = 1U;
    while (candidate != 0U && candidate <= config_.roundMax) {
        if (std::find(values_.begin(), values_.end(), candidate) == values_.end()) {
            const auto insertion = static_cast<std::uint64_t>(candidate) - 1U;
            index = insertion > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())
                        ? -1
                        : static_cast<std::int32_t>(insertion);
            return candidate;
        }
        ++candidate;
    }
    cancel = true;
    return 0U;
}

bool RoundView::addRound() {
    if (read_only_ || !can_add_round_) return false;
    std::int32_t index = -1;
    bool cancel = false;
    const auto newValue = createNewValue(index, cancel);
    if (cancel) return false;
    const auto insertion = index < 0
                               ? values_.size()
                               : std::min<std::size_t>(static_cast<std::size_t>(index), values_.size());
    values_.insert(values_.begin() + static_cast<std::ptrdiff_t>(insertion), newValue);
    sortValues();
    const auto found = std::find(values_.begin(), values_.end(), newValue);
    if (found != values_.end()) {
        const auto position = static_cast<std::size_t>(std::distance(values_.begin(), found));
        selection_={position};
        new_item_index_ = position;
        editing_index_ = position;
        (void)beginEdit(position);
    }
    const std::weak_ptr<int> lifetime = callback_lifetime_;
    const auto revision = render_revision_ + 1;
    rebuildTable(false);
    // D版の add と同様、仮の行を確定する前にツールバーを更新し、Modify は
    // mainValueEditEnd で通知する。 C++版では行の反映が所有者
    // を破棄・置換する場合があるため、借用するホストのコールバックを呼ぶ前に止める。
    if (lifetime.expired() || revision != render_revision_) return true;
    notifyRefreshToolBar();
    if (lifetime.expired() || revision != render_revision_ || read_only_ ||
        !new_item_index_ || *new_item_index_ >= values_.size() ||
        values_[*new_item_index_] != newValue) return true;
    const auto gui = config_.services.gui;
    const auto table = table_;
    if (gui && table) {
        ::cwxeditor::lib::ProgrammaticInlineEditRequest request;
        request.table = table;
        request.focus_scope = table;
        request.matches_stable_row = [this, lifetime, revision, newValue](std::size_t row) {
            return !lifetime.expired() && revision == render_revision_ &&
                   !read_only_ && new_item_index_ && row == *new_item_index_ &&
                   row < values_.size() && values_[row] == newValue;
        };
        (void)gui->request_programmatic_inline_edit(request);
    }
    return true;
}

bool RoundView::addManyRounds(const Round from, const Round to) {
    if (read_only_ || !can_add_round_ || config_.roundMax == 0U) return false;
    const auto first = clampRound(from, config_.roundMax);
    const auto last = clampRound(to, config_.roundMax);
    if (first == 0U || last == 0U || first > last) return false;
    Rounds values;
    values.reserve(static_cast<std::size_t>(last - first) + 1U);
    for (Round value = first;; ++value) {
        values.push_back(value);
        if (value == last) break;
    }
    return appendValues(values);
}

bool RoundView::addManyRounds() {
    if (read_only_ || !can_add_round_) return false;
    const std::weak_ptr<int> lifetime=callback_lifetime_;
    // D版と同様、範囲ダイアログを開く前に、編集中の表の値を確定する。
    // 後でダイアログをキャンセルする場合も同様。 未確定のネイティブの
    // 数値入力は表示側が管理するため、共通の操作経路を通して確定する。
    if (!finishInlineEditForAction() || lifetime.expired()) return false;
    if (read_only_ || !can_add_round_) return false;
    const auto revision=render_revision_;
    const auto config=config_;
    ManyRoundsDialog dialog(config);
    const bool accepted=config.manyRoundsDialog?config.manyRoundsDialog(dialog):dialog.open();
    if(!accepted || lifetime.expired() || revision!=render_revision_) return false;
    return addManyRounds(dialog.from(), dialog.to());
}

bool RoundView::appendValues(const Rounds& values, const bool force) {
    return appendValuesImpl(values, -1, force);
}

bool RoundView::appendValuesImpl(const Rounds& values, const std::int32_t index,
                                 const bool force) {
    Rounds accepted;
    accepted.reserve(values.size());
    if (force || config_.canDuplicate) {
        accepted = values;
    } else {
        Rounds existing = values_;
        for (const auto value : values) {
            if (!isValidValue(value)) continue;
            if (std::find(existing.begin(), existing.end(), value) != existing.end()) continue;
            existing.push_back(value);
            accepted.push_back(value);
        }
    }
    if (accepted.empty()) return false;

    storeSnapshot();
    const auto insertion = index < 0
                               ? values_.size()
                               : std::min<std::size_t>(static_cast<std::size_t>(index), values_.size());
    values_.insert(values_.begin() + static_cast<std::ptrdiff_t>(insertion),
                   accepted.begin(), accepted.end());
    sortValues();
    selection_.clear();
    for (const auto added : accepted) {
        for (std::size_t i = 0; i < values_.size(); ++i) {
            if (values_[i] == added) selection_.push_back(i);
        }
    }
    normalizeSelection();
    rebuildTable(false, true);
    return true;
}

int RoundView::existingValue(const std::size_t index, const Round candidate) const noexcept {
    if (config_.canDuplicate) return -1;
    for (std::size_t i = 0; i < values_.size(); ++i) {
        if (i != index && values_[i] == candidate) return static_cast<int>(i);
    }
    return -1;
}

bool RoundView::deleteSelection() {
    if (read_only_ || selection_.empty()) return false;
    storeSnapshot();
    auto indices = selection_;
    std::sort(indices.begin(), indices.end(), std::greater<>());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    for (const auto index : indices) {
        if (index < values_.size()) values_.erase(values_.begin() + static_cast<std::ptrdiff_t>(index));
    }
    selection_.clear();
    new_item_index_.reset();
    editing_index_.reset();
    rebuildTable(false, true);
    return true;
}

bool RoundView::editValue(const std::size_t index, const Round newValue) {
    if (read_only_ || index >= values_.size()) return false;
    const auto oldValue = values_[index];
    const bool creating = new_item_index_.has_value() && *new_item_index_ == index;
    if (!creating && oldValue == newValue) {
        new_item_index_.reset();
        editing_index_.reset();
        return false;
    }

    const auto duplicate = existingValue(index, newValue);
    if (!isValidValue(newValue) || duplicate != -1) {
        if (creating) {
            values_.erase(values_.begin() + static_cast<std::ptrdiff_t>(index));
            selection_.clear();
            // D版の mainValueEditEnd と同様、仮行を削除してから既存
            // のラウンドを選ぶ。 削除位置が手前なら、その分だけ選択位置を補正する。
            if (duplicate != -1) {
                const auto existing = static_cast<std::size_t>(duplicate);
                selection_.push_back(existing - (index < existing ? 1U : 0U));
            }
            new_item_index_.reset();
            editing_index_.reset();
            const std::weak_ptr<int> lifetime=callback_lifetime_;
            const auto revision=render_revision_+1;
            rebuildTable(false, false, duplicate != -1);
            if(!lifetime.expired() && revision==render_revision_) notifyRefreshToolBar();
            return true;
        }
        if (duplicate != -1) {
            // D版と同様、既存行との重複編集も Modify・ツール
            // バー通知で終了するが、 値と取り消し履歴は変更しない。
            editing_index_.reset();
            selection_ = {static_cast<std::size_t>(duplicate)};
            rebuildTable(false, true, true);
        }
        return false;
    }

    if (creating) {
        values_.erase(values_.begin() + static_cast<std::ptrdiff_t>(index));
        storeSnapshot();
        values_.push_back(newValue);
    } else {
        storeSnapshot();
        values_[index] = newValue;
    }
    sortValues();
    new_item_index_.reset();
    editing_index_.reset();
    const auto found = std::find(values_.begin(), values_.end(), newValue);
    selection_.clear();
    if (found != values_.end()) selection_.push_back(static_cast<std::size_t>(std::distance(values_.begin(), found)));
    rebuildTable(false, true, true);
    return true;
}

bool RoundView::beginEdit(const std::size_t index) {
    if (read_only_ || index >= values_.size()) return false;
    editing_index_ = index;
    updateEditors();
    return index < editors_.size() && static_cast<bool>(editors_[index]);
}

void RoundView::cancelEdit() {
    const bool remove_new=new_item_index_ && *new_item_index_<values_.size();
    if(remove_new) {
        values_.erase(values_.begin()+static_cast<std::ptrdiff_t>(*new_item_index_));
        selection_.clear();
    }
    new_item_index_.reset();
    editing_index_.reset();
    if(remove_new) rebuildTable(false);
    else updateEditors();
}

bool RoundView::undo() {
    if (undo_.empty()) return false;
    redo_.push_back({values_, selection_});
    const auto snapshot = undo_.back();
    undo_.pop_back();
    restoreSnapshot(snapshot);
    return true;
}

bool RoundView::redo() {
    if (redo_.empty()) return false;
    undo_.push_back({values_, selection_});
    const auto snapshot = redo_.back();
    redo_.pop_back();
    restoreSnapshot(snapshot);
    return true;
}

void RoundView::clearUndo() noexcept {
    undo_.clear();
    redo_.clear();
}

void RoundView::setUndoLimit(const std::size_t value) {
    config_.undoMax = value;
    const auto trim = [value](auto& history) {
        if (value == 0U) history.clear();
        else if (history.size() > value)
            history.erase(history.begin(), history.end() - static_cast<std::ptrdiff_t>(value));
    };
    trim(undo_);
    trim(redo_);
    refreshPopupMenu();
    if (config_.refreshToolBar) config_.refreshToolBar();
}

bool RoundView::copySelection() {
    const auto clipboard = config_.services.clipboard;
    if (selection_.empty() || !clipboard) return false;
    const std::weak_ptr<int> lifetime = callback_lifetime_;
    if (!finishInlineEditForAction() || lifetime.expired()) return false;
    if (selection_.empty() || !clipboard) return false;
    const auto xml = toNode(selection_).text();
    ::cwxeditor::lib::ClipboardData data;
    data.text = xml;
    data.formats[String(kEditorXmlMime)] = bytesFromText(xml);
    clipboard->clear();
    clipboard->write(data);
    if (lifetime.expired()) return true;
    notifyRefreshToolBar();
    return true;
}

bool RoundView::cutSelection() {
    const std::weak_ptr<int> lifetime = callback_lifetime_;
    if (read_only_ || selection_.empty() || !copySelection()) return false;
    // コピー完了後に、クリップボードのコールバックがビューを閉じる場合がある。
    if (lifetime.expired()) return false;
    return deleteSelection();
}

bool RoundView::pasteClipboard() {
    if (read_only_ || !config_.services.clipboard) return false;
    const std::weak_ptr<int> lifetime = callback_lifetime_;
    const auto clipboard = config_.services.clipboard;
    const std::string_view requested[]{kEditorXmlMime, "text/plain;charset=utf-8", "text/plain"};
    const auto data = clipboard->read_formats(requested);
    String xml = data.text;
    if (xml.empty()) {
        const auto found = data.formats.find(String(kEditorXmlMime));
        if (found != data.formats.end()) xml = textFromBytes(found->second);
    }
    if (lifetime.expired() || xml.empty()) return false;
    // D版と同様、先にクリップボードを読み、次に現在の
    // 数値編集を確定してから、データを解析・追加する。
    if (!finishInlineEditForAction() || lifetime.expired() || read_only_) return false;
    try {
        const auto node = ::cwxeditor::cwx::xml::XNode::parse(xml, true);
        if (node.name() != xmlName()) return false;
        return appendValues(createFromNode(node));
    } catch (const std::exception&) {
        return false;
    }
}

StringView RoundView::xmlName() const noexcept {
    return event::ROUNDS_XML_NAME;
}

Rounds RoundView::createFromNode(const ::cwxeditor::cwx::xml::XNode& node) const {
    const auto version = config_.dataVersion.empty()
                             ? ::cwxeditor::cwx::versioninfo::LATEST_VERSION
                             : config_.dataVersion;
    const ::cwxeditor::cwx::system::XMLInfo info(
        config_.system ? *config_.system : system_, version);
    return event::roundsFromNode(node, &info);
}

::cwxeditor::cwx::xml::XNode RoundView::toNode(const Indices& indices) const {
    Rounds selected;
    selected.reserve(indices.size());
    for (const auto index : indices) {
        if (index >= 0 && static_cast<std::size_t>(index) < values_.size()) {
            selected.push_back(values_[static_cast<std::size_t>(index)]);
        }
    }
    return event::roundsToNode(selected);
}

::cwxeditor::cwx::xml::XNode RoundView::toNode(
    const ::cwxeditor::lib::Array<std::size_t>& indices) const {
    Indices converted;
    converted.reserve(indices.size());
    for (const auto index : indices) {
        if (index <= static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
            converted.push_back(static_cast<std::int32_t>(index));
        }
    }
    return toNode(converted);
}

void RoundView::refreshTable() {
    rebuildTable(false);
}

void RoundView::rebuildTable(const bool resetTop, const bool changed, const bool reveal) {
    if (!table_) return;
    const std::weak_ptr<int> lifetime=callback_lifetime_;
    const auto revision=++render_revision_;
    const auto values=values_;
    const auto selected=selection_;
    const auto read_only=read_only_;
    const auto was_enabled=enabled();
    std::vector<::cwxeditor::lib::TableRow> rows(values.size());
    for (std::size_t i = 0; i < values.size(); ++i) updateItem(values[i], rows[i]);
    normalizeSelection();
    const auto target=table_;
    if(!::cwxeditor::lib::replace_table_rows(*target, std::move(rows), selection_, {}, [&] {
        return !lifetime.expired() && revision==render_revision_ && table_==target &&
            values_==values && selection_==selected && read_only_==read_only && enabled()==was_enabled;
    }, resetTop)) return;
    // D版の sortValues と同様、Modify リスナーを呼ぶ前に編集した選択を表示する。
    if (reveal && !lib::editor_window_host_options().all_mode && !selection_.empty())
        (void)lib::reveal_table_row(*target, selection_.front());
    updateEditors();
    updateCanAddRound();
    if(changed) {
        raiseModifyEvent();
        if(lifetime.expired() || revision!=render_revision_) return;
        notifyRefreshToolBar();
    }
}

void RoundView::updateItem(const Round round, ::cwxeditor::lib::TableRow& item) const {
    const auto text = roundText(round);
    item.cells.clear();
    item.cells.push_back({text, text, config_.resources.roundImage});
    auto& cell = item.cells.back();
    cell.editor = ::cwxeditor::lib::CellEditorKind::Number;
    cell.editor_step = 1.0;
    cell.editor_minimum = 1;
    cell.editor_maximum = std::max(Round{1}, config_.roundMax);
}

RoundView::EditorPtr RoundView::mainEditor() const noexcept {
    if (editing_index_.has_value() && *editing_index_ < editors_.size()) return editors_[*editing_index_];
    if (!editors_.empty()) return editors_.front();
    return {};
}

void RoundView::raiseModifyEvent() {
    const std::weak_ptr<int> lifetime=callback_lifetime_;
    const auto revision=render_revision_;
    const auto listeners=modEvent;
    for(const auto& listener:listeners) {
        if(lifetime.expired() || revision!=render_revision_) return;
        if(listener) listener();
    }
}

void RoundView::notifyRefreshToolBar() const {
    const auto notify=config_.refreshToolBar;
    if (notify) notify();
}

void RoundView::storeSnapshot() {
    if (config_.undoMax == 0U) {
        redo_.clear();
        return;
    }
    undo_.push_back({values_, selection_});
    if (undo_.size() > config_.undoMax) undo_.erase(undo_.begin());
    redo_.clear();
}

void RoundView::restoreSnapshot(const Snapshot& snapshot) {
    values_ = snapshot.values;
    selection_ = snapshot.selection;
    new_item_index_.reset();
    editing_index_.reset();
    rebuildTable(false, true);
}

void RoundView::sortValues() {
    std::sort(values_.begin(), values_.end());
}

void RoundView::normalizeSelection() noexcept {
    selection_.erase(std::remove_if(selection_.begin(), selection_.end(),
                                    [this](const std::size_t index) {
                                        return index >= values_.size();
                                    }),
                     selection_.end());
    std::sort(selection_.begin(), selection_.end());
    selection_.erase(std::unique(selection_.begin(), selection_.end()), selection_.end());
}

bool RoundView::finishInlineEditForAction() {
    const std::weak_ptr<int> lifetime = callback_lifetime_;
    const auto table = table_;
    const auto gui = config_.services.gui;
    // false は、有効な表示側の編集状態がなかった場合も表す
    // 。 続行の可否は、所有者の寿命と表の同一性だけで判断する。
    if (gui && table) (void)gui->finish_programmatic_inline_edit(table, true);
    return !lifetime.expired() && table_ == table;
}

bool RoundView::canPasteClipboard() const {
    if (read_only_ || !config_.services.clipboard) return false;
    const std::string_view requested[]{kEditorXmlMime, "text/plain;charset=utf-8", "text/plain"};
    const auto data = config_.services.clipboard->read_formats(requested);
    String xml = data.text;
    if (xml.empty()) {
        const auto found = data.formats.find(String(kEditorXmlMime));
        if (found != data.formats.end()) xml = textFromBytes(found->second);
    }
    if (xml.empty()) return false;
    try {
        return ::cwxeditor::cwx::xml::XNode::parse(xml, true).name() == xmlName();
    } catch (const std::exception&) {
        return false;
    }
}

void RoundView::refreshPopupMenu() {
    if (!popup_menu_) return;
    const bool available = enabled();
    const bool selected = available && !selection_.empty();
    setMenuCommandEnabled(*popup_menu_, "undo", available && !read_only_ && canUndo());
    setMenuCommandEnabled(*popup_menu_, "redo", available && !read_only_ && canRedo());
    setMenuCommandEnabled(*popup_menu_, "cut", selected && !read_only_);
    setMenuCommandEnabled(*popup_menu_, "copy", selected);
    setMenuCommandEnabled(*popup_menu_, "paste", available && canPasteClipboard());
    setMenuCommandEnabled(*popup_menu_, "delete", selected && !read_only_);
    setMenuCommandEnabled(*popup_menu_, "select-all", available && canSelectAll());
    setMenuCommandEnabled(*popup_menu_, "add-many",
                          available && !read_only_ && canAddRound());
}

void RoundView::updateEditors() {
    const std::weak_ptr<int> lifetime=callback_lifetime_;
    const auto epoch=table_?table_->row_order_revision:0;
    const auto revision=render_revision_;
    if (read_only_ || values_.empty()) {
        editors_.clear();
        return;
    }
    editors_.resize(values_.size());
    for (std::size_t i = 0; i < values_.size(); ++i) {
        if (!editors_[i]) {
            editors_[i] = std::make_shared<Editor>();
        }
        const auto index = i;
        editors_[i]->on_changed = [this, index,lifetime,epoch,revision](const double value) {
            if(lifetime.expired() || revision!=render_revision_ || !table_ ||
               table_->row_order_revision!=epoch || read_only_ || !enabled()) return;
            if (value < 0.0 || value > static_cast<double>(std::numeric_limits<Round>::max())) return;
            (void)editValue(index, static_cast<Round>(value));
        };
        editors_[i]->minimum = config_.roundMax == 0U ? 0.0 : 1.0;
        editors_[i]->maximum = static_cast<double>(config_.roundMax);
        editors_[i]->step = 1.0;
        editors_[i]->value = static_cast<double>(values_[i]);
        editors_[i]->read_only = read_only_;
        editors_[i]->set_enabled(enabled());
    }
}

ManyRoundsDialog::ManyRoundsDialog(RoundViewConfig config,
                                   ::cwxeditor::lib::Owner<::cwxeditor::lib::Window> owner,
                                   ::cwxeditor::lib::Window* parent)
    : config_(std::move(config)), dialog_owner_(std::move(owner)), parent_(parent) {
    dialog_ = std::make_shared<Dialog>();
    dialog_->title = config_.resources.manyRoundsText;
    dialog_->icon = config_.resources.addManyRoundsImage;
    auto resolved_owner = dialog_owner_;
    if (!resolved_owner && parent_)
        resolved_owner = std::shared_ptr<::cwxeditor::lib::Window>(
            parent_, [](::cwxeditor::lib::Window*) {});
    ::cwxeditor::lib::configure_owned_window(dialog_, resolved_owner,
        ::cwxeditor::lib::OwnedWindowKind::Modal);
    // D版と同様、範囲付きの二つの数値欄は最大値ではなく最小値で初期化する。
    setRange(1U, 1U);
}

void ManyRoundsDialog::setOwner(
    ::cwxeditor::lib::Owner<::cwxeditor::lib::Window> owner) {
    dialog_owner_ = std::move(owner);
    if (!dialog_) return;
    auto resolved_owner = dialog_owner_;
    if (!resolved_owner && parent_)
        resolved_owner = std::shared_ptr<::cwxeditor::lib::Window>(
            parent_, [](::cwxeditor::lib::Window*) {});
    ::cwxeditor::lib::configure_owned_window(dialog_, resolved_owner,
        ::cwxeditor::lib::OwnedWindowKind::Modal);
}

void ManyRoundsDialog::setFrom(const Round value) noexcept {
    const auto next = clampRound(value, config_.roundMax);
    from_value_ = next;
    if (from_value_ >= to_value_) to_value_ = from_value_;
    syncEditors();
}

void ManyRoundsDialog::setTo(const Round value) noexcept {
    const auto next = clampRound(value, config_.roundMax);
    to_value_ = next;
    if (to_value_ <= from_value_) from_value_ = to_value_;
    syncEditors();
}

void ManyRoundsDialog::setRange(const Round from, const Round to) noexcept {
    const auto first = clampRound(from, config_.roundMax);
    const auto last = clampRound(to, config_.roundMax);
    if (first == 0U || last == 0U) {
        from_value_ = 0U;
        to_value_ = 0U;
    } else if (first <= last) {
        from_value_ = first;
        to_value_ = last;
    } else {
        from_value_ = last;
        to_value_ = first;
    }
    syncEditors();
}

void ManyRoundsDialog::setup(Container& area) {
    area.set_layout(std::make_shared<::cwxeditor::lib::GridLayout>());
    auto group = std::make_shared<::cwxeditor::lib::GroupBox>();
    group->title = config_.resources.manyRoundsText;
    group->set_layout(std::make_shared<::cwxeditor::lib::FillLayout>());
    auto controls = std::make_shared<::cwxeditor::lib::Panel>();
    auto grid = std::make_shared<::cwxeditor::lib::GridLayout>();
    grid->columns = 4U;
    grid->equal_width = false;
    controls->set_layout(grid);

    from_ = std::make_shared<NumberEdit>();
    to_ = std::make_shared<NumberEdit>();
    from_->on_changed = [this](const double value) {
        if (value >= 0.0 && value <= static_cast<double>(std::numeric_limits<Round>::max())) {
            setFrom(static_cast<Round>(value));
        }
    };
    to_->on_changed = [this](const double value) {
        if (value >= 0.0 && value <= static_cast<double>(std::numeric_limits<Round>::max())) {
            setTo(static_cast<Round>(value));
        }
    };
    controls->add(from_);
    controls->add(std::make_shared<::cwxeditor::lib::Label>(config_.resources.roundSeparator));
    controls->add(to_);
    controls->add(std::make_shared<::cwxeditor::lib::Label>(
        formatRangeHint(config_.resources.rangeHint, config_.roundMax)));
    group->add(controls);
    area.add(group);
    auto footer = std::make_shared<::cwxeditor::lib::Panel>();
    footer->set_layout(std::make_shared<::cwxeditor::lib::LinearLayout>(
        ::cwxeditor::lib::Orientation::Horizontal));
    ok_button_ = std::make_shared<::cwxeditor::lib::Button>("OK");
    ok_button_->default_button = true;
    ok_button_->on_click = [this] {
        if (dialog_) dialog_->result = ::cwxeditor::lib::DialogResult::OK;
    };
    cancel_button_ = std::make_shared<::cwxeditor::lib::Button>("キャンセル");
    cancel_button_->cancel_button = true;
    cancel_button_->on_click = [this] {
        if (dialog_) dialog_->result = ::cwxeditor::lib::DialogResult::Cancel;
    };
    footer->add(ok_button_);
    footer->add(cancel_button_);
    area.add(footer);
    initialized_ = true;
    syncEditors();
}

bool ManyRoundsDialog::open() {
    if (!dialog_ || !config_.services.gui) return false;
    if (!initialized_) setup(*dialog_);
    auto owner = dialog_owner_;
    if (!owner && parent_) owner = std::shared_ptr<::cwxeditor::lib::Window>(
        parent_, [](::cwxeditor::lib::Window*) {});
    const auto result = config_.services.gui->show_modal(dialog_, owner);
    return close(result == ::cwxeditor::lib::DialogResult::OK);
}

bool ManyRoundsDialog::close(const bool ok) {
    ::cwxeditor::cwx::perf::TraceScope trace_close(__FILE__, __LINE__);
    accepted_ = ok;
    if (dialog_) dialog_->result = ok ? ::cwxeditor::lib::DialogResult::OK
                                     : ::cwxeditor::lib::DialogResult::Cancel;
    if (!ok) return false;
    if (from_value_ == 0U || to_value_ == 0U || from_value_ > to_value_) return true;
    for (Round value = from_value_;; ++value) {
        rounds_.push_back(value);
        if (value == to_value_) break;
    }
    return true;
}

void ManyRoundsDialog::syncEditors() {
    if (!from_ || !to_) return;
    from_->minimum = config_.roundMax == 0U ? 0.0 : 1.0;
    from_->maximum = static_cast<double>(config_.roundMax);
    from_->step = 1.0;
    from_->value = static_cast<double>(from_value_);
    to_->minimum = config_.roundMax == 0U ? 0.0 : 1.0;
    to_->maximum = static_cast<double>(config_.roundMax);
    to_->step = 1.0;
    to_->value = static_cast<double>(to_value_);
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::roundview
