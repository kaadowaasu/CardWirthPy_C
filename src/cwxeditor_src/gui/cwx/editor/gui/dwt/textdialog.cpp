#include "cwxeditor_src/gui/cwx/editor/gui/dwt/textdialog.hpp"
#include "cwxeditor_src/core/cwx/perf.hpp"
#include "cwxeditor_src/lib/ui_window_lifecycle.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dprops.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dutils.hpp"
// UIの実装は一つに統一し、
// 別経路の実装を設けない。

#include <algorithm>
#include <utility>

namespace cwxeditor::cwx::editor::gui::dwt::textdialog {

TextDialog::TextDialog(Services services, std::shared_ptr<Window> parent,
                       std::string title, ::cwxeditor::lib::ResourceId icon,
                       std::string text, bool readOnly,
                       ::cwxeditor::lib::Size size, dprops::Props* prop)
    : services_(std::move(services)), prop_(prop), parent_(std::move(parent)),
      text_(std::move(text)), readOnly_(readOnly), icon_(icon), size_(size) {
    configureDialog(std::move(title), icon, size);
    if (readOnly_) {
        // 読み取り専用のテキストウィンドウは、明示的な結果なしで閉じられ
        // る。 共通モデルでは終了コールバックを通じてその方針を表す。
        dialog_->on_close_requested = [this] { close(DialogResult::OK); };
    }
}

TextDialog::~TextDialog() {
    if (viewer_) viewer_->on_changed = {};
    if (ok_) ok_->on_click = {};
    if (cancel_) cancel_->on_click = {};
    if (dialog_) {
        dialog_->on_close_requested = {};
        dialog_->on_visibility_changed = {};
    }
}

void TextDialog::configureDialog(std::string title,
                                 ::cwxeditor::lib::ResourceId icon,
                                 ::cwxeditor::lib::Size size) {
    if (services_.gui) dialog_ = services_.gui->create_dialog();
    if (!dialog_) dialog_ = std::make_shared<Dialog>();

    dialog_->title = std::move(title);
    ::cwxeditor::lib::configure_owned_window(dialog_, parent_,
        ::cwxeditor::lib::OwnedWindowKind::Modal);
    dialog_->set_visible(false);
    dialog_->result = DialogResult::None;
    icon_ = icon;
    dialog_->icon = icon_;
    size_ = size;
    if (size.width > 0.0F || size.height > 0.0F) {
        auto bounds = dialog_->bounds();
        if (size.width > 0.0F) bounds.width = size.width;
        if (size.height > 0.0F) bounds.height = size.height;
        dialog_->set_bounds(bounds);
    }
    if (!dialog_->on_close_requested) {
        dialog_->on_close_requested = [this] { close(DialogResult::Cancel); };
    }
}

std::string TextDialog::normalizeReturnCode(std::string_view value) {
    // DWTの wrapReturnCode と同様、WindowsではCRLF
    // からCRを除く。 共通の状態管理側で正規化し、TextEdit を
    // 提供したGUIバックエンドによらず text() の値を揃える。
    std::string result;
    result.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\r' && i + 1 < value.size() && value[i + 1] == '\n')
            continue;
        result.push_back(value[i]);
    }
    return result;
}

std::string TextDialog::text() const {
    ::cwxeditor::cwx::perf::TraceScope trace_text(__FILE__, __LINE__);
    if (viewer_) return normalizeReturnCode(viewer_->text);
    return normalizeReturnCode(text_);
}

void TextDialog::setText(std::string value) {
    text_ = std::move(value);
    if (!viewer_) return;
    viewer_->text = text_;
    if (readOnly_) {
        viewer_->selection_start = 0;
        viewer_->selection_length = 0;
    } else {
        viewer_->selection_start = viewer_->text.size();
        viewer_->selection_length = 0;
    }
}

