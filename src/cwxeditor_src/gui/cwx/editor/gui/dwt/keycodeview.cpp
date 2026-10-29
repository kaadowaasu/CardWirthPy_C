#include "cwxeditor_src/gui/cwx/editor/gui/dwt/keycodeview.hpp"

#include "cwxeditor_src/core/cwx/card.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/commons.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dutils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/xmlbytestransfer.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"
#include "cwxeditor_src/lib/ui_metrics.hpp"
#include "cwxeditor_src/lib/ui_window_lifecycle.hpp"
#include "cwxeditor_src/lib/window_host.hpp"

#include <algorithm>
#include <exception>
#include <cctype>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace cwxeditor::cwx::editor::gui::dwt::keycodeview {
namespace {

using ::cwxeditor::lib::Array;
using ::cwxeditor::lib::ChoiceItem;
using ::cwxeditor::lib::ComponentKind;

// D版と同様、現在の Props の言語・メニュー表から補助メニューを作る。 す
// でに開いている補助画面も現在のラベルを使えるよう、ポップアップ時にも更新する。
void localizeKeyCodeHelperMenu(::cwxeditor::lib::Menu& menu,
                               const KeyCodeViewConfig& config) {
    if (::cwxeditor::lib::editor_window_host_options().all_mode || !config.editorCommons) return;
    const auto* prop = config.editorCommons->prop();
    if (!prop) return;
    for (auto& item : menu.items) {
        if (item.command == "copy") item.text = prop->buildMenu(types::MenuID::Copy);
        else if (item.command == "select-all") item.text = prop->buildMenu(types::MenuID::SelectAll);
    }
}


[[nodiscard]] constexpr std::size_t kindIndex(const FKCKind kind) noexcept {
    const auto value = static_cast<std::int32_t>(kind);
    return value < 0 || value > 3 ? 0U : static_cast<std::size_t>(value);
}

[[nodiscard]] bool containsFolded(StringView text, StringView needle) {
    if (needle.empty()) return true;
    if (text.size() < needle.size()) return false;
    const auto fold = [](const unsigned char value) {
        return static_cast<char>(std::tolower(value));
    };
    for (std::size_t start = 0; start + needle.size() <= text.size(); ++start) {
        bool equal = true;
        for (std::size_t offset = 0; offset < needle.size(); ++offset) {
            if (fold(static_cast<unsigned char>(text[start + offset])) !=
                fold(static_cast<unsigned char>(needle[offset]))) {
                equal = false;
                break;
            }
        }
        if (equal) return true;
    }
    return false;
}

template <typename T>
void appendUnique(Array<T>& values, const T& value) {
    if (std::find(values.begin(), values.end(), value) == values.end())
        values.push_back(value);
}

void appendUnique(Array<String>& values, StringView value) {
    if (value.empty()) return;
    if (std::find(values.begin(), values.end(), value) == values.end())
        values.emplace_back(value);
}

void appendWarning(WarningList& warnings, String value) {
    if (!value.empty()) warnings.push_back(std::move(value));
}

[[nodiscard]] String replaceParameter(String format, StringView value) {
    const auto replace = [&format, value](StringView token) {
        const auto position = format.find(token);
        if (position == String::npos) return false;
        format.replace(position, token.size(), value);
        return true;
    };
    if (!replace("%1$s")) replace("%s");
    return format;
}

[[nodiscard]] std::shared_ptr<::cwxeditor::lib::TableView> createTable(
    const ::cwxeditor::lib::Services& services) {
    if (services.gui) {
        if (auto candidate = services.gui->create_component(ComponentKind::TableView)) {
            if (auto table = std::dynamic_pointer_cast<::cwxeditor::lib::TableView>(candidate))
                return table;
        }
    }
    return std::make_shared<::cwxeditor::lib::TableView>();
}

[[nodiscard]] std::shared_ptr<::cwxeditor::lib::ToolBar> createToolBar(
    const ::cwxeditor::lib::Services& services) {
    if (services.gui) {
        if (auto candidate = services.gui->create_component(ComponentKind::ToolBar)) {
            if (auto toolbar = std::dynamic_pointer_cast<::cwxeditor::lib::ToolBar>(candidate))
                return toolbar;
        }
    }
    return std::make_shared<::cwxeditor::lib::ToolBar>();
}

[[nodiscard]] std::shared_ptr<::cwxeditor::lib::ComboBox> createComboBox(
    const ::cwxeditor::lib::Services& services) {
    if (services.gui) {
        if (auto candidate = services.gui->create_component(ComponentKind::ComboBox)) {
            if (auto combo = std::dynamic_pointer_cast<::cwxeditor::lib::ComboBox>(candidate))
                return combo;
        }
    }
    return std::make_shared<::cwxeditor::lib::ComboBox>();
}

[[nodiscard]] std::shared_ptr<::cwxeditor::lib::TextEdit> createTextEdit(
    const ::cwxeditor::lib::Services& services) {
    if (services.gui) {
        if (auto candidate = services.gui->create_component(ComponentKind::TextEdit)) {
            if (auto edit = std::dynamic_pointer_cast<::cwxeditor::lib::TextEdit>(candidate))
                return edit;
        }
    }
    return std::make_shared<::cwxeditor::lib::TextEdit>();
}

[[nodiscard]] std::vector<ChoiceItem> choices(const Array<String>& values) {
    std::vector<ChoiceItem> result;
    result.reserve(values.size());
    for (const auto& value : values) result.push_back({value, value});
    return result;
}

[[nodiscard]] bool isRunAway(const ::cwxeditor::cwx::system::System& system,
                              StringView value) {
    const String copy(value);
    const std::array<String, 1> values{copy};
    return system.isRunAway(std::span<const String>(values));
}

// キーコードのXMLコピーは、すべて同じエディタの転送方針に従う。 XML
// モードの提供処理は、所有するビューや補助ダイアログを同期的に無効化
// する場合があるため、 呼び出す前にホストのコールバックを複製する。
void writeKeyCodeXMLClipboard(const KeyCodeViewConfig& config, StringView xml) {
    const auto clipboard = config.services.clipboard;
    const auto copy_text = config.copyText;
    const auto xml_copy_provider = config.xmlCopyProvider;
    if (clipboard) {
        const bool as_text = xml_copy_provider && xml_copy_provider();
        clipboard->write(as_text ? xmlbytestransfer::makeTextPayload(xml)
                                 : xmlbytestransfer::makeXMLPayload(xml));
        return;
    }
    if (copy_text) copy_text(xml);
}

void setMenuCommandEnabled(::cwxeditor::lib::Menu& menu, const StringView command,
                           const bool enabled) {
    for (auto& item : menu.items) {
        if (item.command == command) item.enabled = enabled;
        for (auto& child : item.children)
            if (child.command == command) child.enabled = enabled;
    }
}

}  // namespace

KeyCodeView::KeyCodeView(::cwxeditor::lib::Container* parent,
                         const std::uint32_t style,
                         KeyCodeViewConfig config,
                         ::cwxeditor::lib::Callback<bool()> catchMod)
    : parent_(parent),
      style_(style),
      config_(std::move(config)),
      catch_mod_(std::move(catchMod)),
      read_only_(config_.readOnly || (style_ & STYLE_READ_ONLY) != 0U) {
    pane_id_ = "keycodeview-" + std::to_string(
        static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(this)));
    active_system_ = config_.system ? config_.system : &system_;
    config_.readOnly = read_only_;
    if (!catch_mod_) catch_mod_ = config_.catchModify;
    initializeToolbar();
    initializeTable();
    initializePopupMenu();
    refreshTable();
    bindExternalNotifications();
    bindDragDrop();
}

KeyCodeView::KeyCodeView(KeyCodeViewConfig config,
                         ::cwxeditor::lib::Container* parent,
                         const std::uint32_t style,
                         ::cwxeditor::lib::Callback<bool()> catchMod)
    : KeyCodeView(parent, style, std::move(config), std::move(catchMod)) {}

KeyCodeView::~KeyCodeView() {
    unbindDragDrop();
    unbindExternalNotifications();
    callback_lifetime_.reset();
    if (auto picker=std::move(select_features_dialog_)) picker->forceCancel();
    if (config_.services.gui && table_)
        (void)config_.services.gui->finish_programmatic_inline_edit(table_, false);
    if (table_) {
        table_->on_selection_changed = {};
        table_->on_cell_activate = {};
        table_->on_cell_edited = {};
        table_->on_pointer = {};
    }
    if (toolbar_) toolbar_->on_command = {};
    if (popup_menu_) {
        popup_menu_->on_command = {};
        popup_menu_->on_open = {};
    }
}

void KeyCodeView::bindExternalNotifications() {
    if (!host_active_ || notification_commons_ || !config_.editorCommons) return;
    notification_commons_ = config_.editorCommons;
    const std::weak_ptr<int> lifetime = callback_lifetime_;
    const auto activation = host_revision_;
    const auto current = [this, lifetime, activation] {
        return !lifetime.expired() && host_active_ && host_revision_ == activation;
    };
    ref_data_version_connection_ = notification_commons_->refDataVersion.add(
        [this, current] { if (current()) refDataVersion(); });
    ref_undo_max_connection_ = notification_commons_->refUndoMax.add(
        [this, current] {
            if (!current()) return;
            const auto provider = config_.undoMaxProvider;
            if (!provider) return;
            const auto limit = provider();
            if (current()) setUndoLimit(limit);
        });
}

void KeyCodeView::unbindExternalNotifications() noexcept {
    if (!notification_commons_) return;
    notification_commons_->refDataVersion.remove(ref_data_version_connection_);
    notification_commons_->refUndoMax.remove(ref_undo_max_connection_);
    ref_data_version_connection_ = 0;
    ref_undo_max_connection_ = 0;
    notification_commons_ = nullptr;
}

void KeyCodeView::bindDragDrop() {
    if (!config_.services.drag_drop || !table_) return;
    config_.services.drag_drop->bind_source({
        table_, ::cwxeditor::lib::DropOperation::Copy |
                    ::cwxeditor::lib::DropOperation::Move,
        [this] { return dragData(); },
        [this](const ::cwxeditor::lib::DropOperation operation) {
            // 同じペイン内の移動は drop() で一括して完
            // 了する。 別ペインへの移動では、転送先がXMLデ
            // ータを受け付けた後にだけ、元の選択を削除する。
            if ((static_cast<std::uint32_t>(operation) &
                 static_cast<std::uint32_t>(::cwxeditor::lib::DropOperation::Move)) != 0U &&
                !drag_selection_.empty()) {
                selection_ = drag_selection_;
                (void)deleteSelection();
            }
            drag_selection_.clear();
        }, {}, [this] {
            ::cwxeditor::lib::DragOrigin origin;
            origin.pane_id = pane_id_;
            if (!drag_selection_.empty())
                origin.cursor_index = drag_selection_.front();
            origin.source_identity = reinterpret_cast<std::uintptr_t>(this);
            return origin;
        }});
    config_.services.drag_drop->bind_target({
        table_, {String(xmlbytestransfer::XML_BYTES_FORMAT),
                 String(xmlbytestransfer::XML_BYTES_MIME), "text/xml",
                 "text/plain"},
        [this](::cwxeditor::lib::DragEvent& event) {
            event.selected = read_only_ ? ::cwxeditor::lib::DropOperation::None
                                        : event.allowed;
        },
        [this](::cwxeditor::lib::DragEvent& event) {
            if (read_only_) event.selected = ::cwxeditor::lib::DropOperation::None;
        },
        [this](::cwxeditor::lib::DragEvent& event) { (void)drop(event); },
        {}, pane_id_, [this](const std::optional<std::size_t> index) {
            return !index || *index <= values_.size();
        }});
}

void KeyCodeView::unbindDragDrop() noexcept {
    if (config_.services.drag_drop && table_)
        config_.services.drag_drop->unbind(table_);
}

::cwxeditor::lib::ClipboardData KeyCodeView::dragData() {
    drag_selection_ = selection_;
    if (drag_selection_.empty()) return {};
    return xmlbytestransfer::makeXMLPayload(toNode(drag_selection_).text());
}

