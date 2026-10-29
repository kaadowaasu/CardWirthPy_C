#include "cw/cw.hpp"
#include "cw/argparser.hpp"
#include "cw/data.hpp"
#include "cw/image.hpp"
#include "cw/imageretouch.hpp"
#include "cw/lib/simd_image.hpp"
#include "cw/thread.hpp"
#include "cw/sprite/base.hpp"
#include "cw/util.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

using Integer = ::cw::lib::Integer;
using Real = ::cw::lib::Real;
using Value = ::cw::lib::Value;

Integer scaled_integer(Real value, Real scale)
{
    if (!std::isfinite(scale))
        throw std::invalid_argument("cw._s_impl: scale must be finite");
    // static_cast<Integer> は、有限値ではPythonのint()
    // と同じゼロ方向への切り捨てになる。 負の座標も含め、意図的にこの動作を使う。
    const Real scaled = value * scale;
    const Real lower_inclusive = -std::ldexp(1.0, std::numeric_limits<Integer>::digits);
    const Real upper_exclusive = std::ldexp(1.0, std::numeric_limits<Integer>::digits);
    if (!std::isfinite(scaled) || scaled >= upper_exclusive || scaled < lower_inclusive)
        throw std::out_of_range("cw._s_impl: scaled integer is out of range");
    return static_cast<Integer>(scaled);
}

std::optional<Integer> integer_value(const Value& value)
{
    if (const auto* boolean = std::get_if<bool>(&value.storage())) return *boolean ? 1 : 0;
    if (const auto* integer = std::get_if<Integer>(&value.storage())) return *integer;
    return std::nullopt;
}

Value scale_sequence(const std::shared_ptr<::cw::data::VariantSequence>& source, Real scale)
{
    if (!source || (source->m_values.size() != 2 && source->m_values.size() != 4))
        throw std::invalid_argument("cw._s_impl: expected a 2- or 4-element numeric tuple");
    auto result = std::make_shared<::cw::data::VariantSequence>();
    result->m_values.reserve(source->m_values.size());
    for (const auto& value : source->m_values) {
        const auto integer = integer_value(value);
        if (integer) result->m_values.emplace_back(scaled_integer(static_cast<Real>(*integer), scale));
        else if (const auto* real = std::get_if<Real>(&value.storage()))
            result->m_values.emplace_back(scaled_integer(*real, scale));
        else throw std::invalid_argument("cw._s_impl: tuple elements must be numeric");
    }
    return Value(std::static_pointer_cast<::cw::lib::Object>(result));
}

