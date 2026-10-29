#include "cwxeditor_src/gui/cwx/editor/gui/dwt/scripterrordialog.hpp"
#include "cwxeditor_src/core/cwx/msgs.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dutils.hpp"
#include "cwxeditor_src/core/cwx/props.hpp"
#include "cwxeditor_src/lib/ui_window_lifecycle.hpp"
#include "cwxeditor_src/lib/window_host.hpp"
// UIの実装は一つに統一し、
// 別経路の実装を設けない。

#include <algorithm>
#include <limits>
#include <utility>

namespace cwxeditor::cwx::editor::gui::dwt::scripterrordialog {
ScriptErrorPresentation presentationFromProps(const dprops::Props& prop) {
    ScriptErrorPresentation value;
    value.title=prop.requiredMsgs().dlgTitScriptError.value;
    value.icon=prop.images().script();
    value.heading=prop.requiredMsgs().scriptError.value;
    value.too_many_errors=prop.requiredMsgs().scriptErrorOver100Error.value;
    value.tab_width=static_cast<std::size_t>(std::max(1U,prop.var().tabs.value));
    value.font=dutils::fontSpecFromDwtData(prop.adjustFont(prop.looks().scriptErrorFont(12)),false);
    return value;
}

ScriptErrorPresentation presentationFromProps(dprops::Props& prop) {
    auto value = presentationFromProps(static_cast<const dprops::Props&>(prop));
    if (!lib::editor_window_host_options().all_mode) {
        value.size = {static_cast<float>(prop.flex().scriptDlg.width()),
                      static_cast<float>(prop.flex().scriptDlg.height())};
        value.bind_size = [&prop](const std::shared_ptr<::cwxeditor::lib::Window>& window) {
            return prop.bindDialogSize(window, prop.flex().scriptDlg);
        };
    }
    return value;
}

namespace {

std::vector<std::string> splitLines(std::string_view value) {
    std::vector<std::string> lines;
    std::size_t begin = 0;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] != '\n' && value[i] != '\r') continue;
        lines.emplace_back(value.substr(begin, i - begin));
        if (value[i] == '\r' && i + 1 < value.size() && value[i + 1] == '\n')
            ++i;
        begin = i + 1;
    }
    lines.emplace_back(value.substr(begin));
    return lines;
}

std::size_t utf8SequenceLength(std::string_view value, std::size_t offset) {
    const auto first = static_cast<unsigned char>(value[offset]);
    std::size_t length = 1;
    if ((first & 0x80U) == 0) return length;
    if ((first & 0xe0U) == 0xc0U) length = 2;
    else if ((first & 0xf0U) == 0xe0U) length = 3;
    else if ((first & 0xf8U) == 0xf0U) length = 4;
    else return 1;
    if (offset + length > value.size()) return 1;
    for (std::size_t i = 1; i < length; ++i) {
        if ((static_cast<unsigned char>(value[offset + i]) & 0xc0U) != 0x80U)
            return 1;
    }
    return length;
}

std::optional<std::size_t> addedLineIndex(std::size_t line,
                                          std::ptrdiff_t start,
                                          std::ptrdiff_t count) {
    if (count <= 0) return std::nullopt;
    // トークンの行番号は、D版の符号付き座標を size_t で表した値。 符
    // 号なしの巡回する減算で startLine を戻すことで、論理行ゼロより
    // 前に生成された行も扱う。 丸めたり拒否したりすると、その出所が失われる。
    const auto relative = line - static_cast<std::size_t>(start);
    if (relative < static_cast<std::size_t>(count)) return relative;
    return std::nullopt;
}

void appendCaret(std::string& output, std::string_view line,
                 std::size_t bytePosition) {
    const auto limit = std::min(bytePosition, line.size());
    for (std::size_t i = 0; i < limit;) {
        if (line[i] == '\t') {
            output.push_back('\t');
            ++i;
            continue;
        }
        const auto length = utf8SequenceLength(line, i);
        output.append(length > 1 ? 2U : 1U, ' ');
        i += length;
    }
    output.push_back('^');
}

}  // namespace

