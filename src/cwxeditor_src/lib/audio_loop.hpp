#pragma once

#include "audio.hpp"
#include <charconv>
#include <fstream>
#include <limits>
#include <span>
#include <string_view>

namespace cwxeditor::lib {

// 中間の積をあふれさせずに変換し、ミリ秒未満の位置も保つ。
[[nodiscard]] inline std::optional<std::int64_t> audio_reference_samples_to_frames(
    std::int64_t samples, int sample_rate) noexcept {
    if (samples < 0 || sample_rate <= 0) return std::nullopt;
    constexpr std::int64_t reference_rate = 44100;
    const auto whole = samples / reference_rate;
    const auto remainder = (samples % reference_rate) * static_cast<std::int64_t>(sample_rate)
        / reference_rate;
    const auto maximum = std::numeric_limits<std::int64_t>::max();
    if (whole > (maximum - remainder) / sample_rate) return std::nullopt;
    return whole * sample_rate + remainder;
}


struct AudioMidiControlEvent {
    std::uint32_t control{};
    std::int64_t position_bytes{};
};

// DのRPG Maker用MIDIループ印は、ストリーム順で最後のCC#111とする。
[[nodiscard]] inline std::optional<std::int64_t> audio_midi_loop_start_byte(
    const std::span<const AudioMidiControlEvent> events) noexcept {
    std::optional<std::int64_t> result;
    for (const auto& event : events)
        if (event.control == 111U && event.position_bytes >= 0)
            result = event.position_bytes;
    return result;
}

[[nodiscard]] inline std::optional<AudioLoopRegion> audio_vorbis_loop_region(
    const std::span<const std::string_view> comments) noexcept {
    std::optional<std::int64_t> start;
    std::optional<std::int64_t> length;
    for (auto comment : comments) {
        const auto equal = comment.find('=');
        if (equal == std::string_view::npos) continue;
        auto key = comment.substr(0, equal);
        auto value = comment.substr(equal + 1U);
        const auto trim = [](std::string_view text) {
            constexpr std::string_view whitespace{" \t\r\n"};
            const auto first = text.find_first_not_of(whitespace);
            if (first == std::string_view::npos) return std::string_view{};
            return text.substr(first, text.find_last_not_of(whitespace) - first + 1U);
        };
        key = trim(key);
        value = trim(value);
        if (key != "LOOPSTART" && key != "LOOPLENGTH") continue;
        std::int64_t parsed{};
        const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
        if (value.empty() || result.ec != std::errc{} ||
            result.ptr != value.data() + value.size() || parsed < 0)
            continue;
        if (key == "LOOPSTART") start = parsed;
        else length = parsed;
    }
    // D版との差異。getLoopInfoはLOOPSTARTだけ
    // の場合にLOOPLENGTHを-1のまま使い、 開始より前
    // の終点を計算する。要求範囲外にBASSの位置同期を設定しな
    // いよう、 C++では不完全な組を通常の全体再生として扱う。
    if (!start || !length ||
        *length > std::numeric_limits<std::int64_t>::max() - *start)
        return std::nullopt;
    return AudioLoopRegion{*start, *start + *length};
}

[[nodiscard]] inline std::optional<std::uint64_t> audio_reference_samples_to_bytes(
    const std::int64_t samples, const int sample_rate, const std::uint32_t channels,
    const std::uint32_t bytes_per_sample) noexcept {
    if (samples < 0 || sample_rate <= 0 || channels == 0U || bytes_per_sample == 0U)
        return std::nullopt;
    const auto bytes_per_frame = static_cast<std::uint64_t>(channels) * bytes_per_sample;
    const auto maximum = static_cast<std::uint64_t>(
        std::numeric_limits<std::int64_t>::max());
    constexpr std::int64_t reference_rate = 44100;
    const auto whole_samples = samples / reference_rate;
    const auto partial_sample_rate = (samples % reference_rate) *
        static_cast<std::int64_t>(sample_rate);
    const auto rate = static_cast<std::uint64_t>(sample_rate);
    if (static_cast<std::uint64_t>(whole_samples) > maximum / rate)
        return std::nullopt;
    const auto whole_frames = static_cast<std::uint64_t>(whole_samples) * rate;
    const auto partial_frames = static_cast<std::uint64_t>(
        partial_sample_rate / reference_rate);
    if (whole_frames > maximum || partial_frames > maximum - whole_frames)
        return std::nullopt;
    const auto frames = whole_frames + partial_frames;
    const auto frame_remainder = static_cast<std::uint64_t>(
        partial_sample_rate % reference_rate);

    // DのgetLoopInfo.posToBytesと同じく、フレームあたりバ
    // イト数を掛けてから切り捨てる。 小数フレームの寄与を早い段階で捨てない。
    if (frames != 0U && bytes_per_frame > maximum / frames) return std::nullopt;
    const auto whole_bytes = frames * bytes_per_frame;
    constexpr std::uint64_t denominator = 44100U;
    const auto bytes_per_frame_whole = bytes_per_frame / denominator;
    const auto bytes_per_frame_remainder = bytes_per_frame % denominator;
    if (frame_remainder != 0U &&
        bytes_per_frame_whole > (maximum - whole_bytes) / frame_remainder)
        return std::nullopt;
    const auto fractional_whole_bytes = frame_remainder * bytes_per_frame_whole;
    const auto fractional_partial_bytes =
        frame_remainder * bytes_per_frame_remainder / denominator;
    if (fractional_partial_bytes > maximum - whole_bytes - fractional_whole_bytes)
        return std::nullopt;
    return whole_bytes + fractional_whole_bytes + fractional_partial_bytes;
}

// Dのsound.getLoopInfoと同じくLink行を読み、Toを開始、From
// を終点とする。 付随ファイルがない・不正な場合も、通常再生は可能にする。
[[nodiscard]] inline std::optional<AudioLoopRegion> read_audio_loop_region(
    const std::filesystem::path& audio_path) {
    auto sidecar = audio_path;
    sidecar += ".sli";
    std::ifstream input(sidecar);
    if (!input) return std::nullopt;
    const auto trim = [](std::string_view value) {
        constexpr auto whitespace = " \t\r\n\f\v";
        const auto first = value.find_first_not_of(whitespace);
        if (first == std::string_view::npos) return std::string_view{};
        return value.substr(first, value.find_last_not_of(whitespace) - first + 1);
    };
    const auto number = [&](std::string_view value) -> std::optional<std::int64_t> {
        value = trim(value);
        if (!value.empty() && value.front() == '+') value.remove_prefix(1);
        if (value.empty()) return std::nullopt;
        std::int64_t result{};
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
        if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
            return std::nullopt;
        return result;
    };
    std::optional<std::int64_t> end;
    std::string line;
    while (std::getline(input, line)) {
        auto text = trim(line);
        if (!text.starts_with("Link")) continue;
        text.remove_prefix(4);
        const auto open = text.find('{');
        const auto close = text.rfind('}');
        if (open == std::string_view::npos || close == std::string_view::npos || close <= open)
            continue;
        auto body = text.substr(open + 1, close - open - 1);
        std::optional<std::int64_t> start;
        while (!body.empty()) {
            const auto separator = body.find(';');
            const auto field = body.substr(0, separator);
            const auto equal = field.find('=');
            if (equal != std::string_view::npos) {
                const auto key = trim(field.substr(0, equal));
                const auto value = number(field.substr(equal + 1));
                if (key == "To") start = value;
                else if (key == "From") end = value;
            }
            if (separator == std::string_view::npos) break;
            body.remove_prefix(separator + 1);
        }
        if (start && *start >= 0) {
            if (end && *end < 0) end.reset();
            return AudioLoopRegion{*start, end};
        }
    }
    return std::nullopt;
}

} // namespace cwxeditor::lib
