#define _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
#include "cw/bassplayer.hpp"
#include "cw/cw.hpp"
#include "cw/setting.hpp"
#include "cw/thread.hpp"
#include "cw/util.hpp"
#include "cw/lib/python_integer.hpp"
#include "cw/lib/python_text.hpp"
#include "cw/lib/encoding.hpp"
#include "cw/lib/python_float.hpp"
#include "cw/lib/audio_loop_position.hpp"
#include "platform/storage/storage_mount.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cctype>
#include <codecvt>
#include <condition_variable>
#include <cmath>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <locale>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
using Integer = ::cw::lib::Integer;
using LoopCount = ::cw::bassplayer::LoopCount;
using Real = ::cw::lib::Real;
using Decimal = ::cw::lib::Decimal;
using String = ::cw::lib::String;
using Value = ::cw::lib::Value;

constexpr std::size_t kStreamCount = 5;

// BASSの公開ABIはC API。ヘッダー・静的ライブラリへの依存を避けるため、
// Windowsでは必要な型と関数だけをここで遅延解決する。 Windows以外
// ではAPIを有効にせず、呼び出し側がSDLなどの共通バックエンドへ切り替える。
using NativeHandle = std::uint32_t;

#if defined(_WIN32)
using BassDword = std::uint32_t;
using BassQword = std::uint64_t;
using BassBool = int;
using BassCallback = void (CALLBACK*)(NativeHandle, BassDword, BassDword, void*);
struct BassDeviceInfoRaw { const char* name; const char* driver; BassDword flags; };
struct BassMidiFontRaw { NativeHandle font; int preset; int bank; };
struct BassChannelInfoRaw {
    BassDword freq;
    BassDword chans;
    BassDword flags;
    BassDword ctype;
    BassDword origres;
    NativeHandle plugin;
    NativeHandle sample;
    const char* filename;
};
struct BassMidiEventRaw {
    BassDword event;
    BassDword param;
    BassDword chan;
    BassDword tick;
    BassDword pos;
};

struct BassApi {
    HMODULE bass{};
    HMODULE midi{};
    HMODULE fx{};
    bool initialized{};
    using Init = BassBool (WINAPI*)(int, BassDword, BassDword, HWND, const void*);
    using Bool0 = BassBool (WINAPI*)();
    using GetDevice = BassDword (WINAPI*)();
    using SetDevice = BassBool (WINAPI*)(BassDword);
    using GetDeviceInfo = BassBool (WINAPI*)(BassDword, BassDeviceInfoRaw*);
    using StreamCreateFile = NativeHandle (WINAPI*)(BassBool, const void*, BassQword, BassQword, BassDword);
    using StreamFree = BassBool (WINAPI*)(NativeHandle);
    using MidiStreamCreateFile = NativeHandle (WINAPI*)(BassBool, const void*, BassQword, BassQword, BassDword, BassDword);
    using MidiFontInit = NativeHandle (WINAPI*)(const void*, BassDword);
    using MidiFontFree = BassBool (WINAPI*)(NativeHandle);
    using MidiFontSetVolume = BassBool (WINAPI*)(NativeHandle, float);
    using MidiStreamSetFonts = BassBool (WINAPI*)(NativeHandle, const BassMidiFontRaw*, BassDword);
    using ChannelPlay = BassBool (WINAPI*)(NativeHandle, BassBool);
    using ChannelStop = BassBool (WINAPI*)(NativeHandle);
    using ChannelSetAttribute = BassBool (WINAPI*)(NativeHandle, BassDword, float);
    using ChannelSlideAttribute = BassBool (WINAPI*)(NativeHandle, BassDword, float, BassDword);
    using ChannelSetPosition = BassBool (WINAPI*)(NativeHandle, BassQword, BassDword);
    using ChannelGetAttribute = BassBool (WINAPI*)(NativeHandle, BassDword, float*);
    using ChannelSetSync = NativeHandle (WINAPI*)(NativeHandle, BassDword, BassQword, BassCallback, void*);
    using ChannelRemoveSync = BassBool (WINAPI*)(NativeHandle, NativeHandle);
    using ChannelSetDevice = BassBool (WINAPI*)(NativeHandle, BassDword);
    using ChannelGetInfo = BassBool (WINAPI*)(NativeHandle, BassChannelInfoRaw*);
    using ChannelGetTags = const char* (WINAPI*)(NativeHandle, BassDword);
    using ChannelIsActive = BassDword (WINAPI*)(NativeHandle);
    using MidiStreamGetEvents = BassDword (WINAPI*)(NativeHandle, int, BassDword, void*);
    using TempoCreate = NativeHandle (WINAPI*)(NativeHandle, BassDword);
    using ErrorGetCode = int (WINAPI*)();

    Init init{};
    Bool0 free_library{};
    Bool0 start{};
    Bool0 pause{};
    Bool0 stop{};
    GetDevice get_device{};
    SetDevice set_device{};
    GetDeviceInfo get_device_info{};
    StreamCreateFile stream_create_file{};
    StreamFree stream_free{};
    MidiStreamCreateFile midi_stream_create_file{};
    MidiFontInit midi_font_init{};
    MidiFontFree midi_font_free{};
    MidiFontSetVolume midi_font_set_volume{};
    MidiStreamSetFonts midi_stream_set_fonts{};
    ChannelPlay channel_play{};
    ChannelStop channel_stop{};
    ChannelStop channel_pause{};
    ChannelSetAttribute channel_set_attribute{};
    ChannelSlideAttribute channel_slide_attribute{};
    ChannelSetPosition channel_set_position{};
    ChannelGetAttribute channel_get_attribute{};
    ChannelSetSync channel_set_sync{};
    ChannelRemoveSync channel_remove_sync{};
    ChannelSetDevice channel_set_device{};
    ChannelGetInfo channel_get_info{};
    ChannelGetTags channel_get_tags{};
    ChannelIsActive channel_is_active{};
    MidiStreamGetEvents midi_stream_get_events{};
    TempoCreate tempo_create{};
    ErrorGetCode error_get_code{};

    template<typename T>
    static T proc(HMODULE module, const char* name)
    {
        return module == nullptr ? nullptr : reinterpret_cast<T>(::GetProcAddress(module, name));
    }

    static HMODULE load_module(const wchar_t* name)
    {
        wchar_t buffer[MAX_PATH]{};
        const auto length = ::GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
        if (length == 0 || length >= std::size(buffer)) return nullptr;
        const auto dir = std::filesystem::path(buffer).parent_path();
#if defined(_WIN64)
        if (HMODULE module = ::LoadLibraryW((dir / L"lib" / name).c_str())) return module;
        if (HMODULE module = ::LoadLibraryW((dir / L"lib" / L"x64" / name).c_str())) return module;
        if (HMODULE module = ::LoadLibraryW((dir / L"x64" / name).c_str())) return module;
        return ::LoadLibraryW((dir / name).c_str());
#else
        if (HMODULE module = ::LoadLibraryW((dir / L"lib" / name).c_str())) return module;
        if (HMODULE module = ::LoadLibraryW((dir / L"lib" / L"x86" / name).c_str())) return module;
        if (HMODULE module = ::LoadLibraryW((dir / name).c_str())) return module;
        return ::LoadLibraryW((dir / L"x64" / name).c_str());
#endif
    }

    bool load()
    {
        if (bass != nullptr) return true;
        bass = load_module(L"bass.dll");
        if (bass == nullptr) return false;
        midi = load_module(L"bassmidi.dll");
        fx = load_module(L"bass_fx.dll");
        init = proc<Init>(bass, "BASS_Init");
        free_library = proc<Bool0>(bass, "BASS_Free");
        start = proc<Bool0>(bass, "BASS_Start");
        pause = proc<Bool0>(bass, "BASS_Pause");
        stop = proc<Bool0>(bass, "BASS_Stop");
        get_device = proc<GetDevice>(bass, "BASS_GetDevice");
        set_device = proc<SetDevice>(bass, "BASS_SetDevice");
        get_device_info = proc<GetDeviceInfo>(bass, "BASS_GetDeviceInfo");
        stream_create_file = proc<StreamCreateFile>(bass, "BASS_StreamCreateFile");
        stream_free = proc<StreamFree>(bass, "BASS_StreamFree");
        channel_play = proc<ChannelPlay>(bass, "BASS_ChannelPlay");
        channel_pause = proc<ChannelStop>(bass, "BASS_ChannelPause");
        channel_stop = proc<ChannelStop>(bass, "BASS_ChannelStop");
        channel_set_attribute = proc<ChannelSetAttribute>(bass, "BASS_ChannelSetAttribute");
        channel_slide_attribute = proc<ChannelSlideAttribute>(bass, "BASS_ChannelSlideAttribute");
        channel_set_position = proc<ChannelSetPosition>(bass, "BASS_ChannelSetPosition");
        channel_get_attribute = proc<ChannelGetAttribute>(bass, "BASS_ChannelGetAttribute");
        channel_set_sync = proc<ChannelSetSync>(bass, "BASS_ChannelSetSync");
        channel_remove_sync = proc<ChannelRemoveSync>(bass, "BASS_ChannelRemoveSync");
        channel_set_device = proc<ChannelSetDevice>(bass, "BASS_ChannelSetDevice");
        channel_get_info = proc<ChannelGetInfo>(bass, "BASS_ChannelGetInfo");
        channel_get_tags = proc<ChannelGetTags>(bass, "BASS_ChannelGetTags");
        channel_is_active = proc<ChannelIsActive>(bass, "BASS_ChannelIsActive");
        error_get_code = proc<ErrorGetCode>(bass, "BASS_ErrorGetCode");
        if (init == nullptr || free_library == nullptr || stream_create_file == nullptr ||
            stream_free == nullptr || channel_play == nullptr || channel_stop == nullptr ||
            channel_set_attribute == nullptr || channel_set_sync == nullptr ||
            channel_remove_sync == nullptr || channel_get_info == nullptr ||
            channel_get_tags == nullptr || channel_is_active == nullptr) {
            unload();
            return false;
        }
        if (midi != nullptr) {
            midi_stream_create_file = proc<MidiStreamCreateFile>(midi, "BASS_MIDI_StreamCreateFile");
            midi_font_init = proc<MidiFontInit>(midi, "BASS_MIDI_FontInit");
            midi_font_free = proc<MidiFontFree>(midi, "BASS_MIDI_FontFree");
            midi_font_set_volume = proc<MidiFontSetVolume>(midi, "BASS_MIDI_FontSetVolume");
            midi_stream_set_fonts = proc<MidiStreamSetFonts>(midi, "BASS_MIDI_StreamSetFonts");
            midi_stream_get_events = proc<MidiStreamGetEvents>(midi, "BASS_MIDI_StreamGetEvents");
        }
        if (fx != nullptr) tempo_create = proc<TempoCreate>(fx, "BASS_FX_TempoCreate");
        return true;
    }

    void unload()
    {
        if (initialized && free_library != nullptr) free_library();
        initialized = false;
        if (fx != nullptr) { ::FreeLibrary(fx); fx = nullptr; }
        if (midi != nullptr) { ::FreeLibrary(midi); midi = nullptr; }
        if (bass != nullptr) { ::FreeLibrary(bass); bass = nullptr; }
        *this = BassApi{};
    }
};
#else
struct BassApi {
    bool initialized{};
    bool load() { return false; }
    void unload() {}
};
#endif

