#include "cwxeditor_src/core/cwx/perf.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dmenu.hpp"
#include "cwxeditor_src/lib/ui_window_lifecycle.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/history.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/xmlbytestransfer.hpp"
// UIの実装は一つに統一し、
// 別経路の実装を設けない。

#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/variables.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace cwxeditor::cwx::editor::gui::dwt::history {

namespace {

using String = lib::String;
using StringView = lib::StringView;
using Path = std::filesystem::path;

[[nodiscard]] Path fs_path(const StringView value) {
    return structs::pathFromUtf8(value);
}

[[nodiscard]] String utf8_path(const Path& value) {
    return structs::pathToUtf8(value);
}

[[nodiscard]] bool starts_with(StringView value, StringView prefix) noexcept {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

[[nodiscard]] String trim_history_whitespace(StringView value) {
    // D版の std.string.strip と同様、Unicode の White_Space
    // を使う。 UTF-8の列全体で照合し、継続バイト単位の判定やロケール依存
    // の isspace は使わない。 途中の空白と不正なバイト列は保持する。コ
    // ードポイントの集合は core/cwx/msgutils.cpp と共通。
    static constexpr StringView whitespace[] = {
        "\x09", "\x0A", "\x0B", "\x0C", "\x0D", "\x20",
        "\xC2\x85", "\xC2\xA0", "\xE1\x9A\x80",
        "\xE2\x80\x80", "\xE2\x80\x81", "\xE2\x80\x82",
        "\xE2\x80\x83", "\xE2\x80\x84", "\xE2\x80\x85",
        "\xE2\x80\x86", "\xE2\x80\x87", "\xE2\x80\x88",
        "\xE2\x80\x89", "\xE2\x80\x8A", "\xE2\x80\xA8",
        "\xE2\x80\xA9", "\xE2\x80\xAF", "\xE2\x81\x9F",
        "\xE3\x80\x80"
    };
    for (;;) {
        const auto found = std::find_if(std::begin(whitespace), std::end(whitespace),
            [value](StringView space) { return value.starts_with(space); });
        if (found == std::end(whitespace)) break;
        value.remove_prefix(found->size());
    }
    for (;;) {
        const auto found = std::find_if(std::begin(whitespace), std::end(whitespace),
            [value](StringView space) { return value.ends_with(space); });
        if (found == std::end(whitespace)) break;
        value.remove_suffix(found->size());
    }
    return String(value);
}

[[nodiscard]] String filename_identity(const lib::FileSystemBackend* filesystem,
                                       const StringView value) {
    const Path path = fs_path(value);
    return filesystem ? filesystem->filename_identity(path)
                      : utf8_path(path.lexically_normal());
}

[[nodiscard]] bool filename_equal(const lib::FileSystemBackend* filesystem,
                                  const StringView left,
                                  const StringView right) noexcept {
    try {
        return filesystem ? filesystem->filenames_equal(fs_path(left), fs_path(right))
                          : filename_identity(nullptr, left) ==
                                filename_identity(nullptr, right);
    } catch (...) {
        return false;
    }
}

[[nodiscard]] String normalized_absolute(StringView value) {
    if (value.empty()) return {};
    std::error_code error;
    const Path path = fs_path(value);
    const Path absolute = std::filesystem::absolute(path, error);
    if (error) return utf8_path(path.lexically_normal());
    return utf8_path(absolute.lexically_normal());
}

[[nodiscard]] String path_basename(StringView value) {
    if (value.empty()) return {};
    return utf8_path(fs_path(value).filename());
}

[[nodiscard]] String path_parent(StringView value) {
    if (value.empty()) return {};
    const auto parent = fs_path(value).parent_path();
    return parent.empty() ? String(".") : utf8_path(parent);
}

[[nodiscard]] String path_extension(StringView value) {
    if (value.empty()) return {};
    return utf8_path(fs_path(value).extension());
}

[[nodiscard]] bool is_name(const lib::FileSystemBackend* filesystem,
                           StringView value, StringView expected) noexcept {
    return filename_equal(filesystem, value, expected);
}

[[nodiscard]] bool is_existing(StringView value) {
    if (value.empty()) return false;
    std::error_code error;
    return std::filesystem::exists(fs_path(value), error);
}

[[nodiscard]] String lower_ascii(StringView value) {
    String result(value);
    for (char& character : result) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character + ('a' - 'A'));
        }
    }
    return result;
}

void replace_all(String& value, StringView needle, StringView replacement) {
    if (needle.empty()) return;
    std::size_t offset = 0;
    while ((offset = value.find(needle, offset)) != String::npos) {
        value.replace(offset, needle.size(), replacement);
        offset += replacement.size();
    }
}

[[nodiscard]] String format_warning(String format, std::size_t count,
                                    std::size_t maximum) {
    replace_all(format, "%1$s", std::to_string(count));
    replace_all(format, "%2$s", std::to_string(maximum));
    // 一部の翻訳カタログでは、明示的な $s 接尾辞の
    // ない位置指定形式を使う。 その形式にも対応し、古
    // い設定ファイルでも警告を表示できるようにする。
    replace_all(format, "%1", std::to_string(count));
    replace_all(format, "%2", std::to_string(maximum));
    return format;
}

[[nodiscard]] std::vector<std::byte> as_bytes(StringView value) {
    std::vector<std::byte> result;
    result.reserve(value.size());
    for (const unsigned char character : value) {
        result.push_back(static_cast<std::byte>(character));
    }
    return result;
}

[[nodiscard]] String from_bytes(const std::vector<std::byte>& value) {
    String result;
    result.reserve(value.size());
    for (const auto byte : value) {
        result.push_back(static_cast<char>(std::to_integer<unsigned char>(byte)));
    }
    return result;
}

[[nodiscard]] bool has_xml_clipboard(const lib::ClipboardData& data) {
    return xmlbytestransfer::CBisXML(data);
}

}  // namespace

HistorySettings historySettings(const ::cwxeditor::cwx::variables::FlexEtcProps& etc) {
    HistorySettings result;
    result.xmlCopy = etc.xmlCopy.value;
    const auto max_history = static_cast<std::int64_t>(etc.historyMax);
    const auto max_undo = static_cast<std::int64_t>(etc.undoMaxEtc);
    result.history_max = max_history > 0 ? static_cast<std::size_t>(max_history) : 0U;
    result.undo_max = max_undo > 0 ? static_cast<std::size_t>(max_undo) : 0U;
    result.remove_non_existing_with_priority =
        static_cast<bool>(etc.removeScenarioHistoryNonExistingWithPriority);
    result.name_column_width = etc.historyScenarioNameColumn;
    result.path_column_width = etc.historyScenarioPathColumn;
    return result;
}


