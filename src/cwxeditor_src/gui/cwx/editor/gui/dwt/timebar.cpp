#include "cwxeditor_src/gui/cwx/editor/gui/dwt/timebar.hpp"
#include "cwxeditor_src/lib/ui_control_palette.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

namespace cwxeditor::cwx::editor::gui::dwt::timebar {

namespace {

// D版と同様、白ではなくウィジェット自身の標準背景色で塗る。

[[nodiscard]] std::int32_t truncToInt32(const double value) noexcept {
    if (!std::isfinite(value)) {
        return value > 0.0 ? std::numeric_limits<std::int32_t>::max()
                           : std::numeric_limits<std::int32_t>::min();
    }
    const auto lower = static_cast<double>(std::numeric_limits<std::int32_t>::min());
    const auto upper = static_cast<double>(std::numeric_limits<std::int32_t>::max());
    if (value <= lower) return std::numeric_limits<std::int32_t>::min();
    if (value >= upper) return std::numeric_limits<std::int32_t>::max();
    return static_cast<std::int32_t>(value);
}

[[nodiscard]] float nonnegative(const float value) noexcept {
    return std::isfinite(value) && value > 0.0F ? value : 0.0F;
}

}  // namespace

TimeBar::TimeBar(::cwxeditor::lib::Container* parent, const std::int32_t style,
                 const ::cwxeditor::lib::SystemBackend* system,
                 ::cwxeditor::lib::GraphicsBackend* graphics)
    : parent_(parent), system_(system), graphics_(graphics), style_(style) {
    if (system_ != nullptr) dpi_scale_ = positiveScale(system_->scale_factor());

    // D版のコントロールは構築時に SWT.DOUBLE_BUFFERED
    // を使う。 共通の Canvas は既定で同じ動作をする。
    double_buffered = true;
    on_key_down = [this](::cwxeditor::lib::KeyEvent& event) { onKeyDown(event); };
    on_pointer = [this](::cwxeditor::lib::PointerEvent& event) {
        switch (event.action) {
        case ::cwxeditor::lib::PointerAction::Down:
            onMouseDown(event);
            break;
        case ::cwxeditor::lib::PointerAction::Up:
            onMouseUp(event);
            break;
        case ::cwxeditor::lib::PointerAction::Move:
            onMouseMove(event);
            break;
        default:
            break;
        }
    };
    on_focus = [this]() { setFocused(true); };
    on_blur = [this]() { setFocused(false); };
    on_paint = [this](const ::cwxeditor::lib::Rect area) { onPaint(area); };
    on_paint_context = [this](::cwxeditor::lib::DrawingContext& context,
                              const ::cwxeditor::lib::Rect area) {
        drawing_context_ = &context;
        onPaint(area);
        drawing_context_ = nullptr;
    };
    redraw();
}

void TimeBar::setWarning(const ::cwxeditor::lib::ResourceId image,
                         std::string message,
                         const ::cwxeditor::lib::Size image_size) {
    warning_ = std::move(message);
    warning_icon_.reset();
    if (image != 0U) {
        auto size = image_size;
        if (!std::isfinite(size.width)) size.width = 16.0F;
        if (!std::isfinite(size.height)) size.height = 16.0F;
        size.width = nonnegative(size.width);
        size.height = nonnegative(size.height);
        warning_icon_ = WarningIcon{image, size};
    }
    redraw();
}

void TimeBar::clearWarning() {
    warning_icon_.reset();
    warning_.clear();
    redraw();
}

void TimeBar::length(const Duration value) {
    duration_ = value;
    redraw();
}

void TimeBar::current(const Duration value) {
    current_ = value;
    redraw();
}

::cwxeditor::lib::Point TimeBar::computeSize(const std::int32_t width_hint,
                                             const std::int32_t height_hint,
                                             const bool changed) const noexcept {
    static_cast<void>(width_hint);
    static_cast<void>(height_hint);
    static_cast<void>(changed);
    // D版との意図的な違い。100pxでは通常のBGMでも
    // およそ一秒一画素になるため、 実際の再生時間は変えず
    // 、長い範囲を確認・シークできる十分な幅を確保する。
    return {240.0F * dpi_scale_, 28.0F * dpi_scale_};
}

void TimeBar::setSystemBackend(const ::cwxeditor::lib::SystemBackend* system) noexcept {
    system_ = system;
    setDpiScale(system_ == nullptr ? 1.0F : system_->scale_factor());
}

void TimeBar::setGraphicsBackend(::cwxeditor::lib::GraphicsBackend* graphics) noexcept {
    graphics_ = graphics;
    redraw();
}

void TimeBar::setDrawingContext(::cwxeditor::lib::DrawingContext* context) noexcept {
    drawing_context_ = context;
    redraw();
}

void TimeBar::setDpiScale(const float scale) noexcept {
    dpi_scale_ = positiveScale(scale);
    redraw();
}

void TimeBar::setFocused(const bool focused) noexcept {
    if (focused_ == focused) return;
    focused_ = focused;
    redraw();
}

void TimeBar::onKeyDown(::cwxeditor::lib::KeyEvent& event) {
    // D版と同じ一秒単位のキーボードシークにする。表示サイズとは独立して扱う。
    const auto seek_step = std::chrono::duration_cast<Duration>(std::chrono::seconds{1});

    switch (event.key) {
    case ::cwxeditor::lib::Key::Left:
        if (!key_moving_) {
            set_current_ = current_ == kUnsetDuration ? Duration::zero() : current_;
        }
        key_moving_ = true;
        moving_ = false;
        if (set_current_ < Duration::min() + seek_step) {
            set_current_ = Duration::min();
        } else {
            set_current_ -= seek_step;
        }
        if (set_current_ < Duration::zero()) set_current_ = Duration::zero();
        event.handled = true;
        redraw();
        break;

    case ::cwxeditor::lib::Key::Right:
        if (!key_moving_) {
            set_current_ = current_ == kUnsetDuration ? Duration::zero() : current_;
        }
        key_moving_ = true;
        moving_ = false;
        if (set_current_ > Duration::max() - seek_step) {
            set_current_ = Duration::max();
        } else {
            set_current_ += seek_step;
        }
        if (duration_ != kUnsetDuration && set_current_ > duration_) {
            set_current_ = duration_;
        }
        event.handled = true;
        redraw();
        break;

    case ::cwxeditor::lib::Key::Down:
    case ::cwxeditor::lib::Key::Up:
    case ::cwxeditor::lib::Key::Enter:
    case ::cwxeditor::lib::Key::Space:
        if (key_moving_) {
            current_ = set_current_;
            key_moving_ = false;
            moving_ = false;
            event.handled = true;
            redraw();
            notifyCurrentUpdated();
        }
        break;

    case ::cwxeditor::lib::Key::Escape:
    case ::cwxeditor::lib::Key::Backspace:
        // D版と同様、キーボード・マウスの両方のプレビューをキャンセルする。 待機中
        // は意図的に処理を通し、外側のダイアログがEscapeを扱えるようにする。
        if (key_moving_ || moving_) {
            key_moving_ = false;
            moving_ = false;
            event.handled = true;
            redraw();
        }
        break;

    default:
        break;
    }
}

void TimeBar::onMouseDown(::cwxeditor::lib::PointerEvent& event) {
    if (event.button == ::cwxeditor::lib::PointerButton::Primary) {
        setFocused(true);
        moving_ = true;
        key_moving_ = false;
        event.handled = true;
        onMouseMove(event);
    } else if (event.button == ::cwxeditor::lib::PointerButton::Secondary && moving_) {
        moving_ = false;
        event.handled = true;
        redraw();
    }
}

void TimeBar::onMouseUp(::cwxeditor::lib::PointerEvent& event) {
    if (event.button == ::cwxeditor::lib::PointerButton::Primary && moving_) {
        current_ = clampSelection(set_current_);
        moving_ = false;
        event.handled = true;
        redraw();
        notifyCurrentUpdated();
    }
}

void TimeBar::onMouseMove(::cwxeditor::lib::PointerEvent& event) {
    if (!moving_) return;
    const auto width = bounds().width;
    if (!(width > 0.0F) || !std::isfinite(width)) return;

    const auto total = seconds(duration_);
    const auto position = static_cast<double>(event.position.x - bounds().x) /
                          static_cast<double>(width) * static_cast<double>(total);
    const auto selected_seconds = truncToInt32(position);
    set_current_ = std::chrono::duration_cast<Duration>(
        std::chrono::seconds{selected_seconds});
    event.handled = true;
    redraw();
}

void TimeBar::onPaint(const ::cwxeditor::lib::Rect area) {
    render_state_ = {};
    render_state_.area = area;
    // D版がGCのコントロール色を変えないのと同様、 明示的なウィジ
    // ェットの上書きがあれば使い、なければ共通の既定色を維持する。
    const auto palette = ::cwxeditor::lib::system_control_palette();
    const auto foreground_color = foreground().value_or(palette.foreground);
    const auto background_color = background().value_or(palette.background);

    const auto width = nonnegative(area.width);
    const auto height = nonnegative(area.height);
    const auto reference_height = 28.0F * dpi_scale_;
    const auto vertical_scale = reference_height > 0.0F
        ? std::clamp(height / reference_height, 0.0F, 1.0F) : 0.0F;
    render_state_.fill = TimeBarRectangle{
        area, background_color, 0.0F, ::cwxeditor::lib::StrokeStyle::Solid};
    render_state_.border = TimeBarRectangle{
        {area.x, area.y, std::max(0.0F, width - 1.0F), std::max(0.0F, height - 1.0F)},
        foreground_color, 1.0F, ::cwxeditor::lib::StrokeStyle::Solid};

    const auto total = seconds(duration_);
    if (total > 0) {
        for (std::int64_t tick = 0; tick < total;) {
            const auto x = area.x + width * static_cast<float>(tick) /
                                         static_cast<float>(total);
            const auto major = tick % 30 == 0;
            const auto tick_height = (major ? 8.0F : 5.0F) *
                                    dpi_scale_ * vertical_scale;
            render_state_.lines.push_back({
                {x, area.y + height},
                {x, area.y + height - tick_height},
                foreground_color,
                1.0F,
                ::cwxeditor::lib::StrokeStyle::Solid});
            if (total - tick <= 5) break;
            tick += 5;
        }
    }

    if (!warning_.empty()) {
        const auto extent = textExtent(warning_);
        float icon_width = 0.0F;
        float icon_height = 0.0F;
        if (warning_icon_) {
            icon_width = nonnegative(warning_icon_->size.width * dpi_scale_);
            icon_height = nonnegative(warning_icon_->size.height * dpi_scale_);
        }
        const auto spacing = warning_icon_ ? 2.0F * dpi_scale_ : 0.0F;
        const auto combined_width = icon_width + spacing + extent.width;
        auto text_x = area.x + (width - combined_width) / 2.0F;
        if (warning_icon_) {
            const ::cwxeditor::lib::Rect destination{
                text_x, area.y + (height - icon_height) / 2.0F,
                icon_width, icon_height};
            render_state_.images.push_back({warning_icon_->image, destination});
            text_x += icon_width + spacing;
        }
        render_state_.texts.push_back({
            warning_,
            {text_x, area.y + (height - extent.height) / 2.0F},
            font().value_or(::cwxeditor::lib::FontSpec{}),
            foreground_color});
        render_state_.warning = true;
    } else {
        if (total > 0) {
            const auto current_seconds = static_cast<float>(seconds(current_));
            const auto x = area.x + width * current_seconds / static_cast<float>(total);
            render_state_.lines.push_back({
                {x, area.y},
                {x, area.y + height},
                foreground_color,
                1.0F,
                ::cwxeditor::lib::StrokeStyle::Solid});

            if (moving_ || key_moving_) {
                const auto selected_seconds = static_cast<float>(seconds(set_current_));
                const auto selected_x = area.x + width * selected_seconds /
                                                        static_cast<float>(total);
                render_state_.lines.push_back({
                    {selected_x, area.y},
                    {selected_x, area.y + height},
                    foreground_color,
                    1.0F,
                    ::cwxeditor::lib::StrokeStyle::Dot});
            }
        }

        const auto length_seconds = seconds(duration_);
        const auto with_hours = length_seconds > 0 && length_seconds / 3600 > 0;
        const auto label = timeLabel(current_, with_hours) + " / " +
                           timeLabel(duration_, with_hours);
        const auto extent = textExtent(label);
        render_state_.texts.push_back({
            label,
            {area.x + (width - extent.width) / 2.0F,
             area.y + (height - 5.0F * dpi_scale_ * vertical_scale -
                       extent.height) / 2.0F},
            font().value_or(::cwxeditor::lib::FontSpec{}),
            foreground_color});
    }

    if (focused_) {
        render_state_.focus = ::cwxeditor::lib::Rect{
            area.x + 2.0F, area.y + 2.0F,
            std::max(0.0F, width - 4.0F), std::max(0.0F, height - 4.0F)};
    }
    drawRenderState();
}

void TimeBar::redraw() {
    onPaint(bounds());
    if (invalidate) invalidate();
}

void TimeBar::notifyCurrentUpdated() {
    const auto callbacks = updateCurrentEvent;
    for (const auto& callback : callbacks) {
        if (callback) callback();
    }
}

void TimeBar::drawRenderState() {
    if (drawing_context_ == nullptr) return;

    drawing_context_->save();
    drawing_context_->clip(render_state_.area);
    if (render_state_.fill) {
        drawing_context_->fill_rect(render_state_.fill->rectangle,
                                    render_state_.fill->color);
    }
    if (render_state_.border) {
        drawing_context_->draw_rect(render_state_.border->rectangle,
                                    render_state_.border->color,
                                    render_state_.border->width,
                                    render_state_.border->style);
    }
    for (const auto& line : render_state_.lines) {
        drawing_context_->draw_line(line.from, line.to, line.color, line.width, line.style);
    }
    for (const auto& image : render_state_.images) {
        const ::cwxeditor::lib::Rect source{
            0.0F, 0.0F, image.destination.width, image.destination.height};
        drawing_context_->draw_image(image.image, source, image.destination,
                                     ::cwxeditor::lib::Interpolation::Linear);
    }
    for (const auto& text : render_state_.texts) {
        drawing_context_->draw_text(text.text, text.origin, text.font, text.color);
    }
    if (render_state_.focus) {
        const auto focus_color = render_state_.border
            ? render_state_.border->color
            : ::cwxeditor::lib::system_control_palette().foreground;
        drawing_context_->draw_rect(*render_state_.focus, focus_color, 1.0F,
                                     ::cwxeditor::lib::StrokeStyle::Dash);
    }
    drawing_context_->restore();
}

::cwxeditor::lib::Size TimeBar::textExtent(const std::string_view text) const {
    const auto selected_font = font().value_or(::cwxeditor::lib::FontSpec{});
    if (graphics_ != nullptr) {
        const auto measured = graphics_->measure_text(text, selected_font, 0.0F);
        if (std::isfinite(measured.width) && measured.width >= 0.0F &&
            std::isfinite(measured.height) && measured.height >= 0.0F) {
            return measured;
        }
    }
    return {static_cast<float>(text.size()) * 7.0F * dpi_scale_, 14.0F * dpi_scale_};
}

std::string TimeBar::timeLabel(const Duration value, const bool with_hours) const {
    auto total = seconds(value);
    if (total < 0) total = 0;
    const auto hours = total / 3600;
    const auto minutes = (total % 3600) / 60;
    const auto seconds_value = total % 60;

    std::ostringstream stream;
    stream << std::setfill('0');
    if (with_hours) {
        stream << std::setw(2) << hours << ':'
               << std::setw(2) << minutes << ':'
               << std::setw(2) << seconds_value;
    } else {
        stream << std::setw(2) << minutes << ':'
               << std::setw(2) << seconds_value;
    }
    return stream.str();
}

Duration TimeBar::clampSelection(const Duration value) const noexcept {
    if (duration_ == kUnsetDuration) return Duration::zero();
    const auto candidate = value == kUnsetDuration ? Duration::zero() : value;
    const auto upper = std::max(Duration::zero(), duration_);
    return std::clamp(candidate, Duration::zero(), upper);
}

std::int64_t TimeBar::seconds(const Duration value) const noexcept {
    if (value == kUnsetDuration) return 0;
    return std::chrono::duration_cast<std::chrono::seconds>(value).count();
}

float TimeBar::positiveScale(const float scale) const noexcept {
    return std::isfinite(scale) && scale > 0.0F ? scale : 1.0F;
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::timebar