bool KeyCodeView::drop(::cwxeditor::lib::DragEvent& event) {
    if (read_only_) return false;
    auto target = std::min(event.target_index.value_or(values_.size()),
                           values_.size());
    if ((static_cast<std::uint32_t>(event.selected) &
         static_cast<std::uint32_t>(::cwxeditor::lib::DropOperation::Move)) != 0U &&
        event.same_pane() && !drag_selection_.empty()) {
        normalizeSelection();
        const auto moving = drag_selection_;
        KeyCodes values;
        values.reserve(moving.size());
        for (const auto index : moving)
            if (index < values_.size()) values.push_back(values_[index]);
        if (values.empty()) return false;
        storeSnapshot();
        for (auto it = moving.rbegin(); it != moving.rend(); ++it) {
            if (*it >= values_.size()) continue;
            if (*it < target) --target;
            values_.erase(values_.begin() + static_cast<std::ptrdiff_t>(*it));
        }
        target = std::min(target, values_.size());
        values_.insert(values_.begin() + static_cast<std::ptrdiff_t>(target),
                       values.begin(), values.end());
        selection_.clear();
        for (std::size_t i = 0; i < values.size(); ++i)
            selection_.push_back(target + i);
        drag_selection_.clear();
        rebuildTable(false, true);
        event.handled = true;
        return true;
    }
    try {
        const auto text = xmlbytestransfer::CBtoXML(event.data);
        if (text.empty()) return false;
        const auto incoming = createFromNode(XNode::parse(text));
        if (incoming.empty()) return false;
        KeyCodes accepted;
        auto existing = values_;
        for (const auto& value : incoming) {
            if (!isValidValue(value) || existing.size() >= maxCount()) break;
            if (!config_.canDuplicate &&
                std::find(existing.begin(), existing.end(), value) != existing.end())
                continue;
            accepted.push_back(value);
            existing.push_back(value);
        }
        if (accepted.empty()) return false;
        storeSnapshot();
        values_.insert(values_.begin() + static_cast<std::ptrdiff_t>(target),
                       accepted.begin(), accepted.end());
        selection_.clear();
        for (std::size_t i = 0; i < accepted.size(); ++i)
            selection_.push_back(target + i);
        rebuildTable(false, true);
        event.handled = true;
        return true;
    } catch (...) {
        return false;
    }
}

void KeyCodeView::initializeTable() {
    const std::weak_ptr<int> lifetime=callback_lifetime_;
    set_layout(std::make_shared<::cwxeditor::lib::LinearLayout>(
        ::cwxeditor::lib::Orientation::Vertical));

    table_ = createTable(config_.services);
    table_->multi_select = true;
    table_->full_row_selection = true;
    table_->virtualized = true;
    table_->edit_on_double_click = true;
    // ALL のツールボタンは1.5倍にする（行の高さは28から38px）
    // 。 一覧の高さを同じ10px分減らし、ビュー全体の高さを維持する。
    const bool all_mode = ::cwxeditor::lib::editor_window_host_options().all_mode;
    table_->set_layout_item({.horizontal = ::cwxeditor::lib::Align::Stretch,
                             .vertical = ::cwxeditor::lib::Align::Stretch,
                             .minimum = {0.0F, all_mode ? 110.0F : 120.0F}, .grow = 1.0F});
    table_->columns.push_back({config_.resources.keyCodeLabel, 260.0F});
    if (!all_mode) {
        // D版の FullTableColumn と同様、タイミング列の残りの幅を名前列に割り当てる。
        table_->columns.front().width = 0.0F;
        table_->columns.front().stretch = 1.0F;
        table_->columns.front().resizable = false;
    }
    if (config_.withIgnitionType) {
        table_->columns.push_back({config_.resources.keyCodeTimingText, 100.0F});
    }
    table_->on_selection_changed = [this,lifetime](const std::vector<std::size_t>& indices) {
        if(lifetime.expired()) return;
        setSelection(indices);
    };
    table_->on_cell_activate = [this,lifetime](const std::size_t row, const std::size_t column) {
        if(lifetime.expired()) return;
        if (row >= valueCount()) return;
        if (column == 0U) (void)beginKeyCodeEdit(row);
        else if (config_.withIgnitionType) (void)createIgnitionEditor(row);
    };
    table_->on_cell_edited = [this,lifetime](const std::size_t row, const std::size_t column,
                                    String text) {
        if(lifetime.expired()) return;
        if (row >= valueCount() || read_only_) return;
        if (column == 0U) {
            editEnd(row, text);
            return;
        }
        if (!config_.withIgnitionType) return;
        const auto labels = ignitionTypeChoices(row);
        const auto found = std::find(labels.begin(), labels.end(), text);
        if (found != labels.end()) {
            ignitionTypeEditEnd(row, static_cast<std::size_t>(found - labels.begin()));
        }
    };
    table_->on_pointer = [this, lifetime](::cwxeditor::lib::PointerEvent& event) {
        if (lifetime.expired() || event.button != ::cwxeditor::lib::PointerButton::Secondary ||
            event.action != ::cwxeditor::lib::PointerAction::Up || !popup_menu_ ||
            !config_.services.gui) {
            return;
        }
        // 共通の表アダプターが汎用の当たり判定と選択を管理する。 こ
        // のビューはD版のキーコード固有のコマンドだけを提供する。
        refreshPopupMenu();
        config_.services.gui->show_context_menu(popup_menu_, table_, event.position);
        event.handled = true;
    };
    if (!read_only_) {
        // D版の TableTextEdit と同様、選択済みのキー
        // コードを押すと、 ダブルクリックと同じ編集欄で編集する。
        ::cwxeditor::cwx::editor::gui::dwt::dutils::installClickSelectedAction(
            table_, config_.services,
            [this, lifetime](const std::size_t row) {
                if (lifetime.expired() || row >= valueCount()) return;
                (void)beginKeyCodeEdit(row);
            });
    }
    table_->on_key_down = [this, lifetime](::cwxeditor::lib::KeyEvent& event) {
        if (lifetime.expired()) return;
        const auto mods = static_cast<std::uint32_t>(event.modifiers);
        const bool command = (mods & (static_cast<std::uint32_t>(
            ::cwxeditor::lib::Modifiers::Control) |
            static_cast<std::uint32_t>(::cwxeditor::lib::Modifiers::Meta))) != 0U;
        bool handled = false;
        if (command && event.key == ::cwxeditor::lib::Key::A) {
            selectAll(); handled = true;
        } else if (command && event.key == ::cwxeditor::lib::Key::C) {
            handled = copySelection(false);
        } else if (command && event.key == ::cwxeditor::lib::Key::X) {
            handled = copySelection(true);
        } else if (command && event.key == ::cwxeditor::lib::Key::V) {
            handled = pasteClipboard();
        } else if (command && event.key == ::cwxeditor::lib::Key::Z) {
            handled = undo();
        } else if (command && event.key == ::cwxeditor::lib::Key::Y) {
            handled = redo();
        } else if (!command && event.key == ::cwxeditor::lib::Key::Delete) {
            handled = deleteSelection();
        }
        if (handled) event.handled = true;
    };
    add(table_);
}

void KeyCodeView::initializeToolbar() {
    const std::weak_ptr<int> lifetime=callback_lifetime_;
    if (read_only_) return;
    toolbar_ = createToolBar(config_.services);
    // ALL のキーコードのツールボタンは1.5倍にする（24から36px、行の高さは38px）。
    const bool all_mode = ::cwxeditor::lib::editor_window_host_options().all_mode;
    if (all_mode) toolbar_->icon_scale = 1.5F;
    const float toolbar_height = all_mode ? 38.0F : 28.0F;
    toolbar_->set_layout_item({.horizontal = ::cwxeditor::lib::Align::Stretch,
                               .vertical = ::cwxeditor::lib::Align::Center,
                               .minimum = {0.0F, toolbar_height},
                               .preferred = {0.0F, toolbar_height}});
    ::cwxeditor::lib::ToolItem add_item{config_.resources.addKeyCodeText, "add",
                                        config_.resources.addKeyCodeImage};
    add_item.tooltip = config_.resources.addKeyCodeText;
    toolbar_->items.push_back(std::move(add_item));
    ::cwxeditor::lib::ToolItem remove_item{config_.resources.delKeyCodeText, "delete",
                                           config_.resources.delKeyCodeImage};
    remove_item.tooltip = config_.resources.delKeyCodeText;
    toolbar_->items.push_back(std::move(remove_item));
    toolbar_->items.push_back({{}, {}, {}, true, false, true});
    ::cwxeditor::lib::ToolItem up_item{config_.resources.upText, "up",
                                       config_.resources.upImage};
    up_item.tooltip = config_.resources.upText;
    toolbar_->items.push_back(std::move(up_item));
    ::cwxeditor::lib::ToolItem down_item{config_.resources.downText, "down",
                                         config_.resources.downImage};
    down_item.tooltip = config_.resources.downText;
    toolbar_->items.push_back(std::move(down_item));
    if (config_.getMotions) {
        toolbar_->items.push_back({{}, {}, {}, true, false, true});
        ::cwxeditor::lib::ToolItem features_item{
            config_.resources.addByFeaturesText, "add-by-features",
            config_.resources.addByFeaturesImage};
        features_item.tooltip = config_.resources.addByFeaturesText;
        toolbar_->items.push_back(std::move(features_item));
    }
    toolbar_->on_command = [this,lifetime](StringView command) {
        if(lifetime.expired()) return;
        if (command == "add") (void)addKeyCode();
        else if (command == "delete") (void)deleteSelection();
        else if (command == "up") (void)moveUp();
        else if (command == "down") (void)moveDown();
        else if (command == "add-by-features") (void)addKeyCodesByFeatures();
    };
    add(toolbar_);
}

void KeyCodeView::initializePopupMenu() {
    const std::weak_ptr<int> lifetime=callback_lifetime_;
    popup_menu_ = std::make_shared<Menu>(true);
    if (!read_only_) {
        popup_menu_->items.push_back({"Undo", "undo"});
        popup_menu_->items.push_back({"Redo", "redo"});
        popup_menu_->items.push_back({});
        popup_menu_->items.back().separator = true;
        popup_menu_->items.push_back({config_.resources.upText, "up"});
        popup_menu_->items.push_back({config_.resources.downText, "down"});
        popup_menu_->items.push_back({});
        popup_menu_->items.back().separator = true;
        popup_menu_->items.push_back({"Cut", "cut"});
    }
    popup_menu_->items.push_back({"Copy", "copy"});
    if (!read_only_) popup_menu_->items.push_back({"Paste", "paste"});
    if (!read_only_) popup_menu_->items.push_back({"Delete", "delete"});
    popup_menu_->items.push_back({});
    popup_menu_->items.back().separator = true;
    popup_menu_->items.push_back({"Select all", "select-all"});
    popup_menu_->items.push_back({});
    popup_menu_->items.back().separator = true;

    if (!read_only_ && !config_.canDuplicate) {
        auto& timing = popup_menu_->items.emplace_back();
        timing.text = config_.resources.keyCodeTimingText;
        timing.command = "timing";
        timing.children.push_back({config_.resources.timingLabels[0], "timing-use"});
        if (config_.withIgnitionType) {
            timing.children.push_back({config_.resources.timingLabels[1], "timing-success"});
            timing.children.push_back({config_.resources.timingLabels[2], "timing-failure"});
        }
        timing.children.push_back({config_.resources.timingLabels[3], "timing-has-not"});
    }

    auto& copy = popup_menu_->items.emplace_back();
    copy.text = config_.resources.copyTimingConvertedText;
    copy.command = "copy-timing";
    copy.children.push_back({config_.resources.timingLabels[0], "copy-use"});
    copy.children.push_back({config_.resources.timingLabels[1], "copy-success"});
    copy.children.push_back({config_.resources.timingLabels[2], "copy-failure"});
    copy.children.push_back({config_.resources.timingLabels[3], "copy-has-not"});

    if (!read_only_ && config_.getMotions) {
        popup_menu_->items.push_back({});
        popup_menu_->items.back().separator = true;
        popup_menu_->items.push_back({config_.resources.addByFeaturesText, "add-by-features",
                                       {}, config_.resources.addByFeaturesImage});
    }
    popup_menu_->on_command = [this,lifetime](StringView command) {
        if(lifetime.expired()) return;
        if (command == "undo") (void)undo();
        else if (command == "redo") (void)redo();
        else if (command == "up") (void)moveUp();
        else if (command == "down") (void)moveDown();
        else if (command == "cut") (void)copySelection(true);
        else if (command == "copy") (void)copySelection(false);
        else if (command == "paste") (void)pasteClipboard();
        else if (command == "delete") (void)deleteSelection();
        else if (command == "select-all") selectAll();
        else if (command == "timing-use") (void)convKeyCode(FKCKind::Use);
        else if (command == "timing-success") (void)convKeyCode(FKCKind::Success);
        else if (command == "timing-failure") (void)convKeyCode(FKCKind::Failure);
        else if (command == "timing-has-not") (void)convKeyCode(FKCKind::HasNot);
        else if (command == "copy-use") (void)copyKeyCodeWith(FKCKind::Use);
        else if (command == "copy-success") (void)copyKeyCodeWith(FKCKind::Success);
        else if (command == "copy-failure") (void)copyKeyCodeWith(FKCKind::Failure);
        else if (command == "copy-has-not") (void)copyKeyCodeWith(FKCKind::HasNot);
        else if (command == "add-by-features") (void)addKeyCodesByFeatures();
        refreshPopupMenu();
    };
    popup_menu_->on_open = [this, lifetime] {
        if (!lifetime.expired()) refreshPopupMenu();
    };
}

