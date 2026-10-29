#include "cwxeditor_src/gui/cwx/editor/gui/dwt/scales.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dutils.hpp"
#include "cwxeditor_src/lib/ui_metrics.hpp"
#include "cwxeditor_src/lib/ui_slider.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace cwxeditor::cwx::editor::gui::dwt::scales {

namespace {

[[nodiscard]] std::int32_t toInt(const double value) noexcept {
    if (!std::isfinite(value)) return 0;
    const auto rounded = std::llround(value);
    if (rounded < std::numeric_limits<std::int32_t>::min()) {
        return std::numeric_limits<std::int32_t>::min();
    }
    if (rounded > std::numeric_limits<std::int32_t>::max()) {
        return std::numeric_limits<std::int32_t>::max();
    }
    return static_cast<std::int32_t>(rounded);
}

[[nodiscard]] float positiveScale(const float value) noexcept {
    return std::isfinite(value) && value > 0.0F ? value : 1.0F;
}

}  // namespace

Scales::Scales(::cwxeditor::lib::Container* parent, const std::int32_t style,
               const ::cwxeditor::lib::SystemBackend* system,
               ::cwxeditor::lib::GraphicsBackend* graphics)
    : parent_(parent), system_(system), graphics_(graphics),
      read_only_((static_cast<std::uint32_t>(style) & STYLE_READ_ONLY) != 0U) {
    if (system_ != nullptr) dpi_scale_ = positiveScale(system_->scale_factor());
    auto grid = std::make_shared<::cwxeditor::lib::GridLayout>();
    grid->columns = 3U;
    grid->equal_width = false;
    set_layout(std::move(grid));
}

void Scales::setSystemBackend(const ::cwxeditor::lib::SystemBackend* system) noexcept {
    system_ = system;
    setDpiScale(system_ == nullptr ? 1.0F : system_->scale_factor());
}

void Scales::setDpiScale(const float scale) noexcept {
    dpi_scale_ = positiveScale(scale);
    redrawHints();
}

