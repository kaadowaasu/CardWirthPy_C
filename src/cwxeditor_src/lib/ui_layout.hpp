#pragma once

#include "components.hpp"

#include <optional>
#include <span>
#include <vector>

namespace cwxeditor::lib {

// 画素単位の表示領域の共通移動処理。計測したドキュメントの範囲に
// 制限し、描画とポインターの座標が一致するように子孫を配置する。
bool scroll_view_content(const std::shared_ptr<ScrollView>& scroll, Point offset);

struct UiLayoutParameters final {
    // モデルのサイズはすべて論理画素単位。表示側は最終的なデバ
    // イス倍率だけを提供し、別の配置アルゴリズムを選ばない。
    float scale{1.0F};
    float split_spacing{7.0F};
    float cool_bar_gripper{9.0F};
    // ネイティブのデスクトップメニューは非クライアント領域を占める。単一描画面のホストは同じMenuBar
    // モデルをクライアント領域内に描く。この違いはホストのパラメーターだけで扱う。
    bool reserve_menu_bar{true};
};

struct UiPlacement final {
    WidgetPtr widget;
    Rect bounds{};
    bool visible{true};
};

// 受け渡し方式やウィジェットに依存しない中央寄せ・アンカ
// ーの配置計画。アダプターと互換配置はそれぞれの計測値を
// 渡し、推奨サイズと子の配置にこの共通の計算規則を使う。
struct CenterLayoutAnchors final {
    std::optional<float> top;
    std::optional<float> right;
    std::optional<float> bottom;
    std::optional<float> left;
};

struct CenterLayoutItemRequest final {
    Rect client{};
    Size preferred{};
    float margin{};
    bool center_horizontal{true};
    bool center_vertical{true};
    bool fill_horizontal{};
    bool fill_vertical{};
    std::optional<CenterLayoutAnchors> anchors;
    std::optional<Rect> explicit_bounds;
};

[[nodiscard]] Size measure_center_layout(std::span<const Size> children,
                                         float margin,
                                         float width_hint = -1.0F,
                                         float height_hint = -1.0F) noexcept;
[[nodiscard]] Size resolve_center_layout_preferred_size(
    Size preferred, Size minimum, Size fallback) noexcept;
[[nodiscard]] Rect plan_center_layout_item(
    const CenterLayoutItemRequest& request) noexcept;

struct CoolBarLayout final {
    Size size{};
    std::vector<UiPlacement> placements;
};

// 共通化したネイティブ枠寸法。すべての表示側で同じ論理的なつまみ寸法を使
// う。OSのrebarの厳密な余白に合わせてUIの規則を分岐させない。
[[nodiscard]] Size cool_bar_item_preferred_size(const CoolBar* bar, Size content) noexcept;
[[nodiscard]] CoolBarLayout layout_cool_bar(
    const CoolBar& bar, Rect available, UiLayoutParameters parameters = {});
[[nodiscard]] Rect cool_bar_grip_bounds(const Widget& child,
                                       UiLayoutParameters parameters = {}) noexcept;
[[nodiscard]] std::optional<std::size_t> cool_bar_item_at(
    const CoolBar& bar, Point point, bool grip_only = false,
    UiLayoutParameters parameters = {}) noexcept;

// ポップアップの描画とポインター・タッチのヒット判定で同じ座標情報を使う。組み込
// みウィンドウの原点も含め、配置済みのコントロールと表示領域の座標に合わせる。
struct ComboPopupLayout final {
    Rect bounds{};
    Rect content{};
    std::optional<Rect> scroll_track{};
    float row_height{};
    std::size_t visible_rows{};
    std::vector<float> action_widths;