void KeyCodeView::refreshPopupMenu() {
    if (!popup_menu_) return;
    const bool available = enabled();
    const auto conversion = [this, available](const FKCKind kind) {
        return available && canConvKeyCode(kind);
    };
    const bool copy = available && canCopyWith();
    setMenuCommandEnabled(*popup_menu_, "undo", available && canUndo());
    setMenuCommandEnabled(*popup_menu_, "redo", available && canRedo());
    setMenuCommandEnabled(*popup_menu_, "up", available && canUp());
    setMenuCommandEnabled(*popup_menu_, "down", available && canDown());
    setMenuCommandEnabled(*popup_menu_, "cut", available && !read_only_ && !selection_.empty());
    setMenuCommandEnabled(*popup_menu_, "copy", available && !selection_.empty());
    setMenuCommandEnabled(*popup_menu_, "paste", available && !read_only_ &&
        config_.services.clipboard != nullptr);
    setMenuCommandEnabled(*popup_menu_, "delete", available && !read_only_ && !selection_.empty());
    setMenuCommandEnabled(*popup_menu_, "select-all", available && canSelectAll());
    setMenuCommandEnabled(*popup_menu_, "timing-use", conversion(FKCKind::Use));
    setMenuCommandEnabled(*popup_menu_, "timing-success", conversion(FKCKind::Success));
    setMenuCommandEnabled(*popup_menu_, "timing-failure", conversion(FKCKind::Failure));
    setMenuCommandEnabled(*popup_menu_, "timing-has-not", conversion(FKCKind::HasNot));
    setMenuCommandEnabled(*popup_menu_, "timing",
                          conversion(FKCKind::Use) || conversion(FKCKind::Success) ||
                              conversion(FKCKind::Failure) || conversion(FKCKind::HasNot));
    setMenuCommandEnabled(*popup_menu_, "copy-timing", copy);
    setMenuCommandEnabled(*popup_menu_, "copy-use", copy);
    setMenuCommandEnabled(*popup_menu_, "copy-success", copy);
    setMenuCommandEnabled(*popup_menu_, "copy-failure", copy);
    setMenuCommandEnabled(*popup_menu_, "copy-has-not", copy);
    setMenuCommandEnabled(*popup_menu_, "add-by-features",
                          available && canAddKeyCodesByFeatures());
}

void KeyCodeView::addModifyListener(ModifyHandler callback) {
    if (callback) modEvent.push_back(std::move(callback));
}

void KeyCodeView::setReadOnly(const bool value) {
    read_only_ = value;
    config_.readOnly = value;
    // 読み取り専用でも、選択・コピーと発火条件の種類の確認は許可する。
    if (table_) table_->set_enabled(enabled());
    if (toolbar_) toolbar_->set_enabled(!value && enabled());
    const std::weak_ptr<int> lifetime=callback_lifetime_;
    const auto revision=render_revision_;
    updateEditors();
    if(lifetime.expired() || revision!=render_revision_) return;
    notifyRefreshToolBar();
}

void KeyCodeView::hostActive(const bool value) {
    const std::weak_ptr<int> lifetime = callback_lifetime_;
    const auto revision = ++host_revision_;
    host_active_ = value;
    ++feature_request_revision_;
    unbindExternalNotifications();
    const auto current = [this, lifetime, revision] {
        return !lifetime.expired() && host_revision_ == revision;
    };
    if (value) {
        bindExternalNotifications();
        setEnabled(true);
        if (current()) refDataVersion();
        return;
    }
    const auto picker = std::move(select_features_dialog_);
    if (picker) {
        picker->appliedEvent = {};
        picker->closeEvent = {};
    }
    std::exception_ptr failure;
    const auto step = [&failure](auto&& callback) {
        try { callback(); } catch (...) { if (!failure) failure = std::current_exception(); }
    };
    step([&] { setEnabled(false); });
    if (current() && picker) step([&] { picker->forceCancel(); });
    if (current()) step([&] { cancelEditors(); });
    if (failure) std::rethrow_exception(failure);
}

void KeyCodeView::setEnabled(const bool value) {
    ::cwxeditor::lib::Widget::set_enabled(value);
    if (table_) table_->set_enabled(value);
    if (toolbar_) toolbar_->set_enabled(value && !read_only_);
    updateEditors();
}

KeyCodes KeyCodeView::values() const {
    auto result = values_;
    if (new_item_index_ && *new_item_index_ < result.size())
        result.erase(result.begin() + static_cast<std::ptrdiff_t>(*new_item_index_));
    return result;
}

void KeyCodeView::setValues(const KeyCodes& values) {
    const auto replacement = values; // 呼び出し元が valueStorage() と同じ領域を参照している場合がある。
    const std::weak_ptr<int> lifetime = callback_lifetime_;
    const auto revision = ++values_revision_;
    ++provisional_revision_;
    cancelEditors();
    if (lifetime.expired() || revision != values_revision_) return;
    values_ = replacement;
    selection_.clear();
    new_item_index_.reset();
    last_add_errors_.clear();
    clearUndo();
    rebuildTable(true);
}

const String& KeyCodeView::value(const std::size_t index) const {
    if (index >= values_.size()) throw std::out_of_range("KeyCodeView::value index");
    return values_[index];
}

void KeyCodeView::setValue(const std::size_t index, String value) {
    (void)editValue(index, std::move(value));
}

void KeyCodeView::normalizeSelection() noexcept {
    selection_.erase(std::remove_if(selection_.begin(), selection_.end(),
                                    [this](const std::size_t index) {
                                        return index >= values_.size();
                                    }),
                     selection_.end());
    std::sort(selection_.begin(), selection_.end());
    selection_.erase(std::unique(selection_.begin(), selection_.end()), selection_.end());
}

void KeyCodeView::setSelection(const SizeIndices& indices) {
    selection_ = indices;
    normalizeSelection();
    if (table_) (void)lib::select_table_rows(*table_, selection_, false, false);
    notifyRefreshToolBar();
}

void KeyCodeView::clearSelection() {
    selection_.clear();
    if (table_) (void)lib::select_table_rows(*table_, {}, false, false);
    notifyRefreshToolBar();
}

bool KeyCodeView::canSelectAll() const noexcept {
    return !values_.empty() && selection_.size() != values_.size();
}

void KeyCodeView::selectAll() {
    if (!canSelectAll()) return;
    selection_.resize(values_.size());
    for (std::size_t i = 0; i < selection_.size(); ++i) selection_[i] = i;
    if (table_) (void)lib::select_table_rows(*table_, selection_, false, false);
    notifyRefreshToolBar();
}

bool KeyCodeView::copySelection(const bool cut) {
    if (selection_.empty() || !config_.services.clipboard ||
        (cut && read_only_)) return false;
    const auto xml_value = toNode(selection_).text();
    const auto current = operationCurrent();
    writeKeyCodeXMLClipboard(config_, xml_value);
    if (!current()) return false;
    if (cut) return deleteSelection();
    return true;
}

bool KeyCodeView::pasteClipboard() {
    if (read_only_ || !config_.services.clipboard) return false;
    try {
        const auto text = xmlbytestransfer::CBtoXML(
            config_.services.clipboard->read());
        if (text.empty()) return false;
        return appendValues(createFromNode(XNode::parse(text)));
    } catch (...) {
        return false;
    }
}

bool KeyCodeView::isValidValue(const StringView value) const {
    if (config_.withIgnitionType) return !activeSystem().toFKeyCode(String(value)).keyCode.empty();
    return config_.canDuplicate || !value.empty();
}

std::size_t KeyCodeView::maxCount() const noexcept {
    if (config_.summaryLegacy && config_.canDuplicate) return config_.keyCodesMaxLegacy;
    return std::numeric_limits<std::size_t>::max();
}

bool KeyCodeView::canAdd() const noexcept {
    return !read_only_ && values_.size() < maxCount();
}

std::optional<std::size_t> KeyCodeView::findValue(
    const StringView value, const std::optional<std::size_t> except) const {
    for (std::size_t i = 0; i < values_.size(); ++i) {
        if (except && i == *except) continue;
        if (values_[i] == value) return i;
    }
    return {};
}

void KeyCodeView::storeSnapshot() {
    if (config_.undoMax == 0U) {
        redo_.clear();
        return;
    }
    auto selection = selection_;
    if (new_item_index_) {
        const auto pending = *new_item_index_;
        std::erase(selection, pending);
        for (auto& index : selection) if (index > pending) --index;
    }
    undo_.push_back({values(), std::move(selection)});
    if (undo_.size() > config_.undoMax) undo_.erase(undo_.begin());
    redo_.clear();
}

void KeyCodeView::restoreSnapshot(const Snapshot& snapshot) {
    values_ = snapshot.values;
    selection_ = snapshot.selection;
    normalizeSelection();
    rebuildTable(false, true);
}

void KeyCodeView::raiseModifyEvent() {
    const std::weak_ptr<int> lifetime=callback_lifetime_;
    const auto revision=render_revision_;
    const auto listeners=modEvent;
    for(const auto& listener:listeners) {
        if(lifetime.expired() || revision!=render_revision_) return;
        if(listener) listener();
    }
}

std::function<bool()> KeyCodeView::operationCurrent() const {
    const std::weak_ptr<int> lifetime=callback_lifetime_;
    const auto revision=render_revision_;
    const auto request=feature_request_revision_;
    const auto table=table_;
    const auto epoch=table?table->row_order_revision:0;
    const auto rows=values_;
    const auto selected=selection_;
    const auto table_selected=table?table->selection:SizeIndices{};
    const auto read_only=read_only_;
    const auto was_enabled=enabled();
    return [this,lifetime,revision,request,table,epoch,rows,selected,table_selected,read_only,was_enabled] {
        return !lifetime.expired() && render_revision_==revision && feature_request_revision_==request &&
            table_==table && (!table || (table->row_order_revision==epoch && table->selection==table_selected)) &&
            values_==rows && selection_==selected && read_only_==read_only && enabled()==was_enabled;
    };
}