std::vector<lib::MenuItem> historyContextMenuItems(
    std::function<lib::String(types::MenuID)> menu_text,
    std::function<lib::ResourceId(types::MenuID)> menu_image) {
    using MenuID = types::MenuID;
    struct Entry {
        MenuID id;
        std::string_view command;
        bool separator_before;
    };
    constexpr std::array entries{
        Entry{MenuID::OpenDir, "open", false},
        Entry{MenuID::Undo, "undo", true},
        Entry{MenuID::Redo, "redo", false},
        Entry{MenuID::Up, "up", true},
        Entry{MenuID::Down, "down", false},
        Entry{MenuID::Cut, "cut", true},
        Entry{MenuID::Copy, "copy", false},
        Entry{MenuID::Paste, "paste", false},
        Entry{MenuID::Delete, "delete", false},
        Entry{MenuID::CopyAsText, "copy-text", true},
        Entry{MenuID::SelectAll, "select-all", true},
        Entry{MenuID::DeleteNotExistsHistory, "delete-missing", true},
    };
    const auto fallback_text = [](const MenuID id) -> lib::String {
        switch (id) {
        case MenuID::OpenDir: return "開く";
        case MenuID::Undo: return "元に戻す";
        case MenuID::Redo: return "やり直し";
        case MenuID::Up: return "上へ";
        case MenuID::Down: return "下へ";
        case MenuID::Cut: return "切り取り";
        case MenuID::Copy: return "コピー";
        case MenuID::Paste: return "貼り付け";
        case MenuID::Delete: return "削除";
        case MenuID::CopyAsText: return "パスをコピー";
        case MenuID::SelectAll: return "すべて選択";
        case MenuID::DeleteNotExistsHistory: return "存在しない項目を削除";
        default: return {};
        }
    };
    std::vector<lib::MenuItem> result;
    result.reserve(entries.size() + 6U);
    for (const auto& entry : entries) {
        if (entry.separator_before) {
            lib::MenuItem separator;
            separator.separator = true;
            result.push_back(std::move(separator));
        }
        lib::MenuItem item;
        item.text = menu_text ? menu_text(entry.id) : fallback_text(entry.id);
        item.command = entry.command;
        item.icon = menu_image ? menu_image(entry.id) : lib::ResourceId{};
        result.push_back(std::move(item));
    }
    return result;
}

class ScenarioHistoryDialog::HUndo final
    : public ::cwxeditor::cwx::editor::gui::dwt::undo::Undo {
public:
    explicit HUndo(ScenarioHistoryDialog& owner) : owner_(&owner) { save(); }

    void undo() override { impl(); }
    void redo() override { impl(); }
    void dispose() override {
        ::cwxeditor::cwx::perf::TraceScope history_trace_88(__FILE__, __LINE__);}

private:
    void save() {
        ::cwxeditor::cwx::perf::TraceScope history_trace_70(__FILE__, __LINE__);
        selected_ = owner_->selected_;
        old_hist_ = owner_->hist_;
    }

    void impl() {
        ::cwxeditor::cwx::perf::TraceScope history_trace_74(__FILE__, __LINE__);
        auto selected = selected_;
        auto old_hist = old_hist_;
        save();
        owner_->replace_state(std::move(old_hist), std::move(selected));
    }

    ScenarioHistoryDialog* owner_{};
    std::vector<std::size_t> selected_;
    std::vector<HistoryEntry> old_hist_;
};

ScenarioHistoryDialog::ScenarioHistoryDialog(
    lib::Services services, std::shared_ptr<lib::Window> owner,
    lib::String title, lib::ResourceId icon, std::vector<OpenHistory> history,
    std::vector<OpenHistory> bookmark, HistorySettings settings,
    HistoryCallbacks callbacks)
    : services_(std::move(services)),
      owner_(std::move(owner)),
      title_(std::move(title)),
      icon_(icon),
      settings_(std::move(settings)),
      callbacks_(std::make_shared<HistoryCallbacks>(std::move(callbacks))),
      history_(std::move(history)),
      bookmark_(std::move(bookmark)),
      undo_(std::make_unique<
            ::cwxeditor::cwx::editor::gui::dwt::undo::UndoManager>(settings_.undo_max)) {
    hist_.reserve(bookmark_.size() + history_.size());
    for (const auto& value : bookmark_) hist_.emplace_back(value, true);
    for (const auto& value : history_) hist_.emplace_back(value, false);
    original_hist_ = hist_;
    refresh_warning();
}

ScenarioHistoryDialog::ScenarioHistoryDialog(
    lib::Services services, std::shared_ptr<lib::Window> owner,
    lib::String title, lib::ResourceId icon, std::vector<OpenHistory> history,
    std::vector<OpenHistory> bookmark,
    const ::cwxeditor::cwx::variables::FlexEtcProps& etc,
    HistoryCallbacks callbacks)
    : ScenarioHistoryDialog(std::move(services), std::move(owner),
                            std::move(title), icon, std::move(history),
                            std::move(bookmark), historySettings(etc),
                            std::move(callbacks)) {}

ScenarioHistoryDialog::~ScenarioHistoryDialog() {
    lifetime_.reset();
    if (dialog_) dialog_->on_close_requested = {};
    if (list_) {
        list_->on_selection_changed = {};
        list_->on_check_changed = {};
        list_->on_cell_activate = {};
        list_->on_pointer = {};
        list_->on_key_down = {};
    }
    if (context_menu_) context_menu_->on_command = {};
    if (ok_button_) ok_button_->on_click = {};
    if (cancel_button_) cancel_button_->on_click = {};
    if (apply_button_) apply_button_->on_click = {};
    if (services_.drag_drop) services_.drag_drop->unbind(list_);
}

