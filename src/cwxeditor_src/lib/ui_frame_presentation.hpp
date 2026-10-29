#pragma once
#include "ui_edit_input.hpp"
#include "drawing_commands.hpp"
#include "ui_image_presentation.hpp"
#include "ui_popup_presentation.hpp"
#include "ui_slider.hpp"
#include "ui_item_presentation.hpp"
#include "ui_chrome_presentation.hpp"
#include "ui_strip_presentation.hpp"
#include "ui_container_presentation.hpp"
#include "ui_pointer_feedback.hpp"
#include "ui_layout.hpp"
#include <unordered_map>

namespace cwxeditor::lib {
// アプリケーションのコールバックをまたいで、描画中のツリー
// を保持する。構造が変わった場合はこのフレームを無効にし、
// 古い配置と新しいコントロールが混ざった画面を公開しない。
class FrameStructureSnapshot {
public:
    void capture(const std::shared_ptr<Window>& window);
    [[nodiscard]] bool current() const;
private:
    struct Entry {
        WidgetPtr widget;
        Rect bounds;
        bool visible{}, enabled{};
        std::optional<std::string> text;
        std::optional<std::string> title;
        std::vector<WidgetPtr> children;
        std::optional<FontSpec> font;
        std::optional<Color> foreground, background;
        CursorKind cursor{CursorKind::Default};
        std::optional<std::vector<ListItem>> list_items;
        std::optional<std::vector<TableColumn>> table_columns;
        std::optional<std::vector<TableRow>> table_rows;
        std::optional<std::vector<TreeNode>> tree_nodes;
        std::vector<std::size_t> index_selection;
        std::optional<std::size_t> index_anchor, index_cursor;
        std::vector<TreeItemId> tree_selection;
        std::optional<TreeItemId> tree_anchor, tree_cursor;
        std::size_t top_index{};
        float horizontal_offset{};
        float row_height{}, header_height{};
        std::optional<std::size_t> highlighted_column, inline_edit_column;
        bool multi_select{}, checkable{}, full_row_selection{}, virtualized{};
        bool check_selected_rows{}, edit_on_double_click{}, header_visible{}, grid_lines{};
        bool show_expanders{}, flush_root_left{};
        std::uint64_t model_update_revision{}, model_order_revision{}, scroll_revision{};
        std::optional<TabLayoutSnapshot> tab;
        std::uint64_t tab_page_update_revision{}, tab_page_order_revision{};
    };
    std::vector<Entry> entries_;
    float scale_{};
};
// 共有描画面は、順序付けされた一組のウィンドウを公開する。異
// なる構造の世代で別々に完成したウィンドウを混在させない。
class SurfaceFramePresentation {
public:
    explicit SurfaceFramePresentation(std::vector<std::shared_ptr<Window>> windows);
    [[nodiscard]] const std::vector<std::shared_ptr<Window>>& windows() const { return windows_; }
    bool append(const std::shared_ptr<Window>& window, FrameStructureSnapshot snapshot);
    [[nodiscard]] bool current(const std::vector<std::shared_ptr<Window>>& windows) const;
private:
    std::vector<std::shared_ptr<Window>> windows_;
    std::vector<FrameStructureSnapshot> frames_;
};
struct TextLinePresentation {
    Point origin;
    std::string text;
    std::optional<Rect> selection;
    DecorationPresentation decoration;
    std::optional<Rect> composition_underline;
};
struct TextFramePresentation {
    Rect inner;
    float line_height{};
    std::vector<TextVisualLine> lines;
    TextEditInteractionState state;
    bool focused{};
    std::optional<Rect> caret;
    Rect bounds;
    std::vector<TextLinePresentation> visible_lines;
    Color foreground;
    Color selection_foreground{255,255,255,255};
    DecorationPresentation decoration, caret_decoration;
    ItemScrollGeometry scroll;
    DecorationPresentation scroll_decoration;
};
struct NumberFramePresentation {
    NumberEditLayout geometry;
    std::string text;
    bool increase_enabled{}, decrease_enabled{};
    std::shared_ptr<TextEdit> editor;
    DecorationPresentation decoration;
    std::vector<DecorationTextPresentation> labels;
};
struct ComboControlPresentation {
    ComboBoxLayout geometry;
    std::string text;
    ResourceId icon{};
    Rect icon_bounds{};
    std::shared_ptr<TextEdit> editor;
    DecorationPresentation decoration;
    std::vector<DecorationTextPresentation> labels;
    bool editable{};
    bool enabled{};
    std::string empty_hint;
};
// ホストは単調増加のミリ秒時刻を提供し、独自の点滅・アニメーション規則は持たない
// 。期限がなければ、完成した静止フレームはタイマーによる再描画を必要としない。
struct FrameAnimationSchedule {
    std::optional<std::uint64_t> next_update;
    [[nodiscard]] bool due(std::uint64_t now) const { return next_update && now >= *next_update; }
    void request_tick(std::uint64_t now, std::uint64_t period);
    void request_at(std::uint64_t deadline);
};
struct ComboFramePresentation {
    std::shared_ptr<ComboBox> owner;
    PopupPresentation popup;
};
struct CanvasFramePresentation {
    WidgetPtr owner;
    Rect bounds;
    std::function<void(DrawingContext&,Rect)> paint;
    std::function<void(Rect)> legacy_paint;
    DrawingCommandList drawing;
    DecorationPresentation decoration;
};
struct ImageViewFramePresentation {
    ResourceId resource{};
    std::size_t requested_frame{};
    Rect bounds;
    ImageScaleMode scale_mode{ImageScaleMode::Fit};
    Interpolation interpolation{Interpolation::Linear};
    ImagePresentation drawing;
    ImageViewportPresentation viewport;
    DecorationPresentation decoration;
    std::vector<ImageLayerSpec> layers;
    std::vector<ImagePresentation> layer_drawing;
};
struct WindowFramePresentation {
    std::optional<WindowChromePresentation> shell;
    // 存在しないIDも含め、描画準備中に一度だけ解決した変更不
    // 可の世代。再生時には現在の描画資源プールを検索しない。
    std::unordered_map<ResourceId,std::shared_ptr<const ImageData>> resources;
    std::unordered_map<const ImageView*,ImageViewFramePresentation> images;
    std::vector<CanvasFramePresentation> canvases;
    bool drawing_prepared{};
    std::unordered_map<const Widget*,DecorationPresentation> decorations;
    std::shared_ptr<Window> window;
    FrameStructureSnapshot structure;
    std::uint64_t focus_revision{};
    std::optional<TextInputPresentation> text_input;
    std::unordered_map<const TextEdit*,TextFramePresentation> text;
    std::unordered_map<const NumberEdit*,NumberFramePresentation> numbers;
    std::unordered_map<const ComboBox*,ComboControlPresentation> combo_controls;
    std::unordered_map<const Widget*,ItemViewPresentation> item_views;
    std::unordered_map<const TabView*,TabViewPresentation> tabs;
    std::unordered_map<const Widget*,WidgetStylePresentation> styles;
    std::unordered_map<const Widget*,ChromePresentation> chrome;
    std::unordered_map<const Widget*,StripPresentation> strips;
    std::unordered_map<const Container*,ContainerPresentation> containers;
    std::optional<TooltipPresentation> tooltip;
    std::shared_ptr<TextEdit> inline_text;
    std::optional<Rect> inline_arrow;
    DecorationPresentation inline_decoration;
    std::optional<DecorationTextPresentation> inline_label;
    std::optional<NumberFramePresentation> inline_number;
    FrameAnimationSchedule animation;
    bool inline_editor{};
    // 有効なインライン編集部の所有元。Table/Treeのセルでは行内に描
    // き、それ以外の所有元（独自描画Canvasなど）では最前面に重ねる。
    const Widget* inline_owner{};
    std::optional<ComboFramePresentation> combo;
    std::vector<PopupPresentation> menus;
    // 空のTabViewのクライアント領域の描画は、通常のウィジェットCanvasとは別に
    // 記録する。FrameReplayでタブのクライアント領域のクリップ内に配置するため。
    std::unordered_map<const TabView*,CanvasFramePresentation> tab_empty_client_paints;
};
// Canvasのコールバックを呼ぶ前に、アイコンとImageViewの資
// 源を保持する。記録するCanvas命令はこの世代を再利用し、Canvas
// 専用のIDは最初の描画命令で保持する。存在しない資源は存在しないまま扱
// う。描画には準備済みの画像座標と記録済みの描画命令だけを使う。false
// なら構造的に無効なフレームを破棄し、部分的な命令列を公開しない。
bool prepare_frame_drawing(WindowFramePresentation& frame,
    const DrawingCaptureServices& services,const std::function<bool()>& live = {});
// 描画前に座標と一時状態を整合させる。ホストはグリフの計測値と
// 描画面の生存状態を提供し、描画は準備済みの結果を読む。再描画
// だけで編集の確定や選択コールバックを発生させない。実際の表示
// 側は共通のホバーツールチップセッションを渡す。nullは画
// 像テストなどで使う、即時表示の静的なヘルプ状態を要求する。
[[nodiscard]] std::optional<WindowFramePresentation> prepare_window_frame(
    EditInputContext context, MenuPopupStateMachine& menus,
    std::shared_ptr<Window> window, Rect content,
    const std::function<bool()>& live = {}, std::uint64_t now_ms = 0,
    const WidgetPtr& hovered = {}, Point pointer = {},
    const std::vector<PointerPressFeedback>& presses = {}, TooltipStateMachine* tooltip_state = nullptr);
} // namespace cwxeditor::lib