void KeyCodeView::notifyRefreshToolBar() const {
    const auto current=operationCurrent();
    const auto notify=config_.refreshToolBar;
    // 有効なツールバー・メニュー項目を走査する前に、提供処理を呼ぶ。
    const auto can_add_features=canAddKeyCodesByFeatures();
    if(!current()) return;
    if (toolbar_) {
        auto items = toolbar_->items;
        for (auto& item : items) {
            if (item.command == "add") item.enabled = canAdd();
            else if (item.command == "delete") item.enabled = !read_only_ && !selection_.empty();
            else if (item.command == "up") item.enabled = canUp();
            else if (item.command == "down") item.enabled = canDown();
            else if (item.command == "add-by-features") item.enabled = can_add_features;
        }
        if (!lib::replace_tool_bar_items(toolbar_, std::move(items), current)) return;
    }
    if (popup_menu_) {
        auto items = popup_menu_->items;
        for (auto& item : items) {
            if (item.command == "undo") item.enabled = !read_only_ && canUndo();
            else if (item.command == "redo") item.enabled = !read_only_ && canRedo();
            else if (item.command == "up") item.enabled = canUp();
            else if (item.command == "down") item.enabled = canDown();
            else if (item.command == "select-all") item.enabled = canSelectAll();
            else if (item.command == "copy-timing") {
                item.enabled = canCopyWith();
                for (auto& child : item.children) {
                    if (child.command == "copy-use") child.enabled = canCopyWith();
                    else if (child.command == "copy-success") child.enabled = canCopyWith();
                    else if (child.command == "copy-failure") child.enabled = canCopyWith();
                    else if (child.command == "copy-has-not") child.enabled = canCopyWith();
                }
            } else if (item.command == "timing") {
                item.enabled = canConvKeyCode(FKCKind::Use) ||
                               canConvKeyCode(FKCKind::Success) ||
                               canConvKeyCode(FKCKind::Failure) ||
                               canConvKeyCode(FKCKind::HasNot);
                for (auto& child : item.children) {
                    if (child.command == "timing-use") child.enabled = canConvKeyCode(FKCKind::Use);
                    else if (child.command == "timing-success") child.enabled = canConvKeyCode(FKCKind::Success);
                    else if (child.command == "timing-failure") child.enabled = canConvKeyCode(FKCKind::Failure);
                    else if (child.command == "timing-has-not") child.enabled = canConvKeyCode(FKCKind::HasNot);
                }
            } else if (item.command == "add-by-features") {
                item.enabled = can_add_features;
            }
        }
        if (!lib::replace_menu_items(popup_menu_, std::move(items), current)) return;
    }
    if(notify) notify();
}

bool KeyCodeView::appendValuesImpl(const KeyCodes& values, const bool force) {
    if (read_only_) return false;
    KeyCodes accepted;
    accepted.reserve(values.size());
    KeyCodes existing = values_;
    for (const auto& candidate : values) {
        if (!force && !isValidValue(candidate)) continue;
        if (!config_.canDuplicate &&
            std::find(existing.begin(), existing.end(), candidate) != existing.end()) {
            continue;
        }
        if (!force && existing.size() >= maxCount()) break;
        accepted.push_back(candidate);
        existing.push_back(candidate);
    }
    if (accepted.empty()) return false;
    storeSnapshot();
    const auto first = values_.size();
    values_.insert(values_.end(), accepted.begin(), accepted.end());
    selection_.clear();
    for (std::size_t i = first; i < values_.size(); ++i) selection_.push_back(i);
    rebuildTable(false, true);
    return true;
}

bool KeyCodeView::appendValues(const KeyCodes& values, const bool force) {
    return appendValuesImpl(values, force);
}

bool KeyCodeView::addKeyCode() {
    if (read_only_ || !enabled()) return false;
    const std::weak_ptr<int> lifetime = callback_lifetime_;
    const auto values_revision = values_revision_;
    finishEditors();
    if (lifetime.expired() || values_revision != values_revision_) return false;
    if (new_item_index_) cancelEdit();
    if (lifetime.expired() || values_revision != values_revision_ || !enabled() || !canAdd())
        return false;
    values_.emplace_back();
    new_item_index_ = values_.size() - 1U;
    const auto pending = ++provisional_revision_;
    selection_ = {*new_item_index_};
    // D版と同様、追加は仮の状態とし、取り消し履歴も変更通知も作らない。
    rebuildTable(false);
    if (lifetime.expired() || values_revision != values_revision_ ||
        pending != provisional_revision_ || !new_item_index_) return false;
    const bool started = beginKeyCodeEdit(*new_item_index_);
    if (lifetime.expired() || values_revision != values_revision_) return false;
    if (!started && pending == provisional_revision_) cancelEdit();
    return started;
}

bool KeyCodeView::beginKeyCodeEdit(const std::size_t index) {
    if (read_only_ || !enabled() || !table_ || !config_.services.gui ||
        index >= values_.size())
        return false;
    const std::weak_ptr<int> lifetime = callback_lifetime_;
    const std::weak_ptr<::cwxeditor::lib::TableView> weak_table = table_;
    const auto epoch = table_->row_order_revision;
    const auto pending = provisional_revision_;
    ::cwxeditor::lib::ProgrammaticInlineEditRequest request;
    request.generic_owner = table_;
    request.generic_text = values_[index];
    request.generic_choices = choices(keyCodeCandidates(String{}));
    if (lifetime.expired() || table_->row_order_revision != epoch ||
        pending != provisional_revision_ || read_only_ || !enabled()) return false;
    request.generic_editable_choice = true;
    request.generic_bounds = [weak_table, index, epoch,
                              with_ignition = config_.withIgnitionType]()
        -> std::optional<::cwxeditor::lib::Rect> {
        const auto table = weak_table.lock();
        if (!table || table->row_order_revision != epoch || index >= table->rows.size())
            return std::nullopt;
        auto rect = ::cwxeditor::lib::table_row_geometry(*table, index, 0.0F);
        // キーコード列だけを編集し、タイミング列は表示したままにする。
        const auto viewport = ::cwxeditor::lib::item_scroll_geometry(*table).viewport;
        if (!::cwxeditor::lib::editor_window_host_options().all_mode)
            rect.width = std::min(rect.width, viewport.width);
        if (with_ignition && !table->columns.empty()) {
            const auto widths = ::cwxeditor::lib::resolved_table_column_widths(
                *table, viewport.width);
            if (!widths.empty()) rect.width = std::min(rect.width, widths.front());
        }
        return rect;
    };
    request.generic_cancelled = [this, lifetime, weak_table, epoch, index, pending] {
        const auto table = weak_table.lock();
        if (lifetime.expired() || !table || table != table_ ||
            table->row_order_revision != epoch || pending != provisional_revision_ ||
            new_item_index_ != index) return;
        cancelEdit();
    };
    request.generic_commit = [this, lifetime, weak_table, epoch, index, pending](String text) {
        const auto table = weak_table.lock();
        if (lifetime.expired() || !table || table != table_ ||
            table->row_order_revision != epoch || pending != provisional_revision_ ||
            index >= values_.size() || read_only_ || !enabled())
            return false;
        editEnd(index, text);
        return true;
    };
    request.row_height = ::cwxeditor::lib::table_row_height(*table_, 0.0F);
    return config_.services.gui->request_programmatic_inline_edit(request);
}

bool KeyCodeView::editValue(const std::size_t index, String value) {
    if (read_only_ || index >= values_.size()) return false;
    const bool creating = new_item_index_ && *new_item_index_ == index;
    if (!creating && values_[index] == value) return false;
    const auto duplicate = config_.canDuplicate
        ? std::optional<std::size_t>{} : findValue(value, index);
    if (!isValidValue(value) || duplicate) {
        if (duplicate) {
            auto selected = *duplicate;
            if (creating) {
                values_.erase(values_.begin() + static_cast<std::ptrdiff_t>(index));
                if (index < selected) --selected;
                new_item_index_.reset();
                ++provisional_revision_;
            }
            // D版の mainValueEditEnd とC++版の AbstractEditableListView
            // は、既存行との重複を拒否した後にも通知する。 KeyCodeView
            // は確定した値の変更だけを通知するため、選択だけ
            // が変わるこの拒否では履歴・通知を作らない。 D版
            // と同様、主列と発火条件の種類のどちらの編集でも、
            // 重複を拒否したら既存の同値を選択して表示する。
            selection_ = {selected};
            rebuildTable(false, false, true);
        } else if (creating) {
            values_.erase(values_.begin() + static_cast<std::ptrdiff_t>(index));
            new_item_index_.reset();
            ++provisional_revision_;
            selection_.clear();
            refreshTable();
        }
        return false;
    }
    // 仮の空行ではなく、確定した表示内容のスナップショットを取得する。
    // 通常の重複を許すカードでは、最初の確定値として空も受け付ける。
    storeSnapshot();
    values_[index] = std::move(value);
    if (creating) {
        new_item_index_.reset();
        ++provisional_revision_;
    }
    selection_ = {index};
    rebuildTable(false, true);
    return true;
}

void KeyCodeView::editEnd(const std::size_t index, const StringView newText) {
    if (index >= values_.size() || read_only_) return;
    if (newText.empty() && !isValidValue(newText)) {
        if (new_item_index_ && *new_item_index_ == index) {
            (void)editValue(index, String{});
        } else {
            // D版の EditableListView と同様、既存の無効な空行
            // は削除する。 編集の確定フックから、再帰的に編集終了を呼ばない。
            storeSnapshot();
            values_.erase(values_.begin() + static_cast<std::ptrdiff_t>(index));
            for (auto& selected:selection_) if (selected>index) --selected;
            normalizeSelection(); // D版の delSelection と同様、残ったネイティブの行を保持する。
            rebuildTable(false, true);
        }
        return;
    }
    (void)editValue(index, String(newText));
}

bool KeyCodeView::deleteSelection() {
    if (read_only_ || !enabled()) return false;
    const std::weak_ptr<int> lifetime = callback_lifetime_;
    const auto revision = values_revision_;
    // D版と同様、構造を変える操作の前に編集中の内容を確定する。
    // 受理した空のキーは通常の確定行にし、古い仮状態は無効化する。
    finishEditors();
    if (lifetime.expired() || revision != values_revision_) return false;
    if (new_item_index_) cancelEditors();
    if (lifetime.expired() || revision != values_revision_ || read_only_ || selection_.empty()) return false;
    storeSnapshot();
    auto indices = selection_;
    std::sort(indices.begin(), indices.end(), std::greater<>());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    KeyCodes deleted_values;
    for (const auto index : indices) {
        if (index < values_.size() &&
            std::find(deleted_values.begin(), deleted_values.end(), values_[index]) ==
                deleted_values.end())
            deleted_values.push_back(values_[index]);
    }
    for (const auto index : indices) {
        if (index < values_.size())
            values_.erase(values_.begin() + static_cast<std::ptrdiff_t>(index));
    }
    selection_.clear();
    for (std::size_t i = 0; i < values_.size(); ++i) {
        if (std::find(deleted_values.begin(), deleted_values.end(), values_[i]) !=
            deleted_values.end())
            selection_.push_back(i);
    }
    new_item_index_.reset();
    rebuildTable(false, true);
    return true;
}

bool KeyCodeView::canUp() const noexcept {
    return !read_only_ && !selection_.empty() && selection_.front() > 0U;
}

bool KeyCodeView::canDown() const noexcept {
    return !read_only_ && !selection_.empty() && selection_.back() + 1U < values_.size();
}

bool KeyCodeView::moveUp() {
    if (read_only_ || !enabled()) return false;
    const std::weak_ptr<int> lifetime = callback_lifetime_;
    const auto revision = values_revision_;
    // D版と同様、構造を変える操作の前に編集中の内容を確定する。
    // 受理した空のキーは通常の確定行にし、古い仮状態は無効化する。
    finishEditors();
    if (lifetime.expired() || revision != values_revision_) return false;
    if (new_item_index_) cancelEditors();
    if (lifetime.expired() || revision != values_revision_ || !canUp()) return false;
    storeSnapshot();
    auto moved = selection_;
    for (const auto index : selection_) std::swap(values_[index], values_[index - 1U]);
    for (auto& index : moved) --index;
    selection_ = std::move(moved);
    rebuildTable(false, true);
    return true;
}

bool KeyCodeView::moveDown() {
    if (read_only_ || !enabled()) return false;
    const std::weak_ptr<int> lifetime = callback_lifetime_;
    const auto revision = values_revision_;
    // D版と同様、構造を変える操作の前に編集中の内容を確定する。
    // 受理した空のキーは通常の確定行にし、古い仮状態は無効化する。
    finishEditors();
    if (lifetime.expired() || revision != values_revision_) return false;
    if (new_item_index_) cancelEditors();
    if (lifetime.expired() || revision != values_revision_ || !canDown()) return false;
    storeSnapshot();
    auto moved = selection_;
    for (auto it = selection_.rbegin(); it != selection_.rend(); ++it)
        std::swap(values_[*it], values_[*it + 1U]);
    for (auto& index : moved) ++index;
    selection_ = std::move(moved);
    rebuildTable(false, true);
    return true;
}