BassApi g_bass_api;
// 短いストリームなどでは、BASS呼び出しから同期コールバック
// がその場で動く場合がある。 公開関数は再帰ロックを使い、コー
// ルバックは g_stream_mutex だけを取得する。
std::recursive_mutex g_audio_backend_mutex;
std::atomic_bool g_callbacks_stopping{true};
std::atomic_uint g_callbacks_inflight{0};
std::mutex g_callbacks_mutex;
std::condition_variable g_callbacks_condition;
std::vector<NativeHandle> g_font_handles;

class CallbackLease {
public:
    CallbackLease()
    {
        if (g_callbacks_stopping.load(std::memory_order_acquire)) return;
        g_callbacks_inflight.fetch_add(1, std::memory_order_acq_rel);
        if (g_callbacks_stopping.load(std::memory_order_acquire)) {
            release();
            return;
        }
        active_ = true;
    }
    ~CallbackLease() { if (active_) release(); }
    explicit operator bool() const noexcept { return active_; }
private:
    bool active_{};
    void release()
    {
        active_ = false;
        if (g_callbacks_inflight.fetch_sub(1, std::memory_order_acq_rel) == 1)
            g_callbacks_condition.notify_all();
    }
};

struct StreamState {
    Integer handle{};
    String path;
    bool midi{};
    bool playing{};
    bool paused{};
    bool fading_out{};
    Real volume{};
    Integer fade{};
    Integer tempo{};
    Integer pitch{};
    Integer position{};
    Integer loop_start{};
    Integer loop_end{-1};
    Integer frequency{44100};
    Integer channels{2};
    Integer flags{};
    NativeHandle source_native{};
    NativeHandle native{};
    NativeHandle end_sync{};
    NativeHandle slide_sync{};
    std::shared_ptr<std::vector<std::byte>> encoded_data{};
};

std::array<StreamState, kStreamCount> g_stream_state{};
struct FadeoutState {
    Integer handle{};
    LoopCount loops{};
    Integer loop_start{};
    NativeHandle native{};
    NativeHandle source_native{};
    NativeHandle end_sync{};
    NativeHandle slide_sync{};
    std::shared_ptr<std::vector<std::byte>> encoded_data{};
    bool paused{};
    bool slide_arming{};
    bool slide_completed{};
};
std::array<std::optional<FadeoutState>, kStreamCount> g_fadeout_state{};
std::array<Real, kStreamCount> g_stream_volume{};
std::vector<std::tuple<String, Real>> g_soundfonts;
std::recursive_mutex g_stream_mutex;
std::atomic<Integer> g_next_handle{1};
Integer g_current_device{};

Real value_real(const Value& value, Real fallback = 0.0)
{
    if (const auto* real = std::get_if<Real>(&value.storage())) return std::isfinite(*real) ? *real : fallback;
    if (const auto* integer = std::get_if<Integer>(&value.storage())) {
        const auto result = static_cast<Real>(*integer);
        return std::isfinite(result) ? result : fallback;
    }
    if (const auto* boolean = std::get_if<bool>(&value.storage())) return *boolean ? 1.0 : 0.0;
    if (const auto* decimal = std::get_if<Decimal>(&value.storage())) {
        try {
            const auto result = std::stod(decimal->to_string(false));
            return std::isfinite(result) ? result : fallback;
        } catch (...) { return fallback; }
    }
    if (const auto* text = std::get_if<String>(&value.storage())) {
        try {
            std::size_t used = 0;
            const auto result = std::stod(*text, &used);
            return used == text->size() && std::isfinite(result) ? result : fallback;
        } catch (...) { return fallback; }
    }
    return fallback;
}

Integer value_integer(const Value& value, Integer fallback = 0)
{
    if (const auto* integer = std::get_if<Integer>(&value.storage())) return *integer;
    if (const auto* boolean = std::get_if<bool>(&value.storage())) return *boolean ? 1 : 0;
    if (const auto* decimal = std::get_if<Decimal>(&value.storage())) {
        try { return decimal->to_integer(::cw::lib::DecimalRounding::Down); }
        catch (const ::cw::lib::DecimalError&) { return fallback; }
    }
    if (const auto* real = std::get_if<Real>(&value.storage())) {
        if (!std::isfinite(*real)) return fallback;
        const auto truncated = std::trunc(*real);
        const auto max_exclusive = static_cast<Real>((std::numeric_limits<Integer>::max)()) + 1.0;
        const auto min_inclusive = static_cast<Real>((std::numeric_limits<Integer>::lowest)());
        if (truncated >= max_exclusive || truncated < min_inclusive) return fallback;
        return static_cast<Integer>(truncated);
    }
    if (const auto* text = std::get_if<String>(&value.storage())) {
        try {
            std::size_t used = 0;
            const auto result = std::stoll(*text, &used);
            return used == text->size() ? result : fallback;
        } catch (...) { return fallback; }
    }
    return fallback;
}

String value_string(const Value& value, String fallback = {})
{
    if (const auto* text = std::get_if<String>(&value.storage())) return *text;
    if (const auto* decimal = std::get_if<Decimal>(&value.storage())) return decimal->to_string(true);
    if (const auto* integer = std::get_if<Integer>(&value.storage())) return std::to_string(*integer);
    if (const auto* real = std::get_if<Real>(&value.storage())) {
        std::ostringstream stream;
        stream << std::setprecision(std::numeric_limits<Real>::max_digits10) << *real;
        return stream.str();
    }
    if (const auto* boolean = std::get_if<bool>(&value.storage())) return *boolean ? "True" : "False";
    return fallback;
}

Real normalize_volume(Real volume)
{
    // 移植版の意図的な方針として、有限な0～1への既存の制
    // 限を維持する。 Python版は制限せず音量を渡すが
    // 、範囲外での機器の動作はこの共通境界では再現しない。
    if (!std::isfinite(volume)) return 0.0;
    return std::clamp(volume, Real{0.0}, Real{1.0});
}

Integer optional_index(const std::optional<Integer>& index)
{
    return index.value_or(0);
}

bool valid_index(Integer index)
{
    return index >= 0 && index < static_cast<Integer>(kStreamCount);
}

Integer stream_index(Integer channel, Integer offset)
{
    // ネイティブのコールバック境界を越える前に、Python形式のリスト位置を解決する
    // 。 負の位置をポインターのユーザーデータにせず、有効な実際のインデックスを渡す。
    const auto count = static_cast<Integer>(kStreamCount);
    if (channel < -count - offset || channel >= count - offset)
        throw std::out_of_range("audio stream index out of range");
    const auto index = channel + offset;
    return index < 0 ? index + count : index;
}

Integer stream_handle(const Value& value)
{
    return value_integer(value);
}

Integer stream_handle_at(Integer index)
{
    if (!valid_index(index)) return 0;
    return stream_handle(::cw::bassplayer::g__streams[static_cast<std::size_t>(index)]);
}

void assign_stream_handle(Integer index, Integer handle)
{
    if (!valid_index(index)) return;
    ::cw::bassplayer::g__streams[static_cast<std::size_t>(index)] = Value(handle);
}

// ストリーム枠の置換後にもネイティブのコールバックが来る場合がある。 一致す
// る現在のハンドル・フェード管理中のハンドルだけを解放し、渡されたというだ
// けで無関係なチャンネルを解放しない。 両チャンネル解放APIでこの所有権
// 検査を共有する。Python版は渡されたチャンネルを直接停止・解放する。
bool take_release_targets(Integer index, Integer channel, NativeHandle& native, NativeHandle& source,
                          NativeHandle& end_sync, NativeHandle& slide_sync,
                          std::shared_ptr<std::vector<std::byte>>& encoded_data)
{
    auto& state = g_stream_state[static_cast<std::size_t>(index)];
    auto& fade = g_fadeout_state[static_cast<std::size_t>(index)];
    if (state.handle == channel) {
        native = state.native;
        source = state.source_native;
        end_sync = state.end_sync;
        slide_sync = state.slide_sync;
        encoded_data = std::move(state.encoded_data);
        state = StreamState{};
        return true;
    }
    if (fade && fade->handle == channel) {
        native = fade->native;
        source = fade->source_native;
        end_sync = fade->end_sync;
        slide_sync = fade->slide_sync;
        encoded_data = std::move(fade->encoded_data);
        fade.reset();
        ::cw::bassplayer::g__fadeoutstreams[static_cast<std::size_t>(index)] = std::nullopt;
        return true;
    }
    return false;
}

bool bassmidi_sample32bit()
{
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting) return ::cw::g_cwpy->m_setting->m_bassmidi_sample32bit;
    return true;
}

bool is_regular_file(const String& path)
{
    const auto file = std::filesystem::u8path(path);
    if (::cardwirth::platform::is_mounted_user_path(file)) {
        const auto status = ::cardwirth::platform::stat_mounted_user_path(file);
        return status && status->is_file();
    }
    std::error_code ec;
    return std::filesystem::is_regular_file(file, ec);
}

bool has_midi_header(const String& path)
{
    const auto file = std::filesystem::u8path(path);
    if (::cardwirth::platform::is_mounted_user_path(file)) {
        const auto data = ::cardwirth::platform::read_mounted_user_file_prefix(file, 4);
        return data && data->size() >= 4 &&
            std::to_integer<char>((*data)[0]) == 'M' && std::to_integer<char>((*data)[1]) == 'T' &&
            std::to_integer<char>((*data)[2]) == 'h' && std::to_integer<char>((*data)[3]) == 'd';
    }
    std::ifstream input(file, std::ios::binary);
    std::array<char, 4> header{};
    return input && input.read(header.data(), static_cast<std::streamsize>(header.size())) &&
           std::string_view(header.data(), header.size()) == "MThd";
}

Integer loop_byte_position(double samples, double samples_per_byte, Integer samples_to_bytes)
{
    return ::cw::lib::audio_loop_byte_position(samples, samples_per_byte, samples_to_bytes);
}

String sli_text_from_bytes(const String& bytes)
{
#if defined(_WIN32)
    // Python標準の open(path, "r") はWindowsのANSI
    // コードページを使う。 ネイティブの組み込み環境にはインタープリター固有のUTF-8
    // モードの上書きがないため、OSの文字コード処理をここで使う。 読み込み・解
    // 析・代替の方針は下でネイティブとマウント済み保存領域に共通化する。 BOM
    // を推測したり、Pythonが拒否する不正バイトを黙って置換したりしない。
    if (bytes.empty()) return {};
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw ::cw::lib::encoding::EncodingError(".sli text exceeds the Windows codec range");
    const UINT codepage = GetACP();
    if (codepage == 932)
        return ::cw::lib::encoding::cp932_to_utf8(::cw::lib::Bytes(bytes.begin(), bytes.end()));
    const int length = static_cast<int>(bytes.size());
    const int count = MultiByteToWideChar(codepage, MB_ERR_INVALID_CHARS, bytes.data(), length, nullptr, 0);
    if (count <= 0) throw ::cw::lib::encoding::EncodingError("Invalid .sli text encoding");
    std::wstring text(static_cast<std::size_t>(count), L'\0');
    if (MultiByteToWideChar(codepage, MB_ERR_INVALID_CHARS, bytes.data(), length, text.data(), count) != count)
        throw ::cw::lib::encoding::EncodingError(".sli text decoding failed");
    const int encoded = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), count, nullptr, 0, nullptr, nullptr);
    if (encoded <= 0) throw ::cw::lib::encoding::EncodingError(".sli UTF-8 conversion failed");
    String result(static_cast<std::size_t>(encoded), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), count, result.data(), encoded, nullptr, nullptr) != encoded)
        throw ::cw::lib::encoding::EncodingError(".sli UTF-8 conversion failed");
    return result;
