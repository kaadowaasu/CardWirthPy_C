#include "midi_player.hpp"

#include <SDL3/SDL.h>

#define TSF_IMPLEMENTATION
#include "../../3rdparty/TinySoundFont/tsf.h"
#define TML_IMPLEMENTATION
#include "../../3rdparty/TinySoundFont/tml.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace cardwirth::explorer {
namespace {

constexpr int kSampleRate = 44100;
constexpr int kChannels = 2;
constexpr int kMaximumCallbackFrames = 4096;
constexpr std::uint64_t kMaximumDurationMs = 15ULL * 60ULL * 1000ULL;
constexpr std::uint64_t kReleaseTailMs = 3000ULL;

float sanitized_gain(const float gain) noexcept {
    return std::isfinite(gain) ? std::clamp(gain, 0.0F, 1.0F) : 0.0F;
}

std::string path_text(const std::filesystem::path& path) {
    const auto encoded = path.u8string();
    return std::string(reinterpret_cast<const char*>(encoded.data()), encoded.size());
}

std::string lowercase_extension(const std::filesystem::path& path) {
    auto value = path_text(path.extension());
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::vector<unsigned char> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("ファイルを開けません: " + path_text(path));
    const auto length = input.tellg();
    if (length <= 0 || static_cast<std::uintmax_t>(length) >
                           static_cast<std::uintmax_t>(std::numeric_limits<int>::max()))
        throw std::runtime_error("MIDIまたはSoundFontのサイズが不正です");
    std::vector<unsigned char> result(static_cast<std::size_t>(length));
    input.seekg(0, std::ios::beg);
    input.read(reinterpret_cast<char*>(result.data()), length);
    if (!input) throw std::runtime_error("ファイルを読み取れません: " + path_text(path));
    return result;
}

struct MidiState {
    std::vector<unsigned char> soundfont_data;
    tsf* synth{};
    tml_message* messages{};
    tml_message* next_message{};
    tml_message* loop_message{};
    std::uint64_t loop_start_frame{};
    std::vector<const tml_message*> loop_setup;
    std::uint64_t rendered_frames{};
    std::uint64_t loop_end_frame{};
    std::uint64_t final_frame{};
    int repeats_remaining{};
    std::atomic<float> gain{1.0F};
    std::atomic_bool nonzero_pcm_logged{};
    std::array<std::int16_t,
               static_cast<std::size_t>(kMaximumCallbackFrames) * kChannels> scratch{};
    std::atomic_bool failed{};
    std::atomic_bool finished{};