ScriptErrorDialog::ScriptErrorDialog(
    Services services, std::shared_ptr<Widget> parent,
    const ScriptException& exception, std::string base, CompileOption option,
    ScriptErrorPresentation presentation)
    : services_(std::move(services)), parent_(std::move(parent)),
      source_text_(exception.text()), errors_(exception.errors()),
      over100_(exception.over100()), base_(std::move(base)), option_(option),
      presentation_(std::move(presentation)) {
    owner_ = std::dynamic_pointer_cast<Window>(parent_);
    if (services_.gui) dialog_ = services_.gui->create_dialog();
    if (!dialog_) dialog_ = std::make_shared<Dialog>();

    dialog_->title = presentation_.title;
    dialog_->icon = presentation_.icon;
    lib::configure_owned_window(dialog_, owner_,
        lib::OwnedWindowKind::Modeless);
    dialog_->resizable = true;
    dialog_->result = DialogResult::None;
    if (presentation_.size.width > 0.0F || presentation_.size.height > 0.0F) {
        auto bounds = dialog_->bounds();
        if (presentation_.size.width > 0.0F)
            bounds.width = presentation_.size.width;
        if (presentation_.size.height > 0.0F)
            bounds.height = presentation_.size.height;
        dialog_->set_bounds(bounds);
    }
    if (presentation_.bind_size)
        release_size_binding_ = presentation_.bind_size(dialog_);
    const std::weak_ptr<void> life=lifetime_;
    dialog_->on_close_requested = [this,life] { if (!life.expired()) close(DialogResult::Cancel); };

    display_text_ = formatDisplay(
        presentation_.heading, errors_, over100_,
        presentation_.too_many_errors, base_, source_text_, option_);
}

ScriptErrorDialog::~ScriptErrorDialog() {
    if (release_size_binding_) release_size_binding_();
    release_size_binding_ = {};
    lifetime_.reset();
    if (viewer_) viewer_->on_changed={};
    if (close_button_) close_button_->on_click={};
    if (dialog_) {
        dialog_->on_close_requested={};
        if (dialog_->visible()) {
            if (services_.gui) services_.gui->close(dialog_);
            else dialog_->set_visible(false);
        }
    }
}

std::string ScriptErrorDialog::formatError(
    const ScriptException& exception, const ::cwxeditor::cwx::script::Props* prop,
    std::string_view base, const CompileOption& option) {
    ScriptErrorPresentation presentation;
    if (prop) {
        presentation.heading = prop->requiredMsgs().scriptError.value;
        presentation.too_many_errors = prop->requiredMsgs().scriptErrorOver100Error.value;
    }
    return formatDisplay(presentation.heading, exception.errors(), exception.over100(),
                         presentation.too_many_errors, base, exception.text(), option);
}

std::string ScriptErrorDialog::formatDisplay(
    std::string_view heading, const std::vector<ScriptError>& errors,
    bool over100, std::string_view over100Message, std::string_view base,
    std::string_view source, const CompileOption& option) {
    const auto lines = splitLines(base);
    const auto sourceLines = base == source ? lines : splitLines(source);

    std::string output;
    output.reserve(heading.size() + source.size() + errors.size() * 64U);
    output.append(heading);
    output.push_back('\n');
    for (const auto& error : errors) {
        output.push_back('\n');
        output.append(error.message);
        output.push_back('\n');
#if !defined(NDEBUG)
        output += "Debug info: ";
        output += error.file;
        output += ", ";
        output += std::to_string(error.line);
        output.push_back('\n');
#endif

        std::string_view line;
        std::string label;
        const auto inserted = addedLineIndex(error.errLine, option.startLine,
                                             option.addLines);
        if (inserted && *inserted < sourceLines.size()) {
            line = sourceLines[*inserted];
            label = "Line ---: ";
        } else {
            if (error.errLine < lines.size()) line = lines[error.errLine];
            const auto lineNumber = error.errLine == std::numeric_limits<std::size_t>::max()
                ? error.errLine
                : error.errLine + 1U;
            label = "Line " + std::to_string(lineNumber) + ": ";
        }
        output.append(label);
        output.append(line);
        output.push_back('\n');
        output.append(label.size(), ' ');
        appendCaret(output, line, error.errPos);
    }
    if (over100) {
        output.push_back('\n');
        output.append(over100Message);
        output.push_back('\n');
    }
    return output;
}