#else
    // Windows以外のネイティブ文字列は、このプロジェクトのUTF-8境界を使う
    // 。 組み込み環境にはPythonのロケール強制・UTF-8モードの状態はない。
    return ::cw::lib::encoding::decode_utf8(::cw::lib::Bytes(bytes.begin(), bytes.end()),
        ::cw::lib::encoding::ErrorPolicy::Strict, false);
#endif
}

std::optional<std::tuple<Integer, Integer>> read_sli(const String& path, double samples_per_byte, Integer samples_to_bytes)
try {
    const auto sli = std::filesystem::u8path(path + ".sli");
    std::ifstream native_input;
    std::istringstream mounted_input;
    std::istream* input = nullptr;
    if (::cardwirth::platform::is_mounted_user_path(sli)) {
        const auto data = ::cardwirth::platform::read_mounted_user_file(sli);
        if (!data) return std::nullopt;
        mounted_input.str(String(reinterpret_cast<const char*>(data->data()), data->size()));
        input = &mounted_input;
    } else {
        // バイト列全体を自分で復号する。 CRTのテキストモードはCtrl-Zで止まる場合
        // があるが、PythonのTextIOWrapperはEOFとして扱わないため。
        native_input.open(sli, std::ios::binary);
        if (!native_input) return std::nullopt;
        input = &native_input;
    }
    // Python版と同様、ファイル全体を読んでから str.splitlines()/strip() を適用する。
    const String bytes{std::istreambuf_iterator<char>(*input), std::istreambuf_iterator<char>()};
    const String contents = sli_text_from_bytes(bytes);
    for (auto line : ::cw::lib::encoding::splitlines(contents)) {
        line = ::cw::lib::python_strip(line);
        if (line.empty() || line.front() == '#' || line.rfind("Link", 0) != 0) continue;
        const auto open = line.find('{');
        const auto close = line.rfind('}');
        if (open == String::npos || close == String::npos || close < open) continue;
        const auto body = line.substr(open + 1, close - open - 1);
        Integer loop_start = -1;
        Integer loop_end = -1;
        std::size_t begin = 0;
        while (begin <= body.size()) {
            const auto end = body.find(';', begin);
            const auto section = body.substr(begin, end == String::npos ? String::npos : end - begin);
            const auto text = ::cw::lib::python_strip(section);
            const auto parse_integer = [](const String& value) -> Integer {
                const auto normalized = ::cw::lib::normalize_python_integer(value);
                if (!normalized) throw std::invalid_argument("invalid .sli integer");
                return std::stoll(*normalized);
            };
            if (text.rfind("From=", 0) == 0) loop_end = parse_integer(text.substr(5));
            if (text.rfind("To=", 0) == 0) loop_start = parse_integer(text.substr(3));
            if (end == String::npos) break;
            begin = end + 1;
        }
        if (loop_start >= 0) {
            const auto start = loop_byte_position(loop_start, samples_per_byte, samples_to_bytes);
            const auto end = loop_end >= 0
                ? loop_byte_position(loop_end, samples_per_byte, samples_to_bytes) : -1;
            return std::tuple<Integer, Integer>{start, end};
        }
    }
    return std::nullopt;
} catch (const std::exception&) {
    // Python版と同様、各フィールドではなく .sli の読み込み・解析全体で例外を
    // 捕捉する。 不正な数値があればこのファイルを使わず、Oggタグの代替経路へ進む。
    ::cw::util::print_ex(std::nullopt);
    return std::nullopt;
}

#if defined(_WIN32)
std::optional<std::tuple<Integer, Integer>> loopinfo_from_stream(const String& path, NativeHandle stream)
{
    if (stream == 0 || g_bass_api.channel_get_info == nullptr) return std::nullopt;
    BassChannelInfoRaw info{};
    if (g_bass_api.channel_get_info(stream, &info) == 0 || info.freq == 0) return std::nullopt;
    const double samples_per_byte = 44100.0 / static_cast<double>(info.freq);
    Integer samples_to_bytes = static_cast<Integer>(info.chans);
    if (info.flags & static_cast<BassDword>(::cw::bassplayer::g_BASS_SAMPLE_FLOAT)) samples_to_bytes *= 4;
    else if (info.flags & static_cast<BassDword>(::cw::bassplayer::g_BASS_SAMPLE_8BITS)) samples_to_bytes *= 1;
    else samples_to_bytes *= 2;
    if (const auto loop = read_sli(path, samples_per_byte, samples_to_bytes)) return loop;
    if (g_bass_api.channel_get_tags == nullptr) return std::nullopt;
    const auto* tags = g_bass_api.channel_get_tags(stream, static_cast<BassDword>(::cw::bassplayer::g_BASS_TAG_OGG));
    if (tags == nullptr) return std::nullopt;
    Integer loop_start = -1;
    Integer loop_length = -1;
    while (*tags) {
        const std::string_view comment(tags);
        const auto parse_integer = [](std::string_view bytes) -> Integer {
            const auto normalized = ::cw::lib::normalize_python_integer_bytes(std::string(bytes));
            if (!normalized) throw std::invalid_argument("invalid Ogg loop integer");
            return std::stoll(*normalized);
        };
        // .sli の代替処理と異なり、Python版と同様、不正なOgg数値は例外を伝える。 数値の先
        // 頭部分やUnicodeの int(str) ではなく、int(bytes) の規則で検証する。
        if (comment.rfind("LOOPSTART=", 0) == 0) loop_start = parse_integer(comment.substr(10));
        if (comment.rfind("LOOPLENGTH=", 0) == 0) loop_length = parse_integer(comment.substr(11));
        tags += comment.size() + 1;
    }
    if (loop_start < 0) return std::nullopt;
    const auto start = loop_byte_position(loop_start, samples_per_byte, samples_to_bytes);
    const auto end = loop_length >= 0
        ? loop_byte_position(static_cast<std::uint64_t>(loop_start) + static_cast<std::uint64_t>(loop_length),
                             samples_per_byte, samples_to_bytes) : -1;
    return std::tuple<Integer, Integer>{start, end};
}

std::optional<Integer> midi_cc111_position(NativeHandle stream)
{
    if (stream == 0 || g_bass_api.midi_stream_get_events == nullptr) return std::nullopt;
    const auto count = g_bass_api.midi_stream_get_events(stream, -1,
        static_cast<BassDword>(::cw::bassplayer::g_MIDI_EVENT_CONTROL), nullptr);
    if (count == 0 || count == static_cast<BassDword>(-1)) return std::nullopt;
    std::vector<BassMidiEventRaw> events(count);
    const auto filled = g_bass_api.midi_stream_get_events(stream, -1,
        static_cast<BassDword>(::cw::bassplayer::g_MIDI_EVENT_CONTROL), events.data());
    if (filled == 0 || filled == static_cast<BassDword>(-1)) return std::nullopt;
    // Python版の不具合への意図的な修正。BASS_MIDI_StreamGetEvents が返す
    // のはバイト長ではなくイベント数。 Python版の range(0, count, 20) は後の
    // 記録を飛ばすため、全イベントを調べ、後方のCC111ループ印も見つける。 参照: un4seen.com/doc/bassmidi/BASS_MIDI_StreamGetEvents.html
    //
    const auto n = std::min(filled, static_cast<BassDword>(events.size()));
    for (BassDword i = 0; i < n; ++i) {
        if ((events[i].param & 0xffu) == static_cast<BassDword>(::cw::bassplayer::g_CC111))
            return static_cast<Integer>(events[i].pos);
    }
    return std::nullopt;
}
#endif

std::vector<std::uint8_t> encode_utf16(const String& fpath)
{
    std::vector<std::uint8_t> result;
    try {
        std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> converter;
        const auto text = converter.from_bytes(fpath);
        result.reserve((text.size() + 1) * 2);
        for (const auto ch : text) {
            result.push_back(static_cast<std::uint8_t>(ch & 0xff));
            result.push_back(static_cast<std::uint8_t>((ch >> 8) & 0xff));
        }
    } catch (const std::exception&) {
        // 変換不能な入力でもNUL終端を含むバイト列を返す。
        for (const auto ch : fpath) { result.push_back(static_cast<std::uint8_t>(ch)); result.push_back(0); }
    }
    result.push_back(0);
    result.push_back(0);
    if constexpr (std::endian::native == std::endian::big) {
        for (std::size_t i = 0; i + 1 < result.size(); i += 2) std::swap(result[i], result[i + 1]);
    }
    return result;
}

#if defined(_WIN32)
std::wstring wide_path(const String& path)
{
    try {
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
        return converter.from_bytes(path);
    } catch (const std::exception&) {
        return {};
    }
}

std::runtime_error bass_failure(const char* operation, const String& path)
{
    const auto code = g_bass_api.error_get_code == nullptr ? 0 : g_bass_api.error_get_code();
    std::ostringstream message;
    // Python版と同様、入力パスとBASSのエラーコードを残す。
    // 追加のネイティブ操作名で、失敗した所有権・設定の段階を示す。
    message << operation << ": " << path << ", " << code;
    return std::runtime_error(message.str());
}

bool native_set_fonts(NativeHandle stream)
{
    if (stream == 0 || g_bass_api.midi_stream_set_fonts == nullptr || g_font_handles.empty()) return false;
    std::vector<BassMidiFontRaw> fonts;
    fonts.reserve(g_font_handles.size());
    for (const auto handle : g_font_handles) fonts.push_back(BassMidiFontRaw{handle, -1, 0});
    return g_bass_api.midi_stream_set_fonts(stream, fonts.data(), static_cast<BassDword>(fonts.size())) != 0;
}

void native_release(NativeHandle native, NativeHandle source, NativeHandle end_sync, NativeHandle slide_sync)
{
    if (native == 0) {
        if (source != 0 && g_bass_api.stream_free != nullptr) g_bass_api.stream_free(source);
        return;
    }
    if (g_bass_api.channel_remove_sync != nullptr) {
        if (end_sync != 0) g_bass_api.channel_remove_sync(native, end_sync);
        if (slide_sync != 0) g_bass_api.channel_remove_sync(native, slide_sync);
    }
    if (g_bass_api.channel_stop != nullptr) g_bass_api.channel_stop(native);
    if (g_bass_api.stream_free != nullptr) g_bass_api.stream_free(native);
    if (source != 0 && source != native && g_bass_api.stream_free != nullptr) g_bass_api.stream_free(source);
}

