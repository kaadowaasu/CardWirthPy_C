#include "cwxeditor_src/lib/audio_loop.hpp"
#include "sdl_audio.hpp"

#include "app/explorer/midi_player.hpp"

#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>


namespace cwxeditor::lib {
namespace {

[[nodiscard]] std::string path_utf8(const std::filesystem::path& path) {
#if defined(_WIN32)
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
#else
    return path.string();
#endif
}

[[nodiscard]] std::size_t bus_index(AudioBus bus) noexcept {
    switch (bus) {
    case AudioBus::Bgm: return 0U;
    case AudioBus::SoundEffect: return 1U;
    case AudioBus::Preview: return 2U;
    }
    return 1U;
}

[[nodiscard]] float gain(float value) noexcept {
    return std::isfinite(value) ? std::clamp(value, 0.0F, 1.0F) : 0.0F;
}

[[nodiscard]] std::string audio_extension(const AudioSource& source) {
    auto extension = source.format_hint.empty()
        ? path_utf8(source.path.extension()) : source.format_hint;
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char value) {
                       return static_cast<char>(std::tolower(value));
                   });
    if (!extension.empty() && extension.front() != '.')
        extension.insert(extension.begin(), '.');
    return extension;
}

[[nodiscard]] bool is_midi_source(const AudioSource& source) {
    const auto extension = audio_extension(source);
    return extension == ".mid" || extension == ".midi";
}

[[nodiscard]] bool regular_soundfont(const std::filesystem::path& path) {
    if (path.empty()) return false;
    auto extension = path_utf8(path.extension());
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char value) {
                       return static_cast<char>(std::tolower(value));
                   });
    if (extension != ".sf2") return false;
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) && !error;
}

// エディタの設定では通常、スキン相対のサウンドフォントを指定する。 ネ
// イティブのバックエンドと同じライブラリのルートから解決し、なければ
// 不変のAndroidのDataツリーを使う。 SDL_mixerの
// TimidityはAndroidでそのツリーを確実に見つけられない
// が、 TinySoundFontは解決済みのファイルを直接使える。
[[nodiscard]] std::optional<std::filesystem::path> resolve_midi_soundfont(
    const AudioConfiguration& request, const AudioSource& source,
    std::filesystem::path& data_directory) {
    const auto choose = [&data_directory](const std::filesystem::path& candidate)
        -> std::optional<std::filesystem::path> {
        if (!regular_soundfont(candidate)) return std::nullopt;
        data_directory = candidate.parent_path().parent_path();
        return candidate;
    };
    if (source.sound_font) {
        if (const auto result = choose(*source.sound_font)) return result;
        for (const auto& root : request.library_search_roots) {
            if (const auto result = choose(root / *source.sound_font)) return result;
        }
    }
    for (const auto& font : request.sound_fonts) {
        if (const auto result = choose(font.path)) return result;
        for (const auto& root : request.library_search_roots) {
            if (const auto result = choose(root / font.path)) return result;
        }
    }

    // .../Data/... にあるMIDIファイルから、兄弟の
    // サウンドフォントのツリーを特定する。 プレビューを押した時点
    // でスキン設定が未読の、持ち運び用のエディタ設定にも対応する。
    for (auto probe = source.path.parent_path(); !probe.empty();) {
        if (std::error_code error; std::filesystem::is_directory(
                probe / "SoundFont", error) && !error) {
            data_directory = probe;
            std::error_code iterate_error;
            for (std::filesystem::directory_iterator it(
                     probe / "SoundFont", iterate_error), end;
                 it != end; it.increment(iterate_error)) {
                if (iterate_error) {
                    iterate_error.clear();
                    continue;
                }
                if (const auto result = choose(it->path())) return result;
            }
        }
        const auto parent = probe.parent_path();
        if (parent == probe) break;
        probe = parent;
    }

#if defined(__ANDROID__)
    if (const char* internal = SDL_GetAndroidInternalStoragePath();
        internal && *internal) {
        const auto root = std::filesystem::path(internal) / "app" / "Data";
        data_directory = root;
        if (const auto result = choose(
                root / "SoundFont" / "005.6mg_Aspirin_Stereo_V1.2_Bank.sf2"))
            return result;
    }
#endif
    return std::nullopt;
}