void TextDialog::setup(Container& area) {
    ::cwxeditor::cwx::perf::TraceScope trace_setup(__FILE__, __LINE__);
    if (!viewer_) {
        ::cwxeditor::lib::WidgetPtr widget;
        if (services_.gui)
            widget = services_.gui->create_component(
                ::cwxeditor::lib::ComponentKind::TextEdit);
        viewer_ = std::dynamic_pointer_cast<TextEdit>(std::move(widget));
        if (!viewer_) viewer_ = std::make_shared<TextEdit>();
    }

    if (!area.layout()) {
        auto layout = std::make_shared<::cwxeditor::lib::LinearLayout>();
        layout->spacing = 6.0F;
        area.set_layout(std::move(layout));
    }
    const auto& children = area.children();
    if (std::find(children.begin(), children.end(), viewer_) == children.end())
        area.add(viewer_);

    viewer_->multiline = true;
    viewer_->wrap = true;
    viewer_->read_only = readOnly_;
    if (!initialized_) {
        viewer_->text = text_;
        viewer_->selection_length = 0;
        if (readOnly_) {
            viewer_->selection_start = 0;
        } else {
            ::cwxeditor::cwx::perf::TraceScope trace_editable_selection(__FILE__, __LINE__);
            viewer_->selection_start = viewer_->text.size();
        }
        if (prop_) {
            const auto default_size = viewer_->font()
                ? static_cast<std::uint32_t>(viewer_->font()->points) : 0U;
            viewer_->set_font(dutils::fontSpecFromDwtData(
                prop_->adjustFont(prop_->looks().textDlgFont(default_size))));
        }
    }
    viewer_->set_layout_item({0, 0, 1, 1, ::cwxeditor::lib::Align::Stretch,
                              ::cwxeditor::lib::Align::Stretch, {}, {}, {}, 1.0F});
    viewer_->on_changed = [this](std::string_view value) {
        text_.assign(value.data(), value.size());
    };
    if (!text_menu_) {
        dmenu::Commons common;
        common.services = services_;
        if (prop_) {
            common.presentation.menuText = [prop = prop_](auto id) {
                return prop->buildMenu(id);
            };
            common.presentation.menuImage = [prop = prop_](auto id) {
                return prop->images().menu(id);
            };
        }
        text_menu_ = dmenu::createTextMenu(common, dmenu::TMM(viewer_));
    }
    if (!buttons_) {
        buttons_ = std::make_shared<::cwxeditor::lib::Panel>();
        auto layout = std::make_shared<::cwxeditor::lib::GridLayout>();
        layout->columns = readOnly_ ? 1U : 2U;
        layout->equal_width = true;
        layout->spacing = 6.0F;
        buttons_->set_layout(std::move(layout));
        ok_ = std::make_shared<::cwxeditor::lib::Button>();
        // D版の TextDialog と同様、読み取り専用の表示も含めて AbsDialog の styleNONE を使う。
        ok_->text = prop_ ? prop_->requiredMsgs().dlgTextOK.value : "OK";
        ok_->default_button = readOnly_;
        ok_->on_click = [this] { close(DialogResult::OK); };
        buttons_->add(ok_);
        if (!readOnly_) {
            cancel_ = std::make_shared<::cwxeditor::lib::Button>();
            cancel_->text = prop_ ? prop_->requiredMsgs().dlgTextCancel.value : "Cancel";
            cancel_->cancel_button = true;
            cancel_->on_click = [this] { close(DialogResult::Cancel); };
            buttons_->add(cancel_);
        }
        area.add(buttons_);
    }
    // D版と同様、読み取り専用ウィンドウを表示した後で、閉じるボタンへフォーカスを移す。
    dialog_->on_visibility_changed = [this](bool visible) {
        if (visible && readOnly_ && ok_ && services_.gui)
            services_.gui->focus(ok_);
    };
    initialized_ = true;
}

void TextDialog::setup() {
    if (!area_) {
        area_ = std::make_shared<::cwxeditor::lib::Panel>();
        area_->set_layout(std::make_shared<::cwxeditor::lib::LinearLayout>());
        dialog_->add(area_);
    }
    setup(*area_);
}

TextDialog::DialogResult TextDialog::show() {
    if (!initialized_) setup();
    closed_ = false;
    if (services_.gui) {
        dialog_->result = services_.gui->show_modal(dialog_, parent_);
    } else {
        dialog_->set_visible(true);
    }
    if (dialog_->result != DialogResult::None) closed_ = true;
    return dialog_->result;
}

bool TextDialog::open() {
    const auto value = show();
    return value == DialogResult::OK || value == DialogResult::Apply;
}

bool TextDialog::close() {
    close(DialogResult::Cancel);
    return closed_;
}

void TextDialog::setImages(
    std::vector<::cwxeditor::lib::ResourceId> images) {
    images_ = std::move(images);
    if (!images_.empty()) {
        icon_ = images_.front();
        if (dialog_) dialog_->icon = icon_;
    }
}

void TextDialog::close(DialogResult result) {
    dialog_->result = result;
    closed_ = true;
    if (services_.gui) services_.gui->close(dialog_);
    else dialog_->set_visible(false);
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::textdialog