std::shared_ptr<::cw::lib::Surface> scale_surface(const std::shared_ptr<::cw::lib::Surface>& source, Real scale)
{
    if (!source || source->size.width <= 0 || source->size.height <= 0) return source;
    const auto metadata = ::cw::imageretouch::get_surface_metadata(source);
    if (!std::isfinite(metadata.logical_scale) || metadata.logical_scale <= 0.0)
        throw std::invalid_argument("cw._s_impl: invalid logical image scale");
    // ネイティブの意図的な表現の違いとして、一つのSurface系統でpygame
    // ・wx画像を扱う。 記録した論理倍率で割り、キャッシュ画像を二重
    // に拡大しない。 サイズ変更したwx画像には新しい所有者を与え、
    // wx.Image.Rescaleのその場の同一性は維持しない。
    const Real effective_scale = scale / metadata.logical_scale;
    if (effective_scale == 1.0) return source;
    const Integer width = scaled_integer(source->size.width, effective_scale);
    const Integer height = scaled_integer(source->size.height, effective_scale);
    if (width <= 0 || height <= 0) return source;
    const bool integral = std::floor(effective_scale) == effective_scale;
    const bool wx_image = std::dynamic_pointer_cast<::cw::lib::Bitmap>(source) ||
        std::dynamic_pointer_cast<::cw::lib::Image>(source);
    auto pixels = source;
    auto result_metadata = metadata;
    // Depth1Surfaceは通常の拡大縮小画像も包み、pygameの平滑化を
    // 無効にするものではない。 Python版と同様、小数倍率の変更前にRGBと透
    // 明色をアルファへ変換する。 depth1で最近傍を選ぶのはwxの分岐だけ。
    if (!wx_image && !integral && !source->has_pixel_alpha && metadata.has_colorkey) {
        pixels = std::make_shared<::cw::lib::Surface>(*source);
        pixels->has_pixel_alpha = true;
        pixels->pixel_bits = 32;
        const auto [r, g, b] = metadata.colorkey;
        ::cw::lib::simd_image::force_alpha(pixels->rgba.data(), pixels->rgba.size() / 4U, 255);
        ::cw::lib::simd_image::colorkey_alpha_zero(pixels->rgba.data(), pixels->rgba.size() / 4U,
            static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g), static_cast<std::uint8_t>(b), true);
        pixels->has_colorkey = false;
        result_metadata.has_colorkey = false;
    }
    auto scaled = ::cw::image::smoothscale(pixels, {width, height},
        !integral && (!wx_image || !metadata.depth1), false);
    std::shared_ptr<::cw::lib::Surface> result = scaled;
    if (std::dynamic_pointer_cast<::cw::lib::Bitmap>(source)) {
        auto bitmap = std::make_shared<::cw::lib::Bitmap>(); bitmap->size = scaled->size; bitmap->rgba = scaled->rgba;
        if (const auto source_bitmap = std::dynamic_pointer_cast<::cw::lib::Bitmap>(source)) ::cw::util::copy_animated_wxbmp_info(source_bitmap, bitmap);
        result = bitmap;
    } else if (std::dynamic_pointer_cast<::cw::lib::Image>(source)) {
        auto image = std::make_shared<::cw::lib::Image>(); image->size = scaled->size; image->rgba = scaled->rgba; result = image;
    }
    // 返す画素は要求された論理倍率を表すため、その倍
    // 率を記録する。 元の倍率を残すと、二倍のキャッ
    // シュボタンが一倍と申告し、 後のダイアログで
    // 未拡大の判定や二重拡大の防止ができなくなる。
    result_metadata.logical_scale = scale;
    ::cw::imageretouch::set_surface_metadata(result, result_metadata);
    result->bmpdepthis1 = source->bmpdepthis1;
    result->scr_scale = scale;
    return result;
}

/*
 * pygame.sprite.LayeredDirty と同様、
 * グループとspriteの組ごとに層を持ち、昇順で描く。 互換Sprite
 * は意図的にバックエンド非依存で、層メンバーを持たない。 小
 * さなグループ状態をここで保持し、SDL/wx型を公開ABI
 * へ出さずに add_layer() の同じ順序を実現する。
 */
std::shared_ptr<::cw::argparser::ArgParser> make_argparser()
{
    auto parser = std::make_shared<::cw::argparser::ArgParser>();
    parser->initialize(::cw::g_APP_NAME,
                       ::cw::g_APP_NAME + " " + std::to_string(std::get<0>(::cw::g_APP_VERSION)) + "." +
                           std::get<1>(::cw::g_APP_VERSION) +
                           "\n\nオープンソースのCardWirthエンジン");
    parser->add_argument("-h", ::cw::argparser::ArgType::Boolean, 0,
                         "このメッセージを表示して終了します。", "--help");
    parser->add_argument("-debug", ::cw::argparser::ArgType::Boolean, 0,
                         "デバッグモードで起動します。");
    parser->add_argument("-yado", ::cw::argparser::ArgType::String, 1,
                         "起動と同時に<YADO>のパスにある拠点を読み込みます。", {},
                         ::cw::argparser::ArgValue{::cw::lib::String{}});
    parser->add_argument("-party", ::cw::argparser::ArgType::String, 1,
                         "起動と同時に<PARTY>のパスにあるパーティを読み込みます。\n"
                         "-yadoと同時に指定しなかった場合は無視されます。", {},
                         ::cw::argparser::ArgValue{::cw::lib::String{}});
    parser->add_argument("-scenario", ::cw::argparser::ArgType::String, 1,
                         "起動と同時に<SCENARIO>のパスにあるシナリオを開始します。\n"
                         "-yado及び-partyと同時に指定しなかった場合は無視されます。", {},
                         ::cw::argparser::ArgValue{::cw::lib::String{}});
    parser->add_argument("-skin", ::cw::argparser::ArgType::String, 1,
                         "<SKIN>のパスにあるスキンで起動します。\n"
                         "起動と同時に拠点が開かれる場合は拠点のスキンが優先されます。", {},
                         ::cw::argparser::ArgValue{::cw::lib::String{}});
    parser->add_argument("--force-skin", ::cw::argparser::ArgType::String, 1,
                         "<SKIN>のパスにあるスキンで起動します。\n"
                         "拠点のスキンや、-skinよりも優先されます。", {},
                         ::cw::argparser::ArgValue{::cw::lib::String{}}, "SKIN");
    parser->add_argument("--debug-skin", ::cw::argparser::ArgType::Boolean, 0,
                         "スキンデバッグモードで起動します。");
    return parser;
}

