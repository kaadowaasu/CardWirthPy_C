#include "cwxeditor_src/gui/cwx/editor/gui/dwt/splitpane.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace cwxeditor::cwx::editor::gui::dwt::splitpane {

SplitPane::SplitPane(const Style style, MeasureCallback measure)
    : style_(style), measure_(std::move(measure)) {
    orientation = vertical() ? lib::Orientation::Vertical
                             : lib::Orientation::Horizontal;
    // D版の SSelL と同様、子の推奨サイズとは独立して端の10px/0px
    // の境界を使う。 共通の配置経路でもその方針を維持する。
    use_explicit_edge_minimums = true;
    drag_minimum_first = drag_minimum_second = static_cast<float>(MIN);
    resize_mode = lib::SplitResizeMode::PreserveFirst;
    // D版の ResizeL と同様、初期の最大化配置の後で、保存済みの重みを一度再適用する。
    reflow_on_initial_maximized_geometry = true;
    on_weights_previewed = [this](const std::vector<float>& values) {
        if (values.size() != 2U) return;
        committed_split_layout.reset();
        weights_ = {static_cast<std::int32_t>(std::lround(values[0])),
                    static_cast<std::int32_t>(std::lround(values[1]))};
        min1_ = weights_[0] <= 0;
        min2_ = weights_[1] <= 0;
    };
    on_drag_committed = [this] {
        // 両ペインを有効・表示状態に保つ。D版の Sash は
        // 片側を最小化しても操作でき、同じ仕切りで再展開できる
        // 。 共通の配置では、実際の寸法ゼロで折り畳みを表す。
        notifySelectionListeners();
    };
}

SplitPane::SplitPane(const lib::Orientation value, MeasureCallback measure)
    : SplitPane(value == lib::Orientation::Vertical ? Vertical : Horizontal,
                std::move(measure)) {}

void SplitPane::set_measure_callback(MeasureCallback measure) {
    measure_ = std::move(measure);
}

lib::Size SplitPane::measuredSize(const lib::Widget& child) const {
    if (measure_) return measure_(child);
    return {child.bounds().width, child.bounds().height};
}

void SplitPane::publishWeights(const bool notify) {
    lib::SplitView::weights.clear();
    lib::SplitView::weights.reserve(weights_.size());
    for (const auto weight : weights_) {
        lib::SplitView::weights.push_back(static_cast<float>(weight));
    }
    if (notify && on_weights_changed) on_weights_changed(lib::SplitView::weights);
}

Weights SplitPane::computeWeights() const {
    const auto& children = this->children();
    if (children.size() >= 2 && children[0] && children[1]) {
        const auto left = measuredSize(*children[0]);
        // 元のD版が cs[0].computeSize(...).x
        // を二度読む動作を維持する。 両方の値に最初
        // の子を使い、縦分割でもx方向の寸法を使う。
        return {static_cast<std::int32_t>(std::lround(left.width)),
                static_cast<std::int32_t>(std::lround(left.width))};
    }
    return {1, 1};
}

Weights SplitPane::getWeights() const {
    if (committed_split_layout) return {
        static_cast<std::int32_t>(std::lround(committed_split_layout->first)),
        static_cast<std::int32_t>(std::lround(committed_split_layout->second))};
    return weights_;
}

lib::Rect SplitPane::sashBounds() const noexcept {
    if (committed_split_layout) {
        const auto& p=*committed_split_layout;
        return {p.sash_x,p.sash_y,p.sash_width,p.sash_height};
    }
    return sash_bounds_;
}

void SplitPane::adoptCommittedLayout() noexcept {
    if (!committed_split_layout) return;
    // すべての変更処理で、この二要素の保存領域を維持する。 その場で更新し、向
    // きを変えるときにメモリ確保なしで確定済みの配置を引き継げるようにする。
    if (weights_.size() != 2U) return;
    weights_[0]=static_cast<std::int32_t>(std::lround(committed_split_layout->first));
    weights_[1]=static_cast<std::int32_t>(std::lround(committed_split_layout->second));
    committed_sash_width_=sashWidth();
    sash_bounds_=sashBounds();
    size_=committed_split_layout->extent;
    sash_offset_=committed_split_layout->first;
    min1_=weights_[0]<=0; min2_=weights_[1]<=0;
    sash_initialized_=true;
    committed_split_layout.reset();
}

