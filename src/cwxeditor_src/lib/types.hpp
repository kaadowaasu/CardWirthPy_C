#pragma once

#include <chrono>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace cwxeditor::lib {

using ComponentId = std::uint64_t;
using ResourceId = std::uint64_t;
using TreeItemId = std::uint64_t;
using TimerId = std::uint64_t;
using ProcessId = std::uint64_t;
using AudioHandle = std::uint64_t;

enum class Platform { Automatic, Windows, MacOS, Linux, IOS, Android, Web, Headless };

struct Point {
    float x{};
    float y{};
};

// 描画面内の点を別の座標系へ変換する。値がない場合は変換できない。
using CoordinatePointMap = std::function<std::optional<Point>(Point)>;

struct Size {
    float width{};
    float height{};
};

struct Rect {
    float x{};
    float y{};
    float width{};
    float height{};
};

struct Insets {
    float top{};
    float right{};
    float bottom{};
    float left{};
};

struct Color {
    std::uint8_t red{};
    std::uint8_t green{};
    std::uint8_t blue{};
    std::uint8_t alpha{255};
};

// 色選択を開始し、表示できなければfalseを返す。完了は後から通
// 知し、取消時はnullopt。入れ子のモーダルループは使わない。
using AsyncColorPicker = std::function<bool(
    Color, std::function<void(std::optional<Color>)>)>;

enum class TextAlignment { Start, Center, End, Justify };
enum class Orientation { Horizontal, Vertical };
enum class ImageScaleMode { None, Fit, Fill, Stretch, Center, Tile };
enum class Interpolation { Nearest, Linear, Best };
enum class PixelFormat { Unknown, Gray8, RGB24, BGR24, RGBA32, BGRA32 };
enum class ImageFormat { Unknown, Bitmap, Png, Jpeg, Gif, WebP, Icon, Cursor };

enum class FontSizeMode : std::uint8_t {
    TypographicPoints,
    CellPixels,
    CharacterPixels,
    DefaultHeight
};

struct FontSpec {
    std::string family;
    float points{10.0F};
    std::uint16_t weight{400};
    bool italic{};
    bool underline{};
    bool strikeout{};
    // グリフのブレンド描画を選ぶ。シナリオのテキストセルでは書体や
    // スタイルとは別に指定でき、プレビューでもこの指定を保持する。
    bool antialias{true};
    // シナリオの文字は指定されたゲーム用フォントを使い、ALLのUI専用の拡大は適用しない。
    bool content_font{};
    // 指定された装飾フラグは装飾内容を表す。共通の場面描画では、ネイ
    // ティブの線ではなくD/SWTの寸法に基づく線を描く場合がある。
    bool decorations_in_policy{};
    // 位置指定の集成体初期化を維持するため末尾に追加する。上記の
    // 数値は既定でポイント単位。GDIの正・負・ゼロの高さは、
    // それぞれセル高・文字高・既定高として区別する必要がある。
    FontSizeMode size_mode{FontSizeMode::TypographicPoints};
};

// 指定された画素単位でのフォントエンジンの計測値。行間と
// 場面の文字装飾は、全プラットフォームで同じ値を使う。
struct FontMetrics {
    float height{}, ascent{}, descent{};
};

// ネイティブのフォントエンジンは形状だけを提供する。複合形状の塗り、線、行配置
// 、ラスタライズ時のアンチエイリアスは、指定された文字描画の共通規則で扱う。
enum class GlyphPathKind { Move, Line, Quadratic, Cubic, Close };
struct GlyphPathCommand {
    GlyphPathKind kind{};
    std::array<Point,3> points{};
};
struct GlyphPath { std::vector<GlyphPathCommand> commands; };

struct ImageFrame {
    Size logical_size{};
    std::uint32_t pixel_width{};
    std::uint32_t pixel_height{};
    PixelFormat pixel_format{PixelFormat::Unknown};
    std::vector<std::byte> pixels;
    std::chrono::milliseconds duration{};
};

// 復号後のチャンネル配置だけでは、不透明なアルファチャンネルを持つPNGとalphaData
// を持たないPNGを区別できない。正規化前に入力元の情報を保持する。
enum class ImageTransparency { Unknown, None, AlphaData, PaletteKey, DirectKey };
struct ImageSourceTransparency {
    ImageTransparency kind{ImageTransparency::Unknown};
    std::optional<Color> key;
    std::optional<std::uint16_t> palette_index;
    bool animated{};
};

struct ImageData {
    ImageFormat format{ImageFormat::Unknown};
    std::vector<ImageFrame> frames;
    std::optional<std::uint32_t> loop_count;
    ImageSourceTransparency source_transparency;
};

struct FileFilter {
    std::string label;
    std::vector<std::string> patterns;
    std::vector<std::string> mime_types;
};

struct ScreenInfo {
    std::string name;
    Rect bounds{};
    Rect work_area{};
    float scale_factor{1.0F};
    bool primary{};
};

}  // namespace cwxeditor::lib