bool ScenarioHistoryDialog::setup(lib::Container& area) {
    auto area_layout = std::make_shared<lib::GridLayout>();
    area_layout->columns = 1U;
    area_layout->spacing = 4.0F;
    area_layout->padding = {5.0F, 5.0F, 5.0F, 5.0F};
    area.set_layout(area_layout);

    if (!hint_) {
        hint_ = std::make_shared<lib::Label>(settings_.scenario_bookmark_hint);
        hint_->wrap = true;
        hint_->accessibility_name = "scenario history hint";
        hint_->set_layout_item({.row = 0U, .column = 0U,
                                .preferred = {300.0F, 18.0F}});
        area.add(hint_);
    }
    if (!list_) {
        list_ = std::make_shared<lib::TableView>();
        // Ctrl+X/C/V は個別に処理する。一覧の切り取り・コピー・貼り付けはポップアップメニューにしか
        // なく、 共通のメニュー検索ではショートカットを見つけられないため（D版の TCPD キーに相当）。
        list_->on_key_down = [this](lib::KeyEvent& event) {
            (void)dmenu::handleClipboardShortcut(event, {
                [this] { return canDoT(); }, [this] { return canDoC(); },
                [this] { return canDoP(); },
                [this] { cut(); }, [this] { copy(); }, [this] { (void)paste(); }});
        };
        list_->multi_select = true;
        list_->checkable = true;
        list_->check_selected_rows = true;
        list_->full_row_selection = true;
        // 共通の表示側は、ここで所有する行から表示用のスナップショットを読む。 virtualized
        // は互換性のための指定であり、ネイティブの所有者データ方式ではない。
        list_->virtualized = false;
        list_->edit_on_double_click = false;
        list_->columns = {
            {settings_.history_scenario_file_name,
             static_cast<float>(settings_.name_column_width)},
            {settings_.history_scenario_path,
             static_cast<float>(settings_.path_column_width)},
        };
        list_->accessibility_name = "scenario history";
        list_->set_layout_item({.row = 1U, .column = 0U,
                                .minimum = {300.0F, 160.0F},
                                .preferred = {450.0F, 220.0F}, .grow = 1.0F});
        list_->on_selection_changed = [this, weak = std::weak_ptr<void>(lifetime_)](
            const std::vector<std::size_t>& values) {
            const auto life = weak;
            auto* const owner = this;
            if (life.expired()) return;
            owner->sync_bookmarks_from_table();
            if (!life.expired()) owner->select(values);
        };
        list_->on_check_changed = [this, weak = std::weak_ptr<void>(lifetime_)](
            std::size_t, bool) {
            if (weak.expired() || !list_ || list_->rows.size() != hist_.size()) return;
            bool changed = false;
            // D版の CHECK と同様、選択全体を直ちに更新し、
            // 取り消し履歴は作らない。 表示側は通知前に一括反映す
            // るため、その後の行ごとのコールバックでは何もしない。
            for (std::size_t index = 0; index < hist_.size(); ++index) {
                const bool checked = list_->rows[index].checked;
                changed = changed || hist_[index].bookmark != checked;
                hist_[index].bookmark = checked;
            }
            if (changed) {
                refresh_warning();
                if (!weak.expired()) notify_changed();
            }
        };
        list_->on_cell_activate = [this](std::size_t, std::size_t) {
            open_selection();
        };
        context_menu_ = std::make_shared<lib::Menu>(true);
        context_menu_->items = settings_.context_menu_items.empty()
            ? historyContextMenuItems() : settings_.context_menu_items;
        context_menu_->on_command = [this](const std::string_view command) {
            if (command == "open") open_selection();
            else if (command == "undo") { if (can_undo()) (void)undo(); }
            else if (command == "redo") { if (can_redo()) (void)redo(); }
            else if (command == "up") up();
            else if (command == "down") down();
            else if (command == "cut") cut();
            else if (command == "copy") copy();
            else if (command == "paste") (void)paste();
            else if (command == "delete") del();
            else if (command == "copy-text") copy_as_text();
            else if (command == "select-all") {
                std::vector<std::size_t> indices(hist_.size());
                std::iota(indices.begin(), indices.end(), std::size_t{});
                select(std::move(indices));
            }
            else if (command == "delete-missing") del_not_exists();
            if (context_menu_ && context_menu_->on_open)
                context_menu_->on_open();
        };
        context_menu_->on_open = [this] {
            if (!context_menu_) return;
            for (auto& item : context_menu_->items) {
                if (item.command == "open") item.enabled = can_open_selection();
                else if (item.command == "undo") item.enabled = can_undo();
                else if (item.command == "redo") item.enabled = can_redo();
                else if (item.command == "up") item.enabled = can_up();
                else if (item.command == "down") item.enabled = can_down();
                else if (item.command == "cut" || item.command == "copy" ||
                         item.command == "copy-text") item.enabled = can_do_C();
                else if (item.command == "paste") item.enabled = can_do_P();
                else if (item.command == "delete") item.enabled = can_do_D();
                else if (item.command == "select-all")
                    item.enabled = !hist_.empty() && selected_.size() < hist_.size();
                else if (item.command == "delete-missing")
                    item.enabled = !hist_.empty();
            }
            ++context_menu_->item_update_revision;
        };
        context_menu_->on_open();
        list_->on_pointer = [this](lib::PointerEvent& event) {
            if (event.button != lib::PointerButton::Secondary ||
                event.action != lib::PointerAction::Up || !services_.gui)
                return;
            if (context_menu_->on_open) context_menu_->on_open();
            services_.gui->show_context_menu(context_menu_, list_, event.position);
            event.handled = true;
        };
        area.add(list_);
        if (services_.drag_drop) {
            lib::DragSourceBinding source;
            source.source = list_;
            source.allowed = lib::DropOperation::Copy | lib::DropOperation::Move;
            source.provide_data = [this] {
                lib::ClipboardData data;
                if (!drag_state_.begin(*list_, selected_)) return data;
                const auto serialized = create_node(selected_).text();
                data.text = serialized;
                data.formats.emplace(String(OPEN_HISTORIES_MIME),
                                     as_bytes(serialized));
                return data;
            };
            source.provide_origin = [this] { return drag_state_.origin(); };
            source.completed = [this](lib::DropOperation) { drag_state_.finish(); };
            services_.drag_drop->bind_source(std::move(source));

            lib::DropTargetBinding target;
            target.target = list_;
            target.pane_id = drag_state_.paneId();
            target.accepts_target_index = [this](std::optional<std::size_t> index) {
                return !index || *index <= hist_.size();
            };
            target.accepted_formats = {String(OPEN_HISTORIES_MIME),
                                       "text/xml", "text/uri-list"};
            const auto negotiate = [](lib::DragEvent& event) {
                ::cwxeditor::cwx::perf::TraceScope history_trace_116(__FILE__, __LINE__);
                event.selected = event.same_pane() ? lib::DropOperation::Move
                                                   : lib::DropOperation::Copy;
            };
            target.on_enter = negotiate;
            target.on_over = negotiate;
            target.on_drop = [this](lib::DragEvent& event) {
                ::cwxeditor::cwx::perf::TraceScope history_trace_125(__FILE__, __LINE__);
                event.selected = lib::DropOperation::None;
                event.handled = false;
                if (event.same_pane()) {
                    ::cwxeditor::cwx::perf::TraceScope history_trace_127(__FILE__, __LINE__);
                    const auto anchor = drag_state_.anchor(event, *list_, selected_);
                    if (!anchor) return;
                    const auto target = event.target_index.value_or(hist_.size());
                    if (target == *anchor || target > hist_.size()) return;
                    store();
                    if (target < *anchor) up_impl(*anchor - target);
                    else down_impl(target - *anchor);
                    refresh_warning();
                    notify_changed();
                    event.handled = true;
                    event.selected = lib::DropOperation::Move;
                    return;
                }
                bool accepted = false;
                if (!event.data.files.empty()) {
                    ::cwxeditor::cwx::perf::TraceScope history_trace_146(__FILE__, __LINE__);
                    // D版と同様、以前の履歴選択とは独立して、ドロップ先として
                    // 指している行に挿入する。 空白部分なら末尾に追加する。
                    const auto index = event.target_index.value_or(hist_.size());
                    accepted = drop_files(event.data.files, index);
                } else if (const auto found = event.data.formats.find(
                               String(OPEN_HISTORIES_MIME));
                           found != event.data.formats.end()) {
                    accepted = paste_xml(from_bytes(found->second));
                } else if (!event.data.text.empty()) {
                    accepted = paste_xml(event.data.text);
                }
                event.handled = accepted;
                if (accepted) event.selected = lib::DropOperation::Copy;
            };
            services_.drag_drop->bind_target(std::move(target));
        }
    }
    if (!ok_button_) {
        auto separator = std::make_shared<lib::Separator>();
        separator->orientation = lib::Orientation::Horizontal;
        separator->set_layout_item({.row = 2U, .column = 0U,
                                    .preferred = {450.0F, 1.0F}});
        area.add(separator);

        auto footer = std::make_shared<lib::Panel>();
        auto footer_layout = std::make_shared<lib::GridLayout>();
        footer_layout->columns = 4U;
        footer_layout->spacing = 8.0F;
        footer->set_layout(footer_layout);
        footer->set_layout_item({.row = 3U, .column = 0U,
                                 .preferred = {450.0F, 30.0F}});

        auto spacer = std::make_shared<lib::Panel>();
        spacer->set_layout_item({.row = 0U, .column = 0U, .grow = 1.0F});
        footer->add(spacer);

        ok_button_ = std::make_shared<lib::Button>(settings_.ok_text);
        ok_button_->default_button = true;
        ok_button_->set_layout_item({.row = 0U, .column = 1U,
                                     .preferred = {96.0F, 24.0F}});
        ok_button_->on_click = [this, weak = std::weak_ptr<void>(lifetime_)] {
            const auto life = weak;
            auto* const owner = this;
            if (life.expired() || !owner->applyEditDialog() || life.expired()) return;
            if (owner->dialog_) owner->dialog_->result = lib::DialogResult::OK;
            owner->close();
        };
        cancel_button_ = std::make_shared<lib::Button>(settings_.cancel_text);
        cancel_button_->cancel_button = true;
        cancel_button_->set_layout_item({.row = 0U, .column = 2U,
                                         .preferred = {96.0F, 24.0F}});
        cancel_button_->on_click = [this] { cancel(); };

        apply_button_ = std::make_shared<lib::Button>(settings_.apply_text);
        apply_button_->set_enabled(false);
        apply_button_->set_layout_item({.row = 0U, .column = 3U,
                                        .preferred = {96.0F, 24.0F}});
        apply_button_->on_click = [this, weak = std::weak_ptr<void>(lifetime_)] {
            const auto life = weak;
            auto* const owner = this;
            if (life.expired() || !owner->applyEditDialog() || life.expired()) return;
            if (owner->dialog_) owner->dialog_->result = lib::DialogResult::Apply;
        };
        footer->add(ok_button_);
        footer->add(cancel_button_);
        footer->add(apply_button_);
        area.add(footer);
    }
    render();
    return true;
}