void SplitPane::setWeights(const Weights& values) {
    if (values.size() != 2) {
        throw std::invalid_argument("SplitPane weights length");
    }

    adoptCommittedLayout();
    committed_split_layout.reset();
    weights_ = values;
    min1_ = weights_[0] <= 0;
    min2_ = weights_[1] <= 0;
    // 共通のWindows配置は SplitView::weights を読
    // む。構築時にはまだ寸法がなく、 resize() から relo()
    // に到達できないため、基底の重みを空のままにしない。 空だとバックエ
    // ンドが両ペインを固定サイズの子と扱い、ダイアログの大部分が空になる。
    publishWeights(false);
    (void)resize(true);
}

SelectionToken SplitPane::addSelectionListener(SelectionListener listener) {
    const auto token = next_listener_token_++;
    listeners_.push_back({token, std::move(listener)});
    return token;
}

bool SplitPane::removeSelectionListener(const SelectionToken token) noexcept {
    const auto it = std::find_if(
        listeners_.begin(), listeners_.end(),
        [token](const ListenerEntry& entry) { return entry.token == token; });
    if (it == listeners_.end()) return false;
    listeners_.erase(it);
    return true;
}

void SplitPane::notifySelectionListeners(const SelectionEvent& base) {
    SelectionEvent event = base;
    event.sash_bounds = sashBounds();

    // リスナーが通知中に自身を解除できるよう、スナップショッ
    // トを使う。 各コールバックには同じイベント値を渡す。
    const auto listeners = listeners_;
    for (const auto& entry : listeners) {
        if (entry.callback) entry.callback(event);
    }
}

void SplitPane::notifySelectionListeners(const lib::PointerEvent& pointer) {
    SelectionEvent event;
    event.pointer = pointer;
    notifySelectionListeners(event);
}

lib::Size SplitPane::computeSize(const float width_hint, const float height_hint,
                                 const bool /*change*/) const {
    lib::Array<lib::Size> sizes;
    sizes.reserve(children().size());
    for (const auto& child : children()) {
        if (child) sizes.push_back(measuredSize(*child));
    }

    float width = 0.0F;
    float height = 0.0F;
    if (width_hint != default_hint) {
        width = width_hint;
    } else if (vertical()) {
        for (const auto& size : sizes) width = std::max(width, size.width);
    } else {
        for (const auto& size : sizes) width += size.width;
        width += sashWidth();
    }

    if (height_hint != default_hint) {
        height = height_hint;
    } else if (vertical()) {
        for (const auto& size : sizes) height += size.height;
        height += sashWidth();
    } else {
        for (const auto& size : sizes) height = std::max(height, size.height);
    }

    // lib::Widget にはSWTの computeTrim に相当
    // するものがなく、 境界はすでに共通のクライアント座標で表されている。
    return {width, height};
}

void SplitPane::initSash() {
    if (children().size() < 2) return;
    sash_initialized_ = true;

    const auto bounds = this->bounds();
    const float extent = vertical() ? bounds.height : bounds.width;
    sash_offset_ = (extent - sashWidth()) / 2.0F;
    if (extent <= 0.0F) sash_offset_ = 0.0F;
}

bool SplitPane::resize(const bool update_min) {
    adoptCommittedLayout();
    if (children().size() < 2) return false;
    if (!sash_initialized_) initSash();

    if (weights_.size() != 2) weights_ = {1, 1};
    // D版と同様、ゼロは未初期化の重みではなく、ペイ
    // ンの折り畳み状態。 共通の枠ドラッグは次の配置
    // 前にゼロを反映するため、 ここで計算し直すと直
    // ちに再展開され、仮想の仕切りが消えてしまう。
    if (weights_[0] < 0 || weights_[1] < 0 ||
        (weights_[0] == 0 && weights_[1] == 0 && !min1_ && !min2_))
        weights_ = computeWeights();

    auto left = static_cast<float>(weights_[0]);
    auto right = static_cast<float>(weights_[1]);
    auto full = left + right;
    if (full <= 0.0F) {
        left = 1.0F;
        right = 1.0F;
        full = 2.0F;
    }

    const auto bounds = this->bounds();
    const float extent = vertical() ? bounds.height : bounds.width;
    if (extent == 0.0F) return false;

    const float available = extent - sashWidth();
    sash_offset_ = static_cast<float>(std::lround(available * (left / full)));
    return relo(update_min, false);
}

bool SplitPane::handleResize() {
    adoptCommittedLayout();
    if (children().size() < 2) return false;
    if (!sash_initialized_) return resize(false);
    return relo(false, true);
}

