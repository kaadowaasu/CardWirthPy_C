#pragma once

#include "components.hpp"

#include <cstdint>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

namespace cwxeditor::lib {

class DialogFontLayoutScope final {
public:
    explicit DialogFontLayoutScope(bool dialog) noexcept;
    ~DialogFontLayoutScope();
    DialogFontLayoutScope(const DialogFontLayoutScope&) = delete;
    DialogFontLayoutScope& operator=(const DialogFontLayoutScope&) = delete;
private:
    bool previous_{};
};
[[nodiscard]] bool all_dialog_font_layout() noexcept;

// 一回の配置トランザクションの間、推奨サイズと最小サイズの計測結果を記
// 憶する。コンテナーの配置では各階層で子のサイズを求め、そのたびに部分
// 木全体を再帰計測するため、深いダイアログ（Editor設定など）では
// フレームやスクロールの各段階でO(depth^2)回の再計測が発生し
// ていた。入れ子の範囲は最外層のキャッシュを共有する。範囲内でウィジェ
// ットツリーを変更し得る処理（配置コールバックなど）は、invalidate_measurement_cache()
// を呼ぶ必要がある。
class MeasurementCacheScope final {
public:
    MeasurementCacheScope() noexcept;
    ~MeasurementCacheScope();
    MeasurementCacheScope(const MeasurementCacheScope&) = delete;
    MeasurementCacheScope& operator=(const MeasurementCacheScope&) = delete;
private:
    bool owner_{};
};
void invalidate_measurement_cache() noexcept;

// Editorのウィジェットは、プラットフォーム非依存の一つの
// 論理座標系で計測する。バックエンドは結果の矩形をデバイス用
// に拡大縮小してよいが、別のウィジェット寸法体系は作らない。
struct EditorUiMetrics final {
    float menu_height{21.0F};
    float tab_height{22.0F};
    float row_height{20.0F};
    float expand_header_height{24.0F};
    float panel_height{28.0F};
    float group_box_height{36.0F};
    float control_height{24.0F};
    float label_height{22.0F};
    float label_horizontal_padding{4.0F};
    float toolbar_height{28.0F};
    float default_spacing{5.0F};
    float split_spacing{7.0F};
    float cool_bar_gripper{9.0F};
    float minimum_control_height{20.0F};
    float minimum_label_height{16.0F};
    float flexible_minimum_width{40.0F};
    float flexible_minimum_height{20.0F};
    Insets default_padding{6.0F, 6.0F, 6.0F, 6.0F};
    float group_caption_padding{20.0F};
    float group_content_padding{7.0F};
};

inline constexpr EditorUiMetrics kEditorUiMetrics{};
inline constexpr float kMinimumTabHorizontalPadding = 2.0F;

// 入力配送、ヒット判定、表示は同じ計測値の状態
// を使う。アダプターは物理イベントを変換してよ
// いが、共通状態機械とは別に行・タブ・ポップア
// ップの規則やポインターのしきい値を持たない。
struct EditorInteractionMetrics final {
    float row_height{};
    float tab_height{};
    float expand_header_height{};
    float resize_margin{};
    float drag_threshold{};
    // このしきい値は従来どおりネイティブ入力単位で表す。タッチの
    // 移動量は、すでに現在の描画面に合わせて正規化されている。
    float touch_scroll_threshold{8.0F};
    std::size_t combo_visible_rows{9U};
};

[[nodiscard]] EditorInteractionMetrics editor_interaction_metrics() noexcept;

// 通常のEditorの操作部とフォームの文字に適用する倍率。Canvas
// 描画はシナリオやスキンで定義された座標のまま扱う。Editorウ
// ィジェットに付くFontSpecはUI文字として、scaled_editor_widget_font()
// を通じて反映する。保存済み設定は整数の百分率を使う
// 。不正な値や旧形式の値に対する範囲制限はここに集
// 約し、起動時と設定ダイアログで解釈を一致させる。
[[nodiscard]] std::uint32_t clamp_editor_ui_text_scale_percent(std::uint32_t value) noexcept;
[[nodiscard]] float editor_ui_text_scale_from_percent(std::uint32_t value) noexcept;
[[nodiscard]] float editor_ui_text_scale() noexcept;
// 有限値は1～2に制限し、整数の百分率に丸め
// る。非有限値なら現在の値と世代を維持する。
void set_editor_ui_text_scale(float value) noexcept;
[[nodiscard]] std::uint64_t editor_ui_text_scale_generation() noexcept;
// DのspinnerUpDownWithWheelはアプリケーション全体の設定。設定
// 適用後に作られたコントロールも含め、すべてのNumberEditに同じ規則を使う。
void set_number_edit_wheel_enabled(bool value) noexcept;
[[nodiscard]] bool number_edit_wheel_enabled() noexcept;
// DのswitchTabWheelは、タブ列のホイール操作とCtrl+Tabによる巡回をまとめて制御する。
void set_tab_wheel_switch_enabled(bool value) noexcept;
[[nodiscard]] bool tab_wheel_switch_enabled() noexcept;
void set_editor_ime_mode(int value) noexcept;
[[nodiscard]] int editor_ime_mode() noexcept;
// プラットフォーム非依存のEditorの寸法を、現在のUI文字
// 倍率に変換する。表示側は描画とヒット判定で同じ結果を使う。
[[nodiscard]] float scaled_ui_metric(float logical_value) noexcept;
// ALLのEditor描画面では、スクロールバーの余白に論理画素を2画
// 素追加する。共通の寸法計算層で扱い、プラットフォーム別の規則を増や
// さずにダイアログの計測、描画、ポインターのヒット判定を一致させる。
[[nodiscard]] float editor_scrollbar_extent() noexcept;
// ネイティブ/SDLのEditorコントロールの実効フォントを返す。カードやメッ
// セージの書体設計をEditorのUI設定から独立させるため、GraphicsBackend
// の描画はこの補助関数を使わない。FontSpecは書体名の文字列を所有する
// ため、コピー時のメモリ確保で例外を送出し得る。数値だけでよい代替処理は、
// 確保を伴わないeditor_widget_font_pointsを使う。
[[nodiscard]] FontSpec scaled_editor_widget_font(const FontSpec& font);
[[nodiscard]] float editor_widget_font_points(const FontSpec* font = nullptr) noexcept;
// 通常コントロールの計測、描画、入力で共有する既定値。実効
// フォントを返すため、戻り値にUI倍率を再度適用しない。
[[nodiscard]] FontSpec effective_editor_widget_font(const FontSpec* font = nullptr);

[[nodiscard]] std::string display_text_without_mnemonic(std::string_view value);
// Dやネイティブのメニューラベルは、タブの後ろにアクセラレーターを格納する場合が
// ある。専用のショートカット欄を優先し、どちらの形式もラベル本体には含めない。
[[nodiscard]] std::string menu_item_label(const MenuItem& item);
[[nodiscard]] std::string_view menu_item_shortcut(const MenuItem& item) noexcept;
// フォントバックエンドはグリフの計測値を提供し、配置の規則は持たない。この範
// 囲はスレッドローカルで入れ子にできる。再入したダイアログや組み込みEditor
// は呼び出し元のプロバイダーを復元し、ワーカーやヘッドレスの計算はそのハン
// ドルを借用しない。コールバックにはUI倍率適用済みの実効フォントを渡す。
class UiTextMeasureScope final {
public:
    using Measure = std::function<std::optional<Size>(std::string_view, const FontSpec&)>;
    // 代表的な日本語グリフの可視部分の中心が、計測し
    // た行矩形の中心より下にある距離を画素で表す。
    using InkOffset = std::function<std::optional<float>(const FontSpec&)>;
    explicit UiTextMeasureScope(Measure measure, InkOffset ink_offset = {});
    ~UiTextMeasureScope();
    UiTextMeasureScope(const UiTextMeasureScope&) = delete;
    UiTextMeasureScope& operator=(const UiTextMeasureScope&) = delete;
private:
    friend std::optional<Size> measured_effective_ui_text_size(std::string_view, const FontSpec&) noexcept;
    friend float measured_effective_ui_text_ink_offset(const FontSpec&) noexcept;
    Measure measure_;
    InkOffset ink_offset_;
    UiTextMeasureScope* previous_{};
    UiTextMeasureScope* next_{};
};
[[nodiscard]] std::optional<Size> measured_ui_text_size(
    std::string_view value, const FontSpec* font = nullptr) noexcept;
// 表示用に取得済みの状態は実効フォントを含む。
// その計測時に現在のUI倍率を二重適用しない。
[[nodiscard]] std::optional<Size> measured_effective_ui_text_size(
    std::string_view value, const FontSpec& effective_font) noexcept;
// 上下中央揃えの単一行文字に対する、グリフ中心の補正。フォントバック
// エンドが判定できなければ0。アイコンの隣の文字を、フォントのアセン
// ト・ディセントの矩形ではなく、アイコンの中心に合わせるために使う。
[[nodiscard]] float measured_effective_ui_text_ink_offset(
    const FontSpec& effective_font) noexcept;
// 設定済みのプロバイダーが使用不能な計測値を返した、または例外を送出した場合の
// 、スレッドごとに単調増加する世代。呼び出し側では代替描画を引き続き使える。
[[nodiscard]] std::uint64_t ui_text_measure_failure_generation() noexcept;
[[nodiscard]] float ui_text_width(std::string_view value, float padding,
                                  const FontSpec* font = nullptr) noexcept;
// 論理的な最小値と余白には倍率を一度だけ適用する。グリフサービスにはすで
// に実効フォントが渡される。操作部の座標と入力のヒット判定の両方で使う。
[[nodiscard]] float ui_text_line_height(float logical_minimum, float logical_padding,
                                       const FontSpec* font = nullptr) noexcept;
// メニューの区切りと通常行は、画素単位の移動とこの計測を共有する。
[[nodiscard]] float menu_popup_item_height(const Menu& menu, std::size_t index) noexcept;
[[nodiscard]] float resolved_tab_vertical_extra(const TabView& tabs) noexcept;
// 展開用の見出しはGroupBoxの見出しを置き換える。ほかのコン
// テナーでは、通常の内容の余白の上に追加の見出し領域を確保する。
[[nodiscard]] std::string_view expand_item_title(const Widget& item) noexcept;
// グリフプロバイダーを呼ばずに準備する。見出しと本体の位置は
// 、所有する同じフォント、倍率、指定された余白から解決する。
struct ExpandItemMeasurement {
    FontSpec font;
    float header_minimum{}, header_fallback{};
    float caption_minimum{}, caption_fallback{}, text_padding{}, body_padding{};
    bool container{}, group{};
};
struct ExpandItemMetrics {
    float header_height{}, content_offset{};
};
[[nodiscard]] ExpandItemMeasurement prepare_expand_item_measurement(const Widget& item);
[[nodiscard]] ExpandItemMetrics measure_expand_item(const ExpandItemMeasurement& input) noexcept;
[[nodiscard]] ExpandItemMetrics expand_item_metrics(const Widget& item) noexcept;
[[nodiscard]] float expand_item_header_height(const Widget& item) noexcept;
[[nodiscard]] float expand_item_content_offset(const Widget& item) noexcept;
// ヘッドレス、フォントプロバイダーの欠如・失敗時にだけ使う、結果が一定の代替計測。
[[nodiscard]] float estimated_ui_text_width(std::string_view value,
                                            float padding) noexcept;
// タブの計測、描画、ヒット判定で共有する、各辺の論理的な内側余白。
// 負の指定では呼び出し側の既定値を維持する。0や1を明示しても最小
// の間隔は除去しない。既存の広い既定値や上書き指定は変更しない。
[[nodiscard]] float resolved_tab_horizontal_padding(
    float requested, float fallback = kMinimumTabHorizontalPadding) noexcept;
[[nodiscard]] float preferred_tab_width(const TabPage& page,
    const FontSpec* font = nullptr,
    float horizontal_padding = -1.0F) noexcept;
[[nodiscard]] float preferred_menu_item_width(const MenuItem& item,
    const FontSpec* font = nullptr) noexcept;
[[nodiscard]] float preferred_tool_item_width(const ToolItem& item,
    const FontSpec* font = nullptr) noexcept;
struct ToolBarItemGeometry {
    Rect bounds{};
    Rect drop_down{};
};
// 帯状領域の共通座標情報。描画、実行、ホバーでそれ
// ぞれ別の先頭余白、区切り幅、項目間隔を定めない。
[[nodiscard]] std::vector<ToolBarItemGeometry> tool_bar_item_geometry(
    const ToolBar& toolbar);
[[nodiscard]] std::vector<Rect> menu_bar_item_geometry(const Menu& menu);
[[nodiscard]] float default_ui_height(ComponentKind kind) noexcept;
// 確保や計測の失敗時は空のSizeを返す。呼び出し側は部
// 分的な計測結果を公開せず、前の完成フレームを保持する。
[[nodiscard]] Size preferred_ui_size(const WidgetPtr& widget) noexcept;
// 推奨サイズと同じ配置ツリーで計算する、使用可能な表示領域と操作部
// の最小サイズ。分割ペインで子を分割境界だけの大きさまで潰さない。
[[nodiscard]] Size minimum_ui_size(const WidgetPtr& widget) noexcept;
// 幅に依存する操作部の計測。折り返すメニューやツー
// ル群の必要な高さを、前フレームの矩形で決めない。
[[nodiscard]] Size preferred_ui_size_for_width(const WidgetPtr& widget,
                                               float width) noexcept;
// 現在のUI文字倍率でウィンドウを表示するための外寸を返す。通常の最上位
// ウィンドウは保存済みのサイズを維持する。ダイアログはフォームの文字に合
// わせて広げ、125～200%でもラベルや編集欄が切れないようにする。
[[nodiscard]] Size realized_window_size(const Window& window) noexcept;
[[nodiscard]] Size realized_window_size(const Window& window,
                                        Size logical_size) noexcept;
[[nodiscard]] Size window_content_preferred_size(const Window& window) noexcept;
[[nodiscard]] Size window_content_minimum_size(const Window& window) noexcept;
[[nodiscard]] std::optional<Size> realized_window_minimum_size(
    const Window& window) noexcept;
[[nodiscard]] std::optional<Size> realized_window_maximum_size(
    const Window& window) noexcept;
// ホストは利用可能なクライアント領域だけを提供する。内容の計測とOS
// のウィンドウ上限は分け、ダイアログは文字を縮めずにスクロールする。
[[nodiscard]] Size fit_window_surface(const Window& window, Size requested,
                                      Size available) noexcept;

}  // namespace cwxeditor::lib
