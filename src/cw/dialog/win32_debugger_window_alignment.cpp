#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <iterator>
#include <string_view>

namespace {

using DwmGetWindowAttributeFn = HRESULT (WINAPI*)(HWND, DWORD, PVOID, DWORD);

DwmGetWindowAttributeFn dwm_get_window_attribute() noexcept
{
    static const HMODULE module = LoadLibraryW(L"dwmapi.dll");
    static const auto function = module
        ? reinterpret_cast<DwmGetWindowAttributeFn>(
              GetProcAddress(module, "DwmGetWindowAttribute"))
        : nullptr;
    return function;
}

bool visible_frame_bounds(HWND window, RECT& rect) noexcept
{
    // DWMWA_EXTENDED_FRAME_BOUNDS の値。 この小さな配置アダプ
    // ターに dwmapi のインポートライブラリー依存を追加しないため、数値で保持する。
    constexpr DWORD extended_frame_bounds = 9;
    if (const auto get_window_attribute = dwm_get_window_attribute();
        get_window_attribute &&
        SUCCEEDED(get_window_attribute(
            window, extended_frame_bounds, &rect, sizeof(rect)))) {
        return true;
    }
    return GetWindowRect(window, &rect) != FALSE;
}

bool is_standalone_debugger(HWND window, HWND& owner) noexcept
{
    if (!window || !IsWindow(window)) return false;

    wchar_t title[128]{};
    if (GetWindowTextW(window, title, static_cast<int>(std::size(title))) <= 0 ||
        std::wstring_view(title) != L"CardWirthPy Debugger") {
        return false;
    }

    owner = GetWindow(window, GW_OWNER);
    if (!owner || !IsWindow(owner)) return false;

    wchar_t owner_class[128]{};
    if (GetClassNameW(owner, owner_class,
                      static_cast<int>(std::size(owner_class))) <= 0 ||
        std::wstring_view(owner_class) != L"CardWirthSDLFrame") {
        return false;
    }
    return true;
}

void align_visible_horizontal_edges(HWND debugger, HWND owner) noexcept
{
    RECT owner_visible{};
    RECT debugger_visible{};
    RECT debugger_outer{};
    if (!visible_frame_bounds(owner, owner_visible) ||
        !visible_frame_bounds(debugger, debugger_visible) ||
        !GetWindowRect(debugger, &debugger_outer)) {
        return;
    }

    // SDL は既に表示側を選び、ウィンドウを作業領域内へ収めて
    // いる。 Y 座標と左右の選択は維持し、見える隙間の原因とな
    // る Win32/DWM の不可視の左右枠だけを補正する。
    const long long owner_center =
        static_cast<long long>(owner_visible.left) + owner_visible.right;
    const long long debugger_center =
        static_cast<long long>(debugger_visible.left) + debugger_visible.right;
    const bool on_right = debugger_center >= owner_center;

    const int delta_x = on_right
        ? owner_visible.right - debugger_visible.left
        : owner_visible.left - debugger_visible.right;
    if (delta_x == 0) return;

    SetWindowPos(debugger, nullptr,
                 debugger_outer.left + delta_x,
                 debugger_outer.top,
                 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER |
                     SWP_NOOWNERZORDER);
}

void CALLBACK debugger_window_shown(HWINEVENTHOOK, DWORD event, HWND window,
                                    LONG object_id, LONG child_id,
                                    DWORD, DWORD) noexcept
{
    if (event != EVENT_OBJECT_SHOW || object_id != OBJID_WINDOW ||
        child_id != CHILDID_SELF) {
        return;
    }

    HWND owner{};
    if (!is_standalone_debugger(window, owner)) return;
    align_visible_horizontal_edges(window, owner);
}

class DebuggerWindowAlignmentHook final {
public:
    DebuggerWindowAlignmentHook() noexcept
        : hook_(SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW,
                                nullptr, debugger_window_shown,
                                GetCurrentProcessId(), 0,
                                WINEVENT_OUTOFCONTEXT)) {}

    ~DebuggerWindowAlignmentHook()
    {
        if (hook_) UnhookWinEvent(hook_);
    }

    DebuggerWindowAlignmentHook(const DebuggerWindowAlignmentHook&) = delete;
    DebuggerWindowAlignmentHook& operator=(const DebuggerWindowAlignmentHook&) = delete;

private:
    HWINEVENTHOOK hook_{};
};

DebuggerWindowAlignmentHook debugger_window_alignment_hook;

} // namespace

#endif // _WIN32
