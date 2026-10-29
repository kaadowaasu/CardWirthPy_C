#pragma once

#include "cwxeditor_src/core/cwx/summary.hpp"

namespace cwxeditor::cwx::editor::gui::dwt::summaryskinbinding {

/// Dのカード画面と同じく、読み取り専用・パスなしのSummaryでは元のスキンを保つ。
[[nodiscard]] inline bool pinsSummarySkin(
    const bool readOnly, const ::cwxeditor::cwx::summary::Summary* summary) noexcept {
    return readOnly || !summary || summary->scenarioPath().empty();
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::summaryskinbinding
