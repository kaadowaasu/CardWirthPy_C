// 設定ダイアログの状態・モデルの移植。設定
// 変更と保存の意味はここで実装し、 wx
// の UI 専用の動作は下の境界で区別
// する。UI の実装は共通の一つに保つ。
#define _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#include "cw/dialog/settings.hpp"
#include "cw/imageretouch.hpp"
#include "cw/dialog/dialog.hpp"
#include "cw/dialog/message.hpp"

#include "cw/cw.hpp"
#include "cw/bassplayer.hpp"
#include "cw/data.hpp"
#include "cw/pc_card_limits.hpp"
#include "cw/header.hpp"
#include "cw/lib/python_integer.hpp"
#include "cw/frame.hpp"
#include "cw/setting.hpp"
#include "cw/lib/settings_xml.hpp"
#include "cw/thread.hpp"
#include "cw/advlog.hpp"
#include "cw/sprite/background.hpp"
#include "cw/sprite/card.hpp"
#include "cw/sprite/statusbar.hpp"
#include "cw/dialog/skininstall.hpp"
#include "cw/dialog/skin.hpp"
#include "cw/dialog/editscenariodb.hpp"
#include "cw/skin/convert.hpp"
#include "cw/lib/encoding.hpp"
#include "cw/lib/ui_surface.hpp"
#include "cw/util.hpp"
#include "cw/xmlcreater.hpp"
#include "cw/debug/statusedit.hpp"
#include "cw/debug/debugger.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#pragma comment(lib, "Gdi32.lib")
#endif

namespace {
using ::cw::lib::Integer;
using ::cw::lib::String;
using ::cw::lib::Value;
using ::cw::lib::Window;
std::optional<::cw::lib::Callback> normalize_settings_callback(
    std::optional<::cw::lib::Callback> callback)
{
    // optionalに入った空のstd::functionも、PythonのNoneと同じ未指定として扱う。
    if (callback && !*callback) callback.reset();
    return callback;
}
// この実装の「適用」は直ちに保存する。遅延した変更はその確定に結び
// 付け、 保存失敗で旧モデルへ戻った後に実行されないようにする。
thread_local std::shared_ptr<std::atomic_bool> g_settings_apply_commit;
struct SettingsApplyCommitScope {
    std::shared_ptr<std::atomic_bool> previous{g_settings_apply_commit};
    std::shared_ptr<std::atomic_bool> commit{std::make_shared<std::atomic_bool>(false)};
    SettingsApplyCommitScope() { g_settings_apply_commit = commit; }
    ~SettingsApplyCommitScope() { g_settings_apply_commit = previous; }
};
bool value_bool(const Value& value, bool fallback = false);
Integer value_integer(const Value& value, Integer fallback = 0);
String value_string(const Value& value, const String& fallback = {});

bool value_bool(const Value& value, bool fallback)
{
    if (const auto* v = std::get_if<bool>(&value.storage())) return *v;
    if (const auto* v = std::get_if<Integer>(&value.storage())) return *v != 0;
    if (const auto* v = std::get_if<::cw::lib::Real>(&value.storage())) return std::isfinite(*v) ? *v != 0.0 : fallback;
    if (const auto* v = std::get_if<String>(&value.storage())) {
        if (*v == "1" || *v == "true" || *v == "True" || *v == "yes" || *v == "Yes") return true;
        if (*v == "0" || *v == "false" || *v == "False" || *v == "no" || *v == "No") return false;
        return fallback;
    }
    return fallback;
}

Integer value_integer(const Value& value, Integer fallback)
{
    if (const auto* v = std::get_if<Integer>(&value.storage())) return *v;
    if (const auto* v = std::get_if<bool>(&value.storage())) return *v ? 1 : 0;
    if (const auto* v = std::get_if<::cw::lib::Real>(&value.storage())) {
        if (std::isfinite(*v) && *v >= static_cast<double>(std::numeric_limits<Integer>::min()) &&
            *v < static_cast<double>(std::numeric_limits<Integer>::max())) return static_cast<Integer>(*v);
        return fallback;
    }
    if (const auto* v = std::get_if<String>(&value.storage()))
        return ::cw::setting::integer_or_initial(*v, fallback,
            std::numeric_limits<Integer>::min(), std::numeric_limits<Integer>::max());
    return fallback;
}

::cw::lib::Real value_real(const Value& value, ::cw::lib::Real fallback = 0.0)
{
    if (const auto* v = std::get_if<::cw::lib::Real>(&value.storage())) return std::isfinite(*v) ? *v : fallback;
    if (const auto* v = std::get_if<Integer>(&value.storage())) return static_cast<::cw::lib::Real>(*v);
    return fallback;
}

String value_string(const Value& value, const String& fallback)
{
    if (const auto* v = std::get_if<String>(&value.storage())) return *v;
    return fallback;
}

Integer status_time_selection(const Value& value)
{
    // Python と同様、この二つの文字列以外は不正な値も含め三番
    // 目の選択肢へ対応付ける。 読み込みとリセットで同じ対応を使う。
    if (const auto* text = std::get_if<String>(&value.storage())) {
        if (*text == "NotEventTime") return 0;
        if (*text == "True") return 1;
    }
    return 2;
}

// 保持型表示の変更検出には、文字列だけでなく全スカラーコントロールの
// 値が必要である。 ここで value_string() を使うと
// 、整数の選択肢・スライダー、真偽値、色のバイト配列が常に同じに見
// え、 モデルが更新されても設定画面に古いピクセルが残ってしまう。
String value_state_string(const Value& value)
{
    if (const auto* bytes = std::get_if<::cw::lib::Bytes>(&value.storage())) {
        String result{"bytes:"};
        for (const auto byte : *bytes) result += std::to_string(byte) + ',';
        return result;
    }
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage()))
        return *object ? "object" : "null";
    return value.is_none() ? "none" : value.as_string();
}

bool value_has(const Value& value)
{
    return !value.is_none();
}

Integer expand_selection_max(Integer maximum)
{
    // Python と同様、小さい画面でも等倍・二倍は含め、それ以上は描画サイズが収まる
    // 2 の累乗だけを追加する。 maximum は整数倍率ではなく十分の一単位である。
    Integer selection=1;
    for(Integer factor=4; factor<=(1LL<<20) && factor*10<=maximum; factor*=2)
        ++selection;
    return selection;
}

std::shared_ptr<Window> value_window(const Value& value)
{
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage())) {
        return std::dynamic_pointer_cast<Window>(*object);
    }
    return {};
}

void set_window_enabled(const std::shared_ptr<Window>& window, bool enabled)
{
    if (window) window->visible = enabled;
}

void set_window_value(const std::shared_ptr<Window>& window, bool value)
{
    if (window) window->id = value ? 1 : 0;
}

bool window_value(const std::shared_ptr<Window>& window, bool fallback = false)
{
    return window ? window->id != 0 : fallback;
}

using Colour = std::tuple<Integer, Integer, Integer, Integer>;

Value colour_value(const Colour& colour)
{
    return ::cw::lib::Bytes{
        static_cast<std::uint8_t>(std::clamp<Integer>(std::get<0>(colour), 0, 255)),
        static_cast<std::uint8_t>(std::clamp<Integer>(std::get<1>(colour), 0, 255)),
        static_cast<std::uint8_t>(std::clamp<Integer>(std::get<2>(colour), 0, 255)),
        static_cast<std::uint8_t>(std::clamp<Integer>(std::get<3>(colour), 0, 255))};
}

String colour_string(const Colour& colour)
{
    return std::to_string(std::get<0>(colour)) + "," + std::to_string(std::get<1>(colour)) + "," +
           std::to_string(std::get<2>(colour)) + "," + std::to_string(std::get<3>(colour));
}

Colour value_colour(const Value& value, const Colour& fallback)
{
    if (const auto* bytes = std::get_if<::cw::lib::Bytes>(&value.storage()); bytes && bytes->size() >= 3) {
        return {(*bytes)[0], (*bytes)[1], (*bytes)[2], bytes->size() >= 4 ? (*bytes)[3] : std::get<3>(fallback)};
    }
    if (const auto* text = std::get_if<String>(&value.storage())) {
        std::istringstream stream(*text);
        Integer r{}, g{}, b{}, a{std::get<3>(fallback)};
        char comma1{}, comma2{}, comma3{};
        if (stream >> r >> comma1 >> g >> comma2 >> b) {
            if (stream >> comma3 >> a) {
                return {std::clamp<Integer>(r, 0, 255), std::clamp<Integer>(g, 0, 255),
                        std::clamp<Integer>(b, 0, 255), std::clamp<Integer>(a, 0, 255)};
            }
            return {std::clamp<Integer>(r, 0, 255), std::clamp<Integer>(g, 0, 255),
                    std::clamp<Integer>(b, 0, 255), std::get<3>(fallback)};
        }
    }
    return fallback;
}

std::shared_ptr<Window> make_window(String title = {})
{
    auto window = std::make_shared<Window>();
    window->title = std::move(title);
    window->visible = true;
    return window;
}

String number_string(Integer value)
{
    return std::to_string(value);
}

String skin_property(const ::cw::header::GetProperty& reader, const String& name, const String& fallback = {})
{
    const auto it = reader.m_properties.find(name);
    return it == reader.m_properties.end() ? fallback : it->second;
}

String xml_property(const std::filesystem::path& path, const String& name, const String& fallback = {})
{
    ::cw::header::GetProperty reader;
    reader.initialize(::cw::lib::encoding::path_to_utf8(path), std::nullopt, std::nullopt);
    return skin_property(reader, name, fallback);
}

String xml_attribute(const std::filesystem::path& path, const String& element, const String& name,
                    const String& fallback = {})
{
    (void)element;
    ::cw::header::GetProperty reader;
    reader.initialize(::cw::lib::encoding::path_to_utf8(path), std::nullopt, std::nullopt);
    const auto root = reader.m_attrs.find(std::nullopt);
    if (root == reader.m_attrs.end()) return fallback;
    const auto it = root->second.find(name);
    return it == root->second.end() ? fallback : it->second;
}

bool supported_skin_version(const String& version)
{
    const auto& supported = ::cw::g_SUPPORTED_SKIN;
    return std::apply([&](const auto&... item) { return ((version == item) || ...); }, supported);
}

bool xml_bool(const std::filesystem::path& path, const String& name, bool fallback = false)
{
    // Python 互換の大文字小文字の扱いを共有し、不正な値は黙っ
    // て false にせず、スキン概要のエラーハンドラーへ渡す。
    return ::cw::util::str2bool(xml_property(path, name, fallback ? "True" : "False"));
}

Integer skin_integer(const String& text)
{
    const auto parsed = ::cw::lib::parse_python_integer(text);
    // Python の整数は任意精度だが、ネイティブ設定モデルは Integer
    // を使う。 桁あふれは不正入力と同じ概要エラーの経路で拒否する。
    if (!parsed) throw std::invalid_argument("Invalid skin integer");
    return *parsed;
}

Integer xml_integer(const std::filesystem::path& path, const String& name, Integer fallback)
{
    return skin_integer(xml_property(path, name, number_string(fallback)));
}

std::shared_ptr<::cw::setting::Setting> current_setting()
{
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting) return ::cw::g_cwpy->m_setting;
    return std::make_shared<::cw::setting::Setting>();
}

std::shared_ptr<::cw::dialog::settings::SettingsPanel> detail_panel(const Value& value)
{
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage())) {
        return std::dynamic_pointer_cast<::cw::dialog::settings::SettingsPanel>(*object);
    }
    return {};
}

std::shared_ptr<::cw::dialog::settings::SimpleSettingsPanel> simple_panel(const Value& value)
{
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage())) {
        return std::dynamic_pointer_cast<::cw::dialog::settings::SimpleSettingsPanel>(*object);
    }
    return {};
}

void reset_scenario_folder_editor(::cw::dialog::settings::SkinPanel& skin)
{
    if (!skin.m_parent) return;
    const auto panel = detail_panel(skin.m_parent->m_panel);
    if (panel && panel->m_pane_scenario) panel->m_pane_scenario->m_celleditor = false;
}

template <typename Dialog>
struct ReleaseSkinDialog {
    std::shared_ptr<Dialog>& slot;
    ~ReleaseSkinDialog() { slot.reset(); }
};

template <typename T>
std::shared_ptr<T> object_value(const Value& value)
{
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage()); object && *object)
        return std::dynamic_pointer_cast<T>(*object);
    return {};
}

::cw::dialog::settings::FontSettingRow font_row_from_tuple(
    const std::tuple<String, String, Integer, bool, bool, bool>& value)
{
    return {std::get<0>(value), std::get<1>(value), std::get<2>(value), std::get<3>(value),
            std::get<4>(value), std::get<5>(value)};
}

std::tuple<String, String, Integer, bool, bool, bool> font_row_to_tuple(
    const ::cw::dialog::settings::FontSettingRow& value)
{
    return {value.m_fonttype, value.m_face, value.m_pixels, value.m_bold, value.m_bold_upscr, value.m_italic};
}

String replace_all(String source, const String& from, const String& to)
{
    if (from.empty()) return source;
    std::size_t offset = 0;
    while ((offset = source.find(from, offset)) != String::npos) {
        source.replace(offset, from.size(), to);
        offset += to.size();
    }
    return source;
}

String normalize_setting_path(const String& value)
{
    if (value.empty()) return {};
    // Python と同様、ここでは realpath ではなく cw.util.relpath と join_paths
    // を使う。 シンボリックリンクを解決すると、設定に保存するユーザー選択の別名が置き換わってしまう。
    const auto relative = ::cw::util::relpath(value, "");
    const auto selected = relative.rfind("..", 0) != 0 ? relative : value;
    return ::cw::util::join_paths({::cw::lib::Value(selected)});
}

bool settings_confirm(const String& message)
{
    if (const auto result = ::cw::dialog::message::show_common_modal_message(
            "CardWirth", message, true))
        return *result;
    return false;
}

void settings_warning(const String& message)
{
    (void)::cw::dialog::message::show_common_modal_message(
        "メッセージ", message, false, true);
}

void refresh_cards_default(bool arg_menu_cards)
{
    if (!::cw::g_cwpy) return;
    const auto running_event = ::cw::g_cwpy->is_runningevent();
    for (const auto& card : ::cw::g_cwpy->get_pcards(Value("unreversed")))
        if (card) card->update_image(false, running_event);
    if (arg_menu_cards) {
        for (const auto& value : ::cw::g_cwpy->get_mcards(Value{}, {})) {
            if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage()); object && *object) {
                if (const auto card = std::dynamic_pointer_cast<::cw::sprite::card::CWPyCard>(*object); card && card->is_initialized())
                    card->update_scale();
            }
        }
        return;
    }
    // Python と同様、キャラクターカードの描画だけが変わった場合は初期化済み
    // の敵・同行者カードを更新する。 メニューカードの拡縮は別フラグで管理する。
    for (const auto& card : ::cw::g_cwpy->get_ecards(Value("unreversed")))
        if (card && card->is_initialized()) card->update_image(false, running_event);
    for (const auto& card : ::cw::g_cwpy->get_fcards(Value("unreversed")))
        if (card && card->is_initialized()) card->update_image(false, running_event);
}

void refresh_statusbar_default()
{
    if (::cw::g_cwpy && ::cw::g_cwpy->m_statusbar)
        ::cw::g_cwpy->m_statusbar->change(::cw::g_cwpy->m_statusbar->m_showbuttons, false);
}

void restart_soundfonts_default(bool arg_had_soundfonts)
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_setting) return;
    const auto& setting = *::cw::g_cwpy->m_setting;
    std::vector<std::tuple<String, ::cw::lib::Real>> enabled;
    for (const auto& [path, is_enabled, volume] : setting.m_soundfonts_typed)
        if (is_enabled) enabled.emplace_back(path, static_cast<::cw::lib::Real>(volume) / 100.0);
    const auto replay = [] {
        for (const auto& channel : ::cw::thread::CWPy::s_music) {
            if (!channel) continue;
            // Python の play(updatepredata=False) と同
            // 様、MusicInterface 内の補助音量とループ回数を維持する。
            // 既定のカード由来情報と空パスは引き続き渡し、通常の再生・停止経路を通す。
            channel->play(channel->m_path_typed.empty() ? channel->m_path : channel->m_path_typed,
                          false, true, false, 100, 0, 0, {});
        }
    };
    if (::cw::bassplayer::is_alivable() && ::cw::bassplayer::change_soundfonts(enabled)) {
        replay();
        return;
    }
    if (::cw::bassplayer::is_alivable()) ::cw::bassplayer::dispose_bass();
    if (setting.m_sdlmixer_enabled && ::cw::util::sdlmixer_is_initialized())
        ::cw::util::sdlmixer_quit();
    if (!enabled.empty()) ::cw::bassplayer::init_bass(enabled);
    else if (setting.m_sdlmixer_enabled) ::cw::util::sdlmixer_init();
    if ((!enabled.empty()) != arg_had_soundfonts) ::cw::g_cwpy->init_sounds();
    replay();
}

#ifdef _WIN32
String from_wide(const wchar_t* value, std::size_t length)
{
    if (!value || length == 0) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value,
                                         static_cast<int>(length), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    String result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, static_cast<int>(length),
                        result.data(), size, nullptr, nullptr);
    return result;
}

struct FontEnumContext {
    std::vector<String> faces;
};

int CALLBACK enum_font_faces_proc(const LOGFONTW* font, const TEXTMETRICW*, DWORD, LPARAM data)
{
    if (!font || !data) return 1;
    auto& context = *reinterpret_cast<FontEnumContext*>(data);
    const auto name = from_wide(font->lfFaceName, wcsnlen(font->lfFaceName, LF_FACESIZE));
    if (!name.empty() && name.front() != '@' &&
        std::find(context.faces.begin(), context.faces.end(), name) == context.faces.end())
        context.faces.push_back(name);
    return 1;
}

std::vector<String> enumerate_font_faces_win32()
{
    FontEnumContext context;
    const auto dc = GetDC(nullptr);
    if (!dc) return context.faces;
    LOGFONTW font{};
    font.lfCharSet = DEFAULT_CHARSET;
    EnumFontFamiliesExW(dc, &font, enum_font_faces_proc, reinterpret_cast<LPARAM>(&context), 0);
    ReleaseDC(nullptr, dc);
    std::sort(context.faces.begin(), context.faces.end());
    return context.faces;
}

#endif

}  // namespace

namespace cw::dialog::settings {
std::vector<::cw::lib::String> SettingsHostServices::enumerate_font_faces() const
{
#ifdef _WIN32
    return enumerate_font_faces_win32();
#else
    return std::vector<::cw::lib::String>{};
#endif
}
void SettingsHostServices::restart_soundfonts()
{
    const auto setting = ::cw::g_cwpy ? ::cw::g_cwpy->m_setting : nullptr;
    const bool enabled = setting && std::any_of(setting->m_soundfonts_typed.begin(),
        setting->m_soundfonts_typed.end(), [](const auto& row) { return std::get<1>(row); });
    restart_soundfonts(enabled);
}
void SettingsHostServices::restart_soundfonts(bool arg_had_soundfonts)
{
    restart_soundfonts_default(arg_had_soundfonts);
}
void SettingsHostServices::refresh_cards(bool arg_menu_cards) { refresh_cards_default(arg_menu_cards); }
void SettingsHostServices::refresh_statusbar() { refresh_statusbar_default(); }
namespace {
const std::shared_ptr<SettingsHostServices>& fixed_host_services()
{
    static const auto services = std::make_shared<SettingsHostServices>();
    return services;
}
}

std::shared_ptr<SettingsHostServices> get_host_services()
{
    return fixed_host_services();
}

void AudioSettingPanel::initialize(std::shared_ptr<::cw::lib::Window> arg_parent)
{
    (void)arg_parent;
    m_box_gene = "詳細";
    m_box_master = "全体音量(右クリック+ホイールでも調節可能)";
    m_box_music = "ミュージック音量";
    m_box_midi = "MIDIミュージック音量";
    m_box_sound = "効果音音量";
    m_box_soundfont = "MIDIサウンドフォント";
    m_cb_playbgm = false;
    m_cb_playsound = false;
    m_sl_master = static_cast<Integer>(0);
    m_sl_music = static_cast<Integer>(0);
    m_sl_midi = static_cast<Integer>(0);
    m_sl_sound = static_cast<Integer>(0);
    m_grid_soundfont = static_cast<Integer>(0);
    m__soundfonts.clear();
    m__selected_soundfont_rows.clear();
    _select_changed_soundfonts();
}

void AudioSettingPanel::load(std::shared_ptr<::cw::setting::Setting> arg_setting)
{
    if (!arg_setting) return;
    m_cb_playbgm = arg_setting->m_play_bgm;
    m_cb_playsound = arg_setting->m_play_sound;
    m_sl_master = static_cast<Integer>(arg_setting->m_vol_master * 100.0);
    m_sl_music = static_cast<Integer>(arg_setting->m_vol_bgm * 100.0);
    m_sl_midi = static_cast<Integer>(arg_setting->m_vol_bgm_midi * 100.0);
    m_sl_sound = static_cast<Integer>(arg_setting->m_vol_sound * 100.0);
    std::vector<std::tuple<String, bool, Integer>> soundfonts{{::cw::g_DEFAULT_SOUNDFONT, true, 100}};
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&arg_setting->m_soundfonts.storage()); object && *object) {
        if (const auto table = std::dynamic_pointer_cast<SoundFontTable>(*object); table) soundfonts = table->m_rows;
    }
    _init_soundfont(std::move(soundfonts));
}

void AudioSettingPanel::init_values(std::shared_ptr<::cw::setting::Setting> arg_setting)
{
    if (!arg_setting) return;
    m_cb_playbgm = value_bool(arg_setting->m_play_bgm_init);
    m_cb_playsound = value_bool(arg_setting->m_play_sound_init);
    m_sl_master = static_cast<Integer>(value_real(arg_setting->m_vol_master_init) * 100.0);
    m_sl_music = static_cast<Integer>(value_real(arg_setting->m_vol_bgm_init) * 100.0);
    m_sl_midi = static_cast<Integer>(value_real(arg_setting->m_vol_bgm_midi_init) * 100.0);
    m_sl_sound = static_cast<Integer>(value_real(arg_setting->m_vol_sound_init) * 100.0);
    std::vector<std::tuple<String, bool, Integer>> soundfonts{{::cw::g_DEFAULT_SOUNDFONT, true, 100}};
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&arg_setting->m_soundfonts_init.storage()); object && *object) {
        if (const auto table = std::dynamic_pointer_cast<SoundFontTable>(*object); table) soundfonts = table->m_rows;
    }
    _init_soundfont(std::move(soundfonts));
}

void AudioSettingPanel::_init_soundfont(std::vector<std::tuple<::cw::lib::String, bool, ::cw::lib::Integer>> arg_soundfonts)
{
    m__soundfonts = std::move(arg_soundfonts);
    m__selected_soundfont_rows.clear();
    m_grid_soundfont = static_cast<Integer>(m__soundfonts.size());
    for (Integer row = 0; row < static_cast<Integer>(m__soundfonts.size()); ++row) set_soundfont(row, m__soundfonts[static_cast<std::size_t>(row)]);
    _select_changed_soundfonts();
}

void AudioSettingPanel::set_soundfont(::cw::lib::Integer arg_row, std::tuple<::cw::lib::String, bool, ::cw::lib::Integer> arg_soundfont)
{
    if (arg_row < 0 || static_cast<std::size_t>(arg_row) >= m__soundfonts.size()) return;
    m__soundfonts[static_cast<std::size_t>(arg_row)] = std::move(arg_soundfont);
}

std::tuple<::cw::lib::String, bool, ::cw::lib::Integer> AudioSettingPanel::get_soundfont(::cw::lib::Integer arg_row)
{
    if (arg_row < 0 || static_cast<std::size_t>(arg_row) >= m__soundfonts.size()) return {};
    return m__soundfonts[static_cast<std::size_t>(arg_row)];
}

void AudioSettingPanel::_bind()
{
    // 入力通知は共通Presentationが各On*メソッドへ一元配線する。
}

void AudioSettingPanel::_do_layout()
{
    // 共通Presentationが音量とSoundFont行を生成・配置する。
}

void AudioSettingPanel::OnGridRangeSelect(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    // 共通表示側で同期済みの選択行から有効状態を更新する。
    _select_changed_soundfonts();
}

void AudioSettingPanel::_select_changed_soundfonts()
{
    std::sort(m__selected_soundfont_rows.begin(), m__selected_soundfont_rows.end());
    m__selected_soundfont_rows.erase(
        std::remove_if(m__selected_soundfont_rows.begin(), m__selected_soundfont_rows.end(),
                       [this](Integer row) { return row < 0 || static_cast<std::size_t>(row) >= m__soundfonts.size(); }),
        m__selected_soundfont_rows.end());
    m__selected_soundfont_rows.erase(
        std::unique(m__selected_soundfont_rows.begin(), m__selected_soundfont_rows.end()),
        m__selected_soundfont_rows.end());
    m__remove_soundfont_enabled = !m__selected_soundfont_rows.empty();
    m__up_soundfont_enabled = m__remove_soundfont_enabled && m__selected_soundfont_rows.front() > 0;
    m__down_soundfont_enabled = m__remove_soundfont_enabled &&
        m__selected_soundfont_rows.back() + 1 < static_cast<Integer>(m__soundfonts.size());
    set_window_enabled(value_window(m_btn_rmvsoundfont), m__remove_soundfont_enabled);
    set_window_enabled(value_window(m_btn_upsoundfont), m__up_soundfont_enabled);
    set_window_enabled(value_window(m_btn_downsoundfont), m__down_soundfont_enabled);
}

void AudioSettingPanel::OnAddSoundFontBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m_settings_parent) m_settings_parent->request_document(SettingsDocumentAction::SoundFonts);
}

void AudioSettingPanel::add_soundfont_files(const std::vector<String>& paths)
{
    if (paths.empty()) return;
    std::set<String> exists;
    for (const auto& [path, unused_enabled, unused_volume] : m__soundfonts) {
        (void)unused_enabled;
        (void)unused_volume;
        exists.insert(::cw::lib::encoding::lower(path));
    }
    for (const auto& selected : paths) {
        const auto path = normalize_setting_path(selected);
        // Python と同様、選択画面を開く前に存在した行と str.lower()
        // で比較する。 この一括処理の途中で、その集合へ追加しない。
        if (path.empty() || exists.contains(::cw::lib::encoding::lower(path))) continue;
        m__soundfonts.emplace_back(path, true, 100);
    }
    m_grid_soundfont = static_cast<Integer>(m__soundfonts.size());
    _select_changed_soundfonts();
}

void AudioSettingPanel::OnRemoveSoundFontBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    std::sort(m__selected_soundfont_rows.rbegin(), m__selected_soundfont_rows.rend());
    for (const auto row : m__selected_soundfont_rows) {
        if (row >= 0 && static_cast<std::size_t>(row) < m__soundfonts.size())
            m__soundfonts.erase(m__soundfonts.begin() + row);
    }
    m__selected_soundfont_rows.clear();
    m_grid_soundfont = static_cast<Integer>(m__soundfonts.size());
    _select_changed_soundfonts();
}

void AudioSettingPanel::OnUpSoundFontBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    std::sort(m__selected_soundfont_rows.begin(), m__selected_soundfont_rows.end());
    if (m__selected_soundfont_rows.empty() || m__selected_soundfont_rows.front() < 1) return;
    std::vector<Integer> moved;
    moved.reserve(m__selected_soundfont_rows.size());
    for (const auto row : m__selected_soundfont_rows) {
        const auto previous = get_soundfont(row - 1);
        const auto current = get_soundfont(row);
        set_soundfont(row - 1, current);
        set_soundfont(row, previous);
        moved.push_back(row - 1);
    }
    m__selected_soundfont_rows = std::move(moved);
    _select_changed_soundfonts();
}

void AudioSettingPanel::OnDownSoundFontBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    std::sort(m__selected_soundfont_rows.begin(), m__selected_soundfont_rows.end());
    if (m__selected_soundfont_rows.empty() ||
        m__selected_soundfont_rows.back() + 1 >= static_cast<Integer>(m__soundfonts.size())) return;
    std::vector<Integer> moved;
    moved.reserve(m__selected_soundfont_rows.size());
    for (auto it = m__selected_soundfont_rows.rbegin(); it != m__selected_soundfont_rows.rend(); ++it) {
        const auto row = *it;
        const auto current = get_soundfont(row);
        const auto next = get_soundfont(row + 1);
        set_soundfont(row, next);
        set_soundfont(row + 1, current);
        moved.push_back(row + 1);
    }
    std::sort(moved.begin(), moved.end());
    m__selected_soundfont_rows = std::move(moved);
    _select_changed_soundfonts();
}

void DrawingSettingPanel::initialize(std::shared_ptr<::cw::lib::Window> arg_parent, bool arg_for_local, std::optional<::cw::lib::Callback> arg_get_localsettings, bool arg_use_copybase)
{
    (void)arg_parent;
    m__for_local = arg_for_local;
    m__get_localsettings_cb = normalize_settings_callback(std::move(arg_get_localsettings));
    m_box_mwin = "メッセージウィンドウ背景";
    m_box_mframe = "メッセージウィンドウ枠";
    m_box_blcurtain = "メッセージログの背景";
    m_box_curtain = "カーテン(選択モードの背景効果)";
    m_box_fscrback = "フルスクリーンの背景";
    m_st_mwin = "カラー";
    m_st_blwin = "ログ";
    m_st_mwin2 = "アルファ値";
    m_st_mframe = "カラー";
    m_st_blframe = "ログ";
    m_st_mframe2 = "アルファ値";
    m_st_blcurtain = "カラー";
    m_st_blcurtain2 = "アルファ値";
    m_st_curtain = "カラー";
    m_st_curtain2 = "アルファ値";
    m_cs_mwin = colour_value({0, 0, 80, 180});
    m_cs_blwin = colour_value({80, 80, 80, 180});
    m_sc_mwin = Integer{180};
    m_cs_mframe = colour_value({128, 0, 0, 255});
    m_cs_blframe = colour_value({128, 128, 128, 255});
    m_sc_mframe = Integer{255};
    m_cs_blcurtain = colour_value({0, 0, 0, 192});
    m_sc_blcurtain = Integer{192};
    m_cs_curtain = colour_value({0, 0, 80, 128});
    m_sc_curtain = Integer{128};
    m_ch_fscrbacktype = Integer{0};
    m_tx_fscrbackfile = String{};
    m__controls_enabled = true;
    m__fullscreen_file_enabled = false;
    if (arg_for_local) {
        m_cb_important = false;
        m_copybtn = arg_use_copybase;
        m_initbtn = true;
    } else {
        m_box_gene = "詳細";
        m_cb_smoothing_card_up = false;
        m_cb_smoothing_card_down = false;
        m_cb_smooth_bg = false;
        m_cb_whitecursor = false;
        m_speed = std::make_shared<::cw::dialog::settings::SpeedPanel>();
        m_speed->initialize(std::make_shared<::cw::lib::Panel>(), true);
    }
    _update_enabled();
    _bind();
    _do_layout();
}

void DrawingSettingPanel::load(std::optional<std::shared_ptr<::cw::setting::Setting>> arg_setting, std::shared_ptr<::cw::setting::LocalSetting> arg_local)
{
    if (!arg_local) return;
    if (value_bool(m__for_local)) {
        m_cb_important = arg_local->m_important_draw;
    } else if (arg_setting && *arg_setting) {
        m_cb_smooth_bg = (*arg_setting)->m_smoothscale_bg;
        m_cb_smoothing_card_up = (*arg_setting)->m_smoothing_card_up;
        m_cb_smoothing_card_down = (*arg_setting)->m_smoothing_card_down;
        m_cb_whitecursor = (*arg_setting)->m_cursor_type == ::cw::setting::g_CURSOR_WHITE;
        if (m_speed) m_speed->load(*arg_setting);
    }
    m_cs_mwin = colour_value(arg_local->m_mwincolour);
    m_sc_mwin = std::get<3>(arg_local->m_mwincolour);
    m_cs_blwin = colour_value(arg_local->m_blwincolour);
    m_cs_mframe = colour_value(arg_local->m_mwinframecolour);
    m_sc_mframe = std::get<3>(arg_local->m_mwinframecolour);
    m_cs_blframe = colour_value(arg_local->m_blwinframecolour);
    const auto current_blcurtain = value_colour(arg_local->m_blcurtaincolour, Colour{0, 0, 0, 192});
    m_cs_blcurtain = colour_value(current_blcurtain);
    m_sc_blcurtain = std::get<3>(current_blcurtain);
    const auto current_curtain = value_colour(arg_local->m_curtaincolour, Colour{0, 0, 80, 128});
    m_cs_curtain = colour_value(current_curtain);
    m_sc_curtain = std::get<3>(current_curtain);
    m_cs_allengineframe = colour_value(arg_local->m_allengineframecolour);
    m_sc_allengineframe = std::get<3>(arg_local->m_allengineframecolour);
    switch (arg_local->m_fullscreenbackgroundtype) {
    case 1:
        m_ch_fscrbacktype = Integer{1};
        m_tx_fscrbackfile = arg_local->m_fullscreenbackgroundfile;
        break;
    case 2:
        if (arg_local->m_fullscreenbackgroundfile == "Resource/Image/Dialog/CAUTION") m_ch_fscrbacktype = Integer{2};
        else if (arg_local->m_fullscreenbackgroundfile == "Resource/Image/Dialog/PAD") m_ch_fscrbacktype = Integer{3};
        else m_ch_fscrbacktype = Integer{0};
        m_tx_fscrbackfile = String{};
        break;
    default:
        m_ch_fscrbacktype = Integer{0};
        m_tx_fscrbackfile = String{};
        break;
    }
    _update_enabled();
}

// Python は type 2 で CAUTION 以外の全ファイルを PAD に
// 対応付ける。 ここでは既知の PAD パスだけをそのプリセットにし、未知の保存済み
// パスは None にする。 不正な値を黙って PAD リソースと表示し直さない。
void DrawingSettingPanel::init_values(std::optional<std::shared_ptr<::cw::setting::Setting>> arg_setting, std::shared_ptr<::cw::setting::LocalSetting> arg_local)
{
    if (!arg_local) return;
    if (!value_bool(m__for_local) && arg_setting && *arg_setting) {
        m_cb_smoothing_card_up = value_bool((*arg_setting)->m_smoothing_card_up_init);
        m_cb_smoothing_card_down = value_bool((*arg_setting)->m_smoothing_card_down_init);
        m_cb_smooth_bg = value_bool((*arg_setting)->m_smoothscale_bg_init);
        m_cb_whitecursor = value_string((*arg_setting)->m_cursor_type_init) == ::cw::setting::g_CURSOR_WHITE;
        if (m_speed) {
            m_speed->m_sl_deal = value_integer((*arg_setting)->m_dealspeed_init);
            m_speed->m_sl_deal_battle = value_integer((*arg_setting)->m_dealspeed_battle_init);
            m_speed->m_cb_use_battlespeed = !value_bool((*arg_setting)->m_use_battlespeed_init);
            m_speed->m__battle_enabled = value_bool((*arg_setting)->m_use_battlespeed_init);
            m_speed->m_sl_msgs = value_integer((*arg_setting)->m_messagespeed_init);
            const auto transition = value_string((*arg_setting)->m_transition_init);
            const auto it = std::find(m_speed->m_transitions.begin(), m_speed->m_transitions.end(), transition);
            m_speed->m_ch_tran = it == m_speed->m_transitions.end() ? Integer{0} : static_cast<Integer>(std::distance(m_speed->m_transitions.begin(), it));
            m_speed->m_sl_tran = value_integer((*arg_setting)->m_transitionspeed_init);
        }
    }
    m_sc_mwin = std::get<3>(value_colour(arg_local->m_mwincolour_init, arg_local->m_mwincolour));
    m_cs_mwin = colour_value(value_colour(arg_local->m_mwincolour_init, arg_local->m_mwincolour));
    m_sc_mframe = std::get<3>(value_colour(arg_local->m_mwinframecolour_init, arg_local->m_mwinframecolour));
    m_cs_mframe = colour_value(value_colour(arg_local->m_mwinframecolour_init, arg_local->m_mwinframecolour));
    m_cs_blwin = colour_value(value_colour(arg_local->m_blwincolour_init, arg_local->m_blwincolour));
    m_cs_blframe = colour_value(value_colour(arg_local->m_blwinframecolour_init, arg_local->m_blwinframecolour));
    const auto blcurtain_fallback = value_colour(arg_local->m_blcurtaincolour, Colour{0, 0, 0, 192});
    const auto blcurtain_initial = value_colour(arg_local->m_blcurtaincolour_init, blcurtain_fallback);
    m_sc_blcurtain = std::get<3>(blcurtain_initial);
    m_cs_blcurtain = colour_value(blcurtain_initial);
    const auto curtain_fallback = value_colour(arg_local->m_curtaincolour, Colour{0, 0, 80, 128});
    const auto curtain_initial = value_colour(arg_local->m_curtaincolour_init, curtain_fallback);
    m_sc_curtain = std::get<3>(curtain_initial);
    m_cs_curtain = colour_value(curtain_initial);
    const auto frame_initial = value_colour(arg_local->m_mwinframecolour_init, Colour{128,0,0,255});
    m_cs_allengineframe = colour_value(frame_initial);
    m_sc_allengineframe = std::get<3>(frame_initial);
    const auto type = value_integer(arg_local->m_fullscreenbackgroundtype_init, arg_local->m_fullscreenbackgroundtype);
    const auto file = value_string(arg_local->m_fullscreenbackgroundfile_init, arg_local->m_fullscreenbackgroundfile);
    m_ch_fscrbacktype = Integer{type == 1 ? 1 : type == 2 && file == "Resource/Image/Dialog/CAUTION" ? 2 :
        type == 2 && file == "Resource/Image/Dialog/PAD" ? 3 : 0};
    m_tx_fscrbackfile = type == 1 ? file : String{};
    _update_enabled();
}

std::tuple<bool, bool, bool> DrawingSettingPanel::apply_localsettings(std::shared_ptr<::cw::setting::LocalSetting> arg_local)
{
    if (!arg_local) return {false, false, false};
    bool updatemessage = false;
    const auto mwin = value_colour(m_cs_mwin, arg_local->m_mwincolour);
    const auto mwin_with_alpha = Colour{std::get<0>(mwin), std::get<1>(mwin), std::get<2>(mwin), value_integer(m_sc_mwin, std::get<3>(mwin))};
    updatemessage |= arg_local->m_mwincolour != mwin_with_alpha;
    arg_local->m_mwincolour = mwin_with_alpha;
    const auto mframe = value_colour(m_cs_mframe, arg_local->m_mwinframecolour);
    const auto mframe_with_alpha = Colour{std::get<0>(mframe), std::get<1>(mframe), std::get<2>(mframe), value_integer(m_sc_mframe, std::get<3>(mframe))};
    updatemessage |= arg_local->m_mwinframecolour != mframe_with_alpha;
    arg_local->m_mwinframecolour = mframe_with_alpha;

    // Python版と同じくログ側のアルファ値は対応する枠側スピン値を使う。
    const auto blwin = value_colour(m_cs_blwin, arg_local->m_blwincolour);
    const auto blwin_with_alpha = Colour{std::get<0>(blwin), std::get<1>(blwin), std::get<2>(blwin), value_integer(m_sc_mwin, std::get<3>(blwin))};
    updatemessage |= arg_local->m_blwincolour != blwin_with_alpha;
    arg_local->m_blwincolour = blwin_with_alpha;
    const auto blframe = value_colour(m_cs_blframe, arg_local->m_blwinframecolour);
    const auto blframe_with_alpha = Colour{std::get<0>(blframe), std::get<1>(blframe), std::get<2>(blframe), value_integer(m_sc_mframe, std::get<3>(blframe))};
    updatemessage |= arg_local->m_blwinframecolour != blframe_with_alpha;
    arg_local->m_blwinframecolour = blframe_with_alpha;

    bool updatecurtain = false;
    const auto blcurtain = value_colour(m_cs_blcurtain, value_colour(arg_local->m_blcurtaincolour, Colour{0, 0, 0, 192}));
    const auto blcurtain_with_alpha = Colour{std::get<0>(blcurtain), std::get<1>(blcurtain), std::get<2>(blcurtain), value_integer(m_sc_blcurtain, std::get<3>(blcurtain))};
    updatecurtain |= value_colour(arg_local->m_blcurtaincolour, Colour{0, 0, 0, 192}) != blcurtain_with_alpha;
    arg_local->m_blcurtaincolour = colour_string(blcurtain_with_alpha);
    const auto curtain = value_colour(m_cs_curtain, value_colour(arg_local->m_curtaincolour, Colour{0, 0, 80, 128}));
    const auto curtain_with_alpha = Colour{std::get<0>(curtain), std::get<1>(curtain), std::get<2>(curtain), value_integer(m_sc_curtain, std::get<3>(curtain))};
    updatecurtain |= value_colour(arg_local->m_curtaincolour, Colour{0, 0, 80, 128}) != curtain_with_alpha;
    arg_local->m_curtaincolour = colour_string(curtain_with_alpha);
    if (::cw::dialog::is_all_mode()) {
        const auto frame = value_colour(m_cs_allengineframe, arg_local->m_allengineframecolour);
        arg_local->m_allengineframecolour = Colour{std::get<0>(frame),std::get<1>(frame),std::get<2>(frame),
            std::clamp<Integer>(value_integer(m_sc_allengineframe,255),0,255)};
    }

    const Integer selection = value_integer(m_ch_fscrbacktype, 0);
    String file = arg_local->m_fullscreenbackgroundfile;
    Integer type = arg_local->m_fullscreenbackgroundtype;
    if (selection == 0) { file.clear(); type = 0; }
    else if (selection == 1) { file = value_string(m_tx_fscrbackfile); type = 1; }
    else if (selection == 2) { file = "Resource/Image/Dialog/CAUTION"; type = 2; }
    else if (selection == 3) { file = "Resource/Image/Dialog/PAD"; type = 2; }
    const bool updatefullscreen = file != arg_local->m_fullscreenbackgroundfile || type != arg_local->m_fullscreenbackgroundtype;
    arg_local->m_fullscreenbackgroundfile = file;
    arg_local->m_fullscreenbackgroundtype = type;
    if (value_bool(m__for_local) && value_bool(m_cb_important) != arg_local->m_important_draw) {
        arg_local->m_important_draw = value_bool(m_cb_important);
        updatemessage = true;
        updatecurtain = true;
        return {updatemessage, updatecurtain, true};
    }
    return {updatemessage, updatecurtain, updatefullscreen};
}

void DrawingSettingPanel::_bind()
{
    // 共通Presentationのcombo/check/button通知が下記On*処理を呼び出す。
}

void DrawingSettingPanel::_do_layout()
{
    // 描画設定の配置は共通Presentationが一度だけ構築する。
}

// Python の直接ハンドラーは type==1 だけでファイル操
// 作を有効にする。 ネイティブのコントロールは、親の選択状態に合わせ
// て優先・ローカル上書きも有効であることを必要とし、 無効な上書き
// 設定の変更で子のファイル選択だけが編集可能にならないようにする。
void DrawingSettingPanel::OnFullScreenBackgroundType(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    _update_enabled();
}

void DrawingSettingPanel::OnImportant(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    _update_enabled();
}

void DrawingSettingPanel::_update_enabled()
{
    m__controls_enabled = !value_bool(m__for_local) || value_bool(m_cb_important);
    m__fullscreen_file_enabled = m__controls_enabled && value_integer(m_ch_fscrbacktype) == 1;
    m_ref_fscrbackfile = m__fullscreen_file_enabled;
}

void DrawingSettingPanel::OnInitValue(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (!value_bool(m__for_local)) return;
    auto local = std::make_shared<::cw::setting::LocalSetting>();
    local->init();
    init_values(std::nullopt, local);
}

void DrawingSettingPanel::OnCopyBase(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    std::shared_ptr<::cw::setting::LocalSetting> local;
    if (m__get_localsettings_cb) {
        const auto value = (*m__get_localsettings_cb)({}, {});
        local = object_value<::cw::setting::LocalSetting>(value);
    }
    if (!local && ::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_local)
        local = ::cw::g_cwpy->m_setting->m_local->copy();
    if (!local) {
        local = std::make_shared<::cw::setting::LocalSetting>();
        local->init();
    }
    local->m_important_draw = true;
    load(std::nullopt, std::move(local));
}

void ExpandPanel::initialize(std::shared_ptr<::cw::lib::Panel> arg_parent, bool arg_options)
{
    (void)arg_parent;
    m_options = arg_options;
    m__expand_enable = true;
    m__expand_max = 100;
    m_st_expandscr = "描画倍率:";
    m_st_expandwin = "表示倍率:";
    m_cb_fullscreen = false;
    m_ch_expanddrawing = Integer{0};
    m_sl_expand = Integer{10};
    m_st_expand = "1.0倍";
    if (arg_options) {
        m_ln_expand = true;
        m_cb_smoothexpand = false;
    }
    _bind();
    _do_layout();
}

void ExpandPanel::_bind()
{
    // 倍率とfullscreenの通知は親設定画面のbackendでモデルへ同期される。
}

void ExpandPanel::load(std::shared_ptr<::cw::setting::Setting> arg_setting)
{
    if (!arg_setting) return;
    const bool fullscreen = arg_setting->m_expandmode == "FullScreen";
    m_cb_fullscreen = fullscreen;
    if (value_bool(m_options)) m_cb_smoothexpand = arg_setting->m_smoothexpand;

    // wx版は実ディスプレイサイズから上限を求める。SDL UI未接続時は
    // 10倍を上限とするが、設定値の選択・保存規則は同じにする。
    Integer nmax = 10;
    if (::cw::g_cwpy) {
        if (const auto frame = object_value<::cw::frame::Frame>(::cw::g_cwpy->m_frame)) {
            if (const auto display = frame->get_displaysize()) {
                const auto display_x = display->bounds.width;
                const auto display_y = display->bounds.height;
                const auto scr_x = std::max<Integer>(1, std::get<0>(::cw::g_SIZE_SCR));
                const auto scr_y = std::max<Integer>(1, std::get<1>(::cw::g_SIZE_SCR));
                nmax = std::min<Integer>(10 * display_x / scr_x, 10 * display_y / scr_y);
            }
        }
    }
    nmax = std::max<Integer>(10, nmax);
    m__expand_max = nmax;
    Integer n = arg_setting->m_expandmode == "None" || fullscreen ? 10 :
        std::clamp<Integer>(value_integer(arg_setting->m_expanddrawing_init, 2) * 10, 10, nmax);
    if (!fullscreen && arg_setting->m_expandmode != "None") {
        // 保存された倍率が空・不正でも設定ダイアログを開けるようにする。 NaN
        // ・Inf も含め、Python 互換の切り捨てを行う前に検証する。
        try {
            std::size_t used = 0;
            const auto scale = std::stod(arg_setting->m_expandmode, &used);
            if (used == arg_setting->m_expandmode.size() && std::isfinite(scale))
                n = static_cast<Integer>(std::clamp(scale, 1.0,
                    static_cast<double>(nmax) / 10.0) * 10.0);
        } catch (const std::invalid_argument&) {
        } catch (const std::out_of_range&) {
        }
    }
    if (n < 10) n = 10;
    if (n > nmax) n = nmax;
    m__expand_enable = nmax > 10;
    m_sl_expand = n;

    // WASM では範囲外の浮動小数点キャストがトラップになる場合があるため、変換前に検証する。
    const auto raw_drawing = arg_setting->m_expanddrawing;
    Integer drawing = std::isfinite(raw_drawing) && raw_drawing >= 1.0 &&
        raw_drawing < static_cast<double>(std::numeric_limits<Integer>::max())
        ? static_cast<Integer>(raw_drawing) : value_integer(arg_setting->m_expanddrawing_init, 2);
    Integer selection = 0;
    for(Integer i=0;i<=expand_selection_max(nmax);++i)
        if(drawing==(1LL<<i)) { selection=i; break; }
    m_ch_expanddrawing = selection;
    m_sl_expand = n;
    make_expandinfo();
}

void ExpandPanel::make_expandinfo()
{
    if (value_bool(m_cb_fullscreen)) {
        m_st_expand = "フルスクリーン";
        return;
    }
    const Integer n = value_integer(m_sl_expand, 10);
    const auto game = ::cw::g_SIZE_GAME;
    const Integer game_x = std::get<0>(game);
    const Integer game_y = std::get<1>(game);
    const Integer x = game_x * n / 10;
    const Integer y = game_y * n / 10;
    m_st_expand = std::to_string(n / 10) + "." + std::to_string(n % 10) + "倍 (" +
                  std::to_string(x) + "x" + std::to_string(y) + ")";
}

void ExpandPanel::OnExpandChange(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    make_expandinfo();
}

void ExpandPanel::_do_layout()
{
    // 親設定画面のbackendがchoice/trackbar/checkを生成・配置する。
}

void ExpandPanel::apply_expand(std::shared_ptr<::cw::setting::Setting> arg_setting)
{
    if (!arg_setting) return;
    if (value_bool(m_options)) arg_setting->m_smoothexpand = value_bool(m_cb_smoothexpand);
    String mode;
    if (value_bool(m_cb_fullscreen)) {
        mode = "FullScreen";
    } else {
        const Integer n = value_integer(m_sl_expand, 10);
        if (n == 10) mode = "None";
        else if (n % 10 == 0) mode = std::to_string(n / 10);
        else mode = std::to_string(n / 10) + "." + std::to_string(n % 10);
    }
    const Integer selection = std::clamp<Integer>(value_integer(m_ch_expanddrawing),0,expand_selection_max(m__expand_max));
    const auto drawing = static_cast<::cw::lib::Real>(1LL << std::min<Integer>(selection, 20));
    bool same_mode = arg_setting->m_expandmode == mode;
    if (!same_mode && mode != "None" && mode != "FullScreen") {
        // C++ の文字列整形によって、数値として同じ倍率を拡縮要求に変えな
        // い。 例えば 1.5 と 1.500000 は同じ値として扱う。
        try {
            std::size_t used = 0;
            const auto old_scale = std::stod(arg_setting->m_expandmode, &used);
            same_mode = used == arg_setting->m_expandmode.size() && std::isfinite(old_scale) &&
                old_scale == static_cast<double>(value_integer(m_sl_expand, 10)) / 10.0;
        } catch (const std::invalid_argument&) {
        } catch (const std::out_of_range&) {
        }
    }
    if (same_mode && arg_setting->m_expanddrawing == drawing) return;
    if (::cw::g_cwpy && arg_setting == ::cw::g_cwpy->m_setting && ::cw::g_cwpy->is_expanded()) {
        // apply_expand は既に CWPy のゲームスレッド
        // で呼ばれる。編集値を反映してから実際の拡縮を予約し、 その値
        // が現在も有効な場合だけ予約処理を実行する。SettingsDialog
        // は後の適用処理に失敗すると旧 Setting へ戻す
        // ため、 古い予約コールバックでその復元を取り消さない。
        arg_setting->m_expandmode = mode;
        arg_setting->m_expanddrawing = drawing;
        ::cw::g_cwpy->exec_func([arg_setting, mode, drawing](const auto&, const auto&) {
            if (!::cw::g_cwpy || arg_setting != ::cw::g_cwpy->m_setting) return Value{};
            if (arg_setting->m_expandmode != mode || arg_setting->m_expanddrawing != drawing) return Value{};
            ::cw::g_cwpy->set_expanded(mode != "FullScreen", mode, true, std::nullopt);
            return Value{};
        });
    } else {
        // Engine が現在拡大表示でなければ、Python
        // と同様に値だけを保存し、次の F4 による拡大で使う。
        arg_setting->m_expandmode = mode;
        arg_setting->m_expanddrawing = drawing;
    }
}

void ExpandPanel::copy_values(std::shared_ptr<::cw::dialog::settings::ExpandPanel> arg_expand)
{
    if (!arg_expand) return;
    m_ch_expanddrawing = std::clamp<Integer>(value_integer(arg_expand->m_ch_expanddrawing),0,expand_selection_max(m__expand_max));
    m_sl_expand = std::clamp<Integer>(value_integer(arg_expand->m_sl_expand),10,std::max<Integer>(10,m__expand_max));
    m_cb_fullscreen = value_bool(arg_expand->m_cb_fullscreen);
    if (value_bool(m_options) && value_bool(arg_expand->m_options)) {
        m_cb_smoothexpand = value_bool(arg_expand->m_cb_smoothexpand);
    }
    make_expandinfo();
}

void FontSettingPanel::initialize(std::shared_ptr<::cw::lib::Window> arg_parent, bool arg_for_local, std::optional<::cw::lib::Callback> arg_get_localsettings, bool arg_use_copybase)
{
    (void)arg_parent;
    m__for_local = arg_for_local;
    m__get_localsettings_cb = normalize_settings_callback(std::move(arg_get_localsettings));
    m_str_default = "[標準フォント]";
    m__bases_order = {"gothic", "pgothic", "mincho", "pmincho", "uigothic"};
    m__types = {"cardname", "ccardname", "level", "message", "fw_symbol", "fw_number", "fw_latin",
                "hiragana", "katakana", "hw_katakana", "greek_and_cyrillic", "jis_kanji_1", "jis_kanji_2",
                "etc_kanji", "symbol", "number", "latin", "selectionbar", "logpage", "uselimit", "price",
                "numcards", "statusnum", "sbarpanel", "sbarprogress", "sbarbtn", "sbardesctitle", "sbardesc",
                "screenshot", "scenario", "targetlevel", "paneltitle", "paneltitle2", "dlgmsg", "dlgmsg2",
                "dlgtitle", "dlgtitle2", "createtitle", "dlglist", "inputname", "datadesc", "charadesc",
                "charaparam", "charaparam2", "characre", "button", "combo", "slider", "spin", "tree", "list",
                "tab", "menu"};
    m_msg_exfonttypes = {"fw_symbol", "fw_number", "fw_latin", "hiragana", "katakana", "hw_katakana",
                         "greek_and_cyrillic", "jis_kanji_1", "jis_kanji_2", "etc_kanji", "symbol", "number", "latin"};
    m_typenames = {{"inherit", "指定しない"}, {"gothic", "等幅ゴシック"}, {"uigothic", "UI用"},
                   {"mincho", "等幅明朝"}, {"pmincho", "可変幅明朝"}, {"pgothic", "可変幅ゴシック"},
                   {"cardname", "カード名"}, {"ccardname", "キャストカード名"}, {"level", "カードレベル"},
                   {"message", "メッセージ"}, {"fw_symbol", " - 全角記号"}, {"fw_number", " - 全角数字"},
                   {"fw_latin", " - 全角英字"}, {"hiragana", " - ひらがな"}, {"katakana", " - カタカナ"},
                   {"hw_katakana", " - 半角カナ"}, {"greek_and_cyrillic", " - ギリシャ・キリル"},
                   {"jis_kanji_1", " - JIS第1水準漢字"}, {"jis_kanji_2", " - JIS第2水準漢字"},
                   {"etc_kanji", " - 外字"}, {"symbol", " - 半角記号"}, {"number", " - 半角数字"},
                   {"latin", " - 半角英字"}, {"selectionbar", "選択肢"}, {"logpage", "メッセージログ頁"},
                   {"uselimit", "カード残り回数"}, {"price", "カード価格"}, {"numcards", "カード枚数"},
                   {"statusnum", "状態値"}, {"sbarpanel", "ステータスパネル"}, {"sbarprogress", "進行状況・音量バー"},
                   {"sbarbtn", "ステータスボタン"}, {"sbardesctitle", "ボタン解説の表題"}, {"sbardesc", "ボタン解説"},
                   {"screenshot", "撮影情報"}, {"scenario", "貼紙見出し"}, {"targetlevel", "対象レベル"},
                   {"paneltitle", "パネル見出し1"}, {"paneltitle2", "パネル見出し2"}, {"dlgmsg", "ダイアログテキスト1"},
                   {"dlgmsg2", "ダイアログテキスト2"}, {"dlgtitle", "英文見出し1"}, {"dlgtitle2", "英文見出し2"},
                   {"createtitle", "登録見出し"}, {"dlglist", "ダイアログリスト"}, {"inputname", "名前入力欄"},
                   {"datadesc", "データ解説文"}, {"charadesc", "キャラクター解説文"}, {"charaparam", "キャラクター見出し1"},
                   {"charaparam2", "キャラクター見出し2"}, {"characre", "キャラクター登録"}, {"button", "ボタン"},
                   {"combo", "コンボボックス"}, {"slider", "スライダ"}, {"spin", "スピナ"}, {"tree", "ツリー"},
                   {"list", "リスト"}, {"tab", "タブ"}, {"menu", "メニュー"}};
    m__fontfaces = {m_str_default};
    // Resource はフォント表の読み込みで見つけた名前を公開する。 これは wx.FontEnumerator
    // に最も近い、バックエンドに依存しない取得元である。 リソースが未
    // 接続の場合は、同梱の五つの IPA フォントを代替として維持する。
    for (const auto& [name, _] : ::cw::setting::Resource::s_facenames_init) {
        if (!name.empty() && name.front() != '@' &&
            std::find(m__fontfaces.begin(), m__fontfaces.end(), name) == m__fontfaces.end())
            m__fontfaces.push_back(name);
    }
    for (const auto& name : {String("IPAゴシック"), String("IPA Pゴシック"), String("IPA UIゴシック"),
                             String("IPA明朝"), String("IPA P明朝")}) {
        if (std::find(m__fontfaces.begin(), m__fontfaces.end(), name) == m__fontfaces.end())
            m__fontfaces.push_back(name);
    }
    if (const auto backend = ::cw::dialog::settings::get_host_services()) {
        for (const auto& name : backend->enumerate_font_faces()) {
            if (!name.empty() && name.front() != '@' &&
                std::find(m__fontfaces.begin(), m__fontfaces.end(), name) == m__fontfaces.end())
                m__fontfaces.push_back(name);
        }
    }
    std::sort(m__fontfaces.begin() + 1, m__fontfaces.end());
    m__fontface_array.clear();
    for (const auto& face : m__fontfaces) m__fontface_array.emplace_back(face);
    m__types_with_inherit = {"[指定しない]"};
    for (const auto& name : m__bases_order) m__types_with_inherit.push_back("[" + m_typenames[name] + "]");
    for (const auto& name : m__fontfaces) m__types_with_inherit.push_back(name);
    m__has_default_faces.clear();
    for (const auto& name : m__bases_order) m__has_default_faces.push_back(FontSettingPanel___init___has_defaultfont(name));
    m_box_example = "表示例";
    m_box_edit_example = "表示例のカスタマイズ";
    m_box_gene = "詳細";
    m_box_base = "基本フォント";
    m_box_type = "役割別フォント";
    m_st_example = {};
    m_tx_example = "%fontface%\\nAaあぁアァ亜宇";
    m_sc_example = static_cast<Integer>(24);
    m_cb_bordering_cardname = true;
    m_cb_decorationfont = false;
    m_cb_fontsmoothingmessage = false;
    m_cb_fontsmoothingcardname = false;
    m_cb_fontsmoothingstatusbar = true;
    if (arg_for_local) {
        m_cb_important = false;
        m_copybtn = arg_use_copybase;
        m_initbtn = true;
    }
    auto local = std::make_shared<::cw::setting::LocalSetting>();
    local->init();
    // update_example はコントロールの値を実体の Setting へ反映
    // する。初回更新前に Setting からコントロールを初期化し、 詳細・スキンパ
    // ネルを構築しただけで、保存済みの独自サンプルを既定値に置き換えないようにする。
    const std::optional<std::shared_ptr<::cw::setting::Setting>> live_setting =
        ::cw::g_cwpy ? std::optional<std::shared_ptr<::cw::setting::Setting>>(
            ::cw::g_cwpy->m_setting) : std::nullopt;
    load(live_setting, local);
    _do_layout();
    _bind();
}

void FontSettingPanel::load(std::optional<std::shared_ptr<::cw::setting::Setting>> arg_setting, std::shared_ptr<::cw::setting::LocalSetting> arg_local)
{
    if (!arg_local) {
        arg_local = std::make_shared<::cw::setting::LocalSetting>();
        arg_local->init();
    }
    if (value_bool(m__for_local)) m_cb_important = arg_local->m_important_font;
    if (arg_setting && *arg_setting) {
        m_tx_example = (*arg_setting)->m_fontexampleformat;
        // wx.SpinCtrl と同様、SetValue は 0 も含め宣言された 8～32 の範囲へ収める。
        m_sc_example = std::clamp<Integer>((*arg_setting)->m_fontexamplepixelsize, 8, 32);
    }
    m_cb_bordering_cardname = arg_local->m_bordering_cardname;
    m_cb_decorationfont = arg_local->m_decorationfont;
    m_cb_fontsmoothingmessage = arg_local->m_fontsmoothing_message;
    m_cb_fontsmoothingcardname = arg_local->m_fontsmoothing_cardname;
    m_cb_fontsmoothingstatusbar = arg_local->m_fontsmoothing_statusbar;
    m__basefont_rows = arg_local->m_basefont;
    for (const auto& base : m__bases_order) {
        if (!m__basefont_rows.contains(base)) m__basefont_rows[base] = {};
    }
    m__fonttype_rows.clear();
    for (const auto& type : m__types) {
        const auto it = arg_local->m_fonttypes_typed.find(type);
        if (it != arg_local->m_fonttypes_typed.end()) {
            m__fonttype_rows[type] = font_row_from_tuple(it->second);
        } else {
            // 古い設定ファイルには互換用のスカラーマップしかない場合
            // がある。 そのフォント名を維持し、残りの列は LocalSetting
            // の型付き既定値と同じ未指定値を使う。
            const auto scalar = arg_local->m_fonttypes.find(type);
            const auto face = scalar == arg_local->m_fonttypes.end()
                ? String{}
                : value_string(scalar->second);
            m__fonttype_rows[type] = {"", face, -1, false, false, false};
        }
    }
    m__msg_exfont_rows.clear();
    for (const auto& type : m__types) {
        if (!m_msg_exfonttypes.contains(type)) continue;
        const auto it = arg_local->m_msg_exfonts.find(type);
        m__msg_exfont_rows[type] = it == arg_local->m_msg_exfonts.end()
            ? FontSettingRow{"inherit", "", -1, false, false, false} : font_row_from_tuple(it->second);
    }
    m__base_cursor = m__bases_order.empty() ? -1 : 0;
    m__type_cursor = m__types.empty() ? -1 : 0;
    m__last_face = get_basefontface(m__bases_order.empty() ? String{} : m__bases_order.front());
    update_example();
    _update_enabled();
}

void FontSettingPanel::update_example()
{
    // Python と同様、サンプルのコントロールを編集すると実体の
    // Setting のこの二項目を更新する。 最終的な適用操作よ
    // り前でも、ダイアログから開いたスキン編集画面に同じ値を見せる。
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting) {
        ::cw::g_cwpy->m_setting->m_fontexampleformat = value_string(m_tx_example, "%fontface%\\nAaあぁアァ亜宇");
        ::cw::g_cwpy->m_setting->m_fontexamplepixelsize = value_integer(m_sc_example, 24);
    }
    const auto face = value_string(m__last_face);
    auto example = value_string(m_tx_example, "%fontface%\\nAaあぁアァ亜宇");
    m_st_example = ::cw::util::format_title(example, {{"fontface", face}}, true);
}

void FontSettingPanel::_update_rowcolour(::cw::lib::String arg_fonttype, ::cw::lib::Integer arg_row)
{
    (void)arg_row;
    // 継承状態は行モデルに保持し、backendの一覧再描画で表示を更新する。
    m_type = arg_fonttype == "inherit";
}

void FontSettingPanel::init_values(std::optional<std::shared_ptr<::cw::setting::Setting>> arg_setting, std::shared_ptr<::cw::setting::LocalSetting> arg_local)
{
    if (!arg_local) {
        arg_local = std::make_shared<::cw::setting::LocalSetting>();
        arg_local->init();
    }
    if (arg_setting && *arg_setting) {
        m_tx_example = value_string((*arg_setting)->m_fontexampleformat_init, "%fontface%\\nAaあぁアァ亜宇");
        m_sc_example = std::clamp<Integer>(value_integer((*arg_setting)->m_fontexamplepixelsize_init, 24), 8, 32);
    }
    auto defaults = std::make_shared<::cw::setting::LocalSetting>();
    defaults->init();
    // Python のスキン別の「既定値」と同様、フォント行と描画設定は戻
    // すが、 「このスキンのフォントを優先」のチェックは意図的に維持する。
    m_cb_bordering_cardname = value_bool(arg_local->m_bordering_cardname_init, defaults->m_bordering_cardname);
    m_cb_decorationfont = value_bool(arg_local->m_decorationfont_init, defaults->m_decorationfont);
    m_cb_fontsmoothingmessage = value_bool(arg_local->m_fontsmoothing_message_init, defaults->m_fontsmoothing_message);
    m_cb_fontsmoothingcardname = value_bool(arg_local->m_fontsmoothing_cardname_init, defaults->m_fontsmoothing_cardname);
    m_cb_fontsmoothingstatusbar = value_bool(arg_local->m_fontsmoothing_statusbar_init, defaults->m_fontsmoothing_statusbar);
    m__basefont_rows = arg_local->m_basefont_init_typed;
    m__fonttype_rows.clear();
    for (const auto& type : m__types) {
        // Python と同様、別に構築した全体の既定表ではなく、渡され
        // た LocalSetting のスナップショットを復元する。
        if (m_msg_exfonttypes.contains(type)) continue;
        const auto it = arg_local->m_fonttypes_init_typed.find(type);
        if (it != arg_local->m_fonttypes_init_typed.end())
            m__fonttype_rows[type] = font_row_from_tuple(it->second);
        else
            m__fonttype_rows[type] = {"", {}, -1, false, false, false};
    }
    m__msg_exfont_rows.clear();
    for (const auto& type : m_msg_exfonttypes) {
        const auto it = arg_local->m_msg_exfonts_init_typed.find(type);
        if (it != arg_local->m_msg_exfonts_init_typed.end())
            m__msg_exfont_rows[type] = font_row_from_tuple(it->second);
        else
            m__msg_exfont_rows[type] = {"inherit", {}, -1, false, false, false};
    }
    update_example();
    _update_enabled();
}

std::tuple<bool, bool, bool, bool> FontSettingPanel::apply_localsettings(std::shared_ptr<::cw::setting::LocalSetting> arg_local)
{
    if (!arg_local) return {};
    bool updatecardimg = false;
    bool updatemcardimg = false;
    bool updatemessage = false;
    bool flag_fontupdate = false;
    const bool important = value_bool(m_cb_important, arg_local->m_important_font);
    if (arg_local->m_important_font != important) {
        arg_local->m_important_font = important;
        flag_fontupdate = true;
    }
    const bool bordering = value_bool(m_cb_bordering_cardname, arg_local->m_bordering_cardname);
    if (arg_local->m_bordering_cardname != bordering) { arg_local->m_bordering_cardname = bordering; updatecardimg = true; updatemcardimg = true; }
    const bool decoration = value_bool(m_cb_decorationfont, arg_local->m_decorationfont);
    if (arg_local->m_decorationfont != decoration) { arg_local->m_decorationfont = decoration; updatemessage = true; }
    const bool smooth_message = value_bool(m_cb_fontsmoothingmessage, arg_local->m_fontsmoothing_message);
    if (arg_local->m_fontsmoothing_message != smooth_message) { arg_local->m_fontsmoothing_message = smooth_message; updatemessage = true; }
    const bool smooth_card = value_bool(m_cb_fontsmoothingcardname, arg_local->m_fontsmoothing_cardname);
    if (arg_local->m_fontsmoothing_cardname != smooth_card) { arg_local->m_fontsmoothing_cardname = smooth_card; flag_fontupdate = true; }
    const bool smooth_status = value_bool(m_cb_fontsmoothingstatusbar, arg_local->m_fontsmoothing_statusbar);
    if (arg_local->m_fontsmoothing_statusbar != smooth_status) { arg_local->m_fontsmoothing_statusbar = smooth_status; flag_fontupdate = true; }

    std::map<String, String> basefont;
    for (const auto& base : m__bases_order) {
        const auto it = m__basefont_rows.find(base);
        const auto& face = it == m__basefont_rows.end() ? String{} : it->second;
        basefont[base] = (face == m_str_default || face == "**フォントが見つかりません**") ? String{} : face;
    }
    if (basefont != arg_local->m_basefont) { arg_local->m_basefont = basefont; flag_fontupdate = true; }
    std::map<String, ::cw::setting::FontSettingValue> fonttypes;
    for (const auto& type : m__types) {
        // メッセージの文字種別ごとの上書きは msg_exfonts だけに保持
        // する。 この表には表示するが、Py の通常のフォント用途には含めない。
        if (m_msg_exfonttypes.contains(type)) continue;
        const auto it = m__fonttype_rows.find(type);
        if (it == m__fonttype_rows.end()) continue;
        fonttypes[type] = font_row_to_tuple(it->second);
    }
    if (fonttypes != arg_local->m_fonttypes_typed) {
        arg_local->m_fonttypes_typed = fonttypes;
        arg_local->m_fonttypes.clear();
        // 旧スカラー形式を使う呼び出し側のため、その複製も同期する。
        for (const auto& [name, row] : fonttypes)
            arg_local->m_fonttypes[name] = Value(std::get<0>(row).empty() ? std::get<1>(row) : std::get<0>(row));
        flag_fontupdate = true;
    }
    std::map<String, std::tuple<String, String, Integer, bool, bool, bool>> msg_exfonts;
    for (const auto& [name, row] : m__msg_exfont_rows) msg_exfonts[name] = font_row_to_tuple(row);
    if (msg_exfonts != arg_local->m_msg_exfonts) { arg_local->m_msg_exfonts = msg_exfonts; flag_fontupdate = true; }
    return {flag_fontupdate, updatecardimg, updatemcardimg, updatemessage};
}

void FontSettingPanel::_bind()
{
    // 共通Presentationがedit/combo/list通知を各On*へ配線する。
}

void FontSettingPanel::_select_base(::cw::lib::Integer arg_i)
{
    if (arg_i < 0 || static_cast<std::size_t>(arg_i) >= m__bases_order.size()) return;
    m__base_cursor = arg_i;
    m__last_face = get_basefontface(m__bases_order[static_cast<std::size_t>(arg_i)]);
    update_example();
}

void FontSettingPanel::OnExampleText(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    update_example();
}

void FontSettingPanel::OnInitExample(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    m_tx_example = "%fontface%\\nAaあぁアァ亜宇";
    m_sc_example = static_cast<Integer>(24);
    update_example();
}

void FontSettingPanel::OnCellChangeBase(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (arg_event && arg_event->id >= 0) m__base_cursor = arg_event->id;
    _select_base(m__base_cursor);
}

void FontSettingPanel::OnSelectFontBase(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (arg_event && arg_event->id >= 0) m__base_cursor = arg_event->id;
    _select_base(m__base_cursor);
}

void FontSettingPanel::OnEditorCreatedBase(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    // 共通表示側の選択処理が選択行を設定する。
}

::cw::lib::String FontSettingPanel::get_basefontface(::cw::lib::String arg_fonttype)
{
    const auto it = m__basefont_rows.find(arg_fonttype);
    auto face = it == m__basefont_rows.end() ? String{} : it->second;
    if (face == m_str_default || face == "**フォントが見つかりません**" || face.empty()) {
        const auto& defaults = ::cw::setting::Resource::s_fontnames_init;
        const auto def = defaults.find(arg_fonttype);
        if (def != defaults.end()) return def->second;
        // 単独の設定モデルを構築する時点では Resource が未初期
        // 化である。 その場合の Python と同じ代替値を維持する。
        static const std::map<String, String> fallback{{"gothic", "IPAゴシック"}, {"pgothic", "IPA Pゴシック"},
            {"uigothic", "IPA UIゴシック"}, {"mincho", "IPA明朝"}, {"pmincho", "IPA P明朝"}};
        const auto it_fallback = fallback.find(arg_fonttype);
        return it_fallback == fallback.end() ? String{} : it_fallback->second;
    }
    return face;
}

void FontSettingPanel::_select_type(::cw::lib::Integer arg_i)
{
    if (arg_i < 0 || static_cast<std::size_t>(arg_i) >= m__types.size()) return;
    m__type_cursor = arg_i;
    m__last_face = get_typefontface(m__types[static_cast<std::size_t>(arg_i)], true);
    update_example();
}

void FontSettingPanel::OnCellChangeType(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (arg_event && arg_event->id >= 0) m__type_cursor = arg_event->id;
    _select_type(m__type_cursor);
}

void FontSettingPanel::OnSelectFontType(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (arg_event && arg_event->id >= 0) m__type_cursor = arg_event->id;
    _select_type(m__type_cursor);
}

void FontSettingPanel::OnEditorCreatedType(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    // 役割別行モデルはbackendの一覧選択・再描画経路へ接続済み。
}

::cw::lib::String FontSettingPanel::get_typefontface(::cw::lib::String arg_fonttype, bool arg_refeditors)
{
    (void)arg_refeditors;
    const auto& rows = m_msg_exfonttypes.contains(arg_fonttype) ? m__msg_exfont_rows : m__fonttype_rows;
    const auto it = rows.find(arg_fonttype);
    if (it == rows.end()) return {};
    if (!it->second.m_fonttype.empty()) {
        if (it->second.m_fonttype == "inherit") return arg_fonttype == "message" ? get_basefontface("uigothic") : get_typefontface("message", false);
        return get_basefontface(it->second.m_fonttype);
    }
    return it->second.m_face == m_str_default ? get_basefontface("uigothic") : it->second.m_face;
}

void FontSettingPanel::_do_layout()
{
    // フォントタブのネイティブの編集欄・チェック・コンボ・一覧は、中央の配置処理で並べる。
}

void FontSettingPanel::OnImportant(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    _update_enabled();
}

void FontSettingPanel::_update_enabled()
{
    m__controls_enabled = !value_bool(m__for_local) || value_bool(m_cb_important);
}

void FontSettingPanel::OnInitValue(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    auto local = std::make_shared<::cw::setting::LocalSetting>();
    local->init();
    init_values(std::nullopt, local);
}

void FontSettingPanel::OnCopyBase(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (!m__get_localsettings_cb) return;
    const auto value = (*m__get_localsettings_cb)({}, {});
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage()); object && *object) {
        auto local = std::dynamic_pointer_cast<::cw::setting::LocalSetting>(*object);
        if (local) {
            local->init();
            local->m_important_font = true;
            load(std::nullopt, local);
        }
    }
}

void GeneralSettingPanel::initialize(std::shared_ptr<::cw::lib::Window> arg_parent)
{
    (void)arg_parent;
    m_box_tablet = "タブレットモード";
    m_box_gene = "詳細";
    m_box_messagelog = "メッセージログ(F5キーで表示)";
    m_box_skin = "スキン";
    m_box_expandmode = "拡大表示方式(F4キーで拡大)";
    m_box_party = "パーティ";
    m_box_ss = "スクリーンショット情報(画像上部に表示)";
    m_cb_show_tiles = false;
    m_cb_enabled_right_flick = false;
    m_cb_can_repeatlclick = false;
    m_cb_debug = ::cw::g_cwpy && ::cw::g_cwpy->is_debugmode();
    m_cb_nolevelup = false;
    m_ch_startupscene = Integer{0};
    m_ch_messagelog_type = Integer{1};
    m_sc_backlogmax = Integer{0};
    m_sc_initmoneyamount = Integer{0};
    m_cb_initmoneyisinitialcash = false;
    m_cb_autosavepartyrecord = false;
    m_cb_overwritepartyrecord = false;
    m__money_enabled = true;
    m__overwrite_enabled = false;
    m_tx_ssinfoformat = String{};
    m_tx_ssfnameformat = String{};
    m_tx_cardssfnameformat = String{};
    m_ch_ssinfocolor = Integer{0};
    m_tx_ssinfobackimage = String{};
    m_cb_sswithstatusbar = false;
    m_sstoolbar = true;
    m_ti_ssins = Integer{1};
    m_st_ssinfo_brackets = "[ ] 内は、各種情報がある場合のみ挿入されます";

    auto parent = std::make_shared<::cw::lib::Panel>();
    m_skin = std::make_shared<::cw::dialog::settings::SkinPanel>();
    m_skin->initialize(parent, true);
    m_expand = std::make_shared<::cw::dialog::settings::ExpandPanel>();
    m_expand->initialize(parent, true);

    // Pythonのメニュー項目ID→トークン対応。UIメニュー生成自体はSDL境界。
    const std::vector<std::pair<String, String>> tokens = {
        {"application", "アプリケーション名"}, {"version", "バージョン情報"},
        {"build", "ビルド情報"}, {"skin", "スキン名"}, {"yado", "拠点名"},
        {"party", "パーティ名"}, {"scenario", "シナリオ名"}, {"author", "作者名"},
        {"path", "シナリオのファイルパス"}, {"file", "シナリオのファイル名"},
        {"compatibility", "互換モード"}, {"date", "日付"}, {"time", "時刻"},
        {"year", "年"}, {"month", "月"}, {"day", "日"}, {"hour", "時"},
        {"minute", "分"}, {"second", "秒"}, {"millisecond", "ミリ秒"},
        {"\\%", "%"}, {"\\[", "["}, {"\\]", "]"}, {"\\\\", "\\"}
    };
    Integer menu_id = 2;
    for (const auto& [token, label] : tokens) {
        (void)label;
        m_ssdic[std::to_string(menu_id++)] = token;
    }
    m_ss_tx = {m_tx_ssinfoformat, m_tx_ssfnameformat, m_tx_cardssfnameformat};
    _ss_focus();
    _bind();
    _do_layout();
}

void GeneralSettingPanel::_bind()
{
    // 一般設定の変更通知は共通Presentationからモデルへ同期される。
}

void GeneralSettingPanel::load(std::shared_ptr<::cw::setting::Setting> arg_setting)
{
    if (!arg_setting) return;
    // Python と同様、読み込みでは現在のデバッグコントロールを意図的に変更しない。
    // 書き出された Setting を読み込んでも、保存されたデバッグモードを選択しない。
    m_cb_show_tiles = arg_setting->m_show_tiles;
    m_cb_enabled_right_flick = arg_setting->m_enabled_right_flick;
    m_cb_can_repeatlclick = arg_setting->m_can_repeatlclick;
    m_cb_nolevelup = arg_setting->m_no_levelup_in_debugmode;
    if (arg_setting->m_messagelog_type == ::cw::setting::g_LOG_SINGLE) m_ch_messagelog_type = Integer{0};
    else if (arg_setting->m_messagelog_type == ::cw::setting::g_LOG_COMPRESS) m_ch_messagelog_type = Integer{2};
    else m_ch_messagelog_type = Integer{1};
    m_ch_startupscene = arg_setting->m_startupscene == ::cw::setting::g_OPEN_LAST_BASE ? Integer{1} : Integer{0};
    m_sc_backlogmax = arg_setting->m_backlogmax;
    m_sc_initmoneyamount = arg_setting->m_initmoneyamount;
    m_cb_initmoneyisinitialcash = arg_setting->m_initmoneyisinitialcash;
    m__money_enabled = !value_bool(m_cb_initmoneyisinitialcash);
    m_cb_autosavepartyrecord = arg_setting->m_autosave_partyrecord;
    m_cb_overwritepartyrecord = arg_setting->m_overwrite_partyrecord;
    m__overwrite_enabled = arg_setting->m_autosave_partyrecord;
    m_tx_ssinfoformat = arg_setting->m_ssinfoformat;
    m_tx_ssfnameformat = arg_setting->m_ssfnameformat;
    m_tx_cardssfnameformat = arg_setting->m_cardssfnameformat;
    const auto* color = std::get_if<::cw::lib::Bytes>(&arg_setting->m_ssinfofontcolor.storage());
    m_ch_ssinfocolor = (color && color->size() >= 3 && (*color)[0] == 255 && (*color)[1] == 255 && (*color)[2] == 255)
        ? Integer{1} : Integer{0};
    m_tx_ssinfobackimage = arg_setting->m_ssinfobackimage;
    m_cb_sswithstatusbar = arg_setting->m_sswithstatusbar;
    if (m_expand) m_expand->load(arg_setting);
}

void GeneralSettingPanel::init_values(std::shared_ptr<::cw::setting::Setting> arg_setting)
{
    if (!arg_setting) return;
    m_cb_show_tiles = value_bool(arg_setting->m_show_tiles_init);
    m_cb_enabled_right_flick = value_bool(arg_setting->m_enabled_right_flick_init);
    m_cb_can_repeatlclick = value_bool(arg_setting->m_can_repeatlclick_init);
    m_cb_nolevelup = value_bool(arg_setting->m_no_levelup_in_debugmode_init);
    if (value_string(arg_setting->m_messagelog_type_init) == ::cw::setting::g_LOG_SINGLE) m_ch_messagelog_type = Integer{0};
    else if (value_string(arg_setting->m_messagelog_type_init) == ::cw::setting::g_LOG_COMPRESS) m_ch_messagelog_type = Integer{2};
    else m_ch_messagelog_type = Integer{1};
    const auto startup = value_string(arg_setting->m_startupscene_init);
    if (startup == ::cw::setting::g_OPEN_LAST_BASE) m_ch_startupscene = Integer{1};
    else if (startup == ::cw::setting::g_OPEN_TITLE) m_ch_startupscene = Integer{0};
    m_sc_backlogmax = value_integer(arg_setting->m_backlogmax_init);
    if (m_expand) {
        m_expand->m_ch_expanddrawing = Integer{0};
        const auto mode = value_string(arg_setting->m_expandmode_init);
        if (mode == "FullScreen") m_expand->m_cb_fullscreen = true;
        else {
            // None は有効な非拡大モードであり、数値の倍率ではない。
            if (mode != "None" && !mode.empty()) {
                try {
                    std::size_t used = 0;
                    const auto scale = std::stoll(mode, &used);
                    if (used == mode.size())
                        m_expand->m_ch_expanddrawing = std::clamp<Integer>(scale, 1,
                            expand_selection_max(m_expand->m__expand_max) + 1) - 1;
                } catch (const std::invalid_argument&) {
                } catch (const std::out_of_range&) {
                }
            }
            m_expand->m_cb_fullscreen = false;
        }
        m_expand->make_expandinfo();
        m_expand->m_cb_smoothexpand = value_bool(arg_setting->m_smoothexpand_init);
    }
    m_sc_initmoneyamount = value_integer(arg_setting->m_initmoneyamount_init);
    m_cb_initmoneyisinitialcash = value_bool(arg_setting->m_initmoneyisinitialcash_init);
    m__money_enabled = !value_bool(m_cb_initmoneyisinitialcash);
    m_cb_autosavepartyrecord = value_bool(arg_setting->m_autosave_partyrecord_init);
    m_cb_overwritepartyrecord = value_bool(arg_setting->m_overwrite_partyrecord_init);
    m__overwrite_enabled = value_bool(m_cb_autosavepartyrecord);
    m_tx_ssinfoformat = value_string(arg_setting->m_ssinfoformat_init);
    m_tx_ssfnameformat = value_string(arg_setting->m_ssfnameformat_init);
    m_tx_cardssfnameformat = value_string(arg_setting->m_cardssfnameformat_init);
    const auto* color = std::get_if<::cw::lib::Bytes>(&arg_setting->m_ssinfofontcolor_init.storage());
    m_ch_ssinfocolor = (color && color->size() >= 3 && (*color)[0] == 255 && (*color)[1] == 255 && (*color)[2] == 255)
        ? Integer{1} : Integer{0};
    m_tx_ssinfobackimage = value_string(arg_setting->m_ssinfobackimage_init);
    m_cb_sswithstatusbar = value_bool(arg_setting->m_sswithstatusbar_init);
}

void GeneralSettingPanel::OnSSTool(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (!arg_event || arg_event->id == value_integer(m_ti_ssins, 1)) {
        // 無効な通知と挿入メニュー自体のIDは、トークン挿入の対象外。
        return;
    }
    const auto found = m_ssdic.find(std::to_string(arg_event->id));
    if (found == m_ssdic.end()) return;
    const auto token = value_string(found->second);
    // この直接呼び出し用の互換モデルは入力先・キャレット状態を持たず、情
    // 報書式へ追記する。 m_ti_ssins は挿入メニューを表し、
    // フォーカス中の書式欄ではない。 実際の入力先・キャレットは共通テ
    // キストエディターが別に管理し、この直接 API では再現しない。
    m_tx_ssinfoformat = value_string(m_tx_ssinfoformat) + (token.rfind("\\", 0) == 0 ? token : "%" + token + "%");
}

void GeneralSettingPanel::OnSSFocus(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    _ss_focus();
}

void GeneralSettingPanel::_ss_focus()
{
    // wx.Window.FindFocus() 相当の状態は、SDL UI の接続時に更新する。
    m__ss_toolbar_enabled = value_has(m_tx_ssinfoformat) || value_has(m_tx_ssfnameformat) || value_has(m_tx_cardssfnameformat);
    m_sstoolbar = m__ss_toolbar_enabled;
}

void GeneralSettingPanel::OnAutoSavePartyRecord(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    m__overwrite_enabled = value_bool(m_cb_autosavepartyrecord);
}

void GeneralSettingPanel::OnInitMoneyIsInitialCash(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    m__money_enabled = !value_bool(m_cb_initmoneyisinitialcash);
}

void GeneralSettingPanel::_do_layout()
{
    // 一般タブの各controlは共通Presentationのlayoutで配置される。
}

void ScenarioSettingPanel::initialize(std::shared_ptr<::cw::lib::Window> arg_parent)
{
    (void)arg_parent;
    m_box_gene = "詳細";
    m_box_folderoftype = "シナリオフォルダ(スキンタイプ別)";
    m_box_application = "外部アプリ";
    m_st_editor = "エディタ";
    m_st_filer_dir = "ファイラー(フォルダ用)";
    m_st_filer_file = "ファイラー(ファイル用)";
    m_cb_selectscenariofromtype = false;
    m_cb_show_paperandtree = false;
    m_cb_write_playlog = false;
    m_cb_can_installscenariofromdrop = false;
    m_cb_delete_sourceafterinstalled = false;
    m_cb_open_lastscenario = false;
    m_grid_folderoftype = static_cast<Integer>(0);
    m_celleditor = false;
    m__folderoftype.clear();
    m__folder_cursor = -1;
    _select_changed_folderoftype();
}

void ScenarioSettingPanel::load(std::shared_ptr<::cw::setting::Setting> arg_setting)
{
    if (!arg_setting) return;
    m_cb_selectscenariofromtype = arg_setting->m_selectscenariofromtype;
    m_cb_show_paperandtree = arg_setting->m_show_paperandtree;
    m_cb_write_playlog = arg_setting->m_write_playlog;
    m_cb_can_installscenariofromdrop = arg_setting->m_can_installscenariofromdrop;
    m_cb_delete_sourceafterinstalled = arg_setting->m_delete_sourceafterinstalled;
    m_cb_open_lastscenario = arg_setting->m_open_lastscenario;
    m__folderoftype = arg_setting->m_folderoftype;
    // Python版と同じく末尾に編集用の空行を常設する。
    m__folderoftype.emplace_back(String{}, String{});
    m_grid_folderoftype = static_cast<Integer>(m__folderoftype.size());
    m__folder_cursor = m__folderoftype.empty() ? -1 : 0;
    m_tx_editor = arg_setting->m_editor;
    m_tx_filer_dir = arg_setting->m_filer_dir;
    m_tx_filer_file = arg_setting->m_filer_file;
    _select_changed_folderoftype();
}

void ScenarioSettingPanel::init_values(std::shared_ptr<::cw::setting::Setting> arg_setting)
{
    if (!arg_setting) return;
    m_tx_editor = value_string(arg_setting->m_editor_init);
    m_cb_selectscenariofromtype = value_bool(arg_setting->m_selectscenariofromtype_init);
    m_cb_show_paperandtree = value_bool(arg_setting->m_show_paperandtree_init);
    m_cb_write_playlog = value_bool(arg_setting->m_write_playlog_init);
    m_cb_can_installscenariofromdrop = value_bool(arg_setting->m_can_installscenariofromdrop_init);
    m_cb_delete_sourceafterinstalled = value_bool(arg_setting->m_delete_sourceafterinstalled_init);
    m_cb_open_lastscenario = value_bool(arg_setting->m_open_lastscenario_init);
    m_tx_filer_dir = value_string(arg_setting->m_filer_dir_init);
    m_tx_filer_file = value_string(arg_setting->m_filer_file_init);
    // folderoftypeはPython版でも初期値復元時に表を再ロードしない。
    _select_changed_folderoftype();
}

void ScenarioSettingPanel::OnGirdSelectCell(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    // m_all_skintypes は、フォルダー一覧の選択候補としてバックエンドへ渡す。
    m_celleditor = true;
    _select_changed_folderoftype();
}

void ScenarioSettingPanel::_select_changed_folderoftype()
{
    const auto count = static_cast<Integer>(m__folderoftype.size());
    if (m__folder_cursor < -1 || m__folder_cursor >= count) m__folder_cursor = -1;
    m__ref_folder_enabled = m__folder_cursor != -1;
    m__remove_folder_enabled = m__folder_cursor != -1 && m__folder_cursor < count - 1;
    m__up_folder_enabled = m__folder_cursor != -1 && m__folder_cursor >= 1 && m__folder_cursor < count - 1;
    m__down_folder_enabled = m__folder_cursor != -1 && m__folder_cursor + 2 < count;
    set_window_enabled(value_window(m_btn_reffolder), m__ref_folder_enabled);
    set_window_enabled(value_window(m_btn_removefolder), m__remove_folder_enabled);
    set_window_enabled(value_window(m_btn_upfolder), m__up_folder_enabled);
    set_window_enabled(value_window(m_btn_downfolder), m__down_folder_enabled);
}

void ScenarioSettingPanel::_bind()
{
    // フォルダー一覧と操作ボタンは、共通 Presentation で各 On* に接続する。
}

void ScenarioSettingPanel::_do_layout()
{
    // scenarioタブのcheck/edit/list/buttonは中央layoutで配置される。
}

void ScenarioSettingPanel::OnGridCellChange(std::shared_ptr<::cw::lib::Window> arg_event)
{
    // backendが選択cursorをm__folder_cursorへ設定してから純粋処理を呼ぶ。
    (void)arg_event;
}

void ScenarioSettingPanel::_grid_cell_change(::cw::lib::Integer arg_row, ::cw::lib::Integer arg_col)
{
    if (arg_col != 0 || arg_row < 0 || static_cast<std::size_t>(arg_row) >= m__folderoftype.size()) return;
    if (arg_row + 1 != static_cast<Integer>(m__folderoftype.size())) return;
    if (std::get<0>(m__folderoftype[static_cast<std::size_t>(arg_row)]).empty()) return;
    m__folderoftype.emplace_back(String{}, String{});
    m_grid_folderoftype = static_cast<Integer>(m__folderoftype.size());
}

void ScenarioSettingPanel::OnRefFolderBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m_settings_parent) m_settings_parent->request_document(SettingsDocumentAction::ScenarioFolder);
}

void ScenarioSettingPanel::apply_folder_choice(Integer row, String selected)
{
    if (row < 0 || row >= static_cast<Integer>(m__folderoftype.size()) || selected.empty()) return;
    auto path = normalize_setting_path(selected);
    if (path.empty()) return;
    std::get<1>(m__folderoftype[static_cast<std::size_t>(row)]) = path;
    if (std::get<0>(m__folderoftype[static_cast<std::size_t>(row)]).empty()) {
        std::get<0>(m__folderoftype[static_cast<std::size_t>(row)]) = "MedievalFantasy";
        _grid_cell_change(row, 0);
    }
    _select_changed_folderoftype();
    if (m_settings_parent) m_settings_parent->applied();
}

void ScenarioSettingPanel::OnRemoveFolderBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    const auto row = m__folder_cursor;
    if (row < 0 || row + 1 >= static_cast<Integer>(m__folderoftype.size())) return;
    m__folderoftype.erase(m__folderoftype.begin() + row);
    m_grid_folderoftype = static_cast<Integer>(m__folderoftype.size());
    if (m__folder_cursor >= static_cast<Integer>(m__folderoftype.size()))
        m__folder_cursor = static_cast<Integer>(m__folderoftype.size()) - 1;
    _select_changed_folderoftype();
}

void ScenarioSettingPanel::OnUpFolderBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    const auto row = m__folder_cursor;
    if (row < 1 || row + 1 >= static_cast<Integer>(m__folderoftype.size())) return;
    std::swap(m__folderoftype[static_cast<std::size_t>(row)], m__folderoftype[static_cast<std::size_t>(row - 1)]);
    m__folder_cursor = row - 1;
    _select_changed_folderoftype();
}

void ScenarioSettingPanel::OnDownFolderBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    const auto row = m__folder_cursor;
    if (row < 0 || row + 2 >= static_cast<Integer>(m__folderoftype.size())) return;
    std::swap(m__folderoftype[static_cast<std::size_t>(row)], m__folderoftype[static_cast<std::size_t>(row + 1)]);
    m__folder_cursor = row + 1;
    _select_changed_folderoftype();
}

void ScenarioSettingPanel::OnConstructDBBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    ::cw::dialog::editscenariodb::ScenarioDbPaths directories;
    for (const auto& [skintype, configured_path] : m__folderoftype) {
        if (configured_path.empty()) continue;
        auto found = std::find_if(directories.begin(), directories.end(), [&](const auto& item) {
            return item.first == skintype;
        });
        if (found == directories.end()) {
            directories.emplace_back(skintype, std::vector<String>{});
            found = std::prev(directories.end());
        }
        const auto target = ::cw::util::get_linktarget(configured_path);
        if (::cw::util::path_is_directory(target) &&
            std::find(found->second.begin(), found->second.end(), target) == found->second.end())
            found->second.push_back(target);
    }
    if (::cw::util::path_is_directory("Scenario")) {
        const auto current_type = ::cw::g_cwpy && ::cw::g_cwpy->m_setting
            ? ::cw::g_cwpy->m_setting->m_skintype : String{};
        auto found = std::find_if(directories.begin(), directories.end(), [&](const auto& item) {
            return item.first == current_type;
        });
        if (found == directories.end()) directories.emplace_back(current_type, std::vector<String>{"Scenario"});
        else if (directories.empty()) directories.emplace_back(String{}, std::vector<String>{"Scenario"});
    }
    m_scenariodb_dialog.reset();
    m_scenariodb_dialog = std::make_shared<::cw::dialog::editscenariodb::ConstructScenarioDB>();
    auto parent = std::make_shared<::cw::lib::Window>();
    parent->native_handle = m_settings_parent ? m_settings_parent->m_native_handle : 0;
    m_scenariodb_dialog->initialize(parent, std::move(directories));
    (void)::cw::dialog::show_common_modal_presentation(m_scenariodb_dialog);
    m_scenariodb_dialog.reset();
}

namespace {
using PresentationField = std::tuple<String, std::function<String()>, std::function<void()>>;
constexpr const char* settings_debug_label="デバッグモード(Ctrl+Dでも切替可)";
constexpr const char* settings_no_levelup_label="デバッグ中はレベル上昇を抑止する";
constexpr int audio_add = 2000, audio_remove = 2001, audio_up = 2002, audio_down = 2003;
constexpr int audio_slider_first = 2010, audio_play_bgm = 2020, audio_play_sound = 2021;
constexpr int audio_scrollbar = 2022;
constexpr int audio_use_first = 3000, audio_volume_first = 4000;
constexpr Integer audio_table_x = 376, audio_table_y = 76, audio_row_height = 24;
constexpr int scenario_ref=4700, scenario_remove=4701, scenario_up=4702, scenario_down=4703;
constexpr int scenario_db=4704, scenario_scrollbar=4705, scenario_types=4706;
constexpr int scenario_check_first=4710, scenario_browse_first=4720;
constexpr int scenario_application_first=4800, scenario_cell_first=5000;
constexpr int scenario_type_first=1000000, scenario_popup_dismiss=999999;
constexpr Integer scenario_table_y=266, scenario_row_height=24, scenario_visible_rows=5;
constexpr int skin_choice=6000, skin_install=6001, skin_convert=6002, skin_edit=6003;
constexpr int skin_copy=6004, skin_delete=6005, skin_show_all=6006, skin_scrollbar=6007, skin_description=6008;
constexpr int general_edit_first=6100, general_startup=6200, general_log=6201, general_color=6202;
constexpr int general_expand=6203, general_display=6204, general_background=6205, general_tokens=6206;
constexpr int general_fullscreen=6207, general_smoothing=6208;
constexpr int settings_choice_first=2000000, settings_choice_dismiss=1999999;
constexpr int drawing_transition=8000, drawing_background=8001, drawing_file=8002, drawing_browse=8003;
constexpr int drawing_alpha_first=8100, drawing_spin_first=8120, drawing_colour_first=8200;
constexpr int drawing_speed_first=8300, drawing_defaults=8400, drawing_copy=8401;
constexpr int ui_status_time=9000, ui_confirm_dump=9001, ui_confirm_save=9002;
constexpr int ui_radius=9003, ui_radius_up=9004, ui_radius_down=9005, ui_scrollbar=9010;
constexpr int ui_skill_limit=9011, ui_personal_limit=9012;
constexpr int ui_skill_up=9013, ui_skill_down=9014, ui_personal_up=9015, ui_personal_down=9016;
constexpr int ui_beast_limit=9017, ui_beast_up=9018, ui_beast_down=9019;
// CardWirthPy_C_All 専用。詳細の隣にあるタブ 6「ステータスダイアログ」で、
// 右側のショートカットパネルの選択肢を設定する。 CardWirthPy にはない機能である。
constexpr Integer status_dialog_tab=6;
constexpr int ui_shortcut_first=9100;
constexpr int ui_shortcut_last=ui_shortcut_first+static_cast<int>(::cw::setting::kAllStatusShortcutSlots)-1;
// 状態編集を登録モード（カスタム状態）で開く。
constexpr int ui_custom_status=9120;
Integer settings_tab_count() { return ::cw::dialog::is_all_mode() ? 7 : 6; }
bool ui_numeric(int id) {
    return id==ui_radius || id==ui_skill_limit || id==ui_personal_limit || id==ui_beast_limit;
}
constexpr ::cw::lib::Rect ui_viewport{12,48,680,464};
struct UIOption { const char* label; Value UISettingPanel::*member; };
// 既存のコールバックを維持するため ID は固定し、ラベルとグループ順は Python に合わせる。
constexpr std::array<UIOption,35> ui_options{{
    {"全てのシステムカードを高速表示する",&UISettingPanel::m_cb_allquickdeal},
    {"キャンプモードへ高速で切り替える",&UISettingPanel::m_cb_quickdeal},
    {"連打状態の時、カードなどの選択を自動的に決定する",&UISettingPanel::m_cb_autoenter_on_sprite},
    {"所持金が増減した時に所持金欄を点滅させる",&UISettingPanel::m_cb_blink_partymoney},
    {"通知のあるステータスボタンを点滅させる",&UISettingPanel::m_cb_blink_statusbutton},
    {"カード選択ダイアログの背景クリックで左右移動を行う",&UISettingPanel::m_cb_can_clicksidesofcardcontrol},
    {"マウスのホイールでメッセージ送りを行う",&UISettingPanel::m_cb_can_forwardmessage_with_wheel},
    {"アニメーションをスキップ可能にする",&UISettingPanel::m_cb_can_skipanimation},
    {"空白時間をスキップ可能にする",&UISettingPanel::m_cb_can_skipwait},
    {"マウスのホイールで空白時間とアニメーションをスキップする",&UISettingPanel::m_cb_can_skipwait_with_wheel},
    {"保存せずに終了しようとしたら警告する",&UISettingPanel::m_cb_cautionbeforesaving},
    {"カード使用時に確認ダイアログを表示",&UISettingPanel::m_cb_confirmbeforeusingcard},
    {"召喚獣カードの拡大率を大きくする",&UISettingPanel::m_cb_enlarge_beastcardzoomingratio},
    {"不可能な行動を選択した時に警告を表示",&UISettingPanel::m_cb_noticeimpossibleaction},
    {"プレミアカードの売却や破棄を禁止する",&UISettingPanel::m_cb_protect_premiercard},
    {"スターつきのカードの売却や破棄を禁止する",&UISettingPanel::m_cb_protect_staredcard},
    {"レベル調節で手放したカードを自動的に戻す",&UISettingPanel::m_cb_revertcardpocket},
    {"絞り込み等の表示切替ボタンを表示する(非表示時はCtrl+Fで切替可能)",&UISettingPanel::m_cb_show_addctrlbtn},
    {"最初から詳細モードで設定を行う",&UISettingPanel::m_cb_show_advancedsettings},
    {"ステータスバーのボタンの解説を表示する",&UISettingPanel::m_cb_show_btndesc},
    {"カード置場と荷物袋でカードの種類を表示する",&UISettingPanel::m_cb_show_cardkind},
    {"キャラクター情報に次のレベルアップまでの割合を表示する",&UISettingPanel::m_cb_show_experiencebar},
    {"荷物袋にあるカードのキャラクターごとの私有を許可する",&UISettingPanel::m_cb_show_personal_cards},
    {"カードの希少度をアイコンで表示する",&UISettingPanel::m_cb_show_premiumicon},
    {"戦闘行動を全員分表示する",&UISettingPanel::m_cb_showallselectedcards},
    {"新規登録ダイアログに自動ボタンを表示する",&UISettingPanel::m_cb_showautobuttoninentrydialog},
    {"荷物袋のカードを一時的に取り出して使えるようにする",&UISettingPanel::m_cb_showbackpackcard},
    {"荷物袋カードを最後に配置する",&UISettingPanel::m_cb_showbackpackcardatend},
    {"マウスホイールを上に回すとログを表示",&UISettingPanel::m_cb_showlogwithwheelup},
    {"バトルで自動的に行動を開始できるようにする",&UISettingPanel::m_cb_showroundautostartbutton},
    {"セーブ完了時に確認ダイアログを表示",&UISettingPanel::m_cb_showsavedmessage},
    {"イベント中にステータスバーの色を変える",&UISettingPanel::m_cb_statusbarmask},
    {"カードの使用前に空白時間を入れる",&UISettingPanel::m_cb_wait_usecard},
    {"この宿のPCの技能カード・私有袋の上限を変更する",&UISettingPanel::m_cb_pc_card_limits},
    {"PCクリック時のモーションを省略する",&UISettingPanel::m_cb_skip_pc_click_motion}
}};
std::shared_ptr<UISettingPanel> settings_ui(const SettingsDialog& dialog) {
    const auto panel=detail_panel(dialog.m_panel); return panel ? panel->m_pane_ui : nullptr;
}
::cw::lib::Rect settings_ui_viewport(const SettingsDialog& dialog) {
    auto viewport=ui_viewport;
    // 詳細タブ自体が縦スクロールする文書である。上位の詳細設
    // 定ダイアログ内では固定の本体領域だけを使う。 以前の内
    // 部表示高 464px を残すと、外側の二つ目のスクロ
    // ールバーで切り取られ、ダイアログ状の枠が二重になる。
    if(detail_panel(dialog.m_panel) && !dialog.local_panel_only()) viewport.height=320;
    return viewport;
}
Value* ui_value(const SettingsDialog& dialog,int id) {
    if(const auto pane=settings_ui(dialog)) {
        switch(id) {
        case ui_status_time:return &pane->m_ch_show_statustime;
        case ui_confirm_dump:return &pane->m_ch_confirm_dumpcard;
        case ui_confirm_save:return &pane->m_ch_confirm_beforesaving;
        case ui_radius:return &pane->m_sc_radius_notdetectmovement;
        case ui_skill_limit:return &pane->m_sc_pc_skill_limit;
        case ui_personal_limit:return &pane->m_sc_pc_personal_limit;
        case ui_beast_limit:return &pane->m_sc_pc_beast_limit;
        }
        if(id>=ui_shortcut_first && id<=ui_shortcut_last)
            return &pane->m_ch_all_shortcuts[static_cast<std::size_t>(id-ui_shortcut_first)];
    }
    return nullptr;
}
void ui_focus_control(SettingsDialog& dialog,int id) {
    if(dialog.m_ui_control_rects.empty()) (void)dialog.dialog_presentation();
    dialog.m_ui_focus=id;
    const auto viewport=settings_ui_viewport(dialog);
    if(const auto found=dialog.m_ui_control_rects.find(id);found!=dialog.m_ui_control_rects.end()) {
        const auto& r=found->second;
        if(r.y<dialog.m_ui_scroll) dialog.m_ui_scroll=r.y;
        if(r.y+r.height>dialog.m_ui_scroll+viewport.height) dialog.m_ui_scroll=r.y+r.height-viewport.height;
        dialog.m_ui_scroll=std::clamp<Integer>(dialog.m_ui_scroll,0,std::max<Integer>(0,dialog.m_ui_content_height-viewport.height));
    }
    dialog.request_dialog_presentation_refresh();
}

std::shared_ptr<DrawingSettingPanel> settings_drawing(const SettingsDialog& dialog) {
    const auto panel=detail_panel(dialog.m_panel);
    return panel ? panel->m_pane_draw : nullptr;
}
std::shared_ptr<SpeedPanel> settings_speed(const SettingsDialog& dialog) {
    if(const auto panel=simple_panel(dialog.m_panel)) return panel->m_speed;
    if(const auto pane=settings_drawing(dialog)) return pane->m_speed;
    return {};
}
Value* drawing_value(const SettingsDialog& dialog,int id) {
    if(const auto speed=settings_speed(dialog)) {
        switch(id) {
        case drawing_transition: return &speed->m_ch_tran;
        case drawing_speed_first: return &speed->m_sl_deal;
        case drawing_speed_first+1: return &speed->m_sl_deal_battle;
        case drawing_speed_first+2: return &speed->m_sl_msgs;
        case drawing_speed_first+3: return &speed->m_sl_tran;
        }
    }
    if(const auto pane=settings_drawing(dialog)) {
        switch(id) {
        case drawing_background: return &pane->m_ch_fscrbacktype;
        case drawing_file: return &pane->m_tx_fscrbackfile;
        case drawing_alpha_first: return &pane->m_sc_mwin;
        case drawing_alpha_first+1: return &pane->m_sc_mframe;
        case drawing_alpha_first+2: return &pane->m_sc_blcurtain;
        case drawing_alpha_first+3: return &pane->m_sc_curtain;
        case drawing_alpha_first+4: return &pane->m_sc_allengineframe;
        case drawing_colour_first: return &pane->m_cs_mwin;
        case drawing_colour_first+1: return &pane->m_cs_blwin;
        case drawing_colour_first+2: return &pane->m_cs_mframe;
        case drawing_colour_first+3: return &pane->m_cs_blframe;
        case drawing_colour_first+4: return &pane->m_cs_blcurtain;
        case drawing_colour_first+5: return &pane->m_cs_curtain;
        case drawing_colour_first+6: return &pane->m_cs_allengineframe;
        }
    }
    return nullptr;
}
bool drawing_enabled(const SettingsDialog& dialog,int id) {
    if(const auto pane=settings_drawing(dialog); pane && !pane->m__controls_enabled) return false;
    if(id==drawing_file || id==drawing_browse) {
        const auto pane=settings_drawing(dialog);
        return pane && pane->m__fullscreen_file_enabled;
    }
    if(id==drawing_speed_first+1) {
        const auto speed=settings_speed(dialog);
        return speed && !value_bool(speed->m_cb_use_battlespeed);
    }
    return true;
}
bool drawing_editable(int id) {
    return id==drawing_file || (id>=drawing_alpha_first && id<drawing_alpha_first+5);
}

std::shared_ptr<SkinPanel> settings_skin(const SettingsDialog& dialog) {
    if (const auto panel=simple_panel(dialog.m_panel)) return panel->m_skin;
    if (const auto panel=detail_panel(dialog.m_panel); panel && panel->m_pane_gene) return panel->m_pane_gene->m_skin;
    return {};
}

Value* general_edit_value(const SettingsDialog& dialog, int field) {
    const auto panel=detail_panel(dialog.m_panel);
    if (!panel || !panel->m_pane_gene) return nullptr;
    auto& pane=*panel->m_pane_gene;
    switch(field-general_edit_first) {
    case 0: return &pane.m_sc_backlogmax;
    case 1: return &pane.m_sc_initmoneyamount;
    case 2: return &pane.m_tx_ssinfoformat;
    case 3: return &pane.m_tx_ssfnameformat;
    case 4: return &pane.m_tx_cardssfnameformat;
    case 5: return &pane.m_tx_ssinfobackimage;
    default: return nullptr;
    }
}

std::shared_ptr<ExpandPanel> settings_expand(const SettingsDialog& dialog) {
    if (const auto panel=simple_panel(dialog.m_panel)) return panel->m_expand;
    if (const auto panel=detail_panel(dialog.m_panel); panel && panel->m_pane_gene) return panel->m_pane_gene->m_expand;
    return {};
}

std::vector<String> settings_choices(const SettingsDialog& dialog, int kind) {
    if(kind==ui_status_time) return {"イベント中でなければ表示","常に表示","表示しない"};
    if(kind==ui_confirm_dump) return {"常に表示","「送り先」の使用時のみ表示","表示しない"};
    if(kind==ui_confirm_save) return {"常に表示","拠点にいる時だけ表示","表示しない"};
    // 順序は ::cw::setting::AllStatusShortcut に合わせる。
    if(kind>=ui_shortcut_first && kind<=ui_shortcut_last)
        return {"なし","セーブ","ロード","スクリーンショット","解説ダイアログ","手札のスクリーンショット",
                "全回復(デバッグ機能)","カスタム状態(デバッグ機能)"};
    if(kind==drawing_transition) {
        if(const auto speed=settings_speed(dialog); speed && !speed->m_choices_tran.empty())
            return speed->m_choices_tran;
        // これらは実行時データではなく、wx/Python の固定の選択
        // 肢である。 設定モデルの構築中も共通 Presentation
        // を利用可能にし、早い初回描画でも空のポップアップを作らない。
        return {"アニメーションなし", "短冊(スレッド)式",
                "ドット置換(シェーブ)式", "色置換(フェード)式"};
    }
    if(kind==drawing_background) return {"<背景なし>","<ファイルから選択>","ダイアログの壁紙","スキンのロゴ"};
    if (const auto panel=detail_panel(dialog.m_panel); panel && panel->m_pane_font) {
        const auto& pane=*panel->m_pane_font;
        if(kind>=7000 && kind<7000+static_cast<int>(pane.m__bases_order.size())) {
            auto choices=pane.m__fontfaces;
            const auto row=kind-7000;
            if(row<static_cast<int>(pane.m__has_default_faces.size()) && !pane.m__has_default_faces[row])
                std::erase(choices,pane.m_str_default);
            return choices;
        }
        if(kind>=7100 && kind<7100+static_cast<int>(pane.m__types.size())*5 && (kind-7100)%5==0) {
            std::vector<String> choices;
            if(pane.m_msg_exfonttypes.contains(pane.m__types[(kind-7100)/5])) choices.push_back("[指定しない]");
            for(const auto& base:pane.m__bases_order) choices.push_back("["+pane.m_typenames.at(base)+"]");
            for(const auto& face:pane.m__fontfaces) if(face!=pane.m_str_default) choices.push_back(face);
            return choices;
        }
    }
    if (kind==skin_choice) {
        std::vector<String> result;
        if (const auto skin=settings_skin(dialog)) for (const auto& name:skin->m_skins) result.push_back(value_string(name));
        return result;
    }
    if (kind==general_startup) return {"タイトル画面を開く","最後に選択した拠点を開く"};
    if (kind==general_log) return {"1件ずつ表示","並べて表示","高さを圧縮"};
    if (kind==general_color) return {"黒文字","白文字"};
    if (kind==general_expand) {
        std::vector<String> result;
        if (const auto expand=settings_expand(dialog))
            for(Integer i=0;i<=expand_selection_max(expand->m__expand_max);++i) result.push_back(std::to_string(1LL<<i)+"倍");
        return result;
    }
    if (kind==general_tokens) return {"application","version","build","skin","yado","party","scenario","author",
        "path","file","compatibility","date","time","year","month","day","hour","minute","second","millisecond",
        "\\%","\\[","\\]","\\\\"};
    return {};
}

Value* settings_choice_value(const SettingsDialog& dialog,int kind) {
    if(kind>=ui_status_time && kind<=ui_confirm_save) return ui_value(dialog,kind);
    if(kind>=ui_shortcut_first && kind<=ui_shortcut_last) return ui_value(dialog,kind);
    if(kind==drawing_transition || kind==drawing_background) return drawing_value(dialog,kind);
    if (kind==skin_choice) { const auto skin=settings_skin(dialog); return skin ? &skin->m_ch_skin : nullptr; }
    if (kind==general_expand) { const auto expand=settings_expand(dialog); return expand ? &expand->m_ch_expanddrawing : nullptr; }
    const auto panel=detail_panel(dialog.m_panel);
    if (!panel || !panel->m_pane_gene) return nullptr;
    if (kind==general_startup) return &panel->m_pane_gene->m_ch_startupscene;
    if (kind==general_log) return &panel->m_pane_gene->m_ch_messagelog_type;
    if (kind==general_color) return &panel->m_pane_gene->m_ch_ssinfocolor;
    return nullptr;
}

constexpr int font_base_first=7000, font_type_first=7100;
constexpr int font_example=7400, font_example_size=7401, font_example_reset=7402;
constexpr int font_scrollbar=7403, font_defaults=7404, font_copy=7405;
constexpr Integer font_visible_rows=7, font_row_height=20, font_rows_y=350;
std::shared_ptr<FontSettingPanel> settings_font(const SettingsDialog& dialog) {
    const auto panel=detail_panel(dialog.m_panel);
    return panel ? panel->m_pane_font : nullptr;
}
FontSettingRow* font_cell(FontSettingPanel& pane,int id) {
    const int row=(id-font_type_first)/5;
    if(id<font_type_first || row>=static_cast<int>(pane.m__types.size())) return nullptr;
    const auto& type=pane.m__types[row];
    auto& rows=pane.m_msg_exfonttypes.contains(type)?pane.m__msg_exfont_rows:pane.m__fonttype_rows;
    const auto it=rows.find(type);
    return it==rows.end()?nullptr:&it->second;
}
String font_face_label(FontSettingPanel& pane,int id) {
    if(id>=font_base_first && id<font_base_first+static_cast<int>(pane.m__bases_order.size())) {
        const auto face=pane.m__basefont_rows[pane.m__bases_order[id-font_base_first]];
        return face.empty()?pane.m_str_default:face;
    }
    if(const auto row=font_cell(pane,id)) {
        if(row->m_fonttype.empty()) return row->m_face;
        return "["+pane.m_typenames[row->m_fonttype]+"]";
    }
    return {};
}
bool font_numeric_field(FontSettingPanel& pane,int id) {
    if(id==font_example_size) return true;
    const auto row=font_cell(pane,id);
    if(!row || (id-font_type_first)%5!=1) return false;
    // Python と同様、編集用コントロールは編集中のサイズではなく、その用途の初期タプルに基づいて設定する。
    static const auto defaults=[] {
        auto local=std::make_shared<::cw::setting::LocalSetting>();
        local->init();
        return local;
    }();
    const auto& type=pane.m__types[(id-font_type_first)/5];
    const auto& table=pane.m_msg_exfonttypes.contains(type)
        ? defaults->m_msg_exfonts_init_typed : defaults->m_fonttypes_init_typed;
    const auto found=table.find(type);
    return found!=table.end() && std::get<2>(found->second)>0;
}
void font_select(SettingsDialog& dialog,FontSettingPanel& pane,int id) {
    dialog.m_font_focus=id;
    if(id>=font_base_first && id<font_base_first+static_cast<int>(pane.m__bases_order.size()))
        pane._select_base(id-font_base_first);
    else if(font_cell(pane,id)) {
        const auto row=(id-font_type_first)/5;
        pane._select_type(row);
        if(row<dialog.m_font_scroll) dialog.m_font_scroll=row;
        if(row>=dialog.m_font_scroll+font_visible_rows) dialog.m_font_scroll=row-font_visible_rows+1;
    }
}
void font_choose(SettingsDialog& dialog,int id,const String& face) {
    const auto pane=settings_font(dialog);
    if(!pane || !pane->m__controls_enabled) return;
    if(id>=font_base_first && id<font_base_first+static_cast<int>(pane->m__bases_order.size())) {
        pane->m__basefont_rows[pane->m__bases_order[id-font_base_first]]=face==pane->m_str_default?String{}:face;
    } else if(auto row=font_cell(*pane,id); row && (id-font_type_first)%5==0) {
        row->m_fonttype.clear(); row->m_face=face;
        for(const auto& base:pane->m__bases_order) if(face=="["+pane->m_typenames[base]+"]") {
            row->m_fonttype=base; row->m_face.clear(); break;
        }
        if(face=="[指定しない]" && pane->m_msg_exfonttypes.contains(pane->m__types[(id-font_type_first)/5])) {
            row->m_fonttype="inherit"; row->m_face.clear();
        }
    } else return;
    font_select(dialog,*pane,id);
    dialog.applied();
}

Value* scenario_application(ScenarioSettingPanel& pane, int field) {
    if (field==scenario_application_first) return &pane.m_tx_editor;
    if (field==scenario_application_first+1) return &pane.m_tx_filer_dir;
    if (field==scenario_application_first+2) return &pane.m_tx_filer_file;
    return nullptr;
}

std::vector<String> scenario_types_for(const SettingsDialog& dialog) {
    std::vector<String> types;
    if (const auto panel=detail_panel(dialog.m_panel); panel && panel->m_pane_gene && panel->m_pane_gene->m_skin) {
        for (const auto& value : panel->m_pane_gene->m_skin->m_all_skintypes) {
            const auto type=value_string(value);
            types.push_back(type);
        }
    }
    std::sort(types.begin(),types.end());
    types.erase(std::unique(types.begin(),types.end()),types.end());
    std::stable_sort(types.begin(), types.end(), [](const String& lhs, const String& rhs) {
        return ::cw::util::logical_compare(lhs, rhs) < 0;
    });
    return types;
}

Value* scenario_check(ScenarioSettingPanel& pane, int index) {
    switch(index) {
    case 0: return &pane.m_cb_selectscenariofromtype;
    case 1: return &pane.m_cb_open_lastscenario;
    case 2: return &pane.m_cb_show_paperandtree;
    case 3: return &pane.m_cb_write_playlog;
    case 4: return &pane.m_cb_can_installscenariofromdrop;
    case 5: return &pane.m_cb_delete_sourceafterinstalled;
    default: return nullptr;
    }
}
constexpr Integer audio_visible_rows = 6;

Value* audio_volume(AudioSettingPanel& pane, int index)
{
    switch (index) {
    case 0: return &pane.m_sl_master;
    case 1: return &pane.m_sl_music;
    case 2: return &pane.m_sl_midi;
    case 3: return &pane.m_sl_sound;
    default: return nullptr;
    }
}

void append_bool_field(std::vector<PresentationField>& fields, String label, Value* value,
                       std::function<void()> changed = {})
{
    fields.emplace_back(std::move(label),
        [value] { return value_bool(value ? *value : Value{}, false) ? "ON" : "OFF"; },
        [value, changed] {
            if (value) *value = !value_bool(*value, false);
            if (changed) changed();
        });
}

void append_window_bool_field(std::vector<PresentationField>& fields, String label,
                              const std::shared_ptr<Window>& value)
{
    fields.emplace_back(std::move(label),
        [value] { return value && value->id != 0 ? "ON" : "OFF"; },
        [value] { if (value) value->id = value->id == 0 ? 1 : 0; });
}

void append_integer_field(std::vector<PresentationField>& fields, String label,
                          Value* value, Integer minimum, Integer maximum)
{
    fields.emplace_back(std::move(label),
        [value] { return std::to_string(value_integer(value ? *value : Value{}, 0)); },
        [value, minimum, maximum] {
            if (!value) return;
            const auto current = value_integer(*value, minimum);
            *value = current >= maximum ? minimum : current + 1;
        });
}

std::vector<PresentationField> settings_presentation_fields(SettingsDialog& dialog,
                                                             const Integer tab)
{
    std::vector<PresentationField> fields;
    if (const auto simple = simple_panel(dialog.m_panel)) {
        append_bool_field(fields, settings_debug_label, &simple->m_cb_debug);
        append_window_bool_field(fields, "BGMを再生", simple->m_cb_playbgm);
        append_window_bool_field(fields, "効果音を再生", simple->m_cb_playsound);
        if (::cw::dialog::is_all_mode())
            append_window_bool_field(fields, "ステータスダイアログを表示",
                                     simple->m_cb_all_status_dialog);
        if (simple->m_speed) {
            append_integer_field(fields, "背景の切り替え速度", &simple->m_speed->m_sl_tran, 0, 10);
            append_integer_field(fields, "カード描画速度", &simple->m_speed->m_sl_deal, 0, 10);
            append_integer_field(fields, "メッセージ表示速度", &simple->m_speed->m_sl_msgs, 0, 10);
        }
        return fields;
    }

    const auto panel = detail_panel(dialog.m_panel);
    if (!panel) return fields;
    if (tab == 0 && panel->m_pane_gene) {
        auto& pane = *panel->m_pane_gene;
        append_bool_field(fields, settings_debug_label, &pane.m_cb_debug);
        // ALLのレイアウトでは、Python版のタブレット用グループを既存の
        // ステータスダイアログ設定へ置き換える。 保存済みの設定値は維持する。
        if (::cw::dialog::is_all_mode() && panel->m_pane_ui)
            append_bool_field(fields, "ステータスダイアログを表示",
                              &panel->m_pane_ui->m_cb_show_all_status_dialog);
        if (!::cw::dialog::is_all_mode()) {
            append_bool_field(fields, "左クリックを連打可能", &pane.m_cb_can_repeatlclick);
            append_bool_field(fields, "右フリックを使用", &pane.m_cb_enabled_right_flick);
        }
        append_bool_field(fields, "パーティ記録を自動保存", &pane.m_cb_autosavepartyrecord,
            [&pane] { pane.OnAutoSavePartyRecord(nullptr); });
        append_bool_field(fields, "パーティ記録を上書き", &pane.m_cb_overwritepartyrecord);
        append_bool_field(fields, "初期所持金を使用", &pane.m_cb_initmoneyisinitialcash,
            [&pane] { pane.OnInitMoneyIsInitialCash(nullptr); });
        append_bool_field(fields, settings_no_levelup_label, &pane.m_cb_nolevelup);
        if (!::cw::dialog::is_all_mode())
            append_bool_field(fields, "タイルを表示", &pane.m_cb_show_tiles);
        append_bool_field(fields, "スクリーンショットにステータスバーを含む", &pane.m_cb_sswithstatusbar);
        append_integer_field(fields, "メッセージログ最大数", &pane.m_sc_backlogmax, 0, 9999);
        append_integer_field(fields, "初期所持金", &pane.m_sc_initmoneyamount, 0, ::cw::lib::money::maximum);
    } else if (tab == 1 && panel->m_pane_draw) {
        auto& pane = *panel->m_pane_draw;
        append_bool_field(fields, "重要なローカル設定", &pane.m_cb_important,
            [&pane] { pane.OnImportant(nullptr); });
        append_bool_field(fields, "背景を滑らかに描画", &pane.m_cb_smooth_bg);
        append_bool_field(fields, "カード配布時を滑らかに描画", &pane.m_cb_smoothing_card_down);
        append_bool_field(fields, "カード収納時を滑らかに描画", &pane.m_cb_smoothing_card_up);
        append_bool_field(fields, "白いカーソル", &pane.m_cb_whitecursor);
        if (pane.m_speed) {
            append_bool_field(fields, "戦闘速度を通常時と同じにする", &pane.m_speed->m_cb_use_battlespeed,
                [speed=pane.m_speed] { speed->OnUseBattleSpeed(nullptr); });
            append_integer_field(fields, "カード描画速度", &pane.m_speed->m_sl_deal, 0, 10);
            append_integer_field(fields, "戦闘カード描画速度", &pane.m_speed->m_sl_deal_battle, 0, 10);
            append_integer_field(fields, "メッセージ表示速度", &pane.m_speed->m_sl_msgs, 0, 10);
            append_integer_field(fields, "背景切替速度", &pane.m_speed->m_sl_tran, 0, 10);
        }
    } else if (tab == 3 && panel->m_pane_font) {
        auto& pane = *panel->m_pane_font;
        append_bool_field(fields, "重要なローカル設定", &pane.m_cb_important,
            [&pane] { pane.OnImportant(nullptr); });
        append_bool_field(fields, "装飾フォント", &pane.m_cb_decorationfont);
        append_bool_field(fields, "カード名に縁取り", &pane.m_cb_bordering_cardname);
        append_bool_field(fields, "カード名を滑らかに表示", &pane.m_cb_fontsmoothingcardname);
        append_bool_field(fields, "メッセージを滑らかに表示", &pane.m_cb_fontsmoothingmessage);
        append_bool_field(fields, "ステータスバーを滑らかに表示", &pane.m_cb_fontsmoothingstatusbar);
    } else if (tab == 5 && panel->m_pane_ui) {
        auto& pane = *panel->m_pane_ui;
        for(std::size_t id=0;id<ui_options.size();++id) {
            append_bool_field(fields,ui_options[id].label,&(pane.*ui_options[id].member),[&pane,id] {
                if(id==0) pane.OnAllQuickDeal(nullptr);
                if(id==1) pane.OnQuickDeal(nullptr);
            });
        }
    }
    return fields;
}

bool settings_field_enabled(SettingsDialog& dialog, Integer tab, const String& label)
{
    if (const auto panel=detail_panel(dialog.m_panel)) {
        if (tab==5 && panel->m_pane_ui && label==ui_options[33].label)
            return !panel->m_pane_ui->m_pc_limits_inn.expired();
        if (tab==0 && panel->m_pane_gene) {
            if (label=="パーティ記録を上書き") return value_bool(panel->m_pane_gene->m_cb_autosavepartyrecord);
            if (label=="初期所持金") return !value_bool(panel->m_pane_gene->m_cb_initmoneyisinitialcash);
        }
        if (tab==1 && panel->m_pane_draw && panel->m_pane_draw->m_speed && label=="戦闘カード描画速度")
            return !value_bool(panel->m_pane_draw->m_speed->m_cb_use_battlespeed);
    }
    return true;
}

void settings_presentation_button(::cw::dialog::DialogPresentation& presentation,
                                  const std::shared_ptr<::cw::lib::Surface>& surface,
                                  const String& label, const ::cw::lib::Rect rect,
                                  const ::cw::dialog::DialogPresentationAction action,
                                  const int index, const bool enabled = true,
                                  const bool selected = false)
{
    ::cw::lib::ui::draw_button_face(surface, rect, {enabled, selected});
    const auto font = ::cw::lib::ui::configured_font("button", 14, true);
    const auto label_width = ::cw::lib::ui::label_width(
        label, font.pixels, font.bold, font.face);
    ::cw::lib::ui::draw_label(surface, label,
        rect.x + std::max<Integer>(7, (rect.width - label_width) / 2), rect.y + 5,
        font.pixels, font.bold,
        enabled ? std::tuple<Integer, Integer, Integer>{0, 0, 0}
                : std::tuple<Integer, Integer, Integer>{120, 120, 120},
        font.face);
    presentation.controls.push_back({action, index, static_cast<float>(rect.x),
        static_cast<float>(rect.y), static_cast<float>(rect.width),
        static_cast<float>(rect.height), enabled});
}

String settings_presentation_version_text()
{
    // 設定画面のフッターは製品向けの表記であり、内部の Engine
    // のバージョンタプルやホストのビット数とは独立させる。
    String text = "Py_C 0.2.0 Daskin";
    if (::cw::thread::g_versioninfo && !::cw::thread::g_versioninfo->m_build_datetime.empty())
        text += "\nBuild: " + ::cw::thread::g_versioninfo->m_build_datetime;
    return text;
}

void settings_presentation_version(SettingsDialog& dialog,const std::shared_ptr<::cw::lib::Surface>& surface,
                                   const ::cw::lib::Rect area,::cw::lib::Point origin={})
{
    std::vector<String> lines;
    std::istringstream input(settings_presentation_version_text());
    for(String line;std::getline(input,line);) lines.push_back(std::move(line));
    if(lines.empty()) return;
const auto scale=std::max<::cw::lib::Real>(1.0,::cw::g_UP_WIN);
    const auto px=[&](const Integer value){return static_cast<Integer>(std::lround(value*scale));};
    const ::cw::lib::Rect final_area{px(area.x),px(area.y),px(area.width),px(area.height)};
    auto pixels=11;
    auto font=::cw::lib::ui::configured_font("dlgmsg2",pixels);
    font.pixels=px(font.pixels);
    const auto widest=[&] {
        Integer width{};
        for(const auto& line:lines) width=std::max(width,::cw::lib::ui::label_width(
            line,font.pixels,font.bold,font.face,font.italic));
        return width;
    };
    while(pixels>8 && widest()>final_area.width) {
        font=::cw::lib::ui::configured_font("dlgmsg2",--pixels);
        font.pixels=px(font.pixels);
    }
    dialog.m_version_text.set(settings_presentation_version_text());
    dialog.m_version_text.layout(final_area,font);
    dialog.m_version_text.draw(surface,{px(origin.x),px(origin.y)});

}

::cw::lib::Real settings_final_scale(const SettingsDialog& dialog)
{
    // 設定ウィンドウと埋め込みパネルに、同じ wins() の拡大率を適用する。
    (void)dialog;
    return std::max<::cw::lib::Real>(1.0, ::cw::g_UP_WIN);
}

Integer settings_detail_view_width()
{
    // ALL では詳細設定ページ全体の領域を確保し、単体版は小さな表示領域を維持
    // する。 どちらのホストも、この論理配置の後に wins() を適用する。
    return ::cw::dialog::is_all_mode() ? 770 : 600;
}

::cw::lib::ui::FontStyle settings_final_font(
    const SettingsDialog& dialog, const String& role, const Integer logical_pixels,
    const bool bold = false, const bool italic = false)
{
    auto result = ::cw::lib::ui::configured_font(
        role, dialog.local_panel_only() ? logical_pixels : std::max<Integer>(15, logical_pixels + 1), bold, italic);
    result.pixels = std::max<Integer>(1, static_cast<Integer>(
        std::lround(result.pixels * settings_final_scale(dialog))));
    return result;
}

// フォントは最終の wins() のサイズでラスター化し、ビットマップとして拡大しない。
struct SettingsCanvas {
    SettingsDialog& dialog;
    ::cw::dialog::DialogPresentation& result;
    std::shared_ptr<::cw::lib::Surface> surface;
    ::cw::lib::Point origin{};
    bool paint{true};
    ::cw::lib::Real scale{settings_final_scale(dialog)};
    ::cw::lib::ui::FontStyle font{settings_final_font(dialog,"dlgmsg2",13)};
    Integer px(const Integer value) const {
        return static_cast<Integer>(std::lround(value * scale));
    }
    Integer logical(const Integer value) const {
        return static_cast<Integer>(std::ceil(value / scale));
    }
    ::cw::lib::Size final_size(const ::cw::lib::Size value) const {
        return {px(value.width), px(value.height)};
    }
    ::cw::lib::Rect local(::cw::lib::Rect r) const {
        const auto left=px(r.x-origin.x),top=px(r.y-origin.y);
        const auto right=px(r.x+r.width-origin.x),bottom=px(r.y+r.height-origin.y);
        return {left,top,right-left,bottom-top};
    }
    // 各保持領域は、ページ全体を自身のクリップ付きレイヤーへ描く。
    // 負荷を抑えるため、そのレイヤー外の基本図形はラスター化しな
    // い。ただし当たり判定矩形は全コントロールについて公開する。
    bool offscreen(::cw::lib::Rect r) const {
        if(!paint || !surface) return false;
        const auto l=local(r);
        return l.x>=surface->size.width || l.y>=surface->size.height ||
               l.x+l.width<=0 || l.y+l.height<=0;
    }
    // テキストは原点から右へ幅不明で伸びるため、占め
    // 得る行と左端だけを使って描画対象外を判定する。
    bool text_offscreen(Integer x,Integer y) const {
        if(!paint || !surface) return false;
        return offscreen({x,y-font.pixels,std::max<Integer>(1,surface->size.width),font.pixels*3}) ||
               px(x-origin.x)>=surface->size.width;
    }
    void fill(::cw::lib::Rect r,::cw::lib::Color color) {if(paint && !offscreen(r)) ::cw::lib::ui::fill_rect(surface,local(r),color);}
    void border(::cw::lib::Rect r) {if(paint && !offscreen(r)) ::cw::lib::ui::draw_sunken_border(surface,local(r));}
    void blend(const std::shared_ptr<::cw::lib::Surface>& image,Integer x,Integer y) {
        if(!paint || !image) return;
        if(offscreen({x,y,logical(image->size.width)+1,logical(image->size.height)+1})) return;
        ::cw::lib::ui::blend_surface(surface,image,px(x-origin.x),px(y-origin.y));
    }
    void label(const String& text,Integer x,Integer y,
               std::tuple<Integer,Integer,Integer> colour={0,0,0}) {
        if(paint && !text_offscreen(x,y)) ::cw::lib::ui::draw_label(surface,text,px(x-origin.x),px(y-origin.y),font.pixels,font.bold,colour,font.face,font.italic);
    }
    void button_label(const String& text,::cw::lib::Rect r,
                      std::tuple<Integer,Integer,Integer> colour={0,0,0}) {
        if(!paint || text.empty() || offscreen(r)) return;
        const auto target=local(r);
        const auto glyph=::cw::lib::ui::render_label(text,font.pixels,font.bold,colour,font.face,font.italic);
        if(!glyph || target.width<=0 || target.height<=0) return;
        const auto available_width=std::max<Integer>(1,target.width-px(6));
        const auto draw_width=std::min(glyph->size.width,available_width);
        const auto draw_x=target.x+std::max<Integer>(0,(target.width-draw_width)/2);
        const auto draw_y=target.y+std::max<Integer>(0,(target.height-glyph->size.height)/2);
        // 固定矩形にボタンの見出しが収まらない場合、ラスター化
        // したフォントの高さは保ち、文字の幅だけを圧縮する。
        ::cw::lib::ui::blend_surface(surface,glyph,draw_x,draw_y,draw_width,glyph->size.height);
    }
    void hit(int id,::cw::lib::Rect r,bool enabled=true,bool continuous=false,const String& text={}) {
        result.controls.push_back({::cw::dialog::DialogPresentationAction::Auxiliary,id,
            static_cast<float>(r.x),static_cast<float>(r.y),static_cast<float>(r.width),static_cast<float>(r.height),
            enabled,continuous,text,text});
    }
    void group(const String& text,::cw::lib::Rect r) {
        if (!paint || offscreen({r.x,r.y-font.pixels,r.width,r.height+font.pixels})) return;
        border(r);
        const auto w=logical(::cw::lib::ui::label_width(text,font.pixels,font.bold,font.face,font.italic));
        fill({r.x+8,r.y-2,w+8,logical(font.pixels)+4},{240,240,240,255});
        label(text,r.x+12,r.y);
    }
    void field(const String& text,int id,::cw::lib::Rect r,bool enabled=true,bool combo=false) {
        if (!paint) {
            // 保持型の配置処理では、当たり判定矩形と IME 状態だけを公開する。 こ
            // こで画面外のラベルをラスター化しても合成時に捨てられるため、描画しない。
            if (dialog.m_scenario_edit == id) {
                result.wants_text_input = true;
                result.input_text = dialog.m_scenario_edit_text;
            }
            hit(id,r,enabled,false,text);
            return;
        }
        if(offscreen(r)) {
            if(dialog.m_scenario_edit==id) {
                result.wants_text_input=true;
                result.input_text=dialog.m_scenario_edit_text;
            }
            hit(id,r,enabled,false,text);
            return;
        }
        fill(r,enabled ? ::cw::lib::Color{255,255,255,255} : ::cw::lib::Color{225,225,225,255});
        border(r);
        auto cell=std::make_shared<::cw::lib::Surface>();
        cell->size=final_size({std::max<Integer>(1,r.width-(combo?22:8)),std::max<Integer>(1,r.height-4)});
        cell->rgba.assign(static_cast<std::size_t>(cell->size.width*cell->size.height*4),0);
        const bool editing=dialog.m_scenario_edit==id;
        const auto value=editing?dialog.m_scenario_edit_text:text;
        Integer offset=0;
        if(editing) {
            const auto caret=::cw::lib::ui::label_width(value.substr(0,dialog.m_scenario_caret),font.pixels,font.bold,font.face);
            offset=std::max<Integer>(0,caret-cell->size.width+px(2));
            if(dialog.m_scenario_select_all) ::cw::lib::ui::fill_rect(cell,{0,0,cell->size.width,cell->size.height},{0,120,215,255});
            else ::cw::lib::ui::fill_rect(cell,{caret-offset,px(1),std::max<Integer>(1,px(1)),cell->size.height-px(2)},{0,0,0,255});
            result.wants_text_input=true; result.input_text=value;
        }
        ::cw::lib::ui::draw_label(cell,value,-offset,(cell->size.height-font.pixels)/2,font.pixels,font.bold,
            editing && dialog.m_scenario_select_all?std::tuple<Integer,Integer,Integer>{255,255,255}:std::tuple<Integer,Integer,Integer>{0,0,0},font.face);
        blend(cell,r.x+4,r.y+2);
        if(combo) label("▾",r.x+r.width-16,r.y+4);
        hit(id,r,enabled,false,text);
    }
    void choice(int id,::cw::lib::Rect r) {
        auto choices=settings_choices(dialog,id);
        auto value=settings_choice_value(dialog,id);
        auto selected=value?value_integer(*value):-1;
        field(selected>=0 && selected<static_cast<Integer>(choices.size())?choices[selected]:String{},id,r,!choices.empty(),true);
    }
    void button(const String& text,int id,::cw::lib::Rect r,bool enabled=true) {
        if (!paint) { hit(id,r,enabled,false,text); result.controls.back().hover_highlight=true; return; }
        if(paint && !offscreen(r)) ::cw::lib::ui::draw_button_face(surface,local(r),{enabled});
        button_label(text,r);
        hit(id,r,enabled,false,text);
        result.controls.back().hover_highlight=true;
    }
    void check(const String& text,int id,bool checked,::cw::lib::Rect r,bool enabled=true) {
        const Integer box=std::min<Integer>(18,r.width);
        if(paint && !offscreen(r)) ::cw::lib::ui::draw_checkbox(surface,local({r.x,r.y+1,box,box}),checked,enabled);
        label(text,r.x+24,r.y+2); hit(id,r,enabled,false,text);
    }
};

void settings_speed_slider(SettingsCanvas& c,int id,::cw::lib::Rect r) {
    const auto value=drawing_value(c.dialog,id);
    const auto current=std::clamp<Integer>(value?value_integer(*value):0,0,10);
    const bool enabled=value && drawing_enabled(c.dialog,id);
    c.fill({r.x+5,r.y+9,r.width-10,3},{195,195,195,255});
    for(int i=0;i<=10;++i)
        c.fill({r.x+5+(r.width-10)*i/10,r.y+15,1,3},{180,180,180,255});
    c.fill({r.x+5+(r.width-10)*current/10-4,r.y+3,8,15},
        enabled?::cw::lib::Color{0,120,215,255}: ::cw::lib::Color{170,170,170,255});
    c.hit(id,r,enabled,true,"速度: "+std::to_string(current));
}

void settings_drawing_presentation(SettingsDialog& dialog,DrawingSettingPanel& pane,
    ::cw::dialog::DialogPresentation& result,const std::shared_ptr<::cw::lib::Surface>& surface,
    ::cw::lib::Point origin={},bool paint=true) {
    SettingsCanvas c{dialog,result,surface,origin,paint};
    const bool enabled=pane.m__controls_enabled, local=value_bool(pane.m__for_local);
    if(local) c.check("このスキンの描画設定を基本設定よりも優先して使用する",0,
        value_bool(pane.m_cb_important),{20,48,680,22});
    else {
        c.group("詳細",{12,76,340,116});
        c.check("拡大したカード画像を滑らかにする",3,value_bool(pane.m_cb_smoothing_card_up),{22,99,322,20});
        c.check("縮小したカード画像を滑らかにする",2,value_bool(pane.m_cb_smoothing_card_down),{22,122,322,20});
        c.check("拡大・縮小した背景画像を滑らかにする",1,value_bool(pane.m_cb_smooth_bg),{22,145,322,20});
        c.check("メイン画面で白いカーソルを使用する",4,value_bool(pane.m_cb_whitecursor),{22,168,322,20});
        if(pane.m_speed) {
            c.group("背景の切り替え方式 (速い⇔遅い)",{12,204,340,87});
            c.choice(drawing_transition,{22,227,310,24});
            settings_speed_slider(c,drawing_speed_first+3,{22,260,310,23});
            c.group("カード描画速度 (速い⇔遅い)",{12,304,340,56});
            settings_speed_slider(c,drawing_speed_first,{22,329,310,23});
            c.group("戦闘時のカード描画速度 (速い⇔遅い)",{12,372,340,83});
            c.check("通常時と同じ速度にする",5,value_bool(pane.m_speed->m_cb_use_battlespeed),{22,395,310,20});
            settings_speed_slider(c,drawing_speed_first+1,{22,424,310,23});
            c.group("メッセージ表示速度 (速い⇔遅い)",{12,466,340,56});
            settings_speed_slider(c,drawing_speed_first+2,{22,491,310,23});
        }
    }
    const String titles[]{"メッセージウィンドウ背景","メッセージウィンドウ枠", "メッセージログの背景","カーテン(選択モードの背景効果)","エンジン外枠（ALL）"};
    const bool all_frame = ::cw::dialog::is_all_mode();
    // Python と同様、スキン固有の設定では左側の全体設定専用列を省く。
    const Integer column_x=local?12:366, column_width=local?696:342;
    for(int row=0;row<(all_frame?5:4);++row) {
        // ALL仕様: カーテンの直下に外枠設定。既存のパネル高さに収める。
        const Integer y=76+row*(all_frame?(local?61:64):(local?76:82));
        c.group(titles[row],{column_x,y,column_width,all_frame?60:(local?66:72)});
        const auto colour_button=[&](int id,Integer x) {
            const auto colour=value_colour(*drawing_value(dialog,id),Colour{0,0,0,255});
            const ::cw::lib::Rect r{x,y+29,34,25};
            c.fill(r,{static_cast<std::uint8_t>(std::get<0>(colour)),
                static_cast<std::uint8_t>(std::get<1>(colour)),static_cast<std::uint8_t>(std::get<2>(colour)),255});
            c.border(r); c.hit(id,r,enabled,false,"色を選択");
        };
        c.label("カラー",column_x+8,y+34);
        colour_button(drawing_colour_first+(row<2?row*2:row+2),column_x+52);
        if(row<2) { c.label("ログ",column_x+94,y+34); colour_button(drawing_colour_first+row*2+1,column_x+122); }
        c.label("アルファ値",column_x+164,y+34);
        c.field(std::to_string(value_integer(*drawing_value(dialog,drawing_alpha_first+row))),drawing_alpha_first+row,{column_x+234,y+29,68,25},enabled);
        c.button("+",drawing_spin_first+row*2,{column_x+304,y+28,24,14},enabled);
        c.button("−",drawing_spin_first+row*2+1,{column_x+304,y+42,24,14},enabled);
    }
    const Integer background_y=local?384:410;
    c.group("フルスクリーンの背景",{column_x,background_y,column_width,100});
    const auto choices=settings_choices(dialog,drawing_background);
    c.field(choices[std::clamp<Integer>(value_integer(pane.m_ch_fscrbacktype),0,3)],drawing_background,{column_x+10,background_y+22,column_width-22,25},enabled,true);
    c.field(value_string(pane.m_tx_fscrbackfile),drawing_file,{column_x+10,background_y+61,column_width-78,25},pane.m__fullscreen_file_enabled);
    c.button("参照",drawing_browse,{column_x+column_width-62,background_y+61,50,25},pane.m__fullscreen_file_enabled);
    if(local) {
        if(value_bool(pane.m_copybtn)) c.button("基本設定をコピー",drawing_copy,{478,502,132,20},enabled);
        c.button("デフォルト",drawing_defaults,{616,502,92,20},enabled);
    }
    for(const auto& control:result.controls) if(control.index==dialog.m_drawing_focus && control.enabled) {
        const Integer x=control.x,y=control.y,w=control.width,h=control.height;
        for(const auto r:{::cw::lib::Rect{x,y,w,1},{x,y+h-1,w,1},{x,y,1,h},{x+w-1,y,1,h}})
            c.fill(r,{0,120,215,255});
    }
}

void settings_ui_presentation(SettingsDialog& dialog,UISettingPanel& pane,
    ::cw::dialog::DialogPresentation& result,const std::shared_ptr<::cw::lib::Surface>& surface,
    ::cw::lib::Point origin={},bool paint=true) {
    const auto viewport=settings_ui_viewport(dialog);
    struct Group { const char* title; std::vector<int> controls; };
    std::array<Group,8> groups{{
        {"スキップと空白時間",{8,7,9,6,32,12,2}},
        {"カード",{1,0,24,20,23,ui_status_time}},
        {"操作",{22,26,27,16,29,25,15,14,ui_confirm_dump,5,28,ui_radius}},
        {"通知と解説",{19,31,4,3}},
        {"セーブとロード",{ui_confirm_save,30,10}},
        {"ダイアログ",{18,17,21,11,13}},
        // 宿のカード枚数上限は、全体設定のPCクリック時モーションとは分けて扱う。
        {"宿のPCカード枚数（各1～50枚／レベルによる上限以上）",
         {33,ui_skill_limit,ui_personal_limit,ui_beast_limit}},
        {"PCクリック時のモーション",{34}}
    }};
    Integer height=8;
    for(const auto& group:groups) height+=32+24*group.controls.size();
    dialog.m_ui_content_height=height;
    dialog.m_ui_scroll=std::clamp<Integer>(dialog.m_ui_scroll,0,std::max<Integer>(0,height-viewport.height));
    // 内容の描画面は約 1000px の文書全体ではなく
    // 、スクロール位置を原点とした表示領域だけを覆う。
    // 負荷を抑えるため、領域外の行はラスター化しない
    // 。当たり判定矩形は従来どおり文書座標で保持する。
    auto content=std::make_shared<::cw::lib::Surface>();
    ::cw::dialog::DialogPresentation body;
    SettingsCanvas c{dialog,body,content,{0,dialog.m_ui_scroll},paint};
    content->size=c.final_size({viewport.width,viewport.height});
    content->rgba.assign(static_cast<std::size_t>(content->size.width)*content->size.height*4U,0);
    ::cw::lib::ui::fill_rect(content,{0,0,content->size.width,content->size.height},{240,240,240,255});
    SettingsCanvas output{dialog,result,surface,origin,paint};
    Integer y=4;
    for(const auto& group:groups) {
        const Integer group_height=26+24*group.controls.size();
        c.group(group.title,{0,y,viewport.width,group_height});
        Integer row=y+23;
        for(const int id:group.controls) {
            if(id>=0 && id<static_cast<int>(ui_options.size()))
                c.check(ui_options[id].label,id,value_bool(pane.*ui_options[id].member),{12,row,viewport.width-24,22},
                    id!=33 || !pane.m_pc_limits_inn.expired());
            else if(id==ui_skill_limit || id==ui_personal_limit || id==ui_beast_limit) {
                const bool enabled=!pane.m_pc_limits_inn.expired() && value_bool(pane.m_cb_pc_card_limits);
                c.label(id==ui_skill_limit ? "技能カード上限:" : id==ui_beast_limit ? "召喚獣上限:"
                                                             : "私有袋上限:",12,row+5);
                c.field(std::to_string(value_integer(*ui_value(dialog,id))),id,{180,row,60,23},enabled);
                c.button("+",id==ui_skill_limit?ui_skill_up:id==ui_beast_limit?ui_beast_up:ui_personal_up,
                         {244,row,24,23},enabled);
                c.button("−",id==ui_skill_limit?ui_skill_down:id==ui_beast_limit?ui_beast_down:ui_personal_down,
                         {270,row,24,23},enabled);
                c.label("枚",300,row+5);
            }
            else if(id==ui_radius) {
                c.label("マウスホイールでのカードの選択中にカーソルの小さな動きを無視する:",12,row+5);
                c.field(std::to_string(value_integer(pane.m_sc_radius_notdetectmovement)),id,{486,row,50,23});
                c.button("+",ui_radius_up,{538,row,20,12});
                c.button("−",ui_radius_down,{538,row+12,20,12});
                c.label("ピクセルまで",566,row+5);
            } else {
                const String label=id==ui_status_time?"状態の残り時間:":id==ui_confirm_dump?"カードの売却と破棄の確認ダイアログ:":"セーブ前の確認ダイアログ:";
                c.label(label,12,row+5);
                const auto label_width=c.logical(::cw::lib::ui::label_width(label,c.font.pixels,c.font.bold,c.font.face));
                c.choice(id,{20+label_width,row,260,23});
            }
            row+=24;
        }
        y+=group_height+6;
    }
    dialog.m_ui_control_rects.clear(); dialog.m_ui_control_order.clear();
    for(const auto& control:body.controls) {
        ::cw::lib::Rect r{static_cast<Integer>(control.x),static_cast<Integer>(control.y),
            static_cast<Integer>(control.width),static_cast<Integer>(control.height)};
        dialog.m_ui_control_rects.emplace(control.index,r); dialog.m_ui_control_order.push_back(control.index);
        if(control.index==dialog.m_ui_focus)
            for(const auto edge:{::cw::lib::Rect{r.x,r.y,r.width,1},{r.x,r.y+r.height-1,r.width,1},{r.x,r.y,1,r.height},{r.x+r.width-1,r.y,1,r.height}})
                c.fill(edge,{0,120,215,255});
        const auto top=std::max<Integer>(r.y,dialog.m_ui_scroll), bottom=std::min<Integer>(r.y+r.height,dialog.m_ui_scroll+viewport.height);
        if(top>=bottom) continue;
        auto hit=control; hit.x+=viewport.x; hit.y=viewport.y+top-dialog.m_ui_scroll; hit.height=bottom-top;
        result.controls.push_back(std::move(hit));
    }
    auto viewport_surface=std::make_shared<::cw::lib::Surface>(); viewport_surface->size=c.final_size({viewport.width,viewport.height});
    viewport_surface->rgba.assign(static_cast<std::size_t>(viewport_surface->size.width)*viewport_surface->size.height*4U,0);
    ::cw::lib::ui::blend_surface(viewport_surface,content,0,0);
    output.blend(viewport_surface,viewport.x,viewport.y);
    result.wants_text_input=body.wants_text_input; result.input_text=body.input_text;
    const ::cw::lib::Rect track{696,viewport.y,12,viewport.height};
    output.fill(track,{220,220,220,255});
    const auto thumb=std::max<Integer>(18,track.height*viewport.height/height);
    const auto offset=(track.height-thumb)*dialog.m_ui_scroll/std::max<Integer>(1,height-viewport.height);
    output.fill({track.x,track.y+offset,track.width,thumb},{130,130,130,255});
    result.controls.push_back({::cw::dialog::DialogPresentationAction::Auxiliary,ui_scrollbar,
        696,static_cast<float>(track.y),12,static_cast<float>(track.height),true,true});
}

// CardWirthPy_C_All 専用の、右側パネルのショートカット枠。
void settings_status_dialog_presentation(SettingsDialog& dialog,UISettingPanel& pane,
    ::cw::dialog::DialogPresentation& result,const std::shared_ptr<::cw::lib::Surface>& surface,
    ::cw::lib::Point origin={},bool paint=true) {
    (void)pane;
    SettingsCanvas c{dialog,result,surface,origin,paint};
    constexpr Integer slots=static_cast<Integer>(::cw::setting::kAllStatusShortcutSlots);
    c.group("ショートカット（最大6個）",{12,48,696,54+30*slots});
    c.label("メイン画面の右側に表示するボタンです。一番上のボタンで、たたむ／展開するを切り替えます。",24,71);
    for(Integer slot=0;slot<slots;++slot) {
        const Integer row=94+30*slot;
        c.label("ショートカット"+std::to_string(slot+1)+":",24,row+5);
        c.choice(ui_shortcut_first+static_cast<int>(slot),{140,row,260,23});
    }
    const Integer custom_top=48+54+30*slots+8;
    c.group("カスタム状態（デバッグ機能）",{12,custom_top,696,69});
    c.label("ショートカットの「カスタム状態」を押した時に、パーティ全員に設定する状態です。",24,custom_top+23);
    c.button("登録...",ui_custom_status,{24,custom_top+41,120,23});
}

void settings_font_presentation(SettingsDialog& dialog,FontSettingPanel& pane,
    ::cw::dialog::DialogPresentation& result,const std::shared_ptr<::cw::lib::Surface>& surface,
    ::cw::lib::Point origin={},bool paint=true) {
    SettingsCanvas c{dialog,result,surface,origin,paint};
    const bool enabled=pane.m__controls_enabled;
    if(value_bool(pane.m__for_local))
        c.check("スキンのフォント設定を上書きする",0,value_bool(pane.m_cb_important),{12,48,340,22});
    c.group("表示例",{12,76,340,91});
    auto preview=std::make_shared<::cw::lib::Surface>();
    preview->size=c.final_size({328,70}); preview->rgba.assign(static_cast<std::size_t>(preview->size.width)*preview->size.height*4U,0);
    const auto pixels=std::clamp<Integer>(value_integer(pane.m_sc_example,24),8,32);
    const auto face=value_string(pane.m__last_face);
    const auto example=dialog.m_scenario_edit==font_example
        ? ::cw::util::format_title(dialog.m_scenario_edit_text,{{"fontface",face}},true)
        : value_string(pane.m_st_example);
    std::istringstream lines(example);
    String line; Integer y=0;
    // Py と同様、プレビューのフォントはウィンドウ拡大率とは独立した物理ピクセルで設定する。
    const auto final_pixels=pixels;
    while(std::getline(lines,line)) {
        const auto w=::cw::lib::ui::label_width(line,final_pixels,false,face);
        ::cw::lib::ui::draw_label(preview,line,(preview->size.width-w)/2,y,final_pixels,false,{0,0,0},face);
        y+=pixels+2;
    }
    c.blend(preview,18,96);
    c.group("詳細",{12,175,340,130});
    c.check("カード名を縁取りする",2,value_bool(pane.m_cb_bordering_cardname),{22,196,320,20},enabled);
    c.check("メッセージで装飾フォントを使用する",1,value_bool(pane.m_cb_decorationfont),{22,217,320,20},enabled);
    c.check("メッセージの文字を滑らかにする",4,value_bool(pane.m_cb_fontsmoothingmessage),{22,238,320,20},enabled);
    c.check("カード名の文字を滑らかにする",3,value_bool(pane.m_cb_fontsmoothingcardname),{22,259,320,20},enabled);
    c.check("ステータスバーの文字を滑らかにする",5,value_bool(pane.m_cb_fontsmoothingstatusbar),{22,280,320,20},enabled);
    c.group("表示例のカスタマイズ",{366,76,342,66});
    c.field(value_string(pane.m_tx_example),font_example,{376,99,204,25},enabled);
    c.result.controls.back().tooltip="%fontface% = フォント名\n\\% = %\n\\n = 改行\n\\\\ = \\";
    c.field(std::to_string(pixels),font_example_size,{586,99,34,25},enabled);
    c.button("+",7406,{620,99,14,12},enabled && pixels<32);
    c.button("-",7407,{620,111,14,13},enabled && pixels>8);
    c.button("初期化",font_example_reset,{640,99,58,25},enabled);
    c.group("基本フォント",{366,149,342,156});
    c.label("種類",380,170); c.label("フォント名",490,170);
    for(int row=0;row<static_cast<int>(pane.m__bases_order.size());++row) {
        c.label(pane.m_typenames[pane.m__bases_order[row]],380,193+row*21);
        c.hit(7500+row,{376,188+row*21,111,21},enabled);
        c.field(font_face_label(pane,font_base_first+row),font_base_first+row,{487,188+row*21,211,21},enabled,true);
        if(dialog.m_font_focus==font_base_first+row)
            c.border({376,188+row*21,322,21});
    }
    c.group("役割別フォント",{12,312,696,188});
    const int widths[]={140,224,70,72,72,74};
    const char* names[]={"役割","フォント名","サイズ","太字(通常)","太字(拡大)","斜体"};
    int x=22;
    for(int col=0;col<6;++col) { c.label(names[col],x+3,333); x+=widths[col]; }
    const auto count=static_cast<Integer>(pane.m__types.size());
    dialog.m_font_scroll=std::clamp<Integer>(dialog.m_font_scroll,0,std::max<Integer>(0,count-font_visible_rows));
    for(Integer visible=0;visible<font_visible_rows && visible+dialog.m_font_scroll<count;++visible) {
        const auto row=visible+dialog.m_font_scroll;
        const auto& type=pane.m__types[row];
        const auto data=font_cell(pane,font_type_first+static_cast<int>(row)*5);
        if(!data) continue;
        const auto top=font_rows_y+visible*font_row_height;
        auto label=std::make_shared<::cw::lib::Surface>();
        label->size=c.final_size({140,20}); label->rgba.assign(static_cast<std::size_t>(label->size.width)*label->size.height*4U,0);
        const bool selected=pane.m__type_cursor==row;
        const auto selection=::cw::lib::ui::system_selection_colours();
        ::cw::lib::ui::fill_rect(label,{0,0,label->size.width,label->size.height},selected ? selection.background : ::cw::lib::Color{240,240,240,255});
        ::cw::lib::ui::draw_label(label,pane.m_typenames[type],c.px(3),c.px(3),c.font.pixels,c.font.bold,
            selected ? std::tuple<Integer,Integer,Integer>{selection.text.r,selection.text.g,selection.text.b}
                     : std::tuple<Integer,Integer,Integer>{0,0,0},c.font.face);
        c.blend(label,22,top);
        c.hit(7600+static_cast<int>(row),{22,top,140,20},enabled);
        x=162;
        for(int col=0;col<5;++col) {
            const int id=font_type_first+static_cast<int>(row)*5+col;
            const ::cw::lib::Rect rect{x,top,widths[col+1],20};
            if(col==0) c.field(font_face_label(pane,id),id,rect,enabled,true);
            else if(col==1) {
                const bool editable=font_numeric_field(pane,id);
                c.field(editable?std::to_string(data->m_pixels):"-",id,rect,enabled && editable);
            }
            else {
                c.fill(rect,data->m_fonttype=="inherit" ? ::cw::lib::Color{230,230,230,255} : ::cw::lib::Color{255,255,255,255});
                c.check("",id,col==2?data->m_bold:col==3?data->m_bold_upscr:data->m_italic,{x+widths[col+1]/2-7,top,14,20},enabled);
                c.hit(id,rect,enabled);
            }
            if(dialog.m_font_focus==id) c.border(rect);
            x+=widths[col+1];
        }
    }
    const ::cw::lib::Rect track{688,font_rows_y,12,font_visible_rows*font_row_height};
    c.fill(track,{220,220,220,255});
    const auto thumb=std::max<Integer>(16,track.height*font_visible_rows/std::max<Integer>(1,count));
    const auto offset=(track.height-thumb)*dialog.m_font_scroll/std::max<Integer>(1,count-font_visible_rows);
    c.fill({track.x,track.y+offset,track.width,thumb},{155,155,155,255});
    c.hit(font_scrollbar,track,enabled,true);
    if(value_bool(pane.m__for_local)) {
        if(value_bool(pane.m_copybtn)) c.button("基本設定をコピー",font_copy,{478,502,132,20},enabled);
        c.button("デフォルト",font_defaults,{616,502,92,20},enabled);
    }
}

void reveal_skin_description(SettingsDialog& dialog) {
    const auto scale=settings_final_scale(dialog);
    const auto font=settings_final_font(dialog,"datadesc",13);
    const auto step=static_cast<Integer>(std::ceil(font.pixels/scale))+2;
    const auto rows=std::max<Integer>(1,(dialog.m_skin_description_rect.height-6)/step);
    const auto row=static_cast<Integer>(dialog.m_skin_description_text.caret_row());
    if(row<dialog.m_skin_description_scroll)dialog.m_skin_description_scroll=row;
    else if(row>=dialog.m_skin_description_scroll+rows)dialog.m_skin_description_scroll=row-rows+1;
}

void settings_skin_presentation(SettingsCanvas& c,SkinPanel& skin,::cw::lib::Rect r,bool editable) {
    c.choice(skin_choice,{r.x,r.y,r.width-(editable?30:0),24});
    const bool has_yado=::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata;
    if(!editable && has_yado)
        c.result.controls.back().tooltip="異なる種別のスキンに切り替えたい場合は、タイトル画面に戻るか、詳細設定で「異なる種別のスキンを表示する」にチェックを入れてください。";
    if(editable) c.button("...",skin_install,{r.x+r.width-26,r.y,26,24});
    const ::cw::lib::Rect desc{r.x,r.y+30,r.width,r.height-30-(editable?53:0)};
    c.dialog.m_skin_description_rect=desc;
    const auto font=settings_final_font(c.dialog,"datadesc",13);
    const int logical_font_pixels=c.logical(font.pixels);
    const int step=logical_font_pixels+2;
    String text;
    const auto source=value_string(skin.m_tx_skin);
    for(std::size_t i=0;i<source.size();++i) {
        if(source[i]=='\r') {if(i+1<source.size() && source[i+1]=='\n')++i;text+='\n';}
        else text+=source[i];
    }
    auto& readonly=c.dialog.m_skin_description_text;
    readonly.set(text);
    const auto wrap_key = std::make_tuple(text, ::cw::imageretouch::get_fontface(font.face),
        font.pixels, font.bold, font.italic, c.scale, desc.width);
    if (!c.dialog.m_skin_description_wrap_key || *c.dialog.m_skin_description_wrap_key != wrap_key) {
        // 配置だけの処理で毎回 UTF-8 の接頭辞を伸ばしながら計測・描画しないよう
        // 、このダイアログの折り返し行だけを保持する。 任意の文字列やフォントオブジ
        // ェクトは保持せず、文字・フォント・拡大率・幅が変わったら結果を無効にする。
        auto lines=::cw::dialog::textedit::layout(text,c.px(std::max<Integer>(1,desc.width-24)),
            [&](std::string_view line){return ::cw::lib::ui::label_width(String(line),font.pixels,font.bold,font.face,font.italic);});
        c.dialog.m_skin_description_wrapped_lines = std::move(lines);
        c.dialog.m_skin_description_wrap_key = wrap_key;
    }
    const auto& lines = c.dialog.m_skin_description_wrapped_lines;
    const auto rows=std::max<Integer>(1,(desc.height-6)/step);
    c.dialog.m_skin_description_lines=static_cast<Integer>(lines.size());
    c.dialog.m_skin_description_scroll=std::clamp<Integer>(c.dialog.m_skin_description_scroll,0,std::max<Integer>(0,static_cast<Integer>(lines.size())-rows));
    c.fill(desc,{255,255,255,255});
    readonly.layout_wrapped({c.px(desc.x+4),c.px(desc.y+3),c.px(std::max<Integer>(1,desc.width-20)),c.px(std::max<Integer>(1,desc.height-6))},
        font,lines,c.px(step),c.dialog.m_skin_description_scroll);
    if(c.paint && !c.offscreen(desc))readonly.draw(c.surface,{c.px(c.origin.x),c.px(c.origin.y)});
    c.hit(skin_description,{desc.x+4,desc.y+3,desc.width-20,desc.height-6},true,true);
    c.border(desc);
    if(static_cast<Integer>(lines.size())>rows) {
        const auto thumb=std::max<Integer>(16,(desc.height-4)*rows/lines.size());
        const Integer top=c.dialog.m_skin_description_scroll*(desc.height-4-thumb)/(static_cast<Integer>(lines.size())-rows);
        c.fill({desc.x+desc.width-12,desc.y+2+top,8,thumb},{150,150,150,255});
        c.hit(skin_scrollbar,{desc.x+desc.width-14,desc.y+2,12,desc.height-4},true,true);
    }
    if(!editable) return;
    const auto index=value_integer(skin.m_ch_skin,-1);
    const bool valid=index>=0 && index<static_cast<Integer>(skin.m_skindirs.size());
    const auto active=::cw::g_cwpy && ::cw::g_cwpy->m_setting?::cw::g_cwpy->m_setting->m_skindirname:String{};
    const auto selected=valid?value_string(skin.m_skindirs[index]):String{};
    const Integer y=desc.y+desc.height+5;
    const auto w=(r.width-12)/4;
    c.button("自動生成...",skin_convert,{r.x,y,w,24});
    c.button("編集...",skin_edit,{r.x+w+4,y,w,24},valid);
    c.button("コピー",skin_copy,{r.x+(w+4)*2,y,w,24},valid);
    c.button("削除",skin_delete,{r.x+(w+4)*3,y,w,24},valid && selected!=active);
    c.check("異なる種別のスキンを表示する",skin_show_all,value_bool(skin.m_cb_show_allskin),{r.x,y+28,r.width,20},has_yado);
    c.result.controls.back().tooltip="スキンはそれぞれ独自のシステムを持つ場合があるため、異なる種別のスキンに切り替えると、キャラクターの情報がおかしくなったり、シナリオが正常に動かなくなるなどの問題が発生する可能性があります。";
}

void settings_expand_presentation(SettingsCanvas& c,ExpandPanel& expand,::cw::lib::Rect r) {
    expand.make_expandinfo();
    const bool enabled=expand.m__expand_enable && !value_bool(expand.m_cb_fullscreen);
    c.label("描画倍率:",r.x,r.y+5);
    const bool compact=simple_panel(c.dialog.m_panel)!=nullptr;
    c.choice(general_expand,{r.x+76,r.y,64,24});
    if((1LL<<expand_selection_max(expand.m__expand_max))*10>expand.m__expand_max)
        c.result.controls.back().tooltip="画面解像度を超える描画サイズは、環境によっては\n正常に機能しない可能性があります。";
    const String info="表示倍率:"+value_string(expand.m_st_expand);
    auto label=std::make_shared<::cw::lib::Surface>(); label->size=c.final_size({std::max<Integer>(1,r.width-(compact?0:146)),24});
    label->rgba.assign(static_cast<std::size_t>(label->size.width)*label->size.height*4U,0);
    ::cw::lib::ui::draw_label(label,info,0,c.px(5),c.font.pixels,c.font.bold,{0,0,0},c.font.face);
    c.blend(label,r.x+(compact?0:146),r.y+(compact?26:0));
    const ::cw::lib::Rect track{r.x,r.y+(compact?52:29),r.width,25};
    const auto maximum=std::max<Integer>(10,expand.m__expand_max);
    const auto value=std::clamp<Integer>(value_integer(expand.m_sl_expand),10,maximum);
    c.fill({track.x+5,track.y+9,track.width-10,3},{190,190,190,255});
    for(Integer n=10;n<=maximum;++n)
        c.fill({track.x+5+(track.width-10)*(n-10)/std::max<Integer>(1,maximum-10),track.y+16,1,3},{180,180,180,255});
    c.fill({track.x+5+(track.width-10)*(value-10)/std::max<Integer>(1,maximum-10)-4,track.y+3,8,15},
        enabled ? ::cw::lib::Color{0,120,215,255} : ::cw::lib::Color{175,175,175,255});
    c.hit(general_display,track,enabled,true,info);
    if(value_bool(expand.m_options)) {
        c.check("フルスクリーン",general_fullscreen,value_bool(expand.m_cb_fullscreen),{r.x,r.y+56,114,21});
        c.check("拡大後の画面を滑らかにする",general_smoothing,value_bool(expand.m_cb_smoothexpand),{r.x+120,r.y+56,r.width-120,21});
    } else c.check("フルスクリーン",general_fullscreen,value_bool(expand.m_cb_fullscreen),{r.x,r.y+(compact?80:56),150,23});
}

void settings_general_presentation(SettingsDialog& dialog,GeneralSettingPanel& pane,
    ::cw::dialog::DialogPresentation& result,const std::shared_ptr<::cw::lib::Surface>& surface,
    ::cw::lib::Point origin={},bool paint=true) {
    SettingsCanvas c{dialog,result,surface,origin,paint};
    const auto fields=settings_presentation_fields(dialog,0);
    const auto check=[&](const String& lookup,const String& text,Integer x,Integer y,Integer width=316) {
        for(std::size_t i=0;i<fields.size();++i) if(std::get<0>(fields[i])==lookup) {
            c.check(text,static_cast<int>(i),std::get<1>(fields[i])()=="ON",{x,y,width,20},settings_field_enabled(dialog,0,lookup));
            break;
        }
    };
    if (::cw::dialog::is_all_mode()) {
        c.group("ステータスダイアログ",{12,48,344,87});
        check("ステータスダイアログを表示","ステータスダイアログを表示",24,67);
    } else {
        c.group("タブレットモード",{12,48,344,87});
        check("タイルを表示","タイルを表示",24,67);
        check("右フリックを使用","右フリックを有効化",24,88);
        check("左クリックを連打可能","左クリック連打を許可",24,109);
    }
    c.group("詳細",{12,143,344,70});
    check(settings_debug_label,settings_debug_label,24,161);
    c.label("起動時の動作:",24,188); c.choice(general_startup,{118,182,226,24});
    c.group("メッセージログ(F5キーで表示)",{12,221,344,66});
    c.label("最大数:",24,244); c.field(std::to_string(value_integer(pane.m_sc_backlogmax)),6100,{80,240,70,24});
    c.choice(general_log,{162,240,182,24});
    c.group("スキン",{12,295,344,218});
    if(pane.m_skin) settings_skin_presentation(c,*pane.m_skin,{24,314,320,190},true);
    c.group("拡大表示方式(F4キーで拡大)",{366,48,342,104});
    if(pane.m_expand) settings_expand_presentation(c,*pane.m_expand,{378,66,318,80});
    c.group("パーティ",{366,160,342,130});
    check("パーティ記録を自動保存","パーティ記録を自動保存",378,179);
    check("パーティ記録を上書き","既存のパーティ記録を上書き",392,200,302);
    c.label("新規パーティの所持金:",378,227);
    c.field(std::to_string(value_integer(pane.m_sc_initmoneyamount)),6101,{552,221,140,24},pane.m__money_enabled);
    check("初期所持金を使用","スキンの初期所持金を使用する",378,250);
    check(settings_no_levelup_label,settings_no_levelup_label,378,270);
    c.group("スクリーンショット情報(画像上部に表示)",{366,299,342,214});
    c.field(value_string(pane.m_tx_ssinfoformat),6102,{378,319,314,24});
    c.choice(general_color,{378,348,116,24});
    c.button("書式を挿入...",general_tokens,{504,348,188,24},
        dialog.m_scenario_edit >= 6102 && dialog.m_scenario_edit <= 6104);
    c.label("背景画像:",378,382);
    c.field(value_string(pane.m_tx_ssinfobackimage),6105,{450,377,212,24});
    c.button("...",general_background,{666,377,26,24});
    c.label("保存名:",378,410); c.field(value_string(pane.m_tx_ssfnameformat),6103,{450,405,242,24});
    c.label("カード名:",378,439); c.field(value_string(pane.m_tx_cardssfnameformat),6104,{450,434,242,24});
    check("スクリーンショットにステータスバーを含む","ステータスバーも撮影する",378,463);
    c.label("[ ]内は各種情報がある場合のみ挿入",378,489);
}

struct SettingsChoiceGeometry {
    ::cw::lib::Rect rect{};
    int first{};
    int rows{};
};

SettingsChoiceGeometry settings_choice_geometry(SettingsDialog& dialog,
    const ::cw::dialog::DialogPresentation& result,::cw::lib::Size canvas_size) {
    const auto choices=settings_choices(dialog,dialog.m_settings_popup);
    SettingsChoiceGeometry geometry;
    Integer x=24,y=40,width=240;
    const auto font=settings_final_font(dialog,"dlgmsg2",13);
    for(const auto& item:result.controls) if(item.index==dialog.m_settings_popup) {
        x=static_cast<Integer>(item.x); y=static_cast<Integer>(item.y+item.height); width=std::max<Integer>(width,item.width);
    }
    for(const auto& choice:choices) width=std::max<Integer>(width,static_cast<Integer>(std::ceil(
        ::cw::lib::ui::label_width(choice,font.pixels,font.bold,font.face)/settings_final_scale(dialog)))+20);
    const bool fixed_detail=detail_panel(dialog.m_panel) && !dialog.local_panel_only();
    const Integer left=fixed_detail?dialog.m_detail_view_x:0;
    const Integer view_width=fixed_detail?std::min(settings_detail_view_width()-20,canvas_size.width):canvas_size.width;
    width=std::min<Integer>(width,view_width);
    geometry.rows=std::min<int>(10,choices.size());
    if(!geometry.rows) return geometry;
    dialog.m_settings_choice=std::clamp(dialog.m_settings_choice,0,static_cast<int>(choices.size())-1);
    geometry.first=std::clamp(dialog.m_settings_choice-geometry.rows/2,0,static_cast<int>(choices.size())-geometry.rows);
    x=std::clamp<Integer>(x,left,left+view_width-width);
    const Integer top=dialog.local_panel_only()?SettingsDialog::local_panel_top:
        fixed_detail?42+dialog.m_detail_view_y:0;
    const Integer bottom=dialog.local_panel_only()?top+SettingsDialog::local_panel_height:
        fixed_detail?top+326:canvas_size.height;
    y=std::clamp<Integer>(y,top,bottom-geometry.rows*24);
    geometry.rect={x,y,width,geometry.rows*24};
    return geometry;
}

void settings_choices_presentation(SettingsDialog& dialog,::cw::dialog::DialogPresentation& result,
    const std::shared_ptr<::cw::lib::Surface>& surface,::cw::lib::Size canvas_size={},
    ::cw::lib::Point origin={},bool paint=true) {
    if(!dialog.m_settings_popup) {
        dialog.dialog_retained_surface().set_component_hits("dropdown.rows",{});
        return;
    }
    const auto choices=settings_choices(dialog,dialog.m_settings_popup);
    if(choices.empty()) { dialog.m_settings_popup=0; return; }
    if(canvas_size.width<=0 || canvas_size.height<=0) canvas_size=surface->size;
    const auto geometry=settings_choice_geometry(dialog,result,canvas_size);
    const auto [x,y,width,unused_height]=geometry.rect;
    const int rows=geometry.rows,first=geometry.first;
    SettingsCanvas c{dialog,result,surface,origin,paint};
    result.wants_text_input=false;
    c.hit(settings_choice_dismiss,{0,0,canvas_size.width,canvas_size.height});
    const auto selection=::cw::lib::ui::system_selection_colours();
    std::vector<::cw::lib::ui::RetainedSurface::ComponentHitRegion> popup_hits;
    for(int i=0;i<rows;++i) {
        ::cw::lib::Rect row{x,y+i*24,width,24};
        const bool selected=first+i==dialog.m_settings_choice;
        c.fill(row,selected ? selection.background : ::cw::lib::Color{255,255,255,255});
        static const std::array<String,24> token_labels{"アプリケーション名","バージョン情報","ビルド情報","スキン名","拠点名","パーティ名","シナリオ名","作者名",
            "シナリオのファイルパス","シナリオのファイル名","互換モード","日付","時刻","年","月","日","時","分","秒","ミリ秒","%","[","]","\\"};
        const auto ink=selected ? selection.text : ::cw::lib::Color{0,0,0,255};
        c.label(dialog.m_settings_popup==general_tokens?token_labels[first+i]:choices[first+i],
                x+6,row.y+5,{ink.r,ink.g,ink.b});
        c.hit(settings_choice_first+first+i,row,true,false,choices[first+i]);
        popup_hits.push_back({row,first+i,true});
    }
    c.border({x,y,width,rows*24});
    // 保持するポップアップは dialog_presentation()
    // が最終描画面のピクセル座標で登録する。 この論理矩形も RetainedSurface
    // に重ねて登録しない。二重登録すると、2 倍・4 倍
    // 表示のホバー更新で無関係な等倍領域を復元してしまう。
    dialog.dialog_retained_surface().set_component_hits(
        "dropdown.rows",std::move(popup_hits));
}

void settings_audio_presentation(SettingsDialog& dialog, AudioSettingPanel& pane,
    ::cw::dialog::DialogPresentation& result, const std::shared_ptr<::cw::lib::Surface>& surface,
    ::cw::lib::Point origin={},bool paint=true)
{
    using Action = ::cw::dialog::DialogPresentationAction;
    SettingsCanvas c{dialog,result,surface,origin,paint};
    const auto font = c.font;
    const auto label = [&](const String& text, Integer x, Integer y) {
        c.label(text,x,y);
    };
    const auto group = [&](const String& title, ::cw::lib::Rect rect) {
        c.group(title,rect);
    };
    const auto hit = [&](Action action, int index, ::cw::lib::Rect rect, String text,
                         bool continuous = false) {
        c.result.controls.push_back({action, index, static_cast<float>(rect.x), static_cast<float>(rect.y),
            static_cast<float>(rect.width), static_cast<float>(rect.height), true, continuous, text, text});
    };
    const auto check = [&](String text, bool checked, int index, Integer x, Integer y) {
        if(c.paint) ::cw::lib::ui::draw_checkbox(c.surface,c.local({x,y,15,15}),checked);
        label(text,x+22,y);
        hit(Action::Auxiliary,index,{x,y,310,20},text);
    };
    // Python の AudioSettingPanel と同様、左側に音量グループ、右側
    // に SoundFont の表を配置する。 項目をページ分割したカード表示にはしない。
    group("詳細", {12,48,344,72});
    check("音楽を再生する", value_bool(pane.m_cb_playbgm), audio_play_bgm, 24,72);
    check("効果音を再生する", value_bool(pane.m_cb_playsound), audio_play_sound, 24,96);
    const std::array<String,4> titles{
        value_string(pane.m_box_master), value_string(pane.m_box_music),
        value_string(pane.m_box_midi), value_string(pane.m_box_sound)};
    for (int i=0;i<4;++i) {
        const Integer y=132+i*66;
        group(titles[i], {12,y,344,60});
        const auto value=std::clamp<Integer>(value_integer(*audio_volume(pane,i)),0,100);
        label("0",24,y+23); label("100",318,y+23);
        c.fill({48,y+30,256,4},{190,190,190,255});
        for (int tick=0;tick<=10;++tick)
            c.fill({48+tick*25,y+36,1,3},{140,140,140,255});
        c.fill({48+value*248/100,y+23,8,18},{0,120,215,255});
        label(std::to_string(value),165,y+42);
        hit(Action::Auxiliary,audio_slider_first+i,{48,y+18,256,26},titles[i],true);
    }
    group("MIDIサウンドフォント",{366,48,342,306});
    dialog.m_soundfont_scroll=std::clamp<Integer>(dialog.m_soundfont_scroll,0,
        std::max<Integer>(0,static_cast<Integer>(pane.m__soundfonts.size())-audio_visible_rows));
    c.fill({audio_table_x,76,322,168},{255,255,255,255});
    c.fill({audio_table_x,76,322,24},{224,224,224,255});
    label("使用",380,80); label("ファイル",420,80); label("音量(%)",642,80);
    for (Integer visible=0;visible<audio_visible_rows;++visible) {
        const Integer row=dialog.m_soundfont_scroll+visible;
        if (row>=static_cast<Integer>(pane.m__soundfonts.size())) break;
        const auto& [path,enabled,volume]=pane.m__soundfonts[static_cast<std::size_t>(row)];
        const Integer y=audio_table_y+audio_row_height*(visible+1);
        const bool selected=std::find(pane.m__selected_soundfont_rows.begin(),
            pane.m__selected_soundfont_rows.end(),row)!=pane.m__selected_soundfont_rows.end();
        const auto selection=::cw::lib::ui::system_selection_colours();
        const ::cw::lib::Color colour=selected ? selection.background
                                                : ::cw::lib::Color{255,255,255,255};
        c.fill({audio_table_x,y,322,24},colour);
        hit(Action::Select,static_cast<int>(row),{audio_table_x,y,322,24},path);
        if(c.paint) ::cw::lib::ui::draw_checkbox(c.surface,c.local({380,y+4,15,15}),enabled);
        hit(Action::Auxiliary,audio_use_first+static_cast<int>(row),{376,y,38,24},"使用");
        // パスはセル内で切り取り、完全なパスはツールチップに保持する。
        auto cell=std::make_shared<::cw::lib::Surface>();
        cell->size=c.final_size({218,24}); cell->rgba.resize(static_cast<std::size_t>(cell->size.width)*cell->size.height*4U);
        ::cw::lib::ui::fill_rect(cell,{0,0,cell->size.width,cell->size.height},colour);
        ::cw::lib::ui::draw_label(cell,path,c.px(3),c.px(4),font.pixels,font.bold,
            selected ? std::tuple<Integer,Integer,Integer>{selection.text.r,selection.text.g,selection.text.b}
                     : std::tuple<Integer,Integer,Integer>{0,0,0},font.face);
        c.blend(cell,416,y);
        label(dialog.m_soundfont_edit_row==row ? dialog.m_soundfont_edit_text : std::to_string(volume),642,y+4);
        hit(Action::Auxiliary,audio_volume_first+static_cast<int>(row),{636,y,62,24},"音量(%)");
    }
    c.border({audio_table_x,76,322,168});
    c.button("追加...",audio_add,{376,258,84,26});
    c.button("削除",audio_remove,{468,258,84,26},pane.m__remove_soundfont_enabled);
    c.button("↑",audio_up,{560,258,36,26},pane.m__up_soundfont_enabled);
    c.button("↓",audio_down,{604,258,36,26},pane.m__down_soundfont_enabled);
    const Integer row_count=static_cast<Integer>(pane.m__soundfonts.size());
    if (row_count>audio_visible_rows) {
        const Integer thumb=std::max<Integer>(18,144*audio_visible_rows/row_count);
        const Integer offset=(144-thumb)*dialog.m_soundfont_scroll/(row_count-audio_visible_rows);
        c.fill({698,100,10,144},{224,224,224,255});
        c.fill({699,100+offset,8,thumb},{145,145,145,255});
        hit(Action::Auxiliary,audio_scrollbar,{698,100,10,144},"スクロール",true);
    }
    if (dialog.m_soundfont_edit_row>=0) {
        result.wants_text_input=true;
        result.input_text=dialog.m_soundfont_edit_text;
    }
}
void settings_scenario_presentation(SettingsDialog& dialog, ScenarioSettingPanel& pane,
    ::cw::dialog::DialogPresentation& result, const std::shared_ptr<::cw::lib::Surface>& surface,
    ::cw::lib::Point origin={},bool paint=true)
{
    using Action=::cw::dialog::DialogPresentationAction;
    SettingsCanvas c{dialog,result,surface,origin,paint};
    const auto font=c.font;
    const auto label=[&](const String& text,Integer x,Integer y,
                         std::tuple<Integer,Integer,Integer> colour=std::tuple<Integer,Integer,Integer>{0,0,0}) {
        c.label(text,x,y,colour);
    };
    const auto group=[&](const String& title,::cw::lib::Rect rect) {
        c.group(title,rect);
    };
    const auto cell=[&](int id,const String& text,::cw::lib::Rect rect,bool selected) {
        const bool editing=dialog.m_scenario_edit==id;
        const auto value=editing ? dialog.m_scenario_edit_text : text;
        const auto selection=::cw::lib::ui::system_selection_colours();
        const ::cw::lib::Color colour=selected ? selection.background : ::cw::lib::Color{255,255,255,255};
        auto image=std::make_shared<::cw::lib::Surface>();
        image->size=c.final_size({rect.width,rect.height}); image->rgba.resize(static_cast<std::size_t>(image->size.width)*image->size.height*4U);
        ::cw::lib::ui::fill_rect(image,{0,0,image->size.width,image->size.height},colour);
        const auto caret_width=editing ? ::cw::lib::ui::label_width(value.substr(0,dialog.m_scenario_caret),font.pixels,font.bold,font.face) : 0;
        const Integer offset=editing ? std::max<Integer>(0,caret_width-image->size.width+c.px(8)) : 0;
        if (editing && dialog.m_scenario_select_all)
            ::cw::lib::ui::fill_rect(image,{c.px(3),c.px(3),image->size.width-c.px(6),image->size.height-c.px(6)},{160,200,250,255});
        ::cw::lib::ui::draw_label(image,value,c.px(3)-offset,c.px(4),font.pixels,font.bold,
            selected ? std::tuple<Integer,Integer,Integer>{selection.text.r,selection.text.g,selection.text.b}
                     : std::tuple<Integer,Integer,Integer>{0,0,0},font.face);
        if (editing) ::cw::lib::ui::fill_rect(image,{c.px(3)+caret_width-offset,c.px(3),std::max<Integer>(1,c.px(1)),image->size.height-c.px(6)},{0,0,0,255});
        ::cw::lib::ui::draw_sunken_border(image,{0,0,image->size.width,image->size.height});
        c.blend(image,rect.x,rect.y);
        c.result.controls.push_back({Action::Auxiliary,id,static_cast<float>(rect.x),static_cast<float>(rect.y),
            static_cast<float>(rect.width),static_cast<float>(rect.height),true,false,value,value});
    };
    // Python の ScenarioSettingPanel と同様、設定項目、行操作付きフォルダ
    // ー表、編集可能な三つの外部アプリパスの順に配置する。 ページ分割したラベル表示へ置き換えない。
    group("詳細",{12,48,696,158});
    static const std::array<String,6> checks{
        "シナリオの選択開始位置をスキン毎に変更する",
        "最後に選んだシナリオをシナリオの選択開始位置にする",
        "シナリオ選択ダイアログで貼紙と一覧を同時に表示する",
        "シナリオのプレイログを出力する",
        "シナリオ選択ダイアログへシナリオをドロップした時はインストールダイアログを表示する",
        "シナリオのインストールに成功したら元ファイルを削除する"};
    for(int i=0;i<6;++i) {
        const Integer y=70+i*21;
        if(c.paint)::cw::lib::ui::draw_checkbox(c.surface,c.local({24,y,15,15}),value_bool(*scenario_check(pane,i)));
        label(checks[i],46,y);
        c.result.controls.push_back({Action::Auxiliary,scenario_check_first+i,24,static_cast<float>(y),672,20,true});
    }
    group("シナリオフォルダ(スキンタイプ別)",{12,216,696,178});
    c.button("参照...",scenario_ref,{24,236,78,25},pane.m__ref_folder_enabled);
    c.button("削除",scenario_remove,{110,236,78,25},pane.m__remove_folder_enabled);
    c.button("↑",scenario_up,{196,236,32,25},pane.m__up_folder_enabled);
    c.button("↓",scenario_down,{236,236,32,25},pane.m__down_folder_enabled);
    c.button("データベース構築...",scenario_db,{528,236,168,25});
    const Integer count=static_cast<Integer>(pane.m__folderoftype.size());
    dialog.m_scenario_scroll=std::clamp<Integer>(dialog.m_scenario_scroll,0,std::max<Integer>(0,count-scenario_visible_rows));
    c.fill({24,scenario_table_y,672,120},{255,255,255,255});
    for(Integer visible=0;visible<scenario_visible_rows;++visible) {
        const Integer row=dialog.m_scenario_scroll+visible;
        if(row>=count) break;
        const auto& [type,path]=pane.m__folderoftype[row];
        const Integer y=scenario_table_y+visible*scenario_row_height;
        const bool selected=pane.m__folder_cursor==row;
        cell(scenario_cell_first+static_cast<int>(row)*2,type,{24,y,142,24},selected);
        cell(scenario_cell_first+static_cast<int>(row)*2+1,path,{166,y,518,24},selected);
        if(selected) c.button("▾",scenario_types,{144,y,22,24});
    }
    const Integer maximum=std::max<Integer>(0,count-scenario_visible_rows);
    if(maximum) {
        const Integer thumb=std::max<Integer>(18,120*scenario_visible_rows/count);
        c.fill({685,scenario_table_y,11,120},{220,220,220,255});
        c.fill({686,scenario_table_y+(120-thumb)*dialog.m_scenario_scroll/maximum,9,thumb},{145,145,145,255});
        c.result.controls.push_back({Action::Auxiliary,scenario_scrollbar,684,static_cast<float>(scenario_table_y),12,120,true,true});
    }
    group("外部アプリ",{12,404,696,109});
    static const std::array<String,3> apps{"エディタ","ファイラー(フォルダ用)","ファイラー(ファイル用)"};
    for(int i=0;i<3;++i) {
        const Integer y=424+i*28;
        label(apps[i],24,y+4);
        cell(scenario_application_first+i,value_string(*scenario_application(pane,scenario_application_first+i)),{174,y,466,24},false);
        c.button("...",scenario_browse_first+i,{648,y,48,24});
    }
    result.wants_text_input=dialog.m_scenario_edit>=0 && !dialog.m_scenario_type_popup;
    result.input_text=dialog.m_scenario_edit_text;
    dialog.dialog_retained_surface().set_component_hits("scenario.dropdown.rows",{});
    if(dialog.m_scenario_type_popup) {
        const auto types=scenario_types_for(dialog);
        if(types.empty()) { dialog.m_scenario_type_popup=false; return; }
        dialog.m_scenario_type_choice=std::clamp(dialog.m_scenario_type_choice,0,static_cast<int>(types.size())-1);
        const Integer rows=std::min<Integer>(8,types.size());
        const Integer first=std::clamp<Integer>(dialog.m_scenario_type_choice-rows/2,0,types.size()-rows);
        const Integer y=std::min<Integer>(scenario_table_y+(pane.m__folder_cursor-dialog.m_scenario_scroll+1)*24,510-rows*24);
        c.result.controls.push_back({Action::Auxiliary,scenario_popup_dismiss,0,0,720,560,true});
        const auto selection=::cw::lib::ui::system_selection_colours();
        std::vector<::cw::lib::ui::RetainedSurface::ComponentHitRegion> popup_hits;
        for(Integer i=0;i<rows;++i) {
            const ::cw::lib::Rect rect{24,y+i*24,250,24};
            const bool selected=first+i==dialog.m_scenario_type_choice;
            c.fill(rect,selected ? selection.background : ::cw::lib::Color{250,250,250,255});
            const auto ink=selected ? selection.text : ::cw::lib::Color{0,0,0,255};
            label(types[first+i],28,rect.y+4,{ink.r,ink.g,ink.b});
            c.result.controls.push_back({Action::Auxiliary,scenario_type_first+static_cast<int>(first+i),24,static_cast<float>(rect.y),250,24,true});
            popup_hits.push_back({rect,first+i,true});
        }
        // これらのピクセルはシナリオページの構成要素が所有する。
        // 下の変更検出にはポップアップ行を含め、ホバー時はそ
        // のページを更新し、別の拡大率の保持矩形を追加しない。
        dialog.dialog_retained_surface().set_component_hits(
            "scenario.dropdown.rows",std::move(popup_hits));
    }
}
} // namespace

void SettingsDialog::configure_local_panels(std::shared_ptr<DrawingSettingPanel> drawing,
                                           std::shared_ptr<FontSettingPanel> fonts)
{
    cancel_document_request();
    auto panel=std::make_shared<SettingsPanel>();
    panel->m_pane_draw=std::move(drawing); panel->m_pane_font=std::move(fonts);
    m_panel=std::static_pointer_cast<::cw::lib::Object>(panel);
    m_local_panel_only=true;
    m_state=SettingsDialogState::Visible;
    m_presentation_tab=1;
}

void SettingsDialog::finish_panel_edit(bool accept)
{
    if(accept) commit_scenario_edit();
    else m_scenario_edit=-1;
    m_settings_popup=0;
    m_scenario_type_popup=false;
    m_audio_slider=-1;
    request_dialog_presentation_refresh();
}

void SettingsDialog::select_local_panel(bool fonts)
{
    const int tab=fonts?3:1;
    if(m_presentation_tab==tab) return;
    finish_panel_edit(true);
    m_presentation_tab=tab;
    dialog_retained_surface().invalidate_all();
    request_dialog_presentation_refresh();
}

::cw::dialog::DialogPresentation SettingsDialog::dialog_presentation()
{
    if(!simple_panel(m_panel) && !(detail_panel(m_panel) && !m_local_panel_only && m_presentation_tab==0))
        m_skin_description_text.blur();
    auto result=settings_content_presentation();
    const auto finish_version=[&] {
        if((m_version_text.menu.opened() || m_skin_description_text.menu.opened()) && result.surface) {
            auto overlay=std::make_shared<::cw::lib::Surface>(*result.surface);
            m_version_text.menu.draw(result,overlay,m_version_text.buffer,settings_final_scale(*this));
            m_skin_description_text.menu.draw(result,overlay,m_skin_description_text.buffer,settings_final_scale(*this));
            result.surface=std::move(overlay);result.damage={{0,0,result.surface->size.width,result.surface->size.height}};
        }
        return result;
    };
    if(!detail_panel(m_panel) || m_local_panel_only || !result.surface) return finish_version();
    // 配置とスクロール位置は論理単位で扱い、ピクセル・文字描画・ポ
    // インターの当たり判定には同じ wins() の拡大率を使う。
    const auto scale=settings_final_scale(*this);
    const auto px=[&](Integer value){return static_cast<Integer>(std::lround(value*scale));};
    const Integer logical_width=settings_detail_view_width();
    const Integer width=px(logical_width),height=px(420);
    const Integer body_width=logical_width-20;
    constexpr Integer body_top=42,body_height=326,footer_top=384;
    const Integer horizontal_max=std::max<Integer>(0,720-body_width);
    const Integer vertical_max=m_presentation_tab==5 || m_presentation_tab==status_dialog_tab ? 0 : 156;
    m_detail_view_x=std::clamp<Integer>(m_detail_view_x,0,horizontal_max);
    m_detail_view_y=std::clamp<Integer>(m_detail_view_y,0,vertical_max);
    const auto key=std::make_tuple(result.surface_revision,m_detail_view_x,m_detail_view_y,m_presentation_tab);
    const bool repaint=!m_settings_view_key || *m_settings_view_key!=key || !result.damage.empty() ||
        !m_settings_view_surface || m_settings_view_surface->size.width!=width || m_settings_view_surface->size.height!=height;
    const auto source=result.surface;
    if(repaint) {
        m_settings_view_surface=std::make_shared<::cw::lib::Surface>();
        m_settings_view_surface->size={width,height};
        m_settings_view_surface->rgba.assign(static_cast<std::size_t>(width)*height*4,240);
        ::cw::lib::ui::fill_rect(m_settings_view_surface,{0,0,width,height},{240,240,240,255});
    }
    const auto scaled_rect=[&](::cw::lib::Rect r) {
        return ::cw::lib::Rect{px(r.x),px(r.y),px(r.x+r.width)-px(r.x),px(r.y+r.height)-px(r.y)};
    };
    const auto copy=[&](::cw::lib::Rect from,Integer x,Integer y) {
        if(!repaint) return;
        from=scaled_rect(from);x=px(x);y=px(y);
        // 内容は最終サイズでラスター化済みなので、文字を再拡縮しない。
        for(Integer row=0;row<from.height;++row) {
            if(from.y+row<0 || from.y+row>=source->size.height || y+row<0 || y+row>=height) continue;
            const auto left=std::max<Integer>({0,-from.x,-x});
            const auto right=std::min<Integer>({from.width,source->size.width-from.x,width-x});
            if(right<=left) continue;
            const auto begin=(static_cast<std::size_t>(from.y+row)*source->size.width+from.x+left)*4;
            const auto dest=(static_cast<std::size_t>(y+row)*width+x+left)*4;
            std::copy_n(source->rgba.begin()+begin,static_cast<std::size_t>(right-left)*4,
                        m_settings_view_surface->rgba.begin()+dest);
        }
    };
    copy({0,0,logical_width,body_top},0,0);
    // 詳細文書は横スクロールするが、縦スクロールバーは見えて
    // いる領域に属する。 600px のホストで切り落とした
    // り、幅広い ALL の文書内に二つ目を描いたりしない。
    const bool pinned_detail_scrollbar=m_presentation_tab==5;
    const Integer document_width=pinned_detail_scrollbar
        ? std::min<Integer>(body_width,696-m_detail_view_x) : body_width;
    copy({m_detail_view_x,body_top+m_detail_view_y,document_width,body_height},0,body_top);
    if(pinned_detail_scrollbar)
        copy({696,body_top,12,body_height},body_width+3,body_top);
    for(auto& control:result.controls) {
        control.x/=scale;control.y/=scale;control.width/=scale;control.height/=scale;
        if(pinned_detail_scrollbar && control.index==ui_scrollbar) {
            control.x=static_cast<float>(body_width+3);
        } else if(control.y>=524) {
            const Integer old_x=std::lround(control.x),old_y=std::lround(control.y);
            const Integer new_x=old_x>=455?old_x+logical_width-720:
                logical_width==600?(old_x==98?94:old_x==184?176:old_x):old_x;
            if(control.index==9500) {
                // ネイティブホストではフッターの寸法を固定する。 選択可能な文字列は
                // OK の手前で切り取り、コピーでは全体を取得できるようにする。
                control.width=std::max<Integer>(1,logical_width-265-new_x-6);
                settings_presentation_version(*this,repaint?m_settings_view_surface:nullptr,
                    {new_x,footer_top,static_cast<Integer>(control.width),28});
            } else copy({old_x,old_y,static_cast<Integer>(std::lround(control.width)),static_cast<Integer>(std::lround(control.height))},new_x,footer_top);
            control.x=static_cast<float>(new_x);control.y=footer_top;
        } else if(control.y>=body_top || control.height>body_top) {
            control.x-=static_cast<float>(m_detail_view_x);control.y-=static_cast<float>(m_detail_view_y);
            const auto right=std::min<float>(body_width,control.x+control.width);
            const auto bottom=std::min<float>(body_top+body_height,control.y+control.height);
            control.x=std::max<float>(0,control.x);control.y=std::max<float>(body_top,control.y);
            control.width=right-control.x;control.height=bottom-control.y;
        }
    }
    std::erase_if(result.controls,[](const auto& c){return c.width<=0 || c.height<=0;});
    const auto bar=[&](int id,::cw::lib::Rect track,::cw::lib::Rect thumb) {
        if(repaint) {
            ::cw::lib::ui::fill_rect(m_settings_view_surface,scaled_rect(track),{220,220,220,255});
            ::cw::lib::ui::fill_rect(m_settings_view_surface,scaled_rect(thumb),{135,135,135,255});
        }
        // ALL が押下・ドラッグ・解除を送るのは continuous_pointer
        // のコントロールだけである。 このフラグがないとバーがボタン用ハンドラーに送
        // られ、9900/9901 の操作がないため軌道をクリックしても動かなくなる。
        result.controls.push_back({::cw::dialog::DialogPresentationAction::Auxiliary,id,
            static_cast<float>(track.x),static_cast<float>(track.y),
            static_cast<float>(track.width),static_cast<float>(track.height),!document_request_pending(),true});
    };
    const Integer horizontal_thumb=body_width*body_width/720,vertical_thumb=body_height*body_height/482;
    if(horizontal_max>0)
        bar(9900,{0,368,body_width,16},{m_detail_view_x*(body_width-horizontal_thumb)/horizontal_max,370,horizontal_thumb,12});
    if(vertical_max>0)
        bar(9901,{body_width,42,20,326},{body_width+3,42+m_detail_view_y*(326-vertical_thumb)/vertical_max,14,vertical_thumb});
    for(auto& control:result.controls) {
        control.x*=scale;control.y*=scale;control.width*=scale;control.height*=scale;
    }
    result.surface=m_settings_view_surface;result.damage.clear();
    if(repaint) {result.damage.push_back({0,0,width,height});++m_settings_view_revision;}
    result.surface_revision=m_settings_view_revision;m_settings_view_key=key;
    return finish_version();
}

::cw::dialog::DialogPresentation SettingsDialog::settings_content_presentation()
{
    using Action = ::cw::dialog::DialogPresentationAction;
    const bool detailed = detail_panel(m_panel) != nullptr;
    // 簡易ウィンドウは 600×420、詳細ウィンドウは ALL で 770×420
    // 、単体版で 600×420 とし、同じ論理 720×560 の内容を表示する。
    // 配置・フォントサイズ・ポインター座標にまとめて wins() を適用する。
    const Integer width = detailed ? 720 : 600;
    const Integer height = detailed ? 560 : 420;
    ::cw::dialog::DialogPresentation result;
    result.route = "SETTINGS";
    result.title = "CardWirthPyの設定";
    result.fit_work_area_height = false;
    result.smooth_fitted_scaling = false;
    const auto apply_button=detailed ? detail_panel(m_panel)->m_btn_apply
        : simple_panel(m_panel) ? simple_panel(m_panel)->m_btn_apply : nullptr;
    const bool apply_enabled=apply_button && apply_button->id!=0 && !is_applying();
    const auto label_font = settings_final_font(*this,"dlgmsg2", 14);
    const auto small_font = settings_final_font(*this,"dlgmsg2", 13);
    auto fields = settings_presentation_fields(*this, detailed ? m_presentation_tab : 0);

    // Windows の簡易設定画面は、平坦な一覧ではなくグループ分けした操作パ
    // ネルである。 SDL でも同じ配置とモデルに基づくコントロールを公開する。
    if (!detailed) {
        using Canvas=::cw::lib::ui::ScaledRetainedCanvas;
        struct Component {
            String key;
            ::cw::lib::Rect rect;
            std::uint64_t signature{};
            std::function<void(SettingsCanvas&)> present;
            bool changed{};
        };
        const auto field_index = [&](const String& label) -> int {
            for (std::size_t i = 0; i < fields.size(); ++i)
                if (std::get<0>(fields[i]) == label) return static_cast<int>(i);
            return -1;
        };
        const auto field_value = [&](const String& label) {
            const auto index=field_index(label);
            return index>=0 ? std::get<1>(fields[static_cast<std::size_t>(index)])() : String{};
        };
        const auto signature = [](std::initializer_list<String> values) {
            std::uint64_t value=1469598103934665603ULL;
            for(const auto& item:values) {
                value^=std::hash<String>{}(item)+0x9e3779b97f4a7c15ULL+(value<<6U)+(value>>2U);
            }
            return value;
        };
        const auto add_control = [](SettingsCanvas& c,const Action action, const int index,
                                     const ::cw::lib::Rect rect,const bool enabled = true) {
            if (index < 0) return;
            c.result.controls.push_back({action, index, static_cast<float>(rect.x),
                static_cast<float>(rect.y), static_cast<float>(rect.width),
                static_cast<float>(rect.height), enabled});
        };
        const auto draw_group = [&](SettingsCanvas& c,const String& label, const ::cw::lib::Rect rect) {
            if (!c.paint) return;
            c.fill(rect,{235,235,235,255}); c.border(rect);
            const auto group_width = c.logical(::cw::lib::ui::label_width(
                label, small_font.pixels, small_font.bold, small_font.face));
            c.fill(
                {rect.x + 7, rect.y, group_width + 10, c.logical(small_font.pixels) + 3},
                {235, 235, 235, 255});
            const auto old=c.font; c.font=small_font; c.label(label,rect.x+12,rect.y+1); c.font=old;
        };
        const auto draw_check = [&](SettingsCanvas& c,const String& label, const Integer index,
                                     const Integer x, const Integer y, const Integer w) {
            if (index < 0) return;
            const bool checked = std::get<1>(fields[static_cast<std::size_t>(index)])() == "ON";
            const ::cw::lib::Rect hit{x, y, w, 21};
            const ::cw::lib::Rect box{x, y + 2, 16, 16};
            if(c.paint) ::cw::lib::ui::draw_checkbox(c.surface,c.local(box),checked);
            const auto old=c.font; c.font=label_font; c.label(label,x+23,y+2); c.font=old;
            add_control(c,Action::Auxiliary,index,hit);
        };

        const Integer left_x = 8;
        const Integer left_w = 244;
        const Integer right_x = 260;
        const Integer right_w = width - right_x - 8;
        const auto simple=simple_panel(m_panel);
        std::vector<Component> components;
        const auto add=[&](String key,::cw::lib::Rect rect,std::uint64_t sig,
                           std::function<void(SettingsCanvas&)> present) {
            components.push_back({std::move(key),rect,sig,std::move(present),false});
        };
        add("settings.compact.debug",{left_x,8,left_w,39},signature({field_value(settings_debug_label)}),
            [&](SettingsCanvas& c){draw_group(c,"デバッグ",{left_x,8,left_w,39});draw_check(c,settings_debug_label,field_index(settings_debug_label),18,23,245);});
        add("settings.compact.skin",{left_x,50,left_w,181},signature({simple&&simple->m_skin?value_state_string(simple->m_skin->m_ch_skin):String{},simple&&simple->m_skin?value_state_string(simple->m_skin->m_tx_skin):String{},std::to_string(m_skin_description_scroll),std::to_string(m_skin_description_text.signature())}),
            [&](SettingsCanvas& c){draw_group(c,"スキン",{left_x,50,left_w,181});if(simple&&simple->m_skin)settings_skin_presentation(c,*simple->m_skin,{18,68,left_w-24,148},false);});
        const bool all_status_option = ::cw::dialog::is_all_mode();
        add("settings.compact.audio",{left_x,234,left_w,63},
            signature({field_value("BGMを再生"),field_value("効果音を再生")}),
            [&](SettingsCanvas& c){
                draw_group(c,"音声",{left_x,234,left_w,63});
                draw_check(c,"BGMを再生",field_index("BGMを再生"),18,251,245);
                draw_check(c,"効果音を再生",field_index("効果音を再生"),18,272,245);
            });
        if(all_status_option)
            add("settings.compact.status-dialog",{left_x,300,left_w,45},
                signature({field_value("ステータスダイアログを表示")}),
                [&](SettingsCanvas& c){
                    draw_group(c,"ステータスダイアログ",{left_x,300,left_w,45});
                    draw_check(c,"表示する",field_index("ステータスダイアログを表示"),18,317,245);
                });
        const auto drawing_text=[&](int id){const auto value=drawing_value(*this,id);return value?value_state_string(*value):String{};};
        add("settings.compact.expand",{right_x,8,right_w,134},signature({simple&&simple->m_expand?value_state_string(simple->m_expand->m_ch_expanddrawing):String{},simple&&simple->m_expand?value_state_string(simple->m_expand->m_sl_expand):String{},simple&&simple->m_expand?value_state_string(simple->m_expand->m_cb_fullscreen):String{},simple&&simple->m_expand?value_state_string(simple->m_expand->m_cb_smoothexpand):String{},simple&&simple->m_expand?value_state_string(simple->m_expand->m_st_expand):String{}}),
            [&](SettingsCanvas& c){draw_group(c,"拡大表示方式(F4キーで拡大)",{right_x,8,right_w,134});if(simple&&simple->m_expand)settings_expand_presentation(c,*simple->m_expand,{right_x+12,27,right_w-24,112});});
        add("settings.compact.transition",{right_x,145,right_w,96},signature({drawing_text(drawing_transition),drawing_text(drawing_speed_first+3)}),
            [&](SettingsCanvas& c){draw_group(c,"背景の切り替え方式(速い⇔遅い)",{right_x,145,right_w,96});if(simple&&simple->m_speed){c.choice(drawing_transition,{right_x+12,168,right_w-24,24});settings_speed_slider(c,drawing_speed_first+3,{right_x+12,204,right_w-24,24});}});
        add("settings.compact.card-speed",{right_x,244,right_w,51},signature({drawing_text(drawing_speed_first)}),
            [&](SettingsCanvas& c){draw_group(c,"カード描画速度(速い⇔遅い)",{right_x,244,right_w,51});if(simple&&simple->m_speed)settings_speed_slider(c,drawing_speed_first,{right_x+12,265,right_w-24,24});});
        add("settings.compact.message-speed",{right_x,298,right_w,54},signature({drawing_text(drawing_speed_first+2)}),
            [&](SettingsCanvas& c){draw_group(c,"メッセージ表示速度(速い⇔遅い)",{right_x,298,right_w,54});if(simple&&simple->m_speed)settings_speed_slider(c,drawing_speed_first+2,{right_x+12,320,right_w-24,24});});
        add("settings.compact.footer",{0,height-42,width,42},signature({std::to_string(apply_enabled),std::to_string(is_applying()),settings_presentation_version_text(),std::to_string(m_version_text.signature())}),
            [&](SettingsCanvas& c){
                c.button("詳細設定...",1200,{12,height-36,100,28});
                settings_presentation_version(*this,c.surface,{122,height-36,width-399,28},c.origin);
                c.hit(9500,{122,height-36,width-399,28},true);c.result.controls.back().continuous_pointer=true;
                const auto button=[&](const String& text,Action action,int index,::cw::lib::Rect rect,bool enabled){
                    if (!c.paint) { add_control(c,action,index,rect,enabled); return; }
                    if(c.paint)::cw::lib::ui::draw_button_face(c.surface,c.local(rect),{enabled});
                    const auto old=c.font;c.font=settings_final_font(*this,"button",15,true);c.button_label(text,rect);c.font=old;
                    add_control(c,action,index,rect,enabled);
                };
                button("OK",Action::Accept,0,{width-265,height-36,76,28},!is_applying());
                button("適用",Action::Toggle,0,{width-179,height-36,76,28},apply_enabled);
                button("キャンセル",Action::Cancel,0,{width-93,height-36,81,28},!is_applying());
            });

        // まず描画せずにコントロールを公開する。 ピクセルを保持している
        // 場合も、ポップアップの配置は現在の所有元の選択欄に依存する。
        ::cw::dialog::DialogPresentation layout;
        for(auto& component:components) {
            SettingsCanvas c{*this,layout,nullptr,{component.rect.x,component.rect.y},false};
            component.present(c);
        }
        result.controls=std::move(layout.controls); result.wants_text_input=layout.wants_text_input;
        result.input_text=std::move(layout.input_text);
const auto scale=settings_final_scale(*this);
        Canvas canvas(dialog_retained_surface(),{width,height},scale);
        for(auto& component:components)
            component.changed=canvas.component_changed(component.key,component.rect,component.signature);
        SettingsChoiceGeometry popup_geometry;
        bool popup_changed=false;
        std::optional<::cw::lib::Rect> erased_popup;
        if(m_settings_popup) {
            popup_geometry=settings_choice_geometry(*this,result,{width,height});
            std::uint64_t popup_signature=signature({std::to_string(m_settings_popup),std::to_string(m_settings_choice)});
            for(const auto& choice:settings_choices(*this,m_settings_popup)) popup_signature^=std::hash<String>{}(choice)+0x9e3779b97f4a7c15ULL+(popup_signature<<6U)+(popup_signature>>2U);
            popup_changed=canvas.component_changed("settings.compact.popup",popup_geometry.rect,popup_signature);
            settings_choices_presentation(*this,result,nullptr,{width,height},{},false);
        } else {
            erased_popup=dialog_retained_surface().component("settings.compact.popup");
            (void)dialog_retained_surface().remove_component("settings.compact.popup");
            dialog_retained_surface().set_component_hits("dropdown.rows",{});
        }
        // RetainedSurface はポップアップの更新領域を未
        // 描画の背景から復元する。 一覧を閉じたら、覆っていた簡易設定
        // の全構成要素を再描画し、元の一覧の下に空白の矩形を残さない。
        if(erased_popup) {
            const auto overlap=[](const ::cw::lib::Rect a,const ::cw::lib::Rect b) {
                return a.x<b.x+b.width&&a.x+a.width>b.x&&
                       a.y<b.y+b.height&&a.y+a.height>b.y;
            };
            for(auto& component:components)
                if(overlap(canvas.rect(component.rect),*erased_popup)) component.changed=true;
        }
        canvas.capture_base_and_restore();
        for(auto& component:components) if(component.changed) {
            auto layer=std::make_shared<::cw::lib::Surface>(); const auto final_rect=canvas.rect(component.rect); layer->size={final_rect.width,final_rect.height};
            layer->rgba.assign(static_cast<std::size_t>(layer->size.width)*layer->size.height*4U,0);
            ::cw::lib::ui::fill_rect(layer,{0,0,layer->size.width,layer->size.height},{240,240,240,255});
            ::cw::dialog::DialogPresentation discard;
            SettingsCanvas c{*this,discard,layer,{component.rect.x,component.rect.y},true}; component.present(c);
            canvas.composite(component.rect,layer);
        }
        if(m_settings_popup && popup_changed) {
            auto layer=std::make_shared<::cw::lib::Surface>();const auto final_rect=canvas.rect(popup_geometry.rect);layer->size={final_rect.width,final_rect.height};
            layer->rgba.assign(static_cast<std::size_t>(layer->size.width)*layer->size.height*4U,0);
            ::cw::dialog::DialogPresentation discard;
            // ポップアップの描画位置は所有元の選択欄から求め
            // る。保持型の再描画は使い捨ての Presentation
            // を使うため、ここでは計算済みの配置を維持す
            // る。 配置を失うと (24,40) に戻る一
            // 方でレイヤーは実際の選択欄の位置へ合成され
            // 、等倍以外で大きな空白や位置ずれが生じる。
            discard.controls = result.controls;
            settings_choices_presentation(*this,discard,layer,{width,height},{popup_geometry.rect.x,popup_geometry.rect.y},true);
            canvas.composite(popup_geometry.rect,layer);
        }
        canvas.finish();
        if(document_request_pending()) {for(auto& control:result.controls)control.enabled=false;result.wants_text_input=false;}
        if(scale>1.0+1e-9) for(auto& control:result.controls) {control.x*=scale;control.y*=scale;control.width*=scale;control.height*=scale;}
        result.surface=canvas.surface;result.damage=canvas.retained.damage;
        result.surface_revision=canvas.retained.revision;
        return result;
    }

    using Canvas=::cw::lib::ui::ScaledRetainedCanvas;
    const auto panel=detail_panel(m_panel);
    using Presenter=std::function<void(::cw::dialog::DialogPresentation&,const std::shared_ptr<::cw::lib::Surface>&,::cw::lib::Point,bool)>;
    Presenter page;
    std::vector<::cw::lib::Rect> regions;
    if(m_presentation_tab==0&&panel->m_pane_gene) {page=[&](auto& p,const auto& s,auto o,bool paint){settings_general_presentation(*this,*panel->m_pane_gene,p,s,o,paint);};regions={{12,48,344,87},{12,143,344,70},{12,221,344,66},{12,295,344,218},{366,48,342,104},{366,160,342,130},{366,299,342,214}};}
    else if(m_presentation_tab==1&&panel->m_pane_draw) {page=[&](auto& p,const auto& s,auto o,bool paint){settings_drawing_presentation(*this,*panel->m_pane_draw,p,s,o,paint);};if(value_bool(panel->m_pane_draw->m__for_local))regions={{12,48,696,22},{12,76,696,66},{12,152,696,66},{12,228,696,66},{12,304,696,66},{12,384,696,100},{478,502,230,20}};else regions={{12,76,340,116},{12,204,340,87},{12,304,340,56},{12,372,340,83},{12,466,340,56},{366,76,342,434}};}
    else if(m_presentation_tab==2&&panel->m_pane_sound) {page=[&](auto& p,const auto& s,auto o,bool paint){settings_audio_presentation(*this,*panel->m_pane_sound,p,s,o,paint);};regions={{12,48,344,72},{12,132,344,60},{12,198,344,60},{12,264,344,60},{12,330,344,60},{366,48,342,306}};}
    else if(m_presentation_tab==3&&panel->m_pane_font) {page=[&](auto& p,const auto& s,auto o,bool paint){settings_font_presentation(*this,*panel->m_pane_font,p,s,o,paint);};regions={{12,48,340,119},{12,175,340,130},{366,76,342,66},{366,149,342,156},{12,312,696,188}};if(value_bool(panel->m_pane_font->m__for_local))regions.push_back({478,502,230,20});}
    else if(m_presentation_tab==4&&panel->m_pane_scenario) {page=[&](auto& p,const auto& s,auto o,bool paint){settings_scenario_presentation(*this,*panel->m_pane_scenario,p,s,o,paint);};regions={{12,48,696,158},{12,216,696,178},{12,404,696,109}};}
    else if(m_presentation_tab==5&&panel->m_pane_ui) {page=[&](auto& p,const auto& s,auto o,bool paint){settings_ui_presentation(*this,*panel->m_pane_ui,p,s,o,paint);};regions={{12,48,696,465}};}
    else if(m_presentation_tab==status_dialog_tab&&panel->m_pane_ui) {page=[&](auto& p,const auto& s,auto o,bool paint){settings_status_dialog_presentation(*this,*panel->m_pane_ui,p,s,o,paint);};regions={{12,48,696,321}};}
    // ネイティブの凹凸枠とグループ見出しは、公称のコントロール矩
    // 形から最大 2px はみ出して描く。 各保持レイヤーにそ
    // の余白を持たせ、クリッピングで片側が欠けないようにする。
    for(auto& rect:regions){rect.x=std::max<Integer>(0,rect.x-2);rect.y=std::max<Integer>(0,rect.y-2);rect.width=std::min<Integer>(width-rect.x,rect.width+4);rect.height=std::min<Integer>(height-rect.y,rect.height+4);}

    ::cw::dialog::DialogPresentation layout;
    if(page) page(layout,nullptr,{},false);
    result.controls=layout.controls;result.wants_text_input=layout.wants_text_input;result.input_text=layout.input_text;
    const auto mix=[](std::uint64_t& value,std::uint64_t part){value^=part+0x9e3779b97f4a7c15ULL+(value<<6U)+(value>>2U);};
    // 配置用コントロールには全編集値が含まれるわけではない。 音量スライ
    // ダー、SoundFont 行、フォントのセル、シナリオ行、UI
    // のスピン欄は、共通モデルの変更時にそれぞれ保持画像を無効にする。
    String tab_state=std::to_string(m_presentation_tab)+":"+m_scenario_edit_text+":";
    const auto append_value=[&](const Value& value){tab_state+=value_state_string(value)+'|';};
    if(m_presentation_tab==0 && panel->m_pane_gene) {
        const auto& pane=*panel->m_pane_gene;
        for(const auto* value:{&pane.m_ch_startupscene,&pane.m_ch_messagelog_type,&pane.m_sc_backlogmax,
                &pane.m_sc_initmoneyamount,&pane.m_tx_ssinfoformat,&pane.m_tx_ssfnameformat,
                &pane.m_tx_cardssfnameformat,&pane.m_ch_ssinfocolor,&pane.m_tx_ssinfobackimage}) append_value(*value);
        if(pane.m_expand) for(const auto* value:{&pane.m_expand->m_ch_expanddrawing,&pane.m_expand->m_sl_expand,
                &pane.m_expand->m_cb_fullscreen,&pane.m_expand->m_cb_smoothexpand,&pane.m_expand->m_st_expand}) append_value(*value);
        if(pane.m_skin) for(const auto* value:{&pane.m_skin->m_ch_skin,&pane.m_skin->m_tx_skin,
                &pane.m_skin->m_cb_show_allskin}) append_value(*value);
    } else if(m_presentation_tab==1 && panel->m_pane_draw) {
        const auto& pane=*panel->m_pane_draw;
        for(const auto* value:{&pane.m_cb_important,&pane.m_cb_smooth_bg,&pane.m_cb_smoothing_card_down,
                &pane.m_cb_smoothing_card_up,&pane.m_cb_whitecursor,&pane.m_ch_fscrbacktype,
                &pane.m_tx_fscrbackfile,&pane.m_cs_mwin,&pane.m_cs_blwin,&pane.m_cs_mframe,
                &pane.m_cs_blframe,&pane.m_cs_blcurtain,&pane.m_cs_curtain,&pane.m_sc_mwin,
                &pane.m_sc_mframe,&pane.m_sc_blcurtain,&pane.m_sc_curtain,
                &pane.m_cs_allengineframe,&pane.m_sc_allengineframe}) append_value(*value);
        if(pane.m_speed) for(const auto* value:{&pane.m_speed->m_ch_tran,&pane.m_speed->m_sl_deal,
                &pane.m_speed->m_sl_deal_battle,&pane.m_speed->m_sl_msgs,&pane.m_speed->m_sl_tran,
                &pane.m_speed->m_cb_use_battlespeed}) append_value(*value);
    } else if(m_presentation_tab==2 && panel->m_pane_sound) {
        const auto& pane=*panel->m_pane_sound;
        for(const auto* value:{&pane.m_cb_playbgm,&pane.m_cb_playsound,&pane.m_sl_master,
                &pane.m_sl_music,&pane.m_sl_midi,&pane.m_sl_sound}) append_value(*value);
        for(const auto& [path,enabled,volume]:pane.m__soundfonts)
            tab_state+=path+":"+(enabled?"1":"0")+":"+std::to_string(volume)+'|';
        for(const auto row:pane.m__selected_soundfont_rows) tab_state+="selected:"+std::to_string(row)+'|';
        tab_state+="cursor:"+std::to_string(m_soundfont_cursor)+":"+std::to_string(m_soundfont_edit_row)+":"+m_soundfont_edit_text;
    } else if(m_presentation_tab==3 && panel->m_pane_font) {
        const auto& pane=*panel->m_pane_font;
        for(const auto* value:{&pane.m_cb_important,&pane.m_cb_decorationfont,&pane.m_cb_bordering_cardname,
                &pane.m_cb_fontsmoothingcardname,&pane.m_cb_fontsmoothingmessage,
                &pane.m_cb_fontsmoothingstatusbar,&pane.m_tx_example,&pane.m_sc_example}) append_value(*value);
        for(const auto& name:pane.m__bases_order) if(const auto found=pane.m__basefont_rows.find(name);found!=pane.m__basefont_rows.end())
            tab_state+=name+":"+found->second+'|';
        const auto append_font_row=[&](const FontSettingRow& row) {tab_state+=row.m_fonttype+":"+row.m_face+":"+
            std::to_string(row.m_pixels)+":"+(row.m_bold?"1":"0")+(row.m_bold_upscr?"1":"0")+(row.m_italic?"1":"0")+'|';};
        for(const auto& [unused,row]:pane.m__fonttype_rows) append_font_row(row);
        for(const auto& [unused,row]:pane.m__msg_exfont_rows) append_font_row(row);
        tab_state+="focus:"+std::to_string(m_font_focus)+":"+std::to_string(m_font_scroll);
    } else if(m_presentation_tab==4 && panel->m_pane_scenario) {
        const auto& pane=*panel->m_pane_scenario;
        for(const auto* value:{&pane.m_cb_selectscenariofromtype,&pane.m_cb_show_paperandtree,
                &pane.m_cb_write_playlog,&pane.m_cb_can_installscenariofromdrop,
                &pane.m_cb_delete_sourceafterinstalled,&pane.m_cb_open_lastscenario,
                &pane.m_tx_editor,&pane.m_tx_filer_dir,&pane.m_tx_filer_file}) append_value(*value);
        for(const auto& [type,path]:pane.m__folderoftype) tab_state+=type+":"+path+'|';
        tab_state+="cursor:"+std::to_string(pane.m__folder_cursor)+":"+std::to_string(m_scenario_scroll);
    } else if(m_presentation_tab==5 && panel->m_pane_ui) {
        append_value(panel->m_pane_ui->m_ch_show_statustime);
        append_value(panel->m_pane_ui->m_ch_confirm_dumpcard);
        append_value(panel->m_pane_ui->m_ch_confirm_beforesaving);
        append_value(panel->m_pane_ui->m_sc_radius_notdetectmovement);
        append_value(panel->m_pane_ui->m_sc_pc_skill_limit);
        append_value(panel->m_pane_ui->m_sc_pc_personal_limit);
        append_value(panel->m_pane_ui->m_sc_pc_beast_limit);
        tab_state+="focus:"+std::to_string(m_ui_focus)+":"+std::to_string(m_ui_scroll);
    } else if(m_presentation_tab==status_dialog_tab && panel->m_pane_ui) {
        for(const auto& value:panel->m_pane_ui->m_ch_all_shortcuts) append_value(value);
        tab_state+="custom:"+panel->m_pane_ui->m_all_custom_status;
    }
    const auto state_for=[&](const ::cw::dialog::DialogPresentationControl& control) {
        String state=std::to_string(control.index)+":"+control.label+":"+control.tooltip+":"+std::to_string(control.enabled);
        if(control.index>=0&&control.index<static_cast<int>(fields.size())) state+=":"+std::get<1>(fields[control.index])();
        if(const auto value=settings_choice_value(*this,control.index)) state+=":"+value_state_string(*value);
        if(const auto value=drawing_value(*this,control.index)) state+=":"+value_state_string(*value);
        return state;
    };
    const auto region_signature=[&](const ::cw::lib::Rect rect) {
        std::uint64_t value=std::hash<int>{}(m_presentation_tab);
        mix(value,std::hash<String>{}(tab_state));
        for(const auto& control:layout.controls) {
            const ::cw::lib::Rect hit{static_cast<Integer>(control.x),static_cast<Integer>(control.y),static_cast<Integer>(control.width),static_cast<Integer>(control.height)};
            if(hit.x<rect.x+rect.width&&hit.x+hit.width>rect.x&&hit.y<rect.y+rect.height&&hit.y+hit.height>rect.y)
                mix(value,std::hash<String>{}(state_for(control)));
        }
        if(rect.x<366) {mix(value,m_ui_scroll);mix(value,m_font_scroll);mix(value,m_soundfont_scroll);mix(value,m_scenario_scroll);}
        if(rect.y>=295) {mix(value,std::hash<String>{}(m_scenario_edit_text));mix(value,m_skin_description_scroll);mix(value,m_skin_description_text.signature());}
        if(m_settings_popup||m_scenario_type_popup) mix(value,1);
        // インラインの種別ポップアップを所有するのはシナリオ表だけである。 ホバ
        // ー変更ではその構成要素だけを再描画し、設定グループ全体を無効にしない。
        if(m_scenario_type_popup && m_presentation_tab==4 &&
           rect.y<394 && rect.y+rect.height>216)
            mix(value,static_cast<std::uint64_t>(m_scenario_type_choice+2));
        return value;
    };
const auto scale=settings_final_scale(*this);
    const bool local_only=m_local_panel_only;
    const auto view_rect=[&](::cw::lib::Rect rect) {
        if(!local_only)return rect;
        const auto top=std::max<Integer>(rect.y,local_panel_top),bottom=std::min<Integer>(rect.y+rect.height,local_panel_top+local_panel_height);
        return ::cw::lib::Rect{rect.x,top-local_panel_top,rect.width,std::max<Integer>(0,bottom-top)};
    };
    Canvas canvas(dialog_retained_surface(),{width,local_only?local_panel_height:height},scale);
    std::vector<bool> changed;changed.reserve(regions.size());
    for(std::size_t i=0;i<regions.size();++i) changed.push_back(canvas.component_changed("settings.detail."+String(local_only?"local.":"")+"tab."+std::to_string(m_presentation_tab)+".region."+std::to_string(i),view_rect(regions[i]),region_signature(regions[i])));
    const ::cw::lib::Rect tabs_rect{0,0,width,42};
    const bool tabs_changed=!local_only&&canvas.component_changed("settings.detail.tabs",tabs_rect,static_cast<std::uint64_t>(m_presentation_tab));
    const ::cw::lib::Rect footer_rect{0,height-36,width,36};
    const bool footer_changed=!local_only&&canvas.component_changed("settings.detail.footer",footer_rect,std::hash<String>{}(settings_presentation_version_text()+std::to_string(apply_enabled)+std::to_string(is_applying())+std::to_string(m_version_text.signature())));
    SettingsChoiceGeometry popup_geometry;bool popup_changed=false;std::optional<::cw::lib::Rect> erased_popup;
    if(m_settings_popup){popup_geometry=settings_choice_geometry(*this,result,{width,height});std::uint64_t popup_signature=std::hash<int>{}(m_settings_popup);mix(popup_signature,m_settings_choice);for(const auto& choice:settings_choices(*this,m_settings_popup))mix(popup_signature,std::hash<String>{}(choice));popup_changed=canvas.component_changed("settings.detail.popup",view_rect(popup_geometry.rect),popup_signature);settings_choices_presentation(*this,result,nullptr,{width,height},{},false);}
    else {erased_popup=dialog_retained_surface().component("settings.detail.popup");(void)dialog_retained_surface().remove_component("settings.detail.popup");dialog_retained_surface().set_component_hits("dropdown.rows",{});}
    const auto overlap=[](const ::cw::lib::Rect a,const ::cw::lib::Rect b){return a.x<b.x+b.width&&a.x+a.width>b.x&&a.y<b.y+b.height&&a.y+a.height>b.y;};
    if(popup_changed)for(std::size_t i=0;i<regions.size();++i)if(overlap(regions[i],popup_geometry.rect))changed[i]=true;
    if(erased_popup)for(std::size_t i=0;i<regions.size();++i)if(overlap(canvas.rect(view_rect(regions[i])),*erased_popup))changed[i]=true;
    canvas.capture_base_and_restore();
    for(std::size_t i=0;i<regions.size();++i) if(changed[i]&&page) {const auto rect=regions[i];const auto visible=view_rect(rect);if(visible.height<=0)continue;auto layer=std::make_shared<::cw::lib::Surface>();const auto final_rect=canvas.rect(visible);layer->size={final_rect.width,final_rect.height};layer->rgba.assign(static_cast<std::size_t>(layer->size.width)*layer->size.height*4U,0);::cw::lib::ui::fill_rect(layer,{0,0,layer->size.width,layer->size.height},{240,240,240,255});::cw::dialog::DialogPresentation discard;page(discard,layer,{rect.x,rect.y},true);canvas.composite(visible,layer);}
    const auto paint_tabs=[&](auto& target,bool paint){SettingsCanvas c{*this,target,paint?std::make_shared<::cw::lib::Surface>():nullptr,{0,0},paint};if(paint){c.surface->size=c.final_size({width,42});c.surface->rgba.assign(static_cast<std::size_t>(c.surface->size.width)*c.surface->size.height*4U,0);::cw::lib::ui::fill_rect(c.surface,{0,0,c.surface->size.width,c.surface->size.height},{240,240,240,255});}const auto font=settings_final_font(*this,"tab",13);Integer x=10;static const std::array<String,7> all_names{"一般","描画","音声","フォント","シナリオ","詳細","ステータスダイアログ"};const std::size_t tab_count=static_cast<std::size_t>(settings_tab_count());for(std::size_t i=0;i<tab_count;++i){const auto& names=all_names;const auto text_width=c.logical(::cw::lib::ui::label_width(names[i],font.pixels,font.bold,font.face,font.italic));const auto w=std::max<Integer>(44,text_width+20);const ::cw::lib::Rect r{x,8,w,28};c.fill(r,static_cast<Integer>(i)==m_presentation_tab ? ::cw::lib::Color{255,255,255,255} : ::cw::lib::Color{240,240,240,255});if(paint)::cw::lib::ui::draw_raised_border(c.surface,c.local(r));const auto old=c.font;c.font=font;c.label(names[i],r.x+(r.width-text_width)/2,r.y+(r.height-c.logical(font.pixels))/2);c.font=old;c.hit(1000+static_cast<int>(i),r);x+=w+2;}return c.surface;};
    if(!local_only){::cw::dialog::DialogPresentation tabs_layout;paint_tabs(tabs_layout,false);result.controls.insert(result.controls.end(),tabs_layout.controls.begin(),tabs_layout.controls.end());if(tabs_changed){::cw::dialog::DialogPresentation discard;canvas.composite(tabs_rect,paint_tabs(discard,true));}}
    const auto paint_footer=[&](auto& target,bool paint){auto layer=paint?std::make_shared<::cw::lib::Surface>():nullptr;SettingsCanvas c{*this,target,layer,{0,height-36},paint};if(paint){layer->size=c.final_size({width,36});layer->rgba.assign(static_cast<std::size_t>(layer->size.width)*layer->size.height*4U,0);::cw::lib::ui::fill_rect(layer,{0,0,layer->size.width,layer->size.height},{240,240,240,255});}c.button("既定値",1300,{12,height-36,76,28});c.button("読込",1301,{98,height-36,76,28});c.button("保存",1302,{184,height-36,76,28});settings_presentation_version(*this,layer,{270,height-36,width-543,28},{0,height-36});c.hit(9500,{270,height-36,width-543,28},true);c.result.controls.back().continuous_pointer=true;const auto native=[&](String text,Action action,::cw::lib::Rect r,bool enabled){c.button(text,action==Action::Accept?1400:action==Action::Toggle?1401:1402,r,enabled);c.result.controls.back().action=action;c.result.controls.back().index=0;};native("OK",Action::Accept,{width-265,height-36,76,28},!is_applying());native("適用",Action::Toggle,{width-179,height-36,76,28},apply_enabled);native("キャンセル",Action::Cancel,{width-93,height-36,81,28},!is_applying());return layer;};
    if(!local_only){::cw::dialog::DialogPresentation footer_layout;paint_footer(footer_layout,false);result.controls.insert(result.controls.end(),footer_layout.controls.begin(),footer_layout.controls.end());if(footer_changed){::cw::dialog::DialogPresentation discard;canvas.composite(footer_rect,paint_footer(discard,true));}}
    if(m_settings_popup&&popup_changed){const auto visible=view_rect(popup_geometry.rect);auto layer=std::make_shared<::cw::lib::Surface>();const auto final_rect=canvas.rect(visible);layer->size={final_rect.width,final_rect.height};layer->rgba.assign(static_cast<std::size_t>(layer->size.width)*layer->size.height*4U,0);::cw::dialog::DialogPresentation discard;discard.controls=result.controls;settings_choices_presentation(*this,discard,layer,{width,height},{popup_geometry.rect.x,popup_geometry.rect.y},true);canvas.composite(visible,layer);}
    canvas.finish();if(local_only){for(auto& control:result.controls){const auto top=std::max(control.y,static_cast<float>(local_panel_top));const auto bottom=std::min(control.y+control.height,static_cast<float>(local_panel_top+local_panel_height));control.y=top-local_panel_top;control.height=bottom-top;}std::erase_if(result.controls,[](const auto& control){return control.height<=0;});}if(document_request_pending()){for(auto& control:result.controls)control.enabled=false;result.wants_text_input=false;}if(scale>1.0+1e-9)for(auto& control:result.controls){control.x*=scale;control.y*=scale;control.width*=scale;control.height*=scale;}result.surface=canvas.surface;result.damage=canvas.retained.damage;result.surface_revision=canvas.retained.revision;return result;
}

void SettingsDialog::dialog_presentation_action(
    const ::cw::dialog::DialogPresentationAction action, const int index)
{
    using Action = ::cw::dialog::DialogPresentationAction;
    if (m_state.load(std::memory_order_acquire) != SettingsDialogState::Visible) return;
    if (document_request_pending()) {
        if (action == Action::Cancel) OnClose(nullptr);
        return;
    }
    if(m_version_text.action(action,index,[](const String& text){::cw::dialog::presentation_set_clipboard_text(text);})) {
        dialog_retained_surface().invalidate_all();request_dialog_presentation_refresh();return;
    }
    if(m_skin_description_text.action(action,index,[](const String& text){::cw::dialog::presentation_set_clipboard_text(text);})) {
        dialog_retained_surface().invalidate_all();request_dialog_presentation_refresh();return;
    }
    request_dialog_presentation_refresh();
    if(m_settings_popup) {
        const auto kind=m_settings_popup;
        const auto choices=settings_choices(*this,kind);
        const int selected=action==Action::Accept?m_settings_choice:index-settings_choice_first;
        m_settings_popup=0;
        if((action==Action::Accept || action==Action::Auxiliary) && selected>=0 && selected<static_cast<int>(choices.size())) {
            if(kind==general_tokens) {
                if(m_scenario_edit<6102 || m_scenario_edit>6104) return;
                const auto field=m_scenario_edit;
                const auto& token=choices[selected];
                dialog_presentation_text(token.starts_with("\\")?token:"%"+token+"%",false);
                const auto caret=m_scenario_caret;
                commit_scenario_edit();
                begin_scenario_edit(field);
                m_scenario_caret=caret;
                m_scenario_select_all=false;
            } else if(kind>=font_base_first && kind<font_example) {
                font_choose(*this,kind,choices[selected]);
            } else if(auto value=settings_choice_value(*this,kind)) {
                if(value_integer(*value)!=selected) {
                    *value=static_cast<Integer>(selected);
                    if(kind==skin_choice) { settings_skin(*this)->OnSkinChoice(nullptr); m_skin_description_scroll=0; }
                    if(kind==general_expand) settings_expand(*this)->OnExpandChange(nullptr);
                    if(kind==drawing_background) settings_drawing(*this)->OnFullScreenBackgroundType(nullptr);
                    applied();
                }
            }
        }
        return;
    }
    if (m_scenario_type_popup) {
        const auto types=scenario_types_for(*this);
        const int choice=action==Action::Accept ? m_scenario_type_choice : index-scenario_type_first;
        m_scenario_type_popup=false;
        if ((action==Action::Accept || action==Action::Auxiliary) && choice>=0 && choice<static_cast<int>(types.size())) {
            m_scenario_edit_text=types[choice];
            commit_scenario_edit();
        }
        return;
    }
    if (m_scenario_edit>=0 && !(action==Action::Auxiliary && index==general_tokens)) {
        if (action==Action::Cancel) { m_scenario_edit=-1; return; }
        commit_scenario_edit();
        if (action==Action::Accept) return;
    }
    if (m_soundfont_edit_row >= 0) {
        if (action == Action::Cancel) { m_soundfont_edit_row = -1; return; }
        commit_soundfont_edit();
        if (action == Action::Accept) return;
    }
    const auto detail = detail_panel(m_panel);
    // ここはパネルだけであり、OK・キャンセル・適用はタブ画面の所有者が処理する。
    if(m_local_panel_only && action!=Action::Auxiliary) return;
    if(detail && m_presentation_tab==status_dialog_tab && detail->m_pane_ui && action==Action::Auxiliary &&
       index>=ui_shortcut_first && index<=ui_shortcut_last) {
        m_settings_popup=index; m_settings_choice=static_cast<int>(value_integer(*ui_value(*this,index))); return;
    }
    if(detail && m_presentation_tab==status_dialog_tab && detail->m_pane_ui && action==Action::Auxiliary &&
       index==ui_custom_status) {
        // ALL 専用の、登録モードのデバッガー状態編集。対象は
        // 「全員」に固定し、「登録」「キャンセル」だけを表示する
        // 。 値は OK・適用時に他の設定とまとめて確定する。
        auto parent=std::make_shared<::cw::lib::Window>();
        parent->native_handle=m_native_handle;
        auto editor=std::make_shared<::cw::debug::statusedit::StatusEditDialog>();
        editor->initialize_register(parent,detail->m_pane_ui->m_all_custom_status);
        bool registered=false;
        editor->m_on_register=[pane=detail->m_pane_ui,&registered](const String& text){
            pane->m_all_custom_status=text; registered=true;
        };
        (void)editor->show_modal();
        if(registered) applied();
        return;
    }
    if(detail && m_presentation_tab==5 && detail->m_pane_ui && action==Action::Auxiliary) {
        if((index>=0 && index<static_cast<int>(ui_options.size())) || (index>=ui_status_time && index<=ui_radius_down))
            ui_focus_control(*this,index);
        if(index>=ui_status_time && index<=ui_confirm_save) {
            m_settings_popup=index; m_settings_choice=static_cast<int>(value_integer(*ui_value(*this,index))); return;
        }
        if(index>=ui_skill_limit && index<=ui_beast_down) {
            if(detail->m_pane_ui->m_pc_limits_inn.expired() || !value_bool(detail->m_pane_ui->m_cb_pc_card_limits)) return;
            if(index==ui_skill_limit || index==ui_personal_limit || index==ui_beast_limit) {
                begin_scenario_edit(index); return;
            }
            const int field=index<=ui_skill_down?ui_skill_limit:
                            index>=ui_beast_limit?ui_beast_limit:ui_personal_limit;
            auto value=ui_value(*this,field);
            const bool up=index==ui_skill_up || index==ui_personal_up || index==ui_beast_up;
            // カード上限は循環させ、1 で減らすと 50、50 で増やすと 1 にする。
            const Integer current=std::clamp<Integer>(value_integer(*value),1,50);
            *value=up?(current>=50?Integer{1}:current+1):(current<=1?Integer{50}:current-1);
            ui_focus_control(*this,field); applied(); return;
        }
        if(index==ui_radius) { begin_scenario_edit(index); return; }
        if(index==ui_radius_up || index==ui_radius_down) {
            auto& value=detail->m_pane_ui->m_sc_radius_notdetectmovement;
            value=std::clamp<Integer>(value_integer(value)+(index==ui_radius_up?1:-1),0,50);
            applied(); return;
        }
    }
    const bool drawing_page=simple_panel(m_panel) || (detail && m_presentation_tab==1);
    if(drawing_page && action==Action::Auxiliary && index>=0 && index<=5) m_drawing_focus=index;
    if(drawing_page && action==Action::Auxiliary && index>=8000 && index<=drawing_copy) {
        if(!drawing_enabled(*this,index)) return;
        m_drawing_focus=index;
        if(index==drawing_transition || index==drawing_background) {
            if(const auto value=drawing_value(*this,index)) {
                m_settings_popup=index; m_settings_choice=static_cast<int>(value_integer(*value));
            }
        } else if(drawing_editable(index)) begin_scenario_edit(index);
        else if(index>=drawing_spin_first && index<drawing_spin_first+10) {
            const auto value=drawing_value(*this,drawing_alpha_first+(index-drawing_spin_first)/2);
            if(value) { *value=std::clamp<Integer>(value_integer(*value)+((index-drawing_spin_first)%2?-1:1),0,255); applied(); }
        } else if(index>=drawing_colour_first && index<drawing_colour_first+7) {
            if(const auto value=drawing_value(*this,index)) {
                const auto previous=value_colour(*value,Colour{0,0,0,255});
                auto picker=std::make_shared<::cw::dialog::ColourSelectionDialog>(::cw::lib::Color{
                    static_cast<std::uint8_t>(std::get<0>(previous)),static_cast<std::uint8_t>(std::get<1>(previous)),
                    static_cast<std::uint8_t>(std::get<2>(previous)),static_cast<std::uint8_t>(std::get<3>(previous))});
                (void)::cw::dialog::show_common_modal_presentation(picker);
                if(picker->accepted()) {
                    const auto colour=picker->colour();
                    *value=colour_value({colour.r,colour.g,colour.b,colour.a}); applied();
                }
            }
        } else if(index==drawing_browse) {
            request_document(SettingsDocumentAction::FullscreenBackground);
        } else if(const auto pane=settings_drawing(*this)) {
            if(index==drawing_defaults) { pane->OnInitValue(nullptr); applied(); }
            if(index==drawing_copy) { pane->OnCopyBase(nullptr); applied(); }
        }
        return;
    }
    if(detail && m_presentation_tab==3 && detail->m_pane_font && action==Action::Auxiliary) {
        auto& pane=*detail->m_pane_font;
        if((index>=7500 && index<7500+static_cast<int>(pane.m__bases_order.size())) ||
           (index>=7600 && index<7600+static_cast<int>(pane.m__types.size()))) {
            if(pane.m__controls_enabled)
                font_select(*this,pane,index>=7600?font_type_first+(index-7600)*5:font_base_first+index-7500);
            return;
        }
        if(index==0) {
            pane.m_cb_important=!value_bool(pane.m_cb_important); pane.OnImportant(nullptr); applied(); return;
        }
        if(index<1000 || (index>=font_base_first && index<=7407)) {
            if(!pane.m__controls_enabled) return;
            if(index>=font_base_first && index<font_example) {
                font_select(*this,pane,index);
                const auto choices=settings_choices(*this,index);
                if(!choices.empty()) {
                    m_settings_popup=index;
                    const auto selected=std::find(choices.begin(),choices.end(),font_face_label(pane,index));
                    m_settings_choice=selected==choices.end()?0:static_cast<int>(selected-choices.begin());
                } else if(font_numeric_field(pane,index)) begin_scenario_edit(index);
                else if(auto cell=font_cell(pane,index)) {
                    const int col=(index-font_type_first)%5;
                    if(col==2) cell->m_bold=!cell->m_bold;
                    if(col==3) cell->m_bold_upscr=!cell->m_bold_upscr;
                    if(col==4) cell->m_italic=!cell->m_italic;
                    if(col>=2) { pane.OnCellChangeType(nullptr); applied(); }
                }
                return;
            }
            if(index==font_example || index==font_example_size) { begin_scenario_edit(index); return; }
            if(index==font_example_reset) { pane.OnInitExample(nullptr); applied(); return; }
            if(index==font_defaults) { pane.OnInitValue(nullptr); applied(); return; }
            if(index==font_copy) { pane.OnCopyBase(nullptr); applied(); return; }
            if(index==7406 || index==7407) {
                pane.m_sc_example=std::clamp<Integer>(value_integer(pane.m_sc_example,24)+(index==7406?1:-1),8,32);
                pane.OnExampleText(nullptr); applied(); return;
            }
        }
    }
    const bool skin_page=simple_panel(m_panel) || (detail && m_presentation_tab==0);
    m_expand_focus=-1;
    if(skin_page && action==Action::Auxiliary) {
        if(index==general_expand || index==general_display || index==general_fullscreen || index==general_smoothing) {
            const auto expand=settings_expand(*this);
            if(!expand) return;
            m_expand_focus=index; m_drawing_focus=-1;
            if(index==general_expand) {
                m_settings_popup=index; m_settings_choice=static_cast<int>(value_integer(expand->m_ch_expanddrawing));
            } else if(index==general_fullscreen) {
                expand->m_cb_fullscreen=!value_bool(expand->m_cb_fullscreen);
                expand->OnExpandChange(nullptr); applied();
            } else if(index==general_smoothing && value_bool(expand->m_options)) {
                expand->m_cb_smoothexpand=!value_bool(expand->m_cb_smoothexpand); applied();
            }
            return;
        }
        if(index==skin_choice || (detail && (index==general_startup || index==general_log || index==general_color || index==general_tokens))) {
            // トークンは現在フォーカスのある書式欄へ挿入する。 フ
            // ォーカス移動後に、以前の欄や既定の欄へ挿入しない。
            if(index==general_tokens && (m_scenario_edit<6102 || m_scenario_edit>6104)) return;
            m_settings_popup=index;
            const auto value=settings_choice_value(*this,index);
            m_settings_choice=value?static_cast<int>(value_integer(*value)):0;
            return;
        }
        if(detail && general_edit_value(*this,index)) { begin_scenario_edit(index); return; }
        if(detail && index==general_background) {
            request_document(SettingsDocumentAction::BackgroundImage);
            return;
        }
        if(detail && index>=skin_install && index<=skin_show_all) {
            const auto skin=settings_skin(*this);
            if(!skin || !value_bool(skin->m_editbuttons)) return;
            const auto selected=value_integer(skin->m_ch_skin,-1);
            const bool valid=selected>=0 && selected<static_cast<Integer>(skin->m_skindirs.size());
            const auto previous_skin=valid?value_string(skin->m_skindirs[selected]):String{};
            if(index>=skin_edit && index<=skin_delete && !valid) return;
            try {
                if(index==skin_install) { request_document(SettingsDocumentAction::SkinArchive); return; }
                else if(index==skin_convert) skin->OnConvertSkin(nullptr);
                else if(index==skin_edit) skin->OnEditSkin(nullptr);
                else if(index==skin_copy) skin->OnCopySkin(nullptr);
                else if(index==skin_delete) skin->OnDeleteSkin(nullptr);
                else if(index==skin_show_all && ::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata) {
                    skin->m_cb_show_allskin=!value_bool(skin->m_cb_show_allskin); skin->OnShowAllSkin(nullptr);
                }
            } catch(const std::exception& error) {
                (void)::cw::dialog::message::show_common_modal_message("スキン操作",String("操作に失敗しました。\n")+error.what(),false);
            }
            m_skin_description_scroll=0;
            const auto current=value_integer(skin->m_ch_skin,-1);
            if(current>=0 && current<static_cast<Integer>(skin->m_skindirs.size()) &&
                value_string(skin->m_skindirs[current])!=previous_skin) applied();
            request_dialog_presentation_refresh();
            return;
        }
    }
    if (detail && m_presentation_tab==4 && detail->m_pane_scenario && action==Action::Auxiliary) {
        auto& pane=*detail->m_pane_scenario;
        if ((index>=scenario_cell_first && index<scenario_cell_first+static_cast<int>(pane.m__folderoftype.size())*2) ||
            (index>=scenario_application_first && index<scenario_application_first+3)) {
            begin_scenario_edit(index);
            return;
        }
        if (index>=scenario_check_first && index<scenario_check_first+6) {
            auto& value=*scenario_check(pane,index-scenario_check_first);
            value=!value_bool(value); applied(); return;
        }
        if (index>=scenario_browse_first && index<scenario_browse_first+3) {
            request_document(static_cast<SettingsDocumentAction>(
                static_cast<int>(SettingsDocumentAction::EditorApplication)+index-scenario_browse_first));
            return;
        }
        if (index==scenario_types && pane.m__folder_cursor>=0) {
            begin_scenario_edit(scenario_cell_first+static_cast<int>(pane.m__folder_cursor)*2);
            m_scenario_type_choice=0;
            m_scenario_type_popup=!scenario_types_for(*this).empty();
            return;
        }
        if (index>=scenario_ref && index<=scenario_db) {
            const auto before=pane.m__folderoftype;
            const bool moved = (index==scenario_up && pane.m__up_folder_enabled) ||
                               (index==scenario_down && pane.m__down_folder_enabled);
            if(index==scenario_ref && pane.m__ref_folder_enabled) pane.OnRefFolderBtn(nullptr);
            else if(index==scenario_remove && pane.m__remove_folder_enabled) pane.OnRemoveFolderBtn(nullptr);
            else if(index==scenario_up && pane.m__up_folder_enabled) pane.OnUpFolderBtn(nullptr);
            else if(index==scenario_down && pane.m__down_folder_enabled) pane.OnDownFolderBtn(nullptr);
            else if(index==scenario_db) pane.OnConstructDBBtn(nullptr);
            if (moved) {
                // 選択行の並べ替え後は、wx の MakeCellVisible と同様に表示範囲へ移す。
                if (pane.m__folder_cursor<m_scenario_scroll)
                    m_scenario_scroll=pane.m__folder_cursor;
                else if (pane.m__folder_cursor>=m_scenario_scroll+scenario_visible_rows)
                    m_scenario_scroll=pane.m__folder_cursor-scenario_visible_rows+1;
            }
            if(before!=pane.m__folderoftype) applied();
            return;
        }
    }
    if (detail && m_presentation_tab == 2 && detail->m_pane_sound) {
        auto& pane = *detail->m_pane_sound;
        const auto select_row = [&](Integer row) {
            if (row < 0 || row >= static_cast<Integer>(pane.m__soundfonts.size())) return;
            auto& selected = pane.m__selected_soundfont_rows;
            if ((m_presentation_modifiers & ::cw::dialog::DialogPresentationModifierShift) &&
                m_soundfont_cursor >= 0) {
                selected.clear();
                for (auto i=std::min(row,m_soundfont_cursor); i<=std::max(row,m_soundfont_cursor); ++i)
                    selected.push_back(i);
            } else if (m_presentation_modifiers & ::cw::dialog::DialogPresentationModifierControl) {
                const auto found=std::find(selected.begin(),selected.end(),row);
                if (found==selected.end()) selected.push_back(row); else selected.erase(found);
                m_soundfont_cursor=row;
            } else { selected={row}; m_soundfont_cursor=row; }
            pane.OnGridRangeSelect(nullptr);
        };
        if (action == Action::Select || action == Action::Activate) {
            select_row(index);
            m_audio_slider=-1;
            return;
        }
        if (action == Action::Auxiliary && index >= audio_volume_first &&
            index < audio_volume_first+static_cast<int>(pane.m__soundfonts.size())) {
            const auto row=index-audio_volume_first;
            select_row(row);
            m_soundfont_edit_row=row;
            m_soundfont_edit_text=std::to_string(std::get<2>(pane.m__soundfonts[row]));
            m_soundfont_replace_text=true;
            m_audio_slider=-1;
            return;
        }
        if (action == Action::Auxiliary && index >= audio_use_first &&
            index < audio_use_first+static_cast<int>(pane.m__soundfonts.size())) {
            auto& value=std::get<1>(pane.m__soundfonts[index-audio_use_first]);
            value=!value;
            applied();
            return;
        }
        if (action == Action::Auxiliary && index >= audio_add && index <= audio_down) {
            const bool moved = (index==audio_up && pane.m__up_soundfont_enabled) ||
                               (index==audio_down && pane.m__down_soundfont_enabled);
            if (index==audio_add) { request_document(SettingsDocumentAction::SoundFonts); return; }
            else if (index==audio_remove && pane.m__remove_soundfont_enabled) pane.OnRemoveSoundFontBtn(nullptr);
            else if (index==audio_up && pane.m__up_soundfont_enabled) pane.OnUpSoundFontBtn(nullptr);
            else if (index==audio_down && pane.m__down_soundfont_enabled) pane.OnDownSoundFontBtn(nullptr);
            m_soundfont_cursor=pane.m__selected_soundfont_rows.empty() ? -1 : pane.m__selected_soundfont_rows.front();
            if (moved && !pane.m__selected_soundfont_rows.empty()) {
                // Python の MakeCellVisible と同様、複数行
                // の選択でも、上移動では先頭行、下移動では末尾行を追従表示する。
                const auto row = index==audio_up ? pane.m__selected_soundfont_rows.front()
                                                : pane.m__selected_soundfont_rows.back();
                if (row<m_soundfont_scroll) m_soundfont_scroll=row;
                else if (row>=m_soundfont_scroll+audio_visible_rows)
                    m_soundfont_scroll=row-audio_visible_rows+1;
            }
            applied();
            return;
        }
        if (action == Action::Auxiliary && (index==audio_play_bgm || index==audio_play_sound)) {
            auto& value=index==audio_play_bgm ? pane.m_cb_playbgm : pane.m_cb_playsound;
            value=!value_bool(value);
            applied();
            return;
        }
    }
    if (action == Action::Accept) {
        request_apply(true);
        return;
    }
    if (action == Action::Toggle) {
        request_apply(false);
        return;
    }
    if (action == Action::Cancel) {
        OnClose(nullptr);
        return;
    }
    if (action != Action::Auxiliary) return;
    if (index >= 1000 && index < 1000 + settings_tab_count()) {
        m_detail_view_x=0; m_detail_view_y=0;
        m_audio_slider=-1;
        m_presentation_tab = index - 1000;
        m_presentation_page = 0;
        dialog_retained_surface().invalidate_all();
        if (const auto panel = detail_panel(m_panel)) panel->m_note = m_presentation_tab;
        return;
    }
    if (index == 1100) {
        m_presentation_page = std::max<Integer>(0, m_presentation_page - 1);
        return;
    }
    if (index == 1101) {
        ++m_presentation_page;
        return;
    }
    if (index == 1200) {
        show_details();
        return;
    }
    if (index >= 1300 && index <= 1302) {
        if (const auto panel = detail_panel(m_panel)) {
            if (index == 1300) panel->OnDefault(nullptr);
            else if (index == 1301) panel->OnLoad(nullptr);
            else panel->OnSave(nullptr);
        }
        return;
    }
    auto fields = settings_presentation_fields(*this,
        detail_panel(m_panel) ? m_presentation_tab : 0);
    if (index >= 0 && static_cast<std::size_t>(index) < fields.size()) {
        if (!settings_field_enabled(*this,m_presentation_tab,std::get<0>(fields[static_cast<std::size_t>(index)]))) return;
        std::get<2>(fields[static_cast<std::size_t>(index)])();
        applied();
    }
}

void SettingsDialog::commit_soundfont_edit()
{
    const auto panel=detail_panel(m_panel);
    if (panel && panel->m_pane_sound && m_soundfont_edit_row>=0 &&
        m_soundfont_edit_row<static_cast<Integer>(panel->m_pane_sound->m__soundfonts.size())) {
        std::get<2>(panel->m_pane_sound->m__soundfonts[m_soundfont_edit_row]) =
            ::cw::setting::integer_or_initial(m_soundfont_edit_text, 100, 0, 100);
        applied();
    }
    m_soundfont_edit_row=-1;
}

void SettingsDialog::begin_scenario_edit(int field)
{
    if(ui_numeric(field)) {
        const auto value=ui_value(*this,field); if(!value) return;
        ui_focus_control(*this,field);
        m_scenario_edit=field; m_scenario_edit_text=std::to_string(value_integer(*value));
        m_scenario_caret=m_scenario_edit_text.size(); m_scenario_select_all=true; return;
    }
    if(drawing_editable(field)) {
        const auto value=drawing_value(*this,field);
        if(!value || !drawing_enabled(*this,field)) return;
        m_scenario_edit=m_drawing_focus=field;
        m_scenario_edit_text=field==drawing_file?value_string(*value):std::to_string(value_integer(*value));
        m_scenario_caret=m_scenario_edit_text.size(); m_scenario_select_all=true;
        return;
    }
    if(const auto pane=settings_font(*this); pane && m_presentation_tab==3 &&
        (field==font_example || font_numeric_field(*pane,field))) {
        if(!pane->m__controls_enabled) return;
        font_select(*this,*pane,field);
        m_scenario_edit=field;
        m_scenario_edit_text=field==font_example?value_string(pane->m_tx_example):
            std::to_string(field==font_example_size?value_integer(pane->m_sc_example,24):font_cell(*pane,field)->m_pixels);
        m_scenario_caret=m_scenario_edit_text.size(); m_scenario_select_all=true;
        return;
    }
    if(const auto value=general_edit_value(*this,field)) {
        if(field==6101) {
            const auto panel=detail_panel(m_panel);
            if(!panel->m_pane_gene->m__money_enabled) return;
        }
        m_scenario_edit=field; m_scenario_edit_text=field<6102?std::to_string(value_integer(*value)):value_string(*value);
        m_scenario_caret=m_scenario_edit_text.size(); m_scenario_select_all=true;
        return;
    }
    const auto panel=detail_panel(m_panel);
    if(!panel || !panel->m_pane_scenario) return;
    auto& pane=*panel->m_pane_scenario;
    String text;
    if (const auto app=scenario_application(pane,field)) text=value_string(*app);
    else {
        const int row=(field-scenario_cell_first)/2, col=(field-scenario_cell_first)%2;
        if(field<scenario_cell_first || row>=static_cast<int>(pane.m__folderoftype.size())) return;
        pane.m__folder_cursor=row;
        pane.OnGirdSelectCell(nullptr);
        text=col ? std::get<1>(pane.m__folderoftype[row]) : std::get<0>(pane.m__folderoftype[row]);
        if(row<m_scenario_scroll) m_scenario_scroll=row;
        if(row>=m_scenario_scroll+scenario_visible_rows) m_scenario_scroll=row-scenario_visible_rows+1;
    }
    m_scenario_edit=field;
    m_scenario_edit_text=std::move(text);
    m_scenario_caret=m_scenario_edit_text.size();
    m_scenario_select_all=true;
}

void SettingsDialog::commit_scenario_edit()
{
    if(ui_numeric(m_scenario_edit)) {
        if(auto value=ui_value(*this,m_scenario_edit)) {
            const auto setting = current_setting();
            const auto initial = m_scenario_edit == ui_radius
                ? (setting ? value_integer(setting->m_radius_notdetectmovement_init, 5) : 5) : 50;
            const auto number = ::cw::setting::integer_or_initial(m_scenario_edit_text,
                initial, m_scenario_edit == ui_radius ? 0 : 1, 50);
            if(value_integer(*value)!=number) {
                *value=static_cast<Integer>(number); applied();
            }
        }
        m_scenario_edit=-1; return;
    }
    if(drawing_editable(m_scenario_edit)) {
        if(auto value=drawing_value(*this,m_scenario_edit); value && drawing_enabled(*this,m_scenario_edit)) {
            if(m_scenario_edit==drawing_file) {
                if(value_string(*value)!=m_scenario_edit_text) { *value=m_scenario_edit_text; applied(); }
            } else {
                auto local = current_setting() ? current_setting()->m_local : nullptr;
                const auto pane = settings_drawing(*this);
                if (pane && pane->m__get_localsettings_cb)
                    local = object_value<::cw::setting::LocalSetting>((*pane->m__get_localsettings_cb)({}, {}));
                if (!local) { local = std::make_shared<::cw::setting::LocalSetting>(); local->init(); }
                const std::array<Value, 5> colours{local->m_mwincolour_init, local->m_mwinframecolour_init,
                    local->m_blcurtaincolour_init, local->m_curtaincolour_init, local->m_mwinframecolour_init};
                const auto initial = std::get<3>(value_colour(colours[m_scenario_edit-drawing_alpha_first], Colour{0,0,0,255}));
                const auto number = ::cw::setting::integer_or_initial(m_scenario_edit_text, initial, 0, 255);
                if(value_integer(*value)!=number) {
                    *value=static_cast<Integer>(number); applied();
                }
            }
        }
        m_scenario_edit=-1; return;
    }
    if(const auto pane=settings_font(*this); pane && m_presentation_tab==3 &&
        (m_scenario_edit==font_example || font_numeric_field(*pane,m_scenario_edit))) {
        if(pane->m__controls_enabled) {
            if(m_scenario_edit==font_example) { pane->m_tx_example=m_scenario_edit_text; applied(); }
            else {
                const bool sample=m_scenario_edit==font_example_size;
                const auto setting = current_setting();
                Integer initial = setting ? value_integer(setting->m_fontexamplepixelsize_init, 24) : 24;
                if (!sample) {
                    auto local = setting ? setting->m_local : nullptr;
                    if (pane->m__get_localsettings_cb)
                        local = object_value<::cw::setting::LocalSetting>((*pane->m__get_localsettings_cb)({}, {}));
                    if (!local) { local = std::make_shared<::cw::setting::LocalSetting>(); local->init(); }
                    const auto& type = pane->m__types[(m_scenario_edit-font_type_first)/5];
                    const auto& table = pane->m_msg_exfonttypes.contains(type) ?
                        local->m_msg_exfonts_init_typed : local->m_fonttypes_init_typed;
                    const auto it = table.find(type);
                    initial = it == table.end() ? -1 : std::get<2>(it->second);
                }
                const auto number = ::cw::setting::integer_or_initial(m_scenario_edit_text, initial, sample?8:1, sample?32:99);
                if(sample) pane->m_sc_example=number;
                else font_cell(*pane,m_scenario_edit)->m_pixels=number;
                applied();
            }
            pane->OnExampleText(nullptr);
        }
        m_scenario_edit=-1; return;
    }
    if(auto value=general_edit_value(*this,m_scenario_edit)) {
        if(m_scenario_edit<6102) {
            const auto setting = current_setting();
            if (setting) {
                const auto initial = value_integer(m_scenario_edit == 6100 ?
                    setting->m_backlogmax_init : setting->m_initmoneyamount_init);
                const auto number = ::cw::setting::integer_or_initial(m_scenario_edit_text,
                    initial, 0, m_scenario_edit == 6100 ? 9999 : ::cw::lib::money::maximum);
                if(value_integer(*value)!=number) {
                    *value=static_cast<Integer>(number); applied();
                }
            }
        } else if(value_string(*value)!=m_scenario_edit_text) { *value=m_scenario_edit_text; applied(); }
        m_scenario_edit=-1;
        return;
    }
    const auto panel=detail_panel(m_panel);
    if(m_scenario_edit<0 || !panel || !panel->m_pane_scenario) { m_scenario_edit=-1; return; }
    auto& pane=*panel->m_pane_scenario;
    if(auto app=scenario_application(pane,m_scenario_edit)) {
        if(value_string(*app)!=m_scenario_edit_text) { *app=m_scenario_edit_text; applied(); }
    } else {
        const int row=(m_scenario_edit-scenario_cell_first)/2, col=(m_scenario_edit-scenario_cell_first)%2;
        if(row>=0 && row<static_cast<int>(pane.m__folderoftype.size())) {
            auto& text=col ? std::get<1>(pane.m__folderoftype[row]) : std::get<0>(pane.m__folderoftype[row]);
            if(text!=m_scenario_edit_text) {
                text=m_scenario_edit_text;
                pane._grid_cell_change(row,col);
                pane._select_changed_folderoftype();
                applied();
            }
        }
    }
    m_scenario_edit=-1;
}

void SettingsDialog::dialog_presentation_text(const std::string& text, bool composition)
{
    // ポインター・キー入力と同様、表示中の編集欄だけが文字イベントを受
    // け取る。 編集行がキャッシュに残っていても、終了後や初期化前の遅
    // 延文字コールバックを SoundFont の代替処理へ送らない。
    if (document_request_pending() || m_state.load()!=SettingsDialogState::Visible) return;
    if(m_version_text.focused || m_skin_description_text.focused) return; // wx.TE_READONLY と同様、TEXTINPUT は確定しない。
    if(!composition && m_scenario_edit>=0 && !m_scenario_type_popup && !m_settings_popup && m_state.load()==SettingsDialogState::Visible) {
        const auto font=settings_font(*this);
        if((ui_numeric(m_scenario_edit) || (m_scenario_edit>=6100 && m_scenario_edit<6102) ||
            (m_scenario_edit>=drawing_alpha_first && m_scenario_edit<drawing_alpha_first+5) ||
            (font && m_presentation_tab==3 && font_numeric_field(*font,m_scenario_edit))) && text!="\b" &&
            !std::all_of(text.begin(),text.end(),[](unsigned char c){return c>='0' && c<='9';})) return;
        auto& value=m_scenario_edit_text;
        if(m_scenario_select_all) { value.clear(); m_scenario_caret=0; }
        m_scenario_select_all=false;
        if(text=="\b") {
            if(m_scenario_caret) {
                auto start=m_scenario_caret-1;
                while(start && (static_cast<unsigned char>(value[start])&0xc0)==0x80) --start;
                value.erase(start,m_scenario_caret-start); m_scenario_caret=start;
            }
        } else {
            String line=text;
            line.erase(std::remove_if(line.begin(),line.end(),[](char c){return c=='\r'||c=='\n'||c=='\t';}),line.end());
            value.insert(m_scenario_caret,line); m_scenario_caret+=line.size();
        }
        if(font && m_presentation_tab==3 && m_scenario_edit==font_example) {
            // wx の EVT_TEXT と同様、キャンセル時に保持する変更も含め、編集のたびに公開する。
            font->m_tx_example=value;
            font->OnExampleText(nullptr);
            applied();
        }
        request_dialog_presentation_refresh();
        return;
    }
    if (composition || m_soundfont_edit_row<0 || is_applying()) return;
    if (text=="\b") {
        if (m_soundfont_replace_text) m_soundfont_edit_text.clear();
        else if (!m_soundfont_edit_text.empty()) m_soundfont_edit_text.pop_back();
    } else {
        if (!std::all_of(text.begin(),text.end(),[](unsigned char c) { return c>='0' && c<='9'; })) return;
        if (m_soundfont_replace_text) m_soundfont_edit_text.clear();
        if (m_soundfont_edit_text.size()+text.size()<=3) m_soundfont_edit_text+=text;
    }
    m_soundfont_replace_text=false;
}

void SettingsDialog::dialog_presentation_pointer(::cw::dialog::DialogPresentationAction action,
    int index,float x,float y,bool pressed)
{
    if (document_request_pending() || m_state.load()!=SettingsDialogState::Visible) return;
    if(m_version_text.pointer(x,y,pressed,(m_presentation_modifiers&::cw::dialog::DialogPresentationModifierShift)!=0)) {
        m_skin_description_text.blur();
        commit_scenario_edit();commit_soundfont_edit();
        dialog_retained_surface().invalidate_all();request_dialog_presentation_refresh();return;
    }
    if(pressed && m_version_text.focused) {m_version_text.blur();dialog_retained_surface().invalidate_all();}
    const auto panel=detail_panel(m_panel);
    const auto scale=settings_final_scale(*this);
    if(panel && !m_local_panel_only) {
        if(index==9900 || index==9901) {
            if(index==9900) {
                const Integer body_width=settings_detail_view_width()-20;
                const Integer maximum=std::max<Integer>(0,720-body_width);
                const Integer thumb=std::min(body_width,body_width*body_width/720);
                m_detail_view_x=std::clamp<Integer>(std::llround((x/scale-thumb/2.0)*maximum/std::max<Integer>(1,body_width-thumb)),0,maximum);
            } else if(m_presentation_tab!=5) {
                constexpr Integer thumb=326*326/482;
                m_detail_view_y=std::clamp<Integer>(std::llround((y/scale-42-thumb/2.0)*156/(326-thumb)),0,156);
            }
            request_dialog_presentation_refresh(); return;
        }
        if(y/scale>=42 && y/scale<368) { x+=m_detail_view_x*scale; y+=m_detail_view_y*scale; }
    }
    if(m_state.load()!=SettingsDialogState::Visible || m_settings_popup) return;
    const bool skin_visible=simple_panel(m_panel) || (panel && !m_local_panel_only && m_presentation_tab==0);
    if(!skin_visible)m_skin_description_text.blur();
    if(skin_visible && m_skin_description_text.pointer(x,y,pressed,
        (m_presentation_modifiers&::cw::dialog::DialogPresentationModifierShift)!=0)) {
        commit_scenario_edit();commit_soundfont_edit();m_version_text.blur();
        dialog_retained_surface().invalidate_all();request_dialog_presentation_refresh();return;
    }
    if(pressed && m_skin_description_text.focused) {m_skin_description_text.blur();dialog_retained_surface().invalidate_all();}
    if(action==::cw::dialog::DialogPresentationAction::Auxiliary && index==ui_scrollbar && m_presentation_tab==5) {
        if(!m_ui_content_height) (void)dialog_presentation();
        const auto viewport=settings_ui_viewport(*this);
        const auto maximum=std::max<Integer>(0,m_ui_content_height-viewport.height);
        const auto thumb=std::max<Integer>(18,viewport.height*viewport.height/std::max<Integer>(1,m_ui_content_height));
        m_ui_scroll=std::clamp<Integer>(std::llround((y/scale-viewport.y-thumb/2.0)*maximum/std::max<Integer>(1,viewport.height-thumb)),0,maximum);
        request_dialog_presentation_refresh(); return;
    }
    if(action==::cw::dialog::DialogPresentationAction::Auxiliary && index>=drawing_speed_first && index<drawing_speed_first+4 &&
        (simple_panel(m_panel) || m_presentation_tab==1)) {
        if(!drawing_enabled(*this,index)) return;
        const auto value=drawing_value(*this,index);
        if(!value) return;
        commit_scenario_edit(); m_drawing_focus=index; m_expand_focus=-1;
        const auto view=settings_content_presentation();
        const auto control=std::find_if(view.controls.begin(),view.controls.end(),[index](const auto& c){return c.index==index;});
        if(control!=view.controls.end() && control->enabled) {
            const auto next=std::clamp<Integer>(std::llround((x-control->x-5*scale)*10/std::max(1.0,control->width-10*scale)),0,10);
            if(value_integer(*value)!=next) { *value=next; applied(); }
        }
        return;
    }
    if(action==::cw::dialog::DialogPresentationAction::Auxiliary && index==font_scrollbar && m_presentation_tab==3) {
        if(const auto pane=settings_font(*this); pane && pane->m__controls_enabled) {
            const auto count=static_cast<Integer>(pane->m__types.size());
            const auto maximum=std::max<Integer>(0,count-font_visible_rows);
            const auto height=font_visible_rows*font_row_height;
            const auto thumb=std::max<Integer>(16,height*font_visible_rows/std::max<Integer>(1,count));
            m_font_scroll=std::clamp<Integer>(std::llround((y/scale-font_rows_y-thumb/2.0)*maximum/std::max<Integer>(1,height-thumb)),0,maximum);
            request_dialog_presentation_refresh();
        }
        return;
    }
    if(action==::cw::dialog::DialogPresentationAction::Auxiliary && index==skin_scrollbar) {
        const auto rect=m_skin_description_rect;
        const auto font=settings_final_font(*this,"datadesc",13);
        const auto logical_font_pixels=static_cast<Integer>(std::ceil(font.pixels/scale));
        const auto rows=std::max<Integer>(1,(rect.height-6)/(logical_font_pixels+2));
        const auto maximum=std::max<Integer>(0,m_skin_description_lines-rows);
        const auto thumb=std::max<Integer>(16,(rect.height-4)*rows/std::max<Integer>(1,m_skin_description_lines));
        m_skin_description_scroll=std::clamp<Integer>(std::llround((y/scale-rect.y-2-thumb/2.0)*maximum/std::max<Integer>(1,rect.height-4-thumb)),0,maximum);
        request_dialog_presentation_refresh(); return;
    }
    if(action==::cw::dialog::DialogPresentationAction::Auxiliary && index==general_display && (simple_panel(m_panel) || m_presentation_tab==0)) {
        if(const auto expand=settings_expand(*this); expand && expand->m__expand_enable && !value_bool(expand->m_cb_fullscreen)) {
            const auto view=settings_content_presentation();
            const auto hit=std::find_if(view.controls.begin(),view.controls.end(),[](const auto& c){return c.index==general_display;});
            if(hit==view.controls.end() || !hit->enabled) return;
            commit_scenario_edit(); m_expand_focus=index; m_drawing_focus=-1;
            const auto next=std::clamp<Integer>(10+std::llround((x-hit->x-5*scale)*(expand->m__expand_max-10)/std::max(1.0,hit->width-10*scale)),10,std::max<Integer>(10,expand->m__expand_max));
            if(next!=value_integer(expand->m_sl_expand)) { expand->m_sl_expand=next; expand->OnExpandChange(nullptr); applied(); }
        }
        return;
    }
    if(m_state.load()==SettingsDialogState::Visible && panel && panel->m_pane_scenario && m_presentation_tab==4 &&
       action==::cw::dialog::DialogPresentationAction::Auxiliary && index==scenario_scrollbar) {
        commit_scenario_edit();
        const auto count=static_cast<Integer>(panel->m_pane_scenario->m__folderoftype.size());
        const auto maximum=std::max<Integer>(0,count-scenario_visible_rows);
        const auto thumb=std::max<Integer>(18,120*scenario_visible_rows/std::max<Integer>(1,count));
        m_scenario_scroll=std::clamp<Integer>(static_cast<Integer>(std::llround(
            (y/scale-scenario_table_y-thumb/2.0)*maximum/std::max<Integer>(1,120-thumb))),0,maximum);
        request_dialog_presentation_refresh();
        return;
    }
    if (m_state.load()==SettingsDialogState::Visible && panel && panel->m_pane_sound &&
        m_presentation_tab==2 && action==::cw::dialog::DialogPresentationAction::Auxiliary &&
        index==audio_scrollbar) {
        commit_soundfont_edit();
        const auto count=static_cast<Integer>(panel->m_pane_sound->m__soundfonts.size());
        const auto maximum=std::max<Integer>(0,count-audio_visible_rows);
        const auto thumb=std::max<Integer>(18,144*audio_visible_rows/std::max<Integer>(1,count));
        m_soundfont_scroll=std::clamp<Integer>(static_cast<Integer>(std::llround(
            (y/scale-100-thumb/2.0)*maximum/std::max<Integer>(1,144-thumb))),0,maximum);
        return;
    }
    if (m_state.load()!=SettingsDialogState::Visible || !panel || !panel->m_pane_sound ||
        m_presentation_tab!=2 || action!=::cw::dialog::DialogPresentationAction::Auxiliary ||
        index<audio_slider_first || index>=audio_slider_first+4) return;
    commit_soundfont_edit();
    m_audio_slider=index-audio_slider_first;
    // 描画する軌道と当たり判定矩形の両方に、UP_WIN を一度だけ適用する。
    const auto value=static_cast<Integer>(std::llround((x/scale-48.0)*100.0/248.0));
    *audio_volume(*panel->m_pane_sound,m_audio_slider)=std::clamp<Integer>(value,0,100);
    applied();
    (void)pressed; // 最後の解除時は、軌道外でも範囲内に収めた値を確定する。
}

void SettingsDialog::dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers)
{
    if(((m_presentation_tab==3 && settings_font(*this)) || (m_presentation_tab==1 && settings_drawing(*this)) ||
        (m_presentation_tab==5 && settings_ui(*this))) && !m_settings_popup && m_scenario_edit<0)
        dialog_presentation_key(::cw::dialog::DialogPresentationKey::Enter,true,modifiers);
    else dialog_presentation_action(::cw::dialog::DialogPresentationAction::Accept,-1);
}

void SettingsDialog::dialog_presentation_key(::cw::dialog::DialogPresentationKey key,bool pressed,
    ::cw::dialog::DialogPresentationModifiers modifiers)
{
    if (!pressed || document_request_pending() || m_state.load()!=SettingsDialogState::Visible) return;
    if(m_version_text.key(key,modifiers,[](const String& text){::cw::dialog::presentation_set_clipboard_text(text);})) {
        dialog_retained_surface().invalidate_all();request_dialog_presentation_refresh();return;
    }
    const bool skin_visible=simple_panel(m_panel) || (detail_panel(m_panel) && !m_local_panel_only && m_presentation_tab==0);
    if(!skin_visible)m_skin_description_text.blur();
    if(skin_visible && m_skin_description_text.key(key,modifiers,[](const String& text){::cw::dialog::presentation_set_clipboard_text(text);})) {
        reveal_skin_description(*this);
        dialog_retained_surface().invalidate_all();request_dialog_presentation_refresh();return;
    }
    m_presentation_modifiers=modifiers;
    using Key=::cw::dialog::DialogPresentationKey;
    using Action=::cw::dialog::DialogPresentationAction;
    const auto panel=detail_panel(m_panel);
    if(panel && !m_local_panel_only && m_presentation_tab!=5 && !m_settings_popup && !m_scenario_type_popup &&
       (key==Key::PageUp || key==Key::PageDown)) {
        m_detail_view_y=std::clamp<Integer>(m_detail_view_y+(key==Key::PageUp?-120:120),0,156);
        request_dialog_presentation_refresh(); return;
    }
    if(m_settings_popup) {
        const int count=static_cast<int>(settings_choices(*this,m_settings_popup).size());
        if(key==Key::Up || key==Key::Down) m_settings_choice=std::clamp(m_settings_choice+(key==Key::Up?-1:1),0,std::max(0,count-1));
        else if(key==Key::Home) m_settings_choice=0;
        else if(key==Key::End) m_settings_choice=std::max(0,count-1);
        else if(key==Key::Enter || key==Key::Space) dialog_presentation_action(Action::Accept,-1);
        request_dialog_presentation_refresh(); return;
    }
    const bool general=panel && m_presentation_tab==0 && panel->m_pane_gene;
    const bool drawing=panel && m_presentation_tab==1 && panel->m_pane_draw;
    const bool ui=panel && m_presentation_tab==5 && panel->m_pane_ui;
    if((general || simple_panel(m_panel)) && m_scenario_edit<0 && m_expand_focus>=0) {
        const auto expand=settings_expand(*this);
        if(expand && m_expand_focus==general_display &&
            (key==Key::Left || key==Key::Right || key==Key::Up || key==Key::Down || key==Key::Home || key==Key::End)) {
            if(expand->m__expand_enable && !value_bool(expand->m_cb_fullscreen)) {
                expand->m_sl_expand=key==Key::Home?Integer{10}:key==Key::End?std::max<Integer>(10,expand->m__expand_max):
                    std::clamp<Integer>(value_integer(expand->m_sl_expand)+(key==Key::Left || key==Key::Down?-1:1),10,std::max<Integer>(10,expand->m__expand_max));
                expand->OnExpandChange(nullptr); applied();
            }
            return;
        }
        if(key==Key::Space || (m_expand_focus==general_expand && (key==Key::Up || key==Key::Down))) {
            dialog_presentation_action(Action::Auxiliary,m_expand_focus); return;
        }
        if(key==Key::Tab) m_expand_focus=-1;
    }
    if(ui && ui_numeric(m_ui_focus) && (key==Key::Up || key==Key::Down)) {
        if(m_ui_focus!=ui_radius && (panel->m_pane_ui->m_pc_limits_inn.expired() || !value_bool(panel->m_pane_ui->m_cb_pc_card_limits))) return;
        const bool editing=m_scenario_edit>=0; commit_scenario_edit();
        auto value=ui_value(*this,m_ui_focus);
        *value=std::clamp<Integer>(value_integer(*value)+(key==Key::Up?1:-1),m_ui_focus==ui_radius?0:1,50);
        applied(); if(editing) begin_scenario_edit(m_ui_focus); return;
    }
    if(drawing && (key==Key::Up || key==Key::Down) &&
        m_drawing_focus>=drawing_alpha_first && m_drawing_focus<drawing_alpha_first+5 && drawing_enabled(*this,m_drawing_focus)) {
        const bool editing=m_scenario_edit>=0;
        commit_scenario_edit();
        if(auto value=drawing_value(*this,m_drawing_focus)) {
            *value=std::clamp<Integer>(value_integer(*value)+(key==Key::Up?1:-1),0,255);
            applied();
            if(editing) begin_scenario_edit(m_drawing_focus);
        }
        return;
    }
    if((drawing || simple_panel(m_panel)) && m_scenario_edit<0 && m_drawing_focus>=drawing_speed_first && m_drawing_focus<drawing_speed_first+4 &&
        (key==Key::Left || key==Key::Right || key==Key::Up || key==Key::Down || key==Key::Home || key==Key::End)) {
        if(auto value=drawing_value(*this,m_drawing_focus); value && drawing_enabled(*this,m_drawing_focus)) {
            *value=key==Key::Home?Integer{0}:key==Key::End?Integer{10}:
                std::clamp<Integer>(value_integer(*value)+(key==Key::Left || key==Key::Down?-1:1),0,10);
            applied();
        }
        return;
    }
    const auto fonts=m_presentation_tab==3?settings_font(*this):nullptr;
    if(fonts && !fonts->m__controls_enabled) return;
    if(panel && ((m_presentation_tab==4 && panel->m_pane_scenario) || general || fonts || drawing || ui)) {
        const auto pane=panel->m_pane_scenario;
        request_dialog_presentation_refresh();
        if(m_scenario_type_popup) {
            const auto types=scenario_types_for(*this);
            if(key==Key::Up || key==Key::Down)
                m_scenario_type_choice=std::clamp(m_scenario_type_choice+(key==Key::Up?-1:1),0,std::max(0,static_cast<int>(types.size())-1));
            else if(key==Key::Enter || key==Key::Space) dialog_presentation_action(Action::Accept,-1);
            return;
        }
        if(key==Key::Tab) {
            const auto field=m_scenario_edit;
            commit_scenario_edit();
            if(ui) {
                if(m_ui_control_order.empty()) (void)dialog_presentation();
                const auto& controls=m_ui_control_order;
                if(!controls.empty()) {
                    const auto it=std::find(controls.begin(),controls.end(),m_ui_focus);
                    const int old=it==controls.end()?-1:static_cast<int>(it-controls.begin());
                    ui_focus_control(*this,controls[(old+static_cast<int>(controls.size())+((modifiers&::cw::dialog::DialogPresentationModifierShift)?-1:1))%controls.size()]);
                }
                return;
            }
            if(drawing) {
                std::vector<int> cells;
                for(const auto& control:dialog_presentation().controls)
                    if(control.enabled && control.action==Action::Auxiliary &&
                        ((control.index>=0 && control.index<=5) || (control.index>=8000 && control.index<=drawing_copy))) cells.push_back(control.index);
                if(!cells.empty()) {
                    const auto it=std::find(cells.begin(),cells.end(),m_drawing_focus);
                    const int old=it==cells.end()?-1:static_cast<int>(it-cells.begin());
                    m_drawing_focus=cells[(old+static_cast<int>(cells.size())+((modifiers&::cw::dialog::DialogPresentationModifierShift)?-1:1))%cells.size()];
                }
                return;
            }
            if(fonts) {
                std::vector<int> cells;
                for(int i=0;i<static_cast<int>(fonts->m__bases_order.size());++i) cells.push_back(font_base_first+i);
                for(int i=0;i<static_cast<int>(fonts->m__types.size());++i)
                    for(int col=0;col<5;++col) {
                        const auto id=font_type_first+i*5+col;
                        if(col!=1 || font_numeric_field(*fonts,id)) cells.push_back(id);
                    }
                cells.push_back(font_example); cells.push_back(font_example_size);
                const auto it=std::find(cells.begin(),cells.end(),m_font_focus);
                const int old=it==cells.end()?0:static_cast<int>(it-cells.begin());
                const int next=(old+static_cast<int>(cells.size())+((modifiers&::cw::dialog::DialogPresentationModifierShift)?-1:1))%static_cast<int>(cells.size());
                font_select(*this,*fonts,cells[next]);
                return;
            }
            if(general) {
                const int next=(std::clamp(field-6100,0,5)+6+((modifiers&::cw::dialog::DialogPresentationModifierShift)?-1:1))%6;
                begin_scenario_edit(6100+next); return;
            }
            const int cells=static_cast<int>(pane->m__folderoftype.size())*2;
            const int old=field>=scenario_cell_first ? field-scenario_cell_first : field>=scenario_application_first ? cells+field-scenario_application_first : -1;
            const int next=(old+cells+3+((modifiers&::cw::dialog::DialogPresentationModifierShift)?-1:1))%(cells+3);
            begin_scenario_edit(next<cells ? scenario_cell_first+next : scenario_application_first+next-cells);
            return;
        }
        if(m_scenario_edit>=0) {
            if(modifiers&::cw::dialog::DialogPresentationModifierControl) {
                if(key==Key::A) { m_scenario_select_all=true; m_scenario_caret=m_scenario_edit_text.size(); }
                else if(key==Key::V) dialog_presentation_text(::cw::dialog::presentation_clipboard_text(),false);
                else if((key==Key::C || key==Key::X) && m_scenario_select_all) {
                    ::cw::dialog::presentation_set_clipboard_text(m_scenario_edit_text);
                    if(key==Key::X) dialog_presentation_text("\b",false);
                }
                return;
            }
            auto& text=m_scenario_edit_text;
            if(key==Key::Left) {
                if(m_scenario_select_all) m_scenario_caret=0;
                else if(m_scenario_caret) {
                    --m_scenario_caret;
                    while(m_scenario_caret && (static_cast<unsigned char>(text[m_scenario_caret])&0xc0)==0x80) --m_scenario_caret;
                }
                m_scenario_select_all=false;
            } else if(key==Key::Right) {
                if(m_scenario_select_all) m_scenario_caret=text.size();
                else if(m_scenario_caret<text.size()) {
                    ++m_scenario_caret;
                    while(m_scenario_caret<text.size() && (static_cast<unsigned char>(text[m_scenario_caret])&0xc0)==0x80) ++m_scenario_caret;
                }
                m_scenario_select_all=false;
            } else if(key==Key::Home || key==Key::End) {
                m_scenario_caret=key==Key::Home ? 0 : text.size(); m_scenario_select_all=false;
            } else if(key==Key::Delete) {
                if(m_scenario_select_all) { text.clear(); m_scenario_caret=0; m_scenario_select_all=false; }
                else if(m_scenario_caret<text.size()) {
                    auto end=m_scenario_caret+1;
                    while(end<text.size() && (static_cast<unsigned char>(text[end])&0xc0)==0x80) ++end;
                    text.erase(m_scenario_caret,end-m_scenario_caret);
                }
            } else if(key==Key::Enter) commit_scenario_edit();
            if(key==Key::Delete && fonts && m_scenario_edit==font_example) {
                fonts->m_tx_example=text;
                fonts->OnExampleText(nullptr);
                applied();
            }
            return;
        }
        if(ui) {
            if(key==Key::Enter || key==Key::Space) dialog_presentation_action(Action::Auxiliary,m_ui_focus);
            else if(key==Key::PageUp || key==Key::PageDown) {
                const auto viewport=settings_ui_viewport(*this);
                m_ui_scroll=std::clamp<Integer>(m_ui_scroll+(key==Key::PageUp?-viewport.height:viewport.height),
                    0,std::max<Integer>(0,m_ui_content_height-viewport.height));
                request_dialog_presentation_refresh();
            }
            else if(key==Key::Home || key==Key::End) {
                if(m_ui_control_order.empty()) (void)dialog_presentation();
                if(!m_ui_control_order.empty()) ui_focus_control(*this,key==Key::Home?m_ui_control_order.front():m_ui_control_order.back());
                // 先頭・末尾への移動では、グループ見出しと余白も含める。
                const auto viewport=settings_ui_viewport(*this);
                m_ui_scroll=key==Key::Home?0:std::max<Integer>(0,m_ui_content_height-viewport.height);
                request_dialog_presentation_refresh();
            }
            return;
        }
        if(drawing) {
            if(key==Key::Enter || key==Key::Space) dialog_presentation_action(Action::Auxiliary,m_drawing_focus);
            return;
        }
        if(fonts) {
            if(key==Key::Enter || key==Key::Space) dialog_presentation_action(Action::Auxiliary,m_font_focus);
            else if(key==Key::Up || key==Key::Down || key==Key::Left || key==Key::Right || key==Key::Home || key==Key::End) {
                if(m_font_focus>=font_base_first && m_font_focus<font_base_first+5) {
                    font_select(*this,*fonts,std::clamp(m_font_focus+(key==Key::Up?-1:key==Key::Down?1:0),font_base_first,font_base_first+4));
                } else if(font_cell(*fonts,m_font_focus)) {
                    int row=(m_font_focus-font_type_first)/5, col=(m_font_focus-font_type_first)%5;
                    if(key==Key::Up) --row; if(key==Key::Down) ++row;
                    if(key==Key::Left) --col; if(key==Key::Right) ++col;
                    if(key==Key::Home) row=0;
                    if(key==Key::End) row=static_cast<int>(fonts->m__types.size())-1;
                    row=std::clamp(row,0,static_cast<int>(fonts->m__types.size())-1); col=std::clamp(col,0,4);
                    font_select(*this,*fonts,font_type_first+row*5+col);
                }
            }
            return;
        }
        if(!general && (key==Key::Up || key==Key::Down)) {
            const auto row=std::clamp<Integer>(pane->m__folder_cursor+(key==Key::Up?-1:1),0,
                std::max<Integer>(0,static_cast<Integer>(pane->m__folderoftype.size())-1));
            begin_scenario_edit(scenario_cell_first+static_cast<int>(row)*2);
        }
        return;
    }
    if (!panel || m_presentation_tab!=2 || !panel->m_pane_sound) return;
    auto& pane=*panel->m_pane_sound;
    if (key==Key::Enter) { commit_soundfont_edit(); return; }
    if (m_audio_slider>=0 && (key==Key::Left || key==Key::Right)) {
        auto* value=audio_volume(pane,m_audio_slider);
        *value=std::clamp<Integer>(value_integer(*value)+(key==Key::Left ? -1 : 1),0,100);
        applied();
    } else if ((key==Key::Up || key==Key::Down) && !pane.m__soundfonts.empty()) {
        const auto next=std::clamp<Integer>(m_soundfont_cursor+(key==Key::Up ? -1 : 1),
            0,static_cast<Integer>(pane.m__soundfonts.size())-1);
        dialog_presentation_action(Action::Select,static_cast<int>(next));
        if (next<m_soundfont_scroll) m_soundfont_scroll=next;
        if (next>=m_soundfont_scroll+audio_visible_rows) m_soundfont_scroll=next-audio_visible_rows+1;
    } else if (key==Key::Space && m_soundfont_cursor>=0)
        dialog_presentation_action(Action::Auxiliary,audio_use_first+static_cast<int>(m_soundfont_cursor));
}

void SettingsDialog::dialog_presentation_context(float x,float y)
{
    if(document_request_pending() || m_state.load()!=SettingsDialogState::Visible) return;
    if(m_version_text.contains(x,y)) {
        m_skin_description_text.blur();
        commit_scenario_edit();commit_soundfont_edit();
        m_version_text.context(x,y);dialog_retained_surface().invalidate_all();request_dialog_presentation_refresh();
    } else {
        const auto scale=settings_final_scale(*this);
        auto content_x=x,content_y=y;
        const bool detailed=detail_panel(m_panel) && !m_local_panel_only;
        const bool skin_visible=simple_panel(m_panel) || (detailed && m_presentation_tab==0 && y/scale>=42 && y/scale<368);
        if(detailed){content_x+=m_detail_view_x*scale;content_y+=m_detail_view_y*scale;}
        if(skin_visible && m_skin_description_text.contains(content_x,content_y)) {
            commit_scenario_edit();commit_soundfont_edit();m_version_text.blur();
            // 選択には内容座標を使う。ポップアップは、共通のスクロー
            // ル・切り取り変換後の、見えているダイアログに属する。
            m_skin_description_text.context(x,y);
            dialog_retained_surface().invalidate_all();request_dialog_presentation_refresh();
        } else dialog_presentation_action(::cw::dialog::DialogPresentationAction::Cancel,-1);
    }
}

void SettingsDialog::dialog_presentation_hover(float x,float y)
{
    if (document_request_pending() || m_state.load()!=SettingsDialogState::Visible)
        return;
    if(m_version_text.hover(x,y)) {dialog_retained_surface().invalidate_all();request_dialog_presentation_refresh();return;}
    if(m_skin_description_text.menu.opened()) {
        if(m_skin_description_text.menu.hover(x,y,m_skin_description_text.buffer)) {dialog_retained_surface().invalidate_all();request_dialog_presentation_refresh();}
        return;
    }
    const auto scale=settings_final_scale(*this);
    if(detail_panel(m_panel) && !m_local_panel_only) {
        if(y/scale<42 || y/scale>=368 || x/scale>=settings_detail_view_width()-20) return;
        x+=m_detail_view_x*scale; y+=m_detail_view_y*scale;
    }
    const bool skin_visible=simple_panel(m_panel) || (detail_panel(m_panel) && !m_local_panel_only && m_presentation_tab==0);
    if(skin_visible && m_skin_description_text.hover(x,y)) {dialog_retained_surface().invalidate_all();request_dialog_presentation_refresh();return;}
    const auto logical_x=x/scale;
    const auto logical_y=y/scale+(m_local_panel_only?local_panel_top:0);
    if(m_settings_popup) {
        if(const auto row=dialog_retained_surface().component_hit(
                "dropdown.rows",logical_x,logical_y)) {
            if(m_settings_choice!=*row) {
                m_settings_choice=static_cast<int>(*row);
                // settings.compact/detail.popup は最終ピクセルの保持型
                // 構成要素を持ち、 その変更検出には m_settings_choice を含める。
                request_dialog_presentation_refresh();
            }
        }
        return;
    }
    if(m_scenario_type_popup) {
        if(const auto row=dialog_retained_surface().component_hit(
                "scenario.dropdown.rows",logical_x,logical_y)) {
            if(m_scenario_type_choice!=*row) {
                m_scenario_type_choice=static_cast<int>(*row);
                // シナリオページの変更検出にはこの選択状態を含めるため、 ポッ
                // プアップも含むページ全体を一つの構成要素として再描画する。
                request_dialog_presentation_refresh();
            }
        }
    }
}

void SettingsDialog::dialog_presentation_scroll(float x,float y,int lines)
{
    if (document_request_pending()) return;
    if(m_state.load()!=SettingsDialogState::Visible) return;
    if(m_settings_popup) {
        const auto count=static_cast<int>(settings_choices(*this,m_settings_popup).size());
        m_settings_choice=std::clamp(m_settings_choice+lines,0,std::max(0,count-1));
        request_dialog_presentation_refresh(); return;
    }
    if(detail_panel(m_panel) && !m_local_panel_only) {
        const auto scale=settings_final_scale(*this);
        const Integer body_width=settings_detail_view_width()-20;
        if(y/scale>=368 && y/scale<384 && x/scale<body_width) {
            m_detail_view_x=std::clamp<Integer>(m_detail_view_x+lines*30,0,std::max<Integer>(0,720-body_width));
            request_dialog_presentation_refresh(); return;
        }
        if(m_presentation_tab!=5 && x/scale>=body_width && y/scale>=42 && y/scale<368) {
            m_detail_view_y=std::clamp<Integer>(m_detail_view_y+lines*30,0,156);
            request_dialog_presentation_refresh(); return;
        }
        if(y/scale<42 || y/scale>=368) return;
        x+=m_detail_view_x*scale; y+=m_detail_view_y*scale;
    }
    if(m_presentation_tab==5 && settings_ui(*this)) {
        const auto scale=settings_final_scale(*this);
        const auto viewport=settings_ui_viewport(*this);
        // 上位の詳細設定の本体は一つのスクロール文書である
        // 。 空の右余白とスクロールバー自体を含め、固定
        // の本体領域全体でホイールを使えるようにする。
        // 内側の狭い矩形だけで判定すると、少し古い座標や
        // 拡縮済み座標のずれで入力を黙って捨ててしまう。
        const bool over_document = detail_panel(m_panel) && !m_local_panel_only
            ? true
            : x/scale>=viewport.x && x/scale<708 &&
                y/scale>=viewport.y && y/scale<viewport.y+viewport.height;
        if(over_document) {
            if(!m_ui_content_height) (void)dialog_presentation();
            const auto next=std::clamp<Integer>(m_ui_scroll+static_cast<Integer>(lines)*25,
                0,std::max<Integer>(0,m_ui_content_height-viewport.height));
            if(next!=m_ui_scroll) {
                m_ui_scroll=next;
                request_dialog_presentation_refresh();
            }
        }
        return;
    }
    if(m_presentation_tab==3) {
        if(const auto pane=settings_font(*this); pane && pane->m__controls_enabled) {
const auto scale=settings_final_scale(*this);
            if(x/scale>=22 && x/scale<700 && y/scale>=font_rows_y && y/scale<font_rows_y+font_visible_rows*font_row_height) {
                m_font_scroll=std::clamp<Integer>(m_font_scroll+lines,0,std::max<Integer>(0,static_cast<Integer>(pane->m__types.size())-font_visible_rows));
                request_dialog_presentation_refresh();
            }
        }
        return;
    }
    if(simple_panel(m_panel) || m_presentation_tab==0) {
const auto scale=settings_final_scale(*this);
        const auto r=m_skin_description_rect;
        if(x/scale>=r.x && x/scale<r.x+r.width && y/scale>=r.y && y/scale<r.y+r.height) {
            const auto font=settings_final_font(*this,"datadesc",13);
            const auto logical_font_pixels=static_cast<Integer>(std::ceil(font.pixels/scale));
            const auto rows=std::max<Integer>(1,(r.height-6)/(logical_font_pixels+2));
            m_skin_description_scroll=std::clamp<Integer>(m_skin_description_scroll+lines,0,std::max<Integer>(0,m_skin_description_lines-rows));
            request_dialog_presentation_refresh(); return;
        }
    }
    const auto panel=detail_panel(m_panel);
const auto scale=settings_final_scale(*this);
    if(m_state.load()==SettingsDialogState::Visible && panel && panel->m_pane_scenario && m_presentation_tab==4) {
        if(m_scenario_type_popup) {
            const auto count=static_cast<int>(scenario_types_for(*this).size());
            m_scenario_type_choice=std::clamp(m_scenario_type_choice+lines,0,std::max(0,count-1));
        } else if(x/scale>=24 && x/scale<696 && y/scale>=scenario_table_y && y/scale<scenario_table_y+120) {
            commit_scenario_edit();
            m_scenario_scroll=std::clamp<Integer>(m_scenario_scroll+lines,0,std::max<Integer>(0,
                static_cast<Integer>(panel->m_pane_scenario->m__folderoftype.size())-scenario_visible_rows));
        }
        request_dialog_presentation_refresh();
        return;
    }
    if (m_state.load()!=SettingsDialogState::Visible || !panel || !panel->m_pane_sound ||
        m_presentation_tab!=2 || x/scale<audio_table_x || x/scale>=698 ||
        y/scale<audio_table_y || y/scale>=244) return;
    commit_soundfont_edit();
    m_soundfont_scroll=std::clamp<Integer>(m_soundfont_scroll+lines,0,
        std::max<Integer>(0,static_cast<Integer>(panel->m_pane_sound->m__soundfonts.size())-audio_visible_rows));
}

bool SettingsDialog::dialog_presentation_finished() const noexcept
{
    return m_state.load(std::memory_order_acquire) == SettingsDialogState::Closed;
}

void SettingsDialog::dialog_presentation_complete()
{
    // 既存の Frame・モーダルの終了処理は close_once() が管理する。 ホ
    // ストは完了を確認するだけとし、二つ目のプラットフォーム専用ダイアログ経路を作らない。
}

namespace {
Value* settings_document_value(SettingsDialog& dialog, SettingsDocumentAction action)
{
    if (action == SettingsDocumentAction::FullscreenBackground) return drawing_value(dialog,drawing_file);
    if (action == SettingsDocumentAction::BackgroundImage) return general_edit_value(dialog,6105);
    const auto panel = detail_panel(dialog.m_panel);
    if (panel && panel->m_pane_scenario && action >= SettingsDocumentAction::EditorApplication &&
        action <= SettingsDocumentAction::FileApplication)
        return scenario_application(*panel->m_pane_scenario,scenario_application_first+
            static_cast<int>(action)-static_cast<int>(SettingsDocumentAction::EditorApplication));
    return nullptr;
}
}

void SettingsDialog::cancel_document_request()
{
    m_document_token.reset();
    m_document_ticket.reset(); // UI ディスパッチャーに予約済みのコールバックも抑止する。
    request_dialog_presentation_refresh();
}

bool SettingsDialog::request_document(SettingsDocumentAction action, DocumentRequest start, DocumentWrite write)
{
    namespace doc = ::cw::lib::documents;
    const auto panel = detail_panel(m_panel);
    const auto weak = weak_from_this();
    if (document_request_pending() || m_state.load()!=SettingsDialogState::Visible || !panel || weak.expired()) return false;
    doc::Request request;
    Integer row = -1;
    if (action == SettingsDocumentAction::Load || action == SettingsDocumentAction::Save) {
        const bool save = action == SettingsDocumentAction::Save;
        request.operation = save ? doc::Operation::SaveFile : doc::Operation::OpenFiles;
        request.title = save ? "設定ファイルの保存" : "設定ファイルの読み込み";
        request.initial_location = save ? "新規設定.wssx" : "";
        request.filters = {{"CardWirthPy設定ファイル","wssx"},{"XMLドキュメント","xml"},{"すべてのファイル","*"}};
        // プロバイダーからの設定ファイルは直ちに読み取り、永続的なパスとしては保存し
        // ない。 下の init_settings が終わるまで利用権を保持する。
        if (!save) request.transient_read_limit = 16u * 1024u * 1024u;
    } else if (action == SettingsDocumentAction::SoundFonts) {
        if (!panel->m_pane_sound) return false;
        request.title = "MIDIの演奏に使用するサウンドフォント選択";
        request.initial_location = "Data/SoundFont";
        request.multiple = true;
        request.filters = {{"MIDIサウンドフォント","sf2"},{"すべてのファイル","*"}};
    } else if (action == SettingsDocumentAction::SkinArchive) {
        if (!settings_skin(*this)) return false;
        request.title = "インストールするスキンを選択";
        request.initial_location = "Data/Skin";
        request.multiple = true;
        request.filters = {{"スキンアーカイブ","zip;lzh;cab;xml"},{"すべてのファイル","*"}};
        // install_skin_files はアーカイブを同期的に読み、内容を
        // Data/Skin へコピーする。 一つの共通ドキュメントサービスでプロバ
        // イダーのファイルを一時保存できるようにし、使い捨てのパスは永続化しない。
        request.transient_read_limit = 512u * 1024u * 1024u;
    } else if (action == SettingsDocumentAction::ScenarioFolder) {
        const auto pane = panel->m_pane_scenario;
        if (!pane) return false;
        row = pane->m__folder_cursor;
        if (row<0 || row>=static_cast<Integer>(pane->m__folderoftype.size())) return false;
        const auto& type = std::get<0>(pane->m__folderoftype[row]);
        request.title = "「" + (type.empty() ? String("(指定無し)") : type) + "」タイプのスキンでプレイするシナリオのフォルダを選択してください";
        request.operation = doc::Operation::OpenFolder;
        request.initial_location = "Scenario";
    } else if (const auto value = settings_document_value(*this,action)) {
        request.initial_location = value_string(*value);
        if (action >= SettingsDocumentAction::EditorApplication) {
            request.title = "外部アプリを選択";
            // Unix の実行ファイルは拡張子を必要としない。 UI とモデルは共通と
            // し、exe フィルターで Linux/macOS のアプリを隠さない。
            request.filters = {{"すべてのファイル","*"}};
        } else {
            request.title = action == SettingsDocumentAction::FullscreenBackground
                ? "フルスクリーンの背景にするファイルを選択" : "背景画像を選択";
            request.filters = {{"画像ファイル","jpg;jpeg;png;gif;bmp;tiff;xpm"},{"すべてのファイル","*"}};
        }
    } else return false;
    // 非同期取得の前に、既存のセッションルートを基準として一度だけ解決する。 作業ディ
    // レクトリーは変更せず、単体モードで UserData への代替経路も追加しない。
    if (!request.initial_location.empty()) {
        std::error_code error;
        const auto absolute = std::filesystem::absolute(std::filesystem::u8path(request.initial_location),error);
        if (!error) request.initial_location = ::cw::lib::encoding::path_to_utf8(absolute);
    }
    const auto token = std::make_shared<int>(0);
    m_document_token = token;
    m_audio_slider = -1;
    request_dialog_presentation_refresh();
    if (!start) start = ::cw::dialog::request_presentation_document;
    try {
        auto ticket = start(request,[weak,token,panel,action,row,write](doc::Result result) {
            if (const auto self = weak.lock(); self && detail_panel(self->m_panel)==panel)
                self->finish_document_request(token,action,row,std::move(result),write);
        });
        // バックエンドは Unsupported の場合も含め、同期的に完了する可
        // 能性がある。 完了処理が開いた新しい要求に、古いチケットを上書きしない。
        if (m_document_token==token) {
            m_document_ticket = std::move(ticket);
            if (!m_document_ticket || !m_document_ticket->pending())
                finish_document_request(token,action,row,{doc::Status::Failed,{},"ファイル選択を開始できませんでした。"});
        }
    } catch (const std::exception& error) {
        finish_document_request(token,action,row,{doc::Status::Failed,{},error.what()});
    } catch (...) {
        finish_document_request(token,action,row,{doc::Status::Failed,{},"ファイル選択に失敗しました。"});
    }
    return true;
}

void SettingsDialog::finish_document_request(const std::shared_ptr<int>& token,
    SettingsDocumentAction action, Integer row, ::cw::lib::documents::Result result, DocumentWrite write)
{
    namespace doc = ::cw::lib::documents;
    if (m_document_token!=token || m_state.load()!=SettingsDialogState::Visible) return;
    cancel_document_request();
    if (result.status==doc::Status::Cancelled) return;
    const bool multiple = action==SettingsDocumentAction::SoundFonts ||
                          action==SettingsDocumentAction::SkinArchive;
    if (result.status!=doc::Status::Success || result.documents.empty() ||
        (!multiple && result.documents.size()!=1)) {
        settings_warning(result.error.empty() ? "ファイルを選択できませんでした。" : result.error);
        return;
    }
    std::vector<String> paths;
    if(action==SettingsDocumentAction::Save) {
        const auto panel=detail_panel(m_panel);
        if(!panel) return;
        const auto write_token=std::make_shared<int>(0);
        m_document_token=write_token;
        request_dialog_presentation_refresh();
        try {
            auto setting=std::make_shared<::cw::setting::Setting>();
            setting->init_settings(std::nullopt,false);
            panel->apply(setting);
            auto bytes=::cw::xmlcreater::serialize_settings(setting,false);
            if(!write) write=::cw::dialog::write_presentation_document;
            const auto weak=weak_from_this();
            auto ticket=write(result.documents.front(),std::move(bytes),[weak,write_token](doc::Result saved) {
                if(const auto self=weak.lock()) self->finish_document_write(write_token,std::move(saved));
            });
            if(m_document_token==write_token) {
                m_document_ticket=std::move(ticket);
                if(!m_document_ticket || !m_document_ticket->pending())
                    finish_document_write(write_token,{doc::Status::Failed,{},"文書の保存を開始できませんでした。"});
            }
        } catch(const std::exception& error) {finish_document_write(write_token,{doc::Status::Failed,{},error.what()});}
          catch(...) {finish_document_write(write_token,{doc::Status::Failed,{},"設定の保存に失敗しました。"});}
        return;
    }
    for (const auto& document : result.documents) {
        if (document.kind!=doc::LocationKind::NativePath || document.location.empty()) {
            // SAF・セキュリティスコープ付きの取り込みには、永続ストレージのアダプターが必要である。
            // content:// URI を std::filesystem のパスとして保存しない。
            settings_warning("選択した文書をローカルファイルとして使用する処理は、この環境では未対応です。");
            return;
        }
        paths.push_back(document.location);
    }
    const auto panel = detail_panel(m_panel);
    if (!panel) return;
    try {
        if (action==SettingsDocumentAction::Load) {
            auto setting = std::make_shared<::cw::setting::Setting>();
            setting->init_settings(paths.front(),true);
            panel->load(setting);
            applied();
        } else if (action==SettingsDocumentAction::SoundFonts && panel->m_pane_sound) {
            const auto before = panel->m_pane_sound->m__soundfonts;
            panel->m_pane_sound->add_soundfont_files(paths);
            if (before!=panel->m_pane_sound->m__soundfonts) applied();
        } else if (action==SettingsDocumentAction::SkinArchive && settings_skin(*this)) {
            const auto skin=settings_skin(*this);
            const auto selected=value_integer(skin->m_ch_skin,-1);
            const auto previous=selected>=0&&selected<static_cast<Integer>(skin->m_skindirs.size())
                ? value_string(skin->m_skindirs[selected]) : String{};
            if(skin->install_skin_files(paths)) {
                m_skin_description_scroll=0;
                const auto current=value_integer(skin->m_ch_skin,-1);
                if(current>=0&&current<static_cast<Integer>(skin->m_skindirs.size())&&
                    value_string(skin->m_skindirs[current])!=previous) applied();
                request_dialog_presentation_refresh();
            }
        } else if (action==SettingsDocumentAction::ScenarioFolder && panel->m_pane_scenario) {
            panel->m_pane_scenario->apply_folder_choice(row,paths.front());
        } else if (const auto value = settings_document_value(*this,action); value && value_string(*value)!=paths.front()) {
            *value = paths.front();
            applied();
        }
    } catch (const std::exception& error) {
        settings_warning("選択したファイルの処理に失敗しました。\n"+String(error.what()));
    } catch (...) {
        settings_warning("選択したファイルの処理に失敗しました。");
    }
}

void SettingsDialog::finish_document_write(const std::shared_ptr<int>& token,::cw::lib::documents::Result result)
{
    if(m_document_token!=token || m_state.load()!=SettingsDialogState::Visible) return;
    cancel_document_request();
    if(result.status!=::cw::lib::documents::Status::Success && result.status!=::cw::lib::documents::Status::Cancelled)
        settings_warning(result.error.empty()?"設定の保存に失敗しました。":result.error);
}

void SettingsDialog::initialize(std::shared_ptr<::cw::lib::Window> arg_parent)
{
    cancel_document_request();
    dialog_retained_surface().reset();
    m_state.store(SettingsDialogState::Constructing, std::memory_order_release);
    m_close_started.store(false, std::memory_order_release);
    m_parent_native_handle = arg_parent ? arg_parent->native_handle : 0;
    m_native_handle = 0;
    // Python 専用の cwpy_debug 印と同様、SettingsDialog
    // は全体のデバッグモードに関係なくスクリーンショット取得を抑止する。
    m_cwpy_debug = true;
    auto setting = current_setting();
    std::shared_ptr<::cw::dialog::settings::SettingsDialog> self;
    try {
        self = shared_from_this();
    } catch (const std::bad_weak_ptr&) {
        // モデルだけを扱う一部のテストは、ダイアログをスタッ
        // ク上に作る。 その互換経路は非所有のままにし、通
        // 常のアプリのダイアログは実際の共有所有権を使う。
        self = std::shared_ptr<::cw::dialog::settings::SettingsDialog>(
            this, [](::cw::dialog::settings::SettingsDialog*) {});
    }
    if (setting->m_show_advancedsettings) {
        auto panel = std::make_shared<::cw::dialog::settings::SettingsPanel>();
        panel->initialize(self);
        m_panel = std::static_pointer_cast<::cw::lib::Object>(panel);
        m_presentation_tab = std::clamp<Integer>(value_integer(panel->m_note),0,settings_tab_count()-1);
    } else {
        auto panel = std::make_shared<::cw::dialog::settings::SimpleSettingsPanel>();
        panel->initialize(self);
        m_panel = std::static_pointer_cast<::cw::lib::Object>(panel);
    }
    _bind();
    _do_layout();
    m_state.store(SettingsDialogState::Visible, std::memory_order_release);
}

void SettingsDialog::_bind()
{
    // 入力操作は共通 DialogPresentation のコントロールで公開する。
}

void SettingsDialog::_do_layout()
{
    // 配置の計算は SettingsDialog::dialog_presentation() だけで行う。
}

// Python は wx の入力種別と最上位の所有者を確認してから適用可能
// にする。ネイティブの表示側は編集したコントロールを直接記録する。 この
// 旧 Event アダプターには wx の所有者・種別の関係がないため、
// 渡されたイベントを編集通知と扱い、通常の伝播用に false を返す。
bool SettingsDialog::OnFilterEvent(std::shared_ptr<::cw::lib::Event> arg_event)
{
    // この旧 wx 形式のフィルターはネイティブの表示側には登録しない。
    // 共通設定の操作・値ハンドラーが applied() を直接呼ぶ。互換
    // イベントが渡された場合も適用可能にし、伝播用に false を返す。
    if (arg_event) applied();
    return false;
}

void SettingsDialog::applied()
{
    request_dialog_presentation_refresh();
    if (auto detail_settings_panel = detail_panel(m_panel)) {
        if (detail_settings_panel->m_btn_apply) detail_settings_panel->m_btn_apply->id = 1;
    } else if (auto simple_settings_panel = simple_panel(m_panel)) {
        if (simple_settings_panel->m_btn_apply) simple_settings_panel->m_btn_apply->id = 1;
    }
}

void SettingsDialog::clear_applied()
{
    request_dialog_presentation_refresh();
    if (auto detail_settings_panel = detail_panel(m_panel)) {
        if (detail_settings_panel->m_btn_apply) detail_settings_panel->m_btn_apply->id = 0;
    } else if (auto simple_settings_panel = simple_panel(m_panel)) {
        if (simple_settings_panel->m_btn_apply) simple_settings_panel->m_btn_apply->id = 0;
    }
}

void SettingsDialog::OnClose(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    const auto state = m_state.load(std::memory_order_acquire);
    // Python は適用・OK の完了中、親を無効にする。 非同期の
    // ゲーム側ディスパッチャーでも finish_apply までパネ
    // ルを保持し、二度目のネイティブ終了通知で最初の待機を飛ばさない。
    if (state == SettingsDialogState::Closing || state == SettingsDialogState::Closed) return;
    if (state == SettingsDialogState::Applying) {
        m_state.store(SettingsDialogState::Closing, std::memory_order_release);
        return;
    }
    close_once();
}

void SettingsDialog::close_once()
{
    cancel_document_request();
    if (m_close_started.exchange(true, std::memory_order_acq_rel)) return;
    m_state.store(SettingsDialogState::Closing, std::memory_order_release);
    // Frame がモデルを所有し、ネイティブバックエンドは生ポインターを保持す
    // る。 終了コールバックが戻るまでローカルの共有参照を保ち、その後 CardWirth
    // のダイアログ階層・メニューカードのロックを一度だけ解放する。
    std::shared_ptr<::cw::frame::Frame> frame;
    std::shared_ptr<::cw::lib::Object> keep_alive;
    if (::cw::g_cwpy) {
        if (const auto* value = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_frame.storage()))
            frame = std::dynamic_pointer_cast<::cw::frame::Frame>(*value);
        if (frame && frame->m_active_dialog_model.get() == this)
            keep_alive = frame->m_active_dialog_model;
    }
    if (auto detail_settings_panel = detail_panel(m_panel)) detail_settings_panel->close();
    else if (auto simple_settings_panel = simple_panel(m_panel)) simple_settings_panel->close();
    m_panel = {};
    if (frame && keep_alive) frame->kill_dlg(std::nullopt, false, true);
    m_state.store(SettingsDialogState::Closed, std::memory_order_release);
}

bool SettingsDialog::is_applying() const noexcept
{
    const auto state = m_state.load(std::memory_order_acquire);
    return state == SettingsDialogState::Applying || state == SettingsDialogState::Closing;
}

void SettingsDialog::request_apply(bool arg_close_after_apply)
{
    if (document_request_pending()) return;
    auto expected = SettingsDialogState::Visible;
    if (!m_state.compare_exchange_strong(expected, SettingsDialogState::Applying,
                                         std::memory_order_acq_rel,
                                         std::memory_order_acquire)) return;

    std::shared_ptr<SettingsDialog> keep_alive;
    try {
        keep_alive = shared_from_this();
    } catch (const std::bad_weak_ptr&) {
        m_state.store(SettingsDialogState::Visible, std::memory_order_release);
        return;
    }
    const ::cw::lib::Callback apply_on_game =
        [keep_alive, arg_close_after_apply](const ::cw::lib::Arguments&,
                                            const ::cw::lib::KeywordArguments&) -> Value {
        bool success = true;
        String error;
        const auto setting = current_setting();
        const SettingsApplyCommitScope apply_commit;
        std::shared_ptr<::cw::setting::Setting> rollback;
        if (setting) {
            rollback = std::make_shared<::cw::setting::Setting>(*setting);
            if (setting->m_local)
                rollback->m_local = std::make_shared<::cw::setting::LocalSetting>(*setting->m_local);
        }
        try {
            // FileSync は最初の予約済み失敗を全体に通知する。 適用前
            // から予約されていた書き込みを完了し、その失敗を今回の副作用や Settings.xml
            // の書き込み失敗と取り違えないようにする。
            if (::cw::g_fsync) ::cw::g_fsync->sync();
            // Python の SettingsPanel.apply は、wx
            // が全コントロール値を集めた後に実行する。 C++ では別名の編集
            // 値確定処理を、このゲームコマンドのディスパッチャー上でだけ呼ぶ。
            if (const auto detail = detail_panel(keep_alive->m_panel)) detail->apply(setting);
            else if (const auto simple = simple_panel(keep_alive->m_panel)) simple->apply();
            // 設定適用は、PC のカード上限を下げた時の調整など、
            // 関連する宿・カードの書き込みも予約する場合がある。
            // Settings.xml を予約する前にそれらの失敗
            // を確定させる。 そうしないと、Settings.xml
            // の公開後に下の sync() が以前の副作用の失敗
            // を返し、メモリー上の設定だけが巻き戻ることになる。
            if (::cw::g_fsync) ::cw::g_fsync->sync();
            // 終了時に Settings.write を行う Python とは意図的
            // に異なり、全プラットフォームで確定した設定を即座に保存する。 ブラウザー
            // の再読み込みでは CWPy::_quit が実行されないため、適用をメモ
            // リー内だけにすると、ゼロを含むメッセージ速度などの編集設定が失われる。
            // 文書としての「保存」による書き出しは、実際の OK・適用とは別にする。
            if (setting) setting->write();
            // 上で予約した Settings.xml の書き込みを待ち、
            // 失敗した場合は今回の適用を成功として通知する前に報告する。
            if (::cw::g_fsync) ::cw::g_fsync->sync();
        } catch (const std::exception& ex) {
            success = false;
            error = ex.what();
        } catch (...) {
            success = false;
            error = "設定の適用中に不明なエラーが発生しました。";
        }
        apply_commit.commit->store(success, std::memory_order_release);
        if (!success && setting && rollback) {
            const bool restore_soundfonts = setting->m_soundfonts_typed != rollback->m_soundfonts_typed;
            const bool had_soundfonts = std::any_of(setting->m_soundfonts_typed.begin(),
                setting->m_soundfonts_typed.end(), [](const auto& row) { return std::get<1>(row); });
            *setting = *rollback;
            try {
                if (::cw::g_cwpy && ::cw::g_cwpy->m_setting == setting) {
                    ::cw::g_cwpy->set_mastervolume(static_cast<Integer>(std::lround(setting->m_vol_master * 100.0)));
                    if (restore_soundfonts) SettingsPanel_apply_func_sfonts(had_soundfonts);
                }
            } catch (...) {
                ::cw::util::print_ex(std::nullopt);
                error += "\n音声設定の復元に失敗しました。";
            }
        }

        const ::cw::lib::Callback finish_on_ui =
            [keep_alive, arg_close_after_apply, success, error](
                const ::cw::lib::Arguments&,
                const ::cw::lib::KeywordArguments&) -> Value {
            keep_alive->finish_apply(arg_close_after_apply, success, error);
            return Value{};
        };
        std::shared_ptr<::cw::frame::Frame> frame;
        if (::cw::g_cwpy) {
            if (const auto* value = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_frame.storage()))
                frame = std::dynamic_pointer_cast<::cw::frame::Frame>(*value);
        }
        if (frame) frame->exec_func(finish_on_ui);
        else finish_on_ui(::cw::lib::Arguments{}, ::cw::lib::KeywordArguments{});
        return Value{};
    };
    if (::cw::g_cwpy) ::cw::g_cwpy->exec_func(apply_on_game);
    else apply_on_game(::cw::lib::Arguments{}, ::cw::lib::KeywordArguments{});
}

void SettingsDialog::finish_apply(bool arg_close_after_apply, bool arg_success,
                                  String arg_error)
{
    const bool close_requested = arg_close_after_apply ||
        m_state.load(std::memory_order_acquire) == SettingsDialogState::Closing;
    if (!arg_success) {
        settings_warning(arg_error.empty() ? "設定を適用できませんでした。" : arg_error);
    }
    if (close_requested && arg_success) {
        close_once();
        return;
    }
    m_state.store(SettingsDialogState::Visible, std::memory_order_release);
    if (arg_success) clear_applied();
    request_dialog_presentation_refresh();
}

void SettingsDialog::close_after_apply()
{
    // 開いている間は Frame が実体のダイアログを所有する。その所有権を保持
    // し、 予約済みの終了処理が破棄されたモデルを呼ばないようにする。全ホストで
    // OnClose の keep_alive と同じ生存期間の境界を使う。
    std::shared_ptr<SettingsDialog> keep_alive;
    if (::cw::g_cwpy) {
        if (const auto* value = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_frame.storage())) {
            if (const auto frame = std::dynamic_pointer_cast<::cw::frame::Frame>(*value);
                frame && frame->m_active_dialog_model.get() == this) {
                keep_alive = std::dynamic_pointer_cast<SettingsDialog>(frame->m_active_dialog_model);
            }
        }
    }
    if (!keep_alive) {
        // 単独・モデル専用の呼び出し側には安全に保持できる所有者
        // がない。 生ポインターを予約せず、同期的に終了する。
        OnClose(nullptr);
        return;
    }
    if (!::cw::g_cwpy) {
        keep_alive->OnClose(nullptr);
        return;
    }
    ::cw::g_cwpy->exec_func([keep_alive](const auto&, const auto&) {
        if (!::cw::g_cwpy) return Value{};
        std::shared_ptr<::cw::frame::Frame> frame;
        if (const auto* value = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_frame.storage()))
            frame = std::dynamic_pointer_cast<::cw::frame::Frame>(*value);
        const auto close = [keep_alive](const auto&, const auto&) {
            keep_alive->OnClose(nullptr);
            return Value{};
        };
        if (frame) frame->exec_func(close);
        else close(::cw::lib::Arguments{}, ::cw::lib::KeywordArguments{});
        return Value{};
    });
}

void SettingsDialog::show_details()
{
    // ネイティブパネルのコールバックも Presentation と同
    // じ所有規則を使う。 非同期の適用が使用中のパネルは置き換えない。
    if (m_state.load(std::memory_order_acquire) != SettingsDialogState::Visible ||
        document_request_pending()) return;
    cancel_document_request();
    auto simple = simple_panel(m_panel);
    if (!simple) return;
    auto detail = std::make_shared<::cw::dialog::settings::SettingsPanel>();
    std::shared_ptr<::cw::dialog::settings::SettingsDialog> self;
    try {
        self = shared_from_this();
    } catch (const std::bad_weak_ptr&) {
        self = std::shared_ptr<::cw::dialog::settings::SettingsDialog>(
            this, [](::cw::dialog::settings::SettingsDialog*) {});
    }
    detail->initialize(self);
    if (detail->m_pane_gene) {
        detail->m_pane_gene->m_cb_debug = simple->m_cb_debug;
        if (detail->m_pane_gene->m_skin && simple->m_skin) detail->m_pane_gene->m_skin->copy_values(simple->m_skin);
        if (detail->m_pane_gene->m_expand && simple->m_expand) detail->m_pane_gene->m_expand->copy_values(simple->m_expand);
    }
    if (detail->m_pane_draw && detail->m_pane_draw->m_speed && simple->m_speed)
        detail->m_pane_draw->m_speed->copy_values(simple->m_speed);
    if (detail->m_pane_sound) {
        detail->m_pane_sound->m_cb_playbgm = simple->m_cb_playbgm ? simple->m_cb_playbgm->id != 0 : false;
        detail->m_pane_sound->m_cb_playsound = simple->m_cb_playsound ? simple->m_cb_playsound->id != 0 : false;
    }
    if (::cw::dialog::is_all_mode() && detail->m_pane_ui &&
        simple->m_cb_all_status_dialog)
        detail->m_pane_ui->m_cb_show_all_status_dialog =
            simple->m_cb_all_status_dialog->id != 0;
    m_panel = std::static_pointer_cast<::cw::lib::Object>(detail);
    m_presentation_tab = std::clamp<Integer>(value_integer(detail->m_note),0,settings_tab_count()-1);
    m_presentation_page = 0;
    request_dialog_presentation_refresh();
    m_state.store(SettingsDialogState::Visible, std::memory_order_release);
}

void SettingsPanel::initialize(std::shared_ptr<::cw::dialog::settings::SettingsDialog> arg_parent)
{
    m_parent = arg_parent.get();
    // Python と同様、最後に使った詳細設定のページを復元する。 常に先頭から始めると、
    // 終了時に cwpy.settingtab も上書きされ、ページ記憶が機能しなくなる。
    m_note = ::cw::g_cwpy ? std::clamp<Integer>(::cw::g_cwpy->m_settingtab,0,settings_tab_count()-1) : Integer{0};
    m_btn_dflt = Integer{1};
    m_btn_save = Integer{2};
    m_btn_load = Integer{3};
    m_btn_ok = Integer{4};
    m_btn_cncl = Integer{5};
    m_btn_apply = make_window("適用");
    m_btn_apply->id = 0;
    s_versioninfo = make_window(::cw::g_APP_NAME);

    auto parent = std::make_shared<::cw::lib::Panel>();
    m_pane_gene = std::make_shared<::cw::dialog::settings::GeneralSettingPanel>();
    m_pane_gene->initialize(parent);
    if (m_pane_gene->m_skin) m_pane_gene->m_skin->m_parent = m_parent;
    m_pane_draw = std::make_shared<::cw::dialog::settings::DrawingSettingPanel>();
    m_pane_draw->initialize(parent, false, std::nullopt, true);
    m_pane_sound = std::make_shared<::cw::dialog::settings::AudioSettingPanel>();
    m_pane_sound->initialize(parent);
    m_pane_sound->m_settings_parent = m_parent;
    m_pane_font = std::make_shared<::cw::dialog::settings::FontSettingPanel>();
    m_pane_font->initialize(parent, false, std::nullopt, true);
    m_pane_scenario = std::make_shared<::cw::dialog::settings::ScenarioSettingPanel>();
    m_pane_scenario->initialize(parent);
    m_pane_scenario->m_settings_parent = m_parent;
    m_pane_ui = std::make_shared<::cw::dialog::settings::UISettingPanel>();
    m_pane_ui->initialize(parent);
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting) load(::cw::g_cwpy->m_setting);
    _bind();
    _do_layout();
}

void SettingsPanel::_bind()
{
    // 全ホストで同じ SettingsDialog の Presentation
    // の生存期間を使い、 ホストのサービスに二つ目のコントロール構成を接続しない。
}

void SettingsPanel::load(std::shared_ptr<::cw::setting::Setting> arg_setting)
{
    if (!arg_setting) return;
    if (m_parent) m_parent->cancel_document_request();
    if (m_pane_gene) m_pane_gene->load(arg_setting);
    if (m_pane_draw) m_pane_draw->load(arg_setting, arg_setting->m_local);
    if (m_pane_sound) m_pane_sound->load(arg_setting);
    if (m_pane_font) m_pane_font->load(arg_setting, arg_setting->m_local);
    if (m_pane_scenario) m_pane_scenario->load(arg_setting);
    if (m_pane_ui) m_pane_ui->load(arg_setting);
}

void SettingsPanel::OnSave(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m_parent) m_parent->request_document(SettingsDocumentAction::Save);
}

void SettingsPanel::OnLoad(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m_parent) m_parent->request_document(SettingsDocumentAction::Load);
}

void SettingsPanel::OnDefault(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m_parent) m_parent->cancel_document_request();
    auto setting = current_setting();
    const auto pane = value_integer(m_note);
    if (pane == 0 && m_pane_gene) m_pane_gene->init_values(setting);
    else if (pane == 1 && m_pane_draw) m_pane_draw->init_values(setting, setting->m_local);
    else if (pane == 2 && m_pane_sound) m_pane_sound->init_values(setting);
    else if (pane == 3 && m_pane_font) m_pane_font->init_values(setting, setting->m_local);
    else if (pane == 4 && m_pane_scenario) m_pane_scenario->init_values(setting);
    else if (pane == 5 && m_pane_ui) m_pane_ui->init_values(setting);
    if (m_btn_apply) m_btn_apply->id = 1;
}

void SettingsPanel::OnOk(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m_parent) m_parent->request_apply(true);
}

void SettingsPanel::OnApply(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m_parent) m_parent->request_apply(false);
}

// Py の独立した保存処理では、記録・個人ポケット・プレミアカードの実行状態
// への通知が起き得る。 ネイティブ版ではこれら三つのコールバックを update
// に応じて制限し、文書の書き出しで現在の記録や宿の操作状態を変えない。
// 通常の適用では通知を維持し、保存では独立した対象をシリアライズする。
void SettingsPanel::apply(std::shared_ptr<::cw::setting::Setting> arg_setting)
{
    if (!arg_setting) return;
    const bool update = ::cw::g_cwpy && arg_setting == ::cw::g_cwpy->m_setting;
    // Python と同様、保存などの独立した Setting
    // へ適用する場合も、cwpy の実際の設定を参照
    // する。 編集対象のオブジェクトとは区別して扱う。
    const bool can_levelup = !::cw::g_cwpy || !::cw::g_cwpy->m_setting
        ? true
        : !(::cw::g_cwpy->is_debugmode() && ::cw::g_cwpy->m_setting->m_no_levelup_in_debugmode);
    bool updatestatusbar = false;
    bool updatebg = false;
    bool updatecardimg = false;
    bool updatemcardimg = false;
    bool updatemessage = false;
    bool updatecurtain = false;
    bool updatefullscreen = false;
    bool flag_fontupdate = false;
    auto gene = m_pane_gene;
    if (m_pane_font && arg_setting->m_local) {
        const auto result = m_pane_font->apply_localsettings(arg_setting->m_local);
        flag_fontupdate = std::get<0>(result);
        updatecardimg = std::get<1>(result);
        updatemcardimg = std::get<2>(result);
        updatemessage = std::get<3>(result);
    }
    if (gene) {
        if (value_has(gene->m_cb_debug)) {
            const bool debug = value_bool(gene->m_cb_debug);
            if (update && ::cw::g_cwpy && debug != ::cw::g_cwpy->is_debugmode()) {
                // Python は cwpy.exec_func で set_debug
                // を送るが、C++ はここで既に ApplySettings のゲー
                // ムコマンドに入っている。 別の非同期書き込みを入れ子で予約しない。
                ::cw::g_cwpy->set_debug(debug);
            }
            // 保存コマンドは独立した Setting を使う。Python の実体の
            // set_debug 経路は最終的に debug_saved へ値をコピー
            // するため、 実行状態を変えずに、独立した対象も同じように保存可能にする。
            arg_setting->m_debug = debug;
            arg_setting->m_debug_saved = debug;
        }
        if (value_has(gene->m_cb_show_tiles) && arg_setting->m_show_tiles != value_bool(gene->m_cb_show_tiles)) {
            arg_setting->m_show_tiles = value_bool(gene->m_cb_show_tiles);
            updatestatusbar = true;
        }
        if (value_has(gene->m_cb_enabled_right_flick)) arg_setting->m_enabled_right_flick = value_bool(gene->m_cb_enabled_right_flick);
        if (value_has(gene->m_cb_can_repeatlclick)) arg_setting->m_can_repeatlclick = value_bool(gene->m_cb_can_repeatlclick);
        if (value_has(gene->m_cb_nolevelup)) arg_setting->m_no_levelup_in_debugmode = value_bool(gene->m_cb_nolevelup);
        if (value_has(gene->m_sc_initmoneyamount)) arg_setting->m_initmoneyamount = value_integer(gene->m_sc_initmoneyamount);
        if (value_has(gene->m_cb_initmoneyisinitialcash)) arg_setting->m_initmoneyisinitialcash = value_bool(gene->m_cb_initmoneyisinitialcash);
        if (value_has(gene->m_cb_autosavepartyrecord)) arg_setting->m_autosave_partyrecord = value_bool(gene->m_cb_autosavepartyrecord);
        if (value_has(gene->m_cb_overwritepartyrecord)) arg_setting->m_overwrite_partyrecord = value_bool(gene->m_cb_overwritepartyrecord);
        if (value_has(gene->m_tx_ssinfoformat)) arg_setting->m_ssinfoformat = value_string(gene->m_tx_ssinfoformat);
        if (value_has(gene->m_tx_ssfnameformat)) arg_setting->m_ssfnameformat = value_string(gene->m_tx_ssfnameformat);
        if (value_has(gene->m_tx_cardssfnameformat)) arg_setting->m_cardssfnameformat = value_string(gene->m_tx_cardssfnameformat);
        if (value_integer(gene->m_ch_ssinfocolor) == 1) {
            arg_setting->m_ssinfofontcolor = ::cw::lib::Bytes{255, 255, 255};
            arg_setting->m_ssinfobackcolor = ::cw::lib::Bytes{0, 0, 0};
        } else {
            arg_setting->m_ssinfofontcolor = ::cw::lib::Bytes{0, 0, 0};
            arg_setting->m_ssinfobackcolor = ::cw::lib::Bytes{255, 255, 255};
        }
        if (value_has(gene->m_tx_ssinfobackimage)) arg_setting->m_ssinfobackimage = value_string(gene->m_tx_ssinfobackimage);
        if (value_has(gene->m_cb_sswithstatusbar)) arg_setting->m_sswithstatusbar = value_bool(gene->m_cb_sswithstatusbar);
        switch (value_integer(gene->m_ch_messagelog_type, 1)) {
        case 0: arg_setting->m_messagelog_type = ::cw::setting::g_LOG_SINGLE; break;
        case 2: arg_setting->m_messagelog_type = ::cw::setting::g_LOG_COMPRESS; break;
        default: arg_setting->m_messagelog_type = ::cw::setting::g_LOG_LIST; break;
        }
        arg_setting->m_startupscene = value_integer(gene->m_ch_startupscene, 0) == 1
            ? ::cw::setting::g_OPEN_LAST_BASE : ::cw::setting::g_OPEN_TITLE;
        if (value_has(gene->m_sc_backlogmax)) {
            const auto backlogmax = value_integer(gene->m_sc_backlogmax);
            if (backlogmax != arg_setting->m_backlogmax) {
                if (update) {
                    SettingsPanel_apply_func(backlogmax);
                    updatestatusbar = true;
                } else {
                    arg_setting->m_backlogmax = std::max<Integer>(0, backlogmax);
                }
            }
        }
        if (gene->m_expand) gene->m_expand->apply_expand(arg_setting);
    }

    // Drawing/Font/Scenario/UIの実体は別段階のパネルだが、既に値があれば
    // Python版applyと同じく設定へ反映する。
    if (m_pane_draw) {
        if (value_has(m_pane_draw->m_cb_smooth_bg) && arg_setting->m_smoothscale_bg != value_bool(m_pane_draw->m_cb_smooth_bg)) {
            arg_setting->m_smoothscale_bg = value_bool(m_pane_draw->m_cb_smooth_bg);
            updatebg = true;
        }
        if (value_has(m_pane_draw->m_cb_smoothing_card_up) && arg_setting->m_smoothing_card_up != value_bool(m_pane_draw->m_cb_smoothing_card_up)) {
            arg_setting->m_smoothing_card_up = value_bool(m_pane_draw->m_cb_smoothing_card_up);
            updatecardimg = updatemcardimg = true;
        }
        if (value_has(m_pane_draw->m_cb_smoothing_card_down) && arg_setting->m_smoothing_card_down != value_bool(m_pane_draw->m_cb_smoothing_card_down)) {
            arg_setting->m_smoothing_card_down = value_bool(m_pane_draw->m_cb_smoothing_card_down);
            updatecardimg = updatemcardimg = true;
        }
        if (m_pane_draw->m_speed) updatemessage |= m_pane_draw->m_speed->apply_speed(arg_setting);
        if (value_has(m_pane_draw->m_cb_whitecursor)) {
            const auto cursor = value_bool(m_pane_draw->m_cb_whitecursor)
                ? ::cw::setting::g_CURSOR_WHITE : ::cw::setting::g_CURSOR_BLACK;
            if (arg_setting->m_cursor_type != cursor) {
                arg_setting->m_cursor_type = cursor;
                if (update) SettingsPanel_apply_func_cursor();
            }
        }
        if (arg_setting->m_local) {
            const auto result = m_pane_draw->apply_localsettings(arg_setting->m_local);
            updatemessage |= std::get<0>(result);
            if (std::get<1>(result)) updatecurtain = true;
            if (std::get<2>(result)) updatefullscreen = true;
        }
    }
    if (m_pane_sound) {
        if (value_has(m_pane_sound->m_cb_playbgm)) arg_setting->m_play_bgm = value_bool(m_pane_sound->m_cb_playbgm);
        if (value_has(m_pane_sound->m_cb_playsound)) arg_setting->m_play_sound = value_bool(m_pane_sound->m_cb_playsound);
        if (value_has(m_pane_sound->m_sl_master)) arg_setting->m_vol_master = ::cw::setting::Setting::wrap_volumevalue(value_integer(m_pane_sound->m_sl_master));
        if (value_has(m_pane_sound->m_sl_sound)) {
            const auto sound_volume = ::cw::setting::Setting::wrap_volumevalue(value_integer(m_pane_sound->m_sl_sound));
            if (arg_setting->m_vol_sound != sound_volume) {
                arg_setting->m_vol_sound = sound_volume;
                arg_setting->m_vol_sound_midi = sound_volume;
            }
        }
        if (value_has(m_pane_sound->m_sl_midi)) arg_setting->m_vol_bgm_midi = ::cw::setting::Setting::wrap_volumevalue(value_integer(m_pane_sound->m_sl_midi));
        if (value_has(m_pane_sound->m_sl_music)) arg_setting->m_vol_bgm = ::cw::setting::Setting::wrap_volumevalue(value_integer(m_pane_sound->m_sl_music));
        const auto old_soundfonts = arg_setting->m_soundfonts_typed;
        const auto enabled_soundfonts = [](const auto& rows) {
            std::vector<std::tuple<String,Integer>> result;
            for (const auto& [path,enabled,volume] : rows)
                if (enabled) result.emplace_back(path,volume);
            return result;
        };
        const auto old_enabled_soundfonts = enabled_soundfonts(old_soundfonts);
        auto soundfonts = std::make_shared<::cw::dialog::settings::SoundFontTable>();
        soundfonts->m_rows = m_pane_sound->m__soundfonts;
        arg_setting->m_soundfonts = std::static_pointer_cast<::cw::lib::Object>(soundfonts);
        arg_setting->update_soundfonts();
        if (update)
            ::cw::g_cwpy->set_mastervolume(static_cast<Integer>(std::lround(arg_setting->m_vol_master * 100.0)));
        // 型付きの表を正本とする。有効な行や音量が変わった場合、 音声コ
        // ールバックが BASS/SDL ミキサーの状態を再構築する。
        if (update && old_enabled_soundfonts != enabled_soundfonts(arg_setting->m_soundfonts_typed))
            SettingsPanel_apply_func_sfonts(!old_enabled_soundfonts.empty());
    }
    if (update) {
        if (updatemessage) ::cw::g_cwpy->update_messagestyle();
        if (updatecurtain) ::cw::g_cwpy->update_curtainstyle();
        if (updatefullscreen) ::cw::g_cwpy->update_fullscreenbackground();
        if (gene && gene->m_skin && gene->m_skin->apply_skin(flag_fontupdate)) {
            updatecardimg = false;
            updatemcardimg = false;
            updatebg = false;
        }
    }
    if (m_pane_font) {
        if (value_has(m_pane_font->m_tx_example)) arg_setting->m_fontexampleformat = value_string(m_pane_font->m_tx_example);
        if (value_has(m_pane_font->m_sc_example)) arg_setting->m_fontexamplepixelsize = value_integer(m_pane_font->m_sc_example, 24);
        // ローカルのフォント行は全般パネルより先に適用済みなので、 四
        // つの更新フラグすべてにローカル設定と描画設定の両方を含める。
    }
    if (m_pane_scenario) {
        if (value_has(m_pane_scenario->m_tx_editor)) arg_setting->m_editor = value_string(m_pane_scenario->m_tx_editor);
        if (value_has(m_pane_scenario->m_cb_selectscenariofromtype)) arg_setting->m_selectscenariofromtype = value_bool(m_pane_scenario->m_cb_selectscenariofromtype);
        if (value_has(m_pane_scenario->m_cb_show_paperandtree)) {
            arg_setting->m_show_paperandtree = value_bool(m_pane_scenario->m_cb_show_paperandtree);
            if (arg_setting->m_show_paperandtree) arg_setting->m_show_scenariotree = false;
        }
        if (value_has(m_pane_scenario->m_cb_write_playlog)) {
            const bool old_write_playlog = arg_setting->m_write_playlog;
            arg_setting->m_write_playlog = value_bool(m_pane_scenario->m_cb_write_playlog);
            if (update && old_write_playlog != arg_setting->m_write_playlog) SettingsPanel_apply_func_advlog();
        }
        if (value_has(m_pane_scenario->m_cb_can_installscenariofromdrop)) arg_setting->m_can_installscenariofromdrop = value_bool(m_pane_scenario->m_cb_can_installscenariofromdrop);
        if (value_has(m_pane_scenario->m_cb_delete_sourceafterinstalled)) arg_setting->m_delete_sourceafterinstalled = value_bool(m_pane_scenario->m_cb_delete_sourceafterinstalled);
        if (value_has(m_pane_scenario->m_tx_filer_dir)) arg_setting->m_filer_dir = value_string(m_pane_scenario->m_tx_filer_dir);
        if (value_has(m_pane_scenario->m_tx_filer_file)) arg_setting->m_filer_file = value_string(m_pane_scenario->m_tx_filer_file);
        if (value_has(m_pane_scenario->m_cb_open_lastscenario)) arg_setting->m_open_lastscenario = value_bool(m_pane_scenario->m_cb_open_lastscenario);
        arg_setting->m_folderoftype.clear();
        const auto row_count = m_pane_scenario->m__folderoftype.size();
        for (std::size_t row = 0; row + 1 < row_count; ++row) {
            arg_setting->m_folderoftype.push_back(m_pane_scenario->m__folderoftype[row]);
        }
    }
    if (m_pane_ui) {
        if (value_has(m_pane_ui->m_cb_can_skipwait)) arg_setting->m_can_skipwait = value_bool(m_pane_ui->m_cb_can_skipwait);
        if (value_has(m_pane_ui->m_cb_can_skipanimation)) arg_setting->m_can_skipanimation = value_bool(m_pane_ui->m_cb_can_skipanimation);
        if (value_has(m_pane_ui->m_cb_can_skipwait_with_wheel)) arg_setting->m_can_skipwait_with_wheel = value_bool(m_pane_ui->m_cb_can_skipwait_with_wheel);
        if (value_has(m_pane_ui->m_cb_can_forwardmessage_with_wheel)) arg_setting->m_can_forwardmessage_with_wheel = value_bool(m_pane_ui->m_cb_can_forwardmessage_with_wheel);
        if (value_has(m_pane_ui->m_cb_wait_usecard)) arg_setting->m_wait_usecard = value_bool(m_pane_ui->m_cb_wait_usecard);
        if (value_has(m_pane_ui->m_cb_enlarge_beastcardzoomingratio)) arg_setting->m_enlarge_beastcardzoomingratio = value_bool(m_pane_ui->m_cb_enlarge_beastcardzoomingratio);
        if (value_has(m_pane_ui->m_cb_autoenter_on_sprite)) arg_setting->m_autoenter_on_sprite = value_bool(m_pane_ui->m_cb_autoenter_on_sprite);
        if (value_has(m_pane_ui->m_cb_quickdeal)) arg_setting->m_quickdeal = value_bool(m_pane_ui->m_cb_quickdeal);
        if (value_has(m_pane_ui->m_cb_allquickdeal)) arg_setting->m_all_quickdeal = value_bool(m_pane_ui->m_cb_allquickdeal);
        if (value_has(m_pane_ui->m_cb_showallselectedcards)) arg_setting->m_show_allselectedcards = value_bool(m_pane_ui->m_cb_showallselectedcards);
        if (value_has(m_pane_ui->m_ch_show_statustime)) {
            const auto selection = value_integer(m_pane_ui->m_ch_show_statustime, 0);
            const auto show_statustime = selection == 1 ? String("True") : (selection == 2 ? String("False") : String("NotEventTime"));
            if (arg_setting->m_show_statustime != show_statustime) {
                arg_setting->m_show_statustime = show_statustime;
                updatecardimg = true;
            }
        }
        if (value_has(m_pane_ui->m_cb_show_cardkind) && arg_setting->m_show_cardkind != value_bool(m_pane_ui->m_cb_show_cardkind)) {
            arg_setting->m_show_cardkind = value_bool(m_pane_ui->m_cb_show_cardkind);
            updatemcardimg = true;
        }
        if (value_has(m_pane_ui->m_cb_show_premiumicon) && arg_setting->m_show_premiumicon != value_bool(m_pane_ui->m_cb_show_premiumicon)) {
            arg_setting->m_show_premiumicon = value_bool(m_pane_ui->m_cb_show_premiumicon);
            updatemcardimg = true;
        }
        if (value_has(m_pane_ui->m_cb_showlogwithwheelup)) arg_setting->m_wheelup_operation = value_bool(m_pane_ui->m_cb_showlogwithwheelup)
            ? ::cw::setting::g_WHEEL_SHOWLOG : ::cw::setting::g_WHEEL_SELECTION;
        if (value_has(m_pane_ui->m_cb_show_btndesc)) arg_setting->m_show_btndesc = value_bool(m_pane_ui->m_cb_show_btndesc);
        if (value_has(m_pane_ui->m_cb_statusbarmask) && arg_setting->m_statusbarmask != value_bool(m_pane_ui->m_cb_statusbarmask)) {
            arg_setting->m_statusbarmask = value_bool(m_pane_ui->m_cb_statusbarmask);
            updatestatusbar = true;
        }
        if (value_has(m_pane_ui->m_cb_blink_statusbutton)) arg_setting->m_blink_statusbutton = value_bool(m_pane_ui->m_cb_blink_statusbutton);
        if (value_has(m_pane_ui->m_cb_blink_partymoney)) arg_setting->m_blink_partymoney = value_bool(m_pane_ui->m_cb_blink_partymoney);
        if (value_has(m_pane_ui->m_cb_show_advancedsettings)) arg_setting->m_show_advancedsettings = value_bool(m_pane_ui->m_cb_show_advancedsettings);
        if (value_has(m_pane_ui->m_cb_show_addctrlbtn)) arg_setting->m_show_addctrlbtn = value_bool(m_pane_ui->m_cb_show_addctrlbtn);
        if (value_has(m_pane_ui->m_cb_show_experiencebar)) arg_setting->m_show_experiencebar = value_bool(m_pane_ui->m_cb_show_experiencebar);
        if (value_has(m_pane_ui->m_cb_cautionbeforesaving)) arg_setting->m_caution_beforesaving = value_bool(m_pane_ui->m_cb_cautionbeforesaving);
        if (value_has(m_pane_ui->m_cb_show_personal_cards) && arg_setting->m_show_personal_cards != value_bool(m_pane_ui->m_cb_show_personal_cards)) {
            arg_setting->m_show_personal_cards = value_bool(m_pane_ui->m_cb_show_personal_cards);
            arg_setting->m_last_sendto = 0;
            if (!arg_setting->m_show_personal_cards && arg_setting->m_last_cardpocket == ::cw::g_POCKET_PERSONAL)
                arg_setting->m_last_cardpocket = ::cw::g_POCKET_SKILL;
            if (update) SettingsPanel_apply_func_personal();
        }
        if (value_has(m_pane_ui->m_cb_showbackpackcard)) arg_setting->m_show_backpackcard = value_bool(m_pane_ui->m_cb_showbackpackcard);
        if (value_has(m_pane_ui->m_cb_showbackpackcardatend)) arg_setting->m_show_backpackcardatend = value_bool(m_pane_ui->m_cb_showbackpackcardatend);
        if (value_has(m_pane_ui->m_cb_can_clicksidesofcardcontrol)) arg_setting->m_can_clicksidesofcardcontrol = value_bool(m_pane_ui->m_cb_can_clicksidesofcardcontrol);
        if (value_has(m_pane_ui->m_cb_revertcardpocket)) arg_setting->m_revert_cardpocket = value_bool(m_pane_ui->m_cb_revertcardpocket);
        if (value_has(m_pane_ui->m_cb_showsavedmessage)) arg_setting->m_show_savedmessage = value_bool(m_pane_ui->m_cb_showsavedmessage);
        arg_setting->m_skip_pc_click_motion = value_bool(m_pane_ui->m_cb_skip_pc_click_motion, true);
        if (value_has(m_pane_ui->m_ch_confirm_beforesaving)) {
            const auto selection = value_integer(m_pane_ui->m_ch_confirm_beforesaving, 0);
            arg_setting->m_confirm_beforesaving = selection == 2 ? ::cw::setting::g_CONFIRM_BEFORESAVING_NO
                : (selection == 1 ? ::cw::setting::g_CONFIRM_BEFORESAVING_BASE : ::cw::setting::g_CONFIRM_BEFORESAVING_YES);
        }
        if (value_has(m_pane_ui->m_ch_confirm_dumpcard)) {
            const auto selection = value_integer(m_pane_ui->m_ch_confirm_dumpcard, 0);
            arg_setting->m_confirm_dumpcard = selection == 2 ? ::cw::setting::g_CONFIRM_DUMPCARD_NO
                : (selection == 1 ? ::cw::setting::g_CONFIRM_DUMPCARD_SENDTO : ::cw::setting::g_CONFIRM_DUMPCARD_ALWAYS);
        }
        if (value_has(m_pane_ui->m_cb_confirmbeforeusingcard)) arg_setting->m_confirm_beforeusingcard = value_bool(m_pane_ui->m_cb_confirmbeforeusingcard);
        if (value_has(m_pane_ui->m_cb_noticeimpossibleaction)) arg_setting->m_noticeimpossibleaction = value_bool(m_pane_ui->m_cb_noticeimpossibleaction);
        if (value_has(m_pane_ui->m_cb_showroundautostartbutton) && arg_setting->m_show_roundautostartbutton != value_bool(m_pane_ui->m_cb_showroundautostartbutton)) {
            arg_setting->m_show_roundautostartbutton = value_bool(m_pane_ui->m_cb_showroundautostartbutton);
            updatestatusbar = true;
            if (update && !arg_setting->m_show_roundautostartbutton) SettingsPanel_apply_func_autostart();
        }
        if (::cw::dialog::is_all_mode() && value_has(m_pane_ui->m_cb_show_all_status_dialog))
            arg_setting->m_show_all_status_dialog = value_bool(m_pane_ui->m_cb_show_all_status_dialog);
        if (::cw::dialog::is_all_mode()) {
            ::cw::setting::AllStatusShortcuts shortcuts{};
            for (std::size_t slot = 0; slot < shortcuts.size(); ++slot)
                shortcuts[slot] = static_cast<::cw::setting::AllStatusShortcut>(std::clamp<Integer>(
                    value_integer(m_pane_ui->m_ch_all_shortcuts[slot]), 0,
                    static_cast<Integer>(::cw::setting::AllStatusShortcut::CustomStatus)));
            arg_setting->m_all_status_shortcuts = ::cw::setting::format_all_status_shortcuts(shortcuts);
            arg_setting->m_all_custom_status = m_pane_ui->m_all_custom_status;
        }
        if (value_has(m_pane_ui->m_cb_showautobuttoninentrydialog)) arg_setting->m_show_autobuttoninentrydialog = value_bool(m_pane_ui->m_cb_showautobuttoninentrydialog);
        if (value_has(m_pane_ui->m_cb_protect_staredcard)) arg_setting->m_protect_staredcard = value_bool(m_pane_ui->m_cb_protect_staredcard);
        if (value_has(m_pane_ui->m_cb_protect_premiercard) && arg_setting->m_protect_premiercard != value_bool(m_pane_ui->m_cb_protect_premiercard)) {
            arg_setting->m_protect_premiercard = value_bool(m_pane_ui->m_cb_protect_premiercard);
            if (update) SettingsPanel_apply_func_redrawcard();
        }
        if (value_has(m_pane_ui->m_sc_radius_notdetectmovement)) arg_setting->m_radius_notdetectmovement = std::clamp<Integer>(value_integer(m_pane_ui->m_sc_radius_notdetectmovement),0,50);
        // 全体設定の書き出しで宿データを変更しない。 利用中の宿
        // が変わった後、古いダイアログから別の宿へ書き込まない。
        const auto inn=m_pane_ui->m_pc_limits_inn.lock();
        if(update && inn && ::cw::g_cwpy->m_ydata && inn==*::cw::g_cwpy->m_ydata) {
            const auto previous_limits=::cw::pc_card_limits::read(inn);
            ::cw::pc_card_limits::write(inn,{value_bool(m_pane_ui->m_cb_pc_card_limits),
                value_integer(m_pane_ui->m_sc_pc_skill_limit,50),value_integer(m_pane_ui->m_sc_pc_personal_limit,50),
                value_integer(m_pane_ui->m_sc_pc_beast_limit,50)});
            if(previous_limits!=::cw::pc_card_limits::read(inn)) {
                try { ::cw::pc_card_limits::reconcile_inn(inn); }
                catch(...) {
                    // 完了済みの通常の移動処理は維持するが、設定値は復元する。
                    // 失敗した適用を黙って省かず、再試行できるようにする。
                    ::cw::pc_card_limits::write(inn,previous_limits);
                    throw;
                }
            }
        }
    }
    if (update) {
        if (updatebg) SettingsPanel_apply_func_updatebg();
        if (updatecardimg || updatemcardimg) SettingsPanel_apply_func_updateimg(updatemcardimg);
        if (updatestatusbar) SettingsPanel_apply_func_statusbar();
        apply_levelupparams(can_levelup);
        if (const auto debugger = ::cw::g_cwpy->is_showingdebugger(); debugger && *debugger)
            (*debugger)->refresh_tools();
    }
    if (m_btn_apply) m_btn_apply->id = 0;
}

void SettingsPanel::OnClose(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m_parent) m_parent->OnClose(nullptr);
    else close();
}

void SettingsPanel::close()
{
    if (::cw::g_cwpy) ::cw::g_cwpy->m_settingtab = value_integer(m_note);
}

void SettingsPanel::_do_layout()
{
    // 配置の計算は SettingsDialog::dialog_presentation() だけで行う。
}

void SimpleSettingsPanel::initialize(std::shared_ptr<::cw::dialog::settings::SettingsDialog> arg_parent)
{
    m_parent = arg_parent.get();
    m_box_debug = "デバッグ";
    m_box_expandmode = "拡大表示方式(F4キーで拡大)";
    m_box_skin = "スキン";
    m_box_audio = "音声";
    m_cb_debug = ::cw::g_cwpy && ::cw::g_cwpy->is_debugmode();
    m_cb_playbgm = make_window();
    m_cb_playsound = make_window();
    m_cb_all_status_dialog = make_window();
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting) {
        set_window_value(m_cb_playbgm, ::cw::g_cwpy->m_setting->m_play_bgm);
        set_window_value(m_cb_playsound, ::cw::g_cwpy->m_setting->m_play_sound);
        set_window_value(m_cb_all_status_dialog,
                         ::cw::g_cwpy->m_setting->m_show_all_status_dialog);
    }
    m_btn_ok = Integer{1};
    m_btn_cncl = Integer{2};
    m_btn_details = Integer{3};
    m_btn_apply = make_window("適用");
    m_btn_apply->id = 0;
    auto parent = std::make_shared<::cw::lib::Panel>();
    m_skin = std::make_shared<::cw::dialog::settings::SkinPanel>();
    m_skin->initialize(parent, false);
    m_skin->m_parent = m_parent;
    m_expand = std::make_shared<::cw::dialog::settings::ExpandPanel>();
    m_expand->initialize(parent, false);
    m_speed = std::make_shared<::cw::dialog::settings::SpeedPanel>();
    m_speed->initialize(parent, false);
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting) {
        m_expand->load(::cw::g_cwpy->m_setting);
        m_speed->load(::cw::g_cwpy->m_setting);
    }
    _bind();
    _do_layout();
}

void SimpleSettingsPanel::_bind()
{
    // 既定の Windows アダプターは実際のコントロールを所有し、OnOk/OnApply/OnDetails
    // へ送る。 別のホストはバックエンドの生存期間処理を使う。
}

void SimpleSettingsPanel::OnOk(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m_parent) m_parent->request_apply(true);
}

void SimpleSettingsPanel::OnApply(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m_parent) m_parent->request_apply(false);
}

void SimpleSettingsPanel::apply()
{
    auto setting = current_setting();
    const bool can_levelup = !(::cw::g_cwpy && ::cw::g_cwpy->is_debugmode() && setting->m_no_levelup_in_debugmode);
    const bool debug = value_bool(m_cb_debug);
    if (::cw::g_cwpy && debug != setting->m_debug) {
        // Python は exec_func で set_debug を送るが、C++ の request_apply
        // は既にゲーム側ディスパッチャーでこの確定処理を実行している。
        ::cw::g_cwpy->set_debug(debug);
    }
    if (m_expand) m_expand->apply_expand(setting);
    if (m_speed) {
        const bool message_changed = m_speed->apply_speed(setting);
        if (::cw::g_cwpy && message_changed) ::cw::g_cwpy->update_messagestyle();
    }
    setting->m_play_bgm = window_value(m_cb_playbgm, setting->m_play_bgm);
    setting->m_play_sound = window_value(m_cb_playsound, setting->m_play_sound);
    if (::cw::dialog::is_all_mode())
        setting->m_show_all_status_dialog = window_value(
            m_cb_all_status_dialog, setting->m_show_all_status_dialog);
    if (::cw::g_cwpy)
        ::cw::g_cwpy->set_mastervolume(static_cast<Integer>(std::lround(setting->m_vol_master * 100.0)));
    if (m_skin) m_skin->apply_skin(false);
    apply_levelupparams(can_levelup);
    if (::cw::g_cwpy) {
        if (const auto debugger = ::cw::g_cwpy->is_showingdebugger(); debugger && *debugger)
            (*debugger)->refresh_tools();
    }
    if (m_btn_apply) m_btn_apply->id = 0;
}

void SimpleSettingsPanel::OnClose(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    // Python と同様、親を閉じる。close() だけではパネル内部の後処理し
    // か行わない。 所有者は、繰り返されるキャンセルと保留中の適用の順序も管理する。
    if (m_parent) m_parent->OnClose(nullptr);
    else close();
}

void SimpleSettingsPanel::close()
{
    // 簡易パネルには保存するタブ状態がなく、破棄は SettingsDialog が管理する。
}

void SimpleSettingsPanel::OnDetails(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m_parent) m_parent->show_details();
}

void SimpleSettingsPanel::_do_layout()
{
    // 配置の計算は SettingsDialog::dialog_presentation() だけで行う。
}

void SkinPanel::initialize(std::shared_ptr<::cw::lib::Panel> arg_parent, bool arg_editbuttons)
{
    (void)arg_parent;
    m_editbuttons = arg_editbuttons;
    m_basecash = xml_integer(std::filesystem::u8path("Data/SkinBase/Skin.xml"), "InitialCash", 4000);
    m_ch_skin = Integer{0};
    m_tx_skin = String{};
    m_cb_show_allskin = !::cw::g_cwpy || !::cw::g_cwpy->m_ydata.has_value();
    String current_skin = ::cw::g_cwpy && ::cw::g_cwpy->m_setting
        ? ::cw::g_cwpy->m_setting->m_skindirname : String{};
    if (current_skin.empty() && ::cw::g_cwpy && ::cw::g_cwpy->m_rsrc &&
        !::cw::g_cwpy->m_rsrc->m_skindir.empty()) {
        current_skin = ::cw::lib::encoding::path_to_utf8(
            std::filesystem::u8path(::cw::g_cwpy->m_rsrc->m_skindir).filename());
    }
    update_skins(current_skin, false);
    _bind();
    _do_layout();
}

void SkinPanel::_bind()
{
    // 共通 Presentation がスキン選択と全操作ボタンを各 On* に接続する。
}

void SkinPanel::update_skins(::cw::lib::String arg_skindirname, bool arg_applied)
{
    m_skins.clear();
    m_skindirs.clear();
    m_skin_summarys.clear();
    m_all_skintypes.clear();
    const bool show_all = value_bool(m_cb_show_allskin) || !::cw::g_cwpy ||
        !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata;
    const String current_type = (::cw::g_cwpy && ::cw::g_cwpy->m_setting) ? ::cw::g_cwpy->m_setting->m_skintype : String{};
    const std::filesystem::path root = std::filesystem::u8path("Data/Skin");
    std::error_code ec;
    if (std::filesystem::is_directory(root, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
            // Skin.xml の欠落など、個々の項目の情報取得失敗を後続項
            // 目へ持ち越さない。 Py と同様、他のスキンの走査を続ける。
            std::error_code entry_error;
            if (!entry.is_directory(entry_error)) continue;
            const auto name = ::cw::lib::encoding::path_to_utf8(entry.path().filename());
            const auto skinxml = entry.path() / "Skin.xml";
            entry_error.clear();
            if (!std::filesystem::is_regular_file(skinxml, entry_error)) continue;
            try {
                ::cw::header::GetProperty reader;
                reader.initialize(::cw::lib::encoding::path_to_utf8(skinxml), std::nullopt, std::nullopt);
                const auto type = skin_property(reader, "Type");
                if (std::none_of(m_all_skintypes.begin(), m_all_skintypes.end(),
                                 [&](const Value& value) { return value_string(value) == type; }))
                    m_all_skintypes.emplace_back(type);
                if (!show_all && !current_type.empty() && type != current_type) continue;
                const auto rootattrs = reader.m_attrs.find(std::nullopt);
                const auto version = rootattrs != reader.m_attrs.end() && rootattrs->second.contains("dataVersion")
                    ? rootattrs->second.at("dataVersion") : String("0");
                if (!supported_skin_version(version)) continue;
                m_skindirs.emplace_back(name);
                m_skins.emplace_back(skin_property(reader, "Name", name));
            } catch (...) { ::cw::util::print_ex(std::nullopt); }
        }
    }
    // Py と同様、代替には実行中のスキンを使い、先頭のディレクトリーや架
    // 空の候補は使わない。 実行中のスキンがなければエラーのままにする。
    const auto contains_skin = [&](const String& name) {
        return std::any_of(m_skindirs.begin(), m_skindirs.end(),
                           [&](const Value& value) { return value_string(value) == name; });
    };
    if (!contains_skin(arg_skindirname)) {
        if (!::cw::g_cwpy || !::cw::g_cwpy->m_setting)
            throw std::logic_error("SkinPanel::update_skins requires the running skin setting");
        arg_skindirname = ::cw::g_cwpy->m_setting->m_skindirname;
    }
    Integer selected = -1;
    for (std::size_t i = 0; i < m_skindirs.size(); ++i) {
        if (value_string(m_skindirs[i]) == arg_skindirname) {
            selected = static_cast<Integer>(i);
            break;
        }
    }
    // 言語境界の対応として、Python の list.index がここで出す ValueError を再現する。
    if (selected < 0) throw std::invalid_argument("running skin is absent from skin choices");
    m_ch_skin = selected;
    _choice_skin(false, arg_applied);
}

void SkinPanel::load_allskins()
{
    for (const auto& dir : m_skindirs) _load_skinproperties(value_string(dir));
}

void SkinPanel::_load_skinproperties(::cw::lib::String arg_name)
{
    if (m_skin_summarys.find(arg_name) != m_skin_summarys.end()) return;
    const auto skinpath = std::filesystem::u8path("Data/Skin") / std::filesystem::u8path(arg_name) / "Skin.xml";
    try {
        ::cw::header::GetProperty reader;
        reader.initialize(::cw::lib::encoding::path_to_utf8(skinpath), std::nullopt, std::nullopt);
        const auto type = skin_property(reader, "Type");
        const auto name = skin_property(reader, "Name");
        const auto author = skin_property(reader, "Author");
        const auto description = skin_property(reader, "Description");
        const auto vocation120 = ::cw::util::str2bool(skin_property(reader, "CW120VocationLevel", "False"));
        const auto initialcash = skin_integer(skin_property(reader, "InitialCash", number_string(m_basecash)));
        m_skin_summarys[arg_name] = {type, name, author, description, vocation120, initialcash};
    } catch (...) {
        ::cw::util::print_ex(std::nullopt);
        m_skin_summarys[arg_name] = {"*読込エラー*", "*読込エラー*", "", "Skin.xmlの読み込みでエラーが発生しました。", false, m_basecash};
    }
}

void SkinPanel::OnSkinChoice(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    _choice_skin(false, true);
}

void SkinPanel::_choice_skin(bool arg_init, bool arg_applied)
{
    const Integer index = value_integer(m_ch_skin, -1);
    if (index < 0 || static_cast<std::size_t>(index) >= m_skindirs.size()) {
        m_tx_skin = String{};
        return;
    }
    const auto skin = value_string(m_skindirs[static_cast<std::size_t>(index)]);
    _load_skinproperties(skin);
    const auto& summary = m_skin_summarys[skin];
    const auto& type = std::get<0>(summary);
    const auto& author = std::get<2>(summary);
    const auto& description = std::get<3>(summary);
    m_tx_skin = "種別: " + type + "\n場所: Data/Skin/" + skin + "\n作者: " + author + "\n" +
                std::string(45, '-') + "\n" + ::cw::util::txtwrap(description,1,30,"",true,nullptr);
    if (arg_init) return;
    if (arg_applied) {
        if (m_parent) m_parent->applied();
    }
}

std::shared_ptr<::cw::setting::LocalSetting> SkinPanel::_get_localsettings()
{
    // Python と同様、Engine へまだ適用していない編集も含め、現在のコントロール
    // 値をコピーする。 Setting::m_local だけを読むと、その編集が失われる。
    if (m_parent) {
        if (const auto panel = detail_panel(m_parent->m_panel)) {
            auto local = std::make_shared<::cw::setting::LocalSetting>();
            local->init();
            if (panel->m_pane_draw) panel->m_pane_draw->apply_localsettings(local);
            if (panel->m_pane_font) panel->m_pane_font->apply_localsettings(local);
            return local;
        }
    }
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_local)
        return ::cw::g_cwpy->m_setting->m_local->copy();
    auto local = std::make_shared<::cw::setting::LocalSetting>();
    local->init();
    return local;
}

void SkinPanel::OnInstallSkin(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m_parent) m_parent->request_document(SettingsDocumentAction::SkinArchive);
}

bool SkinPanel::install_skin_files(const std::vector<String>& selected)
{
    if (selected.empty()) return false;
    std::vector<String> paths;
    paths.reserve(selected.size());
    for (const auto& path : selected) {
        const auto validated = SkinPanel_OnInstallSkin_validate_path(path);
        if (!validated.empty()) paths.push_back(validated);
    }
    if (paths.empty()) return false;
    {
        const auto installed = ::cw::dialog::skininstall::install_skin(std::move(paths), {}, false);
        if (!installed.empty()) {
            const auto& first = installed.front();
            const auto skindir = std::get<0>(first);
            const auto skintype = std::get<3>(first);
            const auto current_type = ::cw::g_cwpy && ::cw::g_cwpy->m_setting
                ? ::cw::g_cwpy->m_setting->m_skintype : String{};
            if (skintype != current_type && !value_bool(m_cb_show_allskin, false))
                m_cb_show_allskin = true;
            update_skins(skindir, true);
            reset_scenario_folder_editor(*this);
            return true;
        }
    }
    return false;
}

void SkinPanel::OnConvertSkin(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m_edit_dialog || m_conversion_dialog) return;
    m_conversion_dialog = std::make_shared<::cw::dialog::skin::SkinConversionDialog>();
    ReleaseSkinDialog release{m_conversion_dialog};
    ::cw::lib::Callback local_callback = [this](const ::cw::lib::Arguments&, const ::cw::lib::KeywordArguments&) -> Value {
        return std::static_pointer_cast<::cw::lib::Object>(_get_localsettings());
    };
    auto parent = std::make_shared<::cw::lib::Window>();
    parent->native_handle = m_parent ? m_parent->m_native_handle : 0;
    m_conversion_dialog->initialize(parent, {}, true, local_callback);
    // Python と同様、モーダル結果を確認してから子を破棄する。 閉じた
    // 子の単一起動予約を残したり、OnClose から親を更新したりしない。
    const bool shown = ::cw::dialog::show_common_modal_presentation(m_conversion_dialog);
    if (!m_conversion_dialog->m_closed) m_conversion_dialog->OnCancel(nullptr);
    if (shown && m_conversion_dialog->m_successful && !m_conversion_dialog->m_cancelled) {
        const auto skin = m_conversion_dialog->m_skindirname;
        const auto skintype = m_conversion_dialog->m_conv
            ? m_conversion_dialog->m_conv->m_skintype : String{};
        const auto current_type = ::cw::g_cwpy && ::cw::g_cwpy->m_setting
            ? ::cw::g_cwpy->m_setting->m_skintype : String{};
        if (skintype != current_type && !value_bool(m_cb_show_allskin, false)) {
            m_cb_show_allskin = true;
        }
        update_skins(skin, true);
        reset_scenario_folder_editor(*this);
    }
}

void SkinPanel::OnEditSkin(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m_edit_dialog || m_conversion_dialog) return;
    const Integer index = value_integer(m_ch_skin, -1);
    if (index < 0 || static_cast<std::size_t>(index) >= m_skindirs.size()) return;
    const auto skin = value_string(m_skindirs[static_cast<std::size_t>(index)]);
    _load_skinproperties(skin);
    const auto summary = m_skin_summarys[skin];
    m_edit_dialog = std::make_shared<::cw::dialog::skin::SkinEditDialog>();
    ReleaseSkinDialog release{m_edit_dialog};
    ::cw::lib::Callback local_callback = [this](const ::cw::lib::Arguments&, const ::cw::lib::KeywordArguments&) -> Value {
        return std::static_pointer_cast<::cw::lib::Object>(_get_localsettings());
    };
    auto parent = std::make_shared<::cw::lib::Window>();
    parent->native_handle = m_parent ? m_parent->m_native_handle : 0;
    m_edit_dialog->initialize(parent, skin, summary, std::move(local_callback));
    const bool shown = ::cw::dialog::show_common_modal_presentation(m_edit_dialog);
    if (!m_edit_dialog->m_closed) m_edit_dialog->OnCancel(nullptr);
    if (shown && m_edit_dialog->m_saved && !m_edit_dialog->m_cancelled) {
        m_skin_summarys[skin] = m_edit_dialog->m_skinsummary_typed;
        update_skins(skin, false);
        reset_scenario_folder_editor(*this);
    }
    // Python と同様、スキン編集をキャンセルしてもフォントのプレビュー編集は維持する。
    if (shown && m_parent) {
        if (const auto panel = detail_panel(m_parent->m_panel); panel && panel->m_pane_font &&
            m_edit_dialog->m_pane_font) {
            panel->m_pane_font->m_tx_example = m_edit_dialog->m_pane_font->m_tx_example;
            panel->m_pane_font->m_sc_example = m_edit_dialog->m_pane_font->m_sc_example;
            panel->m_pane_font->update_example();
        }
    }
}

void SkinPanel::OnCopySkin(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    const auto index = value_integer(m_ch_skin, -1);
    if (index < 0 || static_cast<std::size_t>(index) >= m_skindirs.size()) return;
    const auto skin = value_string(m_skindirs[static_cast<std::size_t>(index)]);
    const auto name = static_cast<std::size_t>(index)<m_skins.size()?value_string(m_skins[static_cast<std::size_t>(index)]):skin;
    if (!settings_confirm("スキン「" + name + "(" + skin + ")」をコピーしますか？")) return;
    const auto src = ::cw::lib::encoding::path_to_utf8(std::filesystem::u8path("Data/Skin") / std::filesystem::u8path(skin));
    {
        const auto dst = ::cw::util::dupcheck_plus(src, false, true);
        ::cw::util::copytree_overwrite(src, dst, 0);
        // Python はここで shutil.copytree を使うが、copytree_overwrite
        // は意図的にディレクトリーのメタデータを変更しない。 この呼び出し側だけで、リンク
        // をたどったディレクトリーも含む子のコピー後に、ディレクトリーの更新時刻を復元する。
        const auto src_path = std::filesystem::u8path(src);
        const auto dst_path = std::filesystem::u8path(dst);
        for (const auto& entry : std::filesystem::recursive_directory_iterator(
                 src_path, std::filesystem::directory_options::follow_directory_symlink)) {
            if (entry.is_directory())
                std::filesystem::last_write_time(dst_path / entry.path().lexically_relative(src_path),
                                                entry.last_write_time());
        }
        std::filesystem::last_write_time(dst_path, std::filesystem::last_write_time(src_path));
        const auto fpath = ::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(dst) / "Skin.xml");
        const auto data = ::cw::data::xml2etree(fpath, "", std::nullopt, std::nullopt, false);
        if (data) {
            data->edit("Property/Name", name + " - コピー", std::nullopt);
            data->write_file(fpath);
        }
        update_skins(::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(dst).filename()), true);
        if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("harvest", false, 100, 1, 0, 0, false);
    }}

void SkinPanel::OnDeleteSkin(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    const auto index = value_integer(m_ch_skin, -1);
    if (index < 0 || static_cast<std::size_t>(index) >= m_skindirs.size()) return;
    const auto skin = value_string(m_skindirs[static_cast<std::size_t>(index)]);
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_skindirname == skin) return;
    const auto name = static_cast<std::size_t>(index)<m_skins.size()?value_string(m_skins[static_cast<std::size_t>(index)]):skin;
    if (!settings_confirm("スキンを削除すると元に戻すことはできません。\n「" + name + "(" + skin + ")」を削除しますか？")) return;
    const auto root = std::filesystem::weakly_canonical(std::filesystem::u8path("Data/Skin"));
    const auto target = std::filesystem::weakly_canonical(root / std::filesystem::u8path(skin));
    // 削除できるのは Data/Skin の直接の子だけとする。 Python の選択
    // 一覧に合わせ、不正な Skin.xml の名前でその外へ出られないようにする。
    if (target.parent_path() != root || target == root) return;
    {
        ::cw::util::remove(::cw::lib::encoding::path_to_utf8(target), false);
        update_skins(::cw::g_cwpy && ::cw::g_cwpy->m_setting
                         ? ::cw::g_cwpy->m_setting->m_skindirname : String{}, true);
        reset_scenario_folder_editor(*this);
        if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("dump", false, 100, 1, 0, 0, false);
    }}

void SkinPanel::OnShowAllSkin(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    const Integer index = value_integer(m_ch_skin, -1);
    const String selected = index >= 0 && static_cast<std::size_t>(index) < m_skindirs.size()
        ? value_string(m_skindirs[static_cast<std::size_t>(index)]) : String{};
    update_skins(selected, false);
}

void SkinPanel::_do_layout()
{
    // スキン選択・説明・操作ボタンは、一般タブの中央の配置処理で並べる。
}

bool SkinPanel::apply_skin(bool arg_forceupdate)
{
    const Integer index = value_integer(m_ch_skin, -1);
    if (index < 0 || static_cast<std::size_t>(index) >= m_skindirs.size()) return false;
    const String skinname = value_string(m_skindirs[static_cast<std::size_t>(index)]);
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_setting) return false;
    auto& setting = *::cw::g_cwpy->m_setting;
    const bool changed = setting.m_skindirname != skinname;
    if (!arg_forceupdate && !changed) return false;
    _load_skinproperties(skinname);
    // update_skin が設定・種別・名前の更新とリソースキャッシュ
    // の再構築を管理する。 Python と同様、cwpy.exec_func
    // に相当する経路でゲーム側の変更を予約し、 UI スレッドが読み
    // 取り中のリソースをネイティブ設定コールバックから置き換えない。
    const auto commit = g_settings_apply_commit;
    ::cw::g_cwpy->exec_func([skinname, changed, commit](const auto&, const auto&) {
        if (commit && !commit->load(std::memory_order_acquire)) return Value{};
        if (::cw::g_cwpy)
            // Python の実際の exec_func(cwpy.update_skin, skinname,
            // changed) の呼び出しに合わせる。 二つ目の位置引数は changearea
            // であり、restartop と switch_skin は既定の True/False
            // を維持する。 別途定義されているラッパーが予約されるわけではない。
            ::cw::g_cwpy->update_skin(skinname, changed, true, std::nullopt, false, false);
        return Value{};
    });
    return true;
}

void SkinPanel::copy_values(std::shared_ptr<::cw::dialog::settings::SkinPanel> arg_skin)
{
    if (!arg_skin) return;
    const Integer source = value_integer(arg_skin->m_ch_skin, -1);
    if (source < 0 || static_cast<std::size_t>(source) >= arg_skin->m_skindirs.size()) return;
    const String name = value_string(arg_skin->m_skindirs[static_cast<std::size_t>(source)]);
    for (std::size_t i = 0; i < m_skindirs.size(); ++i) {
        if (value_string(m_skindirs[i]) == name) {
            m_ch_skin = static_cast<Integer>(i);
            _choice_skin(true, false);
            return;
        }
    }
}

void SpeedPanel::initialize(std::shared_ptr<::cw::lib::Panel> arg_parent, bool arg_battlespeed)
{
    (void)arg_parent;
    m_battlespeed = arg_battlespeed;
    m_box_tran = "背景の切り替え方式(速い⇔遅い)";
    m_box_deal = "カード描画速度(速い⇔遅い)";
    m_box_msgs = "メッセージ表示速度(速い⇔遅い)";
    m_transitions = {"None", "Blinds", "PixelDissolve", "Fade"};
    m_choices_tran = {"アニメーションなし", "短冊(スレッド)式", "ドット置換(シェーブ)式", "色置換(フェード)式"};
    m_ch_tran = Integer{0};
    m_sl_tran = Integer{0};
    m_sl_deal = Integer{0};
    m_sl_msgs = Integer{0};
    m__battle_enabled = false;
    if (arg_battlespeed) {
        m_box_deal_battle = "戦闘行動描画速度(速い⇔遅い)";
        m_sl_deal_battle = Integer{0};
        m_cb_use_battlespeed = false;
    }
    _bind();
    _do_layout();
}

// Python の list.index は保存済みの効果名が不
// 明なら例外を出す。 ここでは先頭の選択肢を表示し、設定画面を開
// いて修復できるようにする。編集値へ読み込むだけで保存はしない。
void SpeedPanel::load(std::shared_ptr<::cw::setting::Setting> arg_setting)
{
    if (!arg_setting) return;
    const auto it = std::find(m_transitions.begin(), m_transitions.end(), arg_setting->m_transition);
    m_ch_tran = it == m_transitions.end() ? Integer{0} : static_cast<Integer>(std::distance(m_transitions.begin(), it));
    m_sl_tran = std::clamp<Integer>(arg_setting->m_transitionspeed, 0, 10);
    m_sl_deal = std::clamp<Integer>(arg_setting->m_dealspeed, 0, 10);
    m_sl_msgs = std::clamp<Integer>(arg_setting->m_messagespeed, 0, 10);
    if (value_bool(m_battlespeed)) {
        m_sl_deal_battle = std::clamp<Integer>(arg_setting->m_dealspeed_battle, 0, 10);
        m_cb_use_battlespeed = !arg_setting->m_use_battlespeed;
        m__battle_enabled = arg_setting->m_use_battlespeed;
    }
}

void SpeedPanel::_bind()
{
    // 戦闘速度のチェックは、共通 Presentation から OnUseBattleSpeed に接続する。
}

void SpeedPanel::_do_layout()
{
    // 速度のトラックバー群は、描画タブの中央の配置処理で並べる。
}

void SpeedPanel::OnUseBattleSpeed(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    m__battle_enabled = !value_bool(m_cb_use_battlespeed);
}

bool SpeedPanel::apply_speed(std::shared_ptr<::cw::setting::Setting> arg_setting)
{
    if (!arg_setting) return false;
    const Integer deal = std::clamp<Integer>(value_integer(m_sl_deal), 0, 10);
    const Integer battle = value_bool(m_battlespeed)
        ? std::clamp<Integer>(value_integer(m_sl_deal_battle), 0, 10) : arg_setting->m_dealspeed_battle;
    const bool use_battle = value_bool(m_battlespeed) ? !value_bool(m_cb_use_battlespeed) : arg_setting->m_use_battlespeed;
    arg_setting->set_dealspeed(deal, battle, use_battle);
    const Integer messages = std::clamp<Integer>(value_integer(m_sl_msgs), 0, 10);
    const bool update_message = arg_setting->m_messagespeed != messages;
    arg_setting->m_messagespeed = messages;
    if (!m_transitions.empty()) {
        const Integer transition = std::clamp<Integer>(value_integer(m_ch_tran), 0,
                                                        static_cast<Integer>(m_transitions.size()) - 1);
        arg_setting->m_transition = m_transitions[static_cast<std::size_t>(transition)];
    }
    arg_setting->m_transitionspeed = std::clamp<Integer>(value_integer(m_sl_tran), 0, 10);
    return update_message;
}

void SpeedPanel::copy_values(std::shared_ptr<::cw::dialog::settings::SpeedPanel> arg_speed)
{
    if (!arg_speed) return;
    m_ch_tran = value_integer(arg_speed->m_ch_tran);
    m_sl_tran = value_integer(arg_speed->m_sl_tran);
    m_sl_deal = value_integer(arg_speed->m_sl_deal);
    m_sl_msgs = value_integer(arg_speed->m_sl_msgs);
    if (value_bool(m_battlespeed) && value_bool(arg_speed->m_battlespeed)) {
        m_sl_deal_battle = value_integer(arg_speed->m_sl_deal_battle);
        m_cb_use_battlespeed = value_bool(arg_speed->m_cb_use_battlespeed);
        m__battle_enabled = arg_speed->m__battle_enabled;
    }
}

void UISettingPanel::initialize(std::shared_ptr<::cw::lib::Window> arg_parent)
{
    (void)arg_parent;
    m_box_skip_and_wait = "スキップと空白時間";
    m_box_card = "カード";
    m_box_control = "操作";
    m_box_confirm_and_description = "通知と解説";
    m_box_save_and_load = "セーブとロード";
    m_box_dialog = "ダイアログ";
    const auto reset = [](Value& value) { value = false; };
    reset(m_cb_can_skipwait); reset(m_cb_can_skipanimation); reset(m_cb_can_skipwait_with_wheel);
    reset(m_cb_can_forwardmessage_with_wheel); reset(m_cb_wait_usecard);
    reset(m_cb_enlarge_beastcardzoomingratio); reset(m_cb_autoenter_on_sprite);
    reset(m_cb_quickdeal); reset(m_cb_allquickdeal); reset(m_cb_showallselectedcards);
    reset(m_cb_show_cardkind); reset(m_cb_show_premiumicon); reset(m_cb_show_personal_cards);
    reset(m_cb_showbackpackcard); reset(m_cb_showbackpackcardatend); reset(m_cb_revertcardpocket);
    reset(m_cb_showroundautostartbutton); reset(m_cb_showautobuttoninentrydialog);
    reset(m_cb_show_all_status_dialog);
    reset(m_cb_protect_staredcard); reset(m_cb_protect_premiercard);
    reset(m_cb_can_clicksidesofcardcontrol); reset(m_cb_showlogwithwheelup);
    reset(m_cb_show_btndesc); reset(m_cb_statusbarmask); reset(m_cb_blink_statusbutton);
    reset(m_cb_blink_partymoney); reset(m_cb_showsavedmessage); reset(m_cb_cautionbeforesaving);
    reset(m_cb_show_advancedsettings); reset(m_cb_show_addctrlbtn); reset(m_cb_show_experiencebar);
    reset(m_cb_confirmbeforeusingcard); reset(m_cb_noticeimpossibleaction);
    m_ch_show_statustime = static_cast<Integer>(0);
    m_ch_confirm_beforesaving = static_cast<Integer>(0);
    m_ch_confirm_dumpcard = static_cast<Integer>(0);
    m_sc_radius_notdetectmovement = static_cast<Integer>(0);
    _do_layout();
    _bind();
}

void UISettingPanel::load(std::shared_ptr<::cw::setting::Setting> arg_setting)
{
    const auto inn=::cw::g_cwpy && ::cw::g_cwpy->m_ydata ? *::cw::g_cwpy->m_ydata : nullptr;
    m_pc_limits_inn=inn && inn->m_environment ? inn : nullptr;
    const auto limits=::cw::pc_card_limits::read(inn);
    m_cb_pc_card_limits=limits.enabled;
    m_sc_pc_skill_limit=limits.skill; m_sc_pc_personal_limit=limits.personal;
    m_sc_pc_beast_limit=limits.beast;
    if (!arg_setting) return;
    m_cb_can_skipwait = arg_setting->m_can_skipwait;
    m_cb_can_skipanimation = arg_setting->m_can_skipanimation;
    m_cb_can_skipwait_with_wheel = arg_setting->m_can_skipwait_with_wheel;
    m_cb_can_forwardmessage_with_wheel = arg_setting->m_can_forwardmessage_with_wheel;
    m_cb_wait_usecard = arg_setting->m_wait_usecard;
    m_cb_enlarge_beastcardzoomingratio = arg_setting->m_enlarge_beastcardzoomingratio;
    m_cb_autoenter_on_sprite = arg_setting->m_autoenter_on_sprite;
    m_cb_quickdeal = arg_setting->m_quickdeal;
    m_cb_allquickdeal = arg_setting->m_all_quickdeal;
    m_cb_showallselectedcards = arg_setting->m_show_allselectedcards;
    m_ch_show_statustime = status_time_selection(arg_setting->m_show_statustime);
    m_cb_show_cardkind = arg_setting->m_show_cardkind;
    m_cb_show_premiumicon = arg_setting->m_show_premiumicon;
    m_cb_show_personal_cards = arg_setting->m_show_personal_cards;
    m_cb_showbackpackcard = arg_setting->m_show_backpackcard;
    m_cb_showbackpackcardatend = arg_setting->m_show_backpackcardatend;
    m_cb_can_clicksidesofcardcontrol = arg_setting->m_can_clicksidesofcardcontrol;
    m_cb_revertcardpocket = arg_setting->m_revert_cardpocket;
    m_cb_showlogwithwheelup = arg_setting->m_wheelup_operation == ::cw::setting::g_WHEEL_SHOWLOG;
    m_cb_showroundautostartbutton = arg_setting->m_show_roundautostartbutton;
    m_cb_show_all_status_dialog = arg_setting->m_show_all_status_dialog;
    {
        const auto shortcuts = ::cw::setting::parse_all_status_shortcuts(arg_setting->m_all_status_shortcuts);
        for (std::size_t slot = 0; slot < shortcuts.size(); ++slot)
            m_ch_all_shortcuts[slot] = static_cast<Integer>(shortcuts[slot]);
        m_all_custom_status = arg_setting->m_all_custom_status;
    }
    m_cb_showautobuttoninentrydialog = arg_setting->m_show_autobuttoninentrydialog;
    m_cb_protect_staredcard = arg_setting->m_protect_staredcard;
    m_cb_protect_premiercard = arg_setting->m_protect_premiercard;
    const auto confirm_dump = arg_setting->m_confirm_dumpcard;
    m_ch_confirm_dumpcard = confirm_dump == ::cw::setting::g_CONFIRM_DUMPCARD_SENDTO
        ? static_cast<Integer>(1) : (confirm_dump == ::cw::setting::g_CONFIRM_DUMPCARD_NO ? 2 : 0);
    m_sc_radius_notdetectmovement = std::clamp<Integer>(arg_setting->m_radius_notdetectmovement,0,50);
    m_cb_show_btndesc = arg_setting->m_show_btndesc;
    m_cb_statusbarmask = arg_setting->m_statusbarmask;
    m_cb_blink_statusbutton = arg_setting->m_blink_statusbutton;
    m_cb_blink_partymoney = arg_setting->m_blink_partymoney;
    const auto confirm_save = arg_setting->m_confirm_beforesaving;
    m_ch_confirm_beforesaving = confirm_save == ::cw::setting::g_CONFIRM_BEFORESAVING_BASE
        ? static_cast<Integer>(1) : (confirm_save == ::cw::setting::g_CONFIRM_BEFORESAVING_NO ? 2 : 0);
    m_cb_showsavedmessage = arg_setting->m_show_savedmessage;
    m_cb_skip_pc_click_motion = arg_setting->m_skip_pc_click_motion;
    m_cb_cautionbeforesaving = arg_setting->m_caution_beforesaving;
    m_cb_show_advancedsettings = arg_setting->m_show_advancedsettings;
    m_cb_show_addctrlbtn = arg_setting->m_show_addctrlbtn;
    m_cb_show_experiencebar = arg_setting->m_show_experiencebar;
    m_cb_confirmbeforeusingcard = arg_setting->m_confirm_beforeusingcard;
    m_cb_noticeimpossibleaction = arg_setting->m_noticeimpossibleaction;
}

void UISettingPanel::init_values(std::shared_ptr<::cw::setting::Setting> arg_setting)
{
    m_cb_pc_card_limits=false;
    m_sc_pc_skill_limit=static_cast<Integer>(50); m_sc_pc_personal_limit=static_cast<Integer>(50);
    m_sc_pc_beast_limit=static_cast<Integer>(50);
    if (!arg_setting) return;
    const auto boolean = [](const Value& value, bool fallback) { return value_bool(value, fallback); };
    m_cb_can_skipwait = boolean(arg_setting->m_can_skipwait_init, arg_setting->m_can_skipwait);
    m_cb_can_skipanimation = boolean(arg_setting->m_can_skipanimation_init, arg_setting->m_can_skipanimation);
    m_cb_can_skipwait_with_wheel = boolean(arg_setting->m_can_skipwait_with_wheel_init, arg_setting->m_can_skipwait_with_wheel);
    m_cb_can_forwardmessage_with_wheel = boolean(arg_setting->m_can_forwardmessage_with_wheel_init, arg_setting->m_can_forwardmessage_with_wheel);
    m_cb_wait_usecard = boolean(arg_setting->m_wait_usecard_init, arg_setting->m_wait_usecard);
    m_cb_enlarge_beastcardzoomingratio = boolean(arg_setting->m_enlarge_beastcardzoomingratio_init, arg_setting->m_enlarge_beastcardzoomingratio);
    m_cb_autoenter_on_sprite = boolean(arg_setting->m_autoenter_on_sprite_init, arg_setting->m_autoenter_on_sprite);
    m_cb_quickdeal = boolean(arg_setting->m_quickdeal_init, arg_setting->m_quickdeal);
    m_cb_allquickdeal = boolean(arg_setting->m_all_quickdeal_init, arg_setting->m_all_quickdeal);
    m_cb_showallselectedcards = boolean(arg_setting->m_show_allselectedcards_init, arg_setting->m_show_allselectedcards);
    m_ch_show_statustime = status_time_selection(arg_setting->m_show_statustime_init);
    m_cb_show_cardkind = boolean(arg_setting->m_show_cardkind_init, arg_setting->m_show_cardkind);
    m_cb_show_premiumicon = boolean(arg_setting->m_show_premiumicon_init, arg_setting->m_show_premiumicon);
    m_cb_show_personal_cards = boolean(arg_setting->m_show_personal_cards_init, arg_setting->m_show_personal_cards);
    m_cb_showbackpackcard = boolean(arg_setting->m_show_backpackcard_init, arg_setting->m_show_backpackcard);
    m_cb_showbackpackcardatend = boolean(arg_setting->m_show_backpackcardatend_init, arg_setting->m_show_backpackcardatend);
    m_cb_can_clicksidesofcardcontrol = boolean(arg_setting->m_can_clicksidesofcardcontrol_init, arg_setting->m_can_clicksidesofcardcontrol);
    m_cb_revertcardpocket = boolean(arg_setting->m_revert_cardpocket_init, arg_setting->m_revert_cardpocket);
    m_cb_showlogwithwheelup = value_string(arg_setting->m_wheelup_operation_init, arg_setting->m_wheelup_operation) == ::cw::setting::g_WHEEL_SHOWLOG;
    m_cb_showroundautostartbutton = boolean(arg_setting->m_show_roundautostartbutton_init, arg_setting->m_show_roundautostartbutton);
    m_cb_show_all_status_dialog = boolean(arg_setting->m_show_all_status_dialog_init, arg_setting->m_show_all_status_dialog);
    {
        const auto shortcuts = ::cw::setting::parse_all_status_shortcuts(
            value_string(arg_setting->m_all_status_shortcuts_init, ::cw::setting::kAllStatusShortcutsDefault));
        for (std::size_t slot = 0; slot < shortcuts.size(); ++slot)
            m_ch_all_shortcuts[slot] = static_cast<Integer>(shortcuts[slot]);
        m_all_custom_status = value_string(arg_setting->m_all_custom_status_init, String{});
    }
    m_cb_showautobuttoninentrydialog = boolean(arg_setting->m_show_autobuttoninentrydialog_init, arg_setting->m_show_autobuttoninentrydialog);
    m_cb_protect_staredcard = boolean(arg_setting->m_protect_staredcard_init, arg_setting->m_protect_staredcard);
    m_cb_protect_premiercard = boolean(arg_setting->m_protect_premiercard_init, arg_setting->m_protect_premiercard);
    const auto confirm_dump = value_string(arg_setting->m_confirm_dumpcard_init, arg_setting->m_confirm_dumpcard);
    m_ch_confirm_dumpcard = confirm_dump == ::cw::setting::g_CONFIRM_DUMPCARD_SENDTO
        ? static_cast<Integer>(1) : (confirm_dump == ::cw::setting::g_CONFIRM_DUMPCARD_NO ? 2 : 0);
    m_sc_radius_notdetectmovement = std::clamp<Integer>(value_integer(arg_setting->m_radius_notdetectmovement_init, arg_setting->m_radius_notdetectmovement),0,50);
    m_cb_show_btndesc = boolean(arg_setting->m_show_btndesc_init, arg_setting->m_show_btndesc);
    m_cb_statusbarmask = boolean(arg_setting->m_statusbarmask_init, arg_setting->m_statusbarmask);
    m_cb_blink_statusbutton = boolean(arg_setting->m_blink_statusbutton_init, arg_setting->m_blink_statusbutton);
    m_cb_blink_partymoney = boolean(arg_setting->m_blink_partymoney_init, arg_setting->m_blink_partymoney);
    const auto confirm_save = value_string(arg_setting->m_confirm_beforesaving_init, arg_setting->m_confirm_beforesaving);
    m_ch_confirm_beforesaving = confirm_save == ::cw::setting::g_CONFIRM_BEFORESAVING_BASE
        ? static_cast<Integer>(1) : (confirm_save == ::cw::setting::g_CONFIRM_BEFORESAVING_NO ? 2 : 0);
    m_cb_showsavedmessage = boolean(arg_setting->m_show_savedmessage_init, arg_setting->m_show_savedmessage);
    m_cb_skip_pc_click_motion = boolean(arg_setting->m_skip_pc_click_motion_init, true);
    m_cb_cautionbeforesaving = boolean(arg_setting->m_caution_beforesaving_init, arg_setting->m_caution_beforesaving);
    m_cb_show_advancedsettings = boolean(arg_setting->m_show_advancedsettings_init, arg_setting->m_show_advancedsettings);
    m_cb_show_addctrlbtn = boolean(arg_setting->m_show_addctrlbtn_init, arg_setting->m_show_addctrlbtn);
    m_cb_show_experiencebar = boolean(arg_setting->m_show_experiencebar_init, arg_setting->m_show_experiencebar);
    m_cb_confirmbeforeusingcard = boolean(arg_setting->m_confirm_beforeusingcard_init, arg_setting->m_confirm_beforeusingcard);
    m_cb_noticeimpossibleaction = boolean(arg_setting->m_noticeimpossibleaction_init, arg_setting->m_noticeimpossibleaction);
}

void UISettingPanel::OnQuickDeal(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (!value_bool(m_cb_quickdeal)) m_cb_allquickdeal = false;
}

void UISettingPanel::OnAllQuickDeal(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (value_bool(m_cb_allquickdeal)) m_cb_quickdeal = true;
}

void UISettingPanel::_bind()
{
    // 高速配布のチェックの相互依存は、バックエンドから OnQuickDeal/OnAllQuickDeal に接続済みである。
}

void UISettingPanel::_do_layout()
{
    // UIタブの各check/combo/editは中央layoutで配置される。
}

::cw::lib::Integer g__spin_w_addition{};
::cw::lib::Value g_versioninfo{};

::cw::lib::Integer _settings_width()
{
    return 250;
}

void create_versioninfo(::cw::lib::Value arg_parent)
{
    String text = settings_presentation_version_text();
    g_versioninfo = text;
    auto window = make_window(text);
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&arg_parent.storage()); object && *object) {
        if (std::dynamic_pointer_cast<::cw::dialog::settings::SettingsPanel>(*object))
            ::cw::dialog::settings::SettingsPanel::s_versioninfo = window;
        else if (std::dynamic_pointer_cast<::cw::dialog::settings::SimpleSettingsPanel>(*object))
            ::cw::dialog::settings::SimpleSettingsPanel::s_versioninfo = window;
    }
}

// Python はこのヘルパー自体を予約する。ネイティブの
// 設定適用は呼び出し前にゲーム側ディスパッチャーへ入ってい
// るため、 別の遅延レベルアップ確認を予約せず、その場で
// 確認する。直接呼び出した場合も同期的に確認を完了する。
void apply_levelupparams(bool arg_can_levelup)
{
    apply_levelupparams_check_levelup(arg_can_levelup);
}

void apply_levelupparams_check_levelup(bool arg_can_levelup_old)
{
    if (!::cw::g_cwpy || ::cw::g_cwpy->is_playingscenario() || !::cw::g_cwpy->m_setting) return;
    const bool can_levelup = !(::cw::g_cwpy->is_debugmode() &&
                               ::cw::g_cwpy->m_setting->m_no_levelup_in_debugmode);
    if (!arg_can_levelup_old && can_levelup) {
        // Python の apply_levelupparams と同様、設定によって保留中の正のレベルアップを有効にする。
        for (const auto& pcard : ::cw::g_cwpy->m_pcards)
            if (pcard && pcard->check_level() > 0) pcard->adjust_level(false);
    }
}

void SimpleSettingsPanel_OnOk_func(std::shared_ptr<::cw::dialog::settings::SimpleSettingsPanel> arg_self)
{
    if (arg_self) arg_self->OnOk(nullptr);
}

void SimpleSettingsPanel_OnOk_func_func(std::shared_ptr<::cw::dialog::settings::SimpleSettingsPanel> arg_self)
{
    if (arg_self && arg_self->m_parent) arg_self->m_parent->close_after_apply();
}

void SettingsPanel_OnOk_func(std::shared_ptr<::cw::dialog::settings::SettingsPanel> arg_self)
{
    if (arg_self) arg_self->OnOk(nullptr);
}

void SettingsPanel_OnOk_func_func(std::shared_ptr<::cw::dialog::settings::SettingsPanel> arg_self)
{
    if (arg_self && arg_self->m_parent) arg_self->m_parent->close_after_apply();
}

void SettingsPanel_apply_func(::cw::lib::Integer arg_backlogmax)
{
    if (::cw::g_cwpy) ::cw::g_cwpy->set_backlogmax(arg_backlogmax);
}

void SettingsPanel_apply_func_cursor()
{
    if (::cw::g_cwpy) ::cw::g_cwpy->change_cursor(::cw::g_cwpy->m_cursor, true);
}

void SettingsPanel_apply_func_sfonts()
{
    if (const auto backend = ::cw::dialog::settings::get_host_services()) backend->restart_soundfonts();
}

void SettingsPanel_apply_func_sfonts(bool arg_had_soundfonts)
{
    if (const auto backend = ::cw::dialog::settings::get_host_services())
        backend->restart_soundfonts(arg_had_soundfonts);
}

void SettingsPanel_apply_func1()
{
    if (::cw::g_cwpy) ::cw::g_cwpy->update_fullscreenbackground();
}

void SettingsPanel_apply_func_advlog()
{
    if (::cw::g_cwpy && ::cw::g_cwpy->m_advlog && ::cw::g_cwpy->m_setting)
        ::cw::g_cwpy->m_advlog->enable(::cw::g_cwpy->m_setting->m_write_playlog);
}

void SettingsPanel_apply_func_personal()
{
    if (!::cw::g_cwpy) return;
    if (::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata && (*::cw::g_cwpy->m_ydata)->m_party)
        (*::cw::g_cwpy->m_ydata)->m_party.value()->sort_backpack(std::nullopt, std::nullopt);
    if (::cw::g_cwpy->m_selectedheader &&
        (::cw::g_cwpy->m_areaid == value_integer(std::get<0>(::cw::g_AREAS_TRADE)) ||
         ::cw::g_cwpy->m_areaid == value_integer(std::get<1>(::cw::g_AREAS_TRADE)) ||
         ::cw::g_cwpy->m_areaid == value_integer(std::get<2>(::cw::g_AREAS_TRADE)))
        && *::cw::g_cwpy->m_selectedheader)
        ::cw::g_cwpy->show_numberofcards((*::cw::g_cwpy->m_selectedheader)->m_type);
}

void SettingsPanel_apply_func_autostart()
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->is_playingscenario()) return;
    if (const auto data = object_value<::cw::data::ScenarioData>(::cw::g_cwpy->m_sdata))
        data->m_autostart_round = false;
}

void SettingsPanel_apply_func_redrawcard()
{
    if (!::cw::g_cwpy) return;
    if (::cw::g_cwpy->m_selectedheader) {
        ::cw::g_cwpy->remove_pricesprites();
        ::cw::data::redraw_cards(::cw::g_cwpy->is_debugmode(), {}, false);
        ::cw::g_cwpy->set_testaptitude(::cw::g_cwpy->m_selectedheader);
    }
}

void SettingsPanel_apply_func_updatebg()
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_background) return;
    if (const auto data = object_value<::cw::data::ScenarioData>(::cw::g_cwpy->m_sdata))
        data->m_resource_cache.clear();
    ::cw::g_cwpy->m_background->reload(false, {"Default", Value("Default")}, true, {}, {}, {}, false, true);
}

void SettingsPanel_apply_func_updateimg(bool arg_menu_cards)
{
    if (const auto backend = ::cw::dialog::settings::get_host_services()) backend->refresh_cards(arg_menu_cards);
}

void SettingsPanel_apply_func_statusbar()
{
    if (const auto backend = ::cw::dialog::settings::get_host_services()) backend->refresh_statusbar();
}

::cw::lib::String SkinPanel_OnInstallSkin_validate_path(::cw::lib::String arg_path)
{
    if (arg_path.empty()) return {};
    std::filesystem::path path = std::filesystem::u8path(arg_path);
    if (path.filename() == "Skin.xml") path = path.parent_path();
    std::error_code ec1, ec2;
    const auto parent = std::filesystem::weakly_canonical(path.parent_path(), ec1);
    const auto skinroot = std::filesystem::weakly_canonical(std::filesystem::u8path("Data/Skin"), ec2);
    if (!ec1 && !ec2 && parent == skinroot) return {};
    return path.generic_string();
}

::cw::lib::Value SkinPanel_OnInstallSkin_lambda_line_1056(::cw::lib::Value arg_path)
{
    const auto path = value_string(arg_path);
    return path.empty() ? Value{} : Value(path);
}

void SkinPanel_OnInstallSkin_func(std::shared_ptr<::cw::dialog::settings::SkinPanel> arg_self)
{
    if (arg_self) arg_self->OnInstallSkin({});
}

void SkinPanel_OnInstallSkin_func_func(std::shared_ptr<::cw::dialog::settings::SkinPanel> arg_self)
{
    if (!arg_self) return;
    const Integer index = value_integer(arg_self->m_ch_skin, -1);
    const String selected = index >= 0 && static_cast<std::size_t>(index) < arg_self->m_skindirs.size()
        ? value_string(arg_self->m_skindirs[static_cast<std::size_t>(index)]) : String{};
    arg_self->update_skins(selected, true);
}

void SkinPanel_apply_skin_update_skin(::cw::lib::String arg_skindirname, bool arg_restartop)
{
    if (::cw::g_cwpy)
        ::cw::g_cwpy->update_skin(std::move(arg_skindirname), false, arg_restartop, std::nullopt, true, false);
}

void ExpandPanel_apply_expand_func(::cw::lib::String arg_value_s)
{
    if (!::cw::g_cwpy) return;
    ::cw::g_cwpy->m_expand_mode = arg_value_s;
    ::cw::g_cwpy->set_expanded(::cw::g_cwpy->is_expanded(), std::move(arg_value_s), true, std::nullopt);
}

void ScenarioSettingPanel__do_layout_add_application(std::shared_ptr<::cw::lib::Control> arg_ctrl, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos, ::cw::lib::Integer arg_flag)
{
    if (arg_ctrl) {
        arg_ctrl->bounds.x = std::get<0>(arg_pos);
        arg_ctrl->bounds.y = std::get<1>(arg_pos);
        arg_ctrl->visible = true;
    }
    // wxSizerのflag/伸縮計算はUIバックエンド側で行う。
    (void)arg_flag;
}

::cw::lib::Value FontSettingPanel___init___lambda_line_3129()
{
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_local)
        return std::static_pointer_cast<::cw::lib::Object>(::cw::g_cwpy->m_setting->m_local);
    return {};
}

std::vector<std::shared_ptr<::cw::lib::Window>> FontSettingPanel___init___create_grid(std::shared_ptr<::cw::lib::Window> arg_grid, std::vector<::cw::lib::String> arg_seq, ::cw::lib::Callback arg_faces, ::cw::lib::Integer arg_cols, ::cw::lib::Integer arg_rowlblsize)
{
    if (arg_grid) {
        arg_grid->bounds.width = arg_rowlblsize;
        arg_grid->bounds.height = static_cast<Integer>(arg_seq.size());
        arg_grid->visible = true;
    }
    std::vector<std::shared_ptr<::cw::lib::Window>> editors;
    editors.reserve(arg_seq.size());
    for (std::size_t i = 0; i < arg_seq.size(); ++i) {
        auto editor = make_window(arg_seq[i]);
        editor->id = static_cast<::cw::lib::Integer>(i);
        if (arg_faces) {
            const Value choices = arg_faces(::cw::lib::Arguments{Value(arg_seq[i])}, {});
            editor->title = choices.as_string();
        }
        editor->bounds.height = arg_cols;
        editors.push_back(std::move(editor));
    }
    return editors;
}

bool FontSettingPanel___init___has_defaultfont(::cw::lib::String arg_name)
{
    static const std::map<String, String> defaults{
        {"gothic", "IPAゴシック"}, {"pgothic", "IPA Pゴシック"},
        {"uigothic", "IPA UIゴシック"}, {"mincho", "IPA明朝"}, {"pmincho", "IPA P明朝"}};
    const auto it = defaults.find(arg_name);
    if (it == defaults.end()) return false;
    // Resource の表は、利用可能ならプラットフォームのフォント
    // ローダーが設定する。 空なら設定モデルが実行環境から切り離されて
    // いるものとし、Python と同じ同梱フォントの代替を維持する。
    if (::cw::setting::Resource::s_facenames_init.empty()) return true;
    return ::cw::setting::Resource::s_facenames_init.contains(it->second);
}

std::vector<::cw::lib::String> FontSettingPanel___init___faces_from_basefont(::cw::lib::String arg_name)
{
    std::vector<String> result;
    if (FontSettingPanel___init___has_defaultfont(arg_name)) result.push_back("[標準フォント]");
    result.push_back("IPAゴシック");
    result.push_back("IPA Pゴシック");
    result.push_back("IPA UIゴシック");
    result.push_back("IPA明朝");
    result.push_back("IPA P明朝");
    for (const auto& [name, _] : ::cw::setting::Resource::s_facenames_init) {
        if (!name.empty() && name.front() != '@' &&
            std::find(result.begin(), result.end(), name) == result.end()) result.push_back(name);
    }
    if (const auto backend = ::cw::dialog::settings::get_host_services()) {
        for (const auto& name : backend->enumerate_font_faces()) {
            if (!name.empty() && name.front() != '@' &&
                std::find(result.begin(), result.end(), name) == result.end()) result.push_back(name);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<::cw::lib::String> FontSettingPanel___init___get_choices(::cw::lib::String arg_ftype)
{
    static const std::vector<String> bases{"[等幅ゴシック]", "[可変幅ゴシック]", "[等幅明朝]", "[可変幅明朝]", "[UI用]"};
    std::vector<String> faces{"[標準フォント]", "IPAゴシック", "IPA Pゴシック", "IPA UIゴシック", "IPA明朝", "IPA P明朝"};
    for (const auto& [name, _] : ::cw::setting::Resource::s_facenames_init) {
        if (!name.empty() && name.front() != '@' &&
            std::find(faces.begin(), faces.end(), name) == faces.end()) faces.push_back(name);
    }
    if (const auto backend = ::cw::dialog::settings::get_host_services()) {
        for (const auto& name : backend->enumerate_font_faces()) {
            if (!name.empty() && name.front() != '@' &&
                std::find(faces.begin(), faces.end(), name) == faces.end()) faces.push_back(name);
        }
    }
    std::vector<String> result;
    if (std::set<String>{"fw_symbol", "fw_number", "fw_latin", "hiragana", "katakana", "hw_katakana",
                         "greek_and_cyrillic", "jis_kanji_1", "jis_kanji_2", "etc_kanji", "symbol", "number", "latin"}.contains(arg_ftype))
        result.push_back("[指定しない]");
    result.insert(result.end(), bases.begin(), bases.end());
    result.insert(result.end(), faces.begin(), faces.end());
    return result;
}

void FontSettingPanel_load_create_grid(std::shared_ptr<::cw::lib::Window> arg_grid, std::vector<::cw::lib::String> arg_seq)
{
    if (arg_grid) {
        arg_grid->id = static_cast<::cw::lib::Integer>(arg_seq.size());
        arg_grid->bounds.height = static_cast<::cw::lib::Integer>(arg_seq.size());
    }
}

void FontSettingPanel_OnSelectFontBase_func(std::shared_ptr<::cw::dialog::settings::FontSettingPanel> arg_self)
{
    if (arg_self) arg_self->_select_base(arg_self->m__base_cursor);
}

void FontSettingPanel_OnSelectFontType_func(std::shared_ptr<::cw::dialog::settings::FontSettingPanel> arg_self)
{
    if (arg_self) arg_self->_select_type(arg_self->m__type_cursor);
}

void main()
{
    // Python版もモジュール実行時の副作用を持たない。
}

}
