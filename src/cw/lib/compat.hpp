#pragma once

#include "cw/lib/decimal.hpp"
#include "cw/lib/datetime.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace cw::lib {

using Integer = std::int64_t;
using Real = double;
using String = std::string;
using Bytes = std::vector<std::uint8_t>;
using Path = std::filesystem::path;

struct Object {
    virtual ~Object() = default;
};

using ObjectPtr = std::shared_ptr<Object>;
using WeakObjectPtr = std::weak_ptr<Object>;

// 静的なソースだけでは Python の実行時の具象型が
// 決まらない値用の代替表現。明示的な型注記は具象 C++
// 型に変換し、Value は本当に動的な値だけに使う。
class Value {
public:
    // Decimal を独立した型として保持する。ここで Real
    // に変換すると、ゲームデータ・プロパティの境界に届く前に、0.1
    // + 0.2 のような小数値や大きな係数の精度が失われる。
    using Storage = std::variant<std::monostate, bool, Integer, Real, Decimal, String, Bytes, ObjectPtr>;

    Value() = default;
    Value(bool value) : storage_(value) {}
    Value(Integer value) : storage_(value) {}
    Value(int value) : storage_(static_cast<Integer>(value)) {}
    Value(Real value) : storage_(value) {}
    Value(Decimal value) : storage_(std::move(value)) {}
    Value(const char* value) : storage_(String(value ? value : "")) {}
    Value(String value) : storage_(std::move(value)) {}
    Value(Bytes value) : storage_(std::move(value)) {}
    Value(ObjectPtr value) : storage_(std::move(value)) {}

    [[nodiscard]] bool is_none() const noexcept { return std::holds_alternative<std::monostate>(storage_); }
    [[nodiscard]] bool is_decimal() const noexcept { return std::holds_alternative<Decimal>(storage_); }
    [[nodiscard]] const Decimal* as_decimal() const noexcept { return std::get_if<Decimal>(&storage_); }
    [[nodiscard]] Decimal* as_decimal() noexcept { return std::get_if<Decimal>(&storage_); }

    // 明示的な変換ヘルパーはネイティブアダプター専用。計算処理は as_decimal()/decimal_from_value
    // を使い、API が基本型を必要とするま
    // では正確な Decimal のまま保つ。
    [[nodiscard]] bool as_bool() const;
    [[nodiscard]] Integer as_integer() const;
    [[nodiscard]] Real as_real() const;
    [[nodiscard]] String as_string() const;
    [[nodiscard]] const Storage& storage() const { return storage_; }

private:
    Storage storage_{};
};

using Arguments = std::vector<Value>;
using KeywordArguments = std::unordered_map<String, Value>;
using ValueList = std::vector<Value>;
using ValueMap = std::map<String, Value>;
using ValueSet = std::vector<Value>;
using Callback = std::function<Value(const Arguments&, const KeywordArguments&)>;

struct Point {
    Integer x{};
    Integer y{};
};

struct Size {
    Integer width{};
    Integer height{};
};

struct Rect {
    Integer x{};
    Integer y{};
    Integer width{};
    Integer height{};
};

struct Color {
    std::uint8_t r{};
    std::uint8_t g{};
    std::uint8_t b{};
    std::uint8_t a{255};
};