bool ScenarioHistoryDialog::setup() {
    if (!services_.gui) return false;
    if (!dialog_) dialog_ = services_.gui->create_dialog();
    if (!dialog_) return false;
    dialog_->title = title_;
    dialog_->icon = icon_;
    // ウィンドウの位置・寸法を通知する前に、子のツリーをすべて
    // 構築する。 境界の変更はネイティブ側の同期処理から観測さ
    // れるため、 このモードレスダイアログの構築の最後に行う。
    if (!setup(*dialog_)) return false;
    // モードレスのSWTウィンドウを閉じる操作は、暗黙の適用ではなくキャンセ
    // ルとして扱う。 移植版の所有者も同じ経路を使い、ユーザーが明示的に適
    // 用しない限り、 ローカルの編集を破棄して保存コールバックを呼ばない。
    dialog_->on_close_requested = [this] { cancel(); };
    // DialogParam はD版・SWTのウィンドウ全体の寸法を保存する。共通の Window
    // モデルは クライアント領域の寸法を使うため、作成前に標準のWin32の枠の分を差し引く。
    const lib::Size client_size{
        std::max(320.0F, settings_.dialog_size.width - 16.0F),
        std::max(220.0F, settings_.dialog_size.height - 39.0F)};
    lib::configure_owned_window(dialog_, owner_,
        lib::OwnedWindowKind::Modeless);
    dialog_->set_bounds({0.0F, 0.0F, client_size.width, client_size.height});
    return true;
}

void ScenarioHistoryDialog::show() {
    if (!dialog_ && !setup()) return;
    if (!dialog_ || !services_.gui) return;
    dialog_->result = lib::DialogResult::None;
    services_.gui->show(dialog_);
}

void ScenarioHistoryDialog::close() {
    if (dialog_ && services_.gui) services_.gui->close(dialog_);
}

void ScenarioHistoryDialog::cancel() {
    replace_state(original_hist_, {});
    if (dialog_) dialog_->result = lib::DialogResult::Cancel;
    close();
}

void ScenarioHistoryDialog::sync_bookmarks_from_table() {
    const std::weak_ptr<void> life = lifetime_;
    if (!list_ || list_->rows.size() != hist_.size()) return;
    bool changed = false;
    for (std::size_t index = 0; index < hist_.size(); ++index)
        changed = changed || hist_[index].bookmark != list_->rows[index].checked;
    if (!changed) return;
    store();
    for (std::size_t index = 0; index < hist_.size(); ++index)
        hist_[index].bookmark = list_->rows[index].checked;
    refresh_warning();
    if (!life.expired()) notify_changed();
}

void ScenarioHistoryDialog::update_live_settings(const std::size_t historyMax,
    const bool priority, const std::size_t undoMax) {
    const bool historyChanged = settings_.history_max != historyMax;
    settings_.history_max = historyMax;
    settings_.remove_non_existing_with_priority = priority;
    settings_.undo_max = undoMax;
    ref_undo_max();
    if (historyChanged) {
        history_ = removeHistoryNonExisting(settings_, priority, std::move(history_), historyMax);
        const auto ordinary = static_cast<std::size_t>(std::count_if(hist_.begin(), hist_.end(),
            [](const auto& entry) { return !entry.bookmark; }));
        if (apply_button_ && history_.size() < historyMax && history_.size() < ordinary)
            apply_button_->set_enabled(true);
        refresh_warning();
    }
    if (callbacks_->toolbar_refresh) callbacks_->toolbar_refresh();
}

