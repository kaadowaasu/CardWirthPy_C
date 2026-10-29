#pragma once

// Python の wx に対応する、ダイアログ UI 描画用の共通 RGBA ヘルパー。
#include "cw/lib/compat.hpp"
#include <cmath>
#include <cstdint>
#include <optional>

namespace cw::lib::ui {

// 共通ダイアログの保持型描画領域。不変の外枠は一度描き、変
// 更された画素矩形だけを無効にして復元してから、動的なコン
// トロールやメディアを再合成する。ページ・配置・倍率の変更
// 時は invalidate_all() を明示的に呼ぶ。
class RetainedSurface {
public:
    struct ComponentHitRegion {
        ::cw::lib::Rect rect{};
        ::cw::lib::Integer value{};
        bool enabled{true};
    };
    std::shared_ptr<::cw::lib::Surface> surface{};
    std::shared_ptr<::cw::lib::Surface> base{};
    std::vector<::cw::lib::Rect> dirty{};
    std::vector<::cw::lib::Rect> damage{};
    std::map<::cw::lib::String, ::cw::lib::Rect> components{};
    std::map<::cw::lib::String, std::uint64_t> component_signatures{};
    std::map<::cw::lib::String, std::vector<ComponentHitRegion>> component_hits{};
    bool full{true};
    std::uint64_t revision{};

    void reset();
    void invalidate_all();
    void invalidate(::cw::lib::Rect rect);
    void set_component(::cw::lib::String key, ::cw::lib::Rect rect);
    // コンポーネントを登録し、領域またはモデル識別値が変わった場合
    // だけ無効にする。全コンポーネントを先に呼び、restore_dirty()
    // の後、true が返ったものだけを描き直す。
    [[nodiscard]] bool component_changed(::cw::lib::String key,
                                         ::cw::lib::Rect rect,
                                         std::uint64_t signature);
    [[nodiscard]] bool invalidate_component(const ::cw::lib::String& key);
    // 最後の矩形を一度だけ無効にし、一時的な重ね表示の記録を消す。ポッ
    // プアップを閉じた後、毎フレームその古い矩形を復元してはならない。
    [[nodiscard]] bool remove_component(const ::cw::lib::String& key);
    [[nodiscard]] std::optional<::cw::lib::Rect> component(const ::cw::lib::String& key) const;
    void set_component_hits(::cw::lib::String key,
                            std::vector<ComponentHitRegion> regions);
    [[nodiscard]] std::optional<::cw::lib::Integer> component_hit(
        const ::cw::lib::String& key, ::cw::lib::Real x,
        ::cw::lib::Real y) const;
    [[nodiscard]] bool needs_full(::cw::lib::Size size) const;
    void allocate(::cw::lib::Size size, ::cw::lib::Color color);
    void capture_base();
    void restore_dirty();
    void finish();
    [[nodiscard]] bool animated_content_changed() const;
    // フレームが新しく進んだ場合だけ true を返す。呼び出し側は
    // 保持した合成結果を一度更新し、静止画像はこの経路を通らない。
    [[nodiscard]] bool consume_animated_content_change();
};

// Python の配置が wx の論理単位で表されるダイアログ用
// の、コンポーネント描画面。小さな論理レイヤーに描き、最終画素の
// 保持面へ直接合成する。キャレット・ホバー・スライダーの変更ごと
// に、ダイアログ全体の一時画像を作って拡大縮小することを避ける。
class ScaledRetainedCanvas {
public:
    RetainedSurface& retained;
    ::cw::lib::Real scale{1.0};
    ::cw::lib::Size logical_size{};
    std::shared_ptr<::cw::lib::Surface> surface{};
    bool full{};