::cw::lib::Value make_default_options(const std::shared_ptr<::cw::argparser::ArgParser>& parser)
{
    if (!parser) return {};
    // Python版はパッケージ取り込み時にsys.argvを解析する。 C++
    // のプロセス引数は実行ファイルのフロントエンドが管理するため、ここでは空入力
    // を使い、 ライブラリ利用側に静的初期化時の入出力なしで同じ既定値を渡す。
    const auto parsed = parser->parse_args(std::vector<::cw::lib::String>{});
    if (!parsed || !*parsed) return {};
    return ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(*parsed));
}

}  // namespace

namespace cw {
::cw::lib::String g_APP_NAME{"CardWirthPy_C"};
std::tuple<::cw::lib::Integer, ::cw::lib::String> g_APP_VERSION{5, "3"};
std::tuple<::cw::lib::Value, ::cw::lib::Value, ::cw::lib::Value, ::cw::lib::Value, ::cw::lib::Value> g_AREAS_SP{-1, -2, -3, -4, -5};
std::tuple<::cw::lib::Integer> g_AREAS_TITLE{1};
std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> g_AREAS_TRADE{-1, -2, -5};
std::tuple<::cw::lib::Value, ::cw::lib::Value, ::cw::lib::Value, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> g_AREAS_YADO{-3, -2, -1, 1, 2, 3, 4};
::cw::lib::Value g_AREA_BREAKUP{-3};
::cw::lib::Value g_AREA_CAMP{-4};
::cw::lib::Value g_AREA_TRADE1{-1};
::cw::lib::Value g_AREA_TRADE2{-2};
::cw::lib::Value g_AREA_TRADE3{-5};
::cw::lib::String g_DEFAULT_SOUNDFONT{"Data/SoundFont/005.6mg_Aspirin_Stereo_V1.2_Bank.sf2"};
std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String> g_EXTS_IMG{".bmp",".jpg",".jpeg",".png",".gif",".pcx",".tif",".tiff",".xpm",".webp",".apng"};
std::tuple<::cw::lib::String, ::cw::lib::String> g_EXTS_MIMG{".webp",".apng"};
std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String> g_EXTS_MSC{".mid",".midi",".mp3",".ogg"};
std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String> g_EXTS_SND{".wav",".wave",".ogg"};
// SDLアダプターは pygame.USEREVENT を安定した互換イベント24へ変換する。
// そのため、ネイティブのイベントABIでは FORCE_USEREVENT は25となる。
::cw::lib::Integer g_FORCE_USEREVENT{25};
::cw::lib::Integer g_HINT_AREA{2};
::cw::lib::Integer g_HINT_CARD{1};
::cw::lib::Integer g_HINT_MESSAGE{0};
::cw::lib::Integer g_HINT_SCENARIO{3};
::cw::lib::Value g_IDX_TREEEND{-1};
::cw::lib::Integer g_LAYER_BACKGROUND{0};
std::tuple<::cw::lib::Value, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> g_LAYER_BATTLE_START{0x00200000, 6, 0x000ffffe, 0};
::cw::lib::Integer g_LAYER_CLICKABLE_SPRITES{0};
::cw::lib::Integer g_LAYER_FCARDS{1000};
::cw::lib::Value g_LAYER_FCARDS_T{0x00200000};
::cw::lib::Integer g_LAYER_FRONT{0x00200000};
std::tuple<::cw::lib::Value, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> g_LAYER_FRONT_INUSECARD{0x00200000, 7, 0x000ffffe, 0};
std::tuple<::cw::lib::Value, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> g_LAYER_FRONT_LIFEBAR{0x00200000, 8, 0x000ffffe, 0};
::cw::lib::Integer g_LAYER_JPY_TEMPORAL{1};
std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> g_LAYER_LOG{2001,0,0,0};
std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> g_LAYER_LOG_BAR{2002,0,0,0};
std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> g_LAYER_LOG_CURTAIN{2000,0,0,0};
std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> g_LAYER_LOG_PAGE{2003,0,0,0};
std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> g_LAYER_LOG_SCROLLBAR{2004,0,0,0};
::cw::lib::Integer g_LAYER_MAX{0x000fffff};
::cw::lib::Integer g_LAYER_MCARDS{100};
::cw::lib::Integer g_LAYER_MCARDS_120{300};
std::tuple<::cw::lib::Integer, ::cw::lib::Value, ::cw::lib::Integer, ::cw::lib::Integer> g_LAYER_MESSAGE{1000, 1, 0, 0};
::cw::lib::Integer g_LAYER_NUMBER_OF_CARDS{0};
::cw::lib::Integer g_LAYER_PCARDS{200};
std::tuple<::cw::lib::Integer, ::cw::lib::Value, ::cw::lib::Integer, ::cw::lib::Integer> g_LAYER_SELECTIONBAR_1{1000, 1, 1, 0};
std::tuple<::cw::lib::Integer, ::cw::lib::Value, ::cw::lib::Integer, ::cw::lib::Integer> g_LAYER_SELECTIONBAR_2{1000, 1, 2, 0};
::cw::lib::Value g_LAYER_SPBACKGROUND{0x00100000};
std::tuple<::cw::lib::Integer, ::cw::lib::Value, ::cw::lib::Integer, ::cw::lib::Integer> g_LAYER_SPMESSAGE{0x001003e8, 1, 0, 0};
std::tuple<::cw::lib::Integer, ::cw::lib::Value, ::cw::lib::Integer, ::cw::lib::Integer> g_LAYER_SPSELECTIONBAR_1{0x001003e8, 1, 1, 0};
std::tuple<::cw::lib::Integer, ::cw::lib::Value, ::cw::lib::Integer, ::cw::lib::Integer> g_LAYER_SPSELECTIONBAR_2{0x001003e8, 1, 2, 0};
::cw::lib::Integer g_LAYER_SP_LAYER{0x00100000};
std::tuple<::cw::lib::Value, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> g_LAYER_TARGET_ARROW{0x00200000, 9, 0x000ffffe, 0};
::cw::lib::Integer g_LAYER_TITLE{0};
std::tuple<::cw::lib::Value, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> g_LAYER_TRANSITION{0x00200000, 10, 0x000ffffe, 0};
::cw::lib::Integer g_LIMIT_RECURSE{10000};
::cw::lib::Integer g_LINDEX_MAX{0x00100000};
::cw::lib::Integer g_LOG_SEPARATOR_LEN_LONG{80};
::cw::lib::Integer g_LOG_SEPARATOR_LEN_MIDDLE{60};
::cw::lib::Integer g_LOG_SEPARATOR_LEN_SHORT{45};
::cw::lib::Integer g_LTYPE2_MAX{2};
::cw::lib::Integer g_LTYPE_BACKGROUND{2};
::cw::lib::Integer g_LTYPE_BACKGROUND_CURTAIN{3};
::cw::lib::Integer g_LTYPE_FCARDS{0};
::cw::lib::Integer g_LTYPE_MAX{11};
::cw::lib::Integer g_LTYPE_MCARDS{4};
::cw::lib::Integer g_LTYPE_MESSAGE{1};
::cw::lib::Integer g_LTYPE_PCARDS{5};
::cw::lib::Integer g_LTYPE_SPMCARDS{4};
::cw::lib::Integer g_LTYPE_SPMESSAGE{1};
#ifdef _WIN32
::cw::lib::String g_MBCS{"mbcs"};
#else
::cw::lib::String g_MBCS{"ms932"};
#endif
::cw::lib::Integer g_M_IMG{0};
::cw::lib::Integer g_M_MSC{1};
::cw::lib::Integer g_M_SND{2};
// 解析処理の宣言後、下で初期化する。 OPTIONSを空のValueで
// はなく、Python版の既定値を持つ実際のArgResultにする。
::cw::lib::Integer g_POCKET_BEAST{2};
::cw::lib::Integer g_POCKET_ITEM{1};
::cw::lib::Integer g_POCKET_PERSONAL{3};
::cw::lib::Integer g_POCKET_SKILL{0};
std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> g_RECT_STATUSBAR{0,420,632,33};
// wx.IMAGE_QUALITY_BILINEAR は列挙値1で表す。 SDL
// 画像バックエンドの補間は、現在 _s_impl の共通変換経路を使う。
::cw::lib::Value g_RESCALE_QUALITY{1};
std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> g_SCALE_LIST{2,4,8,16};
std::tuple<::cw::lib::Integer, ::cw::lib::Integer> g_SIZE_AREA{632,420};
std::tuple<::cw::lib::Integer, ::cw::lib::Integer> g_SIZE_BILL{400,370};
std::tuple<::cw::lib::Integer, ::cw::lib::Integer> g_SIZE_BOOK{460,280};
std::tuple<::cw::lib::Integer, ::cw::lib::Integer> g_SIZE_CARDIMAGE{74,94};
std::tuple<::cw::lib::Integer, ::cw::lib::Integer> g_SIZE_GAME{632,453};
std::tuple<::cw::lib::Integer, ::cw::lib::Integer> g_SIZE_SCR{640,480};
::cw::lib::Integer g_SKIN_AREAS_MAX{20000};
::cw::lib::Integer g_SKIN_AREAS_MIN{10001};
std::vector<::cw::lib::String> g_SKIN_CONV_ARGS{};
std::vector<::cw::lib::String> skin_conversion_arguments(const std::vector<::cw::lib::String>& leftovers)
{
    std::vector<::cw::lib::String> result;
    for(const auto& path:leftovers){
        auto extension=std::get<1>(::cw::util::splitext(path));
        std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return c>='A'&&c<='Z'?static_cast<char>(c-'A'+'a'):static_cast<char>(c);});
        if(extension==".exe"&&::cw::util::path_is_file(path))result.push_back(path);
    }
    return result;
}