void ScenarioHistoryDialog::set_settings(HistorySettings settings) {
    settings_ = std::move(settings);
    ref_undo_max();
    ref_history_max();
    render();
    refresh_warning();
}

void ScenarioHistoryDialog::select(std::vector<std::size_t> indices) {
    indices.erase(std::remove_if(indices.begin(), indices.end(),
                                 [this](const auto index) {
                                     return index >= hist_.size();
                                 }),
                  indices.end());
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    selected_ = std::move(indices);
    if (list_) (void)lib::select_table_rows(*list_, selected_, false, false);
}

void ScenarioHistoryDialog::set_bookmark(const std::size_t index, const bool value) {
    if (index >= hist_.size() || hist_[index].bookmark == value) return;
    store();
    hist_[index].bookmark = value;
    render();
    refresh_warning();
    notify_changed();
}

void ScenarioHistoryDialog::refresh_warning() {
    ::cwxeditor::cwx::perf::TraceScope history_trace_38(__FILE__, __LINE__);
    warnings_.clear();
    const auto count = static_cast<std::size_t>(std::count_if(
        hist_.begin(), hist_.end(), [](const HistoryEntry& value) {
            return !value.bookmark;
        }));
    if (settings_.history_max < count) {
        ::cwxeditor::cwx::perf::TraceScope history_trace_41(__FILE__, __LINE__);
        warnings_.push_back(format_warning(settings_.warning_history_too_many,
                                            count, settings_.history_max));
    }
    const auto callbacks = callbacks_;
    if (callbacks->warning_changed) {
        const auto warnings = warnings_;
        callbacks->warning_changed(warnings);
    }
}

void ScenarioHistoryDialog::render() {
    if (!list_) return;
    std::vector<lib::TableRow> rows;
    rows.reserve(hist_.size());
    for (const auto& entry : hist_) {
        const auto path = fullHistToHist(entry.hist.path);
        const auto display = to_s_file_name(path);
        lib::TableRow row;
        row.cells.resize(2);
        row.cells[0].text = path_basename(display);
        row.cells[0].icon = historyImage(settings_.images, path,
                                         services_.filesystem.get());
        row.cells[1].text = normalized_absolute(display);
        row.checked = entry.bookmark;
        row.cells[0].checked = entry.bookmark;
        rows.push_back(std::move(row));
    }
    const auto target=list_;
    (void)lib::replace_table_rows(*target, std::move(rows), selected_);
}

void ScenarioHistoryDialog::notify_changed(const bool toolbar) {
    const std::weak_ptr<void> life = lifetime_;
    const auto callbacks = callbacks_;
    if (apply_button_) apply_button_->set_enabled(true);
    if (callbacks->changed) callbacks->changed();
    if (life.expired()) return;
    if (toolbar && callbacks->toolbar_refresh) callbacks->toolbar_refresh();
}

void ScenarioHistoryDialog::replace_state(std::vector<HistoryEntry> value,
                                          std::vector<std::size_t> selected) {
    hist_ = std::move(value);
    selected_ = std::move(selected);
    render();
    // select() でも無効なインデックスを除外する。UndoManager
    // のコマンド実行中は、 表のコールバックを二重に呼ばないようにする。
    selected_.erase(std::remove_if(selected_.begin(), selected_.end(),
                                   [this](const auto index) {
                                       return index >= hist_.size();
                                   }),
                    selected_.end());
    std::sort(selected_.begin(), selected_.end());
    selected_.erase(std::unique(selected_.begin(), selected_.end()),
                    selected_.end());
    if (list_) (void)lib::select_table_rows(*list_, selected_, false, false);
    refresh_warning();
    notify_changed();
}

bool ScenarioHistoryDialog::can_open_selection() const {
    ::cwxeditor::cwx::perf::TraceScope history_trace_333(__FILE__, __LINE__);
    return std::any_of(selected_.begin(), selected_.end(), [this](const auto index) {
        if (index >= hist_.size()) return false;
        return is_existing(to_s_file_name(fullHistToHist(hist_[index].hist.path)));
    });
}

void ScenarioHistoryDialog::open_selection() {
    ::cwxeditor::cwx::perf::TraceScope history_trace_342(__FILE__, __LINE__);
    for (const auto index : selected_) {
        ::cwxeditor::cwx::perf::TraceScope history_trace_343(__FILE__, __LINE__);
        if (index >= hist_.size()) continue;
        const Path path(to_s_file_name(fullHistToHist(hist_[index].hist.path)));
        std::error_code error;
        if (!std::filesystem::exists(path, error)) continue;
        if (callbacks_->open_path) {
            callbacks_->open_path(path);
        } else if (services_.shell) {
            String ignored;
            services_.shell->reveal_path(path, ignored);
        }
    }
}

bool ScenarioHistoryDialog::can_up() const noexcept {
    ::cwxeditor::cwx::perf::TraceScope history_trace_352(__FILE__, __LINE__);
    return !selected_.empty() && *std::min_element(selected_.begin(), selected_.end()) > 0;
}

void ScenarioHistoryDialog::up() {
    ::cwxeditor::cwx::perf::TraceScope history_trace_355(__FILE__, __LINE__);
    if (!can_up()) return;
    store();
    up_impl(1);
    notify_changed();
}

void ScenarioHistoryDialog::up_impl(const std::size_t count) {
    ::cwxeditor::cwx::perf::TraceScope history_trace_362(__FILE__, __LINE__);
    auto indices = selected_;
    std::sort(indices.begin(), indices.end());
    for (std::size_t move = 0; move < count && !indices.empty(); ++move) {
        ::cwxeditor::cwx::perf::TraceScope history_trace_365(__FILE__, __LINE__);
        if (indices.front() == 0) break;
        for (const auto index : indices) std::swap(hist_[index - 1], hist_[index]);
        for (auto& index : indices) --index;
    }
    select(std::move(indices));
    render();
}

bool ScenarioHistoryDialog::can_down() const noexcept {
    ::cwxeditor::cwx::perf::TraceScope history_trace_377(__FILE__, __LINE__);
    return !selected_.empty() &&
           *std::max_element(selected_.begin(), selected_.end()) + 1U < hist_.size();
}

void ScenarioHistoryDialog::down() {
    ::cwxeditor::cwx::perf::TraceScope history_trace_380(__FILE__, __LINE__);
    if (!can_down()) return;
    store();
    down_impl(1);
    notify_changed();
}

