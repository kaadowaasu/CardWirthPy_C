#include "app/all/all_layout.hpp"
#include "cw/dialog/placement.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace cardwirth::app::all {
namespace {

constexpr float epsilon = 0.001F;

float finite_nonnegative(float value) noexcept
{
    return std::isfinite(value) ? std::max(0.0F, value) : 0.0F;
}

Rect normalized(Rect rect) noexcept
{
    rect.x = std::isfinite(rect.x) ? rect.x : 0.0F;
    rect.y = std::isfinite(rect.y) ? rect.y : 0.0F;
    rect.width = finite_nonnegative(rect.width);
    rect.height = finite_nonnegative(rect.height);
    return rect;
}

Rect intersection(Rect left, Rect right) noexcept
{
    left = normalized(left);
    right = normalized(right);
    const float x = std::max(left.x, right.x);
    const float y = std::max(left.y, right.y);
    const float right_edge = std::min(left.right(), right.right());
    const float bottom_edge = std::min(left.bottom(), right.bottom());
    return {x, y, std::max(0.0F, right_edge - x),
            std::max(0.0F, bottom_edge - y)};
}

}  // namespace

bool Rect::contains(Point point) const noexcept
{
    if (!valid() || !std::isfinite(point.x) || !std::isfinite(point.y)) return false;
    return point.x >= x && point.y >= y && point.x <= right() && point.y <= bottom();
}

bool Rect::contains(Rect rect) const noexcept
{
    if (!valid() || !rect.valid()) return false;
    return rect.x + epsilon >= x && rect.y + epsilon >= y &&
           rect.right() <= right() + epsilon && rect.bottom() <= bottom() + epsilon;
}

std::optional<Point> CoordinateTransform::to_source(Point host) const noexcept
{
    if (!destination.contains(host)) return std::nullopt;
    return to_source_unclipped(host);
}

std::optional<Point> CoordinateTransform::to_source_unclipped(Point host) const noexcept
{
    if (!source.valid() || !std::isfinite(scale_x) || !std::isfinite(scale_y) ||
        scale_x <= 0.0F || scale_y <= 0.0F) return std::nullopt;
    const Point result{(host.x - destination.x) / scale_x,
                       (host.y - destination.y) / scale_y};
    if (!std::isfinite(result.x) || !std::isfinite(result.y)) return std::nullopt;
    return result;
}

Point CoordinateTransform::to_host(Point local) const noexcept
{
    return {destination.x + local.x * scale_x, destination.y + local.y * scale_y};
}

Rect CoordinateTransform::to_host(Rect local) const noexcept
{
    const auto origin = to_host(Point{local.x, local.y});
    return {origin.x, origin.y, local.width * scale_x, local.height * scale_y};
}

Rect inset(Rect rect, float amount) noexcept
{
    rect = normalized(rect);
    amount = finite_nonnegative(amount);
    const float x = std::min(amount, rect.width * 0.5F);
    const float y = std::min(amount, rect.height * 0.5F);
    return {rect.x + x, rect.y + y,
            std::max(0.0F, rect.width - x * 2.0F),
            std::max(0.0F, rect.height - y * 2.0F)};
}

Rect center_rect(Rect owner, Size size) noexcept
{
    owner = normalized(owner);
    size.width = finite_nonnegative(size.width);
    size.height = finite_nonnegative(size.height);
    return {owner.x + (owner.width - size.width) * 0.5F,
            owner.y + (owner.height - size.height) * 0.5F,
            size.width, size.height};
}

EditorHomeConfirmationLayout editor_home_confirmation_layout(Rect host) noexcept
{
    host = normalized(host);
    EditorHomeConfirmationLayout result;
    result.overlay = host;
    if (!host.valid()) return result;

    result.scale = std::clamp(std::min(host.width / 960.0F,
                                       host.height / 640.0F),
                              0.70F, 1.50F);
    const float margin = 16.0F * result.scale;
    const Size panel_size{
        std::min(460.0F * result.scale, std::max(1.0F, host.width - margin * 2.0F)),
        std::min(190.0F * result.scale, std::max(1.0F, host.height - margin * 2.0F))};
    result.panel = center_rect(host, panel_size);

    const float inset_x = 28.0F * result.scale;
    result.message = {result.panel.x + inset_x,
                      result.panel.y + 26.0F * result.scale,
                      std::max(1.0F, result.panel.width - inset_x * 2.0F),
                      72.0F * result.scale};
    const float button_width = 132.0F * result.scale;
    const float button_height = 42.0F * result.scale;
    const float button_gap = 16.0F * result.scale;
    const float buttons_width = button_width * 2.0F + button_gap;
    const float button_y = result.panel.bottom() - button_height - 22.0F * result.scale;
    const float button_x = result.panel.x + (result.panel.width - buttons_width) * 0.5F;
    result.accept_button = {button_x, button_y, button_width, button_height};
    result.cancel_button = {button_x + button_width + button_gap, button_y,
                            button_width, button_height};
    return result;
}

