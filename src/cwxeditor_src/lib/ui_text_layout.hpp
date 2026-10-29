#pragma once

#include "ui_actions.hpp"

#include <functional>
#include <span>

namespace cwxeditor::lib {

class DrawingContext;

// 表示側はグリフの計測だけを提供する。段落、UTF-8オフセット、折り
// 返し、文字揃え、キャレットのヒット判定、表示領域の規則は共有する。
using TextWidthMeasure = std::function<float(std::string_view)>;
struct GcTextRun {
    std::string text;
    float x{};
};
struct GcTextLine {
    std::vector<GcTextRun> runs;
    float width{};
};
struct GcTextOptions {
    bool delimiters{true};
    bool expand_tabs{true};
};
// SWT/DWTの通常のGC.drawTextとGC.textExtent
// の契約。区切り文字で行を分け、タブは空白8文字幅の次のタブ位置まで
// 進める。背景描画は所有元のコントロールや吹き出しに任せ、この補助処
// 理は文字計測、文字列区間の位置、グリフの描画要求だけを担当する。
struct GcTextLayout {
    std::vector<GcTextLine> lines;
    Size extent{};
    float line_height{};
};
[[nodiscard]] GcTextLayout layout_gc_text(DrawingContext& drawing,
    std::string_view text, const FontSpec& font, GcTextOptions options = {});
void draw_gc_text(DrawingContext& drawing, const GcTextLayout& layout,
    Point origin, const FontSpec& font, Color color,
    std::optional<float> line_advance = {});
// 通常ラベル用の、Dのカード一覧のcutTextと共通の規則。先頭部分の区切りはUnicode
// スカラー値の境界とし、幅に収めたラベルがUTF-8文字の途中で終わらないようにする。
[[nodiscard]] std::string ellipsize_gc_text(DrawingContext& drawing,
    std::string_view text, const FontSpec& font, float width,
    std::string_view suffix = "...");

struct AlignedTextLayout {
    Point origin;
    Rect clip;
};
// 通常の単一行UIラベルは、コントロールに収めるための折り返しを行わない。グリフの
// 計測値はフォントバックエンドが提供し、文字揃えとクリッピングは共通規則で扱う。
[[nodiscard]] std::optional<AlignedTextLayout> layout_aligned_text(Rect bounds,
    TextAlignment alignment,float padding,std::optional<Size> measured) noexcept;
// アプリケーションの描画前に原点とクリップを解決する。ラスタ描画の再生時は、
// グリフの計測値、ウィジェットモデル、変更可能な全体のUI倍率を参照しない。
[[nodiscard]] std::optional<AlignedTextLayout> prepare_aligned_text(
    std::string_view text,Rect bounds,TextAlignment alignment,float padding,
    const FontSpec& effective_font) noexcept;
struct TextVisualLine {
    std::size_t start{};
    std::size_t end{};
    std::size_t next_start{};
    float width{};
};
struct LabelTextLine {
    std::string text;
    AlignedTextLayout placement;
};
struct LabelTextPresentation {
    Size extent;
    std::vector<LabelTextLine> lines;
};
// ラベルは編集部と段落・UTF-8の折り返しを共有するが、編集状態
// やスクロールバーは持たない。高さ0は推奨サイズの計測に使える。
[[nodiscard]] LabelTextPresentation prepare_label_text(std::string_view text,
    Rect bounds, TextAlignment alignment, float padding, bool wrap,
    const FontSpec& effective_font);

// フォントバックエンドはグリフの高さだけを提供する。行間と編
// 集領域の内側余白は、計測、描画、入力で共有するUI規則。
[[nodiscard]] float text_edit_line_height(std::optional<float> glyph_height,
    const FontSpec* font = nullptr) noexcept;
[[nodiscard]] Insets text_edit_insets() noexcept;
[[nodiscard]] Insets text_edit_insets(const TextEdit& edit) noexcept;
// クライアント領域の文字寸法から、既存モデルの内側余白とスクロールバー用余白を含
// む共通のTextEdit寸法を返す。ネイティブホストの枠寸法は問い合わせない。
[[nodiscard]] Size text_edit_outer_size(const TextEdit& edit,
    Size client_text_extent) noexcept;
// 右側に確保する幅と下側に確保する高さ。Dのテキストコン
// トロールと同様に、横スクロールより折り返しを優先する。
[[nodiscard]] Size text_edit_scroll_gutters(const TextEdit& edit) noexcept;
[[nodiscard]] Rect text_edit_inner_bounds(const TextEdit& edit) noexcept;
[[nodiscard]] ItemScrollGeometry text_edit_scroll_geometry(const TextEdit& edit,
    std::span<const TextVisualLine> lines, float line_height,
    const TextEditInteractionState& state);
// 一時的な表示用ドキュメント。実際の編集内容を変更せず、on_changedも発生させ
// ない。TextComposition内のオフセットはすべてUTF-8のバイト単位。
struct TextCompositionView {
    TextEdit edit;
    TextEditInteractionState state;
    TextSelection range;
};
[[nodiscard]] TextComposition composition_from_characters(std::string text, int start, int length);
void update_text_composition(TextEdit& edit, TextEditInteractionState& state,
    TextComposition composition, std::uint64_t focus_revision);
[[nodiscard]] std::optional<TextCompositionView> project_text_composition(
    const TextEdit& edit, const TextEditInteractionState& state);
[[nodiscard]] std::string text_display_range(const TextEdit& edit,
    std::size_t start, std::size_t end);
[[nodiscard]] float measure_text_range(const TextEdit& edit,
    std::size_t start, std::size_t end, const TextWidthMeasure& measure);
[[nodiscard]] std::vector<TextVisualLine> layout_text_lines(const TextEdit& edit,
    float wrap_width, const TextWidthMeasure& measure);
[[nodiscard]] std::size_t text_visual_line_for_offset(
    std::span<const TextVisualLine> lines, std::size_t offset) noexcept;
[[nodiscard]] Point text_visual_line_origin(const TextEdit& edit,
    const TextVisualLine& line, std::size_t index, Rect inner, float line_height,
    const TextEditInteractionState& state) noexcept;
[[nodiscard]] std::size_t text_visual_line_hit(const TextEdit& edit,
    const TextVisualLine& line, float x, const TextWidthMeasure& measure);
[[nodiscard]] std::size_t text_edit_hit(const TextEdit& edit,
    std::span<const TextVisualLine> lines, Rect inner, float line_height,
    const TextEditInteractionState& state, Point point, const TextWidthMeasure& measure);
void clamp_text_edit_scroll(const TextEdit& edit, TextEditInteractionState& state,
    std::span<const TextVisualLine> lines, Rect inner, float line_height) noexcept;
[[nodiscard]] bool scroll_text_edit_by(const TextEdit& edit, TextEditInteractionState& state,
    std::span<const TextVisualLine> lines, Rect inner, float line_height, Point delta) noexcept;
void reveal_text_edit_caret(const TextEdit& edit, TextEditInteractionState& state,
    std::span<const TextVisualLine> lines, Rect inner, float line_height,
    const TextWidthMeasure& measure);
[[nodiscard]] bool move_text_caret_by_lines(TextEdit& edit,
    TextEditInteractionState& state, std::span<const TextVisualLine> lines,
    int delta, bool extend, const TextWidthMeasure& measure);
void drag_text_selection_to(TextEdit& edit, TextEditInteractionState& state,
    std::span<const TextVisualLine> lines, Rect inner, float line_height,
    Point point, const TextWidthMeasure& measure);

// 同期プロバイダーが使うドキュメントと操作状態を所有する。一致判定には文字
// 列だけでなく配置・フォント・IMEも含め、利用側のコードは呼ばない。呼
// び出し側は遷移を公開する前に、フォーカスとセッションの寿命も確認する。
struct TextEditSnapshot {
    TextEdit edit;
    TextEditInteractionState state;
    TextEditSnapshot(const TextEdit& source, const TextEditInteractionState& interaction);
    [[nodiscard]] bool matches(const TextEdit& source,
        const TextEditInteractionState& interaction) const;
};

// OSアダプターはクリップボードの受け渡しを提供し、編集や選択の規則は持たない。
struct TextClipboardAccess {
    std::function<std::optional<std::string>()> read;
    std::function<bool(std::string_view)> write;
};
// 同期プロバイダーはイベント処理を実行し得る。呼び出し側が編集部と状態を生存させ、
// liveで現在のフォーカス・セッションを識別し、acceptsで処理対象の妥当
// 性を追加検証する。拒否または失効したキーは、部分的な遷移を公開せずに消費する。
struct TextEditKeyGuard {
    std::function<bool()> live;
    std::function<bool(std::string_view)> accepts;
    // 省略可能な同期結果。未処理・拒否・失効したキーではfalse。文
    // 字列とキャレットを適用した時点で、変更通知の前にtrueとなる。
    bool* committed{};
};
[[nodiscard]] bool dispatch_text_edit_key(TextEdit& edit,
    TextEditInteractionState& state, Key key, Modifiers modifiers,
    Rect inner, float line_height, const TextWidthMeasure& measure,
    const TextClipboardAccess& clipboard = {}, const TextEditKeyGuard& guard = {});
[[nodiscard]] Rect text_edit_caret_bounds(const TextEdit& edit,
    std::span<const TextVisualLine> lines, Rect inner, float line_height,
    const TextEditInteractionState& state, const TextWidthMeasure& measure);

}  // namespace cwxeditor::lib
