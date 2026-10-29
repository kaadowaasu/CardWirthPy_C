#include "cw/dialog/cardreorder.hpp"

#include "cw/header.hpp"
#include "cw/image.hpp"
#include "cw/lib/ui_surface.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <utility>

namespace cw::dialog::cardreorder {
namespace {

using Integer = ::cw::lib::Integer;
using Action = ::cw::dialog::DialogPresentationAction;

// 論理配置は 630×380 のカード領域に、カードダイアログ共通
// の 30px の下部バーを加える。 5 行×10 列で、各カー
// ドは縦横比を維持しながら幅 60、高さ 75 以下に拡縮する。
constexpr Integer kWidth = 630;
constexpr Integer kGridHeight = 380;
constexpr Integer kBarHeight = 30;
constexpr Integer kColumns = 10;
constexpr Integer kRows = 5;
constexpr Integer kCellWidth = kWidth / kColumns;       // 63
constexpr Integer kCellHeight = kGridHeight / kRows;    // 76
constexpr Integer kThumbWidth = 60;
constexpr Integer kThumbHeight = 75;
constexpr int kAccept = 0;
constexpr int kCancel = 1;
constexpr int kToggleMode = 2;

void mix(std::uint64_t& value, std::uint64_t part)
{
    value ^= part + 0x9e3779b97f4a7c15ULL + (value << 6U) + (value >> 2U);
}

}  // namespace

Integer CardReorderDialog::px(const Integer value) const
{
    return static_cast<Integer>(std::lround(static_cast<double>(value) * m_scale));
}

void CardReorderDialog::initialize(std::vector<ReorderCard> cards, const ::cw::lib::Color background,
                                   const ::cw::lib::Real scale)
{
    m_scale = std::isfinite(scale) && scale > 0.0 ? scale : 1.0;
    m_background = background;
    m_entries.clear();
    if (cards.size() > kMaxCards) cards.resize(kMaxCards);
    for (auto& card : cards) {
        std::shared_ptr<::cw::lib::Surface> thumbnail;
        if (card.image && card.image->size.width > 0 && card.image->size.height > 0) {
            const double fit = std::min(static_cast<double>(px(kThumbWidth)) / card.image->size.width,
                                        static_cast<double>(px(kThumbHeight)) / card.image->size.height);
            const Integer width = std::max<Integer>(1, std::lround(card.image->size.width * fit));
            const Integer height = std::max<Integer>(1, std::lround(card.image->size.height * fit));
            thumbnail = ::cw::image::smoothscale_card(card.image, {width, height}, true);
        }
        m_entries.push_back({std::move(card.header), std::move(thumbnail)});
    }
    title = "順番入替";
    visible = true;
    m_finished = false;
    m_accepted = false;
    m_drag.reset();
    request_dialog_presentation_refresh();
}

std::vector<std::shared_ptr<::cw::header::CardHeader>> CardReorderDialog::order() const
{
    std::vector<std::shared_ptr<::cw::header::CardHeader>> result;
    result.reserve(m_entries.size());
    for (const auto& entry : m_entries) result.push_back(entry.header);
    return result;
}

std::size_t CardReorderDialog::slot_at(const float x, const float y) const
{
    if (m_entries.empty()) return 0;
    const auto column = std::clamp<Integer>(
        static_cast<Integer>(std::floor(x / std::max<Integer>(1, px(kCellWidth)))), 0, kColumns - 1);
    const auto row = std::clamp<Integer>(
        static_cast<Integer>(std::floor(y / std::max<Integer>(1, px(kCellHeight)))), 0, kRows - 1);
    return std::min<std::size_t>(static_cast<std::size_t>(row * kColumns + column), m_entries.size() - 1);
}

std::vector<std::size_t> CardReorderDialog::display_order() const
{
    std::vector<std::size_t> order(m_entries.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    if (m_drag && *m_drag < order.size()) {
        if (m_insert_mode) {
            order.erase(order.begin() + static_cast<std::ptrdiff_t>(*m_drag));
            order.insert(order.begin() + static_cast<std::ptrdiff_t>(std::min(m_target, order.size())), *m_drag);
        } else if (m_target < order.size()) {
            std::swap(order[*m_drag], order[m_target]);
        }
    }
    return order;
}

::cw::dialog::DialogPresentation CardReorderDialog::dialog_presentation()
{
    namespace ui = ::cw::lib::ui;
    const Integer width = px(kWidth), grid_height = px(kGridHeight);
    const Integer height = px(kGridHeight + kBarHeight);
    auto& retained = dialog_retained_surface();
    if (retained.needs_full({width, height})) {
        retained.allocate({width, height}, m_background);
        ui::fill_rect(retained.surface, {0, grid_height, width, height - grid_height}, {242, 242, 242, 255});
        retained.capture_base();
    }
    const auto order = display_order();
    std::uint64_t grid_signature = std::hash<std::size_t>{}(order.size());
    for (const auto index : order) mix(grid_signature, index);
    mix(grid_signature, m_drag ? *m_drag + 1 : 0);
    if (m_drag) {
        mix(grid_signature, static_cast<std::uint64_t>(std::lround(m_pointer_x)));
        mix(grid_signature, static_cast<std::uint64_t>(std::lround(m_pointer_y)));
    }
    const bool paint_grid = retained.component_changed("reorder.grid", {0, 0, width, grid_height}, grid_signature);
    const bool paint_bar = retained.component_changed("reorder.bar", {0, grid_height, width, height - grid_height},
                                                      static_cast<std::uint64_t>(m_finished) |
                                                      (static_cast<std::uint64_t>(m_insert_mode) << 1U));
    retained.restore_dirty();
    auto surface = retained.surface;

    ::cw::dialog::DialogPresentation result;
    result.route = "CARDREORDER";
    result.title = title;

    const auto slot_rect = [&](std::size_t slot) {
        const auto column = static_cast<Integer>(slot) % kColumns;
        const auto row = static_cast<Integer>(slot) / kColumns;
        return ::cw::lib::Rect{px(column * kCellWidth), px(row * kCellHeight), px(kCellWidth), px(kCellHeight)};
    };
    const auto draw_thumbnail = [&](const Entry& entry, Integer center_x, Integer center_y) {
        if (!entry.thumbnail) return;
        ui::blend_surface(surface, entry.thumbnail, center_x - entry.thumbnail->size.width / 2,
                          center_y - entry.thumbnail->size.height / 2);
    };
    for (std::size_t slot = 0; slot < order.size(); ++slot) {
        const auto rect = slot_rect(slot);
        const auto index = order[slot];
        if (paint_grid) {
            if (m_drag && index == *m_drag) {
                // ドロップ先は枠線付きの空き枠で示す。
                const ::cw::lib::Color edge{230, 230, 230, 255};
                const auto inset = px(3), line = std::max<Integer>(1, px(1));
                const ::cw::lib::Rect box{rect.x + inset, rect.y + inset, rect.width - inset * 2, rect.height - inset * 2};
                ui::fill_rect(surface, {box.x, box.y, box.width, line}, edge);
                ui::fill_rect(surface, {box.x, box.y + box.height - line, box.width, line}, edge);
                ui::fill_rect(surface, {box.x, box.y, line, box.height}, edge);
                ui::fill_rect(surface, {box.x + box.width - line, box.y, line, box.height}, edge);
            } else {
                draw_thumbnail(m_entries[index], rect.x + rect.width / 2, rect.y + rect.height / 2);
            }
        }
        // カードごとに連続したポインター操作を一つ扱う。押下でドラッ
        // グを開始し、 共通キャプチャーが解除まで移動を送り続ける。
        ::cw::dialog::DialogPresentationControl control{Action::Select, static_cast<int>(slot),
            static_cast<float>(rect.x), static_cast<float>(rect.y),
            static_cast<float>(rect.width), static_cast<float>(rect.height), !m_finished, true};
        result.controls.push_back(control);
    }
    if (paint_grid && m_drag && *m_drag < m_entries.size()) {
        // ドラッグ中のカードはポインターに追従し、カード領域内に収
        // める。 ドラッグ中に再描画するのはこの領域だけとする。
        const auto& dragged = m_entries[*m_drag];
        const Integer half_w = dragged.thumbnail ? dragged.thumbnail->size.width / 2 : 0;
        const Integer half_h = dragged.thumbnail ? dragged.thumbnail->size.height / 2 : 0;
        draw_thumbnail(dragged,
            std::clamp<Integer>(static_cast<Integer>(std::lround(m_pointer_x)), half_w, std::max(half_w, width - half_w)),
            std::clamp<Integer>(static_cast<Integer>(std::lround(m_pointer_y)), half_h,
                                std::max(half_h, grid_height - half_h)));
    }

    const auto font = ui::configured_font_scaled("button", 14);
    const auto button = [&](const ::cw::lib::String& label, Integer x, Action action, int index) {
        const ::cw::lib::Rect rect{x, grid_height + px(3), px(90), px(24)};
        if (paint_bar) {
            ui::draw_button_face(surface, rect, {!m_finished, false, action == Action::Accept});
            if (const auto glyph = ui::render_label(label, font.pixels, font.bold, {0, 0, 0}, font.face, font.italic))
                ui::blend_surface(surface, glyph, rect.x + (rect.width - glyph->size.width) / 2,
                                  rect.y + (rect.height - glyph->size.height) / 2);
        }
        result.controls.push_back({action, index, static_cast<float>(rect.x), static_cast<float>(rect.y),
                                   static_cast<float>(rect.width), static_cast<float>(rect.height), !m_finished});
        result.controls.back().hover_highlight = true;
    };
    button("OK", width / 2 - px(95), Action::Accept, kAccept);
    button("キャンセル", width / 2 + px(5), Action::Cancel, kCancel);
    // 下部バーの右端に現在のドラッグモードを表示し、押すと切り替える。
    button(m_insert_mode ? "差し込み" : "入れ替え", width - px(95), Action::Auxiliary, kToggleMode);

    retained.finish();
    result.surface = surface;
    result.damage = retained.damage;
    result.surface_revision = retained.revision;
    return result;
}

void CardReorderDialog::dialog_presentation_pointer(const Action action, const int index,
                                                    const float x, const float y, const bool pressed)
{
    if (m_finished || action != Action::Select) return;
    if (pressed) {
        if (!m_drag) {
            if (index < 0 || static_cast<std::size_t>(index) >= m_entries.size()) return;
            m_drag = static_cast<std::size_t>(index);
            m_target = *m_drag;
            m_moved = false;
        } else {
            const auto target = slot_at(x, y);
            if (target != m_target) m_moved = true;
            m_target = target;
        }
        m_pointer_x = x;
        m_pointer_y = y;
        request_dialog_presentation_refresh();
        return;
    }
    if (!m_drag) return;
    const auto source = *m_drag;
    const auto target = slot_at(x, y);
    m_drag.reset();
    if (source < m_entries.size() && target != source && target < m_entries.size()) {
        if (m_insert_mode) {
            auto entry = std::move(m_entries[source]);
            m_entries.erase(m_entries.begin() + static_cast<std::ptrdiff_t>(source));
            m_entries.insert(m_entries.begin() + static_cast<std::ptrdiff_t>(std::min(target, m_entries.size())),
                             std::move(entry));
        } else {
            std::swap(m_entries[source], m_entries[target]);
        }
    }
    request_dialog_presentation_refresh();
}

void CardReorderDialog::dialog_presentation_pointer_cancel()
{
    if (!m_drag) return;
    m_drag.reset();
    request_dialog_presentation_refresh();
}

void CardReorderDialog::dialog_presentation_action(const Action action, const int index)
{
    if (m_finished) return;
    if (action == Action::Accept) finish(true);
    else if (action == Action::Cancel) finish(false);
    else if (action == Action::Auxiliary && index == kToggleMode) {
        m_insert_mode = !m_insert_mode;
        request_dialog_presentation_refresh();
    }
}

void CardReorderDialog::dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers)
{
    (void)modifiers;
    if (!m_finished && !m_drag) finish(true);
}

void CardReorderDialog::finish(const bool accept)
{
    if (m_finished) return;
    m_drag.reset();
    m_accepted = accept;
    m_finished = true;
    visible = false;
    request_dialog_presentation_refresh();
}

}  // namespace cw::dialog::cardreorder