HomeMenuLayout home_menu_layout(Rect host, Size background, Size card) noexcept
{
    host = normalized(host);
    HomeMenuLayout result;
    if (!host.valid() || !background.valid() || !card.valid()) return result;

    result.scale = std::min(host.width / background.width,
                            host.height / background.height);
    const Size fitted_background{background.width * result.scale,
                                 background.height * result.scale};
    result.background = center_rect(host, fitted_background);

    constexpr float source_gap = 14.0F;
    const float card_width = card.width * result.scale;
    const float card_height = card.height * result.scale;
    const float gap = source_gap * result.scale;
    const float row_width = card_width * static_cast<float>(result.cards.size()) +
                            gap * static_cast<float>(result.cards.size() - 1U);
    const float row_x = host.x + (host.width - row_width) * 0.5F;
    const float row_y = host.y + (host.height - card_height) * 0.5F;
    for (std::size_t index = 0; index < result.cards.size(); ++index)
        result.cards[index] = {
            row_x + static_cast<float>(index) * (card_width + gap),
            row_y, card_width, card_height};
    return result;
}

HomeMenuCardLayout home_menu_card_layout(Rect card) noexcept
{
    card = normalized(card);
    HomeMenuCardLayout result;
    if (!card.valid()) return result;
    result.scale = std::min(card.width / 80.0F, card.height / 110.0F);
    result.art = {card.x + 3.0F * result.scale,
                  card.y + 13.0F * result.scale,
                  74.0F * result.scale,
                  94.0F * result.scale};
    result.name_origin = {card.x + 5.0F * result.scale,
                          card.y + 3.0F * result.scale};
    result.name_max_width = 70.0F * result.scale;
    result.name_font_size = 12.0F * result.scale;
    return result;
}

Rect editor_content_viewport(const Rect available, const int logical_height,
                             const int left_side_space, const int right_side_space,
                             const int top_side_space, const int bottom_side_space) noexcept {
    if (!available.valid()) return {};
    // ALLの拡張として、四辺の余白を個別に設定で
    // きる。 描画と入力で共通の均一な倍率を使う。
    const float scale = available.height / static_cast<float>(
        clamp_editor_height(logical_height) + clamp_editor_side_space(top_side_space) +
        clamp_editor_side_space(bottom_side_space));
    float left = static_cast<float>(clamp_editor_side_space(left_side_space))*scale;
    float right = static_cast<float>(clamp_editor_side_space(right_side_space))*scale;
    const float horizontal = left+right;
    if (horizontal>std::max(0.0F,available.width-1.0F)) {
        const float factor=std::max(0.0F,available.width-1.0F)/horizontal;
        left*=factor; right*=factor;
    }
    const float top = static_cast<float>(clamp_editor_side_space(top_side_space))*scale;
    const float bottom = static_cast<float>(clamp_editor_side_space(bottom_side_space))*scale;
    return {available.x+left,available.y+top,
            available.width-left-right,available.height-top-bottom};
}

Size editor_logical_surface_size(const Rect available,
                                 const int logical_height,
                                 const int left_side_space,
                                 const int right_side_space,
                                 const int top_side_space,
                                 const int bottom_side_space) noexcept {
    if (!available.valid()) return {};
    const int height = clamp_editor_height(logical_height);
    const auto content = editor_content_viewport(available,height,left_side_space,
        right_side_space,top_side_space,bottom_side_space);
    const float scale = content.height / static_cast<float>(height);
    if (!std::isfinite(scale) || scale <= 0.0F) return {};
    const float logical_width = content.width / scale;
    return {std::max(1.0F, logical_width),
            static_cast<float>(height)};
}

Size status_dialog_engine_source_size(Size engine_surface) noexcept
{
    if (!engine_surface.valid()) return {};
    constexpr float logical_area_width = 632.0F;
    constexpr float logical_area_height = 420.0F;
    const float visible_height = engine_surface.width *
        logical_area_height / logical_area_width;
    return {engine_surface.width, std::min(engine_surface.height, visible_height)};
}

