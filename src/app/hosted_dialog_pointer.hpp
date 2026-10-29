#pragma once

#include "app/engine_runtime.hpp"

namespace cardwirth::app {

// カードのダイアログはリピートタイマーの動作中に、ボタンのDOWNで表示先を
// 変える。 その再描画中もキャプチャを保持して、同じモデルへUPを届ける。
// モデル自体の置き換え時は、公開前にゲームスレッドでキャプチャを取り消す。
inline void synchronize_hosted_dialog_pointer(
    ::cw::dialog::DialogPointerCapture& capture, std::uint64_t& current_model,
    const std::optional<HostedEngineModalSnapshot>& next) {
    const auto next_model = next ? next->model_id : 0;
    if (next_model != current_model) capture.reset();
    current_model = next_model;
}

} // namespace cardwirth::app