    ScaledRetainedCanvas(RetainedSurface& store, ::cw::lib::Size logical,
                         ::cw::lib::Real output_scale,
                         ::cw::lib::Color background = {240,240,240,255});
    [[nodiscard]] ::cw::lib::Integer pixel(::cw::lib::Integer value) const;
    [[nodiscard]] ::cw::lib::Rect rect(::cw::lib::Rect logical) const;
    [[nodiscard]] bool component_changed(const ::cw::lib::String& key,
                                         ::cw::lib::Rect logical,
                                         std::uint64_t signature);
    void capture_base_and_restore();
    // 出力解像度のコンポーネントレイヤー（draw_scale
    // = scale）。共通描画処理にはコンポーネント内の
    // 論理座標を渡し、文字を最終サイズで鮮明に描く。composite()
    // は等倍で複製する。rgba を直接書く場合は、
    // layer->size と同じ画素座標を使う。
    [[nodiscard]] std::shared_ptr<::cw::lib::Surface> layer(
        ::cw::lib::Rect logical, ::cw::lib::Color fill = {0,0,0,0}) const;
    // レイヤーはコンポーネント内の論理画素で扱い、倍率を一度だけ適用する。layer()
    // からの面はすでに出力解像度なので再サンプリングしない。
    void composite(::cw::lib::Rect logical,
                   const std::shared_ptr<::cw::lib::Surface>& layer);
    void finish();
};

[[nodiscard]] ::cw::lib::Integer win_i(::cw::lib::Integer value);

// 描画処理が受け取る論理単位での面のサイズ（Surface::draw_scale
// ）。通常の画素単位の面では size と一致する。
[[nodiscard]] inline ::cw::lib::Size logical_size(
    const std::shared_ptr<::cw::lib::Surface>& surface) {
    if (!surface) return {};
    const auto scale = std::isfinite(surface->draw_scale) && surface->draw_scale > 0.0
        ? surface->draw_scale : 1.0;
    return {static_cast<::cw::lib::Integer>(std::lround(surface->size.width / scale)),
            static_cast<::cw::lib::Integer>(std::lround(surface->size.height / scale))};
}

// 共通の描画面は、ネイティブ wx ダイアログと同じフォント表を使う。明示
// 的なダイアログ画素サイズは、役割に保存されたゲーム用フォントサイズから
// 独立する。フェイスとスタイルは全 OS で設定済みの役割から取得する。
struct FontStyle {
    ::cw::lib::String face{"ＭＳ Ｐゴシック"};
    ::cw::lib::Integer pixels{14};
    bool bold{};
    bool italic{};
};

// システム形式のダイアログはゲーム・ウィンドウ倍率ではなく、デスクトップ UI
// の寸法を使う。最終画素単位なので、利用側で win_i() を再適用しない。
struct SystemDialogMetrics {
    FontStyle font{"ＭＳ Ｐゴシック",12,false,false};
    ::cw::lib::Real scale{1.0};
    ::cw::lib::Integer line_height{14};
    [[nodiscard]] ::cw::lib::Integer scaled(::cw::lib::Integer value) const;
};
[[nodiscard]] SystemDialogMetrics system_dialog_metrics();
// Py の Frame 起動時に使う既定の 12pt フォントの画
// 素高。ホストの取得処理が未対応・利用不能なら、計測値なしを返す。
[[nodiscard]] std::optional<::cw::lib::Integer> default_font_pixel_height();
// 共通 UI を表示しているウィンドウの内容倍率を取得する。SDL
// の表示問い合わせはイベントスレッドで行い、ダイアログ・モデル側は
// system_dialog_metrics() から保存値を読む。
void set_system_dialog_scale(::cw::lib::Real scale) noexcept;

struct SystemSelectionColours {
    ::cw::lib::Color background{0, 120, 215, 255};
    ::cw::lib::Color text{255, 255, 255, 255};
};
// ネイティブ wx の Choice/VListBox の選択色。ドロップダウンはこの一つ
// の取得元を使い、ホバーとキー選択の色がダイアログやホストモードで変わらないようにする。
[[nodiscard]] SystemSelectionColours system_selection_colours();

// リソースアイコンを含む結果段落も対象にする、読み取り専用テキ
// ストの共通 UTF-8 折り返し。位置は常に元のバイトを指し
// 、表示用の改行を加えてもコピー・保存するテキストは変えない。
struct InlineLabelImage {
    std::size_t offset{};
    ::cw::lib::Size size{};
    ::cw::lib::String key;
};
struct WrappedLabelImage : InlineLabelImage {
    ::cw::lib::Integer x{};
};
struct WrappedLabelRow {
    ::cw::lib::String text;
    std::vector<std::size_t> offsets, columns;
    std::vector<::cw::lib::Integer> positions;
    std::size_t next{};
    std::vector<WrappedLabelImage> images;
};
[[nodiscard]] std::vector<WrappedLabelRow> wrap_label(
    const ::cw::lib::String& text, const FontStyle& font, ::cw::lib::Integer width,
    std::vector<InlineLabelImage> images = {});

[[nodiscard]] FontStyle configured_font(
    const ::cw::lib::String& type, ::cw::lib::Integer fallback_pixels,
    bool fallback_bold = false, bool fallback_italic = false,
    const ::cw::lib::String& fallback_face = "ＭＳ Ｐゴシック");

// FontSetting は倍率なしの論理画素を保存し、共通
// ダイアログの面は cw.wins() の座標で公開する。変
// 換を共通ヘルパーにまとめ、倍率の二重適用や適用漏れを防ぐ。
[[nodiscard]] FontStyle configured_font_scaled(
    const ::cw::lib::String& type, ::cw::lib::Integer fallback_pixels,
    bool fallback_bold = false, bool fallback_italic = false,
    const ::cw::lib::String& fallback_face = "ＭＳ Ｐゴシック");

// 一度のウィンドウ倍率適用後に、Python の get_wxfont の画素互換補正を行う。pixel_adjustment
// は最終画素単位で、ツリーは wins(15)-1 を要求する。
[[nodiscard]] FontStyle configured_wx_font(
    const ::cw::lib::String& type, ::cw::lib::Integer logical_pixels,
    bool adjust_size = false, ::cw::lib::Integer pixel_adjustment = 0,
    bool fallback_bold = false, bool fallback_italic = false);

// ゲーム・カードのフォントは、役割に保存された画素サイズを要求出力倍率
// で使う。ダイアログ内のカードでは UP_SCR と異なる場合がある。
[[nodiscard]] FontStyle configured_game_font(
    const ::cw::lib::String& type, ::cw::lib::Integer fallback_pixels,
    ::cw::lib::Real output_scale, bool fallback_bold = false,
    bool fallback_italic = false,
    const ::cw::lib::String& fallback_face = "ＭＳ Ｐゴシック");

[[nodiscard]] std::shared_ptr<::cw::lib::Surface> wins_surface(
    const std::shared_ptr<::cw::lib::Surface>& surface, ::cw::lib::Real loaded_scale = 1.0);

// リソーステーブルは遅延取得のため、Frame が UP_WIN
// を設定する前に画像をキャッシュし得る。現在のサイズを正しいと仮定
// せず、合成時に記録された論理倍率からキャッシュ画素を正規化する。
[[nodiscard]] std::shared_ptr<::cw::lib::Surface> resource_surface_for_window(
    const std::shared_ptr<::cw::lib::Surface>& surface);
[[nodiscard]] std::shared_ptr<::cw::lib::Surface> resource_surface_for_scale(
    const std::shared_ptr<::cw::lib::Surface>& surface, ::cw::lib::Real scale);
[[nodiscard]] std::shared_ptr<::cw::lib::Surface> resource_surface_for_logical(
    const std::shared_ptr<::cw::lib::Surface>& surface);

[[nodiscard]] std::shared_ptr<::cw::lib::Surface> resource_table_surface(
    const ::cw::lib::Value& table_value, const ::cw::lib::String& key);

[[nodiscard]] std::shared_ptr<::cw::lib::Surface> resource_dialog_surface(const ::cw::lib::String& key);
// Python の Select/CardControl の切り替えは、どこ
// でもこの二つのスキン素材を使う。キーの選択を Presentation 側
// の一経路にまとめ、同じ展開状態では全ホスト・ダイアログで同じ画像を示す。
[[nodiscard]] std::shared_ptr<::cw::lib::Surface> additional_controls_surface(bool expanded);
// Python の fill_bitmap は DrawBitmap(..., useMask=False)
// を使う。再サンプリング前にカラーキーを解除し、キャッシュ済みの文字画像は変更しない。
[[nodiscard]] std::shared_ptr<::cw::lib::Surface> resource_dialog_background(const ::cw::lib::String& key);

void fill_rect(const std::shared_ptr<::cw::lib::Surface>& destination, ::cw::lib::Rect rect,
               ::cw::lib::Color color);

void blend_surface(const std::shared_ptr<::cw::lib::Surface>& destination,
                   const std::shared_ptr<::cw::lib::Surface>& source, ::cw::lib::Integer x,
                   ::cw::lib::Integer y, ::cw::lib::Integer width = 0, ::cw::lib::Integer height = 0,
                   std::optional<::cw::lib::Rect> clip = std::nullopt);
// clip は x/y と同じ出力先の描画座標。画像のサイズ変
// 更やアルファ合成の追加をせず、描画と動画の更新領域を制限す
// る。CPU 側で画像を変換しても元の動画への依存を保持し、
// フレームトークンが進んだら所有者が変換後の画素を作り直す。
void inherit_surface_animation(const std::shared_ptr<const ::cw::lib::Surface>& destination,
                               const std::shared_ptr<const ::cw::lib::Surface>& source);

[[nodiscard]] std::shared_ptr<::cw::lib::Surface> render_label(
    const ::cw::lib::String& text, ::cw::lib::Integer size, bool bold,
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> color,
    const ::cw::lib::String& face = "ＭＳ Ｐゴシック", bool italic = false);

[[nodiscard]] ::cw::lib::Integer label_width(const ::cw::lib::String& text, ::cw::lib::Integer size, bool bold,
                                             const ::cw::lib::String& face = "ＭＳ Ｐゴシック", bool italic = false);

void draw_label(const std::shared_ptr<::cw::lib::Surface>& destination, ::cw::lib::String text,
                ::cw::lib::Integer x, ::cw::lib::Integer y, ::cw::lib::Integer size, bool bold,
                std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> color,
                const ::cw::lib::String& face = "ＭＳ Ｐゴシック", bool italic = false);

void draw_outline_label(const std::shared_ptr<::cw::lib::Surface>& destination, ::cw::lib::String text,
                        ::cw::lib::Integer x, ::cw::lib::Integer y, ::cw::lib::Integer size, bool bold,
                        ::cw::lib::Color outline, std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> color,
                        const ::cw::lib::String& face = "ＭＳ Ｐゴシック", bool italic = false);

void draw_sunken_border(const std::shared_ptr<::cw::lib::Surface>& destination, ::cw::lib::Rect rect);
// 従来のネイティブ押しボタンの立体枠。沈んだ入力欄、一覧、選択済み
// ツールとは分け、通常のボタンが押されたままに見えないようにする。
void draw_raised_border(const std::shared_ptr<::cw::lib::Surface>& destination, ::cw::lib::Rect rect);

// 全ダイアログで使う共通の押し・切り替えボタンの面。Windows
// の Python/wx に合わせ、薄い灰色の枠と明るい
// 角丸の面を描く。選択・押下状態は淡い青、既定ボタンは青
// い枠とする。呼び出し側は上に文字・アイコンだけを描く。
struct ButtonFace {
    bool enabled{true};
    bool pressed{};          // 押下中または切り替え有効。
    bool default_button{};   // Enter に対応する主ボタン。
};
void draw_button_face(const std::shared_ptr<::cw::lib::Surface>& destination,
                      ::cw::lib::Rect rect, ButtonFace face = {});
// ボタンの面とホストのホバー枠で共有する角の半径
// 。矩形自身の単位で指定し、両輪郭を一致させる。
[[nodiscard]] float button_corner_radius(float width, float height) noexcept;

// Windows/wx 形式の共通チェックボックス描画。チェック
// 時はデスクトップのアクセント色と幾何形状の白いチェックを使う。
// 設定フォントで形・押下表示が変わる X の文字に置き換えない。
void draw_checkbox(const std::shared_ptr<::cw::lib::Surface>& destination,
                   ::cw::lib::Rect rect, bool checked, bool enabled = true,
                   bool indeterminate = false);

// Windows/wx 形式の共通ラジオボタン描画。印は設定フォ
// ントから独立させる。押しボタン内の丸い文字を代わりに使わない。
void draw_radio(const std::shared_ptr<::cw::lib::Surface>& destination,
                ::cw::lib::Rect rect, bool selected, bool enabled = true);

void draw_rounded_bar(const std::shared_ptr<::cw::lib::Surface>& destination, ::cw::lib::Integer x,
                      ::cw::lib::Integer y, ::cw::lib::Integer width, ::cw::lib::Integer height,
                      ::cw::lib::Color light, ::cw::lib::Color dark, ::cw::lib::Integer fill_width,
                      bool reverse = false);

} // namespace cw::lib::ui