[[nodiscard]] std::unique_ptr<cardwirth::explorer::MidiPlayer> make_midi_player(
    const AudioConfiguration& request, const AudioSource& source) {
    std::filesystem::path data_directory;
    const auto preferred = resolve_midi_soundfont(request, source, data_directory);
    if (preferred) {
        return std::make_unique<cardwirth::explorer::MidiPlayer>(
            data_directory, *preferred);
    }
    if (data_directory.empty()) {
        // サウンドフォントの指定も、素材の隣のDataツリーもない場合は、
        // エンジンと同様に作業ディレクトリのDataを探す。 メモリ内の素材
        // などが該当し、WebではUserDataが作業ディレクトリとなる。
        std::error_code error;
        if (const auto current = std::filesystem::current_path(error); !error)
            data_directory = current / "Data";
    }
    return std::make_unique<cardwirth::explorer::MidiPlayer>(data_directory);
}

[[nodiscard]] int midi_repeat_count(const std::uint32_t loop_count) noexcept {
    if (loop_count == 0U ||
        loop_count == std::numeric_limits<std::uint32_t>::max()) return -1;
    const auto repeats = loop_count - 1U;
    return repeats > static_cast<std::uint32_t>(std::numeric_limits<int>::max())
        ? std::numeric_limits<int>::max() : static_cast<int>(repeats);
}

class SdlAudioBackend final : public AudioBackend {
public:
    ~SdlAudioBackend() override {
        stop_all();
        std::scoped_lock lock(mutex_);
        if (mixer_) MIX_DestroyMixer(mixer_);
        mixer_ = nullptr;
        if (initialized_) MIX_Quit();
    }

    bool initialize(std::string& error) override {
        std::scoped_lock lock(mutex_);
        if (mixer_) {
            error.clear();
            return true;
        }
        if (!MIX_Init()) {
            error = SDL_GetError();
            return false;
        }
        initialized_ = true;
        mixer_ = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
        if (!mixer_) {
            error = SDL_GetError();
            MIX_Quit();
            initialized_ = false;
            return false;
        }
        error.clear();
        return true;
    }

    AudioConfigurationResult configure(const AudioConfiguration& request) override {
        std::string error;
        const bool ready = initialize(error);
        std::scoped_lock lock(mutex_);
        configuration_request_ = request;
        configuration_ = {};
        configuration_.requested = request.preference;
        configuration_.effective = AudioBackendPreference::SDL;
        configuration_.sound_fonts = request.sound_fonts;
        configuration_.capabilities = ready
            ? AudioCapability::Playback | AudioCapability::SDL |
                   AudioCapability::Midi | AudioCapability::MemorySource |
                   AudioCapability::SoundFont | AudioCapability::Pan |
                   AudioCapability::Fade
            : AudioCapability::None;
        configuration_.used_fallback = ready &&
            request.preference != AudioBackendPreference::Automatic &&
            request.preference != AudioBackendPreference::SDL;
        if (!ready) configuration_.error = std::move(error);
        if (ready && !request.allow_fallback &&
            request.preference != AudioBackendPreference::Automatic &&
            request.preference != AudioBackendPreference::SDL) {
            configuration_.capabilities = AudioCapability::None;
            configuration_.error = "SDL audio does not provide the requested backend";
        }
        return configuration_;
    }

    [[nodiscard]] AudioConfigurationResult configuration() const override {
        std::scoped_lock lock(mutex_);
        return configuration_;
    }

