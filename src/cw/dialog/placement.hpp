#pragma once

#include "cw/lib/compat.hpp"
#include <memory>
#include <optional>

namespace cw::dialog {

struct DialogCenterOffset { double x{}, y{}; };

// Py の Frame.move_dlg の点は、親の中心を基準とする論理的な
// wins() の点。モデルの要求を画像・配置の情報から分離して保持する。
void set_dialog_center_offset(const std::shared_ptr<::cw::lib::Object>& model,
                              ::cw::lib::Point logical_point);
std::optional<DialogCenterOffset> dialog_center_offset(
    const std::shared_ptr<::cw::lib::Object>& model);

// ネイティブウィンドウとホスト内の画像は同じ位置変換の規則を使う
// 。ホストは画像座標からウィンドウ座標への変換だけを提供する。
inline DialogCenterOffset translate_dialog_center(DialogCenterOffset origin,
    DialogCenterOffset offset, double transport_scale = 1.0) {
    return {origin.x + offset.x * transport_scale,
            origin.y + offset.y * transport_scale};
}

} // namespace cw::dialog
