#pragma once

// cw/setting.pyに対
// 応する、設定と表示資源の宣言。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"
#include <array>
#include <map>
#include <functional>
#include <memory>
#include <regex>
#include <set>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace cw::lib::python_re { class Pattern; }

namespace cw::setting {

class Resource;

// CardWirthPy_C_All専用の右側ショートカット。
// Engineの横に、縦型ステータスダイアログと対称に並べる。
// 列挙値の順序は設定画面の選択肢の順序に対応する。 FullRecovery
// はデバッガーの全回復、CustomStatusはm_all_custom_status
// の状態を パーティ全員へ適用する操作で
// 、どちらもALL専用のデバッグ機能。
enum class AllStatusShortcut { None, Save, Load, Screenshot, Instructions, CardScreenshot, FullRecovery, CustomStatus };
inline constexpr std::size_t kAllStatusShortcutSlots = 6;
using AllStatusShortcuts = std::array<AllStatusShortcut, kAllStatusShortcutSlots>;
// Save,Load,Screenshot,Instructions,,のように、名前をカンマで区切って保存する。
[[nodiscard]] AllStatusShortcuts parse_all_status_shortcuts(const ::cw::lib::String& text);
[[nodiscard]] ::cw::lib::String format_all_status_shortcuts(const AllStatusShortcuts& shortcuts);
inline constexpr const char* kAllStatusShortcutsDefault = "Save,Load,Screenshot,Instructions,,";

// LocalSetting.fonttypes/msg_exfontsに対応する型付きの表現。
using FontSettingValue = std::tuple< ::cw::lib::String, ::cw::lib::String,
                                     ::cw::lib::Integer, bool, bool, bool>;
// 保存するLocalSettingのスタイルはboolだが、Pyの明示的なfontinfo指
// 定にはNoneもある。 create_fontが既定値を適用するまで、その状態を保持する。
using FontRequestValue = std::tuple<::cw::lib::String, ::cw::lib::String,
    ::cw::lib::Integer, std::optional<bool>, std::optional<bool>, std::optional<bool>>;
using ResolvedFontRequest = std::tuple<::cw::lib::String, ::cw::lib::Integer,
    std::optional<bool>, std::optional<bool>, std::optional<bool>>;
using SoundFontValue = std::tuple< ::cw::lib::String, bool, ::cw::lib::Integer>;

// ネイティブ資源はこのコールバックの裏で扱う。下の共通実装は常
// に利用可能とし、 Windows/SDLホストは必要な操作だ
// けを置き換えられる。 Python互換の設定モデルへHWND
// ・SDL_ttf・SDL_mixerのハンドルを公開しない。
struct SettingBackend {
    using FontEnumerator = std::function<std::map<::cw::lib::String, ::cw::lib::String>(
        const ::cw::lib::String&, const std::set<::cw::lib::String>&)>;
    using FontInstaller = std::function<bool(const ::cw::lib::String&)>;
    using FontRemover = std::function<void(const ::cw::lib::String&)>;
    using FontFactory = std::function<std::shared_ptr<::cw::imageretouch::Font>(
        const ::cw::lib::String&, ::cw::lib::Integer, bool, bool)>;
    using CursorFactory = std::function<std::shared_ptr<::cw::lib::Window>(
        const ::cw::lib::String&, const ::cw::lib::String&)>;
    using SoundFactory = std::function<std::shared_ptr<::cw::util::SoundInterface>(
        const ::cw::lib::String&, bool)>;
    using ButtonFactory = std::function<::cw::lib::Value(
        std::shared_ptr<::cw::lib::Panel>, ::cw::lib::Integer,
        std::tuple<::cw::lib::Integer, ::cw::lib::Integer>,
        std::optional<::cw::lib::String>,
        std::optional<std::shared_ptr<::cw::lib::Bitmap>>, bool)>;
    using ButtonTimer = std::function<void(std::shared_ptr<::cw::lib::Window>)>;
    using UiTask = std::function<void()>;
    using UiMessage = std::function<void(const ::cw::lib::String&, const ::cw::lib::String&, bool)>;
    using DebugReload = std::function<void(Resource&)>;