std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String> g_SUPPORTED_SKIN{"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12"};
std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String> g_SUPPORTED_WSN{"", "1", "2", "3", "4", "5", "6"};
// PythonのTypeVarは実行時データを持たない。 空のValueを意
// 図的なABI上の印とし、受け付ける具体型は _s_impl で検査する。
::cw::lib::Value g_Scalable{};
::cw::lib::Real g_UP_SCR{1.0};
::cw::lib::Real g_UP_WIN{1.0};
::cw::lib::Real g_UP_WIN_M{1.0};
std::vector<::cw::lib::String> g_py_all{"util", "battle", "yadodb", "data", "dice", "effectmotion", "event", "eventhandler", "eventrelay", "features", "scenariodb", "setting", "skin", "animation", "thread", "header", "image", "imageretouch", "frame", "deck", "character", "effectbooster", "content", "xmlcreater", "bassplayer", "binary", "advlog", "update", "calculator", "animatedimage", "dialog", "debug", "sprite", "argparser", "nctype"};
std::shared_ptr<::cw::argparser::ArgParser> g__argparser{make_argparser()};
::cw::lib::Value g__options{make_default_options(g__argparser)};
::cw::lib::Value g_OPTIONS{g__options};
// C++では翻訳単位をまたぐglobal初期化順序が未規定である。CWPyのconstructorは
// Resource/Data等のglobalを参照するため、実行入口が作業directory・SDL・COMを
// 初期化した後、Frame::_start_wxから生成する。Pythonのimport時生成と同じ状態は
// native環境の準備後に確立される。
std::shared_ptr<::cw::thread::CWPy> g_cwpy{};
::cw::lib::Integer g_dpi_level{1};
::cw::lib::String g_exepath{};
#ifdef _WIN32
::cw::lib::String g_filesystem_encoding{"mbcs"};
#else
::cw::lib::String g_filesystem_encoding{"utf-8"};
#endif
// Python版はFileSyncを先に作るがスレッドは開始しない。 C++
// 版も、呼び出し元が run() を呼ぶまで同じ遅延・既定の状態に保つ。
std::shared_ptr<::cw::util::FileSync> g_fsync{std::make_shared<::cw::util::FileSync>()};
std::atomic_bool g_quit_app{false};
::cw::lib::Value g_tempdir{"Data/Temp/Global"};
::cw::lib::String g_tempdir_init{"Data/Temp/Global"};