Size hosted_dialog_preferred_size(const Size surface,
                                  const bool debugger_descendant,
                                  const float raster_scale) noexcept
{
    if (!surface.valid()) return {};
    // Debugから開くダイアログ（終了印の編集など）は1倍
    // で描かれる場合がある。 常に2で割ると論理サイズが半分
    // になり、2倍表示されないため、実際のラスタ倍率で割る。
    const float scale = debugger_descendant
        ? (std::isfinite(raster_scale) && raster_scale >= 1.0F ? raster_scale : 1.0F)
        : 1.0F;
    return {surface.width / scale, surface.height / scale};
}

Rect clamp_dialog(Rect dialog, Rect constraint) noexcept
{
    dialog = normalized(dialog);
    constraint = normalized(constraint);
    if (!constraint.valid()) return {};
    if (dialog.width <= 0.0F || dialog.height <= 0.0F)
        return {constraint.x, constraint.y, 0.0F, 0.0F};
    if (dialog.width > constraint.width || dialog.height > constraint.height) {
        const float scale = std::min(constraint.width / dialog.width,
                                     constraint.height / dialog.height);
        dialog.width *= scale;
        dialog.height *= scale;
    }
    dialog.x = std::clamp(dialog.x, constraint.x, constraint.right() - dialog.width);
    dialog.y = std::clamp(dialog.y, constraint.y, constraint.bottom() - dialog.height);
    return dialog;
}

Rect center_engine_dialog(Size preferred, Rect engine_rect, float margin, float* scale,
                          float inherited_scale) noexcept
{
    inherited_scale = std::isfinite(inherited_scale)
        ? std::max(0.0F, inherited_scale) : 0.0F;
    const Rect available = inset(engine_rect, margin * inherited_scale);
    float used_scale = inherited_scale;
    if (!preferred.valid() || !available.valid() || used_scale <= 0.0F) {
        if (scale) *scale = 0.0F;
        return center_rect(engine_rect, {});
    }
    const Size inherited{preferred.width * used_scale,
                         preferred.height * used_scale};
    const float fit = std::min({1.0F, available.width / inherited.width,
                               available.height / inherited.height});
    used_scale *= fit;
    if (scale) *scale = used_scale;
    return center_rect(engine_rect, {preferred.width * used_scale,
                                     preferred.height * used_scale});
}

Rect native_pixel_rect(Rect rect, float scale_x, float scale_y) noexcept
{
    rect = normalized(rect);
    if (!rect.valid() || !std::isfinite(scale_x) || !std::isfinite(scale_y) ||
        scale_x <= 0.0F || scale_y <= 0.0F)
        return {};
    const auto rounded = [](const float value) {
        return std::round(value);
    };
    return {rounded(rect.x * scale_x), rounded(rect.y * scale_y),
            std::max(1.0F, rounded(rect.width * scale_x)),
            std::max(1.0F, rounded(rect.height * scale_y))};
}

DialogPlacement plan_engine_dialog_placement(
    const EngineDialogPlacementRequest& request) noexcept
{
    DialogPlacement placement;
    placement.owner_scope = DialogOwnerScope::Engine;
    placement.movable = false;

    const Rect host = normalized(request.host_rect);
    const Rect engine = normalized(request.engine_viewport);
    const Rect debugger = normalized(request.debugger_viewport);
    Size size{finite_nonnegative(request.preferred_size.width),
              finite_nonnegative(request.preferred_size.height)};
    const Rect target = request.debugger && debugger.valid() ? debugger : engine;
    if (!host.valid() || !target.valid() || !size.valid()) return placement;
    // Release版デバッガーの横幅は固定し、作業領域は単独
    // 起動時と同様に デバッガーの表示領域全体の高さを使う。
    if (request.debugger) size.height = target.height;
    // ネイティブの子ウィンドウ矩形は整数ピクセルで指定する
    // 。 SetWindowPosへ渡す前に、従来の中央配
    // 置と同じく半ピクセルの端数をゼロ方向へ切り捨てる。
    placement.rect = {target.x + std::trunc((target.width - size.width) * 0.5F),
                      target.y + std::trunc((target.height - size.height) * 0.5F),
                      size.width, size.height};
    if (!request.debugger) {
        // clamp_dialog()とは異なり、Release版のネイティブコントロ
        // ールは拡縮しない。 要求された寸法を保ち、位置だけを表示範囲内へ補正する。
        placement.rect.x = std::clamp(placement.rect.x, host.x,
                                      std::max(host.x, host.right() - size.width));
        placement.rect.y = std::clamp(placement.rect.y, host.y,
                                      std::max(host.y, host.bottom() - size.height));
    }
    return placement;
}