void Scales::setScales(const std::uint32_t step_count,
                       const StringArray& names, const std::int32_t page,
                       const std::int32_t minimum) {
    if (step_count == 0U) throw std::invalid_argument("Scales requires at least one step");
    if (names.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        throw std::length_error("Too many scale controls");
    }

    clear();
    labels_.clear();
    spinners_.clear();
    scales_.clear();
    selections_.clear();
    hint_marks_.clear();

    auto grid = std::make_shared<::cwxeditor::lib::GridLayout>();
    grid->columns = 3U;
    grid->equal_width = false;
    set_layout(std::move(grid));

    const auto maximum = static_cast<std::int64_t>(minimum) +
                         static_cast<std::int64_t>(step_count) - 1LL;
    if (maximum > std::numeric_limits<std::int32_t>::max() ||
        maximum < std::numeric_limits<std::int32_t>::min()) {
        throw std::out_of_range("Scale range does not fit in int32");
    }
    const auto slider_maximum = static_cast<double>(step_count - 1U);
    const auto page_step = page > 0 ? static_cast<double>(page) : 10.0;

    for (std::size_t index = 0; index < names.size(); ++index) {
        auto label = std::make_shared<::cwxeditor::lib::Label>(names[index]);
        auto label_item = ::cwxeditor::lib::LayoutItem{};
        label_item.row = index;
        label_item.column = 0U;
        label_item.horizontal = ::cwxeditor::lib::Align::Start;
        label_item.vertical = ::cwxeditor::lib::Align::Center;
        label->set_layout_item(label_item);
        add(label);
        labels_.push_back(label);

        auto slider = std::make_shared<::cwxeditor::lib::Slider>();
        slider->minimum = 0.0;
        slider->maximum = slider_maximum;
        slider->step = 1.0;
        slider->page_step = page_step;
        slider->tick_interval = page_step;
        slider->orientation = ::cwxeditor::lib::Orientation::Horizontal;
        slider->set_enabled(!read_only_);
        auto slider_item = ::cwxeditor::lib::LayoutItem{};
        slider_item.row = index;
        slider_item.column = 1U;
        slider_item.horizontal = ::cwxeditor::lib::Align::Stretch;
        slider_item.vertical = ::cwxeditor::lib::Align::Center;
        slider->set_layout_item(slider_item);
        add(slider);
        scales_.push_back(slider);

        auto spinner = std::make_shared<::cwxeditor::lib::NumberEdit>();
        spinner->minimum = static_cast<double>(minimum);
        spinner->maximum = static_cast<double>(maximum);
        spinner->step = 1.0;
        spinner->decimals = 0U;
        spinner->read_only = read_only_;
        dutils::initSpinner(spinner);
        spinner->value = static_cast<double>(minimum);
        auto spinner_item = ::cwxeditor::lib::LayoutItem{};
        spinner_item.row = index;
        spinner_item.column = 2U;
        spinner_item.horizontal = ::cwxeditor::lib::Align::Start;
        spinner_item.vertical = ::cwxeditor::lib::Align::Center;
        spinner->set_layout_item(spinner_item);
        add(spinner);
        spinners_.push_back(spinner);

        auto selection = std::make_shared<Selection>(*this, index, slider, spinner);
        slider->on_changed = [selection](const double value) {
            selection->widgetSelected(true, value);
        };
        spinner->on_changed = [selection](const double value) {
            selection->modifyText(value);
        };
        selections_.push_back(std::move(selection));
    }

    // DWTと同様、案内のキャンバスの前に、最初の列のゼロサイズの
    // 仮要素を確保する。 共通グリッドにはネイティブのゼロサイズの
    // 目印がないため、Spacer で同じ所有・配置構造を保つ。
    auto spacer = std::make_shared<::cwxeditor::lib::Spacer>();
    auto spacer_item = ::cwxeditor::lib::LayoutItem{};
    spacer_item.row = names.size();
    spacer_item.column = 0U;
    spacer_item.preferred = {0.0F, 0.0F};
    spacer->set_layout_item(spacer_item);
    add(spacer);

    hints_ = std::make_shared<::cwxeditor::lib::Canvas>();
    hints_->double_buffered = true;
    hints_->ui_text_lines = 1;
    auto hint_item = ::cwxeditor::lib::LayoutItem{};
    hint_item.row = names.size();
    hint_item.column = 1U;
    hint_item.column_span = 1U;
    hint_item.horizontal = ::cwxeditor::lib::Align::Stretch;
    hint_item.vertical = ::cwxeditor::lib::Align::Center;
    hints_->set_layout_item(hint_item);
    paint_hints_ = std::make_shared<PaintHints>(*this);
    hints_->on_paint = [paint = paint_hints_](const ::cwxeditor::lib::Rect area) {
        paint->paint(area);
    };
    hints_->on_paint_context = [this](::cwxeditor::lib::DrawingContext& context,
                                      const ::cwxeditor::lib::Rect area) {
        updateHintMarks(area);
        const auto color = ::cwxeditor::lib::Color{31, 31, 31, 255};
        const auto font = ::cwxeditor::lib::effective_editor_widget_font(
            hints_->font() ? &*hints_->font() : nullptr);
        for (const auto& mark : hint_marks_)
            context.draw_text(mark.text, {mark.x, mark.y}, font, color);
    };
    add(hints_);
    redrawHints();
}

Scales::Selection::Selection(Scales& owner, const std::size_t index,
                             SliderPtr scale, NumberEditPtr spinner)
    : owner_(owner), index_(index), scale_(std::move(scale)),
      spinner_(std::move(spinner)) {}

void Scales::Selection::widgetSelected(const bool from_scale, const double value) {
    if (from_scale) {
        const auto minimum = toInt(spinner_->minimum);
        const auto selected = toInt(value) + minimum;
        spinner_->value = static_cast<double>(owner_.clampValue(index_, selected));
    } else {
        const auto selected = owner_.clampValue(index_, toInt(value));
        spinner_->value = static_cast<double>(selected);
        scale_->value = static_cast<double>(selected - toInt(spinner_->minimum));
    }
    owner_.notifyModified();
}

void Scales::Selection::modifyText(const double value) {
    const auto selected = owner_.clampValue(index_, toInt(value));
    spinner_->value = static_cast<double>(selected);
    scale_->value = static_cast<double>(selected - toInt(spinner_->minimum));
    owner_.notifyModified();
}

void Scales::PaintHints::paint(const ::cwxeditor::lib::Rect& area) {
    owner_.updateHintMarks(area);
}

