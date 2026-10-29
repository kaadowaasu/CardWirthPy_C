#pragma once

#include <array>
#include <cstddef>
#include <cmath>
#include <functional>
#include <optional>
#include <vector>

namespace cardwirth::app::all {

// ホームは1888x910の固定描画領域を使う。Editorは高さ基準
// の論理描画領域を使い、 既定の高さは597pxとする。ホーム・Engine
// ・Explorerの高さとは独立している。 ホームの設定でEditor
// の高さを変えても、ホストのホーム画面の寸法は変えない。
constexpr int kAllPcDesignWidth = 1888;
constexpr int kAllPcDesignHeight = 910;
constexpr int kAllEditorMinimumHeight = 597;
constexpr int kAllEditorMaximumHeight = 1600;
constexpr int kAllEditorDefaultHeight = 597;
constexpr int kAllEditorDesignHeight = kAllEditorDefaultHeight;
constexpr int kAllEditorMaximumSideSpace = 320;
[[nodiscard]] constexpr int clamp_editor_height(const int value) noexcept {
    return value < kAllEditorMinimumHeight ? kAllEditorMinimumHeight
         : value > kAllEditorMaximumHeight ? kAllEditorMaximumHeight : value;
}
[[nodiscard]] constexpr int clamp_editor_side_space(const int value) noexcept {
    return value < 0 ? 0
         : value > kAllEditorMaximumSideSpace ? kAllEditorMaximumSideSpace : value;
}
constexpr float kAllPcAspect =
    static_cast<float>(kAllPcDesignWidth) / static_cast<float>(kAllPcDesignHeight);
constexpr float kAllEditorAspect =
    static_cast<float>(kAllPcDesignWidth) / static_cast<float>(kAllEditorDesignHeight);
constexpr float kAllPcMinimumScale = 0.5F;
// デバッガーが所有する子ダイアログは、ALL
// 内でこの固定倍率で表示する。 配置前にラ
// スタ画像の寸法をこの論理座標系へ戻す。
constexpr float kDebuggerDescendantScale = 2.0F;

struct Size {
    float width{};
    float height{};
    [[nodiscard]] bool valid() const noexcept {
        return std::isfinite(width) && std::isfinite(height) &&
               width > 0.0F && height > 0.0F;
    }
};

struct Point {
    float x{};
    float y{};
};

struct Rect {
    float x{};
    float y{};
    float width{};
    float height{};
    [[nodiscard]] float right() const noexcept { return x + width; }
    [[nodiscard]] float bottom() const noexcept { return y + height; }
    [[nodiscard]] bool valid() const noexcept {
        return std::isfinite(x) && std::isfinite(y) &&
               std::isfinite(width) && std::isfinite(height) &&
               width > 0.0F && height > 0.0F;
    }
    [[nodiscard]] bool contains(Point point) const noexcept;
    [[nodiscard]] bool contains(Rect rect) const noexcept;
};

enum class AllPlatformClass { NativePc, MobilePhone, MobileTablet, Web };

struct AllPlatformProfile {
    AllPlatformClass platform{AllPlatformClass::NativePc};
    bool fullscreen_host{};
    bool lock_native_window_aspect{true};
    bool use_safe_area{true};
    bool has_native_os_frame{true};
    bool browser_viewport_driven{};
};

enum class Page { Home, Engine, Editor, Explorer };
enum class DebugDockSide { Right, Left };

struct AllLayoutTheme {
    // EngineとDebugはALL表示領域の直接の子とする。 余白は縦
    // 横比を保って収めるためのものだけとし、ホスト側の余白は追加しない。
    float outer_margin{0.0F};
    float engine_debug_gap{0.0F};
    float dialog_margin{12.0F};
};

struct CoordinateTransform {
    Rect destination{};
    Size source{};
    float scale_x{1.0F};
    float scale_y{1.0F};