CoordinateTransform uniform_fit_transform(Rect destination, Size source,
                                          float maximum_scale) noexcept
{
    destination = normalized(destination);
    if (!destination.valid() || !source.valid()) return {};
    float scale = std::min(destination.width / source.width,
                           destination.height / source.height);
    if (maximum_scale > 0.0F) scale = std::min(scale, maximum_scale);
    const Rect fitted = center_rect(destination, {source.width * scale, source.height * scale});
    return {fitted, source, scale, scale};
}

EngineDebugLayout compute_engine_debug_layout(
    Rect safe, Size engine, std::optional<Size> debugger, DebugDockSide side,
    AllLayoutTheme theme, float max_upscale, std::optional<Size> engine_overlay)
{
    if (!engine.valid()) throw std::invalid_argument("Engine intrinsic size must be positive");
    if (!std::isfinite(max_upscale) || max_upscale <= 0.0F)
        throw std::invalid_argument("Engine maximum upscale must be positive");
    const Rect available = inset(safe, theme.outer_margin);
    if (!available.valid()) throw std::invalid_argument("ALL safe area is empty");

    Size engine_slot = engine;
    if (engine_overlay && engine_overlay->valid()) {
        engine_slot.width = std::max(engine_slot.width, engine_overlay->width);
        engine_slot.height = std::max(engine_slot.height, engine_overlay->height);
    }

    if (!debugger || !debugger->valid()) {
        const float scale = std::min({max_upscale, available.width / engine_slot.width,
                                      available.height / engine_slot.height});
        const Rect content_rect = center_rect(
            available, {engine.width * scale, engine.height * scale});
        return {available, available, available, content_rect, {},
                scale, 0.0F, side, false};
    }

    // ALLでは、Debugの横幅をメイン
    // 領域（Engine）の半分にし、 両
    // 者を合わせた領域を安全領域の中央へ
    // 配置する。 EngineとDebug
    // の縦横比を保ち、幅と高さの両方が収
    // まる最大の倍率を使う。 余白は全体
    // の外側へ配分し、EngineとDebug
    // の間隔はgapのみとする。
    const float gap = std::min(finite_nonnegative(theme.engine_debug_gap),
                               available.width);
    const float tiled_width = std::max(0.0F, available.width - gap);
    const float debugger_aspect = debugger->height / debugger->width;
    const float scale = std::min({max_upscale,
                                  tiled_width / (engine_slot.width * 1.5F),
                                  available.height / engine_slot.height,
                                  2.0F * available.height /
                                      (debugger_aspect * engine_slot.width)});
    const float engine_width = engine_slot.width * scale;
    const float debugger_width = engine_width * 0.5F;
    const float group_x = available.x +
        (available.width - (engine_width + gap + debugger_width)) * 0.5F;

    Rect engine_slot_rect{};
    Rect debugger_rect{};
    if (side == DebugDockSide::Right) {
        engine_slot_rect = {group_x, available.y, engine_width, available.height};
        debugger_rect = {engine_slot_rect.right() + gap, available.y,
                         debugger_width, available.height};
    } else {
        debugger_rect = {group_x, available.y, debugger_width, available.height};
        engine_slot_rect = {debugger_rect.right() + gap, available.y,
                            engine_width, available.height};
    }
    const Rect engine_rect = engine_slot_rect;
    const Rect engine_content_rect = center_rect(
        engine_slot_rect, {engine.width * scale, engine.height * scale});
    return {available, engine_slot_rect, engine_rect, engine_content_rect,
            debugger_rect, scale, gap, side, true};
}