void ScriptErrorDialog::setup(Container& area) {
    if (!viewer_) {
        ::cwxeditor::lib::WidgetPtr widget;
        if (services_.gui)
            widget = services_.gui->create_component(
                ::cwxeditor::lib::ComponentKind::TextEdit);
        viewer_ = std::dynamic_pointer_cast<TextEdit>(std::move(widget));
        if (!viewer_) viewer_ = std::make_shared<TextEdit>();
    }
    if (!close_button_) {
        ::cwxeditor::lib::WidgetPtr widget;
        if (services_.gui)
            widget = services_.gui->create_component(
                ::cwxeditor::lib::ComponentKind::Button);
        close_button_ = std::dynamic_pointer_cast<Button>(std::move(widget));
        if (!close_button_) close_button_ = std::make_shared<Button>();
    }

    if (!area.layout())
        area.set_layout(std::make_shared<::cwxeditor::lib::LinearLayout>(
            ::cwxeditor::lib::Orientation::Vertical));
    const auto& initialChildren = area.children();
    if (std::find(initialChildren.begin(), initialChildren.end(), viewer_) ==
        initialChildren.end())
        area.add(viewer_);
    const auto& currentChildren = area.children();
    if (std::find(currentChildren.begin(), currentChildren.end(), close_button_) ==
        currentChildren.end())
        area.add(close_button_);

    viewer_->multiline = true;
    viewer_->read_only = true;
    viewer_->wrap = true;
    viewer_->text = display_text_;
    viewer_->tab_width = presentation_.tab_width;
    viewer_->selection_start = 0;
    viewer_->selection_length = 0;
    if (presentation_.font) viewer_->set_font(presentation_.font);
    auto textLayout = ::cwxeditor::lib::LayoutItem{};
    textLayout.grow = 1.0F;
    viewer_->set_layout_item(textLayout);
    const std::weak_ptr<void> life=lifetime_;
    viewer_->on_changed = [this,life](std::string_view value) {
        if (life.expired()) return;
        display_text_.assign(value.data(), value.size());
    };
    // D版と同様、読み取り専用のエラー表示欄にも標準のテキストメニューを付ける。
    if (!text_menu_) {
        dmenu::Commons common;
        common.services = services_;
        // D版と同様、Props.tabs を適用する前にテキストメニューを付け
        // る。 共通のメニュー接続処理もタブ幅を書くため、同じ表示設定を渡す。
        common.presentation.textTabs = presentation_.tab_width;
        text_menu_ = dmenu::createTextMenu(common, dmenu::TMM(viewer_));
    }

    close_button_->text = presentation_.close_label;
    close_button_->default_button = true;
    close_button_->cancel_button = false;
    close_button_->on_click = [this,life] { if (!life.expired()) close(DialogResult::OK); };
    initialized_ = true;
}

void ScriptErrorDialog::setup() {
    if (!area_) {
        area_ = std::make_shared<Panel>();
        area_->set_layout(std::make_shared<::cwxeditor::lib::LinearLayout>(
            ::cwxeditor::lib::Orientation::Vertical));
        dialog_->add(area_);
    }
    setup(*area_);
}

void ScriptErrorDialog::selectAll() {
    if (!viewer_) return;
    viewer_->selection_start = 0;
    viewer_->selection_length = viewer_->text.size();
}

void ScriptErrorDialog::setSelection(std::size_t start, std::size_t length) {
    if (!viewer_) return;
    start = std::min(start, viewer_->text.size());
    length = std::min(length, viewer_->text.size() - start);
    viewer_->selection_start = start;
    viewer_->selection_length = length;
}

std::string ScriptErrorDialog::selectedText() const {
    if (!viewer_ || viewer_->selection_start >= viewer_->text.size()) return {};
    const auto start = viewer_->selection_start;
    const auto length = std::min(viewer_->selection_length,
                                 viewer_->text.size() - start);
    return viewer_->text.substr(start, length);
}

std::size_t ScriptErrorDialog::selectionStart() const noexcept {
    return viewer_ ? viewer_->selection_start : 0U;
}

std::size_t ScriptErrorDialog::selectionLength() const noexcept {
    return viewer_ ? viewer_->selection_length : 0U;
}

bool ScriptErrorDialog::canCopy() const noexcept {
    return services_.clipboard && viewer_ && viewer_->selection_start < viewer_->text.size()
        && viewer_->selection_length != 0U;
}

bool ScriptErrorDialog::copySelection() {
    if (!canCopy()) return false;
    auto selected = selectedText();
    if (selected.empty()) return false;
    ::cwxeditor::lib::ClipboardData data;
    data.text = std::move(selected);
    services_.clipboard->write(data);
    return true;
}

ScriptErrorDialog::DialogResult ScriptErrorDialog::show() {
    if (!initialized_) setup();
    closed_ = false;
    if (services_.gui) {
        if (dialog_->modal) {
            dialog_->result = services_.gui->show_modal(dialog_, owner_);
        } else {
            services_.gui->show(dialog_);
        }
    } else {
        dialog_->set_visible(true);
    }
    if (dialog_->result != DialogResult::None) closed_ = true;
    return dialog_->result;
}

bool ScriptErrorDialog::open() {
    const auto value = show();
    return dialog_->modal &&
           (value == DialogResult::OK || value == DialogResult::Apply);
}

bool ScriptErrorDialog::close() {
    close(DialogResult::Cancel);
    return closed_;
}

void ScriptErrorDialog::close(DialogResult result) {
    if (!dialog_) return;
    dialog_->result = result;
    closed_ = true;
    if (release_size_binding_) {
        auto release = std::move(release_size_binding_);
        release_size_binding_ = {};
        release();
    }
    if (services_.gui) services_.gui->close(dialog_);
    else dialog_->set_visible(false);
}

void ScriptErrorDialog::notifyParentClosed() {
    if (!closed_) close(DialogResult::Cancel);
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::scripterrordialog
