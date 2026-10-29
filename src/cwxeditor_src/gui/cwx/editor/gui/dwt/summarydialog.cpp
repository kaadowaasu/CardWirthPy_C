#include "cwxeditor_src/lib/service_defaults.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/summarydialog.hpp"
// UIの実装は一つに統一し、
// 別経路の実装を設けない。

#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/core/cwx/warning.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/areaviewutils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/commons.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/customtext.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dutils.hpp"
#include "cwxeditor_src/lib/graphics.hpp"
#include "cwxeditor_src/lib/window_host.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"
#include "cwxeditor_src/lib/ui_text_layout.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <sstream>
#include <utility>

namespace cwxeditor::cwx::editor::gui::dwt::summarydialog {
namespace {

Services resolveServices(const Props* prop, Services supplied) {
    return lib::with_service_defaults(prop ? prop->images().services() : Services{},
                                      std::move(supplied));
}

template <typename Component>
std::shared_ptr<Component> makeComponent(const Services& services,
                                         const lib::ComponentKind kind) {
    if (services.gui) {
        if (auto candidate = services.gui->create_component(kind)) {
            if (auto component = std::dynamic_pointer_cast<Component>(candidate)) {
                return component;
            }
        }
    }
    return std::make_shared<Component>();
}

std::shared_ptr<lib::Dialog> makeDialog(const Services& services) {
    if (services.gui) {
        if (auto dialog = services.gui->create_dialog()) return dialog;
    }
    return std::make_shared<lib::Dialog>();
}

void setColumn(const std::shared_ptr<lib::Widget>& widget, const std::size_t column,
               const std::size_t span = 1U) {
    if (!widget) return;
    lib::LayoutItem item;
    item.column = column;
    item.column_span = std::max<std::size_t>(span, 1U);
    item.horizontal = lib::Align::Stretch;
    item.vertical = lib::Align::Center;
    widget->set_layout_item(item);
}

String joinLines(const std::vector<String>& lines) {
    String result;
    for (const auto& line : lines) {
        if (!result.empty()) result.push_back('\n');
        result += line;
    }
    return result;
}

String targetLevelText(const Props& prop, const Summary& summary, int lower, int upper) {
    if (summary.legacy() && (upper < lower || (lower == 0 && upper != 0))) upper = lower;
    if (lower > 0 && lower == upper) {
        return ::cwxeditor::cwx::utils::tryFormat(prop.requiredMsgs().targetLevelSame.value, lower);
    }
    if (lower > 0 && upper > 0) {
        return ::cwxeditor::cwx::utils::tryFormat(prop.requiredMsgs().targetLevelHL.value, lower, upper);
    }
    if (lower > 0) {
        return ::cwxeditor::cwx::utils::tryFormat(prop.requiredMsgs().targetLevelL.value, lower);
    }
    if (upper > 0) {
        return ::cwxeditor::cwx::utils::tryFormat(prop.requiredMsgs().targetLevelH.value, upper);
    }
    return {};
}

bool accepted(const lib::DialogResult result) noexcept {
    return result == lib::DialogResult::OK || result == lib::DialogResult::Yes ||
           result == lib::DialogResult::Apply;
}

String imageLabel(const CardImage& image) {
    switch (image.type) {
    case types::CardImageType::File:
        return image.path();
    case types::CardImageType::PCNumber:
        return String("PC#") + std::to_string(image.pcNumber());
    case types::CardImageType::Talker:
        return "talker";
    }
    return {};
}

String pathText(const std::filesystem::path& path) {
    return structs::pathToUtf8(path);
}

String summaryDialogTitle(const Props& prop, const Summary* summary) {
    return summary ? ::cwxeditor::cwx::utils::tryFormat(
                         prop.requiredMsgs().dlgTitSummary.value, summary->scenarioName())
                   : prop.requiredMsgs().dlgTitSummary.value;
}

}  // namespace

SummaryPreview::SummaryPreview(Props& prop, Summary& summary, Parent parent,
                               std::function<bool()> loadScaledImage,
                               TextGetter dataVersion, Services services)
    : prop_(&prop), summary_(&summary), root_(std::move(parent)),
      services_(resolveServices(&prop, std::move(services))),
      load_scaled_image_(std::move(loadScaledImage)), data_version_(std::move(dataVersion)) {
    if (!root_) root_ = makeComponent<lib::Panel>(services_, lib::ComponentKind::Panel);
    build();
}

SummaryPreview::SummaryPreview(Props& prop, Summary& summary, lib::Container& parent,
                               std::function<bool()> loadScaledImage,
                               TextGetter dataVersion, Services services)
    : SummaryPreview(prop, summary, Parent{}, std::move(loadScaledImage),
                     std::move(dataVersion), std::move(services)) {
    parent.add(root_);
}

SummaryPreview::~SummaryPreview() {
    releaseResource();
}

void SummaryPreview::build() {
    root_->set_layout(std::make_shared<lib::FillLayout>());
    frame_ = makeComponent<lib::GroupBox>(services_, lib::ComponentKind::GroupBox);
    frame_->title = prop_->requiredMsgs().summaryPreview.value;
    // D版と同様、シナリオ概要の Canvas を正確な倍率適用済みサイズ
    // で中央に置く。 共通バックエンドの CenterLayout に対応
    // する Stack は意図的に全体を埋めるため、 ここでは共通のグリッ
    // ド配置を使い、400x370の概要を右ペイン全体に引き伸ばさない。
    auto preview_layout = std::make_shared<lib::GridLayout>();
    preview_layout->columns = 1U;
    preview_layout->spacing = 0.0F;
    preview_layout->padding = {5.0F, 5.0F, 5.0F, 5.0F};
    frame_->set_layout(std::move(preview_layout));
    root_->add(frame_);

    // リソースを持つ ImageView は互換用の取得口として残
    // すが、D版のプレビューは一つの Canvas で描く。 画像
    // の下に別々の部品として現れないよう、背景と文字を合成する。
    image_ = makeComponent<lib::ImageView>(services_, lib::ComponentKind::ImageView);
    image_->scale_mode = lib::ImageScaleMode::Fit;
    image_->accessibility_name = prop_->requiredMsgs().summaryPreview.value;
    canvas_ = makeComponent<lib::Canvas>(services_, lib::ComponentKind::Canvas);
    canvas_->accessibility_name = prop_->requiredMsgs().summaryPreview.value;
    canvas_->on_paint_context = [this](lib::DrawingContext& context,
                                       const lib::Rect bounds) {
        paint(context, bounds);
    };
    frame_->add(canvas_);
    updateScale();
}

void SummaryPreview::releaseResource() noexcept {
    if (owns_resource_ && resource_ != 0U && services_.graphics) {
        services_.graphics->destroy_image(resource_);
    }
    owns_resource_ = false;
    resource_ = 0U;
    if (image_) image_->image = 0U;
}

void SummaryPreview::updateScale() {
    if (!prop_) return;
    const auto size = prop_->looks().summarySize();
    const auto width = static_cast<float>(prop_->s(static_cast<std::int32_t>(size.width)));
    const auto height = static_cast<float>(prop_->s(static_cast<std::int32_t>(size.height)));
    if (image_) image_->set_bounds({0.0F, 0.0F, width, height});
    if (canvas_) {
        canvas_->set_bounds({0.0F, 0.0F, width, height});
        canvas_->set_layout_item({.horizontal = lib::Align::Center,
                                  .vertical = lib::Align::Center,
                                  .minimum = {width, height},
                                  .preferred = {width, height},
                                  .constrain_minimum_to_preferred = true});
    }
    // CenterLayout の標準余白は各辺5px。 ネイティ
    // ブの実際の境界が返る前でも、表示・非表示で対称にサイズを変
    // えられるよう、 Group の安定した推奨寸法を保持する。
    const lib::Size groupSize{width + 10.0F, height + 10.0F};
    if (frame_) {
        frame_->set_layout_item({.horizontal = lib::Align::Stretch,
                                 .vertical = lib::Align::Stretch,
                                 .preferred = groupSize});
    }
    if (root_) {
        root_->set_layout_item({.horizontal = lib::Align::Stretch,
                                .vertical = lib::Align::Stretch,
                                .preferred = groupSize});
    }
}

float SummaryPreview::displayWidth() const noexcept {
    if (frame_ && frame_->bounds().width > 0.0F) return frame_->bounds().width;
    if (frame_ && frame_->layout_item().preferred.width > 0.0F)
        return frame_->layout_item().preferred.width;
    if (root_ && root_->bounds().width > 0.0F) return root_->bounds().width;
    if (root_ && root_->layout_item().preferred.width > 0.0F)
        return root_->layout_item().preferred.width;
    return canvas_ ? canvas_->bounds().width + 10.0F : 0.0F;
}

void SummaryPreview::setImageSelect(TextGetter scenarioName, ImagesGetter imagePaths,
                                    SkinGetter selectedSkin, TextGetter description,
                                    LevelGetter levelMin, LevelGetter levelMax) {
    scenario_name_ = std::move(scenarioName);
    image_paths_ = std::move(imagePaths);
    selected_skin_ = std::move(selectedSkin);
    description_text_ = std::move(description);
    level_min_ = std::move(levelMin);
    level_max_ = std::move(levelMax);
    redrawImage();
}

void SummaryPreview::updateLabels() {
    if (!prop_ || !summary_) return;
    if (image_) {
        std::size_t count = image_paths_ ? image_paths_().size() : summary_->imagePaths().size();
        image_->accessibility_description = "summary images: " + std::to_string(count);
    }
}

void SummaryPreview::paint(lib::DrawingContext& context, const lib::Rect bounds) {
    if (!prop_ || !summary_) return;
    const auto summarySize = prop_->looks().summarySize();
    const float baseWidth = std::max(1.0F, static_cast<float>(summarySize.width));
    const float baseHeight = std::max(1.0F, static_cast<float>(summarySize.height));
    const float sx = bounds.width / baseWidth;
    const float sy = bounds.height / baseHeight;

    if (resource_ != 0U && !buffer_.data.frames.empty()) {
        const auto& frame = buffer_.data.frames.front();
        context.draw_image(resource_,
                           {0.0F, 0.0F, static_cast<float>(frame.pixel_width),
                            static_cast<float>(frame.pixel_height)},
                           bounds, lib::Interpolation::Best);
    }

    auto selected = selected_skin_ ? selected_skin_() : SkinPtr{};
    if (!selected) {
        selected = dskin::findSkin2(*prop_, summary_->type(), summary_->skinName());
    }
    const bool legacy = selected ? selected->legacy() : summary_->legacy();
    if (selected && image_paths_) {
        ensurePortraitCache(selected);
        const auto imagePoint = prop_->looks().summaryImageXY();
        for (const auto& portrait : portrait_cache_) {
            if (portrait.resource == 0U || portrait.data.frames.empty()) continue;
            const auto& frame = portrait.data.frames.front();
            const float sourceScale = static_cast<float>(
                std::max<std::uint32_t>(1U, portrait.source_scale));
            const float logicalWidth = portrait.logical_size.width > 0.0F
                                           ? portrait.logical_size.width
                                           : static_cast<float>(frame.pixel_width) /
                                                 sourceScale;
            const float logicalHeight = portrait.logical_size.height > 0.0F
                                            ? portrait.logical_size.height
                                            : static_cast<float>(frame.pixel_height) /
                                                  sourceScale;
            float x = static_cast<float>(imagePoint.x);
            float y = static_cast<float>(imagePoint.y);
            if (portrait.position == types::CardImagePosition::Center) {
                x = (baseWidth - logicalWidth) / 2.0F;
                y = (baseHeight - logicalHeight) / 2.0F;
            }
            context.draw_image(
                portrait.resource,
                {0.0F, 0.0F, static_cast<float>(frame.pixel_width),
                 static_cast<float>(frame.pixel_height)},
                {bounds.x + x * sx, bounds.y + y * sy,
                 logicalWidth * sx, logicalHeight * sy},
                lib::Interpolation::Best);
        }
    }
    const auto black = lib::Color{0U, 0U, 0U, 255U};
    const auto levelColorValue = dprops::dwtData(prop_->looks().summaryLevelColor());
    const auto levelColor = lib::Color{
        levelColorValue.color.red, levelColorValue.color.green,
        levelColorValue.color.blue,
        static_cast<std::uint8_t>(std::clamp(levelColorValue.alpha, 0, 255))};

    const auto centered = [&](StringView text, structs::CFont font,
                              const float y, const lib::Color color) {
        auto spec = dutils::createFontFromPixels(prop_->adjustFont(std::move(font)),
                                                 prop_->drawingScale() >= 2U);
        spec.points *= sy;
        const auto measured = context.measure_text(text, spec, bounds.width);
        context.draw_text(text,
                          {bounds.x + (bounds.width - measured.width) / 2.0F,
                           bounds.y + y * sy},
                          spec, color);
    };

    const auto lower = level_min_ ? level_min_() : static_cast<int>(summary_->levelMin());
    const auto upper = level_max_ ? level_max_() : static_cast<int>(summary_->levelMax());
    centered(targetLevelText(*prop_, *summary_, lower, upper),
             prop_->looks().summaryLevelFont(legacy),
             static_cast<float>(prop_->looks().summaryLevelY()), levelColor);
    centered(scenario_name_ ? scenario_name_() : summary_->scenarioName(),
             prop_->looks().summaryTitleFont(legacy),
             static_cast<float>(prop_->looks().summaryTitleY()), black);

    auto descFont = dutils::createFontFromPixels(
        prop_->adjustFont(prop_->looks().summaryDescFont(legacy)),
        prop_->drawingScale() >= 2U);
    descFont.points *= sy;
    const auto descOrigin = prop_->looks().summaryDescXY();
    const float y = bounds.y + static_cast<float>(descOrigin.y) * sy;
    const auto description = description_text_ ? description_text_() : summary_->desc();
    // D版と同様、両形式で DRAW_DELIMITER | DRAW_TRANSPARENT
    // を使い、DRAW_TAB は付けない。 区切りでの分割は共有するが、計測・描画する
    // 各文字列内のタブは保持する。 Classicスキンは設計上の固定の行送りを維持する。
    const auto description_layout = lib::layout_gc_text(
        context, description, descFont, lib::GcTextOptions{true, false});
    const auto classic_line_step = legacy
        ? std::optional<float>{static_cast<float>(
              prop_->looks().summaryDescLineHeightClassic()) * sy}
        : std::optional<float>{};
    lib::draw_gc_text(context, description_layout,
        {bounds.x + static_cast<float>(descOrigin.x) * sx, y},
        descFont, black, classic_line_step);
    centered(prop_->requiredMsgs().summaryPageDummy.value,
             prop_->looks().summaryPageFont(legacy),
             static_cast<float>(prop_->looks().summaryPageY()), black);
}

void SummaryPreview::ensurePortraitCache(const SkinPtr& selected) {
    if (!prop_ || !summary_ || !selected || !image_paths_) return;
    const auto paths = image_paths_();
    const auto targetScale = load_scaled_image_ && load_scaled_image_()
                                 ? prop_->drawingScale()
                                 : 1U;
    const auto version = data_version_ ? data_version_() : summary_->dataVersion();
    const auto same_paths = [&] {
        if (paths.size() != portrait_paths_.size()) return false;
        for (std::size_t index = 0; index < paths.size(); ++index) {
            const auto& current = paths[index];
            const auto& cached = portrait_paths_[index];
            if (static_cast<bool>(current) != static_cast<bool>(cached)) return false;
            if (current && cached && !current->opEquals(*cached)) return false;
        }
        return true;
    };
    if (portrait_cache_valid_ && portrait_skin_ == selected.get() &&
        portrait_scale_ == targetScale && portrait_version_ == version &&
        same_paths()) {
        return;
    }

    std::vector<CachedPortrait> prepared;
    prepared.reserve(paths.size());
    for (const auto& image : paths) {
        if (!image || image->type != types::CardImageType::File ||
            image->path().empty()) {
            continue;
        }
        bool skinMaterial = false;
        bool engineMaterial = false;
        const auto path = selected->findImagePathF(
            image->path(), summary_->scenarioPath(), version,
            skinMaterial, engineMaterial);
        if (path.empty()) continue;
        // D版の SummaryPreview と同様、CardWirth
        // の左上の色を透明色として貼り紙画像を読み込み、 マスク適用済み
        // の画像をプレビュー倍率へ広げる。 不透明として読むと、シナ
        // リオの貼り紙にClassicの紫のマスク色が見えてしまう。
        auto loaded = areaviewutils::detail::detailLoadImage(
            *prop_, *selected, summary_, path, targetScale, {}, true, false);
        if (loaded.data.frames.empty()) continue;
        CachedPortrait portrait;
        portrait.data = std::move(loaded.data);
        portrait.resource = loaded.resource;
        portrait.logical_size = loaded.logical_size;
        portrait.source_scale = loaded.source_scale;
        portrait.position = image->positionType();
        if (services_.graphics &&
            (loaded.owned_resource || portrait.resource == 0U)) {
            portrait.upload =
                std::make_shared<lib::OwnedImageResource>(services_.graphics);
            portrait.upload->id = loaded.owned_resource
                                      ? portrait.resource
                                      : services_.graphics->create_image(portrait.data);
            portrait.resource = portrait.upload->id;
        }
        prepared.push_back(std::move(portrait));
    }

    std::vector<CardImagePtr> snapshots;
    snapshots.reserve(paths.size());
    for (const auto& image : paths) {
        snapshots.push_back(image ? std::make_shared<CardImage>(nullptr, *image)
                                  : CardImagePtr{});
    }
    portrait_cache_.swap(prepared);
    portrait_paths_ = std::move(snapshots);
    portrait_skin_ = selected.get();
    portrait_scale_ = targetScale;
    portrait_version_ = version;
    portrait_cache_valid_ = true;
}

void SummaryPreview::redrawImage() {
    if (!prop_ || !summary_) return;
    auto selected = selected_skin_ ? selected_skin_() : SkinPtr{};
    if (!selected) {
        selected = dskin::findSkin2(*prop_, summary_->type(), summary_->skinName());
    }
    if (!selected) {
        clearBuf();
        updateLabels();
        return;
    }
    // スキンの概要背景は常に drawingScale で選ぶ。 loadScaledImage
    // に従うのは、paint() で処理するシナリオの貼り紙の画像素材だけ。
    const auto scale = prop_->drawingScale();
    if (!background_cache_valid_ || rendered_skin_ != selected.get() ||
        rendered_scale_ != scale) {
        auto prepared = dskin::summary(*selected, scale, services_);
        if (owns_resource_ && resource_ != 0U && services_.graphics &&
            prepared.resource == 0U && !prepared.data.frames.empty()) {
            services_.graphics->update_image(resource_, prepared.data);
            prepared.resource = resource_;
            prepared.owned_resource = true;
            buffer_ = std::move(prepared);
        } else {
            releaseResource();
            buffer_ = std::move(prepared);
            resource_ = buffer_.resource;
            owns_resource_ = buffer_.owned_resource;
            if (resource_ == 0U && services_.graphics &&
                !buffer_.data.frames.empty()) {
                resource_ = services_.graphics->create_image(buffer_.data);
                owns_resource_ = resource_ != 0U;
                buffer_.resource = resource_;
                buffer_.owned_resource = owns_resource_;
            }
        }
        rendered_skin_ = selected.get();
        rendered_scale_ = scale;
        background_cache_valid_ = true;
    }
    if (image_) image_->image = resource_;
    updateScale();
    updateLabels();
    if (canvas_ && canvas_->invalidate) canvas_->invalidate();
}

void SummaryPreview::invalidatePortraitCache() noexcept {
    portrait_cache_valid_ = false;
}

void SummaryPreview::clearBuf() {
    releaseResource();
    buffer_.clear();
    rendered_skin_ = nullptr;
    rendered_scale_ = 0U;
    background_cache_valid_ = false;
    portrait_cache_.clear();
    portrait_paths_.clear();
    portrait_skin_ = nullptr;
    portrait_scale_ = 0U;
    portrait_version_.clear();
    portrait_cache_valid_ = false;
    if (canvas_ && canvas_->invalidate) canvas_->invalidate();
}

SummaryDialog::SummaryDialog(Props& prop, std::shared_ptr<lib::Window> parent,
                             Summary* summary,
                             const bool readOnly, Services services,
                             commons::Commons* editorCommon)
    : absdialog::AbsDialog(
          prop, parent,
          readOnly ? static_cast<std::uint32_t>(absdialog::StyleFlag::ReadOnly)
                   : 0U,
          false, summaryDialogTitle(prop, summary),
          prop.images().load(StringView{"summary.png"}), true,
          &prop.flex().summaryDlg, true, true, {}, false, services),
      prop_(&prop), summary_(summary),
      services_(resolveServices(&prop, std::move(services))),
      content_(absdialog::AbsDialog::content()), read_only_(readOnly),
      image_paths_(summary ? summary->imagePaths() : std::vector<CardImagePtr>{}),
      load_scaled_image_value_(summary && summary->loadScaledImage()),
      start_area_value_(summary ? summary->startArea() : 0U),
      last_image_scale_(prop.imageScalePercent()) {
    if (dialog()) {
        dialog()->icon = prop.images().load(StringView{"summary.png"});
        // D版の400x370のプレビューと通常のダイアログ部品で、ク
        // ライアント領域の最小高さは460pxとなる。 従来の490px
        // では、プレビューと説明グループがさらに30px大きくなる。
        dialog()->minimum_size = lib::Size{500.0F, 460.0F};
    }
    if (summary_ && (read_only_ || summary_->scenarioPath().empty())) {
        // D版と同様、取り込み元やパスのない Summary には完全な解決処理を使う。
        // 有効なホストの Common、または短命の Common で同じ経路を通す。
        if (editorCommon && !editorCommon->lifetimeToken().expired()) {
            summary_skin_ = commons::findSkin(*editorCommon, *prop_, summary_);
        } else {
            commons::Commons resolver(prop_);
            summary_skin_ = commons::findSkin(resolver, *prop_, summary_);
        }
    }
    if (editorCommon && !editorCommon->lifetimeToken().expired())
        bindEditorCommons(editorCommon);
}

SummaryDialog::~SummaryDialog() {
    if (editor_commons_ && !editor_commons_lifetime_.expired()) {
        editor_commons_->refSortCondition.remove(sort_refresh_connection_);
        editor_commons_->refClassicSkin.remove(classic_refresh_connection_);
        editor_commons_->refSkin.remove(skin_refresh_connection_);
        editor_commons_->refImageScale.remove(image_scale_refresh_connection_);
        editor_commons_->refDataVersion.remove(data_version_refresh_connection_);
        editor_commons_->refTargetVersion.remove(target_version_refresh_connection_);
    }
    applying_lifetime_.reset();
    const auto clear = [](const auto& control) {
        if (control) {
            control->on_changed = {};
        }
    };
    clear(scenario_name_);
    clear(author_);
    clear(description_);
    clear(level_min_);
    clear(level_max_);
    clear(coupon_number_);
    clear(coupons_);
    clear(type_skin_);
    clear(type_classic_);
    clear(load_scaled_image_);
    clear(use_key_in_action_);
    clear(no_consume_unmatched_menu_card_);
    clear(numeric_keycodes_);
    if (type_) {
        type_->on_selection_changed = {};
        type_->on_key_down = {};
        type_->on_text_input = {};
        type_->on_pointer = {};
    }
    if (data_version_control_) data_version_control_->on_selection_changed = {};
    if (preview_toggle_) preview_toggle_->on_changed = {};
    if (start_area_tree_) {
        start_area_tree_->on_selection_changed = {};
        start_area_tree_->on_activate = {};
        start_area_tree_->on_expansion_changed = {};
    }
    if (start_area_chooser_) {
        if (const auto list = start_area_chooser_->list()) {
            list->on_selection_changed = {};
            list->on_cell_activate = {};
        }
    }
    cancelEvent.clear();
}

void SummaryDialog::buildControls() {
    if (initialized_ || !content_ || !summary_) return;
    auto layout = std::make_shared<lib::GridLayout>();
    layout->columns = 2;
    layout->spacing = 6.0F;
    content_->set_layout(std::move(layout));

    auto pages = makeComponent<lib::TabView>(services_, lib::ComponentKind::TabView);
    pages->set_layout_item({.column = 0,
                            // D版の summarydialog.d と同様、左の
                            // CTabFolder に GridData.FILL_BOTH
                            // を指定する。 横・縦の余りを左側が使い、プ
                            // レビューは自然な幅を保って縦だけを埋める。
                            .horizontal = lib::Align::Stretch,
                            .vertical = lib::Align::Stretch,
                            .grow = 1.0F});
    auto base = makeComponent<lib::Panel>(services_, lib::ComponentKind::Panel);
    auto baseLayout = std::make_shared<lib::LinearLayout>(lib::Orientation::Vertical);
    baseLayout->spacing = 4.0F;
    base->set_layout(std::move(baseLayout));
    auto skinPage = makeComponent<lib::Panel>(services_, lib::ComponentKind::Panel);
    auto skinLayout = std::make_shared<lib::LinearLayout>(lib::Orientation::Vertical);
    skinLayout->spacing = 4.0F;
    skinPage->set_layout(std::move(skinLayout));
    auto etcPage = makeComponent<lib::Panel>(services_, lib::ComponentKind::Panel);
    auto etcLayout = std::make_shared<lib::LinearLayout>(lib::Orientation::Vertical);
    etcLayout->spacing = 4.0F;
    etcPage->set_layout(std::move(etcLayout));
    pages->pages = {{prop_->requiredMsgs().baseData.value, 0, base, false},
                    {prop_->requiredMsgs().etcData.value, 0, skinPage, false},
                    {prop_->requiredMsgs().etcSettings.value, 0, etcPage, false}};
    pages->selected_index = 0U;
    content_->add(pages);

    image_area_ = makeComponent<lib::Panel>(services_, lib::ComponentKind::Panel);
    setColumn(image_area_, 1);
    auto imageItem = image_area_->layout_item();
    // D版の SummaryPreview と同様、FILL_BOTH で
    // はなく GridData.FILL_VERTICAL を使う。 プ
    // レビューの自然幅は安定させ、余った横幅は左の編集タブに割り当てる。
    imageItem.horizontal = lib::Align::Start;
    imageItem.vertical = lib::Align::Stretch;
    image_area_->set_layout_item(imageItem);
    content_->add(image_area_);

    constructTab1(*base);
    constructTab2(*skinPage);
    constructTab3(*etcPage);
    constructImage(image_area_);
    initialized_ = true;
}

void SummaryDialog::setup(lib::Container& area) {
    if (initialized_ || !summary_) return;
    if (!content_ || content_.get() != &area)
        content_ = Parent(&area, [](lib::Container*) {});
    buildControls();
    setControlEnabled(!read_only_);
    // AbsDialog::openImpl が初回表示前に setup
    // を一度呼ぶ。 updateDataVersion は警告も更新す
    // るため、初期処理はここだけにし、buildControls()
    // の末尾で重ねない。 旧形式の機能制限を、最終的な有効状態にする。
    updateDataVersion();
}

void SummaryDialog::setup(const Parent& area) {
    if (area) setup(*area);
}

void SummaryDialog::buildTextField(lib::Container& area, String title,
                                   const std::shared_ptr<lib::TextEdit>& control) {
    auto row = makeComponent<lib::Panel>(services_, lib::ComponentKind::Panel);
    auto layout = std::make_shared<lib::GridLayout>();
    layout->columns = 2;
    layout->spacing = 4.0F;
    row->set_layout(std::move(layout));
    auto label = makeComponent<lib::Label>(services_, lib::ComponentKind::Label);
    label->text = std::move(title);
    setColumn(label, 0);
    row->add(label);
    setColumn(control, 1);
    row->add(control);
    area.add(row);
}

void SummaryDialog::buildNumberField(
    lib::Container& area, String title,
    const std::shared_ptr<lib::NumberEdit>& control) {
    auto row = makeComponent<lib::Panel>(services_, lib::ComponentKind::Panel);
    auto layout = std::make_shared<lib::GridLayout>();
    layout->columns = 2;
    layout->spacing = 4.0F;
    row->set_layout(std::move(layout));
    auto label = makeComponent<lib::Label>(services_, lib::ComponentKind::Label);
    label->text = std::move(title);
    setColumn(label, 0);
    row->add(label);
    setColumn(control, 1);
    row->add(control);
    area.add(row);
}

void SummaryDialog::constructTab1(lib::Container& area) {
    const auto group = [this](String title) {
        auto result = makeComponent<lib::GroupBox>(services_, lib::ComponentKind::GroupBox);
        result->title = std::move(title);
        auto layout = std::make_shared<lib::LinearLayout>(lib::Orientation::Vertical);
        layout->spacing = 4.0F;
        layout->padding = {5.0F, 5.0F, 5.0F, 5.0F};
        result->set_layout(std::move(layout));
        return result;
    };

    auto upper = std::make_shared<splitpane::SplitPane>(splitpane::Horizontal);
    upper->resizeControl1(true);
    upper->set_layout_item({.horizontal = lib::Align::Stretch,
                            .vertical = lib::Align::Stretch,
                            .preferred = {460.0F, 225.0F}, .grow = 1.0F});
    area.add(upper);

    auto images = makeComponent<lib::Panel>(services_, lib::ComponentKind::Panel);
    images->set_layout_item({.horizontal = lib::Align::Stretch,
                             .vertical = lib::Align::Stretch,
                             .preferred = {220.0F, 220.0F}, .grow = 1.0F});
    upper->add(images);

    scenario_name_ = makeComponent<lib::TextEdit>(services_, lib::ComponentKind::TextEdit);
    scenario_name_->text = summary_->scenarioName();
    author_ = makeComponent<lib::TextEdit>(services_, lib::ComponentKind::TextEdit);
    author_->text = summary_->author();

    const auto cardSize = prop_->looks().cardSize();
    image_select_ = std::make_unique<imageselect::ImageSelectBase>(
        imageselect::ImageSelectKind::Card, false, images, read_only_ ? 1 : 0,
        *prop_, summary_, prop_->s(static_cast<std::int32_t>(cardSize.width)),
        prop_->s(static_cast<std::int32_t>(cardSize.height)),
        prop_->s(structs::CInsets{}), types::CardImagePosition::TopLeft, true,
        [this] {
            return summary::toExportedImageNameWithoutCardName(
                prop_->parent(),
                scenario_name_ ? scenario_name_->text : summary_->scenarioName(),
                author_ ? author_->text : summary_->author());
        },
        [this] {
            if (summary_image_) summary_image_->invalidatePortraitCache();
        }, imageselect::ImageSelectBase::Definitions{},
        // D版と同様、取り込み元・読み取り専用の Summary は開いた時点のスキンに固定する。
        // 編集可能なシナリオの Summary は、現在の Commons のスキンに追従する。
        false, summary_skin_, services_);
    image_select_->bindEditorCommons(
        editor_commons_lifetime_.expired() ? nullptr : editor_commons_);
    image_select_->setCardImages(image_paths_, false);
    closeEvent.push_back([this] {
        if (image_select_) image_select_->active(false);
    });
    openedEvent.push_back([this] {
        if (image_select_) image_select_->active(true);
    });
    image_select_->loadScaledImage([this] { return load_scaled_image_value_; });
    image_select_->enabled(!read_only_);
    image_select_->widget()->set_layout_item({
        .horizontal = lib::Align::Stretch, .vertical = lib::Align::Stretch,
        .grow = 1.0F});
    image_select_->modEvent.push_back([this] {
        image_paths_ = image_select_->cardImages();
        modified_ = true;
        applyEnabled();
        refreshWarning();
        // ImageSelect はこの変更コールバックの後に、updateImageEvent
        // を一度通知する。 プレビューはそこで更新し、一
        // 回の画像編集で同じ概要ラスターを二度生成しない。
    });
    image_select_->updateImageEvent.push_back([this] { refreshPreview(); });

    auto parameters = makeComponent<lib::Panel>(services_, lib::ComponentKind::Panel);
    auto parameterLayout = std::make_shared<lib::LinearLayout>(lib::Orientation::Vertical);
    parameterLayout->spacing = 4.0F;
    parameters->set_layout(std::move(parameterLayout));
    parameters->set_layout_item({.horizontal = lib::Align::Stretch,
                                 .vertical = lib::Align::Stretch,
                                 .preferred = {230.0F, 220.0F}, .grow = 1.0F});
    upper->add(parameters);
    upper->setWeights({prop_->var().summaryParamSashL.value,
                       prop_->var().summaryParamSashR.value});
    upper->on_weights_changed = [this](const std::vector<float>& weights) {
        if (weights.size() < 2U) return;
        prop_->var().summaryParamSashL.value = static_cast<std::int32_t>(
            std::max(1.0F, std::round(weights[0])));
        prop_->var().summaryParamSashR.value = static_cast<std::int32_t>(
            std::max(1.0F, std::round(weights[1])));
        saveWin();
    };

    auto titleGroup = group(prop_->requiredMsgs().title.value);
    // D版の対応する centerGroup と同様、固有の高さではなく FILL_BOTH を使う。
    if (!lib::editor_window_host_options().all_mode)
        titleGroup->set_layout_item({.horizontal = lib::Align::Stretch,
                                  .vertical = lib::Align::Stretch, .grow = 1.0F});
    titleGroup->add(scenario_name_);
    parameters->add(titleGroup);
    auto authorGroup = group(prop_->requiredMsgs().author.value);
    // D版の対応する centerGroup と同様、固有の高さではなく FILL_BOTH を使う。
    if (!lib::editor_window_host_options().all_mode)
        authorGroup->set_layout_item({.horizontal = lib::Align::Stretch,
                                  .vertical = lib::Align::Stretch, .grow = 1.0F});
    authorGroup->add(author_);
    parameters->add(authorGroup);

    auto levelRow = group(prop_->requiredMsgs().targetLevel.value);
    // D版の対応する centerGroup と同様、固有の高さではなく FILL_BOTH を使う。
    if (!lib::editor_window_host_options().all_mode)
        levelRow->set_layout_item({.horizontal = lib::Align::Stretch,
                                  .vertical = lib::Align::Stretch, .grow = 1.0F});
    auto levelLayout = std::make_shared<lib::GridLayout>();
    levelLayout->columns = 4;
    levelLayout->spacing = 4.0F;
    levelRow->set_layout(std::move(levelLayout));
    level_min_ = makeComponent<lib::NumberEdit>(services_, lib::ComponentKind::NumberEdit);
    level_min_->minimum = 0.0;
    level_min_->maximum = prop_->var().levelMax.value;
    level_min_->value = summary_->levelMin();
    setColumn(level_min_, 0);
    levelRow->add(level_min_);
    auto separator = makeComponent<lib::Label>(services_, lib::ComponentKind::Label);
    separator->text = prop_->requiredMsgs().levSep.value;
    setColumn(separator, 1);
    levelRow->add(separator);
    level_max_ = makeComponent<lib::NumberEdit>(services_, lib::ComponentKind::NumberEdit);
    level_max_->minimum = 0.0;
    level_max_->maximum = prop_->var().levelMax.value;
    level_max_->value = summary_->levelMax();
    setColumn(level_max_, 2);
    levelRow->add(level_max_);
    auto rangeHint = makeComponent<lib::Label>(services_, lib::ComponentKind::Label);
    rangeHint->text = ::cwxeditor::cwx::utils::tryFormat(
        prop_->requiredMsgs().rangeHint.value,
        static_cast<std::int32_t>(level_min_->minimum),
        static_cast<std::int32_t>(level_min_->maximum));
    setColumn(rangeHint, 3);
    levelRow->add(rangeHint);
    parameters->add(levelRow);

    description_ = makeComponent<lib::TextEdit>(services_, lib::ComponentKind::TextEdit);
    description_->multiline = true;
    description_->wrap = true;
    // D版の FixedWidthText と同様、MULTI|WRAP|BORDER
    // を付け、SWT.V_SCROLL は付けない。 共通の TextEdit の標準ス
    // クロールバーは、設計上の半角40セルから14pxを消費し、余分な折り返しを起こす。
    description_->vertical_scroll = false;
    description_->text = summary_->desc();
    const bool summary_legacy = [&] {
        const auto skin = summSkin();
        return skin ? skin->legacy() : summary_->legacy();
    }();
    // D版の FixedWidthText と同様、通常のUI編
    // 集フォントではなく summaryDescFont で構
    // 築する。 概要のプレビューにも使う、太字の明朝フォント。
    auto description_font = dutils::createFontFromPixels(
        prop_->adjustFont(prop_->looks().summaryDescFont(summary_legacy)),
        prop_->drawingScale() >= 2U);
    // 概要のテキストも FixedWidthText の本文であり、ダイアログの操作文字ではない。
    description_font.content_font = true;
    description_->set_font(description_font);
    // D版の FixedWidthText(summaryDescLen)
    // と同様、半角40セルでコントロールの幅を決める。 従来の455px
    // 固定ではタブのほぼ全幅を消費し、右半分に大きな空白ができる。
    const auto description_cells = std::max(1, prop_->looks().summaryDescLen());
    // FixedWidthText は、指定した画素フォントを現在のUI・
    // DPI倍率で実体化してから全角セルを計測する。 倍率未適用の CFont
    // ではなく、表示側が使う実際のウィジェットのフォントを計測する。
    const auto effective_description_font =
        lib::scaled_editor_widget_font(description_font);
    const auto full_width = customtext::detail::measureText(
        services_.graphics.get(), "　", effective_description_font).width;
    const auto half_width = customtext::detail::measureText(
        services_.graphics.get(), " ", effective_description_font).width;
    const float measured_full_width = full_width > 0.0F
        ? full_width : effective_description_font.points;
    const float measured_half_width = half_width > 0.0F
        ? half_width : effective_description_font.points * 0.5F;
    const float description_width =
        measured_full_width * static_cast<float>(description_cells / 2) + 1.0F +
        ((description_cells & 1) != 0 ? measured_half_width : 0.0F);
    const auto measured_line = customtext::detail::measureText(
        services_.graphics.get(), "Mg", effective_description_font).height;
    const float line_height = measured_line > 0.0F
        ? measured_line : effective_description_font.points;
    // SWT の computeSize(widthHint, fontMetrics.height * lines)
    // と同様、文字欄の枠・内側の余白を加える。 共通の TextEdit は各辺3pxを使う。
    const float description_height =
        line_height * static_cast<float>(prop_->looks().summaryDescLine()) + 6.0F;
    description_->set_layout_item({.horizontal = lib::editor_window_host_options().all_mode
                                    ? lib::Align::Start : lib::Align::Center,
                                    .vertical = lib::Align::Stretch,
                                    .minimum = {description_width, description_height},
                                    .preferred = {description_width, description_height}});
    auto descriptionGroup = group(prop_->requiredMsgs().desc.value);
    // D版と同様、説明の Group を伸ばし、固定幅の子を中央に置く。 ALL
    // は既存の、Group を中央、子を左寄せにする配置を維持する。
    descriptionGroup->set_layout_item({
        .horizontal = lib::editor_window_host_options().all_mode
            ? lib::Align::Center : lib::Align::Stretch,
        .vertical = lib::Align::Start});
    descriptionGroup->add(description_);
    area.add(descriptionGroup);

    scenario_name_->on_changed = [this](StringView) {
        modified_ = true;
        refreshWarning();
        refreshPreview();
    };
    author_->on_changed = [this](StringView) { modified_ = true; refreshWarning(); };
    description_->on_changed = [this](StringView) {
        modified_ = true;
        refreshWarning();
        refreshPreview();
    };
    const auto alive = std::weak_ptr<int>(applying_lifetime_);
    const auto preview = [this, alive](double) {
        if (alive.expired()) return;
        modified_ = true;
        refreshWarning();
        if (alive.expired()) return;
        refreshPreview();
        if (!alive.expired()) applyEnabled();
    };
    level_min_->on_preview = preview;
    level_max_->on_preview = preview;
    const auto cancelled = [this, alive] {
        if (alive.expired()) return;
        refreshWarning();
        if (alive.expired()) return;
        refreshPreview();
        if (!alive.expired()) applyEnabled();
    };
    level_min_->on_edit_cancelled = cancelled;
    level_max_->on_edit_cancelled = cancelled;
    level_min_->on_committed = [this, alive, preview](const double value) {
        if (alive.expired()) return;
        levMinEnter(static_cast<int>(value));
        preview(value);
    };
    level_max_->on_committed = [this, alive, preview](const double value) {
        if (alive.expired()) return;
        levMaxEnter(static_cast<int>(value));
        preview(value);
    };
    mod(*scenario_name_);
    mod(*author_);
    mod(*description_);
    mod(*level_min_);
    mod(*level_max_);
    // 後の編集でもガードが残るよう、すべてのコールバックのラッパーを設定した後に登録する。
    checker(scenario_name_);
}

void SummaryDialog::constructTab2(lib::Container& area) {
    const auto group = [this](String title) {
        auto result = makeComponent<lib::GroupBox>(services_, lib::ComponentKind::GroupBox);
        result->title = std::move(title);
        auto layout = std::make_shared<lib::LinearLayout>(lib::Orientation::Vertical);
        layout->spacing = 4.0F;
        layout->padding = {5.0F, 5.0F, 5.0F, 5.0F};
        result->set_layout(std::move(layout));
        return result;
    };

    auto split = std::make_shared<splitpane::SplitPane>(splitpane::Horizontal);
    split->set_layout_item({.horizontal = lib::Align::Stretch,
                            .vertical = lib::Align::Stretch,
                            .preferred = {470.0F, 335.0F}, .grow = 1.0F});
    area.add(split);

    auto left = makeComponent<lib::Panel>(services_, lib::ComponentKind::Panel);
    auto leftLayout = std::make_shared<lib::LinearLayout>(lib::Orientation::Vertical);
    leftLayout->spacing = 4.0F;
    left->set_layout(std::move(leftLayout));
    left->set_layout_item({.horizontal = lib::Align::Stretch,
                           .vertical = lib::Align::Stretch,
                           .preferred = {235.0F, 330.0F}, .grow = 1.0F});
    split->add(left);

    auto modeRow = group(prop_->requiredMsgs().scenarioType.value);
    type_skin_ = makeComponent<lib::RadioButton>(services_, lib::ComponentKind::RadioButton);
    type_skin_->text = prop_->requiredMsgs().sTypeXML.value;
    type_skin_->group = "summary-skin-mode";
    modeRow->add(type_skin_);
    type_classic_ = makeComponent<lib::RadioButton>(services_, lib::ComponentKind::RadioButton);
    type_classic_->text = prop_->requiredMsgs().sTypeClassic.value;
    type_classic_->group = "summary-skin-mode";
    modeRow->add(type_classic_);

    type_ = makeComponent<lib::ComboBox>(services_, lib::ComponentKind::ComboBox);
    type_->accessibility_name = prop_->requiredMsgs().scenarioType.value;
    modeRow->add(type_);
    left->add(modeRow);

    incsearch::IncSearchSettings searchSettings;
    searchSettings.startIncrementalSearchWhenKeyDown =
        prop_->var().startIncrementalSearchWhenKeyDown.value;
    const auto* search_prop = prop_;
    searchSettings.startIncrementalSearchWhenKeyDownProvider = [search_prop] {
        return search_prop && search_prop->var().startIncrementalSearchWhenKeyDown.value;
    };
    searchSettings.incrementalSearchBoxWidth =
        prop_->var().incrementalSearchBoxWidth.value;
    searchSettings.incrementalSearchType = prop_->var().incrementalSearchType.value;
    searchSettings.containsLabel = prop_->requiredMsgs().incSearchContains.value;
    searchSettings.wildcardLabel = prop_->requiredMsgs().incSearchWildcard.value;
    searchSettings.regexLabel = prop_->requiredMsgs().incSearchRegex.value;
    searchSettings.windowTitle = prop_->requiredMsgs().menuText(types::MenuID::IncSearch);
    skin_inc_search_ = std::make_unique<incsearch::IncSearch>(
        services_, type_.get(),
        [this] { return type_ && type_->enabled() && !type_->items.empty(); },
        std::vector<incsearch::AdditionMatcher>{}, std::move(searchSettings));
    skin_inc_search_->modEvent.push_back(
        [this] { refreshTypes(selected_skin_, true); });
    type_->on_key_down = [this](lib::KeyEvent& event) {
        if (skin_inc_search_ &&
            skin_inc_search_->handleHostKeyDown(event, type_.get())) {
            event.handled = true;
        }
    };
    type_->on_text_input = [this](lib::TextInputEvent& event) {
        if (skin_inc_search_)
            event.handled = skin_inc_search_->handleHostTextInput(event, type_.get());
    };
    skin_popup_ = std::make_shared<lib::Menu>(true);
    lib::MenuItem searchItem;
    searchItem.text = prop_->requiredMsgs().menuText(types::MenuID::IncSearch);
    searchItem.command = "incremental-search";
    searchItem.icon = prop_->images().load(StringView{"inc_search.png"});
    skin_popup_->items.push_back(std::move(searchItem));
    skin_popup_->on_command = [this](const StringView command) {
        if (command == "incremental-search" && skin_inc_search_)
            skin_inc_search_->startIncSearch();
    };
    type_->on_pointer = [this](lib::PointerEvent& event) {
        const bool popup =
            (event.action == lib::PointerAction::Up &&
             event.button == lib::PointerButton::Secondary) ||
            event.action == lib::PointerAction::LongPress;
        if (!popup || !skin_popup_ || !services_.gui) return;
        services_.gui->show_context_menu(skin_popup_, type_, event.position);
        event.handled = true;
    };
    type_->on_selection_changed = [this](std::optional<std::size_t>) {
        selected_skin_ = selectedSkin();
        modified_ = true;
        refreshWarning();
        refreshPreview();
    };
    type_skin_->on_changed = [this](const bool checked) {
        if (checked) type_classic_->checked = false;
        updateSkinMode();
    };
    type_classic_->on_changed = [this](const bool checked) {
        if (checked) type_skin_->checked = false;
        updateSkinMode();
    };

    auto versionGroup = group(prop_->requiredMsgs().dataVersion.value);
    data_version_control_ = makeComponent<lib::ComboBox>(services_, lib::ComponentKind::ComboBox);
    std::optional<std::size_t> selected_version;
    for (std::size_t i = 0; i < versioninfo::VERSIONS.size(); ++i) {
        data_version_control_->items.push_back(
            {::cwxeditor::cwx::utils::tryFormat(prop_->requiredMsgs().dataVersionName.value,
                                                versioninfo::VERSION_NAMES[i],
                                                versioninfo::ENGINES[i]),
              versioninfo::VERSIONS[i], 0, true});
        if (summary_->dataVersion() == versioninfo::VERSIONS[i]) selected_version = i;
    }
    if (!data_version_control_->items.empty())
        data_version_control_->selected_index = selected_version.value_or(0U);
    data_version_control_->on_selection_changed = [this](std::optional<std::size_t>) {
        modified_ = true;
        updateDataVersion();
        refreshPreview();
    };
    versionGroup->add(data_version_control_);

    load_scaled_image_ = makeComponent<lib::CheckBox>(services_, lib::ComponentKind::CheckBox);
    load_scaled_image_->text = prop_->requiredMsgs().loadScaledImage.value;
    load_scaled_image_->accessibility_description = prop_->requiredMsgs().loadScaledImageHint.value;
    load_scaled_image_->checked = load_scaled_image_value_;
    load_scaled_image_->on_changed = [this](const bool value) {
        const std::weak_ptr<int> lifetime = applying_lifetime_;
        load_scaled_image_value_ = value;
        modified_ = true;
        if (summary_image_) summary_image_->invalidatePortraitCache();
        // D版と同様、新しい拡大画像の方針で画像候補を調べ直す。
        if (image_select_) image_select_->refreshList();
        if (lifetime.expired()) return;
        refreshWarning();
        if (lifetime.expired()) return;
        refreshPreview();
    };
    versionGroup->add(load_scaled_image_);
    left->add(versionGroup);

    auto qualification = group(prop_->requiredMsgs().qualification.value);
    auto qualificationLayout = std::make_shared<lib::GridLayout>();
    qualificationLayout->columns = 2;
    qualificationLayout->spacing = 4.0F;
    qualification->set_layout(std::move(qualificationLayout));
    coupon_number_ = makeComponent<lib::NumberEdit>(services_, lib::ComponentKind::NumberEdit);
    coupon_number_->minimum = 0.0;
    coupon_number_->maximum = 999.0;
    coupon_number_->value = summary_->rCouponNum();
    auto couponNumberLabel = makeComponent<lib::Label>(services_, lib::ComponentKind::Label);
    couponNumberLabel->text = prop_->requiredMsgs().rCouponNum.value;
    setColumn(couponNumberLabel, 0);
    qualification->add(couponNumberLabel);
    setColumn(coupon_number_, 1);
    qualification->add(coupon_number_);
    auto couponLabel = makeComponent<lib::Label>(services_, lib::ComponentKind::Label);
    couponLabel->text = prop_->requiredMsgs().rCoupons.value;
    couponLabel->set_layout_item({.row = 1, .column = 0, .column_span = 2,
                                  .horizontal = lib::Align::Start,
                                  .vertical = lib::Align::Center});
    qualification->add(couponLabel);
    coupons_ = makeComponent<lib::TextEdit>(services_, lib::ComponentKind::TextEdit);
    coupons_->multiline = true;
    coupons_->wrap = true;
    coupons_->text = joinLines(summary_->rCoupons());
    coupons_->set_layout_item({.row = 2, .column = 0, .column_span = 2,
                               .horizontal = lib::Align::Stretch,
                               .vertical = lib::Align::Stretch,
                               .preferred = {220.0F, 95.0F}, .grow = 1.0F});
    qualification->add(coupons_);
    left->add(qualification);

    auto startGroup = group(prop_->requiredMsgs().startArea.value);
    startGroup->set_layout_item({.horizontal = lib::Align::Stretch,
                                 .vertical = lib::Align::Stretch,
                                 .preferred = {225.0F, 330.0F}, .grow = 1.0F});
    split->add(startGroup);
    chooser::Commons chooserCommons;
    chooserCommons.services = services_;
    chooser::ChooserProperties chooserProperties;
    chooserProperties.logicalSort = prop_->var().logicalSort.value;
    chooserProperties.showAreaDirTree = prop_->var().showAreaDirTree.value;
    chooserProperties.expandChooserItems = prop_->var().expandChooserItems.value;
    chooserProperties.idColumn = prop_->var().idColumn.value;
    chooserProperties.nameTableWidth = prop_->var().nameTableWidth.value;
    chooserProperties.nameTableHeight = prop_->var().nameTableHeight.value;
    chooserProperties.storeIdColumn = [this](const std::int32_t width) {
        if (!prop_) return;
        prop_->var().idColumn.value = width;
        (void)prop_->apply();
    };
    chooserProperties.messages.allExpanded = prop_->requiredMsgs().allExpanded.value;
    chooserProperties.messages.incrementalSearch =
        prop_->requiredMsgs().menuText(types::MenuID::IncSearch);
    chooserProperties.images.area = prop_->images().load(StringView{"area.png"});
    chooserProperties.images.areaDir = prop_->images().load(StringView{"areadir.png"});
    chooserProperties.images.startArea =
        prop_->images().load(StringView{"start_area.png"});

    Summary::Owner summaryOwner = summary_->weak_from_this().lock();
    if (!summaryOwner) summaryOwner = Summary::Owner(summary_, [](Summary*) {});
    start_area_chooser_ =
        std::make_unique<chooser::AreaChooser<area::Area, true>>(
            std::move(chooserCommons), std::move(summaryOwner), startGroup.get(),
            std::move(chooserProperties));
    start_area_tree_ = start_area_chooser_->tree();
    start_area_chooser_->widget()->set_layout_item(
        {.horizontal = lib::Align::Stretch, .vertical = lib::Align::Stretch,
         .preferred = {215.0F, 295.0F}, .grow = 1.0F});
    // 選択部品がツリー・表、展開行の配置と有効化を管理する。 ホス
    // トは未確定の開始値を渡し、確定した編集を監視するだけにする。
    start_area_chooser_->startArea(start_area_value_);
    start_area_chooser_->modEvent.push_back([this] {
        if (!start_area_chooser_) return;
        const auto value = start_area_chooser_->startArea();
        if (value == start_area_value_) return;
        start_area_value_ = value;
        modified_ = true;
        applyEnabled();
    });
    start_area_chooser_->selected(start_area_value_);

    split->setWeights({prop_->var().rCouponsStartAreaSashL.value,
                       prop_->var().rCouponsStartAreaSashR.value});
    split->on_weights_changed = [this](const std::vector<float>& weights) {
        if (weights.size() < 2U) return;
        prop_->var().rCouponsStartAreaSashL.value = static_cast<std::int32_t>(
            std::max(1.0F, std::round(weights[0])));
        prop_->var().rCouponsStartAreaSashR.value = static_cast<std::int32_t>(
            std::max(1.0F, std::round(weights[1])));
        saveWin();
    };

    coupon_number_->on_changed = [this](double) {
        modified_ = true;
        refreshWarning();
    };
    coupons_->on_changed = [this](StringView) {
        modified_ = true;
        refreshWarning();
    };

    refreshTypes();
    mod(*type_skin_);
    mod(*type_classic_);
    mod(*type_);
    mod(*data_version_control_);
    mod(*load_scaled_image_);
    mod(*coupon_number_);
    mod(*coupons_);
}

void SummaryDialog::constructTab3(lib::Container& area) {
    auto other = makeComponent<lib::GroupBox>(services_, lib::ComponentKind::GroupBox);
    other->title = prop_->requiredMsgs().etcSettings.value;
    auto layout = std::make_shared<lib::LinearLayout>(lib::Orientation::Vertical);
    layout->padding = {5.0F, 5.0F, 5.0F, 5.0F};
    other->set_layout(std::move(layout));
    area.add(other);
    use_key_in_action_ = makeComponent<lib::CheckBox>(services_, lib::ComponentKind::CheckBox);
    use_key_in_action_->text = prop_->requiredMsgs().useKeyInAction.value;
    use_key_in_action_->checked = summary_->useKeyInAction();
    other->add(use_key_in_action_);
    use_key_in_action_->on_changed = [this](bool) { modified_ = true; };
    mod(*use_key_in_action_);
    no_consume_unmatched_menu_card_ = makeComponent<lib::CheckBox>(services_, lib::ComponentKind::CheckBox);
    no_consume_unmatched_menu_card_->text = prop_->requiredMsgs().noConsumeUnmatchedMenuCard.value;
    no_consume_unmatched_menu_card_->checked = summary_->noConsumeUnmatchedMenuCard();
    other->add(no_consume_unmatched_menu_card_);
    no_consume_unmatched_menu_card_->on_changed = [this](bool) { modified_ = true; };
    mod(*no_consume_unmatched_menu_card_);
    numeric_keycodes_ = makeComponent<lib::CheckBox>(services_, lib::ComponentKind::CheckBox);
    numeric_keycodes_->text = "キーコード＠数値によるキーコードの強度判定を有効にする";
    numeric_keycodes_->checked = summary_->numericKeycodes();
    other->add(numeric_keycodes_);
    numeric_keycodes_->on_changed = [this](bool) { modified_ = true; };
    mod(*numeric_keycodes_);
}

void SummaryDialog::constructImage(const Parent& area) {
    image_area_ = area;
    if (!image_area_) return;
    image_area_->set_layout(std::make_shared<lib::FillLayout>());

    auto footer = addition();
    auto footerLayout = std::make_shared<lib::LinearLayout>(lib::Orientation::Horizontal);
    footerLayout->spacing = 4.0F;
    footer->set_layout(std::move(footerLayout));
    preview_toggle_ = makeComponent<lib::ToggleButton>(services_, lib::ComponentKind::ToggleButton);
    preview_toggle_->text = prop_->requiredMsgs().messagePreview.value;
    preview_toggle_->checked = prop_->var().showSummaryPreview.value;
    preview_toggle_->set_layout_item({.horizontal = lib::Align::Start,
                                      .vertical = lib::Align::Center,
                                      .preferred = {
                                          static_cast<float>(prop_->var().buttonWidth.value),
                                          0.0F}});
    preview_toggle_->on_changed = [this](const bool value) { showImagePreview(value); };
    footer->add(preview_toggle_);
    showImagePreview(preview_toggle_->checked, false);
}

void SummaryDialog::setControlEnabled(const bool enabled) {
    const auto set = [enabled](const auto& control) {
        if (control) control->set_enabled(enabled);
    };
    set(scenario_name_); set(author_); set(description_); set(level_min_);
    set(level_max_); set(coupon_number_); set(coupons_); set(start_area_tree_);
    set(type_skin_); set(type_classic_); set(type_); set(data_version_control_);
    set(load_scaled_image_); set(use_key_in_action_);
    set(no_consume_unmatched_menu_card_);
    set(numeric_keycodes_);
    if (start_area_chooser_) {
        if (const auto widget = start_area_chooser_->widget()) widget->set_enabled(enabled);
        if (const auto expanded = start_area_chooser_->allExpandedButton())
            expanded->set_enabled(enabled);
    }
    if (image_select_) image_select_->enabled(enabled);
}

void SummaryDialog::updateSkinMode() {
    if (!type_skin_ || !type_classic_) return;
    if (!type_skin_->checked && !type_classic_->checked) {
        type_skin_->checked = !summSkin() || !summSkin()->legacy();
        type_classic_->checked = !type_skin_->checked;
    }
    refreshTypes();
    modified_ = true;
    refreshWarning();
    refreshPreview();
}

void SummaryDialog::raiseCancelled() {
    for (const auto& callback : cancelEvent) if (callback) callback();
}

SkinPtr SummaryDialog::summSkin() const {
    if (summary_skin_) return summary_skin_;
    // D版と同様、既存の編集可能なシナリオでは、Classicも含めて現在の Commons のスキンを使う。
    if (!lib::editor_window_host_options().all_mode && editor_commons_ &&
        !editor_commons_lifetime_.expired()) return editor_commons_->skin();
    if (!summary_ || !prop_) return {};
    return dskin::findSkin2(*prop_, summary_->type(), summary_->skinName());
}

SkinPtr SummaryDialog::selectedSkin() const {
    if (!summary_ || !prop_ || !type_) return summSkin();
    if (!type_->selected_index)
        return selected_skin_ ? selected_skin_ : summSkin();
    const auto index = *type_->selected_index;
    if (type_skin_ && type_skin_->checked) {
        if (index >= skin_info_.size()) return summSkin();
        const auto& info = skin_info_[index];
        for (const auto& [path, candidate] : dskin::skinTable(*prop_)) {
            if (candidate && candidate->path() == info.path) return candidate;
        }
        return dskin::findSkin2(*prop_, prop_->var().defaultSkin.value,
                                prop_->var().defaultSkinName.value);
    }
    if (type_classic_ && type_classic_->checked) {
        if (has_legacy_skin_ && index == 0U) {
            return Skin::findLegacySkin(prop_->parent(), prop_->enginePath(),
                                        summary_->scenarioPath(),
                                        prop_->var().classicEngineRegex.value,
                                        prop_->var().classicDataDirRegex.value,
                                        prop_->var().classicMatchKey.value,
                                        classic_engines_, prop_->var().defaultSkin.value);
        }
        const auto engineIndex = index - (has_legacy_skin_ ? 1U : 0U);
        if (engineIndex < classic_engines_.size()) {
            return dskin::createClassicSkin(*prop_, classic_engines_[engineIndex]);
        }
    }
    return summSkin();
}

String SummaryDialog::dataVersion() const {
    if (data_version_control_ && data_version_control_->selected_index &&
        *data_version_control_->selected_index < data_version_control_->items.size()) {
        return data_version_control_->items[*data_version_control_->selected_index].value;
    }
    return summary_ ? summary_->dataVersion() : versioninfo::DEFAULT_VERSION;
}

void SummaryDialog::refreshWarning() {
    warnings_.clear();
    if (!summary_ || !prop_) return;
    const auto skin = selectedSkin();
    auto append = [this](const std::vector<String>& values) {
        warnings_.insert(warnings_.end(), values.begin(), values.end());
    };
    if (skin && image_select_) {
        // パスだけを別に検査せず、画像位置やPC番号の互換
        // 性も含め、 D版と同じ選択欄の警告処理を使う。
        const auto imageWarnings = image_select_->warningsWith(summary_->legacy(), dataVersion(), *skin);
        append(imageWarnings);
    }
    if (load_scaled_image_ && load_scaled_image_->checked &&
        !versioninfo::isTargetVersion(dataVersion(), "2")) {
        warnings_.push_back(prop_->requiredMsgs().warningLoadScaledImage.value);
    }
    const auto name = scenario_name_ ? scenario_name_->text : summary_->scenarioName();
    const auto author = author_ ? author_->text : summary_->author();
    const auto desc = description_ ? description_->text : summary_->desc();
    const auto coupons = coupons_ ? coupons_->text : joinLines(summary_->rCoupons());
    append(::cwxeditor::cwx::warning::sjisWarnings(prop_->parent(), summary_, name,
                                                   prop_->requiredMsgs().title.value));
    append(::cwxeditor::cwx::warning::sjisWarnings(prop_->parent(), summary_, author,
                                                   prop_->requiredMsgs().author.value));
    const auto lower = level_min_ ? static_cast<int>(level_min_->value)
                                  : static_cast<int>(summary_->levelMin());
    const auto upper = level_max_ ? static_cast<int>(level_max_->value)
                                  : static_cast<int>(summary_->levelMax());
    if (summary_->legacy() && (lower != 0 || upper != 0)) {
        if (lower == 0) warnings_.push_back(prop_->requiredMsgs().warningNoLevelMin.value);
        else if (upper == 0) warnings_.push_back(prop_->requiredMsgs().warningNoLevelMax.value);
    }
    if (lower != 0 && upper != 0 && lower > upper) {
        warnings_.push_back(prop_->requiredMsgs().searchErrorReversalLevel.value);
    }
    append(::cwxeditor::cwx::warning::sjisWarnings(prop_->parent(), summary_, desc,
                                                   prop_->requiredMsgs().desc.value));
    append(::cwxeditor::cwx::warning::sjisWarnings(prop_->parent(), summary_, coupons,
                                                   prop_->requiredMsgs().rCoupons.value));
    warning(warnings_);
}

void SummaryDialog::levMaxEnter(const int value) {
    if (read_only_ || !level_min_) return;
    if (value > 0 && level_min_->value != 0.0 && value < level_min_->value) {
        level_min_->value = value;
    }
}

void SummaryDialog::levMinEnter(const int value) {
    if (read_only_ || !level_max_) return;
    if (value > 0 && level_max_->value != 0.0 && value > level_max_->value) {
        level_max_->value = value;
    }
}

void SummaryDialog::showImagePreview(const bool visible, const bool resizeWindow) {
    if (prop_) prop_->var().showSummaryPreview = visible;
    if (preview_toggle_) preview_toggle_->checked = visible;
    if (content_) {
        if (const auto grid =
                std::dynamic_pointer_cast<lib::GridLayout>(content_->layout())) {
            grid->columns = visible ? 2U : 1U;
        }
    }
    if (image_area_) image_area_->set_visible(visible);
    if (!visible) {
        if (!summary_image_) return;
        preview_width_ = summary_image_->displayWidth();
        if (preview_width_ <= 0.0F) {
            preview_width_ = static_cast<float>(prop_->s(
                                 static_cast<std::int32_t>(
                                     prop_->looks().summarySize().width))) +
                             10.0F;
        }
        summary_image_.reset();
        if (image_area_) image_area_->clear();
        if (resizeWindow && dialog() && dialog()->bounds().width > preview_width_) {
            auto bounds = dialog()->bounds();
            bounds.width -= preview_width_;
            dialog()->set_bounds(bounds);
        }
        return;
    }
    if (!image_area_ || !summary_) return;
    if (!summary_image_) {
        summary_image_ = std::make_shared<SummaryPreview>(
            *prop_, *summary_, image_area_, [this] { return load_scaled_image_value_; },
            [this] { return dataVersion(); }, services_);
        summary_image_->setImageSelect(
            [this] { return scenario_name_ ? scenario_name_->text : summary_->scenarioName(); },
            [this] { return image_paths_; }, [this] { return selectedSkin(); },
            [this] {
                if (!description_) return summary_->desc();
                // D版と同様、プレビューは生の編集バッファではなく FixedWidthText::getRRText()
                // に結び付ける。 この入力欄の全角空白の実測値を使
                // い、ウィンドウ幅ではなく設計上の桁数で折り返す。
                const auto font = lib::scaled_editor_widget_font(
                    description_->font().value_or(lib::FontSpec{}));
                const auto columns = std::max(0, prop_->looks().summaryDescLen());
                auto* graphics = services_.graphics.get();
                const auto full = customtext::detail::measureText(graphics, "　", font).width;
                const auto half = customtext::detail::measureText(graphics, " ", font).width;
                const auto width = full * static_cast<float>(columns / 2) + 1.0F
                    + ((columns & 1) ? half : 0.0F);
                return customtext::FixedWidthText<>::toRRText(
                    description_->text, width, graphics, true, font);
            },
            [this] { return level_min_ ? static_cast<int>(level_min_->value)
                                       : static_cast<int>(summary_->levelMin()); },
             [this] { return level_max_ ? static_cast<int>(level_max_->value)
                                        : static_cast<int>(summary_->levelMax()); });
        preview_width_ = summary_image_->displayWidth();
        if (preview_width_ <= 0.0F) {
            preview_width_ = static_cast<float>(prop_->s(
                                 static_cast<std::int32_t>(
                                     prop_->looks().summarySize().width))) +
                             10.0F;
        }
        if (resizeWindow && dialog()) {
            auto bounds = dialog()->bounds();
            bounds.width += preview_width_;
            dialog()->set_bounds(bounds);
        }
    }
}

void SummaryDialog::bindEditorCommons(commons::Commons* common) {
    if (editor_commons_ && !editor_commons_lifetime_.expired()) {
        editor_commons_->refSortCondition.remove(sort_refresh_connection_);
        editor_commons_->refClassicSkin.remove(classic_refresh_connection_);
        editor_commons_->refSkin.remove(skin_refresh_connection_);
        editor_commons_->refImageScale.remove(image_scale_refresh_connection_);
        editor_commons_->refDataVersion.remove(data_version_refresh_connection_);
        editor_commons_->refTargetVersion.remove(target_version_refresh_connection_);
    }
    sort_refresh_connection_ = classic_refresh_connection_ = 0;
    skin_refresh_connection_ = image_scale_refresh_connection_ = 0;
    data_version_refresh_connection_ = target_version_refresh_connection_ = 0;
    editor_commons_ = common;
    editor_commons_lifetime_ = common ? common->lifetimeToken()
                                     : std::weak_ptr<const int>{};
    if (image_select_) image_select_->bindEditorCommons(common);
    if (!common) return;
    const std::weak_ptr<int> dialog_life = applying_lifetime_;
    const auto common_life = editor_commons_lifetime_;
    // バージョン通知は、単独版と ALL でこの経路を共有する。
    data_version_refresh_connection_ = common->refDataVersion.add(
        [this, dialog_life, common_life] {
            if (dialog_life.expired() || common_life.expired()) return;
            updateDataVersion();
        });
    target_version_refresh_connection_ = common->refTargetVersion.add(
        [this, dialog_life, common_life] {
            if (dialog_life.expired() || common_life.expired()) return;
            refreshWarning();
        });
    if (!lib::editor_window_host_options().all_mode) {
        // D版の SummaryDialog と同様、開いている選択欄を設定画面と同期する。
        const auto refresh = [this, dialog_life, common_life] {
            if (dialog_life.expired() || common_life.expired()) return;
            refreshTypes(selectedSkin());
        };
        sort_refresh_connection_ = common->refSortCondition.add(refresh);
        classic_refresh_connection_ = common->refClassicSkin.add(refresh);
        skin_refresh_connection_ = common->refSkin.add(
            [this, dialog_life, common_life] {
                if (dialog_life.expired() || common_life.expired()) return;
                refSkin();
            });
        image_scale_refresh_connection_ = common->refImageScale.add(
            [this, dialog_life, common_life] {
                if (dialog_life.expired() || common_life.expired()) return;
                refImageScale();
            });
    }
}

void SummaryDialog::refSkin() {
    if (read_only_) return;
    refreshPreview();
    const auto skin = summSkin();
    auto font = dutils::createFontFromPixels(
        prop_->adjustFont(prop_->looks().summaryDescFont(
            skin ? skin->legacy() : summary_->legacy())),
        prop_->drawingScale() >= 2U);
    font.content_font = true;
    if (description_) description_->set_font(std::move(font));
    // SummaryDialog からの Commons.refSkin には送
    // 信元がないため、 現在のスキン選択を保ちつつ、通知ごとに選択欄を組み直す。
    refreshTypes(selectedSkin());
}

void SummaryDialog::refImageScale() {
    const auto current = prop_->imageScalePercent();
    if (image_select_) {
        const auto cardSize = prop_->looks().cardSize();
        image_select_->setPreviewSize(
            prop_->s(static_cast<std::int32_t>(cardSize.width)),
            prop_->s(static_cast<std::int32_t>(cardSize.height)),
            prop_->s(structs::CInsets{}));
    }
    const bool changed = current != last_image_scale_;
    last_image_scale_ = current;
    if (summary_image_) {
        const auto old_width = preview_width_ > 0.0F
                                   ? preview_width_
                                   : summary_image_->displayWidth();
        if (changed) {
            // D版は倍率変更時にSWTのプレビューを再構築する。 共通
            // のプレビューはウィジェットを保持し、倍率をキーとするキャ
            // ッシュをその場で更新して、 下で同じ表示幅を更新する。
            summary_image_->redrawImage();
        } else {
            // D版と同様、倍率が変わらず他の画像設定だけが変わった場合は、
            // ラスターのキャッシュを消してプレビューの再描画を要求する。
            clearBuf();
            refreshPreview();
        }
        const auto size = prop_->looks().summarySize();
        const auto new_width = static_cast<float>(prop_->s(
                                   static_cast<std::int32_t>(size.width))) +
                               10.0F;
        preview_width_ = new_width;
        if (changed && preview_toggle_ && preview_toggle_->checked && dialog() &&
            old_width > 0.0F && std::abs(new_width - old_width) > 0.5F) {
            auto bounds = dialog()->bounds();
            bounds.width = std::max(1.0F, bounds.width + new_width - old_width);
            dialog()->set_bounds(bounds);
        }
    }
}

void SummaryDialog::refreshPreview() {
    if (summary_image_) summary_image_->redrawImage();
}

void SummaryDialog::clearBuf() {
    if (summary_image_) summary_image_->clearBuf();
}

void SummaryDialog::updateDataVersion() {
    if (!data_version_control_ || !summary_) return;
    data_version_control_->set_enabled(!read_only_ && !summary_->legacy());
    if (load_scaled_image_) load_scaled_image_->set_enabled(!read_only_ && !summary_->legacy());
    refreshWarning();
}

void SummaryDialog::refreshTypes(SkinPtr skin, const bool incrementalSearch) {
    if (!type_ || !summary_ || !prop_) return;
    const auto item_order_revision = type_->item_order_revision;
    if (incrementalSearch && !skin) skin = selected_skin_;
    String selectedType = skin ? skin->type() : summary_->type();
    String selectedName = skin ? skin->name() : summary_->skinName();
    String selectedPath = skin ? skin->path() : String{};
    std::vector<ClassicEngine> next_classic_engines;
    for (const auto& engine : prop_->var().classicEngines.value) {
        if (skin_inc_search_ && !skin_inc_search_->match(engine.name)) continue;
        next_classic_engines.push_back(engine);
    }
    std::vector<lib::ChoiceItem> items;
    std::vector<SkinInfo> next_skin_info;
    bool next_has_legacy_skin = false;
    std::optional<std::size_t> selected;

    if (!type_skin_ || (!type_skin_->checked && !type_classic_->checked)) {
        const auto current = summSkin();
        if (current && current->legacy()) type_classic_->checked = true;
        else type_skin_->checked = true;
    }

    if (type_skin_->checked) {
        std::vector<SkinPtr> skins;
        for (const auto& [path, candidate] : dskin::skinTable(*prop_)) {
            if (candidate) skins.push_back(candidate);
        }
        if (skins.empty()) skins.push_back(dskin::findSkin2(*prop_, prop_->var().defaultSkin.value,
                                                            prop_->var().defaultSkinName.value));
        const bool logical = prop_->var().logicalSort.value;
        std::stable_sort(skins.begin(), skins.end(), [logical](const SkinPtr& left,
                                                               const SkinPtr& right) {
            if (!left || !right) return static_cast<bool>(right);
            if (logical) {
                const auto name = ::cwxeditor::cwx::utils::ncmp(left->name(), right->name());
                return name == 0 ? ::cwxeditor::cwx::utils::ncmp(left->type(), right->type()) < 0
                                 : name < 0;
            }
            return left->name() == right->name() ? left->type() < right->type()
                                                 : left->name() < right->name();
        });
        for (const auto& candidate : skins) {
            if (!candidate) continue;
            SkinInfo info{candidate->type(), candidate->name(), candidate->path()};
            if (candidate->isEmpty()) {
                info.type = prop_->var().defaultSkin.value;
                info.name = prop_->var().defaultSkinName.value;
            }
            if (skin_inc_search_ && !skin_inc_search_->match(info.name)) continue;
            items.push_back({info.name + "(" + info.type + ")", info.type, 0, true});
            next_skin_info.push_back(std::move(info));
        }
        for (std::size_t i = 0; i < next_skin_info.size(); ++i) {
            if ((!selectedPath.empty() && next_skin_info[i].path == selectedPath) ||
                (selectedPath.empty() && next_skin_info[i].type == selectedType &&
                 next_skin_info[i].name == selectedName)) {
                selected = i;
                break;
            }
        }
        if (!incrementalSearch && !selected && !items.empty()) selected = 0U;
    } else {
        String resourceDir;
        String legacyEngine;
        next_has_legacy_skin = Skin::findLegacy(
            summary_->scenarioPath(), resourceDir, legacyEngine,
            prop_->var().classicEngineRegex.value, prop_->var().classicDataDirRegex.value,
            prop_->var().classicMatchKey.value, next_classic_engines);
        std::optional<std::size_t> current_engine;
        if (next_has_legacy_skin) {
            const auto current_path = ::cwxeditor::cwx::utils::nabs(
                StringView{legacyEngine});
            for (std::size_t i = 0; i < next_classic_engines.size(); ++i) {
                const auto configured_path = ::cwxeditor::cwx::utils::nabs(
                    StringView{prop_->toAppAbs(next_classic_engines[i].enginePath)});
                if (::cwxeditor::cwx::utils::cfnmatch(
                        current_path, configured_path)) {
                    current_engine = i;
                    break;
                }
            }
            // D版と同様、現在のエンジンが設定済みの ClassicEngine
            // の選択肢に含まれる場合は、 一覧外の現在エンジンの行を追加しない。
            if (current_engine) next_has_legacy_skin = false;
        }
        if (incrementalSearch) {
            // D版と同様、インクリメンタル検索は設定済みの選択肢だけを
            // 絞り込み、 検索中に現在のエンジンの代替行を加えない。
            next_has_legacy_skin = false;
        }
        if (next_has_legacy_skin) {
            items.push_back({prop_->requiredMsgs().defaultSelection(legacyEngine), "legacy", 0, true});
        }
        for (const auto& engine : next_classic_engines) {
            items.push_back({engine.name, engine.enginePath, 0, true});
        }
        for (std::size_t i = 0; i < next_classic_engines.size(); ++i) {
            const auto row = i + (next_has_legacy_skin ? 1U : 0U);
            if (selectedPath == next_classic_engines[i].enginePath ||
                selectedType == next_classic_engines[i].type) {
                selected = row;
                break;
            }
            if (current_engine && *current_engine == i && !selected)
                selected = row;
        }
        if (!incrementalSearch && !selected && next_has_legacy_skin) selected = 0U;
        if (!incrementalSearch && !selected && !items.empty()) selected = 0U;
        if (!incrementalSearch && items.empty()) {
            type_skin_->checked = true;
            type_classic_->checked = false;
            refreshTypes({}, incrementalSearch);
            return;
        }
    }
    const std::weak_ptr<lib::ComboBox> weak = type_;
    if (!lib::replace_combo_items(type_, std::move(items), selected, {},
            [weak, item_order_revision] {
                const auto target = weak.lock();
                return target && target->item_order_revision == item_order_revision;
            })) return;
    classic_engines_ = std::move(next_classic_engines);
    skin_info_ = std::move(next_skin_info);
    has_legacy_skin_ = next_has_legacy_skin;
    // D版と同様、インクリメンタル検索の一致がゼロでも、選択欄を有効なままにする。
    type_->set_enabled(!read_only_);
    if (type_->selected_index) selected_skin_ = selectedSkin();
}

std::vector<String> SummaryDialog::rCoupons() const {
    if (!coupons_) return summary_ ? summary_->rCoupons() : std::vector<String>{};
    auto result = ::cwxeditor::cwx::utils::splitLines(coupons_->text);
    std::erase_if(result, [](const auto& line) { return line.empty(); });
    return result;
}

void SummaryDialog::imagePaths(std::vector<CardImagePtr> value) {
    image_paths_ = std::move(value);
    if (image_select_) image_select_->setCardImages(image_paths_, false);
    modified_ = true;
    applyEnabled();
    refreshPreview();
    refreshImageControls();
}

void SummaryDialog::refreshImageControls(const std::size_t selection) {
    if (!image_select_) return;
    image_select_->setCardImages(image_paths_, false);
    if (!image_paths_.empty())
        image_select_->imageIndex(std::min(selection, image_paths_.size() - 1U));
    image_select_->enabled(!read_only_);
}

bool SummaryDialog::addImage(String path, const types::CardImagePosition position) {
    if (read_only_ || path.empty()) return false;
    image_paths_.push_back(std::make_shared<CardImage>(std::move(path), position));
    modified_ = true;
    applyEnabled();
    refreshImageControls(image_paths_.size() - 1U);
    refreshPreview();
    return true;
}

bool SummaryDialog::removeImage(const std::size_t index) {
    if (read_only_ || index >= image_paths_.size()) return false;
    image_paths_.erase(image_paths_.begin() + static_cast<std::ptrdiff_t>(index));
    modified_ = true;
    applyEnabled();
    refreshImageControls(index == 0U ? 0U : index - 1U);
    refreshPreview();
    return true;
}

bool SummaryDialog::moveImage(const std::size_t from, const std::size_t to) {
    if (read_only_ || from >= image_paths_.size() || to >= image_paths_.size()) return false;
    if (from == to) return true;
    auto image = image_paths_[from];
    image_paths_.erase(image_paths_.begin() + static_cast<std::ptrdiff_t>(from));
    image_paths_.insert(image_paths_.begin() + static_cast<std::ptrdiff_t>(to), std::move(image));
    modified_ = true;
    applyEnabled();
    refreshImageControls(to);
    refreshPreview();
    return true;
}

bool SummaryDialog::apply() {
    const std::weak_ptr<int> alive = applying_lifetime_;
    const auto generation = ++applying_generation_;
    const auto window = dialog();
    const bool was_visible = window && window->visible();
    const auto current = [&] {
        return !alive.expired() && generation == applying_generation_ &&
            (!was_visible || window->visible());
    };
    if (!summary_) return false;
    if (read_only_) return true;
    if (!scenario_name_ || !author_ || !description_ || !level_min_ || !level_max_ ||
        !coupon_number_ || !coupons_ || !start_area_chooser_ || !use_key_in_action_ ||
        !no_consume_unmatched_menu_card_ || !numeric_keycodes_) return false;
    if (image_select_) {
        const auto material = image_select_->cardMaterialPath(forceApplying());
        if (!current() || material.cancel) return false;
        image_paths_ = material.images;
    }
    // D版と同様、素材の解決後に現在の状態を取得する。 コールバックが自
    // 身を置き換えても今回の呼び出しが無効にならないよう、先に複製する。
    const auto applying = on_applying;
    if (applying) applying();
    if (!current()) return false;
    summary_->setBaseParams(scenario_name_->text, author_->text);
    // D版と同様、生の折り返し表示バッファではなく FixedWidthText::getRRText()
    // を確定する。 設計上の桁数と実際の入力欄のフォントを使い、 UIフォ
    // ント倍率でシナリオの保存テキストの折り返し位置が変わらないようにする。
    summary_->desc(customtext::FixedWidthText<>::toRRText(
        description_->text, prop_->looks().summaryDescLen(),
        description_->font().value_or(lib::FontSpec{}), true,
        services_.graphics.get()));
    summary_->imagePaths(image_paths_);
    summary_->levelMin(static_cast<std::uint32_t>(std::max(0.0, level_min_->value)));
    summary_->levelMax(static_cast<std::uint32_t>(std::max(0.0, level_max_->value)));
    summary_->rCoupons(rCoupons());
    summary_->rCouponNum(static_cast<std::uint32_t>(std::max(0.0, coupon_number_->value)));
    summary_->startArea(start_area_value_);
    if (type_skin_ && type_skin_->checked && type_ && type_->selected_index &&
        *type_->selected_index < skin_info_.size()) {
        const auto& info = skin_info_[*type_->selected_index];
        summary_->type(info.type);
        summary_->skinName(info.name);
    } else {
        summary_->type({});
        summary_->skinName({});
    }
    const auto version = dataVersion();
    if (summary_->dataVersion() != version) summary_->dataVersion(version);
    summary_->loadScaledImage(load_scaled_image_->checked);
    summary_->useKeyInAction(use_key_in_action_->checked);
    // これらの Summary.xml の設定はCardWirthCエンジンの拡張で、D版
    // の SummaryDialog に項目はない。 同じ適用成功時の確定処理で保持する。
    summary_->noConsumeUnmatchedMenuCard(no_consume_unmatched_menu_card_->checked);
    summary_->numericKeycodes(numeric_keycodes_->checked);
    selected_skin_ = selectedSkin();
    if (selected_skin_) selected_skin_->initialize();
    modified_ = false;
    if (dialog()) {
        dialog()->modified = false;
        dialog()->title = ::cwxeditor::cwx::utils::tryFormat(
            prop_->requiredMsgs().dlgTitSummary.value, summary_->scenarioName());
    }
    return true;
}

bool SummaryDialog::close(const bool ok, bool& cancelClose) {
    const auto result = absdialog::AbsDialog::close(ok, cancelClose);
    if (!ok && !cancelClose) raiseCancelled();
    return result;
}

void SummaryDialog::cancel() {
    modified_ = false;
    raiseCancelled();
    forceCancel();
}

bool SummaryDialog::open() {
    ++applying_generation_;
    if (!summary_) return false;
    return absdialog::AbsDialog::open();
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::summarydialog