NativeHandle native_create_stream(const String& path, bool midi, BassDword flags, BassDword frequency,
                                  const std::shared_ptr<std::vector<std::byte>>& encoded_data)
{
    if (encoded_data) {
        const void* bytes = encoded_data->empty() ? nullptr : encoded_data->data();
        const auto actual_flags = flags & ~static_cast<BassDword>(::cw::bassplayer::g_BASS_UNICODE);
        if (midi) {
            if (g_bass_api.midi_stream_create_file == nullptr) return 0;
            return g_bass_api.midi_stream_create_file(1, bytes, 0,
                static_cast<BassQword>(encoded_data->size()), actual_flags, frequency);
        }
        if (g_bass_api.stream_create_file == nullptr) return 0;
        return g_bass_api.stream_create_file(1, bytes, 0,
            static_cast<BassQword>(encoded_data->size()), actual_flags);
    }
    const auto wide = wide_path(path);
    const void* file = wide.empty() ? static_cast<const void*>(path.c_str()) : static_cast<const void*>(wide.c_str());
    const auto actual_flags = wide.empty() ? flags : flags | static_cast<BassDword>(::cw::bassplayer::g_BASS_UNICODE);
    if (midi) {
        if (g_bass_api.midi_stream_create_file == nullptr) return 0;
        return g_bass_api.midi_stream_create_file(0, file, 0, 0, actual_flags, frequency);
    }
    if (g_bass_api.stream_create_file == nullptr) return 0;
    return g_bass_api.stream_create_file(0, file, 0, 0, actual_flags);
}

NativeHandle native_font_init(const String& path)
{
    if (g_bass_api.midi_font_init == nullptr) return 0;
    const auto wide = wide_path(path);
    if (!wide.empty()) return g_bass_api.midi_font_init(wide.c_str(), static_cast<BassDword>(::cw::bassplayer::g_BASS_UNICODE));
    return g_bass_api.midi_font_init(path.c_str(), 0);
}

Integer logical_for_native(Integer index, NativeHandle native)
{
    const auto& fade = g_fadeout_state[static_cast<std::size_t>(index)];
    if (fade && fade->native == native) return fade->handle;
    const auto& state = g_stream_state[static_cast<std::size_t>(index)];
    if (state.native == native) return state.handle;
    return 0;
}

void native_loop_callback(NativeHandle sync, BassDword channel, BassDword data, void* user)
{
    CallbackLease callback;
    if (!callback) return;
    (void)sync;
    (void)data;
    const auto raw = reinterpret_cast<std::uintptr_t>(user);
    if (raw == 0) return;
    const auto index = static_cast<Integer>(raw - 1);
    if (!valid_index(index)) return;
    Integer logical = 0;
    {
        std::scoped_lock lock(g_stream_mutex);
        logical = logical_for_native(index, channel);
    }
    if (logical == 0) return;
    ::cw::bassplayer::_loop(Value(logical), Value(logical), Value(0), index);
}

void native_free_callback(NativeHandle sync, BassDword channel, BassDword data, void* user)
{
    CallbackLease callback;
    if (!callback) return;
    // SLIDE同期はチャンネル全体に作用するため、該当する自身の音量フェードだけに解放を許可する。
    if (data != static_cast<BassDword>(::cw::bassplayer::g_BASS_ATTRIB_VOL)) return;
    const auto raw = reinterpret_cast<std::uintptr_t>(user);
    if (raw == 0) return;
    const auto index = static_cast<Integer>(raw - 1);
    if (!valid_index(index)) return;
    NativeHandle native = 0, source = 0, end_sync = 0, slide_sync = 0;
    std::shared_ptr<std::vector<std::byte>> encoded_data;
    {
        std::scoped_lock lock(g_stream_mutex);
        auto& fade = g_fadeout_state[static_cast<std::size_t>(index)];
        if (!fade || fade->native != channel || sync == 0 || fade->slide_sync != sync) return;
        if (fade->slide_arming) {
            // 同期的なコールバックでも、ChannelSlideAttribute に
            // 渡している最中のハンドルは解放しない。 この完了は呼び出し元が処理する。
            fade->slide_completed = true;
            return;
        }
        const auto logical = fade->handle;
        take_release_targets(index, logical, native, source, end_sync, slide_sync, encoded_data);
    }
    native_release(native, source, end_sync, slide_sync);
}
#endif
}