void ScenarioHistoryDialog::down_impl(const std::size_t count) {
    ::cwxeditor::cwx::perf::TraceScope history_trace_387(__FILE__, __LINE__);
    auto indices = selected_;
    std::sort(indices.begin(), indices.end());
    for (std::size_t move = 0; move < count && !indices.empty(); ++move) {
        ::cwxeditor::cwx::perf::TraceScope history_trace_390(__FILE__, __LINE__);
        if (indices.back() + 1U >= hist_.size()) break;
        for (auto it = indices.rbegin(); it != indices.rend(); ++it) {
            ::cwxeditor::cwx::perf::TraceScope history_trace_392(__FILE__, __LINE__);
            std::swap(hist_[*it], hist_[*it + 1U]);
        }
        for (auto& index : indices) ++index;
    }
    select(std::move(indices));
    render();
}

bool ScenarioHistoryDialog::can_undo() const noexcept {
    return undo_ && undo_->canUndo();
}

bool ScenarioHistoryDialog::can_redo() const noexcept {
    return undo_ && undo_->canRedo();
}

bool ScenarioHistoryDialog::undo() {
    return undo_ && undo_->undo();
}

bool ScenarioHistoryDialog::redo() {
    return undo_ && undo_->redo();
}

void ScenarioHistoryDialog::ref_undo_max() {
    if (undo_) undo_->max(settings_.undo_max);
}

void ScenarioHistoryDialog::ref_history_max() {
    ::cwxeditor::cwx::perf::TraceScope history_trace_532(__FILE__, __LINE__);
    history_ = removeHistoryNonExisting(
        settings_, settings_.remove_non_existing_with_priority, std::move(history_),
        settings_.history_max);
    refresh_warning();
    notify_changed(false);
}

void ScenarioHistoryDialog::store() {
    ::cwxeditor::cwx::perf::TraceScope history_trace_92(__FILE__, __LINE__);
    if (undo_) undo_->add(std::make_shared<HUndo>(*this));
}

XNode ScenarioHistoryDialog::create_node(
    const std::span<const std::size_t> indices) const {
        ::cwxeditor::cwx::perf::TraceScope history_trace_401(__FILE__, __LINE__);
    auto document = XNode::create("openHistories");
    for (const auto index : indices) {
        ::cwxeditor::cwx::perf::TraceScope history_trace_403(__FILE__, __LINE__);
        if (index >= hist_.size()) continue;
        auto element = hist_[index].hist.toNode(document);
        if (hist_[index].bookmark) element.newAttr("bookmark", true);
    }
    return document;
}

bool ScenarioHistoryDialog::can_do_C() const noexcept {
    return services_.clipboard && !selected_.empty();
}

bool ScenarioHistoryDialog::can_do_P() const {
    if (!services_.clipboard) return false;
    const auto data = services_.clipboard->read();
    return has_xml_clipboard(data);
}

bool ScenarioHistoryDialog::can_do_D() const noexcept {
    return !selected_.empty();
}

void ScenarioHistoryDialog::copy() {
    ::cwxeditor::cwx::perf::TraceScope history_trace_416(__FILE__, __LINE__);
    if (!can_do_C() || !services_.clipboard) return;
    const auto serialized = create_node(selected_).text();
    const bool asText = settings_.xmlCopyProvider ? settings_.xmlCopyProvider() : settings_.xmlCopy;
    services_.clipboard->write(asText ? xmlbytestransfer::makeTextPayload(serialized)
                                      : xmlbytestransfer::makeXMLPayload(serialized));
    if (callbacks_->toolbar_refresh) callbacks_->toolbar_refresh();
}

void ScenarioHistoryDialog::cut() {
    ::cwxeditor::cwx::perf::TraceScope history_trace_412(__FILE__, __LINE__);
    if (!can_do_C()) return;
    copy();
    del();
}

bool ScenarioHistoryDialog::paste() {
    ::cwxeditor::cwx::perf::TraceScope history_trace_422(__FILE__, __LINE__);
    if (!services_.clipboard) return false;
    const auto data = services_.clipboard->read();
    const auto serialized = xmlbytestransfer::CBtoXML(data);
    return !serialized.empty() && paste_xml(serialized);
}

void ScenarioHistoryDialog::del() {
    ::cwxeditor::cwx::perf::TraceScope history_trace_492(__FILE__, __LINE__);
    if (selected_.empty()) return;
    store();
    for (auto it = selected_.rbegin(); it != selected_.rend(); ++it) {
        ::cwxeditor::cwx::perf::TraceScope history_trace_496(__FILE__, __LINE__);
        if (*it < hist_.size()) hist_.erase(hist_.begin() + static_cast<std::ptrdiff_t>(*it));
    }
    selected_.clear();
    render();
    refresh_warning();
    notify_changed();
}

void ScenarioHistoryDialog::clone() {
    // D版では、このダイアログの複製操作は意図的に assert(false) となっている。
}

bool ScenarioHistoryDialog::paste_xml(const std::string_view serialized) {
    try {
        return paste_impl(XNode::parse(serialized));
    } catch (const std::exception&) {
        return false;
    }
}

bool ScenarioHistoryDialog::paste_impl(XNode node) {
    ::cwxeditor::cwx::perf::TraceScope history_trace_445(__FILE__, __LINE__);
    if (!node.valid() || node.name() != "openHistories") return false;
    std::vector<HistoryEntry> added;
    std::vector<std::size_t> selected;
    std::vector<std::pair<String, std::size_t>> known;
    known.reserve(hist_.size());
    // 貼り付けの重複判定には、履歴のpathを
    // 正規化したファイル名の識別情報を使う。
    const auto paste_key = [this](const OpenHistory& value) {
        return filename_identity(services_.filesystem.get(),
                                 normalized_absolute(value.path));
    };
    // D版と同様、既存キーのマップには最後の既存インデックスを保持する。 取り
    // 込むキーは一度だけ追加し、最初のデータと新しいインデックスを保持する。
    for (std::size_t index = hist_.size(); index-- > 0;) {
        known.emplace_back(paste_key(hist_[index].hist), index);
    }
    node.onTag[OpenHistory::XML_NAME.data()] =
        [this, &added, &selected, &known, &paste_key](XNode& element) {
            ::cwxeditor::cwx::perf::TraceScope history_trace_453(__FILE__, __LINE__);
            OpenHistory value;
            value.fromNode(element);
            const auto key = paste_key(value);
            const auto found = std::find_if(
                known.begin(), known.end(), [this, &key](const auto& item) {
                    return filename_equal(services_.filesystem.get(), item.first, key);
                });
            if (found != known.end()) {
                ::cwxeditor::cwx::perf::TraceScope history_trace_457(__FILE__, __LINE__);
                selected.push_back(found->second);
                return;
            }
            const auto index = hist_.size() + added.size();
            const bool marked = element.attr("bookmark", false, false);
            added.emplace_back(std::move(value), marked);
            selected.push_back(index);
            known.emplace_back(key, index);
        };
    node.parse();
    if (added.empty()) return false;

    store();
    hist_.insert(hist_.end(), std::make_move_iterator(added.begin()),
                 std::make_move_iterator(added.end()));
    select(std::move(selected));
    render();
    // 単独版の履歴一覧と同様、追加後に showSelection で選択行を表示する。
    if (!lib::editor_window_host_options().all_mode && list_ && !selected_.empty())
        (void)lib::reveal_table_row(*list_, selected_.front());
    refresh_warning();
    notify_changed();
    return true;
}

