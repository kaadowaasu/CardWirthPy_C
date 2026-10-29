#pragma once

#include "cw/lib/ui_surface.hpp"

namespace cw::dialog {
class DialogPresentationProvider;
::cw::lib::Integer richtext_wheel_scroll_delta(::cw::lib::Integer rotation);
void dispatch_dialog_wheel(DialogPresentationProvider& provider,float x,float y,::cw::lib::Integer rotation);
// Py の CWPyRichTextCtrl.OnMotion は、下端そのものでは
// スクロールしない。共通コントロールと表示側で wins(4) の規則を共有する。
inline ::cw::lib::Integer richtext_drag_scroll_delta(
    ::cw::lib::Real relative_y, ::cw::lib::Real client_height)
{
    if (relative_y < 0) return -::cw::lib::ui::win_i(4);
    if (relative_y > client_height) return ::cw::lib::ui::win_i(4);
    return 0;
}
}