    [[nodiscard]] Rect row_bounds(std::size_t visible_row) const noexcept;
    [[nodiscard]] Rect action_bounds(std::size_t visible_row, std::size_t action) const noexcept;
    [[nodiscard]] std::optional<std::size_t> item_at(
        Point point, std::size_t top, std::size_t item_count) const noexcept;
};

[[nodiscard]] ComboPopupLayout layout_combo_popup(
    const ComboBox& combo, Rect viewport, std::size_t maximum_rows = 9U,
    UiLayoutParameters parameters = {}) noexcept;

// 先頭表示行がtopのポップアップについて、ComboPopupLayout::scroll_track
// 内のスクロールつまみを返す。描画とポインタードラッグで共有し、つか
// む位置を描いたつまみに正確に合わせる。スクロールしない場合は空。
[[nodiscard]] std::optional<Rect> combo_popup_scroll_thumb(
    const ComboPopupLayout& layout, std::size_t item_count, std::size_t top) noexcept;

struct MenuPopupRow final {
    std::size_t index{};
    Rect bounds{};
    Rect check{};
    Rect icon{};
    Rect label{};
    Rect shortcut{};
    Rect arrow{};
};
struct MenuPopupLayout final {
    Rect bounds{};
    std::vector<MenuPopupRow> rows;
    [[nodiscard]] std::optional<std::size_t> item_at(Point point) const noexcept;
};
[[nodiscard]] MenuPopupLayout layout_menu_popup(
    const Menu& menu, Point anchor, Rect viewport, std::size_t top = 0U,
    std::optional<Rect> parent_row = std::nullopt,
    UiLayoutParameters parameters = {});

// 計測と配置では、同じ暗黙の配置とタイトル部・
// 分割部の余白を解決する。値は表示側の座標単
// 位で、推奨サイズの計測時はscale=1。
struct UiContainerLayout final {
    LayoutKind kind{LayoutKind::Column};
    float spacing{};
    Insets padding{};
};

[[nodiscard]] UiContainerLayout resolve_ui_container_layout(
    const Container& container, UiLayoutParameters parameters = {}) noexcept;

enum class SplitResizePhase {
    Preview,
    Commit,
};

struct SplitSashHit final {
    std::size_t index{};
    float boundary{};
    float gap{};
};

[[nodiscard]] std::vector<UiPlacement> layout_ui_children(
    const std::shared_ptr<Container>& container,
    Rect available,
    UiLayoutParameters parameters = {}, bool* completed = nullptr);
// 計算が無効になった場合や再入した場合はcompletedがfalseになる。この
// 場合、計算した子の矩形やScrollViewの寸法・位置は確定しない。on_layout
// はアプリケーションのコードであり、そこで行った変更自体は巻き戻さない。

// タブヘッダーと内容の余白は共通UIの契約に含める。表示
// 側はこの矩形を拡大縮小してよいが、ネイティブツールキッ
// トから別のページ座標を取得しない。計測入力はすべて所
// 有し、プロバイダーが使用中のページやフォントを置換し
// ても、この不変の入力を配置と表示に使えるようにする。
struct TabLayoutSnapshot {
    Rect bounds{};
    std::optional<FontSpec> font;
    std::vector<TabPage> pages;
    std::optional<std::size_t> selected_index;
    TabHeaderViewport header_viewport;
    float header_horizontal_padding{-1.0F};
    float chrome_inset{-1.0F};
    bool header_visible{true};
    bool enabled{}, visible{};
    std::uint64_t revision{}, text_scale_generation{};
    std::weak_ptr<const int> lifetime;
    float header_vertical_extra{};
};
[[nodiscard]] TabLayoutSnapshot snapshot_tab_layout(const TabView& tabs);
[[nodiscard]] bool tab_layout_is_current(const TabLayoutSnapshot& snapshot,
                                         const TabView& tabs) noexcept;
[[nodiscard]] Rect layout_tab_content(const TabLayoutSnapshot& snapshot,
    UiLayoutParameters parameters = {}) noexcept;
[[nodiscard]] Rect layout_tab_content(
    Rect tab_bounds,
    UiLayoutParameters parameters = {}, const FontSpec* font = nullptr) noexcept;
[[nodiscard]] Rect layout_tab_content(const TabView& tabs,
    UiLayoutParameters parameters = {});

struct TabHeaderItem {
    std::size_t index{};
    Rect bounds{};
    Rect close{};
};
struct TabHeaderLayout {
    std::vector<TabHeaderItem> items;
    std::vector<float> natural_widths;
    Rect content{};
    Rect previous{};
    Rect next{};
    bool can_previous{};
    bool can_next{};
};
[[nodiscard]] TabHeaderLayout layout_tab_headers(
    const TabView& tabs, UiLayoutParameters parameters = {});
[[nodiscard]] TabHeaderLayout layout_tab_headers(
    const TabLayoutSnapshot& snapshot, UiLayoutParameters parameters = {});

// 全表示側で同じ可視の子の順序と判定帯を使い、ポインタ
// ー下の分割境界を解決する。座標系はlayout_ui_children()
// が生成する子の矩形と同じ。
[[nodiscard]] std::optional<SplitSashHit> split_sash_at(
    const SplitView& split,
    float x,
    float y,
    float minimum_hit_radius = 5.0F) noexcept;

// 分割境界の座標を共通のSplitViewモデルへ適用する。インデック
// スはlayout_ui_children()と同じ可視の子の順序。表
// 示側はポインター座標だけを渡し、この意味上の遷移の後に再配置する。
[[nodiscard]] bool resize_split_pair_at(SplitView& split,
                                        std::size_t index,
                                        float coordinate,
                                        float minimum_size = 24.0F);
[[nodiscard]] bool resize_split_pair_at(SplitView& split,
                                        std::size_t index,
                                        float coordinate,
                                        float minimum_size,
                                        SplitResizePhase phase);
[[nodiscard]] bool resize_split_pair_at(SplitView& split,
                                        std::size_t index,
                                        float coordinate,
                                        float minimum_first,
                                        float minimum_second,
                                        SplitResizePhase phase);

void commit_split_resize(SplitView& split);

// 共通の配置モデルを再帰的に適用する。非アクティブなページや折りたた
// まれた内容を、個々の表示側が可視コントロールとして配置しない。false
// は再入したタブ更新により配置が無効になったことを表す。座標がす
// でに変わっていても、その状態を完成フレームとして公開しない。入
// 力処理側は再描画を要求し、フレーム準備側はこの結果を確認する。
bool arrange_widget(const WidgetPtr& widget, Rect bounds);
bool arrange_container(const std::shared_ptr<Container>& container, Rect bounds);
[[nodiscard]] bool scroll_window_content(const std::shared_ptr<Window>& window,
                                         Point offset);
void reveal_window_control(const WidgetPtr& scope, const WidgetPtr& control);

}  // namespace cwxeditor::lib
