#pragma once

// cw/bassplayer.py のBASS公開API。実機のBASS DLLはbassplayer.cppで遅延ロードし、
// DLLが利用できない環境ではis_alivable()がfalseを返して呼び出し側の音声
// サービスへフォールバックできるようにする。
#include <cstdint>
#include <string>
#include <vector>
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"

namespace cw::bassplayer {
// Pythonで0以下のループ回数は、すべて無期限の再生を
// 意味する。 正の回数では、現在再生中の1回分を追加する。
using LoopCount = std::uint64_t;
inline LoopCount playback_loop_count(::cw::lib::Integer count, bool playing = false) noexcept {
    return count > 0 ? static_cast<LoopCount>(count) + (playing ? 1u : 0u) : 0;
}
class BASS_CHANNELINFO : public ::cw::lib::Object {
public:
    ::cw::lib::Integer freq{};
    ::cw::lib::Integer chans{};
    ::cw::lib::Integer flags{};
    ::cw::lib::Integer ctype{};
    ::cw::lib::Integer origres{};
    ::cw::lib::Integer plugin{};
    ::cw::lib::Integer sample{};
    ::cw::lib::String filename;
    BASS_CHANNELINFO() = default;
    virtual ~BASS_CHANNELINFO() = default;
    inline static std::vector<std::tuple<::cw::lib::String, ::cw::lib::Value>> s__fields_{};
};

class BASS_DEVICEINFO : public ::cw::lib::Object {
public:
    ::cw::lib::String name;
    ::cw::lib::String driver;
    ::cw::lib::Integer flags{};
    BASS_DEVICEINFO() = default;
    virtual ~BASS_DEVICEINFO() = default;
    inline static std::vector<std::tuple<::cw::lib::String, ::cw::lib::Value>> s__fields_{};
};

extern ::cw::lib::Integer g_BASS_ACTIVE_STOPPED;
extern ::cw::lib::Integer g_BASS_ATTRIB_EAXMIX;
extern ::cw::lib::Integer g_BASS_ATTRIB_FREQ;
extern ::cw::lib::Integer g_BASS_ATTRIB_MUSIC_AMPLIFY;
extern ::cw::lib::Integer g_BASS_ATTRIB_MUSIC_BPM;
extern ::cw::lib::Integer g_BASS_ATTRIB_MUSIC_PANSEP;
extern ::cw::lib::Integer g_BASS_ATTRIB_MUSIC_PSCALER;
extern ::cw::lib::Integer g_BASS_ATTRIB_MUSIC_SPEED;
extern ::cw::lib::Integer g_BASS_ATTRIB_MUSIC_VOL_CHAN;
extern ::cw::lib::Integer g_BASS_ATTRIB_MUSIC_VOL_GLOBAL;
extern ::cw::lib::Integer g_BASS_ATTRIB_MUSIC_VOL_INST;
extern ::cw::lib::Integer g_BASS_ATTRIB_PAN;
extern ::cw::lib::Integer g_BASS_ATTRIB_TEMPO;
extern ::cw::lib::Integer g_BASS_ATTRIB_TEMPO_FREQ;
extern ::cw::lib::Integer g_BASS_ATTRIB_TEMPO_PITCH;
extern ::cw::lib::Integer g_BASS_ATTRIB_VOL;
extern ::cw::lib::Integer g_BASS_DEFAULT;
extern ::cw::lib::Integer g_BASS_DEVICE_3D;
extern ::cw::lib::Integer g_BASS_DEVICE_8BITS;
extern ::cw::lib::Integer g_BASS_DEVICE_DEFAULT;
extern ::cw::lib::Integer g_BASS_DEVICE_ENABLED;
extern ::cw::lib::Integer g_BASS_DEVICE_INIT;
extern ::cw::lib::Integer g_BASS_DEVICE_MONO;
extern ::cw::lib::Integer g_BASS_FILEPOS_CURRENT;
extern ::cw::lib::Integer g_BASS_FILEPOS_END;
extern ::cw::lib::Integer g_BASS_FX_FREESOURCE;
extern ::cw::lib::Integer g_BASS_MUSIC_POSRESET;
extern ::cw::lib::Integer g_BASS_MUSIC_PRESCAN;
extern ::cw::lib::Integer g_BASS_MUSIC_RAMP;
extern ::cw::lib::Integer g_BASS_MUSIC_RAMPS;
extern ::cw::lib::Integer g_BASS_MUSIC_STOPBACK;
extern ::cw::lib::Integer g_BASS_POS_BYTE;
extern ::cw::lib::Integer g_BASS_SAMPLE_8BITS;
extern ::cw::lib::Integer g_BASS_SAMPLE_FLOAT;
extern ::cw::lib::Integer g_BASS_SAMPLE_LOOP;
extern ::cw::lib::Integer g_BASS_STREAM_DECODE;
extern ::cw::lib::Integer g_BASS_SYNC_END;
extern ::cw::lib::Integer g_BASS_SYNC_MIXTIME;
extern ::cw::lib::Integer g_BASS_SYNC_MUSICPOS;
extern ::cw::lib::Integer g_BASS_SYNC_POS;
extern ::cw::lib::Integer g_BASS_SYNC_SLIDE;
extern ::cw::lib::Integer g_BASS_TAG_OGG;
extern ::cw::lib::Integer g_BASS_TAG_RIFF_BEXT;
extern ::cw::lib::Integer g_BASS_TAG_RIFF_CART;
extern ::cw::lib::Integer g_BASS_TAG_RIFF_DISP;
extern ::cw::lib::Integer g_BASS_TAG_RIFF_INFO;
extern ::cw::lib::Integer g_BASS_UNICODE;
extern ::cw::lib::Integer g_CC111;
extern ::cw::lib::Value g_CC111LOOP;
extern ::cw::lib::Value g_FREE_CHANNEL;
extern ::cw::lib::Integer g_MAX_BGM_CHANNELS;
extern ::cw::lib::Integer g_MAX_SOUND_CHANNELS;
extern ::cw::lib::Integer g_MIDI_EVENT_CONTROL;
extern ::cw::lib::Integer g_MIDI_EVENT_END;
extern ::cw::lib::Integer g_MIDI_EVENT_END_TRACK;
extern ::cw::lib::Integer g_STREAM_BGM;
extern ::cw::lib::Integer g_STREAM_SOUND1;
extern ::cw::lib::Integer g_STREAM_SOUND2;
extern ::cw::lib::Value g_SYNCPROC;
extern std::optional<::cw::lib::Value> g__bass;
extern std::optional<::cw::lib::Value> g__bassfx;
extern std::optional<::cw::lib::Value> g__bassmidi;
extern ::cw::lib::Value g__fadeoutlock;
extern std::vector<std::optional<std::tuple<::cw::lib::Value, LoopCount, ::cw::lib::Integer>>> g__fadeoutstreams;
extern ::cw::lib::Value g__lock;
extern std::vector<LoopCount> g__loopcounts;
extern std::vector<::cw::lib::Integer> g__loopstarts;
extern bool g__paused;
extern ::cw::lib::Bytes g__sfonts;
extern std::vector<::cw::lib::Value> g__streams;
extern ::cw::lib::Value g_c_BOOL;
extern ::cw::lib::Value g_c_BYTE;
extern ::cw::lib::Value g_c_DWORD;
extern ::cw::lib::Value g_c_HCHANNEL;
extern ::cw::lib::Value g_c_HMUSIC;
extern ::cw::lib::Value g_c_HPLUGIN;
extern ::cw::lib::Value g_c_HSAMPLE;
extern ::cw::lib::Value g_c_HSOUNDFONT;
extern ::cw::lib::Value g_c_HSTREAM;
extern ::cw::lib::Value g_c_HSYNC;
extern ::cw::lib::Value g_c_QWORD;
extern ::cw::lib::Value g_c_WORD;

void _cc111loop(::cw::lib::Value arg_handle, ::cw::lib::Value arg_channel, ::cw::lib::Value arg_data, std::optional<::cw::lib::Integer> arg_streamindex = std::nullopt);
void _free_channel(std::optional<::cw::lib::Value> arg_handle, ::cw::lib::Value arg_channel, ::cw::lib::Value arg_data, std::optional<::cw::lib::Integer> arg_streamindex = std::nullopt);
void _free_channel_func(::cw::lib::Integer arg_streamindex);
void _free_channel_lockfree(std::optional<::cw::lib::Value> arg_handle, ::cw::lib::Value arg_channel, ::cw::lib::Value arg_data, std::optional<::cw::lib::Integer> arg_streamindex = std::nullopt);
void _loop(::cw::lib::Value arg_handle, ::cw::lib::Value arg_channel, ::cw::lib::Value arg_data, std::optional<::cw::lib::Integer> arg_streamindex = std::nullopt);
bool is_alivable();
bool is_alivablemidi();
bool is_alivablewithpath(::cw::lib::String arg_path);
::cw::lib::Bytes _encode_fpath(::cw::lib::String arg_fpath);
bool init_bass(std::vector<std::tuple<::cw::lib::String, ::cw::lib::Real>> arg_soundfonts);
bool change_soundfonts(std::vector<std::tuple<::cw::lib::String, ::cw::lib::Real>> arg_soundfonts);
bool pause();
bool start();
void _switch_device();
::cw::lib::Value _play(::cw::lib::String arg_fpath, ::cw::lib::Real arg_volume, ::cw::lib::Integer arg_loopcount, ::cw::lib::Integer arg_streamindex, ::cw::lib::Integer arg_fade, ::cw::lib::Integer arg_tempo = 0, ::cw::lib::Integer arg_pitch = 0);
::cw::lib::Real _get_attribute(::cw::lib::Value arg_stream, ::cw::lib::Value arg_flag);
std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> _get_loopinfo(::cw::lib::String arg_fpath, ::cw::lib::Value arg_stream);
void set_loopcount(::cw::lib::Integer arg_loopcount, ::cw::lib::Integer arg_streamindex);
void dispose_bass();
bool play_bgm(::cw::lib::String arg_fpath, ::cw::lib::Real arg_volume = 1.0, ::cw::lib::Integer arg_loopcount = 0, ::cw::lib::Integer arg_channel = 0, ::cw::lib::Integer arg_fade = 0);
void set_bgmloopcount(::cw::lib::Integer arg_loopcount, ::cw::lib::Integer arg_channel = 0);
bool play_sound(::cw::lib::String arg_fpath, ::cw::lib::Real arg_volume = 1.0, bool arg_fromscenario = false, ::cw::lib::Integer arg_loopcount = 1, ::cw::lib::Integer arg_channel = 0, ::cw::lib::Integer arg_fade = 0);
void _stop(::cw::lib::Integer arg_streamindex, ::cw::lib::Integer arg_fade, bool arg_stopfadeout = false);
void _stop_func(::cw::lib::Value arg_stream);
void _free_fadeoutstream(::cw::lib::Integer arg_streamindex);
void stop_bgm(::cw::lib::Integer arg_channel = 0, ::cw::lib::Integer arg_fade = 0, bool arg_stopfadeout = false);
void stop_sound(bool arg_fromscenario = false, ::cw::lib::Integer arg_channel = 0, ::cw::lib::Integer arg_fade = 0, bool arg_stopfadeout = false);
void _set_volume(::cw::lib::Real arg_volume, ::cw::lib::Integer arg_streamindex, ::cw::lib::Integer arg_fade);
void set_bgmvolume(::cw::lib::Real arg_volume, ::cw::lib::Integer arg_channel = 0, ::cw::lib::Integer arg_fade = 0);
void set_soundvolume(::cw::lib::Real arg_volume, bool arg_fromscenario = false, ::cw::lib::Integer arg_channel = 0, ::cw::lib::Integer arg_fade = 0);
void main();
}