    [[nodiscard]] std::optional<Point> to_source(Point host) const noexcept;
    // キャプチャ中の操作は、表示先の外へ出ても同じ座標系で扱う。
    [[nodiscard]] std::optional<Point> to_source_unclipped(Point host) const noexcept;
    [[nodiscard]] Point to_host(Point local) const noexcept;
    [[nodiscard]] Rect to_host(Rect local) const noexcept;
};

struct EngineDebugLayout {
    Rect group_rect{};
    // EngineとDebuggerは表示領域の最上位の子と
    // して、安全領域全体を分割して使う。 縦横比による調整は
    // 内容の矩形に適用し、親やルートのレイアウトは縮めない。
    Rect engine_slot_rect{};
    Rect engine_rect{};
    Rect engine_content_rect{};
    Rect debugger_rect{};
    float scale{};
    float gap{};
    DebugDockSide debug_side{DebugDockSide::Right};
    bool debugger_visible{};
    Rect status_dialog_rect{};
    bool status_dialog_visible{};
    // Engine右端のショートカットパネル。左側のstatus_dialog_rect
    // と対称に配置する。 status_dialog_visibleが真のときに有効。
    Rect shortcut_panel_rect{};
};

enum class DialogOwnerScope { Host, Engine };
enum class DialogPlacementPolicy { Center, PreserveAndClamp };

struct DialogMetrics {
    Size preferred_size{};
    Point requested_origin{};
    DialogOwnerScope owner_scope{DialogOwnerScope::Host};
    DialogPlacementPolicy placement{DialogPlacementPolicy::PreserveAndClamp};
    bool movable{true};
    bool resizable{};
    // 子孫用の表示倍率と配置を使う。デバッガー表示中でメイン領域に収まらない場合はALL全体へ配置する。
    bool debugger_descendant{};
    // 中央配置を基準とした、ダイアログの論理座標での移動量。 ホ
    // ストは画面ごとのUIからではなく、共通モデルから取得する。
    Point center_offset{};
};

struct DialogPlacement {
    Rect rect{};
    float scale{1.0F};
    DialogOwnerScope owner_scope{DialogOwnerScope::Host};
    bool movable{};
};

// Engineの既存ダイアログをALLのネイティブ子ウィンドウと
// して配置するための計算。 座標系はホストに合わせ、各プラットフ
// ォームからクライアント領域と表示領域を受け取る。 配置先の選択
// 、Release版のサイズ保持、中央配置、通常ダイアログの範囲
// 制限は共通で行う。 DialogPlacementを返すことで
// 、ネイティブ表示とホスト内描画で所有関係と移動可否を揃える。
struct EngineDialogPlacementRequest {
    Rect host_rect{};
    Rect engine_viewport{};
    Rect debugger_viewport{};
    // Release版から取得した推奨サイズ。取得できない場合は、 呼
    // び出し元が現在のネイティブウィンドウの寸法を代わりに指定できる。
    Size preferred_size{};
    bool debugger{};
};

struct AllLayoutInput {
    AllPlatformProfile platform{};
    Rect full_rect{};
    Rect safe_rect{};
    Page page{Page::Home};
    bool debugger_visible{};
    DebugDockSide debug_side{DebugDockSide::Right};
    Size engine_intrinsic_size{};
    Size debugger_intrinsic_size{};
    std::vector<DialogMetrics> dialogs{};
    AllLayoutTheme theme{};
    float max_upscale{1.0F};
    bool status_dialog_visible{};
    // Editorの論理高さ。Page::Editorでのみ使用し、
    // ホーム・Engine・Explorerでは従来の固定寸法を使う。
    int editor_design_height{kAllEditorDefaultHeight};
    // ALL内のEditorに適用する、論理ピクセル単位の四
    // 辺の余白。 すべてゼロなら従来どおり領域全体を使う。
    int editor_left_side_space{};
    int editor_right_side_space{};
    int editor_top_side_space{};
    int editor_bottom_side_space{};
};

struct AllLayoutSnapshot {
    Rect host_rect{};
    Rect safe_rect{};
    Rect home_rect{};
    Rect page_rect{};
    std::optional<EngineDebugLayout> engine_debug{};
    std::vector<DialogPlacement> dialogs{};
    CoordinateTransform host_to_render{};
};

// 埋め込みEditorより手前に表示する、ホスト側の確認画面。 背
// 景の暗幕がホスト全体を覆うように、ALLの物理座標を使う。 パネ
// ルはEditorの拡大率やスクロール位置によらず中央へ配置する。
struct EditorHomeConfirmationLayout {
    Rect overlay{};
    Rect panel{};
    Rect message{};
    Rect accept_button{};
    Rect cancel_button{};
    float scale{1.0F};
};

struct HomeMenuLayout {
    Rect background{};
    std::array<Rect, 4> cards{};
    float scale{1.0F};
};

// CardWirthのNORMALメニューカード内部の配置。 CardImageと
// 同じく、80x110の枠内のy=13に74x94の素材を左右中央揃えで配置する。
// ホームの名称は12pxの太字で、素材に重ねて(5,3)から左揃えで描画する。
struct HomeMenuCardLayout {
    Rect art{};
    Point name_origin{};
    float name_max_width{};
    float name_font_size{};
    float scale{1.0F};
};

[[nodiscard]] Rect inset(Rect rect, float amount) noexcept;
[[nodiscard]] Rect center_rect(Rect owner, Size size) noexcept;
[[nodiscard]] EditorHomeConfirmationLayout editor_home_confirmation_layout(
    Rect host) noexcept;
// 横方向と縦方向の倍率の小さい方で背景全体を収め、 4枚の
// 従来型メニューカードを同じ倍率で横一列に中央配置する。
[[nodiscard]] HomeMenuLayout home_menu_layout(
    Rect host, Size background = {632.0F, 420.0F},
    Size card = {80.0F, 110.0F}) noexcept;
[[nodiscard]] HomeMenuCardLayout home_menu_card_layout(Rect card) noexcept;
// ALLのEditorは四辺の余白を差し引いたホストの高さから
// 論理幅を求め、 PC・Web・モバイルで同じ座標変換を使う。
[[nodiscard]] Size editor_logical_surface_size(
    Rect available, int logical_height = kAllEditorDefaultHeight,
    int left_side_space = 0, int right_side_space = 0,
    int top_side_space = 0, int bottom_side_space = 0) noexcept;
[[nodiscard]] Rect editor_content_viewport(
    Rect available, int logical_height = kAllEditorDefaultHeight,
    int left_side_space = 0, int right_side_space = 0,
    int top_side_space = 0, int bottom_side_space = 0) noexcept;
// ALLが高さ33pxの横型ステータスバーを縦型ダイ
// アログへ置き換えたときの、 Engineの表示対
// 象領域を返す。元画像が2倍などの場合はその倍率を
// 保ち、 論理サイズの632x420へ固定しない。
[[nodiscard]] Size status_dialog_engine_source_size(Size engine_surface) noexcept;
// ホスト内のラスタ画像を、AllLayoutEngineが使う論理的な推
// 奨サイズへ変換する。 配置時には子孫用の倍率を再適用する。raster_scale
// は論理1ピクセルあたりの画像ピクセル数で、 子孫は論理サイズの
// kDebuggerDescendantScale倍で表示する。
[[nodiscard]] Size hosted_dialog_preferred_size(
    Size surface, bool debugger_descendant,
    float raster_scale = kDebuggerDescendantScale) noexcept;
[[nodiscard]] Rect clamp_dialog(Rect dialog, Rect constraint) noexcept;
[[nodiscard]] Rect center_engine_dialog(Size preferred, Rect engine_rect,
                                        float margin, float* scale = nullptr,
                                        float inherited_scale = 1.0F) noexcept;
// ALLの矩形をネイティブの物理ピクセルへ変換する。 丸めと最
// 小サイズの補正はすべてのホストで共通とし、 各プラットフォ
// ームではクライアント倍率の取得と計算結果の適用だけを行う。
[[nodiscard]] Rect native_pixel_rect(Rect rect, float scale_x, float scale_y) noexcept;
[[nodiscard]] DialogPlacement plan_engine_dialog_placement(
    const EngineDialogPlacementRequest& request) noexcept;
[[nodiscard]] CoordinateTransform uniform_fit_transform(Rect destination, Size source,
                                                        float maximum_scale = 0.0F) noexcept;
[[nodiscard]] EngineDebugLayout compute_engine_debug_layout(
    Rect safe, Size engine, std::optional<Size> debugger, DebugDockSide side,
    AllLayoutTheme theme = {}, float max_upscale = 1.0F,
    std::optional<Size> engine_overlay = std::nullopt);

class AllLayoutEngine {
public:
    [[nodiscard]] AllLayoutSnapshot compute(const AllLayoutInput& input) const;
};

class EngineLayoutPreferences {
public:
    using Listener = std::function<void(DebugDockSide)>;

    [[nodiscard]] DebugDockSide debug_side() const noexcept { return debug_side_; }
    void set_debug_side(DebugDockSide side);
    [[nodiscard]] std::size_t on_debug_side_changed(Listener listener);
    void remove_listener(std::size_t token) noexcept;

private:
    struct Entry { std::size_t token{}; Listener listener{}; };
    DebugDockSide debug_side_{DebugDockSide::Right};
    std::size_t next_token_{1};
    std::vector<Entry> listeners_{};
};

}  // namespace cardwirth::app::all
