#pragma once

#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/events.hpp"
#include "cwxeditor_src/lib/portable.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::splitpane {

/// DのSWTスタイルに対応する、共通の向き指定ビット。
using Style = std::uint32_t;
inline constexpr Style None = 0U;
inline constexpr Style Horizontal = 1U << 0U;
inline constexpr Style Vertical = 1U << 1U;

/// 分割境界のドラッグ・明示通知後に出す選択情報。 合成通知で
/// はポインターイベントを省略できるが、sash_bounds
/// は常に現在のSplitPane内座標の境界矩形とする。
struct SelectionEvent {
    lib::PointerEvent pointer{};
    lib::Rect sash_bounds{};
    bool doit{true};
};

using SelectionListener = lib::Callback<void(SelectionEvent&)>;
using SelectionToken = std::size_t;
using Weights = lib::Array<std::int32_t>;

/// Dの拡縮・最小化・ドラッグ動作を持つ二分割コンテナ
/// ー。 lib::SplitViewをコンテナーと
/// 向きの共通表現に使う。SWTのSashはsashBounds()
/// で表し、 children()へは追加せ
/// ず、子を利用者の二つのペインだけに保つ。
class SplitPane : public lib::SplitView {
public:
    inline static constexpr std::int32_t SASH_WIDTH = 5;
    inline static constexpr std::int32_t MIN = 10;
    static constexpr float default_hint = -1.0F;

    using MeasureCallback = lib::Callback<lib::Size(const lib::Widget&)>;

    explicit SplitPane(Style style = Horizontal,
                       MeasureCallback measure = {});
    explicit SplitPane(lib::Orientation orientation,
                       MeasureCallback measure = {});

    [[nodiscard]] lib::ComponentKind kind() const noexcept override {
        return lib::ComponentKind::SplitView;
    }

    [[nodiscard]] Style getStyle() const noexcept { return style_; }
    // 既存ペインの向きが変わったら、共通の配置
    // とD形式の分割境界計算を同じ軸へ揃える。
    void setOrientation(lib::Orientation value) noexcept {
        adoptCommittedLayout();
        sash_initialized_ = false;
        sash_bounds_ = {};
        size_ = -1.0F;
        orientation = value;
        style_ = (style_ & ~(Horizontal | Vertical)) |
                 (value == lib::Orientation::Vertical ? Vertical : Horizontal);
    }

    [[nodiscard]] bool canMinimized1() const noexcept {
        return can_minimized1_;
    }
    void canMinimized1(bool value) noexcept {
        can_minimized1_ = value;
        drag_minimum_first = value ? 0.0F : static_cast<float>(MIN);
    }

    [[nodiscard]] bool canMinimized2() const noexcept {
        return can_minimized2_;
    }
    void canMinimized2(bool value) noexcept {
        can_minimized2_ = value;
        drag_minimum_second = value ? 0.0F : static_cast<float>(MIN);
    }

    [[nodiscard]] bool resizeControl1() const noexcept {
        return resize_control1_;
    }
    void resizeControl1(bool value) noexcept {
        resize_control1_ = value;
        resize_mode = value ? lib::SplitResizeMode::PreserveSecond : lib::SplitResizeMode::PreserveFirst;
    }

    [[nodiscard]] Weights getWeights() const;
    void setWeights(const Weights& values);

    /// リスナーを登録し、解除用の固定トークンを返す。
    [[nodiscard]] SelectionToken addSelectionListener(
        SelectionListener listener);
    bool removeSelectionListener(SelectionToken token) noexcept;

    /// Dのnotify相当の、合成された選択イベントを送る。
    void notifySelectionListeners(const SelectionEvent& base = {});
    void notifySelectionListeners(const lib::PointerEvent& pointer);

    void set_measure_callback(MeasureCallback measure);

    /// 子の推奨サイズと現在の指定値から、ペインの寸法を再計算する。
    [[nodiscard]] lib::Size computeSize(float width_hint = default_hint,
                                        float height_hint = default_hint,
                                        bool change = true) const;
    [[nodiscard]] lib::Size compute_size(
        float width_hint = default_hint, float height_hint = default_hint,
        bool change = true) const {
        return computeSize(width_hint, height_hint, change);
    }

    /// 現在の分割位置を二つの子へ適用する。 子が二つない場
    /// 合、または主軸の寸法が0の場合はfalseを返す。
    bool resize(bool update_min = false);
    /// resizeControl1()が有効なら2番目のペインを保ち、
    /// コンテナーのサイズ変更を適用する。 初回は重みから初期化する。
    bool handleResize();
    bool layout(bool update_min = false);

    /// 仮想の分割境界へのポインター移動・押下を処理し、
    /// 補正後の位置が変わったらリスナーへ通知する。
    bool handlePointer(const lib::PointerEvent& event);

    /// 両ペインの最小サイズを守りながら、主軸方向へ分割境界を動かす。
    bool setSashPosition(float coordinate,
                         const lib::PointerEvent* source = nullptr);

    [[nodiscard]] lib::Rect sashBounds() const noexcept;
    [[nodiscard]] bool minimized1() const noexcept { return committed_split_layout ? committed_split_layout->first<=0.0F : min1_; }
    [[nodiscard]] bool minimized2() const noexcept { return committed_split_layout ? committed_split_layout->second<=0.0F : min2_; }

private:
    struct ListenerEntry {
        SelectionToken token{};
        SelectionListener callback;
    };

    [[nodiscard]] bool vertical() const noexcept {
        return (style_ & Vertical) != 0U;
    }
    [[nodiscard]] lib::Size measuredSize(const lib::Widget& child) const;
    [[nodiscard]] Weights computeWeights() const;
    [[nodiscard]] bool relo(bool update_min, bool resize_event);
    void refreshWeights();
    void adoptCommittedLayout() noexcept;
    [[nodiscard]] float sashWidth() const noexcept {
        if (committed_split_layout)
            return vertical() ? committed_split_layout->sash_height : committed_split_layout->sash_width;
        const auto bounds=sashBounds();
        const auto gap=vertical() ? bounds.height : bounds.width;
        const auto cross=vertical() ? bounds.width : bounds.height;
        return gap>=0.0F && (gap>0.0F || cross>0.0F) ? gap
            : committed_sash_width_.value_or(static_cast<float>(SASH_WIDTH));
    }
    void initSash();
    void publishWeights(bool notify);

    Style style_{};
    MeasureCallback measure_;
    Weights weights_{0, 0};
    std::vector<ListenerEntry> listeners_;
    SelectionToken next_listener_token_{1};

    bool sash_initialized_{};
    bool can_minimized1_{};
    bool can_minimized2_{};
    bool min1_{};
    bool min2_{};
    bool resize_control1_{};
    float size_{-1.0F};
    float sash_offset_{};
    lib::Rect sash_bounds_{};
    std::optional<float> committed_sash_width_;
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::splitpane