AllLayoutSnapshot AllLayoutEngine::compute(const AllLayoutInput& input) const
{
    AllLayoutSnapshot snapshot;
    snapshot.host_rect = normalized(input.full_rect);
    if (!snapshot.host_rect.valid())
        throw std::invalid_argument("ALL viewport must be positive");
    const Rect requested_safe = normalized(
        input.safe_rect.valid() ? input.safe_rect : input.full_rect);
    snapshot.safe_rect = intersection(snapshot.host_rect, requested_safe);
    if (!snapshot.safe_rect.valid())
        throw std::invalid_argument("ALL safe area must intersect its viewport");
    // ホームとExplorerは従来の960x640の描画領域を使う。 これ
    // らはシェルのページなので、切り欠きやナビゲーションバーで偏る安全領域で
    // はなく、 ALLの物理的な表示領域全体の中央に置く。 Engineのダ
    // イアログとEditorには、下でsafe_rectの制約を適用する。
    const bool legacy_page = input.page == Page::Home || input.page == Page::Explorer;
    const Rect page_owner = legacy_page ? snapshot.host_rect : snapshot.safe_rect;
    snapshot.home_rect = page_owner;
    snapshot.page_rect = page_owner;
    // Editorは、Windowsのネイティブ子HWNDと
    // 同様にALL内を移動できる子画面とする。 論理高さを基
    // 準に倍率を決め、利用できる表示領域から論理幅を求める。
    const int editor_height = clamp_editor_height(input.editor_design_height);
    const bool editor_page = input.page == Page::Editor;
    const Size page_source = input.page == Page::Editor
        ? editor_logical_surface_size(snapshot.page_rect, editor_height,
                                      input.editor_left_side_space,
                                      input.editor_right_side_space,
                                      input.editor_top_side_space,
                                      input.editor_bottom_side_space)
        : Size{960.0F, 640.0F};
    const Size effective_page_source = page_source.valid()
        ? page_source
        : Size{static_cast<float>(kAllPcDesignWidth),
               static_cast<float>(editor_height)};
    if (editor_page)
        snapshot.page_rect = editor_content_viewport(snapshot.page_rect,editor_height,
            input.editor_left_side_space,input.editor_right_side_space,
            input.editor_top_side_space,input.editor_bottom_side_space);
    snapshot.host_to_render = uniform_fit_transform(
        snapshot.page_rect, effective_page_source,
        0.0F);

    if (input.page == Page::Engine) {
        const auto debugger = input.debugger_visible
            ? std::optional<Size>(input.debugger_intrinsic_size) : std::nullopt;
        snapshot.engine_debug = compute_engine_debug_layout(
            snapshot.safe_rect, input.engine_intrinsic_size, debugger,
            input.debug_side, input.theme, input.max_upscale);
        if (input.status_dialog_visible && !input.debugger_visible &&
            input.engine_intrinsic_size.valid()) {
            auto& layout = *snapshot.engine_debug;
            Size required = input.engine_intrinsic_size;
            // 90x620はEngineの論理サイズ。ホスト側の画像が
            // 2倍などで描画されている場合は、 パネルを同じ倍率に換
            // 算してから表示領域へ収める。 Engine本体の中央位
            // 置は保ち、ステータスダイアログによって右へずらさない。
            constexpr float logical_engine_width = 632.0F;
            constexpr float logical_panel_width = 90.0F;
            constexpr float logical_panel_height = 620.0F;
            const float backing_scale = input.engine_intrinsic_size.width /
                logical_engine_width;
            const float panel_source_width = logical_panel_width * backing_scale;
            layout.scale = std::min({input.max_upscale,
                layout.group_rect.width / (required.width + 2.0F * panel_source_width),
                layout.group_rect.height / required.height});
            const float engine_slot_width = required.width * layout.scale;
            const float engine_slot_height = required.height * layout.scale;
            const float panel_width = panel_source_width * layout.scale;
            // 狭いホストへ収める際は右側にも同じ幅を確保するが、ス
            // テータスダイアログは左側だけに置く。 パネルとEngine
            // を合わせて中央配置すると、サイズや設定の変更後も
            // Engineがパネル幅の半分だけずれてしまう。
            layout.engine_slot_rect = center_rect(layout.group_rect,
                {engine_slot_width, engine_slot_height});
            layout.engine_rect = layout.engine_slot_rect;
            layout.engine_content_rect = center_rect(layout.engine_slot_rect,
                {input.engine_intrinsic_size.width * layout.scale,
                 input.engine_intrinsic_size.height * layout.scale});
            const float logical_scale = backing_scale * layout.scale;
            const float panel_height = std::min(
                logical_panel_height * logical_scale,
                layout.engine_content_rect.height);
            layout.status_dialog_rect = {
                layout.engine_content_rect.x - panel_width,
                layout.engine_content_rect.bottom() - panel_height,
                panel_width, panel_height};
            // 右側に確保した同じ幅の領域へショートカットパネルを配置する
            // 。 サイズと上端の位置はステータスダイアログに合わせる。
            layout.shortcut_panel_rect = {
                layout.engine_content_rect.right(),
                layout.status_dialog_rect.y,
                panel_width, panel_height};
            layout.status_dialog_visible = true;
        }
    }

    snapshot.dialogs.reserve(input.dialogs.size());
    for (const auto& dialog : input.dialogs) {
        DialogPlacement placement;
        placement.owner_scope = dialog.owner_scope;
        placement.movable = dialog.movable && dialog.owner_scope == DialogOwnerScope::Host;
        if (dialog.owner_scope == DialogOwnerScope::Engine) {
            if (!snapshot.engine_debug) continue;
            Rect constraint=snapshot.safe_rect;
            // 通常のEngineダイアログにはEngineと同じ表示倍率を適用する。 1倍
            // に固定すると、拡大された画面に対して400pxの選択画面などが小さすぎる。
            const float preferred_scale=dialog.debugger_descendant
                ? kDebuggerDescendantScale : snapshot.engine_debug->scale;
            if(snapshot.engine_debug->debugger_visible || dialog.debugger_descendant) {
                // ALLでは、デバッガー表示中またはその子孫のダイアログをメイン側中央へ置く。
                // UI倍率適用後の寸法がメイン領域を超え、かつデバッガーが表示中なら、
                // ALL全体の中央へ切り替える。デバッガー非表示時はメイン領域内に収める。
                auto main_area=snapshot.engine_debug->engine_content_rect;
                main_area.height=std::min(main_area.height,main_area.width*420.0F/632.0F);
                main_area=intersection(main_area,snapshot.safe_rect);
                const bool oversized=dialog.preferred_size.width*preferred_scale>main_area.width ||
                    dialog.preferred_size.height*preferred_scale>main_area.height;
                if(!snapshot.engine_debug->debugger_visible || !oversized)
                    constraint=main_area;
            }
            placement.rect = center_engine_dialog(dialog.preferred_size,
                constraint, input.theme.dialog_margin,
                &placement.scale, preferred_scale);
            const auto translated = ::cw::dialog::translate_dialog_center(
                {placement.rect.x, placement.rect.y},
                {dialog.center_offset.x, dialog.center_offset.y}, placement.scale);
            placement.rect.x = static_cast<float>(translated.x);
            placement.rect.y = static_cast<float>(translated.y);
            // 最終配置も必ず制約内に収める。リサイズ・回転時にも毎回再計算する。
            placement.rect=clamp_dialog(placement.rect,constraint);
            placement.movable = false;
        } else {
            const Rect requested{dialog.requested_origin.x, dialog.requested_origin.y,
                                 dialog.preferred_size.width, dialog.preferred_size.height};
            if (dialog.placement == DialogPlacementPolicy::Center) {
                placement.rect = center_rect(snapshot.safe_rect, dialog.preferred_size);
                const auto translated = ::cw::dialog::translate_dialog_center(
                    {placement.rect.x, placement.rect.y},
                    {dialog.center_offset.x, dialog.center_offset.y});
                placement.rect.x = static_cast<float>(translated.x);
                placement.rect.y = static_cast<float>(translated.y);
                placement.rect = clamp_dialog(placement.rect, snapshot.safe_rect);
            } else {
                placement.rect = clamp_dialog(requested, snapshot.safe_rect);
            }
            placement.scale = dialog.preferred_size.width > 0.0F
                ? placement.rect.width / dialog.preferred_size.width : 0.0F;
        }
        snapshot.dialogs.push_back(placement);
    }
    return snapshot;
}

void EngineLayoutPreferences::set_debug_side(DebugDockSide side)
{
    if (side == debug_side_) return;
    debug_side_ = side;
    const auto listeners = listeners_;
    for (const auto& entry : listeners)
        if (entry.listener) entry.listener(side);
}

std::size_t EngineLayoutPreferences::on_debug_side_changed(Listener listener)
{
    if (!listener) return 0;
    const auto token = next_token_++;
    listeners_.push_back({token, std::move(listener)});
    return token;
}

void EngineLayoutPreferences::remove_listener(std::size_t token) noexcept
{
    listeners_.erase(std::remove_if(listeners_.begin(), listeners_.end(),
        [token](const Entry& entry) { return entry.token == token; }), listeners_.end());
}

}  // namespace cardwirth::app::all