::cw::lib::Integer layer_val(std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> arg_tlayer)
{
    const auto [layer, type, index, type2] = arg_tlayer;
    if (layer < 0 || type < 0 || index < -1 || type2 < 0 || g_LAYER_FRONT < layer)
        throw std::invalid_argument("layer component assertion failed");
    if (g_LTYPE_MAX <= type)
        throw std::invalid_argument("layer type exceeds encoding range");
    if (g_LINDEX_MAX <= index + 1)
        throw std::invalid_argument("Invalid index: " + std::to_string(g_LINDEX_MAX) + " <= " +
                                    std::to_string(index) + " + 1");
    if (g_LTYPE2_MAX <= type2)
        throw std::invalid_argument("Invalid background index: " + std::to_string(g_LTYPE2_MAX) + " <= " +
                                    std::to_string(type2));
    return layer * (g_LTYPE_MAX * g_LINDEX_MAX * g_LTYPE2_MAX)
        + type * (g_LINDEX_MAX * g_LTYPE2_MAX) + (index + 1) * g_LTYPE2_MAX + type2;
}

void add_layer(std::shared_ptr<::cw::lib::Object> arg_grp, const ::cw::lib::Arguments& arg_sprites, ::cw::lib::Integer arg_layer)
{
    const auto group = std::dynamic_pointer_cast<::cw::lib::SpriteGroup>(arg_grp);
    if (!group) return;
    auto& layers = group->layers;
    for (auto it = layers.begin(); it != layers.end();) {
        const auto member = std::find_if(group->sprites.begin(), group->sprites.end(),
            [&](const auto& sprite) { return sprite.get() == it->first; });
        if (member == group->sprites.end()) it = layers.erase(it);
        else ++it;
    }
    for (const auto& value : arg_sprites) {
        const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
        if (!object || !*object) continue;
        if (const auto sprite = std::dynamic_pointer_cast<::cw::lib::Sprite>(*object)) {
            // pygameのadd()と同様、既存メンバーの層・順序は変えない。 明示的な変更に
            // は change_layer を使い、add_layer を繰り返して代用しない。
            if (std::find(group->sprites.begin(), group->sprites.end(), sprite) != group->sprites.end()) continue;
            layers[sprite.get()] = arg_layer;
            // 生のSpriteとモデルの代理要素で、一つの挿入・順序の方針を共有する。
            ::cw::sprite::base::insert_sprite_group_by_layer(group, sprite);
        }
    }
}