    ~MidiState() {
        if (messages) tml_free(messages);
        if (synth) tsf_close(synth);
    }
};

void apply_message(tsf* synth, const tml_message& message) noexcept {
    const auto channel = static_cast<int>(message.channel);
    switch (message.type) {
        case TML_PROGRAM_CHANGE:
            (void)tsf_channel_set_presetnumber(
                synth, channel, static_cast<unsigned char>(message.program), channel == 9);
            break;
        case TML_NOTE_ON:
            (void)tsf_channel_note_on(synth, channel,
                static_cast<unsigned char>(message.key),
                static_cast<unsigned char>(message.velocity) / 127.0F);
            break;
        case TML_NOTE_OFF:
            tsf_channel_note_off(synth, channel, static_cast<unsigned char>(message.key));
            break;
        case TML_PITCH_BEND:
            (void)tsf_channel_set_pitchwheel(synth, channel, message.pitch_bend);
            break;
        case TML_CONTROL_CHANGE:
            (void)tsf_channel_midi_control(
                synth, channel, static_cast<unsigned char>(message.control),
                static_cast<unsigned char>(message.control_value));
            break;
        default:
            break;
    }
}

void reset_for_repeat(MidiState& state) noexcept {
    tsf_reset(state.synth);
    tsf_set_output(state.synth, TSF_STEREO_INTERLEAVED, kSampleRate, -6.0F);
    for (int channel = 0; channel < 16; ++channel)
        (void)tsf_channel_set_presetnumber(state.synth, channel, 0, channel == 9);
    // シーク時と同様にコントローラー・音色・ピッチの状態を復元するが、冒頭の
    // 音符は再演奏しない。 バンク選択やRPNのため、元のイベント順を保つ。
    for (const auto* message : state.loop_setup) apply_message(state.synth, *message);
    state.next_message = state.loop_message;
    state.rendered_frames = state.loop_start_frame;
}

bool repeat_if_requested(MidiState& state) noexcept {
    if (state.repeats_remaining == 0) return false;
    if (state.repeats_remaining > 0) --state.repeats_remaining;
    reset_for_repeat(state);
    return true;
}

void apply_gain_and_log(MidiState& state, const int frame_count) noexcept {
    const auto gain = sanitized_gain(state.gain.load(std::memory_order_relaxed));
    int peak = 0;
    const auto sample_count = static_cast<std::size_t>(frame_count) * kChannels;
    for (std::size_t index = 0; index < sample_count; ++index) {
        const auto scaled = static_cast<int>(std::lround(
            static_cast<float>(state.scratch[index]) * gain));
        state.scratch[index] = static_cast<std::int16_t>(std::clamp(
            scaled, static_cast<int>(std::numeric_limits<std::int16_t>::min()),
            static_cast<int>(std::numeric_limits<std::int16_t>::max())));
        peak = std::max(peak, std::abs(static_cast<int>(state.scratch[index])));
    }
#if defined(__ANDROID__) || defined(__linux__)
    if (peak > 0 && !state.nonzero_pcm_logged.exchange(true, std::memory_order_relaxed))
        SDL_Log("CardWirthAudio MIDI synthesized nonzero PCM: peak=%d gain=%.3f",
                peak, static_cast<double>(gain));
#else
    (void)peak;
#endif
}

bool put_audio_chunk(MidiState& state, SDL_AudioStream* stream,
                     const int frame_count) noexcept {
    return SDL_PutAudioStreamData(
        stream, state.scratch.data(),
        frame_count * static_cast<int>(sizeof(std::int16_t)) * kChannels);
}

void put_silence(MidiState& state, SDL_AudioStream* stream,
                 int frame_count) noexcept {
    while (frame_count > 0) {
        const int chunk = std::min(frame_count, kMaximumCallbackFrames);
        std::fill_n(state.scratch.data(),
                    static_cast<std::size_t>(chunk) * kChannels,
                    static_cast<std::int16_t>(0));
        if (!put_audio_chunk(state, stream, chunk)) return;
        frame_count -= chunk;
    }
}

void SDLCALL fill_midi_stream(void* userdata, SDL_AudioStream* stream,
                              int additional_amount, int) noexcept {
    if (!userdata || !stream || additional_amount <= 0) return;
    auto& state = *static_cast<MidiState*>(userdata);
    if (state.finished.load(std::memory_order_acquire)) return;
    constexpr int bytes_per_frame = static_cast<int>(sizeof(std::int16_t)) * kChannels;
    const int frame_count = additional_amount / bytes_per_frame +
        (additional_amount % bytes_per_frame != 0 ? 1 : 0);
    if (state.failed.load(std::memory_order_acquire)) {
        put_silence(state, stream, frame_count);
        return;
    }

    int remaining_frames = frame_count;
    try {
        while (remaining_frames > 0) {
            bool completed = false;
            const int chunk = std::min(remaining_frames, kMaximumCallbackFrames);
            std::fill_n(state.scratch.data(),
                        static_cast<std::size_t>(chunk) * kChannels,
                        static_cast<std::int16_t>(0));
            int output_frame = 0;
            while (output_frame < chunk) {
                while (state.next_message) {
                    const auto event_frame = static_cast<std::uint64_t>(state.next_message->time) *
                                             kSampleRate / 1000ULL;
                    if (event_frame > state.rendered_frames) break;
                    apply_message(state.synth, *state.next_message);
                    state.next_message = state.next_message->next;
                }

                // 余韻は最後の再生にだけ付け、各ループには付けない
                // 。 境界を正の値にすることで、長さ0の無限MIDI
                // がこのコールバック内で回り続けるのを防ぐ。
                const auto traversal_end = state.repeats_remaining != 0
                    ? state.loop_end_frame : state.final_frame;
                if (state.rendered_frames >= traversal_end) {
                    if (repeat_if_requested(state)) continue;
                    completed = true;
                    break;
                }
                const auto next_frame = state.next_message
                    ? std::min<std::uint64_t>(traversal_end,
                          static_cast<std::uint64_t>(state.next_message->time) *
                              kSampleRate / 1000ULL)
                    : traversal_end;
                const auto until_event = next_frame > state.rendered_frames
                    ? next_frame - state.rendered_frames : 1ULL;
                const auto frames = static_cast<int>(std::min<std::uint64_t>(
                    static_cast<std::uint64_t>(chunk - output_frame), until_event));
                tsf_render_short(state.synth,
                                 state.scratch.data() + output_frame * kChannels,
                                 frames, 0);
                output_frame += frames;
                state.rendered_frames += static_cast<std::uint64_t>(frames);
            }
            apply_gain_and_log(state, chunk);
            if (!put_audio_chunk(state, stream, chunk)) {
                state.failed.store(true, std::memory_order_release);
                return;
            }
            if (completed) {
                state.finished.store(true, std::memory_order_release);
                return;
            }
            remaining_frames -= chunk;
        }
    } catch (...) {
        state.failed.store(true, std::memory_order_release);
        put_silence(state, stream, remaining_frames);
    }
}

static_assert(noexcept(fill_midi_stream(nullptr, nullptr, 0, 0)));

}  // namespace