    AudioHandle play(const AudioSource& source, const AudioPlayOptions& options) override {
        std::scoped_lock lock(mutex_);
        // AndroidのSDL_mixerはPCMなどを再生できても、Timidity
        // の設定・音源検索先が同梱されず、標準MIDIを拒否する場合がある。
        // MIX_LoadAudio に復号を頼む前に、メモリ内の TinySoundFont
        // 再生経路へ渡す。 ハンドルは通常の AudioBackend
        // のトラックなので、 RoutedAudio と MaterialSelect
        // は既存の停止・一時停止・音量管理を維持する。
        if (is_midi_source(source)) {
            if (source.path.empty() && source.encoded_data.empty()) return 0U;
            try {
                auto midi = make_midi_player(configuration_request_, source);
                const int repeats = midi_repeat_count(options.loop_count);
                const float volume = gain(options.volume) * bus_volumes_[bus_index(options.bus)];
                if (!source.encoded_data.empty()) {
                    // メモリ内の素材を扱う（Webは素材をRAMに保持する）。 サウ
                    // ンドフォントは引き続き設定済み、またはData内のものを使う。
                    const auto soundfont = midi->find_soundfont();
                    if (!soundfont) return 0U;
                    std::size_t size{};
                    void* loaded = SDL_LoadFile(path_utf8(*soundfont).c_str(), &size);
                    if (!loaded) return 0U;
                    const std::vector<std::byte> soundfont_data(
                        static_cast<const std::byte*>(loaded),
                        static_cast<const std::byte*>(loaded) + size);
                    SDL_free(loaded);
                    midi->play_memory(source.encoded_data, soundfont_data, repeats, volume,
                                      path_utf8(source.path), path_utf8(*soundfont));
                } else {
                    midi->play(source.path, repeats, volume);
                }
                AudioHandle handle = next_handle_++;
                if (handle == 0U) handle = next_handle_++;
                Track state;
                state.midi = std::move(midi);
                state.bus = options.bus;
                state.gain = gain(options.volume);
                tracks_.emplace(handle, std::move(state));
                return handle;
            } catch (...) {
                // ネイティブのMIDIデコーダーが利用できる環境では、 通常
                // のSDL_mixer経路でもそのデータを試せるようにする。
            }
        }
        if (!mixer_) return 0U;

        MIX_Audio* audio = nullptr;
        if (!source.encoded_data.empty()) {
            auto* stream = SDL_IOFromConstMem(source.encoded_data.data(), source.encoded_data.size());
            if (stream) audio = MIX_LoadAudio_IO(mixer_, stream, true, true);
        } else if (!source.path.empty()) {
            const auto path = path_utf8(source.path);
            audio = MIX_LoadAudio(mixer_, path.c_str(), false);
        }
        if (!audio) return 0U;

        MIX_Track* track = MIX_CreateTrack(mixer_);
        if (!track || !MIX_SetTrackAudio(track, audio)) {
            if (track) MIX_DestroyTrack(track);
            MIX_DestroyAudio(audio);
            return 0U;
        }

        SDL_AudioSpec spec{};
        if (!MIX_GetAudioFormat(audio, &spec) || spec.freq <= 0) spec.freq = 48000;
        const float volume = gain(options.volume);
        (void)MIX_SetTrackGain(track, volume * bus_volumes_[bus_index(options.bus)]);
        if (options.pan != 0.0F) {
            const float pan = std::clamp(options.pan, -1.0F, 1.0F);
            MIX_StereoGains stereo{pan > 0.0F ? 1.0F - pan : 1.0F,
                                   pan < 0.0F ? 1.0F + pan : 1.0F};
            (void)MIX_SetTrackStereo(track, &stereo);
        }

        std::unique_ptr<FinalLoopPass> finalLoopPass;
        const SDL_PropertiesID properties = SDL_CreateProperties();
        const Sint64 loops = options.loop_count == 0U ||
                                     options.loop_count == std::numeric_limits<std::uint32_t>::max()
                                 ? -1
                                 : static_cast<Sint64>(options.loop_count) - 1;
        SDL_SetNumberProperty(properties, MIX_PROP_PLAY_LOOPS_NUMBER, loops);
        SDL_SetNumberProperty(properties, MIX_PROP_PLAY_START_MILLISECOND_NUMBER,
                              std::max<Sint64>(0, options.start_position.count()));
        SDL_SetNumberProperty(properties, MIX_PROP_PLAY_LOOP_START_MILLISECOND_NUMBER,
                              std::max<Sint64>(0, options.loop_start.count()));
        if (options.loop_region) {
            const auto start = audio_reference_samples_to_frames(
                options.loop_region->start_reference_samples_44100, spec.freq);
            if (start) {
                SDL_SetNumberProperty(properties, MIX_PROP_PLAY_LOOP_START_FRAME_NUMBER, *start);
                if (options.loop_region->end_reference_samples_44100) {
                    const auto end = audio_reference_samples_to_frames(
                        *options.loop_region->end_reference_samples_44100, spec.freq);
                    if (end && *end > *start && options.loop_count != 1U) {
                        SDL_SetNumberProperty(properties, MIX_PROP_PLAY_MAX_FRAME_NUMBER, *end);
                        if (loops >= 0) {
                            // D版の bassLoop と同様、有限ループの最終回は From を越え
                            // てファイル末尾まで再生する。 From で戻るのは、それより前の回だけ。
                            SDL_SetNumberProperty(properties, MIX_PROP_PLAY_LOOPS_NUMBER, loops - 1);
                            try {
                                finalLoopPass = std::make_unique<FinalLoopPass>();
                                finalLoopPass->start_frame = *start;
                            } catch (...) {
                                SDL_DestroyProperties(properties);
                                MIX_DestroyTrack(track);
                                MIX_DestroyAudio(audio);
                                return 0U;
                            }
                        }
                    }
                }
            }
        }
        SDL_SetNumberProperty(properties, MIX_PROP_PLAY_FADE_IN_MILLISECONDS_NUMBER,
                              std::max<Sint64>(0, options.fade_in.count()));
        SDL_SetFloatProperty(properties, MIX_PROP_PLAY_FADE_IN_START_GAIN_FLOAT, 0.0F);
        AudioHandle handle = next_handle_++;
        if (handle == 0U) handle = next_handle_++;
        Track state;
        state.final_loop_pass = std::move(finalLoopPass);
        state.audio = audio;
        state.track = track;
        state.bus = options.bus;
        state.spec = spec;
        state.gain = volume;
        const Sint64 frames = MIX_GetAudioDuration(audio);
        if (frames > 0)
            state.duration = std::chrono::milliseconds(frames * 1000 / spec.freq);
        // コールバックのユーザーデータ登録や再生開始より先に、安定した
        // 所有関係を確立する。 確保失敗時に、有効なミキサーのコール
        // バックが破棄済みの一時 Track を指さないようにする。
        decltype(tracks_)::iterator owned;
        try {
            const auto inserted = tracks_.emplace(handle, std::move(state));
            if (!inserted.second) {
                SDL_DestroyProperties(properties);
                MIX_DestroyTrack(track);
                MIX_DestroyAudio(audio);
                return 0U;
            }
            owned = inserted.first;
        } catch (...) {
            SDL_DestroyProperties(properties);
            MIX_DestroyTrack(track);
            MIX_DestroyAudio(audio);
            return 0U;
        }
        if (owned->second.final_loop_pass &&
            !MIX_SetTrackStoppedCallback(track, &playFinalLoopPass,
                                        owned->second.final_loop_pass.get())) {
            SDL_DestroyProperties(properties);
            destroy(owned->second, {});
            tracks_.erase(owned);
            return 0U;
        }
        const bool started = MIX_PlayTrack(track, properties);
        SDL_DestroyProperties(properties);
        if (!started) {
            destroy(owned->second, {});
            tracks_.erase(owned);
            return 0U;
        }
        return handle;
    }