::cw::lib::Value wins(::cw::lib::Value arg_num)
{
    return _s_impl(std::move(arg_num), g_UP_WIN);
}

::cw::lib::Value s(::cw::lib::Value arg_num)
{
    return _s_impl(std::move(arg_num), g_UP_SCR);
}

// 四つの倍率はネイティブの浮動小数点除算を使う。 不正な分母ゼロでPython
// 版は除算時に例外を出すが、ネイティブの _s_impl は整数サイ
// ズへ変換するときに非有限値を検査する。 新しい比率ガードは加えず
// この境界差を維持し、通常の設定倍率はゼロでないことを前提とする。
::cw::lib::Value scr2win_s(::cw::lib::Value arg_num)
{
    return _s_impl(std::move(arg_num), g_UP_WIN == g_UP_SCR ? 1.0 : g_UP_WIN / g_UP_SCR);
}

::cw::lib::Value win2scr_s(::cw::lib::Value arg_num)
{
    return _s_impl(std::move(arg_num), g_UP_WIN == g_UP_SCR ? 1.0 : g_UP_SCR / g_UP_WIN);
}

::cw::lib::Value scr2mwin_s(::cw::lib::Value arg_num)
{
    return _s_impl(std::move(arg_num), g_UP_WIN_M == g_UP_SCR ? 1.0 : g_UP_WIN_M / g_UP_SCR);
}