struct MidiPlayer::Impl {
    Impl(std::filesystem::path directory, std::filesystem::path preferred)
        : data_directory(std::move(directory)), preferred_soundfont(std::move(preferred)) {
        if (preferred_soundfont.empty() && !data_directory.empty())
            preferred_soundfont = data_directory / "SoundFont" /
                "005.6mg_Aspirin_Stereo_V1.2_Bank.sf2";
    }
    ~Impl() { stop(); }

    void stop() noexcept {
        if (stream) {
            SDL_DestroyAudioStream(stream);
            stream = nullptr;
        }
        state.reset();
    }

    std::filesystem::path data_directory;
    std::filesystem::path preferred_soundfont;
    std::unique_ptr<MidiState> state;
    SDL_AudioStream* stream{};
};

MidiPlayer::MidiPlayer(std::filesystem::path data_directory,
                       std::filesystem::path preferred_soundfont)
    : impl_(std::make_unique<Impl>(std::move(data_directory),
                                   std::move(preferred_soundfont))) {}

MidiPlayer::~MidiPlayer() { stop(); }
MidiPlayer::MidiPlayer(MidiPlayer&&) noexcept = default;
MidiPlayer& MidiPlayer::operator=(MidiPlayer&&) noexcept = default;

std::optional<std::filesystem::path> MidiPlayer::find_soundfont() const {
    if (!impl_) return std::nullopt;
    std::error_code error;
    if (!impl_->preferred_soundfont.empty() &&
        lowercase_extension(impl_->preferred_soundfont) == ".sf2" &&
        std::filesystem::is_regular_file(impl_->preferred_soundfont, error) && !error)
        return impl_->preferred_soundfont;
    if (impl_->data_directory.empty()) return std::nullopt;
    error.clear();
    if (!std::filesystem::is_directory(impl_->data_directory, error) || error)
        return std::nullopt;
    error.clear();
    std::vector<std::filesystem::path> candidates;
    for (std::filesystem::recursive_directory_iterator it(
             impl_->data_directory,
             std::filesystem::directory_options::skip_permission_denied, error), end;
         it != end; it.increment(error)) {
        if (error) {
            error.clear();
            continue;
        }
        const auto status = it->symlink_status(error);
        if (error || std::filesystem::is_symlink(status)) {
            error.clear();
            if (std::filesystem::is_directory(status)) it.disable_recursion_pending();
            continue;
        }
        if (std::filesystem::is_regular_file(status) && lowercase_extension(it->path()) == ".sf2")
            candidates.push_back(it->path());
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
        return path_text(left) < path_text(right);
    });
    return candidates.empty() ? std::nullopt
                              : std::optional<std::filesystem::path>(candidates.front());
}

void MidiPlayer::play(const std::filesystem::path& midi_path, const int repeat_count,
                      const float gain) {
    const auto soundfont = find_soundfont();
    if (!soundfont)
        throw std::runtime_error("Data内にSoundFont (.sf2) がないためMIDIを再生できません");
    play_buffers(read_file(midi_path), read_file(*soundfont), repeat_count, gain,
                 path_text(midi_path), path_text(*soundfont));
}

void MidiPlayer::play_memory(const std::span<const std::byte> midi_data,
                             const std::span<const std::byte> soundfont_data,
                             const int repeat_count, const float gain,
                             const std::string_view midi_name,
                             const std::string_view soundfont_name) {
    const auto copy = [](const std::span<const std::byte> value) {
        std::vector<unsigned char> result(value.size());
        if (!value.empty()) std::memcpy(result.data(), value.data(), value.size());
        return result;
    };
    play_buffers(copy(midi_data), copy(soundfont_data), repeat_count, gain,
                 midi_name, soundfont_name);
}