    FontEnumerator enumerate_fonts{};
    FontInstaller install_font{};
    FontRemover remove_font{};
    std::function<void()> notify_font_change{};
    FontFactory create_font{};
    CursorFactory create_cursor{};
    SoundFactory load_sound{};
    ButtonFactory create_button{};
    ButtonTimer start_button_repeat{};
    ButtonTimer tick_button_repeat{};
    ButtonTimer stop_button_repeat{};
    // ワーカーから設定コールバックを呼ぶ場合、UI操作はここか
    // ら配送する。 既定のWindows実装は所有スレッドで処
    // 理し、SDLホストは自分のイベントループへ接続できる。
    std::function<void(UiTask)> dispatch_ui{};
    std::function<void()> pump_ui{};
    UiMessage show_message{};
    DebugReload reload_debug_images{};
};

[[nodiscard]] std::uint64_t cursor_resource_generation() noexcept;
// 共通Resourceへ登録済みのカーソルを適用する。
// SDL・ネイティブUIを所有するスレッドから呼ぶこと。
bool apply_native_cursor(const ::cw::lib::String& name);
// 共通の論理名から必要時にOSのカーソルを作って適用する。
// WindowsとSDLのデスクトップホストで共用する。
bool ensure_native_cursor(const ::cw::lib::String& name);

struct DirectionCursorImage {
    std::shared_ptr<::cw::lib::Surface> surface;
    ::cw::lib::Point hotspot;
};
// SDLには標準のPOINT_LEFT/POINT_RIGHTがないため、共通の固定画
// 像で代替する。 .curがないか不正な場合も、Pythonの左右の方向を区別する。
[[nodiscard]] DirectionCursorImage direction_cursor_fallback(bool forward);
// Py標準のカーソル画像と基点。OSのカーソル作成もこの共通データを使う。
[[nodiscard]] std::optional<DirectionCursorImage> pygame_cursor_fallback(const ::cw::lib::String& name);

void set_backend(std::shared_ptr<SettingBackend> arg_backend);
std::shared_ptr<SettingBackend> backend();
std::shared_ptr<SettingBackend> make_default_backend();
// Frameの起動時に、既定の実装を実際のホスト・UIスレッドへ結び付け
// る。 最初に呼んだ任意のワーカーがUIの所有者にならないようにする。
void bind_default_ui_thread();

class LazyResource : public ::cw::lib::Object {
public:
    LazyResource() = default;
    virtual ~LazyResource() = default;
    std::optional<::cw::lib::Value> m__res{};
    ::cw::lib::Value m_args{};
    bool m_failure{};
    ::cw::lib::Value m_func{};
    ::cw::lib::Value m_kwargs{};
    bool m_load{};
    ::cw::lib::Callback m_callback{};
    std::vector<::cw::lib::Value> m_arglist{};
    ::cw::lib::ValueMap m_kwargmap{};
    void initialize(::cw::lib::Callback arg_func, std::vector<::cw::lib::Value> arg_args, ::cw::lib::ValueMap arg_kwargs);
    void clear();
    ::cw::lib::Value get_res();
};

class LocalSetting : public ::cw::lib::Object {
public:
    LocalSetting() = default;
    virtual ~LocalSetting() = default;
    std::map<::cw::lib::String, ::cw::lib::String> m_basefont{};
    std::map<::cw::lib::String, ::cw::lib::String> m_basefont_init_typed{
        {"gothic", ""}, {"uigothic", ""}, {"mincho", ""}, {"pmincho", ""}, {"pgothic", ""}};
    ::cw::lib::Value m_basefont_init{};
    ::cw::lib::Value m_blcurtaincolour{};
    ::cw::lib::Value m_blcurtaincolour_init{};
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> m_blwincolour{};
    ::cw::lib::Value m_blwincolour_init{};
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> m_blwinframecolour{};
    ::cw::lib::Value m_blwinframecolour_init{};
    bool m_bordering_cardname{};
    ::cw::lib::Value m_bordering_cardname_init{};
    ::cw::lib::Value m_curtaincolour{};
    ::cw::lib::Value m_curtaincolour_init{};
    bool m_decorationfont{};
    ::cw::lib::Value m_decorationfont_init{};
    bool m_fontsmoothing_cardname{};
    ::cw::lib::Value m_fontsmoothing_cardname_init{};
    bool m_fontsmoothing_message{};
    ::cw::lib::Value m_fontsmoothing_message_init{};
    bool m_fontsmoothing_statusbar{};
    ::cw::lib::Value m_fontsmoothing_statusbar_init{};
    // 生成済みUIコード向けの互換用の保持値。
    std::map<::cw::lib::String, ::cw::lib::Value> m_fonttypes{};
    ::cw::lib::Value m_fonttypes_init{};
    // 正式な値は型付きフォントタプルのマップへ保持する。
    std::map<::cw::lib::String, ::cw::setting::FontSettingValue> m_fonttypes_typed{};
    std::map<::cw::lib::String, ::cw::setting::FontSettingValue> m_fonttypes_init_typed{};
    ::cw::lib::String m_fullscreenbackgroundfile{};
    ::cw::lib::Value m_fullscreenbackgroundfile_init{};
    ::cw::lib::Integer m_fullscreenbackgroundtype{};
    ::cw::lib::Value m_fullscreenbackgroundtype_init{};
    bool m_important_draw{};
    ::cw::lib::Value m_important_draw_init{};
    bool m_important_font{};
    ::cw::lib::Value m_important_font_init{};
    std::map<::cw::lib::String, std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::Integer, bool, bool, bool>> m_msg_exfonts{};
    ::cw::lib::Value m_msg_exfonts_init{};
    std::map<::cw::lib::String, ::cw::setting::FontSettingValue> m_msg_exfonts_init_typed{};
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> m_mwincolour{};
    ::cw::lib::Value m_mwincolour_init{};
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> m_mwinframecolour{};
    ::cw::lib::Value m_mwinframecolour_init{};
    // ALL仕様: エンジン表示全体の外枠。メッセージ枠とは独立して変更可能。
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> m_allengineframecolour{128,0,0,255};
    void initialize();
    void init();
    void load(::cw::lib::Value arg_data);
    void load(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    std::shared_ptr<::cw::setting::LocalSetting> copy();
};

class MsgDict : public ::cw::lib::Object {
public:
    MsgDict() = default;
    virtual ~MsgDict() = default;
    std::map<::cw::lib::String, ::cw::lib::String> m__d{};
    std::set<::cw::lib::String> m__error_keys{};
    void initialize();
    void set_item(::cw::lib::String arg_key, ::cw::lib::String arg_msg);
    ::cw::lib::String get_item(::cw::lib::String arg_key);
    ::cw::lib::String get(::cw::lib::String arg_key, ::cw::lib::String arg_defvalue);
};

class NoFontError : public ::cw::lib::Object, public std::invalid_argument {
public:
    // PyはこのエラーをValueErrorの派生型とし、見つからなかったフ
    // ァイル名を保持する。 モデル上の型を保ちつつ、ネイティブの例外処理には
    // std::invalid_argumentを通じて同じ分類を伝える。
    explicit NoFontError(::cw::lib::String message = {})
        : std::invalid_argument(std::move(message)) {}
    virtual ~NoFontError() = default;
};

class RecentHistory : public ::cw::lib::Object {
public:
    RecentHistory() = default;
    virtual ~RecentHistory() = default;
    std::shared_ptr<::cw::data::CWPyElementTree> m_data{};
    ::cw::lib::Value m_limit{};
    std::vector<::cw::lib::Value> m_scelist{};
    ::cw::lib::Value m_ydata{};
    // Python版のscelistは (path, md5, temppath, name) の順序を持つ
    // タプル列である。Valueのスカラー制約により、C++側では型付き列を
    // 正本として保持し、m_scelistは生成コード互換のミラーとして残す。
    std::vector<std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String>> m_scelist_typed{};
    ::cw::lib::String m_tempdir{};
    std::shared_ptr<::cw::data::YadoData> m_ydata_typed{};
    ::cw::lib::String m_fpath{};
    void initialize(::cw::lib::String arg_tempdir, std::shared_ptr<::cw::data::YadoData> arg_ydata);
    void update_scenariopath(::cw::lib::String arg_from_normpath, ::cw::lib::String arg_to_path);
    void write();
    void set_limit(::cw::lib::Integer arg_value);
    bool _remove_old(std::optional<::cw::lib::String> arg_exclude);
    void _sort_scelist(std::vector<std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String>>& arg_scelist, std::optional<::cw::lib::String> arg_exclude);
    void moveend(::cw::lib::String arg_path);
    void append(::cw::lib::String arg_name, ::cw::lib::String arg_path, ::cw::lib::String arg_temppath, std::optional<::cw::lib::String> arg_md5);
    void remove(::cw::lib::String arg_path, bool arg_save, std::optional<std::reference_wrapper<std::vector<std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String>>>> arg_scelist);
    std::optional<::cw::lib::String> check(::cw::lib::String arg_path, std::optional<::cw::lib::String> arg_md5);
};

class Resource : public ::cw::lib::Object {
public:
    Resource() = default;
    virtual ~Resource() = default;
    inline static ::cw::lib::Value s_buttons{};
    inline static ::cw::lib::Value s_cardbgs{};
    inline static ::cw::lib::Value s_cardnamecolorhints{};
    inline static ::cw::lib::Value s_cursors{};
    inline static ::cw::lib::Value s_debugs{};
    inline static ::cw::lib::Value s_dialogs{};
    inline static ::cw::lib::Integer s_ext_img{};
    inline static std::map<::cw::lib::String, ::cw::lib::String> s_facenames_init{};
    inline static std::set<::cw::lib::String> s_facenames_lower{};
    inline static std::map<::cw::lib::String, ::cw::lib::String> s_fontnames_init{};
    inline static ::cw::lib::Value s_fonts{};
    inline static ::cw::lib::Value s_msg_exfonts{};
    inline static ::cw::lib::Value s_pygamedebugs{};
    inline static ::cw::lib::Value s_pygamedialogs{};
    inline static ::cw::lib::Value s_specialchars{};
    inline static bool s_specialchars_is_changed{};
    inline static ::cw::lib::Value s_statuses{};
    inline static ::cw::lib::Value s_stones{};
    inline static ::cw::lib::Value s_wxcardbgs{};
    inline static ::cw::lib::Value s_wxstones{};
    bool m__init{};
    std::map<::cw::lib::Integer, std::shared_ptr<::cw::lib::Surface>> m__statusbtnbmp0{};
    std::map<::cw::lib::Integer, std::shared_ptr<::cw::lib::Surface>> m__statusbtnbmp1{};
    std::map<::cw::lib::Integer, std::shared_ptr<::cw::lib::Surface>> m__statusbtnbmp2{};
    std::map<::cw::lib::Integer, std::shared_ptr<::cw::header::CardHeader>> m_actioncards{};
    std::map<::cw::lib::String, std::shared_ptr<::cw::header::CardHeader>> m_backpackcards{};
    ::cw::lib::Value m_buttons{};
    ::cw::lib::Value m_cardbgs{};
    ::cw::lib::Value m_cardnamecolorhints{};
    ::cw::lib::Value m_cursors{};
    ::cw::lib::Value m_debugs{};
    ::cw::lib::Value m_debugs_noscale{};
    ::cw::lib::Value m_debugs_wx{};
    ::cw::lib::Value m_dialogs{};
    ::cw::lib::Integer m_ext_bgm{};
    ::cw::lib::Integer m_ext_img{};
    ::cw::lib::Integer m_ext_snd{};
    ::cw::lib::ValueSet m_facenames{};
    ::cw::lib::ValueSet m_facenames_lower{};
    ::cw::lib::Value m_fontnames{};
    ::cw::lib::Value m_fontnames_init{};
    // 型付きの保持先でPythonの辞書全体を維持する。 上のValue
    // メンバーは生成済みの呼び出し元とのABI互換用に残す。
    std::map<::cw::lib::String, ::cw::lib::String> m_fontnames_typed{};
    std::map<::cw::lib::String, ::cw::lib::String> m_fontnames_init_typed{};
    ::cw::lib::Value m_fontpaths{};
    std::map<::cw::lib::String, ::cw::lib::String> m_fontpaths_typed{};
    ::cw::lib::Value m_fonts{};
    ::cw::lib::ValueMap m_ignorecase_table{};
    ::cw::lib::Value m_msg_exfonts{};
    ::cw::lib::Value m_msgs{};
    ::cw::lib::Value m_pygamedebugs{};
    ::cw::lib::Value m_pygamedialogs{};
    ::cw::lib::Value m_setting{};
    ::cw::lib::String m_skindir{};
    ::cw::lib::Value m_skinsounds{};
    ::cw::lib::Value m_sounds{};
    ::cw::lib::Value m_specialchars{};
    bool m_specialchars_is_changed{};
    ::cw::lib::Value m_statuses{};
    ::cw::lib::Value m_stones{};
    ::cw::lib::Value m_wxcardbgs{};
    ::cw::lib::Value m_wxstatuses{};
    ::cw::lib::Value m_wxstones{};
    void initialize(std::optional<std::shared_ptr<::cw::setting::Setting>> arg_setting);
    bool to_bool();
    void init(std::shared_ptr<::cw::setting::Setting> arg_setting);
    ::cw::lib::String get_filepath(::cw::lib::String arg_fpath);
    void dispose();
    ::cw::lib::Integer get_cardnamecolorborder();
    void update_winscale();
    void init_sounds();
    void init_wxresources();
    void init_debugresources(bool arg_game_images = true);
    void init_debugicon();
    std::map<::cw::lib::String, ::cw::lib::String> get_fontpaths();
    static std::map<::cw::lib::String, ::cw::lib::String> get_fontpaths_s(::cw::lib::String arg_fontdir, std::set<::cw::lib::String> arg_facenames);
    static void install_defaultfonts(std::map<::cw::lib::String, ::cw::lib::String> arg_fontpaths, std::set<::cw::lib::String> arg_facenames, std::map<::cw::lib::String, ::cw::lib::String>& arg_d);
    std::tuple<std::map<::cw::lib::String, ::cw::lib::String>, std::map<::cw::lib::String, ::cw::lib::String>> set_systemfonttable();
    void clear_systemfonttable();
    ResolvedFontRequest get_fontfromtype(::cw::lib::String arg_name, std::optional<FontRequestValue> arg_fontinfo);
    std::shared_ptr<::cw::lib::Window> get_wxfont(::cw::lib::String arg_name, std::optional<::cw::lib::Integer> arg_size, std::optional<::cw::lib::Integer> arg_pixelsize, ::cw::lib::Integer arg_family, ::cw::lib::Integer arg_style, ::cw::lib::Integer arg_weight, ::cw::lib::Integer arg_encoding, bool arg_adjustsize, bool arg_adjustsizewx3, std::optional<::cw::lib::Integer> arg_pointsize);
    std::shared_ptr<::cw::imageretouch::Font> create_font(::cw::lib::String arg_ftype, ::cw::lib::String arg_basetype, ::cw::lib::String arg_fontname, ::cw::lib::Integer arg_size_noscale, bool arg_defbold, bool arg_defbold_upscr, bool arg_defitalic, ::cw::lib::Integer arg_pixelsadd, bool arg_nobold, std::optional<FontRequestValue> arg_fontinfo);
    std::shared_ptr<::cw::imageretouch::Font> create_exfont(::cw::lib::String arg_ftype, std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::Integer, bool, bool, bool> arg_fontinfo, std::map<::cw::lib::String, std::shared_ptr<::cw::imageretouch::Font>> arg_basetable, bool arg_nobold);
#if defined(_WIN32)
    // 混在フォントの資源を取得できなければ本文用へ戻す。 フォント自
    // 体がなければ、メッセージ描画側で既存の資源外の代替処理を使う。
    std::shared_ptr<::cw::imageretouch::Font> find_message_font(const ::cw::lib::String& category);
#endif
    std::tuple<::cw::lib::Value, ::cw::lib::Value> create_fonts();
    ::cw::lib::Value create_wxbutton(std::shared_ptr<::cw::lib::Panel> arg_parent, ::cw::lib::Integer arg_cid, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_size, std::optional<::cw::lib::String> arg_name, std::optional<std::shared_ptr<::cw::lib::Bitmap>> arg_bmp, bool arg_chain);
    ::cw::lib::Value create_wxbutton_dbg(std::shared_ptr<::cw::lib::Panel> arg_parent, ::cw::lib::Integer arg_cid, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_size, std::optional<::cw::lib::String> arg_name, std::optional<std::shared_ptr<::cw::lib::Bitmap>> arg_bmp);
    static std::tuple<std::shared_ptr<::cw::lib::Surface>, std::shared_ptr<::cw::lib::Surface>, std::shared_ptr<::cw::lib::Surface>, std::shared_ptr<::cw::lib::Surface>> create_cornerimg(std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> arg_rgb);
    static void draw_frame(std::shared_ptr<::cw::lib::Surface> arg_bmp, std::shared_ptr<::cw::lib::Object> arg_rect, std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> arg_color);
    static void draw_corneroutimg(std::shared_ptr<::cw::lib::Surface> arg_bmp, std::optional<std::shared_ptr<::cw::lib::Object>> arg_rect, ::cw::lib::Integer arg_outframe);
    std::shared_ptr<::cw::lib::Surface> _create_statusbtnbmp(::cw::lib::Integer arg_w, ::cw::lib::Integer arg_h, ::cw::lib::Integer arg_flags);
    std::optional<std::shared_ptr<::cw::lib::Surface>> get_statusbtnbmp(::cw::lib::Integer arg_sizetype, ::cw::lib::Integer arg_flags);
    ::cw::lib::Value get_resources(::cw::lib::Callback arg_func, ::cw::lib::String arg_dpath1, ::cw::lib::String arg_dpath2, ::cw::lib::Integer arg_ext, std::optional<bool> arg_mask, std::optional<::cw::lib::Callback> arg_ss, std::vector<::cw::lib::String> arg_noresize, bool arg_nodbg, std::optional<::cw::lib::Callback> arg_emptyfunc, std::optional<::cw::lib::String> arg_editor_res, bool arg_warning, bool arg_can_loaded_scaledimage);
    ::cw::lib::Value get_sounds(std::shared_ptr<::cw::setting::Setting> arg_setting, ::cw::lib::Value arg_skinsounds);
    ::cw::lib::Value get_skinsounds();
    std::shared_ptr<::cw::setting::MsgDict> get_msgs(std::shared_ptr<::cw::setting::Setting> arg_setting);
    ::cw::lib::Value get_buttons();
    ::cw::lib::Value get_cursors();
    ::cw::lib::Value get_stones();
    ::cw::lib::Value get_wxstones();
    ::cw::lib::Value get_statuses(::cw::lib::Callback arg_load_image, bool arg_for_wx = false);
    ::cw::lib::Value get_dialogs(::cw::lib::Callback arg_load_image, ::cw::lib::Callback arg_emptyfunc, bool arg_for_wx = false);
    ::cw::lib::Value get_debugs(::cw::lib::Callback arg_load_image, ::cw::lib::Callback arg_emptyfunc, ::cw::lib::Callback arg_ss, bool arg_can_loaded_scaledimage);
    ::cw::lib::Value get_cardbgs(::cw::lib::Callback arg_load_image, ::cw::lib::Callback arg_emptyfunc,
                                 bool arg_for_wx = false);
    ::cw::lib::Value get_cardnamecolorhints(::cw::lib::Value arg_cardbgs);
    ::cw::lib::Integer calc_cardnamecolorhint(std::shared_ptr<::cw::lib::Surface> arg_bmp);
    ::cw::lib::Integer calc_wxcardnamecolorhint(std::shared_ptr<::cw::lib::Bitmap> arg_wxbmp);
    std::map<::cw::lib::Integer, std::shared_ptr<::cw::header::CardHeader>> get_actioncards();
    std::map<::cw::lib::String, std::shared_ptr<::cw::header::CardHeader>> get_backpackcards();
    ::cw::lib::Value get_specialchars();
};

class ResourceTable : public ::cw::lib::Object {
public:
    ResourceTable() = default;
    virtual ~ResourceTable() = default;
    ::cw::lib::Value m_deffunc{};
    bool m_defload{};
    std::optional<::cw::lib::Value> m_defvalue{};
    ::cw::lib::Value m_dic{};
    ::cw::lib::Value m_name{};
    ::cw::lib::Value m_nokeyfunc{};
    // Pythonのdict[str, LazyResource]を型付きで保持する本体。
    std::map<::cw::lib::String, std::shared_ptr<::cw::setting::LazyResource>> m_entries{};
    ::cw::lib::String m_name_typed{};
    ::cw::lib::Callback m_deffunc_typed{};
    ::cw::lib::Callback m_nokeyfunc_typed{};
    void initialize(::cw::lib::String arg_name, std::optional<::cw::lib::ValueMap> arg_init = std::nullopt, std::optional<::cw::lib::Callback> arg_deffunc = std::nullopt, std::optional<::cw::lib::Callback> arg_nokeyfunc = std::nullopt);
    void reset();
    void merge(::cw::lib::Value arg_d);
    ::cw::lib::Value get_item(::cw::lib::Value arg_key);
    ::cw::lib::Value get_defvalue();
    void _put_nokeyvalue(::cw::lib::Value arg_key);
    std::optional<::cw::lib::Value> get(::cw::lib::Value arg_key, std::optional<::cw::lib::Value> arg_defvalue = std::nullopt);
    void set(::cw::lib::Value arg_key, ::cw::lib::Callback arg_func, const ::cw::lib::Arguments& arg_args = {}, const ::cw::lib::KeywordArguments& arg_kwargs = {});
    void remove(::cw::lib::Value arg_key);
    bool contains(::cw::lib::Value arg_key);
    ::cw::lib::Value copy();
    std::vector<::cw::lib::Value> iterkeys();
    bool is_loaded(::cw::lib::Value arg_key);
    ::cw::lib::Value keys();
};

class ScenarioCompatibilityTable : public ::cw::lib::Object {
public:
    ScenarioCompatibilityTable() = default;
    virtual ~ScenarioCompatibilityTable() = default;
    std::map<::cw::lib::String, std::tuple<::cw::lib::String, ::cw::lib::String, bool, bool, bool>> m_table{};
    void initialize();
    void init();
    std::optional<std::tuple<::cw::lib::String, ::cw::lib::String, bool, bool, bool>> get_versionhint(std::optional<::cw::lib::String> arg_fpath = std::nullopt, std::optional<::cw::lib::Bytes> arg_filedata = std::nullopt);
    bool lessthan(::cw::lib::String arg_versionhint, std::optional<std::tuple<::cw::lib::String, ::cw::lib::String, bool, bool, bool>> arg_currentversion);
    bool zindexmode(std::optional<std::tuple<::cw::lib::String, ::cw::lib::String, bool, bool, bool>> arg_currentversion);
    bool enable_vanishmembercancellation(std::optional<std::tuple<::cw::lib::String, ::cw::lib::String, bool, bool, bool>> arg_currentversion);
    bool disable_gossiprestration(std::optional<std::tuple<::cw::lib::String, ::cw::lib::String, bool, bool, bool>> arg_currentversion);
    bool disable_compstamprestration(std::optional<std::tuple<::cw::lib::String, ::cw::lib::String, bool, bool, bool>> arg_currentversion);
    std::optional<std::tuple<::cw::lib::String, ::cw::lib::String, bool, bool, bool>> merge_versionhints(std::optional<std::tuple<::cw::lib::String, ::cw::lib::String, bool, bool, bool>> arg_hint1, std::optional<std::tuple<::cw::lib::String, ::cw::lib::String, bool, bool, bool>> arg_hint2);
    std::tuple<::cw::lib::String, ::cw::lib::String, bool, bool, bool> from_basehint(::cw::lib::String arg_basehint);
    ::cw::lib::String to_basehint(std::optional<std::tuple<::cw::lib::String, ::cw::lib::String, bool, bool, bool>> arg_versionhint);
    std::optional<std::tuple<::cw::lib::String, ::cw::lib::String, bool, bool, bool>> read_modeini(::cw::lib::String arg_fpath);
};

class Setting : public ::cw::lib::Object {
public:
    Setting() = default;
    virtual ~Setting() = default;
    inline static bool s__classicstyletext{};
    inline static ::cw::lib::Integer s_initialcash{};
    inline static std::vector<::cw::lib::String> s_makingcoupons{};
    inline static std::vector<::cw::lib::String> s_makingnames{};
    inline static std::vector<std::shared_ptr<::cw::features::Making>> s_makings{};
    inline static std::shared_ptr<::cw::setting::MsgDict> s_msgs{};
    inline static std::vector<::cw::lib::String> s_naturecoupons{};
    inline static std::vector<::cw::lib::String> s_naturenames{};
    inline static std::vector<std::shared_ptr<::cw::features::Nature>> s_natures{};
    inline static std::vector<::cw::lib::String> s_periodcoupons{};
    inline static std::vector<::cw::lib::String> s_periodnames{};
    inline static std::vector<std::shared_ptr<::cw::features::Period>> s_periods{};
    inline static std::vector<std::shared_ptr<::cw::header::RaceHeader>> s_races{};
    inline static std::vector<std::shared_ptr<::cw::features::SampleType>> s_sampletypes{};
    inline static std::vector<::cw::lib::String> s_sexcoupons{};
    inline static std::vector<std::shared_ptr<::cw::features::Sex>> s_sexes{};
    inline static std::vector<::cw::lib::String> s_sexnames{};
    inline static std::vector<::cw::lib::String> s_sexsubnames{};
    inline static ::cw::lib::String s_skinname{};
    inline static std::shared_ptr<::cw::setting::SystemCoupons> s_skinsyscoupons{};
    inline static ::cw::lib::String s_skintype{};
    inline static std::shared_ptr<::cw::header::UnknownRaceHeader> s_unknown_race{};
    bool m__classicstyletext{};
    bool m_all_quickdeal{};
    ::cw::lib::Value m_all_quickdeal_init{};
    bool m_auto_update_files{};
    ::cw::lib::Value m_auto_update_files_init{};
    bool m_autoenter_on_sprite{};
    ::cw::lib::Value m_autoenter_on_sprite_init{};
    bool m_autosave_partyrecord{};
    ::cw::lib::Value m_autosave_partyrecord_init{};
    ::cw::lib::Integer m_backlogmax{};
    ::cw::lib::Value m_backlogmax_init{};
    bool m_bassmidi_sample32bit{};
    ::cw::lib::Value m_bassmidi_sample32bit_init{};
    bool m_blink_partymoney{};
    ::cw::lib::Value m_blink_partymoney_init{};
    bool m_blink_statusbutton{};
    ::cw::lib::Value m_blink_statusbutton_init{};
    std::vector<std::tuple<::cw::lib::String, ::cw::lib::String>> m_bookmarks_for_cardedit{};
    ::cw::lib::Value m_bookmarks_for_cardedit_init{};
    bool m_can_clicksidesofcardcontrol{};
    ::cw::lib::Value m_can_clicksidesofcardcontrol_init{};
    bool m_can_forwardmessage_with_wheel{};
    ::cw::lib::Value m_can_forwardmessage_with_wheel_init{};
    bool m_can_installscenariofromdrop{};
    ::cw::lib::Value m_can_installscenariofromdrop_init{};
    bool m_can_repeatlclick{};
    ::cw::lib::Value m_can_repeatlclick_init{};
    bool m_can_skipanimation{};
    ::cw::lib::Value m_can_skipanimation_init{};
    bool m_can_skipwait{};
    ::cw::lib::Value m_can_skipwait_init{};
    bool m_can_skipwait_with_wheel{};
    ::cw::lib::Value m_can_skipwait_with_wheel_init{};
    ::cw::lib::String m_card_narrow{};
    ::cw::lib::Value m_card_narrow_init{};
    ::cw::lib::Integer m_card_narrowtype{};
    ::cw::lib::Value m_card_narrowtype_init{};
    ::cw::lib::String m_cardssfnameformat{};
    ::cw::lib::Value m_cardssfnameformat_init{};
    bool m_caution_beforesaving{};
    ::cw::lib::Value m_caution_beforesaving_init{};
    ::cw::lib::String m_confirm_beforesaving{};
    ::cw::lib::Value m_confirm_beforesaving_init{};
    bool m_confirm_beforeusingcard{};
    ::cw::lib::Value m_confirm_beforeusingcard_init{};
    ::cw::lib::String m_confirm_dumpcard{};
    ::cw::lib::Value m_confirm_dumpcard_init{};
    ::cw::lib::String m_cursor_type{};
    ::cw::lib::Value m_cursor_type_init{};
    ::cw::lib::Value m_data{};
    ::cw::lib::Value m_dealing_scales{};
    ::cw::lib::Value m_dealing_scales_battle{};
    ::cw::lib::Integer m_dealspeed{};
    ::cw::lib::Integer m_dealspeed_battle{};
    ::cw::lib::Value m_dealspeed_battle_init{};
    ::cw::lib::Value m_dealspeed_init{};
    bool m_debug{};
    ::cw::lib::Value m_debug_init{};
    bool m_debug_saved{};
    ::cw::lib::Value m_debug_saved_init{};
    bool m_delete_sourceafterinstalled{};
    ::cw::lib::Value m_delete_sourceafterinstalled_init{};
    bool m_display_bill_in_messagelog{};
    ::cw::lib::Value m_display_bill_in_messagelog_init{};
    bool m_edit_star{};
    ::cw::lib::Value m_edit_star_init{};
    ::cw::lib::String m_editor{};
    ::cw::lib::Value m_editor_init{};
    bool m_enabled_right_flick{};
    ::cw::lib::Value m_enabled_right_flick_init{};
    bool m_enabled_timekeeper{};
    ::cw::lib::Value m_enabled_timekeeper_init{};
    bool m_enlarge_beastcardzoomingratio{};
    ::cw::lib::Value m_enlarge_beastcardzoomingratio_init{};
    ::cw::lib::Real m_expanddrawing{};
    ::cw::lib::Value m_expanddrawing_init{};
    ::cw::lib::String m_expandmode{};
    ::cw::lib::Value m_expandmode_init{};
    bool m_face_variants{};
    ::cw::lib::String m_filer_dir{};
    ::cw::lib::Value m_filer_dir_init{};
    ::cw::lib::String m_filer_file{};
    ::cw::lib::Value m_filer_file_init{};
    ::cw::lib::Integer m_flick_distance{};
    ::cw::lib::Value m_flick_distance_init{};
    ::cw::lib::Integer m_flick_time_msec{};
    ::cw::lib::Value m_flick_time_msec_init{};
    std::vector<std::tuple<::cw::lib::String, ::cw::lib::String>> m_folderoftype{};
    ::cw::lib::Value m_folderoftype_init{};
    ::cw::lib::String m_fontexampleformat{};
    ::cw::lib::Value m_fontexampleformat_init{};
    ::cw::lib::Integer m_fontexamplepixelsize{};
    ::cw::lib::Value m_fontexamplepixelsize_init{};
    ::cw::lib::Integer m_fps{};
    ::cw::lib::Integer m_frametime{};
    ::cw::lib::Integer m_infoview_narrowtype{};
    ::cw::lib::Value m_infoview_narrowtype_init{};
    ::cw::lib::Integer m_initialcash{};
    ::cw::lib::Integer m_initmoneyamount{};
    ::cw::lib::Value m_initmoneyamount_init{};
    bool m_initmoneyisinitialcash{};
    ::cw::lib::Value m_initmoneyisinitialcash_init{};
    bool m_install_notscenariofiles{};
    ::cw::lib::Value m_install_notscenariofiles_init{};
    std::map<::cw::lib::String, std::vector<::cw::lib::String>> m_installed_dir{};
    ::cw::lib::Value m_installed_dir_init{};
    bool m_is_expanded{};
    ::cw::lib::Value m_is_expanded_init{};
    ::cw::lib::Integer m_last_backpackpage{};
    ::cw::lib::Value m_last_backpackpage_init{};
    ::cw::lib::Integer m_last_cardpocket{};
    ::cw::lib::Value m_last_cardpocket_init{};
    ::cw::lib::Value m_last_cardpocketbpage{};
    ::cw::lib::Value m_last_cardpocketbpage_init{};
    ::cw::lib::Integer m_last_sendto{};
    ::cw::lib::Value m_last_sendto_init{};
    ::cw::lib::Integer m_last_storehousepage{};
    ::cw::lib::Value m_last_storehousepage_init{};
    std::vector<::cw::lib::String> m_lastfindresult{};
    ::cw::lib::Value m_lastfindresult_init{};
    std::vector<::cw::lib::String> m_lastscenario{};
    ::cw::lib::Value m_lastscenario_init{};
    ::cw::lib::String m_lastscenariopath{};
    ::cw::lib::Value m_lastscenariopath_init{};
    ::cw::lib::String m_lastyado{};
    ::cw::lib::Value m_lastyado_init{};
    bool m_level_adjustment_affect_personal_pocket{};
    ::cw::lib::Value m_level_adjustment_affect_personal_pocket_init{};
    std::shared_ptr<::cw::setting::LocalSetting> m_local{};
    ::cw::lib::Value m_makingcoupons{};
    ::cw::lib::Value m_makingnames{};
    ::cw::lib::Value m_makings{};
    ::cw::lib::String m_messagelog_type{};
    ::cw::lib::Value m_messagelog_type_init{};
    ::cw::lib::Integer m_messagespeed{};
    ::cw::lib::Value m_messagespeed_init{};
    ::cw::lib::Integer m_move_repeat{};
    ::cw::lib::Value m_move_repeat_init{};
    std::shared_ptr<::cw::setting::MsgDict> m_msgs{};
    ::cw::lib::Value m_naturecoupons{};
    ::cw::lib::Value m_naturenames{};
    ::cw::lib::Value m_natures{};
    bool m_no_levelup_in_debugmode{};
    ::cw::lib::Value m_no_levelup_in_debugmode_init{};
    bool m_noticeimpossibleaction{};
    ::cw::lib::Value m_noticeimpossibleaction_init{};
    bool m_open_lastfindresult{};
    ::cw::lib::Value m_open_lastfindresult_init{};
    bool m_open_lastscenario{};
    ::cw::lib::Value m_open_lastscenario_init{};
    bool m_overwrite_partyrecord{};
    ::cw::lib::Value m_overwrite_partyrecord_init{};
    ::cw::lib::Integer m_parties_narrowtype{};
    ::cw::lib::Value m_parties_narrowtype_init{};
    ::cw::lib::Value m_periodcoupons{};
    ::cw::lib::Value m_periodnames{};
    ::cw::lib::Value m_periods{};
    ::cw::lib::Value m_periodsubnames{};
    bool m_play_bgm{};
    ::cw::lib::Value m_play_bgm_init{};
    bool m_play_sound{};
    ::cw::lib::Value m_play_sound_init{};
    ::cw::lib::String m_playlogformat{};
    ::cw::lib::Value m_playlogformat_init{};
    bool m_protect_premiercard{};
    ::cw::lib::Value m_protect_premiercard_init{};
    bool m_protect_staredcard{};
    ::cw::lib::Value m_protect_staredcard_init{};
    bool m_quickdeal{};
    ::cw::lib::Value m_quickdeal_init{};
    ::cw::lib::Value m_races{};
    ::cw::lib::Integer m_radius_notdetectmovement{};
    ::cw::lib::Value m_radius_notdetectmovement_init{};
    ::cw::lib::Integer m_recenthistory_limit{};
    ::cw::lib::Value m_recenthistory_limit_init{};
    bool m_replacecard_when_sendfullcardpocket{};
    ::cw::lib::Value m_replacecard_when_sendfullcardpocket_init{};
    bool m_revert_cardpocket{};
    ::cw::lib::Value m_revert_cardpocket_init{};
    ::cw::lib::Value m_sampletypes{};
    ::cw::lib::String m_scenario_narrow{};
    ::cw::lib::Value m_scenario_narrow_init{};
    ::cw::lib::Integer m_scenario_narrowtype{};
    ::cw::lib::Value m_scenario_narrowtype_init{};
    ::cw::lib::Integer m_scenario_sorttype{};
    ::cw::lib::Value m_scenario_sorttype_init{};
    bool m_sdlmixer_enabled{};
    ::cw::lib::Value m_sdlmixer_enabled_init{};
    bool m_selectscenariofromtype{};
    ::cw::lib::Value m_selectscenariofromtype_init{};
    ::cw::lib::Value m_sexcoupons{};
    ::cw::lib::Value m_sexes{};
    ::cw::lib::Value m_sexnames{};
    ::cw::lib::Value m_sexsubnames{};
    bool m_shiftup_touchbutton{};
    ::cw::lib::Value m_shiftup_touchbutton_init{};
    bool m_show_addctrlbtn{};
    ::cw::lib::Value m_show_addctrlbtn_init{};
    bool m_show_additional_card{};
    ::cw::lib::Value m_show_additional_card_init{};
    bool m_show_additional_party{};
    ::cw::lib::Value m_show_additional_party_init{};
    bool m_show_additional_player{};
    ::cw::lib::Value m_show_additional_player_init{};
    bool m_show_additional_scenario{};
    ::cw::lib::Value m_show_additional_scenario_init{};
    bool m_show_additional_yado{};
    ::cw::lib::Value m_show_additional_yado_init{};
    bool m_show_advancedsettings{};
    ::cw::lib::Value m_show_advancedsettings_init{};
    bool m_show_aim{};
    ::cw::lib::Value m_show_aim_init{};
    bool m_show_allselectedcards{};
    ::cw::lib::Value m_show_allselectedcards_init{};
    bool m_show_autobuttoninentrydialog{};
    ::cw::lib::Value m_show_autobuttoninentrydialog_init{};
    bool m_show_backpackcard{};
    ::cw::lib::Value m_show_backpackcard_init{};
    bool m_show_backpackcardatend{};
    ::cw::lib::Value m_show_backpackcardatend_init{};
    bool m_show_btndesc{};
    ::cw::lib::Value m_show_btndesc_init{};
    bool m_show_cardkind{};
    ::cw::lib::Value m_show_cardkind_init{};
    ::cw::lib::Value m_show_cardtype{};
    ::cw::lib::Value m_show_cardtype_init{};
    bool m_show_completedscenario{};
    ::cw::lib::Value m_show_completedscenario_init{};
    bool m_show_debuglogdialog{};
    ::cw::lib::Value m_show_debuglogdialog_init{};
    bool m_show_experiencebar{};
    ::cw::lib::Value m_show_experiencebar_init{};
    bool m_show_fcardsinbattle{};
    ::cw::lib::Value m_show_fcardsinbattle_init{};
    bool m_show_invisiblescenario{};
    ::cw::lib::Value m_show_invisiblescenario_init{};
    bool m_show_lifebar_on_selection{};
    ::cw::lib::Value m_show_lifebar_on_selection_init{};
    bool m_show_multiplebases{};
    ::cw::lib::Value m_show_multiplebases_init{};
    bool m_show_multipleparties{};
    ::cw::lib::Value m_show_multipleparties_init{};
    bool m_show_multipleplayers{};
    ::cw::lib::Value m_show_multipleplayers_init{};
    bool m_show_paperandtree{};
    ::cw::lib::Value m_show_paperandtree_init{};
    bool m_show_personal_cards{};
    ::cw::lib::Value m_show_personal_cards_init{};
    bool m_show_premiumicon{};
    ::cw::lib::Value m_show_premiumicon_init{};
    bool m_show_roundautostartbutton{};
    ::cw::lib::Value m_show_roundautostartbutton_init{};
    // CardWirthPy_C_All専用。幅の広いホストの
    // 余った側面領域に、共通ステータスバーの操作を表示する。
    bool m_show_all_status_dialog{};
    ::cw::lib::Value m_show_all_status_dialog_init{};
    // CardWirthPy_C_All専用。右側ショートカット（AllStatusShortcut
    // ）と、 切り替えボタンだけに折り畳まれているかを保持する。
    ::cw::lib::String m_all_status_shortcuts{kAllStatusShortcutsDefault};
    ::cw::lib::Value m_all_status_shortcuts_init{};
    bool m_all_status_shortcuts_folded{};
    ::cw::lib::Value m_all_status_shortcuts_folded_init{};
    // CardWirthPy_C_All専用のカスタム状態ショートカットへ登録した状態。 形式はcw::debug::statusedit::format_custom_status
    // に従い、空なら既定状態。
    ::cw::lib::String m_all_custom_status{};
    ::cw::lib::Value m_all_custom_status_init{};
    bool m_show_savedmessage{};
    bool m_skip_pc_click_motion{true};
    ::cw::lib::Value m_skip_pc_click_motion_init{true};
    ::cw::lib::Value m_show_savedmessage_init{};
    bool m_show_scenariotree{};
    ::cw::lib::Value m_show_scenariotree_init{};
    bool m_show_sell_with_premiercard{};
    ::cw::lib::Value m_show_sell_with_premiercard_init{};
    ::cw::lib::String m_show_statustime{};
    ::cw::lib::Value m_show_statustime_init{};
    bool m_show_tiles{};
    ::cw::lib::Value m_show_tiles_init{};
    bool m_show_unfitnessscenario{};
    ::cw::lib::Value m_show_unfitnessscenario_init{};
    bool m_showfps{};
    ::cw::lib::Value m_showfps_init{};
    std::shared_ptr<::cw::setting::LocalSetting> m_skin_local{};
    ::cw::lib::Value m_skindata{};
    ::cw::lib::String m_skindir{};
    ::cw::lib::String m_skindirname{};
    ::cw::lib::Value m_skindirname_init{};
    ::cw::lib::String m_skinname{};
    std::shared_ptr<::cw::setting::SystemCoupons> m_skinsyscoupons{};
    ::cw::lib::String m_skintype{};
    bool m_smoothexpand{};
    ::cw::lib::Value m_smoothexpand_init{};
    bool m_smoothing_card_down{};
    ::cw::lib::Value m_smoothing_card_down_init{};
    bool m_smoothing_card_up{};
    ::cw::lib::Value m_smoothing_card_up_init{};
    bool m_smoothscale_bg{};
    ::cw::lib::Value m_smoothscale_bg_init{};
    ::cw::lib::String m_sort_cards{};
    ::cw::lib::Value m_sort_cards_init{};
    bool m_sort_cardswithstar{};
    ::cw::lib::Value m_sort_cardswithstar_init{};
    ::cw::lib::String m_sort_parties{};
    ::cw::lib::Value m_sort_parties_init{};
    ::cw::lib::String m_sort_standbys{};
    ::cw::lib::Value m_sort_standbys_init{};
    ::cw::lib::String m_sort_yado{};
    ::cw::lib::Value m_sort_yado_init{};
    ::cw::lib::ValueMap m_sound_table{};
    ::cw::lib::Value m_soundfonts{};
    ::cw::lib::Value m_soundfonts_init{};
    // 正式な値は型付きのSoundFonts/SoundFont行へ保持する。
    std::vector<::cw::setting::SoundFontValue> m_soundfonts_typed{};
    std::vector<::cw::setting::SoundFontValue> m_soundfonts_init_typed{};
    bool m_spend_noeffectcard{};
    ::cw::lib::Value m_spend_noeffectcard_init{};
    ::cw::lib::String m_ssfnameformat{};
    ::cw::lib::Value m_ssfnameformat_init{};
    ::cw::lib::Value m_ssinfobackcolor{};
    ::cw::lib::Value m_ssinfobackcolor_init{};
    ::cw::lib::String m_ssinfobackimage{};
    ::cw::lib::Value m_ssinfobackimage_init{};
    ::cw::lib::Value m_ssinfofontcolor{};
    ::cw::lib::Value m_ssinfofontcolor_init{};
    ::cw::lib::String m_ssinfoformat{};
    ::cw::lib::Value m_ssinfoformat_init{};
    bool m_sswithstatusbar{};
    ::cw::lib::Value m_sswithstatusbar_init{};
    // Python版にはないCardWirthPy_Cの設定。trueな
    // らスクリーンショットにはゲーム領域だけを含め、 上のタイトル・情
    // 報欄と下のステータスバーを除く。falseなら従来の動作とする。
    bool m_ss_gamearea_only{true};
    ::cw::lib::Value m_ss_gamearea_only_init{};
    ::cw::lib::Integer m_standbys_narrowtype{};
    ::cw::lib::Value m_standbys_narrowtype_init{};
    ::cw::lib::String m_startupscene{};
    ::cw::lib::Value m_startupscene_init{};
    bool m_statusbarmask{};
    ::cw::lib::Value m_statusbarmask_init{};
    bool m_stop_the_world_with_iconized{};
    ::cw::lib::Value m_stop_the_world_with_iconized_init{};
    ::cw::lib::String m_titleformat{};
    ::cw::lib::Value m_titleformat_init{};
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> m_trademode_cardholder_color{};
    ::cw::lib::Value m_trademode_cardholder_color_init{};
    ::cw::lib::String m_transition{};
    ::cw::lib::Value m_transition_init{};
    ::cw::lib::Integer m_transitionspeed{};
    ::cw::lib::Value m_transitionspeed_init{};
    ::cw::lib::String m_unconvert_targetfolder{};
    ::cw::lib::Value m_unconvert_targetfolder_init{};
    std::shared_ptr<::cw::header::UnknownRaceHeader> m_unknown_race{};
    bool m_use_battlespeed{};
    ::cw::lib::Value m_use_battlespeed_init{};
    bool m_vocation120{};
    ::cw::lib::Value m_vocation120_init{};
    ::cw::lib::Real m_vol_bgm{};
    ::cw::lib::Value m_vol_bgm_init{};
    ::cw::lib::Real m_vol_bgm_midi{};
    ::cw::lib::Value m_vol_bgm_midi_init{};
    ::cw::lib::Real m_vol_master{};
    ::cw::lib::Value m_vol_master_init{};
    ::cw::lib::Real m_vol_sound{};
    ::cw::lib::Value m_vol_sound_init{};
    ::cw::lib::Real m_vol_sound_midi{};
    ::cw::lib::Value m_vol_sound_midi_init{};
    ::cw::lib::Integer m_volume_increment{};
    ::cw::lib::Value m_volume_increment_init{};
    bool m_wait_after_punctuation_mark{};
    ::cw::lib::Value m_wait_after_punctuation_mark_init{};
    bool m_wait_usecard{};
    ::cw::lib::Value m_wait_usecard_init{};
    ::cw::lib::String m_wheelup_operation{};
    ::cw::lib::Value m_wheelup_operation_init{};
    std::tuple<std::optional<::cw::lib::Integer>, std::optional<::cw::lib::Integer>> m_window_position{};
    ::cw::lib::Value m_window_position_init{};
    bool m_write_playlog{};
    ::cw::lib::Value m_write_playlog_init{};
    ::cw::lib::Integer m_yado_narrowtype{};
    ::cw::lib::Value m_yado_narrowtype_init{};
    std::map<::cw::lib::String, ::cw::lib::Integer> m_yado_order{};
    ::cw::lib::Value m_yado_order_init{};
    bool m_zoomout_friend{};
    ::cw::lib::Value m_zoomout_friend_init{};
    void initialize();
    void init_settings(std::optional<::cw::lib::String> arg_loadfile, bool arg_init);
    void init_skin(std::optional<std::shared_ptr<::cw::data::CWPyElementTree>> arg_basedata);
    std::shared_ptr<::cw::data::CWPyElementTree> _update_skin(::cw::lib::String arg_path);
    ::cw::lib::String _check_skin();
    void set_dealspeed(::cw::lib::Integer arg_value, ::cw::lib::Integer arg_battlevalue, bool arg_usebattle);
    ::cw::lib::Integer get_dealspeed(bool arg_isbattle);
    std::vector<::cw::lib::Integer> create_dealingscales(::cw::lib::Integer arg_dealspeed);
    std::shared_ptr<::cw::setting::LocalSetting> get_drawsetting();
    std::shared_ptr<::cw::setting::LocalSetting> get_fontsetting();
    ::cw::lib::Integer get_inusecardalpha(std::shared_ptr<::cw::sprite::card::CWPyCard> arg_sprite);
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> get_mwincolour();
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> get_mwinframecolour();
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> get_blwincolour();
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> get_blwinframecolour();
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> get_curtaincolour();
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> get_blcurtaincolour();
    ::cw::lib::Integer get_fullscreenbackgroundtype();
    ::cw::lib::String get_fullscreenbackgroundfile();
    bool get_bordering_cardname();
    bool get_decorationfont();
    bool get_fontsmoothing_message();
    bool get_fontsmoothing_cardname();
    bool get_fontsmoothing_statusbar();
    std::map<::cw::lib::String, ::cw::lib::String> get_basefont();
    std::map<::cw::lib::String, std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::Integer, bool, bool, bool>> get_fonttypes();
    std::map<::cw::lib::String, std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::Integer, bool, bool, bool>> get_msg_exfonts();
    bool is_logscrollable();
    void write();
    // SettingsPanelで置き換えた行を、XMLとBASSへ同期する。
    void update_soundfonts();
    static ::cw::lib::Real wrap_volumevalue(::cw::lib::Integer arg_value);
    static std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> wrap_colorvalue(::cw::lib::Integer arg_r, ::cw::lib::Integer arg_g, ::cw::lib::Integer arg_b, ::cw::lib::Integer arg_a);
    ::cw::lib::String get_scedir(std::optional<::cw::lib::String> arg_skintype);
    void insert_yadoorder(::cw::lib::String arg_yadodirname);
};

class SystemCoupons : public ::cw::lib::Object {
public:
    SystemCoupons() = default;
    virtual ~SystemCoupons() = default;
    bool m__ats{};
    std::set<::cw::lib::String> m__normal{};
    std::vector<std::shared_ptr<::cw::lib::python_re::Pattern>> m__regexes{};
    std::set<::cw::lib::String> m__uc_normal{};
    std::vector<std::shared_ptr<::cw::lib::python_re::Pattern>> m__uc_regexes{};
    void initialize(::cw::lib::String arg_fpath = "Data/SystemCoupons.xml", std::optional<std::shared_ptr<::cw::data::CWPyElement>> arg_data = std::nullopt);
    bool unconditional_match(::cw::lib::String arg_coupon);
    bool match(::cw::lib::String arg_coupon);
    bool _match_impl(const ::cw::lib::String& arg_coupon, const std::set<::cw::lib::String>& arg_normal, const std::vector<std::shared_ptr<::cw::lib::python_re::Pattern>>& arg_regexes) const;
};

extern ::cw::lib::String g_CONFIRM_BEFORESAVING_BASE;
extern ::cw::lib::String g_CONFIRM_BEFORESAVING_NO;
extern ::cw::lib::String g_CONFIRM_BEFORESAVING_YES;
extern ::cw::lib::String g_CONFIRM_DUMPCARD_ALWAYS;
extern ::cw::lib::String g_CONFIRM_DUMPCARD_NO;
extern ::cw::lib::String g_CONFIRM_DUMPCARD_SENDTO;
extern ::cw::lib::String g_CURSOR_BLACK;
extern ::cw::lib::String g_CURSOR_WHITE;
extern std::map<::cw::lib::String, ::cw::lib::String> g_CWXEDITOR_RESOURCES;
extern ::cw::lib::String g_DATA_PATH;
extern ::cw::lib::String g_FONT_EXAMPLE_FORMAT_INIT;
extern ::cw::lib::Integer g_FONT_EXAMPLE_PIXEL_SIZE_INIT;
extern ::cw::lib::String g_LOG_COMPRESS;
extern ::cw::lib::String g_LOG_LIST;
extern ::cw::lib::String g_LOG_SINGLE;
extern ::cw::lib::String g_OPEN_LAST_BASE;
extern ::cw::lib::String g_OPEN_TITLE;
extern ::cw::lib::Integer g_SB_CURRENT;
extern ::cw::lib::Integer g_SB_DISABLE;
extern ::cw::lib::Integer g_SB_EMPHASIZE;
extern ::cw::lib::Integer g_SB_NOTICE;
extern ::cw::lib::Integer g_SB_PRESSED;
extern std::map<::cw::lib::String, std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> g_SIZE_RESOURCES;
extern std::tuple<::cw::lib::Integer, ::cw::lib::Integer> g_SIZE_SPFONT;
extern ::cw::lib::String g_WHEEL_SELECTION;
extern ::cw::lib::String g_WHEEL_SHOWLOG;
extern ::cw::lib::Value g__KeyType;
extern ::cw::lib::Value g__ResType;
extern ::cw::lib::String g_data_path;
extern ::cw::lib::Value g_encoding;
extern ::cw::lib::Value g_fcconfig;
extern std::optional<::cw::lib::Value> g_fontconfig;
extern ::cw::lib::String g_path;

void LocalSetting_load_read_font(std::shared_ptr<::cw::data::CWPyElement> arg_e, std::map<::cw::lib::String, std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::Integer, bool, bool, bool>> arg_table, std::map<::cw::lib::String, std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::Integer, bool, bool, bool>> arg_table_init);
void Setting__update_skin_update_mental(std::shared_ptr<::cw::data::CWPyElement> arg_e);
bool Setting__update_skin_check_sampletype(std::shared_ptr<::cw::data::CWPyElement> arg_ste, ::cw::lib::String arg_name, ::cw::lib::Real arg_cautious, ::cw::lib::Real arg_cheerful);
void MsgDict___getitem___func();
::cw::lib::Value Resource_init_wxresources_ss(::cw::lib::Value arg_num);
void Resource_init_debugicon_func();
::cw::lib::Value Resource_init_debugicon_ss(::cw::lib::Value arg_num);
void Resource_install_defaultfonts_func();
void Resource_create_fonts_defvalue();
void Resource_create_wxbutton_starttimer(std::shared_ptr<::cw::lib::Window> arg_event);
void Resource_create_wxbutton_timerfunc(std::shared_ptr<::cw::lib::Window> arg_event);
void Resource_create_wxbutton_stoptimer(std::shared_ptr<::cw::lib::Window> arg_event);
void Resource__create_statusbtnbmp_subtract_corner(::cw::lib::Integer arg_value);
::cw::lib::Value Resource_get_resources_nokeyfunc(::cw::lib::String arg_key);
void Resource_get_resources_nokeyfunc_errfunc(::cw::lib::String arg_dname, ::cw::lib::String arg_key);
void Resource_get_sounds_func(::cw::lib::String arg_sound);
::cw::lib::Value Resource_get_sounds_func_lambda_line_2654();
std::shared_ptr<::cw::lib::Window> Resource_get_cursors_get_cursor(::cw::lib::String arg_name);
::cw::lib::Value Resource_get_cursors_lambda_line_2715();
::cw::lib::Value Resource_get_statuses_load_image2(::cw::lib::String arg_fpath, bool arg_mask, bool arg_can_loaded_scaledimage, std::optional<::cw::lib::Integer> arg_up_scr);
::cw::lib::Value Resource_get_dialogs_load_image2(::cw::lib::String arg_fpath, bool arg_mask, bool arg_can_loaded_scaledimage, std::optional<::cw::lib::Integer> arg_up_scr);
::cw::lib::Value Resource_get_cardbgs_load_image2(::cw::lib::String arg_fpath, bool arg_mask, bool arg_can_loaded_scaledimage, std::optional<::cw::lib::Integer> arg_up_scr);
::cw::lib::Value Resource_get_cardnamecolorhints_lambda_line_2861();
void Resource_get_cardnamecolorhints_func(::cw::lib::String arg_key);
::cw::lib::Value Resource_get_cardnamecolorhints_func_lambda_line_2865();
::cw::lib::Value Resource_get_specialchars_lambda_line_2955();
std::tuple<std::shared_ptr<::cw::lib::Surface>, bool> Resource_get_specialchars_load(::cw::lib::String arg_key, ::cw::lib::String arg_name);
std::tuple<::cw::lib::Integer, ::cw::lib::Integer> get_resourcesize(::cw::lib::String arg_path);
std::shared_ptr<::cw::lib::Bitmap> empty_wxbmp();
std::shared_ptr<::cw::lib::Surface> empty_image();
std::shared_ptr<::cw::util::SoundInterface> empty_sound();
::cw::lib::Value ResourceTable__put_nokeyvalue_func();
::cw::lib::Value RecentHistory__sort_scelist_lambda_line_3528(::cw::lib::Value arg_t);
::cw::lib::Value RecentHistory__sort_scelist_lambda_line_3529(::cw::lib::Value arg_t);
}

