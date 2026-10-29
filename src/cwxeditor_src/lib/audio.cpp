#include "audio.hpp"

#include <array>

namespace cwxeditor::lib {
namespace {

[[nodiscard]] bool matches(const AudioBackendPreference requested,
                           const AudioConfigurationResult& candidate) noexcept {
    if (!candidate.available()) return false;
    if (requested == AudioBackendPreference::Automatic) return true;
    return candidate.effective == requested;
}

}  // namespace

AudioConfigurationResult select_audio_configuration(
    const AudioConfiguration& request,
    const std::span<const AudioConfigurationResult> candidates) {
    if (request.preference != AudioBackendPreference::Automatic) {
        for (const auto& candidate : candidates) {
            if (!matches(request.preference, candidate)) continue;
            auto selected = candidate;
            selected.requested = request.preference;
            selected.used_fallback = false;
            return selected;
        }
    }
    if (request.allow_fallback || request.preference == AudioBackendPreference::Automatic) {
        constexpr std::array order{
            AudioBackendPreference::Native,
            AudioBackendPreference::SDL,
            AudioBackendPreference::Bass,
            AudioBackendPreference::Automatic};
        for (const auto preferred : order) {
            for (const auto& candidate : candidates) {
                if (!candidate.available() || candidate.effective != preferred) continue;
                auto selected = candidate;
                selected.requested = request.preference;
                selected.used_fallback = request.preference != selected.effective;
                return selected;
            }
        }
    }
    AudioConfigurationResult unavailable;
    unavailable.requested = request.preference;
    unavailable.effective = request.preference;
    unavailable.error = "requested audio backend is unavailable";
    for (const auto& candidate : candidates) {
        if (!candidate.error.empty()) {
            unavailable.error = candidate.error;
            break;
        }
    }
    return unavailable;
}

}  // namespace cwxeditor::lib
