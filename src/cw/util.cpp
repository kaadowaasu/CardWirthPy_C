// cw/util.pyの移植可能な処理、パス処理、画像処理を移植したもの。ネイティ
// ブ呼び出しは小さなプラットフォーム別アダプタへ隔離し、データ層の移植性を保つ。
#include "cw/lib/exception_origin.hpp"
#include "cw/util.hpp"
#include <unordered_set>
#include "cw/dialog/dialog.hpp"
#include "cw/dialog/message.hpp"
#include "cw/dialog/presentation.hpp"
#include "cw/lib/python_printable.hpp"
#include "cw/lib/python_unicode_width.hpp"
#include "cw/lib/python_text.hpp"
#include "cw/lib/python_percent_string.hpp"
#include "cw/lib/python_integer.hpp"
#include "cw/lib/ui_surface.hpp"
#include "cw/dialog/richtext_scroll.hpp"
#include "cw/lib/runtime_access.hpp"
#include "cw/lib/system_clipboard.hpp"
#include "app/debug_control_server.hpp"
// Webを含む全プラットフォームで、共通のTinySoundFont
// プレーヤーを使ってMIDIを合成する。
#include "app/explorer/midi_player.hpp"
#define CW_UTIL_HAS_PORTABLE_MIDI 1
#include "cw/cw.hpp"
#include "cw/bassplayer.hpp"
#include "cw/features.hpp"
#include "cw/image.hpp"
#include "cw/setting.hpp"
#include "cw/thread.hpp"
#include "cw/binary/image.hpp"
#include "cw/binary/util.hpp"
#include "cw/data.hpp"
#include "cw/event.hpp"
#include "cw/frame.hpp"
#include "cw/header.hpp"
#include "cw/character.hpp"
#include "cw/sprite/card.hpp"
#include "cw/lib/archive.hpp"
#include "cw/lib/encoding.hpp"
#include "cw/lib/imagecodec.hpp"
#include "cw/lib/managed_userdata.hpp"
#include "cw/lib/regex_grapheme.hpp"
#include "cw/lib/python_re.hpp"
#include "3rdparty/srell/srell.hpp"
#include "cw/lib/simd_image.hpp"
#include "cw/lib/stdlib.hpp"
#include "cw/animatedimage.hpp"
#include "cw/imageretouch.hpp"
#include "platform/storage/storage_mount.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <thread>
#include <chrono>
#include <ctime>
#include <iterator>
#include <regex>
#include <sstream>
#include <span>
#include <stdexcept>
#include <system_error>
#include <utility>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <objbase.h>
#include <direct.h>
#include <io.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

#if __has_include(<SDL3/SDL.h>)
#include <SDL3/SDL.h>
#define CW_UTIL_HAS_SDL 1
#else
#define CW_UTIL_HAS_SDL 0
#endif
#if CW_UTIL_HAS_SDL && __has_include(<SDL3_mixer/SDL_mixer.h>)
#include <SDL3_mixer/SDL_mixer.h>
#define CW_UTIL_HAS_SDL_MIXER 1
#else
#define CW_UTIL_HAS_SDL_MIXER 0
#endif

namespace cw::dialog {
::cw::lib::Integer richtext_wheel_scroll_delta(::cw::lib::Integer rotation)
{
    ::cw::lib::Integer lines=4;
#if defined(_WIN32)
    // ネイティブ固有の取得が必要なのは、Windowsのユーザー設定にあるホイール
    // 量。乗算、winsの倍率、符号の扱いは全プラットフォームでPyと共通にする。
    UINT system_lines{};
    if (!SystemParametersInfoW(SPI_GETWHEELSCROLLLINES,0,&system_lines,0))
        throw std::runtime_error("SystemParametersInfo(SPI_GETWHEELSCROLLLINES) failed");
    lines=static_cast<::cw::lib::Integer>(system_lines);
#endif
    const auto amount=::cw::lib::ui::win_i(lines*4);
    return rotation>0 ? -amount : amount;
}
}

namespace cw::util {

// util.hppで宣言し、下の他のモジュール全体変数とともに定義する
// 。コンテキストマネージャの実装では、メソッドより前にこの宣言が必要。
extern std::vector<std::tuple<::cw::lib::Value, ::cw::lib::String>> g__mutex;

namespace {
bool g_sound_suspended{}; // Engineスレッドで処理し、新しく再生を始める効果音にも適用する。

using ::cw::lib::Bytes;
using ::cw::lib::Integer;
using ::cw::lib::Real;
using ::cw::lib::String;
using ::cw::lib::Surface;
using ::cw::lib::Decimal;

template<class Interface, class Apply>
bool defer_audio_to_game_thread(Interface& source, Apply apply)
{
    const auto runtime = ::cw::g_cwpy;
    if (!runtime) return false;
    std::thread::id owner;
    {
        const std::scoped_lock lock(runtime->m_game_exec_mutex);
        owner = runtime->m_game_thread_id;
    }
    // Pythonには常にCWPyスレッドがある。ネイティブ版の初期化中やヘッドレスツー
    // ルでは処理先がまだない場合があり、その状態に限り同じ設定処理をその場で実行する。
    if (owner == std::thread::id{} || owner == std::this_thread::get_id())
        return false;
    // Pythonの束縛メソッドは、キューに積んだ呼び出しを実行するまでselfを生存させる。
    // ネイティブ版のEngine音声オブジェクトも共有所有する。shared_from_this()
    // で未所有のスレッド間オブジェクトを拒否し、無効になるthisをキューへ積まない。
    const auto retained = source.shared_from_this();
    runtime->exec_func([retained, apply](const ::cw::lib::Arguments&,
                                      const ::cw::lib::KeywordArguments&) {
        apply(*retained);
        return ::cw::lib::Value{};
    });
    return true;
}

bool read_binary_path(const std::filesystem::path& path, Bytes& data)
{
    if (::cardwirth::platform::is_mounted_user_path(path)) {
        const auto mounted = ::cardwirth::platform::read_mounted_user_file(path);
        if (!mounted) return false;
        data.resize(mounted->size());
        if (!mounted->empty())
            std::memcpy(data.data(), mounted->data(), mounted->size());
        return true;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    // 入出力の失敗が状態に反映されるよう、istreamを通して読む。ストリームバッファのイテレ
    // ータはその状態更新を経由しない。また、eofだけでbadbitを見落とさないようにする。
    data.clear();
    std::array<char, 65536> buffer;
    for (;;) {
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = file.gcount();
        if (count > 0) data.insert(data.end(), buffer.data(), buffer.data() + count);
        if (file.bad() || (file.fail() && !file.eof())) {
            data.clear();
            return false;
        }
        if (file.eof()) return true;
    }
}

bool storage_file_exists(const std::filesystem::path& path)
{
    if (::cardwirth::platform::is_mounted_user_path(path)) {
        const auto status = ::cardwirth::platform::stat_mounted_user_path(path);
        return status && status->is_file();
    }
    std::error_code error;
    return std::filesystem::is_regular_file(path, error);
}

bool storage_path_exists(const std::filesystem::path& path)
{
    if (::cardwirth::platform::is_mounted_user_path(path)) {
        const auto status = ::cardwirth::platform::stat_mounted_user_path(path);
        return status && status->exists();
    }
    std::error_code error;
    return std::filesystem::exists(path, error);
}

bool storage_path_exists_checked(const std::filesystem::path& path)
{
    if (::cardwirth::platform::is_mounted_user_path(path)) {
        const auto status = ::cardwirth::platform::stat_mounted_user_path(path, true);
        return status && status->exists();
    }
    std::error_code error;
    const auto result = std::filesystem::exists(path, error);
    if (error) throw std::filesystem::filesystem_error("path existence check", path, error);
    return result;
}

bool audio_disabled_for_test() noexcept
{
#if CW_UTIL_HAS_PORTABLE_MIDI
    const auto* value = std::getenv("CARDWIRTH_DISABLE_AUDIO");
    return value && *value && std::strcmp(value, "0") != 0;
#else
    return false;
#endif
}

#if defined(_WIN32)
std::wstring wide_from_utf8(const String& value);

bool managed_userdata_session() noexcept
{
    wchar_t value[2]{};
    return GetEnvironmentVariableW(L"CARDWIRTHC_MANAGED_USERDATA_SESSION",
                                   value, 2U) == 1U &&
           value[0] == L'1';
}

bool starts_with_ascii_case_insensitive(std::string_view value,
                                        std::string_view prefix) noexcept
{
    if (value.size() < prefix.size()) return false;
    for (std::size_t index = 0; index < prefix.size(); ++index) {
        const auto fold = [](unsigned char character) {
            return character >= 'A' && character <= 'Z'
                ? static_cast<unsigned char>(character - 'A' + 'a')
                : character;
        };
        if (fold(static_cast<unsigned char>(value[index])) !=
            fold(static_cast<unsigned char>(prefix[index]))) return false;
    }
    return true;
}
#endif

#if CW_UTIL_HAS_SDL_MIXER
    MIX_Mixer* g_sdl_mixer{};
bool g_sdl_audio_initialized{};
bool g_sdl_mixer_initialized{};

void run_mixer_on_main_thread(const std::function<void()>& operation)
{
    // SDL_mixer3のデバイス作成・破棄はSDLのメインスレッドで行う必要がある。Python
    // のミキサーはこの制約を内部に隠している。トラックの扱いはゲームスレッドで管理する。
    if (SDL_IsMainThread()) {
        operation();
        return;
    }
    const auto* frame_object = ::cw::g_cwpy
        ? std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_frame.storage()) : nullptr;
    const auto frame = frame_object
        ? std::dynamic_pointer_cast<::cw::frame::Frame>(*frame_object) : nullptr;
    if (::cw::g_quit_app.load(std::memory_order_acquire) || !frame)
        throw std::runtime_error("SDL audio main-thread dispatcher unavailable");
    // Frameはゲームスレッドの終了を待つ前に、promiseを伴う呼び出しを中止する。SDL_RunOnMainThread(wait=true)
    // は別のキューを使うため、この終了待ちとデッドロックするおそれが
    // ある。終了処理に対応した共通のUIメールボックスを再利用する。
    ::cw::lib::without_runtime_access([&] {
        frame->sync_exec([&](const ::cw::lib::Arguments&,
                            const ::cw::lib::KeywordArguments&) -> ::cw::lib::Value {
            if (!SDL_IsMainThread())
                throw std::runtime_error("SDL audio dispatcher is not on SDL's main thread");
            operation(); // メールボックスはコールバックの例外を呼び出し元へ伝える。
            return {};
        });
    });
}

struct MixerTrack {
    MIX_Track* track{};
    MIX_Audio* audio{};
    // MIX_LoadAudioNoCopyはストリーミング形式の元バイト列
    // を保持する。復号したデータベースの内容は音声の生存期間中メモリに保持し
    // 、暗号化されたEngineセッションでは一時ファイルへ書き出さない。
    std::vector<std::byte> mounted_data;
};
std::unordered_map<const MusicInterface*, MixerTrack> g_music_tracks;
std::unordered_map<const SoundInterface*, MixerTrack> g_sound_tracks;

Sint64 mixer_fade_frames(MIX_Track* track, Integer milliseconds)
{
    // SDL_mixer3のStopTrackは、Pythonのフェード時間のミリ秒と異なり、サ
    // ンプルフレーム数を受け取る。無効値や入力なしの変換で、意図しない負のフェード値にしない。
    return track && milliseconds > 0
        ? std::max<Sint64>(0, MIX_TrackMSToFrames(track, milliseconds)) : 0;
}

struct FadingMixerTrack {
    MixerTrack value;
    bool music{}, scenario{};
    Integer channel{};
};
std::vector<FadingMixerTrack> g_fading_mixer_tracks;

void destroy_mixer_track(MixerTrack& value)
{
    if (value.track) MIX_DestroyTrack(value.track);
    if (value.audio) MIX_DestroyAudio(value.audio);
    value = {}; // NoCopyの元バイト列は、音声とトラックの破棄後に解放する。
}

void finish_channel_fades(bool music, bool scenario, Integer channel)
{
    std::erase_if(g_fading_mixer_tracks, [&](auto& fade) {
        if (fade.music != music || fade.scenario != scenario || fade.channel != channel) return false;
        destroy_mixer_track(fade.value);
        return true;
    });
}

void retire_mixer_track(MixerTrack& value, Integer milliseconds,
                        bool music, bool scenario, Integer channel)
{
    const auto frames = mixer_fade_frames(value.track, milliseconds);
    if (value.track && frames > 0 && MIX_StopTrack(value.track, frames)) {
        // NoCopyのバイトバッファの所有権を移す前に、所有情報を格納する領域を確保する。
        g_fading_mixer_tracks.push_back({{}, music, scenario, channel});
        g_fading_mixer_tracks.back().value = std::move(value);
        value = {};
    } else {
        destroy_mixer_track(value);
    }
}
#endif

#if CW_UTIL_HAS_PORTABLE_MIDI
std::unordered_map<const MusicInterface*,
                   std::unique_ptr<::cardwirth::explorer::MidiPlayer>> g_portable_music_midi_players;
std::unordered_map<const SoundInterface*,
                   std::unique_ptr<::cardwirth::explorer::MidiPlayer>> g_portable_sound_midi_players;

std::filesystem::path portable_midi_data_directory(const String& resolved = {})
{
    std::error_code error;
    const auto has_soundfont = [&error](const std::filesystem::path& directory) {
        error.clear();
        return std::filesystem::is_directory(directory / "SoundFont", error) && !error;
    };
    if (!::cw::g_exepath.empty()) {
        const auto candidate = std::filesystem::u8path(::cw::g_exepath).parent_path() / "Data";
        if (has_soundfont(candidate)) return candidate;
    }
    if (!resolved.empty()) {
        auto candidate = std::filesystem::u8path(resolved).parent_path();
        for (int depth = 0; depth < 5 && !candidate.empty(); ++depth) {
            if (has_soundfont(candidate / "Data")) return candidate / "Data";
            const auto parent = candidate.parent_path();
            if (parent == candidate) break;
            candidate = parent;
        }
    }
    const auto current = std::filesystem::current_path(error);
    if (!error && has_soundfont(current / "Data")) return current / "Data";
    return error ? std::filesystem::u8path("Data") : current / "Data";
}

int mixer_repeat_count(const Integer loopcount)
{
    return loopcount == 0
        ? -1
        : static_cast<int>(std::clamp<Integer>(
              loopcount - 1, 0, std::numeric_limits<int>::max()));
}
#endif

#if defined(_WIN32)
std::mutex g_mci_music_mutex;
std::unordered_map<Integer, String> g_mci_music_aliases;
std::unordered_map<Integer, std::uint64_t> g_mci_music_generations;
std::unordered_map<Integer, Integer> g_mci_music_repeats;
std::unordered_map<Integer, std::chrono::steady_clock::time_point> g_mci_music_stopped_since;
std::chrono::steady_clock::time_point g_mci_music_last_poll{};

void close_mci_music(Integer channel)
{
    ++g_mci_music_generations[channel];
    g_mci_music_repeats.erase(channel);
    g_mci_music_stopped_since.erase(channel);
    const auto found = g_mci_music_aliases.find(channel);
    if (found == g_mci_music_aliases.end()) return;
    const auto alias = wide_from_utf8(found->second);
    mciSendStringW((L"stop " + alias).c_str(), nullptr, 0, nullptr);
    mciSendStringW((L"close " + alias).c_str(), nullptr, 0, nullptr);
    g_mci_music_aliases.erase(found);
}

bool play_mci_midi(const String& path, Integer channel, Integer loopcount)
{
    std::scoped_lock lock(g_mci_music_mutex);
    close_mci_music(channel);
    const String alias_utf8 = "cwbgm" + std::to_string(channel);
    const auto alias = wide_from_utf8(alias_utf8);
    const auto wide_path = wide_from_utf8(path);
    if (wide_path.empty()) return false;
    const auto open = L"open \"" + wide_path + L"\" type sequencer alias " + alias;
    const auto open_error = mciSendStringW(open.c_str(), nullptr, 0, nullptr);
    if (open_error != 0) {
#if defined(CARDWIRTH_LOG_ENABLED)
        ::cardwirth::debug::log("Windows MIDI open failed: path=" + path + " error=" + std::to_string(open_error));
#endif
        return false;
    }
    // MCIのシーケンサデバイスは文字列コマンドのrepeatフラグに対応せ
    // ず、MCIERR_UNSUPPORTED_FUNCTIONとなる。MIDI
    // は通常どおり開始し、末尾に達したらゲームスレッド上の音声更新で再開する。
    // loopcount=0はCardWirthのBGM無限再生を表す。
    const auto play = L"play " + alias;
    const auto play_error = mciSendStringW(play.c_str(), nullptr, 0, nullptr);
    if (play_error != 0) {
#if defined(CARDWIRTH_LOG_ENABLED)
        ::cardwirth::debug::log("Windows MIDI play failed: path=" + path + " error=" + std::to_string(play_error));
#endif
        mciSendStringW((L"close " + alias).c_str(), nullptr, 0, nullptr);
        return false;
    }
    g_mci_music_aliases[channel] = alias_utf8;
    ++g_mci_music_generations[channel];
    auto repeats = loopcount == 0
        ? Integer{-1}
        : std::max<Integer>(0, loopcount - 1);
    if (repeats != 0) g_mci_music_repeats[channel] = repeats;
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log("Windows MIDI playback started: path=" + path +
                            " channel=" + std::to_string(channel) +
                            " loopcount=" + std::to_string(loopcount));
#endif
    return true;
}

// Windowsの再生はPyのバックエンドと同じ順序でBASSとWinMM-MCIを使う。
// 共通のMusic/Sound APIはこの互換性を維持し、他のプラットフォームやマウント
// されたメモリ内音声には移植可能なバックエンドを使う。PythonのSoundInterface
// のtype 1に相当し、BASSが使えない場合はWinMMでWAV/MP3効果音を再生
// する。Python側でもWinMMの使用によりSDL_mixerの問題を避けている。
String mci_sound_alias(const bool from_scenario, const Integer channel)
{
    return from_scenario ? "cwsnd1_" + std::to_string(channel) : String("cwsnd2");
}

void close_mci_sound(const String& alias)
{
    const auto name = wide_from_utf8(alias);
    mciSendStringW((L"stop " + name).c_str(), nullptr, 0, nullptr);
    mciSendStringW((L"close " + name).c_str(), nullptr, 0, nullptr);
}

void set_mci_sound_volume(const String& alias, const Real volume)
{
    // Pythonはここで浮動小数点値を送り、MCIにとって不正な値
    // となる。代わりにplay()と同じ0～1000の整数を送る。
    const auto value = std::to_wstring(static_cast<int>(std::clamp(volume, 0.0, 1.0) * 1000));
    mciSendStringW((L"setaudio " + wide_from_utf8(alias) + L" volume to " + value).c_str(),
                   nullptr, 0, nullptr);
}

bool play_mci_sound(const String& path, const String& alias, const Real volume)
{
    close_mci_sound(alias);
    const auto wide_path = wide_from_utf8(path);
    if (wide_path.empty()) return false;
    const auto name = wide_from_utf8(alias);
    if (mciSendStringW((L"open \"" + wide_path + L"\" alias " + name).c_str(), nullptr, 0, nullptr) != 0)
        return false;
    set_mci_sound_volume(alias, volume);
    if (mciSendStringW((L"play " + name).c_str(), nullptr, 0, nullptr) != 0) {
        mciSendStringW((L"close " + name).c_str(), nullptr, 0, nullptr);
        return false;
    }
    return true;
}
#endif

struct SideClickState {
    std::shared_ptr<::cw::lib::Panel> panel{};
    std::shared_ptr<::cw::lib::Window> left{};
    std::shared_ptr<::cw::lib::Window> right{};
    Integer mouse_x{};
    Integer mouse_y{};
    bool left_hover{};
    bool right_hover{};
};
SideClickState g_sideclick{};
std::mutex g_ui_state_mutex;
std::unordered_map<const ::cw::lib::DeviceContext*, DeviceContextState> g_device_context_states;
std::unordered_map<const ::cw::lib::Window*, WindowState> g_window_states;

String lower_ascii(String value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

#if defined(_WIN32)
std::wstring wide_from_utf8(const String& value)
{
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "MultiByteToWideChar");
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), size) != size)
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "MultiByteToWideChar");
    return result;
}

String utf8_from_wide(const std::wstring& value)
{
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "WideCharToMultiByte");
    String result(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr) != size)
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "WideCharToMultiByte");
    return result;
}

void check_hresult(HRESULT result, const char* operation)
{
    if (FAILED(result)) throw std::system_error(static_cast<int>(result), std::system_category(), operation);
}
#endif

// Pythonのopen/writeと共通の型規則。文字コードは開く段階で
// 、書き込む内容は開いた後に検証し、通常の書き込みで失敗する順序を保つ。
void validate_file_encoding(const String& mode, const std::optional<String>& encoding)
{
    if (mode.find('b') != String::npos && encoding)
        throw std::invalid_argument("binary mode does not take an encoding argument");
}

void validate_file_payload(const ::cw::lib::Value& data, const String& mode)
{
    if (mode.find('b') != String::npos) {
        if (!std::holds_alternative<Bytes>(data.storage()))
            throw std::invalid_argument("binary write requires Bytes");
    } else if (!std::holds_alternative<String>(data.storage())) {
        throw std::invalid_argument("text write requires String");
    }
}

Bytes encode_file_text(const String& text, const std::optional<String>& encoding, bool text_mode)
{
    // Pythonのtext openと同じく、改行は文字列の段階で変換してからencodeする。
    // encode済みのUTF-16などをCRTのtext streamへ渡すと、文字内の0x0Aまで
    // 変換されるため、実際の書込みは全経路でbinary streamを使う。
#if defined(_WIN32)
    constexpr bool native_crlf = true;
#else
    constexpr bool native_crlf = false;
#endif
    String translated;
    const String* source = &text;
    if (text_mode && native_crlf && text.find('\n') != String::npos) {
        translated.reserve(text.size());
        for (const char ch : text) {
            if (ch == '\n') translated.push_back('\r');
            translated.push_back(ch);
        }
        source = &translated;
    }
    // Pythonとの意図的な差: encoding省略時はlocaleに依存せずUTF-8とする。
    // 既存の共通Storage／全OSで同じ保存形式を保つ。明示codecはその指定に従う。
    if (!encoding || encoding->empty() || lower_ascii(*encoding) == "utf-8" || lower_ascii(*encoding) == "utf8")
        return Bytes(source->begin(), source->end());
    const auto name = lower_ascii(*encoding);
    using ::cw::lib::encoding::TextEncoding;
    TextEncoding codec{};
    if (name == "cp932" || name == "mbcs") codec = TextEncoding::Cp932;
    else if (name == "shift_jis" || name == "shift-jis") codec = TextEncoding::ShiftJis;
    else if (name == "cp437") codec = TextEncoding::Cp437;
    else if (name == "latin-1" || name == "latin1" || name == "iso-8859-1") codec = TextEncoding::Latin1;
    else if (name == "utf-16") codec = TextEncoding::Utf16;
    else if (name == "utf-16-le" || name == "utf16-le") codec = TextEncoding::Utf16LE;
    else if (name == "utf-16-be" || name == "utf16-be") codec = TextEncoding::Utf16BE;
    else throw std::invalid_argument("unsupported text encoding: " + *encoding);
    return ::cw::lib::encoding::encode(*source, codec, {::cw::lib::encoding::ErrorPolicy::Strict, false});
}

String value_string(const ::cw::lib::Value& value)
{
    if (const auto* string = std::get_if<String>(&value.storage())) return *string;
    if (const auto* decimal = std::get_if<Decimal>(&value.storage())) return decimal->to_string(true);
    return {};
}

Integer value_integer(const ::cw::lib::Value& value, Integer fallback)
{
    if (const auto* integer = std::get_if<Integer>(&value.storage())) return *integer;
    if (const auto* real = std::get_if<Real>(&value.storage())) {
        const long double number = static_cast<long double>(*real);
        if (!std::isfinite(*real) || number > static_cast<long double>(std::numeric_limits<Integer>::max()) || number < static_cast<long double>(std::numeric_limits<Integer>::min()))
            throw std::overflow_error("Real does not fit compat::Integer");
        return static_cast<Integer>(*real);
    }
    if (const auto* decimal = std::get_if<Decimal>(&value.storage()))
        return decimal->to_integer(::cw::lib::DecimalRounding::Down);
    return fallback;
}

bool value_truthy(const ::cw::lib::Value& value, bool fallback = false)
{
    if (const auto* boolean = std::get_if<bool>(&value.storage())) return *boolean;
    if (const auto* integer = std::get_if<Integer>(&value.storage())) return *integer != 0;
    if (const auto* real = std::get_if<Real>(&value.storage())) return *real != 0.0;
    if (const auto* decimal = std::get_if<Decimal>(&value.storage())) return !decimal->is_zero();
    if (const auto* string = std::get_if<String>(&value.storage())) return !string->empty();
    return fallback;
}

::cw::lib::Color value_color(const ::cw::lib::Value& value, ::cw::lib::Color fallback)
{
    if (const auto* bytes = std::get_if<Bytes>(&value.storage()); bytes && bytes->size() >= 3)
        return {(*bytes)[0], (*bytes)[1], (*bytes)[2], bytes->size() > 3 ? (*bytes)[3] : static_cast<std::uint8_t>(255)};
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage()); object && *object) {
        if (const auto sequence = std::dynamic_pointer_cast<::cw::data::VariantSequence>(*object); sequence && sequence->m_values.size() >= 3)
            return {static_cast<std::uint8_t>(std::clamp<Integer>(value_integer(sequence->m_values[0], fallback.r), 0, 255)),
                    static_cast<std::uint8_t>(std::clamp<Integer>(value_integer(sequence->m_values[1], fallback.g), 0, 255)),
                    static_cast<std::uint8_t>(std::clamp<Integer>(value_integer(sequence->m_values[2], fallback.b), 0, 255)),
                    static_cast<std::uint8_t>(sequence->m_values.size() > 3 ? std::clamp<Integer>(value_integer(sequence->m_values[3], fallback.a), 0, 255) : fallback.a)};
    }
    return fallback;
}

std::optional<Decimal> numeric_decimal(const ::cw::lib::Value& value)
{
    if (const auto* decimal = std::get_if<Decimal>(&value.storage())) return *decimal;
    if (const auto* integer = std::get_if<Integer>(&value.storage())) return Decimal(*integer);
    if (const auto* real = std::get_if<Real>(&value.storage())) {
        if (!std::isfinite(*real)) return std::nullopt;
        std::ostringstream text;
        text << std::setprecision(std::numeric_limits<Real>::max_digits10) << *real;
        try {
            return Decimal::parse(text.str());
        } catch (const ::cw::lib::DecimalError&) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

template <typename Tuple>
bool tuple_contains(const Tuple& values, const String& candidate)
{
    bool found = false;
    std::apply([&](const auto&... value) {
        ((found = found || lower_ascii(value) == candidate), ...);
    }, values);
    return found;
}

std::array<Integer, 4> scale_values()
{
    return {std::get<0>(::cw::g_SCALE_LIST), std::get<1>(::cw::g_SCALE_LIST),
            std::get<2>(::cw::g_SCALE_LIST), std::get<3>(::cw::g_SCALE_LIST)};
}

bool is_image_file(const std::filesystem::path& path)
{
    return tuple_contains(::cw::g_EXTS_IMG, lower_ascii(path.extension().generic_string()));
}

// PythonのLogicalStrは大文字・小文字を区別しない自
// 然順で比較し、最後の同順位判定には元の表記を使う。ファイル名の並
// べ替えにも使えるよう、この補助処理はValueから独立させる。
Integer natural_compare(const String& lhs, const String& rhs)
{
    const auto lower = [](const String& value) { return ::cw::lib::encoding::python_lower(value); };
    const String a = lower(lhs);
    const String b = lower(rhs);
    std::size_t ia = 0;
    std::size_t ib = 0;
    while (ia < a.size() && ib < b.size()) {
        const bool adigit = std::isdigit(static_cast<unsigned char>(a[ia])) != 0;
        const bool bdigit = std::isdigit(static_cast<unsigned char>(b[ib])) != 0;
        if (adigit && bdigit) {
            const std::size_t sa = ia;
            const std::size_t sb = ib;
            while (ia < a.size() && std::isdigit(static_cast<unsigned char>(a[ia]))) ++ia;
            while (ib < b.size() && std::isdigit(static_cast<unsigned char>(b[ib]))) ++ib;
            const auto na = a.substr(sa, ia - sa);
            const auto nb = b.substr(sb, ib - sb);
            const auto trim_zero = [](const String& value) {
                const auto first = value.find_first_not_of('0');
                return first == String::npos ? String("0") : value.substr(first);
            };
            const auto ta = trim_zero(na);
            const auto tb = trim_zero(nb);
            if (ta.size() != tb.size()) return ta.size() < tb.size() ? -1 : 1;
            if (ta != tb) return ta < tb ? -1 : 1;
            // LogicalStrは(int(ss), ss)を保持する。数値が等しい場合は元の
            // 数字表記を比較するため、Pythonでは"01234" < "1234"となる。
            if (na != nb) return na < nb ? -1 : 1;
            continue;
        }
        if (a[ia] != b[ib]) return static_cast<unsigned char>(a[ia]) < static_cast<unsigned char>(b[ib]) ? -1 : 1;
        ++ia;
        ++ib;
    }
    if (ia != a.size() || ib != b.size()) return ia == a.size() ? -1 : 1;
    if (lhs == rhs) return 0;
    return lhs < rhs ? -1 : 1;
}

Integer value_type_rank(const ::cw::lib::Value& value)
{
    const auto& storage = value.storage();
    if (std::holds_alternative<std::monostate>(storage)) return 0;
    if (std::holds_alternative<bool>(storage)) return 1;
    if (std::holds_alternative<Integer>(storage)) return 2;
    if (std::holds_alternative<Real>(storage)) return 3;
    if (std::holds_alternative<Decimal>(storage)) return 4;
    if (std::holds_alternative<String>(storage)) return 5;
    if (std::holds_alternative<Bytes>(storage)) return 6;
    return 7;
}

Integer value_compare(const std::optional<::cw::lib::Value>& lhs,
                      const std::optional<::cw::lib::Value>& rhs,
                      bool literal_strings = false)
{
    const bool left_none = !lhs || lhs->is_none();
    const bool right_none = !rhs || rhs->is_none();
    if (left_none && right_none) return 0;
    if (literal_strings && left_none != right_none)
        throw std::invalid_argument("LogicalStr sequence values do not support ordering");
    if (left_none) return -1;
    if (right_none) return 1;
    // Integer、Real、Decimalは、下の異種格
    // 納型の順序より先に正確な十進数として比較する。ネイティ
    // ブのRealは二進浮動小数点なので、変換はこの比較箇所
    // だけで行う。非有限のReal値は従来の型順序を保つ。
    const auto left_number = literal_strings && std::holds_alternative<bool>(lhs->storage())
        ? std::optional<Decimal>(Decimal(std::get<bool>(lhs->storage())?1:0)) : numeric_decimal(*lhs);
    const auto right_number = literal_strings && std::holds_alternative<bool>(rhs->storage())
        ? std::optional<Decimal>(Decimal(std::get<bool>(rhs->storage())?1:0)) : numeric_decimal(*rhs);
    if (left_number && right_number) return left_number->compare(*right_number);
    const auto left_rank = value_type_rank(*lhs);
    const auto right_rank = value_type_rank(*rhs);
    if (left_rank != right_rank) {
        if(literal_strings)throw std::invalid_argument("LogicalStr sequence values do not support ordering");
        // Pythonのcmpは異種の値を比較する際に整数を優先
        // する。他の組み合わせは格納型の順位で全順序を定める。
        if (left_rank == 2) return -1;
        if (right_rank == 2) return 1;
        return left_rank < right_rank ? -1 : 1;
    }
    if (const auto* a = std::get_if<bool>(&lhs->storage())) {
        const auto b = std::get<bool>(rhs->storage());
        return *a == b ? 0 : (*a ? 1 : -1);
    }
    if (const auto* a = std::get_if<Integer>(&lhs->storage())) {
        const auto b = std::get<Integer>(rhs->storage());
        return *a == b ? 0 : (*a < b ? -1 : 1);
    }
    if (const auto* a = std::get_if<Real>(&lhs->storage())) {
        const auto b = std::get<Real>(rhs->storage());
        if (std::isnan(*a) || std::isnan(b)) return 0;
        return *a == b ? 0 : (*a < b ? -1 : 1);
    }
    if (const auto* a = std::get_if<Decimal>(&lhs->storage())) {
        const auto b = std::get<Decimal>(rhs->storage());
        return a->compare(b);
    }
    if (const auto* a = std::get_if<String>(&lhs->storage())) {
        const auto& b = std::get<String>(rhs->storage());
        if (literal_strings) return *a == b ? 0 : *a < b ? -1 : 1;
        return natural_compare(*a, b);
    }
    if (const auto* a = std::get_if<Bytes>(&lhs->storage())) {
        const auto& b = std::get<Bytes>(rhs->storage());
        return *a == b ? 0 : (*a < b ? -1 : 1);
    }
    if (const auto* a = std::get_if<::cw::lib::ObjectPtr>(&lhs->storage())) {
        const auto b = std::get<::cw::lib::ObjectPtr>(rhs->storage());
        if (a->get() == b.get()) return 0;
        if (!*a || !b) return !*a ? -1 : 1;
        // Pythonは再帰的な比較でポインタ順を割り当てず、例外を投
        // げる。全体の訪問済みキャッシュではなく、現在比較中の組だけを
        // 検出し、循環しない共有部分を繰り返し比較できるようにする。
        using Pair = std::pair<const ::cw::lib::Object*, const ::cw::lib::Object*>;
        static thread_local std::set<Pair> active;
        const Pair pair{a->get(), b.get()};
        if (!active.insert(pair).second) throw std::runtime_error("recursive Value comparison");
        struct Leave { std::set<Pair>& active; Pair pair; ~Leave(){active.erase(pair);} } leave{active,pair};
        const auto left_seq = std::dynamic_pointer_cast<::cw::data::VariantSequence>(*a);
        const auto right_seq = std::dynamic_pointer_cast<::cw::data::VariantSequence>(b);
        if (left_seq && right_seq) {
            for (std::size_t i=0;i<std::min(left_seq->m_values.size(),right_seq->m_values.size());++i) {
                const auto result=value_compare(left_seq->m_values[i],right_seq->m_values[i],literal_strings);
                if (result) return result;
            }
            return left_seq->m_values.size()==right_seq->m_values.size()?0:left_seq->m_values.size()<right_seq->m_values.size()?-1:1;
        }
        const auto comparable=std::dynamic_pointer_cast<::cw::util::Comparable>(*a);
        const auto other=std::dynamic_pointer_cast<::cw::util::Comparable>(b);
        if (comparable && other && typeid(**a)==typeid(*b)) {
            if(comparable->less_than(*rhs))return -1;
            if(other->less_than(*lhs))return 1;
            return 0;
        }
        // ObjectにはPythonのリフレクションやoperator<がない
        // 。未対応のネイティブオブジェクトは確保順で並べず、明示的に失敗させる。
        throw std::invalid_argument("incomparable object Values");
    }
    return 0;
}

bool ordering_value_equal(const ::cw::lib::Value& lhs,const ::cw::lib::Value& rhs)
{
    const auto number=[](const ::cw::lib::Value& value){
        if(const auto* boolean=std::get_if<bool>(&value.storage()))return std::optional<Decimal>(Decimal(*boolean?1:0));
        return numeric_decimal(value);
    };
    const auto a=number(lhs),b=number(rhs);
    if(a && b)return a->compare(*b)==0;
    const auto* left=std::get_if<::cw::lib::ObjectPtr>(&lhs.storage());
    const auto* right=std::get_if<::cw::lib::ObjectPtr>(&rhs.storage());
    if(!left || !right)return lhs.storage()==rhs.storage();
    if(left->get()==right->get())return true;
    if(!*left || !*right)return false;
    using Pair=std::pair<const ::cw::lib::Object*,const ::cw::lib::Object*>;
    static thread_local std::set<Pair> active;
    const Pair pair{left->get(),right->get()};
    if(!active.insert(pair).second)throw std::runtime_error("recursive Value equality");
    struct Leave{std::set<Pair>& active;Pair pair;~Leave(){active.erase(pair);}}leave{active,pair};
    const auto lseq=std::dynamic_pointer_cast<::cw::data::VariantSequence>(*left);
    const auto rseq=std::dynamic_pointer_cast<::cw::data::VariantSequence>(*right);
    if(lseq && rseq){
        if(lseq->m_values.size()!=rseq->m_values.size())return false;
        for(std::size_t i=0;i<lseq->m_values.size();++i)if(!ordering_value_equal(lseq->m_values[i],rseq->m_values[i]))return false;
        return true;
    }
    const auto logical=std::dynamic_pointer_cast<::cw::util::LogicalStr>(*left);
    if(logical && std::dynamic_pointer_cast<::cw::util::LogicalStr>(*right))return logical->equals(rhs);
    // Objectには汎用の__eq__仮想関数がない。未対応のインスタンスは順序
    // 上の同値ではなく、Pythonの既定と同じオブジェクト同一性で等値判定する。
    return false;
}

::cw::lib::Value attribute_part(const ::cw::lib::Value& value, const String& name)
{
    // Pythonのattrgetterは動的なリフレクションを使う。ネイティブのObjectにはその
    // 機能がないため、全プラットフォームで共通の登録済みフィールドとStructValの名前を使う。
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    if (!object || !*object) throw std::invalid_argument("attribute access on scalar Value");
    if (const auto adventurer = std::dynamic_pointer_cast<::cw::header::AdventurerHeader>(*object)) {
        if (name == "order") return adventurer->m_order;
        if (name == "name") return adventurer->m_name;
        if (name == "fpath") return adventurer->m_fpath;
        if (name == "level") return adventurer->m_level;
        if (name == "age") return adventurer->m_age;
        if (name == "sex") return adventurer->m_sex;
        if (name == "ep") return adventurer->m_ep;
        if (name == "desc") return adventurer->m_desc;
        if (name == "album") return adventurer->m_album;
        if (name == "lost") return adventurer->m_lost;
        if (name == "race") return adventurer->m_race;
    } else if (const auto card = std::dynamic_pointer_cast<::cw::header::CardHeader>(*object)) {
        if (name == "order") return card->m_order;
        if (name == "name") return card->m_name;
        if (name == "fpath") return card->m_fpath;
        if (name == "level") return card->m_level;
        if (name == "type") return card->m_type;
        if (name == "type_id") return card->m_type_id;
        if (name == "scenario") return card->m_scenario;
        if (name == "author") return card->m_author;
        if (name == "star") return card->m_star;
        if (name == "premium") return card->m_premium;
        if (name == "price") return card->m_price;
        if (name == "vocation_for_sort") return card->m_vocation_for_sort;
        if (name == "uselimit") return card->m_uselimit;
        if (name == "hold") return card->m_hold;
        if (name == "recycle") return card->m_recycle;
        if (name == "attachment") return card->m_attachment;
    } else if (const auto party = std::dynamic_pointer_cast<::cw::header::PartyHeader>(*object)) {
        if (name == "order") return party->m_order;
        if (name == "name") return party->m_name;
        if (name == "fpath") return party->m_fpath;
        if (name == "money") return party->m_money;
    } else if (const auto image = std::dynamic_pointer_cast<::cw::image::ImageInfo>(*object)) {
        if (name == "path") return image->m_path;
    } else if (const auto structure = std::dynamic_pointer_cast<::cw::data::StructVal>(*object)) {
        for (std::size_t i = 0; i < structure->m_member_names.size() && i < structure->m_member_values.size(); ++i)
            if (structure->m_member_names[i] == name) return structure->m_member_values[i];
    }
    throw std::invalid_argument("unknown attribute: " + name);
}

::cw::lib::Value sort_key_for(const ::cw::lib::Value& value, const ::cw::lib::Arguments& attrs)
{
    if (attrs.empty()) return value;
    if (attrs.size()>1) {
        auto key=std::make_shared<::cw::data::VariantSequence>();
        // attrgetter(*attrs)は比較前に全属性を取得し、先頭フ
        // ィールドが異なる場合でもタプル1つをキーとしてcmpfuncへ渡す。
        for(const auto& attr:attrs)key->m_values.push_back(sort_key_for(value,{attr}));
        return std::static_pointer_cast<::cw::lib::Object>(key);
    }
    const auto* name = std::get_if<String>(&attrs.front().storage());
    if (!name) throw std::invalid_argument("attribute name must be a string");
    ::cw::lib::Value result = value;
    std::size_t begin = 0;
    while (begin <= name->size()) {
        const auto dot = name->find('.', begin);
        const auto part = name->substr(begin, dot == String::npos ? String::npos : dot - begin);
        if (part.empty()) throw std::invalid_argument("empty attribute component");
        result = attribute_part(result, part);
        if (dot == String::npos) break;
        begin = dot + 1;
    }
    return result;
}

// RFC 1321に従う小規模で独立したMD5実装。OpenSSLやWindows
// CNGをリンクしないビルドでもget_md5を利用できるようにする。
String md5_hex(const Bytes& data)
{
    // ファイルのハッシュ計算と同じ逐次処理の実装を使う。Pythonの不変な
    // bytes入力に対し、パディング付きの全体コピーを別途作る必要はない。
    auto hash = ::cw::lib::HashContext::md5();
    hash.update(data);
    return hash.hexdigest();
}

struct AnimatedBitmapInfo {
    std::weak_ptr<::cw::lib::Bitmap> bitmap;
    std::weak_ptr<::cw::lib::Bitmap> resolved;
    String path;
    std::optional<Integer> instance_id;
    Integer frame_id{};
    bool mask{};
    ::cw::lib::Value maskpos;
    Real scale{1.0};
    String owner;
    std::optional<Bytes> data;
    bool force_opaque{};
};

std::mutex animated_bitmap_mutex;
std::unordered_map<const ::cw::lib::Bitmap*, AnimatedBitmapInfo> animated_bitmap_infos;

void sweep_animated_bitmap_infos_locked()
{
    for (auto it = animated_bitmap_infos.begin(); it != animated_bitmap_infos.end();) {
        if (it->second.bitmap.expired()) it = animated_bitmap_infos.erase(it);
        else ++it;
    }
}

auto find_animated_bitmap_info_locked(const std::shared_ptr<::cw::lib::Bitmap>& bitmap)
    -> decltype(animated_bitmap_infos.begin())
{
    if (!bitmap) return animated_bitmap_infos.end();
    const auto found=animated_bitmap_infos.find(bitmap.get());
    if(found==animated_bitmap_infos.end()) return found;
    const auto registered=found->second.bitmap.lock();
    if(!registered || registered.get()!=bitmap.get()) {
        animated_bitmap_infos.erase(found);
        return animated_bitmap_infos.end();
    }
    return found;
}

std::mutex timing_mutex;
std::vector<long long> performance_times(1024, 0);
std::map<String, long long> performance_dictimes;
long long performance_timer_ns{};

struct MutexRecord {
    std::filesystem::path path;
#if defined(_WIN32)
    HANDLE handle{INVALID_HANDLE_VALUE};
#else
    int fd{-1};
#endif
};
std::vector<MutexRecord> mutex_records;
// Pythonの_lock_mutexは独立した再入不可のthreading.Lockで、OS
// の基本操作を含むcreate/exists/release/clearの全APIで共有する。
std::mutex mutex_api_mutex;
bool unlock_mutex_record(std::size_t index, std::optional<::cw::lib::Value>* public_file = nullptr);

void sync_performance_globals()
{
    g_timer = static_cast<Real>(performance_timer_ns);
    g_dictimes.clear();
    for (const auto& [key, value] : performance_dictimes) g_dictimes[key] = static_cast<Real>(value);
    auto values = std::make_shared<::cw::data::VariantSequence>();
    values->m_values.reserve(performance_times.size());
    for (const auto value : performance_times) values->m_values.emplace_back(static_cast<Real>(value));
    g_times = std::static_pointer_cast<::cw::lib::Object>(values);
}

void sync_mutex_globals()
{
    g__mutex.clear();
    for (const auto& record : mutex_records) g__mutex.emplace_back(::cw::lib::Value{}, ::cw::lib::encoding::path_to_utf8(record.path));
}

// cw.utilの正規表現は、util.pyに記載された小さな文
// 法だけを意図的に受け付ける。std::regexはPython
// が拒否する\dや{m,n}などの拡張を受け付けるため、代用で
// きない。この小規模なAST/NFAで拒否条件を正確に保つ。
std::u32string regex_characters(const String& text)
{
    const auto bytes = ::cw::lib::encoding::utf8_to_utf32(text, ::cw::lib::encoding::ByteOrder::Little);
    std::u32string result;
    for (std::size_t i = 0; i < bytes.size(); i += 4)
        result.push_back(::cw::lib::encoding::read_u32(bytes, i, ::cw::lib::encoding::ByteOrder::Little));
    return result;
}

struct RegexNode;
using RegexNodePtr = std::shared_ptr<RegexNode>;
struct RegexNode {
    enum class Kind { Literal, Dot, CharClass, Anchor, Group, Concat, Alt, Repeat };
    Kind kind{Kind::Literal};
    char32_t ch{};
    std::optional<std::u32string> public_literal{};
    std::optional<std::vector<std::pair<std::u32string,std::u32string>>> public_ranges{};
    bool negate{};
    char anchor{};
    char repeat{};
    std::vector<std::pair<char32_t, char32_t>> items{};
    std::vector<RegexNodePtr> children{};
    RegexNodePtr child{};
};

class RegexAstObject final : public ::cw::lib::Object {
public:
    RegexNodePtr root{};
    String pattern{};
};

// ネイティブ版は公開された捕捉用の型_ParseErrorを保ち、PyのメッセージをPython
// のBaseException.argsやトレースバックではなくstd::exception::what()
// で公開する。どちらのインターフェースも診断文字列を所有する。
class RegexParseError final : public ::cw::util::_ParseError, public std::runtime_error {
public:
    explicit RegexParseError(String message) : std::runtime_error(std::move(message)) {}
};
String utf8_codepoint(Integer code);

class RegexParser {
public:
    explicit RegexParser(const String& pattern, Integer cursor = 0, std::optional<Integer> limit = std::nullopt) : pattern_(regex_characters(pattern)), index_(cursor), limit_(limit.value_or(static_cast<Integer>(pattern_.size()))) {}
    Integer cursor() const { return index_; }

    RegexNodePtr parse()
    {
        const auto result = parse_alt();
        if (index_ != limit_) throw RegexParseError("unexpected trailing characters");
        return result;
    }

public:
    static constexpr char32_t end = 0x110000;
    char32_t peek() const {
        if (index_ >= limit_) return end;
        const auto offset = index_ < 0 ? static_cast<Integer>(pattern_.size()) + index_ : index_;
        if (offset < 0 || offset >= static_cast<Integer>(pattern_.size())) throw std::out_of_range("string index out of range");
        return pattern_[static_cast<std::size_t>(offset)];
    }

    char32_t consume(char32_t expected = end)
    {
        const auto value = peek();
        if (value == end) throw RegexParseError("unexpected end of pattern");
        if (expected != end && value != expected)
            throw RegexParseError("expected " + ::cw::lib::python_percent_detail::repr(utf8_codepoint(expected),false) +
                                  " but got " + ::cw::lib::python_percent_detail::repr(utf8_codepoint(value),false));
        ++index_;
        return value;
    }

    RegexNodePtr parse_alt()
    {
        std::vector<RegexNodePtr> branches{parse_concat()};
        while (peek() == '|') {
            consume('|');
            branches.push_back(parse_concat());
        }
        if (branches.size() == 1) return branches.front();
        auto node = std::make_shared<RegexNode>();
        node->kind = RegexNode::Kind::Alt;
        node->children = std::move(branches);
        return node;
    }

    RegexNodePtr parse_concat()
    {
        std::vector<RegexNodePtr> nodes;
        while (peek() != end && peek() != '|' && peek() != ')') nodes.push_back(parse_repeat());
        if (nodes.size() == 1) return nodes.front();
        auto node = std::make_shared<RegexNode>();
        node->kind = RegexNode::Kind::Concat;
        node->children = std::move(nodes);
        return node;
    }

    RegexNodePtr parse_repeat()
    {
        const auto atom = parse_atom();
        const auto op = peek();
        if (op != '*' && op != '+' && op != '?') return atom;
        consume();
        auto node = std::make_shared<RegexNode>();
        node->kind = RegexNode::Kind::Repeat;
        node->repeat = op;
        node->child = atom;
        return node;
    }

    RegexNodePtr parse_atom()
    {
        const auto value = peek();
        if (value == end) throw RegexParseError("unexpected end, atom expected");
        if (value == '*' || value == '+' || value == '?' || value == '|' || value == ')')
            throw RegexParseError("unexpected " + ::cw::lib::python_percent_detail::repr(utf8_codepoint(value),false));
        if (value == '(') {
            // Pythonの既定の再帰上限は1000フレームで、入れ子のグ
            // ループごとに文法処理4回と、ラッパー・末端の呼び出しが加わる
            // 。C++にはインタプリタの上限がないため、公開の解析処理とfullmatch
            // ラッパーで共通の247グループを上限にする。
            if (group_depth_ >= 247) throw RegexParseError("native regex nesting limit exceeded");
            struct GroupDepth { std::size_t& depth; explicit GroupDepth(std::size_t& d):depth(d){++depth;} ~GroupDepth(){--depth;} } guard(group_depth_);
            consume('(');
            const auto inner = parse_alt();
            if (peek() != ')') throw RegexParseError("missing closing )");
            consume(')');
            auto node = std::make_shared<RegexNode>();
            node->kind = RegexNode::Kind::Group;
            node->child = inner;
            return node;
        }
        if (value == '[') return parse_class();
        if (value == '.') {
            consume();
            auto node = std::make_shared<RegexNode>();
            node->kind = RegexNode::Kind::Dot;
            return node;
        }
        if (value == '^' || value == '$') {
            consume();
            auto node = std::make_shared<RegexNode>();
            node->kind = RegexNode::Kind::Anchor;
            node->anchor = value;
            return node;
        }
        if (value == '\\') {
            consume('\\');
            const auto escaped = peek();
            if (escaped == end || std::u32string(U".*+?[]()|^$\\").find(escaped) == std::u32string::npos) throw RegexParseError("unsupported escape sequence");
            consume();
            auto node = std::make_shared<RegexNode>();
            node->kind = RegexNode::Kind::Literal;
            node->ch = escaped;
            return node;
        }
        consume();
        auto node = std::make_shared<RegexNode>();
        node->kind = RegexNode::Kind::Literal;
        node->ch = value;
        return node;
    }

    char32_t parse_class_char()
    {
        const auto value = peek();
        if (value == end) throw RegexParseError("unterminated character class");
        if (value == '\\') {
            consume('\\');
            const auto escaped = peek();
            if (escaped == end || std::u32string(U".*+?[]()|^$\\").find(escaped) == std::u32string::npos) throw RegexParseError("unsupported escape sequence in class");
            consume();
            return escaped;
        }
        return consume();
    }

    RegexNodePtr parse_class()
    {
        consume('[');
        bool negate = false;
        if (peek() == '^') { negate = true; consume('^'); }
        std::vector<std::pair<char32_t, char32_t>> items;
        while (true) {
            if (peek() == end) throw RegexParseError("unterminated character class");
            if (peek() == ']') { consume(']'); break; }
            const auto first = parse_class_char();
            if (peek() == '-') {
                consume('-');
                if (peek() == end) throw RegexParseError("unterminated character class");
                if (peek() == ']') {
                    items.emplace_back(first, first);
                    items.emplace_back('-', '-');
                    continue;
                }
                const auto last = parse_class_char();
                if (last < first) throw RegexParseError("invalid range in character class");
                items.emplace_back(first, last);
            } else {
                items.emplace_back(first, first);
            }
        }
        auto node = std::make_shared<RegexNode>();
        node->kind = RegexNode::Kind::CharClass;
        node->negate = negate;
        node->items = std::move(items);
        return node;
    }

    std::u32string pattern_;
    Integer index_{};
    Integer limit_{};
    std::size_t group_depth_{};
};

std::set<Integer> regex_match_node(const RegexNodePtr& node, const std::u32string& text, Integer position);

std::set<Integer> regex_closure(const RegexNodePtr& node, const std::u32string& text, Integer position)
{
    std::set<Integer> result{position};
    std::set<Integer> frontier{position};
    while (!frontier.empty()) {
        std::set<Integer> next;
        for (const auto current : frontier) {
            for (const auto reached : regex_match_node(node, text, current)) {
                if (result.insert(reached).second) next.insert(reached);
            }
        }
        frontier = std::move(next);
    }
    return result;
}

std::set<Integer> regex_match_node(const RegexNodePtr& node, const std::u32string& text, Integer position)
{
    std::set<Integer> result;
    if (!node) throw std::logic_error("unknown regex node type");
    const auto character_at = [&]() {
        const auto offset=position<0?static_cast<Integer>(text.size())+position:position;
        // Pyは文字列の範囲外の文字アクセスでIndexErrorを投げる。ネイティブ版
        // も同じアクセス時点でout_of_rangeを投げ、空の一致として扱わない。
        if(offset<0 || offset>=static_cast<Integer>(text.size()))throw std::out_of_range("string index out of range");
        return text[static_cast<std::size_t>(offset)];
    };
    switch (node->kind) {
    case RegexNode::Kind::Literal:
        if (position < static_cast<Integer>(text.size())) {
            const auto ch=character_at();
            const bool matches=node->public_literal?std::u32string(1,ch)==*node->public_literal:ch==node->ch;
            if(matches)result.insert(position+1);
        }
        break;
    case RegexNode::Kind::Dot:
        if (position < static_cast<Integer>(text.size())) result.insert(position + 1);
        break;
    case RegexNode::Kind::CharClass:
        if (position < static_cast<Integer>(text.size())) {
            const auto ch = character_at();
            bool inside = false;
            if(node->public_ranges) {
                const std::u32string character(1,ch);
                for(const auto& [lo,hi]:*node->public_ranges)if(lo<=character && character<=hi){inside=true;break;}
            } else for (const auto [low, high] : node->items) if (low <= ch && ch <= high) { inside = true; break; }
            if (node->negate ? !inside : inside) result.insert(position + 1);
        }
        break;
    case RegexNode::Kind::Anchor:
        if ((node->anchor == '^' && position == 0) || (node->anchor == '$' && position == static_cast<Integer>(text.size()))) result.insert(position);
        break;
    case RegexNode::Kind::Group:
        return regex_match_node(node->child, text, position);
    case RegexNode::Kind::Concat: {
        result.insert(position);
        for (const auto& child : node->children) {
            std::set<Integer> next;
            for (const auto current : result) {
                const auto reached = regex_match_node(child, text, current);
                next.insert(reached.begin(), reached.end());
            }
            result = std::move(next);
            if (result.empty()) break;
        }
        break;
    }
    case RegexNode::Kind::Alt:
        for (const auto& child : node->children) {
            const auto reached = regex_match_node(child, text, position);
            result.insert(reached.begin(), reached.end());
        }
        break;
    case RegexNode::Kind::Repeat:
        if (node->repeat == '?') {
            result.insert(position);
            const auto reached = regex_match_node(node->child, text, position);
            result.insert(reached.begin(), reached.end());
        } else if (node->repeat == '*') {
            return regex_closure(node->child, text, position);
        } else {
            for (const auto first : regex_match_node(node->child, text, position)) {
                const auto reached = regex_closure(node->child, text, first);
                result.insert(reached.begin(), reached.end());
            }
        }
        break;
    }
    return result;
}

std::filesystem::path normalized_mutex_path(const String& directory)
{
    std::error_code ec;
    auto path = ::cw::lib::encoding::path_from_utf8(directory);
    // マウントされたUserDataパスには対応する物理的なYadoディレクトリが
    // ない。OSのファイルロックには共通のネイティブパスが必要だが、見かけ上のマウ
    // ントルートに置くとUserData.dataの隣に平文のUserDataディ
    // レクトリが作られる。OSの一時領域には内容を明かさないハッシュだけを置く。
    if (const auto mounted =
            ::cardwirth::platform::resolve_mounted_user_path(path)) {
        if (const auto root =
                ::cardwirth::platform::resolve_mounted_user_path(".")) {
            const auto normalized = ::cw::lib::encoding::lower(
                ::cw::lib::encoding::path_to_utf8(mounted->lexically_normal()));
            std::uint64_t hash = 14695981039346656037ULL;
            for (const unsigned char byte : normalized) {
                hash ^= byte;
                hash *= 1099511628211ULL;
            }
            std::ostringstream name;
            name << std::hex << std::setfill('0') << std::setw(16) << hash
                 << ".lock";
            auto lock_root = std::filesystem::temp_directory_path(ec);
            if (ec) {
                ec.clear();
                lock_root = root->parent_path();
            }
            return lock_root / ".CardWirthPy_C-encrypted-locks" /
                   std::filesystem::u8path(name.str());
        }
    }
    path = std::filesystem::absolute(path, ec);
    if (ec) path = ::cw::lib::encoding::path_from_utf8(directory);
    path = path.lexically_normal();
#if defined(_WIN32)
    // ネイティブパスを狭い文字型へ変換せず、Pythonのnormcaseを再現する。Unicode
    // の小文字化により、非ASCII文字の大小文字違いでも同じ宿のミューテックスを使う。
    return ::cw::lib::encoding::path_from_utf8(
        ::cw::lib::encoding::lower(::cw::lib::encoding::path_to_utf8(path))) /
        L".CardWirthPy.lock";
#else
    return path / ".CardWirthPy.lock";
#endif
}

std::shared_ptr<::cw::lib::Bitmap> clone_bitmap(const std::shared_ptr<::cw::lib::Bitmap>& source)
{
    if (!source) return {};
    auto result = std::make_shared<::cw::lib::Bitmap>(*source);
    return copy_animated_wxbmp_info(source,result);
}

void cleanup_mutex_directory(const std::filesystem::path& lock_path)
{
    const auto directory = lock_path.parent_path();
    if (directory.filename() != ".CardWirthPy-locks" &&
        directory.filename() != ".CardWirthPy_C-encrypted-locks") return;
    std::error_code ec;
    if (std::filesystem::is_directory(directory, ec) &&
        std::filesystem::is_empty(directory, ec))
        std::filesystem::remove(directory, ec);
}

std::pair<Integer, Integer> value_pair(const ::cw::lib::Value& value)
{
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage()); object && *object) {
        if (const auto sequence = std::dynamic_pointer_cast<::cw::data::VariantSequence>(*object); sequence && sequence->m_values.size() >= 2)
            return {value_integer(sequence->m_values[0], 0), value_integer(sequence->m_values[1], 0)};
    }
    return {0, 0};
}

std::tuple<Integer, Integer, Integer, Integer> value_rect(const ::cw::lib::Value& value)
{
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage()); object && *object) {
        if (const auto sequence = std::dynamic_pointer_cast<::cw::data::VariantSequence>(*object); sequence && sequence->m_values.size() >= 4)
            return {value_integer(sequence->m_values[0], 0), value_integer(sequence->m_values[1], 0), value_integer(sequence->m_values[2], 0), value_integer(sequence->m_values[3], 0)};
    }
    return {0, 0, 0, 0};
}

void blit_rgba(const ::cw::lib::Surface& source, ::cw::lib::Surface& destination, Integer dx, Integer dy)
{
    if (source.size.width <= 0 || source.size.height <= 0 || destination.size.width <= 0 || destination.size.height <= 0) return;
    const auto source_stride = static_cast<std::size_t>(source.size.width) * 4U;
    const auto destination_stride = static_cast<std::size_t>(destination.size.width) * 4U;
    for (Integer y = 0; y < source.size.height; ++y) {
        const Integer ty = dy + y;
        if (ty < 0 || ty >= destination.size.height) continue;
        for (Integer x = 0; x < source.size.width; ++x) {
            const Integer tx = dx + x;
            if (tx < 0 || tx >= destination.size.width) continue;
            const auto si = static_cast<std::size_t>(y) * source_stride + static_cast<std::size_t>(x) * 4U;
            const auto di = static_cast<std::size_t>(ty) * destination_stride + static_cast<std::size_t>(tx) * 4U;
            if (si + 3 >= source.rgba.size() || di + 3 >= destination.rgba.size()) continue;
            std::copy_n(source.rgba.begin() + static_cast<std::ptrdiff_t>(si), 4, destination.rgba.begin() + static_cast<std::ptrdiff_t>(di));
        }
    }
}

void alpha_blit_rgba(const ::cw::lib::Surface& source, ::cw::lib::Surface& destination,
                     Integer dx, Integer dy, bool use_mask = true, bool use_surface_alpha = true)
{
    if (source.size.width <= 0 || source.size.height <= 0 ||
        destination.size.width <= 0 || destination.size.height <= 0) return;
    for (Integer y = 0; y < source.size.height; ++y) {
        const Integer ty = dy + y;
        if (ty < 0 || ty >= destination.size.height) continue;
        for (Integer x = 0; x < source.size.width; ++x) {
            const Integer tx = dx + x;
            if (tx < 0 || tx >= destination.size.width) continue;
            const auto si = static_cast<std::size_t>((y * source.size.width + x) * 4);
            const auto di = static_cast<std::size_t>((ty * destination.size.width + tx) * 4);
            if (si + 3 >= source.rgba.size() || di + 3 >= destination.rgba.size()) continue;
            // RGBAを格納していても、アルファチャンネルを参照するとは限らない。pygame
            // 1のSRCALPHAはそのチャンネルだけを使い、RGBサーフェスは透過色
            // キーと全体の不透明度を使う。wxのDrawBitmap(False)はビッ
            // トマップのマスクを無視するが、実際のアルファチャンネルによる合成は行う。
            unsigned alpha = source.has_pixel_alpha ? source.rgba[si + 3] : 255U;
            if (!source.has_pixel_alpha) {
                if (use_mask && source.has_colorkey &&
                    source.rgba[si] == std::get<0>(source.colorkey) &&
                    source.rgba[si + 1] == std::get<1>(source.colorkey) &&
                    source.rgba[si + 2] == std::get<2>(source.colorkey)) alpha = 0;
                if (use_surface_alpha && source.surface_alpha_enabled)
                    alpha = alpha * source.surface_alpha / 255U;
            }
            const auto inverse = 255U - alpha;
            for (std::size_t channel = 0; channel < 3; ++channel)
                destination.rgba[di + channel] = static_cast<std::uint8_t>(
                    (static_cast<unsigned>(source.rgba[si + channel]) * alpha +
                     static_cast<unsigned>(destination.rgba[di + channel]) * inverse + 127U) / 255U);
            destination.rgba[di + 3] = static_cast<std::uint8_t>(
                std::min(255U, alpha + static_cast<unsigned>(destination.rgba[di + 3]) * inverse / 255U));
        }
    }
}

std::shared_ptr<::cw::lib::Bitmap> scaled_bitmap(const ::cw::lib::Surface& source,
                                                  Integer width, Integer height)
{
    width = std::max<Integer>(0, width);
    height = std::max<Integer>(0, height);
    auto result = std::make_shared<::cw::lib::Bitmap>();
    result->size = {width, height};
    result->rgba.assign(static_cast<std::size_t>(width * height * 4), 0);
    if (source.size.width <= 0 || source.size.height <= 0 || source.rgba.empty()) return result;
    for (Integer y = 0; y < height; ++y) for (Integer x = 0; x < width; ++x) {
        const Integer sx = std::min(source.size.width - 1, x * source.size.width / std::max<Integer>(1, width));
        const Integer sy = std::min(source.size.height - 1, y * source.size.height / std::max<Integer>(1, height));
        const auto si = static_cast<std::size_t>((sy * source.size.width + sx) * 4);
        const auto di = static_cast<std::size_t>((y * width + x) * 4);
        if (si + 3 < source.rgba.size() && di + 3 < result->rgba.size())
            std::copy_n(source.rgba.begin() + static_cast<std::ptrdiff_t>(si), 4,
                        result->rgba.begin() + static_cast<std::ptrdiff_t>(di));
    }
    return result;
}

std::shared_ptr<::cw::lib::Surface> value_surface(const ::cw::lib::Value& value)
{
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    return object && *object ? std::dynamic_pointer_cast<::cw::lib::Surface>(*object) : nullptr;
}

Integer tile_origin(Integer control, Integer size)
{
    // Pythonと同じ正の剰余を求める。余りにサイズを足す際のオーバーフローを避ける。
    const auto remainder = control % size;
    return remainder < 0 ? -(remainder + size) : -remainder;
}

void draw_dc_axis_line(::cw::lib::Bitmap& canvas, Integer x1, Integer y1,
                       Integer x2, Integer y2, ::cw::lib::Color color)
{
    // 軸に平行なwxのMemoryDCの線は、逆向きの線分も含め、小さ
    // い側の座標を含み、大きい側を含まない。走査範囲は既存のDCビット
    // マップ内に制限し、描画によって選択済み画像のサイズを変えない。
    const auto paint = [&](Integer x, Integer y) {
        const auto i = static_cast<std::size_t>((y * canvas.size.width + x) * 4);
        if (i + 3 >= canvas.rgba.size()) return;
        canvas.rgba[i] = color.r; canvas.rgba[i + 1] = color.g;
        canvas.rgba[i + 2] = color.b; canvas.rgba[i + 3] = color.a;
    };
    if (x1 == x2 && 0 <= x1 && x1 < canvas.size.width) {
        const auto begin = std::max<Integer>(0, std::min(y1, y2));
        const auto end = std::min(canvas.size.height, std::max(y1, y2));
        for (Integer y = begin; y < end; ++y) {
            paint(x1, y);
        }
    } else if (y1 == y2 && 0 <= y1 && y1 < canvas.size.height) {
        const auto begin = std::max<Integer>(0, std::min(x1, x2));
        const auto end = std::min(canvas.size.width, std::max(x1, x2));
        for (Integer x = begin; x < end; ++x) {
            paint(x, y1);
        }
    }
}

struct ZipEntryInfo {
    String name;
    std::size_t central_index{};
    std::uint16_t flags{};
    std::uint16_t dos_date{};
    std::uint16_t dos_time{};
};

class ZipArchiveObject final : public ::cw::lib::Object {
public:
    String path;
    std::vector<ZipEntryInfo> entries;
    std::shared_ptr<::cw::lib::archive::Reader> reader;
    bool lha{};
};

std::uint16_t read_le16(const Bytes& data, std::size_t offset)
{
    if (offset + 2 > data.size()) throw std::runtime_error("archive: truncated 16-bit field");
    return static_cast<std::uint16_t>(data[offset] | (static_cast<std::uint16_t>(data[offset + 1]) << 8U));
}

std::uint32_t read_le32(const Bytes& data, std::size_t offset)
{
    if (offset + 4 > data.size()) throw std::runtime_error("archive: truncated 32-bit field");
    return static_cast<std::uint32_t>(data[offset]) | (static_cast<std::uint32_t>(data[offset + 1]) << 8U) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16U) | (static_cast<std::uint32_t>(data[offset + 3]) << 24U);
}

void append_le16(Bytes& data, std::uint16_t value)
{
    data.push_back(static_cast<std::uint8_t>(value & 0xffU));
    data.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
}

void append_le32(Bytes& data, std::uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) data.push_back(static_cast<std::uint8_t>((value >> (8U * i)) & 0xffU));
}

std::uint32_t crc32_bytes(const Bytes& data)
{
    std::uint32_t crc = 0xffffffffU;
    for (const auto byte : data) {
        crc ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1U) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

Bytes read_binary_file(const String& path)
{
    Bytes data;
    if (!read_binary_path(std::filesystem::u8path(path), data))
        throw std::runtime_error("archive: cannot read " + path);
    return data;
}

std::shared_ptr<ZipArchiveObject> parse_zip_archive(const String& path)
{
    auto archive = std::make_shared<ZipArchiveObject>();
    archive->path = path;
    // Pythonと同様に、一覧取得と各項目の読み込みで同じ開いたアーカイ
    // ブを使う。独立したZIP32解析では共通デコーダへ渡る前にLHAを拒
    // 否し、ZIP64を切り詰めるうえ、項目ごとに入力を開き直してしまう。
    archive->reader = std::make_shared<::cw::lib::archive::Reader>(
        ::cw::lib::archive::Reader::open(std::filesystem::u8path(path), ::cw::lib::archive::ReaderProfile::EngineScenario));
    archive->lha = archive->reader->format() == ::cw::lib::archive::Format::Lha;
    for (const auto& source : archive->reader->entries()) {
        ZipEntryInfo entry;
        entry.central_index = archive->entries.size();
        entry.name = source.name;
        entry.flags = 0x800; // Readerで旧形式の名前はUTF-8へ復号済み。
        entry.dos_date = source.zip_dos_date;
        entry.dos_time = source.zip_dos_time;
        archive->entries.push_back(std::move(entry));
    }
    return archive;
}

Bytes read_zip_entry(const ZipArchiveObject& archive, const ZipEntryInfo& entry)
{
    // 共通のReaderは無圧縮項目にも対応する。その
    // 検証失敗を、検査の弱い別デコーダで無視しない。
    try {
        if (!archive.reader) throw std::runtime_error("archive: not opened for reading");
        const auto& reader = *archive.reader;
        const auto& entries = reader.entries();
        if (entry.central_index >= entries.size())
            throw std::runtime_error("archive: central entry index is out of range");
        // Pyのdecompress_zipはread(ZipInfo)ではなくread(zname)
        // を呼ぶ。ZIP/LHA内に同名項目がある場合、最初の走査時点から最後のレコードを読む。
        const auto decoded = reader.read(entries[entry.central_index].name);
        return Bytes(reinterpret_cast<const std::uint8_t*>(decoded.data()),
                     reinterpret_cast<const std::uint8_t*>(decoded.data() + decoded.size()));
    } catch (const ::cw::lib::archive::ArchiveError& error) {
        ::cardwirth::debug::log("read_zip_entry shared reader failed: path=" +
                                archive.path + " entry=" + entry.name +
                                " error=" + error.what());
        throw;
    }
}

void remove_obsolete_archive_files(const std::vector<::cw::lib::archive::Entry>& entries,
    const std::filesystem::path& destination,
    std::size_t retained_entry_count = std::numeric_limits<std::size_t>::max())
{
    // Pythonのdecompress_zipはシナリオのファイル一式を置き換える。汎用の上書
    // きが既存内容への追加・更新となるReader::extractには、この処理を入れない。
    std::set<String> retained;
    const auto retained_count = std::min(retained_entry_count, entries.size());
    for (std::size_t i = 0; i < retained_count; ++i) {
        const auto& entry = entries[i];
        if (entry.directory) continue;
        if (!::cw::lib::archive::is_safe_relative_path(entry.name)) continue;
        auto name = entry.name;
        std::replace(name.begin(), name.end(), '\\', '/');
        retained.insert(::cw::util::get_keypath(::cw::lib::encoding::path_to_utf8(
            destination / std::filesystem::u8path(name))));
    }
    std::vector<std::filesystem::path> stale;
    const bool mounted = ::cardwirth::platform::is_mounted_user_path(destination);
    // Pyでは空またはキャンセルされたZIP/LHAの展開先が作られないことがあ
    // る。展開先がない場合は空の一覧とし、実際の一覧取得エラーはそのまま伝える。
    if (mounted) {
        const auto status = ::cardwirth::platform::stat_mounted_user_path(destination, true);
        if (!status || !status->exists()) return;
    } else {
        std::error_code error;
        if (!std::filesystem::exists(destination, error) && !error) return;
    }
    const auto consider = [&](const std::filesystem::path& file) {
        if (!retained.contains(::cw::util::get_keypath(::cw::lib::encoding::path_to_utf8(file))))
            stale.push_back(file);
    };
    if (mounted) {
        std::vector<std::filesystem::path> directories{destination};
        while (!directories.empty()) {
            const auto directory = directories.back(); directories.pop_back();
            const auto entries = ::cardwirth::platform::list_mounted_user_path(directory, true);
            if (!entries) throw std::runtime_error("archive update: cannot list destination");
            for (const auto& entry : *entries) {
                const auto name = std::filesystem::u8path(entry.name);
                if (name.empty() || name.has_root_path() || name != name.filename() || name == "." || name == "..")
                    throw std::runtime_error("archive update: invalid storage entry name");
                const auto child = directory / name;
                if (entry.stat.is_directory()) directories.push_back(child);
                else if (entry.stat.is_file()) consider(child);
            }
        }
    } else {
        ::cw::lib::archive::validate_destination_root(destination);
        for (const auto& entry : std::filesystem::recursive_directory_iterator(destination))
            if (entry.is_regular_file()) consider(entry.path());
    }
    // 削除より前に一覧の取得を成功させる。ディレクトリを削
    // 除せず、明示的に選択した展開ルートの外にも触れない。
    for (const auto& file : stale)
        ::cw::util::remove(::cw::lib::encoding::path_to_utf8(file), false);
}

::cw::lib::archive::ExtractOptions unbounded_extract_options()
{
    ::cw::lib::archive::ExtractOptions options;
    // Pyのシナリオ導入には展開件数・単一ファイルサイズ・総量の明示的な上限
    // がない。汎用のEditor既定値とデコーダのメモリ確保上限は維持する。
    options.max_file_size = std::numeric_limits<std::uint64_t>::max();
    options.max_total_size = std::numeric_limits<std::uint64_t>::max();
    options.max_files = std::numeric_limits<std::size_t>::max();
    return options;
}

void displace_archive_output(const std::filesystem::path& output)
{
    const bool mounted = ::cardwirth::platform::is_mounted_user_path(output);
    const auto status = mounted ? ::cardwirth::platform::stat_mounted_user_path(output, true) : std::nullopt;
    if (!(mounted ? status && status->is_file() : std::filesystem::is_regular_file(output))) return;
    // g_tempdir_initは暗号化セッションを含むEngineの論理一時ルート。Py
    // の固定パスData/Temp/Globalを使うと、そのルートの外へ書き出してしまう。
    const auto deleted = std::filesystem::u8path(::cw::g_tempdir_init) / "Deleted";
    if (::cardwirth::platform::is_mounted_user_path(deleted)) {
        if (!::cardwirth::platform::make_mounted_user_directory(deleted, true))
            throw std::runtime_error("archive overwrite: cannot create Deleted");
    } else std::filesystem::create_directories(deleted);
    const auto displaced = dupcheck_plus(::cw::lib::encoding::path_to_utf8(deleted / output.filename()), false, false);
    ::cw::util::rename_file(::cw::lib::encoding::path_to_utf8(output), displaced, false);
}

String extract_archive_with_progress(const ::cw::lib::archive::Reader& reader,
    const String& destination, bool overwrite,
    const std::optional<::cw::lib::Callback>& startup,
    const std::optional<::cw::lib::Callback>& progress,
    bool report_completion = true, const std::function<void()>& close_archive = {})
{
    const auto entries = reader.entries();
    if (startup) (*startup)({::cw::lib::Value(static_cast<Integer>(entries.size()))}, {});
    auto options = unbounded_extract_options();
    options.overwrite = overwrite;
    options.skip_unsafe_paths = reader.format() == ::cw::lib::archive::Format::Zip ||
                                reader.format() == ::cw::lib::archive::Format::Lha;
    options.local_zip_time = true;
    options.lazy_destination = true;
    options.recover_overwrite = displace_archive_output;
    options.reuse_matching_time = overwrite && reader.format() != ::cw::lib::archive::Format::Cab;
    std::optional<std::size_t> cancelled_at;
    if (progress) options.cancel_before_entry = [&](std::size_t index) {
        // Pythonは最初の項目の前と、10項目ごとに進捗を通知する。
        const bool cancel = index % 10 == 0 && value_truthy(
            (*progress)({::cw::lib::Value(static_cast<Integer>(index))}, {}));
        if (cancel) cancelled_at = index;
        return cancel;
    };
    const auto result = reader.extract(std::filesystem::u8path(destination), options);
    // Pyは正常完了やキャンセルでは後始末の前に閉じるが、項目の読み込み
    // やコールバック中の例外では、渡されたReaderを開いたまま残す。
    if (close_archive) close_archive();
    if (result.empty()) {
        // このキャンセル処理で削除できるのは、新しく作った展
        // 開ディレクトリだけ。既存の上書き先は削除しない。
        if (!overwrite) {
            ::cw::util::remove(destination, false);
            return {};
        }
        // PythonのZIP/LHA上書きループはキャンセルで抜けても
        // 、処理済み項目の一覧を反映し、完了を通知して展開先を返す。この
        // 特殊だが保存結果に影響する挙動を保つ。Reader::extract
        // 自体は用途に依存させず、呼び出し元のデータを削除しない。
        if (cancelled_at) {
            remove_obsolete_archive_files(entries, std::filesystem::u8path(destination), *cancelled_at);
            if (progress && report_completion)
                (*progress)({::cw::lib::Value(static_cast<Integer>(entries.size()))}, {});
            return destination;
        }
        return {};
    }
    if (overwrite) remove_obsolete_archive_files(entries, std::filesystem::u8path(destination));
    if (progress && report_completion)
        (*progress)({::cw::lib::Value(static_cast<Integer>(entries.size()))}, {});
    return destination;
}

double cabinet_process_time()
{
#if defined(_WIN32)
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
        throw std::system_error(GetLastError(), std::system_category(), "CAB process time");
    const auto ticks = [](FILETIME value) { return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32) | value.dwLowDateTime; };
    return static_cast<double>(ticks(kernel) + ticks(user)) / 10000000.0;
#else
    return static_cast<double>(std::clock()) / CLOCKS_PER_SEC;
#endif
}

Integer count_cabinet_output_files(const std::filesystem::path& root)
{
    Integer count = 0;
    if (::cardwirth::platform::is_mounted_user_path(root)) {
        std::vector<std::filesystem::path> pending{root};
        while (!pending.empty()) {
            const auto directory = pending.back(); pending.pop_back();
            const auto entries = ::cardwirth::platform::list_mounted_user_path(directory, true);
            if (!entries) continue;
            for (const auto& entry : *entries) {
                const auto name = std::filesystem::u8path(entry.name);
                if (name.empty() || name.has_root_path() || name != name.filename() || name == "." || name == "..")
                    throw std::runtime_error("CAB progress: invalid storage entry name");
                if (entry.stat.is_file()) ++count;
                else if (entry.stat.is_directory() && !entry.stat.directory_link) pending.push_back(directory / name);
            }
        }
    } else {
        std::error_code error;
        for (std::filesystem::recursive_directory_iterator it(root, error), end; it != end && !error; it.increment(error))
            if (it->is_regular_file(error)) ++count;
    }
    return count;
}

String extract_cabinet_with_progress(const ::cw::lib::archive::Reader& reader,
    const String& destination, bool overwrite, const std::optional<::cw::lib::Callback>& progress)
{
    auto options = unbounded_extract_options(); options.overwrite = overwrite;
    std::atomic<bool> cancelled{false}, finished{false};
    ::cw::lib::ExceptionPtr failure;
    std::filesystem::path result;
    options.cancel_before_entry = [&](std::size_t) { return cancelled.load(); };
    const auto run = [&] {
        try { result = reader.extract(std::filesystem::u8path(destination), options); }
        catch (...) { failure = ::cw::lib::capture_exception(); }
        finished.store(true);
    };
    if (progress) {
        std::thread worker(run);
        try {
            Integer previous = 0;
            while (!finished.load()) {
                const auto count = count_cabinet_output_files(std::filesystem::u8path(destination));
                if (count != previous) {
                    previous = count;
                    if (value_truthy((*progress)({::cw::lib::Value(count)}, {}))) cancelled.store(true);
                }
                const auto until = cabinet_process_time() + 0.1;
                while (!finished.load() && cabinet_process_time() < until)
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            worker.join();
        } catch (...) {
            // Pythonはコールバックの例外後もワーカーを動かしたままにする場合がある。C++
            // では捕捉したReader・オプション・ローカルデータが失効する前に終了を待つ。
            cancelled.store(true); worker.join(); throw;
        }
    } else run();
    if (failure) ::cw::lib::rethrow_exception(failure);
    if (cancelled.load() && !overwrite) { ::cw::util::remove(destination, false); return {}; }
    // PyはCABの検査・開始より前に古い全ファイルを移す。ネイティ
    // ブ版は展開成功まで保持し、その後で一覧から外れた古いファイルを
    // 削除する。アーカイブや開始処理の失敗で既存の導入内容を失わない
    // ため。そのため、ロックされた既存項目は事前に移されず置き換え
    // に失敗する場合があり、ロック時の失敗挙動は同一とは限らない。
    if (overwrite) remove_obsolete_archive_files(reader.entries(), std::filesystem::u8path(destination));
    return result.empty() && !overwrite ? String{} : destination;
}

String decode_archive_bytes(const Bytes& bytes)
{
    if (bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf)
        return String(reinterpret_cast<const char*>(bytes.data() + 3), bytes.size() - 3);
    return String(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

std::tuple<Integer, Integer> utf8_next(const String& text, std::size_t& offset)
{
    if (offset >= text.size()) return {0, 0};
    const auto first = static_cast<unsigned char>(text[offset]);
    if (first < 0x80U) { ++offset; return {first, 1}; }
    unsigned width = (first & 0xe0U) == 0xc0U ? 2U : ((first & 0xf0U) == 0xe0U ? 3U : 4U);
    if (offset + width > text.size()) { ++offset; return {0xfffd, 1}; }
    Integer code = first & ((1U << (8U - width - 1U)) - 1U);
    for (unsigned i = 1; i < width; ++i) {
        const auto byte = static_cast<unsigned char>(text[offset + i]);
        if ((byte & 0xc0U) != 0x80U) { ++offset; return {0xfffd, 1}; }
        code = (code << 6U) | (byte & 0x3fU);
    }
    offset += width;
    return {code, static_cast<Integer>(width)};
}

String utf8_codepoint(Integer code)
{
    String result;
    if (code < 0x80) result.push_back(static_cast<char>(code));
    else if (code < 0x800) { result.push_back(static_cast<char>(0xc0 | (code >> 6))); result.push_back(static_cast<char>(0x80 | (code & 0x3f))); }
    else if (code < 0x10000) { result.push_back(static_cast<char>(0xe0 | (code >> 12))); result.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f))); result.push_back(static_cast<char>(0x80 | (code & 0x3f))); }
    else { result.push_back(static_cast<char>(0xf0 | (code >> 18))); result.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3f))); result.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f))); result.push_back(static_cast<char>(0x80 | (code & 0x3f))); }
    return result;
}

Integer codepoint_width(Integer code)
{
    return ::cw::lib::python_unicode_width::width(static_cast<std::uint32_t>(code));
}

bool is_scaled_filename(const String& filename)
{
    static const std::regex scaled(R"(\.x(?:2|4|8|16)\.[^.]+$)", std::regex::icase);
    return std::regex_search(filename, scaled);
}

String scaled_image_name(const String& path, Integer scale)
{
    // Pyは先頭がドットのパスも含め、cw.util.splitextでパス全体を分割する。ファイ
    // ル名だけにfilesystem::stem/extensionを使うと、この扱いが変わる。
    const auto [stem, ext] = splitext(path);
    return stem + ".x" + std::to_string(scale) + ext;
}

std::filesystem::path scaled_image_path(const std::filesystem::path& path, Integer scale)
{
    return std::filesystem::u8path(scaled_image_name(::cw::lib::encoding::path_to_utf8(path), scale));
}

std::uint32_t be32(const Bytes& bytes, std::size_t offset)
{
    if (offset + 4 > bytes.size()) return 0;
    return (static_cast<std::uint32_t>(bytes[offset]) << 24) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
           static_cast<std::uint32_t>(bytes[offset + 3]);
}

std::uint32_t le32(const Bytes& bytes, std::size_t offset)
{
    if (offset + 4 > bytes.size()) return 0;
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

std::uint16_t le16(const Bytes& bytes, std::size_t offset)
{
    if (offset + 2 > bytes.size()) return 0;
    return static_cast<std::uint16_t>(bytes[offset]) |
           static_cast<std::uint16_t>(bytes[offset + 1] << 8);
}

std::uint16_t be16(const Bytes& bytes, std::size_t offset);

std::tuple<Integer, Integer> image_size_from_bytes(const Bytes& bytes)
{
    // PNGとAPNGはIHDRヘッダを共有する。
    if (bytes.size() >= 24 && std::equal(bytes.begin(), bytes.begin() + 8,
                                         std::array<std::uint8_t, 8>{0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a}.begin())) {
        return {static_cast<Integer>(be32(bytes, 16)), static_cast<Integer>(be32(bytes, 20))};
    }
    if (bytes.size() >= 26 && bytes[0] == 'B' && bytes[1] == 'M') {
        // BITMAPCOREHEADERの寸法はINFOと異なり、符号なし16ビット整数。
        if (le32(bytes, 14) == 12)
            return {static_cast<Integer>(le16(bytes, 18)), static_cast<Integer>(le16(bytes, 20))};
        const auto width = static_cast<std::int32_t>(le32(bytes, 18));
        const auto height = static_cast<std::int32_t>(le32(bytes, 22));
        return {std::abs(static_cast<Integer>(width)), std::abs(static_cast<Integer>(height))};
    }
    if (bytes.size() >= 10 && bytes[0] == 'G' && bytes[1] == 'I' && bytes[2] == 'F') {
        return {static_cast<Integer>(le16(bytes, 6)), static_cast<Integer>(le16(bytes, 8))};
    }
    // RIFF/WEBPのVP8Xは寸法から1を引いた値を24ビットのリトルエンディアンで格納する。
    if (bytes.size() >= 30 && bytes[0] == 'R' && bytes[1] == 'I' && bytes[2] == 'F' && bytes[3] == 'F' &&
        bytes[8] == 'W' && bytes[9] == 'E' && bytes[10] == 'B' && bytes[11] == 'P' &&
        bytes[12] == 'V' && bytes[13] == 'P' && bytes[14] == '8' && bytes[15] == 'X') {
        const auto width = static_cast<Integer>(bytes[24]) | (static_cast<Integer>(bytes[25]) << 8) |
                            (static_cast<Integer>(bytes[26]) << 16);
        const auto height = static_cast<Integer>(bytes[27]) | (static_cast<Integer>(bytes[28]) << 8) |
                             (static_cast<Integer>(bytes[29]) << 16);
        return {width + 1, height + 1};
    }
    // JPEGには寸法の固定位置がないため、Pillowと同様にSOFマーカーを走査する。
    if (bytes.size() > 4 && bytes[0] == 0xff && bytes[1] == 0xd8) {
        std::size_t i = 2;
        while (i + 8 < bytes.size()) {
            if (bytes[i] != 0xff) { ++i; continue; }
            while (i < bytes.size() && bytes[i] == 0xff) ++i;
            if (i >= bytes.size()) break;
            const auto marker = bytes[i++];
            if (marker == 0xd8 || marker == 0xd9 || (marker >= 0xd0 && marker <= 0xd7)) continue;
            if (i + 2 > bytes.size()) break;
            const auto length = static_cast<std::size_t>(be16(bytes, i));
            if (length < 2 || i + length > bytes.size()) break;
            if ((marker >= 0xc0 && marker <= 0xc3) || (marker >= 0xc5 && marker <= 0xc7) ||
                (marker >= 0xc9 && marker <= 0xcb) || (marker >= 0xcd && marker <= 0xcf)) {
                return {static_cast<Integer>(be16(bytes, i + 5)), static_cast<Integer>(be16(bytes, i + 3))};
            }
            i += length;
        }
    }
    return {0, 0};
}

// JPEGはビッグエンディアンの16ビットフィールドを使う。不正なファイルは単に(0,
// 0)を返し、Pythonのload_imageの例外時の代替処理と一致させる。
std::uint16_t be16(const Bytes& bytes, std::size_t offset)
{
    if (offset + 2 > bytes.size()) return 0;
    return static_cast<std::uint16_t>((bytes[offset] << 8) | bytes[offset + 1]);
}

std::shared_ptr<Surface> surface_from_bytes(const Bytes& bytes)
{
    // Pyの内部のPillow補助処理と同様に、復号失敗はload_image
    // へ伝える。寸法だけでは復号済み画素の代わりにならない。
    const auto decoded = ::cw::lib::imagecodec::decode_rgba(
        std::span<const std::uint8_t>(bytes.data(), bytes.size()), 0);
    auto surface = std::make_shared<Surface>();
    surface->size = {decoded.width, decoded.height};
    surface->rgba = decoded.pixels;
    return surface;
}

String image_info_path(const std::shared_ptr<::cw::image::ImageInfo>& info)
{
    return info ? info->m_path : String{};
}

std::shared_ptr<::cw::image::ImageInfo> make_image_info(const std::filesystem::path& path)
{
    auto info = std::make_shared<::cw::image::ImageInfo>();
    // create.path_to_imageinfo()は選択可能な顔画像にCenterを設定する。
    info->initialize(::cw::lib::encoding::path_to_utf8(path), 0, std::nullopt, "Center", std::nullopt);
    return info;
}

using FaceKey = std::optional<std::tuple<Integer, String, String>>;
using FaceMap = std::map<FaceKey, std::vector<std::vector<std::shared_ptr<::cw::image::ImageInfo>>>>;

bool face_variants_enabled()
{
    return ::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_face_variants;
}

void collect_facepaths(const String& facedir, FaceMap& imgpaths,
                       const std::vector<std::tuple<Integer, String, String>>& dpaths,
                       std::set<String>& passed, const String& base_dir)
{
    (void)facedir;
    for (const auto& [weight, showpath, directory] : dpaths) {
        const auto root = std::filesystem::u8path(directory);
        std::error_code ec;
        if (!std::filesystem::is_directory(root, ec)) continue;
        const auto key = get_keypath(get_symlinktarget(directory));
        if (!passed.insert(key).second) continue;
        std::vector<std::vector<std::shared_ptr<::cw::image::ImageInfo>>> seq;
        std::map<String, std::vector<std::shared_ptr<::cw::image::ImageInfo>>> bases, variants;
        std::vector<String> base_order;
        std::vector<std::tuple<Integer, String, String>> children;
        for (std::filesystem::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
            const auto source = ::cw::lib::encoding::path_to_utf8(it->path());
            const auto target = get_linktarget(source);
            const auto path = std::filesystem::u8path(target);
            std::error_code status_error;
            if (std::filesystem::is_regular_file(path, status_error)) {
                if (!is_image_file(path) || is_scaled_filename(target)) continue;
                auto info = make_image_info(path);
                if (!face_variants_enabled()) { seq.push_back({info}); continue; }
                // Pyは親フォルダのドットを含む、拡張子を除いたパ
                // ス全体を分割する。このグループ分けを変えない。
                const auto stem = std::get<0>(splitext(target));
                const auto dot = stem.find('.');
                const auto base = stem.substr(0, dot);
                if (dot != String::npos) variants[base].push_back(info);
                else {
                    if (!bases.contains(base)) base_order.push_back(base);
                    bases[base] = {info};
                }
            } else if (std::filesystem::is_directory(path, status_error)) {
                auto shown = join_paths({::cw::lib::Value(showpath), ::cw::lib::Value(::cw::lib::encoding::path_to_utf8(it->path().filename()))});
                if (source != target && shown.size() >= 4 && lower_ascii(shown.substr(shown.size() - 4)) == ".lnk")
                    shown = std::get<0>(splitext(shown));
                children.emplace_back(weight, shown, target);
            }
        }
        if (face_variants_enabled()) for (const auto& base : base_order) {
            auto group = bases[base];
            const auto found = variants.find(base);
            if (found != variants.end()) group.insert(group.end(), found->second.begin(), found->second.end());
            seq.push_back(std::move(group));
        }
        if (!seq.empty()) {
            auto relative = join_paths({::cw::lib::Value(relpath(directory, base_dir))});
            if (relative.rfind("../", 0) == 0) relative = join_paths({::cw::lib::Value(directory)});
            imgpaths[std::make_tuple(weight, showpath, relative)] = std::move(seq);
        }
        collect_facepaths(base_dir, imgpaths, children, passed, base_dir);
    }
}

}  // namespace

void bind_device_context(std::shared_ptr<::cw::lib::DeviceContext> dc,
                         std::shared_ptr<::cw::lib::Bitmap> target)
{
    if (!dc) throw std::invalid_argument("bind_device_context requires a device context");
    std::lock_guard<std::mutex> guard(g_ui_state_mutex);
    g_device_context_states[dc.get()].target = std::move(target);
}

std::shared_ptr<::cw::lib::Bitmap> get_device_context_target(
    const std::shared_ptr<::cw::lib::DeviceContext>& dc)
{
    if (!dc) return {};
    std::lock_guard<std::mutex> guard(g_ui_state_mutex);
    const auto it = g_device_context_states.find(dc.get());
    return it == g_device_context_states.end() ? nullptr : it->second.target;
}

void set_window_state(std::shared_ptr<::cw::lib::Window> window, WindowState state)
{
    if (!window) throw std::invalid_argument("set_window_state requires a window");
    std::lock_guard<std::mutex> guard(g_ui_state_mutex);
    g_window_states[window.get()] = std::move(state);
}

std::optional<WindowState> get_window_state(const std::shared_ptr<::cw::lib::Window>& window)
{
    if (!window) return std::nullopt;
    std::lock_guard<std::mutex> guard(g_ui_state_mutex);
    const auto it = g_window_states.find(window.get());
    return it == g_window_states.end() ? std::nullopt : std::optional<WindowState>(it->second);
}
void Alt::initialize(::cw::lib::Value arg_branches)
{
    m_branches = std::move(arg_branches);
}

void Anchor::initialize(::cw::lib::String arg_kind)
{
    m_kind = std::move(arg_kind);
}

void CWPyRichTextCtrl::initialize(std::shared_ptr<::cw::lib::Panel> arg_parent, ::cw::lib::Integer arg_wid, ::cw::lib::String arg_text, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_size, ::cw::lib::Integer arg_style, bool arg_searchmenu)
{
    m_parent=arg_parent?::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(arg_parent)): ::cw::lib::Value{};
    parent_window=arg_parent;native_handle=arg_parent?arg_parent->native_handle:0;id=arg_wid;
    bounds.width=std::get<0>(arg_size);bounds.height=std::get<1>(arg_size);
    if (const auto self=weak_from_this().lock()) {
        auto own=get_window_state(self).value_or(WindowState{});own.parent=arg_parent;set_window_state(self,std::move(own));
        if(arg_parent){auto state=get_window_state(arg_parent).value_or(WindowState{});state.children.emplace_back(self);set_window_state(arg_parent,std::move(state));}
    }
    m_text_typed = std::move(arg_text);
    m_newline_appended_typed=false;
    (void)arg_size;
    (void)arg_style;
    if (arg_searchmenu && storage_file_exists(std::filesystem::u8path("Data/SearchEngines.xml"))) {
        try {
            if (!CWPyRichTextCtrl::s__search_engines) {
                CWPyRichTextCtrl::s__search_engines = std::vector<std::tuple<String, String, Integer>>{};
                const auto root = ::cw::data::xml2element("Data/SearchEngines.xml", {}, std::nullopt, std::nullopt, true, std::nullopt);
                Integer menu_id = 10000;
                for (const auto& child : root ? root->m_children : std::vector<std::shared_ptr<::cw::data::CWPyElement>>{}) {
                    if (!child || child->m_tag != "SearchEngine") continue;
                    const auto url = child->get("url", {});
                    const auto name = child->m_text;
                    if (!url.empty() && !name.empty()) CWPyRichTextCtrl::s__search_engines->emplace_back(url, name, menu_id++);
                }
            }
            for (const auto& [url, name, menu_id] : *CWPyRichTextCtrl::s__search_engines) {
                auto engine = std::make_shared<CWPyRichTextCtrl_SearchEngine>();
                engine->initialize(weak_from_this().lock(), url, name, menu_id);
                m_search_engines.emplace_back(std::static_pointer_cast<::cw::lib::Object>(engine));
            }
        } catch (...) {
            print_ex(std::optional<::cw::lib::Value>(::cw::lib::Value("Data/SearchEngines.xml")));
        }
    }
}

void CWPyRichTextCtrl::set_text(::cw::lib::Bytes arg_value, bool arg_linkurl)
{
    m_text_typed = decode_text(arg_value);
    // Pyは置き換え用テキストを書き込む前にwxのコントロールを消去する。
    // 元の選択範囲が新しく復号した文字列のバイトを選択しないようにする。
    m_selection_start_typed = m_selection_end_typed = 0;
    m_scroll_line_typed = 0; // Pyは最後にShowPosition(0)を実行する。
    m_newline_appended_typed=false;
    // Pyは消去前に入力を復号するが、書き込むのはlinkurlがtrueの場合
    // だけ。falseではコントロールが空になる挙動を保つ。共通のReadme
    // 側はリンクを明示的に有効にし、通常のTextは本文の状態を自ら保持する。
    if (!arg_linkurl) {
        m_text_typed.clear();
        m_url_segments_typed.clear();
        return;
    }
    if (arg_linkurl) {
        // wxのRichTextCtrl.WriteTextはCRLFと単独のCRをLF
        // へ正規化する。ここでも同じ規則を使い、全ネイティブホストと表示処理で共有する。
        String normalized;
        normalized.reserve(m_text_typed.size());
        for (std::size_t i=0;i<m_text_typed.size();++i) {
            if (m_text_typed[i]=='\r') {
                normalized.push_back('\n');
                if (i+1<m_text_typed.size() && m_text_typed[i+1]=='\n') ++i;
            } else normalized.push_back(m_text_typed[i]);
        }
        m_text_typed=std::move(normalized);
    }
    m_url_segments_typed = CWPyRichTextCtrl_set_text_get_urls(m_text_typed);
    // PythonはURL装飾が有効で末尾が改行で
    // ない場合、改行を追加する。wxのURLキャレ
    // ット状態を後続のテキストへ持ち越さないため。
    if (arg_linkurl && !m_text_typed.empty() && m_text_typed.back() != '\n' && m_text_typed.back() != '\r') {
        m_text_typed.push_back('\n');
        m_newline_appended_typed=true;
    }
}

void CWPyRichTextCtrl::OnMouseWheel(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (!arg_event || has_modalchild(arg_event)) return;
    const Integer rotation = get_wheelrotation(arg_event);
    m_scroll_line_typed = std::max<Integer>(0, m_scroll_line_typed +
        ::cw::dialog::richtext_wheel_scroll_delta(rotation));
}

void CWPyRichTextCtrl::OnMotion(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (!arg_event) return;
    const auto delta = ::cw::dialog::richtext_drag_scroll_delta(
        arg_event->bounds.y, bounds.height);
    if (delta) m_scroll_line_typed = std::max<Integer>(0, m_scroll_line_typed + delta);
}

void CWPyRichTextCtrl::OnContextMenu(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (!arg_event) return;
    const auto selection = selected_text();
    const bool selected = !selection.empty();
    arg_event->visible = selected || !m_search_engines.empty();
    arg_event->title = selection;
}

::cw::lib::String CWPyRichTextCtrl::selected_text() const
{
    // 表示側はUTF-8のバイト位置を保持するが、wxはUnicode
    // 文字全体を選択する。古い位置や外部からの位置を含め、
    // 全選択処理で当たり判定と同じ直前の文字境界を使う。
    const auto boundary = [&](Integer position) {
        auto offset = static_cast<std::size_t>(std::clamp<Integer>(
            position, 0, static_cast<Integer>(m_text_typed.size())));
        while (offset && offset < m_text_typed.size() &&
               (static_cast<unsigned char>(m_text_typed[offset]) & 0xc0U) == 0x80U) --offset;
        return offset;
    };
    const auto begin = boundary(m_selection_start_typed);
    const auto end = std::max(begin, boundary(m_selection_end_typed));
    return m_text_typed.substr(begin, end - begin);
}

void CWPyRichTextCtrl::OnCopy(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    const auto selection = selected_text();
    if (!selection.empty()) to_clipboard(selection);
}

void CWPyRichTextCtrl::OnSelectAll(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    m_selection_start_typed = 0;
    m_selection_end_typed = static_cast<Integer>(m_text_typed.size());
}

namespace {
std::shared_ptr<::cw::lib::Window> rich_text_url_parent(CWPyRichTextCtrl& control)
{
    auto self = control.weak_from_this().lock();
    // スタック上のテスト用オブジェクトには共有所有者がないため、移植可能なWindowのコンテキストだけ
    // をコピーする。製品のコントロールはキューに積んだshared_ptrで同じオブジェクトを保持する。
    return self ? std::static_pointer_cast<::cw::lib::Window>(self)
                : std::make_shared<::cw::lib::Window>(static_cast<const ::cw::lib::Window&>(control));
}

void rich_text_go_url(CWPyRichTextCtrl& control, ::cw::lib::String url,
                      std::optional<OpenUrlServices> services)
{
    auto parent = rich_text_url_parent(control);
    if (services)
        open_url_with_services(std::move(parent), std::move(url), std::move(*services));
    else
        open_url(std::move(parent), std::move(url));
}
}

void CWPyRichTextCtrl::go_url(::cw::lib::String arg_url)
{
    rich_text_go_url(*this, std::move(arg_url), std::nullopt);
}

void CWPyRichTextCtrl::go_url_with_services(::cw::lib::String arg_url,
                                             OpenUrlServices arg_services)
{
    rich_text_go_url(*this, std::move(arg_url), std::move(arg_services));
}

// Pythonは空URLでもブラウザへの移動を要求できるが、ここで
// は抑制する。空のイベントにはリンク先がなく、ブラウザの既定ペー
// ジを開くべきではない。テキスト選択中は引き続き移動を抑制する。
void CWPyRichTextCtrl::OnURL(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (!arg_event || !selected_text().empty()) return;
    if (!arg_event->title.empty()) go_url(arg_event->title);
}

void CWPyStaticBitmap::initialize(std::shared_ptr<::cw::lib::Panel> arg_parent, ::cw::lib::Integer arg_cid, std::vector<std::shared_ptr<::cw::lib::Bitmap>> arg_bmps, std::vector<std::shared_ptr<::cw::lib::Bitmap>> arg_bmps_bmpdepthkey, std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> arg_size, std::optional<std::vector<std::shared_ptr<::cw::image::ImageInfo>>> arg_infos, std::optional<::cw::lib::Callback> arg_ss)
{
    m_bmps_typed = std::move(arg_bmps);
    m_bmps_bmpdepthkey_typed = std::move(arg_bmps_bmpdepthkey);
    m_size_typed = arg_size;
    m_infos_typed = arg_infos.value_or(std::vector<std::shared_ptr<::cw::image::ImageInfo>>{});
    m__ss = arg_ss.value_or(::cw::lib::Callback{});
    m_bmps = std::static_pointer_cast<::cw::lib::Object>(std::make_shared<::cw::data::VariantSequence>());
    if (const auto object = std::get_if<::cw::lib::ObjectPtr>(&m_bmps.storage()); object && *object) {
        const auto sequence = std::dynamic_pointer_cast<::cw::data::VariantSequence>(*object);
        for (const auto& bitmap : m_bmps_typed)
            sequence->m_values.emplace_back(bitmap ? std::static_pointer_cast<::cw::lib::Object>(bitmap) : nullptr);
    }
    m_bmps_bmpdepthkey = std::static_pointer_cast<::cw::lib::Object>(std::make_shared<::cw::data::VariantSequence>());
    if (const auto object = std::get_if<::cw::lib::ObjectPtr>(&m_bmps_bmpdepthkey.storage()); object && *object) {
        const auto sequence = std::dynamic_pointer_cast<::cw::data::VariantSequence>(*object);
        for (const auto& bitmap : m_bmps_bmpdepthkey_typed)
            sequence->m_values.emplace_back(bitmap ? std::static_pointer_cast<::cw::lib::Object>(bitmap) : nullptr);
    }
    m_infos = std::static_pointer_cast<::cw::lib::Object>(std::make_shared<::cw::data::VariantSequence>());
    if (const auto object = std::get_if<::cw::lib::ObjectPtr>(&m_infos.storage()); object && *object) {
        const auto sequence = std::dynamic_pointer_cast<::cw::data::VariantSequence>(*object);
        for (const auto& info : m_infos_typed)
            sequence->m_values.emplace_back(info ? std::static_pointer_cast<::cw::lib::Object>(info) : nullptr);
    }
    (void)arg_cid;
    if (arg_parent && m_size_typed) {
        const auto [width, height] = *m_size_typed;
        arg_parent->bounds.width = std::max<Integer>(0, width);
        arg_parent->bounds.height = std::max<Integer>(0, height);
    }
}

void CWPyStaticBitmap::_bind()
{
    // 生成されたWindow型にはイベントテーブルがない。OnPaintは呼び出
    // し可能なメソッドとして公開し、SDLやネイティブのアダプタから登録できる。
}

void CWPyStaticBitmap::OnPaint(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (m__ss) (void)m__ss({}, {});
    auto state = get_window_state(arg_event);
    if (!state || !state->target) return;
    auto dest = state->target;
    const Integer width = dest->size.width;
    const Integer height = dest->size.height;
    if (width <= 0 || height <= 0) return;
    dest->rgba.assign(static_cast<std::size_t>(width * height * 4), 255);
    for (std::size_t i = 3; i < dest->rgba.size(); i += 4) dest->rgba[i] = 255;
    if (const auto painted = render_surface(dest)) static_cast<::cw::lib::Surface&>(*dest) = *painted;
}

std::shared_ptr<::cw::lib::Surface> CWPyStaticBitmap::render_surface(
    const std::shared_ptr<::cw::lib::Surface>& background, ::cw::lib::Real position_scale)
{
    if (!background) return {};
    auto dest = std::make_shared<::cw::lib::Bitmap>();
    static_cast<::cw::lib::Surface&>(*dest) = *background;
    const auto count = std::min(m_bmps_typed.size(), m_bmps_bmpdepthkey_typed.size());
    for (std::size_t i = 0; i < count; ++i) {
        auto bmp = resolve_animated_wxbmp(m_bmps_typed[i]);
        auto key = resolve_animated_wxbmp(m_bmps_bmpdepthkey_typed[i]);
        if (!bmp) continue;
        Integer x = 0;
        Integer y = 0;
        if (i < m_infos_typed.size() && m_infos_typed[i] && key) {
            Integer w = key->size.width;
            Integer h = key->size.height;
            const auto scale = key->scr_scale > 0.0 ? key->scr_scale : 1.0;
            w = static_cast<Integer>(w / scale);
            h = static_cast<Integer>(h / scale);
            const auto baserect = m_infos_typed[i]->calc_basecardposition_wx({w, h}, true, "LargeCard", "NotCard");
            x = static_cast<Integer>(std::lround(baserect.x * position_scale));
            y = static_cast<Integer>(std::lround(baserect.y * position_scale));
        }
        ::cw::imageretouch::wxblit_2bitbmp_to_card({}, dest, bmp, x, y, true, key);
        ::cw::lib::ui::inherit_surface_animation(dest, bmp);
    }
    return dest;
}

void CWPyStaticBitmap::SetBitmap(std::vector<std::shared_ptr<::cw::lib::Bitmap>> arg_bmps, std::vector<std::shared_ptr<::cw::lib::Bitmap>> arg_bmps_bmpdepthkey, std::optional<std::vector<std::shared_ptr<::cw::image::ImageInfo>>> arg_infos)
{
    initialize(nullptr, 0, std::move(arg_bmps), std::move(arg_bmps_bmpdepthkey), m_size_typed, std::move(arg_infos), m__ss);
}

std::vector<std::shared_ptr<::cw::lib::Bitmap>> CWPyStaticBitmap::GetBitmap()
{
    return m_bmps_typed;
}

// Pythonは呼び出し元のリストを参照で保持する。ネイティブ版は範囲タプル
// を所有するvectorで受け取り、ノード自身のVariantSequence
// へ保持するため、入力vectorがスコープを抜けても範囲は残る。その後で
// 呼び出し元のvectorを変更しても、この文字クラスには反映されない。
void CharClass::initialize(bool arg_negate, std::vector<std::tuple<::cw::lib::String, ::cw::lib::String>> arg_items)
{
    m_negate = arg_negate;
    auto values = std::make_shared<::cw::data::VariantSequence>();
    for (const auto& [low, high] : arg_items) {
        auto pair = std::make_shared<::cw::data::VariantSequence>();
        pair->m_values.emplace_back(low);
        pair->m_values.emplace_back(high);
        values->m_values.emplace_back(std::static_pointer_cast<::cw::lib::Object>(pair));
    }
    m_items = std::static_pointer_cast<::cw::lib::Object>(values);
}

void Concat::initialize(::cw::lib::Value arg_nodes)
{
    m_nodes = std::move(arg_nodes);
}

void Depth1Surface::initialize(std::shared_ptr<::cw::lib::Surface> arg_surface, ::cw::lib::Real arg_scr_scale, ::cw::lib::Integer arg_bmpdepth)
{
    m_surface_typed = arg_surface ? std::make_shared<::cw::lib::Surface>(*arg_surface) : nullptr;
    m_scr_scale = arg_scr_scale;
    m_bmpdepthis1 = arg_surface && arg_surface->depth1
        ? arg_surface->bmpdepthis1 : arg_bmpdepth == 1;
    if (m_surface_typed) {
        // Pythonは新しいSurfaceを確保し、BLEND_RGBA_ADD
        // で画素をコピーする。この構築では元画像の全体不透明度を引き継がない。
        m_surface_typed->surface_alpha = 255;
        m_surface_typed->surface_alpha_enabled = false;
        m_surface_typed->scr_scale = m_surface_typed->logical_scale = m_scr_scale;
        m_surface_typed->bmpdepthis1 = m_bmpdepthis1;
        m_surface_typed->depth1 = true;
    }
}

std::shared_ptr<::cw::lib::Surface> Depth1Surface::copy()
{
    if (!m_surface_typed) return {};
    auto result = std::make_shared<::cw::lib::Surface>(*m_surface_typed);
    // Pythonのcopy()は、画素のコピーから別のDepth1Surfaceを構築する。
    result->surface_alpha = 255;
    result->surface_alpha_enabled = false;
    result->scr_scale = result->logical_scale = m_scr_scale;
    result->bmpdepthis1 = m_bmpdepthis1;
    result->depth1 = true;
    return result;
}

std::shared_ptr<::cw::lib::Surface> Depth1Surface::convert_alpha(std::optional<std::shared_ptr<::cw::lib::Surface>> arg_surface)
{
    // pygameが変換するのは呼び出し先自身であり、省略可能なサーフェス引数は
    // 変換先の画素形式だけを選ぶ。ネイティブ側の格納形式は共通のRGBAのみ。
    (void)arg_surface;
    auto result = ::cw::lib::convert_surface_alpha(m_surface_typed);
    if (!result) return {};
    result->depth1 = true;
    result->scr_scale = m_scr_scale;
    result->logical_scale = m_scr_scale;
    return result;
}

FileSync::~FileSync()
{
    quit();
}

void FileSync::initialize()
{
    m_mutex_typed = std::make_shared<::cw::lib::Mutex>();
    m__mutex = ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(m_mutex_typed));
    m__quit = false;
    m_quit_typed.store(false, std::memory_order_release);
    m__files.clear();
    m_inflight_typed = 0;
    m_write_error_typed = nullptr;
    if (!m_started_typed) {
        m_started_typed = true;
        worker = std::thread([this] { run(); });
    }
}

void FileSync::quit()
{
    {
        // run()は待機に入る前に、同じミューテックスを保持してこの条件を
        // 調べる。アトミックなフラグだけでは、条件判定と待機登録の間にnotify_all
        // が入り、ネイティブ側の終了待ちが止まるのを防げない。
        std::unique_lock<::cw::lib::Mutex> lock;
        if (m_mutex_typed) lock = std::unique_lock<::cw::lib::Mutex>(*m_mutex_typed);
        m__quit = true;
        m_quit_typed.store(true, std::memory_order_release);
    }
    m_files_changed_typed.notify_all();
    {
        std::lock_guard<std::mutex> joining(m_join_typed);
        if (worker.joinable() && worker.get_id() != std::this_thread::get_id()) worker.join();
        m_started_typed = false;
    }
    // 呼び出し元がinitialize()を呼ばずにFileSyncを作る場合も、
    // キューの書き込みを完了させる。フラグだけを変えるPythonのquit()と
    // 異なり、ネイティブ版の終了処理は永続化の完了を待ってからワーカーを破棄する。
    _write_files();
}

void FileSync::push(::cw::lib::String arg_file, ::cw::lib::Value arg_data, ::cw::lib::String arg_mode, std::optional<::cw::lib::String> arg_encoding)
{
    validate_write_mode(arg_mode);
    if (!m_mutex_typed) initialize();
    {
        std::lock_guard<::cw::lib::Mutex> guard(*m_mutex_typed);
        m__files.emplace_back(std::move(arg_file), std::move(arg_data), std::move(arg_mode), std::move(arg_encoding));
    }
    m_files_changed_typed.notify_one();
    if (m_quit_typed.load(std::memory_order_acquire)) {
        // Pythonはquit後に追加された処理を呼び出し元が消化する前に、ワーカーの終了を待つ。
        // 待たないと、実行中のワーカーと呼び出し元が同じキュー先頭を書き込んだり取り除いたりする。
        {
            std::lock_guard<std::mutex> joining(m_join_typed);
            if (worker.joinable() && worker.get_id() != std::this_thread::get_id()) worker.join();
        }
        _write_files();
    }
}

void FileSync::sync()
{
#if defined(CARDWIRTH_LOG_ENABLED)
    const auto sync_started = std::chrono::steady_clock::now();
#endif
    // ワーカーは条件変数を通じてキューと処理中件数の変化
    // を通知する。Pythonの動作中のワーカーと同様に
    // 、任意の時間制限を設けず完了を待つ。ネイティブ版
    // はquit後も待ち、上記の永続保存の保証を保つ。
    if (!m_mutex_typed) initialize();
    std::unique_lock<::cw::lib::Mutex> lock(*m_mutex_typed);
    m_files_changed_typed.wait(lock, [this] {
        return m__files.empty() && m_inflight_typed == 0;
    });
    auto write_error = m_write_error_typed;
    m_write_error_typed = nullptr;
    lock.unlock();
    // Pythonのバックグラウンドだけでの書き込み失敗と異なり、ネイティブ版は待
    // 機中の呼び出し元へ失敗を通知し、保存成功として処理を続けられないようにする。
    if (write_error) ::cw::lib::rethrow_exception(write_error);
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log_slow(
        "io.filesync_sync", std::chrono::steady_clock::now() - sync_started,
        std::string("quit=") + (m__quit ? "1" : "0"));
#endif
}

bool FileSync::is_waiting(::cw::lib::String arg_file)
{
    (void)arg_file;
    return true;
}

void FileSync::replace_native_file(const std::filesystem::path& from, const std::filesystem::path& to, std::error_code& error)
{
#if defined(_WIN32)
    if (!MoveFileExW(from.wstring().c_str(),to.wstring().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        error=std::error_code(static_cast<int>(GetLastError()),std::system_category());
#else
    std::filesystem::rename(from,to,error);
#endif
}

void FileSync::validate_write_mode(const ::cw::lib::String& mode)
{
    // ネイティブ版の意図的な仕様。呼び出し元は通常の上書き書き込みを使う
    // 。この書き込み処理がPythonと同じ意味を実装していないモード
    // で無条件に切り詰めないよう、追記・排他作成・読み込み・更新や不正
    // なモードは、ネイティブ側やマウント側の対象へ触れる前に拒否する。
    if (mode != "w" && mode != "wb" && mode != "bw" && mode != "wt" && mode != "tw")
        throw std::invalid_argument("write_file: unsupported write mode: " + mode);
}

void FileSync::flush_native_file(const std::filesystem::path& path)
{
    // 異なるのはOSの永続化用基本操作だけ。キュー経由の書き込みと名前
    // 変更の代替処理は、ストリームを閉じた後に同じ永続化手順を使う。
#if defined(_WIN32)
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "open for flush");
    const bool flushed = FlushFileBuffers(handle) != 0;
    const auto flush_error = flushed ? ERROR_SUCCESS : GetLastError();
    const bool closed = CloseHandle(handle) != 0;
    const auto close_error = closed ? ERROR_SUCCESS : GetLastError();
    if (!flushed || !closed)
        throw std::system_error(static_cast<int>(!flushed ? flush_error : close_error), std::system_category(), "flush/close");
#else
    const int fd = ::open(path.c_str(), O_RDWR);
    if (fd < 0) throw std::system_error(errno, std::generic_category(), "open for fsync");
    const int flushed = ::fsync(fd);
    const int flush_error = errno;
    const int closed = ::close(fd);
    const int close_error = errno;
    if (flushed != 0 || closed != 0)
        throw std::system_error(flushed != 0 ? flush_error : close_error, std::generic_category(), "fsync/close");
#endif
}

// Pythonと異なり、キュー経由の書き込みは一時ファイルを開く前に
// 不足する親ディレクトリを作る。新規の保存ディレクトリでキュー経由・
// 直接の両方の書き込みを可能にする。置き換えは全書き込みと永続化の完
// 了後にのみ行い、親が既存の非ディレクトリなら引き続きエラーとする。
void FileSync::_write_files()
{
    std::lock_guard<std::mutex> writer(m_writer_typed);
    while (true) {
        ::cw::lib::String file;
        ::cw::lib::Value data;
        ::cw::lib::String mode;
        std::optional<::cw::lib::String> encoding;
        {
            if (m_mutex_typed) {
                std::lock_guard<::cw::lib::Mutex> guard(*m_mutex_typed);
                if (m__files.empty()) break;
                std::tie(file, data, mode, encoding) = m__files.front();
                ++m_inflight_typed;
            } else {
                if (m__files.empty()) break;
                std::tie(file, data, mode, encoding) = m__files.front();
                ++m_inflight_typed;
            }
        }
        std::filesystem::path temp;
        bool created_temp = false;
        try {
            // ネイティブパスの変換やstatはopen()より前に例外を投げ
            // る場合がある。その準備処理も書き込みと同じ失敗処理に含め、キュ
            // ー先頭と処理中件数を残したままワーカーが終了しないようにする。
            temp = std::filesystem::u8path(file + ".cardwirthpy_temp");
            for (Integer i = 2; std::filesystem::exists(temp); ++i)
                temp = std::filesystem::u8path(file + ".cardwirthpy_temp(" + std::to_string(i) + ")");
            validate_write_mode(mode);
            validate_file_encoding(mode, encoding);
            const auto destination = std::filesystem::u8path(file);
            if (!destination.parent_path().empty()) std::filesystem::create_directories(destination.parent_path());
            std::ofstream stream(temp, std::ios::binary | std::ios::trunc);
            created_temp = stream.is_open();
            stream.exceptions(std::ios::badbit | std::ios::failbit);
            if (!stream) throw std::runtime_error("FileSync: cannot open " + ::cw::lib::encoding::path_to_utf8(temp));
            validate_file_payload(data, mode);
            if (const auto* bytes = std::get_if<Bytes>(&data.storage())) {
                stream.write(reinterpret_cast<const char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
            } else if (const auto* text = std::get_if<String>(&data.storage())) {
                const auto encoded = encode_file_text(*text, encoding, mode.find('b') == String::npos);
                stream.write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
            } else {
                throw std::invalid_argument("FileSync: expected String or Bytes");
            }
            stream.flush();
            stream.close();
            flush_native_file(temp);
            std::error_code replace_error;
            if (m_native_replace) m_native_replace(temp,destination,replace_error);
            else replace_native_file(temp,destination,replace_error);
            if (replace_error) throw std::filesystem::filesystem_error("FileSync replace",temp,destination,replace_error);
        } catch (...) {
            const auto error = ::cw::lib::capture_exception();
            try { ::cw::lib::rethrow_exception(error); }
            catch (const std::exception& detail) { std::cerr << "FileSync: " << detail.what() << '\n'; }
            catch (...) { std::cerr << "FileSync: unknown queued write failure\n"; }
            if (m_mutex_typed) {
                std::lock_guard<::cw::lib::Mutex> guard(*m_mutex_typed);
                if (!m_write_error_typed) m_write_error_typed = error;
            } else if (!m_write_error_typed) {
                m_write_error_typed = error;
            }
            // 存在確認に失敗したパスでも、他の処理のファイルを指している可能
            // 性がある。今回の書き込みで開いた一時ファイルだけを後始末する。
            if (created_temp) {
                std::error_code ec;
                std::filesystem::remove(temp, ec);
            }
        }
        {
            if (m_mutex_typed) {
                std::lock_guard<::cw::lib::Mutex> guard(*m_mutex_typed);
                if (!m__files.empty()) m__files.erase(m__files.begin());
                if (m_inflight_typed > 0) --m_inflight_typed;
            } else if (!m__files.empty()) {
                m__files.erase(m__files.begin());
                if (m_inflight_typed > 0) --m_inflight_typed;
            }
        }
        m_files_changed_typed.notify_all();
    }
}

void FileSync::run()
{
    while (!m_quit_typed.load(std::memory_order_acquire)) {
        if (m_mutex_typed) {
            std::unique_lock<::cw::lib::Mutex> lock(*m_mutex_typed);
            m_files_changed_typed.wait(lock, [this] {
                return m_quit_typed.load(std::memory_order_acquire) || !m__files.empty();
            });
        }
        _write_files();
    }
    _write_files();
}

void Group::initialize(::cw::lib::Value arg_node)
{
    m_node = std::move(arg_node);
}

void Lit::initialize(::cw::lib::String arg_ch)
{
    m_ch = std::move(arg_ch);
}

void MusicInterface::initialize(::cw::lib::Integer arg_channel, ::cw::lib::Integer arg_mastervolume)
{
    m_channel_typed = arg_channel;
    m_mastervolume_typed = arg_mastervolume;
    m_subvolume_typed = 100;
    m_loopcount_typed = 0;
    m_volume_typed = 0.0;
    m_path_typed.clear();
    m_fpath_typed.clear();
    // 再初期化ではPythonの__init__と同じ属性の初期化を行う
    // 。型付きパスが空のときは、XMLの利用側もこれらの公開属性を読む。
    m_channel = arg_channel;
    m_path.clear();
    m_fpath.clear();
    m_loopcount = 0;
    m_inusecard = false;
    m__movie.reset();
    m_movie_scr.reset();
    m_inusecard_typed = false;
    m_winmm_typed = false;
    m_bass_typed = false;
    m__winmm = false;
    m__bass = false;
    m_playing_typed = false;
    m_paused_typed = false;
    m_mastervolume = arg_mastervolume;
    m_subvolume = 100;
}

void MusicInterface::update_scale()
{
    // 動画再生は任意の機能。SDL_mixerのトラックは表示サ
    // ーフェスを持たない。m_movieを保持してPythonの
    // 属性との対応を保ち、動画の拡大縮小は描画側が別途管理する。
}

void MusicInterface::play(::cw::lib::String arg_path, bool arg_updatepredata, bool arg_restart, bool arg_inusecard, ::cw::lib::Integer arg_subvolume, ::cw::lib::Integer arg_loopcount, ::cw::lib::Integer arg_fade, ::cw::lib::String arg_fullpath)
{
    _play(std::move(arg_path), arg_updatepredata, arg_restart, arg_inusecard,
          arg_subvolume, arg_loopcount, arg_fade, std::move(arg_fullpath));
}

void MusicInterface::_play(::cw::lib::String arg_path, bool arg_updatepredata, bool arg_restart, bool arg_inusecard, ::cw::lib::Integer arg_subvolume, ::cw::lib::Integer arg_loopcount, ::cw::lib::Integer arg_fade, ::cw::lib::String arg_fullpath)
{
    // Pythonは音楽を解決したり再生を試みたりする前に宿を変更済みにする。失
    // 敗する要求やupdatepredata=falseでの復元呼び出しも含む。
    if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata &&
        ::cw::g_cwpy->is_playingscenario())
        (*::cw::g_cwpy->m_ydata)->changed();
    const bool preserve_paused = m_paused_typed;
    if (!arg_updatepredata) {
        arg_subvolume = m_subvolume_typed;
        arg_loopcount = m_loopcount_typed;
    }
    // get_pathはカード由来の情報を設定するため、先に元のパスを解決する
    // 。復元したfullpathは、その検索結果が空の場合だけ代替として使う。
    auto resolved = get_path(arg_path, arg_inusecard);
    if (resolved.empty()) resolved = arg_fullpath;
#if defined(__EMSCRIPTEN__)
    SDL_Log("CardWirthAudio BGM request: path=%s resolved=%s loopcount=%lld",
            arg_path.c_str(), resolved.c_str(), static_cast<long long>(arg_loopcount));
#endif
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log("MusicInterface play request: path=" + arg_path + " resolved=" + resolved);
#endif
    const auto resolved_path = std::filesystem::u8path(resolved);
    const bool mounted_audio = ::cardwirth::platform::is_mounted_user_path(resolved_path);
    const bool missing = resolved.empty() || !storage_file_exists(resolved_path);
    if (missing) {
#if defined(__EMSCRIPTEN__)
        SDL_Log("CardWirthAudio BGM file not found: path=%s", resolved.c_str());
#endif
#if defined(CARDWIRTH_LOG_ENABLED)
        ::cardwirth::debug::log("MusicInterface file not found: " + resolved);
#endif
        // Pythonは素材不在で再生を止めても、戦闘前のデータは消去しない
        // 。下の共通更新では、明示的に要求されたトラックを引き続き記録する。
        stop_impl(arg_fade, false, false);
    } else if (audio_disabled_for_test()) {
        if (arg_restart || m_fpath_typed != resolved) {
            stop_impl(arg_fade, false, false);
            m_fpath_typed = resolved;
            m_fpath = resolved;
            m_playing_typed = false;
        }
    } else if (arg_restart || m_fpath_typed != resolved) {
        // Pythonと同様に、新しいバックエンドを選ぶ前に
        // 以前のものを解放する。同じチャンネルでBASS、MCI
        // 、SDL_mixerを切り替える場合にも必要。
        stop_impl(arg_fade, false, false);
        bool native_midi_playing = false;
        bool playback_started = false;
        const bool midi_audio = is_midi(resolved);
#if CW_UTIL_HAS_PORTABLE_MIDI
        g_portable_music_midi_players.erase(this);
#if CW_UTIL_HAS_SDL_MIXER
        if (midi_audio) {
            if (const auto found = g_music_tracks.find(this); found != g_music_tracks.end()) {
                if (found->second.track) MIX_StopTrack(found->second.track, 0);
                if (found->second.track) MIX_DestroyTrack(found->second.track);
                if (found->second.audio) MIX_DestroyAudio(found->second.audio);
                g_music_tracks.erase(found);
            }
        }
#endif
#endif
#if defined(_WIN32)
        // Windowsでは固有の再生処理を使う。Pyと同じくBASSを優先し
        // 、その次にネイティブのMIDIデバイスを使う。下の移植可能な処理と暗
        // 号化メモリ用の代替処理も同じAPIを共有する。Pythonのload_bgm()
        // もMIDIを含む対応ファイルでBASSを優先し、BASSがなけ
        // ればpygame.mixer.musicからWindowsのMIDI
        // デバイスで再生する。ここではMCIでその動作を再現する。
        if (!playback_started && !mounted_audio && ::cw::bassplayer::is_alivablewithpath(resolved)) {
            const auto volume = _get_volumevalue(resolved) *
                static_cast<Real>(arg_subvolume) / 100.0;
            if (::cw::bassplayer::play_bgm(
                    resolved, volume, arg_loopcount, m_channel_typed, arg_fade)) {
                m_bass_typed = true;
                m__bass = true;
                playback_started = true;
            }
        }
        if (!mounted_audio && !playback_started && midi_audio) {
            native_midi_playing = play_mci_midi(resolved, m_channel_typed, arg_loopcount);
            m_winmm_typed = native_midi_playing;
            m__winmm = native_midi_playing;
            playback_started = native_midi_playing;
        }
#endif
#if CW_UTIL_HAS_PORTABLE_MIDI
        // TinySoundFontは他の全プラットフォームで使うMIDI
        // シンセサイザ。Windowsでは、Pythonにはない拡張
        // として、BASSとMCIで再生できなかった場合と、BASS/MCI
        // では開けない暗号化データベース内のMIDIに使う。
        if (midi_audio && !playback_started) {
            try {
                sdlmixer_init();
                auto player = std::make_unique<::cardwirth::explorer::MidiPlayer>(
                    mounted_audio ? std::filesystem::path{} : portable_midi_data_directory(resolved));
                const auto gain = static_cast<float>(std::clamp(
                    _get_volumevalue(resolved) * static_cast<Real>(arg_subvolume) / 100.0,
                    0.0, 1.0));
                const auto repeats = mixer_repeat_count(arg_loopcount);
                if (mounted_audio) {
                    const auto midi = ::cardwirth::platform::read_mounted_user_file(resolved_path);
                    const auto soundfont_path = ::cardwirth::platform::resolve_mounted_user_path(
                        std::filesystem::u8path(::cw::g_DEFAULT_SOUNDFONT));
                    const auto soundfont = soundfont_path
                        ? ::cardwirth::platform::read_mounted_user_file(*soundfont_path)
                        : std::nullopt;
                    if (!midi || !soundfont)
                        throw std::runtime_error("暗号化DB内のMIDIまたはSoundFontを読み込めません");
                    player->play_memory(*midi, *soundfont, repeats, gain, resolved,
                        ::cw::lib::encoding::path_to_utf8(*soundfont_path));
#if defined(CARDWIRTH_LOG_ENABLED)
                    ::cardwirth::debug::log("Mounted MIDI playback started: midi=" + resolved +
                        " soundfont=" + ::cw::lib::encoding::path_to_utf8(*soundfont_path));
#endif
                } else {
                    player->play(std::filesystem::u8path(resolved), repeats, gain);
                }
                g_portable_music_midi_players[this] = std::move(player);
                native_midi_playing = true;
                playback_started = true;
                SDL_Log("CardWirthAudio portable BGM MIDI started: path=%s repeats=%d gain=%.3f",
                        resolved.c_str(), repeats, static_cast<double>(gain));
            } catch (const std::exception& error) {
                SDL_Log("CardWirthAudio portable BGM MIDI failed: path=%s error=%s",
                        resolved.c_str(), error.what());
#if defined(CARDWIRTH_LOG_ENABLED)
                ::cardwirth::debug::log("Portable BGM MIDI failed: path=" + resolved +
                    " error=" + error.what());
#endif
            }
        }
#endif
#if CW_UTIL_HAS_SDL_MIXER
        // WindowsのBASSは任意。PythonはSDL_mixer
        // 優先設定が明示的に有効でなくても、OGG/MP3/WAV
        // をpygame/SDLへ切り替える。そのため
        // 、最初のMCI以外の要求時に遅延初期化する。
        if (!midi_audio && !playback_started && !g_sdl_mixer) sdlmixer_init();
#if defined(__EMSCRIPTEN__) || defined(__linux__)
        if (!midi_audio && !g_sdl_mixer && !playback_started)
            SDL_Log("CardWirthAudio BGM mixer unavailable: %s", SDL_GetError());
#endif
        if (g_sdl_mixer && !playback_started && !midi_audio) {
            auto& previous = g_music_tracks[this];
            if (previous.track) MIX_StopTrack(previous.track, 0);
            if (previous.track) MIX_DestroyTrack(previous.track);
            if (previous.audio) MIX_DestroyAudio(previous.audio);
            previous = {};
            if (mounted_audio) {
                if (const auto bytes = ::cardwirth::platform::read_mounted_user_file(resolved_path))
                    previous.mounted_data = *bytes;
                if (!previous.mounted_data.empty()) {
                    // ブラウザのUserDataはシーク可能なネイティ
                    // ブファイルではなく、IndexedDB由来のメモ
                    // リバッファとして提供される。マウントされた効果音
                    // と同様に、トラックを開始する前にSDLのメモリス
                    // トリームからBGMを復号する。音声コールバックま
                    // で復号を遅らせると、Webビルドで通知なく失敗し
                    // 、OGGのBGMがすべて無音になるおそれがある。
                    SDL_IOStream* stream = SDL_IOFromConstMem(
                        previous.mounted_data.data(), previous.mounted_data.size());
                    previous.audio = stream
                        ? MIX_LoadAudio_IO(g_sdl_mixer, stream, true, true)
                        : nullptr;
                }
            } else {
                previous.audio = MIX_LoadAudio(g_sdl_mixer, resolved.c_str(), false);
            }
            if (previous.audio) {
                previous.track = MIX_CreateTrack(g_sdl_mixer);
                if (!previous.track || !MIX_SetTrackAudio(previous.track, previous.audio)) {
#if defined(__EMSCRIPTEN__) || defined(__linux__)
                    SDL_Log("CardWirthAudio BGM track setup failed: path=%s error=%s",
                            resolved.c_str(), SDL_GetError());
#endif
                    if (previous.track) MIX_DestroyTrack(previous.track);
                    MIX_DestroyAudio(previous.audio);
                    previous = {};
                } else {
                    // SDL_mixerが数えるのは総再生回数ではなく繰り返し
                    // 回数。Pythonは新方式のMP3にloopcount
                    // - 1を適用する。他の形式で明示的な回数を指定できるネイ
                    // ティブ版の拡張は、このMP3の互換規則とともに保持する。
                    std::shared_ptr<::cw::data::SystemData> sdata;
                    if (::cw::g_cwpy) {
                        if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_sdata.storage());
                            object && *object)
                            sdata = std::dynamic_pointer_cast<::cw::data::SystemData>(*object);
                    }
                    auto extension = ::cw::lib::encoding::path_to_utf8(
                        std::filesystem::u8path(resolved).extension());
                    std::transform(extension.begin(), extension.end(), extension.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    // Pythonの1.28以前の互換動作ではSDLのMP3を1回再生
                    // する。SDL_mixerでは繰り返し0回が1回再生に相当する。
                    const bool legacy_mp3 = extension == ".mp3" && ::cw::g_cwpy &&
                        ::cw::g_cwpy->m_sct && sdata &&
                        ::cw::g_cwpy->m_sct->lessthan("1.28", sdata->get_versionhint(0));
                    const auto loops = legacy_mp3 ? 0 : arg_loopcount == 0
                        ? -1
                        : static_cast<int>(std::clamp<Integer>(arg_loopcount - 1, 0, std::numeric_limits<int>::max()));
                    // MIX_SetTrackLoopsが効くのは再生中のトラックだけで、
                    // MIX_PlayTrackで初期化される。開始時のプロパティにループ
                    // 回数を指定し、すべてのSDLホストでオープニングやBGMを繰り返す。
                    const auto options = SDL_CreateProperties();
                    const bool configured = options != 0 &&
                        SDL_SetNumberProperty(options, MIX_PROP_PLAY_LOOPS_NUMBER, loops);
                    const bool started = configured && MIX_PlayTrack(previous.track, options);
                    if (options) SDL_DestroyProperties(options);
                    if (!started) {
#if defined(__EMSCRIPTEN__) || defined(__linux__)
                        SDL_Log("CardWirthAudio BGM play failed: path=%s error=%s",
                                resolved.c_str(), SDL_GetError());
#endif
                        MIX_DestroyTrack(previous.track);
                        MIX_DestroyAudio(previous.audio);
                        previous = {};
                    } else {
                        playback_started = true;
#if defined(__EMSCRIPTEN__) || defined(__linux__)
                        SDL_Log("CardWirthAudio BGM started: path=%s loops=%d",
                                resolved.c_str(), loops);
#endif
                    }
                }
            } else {
#if defined(__EMSCRIPTEN__) || defined(__linux__)
                    SDL_Log("CardWirthAudio BGM load failed: path=%s error=%s",
                            resolved.c_str(), SDL_GetError());
#endif
            }
        }
#endif
        m_fpath_typed = resolved;
        m_fpath = resolved;
        m_playing_typed = playback_started;
        if (preserve_paused && playback_started) {
            // stop_impl()は古いトラックの一時停止フラグを解除する。新しい
            // トラックが鳴り続けないよう、置き換え先にもアプリの中断状態を適用する。
            m_paused_typed = false;
            pause();
        }
    } else {
#if defined(_WIN32)
        // Pythonがループ回数を更新するのは、既存の同じトラックへの要求だけ。
        // 新規・再開のBASSトラックにはplay_bgmでarg_loopcount
        // を渡しているため、再設定すると再生中の1回が加わりN+1回になる。
        if (m_bass_typed)
            ::cw::bassplayer::set_bgmloopcount(arg_loopcount, m_channel_typed);
#endif
    }
    if (!missing) {
        m_path_typed = arg_path;
        m_path = arg_path;
        // 由来の判定結果はget_pathが管理する。呼び出し元のフラグは判定結果ではない。
        m_subvolume_typed = arg_subvolume;
        m_subvolume = arg_subvolume;
        m_loopcount_typed = arg_loopcount;
        m_loopcount = arg_loopcount;
        set_volume(std::nullopt, arg_fade);
    }
    // 移植可能な音声処理の意図的な拡張。SDL、TinySoundFont、MCI
    // の代替処理は遅延初期化するため、Pyのpygame/BASS準備状態の事前検
    // 査だけでは全ネイティブバックエンドを判定できない。追加バックエンドが現在のホ
    // ストで再生を開始できなくても、復元対象として要求されたトラックを保持する。
    if (arg_updatepredata && ::cw::g_cwpy) {
        if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_sdata.storage()); object && *object) {
            if (const auto data = std::dynamic_pointer_cast<::cw::data::SystemData>(*object);
                data && data->m_pre_battleareadata &&
                std::get<3>(std::get<1>(*data->m_pre_battleareadata)) == m_channel_typed) {
                std::get<1>(*data->m_pre_battleareadata) =
                    {arg_path, arg_subvolume, arg_loopcount, m_channel_typed};
            }
        }
    }
}

void MusicInterface::stop(::cw::lib::Integer arg_fade)
{
    stop_impl(arg_fade, true, true);
}

void MusicInterface::stop_impl(::cw::lib::Integer arg_fade, bool arg_stopfadeout, bool arg_updatepredata)
{
#if CW_UTIL_HAS_PORTABLE_MIDI
    g_portable_music_midi_players.erase(this);
#endif
#if defined(_WIN32)
    // Pythonは_bassが既に解除されていてもこのBGMチャンネルを停止
    // する。バックエンドの切り替え後にフェードアウト中のストリームを残さない。
    if (::cw::bassplayer::is_alivable())
        ::cw::bassplayer::stop_bgm(m_channel_typed, arg_fade, arg_stopfadeout);
    m_bass_typed = false;
    m__bass = false;
    {
        std::scoped_lock lock(g_mci_music_mutex);
        close_mci_music(m_channel_typed);
    }
    m_winmm_typed = false;
    m__winmm = false;
#endif
#if CW_UTIL_HAS_SDL_MIXER
    if (arg_stopfadeout) finish_channel_fades(true, false, m_channel_typed);
    if (const auto found = g_music_tracks.find(this); found != g_music_tracks.end()) {
        retire_mixer_track(found->second, arg_fade, true, false, m_channel_typed);
        g_music_tracks.erase(found);
    }
#else
    (void)arg_fade;
#endif
    (void)arg_stopfadeout;
    (void)arg_updatepredata;
    m_playing_typed = false;
    // 停止や置き換えをまたいでアプリの中断状態を保つ。最小化中や
    // モバイルのバックグラウンドでもシナリオがBGMを変える場合
    // があり、新しいトラックも前面復帰の通知まで一時停止させる。
    m_path_typed.clear();
    m_fpath_typed.clear();
    m_path.clear();
    m_fpath.clear();
    m_volume_typed = 0.0;
    (void)arg_fade;
    if (arg_updatepredata && ::cw::g_cwpy) {
        if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_sdata.storage()); object && *object) {
            if (const auto data = std::dynamic_pointer_cast<::cw::data::SystemData>(*object);
                data && data->m_pre_battleareadata &&
                std::get<3>(std::get<1>(*data->m_pre_battleareadata)) == m_channel_typed) {
                std::get<1>(*data->m_pre_battleareadata) = {String{}, 100, 0, m_channel_typed};
            }
        }
    }
}

::cw::lib::Real MusicInterface::_get_volumevalue(::cw::lib::String arg_fpath)
{
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting) {
        const auto& setting = *::cw::g_cwpy->m_setting;
        if (!setting.m_play_bgm) return 0.0;
        const auto base = is_midi(arg_fpath) ? setting.m_vol_bgm_midi : setting.m_vol_bgm;
        return base * static_cast<Real>(m_mastervolume_typed) / 100.0;
    }
    // Pythonとの意図的な差異。ネイティブ版のヘッドレス初期化はSetting::initより前
    // に行えるため、その場合は最大音量を使う。Pythonは初期化済みのSettingを必要とする。
    return 1.0;
}

// PyはWindows以外のMPEG代替処理でpygame.movie
// にも通知する。ネイティブ版のMusicInterfaceにはこのバッ
// クエンドを作る処理がなく、m__movieは初期化時にリセットする互
// 換用の保持フィールドのみ。下の音量更新は実際のBASS/MCI、SDL
// ミキサー、移植可能なMIDIバックエンドへ届く。Movieの生成処理
// を追加する場合も同じ音量更新経路へ組み込み、以前の音量を残さない。
void MusicInterface::set_volume(std::optional<::cw::lib::Real> arg_volume, ::cw::lib::Integer arg_fade)
{
    // Pythonはここで音量だけをキューへ積むため、遅延呼び出しはfade=0を使う。
    if (defer_audio_to_game_thread(*this, [arg_volume](MusicInterface& self) {
            self.set_volume(arg_volume, 0);
        })) return;
    const auto base = arg_volume ? *arg_volume : _get_volumevalue(m_fpath_typed);
    // Editorが画面を使っている間、保持されたEngineの音を出さ
    // ない。ネイティブの一時停止だけではMIDIの発音が残る場合がある。
    // resume()で使うユーザーの主音量・副音量設定は変えず、一時
    // 停止中に届く音量変更も含め、全バックエンドの出力音量を抑制する。
    m_volume_typed = m_paused_typed ? 0.0
        : base * static_cast<Real>(m_subvolume_typed) / 100.0;
    if (m_paused_typed) arg_fade = 0;
#if defined(_WIN32)
    if (m_bass_typed)
        ::cw::bassplayer::set_bgmvolume(m_volume_typed, m_channel_typed, arg_fade);
    {
        std::scoped_lock lock(g_mci_music_mutex);
        if (const auto found = g_mci_music_aliases.find(m_channel_typed); found != g_mci_music_aliases.end()) {
            const auto alias = wide_from_utf8(found->second);
            const auto volume = static_cast<Integer>(std::llround(std::clamp(m_volume_typed, 0.0, 1.0) * 1000.0));
            mciSendStringW((L"setaudio " + alias + L" volume to " + std::to_wstring(volume)).c_str(), nullptr, 0, nullptr);
        }
    }
#endif
#if CW_UTIL_HAS_SDL_MIXER
    if (const auto found = g_music_tracks.find(this); found != g_music_tracks.end() && found->second.track)
        MIX_SetTrackGain(found->second.track, static_cast<float>(std::clamp(m_volume_typed, 0.0, 1.0)));
#endif
#if CW_UTIL_HAS_PORTABLE_MIDI
    if (const auto found = g_portable_music_midi_players.find(this);
        found != g_portable_music_midi_players.end())
        found->second->set_volume(static_cast<float>(
            std::clamp(m_volume_typed, 0.0, 1.0)));
#endif
    (void)arg_fade;
}

void collect_finished_audio_fades(bool force)
{
#if CW_UTIL_HAS_SDL_MIXER
    std::erase_if(g_fading_mixer_tracks, [force](auto& fade) {
        if (!force && fade.value.track &&
            (MIX_TrackPlaying(fade.value.track) || MIX_TrackPaused(fade.value.track))) return false;
        destroy_mixer_track(fade.value);
        return true;
    });
#else
    (void)force;
#endif
}

void set_sound_suspended(bool suspended)
{
    g_sound_suspended = suspended;
#if CW_UTIL_HAS_SDL_MIXER
    for (auto& fade : g_fading_mixer_tracks) if (fade.value.track) {
        if (suspended) MIX_PauseTrack(fade.value.track);
        else MIX_ResumeTrack(fade.value.track);
    }
    for (auto& [sound, value] : g_sound_tracks) if (value.track) {
        if (suspended) MIX_PauseTrack(value.track);
        else MIX_ResumeTrack(value.track);
    }
#endif
#if CW_UTIL_HAS_PORTABLE_MIDI
    for (auto& [sound, player] : g_portable_sound_midi_players) {
        if (suspended) player->pause();
        else player->resume();
    }
#endif
}

void MusicInterface::pause()
{
    if (m_paused_typed) return;
    m_paused_typed = true;
    set_volume(std::nullopt, 0);
#if defined(_WIN32)
    {
        std::scoped_lock lock(g_mci_music_mutex);
        if (const auto found = g_mci_music_aliases.find(m_channel_typed);
            found != g_mci_music_aliases.end())
            (void)mciSendStringW((L"pause " + wide_from_utf8(found->second)).c_str(), nullptr, 0, nullptr);
    }
#endif
#if CW_UTIL_HAS_SDL_MIXER
    if (const auto found = g_music_tracks.find(this);
        found != g_music_tracks.end() && found->second.track)
        (void)MIX_PauseTrack(found->second.track);
#endif
#if CW_UTIL_HAS_PORTABLE_MIDI
    if (const auto found = g_portable_music_midi_players.find(this);
        found != g_portable_music_midi_players.end())
        found->second->pause();
#endif
}

void MusicInterface::resume()
{
    if (!m_paused_typed) return;
    m_paused_typed = false;
#if defined(_WIN32)
    {
        std::scoped_lock lock(g_mci_music_mutex);
        if (const auto found = g_mci_music_aliases.find(m_channel_typed);
            found != g_mci_music_aliases.end())
            (void)mciSendStringW((L"resume " + wide_from_utf8(found->second)).c_str(), nullptr, 0, nullptr);
    }
#endif
#if CW_UTIL_HAS_SDL_MIXER
    if (const auto found = g_music_tracks.find(this);
        found != g_music_tracks.end() && found->second.track)
        (void)MIX_ResumeTrack(found->second.track);
#endif
#if CW_UTIL_HAS_PORTABLE_MIDI
    if (const auto found = g_portable_music_midi_players.find(this);
        found != g_portable_music_midi_players.end())
        found->second->resume();
#endif
    set_volume(std::nullopt, 0);
}

void MusicInterface::set_mastervolume(::cw::lib::Integer arg_volume)
{
    if (defer_audio_to_game_thread(*this, [arg_volume](MusicInterface& self) {
            self.set_mastervolume(arg_volume);
        })) return;
    m_mastervolume_typed = arg_volume;
    m_mastervolume = arg_volume;
    set_volume(std::nullopt, 0);
}

::cw::lib::String MusicInterface::get_path(::cw::lib::String arg_path, bool arg_inusecard)
{
    const auto path = std::filesystem::u8path(arg_path);
    if (path.is_absolute()) return ::cw::lib::encoding::path_to_utf8(path);
    if (arg_inusecard) {
        m_inusecard_typed = true;
        m_inusecard = true;
        return join_yadodir(arg_path);
    }
    const auto material = get_inusecardmaterialpath(arg_path, ::cw::g_M_MSC, std::nullopt, true);
    if (path_is_file(material)) {
        m_inusecard_typed = true;
        m_inusecard = true;
        return material;
    }
    const auto fallback = get_materialpath(arg_path, ::cw::g_M_MSC, {}, false, true);
    m_inusecard_typed = false;
    m_inusecard = false;
    return fallback;
}

void Repeat::initialize(::cw::lib::Value arg_node, ::cw::lib::String arg_op)
{
    m_node = std::move(arg_node);
    m_op = std::move(arg_op);
}

void SoundInterface::initialize(std::optional<::cw::lib::Value> arg_sound, ::cw::lib::String arg_path, bool arg_is_midi)
{
    m_sound_typed = std::move(arg_sound);
    m_path_typed = std::move(arg_path);
    m_is_midi_typed = arg_is_midi;
    m_channel_typed = -1;
    m_type_typed = -1;
    m_mastervolume_typed = 0;
    m_subvolume_typed = 100;
    m__path = m_path_typed;
    m__is_midi = arg_is_midi;
}

std::shared_ptr<::cw::util::SoundInterface> SoundInterface::copy()
{
    auto result = std::make_shared<SoundInterface>();
    result->m_sound_typed = m_sound_typed;
    result->m_path_typed = m_path_typed;
    result->m_is_midi_typed = m_is_midi_typed;
    result->m_channel_typed = m_channel_typed;
    result->m_type_typed = m_type_typed;
    result->m_mastervolume_typed = m_mastervolume_typed;
    result->m_subvolume_typed = m_subvolume_typed;
    if (m_sound_typed) result->m__sound = *m_sound_typed;
    result->m__path = m__path;
    result->m__is_midi = m__is_midi;
    result->m__type = m__type;
    return result;
}

::cw::lib::String SoundInterface::get_path()
{
    return m_path_typed;
}

::cw::lib::String SoundInterface::_play_before(bool arg_from_scenario, ::cw::lib::Integer arg_channel, ::cw::lib::Integer arg_fade)
{
    m_channel_typed = arg_channel;
    m_channel = arg_channel;
    // Pyは再生開始に成功するまでバックエンド種別を-1のままにする。チャン
    // ネルの準備だけで、開始に失敗したものをBASS再生として分類しない。
    if (::cw::g_cwpy) {
        if (arg_from_scenario) {
            auto& sounds = ::cw::g_cwpy->m_lastsound_scenario;
            if (arg_channel >= 0 && static_cast<std::size_t>(arg_channel) < sounds.size()) {
                auto& slot = sounds[static_cast<std::size_t>(arg_channel)];
                if (slot && slot->get() != this) {
                    (*slot)->stop_impl(true, arg_fade, false);
                    slot.reset();
                }
            }
        } else if (::cw::g_cwpy->m_lastsound_system && ::cw::g_cwpy->m_lastsound_system->get() != this) {
            (*::cw::g_cwpy->m_lastsound_system)->stop_impl(false, arg_fade, false);
            ::cw::g_cwpy->m_lastsound_system.reset();
        }
    }
    return arg_from_scenario ? "Sound" : "SystemSound";
}

void SoundInterface::play(bool arg_from_scenario, ::cw::lib::Integer arg_subvolume, ::cw::lib::Integer arg_loopcount, ::cw::lib::Integer arg_channel, ::cw::lib::Integer arg_fade)
{
    // Pythonは明示的な音量0とループ回数0（無限）を保持する。
    // UI側は100と1を明示的に渡し、0を省略値として扱わない。
    m_type_typed = -1;
    m__type = -1;
    if (::cw::thread::CWPy::s_music.empty() || !::cw::thread::CWPy::s_music.front()) {
        m_mastervolume_typed = 0;
    } else {
        m_mastervolume_typed = ::cw::thread::CWPy::s_music.front()->m_mastervolume_typed;
    }
    m_mastervolume = m_mastervolume_typed;
    // Pyは音声が未読み込みでも、チャンネルが無効でもmastervolumeを更新する。
    if (!m_sound_typed || m_sound_typed->is_none()) return;
    if (arg_channel < 0 || arg_channel >= ::cw::bassplayer::g_MAX_SOUND_CHANNELS) return;
    const auto path = get_path();
    const auto sound_path = std::filesystem::u8path(path);
    const bool mounted_audio = ::cardwirth::platform::is_mounted_user_path(sound_path);
    if (path.empty() || !storage_file_exists(sound_path)) {
        stop_impl(arg_from_scenario, arg_fade, false);
        return;
    }
    _play_before(arg_from_scenario, arg_channel, arg_fade);
    m_subvolume_typed = arg_subvolume;
    m_subvolume = arg_subvolume;
    if (audio_disabled_for_test()) return;
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log("SoundInterface play: path=" + path +
                            " scenario=" + (arg_from_scenario ? "1" : "0") +
                            " channel=" + std::to_string(arg_channel) +
                            " loopcount=" + std::to_string(arg_loopcount));
#endif
#if CW_UTIL_HAS_PORTABLE_MIDI
    g_portable_sound_midi_players.erase(this);
#endif
#if defined(_WIN32)
    // Windowsの再生はPyと同じくBASSを優先
    // し、WinMMの互換用代替処理を維持する。全プラ
    // ットフォームで同じSound APIを使い、マウ
    // ント済み・暗号化済みのバイト列は下のメモリ対応の
    // 移植可能な経路を通す。BASSが利用可能なら通常
    // のファイルとMIDIを担当し、SDL_mixer
    // を代替とする。任意のBASS DLLがない構成
    // でも、読み込めた効果音を再生できるようにする。
    if (!mounted_audio && ::cw::bassplayer::is_alivablewithpath(path)) {
        const auto volume = _get_volumevalue(path) * static_cast<Real>(arg_subvolume) / 100.0;
        if (::cw::bassplayer::play_sound(path, volume, arg_from_scenario, arg_loopcount, arg_channel, arg_fade)) {
            m_type_typed = 0;
            m__type = 0;
            return;
        }
    }
    // Pythonのload_sound()はBASSがない場合、WAV/MP3効果音をWinMM
    // （type 1）で再生し、ループ回数は指定しない。PythonはMCIコマンドを
    // UIスレッドで実行するが、ここでのBGM用MCI経路は既にゲームスレッドで動く。
    if (!mounted_audio) {
        const auto extension = lower_ascii(std::get<1>(splitext(path)));
        if (extension == ".wav" || extension == ".mp3") {
            const auto volume = _get_volumevalue(path) * static_cast<Real>(arg_subvolume) / 100.0;
            if (play_mci_sound(path, mci_sound_alias(arg_from_scenario, arg_channel), volume)) {
                m_type_typed = 1;
                m__type = 1;
                return;
            }
        }
    }
#endif
#if CW_UTIL_HAS_PORTABLE_MIDI
    // TinySoundFontは他の全プラットフォームで使うMIDIシンセサイザ。Windows
    // ではPythonと同じくBASSを先に試してから使う。Pythonにはこの代替処
    // 理がなく、BASSがなければMIDI効果音は無音だが、ここでは再生を可能にする。
    if (m_is_midi_typed) {
        try {
            sdlmixer_init();
            auto player = std::make_unique<::cardwirth::explorer::MidiPlayer>(
                portable_midi_data_directory(path));
            const auto gain = static_cast<float>(std::clamp(
                _get_volumevalue(path) * static_cast<Real>(arg_subvolume) / 100.0,
                0.0, 1.0));
            const auto repeats = mixer_repeat_count(arg_loopcount);
            player->play(std::filesystem::u8path(path), repeats, gain);
            if (g_sound_suspended) player->pause();
            g_portable_sound_midi_players[this] = std::move(player);
            m_type_typed = 3;
            m__type = 3;
            SDL_Log("CardWirthAudio portable SFX MIDI started: path=%s repeats=%d gain=%.3f",
                    path.c_str(), repeats, static_cast<double>(gain));
            return;
        } catch (const std::exception& error) {
            SDL_Log("CardWirthAudio portable SFX MIDI failed: path=%s error=%s",
                    path.c_str(), error.what());
        }
    }
#endif
#if CW_UTIL_HAS_SDL_MIXER
    // Webや他の移植先では、最初のBGMより前にシステム効果音を
    // 再生する場合がある。ここでもミキサーを初期化し、効果音がBGM
    // による先行の音声デバイス初期化に依存しないようにする。
    if (!g_sdl_mixer) sdlmixer_init();
    if (g_sdl_mixer) {
        auto& current = g_sound_tracks[this];
        retire_mixer_track(current, arg_fade, false, arg_from_scenario, arg_channel);
        if (mounted_audio) {
            if (const auto bytes = ::cardwirth::platform::read_mounted_user_file(sound_path))
                current.mounted_data = *bytes;
            if (!current.mounted_data.empty()) {
                // CardWirthPyが効果音に使うpygame.mixer.Sound
                // は、再生前に効果音全体を復号する。MIX_LoadAudioNoCopy()
                // はミキサーのコールバックまで復号を遅らせ
                // るため、MIX_PlayTrack()の
                // 成功だけでは長い効果音、特に足音を実際に
                // 復号できるとは限らず、後の復号失敗も通知
                // されない。圧縮バイト列をRAMに保持し
                // 、シーク可能なメモリストリームで事前復
                // 号する。平文の一時ファイルは作らない。
                SDL_IOStream* stream = SDL_IOFromConstMem(
                    current.mounted_data.data(), current.mounted_data.size());
                current.audio = stream
                    ? MIX_LoadAudio_IO(g_sdl_mixer, stream, true, true)
                    : nullptr;
#if defined(CARDWIRTH_LOG_ENABLED)
                if (!current.audio) {
                    ::cardwirth::debug::log(
                        "Mounted SFX decode failed: path=" + path +
                        " bytes=" + std::to_string(current.mounted_data.size()) +
                        " error=" + SDL_GetError());
                }
#endif
            }
#if defined(CARDWIRTH_LOG_ENABLED)
            else {
                ::cardwirth::debug::log(
                    "Mounted SFX read failed: path=" + path);
            }
#endif
        } else {
            current.audio = MIX_LoadAudio(g_sdl_mixer, path.c_str(), false);
        }
        if (current.audio) {
            current.track = MIX_CreateTrack(g_sdl_mixer);
            if (!current.track || !MIX_SetTrackAudio(current.track, current.audio)) {
#if defined(CARDWIRTH_LOG_ENABLED)
                ::cardwirth::debug::log(
                    "SFX track setup failed: path=" + path +
                    " error=" + SDL_GetError());
#endif
                if (current.track) MIX_DestroyTrack(current.track);
                MIX_DestroyAudio(current.audio);
                current = {};
            } else {
                // Pythonはpygame.mixer.Channel.playへloopcount
                // - 1を渡す。SDL_mixerの値は総再生回数ではなく繰り返し回数。
                const auto loops = arg_loopcount == 0
                    ? -1
                    : static_cast<int>(std::clamp<Integer>(arg_loopcount - 1, 0, std::numeric_limits<int>::max()));
                MIX_SetTrackGain(current.track, static_cast<float>(std::clamp(_get_volumevalue(path) * arg_subvolume / 100.0, 0.0, 1.0)));
                SDL_PropertiesID options = SDL_CreateProperties();
                bool configured = options != 0 &&
                    SDL_SetNumberProperty(options, MIX_PROP_PLAY_LOOPS_NUMBER, loops);
                if (arg_fade > 0) {
                    configured = configured && SDL_SetNumberProperty(
                        options, MIX_PROP_PLAY_FADE_IN_MILLISECONDS_NUMBER, arg_fade);
                }
                const bool started = configured && MIX_PlayTrack(current.track, options);
                if (started && g_sound_suspended) MIX_PauseTrack(current.track);
                if (options) SDL_DestroyProperties(options);
                if (!started) {
#if defined(CARDWIRTH_LOG_ENABLED)
                    ::cardwirth::debug::log(
                        "SFX playback start failed: path=" + path +
                        " error=" + SDL_GetError());
#endif
                    MIX_DestroyTrack(current.track);
                    MIX_DestroyAudio(current.audio);
                    current = {};
                } else {
                    m_type_typed = 2;
                    m__type = 2;
#if defined(CARDWIRTH_LOG_ENABLED)
                    ::cardwirth::debug::log(
                        "SFX playback started: path=" + path +
                        " mounted=" + (mounted_audio ? "1" : "0") +
                        " bytes=" + std::to_string(current.mounted_data.size()) +
                        " loops=" + std::to_string(loops) +
                        " gain=" + std::to_string(MIX_GetTrackGain(current.track)));
#endif
#if defined(__EMSCRIPTEN__) || CW_UTIL_HAS_PORTABLE_MIDI
                    SDL_Log("CardWirthAudio SFX started: path=%s loops=%d",
                            path.c_str(), loops);
#endif
                }
            }
        }
    }
#else
    (void)arg_loopcount;
#endif
}

void SoundInterface::stop(bool arg_from_scenario, ::cw::lib::Integer arg_fade)
{
    stop_impl(arg_from_scenario, arg_fade, true);
}

void SoundInterface::stop_impl(bool arg_from_scenario, ::cw::lib::Integer arg_fade, bool arg_stopfadeout)
{
    (void)arg_stopfadeout;
#if CW_UTIL_HAS_PORTABLE_MIDI
    g_portable_sound_midi_players.erase(this);
#endif
#if defined(_WIN32)
    // Sound::playが選んだものと同じWindowsのバックエンド処理を使う。
    if (m_type_typed == 0) {
        ::cw::bassplayer::stop_sound(arg_from_scenario, m_channel_typed, arg_fade, arg_stopfadeout);
    } else if (m_type_typed == 1) {
        close_mci_sound(mci_sound_alias(arg_from_scenario, m_channel_typed));
    }
#endif
#if CW_UTIL_HAS_SDL_MIXER
    if (arg_stopfadeout) finish_channel_fades(false, arg_from_scenario, m_channel_typed);
    if (const auto found = g_sound_tracks.find(this); found != g_sound_tracks.end()) {
        retire_mixer_track(found->second, arg_fade, false, arg_from_scenario, m_channel_typed);
        g_sound_tracks.erase(found);
    }
#else
    (void)arg_fade;
#endif
    m_type_typed = -1;
    m__type = -1;
    m_channel_typed = -1;
    m_channel = -1;
    m_mastervolume_typed = 0;
    m_mastervolume = 0;
}

::cw::lib::Real SoundInterface::_get_volumevalue(::cw::lib::String arg_fpath)
{
    if (g_sound_suspended) return 0.0;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting) {
        const auto& setting = *::cw::g_cwpy->m_setting;
        if (!setting.m_play_sound) return 0.0;
        const auto base = is_midi(arg_fpath) ? setting.m_vol_sound_midi : setting.m_vol_sound;
        return base * static_cast<Real>(m_mastervolume_typed) / 100.0;
    }
    // Pythonとの意図的な差異。ネイティブ版のヘッドレス初期化はSetting::initより前
    // に行えるため、その場合は最大音量を使う。Pythonは初期化済みのSettingを必要とする。
    return 1.0;
}

void SoundInterface::set_mastervolume(bool arg_from_scenario, ::cw::lib::Integer arg_volume)
{
    if (defer_audio_to_game_thread(*this, [arg_from_scenario, arg_volume](SoundInterface& self) {
            self.set_mastervolume(arg_from_scenario, arg_volume);
        })) return;
    (void)arg_from_scenario;
    m_mastervolume_typed = arg_volume;
    m_mastervolume = arg_volume;
    set_volume(arg_from_scenario, std::nullopt);
}

void SoundInterface::set_volume(bool arg_from_scenario, std::optional<::cw::lib::Integer> arg_volume)
{
    if (defer_audio_to_game_thread(*this, [arg_from_scenario, arg_volume](SoundInterface& self) {
            self.set_volume(arg_from_scenario, arg_volume);
        })) return;
    (void)arg_from_scenario;
    if (m_type_typed == -1) return;
    // Pythonの明示的な音量指定は基準音量の置き換えであり、副音量の
    // 永続的な更新ではない。全バックエンドへこの共通の積を一度だけ渡す。
    const Real base = arg_volume ? static_cast<Real>(*arg_volume) : _get_volumevalue(m_path_typed);
    const Real gain = base * static_cast<Real>(m_subvolume_typed) / 100.0;
#if CW_UTIL_HAS_SDL_MIXER
    if (const auto found = g_sound_tracks.find(this); found != g_sound_tracks.end() && found->second.track)
        MIX_SetTrackGain(found->second.track, static_cast<float>(std::clamp(gain, 0.0, 1.0)));
#endif
#if CW_UTIL_HAS_PORTABLE_MIDI
    if (const auto found = g_portable_sound_midi_players.find(this);
        found != g_portable_sound_midi_players.end())
        found->second->set_volume(static_cast<float>(std::clamp(
            gain, 0.0, 1.0)));
#endif
#if defined(_WIN32)
    // Sound::playが選んだものと同じWindowsのバックエンド処理を使う。
    if (m_type_typed == 0) {
        ::cw::bassplayer::set_soundvolume(gain, arg_from_scenario, m_channel_typed, 0);
    } else if (m_type_typed == 1) {
        set_mci_sound_volume(mci_sound_alias(arg_from_scenario, m_channel_typed), gain);
    }
#endif
}

void _LhafileWrapper::initialize(::cw::lib::String arg_path, ::cw::lib::String arg_mode)
{
    // Pythonは互換性のためmodeを受け取るが、常にLHAのReaderを開く。
    (void)arg_mode;
    m_f = zip_file(std::move(arg_path), ::cw::lib::Value("r"));
}

// Pyは同じラッパーを返す。ネイティブ版の生成されたObjectにはshared_from_this
// の保証がなく、非所有の別参照は本体より長く残るおそれがあるため、所有権を持つ
// ラッパーを複製する。複製はReaderを別々に保持し、close()はこの
// ラッパーだけを解放する。そのため、返却済みの別の複製からは引き続き読める。
std::shared_ptr<::cw::util::_LhafileWrapper> _LhafileWrapper::enter()
{
    // 生成されたクラスのcompat::Objectにはenable_shared_from_this
    // の保証がないため、ラッパーを複製する。
    return std::make_shared<_LhafileWrapper>(*this);
}

// PyはFalseそのものを返すが、ネイティブ版はclose後に空のValue（None
// ）を返す。どちらも偽として評価され、コンテキスト内の例外を抑制しない。ただし直
// 接の戻り値の型は異なり、このコールバックはPyのFalseそのものを返さない。
::cw::lib::Value _LhafileWrapper::exit(std::optional<::cw::lib::Value> arg_exc_type, std::optional<::cw::lib::Value> arg_exc_value, std::optional<::cw::lib::Value> arg_traceback)
{
    (void)arg_exc_type;
    (void)arg_exc_value;
    (void)arg_traceback;
    close();
    return {};
}

void _LhafileWrapper::close()
{
    m_f = {};
}

// 公開の解析処理とregex_fullmatchは同じUnicode文法を共有
// する。公開APIは非公開のASTではなく、元のPyのノードクラスを公開する。
namespace {
String regex_utf8_character(char32_t ch) { return utf8_codepoint(static_cast<Integer>(ch)); }
::cw::lib::Value regex_public_node(const RegexNodePtr& node);
::cw::lib::Value regex_public_children(const std::vector<RegexNodePtr>& nodes) {
    auto seq = std::make_shared<::cw::data::VariantSequence>();
    for (const auto& node : nodes) seq->m_values.push_back(regex_public_node(node));
    return std::static_pointer_cast<::cw::lib::Object>(seq);
}
::cw::lib::Value regex_public_node(const RegexNodePtr& node) {
    switch (node->kind) {
    case RegexNode::Kind::Literal: { auto n=std::make_shared<Lit>(); n->initialize(regex_utf8_character(node->ch)); return std::static_pointer_cast<::cw::lib::Object>(n); }
    case RegexNode::Kind::Dot: return std::static_pointer_cast<::cw::lib::Object>(std::make_shared<Dot>());
    case RegexNode::Kind::CharClass: {
        // 言語による差異。PyのCharClassは解析処理自身のitemsリ
        // ストを保持する。ネイティブのノードは範囲のコピーを公開する。解析は完
        // 了済みなので、異なるのはリストの同一性だけで、照合する範囲は同じ。
        auto n=std::make_shared<CharClass>(); std::vector<std::tuple<String,String>> items;
        for (auto [lo,hi]:node->items) items.emplace_back(regex_utf8_character(lo),regex_utf8_character(hi));
        n->initialize(node->negate,std::move(items)); return std::static_pointer_cast<::cw::lib::Object>(n);
    }
    case RegexNode::Kind::Anchor: { auto n=std::make_shared<Anchor>(); n->initialize(String(1,node->anchor)); return std::static_pointer_cast<::cw::lib::Object>(n); }
    case RegexNode::Kind::Group: { auto n=std::make_shared<Group>(); n->initialize(regex_public_node(node->child)); return std::static_pointer_cast<::cw::lib::Object>(n); }
    case RegexNode::Kind::Concat: { auto n=std::make_shared<Concat>(); n->initialize(regex_public_children(node->children)); return std::static_pointer_cast<::cw::lib::Object>(n); }
    case RegexNode::Kind::Alt: { auto n=std::make_shared<Alt>(); n->initialize(regex_public_children(node->children)); return std::static_pointer_cast<::cw::lib::Object>(n); }
    case RegexNode::Kind::Repeat: { auto n=std::make_shared<Repeat>(); n->initialize(regex_public_node(node->child),String(1,node->repeat)); return std::static_pointer_cast<::cw::lib::Object>(n); }
    }
    throw std::logic_error("unknown regex node type");
}
std::shared_ptr<::cw::data::VariantSequence> regex_node_sequence(const ::cw::lib::Value& value) {
    const auto* p=std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    auto seq=p?std::dynamic_pointer_cast<::cw::data::VariantSequence>(*p):nullptr;
    if (!seq) throw std::logic_error("regex node sequence required");
    return seq;
}
// 未知の具象ノードに対するPyのAssertionErrorは、ネイティブのstd::logic_error
// に対応させる。C++にはPython組み込みの例外クラスオブジェクトはない。
RegexNodePtr regex_import_public_node(const ::cw::lib::Value& value) {
    const auto* p=std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    if (!p || !*p) throw std::logic_error("unknown regex node type");
    const auto& obj=*p;
    if (auto legacy=std::dynamic_pointer_cast<RegexAstObject>(obj)) return legacy->root;
    auto n=std::make_shared<RegexNode>();
    if (typeid(*obj)==typeid(Lit)) {
        n->kind=RegexNode::Kind::Literal;
        const auto& ch=std::static_pointer_cast<Lit>(obj)->m_ch;
        if(const auto* str=std::get_if<String>(&ch.storage()))n->public_literal=regex_characters(*str);
        else n->ch=RegexParser::end;
    }
    else if (typeid(*obj)==typeid(Dot)) n->kind=RegexNode::Kind::Dot;
    else if (typeid(*obj)==typeid(CharClass)) {
        auto pub=std::static_pointer_cast<CharClass>(obj);n->kind=RegexNode::Kind::CharClass;n->negate=pub->m_negate.as_bool();
        n->public_ranges.emplace();
        for (const auto& pair:regex_node_sequence(pub->m_items)->m_values) {
            auto values=regex_node_sequence(pair);if(values->m_values.size()!=2) throw std::logic_error("regex range pair required");
            const auto* lo=std::get_if<String>(&values->m_values[0].storage());
            const auto* hi=std::get_if<String>(&values->m_values[1].storage());
            if(!lo || !hi)throw std::invalid_argument("regex range strings required");
            n->public_ranges->emplace_back(regex_characters(*lo),regex_characters(*hi));
        }
    }
    else if (typeid(*obj)==typeid(Anchor)) { n->kind=RegexNode::Kind::Anchor;n->anchor=value_string(std::static_pointer_cast<Anchor>(obj)->m_kind)=="^"?'^':'$'; }
    else if (typeid(*obj)==typeid(Group)) { n->kind=RegexNode::Kind::Group;n->child=regex_import_public_node(std::static_pointer_cast<Group>(obj)->m_node); }
    else if (typeid(*obj)==typeid(Concat) || typeid(*obj)==typeid(Alt)) {
        const bool alt=typeid(*obj)==typeid(Alt);n->kind=alt?RegexNode::Kind::Alt:RegexNode::Kind::Concat;
        const auto values=alt?std::static_pointer_cast<Alt>(obj)->m_branches:std::static_pointer_cast<Concat>(obj)->m_nodes;
        for(const auto& child:regex_node_sequence(values)->m_values)n->children.push_back(regex_import_public_node(child));
    }
    else if (typeid(*obj)==typeid(Repeat)) {
        auto pub=std::static_pointer_cast<Repeat>(obj);const auto op=value_string(pub->m_op);
        if(op!="*" && op!="+" && op!="?")throw std::logic_error("unknown regex repeat operator");
        n->kind=RegexNode::Kind::Repeat;n->repeat=op.front();n->child=regex_import_public_node(pub->m_node);
    }
    else throw std::logic_error("unknown regex node type");
    return n;
}
template<class Fn> auto regex_parse_public(_Parser& owner, Fn fn) {
    const auto* pattern=std::get_if<String>(&owner.m_p.storage());
    if (!pattern) throw std::invalid_argument("regex pattern must be a string");
    // _Parserのメソッドを直接呼ぶ場合はm_iから開始し、解析エラーで処理を抜け
    // る際も消費した位置を反映する。regex_fullmatchは0から開始する。
    RegexParser parser(*pattern,owner.m_i,owner.m_n);
    struct Cursor { _Parser& owner; RegexParser& parser; ~Cursor(){owner.m_i=parser.cursor();} } sync{owner,parser};
    return fn(parser);
}
}
void _Parser::initialize(String pattern) {
    m_pattern_typed=std::move(pattern);m_p=m_pattern_typed;m_i=0;
    m_n=static_cast<Integer>(regex_characters(m_pattern_typed).size());
}
::cw::lib::Value _Parser::peek() { return regex_parse_public(*this,[](RegexParser& p)->::cw::lib::Value { auto ch=p.peek();if(ch==RegexParser::end)return ::cw::lib::Value{};return ::cw::lib::Value(regex_utf8_character(ch)); }); }
String _Parser::consume(std::optional<String> expected) {
    return regex_parse_public(*this,[&](RegexParser& p){
        auto ch=p.peek();
        if(ch==RegexParser::end)throw RegexParseError("unexpected end of pattern");
        if(expected && regex_utf8_character(ch)!=*expected)
            throw RegexParseError("expected " + ::cw::lib::python_percent_detail::repr(*expected,false) +
                                  " but got " + ::cw::lib::python_percent_detail::repr(regex_utf8_character(ch),false));
        return regex_utf8_character(p.consume());
    });
}
::cw::lib::Value _Parser::parse() { return regex_parse_public(*this,[](RegexParser& p){return regex_public_node(p.parse());}); }
::cw::lib::Value _Parser::parse_alt() { return regex_parse_public(*this,[](RegexParser& p){return regex_public_node(p.parse_alt());}); }
::cw::lib::Value _Parser::parse_concat() { return regex_parse_public(*this,[](RegexParser& p){return regex_public_node(p.parse_concat());}); }
::cw::lib::Value _Parser::parse_repeat() { return regex_parse_public(*this,[](RegexParser& p){return regex_public_node(p.parse_repeat());}); }
::cw::lib::Value _Parser::parse_atom() { return regex_parse_public(*this,[](RegexParser& p){return regex_public_node(p.parse_atom());}); }
::cw::lib::Value _Parser::parse_charclass() { return regex_parse_public(*this,[](RegexParser& p){return regex_public_node(p.parse_class());}); }
String _Parser::parse_class_char() {
    // 公開する文字クラス内エスケープの仕様を保つ。\dなどの未対応エスケ
    // ープは、バックスラッシュそのものとして消費せず、ここで失敗させる。
    return regex_parse_public(*this,[](RegexParser& p){return regex_utf8_character(p.parse_class_char());});
}

void _Unlock::initialize(::cw::lib::String arg_name, ::cw::lib::Value arg_f)
{
    m_name = std::move(arg_name);
    m_f = std::move(arg_f);
}

void _Unlock::unlock()
{
    // Pythonの_Unlockには__del__がなく、fが真と評価される場合
    // だけロックを解除する。ネイティブの生ハンドルは非公開の登録表に属する。同期さ
    // れたアダプタはタプルのパス（要素1）を使い、Noneの仮置き値は使わない。
    std::lock_guard<std::mutex> guard(mutex_api_mutex);
    if (!m_f || !m_f->as_bool()) return;
    const auto name = value_string(m_name);
    for (std::size_t i = 0; i < g__mutex.size(); ++i) {
        if (std::get<1>(g__mutex[i]) == name && i < mutex_records.size() &&
            unlock_mutex_record(i, &m_f)) {
            // 公開の_Unlock.unlockはファイルを閉じるが、release/clearが取り
            // 出すまで_mutexのタプルを残す。Pythonのowner.f=Noneと同じ扱い。
            break;
        }
    }
    m_f.reset();
}

void LogicalStr::initialize(::cw::lib::String arg_s)
{
    m_s = arg_s;
    m_seq.clear();
    std::size_t pos = 0;
    while (!arg_s.empty()) {
        std::size_t begin = pos;
        while (pos < arg_s.size() && !std::isdigit(static_cast<unsigned char>(arg_s[pos]))) ++pos;
        if (pos == arg_s.size()) { m_seq.emplace_back(::cw::lib::encoding::python_lower(arg_s.substr(begin))); break; }
        m_seq.emplace_back(::cw::lib::encoding::python_lower(arg_s.substr(begin,pos-begin)) + "0");
        if (pos == arg_s.size()) break;
        begin = pos;
        while (pos < arg_s.size() && std::isdigit(static_cast<unsigned char>(arg_s[pos]))) ++pos;
        const auto digits = arg_s.substr(begin, pos - begin);
        auto token = std::make_shared<::cw::data::VariantSequence>();
        // Valueはint64を持つが任意精度整数型は持たない
        // 。大きなPythonのintの係数はDecimalで
        // 正確に保持し、元の数字表記をタプルの第2要素に残す。
        if (const auto number=::cw::lib::parse_python_integer(digits)) token->m_values.emplace_back(*number);
        else token->m_values.emplace_back(Decimal::parse(digits));
        token->m_values.emplace_back(digits);
        m_seq.emplace_back(std::static_pointer_cast<::cw::lib::Object>(token));
    }
}

bool LogicalStr::less_than(::cw::lib::Value arg_other)
{
    const auto* object=std::get_if<::cw::lib::ObjectPtr>(&arg_other.storage());
    const auto other=object?std::dynamic_pointer_cast<LogicalStr>(*object):nullptr;
    if (!other) throw std::invalid_argument("LogicalStr comparison requires LogicalStr");
    for(std::size_t i=0;i<std::min(m_seq.size(),other->m_seq.size());++i){
        const auto result=value_compare(m_seq[i],other->m_seq[i],true);
        if(result)return result<0;
    }
    if(m_seq.size()!=other->m_seq.size())return m_seq.size()<other->m_seq.size();
    return value_compare(m_s,other->m_s,true)<0;
}

Integer logical_compare(const String& lhs, const String& rhs)
{
    return natural_compare(lhs,rhs);
}

bool LogicalStr::equals(::cw::lib::Value arg_other)
{
    const auto* object=std::get_if<::cw::lib::ObjectPtr>(&arg_other.storage());
    const auto other=object?std::dynamic_pointer_cast<LogicalStr>(*object):nullptr;
    if (!other) throw std::invalid_argument("LogicalStr comparison requires LogicalStr");
    if(m_seq.size()!=other->m_seq.size())return false;
    for(std::size_t i=0;i<m_seq.size();++i)if(!ordering_value_equal(m_seq[i],other->m_seq[i]))return false;
    return ordering_value_equal(m_s,other->m_s);
}

::cw::lib::Integer g_NO_OVERWRITE{};
::cw::lib::Integer g_OVERWRITE_ALWAYS{};
::cw::lib::Integer g_OVERWRITE_WITH_LATEST_FILES{};
::cw::lib::String g_WRAPS_CHARS{"｡|､|，|、|。|．|）|」|』|〕|｝|】"};
std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String> g__ANIMATED_WXBMP_ATTRS{};
::cw::lib::ValueSet g__ESCAPABLE{};
::cw::lib::Value g__SortableByAttrT{};
::cw::lib::Value g__SortableT{};
::cw::lib::Value g__SyncLock{};
std::set<std::shared_ptr<::cw::lib::Thread>> g__cominit_table{};
::cw::lib::Value g__lock_mutex{};
std::vector<std::tuple<::cw::lib::Value, ::cw::lib::String>> g__mutex{};
::cw::lib::Value g__re_num{};
::cw::lib::Value g__shlwapi{};
std::mutex g__zip_mutex;
std::map<::cw::lib::String, ::cw::lib::Real> g_dictimes{};
::cw::lib::Real g_timer{};
::cw::lib::Value g_times{};

std::tuple<std::shared_ptr<::cw::lib::Surface>, std::shared_ptr<::cw::lib::Surface>, std::optional<std::shared_ptr<::cw::lib::Surface>>, std::shared_ptr<::cw::lib::Object>> init(std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> arg_size_noscale, ::cw::lib::String arg_title, bool arg_fullscreen, std::optional<std::vector<std::tuple<::cw::lib::String, bool, ::cw::lib::Integer>>> arg_soundfonts, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_fullscreensize, bool arg_sdlmixer_enabled)
{
    const auto base_size = arg_size_noscale.value_or(arg_fullscreensize);
    const auto screen_scale = std::max<Real>(::cw::g_UP_SCR, 1.0);
    const auto window_scale = std::max<Real>(::cw::g_UP_WIN, 1.0);
    const auto width = std::max<Integer>(1, static_cast<Integer>(std::llround(std::get<0>(base_size) * screen_scale)));
    const auto height = std::max<Integer>(1, static_cast<Integer>(std::llround(std::get<1>(base_size) * screen_scale)));
    const auto window_width = std::max<Integer>(1, static_cast<Integer>(std::llround(std::get<0>(base_size) * window_scale)));
    const auto window_height = std::max<Integer>(1, static_cast<Integer>(std::llround(std::get<1>(base_size) * window_scale)));
    auto scr = std::make_shared<::cw::lib::Surface>();
    auto scr_draw = std::make_shared<::cw::lib::Surface>();
    std::optional<std::shared_ptr<::cw::lib::Surface>> scr_fullscreen;
    if (arg_fullscreen) {
        // SDLやネイティブのウィンドウアダプタが、この要求された位置・サイ
        // ズを使う。生成された互換層はウィンドウが存在しなくても記録する。
        scr_fullscreen = std::make_shared<::cw::lib::Surface>();
        (*scr_fullscreen)->size = {std::get<0>(arg_fullscreensize), std::get<1>(arg_fullscreensize)};
        scr->size = {width, height};
        scr_draw->size = {std::get<0>(arg_fullscreensize), std::get<1>(arg_fullscreensize)};
        scr_draw = scr;
    } else {
        scr->size = {window_width, window_height};
        scr_draw->size = (::cw::g_UP_WIN == ::cw::g_UP_SCR) ? scr->size : ::cw::lib::Size{width, height};
    }
    (void)arg_title;
    (void)arg_soundfonts;
    if (arg_sdlmixer_enabled) sdlmixer_init();
    // SDL_mixerは上で初期化済み。SDL_mixerはサウンドフォント用APIを公
    // 開しないため、FluidSynthのサウンドフォント選択はミキサーのアダプタが担う。
    auto clock = std::make_shared<::cw::lib::Object>();
    return {scr, scr_draw, scr_fullscreen, clock};
}

bool sdlmixer_is_initialized()
{
#if CW_UTIL_HAS_SDL_MIXER
    return g_sdl_mixer != nullptr;
#else
    return false;
#endif
}

void sdlmixer_quit()
{
#if CW_UTIL_HAS_SDL_MIXER
    // pygameはこれらのリソースを内部で所有する。SDL_mixer3の利用側は
    // トラックや音声の生ポインタをここで保持するため、ミキサーを破棄する前にそれらと
    // マウントされたバイトバッファを解放する。公開のMusic/Soundオブジェ
    // クトは破棄しない。音声要求の受け付けとこの切り替えは、既存のゲームスレッドの
    // 振り分け処理で実行する。これらの対応表に古いミキサーへのポインタを残さない。
    for (auto& [unused, value] : g_music_tracks) destroy_mixer_track(value);
    for (auto& [unused, value] : g_sound_tracks) destroy_mixer_track(value);
    for (auto& fade : g_fading_mixer_tracks) destroy_mixer_track(fade.value);
    g_music_tracks.clear();
    g_sound_tracks.clear();
    g_fading_mixer_tracks.clear();
    if (!g_sdl_mixer && !g_sdl_mixer_initialized && !g_sdl_audio_initialized) return;
    run_mixer_on_main_thread([] {
        if (g_sdl_mixer) MIX_DestroyMixer(g_sdl_mixer);
        g_sdl_mixer = nullptr;
        if (g_sdl_mixer_initialized) MIX_Quit();
        g_sdl_mixer_initialized = false;
        if (g_sdl_audio_initialized) SDL_QuitSubSystem(SDL_INIT_AUDIO);
        g_sdl_audio_initialized = false;
    });
#endif
}

void sdlmixer_init()
{
#if CW_UTIL_HAS_SDL_MIXER
    if (audio_disabled_for_test() || g_sdl_mixer) return;
    // サウンドフォントの切り替えではSDLを終了・再初期化する場合があり、プロセスの生存期間を通じたonce_flag
    // とは異なる。自分で取得したSDL/MIXの初期化参照だけを対応させて解放する。
    run_mixer_on_main_thread([] {
        if (!g_sdl_audio_initialized) {
            if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
#if CW_UTIL_HAS_PORTABLE_MIDI
                SDL_LogError(SDL_LOG_CATEGORY_AUDIO,
                             "CardWirthAudio SDL audio init failed: %s", SDL_GetError());
#endif
                return;
            }
            g_sdl_audio_initialized = true;
        }
        if (!g_sdl_mixer_initialized) {
            if (!MIX_Init()) {
#if CW_UTIL_HAS_PORTABLE_MIDI
                SDL_LogError(SDL_LOG_CATEGORY_AUDIO,
                             "CardWirthAudio SDL_mixer init failed: %s", SDL_GetError());
#endif
                return;
            }
            g_sdl_mixer_initialized = true;
        }
        g_sdl_mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
#if CW_UTIL_HAS_PORTABLE_MIDI
        if (!g_sdl_mixer)
            SDL_LogError(SDL_LOG_CATEGORY_AUDIO,
                         "CardWirthAudio mixer device failed: %s", SDL_GetError());
        else
            SDL_Log("CardWirthAudio mixer device ready");
#endif
    });
#endif
}

void poll_music_playback()
{
#if defined(_WIN32)
    // MCIシーケンサの別名はMusicInterfaceがゲー
    // ムスレッドで開く。状態取得・シーク・再生も同じスレッドで行
    // う。別スレッドからMCIコマンドを送ると、実際のWinMM
    // で末尾に達しても再開コマンドが受け付けられない場合がある。
    const auto now = std::chrono::steady_clock::now();
    if (g_mci_music_last_poll.time_since_epoch().count() != 0 &&
        now - g_mci_music_last_poll < std::chrono::milliseconds(100))
        return;
    g_mci_music_last_poll = now;
    std::scoped_lock lock(g_mci_music_mutex);
    for (auto it = g_mci_music_repeats.begin(); it != g_mci_music_repeats.end();) {
        const auto alias_it = g_mci_music_aliases.find(it->first);
        if (alias_it == g_mci_music_aliases.end()) {
            it = g_mci_music_repeats.erase(it);
            continue;
        }
        const auto alias = wide_from_utf8(alias_it->second);
        wchar_t mode[32]{};
        const auto status_error = mciSendStringW(
            (L"status " + alias + L" mode").c_str(), mode, 32, nullptr);
        if (status_error != 0) {
#if defined(CARDWIRTH_LOG_ENABLED)
            ::cardwirth::debug::log("Windows MIDI loop status failed: channel=" +
                                    std::to_string(it->first) +
                                    " error=" + std::to_string(status_error));
#endif
            ++it;
            continue;
        }
        const bool ended = _wcsicmp(mode, L"stopped") == 0 ||
                           _wcsicmp(mode, L"not ready") == 0;
        if (!ended) {
            g_mci_music_stopped_since.erase(it->first);
            ++it;
            continue;
        }
        if (it->second == 0) {
            it = g_mci_music_repeats.erase(it);
            continue;
        }
        // WinMMシーケンサの終端コールバックが完了してから再開す
        // る。midimap.dllがそのコールバックを処理中にseek
        // の直後にplayを送ると、マッパーへ二重に入り、Release
        // 実行時にアクセス違反を起こすことがある。play ...
        // from 0という単一コマンドで、MCIの境界でも再開
        // を一体化する。これはWinMMの代替経路だけに適用し、BASS
        // と移植可能なMIDIはそれぞれのループ実装を使う。
        const auto [stopped, inserted] = g_mci_music_stopped_since.emplace(it->first, now);
        if (inserted || now - stopped->second < std::chrono::milliseconds(250)) {
            ++it;
            continue;
        }
        const auto play_error = mciSendStringW(
            (L"play " + alias + L" from 0").c_str(), nullptr, 0, nullptr);
        if (play_error == 0) {
            if (it->second > 0) --it->second;
            g_mci_music_stopped_since.erase(it->first);
#if defined(CARDWIRTH_LOG_ENABLED)
            ::cardwirth::debug::log("Windows MIDI loop restarted: channel=" +
                                    std::to_string(it->first));
#endif
        } else {
            stopped->second = now;
#if defined(CARDWIRTH_LOG_ENABLED)
            ::cardwirth::debug::log("Windows MIDI loop restart failed: channel=" +
                                    std::to_string(it->first) +
                                    " error=" + std::to_string(play_error));
#endif
        }
        ++it;
    }
#endif
}

// Pyは文字列でない値をそのまま返す。ネイティブの画素処理は整数
// 座標2つを必要とし、任意のValueを添字アクセスできないため
// 、ここで正規化する。省略・Noneは既定の(0, 0)、Integer
// は(x, 0)、単一のDecimalは0方向に切り捨てた値を
// xとする。タプル・リストはVariantSequenceとし
// て先頭2要素を使い、残りを無視する。value_integer
// はReal/Decimal要素を切り捨て、未対応の要素型は0
// とする。数値変換失敗はその補助処理のエラーを維持する。他の値
// や2要素未満の列は画素参照へ渡さず、ここで拒否する。center/right
// の名前指定は引き続きPyの座標計算に従う。
std::tuple<::cw::lib::Integer, ::cw::lib::Integer> convert_maskpos(::cw::lib::Value arg_maskpos, ::cw::lib::Integer arg_width, ::cw::lib::Integer arg_height)
{
    // Pythonの既定引数を使う場合、C++からload_wxbmp()
    // へ空のValueが渡る。Pythonの既定値は左上の画素(0,
    // 0)なので、省略やNoneのマスク位置をここで拒否しない。
    if (arg_maskpos.is_none()) return {0, 0};
    if (const auto* text = std::get_if<::cw::lib::String>(&arg_maskpos.storage())) {
        // Pyの//は負の奇数寸法を小さい方へ丸めるが、C++の/は0方向へ切り捨て
        // る。Integerの最小値でも表現可能なままにするため、除算後に減算する。
        if (*text == "center") {
            const auto half = [](Integer value) { return value / 2 - (value < 0 && value % 2 != 0 ? 1 : 0); };
            return {half(arg_width), half(arg_height)};
        }
        if (*text == "right") return {arg_width - 1, 0};
        throw std::invalid_argument("invalid mask position: " + *text);
    }
    if (const auto* pair = std::get_if<::cw::lib::Integer>(&arg_maskpos.storage())) return {*pair, 0};
    if (const auto* pair = std::get_if<::cw::lib::Decimal>(&arg_maskpos.storage())) {
        { return {pair->to_integer(::cw::lib::DecimalRounding::Down), 0}; }}
    // タプル・リストの座標は、ネイティブの境界では(x, y)などの
    // VariantSequenceで渡る。アニメーション付きビット
    // マップの経路で使うvalue_pair()と同じ扱いを保つ。
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&arg_maskpos.storage()); object && *object) {
        if (const auto sequence = std::dynamic_pointer_cast<::cw::data::VariantSequence>(*object);
            sequence && sequence->m_values.size() >= 2) {
            return {value_integer(sequence->m_values[0], 0), value_integer(sequence->m_values[1], 0)};
        }
    }
    throw std::invalid_argument("mask position must be a string or coordinate");
}

std::vector<std::tuple<::cw::lib::String, ::cw::lib::Integer>> get_scaledimagepaths(::cw::lib::String arg_path, bool arg_can_loaded_scaledimage)
{
    std::vector<std::tuple<::cw::lib::String, ::cw::lib::Integer>> result{{arg_path, 1}};
    if (!arg_can_loaded_scaledimage) return result;
    for (const auto scale : scale_values()) {
        result.emplace_back(scaled_image_name(arg_path, scale), scale);
    }
    return result;
}

void copy_scaledimagepaths(::cw::lib::String arg_frompath, ::cw::lib::String arg_topath, bool arg_can_loaded_scaledimage)
{
    // リソースアーカイブは素材パスを遅延して公開する。Resource.get_filepath()
    // と同様に、ファイルシステムへ触れる前に元のパスを解決する。
    if (::cw::g_cwpy && ::cw::g_cwpy->m_rsrc) {
        const auto resolved = ::cw::g_cwpy->m_rsrc->get_filepath(arg_frompath);
        if (!resolved.empty()) arg_frompath = resolved;
    }
    const auto from = std::filesystem::u8path(arg_frompath);
    const auto to = std::filesystem::u8path(arg_topath);
    const auto copy = [](const std::filesystem::path& source, const std::filesystem::path& requested_destination) {
        // shutil.copy2は対象がディレクトリなら移動元のファイル
        // 名を追加し、対象の親がなければ失敗する。ネイティブ側・マウント
        // 側の基本処理より前にこの規則を適用し、全ストレージで共有する。
        auto destination = requested_destination;
        if (path_is_directory(::cw::lib::encoding::path_to_utf8(destination)))
            destination /= source.filename();
        const auto parent = destination.parent_path().empty()
            ? std::filesystem::path(".") : destination.parent_path();
        if (!path_is_directory(::cw::lib::encoding::path_to_utf8(parent)))
            throw std::filesystem::filesystem_error("copy_scaledimagepaths", source, destination,
                                                    std::make_error_code(std::errc::no_such_file_or_directory));
        const bool source_mounted = ::cardwirth::platform::is_mounted_user_path(source);
        const bool destination_mounted = ::cardwirth::platform::is_mounted_user_path(destination);
        if (source_mounted && destination_mounted) {
            if (!::cardwirth::platform::copy_mounted_user_path(source, destination, true))
                throw std::runtime_error("copy_scaledimagepaths: Storage copy failed");
            return;
        }
        if (source_mounted || destination_mounted) {
            Bytes bytes;
            if (!read_binary_path(source, bytes)) throw std::runtime_error("copy_scaledimagepaths: read failed");
            if (destination_mounted) {
                if (!::cardwirth::platform::write_mounted_user_file(destination,
                        std::as_bytes(std::span<const std::uint8_t>(bytes.data(), bytes.size())), true))
                    throw std::runtime_error("copy_scaledimagepaths: Storage write failed");
            } else {
                std::ofstream output(destination, std::ios::binary | std::ios::trunc);
                output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                output.flush();
                if (!output) throw std::runtime_error("copy_scaledimagepaths: write failed");
                output.close();
                if (!output) throw std::runtime_error("copy_scaledimagepaths: close failed");
            }
            return;
        }
        std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing);
    };
    copy(from, to);
    if (!arg_can_loaded_scaledimage || !is_image_file(from)) return;
    for (const auto scale : scale_values()) {
        const auto scaled_from = scaled_image_path(from, scale);
        const auto scaled_to = scaled_image_path(to, scale);
        if (storage_file_exists(scaled_from)) copy(scaled_from, scaled_to);
    }
}

void remove_scaledimagepaths(::cw::lib::String arg_fpath, bool arg_can_loaded_scaledimage, bool arg_trashbox)
{
    const auto path = std::filesystem::u8path(arg_fpath);
    if (!storage_file_exists(path)) return;
    const auto remove_one=[&](const std::filesystem::path& target) {
        if (!storage_file_exists(target)) return;
        if (::cardwirth::platform::is_mounted_user_path(target)) {
            // マウントされたオブジェクトには、ネイティブのゴミ箱で使う実体がない。
            if (!::cardwirth::platform::remove_mounted_user_path(target, false))
                throw std::runtime_error("remove_scaledimagepaths: Storage deletion failed");
        } else if (arg_trashbox) {
            send_trashbox(::cw::lib::encoding::path_to_utf8(target));
        } else {
            std::error_code error;
            if (!std::filesystem::remove(target,error) && error)
                throw std::filesystem::filesystem_error("remove_scaledimagepaths",target,error);
        }
    };
    remove_one(path);
    if (!arg_can_loaded_scaledimage || !is_image_file(path)) return;
    for (const auto scale : scale_values()) {
        const auto scaled = scaled_image_path(path, scale);
        remove_one(scaled);
    }
}

std::tuple<::cw::lib::String, ::cw::lib::Integer> find_scaledimagepath(::cw::lib::String arg_path, ::cw::lib::Real arg_up_scr, bool arg_can_loaded_scaledimage, bool arg_noscale)
{
    if (::cw::binary::image::path_is_code(arg_path)) return {arg_path, 1};
    const auto normalized_path = join_paths({::cw::lib::Value(arg_path)});
    const auto source = std::filesystem::u8path(normalized_path);
    bool special_directory = false;
    if (!arg_can_loaded_scaledimage && ::cw::g_cwpy) {
        const auto temp = value_string(::cw::g_tempdir);
        const auto skin = value_string(::cw::g_cwpy->m_skindir);
        // generic_string()はWindowsのナロー文字列用コードページを使い、そこで表せな
        // いUnicodeの宿名・メンバー名では変換に失敗するため、path_to_utf8()を使う。
        const auto normalized = ::cw::lib::encoding::path_to_utf8(source);
        const auto temp_prefix = join_paths({::cw::lib::Value(temp), ::cw::lib::Value("ScenarioLog/TempFile")});
        const auto skin_prefix = join_paths({::cw::lib::Value(skin), ::cw::lib::Value("Table")});
        special_directory = (!temp_prefix.empty() && normalized.rfind(temp_prefix + "/", 0) == 0) ||
                            (!skin_prefix.empty() && normalized.rfind(skin_prefix + "/", 0) == 0);
    }
    if (arg_noscale || (!arg_can_loaded_scaledimage && !special_directory)) return {normalized_path, 1};
    if (!(arg_up_scr > 0.0)) throw std::invalid_argument("up_scr must be positive");
    // Pyはint(log(up_scr, 2))で0方向へ切り捨て、そのべき乗を
    // 半分にする。SCALE_LISTとは独立しており、微小な補正値も使わない。
    const auto power = std::pow(2.0, std::trunc(std::log(arg_up_scr) / std::log(2.0)));
    // Pythonはより大きな整数倍率を表せるが、ネイティブの戻り値型には
    // 上限がある。未定義のキャストを行う前に、表現できない範囲を拒否する。
    if (!std::isfinite(power) || power >= std::ldexp(1.0, std::numeric_limits<Integer>::digits))
        throw std::out_of_range("scaled image factor is outside the representable range");
    Integer scale = static_cast<Integer>(power);
    for (; scale >= 2; scale /= 2) {
        auto candidate = scaled_image_path(source, scale);
        if (::cw::g_cwpy && ::cw::g_cwpy->m_rsrc)
            candidate = std::filesystem::u8path(::cw::g_cwpy->m_rsrc->get_filepath(
                ::cw::lib::encoding::path_to_utf8(candidate)));
        const bool exists = ::cardwirth::platform::is_mounted_user_path(candidate)
            ? [&] {
                const auto status = ::cardwirth::platform::stat_mounted_user_path(candidate);
                return status && status->is_file();
              }()
            : std::filesystem::is_regular_file(candidate);
        if (exists)
            return {::cw::lib::encoding::path_to_utf8(candidate), scale};
    }
    return {normalized_path, scale};
}

::cw::lib::String find_noscalepath(::cw::lib::String arg_path)
{
    const auto path = std::filesystem::u8path(arg_path);
    const auto name = ::cw::lib::encoding::path_to_utf8(path.filename());
    // PyはEXTS_IMGだけを対象とし、他のファイルの.xN接尾辞は名前の一部として扱う。
    const auto ext = ::cw::lib::encoding::path_to_utf8(path.extension());
    // ネイティブのデコーダは.tiffにも対応するが、PyのEXTS_IMGに
    // は含まれない。このパス処理では、その.xN表記を名前の一部として保つ。
    if (!is_image_file(path) || lower_ascii(ext) == ".tiff" || !is_scaled_filename(name)) return arg_path;
    const auto stem = ::cw::lib::encoding::path_to_utf8(path.stem());
    const auto base_stem = std::regex_replace(stem, std::regex(R"(\.x(?:2|4|8|16)$)", std::regex::icase), "");
    const auto candidate = path.parent_path() / (base_stem + ext);
    return storage_file_exists(candidate)
        ? ::cw::lib::encoding::path_to_utf8(candidate) : arg_path;
}

std::shared_ptr<::cw::lib::Surface> load_image(::cw::lib::String arg_path, bool arg_mask, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_maskpos, std::optional<::cw::lib::Value> arg_f, bool arg_retry, bool arg_isback, bool arg_can_loaded_scaledimage, bool arg_noscale, std::optional<::cw::lib::Value> arg_up_scr, bool arg_use_excache, std::optional<::cw::lib::String> arg_named_maskpos)
{
    // Pythonは倍率付き画像の検索とファイル読み込みの両方より
    // 前に、ResourceTableの別名を解決する。ここでも同
    // じ順序を保ち、解決済みパスではなく論理スキンパスが渡された場
    // 合も、実際のUTF-8ファイルシステム上のファイルを読む。
    if (!arg_path.empty() && !::cw::binary::image::path_is_code(arg_path) &&
        ::cw::g_cwpy && ::cw::g_cwpy->m_rsrc) {
        const auto resolved_path = ::cw::g_cwpy->m_rsrc->get_filepath(
            ::cw::util::normalize_path(std::move(arg_path)));
        arg_path = resolved_path;
    }
    Bytes data;
    bool supplied_data = false;
    Real requested_scale = ::cw::g_UP_SCR;
    if (arg_up_scr) {
        // 指定されたRealの倍率を保持する。Pyはそれをキャッシュの倍率と直接比較する。
        if (const auto* scale = std::get_if<Real>(&arg_up_scr->storage())) requested_scale = *scale;
        else requested_scale = static_cast<Real>(value_integer(*arg_up_scr, 1));
    }
    if (arg_use_excache && !arg_path.empty() && ::cw::g_cwpy) {
        if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_sdata.storage()); object && *object) {
            if (const auto sdata = std::dynamic_pointer_cast<::cw::data::SystemData>(*object)) {
                const auto npath = get_keypath(get_symlinktarget(arg_path));
                if (const auto found = sdata->m_ex_cache.find(npath); found != sdata->m_ex_cache.end()) {
                    if (const auto* seq_object = std::get_if<::cw::lib::ObjectPtr>(&found->second.storage());
                        seq_object && *seq_object) {
                        if (const auto sequence = std::dynamic_pointer_cast<::cw::data::VariantSequence>(*seq_object)) {
                            const Integer scales[5] = {1, 2, 4, 8, 16};
                            Bytes cached;
                            Integer cached_scale = 1;
                            for (std::size_t i = 0; i < sequence->m_values.size() && i < 5; ++i) {
                                if (const auto* bytes = std::get_if<Bytes>(&sequence->m_values[i].storage());
                                    bytes && !bytes->empty()) {
                                    cached = *bytes;
                                    cached_scale = scales[i];
                                }
                                // Pyは倍率が完全一致したときだけ検索を止める
                                // 。小数の表示倍率では5つのスナップショットを
                                // すべて調べ、最後に見つかったものを保持する。
                                if (static_cast<Real>(scales[i]) == requested_scale) break;
                            }
                            // Pyは空のスナップショット列でもファイルの代替倍率
                            // を1倍へ戻す。より新しいxNファイルを選ばない。
                            requested_scale = static_cast<Real>(cached_scale);
                            if (!cached.empty()) {
                                data = std::move(cached);
                                supplied_data = true;
                            }
                        }
                    }
                }
            }
        }
    }
    if (arg_f && !supplied_data) {
        if (const auto* bytes = std::get_if<Bytes>(&arg_f->storage())) {
            data = *bytes;
            supplied_data = true;
        }
    }
    if (!supplied_data && ::cw::binary::image::path_is_code(arg_path)) {
        data = ::cw::binary::image::code_to_data(arg_path);
        supplied_data = true;
        // Pyのfind_scaledimagepathは画像コードに対して元
        // 倍率1を返す。表示倍率を、符号化された画像の元倍率と取り違えない。
        requested_scale = 1.0;
    }
    auto [resolved, scale] = supplied_data
        ? std::tuple<String, Integer>{arg_path, std::max<Integer>(1, static_cast<Integer>(requested_scale))}
        : find_scaledimagepath(arg_path, requested_scale, arg_can_loaded_scaledimage, arg_noscale);
    if (!supplied_data && data.empty() && !resolved.empty()) {
        if (!read_binary_path(std::filesystem::u8path(resolved), data))
            return std::make_shared<::cw::lib::Surface>();
    }
    if (data.empty()) return std::make_shared<::cw::lib::Surface>();
    ::cw::lib::imagecodec::Info info;
    auto try_decode = [&info](const Bytes& bytes) -> std::shared_ptr<Surface> {
        try {
            const auto decoded = ::cw::lib::imagecodec::decode_rgba(
                std::span<const std::uint8_t>(bytes.data(), bytes.size()), 0,
                ::cw::lib::imagecodec::DecodeProfile::PygameSurface);
            auto result = std::make_shared<Surface>();
            result->size = {decoded.width, decoded.height};
            result->rgba = decoded.pixels;
            // Pyは通常のRGB画像を読み込むとconvert()で変換する
            // が、WebP/Pillowの経路は明示的にRGBAを生成する。
            info = ::cw::lib::imagecodec::inspect(bytes);
            result->has_pixel_alpha = decoded.source_has_pixel_alpha || info.format == ::cw::lib::imagecodec::Format::WebP;
            result->pixel_bits = decoded.source_pixel_bits;
            result->has_colorkey = decoded.source_has_colorkey;
            result->colorkey = decoded.source_colorkey;
            if (decoded.source_frame) {
                const auto rect = *decoded.source_frame;
                if (rect.x >= 0 && rect.y >= 0 && rect.width > 0 && rect.height > 0 &&
                    rect.x + rect.width <= result->size.width && rect.y + rect.height <= result->size.height) {
                    Bytes pixels(static_cast<std::size_t>(rect.width * rect.height) * 4U);
                    for (Integer y = 0; y < rect.height; ++y)
                        std::copy_n(result->rgba.begin() + ((rect.y + y) * result->size.width + rect.x) * 4,
                            static_cast<std::size_t>(rect.width) * 4U, pixels.begin() + y * rect.width * 4);
                    result->size = {rect.width, rect.height};
                    result->rgba = std::move(pixels);
                }
            }
            if (result->has_colorkey && !result->has_pixel_alpha) {
                // SDL3の合成済みGIFはアルファ0の画素で透過色キーのRGB
                // を隠す。Pyはconvert()までパレットの色を保持する。
                const auto [r, g, b] = result->colorkey;
                for (std::size_t i = 0; i + 3 < result->rgba.size(); i += 4)
                    if (result->rgba[i + 3] == 0) {
                        result->rgba[i] = static_cast<std::uint8_t>(r);
                        result->rgba[i + 1] = static_cast<std::uint8_t>(g);
                        result->rgba[i + 2] = static_cast<std::uint8_t>(b);
                    }
            }
            return result;
        } catch (const ::cw::lib::imagecodec::Error&) {
            return {};
        }
    };
    const auto original_data = data;
    const auto ext = get_imageext(data);
    if (ext == ".bmp") {
        data = ::cw::image::patch_rle4bitmap(std::move(data));
        if (!supplied_data) {
            auto [fixed, unchanged] = ::cw::image::fix_cwnext32bitbitmap(std::move(data));
            (void)unchanged;
            data = std::move(fixed);
        }
    }
    auto surface = try_decode(data);
    if (!surface && arg_retry) {
        try {
            auto repaired = original_data;
            auto [fixed32, unused32] = ::cw::image::fix_cwnext32bitbitmap(std::move(repaired));
            (void)unused32;
            repaired = std::move(fixed32);
            auto [fixed16, unused16] = ::cw::image::fix_cwnext16bitbitmap(repaired);
            (void)unused16;
            if (const auto* bytes = std::get_if<Bytes>(&fixed16.storage())) repaired = *bytes;
            if (get_imageext(repaired) == ".bmp") repaired = ::cw::image::patch_rle4bitmap(std::move(repaired));
            surface = try_decode(repaired);
            if (surface) data = std::move(repaired);
        } catch (...) {
        }
    }
    if (!surface) {
        try {
            surface = load_image_load_pillow_surface(data);
        } catch (const ::cw::lib::imagecodec::Error& error) {
#if defined(CARDWIRTH_LOG_ENABLED)
            ::cardwirth::debug::log(String("Image decode failed: ") + error.what());
#endif
            // Pyの読み込み処理は、復号や再試行に失敗するとSurface((0, 0))を返す。
            return std::make_shared<Surface>();
        }
    }
    if (surface) {
        surface->scr_scale = scale > 0 ? static_cast<Real>(scale) : 1.0;
        surface->bmpdepthis1 = ::cw::image::get_bmpdepth_view(data) == 1;
    }
    if (surface) {
        if (info.format == ::cw::lib::imagecodec::Format::Unknown) {
            try { info = ::cw::lib::imagecodec::inspect(data); }
            catch (const ::cw::lib::imagecodec::Error&) {
                // 未対応のデータには既存の代替サーフェスを使う。
            }
        }
        if (info.format == ::cw::lib::imagecodec::Format::BMP)
            surface = ::cw::imageretouch::patch_alphadata(surface, ".bmp", data);
    }
    bool applied_mask = surface && surface->has_colorkey;
    auto mask_color = surface ? surface->colorkey : std::tuple<Integer, Integer, Integer>{};
    bool is_png = false;
    if (surface) {
        const auto format = info.format;
        is_png = format == ::cw::lib::imagecodec::Format::PNG;
        const bool is_gif = format == ::cw::lib::imagecodec::Format::GIF;
        const bool is_bmp = format == ::cw::lib::imagecodec::Format::BMP;
        const auto bmpdepth = is_bmp ? ::cw::image::get_bmpdepth_view(data) : 0;
        if ((bmpdepth == 0 || bmpdepth == 32) && surface->has_pixel_alpha) {
            // Pyのconvert_alphaは透過色キーがあればRGBAへ反映する。
            if (applied_mask) {
                const auto [r, g, b] = mask_color;
                ::cw::lib::simd_image::colorkey_alpha_zero(surface->rgba.data(), surface->rgba.size() / 4U,
                    static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g), static_cast<std::uint8_t>(b), true);
            }
            applied_mask = false;
            surface->pixel_bits = 32;
        } else {
            // RGB形式へ変換する前に、issue #723のパ
            // レット背景色キーとPyの1.30互換条件を保つ。
            if (surface->pixel_bits <= 8 && applied_mask && !is_gif && arg_isback)
                arg_mask = false;
            if (format == ::cw::lib::imagecodec::Format::JPEG && arg_isback) {
                bool old_scenario = false;
                if (::cw::g_cwpy && ::cw::g_cwpy->m_sct) {
                    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_sdata.storage());
                    const auto sdata = object ? std::dynamic_pointer_cast<::cw::data::SystemData>(*object) : nullptr;
                    if (sdata) old_scenario = ::cw::g_cwpy->m_sct->lessthan("1.30", sdata->get_versionhint(0));
                }
                if (!old_scenario) arg_mask = false;
            }
            const bool palette_gif = is_gif && surface->pixel_bits <= 8;
            if (!(is_bmp && bmpdepth == 16)) surface->pixel_bits = 32;
            surface->has_pixel_alpha = false;
            ::cw::lib::simd_image::force_alpha(surface->rgba.data(), surface->rgba.size() / 4U, 255);
            if (applied_mask && is_png && !arg_isback) applied_mask = false;
            if (arg_mask && ((!applied_mask) || (applied_mask && palette_gif))) {
                // Pyは明示的な座標に加えcenter/rightを受け付ける。倍率付きスキン
                // 素材でも同じ透過色キーになるよう、拡大縮小前の復号済み画像で座標を解決する。
                const auto [mx, my] = arg_named_maskpos
                    ? convert_maskpos(::cw::lib::Value(*arg_named_maskpos), surface->size.width, surface->size.height)
                    : arg_maskpos;
                if (mx >= 0 && my >= 0 && mx < surface->size.width && my < surface->size.height) {
                    const auto index = static_cast<std::size_t>((my * surface->size.width + mx) * 4);
                    if (index + 2 < surface->rgba.size()) {
                        mask_color = {surface->rgba[index], surface->rgba[index + 1], surface->rgba[index + 2]};
                        applied_mask = true;
                    }
                }
            }
        }
    }
    if (surface) {
        // Pythonは1ビットBMPかつマスクあり、またはup_scr != 1の場合、Depth1Surfaceで包む。
        const bool wrap_depth1 =
            (!is_png && surface->bmpdepthis1 && arg_mask && !arg_isback) || (scale != 1);
        ::cw::imageretouch::set_surface_metadata(surface, {
            applied_mask, mask_color, wrap_depth1,
            surface->scr_scale > 0.0 ? surface->scr_scale : 1.0});
        if (applied_mask && !surface->has_pixel_alpha) {
            // 格納方式による意図的な差異。PyのRGBサーフェスは透過色キ
            // ーを保持するが、ネイティブ側の描画・転送処理は詰め込まれたRGBA
            // も使う。RGBとRGB・透過色キーのメタデータを残しつつ、
            // キーの画素をアルファ0で表し、共通描画で透過を維持する。
            const auto [r, g, b] = mask_color;
            ::cw::lib::simd_image::colorkey_alpha_zero(surface->rgba.data(), surface->rgba.size() / 4U,
                static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g), static_cast<std::uint8_t>(b), false);
        }
    }
    return surface;
}

std::shared_ptr<::cw::lib::Surface> load_image_load_pillow_surface(::cw::lib::Bytes arg_data)
{
    // imagecodecはPNG/BMP/JPEG/GIF/WebP用のPillow互換バ
    // ックエンド。アニメーション入力では、このAPIと同様に先頭フレームのRGBAを返す。
    return surface_from_bytes(arg_data);
}

::cw::lib::Integer calc_imagesize(std::shared_ptr<::cw::lib::Surface> arg_image)
{
    if (!arg_image) return 0;
    // Pythonはbitsize * width * height // 8で求める。C++のサー
    // フェスは32ビットRGBAなので、バイト数はrgba.size()であり、その8倍ではない。
    if (!arg_image->rgba.empty()) return static_cast<::cw::lib::Integer>(arg_image->rgba.size());
    return arg_image->size.width * arg_image->size.height * 4;
}

::cw::lib::Integer calc_wxbmpsize(std::shared_ptr<::cw::lib::Bitmap> arg_wxbmp)
{
    return calc_imagesize(arg_wxbmp);
}

std::shared_ptr<::cw::lib::Surface> put_number(std::shared_ptr<::cw::lib::Surface> arg_image, ::cw::lib::Integer arg_num)
{
    if (!arg_image) return arg_image;
    // Pyは別サーフェスへ描画する前にconvert_alpha()を呼
    // ぶ。共通の変換処理を使い、無効化されたRGBの不透明度と形式メタデ
    // ータを、他のconvert_alpha呼び出しと同じ規則で扱う。
    auto image = ::cw::lib::convert_surface_alpha(arg_image);
    image->known_opaque = false;
    const auto text = std::to_string(static_cast<long long>(arg_num));
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_rsrc)
        throw std::logic_error("put_number requires initialized engine resources");
    const auto* table_object = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_rsrc->m_fonts.storage());
    const auto table = table_object ? std::dynamic_pointer_cast<::cw::setting::ResourceTable>(*table_object) : nullptr;
    if (!table) throw std::runtime_error("status font table is unavailable");
    const auto font_value = table->get_item(::cw::lib::Value(text.size() == 1 ? "statusimg1" : text.size() == 2 ? "statusimg2" : "statusimg3"));
    const auto* font_object = std::get_if<::cw::lib::ObjectPtr>(&font_value.storage());
    const auto font = font_object ? std::dynamic_pointer_cast<::cw::imageretouch::Font>(*font_object) : nullptr;
    if (!font) throw std::runtime_error("status font resource is unavailable");
    const Integer height = font->get_height();
    const Integer advance = (height + 1) / 2;
    const Integer x = image->size.width - static_cast<Integer>(text.size()) * advance -
        value_integer(::cw::s(::cw::lib::Value(1)), 0);
    const Integer y = image->size.height - height;
    const bool antialias = 2.0 <= ::cw::g_UP_SCR;
    const std::array<std::pair<Integer, Integer>, 8> offsets{{
        {1, 1}, {1, -1}, {-1, 1}, {-1, -1}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
    for (std::size_t i = 0; i < text.size(); ++i) {
        const Integer px = x + static_cast<Integer>(i) * advance;
        auto glyph = font->render(String(1, text[i]), antialias, {0, 0, 0});
        for (const auto& [ox, oy] : offsets)
            ::cw::image::blit_surface_pixels(image, glyph, px + ox, y + oy);
        glyph = font->render(String(1, text[i]), antialias, {255, 255, 255});
        ::cw::image::blit_surface_pixels(image, glyph, px, y);
    }
    return image;
}

::cw::lib::String get_imageext(::cw::lib::Bytes arg_b)
{
    // Pyは切り詰められたヘッダやPNGデータ中の任意の位置にあるacTL
    // マーカーも含め、シグネチャで判定する。検証はデコーダが担う。
    if (arg_b.size() >= 25 && arg_b[0] == 0x89 && arg_b[1] == 'P' && arg_b[2] == 'N' && arg_b[3] == 'G') {
        const std::array<std::uint8_t, 4> marker{'a', 'c', 'T', 'L'};
        if (std::search(arg_b.begin(), arg_b.end(), marker.begin(), marker.end()) != arg_b.end()) return ".apng";
        return ".png";
    }
    if (arg_b.size() >= 23 && arg_b[0] == 'B' && arg_b[1] == 'M') return ".bmp";
    if (arg_b.size() >= 10 && arg_b[0] == 'G' && arg_b[1] == 'I' && arg_b[2] == 'F') return ".gif";
    if (arg_b.size() >= 6 && arg_b[0] == 0xff && arg_b[1] == 0xd8) return ".jpg";
    if (arg_b.size() >= 12 && arg_b[0] == 'R' && arg_b[1] == 'I' && arg_b[2] == 'F' && arg_b[3] == 'F' &&
        arg_b[8] == 'W' && arg_b[9] == 'E' && arg_b[10] == 'B' && arg_b[11] == 'P') return ".webp";
    if (arg_b.size() >= 10 && ((arg_b[0] == 'I' && arg_b[1] == 'I' && arg_b[2] == 42) ||
                              (arg_b[0] == 'M' && arg_b[1] == 'M' && arg_b[3] == 42))) return ".tiff";
    return {};
}

std::map<std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::String, ::cw::lib::String>>, std::vector<std::vector<std::shared_ptr<::cw::image::ImageInfo>>>> get_facepaths(::cw::lib::String arg_sexcoupon, ::cw::lib::String arg_agecoupon, bool arg_adddefaults, bool include_all_folders)
{
    FaceMap result;
    std::vector<std::tuple<Integer, String, String>> dpaths;
    String sex;
    for (const auto& feature : ::cw::setting::Setting::s_sexes) {
        if (feature && arg_sexcoupon == "＿" + feature->m_name) { sex = feature->m_subname; break; }
    }
    String age;
    for (const auto& feature : ::cw::setting::Setting::s_periods) {
        if (feature && arg_agecoupon == "＿" + feature->m_name) { age = feature->m_abbr; break; }
    }
    const auto setting = ::cw::g_cwpy ? ::cw::g_cwpy->m_setting : nullptr;
    const String skin_root = setting && !setting->m_skindir.empty() ? setting->m_skindir : "Data/Skin/Classic";
    const String facedir1 = join_paths({::cw::lib::Value(skin_root), ::cw::lib::Value("Face")});
    const String facedir2 = "Data/Face";
    auto add = [&](Integer weight, const String& dpath1, const String& face_root, const String& root_name) {
        const auto dpath = join_paths({::cw::lib::Value(face_root), ::cw::lib::Value(dpath1)});
        dpaths.emplace_back(weight, "<" + root_name + "> " + dpath1, dpath);
    };
    auto add_root = [&](Integer root_weight, const String& face_root, const String& root_name) {
        if (!sex.empty() && !age.empty()) add(root_weight + 0, sex + "-" + age, face_root, root_name);
        if (!sex.empty()) add(root_weight + 1, sex, face_root, root_name);
        if (!age.empty()) add(root_weight + 2, "Common-" + age, face_root, root_name);
        add(root_weight + 3, "Common", face_root, root_name);
    };
    add_root(0, facedir1, setting ? ::cw::setting::Setting::s_skinname : String("Skin"));
    String common = "共通";
    // フィールドのみを使う呼び出し元や起動処理には、メッセージ表がまだない場合があ
    // る。その場合は日本語の既定値を保ち、稼働中のセッションでは共通の翻訳を使う。
    if (::cw::g_cwpy) if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_msgs.storage()); object && *object)
        if (const auto messages = std::dynamic_pointer_cast<::cw::setting::MsgDict>(*object)) common = messages->get("common", common);
    add_root(10, facedir2, common);
    std::set<String> passed;
    _get_facepaths(facedir2, result, dpaths, passed);
    if (include_all_folders) {
        // Pythonとの意図的な差異。顔画像の手動選択では、Face
        // 内の任意のサブフォルダとFace直下のファイルも表示する。元
        // の絞り込み済み候補を先に置き、共有の訪問済みパスでグループの
        // 重複とシンボリックリンクの循環を防ぐ。ランダム選択は既定値false
        // を使うため、性別・年齢による絞り込みは変わらない。
        const auto scan_root = [&](Integer weight, const String& root, const String& label) {
            collect_facepaths(root, result, {std::make_tuple(weight, label, root)}, passed, root);
        };
        scan_root(4, facedir1, "<" + (setting ? ::cw::setting::Setting::s_skinname : String("Skin")) + ">");
        scan_root(14, facedir2, "<" + common + ">");
    }
    if (result.empty() && arg_adddefaults) {
        const auto carddir = join_paths({::cw::lib::Value(skin_root), ::cw::lib::Value("Resource/Image/Card")});
        std::vector<std::vector<std::shared_ptr<::cw::image::ImageInfo>>> defaults;
        if (const auto* feature = [&]() -> const ::cw::features::Sex* {
                for (const auto& item : ::cw::setting::Setting::s_sexes) {
                    if (item && arg_sexcoupon == "＿" + item->m_name) return item.get();
                }
                return nullptr;
            }()) {
            // Pyはfind_resourceで接尾辞とSkinBaseを解決
            // し、存在しない場合も各親を別々の選択可能な顔画像として保持する。
            const auto add_default = [&](const String& name) {
                const auto path = find_resource(join_paths({::cw::lib::Value(carddir), ::cw::lib::Value(name)}), ::cw::g_M_IMG);
                defaults.push_back({make_image_info(std::filesystem::u8path(path))});
            };
            if (feature->m_father) add_default("FATHER");
            if (feature->m_mother) add_default("MOTHER");
        }
        if (!defaults.empty()) result[std::make_tuple(0, carddir, "Resource/Image/Card")] = std::move(defaults);
    }
    return result;
}

void _get_facepaths(::cw::lib::String arg_facedir, std::map<std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::String, ::cw::lib::String>>, std::vector<std::vector<std::shared_ptr<::cw::image::ImageInfo>>>>& arg_imgpaths, std::vector<std::tuple<::cw::lib::Integer, ::cw::lib::String, ::cw::lib::String>> arg_dpaths, std::set<::cw::lib::String>& arg_passed)
{
    collect_facepaths(arg_facedir, arg_imgpaths, arg_dpaths, arg_passed, arg_facedir);
}

bool is_face_variant_imageinfos(std::vector<std::shared_ptr<::cw::image::ImageInfo>> arg_infos)
{
    if (!face_variants_enabled() || arg_infos.empty()) return false;
    std::optional<String> base_name;
    for (const auto& info : arg_infos) {
        if (!info || info->m_pcnumber != 0) return false;
        const auto filename = ::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(image_info_path(info)).filename());
        const auto stem = std::get<0>(splitext(filename));
        if (stem.empty()) return false;
        const auto dot = stem.find('.');
        const auto current = stem.substr(0, dot == String::npos ? stem.size() : dot);
        if (!base_name) base_name = current;
        if (*base_name != current) return false;
    }
    return true;
}

static std::optional<std::size_t> face_scale_suffix_start(const String& stem)
{
    // Pythonの顔画像選択の正規表現はUnicodeのNd数字と\Zを使い、
    // .は改行に一致しない。Engineの固定のSCALE_LISTとは異なる。
    const auto dot = stem.rfind('.');
    if (dot == String::npos || dot + 2 >= stem.size() ||
        (stem[dot + 1] != 'x' && stem[dot + 1] != 'X') ||
        stem.find('\n', 0) != String::npos) return std::nullopt;
    const auto utf16 = ::cw::lib::encoding::utf8_to_utf16(stem.substr(dot + 2),
        ::cw::lib::encoding::ByteOrder::Little);
    for (std::size_t i = 0; i < utf16.size(); i += 2) {
        std::uint32_t code = static_cast<unsigned char>(utf16[i]) |
            (static_cast<unsigned char>(utf16[i + 1]) << 8);
        if (code >= 0xd800 && code <= 0xdbff) {
            const auto low = static_cast<unsigned char>(utf16[i + 2]) |
                (static_cast<unsigned char>(utf16[i + 3]) << 8);
            code = 0x10000 + ((code - 0xd800) << 10) + low - 0xdc00;
            i += 2;
        }
        if (!::cw::lib::python_decimal_digit(code)) return std::nullopt;
    }
    return dot;
}

std::optional<::cw::lib::String> _extract_base_for_matching(::cw::lib::String arg_fname)
{
    auto path = std::filesystem::u8path(arg_fname);
    const auto ext = path.extension();
    if (ext.empty()) return std::nullopt;
    path.replace_extension();
    auto stem = ::cw::lib::encoding::path_to_utf8(path);
    if (const auto suffix = face_scale_suffix_start(stem)) stem.resize(*suffix);
    if (stem.empty() || stem.find('.') != String::npos) return std::nullopt;
    return stem;
}

std::vector<std::shared_ptr<::cw::image::ImageInfo>> pick_up_basename_image(std::vector<std::shared_ptr<::cw::image::ImageInfo>> arg_infos, bool arg_skip_xN)
{
    if (arg_infos.empty()) return {};
    std::vector<std::shared_ptr<::cw::image::ImageInfo>> source_infos;
    if (arg_skip_xN) {
        for (const auto& info : arg_infos) {
            if (!info) continue;
            const auto name = ::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(image_info_path(info)).filename());
            if (image_info_path(info).empty()) continue;
            if (!pick_up_basename_image__is_scaled_fname(name)) source_infos.push_back(info);
        }
        if (source_infos.empty()) source_infos = arg_infos;
    } else {
        source_infos = arg_infos;
    }
    std::map<String, std::vector<std::shared_ptr<::cw::image::ImageInfo>>> by_base;
    for (const auto& info : source_infos) {
        if (!info) continue;
        const auto path = image_info_path(info);
        const auto name = ::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(path).filename());
        const auto base = _extract_base_for_matching(name);
        if (base) by_base[*base].push_back(info);
    }
    if (by_base.size() == 1) return by_base.begin()->second;
    if (arg_skip_xN && source_infos != arg_infos) return source_infos;
    return arg_infos;
}

bool pick_up_basename_image__is_scaled_fname(::cw::lib::String arg_fname)
{
    auto path = std::filesystem::u8path(arg_fname);
    path.replace_extension();
    return face_scale_suffix_start(::cw::lib::encoding::path_to_utf8(path)).has_value();
}

std::vector<std::vector<std::shared_ptr<::cw::image::ImageInfo>>> pick_up_message_imgpaths(std::vector<std::vector<std::shared_ptr<::cw::image::ImageInfo>>> arg_imgpaths, bool arg_skip_xN)
{
    (void)arg_skip_xN;
    if (!face_variants_enabled() || arg_imgpaths.empty()) return arg_imgpaths;
    std::set<String> base_names;
    for (const auto& group : arg_imgpaths) {
        if (group.empty() || !group.front()) continue;
        const auto filename = ::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(image_info_path(group.front())).filename());
        const auto dot = filename.find('.');
        const auto base = filename.substr(0, dot == String::npos ? filename.size() : dot);
        if (!base.empty()) base_names.insert(base);
    }
    if (base_names.size() != 1) return arg_imgpaths;
    const auto common = *base_names.begin();
    for (const auto& group : arg_imgpaths) {
        if (group.empty() || !group.front()) continue;
        const auto [stem, ext] = splitext(::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(image_info_path(group.front())).filename()));
        if (stem == common + ".message") return {group};
    }
    for (const auto& group : arg_imgpaths) {
        if (group.empty() || !group.front()) continue;
        const auto [stem, ext] = splitext(::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(image_info_path(group.front())).filename()));
        if (stem == common) return {group};
    }
    return arg_imgpaths;
}

::cw::lib::Integer load_bgm(::cw::lib::String arg_path)
{
    if (!::cw::g_cwpy) throw std::runtime_error("load_bgm requires the CWPy thread");
    if (::cw::g_cwpy->m_rsrc) arg_path = ::cw::g_cwpy->m_rsrc->get_filepath(arg_path);
    std::error_code ec;
    if (!std::filesystem::is_regular_file(std::filesystem::u8path(arg_path), ec)) return -1;
    const auto extension = lower_ascii(std::get<1>(splitext(arg_path)));
    if (extension == ".mpg" || extension == ".mpeg") return 1;
    if (extension == ".ogg" || extension == ".mp3" || extension == ".wav" ||
        extension == ".mid" || extension == ".midi") {
        arg_path = get_soundfilepath("Bgm", ::cw::lib::Value(arg_path));
#if CW_UTIL_HAS_SDL_MIXER
        sdlmixer_init();
        if (g_sdl_mixer) {
            auto* audio = MIX_LoadAudio(g_sdl_mixer, arg_path.c_str(), false);
            if (audio) {
                MIX_DestroyAudio(audio);
                return 0;
            }
        }
#endif
        return 0;
    }
    return -1;
}

std::shared_ptr<::cw::util::SoundInterface> load_sound(::cw::lib::String arg_path)
{
    if (!::cw::g_cwpy) throw std::runtime_error("load_sound requires the CWPy thread");
    if (::cw::g_cwpy->m_rsrc) arg_path = ::cw::g_cwpy->m_rsrc->get_filepath(arg_path);
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_sdata.storage()); object && *object) {
        if (const auto scenario = std::dynamic_pointer_cast<::cw::data::ScenarioData>(*object)) {
            if (const auto found = scenario->m_resource_cache.find(arg_path); found != scenario->m_resource_cache.end()) {
                if (const auto* cached = std::get_if<::cw::lib::ObjectPtr>(&found->second.storage()); cached && *cached) {
                    if (const auto sound = std::dynamic_pointer_cast<SoundInterface>(*cached)) return sound->copy();
                }
            }
        }
    }
    auto result = std::make_shared<SoundInterface>();
    std::error_code ec;
    const auto native_path = std::filesystem::u8path(arg_path);
    const bool mounted = ::cardwirth::platform::is_mounted_user_path(native_path);
    std::optional<std::uintmax_t> resource_size;
    if (mounted) {
        const auto status = ::cardwirth::platform::stat_mounted_user_path(native_path);
        if (!status || !status->is_file()) return result;
        resource_size = status->size;
    } else {
        if (!std::filesystem::is_regular_file(native_path, ec) || ec) return result;
        const auto size = std::filesystem::file_size(native_path, ec);
        if (!ec) resource_size = size;
    }
    result->initialize(std::optional<::cw::lib::Value>(::cw::lib::Value(arg_path)), arg_path, is_midi(arg_path));
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_sdata.storage()); object && *object) {
        if (const auto scenario = std::dynamic_pointer_cast<::cw::data::ScenarioData>(*object)) {
            constexpr Integer max_cache = 64 * 1024 * 1024;
            if (resource_size && *resource_size <= static_cast<std::uintmax_t>(max_cache)) {
                if (scenario->m_resource_cache_size + static_cast<Integer>(*resource_size) > max_cache) {
                    scenario->m_resource_cache.clear();
                    scenario->m_resource_cache_size = 0;
                }
                scenario->m_resource_cache[arg_path] = std::static_pointer_cast<::cw::lib::Object>(result);
                scenario->m_resource_cache_size += static_cast<Integer>(*resource_size);
            }
        }
    }
    // キャッシュするのは音源であり、呼び出し元へ返す可変の再生状態の所有者ではない。
    return result->copy();
}

bool is_midi(::cw::lib::String arg_path)
{
    try {
        if (::cw::g_fsync && ::cw::g_fsync->is_waiting(arg_path)) ::cw::g_fsync->sync();
        // Pythonはファイルシステムへアクセスする前に埋め込みNULを拒否す
        // る。ネイティブAPIはNULより前だけを読む場合があるため、待機中の
        // FileSync処理の完了後に、下の同じ拡張子による代替判定を行う。
        if (arg_path.find('\0') != String::npos) throw std::invalid_argument("embedded null byte in MIDI path");
        const auto path = std::filesystem::u8path(arg_path);
        if (::cardwirth::platform::is_mounted_user_path(path)) {
            const auto status = ::cardwirth::platform::stat_mounted_user_path(path, true);
            if (status && status->is_file() && status->size >= 4) {
                const auto bytes = ::cardwirth::platform::read_mounted_user_file_prefix(path, 4);
                if (bytes) return bytes->size() == 4 && std::memcmp(bytes->data(), "MThd", 4) == 0;
            }
        } else if (std::filesystem::is_regular_file(path) && std::filesystem::file_size(path) >= 4) {
            std::ifstream stream(path, std::ios::binary);
            if (!stream) throw std::runtime_error("cannot open MIDI signature source");
            std::array<char, 4> signature{};
            stream.read(signature.data(), static_cast<std::streamsize>(signature.size()));
            if (stream.bad()) throw std::runtime_error("cannot read MIDI signature");
            return stream.gcount() == 4 && std::memcmp(signature.data(), "MThd", 4) == 0;
        }
    } catch (...) {
        // 一時ファイルではPythonと同じく拡張子による代替判定を行う。
    }
    const auto extension = lower_ascii(std::get<1>(splitext(arg_path)));
    return extension == ".mid" || extension == ".midi";
}

::cw::lib::String get_soundfilepath(::cw::lib::String arg_basedir, ::cw::lib::Value arg_path)
{
    auto path = value_string(arg_path);
    if (path.empty() || !::cw::g_cwpy || !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata) return path;
    const auto& yado = **::cw::g_cwpy->m_ydata;
    if (!resolve_yado_paths(path, yado.m_yadodir, yado.m_tempdir)) return path;
    if (::cardwirth::platform::is_mounted_user_path(std::filesystem::u8path(path))) {
        // BassPlayerはマウントされた音声をメモリから読む。論理
        // パスを返し、暗号化されたUserDataを平文で書き出さない。
        return path;
    }
    const auto temp_root = value_string(::cw::g_tempdir);
    const auto dpath = join_paths({::cw::lib::Value(temp_root), ::cw::lib::Value("Playing"), ::cw::lib::Value(arg_basedir)});
    const auto filename = ::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(path).filename());
    auto destination = dupcheck_plus(join_paths({::cw::lib::Value(dpath), ::cw::lib::Value(filename)}), false, false);
    std::filesystem::create_directories(std::filesystem::u8path(dpath));
    std::error_code ec;
    std::filesystem::copy_file(std::filesystem::u8path(path), std::filesystem::u8path(destination), std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) throw std::filesystem::filesystem_error("get_soundfilepath", std::filesystem::u8path(path), std::filesystem::u8path(destination), ec);
    return destination;
}

void remove_soundtempfile(::cw::lib::String arg_basedir)
{
    if (::cw::g_fsync) ::cw::g_fsync->sync();
    const auto dpath = join_paths({::cw::lib::Value(value_string(::cw::g_tempdir)), ::cw::lib::Value("Playing"), ::cw::lib::Value(arg_basedir)});
    if (::cardwirth::platform::is_mounted_user_path(std::filesystem::u8path(dpath)))
        return; // 暗号化された音声はPlayingへ書き出していない。
    std::error_code ec;
    std::filesystem::remove_all(std::filesystem::u8path(dpath), ec);
    if (ec) throw std::filesystem::filesystem_error("remove_soundtempfile", std::filesystem::u8path(dpath), ec);
    const auto playing = std::filesystem::u8path(join_paths({::cw::lib::Value(value_string(::cw::g_tempdir)), ::cw::lib::Value("Playing")}));
    if (std::filesystem::is_directory(playing, ec) && std::filesystem::is_empty(playing, ec)) std::filesystem::remove(playing, ec);
}

std::vector<::cw::lib::Value> _sorted_by_attr_impl(bool arg_d, std::vector<::cw::lib::Value>& arg_seq, const ::cw::lib::Arguments& arg_attr, std::optional<::cw::lib::Callback> arg_cmpfunc)
{
    auto copy=arg_d?std::vector<::cw::lib::Value>{}:arg_seq;
    auto& target=arg_d?arg_seq:copy;
    std::vector<std::size_t> indices(target.size());
    for (std::size_t i=0;i<indices.size();++i) indices[i]=i;
    // コールバックの実行中はValueの所有権を保つ。Pythonでは比較の途中で
    // 失敗すると部分的に並べ替わるが、ネイティブ版では比較失敗時に入力を変えない。
    std::stable_sort(indices.begin(), indices.end(), [&](std::size_t l,std::size_t r) {
        const auto& lhs=target[l];const auto& rhs=target[r];
        const auto compare_keys = [&](const ::cw::lib::Value& left_key, const ::cw::lib::Value& right_key) {
            if (arg_cmpfunc) return value_integer((*arg_cmpfunc)({left_key, right_key}, {}), 0);
            return value_compare(left_key, right_key);
        };
        return compare_keys(sort_key_for(lhs, arg_attr), sort_key_for(rhs, arg_attr)) < 0;
    });
    auto ordered=target;
    for (std::size_t i=0;i<indices.size();++i) ordered[i]=target[indices[i]];
    target=std::move(ordered);
    // 動的なdフラグでは、入力を変更しない分岐のために結果の所有が必要。
    // 変更可能な公開ラッパーは、呼び出し元のvectorへの参照も返す。
    return target;
}

::cw::lib::Integer cmp(std::optional<::cw::lib::Value> arg_a, std::optional<::cw::lib::Value> arg_b)
{
    // ネイティブ版の意図的な差異。ScenarioSelect::scenario_value_tuple_less
    // がタイトル・作者・ファイル名の生文字列をこの公開cmpへ渡すため、こ
    // こではStringを自然順で比較する。PyはLogicalStrへの
    // 変換をsort_by_attrで行い、単独のcmpではStringを
    // 辞書順で比較する。ネイティブの呼び出し元を補うこの動作を維持するが
    // 、このAPIのString比較はPyの単独のcmpと同等ではない。
    return value_compare(arg_a, arg_b);
}

std::vector<::cw::lib::Value> sorted_by_attr(std::vector<::cw::lib::Value> arg_seq, const ::cw::lib::Arguments& arg_attr)
{
    return _sorted_by_attr_impl(false, arg_seq, arg_attr, std::nullopt);
}

std::vector<::cw::lib::Value>& sort_by_attr(std::vector<::cw::lib::Value>& arg_seq, const ::cw::lib::Arguments& arg_attr)
{
    _sorted_by_attr_impl(true, arg_seq, arg_attr, std::nullopt);
    return arg_seq;
}

std::vector<::cw::lib::Value>& sort_by_filename(std::vector<::cw::lib::Value>& arg_seq, const ::cw::lib::Arguments& arg_attr)
{
    _sorted_by_attr_impl(true, arg_seq, arg_attr,
        [](const ::cw::lib::Arguments& args, const ::cw::lib::KeywordArguments&) {
            if (args.size() < 2) return ::cw::lib::Value(0);
            const auto* left=std::get_if<String>(&args[0].storage());
            const auto* right=std::get_if<String>(&args[1].storage());
            if (!left || !right) throw std::invalid_argument("filename comparison requires string keys");
            return ::cw::lib::Value(sort_by_filename_cmp_fname(*left,*right));
        });
    return arg_seq;
}

::cw::lib::Integer sort_by_filename_cmp_fname(::cw::lib::String arg_a, ::cw::lib::String arg_b)
{
#if defined(_WIN32)
    using LogicalCompare = int(WINAPI*)(LPCWSTR, LPCWSTR);
    HMODULE module = LoadLibraryW(L"Shlwapi.dll");
    if (module) {
        const auto compare = reinterpret_cast<LogicalCompare>(GetProcAddress(module, "StrCmpLogicalW"));
        if (compare) {
            const auto result = compare(wide_from_utf8(arg_a).c_str(), wide_from_utf8(arg_b).c_str());
            FreeLibrary(module);
            return result < 0 ? -1 : result > 0 ? 1 : 0;
        }
        FreeLibrary(module);
    }
#endif
    return natural_compare(arg_a, arg_b);
}

::cw::lib::Value new_order(::cw::lib::Value arg_seq, ::cw::lib::Integer arg_mode)
{
    std::vector<::cw::lib::Value> values;
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&arg_seq.storage()); object && *object) {
        if (const auto sequence = std::dynamic_pointer_cast<::cw::data::VariantSequence>(*object)) values = sequence->m_values;
        else throw std::invalid_argument("new_order: expected iterable sequence");
    } else throw std::invalid_argument("new_order: expected iterable sequence");
    auto order_ref = [](const ::cw::lib::Value& value) -> ::cw::lib::Value* {
        const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
        if (!object || !*object) return nullptr;
        if (const auto header = std::dynamic_pointer_cast<::cw::header::AdventurerHeader>(*object)) return &header->m_order;
        if (const auto header = std::dynamic_pointer_cast<::cw::header::CardHeader>(*object)) return &header->m_order;
        if (const auto header = std::dynamic_pointer_cast<::cw::header::PartyHeader>(*object)) return &header->m_order;
        if (const auto structure = std::dynamic_pointer_cast<::cw::data::StructVal>(*object))
            for(std::size_t i=0;i<structure->m_member_names.size()&&i<structure->m_member_values.size();++i)
                if(structure->m_member_names[i]=="order")return &structure->m_member_values[i];
        return nullptr;
    };
    const auto add_one=[](const ::cw::lib::Value& value)->::cw::lib::Value {
        if(const auto* integer=std::get_if<Integer>(&value.storage())) {
            if(*integer!=std::numeric_limits<Integer>::max())return *integer+1;
            // Pyの整数には上限がないため、符号付き整数をオーバーフローさせず型を拡張する。
            ::cw::lib::DecimalContext context;context.precision=0;
            return Decimal(*integer).add(Decimal(1),context);
        }
        if(const auto* boolean=std::get_if<bool>(&value.storage()))return Integer(*boolean?2:1);
        if(const auto* real=std::get_if<Real>(&value.storage()))return *real+1;
        if(const auto* decimal=std::get_if<Decimal>(&value.storage()))return decimal->add(Decimal(1));
        throw std::invalid_argument("new_order: order does not support addition");
    };
    if (arg_mode == 0) {
        ::cw::lib::Value order(Integer(-1));
        for (const auto& value : values) {
            auto* field=order_ref(value);
            if(!field)throw std::invalid_argument("new_order: missing order attribute");
            // 既存のPython互換の正確な数値比較を再利用する。max(item.order,
            // order)は等値時と順序を決められないNaNの場合にitem側を返す。
            order=numwrap_3(*field,order,*field);
        }
        return add_one(order);
    }
    for (const auto& value : values) {
        auto* field=order_ref(value);
        if(!field)throw std::invalid_argument("new_order: missing order attribute");
        *field=add_one(*field);
    }
    return 0;
}

::cw::lib::String normalize_path(::cw::lib::String arg_path)
{
    if (arg_path.empty()) return {};
    return ::cw::lib::encoding::path_to_utf8(
        std::filesystem::u8path(std::move(arg_path)).lexically_normal());
}

::cw::lib::String join_paths(const ::cw::lib::Arguments& arg_paths)
{
    // Pythonのcw.util.join_paths()は結
    // 合と区切り文字の正規化だけを行い、..を解決しない。XML
    // の相対パスが同じ表記で往復できるよう、その文字列を保つ。
    ::cw::lib::String result;
    for (const auto& value : arg_paths) {
        const auto* part = std::get_if<::cw::lib::String>(&value.storage());
        if (!part || part->empty()) continue;
        if (!result.empty()) result.push_back('/');
        result += *part;
    }
    std::replace(result.begin(), result.end(), '\\', '/');
    while (!result.empty() && result.back() == '/') result.pop_back();
    return result;
}

::cw::lib::String selected_document_path(const ::cw::lib::String& path,const ::cw::lib::String& base)
{
    const auto relative=relpath(path,base);
    // create_fileselectionの文字列としての..とos.sepの結合を調べる条件に合
    // わせる。単独の..は相対パスであり、relpathの接頭辞による高速経路は意図的に表記を保つ。
#if defined(_WIN32)
    const bool external=relative.starts_with("..\\");
#else
    const bool external=relative.starts_with("../");
#endif
    return external?path:relative;
}

::cw::lib::String relpath(::cw::lib::String arg_path, ::cw::lib::String arg_start)
{
    // Pythonの接頭辞による高速経路では、論理パスの表記を保つ。
    if (!arg_start.empty() && arg_start.size() < arg_path.size() && arg_path.rfind(arg_start, 0) == 0) {
        const auto separator = arg_path[arg_start.size()];
        if (separator == '/'
#if defined(_WIN32)
            || separator == '\\'
#endif
        ) return arg_path.substr(arg_start.size() + 1);
    }
    try {
        const auto native_spelling = [](const std::filesystem::path& path) {
            const auto text = path.u8string();
            return String(text.begin(), text.end());
        };
        // Pythonのabspath/relpathは論理名を扱う。filesystem::relative
        // はweakly_canonicalでシンボリックリンクを解決する
        // ため、XMLへ保存する名前が変わり、既存ディレクトリの状態にも依
        // 存する。実パスの包含検査はストレージやアーカイブの境界で行う。
        const auto absolute = std::filesystem::absolute(
            std::filesystem::u8path(arg_path.empty() ? "." : arg_path)).lexically_normal();
        arg_path = native_spelling(absolute);
        const auto start = std::filesystem::absolute(
            std::filesystem::u8path(arg_start.empty() ? "." : arg_start)).lexically_normal();
#if defined(_WIN32)
        // ntpath.relpathは共通部分をnormcase
        // で比較し、残りの対象パス要素の表記は保持する。
        const auto folded = [](const std::filesystem::path& p) {
            return ::cw::lib::encoding::lower(::cw::lib::encoding::path_to_utf8(p));
        };
        if (folded(absolute.root_path()) != folded(start.root_path())) return arg_path;
        std::vector<std::filesystem::path> target_parts, start_parts;
        for (const auto& p : absolute.relative_path()) if (!p.empty()) target_parts.push_back(p);
        for (const auto& p : start.relative_path()) if (!p.empty()) start_parts.push_back(p);
        std::size_t common = 0;
        while (common < target_parts.size() && common < start_parts.size() &&
               folded(target_parts[common]) == folded(start_parts[common])) ++common;
        std::filesystem::path relative;
        for (auto i = common; i < start_parts.size(); ++i) relative /= "..";
        for (auto i = common; i < target_parts.size(); ++i) relative /= target_parts[i];
        return relative.empty() ? "." : native_spelling(relative);
#else
        const auto relative = absolute.lexically_relative(start);
        // Pythonはドライブ違いなどでrelpathに失敗すると
        // 、絶対パスの入力を返す。空の結果で素材の論理名を消さない。
        return relative.empty() ? arg_path : ::cw::lib::encoding::path_to_utf8(relative);
#endif
    } catch (...) {
        return arg_path;
    }
}

static bool python_path_is_absolute(const String& value)
{
#if defined(_WIN32)
    // Python 3.8のntpathは末尾区切りなしのUNC共有をドライブと空
    // の末尾部分へ分割する。ルート相対の/nameや末尾/付きの共有と異なり、isabs()
    // は偽となる。C++のUNC解析ではなく、この実行時の挙動を保つ。
    auto normalized = value;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    if (normalized.starts_with("//") && normalized.size() > 2 && normalized[2] != '/' &&
        !normalized.starts_with("//?/")) {
        const auto server_end = normalized.find('/', 2);
        if (server_end != String::npos && normalized.find('/', server_end + 1) == String::npos) return false;
    }
#endif
    return std::filesystem::u8path(value).has_root_directory();
}

::cw::lib::String validate_filepath(std::optional<::cw::lib::String> arg_fpath)
{
    if (!arg_fpath || arg_fpath->empty()) return {};
    // 埋め込まれた従来形式の画像は、ファイルシステムのパスではなく論理URLのbinaryimage://
    // を使う。Pythonは後でImageInfoが復号できるようこの値を保つ
    // 。Windowsのルートパスとして扱うとメニューカードの画像が失われる。
    if (::cw::binary::image::path_is_code(*arg_fpath)) return *arg_fpath;
    if (python_path_is_absolute(*arg_fpath)) return {};
    const auto normalized = join_paths({::cw::lib::Value(*arg_fpath)});
    const auto lexical = ::cw::lib::encoding::path_to_utf8(
        std::filesystem::u8path(normalized).lexically_normal());
    if (lexical == ".." || lexical.rfind("../", 0) == 0) return {};
    return *arg_fpath;
}

std::vector<::cw::lib::String> validate_filepath_2(std::vector<std::optional<::cw::lib::String>> arg_fpath)
{
    std::vector<::cw::lib::String> result;
    result.reserve(arg_fpath.size());
    for (auto& path : arg_fpath) {
        auto validated = validate_filepath(std::move(path));
        if (!validated.empty()) result.push_back(std::move(validated));
    }
    return result;
}

::cw::lib::Value validate_filepath_3(std::optional<::cw::lib::Value> arg_fpath)
{
    if (!arg_fpath || arg_fpath->is_none()) return String{};
    if (const auto* path = std::get_if<String>(&arg_fpath->storage())) {
        if (::cw::binary::image::path_is_code(*path)) return *path;
        return validate_filepath(*path);
    }
    if (const auto* bytes = std::get_if<Bytes>(&arg_fpath->storage())) return *bytes;
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&arg_fpath->storage()); object && *object) {
        if (const auto sequence = std::dynamic_pointer_cast<::cw::data::VariantSequence>(*object)) {
            auto result = std::make_shared<::cw::data::VariantSequence>();
            for (const auto& item : sequence->m_values) {
                if (const auto* text = std::get_if<String>(&item.storage())) {
                    const auto validated = validate_filepath(*text);
                    if (!validated.empty()) result->m_values.emplace_back(validated);
                }
            }
            return std::static_pointer_cast<::cw::lib::Object>(result);
        }
    }
    return {};
}

::cw::lib::String is_descendant(::cw::lib::String arg_path, ::cw::lib::String arg_start)
{
    if (arg_path.empty() || arg_start.empty()) return {};
    ::cw::lib::Arguments parts{::cw::lib::Value(relpath(std::move(arg_path), std::move(arg_start)))};
    auto relative = join_paths(parts);
    if (python_path_is_absolute(relative) || relative.rfind("../", 0) == 0) return {};
    return relative;
}

std::tuple<::cw::lib::String, ::cw::lib::String> splitext(::cw::lib::String arg_fname)
{
    const auto slash = arg_fname.find_last_of("/\\");
    const auto dot = arg_fname.find_last_of('.');
    const auto component_start = slash == ::cw::lib::String::npos ? 0 : slash + 1;
    const auto first_non_dot = arg_fname.find_first_not_of('.', component_start);
    if (dot != ::cw::lib::String::npos && dot >= component_start &&
        first_non_dot != ::cw::lib::String::npos && first_non_dot < dot)
        return {arg_fname.substr(0, dot), arg_fname.substr(dot)};
    // os.path.splitextの後、Pyはファイル名だけでなく拡張子を除いたパス全体にstartswith('.')
    // を適用する。そのため../cardや.hidden/cardも入れ替え対象となる。
    if (!arg_fname.empty() && arg_fname.front() == '.')
        return {::cw::lib::String{}, std::move(arg_fname)};
    return {std::move(arg_fname), {}};
}

bool str2bool(::cw::lib::String arg_s)
{
    std::transform(arg_s.begin(), arg_s.end(), arg_s.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (arg_s == "true" || arg_s == "1") return true;
    if (arg_s == "false" || arg_s == "0") return false;
    throw std::invalid_argument("str2bool: expected true, false, 1 or 0");
}

::cw::lib::Integer numwrap(::cw::lib::Integer arg_n, ::cw::lib::Integer arg_nmin, ::cw::lib::Integer arg_nmax)
{
    // Pythonは上下限が逆でも下限を先に調べる。この公開入力
    // にstd::clampを使うと、その前提条件に違反する。
    if (arg_n < arg_nmin) return arg_nmin;
    else if (arg_n > arg_nmax) return arg_nmax;
    return arg_n;
}

::cw::lib::Real numwrap_2(::cw::lib::Real arg_n, ::cw::lib::Real arg_nmin, ::cw::lib::Real arg_nmax)
{
    if (arg_n < arg_nmin) return arg_nmin;
    else if (arg_n > arg_nmax) return arg_nmax;
    return arg_n;
}

::cw::lib::Value numwrap_3(::cw::lib::Value arg_n, ::cw::lib::Value arg_nmin, ::cw::lib::Value arg_nmax)
{
    using Value = ::cw::lib::Value;
    using DecimalContext = ::cw::lib::DecimalContext;
    using DecimalInvalidOperation = ::cw::lib::DecimalInvalidOperation;
    // Valueには任意のObjectPtrに対するPythonの比較プロトコルがな
    // い。対応するスカラー型はPythonの順序に従い、順序付けできない組ではネイテ
    // ィブのTypeError相当としてinvalid_argumentを投げる。
    const auto is_numeric = [](const Value& value) {
        const auto& storage = value.storage();
        return std::holds_alternative<bool>(storage) || std::holds_alternative<Integer>(storage) ||
               std::holds_alternative<Real>(storage) || std::holds_alternative<Decimal>(storage);
    };
    const auto exact_decimal = [](const Value& value) -> Decimal {
        const auto& storage = value.storage();
        if (const auto* decimal = std::get_if<Decimal>(&storage)) return *decimal;
        if (const auto* integer = std::get_if<Integer>(&storage)) return Decimal(*integer);
        if (const auto* boolean = std::get_if<bool>(&storage)) return Decimal(*boolean ? 1 : 0);
        const auto real = std::get<Real>(storage);
        // Pythonは丸めた十進表記ではなく、正確な二進浮動小数点値を整数やDecimal
        // と比較する。有限のdoubleはm * 2**eで表せる。
        int exponent = 0;
        const auto fraction = std::frexp(real, &exponent);
        auto result = Decimal(static_cast<Integer>(std::ldexp(fraction, std::numeric_limits<Real>::digits)));
        exponent -= std::numeric_limits<Real>::digits;
        DecimalContext context;
        context.precision = 0;
        auto factor = Decimal(1);
        auto power = Decimal(2);
        for (auto remaining = static_cast<unsigned>(std::abs(exponent)); remaining; remaining >>= 1U) {
            if (remaining & 1U) factor = factor.multiply(power, context);
            if (remaining > 1U) power = power.multiply(power, context);
        }
        return exponent < 0 ? result.divide(factor, context) : result.multiply(factor, context);
    };
    const auto compare = [&](const Value& left, const Value& right) -> std::optional<int> {
        if (is_numeric(left) && is_numeric(right)) {
            const auto* lreal = std::get_if<Real>(&left.storage());
            const auto* rreal = std::get_if<Real>(&right.storage());
            if ((lreal && std::isnan(*lreal)) || (rreal && std::isnan(*rreal))) {
                if (left.is_decimal() || right.is_decimal())
                    throw DecimalInvalidOperation("numwrap: unordered Decimal comparison");
                return std::nullopt;
            }
            if (lreal && rreal) return *lreal < *rreal ? -1 : (*lreal > *rreal ? 1 : 0);
            if (lreal && std::isinf(*lreal)) return *lreal < 0 ? -1 : 1;
            if (rreal && std::isinf(*rreal)) return *rreal < 0 ? 1 : -1;
            return exact_decimal(left).compare(exact_decimal(right));
        }
        if (const auto* lstring = std::get_if<String>(&left.storage())) {
            if (const auto* rstring = std::get_if<String>(&right.storage()))
                return *lstring < *rstring ? -1 : (*lstring > *rstring ? 1 : 0);
        }
        if (const auto* lbytes = std::get_if<Bytes>(&left.storage())) {
            if (const auto* rbytes = std::get_if<Bytes>(&right.storage()))
                return *lbytes < *rbytes ? -1 : (*lbytes > *rbytes ? 1 : 0);
        }
        throw std::invalid_argument("numwrap: values do not support ordering");
    };
    if (const auto ordering = compare(arg_n, arg_nmin); ordering && *ordering < 0) return arg_nmin;
    else if (const auto ordering = compare(arg_n, arg_nmax); ordering && *ordering > 0) return arg_nmax;
    return arg_n;
}

::cw::lib::Integer div_vocation(::cw::lib::Integer arg_value)
{
    // Pythonの//は小さい方へ丸める除算。C++の/は0方向
    // へ切り捨てるため、負の奇数の適性値では結果が1大きくなる。
    if (arg_value < 0) {
        const auto n = arg_value + 2;
        return n >= 0 ? n / 2 : (n - 1) / 2;
    }
    // 固定幅整数をオーバーフローさせず、Pythonの(value + 1) // 2を再現する。
    return arg_value / 2 + arg_value % 2;
}

::cw::lib::String get_truetypefontname(::cw::lib::String arg_path)
{
    std::ifstream stream(std::filesystem::u8path(arg_path), std::ios::binary);
    if (!stream) throw std::runtime_error("get_truetypefontname: cannot open " + arg_path);
    auto read_u16 = [&]() -> std::uint16_t {
        std::array<unsigned char, 2> data{};
        stream.read(reinterpret_cast<char*>(data.data()), 2);
        if (stream.gcount() != 2) throw std::out_of_range("get_truetypefontname: truncated font");
        return static_cast<std::uint16_t>((data[0] << 8U) | data[1]);
    };
    auto read_u32 = [&]() -> std::uint32_t {
        std::array<unsigned char, 4> data{};
        stream.read(reinterpret_cast<char*>(data.data()), 4);
        if (stream.gcount() != 4) throw std::out_of_range("get_truetypefontname: truncated font");
        return (static_cast<std::uint32_t>(data[0]) << 24U) | (static_cast<std::uint32_t>(data[1]) << 16U) |
               (static_cast<std::uint32_t>(data[2]) << 8U) | data[3];
    };
    (void)read_u32();
    const auto num_tables = read_u16();
    (void)read_u16(); (void)read_u16(); (void)read_u16();
    // C++の例外型はそれぞれPythonのstruct.error、アサーシ
    // ョン、ループの束縛不在に対応する。解析時の失敗を空文字列へ置き換えない。
    if (num_tables == 0) throw std::logic_error("get_truetypefontname: empty font directory");
    std::uint32_t name_offset = 0;
    std::uint32_t name_length = 0;
    bool found_name = false;
    for (std::uint16_t i = 0; i < num_tables; ++i) {
        std::array<char, 4> tag{};
        stream.read(tag.data(), 4);
        if (stream.gcount() != 4) throw std::out_of_range("get_truetypefontname: truncated directory");
        (void)read_u32();
        const auto offset = read_u32();
        const auto length = read_u32();
        if (std::memcmp(tag.data(), "name", 4) == 0) {
            name_offset = offset; name_length = length; found_name = true;
            break;
        }
    }
    if (!found_name) throw std::invalid_argument("get_truetypefontname: name table not found");
    // Pythonのread(length)は残っている分を返す。偽装されたuint32
    // の表サイズでメモリを確保せず、実際のファイルの残りバイト数で制限する。
    stream.seekg(0, std::ios::end);
    const auto end = stream.tellg();
    if (end < 0) throw std::runtime_error("get_truetypefontname: cannot seek font");
    const auto available = static_cast<std::uint64_t>(end) > name_offset ?
        static_cast<std::uint64_t>(end) - name_offset : 0;
    const auto table_length = std::min<std::uint64_t>(name_length, available);
    stream.seekg(static_cast<std::streamoff>(name_offset));
    Bytes table(static_cast<std::size_t>(table_length));
    stream.read(reinterpret_cast<char*>(table.data()), static_cast<std::streamsize>(table.size()));
    table.resize(static_cast<std::size_t>(stream.gcount()));
    const auto u16 = [&](std::size_t offset) -> std::uint16_t {
        if (offset + 2 > table.size()) throw std::out_of_range("get_truetypefontname: invalid name table");
        return static_cast<std::uint16_t>((table[offset] << 8U) | table[offset + 1]);
    };
    const auto format = u16(0);
    (void)format;
    const auto count = u16(2);
    const auto string_offset = u16(4);
    String fontname;
    for (std::uint16_t i = 0; i < count; ++i) {
        const std::size_t offset = 6U + static_cast<std::size_t>(i) * 12U;
        if (offset + 12U > table.size()) throw std::out_of_range("get_truetypefontname: invalid name record");
        const auto platform = u16(offset);
        const auto encoding = u16(offset + 2U);
        const auto language = u16(offset + 4U);
        const auto name_id = u16(offset + 6U);
        const auto length = u16(offset + 8U);
        const auto value_offset = u16(offset + 10U);
        if (name_id != 4) continue;
        if (static_cast<std::size_t>(string_offset) + value_offset + length > table.size())
            throw std::out_of_range("get_truetypefontname: invalid name value");
        const auto* begin = table.data() + string_offset + value_offset;
        if (platform == 1 && encoding == 0 && language == 0) {
            fontname.assign(reinterpret_cast<const char*>(begin), length);
        } else if (platform == 3 && encoding == 1 && language == 1033) {
            // 元のPyの実装はすべてのNULバイトを除去してから、最後に一度
            // だけ厳密なASCII復号を行う。UTF-16としては復号しない
            // ため、有効なサロゲートペアや非ASCIIの名前でも失敗する。
            fontname.clear();
            for (std::size_t p = 0; p < length; ++p)
                if (begin[p] != 0) fontname.push_back(static_cast<char>(begin[p]));
        }
    }
    for (const unsigned char byte : fontname)
        if (byte >= 0x80U) throw ::cw::lib::encoding::EncodingError("get_truetypefontname: non-ASCII font name");
    return fontname;
}

::cw::lib::Bytes read_file_bytes(const ::cw::lib::String& arg_path)
{
    Bytes data;
    if (!read_binary_path(std::filesystem::u8path(arg_path), data))
        throw std::runtime_error("read_file_bytes: cannot read " + arg_path);
    return data;
}

::cw::lib::String get_md5(::cw::lib::String arg_path)
{
    // Pythonのopenと同じく、埋込みNULで別のファイル名へ切り詰めない。
    if (arg_path.find('\0') != ::cw::lib::String::npos)
        throw std::invalid_argument("get_md5: embedded null byte");
    const auto path = std::filesystem::u8path(arg_path);
    const bool mounted_path = ::cardwirth::platform::is_mounted_user_path(path);
    auto mounted = mounted_path ? ::cardwirth::platform::open_mounted_user_file(path) : nullptr;
    std::ifstream native;
    if (mounted_path) {
        if (!mounted) throw std::runtime_error("get_md5: cannot open " + arg_path);
    } else {
        native.open(path, std::ios::binary);
        if (!native) throw std::runtime_error("get_md5: cannot open " + arg_path);
    }
    // Pyと同じ32768バイト単位で読む。マウント先の提供側は既存の入
    // 力ハンドルと内部のバッファリングを維持する。ここではファイル全体の
    // コピーを追加せず、Web/IndexedDBの格納形式も変えない。
    auto hash = ::cw::lib::HashContext::md5();
    std::array<std::byte, 32768> buffer;
    for (;;) {
        std::size_t count;
        if (mounted) {
            count = mounted->read(buffer);
            if (!mounted->good() || count > buffer.size())
                throw std::runtime_error("get_md5: cannot read " + arg_path);
        } else {
            native.read(reinterpret_cast<char*>(buffer.data()),
                        static_cast<std::streamsize>(buffer.size()));
            if (native.bad() || (native.fail() && !native.eof()))
                throw std::runtime_error("get_md5: cannot read " + arg_path);
            count = static_cast<std::size_t>(native.gcount());
        }
        if (!count) break;
        hash.update(buffer.data(), count);
    }
    return hash.hexdigest();
}

::cw::lib::String get_md5_from_data(const ::cw::lib::Bytes& arg_data)
{
    return md5_hex(arg_data);
}

::cw::lib::Integer number_normalization(::cw::lib::Integer arg_value, ::cw::lib::Integer arg_fromvalue, ::cw::lib::Integer arg_tovalue)
{
    if (arg_tovalue == 0) return arg_value;
    if (arg_value < arg_fromvalue || arg_tovalue <= arg_value) {
        // Pythonの無制限整数では床除算とその積を安全に計算できる
        // 。ここではオーバーフローし得る商や積を使わず、等価な床除算
        // の余りを求める。固定幅整数の最小値を-1で割る場合も含む。
        if (arg_value == std::numeric_limits<Integer>::min() && arg_tovalue == -1) {
            arg_value = 0;
        } else {
            auto remainder = arg_value % arg_tovalue;
            if (remainder != 0 && ((remainder < 0) != (arg_tovalue < 0))) remainder += arg_tovalue;
            arg_value = remainder;
        }
    }
    if (arg_value < arg_fromvalue) {
        // 言語による意図的な差異。Pythonは任意の大きさの整数を返せるが、こ
        // のAPIは未定義動作にせず、表現できない結果をエラーとして報告する。
        if ((arg_tovalue > 0 && arg_value > std::numeric_limits<Integer>::max() - arg_tovalue) ||
            (arg_tovalue < 0 && arg_value < std::numeric_limits<Integer>::min() - arg_tovalue)) {
            throw std::overflow_error("number_normalization: result exceeds Integer range");
        }
        arg_value += arg_tovalue;
    }
    return arg_value;
}

// ネイティブの例外診断は通常の標準出力と混ざらないよう標準エラーへ出す。
// Pythonの任意のTextIO出力先と異なり、このValue引数はス
// トリーム書き込みを公開しない。指定されたファイル名のStringは診断
// の補足だけに使う。Pythonは例外型・値・トレースバックと追加の空行
// を表示するが、ネイティブ側にはPythonのトレースバックオブジェクト
// がないため、std::exception::what()または非標準例
// 外の印と、保持したネイティブの発生元スタックを報告する。処理中の例外が
// ない呼び出しには専用の診断を出す。接頭辞や改行はtraceback.print_exception
// を模倣せず、ネイティブの診断形式に従う。
void print_ex(std::optional<::cw::lib::Value> arg_file)
{
    // Noneは既定のストリームを選ぶ。他のPythonで偽と評価される値は出力を抑制する。
    if (arg_file && !std::holds_alternative<std::monostate>(arg_file->storage()) && !arg_file->as_bool()) return;
    // ネイティブのアーカイブ・リンク・検索設定の例外捕捉箇所は、失敗したパスを渡す
    // 。Stringを診断の補足として受け付ける。PythonではTextIOとし
    // て.write()を呼ぼうとするため、元のエラーを報告せず別の失敗となる。
    if (arg_file && std::holds_alternative<String>(arg_file->storage())) {
        std::cerr << "[cw.util] exception context: " << value_string(*arg_file) << '\n';
    }
    const auto active = ::cw::lib::capture_exception();
    if (!active) {
        std::cerr << "[cw.util] print_ex called without an active exception\n";
        return;
    }
    // Pythonのcw.util.print_ex()は処理中の例
    // 外とトレースバックを表示して呼び出し元へ戻る。UIの復旧経路に
    // はcatchから意図的に呼ぶ箇所があるため、ここで再送出しない
    // 。MSVCのexception_ptrがオブジェクトをコピー
    // ・再送出する可能性があるため、その前に保持した発生元を読む。
    const auto origin_trace = ::cw::thread::current_exception_stacktrace();
    try {
        ::cw::lib::rethrow_exception(active);
    } catch (const std::exception& error) {
        std::cerr << "[cw.util] " << error.what() << "\n"
                  << "[cw.util] Stack trace:\n"
                  << origin_trace;
    } catch (...) {
        std::cerr << "[cw.util] non-standard exception\n"
                  << "[cw.util] Stack trace:\n"
                  << origin_trace;
    }
}


::cw::lib::String screenshot_title(std::map<::cw::lib::String, ::cw::lib::String> arg_titledic)
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_setting) return {};
    return format_title(::cw::g_cwpy->m_setting->m_ssinfoformat, arg_titledic, false);
}

std::tuple<std::shared_ptr<::cw::lib::Surface>, std::shared_ptr<::cw::lib::Surface>, ::cw::lib::Integer, ::cw::lib::Integer> screenshot_header(::cw::lib::String arg_title, ::cw::lib::Integer arg_w)
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_setting || !::cw::g_cwpy->m_rsrc)
        throw std::logic_error("screenshot_header requires initialized engine resources");
    // Pyはwxの描画DCや独自に選んだフォント・既定フォントではなく、遅延取得
    // するrsrc.fonts["screenshot"]を使う。透過した字形
    // の画素をそのままスクリーンショットへ渡す。不透明なビットマップを再着色す
    // ると字形を失う。描画・書体・スタイルの共通規則は既にFontが管理する。
    const auto* table_object = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_rsrc->m_fonts.storage());
    const auto table = table_object ? std::dynamic_pointer_cast<::cw::setting::ResourceTable>(*table_object) : nullptr;
    if (!table) throw std::runtime_error("screenshot font table is unavailable");
    const auto font_value = table->get_item(::cw::lib::Value("screenshot"));
    const auto* font_object = std::get_if<::cw::lib::ObjectPtr>(&font_value.storage());
    const auto font = font_object ? std::dynamic_pointer_cast<::cw::imageretouch::Font>(*font_object) : nullptr;
    if (!font) throw std::runtime_error("screenshot font resource is unavailable");
    const auto& setting = ::cw::g_cwpy->m_setting;
    const auto foreground = value_color(setting->m_ssinfofontcolor, {0, 0, 0, 255});
    const auto background = value_color(setting->m_ssinfobackcolor, {255, 255, 255, 255});
    const Integer fh = std::get<1>(font->size("#"));
    const Integer lh = fh + 2;
    const Integer max_width = arg_w - value_integer(::cw::s(::cw::lib::Value(5)), 0) * 2;
    const auto render = [&](const ::cw::lib::Color& color) {
        auto text = font->render(arg_title, true, {color.r, color.g, color.b});
        if (text && max_width < text->size.width)
            text = ::cw::image::smoothscale(text, {max_width, text->size.height}, true, false);
        return text;
    };
    auto fore = render(foreground);
    auto back = render(background);
    // 丸めも含め、PyのBLEND_RGBA_MULT (255,255,255,80)と同じ処理。
    ::cw::imageretouch::mul_alpha(back, 80);
    return {fore, back, fh, lh};
}

namespace {
// CardWirthPy_C_Allのみの拡張。スクリーンショットは常にUserData/ScreenShot
// へ保存し、設定書式からはファイル名だけを使う。
::cw::lib::String all_screenshot_path(const ::cw::lib::String& path)
{
    if (!::cw::dialog::is_all_mode()) return path;
    const auto name = std::filesystem::u8path(path).filename().u8string();
    return "ScreenShot/" + ::cw::lib::String(name.begin(), name.end());
}

void prepare_screenshot_output(String& output)
{
    const auto directory = std::filesystem::u8path(output).parent_path();
    const bool mounted = ::cardwirth::platform::is_mounted_user_path(directory);
    std::error_code error;
    const auto status = mounted ? ::cardwirth::platform::stat_mounted_user_path(directory)
                                : std::optional<::cardwirth::platform::StorageStat>{};
    const bool exists = mounted ? status && status->is_directory()
                                : std::filesystem::is_directory(directory, error);
    if (exists) output = dupcheck_plus(output, false, true);
    else if (mounted) {
        // 既存のマウント用Storage処理はos.makedirsだけを
        // 置き換える。準備の順序と同名ファイルの扱いは全ホストで共有する。
        if (!::cardwirth::platform::make_mounted_user_directory(directory, true))
            throw std::runtime_error("screenshot: cannot create output directory");
    } else std::filesystem::create_directories(directory);
}

void write_screenshot_image(const std::shared_ptr<Surface>& surface, const String& output)
{
    if (!surface) throw std::invalid_argument("screenshot: expected a Surface");
    const ::cw::lib::imagecodec::RGBAImage image{
        surface->size.width, surface->size.height, surface->rgba};
    const auto extension = lower_ascii(std::get<1>(splitext(output)));
    Bytes bytes;
    if (extension == ".png") bytes = ::cw::lib::imagecodec::encode_png(image);
    else if (extension == ".bmp") bytes = ::cw::lib::imagecodec::encode_bmp(image);
    else if (extension == ".jpg" || extension == ".jpeg")
        bytes = ::cw::lib::imagecodec::encode_jpeg(image, surface->has_pixel_alpha);
    else bytes = ::cw::lib::imagecodec::encode_tga(image, surface->has_pixel_alpha);
    // pygame.image.saveは.gifや.tifを含む残りの拡張子
    // にTGAを使う。既存の共通コーデックと同期write_fileで、通常の
    // 保存先とマウント先を同じように扱う。FileSyncのジョブは使わない。
    write_file(output, ::cw::lib::Value(std::move(bytes)), nullptr, "wb", std::nullopt);
}

void execute_screenshot(bool cards)
{
    const auto runtime = ::cw::g_cwpy;
    // 常にcwpyが初期化済みのPyと異なり、ネイティブ版の初期化ではランタイ
    // ムや設定がない場合を許容する。その場合は既存どおり、安全に何もしない。
    if (!runtime || !runtime->m_setting) return;
    std::thread::id owner;
    { std::scoped_lock lock(runtime->m_game_exec_mutex); owner = runtime->m_game_thread_id; }
    if (owner != std::thread::id{} && owner != std::this_thread::get_id()) {
        // ネイティブのダイアログ補助操作はUIスレッドから入る場合がある。画像取
        // 得、ゲームデータ、call_modaldlgは既存のゲーム実行処理に任
        // せ、ランタイムと選択情報だけを保持し、ダイアログへの参照は残さない。
        runtime->exec_func([runtime, cards](const ::cw::lib::Arguments&, const ::cw::lib::KeywordArguments&) -> ::cw::lib::Value {
            if (::cw::g_cwpy == runtime) execute_screenshot(cards);
            return {};
        });
        return;
    }
    if (cards && (!runtime->m_ydata || !*runtime->m_ydata ||
                  !(*runtime->m_ydata)->m_party || !(*runtime->m_ydata)->m_party.value())) return;
    runtime->play_sound("screenshot", false, 100, 1, 0, 0, false);
    const auto value = runtime->get_titledic(true, true);
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    const auto dictionary = object ? std::dynamic_pointer_cast<::cw::thread::TitleDictionaryPair>(*object)
                                   : nullptr;
    if (!dictionary) throw std::logic_error("screenshot: expected paired title dictionaries");
    auto output = cards ? create_cardscreenshotfilename(dictionary->filename_values)
                        : create_screenshotfilename(dictionary->filename_values);
    // ファイル名・辞書・音声に関する失敗は、Pyと同様に保存用のtryブロックの外に置く。
    try {
        prepare_screenshot_output(output);
        const auto surface = cards ? create_cardscreenshot(dictionary->values)
                                   : std::get<0>(create_screenshot(dictionary->values));
        write_screenshot_image(surface, output);
    } catch (...) {
        print_ex(std::nullopt);
        runtime->call_modaldlg("ERROR", {{"text", ::cw::lib::Value(
            "スクリーンショットの保存に失敗しました。\n" + output)}});
    }
}
}  // namespace

void screenshot()
{
    execute_screenshot(false);
}

::cw::lib::String create_screenshotfilename(std::map<::cw::lib::String, ::cw::lib::String> arg_titledic)
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_setting) return {};
    auto path = format_title(::cw::g_cwpy->m_setting->m_ssfnameformat, arg_titledic, false);
    const auto extension = lower_ascii(std::get<1>(splitext(path)));
    // Pythonとの意図的な差異。共通の画像拡張子表は.tifに
    // 加えて長い表記の.tiffも受け付ける。スクリーンショットの
    // 両ファイル名APIは.pngを追加せず、その表記を保持する。
    if (!tuple_contains(::cw::g_EXTS_IMG, extension)) path += ".png";
    path = all_screenshot_path(path);
    return ::cw::lib::managed_userdata::resolve_output_path(
        path, "ScreenShot/managed-screenshot.png");
}

namespace {
void screenshot_header_background(const std::shared_ptr<Surface>& destination, Integer width, Integer height)
{
    const auto& setting = ::cw::g_cwpy->m_setting;
    auto path = setting->m_ssinfobackimage;
    if (path.empty() || !storage_file_exists(std::filesystem::u8path(path)))
        path = find_resource(join_paths({::cw::lib::Value(::cw::g_cwpy->m_skindir),
            ::cw::lib::Value("Resource/Image/Other/SCREENSHOT_HEADER")}), ::cw::g_M_IMG);
    if (path.empty()) return;
    auto image = load_image(path, false, {0, 0}, std::nullopt, true, false, false,
                            false, std::nullopt, false);
    if (!image) return;
    const auto scaled = ::cw::s(::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(image)));
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&scaled.storage());
    if (object && *object) if (const auto surface = std::dynamic_pointer_cast<Surface>(*object))
        fill_image(destination, surface, {width, height}, {0, 0}, {0, 0});
}
} // namespace

std::tuple<std::shared_ptr<::cw::lib::Surface>, ::cw::lib::Integer> create_screenshot(std::map<::cw::lib::String, ::cw::lib::String> arg_titledic)
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_setting)
        throw std::logic_error("create_screenshot requires an initialized engine");
    const auto& runtime = ::cw::g_cwpy;
    const auto& setting = runtime->m_setting;
    // ネイティブ版のGameAreaOnly拡張は、ヘッダとステータスバーの両
    // 設定に優先する。無効時は元のsswithstatusbarフラグを保つ。
    const bool gamearea_only = setting->m_ss_gamearea_only;
    const auto title = gamearea_only ? String{} : screenshot_title(arg_titledic);
    const bool with_statusbar = !gamearea_only && setting->m_sswithstatusbar;
    const auto scaled = [](Integer value) {
        return value_integer(::cw::s(::cw::lib::Value(value)), 0);
    };
    auto scr = std::make_shared<Surface>();
    // scr_drawからは現在の物理サイズだけを使う。Pythonのdraw_to(scr,
    // False)と同様に、ステータスバーの説明を除いて現在のレイヤーを別のRGB
    // サーフェスへ取得する。表示更新やscr_drawの画素の再利用は行わない。
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&runtime->m_scr_draw.storage()); object && *object)
        if (const auto source = std::dynamic_pointer_cast<Surface>(*object)) scr->size = source->size;
    if (scr->size.width <= 0 || scr->size.height <= 0)
        throw std::logic_error("create_screenshot requires a drawing surface");
    scr->has_pixel_alpha = false;
    scr->rgba.resize(static_cast<std::size_t>(scr->size.width * scr->size.height * 4), 0);
    for (std::size_t i = 3; i < scr->rgba.size(); i += 4) scr->rgba[i] = 255;
    const auto plan = runtime->build_render_plan();
    runtime->compose_to(scr, false, std::nullopt, plan);
    if (title.empty() && with_statusbar) return {scr, 0};
    const Integer width = title.empty() ? scaled(std::get<0>(::cw::g_SIZE_AREA))
                                      : scaled(std::get<0>(::cw::g_SIZE_GAME));
    const Integer content_height = scaled(with_statusbar ? std::get<1>(::cw::g_SIZE_GAME)
                                                         : std::get<1>(::cw::g_SIZE_AREA));
    if (title.empty() && (width > scr->size.width || content_height > scr->size.height))
        throw std::out_of_range("create_screenshot: game area outside drawing surface");
    std::tuple<std::shared_ptr<Surface>, std::shared_ptr<Surface>, Integer, Integer> header_images{};
    if (!title.empty()) header_images = screenshot_header(title, width);
    const Integer lh = title.empty() ? 0 : std::get<3>(header_images);
    auto result = std::make_shared<Surface>();
    result->size = {width, content_height + lh};
    result->has_pixel_alpha = false;
    result->rgba.resize(static_cast<std::size_t>(width * result->size.height * 4), 0);
    for (std::size_t i = 3; i < result->rgba.size(); i += 4) result->rgba[i] = 255;
    if (!title.empty()) {
        const auto base = value_color(setting->m_ssinfobackcolor, {255, 255, 255, 255});
        for (Integer y = 0; y < lh; ++y) for (Integer x = 0; x < width; ++x) {
            const auto i = static_cast<std::size_t>((y * width + x) * 4);
            result->rgba[i] = base.r; result->rgba[i + 1] = base.g; result->rgba[i + 2] = base.b;
        }
        screenshot_header_background(result, width, lh);
    }
    blit_rgba(*scr, *result, 0, lh);
    if (!title.empty()) {
        const auto& [fore, back, fh, header_lh] = header_images;
        const Integer x = scaled(5), y = (header_lh - fh) / 2;
        for (Integer xx = -1; xx <= 1; ++xx) for (Integer yy = -1; yy <= 1; ++yy)
            if (xx != x || yy != y) if (back) alpha_blit_rgba(*back, *result, x + xx, y + yy);
        if (fore) alpha_blit_rgba(*fore, *result, x, y);
    }
    return {result, lh};
}

void card_screenshot()
{
    execute_screenshot(true);
}

::cw::lib::String create_cardscreenshotfilename(std::map<::cw::lib::String, ::cw::lib::String> arg_titledic)
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_setting) return {};
    auto path = format_title(::cw::g_cwpy->m_setting->m_cardssfnameformat, arg_titledic, false);
    const auto extension = lower_ascii(std::get<1>(splitext(path)));
    if (!tuple_contains(::cw::g_EXTS_IMG, extension)) path += ".png";
    path = all_screenshot_path(path);
    return ::cw::lib::managed_userdata::resolve_output_path(
        path, "ScreenShot/managed-card-screenshot.png");
}

std::shared_ptr<::cw::lib::Surface> create_cardscreenshot(std::map<::cw::lib::String, ::cw::lib::String> arg_titledic)
{
    if (!::cw::g_cwpy) throw std::runtime_error("card screenshot requires an active game thread");
    const auto pcards = ::cw::g_cwpy->get_pcards({});
    if (pcards.empty()) throw std::runtime_error("card screenshot requires party members");
    // 技能や個人用の所持上限が拡張されても、スクリーンショットを拡大しな
    // い。最初のページの表示だけを制限し、実際の所持カードは変えない。
    const auto visible_count = [](std::size_t pocket, std::size_t count) -> Integer {
        return static_cast<Integer>((pocket == 0 || pocket == 3)
            ? std::min<std::size_t>(count, 10) : count);
    };
    std::array<Integer, 4> max_cards{2, 2, 2, 0};
    for (const auto& pcard : pcards) if (pcard) {
        max_cards[0] = std::max<Integer>(max_cards[0], visible_count(0, std::get<0>(pcard->m_cardpocket_typed).size()));
        max_cards[1] = std::max<Integer>(max_cards[1], static_cast<Integer>(std::get<1>(pcard->m_cardpocket_typed).size()));
        max_cards[2] = std::max<Integer>(max_cards[2], static_cast<Integer>(std::get<2>(pcard->m_cardpocket_typed).size()));
        if (::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_show_personal_cards)
            max_cards[3] = std::max<Integer>(max_cards[3], visible_count(3, pcard->m_personal_pocket.size()));
    }
    if (max_cards[3] > 0) max_cards[3] = std::max<Integer>(2, max_cards[3]);
    constexpr Integer margin = 2;
    // Pythonはcw.s()を通してすべてを論理画素で配置する
    // 。カード画像は既に画面倍率に合わせてあるため、全矩形とずれ量
    // にも同じ倍率を適用する。倍率を適用しないとカードが重なる。
    const auto s = [](Integer value) {
        return value_integer(::cw::s(::cw::lib::Value(value)), 0);
    };
    const auto title = screenshot_title(arg_titledic);
    const Integer card_total = max_cards[0] + max_cards[1] + max_cards[2] + max_cards[3];
    const Integer width = s(95 + 80 * card_total + margin * (5 + card_total)) +
                          (max_cards[3] ? s(margin) : 0);
    std::tuple<std::shared_ptr<Surface>, std::shared_ptr<Surface>, Integer, Integer> header_images{};
    if (!title.empty()) header_images = screenshot_header(title, width);
    const Integer header_height = title.empty() ? 0 : std::get<3>(header_images);
    const Integer row_height = s(130 + 2 * margin);
    // Pythonは論理的な総高さへ一度だけ倍率を適用する。行ごと
    // の高さを個別に切り捨てて足すと、小数倍率で画素が不足する。
    const Integer height = s((130 + 2 * margin) * static_cast<Integer>(pcards.size())) + header_height;
    auto result = std::make_shared<Surface>();
    // PythonのSurface(...).convert()はRGBとなる。共通のスク
    // リーンショット書き込みがTGA/JPEG形式を選ぶ際も、この公開形式フラグを保つ。
    result->has_pixel_alpha = false;
    result->size = {std::max<Integer>(0, width), std::max<Integer>(0, height)};
    const auto base = ::cw::g_cwpy->m_setting ? value_color(::cw::g_cwpy->m_setting->m_ssinfobackcolor, {255, 255, 255, 255}) : ::cw::lib::Color{255, 255, 255, 255};
    result->rgba.resize(static_cast<std::size_t>(result->size.width * result->size.height * 4));
    const auto fill_rect = [&](Integer left, Integer top, Integer w, Integer h, ::cw::lib::Color color) {
        for (Integer y = std::max<Integer>(0, top); y < std::min(result->size.height, top + h); ++y)
            for (Integer x = std::max<Integer>(0, left); x < std::min(result->size.width, left + w); ++x) {
                const auto i = static_cast<std::size_t>((y * result->size.width + x) * 4);
                result->rgba[i] = color.r; result->rgba[i + 1] = color.g; result->rgba[i + 2] = color.b; result->rgba[i + 3] = 255;
            }
    };
    fill_rect(0, 0, width, height, base);
    if (!title.empty()) {
        screenshot_header_background(result, width, header_height);
        const auto& [fore, back, fh, lh] = header_images;
        const Integer x = s(5), y = (lh - fh) / 2;
        if (back) for (Integer xx = -1; xx <= 1; ++xx) for (Integer yy = -1; yy <= 1; ++yy)
            if (xx != x || yy != y) alpha_blit_rgba(*back, *result, x + xx, y + yy);
        if (fore) alpha_blit_rgba(*fore, *result, x, y);
    }
    const std::array<::cw::lib::Color, 2> tiles{{
        {static_cast<std::uint8_t>(std::clamp<Integer>(base.r / 2 + 88, 0, 255)), static_cast<std::uint8_t>(std::clamp<Integer>(base.g / 2 + 88, 0, 255)), static_cast<std::uint8_t>(std::clamp<Integer>(base.b / 2 + 88, 0, 255)), 255},
        {static_cast<std::uint8_t>(std::clamp<Integer>(base.r / 2 + 40, 0, 255)), static_cast<std::uint8_t>(std::clamp<Integer>(base.g / 2 + 40, 0, 255)), static_cast<std::uint8_t>(std::clamp<Integer>(base.b / 2 + 40, 0, 255)), 255}}};
    Integer sy = header_height;
    for (std::size_t player_index = 0; player_index < pcards.size(); ++player_index, sy += row_height) {
        const auto& pcard = pcards[player_index];
        if (!pcard) continue;
        fill_rect(0, sy, s(95 + 2 * margin), row_height, tiles[(1 + player_index) % 2]);
        if (const auto card_image = pcard->get_cardimg()) if (const auto image = card_image->get_image()) blit_rgba(*image, *result, s(margin), sy + s(margin));
        Integer current_x = 95 + 2 * margin;
        for (std::size_t pocket = 0; pocket < 4; ++pocket) {
            if (max_cards[pocket] == 0) continue;
            const Integer tile_width = 80 * max_cards[pocket] + margin * (max_cards[pocket] + 1);
            fill_rect(s(current_x), sy, s(tile_width), row_height, tiles[(pocket + player_index) % 2]);
            const auto* headers = pocket == 0 ? &std::get<0>(pcard->m_cardpocket_typed) :
                                  pocket == 1 ? &std::get<1>(pcard->m_cardpocket_typed) :
                                  pocket == 2 ? &std::get<2>(pcard->m_cardpocket_typed) : &pcard->m_personal_pocket;
            const Integer count = visible_count(pocket, headers->size());
            const Integer adjust = max_cards[pocket] - count;
            Integer x = current_x + adjust * 40 + margin * (2 + adjust) / 2;
            for (Integer index = 0; index < count; ++index) {
                const auto& header = (*headers)[static_cast<std::size_t>(index)];
                if (header) {
                    // 元の実装と同様に、スクリーンショットでは画
                    // 像取得前に表示ヘッダの選択反転を解除する。
                    header->m_negaflag = false;
                    if (const auto image = header->get_cardimg_2()) blit_rgba(*image, *result, s(x), sy + s(10 + margin));
                }
                x += 80 + margin;
            }
            current_x += tile_width;
        }
    }
    return result;
}

void to_clipboard(::cw::lib::String arg_s)
{
    // この公開の互換用入口は残すが、EngineやExplorer
    // の全ダイアログと同じクリップボード実装を使う。共通層がUI
    // スレッドへの振り分けとWeb転送を担い、Pyのwx補助処理
    // と同様に、クリップボードへの書き込み失敗を例外にしない。
    ::cw::lib::set_system_clipboard_text(arg_s);
}

::cw::lib::String dupcheck_plus(::cw::lib::String arg_path, bool arg_yado, bool arg_checkfilename)
{
    if (::cw::g_fsync) ::cw::g_fsync->sync();
    std::filesystem::path original = std::filesystem::u8path(arg_path);
    auto dir = original.parent_path();
    auto [stem, ext] = splitext(::cw::lib::encoding::path_to_utf8(original.filename()));
    if (arg_checkfilename) stem = ::cw::binary::util::check_filename(::cw::lib::python_strip(stem));
    ext = ::cw::lib::python_strip(ext);
    // checkfilename=falseでは、空文字列や前後の空白を含め、拡張子を除いた名前を保持する。
    const auto make_candidate = [&](const String& name) {
        return std::filesystem::u8path(join_paths({::cw::lib::Value(::cw::lib::encoding::path_to_utf8(dir)), ::cw::lib::Value(name)}));
    };
    auto candidate = make_candidate(stem + ext);
    auto collides = [&](const std::filesystem::path& path) {
        if (storage_path_exists_checked(path)) return true;
        if (arg_yado) {
            const auto text = ::cw::lib::encoding::path_to_utf8(path);
            const auto temp_yado = join_paths({::cw::lib::Value(value_string(::cw::g_tempdir)), ::cw::lib::Value("Yado")});
            auto paths = g_cwpy ? resolve_yado_paths(text, g_cwpy->m_yadodir, g_cwpy->m_tempdir)
                               : std::optional<std::pair<String, String>>{};
            // 作成・転送は宿が有効になる前にも実行される。この名前空間は
            // 保つが、文字列のYado接頭辞ではなくパス要素を比較する。
            if (!paths) paths = resolve_yado_paths(text, "Yado", temp_yado);
            return paths && (storage_path_exists_checked(std::filesystem::u8path(paths->first)) ||
                             storage_path_exists_checked(std::filesystem::u8path(paths->second)));
        }
        return false;
    };
    for (::cw::lib::Integer count = 2; collides(candidate); ++count)
        candidate = make_candidate(stem + "(" + std::to_string(count) + ")" + ext);
    return ::cw::lib::encoding::path_to_utf8(candidate);
}

::cw::lib::String repl_dischar(::cw::lib::String arg_fname)
{
    static const std::vector<std::pair<::cw::lib::String, ::cw::lib::String>> replacements{
        {"\\", "￥"}, {"/", "／"}, {":", "："}, {",", "，"}, {";", "；"},
        {"*", "＊"}, {"?", "？"}, {"\"", "”"}, {"<", "＜"}, {">", "＞"}, {"|", "｜"}};
    for (const auto& [from, to] : replacements) {
        std::size_t pos = 0;
        while ((pos = arg_fname.find(from, pos)) != ::cw::lib::String::npos) {
            arg_fname.replace(pos, from.size(), to);
            pos += to.size();
        }
    }
    arg_fname = ::cw::lib::python_strip(arg_fname);
    return arg_fname.empty() ? "noname" : arg_fname;
}

bool check_dischar(::cw::lib::String arg_s)
{
    constexpr const char* invalid = "\\/:,;*?\"<>|";
    return arg_s.find_first_of(invalid) != ::cw::lib::String::npos;
}

bool path_starts_with(const ::cw::lib::String& arg_path, const ::cw::lib::String& arg_prefix)
{
    const auto normalize = [](const ::cw::lib::String& value) {
        auto result = normalize_path(value);
#if defined(_WIN32)
        result = ::cw::lib::encoding::lower(result);
#endif
        return result;
    };
    auto path = normalize(arg_path);
    auto prefix = normalize(arg_prefix);
    while (prefix.size() > 1 && prefix.back() == '/') prefix.pop_back();
    if (prefix.empty() || path.size() < prefix.size() ||
        path.compare(0, prefix.size(), prefix) != 0) return false;
    return path.size() == prefix.size() || prefix.back() == '/' || path[prefix.size()] == '/';
}

::cw::lib::String replace_path_prefix(::cw::lib::String arg_path,
                                      const ::cw::lib::String& arg_prefix,
                                      const ::cw::lib::String& arg_replacement)
{
    const auto path = normalize_path(arg_path);
    auto prefix = normalize_path(arg_prefix);
    while (prefix.size() > 1 && prefix.back() == '/') prefix.pop_back();
    if (!path_starts_with(path, prefix)) return path;
    return normalize_path(arg_replacement) + path.substr(prefix.size());
}

::cw::lib::String join_yadodir(::cw::lib::String arg_path)
{
    if (!g_cwpy) return arg_path;
    return join_yadodir(std::move(arg_path),g_cwpy->m_yadodir,g_cwpy->m_tempdir);
}

::cw::lib::String join_yadodir(::cw::lib::String arg_path,
    const ::cw::lib::String& saved_root, const ::cw::lib::String& temp_root)
{
    const auto temp = join_paths({::cw::lib::Value(temp_root), ::cw::lib::Value(arg_path)});
    const auto yado = join_paths({::cw::lib::Value(saved_root), ::cw::lib::Value(arg_path)});
    if (::cw::g_fsync && !temp.empty() && ::cw::g_fsync->is_waiting(temp)) ::cw::g_fsync->sync();
    else if (::cw::g_fsync && !yado.empty() && ::cw::g_fsync->is_waiting(yado)) ::cw::g_fsync->sync();
    return storage_path_exists(std::filesystem::u8path(temp)) ? temp : yado;
}

std::optional<std::pair<String, String>> resolve_yado_paths(
    const String& path, const String& saved_root, const String& temp_root)
{
    if (path.empty() || saved_root.empty() || temp_root.empty()) return {};
    const auto absolute = [](const String& value) {
        const auto native = std::filesystem::u8path(value);
        auto result = ::cw::lib::encoding::path_to_utf8(
            ::cardwirth::platform::resolve_mounted_user_path(native)
                .value_or(std::filesystem::absolute(native)).lexically_normal());
        while (result.size() > 3 && result.back() == '/') result.pop_back();
        return result;
    };
    const auto candidate = absolute(path);
    const auto saved = absolute(saved_root), temporary = absolute(temp_root);
    String relative;
    if (path_starts_with(candidate, temporary)) relative = candidate.substr(temporary.size());
    else if (path_starts_with(candidate, saved)) relative = candidate.substr(saved.size());
    else return {};
    if (path_starts_with(saved, temporary) && path_starts_with(temporary, saved))
        return std::pair{normalize_path(saved_root + relative), normalize_path(saved_root + relative)};
    return std::pair{normalize_path(saved_root + relative), normalize_path(temp_root + relative)};
}

String get_yado_writepath(const String& path, const String& saved_root, const String& temp_root)
{
    const auto paths = resolve_yado_paths(path, saved_root, temp_root);
    if (!paths || resolve_yado_paths(saved_root, temp_root, temp_root) ||
                  resolve_yado_paths(temp_root, saved_root, saved_root))
        throw std::runtime_error("Unsafe inn staging destination: " + path);
    return paths->second;
}

String get_yado_relpath(const String& path, const String& saved_root, const String& temp_root)
{
    const auto paths = resolve_yado_paths(path, saved_root, temp_root);
    if (!paths) throw std::runtime_error("Path is outside the inn: " + path);
    // Pyのcopy_materials/_copy_materialは既
    // 知のルート接頭辞だけを取り除く。共通の解決処理で保存先が宿の内側と
    // 確認した後は、XMLの論理名の末尾ドットや元のパス要素を保持する。
    const auto raw = join_paths({::cw::lib::Value(path)});
    for (const auto& root : {saved_root, temp_root}) {
        const auto prefix = join_paths({::cw::lib::Value(root)}) + "/";
        if (raw.rfind(prefix, 0) == 0)
            return join_paths({::cw::lib::Value(raw.substr(prefix.size()))});
    }
    // ネイティブの絶対パスやマウント先の別名には、検証済みの正規化パスを代替として残す。
    return normalize_path(relpath(paths->first, saved_root));
}

::cw::lib::String get_yadofilepath(::cw::lib::String arg_path, bool allow_directory)
{
    if (arg_path.empty() || !g_cwpy || !g_cwpy->m_ydata || !*g_cwpy->m_ydata) return {};
    const auto temp_root = g_cwpy->m_tempdir;
    const auto yado_root = g_cwpy->m_yadodir;
    auto paths = resolve_yado_paths(arg_path, yado_root, temp_root);
    if (!paths && std::filesystem::u8path(arg_path).is_relative())
        // C++のYadoDBの画像パスは、現在の宿からの相対パスの場合がある。
        paths = resolve_yado_paths(join_paths({::cw::lib::Value(yado_root), ::cw::lib::Value(arg_path)}), yado_root, temp_root);
    if (!paths) return {};
    // 返却するパスやキューへ積むパスには元の論理名を保つ。resolve_yado_paths
    // は引き続きマウント側・ネイティブ側の範囲内か検証し、正規化した
    // 結果はネイティブ絶対パスやストレージの別名の代替として残す。
    const auto preserve = [&](const String& root) -> std::optional<String> {
        if (arg_path.size() >= root.size() && arg_path.compare(0, root.size(), root) == 0 &&
            (arg_path.size() == root.size() || arg_path[root.size()] == '/' || arg_path[root.size()] == '\\'))
            return arg_path.substr(root.size());
        return {};
    };
    if (const auto suffix = preserve(temp_root)) paths = std::pair{yado_root + *suffix, arg_path};
    else if (const auto suffix = preserve(yado_root)) paths = std::pair{arg_path, temp_root + *suffix};
    const auto& [yado, temp] = *paths;
    if (::cw::g_fsync && !temp.empty() && ::cw::g_fsync->is_waiting(temp)) ::cw::g_fsync->sync();
    else if (::cw::g_fsync && !yado.empty() && ::cw::g_fsync->is_waiting(yado)) ::cw::g_fsync->sync();
    if ((*g_cwpy->m_ydata)->m_deletedpaths && (*g_cwpy->m_ydata)->m_deletedpaths->contains(yado)) return {};
    const auto exists = [allow_directory](const String& path) {
        return allow_directory ? storage_path_exists(std::filesystem::u8path(path))
                               : storage_file_exists(std::filesystem::u8path(path));
    };
    if (exists(temp)) return temp;
    if (exists(yado)) return yado;
    return {};
}

::cw::lib::String find_resource(::cw::lib::String arg_path, ::cw::lib::Integer arg_mtype)
{
    if (::cw::g_fsync) ::cw::g_fsync->sync();
    std::vector<String> extensions;
    if (arg_mtype == ::cw::g_M_IMG) extensions = {".png", ".bmp", ".gif", ".jpg"};
    else if (arg_mtype == ::cw::g_M_MSC) extensions = {".ogg", ".mp3", ".mid", ".wav"};
    else if (arg_mtype == ::cw::g_M_SND) extensions = {".wav", ".ogg", ".mp3", ".mid"};
    else throw std::invalid_argument("find_resource: unknown material type");
    for (const auto& extension : extensions) {
        std::vector<String> candidates{extension};
        // PythonのnormcaseはOS依存で、Windowsでは接
        // 尾辞の大文字・小文字を統一する。他のホストでは大文字表記も調べる
        // 。下の検索とリソースの別名処理は全プラットフォームで共通にする。
#if !defined(_WIN32)
        String upper = extension;
        std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        candidates.push_back(std::move(upper));
#endif
        for (const auto& suffix : candidates) {
            auto candidate = arg_path + suffix;
            if (::cw::g_cwpy && ::cw::g_cwpy->m_rsrc) candidate = ::cw::g_cwpy->m_rsrc->get_filepath(candidate);
            if (path_is_file(candidate)) return candidate;
        }
    }
    if (::cw::g_cwpy) {
        const auto skin = value_string(::cw::g_cwpy->m_skindir);
        const auto relative = is_descendant(arg_path, skin);
        if (!relative.empty()) {
            const auto base = join_paths({::cw::lib::Value("Data/SkinBase"), ::cw::lib::Value(relative)});
            if (base != arg_path) return find_resource(base, arg_mtype);
        }
    }
    return {};
}

::cw::lib::String get_inusecardmaterialpath(::cw::lib::String arg_path, ::cw::lib::Integer arg_mtype, std::optional<std::shared_ptr<::cw::header::CardHeader>> arg_inusecard, bool arg_findskin)
{
    if (::cw::g_fsync) ::cw::g_fsync->sync();
    // 明示的なカード検索はイベント実行中でなくても有効。暗黙の検索
    // では、イベント終了後に保持されているカードを再利用しない。
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_event || !::cw::g_cwpy->m_event->m_in_inusecardevent) return {};
    std::shared_ptr<::cw::header::CardHeader> active;
    if (arg_inusecard && *arg_inusecard) active = *arg_inusecard;
    else {
        if (!::cw::g_cwpy->is_runningevent()) return {};
        const auto event_card = ::cw::g_cwpy->m_event->get_inusecard();
        if (event_card && *event_card) active = *event_card;
    }
    if (!active) return {};
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&active->m_carddata.storage());
    const auto carddata = object && *object
        ? std::dynamic_pointer_cast<::cw::data::CWPyElement>(*object) : nullptr;
    // Pyは選択中の使用カードのXMLが読み込み済みであることをアサー
    // ションで確認する。ここでも型付き所有者の不在や型違いを拒否し、シ
    // ナリオ外のカードと見なして宿の素材を選ばない。ネイティブの型付き
    // 境界では、この開発上の前提違反をlogic_errorで示す。
    if (!carddata) throw std::logic_error("get_inusecardmaterialpath requires loaded card XML");
    const bool scenario_card = carddata->getbool(".", std::optional<::cw::lib::Value>(::cw::lib::Value("scenariocard")), false);
    const String materials = carddata->gettext("Property/Materials", "");
    if (scenario_card && materials.empty()) return {};
    const auto yado_path = join_yadodir(std::move(arg_path));
    return get_materialpathfromskin(yado_path, arg_mtype, arg_findskin);
}

::cw::lib::String get_materialpath(::cw::lib::String arg_path, ::cw::lib::Integer arg_mtype, ::cw::lib::String arg_scedir, bool arg_system, bool arg_findskin)
{
    if (::cw::g_fsync) ::cw::g_fsync->sync();
    if (arg_mtype == ::cw::g_M_IMG && ::cw::binary::image::path_is_code(arg_path)) return arg_path;
    std::shared_ptr<::cw::data::SystemData> system_data;
    std::shared_ptr<::cw::data::ScenarioData> scenario;
    if (::cw::g_cwpy) {
        if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_sdata.storage()); object && *object)
            system_data = std::dynamic_pointer_cast<::cw::data::SystemData>(*object);
        scenario = std::dynamic_pointer_cast<::cw::data::ScenarioData>(system_data);
    }
    const auto cache_key = std::make_tuple(arg_path, arg_mtype, arg_system, arg_findskin);
    // Pythonは明示的に渡された素材ルートを共通キャッシュから分離する。
    const bool use_cache = arg_scedir.empty();
    if (system_data && use_cache) {
        if (const auto cached = system_data->m_path_cache.find(cache_key); cached != system_data->m_path_cache.end())
            return cached->second;
    }
    String path = arg_path;
    bool selected_temp = false;
    const bool from_scenario = !arg_system && ::cw::g_cwpy &&
        (::cw::g_cwpy->is_playingscenario() || !arg_scedir.empty());
    if (from_scenario) {
        auto temp_path = join_paths({::cw::lib::Value(value_string(::cw::g_tempdir)), ::cw::lib::Value("ScenarioLog/TempFile"), ::cw::lib::Value(arg_path)});
        if (::cw::g_cwpy->m_rsrc) temp_path = ::cw::g_cwpy->m_rsrc->get_filepath(temp_path);
        if (path_is_file(temp_path)) {
            path = temp_path;
            selected_temp = true;
        } else if (!arg_scedir.empty()) {
            path = join_paths({::cw::lib::Value(arg_scedir), ::cw::lib::Value(arg_path)});
        } else if (scenario) {
            path = join_paths({::cw::lib::Value(scenario->m_scedir), ::cw::lib::Value(arg_path)});
        }
    }
    if (!selected_temp && ::cw::g_cwpy && ::cw::g_cwpy->m_rsrc) path = ::cw::g_cwpy->m_rsrc->get_filepath(path);
    if (!from_scenario && !path_is_file(path) && ::cw::g_cwpy)
        path = join_paths({::cw::lib::Value(value_string(::cw::g_cwpy->m_skindir)), ::cw::lib::Value(arg_path)});
    const auto result = get_materialpathfromskin(path, arg_mtype, arg_findskin);
    // Pythonは見つからなかった結果もSystemDataへキャッシュする。セ
    // ッション中にファイルが増減しても、素材の参照元が無断で変わらないようにする。
    if (system_data && use_cache) system_data->m_path_cache[cache_key] = result;
    return result;
}

::cw::lib::String get_materialpathfromskin(::cw::lib::String arg_path, ::cw::lib::Integer arg_mtype, bool arg_findskin)
{
    if (::cw::g_fsync) ::cw::g_fsync->sync();
    std::error_code ec;
    if (path_is_file(arg_path)) return arg_path;
    // Pyはfindskinがfalseなら見つからないパスを空にし
    // 、その後も空のファイル名を含め、通常のファイル名検索を行う。
    if (!arg_findskin) arg_path.clear();
    const auto skin = ::cw::g_cwpy ? value_string(::cw::g_cwpy->m_skindir) : String{};
    if (skin.empty()) return {};
    const auto relative = arg_findskin ? is_descendant(arg_path, skin) : String{};
    if (!relative.empty()) {
        const auto [stem, extension] = splitext(relative);
        const auto found = find_resource(join_paths({::cw::lib::Value(skin), ::cw::lib::Value(stem)}), arg_mtype);
        if (!found.empty()) return found;
    }
    const auto filename = ::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(arg_path).filename());
    const auto [stem, extension] = splitext(filename);
    const auto ext = lower_ascii(extension);
    // Pythonの汎用EB代替処理は、ラスタ画像の検索より先に元の拡張子を保持する。
    if (ext == ".jpy1" || ext == ".jptx" || ext == ".jpdc") {
        const auto candidate = join_paths({::cw::lib::Value(skin), ::cw::lib::Value("Table"), ::cw::lib::Value(filename)});
        return ::cw::g_cwpy && ::cw::g_cwpy->m_rsrc
            ? ::cw::g_cwpy->m_rsrc->get_filepath(candidate) : candidate;
    }
    std::vector<String> roots{skin};
    const auto materials = std::filesystem::u8path("Data/Materials");
    if (::cardwirth::platform::is_mounted_user_path(materials)) {
        const auto status = ::cardwirth::platform::stat_mounted_user_path(materials, true);
        if (status && status->is_directory()) {
            if (const auto entries = ::cardwirth::platform::list_mounted_user_path(materials, true)) {
                for (const auto& entry : *entries)
                    if (entry.stat.is_directory()) {
                        // Pythonが相対のData/Materials
                        // を起点としていても、Storage実装は項目の
                        // 絶対論理パスを返す場合がある。検証済みの子要素
                        // 名を使い、呼び出し元と同じパスの基準を保つ。
                        const auto child = !entry.name.empty()
                            ? ::cw::lib::encoding::path_from_utf8(entry.name)
                            : entry.path.filename();
                        roots.push_back(::cw::lib::encoding::path_to_utf8(materials / child));
                    }
            }
        }
    } else if (std::filesystem::is_directory(materials, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(materials, ec))
            if (!ec && entry.is_directory(ec)) roots.push_back(::cw::lib::encoding::path_to_utf8(entry.path()));
    }
    for (const auto& root : roots) {
        if (arg_mtype == ::cw::g_M_IMG) {
            const auto found = find_resource(join_paths({::cw::lib::Value(root), ::cw::lib::Value("Table"), ::cw::lib::Value(stem)}), arg_mtype);
            if (!found.empty()) return found;
        } else if (arg_mtype == ::cw::g_M_MSC) {
            auto found = find_resource(join_paths({::cw::lib::Value(root), ::cw::lib::Value("Bgm"), ::cw::lib::Value(stem)}), arg_mtype);
            if (found.empty()) found = find_resource(join_paths({::cw::lib::Value(root), ::cw::lib::Value("BgmAndSound"), ::cw::lib::Value(stem)}), arg_mtype);
            if (!found.empty()) return found;
        } else if (arg_mtype == ::cw::g_M_SND) {
            auto found = find_resource(join_paths({::cw::lib::Value(root), ::cw::lib::Value("Sound"), ::cw::lib::Value(stem)}), arg_mtype);
            if (found.empty()) found = find_resource(join_paths({::cw::lib::Value(root), ::cw::lib::Value("BgmAndSound"), ::cw::lib::Value(stem)}), arg_mtype);
            if (!found.empty()) return found;
        }
    }
    return {};
}

// Pythonは呼び出し元に親ディレクトリの作成を要求する。ネイティブの直
// 接書き込みはFileSyncと同じく親も準備し、キュー経由と直接の切り
// 替えで、新規の保存ディレクトリへ書けるかどうかが変わらないようにする。
void write_file(::cw::lib::String arg_file, ::cw::lib::Value arg_data, std::shared_ptr<::cw::util::FileSync> arg_fsync, ::cw::lib::String arg_mode, std::optional<::cw::lib::String> arg_encoding)
{
    FileSync::validate_write_mode(arg_mode);
    const auto path = std::filesystem::u8path(arg_file);
    if (::cardwirth::platform::is_mounted_user_path(path)) {
        // マウントされたストレージはバイト列を不可分に置き換える。開いた
        // ストリームを公開しない基本操作でも、モードと型の規則は同じ。
        validate_file_encoding(arg_mode, arg_encoding);
        validate_file_payload(arg_data, arg_mode);
        ::cw::lib::Bytes bytes;
        if (const auto* source = std::get_if<::cw::lib::Bytes>(&arg_data.storage()))
            bytes = *source;
        else if (const auto* text = std::get_if<::cw::lib::String>(&arg_data.storage()))
            bytes = encode_file_text(*text, arg_encoding, arg_mode.find('b') == String::npos);
        else throw std::invalid_argument("write_file: expected String or Bytes");
        const auto data = std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(bytes.data()), bytes.size());
        if (!::cardwirth::platform::write_mounted_user_file(path, data, true))
            throw std::runtime_error("write_file: encrypted UserData write failed: " + arg_file);
        return;
    }
    if (arg_fsync) {
        arg_fsync->push(std::move(arg_file), std::move(arg_data), std::move(arg_mode), std::move(arg_encoding));
        return;
    }
    validate_file_encoding(arg_mode, arg_encoding);
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) throw std::runtime_error("write_file: cannot open " + arg_file);
    validate_file_payload(arg_data, arg_mode);
    if (const auto* bytes = std::get_if<::cw::lib::Bytes>(&arg_data.storage())) stream.write(reinterpret_cast<const char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
    else if (const auto* text = std::get_if<::cw::lib::String>(&arg_data.storage())) {
        const auto encoded = encode_file_text(*text, arg_encoding, arg_mode.find('b') == String::npos);
        stream.write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
    }
    else throw std::invalid_argument("write_file: expected String or Bytes");
    if (!stream) throw std::runtime_error("write_file: cannot write " + arg_file);
    stream.flush();
    if (!stream) throw std::runtime_error("write_file: cannot flush " + arg_file);
    stream.close();
    if (!stream) throw std::runtime_error("write_file: cannot close " + arg_file);
}

void write_textfile(::cw::lib::String arg_file, ::cw::lib::String arg_data, std::shared_ptr<::cw::util::FileSync> arg_fsync)
{
    write_file(std::move(arg_file), ::cw::lib::Value(std::move(arg_data)), std::move(arg_fsync), "w", String("utf-8"));
}

void remove_temp()
{
    if (::cw::g_fsync) ::cw::g_fsync->sync();
    const auto dpath = value_string(::cw::g_tempdir);
    if (dpath.empty()) throw std::filesystem::filesystem_error("remove_temp", std::filesystem::path{},
        std::make_error_code(std::errc::no_such_file_or_directory));
    const auto native = std::filesystem::u8path(dpath);
    if (::cardwirth::platform::is_mounted_user_path(native)) {
        const auto status = ::cardwirth::platform::stat_mounted_user_path(native, true);
        if ((!status || !status->exists()) &&
            !::cardwirth::platform::make_mounted_user_directory(native, true))
            throw std::runtime_error("remove_temp: encrypted UserData mkdir failed");
        bool preserve = false;
        const auto entries = ::cardwirth::platform::list_mounted_user_path(native, true);
        if (!entries) throw std::runtime_error("remove_temp: cannot enumerate mounted temporary directory");
        {
            for (const auto& entry : *entries) {
                if (entry.name == "Scenario" || entry.name == "LockFiles") {
                    preserve = true;
                    continue;
                }
                const auto text = ::cw::lib::encoding::path_to_utf8(entry.path);
                try { remove(text, false); }
                catch (...) { print_ex(std::nullopt); remove_treefiles(text, false); }
            }
        }
        if (!preserve && dpath != ::cw::g_tempdir_init) {
            try { remove(dpath, false); }
            catch (...) { print_ex(std::nullopt); remove_treefiles(dpath, false); }
        }
        // Pythonは上のセッションごとの列挙・ファイル削除と異なり、
        // この共通の後始末の失敗は許容する。Storageバックエンドは
        // falseを返すだけでなく例外も投げ得るため、同じ扱いを保つ。
        try {
            if (const auto deleted = ::cardwirth::platform::resolve_mounted_user_path(
                    std::filesystem::u8path("Data/Temp/Global/Deleted")))
                (void)::cardwirth::platform::remove_mounted_user_path(*deleted, true);
        } catch (...) { /* Pythonのremove_tempと同様に、共通のDeletedの後始末の失敗は無視する。 */ }
        return;
    }
    std::error_code ec;
    if (!std::filesystem::exists(std::filesystem::u8path(dpath), ec)) {
        std::filesystem::create_directories(std::filesystem::u8path(dpath), ec);
        if (ec) throw std::filesystem::filesystem_error("remove_temp", std::filesystem::u8path(dpath), ec);
    }
    bool preserve = false;
    // Pythonは削除を始める前にos.listdirを完了する。列挙失敗
    // をScenario/LockFilesが存在しないものとして扱わない。
    std::vector<std::filesystem::directory_entry> entries;
    for (const auto& entry : std::filesystem::directory_iterator(native)) entries.push_back(entry);
    for (const auto& entry : entries) {
        const auto name = ::cw::lib::encoding::path_to_utf8(entry.path().filename());
        if (name == "Scenario" || name == "LockFiles") { preserve = true; continue; }
        try {
            remove(::cw::lib::encoding::path_to_utf8(entry.path()), false);
        } catch (...) {
            print_ex(std::optional<::cw::lib::Value>(::cw::lib::Value(::cw::lib::encoding::path_to_utf8(entry.path()))));
            remove_treefiles(::cw::lib::encoding::path_to_utf8(entry.path()), false);
        }
    }
    if (!preserve && dpath != ::cw::g_tempdir_init) {
        try { remove(dpath, false); }
        catch (...) { print_ex(std::optional<::cw::lib::Value>(::cw::lib::Value(dpath))); remove_treefiles(dpath, false); }
    }
    try { remove("Data/Temp/Global/Deleted", false); } catch (...) { /* 存在しなくても正常。 */ }
}

void remove(::cw::lib::String arg_path, bool arg_trashbox)
{
    if (::cw::g_fsync) ::cw::g_fsync->sync();
    arg_path = normalize_path(std::move(arg_path));
    std::error_code ec;
    const auto path = std::filesystem::u8path(arg_path);
    const bool is_scenariolog = arg_path.find("ScenarioLog") != ::cw::lib::String::npos;
#if !defined(CARDWIRTH_LOG_ENABLED)
    (void)is_scenariolog;
#endif
#if defined(CARDWIRTH_LOG_ENABLED)
    if (is_scenariolog) {
        ::cardwirth::debug::log("remove ScenarioLog begin: path=" + arg_path +
                                " trashbox=" + (arg_trashbox ? "1" : "0"));
    }
#endif
    if (::cardwirth::platform::is_mounted_user_path(path)) {
        // 仮想の暗号化パスにはホストのゴミ箱で扱う実体がない。どち
        // らの削除モードでも、認証済みアーカイブの内側で変更する。
        const auto status = ::cardwirth::platform::stat_mounted_user_path(path, true);
        if (!status || !status->exists()) return;
        if (status->is_file()) {
            remove_file(arg_path, 0, arg_trashbox);
        } else if (status->is_directory()) {
            auto folded = arg_path;
            std::transform(folded.begin(), folded.end(), folded.begin(),
                [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            if (!arg_trashbox && folded.rfind("data/temp/", 0) == 0) {
                try {
                    remove_treefiles(arg_path, false);
                    remove_tree(arg_path, 0, true, false);
                } catch (...) {
                    print_ex(std::nullopt);
                    remove_treefiles(arg_path, false);
                }
            } else if (arg_trashbox) {
                try { remove_tree(arg_path, 0, false, true); }
                catch (...) {
                    print_ex(std::nullopt);
                    remove_treefiles(arg_path, true);
                    remove_tree(arg_path, 0, false, true);
                }
            } else {
                remove_treefiles(arg_path, false);
                remove_tree(arg_path, 0, false, false);
            }
        }
        return;
    }
    // Pythonのcw.util.remove()は最初にisfile/isdir
    // を調べるため、既に存在しないパスは成功扱いで何もしない。is_directory()
    // のERROR_FILE_NOT_FOUNDを伝えると、
    // ログディレクトリが未作成の場合に通常のScenarioLog
    // の後始末がシナリオ読み込みを中断してしまう。
    if (!std::filesystem::exists(path, ec)) {
        if (ec) {
#if defined(CARDWIRTH_LOG_ENABLED)
            if (is_scenariolog) {
                ::cardwirth::debug::log("remove ScenarioLog existence check failed: path=" + arg_path +
                                        " error=" + ec.message());
            }
#endif
            throw std::filesystem::filesystem_error("remove", path, ec);
        }
#if defined(CARDWIRTH_LOG_ENABLED)
        if (is_scenariolog) {
            ::cardwirth::debug::log("remove ScenarioLog skipped: path is absent");
        }
#endif
        return;
    }
    if (arg_trashbox) {
        // send_trashbox()がプラットフォーム固有の
        // 分岐を担い、ゴミ箱APIがなければ削除へ切り替える。
        if (std::filesystem::is_directory(path, ec)) {
            try { remove_tree(arg_path, 0, false, true); }
            catch (...) {
                print_ex(std::nullopt);
                remove_treefiles(arg_path, true);
                remove_tree(arg_path, 0, false, true);
            }
        } else {
            remove_file(arg_path, 0, true);
        }
#if defined(CARDWIRTH_LOG_ENABLED)
        if (is_scenariolog) ::cardwirth::debug::log("remove ScenarioLog completed: trashbox");
#endif
        return;
    }
    ec.clear();
    if (std::filesystem::is_directory(path, ec)) {
        // Pythonが空ディレクトリの残存を許容するのはData/Temp
        // だけ。通常のディレクトリの失敗は伝え、ファイル
        // のみの代替削除が完了しただけで成功にしない。
        auto folded = arg_path;
        std::transform(folded.begin(), folded.end(), folded.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (folded.rfind("data/temp/", 0) == 0) {
            try {
                remove_treefiles(arg_path, false);
                remove_tree(arg_path, 0, true, false);
            } catch (...) {
                print_ex(std::nullopt);
                remove_treefiles(arg_path, false);
            }
        } else {
            remove_treefiles(arg_path, false);
            remove_tree(arg_path, 0, false, false);
        }
    } else if (std::filesystem::is_regular_file(path, ec)) remove_file(arg_path, 0, false);
    if (ec) {
#if defined(CARDWIRTH_LOG_ENABLED)
        if (is_scenariolog) {
            ::cardwirth::debug::log("remove ScenarioLog failed: path=" + arg_path +
                                    " error=" + ec.message());
        }
#endif
        throw std::filesystem::filesystem_error("remove", path, ec);
    }
#if defined(CARDWIRTH_LOG_ENABLED)
    if (is_scenariolog) ::cardwirth::debug::log("remove ScenarioLog completed");
#endif
}

namespace {
void wait_for_removal_retry()
{
    // Pythonと同じ1秒待機だが、ブラウザのイベントループへ制御を譲る。emscripten_sleep
    // はWebビルドで既に使うAsyncifyのインポート関数。
#if defined(__EMSCRIPTEN__)
    emscripten_sleep(1000);
#else
    std::this_thread::sleep_for(std::chrono::seconds(1));
#endif
}
}

void remove_file(::cw::lib::String arg_path, ::cw::lib::Integer arg_retry, bool arg_trashbox)
{
    if (::cw::g_fsync) ::cw::g_fsync->sync();
    arg_path = normalize_path(std::move(arg_path));
    const auto mounted_path = std::filesystem::u8path(arg_path);
    if (::cardwirth::platform::is_mounted_user_path(mounted_path)) {
        const auto status = ::cardwirth::platform::stat_mounted_user_path(mounted_path, true);
        if (!status || !status->exists() || status->is_directory())
            throw std::runtime_error("remove_file: mounted path is missing or a directory: " + arg_path);
        // マウントされたファイルにはネイティブのゴミ箱で扱う実体がない。バック
        // エンドの失敗では、下のネイティブのchmod・再試行経路へ進まない。
        if (!::cardwirth::platform::remove_mounted_user_path(mounted_path, false))
            throw std::runtime_error("remove_file: mounted deletion failed: " + arg_path);
        return;
    }
    try {
        if (arg_trashbox) { send_trashbox(arg_path); return; }
        const auto path = std::filesystem::u8path(arg_path);
        // ファイル専用のunlinkを使い、ネイティブのerrnoを保つ。
        // 特にWindowsではディレクトリにEACCESを返すため、Python
        // のchmod・再試行経路をツリーの再帰削除へ変えない。
#if defined(_WIN32)
        // CPythonのWindowsのunlinkは、ディレクトリリンクをRemoveDirectoryW
        // で削除する。CRTのunlinkだけではジャンクションをディレクトリとして拒
        // 否する。リンク自身のネイティブメタデータだけを調べ、リンク先は削除しない。
        WIN32_FIND_DATAW info{};
        const auto find = FindFirstFileW(path.c_str(), &info);
        const bool directory_link = find != INVALID_HANDLE_VALUE &&
            (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ==
                (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT) &&
            (info.dwReserved0 == IO_REPARSE_TAG_SYMLINK || info.dwReserved0 == IO_REPARSE_TAG_MOUNT_POINT);
        if (find != INVALID_HANDLE_VALUE) FindClose(find);
        if (directory_link) {
            if (!RemoveDirectoryW(path.c_str()))
                throw std::filesystem::filesystem_error("remove_file", path,
                    std::error_code(static_cast<int>(GetLastError()), std::system_category()));
        } else if (::_wunlink(path.c_str()) != 0) {
            throw std::filesystem::filesystem_error("remove_file", path,
                std::error_code(errno, std::generic_category()));
        }
#else
        if (::unlink(path.c_str()) != 0)
            throw std::filesystem::filesystem_error("remove_file", path,
                std::error_code(errno, std::generic_category()));
#endif
    } catch (const std::filesystem::filesystem_error& error) {
        if (arg_retry >= 5) throw;
        if (error.code() == std::errc::permission_denied) {
            // Pythonのchmodはファイルのモードを置き換え、失敗は呼び出し元へ伝える。
            std::filesystem::permissions(std::filesystem::u8path(arg_path),
                std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                std::filesystem::perm_options::replace);
            remove_file(arg_path, arg_retry + 1, arg_trashbox);
        } else {
            // 再試行回数を含め、Pythonのツリー削除への代替経路を保つ。
            wait_for_removal_retry();
            remove_tree(arg_path, arg_retry + 1, false, arg_trashbox);
        }
    }
}

void add_winauth(::cw::lib::String arg_file)
{
#if defined(_WIN32)
    std::error_code ec;
    if (!std::filesystem::is_regular_file(std::filesystem::u8path(arg_file), ec)) return;
    const auto path = std::filesystem::u8path(arg_file);
    std::filesystem::permissions(path,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace, ec);
    if (ec) throw std::filesystem::filesystem_error("add_winauth", path, ec);
#else
    // Pythonがファイルのアクセス権を変更するのはWindowsだけ。
    (void)arg_file;
#endif
}

void remove_tree(::cw::lib::String arg_treepath, ::cw::lib::Integer arg_retry, bool arg_noretry, bool arg_trashbox)
{
    if (::cw::g_fsync) ::cw::g_fsync->sync();
    arg_treepath = normalize_path(std::move(arg_treepath));
    const auto mounted_path = std::filesystem::u8path(arg_treepath);
    if (::cardwirth::platform::is_mounted_user_path(mounted_path)) {
        const auto status = ::cardwirth::platform::stat_mounted_user_path(mounted_path, true);
        if (!status || !status->is_directory())
            throw std::runtime_error("remove_tree: mounted path is not a directory: " + arg_treepath);
        if (!::cardwirth::platform::remove_mounted_user_path(mounted_path, true))
            throw std::runtime_error("remove_tree: mounted deletion failed: " + arg_treepath);
        return;
    }
    try {
        if (arg_trashbox) { send_trashbox(arg_treepath); return; }
        const auto root = std::filesystem::u8path(arg_treepath);
        if (!std::filesystem::is_directory(std::filesystem::symlink_status(root)))
            throw std::filesystem::filesystem_error("remove_tree", root,
                std::make_error_code(std::errc::not_a_directory));
        std::filesystem::remove_all(root);
    } catch (const std::filesystem::filesystem_error& error) {
        if (arg_noretry || arg_retry >= 5) {
            remove_tree2(arg_treepath, arg_trashbox);
            return;
        }
        if (error.code() == std::errc::permission_denied) {
            try {
                // Pythonのos.walkは既定で親から子へ走査し、読めないディ
                // レクトリを飛ばす。この代替処理へ進むのはchmodの失敗時だけ。
                const auto grant = [&](const auto& self, const std::filesystem::path& directory) -> void {
                    std::error_code ec;
                    std::filesystem::directory_iterator it(directory, ec), end;
                    if (ec) return;
                    std::vector<std::filesystem::path> directories, files;
                    for (; it != end; it.increment(ec)) {
                        if (ec) return;
                        std::error_code type_error;
                        if (it->is_directory(type_error)) directories.push_back(it->path());
                        else files.push_back(it->path());
                    }
                    if (ec) return;
                    const auto permissions = std::filesystem::perms::owner_read |
                                             std::filesystem::perms::owner_write;
                    for (const auto& child : directories) {
                        std::error_code type_error;
                        if (std::filesystem::is_directory(child, type_error))
                            std::filesystem::permissions(child, permissions);
                    }
                    for (const auto& file : files) {
                        std::error_code type_error;
                        if (std::filesystem::is_regular_file(file, type_error))
                            std::filesystem::permissions(file, permissions);
                    }
                    for (const auto& child : directories) {
                        std::error_code type_error;
                        if (!std::filesystem::is_symlink(child, type_error)) self(self, child);
                    }
                };
                grant(grant, std::filesystem::u8path(arg_treepath));
            } catch (const std::filesystem::filesystem_error&) {
                print_ex(std::nullopt);
                wait_for_removal_retry();
                remove_tree2(arg_treepath, arg_trashbox);
                return;
            }
        } else {
            wait_for_removal_retry();
        }
        remove_tree(arg_treepath, arg_retry + 1, false, arg_trashbox);
    }
}

void remove_tree2(::cw::lib::String arg_treepath, bool arg_trashbox)
{
    if (::cw::g_fsync) ::cw::g_fsync->sync();
    arg_treepath = normalize_path(std::move(arg_treepath));
    const auto root = std::filesystem::u8path(arg_treepath);
    if (::cardwirth::platform::is_mounted_user_path(root)) {
        const auto remove_directory = [](const std::filesystem::path& directory) {
            const auto status = ::cardwirth::platform::stat_mounted_user_path(directory, true);
            if (!status || !status->is_directory() ||
                !::cardwirth::platform::remove_mounted_user_path(directory, false))
                throw std::runtime_error("remove_tree2: mounted directory removal failed: " +
                    ::cw::lib::encoding::path_to_utf8(directory));
        };
        const auto walk = [&](const auto& self, const std::filesystem::path& directory) -> void {
            const auto entries = ::cardwirth::platform::list_mounted_user_path(directory);
            if (!entries) return;
            for (const auto& entry : *entries)
                if (entry.stat.is_directory()) self(self, entry.path);
            for (const auto& entry : *entries) {
                if (!entry.stat.is_directory()) continue;
                const auto status = ::cardwirth::platform::stat_mounted_user_path(entry.path);
                if (status && status->is_directory()) remove_directory(entry.path);
            }
            for (const auto& entry : *entries) {
                if (entry.stat.is_directory()) continue;
                const auto status = ::cardwirth::platform::stat_mounted_user_path(entry.path);
                if (status && status->is_file() &&
                    !::cardwirth::platform::remove_mounted_user_path(entry.path, false))
                    throw std::runtime_error("remove_tree2: mounted file removal failed: " +
                        ::cw::lib::encoding::path_to_utf8(entry.path));
            }
        };
        walk(walk, root);
        remove_directory(root);
        return;
    }
    const auto remove_directory = [](const std::filesystem::path& directory) {
        // rmdirの動作を直接使う。filesystem::removeは
        // ファイルも削除し、独自にシンボリックリンクを拒否するとWindows
        // 固有のディレクトリリンク処理や実際のOSのエラー分類を失う。
#if defined(_WIN32)
        const auto result = ::_wrmdir(directory.c_str());
#else
        const auto result = ::rmdir(directory.c_str());
#endif
        if (result != 0) {
            const auto error = std::error_code(errno, std::generic_category());
            throw std::filesystem::filesystem_error("remove_tree2", directory,
                error);
        }
    };
    // Pythonのos.walk(topdown=False, followlinks=False)の後にos.rmdir(root)
    // を行う処理に相当する。最後に再帰的なremove_allを行ってunlinkの失敗を隠さない。
    const auto walk = [&](const auto& self, const std::filesystem::path& directory) -> void {
        std::error_code error;
        std::filesystem::directory_iterator it(directory, error), end;
        if (error) return; // os.walkの既定のonerrorは、読めないツリーを飛ばす。
        std::vector<std::filesystem::path> directories, files;
        for (; it != end; it.increment(error)) {
            if (error) return;
            std::error_code type_error;
            if (it->is_directory(type_error)) directories.push_back(it->path());
            else files.push_back(it->path());
        }
        if (error) return;
        for (const auto& child : directories) {
            std::error_code type_error;
            if (!std::filesystem::is_symlink(child, type_error)) self(self, child);
        }
        for (const auto& child : directories) {
            std::error_code type_error;
            if (std::filesystem::is_directory(child, type_error)) remove_directory(child);
        }
        for (const auto& file : files) {
            std::error_code type_error;
            if (!std::filesystem::is_regular_file(file, type_error)) continue;
            if (arg_trashbox) send_trashbox(::cw::lib::encoding::path_to_utf8(file));
            else if (!std::filesystem::remove(file))
                throw std::filesystem::filesystem_error("remove_tree2", file,
                    std::make_error_code(std::errc::no_such_file_or_directory));
        }
    };
    walk(walk, root);
    remove_directory(root);
}

void remove_treefiles(::cw::lib::String arg_treepath, bool arg_trashbox)
{
    if (::cw::g_fsync) ::cw::g_fsync->sync();
    arg_treepath = normalize_path(std::move(arg_treepath));
    const auto dir = std::filesystem::u8path(arg_treepath);
    if (::cardwirth::platform::is_mounted_user_path(dir)) {
        // os.walk(topdown=False)と同様に、ディレクトリを保持し、読めな
        // いツリーを飛ばし、ファイル削除の失敗は伝える。ここではネイティブ入出力を使わない。
        const auto walk = [&](const auto& self, const std::filesystem::path& directory) -> void {
            const auto entries = ::cardwirth::platform::list_mounted_user_path(directory);
            if (!entries) return;
            for (const auto& entry : *entries)
                if (entry.stat.is_directory()) self(self, entry.path);
            for (const auto& entry : *entries) {
                if (entry.stat.is_directory()) continue;
                const auto status = ::cardwirth::platform::stat_mounted_user_path(entry.path);
                if (!status || !status->is_file()) continue;
                if (!::cardwirth::platform::remove_mounted_user_path(entry.path, false))
                    throw std::runtime_error("remove_treefiles: mounted file deletion failed: " +
                        ::cw::lib::encoding::path_to_utf8(entry.path));
            }
        };
        const auto status = ::cardwirth::platform::stat_mounted_user_path(dir);
        if (status && status->is_directory()) walk(walk, dir);
        return;
    }
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return;
    // Pythonはディレクトリのシンボリックリンクをたどらず、子から親へ走査する
    // 。走査のエラーはos.walkが無視するが、ファイル削除のエラーは伝える。
    const auto walk = [&](const auto& self, const std::filesystem::path& directory) -> void {
        std::error_code error;
        std::vector<std::filesystem::path> directories, files;
        std::filesystem::directory_iterator it(directory, error), end;
        if (error) return;
        for (; it != end; it.increment(error)) {
            if (error) return;
            std::error_code type_error;
            if (it->is_directory(type_error)) {
                if (!it->is_symlink(type_error) && !type_error) directories.push_back(it->path());
            } else files.push_back(it->path());
        }
        if (error) return;
        for (const auto& child : directories) self(self, child);
        for (const auto& file : files) {
            std::error_code type_error;
            if (!std::filesystem::is_regular_file(file, type_error)) continue;
            const auto text = ::cw::lib::encoding::path_to_utf8(file);
            add_winauth(text);
            if (arg_trashbox) send_trashbox(text);
            else {
                std::error_code remove_error;
                const bool removed = std::filesystem::remove(file, remove_error);
                if (!removed || remove_error)
                    throw std::filesystem::filesystem_error("remove_treefiles", file,
                        remove_error ? remove_error : std::make_error_code(std::errc::no_such_file_or_directory));
            }
        }
    };
    walk(walk, dir);
}

// Pythonは移動先がファイル名だけならmakedirs("")を
// 試して失敗する。ここでは空の親を現在のディレクトリとして扱い、移
// 動前に空のパスを作らない。製品の呼び出し元は通常、親を指定する。
void rename_file(::cw::lib::String arg_path, ::cw::lib::String arg_dstpath, bool arg_trashbox)
{
    if (::cw::g_fsync) ::cw::g_fsync->sync();
    arg_path = normalize_path(std::move(arg_path));
    arg_dstpath = normalize_path(std::move(arg_dstpath));
    auto target = std::filesystem::u8path(arg_dstpath);
    if (!target.parent_path().empty()) std::filesystem::create_directories(target.parent_path());
    std::error_code ec;
    // Pythonのcw.util.rename_file()が上書きす
    // るのは既存の移動先ファイルだけ。続くshutil.move()は、
    // 移動先が既存ディレクトリならそれを削除せず、移動元をその中へ置く。
    if (std::filesystem::is_regular_file(target, ec)) remove(arg_dstpath, arg_trashbox);
    try {
        move_path(arg_path, arg_dstpath);
    } catch (const std::filesystem::filesystem_error&) {
        print_ex(std::nullopt);
        // Pythonの外側の代替処理はshutil.moveのcopy2
        // とは別。移動先を切り詰めてバイト列をコピーし、fsync後に指定
        // されたゴミ箱方針で移動元を取り除く。コピー失敗時は移動元を残す。
        std::ifstream input(std::filesystem::u8path(arg_path), std::ios::binary);
        input.exceptions(std::ios::badbit);
        if (!input) throw std::runtime_error("rename_file: cannot open source");
        std::ofstream output(target, std::ios::binary | std::ios::trunc);
        output.exceptions(std::ios::badbit | std::ios::failbit);
        const Bytes bytes{std::istreambuf_iterator<char>(input), {}};
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        output.flush();
        output.close();
        input.close();
        FileSync::flush_native_file(target);
        remove_file(arg_path, 0, arg_trashbox);
    }
}

static void move_path_impl(::cw::lib::String arg_path,
                           ::cw::lib::String arg_dstpath,
                           const testing::MovePathRename* injected_native_rename)
{
    namespace fs=std::filesystem;
    auto source=fs::u8path(arg_path);
    auto target=fs::u8path(arg_dstpath);
    std::error_code ec;
    const auto is_directory = [](const fs::path& path) {
        if (::cardwirth::platform::is_mounted_user_path(path)) {
            const auto status = ::cardwirth::platform::stat_mounted_user_path(path);
            return status && status->is_directory();
        }
        std::error_code error;
        return fs::is_directory(path, error);
    };
    const auto rename_primitive = [&](const fs::path& from, const fs::path& to,
                                      std::error_code& error) {
        if (::cardwirth::platform::is_mounted_user_path(from) ||
            ::cardwirth::platform::is_mounted_user_path(to)) {
            error = ::cardwirth::platform::rename_mounted_user_path(from, to, true)
                ? std::error_code{} : std::make_error_code(std::errc::io_error);
        } else if (injected_native_rename && *injected_native_rename) {
            (*injected_native_rename)(from, to, error);
        } else fs::rename(from, to, error);
    };
    if (is_directory(target)) {
        // shutil.moveはディレクトリを自分自身へ移す場合、大文字・小文字を区別しな
        // いファイルシステムでの表記変更も含め、入れ子への移動ではなく名前変更として扱う。
        ec.clear();
        // マウントされたストレージはネイティブのinodeの同一性ではなく論理パス
        // を公開する。同一性を判定する基本操作だけが異なり、移動先の扱いは共通。
        const bool same_directory = ::cardwirth::platform::is_mounted_user_path(source)
            ? source.lexically_normal() == target.lexically_normal()
            : std::filesystem::equivalent(source, target, ec);
        if (same_directory) {
            // Pythonのos.renameは、この同一ディレクトリのパスに末尾
            // 区切りを許容する。ファイルシステムの基本操作へ渡す際はその区切りだけ
            // を除去し、対象の大小文字は保って、大小文字だけの名前変更も実行する。
            while (source.filename().empty() && source.has_relative_path())
                source = source.parent_path();
            while (target.filename().empty() && target.has_relative_path())
                target = target.parent_path();
            // shutil.moveの同一ディレクトリへの名前変更は、代替処理の外側で行う。
            rename_primitive(source, target, ec);
            if (ec) throw fs::filesystem_error("move_path: rename failed", source, target, ec);
            return;
        } else {
            auto basename = source;
            while (basename.filename().empty() && basename.has_relative_path())
                basename = basename.parent_path();
            target /= basename.filename();
            ec.clear();
            // Pythonは名前変更やコピーを試す前にexists(real_dst)
            // を調べる。同名の子要素がある場合は両方を残し、
            // 下の代替処理でこのエラーを上書きへ変えない。
            if (storage_path_exists(target))
                throw std::filesystem::filesystem_error("rename_file: destination already exists",
                    source, target, std::make_error_code(std::errc::file_exists));
        }
    }
    ec.clear();
    rename_primitive(source, target, ec);
    if (!ec) return;
    // shutil.moveはEXDEVだけでなく、名前変更のすべてのOSError
    // でコピーへ切り替える。ツリー全体をコピーしてから移動元を削除し、コ
    // ピー失敗時はPythonと同様に移動元と途中までの移動先を残す。
    if (::cardwirth::platform::is_mounted_user_path(source) ||
        ::cardwirth::platform::is_mounted_user_path(target)) {
        // 暗号化・仮想ファイルには、提供側の既存のコピー・削除処理を使う。ネイ
        // ティブのファイルシステム操作ではアクセスできない。shutil.move
        // は名前変更のすべての失敗でコピーを試すため、EXDEVだけに対応す
        // るStorage::moveの代替処理は使わない。メタデータは提供
        // 側が公開するコピー仕様の範囲にとどめ、OS固有の規則を追加しない。
        if (is_directory(source)) {
            const auto relative = target.lexically_normal().lexically_relative(source.lexically_normal());
            if (relative.empty() || (!relative.empty() && *relative.begin() != ".."))
                throw fs::filesystem_error("move_path: destination is inside source", source, target,
                    std::make_error_code(std::errc::invalid_argument));
        }
        if (!::cardwirth::platform::copy_mounted_user_path(source, target, true))
            throw fs::filesystem_error("move_path: copy failed", source, target,
                std::make_error_code(std::errc::io_error));
        if (!::cardwirth::platform::remove_mounted_user_path(source, true))
            throw fs::filesystem_error("move_path: source removal failed", source, target,
                std::make_error_code(std::errc::io_error));
        return;
    }
    const auto copy_metadata=[](const fs::path& from,const fs::path& to) {
        // std::filesystemが公開するのは更新時刻とアクセス権で、copy2
        // のアクセス時刻・拡張属性・フラグまでは含まない。全OSで公開された項
        // 目を保持し、個別のプラットフォームだけに異なる移動規則を加えない。
        fs::last_write_time(to,fs::last_write_time(from));
        fs::permissions(to,fs::status(from).permissions());
    };
    const auto copy_link=[](const fs::path& from,const fs::path& to) {
        const auto link=fs::read_symlink(from);
        if (fs::is_directory(from)) fs::create_directory_symlink(link,to);
        else fs::create_symlink(link,to);
    };
    if (fs::is_symlink(fs::symlink_status(source))) {
        copy_link(source,target);
        fs::remove(source);
    } else if (fs::is_directory(source)) {
        // Pyはcopytreeの前に、移動先が移動元の内側なら拒
        // 否する。既存の物理的な祖先も調べ、ネイティブの別名パスを
        // 介して新設した移動先を自身へ再帰コピーしないようにする。
        auto parent=fs::absolute(target).lexically_normal();
        for (;;) {
            ec.clear();
            if (fs::equivalent(source,parent,ec))
                throw fs::filesystem_error("move_path: destination is inside source",source,target,
                    std::make_error_code(std::errc::invalid_argument));
            const auto next=parent.parent_path();
            if (next==parent || next.empty()) break;
            parent=next;
        }
        // copytree(symlinks=True)はリンクを保ち、
        // たどらない。移動元の読み取り専用設定で移動先の内容を作れなくな
        // らないよう、ディレクトリのメタデータは子要素の後に設定する。
        ::cw::lib::ExceptionPtr copy_error;
        const auto copy_tree=[&](const auto& self,const fs::path& from,const fs::path& to)->void {
            std::vector<fs::directory_entry> entries;
            for (const auto& entry:fs::directory_iterator(from)) entries.push_back(entry);
            if (!fs::create_directories(to))
                throw fs::filesystem_error("move_path: destination already exists",from,to,
                    std::make_error_code(std::errc::file_exists));
            for (const auto& entry:entries) {
                try {
                    const auto child=to/entry.path().filename();
                    const auto status=entry.symlink_status();
                    if (fs::is_symlink(status)) copy_link(entry.path(),child);
                    else if (fs::is_directory(status)) self(self,entry.path(),child);
                    else {
                        fs::copy_file(entry.path(),child);
                        copy_metadata(entry.path(),child);
                    }
                } catch (...) {
                    // Pyのcopytreeは子要素1つのコピーが失敗して
                    // も、兄弟要素の処理とディレクトリのメタデータ設定を続
                    // ける。ツリー全体が成功するまで移動元は削除しない。
                    if (!copy_error) copy_error=::cw::lib::capture_exception();
                }
            }
            try { copy_metadata(from,to); }
            catch (...) { if (!copy_error) copy_error=::cw::lib::capture_exception(); }
        };
        copy_tree(copy_tree,source,target);
        if (copy_error) ::cw::lib::rethrow_exception(copy_error);
        fs::remove_all(source);
    } else {
        fs::copy_file(source,target,fs::copy_options::overwrite_existing);
        copy_metadata(source,target);
        fs::remove(source);
    }
}

void move_path(::cw::lib::String arg_path, ::cw::lib::String arg_dstpath)
{
    move_path_impl(std::move(arg_path), std::move(arg_dstpath), nullptr);
}

void testing::move_path_with_rename_for_testing(
    ::cw::lib::String arg_path,
    ::cw::lib::String arg_dstpath,
    MovePathRename native_rename)
{
    if (!native_rename)
        throw std::invalid_argument("move_path test seam requires a rename primitive");
    move_path_impl(std::move(arg_path), std::move(arg_dstpath), &native_rename);
}

void send_trashbox(::cw::lib::String arg_path)
{
    send_trashbox2({std::move(arg_path)});
}

void send_trashbox2(std::vector<::cw::lib::String> arg_paths)
{
    if (::cw::g_fsync) ::cw::g_fsync->sync();
    for (auto& path : arg_paths) path = normalize_path(std::move(path));
#if defined(_WIN32)
    // SHFileOperationは対応する全Windows版で使え、Python
    // が期待するExplorerのゴミ箱動作を提供する。Pythonと同様に
    // 、SHFileOperationの前に絶対パスへ変換する。相対パスでは
    // FOF_ALLOWUNDOがあってもゴミ箱を経由しない場合があるため
    // 、論理Storageの削除経路は変えず、このAPIの境界で解決する。
    std::wstring wide;
    for (const auto& path : arg_paths) {
        auto absolute = std::filesystem::absolute(std::filesystem::u8path(path)).lexically_normal();
        absolute.make_preferred();
        wide += absolute.native();
        wide.push_back(L'\0');
    }
    wide.push_back(L'\0');
    if (arg_paths.empty()) wide.push_back(L'\0');
    SHFILEOPSTRUCTW operation{};
    operation.wFunc = FO_DELETE;
    operation.pFrom = wide.c_str();
    operation.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;
    const int result = SHFileOperationW(&operation);
    // ネイティブ版の意図的な失敗通知。Pythonはシェルの状態を無視する
    // が、ネイティブの呼び出し元はキャンセル後に削除成功として扱わない。
    if (result != 0 || operation.fAnyOperationsAborted)
        throw std::system_error(result != 0 ? result : ERROR_CANCELLED, std::system_category(), "send_trashbox");
#else
    // PythonのWindows以外の経路は、ゴミ箱ではなくunlink/rmtreeを使う。
    for (const auto& raw : arg_paths) {
        const auto path = std::filesystem::u8path(raw);
        std::error_code ec;
        if (std::filesystem::is_regular_file(path, ec)) std::filesystem::remove(path);
        else if (std::filesystem::is_directory(path, ec)) {
            // shutil.rmtreeはルートがディレクトリのシンボリックリンクなら拒
            // 否するが、remove_allはリンクを削除する。子のリンクはたどらない。
            if (std::filesystem::is_symlink(std::filesystem::symlink_status(path)))
                throw std::filesystem::filesystem_error("send_trashbox: directory symlink", path,
                    std::make_error_code(std::errc::not_a_directory));
            std::filesystem::remove_all(path);
        }
    }
#endif
}

void remove_emptydir(::cw::lib::String arg_dpath)
{
    if (::cw::g_fsync) ::cw::g_fsync->sync();
    arg_dpath = normalize_path(std::move(arg_dpath));
    const auto dir = std::filesystem::u8path(arg_dpath);
    const auto is_directory = [](const std::filesystem::path& path) {
        if (::cardwirth::platform::is_mounted_user_path(path)) {
            const auto status = ::cardwirth::platform::stat_mounted_user_path(path, true);
            return status && status->is_directory();
        }
        std::error_code error;
        return std::filesystem::is_directory(path, error);
    };
    const auto list = [](const std::filesystem::path& path)
        -> std::optional<std::vector<std::tuple<std::filesystem::path, bool, bool>>> {
        std::vector<std::tuple<std::filesystem::path, bool, bool>> result;
        if (::cardwirth::platform::is_mounted_user_path(path)) {
            std::optional<std::vector<::cardwirth::platform::StorageEntry>> entries;
            try { entries = ::cardwirth::platform::list_mounted_user_path(path, true); }
            catch (const std::runtime_error&) { return std::nullopt; } // Pythonのos.walkはscandirのエラーを飛ばす。
            if (!entries) return std::nullopt;
            for (const auto& entry : *entries) result.emplace_back(path / std::filesystem::u8path(entry.name),
                entry.stat.is_directory() || entry.stat.directory_link, entry.stat.is_directory());
        } else {
            std::error_code error;
            std::filesystem::directory_iterator entry_it(path, error), end;
            for (; !error && entry_it != end; entry_it.increment(error)) {
                const auto& entry = *entry_it;
                // symlink_statusはWindowsのジャンクションを含め、リンクを
                // たどらない。Pythonのos.walk(followlinks=False)
                // はディレクトリへのリンクをディレクトリとして数えるが、その中へは進まない。
                std::error_code type_error;
                const bool directory = entry.is_directory(type_error);
                const bool descend = std::filesystem::is_directory(entry.symlink_status(type_error));
                result.emplace_back(entry.path(), directory, descend);
            }
            // 一覧取得の失敗と、正常に確認できた空ディレクトリを区別する。区
            // 別しないと、後始末で確認できなかったデータを削除してしまう。
            if (error) return std::nullopt;
        }
        return result;
    };
    // 省略可能なルートは列挙前に存在を調べ、仮想ディレクトリがないだ
    // けで外側のストレージ処理を失敗扱いにしない。両バックエンドで同
    // じ規則を使い、全子孫がディレクトリの場合だけツリーを削除する。
    if (!is_directory(dir)) return;
    std::vector<std::filesystem::path> pending{dir};
    while (!pending.empty()) {
        const auto current = pending.back(); pending.pop_back();
        const auto children = list(current);
        if (!children) return;
        for (const auto& [child, directory, descend] : *children) {
            if (!directory) return;
            if (descend) pending.push_back(child);
        }
    }
    remove(arg_dpath, false);
}

void copytree_overwrite(::cw::lib::String arg_src, ::cw::lib::String arg_dst, ::cw::lib::Integer arg_files_overwrite)
{
    if (::cw::g_fsync) ::cw::g_fsync->sync();
    arg_src = normalize_path(std::move(arg_src));
    arg_dst = normalize_path(std::move(arg_dst));
    const auto src = std::filesystem::u8path(arg_src);
    const auto dst = std::filesystem::u8path(arg_dst);
    // Pythonのos.walkはdstを作成した後、移動元が不在またはフ
    // ァイルでも何も列挙せず終わる。成功したように見える空のコピーを作る前に
    // 拒否する。スキン導入の呼び出し元は、省略可能な元フォルダを確認済み。
    if (!std::filesystem::is_directory(src)) throw std::runtime_error("copytree_overwrite: source is not a directory");
    std::filesystem::create_directories(dst);
    // Pythonはディレクトリリンクを通してコピーする。解決済みの外部
    // リンク先から相対パスを求めず、コピー先にはリンクの元の名前を保つ。
    std::vector<std::filesystem::path> ancestors{std::filesystem::canonical(src)};
    auto walk = std::filesystem::recursive_directory_iterator(
        src, std::filesystem::directory_options::follow_directory_symlink);
    for (auto end = std::filesystem::recursive_directory_iterator(); walk != end; ++walk) {
        const auto& entry = *walk;
        ancestors.resize(static_cast<std::size_t>(walk.depth()) + 1);
        if (entry.is_directory()) {
            const auto resolved = std::filesystem::canonical(entry.path());
            // Pythonの上限のない再帰と異なり、循環するリンクは安全に拒否する。
            if (std::find(ancestors.begin(), ancestors.end(), resolved) != ancestors.end())
                throw std::runtime_error("copytree_overwrite: cyclic directory link");
            ancestors.push_back(resolved);
        }
        const auto rel = entry.path().lexically_relative(src);
        const auto target = dst / rel;
        if (entry.is_directory()) { std::filesystem::create_directories(target); continue; }
        // Pythonのcopy2はFIFOやデバイス項目を拒否する。ここではパイプ
        // を読んだりOSデバイスをスキンのディレクトリへコピーしたりせず、これらを飛
        // ばす。通常の画像・XML・音声ファイルは引き続き共通のコピー規則に従う。
        if (!entry.is_regular_file()) continue;
        bool copy = true;
        if (std::filesystem::is_regular_file(target)) {
            if (arg_files_overwrite == 1) copy = false;
            else if (arg_files_overwrite == 2) copy = std::filesystem::last_write_time(target) < std::filesystem::last_write_time(entry.path());
        }
        if (copy) {
            // shutil.copy2は既存のコピー先ディレクトリを親として扱
            // う。対象自身がディレクトリの場合、上のPythonのisfile
            // による上書き判定は、最終的にその下へ置く子要素までは検査しない。
            const auto file_target = std::filesystem::is_directory(target)
                ? target / entry.path().filename() : target;
            std::filesystem::copy_file(entry.path(), file_target, std::filesystem::copy_options::overwrite_existing);
            std::filesystem::last_write_time(file_target, std::filesystem::last_write_time(entry.path()));
            std::filesystem::permissions(file_target, entry.status().permissions());
        }
    }
}

// ネイティブのMutexはPythonのthreading.Lockと異なり再
// 帰可能。同じスレッドのコールバックが再取得しても、入れ子のネイティブ呼び出
// しでデッドロックしない。他スレッド間の排他は維持する。したがって自分での非
// 待機再取得は、PythonのLockならFalseとなる場面でも成功する。
::cw::lib::Callback synclock(std::shared_ptr<::cw::lib::Mutex> arg_lock)
{
    return [lock = std::move(arg_lock)](const ::cw::lib::Arguments& args, const ::cw::lib::KeywordArguments&) {
        if (args.empty()) throw std::invalid_argument("synclock decorator requires CallbackHolder");
        const auto* object = std::get_if<::cw::lib::ObjectPtr>(&args.front().storage());
        const auto source = object && *object ? std::dynamic_pointer_cast<CallbackHolder>(*object) : nullptr;
        if (!source || !source->callback) throw std::invalid_argument("synclock argument is not callable");
        auto decorated = std::make_shared<CallbackHolder>();
        // std::functionを値で捕捉し、装飾されたコールバックがCallbackHolder
        // より長く生存できるようにする。Pythonは元の呼び出し可能オブジェクトを保
        // 持するが、ネイティブ版は値として所有する関数オブジェクトの状態をコピーする。
        // 状態を共有したい呼び出し元は、その中で参照かshared_ptrを捕捉する。
        decorated->callback = [lock, callable = source->callback](const ::cw::lib::Arguments& call_args,
                                                                  const ::cw::lib::KeywordArguments& kwargs) {
            // Pyはfを呼ぶ前にlock.acquire()を試す。ロック
            // 不在なら、無施錠でコールバックを動かさずここで失敗させる。
            if (!lock) throw std::invalid_argument("synclock requires a mutex");
            std::lock_guard<::cw::lib::Mutex> guard(*lock);
            return callable(call_args, kwargs);
        };
        return ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(decorated));
    };
}

::cw::lib::Value zip_file(::cw::lib::String arg_path, ::cw::lib::Value arg_mode)
{
    // Pythonの共通の@synclock(_zip_mutex)付き生成処理と同じ範囲でロックする。
    std::lock_guard<std::mutex> archive_guard(g__zip_mutex);
    arg_path = normalize_path(std::move(arg_path));
    const auto mode = value_string(arg_mode);
    // ネイティブ版の意図的な差異。この生成処理は共通の読み取り専用Reader
    // を公開し、製品の全呼び出し元はrを使う。他のモードは、書き込みや追
    // 記のできない未開封オブジェクトを返すのではなく拒否する。直接のLHA
    // ラッパーはPythonと同じく互換用modeを無視し、ここへrを
    // 要求する。アーカイブの作成は別の共通Writer APIを使う。
    if (mode != "r")
        throw std::invalid_argument("zip_file: unsupported mode");
    auto result = parse_zip_archive(arg_path);
    return ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(result));
}

// Pyのos.walkはルートが不在やファイルでも、正常な空
// ZIPを作れる。Writerを作る前に指定の元ディレクト
// リを検証し、不正な圧縮元が通常のアーカイブに見えたり、そ
// の空の結果で既存の保存先を置き換えたりしないようにする。
::cw::lib::String compress_zip(::cw::lib::String arg_path, ::cw::lib::String arg_zpath, bool arg_unicodefilename)
{
    // Pythonはfsyncより前にzip_fileと同じ再入不可のロッ
    // クを取得し、例外で抜ける場合も含め、アーカイブを閉じた後に解放する。
    std::lock_guard<std::mutex> archive_guard(g__zip_mutex);
    if (::cw::g_fsync) ::cw::g_fsync->sync();
    arg_path = normalize_path(std::move(arg_path));
    arg_zpath = normalize_path(std::move(arg_zpath));
    const auto source = std::filesystem::u8path(arg_path);
    const bool mounted_source = ::cardwirth::platform::is_mounted_user_path(source);
    if (mounted_source) {
        const auto status = ::cardwirth::platform::stat_mounted_user_path(source);
        if (!status || !status->is_directory())
            throw std::filesystem::filesystem_error("compress_zip", source, std::make_error_code(std::errc::not_a_directory));
    } else if (!std::filesystem::is_directory(source)) {
        throw std::filesystem::filesystem_error("compress_zip", source, std::make_error_code(std::errc::not_a_directory));
    }
    const auto destination = std::filesystem::u8path(arg_zpath);
    if (!destination.parent_path().empty()) {
        if (::cardwirth::platform::is_mounted_user_path(destination)) {
            if (!::cardwirth::platform::make_mounted_user_directory(destination.parent_path(), true))
                throw std::filesystem::filesystem_error("compress_zip", destination.parent_path(),
                    std::make_error_code(std::errc::io_error));
        } else {
            std::filesystem::create_directories(destination.parent_path());
        }
    }
    // 圧縮・CRC・中央ディレクトリの構築は共通のアーカイブAPI
    // へ任せ、無圧縮だけでなくdeflateにも対応する。出力時
    // 期はPythonと意図的に異なる。Pyは走査前にzpath
    // を切り詰めるが、共通Writerはバイト列を一時保持し、
    // 閉じる時点で公開するため、読み取り側に書きかけを見せない
    // 。ただし、すべての入力エラーで旧ZIPが保たれるとは限ら
    // ない。例外処理中のWriterの破棄でも閉じる処理を行い
    // 、空または途中まで集めたアーカイブを公開する場合がある。
    const auto write_archive = [&](const ::cw::lib::archive::Compression compression) {
        auto writer = ::cw::lib::archive::Writer::create(destination, {6, true});
        // Pyがunicodefilename=Falseを拒否するのはファイルに遭遇し
        // た時点だけ。ディレクトリのみのツリーではZIPのディレクトリ項目を書き出し、ZipInfo
        // は非ASCIIのディレクトリ名も自動的にUTF-8へ符号化する。
        writer.add_path(source, {}, compression, arg_unicodefilename);
        writer.close();
    };
    try {
        write_archive(::cw::lib::archive::Compression::Deflated);
    } catch (const ::cw::lib::archive::BackendUnavailable&) {
        // 単一ファイルの暗号化配布では、隣に実行時DLLを置かない。zlib
        // が使えない場合はPythonのReaderと互換な無圧縮ZIP
        // を使い、ScenarioLog/Party.wslをUserData.data
        // の内側に保つ。シナリオ開始を失敗させたり、ネイ
        // ティブの一時アーカイブへ書き出したりしない。
        write_archive(::cw::lib::archive::Compression::Stored);
    }
    return arg_zpath;
}

::cw::lib::String decompress_zip(::cw::lib::String arg_path, ::cw::lib::String arg_dstdir, ::cw::lib::String arg_dname, std::optional<::cw::lib::Callback> arg_startup, std::optional<::cw::lib::Callback> arg_progress, bool arg_overwrite, std::optional<::cw::lib::Value> arg_z)
{
    if (::cw::g_fsync) ::cw::g_fsync->sync();
    arg_path = normalize_path(std::move(arg_path));
    arg_dstdir = normalize_path(std::move(arg_dstdir));
    std::shared_ptr<ZipArchiveObject> archive;
    bool opened = false;
    try {
        if (arg_z) {
            if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&arg_z->storage()))
                archive = std::dynamic_pointer_cast<ZipArchiveObject>(*object);
        }
        if (archive) {
            opened = true;
            if (!archive->reader) throw std::invalid_argument("decompress_zip: supplied archive is closed");
        } else archive = parse_zip_archive(arg_path);
        opened = true;
        if (archive->lha) {
            // LHA/CAB/ZIPは検証を強化した共通のReader
            // を使う。ここでもそれを使い、レベル0のLHAや対応する圧
            // 縮形式で、無通知で空の展開先を返さないようにする。未対応
            // のコーデックは下のPython互換の失敗経路で報告する。
            const auto& reader = *archive->reader;
            String destination = arg_dstdir;
            String dirname = arg_dname;
            if (dirname.empty()) dirname = std::get<0>(splitext(::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(arg_path).filename())));
            if (!arg_overwrite) destination = dupcheck_plus(join_paths({::cw::lib::Value(destination), ::cw::lib::Value(dirname)}), false, false);
            return extract_archive_with_progress(reader, destination, arg_overwrite, arg_startup, arg_progress, true, [&] { archive->reader.reset(); });
        }
        String destination = arg_dstdir;
        String dirname = arg_dname;
        if (dirname.empty()) dirname = std::get<0>(splitext(::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(arg_path).filename())));
        if (!arg_overwrite) destination = dupcheck_plus(join_paths({::cw::lib::Value(destination), ::cw::lib::Value(dirname)}), false, false);
        const auto mounted_destination = std::filesystem::u8path(destination);
        if (::cardwirth::platform::is_mounted_user_path(mounted_destination)) {
            // アーカイブの内容も実行時データとして扱う。Reader::extract
            // は各項目をマウントされたStorage APIで書くため、暗号化Engine
            // の隣に平文のUserData/Data/Tempツリーを作らな
            // い。ネイティブセッション用には下の従来のZIPループを保つ。
            const auto& reader = *archive->reader;
            return extract_archive_with_progress(reader, destination, arg_overwrite, arg_startup, arg_progress, true, [&] { archive->reader.reset(); });
        }
        // ネイティブZIPの時刻と上書きの挙動は保ち、展開
        // 量の上限と同名項目の扱いは他の全経路と共有する。
        auto extract_options = unbounded_extract_options();
        extract_options.skip_unsafe_paths = true;
        archive->reader->validate_extraction(extract_options);
        ::cw::lib::archive::validate_destination_root(std::filesystem::u8path(destination));
        if (arg_startup) (*arg_startup)({::cw::lib::Value(static_cast<Integer>(archive->entries.size()))}, {});
        Integer count = 0;
        bool cancelled = false;
        for (const auto& entry : archive->entries) {
            if (arg_progress && count % 10 == 0) {
                if (value_truthy((*arg_progress)({::cw::lib::Value(count)}, {}))) {
                    if (!arg_overwrite) {
                        archive->reader.reset();
                        remove(destination, false);
                        return {};
                    }
                    // Pythonは上書きモードではreturnせずループ
                    // を抜ける。処理済み一覧に基づく後始末と最後の進捗コー
                    // ルバックは、シナリオ導入やスキン更新から観測できる。
                    cancelled = true;
                    break;
                }
            }
            ++count;
            String name = decode_zipfilename(entry.name, ::cw::lib::Value(static_cast<Integer>(entry.flags)));
            std::replace(name.begin(), name.end(), '\\', '/');
            if (!::cw::lib::archive::is_safe_relative_path(name)) continue;
            const auto output_path = ::cw::lib::archive::checked_destination(std::filesystem::u8path(destination), name);
            if (!name.empty() && name.back() == '/') {
                std::filesystem::create_directories(output_path);
                (void)::cw::lib::archive::checked_destination(std::filesystem::u8path(destination), name);
                continue;
            }
            std::filesystem::create_directories(output_path.parent_path());
            (void)::cw::lib::archive::checked_destination(std::filesystem::u8path(destination), name);
            std::optional<std::time_t> archived_timestamp;
            // PyのdatetimeはDOS日付が0だと書き込み前に拒否
            // する。ここでは時刻情報がない場合も項目のバイト列を保持し
            // 、0なら時刻設定を省く。0以外は下のmktimeを使う。
            // この条件だけですべての不正な日付を検証するわけではない。
            if (entry.dos_date != 0) {
                std::tm tm{};
                tm.tm_year = static_cast<int>((entry.dos_date >> 9) & 0x7fU) + 80;
                tm.tm_mon = static_cast<int>(((entry.dos_date >> 5) & 0x0fU) - 1U);
                tm.tm_mday = static_cast<int>(entry.dos_date & 0x1fU);
                tm.tm_hour = static_cast<int>((entry.dos_time >> 11) & 0x1fU);
                tm.tm_min = static_cast<int>((entry.dos_time >> 5) & 0x3fU);
                tm.tm_sec = static_cast<int>((entry.dos_time & 0x1fU) * 2U);
                tm.tm_isdst = -1;
                const auto timestamp = std::mktime(&tm);
                if (timestamp != static_cast<std::time_t>(-1)) archived_timestamp = timestamp;
            }
            if (arg_overwrite && archived_timestamp && std::filesystem::is_regular_file(output_path)) {
                std::error_code time_ec;
                const auto current_file_time = std::filesystem::last_write_time(output_path, time_ec);
                if (!time_ec) {
                    const auto current_system_time = ::cardwirth::platform::storage_file_time_to_system(current_file_time);
                    const auto current_timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                        current_system_time.time_since_epoch()).count();
                    // Pyのgetmtime完全一致とは意図的に異な
                    // る。ZIPのDOS時刻は2秒単位のため、既存の
                    // ±1秒の許容幅を保ち、ファイルシステムの丸めだ
                    // けで再生中のMIDIを書き直さないようにする。
                    if (std::llabs(static_cast<long long>(current_timestamp) -
                                   static_cast<long long>(*archived_timestamp)) <= 1)
                        continue;
                }
            }
            const auto bytes = read_zip_entry(*archive, entry);
            const auto write_member = [&] {
                std::ofstream stream(output_path, std::ios::binary | std::ios::trunc);
                stream.exceptions(std::ios::badbit | std::ios::failbit);
                stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                stream.flush();
                // 時刻設定の前にC++のストリームを解放する。Windowsで
                // は共有を許可しないハンドルを開いたまま時刻を変更できない。
                stream.close();
                if (!arg_overwrite) return;
                // Pythonは上書き展開を成功扱いにする前に明示的に
                // fsyncする。永続化の失敗も再試行の対象にする。
#if defined(_WIN32)
                const auto handle = CreateFileW(output_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (handle == INVALID_HANDLE_VALUE)
                    throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "ZIP open for flush");
                const bool flushed = FlushFileBuffers(handle) != 0;
                const auto flush_error = flushed ? ERROR_SUCCESS : GetLastError();
                const bool closed = CloseHandle(handle) != 0;
                const auto close_error = closed ? ERROR_SUCCESS : GetLastError();
                if (!flushed || !closed)
                    throw std::system_error(static_cast<int>(!flushed ? flush_error : close_error),
                                            std::system_category(), "ZIP flush/close");
#else
                const int fd = ::open(output_path.c_str(), O_RDWR);
                if (fd < 0) throw std::system_error(errno, std::generic_category(), "ZIP open for fsync");
                const int flushed = ::fsync(fd);
                const int flush_error = errno;
                const int closed = ::close(fd);
                const int close_error = errno;
                if (flushed != 0 || closed != 0)
                    throw std::system_error(flushed != 0 ? flush_error : close_error,
                                            std::generic_category(), "ZIP fsync/close");
#endif
            };
            try {
                write_member();
            } catch (const std::exception&) {
                if (!arg_overwrite) throw;
                // 最初に開くときだけでなく、書き込み・永続化・クローズのエラ
                // ーもPythonと同様に再試行する。復号はその外で行う。
                displace_archive_output(output_path);
                write_member();
            }
            if (archived_timestamp) {
                    const auto system_time = std::chrono::system_clock::from_time_t(*archived_timestamp);
                    const auto file_time = ::cardwirth::platform::storage_file_time_from_system(
                        std::chrono::time_point_cast<std::filesystem::file_time_type::duration>(system_time));
                    std::filesystem::last_write_time(output_path, file_time);
            }
        }
        const auto entries = archive->reader->entries();
        archive->reader.reset();
        if (arg_overwrite) {
            remove_obsolete_archive_files(entries, std::filesystem::u8path(destination),
                cancelled ? static_cast<std::size_t>(count) : std::numeric_limits<std::size_t>::max());
        }
        if (arg_progress) (*arg_progress)({::cw::lib::Value(
            cancelled ? static_cast<Integer>(archive->entries.size()) : count)}, {});
        return destination;
    } catch (const std::exception& error) {
        ::cardwirth::debug::log("decompress_zip failed: path=" + arg_path +
                                " destination=" + arg_dstdir +
                                " error=" + error.what());
        print_ex(std::optional<::cw::lib::Value>(::cw::lib::Value(arg_path)));
        // Pythonが空の結果に変えるのはアーカイブを開く際の失敗だけ。
        // 開いた後のCRC・入出力・コールバックのエラーはそのまま伝える。
        if (opened) throw;
        return {};
    } catch (...) {
        ::cardwirth::debug::log("decompress_zip failed: path=" + arg_path +
                                " destination=" + arg_dstdir +
                                " error=unknown exception");
        print_ex(std::optional<::cw::lib::Value>(::cw::lib::Value(arg_path)));
        if (opened) throw;
        return {};
    }
}

bool path_exists(const ::cw::lib::String& arg_path)
{
    return !arg_path.empty() && storage_path_exists(std::filesystem::u8path(arg_path));
}

bool path_is_file(const ::cw::lib::String& arg_path)
{
    return !arg_path.empty() && storage_file_exists(std::filesystem::u8path(arg_path));
}

bool path_is_directory(const ::cw::lib::String& arg_path)
{
    if (arg_path.empty()) return false;
    const auto path = std::filesystem::u8path(arg_path);
    if (::cardwirth::platform::is_mounted_user_path(path)) {
        const auto status = ::cardwirth::platform::stat_mounted_user_path(path);
        return status && status->is_directory();
    }
    std::error_code error;
    return std::filesystem::is_directory(path, error);
}

::cw::lib::String decode_zipfilename(::cw::lib::String arg_zname, ::cw::lib::Value arg_info)
{
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&arg_info.storage());
    const auto info = object && *object
        ? std::dynamic_pointer_cast<ArchiveFilenameInfo>(*object) : nullptr;
    const bool is_lha = info && info->kind == ArchiveFilenameInfo::Kind::Lha;
    const auto flags = info ? info->flag_bits : value_integer(arg_info, 0);
    if (!is_lha && (flags & 0x800) != 0) return arg_zname;
    // PyはZipFileのCP437またはLhaFileのLatin1
    // の文字列から元のファイル名バイト列を戻し、その後decode_zipname
    // を実行する。str.encodeと同じく厳密に符号化し、表現
    // できない名前を別のアーカイブ項目へ置き換えない。共通デコーダ
    // のCP932とUTF32-BOMに関する既定の補正は保つ。
    return decode_zipname(::cw::lib::Value(::cw::lib::encoding::encode(
        arg_zname, is_lha ? ::cw::lib::encoding::TextEncoding::Latin1
                         : ::cw::lib::encoding::TextEncoding::Cp437)));
}

::cw::lib::String decode_zipname(::cw::lib::Value arg_name)
{
    if (const auto* value = std::get_if<String>(&arg_name.storage())) return *value;
    if (const auto* bytes = std::get_if<Bytes>(&arg_name.storage()))
        return ::cw::lib::encoding::decode_legacy_archive_name(*bytes);
    return value_string(arg_name);
}

::cw::lib::String decode_text(::cw::lib::Bytes arg_name)
{
    return ::cw::lib::encoding::decode_legacy_text(arg_name);
}

::cw::lib::Bytes read_zipdata(::cw::lib::Value arg_zfile, ::cw::lib::String arg_name)
{
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&arg_zfile.storage());
    const auto archive = object && *object ? std::dynamic_pointer_cast<ZipArchiveObject>(*object) : nullptr;
    if (!archive || !archive->reader) throw std::invalid_argument("read_zipdata: archive is not open for reading");
    // Pythonが捕捉するのは項目不在のKeyErrorだけ。CRC
    // ・入出力・復号の失敗を空の項目に見せず、呼び出し元へ伝える。zipfile
    // とlhafileの名前表は最後の同名項目を保持する。
    for (auto entry = archive->entries.rbegin(); entry != archive->entries.rend(); ++entry)
        if (entry->name == arg_name)
            return read_zip_entry(*archive, *entry);
    print_ex(std::optional<::cw::lib::Value>(::cw::lib::Value(arg_name)));
    return {};
}

std::shared_ptr<::cw::data::CWPyElement> get_elementfromzip(::cw::lib::String arg_zpath, ::cw::lib::String arg_name, ::cw::lib::String arg_tag)
{
    Bytes bytes;
    {
        const auto archive = zip_file(arg_zpath, ::cw::lib::Value("r"));
        bytes = read_zipdata(archive, arg_name);
    } // PythonはXMLやキャッシュの読み込み処理を呼ぶ前にアーカイブ
    // を閉じる。この補助処理でもアーカイブとXMLのエラーをそのまま伝え
    // る。特に項目不在は空のXMLとして解析してエラーとし、ScenarioLog/Property
    // が正常に存在しないものとして扱わない。
    return ::cw::data::xml2element(arg_name, arg_tag, ::cw::lib::Value(std::move(bytes)), std::nullopt, false, std::nullopt);
}

// Pyの進捗表示モードは外側の制御処理が結果を無視するため
// 、展開ワーカーの失敗後も展開先を返す場合がある。ネイティ
// ブ版は終了を待って失敗を確認してから最後の進捗・成功を通
// 知し、進捗表示の有無にかかわらず展開失敗では空を返す。ま
// た、Pyのexpandは-Rなしだと単一項目の名前を入力
// CAB名にする場合があるが、ここでは全ホストで共通Reader
// のCAB項目名を使う。単一・複数項目とも埋め込み名を
// 保ち、外部コマンド固有の命名は意図的に再現しない。
::cw::lib::String decompress_cab(::cw::lib::String arg_path, ::cw::lib::String arg_dstdir, ::cw::lib::String arg_dname, std::optional<::cw::lib::Callback> arg_startup, std::optional<::cw::lib::Callback> arg_progress, bool arg_overwrite)
{
    if (::cw::g_fsync) ::cw::g_fsync->sync();
    arg_path = normalize_path(std::move(arg_path));
    arg_dstdir = normalize_path(std::move(arg_dstdir));
    const Integer count = (arg_startup || arg_progress) ? cab_filenum(arg_path) : 0;
    if (arg_startup) (*arg_startup)({::cw::lib::Value(count)}, {});
    std::optional<::cw::lib::archive::Reader> reader;
    try {
        reader.emplace(::cw::lib::archive::Reader::open(std::filesystem::u8path(arg_path)));
    } catch (...) {
        print_ex(std::optional<::cw::lib::Value>(::cw::lib::Value(arg_path)));
        return {};
    }
    String destination = arg_dstdir;
    if (arg_dname.empty()) arg_dname = std::get<0>(splitext(::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(arg_path).filename())));
    if (!arg_overwrite) destination = dupcheck_plus(join_paths({::cw::lib::Value(destination), ::cw::lib::Value(arg_dname)}), false, false);
    String result;
    try {
        result = extract_cabinet_with_progress(*reader, destination, arg_overwrite, arg_progress);
    } catch (...) {
        print_ex(std::optional<::cw::lib::Value>(::cw::lib::Value(arg_path)));
        return {};
    }
    if (!result.empty() && arg_progress)
        (*arg_progress)({::cw::lib::Value(count)}, {});
    return result;
}

void Progress_decompress_cab___init__(::cw::lib::Value arg_self)
{
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&arg_self.storage());
    const auto progress = object && *object ? std::dynamic_pointer_cast<Progress>(*object) : nullptr;
    if (!progress) throw std::invalid_argument("Progress.__init__ requires Progress object");
    progress->m_result.reset();
    progress->m_cancel = false;
}

void Progress_decompress_cab_run(::cw::lib::Value arg_self)
{
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&arg_self.storage());
    const auto progress = object && *object ? std::dynamic_pointer_cast<Progress>(*object) : nullptr;
    if (!progress) throw std::invalid_argument("Progress.run requires Progress object");
    if (progress->m_cancel || !progress->m_job) return;
    const auto result = progress->m_job({}, {});
    if (!progress->m_cancel) progress->m_result = value_string(result);
}

::cw::lib::Integer cab_filenum(::cw::lib::String arg_cab)
{
    try {
        return ::cw::lib::archive::cabinet_file_count(std::filesystem::u8path(arg_cab));
    } catch (...) {
        return 0;
    }
}

String cabinet_normcase(String name)
{
    // プラットフォーム間で、CW共通の大文字・小文字を区別しないアーカイブ名の規則を保つ。POSIX
    // のos.path.normcaseはWindows版Pyと異なり表記を変えない。
    std::replace(name.begin(), name.end(), '/', '\\');
    return ::cw::lib::encoding::python_lower(name);
}

::cw::lib::String cab_hasfile(::cw::lib::String arg_cab, ::cw::lib::Value arg_fname)
{
    try {
        std::vector<String> names;
        if (const auto* value = std::get_if<String>(&arg_fname.storage())) names.push_back(*value);
        else if (const auto* bytes = std::get_if<Bytes>(&arg_fname.storage())) names.push_back(decode_text(*bytes));
        else if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&arg_fname.storage()); object && *object) {
            if (const auto sequence = std::dynamic_pointer_cast<::cw::data::VariantSequence>(*object))
                for (const auto& item : sequence->m_values) names.push_back(value_string(item));
        }
        String result;
        ::cw::lib::archive::visit_cabinet_names(std::filesystem::u8path(arg_cab), [&](const String& name) {
            auto base = name;
            std::replace(base.begin(), base.end(), '\\', '/');
            base = ::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(base).filename());
            for (const auto& requested : names) {
                if (cabinet_normcase(base) == cabinet_normcase(requested)) {
                    result = name; return false;
                }
            }
            return true;
        });
        return result;
    } catch (...) {
        print_ex(std::optional<::cw::lib::Value>(::cw::lib::Value(arg_cab)));
    }
    return {};
}

std::set<::cw::lib::String> cab_dpaths(::cw::lib::String arg_cab)
{
    std::set<String> result;
    try {
        ::cw::lib::archive::visit_cabinet_names(std::filesystem::u8path(arg_cab), [&](const String& name) {
            const auto slash = name.find_last_of('\\');
            if (slash == String::npos) result.insert(String{});
            else result.insert(name.substr(0, slash));
            return true;
        });
    } catch (...) {
        print_ex(std::optional<::cw::lib::Value>(::cw::lib::Value(arg_cab)));
    }
    return result;
}

::cw::lib::String cab_scdir(::cw::lib::String arg_cab)
{
    auto names = std::make_shared<::cw::data::VariantSequence>();
    names->m_values = {::cw::lib::Value("Summary.xml"), ::cw::lib::Value("Summary.wsm")};
    // 遅延した表の走査を一度だけ行い、CAB内の順序で最初に一致するSummaryを選ぶ。
    const auto selected = cab_hasfile(arg_cab, ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(names)));
    if (selected.empty()) return {};
    auto normalized = selected;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    return ::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(normalized).parent_path());
}

::cw::lib::String encodewrap(::cw::lib::String arg_s)
{
    String result;
    result.reserve(arg_s.size());
    for (std::size_t i = 0; i < arg_s.size(); ++i) {
        const auto c = arg_s[i];
        if (c == '\\') result += "\\\\";
        else if (c == '\n') result += "\\n";
        else if (c != '\r') result.push_back(c);
    }
    return result;
}

::cw::lib::String decodewrap(::cw::lib::String arg_s, ::cw::lib::String arg_code)
{
    // 明示的な空の置換文字列はエスケープされた改行を除去し、省略時
    // だけLFを使う。公開APIと共通の復号処理は同じ実装を使う。
    return ::cw::lib::encoding::decodewrap(arg_s, arg_code);
}

::cw::lib::String encodetextlist(std::vector<::cw::lib::String> arg_arr)
{
    String joined;
    for (std::size_t i = 0; i < arg_arr.size(); ++i) {
        if (i) joined.push_back('\n');
        joined += arg_arr[i];
    }
    return encodewrap(std::move(joined));
}

std::vector<::cw::lib::String> decodetextlist(::cw::lib::String arg_s)
{
    if (arg_s.empty()) return {};
    const auto decoded = decodewrap(std::move(arg_s));
    std::vector<String> result;
    std::size_t begin = 0;
    while (begin <= decoded.size()) {
        const auto end = decoded.find('\n', begin);
        if (end == String::npos) { result.push_back(decoded.substr(begin)); break; }
        result.push_back(decoded.substr(begin, end - begin)); begin = end + 1;
    }
    return result;
}

bool _is_hw(::cw::lib::String arg_c)
{
    const auto characters = ::cw::lib::encoding::utf8_to_utf32(arg_c, ::cw::lib::encoding::ByteOrder::Little);
    if (characters.size() != 4) throw std::invalid_argument("_is_hw requires one Unicode character");
    const auto cp = static_cast<std::uint32_t>(characters[0]) |
        (static_cast<std::uint32_t>(characters[1]) << 8) |
        (static_cast<std::uint32_t>(characters[2]) << 16) |
        (static_cast<std::uint32_t>(characters[3]) << 24);
    return !::cw::lib::python_unicode_width::is_wide(cp);
}

::cw::lib::Integer get_strlen(::cw::lib::String arg_s)
{
    // C++側の意図的な補正。Pythonの初期値なしのreduce
    // は空文字列で例外となるが、配置処理には自然な幅0が必要。
    Integer result = 0;
    std::size_t offset = 0;
    while (offset < arg_s.size()) {
        const auto [code, size] = utf8_next(arg_s, offset);
        (void)size;
        result += codepoint_width(code);
    }
    return result;
}

::cw::lib::Integer get_strlen_ln(::cw::lib::String arg_c)
{
    if (arg_c.empty()) return 0;
    std::size_t offset = 0;
    const auto [code, size] = utf8_next(arg_c, offset);
    (void)size;
    // 原本のunicodedataは1文字だけを受け付ける。残りの文字を黙って無視しない。
    if (offset != arg_c.size()) throw std::invalid_argument("get_strlen_ln: expected one character");
    return codepoint_width(code);
}

::cw::lib::Value get_strlen_lambda_line_3413(::cw::lib::Value arg_a, ::cw::lib::Value arg_b)
{
    using ::cw::lib::Real;
    using ::cw::lib::Value;
    const auto& left = arg_a.storage();
    const auto& right = arg_b.storage();
    if (const auto* text = std::get_if<String>(&left)) {
        if (const auto* other = std::get_if<String>(&right)) return Value(*text + *other);
        throw std::invalid_argument("cannot add string and number");
    }
    const auto number = [](const Value& value) -> Real {
        if (const auto* real = std::get_if<Real>(&value.storage())) return *real;
        if (const auto* integer = std::get_if<Integer>(&value.storage())) return static_cast<Real>(*integer);
        if (const auto* boolean = std::get_if<bool>(&value.storage())) return *boolean ? 1.0 : 0.0;
        throw std::invalid_argument("addition requires numbers or two strings");
    };
    if (std::holds_alternative<Real>(left) || std::holds_alternative<Real>(right))
        return Value(number(arg_a) + number(arg_b));
    const auto integer = [](const Value& value) -> Integer {
        if (const auto* n = std::get_if<Integer>(&value.storage())) return *n;
        if (const auto* b = std::get_if<bool>(&value.storage())) return *b ? 1 : 0;
        throw std::invalid_argument("integer addition requires integers");
    };
    const auto a = integer(arg_a), b = integer(arg_b);
    // 言語差: ネイティブのIntegerに収まらない内部加算は、あふれさせず拒否する。
    if ((b > 0 && a > std::numeric_limits<Integer>::max() - b) ||
        (b < 0 && a < std::numeric_limits<Integer>::min() - b))
        throw std::overflow_error("integer addition overflow");
    return Value(a + b);
}

std::tuple<::cw::lib::String, ::cw::lib::String> slice_str(::cw::lib::String arg_s, ::cw::lib::Integer arg_width, std::optional<::cw::lib::Callback> arg_get_width)
{
    Integer width = 0;
    std::optional<Decimal> wide_width;
    std::optional<Real> real_width;
    ::cw::lib::DecimalContext exact_width;
    exact_width.precision = 0;
    std::size_t offset = 0;
    std::size_t cut = 0;
    while (offset < arg_s.size()) {
        const auto begin = offset;
        const auto [code, size] = utf8_next(arg_s, offset);
        const auto grapheme = arg_s.substr(begin, size == 0 ? offset - begin : static_cast<std::size_t>(size));
        Integer glyph_width = codepoint_width(code);
        std::optional<Real> real_glyph;
        if (arg_get_width) {
            const auto measured = (*arg_get_width)({::cw::lib::Value(grapheme)}, {});
            if (const auto* boolean = std::get_if<bool>(&measured.storage())) glyph_width = *boolean ? 1 : 0;
            else if (const auto* integer = std::get_if<Integer>(&measured.storage())) glyph_width = *integer;
            else if (const auto* real = std::get_if<Real>(&measured.storage())) real_glyph = *real;
            else throw std::invalid_argument("slice_str: width callback must return a number");
        }
        if (real_glyph || real_width) {
            if (!real_width) real_width = wide_width
                ? std::stod(wide_width->to_string(false)) : static_cast<Real>(width);
            const auto next = *real_width + real_glyph.value_or(static_cast<Real>(glyph_width));
            // Pythonのfloat/int比較と同じく、幅の64bit整数を先にdoubleへ丸めない。
            const bool exceeds = next >= 9223372036854775808.0 ||
                (next >= -9223372036854775808.0 && !std::isnan(next) &&
                 (static_cast<Integer>(next) > arg_width ||
                  (static_cast<Integer>(next) == arg_width &&
                   next > static_cast<Real>(static_cast<Integer>(next)))));
            if (exceeds) break;
            real_width = next;
            cut = offset;
            continue;
        }
        // Pythonの整数の加算には上限がない。符号付き整数があふれる
        // 場合だけ型を拡張し、コールバックの合計を正確に保つ。途中の値を
        // 切り詰めたり上限に固定したりせず、後に範囲内へ戻る場合も扱う。
        if (!wide_width && ((glyph_width > 0 && width > std::numeric_limits<Integer>::max() - glyph_width) ||
                            (glyph_width < 0 && width < std::numeric_limits<Integer>::min() - glyph_width)))
            wide_width = Decimal(width);
        if (wide_width) {
            const auto next = wide_width->add(Decimal(glyph_width), exact_width);
            if (next > Decimal(arg_width)) break;
            wide_width = next;
        } else {
            const auto next = width + glyph_width;
            if (next > arg_width) break;
            width = next;
        }
        cut = offset;
    }
    return {arg_s.substr(0, cut), arg_s.substr(cut)};
}

::cw::lib::String rjustify(::cw::lib::String arg_s, ::cw::lib::Integer arg_length, ::cw::lib::String arg_c)
{
    const auto length = get_strlen(arg_s);
    if (arg_length <= length) return arg_s;
    const auto count = arg_length - length;
    String result = arg_s;
    for (Integer i = 0; i < count; ++i) result += arg_c;
    return result;
}

::cw::lib::String ljustify(::cw::lib::String arg_s, ::cw::lib::Integer arg_length, ::cw::lib::String arg_c)
{
    const auto length = get_strlen(arg_s);
    if (arg_length <= length) return arg_s;
    const auto count = arg_length - length;
    String result;
    for (Integer i = 0; i < count; ++i) result += arg_c;
    return result + arg_s;
}

namespace {
Integer utf8_codepoint_count(const String& text)
{
    Integer count = 0;
    std::size_t offset = 0;
    while (offset < text.size()) {
        utf8_next(text, offset);
        ++count;
    }
    return count;
}

String ascii_lower_copy(String text)
{
    for (auto& ch : text) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
    return text;
}

bool txtwrap_is_spchar_pair(const String& chars)
{
    if (chars.empty()) return false;
    std::size_t offset = 0;
    const auto [first, first_bytes] = utf8_next(chars, offset);
    if (first == '#' && offset < chars.size()) {
        // Pythonのreの#.は、DOTALLがないためLF以外のすべてのスカラー値に一致する。
        return std::get<0>(utf8_next(chars, offset)) != '\n';
    }
    if (first != '&' || offset >= chars.size()) return false;
    const auto [second, second_bytes] = utf8_next(chars, offset);
    (void)second_bytes;
    return second >= 0x20 && second <= 0x7E;
}

bool txtwrap_known_special(const String& pair)
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_rsrc) return false;
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_rsrc->m_specialchars.storage());
    if (!object || !*object) return false;
    const auto table = std::dynamic_pointer_cast<::cw::setting::ResourceTable>(*object);
    return table && table->contains(::cw::lib::Value(pair));
}

bool txtwrap_space_compat(::cw::lib::Integer mode)
{
    if (mode == 3 || mode == 4) return true;
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_sct) return false;
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_sdata.storage());
    if (!object || !*object) return false;
    const auto sdata = std::dynamic_pointer_cast<::cw::data::ScenarioData>(*object);
    if (!sdata) return false;
    return !::cw::g_cwpy->m_sct->lessthan("1.20", sdata->get_versionhint(0));
}
}  // namespace

::cw::lib::String txtwrap(::cw::lib::String arg_s, ::cw::lib::Integer arg_mode, ::cw::lib::Integer arg_width, ::cw::lib::String arg_wrapschars, bool arg_encodedtext, std::set<::cw::lib::Integer>* arg_spcharinfo)
{
    String wrapschars = arg_wrapschars;
    Integer width = arg_width;
    const String default_wraps = g_WRAPS_CHARS.empty() ? String{"｡|､|，|、|。|．|）|」|』|〕|｝|】"} : g_WRAPS_CHARS;
    switch (arg_mode) {
    case 1:
        wrapschars = default_wraps;
        width = 37;
        break;
    case 2:
        wrapschars = {};
        width = 32;
        break;
    case 3:
        wrapschars = {};
        width = 42;
        break;
    case 4:
        wrapschars = default_wraps;
        width = 37;
        break;
    case 5:
        wrapschars = default_wraps;
        width = 24;
        break;
    case 6:
        wrapschars = default_wraps;
        width = 48;
        break;
    default:
        break;
    }
    if (arg_encodedtext) arg_s = decodewrap(std::move(arg_s));
    const bool use_wchar = arg_mode != 2 && arg_mode != 3 && !wrapschars.empty();
    std::optional<::cw::lib::python_re::Pattern> wchar_pattern;
    if (use_wchar) wchar_pattern.emplace(wrapschars);
    const bool use_spchar = arg_mode == 2 || arg_mode == 3;
    std::vector<Integer> spcharinfo2;
    const Integer source_len = utf8_codepoint_count(arg_s);
    std::vector<String> seq;
    Integer seqlen = 0;
    Integer cnt = 0;
    Integer asciicnt = 0;
    bool wraped = false;
    bool skip = false;
    bool spchar = false;
    bool defspchar = false;
    bool wrapafter = false;
    String skipchars;
    auto seq_insert = [&](Integer index, const String& ch) {
        auto pos = index;
        if (pos < 0) pos = static_cast<Integer>(seq.size()) + pos;
        if (pos < 0) pos = 0;
        if (pos > static_cast<Integer>(seq.size())) pos = static_cast<Integer>(seq.size());
        seq.insert(seq.begin() + static_cast<std::ptrdiff_t>(pos), ch);
        if (arg_spcharinfo) {
            for (Integer i = static_cast<Integer>(spcharinfo2.size()) - 1; i >= 0; --i) {
                if (spcharinfo2[static_cast<std::size_t>(i)] < pos) break;
                spcharinfo2[static_cast<std::size_t>(i)] += utf8_codepoint_count(ch);
            }
        }
    };
    auto insert_wrap = [&](Integer index) {
        seq_insert(index, "\n");
        if (!arg_spcharinfo) return;
        auto pos = index;
        if (pos < 0) pos = static_cast<Integer>(seq.size()) + pos;
        if (pos == static_cast<Integer>(seq.size())) spcharinfo2.push_back(seqlen);
        else {
            Integer ln = 0;
            const auto last = std::min<Integer>(pos, static_cast<Integer>(seq.size()));
            for (Integer i = 0; i < last; ++i) ln += utf8_codepoint_count(seq[static_cast<std::size_t>(i)]);
            spcharinfo2.push_back(ln);
        }
    };

    Integer index = 0;
    for (const auto& ch : graphemes(arg_s)) {
        const bool spchar2 = spchar;
        spchar = false;
        Integer width2 = width;
        const bool wrapafter2 = wrapafter;
        const bool defspchar2 = defspchar;
        defspchar = false;
        const Integer ch_len = utf8_codepoint_count(ch);

        if (use_spchar && !defspchar2) {
            if (skip) {
                if (arg_spcharinfo && arg_spcharinfo->contains(index)) spcharinfo2.push_back(seqlen);
                seq.push_back(ch);
                seqlen += ch_len;
                skip = false;
                if (!skipchars.empty() && skipchars.front() == '#') {
                    cnt += ch_len;
                    asciicnt = 0;
                    if (width + 1 < cnt) {
                        if (!wrapafter) {
                            insert_wrap(static_cast<Integer>(seq.size()));
                            seqlen += 1;
                        }
                        cnt = 0;
                        asciicnt = 0;
                        wraped = false;
                        wrapafter = true;
                    }
                }
                index += ch_len;
                continue;
            }
            const auto chars = ch + get_char(arg_s, index + 1);
            if (txtwrap_is_spchar_pair(::cw::lib::encoding::python_lower(chars))) {
                if (arg_spcharinfo && arg_spcharinfo->contains(index)) {
                    spcharinfo2.push_back(seqlen);
                    String pair = chars;
                    {
                        std::size_t off = 0;
                        utf8_next(pair, off);
                        if (off < pair.size()) utf8_next(pair, off);
                        pair = ::cw::lib::encoding::python_lower(chars.substr(0, off));
                    }
                    const bool hash = !chars.empty() && chars.front() == '#';
                    if (!hash || !txtwrap_known_special(pair) || width < cnt) {
                        if (width < cnt && hash) {
                            if (!wrapafter) {
                                insert_wrap(static_cast<Integer>(seq.size()));
                                seqlen += 1;
                            }
                            cnt = 0;
                            asciicnt = 0;
                        }
                        seq.push_back(ch);
                        seqlen += ch_len;
                        skip = true;
                        if (hash) cnt += ch_len;
                        skipchars = chars;
                        index += ch_len;
                        continue;
                    }
                    spchar = true;
                    if (chars.empty() || chars.front() != '&') {
                        wrapafter = false;
                        defspchar = true;
                    }
                }
            }
        }

        if (cnt == 0 && !wraped && wchar_pattern && wchar_pattern->match(ch)) {
            seq_insert(-1, ch);
            seqlen += ch_len;
            asciicnt = 0;
            wraped = true;
        } else if (ch == "\n") {
            if (!wrapafter) {
                seq.push_back(ch);
                seqlen += ch_len;
            }
            cnt = 0;
            asciicnt = 0;
            wraped = false;
            wrapafter = false;
        } else if (get_strlen(ch) == 1) {
            seq.push_back(ch);
            seqlen += ch_len;
            cnt += 1;
            const bool skip_ascii = (arg_mode == 2 || arg_mode == 3) || ((arg_mode == 1 || arg_mode == 4) && ch == " ");
            if (!skip_ascii && !(arg_mode == 1 && index + 1 < source_len && get_strlen(get_char(arg_s, index + 1)) != 1))
                asciicnt += 1;
            if (spchar2 || !skip_ascii || index + 1 >= source_len || get_strlen(get_char(arg_s, index + 1)) == 1)
                width2 += 1;
            wrapafter = false;
        } else {
            seq.push_back(ch);
            seqlen += ch_len;
            cnt += 2;
            asciicnt = 0;
            wrapafter = false;
            if ((arg_mode == 1 || arg_mode == 2 || arg_mode == 3) && index + 1 < source_len && get_strlen(get_char(arg_s, index + 1)) == 1)
                width2 += 1;
        }

        if (txtwrap_space_compat(arg_mode)) {
            if (!wrapafter2 && index + 1 < source_len && get_char(arg_s, index + 1) == " " &&
                (arg_mode == 1 || arg_mode == 2 || arg_mode == 3 || arg_mode == 4)) {
                width2 += 1;
                asciicnt = 0;
            }
        }

        if (!spchar && cnt > width2) {
            if (defspchar2 && width2 + 1 < cnt) {
                // Pythonはここでループ位置を上書きする。後続の特殊文
                // 字の位置を一致させるため、折り返し位置は別に保持する。
                const Integer wrap_index = -(cnt - (width + 1));
                const Integer check_index = -wrap_index;
                if (check_index >= 0 && check_index < static_cast<Integer>(seq.size()) &&
                    seq[static_cast<std::size_t>(check_index)] != "\n") {
                    insert_wrap(wrap_index);
                    seqlen += 1;
                }
                cnt = 1;
            } else if (width2 >= asciicnt && asciicnt > 0 && !defspchar2) {
                if (get_char(arg_s, index + 1) != "\n" &&
                    static_cast<Integer>(seq.size()) >= asciicnt &&
                    seq[seq.size() - static_cast<std::size_t>(asciicnt)] != "\n") {
                    insert_wrap(-asciicnt);
                    seqlen += 1;
                }
                cnt = asciicnt;
            } else if (index + 1 <= source_len || get_char(arg_s, index + 1) != "\n") {
                if (index + 2 <= source_len || get_char(arg_s, index + 2) != "\n") {
                    insert_wrap(static_cast<Integer>(seq.size()));
                    seqlen += 1;
                    wrapafter = true;
                }
                cnt = 0;
                asciicnt = 0;
                wraped = false;
            }
        }
        index += ch_len;
    }

    if (arg_spcharinfo) {
        arg_spcharinfo->clear();
        arg_spcharinfo->insert(spcharinfo2.begin(), spcharinfo2.end());
    }
    String result;
    for (const auto& part : seq) result += part;
    return ::cw::lib::python_rstrip(result);
}

std::vector<::cw::lib::String> graphemes(::cw::lib::String arg_s)
{
    return ::cw::lib::regex_grapheme::graphemes(std::move(arg_s));
}

::cw::lib::String _wordwrap_impl(::cw::lib::String arg_s, ::cw::lib::Integer arg_width, std::optional<::cw::lib::Callback> arg_get_width, ::cw::lib::String arg_open_chars, ::cw::lib::String arg_close_chars, ::cw::lib::Integer arg_startindex, ::cw::lib::Integer arg_resultindex, std::set<::cw::lib::Integer>* arg_spcharinfo, std::vector<::cw::lib::Integer>* arg_spcharinfo2)
{
    // Pyはwidth <= 0でも幅を測定し、特殊文字の位置を検証する。
    auto width_of = [&](const String& text) -> Integer {
        Integer value = get_strlen(text);
        if (arg_get_width) value = value_integer((*arg_get_width)({::cw::lib::Value(text)}, {}), value);
        return value;
    };
    auto first_char = [](const String& text) -> String {
        if (text.empty()) return {};
        std::size_t offset = 0;
        const auto begin = offset;
        utf8_next(text, offset);
        return text.substr(begin, offset - begin);
    };
    auto rest_chars = [](const String& text) -> String {
        std::size_t offset = 0;
        utf8_next(text, offset);
        return offset >= text.size() ? String{} : text.substr(offset);
    };
    auto is_space_word = [](const String& text) -> bool {
        if (text.empty()) return false;
        std::size_t offset = 0;
        while (offset < text.size()) {
            const auto [code, bytes] = utf8_next(text, offset);
            (void)bytes;
            // Pythonはstr.isspace()で真となるすべてのトークンの行末へのはみ出しを許
            // 容する。U+001F、1680、202F、205Fを含む既存の共通スカラー規則を使う。
            const bool space = ::cw::lib::is_python_str_whitespace(code);
            if (!space) return false;
        }
        return true;
    };

    auto tokens = ::cw::lib::regex_grapheme::findall_word_tokens(arg_s);
    if (arg_spcharinfo) {
        std::vector<String> split;
        Integer index = arg_startindex;
        String pending;
        for (const auto& word : tokens) {
            if (!pending.empty()) {
                split.push_back(pending + first_char(word));
                const auto rest = rest_chars(word);
                if (!rest.empty()) split.push_back(rest);
                pending.clear();
            } else if (arg_spcharinfo->contains(index)) {
                pending = word;
            } else {
                split.push_back(word);
            }
            index += utf8_codepoint_count(word);
        }
        // 元の_wordwrap_implは特殊コードの接頭辞に後続トークンがある
        // ことをアサーションで確認する。移植可能な公開入力でも、文字列を黙って失う
        // のではなく、同じ前提違反をinvalid_argumentで報告する。
        if (!pending.empty()) throw std::invalid_argument("dangling wordwrap special token");
        tokens = std::move(split);
    }

    using Buf = std::tuple<String, bool>;
    std::vector<std::vector<Buf>> lines;
    std::vector<Buf> buf;
    Integer buflen = 0;
    const Integer hw = width_of("#");
    Integer index = arg_startindex;

    auto match_op = [&](const Buf& item) {
        return !std::get<1>(item) && arg_open_chars.find(std::get<0>(item)) != String::npos;
    };
    auto match_cl = [&](const Buf& item) {
        return !std::get<1>(item) && arg_close_chars.find(std::get<0>(item)) != String::npos;
    };
    auto match_last = [&](const std::vector<Buf>& items, auto matcher) {
        for (Integer i = 0; i < static_cast<Integer>(items.size()); ++i) {
            const auto& item = items[items.size() - 1 - static_cast<std::size_t>(i)];
            if (std::get<1>(item) && !std::get<0>(item).empty() && std::get<0>(item).front() == '&') continue;
            return matcher(item);
        }
        return false;
    };

    auto append_word_wrap = [&](std::vector<Buf> current, Integer current_len, String word, bool is_spchar)
        -> std::tuple<std::vector<Buf>, Integer, String> {
        if (is_spchar) return {std::move(current), current_len, std::move(word)};
        while (arg_width < current_len + width_of(word)) {
            auto [left, right] = slice_str(word, arg_width - current_len, arg_get_width);
            if (!left.empty()) left += "-";
            current.emplace_back(left, false);
            lines.push_back(std::move(current));
            current.clear();
            current_len = 0;
            word = std::move(right);
        }
        // Pyは状態を持つコールバックによって内側のループが不要になった場合
        // でも、このバッファを破棄する。観測可能なコールバックの挙動を保つ。
        return {std::vector<Buf>{}, Integer{0}, std::move(word)};
    };

    auto break_before_openchar = [&](std::vector<Buf> buf2, std::vector<Buf> current, Integer current_len, String word, bool is_spchar)
        -> std::tuple<std::vector<Buf>, Integer, String> {
        while (!buf2.empty() && match_last(buf2, match_op)) buf2.pop_back();
        if (!buf2.empty()) {
            const auto cut = buf2.size();
            lines.emplace_back(current.begin(), current.begin() + static_cast<std::ptrdiff_t>(cut));
            current.erase(current.begin(), current.begin() + static_cast<std::ptrdiff_t>(cut));
            current_len = 0;
            for (const auto& item : current) current_len += width_of(std::get<0>(item));
            return {std::move(current), current_len, std::move(word)};
        }
        return append_word_wrap(std::move(current), current_len, std::move(word), is_spchar);
    };

    for (auto word : tokens) {
        const bool is_spchar = arg_spcharinfo && arg_spcharinfo->contains(index);
        Integer wordlen = width_of(word);
        if (arg_width < buflen + wordlen) {
            if (!buf.empty() && match_last(buf, match_op) && !match_last(buf, match_cl)) {
                std::tie(buf, buflen, word) = break_before_openchar(buf, buf, buflen, std::move(word), is_spchar);
                wordlen = width_of(word);
            } else if (!is_space_word(word)) {
                if (match_cl(Buf{word, is_spchar})) {
                    if (arg_width < buflen || (arg_width == buflen && hw < wordlen)) {
                        auto buf2 = buf;
                        while (!buf2.empty() && match_last(buf2, match_cl)) buf2.pop_back();
                        if (buf2.empty() || (buf2.size() == 1 && !match_last(buf2, match_op))) {
                            lines.push_back(buf);
                            buf.clear();
                            buflen = 0;
                        } else if (buf2.size() >= 2 && !match_last(std::vector<Buf>(buf2.begin(), buf2.end() - 1), match_op)) {
                            const auto cut = buf2.size() - 1;
                            lines.emplace_back(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(cut));
                            buf.erase(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(cut));
                            buflen = 0;
                            for (const auto& item : buf) buflen += width_of(std::get<0>(item));
                        } else {
                            std::tie(buf, buflen, word) = break_before_openchar(buf2, buf, buflen, std::move(word), is_spchar);
                            wordlen = width_of(word);
                        }
                    }
                } else {
                    if (!buf.empty()) {
                        lines.push_back(buf);
                        buf.clear();
                        buflen = 0;
                    }
                    std::tie(buf, buflen, word) = append_word_wrap(std::move(buf), buflen, std::move(word), is_spchar);
                    wordlen = width_of(word);
                }
            }
        }
        if (!word.empty()) {
            buf.emplace_back(word, is_spchar);
            if (!is_spchar || word.front() != '&') buflen += wordlen;
        }
        index += utf8_codepoint_count(word);
    }
    if (!buf.empty()) lines.push_back(buf);

    if (!arg_spcharinfo2) {
        String result;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            if (i) result.push_back('\n');
            for (const auto& item : lines[i]) result += std::get<0>(item);
        }
        return result;
    }

    String result;
    Integer resultindex = arg_resultindex;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        for (const auto& [word, is_spchar] : lines[i]) {
            if (is_spchar) arg_spcharinfo2->push_back(resultindex);
            result += word;
            resultindex += utf8_codepoint_count(word);
        }
        if (i + 1 < lines.size()) {
            arg_spcharinfo2->push_back(resultindex);
            result.push_back('\n');
            resultindex += 1;
        }
    }
    return result;
}

bool _wordwrap_impl_match_op(std::tuple<::cw::lib::String, bool> arg_buf)
{
    return std::get<1>(arg_buf);
}

bool _wordwrap_impl_match_cl(std::tuple<::cw::lib::String, bool> arg_buf)
{
    return !std::get<1>(arg_buf);
}

bool _wordwrap_impl_match_last(std::vector<std::tuple<::cw::lib::String, bool>> arg_bufs, ::cw::lib::Callback arg_matcher)
{
    if (arg_bufs.empty()) return false;
    const auto& last = arg_bufs.back();
    const auto value = arg_matcher({::cw::lib::Value(std::get<0>(last)), ::cw::lib::Value(std::get<1>(last))}, {});
    return value_truthy(value);
}

bool _wordwrap_impl_match_op_last(std::vector<std::tuple<::cw::lib::String, bool>> arg_bufs)
{
    return !arg_bufs.empty() && _wordwrap_impl_match_op(arg_bufs.back());
}

bool _wordwrap_impl_match_cl_last(std::vector<std::tuple<::cw::lib::String, bool>> arg_bufs)
{
    return !arg_bufs.empty() && _wordwrap_impl_match_cl(arg_bufs.back());
}

std::tuple<std::vector<std::tuple<::cw::lib::String, bool>>, ::cw::lib::Integer, ::cw::lib::String> _wordwrap_impl_append_word_wrap(std::vector<std::tuple<::cw::lib::String, bool>> arg_buf, ::cw::lib::Integer arg_buflen, ::cw::lib::String arg_word)
{
    arg_buf.emplace_back(arg_word, false);
    arg_buflen += get_strlen(arg_word);
    return {std::move(arg_buf), arg_buflen, {}};
}

std::tuple<std::vector<std::tuple<::cw::lib::String, bool>>, ::cw::lib::Integer, ::cw::lib::String> _wordwrap_impl_break_before_openchar(std::vector<std::tuple<::cw::lib::String, bool>> arg_buf2, std::vector<std::tuple<::cw::lib::String, bool>> arg_buf, ::cw::lib::Integer arg_buflen, ::cw::lib::String arg_word)
{
    (void)arg_buf2; (void)arg_buflen;
    arg_buf.push_back({"\n", false});
    arg_buf.push_back({arg_word, false});
    return {std::move(arg_buf), get_strlen(arg_word), arg_word};
}

::cw::lib::String wordwrap(::cw::lib::String arg_s, ::cw::lib::Integer arg_width, std::optional<::cw::lib::Callback> arg_get_width, ::cw::lib::String arg_open_chars, ::cw::lib::String arg_close_chars, std::set<::cw::lib::Integer>* arg_spcharinfo)
{
    // ネイティブAPIの意図的な橋渡し。移植された呼び出し元はPyの
    // 省略引数に空文字列を渡すため、既定値を選ぶ印として扱う。Pyで
    // 禁則リストを明示的に空にする場合とは意味が異なる。公開APIの
    // 省略引数と既存の明示的な空文字列指定は、この経路を共有する。
    static const String kOpenChars = "\"'(<[`{‘“〈《≪「『【〔（＜［｛｢";
    static const String kCloseChars = "!\"'),.:;>?]`}゜’”′″、。々＞》≫」』】〕〟゛°ゝゞヽヾ〻！），．：；＞？］｝｡｣､ﾞﾟ"
                                      "ぁぃぅぇぉァィゥェォｧｨｩｪｫヵっッｯゃゅょャュョｬｭｮゎヮㇵㇶㇷㇸㇹㇺ…―ーｰ";
    if (arg_open_chars.empty()) arg_open_chars = kOpenChars;
    if (arg_close_chars.empty()) arg_close_chars = kCloseChars;
    std::vector<Integer> spcharinfo2;
    std::vector<String> wrapped_lines;
    Integer index = 0;
    Integer resultindex = 0;
    for (const auto& line : ::cw::lib::encoding::splitlines(arg_s)) {
        auto wrapped = _wordwrap_impl(line, arg_width, arg_get_width, arg_open_chars, arg_close_chars, index, resultindex,
                                      arg_spcharinfo, arg_spcharinfo ? &spcharinfo2 : nullptr);
        wrapped_lines.push_back(wrapped);
        index += utf8_codepoint_count(line) + 1;
        resultindex += utf8_codepoint_count(wrapped) + 1;
    }
    if (arg_spcharinfo) {
        arg_spcharinfo->clear();
        arg_spcharinfo->insert(spcharinfo2.begin(), spcharinfo2.end());
    }
    String result;
    for (std::size_t i = 0; i < wrapped_lines.size(); ++i) {
        if (i) result.push_back('\n');
        result += wrapped_lines[i];
    }
    return result;
}

std::tuple<::cw::lib::String, std::set<::cw::lib::Integer>> _test_wordwrap(::cw::lib::String arg_s, ::cw::lib::Integer arg_width, std::set<::cw::lib::Integer> arg_spcharinfo)
{
    const auto wrapped = wordwrap(arg_s, arg_width, std::nullopt, {}, {}, &arg_spcharinfo);
    return {wrapped, std::move(arg_spcharinfo)};
}

::cw::lib::String get_char(::cw::lib::String arg_s, ::cw::lib::Integer arg_index)
{
    if (arg_index < 0) return {};
    std::size_t offset = 0;
    Integer index = 0;
    while (offset < arg_s.size()) {
        const auto begin = offset;
        const auto [code, bytes] = utf8_next(arg_s, offset);
        if (index++ == arg_index) return arg_s.substr(begin, static_cast<std::size_t>(bytes));
        (void)code;
    }
    return {};
}

::cw::lib::String format_title(::cw::lib::String arg_fmt, std::map<::cw::lib::String, ::cw::lib::String> arg_d, bool arg_use_lf)
{
    struct Part {
        enum class Kind { Literal, Variable, Section };
        Kind kind{Kind::Literal};
        String text{};
        std::vector<Part> children{};
    };
    const auto eat = [&](auto&& self, std::size_t& pos, bool subsection) -> std::vector<Part> {
        std::vector<Part> seq;
        bool backslash = false;
        while (pos < arg_fmt.size()) {
            const auto begin = pos;
            const auto [code, size] = utf8_next(arg_fmt, pos);
            (void)code;
            const auto ch = arg_fmt.substr(begin, size == 0 ? pos - begin : static_cast<std::size_t>(size));
            if (backslash) {
                Part part;
                part.kind = Part::Kind::Literal;
                part.text = (ch == "n" && arg_use_lf) ? String("\n") : ch;
                seq.push_back(std::move(part));
                backslash = false;
                continue;
            }
            if (ch == "\\") { backslash = true; continue; }
            if (subsection && ch == "]") break;
            if (ch == "%") {
                const auto end = arg_fmt.find('%', pos);
                if (end != String::npos) {
                    Part part;
                    part.kind = Part::Kind::Variable;
                    part.text = arg_fmt.substr(pos, end - pos);
                    seq.push_back(std::move(part));
                    pos = end + 1;
                }
                continue;
            }
            if (ch == "[") {
                Part part;
                part.kind = Part::Kind::Section;
                part.children = self(self, pos, true);
                seq.push_back(std::move(part));
                continue;
            }
            Part part;
            part.kind = Part::Kind::Literal;
            part.text = ch;
            seq.push_back(std::move(part));
        }
        return seq;
    };
    std::size_t pos = 0;
    const auto sections = eat(eat, pos, false);
    const auto format = [&](auto&& self, const std::vector<Part>& secs) -> std::pair<String, bool> {
        String result;
        bool used = false;
        for (const auto& sec : secs) {
            if (sec.kind == Part::Kind::Variable) {
                const auto it = arg_d.find(sec.text);
                if (it != arg_d.end() && !it->second.empty()) {
                    used = true;
                    result += it->second;
                }
            } else if (sec.kind == Part::Kind::Section) {
                const auto [text, nested] = self(self, sec.children);
                if (nested) {
                    result += text;
                    used = true;
                }
            } else {
                result += sec.text;
            }
        }
        return {std::move(result), used};
    };
    return format(format, sections).first;
}

void _FormatPart_format_title___init__(::cw::lib::Value arg_self, ::cw::lib::String arg_name)
{
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&arg_self.storage()); object && *object) {
        if (const auto part = std::dynamic_pointer_cast<_FormatPart>(*object)) part->m_name_typed = std::move(arg_name);
    }
}

void _AbsList_format_title___init__(::cw::lib::Value arg_self)
{
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&arg_self.storage()); object && *object)
        if (const auto list = std::dynamic_pointer_cast<_AbsList>(*object)) list->m_seq_typed.clear();
}

void _List_format_title___init__(::cw::lib::Value arg_self, std::vector<::cw::lib::Value> arg_seq)
{
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&arg_self.storage()); object && *object)
        if (const auto list = std::dynamic_pointer_cast<_List>(*object)) list->m_seq_typed = std::move(arg_seq);
}

std::tuple<::cw::lib::String, std::vector<::cw::lib::Value>> format_title_eat_parts(::cw::lib::String arg_fmt, bool arg_subsection)
{
    std::vector<::cw::lib::Value> sections;
    String text;
    std::size_t pos = 0;
    const char end = arg_subsection ? ']' : '\0';
    while (pos < arg_fmt.size()) {
        if (end != '\0' && arg_fmt[pos] == end) { ++pos; break; }
        if (arg_fmt[pos] == '[') {
            if (!text.empty()) { sections.emplace_back(std::move(text)); text.clear(); }
            std::size_t depth = 1; const auto begin = pos++;
            while (pos < arg_fmt.size() && depth != 0) { if (arg_fmt[pos] == '[') ++depth; else if (arg_fmt[pos] == ']') --depth; ++pos; }
            sections.emplace_back(arg_fmt.substr(begin, pos - begin));
        } else text.push_back(arg_fmt[pos++]);
    }
    if (!text.empty()) sections.emplace_back(std::move(text));
    return {arg_fmt.substr(pos), std::move(sections)};
}

std::tuple<::cw::lib::String, bool> format_title_do_format(std::vector<::cw::lib::Value> arg_secs)
{
    String result;
    bool present = true;
    for (const auto& section : arg_secs) {
        const auto value = value_string(section);
        if (value.empty() && !section.is_none()) present = false;
        result += value;
    }
    return {std::move(result), present};
}

std::shared_ptr<::cw::lib::Bitmap> load_wxbmp(::cw::lib::String arg_name, bool arg_mask, std::shared_ptr<::cw::lib::Image> arg_image, ::cw::lib::Value arg_maskpos, std::optional<::cw::lib::Value> arg_f, bool arg_retry, bool arg_can_loaded_scaledimage, bool arg_noscale, std::optional<::cw::lib::Value> arg_up_scr)
{
    (void)arg_retry;
    // wxのビットマップ読み込みもload_image/Pythonと
    // 同じく、倍率付きファイルを探す前に論理リソースパスを解決する。
    if (!arg_name.empty() && !::cw::binary::image::path_is_code(arg_name) &&
        ::cw::g_cwpy && ::cw::g_cwpy->m_rsrc) {
        arg_name = ::cw::g_cwpy->m_rsrc->get_filepath(
            ::cw::util::normalize_path(std::move(arg_name)));
    }
    auto result = std::make_shared<::cw::lib::Bitmap>();
    bool source_has_alpha = false;
    Bytes data;
    bool supplied_data = false;
    const auto [resolved_path, selected_scale] = find_scaledimagepath(
        arg_name, arg_up_scr ? arg_up_scr->as_real() : ::cw::g_UP_SCR,
        arg_can_loaded_scaledimage, arg_noscale);
    arg_name = resolved_path;
    std::size_t frame_count = 1;
    if (arg_f) {
        if (const auto* bytes = std::get_if<Bytes>(&arg_f->storage())) {
            data = *bytes;
            supplied_data = true;
        }
    }
    if (arg_image) {
        static_cast<::cw::lib::Surface&>(*result) = static_cast<const ::cw::lib::Surface&>(*arg_image);
        source_has_alpha = arg_image->has_pixel_alpha;
    } else {
        std::shared_ptr<::cw::lib::Surface> surface;
        try {
            if (!supplied_data && ::cw::binary::image::path_is_code(arg_name)) {
                data = ::cw::binary::image::code_to_data(arg_name);
                supplied_data = true;
            }
            if (!supplied_data) {
                if (!resolved_path.empty())
                    (void)read_binary_path(std::filesystem::u8path(resolved_path), data);
            }
            // Pyのマスクなしのビットマップ経路は、fが渡されても選択したファ
            // イル名を使う。マスクありでは復号前に3つの補正をすべて実行する。
            const auto encoded_ext = get_imageext(data);
            const bool pillow_input = encoded_ext == ".webp" || encoded_ext == ".apng";
            if (!arg_mask && supplied_data && !pillow_input && !::cw::binary::image::path_is_code(arg_name)) {
                data.clear();
                (void)read_binary_path(std::filesystem::u8path(arg_name), data);
            }
            if (!data.empty()) {
                if (arg_mask && !pillow_input) {
                    auto [fixed32, unchanged32] = ::cw::image::fix_cwnext32bitbitmap(std::move(data));
                    auto [fixed16, unchanged16] = ::cw::image::fix_cwnext16bitbitmap(std::move(fixed32));
                    if (const auto* bytes = std::get_if<Bytes>(&fixed16.storage())) data = *bytes;
                    auto [fixed8, unchanged8] = ::cw::image::patch_rle8bitmap(std::move(data));
                    data = std::move(fixed8);
                    // Pyはすべての補正でデータが変わらなければwx.Image(name)を使う。
                    if (unchanged32 && unchanged16 && unchanged8 && !arg_name.empty() &&
                        !::cw::binary::image::path_is_code(arg_name)) {
                        data.clear();
                        (void)read_binary_path(std::filesystem::u8path(arg_name), data);
                    }
                }
                try {
                    const auto info = ::cw::lib::imagecodec::inspect(
                        std::span<const std::uint8_t>(data.data(), data.size()));
                    // PyのPillow経路は不透明なWebP/APNGでも明示的にRGBA
                    // を生成する。画素が不透明でもアルファチャンネルの存在は保つ。
                    source_has_alpha = info.has_alpha || pillow_input;
                    frame_count = info.frame_count;
                } catch (const ::cw::lib::imagecodec::Error&) {
                    source_has_alpha = false;
                }
                surface = load_image_load_pillow_surface(data);
            }
        } catch (const std::exception&) {
            // Pyのload_wxbmpは読み込み・補正・復号の失敗を捕捉
            // し、例外を外へ出さず、マスク付きの1×1の代替画像を返す。
            return load_wxbmp_masked_empty_bitmap();
        }
        if (!surface || surface->size.width <= 0 || surface->size.height <= 0 || surface->rgba.empty())
            return load_wxbmp_masked_empty_bitmap();
        static_cast<::cw::lib::Surface&>(*result) = *surface;
        // Pythonのload_wxbmpは要求倍率ではなく、選択したファイルの倍
        // 率を保持する。共通ダイアログはその倍率でビットマップを一度だけ正規化する。
        result->scr_scale = static_cast<Real>(selected_scale);
        result->logical_scale = static_cast<Real>(selected_scale);
    }

    result->has_pixel_alpha = source_has_alpha;
    result->scr_scale = static_cast<Real>(selected_scale);
    result->logical_scale = static_cast<Real>(selected_scale);
    if (arg_mask && !source_has_alpha && !result->rgba.empty()) {
        const auto [mx, my] = convert_maskpos(arg_maskpos, result->size.width, result->size.height);
        if (mx >= 0 && my >= 0 && mx < result->size.width && my < result->size.height) {
            const auto index = (my * result->size.width + mx) * 4;
            if (index + 2 >= static_cast<Integer>(result->rgba.size())) return result;
            const auto r = result->rgba[static_cast<std::size_t>(index)];
            const auto g = result->rgba[static_cast<std::size_t>(index + 1)];
            const auto b = result->rgba[static_cast<std::size_t>(index + 2)];
            // 互換層のBitmapは非乗算済みRGBAと元のマスクRGBを保持する。Windows
            // のwxはConvertToBitmap/ConvertToImageで非
            // 表示のRGBをGDIのマスク用値へ置き換えるが、共通のネイティブ格納形
            // 式ではGDIのバイト列を模倣せず、アルファ0の下にも元のRGBを残す。
            for (std::size_t i = 0; i + 3 < result->rgba.size(); i += 4)
                if (result->rgba[i] == r && result->rgba[i + 1] == g && result->rgba[i + 2] == b) result->rgba[i + 3] = 0;
            result->bmpdepthis1 = ::cw::image::get_bmpdepth_view(data) == 1;
            // Pythonのload_wxbmpは、実際にマスク付き1ビットBMP
            // の場合だけ特殊なカード合成の対象にする。高解像度のRGBファイルは
            // 通常のカラー画像であり、ファイル倍率とビット深度は独立している。
            ::cw::imageretouch::set_surface_metadata(result, {
                true, {r, g, b}, result->bmpdepthis1,
                result->scr_scale > 0.0 ? result->scr_scale : 1.0});
        }
    }
    // ダイアログのリソースは静止画とは限らない。読み込み時に選択ファイ
    // ルとフレームの識別情報を保ち、Presentationの利用側
    // がダイアログ固有のタイマーなしでAPNG/WebPを進められる
    // ようにする。通常画像は登録せず、定期的な再描画を発生させない。
    if (frame_count > 1 && !resolved_path.empty()) {
        // 読み込み時点で共通デコーダを開始する。最初の合成処
        // 理がビットマップを解決するまで取得を遅らせると、復
        // 号開始前にダイアログが見えてしまう。所有者は既存
        // の共通DialogBitmapのままとし、ダイア
        // ログ別・プラットフォーム別の再生経路は作らない。
        const auto owner = String("DialogBitmap:") + resolved_path;
        const auto instance = ::cw::animatedimage::acquire(
            resolved_path, owner, false, std::nullopt);
        set_animated_wxbmp_info(result, resolved_path, instance, 0,
            arg_mask, arg_maskpos, ::cw::lib::Value(static_cast<Real>(selected_scale)), owner);
    } else if (frame_count > 1 && supplied_data) {
        // アーカイブ内画像やコード画像にはファイルシステムのパスがない。共
        // 通デコーダへ、対応形式を示す安定した仮想パスを渡し、バイト列を
        // 元にしたスロットを一度だけ取得する。その後はインスタンスIDで
        // フレームを調べ、ホストの更新確認ごとに画像全体をコピーしない。
        const auto extension = ::cw::animatedimage::get_supported_extension(arg_name, data);
        if (!extension.empty()) {
            auto animation_path = arg_name;
            if (!::cw::animatedimage::is_supported_path(animation_path)) animation_path += extension;
            const auto owner = String("DialogBitmapData:") + md5_hex(data);
            const auto animation_data = std::optional<Bytes>(data);
            const auto instance = ::cw::animatedimage::acquire(
                animation_path, owner, false, animation_data);
            if (instance) set_animated_wxbmp_info(result, std::move(animation_path), instance, 0,
                arg_mask, arg_maskpos, ::cw::lib::Value(static_cast<Real>(selected_scale)), owner,
                animation_data);
        }
    }
    return result;
}

std::shared_ptr<::cw::lib::Bitmap> load_wxbmp_masked_empty_bitmap()
{
    // Pyは不透明なRGBA画素ではなく、透過色キーのマスクを持つRGB画像を使う。
    auto result = empty_bitmap(1, 1);
    result->has_colorkey = true;
    result->colorkey = {0, 0, 0};
    return result;
}

void set_animated_wxbmp_info(std::shared_ptr<::cw::lib::Bitmap> arg_wxbmp, ::cw::lib::String arg_path, std::optional<::cw::lib::Integer> arg_instance_id, ::cw::lib::Integer arg_frame_id, bool arg_mask, ::cw::lib::Value arg_maskpos, ::cw::lib::Value arg_scr_scale, std::optional<::cw::lib::String> arg_owner, std::optional<::cw::lib::Bytes> arg_data)
{
    if (!arg_wxbmp) return;
    // Pyの登録処理は値を保存するだけ。名前付き座標は、後で取得する実際のフレ
    // ームにアルファチャンネルがない場合だけ、そのフレームに対して解決する。
    const Real scale = arg_scr_scale.as_real();
    std::lock_guard<std::mutex> guard(animated_bitmap_mutex);
    sweep_animated_bitmap_infos_locked();
    AnimatedBitmapInfo info;
    info.bitmap=arg_wxbmp; info.path=std::move(arg_path); info.instance_id=arg_instance_id;
    info.frame_id=arg_frame_id; info.mask=arg_mask; info.maskpos=std::move(arg_maskpos);
    info.scale=scale; info.owner=arg_owner.value_or(String{}); info.data=std::move(arg_data);
    animated_bitmap_infos[arg_wxbmp.get()] = std::move(info);
}

std::shared_ptr<::cw::lib::Bitmap> copy_animated_wxbmp_info(std::shared_ptr<::cw::lib::Bitmap> arg_src, std::shared_ptr<::cw::lib::Bitmap> arg_dst)
{
    if (!arg_src || !arg_dst) return arg_dst;
    std::lock_guard<std::mutex> guard(animated_bitmap_mutex);
    sweep_animated_bitmap_infos_locked();
    if (const auto it = find_animated_bitmap_info_locked(arg_src); it != animated_bitmap_infos.end()) {
        auto info=it->second; info.bitmap=arg_dst;
        animated_bitmap_infos[arg_dst.get()] = std::move(info);
    }
    return arg_dst;
}

std::shared_ptr<::cw::lib::Bitmap> conform_animated_wxbmp_frame(
    std::shared_ptr<::cw::lib::Bitmap> arg_frame,
    const std::shared_ptr<::cw::lib::Bitmap>& arg_reference)
{
    if (!arg_frame || !arg_reference) return arg_frame;
    // デコーダのフレームは同じアニメーションスロットの全利
    // 用側で共有する。画素寸法が偶然一致していても、ダイア
    // ログ固有のサイズや倍率を共有フレームへ書き戻さない。
    arg_frame = std::make_shared<::cw::lib::Bitmap>(*arg_frame);
    if (arg_reference->size.width > 0 && arg_reference->size.height > 0 &&
        (arg_frame->size.width != arg_reference->size.width ||
         arg_frame->size.height != arg_reference->size.height)) {
        arg_frame = ::cw::lib::rescale_bitmap_normal(arg_frame, arg_reference->size);
    }
    if (arg_frame) {
        arg_frame->scr_scale = arg_reference->scr_scale;
        auto metadata = ::cw::imageretouch::get_surface_metadata(arg_frame);
        metadata.logical_scale = ::cw::imageretouch::get_surface_metadata(arg_reference).logical_scale;
        ::cw::imageretouch::set_surface_metadata(arg_frame, metadata);
    }
    return arg_frame;
}

bool is_animated_wxbmp(std::shared_ptr<::cw::lib::Bitmap> arg_wxbmp)
{
    if (!arg_wxbmp) return false;
    std::lock_guard<std::mutex> guard(animated_bitmap_mutex);
    return find_animated_bitmap_info_locked(arg_wxbmp) != animated_bitmap_infos.end();
}

::cw::lib::Integer animated_wxbmp_frame_id(std::shared_ptr<::cw::lib::Bitmap> arg_wxbmp)
{
    if (!arg_wxbmp) return 0;
    AnimatedBitmapInfo info;
    {
        std::lock_guard<std::mutex> guard(animated_bitmap_mutex);
        const auto found = find_animated_bitmap_info_locked(arg_wxbmp);
        if (found == animated_bitmap_infos.end()) return 0;
        info = found->second;
    }
    auto frame_id = ::cw::animatedimage::get_frame_id(info.path, info.instance_id);
    if (!info.instance_id || ::cw::animatedimage::has_instance(info.path, *info.instance_id) ||
        info.owner.empty()) return frame_id;
    const auto instance = ::cw::animatedimage::acquire(
        info.path, info.owner, false, info.data);
    if (!instance) return 0;
    info.instance_id = instance;
    {
        std::lock_guard<std::mutex> guard(animated_bitmap_mutex);
        if (const auto found = find_animated_bitmap_info_locked(arg_wxbmp);
            found != animated_bitmap_infos.end())
            found->second.instance_id = instance;
    }
    return ::cw::animatedimage::get_frame_id(info.path, instance);
}

void set_animated_wxbmp_opaque(std::shared_ptr<::cw::lib::Bitmap> arg_wxbmp)
{
    if (!arg_wxbmp) return;
    std::lock_guard<std::mutex> guard(animated_bitmap_mutex);
    if (const auto found = find_animated_bitmap_info_locked(arg_wxbmp); found != animated_bitmap_infos.end())
        found->second.force_opaque = true;
}

std::shared_ptr<::cw::lib::Bitmap> resolve_animated_wxbmp(std::shared_ptr<::cw::lib::Bitmap> arg_wxbmp)
{
    AnimatedBitmapInfo info;
    {
        std::lock_guard<std::mutex> guard(animated_bitmap_mutex);
        const auto found = find_animated_bitmap_info_locked(arg_wxbmp);
        if (found == animated_bitmap_infos.end()) return arg_wxbmp;
        info = found->second;
    }
    auto current_frame_id = ::cw::animatedimage::get_frame_id(info.path, info.instance_id);
    // Pythonはデコーダのインスタンスがなくなると元のビットマップを返す。ネイ
    // ティブ側で保持されたビットマップは有効なアニメーション要求を所有しているため
    // 、保持したバイト列も含めて同じ所有者・パスを再取得する。古いインスタンスの解
    // 放でこの表示が永久に止まらないよう、このビットマップのIDだけを置き換える。
    if (info.instance_id && !::cw::animatedimage::has_instance(info.path, *info.instance_id) &&
        !info.owner.empty()) {
        if (const auto instance = ::cw::animatedimage::acquire(
                info.path, info.owner, false, info.data)) {
            info.instance_id = instance;
            current_frame_id = ::cw::animatedimage::get_frame_id(info.path, instance);
            std::lock_guard<std::mutex> guard(animated_bitmap_mutex);
            if (const auto source = find_animated_bitmap_info_locked(arg_wxbmp);
                source != animated_bitmap_infos.end())
                source->second.instance_id = instance;
        }
    }
    // Pyは呼び出しごとに新しいフレームのコピーを取得する。呼び出し元が以前の
    // 結果を変更したり、同じフレームIDで基準寸法を変えたりする場合がある。
    (void)current_frame_id;
    auto [frame, frame_id] = ::cw::animatedimage::get_wxbmp(info.path, info.instance_id);
    if (!frame || !*frame) return arg_wxbmp;
    auto bmp = *frame;
    if (info.mask && bmp && !bmp->rgba.empty()) {
        if (!bmp->has_pixel_alpha) {
            const auto [x, y] = convert_maskpos(info.maskpos, bmp->size.width, bmp->size.height);
            if (x >= 0 && y >= 0 && x < bmp->size.width && y < bmp->size.height) {
                const auto index = static_cast<std::size_t>((y * bmp->size.width + x) * 4);
                if (index + 2 < bmp->rgba.size()) {
                    const auto r = bmp->rgba[index], g = bmp->rgba[index + 1], b = bmp->rgba[index + 2];
                    ::cw::lib::simd_image::colorkey_alpha_zero(
                        bmp->rgba.data(), bmp->rgba.size() / 4U, r, g, b);
                }
            }
        }
    }
    bmp = conform_animated_wxbmp_frame(std::move(bmp), arg_wxbmp);
    if (bmp && info.force_opaque)
        ::cw::lib::simd_image::force_alpha(bmp->rgba.data(), bmp->rgba.size() / 4U, 255);
    if (bmp) {
        bmp->scr_scale = info.scale;
        auto metadata = ::cw::imageretouch::get_surface_metadata(bmp);
        metadata.logical_scale = info.scale;
        ::cw::imageretouch::set_surface_metadata(bmp, metadata);
        std::lock_guard<std::mutex> guard(animated_bitmap_mutex);
        if (const auto source = find_animated_bitmap_info_locked(arg_wxbmp);
            source != animated_bitmap_infos.end()) {
            source->second.frame_id = frame_id;
            source->second.resolved = bmp;
        }
        info.bitmap=bmp; info.resolved=bmp; info.frame_id=frame_id;
        animated_bitmap_infos[bmp.get()] = std::move(info);
        sweep_animated_bitmap_infos_locked();
    }
    return bmp;
}

void release_animated_wxbmp(std::shared_ptr<::cw::lib::Bitmap> arg_wxbmp)
{
    if (!arg_wxbmp) return;
    String owner;
    {
        std::lock_guard<std::mutex> guard(animated_bitmap_mutex);
        const auto found = animated_bitmap_infos.find(arg_wxbmp.get());
        if (found == animated_bitmap_infos.end()) return;
        owner = found->second.owner;
        // Pyは所有者を解放した後もビットマップのアニメーション属性を保持する。
    }
    if (!owner.empty() && owner.rfind("DialogBitmap:", 0) != 0)
        ::cw::animatedimage::release(owner, std::nullopt);
}

std::shared_ptr<::cw::lib::Bitmap> empty_bitmap(::cw::lib::Integer arg_w, ::cw::lib::Integer arg_h)
{
    auto result = std::make_shared<::cw::lib::Bitmap>();
    // wx.Bitmapのdepth=24では、Pyは正でない各寸法を1へ補正する。
    result->size = {std::max<Integer>(1, arg_w), std::max<Integer>(1, arg_h)};
    result->has_pixel_alpha = false;
    result->pixel_bits = 24;
    if (result->size.width > 0 && result->size.height > 0) result->rgba.assign(static_cast<std::size_t>(result->size.width * result->size.height * 4), 0);
    return result;
}

std::shared_ptr<::cw::lib::Bitmap> empty_bitmap_rgba(::cw::lib::Integer arg_w, ::cw::lib::Integer arg_h)
{
    // empty_bitmap()と異なり、Pyのdepth=32のAPIは寸法
    // を1へ補正しない。不正なwx寸法にはネイティブの空サーフェス表現を使う。
    auto result = std::make_shared<::cw::lib::Bitmap>();
    result->size = {std::max<Integer>(0, arg_w), std::max<Integer>(0, arg_h)};
    if (result->size.width > 0 && result->size.height > 0) result->rgba.assign(static_cast<std::size_t>(result->size.width * result->size.height * 4), 0);
    ::cw::lib::simd_image::force_alpha(result->rgba.data(), result->rgba.size() / 4U, 255);
    return result;
}

// PythonのusebufferはWindowsのビットマップ複製の問題を避
// けるためwxのRGBを経由し、アルファを失い補助属性も落とす。ネイティブのBitmap
// はwxハンドルなしでRGBA画素を所有するため、画素と倍率・深度・マス
// クのメタデータを直接複製し、usebufferでもアルファを失わない。
std::shared_ptr<::cw::lib::Bitmap> copy_wxbmp(std::shared_ptr<::cw::lib::Bitmap> arg_bmp, bool arg_usebuffer)
{
    (void)arg_usebuffer;
    return clone_bitmap(arg_bmp);
}

// Pythonは最初にwxのImageFromBufferのRGBを経由し、アルファを失
// う場合がある。ネイティブのImageとBitmapは所有権付きRGBA Surface
// 形式を共有するため、wxのRGBを経由せず直接コピーし、アルファと論理倍率を保つ。
std::shared_ptr<::cw::lib::Image> convert_to_image(std::shared_ptr<::cw::lib::Bitmap> arg_bmp)
{
    if (!arg_bmp) return {};
    auto result = std::make_shared<::cw::lib::Image>();
    static_cast<::cw::lib::Surface&>(*result) = static_cast<const ::cw::lib::Surface&>(*arg_bmp);
    return result;
}

std::vector<::cw::lib::Integer> wxbmp_to_buffer(std::shared_ptr<::cw::lib::Bitmap> arg_bmp)
{
    if (!arg_bmp) return {};
    std::vector<Integer> result;
    result.reserve(arg_bmp->rgba.size());
    for (const auto byte : arg_bmp->rgba) result.push_back(byte);
    return result;
}

void fill_image(std::shared_ptr<::cw::lib::Surface> arg_img, std::shared_ptr<::cw::lib::Surface> arg_surface, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_csize, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_ctrlpos, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_cpos)
{
    if (!arg_img || !arg_surface) return;
    const auto w = arg_surface->size.width, h = arg_surface->size.height;
    // 空のネイティブ画像の仮置き値には何もしない。Pythonはここで0による剰
    // 余演算の例外を出すが、この条件判定はタイル配置の刻みが0になることも防ぐ。
    if (w <= 0 || h <= 0) return;
    const auto [cw, ch] = arg_csize;
    const auto [ox, oy] = arg_ctrlpos;
    const auto [px, py] = arg_cpos;
    const Integer startx = tile_origin(ox, w);
    const Integer starty = tile_origin(oy, h);
    // csizeはクリップ矩形ではなく、タイルの開始位置を制御する
    // 。最後のタイルはその外へはみ出せる。既存の共通転送処理がimg
    // の範囲で切り抜き、共通のアルファ量子化規則を維持する。
    for (Integer x = startx; x < cw; x += w)
        for (Integer y = starty; y < ch; y += h)
            alpha_blit_rgba(*arg_surface, *arg_img, x + px, y + py);
}

void fill_bitmap(std::shared_ptr<::cw::lib::DeviceContext> arg_dc, std::shared_ptr<::cw::lib::Bitmap> arg_bmp, ::cw::lib::Value arg_csize, ::cw::lib::Value arg_ctrlpos, ::cw::lib::Value arg_cpos)
{
    auto canvas = get_device_context_target(arg_dc);
    if (!canvas || !arg_bmp || arg_bmp->size.width <= 0 || arg_bmp->size.height <= 0) return;
    const auto [width, height] = value_pair(arg_csize);
    const auto [control_x, control_y] = value_pair(arg_ctrlpos);
    const auto [offset_x, offset_y] = value_pair(arg_cpos);
    const Integer start_x = tile_origin(control_x, arg_bmp->size.width);
    const Integer start_y = tile_origin(control_y, arg_bmp->size.height);
    for (Integer x = start_x; x < width; x += arg_bmp->size.width)
        for (Integer y = start_y; y < height; y += arg_bmp->size.height)
            alpha_blit_rgba(*arg_bmp, *canvas, x + offset_x, y + offset_y, false, false);
}

std::tuple<::cw::lib::Integer, ::cw::lib::Integer> get_centerposition(::cw::lib::Size arg_size, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_targetpos, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_targetsize)
{
    const auto [x, y] = arg_targetpos;
    const auto [target_width, target_height] = arg_targetsize;
    const auto half = [](Integer value) {
        // Pythonの//は負の奇数寸法も含め、小さい方へ丸める。
        return value / 2 - (value < 0 && value % 2 != 0 ? 1 : 0);
    };
    const auto coordinate = [&half](Integer position, Integer target, Integer size) {
        // 各半分の値は[-2**62, 2**62-1]に収まるため、そ
        // の差はIntegerで表せる。先に差を取ることで、相殺後の最
        // 終座標が表現可能なのに途中でオーバーフローする場合も避ける。
        const auto delta = half(target) - half(size);
        // Pythonの無制限のintと異なり、ネイティブの座標は符号付き64ビット
        // 整数。表現できない最終結果は、符号付き整数をオーバーフローさせず拒否する。
        if ((delta > 0 && position > std::numeric_limits<Integer>::max() - delta) ||
            (delta < 0 && position < std::numeric_limits<Integer>::min() - delta))
            throw std::overflow_error("get_centerposition: coordinate exceeds Integer");
        return position + delta;
    };
    return {coordinate(x, target_width, arg_size.width),
            coordinate(y, target_height, arg_size.height)};
}

void draw_box(std::shared_ptr<::cw::lib::DeviceContext> arg_dc, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_size)
{
    auto canvas = get_device_context_target(arg_dc);
    if (!canvas) return;
    const auto [x, y] = arg_pos;
    const auto [width, height] = arg_size;
    const auto box = [&](Integer left, Integer top, ::cw::lib::Color color) {
        draw_dc_axis_line(*canvas, left, top, left + width, top, color);
        draw_dc_axis_line(*canvas, left, top, left, top + height, color);
        draw_dc_axis_line(*canvas, left + width, top, left + width, top + height, color);
        draw_dc_axis_line(*canvas, left, top + height, left + width, top + height, color);
    };
    // テーマに関する意図的な差異。共通のネイティブUIはwxのシス
    // テムテーマの強調色・影色ではなく、全ホストで固定色を使う。
    box(x + 1, y + 1, {255, 255, 255, 255});
    box(x, y, {128, 128, 128, 255});
}

::cw::lib::Rect window_work_area(const ::cw::lib::Window& window)
{
    // OSの画面矩形は符号付き32ビット座標を使う。共通モデルでは符
    // 号付き64ビットを保持し、この基本操作の境界だけで変換する。
    const auto coordinate=[](long double value){return static_cast<int>(std::clamp<long double>(value,std::numeric_limits<int>::min(),std::numeric_limits<int>::max()));};
#if defined(_WIN32)
    HMONITOR monitor{};
    if(window.native_handle)monitor=MonitorFromWindow(reinterpret_cast<HWND>(window.native_handle),MONITOR_DEFAULTTONULL);
    if(!monitor){
        RECT rect{coordinate(window.bounds.x),coordinate(window.bounds.y),coordinate(static_cast<long double>(window.bounds.x)+window.bounds.width),coordinate(static_cast<long double>(window.bounds.y)+window.bounds.height)};
        monitor=MonitorFromRect(&rect,MONITOR_DEFAULTTONULL);
    }
    if(!monitor)monitor=MonitorFromPoint({0,0},MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info{};info.cbSize=sizeof(info);
    if(monitor && GetMonitorInfoW(monitor,&info))return {info.rcWork.left,info.rcWork.top,info.rcWork.right-info.rcWork.left,info.rcWork.bottom-info.rcWork.top};
#elif CW_UTIL_HAS_SDL
    SDL_Rect rect{coordinate(window.bounds.x),coordinate(window.bounds.y),coordinate(window.bounds.width),coordinate(window.bounds.height)};
    auto display=SDL_GetDisplayForRect(&rect);if(!display)display=SDL_GetPrimaryDisplay();
    SDL_Rect area{};if(display && SDL_GetDisplayUsableBounds(display,&area))return {area.x,area.y,area.w,area.h};
#endif
    throw std::runtime_error("monitor work area is unavailable");
}
::cw::lib::Rect constrain_window_to_work_area(::cw::lib::Rect bounds,::cw::lib::Rect area)
{
    const auto maximum=[](Integer origin,Integer extent,Integer size){
        if(extent<0 || size<0)throw std::invalid_argument("negative native window or monitor extent");
        const auto delta=extent>size?extent-size:0;
        // Pythonの整数には上限がない。サイズは変えず、表現
        // できないネイティブ座標はオーバーフローさせず拒否する。
        if(origin>std::numeric_limits<Integer>::max()-delta)throw std::overflow_error("monitor coordinate exceeds Integer");
        return origin+delta;
    };
    bounds.x=std::clamp(bounds.x,area.x,maximum(area.x,area.width,bounds.width));
    bounds.y=std::clamp(bounds.y,area.y,maximum(area.y,area.height,bounds.height));
    return bounds;
}
void adjust_position(std::shared_ptr<::cw::lib::Window> arg_frame)
{
    if(!arg_frame)return; // 所有者なしを許容する移植用の拡張。wxはフレームを必要とする。
    auto bounds = arg_frame->bounds;
    // wxは移動前に実際の最上位ウィンドウの矩形を読む。ネイティブの
    // 所有者を持たない移植可能なモデルでは、設定済みの矩形を使う。
#if defined(_WIN32)
    const auto handle = reinterpret_cast<HWND>(arg_frame->native_handle);
    if (handle) {
        RECT rectangle{};
        if (!GetWindowRect(handle, &rectangle))
            throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                    "GetWindowRect");
        bounds = {rectangle.left, rectangle.top,
                  rectangle.right - rectangle.left, rectangle.bottom - rectangle.top};
    }
#elif CW_UTIL_HAS_SDL
    auto* handle = reinterpret_cast<SDL_Window*>(arg_frame->native_handle);
    int top{}, left{}, bottom{}, right{};
    if (handle) {
        int x{}, y{}, width{}, height{};
        if (!SDL_GetWindowPosition(handle, &x, &y) ||
            !SDL_GetWindowSize(handle, &width, &height))
            throw std::runtime_error(std::string("SDL window geometry failed: ") + SDL_GetError());
        // SDLはクライアント領域の原点を返すが、wxは装飾込みの外枠を使う。プラ
        // ットフォームに装飾情報の取得手段がない場合、SDLは境界幅に0を返す。
        (void)SDL_GetWindowBordersSize(handle, &top, &left, &bottom, &right);
        bounds = {static_cast<Integer>(x) - left, static_cast<Integer>(y) - top,
                  static_cast<Integer>(width) + left + right,
                  static_cast<Integer>(height) + top + bottom};
    }
#endif
    ::cw::lib::Window requested;
    requested.native_handle = arg_frame->native_handle;
    requested.bounds = bounds;
    const auto placed = constrain_window_to_work_area(bounds, window_work_area(requested));
    // 上のモニター選択と範囲内への補正は共通。異なるのはネイティ
    // ブの移動操作だけで、どちらもウィンドウサイズは変えない。
#if defined(_WIN32)
    if (handle && !SetWindowPos(handle, nullptr, static_cast<int>(placed.x),
                              static_cast<int>(placed.y), 0, 0,
                              SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE))
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                "SetWindowPos");
#elif CW_UTIL_HAS_SDL
    if (handle && !SDL_SetWindowPosition(handle, static_cast<int>(placed.x + left),
                                       static_cast<int>(placed.y + top)))
        throw std::runtime_error(std::string("SDL_SetWindowPosition failed: ") + SDL_GetError());
#endif
    arg_frame->bounds = placed;
}

void clear_background(std::shared_ptr<::cw::lib::DeviceContext> arg_dc, std::shared_ptr<::cw::lib::Control> arg_window)
{
    auto canvas = get_device_context_target(arg_dc);
    if (!canvas) return;
    const auto state = get_window_state(arg_window);
    const auto color = state ? state->background : ::cw::lib::Color{255, 255, 255, 255};
    for (std::size_t i = 0; i + 3 < canvas->rgba.size(); i += 4) {
        canvas->rgba[i] = color.r; canvas->rgba[i + 1] = color.g;
        canvas->rgba[i + 2] = color.b; canvas->rgba[i + 3] = color.a;
    }
}

::cw::lib::Color get_backgroundcolour(::cw::lib::Value arg_window)
{
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&arg_window.storage()); object && *object) {
        if (const auto window = std::dynamic_pointer_cast<::cw::lib::Window>(*object))
            if (const auto state = get_window_state(window)) return state->background;
    }
    return {255, 255, 255, 255};
}

::cw::lib::String abbr_longstr(std::shared_ptr<::cw::lib::DeviceContext> arg_dc, ::cw::lib::String arg_text, ::cw::lib::Integer arg_w)
{
    // Pythonは現在のフォントを持つwx.DCを受け取る。
    // 移植用の境界でも同じ文字寸法の取得処理を結び付け、描画
    // コンテキスト不在時に文字数による代替計算を追加しない。
    if (!arg_dc || !arg_dc->text_extent)
        throw std::invalid_argument("abbr_longstr requires a text extent context");
    const auto text_extent = arg_dc->text_extent;
    const auto width = [&](const String& text) { return text_extent(text).width; };
    const String marker = "...";
    if (arg_w <= 0 && !arg_text.empty())
        return width(arg_text) <= width(marker) ? arg_text : marker;
    if (width(arg_text) > arg_w) {
        // Pythonはスカラー値1つを取り除いて候補全体を再測定する
        // 。字形単体の幅を引くと、カーニングや結合文字の効果を失う。
        while (!arg_text.empty() && width(arg_text + marker) > arg_w) {
            auto end = arg_text.size() - 1;
            while (end > 0 &&
                   (static_cast<unsigned char>(arg_text[end]) & 0xc0) == 0x80) --end;
            arg_text.resize(end);
        }
        arg_text += marker;
    }
    return arg_text;
}

::cw::lib::String abbr_longstr_with_count(::cw::lib::String arg_text, ::cw::lib::Integer arg_w)
{
    // 移植版で定めた空文字列の幅に関する拡張を保つ。
    if (arg_text.empty()) return arg_text;
    Integer width = get_strlen(arg_text);
    if (arg_w <= 0 && !arg_text.empty()) return width <= 3 ? arg_text : "...";
    if (width <= arg_w) return arg_text;
    // Pythonは結合文字も含め、末尾からUnicodeスカラー
    // 値1つを切り落とす。従来の関数名は個数の接尾辞を意味しない。
    std::vector<std::pair<std::size_t, Integer>> characters;
    for (std::size_t offset = 0; offset < arg_text.size();) {
        const auto start = offset;
        const auto [code, bytes] = utf8_next(arg_text, offset);
        characters.emplace_back(start, codepoint_width(code));
    }
    while (!characters.empty() && width + 3 > arg_w) {
        width -= characters.back().second;
        arg_text.resize(characters.back().first);
        characters.pop_back();
    }
    return arg_text + "...";
}

void add_sideclickhandlers(std::shared_ptr<::cw::lib::Panel> arg_toppanel, std::shared_ptr<::cw::lib::Window> arg_leftbtn, std::shared_ptr<::cw::lib::Window> arg_rightbtn)
{
    g_sideclick.panel = std::move(arg_toppanel);
    g_sideclick.left = std::move(arg_leftbtn);
    g_sideclick.right = std::move(arg_rightbtn);
    g_sideclick.left_hover = false;
    g_sideclick.right_hover = false;
    add_sideclickhandlers__update_mousepos();
    add_sideclickhandlers_OnMotion({});
}

bool add_sideclickhandlers__is_cursorinleft()
{
    return g_sideclick.left_hover;
}

bool add_sideclickhandlers__is_cursorinright()
{
    return g_sideclick.right_hover;
}

void add_sideclickhandlers__update_mousepos()
{
#if defined(_WIN32)
    POINT point{};
    if (GetCursorPos(&point)) {
        if(g_sideclick.panel && g_sideclick.panel->native_handle)ScreenToClient(reinterpret_cast<HWND>(g_sideclick.panel->native_handle),&point);
        g_sideclick.mouse_x = point.x;
        g_sideclick.mouse_y = point.y;
    }
#endif
}

void add_sideclickhandlers_OnMotion(std::shared_ptr<::cw::lib::Window> arg_evt)
{
    if(arg_evt){g_sideclick.mouse_x=arg_evt->bounds.x;g_sideclick.mouse_y=arg_evt->bounds.y;}
    if(!g_sideclick.panel){g_sideclick.left_hover=g_sideclick.right_hover=false;return;}
    // PyはGetClientRectを使う。イベントのx座標と4分の1のしきい値は同じローカル原点を使う。
    const auto& bounds = g_sideclick.panel->bounds;
    const auto edges=::cw::lib::sideclick_quarter_edges(::cw::lib::Rect{0, 0, bounds.width, bounds.height});
    // イベント座標はクライアント座標。wxはここでyを制限しない。
    g_sideclick.left_hover=g_sideclick.left && g_sideclick.left->enabled && g_sideclick.mouse_x<edges.first;
    g_sideclick.right_hover=g_sideclick.right && g_sideclick.right->enabled && edges.second<g_sideclick.mouse_x;
}

void add_sideclickhandlers_OnLeftUp(std::shared_ptr<::cw::lib::Window> arg_evt)
{
    add_sideclickhandlers_OnMotion(std::move(arg_evt));
    // 生成されたWindowにはコールバックの格納先がない。ボタンIDを通して
    // クリック結果を保持し、イベント中継で実際の操作を実行できるようにする。
    if (g_sideclick.left_hover && g_sideclick.left) g_sideclick.left->id = 1;
    if (g_sideclick.right_hover && g_sideclick.right) g_sideclick.right->id = 1;
}

static void apply_accelerators(const std::shared_ptr<::cw::lib::Window>& widget,const std::vector<std::tuple<Integer,Integer,Integer>>& seq,const std::vector<AcceleratorWidgetFilter>& filters,std::unordered_set<const ::cw::lib::Window*>& visited)
{
    // ネイティブの弱参照グラフには重複リンクがあり得るため、各コントロールは一度だけ訪問する。
    if(!widget || !visited.insert(widget.get()).second)return;
    auto state=get_window_state(widget).value_or(WindowState{});state.accelerator_source=seq;state.accelerator_filters=filters;state.accelerators.clear();
    const bool ignore=std::any_of(filters.begin(),filters.end(),[&](const auto& f){return f && f(widget);});
    for(const auto& accelerator:seq){const auto [flags,key,id]=accelerator;(void)id;
        // 移植用のネイティブキー37/39とwxキー314/316は、同じ左右矢印を表す。
        if(ignore && flags==0 && (key==37 || key==39 || key==314 || key==316))continue;
        state.accelerators.push_back(accelerator);
    }
    const auto children=state.children;set_window_state(widget,std::move(state));
    for(const auto& child:children)apply_accelerators(child.lock(),seq,filters,visited);
}
std::vector<AcceleratorWidgetFilter> default_accelerator_filters()
{
    // 移植用のRichTextは共通のwx.TextCtrl相当の型。動的キャストにより
    // 、3つの既定型すべてでPythonのisinstanceと同じ派生型の扱いを保つ。
    return {[](const std::shared_ptr<::cw::lib::Window>& widget){return
        std::dynamic_pointer_cast<CWPyRichTextCtrl>(widget) ||
        std::dynamic_pointer_cast<::cw::lib::Dialog>(widget) ||
        std::dynamic_pointer_cast<::cw::lib::Panel>(widget);}};
}
void set_acceleratortable(std::shared_ptr<::cw::lib::Window> panel,std::vector<std::tuple<Integer,Integer,Integer>> seq,std::vector<AcceleratorWidgetFilter> filters)
{
    std::unordered_set<const ::cw::lib::Window*> visited;apply_accelerators(panel,seq,filters,visited);
}
void set_acceleratortable_with_filters(std::shared_ptr<::cw::lib::Window> panel,std::vector<std::tuple<Integer,Integer,Integer>> seq,std::vector<AcceleratorWidgetFilter> filters)
{
    set_acceleratortable(std::move(panel),std::move(seq),std::move(filters));
}
void set_acceleratortable_legacy(std::shared_ptr<::cw::lib::Window> arg_panel,std::vector<std::tuple<Integer,Integer,Integer>> arg_seq,std::vector<std::shared_ptr<::cw::lib::Control>> arg_ignoreleftrightkeys)
{
    std::vector<AcceleratorWidgetFilter> filters;
    // 旧形式のインスタンス値は動的なクラスを表す。任意のネイティブ
    // 派生型の検査では、UIの親子関係を変えず判定APIを使う。
    for(const auto& exemplar:arg_ignoreleftrightkeys)if(exemplar){const std::type_info* type=&typeid(*exemplar);filters.push_back([type](const auto& widget){return typeid(*widget)==*type;});}
    set_acceleratortable_with_filters(std::move(arg_panel),std::move(arg_seq),std::move(filters));
}
void set_acceleratortable_recurse(std::shared_ptr<::cw::lib::Control> arg_widget)
{
    if(!arg_widget)return;const auto state=get_window_state(arg_widget);if(!state)return;
    set_acceleratortable_with_filters(arg_widget,state->accelerator_source.empty()?state->accelerators:state->accelerator_source,state->accelerator_filters);
}

void adjust_dropdownwidth(std::shared_ptr<::cw::lib::Window> arg_choice)
{
    // PyのCB_SETDROPPEDWIDTHは開いた一覧だけを変え、閉じたコンボ
    // ボックスの範囲は変えない。移植用のコントロールにはwxのドロップダウン用ネイテ
    // ィブハンドルがないため、Text::dialog_presentationが
    // 実際のフォントで開いたポップアップを測定し、全ホストで共通の表示領域内へ制限
    // する。この一つのサイズ決定経路を保ち、ここで閉じたコントロールを拡大しない。
    (void)arg_choice;
}

bool has_modalchild(std::shared_ptr<::cw::lib::Window> arg_frame)
{
    if (!arg_frame) return false;
    // ネイティブの呼び出し元は、wxでは作れない循環したデバッガのリンクを渡す場合がある。
    static thread_local std::vector<const ::cw::lib::Window*> querying;
    if(std::find(querying.begin(),querying.end(),arg_frame.get())!=querying.end()) return false;
    querying.push_back(arg_frame.get());
    struct QueryGuard { std::vector<const ::cw::lib::Window*>& entries; ~QueryGuard(){entries.pop_back();} } guard{querying};
    // wx.TopLevelParentと同じ扱い。パネルは最も近いDialog/Frame
    // を使い、問い合わせ対象が最上位ダイアログ自身なら、その所有者の他の子要素は調べない。
    auto top = arg_frame;
    std::set<const ::cw::lib::Window*> visited;
    while (!std::dynamic_pointer_cast<::cw::lib::Dialog>(top) &&
           !std::dynamic_pointer_cast<::cw::lib::Frame>(top)) {
        // wxの親グラフと異なり、ネイティブの値のグラフは不正な構造に
        // なり得る。この表現の境界で循環を拒否し、無限ループを避ける。
        if (!visited.insert(top.get()).second) return false;
        const auto current = get_window_state(top);
        auto parent = current ? current->parent.lock() : nullptr;
        if (!parent) parent = top->parent_window.lock();
        if (!parent) break;
        top = std::move(parent);
    }
    const auto state = get_window_state(top);
    if (state && std::any_of(state->children.begin(), state->children.end(), [](const auto& child_ref) {
        const auto child = child_ref.lock();
        if (!child || !child->visible || !std::dynamic_pointer_cast<::cw::lib::Dialog>(child)) return false;
        const auto child_state = get_window_state(child);
        return child_state && child_state->modal;
    })) return true;
    // PythonではメインFrame自身への問い合わせだけがその
    // デバッガへ到達する。共通のFrameの識別情報がこの関係を示
    // す。パネルは最上位の親が直接持つモーダルの子だけを解決する。
    if (arg_frame==top && state && state->main_frame) {
        auto debugger=state->debugger.lock();
        if (debugger && debugger!=arg_frame) return has_modalchild(std::move(debugger));
    }
    return false;
}

void CWPyRichTextCtrl_SearchEngine::initialize(std::shared_ptr<::cw::util::CWPyRichTextCtrl> arg_parent, ::cw::lib::String arg_url, ::cw::lib::String arg_name, ::cw::lib::Integer arg_menuid)
{
    m_parent_typed = std::move(arg_parent);
    m_url_typed = std::move(arg_url);
    m_name_typed = std::move(arg_name);
    m_menuid_typed = arg_menuid;
}

String CWPyRichTextCtrl_SearchEngine::format_url(const String& selected) const
{
    String result;
    bool consumed = false;
    for (std::size_t i = 0; i < m_url_typed.size(); ++i) {
        if (m_url_typed[i] != '%') { result.push_back(m_url_typed[i]); continue; }
        if (++i == m_url_typed.size()) throw std::invalid_argument("incomplete search URL format");
        if (m_url_typed[i] == '%') { result.push_back('%'); continue; }
        bool left = false;
        while (i < m_url_typed.size() && String("#0- +").find(m_url_typed[i]) != String::npos) {
            left = left || m_url_typed[i] == '-';
            ++i;
        }
        const auto decimal = [&]() {
            std::size_t number = 0;
            while (i < m_url_typed.size() && m_url_typed[i] >= '0' && m_url_typed[i] <= '9') {
                const auto digit = static_cast<std::size_t>(m_url_typed[i++] - '0');
                const auto maximum = static_cast<std::size_t>(std::numeric_limits<Integer>::max());
                if (number > (maximum - digit) / 10) throw std::overflow_error("search URL field is too large");
                number = number * 10 + digit;
            }
            return number;
        };
        const auto width = decimal();
        std::optional<std::size_t> precision;
        if (i < m_url_typed.size() && m_url_typed[i] == '.') { ++i; precision = decimal(); }
        // Pythonは文字列フィールドを含め、Cの長さ修飾子1つを無視する。
        if (i < m_url_typed.size() && String("hlL").find(m_url_typed[i]) != String::npos) ++i;
        if (i == m_url_typed.size()) throw std::invalid_argument("incomplete search URL format");
        const auto conversion = m_url_typed[i];
        if (conversion != 's' && conversion != 'c' && conversion != 'r' && conversion != 'a')
            throw std::invalid_argument("unsupported search URL format");
        if (consumed) throw std::invalid_argument("not enough search URL arguments");
        String representation;
        const String* field = &selected;
        if (conversion == 'r' || conversion == 'a') {
            const char quote = selected.find('\'') != String::npos && selected.find('"') == String::npos ? '"' : '\'';
            representation.push_back(quote);
            std::size_t offset = 0;
            while (offset < selected.size()) {
                const auto begin = offset;
                const auto [code, size] = utf8_next(selected, offset);
                if (code == quote || code == '\\') {
                    representation.push_back('\\');
                    representation.push_back(static_cast<char>(code));
                } else if (code == '\t' || code == '\n' || code == '\r') {
                    representation.push_back('\\');
                    representation.push_back(code == '\t' ? 't' : code == '\n' ? 'n' : 'r');
                } else if ((conversion == 'a' && code >= 128) ||
                           !::cw::lib::python_unicode_printable(static_cast<std::uint32_t>(code))) {
                    const unsigned digits = code <= 0xff ? 2 : code <= 0xffff ? 4 : 8;
                    representation += digits == 2 ? "\\x" : digits == 4 ? "\\u" : "\\U";
                    for (unsigned shift = digits * 4; shift != 0;) {
                        shift -= 4;
                        representation.push_back("0123456789abcdef"[(static_cast<std::uint32_t>(code) >> shift) & 15]);
                    }
                } else representation.append(selected, begin, static_cast<std::size_t>(size));
            }
            representation.push_back(quote);
            field = &representation;
        }
        std::size_t bytes = 0, characters = 0;
        while (bytes < field->size() && (conversion == 'c' || !precision || characters < *precision)) {
            utf8_next(*field, bytes);
            ++characters;
        }
        if (conversion == 'c' && characters != 1) throw std::invalid_argument("search URL %c requires one character");
        const auto padding = width > characters ? width - characters : 0;
        if (!left) result.append(padding, ' ');
        result.append(*field, 0, bytes);
        if (left) result.append(padding, ' ');
        consumed = true;
    }
    // Pythonは空でも選択文字列1つを引数として適用する。置換箇
    // 所がなければエラーであり、選択文字列を末尾へ足す指定ではない。
    if (!consumed) throw std::invalid_argument("unused search URL argument");
    return result;
}

void CWPyRichTextCtrl_SearchEngine::OnSearch(std::shared_ptr<::cw::lib::Window> arg_event)
{
    // Pythonはメニューイベントの内容を無視し、GetStringSelection()
    // を使う。操作全体の間は親を生存させ、切り離された検索項目が
    // 無関係なイベントのタイトルでURLを開かないようにする。
    (void)arg_event;
    const auto parent = m_parent_typed.lock();
    if (!parent) return;
    const String selected = parent->selected_text();
    try {
        parent->go_url(format_url(selected));
    } catch (const std::exception&) {
        print_ex(std::nullopt);
    }
}

std::vector<std::tuple<::cw::lib::String, bool>> CWPyRichTextCtrl_set_text_get_urls(::cw::lib::String arg_text)
{
    std::vector<std::tuple<String, bool>> result;
    // PyはこのURL式をUnicodeのstrへ適用する。状態層の区間
    // と共通のText表示処理で、同じUnicodeの照合処理を使う。
    static const srell::u8cregex url_pattern(
        R"(http(s)?://([\p{L}\p{N}_-]+\.)+[\p{L}\p{N}_]+(/[\p{L}\p{N}_./?%&=~#!-]*)?)");
    std::size_t cursor = 0;
    for (srell::u8csregex_iterator it(arg_text.begin(), arg_text.end(), url_pattern), end; it != end; ++it) {
        const auto match_pos = static_cast<std::size_t>(it->position());
        if (match_pos > cursor) result.emplace_back(arg_text.substr(cursor, match_pos - cursor), false);
        result.emplace_back(it->str(), true);
        cursor = match_pos + static_cast<std::size_t>(it->length());
    }
    if (cursor < arg_text.size()) result.emplace_back(arg_text.substr(cursor), false);
    return result;
}

static void open_url_primitive(const ::cw::lib::String& arg_url)
{
#if defined(_WIN32)
    // Rich Editの自動URL検出にはfile:、UNCパス、多くのア
    // プリ固有スキームが含まれる。ShellExecuteはそれらを非公開ラ
    // ンチャーのJob Object外の既存プロセスへ渡す場合があり、ランチ
    // ャーが状態を保存して削除した後も、一時セッションのパスが保持され得る。
    // 通常の平文起動では従来のスキーム対応を保ち、管理された保管庫セッション
    // ではローカルファイルを指定できないネットワークURLだけを公開する。
    if (managed_userdata_session() &&
        !starts_with_ascii_case_insensitive(arg_url, "http://") &&
        !starts_with_ascii_case_insensitive(arg_url, "https://")) {
        throw std::runtime_error(
            "opening non-HTTP URLs through the Windows shell is disabled in a "
            "managed UserData session");
    }
    const auto wide = wide_from_utf8(arg_url);
    const auto result = reinterpret_cast<std::intptr_t>(ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (result <= 32) throw std::system_error(static_cast<int>(result), std::system_category(), "ShellExecuteW");
#elif CW_UTIL_HAS_SDL_MIXER
    if (!SDL_OpenURL(arg_url.c_str())) throw std::runtime_error(SDL_GetError());
#else
    throw std::runtime_error("URL opener is not available");
#endif
}

static void open_url_with_services_on_thread(
    const std::shared_ptr<::cw::frame::Frame>& frame,
    std::shared_ptr<::cw::lib::Window> parent,
    const ::cw::lib::String& url,
    const OpenUrlServices& services) noexcept
{
    try {
        services.launch(parent, url);
        return;
    } catch (...) {
        // wx.LaunchDefaultBrowserと同様に、外部起動の失敗
        // をイベントの外へ投げず、アプリのErrorMessageで報告する。
    }

    std::shared_ptr<::cw::dialog::message::ErrorMessage> dialog;
    try {
        dialog = std::make_shared<::cw::dialog::message::ErrorMessage>();
        dialog->initialize(parent, "「" + url +
            "」が開けませんでした。インターネットブラウザが正常に関連付けされているか確認して下さい。");
    } catch (...) {
        // メッセージ用バックエンドが不正または利用不能でも、ブ
        // ラウザ起動の例外をUIコールバックの外へ出さない。
        return;
    }
    try { if (frame) frame->move_dlg(dialog, {0, 0}); } catch (...) {}
    try { services.present_error(frame, parent, dialog); } catch (...) {}
    // 共通モーダルがネイティブの表示と完了を管理する。wxのモーダルループ後
    // のDestroy()に相当し、ホスト呼び出しの失敗や中断時にも閉じる。
    dialog->visible = false;
}

void open_url(std::shared_ptr<::cw::lib::Window> arg_parentdlg, ::cw::lib::String arg_url)
{
    OpenUrlServices services;
    services.launch = [](const std::shared_ptr<::cw::lib::Window>&,
                         const ::cw::lib::String& url) {
        open_url_primitive(url);
    };
    services.present_error = [](const std::shared_ptr<::cw::frame::Frame>&,
                                 const std::shared_ptr<::cw::lib::Window>&,
                                 const std::shared_ptr<::cw::lib::Window>& dialog) {
        (void)::cw::dialog::show_common_modal_presentation(dialog);
    };
    open_url_with_services(std::move(arg_parentdlg), std::move(arg_url), std::move(services));
}

void open_url_with_services(std::shared_ptr<::cw::lib::Window> arg_parentdlg,
                            ::cw::lib::String arg_url,
                            OpenUrlServices arg_services)
{
    if (!arg_services.launch || !arg_services.present_error)
        throw std::invalid_argument("URL opening requires launch and error-presentation services");

    std::shared_ptr<::cw::frame::Frame> frame;
    if (::cw::g_cwpy) {
        if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_frame.storage()))
            frame = std::dynamic_pointer_cast<::cw::frame::Frame>(*object);
    }

    // PythonはCWPyからの要求をwxのUIループへ積む。ネイティ
    // ブ版ではUI以外の全ワーカーからのコールバックに、同じ登録済みUI
    // キューを使う。元の親と両端の処理先をまとめて捕捉し、UIイベントを
    // 処理する時点のウィンドウやバックエンドに置き換わらないようにする。
    if (frame && frame->m_ui_thread != std::thread::id{} &&
        frame->m_ui_thread != std::this_thread::get_id()) {
        frame->exec_func([frame, parent = std::move(arg_parentdlg),
                          url = std::move(arg_url), services = std::move(arg_services)](
                             const ::cw::lib::Arguments&,
                             const ::cw::lib::KeywordArguments&) mutable {
            open_url_with_services_on_thread(frame, std::move(parent), std::move(url),
                                              std::move(services));
            return ::cw::lib::Value{};
        });
        return;
    }
    open_url_with_services_on_thread(frame, std::move(arg_parentdlg), std::move(arg_url),
                                     std::move(arg_services));
}

namespace {
bool open_compiled_help(std::uintptr_t native_handle, const std::filesystem::path& path)
{
#if defined(_WIN32)
    using HtmlHelpFunction = HWND(WINAPI*)(HWND, LPCWSTR, UINT, DWORD_PTR);
    static const HMODULE module = LoadLibraryExW(L"hhctrl.ocx", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    const auto html_help = module ? reinterpret_cast<HtmlHelpFunction>(GetProcAddress(module, "HtmlHelpW")) : nullptr;
    return html_help && html_help(reinterpret_cast<HWND>(native_handle), path.c_str(), 1, 0);
#else
    (void)native_handle;
    (void)path;
    return false;
#endif
}
}

void show_help()
{
    if (!::cw::g_cwpy) return;
    const auto* frame_object = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_frame.storage());
    const auto frame = frame_object ? std::dynamic_pointer_cast<::cw::frame::Frame>(*frame_object) : nullptr;
    if (!frame) return;
    ::cw::g_cwpy->play_sound("click", false, 100, 1, 0, 0, false);
    const auto chm = std::filesystem::u8path(::cw::g_exepath).parent_path() / "CardWirthPy.chm";
    if (std::filesystem::is_regular_file(chm) && open_compiled_help(frame->m_native_window_handle, chm)) return;
    const auto sites = ::cw::data::xml2etree("Data/Sites.xml", "", std::nullopt, std::nullopt, false);
    const auto url = sites->getattr("OnlineHelp", "url", "");
    if (!url.empty()) open_url({}, url);
}

::cw::lib::Integer get_wheelrotation(::cw::lib::Integer rotation,bool horizontal)
{
    // wxのホイール回転量は符号付き32ビット整数。移植用の値はそれより広い範囲を持つ。
    if(horizontal && rotation==std::numeric_limits<::cw::lib::Integer>::min())throw std::overflow_error("wheel rotation cannot be negated");
    return horizontal?-rotation:rotation;
}

::cw::lib::Integer get_wheelrotation(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if(!arg_event)return 0;
    return get_wheelrotation(arg_event->id,arg_event->wheel_horizontal);
}

void _co_initialize()
{
#if defined(_WIN32)
    static thread_local bool initialized = false;
    if (!initialized) {
        const auto result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(result) && result != RPC_E_CHANGED_MODE) check_hresult(result, "CoInitializeEx");
        initialized = SUCCEEDED(result) || result == S_FALSE || result == RPC_E_CHANGED_MODE;
    }
#endif
}

::cw::lib::String get_linktarget(::cw::lib::String arg_fpath)
{
    // プラットフォーム固有なのはネイティブの読み込み処理だけ。連鎖したリンク
    // 先と失敗時の代替処理は、全プラットフォームで同じ論理パス規則に従う。
    std::set<String> visited;
    for (;;) {
        if (::cardwirth::platform::is_mounted_user_path(std::filesystem::u8path(arg_fpath)))
            // マウントされた暗号化データはStorageのキーであり、ネイティブのリンクではない。
            return arg_fpath;
#if defined(_WIN32)
        std::error_code ec;
        if (arg_fpath.size() < 4 || lower_ascii(arg_fpath.substr(arg_fpath.size() - 4)) != ".lnk" ||
            !std::filesystem::is_regular_file(std::filesystem::u8path(arg_fpath), ec)) return arg_fpath;
        // Pythonはリンクを再帰的にたどる。ここでは循環を制限し、スタックのあふれを防ぐ。
        if (!visited.insert(get_keypath(arg_fpath)).second) return arg_fpath;
        try {
            _co_initialize();
            IShellLinkW* link = nullptr;
            check_hresult(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)), "CoCreateInstance(CLSID_ShellLink)");
            struct LinkGuard { IShellLinkW* value; ~LinkGuard() { if (value) value->Release(); } } link_guard{link};
            IPersistFile* file = nullptr;
            check_hresult(link->QueryInterface(IID_PPV_ARGS(&file)), "IShellLink::QueryInterface");
            struct FileGuard { IPersistFile* value; ~FileGuard() { if (value) value->Release(); } } file_guard{file};
            check_hresult(file->Load(wide_from_utf8(arg_fpath).c_str(), STGM_READ), "IPersistFile::Load");
            wchar_t target[MAX_PATH]{};
            WIN32_FIND_DATAW data{};
            check_hresult(link->GetPath(target, MAX_PATH, &data, SLGP_UNCPRIORITY), "IShellLink::GetPath");
            arg_fpath = join_paths({::cw::lib::Value(utf8_from_wide(target))});
        } catch (const std::exception&) {
            // PyのLoad/GetPathの失敗経路と同様
            // に現在の名前を保持する。ネイティブ側にはPython
            // のトレースバックがないため、標準エラーへの
            // 出力を省き、現在の論理名を残す。COMの作
            // 成失敗も含め、選択可能な元パスを保持する。
            return arg_fpath;
        }
#else
        return arg_fpath;
#endif
    }
}

void set_linktarget(::cw::lib::String arg_fpath, ::cw::lib::String arg_targetpath)
{
    arg_fpath = normalize_path(std::move(arg_fpath));
    if (::cardwirth::platform::is_mounted_user_path(std::filesystem::u8path(arg_fpath)))
        return;
#if defined(_WIN32)
    if (arg_fpath.size() < 4 || lower_ascii(arg_fpath.substr(arg_fpath.size() - 4)) != ".lnk" ||
        !std::filesystem::is_regular_file(std::filesystem::u8path(arg_fpath))) return;
    _co_initialize();
    IShellLinkW* link = nullptr;
    check_hresult(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)), "CoCreateInstance(CLSID_ShellLink)");
    struct LinkGuard { IShellLinkW* value; ~LinkGuard() { if (value) value->Release(); } } link_guard{link};
    try {
        struct FileGuard { IPersistFile* value; ~FileGuard() { if (value) value->Release(); } };
        // Pyの読み取りモード、呼び出し元が渡した対象、Save(FALSE)の方針を保
        // つ。PyはLoad後に一時的なQueryInterfaceのラッパーを解放し、
        // Save用には新しいIPersistFileラッパーを取得する。SetPath
        // をまたいでLoad用のインターフェースを保持せず、同じ寿命の区切りを守る。
        {
            IPersistFile* file = nullptr;
            check_hresult(link->QueryInterface(IID_PPV_ARGS(&file)), "IShellLink::QueryInterface");
            FileGuard file_guard{file};
            check_hresult(file->Load(wide_from_utf8(arg_fpath).c_str(), STGM_READ), "IPersistFile::Load");
        }
        check_hresult(link->SetPath(wide_from_utf8(arg_targetpath).c_str()), "IShellLink::SetPath");
        {
            IPersistFile* file = nullptr;
            check_hresult(link->QueryInterface(IID_PPV_ARGS(&file)), "IShellLink::QueryInterface");
            FileGuard file_guard{file};
            // IPersistFile::Saveは絶対ファイル名を要求する。Pyは相対のショ
            // ートカット名を拒否するが、ネイティブの実行状況によってはこの仕様に反して受け付
            // ける場合がある。予期せずリンクを書き換えないよう、COMの境界で不正な入力を拒
            // 否する。絶対ファイル名では実際のCOMのSave結果を使う。 https://learn.microsoft.com/en-us/windows/win32/api/objidl/nf-objidl-ipersistfile-save
            //
            //
            if (!std::filesystem::u8path(arg_fpath).is_absolute())
                throw std::invalid_argument("IPersistFile::Save requires an absolute shortcut filename");
            check_hresult(file->Save(wide_from_utf8(arg_fpath).c_str(), FALSE), "IPersistFile::Save");
        }
    } catch (...) {
        print_ex(std::nullopt);
    }
#else
    // PythonはWindows専用のショートカット変更処理。他のプラットフォー
    // ムからこの補助処理を呼んでも、任意のPOSIXファイルを削除・置換しない。
    (void)arg_fpath;
    (void)arg_targetpath;
#endif
}

bool supports_shell_links() noexcept
{
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

void create_link(::cw::lib::String arg_shortcutpath, ::cw::lib::String arg_targetpath)
{
    arg_shortcutpath = normalize_path(std::move(arg_shortcutpath));
    arg_targetpath = normalize_path(std::move(arg_targetpath));
    if (::cardwirth::platform::is_mounted_user_path(std::filesystem::u8path(arg_shortcutpath)))
        return;
#if defined(_WIN32)
    const auto link = std::filesystem::absolute(std::filesystem::u8path(arg_shortcutpath));
    const auto target = std::filesystem::absolute(std::filesystem::u8path(arg_targetpath));
    std::error_code ec;
    if (!link.parent_path().empty()) std::filesystem::create_directories(link.parent_path(), ec);
    _co_initialize();
    IShellLinkW* shell_link = nullptr;
    check_hresult(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shell_link)), "CoCreateInstance(CLSID_ShellLink)");
    struct LinkGuard { IShellLinkW* value; ~LinkGuard() { if (value) value->Release(); } } link_guard{shell_link};
    check_hresult(shell_link->SetPath(target.wstring().c_str()), "IShellLink::SetPath");
    IPersistFile* persist = nullptr;
    check_hresult(shell_link->QueryInterface(IID_PPV_ARGS(&persist)), "IShellLink::QueryInterface");
    struct FileGuard { IPersistFile* value; ~FileGuard() { if (value) value->Release(); } } file_guard{persist};
    // Pyはリンク作成時もremember=falseを使う。作成時と
    // 後の再読み込みで、同じ保存済みショートカットの扱いから始める。
    check_hresult(persist->Save(link.wstring().c_str(), FALSE), "IPersistFile::Save");
#else
    (void)arg_shortcutpath;
    (void)arg_targetpath;
#endif
}

// Pyはリンクでない入力の表記を保つ。ネイティブの論理パスは./の除
// 去を含む字句的正規化と共通のUTF-8区切りを使い、リソースキーの
// 表記を統一する。OS固有の構文はファイルシステム操作の境界に限定し
// 、戻り値の文字列は意図的に正規化する。PyはValueError
// だけを捕捉するが、ネイティブのメタデータ取得やread_symlink
// のerror_codeによる失敗でも正規化した元パスを保持する。
// 列挙から読み取りまでの間にリンクが消える場合にも、架空の対象を作
// らず選択可能な元パスを残す。Windowsのread_symlink
// は、Pyのos.readlinkが返すNT名前空間の接頭辞なし
// で、表示用のリンク先を返すことがある。対象のMSVC実装はPrintName
// を優先し、空ならSubstituteNameを使う。名前空
// 間の接頭辞を追加せず、この結果を正規化した論理リソースパス
// に使う。このAPI自体が全接頭辞を除去するわけではない。
::cw::lib::String get_symlinktarget(::cw::lib::String arg_path)
{
    arg_path = normalize_path(std::move(arg_path));
    if (::cardwirth::platform::is_mounted_user_path(std::filesystem::u8path(arg_path)))
        return arg_path;
    std::error_code ec;
    const auto original = arg_path;
    auto path = std::filesystem::absolute(std::filesystem::u8path(arg_path), ec).lexically_normal();
    if (ec || !std::filesystem::is_symlink(path, ec)) return original;
    auto target = std::filesystem::read_symlink(path, ec);
    if (ec) return original;
    if (target.is_absolute()) return ::cw::lib::encoding::path_to_utf8(target.lexically_normal());
    return ::cw::lib::encoding::path_to_utf8((path.parent_path() / target).lexically_normal());
}

::cw::lib::String get_keypath(::cw::lib::String arg_path)
{
    if (arg_path.empty()) arg_path = ".";
    arg_path = normalize_path(std::move(arg_path));
    std::error_code ec;
    auto path = std::filesystem::absolute(std::filesystem::u8path(arg_path), ec);
    if (ec) path = std::filesystem::u8path(arg_path);
    path = path.lexically_normal();
    // PythonのUnicodeのabspath/normpathキーと同様に、システムのコ
    // ードページへ狭めない。ANSIで表せないシナリオパスも集合や対応表で使えるようにする。
    auto result = ::cw::lib::encoding::path_to_utf8(path);
    const auto root = ::cw::lib::encoding::path_to_utf8(path.root_path());
    while (result.size() > root.size() && result.back() == '/') result.pop_back();
#if defined(_WIN32)
    result = ::cw::lib::encoding::lower(result);
#endif
    return result;
}

static bool face_path_truthy(const ::cw::lib::Value& value)
{
    if (const auto* object=std::get_if<::cw::lib::ObjectPtr>(&value.storage());object && *object) {
        if (const auto mapping=std::dynamic_pointer_cast<cw::data::VariantMapping>(*object)) return !mapping->m_values.empty();
        if (const auto sequence=std::dynamic_pointer_cast<cw::data::VariantSequence>(*object)) return !sequence->m_values.empty();
        return true;
    }
    if (const auto* bytes=std::get_if<::cw::lib::Bytes>(&value.storage())) return !bytes->empty();
    return value_truthy(value);
}

static ::cw::lib::Value face_path_attribute(const ::cw::lib::Value& value,const ::cw::lib::String& name)
{
    const auto* object=std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    if (!object || !*object) return {};
    if (const auto image=std::dynamic_pointer_cast<cw::image::ImageInfo>(*object))
        return name=="path"?::cw::lib::Value(image->m_path): ::cw::lib::Value{};
    // ネイティブのObjectにはPythonのリフレクションがない。StructValが
    // 登録済みのオブジェクト属性を保持し、辞書はVariantMappingで区別する。
    if (const auto structure=std::dynamic_pointer_cast<cw::data::StructVal>(*object))
        for (std::size_t i=0;i<structure->m_member_names.size() && i<structure->m_member_values.size();++i)
            if (structure->m_member_names[i]==name) return structure->m_member_values[i];
    return {};
}

::cw::lib::Value py_get_path(::cw::lib::Value arg_ldata)
{
    const auto* object=std::get_if<::cw::lib::ObjectPtr>(&arg_ldata.storage());
    if (object && *object) {
        if (const auto mapping=std::dynamic_pointer_cast<cw::data::VariantMapping>(*object)) {
            const auto get=[](const auto& m,const ::cw::lib::String& name)->::cw::lib::Value {
                const auto it=m.find(name);return it==m.end()?::cw::lib::Value{}:it->second;
            };
            const auto option=get(mapping->m_values,"option");
            if (face_path_truthy(option)) {
                const auto* optobject=std::get_if<::cw::lib::ObjectPtr>(&option.storage());
                const auto opt=optobject && *optobject?std::dynamic_pointer_cast<cw::data::VariantMapping>(*optobject):nullptr;
                if (!opt) throw std::invalid_argument("dict option has no get method");
                const auto path=get(opt->m_values,"path");
                if (face_path_truthy(path)) return path;
            }
            const auto path=get(mapping->m_values,"path");
            return face_path_truthy(path)?path: ::cw::lib::Value(::cw::lib::String{});
        }
    }
    const auto option=face_path_attribute(arg_ldata,"option");
    if (face_path_truthy(option)) {
        const auto path=face_path_attribute(option,"path");
        if (face_path_truthy(path)) return path;
    }
    const auto path=face_path_attribute(arg_ldata,"path");
    return face_path_truthy(path)?path: ::cw::lib::Value(::cw::lib::String{});
}

static ::cw::lib::String face_path_string(const ::cw::lib::Value& value)
{
    if (const auto* path=std::get_if<::cw::lib::String>(&value.storage())) return *path;
    // Pythonのファイル名操作は、パス検索後に文字列でない値を拒否する。
    throw std::invalid_argument("face filename requires a string path");
}

static ::cw::lib::String face_overlay_path(const ::cw::lib::Value& layer)
{
    const auto path=py_get_path(layer);
    if (face_path_truthy(path)) return face_path_string(path);
    // Pythonの重ね合わせ処理は、任意のパス検索が空だった値で
    // も必須のl.pathへ切り替える。属性がなければ失敗させる。
    const auto* object=std::get_if<::cw::lib::ObjectPtr>(&layer.storage());
    if (object && *object) {
        if (const auto image=std::dynamic_pointer_cast<cw::image::ImageInfo>(*object)) return image->m_path;
        if (const auto structure=std::dynamic_pointer_cast<cw::data::StructVal>(*object))
            for (std::size_t i=0;i<structure->m_member_names.size() && i<structure->m_member_values.size();++i)
                if (structure->m_member_names[i]=="path") {
                    const auto value=structure->m_member_values[i];
                    return face_path_truthy(value)?face_path_string(value): ::cw::lib::String{};
                }
    }
    throw std::invalid_argument("overlay carrier has no path attribute");
}

static bool python_digit_string(const String& text)
{
    if (text.empty()) return false;
    try {
        const auto utf16 = ::cw::lib::encoding::utf8_to_utf16(text, ::cw::lib::encoding::ByteOrder::Little);
        // 参照版Python 3.8のisdigit()に基づく、UCD 12.1のNd以外の
        // Digit範囲。int(str)はこれらを拒否するため、呼び出し元もその失敗を保つ。
        static constexpr std::pair<std::uint32_t, std::uint32_t> extra[]{{0xb2, 0xb3}, {0xb9, 0xb9}, {0x1369, 0x1371}, {0x19da, 0x19da}, {0x2070, 0x2070}, {0x2074, 0x2079}, {0x2080, 0x2089}, {0x2460, 0x2468}, {0x2474, 0x247c}, {0x2488, 0x2490}, {0x24ea, 0x24ea}, {0x24f5, 0x24fd}, {0x24ff, 0x24ff}, {0x2776, 0x277e}, {0x2780, 0x2788}, {0x278a, 0x2792}, {0x10a40, 0x10a43}, {0x10e60, 0x10e68}, {0x11052, 0x1105a}, {0x1f100, 0x1f10a}};
        for (std::size_t i = 0; i < utf16.size(); i += 2) {
            std::uint32_t c = static_cast<unsigned char>(utf16[i]) | (static_cast<unsigned char>(utf16[i + 1]) << 8);
            if (c >= 0xd800 && c <= 0xdbff) {
                const auto low = static_cast<unsigned char>(utf16[i + 2]) | (static_cast<unsigned char>(utf16[i + 3]) << 8);
                c = 0x10000 + ((c - 0xd800) << 10) + low - 0xdc00;
                i += 2;
            }
            if (!::cw::lib::python_decimal_digit(c) &&
                !std::any_of(std::begin(extra), std::end(extra), [c](const auto& range) { return range.first <= c && c <= range.second; })) return false;
        }
        return true;
    } catch (const ::cw::lib::encoding::EncodingError&) {
        return false;
    }
}

std::vector<bool> pick_up_face_image(::cw::lib::Value arg_target, ::cw::lib::Value arg_layer_data, bool arg_skip_xN)
{
    const auto* target_object = std::get_if<::cw::lib::ObjectPtr>(&arg_target.storage());
    const auto character = target_object && *target_object ? std::dynamic_pointer_cast<::cw::character::Character>(*target_object) : nullptr;
    const auto* layers_object = std::get_if<::cw::lib::ObjectPtr>(&arg_layer_data.storage());
    const auto layers = layers_object && *layers_object ? std::dynamic_pointer_cast<::cw::data::VariantSequence>(*layers_object) : nullptr;
    if (!character || !layers) return layers ? std::vector<bool>(layers->m_values.size(), true) : std::vector<bool>{};

    const auto status_matches = [&](const String& status) {
        const auto name = lower_ascii(status);
        if (name == "normal") return character->is_normal();
        if (name == "panic") return character->is_panic();
        if (name == "brave") return character->is_brave();
        if (name == "overheat") return character->is_overheat();
        if (name == "confuse") return character->is_confuse();
        if (name == "sleep") return character->is_sleep();
        if (name == "paralyze") return character->is_paralyze();
        if (name == "poison") return character->is_poison();
        if (name == "bind") return character->is_bind();
        if (name == "silence") return character->is_silence();
        if (name == "faceup") return character->is_faceup();
        if (name == "antimagic") return character->is_antimagic();
        if (name == "petrified") return character->is_petrified();
        if (name == "unconscious") return character->is_unconscious();
        if (name == "heavyinjured") return character->is_heavyinjured();
        if (name == "injured") return character->is_injured();
        if (name == "injuredall") return character->is_injuredall();
        if (name == "inactive") return character->is_inactive();
        if (name == "active") return character->is_active();
        if (name == "dead") return character->is_dead();
        if (name == "alive") return character->is_alive();
        if (name == "fine") return character->is_fine();
        if (name == "analyzable") return character->is_analyzable();
        if (name == "avoidable") return character->is_avoidable();
        if (name == "resistable") return character->is_resistable();
        if (name == "reversed") return character->is_reversed();
        if (name == "vanished") return character->is_vanished();
        if (name == "enhanced_act") return character->is_enhanced_act();
        if (name == "enhanced_res") return character->is_enhanced_res();
        if (name == "enhanced_avo") return character->is_enhanced_avo();
        if (name == "enhanced_def") return character->is_enhanced_def();
        if (name == "upaction") return character->is_upaction();
        if (name == "upresist") return character->is_upresist();
        if (name == "upavoid") return character->is_upavoid();
        if (name == "updefense") return character->is_updefense();
        if (name == "downaction") return character->is_downaction();
        if (name == "downresist") return character->is_downresist();
        if (name == "downavoid") return character->is_downavoid();
        if (name == "downdefense") return character->is_downdefense();
        return false;
    };

    const auto n = layers->m_values.size();
    // 共通のint(str)構文と正確なDecimal格納により、任意の
    // 大きさの優先度を保持する。固定幅へ狭めると異なる優先度を同順位にし
    // てしまう。Decimalの既存の100万桁の確保上限は保つ。実際
    // のファイル名要素は十分短いが、巨大な生のAPI入力は失敗し得る。
    std::vector<::cw::lib::Decimal> priorities(n), orders(n);
    const auto integer = [](const String& text) -> std::optional<::cw::lib::Decimal> {
        const auto normalized = ::cw::lib::normalize_python_integer(text);
        if (!normalized) return {};
        return ::cw::lib::Decimal::parse(*normalized);
    };
    std::vector<bool> result(n, false);
    std::vector<std::size_t> base_indices;
    for (std::size_t i = 0; i < n; ++i) {
        const auto path = face_path_string(py_get_path(layers->m_values[i]));
        if (path.empty()) { result[i] = true; continue; }
        const auto filename = ::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(path).filename());
        const auto stem = ::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(filename).stem());
        std::vector<String> conditions;
        std::size_t start = 0;
        while (start <= stem.size()) {
            const auto end = stem.find('.', start);
            conditions.push_back(stem.substr(start, end == String::npos ? String::npos : end - start));
            if (end == String::npos) break;
            start = end + 1;
        }
        if (conditions.size() == 1) { base_indices.push_back(i); continue; }
        bool priority_parsed = false, order_parsed = false, should_include = false;
        for (std::size_t j = 1; j < conditions.size();) {
            const auto& token = conditions[j];
            // 優先度のトークンにはstr.isdigit()を使う。int(str)
            // が受け付ける符号・アンダースコア・空白も、こちらでは除外する。
            if (python_digit_string(token)) {
                const auto numeric = integer(token);
                if (!numeric) throw std::invalid_argument("invalid Python priority integer: " + token);
                if (!priority_parsed) { priorities[i] = *numeric; priority_parsed = true; }
                else if (!order_parsed) { orders[i] = *numeric; order_parsed = true; }
                ++j;
                continue;
            }
            if (token == "all") { should_include = true; break; }
            if (token == "life" && j + 2 < conditions.size()) {
                const auto min_hp = integer(conditions[j + 1]);
                const auto max_hp = integer(conditions[j + 2]);
                if (min_hp && max_hp) {
                    // Pyはmaxlifeではなく、未定義のCharacter.maxLife
                    // 属性を要求する。この失敗を保持し、共通の画像処理がPy
                    // の全画像を使う代替経路へ進むようにする。ネイティブの
                    // 型付きフィールドの境界でもm_maxlifeを代入せ
                    // ず、AttributeError相当の例外を投げる。
                    throw std::invalid_argument("Character has no attribute maxLife");
                }
                j += 3;
                continue;
            }
            if (token == "coupon" && j + 1 < conditions.size()) {
                if (character->has_coupon(conditions[j + 1])) { should_include = true; break; }
                j += 2;
                continue;
            }
            if (token == "status" && j + 1 < conditions.size()) {
                if (status_matches(conditions[j + 1])) { should_include = true; break; }
                j += 2;
                continue;
            }
            ++j;
        }
        result[i] = should_include;
    }
    if (arg_skip_xN) {
        for (std::size_t i = 0; i < n; ++i) if (result[i] && get_scale_xN(face_path_string(py_get_path(layers->m_values[i]))) > 1.0) result[i] = false;
    }
    std::vector<std::size_t> selected;
    for (std::size_t i = 0; i < n; ++i) if (result[i]) selected.push_back(i);
    const auto is_overlay = [&](std::size_t index) {
        const auto path = face_overlay_path(layers->m_values[index]);
        const auto stem = ::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(path).stem());
        std::size_t start = 0;
        while (start <= stem.size()) {
            const auto end = stem.find('.', start);
            if (stem.substr(start, end == String::npos ? String::npos : end - start) == "overlay") return true;
            if (end == String::npos) break;
            start = end + 1;
        }
        return false;
    };
    std::vector<std::size_t> non_overlay;
    for (const auto index : selected) if (!is_overlay(index)) non_overlay.push_back(index);
    if (!non_overlay.empty()) {
        const auto max_priority = std::max_element(non_overlay.begin(), non_overlay.end(), [&](auto lhs, auto rhs) { return priorities[lhs] < priorities[rhs]; });
        const auto priority = priorities[*max_priority];
        for (const auto index : non_overlay) if (priorities[index] != priority) result[index] = false;
    } else if (!base_indices.empty()) {
        result[base_indices.front()] = true;
    }
    return result;
}

bool pick_up_face_image__is_overlay(::cw::lib::Integer arg_idx)
{
    (void)arg_idx;
    // Pythonの補助処理は各レイヤーの位置ではなくファイル名を捕捉するクロージャ
    // 。公開された生成済みの中継関数にはレイヤー引数がなく、その動作を再現できない。
    return false;
}

::cw::lib::Real get_scale_xN(::cw::lib::String arg_path)
{
    if (arg_path.empty()) return 1.0;
    std::smatch match;
    if (!std::regex_match(arg_path, match, std::regex(R"(^(.+)\.x([^.]+)(\.[^.]+)$)", std::regex::icase))) return 1.0;
    if (!python_digit_string(match[2].str())) return 1.0;
    Integer scale = 0;
    // ネイティブ範囲を超えるPython整数は、許可された画面倍率に
    // はなり得ない。例外や切り詰めを行わず、同じ結果の1.0を返す。
    const auto parsed = ::cw::lib::parse_python_integer(match[2].str());
    if (!parsed) return 1.0;
    scale = *parsed;
    Integer max_scale = 1;
    while (max_scale <= static_cast<Integer>(::cw::g_UP_SCR) / 2) max_scale *= 2;
    bool allowed = false;
    if (::cw::g_cwpy) {
        if (const auto object = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_sdata.storage()); object && *object) {
            if (const auto scenario = std::dynamic_pointer_cast<::cw::data::ScenarioData>(*object)) allowed = scenario->m_can_loaded_scaledimage;
        }
    }
    const auto tempdir = value_string(::cw::g_tempdir);
    const auto skindir = ::cw::g_cwpy ? value_string(::cw::g_cwpy->m_skindir) : String{};
    const auto temp_prefix=join_paths({tempdir,"ScenarioLog/TempFile"})+"/";
    const auto table_prefix=join_paths({skindir,"Table"})+"/";
    if (arg_path.rfind(temp_prefix,0)==0 || arg_path.rfind(table_prefix,0)==0) allowed=true;
    String resolved = arg_path;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_rsrc) {
        const auto candidate = ::cw::g_cwpy->m_rsrc->get_filepath(arg_path);
        if (!candidate.empty()) resolved = candidate;
    }
    const bool exists = path_is_file(resolved);
    if (allowed && exists && scale >= 2 && scale <= max_scale) return static_cast<Real>(scale);
    return 1.0;
}

std::set<::cw::lib::Integer> _match_node(::cw::lib::Value node, ::cw::lib::String text, ::cw::lib::Integer pos)
{
    // Pyは未知のノード型を拒否し、黙って空文字列に一致させない。Pyの公開結果
    // は不変のfrozensetだが、ネイティブAPIは所有権を持つ集合値を返
    // す。呼び出し元が複製を編集しても、ノード・将来の照合・他の結果は変わらな
    // い。ネイティブの利用側は位置を統合・参照し、Pythonのfrozenset
    // の同一性や変更時の例外はC++の集合インターフェースでは再現しない。
    return regex_match_node(regex_import_public_node(node),regex_characters(text),pos);
}
std::set<::cw::lib::Integer> _closure(::cw::lib::Value node, ::cw::lib::String text, ::cw::lib::Integer pos)
{
    // _match_nodeと同様に所有権を持つ位置の値を返し、共有キャッシュにはしない。
    return regex_closure(regex_import_public_node(node),regex_characters(text),pos);
}

bool regex_fullmatch(::cw::lib::String arg_pattern, ::cw::lib::String arg_string)
{
    try {
        // セル名とChangeEnemyの呼び出し元が共有する、元Engineの
        // 制限された正規表現文法。Editorの正規表現には別のAPIがある。
        const auto text = regex_characters(arg_string);
        const auto matched = regex_match_node(RegexParser(arg_pattern).parse(), text, 0);
        return matched.contains(static_cast<Integer>(text.size()));
    } catch (...) {
        // ここでは不正なパターンを通常の不一致とする。_Parserの直接呼び出しでは、公
        // 開基底_ParseErrorを通してRegexParseErrorを通知する。
        return false;
    }
}

std::vector<::cw::lib::String> match_cellnames_by_regex(::cw::lib::String arg_pattern, std::vector<::cw::lib::String> arg_cur_names)
{
    std::vector<String> result;
    for (const auto& name : arg_cur_names) if (regex_fullmatch(arg_pattern, name)) result.push_back(name);
    return result;
}

void t_start()
{
    std::lock_guard<std::mutex> guard(timing_mutex);
    performance_timer_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    sync_performance_globals();
}

void t_end(::cw::lib::Integer arg_index)
{
    std::lock_guard<std::mutex> guard(timing_mutex);
    const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    const auto elapsed = now - performance_timer_ns;
    const auto index = arg_index < 0 ? static_cast<Integer>(performance_times.size()) + arg_index : arg_index;
    // std::out_of_rangeはPythonのIndexErrorに相当するC++
    // 側の境界。添字アクセスに失敗した場合は、カウンタとタイマーの両方を変えない。
    if (index < 0 || index >= static_cast<Integer>(performance_times.size())) throw std::out_of_range("timing index out of range");
    performance_times[static_cast<std::size_t>(index)] += elapsed;
    performance_timer_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    sync_performance_globals();
}

void td_end(::cw::lib::String arg_key)
{
    std::lock_guard<std::mutex> guard(timing_mutex);
    const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    performance_dictimes[std::move(arg_key)] += now - performance_timer_ns;
    performance_timer_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    sync_performance_globals();
}

void t_reset()
{
    std::lock_guard<std::mutex> guard(timing_mutex);
    std::fill(performance_times.begin(), performance_times.end(), 0);
    performance_dictimes.clear();
    sync_performance_globals();
}

void t_print()
{
    std::lock_guard<std::mutex> guard(timing_mutex);
    std::vector<String> lines;
    for (std::size_t i = 0; i < performance_times.size(); ++i) {
        if (performance_times[i] <= 0) continue;
        const auto line = "time[" + std::to_string(i) + "] = " + std::to_string(performance_times[i]);
        lines.push_back(line);
        std::cout << line << '\n';
    }
    for (const auto& [key, value] : performance_dictimes) {
        if (value <= 0) continue;
        const auto line = "time[" + key + "] = " + std::to_string(value);
        lines.push_back(line);
        std::cout << line << '\n';
    }
    if (!lines.empty()) {
        std::ofstream output("performance.txt", std::ios::out | std::ios::trunc);
        if (output) {
            for (std::size_t i = 0; i < lines.size(); ++i) {
                if (i) output << '\n';
                output << lines[i];
            }
            output.flush();
        }
    }
}

// Pythonが親を作成するのはPOSIX経路だけ。共通のネイティブ
// 規則では、どちらのOSのロック操作より前にも印の親ディレクトリを
// 準備し、更新用ディレクトリがない場合を全ホストで同じように扱う。
bool create_mutex(::cw::lib::String arg_dpath)
{
    std::lock_guard<std::mutex> guard(mutex_api_mutex);
    const auto lock_path = normalized_mutex_path(arg_dpath);
    std::error_code ec;
    if (!lock_path.parent_path().empty()) std::filesystem::create_directories(lock_path.parent_path(), ec);
    // 親を作れない場合のエラーは開く処理に任せる。PyはWindows
    // のopenエラーを伝え、POSIXのIOError経路ではfalse
    // を返す。恒久的なWindowsの入出力失敗をロック競合
    // に見せ、共通スキン更新処理の取得ループを続けさせない。
    MutexRecord record;
    record.path = lock_path;
#if defined(_WIN32)
    // Pythonのopen("w")はロック取得前に古い印のフ
    // ァイルを切り詰める。印の内容はロック状態ではないため、OPEN_ALWAYS
    // で既存バイトを保ち、所有権はOSのロックだけで判定
    // する。POSIXのO_CREATも切り詰めない。
    record.handle = CreateFileW(lock_path.wstring().c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (record.handle == INVALID_HANDLE_VALUE) {
        const auto error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
        throw std::filesystem::filesystem_error("create_mutex open", lock_path, error);
    }
    OVERLAPPED overlapped{};
    if (!LockFileEx(record.handle, LOCKFILE_FAIL_IMMEDIATELY | LOCKFILE_EXCLUSIVE_LOCK, 0, 0, 0xffff0000, &overlapped)) {
        CloseHandle(record.handle);
        return false;
    }
#else
    record.fd = ::open(lock_path.c_str(), O_RDWR | O_CREAT, 0666);
    if (record.fd < 0) return false;
    if (::flock(record.fd, LOCK_EX | LOCK_NB) != 0) {
        ::close(record.fd);
        return false;
    }
#endif
    mutex_records.push_back(std::move(record));
    sync_mutex_globals();
    return true;
}

bool exists_mutex(::cw::lib::String arg_dpath)
{
    std::lock_guard<std::mutex> guard(mutex_api_mutex);
    const auto lock_path = normalized_mutex_path(arg_dpath);
    for (const auto& record : mutex_records) if (record.path == lock_path) return false;
#if defined(_WIN32)
    std::error_code ec;
    if (!std::filesystem::is_regular_file(lock_path, ec)) return false;
    const auto handle = CreateFileW(lock_path.wstring().c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return true;
    OVERLAPPED overlapped{};
    const bool locked = LockFileEx(handle, LOCKFILE_FAIL_IMMEDIATELY | LOCKFILE_EXCLUSIVE_LOCK, 0, 0, 0xffff0000, &overlapped) != 0;
    if (locked) {
        UnlockFileEx(handle, 0, 0, 0xffff0000, &overlapped);
        CloseHandle(handle);
        try { remove(::cw::lib::encoding::path_to_utf8(lock_path), false); }
        catch (...) { return true; } // PythonのWindows版existsはExceptionを捕捉する。
        return false;
    }
    CloseHandle(handle);
    return true;
#else
    // PythonのPOSIX版の検査は、不足する親や印のファイルを作りflockを調べる
    // 。作成できないパスは使用中のTrueとし、早期にファイル不在のFalseを返さない。
    try {
        if (!std::filesystem::is_regular_file(lock_path) &&
            !std::filesystem::is_directory(lock_path.parent_path()))
            std::filesystem::create_directories(lock_path.parent_path());
    } catch (const std::system_error&) { return true; }
    // Pythonのopen(name, "wb")と同様に書き込み権限を要求し、
    // flockより前に切り詰める。後続の利用可能性の検査に失敗する場合も同じ。
    const auto fd = ::open(lock_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return true;
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
        ::close(fd);
        return true;
    }
    ::flock(fd, LOCK_UN);
    ::close(fd);
    try { remove(::cw::lib::encoding::path_to_utf8(lock_path), false); }
    catch (const std::system_error&) { return true; } // PythonはIOErrorを捕捉する。
    return false;
#endif
}

namespace {
// 呼び出し元がmutex_api_mutexを保持する。この基本操作では再取得しない。
// 公開の_Unlockの寿命を保つため、ファイルのロック解除とタプルの除去を分離する。
bool unlock_mutex_record(std::size_t index, std::optional<::cw::lib::Value>* public_file)
{
    auto& record = mutex_records[index];
#if defined(_WIN32)
    if (record.handle != INVALID_HANDLE_VALUE) {
        OVERLAPPED overlapped{};
        UnlockFileEx(record.handle, 0, 0, 0xffff0000, &overlapped);
        CloseHandle(record.handle);
        record.handle = INVALID_HANDLE_VALUE;
    } else return false;
#else
    if (record.fd >= 0) {
        // Pythonのfcntlはロック解除に失敗すると、FileIO.closeや取り出しより前に例外を投げる。
        if (::flock(record.fd, LOCK_UN) != 0)
            throw std::system_error(errno, std::generic_category(), "flock: unlock");
        const auto result = ::close(record.fd);
        const auto close_error = errno;
        record.fd = -1;
        // FileIOはcloseがOSエラーを報告しても、自身を閉じた状態にする。
        if (result != 0)
            throw std::system_error(close_error, std::generic_category(), "close: mutex");
    } else {
        // 保持されたPOSIXのタプルはPythonのFileIOを所有する。削除
        // 失敗時に閉じられると、その後のfileno()はValueErrorを投
        // げる。タプルを黙って取り除かず保持し、対応するネイティブの例外を伝える。
        throw std::invalid_argument("unlock_mutex_record: I/O operation on closed file");
    }
#endif
    // PythonはOSのロック解除後、removeが例外を投げるより前にowner.f
    // を解除する。ネイティブの生ハンドルは非公開の登録表が所有するため、
    // 所有関係を置き換えず、同じ時点で公開Valueアダプタを更新する。
    if (public_file) public_file->reset();
    // 共通のremoveがFileSyncの完了待ちとファイル削除の方針を担う。
    // 呼び出し元がPythonと同じ取り出しを終えるまで、閉じた記録を保持する。
    remove(::cw::lib::encoding::path_to_utf8(record.path), false);
    cleanup_mutex_directory(record.path);
    return true;
}

void release_mutex_record(std::size_t index)
{
    (void)unlock_mutex_record(index);
    mutex_records.erase(mutex_records.begin() + index);
    sync_mutex_globals();
}
}

void release_mutex(::cw::lib::Integer arg_index)
{
    std::lock_guard<std::mutex> guard(mutex_api_mutex);
    if (mutex_records.empty()) return; // Pythonはリストが空ならindexを無視する。
    const auto index = arg_index < 0 ? static_cast<Integer>(mutex_records.size()) + arg_index : arg_index;
    // std::out_of_rangeはPythonのIndexErrorに相当するネイティブの例外。
    if (index < 0 || static_cast<std::size_t>(index) >= mutex_records.size())
        throw std::out_of_range("release_mutex: list index out of range");
    release_mutex_record(static_cast<std::size_t>(index));
}

void release_mutex_path(const ::cw::lib::String& path)
{
    std::lock_guard<std::mutex> guard(mutex_api_mutex);
    const auto lock_path = normalized_mutex_path(path);
    for (std::size_t index = 0; index < mutex_records.size(); ++index) {
        if (mutex_records[index].path == lock_path) {
            release_mutex_record(index);
            return;
        }
    }
}

void clear_mutex()
{
    std::lock_guard<std::mutex> guard(mutex_api_mutex);
    // Pythonは取得順にロックを解除し、すべての削除が成功した後でのみ
    // _mutexを置き換える。途中で例外が出た場合は全タプルを保持する。
    for (std::size_t index = 0; index < mutex_records.size(); ++index)
        (void)unlock_mutex_record(index);
    mutex_records.clear();
    sync_mutex_globals();
}

void main()
{
#if defined(_WIN32)
    // util.pyのwin32timezone.now()のインポート時のガードと同じ
    // Windowsのタイムゾーンデータベース初期化を行う。イベントループは所有しない。
    TIME_ZONE_INFORMATION information{};
    (void)GetTimeZoneInformation(&information);
    // ネイティブのWindows補助処理はpywin32のCOMアパートメント設定を
    // 引き継がない。ここで呼び出しスレッドをショートカットやシェルのCOM操作向け
    // に準備する。配布用だけのPythonのmainと異なり、初期化失敗は伝える。
    _co_initialize();
#endif
}

}
