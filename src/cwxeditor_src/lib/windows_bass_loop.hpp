#pragma once

#include "audio_loop.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

namespace cwxeditor::lib::windows_bass {

// WindowsBassAudioBackendが使うBASSの同期フラグ。ル
// ープ境界からネイティブの登録引数への変換を、副作用のない共通の計画処理に集約
// し、Windows向けテストでBASSへ渡す値を正確に確認できるようにする。
inline constexpr std::uint32_t kSyncPosition = 0U;
inline constexpr std::uint32_t kSyncEnd = 2U;
inline constexpr std::uint32_t kSyncMixTime = 0x40000000U;

struct StreamLayout final {
    int frequency{};
    std::uint32_t channels{};
    std::uint32_t bytes_per_sample{};
};

struct SyncRegistration final {
    std::uint32_t flags{};
    std::uint64_t position_bytes{};
    bool operator==(const SyncRegistration&) const = default;
};

struct LoopPlan final {
    std::uint64_t start_byte{};
    bool repeat_forever{};
    std::uint32_t repetitions_remaining{};
    std::array<SyncRegistration, 2> registrations{};
    std::size_t registration_count{};
};

// D互換のBASSループ境界と、正確な同期登録内容を解決する。明示的なSLI情報
// を埋め込みのMIDI/Ogg情報より優先する。special_loopは両方
// の入力でDのplayBassのspLoop条件に対応する。同期APIがない場
// 合や単発再生では、呼び出し側の通常のストリーム全体の繰り返し経路を維持する。
[[nodiscard]] inline std::optional<LoopPlan> plan_loop(
    const AudioPlayOptions& options, const bool midi, const StreamLayout layout,
    const std::span<const AudioMidiControlEvent> midi_events,
    const std::span<const std::string_view> vorbis_comments,
    const bool sync_available) noexcept {
    if (!options.special_loop || layout.frequency <= 0 || layout.channels == 0U ||
        layout.bytes_per_sample == 0U)
        return std::nullopt;

    const auto convert_region = [layout](const AudioLoopRegion& region)
        -> std::optional<std::pair<std::uint64_t, std::optional<std::uint64_t>>> {
        const auto start = audio_reference_samples_to_bytes(
            region.start_reference_samples_44100, layout.frequency, layout.channels,
            layout.bytes_per_sample);
        if (!start) return std::nullopt;
        std::optional<std::uint64_t> end;
        if (region.end_reference_samples_44100) {
            const auto converted = audio_reference_samples_to_bytes(
                *region.end_reference_samples_44100, layout.frequency, layout.channels,
                layout.bytes_per_sample);
            if (!converted) return std::nullopt;
            end = *converted;
        }
        return std::pair{*start, end};
    };

    std::optional<std::uint64_t> start_byte;
    std::optional<std::uint64_t> end_byte;
    if (options.loop_region) {
        const auto bounds = convert_region(*options.loop_region);
        if (!bounds) return std::nullopt;
        start_byte = bounds->first;
        end_byte = bounds->second;
    } else {
        if (midi) {
            if (const auto position = audio_midi_loop_start_byte(midi_events))
                start_byte = static_cast<std::uint64_t>(*position);
        }
        // DはMIDIのCC#111の後にOggタグを調べるため、両方がある
        // 場合は完全で有効なOgg領域を優先する。Oggタグが不完全なら有効
        // なMIDIマーカーを維持する。そうしないと、DではLOOPLENGTH
        // の既定値-1により、開始位置より前の終了位置を計算し得る。
        if (const auto region = audio_vorbis_loop_region(vorbis_comments)) {
            if (const auto bounds = convert_region(*region)) {
                start_byte = bounds->first;
                end_byte = bounds->second;
            }
        }
    }
    if (!start_byte) return std::nullopt;

    LoopPlan plan;
    plan.start_byte = *start_byte;
    // sound.dはuint入力をint32_tのloopCountsへ
    // 代入する。符号ビットの立った値は非正となり、bassLoopは正の
    // 回数だけを減らすため無限に繰り返す。このD側の境界動作を維持する。
    const auto signed_loop_count = std::bit_cast<std::int32_t>(options.loop_count);
    plan.repeat_forever = signed_loop_count <= 0;
    plan.repetitions_remaining = plan.repeat_forever || signed_loop_count == 1
        ? 0U : static_cast<std::uint32_t>(signed_loop_count - 1);
    if (!sync_available || signed_loop_count == 1)
        return plan;

    if (end_byte) {
        plan.registrations[plan.registration_count++] = {
            kSyncPosition | kSyncMixTime, *end_byte};
    }
    plan.registrations[plan.registration_count++] = {kSyncEnd | kSyncMixTime, 0U};
    return plan;
}

}  // namespace cwxeditor::lib::windows_bass