bool ScenarioHistoryDialog::drop_files(
    const std::vector<std::filesystem::path>& files, std::size_t drop_index) {
    std::vector<String> known;
    known.reserve(hist_.size());
    for (const auto& entry : hist_) known.push_back(to_key_file_name(entry.hist));

    std::vector<HistoryEntry> added;
    for (const auto& original : files) {
        ::cwxeditor::cwx::perf::TraceScope history_trace_158(__FILE__, __LINE__);
        std::error_code error;
        if (!std::filesystem::exists(original, error)) continue;
        Path file = original;
        if (std::filesystem::is_directory(file, error)) {
            ::cwxeditor::cwx::perf::TraceScope history_trace_160(__FILE__, __LINE__);
            const Path summary_xml = file / "Summary.xml";
            const Path summary_wsm = file / "Summary.wsm";
            if (std::filesystem::exists(summary_xml, error)) file = summary_xml;
            else if (std::filesystem::exists(summary_wsm, error)) file = summary_wsm;
            else continue;
        }
        OpenHistory value;
        value.path = normalized_absolute(utf8_path(file));
        const auto key = to_key_file_name(value);
        if (std::find_if(known.begin(), known.end(), [this, &key](const auto& item) {
                return filename_equal(services_.filesystem.get(), item, key);
            }) != known.end()) {
            continue;
        }
        known.push_back(key);
        added.emplace_back(std::move(value), false);
    }
    if (added.empty()) return false;

    drop_index = std::min(drop_index, hist_.size());
    store();
    hist_.insert(hist_.begin() + static_cast<std::ptrdiff_t>(drop_index),
                 std::make_move_iterator(added.begin()),
                 std::make_move_iterator(added.end()));
    select({drop_index});
    render();
    refresh_warning();
    notify_changed();
    return true;
}

void ScenarioHistoryDialog::copy_as_text() {
    ::cwxeditor::cwx::perf::TraceScope history_trace_481(__FILE__, __LINE__);
    if (selected_.empty() || !services_.clipboard) return;
    String text;
    for (const auto index : selected_) {
        ::cwxeditor::cwx::perf::TraceScope history_trace_483(__FILE__, __LINE__);
        if (index >= hist_.size()) continue;
        if (!text.empty()) text.push_back('\n');
        text += to_s_file_name(fullHistToHist(hist_[index].hist.path));
    }
    if (text.empty()) return;
    lib::ClipboardData data;
    data.text = std::move(text);
    services_.clipboard->write(data);
    if (callbacks_->toolbar_refresh) callbacks_->toolbar_refresh();
}

void ScenarioHistoryDialog::del_not_exists() {
    std::vector<HistoryEntry> kept;
    kept.reserve(hist_.size());
    for (const auto& entry : hist_) {
        if (!exists(entry.hist)) continue;
        kept.push_back(entry);
    }
    if (kept.size() == hist_.size()) return;

    // 選択行より前のファイルを削除した後は、古いインデック
    // スではなく 対象の識別情報に基づいて選択を組み直す。
    std::vector<std::size_t> selected;
    for (std::size_t old = 0; old < hist_.size(); ++old) {
        if (std::find(selected_.begin(), selected_.end(), old) == selected_.end()) continue;
        const auto& target = hist_[old];
        for (std::size_t next = 0; next < kept.size(); ++next) {
            const auto& value = kept[next];
            // D版と同様、ファイル名だけでなく、ブックマークとスキンの
            // 全付加情報を含む Hist の値全体で選択を復元する。
            if (value.bookmark == target.bookmark &&
                value.hist.path == target.hist.path &&
                value.hist.skinType == target.hist.skinType &&
                value.hist.skinName == target.hist.skinName &&
                value.hist.skinEngine == target.hist.skinEngine)
                selected.push_back(next);
        }
    }
    store();
    hist_ = std::move(kept);
    select(std::move(selected));
    render();
    refresh_warning();
    notify_changed();
}

bool ScenarioHistoryDialog::applyEditDialog() {
    const std::weak_ptr<void> life = lifetime_;
    sync_bookmarks_from_table();
    if (life.expired()) return false;
    if (!apply()) return false;
    if (life.expired()) return true;
    // D版の AbsDialog::forceApply と同様、適用コールバックが完了してか
    // ら pending を解除する。 これは、その後のシナリオ保存の成否とは独立している。
    original_hist_ = hist_;
    if (apply_button_) apply_button_->set_enabled(false);
    return true;
}

bool ScenarioHistoryDialog::apply() {
    ::cwxeditor::cwx::perf::TraceScope history_trace_541(__FILE__, __LINE__);
    bookmark_.clear();
    history_.clear();
    for (const auto& entry : hist_) {
        if (entry.bookmark) bookmark_.push_back(entry.hist);
        else history_.push_back(entry.hist);
    }
    history_ = removeHistoryNonExisting(
        settings_, settings_.remove_non_existing_with_priority, std::move(history_),
        settings_.history_max);
    // 通知コールバックが、このモデルを同期的に無効化する場合があ
    // る。 実行中の関数とその値引数を独立して保持し、 無効化さ
    // れる可能性のある呼び出しの後は、別のメンバーを取得しない。
    const std::weak_ptr<void> life = lifetime_;
    const auto callbacks = callbacks_;
    const auto histories = history_;
    const auto bookmarks = bookmark_;
    if (callbacks->persisted) callbacks->persisted(histories, bookmarks);
    if (life.expired()) return true;
    if (callbacks->changed) callbacks->changed();
    return true;
}

bool ScenarioHistoryDialog::exists(const OpenHistory& value) {
    return is_existing(fullHistToHist(value.path));
}

lib::String ScenarioHistoryDialog::to_s_file_name(const lib::StringView path) const {
    ::cwxeditor::cwx::perf::TraceScope history_trace_320(__FILE__, __LINE__);
    const auto name = path_basename(path);
    if (is_name(services_.filesystem.get(), name, "Summary.xml") ||
        is_name(services_.filesystem.get(), name, "Summary.wsm")) {
        return path_parent(path);
    }
    return String(path);
}