bool SplitPane::layout(const bool update_min) {
    return update_min ? resize(true) : handleResize();
}

bool SplitPane::relo(const bool update_min, const bool resize_event) {
    if (!sash_initialized_ || children().size() < 2) return false;

    const auto bounds = this->bounds();
    const float extent = vertical() ? bounds.height : bounds.width;
    if (update_min) {
        min1_ = false;
        min2_ = false;
    }

    float offset = sash_offset_;
    if (resize_control1_ && resize_event && size_ != -1.0F) {
        offset += extent - size_;
    }

    if (min1_ || offset < static_cast<float>(MIN)) {
        if (update_min && can_minimized1_) min1_ = true;
        offset = min1_ ? 0.0F : static_cast<float>(MIN);
    }
    if (min2_ || offset + sashWidth() >=
                     extent - static_cast<float>(MIN)) {
        if (update_min && can_minimized2_) min2_ = true;
        offset = extent - sashWidth() -
                 (min2_ ? 0.0F : static_cast<float>(MIN));
    }

    sash_offset_ = offset;
    const float sash_width = sashWidth();
    const auto first = children()[0];
    const auto second = children()[1];

    if (vertical()) {
        sash_bounds_ = {0.0F, offset, bounds.width, sash_width};
        first->set_bounds({0.0F, 0.0F, bounds.width, offset});
        second->set_bounds({0.0F, offset + sash_width, bounds.width,
                            bounds.height - offset - sash_width});
    } else {
        sash_bounds_ = {offset, 0.0F, sash_width, bounds.height};
        first->set_bounds({0.0F, 0.0F, offset, bounds.height});
        second->set_bounds({offset + sash_width, 0.0F,
                            bounds.width - offset - sash_width, bounds.height});
    }

    // 共通モデルでは表示状態を折り畳み状態として使わない。 両ペインを有効な
    // まま保ち、0pxでのサイズ変更・履歴適用をまたいでも仕切りを維持する。
    first->set_visible(true);
    second->set_visible(true);
    if (min1_) weights_[0] = 0;
    if (min2_) weights_[1] = 0;

    size_ = extent;
    refreshWeights();
    return true;
}

void SplitPane::refreshWeights() {
    committed_split_layout.reset();
    if (children().size() < 2 || !sash_initialized_) return;

    const auto bounds = this->bounds();
    const float extent = vertical() ? bounds.height : bounds.width;
    const auto first_weight = static_cast<std::int32_t>(std::lround(sash_offset_));
    const auto second_weight = static_cast<std::int32_t>(std::lround(
        extent - sash_offset_ - sashWidth()));

    const bool changed = weights_.size() != 2 || weights_[0] != first_weight ||
                         weights_[1] != second_weight;
    weights_ = {first_weight, second_weight};
    publishWeights(changed);
}

bool SplitPane::setSashPosition(const float coordinate,
                                const lib::PointerEvent* source) {
    adoptCommittedLayout();
    if (children().size() < 2) return false;
    initial_first_extent.reset();
    initial_second_extent.reset();
    initial_first_extent_offset.reset();
    if (!sash_initialized_) initSash();

    const auto bounds = this->bounds();
    const float extent = vertical() ? bounds.height : bounds.width;
    const float min_size1 = can_minimized1_ ? 0.0F : static_cast<float>(MIN);
    const float min_size2 = can_minimized2_ ? 0.0F : static_cast<float>(MIN);
    const float right = extent - sashWidth() - min_size2;
    // std::clamp ではなく、D版の順序付きの二回の比較を維
    // 持する。 領域が両最小値より小さい場合、下限が上限を超えていて
    // も、 元の処理は意図的に最初のペインの最小値を最終値とする。
    float clamped = coordinate;
    if (right < clamped) clamped = right;
    if (min_size1 > clamped) clamped = min_size1;
    if (clamped == sash_offset_) return false;

    sash_offset_ = clamped;
    if (!relo(true, false)) return false;

    if (source) {
        SelectionEvent event;
        event.pointer = *source;
        notifySelectionListeners(event);
    } else {
        notifySelectionListeners();
    }
    return true;
}

bool SplitPane::handlePointer(const lib::PointerEvent& event) {
    if (event.action != lib::PointerAction::Down &&
        event.action != lib::PointerAction::Move) {
        return false;
    }
    return setSashPosition(vertical() ? event.position.y : event.position.x,
                           &event);
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::splitpane