    void pause(AudioHandle handle) override {
        std::scoped_lock lock(mutex_);
        if (const auto found = tracks_.find(handle); found != tracks_.end()) {
            if (found->second.midi) {
                found->second.midi->pause();
                return;
            }
            (void)MIX_PauseTrack(found->second.track);
        }
    }
    void resume(AudioHandle handle) override {
        std::scoped_lock lock(mutex_);
        if (const auto found = tracks_.find(handle); found != tracks_.end()) {
            if (found->second.midi) {
                found->second.midi->resume();
                return;
            }
            (void)MIX_ResumeTrack(found->second.track);
        }
    }
    void stop(AudioHandle handle, std::chrono::milliseconds fade_out = {}) noexcept override {
        try {
            std::scoped_lock lock(mutex_);
            const auto found = tracks_.find(handle);
            if (found == tracks_.end()) return;
            if (found->second.midi) {
                found->second.midi->stop();
                tracks_.erase(found);
                return;
            }
            destroy(found->second, fade_out);
            tracks_.erase(found);
        } catch (...) {
        }
    }
    void stop_bus(AudioBus bus) noexcept override {
        try {
            std::scoped_lock lock(mutex_);
            for (auto found = tracks_.begin(); found != tracks_.end();) {
                if (found->second.bus != bus) {
                    ++found;
                    continue;
                }
                if (found->second.midi) {
                    found->second.midi->stop();
                    found = tracks_.erase(found);
                    continue;
                }
                destroy(found->second, {});
                found = tracks_.erase(found);
            }
        } catch (...) {
        }
    }
    void stop_all() noexcept override {
        try {
            std::scoped_lock lock(mutex_);
            for (auto& [handle, track] : tracks_) {
                (void)handle;
                if (track.midi) {
                    track.midi->stop();
                    continue;
                }
                destroy(track, {});
            }
            tracks_.clear();
        } catch (...) {
        }
    }
    void seek(AudioHandle handle, std::chrono::milliseconds position) override {
        std::scoped_lock lock(mutex_);
        const auto found = tracks_.find(handle);
        if (found == tracks_.end()) return;
        if (found->second.midi) return;
        const Sint64 frames = std::max<Sint64>(0, position.count()) * found->second.spec.freq / 1000;
        (void)MIX_SetTrackPlaybackPosition(found->second.track, frames);
    }
    std::chrono::milliseconds position(AudioHandle handle) const override {
        std::scoped_lock lock(mutex_);
        const auto found = tracks_.find(handle);
        if (found == tracks_.end()) return {};
        if (found->second.midi) return {};
        const Sint64 frames = MIX_GetTrackPlaybackPosition(found->second.track);
        return frames < 0 ? std::chrono::milliseconds{}
                          : std::chrono::milliseconds(frames * 1000 / found->second.spec.freq);
    }
    std::chrono::milliseconds duration(AudioHandle handle) const override {
        std::scoped_lock lock(mutex_);
        const auto found = tracks_.find(handle);
        if (found != tracks_.end() && found->second.midi)
            return {};
        return found == tracks_.end() ? std::chrono::milliseconds{} : found->second.duration;
    }
    bool playing(AudioHandle handle) const override {
        std::scoped_lock lock(mutex_);
        const auto found = tracks_.find(handle);
        if (found != tracks_.end() && found->second.midi)
            return found->second.midi->playing();
        return found != tracks_.end() && MIX_TrackPlaying(found->second.track);
    }
    void set_bus_volume(AudioBus bus, float value) noexcept override {
        try {
            std::scoped_lock lock(mutex_);
            bus_volumes_[bus_index(bus)] = gain(value);
            for (auto& [handle, track] : tracks_) {
                (void)handle;
                if (track.bus == bus) {
                    if (track.midi) {
                        track.midi->set_volume(
                            track.gain * bus_volumes_[bus_index(bus)]);
                        continue;
                    }
                    (void)MIX_SetTrackGain(track.track, track.gain * bus_volumes_[bus_index(bus)]);
                }
            }
        } catch (...) {
        }
    }
    void cancel_fade_in(AudioHandle handle) noexcept override {
        try {
            std::scoped_lock lock(mutex_);
            const auto found = tracks_.find(handle);
            if (found == tracks_.end() || found->second.midi) return;
            (void)MIX_CardWirthCancelTrackFadeIn(found->second.track);
        } catch (...) {
        }
    }
    void set_track_volume(AudioHandle handle, float value) noexcept override {
        try {
            std::scoped_lock lock(mutex_);
            const auto found = tracks_.find(handle);
            if (found == tracks_.end()) return;
            found->second.gain = gain(value);
            if (found->second.midi) {
                found->second.midi->set_volume(
                    found->second.gain * bus_volumes_[bus_index(found->second.bus)]);
                return;
            }
            (void)MIX_SetTrackGain(
                found->second.track,
                found->second.gain * bus_volumes_[bus_index(found->second.bus)]);
        } catch (...) {
        }
    }
    std::vector<std::string> supported_formats() const override {
        return {"wav", "ogg", "mp3", "flac", "mid", "midi"};
    }

private:
    struct FinalLoopPass {
        Sint64 start_frame{};
    };

