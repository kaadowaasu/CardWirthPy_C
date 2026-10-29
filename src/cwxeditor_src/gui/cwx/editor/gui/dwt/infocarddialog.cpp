#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dutils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/infocarddialog.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/summaryskinbinding.hpp"
#include "cwxeditor_src/lib/window_host.hpp"
// UIの実装は一つに統一し、
// 別経路の実装を設けない。

#include "cwxeditor_src/core/cwx/path.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/commons.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"
#include "cwxeditor_src/lib/ui_metrics.hpp"
#include "cwxeditor_src/lib/ui_text_layout.hpp"
#include "cwxeditor_src/lib/ui_window_lifecycle.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <stdexcept>

namespace cwxeditor::cwx::editor::gui::dwt::infocarddialog {

namespace {

[[nodiscard]] float nonNegative(const float value) noexcept {
    return value < 0.0F ? 0.0F : value;
}

[[nodiscard]] float insetTop(const lib::Insets& value) noexcept {
    return nonNegative(value.top);
}

[[nodiscard]] float insetBottom(const lib::Insets& value) noexcept {
    return nonNegative(value.bottom);
}

}  // namespace

lib::Owner<InfoCard> InfoCardDialog::nonOwning(InfoCard* card) {
    if (!card) return {};
    return lib::Owner<InfoCard>(card, [](InfoCard*) noexcept {});
}

InfoCardDialog::InfoCardDialog(const CProps& prop,
                               lib::Owner<Summary> summary,
                               lib::Owner<InfoCard> card, const bool read_only,
                               Options options)
    : prop_(&prop), summary_owner_(std::move(summary)),
      summary_(summary_owner_.get()), card_owner_(std::move(card)),
      card_(card_owner_.get()), options_(std::move(options)),
      summary_skin_(std::move(options_.skin)), icon_(options_.icon),
      read_only_(read_only) {
    initialize();
}

InfoCardDialog::InfoCardDialog(const CProps& prop, Summary& summary,
                               InfoCard* card, const bool read_only,
                               Options options)
    : prop_(&prop), summary_(&summary), card_owner_(nonOwning(card)),
      card_(card), options_(std::move(options)),
      summary_skin_(std::move(options_.skin)), icon_(options_.icon),
      read_only_(read_only) {
    initialize();
}

InfoCardDialog::~InfoCardDialog() {
    applying_lifetime_.reset();
    disconnectCommonsObservers();
    on_close_requested = {};
}

lib::Owner<Skin> InfoCardDialog::effectiveSummarySkin() const noexcept {
    if (summary_skin_) return summary_skin_;
    return options_.editor_commons ? options_.editor_commons->skin()
                                   : lib::Owner<Skin>{};
}

void InfoCardDialog::connectCommonsObservers() {
    auto* const common = options_.editor_commons;
    if (!common || ref_skin_connection_) return;
    ref_info_connection_ = common->refInfo.add(
        [this](const lib::Owner<InfoCard>& value) { updateTitle(value.get()); });
    del_info_connection_ = common->delInfo.add(
        [this](const lib::Owner<InfoCard>& value) { delCard(value); });
    ref_scenario_connection_ = common->refScenario.add(
        [this](const lib::Owner<Summary>& value) { refScenario(value.get()); });
    ref_skin_connection_ = common->refSkin.add([this] { refSkin(); });
    ref_image_scale_connection_ = common->refImageScale.add([this] { refImageScale(); });
    ref_data_version_connection_ = common->refDataVersion.add([this] { refDataVersion(); });
    ref_target_version_connection_ = common->refTargetVersion.add([this] { refDataVersion(); });
}

void InfoCardDialog::disconnectCommonsObservers() noexcept {
    auto* const common = options_.editor_commons;
    if (!common) return;
    common->refInfo.remove(ref_info_connection_);
    common->delInfo.remove(del_info_connection_);
    common->refScenario.remove(ref_scenario_connection_);
    common->refSkin.remove(ref_skin_connection_);
    common->refImageScale.remove(ref_image_scale_connection_);
    common->refDataVersion.remove(ref_data_version_connection_);
    common->refTargetVersion.remove(ref_target_version_connection_);
    ref_info_connection_ = del_info_connection_ = ref_scenario_connection_ = 0U;
    ref_skin_connection_ = ref_image_scale_connection_ = 0U;
    ref_data_version_connection_ = ref_target_version_connection_ = 0U;
}

void InfoCardDialog::initialize() {
    set_visible(false);
    if (!prop_) throw std::invalid_argument("InfoCardDialog requires props");
    if (!summary_)
        throw std::invalid_argument("InfoCardDialog requires a Summary");

    creation_mode_ = options_.creationMode || !card_;
    model_.read_only = read_only_;
    model_.name = card_ ? card_->name() : lib::String{};
    model_.description = card_ ? card_->desc() : lib::String{};
    model_.image.default_position = CardImagePosition::TopLeft;

    const auto card_size = prop_->looks().cardSize();
    const auto card_insets = prop_->looks().cardInsets();
    model_.image.preview_size = {
        scale(static_cast<float>(card_size.width)),
        scale(static_cast<float>(card_size.height)),
    };
    model_.image.preview_insets = {
        scale(static_cast<float>(card_insets.n)),
        scale(static_cast<float>(card_insets.e)),
        scale(static_cast<float>(card_insets.s)),
        scale(static_cast<float>(card_insets.w)),
    };

    // D版の findSkin と同様、読み取り専用やシナリオパスの
    // ないダイアログで検索する。 ネイティブのホストは解決処理を通
    // じて検索を提供でき、通常の代替値には設定済みのスキンを使う。
    summary_skin_pinned_ = summaryskinbinding::pinsSummarySkin(
        read_only_, summary_);
    if (summary_skin_pinned_ && options_.skin_resolver) {
        if (auto skin = options_.skin_resolver(*prop_, summary_))
            summary_skin_ = std::move(skin);
    }

    const auto& messages = prop_->requiredMsgs();
    title = creation_mode_ ? messages.dlgTitNewInfo.value
                           : ::cwxeditor::cwx::utils::tryFormat(messages.dlgTitInfo.value,
                                                                card_->name());
    if (icon_ == 0U && options_.editor_props)
        icon_ = options_.editor_props->images().info();
    static_cast<lib::Window&>(*this).icon = icon_;
    // D版ではmodal=falseのAbsDialog。C++側も所有者付きのモードレスダイアログとして設定する。
    lib::configure_owned_window(*this, options_.owner,
        lib::OwnedWindowKind::Modeless);
    resizable = true;
    // ALL では内容の推奨幅ではなく、設計上のコ
    // ンパクトな幅を維持する（横幅を半分にする）。
    initial_preferred_width = !lib::editor_window_host_options().all_mode;
    minimizable = false;
    maximizable = false;
    if (options_.editor_props && options_.window_size.width <= 0.0F && options_.window_size.height <= 0.0F) {
        const auto& size = options_.editor_props->flex().infoCardDlg;
        options_.window_size = {static_cast<float>(size.width()), static_cast<float>(size.height())};
        // ALL では、従来の幅広い配置で保存された古い幅を無視する。
        if (lib::editor_window_host_options().all_mode && options_.window_size.width > 240.0F)
            options_.window_size.width = 0.0F;
    }
    on_close_requested = [this] { forceCancel(); };

}

float InfoCardDialog::scale(const float value) const {
    if (!options_.scale) return value;
    const auto scaled = options_.scale(value);
    return std::isfinite(scaled) && scaled >= 0.0F ? scaled : value;
}

bool InfoCardDialog::legacyForLayout() const noexcept {
    return summary_ && summary_->legacy();
}

lib::FontSpec InfoCardDialog::makeFont(const props::CFont& value) {
    lib::FontSpec result;
    result.family = value.name;
    result.points = value.point == 0U ? 10.0F : static_cast<float>(value.point);
    result.weight = value.bold ? 700U : 400U;
    result.italic = value.italic;
    return result;
}

lib::String InfoCardDialog::normalizeReturnCode(const lib::StringView value) {
    return dutils::wrapReturnCode(lib::String(value));
}

void InfoCardDialog::setup() {
    setup(static_cast<Container&>(*this));
}

void InfoCardDialog::setup(Container& area) {
    if (!area_) {
        area_ = std::make_shared<lib::Panel>();
        auto root_layout = std::make_shared<lib::GridLayout>();
        root_layout->columns = 1U;
        root_layout->spacing = 6.0F;
        area_->set_layout(std::move(root_layout));

        name_group_ = std::make_shared<lib::GroupBox>();
        name_group_->title = prop_->requiredMsgs().name.value;
        name_group_->set_layout(std::make_shared<lib::FillLayout>());
        name_group_->set_layout_item({.row = 0U, .column = 0U,
                                      .horizontal = lib::Align::Stretch,
                                      .vertical = lib::Align::Center,
                                      .minimum = {0.0F, 46.0F},
                                      .preferred = {0.0F, 46.0F}});
        name_edit_ = std::make_shared<lib::TextEdit>();
        name_edit_->read_only = read_only_;
        // D版の nameLimit は表示幅の警告しきい値であり、入力文字数
        // の上限ではない。 ネイティブの max_length はUTF-16
        // 入力を切り詰め、検証で通知する前にデータを失わせるため使わない。
        name_edit_->max_length = 0U;
        const auto monospace = prop_->looks().monospace();
        if (!monospace.empty()) {
            lib::FontSpec name_font;
            name_font.family = monospace;
            // ALL のカード名は、従来の10pt等幅文字の1.25倍にする。
            name_font.points = lib::editor_window_host_options().all_mode ? 12.5F : 10.0F;
            name_edit_->set_font(std::move(name_font));
        }
        name_edit_->on_changed = [this](const std::string_view value) {
            if (updating_) return;
            model_.name.assign(value.data(), value.size());
            markModified();
            refreshWarning();
        };
        name_group_->add(name_edit_);
        // ImageSelect が独自のネイティブの子ツリーを追加する前に、暗黙の0
        // 行目のコントロールを追加する。 順序が逆だと、サイズ変更後に GridLayout
        // が両方を1行目に割り当て、名前欄が画像ペインの裏に隠れる場合がある。
        area_->add(name_group_);

        if (options_.editor_props) {
            auto& editor_props = *options_.editor_props;
            const auto card_size = editor_props.looks().cardSize();
            image_select_ = std::make_unique<imageselect::ImageSelectBase>(
                imageselect::ImageSelectKind::Card, false, area_,
                read_only_ ? 1 : 0, editor_props,
                const_cast<Summary*>(summary_),
                editor_props.s(static_cast<std::int32_t>(card_size.width)),
                editor_props.s(static_cast<std::int32_t>(card_size.height)),
                editor_props.s(editor_props.looks().cardInsets()),
                CardImagePosition::TopLeft, true,
                [this] { return exportedImageName(); }, lib::Action{},
                imageselect::ImageSelectBase::Definitions{}, false,
                effectiveSummarySkin(), options_.services);
            image_select_->bindEditorCommons(options_.editor_commons);
            image_view_ = image_select_->previewControl();
            const auto preview_height = static_cast<float>(
                editor_props.s(static_cast<std::int32_t>(card_size.height)) +
                editor_props.s(editor_props.looks().cardInsets()).n +
                editor_props.s(editor_props.looks().cardInsets()).s);
            const auto image_section_height = std::max(
                preview_height + 16.0F,
                static_cast<float>(editor_props.var().filesHeight.value) + 88.0F);
            image_select_->widget()->set_layout_item({
                .row = 1U, .column = 0U,
                .horizontal = lib::Align::Stretch,
                // D版と同様、ImageSelect に FILL_BOTH を
                // 指定する。 従来の188px固定行では、74x94のカードキャ
                // ンバスと上下の余白が圧縮され、 画像の位置ずれや欠けが生じる。
                .vertical = lib::Align::Stretch,
                .minimum = {0.0F, image_section_height},
                .preferred = {0.0F, image_section_height},
                .grow = 1.0F});
            image_select_->modEvent.push_back([this] {
                if (updating_ || !image_select_) return;
                model_.image.images = image_select_->cardImages();
                model_.image.selected_index = model_.image.images.empty()
                                                  ? no_image : 0U;
                model_.image.dirty = true;
                markModified();
                rebuildImagePreviews();
                refreshWarning();
            });
            updating_ = true;
            image_select_->setCardImages(model_.image.images, false);
            updating_ = false;
        } else {
            image_panel_ = std::make_shared<lib::Panel>();
            auto image_layout = std::make_shared<lib::GridLayout>();
            image_layout->columns = 2U;
            image_layout->spacing = 6.0F;
            image_panel_->set_layout(image_layout);

            image_list_ = std::make_shared<lib::ListView>();
            image_list_->set_tooltip("Card image layers");
            image_list_->on_selection_changed = [this](const std::vector<std::size_t>& values) {
                if (updating_) return;
                if (values.empty()) clearImageSelection();
                else (void)selectImage(values.front());
                refreshImageControls();
            };
            image_panel_->add(image_list_);

            image_view_ = std::make_shared<lib::ImageView>();
            image_view_->scale_mode = lib::ImageScaleMode::Fit;
            image_view_->interpolation = lib::Interpolation::Best;
            image_panel_->add(image_view_);

            image_buttons_ = std::make_shared<lib::Panel>();
            auto image_button_layout = std::make_shared<lib::LinearLayout>(
                lib::Orientation::Horizontal);
            image_button_layout->spacing = 4.0F;
            image_buttons_->set_layout(image_button_layout);
            image_up_button_ = std::make_shared<lib::Button>(
                prop_->requiredMsgs().menuText(::cwxeditor::cwx::types::MenuID::Up));
            image_down_button_ = std::make_shared<lib::Button>(
                prop_->requiredMsgs().menuText(::cwxeditor::cwx::types::MenuID::Down));
            image_remove_button_ = std::make_shared<lib::Button>(
                prop_->requiredMsgs().menuText(::cwxeditor::cwx::types::MenuID::Delete));
            image_up_button_->on_click = [this] {
                const auto index = model_.image.selected_index;
                if (read_only_ || index == no_image || index == 0 ||
                    index >= model_.image.images.size()) return;
                std::swap(model_.image.images[index], model_.image.images[index - 1]);
                model_.image.selected_index = index - 1;
                model_.image.dirty = true;
                markModified();
                rebuildImagePreviews();
                updateImageView();
            };
            image_down_button_->on_click = [this] {
                const auto index = model_.image.selected_index;
                if (read_only_ || index == no_image ||
                    index + 1 >= model_.image.images.size()) return;
                std::swap(model_.image.images[index], model_.image.images[index + 1]);
                model_.image.selected_index = index + 1;
                model_.image.dirty = true;
                markModified();
                rebuildImagePreviews();
                updateImageView();
            };
            image_remove_button_->on_click = [this] {
                const auto index = model_.image.selected_index;
                if (read_only_ || index == no_image || index >= model_.image.images.size()) return;
                model_.image.images.erase(model_.image.images.begin() +
                                          static_cast<std::ptrdiff_t>(index));
                model_.image.selected_index = model_.image.images.empty()
                                                  ? no_image
                                                  : std::min(index, model_.image.images.size() - 1);
                model_.image.dirty = true;
                markModified();
                rebuildImagePreviews();
                updateImageView();
            };
            image_buttons_->add(image_up_button_);
            image_buttons_->add(image_down_button_);
            image_buttons_->add(image_remove_button_);
            image_panel_->add(image_buttons_);
        }

        description_group_ = std::make_shared<lib::GroupBox>();
        description_group_->title = prop_->requiredMsgs().desc.value;
        description_group_->set_layout(std::make_shared<lib::FillLayout>());
        const auto description_font = prop_->looks().cardDescFont(legacyForLayout());
        const auto description_line_height = std::max(
            static_cast<float>(description_font.point),
            std::ceil(static_cast<float>(description_font.point) * 1.25F));
        const auto description_height =
            description_line_height * static_cast<float>(
                std::max(1, prop_->looks().cardDescLine(legacyForLayout()))) + 24.0F;
        description_group_->set_layout_item({.row = 2U, .column = 0U,
                                              .horizontal = lib::Align::Stretch,
                                              // D版と同様、説明グループは文字を基
                                              // 準にした固定の高さとする。 余っ
                                              // た縦の領域は、その上の ImageSelect
                                              // 行に割り当てる。
                                              .vertical = lib::Align::Center,
                                              .minimum = {lib::editor_window_host_options().all_mode ? 160.0F : 320.0F,
                                                          description_height},
                                              .preferred = {lib::editor_window_host_options().all_mode ? 160.0F : 320.0F,
                                                            description_height}});
        description_edit_ = std::make_shared<lib::TextEdit>();
        description_edit_->multiline = true;
        description_edit_->wrap = true;
        description_edit_->vertical_scroll = false; // D の FixedWidthText と同様、縦スクロールなし。
        description_edit_->read_only = read_only_;
        // cardDescLen は見た目の行幅を制御する値であ
        // り、最大文字数ではない。 カードの説明を切り詰めない。
        description_edit_->max_length = 0U;
        description_edit_->on_changed = [this](const std::string_view value) {
            if (updating_) return;
            model_.description.assign(value.data(), value.size());
            markModified();
            refreshWarning();
        };
        description_group_->add(description_edit_);

        if (image_panel_) area_->add(image_panel_);
        area_->add(description_group_);
        setDescriptionFont();
    }

    const auto& children = area.children();
    if (&area != area_.get() &&
        std::find(children.begin(), children.end(), area_) == children.end()) {
        area.add(area_);
    }
    if (&area == static_cast<Container*>(this) && !buttons_panel_) {
        auto root_layout = std::make_shared<lib::GridLayout>();
        root_layout->columns = 1U;
        root_layout->spacing = 0.0F;
        area.set_layout(root_layout);
        area_->set_layout_item({0U, 0U, 1U, 1U, lib::Align::Stretch,
                                lib::Align::Stretch, {}, {}, {}, 1.0F});

        auto separator = std::make_shared<lib::Separator>();
        separator->set_layout_item({.row = 1U, .column = 0U,
                                    .preferred = {0.0F, 1.0F}});
        area.add(separator);

        buttons_panel_ = std::make_shared<lib::Panel>();
        auto footer_layout = std::make_shared<lib::GridLayout>();
        footer_layout->columns = 2U;
        footer_layout->spacing = 0.0F;
        footer_layout->padding = {6.0F, 5.0F, 6.0F, 5.0F};
        buttons_panel_->set_layout(footer_layout);
        buttons_panel_->set_layout_item({2U, 0U, 1U, 1U, lib::Align::Stretch,
                                         lib::Align::Center, {}, {}, {}, 0.0F});

        // D版の AbsDialog と同様、操作ボタン群を右側に配置
        // する。 行全体の上だけに隙間を置くのではなく、フッター内で上
        // 下に同じ余白を確保し、 ボタンが下端に接しないようにする。
        auto leading = std::make_shared<lib::Spacer>();
        leading->set_layout_item({.row = 0U, .column = 0U, .grow = 1.0F});
        buttons_panel_->add(leading);
        auto button_group = std::make_shared<lib::Panel>();
        auto button_layout = std::make_shared<lib::GridLayout>();
        button_layout->columns = read_only_ ? 1U : 3U;
        button_layout->equal_width = true;
        button_layout->spacing = 6.0F;
        button_group->set_layout(button_layout);
        button_group->set_layout_item({.row = 0U, .column = 1U,
                                       .vertical = lib::Align::Center});
        buttons_panel_->add(button_group);

        ok_button_ = std::make_shared<lib::Button>(
            read_only_ ? prop_->requiredMsgs().dlgTextClose.value
                       : prop_->requiredMsgs().dlgTextOK.value);
        ok_button_->set_layout_item({.row = 0U, .column = 0U,
                                     .horizontal = lib::Align::Stretch,
                                     .vertical = lib::Align::Center,
                                     .minimum = {96.0F, 0.0F}});
        ok_button_->default_button = true;
        ok_button_->on_click = [this] {
            if (read_only_) close(DialogResult::OK);
            else (void)accept();
        };
        button_group->add(ok_button_);
        if (!read_only_) {
            cancel_button_ = std::make_shared<lib::Button>(
                prop_->requiredMsgs().dlgTextCancel.value);
            cancel_button_->set_layout_item({.row = 0U, .column = 1U,
                                             .horizontal = lib::Align::Stretch,
                                             .vertical = lib::Align::Center,
                                             .minimum = {96.0F, 0.0F}});
            cancel_button_->cancel_button = true;
            cancel_button_->on_click = [this] { (void)cancel(); };
            button_group->add(cancel_button_);

            apply_button_ = std::make_shared<lib::Button>(
                prop_->requiredMsgs().dlgTextApply.value);
            apply_button_->set_layout_item({.row = 0U, .column = 2U,
                                            .horizontal = lib::Align::Stretch,
                                            .vertical = lib::Align::Center,
                                            .minimum = {96.0F, 0.0F}});
            apply_button_->set_enabled(false);
            apply_button_->on_click = [this] { (void)apply(); };
            button_group->add(apply_button_);
        }
        area.add(buttons_panel_);
    }
    if (this->on_close_requested == nullptr)
        this->on_close_requested = [this] { forceCancel(); };
    initialized_ = true;
    syncWidgets();
    dmenu::Commons textCommons;
    textCommons.services = options_.services;
    textCommons.undoMaxEtc = options_.editor_props
        ? static_cast<std::size_t>(std::max<std::int32_t>(
              0, options_.editor_props->var().undoMaxEtc.value))
        : 100U;
    if (auto* presentation = options_.editor_props) {
        textCommons.presentation.textTabs = static_cast<std::size_t>(
            std::max<std::int32_t>(1, presentation->var().textTabs.value));
        textCommons.presentation.menuText = [presentation](auto id) { return presentation->buildMenu(id); };
        textCommons.presentation.menuImage = [presentation](auto id) { return presentation->images().menu(id); };
    }
    text_menus_.clear();
    for (const auto& text : {name_edit_, description_edit_})
        text_menus_.push_back(dmenu::createTextMenu(textCommons, dmenu::TMM(text)));
    rebuildImagePreviews();
    updateImageView();
    layoutModel(bounds());
}

void InfoCardDialog::syncWidgets() {
    updating_ = true;
    if (name_edit_) name_edit_->text = model_.name;
    if (description_edit_) description_edit_->text = model_.description;
    updating_ = false;
}

void InfoCardDialog::setDescriptionFont() {
    if (!description_edit_ || !prop_ || !summary_) return;
    const auto skin = effectiveSummarySkin();
    const bool legacy = skin ? skin->legacy() : summary_->legacy();
    auto authored_font = prop_->looks().cardDescFont(legacy);
    if (options_.editor_props)
        authored_font = options_.editor_props->adjustFont(std::move(authored_font));
    auto font = makeFont(authored_font);
    // 他のカード説明と同様、設計上のフォント調整を一度だけ行う。 ALL の追
    // 加のUIフォント拡大で FixedWidthText の寸法を変えない。
    font.content_font = true;
    description_edit_->set_font(std::move(font));
    description_edit_->minimum_visible_columns = static_cast<std::size_t>(std::max(1, prop_->looks().cardDescLen()));
    description_edit_->minimum_visible_lines = static_cast<std::size_t>(std::max(1, prop_->looks().cardDescLine(summary_->legacy())));
    auto layout = description_group_->layout_item();
    layout.minimum = {};
    layout.preferred = {};
    description_group_->set_layout_item(layout);
}

void InfoCardDialog::markModified() {
    if (updating_) return;
    model_.modified = true;
    modified = true;
    if (apply_button_) apply_button_->set_enabled(true);
}

void InfoCardDialog::setName(lib::String value) {
    if (model_.name == value) return;
    model_.name = std::move(value);
    syncWidgets();
    markModified();
    refreshWarning();
}

void InfoCardDialog::setDescription(lib::String value) {
    if (model_.description == value) return;
    model_.description = std::move(value);
    syncWidgets();
    markModified();
    refreshWarning();
}

void InfoCardDialog::setImagesImpl(lib::Array<lib::Owner<CardImage>> value,
                                   const bool mark_modified) {
    model_.image.images = std::move(value);
    if (model_.image.images.empty()) {
        model_.image.selected_index = no_image;
    } else if (model_.image.selected_index >= model_.image.images.size()) {
        model_.image.selected_index = 0U;
    }
    model_.image.dirty = mark_modified;
    if (image_select_) {
        const bool was_updating = updating_;
        updating_ = true;
        image_select_->setCardImages(model_.image.images, false);
        updating_ = was_updating;
    }
    rebuildImagePreviews();
    updateImageView();
    if (mark_modified) markModified();
}

void InfoCardDialog::setImageSelection(ImageSelection value) {
    if (value.preview_size.width <= 0.0F || value.preview_size.height <= 0.0F) {
        const auto card_size = prop_->looks().cardSize();
        value.preview_size = {
            scale(static_cast<float>(card_size.width)),
            scale(static_cast<float>(card_size.height)),
        };
    }
    model_.image = std::move(value);
    if (model_.image.images.empty()) {
        model_.image.selected_index = no_image;
    } else if (model_.image.selected_index >= model_.image.images.size()) {
        model_.image.selected_index = 0U;
    }
    model_.image.dirty = true;
    if (image_select_) {
        const bool was_updating = updating_;
        updating_ = true;
        image_select_->setCardImages(model_.image.images, false);
        updating_ = was_updating;
    }
    rebuildImagePreviews();
    updateImageView();
    markModified();
    refreshWarning();
}

void InfoCardDialog::setImages(lib::Array<lib::Owner<CardImage>> value) {
    setImagesImpl(std::move(value), true);
    refreshWarning();
}

void InfoCardDialog::rebuildImagePreviews() {
    image_previews_.clear();
    image_previews_.reserve(model_.image.images.size());
    for (const auto& image : model_.image.images) {
        ImagePreview preview;
        if (image) {
            preview.path = image->path();
            preview.type = image->type;
            preview.position = image->positionType();
            if (options_.image_resource && !preview.path.empty())
                preview.resource = options_.image_resource(preview.path);
        }
        preview.bounds = {
            0.0F,
            0.0F,
            model_.image.preview_size.width,
            model_.image.preview_size.height,
        };
        image_previews_.push_back(std::move(preview));
    }
    refreshImageControls();
}

void InfoCardDialog::refreshImageControls() {
    if (image_list_) {
        updating_ = true;
        std::vector<lib::ListItem> items;
        items.reserve(image_previews_.size());
        for (std::size_t index = 0; index < image_previews_.size(); ++index) {
            const auto& preview = image_previews_[index];
            auto label = preview.path.empty() ? lib::String("(embedded image)")
                                              : preview.path;
            items.push_back({std::move(label), std::to_string(index),
                             preview.resource, false, true});
        }
        std::vector<std::size_t> selection;
        if (model_.image.selected_index < image_previews_.size())
            selection.push_back(model_.image.selected_index);
        (void)lib::replace_list_items(image_list_, std::move(items),
                                      std::move(selection));
        updating_ = false;
    }
    const auto index = model_.image.selected_index;
    const bool selected = index < model_.image.images.size();
    if (image_up_button_) image_up_button_->set_enabled(!read_only_ && selected && index > 0);
    if (image_down_button_)
        image_down_button_->set_enabled(!read_only_ && selected &&
                                        index + 1 < model_.image.images.size());
    if (image_remove_button_) image_remove_button_->set_enabled(!read_only_ && selected);
}

void InfoCardDialog::updateImageView() {
    if (image_select_) return;
    if (!image_view_) return;
    image_view_->image = 0U;
    if (model_.image.selected_index < image_previews_.size())
        image_view_->image =
            image_previews_[model_.image.selected_index].resource;
    refreshImageControls();
}

const ImagePreview* InfoCardDialog::selectedImage() const noexcept {
    if (model_.image.selected_index >= image_previews_.size()) return nullptr;
    return &image_previews_[model_.image.selected_index];
}

bool InfoCardDialog::selectImage(const std::size_t index) {
    if (index >= model_.image.images.size()) return false;
    if (model_.image.selected_index == index) return true;
    model_.image.selected_index = index;
    model_.image.dirty = true;
    updateImageView();
    markModified();
    return true;
}

void InfoCardDialog::clearImageSelection() {
    if (model_.image.selected_index == no_image) return;
    model_.image.selected_index = no_image;
    model_.image.dirty = true;
    updateImageView();
    markModified();
}

lib::String InfoCardDialog::exportedImageName() const {
    if (options_.exported_image_name) {
        return options_.exported_image_name(
            *prop_, summary_->scenarioName(), summary_->author(), model_.name);
    }
    return ::cwxeditor::cwx::summary::toExportedImageNameWithCardName(
        *prop_, summary_->scenarioName(), summary_->author(), model_.name);
}

void InfoCardDialog::refreshWarning() {
    WarningList warning_result;
    if (prop_ && summary_) {
        const auto name_warnings = ::cwxeditor::cwx::warning::sjisWarnings(
            *prop_, summary_, model_.name, prop_->requiredMsgs().name.value);
        warning_result.insert(warning_result.end(), name_warnings.begin(),
                              name_warnings.end());

        if (options_.image_warnings) {
            image_warnings_ = options_.image_warnings(*prop_, summary_, model_.image);
        } else if (image_select_) {
            image_warnings_ = image_select_->warnings();
        }
        warning_result.insert(warning_result.end(), image_warnings_.begin(),
                              image_warnings_.end());

        const auto description_warnings =
            ::cwxeditor::cwx::warning::sjisWarnings(
                *prop_, summary_, model_.description, prop_->requiredMsgs().desc.value);
        warning_result.insert(warning_result.end(), description_warnings.begin(),
                              description_warnings.end());
    }
    model_.warnings = std::move(warning_result);
    if (ok_button_) {
        lib::String tooltip;
        for (const auto& warning : model_.warnings) {
            if (!tooltip.empty()) tooltip += '\n';
            tooltip += warning;
        }
        ok_button_->set_tooltip(std::move(tooltip));
        ok_button_->icon = !model_.warnings.empty() && options_.editor_props
            ? options_.editor_props->images().load(lib::StringView{"warning.png"}) : 0;
        if (options_.services.gui) options_.services.gui->request_redraw(ok_button_);
    }
    if (on_warning_changed) on_warning_changed(model_.warnings);
}

void InfoCardDialog::delCard(const InfoCard* card) {
    if (card_ && card_ == card) forceCancel();
}

void InfoCardDialog::refScenario(const Summary* summary) {
    // MainWindow は、カードの適用後にも同じ Summary の更新とし
    // て refScenario を通知する。 このダイアログが借用するカードと素
    // 材選択の所有関係を無効にするのは、 シナリオが置き換わったか閉じた場合だけ。
    if (summary != summary_) forceCancel();
}

void InfoCardDialog::refSkin() {
    // D版と同様、読み取り専用やパスのない所有者では、構築時に解決したスキンを維持する。
    if (!summary_skin_pinned_ && options_.skin_resolver && summary_) {
        if (auto skin = options_.skin_resolver(*prop_, summary_))
            summary_skin_ = std::move(skin);
    }
    setDescriptionFont();
}

void InfoCardDialog::refImageScale() {
    if (!prop_) return;
    const auto card_size = prop_->looks().cardSize();
    const auto card_insets = prop_->looks().cardInsets();
    model_.image.preview_size = {
        scale(static_cast<float>(card_size.width)),
        scale(static_cast<float>(card_size.height)),
    };
    model_.image.preview_insets = {
        scale(static_cast<float>(card_insets.n)),
        scale(static_cast<float>(card_insets.e)),
        scale(static_cast<float>(card_insets.s)),
        scale(static_cast<float>(card_insets.w)),
    };
    if (image_select_ && options_.editor_props) {
        auto& editor_props = *options_.editor_props;
        image_select_->setPreviewSize(
            editor_props.s(static_cast<std::int32_t>(card_size.width)),
            editor_props.s(static_cast<std::int32_t>(card_size.height)),
            editor_props.s(card_insets));
    }
    rebuildImagePreviews();
    updateImageView();
    if (initialized_) layoutModel(bounds());
}

void InfoCardDialog::refDataVersion() {
    refreshWarning();
    setDescriptionFont();
    if (initialized_) layoutModel(bounds());
}

void InfoCardDialog::updateTitle(const InfoCard* card) {
    if (!card_ || card_ != card) return;
    const auto& messages = prop_->requiredMsgs();
    const auto id_name = ::cwxeditor::cwx::utils::tryFormat(
        messages.nameWithID.value, card_->id(), card_->name());
    title = ::cwxeditor::cwx::utils::tryFormat(messages.dlgTitInfo.value,
                                               id_name);
    if (on_title_changed) on_title_changed(*card_);
}

void InfoCardDialog::refCard(const InfoCard* card) {
    // D版と同じ条件で保護する。一つのカードのダイアログは、別のカードの通知を
    // 無視する。 新規カードのダイアログには、更新するカードがまだ存在しない。
    if (card_ && card_ != card) return;
    if (creation_mode_ && creation_initialized_) return;
    creation_initialized_ = true;

    updating_ = true;
    if (card_ && !creation_mode_) {
        model_.name = card_->name();
        model_.description = card_->desc();
        model_.image.cancel = false;
        model_.image.dirty = false;
        setImagesImpl(card_->paths(), false);
        updateTitle(card_);
    } else {
        model_.name.clear();
        model_.description.clear();
        model_.image.cancel = false;
        model_.image.dirty = false;
        setImagesImpl({}, false);
    }
    model_.modified = false;
    modified = false;
    if (apply_button_) apply_button_->set_enabled(creation_mode_ && !read_only_);
    syncWidgets();
    updating_ = false;
}

bool InfoCardDialog::apply() {
    if (read_only_ || !summary_) return false;

    if (name_edit_) model_.name = name_edit_->text;
    if (description_edit_) model_.description = description_edit_->text;

    MaterialPathResult material;
    material.images = model_.image.images;
    if (image_select_) {
        const auto selected = image_select_->cardMaterialPath(force_applying_);
        material.images = selected.images;
        material.cancel = selected.cancel;
        material.warnings = image_select_->warnings();
    } else if (options_.material_path) {
        material = options_.material_path(force_applying_, model_.image);
    } else if (model_.image.cancel) {
        return false;
    }
    if (material.cancel) return false;

    image_warnings_ = std::move(material.warnings);
    setImagesImpl(std::move(material.images), false);

    const auto description = normalizeReturnCode(model_.description);
    model_.description = description;
    const std::weak_ptr<int> lifetime = applying_lifetime_;
    const auto applying = on_applying;
    if (applying) applying();
    if (lifetime.expired() || closed_) return false;
    if (card_) {
        card_->name(model_.name);
        card_->paths(model_.image.images);
        card_->desc(description);
    } else {
        card_owner_ = std::make_shared<InfoCard>(
            summary_->newId<InfoCard>(), model_.name, model_.image.images,
            description);
        card_ = card_owner_.get();
    }

    creation_mode_ = false;
    model_.image.cancel = false;
    model_.image.dirty = false;
    model_.modified = false;
    modified = false;
    if (apply_button_) apply_button_->set_enabled(false);
    if (card_) updateTitle(card_);
    if (on_applied && card_) on_applied(*card_);
    return true;
}

bool InfoCardDialog::accept() {
    if (!apply()) return false;
    close(DialogResult::OK);
    return true;
}

bool InfoCardDialog::open() {
    // D版と同様、モードレスウィンドウを表示する前に、open/setup でコントロール
    // を構築し、 カード内容を反映して境界を計算する。アクティブ化時は編集内容を保持する。
    if (!area_) {
        setup();
        refCard(card_);
        refDataVersion();
        if (options_.window_size.width > 0.0F ||
            options_.window_size.height > 0.0F) {
            auto current = bounds();
            if (options_.window_size.width > 0.0F)
                current.width = options_.window_size.width;
            if (options_.window_size.height > 0.0F)
                current.height = options_.window_size.height;
            set_bounds(current);
        }
        auto current = bounds();
        const auto preferred = preferredSize();
        if (current.width <= 0.0F) current.width = preferred.width;
        if (current.height <= 0.0F) current.height = preferred.height;
        set_bounds(current);
        if (!lib::editor_window_host_options().all_mode)
            minimum_size = lib::Size{current.width, current.height};
    }
    connectCommonsObservers();
    result = DialogResult::None;
    closed_ = false;
    const std::weak_ptr<int> lifetime = applying_lifetime_;
    set_visible(true);
    if (lifetime.expired() || closed_) return false;
    if (image_select_) {
        image_select_->bindEditorCommons(options_.editor_commons);
        if (lifetime.expired() || closed_) return false;
        image_select_->active(true);
    }
    return !lifetime.expired() && !closed_;
}

bool InfoCardDialog::cancel() {
    forceCancel();
    return true;
}

void InfoCardDialog::close(const DialogResult value) {
    if (closed_) return;
    if (options_.editor_props) {
        // 未スケールの寸法を保存(再オープン時の二重スケール防止)。
        const auto size = lib::authored_window_bounds(*this);
        auto& saved = options_.editor_props->flex().infoCardDlg;
        saved.width(static_cast<std::int32_t>(std::lround(size.width)));
        saved.height(static_cast<std::int32_t>(std::lround(size.height)));
    }
    const bool notify_cancel = value == DialogResult::Cancel && !closed_;
    result = value;
    closed_ = true;
    on_applying = {};
    // D版ではSWTウィンドウの破棄で Commons の全リスナーが外れる。 共通
    // のダイアログオブジェクトは、非表示になってもホストが保持し続ける場合がある。
    disconnectCommonsObservers();
    // D版ならウィンドウが破棄される時点を過ぎても、ホストが共通ダイアログを保持する場
    // 合がある。 デストラクターだけでなく Close 時にギャラリーと購読を無効化す
    // る。 バックエンドがこのオブジェクトを無効化する前に、コールバックを複製する。
    const std::weak_ptr<int> lifetime = applying_lifetime_;
    const auto cancelled = on_cancelled;
    const auto closed = on_closed;
    std::exception_ptr failure;
    const auto step = [&failure](auto&& action) {
        try { action(); } catch (...) {
            if (!failure) failure = std::current_exception();
        }
    };
    const auto still_closing = [&] {
        return !lifetime.expired() && closed_;
    };
    step([&] { set_visible(false); });
    if (still_closing() && image_select_)
        step([&] { image_select_->bindEditorCommons(nullptr); });
    if (still_closing() && image_select_)
        step([&] { image_select_->active(false); });
    if (still_closing() && notify_cancel && cancelled) step(cancelled);
    if (still_closing() && closed) step(closed);
    if (failure) std::rethrow_exception(failure);
}

void InfoCardDialog::forceCancel() {
    close(DialogResult::Cancel);
}

lib::Size InfoCardDialog::preferredSize() const {
    if (!prop_ || !summary_) return {};

    // preferredSize() はクライアント領域の論理サイズ
    // を返す。 ダイアログの実体化時に、どのOSでも editor_ui_text_scale()
    // を一度適用する。 ここでも拡大すると、情報
    // カードだけが倍率の二乗で大きくなってしまう。
    const auto ui = [](const float value) noexcept { return value; };
    const auto card_size = prop_->looks().cardSize();
    const auto card_insets = prop_->looks().cardInsets();
    const auto preview_width = scale(static_cast<float>(card_size.width));
    const auto preview_height = scale(static_cast<float>(card_size.height));
    const auto inset_width =
        scale(static_cast<float>(card_insets.e + card_insets.w));
    const auto inset_height =
        scale(static_cast<float>(card_insets.n + card_insets.s));
    const auto skin = effectiveSummarySkin();
    const auto font = prop_->looks().cardDescFont(
        skin ? skin->legacy() : summary_->legacy());
    const auto logical_font_height = scale(static_cast<float>(font.point));
    // text_edit_line_height() は全体のUI倍率を含
    // む表示画素単位の寸法。 指定するウィンドウサイズは論理単位に保つ。layoutModel()
    // は引き続き倍率適用済みの表示座標を使う。
    const auto line_height = std::max(logical_font_height,
        std::ceil(logical_font_height * 1.25F));
    const auto line_count = static_cast<float>(
        prop_->looks().cardDescLine(legacyForLayout()));
    auto width = std::max(ui(240.0F), preview_width + inset_width + ui(24.0F));
    // name_group_ は46論理画素で設計している。minimum_ui_size()
    // は表示画素を返すため、 論理的な外側サイズの計算で参照すると、preferredSize
    // が現在の cw.s の値に依存してしまう。
    const auto name_height = ui(46.0F);
    auto image_height = preview_height + inset_height + ui(16.0F);
    const bool all_mode = lib::editor_window_host_options().all_mode;
    if (options_.editor_props) {
        // ALL ではファイル一覧の幅を ImageSelect の最小サイ
        // ズで確保し、 追加の72pxの余白は設けない（横幅を半分にする）。
        width = std::max(width, preview_width + inset_width +
                                    ui(static_cast<float>(options_.editor_props->var().filesWidth.value)) +
                                    ui(all_mode ? 24.0F : 72.0F));
        image_height = std::max(image_height,
                                ui(static_cast<float>(
                                    options_.editor_props->var().filesHeight.value)) +
                                    ui(88.0F));
    }
    // ここは設計上の論理サイズ。説明欄の配置上の最小サイ
    // ズは表示寸法であり、 ネイティブのホストが倍率を適
    // 用するため、ここで使うとUI倍率が二重にかかる。
    const auto description_height =
        line_height * std::max(1.0F, line_count) + ui(24.0F);
    // D版の96DPIでの実寸（ネイティブ
    // の枠を含め約324×509）に合わせ
    // 、 縦長で固定幅の説明欄を維持する。
    // ウィンドウの基準サイズは論理単位で指
    // 定し、 実体化時にダイアログ全体へ倍
    // 率を一度だけ適用する。 ALL の
    // 情報カード設定は幅の基準値を456
    // から228へ縮める。 名前・画像選
    // 択・説明・フッターに実際に必要な幅
    // は、実体化時の共通計測で確保する。
    width = std::max(width, ui(all_mode ? 228.0F : 456.0F));
    const auto height = std::max(name_height + image_height +
                                      description_height + ui(24.0F),
                                  ui(497.0F));
    return {width, height};
}

void InfoCardDialog::layoutModel(const lib::Rect client_area) {
    if (!initialized_ || !area_) return;

    area_->set_bounds({0.0F, 0.0F, client_area.width, client_area.height});
    if (image_select_) return;
    const auto ui = [](const float value) noexcept {
        return lib::scaled_ui_metric(value);
    };
    const auto spacing = ui(6.0F);
    const auto name_height = name_group_
        ? std::max(ui(34.0F), lib::minimum_ui_size(name_group_).height)
        : ui(34.0F);
    const auto insets = model_.image.preview_insets;
    const auto image_height = model_.image.preview_size.height +
                              insetTop(insets) + insetBottom(insets) + ui(16.0F);
    const auto desc_y = name_height + spacing + image_height + spacing;
    const auto desc_height = std::max(0.0F, client_area.height - desc_y);

    if (name_group_) {
        name_group_->set_bounds({0.0F, 0.0F, client_area.width, name_height});
        if (name_edit_)
            name_edit_->set_bounds({ui(8.0F), ui(7.0F),
                                    std::max(0.0F, client_area.width - ui(16.0F)),
                                    std::max(0.0F, name_height - ui(14.0F))});
    }
    if (image_view_) {
        const auto image_width = model_.image.preview_size.width;
        const auto image_x = std::max(0.0F, (client_area.width - image_width) / 2.0F);
        const auto image_y = name_height + spacing + insetTop(insets) + ui(8.0F);
        image_view_->set_bounds({image_x, image_y, image_width,
                                 model_.image.preview_size.height});
        for (auto& preview : image_previews_) preview.bounds = image_view_->bounds();
    }
    if (description_group_) {
        description_group_->set_bounds({0.0F, desc_y, client_area.width,
                                        desc_height});
        if (description_edit_)
            description_edit_->set_bounds({ui(8.0F), ui(7.0F),
                                           std::max(0.0F, client_area.width - ui(16.0F)),
                                           std::max(0.0F, desc_height - ui(14.0F))});
    }
}

bool InfoCardDialog::openCWXPath(const lib::StringView path,
                                 const bool shellActivate) {
    (void)shellActivate;
    return ::cwxeditor::cwx::path::cpempty(path);
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::infocarddialog