// pygame/Pillow 向けの代替型。最終描画は SDL が行うが、これ
// らの型で Python に対応するモデルを SDL ヘッダーから独立させる。
struct Surface : Object {
    Size size{};
    std::vector<std::uint8_t> rgba;
    // 格納形式は常に RGBA だが、pygame の RGB サーフェスに画素ごとの
    // アルファはない。不透明な RGBA の画素からこのフラグを推定しない。固定小数点
    // の縮小ではアルファが 255 未満に丸まる場合がある。wx Image の HasAlpha
    // も、不透明 RGBA を含め同じチャンネル有無のフラグを使う。
    bool has_pixel_alpha{true};
    // pygame の論理的な一画素のビット幅。全ホストで格納形式は RGBA とし
    // 、通常の C++ の復号・変換済みサーフェスは共通の 32 ビット形式を使う。
    unsigned pixel_bits{32};
    // Python は Depth1Surface.scr_scale / bmpdepthis1 を動的に付加する。
    Real scr_scale{1.0};
    bool bmpdepthis1{};
    // pygame の Surface.set_colorkey() と Depth1Surface
    // 判定の状態。オブジェクト自身に保持し、再利用されたヒープアドレスに別画
    // 像のマスクフラグが引き継がれ、顔画像の透過が不定に失敗することを防ぐ。
    bool has_colorkey{};
    std::tuple<Integer, Integer, Integer> colorkey{};
    bool depth1{};
    Real logical_scale{1.0};
    // 描画先としてのサーフェスの倍率。論理単位あたりのデバイ
    // ス画素数を表す。共通の fill_rect、blend_surface
    // 、draw_label などは論理座標を受けてこ
    // の倍率で変換し、文字を一倍で描いて拡大せず、最終
    // サイズで画像化する。1 は通常の画素単位の面。
    Real draw_scale{1.0};
    // 頻繁な合成処理で使う、生成側の任意の保証。多
    // くの復号済み・変更可能な面ではアルファの不変
    // 条件が未確立なので、既定では安全側に扱う。
    bool known_opaque{};
    // pygame の Surface.set_alpha() に対応する面全体の不透明度。転送時に適
    // 用し、Fade など SRCALPHA なしの取得画像で、RGBA のバイトとは独立して使う。
    std::uint8_t surface_alpha{255};
    // アルファマスクのない RGB32 でも SDL_SRCALPHA を有効にでき
    // る。Py の画像補正はマスクではなくフラグを見るため、この状態を別に保持する。
    bool surface_alpha_enabled{};
};

// 共通の RGBA 格納形式で pygame の Surface.convert_alpha に
// 対応する。画素を複製し、RGB・カラーキーの透過をアルファへ反映して、元のオブジェクトを保つ。
std::shared_ptr<Surface> convert_surface_alpha(const std::shared_ptr<Surface>& source);

struct Image : Surface {};
struct Bitmap : Surface {};

// wx.Image.Rescale(..., IMAGE_QUALITY_NORMAL)
// に対応する共通処理。整数比の縮小はブロックを平均し、それ以外は最近傍を使う。
std::shared_ptr<Bitmap> rescale_bitmap_normal(const std::shared_ptr<Bitmap>& source, Size size);

struct Font : Object {
    Path path;
    Real point_size{16.0};
};

struct Sound : Object {
    Path path;
};

struct MusicStream : Object {
    Path path;
    bool looping{};
    bool playing{};
};

struct Sprite : Object {
    std::shared_ptr<Surface> image;
    Rect rect{};
    bool visible{true};
};

struct SpriteGroup : Object {
    std::vector<std::shared_ptr<Sprite>> sprites;
    // pygame.sprite.LayeredDirty は要素ごとに一
    // つのレイヤーを持つ。対応表をグループ内に置き、構築時の別名参照との互
    // 換性を保ちつつ、生のアドレスがグループの寿命を越えて残ることを防ぐ。
    std::map<const Sprite*, Integer> layers;
};

// wxPython 向けの代替型。アプリケーションのモデルに wx 型
// を公開せず、SDL UI やネイティブダイアログで表示を実装できる。
struct Event : Object {
    Integer type{};
    Point position{};
    // Window のイベントアダプターと同じ共通入力 ID・修飾キーを保持す
    // る。アプリケーションフィルターでキーイベントを空のデータに置き換えない。
    Integer id{};
    std::uint32_t modifiers{};
};

// 従来の wx 互換ダイアログと共通表示のダイアログで共有する、四分割の境界。
inline std::pair<Integer,Integer> sideclick_quarter_edges(const Rect& panel){const auto quarter=panel.width/4;return {panel.x+quarter,panel.x+quarter*3};}

