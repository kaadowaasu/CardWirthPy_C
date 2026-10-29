#include "cwxeditor_src/gui/cwx/editor/gui/dwt/radarspinner.hpp"
#include "cwxeditor_src/core/cwx/perf.hpp"
#include "cwxeditor_src/lib/ui_metrics.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <limits>

namespace cwxeditor::cwx::editor::gui::dwt::radarspinner {
namespace {

constexpr double PI = 3.141592653589793238462643383279502884;

int rounded(const double value) {
    return static_cast<int>(std::lround(value));
}

int toInt(const float value) {
    return rounded(static_cast<double>(value));
}

int clampValue(const int value, const int minimum, const int maximum) {
    return std::clamp(value, minimum, std::max(minimum, maximum));
}

bool sameFontSpec(const std::optional<lib::FontSpec>& left,
                  const std::optional<lib::FontSpec>& right) noexcept {
    // D版は Font オブジェクトの同一性を比較する。 共通のC++モデルはフ
    // ォントを値で表すため、同じ FontSpec を同じフォントとして扱う。
    if (left.has_value() != right.has_value()) return false;
    if (!left) return true;
    return left->family == right->family && left->points == right->points &&
           left->weight == right->weight && left->italic == right->italic &&
           left->underline == right->underline &&
           left->strikeout == right->strikeout &&
           left->antialias == right->antialias &&
           left->content_font == right->content_font &&
           left->decorations_in_policy == right->decorations_in_policy &&
           left->size_mode == right->size_mode;
}

int rangeMaximum(const int minimum, const std::uint32_t steps) {
    const auto upper = static_cast<std::int64_t>(minimum) + (steps == 0 ? 0 : steps - 1);
    return static_cast<int>(std::min(upper, static_cast<std::int64_t>(std::numeric_limits<int>::max())));
}

std::size_t pointIndex(const int value, const int minimum,
                       const std::uint32_t step) {
    if (step == 0U) return 0U;
    const auto clamped = clampValue(value, minimum,
                                    rangeMaximum(minimum, step));
    return static_cast<std::size_t>(static_cast<std::int64_t>(clamped) - minimum);
}

}  // namespace

RadarSpinner::RadarSpinner(std::shared_ptr<lib::Container> parent,
                           const std::uint32_t style,
                           std::shared_ptr<lib::GuiBackend> gui)
    : gui_(std::move(gui)), read_only_((style & ReadOnly) != 0U), parent_(std::move(parent)) {
    accessibility_name = "RadarSpinner";
    set_layout(std::make_shared<lib::AbsoluteLayout>());
    canvas_ = std::make_shared<lib::Canvas>();
    canvas_->double_buffered = true;
    canvas_->set_layout_item({.horizontal = lib::Align::Stretch,
                              .vertical = lib::Align::Stretch,
                              .grow = 1.0F});
    const std::weak_ptr<int> lifetime = callback_lifetime_;
    canvas_->on_paint_context = [this, lifetime](lib::DrawingContext& context, lib::Rect area) {
        if (lifetime.expired()) return;
        paint(context, area);
    };
    add(canvas_);
    set_child_layout_provider([this](const lib::Rect bounds) {
        return prepareChildLayout(bounds);
    });
    mod_redraw_ = [this] { notifyModified(); };
    installPointerHandlers();
    setRadarColor(oval_foreground_, oval_background_);
}

void RadarSpinner::SpnListener::handleEvent(const int value) {
    if (owner_) owner_->setValueFromSpinner(static_cast<std::size_t>(index_), value);
}

void RadarSpinner::installPointerHandlers() {
    const std::weak_ptr<int> lifetime = callback_lifetime_;
    on_pointer = [this, lifetime](lib::PointerEvent& event) {
        if (lifetime.expired()) return;
        if (event.action == lib::PointerAction::Cancel || read_only_ || !enabled() || !visible()) {
            on_drag_ = -1;
            set_cursor(lib::CursorKind::Arrow);
            return;
        }
        // 表示側のポインター座標はウィンドウ相対だが、レーダーの形状はコントロ
        // ール内の座標。 D版の Canvas のマウス座標と同じ基準にする。
        const auto origin = bounds();
        const auto x = event.position.x - origin.x;
        const auto y = event.position.y - origin.y;
        if (event.action == lib::PointerAction::Down &&
            event.button == lib::PointerButton::Primary) {
            on_drag_ = cursorFromPos(x, y);
            if (on_drag_ >= 0) {
                set_cursor(lib::CursorKind::Crosshair);
                event.handled = true;
                // D版の RadarSpinner と同様、クリックした軸にキーボードフォーカスを移す。
                const auto index = static_cast<std::size_t>(on_drag_);
                const auto target = index < spinners_.size() ? spinners_[index] : lib::WidgetPtr{};
                if (const auto gui = gui_.lock(); gui && target) gui->focus(target);
                // フォーカスのコールバックがこのウィジェットを破棄する場合があるため、その後はメンバーを参照しない。
            }
            return;
        }
        if (event.action == lib::PointerAction::Move) {
            if (on_drag_ >= 0 && static_cast<std::size_t>(on_drag_) < parameter_count_) {
                const auto index = static_cast<std::size_t>(on_drag_);
                const auto& points = toggles_[index];
                if (!points.empty()) {
                    std::size_t nearest = 0U;
                    float distance = std::numeric_limits<float>::max();
                    for (std::size_t point = 0; point < points.size(); ++point) {
                        const auto current = std::abs(points[point].x - x) +
                                             std::abs(points[point].y - y);
                        if (current <= distance) {
                            distance = current;
                            nearest = point;
                        }
                    }
                    setValue(index, minimum_ + static_cast<int>(nearest));
                    notifyModified();
                    event.handled = true;
                }
            } else {
                checkCursor(x, y);
            }
            return;
        }
        if (event.action == lib::PointerAction::Up &&
            event.button == lib::PointerButton::Primary) {
            on_drag_ = -1;
            checkCursor(x, y);
            event.handled = true;
        }
    };
    if (canvas_) canvas_->on_pointer = on_pointer;
    on_blur = [this, lifetime] { if (lifetime.expired()) return; on_drag_ = -1; set_cursor(lib::CursorKind::Arrow); };
    if (canvas_) canvas_->on_blur = on_blur;
}

int RadarSpinner::toggleSize() { return toInt(lib::scaled_ui_metric(TOGGLE_SIZE)); }
int RadarSpinner::toggleCatchSize() { return toInt(lib::scaled_ui_metric(TOGGLE_CATCH_SIZE)); }
int RadarSpinner::figureMargin() { return toInt(lib::scaled_ui_metric(MARGIN)); }

void RadarSpinner::notifyModified() {
    modified_ = true;
    for (const auto& action : modEvent) {
        if (action) action();
    }
    redraw();
}

void RadarSpinner::updateRenderState() {
    // 三角形と通常のレーダー配置で、同じ完全な状態を反映する。
    // 三角形だけ早期に戻ると、軸が欠けたり古いまま残ったりする。
    render_state_.ovals = ovals_;
    render_state_.polygons.clear();
    for (const auto& polygon : polygons_) {
        std::vector<lib::Point> points;
        for (std::size_t index = 0; index + 1U < polygon.size(); index += 2U)
            points.push_back({static_cast<float>(polygon[index]),
                              static_cast<float>(polygon[index + 1U])});
        render_state_.polygons.push_back(std::move(points));
    }
    render_state_.axes.clear();
    for (const auto& points : toggles_)
        if (points.size() >= 2U) render_state_.axes.emplace_back(points.front(),points.back());
    render_state_.activePolygon.clear();
    render_state_.markers.clear();
    for (std::size_t index = 0; index < parameter_count_; ++index) {
        const auto value = pointIndex(getValue(index), minimum_, step_count_);
        if (index >= toggles_.size() || value >= toggles_[index].size()) continue;
        render_state_.activePolygon.push_back(toggles_[index][value]);
        if (!read_only_)
            render_state_.markers.push_back({toggles_[index][value], toggle_style_});
    }
    render_state_.foreground = oval_foreground_;
    render_state_.background = oval_background_;
    render_state_.alpha = alpha_;
    render_state_.antialias = antialias_;
}

void RadarSpinner::redraw() {
    updateRenderState();
    if (!canvas_) return;
    canvas_->accessibility_description =
        "radar points: " + std::to_string(parameter_count_);
    if (canvas_->invalidate) canvas_->invalidate();
}

void RadarSpinner::paint(lib::DrawingContext& context, const lib::Rect area) {
    context.set_antialias(render_state_.antialias != 0);
    // 形状と子の境界は、共通の配置処理で準備して一括反映済み
    // 。 描画は、その変更されない結果を再生するだけにする。
    context.save();
    context.clip(area);
    const auto control_background = background().value_or(
        parent_ ? parent_->background().value_or(lib::Color{240,240,240,255})
                : lib::Color{240,240,240,255});
    context.clear(control_background);
    const auto outer_foreground=foreground().value_or(lib::Color{0,0,0,255});
    // 強調するのは現在値だけとする。 D版の背景グリ
    // ッドと外枠は変えず、データより目立たせない。
    constexpr float outer_width=2.0F;
    constexpr float grid_width=1.0F;
    // 仕様変更(D版と異なる): 現在値を結ぶ線を見やすくするため +2px (1 -> 3)。
    // ALL・単体エディタ共通。使用/所持ボーナス、キャストの能力値など、
    // RadarSpinner を使う全てのレーダー表示に適用される。
    constexpr float value_width=3.0F;
    const auto is_borderline = [this](const std::size_t index) {
        const auto value = static_cast<int>(step_count_) + minimum_ - 1 -
                           static_cast<int>(index);
        return isBorderline(value);
    };
    const auto draw_oval = [&](const std::size_t index,
                               const lib::StrokeStyle style) {
        if (index >= render_state_.ovals.size()) return;
        context.draw_ellipse(render_state_.ovals[index], render_state_.foreground,
                             grid_width, style);
    };
    const auto draw_polygon = [&](const std::size_t index,
                                  const lib::StrokeStyle style) {
        if (index >= render_state_.polygons.size()) return;
        context.draw_polygon(render_state_.polygons[index], render_state_.foreground,
                             grid_width, style);
    };
    if (!render_state_.ovals.empty()) {
        context.fill_ellipse(render_state_.ovals.front(), render_state_.background);
        context.draw_ellipse(render_state_.ovals.front(), outer_foreground, outer_width,
                             lib::StrokeStyle::Solid);
        for (std::size_t index = oval_step_; index < render_state_.ovals.size();
             index += oval_step_) {
            if (!is_borderline(index)) draw_oval(index, lib::StrokeStyle::Dash);
        }
        if ((render_state_.ovals.size() - 1U) % oval_step_ != 0U &&
            !is_borderline(render_state_.ovals.size() - 1U))
            draw_oval(render_state_.ovals.size() - 1U, lib::StrokeStyle::Dash);
        for (const auto value : borderlines_) {
            const auto index = static_cast<int>(render_state_.ovals.size()) +
                               minimum_ - 1 - value;
            if (index >= 0) draw_oval(static_cast<std::size_t>(index),
                                      lib::StrokeStyle::Solid);
        }
    }
    if (!render_state_.polygons.empty()) {
        context.fill_polygon(render_state_.polygons.front(), render_state_.background);
        context.draw_polygon(render_state_.polygons.front(), outer_foreground, outer_width,
                             lib::StrokeStyle::Solid);
        for (std::size_t index = oval_step_; index < render_state_.polygons.size();
             index += oval_step_) {
            if (!is_borderline(index)) draw_polygon(index, lib::StrokeStyle::Dash);
        }
        if ((render_state_.polygons.size() - 1U) % oval_step_ != 0U &&
            !is_borderline(render_state_.polygons.size() - 1U))
            draw_polygon(render_state_.polygons.size() - 1U,
                         lib::StrokeStyle::Dash);
        for (const auto value : borderlines_) {
            const auto index = static_cast<int>(render_state_.polygons.size()) +
                               minimum_ - 1 - value;
            if (index >= 0) draw_polygon(static_cast<std::size_t>(index),
                                         lib::StrokeStyle::Solid);
        }
    }
    for (const auto& axis : render_state_.axes)
        context.draw_line(axis.first, axis.second, render_state_.foreground,
                          grid_width, lib::StrokeStyle::Dash);
    if (render_state_.activePolygon.size() >= 3U) {
        // D版と同様、一覧の背景上に半透明（159）の値の領域を描く。
        if (render_state_.alpha > 0) {
            auto fill = render_state_.background;
            fill.alpha = static_cast<std::uint8_t>(std::clamp(render_state_.alpha,0,255));
            context.fill_polygon(render_state_.activePolygon, fill);
        }
        context.draw_polygon(render_state_.activePolygon, render_state_.foreground,
                             value_width, lib::StrokeStyle::Solid);
    }
    const float half = static_cast<float>(toggleSize() / 2);
    // D版と同様、小さいインデックスを最後に描き、重なった部分の当たり判定と描画の優先順を保つ。
    for (auto it=render_state_.markers.rbegin(); it!=render_state_.markers.rend(); ++it) {
        const auto& marker=*it;
        const lib::Rect rect{marker.center.x-half,marker.center.y-half,
                             static_cast<float>(toggleSize()),static_cast<float>(toggleSize())};
        switch (marker.style) {
        case Toggle::SQUARE:
            context.fill_rect(rect,render_state_.background);
            context.draw_rect(rect,render_state_.foreground,1,lib::StrokeStyle::Solid);
            break;
        case Toggle::OVAL:
            context.fill_ellipse(rect,render_state_.background);
            context.draw_ellipse(rect,render_state_.foreground,1,lib::StrokeStyle::Solid);
            break;
        case Toggle::OBLIQUE_SQUARE: {
            const float radius=half+1;
            const std::array<lib::Point,4> diamond{{
                {marker.center.x-radius,marker.center.y},
                {marker.center.x,marker.center.y-radius},
                {marker.center.x+radius,marker.center.y},
                {marker.center.x,marker.center.y+radius}}};
            context.fill_polygon(diamond,render_state_.background);
            context.draw_polygon(diamond,render_state_.foreground,1,lib::StrokeStyle::Solid);
            break;
        }
        case Toggle::CROSS: {
            const float radius=half+2;
            const float x=marker.center.x,y=marker.center.y;
            const std::array<lib::Point,12> cross{{
                {x-radius,y-1},{x-1,y-1},{x-1,y-radius},{x+1,y-radius},
                {x+1,y-1},{x+radius,y-1},{x+radius,y+1},{x+1,y+1},
                {x+1,y+radius},{x-1,y+radius},{x-1,y+1},{x-radius,y+1}}};
            context.fill_polygon(cross,render_state_.background);
            context.draw_polygon(cross,render_state_.foreground,1,lib::StrokeStyle::Solid);
            break;
        }
        case Toggle::NOTHING: break;
        }
    }
    context.restore();
}

void RadarSpinner::setRadar(std::uint32_t step,
                            const std::vector<String>& names,
                            const int minimum) {
    ::cwxeditor::cwx::perf::TraceScope trace_setRadar(__FILE__, __LINE__);
    spinner_lifetime_ = std::make_shared<int>(0);
    const std::weak_ptr<int> generation = spinner_lifetime_;
    on_drag_ = -1;
    set_cursor(lib::CursorKind::Arrow);
    clear();
    if (canvas_) add(canvas_);
    step_count_ = step;
    names_ = names;
    parameter_count_ = names_.size();
    minimum_ = minimum;
    // 三軸のボーナス図では、対称な -N～N の全値を
    // 三角形の補助線で描くと読みにくい。 大きい能力レ
    // ーダーと同じ、およそ五区間の表示にする。 呼び
    // 出し元は lineStep() で変更できる。
    oval_step_ = parameter_count_ == 3U
        ? std::max(1U, (step_count_ > 0U ? step_count_ - 1U : 0U) / 4U)
        : 1U;
    values_.assign(parameter_count_, minimum_);
    borderlines_.clear();
    components_.clear();
    labels_.clear();
    spinners_.clear();
    listeners_.clear();

    components_.reserve(parameter_count_);
    labels_.reserve(parameter_count_);
    spinners_.reserve(parameter_count_);
    listeners_.reserve(parameter_count_);
    for (std::size_t index = 0; index < parameter_count_; ++index) {
        auto component = std::make_shared<lib::Panel>();
        auto component_layout = std::make_shared<lib::LinearLayout>(
            lib::Orientation::Vertical);
        component_layout->spacing = 2.0F;
        component_layout->padding = {};
        component_layout->cross_axis = lib::Align::Center;
        component->set_layout(component_layout);
        auto label = std::make_shared<lib::Label>(names_[index]);
        label->alignment = lib::TextAlignment::Center;
        label->set_layout_item({.horizontal = lib::Align::Center,
                                .vertical = lib::Align::Center});
        component->add(label);
        labels_.push_back(label);

        if (read_only_) {
            auto value = std::make_shared<lib::Label>(std::to_string(minimum_));
            value->alignment = lib::TextAlignment::Center;
            value->set_tooltip("Radar value");
            value->set_layout_item({.horizontal = lib::Align::Center,
                                    .vertical = lib::Align::Center});
            component->add(value);
            spinners_.push_back(value);
        } else {
            auto value = std::make_shared<lib::NumberEdit>();
            value->minimum = static_cast<double>(minimum_);
            value->maximum = static_cast<double>(rangeMaximum(minimum_, step_count_));
            value->value = static_cast<double>(minimum_);
            value->step = 1.0;
            value->set_layout_item({.horizontal = lib::Align::Center,
                                    .vertical = lib::Align::Center,
                                    .minimum = {44.0F, 22.0F},
                                    .preferred = {44.0F, 22.0F}});
            auto listener = std::make_unique<SpnListener>(
                *this, static_cast<int>(index));
            auto* listenerRaw = listener.get();
            value->on_changed = [listenerRaw, generation](double changed) {
                if (generation.expired()) return;
                listenerRaw->handleEvent(rounded(changed));
            };
            listeners_.push_back(std::move(listener));
            component->add(value);
            spinners_.push_back(value);
        }
        components_.push_back(component);
        add(component);
    }

    toggles_.assign(parameter_count_, std::vector<lib::Point>(step_count_));
    ovals_.clear();
    polygons_.clear();
    modified_ = true;
    calcMaxSize();
    redraw();
}

void RadarSpinner::checkCursor(const float x, const float y) {
    if (read_only_) return;
    set_cursor(cursorFromPos(x, y) >= 0 ? lib::CursorKind::Crosshair
                                       : lib::CursorKind::Default);
}

std::ptrdiff_t RadarSpinner::cursorFromPos(const float x, const float y) const {
    if (read_only_) return -1;
    float minimumDistance = std::numeric_limits<float>::max();
    std::ptrdiff_t index = -1;
    for (std::size_t parameter = 0; parameter < parameter_count_; ++parameter) {
        const auto value = getValue(parameter);
        const auto point = pointIndex(value, minimum_, step_count_);
        if (parameter >= toggles_.size() || point >= toggles_[parameter].size()) continue;
        const auto& toggle = toggles_[parameter][point];
        const auto tx = toggle.x - static_cast<float>(toggleCatchSize()) / 2.0F;
        const auto ty = toggle.y - static_cast<float>(toggleCatchSize()) / 2.0F;
        if (x < tx || x > tx + toggleCatchSize() ||
            y < ty || y > ty + toggleCatchSize()) continue;
        const auto distance = std::abs(toggle.x - x) + std::abs(toggle.y - y);
        if (distance < minimumDistance) {
            minimumDistance = distance;
            index = static_cast<std::ptrdiff_t>(parameter);
        }
    }
    return index;
}

unsigned RadarSpinner::figure(const int value, const int minus_mark_length,
                              const int radix) {
    ::cwxeditor::cwx::perf::TraceScope trace_figure(__FILE__, __LINE__);
    if (value == 0) return 1U;
    std::int64_t number = value;
    const bool negative = number < 0;
    if (negative) number = -number;
    unsigned result = 0U;
    std::int64_t unit = 1;
    while (number >= unit) {
        unit *= radix;
        ++result;
    }
    if (negative) result += static_cast<unsigned>(minus_mark_length);
    return result;
}

bool RadarSpinner::__unittest_L385_C4() const {
    return figure(100, 1, 10) == 3U && figure(99, 1, 10) == 2U &&
           figure(123, 1, 10) == 3U && figure(0x0F, 1, 16) == 1U &&
           figure(-123, 1, 10) == 4U && figure(-5555, 2, 10) == 6U;
}

void RadarSpinner::calcMaxSize() {
    ::cwxeditor::cwx::perf::TraceScope trace_calcMaxSize(__FILE__, __LINE__);
    max_size_ = {};
    for (std::size_t index = 0; index < labels_.size(); ++index) {
        const auto caption = lib::preferred_ui_size(labels_[index]);
        const auto editor = lib::preferred_ui_size(spinners_[index]);
        max_size_.width = std::max(max_size_.width,
                                  std::max(caption.width, editor.width));
        max_size_.height = std::max(max_size_.height,
            caption.height + lib::scaled_ui_metric(2.0F) + editor.height);
    }
    if (parameter_count_ == 0U) {
        max_size_.width = 0.0F;
        max_size_.height = 0.0F;
    }
}

double RadarSpinner::nPos(const std::size_t index) const {
    ::cwxeditor::cwx::perf::TraceScope trace_nPos(__FILE__, __LINE__);
    if (side_) {
        return static_cast<double>(index) -
               static_cast<double>(parameter_count_) / 4.0 - 0.5;
    }
    return static_cast<double>(index) -
           static_cast<double>(parameter_count_) / 4.0;
}

lib::Rect RadarSpinner::computeBounds(const int width, const int height) const {
    ::cwxeditor::cwx::perf::TraceScope trace_computeBounds(__FILE__, __LINE__);
    if (parameter_count_ == 0U) return {};
    int minLeft = std::numeric_limits<int>::max();
    int minTop = std::numeric_limits<int>::max();
    int maxRight = std::numeric_limits<int>::min();
    int maxBottom = std::numeric_limits<int>::min();
    const auto spWidth = toInt(max_size_.width);
    const auto spHeight = toInt(max_size_.height);
    const auto ovalX = width - spWidth;
    const auto ovalY = height - spHeight;
    for (std::size_t index = 0; index < parameter_count_; ++index) {
        const auto position = nPos(index);
        const auto rw = static_cast<double>(ovalX) / 2.0;
        const auto rh = static_cast<double>(ovalY) / 2.0;
        const auto px = static_cast<double>(spWidth) / 2.0 + rw +
                        rw * std::cos(PI * 2.0 * position / parameter_count_);
        const auto py = static_cast<double>(spHeight) / 2.0 + rh +
                        rh * std::sin(PI * 2.0 * position / parameter_count_);
        const auto left = rounded(px - static_cast<double>(spWidth) / 2.0);
        const auto top = rounded(py - static_cast<double>(spHeight) / 2.0);
        minLeft = std::min(minLeft, left);
        minTop = std::min(minTop, top);
        maxRight = std::max(maxRight, left + spWidth);
        maxBottom = std::max(maxBottom, top + spHeight);
    }

    if (oval_) {
        auto ovalWidth = width - toggleSize() - figureMargin() * 2 - spWidth * 2 + 5;
        auto ovalHeight = height - toggleSize() - figureMargin() * 2 - spHeight * 2 + 5;
        if (minLeft == 0) ovalWidth += figureMargin() + spWidth;
        else if (maxRight == width) {
            ovalWidth += figureMargin() + spWidth;
            if (maxRight - minLeft < ovalWidth)
                minLeft += maxRight - minLeft - ovalWidth;
        } else if (maxRight - minLeft < ovalWidth) {
            minLeft = figureMargin() + spWidth;
        }
        if (minTop == 0) ovalHeight += figureMargin() + spHeight;
        else if (maxBottom == height) {
            ovalHeight += figureMargin() + spHeight;
            if (maxBottom - minTop < ovalHeight)
                minTop += maxBottom - minTop - ovalHeight;
        } else if (maxBottom - minTop < ovalHeight) {
            minTop = figureMargin() + spHeight;
        }
        const auto resultWidth = std::max(maxRight - minLeft, ovalWidth);
        const auto resultHeight = std::max(maxBottom - minTop, ovalHeight);
        return {static_cast<float>(minLeft), static_cast<float>(minTop),
                static_cast<float>(resultWidth), static_cast<float>(resultHeight)};
    }
    return {static_cast<float>(minLeft), static_cast<float>(minTop),
            static_cast<float>(maxRight - minLeft),
            static_cast<float>(maxBottom - minTop)};
}

std::vector<lib::PreparedChildBounds> RadarSpinner::prepareChildLayout(
    const lib::Rect current) {
    if (layout_size_.width != current.width || layout_size_.height != current.height) {
        on_drag_ = -1;
        set_cursor(lib::CursorKind::Arrow);
    }
    std::vector<lib::PreparedChildBounds> placements;
    placements.reserve(components_.size() + (canvas_ ? 1U : 0U));
    if (canvas_) placements.push_back({canvas_, current});
    calcMaxSize();
    modified_ = false;
    layout_size_ = {current.width, current.height};
    if (parameter_count_ == 0U || step_count_ == 0U) {
        render_state_ = {};
        return placements;
    }

    const auto client = current;
    int clientWidth = std::max(0, toInt(client.width));
    int clientHeight = std::max(0, toInt(client.height));
    if (clientWidth == 0 || clientHeight == 0) {
        const auto preferred = computeSize();
        clientWidth = std::max(0, toInt(preferred.width));
        clientHeight = std::max(0, toInt(preferred.height));
    }
    const auto spWidth = toInt(max_size_.width);
    const auto spHeight = toInt(max_size_.height);
    const auto toggleHalf = static_cast<double>(toggleSize()) / 2.0;

    if (oval_width_ < 0 && oval_height_ < 0 && parameter_count_ == 3U && !oval_) {
        auto width = clientWidth;
        auto height = clientHeight;
        if (height * MAX_GAP < width) width = static_cast<int>(height * MAX_GAP);
        if (width * MAX_GAP_3H < height) height = static_cast<int>(width * MAX_GAP_3H);
        const auto xStart = (clientWidth - width) / 2;
        const auto yStart = (clientHeight - height) / 2;
        double x1, y1, x2, y2, x3, y3;
        if (side_) {
            x1 = spWidth / 2.0; y1 = spHeight / 2.0;
            x2 = width - spWidth / 2.0; y2 = spHeight / 2.0;
            x3 = width / 2.0; y3 = height - spHeight / 2.0;
        } else {
            x1 = spWidth / 2.0; y1 = height - spHeight / 2.0;
            x2 = width - spWidth / 2.0; y2 = height - spHeight / 2.0;
            x3 = width / 2.0; y3 = spHeight / 2.0;
        }
        const auto centerX = (x1 + x2 + x3) / 3.0;
        const auto centerY = (y1 + y2 + y3) / 3.0;
        const auto r1 = std::atan2(y1 - centerY, x1 - centerX);
        const auto r2 = std::atan2(y2 - centerY, x2 - centerX);
        const auto r3 = std::atan2(y3 - centerY, x3 - centerX);
        for (std::size_t index = 0; index < 3U; ++index) {
            const auto* point = index == 0U ? &x1 : index == 1U ? &x2 : &x3;
            const auto* vertical = index == 0U ? &y1 : index == 1U ? &y2 : &y3;
            placements.push_back({components_[index], {
                client.x + static_cast<float>(xStart + rounded(*point - spWidth / 2.0)),
                client.y + static_cast<float>(yStart + rounded(*vertical - spHeight / 2.0)),
                max_size_.width, max_size_.height}});
        }
        const auto distance = toggleSize() + figureMargin() + (spWidth + spHeight) / 4.0;
        x1 -= distance * std::cos(r1); y1 -= distance * std::sin(r1);
        x2 -= distance * std::cos(r2); y2 -= distance * std::sin(r2);
        x3 -= distance * std::cos(r3); y3 -= distance * std::sin(r3);
        const auto x1d = (centerX - x1) / step_count_;
        const auto y1d = (centerY - y1) / step_count_;
        const auto x2d = (centerX - x2) / step_count_;
        const auto y2d = (centerY - y2) / step_count_;
        const auto x3d = (centerX - x3) / step_count_;
        const auto y3d = (centerY - y3) / step_count_;
        polygons_.assign(step_count_, std::vector<int>(parameter_count_ * 2U));
        for (std::uint32_t step = 0; step < step_count_; ++step) {
            const auto x1i = rounded(x1 + x1d * step);
            const auto y1i = rounded(y1 + y1d * step);
            const auto x2i = rounded(x2 + x2d * step);
            const auto y2i = rounded(y2 + y2d * step);
            const auto x3i = rounded(x3 + x3d * step);
            const auto y3i = rounded(y3 + y3d * step);
            polygons_[step] = {xStart + x1i, yStart + y1i,
                               xStart + x2i, yStart + y2i,
                               xStart + x3i, yStart + y3i};
            toggles_[0][step_count_ - 1U - step] = {
                static_cast<float>(xStart + x1i), static_cast<float>(yStart + y1i)};
            toggles_[1][step_count_ - 1U - step] = {
                static_cast<float>(xStart + x2i), static_cast<float>(yStart + y2i)};
            toggles_[2][step_count_ - 1U - step] = {
                static_cast<float>(xStart + x3i), static_cast<float>(yStart + y3i)};
        }
        updateRenderState();
        return placements;
    }

    int ovalWidth = oval_width_;
    int ovalHeight = oval_height_;
    if (ovalWidth < 0 || ovalHeight < 0) {
        double ovalWidthRatio = std::numeric_limits<double>::max();
        double ovalHeightRatio = std::numeric_limits<double>::max();
        const auto cw = static_cast<double>(clientWidth);
        const auto ch = static_cast<double>(clientHeight);
        const auto bw = static_cast<double>(spWidth);
        const auto bh = static_cast<double>(spHeight);
        const auto distanceW = toggleSize() + figureMargin() + spWidth / 2.0;
        const auto distanceH = toggleSize() + figureMargin() + spHeight / 2.0;
        for (std::size_t index = 0; index < parameter_count_; ++index) {
            auto degree = static_cast<int>(
                (PI * 2.0 * nPos(index) / parameter_count_) * 180.0 / PI);
            degree = (degree % 360 + 360) % 360;
            const auto cosine = std::cos(degree * PI / 180.0);
            const auto sine = degree == 180 ? 0.0 : std::sin(degree * PI / 180.0);
            double baseX = 0.0;
            double baseY = 0.0;
            if (degree < 90) { baseX = +(cw - bw); baseY = +(ch - bh); }
            else if (degree < 180) { baseX = -(cw - bw); baseY = +(ch - bh); }
            else if (degree < 270) { baseX = -(cw - bw); baseY = -(ch - bh); }
            else { baseX = +(cw - bw); baseY = -(ch - bh); }
            if (std::abs(cosine) > 1.0e-9)
                ovalWidthRatio = std::min(ovalWidthRatio,
                    baseX / (cosine * 2.0) - distanceW);
            if (std::abs(sine) > 1.0e-9)
                ovalHeightRatio = std::min(ovalHeightRatio,
                    baseY / (sine * 2.0) - distanceH);
        }
        if (ovalWidth < 0)
            ovalWidth = std::max(0, rounded(ovalWidthRatio * 2.0));
        if (ovalHeight < 0)
            ovalHeight = std::max(0, rounded(ovalHeightRatio * 2.0));
    }
    if (oval_width_ < 0 && oval_height_ < 0) {
        if (std::min(ovalWidth, ovalHeight) * MAX_GAP <
            std::max(ovalWidth, ovalHeight)) {
            if (ovalWidth < ovalHeight) ovalHeight = rounded(ovalWidth * MAX_GAP);
            else ovalWidth = rounded(ovalHeight * MAX_GAP);
        }
    }

    const auto totalWidth = ovalWidth + toggleSize() + figureMargin() * 2 + spWidth * 2;
    const auto totalHeight = ovalHeight + toggleSize() + figureMargin() * 2 + spHeight * 2;
    const auto included = computeBounds(totalWidth, totalHeight);
    const auto positionX = (clientWidth - included.width) / 2.0F - included.x;
    const auto positionY = (clientHeight - included.height) / 2.0F - included.y;
    const auto baseX = totalWidth - toggleSize() - figureMargin() * 2 - spWidth * 2;
    const auto baseY = totalHeight - toggleSize() - figureMargin() * 2 - spHeight * 2;
    double ovalX = static_cast<double>(baseX);
    double ovalY = static_cast<double>(baseY);
    const auto ovalDX = ovalX / step_count_;
    const auto ovalDY = ovalY / step_count_;
    double x = toggleHalf + figureMargin() + spWidth + positionX;
    double y = toggleHalf + figureMargin() + spHeight + positionY;
    ovals_.assign(oval_ ? step_count_ : 0U, {});
    polygons_.assign(oval_ ? 0U : step_count_,
                     std::vector<int>(parameter_count_ * 2U));
    for (std::uint32_t step = 0; step < step_count_; ++step) {
        if (oval_) {
            ovals_[step] = {static_cast<float>(rounded(x)),
                            static_cast<float>(rounded(y)),
                            static_cast<float>(rounded(ovalX) + 1),
                            static_cast<float>(rounded(ovalY) + 1)};
        }
        const auto rw = ovalX / 2.0;
        const auto rh = ovalY / 2.0;
        for (std::size_t index = 0; index < parameter_count_; ++index) {
            const auto position = nPos(index);
            const auto pointX = rounded(x + rw + rw *
                                        std::cos(PI * 2.0 * position / parameter_count_));
            const auto pointY = rounded(y + rh + rh *
                                        std::sin(PI * 2.0 * position / parameter_count_));
            toggles_[index][step_count_ - 1U - step] = {
                static_cast<float>(pointX), static_cast<float>(pointY)};
            if (!oval_) {
                polygons_[step][index * 2U] = pointX;
                polygons_[step][index * 2U + 1U] = pointY;
            }
        }
        if (step + 1U < step_count_) {
            ovalX -= ovalDX;
            ovalY -= ovalDY;
            x = spWidth + figureMargin() + toggleHalf +
                (baseX - ovalX) / 2.0 + positionX;
            y = spHeight + figureMargin() + toggleHalf +
                (baseY - ovalY) / 2.0 + positionY;
        }
    }

    x = spWidth / 2.0 + positionX;
    y = spHeight / 2.0 + positionY;
    ovalX = totalWidth - spWidth;
    ovalY = totalHeight - spHeight;
    for (std::size_t index = 0; index < parameter_count_; ++index) {
        const auto position = nPos(index);
        const auto rw = ovalX / 2.0;
        const auto rh = ovalY / 2.0;
        const auto pointX = x + rw + rw * std::cos(PI * 2.0 * position / parameter_count_);
        const auto pointY = y + rh + rh * std::sin(PI * 2.0 * position / parameter_count_);
        placements.push_back({components_[index], {
            client.x + static_cast<float>(rounded(pointX - spWidth / 2.0)),
            client.y + static_cast<float>(rounded(pointY - spHeight / 2.0)),
            max_size_.width, max_size_.height}});
    }

    updateRenderState();
    return placements;
}

void RadarSpinner::setValueFromSpinner(const std::size_t index, const int value) {
    if (index >= values_.size()) return;
    const auto adjusted = clampValue(value, minimum_,
                                     rangeMaximum(minimum_, step_count_));
    values_[index] = adjusted;
    updateReadOnlyLabel(index, adjusted);
    for (const auto& handler : mod_handler_) {
        if (handler) handler(static_cast<int>(index), adjusted);
    }
    if (mod_redraw_) mod_redraw_();
}

void RadarSpinner::updateReadOnlyLabel(const std::size_t index, const int value) {
    if (index >= spinners_.size()) return;
    if (auto label = std::dynamic_pointer_cast<lib::Label>(spinners_[index])) {
        label->text = std::to_string(value);
    } else if (auto spinner = std::dynamic_pointer_cast<lib::NumberEdit>(spinners_[index])) {
        spinner->value = static_cast<double>(value);
    }
}

void RadarSpinner::setValue(const std::size_t index, const int value) {
    ::cwxeditor::cwx::perf::TraceScope trace_setValue(__FILE__, __LINE__);
    if (index >= values_.size()) return;
    const auto adjusted = read_only_ ? value : clampValue(value, minimum_,
                                     rangeMaximum(minimum_, step_count_));
    values_[index] = adjusted;
    updateReadOnlyLabel(index, adjusted);
    redraw();
}

void RadarSpinner::setValues(const std::vector<int>& values) {
    ::cwxeditor::cwx::perf::TraceScope trace_setValues(__FILE__, __LINE__);
    const auto count = std::min(values.size(), values_.size());
    for (std::size_t index = 0; index < count; ++index) setValue(index, values[index]);
    redraw();
}

int RadarSpinner::getValue(const std::size_t index) const {
    ::cwxeditor::cwx::perf::TraceScope trace_getValue(__FILE__, __LINE__);
    return index < values_.size() ? values_[index] : minimum_;
}

std::vector<int> RadarSpinner::getValues() const {
    ::cwxeditor::cwx::perf::TraceScope trace_getValues(__FILE__, __LINE__);
    return values_;
}

void RadarSpinner::lineStep(const std::uint32_t step) {
    // D版の EffectCardDialog は enhanceMax /
    // 2 を渡すが、2未満ではゼロとなり、補助線のループが進まなくなる。
    // どの表示側でも描画できるよう、共通コントロールでは間隔を最低1にする。
    oval_step_ = std::max(1U, step);
    modified_ = true;
    redraw();
}

void RadarSpinner::antialias(const int value) {
    antialias_ = value;
    redraw();
}

void RadarSpinner::sideMode(const bool value) {
    ::cwxeditor::cwx::perf::TraceScope trace_sideMode(__FILE__, __LINE__);
    if (side_ == value) return;
    side_ = value;
    modified_ = true;
    redraw();
}

void RadarSpinner::oval(const bool value) {
    ::cwxeditor::cwx::perf::TraceScope trace_oval(__FILE__, __LINE__);
    if (oval_ == value) return;
    oval_ = value;
    ovals_.assign(oval_ ? step_count_ : 0U, {});
    polygons_.assign(oval_ ? 0U : step_count_,
                     std::vector<int>(parameter_count_ * 2U));
    modified_ = true;
    redraw();
}

bool RadarSpinner::isBorderline(const int value) const {
    return std::find(borderlines_.begin(), borderlines_.end(), value) !=
           borderlines_.end();
}

void RadarSpinner::borderlines(const std::vector<int>& lines) {
    borderlines_ = lines;
    redraw();
}

void RadarSpinner::toggleStyle(const Toggle value) {
    toggle_style_ = value;
    for (auto& marker : render_state_.markers) marker.style = value;
    redraw();
}

void RadarSpinner::setRadarSize(const int width, const int height) {
    ::cwxeditor::cwx::perf::TraceScope trace_setRadarSize(__FILE__, __LINE__);
    if (oval_width_ == width && oval_height_ == height) return;
    oval_width_ = width;
    oval_height_ = height;
    modified_ = true;
    redraw();
}

void RadarSpinner::setRadarColor(const lib::Color foreground,
                                 const lib::Color background) {
    oval_foreground_ = foreground;
    oval_background_ = background;
    redraw();
}

void RadarSpinner::alpha(const int value) {
    alpha_ = value;
    redraw();
}

void RadarSpinner::addModifyHandler(ModifyHandler handler) {
    ::cwxeditor::cwx::perf::TraceScope trace_addModifyHandler(__FILE__, __LINE__);
    if (handler) mod_handler_.push_back(std::move(handler));
}

lib::Size RadarSpinner::computeSize(const int width_hint,
                                    const int height_hint) const {
    ::cwxeditor::cwx::perf::TraceScope trace_computeSize_2(__FILE__, __LINE__);
    return computeSize(width_hint, height_hint, false);
}

lib::Size RadarSpinner::computeSize(const int width_hint, const int height_hint,
                                    const bool changed) const {
    ::cwxeditor::cwx::perf::TraceScope trace_computeSize_3(__FILE__, __LINE__);
    (void)changed;
    if (width_hint != -1 && height_hint != -1)
        return {static_cast<float>(width_hint), static_cast<float>(height_hint)};
    if (width_hint == -1 && height_hint != -1)
        return {static_cast<float>(height_hint), static_cast<float>(height_hint)};
    if (width_hint != -1 && height_hint == -1)
        return {static_cast<float>(width_hint), static_cast<float>(width_hint)};
    if (names_.empty()) {
        const auto width = oval_width_ == -1 ? 0 : oval_width_;
        const auto height = oval_height_ == -1 ? 0 : oval_height_;
        return {static_cast<float>(width), static_cast<float>(height)};
    }
    const auto width = oval_width_;
    const auto height = oval_height_;
    const auto totalWidth = width + toggleSize() + figureMargin() * 2 + toInt(max_size_.width) * 2;
    const auto totalHeight = height + toggleSize() + figureMargin() * 2 + toInt(max_size_.height) * 2;
    const auto bounds = computeBounds(totalWidth, totalHeight);
    return {bounds.width, bounds.height};
}

void RadarSpinner::set_font(std::optional<lib::FontSpec> font) {
    if (sameFontSpec(this->font(), font)) return;
    lib::Widget::set_font(font);
    for (const auto& label : labels_) {
        if (label) label->set_font(font);
    }
    calcMaxSize();
    modified_ = true;
    redraw();
}

void RadarSpinner::setFont(std::optional<lib::FontSpec> font) {
    set_font(std::move(font));
}

void RadarSpinner::set_foreground(std::optional<lib::Color> color) noexcept {
    lib::Widget::set_foreground(color);
    for (const auto& label : labels_) {
        if (label) label->set_foreground(color);
    }
}

void RadarSpinner::setForeground(const lib::Color color) {
    set_foreground(color);
}

void RadarSpinner::resize() {
    modified_ = true;
    redraw();
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::radarspinner