namespace cw::bassplayer {
::cw::lib::Integer g_BASS_ACTIVE_STOPPED{0};
::cw::lib::Integer g_BASS_ATTRIB_EAXMIX{4};
::cw::lib::Integer g_BASS_ATTRIB_FREQ{1};
::cw::lib::Integer g_BASS_ATTRIB_MUSIC_AMPLIFY{0x100};
::cw::lib::Integer g_BASS_ATTRIB_MUSIC_BPM{0x103};
::cw::lib::Integer g_BASS_ATTRIB_MUSIC_PANSEP{0x101};
::cw::lib::Integer g_BASS_ATTRIB_MUSIC_PSCALER{0x102};
::cw::lib::Integer g_BASS_ATTRIB_MUSIC_SPEED{0x104};
::cw::lib::Integer g_BASS_ATTRIB_MUSIC_VOL_CHAN{0x200};
::cw::lib::Integer g_BASS_ATTRIB_MUSIC_VOL_GLOBAL{0x105};
::cw::lib::Integer g_BASS_ATTRIB_MUSIC_VOL_INST{0x300};
::cw::lib::Integer g_BASS_ATTRIB_PAN{3};
::cw::lib::Integer g_BASS_ATTRIB_TEMPO{0x10000};
::cw::lib::Integer g_BASS_ATTRIB_TEMPO_FREQ{0x10002};
::cw::lib::Integer g_BASS_ATTRIB_TEMPO_PITCH{0x10001};
::cw::lib::Integer g_BASS_ATTRIB_VOL{2};
::cw::lib::Integer g_BASS_DEFAULT{0};
::cw::lib::Integer g_BASS_DEVICE_3D{4};
::cw::lib::Integer g_BASS_DEVICE_8BITS{1};
::cw::lib::Integer g_BASS_DEVICE_DEFAULT{2};
::cw::lib::Integer g_BASS_DEVICE_ENABLED{1};
::cw::lib::Integer g_BASS_DEVICE_INIT{4};
::cw::lib::Integer g_BASS_DEVICE_MONO{2};
::cw::lib::Integer g_BASS_FILEPOS_CURRENT{0};
::cw::lib::Integer g_BASS_FILEPOS_END{2};
::cw::lib::Integer g_BASS_FX_FREESOURCE{0x10000};
::cw::lib::Integer g_BASS_MUSIC_POSRESET{0x8000};
::cw::lib::Integer g_BASS_MUSIC_PRESCAN{0x20000};
::cw::lib::Integer g_BASS_MUSIC_RAMP{0x200};
::cw::lib::Integer g_BASS_MUSIC_RAMPS{0x400};
::cw::lib::Integer g_BASS_MUSIC_STOPBACK{0x80000};
::cw::lib::Integer g_BASS_POS_BYTE{0};
::cw::lib::Integer g_BASS_SAMPLE_8BITS{1};
::cw::lib::Integer g_BASS_SAMPLE_FLOAT{256};
::cw::lib::Integer g_BASS_SAMPLE_LOOP{4};
::cw::lib::Integer g_BASS_STREAM_DECODE{0x200000};
::cw::lib::Integer g_BASS_SYNC_END{2};
::cw::lib::Integer g_BASS_SYNC_MIXTIME{0x40000000};
::cw::lib::Integer g_BASS_SYNC_MUSICPOS{10};
::cw::lib::Integer g_BASS_SYNC_POS{0};
::cw::lib::Integer g_BASS_SYNC_SLIDE{5};
::cw::lib::Integer g_BASS_TAG_OGG{2};
::cw::lib::Integer g_BASS_TAG_RIFF_BEXT{0x101};
::cw::lib::Integer g_BASS_TAG_RIFF_CART{0x102};
::cw::lib::Integer g_BASS_TAG_RIFF_DISP{0x103};
::cw::lib::Integer g_BASS_TAG_RIFF_INFO{0x100};
::cw::lib::Integer g_BASS_UNICODE{0x80000000LL};
::cw::lib::Integer g_CC111{111};
::cw::lib::Value g_CC111LOOP{};
::cw::lib::Value g_FREE_CHANNEL{};
::cw::lib::Integer g_MAX_BGM_CHANNELS{2};
::cw::lib::Integer g_MAX_SOUND_CHANNELS{2};
::cw::lib::Integer g_MIDI_EVENT_CONTROL{64};
::cw::lib::Integer g_MIDI_EVENT_END{0};
::cw::lib::Integer g_MIDI_EVENT_END_TRACK{0x10003};
::cw::lib::Integer g_STREAM_BGM{0};
::cw::lib::Integer g_STREAM_SOUND1{2};
::cw::lib::Integer g_STREAM_SOUND2{4};
::cw::lib::Value g_SYNCPROC{};
std::optional<::cw::lib::Value> g__bass{};
std::optional<::cw::lib::Value> g__bassfx{};
std::optional<::cw::lib::Value> g__bassmidi{};
::cw::lib::Value g__fadeoutlock{};
std::vector<std::optional<std::tuple<::cw::lib::Value, LoopCount, ::cw::lib::Integer>>> g__fadeoutstreams(kStreamCount);
::cw::lib::Value g__lock{};
std::vector<LoopCount> g__loopcounts{0, 1, 1, 1, 1};
std::vector<::cw::lib::Integer> g__loopstarts(kStreamCount, 0);
bool g__paused{};
::cw::lib::Bytes g__sfonts{};
std::vector<::cw::lib::Value> g__streams(kStreamCount, ::cw::lib::Value(::cw::lib::Integer(0)));
::cw::lib::Value g_c_BOOL{};
::cw::lib::Value g_c_BYTE{};
::cw::lib::Value g_c_DWORD{};
::cw::lib::Value g_c_HCHANNEL{};
::cw::lib::Value g_c_HMUSIC{};
::cw::lib::Value g_c_HPLUGIN{};
::cw::lib::Value g_c_HSAMPLE{};
::cw::lib::Value g_c_HSOUNDFONT{};
::cw::lib::Value g_c_HSTREAM{};
::cw::lib::Value g_c_HSYNC{};
::cw::lib::Value g_c_QWORD{};
::cw::lib::Value g_c_WORD{};

void _cc111loop(::cw::lib::Value arg_handle, ::cw::lib::Value arg_channel, ::cw::lib::Value arg_data, std::optional<::cw::lib::Integer> arg_streamindex)
{
    _loop(std::move(arg_handle), std::move(arg_channel), std::move(arg_data), arg_streamindex);
}

void _free_channel(std::optional<::cw::lib::Value> arg_handle, ::cw::lib::Value arg_channel, ::cw::lib::Value arg_data, std::optional<::cw::lib::Integer> arg_streamindex)
{
    // BASSのメモリストリームは、StreamFree が戻るまで入力バッファを保持する。
    std::shared_ptr<std::vector<std::byte>> encoded_data;
    (void)arg_handle;
    (void)arg_data;
    const auto index = optional_index(arg_streamindex);
    const auto channel = stream_handle(arg_channel);
    if (!valid_index(index) || channel == 0) return;
    NativeHandle native = 0;
    NativeHandle source = 0;
    NativeHandle end_sync = 0;
    NativeHandle slide_sync = 0;
    {
        std::scoped_lock lock(g_stream_mutex);
        take_release_targets(index, channel, native, source, end_sync, slide_sync, encoded_data);
    }
#if defined(_WIN32)
    native_release(native, source, end_sync, slide_sync);
#else
    (void)native;
    (void)source;
    (void)end_sync;
    (void)slide_sync;
#endif
}

void _free_channel_func(::cw::lib::Integer arg_streamindex)
{
    if (!valid_index(arg_streamindex)) return;
    const auto stream = stream_handle_at(arg_streamindex);
    _free_channel(std::nullopt, ::cw::lib::Value(stream), ::cw::lib::Value(0), arg_streamindex);
}

void _free_channel_lockfree(std::optional<::cw::lib::Value> arg_handle, ::cw::lib::Value arg_channel, ::cw::lib::Value arg_data, std::optional<::cw::lib::Integer> arg_streamindex)
{
    std::shared_ptr<std::vector<std::byte>> encoded_data;
    (void)arg_handle;
    (void)arg_data;
    const auto index = optional_index(arg_streamindex);
    const auto channel = stream_handle(arg_channel);
    if (!valid_index(index) || channel == 0) return;
    NativeHandle native = 0;
    NativeHandle source = 0;
    NativeHandle end_sync = 0;
    NativeHandle slide_sync = 0;
    {
        std::scoped_lock lock(g_stream_mutex);
        take_release_targets(index, channel, native, source, end_sync, slide_sync, encoded_data);
    }
#if defined(_WIN32)
    native_release(native, source, end_sync, slide_sync);
#else
    (void)native;
    (void)source;
    (void)end_sync;
    (void)slide_sync;
#endif
}

void _loop(::cw::lib::Value arg_handle, ::cw::lib::Value arg_channel, ::cw::lib::Value arg_data, std::optional<::cw::lib::Integer> arg_streamindex)
{
    (void)arg_handle;
    (void)arg_data;
    const auto index = optional_index(arg_streamindex);
    const auto channel = stream_handle(arg_channel);
    if (!valid_index(index) || channel == 0) return;
    std::scoped_lock lock(g_stream_mutex);
    auto& fade = g__fadeoutstreams[static_cast<std::size_t>(index)];
    bool fadeouting = false;
    LoopCount loops = 0;
    Integer position = 0;
    if (fade && stream_handle(std::get<0>(*fade)) == channel) {
        fadeouting = true;
        loops = std::get<1>(*fade);
        position = std::get<2>(*fade);
    } else {
        // native_loop_callback は別のロック下で論理ハン
        // ドルを解決する。 こちらのロック取得前に再生が置き換わる場合があ
        // るため、 古いコールバックで新しい再生のカウンターを消費しない。
        if (g_stream_state[static_cast<std::size_t>(index)].handle != channel) return;
        loops = g__loopcounts[static_cast<std::size_t>(index)];
        position = g__loopstarts[static_cast<std::size_t>(index)];
    }
    if (loops != 1) {
        if (loops > 0) {
            if (fadeouting) {
                fade = std::tuple<::cw::lib::Value, LoopCount, Integer>{::cw::lib::Value(channel), loops - 1, position};
                if (auto& fade_state = g_fadeout_state[static_cast<std::size_t>(index)]; fade_state)
                    fade_state->loops = loops - 1;
            } else {
                g__loopcounts[static_cast<std::size_t>(index)] = loops - 1;
            }
        }
        NativeHandle native = 0;
        auto& state = g_stream_state[static_cast<std::size_t>(index)];
        if (fadeouting) {
            if (const auto& fade_state = g_fadeout_state[static_cast<std::size_t>(index)];
                fade_state && fade_state->handle == channel) native = fade_state->native;
        } else if (state.handle == channel) {
            state.position = position;
            native = state.native;
        }
#if defined(_WIN32)
        if (native != 0 && g_bass_api.channel_set_position != nullptr) {
            g_bass_api.channel_set_position(native, static_cast<BassQword>(std::max<Integer>(0, position)),
                static_cast<BassDword>(::cw::bassplayer::g_BASS_POS_BYTE));
        }
#endif
    }
}

// BASSの可否と、MIDI全体の再生可否は別。ネイティブBASS
// 接続はWindows専用だが、 共通のTinySoundFont
// 経路は一つのSF2とトラック音量でMIDIを再生する。
// 複数の設定済みサウンドフォント・個別音量やBASSの
// .sli 情報は使わず、CC111ループは別に扱う。
bool is_alivable()
{
#if defined(_WIN32)
    return g_bass_api.initialized && g__bass.has_value() && !g__bass->is_none();
#else
    return false;
#endif
}

// このBASS機能検査は、Python版のモジュール・フォントの真偽判定よ
// り厳密にする。 コアの初期化と呼び出し可能なMIDI関数も要求し、 読み
// 込み済みでも使えないネイティブAPI表でMIDI復号準備完了としない。
bool is_alivablemidi()
{
#if defined(_WIN32)
    return is_alivable() && g_bass_api.midi_stream_create_file != nullptr &&
        g_bass_api.midi_font_init != nullptr && g_bass_api.midi_stream_set_fonts != nullptr &&
        g__bassmidi.has_value() && !g__bassmidi->is_none() && !g__sfonts.empty();
#else
    return false;
#endif
}

bool is_alivablewithpath(::cw::lib::String arg_path)
{
    // Python版と同様、機能の問い合わせには共通の拡張子による代替を使うが、 _play
    // がMIDIデコーダーを選ぶには、意図的に実際のMIDIヘッダーを要求する。
    return ::cw::util::is_midi(arg_path) ? is_alivablemidi() : is_alivable();
}

::cw::lib::Bytes _encode_fpath(::cw::lib::String arg_fpath)
{
    return encode_utf16(arg_fpath);
}

// ネイティブの初期化は、生のモジュールハンドルと関数ポインター表を所有する。 必
// 須シンボルがなければ、部分的な表を解放・リセットしてからfalseを返し、
// 再試行が解放済みバックエンドのポインターを使わないようにする。 コアの初期化
// 失敗でも、Python版が BASS_Init 失敗時に dispose_bass
// を呼ぶのと同様に破棄する。 後のフォント設定失敗は下で別に処理する。
bool init_bass(std::vector<std::tuple<::cw::lib::String, ::cw::lib::Real>> arg_soundfonts)
{
    if (is_alivable()) return true;
#if !defined(_WIN32)
    (void)arg_soundfonts;
    return false;
#else
    std::scoped_lock backend_lock(g_audio_backend_mutex);
    if (!g_bass_api.load()) return false;
    if (g_bass_api.tempo_create == nullptr || g_bass_api.midi == nullptr ||
        g_bass_api.midi_stream_create_file == nullptr) {
        g_bass_api.unload();
        return false;
    }
    if (g_bass_api.init(-1, 44100, 0, nullptr, nullptr) == 0) {
        g_bass_api.unload();
        return false;
    }
    g_bass_api.initialized = true;
    g__bass = ::cw::lib::Value(String("bass.dll"));
    if (g_bass_api.midi != nullptr) g__bassmidi = ::cw::lib::Value(String("bassmidi.dll"));
    if (g_bass_api.fx != nullptr && g_bass_api.tempo_create != nullptr) g__bassfx = ::cw::lib::Value(String("bass_fx.dll"));
    g__sfonts.clear();
    g_soundfonts.clear();
    g_font_handles.clear();
    // 後のフォント・音量の設定失敗では、作成済みフォントを解放し、利用可
    // 否・モジュールを消す。 Python版の一部を残す読み込み状態とは
    // 異なり、ネイティブ版は後の再生へ半初期化のフォント群を公開しない。
    const auto cleanup = [&]() {
        for (const auto font : g_font_handles) if (g_bass_api.midi_font_free != nullptr) g_bass_api.midi_font_free(font);
        g_font_handles.clear();
        g_soundfonts.clear();
        g__sfonts.clear();
        g_bass_api.unload();
        g__bass.reset();
        g__bassmidi.reset();
        g__bassfx.reset();
    };
    if (arg_soundfonts.empty()) {
        cleanup();
        return false;
    }
    for (const auto& [path, volume] : arg_soundfonts) {
        if (!is_regular_file(path)) {
            std::cout << "BASS_MIDI_FontInit() failure: " << path << std::endl;
            cleanup();
            return false;
        }
        const auto font = native_font_init(path);
        const bool font_created = font != 0;
        if (!font_created || g_bass_api.midi_font_set_volume == nullptr ||
            g_bass_api.midi_font_set_volume(font, static_cast<float>(normalize_volume(volume))) == 0) {
            // ネイティブの後処理前に、Python版と同じ失敗通知を行う。
            if (!font_created) std::cout << "BASS_MIDI_FontInit() failure: " << path << std::endl;
            else std::cout << "BASS_MIDI_FontSetVolume() failure: " << path << ", "
                           << ::cw::lib::python_float_repr(volume) << std::endl;
            if (font != 0 && g_bass_api.midi_font_free != nullptr) g_bass_api.midi_font_free(font);
            cleanup();
            return false;
        }
        g_font_handles.push_back(font);
        g_soundfonts.emplace_back(path, normalize_volume(volume));
        for (int byte = 0; byte < 4; ++byte) g__sfonts.push_back(static_cast<std::uint8_t>((font >> (byte * 8)) & 0xff));
        for (int byte = 0; byte < 8; ++byte) g__sfonts.push_back(0);
    }
    if (g__sfonts.empty()) {
        cleanup();
        return false;
    }
    g__paused = false;
    g_current_device = 0;
    g_next_handle.store(1);
    for (std::size_t i = 0; i < kStreamCount; ++i) {
        g_stream_state[i] = StreamState{};
        g_fadeout_state[i] = std::nullopt;
        g_stream_volume[i] = 0.0;
        g__streams[i] = ::cw::lib::Value(Integer(0));
        g__fadeoutstreams[i] = std::nullopt;
        g__loopstarts[i] = 0;
        g__loopcounts[i] = i == 0 ? 0 : 1;
    }
    g_callbacks_stopping.store(false, std::memory_order_release);
    return true;
#endif
}

// ネイティブ版は、フォント変更を適用できるMIDIバックエンドがなければfalse
// を返す。 Python版はMIDIモジュールがなくても何もせずtrue
// を返す。 置換後は現在枠のネイティブMIDIストリームを新しいフォン
// ト表へ接続し直すが、フェードアウト表へ移ったチャンネルは対象外。 Python
// 版はこの明示的な再接続をせず、フォント表だけを置き換える。
bool change_soundfonts(std::vector<std::tuple<::cw::lib::String, ::cw::lib::Real>> arg_soundfonts)
{
#if !defined(_WIN32)
    (void)arg_soundfonts;
    return false;
#else
    std::scoped_lock backend_lock(g_audio_backend_mutex);
    if (!is_alivablemidi()) return false;
    // Python版は既存FontFree後に新しいFontInitを順に行い、途中失敗ならFalseを返す。
    for (const auto font : g_font_handles) if (g_bass_api.midi_font_free != nullptr) g_bass_api.midi_font_free(font);
    g_font_handles.clear();
    g__sfonts.clear();
    g_soundfonts.clear();
    for (const auto& [path, volume] : arg_soundfonts) {
        if (!is_regular_file(path)) {
            std::cout << "BASS_MIDI_FontInit() failure: " << path << std::endl;
            return false;
        }
        const auto font = native_font_init(path);
        const bool font_created = font != 0;
        if (!font_created || g_bass_api.midi_font_set_volume == nullptr ||
            g_bass_api.midi_font_set_volume(font, static_cast<float>(normalize_volume(volume))) == 0) {
            // ネイティブの後処理前に、Python版と同じ失敗通知を行う。
            if (!font_created) std::cout << "BASS_MIDI_FontInit() failure: " << path << std::endl;
            else std::cout << "BASS_MIDI_FontSetVolume() failure: " << path << ", "
                           << ::cw::lib::python_float_repr(volume) << std::endl;
            if (font != 0 && g_bass_api.midi_font_free != nullptr) g_bass_api.midi_font_free(font);
            return false;
        }
        g_font_handles.push_back(font);
        g_soundfonts.emplace_back(path, normalize_volume(volume));
        for (int byte = 0; byte < 4; ++byte) g__sfonts.push_back(static_cast<std::uint8_t>((font >> (byte * 8)) & 0xff));
        for (int byte = 0; byte < 8; ++byte) g__sfonts.push_back(0);
    }
    if (g__sfonts.empty()) return false;
    for (const auto& state : g_stream_state) {
        if (state.midi && state.native != 0 && !native_set_fonts(state.native)) return false;
    }
    return true;
#endif
}

bool pause()
{
    if (!is_alivable()) return false;
#if defined(_WIN32)
    std::scoped_lock backend_lock(g_audio_backend_mutex);
    if (g_bass_api.channel_pause == nullptr) return false;
#endif
    std::scoped_lock lock(g_stream_mutex);
    g__paused = true;
    // ALL はBASS機器をエディタと共有する。エンジン所有のストリームだけを一時
    // 停止し、 前面のエディタまで無音にする BASS_Pause() は使わない。
    for (auto& state : g_stream_state) if (state.handle != 0 && state.playing) {
#if defined(_WIN32)
        if (state.native && g_bass_api.channel_pause(state.native)) state.paused = true;
#else
        state.paused = true;
#endif
    }
#if defined(_WIN32)
    for (auto& fade : g_fadeout_state) if (fade && fade->native &&
        g_bass_api.channel_pause(fade->native)) fade->paused = true;
#endif
    return true;
}

bool start()
{
    if (!is_alivable()) return false;
#if defined(_WIN32)
    std::scoped_lock backend_lock(g_audio_backend_mutex);
#endif
    _switch_device();
#if defined(_WIN32)
    if (g_bass_api.start == nullptr || g_bass_api.start() == 0) return false;
#endif
    std::scoped_lock lock(g_stream_mutex);
    g__paused = false;
    for (auto& state : g_stream_state) if (state.handle != 0 && state.playing && state.paused) {
#if defined(_WIN32)
        if (state.native && g_bass_api.channel_play(state.native, 0) == 0) continue;
#endif
        state.paused = false;
    }
#if defined(_WIN32)
    for (auto& fade : g_fadeout_state) if (fade && fade->paused) {
        if (fade->native) g_bass_api.channel_play(fade->native, 0);
        fade->paused = false;
    }
#endif
    return true;
}

void _switch_device()
{
    if (!is_alivable()) return;
#if defined(_WIN32)
    std::scoped_lock backend_lock(g_audio_backend_mutex);
    if (g_bass_api.get_device == nullptr || g_bass_api.set_device == nullptr) return;
    auto selected = g_bass_api.get_device();
    if (g_bass_api.get_device_info != nullptr) {
        // 既定機器の探索は64位置までに制限し、列挙終端を返さ
        // ないバックエンドでも無限に回らないようにする。 Python
        // 版は GetDeviceInfo 失敗まで調べる
        // が、ネイティブ版では64以上の既定機器は探索外。
        // また範囲内の情報取得失敗は飛ばして後続も調べ、
        // 最初の失敗で止まるPython版とは区別する。
        for (BassDword device = 0; device < 64; ++device) {
            BassDeviceInfoRaw info{};
            if (g_bass_api.get_device_info(device, &info) == 0) continue;
            if ((info.flags & static_cast<BassDword>(::cw::bassplayer::g_BASS_DEVICE_ENABLED)) != 0 &&
                (info.flags & static_cast<BassDword>(::cw::bassplayer::g_BASS_DEVICE_DEFAULT)) != 0) {
                if ((info.flags & static_cast<BassDword>(::cw::bassplayer::g_BASS_DEVICE_INIT)) == 0 &&
                    g_bass_api.init != nullptr) {
                    g_bass_api.init(static_cast<int>(device), 44100, static_cast<BassDword>(::cw::bassplayer::g_BASS_DEFAULT),
                        nullptr, nullptr);
                }
                selected = device;
                break;
            }
        }
    }
    if (g_bass_api.set_device(selected) == 0) return;
    g_current_device = static_cast<Integer>(selected);
    std::scoped_lock lock(g_stream_mutex);
    if (g_bass_api.channel_set_device != nullptr) {
        for (const auto& state : g_stream_state) if (state.native != 0) g_bass_api.channel_set_device(state.native, selected);
        for (const auto& state : g_fadeout_state)
            if (state && state->native != 0) g_bass_api.channel_set_device(state->native, selected);
    }
#endif
}

::cw::lib::Value _play(::cw::lib::String arg_fpath, ::cw::lib::Real arg_volume, ::cw::lib::Integer arg_loopcount, ::cw::lib::Integer arg_streamindex, ::cw::lib::Integer arg_fade, ::cw::lib::Integer arg_tempo, ::cw::lib::Integer arg_pitch)
{
    if (!is_alivable()) return ::cw::lib::Value(Integer(0));
    if (!valid_index(arg_streamindex)) throw std::out_of_range("bassplayer streamindex");
    const bool ismidi = has_midi_header(arg_fpath);
    if (ismidi && !is_alivablemidi()) return ::cw::lib::Value(Integer(0));
    if (!ismidi && (!g__bass.has_value() || g__bass->is_none())) return ::cw::lib::Value(Integer(0));
    const Real volume = normalize_volume(arg_volume);
#if !defined(_WIN32)
    (void)arg_loopcount;
    (void)arg_fade;
    (void)arg_tempo;
    (void)arg_pitch;
    return ::cw::lib::Value(Integer(0));
#else
    std::scoped_lock backend_lock(g_audio_backend_mutex);
    _switch_device();
    BassDword flags = static_cast<BassDword>(g_BASS_MUSIC_STOPBACK | g_BASS_MUSIC_POSRESET |
        g_BASS_MUSIC_PRESCAN | g_BASS_UNICODE | g_BASS_STREAM_DECODE);
    if (bassmidi_sample32bit()) flags |= static_cast<BassDword>(g_BASS_SAMPLE_FLOAT);
    if (g_bass_api.tempo_create == nullptr) return ::cw::lib::Value(Integer(0));
    std::shared_ptr<std::vector<std::byte>> encoded_data;
    // ネイティブハンドルを取得する前に、確保を伴う文字列複製を準備する
    // 。 下の StreamState の反映では、確保なしで移す。
    String playback_path = arg_fpath;
    const auto source_path = std::filesystem::u8path(arg_fpath);
    if (::cardwirth::platform::is_mounted_user_path(source_path)) {
        auto bytes = ::cardwirth::platform::read_mounted_user_file(source_path);
        if (!bytes) throw std::runtime_error("_play() encrypted read failure: " + arg_fpath);
        encoded_data = std::make_shared<std::vector<std::byte>>(std::move(*bytes));
    }
    auto source_native = native_create_stream(arg_fpath, ismidi, flags, 44100, encoded_data);
    if (source_native == 0) throw bass_failure("_play() failure", arg_fpath);
    if (ismidi && !native_set_fonts(source_native)) {
        const auto failure = bass_failure(g_font_handles.empty() ? "sound font not found" : "sound font failure", arg_fpath);
        native_release(0, source_native, 0, 0);
        throw failure;
    }
    std::optional<std::tuple<Integer, Integer>> loopinfo;
    try {
        if (arg_loopcount != 1) loopinfo = loopinfo_from_stream(arg_fpath, source_native);
        if (!loopinfo && ismidi) {
            if (const auto position = midi_cc111_position(source_native))
                loopinfo = std::tuple<Integer, Integer>{*position, -1};
        }
    } catch (...) {
        // 付加情報のエラーは外へ伝えるが、未所有の復号ストリームを漏らさない。
        native_release(0, source_native, 0, 0);
        throw;
    }
    auto native = source_native;
    {
        // Python版と同様、テンポ変更ストリームが復号
        // 元を所有・解放する。 二つを独立して解放すると
        // 、BASS_FXのループ・シークの所有規則と合
        // わないストリームにEND同期が残る場合がある。
        native = g_bass_api.tempo_create(
            source_native, static_cast<BassDword>(g_BASS_FX_FREESOURCE));
        if (native == 0) {
            const auto failure = bass_failure("BASS_FX_TempoCreate", arg_fpath);
            native_release(source_native, 0, 0, 0);
            throw failure;
        }
        source_native = 0; // BASS_FX_FREESOURCE により所有権を移す。
    }
    if (g_bass_api.channel_set_attribute == nullptr ||
        g_bass_api.channel_set_attribute(native, static_cast<BassDword>(g_BASS_ATTRIB_VOL), 0.0F) == 0) {
        const auto failure = bass_failure("BASS_ChannelSetAttribute", arg_fpath);
        native_release(native, source_native, 0, 0);
        throw failure;
    }
    if (arg_tempo != 0 && g_bass_api.channel_set_attribute(native, static_cast<BassDword>(g_BASS_ATTRIB_TEMPO),
        static_cast<float>(arg_tempo)) == 0) {
        const auto failure = bass_failure("BASS_ChannelSetAttribute(TEMPO)", arg_fpath);
        native_release(native, source_native, 0, 0);
        throw failure;
    }
    if (arg_pitch != 0 && g_bass_api.channel_set_attribute(native, static_cast<BassDword>(g_BASS_ATTRIB_TEMPO_PITCH),
        static_cast<float>(arg_pitch)) == 0) {
        const auto failure = bass_failure("BASS_ChannelSetAttribute(TEMPO_PITCH)", arg_fpath);
        native_release(native, source_native, 0, 0);
        throw failure;
    }
    const auto handle = g_next_handle.fetch_add(1);
    NativeHandle end_sync = 0;
    if (g_bass_api.channel_set_sync == nullptr) {
        const auto failure = bass_failure("BASS_ChannelSetSync", arg_fpath);
        native_release(native, source_native, 0, 0);
        throw failure;
    }
    const auto callback_user = reinterpret_cast<void*>(static_cast<std::uintptr_t>(arg_streamindex + 1));
    if (loopinfo && std::get<1>(*loopinfo) >= 0) {
        end_sync = g_bass_api.channel_set_sync(native,
            static_cast<BassDword>(g_BASS_SYNC_POS | g_BASS_SYNC_MIXTIME),
            static_cast<BassQword>(std::max<Integer>(0, std::get<1>(*loopinfo))), native_loop_callback, callback_user);
    } else {
        end_sync = g_bass_api.channel_set_sync(native,
            static_cast<BassDword>(g_BASS_SYNC_END | g_BASS_SYNC_MIXTIME), 0, native_loop_callback, callback_user);
    }
    if (end_sync == 0) {
        const auto failure = bass_failure("BASS_ChannelSetSync", arg_fpath);
        native_release(native, source_native, 0, 0);
        throw failure;
    }
    {
        std::scoped_lock lock(g_stream_mutex);
        auto& state = g_stream_state[static_cast<std::size_t>(arg_streamindex)];
        state = StreamState{};
        state.handle = handle;
        state.path = std::move(playback_path);
        state.midi = ismidi;
        state.playing = true;
        state.paused = g__paused;
        state.volume = volume;
        state.fade = std::max<Integer>(0, arg_fade);
        state.tempo = arg_tempo;
        state.pitch = arg_pitch;
        state.flags = static_cast<Integer>(flags);
        state.source_native = source_native;
        state.native = native;
        state.end_sync = end_sync;
        state.encoded_data = std::move(encoded_data);
        state.loop_start = loopinfo ? std::get<0>(*loopinfo) : 0;
        state.loop_end = loopinfo ? std::get<1>(*loopinfo) : -1;
        // callbackが即時終了した場合も、呼び出し側のassign前に論理handleを参照できる。
        g__streams[static_cast<std::size_t>(arg_streamindex)] = ::cw::lib::Value(handle);
        g__loopcounts[static_cast<std::size_t>(arg_streamindex)] = playback_loop_count(arg_loopcount);
        g__loopstarts[static_cast<std::size_t>(arg_streamindex)] = state.loop_start;
        g_stream_volume[static_cast<std::size_t>(arg_streamindex)] = volume;
    }
    if (g__paused) {
        // エンジンが見えるまで、新規チャンネルは停止したままにする。
        // 機器全体を一時的に開始すると、背景の音声が漏れてしまう。
        if (arg_fade > 0) {
            if (g_bass_api.channel_slide_attribute == nullptr ||
                g_bass_api.channel_slide_attribute(native, static_cast<BassDword>(g_BASS_ATTRIB_VOL),
                    static_cast<float>(volume), static_cast<BassDword>(arg_fade)) == 0) {
                _free_channel(std::nullopt, Value(handle), Value(0), arg_streamindex);
                return Value(Integer(0));
            }
        } else if (g_bass_api.channel_set_attribute(native, static_cast<BassDword>(g_BASS_ATTRIB_VOL),
            static_cast<float>(volume)) == 0) {
            _free_channel(std::nullopt, Value(handle), Value(0), arg_streamindex);
            return Value(Integer(0));
        }
    } else if (arg_fade > 0) {
        if (g_bass_api.channel_slide_attribute == nullptr ||
            g_bass_api.channel_slide_attribute(native, static_cast<BassDword>(g_BASS_ATTRIB_VOL),
                static_cast<float>(volume), static_cast<BassDword>(arg_fade)) == 0 ||
            g_bass_api.channel_play(native, 0) == 0) {
            _free_channel(std::nullopt, Value(handle), Value(0), arg_streamindex);
            return Value(Integer(0));
        }
    } else {
        if (g_bass_api.channel_set_attribute(native, static_cast<BassDword>(g_BASS_ATTRIB_VOL),
            static_cast<float>(volume)) == 0 || g_bass_api.channel_play(native, 0) == 0) {
            _free_channel(std::nullopt, Value(handle), Value(0), arg_streamindex);
            return Value(Integer(0));
        }
    }
    return ::cw::lib::Value(handle);
#endif
}

::cw::lib::Real _get_attribute(::cw::lib::Value arg_stream, ::cw::lib::Value arg_flag)
{
    const auto handle = stream_handle(arg_stream);
    const auto flag = value_integer(arg_flag);
    if (handle == 0) return 0.0;
#if defined(_WIN32)
    std::scoped_lock backend_lock(g_audio_backend_mutex);
#endif
    std::scoped_lock lock(g_stream_mutex);
    for (const auto& state : g_stream_state) {
        if (state.handle != handle) continue;
#if defined(_WIN32)
        if (state.native != 0) {
            // Python版は c_float をゼロで初期化し、BASSの真
            // 偽の結果を無視して取得値を返す。 ネイティブの問い合わせが失敗し
            // ても、古い共通状態の音量・テンポ・音程キャッシュへ切り替えない。
            float result = 0.0F;
            if (g_bass_api.channel_get_attribute != nullptr)
                g_bass_api.channel_get_attribute(state.native, static_cast<BassDword>(flag), &result);
            return static_cast<Real>(result);
        }
#endif
        if (flag == g_BASS_ATTRIB_VOL) return state.volume;
        if (flag == g_BASS_ATTRIB_TEMPO) return static_cast<Real>(state.tempo);
        if (flag == g_BASS_ATTRIB_TEMPO_PITCH) return static_cast<Real>(state.pitch);
        if (flag == g_BASS_ATTRIB_TEMPO_FREQ || flag == g_BASS_ATTRIB_FREQ) return static_cast<Real>(state.frequency);
        // バックエンド非依存のこの状態では、BASS_ATTRIB_PANと未知の属性の既定値はゼロ。
        return 0.0;
    }
    return 0.0;
}

std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> _get_loopinfo(::cw::lib::String arg_fpath, ::cw::lib::Value arg_stream)
{
#if defined(_WIN32)
    std::scoped_lock backend_lock(g_audio_backend_mutex);
    // BASSのタグはストリーム内を指す。 ネイティブハンドルを
    // 得る間だけでなく、解析完了まで所有権をロックして保持する。
    std::scoped_lock state_lock(g_stream_mutex);
    NativeHandle native = 0;
    const auto handle = stream_handle(arg_stream);
    {
        for (const auto& state : g_stream_state) if (state.handle == handle) { native = state.native; break; }
        if (native == 0) {
            for (const auto& fade : g_fadeout_state) if (fade && fade->handle == handle) { native = fade->native; break; }
        }
    }
    if (native != 0) return loopinfo_from_stream(arg_fpath, native);
#else
    (void)arg_fpath;
    (void)arg_stream;
#endif
    return std::nullopt;
}

void set_loopcount(::cw::lib::Integer arg_loopcount, ::cw::lib::Integer arg_streamindex)
{
    if (!is_alivable() || !valid_index(arg_streamindex)) return;
#if defined(_WIN32)
    std::scoped_lock backend_lock(g_audio_backend_mutex);
#endif
    const auto stream = stream_handle_at(arg_streamindex);
    if (stream == 0) return;
    std::scoped_lock lock(g_stream_mutex);
    auto& state = g_stream_state[static_cast<std::size_t>(arg_streamindex)];
    // stream_handle_at() 以降に、完了コールバックが論理ストリーム
    // を解放している場合がある。 空・置換済みの状態を、その代わりに復活させない。
    if (state.handle != stream) return;
#if defined(_WIN32)
    const auto active = (state.native != 0 && g_bass_api.channel_is_active != nullptr)
        ? g_bass_api.channel_is_active(state.native)
        : (state.playing ? 1u : static_cast<BassDword>(g_BASS_ACTIVE_STOPPED));
    if (active == static_cast<BassDword>(g_BASS_ACTIVE_STOPPED)) {
        g__loopcounts[static_cast<std::size_t>(arg_streamindex)] = playback_loop_count(arg_loopcount);
        state.playing = true;
        state.paused = g__paused;
        if (!g__paused && state.native != 0 && g_bass_api.channel_play != nullptr &&
            g_bass_api.channel_play(state.native, 0) == 0) state.playing = false;
    } else {
        g__loopcounts[static_cast<std::size_t>(arg_streamindex)] = playback_loop_count(arg_loopcount, true);
    }
#else
    if (!state.playing) {
        g__loopcounts[static_cast<std::size_t>(arg_streamindex)] = playback_loop_count(arg_loopcount);
        state.playing = true;
        state.paused = g__paused;
    } else {
        g__loopcounts[static_cast<std::size_t>(arg_streamindex)] = playback_loop_count(arg_loopcount, true);
    }
#endif
}

void dispose_bass()
{
    if (!is_alivable()) return;
    g_callbacks_stopping.store(true, std::memory_order_release);
    std::unique_lock backend_lock(g_audio_backend_mutex);
    for (std::size_t i = 0; i < kStreamCount; ++i) {
        _free_fadeoutstream(static_cast<Integer>(i));
        const auto stream = stream_handle_at(static_cast<Integer>(i));
        if (stream != 0) _free_channel_lockfree(std::nullopt, ::cw::lib::Value(stream), ::cw::lib::Value(0), static_cast<Integer>(i));
        // 停止フラグで新しいコールバックを拒否しても、受け付け済みのものが
        // まだ処理中の場合がある。 終了まで、同じ状態ロックで同期する。
        std::scoped_lock state_lock(g_stream_mutex);
        g__streams[i] = ::cw::lib::Value(Integer(0));
        g__fadeoutstreams[i] = std::nullopt;
        g_fadeout_state[i] = std::nullopt;
        g__loopstarts[i] = 0;
        g__loopcounts[i] = i == 0 ? 0 : 1;
    }
    for (const auto font : g_font_handles) {
#if defined(_WIN32)
        if (g_bass_api.midi_font_free != nullptr) g_bass_api.midi_font_free(font);
#endif
    }
    g_font_handles.clear();
#if defined(_WIN32)
    // 上の同期解除で、新しいコールバックは止めてある。 停止フラグ
    // より前に入った処理をDLLが有効なうちに終わらせ、 機器・モ
    // ジュールの解放前にバックエンドの排他的な所有権を取り直す。
    backend_lock.unlock();
    {
        std::unique_lock callback_lock(g_callbacks_mutex);
        g_callbacks_condition.wait(callback_lock, [] {
            return g_callbacks_inflight.load(std::memory_order_acquire) == 0;
        });
    }
    backend_lock.lock();
    if (g_bass_api.get_device_info != nullptr && g_bass_api.set_device != nullptr && g_bass_api.free_library != nullptr) {
        // 機器ごとの終了も探索と同じく64位置に制限する。
        // Python版は GetDeviceInfo 失敗
        // まで走査するが、64以上の初期化済み機器は、 下の
        // モジュール解放前にこのループで個別のFreeを受け
        // ない。 範囲内の情報取得失敗は飛ばして後続も調べ
        // 、Python版の最初の失敗での終了とは分ける。
        for (BassDword device = 0; device < 64; ++device) {
            BassDeviceInfoRaw info{};
            if (g_bass_api.get_device_info(device, &info) == 0) continue;
            if ((info.flags & static_cast<BassDword>(g_BASS_DEVICE_INIT)) != 0) {
                if (g_bass_api.set_device(device) != 0) g_bass_api.free_library();
            }
        }
        g_bass_api.initialized = false;
    }
    g_bass_api.unload();
#endif
    g_soundfonts.clear();
    g__sfonts.clear();
    g__bass.reset();
    g__bassmidi.reset();
    g__bassfx.reset();
    g__paused = false;
    for (auto& state : g_stream_state) state = StreamState{};
    for (auto& fade : g_fadeout_state) fade.reset();
}

bool play_bgm(::cw::lib::String arg_fpath, ::cw::lib::Real arg_volume, ::cw::lib::Integer arg_loopcount, ::cw::lib::Integer arg_channel, ::cw::lib::Integer arg_fade)
{
    if (!is_alivable() || arg_channel >= g_MAX_BGM_CHANNELS) return false;
    stop_bgm(arg_channel, arg_fade, false);
    const auto index = stream_index(arg_channel, g_STREAM_BGM);
    const auto stream = _play(arg_fpath, arg_volume, arg_loopcount, index, arg_fade, 0, 0);
    assign_stream_handle(index, stream_handle(stream));
    return stream_handle(stream) != 0;
}

void set_bgmloopcount(::cw::lib::Integer arg_loopcount, ::cw::lib::Integer arg_channel)
{
    set_loopcount(arg_loopcount, stream_index(arg_channel, g_STREAM_BGM));
}

bool play_sound(::cw::lib::String arg_fpath, ::cw::lib::Real arg_volume, bool arg_fromscenario, ::cw::lib::Integer arg_loopcount, ::cw::lib::Integer arg_channel, ::cw::lib::Integer arg_fade)
{
    if (!is_alivable() || arg_channel >= g_MAX_SOUND_CHANNELS) return false;
    stop_sound(arg_fromscenario, arg_channel, arg_fade, false);
    const auto index = arg_fromscenario ? stream_index(arg_channel, g_STREAM_SOUND1) : g_STREAM_SOUND2;
    const auto stream = _play(arg_fpath, arg_volume, arg_loopcount, index, arg_fade, 0, 0);
    assign_stream_handle(index, stream_handle(stream));
    return stream_handle(stream) != 0;
}

void _stop(::cw::lib::Integer arg_streamindex, ::cw::lib::Integer arg_fade, bool arg_stopfadeout)
{
    if (!is_alivable() || !valid_index(arg_streamindex)) return;
#if defined(_WIN32)
    std::scoped_lock backend_lock(g_audio_backend_mutex);
#endif
    if (arg_stopfadeout) _free_fadeoutstream(arg_streamindex);
    const auto stream = stream_handle_at(arg_streamindex);
    if (stream == 0) return;
    if (arg_fade > 0) {
        _free_fadeoutstream(arg_streamindex);
        NativeHandle native = 0;
        {
            std::scoped_lock lock(g_stream_mutex);
            auto& state = g_stream_state[static_cast<std::size_t>(arg_streamindex)];
            g_fadeout_state[static_cast<std::size_t>(arg_streamindex)] = FadeoutState{
                stream,
                g__loopcounts[static_cast<std::size_t>(arg_streamindex)],
                g__loopstarts[static_cast<std::size_t>(arg_streamindex)],
                state.native,
                state.source_native,
                state.end_sync,
                0,
                state.encoded_data,
                state.paused
            };
            g__fadeoutstreams[static_cast<std::size_t>(arg_streamindex)] =
                std::tuple<::cw::lib::Value, LoopCount, Integer>{::cw::lib::Value(stream),
                    g__loopcounts[static_cast<std::size_t>(arg_streamindex)],
                    g__loopstarts[static_cast<std::size_t>(arg_streamindex)]};
            native = state.native;
            state = StreamState{};
        }
        bool scheduled = false;
#if defined(_WIN32)
        if (native != 0 && g_bass_api.channel_slide_attribute != nullptr && g_bass_api.channel_set_sync != nullptr &&
            g_bass_api.channel_get_attribute != nullptr && g_bass_api.channel_set_attribute != nullptr) {
            // ネイティブ版の意図的な安全策として、チャンネル全体の同期を設定す
            // る前に、以前のフェードイン・音量変化を完了・キャンセルする。
            // SetAttribute は既存の変化を止め、その同期を呼ぶ。
            float current_volume = 0.0F;
            if (g_bass_api.channel_get_attribute(native, static_cast<BassDword>(g_BASS_ATTRIB_VOL), &current_volume) &&
                g_bass_api.channel_set_attribute(native, static_cast<BassDword>(g_BASS_ATTRIB_VOL), current_volume)) {
                const auto user = reinterpret_cast<void*>(static_cast<std::uintptr_t>(arg_streamindex + 1));
                const auto sync = g_bass_api.channel_set_sync(native, static_cast<BassDword>(g_BASS_SYNC_SLIDE), 0,
                    native_free_callback, user);
                if (sync != 0) {
                    bool armed = false;
                    {
                        std::scoped_lock lock(g_stream_mutex);
                        if (auto& fade = g_fadeout_state[static_cast<std::size_t>(arg_streamindex)];
                            fade && fade->handle == stream && fade->native == native) {
                            fade->slide_sync = sync;
                            fade->slide_arming = true;
                            fade->slide_completed = false;
                            armed = true;
                        }
                    }
                    if (armed) {
                        // ネイティブ呼び出しをまたいで状態ミューテックスを保持しない。 BASSが
                        // コールバックを同期実行したり、別スレッドを待ったりする場合があるため。
                        scheduled = g_bass_api.channel_slide_attribute(native, static_cast<BassDword>(g_BASS_ATTRIB_VOL), 0.0F,
                            static_cast<BassDword>(arg_fade)) != 0;
                        bool completed = false;
                        {
                            std::scoped_lock lock(g_stream_mutex);
                            if (auto& fade = g_fadeout_state[static_cast<std::size_t>(arg_streamindex)];
                                fade && fade->handle == stream && fade->native == native && fade->slide_sync == sync) {
                                fade->slide_arming = false;
                                completed = fade->slide_completed;
                            }
                        }
                        if (completed) _free_channel(std::nullopt, Value(stream), Value(0), arg_streamindex);
                    } else if (g_bass_api.channel_remove_sync != nullptr) {
                        g_bass_api.channel_remove_sync(native, sync);
                    }
                }
            }
        }
#endif
        if (!scheduled) _free_channel(std::nullopt, Value(stream), Value(0), arg_streamindex);
    } else {
        _free_channel(std::nullopt, ::cw::lib::Value(stream), ::cw::lib::Value(0), arg_streamindex);
    }
    assign_stream_handle(arg_streamindex, 0);
}

void _stop_func(::cw::lib::Value arg_stream)
{
    const auto channel = stream_handle(arg_stream);
    if (channel == 0) return;
    for (Integer index = 0; index < static_cast<Integer>(kStreamCount); ++index) {
        if (stream_handle_at(index) == channel) {
            _free_channel(std::nullopt, std::move(arg_stream), ::cw::lib::Value(0), index);
            std::scoped_lock lock(g_stream_mutex);
            if (stream_handle(g__streams[static_cast<std::size_t>(index)]) == channel)
                g__streams[static_cast<std::size_t>(index)] = Value(Integer(0));
            return;
        }
        bool fading = false;
        {
            std::scoped_lock lock(g_stream_mutex);
            const auto& fade = g__fadeoutstreams[static_cast<std::size_t>(index)];
            fading = fade && stream_handle(std::get<0>(*fade)) == channel;
        }
        if (fading) {
            _free_channel_lockfree(std::nullopt, std::move(arg_stream), ::cw::lib::Value(0), index);
            return;
        }
    }
}

void _free_fadeoutstream(::cw::lib::Integer arg_streamindex)
{
    if (!valid_index(arg_streamindex)) return;
    Integer handle = 0;
    {
        std::scoped_lock lock(g_stream_mutex);
        if (const auto& fade_state = g_fadeout_state[static_cast<std::size_t>(arg_streamindex)]; fade_state)
            handle = fade_state->handle;
        else if (const auto& fade = g__fadeoutstreams[static_cast<std::size_t>(arg_streamindex)]; fade)
            handle = stream_handle(std::get<0>(*fade));
    }
    if (handle != 0) _free_channel_lockfree(std::nullopt, ::cw::lib::Value(handle), ::cw::lib::Value(0), arg_streamindex);
}

void stop_bgm(::cw::lib::Integer arg_channel, ::cw::lib::Integer arg_fade, bool arg_stopfadeout)
{
    if (!is_alivable()) return;
    _stop(stream_index(arg_channel, g_STREAM_BGM), arg_fade, arg_stopfadeout);
}

void stop_sound(bool arg_fromscenario, ::cw::lib::Integer arg_channel, ::cw::lib::Integer arg_fade, bool arg_stopfadeout)
{
    if (!is_alivable()) return;
    _stop(arg_fromscenario ? stream_index(arg_channel, g_STREAM_SOUND1) : g_STREAM_SOUND2, arg_fade, arg_stopfadeout);
}

// ネイティブ版はゼロ以下のフェード時間では属性を即時設定する。
// Python版はゼロでも変化用の関数を呼ぶが、ここでは即時・遷
// 移を明示的に分け、 正の時間では引き続き変化用の関数を使う。
void _set_volume(::cw::lib::Real arg_volume, ::cw::lib::Integer arg_streamindex, ::cw::lib::Integer arg_fade)
{
    if (!is_alivable() || !valid_index(arg_streamindex)) return;
#if defined(_WIN32)
    std::scoped_lock backend_lock(g_audio_backend_mutex);
#endif
    const Real volume = normalize_volume(arg_volume);
    if (volume == 0.0 && arg_fade == 0) _free_fadeoutstream(arg_streamindex);
    const auto stream = stream_handle_at(arg_streamindex);
    if (stream == 0) return;
    std::scoped_lock lock(g_stream_mutex);
    auto& state = g_stream_state[static_cast<std::size_t>(arg_streamindex)];
    if (state.handle == stream) {
#if defined(_WIN32)
        if (state.native != 0) {
            const bool ok = arg_fade > 0
                ? g_bass_api.channel_slide_attribute != nullptr &&
                    g_bass_api.channel_slide_attribute(state.native, static_cast<BassDword>(g_BASS_ATTRIB_VOL),
                        static_cast<float>(volume), static_cast<BassDword>(arg_fade)) != 0
                : g_bass_api.channel_set_attribute != nullptr &&
                    g_bass_api.channel_set_attribute(state.native, static_cast<BassDword>(g_BASS_ATTRIB_VOL),
                        static_cast<float>(volume)) != 0;
            if (!ok) return;
        }
#endif
        state.volume = volume;
        state.fade = std::max<Integer>(0, arg_fade);
    }
    g_stream_volume[static_cast<std::size_t>(arg_streamindex)] = volume;
}

void set_bgmvolume(::cw::lib::Real arg_volume, ::cw::lib::Integer arg_channel, ::cw::lib::Integer arg_fade)
{
    if (!is_alivable()) return;
    _set_volume(arg_volume, stream_index(arg_channel, g_STREAM_BGM), arg_fade);
}

void set_soundvolume(::cw::lib::Real arg_volume, bool arg_fromscenario, ::cw::lib::Integer arg_channel, ::cw::lib::Integer arg_fade)
{
    if (!is_alivable()) return;
    _set_volume(arg_volume, arg_fromscenario ? stream_index(arg_channel, g_STREAM_SOUND1) : g_STREAM_SOUND2, arg_fade);
}

void main()
{
    // CLIデモはアプリケーションの音声サービスがデバイス寿命を管理する。
    // この公開関数はPython互換のエントリポイントとして副作用を持たない。
}

}
