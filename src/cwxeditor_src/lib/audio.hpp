#pragma once

#include "types.hpp"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace cwxeditor::lib {

enum class AudioBus { Bgm, SoundEffect, Preview };
enum class AudioBackendPreference { Automatic, Native, SDL, Bass, ExternalApplication };
enum class AudioCapability : std::uint32_t {
    None = 0U,
    Playback = 1U << 0U,
    Native = 1U << 1U,
    SDL = 1U << 2U,
    Bass = 1U << 3U,
    Midi = 1U << 4U,
    SoundFont = 1U << 5U,
    MemorySource = 1U << 6U,
    Pan = 1U << 7U,
    Fade = 1U << 8U
};

[[nodiscard]] constexpr AudioCapability operator|(AudioCapability left,
                                                  AudioCapability right) noexcept {
    return static_cast<AudioCapability>(static_cast<std::uint32_t>(left) |
                                        static_cast<std::uint32_t>(right));
}
[[nodiscard]] constexpr AudioCapability operator&(AudioCapability left,
                                                  AudioCapability right) noexcept {
    return static_cast<AudioCapability>(static_cast<std::uint32_t>(left) &
                                        static_cast<std::uint32_t>(right));
}
constexpr AudioCapability& operator|=(AudioCapability& left,
                                     AudioCapability right) noexcept {
    left = left | right;
    return left;
}
[[nodiscard]] constexpr bool has_audio_capability(AudioCapability value,
                                                  AudioCapability expected) noexcept {
    return (value & expected) == expected;
}

struct AudioSoundFont {
    std::filesystem::path path;
    std::uint32_t volume{100U};
    bool operator==(const AudioSoundFont&) const = default;
};

struct AudioConfiguration {
    AudioBackendPreference preference{AudioBackendPreference::Automatic};
    std::vector<std::filesystem::path> library_search_roots;
    std::vector<AudioSoundFont> sound_fonts;
    bool allow_fallback{true};
    bool operator==(const AudioConfiguration&) const = default;
};

struct AudioConfigurationResult {
    AudioBackendPreference requested{AudioBackendPreference::Automatic};
    AudioBackendPreference effective{AudioBackendPreference::Automatic};
    AudioCapability capabilities{AudioCapability::None};
    std::optional<std::filesystem::path> library_root;
    std::vector<AudioSoundFont> sound_fonts;
    bool used_fallback{};
    std::string error;

    [[nodiscard]] bool available() const noexcept {
        return has_audio_capability(capabilities, AudioCapability::Playback);
    }
};

// ネイティブ・SDL・ヘッドレスで同じ再生選択規則を使う。
// 実装側は能力だけを報告し、独自に製品の代替方針を決めない。
[[nodiscard]] AudioConfigurationResult select_audio_configuration(
    const AudioConfiguration& request,
    std::span<const AudioConfigurationResult> candidates);

struct AudioSource {
    std::filesystem::path path;
    std::span<const std::byte> encoded_data{};
    std::string format_hint;
    std::optional<std::filesystem::path> sound_font;
};

// 吉里吉里のSLI位置は44.1kHz基準のサンプル数。 バッ
// クエンドで復号入力の周波数が分かるまでは、その単位を保つ。
struct AudioLoopRegion {
    std::int64_t start_reference_samples_44100{};
    std::optional<std::int64_t> end_reference_samples_44100;
};

struct AudioPlayOptions {
    AudioBus bus{AudioBus::SoundEffect};
    AudioBackendPreference backend{AudioBackendPreference::Automatic};
    float volume{1.0F};
    float pan{};
    std::uint32_t loop_count{1};
    std::chrono::milliseconds loop_start{};
    std::chrono::milliseconds fade_in{};
    std::chrono::milliseconds start_position{};
    std::optional<AudioLoopRegion> loop_region;
    // DのplayBassのspLoop指定で、SLIとMIDI/Ogg内のループ情報の検索を有効にする。
    bool special_loop{};
};

class AudioBackend {
public:
    virtual ~AudioBackend() = default;
    virtual bool initialize(std::string& error) = 0;
    // 初期化前に実行時ファイルの有無を返す、従来の初期
    // 化要求を保存する。 動作中の選択バックエンドは解
    // 放するが、再生能力を新たに用意しない。 true
    // は必要な実行時ファイルがあることだけを示し、使用
    // 可能とみなす前にconfigure()が必要。
    [[nodiscard]] virtual bool defer_configuration(
        const AudioConfiguration&) noexcept { return false; }
    virtual AudioConfigurationResult configure(const AudioConfiguration& request) = 0;
    [[nodiscard]] virtual AudioConfigurationResult configuration() const = 0;
    virtual AudioHandle play(const AudioSource& source, const AudioPlayOptions& options) = 0;
    virtual void pause(AudioHandle handle) = 0;
    virtual void resume(AudioHandle handle) = 0;
    // セッションの表示状態。ユーザーによる個別トラックの一時停止とは分ける。
    virtual void set_suspended(bool) {}
    virtual void stop(AudioHandle handle, std::chrono::milliseconds fade_out = {}) noexcept = 0;
    virtual void stop_bus(AudioBus bus) noexcept = 0;
    virtual void stop_all() noexcept = 0;
    virtual void seek(AudioHandle handle, std::chrono::milliseconds position) = 0;
    [[nodiscard]] virtual std::chrono::milliseconds position(AudioHandle handle) const = 0;
    [[nodiscard]] virtual std::chrono::milliseconds duration(AudioHandle handle) const = 0;
    [[nodiscard]] virtual bool playing(AudioHandle handle) const = 0;
    // initialize()前の呼び出しでも出力全体の音量を保持し、
    // 初回プレビュー前に設定した値を後から始まるトラックへ適用する。
    virtual void set_bus_volume(AudioBus bus, float value) noexcept = 0;
    // 再生中のフェードインを目標音量で止める。 独立したフェ
    // ード係数を持たないバックエンドでは、何もしなくてよい。
    virtual void cancel_fade_in(AudioHandle handle) noexcept { (void)handle; }
    // 個別トラックの音量変更は出力全体より狭い範囲に作用させ、 Editor
    // のプレビューで同じ出力を共有する無関係な音楽を変えない。
    virtual void set_track_volume(AudioHandle handle, float value) noexcept {
        (void)handle;
        (void)value;
    }
    [[nodiscard]] virtual std::vector<std::string> supported_formats() const = 0;
};

}  // namespace cwxeditor::lib