bool KeyCodeView::undo() {
    if (read_only_) return false;
    const std::weak_ptr<int> lifetime = callback_lifetime_;
    const auto revision = values_revision_;
    cancelEditors();
    if (lifetime.expired() || revision != values_revision_ || undo_.empty()) return false;
    redo_.push_back({values_, selection_});
    const auto snapshot = undo_.back();
    undo_.pop_back();
    new_item_index_.reset();
    restoreSnapshot(snapshot);
    return true;
}

bool KeyCodeView::redo() {
    if (read_only_) return false;
    const std::weak_ptr<int> lifetime = callback_lifetime_;
    const auto revision = values_revision_;
    cancelEditors();
    if (lifetime.expired() || revision != values_revision_ || redo_.empty()) return false;
    undo_.push_back({values_, selection_});
    const auto snapshot = redo_.back();
    redo_.pop_back();
    new_item_index_.reset();
    restoreSnapshot(snapshot);
    return true;
}

void KeyCodeView::clearUndo() noexcept {
    undo_.clear();
    redo_.clear();
}

void KeyCodeView::setUndoLimit(const std::size_t value) {
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

void KeyCodeView::cancelEdit() {
    if (!new_item_index_ || *new_item_index_ >= values_.size()) {
        new_item_index_.reset();
        return;
    }
    values_.erase(values_.begin() + static_cast<std::ptrdiff_t>(*new_item_index_));
    new_item_index_.reset();
    ++provisional_revision_;
    selection_.clear();
    refreshTable();
}

KeyCodes KeyCodeView::createFromNode(XNode node) const {
    const auto version = config_.dataVersion.empty()
                             ? ::cwxeditor::cwx::versioninfo::LATEST_VERSION
                             : config_.dataVersion;
    const ::cwxeditor::cwx::system::XMLInfo info(system_, version);
    return ::cwxeditor::cwx::card::keyCodesFromNode(node, info);
}

void KeyCodeView::refDataVersion() {
    const std::weak_ptr<int> lifetime = callback_lifetime_;
    const auto activation = host_revision_;
    const auto current = [this, lifetime, activation] {
        return !lifetime.expired() && host_active_ && host_revision_ == activation;
    };
    if (!current()) return;
    const auto config = config_;
    if (config.summaryLegacyProvider) {
        const auto value = config.summaryLegacyProvider();
        if (!current()) return;
        config_.summaryLegacy = value;
    }
    if (config.dataVersionProvider) {
        const auto value = config.dataVersionProvider();
        if (!current()) return;
        config_.dataVersion = value;
    }
    if (config.keyCodesMaxLegacyProvider) {
        const auto value = config.keyCodesMaxLegacyProvider();
        if (!current()) return;
        config_.keyCodesMaxLegacy = value;
    }
    if (config.undoMaxProvider) {
        const auto value = config.undoMaxProvider();
        if (!current()) return;
        setUndoLimit(value);
    }
    if (current()) clearAll();
}

XNode KeyCodeView::toNode(const Indices& indices) const {
    KeyCodes selected;
    selected.reserve(indices.size());
    for (const auto index : indices) {
        if (index >= 0 && static_cast<std::size_t>(index) < values_.size())
            selected.push_back(values_[static_cast<std::size_t>(index)]);
    }
    return ::cwxeditor::cwx::card::keyCodesToNode(selected);
}

XNode KeyCodeView::toNode(const SizeIndices& indices) const {
    Indices converted;
    converted.reserve(indices.size());
    for (const auto index : indices) {
        if (index <= static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
            converted.push_back(static_cast<std::int32_t>(index));
    }
    return toNode(converted);
}

XNode KeyCodeView::toNode() const {
    return ::cwxeditor::cwx::card::keyCodesToNode(values());
}

void KeyCodeView::updateItem(const String& value, ::cwxeditor::lib::TableRow& item) const {
    const String text=value;
    const auto resources=config_.resources;
    const bool with_timing=config_.withIgnitionType, editable=!read_only_;
    const auto code = activeSystem().toFKeyCode(text);
    const auto index = kindIndex(code.kind);
    const auto candidates=keyCodeCandidates(text);
    // 提供処理の後は、閉じられた可能性のあるビューではなく、所有しているデータだけを使う。
    item.cells.clear();
    item.cells.push_back({text,text,with_timing?resources.timingImages[index]:resources.keyCodeImage,
                          false,editable,::cwxeditor::lib::CellEditorKind::EditableChoice,candidates});
    if (with_timing) {
        const auto& timing_choices=resources.timingLabels;
        item.cells.push_back({resources.timingLabels[index],
                              resources.timingLabels[index], {}, false, editable,
                              ::cwxeditor::lib::CellEditorKind::Choice,
                              {timing_choices.begin(), timing_choices.end()}});
    }
}

void KeyCodeView::refreshTable() {
    rebuildTable(false);
}

void KeyCodeView::rebuildTable(const bool resetTop, const bool changed, const bool reveal) {
    if (!table_) return;
    const std::weak_ptr<int> lifetime=callback_lifetime_;
    const auto revision=++render_revision_;
    const auto table = table_;
    const auto epoch = table->row_order_revision;
    const auto values = values_;
    const auto read_only = read_only_;
    const auto was_enabled=enabled();
    const auto selected=selection_;
    const auto live = [&] {
        return !lifetime.expired() && revision==render_revision_ && table_ == table &&
            table->row_order_revision == epoch && read_only_ == read_only &&
            enabled()==was_enabled && selection_==selected;
    };
    std::vector<::cwxeditor::lib::TableRow> rows(values.size());
    for (std::size_t i = 0; i < values.size(); ++i) {
        // 候補・文字列の提供処理が、このビューを同期的に再読み込みする場合がある。
        updateItem(values[i], rows[i]);
        if (!live()) return;
        rows[i].warnings = getWarnings(i);
        if (!live()) return;
        rows[i].warning_icon = rows[i].warnings.empty()
            ? 0U : config_.resources.warningImage;
        if (!live()) return;
    }
    // 各行でベクター全体を走査せず、対象の値を一度ずつ比較する。
    if (values_ != values) return;
    normalizeSelection();
    const std::optional<std::size_t> reveal_index =
        reveal && !selection_.empty()
            ? std::optional<std::size_t>{selection_.front()}
            : std::nullopt;
    // 行、選択する重複項目、表示範囲を、一回の表更新として反映する。
    if(!::cwxeditor::lib::replace_table_rows(*table, std::move(rows), selection_, reveal_index, [&] {
        return live() && values_==values;
    }, resetTop)) return;
    updateEditors();
    if(lifetime.expired() || revision!=render_revision_) return;
    if(changed) {
        raiseModifyEvent();
        if(lifetime.expired() || revision!=render_revision_) return;
    }
    notifyRefreshToolBar();
}

KeyCodeView::EditorPtr KeyCodeView::mainEditor() const noexcept {
    if (!selection_.empty() && selection_.front() < editors_.size())
        return editors_[selection_.front()];
    if (!editors_.empty()) return editors_.front();
    return {};
}

void KeyCodeView::finishEditors() noexcept {
    const auto table=table_;
    const auto gui=config_.services.gui;
    if (!table || !gui) return;
    (void)gui->finish_programmatic_inline_edit(table,true);
    // 公開の確定処理には借用したメンバーを引数として渡さず、その後の処理も続けない。
}

void KeyCodeView::cancelEditors() {
    const std::weak_ptr<int> lifetime = callback_lifetime_;
    const auto revision = values_revision_;
    const auto gui = config_.services.gui;
    if (gui && table_) (void)gui->finish_programmatic_inline_edit(table_, false);
    if (lifetime.expired() || revision != values_revision_) return;
    if (new_item_index_) cancelEdit();
}

void KeyCodeView::updateEditors() {
    const std::weak_ptr<int> lifetime=callback_lifetime_;
    const auto revision=render_revision_;
    if (read_only_ || values_.empty()) {
        editors_.clear();
        return;
    }
    const auto table = table_;
    const auto epoch = table ? table->row_order_revision : 0;
    const auto values = values_;
    const auto was_enabled = enabled();
    EditorArray editors;
    editors.reserve(values.size());
    for (std::size_t i = 0; i < values.size(); ++i) {
        auto editor = createKeyCodeEditor(i);
        if (lifetime.expired() || revision!=render_revision_ || table_ != table ||
            (table && table->row_order_revision != epoch) ||
            read_only_ || enabled() != was_enabled) return;
        editors.push_back(std::move(editor));
    }
    if (values_ != values) return;
    editors_.swap(editors);
}

KeyCodeView::ComboBoxPtr KeyCodeView::createKeyCodeEditor(const std::size_t index) {
    const std::weak_ptr<int> lifetime=callback_lifetime_;
    const auto table=table_;
    const auto epoch=table?table->row_order_revision:0;
    const auto revision=render_revision_;
    auto editor = createComboBox(config_.services);
    const String initial = index < values_.size() ? values_[index] : String{};
    editor->editable = true;
    editor->edit_text = initial;
    editor->items = choices(keyCodeCandidates(initial));
    if(lifetime.expired() || revision!=render_revision_ || table_!=table ||
       (table && epoch!=table->row_order_revision)) return editor;
    editor->set_enabled(!read_only_ && enabled());
    const std::weak_ptr<ComboBox> weak_editor = editor;
    const std::weak_ptr<Table> weak_table=table;
    const auto current=[this,lifetime,weak_table,epoch,revision] {
        if(lifetime.expired() || revision!=render_revision_ || read_only_ || !enabled()) return false;
        const auto target=weak_table.lock();
        return target && target==table_ && target->row_order_revision==epoch;
    };
    editor->on_selection_changed = [this, weak_editor, index,current](std::optional<std::size_t> selected) {
        if(!current()) return;
        const auto target = weak_editor.lock();
        if (!target || !selected || *selected >= target->items.size()) return;
        editEnd(index, target->items[*selected].text);
    };
    editor->on_text_changed = [this, weak_editor,current](const StringView value) {
        if(!current()) return;
        if (const auto target = weak_editor.lock()) {
            const auto text=target->edit_text;
            auto items=choices(keyCodeCandidates(value));
            std::optional<std::size_t> selected;
            for (std::size_t i = 0; i < items.size(); ++i) {
                if (items[i].text == text || items[i].value == text) {
                    selected = i;
                    break;
                }
            }
            const std::weak_ptr<lib::ComboBox> weak_target = target;
            (void)lib::replace_combo_items(target, std::move(items), selected, text,
                [current, weak_target, text] {
                    const auto control = weak_target.lock();
                    return current() && control && control->edit_text == text;
                });
        }
    };
    return editor;
}

KeyCodeView::TextEditPtr KeyCodeView::textEditor(const std::size_t index) {
    const std::weak_ptr<int> lifetime=callback_lifetime_;
    const auto epoch=table_?table_->row_order_revision:0;
    const auto revision=render_revision_;
    auto editor = createTextEdit(config_.services);
    editor->text = index < values_.size() ? values_[index] : String{};
    editor->read_only = read_only_ || !enabled();
    editor->set_enabled(!editor->read_only);
    editor->on_changed = [this, index,lifetime,epoch,revision](StringView text) {
        if(lifetime.expired() || revision!=render_revision_ || !table_ ||
           table_->row_order_revision!=epoch || read_only_ || !enabled()) return;
        editEnd(index, text);
    };
    return editor;
}

std::array<String, 4> KeyCodeView::ignitionTypeChoices(const std::size_t) const {
    return config_.resources.timingLabels;
}

void KeyCodeView::ignitionTypeCombo(const std::size_t index, Array<String>& strings,
                                    String& current) const {
    strings.clear();
    const auto values = ignitionTypeChoices(index);
    strings.insert(strings.end(), values.begin(), values.end());
    if (index < values_.size()) {
        current = values[kindIndex(activeSystem().fireKeyCodeKind(values_[index]))];
    } else {
        current.clear();
    }
}

void KeyCodeView::ignitionTypeEditEnd(const std::size_t index,
                                      const std::optional<std::size_t> choice) {
    if (!choice || *choice > 3U || index >= values_.size() || read_only_) return;
    const auto kind = static_cast<FKCKind>(static_cast<std::int32_t>(*choice));
    (void)editValue(index, activeSystem().convFireKeyCode(values_[index], kind));
}

KeyCodeView::ComboBoxPtr KeyCodeView::createIgnitionEditor(const std::size_t index) {
    const std::weak_ptr<int> lifetime=callback_lifetime_;
    const auto epoch=table_?table_->row_order_revision:0;
    const auto revision=render_revision_;
    auto editor = createComboBox(config_.services);
    const auto labels = ignitionTypeChoices(index);
    editor->editable = false;
    editor->items = choices(Array<String>(labels.begin(), labels.end()));
    if (index < values_.size())
        editor->selected_index = kindIndex(activeSystem().fireKeyCodeKind(values_[index]));
    editor->set_enabled(!read_only_ && enabled());
    editor->on_selection_changed = [this, index,lifetime,epoch,revision](std::optional<std::size_t> selected) {
        if(lifetime.expired() || revision!=render_revision_ || !table_ ||
           table_->row_order_revision!=epoch || read_only_ || !enabled()) return;
        ignitionTypeEditEnd(index, selected);
    };
    return editor;
}

WarningList KeyCodeView::warnings() const {
    const auto current=operationCurrent();
    const auto config=config_;
    const auto values=values_;
    // 外部の提供処理に入る前に、System に依存する警告を計算する。 提供処理
    // により破棄される所有者は、借用中の有効な System も所有しているため。
    std::vector<WarningList> per_value(values.size());
    WarningList trailing;
    for (std::size_t i=0;i<values.size();++i)
        if (config.withIgnitionType && !config.targetVersion150 &&
            activeSystem().fireKeyCodeKind(values[i])==FKCKind::HasNot)
            appendWarning(per_value[i],config.resources.warningHasNotKeyCode);
    if (!config.canDuplicate && !values.empty() && values.front()=="MatchingType=All")
        appendWarning(trailing,config.resources.searchErrorKeyCodeMatchingAll);
    if (config.canDuplicate && activeSystem().isRunAway(std::span<const String>(values)))
        appendWarning(trailing,warningFormat(config.resources.warningRunAwayCard,activeSystem().runAway()));
    if (config.canDuplicate && config.summaryLegacy && config.keyCodesMaxLegacy<values.size())
        appendWarning(trailing,warningFormat(config.resources.warningKeyCodeCount,config.keyCodesMaxLegacy));
    WarningList result;
    for (std::size_t i=0;i<values.size();++i) {
        if (config.warningProvider) {
            const auto warnings=config.warningProvider(values[i]);
            if (!current()) return {};
            for (const auto& warning:warnings) appendWarning(result,warning);
        }
        for (const auto& warning:per_value[i]) appendWarning(result,warning);
    }
    for (const auto& warning:trailing) appendWarning(result,warning);
    WarningList unique;
    for (auto& warning:result)
        if (std::ranges::find(unique,warning)==unique.end()) unique.push_back(std::move(warning));
    return unique;
}

WarningList KeyCodeView::getWarnings(const std::size_t index) const {
    if (index>=values_.size()) return {};
    const auto current=operationCurrent();
    const auto config=config_;
    const String value=values_[index];
    WarningList builtin;
    if (config.withIgnitionType && !config.targetVersion150 &&
        activeSystem().fireKeyCodeKind(value)==FKCKind::HasNot)
        appendWarning(builtin,config.resources.warningHasNotKeyCode);
    if (!config.canDuplicate && index==0 && value=="MatchingType=All")
        appendWarning(builtin,config.resources.searchErrorKeyCodeMatchingAll);
    if (config.canDuplicate && isRunAway(activeSystem(),value))
        appendWarning(builtin,warningFormat(config.resources.warningRunAwayCard,activeSystem().runAway()));
    WarningList result;
    if (config.warningProvider) {
        const auto warnings=config.warningProvider(value);
        if (!current()) return {};
        for (const auto& warning:warnings) appendWarning(result,warning);
    }
    for (const auto& warning:builtin) appendWarning(result,warning);
    return result;
}

bool KeyCodeView::canConvKeyCode(const FKCKind kind) const {
    if (read_only_) return false;
    if (config_.canDuplicate) {
        for (const auto& name : values_) {
            if (!name.empty() && name != activeSystem().convFireKeyCode(name, kind)) return true;
        }
        return false;
    }
    if (!config_.withIgnitionType && kind != FKCKind::Use && kind != FKCKind::HasNot)
        return false;
    KeyCodes names;
    for (const auto& name : values_)
        if (!name.empty()) appendUnique(names, name);
    for (const auto index : selection_) {
        if (index >= values_.size() || values_[index].empty()) continue;
        const auto converted = activeSystem().convFireKeyCode(values_[index], kind);
        if (std::find(names.begin(), names.end(), converted) == names.end()) return true;
        const auto old = std::find(names.begin(), names.end(), values_[index]);
        if (old != names.end()) names.erase(old);
        appendUnique(names, converted);
    }
    return false;
}

bool KeyCodeView::convKeyCode(const FKCKind kind) {
    if (!canConvKeyCode(kind)) return false;
    finishEditors();
    storeSnapshot();
    bool changed = false;
    if (config_.canDuplicate) {
        for (const auto index : selection_) {
            if (index >= values_.size() || values_[index].empty()) continue;
            const auto converted = activeSystem().convFireKeyCode(values_[index], kind);
            if (converted != values_[index]) {
                values_[index] = converted;
                changed = true;
            }
        }
    } else {
        KeyCodes names;
        for (const auto& name : values_)
            if (!name.empty()) appendUnique(names, name);
        for (const auto index : selection_) {
            if (index >= values_.size() || values_[index].empty()) continue;
            const auto old = values_[index];
            const auto converted = activeSystem().convFireKeyCode(old, kind);
            const auto oldIt = std::find(names.begin(), names.end(), old);
            if (oldIt != names.end()) names.erase(oldIt);
            if (std::find(names.begin(), names.end(), converted) != names.end()) {
                appendUnique(names, old);
                continue;
            }
            appendUnique(names, converted);
            if (converted != old) {
                values_[index] = converted;
                changed = true;
            }
        }
    }
    if (!changed) {
        if (!undo_.empty()) undo_.pop_back();
        return false;
    }
    rebuildTable(false, true);
    return true;
}

bool KeyCodeView::canCopyWith() const noexcept {
    for (const auto index : selection_)
        if (index < values_.size() && !values_[index].empty()) return true;
    return false;
}

String KeyCodeView::copyKeyCodeWith(const FKCKind kind) {
    if (!canCopyWith()) return {};
    // D版の keycodeview.d と同様、変換後の選択を直列化する前に、
    // 有効な TableTextEdit/TableComboEdit をす
    // べて確定する。 そうしないと、表示中の入力とコピーしたXMLが食い違う。
    finishEditors();
    KeyCodes converted;
    for (const auto index : selection_) {
        if (index < values_.size() && !values_[index].empty())
            converted.push_back(activeSystem().convFireKeyCode(values_[index], kind));
    }
    if (converted.empty()) return {};
    const auto xml = ::cwxeditor::cwx::card::keyCodesToXML(converted);
    const auto current=operationCurrent();
    writeKeyCodeXMLClipboard(config_, xml);
    if (current()) notifyRefreshToolBar();
    return xml;
}

Array<String> KeyCodeView::keyCodeCandidates(const StringView filter) const {
    const String owned_filter(filter);
    const auto source=config_.standardKeyCodes;
    const auto text=config_.services.text;
    const auto values=values_;
    Array<String> result;
    if (source) {
        for (const auto& value : source()) appendUnique(result, value);
    }
    for (const auto& value : values) appendUnique(result, value);
    if (owned_filter.empty()) return result;
    const auto folded_filter = text ? text->case_fold(owned_filter) : owned_filter;
    result.erase(std::remove_if(result.begin(), result.end(), [text, &owned_filter,
                                                                &folded_filter](const String& value) {
                     if (!text) return !containsFolded(value, owned_filter);
                     return text->case_fold(value).find(folded_filter) ==
                            String::npos;
                  }),
                 result.end());
    return result;
}

bool KeyCodeView::catchModifier() const {
    const auto current=operationCurrent();
    const auto callback=catch_mod_;
    const bool caught=callback && callback();
    return current() && caught;
}

bool KeyCodeView::canAddKeyCodesByFeatures() const {
    if (read_only_ || !enabled() || !config_.getMotions) return false;
    const auto current=operationCurrent();
    const auto by_features=config_.keyCodesByFeatures;
    const auto motions=config_.getMotions;
    const bool features=by_features && !by_features().empty();
    if (!current()) return false;
    if (features) return true;
    const bool has_motions=!motions().empty();
    return current() && has_motions;
}

bool KeyCodeView::addKeyCodesByFeatures() {
    ++feature_request_revision_;
    if (read_only_ || !enabled() || !config_.getMotions) return false;
    const std::weak_ptr<int> lifetime=callback_lifetime_;
    const auto current=operationCurrent();
    if (select_features_dialog_) {
        const auto picker=select_features_dialog_;
        picker->active();
        return !lifetime.expired();
    }
    const auto config=config_;
    const auto features=config.keyCodesByFeatures
        ? config.keyCodesByFeatures() : Array<KeyCodeByFeature>{};
    if (!current()) return false;
    if (features.empty()) return addKeyCodesByFeatures2({});
    if (config.featureSelection) {
        const auto selected=config.featureSelection();
        return current() && addKeyCodesByFeatures2(selected);
    }
    auto picker=std::make_shared<SelectCardFeaturesDialog>(config,config.dialogOwner.get());
    picker->setFeatureEntries(features);
    if (!current()) return false;
    const std::weak_ptr<SelectCardFeaturesDialog> weak=picker;
    picker->appliedEvent=[this,lifetime,weak] {
        if (lifetime.expired()) return;
        const auto source=weak.lock();
        if (!source || select_features_dialog_!=source) return;
        // 適用時には、モーダル画面を開いた時点のスナップショットではなく、現在の効果モーションを読む。
        const auto selected=source->keyCodes();
        (void)addKeyCodesByFeatures2(selected);
    };
    picker->closeEvent=[this,lifetime,weak] {
        if (lifetime.expired()) return;
        const auto source=weak.lock();
        if (source && select_features_dialog_==source) select_features_dialog_.reset();
    };
    select_features_dialog_=picker;
    const bool opened=picker->open();
    if (lifetime.expired()) return false;
    // ネイティブ側で表示が許可されなかった場合、保持されているが動作しない選択画面を残さない。
    if (!opened && !lifetime.expired() && select_features_dialog_==picker)
        select_features_dialog_.reset();
    return opened; // モードレスの場合は、同期呼び出し用の判定関数を保持しない。
}

bool KeyCodeView::addKeyCodesByFeatures2(const KeyCodes& byFeatures) {
    ++feature_request_revision_;
    if (read_only_ || !enabled()) return false;
    const std::weak_ptr<int> edit_lifetime=callback_lifetime_;
    const auto values_revision=values_revision_;
    const auto feature_request=feature_request_revision_;
    finishEditors(); // ネイティブの確定処理は、実際のホストへの通知・無効化・再接続を行う場合がある。
    if (edit_lifetime.expired() || values_revision!=values_revision_ ||
        feature_request!=feature_request_revision_ || read_only_ || !enabled()) return false;
    const auto current=operationCurrent();
    const auto config=config_;
    const KeyCodes features=byFeatures; // 現在の値の保存領域と同じ場所を参照する場合がある。
    const auto before=values_;
    const auto maximum=maxCount();
    KeyCodes existing=before;
    KeyCodes add;
    const auto append=[&](const String& code) {
        if (code.empty() || std::ranges::find(existing,code)!=existing.end()) return;
        existing.push_back(code);
        add.push_back(code);
    };
    for (const auto& code:features) append(code);
    if (config.getMotions && config.keyCodesByMotions) {
        const auto motions=config.getMotions();
        if (!current()) return false;
        const auto mappings=config.keyCodesByMotions();
        if (!current()) return false;
        for (const auto& motion:motions)
            for (const auto& mapping:mappings)
                if (mapping.match(motion.type,motion.element)) append(mapping.keyCode);
    }
    if (!current()) return false;
    KeyCodes rejected;
    if (maximum!=std::numeric_limits<std::size_t>::max()) {
        const auto available=before.size()>=maximum?0U:maximum-before.size();
        if (add.size()>available) {
            rejected.assign(add.begin()+static_cast<std::ptrdiff_t>(available),add.end());
            add.resize(available);
        }
    }
    last_add_errors_=rejected;
    if (add.empty() && rejected.empty()) return false;

    // appendValues は行を反映してからアプリへ通知する。 通知によって
    // このビューが閉じたり再読み込みされたりする場合があるため、 操作が置き換わっ
    // た後に古い上限超過ダイアログを開いたり、ツールバーを再び通知したりしない。
    const std::weak_ptr<int> lifetime=callback_lifetime_;
    const auto request=feature_request_revision_;
    const auto revision=render_revision_;
    const auto table=table_;
    const auto epoch=table?table->row_order_revision:0;
    const bool added=!add.empty() && appendValues(add);
    if (lifetime.expired() || feature_request_revision_!=request ||
        render_revision_!=revision+(added?1U:0U) || read_only_ || !enabled() ||
        table_!=table || (added && table && table->row_order_revision!=epoch+1)) return added;
    const auto completed=operationCurrent();
    if (!added) notifyRefreshToolBar();
    if (!completed()) return added;
    if (!rejected.empty() && config.services.gui) {
        AddKeyCodeErrorDialog dialog(config,rejected,config.dialogOwner.get());
        (void)dialog.open();
        // モーダル表示の提供処理の後は、メンバーにアクセスしない。
    }
    return added || !rejected.empty();
}

String KeyCodeView::warningFormat(String format, const StringView value) const {
    return replaceParameter(std::move(format), value);
}

String KeyCodeView::warningFormat(String format, const std::size_t value) const {
    return warningFormat(std::move(format), std::to_string(value));
}

SelectCardFeaturesDialog::SelectCardFeaturesDialog(KeyCodeViewConfig config,
                                                   ::cwxeditor::lib::Window* parent)
    : config_(std::move(config)), parent_(parent), dialog_(std::make_shared<Dialog>()) {
    dialog_->title = config_.resources.selectFeaturesText;
    dialog_->icon = config_.resources.addByFeaturesImage;
    ::cwxeditor::lib::configure_owned_window(dialog_, config_.dialogOwner,
        ::cwxeditor::lib::OwnedWindowKind::Modeless, parent_
            ? ::cwxeditor::lib::OwnedWindowCenter::OwnerOrHost
            : ::cwxeditor::lib::OwnedWindowCenter::Owner);
    dialog_->minimum_size = ::cwxeditor::lib::Size{360.0F, 300.0F};
}

SelectCardFeaturesDialog::~SelectCardFeaturesDialog() {
    callback_lifetime_.reset();
    if (notification_commons_ && !commons_lifetime_.expired()) {
        notification_commons_->refSkin.remove(skin_connection_);
        notification_commons_->refKeyCodesByFeatures.remove(feature_connection_);
    }
    if (dialog_) {
        dialog_->on_close_requested={};
        dialog_->set_visible(false);
    }
}

void SelectCardFeaturesDialog::refreshFeatures() {
    const std::weak_ptr<int> life=callback_lifetime_;
    const auto provider=config_.keyCodesByFeatures;
    const auto entries=provider ? provider() : Array<KeyCodeByFeature>{};
    if (life.expired()) return;
    if (entries.empty()) { forceCancel(); return; }
    // D版の仮想表と同様、空でないデータの再取得では、有効な選択・チ
    // ェック位置を維持する。 共通の行を再構築する前に保存しておく。
    auto selection = table_ ? table_->selection : selection_;
    auto checked = checked_;
    if (table_) {
        checked.clear();
        for (std::size_t i = 0; i < table_->rows.size(); ++i)
            if (table_->rows[i].checked) checked.push_back(i);
    }
    const auto retain_valid = [&entries](SizeIndices& indices) {
        indices.erase(std::remove_if(indices.begin(), indices.end(), [&entries](const auto index) {
                          return index >= entries.size();
                      }),
                      indices.end());
        std::sort(indices.begin(), indices.end());
        indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    };
    retain_valid(selection);
    retain_valid(checked);
    features_ = entries;
    selection_ = std::move(selection);
    checked_ = std::move(checked);
    key_codes_.clear();
    rebuildRows();
}

void SelectCardFeaturesDialog::setFeatureEntries(
    const Array<KeyCodeByFeature>& entries) {
    features_ = entries;
    selection_.clear();
    checked_.clear();
    key_codes_.clear();
    rebuildRows();
}

void SelectCardFeaturesDialog::setup(::cwxeditor::lib::Container& area) {
    if (initialized_) return;
    const std::weak_ptr<int> lifetime=callback_lifetime_;
    area.set_layout(std::make_shared<::cwxeditor::lib::GridLayout>());
    auto label = std::make_shared<::cwxeditor::lib::Label>(config_.resources.selectFeaturesHint);
    label->wrap = true;
    area.add(label);
    table_ = createTable(config_.services);
    table_->multi_select = true;
    table_->full_row_selection = true;
    table_->checkable = true;
    table_->check_selected_rows = true;
    table_->set_layout_item({.horizontal = ::cwxeditor::lib::Align::Stretch,
                             .vertical = ::cwxeditor::lib::Align::Stretch,
                             .minimum = {320.0F, 210.0F},
                             .grow = 1.0F});
    // D版の FullTableColumn と同様、見出しを付けず、サイズ変更を許さず、
    // 表のクライアント領域を埋める。 共通の TableView でもこの動作を維持する。
    table_->columns.push_back({{}, 50.0F, ::cwxeditor::lib::TextAlignment::Start,
                               false, false, 1.0F});
    table_->on_selection_changed = [this,lifetime](const std::vector<std::size_t>& indices) {
        if (lifetime.expired() || !table_) return;
        selection_ = indices;
    };
    table_->on_check_changed = [this,lifetime](const std::size_t index, const bool checked) {
        if (lifetime.expired()) return;
        const auto found = std::find(checked_.begin(), checked_.end(), index);
        if (checked && found == checked_.end()) checked_.push_back(index);
        else if (!checked && found != checked_.end()) checked_.erase(found);
        std::sort(checked_.begin(), checked_.end());
    };
    area.add(table_);

    auto menu = std::make_shared<::cwxeditor::lib::Menu>(true);
    menu->items.push_back({"すべて選択", "select-all"});
    const std::weak_ptr<::cwxeditor::lib::Menu> weak_menu = menu;
    const std::weak_ptr<Table> weak_table = table_;
    menu->on_open = [weak_menu, weak_table, lifetime] {
        if (lifetime.expired()) return;
        const auto current_menu = weak_menu.lock();
        const auto current_table = weak_table.lock();
        if (!current_menu || !current_table) return;
        setMenuCommandEnabled(*current_menu, "select-all",
            current_table->selection.size() < current_table->rows.size());
    };
    menu->on_command = [this,lifetime](const std::string_view command) {
        if (lifetime.expired()) return;
        if (command == "select-all") selectAll();
    };
    localizeKeyCodeHelperMenu(*menu, config_);
    const auto common_lifetime = config_.editorCommons
        ? config_.editorCommons->lifetimeToken() : std::weak_ptr<const int>{};
    table_->on_pointer = [this, menu,lifetime,common_lifetime](::cwxeditor::lib::PointerEvent& event) {
        if (lifetime.expired()) return;
        if (event.action != ::cwxeditor::lib::PointerAction::Down ||
            event.button != ::cwxeditor::lib::PointerButton::Secondary ||
            !config_.services.gui || !table_) return;
        const auto gui=config_.services.gui;
        const auto table=table_;
        event.handled = true;
        if (!common_lifetime.expired()) localizeKeyCodeHelperMenu(*menu, config_);
        if (menu->on_open) menu->on_open();
        gui->show_context_menu(menu,table,event.position);
    };

    auto footer = std::make_shared<::cwxeditor::lib::Panel>();
    footer->set_layout(std::make_shared<::cwxeditor::lib::LinearLayout>(
        ::cwxeditor::lib::Orientation::Horizontal));
    auto ok = std::make_shared<::cwxeditor::lib::Button>("OK");
    ok->default_button = true;
    ok->on_click = [this,lifetime] {
        if (lifetime.expired()) return;
        (void)close(true);
    };
    auto cancel = std::make_shared<::cwxeditor::lib::Button>("キャンセル");
    cancel->cancel_button = true;
    cancel->on_click = [this,lifetime] {
        if (lifetime.expired()) return;
        forceCancel();
    };
    footer->add(ok);
    footer->add(cancel);
    area.add(footer);
    initialized_ = true;
    rebuildRows();
}

void SelectCardFeaturesDialog::rebuildRows() {
    const auto revision=++rows_revision_;
    if (!table_) return;
    const std::weak_ptr<int> lifetime=callback_lifetime_;
    const auto target=table_;
    auto features=features_;
    const auto checked=checked_;
    std::vector<::cwxeditor::lib::TableRow> rows(features.size());
    for (std::size_t i=0;i<features.size();++i) {
        rows[i].cells={{features[i].feature,features[i].keyCode}};
        rows[i].checked=std::ranges::find(checked,i)!=checked.end();
    }
    if (!::cwxeditor::lib::replace_table_rows(*target,std::move(rows),selection_,{},[&] {
        return !lifetime.expired() && rows_revision_==revision && table_==target;
    })) return;
    published_features_.swap(features);
    published_revision_=revision;
}

bool SelectCardFeaturesDialog::open() {
    const auto keep_alive=weak_from_this().lock();
    const std::weak_ptr<int> lifetime=callback_lifetime_;
    const auto window=dialog_;
    const auto gui=config_.services.gui;
    if (open_) { active(); return !lifetime.expired(); }
    if (!initialized_) setup(*window);
    if (lifetime.expired() || published_revision_!=rows_revision_) return false;
    if (!gui) {
        // 単独で使用する、またはGUIを持たない補助処理は、モードレスウィンドウを表示でき
        // ない。 実際のGUIホストの動作は変えず、従来の一定した受け付け動作を維持する。
        if (checked_.empty() && !features_.empty()) {
            SizeIndices indices(features_.size());
            for (std::size_t i=0;i<indices.size();++i) indices[i]=i;
            setChecked(indices);
            if (lifetime.expired()) return false;
        }
        open_=true;
        bool cancel=false;
        (void)close(true,cancel);
        return !cancel;
    }
    if (!notification_commons_ && config_.editorCommons) {
        notification_commons_=config_.editorCommons;
        commons_lifetime_=notification_commons_->lifetimeToken();
        const auto refresh=[this,lifetime] { if (!lifetime.expired()) refreshFeatures(); };
        skin_connection_=notification_commons_->refSkin.add(refresh);
        feature_connection_=notification_commons_->refKeyCodesByFeatures.add(refresh);
    }
    window->on_close_requested=[this,lifetime] {
        if (!lifetime.expired()) forceCancel();
    };
    ::cwxeditor::lib::configure_owned_window(window,config_.dialogOwner,
        ::cwxeditor::lib::OwnedWindowKind::Modeless);
    window->result=::cwxeditor::lib::DialogResult::None;
    open_=true;
    if (gui) gui->show(window);
    else window->set_visible(true);
    return !lifetime.expired(); // 同期的なネイティブの Show が、単独使用の所有者を無効化する場合がある。
}

void SelectCardFeaturesDialog::active() {
    const auto window=dialog_;
    const auto gui=config_.services.gui;
    if (!open_ || !window) return;
    const std::weak_ptr<int> lifetime=callback_lifetime_;
    if (gui) {
        gui->show(window);
        if (lifetime.expired() || !open_) return;
        gui->focus(window);
    } else window->set_visible(true);
}

void SelectCardFeaturesDialog::forceCancel() { (void)close(false); }

void SelectCardFeaturesDialog::selectAll() {
    // D版と同様、行の選択と特性の採用は分ける。採用は強調表示ではなく CHECK
    // 状態で決める。 全選択によって、すべての特性を暗黙にチェックしない。
    if (!table_) return;
    SizeIndices indices(table_->rows.size());
    for (std::size_t i=0;i<indices.size();++i) indices[i]=i;
    (void)::cwxeditor::lib::select_table_rows(*table_,std::move(indices));
}

void SelectCardFeaturesDialog::setChecked(const SizeIndices& indices) {
    checked_ = indices;
    checked_.erase(std::remove_if(checked_.begin(), checked_.end(), [this](const std::size_t i) {
                       return i >= features_.size();
                   }),
                   checked_.end());
    std::sort(checked_.begin(), checked_.end());
    checked_.erase(std::unique(checked_.begin(), checked_.end()), checked_.end());
    rebuildRows();
}

bool SelectCardFeaturesDialog::close(const bool ok, bool& cancel) {
    if (closing_) { cancel=!ok; return true; }
    // 閉じる・適用の操作が、親の保持する選択画面を解除したりビューを無効化したりする場
    // 合がある。 通知前に、このトランザクションを保持し、すべての監視先を複製する。
    const auto keep_alive=weak_from_this().lock();
    const std::weak_ptr<int> lifetime=callback_lifetime_;
    const auto window=dialog_;
    const auto gui=config_.services.gui;
    const auto applied=appliedEvent;
    const auto closed=closeEvent;
    cancel = !ok || (table_ && published_revision_!=rows_revision_);
    if (!cancel) {
        if (table_ && table_->checkable) {
            checked_.clear();
            for (std::size_t index=0;index<table_->rows.size();++index)
                if (table_->rows[index].checked) checked_.push_back(index);
        }
        key_codes_.clear();
        const auto& features=table_?published_features_:features_;
        for (const auto index:checked_)
            if (index<features.size()) appendUnique(key_codes_,features[index].keyCode);
    }
    const bool was_open=open_;
    closing_=true;
    try {
        if (!cancel && was_open && applied) applied();
    } catch (...) {
        // D版と同様、適用に失敗しても操作可能なモードレス編集画面を残す。
        // 再入した終了操作が、実行中の適用処理と競合しないようにする。
        if (!lifetime.expired()) closing_=false;
        throw;
    }
    if (!lifetime.expired()) { open_=false; closing_=false; }
    if (window) {
        window->result=cancel ? ::cwxeditor::lib::DialogResult::Cancel
                              : ::cwxeditor::lib::DialogResult::OK;
        window->set_visible(false);
        if (gui && was_open) gui->close(window);
    }
    if (!lifetime.expired() && was_open && closed) closed();
    return true;
}

bool SelectCardFeaturesDialog::close(const bool ok) {
    bool cancel = false;
    return close(ok, cancel);
}

AddKeyCodeErrorDialog::AddKeyCodeErrorDialog(KeyCodeViewConfig config, KeyCodes keyCodes,
                                             ::cwxeditor::lib::Window* parent)
    : config_(std::move(config)), parent_(parent), dialog_(std::make_shared<Dialog>()),
      key_codes_(std::move(keyCodes)) {
    dialog_->title = config_.resources.addKeyCodesErrorText;
    dialog_->icon = config_.resources.warningImage;
    ::cwxeditor::lib::configure_owned_window(dialog_, config_.dialogOwner,
        ::cwxeditor::lib::OwnedWindowKind::Modal, parent_
            ? ::cwxeditor::lib::OwnedWindowCenter::OwnerOrHost
            : ::cwxeditor::lib::OwnedWindowCenter::Owner);
    dialog_->minimum_size = ::cwxeditor::lib::Size{360.0F, 300.0F};
}

void AddKeyCodeErrorDialog::setup(::cwxeditor::lib::Container& area) {
    if (initialized_) {
        if (!rows_ready_) rebuildRows();
        return;
    }
    const std::weak_ptr<int> lifetime=callback_lifetime_;
    area.set_layout(std::make_shared<::cwxeditor::lib::GridLayout>());
    auto description = std::make_shared<::cwxeditor::lib::Label>(
        replaceParameter(config_.resources.addKeyCodesErrorDesc,
                         std::to_string(config_.keyCodesMaxLegacy)));
    description->wrap = true;
    area.add(description);
    table_ = createTable(config_.services);
    table_->multi_select = true;
    table_->full_row_selection = true;
    // D版の FullTableColumn と同様、見出しを付けず、サイズ変更を許さず、
    // 表のクライアント領域を埋める。 共通の TableView でもこの動作を維持する。
    table_->columns.push_back({{}, 50.0F, ::cwxeditor::lib::TextAlignment::Start,
                               false, false, 1.0F});
    table_->set_layout_item({.horizontal = ::cwxeditor::lib::Align::Stretch,
                             .vertical = ::cwxeditor::lib::Align::Stretch,
                             .minimum = {320.0F, 210.0F},
                             .grow = 1.0F});
    area.add(table_);
    initialized_ = true;

    auto menu = std::make_shared<::cwxeditor::lib::Menu>(true);
    menu->items.push_back({"コピー", "copy"});
    menu->items.push_back({{}, {}, {}, 0, true, false, false, true});
    menu->items.push_back({"すべて選択", "select-all"});
    const std::weak_ptr<::cwxeditor::lib::Menu> weak_menu = menu;
    const std::weak_ptr<Table> weak_table = table_;
    menu->on_open = [weak_menu, weak_table, lifetime] {
        if (lifetime.expired()) return;
        const auto current_menu = weak_menu.lock();
        const auto current_table = weak_table.lock();
        if (!current_menu || !current_table) return;
        setMenuCommandEnabled(*current_menu, "copy", !current_table->selection.empty());
        setMenuCommandEnabled(*current_menu, "select-all",
            current_table->selection.size() < current_table->rows.size());
    };
    menu->on_command = [this,lifetime](const std::string_view command) {
        if (lifetime.expired()) return;
        if (command == "copy") copy();
        else if (command == "select-all") selectAll();
    };
    localizeKeyCodeHelperMenu(*menu, config_);
    const auto common_lifetime = config_.editorCommons
        ? config_.editorCommons->lifetimeToken() : std::weak_ptr<const int>{};
    table_->on_pointer = [this, menu,lifetime,common_lifetime](::cwxeditor::lib::PointerEvent& event) {
        if (lifetime.expired()) return;
        if (event.action != ::cwxeditor::lib::PointerAction::Down ||
            event.button != ::cwxeditor::lib::PointerButton::Secondary ||
            !config_.services.gui || !table_) return;
        const auto gui=config_.services.gui;
        const auto table=table_;
        event.handled = true;
        if (!common_lifetime.expired()) localizeKeyCodeHelperMenu(*menu, config_);
        if (menu->on_open) menu->on_open();
        gui->show_context_menu(menu,table,event.position);
    };

    auto footer = std::make_shared<::cwxeditor::lib::Panel>();
    footer->set_layout(std::make_shared<::cwxeditor::lib::LinearLayout>(
        ::cwxeditor::lib::Orientation::Horizontal));
    auto ok = std::make_shared<::cwxeditor::lib::Button>("OK");
    ok->default_button = true;
    ok->on_click = [this,lifetime] {
        if (lifetime.expired()) return;
        if (dialog_) dialog_->result = ::cwxeditor::lib::DialogResult::OK;
    };
    footer->add(ok);
    area.add(footer);
    rebuildRows();
}

void AddKeyCodeErrorDialog::rebuildRows() {
    if (!table_) return;
    const auto revision = ++rows_revision_;
    rows_ready_ = false;
    const std::weak_ptr<int> lifetime=callback_lifetime_;
    const auto target=table_;
    std::vector<::cwxeditor::lib::TableRow> rows(key_codes_.size());
    for (std::size_t i = 0; i < key_codes_.size(); ++i)
        rows[i].cells = {{key_codes_[i], key_codes_[i], config_.resources.keyCodeImage}};
    const bool published=::cwxeditor::lib::replace_table_rows(*target, std::move(rows), selection_, {}, [&] {
        return !lifetime.expired() && rows_revision_==revision && table_==target;
    });
    if (lifetime.expired() || rows_revision_!=revision) return;
    rows_ready_=published;
}

bool AddKeyCodeErrorDialog::open() {
    const std::weak_ptr<int> lifetime=callback_lifetime_;
    const auto window=dialog_;
    const auto gui=config_.services.gui;
    auto owner=config_.dialogOwner;
    if (!owner && parent_)
        owner=std::shared_ptr<::cwxeditor::lib::Window>(parent_,[](::cwxeditor::lib::Window*) {});
    if (!window) return false;
    if (!initialized_ || !rows_ready_) setup(*window);
    if (lifetime.expired() || !rows_ready_) return false;
    if (!gui) {
        window->result=::cwxeditor::lib::DialogResult::OK;
        return true;
    }
    ::cwxeditor::lib::configure_owned_window(window, owner,
        ::cwxeditor::lib::OwnedWindowKind::Modal);
    auto* commons = config_.editorCommons;
    const auto commons_life = commons ? commons->lifetimeToken() : std::weak_ptr<const int>{};
    auto* props = commons && !commons_life.expired() ? commons->prop() : nullptr;
    if (props) {
        const auto& saved = props->flex().addKeyCodesErrorDlg;
        auto bounds = window->bounds();
        bounds.width = static_cast<float>(std::max(360, saved.width()));
        bounds.height = static_cast<float>(std::max(300, saved.height()));
        window->bounds_ui_text_scale = 1.0F;
        window->set_bounds(bounds);
    }
    window->result=::cwxeditor::lib::DialogResult::None;
    const auto result=gui->show_modal(window,owner);
    if (props && !commons_life.expired() && !window->maximized && !window->minimized) {
        const auto bounds = window->bounds();
        const auto scale = std::max(1.0F, window->bounds_ui_text_scale);
        auto& saved = props->flex().addKeyCodesErrorDlg;
        saved.width(static_cast<int>(bounds.width / scale + 0.5F));
        saved.height(static_cast<int>(bounds.height / scale + 0.5F));
    }
    if (lifetime.expired()) return false;
    return close(result==::cwxeditor::lib::DialogResult::OK ||
                 result==::cwxeditor::lib::DialogResult::Yes);
}

void AddKeyCodeErrorDialog::selectAll() {
    SizeIndices indices(key_codes_.size());
    for (std::size_t i = 0; i < indices.size(); ++i) indices[i] = i;
    setSelection(indices);
}

void AddKeyCodeErrorDialog::setSelection(const SizeIndices& indices) {
    if (table_) {
        // 選択・フォーカス・通知は共通の表モデルが管理する。
        // リスナーの実行後に、選択をもう一度書き込まない。
        (void)::cwxeditor::lib::select_table_rows(*table_,indices);
        return;
    }
    selection_ = indices;
    selection_.erase(std::remove_if(selection_.begin(), selection_.end(), [this](const std::size_t i) {
                       return i >= key_codes_.size();
                   }),
                   selection_.end());
    std::sort(selection_.begin(), selection_.end());
    selection_.erase(std::unique(selection_.begin(), selection_.end()), selection_.end());
}

String AddKeyCodeErrorDialog::copySelectedXML() {
    KeyCodes selected;
    for (const auto index : selection())
        if (index < key_codes_.size()) selected.push_back(key_codes_[index]);
    if (selected.empty()) return {};
    const auto xml = ::cwxeditor::cwx::card::keyCodesToXML(selected);
    writeKeyCodeXMLClipboard(config_, xml);
    return xml;
}

void AddKeyCodeErrorDialog::copy() {
    const std::weak_ptr<int> lifetime = callback_lifetime_;
    const auto refresh = config_.refreshToolBar;
    const auto copied = copySelectedXML();
    if (lifetime.expired() || copied.empty()) return;
    if (refresh) refresh();
}

bool AddKeyCodeErrorDialog::close(const bool ok) {
    if (dialog_) dialog_->result = ok ? ::cwxeditor::lib::DialogResult::OK
                                     : ::cwxeditor::lib::DialogResult::Cancel;
    return true;
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::keycodeview