    static void SDLCALL playFinalLoopPass(void* userdata, MIX_Track* track) noexcept {
        const auto start = static_cast<const FinalLoopPass*>(userdata)->start_frame;
        // SDL_mixerは、ここで音の途切れなしの再開を明示的に許可する。
        // 先に無効化し、最終回の末尾到達で再び再生が始まらないようにする。
        (void)MIX_SetTrackStoppedCallback(track, nullptr, nullptr);
        const auto properties = SDL_CreateProperties();
        if (!properties) return;
        SDL_SetNumberProperty(properties, MIX_PROP_PLAY_START_FRAME_NUMBER, start);
        SDL_SetNumberProperty(properties, MIX_PROP_PLAY_LOOPS_NUMBER, 0);
        (void)MIX_PlayTrack(track, properties);
        SDL_DestroyProperties(properties);
    }

    struct Track {
        std::unique_ptr<FinalLoopPass> final_loop_pass;
        MIX_Audio* audio{};
        MIX_Track* track{};
        std::unique_ptr<cardwirth::explorer::MidiPlayer> midi;
        AudioBus bus{AudioBus::SoundEffect};
        SDL_AudioSpec spec{};
        float gain{1.0F};
        std::chrono::milliseconds duration{};
    };

    static void destroy(Track& track, std::chrono::milliseconds fade) noexcept {
        if (track.midi) {
            track.midi->stop();
            track.midi.reset();
        }
        if (track.track) {
            // 明示的な停止・終了では、有限ループの継続処理を起動しない。
            (void)MIX_SetTrackStoppedCallback(track.track, nullptr, nullptr);
            const Sint64 frames = fade.count() <= 0 || track.spec.freq <= 0
                                      ? 0
                                      : fade.count() * track.spec.freq / 1000;
            (void)MIX_StopTrack(track.track, frames);
            MIX_DestroyTrack(track.track);
        }
        if (track.audio) MIX_DestroyAudio(track.audio);
        track = {};
    }

    mutable std::mutex mutex_;
    MIX_Mixer* mixer_{};
    std::unordered_map<AudioHandle, Track> tracks_;
    std::array<float, 3> bus_volumes_{1.0F, 1.0F, 1.0F};
    AudioHandle next_handle_{1U};
    bool initialized_{};
    AudioConfiguration configuration_request_{};
    AudioConfigurationResult configuration_{};
};

}  // namespace

std::shared_ptr<AudioBackend> make_sdl_audio_backend() {
    return std::make_shared<SdlAudioBackend>();
}

}  // namespace cwxeditor::lib