lib::String ScenarioHistoryDialog::to_key_file_name(
    const OpenHistory& history) const {
    return filename_identity(services_.filesystem.get(),
                             normalized_absolute(fullHistToHist(history.path)));
}

lib::ResourceId historyImage(const HistoryImageIds& images,
                             const lib::StringView hist,
                             const lib::FileSystemBackend* filesystem) {
    if (!is_existing(hist)) return images.warning;
    const auto extension = lower_ascii(path_extension(hist));
    const auto name = path_basename(hist);
    if (is_name(filesystem, name, "Summary.xml")) return images.summary_file;
    if (extension == ".wsn" || extension == ".cab" || extension == ".zip" ||
        extension == ".lzh" || extension == ".lha") {
        return images.scenario_archive;
    }
    if (is_name(filesystem, name, "Summary.wsm")) return images.classic;
    return images.unknown;
}

lib::String fullHistToHist(const lib::StringView hist) {
    ::cwxeditor::cwx::perf::TraceScope history_trace_566(__FILE__, __LINE__);
    if (!starts_with(hist, "\"")) return String(hist);
    const auto end = hist.find('"', 1U);
    if (end == StringView::npos) return String(hist);
    return String(hist.substr(1U, end - 1U));
}

lib::Array<lib::String> fullHistToCWXPaths(const lib::StringView hist) {
    ::cwxeditor::cwx::perf::TraceScope history_trace_581(__FILE__, __LINE__);
    if (!starts_with(hist, "\"")) return {};
    const auto end = hist.find('"', 1U);
    if (end == StringView::npos || end + 1U >= hist.size()) return {};
    const auto tail = trim_history_whitespace(hist.substr(end + 1U));
    if (tail.empty()) return {};
    lib::Array<String> result;
    std::size_t start = 0;
    while (start <= tail.size()) {
        const auto separator = tail.find('&', start);
        const auto finish = separator == StringView::npos ? tail.size() : separator;
        result.emplace_back(tail.substr(start, finish - start));
        if (separator == StringView::npos) break;
        start = separator + 1U;
    }
    return result;
}

lib::String createHistString(const ::cwxeditor::cwx::summary::Summary* summary) {
    ::cwxeditor::cwx::perf::TraceScope history_trace_596(__FILE__, __LINE__);
    if (!summary) return {};
    String hist;
    const auto& read_only = summary->readOnlyPath();
    if (!read_only.empty()) {
        ::cwxeditor::cwx::perf::TraceScope history_trace_599(__FILE__, __LINE__);
        if (!is_existing(read_only)) return {};
        std::error_code error;
        if (std::filesystem::is_directory(fs_path(read_only), error)) {
            ::cwxeditor::cwx::perf::TraceScope history_trace_601(__FILE__, __LINE__);
            hist = utf8_path(fs_path(read_only) / "Summary.wsm");
        } else {
            ::cwxeditor::cwx::perf::TraceScope history_trace_603(__FILE__, __LINE__);
            hist = read_only;
        }
    } else if (summary->legacy()) {
        ::cwxeditor::cwx::perf::TraceScope history_trace_606(__FILE__, __LINE__);
        if (summary->useTemp()) {
            ::cwxeditor::cwx::perf::TraceScope history_trace_607(__FILE__, __LINE__);
            hist = summary->origZipName();
        } else {
            ::cwxeditor::cwx::perf::TraceScope history_trace_609(__FILE__, __LINE__);
            hist = utf8_path(fs_path(summary->scenarioPath()) / "Summary.wsm");
        }
    } else if (summary->useTemp()) {
        ::cwxeditor::cwx::perf::TraceScope history_trace_612(__FILE__, __LINE__);
        hist = summary->origZipName();
        if (hist.empty()) return {};
    } else {
        ::cwxeditor::cwx::perf::TraceScope history_trace_615(__FILE__, __LINE__);
        hist = utf8_path(fs_path(summary->scenarioPath()) / "Summary.xml");
    }
    return normalized_absolute(hist);
}

OpenHistory findHistory(const std::span<const OpenHistory> bookmarks,
                        const std::span<const OpenHistory> histories,
                        const lib::StringView hist,
                        const lib::FileSystemBackend* filesystem) {
                            ::cwxeditor::cwx::perf::TraceScope history_trace_621(__FILE__, __LINE__);
    // D版の history.d と同様、. や .. を正規化せず、ファイル名の表記を比較する。 ファイルシステム上
    // で同一かどうかの判定では広すぎる。 先にある別名の記録が、別の記録に保存されたペインを取り込まないようにする。
    (void)filesystem;
    if (hist.empty()) return {};
    for (const auto& value : bookmarks) {
        ::cwxeditor::cwx::perf::TraceScope history_trace_623(__FILE__, __LINE__);
        if (::cwxeditor::cwx::utils::cfnmatch(fullHistToHist(value.path), hist)) return value;
    }
    for (const auto& value : histories) {
        ::cwxeditor::cwx::perf::TraceScope history_trace_628(__FILE__, __LINE__);
        if (::cwxeditor::cwx::utils::cfnmatch(fullHistToHist(value.path), hist)) return value;
    }
    return {};
}

void addHistory(const HistorySettings& settings, OpenHistory hist,
                std::vector<OpenHistory>& open_histories,
                std::vector<OpenHistory>& bookmarks,
                const lib::FileSystemBackend* filesystem) {
                    ::cwxeditor::cwx::perf::TraceScope history_trace_636(__FILE__, __LINE__);
    if (hist.path.empty()) return;
    const auto path = fullHistToHist(hist.path);
    if (settings.history_max == 0U) {
        ::cwxeditor::cwx::perf::TraceScope history_trace_639(__FILE__, __LINE__);
        open_histories.clear();
        return;
    }
    const bool bookmarked = std::any_of(
        bookmarks.begin(), bookmarks.end(), [filesystem, &path](const OpenHistory& value) {
            return filename_equal(filesystem, fullHistToHist(value.path), path);
        });
    if (bookmarked) return;

    std::vector<OpenHistory> remaining;
    remaining.reserve(open_histories.size());
    bool promoted = false;
    for (const auto& value : open_histories) {
        if (!promoted && filename_equal(filesystem, fullHistToHist(value.path), path)) {
            hist = value;
            promoted = true;
        } else {
            remaining.push_back(value);
        }
    }
    remaining.insert(remaining.begin(), hist);
    open_histories = removeHistoryNonExisting(
        settings, settings.remove_non_existing_with_priority,
        std::move(remaining), settings.history_max);
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::history