void Scales::setValue(const std::size_t index, const std::int32_t value) {
    const auto& spinner = spinners_.at(index);
    const auto& slider = scales_.at(index);
    const auto selected = clampValue(index, value);
    spinner->value = static_cast<double>(selected);
    slider->value = static_cast<double>(selected - toInt(spinner->minimum));
}

void Scales::setValues(const IntArray& values) {
    for (std::size_t index = 0; index < values.size(); ++index) {
        setValue(index, values[index]);
    }
}

std::int32_t Scales::getValue(const std::size_t index) const {
    return toInt(spinners_.at(index)->value);
}

Scales::IntArray Scales::getValues() const {
    IntArray result;
    result.reserve(spinners_.size());
    for (const auto& spinner : spinners_) result.push_back(toInt(spinner->value));
    return result;
}

void Scales::borderlines(const IntArray& lines) {
    borderlines_ = lines;
    redrawHints();
}

void Scales::notifyModified() {
    // D版のデリゲート配列と同様、コールバックはリスナー
    // を追加できる。 スナップショットを使い、ベクター
    // のイテレーターを無効化せずに同じ走査動作を保つ。
    const auto callbacks = modEvent;
    for (const auto& callback : callbacks) {
        if (callback) callback();
    }
}

void Scales::redrawHints() {
    if (!paint_hints_) return;
    auto area = hints_ ? hints_->bounds() : ::cwxeditor::lib::Rect{};
    if (area.width <= 0.0F) area.width = ::cwxeditor::lib::scaled_ui_metric(300);
    if (area.height <= 0.0F) area.height = 1.0F;
    paint_hints_->paint(area);
    if (hints_ && hints_->invalidate) hints_->invalidate();
}

void Scales::updateHintMarks(const ::cwxeditor::lib::Rect& area) {
    hint_marks_.clear();
    if (spinners_.empty()) return;

    // D版と同様、案内表示を実際のつまみの端に合わせる。 OS・DPI
    // 固有の余白ではなく、描画と当たり判定と共通の位置計算を使う。
    ::cwxeditor::lib::Slider track;
    track.set_bounds({area.x, area.y, std::max(0.0F, area.width),
                      ::cwxeditor::lib::scaled_ui_metric(24)});
    const auto geometry = ::cwxeditor::lib::layout_slider(track);
    const auto left = geometry.first;
    const auto usable = std::max(0.0F, geometry.last - geometry.first);
    const auto minimum = toInt(spinners_.front()->minimum);
    const auto maximum = toInt(spinners_.front()->maximum);
    const auto range = static_cast<double>(maximum) - minimum;
    const auto position = [left, usable, minimum, range](const std::int32_t value) {
        if (range == 0) return left;
        return left + usable * static_cast<float>((static_cast<double>(value) - minimum) / range);
    };

    const auto min_text = std::to_string(minimum);
    const auto max_text = std::to_string(maximum);
    hint_marks_.push_back({min_text, position(minimum) - textWidth(min_text) / 2.0F, area.y});
    hint_marks_.push_back({max_text, position(maximum) - textWidth(max_text) / 2.0F, area.y});
    for (const auto value : borderlines_) {
        const auto text = std::to_string(value);
        hint_marks_.push_back({text, position(value) - textWidth(text) / 2.0F, area.y});
    }
}

float Scales::textWidth(const std::string_view value) const {
    const auto* font = hints_ && hints_->font() ? &*hints_->font() : nullptr;
    if (const auto measured = ::cwxeditor::lib::measured_ui_text_size(value, font))
        return measured->width;
    if (graphics_) {
        const auto measured = graphics_->measure_text(value,
            ::cwxeditor::lib::effective_editor_widget_font(font), 0.0F);
        if (std::isfinite(measured.width) && measured.width >= 0.0F) return measured.width;
    }
    return ::cwxeditor::lib::ui_text_width(value, 0, font);
}

std::int32_t Scales::clampValue(const std::size_t index,
                                const std::int32_t value) const {
    const auto& spinner = spinners_.at(index);
    const auto minimum = toInt(spinner->minimum);
    const auto maximum = toInt(spinner->maximum);
    return std::clamp(value, minimum, maximum);
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::scales