void MidiPlayer::play_buffers(std::vector<unsigned char> midi_data,
                              std::vector<unsigned char> soundfont_data,
                              const int repeat_count, const float gain,
                              const std::string_view midi_name,
                              const std::string_view soundfont_name) {
    if (midi_data.empty() || soundfont_data.empty())
        throw std::runtime_error("MIDIまたはSoundFontのサイズが不正です");
    stop();
    auto state = std::make_unique<MidiState>();
    state->soundfont_data = std::move(soundfont_data);
    state->synth = tsf_load_memory(state->soundfont_data.data(),
                                   static_cast<int>(state->soundfont_data.size()));
    if (!state->synth) throw std::runtime_error("SoundFontを読み込めません: " + std::string(soundfont_name));

    state->messages = tml_load_memory(midi_data.data(), static_cast<int>(midi_data.size()));
    if (!state->messages) throw std::runtime_error("MIDIファイルを読み込めません");
    state->next_message = state->messages;
    state->repeats_remaining = std::max(-1, repeat_count);
    state->gain.store(sanitized_gain(gain), std::memory_order_relaxed);
    auto* last = state->messages;
    while (last->next) last = last->next;
    const auto duration_ms = std::min<std::uint64_t>(
        static_cast<std::uint64_t>(last->time) + kReleaseTailMs, kMaximumDurationMs);
    state->loop_end_frame = std::max<std::uint64_t>(1,
        std::min<std::uint64_t>(last->time, kMaximumDurationMs) * kSampleRate / 1000ULL);
    state->final_frame = duration_ms * kSampleRate / 1000ULL;
    state->loop_message = state->messages;
    // Python版のMIDI再生と同様に、外部のループ指定がなけれ
    // ばCC111をループ開始とする。 トラック内でマーカーより後の
    // メッセージだけでなく、同時刻の全イベントをシーク位置に含める。
    for (auto* marker = state->messages; marker; marker = marker->next) {
        if (marker->type != TML_CONTROL_CHANGE ||
            static_cast<unsigned char>(marker->control) != 111) continue;
        const auto start = static_cast<std::uint64_t>(marker->time) * kSampleRate / 1000ULL;
        if (start >= state->loop_end_frame) break; // 再生時間の上限内に、長さが正のループがない。
        state->loop_start_frame = start;
        auto* message = state->messages;
        for (; message && message->time < marker->time; message = message->next) {
            if (message->type == TML_PROGRAM_CHANGE || message->type == TML_CONTROL_CHANGE ||
                message->type == TML_PITCH_BEND) state->loop_setup.push_back(message);
        }
        state->loop_message = message;
        break;
    }
    tsf_set_output(state->synth, TSF_STEREO_INTERLEAVED, kSampleRate, -6.0F);
    for (int channel = 0; channel < 16; ++channel)
        (void)tsf_channel_set_presetnumber(state->synth, channel, 0, channel == 9);

    SDL_Log("CardWirthAudio MIDI prepared: midi=%s soundfont=%s repeats=%d gain=%.3f",
            std::string(midi_name).c_str(), std::string(soundfont_name).c_str(),
            state->repeats_remaining,
            static_cast<double>(state->gain.load(std::memory_order_relaxed)));

    const SDL_AudioSpec spec{SDL_AUDIO_S16, kChannels, kSampleRate};
    impl_->state = std::move(state);
    impl_->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
                                               fill_midi_stream, impl_->state.get());
    if (!impl_->stream) {
        impl_->state.reset();
        throw std::runtime_error("MIDI用音声デバイスを開けません: " + std::string(SDL_GetError()));
    }
    if (!SDL_ResumeAudioStreamDevice(impl_->stream)) {
        stop();
        throw std::runtime_error("MIDI再生を開始できません: " + std::string(SDL_GetError()));
    }
    SDL_Log("CardWirthAudio MIDI stream started: midi=%s",
            std::string(midi_name).c_str());
}

void MidiPlayer::set_volume(const float gain) noexcept {
    if (!impl_ || !impl_->state) return;
    impl_->state->gain.store(sanitized_gain(gain), std::memory_order_relaxed);
}

void MidiPlayer::pause() noexcept {
    if (!impl_ || !impl_->stream) return;
    (void)SDL_PauseAudioStreamDevice(impl_->stream);
}

void MidiPlayer::resume() noexcept {
    if (!impl_ || !impl_->stream) return;
    (void)SDL_ResumeAudioStreamDevice(impl_->stream);
}

void MidiPlayer::stop() noexcept {
    if (!impl_) return;
    impl_->stop();
}

bool MidiPlayer::collect_finished() noexcept {
    if (!impl_ || !impl_->state ||
        !impl_->state->finished.load(std::memory_order_acquire)) return false;
    impl_->stop();
    return true;
}

bool MidiPlayer::playing() const noexcept {
    return impl_ && impl_->stream && impl_->state &&
        !impl_->state->finished.load(std::memory_order_acquire) &&
        !impl_->state->failed.load(std::memory_order_acquire);
}

}  // namespace cardwirth::explorer
