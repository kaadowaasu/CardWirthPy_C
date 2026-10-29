#pragma once

#include "cw/lib/compat.hpp"

namespace cardwirth::app {

// 画像の所有者に対し、共有ランタイムのトランザクション内で呼び出す。 動作中の画像を
// const shared_ptrで参照しても、不変のUIスナップショットにはなら
// ない。 変更がなければ前の独立した画像を再利用し、ホストのテクスチャを維持する。
inline std::shared_ptr<const ::cw::lib::Surface> freeze_hosted_image(
    const std::shared_ptr<const ::cw::lib::Surface>& source,
    const std::shared_ptr<const ::cw::lib::Surface>& previous = {}) {
    if (!source) return {};
    if (previous && previous.get() != source.get() &&
        previous->size.width == source->size.width &&
        previous->size.height == source->size.height &&
        previous->scr_scale == source->scr_scale &&
        previous->bmpdepthis1 == source->bmpdepthis1 &&
        previous->has_colorkey == source->has_colorkey &&
        previous->colorkey == source->colorkey &&
        previous->depth1 == source->depth1 &&
        previous->logical_scale == source->logical_scale &&
        previous->known_opaque == source->known_opaque &&
        previous->surface_alpha == source->surface_alpha &&
        previous->rgba == source->rgba) return previous;
    return std::make_shared<const ::cw::lib::Surface>(*source);
}

inline std::shared_ptr<const ::cw::lib::Surface> freeze_hosted_status_image(
    const std::shared_ptr<::cw::lib::Surface>& source,
    const std::shared_ptr<const ::cw::lib::Surface>& previous = {}) {
    return freeze_hosted_image(source, previous);
}

} // namespace cardwirth::app