struct Window : Object {
    String title;
    // 各 OS から Frame の共通 DROPFILES 処理へ渡す DropFilesEvent
    // の内容。OS アダプターはネイティブのパス一覧の復号だけを行う。
    std::vector<String> dropped_files{};
    // UIイベントがwx.CommandEvent.GetId()を運ぶための共通ID。
    Integer id{};
    // 共通 UI 境界で受け渡す修飾キー。ビット 0 は Shift。ネイティブ
    // Win32 ハンドラーは引き続き VK_SHIFT を直接確認できる。
    std::uint32_t modifiers{};
    Rect bounds{};
    bool visible{};
    bool enabled{true};
    bool wheel_horizontal{};
    bool cwpy_debug{};
    // ホストが指のイベントをこの共通ポインターイベントに変換した
    // 場合は true。ネイティブのマウス入力では false。
    bool touch{};
    std::function<void()> copy_detail{};
    std::weak_ptr<Window> parent_window{};
    std::function<bool(const String&)> is_playing{};
    std::function<bool(const String&)> is_complete{};
    std::function<bool(const String&)> is_invisible{};
    // ホストダイアログが渡す任意のネイティブ所有者。互換境界
    // に保持し、共通ダイアログモデルを Win32/SDL
    // 型に依存させずに、実際の親を尊重できるようにする。
    std::uintptr_t native_handle{};
};

struct Dialog : Window {
    std::function<void()> after_message_callback{};
    // ゲームのダイアログから開くデバッガー編集画面（デバッグ中
    // のキャラクター情報の経歴・解説・状態など）は、親と同じゲ
    // ームウィンドウ倍率 g_UP_WIN とゲーム用の配置を
    // 使う。デバッガーのデスクトップ DPI 倍率は使わない。
    bool game_window_scale{};
};
struct Frame : Window {};
struct Panel : Window {};
struct Control : Window {};
struct DeviceContext : Object {
    // 受け渡しだけを担当し、共通の wx.DC に現在の描画フォントの寸法を結び付
    // ける。省略の方針は cw::util に置き、全ホスト・OS で共有する。
    std::function<Size(const String&)> text_extent{};
};
struct Timer : Object {};
struct Sizer : Object {};
struct Menu : Object {};

struct XmlNode : Object {
    String tag;
    String text;
    std::map<String, String> attributes;
    std::vector<std::shared_ptr<XmlNode>> children;
};

struct XmlDocument : Object {
    std::shared_ptr<XmlNode> root;
};

struct SqlRow {
    std::vector<Value> columns;
};

struct SqlCursor : Object {
    std::vector<SqlRow> rows;
};

struct SqlConnection : Object {
    Path filename;
    bool open{};
};

struct Mutex : Object {
    void lock() { mutex.lock(); }
    void unlock() { mutex.unlock(); }
    bool try_lock() { return mutex.try_lock(); }
    std::recursive_mutex mutex;
};

struct Condition : Object {};

struct Thread : Object {
    std::thread worker;
};

struct Process : Object {
    Integer exit_code{};
};

struct Archive : Object {
    Path filename;
};

struct DynamicLibrary : Object {
    Path filename;
};

class FileSystem {
public:
    [[nodiscard]] static bool exists(const Path& path);
    [[nodiscard]] static Bytes read_binary(const Path& path);
    [[nodiscard]] static String read_text(const Path& path);
    static bool write_binary(const Path& path, const Bytes& data);
};

class Database {
public:
    [[nodiscard]] std::shared_ptr<SqlConnection> open(const Path& path) const;
    [[nodiscard]] std::shared_ptr<SqlCursor> execute(
        const std::shared_ptr<SqlConnection>& connection,
        const String& sql,
        const Arguments& parameters = {}) const;
};

class XmlParser {
public:
    [[nodiscard]] std::shared_ptr<XmlDocument> parse(const String& xml) const;
};

}  // namespace cw::lib
