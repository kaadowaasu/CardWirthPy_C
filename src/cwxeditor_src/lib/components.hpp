#pragma once

#include "events.hpp"
#include "layout.hpp"
#include "types.hpp"
#include "ui_image_presentation.hpp"
#include "window_host.hpp"

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cwxeditor::lib {

class DrawingContext;
class Container;

// 自身は入力部品でない、確定文字列の受信先。接続され
// たIncSearchが登録を所有し、 検索の再接続
// ・破棄、または親Widgetの破棄で無効にする。
struct CommittedTextInputRegistration {
    std::weak_ptr<const int> owner_lifetime;
    std::weak_ptr<const int> parent_lifetime;
    std::function<bool()> current;
    std::function<std::optional<Rect>()> anchor;
    std::function<bool(TextInputEvent&)> on_text_input;
    std::function<bool(KeyEvent&)> on_key_down;
};

// Widgetのコピーで引き継ぐのは表示情報だけで、実行中のネイティブ入力セッションは
// コピーしない。 新しい寿命情報にすることで、既存の登録が複製先へ追従するのも防ぐ。
struct WidgetCommittedTextInputState {
    std::shared_ptr<CommittedTextInputRegistration> registration;
    std::weak_ptr<CommittedTextInputRegistration> published_registration;
    std::shared_ptr<const int> lifetime{std::make_shared<const int>(0)};

    WidgetCommittedTextInputState() = default;
    WidgetCommittedTextInputState(const WidgetCommittedTextInputState&)
        : WidgetCommittedTextInputState() {}
    WidgetCommittedTextInputState& operator=(const WidgetCommittedTextInputState&) {
        lifetime = std::make_shared<const int>(0);
        registration.reset();
        published_registration.reset();
        return *this;
    }

    [[nodiscard]] bool published() const noexcept {
        const auto active = published_registration.lock();
        return active && active == registration &&
            !active->owner_lifetime.expired() && !active->parent_lifetime.expired();
    }
};

enum class ComponentKind {
    Window, Dialog, Panel, GroupBox, Label, Link, Button, ToggleButton,
    CheckBox, RadioButton, TextEdit, NumberEdit, ComboBox, ListView, TableView,
    TreeView, TabView, ToolBar, CoolBar, MenuBar, ContextMenu, StatusBar,
    Canvas, ImageView, ProgressBar, Slider, Separator, SplitView, ScrollView,
    ExpandBar, ToolTip, ColorWell, Spacer,
};

// 独自描画カードと共通一覧行で使うコメント印の配置。 画像
// カードでは右下の内側、ツリー行では可視クライアント端に
// 沿って縦中央に置き、Dのcomment.dへ合わせる。
enum class CommentMarkerPlacement { BottomRight, CenterRight };
[[nodiscard]] inline Rect comment_marker_bounds(
    const Rect item, const Size icon, const float inset,
    const CommentMarkerPlacement placement) noexcept {
    const float y = placement == CommentMarkerPlacement::CenterRight
        ? item.y + (item.height - icon.height) * 0.5F
        : item.y + item.height - icon.height - inset;
    return {item.x + item.width - icon.width - inset, y,
            icon.width, icon.height};
}
[[nodiscard]] inline bool comment_marker_contains(
    const Rect bounds, const Point point) noexcept {
    return bounds.width > 0.0F && bounds.height > 0.0F &&
           bounds.x <= point.x && point.x < bounds.x + bounds.width &&
           bounds.y <= point.y && point.y < bounds.y + bounds.height;
}
[[nodiscard]] inline std::string_view comment_tooltip_at(
    const std::string_view comment, const Point point,
    const Rect marker_bounds) noexcept {
    return !comment.empty() && comment_marker_contains(marker_bounds, point)
        ? comment : std::string_view{};
}

enum class CursorKind {
    Default, Arrow, Text, Hand, Move, Crosshair, Wait, Forbidden, SizeHorizontal,
    SizeVertical, SizeDiagonalNorthEast, SizeDiagonalNorthWest,
    // Dのnew Cursor(display, imageData, w / 2, h / 2)
    // 相当。 画像中央を基点とする画像カーソル（Widget::custom_cursor()）。
    Custom,
};

class Widget {
public:
    virtual ~Widget() = default;
    [[nodiscard]] virtual ComponentKind kind() const noexcept = 0;
    // 独自描画部品も、コンテナー配置と同じ計測経
    // 路で推奨寸法を返せる。 指定値は論理ピクセ
    // ルとし、省略時は自然なサイズを要求する。
    [[nodiscard]] virtual std::optional<Size> preferred_size_for_layout(
        std::optional<float>, std::optional<float>) const { return std::nullopt; }
    // 独自一覧は、ネイティブ入力の登録を引き継がずに、 現在
    // 見えている主選択行を共通の文字入力表示へ公開できる。
    [[nodiscard]] virtual bool supports_item_view_text_input() const noexcept {
        return false;
    }
    [[nodiscard]] virtual std::optional<Rect> item_view_text_input_anchor(
        float) const { return std::nullopt; }
    // DWTの逐次検索など一時的な子画面へフォーカスが移
    // っても、論理的な表が持つ編集は続けられる。 この弱
    // い参照で、共通のフォーカス処理に所有者を伝える。
    std::weak_ptr<Widget> focus_transaction_owner;
    // 逆リンクは既存Containerの共有の子配列に対応す
    // る。 所有権は変えず、ビューを一つの親に限定もしない。
    [[nodiscard]] const std::vector<Container*>& containing_parents() const noexcept {
        return parent_links_.values;
    }

    void set_id(ComponentId value) noexcept { id_ = value; }
    [[nodiscard]] ComponentId id() const noexcept { return id_; }
    void set_bounds(Rect value) noexcept { bounds_ = value; }
    [[nodiscard]] Rect bounds() const noexcept { return bounds_; }
    void set_visible(bool value) {
        if (visible_ == value) return;
        visible_ = value;
        const auto callback = on_visibility_changed;
        if (callback) callback(value);
    }
    [[nodiscard]] bool visible() const noexcept { return visible_; }
    void set_enabled(bool value) noexcept { enabled_ = value; }
    [[nodiscard]] bool enabled() const noexcept { return enabled_; }
    void set_tooltip(std::string value) { tooltip_ = std::move(value); }
    [[nodiscard]] const std::string& tooltip() const noexcept { return tooltip_; }
    void set_tooltip_balloon(bool value) noexcept { tooltip_balloon_ = value; }
    [[nodiscard]] bool tooltip_balloon() const noexcept { return tooltip_balloon_; }
    // 明示的な警告のヒントは、吹き出し形状によらずDのsetVisible(true)/setAutoHide(false)
    // に従う。 通常のホバーヘルプには共通タイマーを使う。
    void set_tooltip_immediate(bool value) noexcept { tooltip_immediate_ = value; }
    [[nodiscard]] bool tooltip_immediate() const noexcept { return tooltip_immediate_; }
    // 複合部品は、これらの汎用スタイルを子へ伝えられる。
    virtual void set_font(std::optional<FontSpec> value) { font_ = std::move(value); }
    [[nodiscard]] const std::optional<FontSpec>& font() const noexcept { return font_; }
    virtual void set_foreground(std::optional<Color> value) noexcept { foreground_ = value; }
    void set_background(std::optional<Color> value) noexcept { background_ = value; }
    void set_cursor(CursorKind value) noexcept { cursor_ = value; }
    // CursorKind::Customの画像カーソル。0なら既定カーソルへ戻す。
    void set_custom_cursor(ResourceId image) noexcept {
        custom_cursor_ = image;
        cursor_ = image ? CursorKind::Custom : CursorKind::Default;
    }
    [[nodiscard]] ResourceId custom_cursor() const noexcept { return custom_cursor_; }
    void set_reveal_on_focus(bool value) noexcept { reveal_on_focus_ = value; }
    [[nodiscard]] const std::optional<Color>& foreground() const noexcept { return foreground_; }
    [[nodiscard]] const std::optional<Color>& background() const noexcept { return background_; }
    [[nodiscard]] CursorKind cursor() const noexcept { return cursor_; }
    [[nodiscard]] bool reveal_on_focus() const noexcept { return reveal_on_focus_; }
    void set_layout_item(LayoutItem value) noexcept {
        layout_item_ = value;
        layout_item_explicit_ = true;
    }
    [[nodiscard]] const LayoutItem& layout_item() const noexcept { return layout_item_; }
    [[nodiscard]] bool has_explicit_layout_item() const noexcept {
        return layout_item_explicit_;
    }
    [[nodiscard]] bool has_explicit_grid_position() const noexcept {
        if (!layout_item_explicit_) return false;
        if (layout_item_.row != 0U || layout_item_.column != 0U ||
            layout_item_.row_span != 1U || layout_item_.column_span != 1U)
            return true;
        // LayoutItemには寸法・配置の制約も入る。 それらの指定
        // だけで、既定の行・列を明示的な(0,0)配置として扱わない。
        const bool has_non_position_data =
            layout_item_.horizontal != Align::Stretch ||
            layout_item_.vertical != Align::Stretch ||
            layout_item_.minimum.width != 0.0F ||
            layout_item_.minimum.height != 0.0F ||
            layout_item_.preferred.width != 0.0F ||
            layout_item_.preferred.height != 0.0F ||
            layout_item_.margin.top != 0.0F ||
            layout_item_.margin.right != 0.0F ||
            layout_item_.margin.bottom != 0.0F ||
            layout_item_.margin.left != 0.0F ||
            layout_item_.grow != 0.0F;
        // 他が既定値でも、項目自体を明示して渡し
        // た場合はGridの最初のセルを選ぶ。
        return !has_non_position_data;
    }

    // SWTのDisposeリスナーは部品に属する。共通のRAII
    // 接続をここへ保持し、 全バックエンドで破棄時に外す。外
    // 部の通知元に保存したコールバックから部品を保持しない。
    void retain_lifetime_binding(std::shared_ptr<void> binding) {
        if (binding) lifetime_bindings_.push_back(std::move(binding));
    }

    std::string accessibility_name;
    std::string accessibility_description;

    // ALLでは縦方向を節約するため、ダイアログ内の外側の縦余白
    // を半分にする。 上部バーと名前枠、名前枠と画像枠の間など
    // が対象。Dialogが設定し、 Container::add
    // でGroupBoxを並べるコンテナーへ伝えるが、GroupBox
    // で伝播を止める。 GroupBox自身の余白・見出
    // しとの間隔と内部の余白は、通常のALL寸法を保つ。
    bool compact_all_dialog_metrics{};

    KeyHandler on_key_down;
    KeyHandler on_descendant_key_down;
    KeyHandler on_key_up;
    TextInputHandler on_text_input;
    // IncSearchは、この共通の登録先で編集部品でない
    // 親を登録する。 有効な登録を別の検索で置き換えない。
    WidgetCommittedTextInputState committed_text_input;
    PointerHandler on_pointer;
    WheelHandler on_wheel;
    Action on_focus;
    Action on_blur;
    std::function<void(bool)> on_visibility_changed;
    // SWTのCompositeとCanvasはPaintリスナーを共有する。
    // 共通Widgetで保持し、独自の複合部品の装飾も同じフレーム経路へ通す。
    std::function<void(DrawingContext&, Rect)> on_paint_context;

private:
    friend class Container;
    struct ParentLinks {
        std::vector<Container*> values;
        ParentLinks() = default;
        // 表示情報をコピーしても、元の部品だけを子
        // 配列に持つコンテナーの子にはならない。
        ParentLinks(const ParentLinks&) noexcept {}
        ParentLinks& operator=(const ParentLinks&) noexcept { return *this; }
    } parent_links_;
    std::vector<std::shared_ptr<void>> lifetime_bindings_;
    ComponentId id_{};
    Rect bounds_{};
    LayoutItem layout_item_{};
    bool layout_item_explicit_{};
    bool visible_{true};
    bool enabled_{true};
    std::string tooltip_;
    bool tooltip_balloon_{};
    bool tooltip_immediate_{};
    bool reveal_on_focus_{true};
    std::optional<FontSpec> font_;
    std::optional<Color> foreground_;
    std::optional<Color> background_;
    CursorKind cursor_{CursorKind::Default};
    ResourceId custom_cursor_{};
};

using WidgetPtr = std::shared_ptr<Widget>;

// 独自の複合部品は、子の配置を未公開の値として返す。 共通レイアウ
// トが他の子とまとめて検証・確定するため、 提供元は配置計算の副
// 作用としてWidget::set_boundsを呼ばないこと。
struct PreparedChildBounds {
    WidgetPtr widget;
    Rect bounds{};
};
using ChildLayoutProvider =
    std::function<std::vector<PreparedChildBounds>(Rect)>;
using ScaledChildLayoutProvider =
    std::function<std::vector<PreparedChildBounds>(Rect, float)>;

class Container : public Widget {
public:
    Container() = default;
    Container(const Container& other)
        : Widget(other), layout_update_revision(other.layout_update_revision),
          scroll_update_revision(other.scroll_update_revision), on_layout(other.on_layout),
          children_(other.children_), tab_order_(other.tab_order_), layout_(other.layout_),
          child_layout_provider_(other.child_layout_provider_),
          child_layout_provider_adapter_(other.child_layout_provider_adapter_),
          child_layout_provider_revision_(other.child_layout_provider_revision_) {
        // 逆リンクはすべて事前に容量を確保してから反映する。 コピ
        // ー失敗で、構築途中のオブジェクトを指すリンクを残さない。
        for (const auto& child : children_) prepare_parent_edge(child);
        for (const auto& child : children_) add_parent_edge(child);
    }
    Container(Container&& other)
        : Widget(std::move(other)), layout_update_revision(other.layout_update_revision),
          scroll_update_revision(other.scroll_update_revision), on_layout(std::move(other.on_layout)),
          children_(std::move(other.children_)),
          tab_order_(std::move(other.tab_order_)), layout_(std::move(other.layout_)),
          child_layout_provider_(std::move(other.child_layout_provider_)),
          child_layout_provider_adapter_(std::move(other.child_layout_provider_adapter_)),
          child_layout_provider_revision_(other.child_layout_provider_revision_) {
        transfer_parent_edges(other);
    }
    Container& operator=(const Container& other) {
        if (this == &other) return *this;
        auto children = other.children_;
        for (const auto& child : children) prepare_parent_edge(child);
        Widget::operator=(other);
        layout_update_revision = other.layout_update_revision;
        scroll_update_revision = other.scroll_update_revision;
        on_layout = other.on_layout;
        tab_order_ = other.tab_order_;
        layout_ = other.layout_;
        child_layout_provider_ = other.child_layout_provider_;
        child_layout_provider_adapter_ = other.child_layout_provider_adapter_;
        child_layout_provider_revision_ = other.child_layout_provider_revision_;
        detach_parent_edges();
        children_ = std::move(children);
        for (const auto& child : children_) add_parent_edge(child);
        return *this;
    }
    Container& operator=(Container&& other) {
        if (this == &other) return *this;
        Widget::operator=(std::move(other));
        layout_update_revision = other.layout_update_revision;
        scroll_update_revision = other.scroll_update_revision;
        on_layout = std::move(other.on_layout);
        tab_order_ = std::move(other.tab_order_);
        layout_ = std::move(other.layout_);
        child_layout_provider_ = std::move(other.child_layout_provider_);
        child_layout_provider_adapter_ = std::move(other.child_layout_provider_adapter_);
        child_layout_provider_revision_ = other.child_layout_provider_revision_;
        detach_parent_edges();
        children_ = std::move(other.children_);
        transfer_parent_edges(other);
        return *this;
    }
    ~Container() override { detach_parent_edges(); }
    // 結果が同じ範囲でも、新しい配置要求で処理中の計算を
    // 無効にする。 UIスレッド上の操作番号として使う。
    std::uint64_t layout_update_revision{};
    // スクロール部品とタッチのキャプチャはこの世代を参
    // 照する。 新しい要求や構造・配置の置き換えがあ
    // れば、範囲・位置が同じでも古い操作を終了する。
    std::uint64_t scroll_update_revision{};
    void add(WidgetPtr child) {
        if (!child) return;
        prepare_parent_edge(child);
        if (compact_all_dialog_metrics) markCompactAllDialogMetrics(*child);
        children_.push_back(std::move(child));
        add_parent_edge(children_.back());
        ++layout_update_revision; ++scroll_update_revision;
    }
    void insert(std::size_t index, WidgetPtr child) {
        if (!child) return;
        prepare_parent_edge(child);
        index = std::min(index, children_.size());
        if (compact_all_dialog_metrics) markCompactAllDialogMetrics(*child);
        children_.insert(children_.begin() + static_cast<std::ptrdiff_t>(index), std::move(child));
        add_parent_edge(children_[index]);
        ++layout_update_revision; ++scroll_update_revision;
    }
    // 外側の配置部品と、その外側配置の子孫へ印を付ける。
    // GroupBox以降は内側の余白なので対象にしない。
    static void markCompactAllDialogMetrics(Widget& widget) {
        if (widget.kind() == ComponentKind::GroupBox) return;
        widget.compact_all_dialog_metrics = true;
        if (auto* nested = dynamic_cast<Container*>(&widget))
            for (const auto& child : nested->children_)
                if (child) markCompactAllDialogMetrics(*child);
    }
    void remove(const WidgetPtr& child) {
        // 引数が、children_から削除される要素への参照である場合がある。
        const auto removed = child;
        if (std::erase(children_, removed)) {
            remove_parent_edge(removed);
            ++layout_update_revision; ++scroll_update_revision;
        }
    }
    bool move_child(std::size_t from, std::size_t to) {
        if (from >= children_.size() || to >= children_.size() || from == to) return false;
        auto child = std::move(children_[from]);
        children_.erase(children_.begin() + static_cast<std::ptrdiff_t>(from));
        children_.insert(children_.begin() + static_cast<std::ptrdiff_t>(to),
                         std::move(child));
        ++layout_update_revision; ++scroll_update_revision;
        return true;
    }
    void clear() noexcept {
        if (children_.empty()) return;
        detach_parent_edges();
        children_.clear(); ++layout_update_revision; ++scroll_update_revision;
    }
    [[nodiscard]] const std::vector<WidgetPtr>& children() const noexcept { return children_; }
    // DのComposite.setTabList相当。明
    // 示的な移動先には入れ子の部品も指定できる。 弱参照で
    // 保持し、終了・非表示・無効・子孫でなくなった対象は共
    // 通の移動処理で省く。 残りの有効な部品は、明示対象の
    // 後に子ツリー順でたどる。空一覧なら通常の順へ戻す。
    void set_tab_order(std::vector<WidgetPtr> widgets) {
        tab_order_.clear();
        tab_order_.reserve(widgets.size());
        for (const auto& widget : widgets)
            if (widget) tab_order_.emplace_back(widget);
    }
    [[nodiscard]] const std::vector<std::weak_ptr<Widget>>& tab_order() const noexcept {
        return tab_order_;
    }
    void set_layout(LayoutPtr value) {
        if (layout_ == value) return;
        layout_ = std::move(value); ++layout_update_revision; ++scroll_update_revision;
    }
    [[nodiscard]] const LayoutPtr& layout() const noexcept { return layout_; }
    void set_child_layout_provider(ChildLayoutProvider value) {
        // 従来の物理座標アダプターは維持する。倍率付きの
        // 提供元も同じ準備・検証・確定を使い、 OSとの
        // 境界で倍率を渡す。UIの規則を重複させない。
        set_scaled_child_layout_provider(value ? ScaledChildLayoutProvider{
            [value=std::move(value)](Rect bounds, float) { return value(bounds); }}
            : ScaledChildLayoutProvider{});
    }
    void set_scaled_child_layout_provider(ScaledChildLayoutProvider value) {
        child_layout_provider_ = std::move(value);
        // 既存のアダプター呼び出しも、同じ提供元から論理倍率の
        // 配置を受け取る。 このラッパーで別の規則は選ばない。
        child_layout_provider_adapter_ = child_layout_provider_ ? ChildLayoutProvider{
            [prepare=child_layout_provider_](Rect bounds) { return prepare(bounds, 1.0F); }} : ChildLayoutProvider{};
        ++child_layout_provider_revision_;
        ++layout_update_revision;
        ++scroll_update_revision;
    }
    [[nodiscard]] const ChildLayoutProvider& child_layout_provider() const noexcept {
        return child_layout_provider_adapter_;
    }
    [[nodiscard]] const ScaledChildLayoutProvider& scaled_child_layout_provider() const noexcept {
        return child_layout_provider_;
    }
    [[nodiscard]] std::uint64_t child_layout_provider_revision() const noexcept {
        return child_layout_provider_revision_;
    }

    // 準備段階の通知だけを行う。モデル入力の変更や新しい配置要求
    // で現在の要求を無効にする。 独自の子配置は直接反映せず、child_layout_provider()
    // から返すこと。
    std::function<void(Rect)> on_layout;

private:
    void prepare_parent_edge(const WidgetPtr& child) const {
        if (!child) return;
        auto& parents = child->parent_links_.values;
        if (std::find(parents.begin(), parents.end(), this) == parents.end())
            parents.reserve(parents.size() + 1U);
    }
    void add_parent_edge(const WidgetPtr& child) noexcept {
        if (!child) return;
        auto& parents = child->parent_links_.values;
        if (std::find(parents.begin(), parents.end(), this) == parents.end())
            parents.push_back(this); // 子配列を変える前に、容量は確保済み。
    }
    void remove_parent_edge(const WidgetPtr& child) noexcept {
        if (child) std::erase(child->parent_links_.values, this);
    }
    void detach_parent_edges() noexcept {
        for (const auto& child : children_) remove_parent_edge(child);
    }
    void transfer_parent_edges(Container& previous) noexcept {
        for (const auto& child : children_) {
            if (!child) continue;
            auto& parents = child->parent_links_.values;
            const auto old = std::find(parents.begin(), parents.end(), &previous);
            if (old == parents.end()) continue; // 重複する別名参照は、既に移管済み。
            if (std::find(parents.begin(), parents.end(), this) == parents.end()) *old = this;
            else parents.erase(old);
        }
    }
    std::vector<WidgetPtr> children_;
    std::vector<std::weak_ptr<Widget>> tab_order_;
    LayoutPtr layout_;
    ScaledChildLayoutProvider child_layout_provider_;
    ChildLayoutProvider child_layout_provider_adapter_;
    std::uint64_t child_layout_provider_revision_{};
};

class Panel : public Container {
public:
    [[nodiscard]] ComponentKind kind() const noexcept override { return ComponentKind::Panel; }
};

class GroupBox : public Container {
public:
    [[nodiscard]] ComponentKind kind() const noexcept override { return ComponentKind::GroupBox; }
    std::string title;
};

// 重ね表示画像のウィンドウ専用の当たり形状。0の画素は入力
// を通し、0以外は受け止める。 画像・Engineのデー
// タAPIへUI依存を加えないよう、GUI部品側に置く。
struct WindowInputMask {
    std::uint32_t pixel_width{};
    std::uint32_t pixel_height{};
    std::vector<std::uint8_t> opaque_pixels;
};

// 表示方針はOSに依存しないWindowモデルが持つ。ダイアロ
// グ基底が方針を選び、 各バックエンドは実現方法だけを決める。
// 個別のEditor画面へALL統合を持ち込まず、 Web・モ
// バイルもWin32型なしで同じ契約を実装できるようにする。
enum class WindowHostMode { Default, EmbeddedDialog };

class Window : public Container {
public:
    [[nodiscard]] ComponentKind kind() const noexcept override { return ComponentKind::Window; }

    void set_bounds(Rect value) {
        const auto old = bounds();
        if (old.x == value.x && old.y == value.y && old.width == value.width &&
            old.height == value.height) return;
        Widget::set_bounds(value);
        const auto callback = on_bounds_changed;
        if (callback) callback(value);
    }

    std::string title;
    ResourceId icon{};
    std::optional<Size> minimum_size;
    std::optional<Size> maximum_size;
    // 現在の幅・高さに適用した倍率。設計寸法は1、共通
    // フレーム準備で実際のUI文字倍率を記録する。 表
    // 示ピクセルを新しい設計単位として保存しないこと。
    float bounds_ui_text_scale{1.0F};
    // 共通ダイアログの表示領域の状態。ホストが小さくても内容の
    // 実測サイズを保持し、 入力・描画で同じオフセットを使う。
    Size content_extent{};
    Point content_offset{};
    bool resizable{true};
    bool minimizable{true};
    bool maximizable{true};
    // ALLの埋め込みタイトルバー。falseならタイトルだけを表示し、 元
    // に戻す・やり直し・最小化・閉じるボタンは出さない。進捗通知などに使う。
    bool caption_buttons{true};
    bool always_on_top{};
    bool maximized{};
    // ALLのタイトルバーの最小化ボタンで隠したモードレス画面。
    // 復元まで、モデル・スタック項目・状態を保ったまま開いておく。
    bool minimized{};
    bool modified{};
    // ネイティブの自動操作では、表示・アクティブ化せずに全ウィンドウツリーを
    // 作れる。 Windows以外のバックエンドは、この指定を無視してよい。
    bool initially_hidden{};
    bool activate_on_show{true};
    // 軽量なホバープレビューは、親の切り取り領域の外へ出す
    // ためだけにウィンドウとする。 ダイアログではなく、
    // 装飾・タスクバー表示のない重ね表示として実現する。
    bool overlay_surface{};
    // 任意の重ね表示の当たり形状。nullなら従来の入力を通す動作を保つ。
    // Previewは全面またはキー色による明示的なマスクを設定する。
    std::shared_ptr<const WindowInputMask> input_mask;
    // 作成時の補助ウィンドウ指定。DのSWT.TOOL相当。 枠とキー
    // 入力は保ち、独立したタスクバー・ウィンドウ一覧の項目は作らない。
    bool utility_window{};
    // 通常の初期配置は内容の推奨幅まで広げる。 小
    // 型の幅を指定し、切り取り防止は実測最小値へ任
    // せる画面では、横方向の自動拡大を抑止できる。
    bool initial_preferred_width{true};
    // 指定範囲や内容の推奨値でなく、実際の最小サイズ
    // ちょうどで初期配置する。 指定minimum_size
    // と内容の実測最小値の大きい方を使う。
    bool initial_minimum_size{};
    // 重ね表示は合成済みの一枚の画像として最上位モデル
    // へ置き、ネイティブ側から直接描く。 子ImageView
    // では、自身のWM_PAINTより先にポップア
    // ップ管理側で消去・切り取りされる場合がある。
    ResourceId overlay_image{};
    std::uint8_t opacity{255U};
    std::weak_ptr<Window> owner;
    bool center_on_owner{};
    WindowHostMode host_mode{WindowHostMode::Default};
    Action on_close_requested;
    Action on_activated;
    Action on_deactivated;
    std::function<void(Rect)> on_bounds_changed;
};

enum class DialogResult { None, OK, Cancel, Yes, No, Retry, Abort, Apply };

class Dialog : public Window {
public:
    Dialog() {
        // ALLが変えるのはEditorダイアログの表
        // 示方法だけ。共通のDialog基底に方針を置
        // き、 個々の内容や単独起動の経路を変えず、既
        // 存・追加の全ダイアログに同じ規則を適用する。
        if (editor_window_host_options().all_mode) {
            host_mode = WindowHostMode::EmbeddedDialog;
            compact_all_dialog_metrics = true;
        }
    }

    [[nodiscard]] ComponentKind kind() const noexcept override { return ComponentKind::Dialog; }
    DialogResult result{DialogResult::None};
    bool modal{true};
    // 任意の単独起動用の警告表示規則。上に入れ子のモーダルがあ
    // る間は非同期終了を遅らせ、 実際に終了してから通知する。
    bool defer_modal_end_until_top{};
    Action on_modal_session_ended;
    // ブラウザー内の一部のフォームは、実際のポインター操作ま
    // で最初の文字入力へフォーカスを移さない。 そうしないと
    // 、モバイルのキーボードが拒否・解除される場合がある。
    bool focus_first_control_on_open{true};
};

class TextWidget : public Widget {
public:
    explicit TextWidget(std::string value = {}) : text(std::move(value)) {}
    std::string text;
    TextAlignment alignment{TextAlignment::Start};
    bool wrap{};
};

class Label : public TextWidget {
public:
    using TextWidget::TextWidget;
    [[nodiscard]] ComponentKind kind() const noexcept override { return ComponentKind::Label; }
};

class Link : public TextWidget {
public:
    using TextWidget::TextWidget;
    [[nodiscard]] ComponentKind kind() const noexcept override { return ComponentKind::Link; }
    std::string target;
    Action on_activate;
};

class Button : public TextWidget {
public:
    using TextWidget::TextWidget;
    [[nodiscard]] ComponentKind kind() const noexcept override { return ComponentKind::Button; }
    ResourceId icon{};
    // 文字に幅を合わせるボタンでは、通常の左右の余白を上書きできる。
    std::optional<float> text_horizontal_padding;
    // ネイティブのチェック・ラジオ印と同じ行にアイコンがある場
    // 合の、任意の表示位置補正。 通常のボタンでは0にする。
    float icon_offset_x{};
    bool default_button{};
    bool cancel_button{};
    Action on_click;
};

class ToggleButton : public Button {
public:
    using Button::Button;
    [[nodiscard]] ComponentKind kind() const noexcept override { return ComponentKind::ToggleButton; }
    bool checked{};
    std::function<void(bool)> on_changed;
};

class CheckBox : public ToggleButton {
public:
    using ToggleButton::ToggleButton;
    [[nodiscard]] ComponentKind kind() const noexcept override { return ComponentKind::CheckBox; }
    bool indeterminate{};
};

class RadioButton : public ToggleButton {
public:
    using ToggleButton::ToggleButton;
    [[nodiscard]] ComponentKind kind() const noexcept override { return ComponentKind::RadioButton; }
    std::string group;
};

class TextEdit : public TextWidget {
public:
    using TextWidget::TextWidget;
    [[nodiscard]] ComponentKind kind() const noexcept override { return ComponentKind::TextEdit; }
    bool multiline{};
    bool vertical_scroll{true};
    bool read_only{};
    bool horizontal_scroll{};
    bool password{};
    // 数値の行内入力もTextEditの表示を使うが、 入力
    // セッション中にユーザーの日本語変換モードを再開しない。
    bool numeric_input{};
    // 小型の数値入力では、境界のすぐ横から数字を置ける。
    // 描画・カーソル位置・当たり判定でこの設定を共用する。
    bool zero_text_padding{};
    // タブ位置の間隔を文字セル数で指定する。 0なら、ネイ
    // ティブ処理を持つホスト向けにタブ文字をそのまま残す。
    std::size_t tab_width{};
    std::size_t max_length{};
    // 表示領域への要求であり、文書の行数制限ではない。
    std::size_t minimum_visible_lines{};
    // DのFixedWidthTextの列数。実測した全角空白を2列とし
    // て扱う。 表示領域の大きさであり、入力文字数の制限には使わない。
    std::size_t minimum_visible_columns{};
    // CardWirthのメッセージ折り返し。0以
    // 外かつ折り返し有効なら、この列数で改行する
    // 。 プレビュー・Engineと同じくASCII
    // ・半角カナは1、その他は2と数え、 編集
    // フォントの実測ピクセル幅では判定しない。
    std::size_t wrap_columns{};
    // ファイル名など一つの値を複数行へ折り返して見せる
    // 入力欄。 一行入力と同じく、Enterはon_enter
    // を呼び、貼り付けの改行は空白へ変える。
    bool single_logical_line{};
    std::size_t selection_start{};
    std::size_t selection_length{};
    // selection_start/lengthは正規化した
    // まま、このフラグで操作中の端を記録する。 ネイティブの一
    // 行Text/Comboは後端のカーソルへ揃え、 複数行Text/StyledText
    // は逆方向の操作端も保持する。
    bool selection_reversed{};
    // 文字入力とクリップボード編集で共用する、Verify相当の
    // 検証。 数値部品は上書きでき、通常の文字列はそのまま通す。
    [[nodiscard]] virtual std::string filterInsertedText(std::string_view value) const {
        return std::string(value);
    }
    std::function<void(std::string_view)> on_changed;
    Action on_enter;
    // 実際の共通行内編集の、任意の操作完了通知。 false
    // には拒否・古い確定要求・破棄された編集状態も含む。
    std::function<void(bool)> on_inline_edit_finished;
};

class NumberEdit : public Widget {
    friend class NumberEditSession;
    // 共通の表示側が接続を所有し、破棄・再接続で終了済みNumberEditSession
    // への通知を残さない。 実行中の文字列は既存TextEdit
    // だけが所有する。 finish時だけ一度限りのスナッ
    // プショットを固定し、次のフォーカス喪失通知まで保つ。
    struct DraftState {
        std::weak_ptr<NumberEdit> owner;
        std::weak_ptr<int> session_lifetime;
        std::weak_ptr<TextEdit> editor;
        std::optional<std::string> finished_text;
        double observed_value{};
        unsigned observed_decimals{};
        std::uint64_t revision{};
    };
    std::weak_ptr<DraftState> edit_draft_;
public:
    class EditTextSnapshot {
        friend class NumberEdit;
        std::shared_ptr<DraftState> state_;
        std::uint64_t revision_{};
    public:
        std::optional<std::string> text;
        // 通知に成功したら、この完了済みの作業値だけを終了する。
        // 例外なら再試行でき、再入で置き換わった状態は保持する。
        void accepted() const noexcept {
            if (state_ && state_->revision == revision_ && state_->editor.expired())
                state_->finished_text.reset();
        }
    };
    [[nodiscard]] EditTextSnapshot edit_text_snapshot() const {
        EditTextSnapshot result;
        const auto draft = edit_draft_.lock();
        if (!draft || draft->session_lifetime.expired() || draft->owner.lock().get() != this || draft->observed_value != value ||
            draft->observed_decimals != decimals) return result;
        result.state_ = draft;
        result.revision_ = draft->revision;
        if (const auto editor = draft->editor.lock()) result.text = editor->text;
        else result.text = draft->finished_text;
        return result;
    }
    [[nodiscard]] ComponentKind kind() const noexcept override { return ComponentKind::NumberEdit; }
    double value{};
    double minimum{};
    double maximum{100.0};
    double step{1.0};
    unsigned decimals{};
    // 小型の数値欄用の任意の入力長制限。ネイティ
    // ブの入力部品でなくモデルに持たせ、 全ホス
    // トで編集・貼り付けに同じ上限を適用する。
    std::size_t input_max_length{};
    // ALLで入力欄の直後、増減ボタンの前に表示する項目名（Xなど）
    // 。 順序は入力・ラベル・＋・−となり、空なら通常の配置を使う。
    std::string inline_label;
    // ALL仕様: +/- ボタンの一辺(論理px)。0 は標準の 34px。
    float step_button_extent{};
    // ALL仕様: input_max_length で決まる入力欄幅への加算(論理px、負で縮小)。
    float input_width_adjust{};
    bool zero_text_padding{};
    bool read_only{};
    std::function<void(double)> on_changed;
    // 任意のDのSpinnerEdit形式の編集処理。全表示実装でNumberEditSession
    // を使い、 プレビューは表示だけ、確定はモデルを一度記録、中止は
    // 表示を復元する。通常の部品はon_changedを維持する。
    std::function<void(double)> on_preview;
    std::function<void(double)> on_committed;
    std::function<void()> on_edit_cancelled;
};

struct ChoiceItem {
    std::string text;
    std::string value;
    ResourceId icon{};
    bool enabled{true};
    // ポップアップの各行の任意の説明。ComboBox自体のツ
    // ールチップと異なり、 ポインター下の選択肢を説明する。
    std::string tooltip;
    bool operator==(const ChoiceItem&) const = default;
};

class ComboBox : public Widget {
public:
    // 表示専用。選択値・編集値としては返さない。
    std::string empty_hint;
    // 部品とポップアップの行高だけに適用し、フォントと横方向の寸法は変えない。
    float row_height_scale{1.0F};
    [[nodiscard]] ComponentKind kind() const noexcept override { return ComponentKind::ComboBox; }
    std::vector<ChoiceItem> items;
    // 置き換え要求が来たら、公開前であっても
    // 古い準備済みの項目一覧を無効にする。
    std::uint64_t item_update_revision{};
    std::uint64_t item_order_revision{};
    std::optional<std::size_t> selected_index;
    bool editable{};
    // 部品ごとのDWT Combo.setVisibleItemCount()
    // 相当。0ならホストの既定値を使う。
    // 式の関数選択など、用途に応じた長
    // い一覧を汎用の9行へ制限しない。
    std::size_t visible_item_count{};
    // 真なら閉じた部品の最小幅に項目の文字幅を含めない。推
    // 奨幅は引き続き文字に合わせる。 固定幅のペインでも
    // 長い選択肢を使え、選択文字列は表示範囲で切り取る。
    bool minimum_ignores_items{};
    // 項目文字列やALLのフォントでも広がらない
    // 、閉じた部品の論理幅。0なら実測する。
    // ポップアップ自体は最長の項目まで広げる。
    float fixed_width{};
    std::string edit_text;
    // 通常のComboアダプターとメニュー操作で共用する、編集選択の状態。
    std::optional<std::pair<std::size_t, std::size_t>> edit_selection;
    std::size_t selection_start{};
    std::size_t selection_length{};
    // 共通Comboの編集はキー操作後の選択方向を保つ。 プログラム指定
    // の逆向き範囲は、CB_SETEDITSEL自体の規則で正規化する。
    bool edit_selection_reversed{};
    std::function<void(std::optional<std::size_t>)> on_selection_changed;
    std::function<void(std::string_view)> on_text_changed;
    // 任意のポップアップ行の先頭操作。閉じた部品は選択ラベルだ
    // けを表示する。 可変の行番号でなく、固定の項目値を渡す。
    std::vector<std::string> popup_actions;
    std::function<void(std::string, std::size_t)> on_popup_action;
};

enum class CellEditorKind { Text, Choice, EditableChoice, Number };

struct ListItem {
    std::string text;
    std::string value;
    ResourceId icon{};
    bool checked{};
    bool enabled{true};
    CellEditorKind editor{CellEditorKind::Text};
    std::vector<std::string> editor_choices;
    double editor_minimum{};
    double editor_maximum{};
    // セルの反対側の端に描く、任意のDWT形式の装飾
    // 。 MotionViewの半独立の効果属性印に
    // 使い、先頭の効果アイコンの位置は動かさない。
    ResourceId trailing_icon{};
    // 診断情報は部品全体でなく行に属する。
    // 共通モデルに保持し、全バックエンドでD
    // と同じ警告印・行ごとの説明を提供する。
    std::vector<std::string> warnings;
    ResourceId warning_icon{};
    // 編集には、表示ラベルとは別の型付きリテラル・固定の選択キーが必要な場合
    // がある。 同じ「準備完了」と表示される二つのステップ値などを区別する。
    std::optional<std::string> editor_value;
    std::vector<std::string> editor_choice_values;
    // falseならセルの行内編集は開かず、別ダイア
    // ログで編集する。文字の描画は通常どおり行う。
    bool inline_editable{true};
    // 明示指定時にネイティブ・Dのスピナー動作を使う。その他の数値セルは文字入力だけにする。
    std::optional<double> editor_step;
    bool operator==(const ListItem&) const = default;
};

// モデルのコピーは別の操作所有者とする。 寸法の提供
// 元が入力確定前にアプリへ再入できる部品で共用する。
struct WidgetOperationLifetime {
    std::shared_ptr<const int> token{std::make_shared<const int>(0)};
    WidgetOperationLifetime() = default;
    WidgetOperationLifetime(const WidgetOperationLifetime&) : WidgetOperationLifetime() {}
    WidgetOperationLifetime& operator=(const WidgetOperationLifetime&) {
        token=std::make_shared<const int>(0);
        return *this;
    }
};

class ItemView : public Widget {
public:
    std::vector<ListItem> items;
    std::uint64_t item_update_revision{};
    std::uint64_t item_order_revision{};
    std::vector<std::size_t> selection;
    std::optional<std::size_t> selection_anchor;
    std::optional<std::size_t> selection_cursor;
    bool multi_select{};
    bool checkable{};
    std::size_t top_index{};
    // 結果が同じオフセットでも、新しいホイール・移
    // 動・つまみの要求で処理中の計測を無効にする。
    std::uint64_t scroll_update_revision{};
    [[nodiscard]] std::weak_ptr<const int> operation_lifetime() const noexcept { return lifetime_.token; }
    std::function<void(const std::vector<std::size_t>&)> on_selection_changed;
    std::function<void(std::size_t)> on_activate;
    std::function<void(std::size_t, Modifiers)> on_activate_with_modifiers;
private:
    WidgetOperationLifetime lifetime_;
};

class ListView : public ItemView {
public:
    [[nodiscard]] ComponentKind kind() const noexcept override { return ComponentKind::ListView; }
};

struct TableColumn {
    std::string title;
    float width{};
    TextAlignment alignment{TextAlignment::Start};
    bool resizable{true};
    bool sortable{};
    // 正の重みで、固定列の後に残った幅を分ける。 width
    // は設計上のUI寸法で、伸縮列では最小値として使う。
    float stretch{};
    bool operator==(const TableColumn&) const = default;
};

struct TableRow {
    std::vector<ListItem> cells;
    bool checked{};
    // SWTのTableItem#getGrayedは、checkedから独立した
    // 第三の表示・意味状態。 Area/Battleでは個別に固定した画像を示す。
    bool grayed{};
    std::vector<std::string> warnings;
    ResourceId warning_icon{};
    // CardPaneのDのsetupComment(Table)に対応する印と操
    // 作対象を、 警告と同じ行スナップショットで持ち、単独起動とALLで共用する。
    std::string comment_text;
    ResourceId comment_icon{};
    bool operator==(const TableRow&) const = default;
};

class TableView : public Widget {
public:
    [[nodiscard]] ComponentKind kind() const noexcept override { return ComponentKind::TableView; }
    std::vector<TableColumn> columns;
    std::vector<TableRow> rows;
    // 新しい準備が始まったら、公開に至らなくても古い処理を無効にする。
    std::uint64_t row_update_revision{};
    // 共通の構造変更でこの世代を進める。 古い添字に結び付
    // いた行内編集を、同名の別の行へ確定しないようにする。
    std::uint64_t row_order_revision{};
    std::vector<std::size_t> selection;
    std::optional<std::size_t> selection_anchor;
    std::optional<std::size_t> selection_cursor;
    // 最初に見えるモデル行。スクロール位置は全バックエンド共通の表
    // 示状態とし、 SDL・ネイティブ部品だけの内部情報にしない。
    std::size_t top_index{};
    // 一覧表示は、並べ替え対象列を通常の背景とは別の色で示す。 Windows
    // のSWT Table#setSortColumnに合わせる。
    std::optional<std::size_t> highlighted_column;
    bool multi_select{};
    bool full_row_selection{true};
    // 互換用の指定であり、別のデータ源ではない。どちらのモードでもrowsが
    // 全モデル行を所有する。 表示は可視行だけを取得するが、SWTのSetData
    // キャッシュを再現するために並べ替え・再描画でデータを捨てない。
    bool virtualized{};
    bool checkable{};
    // DのupdateCheckedと同じく、選択行のチェックを変
    // えると全選択行へ同じ値を適用する。 一覧によってはクリック行
    // の灰色状態もコピーするが、Flag行は各行の灰色状態を保つ。
    bool check_selected_rows{};
    bool check_selected_rows_copy_grayed{};
    // Areaを開く表など、ダブルクリックを
    // 主要な実行操作に使うDWT表では、
    // 同じ操作で汎用の行内編集も始めない。
    bool edit_on_double_click{true};
    // DのTableTextEditと同じく、選んだ列にquickStart=SingleClickを指定できる。
    bool edit_on_single_click{};
    // メニューで定義したEnter操作は、マウスのダブルクリックとは独立に持てる。
    bool activate_on_enter{true};
    // DのTableTextEditと同じく、F2を指定のデータ
    // 列へ結び付ける。 指定のない表は共通の先頭列の動作を保つ。
    std::optional<std::size_t> inline_edit_column;
    // DはTableEditを開くたびに選択肢を作る。 表
    // 示行も、表の選択を再構成せずここで候補を更新できる。
    std::function<bool(std::size_t, std::size_t)> on_cell_edit_begin;
    // 簡易の行内編集向けの、任意のD AbstractTableEdit
    // 形式の行移動。 ホストは行の版・同一性を事前確認し、確定
    // 後のモデルを検証してから共通選択へ渡す解決関数を返す。
    using QuickEditArrowResolver =
        std::function<std::optional<std::size_t>(std::string_view)>;
    std::function<QuickEditArrowResolver(std::size_t, std::uint64_t,
                                         std::string_view, Key)>
        quick_edit_arrow_target;
    bool header_visible{true};
    // SWTの表はsetLinesVisible(true)を指定しない限りセ
    // ルの罫線を描かない。 通常のEditorの一覧では、白い行の背景を保つ。
    bool grid_lines{};
    // 0なら共通の行寸法を使う。上書き値は設計上のUI単位。
    float row_height{};
    float header_height{};
    // Dの一部の表は、別ビューの上に見出しだけを残す。 その
    // 場合も列は保持するが、横スクロールバーは明示的に隠す。
    bool horizontal_scroll_enabled{true};
    // この表の縦スクロールバーにだけ加える、設計上の追加幅。
    float vertical_scrollbar_extra_width{};
    float horizontal_offset{};
    std::uint64_t scroll_update_revision{};
    [[nodiscard]] std::weak_ptr<const int> operation_lifetime() const noexcept { return lifetime_.token; }
    // ホイール・スクロールバーの移動を確定したら、モデル固有の重ね表示を終了する。
    Action on_scroll_changed;
    std::function<void(const std::vector<std::size_t>&)> on_selection_changed;
    // 選択行全体へ反映する前に、クリック行の固有のチェック・灰色状
    // 態の遷移を求める。 この通知内で配送・再構成を行わないこと。
    std::function<void(std::size_t, bool&, bool&)> on_check_transition;
    // 共通のチェック操作全体が終わってから通知する。リスナー
    // が表を再構成・破棄できるため、 ui_actionsが
    // 最初の通知前にコールバックと変更行の添字を保持する。
    std::function<void(std::size_t, bool)> on_check_changed;
    // Area一覧は全選択行への反映後に一度だけ
    // 更新し、通常の表は上の行ごとの通知を保つ。
    std::function<void(std::size_t, const std::vector<std::size_t>&)> on_check_batch_changed;
    std::function<void(std::size_t, std::size_t)> on_cell_activate;
    std::function<void(std::size_t, std::size_t, Modifiers)> on_cell_activate_with_modifiers;
    std::function<void(std::size_t, std::size_t, std::string)> on_cell_edited;
    // 仮のモデル行は、空も含め値が変わらないEnterでも編集を
    // 終える必要がある。 通常の表は、無変更時の短縮経路を使う。
    bool commit_unchanged_inline_edits{};
    std::function<void(std::size_t, std::size_t)> on_inline_edit_cancelled;
    // 行公開中の一時的な説明ではなく、実際の行内部品を渡す。 モデル
    // 側の編集処理がメニュー・ツールチップを付けられるようにする。
    std::function<void(std::size_t, std::size_t, const std::shared_ptr<TextEdit>&,
                       const std::shared_ptr<ComboBox>&)> on_inline_editor_created;
    std::function<void(std::size_t)> on_sort_requested;
    std::function<void(std::size_t, float)> on_column_resized;
private:
    WidgetOperationLifetime lifetime_;
};

struct TreeNode {
    TreeItemId id{};
    TreeItemId parent{};
    std::string text;
    std::string value;
    ResourceId icon{};
    bool expanded{};
    bool checked{};
    bool has_lazy_children{};
    // 素材画面のScenarioなど常に展開される構造
    // 上のルートは、子があっても＋/−を表示しない。
    bool suppress_expander{};
    std::vector<std::string> warnings;
    ResourceId warning_icon{};
    // Owner/EventTree行は、共通TreeViewを通じ
    // てコメント印とアイコンを持てる。 その他のツリーでは空にする。
    std::string comment_text;
    ResourceId comment_icon{};
    bool operator==(const TreeNode&) const = default;
};

class TreeView : public Widget {
public:
    [[nodiscard]] ComponentKind kind() const noexcept override { return ComponentKind::TreeView; }
    // 準備に失敗したら、以前の表示と行内編集を保つ。
    std::uint64_t node_update_revision{};
    std::uint64_t node_order_revision{};
    std::vector<TreeNode> nodes;
    std::vector<TreeItemId> selection;
    std::optional<TreeItemId> selection_anchor;
    std::optional<TreeItemId> selection_cursor;
    // 最初の表示行の、平坦化した可視ノード順での添字。
    std::size_t top_index{};
    // 共通の表示ピクセル単位で扱い、可視ノードの範囲へ補正する。
    float horizontal_offset{};
    std::uint64_t scroll_update_revision{};
    // 全表示実装で共通の、確定済みのホイール・スクロールバー位置変更。
    // モデル側の重ね表示は、ネイティブ部品の入力を横取りせず終了できる。
    Action on_scroll_changed;
    [[nodiscard]] std::weak_ptr<const int> operation_lifetime() const noexcept { return lifetime_.token; }
    bool multi_select{};
    bool checkable{};
    bool show_expanders{true};
    // 常に展開される構造ルートを左端へ
    // 揃え、子孫の展開ボタンは残す。
    bool flush_root_left{};
    std::function<void(const std::vector<TreeItemId>&)> on_selection_changed;
    std::function<void(TreeItemId)> on_activate;
    std::function<void(TreeItemId, Modifiers)> on_activate_with_modifiers;
    std::function<void(TreeItemId, bool)> on_expansion_changed;
    // 選択済みの行のチェック操作は、選択変更とは独立に扱う。
    std::function<void(TreeItemId, bool)> on_check_changed;
    std::function<void(TreeItemId)> on_request_children;
    std::function<void(TreeItemId, std::string)> on_item_edited;
    // ホストは個別ノードについて、共通の行内編集の作成を拒否できる。
    std::function<bool(const TreeNode&)> inline_edit_admission;
    // 表示側が所有する名前エディターへ、モデル側から標準の文字列コマンドを接続する。
    std::function<void(const std::shared_ptr<TextEdit>&)> on_inline_editor_created;
private:
    WidgetOperationLifetime lifetime_;
};

struct TabPage {
    std::string title;
    ResourceId icon{};
    WidgetPtr content;
    bool closable{};
    bool operator==(const TabPage&) const = default;
};

struct TabHeaderViewport {
    std::size_t first{};
    std::optional<std::size_t> revealed_selection;
    // 改名したタブ・アイコン・閉じるボタン・提供元やフォントの変化も含め、
    // 実際の配置を比較する。 単なる再描画では手動のスクロール位置を保つ。
    std::vector<float> measured_widths;
    float available_width{};
};

class TabView : public Widget {
public:
    [[nodiscard]] ComponentKind kind() const noexcept override { return ComponentKind::TabView; }
    std::vector<TabPage> pages;
    std::uint64_t page_update_revision{};
    std::uint64_t page_order_revision{};
    std::optional<std::size_t> selected_index;
    TabHeaderViewport header_viewport;
    std::uint64_t header_update_revision{};
    // 負の値なら共通のネイティブ形式のタブ余白を使う。
    // 辺ごとの明示的な値は、論理2px以上へ補正する。
    float header_horizontal_padding{-1.0F};
    // 見出し行の上下へ加える論理ピクセル数。 配置・描画・ポイ
    // ンターとタッチの判定は、すべて高くなった見出しを使う。
    float header_vertical_extra{};
    // 共通のウィンドウ配置処理で、各ダイアログのタブツリーへ設定する。
    bool dialog_header_context{};
    // 負なら通常の枠付きタブを使う。ドックでは上下の縁
    // を詰められるが、左右の余白は最低2pxを保つ。
    float chrome_inset{-1.0F};
    // 小型ホストは独自の選択部品を提供できる。その場
    // 合、選択ページの左右の余白は保ち、 内蔵タブ列
    // は高さを確保せず、見出し入力も受け付けない。
    bool header_visible{true};
    // 通常のダイアログ計測ではタブ列全体の幅を確保する。 無効
    // 化した画面は指定幅を保ち、タブ見出しをスクロールさせる。
    bool dialog_header_scroll{};
    // 計測中に、単独所有のTabViewの破棄などアプリの処理が
    // 呼ばれる場合がある。 コピーは独立した操作識別子を持つ。
    [[nodiscard]] std::weak_ptr<const int> operation_lifetime() const noexcept { return lifetime_.token; }
    std::function<void(std::optional<std::size_t>)> on_selection_changed;
    // 同じページの見出しを操作した場合は、選択を変えずフォーカスだけを要求する。
    std::function<void(std::size_t)> on_selected_page_activated;
    std::function<void(std::size_t)> on_close_page;
    // ページのないTabView用の限定した描画通知。 重ね表示の別部品を追
    // 加せず、フレーム取得・画像保持・クライアント範囲の切り取りを共有する。
    std::function<void(DrawingContext&, Rect)> on_empty_client_paint;
private:
    WidgetOperationLifetime lifetime_;
};

struct MenuItem {
    std::string text;
    std::string command;
    std::string shortcut;
    ResourceId icon{};
    bool enabled{true};
    bool checked{};
    bool radio{};
    bool separator{};
    std::vector<MenuItem> children;
    /// 所有メニュー側でコマンドを再構成できない動的項目用の
    /// 、任意の項目単位の操作。 指定されていれば、バックエ
    /// ンドはメニュー全体のコマンド処理よりこちらを使う。
    Action action;
    // 項目固有の動的操作に、入力ジェスチャーの情報も渡す
    // 版。 既存のActionは互換用の代替として残す。
    CommandGestureAction action_with_gesture;
    // D/SWTのShow相当。このサブメニューが表示されるときだけ呼ぶ。
    std::function<std::vector<MenuItem>()> open_children;
    [[nodiscard]] bool has_submenu() const noexcept {
        return !children.empty() || static_cast<bool>(open_children);
    }
};

class Menu : public Widget {
public:
    explicit Menu(bool context = false) : context_(context) {}
    [[nodiscard]] ComponentKind kind() const noexcept override {
        return context_ ? ComponentKind::ContextMenu : ComponentKind::MenuBar;
    }
    std::vector<MenuItem> items;
    // メインのメニューバーでは外側の余白をなくし、その分をメニュー
    // 行へ組み込める。 コンテキストメニューはこの指定を使わない。
    float bar_padding{3.0F};
    float bar_extra_height{};
    std::uint64_t item_update_revision{};
    std::uint64_t item_order_revision{};
    std::function<void(std::string_view)> on_command;
    std::function<void(std::string_view, CommandGesture)> on_command_with_gesture;
    /// ネイティブがメニューを表示・同期する直前に呼
    /// ぶ。 詳細なメニュー実装は、所有ウィンドウを
    /// 作り直さずに有効・チェック状態を更新できる。
    Action on_open;

private:
    bool context_{};
};

struct ToolItem {
    std::string text;
    std::string command;
    ResourceId icon{};
    bool enabled{true};
    bool checked{};
    bool separator{};
    bool drop_down{};
    /// ドロップダウン矢印から開く任意のメニュー。共通項目に保持し、
    /// Windows・macOS・モバイル・WebがEditor専用
    /// のネイティブ処理なしに同じツールバーを実現できるようにする。
    std::shared_ptr<Menu> drop_down_menu;
    /// アイコンだけのツールバー項目のホバー説明。text
    /// と分け、 ネイティブ側が文字を隠しても、翻訳
    /// 済みのツールチップは保持できるようにする。
    std::string tooltip;
};

class ToolBar : public Widget {
public:
    // ツールバーの説明に使う、任意の現在の表示ピクセル幅。
    std::function<float()> tooltip_width_provider;
    [[nodiscard]] ComponentKind kind() const noexcept override { return ComponentKind::ToolBar; }
    std::vector<ToolItem> items;
    // SWT.WRAPのないSWT.FLATツールバ
    // ーは、親が推奨幅全体を渡せなくても一行を保つ。
    bool single_row{};
    // ネイティブの平坦なメッセージ用ツールバーだけが、SWT形式のキーボードフォ
    // ーカスを使う。 ALLの小型バーを含め、他は既定でフォーカスを持たない。
    bool keyboard_navigation{};
    bool keyboard_vertical{};
    std::optional<std::size_t> keyboard_item_index;
    // アイコンのみの項目は、共通の16px画像・24pxボタン
    // にこの倍率を掛けて計測・描画する。 ALLの効果ツールバ
    // ーでは1.2倍などを使い、アイコンはボタンの中央に置く。
    float icon_scale{1.0F};
    std::uint64_t item_update_revision{};
    std::uint64_t item_order_revision{};
    std::function<void(std::string_view)> on_command;
    std::function<void(std::string_view, CommandGesture)> on_command_with_gesture;
};

// SWTは部品の最小寸法、外枠込みの理想帯サイズ、要求中の
// 帯サイズを分ける。 弱い部品参照で表示順変更後も帯を識
// 別し、削除済み部品を配置APIが保持しないようにする。
struct CoolBarBandSize {
    std::weak_ptr<Widget> owner;
    Size minimumSize{};
    Size preferredSize{};
    std::optional<Size> currentSize;
};

class CoolBar : public Container {
public:
    [[nodiscard]] ComponentKind kind() const noexcept override { return ComponentKind::CoolBar; }
    bool locked{};
    // 一部の埋め込みパレットは一段の横並びを保つ
    // 。 表示領域が帯より狭くても行を増やさず
    // 、ホスト側で切り取り・スクロールできる。
    bool single_row{};
    // DのCoolBarの折り返し位置。新しい行を始める子の添字。
    std::vector<std::size_t> wrap_indices;
    std::vector<std::shared_ptr<CoolBarBandSize>> band_sizes;
    std::function<void(std::size_t, std::size_t)> on_item_moved;
};

class StatusBar : public TextWidget {
public:
    using TextWidget::TextWidget;
    [[nodiscard]] ComponentKind kind() const noexcept override { return ComponentKind::StatusBar; }
};

class Canvas : public Widget {
public:
    [[nodiscard]] ComponentKind kind() const noexcept override { return ComponentKind::Canvas; }
    std::function<void(Rect)> on_paint;
    // 描画コンテキストを提供できるネイティブ実装はこの通知を
    // 使う。 軽量な利用側には、従来の矩形だけの通知も残す。
    Action invalidate;
    bool double_buffered{true};
    // 通常の独自部品はEditorの凹んだ輪郭を使う。 重ね表示用Canvas
    // では無効にし、半透明の描画だけが見えるようにする。
    bool border{true};
    // 明示指定で使う通常のUI文字列の表示領域。共通計測で有効フォント
    // の行高を確保する。 0なら、指定されたキャンバス寸法を変えない。
    std::uint32_t ui_text_lines{};
    // 全サイズの独自描画面を複合部品の背景に使い、その
    // 上のネイティブ入力欄・ラベルは操作可能にできる。
    bool behind_siblings{};
};

class ImageView : public Widget {
public:
    [[nodiscard]] ComponentKind kind() const noexcept override { return ComponentKind::ImageView; }
    ResourceId image{};
    ImageScaleMode scale_mode{ImageScaleMode::Fit};
    Interpolation interpolation{Interpolation::Linear};
    std::size_t frame{};
    // 通常のネイティブ画像部品はEditorの凹んだ枠を使う。 装飾な
    // しの重ね表示であるホバープレビューでは、無効にできるようにする。
    bool border{true};
    // レイヤーがあれば、従来の単一画像の代わりに指定順で描画する。
    std::vector<ImageLayerSpec> layers;
    // 指定されたクライアント寸法。共通計測で枠を加える。 未設定
    // なら、既存の拡縮可能なフォームの表示領域の決め方を保つ。
    std::optional<Size> design_size;
};

class ProgressBar : public Widget {
public:
    [[nodiscard]] ComponentKind kind() const noexcept override { return ComponentKind::ProgressBar; }
    double value{};
    double minimum{};
    double maximum{100.0};
    bool indeterminate{};
};

class Slider : public Widget {
public:
    [[nodiscard]] ComponentKind kind() const noexcept override { return ComponentKind::Slider; }
    double value{};
    double minimum{};
    double maximum{100.0};
    double step{1.0};
    // キーのページ移動量と、値を刻む単位を分ける。 0なら通常の
    // 1段階の10倍とし、目盛りは明示指定で入力とは独立に扱う。
    double page_step{};
    double tick_interval{};
    Orientation orientation{Orientation::Horizontal};
    std::function<void(double)> on_changed;
};

class Separator : public Widget {
public:
    [[nodiscard]] ComponentKind kind() const noexcept override { return ComponentKind::Separator; }
    Orientation orientation{Orientation::Horizontal};
};

enum class SplitResizeMode { Ratio, PreserveFirst, PreserveSecond };
struct SplitLayoutSnapshot {
    float first{}, second{}, extent{};
    float sash_x{}, sash_y{}, sash_width{}, sash_height{};
    bool operator==(const SplitLayoutSnapshot&) const = default;
};

class SplitView : public Container {
public:
    [[nodiscard]] ComponentKind kind() const noexcept override { return ComponentKind::SplitView; }
    Orientation orientation{Orientation::Horizontal};
    std::vector<float> weights;
    // 派生部品が明示的に使う。共通配置が成功した場合だけこ
    // の状態を公開し、 準備中は確定済みの配置を変えない。
    SplitResizeMode resize_mode{SplitResizeMode::Ratio};
    // 元のSplitPane部品だけが、初期の最大化による再配置を有効にする。
    bool reflow_on_initial_maximized_geometry{};
    bool initial_maximized_reflow_pending{};
    std::optional<SplitLayoutSnapshot> committed_split_layout;
    // 初回配置の論理寸法は一度限りの指定。最初の境界ド
    // ラッグで破棄し、以後は保存済みの比率を使う。 最
    // 初の寸法への正のオフセットは、設定比率を保ったま
    // ま初期境界をその論理ピクセル分だけ下・右へ移す。
    std::optional<float> initial_first_extent;
    std::optional<float> initial_second_extent;
    std::optional<float> initial_first_extent_offset;
    // falseなら、どちらのペイン内の有効ページが変わっても境界
    // を動かさない。 小型ALLの主ドックで、AreaとBattle
    // の推奨最小値が違っても同じフレーム配置を保つために使う。
    bool constrain_to_child_minimums{true};
    // 元の契約で明示的な端の最小値だけを制限する互換
    // ペインは、 共通配置・境界ドラッグにもその範囲
    // を使う。ALLと単独起動へ同じ規則を適用する。
    bool use_explicit_edge_minimums{false};
    // 共通の境界ドラッグ処理で使う、辺ごとの意味上の
    // 最小値。 汎用分割ビューは従来の24px、専用
    // 部品はDの0/10pxの折り畳み規則を選べる。
    float drag_minimum_first{24.0F};
    float drag_minimum_second{24.0F};
    std::function<void(const std::vector<float>&)> on_weights_previewed;
    std::function<void()> on_drag_committed;
    std::function<void(const std::vector<float>&)> on_weights_changed;
};

class ScrollView : public Container {
public:
    [[nodiscard]] ComponentKind kind() const noexcept override { return ComponentKind::ScrollView; }
    Point scroll_offset{};
    // 以前引き伸ばした子の範囲ではなく、最後の共通計測の結果。
    std::optional<Size> content_extent;
    bool horizontal_scroll{};
    bool vertical_scroll{true};
    // 0なら共通の移動量1あたり36pxの動作を保つ。 正な
    // ら、共通処理で倍率を適用する指定の論理ステップを使う。
    float wheel_step{};
    // 独自ステップを有効にした場合、0でない各ホイール通知では移動量の符号だけを使う。
    bool wheel_step_per_event{};
};

class ExpandBar : public Container {
public:
    [[nodiscard]] ComponentKind kind() const noexcept override { return ComponentKind::ExpandBar; }
    [[nodiscard]] std::weak_ptr<const int> operation_lifetime() const noexcept { return lifetime_.token; }
    std::uint64_t expansion_update_revision{};
    std::vector<bool> expanded;
    std::function<void(std::size_t, bool)> on_expansion_changed;
private:
    WidgetOperationLifetime lifetime_;
};

class ToolTip : public TextWidget {
public:
    using TextWidget::TextWidget;
    [[nodiscard]] ComponentKind kind() const noexcept override { return ComponentKind::ToolTip; }
    bool balloon{};
};

class ColorWell : public Widget {
public:
    [[nodiscard]] ComponentKind kind() const noexcept override { return ComponentKind::ColorWell; }
    Color value{};
    std::function<void(Color)> on_changed;
};

class Spacer : public Widget {
public:
    [[nodiscard]] ComponentKind kind() const noexcept override { return ComponentKind::Spacer; }
};

}  // namespace cwxeditor::lib