::cw::lib::Value mwin2scr_s(::cw::lib::Value arg_num)
{
    return _s_impl(std::move(arg_num), g_UP_WIN_M == g_UP_SCR ? 1.0 : g_UP_SCR / g_UP_WIN_M);
}

::cw::lib::Value _s_impl(::cw::lib::Value arg_num, ::cw::lib::Real arg_up_scr)
{
    if (arg_up_scr == 1.0)
        return arg_num;
    // Pythonのboolはintの派生型なので、整
    // 数の分岐に従う。 真偽値を保持せず、整数を返す。
    if (const auto* value = std::get_if<bool>(&arg_num.storage()))
        return scaled_integer(*value ? 1 : 0, arg_up_scr);
    if (const auto* value = std::get_if<::cw::lib::Integer>(&arg_num.storage()))
        return scaled_integer(*value, arg_up_scr);
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&arg_num.storage()); object && *object) {
        if (const auto rect = std::dynamic_pointer_cast<::cw::RectValue>(*object)) {
            auto result = std::make_shared<::cw::RectValue>(rect->rect);
            result->rect.x = scaled_integer(rect->rect.x, arg_up_scr);
            result->rect.y = scaled_integer(rect->rect.y, arg_up_scr);
            result->rect.width = scaled_integer(rect->rect.width, arg_up_scr);
            result->rect.height = scaled_integer(rect->rect.height, arg_up_scr);
            return Value(std::static_pointer_cast<::cw::lib::Object>(result));
        }
        if (const auto sequence = std::dynamic_pointer_cast<::cw::data::VariantSequence>(*object))
            return scale_sequence(sequence, arg_up_scr);
        // scale_surface が論理倍率の補正とネイティブ
    // のwx画像の同一性の境界を管理する。 共通の表現による
    // 意図的な差であり、別のOS固有の拡大縮小実装ではない。
    if (const auto surface = std::dynamic_pointer_cast<::cw::lib::Surface>(*object))
            return Value(std::static_pointer_cast<::cw::lib::Object>(scale_surface(surface, arg_up_scr)));
    }
    // Python版が未対応のScalableにAssertionError
    // を出すのに合わせ、 C++版は型付き例外で早期に
    // 失敗させ、ビルドのNDEBUG設定に依存しない。
    throw std::invalid_argument("cw._s_impl: unsupported Scalable value");
}

::cw::lib::Value uns(::cw::lib::Value arg_num)
{
    return g_UP_SCR == 0.0 ? arg_num : _s_impl(std::move(arg_num), 1.0 / g_UP_SCR);
}

::cw::lib::Value ppis(::cw::lib::Value arg_num)
{
    return _s_impl(std::move(arg_num), static_cast<::cw::lib::Real>(g_dpi_level));
}

void main()
{
    // Pythonのmain()と同様、意図的に何もしない。 解析処理と既定値は
    // 上で構築し、実行ファイル側がプロセス引数を解析して、 起動前に g_OPTIONS/g_SKIN_CONV_ARGS
    // を置き換えられるようにする。
}

}
