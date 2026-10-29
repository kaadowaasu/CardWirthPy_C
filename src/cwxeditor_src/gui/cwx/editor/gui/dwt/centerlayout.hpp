#pragma once

#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/layout.hpp"
#include "cwxeditor_src/lib/portable.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <utility>
#include <variant>

namespace cwxeditor::cwx::editor::gui::dwt::centerlayout {

/// CenterLayoutで子を各軸いっぱいに広げるための、子ごとのフラグ。
struct CenterLayoutData {
    bool fillVertical{};
    bool fillHorizontal{};

    CenterLayoutData() = default;
    CenterLayoutData(bool fillVertical, bool fillHorizontal)
        : fillVertical(fillVertical), fillHorizontal(fillHorizontal) {}
};

/// 子の固定位置に使う四辺の値。-1はSWT.DEFAULT
/// 相当で、その辺の未指定を表す。
struct Insets {
    static constexpr std::int32_t default_value = -1;

    std::int32_t n{};
    std::int32_t e{};
    std::int32_t s{};
    std::int32_t w{};

    constexpr Insets() = default;
    constexpr Insets(std::int32_t north, std::int32_t east,
                     std::int32_t south, std::int32_t west)
        : n(north), e(east), s(south), w(west) {}

    /// libの標準の余白順（上・右・下・左）から変換する。
    [[nodiscard]] static Insets from(const lib::Insets& value) noexcept;
    [[nodiscard]] lib::Insets to_lib() const noexcept;

    /// 全辺を未指定にした余白。一部の辺だけ分かる場合に使う。
    [[nodiscard]] static constexpr Insets unspecified() noexcept {
        return {default_value, default_value, default_value, default_value};
    }
};

/// CenterLayoutが受け付ける子の配置
/// データ。DのPointはlib::Point
/// で表す。 共通C++の寸法型を使う呼び出し元
/// には、同等の推奨サイズとしてlib::Size
/// も認める。 配置にはRectangleを優
/// 先し、Insetsは辺の制約を指定する。
using LayoutData = std::variant<std::monostate, lib::Point, lib::Size,
                                lib::Rect, Insets, CenterLayoutData>;

/// CenterLayoutが使うDのSWTスタイルに対応する、軸のビット。
using Style = std::uint32_t;
inline constexpr Style None = 0U;
inline constexpr Style Horizontal = 1U << 0U;
inline constexpr Style Vertical = 1U << 1U;
inline constexpr Style Both = Horizontal | Vertical;

/// lib::Containerの子を中央配置、または領
/// 域いっぱいに広げる共通レイアウト。 元のDWTはSWT
/// から推奨サイズを得るが、共通層にはOSの計測API
/// がないため、計測コールバックを渡せる。 省略時は子
/// の現在の範囲を推奨サイズとする。子固有のPoint/Size
/// のLayoutDataがあれば、 DのPoint
/// 配置データと同じく、その値を優先する。
class CenterLayout : public lib::Layout {
public:
    static constexpr float default_hint = -1.0F;

    using MeasureCallback = lib::Callback<lib::Size(const lib::Widget&)>;

    explicit CenterLayout(Style style = Both, std::int32_t margin = 5,
                          MeasureCallback measure = {});

    [[nodiscard]] lib::LayoutKind kind() const noexcept override {
        return lib::LayoutKind::Stack;
    }

    bool fillVertical{};
    bool fillHorizontal{};

    [[nodiscard]] std::int32_t margin() const noexcept { return margin_; }
    [[nodiscard]] Style style() const noexcept { return style_; }

    void set_measure_callback(MeasureCallback measure);

    /// このレイアウトの子へ、D相当の配置データを関連付ける。
    void set_layout_data(const lib::Widget& child, LayoutData data);
    void setLayoutData(const lib::Widget& child, LayoutData data) {
        set_layout_data(child, std::move(data));
    }
    void clear_layout_data(const lib::Widget& child) noexcept;
    void clearLayoutData(const lib::Widget& child) noexcept {
        clear_layout_data(child);
    }
    [[nodiscard]] const LayoutData* layout_data(
        const lib::Widget& child) const noexcept;
    [[nodiscard]] const LayoutData* getLayoutData(
        const lib::Widget& child) const noexcept {
        return layout_data(child);
    }

    /// 必要サイズを求める。SWT.DEFAULTの指定だけを0へ置き換える。
    [[nodiscard]] lib::Size compute_size(
        const lib::Container& composite, float width_hint = default_hint,
        float height_hint = default_hint, bool flush_cache = false) const;
    [[nodiscard]] lib::Size computeSize(
        const lib::Container& composite, float width_hint = default_hint,
        float height_hint = default_hint, bool flush_cache = false) const {
        return compute_size(composite, width_hint, height_hint, flush_cache);
    }

    /// コンテナーの範囲をクライアント領域として使う。
    void layout(lib::Container& composite, bool flush_cache = false) const;

    /// 明示されたクライアント領域へ子を配置する。
    void layout(lib::Container& composite, const lib::Rect& client_area,
                bool flush_cache = false) const;

private:
    [[nodiscard]] lib::Size measured_size(const lib::Widget& child) const;
    [[nodiscard]] lib::Size child_size(const lib::Widget& child) const;

    std::int32_t margin_{};
    Style style_{};
    MeasureCallback measure_;
    std::unordered_map<const lib::Widget*, LayoutData> layout_data_;
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::centerlayout
