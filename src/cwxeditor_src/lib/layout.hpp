#pragma once

#include "types.hpp"

#include <cstddef>
#include <memory>

namespace cwxeditor::lib {

enum class LayoutKind { Absolute, Fill, Row, Column, Grid, Form, Stack, Split, Scroll };
enum class Align { Start, Center, End, Stretch };

struct LayoutItem {
    std::size_t row{};
    std::size_t column{};
    std::size_t row_span{1};
    std::size_t column_span{1};
    Align horizontal{Align::Stretch};
    Align vertical{Align::Stretch};
    Size minimum{};
    Size preferred{};
    Insets margin{};
    float grow{};
    // 固定の論理表示領域内に子を意図的に配置する複合コントロール向けの
    // 明示的な指定（SWTのGridDataヒントに相当）。通常のフォ
    // ームではfalseのままとし、子孫の文字が切れないようにする。
    bool constrain_minimum_to_preferred{};
};

class Layout {
public:
    virtual ~Layout() = default;
    [[nodiscard]] virtual LayoutKind kind() const noexcept = 0;

    Insets padding{};
    float spacing{};
};

class FillLayout final : public Layout {
public:
    [[nodiscard]] LayoutKind kind() const noexcept override { return LayoutKind::Fill; }
};

// 各子の明示的なWidget::boundsを変更しない。RadarSpinner
// などの独自コントロールは極座標から子の位置を計算
// するため、行やグリッドの配置では表現できない。
class AbsoluteLayout final : public Layout {
public:
    [[nodiscard]] LayoutKind kind() const noexcept override {
        return LayoutKind::Absolute;
    }
};

class LinearLayout final : public Layout {
public:
    explicit LinearLayout(Orientation value = Orientation::Vertical) : orientation(value) {}
    [[nodiscard]] LayoutKind kind() const noexcept override {
        return orientation == Orientation::Horizontal ? LayoutKind::Row : LayoutKind::Column;
    }

    Orientation orientation{Orientation::Vertical};
    Align cross_axis{Align::Stretch};
};

class GridLayout final : public Layout {
public:
    [[nodiscard]] LayoutKind kind() const noexcept override { return LayoutKind::Grid; }
    std::size_t columns{1};
    bool equal_width{};
};

class FormLayout final : public Layout {
public:
    [[nodiscard]] LayoutKind kind() const noexcept override { return LayoutKind::Form; }
};

class StackLayout final : public Layout {
public:
    [[nodiscard]] LayoutKind kind() const noexcept override { return LayoutKind::Stack; }
    std::size_t active_index{};
};

using LayoutPtr = std::shared_ptr<Layout>;

}  // namespace cwxeditor::lib
