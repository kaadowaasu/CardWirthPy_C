#include "sdl_ui.hpp"
#include "utf8.hpp"
#include "sdl_modifier_policy.hpp"
#include "platform/app_visibility.hpp"
#include "sdl_font_provider.hpp"
#include "sdl_pen_input.hpp"
#include "image_composition.hpp"
#include "image_mask.hpp"
#include "file_picker.hpp"
#include "drawing_geometry.hpp"
#include "drawing_clip.hpp"
#include "ui_decoration_presentation.hpp"
#include "dialog_dispatch.hpp"
#include "editor_diagnostics.hpp"
#include "portable_async.hpp"
#include "portable_runtime.hpp"
#include "runtime_context.hpp"
#include "ui_layout.hpp"
#include "ui_actions.hpp"
#include "ui_drag_drop.hpp"
#include "ui_external_drop.hpp"
#include "ui_touch_input.hpp"
#include "ui_chrome_input.hpp"
#include "ui_mouse_input.hpp"
#include "ui_passive_input.hpp"
#include "ui_pointer_input.hpp"
#include "ui_window_stack.hpp"
#include "ui_activation.hpp"
#include "ui_edit_focus.hpp"
#include "ui_edit_input.hpp"
#include "ui_input_publication.hpp"
#include "ui_value_dialogs.hpp"
#include "ui_window_lifecycle.hpp"
#include "ui_window_maximize.hpp"
#include "ui_frame_presentation.hpp"
#include "ui_frame_replay.hpp"
#include "ui_frame_updates.hpp"
#include "ui_surface_lifetime.hpp"
#include "ui_accessibility.hpp"
#include "ui_metrics.hpp"
#include "ui_text_layout.hpp"

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <imm.h>
#include <shellapi.h>
#include <shlobj.h>
#endif
#include "ui_number_edit.hpp"
#include "ui_combo_edit.hpp"
#include "ui_inline_edit.hpp"
#include "ui_scroll.hpp"
#include "window_host.hpp"
#if defined(__APPLE__)
#include "apple_accessibility.hpp"
#endif

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

#if defined(__ANDROID__) || defined(__EMSCRIPTEN__) || defined(__APPLE__)
#include "app/explorer/platform_transfer.hpp"
#endif

#if defined(__ANDROID__)
#include <jni.h>
#endif

#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#include "app/explorer/platform_transfer_apple.hpp"
#include <CoreFoundation/CoreFoundation.h>
#endif
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <list>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <mutex>
#include <numeric>
#include <span>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

namespace cwxeditor::lib {
namespace {
#if defined(__EMSCRIPTEN__)
void publish_web_clipboard_text(std::string_view value) {
    MAIN_THREAD_EM_ASM({
        const text = UTF8ToString($0, $1);
        const active = document.activeElement;
        let copied = false;
        try {
            const field = document.createElement('textarea');
            field.value = text;
            field.setAttribute('readonly', '');
            field.style.cssText = 'position:fixed;left:-9999px;top:0;opacity:0;';
            document.body.appendChild(field);
            field.focus();
            field.select();
            copied = document.execCommand('copy');
            field.remove();
        } catch (_) {}
        if (active && typeof active.focus === 'function') active.focus();
        if (!copied && navigator.clipboard && navigator.clipboard.writeText) {
            try {
                const pending = navigator.clipboard.writeText(text);
                if (pending && pending.catch) pending.catch(() => {});
            } catch (_) {}
        }
    }, value.data(), value.size());
}
#endif
enum class SurfacePresent {
    HostedNoPresent,
    Standalone,
};

struct TimingSeries {
    void add(const std::chrono::steady_clock::duration elapsed) {
        const auto us = static_cast<std::uint64_t>(std::max<std::int64_t>(0,
            std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count()));
        total_us += us;
        maximum_us = std::max(maximum_us, us);
        if (samples.size() < 1024U) samples.push_back(us);
        else {
            samples[next_sample] = us;
            next_sample = (next_sample + 1U) % samples.size();
        }
        ++count;
    }
    [[nodiscard]] std::uint64_t percentile(const std::size_t numerator) const {
        if (samples.empty()) return 0U;
        auto ordered = samples;
        std::sort(ordered.begin(), ordered.end());
        const auto index = std::min(ordered.size() - 1U,
            (ordered.size() - 1U) * numerator / 100U);
        return ordered[index];
    }
    [[nodiscard]] std::string describe(const std::string_view name) const {
        std::ostringstream out;
        out << name << "_count=" << count
            << ' ' << name << "_avg_us=" << (count ? total_us / count : 0U)
            << ' ' << name << "_p50_us=" << percentile(50U)
            << ' ' << name << "_p95_us=" << percentile(95U)
            << ' ' << name << "_p99_us=" << percentile(99U)
            << ' ' << name << "_max_us=" << maximum_us;
        return out.str();
    }
    std::vector<std::uint64_t> samples;
    std::uint64_t count{}, total_us{}, maximum_us{};
    std::size_t next_sample{};
};

struct FrameProfile {
    [[nodiscard]] std::string describe() const {
        std::ostringstream out;
        out << "Editor presentation profile " << prepare.describe("prepare") << ' '
            << layout.describe("layout") << ' ' << drawing.describe("drawing") << ' '
            << raster.describe("raster") << ' ' << present.describe("present")
            << " completed=" << completed << " rejected=" << rejected
            << " replayed=" << replayed;
        return out.str();
    }
    bool enabled{};
    TimingSeries prepare, layout, drawing, raster, present;
    std::uint64_t completed{}, rejected{}, replayed{};
};

[[nodiscard]] bool presentation_profile_enabled() noexcept {
    if (editor_diagnostics_enabled()) return true;
    const char* const value = SDL_getenv("CWXEDITOR_PROFILE_PRESENTATION");
    return value && *value && std::string_view{value} != "0" &&
           std::string_view{value} != "false";
}

void configure_standalone_present_mode(SDL_Renderer* renderer,
                                       const EditorPresentMode mode) noexcept {
    if (!renderer) return;
    switch (mode) {
    case EditorPresentMode::Immediate:
        (void)SDL_SetRenderVSync(renderer, SDL_RENDERER_VSYNC_DISABLED);
        return;
    case EditorPresentMode::VSync:
        (void)SDL_SetRenderVSync(renderer, 1);
        return;
    case EditorPresentMode::Adaptive:
        // SDLドライバーの適応型同期は任意の機能。 利用できない場合
        // は従来の同期動作を保ち、黙ってティアリングを有効にしない。
        if (!SDL_SetRenderVSync(renderer, SDL_RENDERER_VSYNC_ADAPTIVE))
            (void)SDL_SetRenderVSync(renderer, 1);
        return;
    }
}

[[nodiscard]] std::string_view present_mode_name(const EditorPresentMode mode) noexcept {
    switch (mode) {
    case EditorPresentMode::VSync: return "vsync";
    case EditorPresentMode::Adaptive: return "adaptive";
    case EditorPresentMode::Immediate: return "immediate";
    }
    return "unknown";
}

#if defined(_WIN32)
[[nodiscard]] HWND native_window_handle(SDL_Window* window) noexcept {
    if (!window) return nullptr;
    return static_cast<HWND>(SDL_GetPointerProperty(
        SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER,
        nullptr));
}

[[nodiscard]] std::optional<int> native_ime_mode(SDL_Window* window) noexcept {
    const auto handle = native_window_handle(window);
    if (!handle) return {};
    const auto context = ::ImmGetContext(handle);
    if (!context) return {};
    DWORD conversion{}, sentence{};
    const bool read = ::ImmGetConversionStatus(context, &conversion, &sentence) != FALSE;
    ::ImmReleaseContext(handle, context);
    return read ? std::optional<int>{static_cast<int>(conversion)} : std::nullopt;
}

void apply_native_ime_mode(SDL_Window* window, const int mode) noexcept {
    const auto handle = native_window_handle(window);
    if (!handle) return;
    const auto context = ::ImmGetContext(handle);
    if (!context) return;
    DWORD conversion{}, sentence{};
    if (::ImmGetConversionStatus(context, &conversion, &sentence))
        (void)::ImmSetConversionStatus(context, static_cast<DWORD>(mode), sentence);
    ::ImmReleaseContext(handle, context);
}
#else
[[nodiscard]] std::optional<int> native_ime_mode(SDL_Window*) noexcept { return {}; }
void apply_native_ime_mode(SDL_Window*, int) noexcept {}
#endif

constexpr SDL_Color kText{24, 24, 24, 255};
constexpr SDL_Color kDisabled{130, 130, 130, 255};
constexpr SDL_Color kBorder{145, 145, 145, 255};
constexpr SDL_Color kSelection{49, 106, 197, 255};

[[nodiscard]] SDL_FRect sdl_rect(Rect r) noexcept {
    return SDL_FRect{r.x, r.y, std::max(0.0F, r.width), std::max(0.0F, r.height)};
}

[[nodiscard]] constexpr PointerButton sdl_pointer_button(Uint8 value) noexcept {
    switch(value) {
    case SDL_BUTTON_LEFT: return PointerButton::Primary;
    case SDL_BUTTON_MIDDLE: return PointerButton::Middle;
    case SDL_BUTTON_RIGHT: return PointerButton::Secondary;
    case SDL_BUTTON_X1: return PointerButton::Back;
    case SDL_BUTTON_X2: return PointerButton::Forward;
    default: return PointerButton::None;
    }
}
static_assert(sdl_pointer_button(SDL_BUTTON_X1)==PointerButton::Back);
static_assert(sdl_pointer_button(SDL_BUTTON_X2)==PointerButton::Forward);
static_assert(sdl_pointer_button(0)==PointerButton::None && sdl_pointer_button(255)==PointerButton::None);

[[nodiscard]] std::optional<ExternalDropEvent> sdl_external_drop(const SDL_Event& event) {
    ExternalDropEvent input;
    switch (event.type) {
    case SDL_EVENT_DROP_BEGIN: input.phase = ExternalDropPhase::Begin; break;
    case SDL_EVENT_DROP_POSITION: input.phase = ExternalDropPhase::Position; break;
    case SDL_EVENT_DROP_COMPLETE: input.phase = ExternalDropPhase::Complete; break;
    case SDL_EVENT_DROP_FILE:
        input.phase = ExternalDropPhase::Data;
        if (event.drop.data && *event.drop.data) {
            // SDL3 は通知の間だけUTF-8領域を貸す。 Windows
            // のロケールを使わずに複製し、SDLのデータ自体は解放しない。
            const std::string_view path(event.drop.data);
#if defined(__EMSCRIPTEN__)
            std::error_code path_error;
            const auto resolved = std::filesystem::canonical(
                std::filesystem::u8path(path), path_error).generic_string();
            if (path_error || (resolved != "/user" && !resolved.starts_with("/user/"))) {
                // 外部からのブラウザードロップは、一時アップロードのパスを使う。 エ
                // ディタの文書は、選択された永続的な提供側から取得する必要がある。
                input.phase = ExternalDropPhase::Cancel;
                break;
            }
#endif
            input.data.files.emplace_back(std::u8string(path.begin(), path.end()));
        }
        break;
    case SDL_EVENT_DROP_TEXT:
        input.phase = ExternalDropPhase::Data;
        if (event.drop.data) input.data.text = event.drop.data;
        break;
    default: return std::nullopt;
    }
    input.surface = event.drop.windowID;
    if (event.type != SDL_EVENT_DROP_BEGIN)
        input.position = Point{event.drop.x, event.drop.y};
    return input;
}

[[nodiscard]] detail::SdlModifierSnapshot native_modifier_snapshot(const SDL_Keymod value) noexcept {
#if defined(__APPLE__) || defined(__EMSCRIPTEN__)
    return detail::sdl_modifier_snapshot(value, true);
#else
    return detail::sdl_modifier_snapshot(value, false);
#endif
}

[[nodiscard]] Modifiers sdl_modifiers(const SDL_Keymod value) noexcept {
    return native_modifier_snapshot(value).modifiers;
}

[[nodiscard]] bool has_modifier(const Modifiers value, const Modifiers wanted) noexcept {
    return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(wanted)) != 0U;
}

[[nodiscard]] Key sdl_key(const SDL_Keycode value) noexcept {
    if (value >= SDLK_A && value <= SDLK_Z)
        return static_cast<Key>(static_cast<int>(Key::A) + static_cast<int>(value - SDLK_A));
    if (value >= SDLK_0 && value <= SDLK_9)
        return static_cast<Key>(static_cast<int>(Key::Digit0) + static_cast<int>(value - SDLK_0));
    // SDLのスキャンコードでは、F12とF13の間に移動キーがある。 F1～F15を
    // 一つの範囲と扱うと、Home/Delete/矢印が無関係な列挙値になってしまう。
    if (value >= SDLK_F1 && value <= SDLK_F12)
        return static_cast<Key>(static_cast<int>(Key::F1) + static_cast<int>(value - SDLK_F1));
    if (value >= SDLK_F13 && value <= SDLK_F15)
        return static_cast<Key>(static_cast<int>(Key::F13) + static_cast<int>(value - SDLK_F13));
    switch (value) {
    case SDLK_BACKSPACE: return Key::Backspace;
    case SDLK_TAB: return Key::Tab;
    case SDLK_RETURN: case SDLK_KP_ENTER: return Key::Enter;
    // Androidのシステムの戻る操作は AC_BACK で届く。 エデ
    // ィタの既存のキャンセル・戻るキーとして扱い、 物理Escapeキーが
    // なくてもすべてのモーダル画面・ポップアップを閉じられるようにする。
    case SDLK_ESCAPE: case SDLK_AC_BACK: return Key::Escape;
    case SDLK_SPACE: return Key::Space;
    case SDLK_INSERT: return Key::Insert;
    case SDLK_DELETE: return Key::Delete;
    case SDLK_HOME: return Key::Home;
    case SDLK_END: return Key::End;
    case SDLK_PAGEUP: return Key::PageUp;
    case SDLK_PAGEDOWN: return Key::PageDown;
    case SDLK_LEFT: return Key::Left;
    case SDLK_RIGHT: return Key::Right;
    case SDLK_UP: return Key::Up;
    case SDLK_DOWN: return Key::Down;
    case SDLK_LSHIFT: case SDLK_RSHIFT: return Key::Shift;
    case SDLK_LCTRL: case SDLK_RCTRL: return Key::Control;
    case SDLK_LALT: case SDLK_RALT: return Key::Alt;
    case SDLK_LGUI: case SDLK_RGUI: return Key::Meta;
    default: return Key::Unknown;
    }
}

[[nodiscard]] std::string sdl_printable_key(const SDL_Keycode value) {
    const char* const name = SDL_GetKeyName(value);
    if (!name) return {};
    const std::string_view text(name);
    if (text.size() != 1U || !std::isprint(static_cast<unsigned char>(text.front())))
        return {};
    return std::string(text);
}

[[nodiscard]] std::string sdl_key_character(const SDL_Scancode scancode,
                                            const SDL_Keymod modifiers) {
    // key_event=false は、現在のキーボード配列と修飾キーの状態を適用する。 イベントのキ
    // ーコードと SDL_GetKeyName() はラベルであり、修飾キー適用後の入力文字ではない。
    const auto key = SDL_GetKeyFromScancode(scancode, modifiers, false);
    if (key == SDLK_UNKNOWN ||
        (key & (SDLK_SCANCODE_MASK | SDLK_EXTENDED_MASK)) != 0U ||
        key == 0xFFFDU) return {};
    const auto scalar = static_cast<std::uint32_t>(key);
    if (!utf8::is_scalar(scalar) || scalar < 0x20U ||
        (scalar >= 0x7FU && scalar <= 0x9FU)) return {};
    // SDLのWindows用キーマップは ToUnicode から作られ、
    // デッドキーの場合も最初のスカラーを保持することがある。 これは監視側の
    // 直接の代替処理だけに使い、確定文字やIMEの合成結果としては扱わない。
    return utf8::encode_scalar(scalar);
}

[[nodiscard]] std::string mnemonic_text(std::string_view value) {
    return display_text_without_mnemonic(value);
}

class SdlGraphicsBackend final : public GraphicsBackend {
public:
    explicit SdlGraphicsBackend(std::shared_ptr<SdlFontProvider> fonts) : fonts_(std::move(fonts)) {}
    ResourceId create_image(const ImageData& image) override {
        const ResourceId id = ++next_;
        images_[id] = std::make_shared<const ImageData>(image);
        return id;
    }
    void update_image(ResourceId id, const ImageData& image) override {
        images_[id] = std::make_shared<const ImageData>(image);
    }
    void destroy_image(ResourceId id) noexcept override { images_.erase(id); }
    std::vector<std::string> font_families() const override { return {"system-ui", "sans-serif"}; }
    Size measure_text(std::string_view value, const FontSpec& font, float wrap_width) override {
        return fonts_->measure_text(value,font,wrap_width);
    }
    FontMetrics font_metrics(const FontSpec& font) override {return fonts_->font_metrics(font);}
    std::optional<GlyphPath> glyph_path(char32_t scalar,const FontSpec& font) override {return fonts_->glyph_path(scalar,font);}
    bool draw_text_to_image(ImageData& image,std::size_t frame,std::string_view value,
                            Rect area,const FontSpec& font,Color color) override;
    ResourceId load_application_icon(const std::filesystem::path& executable,
                                     const Size requested_size) override {
        const auto pixels = extract_application_icon_data(executable, requested_size);
        if (pixels.frames.empty()) return 0;
        try { return create_image(pixels); } catch (...) { return 0; }
    }
    ImageData extract_application_icon_data(const std::filesystem::path& executable,
                                     const Size requested_size) override {
#if defined(_WIN32)
        if (executable.empty() || !std::isfinite(requested_size.width) ||
            !std::isfinite(requested_size.height) || requested_size.width <= 0.0F ||
            requested_size.height <= 0.0F || requested_size.width > 4096.0F ||
            requested_size.height > 4096.0F) return {};
        const int width = static_cast<int>(std::lround(requested_size.width));
        const int height = static_cast<int>(std::lround(requested_size.height));
        if (width <= 0 || height <= 0) return {};

        SHFILEINFOW file_info{};
        const auto icon_result = SHGetFileInfoW(
            executable.c_str(), 0, &file_info, sizeof(file_info),
            SHGFI_ICON | SHGFI_SMALLICON);
        struct WinIconResources {
            HICON icon{};
            HDC dc{};
            HBITMAP bitmap{};
            HBITMAP color_bitmap{};
            HBITMAP mask_bitmap{};
            HGDIOBJ previous_bitmap{};
            ~WinIconResources() {
                if (dc && previous_bitmap && previous_bitmap != HGDI_ERROR)
                    (void)SelectObject(dc, previous_bitmap);
                if (bitmap) (void)DeleteObject(bitmap);
                if (color_bitmap) (void)DeleteObject(color_bitmap);
                if (mask_bitmap) (void)DeleteObject(mask_bitmap);
                if (dc) (void)DeleteDC(dc);
                if (icon) (void)DestroyIcon(icon);
            }
        } resources{file_info.hIcon};
        if (!icon_result || !resources.icon) return {};
        resources.dc = CreateCompatibleDC(nullptr);
        if (!resources.dc) return {};

        BITMAPINFO bitmap_info{};
        bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmap_info.bmiHeader.biWidth = width;
        bitmap_info.bmiHeader.biHeight = -height;
        bitmap_info.bmiHeader.biPlanes = 1;
        bitmap_info.bmiHeader.biBitCount = 32;
        bitmap_info.bmiHeader.biCompression = BI_RGB;
        void* dib_pixels{};
        resources.bitmap = CreateDIBSection(resources.dc, &bitmap_info, DIB_RGB_COLORS,
                                            &dib_pixels, nullptr, 0);
        if (!resources.bitmap || !dib_pixels) return {};
        resources.previous_bitmap = SelectObject(resources.dc, resources.bitmap);
        if (!resources.previous_bitmap || resources.previous_bitmap == HGDI_ERROR) return {};

        const auto pixel_count = static_cast<std::size_t>(width) *
                                 static_cast<std::size_t>(height);
        const auto byte_count = pixel_count * 4U;
        std::memset(dib_pixels, 0, byte_count);
        if (!DrawIconEx(resources.dc, 0, 0, resources.icon, width, height,
                        0, nullptr, DI_NORMAL)) return {};

        try {
            ImageData image;
            image.format = ImageFormat::Icon;
            image.source_transparency.kind = ImageTransparency::AlphaData;
            ImageFrame frame;
            frame.logical_size = requested_size;
            frame.pixel_width = static_cast<std::uint32_t>(width);
            frame.pixel_height = static_cast<std::uint32_t>(height);
            frame.pixel_format = PixelFormat::BGRA32;
            frame.pixels.resize(byte_count);
            std::memcpy(frame.pixels.data(), dib_pixels, byte_count);
            auto* const channels = reinterpret_cast<std::uint8_t*>(frame.pixels.data());
            bool has_alpha = false;
            for (std::size_t offset = 0; offset < byte_count; offset += 4U)
                has_alpha = has_alpha || channels[offset + 3U] != 0U;
            if (has_alpha) {
                // DrawIconEx は32ビットのアルファ付きアイコンに AC_SRC_OVER を
                // 使う。 SDL画像はストレートアルファなので、アップロード前に乗算済みの色を元へ戻す。
                for (std::size_t offset = 0; offset < byte_count; offset += 4U) {
                    const auto alpha = channels[offset + 3U];
                    if (alpha == 0U) {
                        channels[offset] = channels[offset + 1U] = channels[offset + 2U] = 0U;
                    } else if (alpha < 255U) {
                        for (std::size_t channel = 0; channel < 3U; ++channel) {
                            const auto straight = (static_cast<unsigned>(channels[offset + channel]) * 255U +
                                                   alpha / 2U) / alpha;
                            channels[offset + channel] = static_cast<std::uint8_t>(
                                std::min(255U, straight));
                        }
                    }
                }
            } else {
                // 古いアイコンは、32ビットDIBのアルファ値ではなく
                // ANDマスクに透明情報を持つ。 上半分のマスクを読み
                // 、DrawIconEx と同じ指定サイズに合わせる。
                ICONINFO icon_info{};
                if (!GetIconInfo(resources.icon, &icon_info)) return {};
                resources.color_bitmap = icon_info.hbmColor;
                resources.mask_bitmap = icon_info.hbmMask;
                if (!resources.mask_bitmap) return {};
                BITMAP mask_bitmap{};
                if (!GetObjectW(resources.mask_bitmap, sizeof(mask_bitmap), &mask_bitmap) ||
                    mask_bitmap.bmWidth <= 0 || mask_bitmap.bmHeight <= 0 ||
                    mask_bitmap.bmWidth > 4096 || mask_bitmap.bmHeight > 8192 ||
                    (!resources.color_bitmap && (mask_bitmap.bmHeight % 2) != 0)) return {};
                const int mask_height = mask_bitmap.bmHeight;
                const int source_height = resources.color_bitmap
                    ? mask_height : mask_height / 2;
                const std::size_t mask_stride =
                    ((static_cast<std::size_t>(mask_bitmap.bmWidth) + 31U) / 32U) * 4U;
                std::vector<std::byte> mask_pixels(mask_stride *
                    static_cast<std::size_t>(mask_height));
                struct MonochromeBitmapInfo {
                    BITMAPINFOHEADER header{};
                    RGBQUAD colors[2]{};
                } mask_info{};
                mask_info.header.biSize = sizeof(BITMAPINFOHEADER);
                mask_info.header.biWidth = mask_bitmap.bmWidth;
                mask_info.header.biHeight = -mask_height;
                mask_info.header.biPlanes = 1;
                mask_info.header.biBitCount = 1;
                mask_info.header.biCompression = BI_RGB;
                mask_info.header.biSizeImage = static_cast<DWORD>(mask_pixels.size());
                mask_info.header.biClrUsed = 2;
                mask_info.colors[0] = {0, 0, 0, 0};
                mask_info.colors[1] = {255, 255, 255, 0};
                const auto scan_lines = GetDIBits(resources.dc, resources.mask_bitmap,
                    0, static_cast<UINT>(mask_height), mask_pixels.data(),
                    reinterpret_cast<BITMAPINFO*>(&mask_info), DIB_RGB_COLORS);
                if (scan_lines != mask_height) return {};
                for (int y = 0; y < height; ++y) {
                    const auto source_y = static_cast<std::size_t>(y) *
                                          static_cast<std::size_t>(source_height) /
                                          static_cast<std::size_t>(height);
                    for (int x = 0; x < width; ++x) {
                        const auto source_x = static_cast<std::size_t>(x) *
                                              static_cast<std::size_t>(mask_bitmap.bmWidth) /
                                              static_cast<std::size_t>(width);
                        const auto mask_byte = std::to_integer<unsigned char>(
                            mask_pixels[source_y * mask_stride + source_x / 8U]);
                        const bool transparent =
                            (mask_byte & (0x80U >> (source_x % 8U))) != 0U;
                        const auto offset = (static_cast<std::size_t>(y) *
                            static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4U;
                        channels[offset + 3U] = transparent ? 0U : 255U;
                        if (transparent)
                            channels[offset] = channels[offset + 1U] = channels[offset + 2U] = 0U;
                    }
                }
            }
            image.frames.push_back(std::move(frame));
            return image;
        } catch (...) {
            return {};
        }
#else
        (void)executable;
        (void)requested_size;
        return {};
#endif
    }
    const ImageData* image(ResourceId id) const {
        const auto found = images_.find(id);
        return found == images_.end() ? nullptr : found->second.get();
    }
    std::shared_ptr<const ImageData> retain_image(ResourceId id) const {
        const auto found=images_.find(id);
        return found==images_.end() ? nullptr : found->second;
    }
private:
    std::shared_ptr<SdlFontProvider> fonts_;
    ResourceId next_{};
    // 更新は世代を置き換える。準備済みの Canvas フレームは
    // 古い不変の画素を保持し、 再描画のたびに全画像を複製しない。
    std::unordered_map<ResourceId, std::shared_ptr<const ImageData>> images_;
};

struct ImageSurfaceDeleter {
    void operator()(SDL_Surface* value) const noexcept {
        if (value) SDL_DestroySurface(value);
    }
};

struct ImageAnimationDecoderDeleter {
    void operator()(IMG_AnimationDecoder* value) const noexcept {
        if (value) IMG_CloseAnimationDecoder(value);
    }
};
struct ImageStreamDeleter {
    void operator()(SDL_IOStream* value) const noexcept {
        if(value)SDL_CloseIO(value);
    }
};

using ImageSurface = std::unique_ptr<SDL_Surface, ImageSurfaceDeleter>;

[[nodiscard]] ImageSurface create_window_input_mask_surface(const WindowInputMask& mask) {
    const auto count=std::uint64_t(mask.pixel_width)*mask.pixel_height;
    if(!mask.pixel_width || !mask.pixel_height ||
       count!=mask.opaque_pixels.size() ||
       mask.pixel_width>std::uint32_t(std::numeric_limits<int>::max()) ||
       mask.pixel_height>std::uint32_t(std::numeric_limits<int>::max())) return {};
    ImageSurface surface(SDL_CreateSurface(static_cast<int>(mask.pixel_width),
        static_cast<int>(mask.pixel_height),SDL_PIXELFORMAT_RGBA32));
    if(!surface || !SDL_LockSurface(surface.get())) return {};
    auto* pixels=static_cast<std::uint8_t*>(surface->pixels);
    for(std::uint32_t y=0;y<mask.pixel_height;++y)
        for(std::uint32_t x=0;x<mask.pixel_width;++x) {
            const auto source=static_cast<std::size_t>(y)*mask.pixel_width+x;
            auto* pixel=pixels+static_cast<std::size_t>(y)*surface->pitch+
                static_cast<std::size_t>(x)*4U;
            pixel[0]=pixel[1]=pixel[2]=255U;
            pixel[3]=mask.opaque_pixels[source] ? 255U : 0U;
        }
    SDL_UnlockSurface(surface.get());
    return surface;
}

using ImageAnimationDecoder =
    std::unique_ptr<IMG_AnimationDecoder, ImageAnimationDecoderDeleter>;
using ImageStream=std::unique_ptr<SDL_IOStream,ImageStreamDeleter>;

[[nodiscard]] std::string image_path_utf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

[[nodiscard]] ImageFormat image_format_for_request(const DecodeImageRequest& request) {
    if (request.format_hint != ImageFormat::Unknown) return request.format_hint;
    auto extension = request.path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    if (extension == ".bmp") return ImageFormat::Bitmap;
    if (extension == ".png" || extension == ".apng") return ImageFormat::Png;
    if (extension == ".jpg" || extension == ".jpeg") return ImageFormat::Jpeg;
    if (extension == ".gif") return ImageFormat::Gif;
    if (extension == ".webp") return ImageFormat::WebP;
    if (extension == ".ico") return ImageFormat::Icon;
    if (extension == ".cur" || extension == ".ani") return ImageFormat::Cursor;
    return ImageFormat::Unknown;
}

[[nodiscard]] SDL_IOStream* open_image_stream(const DecodeImageRequest& request) {
    if (!request.encoded_data.empty())
        return SDL_IOFromConstMem(request.encoded_data.data(), request.encoded_data.size());
    if (request.path.empty()) return nullptr;
    const auto path = image_path_utf8(request.path);
    return SDL_IOFromFile(path.c_str(), "rb");
}

// 保持するのは、IDATより前の選択された付加情報だけ。 大きい補助チャンクは
// 同じ所有ストリーム上で飛ばし、画素の復号は SDL_image に任せる。
[[nodiscard]] ImageSourceTransparency png_transparency_from_stream(SDL_IOStream* stream) {
    const auto start=SDL_TellIO(stream);
    if(start<0)return {};
    struct Restore { SDL_IOStream* stream; Sint64 position; ~Restore(){SDL_SeekIO(stream,position,SDL_IO_SEEK_SET);} } restore{stream,start};
    std::array<std::byte,8> signature{};
    if(SDL_ReadIO(stream,signature.data(),signature.size())!=signature.size())return {};
    constexpr std::array<std::byte,8> expected{std::byte{137},std::byte{80},std::byte{78},std::byte{71},std::byte{13},std::byte{10},std::byte{26},std::byte{10}};
    if(signature!=expected)return {};
    std::vector<std::byte> metadata(signature.begin(),signature.end());
    for(unsigned chunks=0;chunks<4096;++chunks) {
        std::array<std::byte,8> header{};
        if(SDL_ReadIO(stream,header.data(),header.size())!=header.size())return {};
        const auto word=[&](unsigned i){return (std::uint32_t(std::to_integer<unsigned>(header[i]))<<24)|(std::uint32_t(std::to_integer<unsigned>(header[i+1]))<<16)|(std::uint32_t(std::to_integer<unsigned>(header[i+2]))<<8)|std::to_integer<unsigned>(header[i+3]);};
        const auto length=word(0),tag=word(4);
        if(chunks==0 && tag!=0x49484452U)return {};
        if(tag==0x49444154U) {
            metadata.insert(metadata.end(),header.begin(),header.end());metadata.resize(metadata.size()+4);
            return png_source_transparency(metadata);
        }
        const bool needed=tag==0x49484452U||tag==0x504c5445U||tag==0x74524e53U||tag==0x6163544cU;
        if(needed) {
            if(length>768 || metadata.size()+length+12>4096)return {};
            metadata.insert(metadata.end(),header.begin(),header.end());
            const auto position=metadata.size();metadata.resize(position+length+4);
            if(SDL_ReadIO(stream,metadata.data()+position,length+4)!=length+4)return {};
        } else {
            if(tag==0x49454e44U || !(tag&0x20000000U) || SDL_SeekIO(stream,Sint64(length)+4,SDL_IO_SEEK_CUR)<0)return {};
        }
    }
    return {};
}

// パレットの透明インデックスは、復号されたどの画素にも使われていない場
// 合がある。 SDL_ConvertSurface がパレットを捨て
// る前に元のRGBを取得し、 アルファゼロの出力画素から推定しない。
void retain_native_palette_key(SDL_Surface* surface,ImageSourceTransparency& source) {
    if(source.kind!=ImageTransparency::PaletteKey || !source.palette_index)return;
    if(const auto* palette=SDL_GetSurfacePalette(surface);palette && *source.palette_index<palette->ncolors) {
        const auto color=palette->colors[*source.palette_index];
        source.key=Color{color.r,color.g,color.b,255};
    }
}

[[nodiscard]] ImageFrame image_frame_from_surface(
    SDL_Surface* source, const std::optional<Size>& requested_size,
    std::chrono::milliseconds duration, std::string& error,
    bool png_high8 = false) {
    ImageFrame result;
    if (!source || source->w <= 0 || source->h <= 0) {
        error = "SDL_image returned an invalid surface";
        return result;
    }
    // DWTの PNGFileFormat.d と同様、アルファも含めて各値の下位
    // バイトを捨てる。 SDLの ARRAYU16 から RGBA32 への変換は
    // 丸めるため、縮小後のtRNSの一致関係が変わる。 実際に復号したPNGサーフ
    // ェスを一度正規化し、二つ目のデコーダーや8ビット出力からの推測は使わない。
    const bool high16 = png_high8 &&
        (source->format == SDL_PIXELFORMAT_RGB48 || source->format == SDL_PIXELFORMAT_RGBA64);
    ImageSurface converted(high16 ? SDL_CreateSurface(source->w, source->h, SDL_PIXELFORMAT_RGBA32)
        : SDL_ConvertSurface(source, SDL_PIXELFORMAT_RGBA32));
    if (converted && high16) {
        if (!SDL_LockSurface(source)) {
            error = std::string("SDL PNG sample lock failed: ") + SDL_GetError();
            return result;
        }
        const unsigned channels = source->format == SDL_PIXELFORMAT_RGBA64 ? 4U : 3U;
        for (int y = 0; y < source->h; ++y) {
            const auto* input = static_cast<const std::byte*>(source->pixels) + std::size_t(y) * source->pitch;
            auto* output = static_cast<std::byte*>(converted->pixels) + std::size_t(y) * converted->pitch;
            for (int x = 0; x < source->w; ++x) {
                for (unsigned c = 0; c < channels; ++c) {
                    Uint16 sample{};
                    std::memcpy(&sample, input + (std::size_t(x) * channels + c) * sizeof(sample), sizeof(sample));
                    // 同梱の IMG_libpng.c はRGBA64をネイティブのバイト
                    // 順に変えるが、 RGB48はPNGのビッグエンディアンのままにする。
                    output[std::size_t(x) * 4U + c] = channels == 3U
                        ? input[(std::size_t(x) * channels + c) * sizeof(sample)]
                        : std::byte(sample >> 8);
                }
                if (channels == 3U) output[std::size_t(x) * 4U + 3U] = std::byte{255};
            }
        }
        SDL_UnlockSurface(source);
    }
    if (!converted) {
        error = std::string("SDL image conversion failed: ") + SDL_GetError();
        return result;
    }
    if (requested_size && requested_size->width > 0.0F && requested_size->height > 0.0F) {
        const int width = std::max(1, static_cast<int>(std::lround(requested_size->width)));
        const int height = std::max(1, static_cast<int>(std::lround(requested_size->height)));
        if (width != converted->w || height != converted->h) {
            ImageSurface scaled(SDL_ScaleSurface(converted.get(), width, height,
                                                 SDL_SCALEMODE_LINEAR));
            if (!scaled) {
                error = std::string("SDL image scaling failed: ") + SDL_GetError();
                return result;
            }
            converted = std::move(scaled);
        }
    }
    if (!SDL_LockSurface(converted.get())) {
        error = std::string("SDL image lock failed: ") + SDL_GetError();
        return result;
    }
    const std::size_t row_size = static_cast<std::size_t>(converted->w) * 4U;
    result.pixel_width = static_cast<std::uint32_t>(converted->w);
    result.pixel_height = static_cast<std::uint32_t>(converted->h);
    result.logical_size = {static_cast<float>(converted->w), static_cast<float>(converted->h)};
    result.pixel_format = PixelFormat::RGBA32;
    result.duration = duration;
    result.pixels.resize(row_size * static_cast<std::size_t>(converted->h));
    for (int row = 0; row < converted->h; ++row) {
        const auto* input = static_cast<const std::byte*>(converted->pixels) +
                            static_cast<std::size_t>(row) * converted->pitch;
        std::memcpy(result.pixels.data() + static_cast<std::size_t>(row) * row_size,
                    input, row_size);
    }
    SDL_UnlockSurface(converted.get());
    error.clear();
    return result;
}

bool SdlGraphicsBackend::draw_text_to_image(ImageData& image,std::size_t frame,std::string_view value,
                                           Rect area,const FontSpec& font,Color color) {
    if(frame>=image.frames.size() || !std::isfinite(area.x) || !std::isfinite(area.y) ||
       !std::isfinite(area.width) || !std::isfinite(area.height) ||
       area.width<=0 || area.height<=0) return false;
    if(value.empty()) return true;
    auto* selected=fonts_->font_for(font);
    if(!selected) return false;
    const auto native_color=SDL_Color{color.red,color.green,color.blue,color.alpha};
    ImageSurface glyphs(font.antialias
        ? TTF_RenderText_Blended_Wrapped(selected,value.data(),value.size(),native_color,
                                        SdlFontProvider::wrap_pixels(area.width))
        : TTF_RenderText_Solid_Wrapped(selected,value.data(),value.size(),native_color,
                                      SdlFontProvider::wrap_pixels(area.width)));
    if(!glyphs) return false;
    std::string error;
    const auto pixels=image_frame_from_surface(glyphs.get(),{},std::chrono::milliseconds{},error);
    return error.empty() && composite_image_pixels(image.frames[frame],pixels,{area.x,area.y},area);
}

class SdlImageCodecBackend final : public ImageCodecBackend {
public:
    ImageData decode(const DecodeImageRequest& request, std::string& error) override {
        ImageData result;
        result.format = image_format_for_request(request);
        // D版のスコープで所有する入力ストリームに合わせ、所有権はここに保つ。 ア
        // ニメーションのデコーダーは自動終了が有効でも、 BMP・JPEG・未知の
        // 入力などで種類が不足していると、所有権を引き取る前に拒否する場合がある。
        ImageStream stream(open_image_stream(request));
        if (!stream) {
            error = std::string("SDL image stream creation failed: ") + SDL_GetError();
            return {};
        }
        result.source_transparency=png_transparency_from_stream(stream.get());
        const bool known_png=result.source_transparency.kind!=ImageTransparency::Unknown;
        if(known_png)result.format=ImageFormat::Png;
        // 通常のPNGは、既存の静止画デコーダーを一度だけ使う。 APNG
        // は合成済みのRGBAフレーム、表示時間、ループ情報を保持する。
        const bool static_png=known_png && !result.source_transparency.animated;
        SDL_PropertiesID properties = static_png ? 0 : SDL_CreateProperties();
        if (properties) {
            SDL_SetPointerProperty(properties,
                                   IMG_PROP_ANIMATION_DECODER_CREATE_IOSTREAM_POINTER, stream.get());
            SDL_SetBooleanProperty(properties,
                                   IMG_PROP_ANIMATION_DECODER_CREATE_IOSTREAM_AUTOCLOSE_BOOLEAN, false);
            SDL_SetNumberProperty(properties,
                                  IMG_PROP_ANIMATION_DECODER_CREATE_TIMEBASE_NUMERATOR_NUMBER, 1);
            SDL_SetNumberProperty(properties,
                                  IMG_PROP_ANIMATION_DECODER_CREATE_TIMEBASE_DENOMINATOR_NUMBER, 1000);
            const char* type = result.format == ImageFormat::WebP ? "webp" :
                               result.format == ImageFormat::Gif ? "gif" :
                               result.format == ImageFormat::Cursor ? "ani" :
                               result.format == ImageFormat::Png ? "png" : nullptr;
            if (type)
                SDL_SetStringProperty(properties,
                                      IMG_PROP_ANIMATION_DECODER_CREATE_TYPE_STRING, type);
        }
        ImageAnimationDecoder decoder(properties
            ? IMG_CreateAnimationDecoderWithProperties(properties) : nullptr);
        if (properties) SDL_DestroyProperties(properties);
        if (decoder) {
            if(known_png)result.source_transparency.kind=ImageTransparency::AlphaData;
            const SDL_PropertiesID metadata = IMG_GetAnimationDecoderProperties(decoder.get());
            if (metadata && SDL_HasProperty(metadata, IMG_PROP_METADATA_LOOP_COUNT_NUMBER))
                result.loop_count = static_cast<std::uint32_t>(std::max<Sint64>(
                    0, SDL_GetNumberProperty(metadata, IMG_PROP_METADATA_LOOP_COUNT_NUMBER, 0)));
            for (;;) {
                SDL_Surface* raw_frame{};
                Uint64 duration{};
                if (!IMG_GetAnimationDecoderFrame(decoder.get(), &raw_frame, &duration)) break;
                ImageSurface surface(raw_frame);
                auto frame = image_frame_from_surface(
                    surface.get(), request.requested_size,
                    std::chrono::milliseconds(std::max<Uint64>(duration, 16U)), error);
                if (frame.pixels.empty()) return {};
                result.frames.push_back(std::move(frame));
            }
            if (IMG_GetAnimationDecoderStatus(decoder.get()) == IMG_DECODER_STATUS_FAILED) {
                error = std::string("SDL_image animation decode failed: ") + SDL_GetError();
                return {};
            }
            if (!result.frames.empty()) {
                error.clear();
                return result;
            }
        }

        // 所有するストリームを置き換える前に、それを借用するデコーダーを解放する。
        decoder.reset();
        stream.reset(open_image_stream(request));
        if (!stream) {
            error = std::string("SDL image stream recreation failed: ") + SDL_GetError();
            return {};
        }
        ImageSurface surface(IMG_Load_IO(stream.get(), false));
        if (!surface) {
            error = std::string("SDL_image could not decode the image: ") + SDL_GetError();
            return {};
        }
        retain_native_palette_key(surface.get(),result.source_transparency);
        auto frame = image_frame_from_surface(surface.get(), request.requested_size, {}, error,
                                              known_png);
        if (frame.pixels.empty()) return {};
        result.frames.push_back(std::move(frame));
        error.clear();
        return result;
    }
    bool encode(const ImageData& image, const EncodeImageRequest& request, std::string& error) override {
        if (image.frames.empty() || request.path.empty()) {
            error = "image frame or output path is empty";
            return false;
        }
        if (image.frames.size() > 1U) return encode_animation(image, request, error);
        const auto& frame = image.frames.front();
        if (frame.pixel_format != PixelFormat::RGBA32 || frame.pixels.empty()) {
            error = "SDL image encoder requires RGBA32 pixels"; return false;
        }
        SDL_Surface* surface = SDL_CreateSurfaceFrom(static_cast<int>(frame.pixel_width),
            static_cast<int>(frame.pixel_height), SDL_PIXELFORMAT_RGBA32,
            const_cast<std::byte*>(frame.pixels.data()), static_cast<int>(frame.pixel_width * 4U));
        if (!surface) { error = SDL_GetError(); return false; }
        const auto path = image_path_utf8(request.path);
        bool ok = false;
        switch (request.format) {
        case ImageFormat::Bitmap: ok = SDL_SaveBMP(surface, path.c_str()); break;
        case ImageFormat::Jpeg:
            ok = IMG_SaveJPG(surface, path.c_str(),
                static_cast<int>(std::lround(std::clamp(request.quality, 0.0F, 1.0F) * 100.0F)));
            break;
        case ImageFormat::WebP:
            ok = IMG_SaveWEBP(surface, path.c_str(),
                              std::clamp(request.quality, 0.0F, 1.0F) * 100.0F);
            break;
        case ImageFormat::Gif: ok = IMG_SaveGIF(surface, path.c_str()); break;
        case ImageFormat::Icon: ok = IMG_SaveICO(surface, path.c_str()); break;
        case ImageFormat::Cursor: ok = IMG_SaveCUR(surface, path.c_str()); break;
        case ImageFormat::Png:
        case ImageFormat::Unknown: ok = IMG_SavePNG(surface, path.c_str()); break;
        }
        SDL_DestroySurface(surface);
        error = ok ? std::string{} : std::string(SDL_GetError());
        return ok;
    }
    Size probe(std::span<const std::byte> encoded, ImageFormat hint) override {
        std::string error;
        DecodeImageRequest request; request.encoded_data = encoded; request.format_hint = hint;
        const auto data = decode(request, error);
        return data.frames.empty() ? Size{} : data.frames.front().logical_size;
    }

private:
    static bool encode_animation(const ImageData& image, const EncodeImageRequest& request,
                                 std::string& error) {
        const char* type = request.format == ImageFormat::WebP ? "webp" :
                           request.format == ImageFormat::Gif ? "gif" : nullptr;
        if (!type) {
            error = "the requested format does not support multiple frames";
            return false;
        }
        const auto path = image_path_utf8(request.path);
        SDL_PropertiesID properties = SDL_CreateProperties();
        if (!properties) {
            error = std::string("SDL animation properties creation failed: ") + SDL_GetError();
            return false;
        }
        SDL_SetStringProperty(properties,
                              IMG_PROP_ANIMATION_ENCODER_CREATE_FILENAME_STRING, path.c_str());
        SDL_SetStringProperty(properties,
                              IMG_PROP_ANIMATION_ENCODER_CREATE_TYPE_STRING, type);
        SDL_SetNumberProperty(properties, IMG_PROP_ANIMATION_ENCODER_CREATE_QUALITY_NUMBER,
                              static_cast<Sint64>(std::lround(
                                  std::clamp(request.quality, 0.0F, 1.0F) * 100.0F)));
        SDL_SetNumberProperty(properties,
                              IMG_PROP_ANIMATION_ENCODER_CREATE_TIMEBASE_NUMERATOR_NUMBER, 1);
        SDL_SetNumberProperty(properties,
                              IMG_PROP_ANIMATION_ENCODER_CREATE_TIMEBASE_DENOMINATOR_NUMBER, 1000);
        if (image.loop_count) {
            SDL_SetNumberProperty(properties, IMG_PROP_METADATA_LOOP_COUNT_NUMBER,
                                  static_cast<Sint64>(*image.loop_count));
            if (request.format == ImageFormat::WebP)
                SDL_SetStringProperty(properties, IMG_PROP_METADATA_TITLE_STRING, "CWXEditor_C");
        }
        IMG_AnimationEncoder* encoder = IMG_CreateAnimationEncoderWithProperties(properties);
        SDL_DestroyProperties(properties);
        if (!encoder) {
            error = std::string("SDL animation encoder creation failed: ") + SDL_GetError();
            return false;
        }
        bool success = true;
        for (const auto& frame : image.frames) {
            if (frame.pixel_format != PixelFormat::RGBA32 || frame.pixels.empty()) {
                error = "SDL animation encoder requires RGBA32 pixels";
                success = false;
                break;
            }
            ImageSurface surface(SDL_CreateSurfaceFrom(
                static_cast<int>(frame.pixel_width), static_cast<int>(frame.pixel_height),
                SDL_PIXELFORMAT_RGBA32, const_cast<std::byte*>(frame.pixels.data()),
                static_cast<int>(frame.pixel_width * 4U)));
            if (!surface || !IMG_AddAnimationEncoderFrame(
                    encoder, surface.get(),
                    static_cast<Uint64>(std::max<std::int64_t>(frame.duration.count(), 16)))) {
                error = std::string("SDL animation frame encoding failed: ") + SDL_GetError();
                success = false;
                break;
            }
        }
        if (!IMG_CloseAnimationEncoder(encoder)) {
            if (success)
                error = std::string("SDL animation finalization failed: ") + SDL_GetError();
            success = false;
        }
        if (!success) {
            std::error_code ignored;
            std::filesystem::remove(request.path, ignored);
            return false;
        }
        error.clear();
        return true;
    }
};


class SdlGuiBackend final : public GuiBackend {
    struct Native;
public:
    SdlGuiBackend(std::shared_ptr<SdlFontProvider> fonts,
                  std::shared_ptr<SdlGraphicsBackend> graphics,
                  std::shared_ptr<EventLoopDispatcher> dispatcher,
                  std::shared_ptr<DragDropStateMachine> drag_drop,
                  std::shared_ptr<SystemBackend> system,
                  std::shared_ptr<PlatformAccessibilityBackend> accessibility = {})
        : fonts_(std::move(fonts)), host_options_(editor_window_host_options()),
          present_mode_(editor_present_mode_from_environment()),
          dispatcher_(std::move(dispatcher)), graphics_(std::move(graphics)),
          drag_drop_(std::move(drag_drop)), system_(std::move(system)),
          accessibility_(std::move(accessibility)) {
        frame_profile_.enabled = presentation_profile_enabled();
        if (accessibility_) {
            accessibility_dispatch_lifetime_=std::make_shared<int>(0);
            const std::weak_ptr<int> lifetime=accessibility_dispatch_lifetime_;
            accessibility_dispatcher_=std::make_shared<AccessibilityActionDispatcher>();
            accessibility_dispatcher_->bind([this,lifetime](const std::uint64_t surface,
                                                             const AccessibilityActionRequest& request) {
                if (lifetime.expired() || !request.target) return false;
                const auto* native=native_for(static_cast<SDL_WindowID>(surface));
                if (!native || window_stack_.owner_of(request.target)!=native->model) return false;
                return request_accessibility_action(request);
            });
            // プラットフォームの転送側は弱参照で保持し、 ネイティブのアクセシビ
            // リティ要素がコールバック経由でGUIホストを所有しないようにする。
            accessibility_->set_host_action_dispatcher(accessibility_dispatcher_);
        }
        if ((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0) {
            configure_sdl_ui_before_init();
            owns_sdl_ = SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
        }
        refresh_ui_font();
        set_cursor_realizer([this](const CursorKind cursor) {update_cursor(cursor);});
    }

    ~SdlGuiBackend() override {
        set_cursor_realizer({});
        if (frame_profile_.enabled) SDL_Log("%s", frame_profile_.describe().c_str());
        // 実際の画面を無効化する前に、遅延したネイティブの操作を止める。
        // プラットフォームの登録先には、通知処理への弱参照だけを渡す。
        accessibility_dispatch_lifetime_.reset();
        if (accessibility_dispatcher_) accessibility_dispatcher_->invalidate();
        if (accessibility_) accessibility_->set_host_action_dispatcher({});
        // ALL の既存の終了経路を維持する。単独版の警告は覆われた入れ子
        // のモーダル画面より長く存続する場合があるため、 その経路だけでス
        // タック全体を後入れ先出しに解き、無効化した所有者を正しく戻す。
        if (host_options_.all_mode) {
            while (!async_modal_sessions_.empty()) {
                auto found = async_modal_sessions_.begin();
                const auto session = std::move(found->second);
                async_modal_sessions_.erase(found);
                if (modal_state_.active(session.id))
                    (void)modal_state_.finish(session.id);
            }
        } else {
            // 通常は MainWindow が先にこれらを閉じる。ブラウザーのモー
            // ダル画面が開いたまま ホストが表示側を破棄する場合にも、終了済みの
            // バックエンドに所有者を無効のまま残さないよう、この代替処理を使う。
            while (!async_modal_sessions_.empty() || !modal_state_.sessions().empty()) {
                const auto& sessions = modal_state_.sessions();
                if (sessions.empty()) {
                    for (auto& [_, session] : async_modal_sessions_)
                        if (session.dialog) session.dialog->on_modal_session_ended = {};
                    async_modal_sessions_.clear();
                    break;
                }
                const auto top_id = sessions.back().id;
                const auto found = std::ranges::find_if(
                    async_modal_sessions_, [top_id](const auto& entry) {
                        return entry.second.id == top_id;
                    });
                if (found != async_modal_sessions_.end()) {
                    auto session = std::move(found->second);
                    async_modal_sessions_.erase(found);
                    if (session.dialog) session.dialog->on_modal_session_ended = {};
                    if (modal_state_.active(session.id))
                        (void)modal_state_.finish(session.id);
                } else {
                    // 追跡されていない同期の最上位画面を直接解けるのは、
                    // 終了処理のときだけ。 その後、下にある非同期の所有
                    // 者を正しい後入れ先出し順で復元できるようにする。
                    (void)modal_state_.finish(top_id);
                }
            }
        }
        // 作成後に変わっている可能性のある論理的な所有者ではなく、取得済みの
        // 実際の所有関係を使う。 終了中はアプリのコールバックを呼ばない。
        for (auto& [_, native] : windows_) native.pending_destroy=true;
        native_retirements_pending_=true;
        drain_native_retirements();
        for (auto& [_, cursor] : cursors_) if (cursor) SDL_DestroyCursor(cursor);
        for (auto& [_, cursor] : custom_cursors_) if (cursor) SDL_DestroyCursor(cursor);

        if (owns_sdl_) SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
    }

    Platform platform() const noexcept override { return native_platform(); }

    void set_application_lifecycle_handler(
        std::function<void(LifecycleEvent)> handler) {
        lifecycle_handler_ = std::move(handler);
    }

    void set_hosted_canvas_size(const Size size) override {
        if (!host_options_.all_mode || !std::isfinite(size.width) ||
            !std::isfinite(size.height) || size.width <= 0.0F || size.height <= 0.0F)
            return;
        host_options_.hosted_canvas_size = size;
        auto shared = editor_window_host_options();
        shared.hosted_canvas_size = size;
        set_editor_window_host_options(shared);
    }

    [[nodiscard]] WindowAttributeReport window_attribute_report(
        const std::shared_ptr<Window>& window) const override {
        const auto found=windows_.find(window.get());
        return found==windows_.end() || found->second.pending_destroy
            ? WindowAttributeReport{} : found->second.attributes->report();
    }

    [[nodiscard]] std::shared_ptr<Window> dialog_parent_model() const {
        return window_stack_.dialog_owner(modal_state_,focus_state_.focused());
    }

    [[nodiscard]] std::shared_ptr<Window> window_owner(const WidgetPtr& widget) const override {
        return window_stack_.owner_of(widget);
    }

    [[nodiscard]] SDL_Window* dialog_parent_window() const {
        const auto owner=dialog_parent_model();
        const auto found=windows_.find(owner.get());
        return found!=windows_.end() ? found->second.window : nullptr;
    }

    void show(const std::shared_ptr<Window>& window) override {
        UiTextMeasureScope text_metrics{[this](std::string_view text, const FontSpec& font) {
            return fonts_->measure_text(text, font, 0);
        }, [this](const FontSpec& font) { return fonts_->ink_center_offset(font); }};
        if (!window || window_close_.disposing(window) || retiring(window)) return;
        notify_window_shown(window);
        Native& native = ensure(window);
        synchronize_input_mask(native);
        const auto plan=window_close_.show(window_lifecycle_context(),window_stack_,modal_state_,window,
            {native.id,native.embedded ? native.hosted_bounds : window->bounds(),native.embedded});
        if(!plan) return;
        native.updates->request();
        if (plan->native_visible) {
            SDL_ShowWindow(native.window);
            if (plan->native_activate) SDL_RaiseWindow(native.window);
        }
        if (single_surface_root_) render_single_surface();
        else if (native.embedded) {
            if (!host_iteration_) render_hosted();
        }
        else render_surface_frame(native.renderer, SurfacePresent::Standalone);
    }

    DialogResult show_modal(const std::shared_ptr<Dialog>& dialog,
                            const std::shared_ptr<Window>& owner) override {
        if (!dialog || window_close_.disposing(dialog) || retiring(dialog)) return DialogResult::Cancel;
        const auto modal_owner = owner ? owner : dialog->owner.lock();
        ModalDialogOperation operation;
        operation.present=[this,dialog] { show(dialog); };
        operation.dismiss=[this,dialog] { if(windows_.contains(dialog.get())) close(dialog); };
        DialogResult result{};
        try {
            result = run_modal_dialog_operation(modal_operation_context(modal_owner), dialog,
                                                modal_owner, std::move(operation));
        } catch (...) {
            const auto error = std::current_exception();
            if (!host_options_.all_mode) {
                try { drain_one_pending_async_modal_end(); } catch (...) {}
            }
            std::rethrow_exception(error);
        }
        if (!host_options_.all_mode) drain_one_pending_async_modal_end();
        return result;
    }

    void synchronize_input_mask(Native& native) {
        if(!native.model || !native.window) return;
        const auto mask=native.model->input_mask;
        if(native.embedded) {
            if(native.input_mask_synchronized && native.input_mask==mask) return;
            // 埋め込みプレビューは所有者の描画面を共有し、 WindowStackStateMachine
            // の論理的な当たり判定でも同じ元マスクを使う。
            native.input_mask=mask;
            native.input_mask_synchronized=true;
            return;
        }

        if(!mask && !native.input_mask) {
            native.input_mask_synchronized=true;
            return;
        }
        int pixel_width{},pixel_height{};
        if(!SDL_GetWindowSizeInPixels(native.window,&pixel_width,&pixel_height) ||
           pixel_width<=0 || pixel_height<=0) {
            editor_action_log(std::string("Editor overlay input shape size query failed: ")+SDL_GetError());
            return;
        }
        if(native.input_mask_synchronized && native.input_mask==mask &&
           native.input_mask_pixel_width==pixel_width &&
           native.input_mask_pixel_height==pixel_height) return;

        bool applied=true;
        // SDLのウィンドウ形状は描画画素単位。 現在のDPIで、埋め込みの
        // 当たり判定と同じ中心に最も近い画素を選ぶ規則で再サンプリングする。
        if(mask) {
            const auto pixels=resample_window_input_mask(*mask,
                static_cast<std::uint32_t>(pixel_width),static_cast<std::uint32_t>(pixel_height));
            auto shape=pixels ? create_window_input_mask_surface(*pixels) : ImageSurface{};
            if(!shape) applied=false;
            else applied=SDL_SetWindowShape(native.window,shape.get());
        } else if(native.input_mask) applied=SDL_SetWindowShape(native.window,nullptr);
        if(!applied) {
            editor_action_log(std::string("Editor overlay input shape update failed: ")+SDL_GetError());
            return;
        }
        native.input_mask=mask;
        native.input_mask_pixel_width=pixel_width;
        native.input_mask_pixel_height=pixel_height;
        native.input_mask_synchronized=true;
    }

    bool begin_modal(const std::shared_ptr<Dialog>& dialog,
                     const std::shared_ptr<Window>& owner) override {
        if (!dialog || window_close_.disposing(dialog) || retiring(dialog) ||
            async_modal_sessions_.contains(dialog.get()))
            return false;
        const auto modal_owner = owner ? owner : dialog->owner.lock();
        ModalDialogOperation operation;
        operation.present = [this, dialog] { show(dialog); };
        operation.dismiss = [this, dialog] {
            if (windows_.contains(dialog.get())) close(dialog);
        };
        // モーダル状態が所有者を無効化したりUIを表示したりする
        // 前に、確保が必要な全オブジェクトと非同期セッションのマ
        // ップ項目を準備する。 表示中に同期的に閉じた場合は、こ
        // の仮項目に印を付け、セッションIDの反映後に確定する。
        auto stored_dismiss = operation.dismiss;
        auto rollback_dismiss = operation.dismiss;
        auto [placeholder, inserted] = async_modal_sessions_.try_emplace(
            dialog.get(), AsyncModalSession{0, dialog, modal_owner, true, false,
                dialog->defer_modal_end_until_top, false, false, std::move(stored_dismiss)});
        (void)placeholder;
        if (!inserted) return false;
        try {
            const auto id = begin_modal_dialog_operation(
                modal_operation_context(modal_owner), dialog, modal_owner,
                std::move(operation));
            auto found = async_modal_sessions_.find(dialog.get());
            if (!id) {
                if (!dialog->defer_modal_end_until_top) {
                    const auto failed = async_modal_sessions_.find(dialog.get());
                    if (failed != async_modal_sessions_.end())
                        async_modal_sessions_.erase(failed);
                    return false; // 他のダイアログは従来の動作を維持する。
                }
                // 補助処理は、再入による最上位以外の終了試行の後で失敗
                // を返す場合がある。 例外時の後処理が成功したとは仮定
                // せず、モーダルスタックからそのセッションを回収する。
                const bool pending = recover_failed_async_modal_begin(dialog);
                if (dialog->on_close_requested) {
                    // この方式を選んだ警告は、ネイティブのセッションが
                    // 保留中か、表示失敗ですでに無効になっている。 同
                    // じ破棄済みのダイアログをモードレスで表示し直さ
                    // ない。 呼び出し元の終了継続を予約し、残るセッ
                    // ションの完了を待つか、なければ直ちに実行する。
                    const auto close_requested = dialog->on_close_requested;
                    close_requested();
                    return true;
                }
                return pending;
            }
            if (found == async_modal_sessions_.end()) {
                // 仮項目は開始前に確保し、end_modal は beginning
                // の項目を削除しない。 予期しない再入経路で消
                // えた場合の保護処理も、メモリ確保なしで行う。
                ModalDialogOperation rollback;
                rollback.dismiss = std::move(rollback_dismiss);
                (void)finish_modal_dialog_operation(
                    modal_operation_context(modal_owner), *id, std::move(rollback));
                return false;
            }
            found->second.id = *id;
            found->second.beginning = false;
            const bool end_requested = found->second.end_requested;
            if (end_requested) end_modal(dialog);
            return true;
        } catch (...) {
            // 補助処理の例外経路の後処理は、入れ子のモーダルの下にある
            // セッションを終了できない場合がある。 元の例外を保ちなが
            // ら、まだ有効なセッションは後で終了できるよう保持する。
            const auto error = std::current_exception();
            if (dialog->defer_modal_end_until_top) {
                (void)recover_failed_async_modal_begin(dialog);
            } else {
                const auto failed = async_modal_sessions_.find(dialog.get());
                if (failed != async_modal_sessions_.end())
                    async_modal_sessions_.erase(failed);
            }
            std::rethrow_exception(error);
        }
    }

    void end_modal(const std::shared_ptr<Dialog>& dialog) override {
        end_async_modal(dialog, dialog && dialog->defer_modal_end_until_top);
    }

    void end_modal(const std::shared_ptr<Dialog>& dialog, Action ended) override {
        if (!dialog) {
            if (ended) ended();
            return;
        }
        if (!dialog->defer_modal_end_until_top) {
            // この方式を選んでいないダイアログは、GuiBackend
            // の互換オーバーロードの同期完了の規則を維持する。
            end_modal(dialog);
            if (ended) ended();
            return;
        }
        // 一つの Dialog のセッションに、ホストが所有する終了後の処理は最大一つ。
        // finish() がモーダルの終了を確認するまで、セッションとともに保持する。
        if (ended) dialog->on_modal_session_ended = std::move(ended);
        end_async_modal(dialog, dialog->defer_modal_end_until_top);
    }

    void end_async_modal(const std::shared_ptr<Dialog>& dialog, const bool drain_after) {
        if (!dialog) return;
        auto found = async_modal_sessions_.find(dialog.get());
        if (found == async_modal_sessions_.end()) {
            // 完了フックは begin_modal が true を返した後で
            // だけ登録する。 その記録がすでに無効なら、セッションは終了済み。
            if (std::ranges::any_of(modal_state_.sessions(), [&dialog](const auto& session) {
                    return session.dialog == dialog;
                })) return; // セッションが有効な間は、完了を通知しない。
            auto completed = std::exchange(dialog->on_modal_session_ended, {});
            if (completed) completed();
            return;
        }
        if (found->second.beginning) {
            // 開始の補助処理は、表示から同期的に再入する場合があ
            // る。 セッションIDが返るまで、終了を遅延する。
            found->second.end_requested = true;
            return;
        }
        auto& session = found->second;
        if (session.finishing) {
            session.end_requested = true;
            return;
        }
        if (!session.id) return;
        if (session.defer_end_until_top && modal_state_.active(session.id) &&
            !is_top_modal_session(session.id)) {
            // 終了要求は受け付け済みだが、子の下での終了はスタックの
            // 規則で禁止される。 警告を隠して記録を残し、最上位セ
            // ッションの終了処理で、最上位になった時点で処理する。
            session.end_requested = true;
            try { hide(dialog); } catch (...) {}
            return;
        }
        if (!session.defer_end_until_top && modal_state_.active(session.id) &&
            covered_by_deferred_warning(session.id)) {
            // 上の警告が閉じるまで子の非同期ダイアログのネイティブセ
            // ッションを保持しつつ、 発行済みの完了通知は維持する。
            session.end_requested = true;
            session.deferred_by_warning = true;
            try { hide(dialog); } catch (...) {}
            return;
        }
        if (!session.defer_end_until_top) {
            auto legacy_session = std::move(session);
            async_modal_sessions_.erase(found);
            ModalDialogOperation operation;
            operation.dismiss = std::move(legacy_session.dismiss);
            (void)finish_modal_dialog_operation(
                modal_operation_context(legacy_session.owner), legacy_session.id,
                std::move(operation));
            if (drain_after && !host_options_.all_mode)
                drain_one_pending_async_modal_end();
            return; // 既存の非同期呼び出し元は、従来の終了動作を維持する。
        }

        // 再入によるスタック変更で finish() が拒否す
        // る場合があるため、標準の終了コールバックは再利用可能
        // に保つ。 後の再試行でも使えるよう、セッション変更
        // 前に複製し、確保失敗時は既存の状態をそのまま残す。
        auto dismiss = session.dismiss;
        session.finishing = true;
        const auto session_id = session.id;
        const auto session_owner = session.owner;
        ModalDialogOperation operation;
        operation.dismiss = std::move(dismiss);
        std::exception_ptr error;
        try {
            (void)finish_modal_dialog_operation(
                modal_operation_context(session_owner), session_id,
                std::move(operation));
        } catch (...) {
            error = std::current_exception();
        }

        const auto still_active = modal_state_.active(session_id);
        found = async_modal_sessions_.find(dialog.get());
        if (found == async_modal_sessions_.end()) {
            // 再入した終了を重複実行しないよう、finish 中もこの項目を
            // 保持する。 消えている場合は、保護的に扱う終了処理の例外状況。
            if (error) std::rethrow_exception(error);
            return;
        }
        if (still_active) {
            // finish() は、再入で別画面に覆われたセッションを拒否する
            // 場合がある。 今回の試行には標準のコールバックを複製してあるため
            // 、 上のモーダルが終わった後も、同じコールバックで再試行できる。
            found->second.finishing = false;
            found->second.end_requested = true;
            try { hide(dialog); } catch (...) { if (!error) error = std::current_exception(); }
            if (error) std::rethrow_exception(error);
            return;
        }

        auto completed = std::exchange(dialog->on_modal_session_ended, {});
        async_modal_sessions_.erase(found);
        if (drain_after && !host_options_.all_mode) {
            try { drain_one_pending_async_modal_end(); }
            catch (...) { if (!error) error = std::current_exception(); }
        }
        // 外部の継続処理は最後に呼ぶ。MainWindow や表示側を無効化
        // する場合があるため、 その後はこのメソッドからメンバーに触れない。
        try { if (completed) completed(); }
        catch (...) { if (!error) error = std::current_exception(); }
        if (error) std::rethrow_exception(error);
    }

    bool run_external_dialog(const std::shared_ptr<Window>& owner,
                             ExternalDialogOperation operation) override {
        bool result{};
        try {
            result = run_external_dialog_operation(
                modal_operation_context(owner), owner, std::move(operation));
        } catch (...) {
            const auto error = std::current_exception();
            if (!host_options_.all_mode) {
                try { drain_one_pending_async_modal_end(); } catch (...) {}
            }
            std::rethrow_exception(error);
        }
        if (!host_options_.all_mode) drain_one_pending_async_modal_end();
        return result;
    }

    void run_background_io(const std::shared_ptr<Window>& owner, Action work) override {
        run_background_io_operation([this, owner](ExternalDialogOperation operation) {
            return run_external_dialog(owner, std::move(operation));
        }, std::move(work));
    }

    void close(const std::shared_ptr<Window>& requested) override {
        window_close_.dispose(window_lifecycle_context(),window_stack_,requested,[this](const auto& window) {
        if (minimized_groups_.erase(window.get()) != 0U || window->minimized) {
            window->minimized = false;
            notify_minimized_changed();
        }
        const auto found = windows_.find(window.get());
        if (found != windows_.end()) {
            found->second.updates->retire();
            found->second.pending_destroy=true;
            native_retirements_pending_=true;
            drain_native_retirements();
        }
        if (window.get() == single_surface_root_) {
            single_surface_root_ = nullptr;
        } else if (single_surface_root_) {
            const auto root = windows_.find(single_surface_root_);
            if (root != windows_.end()) root->second.updates->request();
        }
        },[this] {synchronize_native_focus();});
    }

    void hide(const std::shared_ptr<Window>& window) override {
        window_close_.hide(window_lifecycle_context(),window_stack_,window,[this](const auto& hidden) {
            const auto found=windows_.find(hidden.get());
            if (found==windows_.end()) return;
            found->second.updates->request();
            if (!found->second.embedded && found->second.window) SDL_HideWindow(found->second.window);
        },[this] {synchronize_native_focus();});
        if (single_surface_root_) render_single_surface();
    }

    void minimize_window(const std::shared_ptr<Window>& window) override {
        if (!window || !windows_.contains(window.get()) ||
            window_close_.disposing(window) || !hosted_minimizable(*window)) return;
        // 子ダイアログなどの所有ウィンドウは、所有者とともに消える
        // 。 表示中のものを記憶し、所有者の復元時に一緒に戻す。
        std::vector<std::weak_ptr<Window>> group;
        for (const auto& candidate : window_stack_.windows()) {
            if (!candidate || candidate == window || !candidate->visible()) continue;
            std::unordered_set<const Window*> visited;
            for (auto owner = candidate->owner.lock(); owner && visited.insert(owner.get()).second;
                 owner = owner->owner.lock()) {
                if (owner == window) { group.push_back(candidate); break; }
            }
        }
        minimized_groups_[window.get()] = std::move(group);
        window->minimized = true;
        hide(window);
        notify_minimized_changed();
    }

    void restore_window(const std::shared_ptr<Window>& window) override {
        if (!window || !windows_.contains(window.get()) || window_close_.disposing(window)) return;
        window->minimized = false;
        show(window);
        notify_minimized_changed();
        const auto found = minimized_groups_.find(window.get());
        if (found == minimized_groups_.end()) return;
        auto group = std::move(found->second);
        minimized_groups_.erase(found);
        for (const auto& weak : group)
            if (const auto child = weak.lock();
                child && windows_.contains(child.get()) && !window_close_.disposing(child))
                show(child);
    }

    [[nodiscard]] std::vector<std::shared_ptr<Window>> hosted_dialogs() const override {
        std::vector<std::shared_ptr<Window>> result;
        for (const auto& window : window_stack_.windows()) {
            if (!window || window->overlay_surface ||
                window->host_mode != WindowHostMode::EmbeddedDialog) continue;
            const auto found = windows_.find(window.get());
            if (found == windows_.end() || found->second.pending_destroy) continue;
            if (!window->visible() && !window->minimized) continue;
            result.push_back(window);
        }
        return result;
    }

    void set_window_bounds(const std::shared_ptr<Window>& window, Rect bounds) override {
        if (!window) return;
        hosted_caption_.cancel_for(window);
        const auto found = windows_.find(window.get());
        if(found==windows_.end()) {window->set_bounds(bounds);return;}
        const auto token=token_for(found->second);
        const auto available=found->second.embedded ? surface_available_bounds(found->second)
            : native_client_bounds(found->second.window,Point{bounds.x,bounds.y}).value_or(Rect{});
        const auto plan=plan_window_geometry(*window,{bounds,
            available,{},found->second.embedded,WindowGeometryIntent::Explicit});
        window->set_bounds(plan.bounds);
        auto* current=native_for(token); // set_bounds が、アプリの配置コールバックを呼ぶ場合がある。
        if(!current) return;
        current->authored_bounds=authored_window_bounds(*window);
        current->attributes->invalidate_constraints();
        if(current->embedded) current->hosted_bounds=plan.bounds;
        else if(current->window) {
            apply_native_window_limits(current->window,plan.minimum,plan.maximum);
            SDL_SetWindowPosition(current->window,static_cast<int>(plan.bounds.x),static_cast<int>(plan.bounds.y));
            SDL_SetWindowSize(current->window,static_cast<int>(plan.bounds.width),static_cast<int>(plan.bounds.height));
        }
        current->updates->request();
        window_stack_.set_bounds(window,current->embedded ? plan.bounds
            : Rect{0,0,plan.bounds.width,plan.bounds.height});
        wake_event_loop();
    }

    [[nodiscard]] Rect window_bounds(const std::shared_ptr<Window>& window) const override {
        if (!window) return {};
        const auto found = windows_.find(window.get());
        if (found != windows_.end() && !found->second.embedded &&
            !found->second.pending_destroy && found->second.window) {
            int x{}, y{}, width{}, height{};
            if (SDL_GetWindowPosition(found->second.window, &x, &y) &&
                SDL_GetWindowSize(found->second.window, &width, &height))
                return {static_cast<float>(x), static_cast<float>(y),
                        static_cast<float>(width), static_cast<float>(height)};
        }
        return window->bounds();
    }

    void set_redraw(const WidgetPtr& widget, bool enabled) override {
        if (!widget) return;
        if (enabled) {
            // 処理区間の中でウィジェットが切り離されたり親を変えられたりする場
            // 合がある。 元の保持側を解放してから、現在の所有者を取得する。
            for (auto& [_,native] : windows_) native.updates->set_redraw(widget,true);
            request_redraw(widget,std::nullopt);
            wake_event_loop();
        } else {
            for (const auto& owner : redraw_windows(window_stack_,widget))
                if (auto* native=native_for(owner)) native->updates->set_redraw(widget,false);
        }
    }

    void request_redraw(const WidgetPtr& widget, std::optional<Rect>) override {
        const auto targets = redraw_popup_windows(window_stack_, context_popup_, widget);
        for (const auto& target : targets)
            if (auto* native=native_for(target)) native->updates->request();
        if (!targets.empty()) wake_event_loop();
    }

    void set_ui_text_scale(const float value) override {
        UiTextMeasureScope text_metrics{[this](std::string_view text, const FontSpec& font) {
            return fonts_->measure_text(text, font, 0);
        }, [this](const FontSpec& font) { return fonts_->ink_center_offset(font); }};
        std::unordered_map<const Window*,NativeToken> tokens;
        (void)text_scale_changes_.apply(value,{window_lifecycle_context(),window_stack_,
            [this] {refresh_ui_font();},
            [this,&tokens] {
        std::vector<WindowTextScaleSurface> surfaces;
        surfaces.reserve(windows_.size());
        tokens.reserve(windows_.size());
        for (auto& [_, native] : windows_) {
            if (!native.model || native.pending_destroy) continue;
            Rect current_bounds = native.embedded ? native.hosted_bounds : native.model->bounds();
            if (!native.embedded && native.window) {
                int x{}, y{}, width{}, height{};
                SDL_GetWindowPosition(native.window, &x, &y);
                SDL_GetWindowSize(native.window, &width, &height);
                current_bounds = {static_cast<float>(x), static_cast<float>(y),
                                  static_cast<float>(width), static_cast<float>(height)};
            }
            const auto available_bounds=surface_available_bounds(native);
            std::optional<Rect> owner_bounds;
            if (const auto owner = native.model->owner.lock()) {
                const auto found = windows_.find(owner.get());
                if (found != windows_.end()) {
                    const auto& owner_native = found->second;
                    if (owner_native.embedded) owner_bounds = owner_native.hosted_bounds;
                    else if (owner_native.window) {
                        int x{}, y{}, width{}, height{};
                        SDL_GetWindowPosition(owner_native.window, &x, &y);
                        SDL_GetWindowSize(owner_native.window, &width, &height);
                        if(native.embedded && owner_native.window==native.window) {
                            // モバイルのルートはネイティブだが、埋め込みの子は
                            // デスクトップ位置ではなくクライアント座標を使う。
                            SDL_Rect client{0,0,width,height};
                            if(!SDL_GetWindowSafeArea(native.window,&client))
                                client={0,0,width,height};
                            x=client.x; y=client.y; width=client.w; height=client.h;
                        }
                        owner_bounds = Rect{static_cast<float>(x), static_cast<float>(y),
                                            static_cast<float>(width), static_cast<float>(height)};
                    }
                }
            }
            surfaces.push_back({native.model,{
                {native.authored_bounds.width, native.authored_bounds.height}, current_bounds,
                available_bounds, owner_bounds, native.embedded},
                native.embedded ? static_cast<std::uintptr_t>(native.id) : 0});
            tokens.emplace(native.model.get(),token_for(native));
        }
        return surfaces;
            },
            [this,&tokens](const std::shared_ptr<Window>& window,
                           const std::optional<WindowTextScalePlan>& plan) {
            const auto token=tokens.find(window.get());
            if(token==tokens.end()) return false;
            auto* current=native_for(token->second);
            if(!current) return false;
            auto& native=*current;
            if (plan) {
                if (native.embedded) native.hosted_bounds = plan->bounds;
                else if (native.window) {
                    // SDLは古い最大値を超える新しい最小値、その逆も拒否す
                    // るため、 新しい配置を設定する前に古い制限を解除する。
                    apply_native_window_limits(native.window, plan->minimum, plan->maximum);
                    if(plan->apply_bounds) {
                        SDL_SetWindowSize(native.window, static_cast<int>(plan->bounds.width),
                                          static_cast<int>(plan->bounds.height));
                        SDL_SetWindowPosition(native.window, static_cast<int>(plan->bounds.x),
                                              static_cast<int>(plan->bounds.y));
                    }
                }
            }
            return true;
            },
            [this](const auto& window) {if(auto* native=native_for(window)) native->updates->request();},
            [this] {synchronize_native_focus();},
            [this,&tokens](const auto& window) {
                const auto found=tokens.find(window.get());
                return found!=tokens.end() && native_for(found->second)!=nullptr;
            }});
        wake_event_loop();
    }

    void focus(const WidgetPtr& widget) override { set_focus(widget); }
    [[nodiscard]] bool request_programmatic_inline_edit(
        const ProgrammaticInlineEditRequest& request) override {
        return begin_presented_programmatic_inline_edit(
            inline_edit_, request, [this](const WidgetPtr& widget) {
                return publish_and_require_edit_focus(
                    input_publication_context(), widget);
            });
    }
    [[nodiscard]] bool programmatic_inline_edit_active(
        const WidgetPtr& owner) const override {
        return owner && inline_edit_.active() && inline_edit_.owner() == owner;
    }
    [[nodiscard]] bool finish_programmatic_inline_edit(
        const WidgetPtr& owner, const bool commit) override {
        if (!owner || !inline_edit_.active() || inline_edit_.owner() != owner)
            return false;
        if (!commit) {
            if (combo_popup_.is_open_for(inline_edit_.choice())) combo_popup_.close();
            inline_edit_.cancel();
            return true;
        }
        if (combo_popup_.is_open_for(inline_edit_.choice())) combo_popup_.close();
        return inline_edit_.commit();
    }
    [[nodiscard]] bool request_accessibility_action(
        const AccessibilityActionRequest& request) override {
        const auto scope = window_stack_.owner_of(request.target);
        if (!scope || !dispatch_accessibility_action(scope, request)) return false;
        // 共通の通知処理でモデルの変更は完了済み。 アダプターは結
        // 果のスナップショットを再反映して実際の描画を予約するだけ
        // とし、 アクセシビリティの選択・操作を別に導出しない。
        publish_accessibility(request.target);
        request_redraw(scope, std::nullopt);
        return true;
    }
    WidgetPtr focused_widget() const override { return focus_state_.focused(); }
    [[nodiscard]] std::shared_ptr<Window> active_input_window() const override {
        return window_stack_.dialog_owner(modal_state_, focus_state_.focused());
    }
    [[nodiscard]] std::uint64_t focus_revision() const noexcept override {
        return focus_state_.revision();
    }
    void suspend_input(const std::shared_ptr<Window>& window) override {
        if (!window) return;
        const auto* native = native_for(window);
        const auto token = native ? std::optional{token_for(*native)} : std::nullopt;
        const auto live = [this, token] {
            return token && native_for(*token) != nullptr;
        };
        // ネイティブのフォーカス喪失・モーダル開始と同じ共通の寿命処理を使う。
        // 解放イベントを生成せず、マウス・タッチの捕捉、ドラッグ、ポップア
        // ップ、 ホバー、IME・編集フォーカス、押下プレビューを終了する。
        ::cwxeditor::lib::suspend_window_input(
            window_lifecycle_context(), window, {}, live);
        synchronize_native_focus();
        request_redraw(window, std::nullopt);
    }
    [[nodiscard]] Modifiers current_modifiers() const noexcept override {
        return sdl_modifiers(SDL_GetModState());
    }
    void capture_pointer(const WidgetPtr& widget, bool capture) override {
        mouse_input_.capture(window_stack_.owner_of(widget), widget, capture);
    }
    [[nodiscard]] bool dispatch_pointer_event(
        const std::shared_ptr<Window>& scope, PointerEvent event,
        const bool primary_down, const unsigned clicks) override {
        auto* native = native_for(scope);
        if (!native || native->pending_destroy) return false;
        if ((host_options_.all_mode || !event.screen_position_available) &&
            event.screen_position.x == 0.0F && event.screen_position.y == 0.0F) {
            if (native->embedded) {
                event.screen_position = event.position;
                event.screen_position_available = true;
            } else if (native->window) {
                int x{}, y{};
                SDL_GetWindowPosition(native->window, &x, &y);
                event.screen_position = {
                    event.position.x + static_cast<float>(x),
                    event.position.y + static_cast<float>(y)};
                event.screen_position_available = true;
            }
        }
        return handle_pointer_input(*native, std::move(event), primary_down,
                                    clicks);
    }

    [[nodiscard]] bool dispatch_key_event(
        const std::shared_ptr<Window>& scope, KeyEvent event,
        const bool down) override {
        auto* native = native_for(scope);
        if (!native || native->pending_destroy) return false;
        const auto result = dispatch_and_publish_edit_key(
            input_publication_context(token_for(*native)), scope,
            std::move(event), down);
        return result.handled;
    }

    void show_context_menu(const std::shared_ptr<Menu>& menu, const WidgetPtr& owner, Point position) override {
        context_popup_.open(menu, owner ? owner : focus_state_.focused(), position);
        close_combo_popup();
        request_redraw(menu, std::nullopt);
    }

    bool pump_events(bool wait) override {
        // SDLのOSイベント取得と通知キューの処理も計測に含める。
        // ハンドラーだけを計ると、エディタへ届く前の停止を見逃す。
        const EditorPerformanceScope pump_performance("input.event-pump", 100000);
        drain_native_retirements();
        UiTextMeasureScope text_metrics{[this](std::string_view text, const FontSpec& font) {
            return fonts_->measure_text(text, font, 0);
        }, [this](const FontSpec& font) { return fonts_->ink_center_offset(font); }};
        if (dispatcher_) dispatcher_->drain(256U);
        if (host_options_.all_mode && host_options_.native_renderer != 0) {
            // 通常のホストの更新周期では、待機中の処理を同期するだけにする
            // 。 ここで描くと、ホストがエディタ用テクスチャーを選ぶ前に、
            // その時点の別のバックバッファを描画先にしてしまう。 モーダル
            // 待ちでも、同じホストに入力の振り分けとフレーム完了を任せる。
            if (wait) {
                const auto alive=iterate_host();
                if (alive && !*alive) return false;
                if (!alive) SDL_Delay(16);
            }
            if (dispatcher_) dispatcher_->drain(256U);
            advance_pointer_timers();
            return !windows_.empty() || modal_state_.has_external_session();
        }
        SDL_Event event{};
#if defined(__EMSCRIPTEN__) && \
    (!defined(CARDWIRTH_WEB_IO_SERVER) || !CARDWIRTH_WEB_IO_SERVER)
        // 単独のWebエディタは通常の共通イベントループを持つ。 ブラウザーの入
        // 力・描画が動けるよう Asyncify で制御を譲り、 SDL_WaitEventTimeout
        // でブラウザーのイベントスレッドを止めない。
        if (wait) emscripten_sleep(16);
        bool received = SDL_PollEvent(&event);
#else
        // ネイティブ構成とWebの入出力サーバー構成では、このループはブラウザー
        // の主スレッド外で動くため、 通常のブロッキングするSDL待機を使える。
        bool received = wait ? SDL_WaitEventTimeout(&event, 16) : SDL_PollEvent(&event);
#endif
        if (received) {
            if (!dispatchNativeEventOnce(event)) return false;
            while (SDL_PollEvent(&event))
                if (!dispatchNativeEventOnce(event)) return false;
        }
        if (dispatcher_) dispatcher_->drain(256U);
        bool redraw_single_surface = false;
        advance_pointer_timers();
        std::vector<NativeToken> targets;
        for (const auto& [_, native] : windows_) targets.push_back(token_for(native));
        for (const auto& token : targets) {
            auto* current=native_for(token);
            if (!current || !current->model->visible()) continue;
            auto& native=*current;
            if (native.animation.due(SDL_GetTicks())) native.updates->request();
            if (single_surface_root_) redraw_single_surface = redraw_single_surface || native.updates->pending();
            else if (native.updates->pending())
                render_surface_frame(native.renderer, SurfacePresent::Standalone);
        }
        if (redraw_single_surface) render_single_surface();
        const bool alive = !windows_.empty() || modal_state_.has_external_session();
        if (!host_options_.all_mode) drain_one_pending_async_modal_end();
        return alive;
    }

    bool dispatch_native_event(const void* native_event) override {
        if (!native_event) return false;
        SDL_Event event = *static_cast<const SDL_Event*>(native_event);
        return dispatchNativeEventOnce(event);
    }

    void invalidate_renderer_resources() {
        clear_text_texture_cache();
        clear_image_texture_cache();
        for (auto& [model, native] : windows_) {
            SDL_DestroyTexture(native.current_frame);
            SDL_DestroyTexture(native.pending_frame);
            SDL_DestroyTexture(native.opacity_layer);
            native.current_frame = native.pending_frame = native.opacity_layer = nullptr;
            native.current_width = native.current_height = 0;
            native.pending_width = native.pending_height = 0;
            native.opacity_width = native.opacity_height = 0;
            native.updates->request();
        }
    }

    std::optional<ImageFrame> completed_frame(
        const std::shared_ptr<Window>& window) override {
        if (!window) return std::nullopt;
        const auto found = windows_.find(window.get());
        if (found == windows_.end()) return std::nullopt;
        auto& native = found->second;
        if (native.pending_destroy || native.embedded || !native.renderer || !native.current_frame)
            return std::nullopt;
        // OSが合成した画面や Present 後の未定義のバックバッファ
        // ではなく、完成した自身のテクスチャーを読む。 読み戻し・変換が
        // 失敗しても描画先の状態を復元し、未完成のフレームを公開しない。
        struct TargetGuard {
            SDL_Renderer* renderer;
            SDL_Texture* previous;
            ~TargetGuard() { SDL_SetRenderTarget(renderer, previous); }
        } guard{native.renderer, SDL_GetRenderTarget(native.renderer)};
        if (!SDL_SetRenderTarget(native.renderer, native.current_frame))
            return std::nullopt;
        ImageSurface surface(SDL_RenderReadPixels(native.renderer, nullptr));
        if (!surface) return std::nullopt;
        std::string error;
        auto frame = image_frame_from_surface(surface.get(), std::nullopt, {}, error);
        if (frame.pixels.empty()) return std::nullopt;
        return frame;
    }

    void render_hosted() override {
        if (!host_options_.all_mode || host_options_.native_renderer == 0) return;
        render_surface_frame(reinterpret_cast<SDL_Renderer*>(host_options_.native_renderer),
                             SurfacePresent::HostedNoPresent);
    }

    void wake_event_loop() override {
        // 多数のモデル変更にも起床通知は一度でよく、ウィジェットごとにSDLイベン
        // トを出さない。 UIスレッドがイベントを消費するときにフラグを解除する。
        if (wake_posted_.exchange(true, std::memory_order_acq_rel)) return;
        SDL_Event event{};
        event.type = SDL_EVENT_USER;
        if (!SDL_PushEvent(&event))
            wake_posted_.store(false, std::memory_order_release);
    }

    std::vector<ScreenInfo> screens() const override {
        std::vector<ScreenInfo> result;
        int count = 0;
        SDL_DisplayID* displays = SDL_GetDisplays(&count);
        for (int i = 0; displays && i < count; ++i) {
            SDL_Rect bounds{}, work{};
            SDL_GetDisplayBounds(displays[i], &bounds);
            if (!SDL_GetDisplayUsableBounds(displays[i], &work)) work = bounds;
            const char* name = SDL_GetDisplayName(displays[i]);
            result.push_back({name ? name : "Display",
                              {static_cast<float>(bounds.x), static_cast<float>(bounds.y), static_cast<float>(bounds.w), static_cast<float>(bounds.h)},
                              {static_cast<float>(work.x), static_cast<float>(work.y), static_cast<float>(work.w), static_cast<float>(work.h)},
                              SDL_GetDisplayContentScale(displays[i]), i == 0});
        }
        SDL_free(displays);
        return result;
    }

    void set_color_picker_async(AsyncColorPicker picker) {
        color_picker_async_ = std::move(picker);
    }
    void set_color_picker(std::function<std::optional<Color>(Color)> picker) {
        color_picker_ = std::move(picker);
    }

private:
    [[nodiscard]] bool is_top_modal_session(const ModalStateMachine::SessionId id) const {
        const auto& sessions = modal_state_.sessions();
        return !sessions.empty() && sessions.back().id == id;
    }

    [[nodiscard]] bool covered_by_deferred_warning(
        const ModalStateMachine::SessionId id) const {
        const auto& sessions = modal_state_.sessions();
        const auto current = std::ranges::find(sessions, id, &ModalStateMachine::Session::id);
        if (current == sessions.end()) return false;
        auto above = current;
        ++above;
        for (; above != sessions.end(); ++above) {
            if (!above->dialog) continue;
            const auto record = async_modal_sessions_.find(above->dialog.get());
            if (record != async_modal_sessions_.end() &&
                record->second.defer_end_until_top) return true;
        }
        return false;
    }

    // 最上位のモーダルが実際に終了した後でだけ呼ぶ。 対象
    // 方式の保留警告は確保済みの非同期セッションのマップ項
    // 目で表すため、 この繰り上げ処理自体では確保しない。
    void drain_one_pending_async_modal_end() {
        if (host_options_.all_mode) return;
        const auto& sessions = modal_state_.sessions();
        if (sessions.empty()) return;
        const auto top_id = sessions.back().id;
        for (const auto& [_, session] : async_modal_sessions_) {
            if (session.id == top_id &&
                (session.defer_end_until_top || session.deferred_by_warning) &&
                session.end_requested && !session.beginning && !session.finishing) {
                const auto dialog = session.dialog;
                end_async_modal(dialog, true);
                return; // このバックエンドを無効化する継続処理を呼ぶ場合がある。
            }
        }
    }

    // 共通の開始処理は可能な範囲で元へ戻すが、最上位以外
    // のセッションが消えたとは保証できない。 ダイアログ
    // の識別情報で探し、正当に終了できるまで待機させる。
    [[nodiscard]] bool recover_failed_async_modal_begin(
        const std::shared_ptr<Dialog>& dialog) {
        if (!dialog) return false;
        auto record = async_modal_sessions_.find(dialog.get());
        if (record == async_modal_sessions_.end()) return false;
        const auto& sessions = modal_state_.sessions();
        const auto active = std::ranges::find_if(sessions, [&dialog](const auto& session) {
            return session.dialog == dialog;
        });
        if (active == sessions.end()) {
            async_modal_sessions_.erase(record);
            return false;
        }
        const auto id = active->id;
        record->second.id = id;
        record->second.beginning = false;
        record->second.end_requested = true;
        dialog->on_modal_session_ended = {};
        // この復旧処理から別の警告の継続を実行しない。
        // 元の例外を伝える前に表示側が無効化される場合が
        // あるため、次の単独版のイベント処理に任せる。
        try { end_async_modal(dialog, false); } catch (...) {}
        return modal_state_.active(id);
    }

    struct Native {
        Native(std::shared_ptr<Window> value, SDL_Window* handle, SDL_Renderer* raster,
               SDL_WindowID identity, bool in_host, bool owns, Rect bounds, std::uint64_t serial)
            : model(std::move(value)), window(handle), renderer(raster), id(identity),
              embedded(in_host), owns_handles(owns), hosted_bounds(bounds), generation(serial) {}
        std::shared_ptr<Window> model;
        SDL_Window* window{};
        bool numeric_text_input{};
        SDL_Renderer* renderer{};
        SDL_WindowID id{};
        std::shared_ptr<FrameUpdateState> updates{std::make_shared<FrameUpdateState>()};
        // 更新状態と同様、再入する配置・終了処理をまたいで保持する。
        std::shared_ptr<WindowAttributeStateMachine> attributes{std::make_shared<WindowAttributeStateMachine>()};
        std::shared_ptr<WindowMaximizeStateMachine> maximize{std::make_shared<WindowMaximizeStateMachine>()};
        bool embedded{};
        bool owns_handles{true};
        Rect hosted_bounds{};
        std::uint64_t generation{};
        // SDLのネイティブの親設定に成功してから取得する。 その後、
        // 実際の親を変更せずにモデル上の所有者だけが変わる場合がある。
        std::uint64_t parent_generation{};
        SDL_Texture* current_frame{};
        SDL_Texture* pending_frame{};
        SDL_Texture* opacity_layer{};
        int opacity_width{}, opacity_height{};
        int current_width{};
        int current_height{};
        int pending_width{};
        int pending_height{};
        std::uint64_t frame_sequence{};
        FrameAnimationSchedule animation;
        // ダイアログが指定した不変の論理寸法。描画は実体化し
        // たクライアント境界をモデルへ戻す場合があるが、 UI
        // 倍率変更は必ずこの基準から始め、倍率を重ねない。
        Rect authored_bounds{};

        bool pending_destroy{};
        std::shared_ptr<const WindowInputMask> input_mask;
        int input_mask_pixel_width{},input_mask_pixel_height{};
        bool input_mask_synchronized{};
    };

    struct NativeToken {
        Window* model{};
        SDL_WindowID id{};
        std::uint64_t generation{};
    };

    struct TextTextureKey {
        SDL_Renderer* renderer{};
        TTF_Font* font{};
        std::string text;
        std::uint32_t color{};
        bool antialias{true};
        bool operator==(const TextTextureKey&) const noexcept = default;
    };
    struct TextTextureLookup {
        SDL_Renderer* renderer{};
        TTF_Font* font{};
        std::string_view text;
        std::uint32_t color{};
        bool antialias{true};
    };
    struct TextTextureKeyHash {
        using is_transparent=void;
        static std::size_t hash(SDL_Renderer* renderer,TTF_Font* font,
                                std::string_view text,std::uint32_t color,
                                bool antialias) noexcept {
            std::size_t result = std::hash<void*>{}(renderer);
            const auto combine = [&result](const std::size_t hash) {
                result ^= hash + 0x9e3779b9U + (result << 6U) + (result >> 2U);
            };
            combine(std::hash<void*>{}(font));
            combine(std::hash<std::string_view>{}(text));
            combine(std::hash<std::uint32_t>{}(color));
            combine(std::hash<bool>{}(antialias));
            return result;
        }
        std::size_t operator()(const TextTextureKey& value) const noexcept {
            return hash(value.renderer,value.font,value.text,value.color,value.antialias);
        }
        std::size_t operator()(const TextTextureLookup& value) const noexcept {
            return hash(value.renderer,value.font,value.text,value.color,value.antialias);
        }
    };
    struct TextTextureKeyEqual {
        using is_transparent=void;
        static bool equal(SDL_Renderer* a_renderer,TTF_Font* a_font,std::string_view a_text,
                          std::uint32_t a_color,SDL_Renderer* b_renderer,TTF_Font* b_font,
                          std::string_view b_text,std::uint32_t b_color,
                          bool a_antialias,bool b_antialias) noexcept {
            return a_renderer==b_renderer && a_font==b_font && a_text==b_text &&
                   a_color==b_color && a_antialias==b_antialias;
        }
        bool operator()(const TextTextureKey& a,const TextTextureKey& b) const noexcept {
            return equal(a.renderer,a.font,a.text,a.color,b.renderer,b.font,b.text,b.color,
                         a.antialias,b.antialias);
        }
        bool operator()(const TextTextureKey& a,const TextTextureLookup& b) const noexcept {
            return equal(a.renderer,a.font,a.text,a.color,b.renderer,b.font,b.text,b.color,
                         a.antialias,b.antialias);
        }
        bool operator()(const TextTextureLookup& a,const TextTextureKey& b) const noexcept {
            return equal(a.renderer,a.font,a.text,a.color,b.renderer,b.font,b.text,b.color,
                         a.antialias,b.antialias);
        }
    };
    struct CachedTextTexture {
        SDL_Texture* texture{};
        float width{}, height{};
        std::list<const TextTextureKey*>::iterator recent;
    };

    struct ImageTextureKey {
        SDL_Renderer* renderer{};
        const ImageData* image{};
        std::size_t frame{};
        bool filtered{};
        bool operator==(const ImageTextureKey&) const noexcept = default;
    };
    struct ImageTextureKeyHash {
        std::size_t operator()(const ImageTextureKey& value) const noexcept {
            std::size_t result = std::hash<void*>{}(value.renderer);
            const auto combine = [&result](const std::size_t hash) {
                result ^= hash + 0x9e3779b9U + (result << 6U) + (result >> 2U);
            };
            combine(std::hash<const void*>{}(value.image));
            combine(std::hash<std::size_t>{}(value.frame));
            combine(std::hash<bool>{}(value.filtered));
            return result;
        }
    };
    struct CachedImageTexture {
        std::shared_ptr<const ImageData> image;
        SDL_Texture* texture{};
        std::size_t bytes{};
    };

    using TextLine = TextVisualLine;



    // OSへの問い合わせだけを行う。内容のサイズ・はみ出し・中央配置の方針は共通に保つ。
    static std::optional<Rect> native_client_bounds(SDL_Window* window,std::optional<Point> preferred={}) {
        SDL_Rect work{};
        auto display = window ? SDL_GetDisplayForWindow(window) : SDL_GetPrimaryDisplay();
        if(preferred && std::isfinite(preferred->x) && std::isfinite(preferred->y)) {
            const SDL_Point point{static_cast<int>(preferred->x),static_cast<int>(preferred->y)};
            if(const auto at_point=SDL_GetDisplayForPoint(&point)) display=at_point;
        }
        if (!SDL_GetDisplayUsableBounds(display, &work)) return {};
        int top{}, left{}, bottom{}, right{};
        if (window) SDL_GetWindowBordersSize(window, &top, &left, &bottom, &right);
        // SDLの位置はクライアント領域の原点を表す。 各辺にネイティブ
        // の枠を確保し、モニターの作業領域の負・非ゼロの原点を維持する。
        return Rect{static_cast<float>(work.x + left), static_cast<float>(work.y + top),
                    static_cast<float>(std::max(1, work.w - left - right)),
                    static_cast<float>(std::max(1, work.h - top - bottom))};
    }

    Rect surface_available_bounds(const Native& native) const {
        if(!native.embedded) return native_client_bounds(native.window).value_or(Rect{});
        if(host_options_.all_mode) return editor_hosted_canvas_bounds(host_options_);
        int width{},height{};SDL_GetWindowSize(native.window,&width,&height);
        SDL_Rect area{0,0,width,height};
        if(!SDL_GetWindowSafeArea(native.window,&area)) area={0,0,width,height};
        return {float(area.x),float(area.y),float(area.w),float(area.h)};
    }

    static bool apply_native_window_limits(SDL_Window* window,
                                           const std::optional<Size>& minimum,
                                           const std::optional<Size>& maximum) {
        if (!window) return false;
        const auto pixels=[](float value) {
            if(!std::isfinite(value) || value<=0) return 0;
            return static_cast<int>(std::min(static_cast<double>(value),
                static_cast<double>(std::numeric_limits<int>::max())));
        };
        // 制限を解除する場合も含め、先に古い上下限を両方外す。
        // 一部だけ失敗した更新を、同期完了として扱わない。
        bool accepted=SDL_SetWindowMinimumSize(window, 0, 0);
        accepted=SDL_SetWindowMaximumSize(window, 0, 0) && accepted;
        if (minimum) {
            accepted=SDL_SetWindowMinimumSize(window, pixels(minimum->width),
                                     pixels(minimum->height)) && accepted;
        }
        if (maximum) {
            accepted=SDL_SetWindowMaximumSize(window, pixels(maximum->width),
                                     pixels(maximum->height)) && accepted;
        }
        return accepted;
    }

    void synchronize_window_attributes(NativeToken token) {
        auto* native=native_for(token);
        if(!native) return;
        const auto model=native->model;
        const auto attributes=native->attributes;
        auto bounds=native->embedded ? native->hosted_bounds : model->bounds();
        if(!native->embedded) {
            int x{},y{},w{},h{};
            if(SDL_GetWindowPosition(native->window,&x,&y) && SDL_GetWindowSize(native->window,&w,&h))
                bounds={float(x),float(y),float(w),float(h)};
        }
        const WindowGeometryRequest geometry_request{bounds,surface_available_bounds(*native),
            {},native->embedded,WindowGeometryIntent::Explicit};
        auto requested=attributes->prepare(*model,geometry_request);
        requested.icon_image=graphics_ ? graphics_->retain_image(requested.icon) : nullptr;
        attributes->synchronize(requested,[this,token,model,geometry_request](WindowAttribute key,const WindowAttributes& value) {
            auto* current=native_for(token);
            if(!current) return WindowAttributeResult::Failed;
            const auto result=[updates=current->updates](bool accepted) {
                if(accepted) updates->request();
                return accepted ? WindowAttributeResult::Accepted : WindowAttributeResult::Failed;
            };
            switch(key) {
            case WindowAttribute::Title:
                // 組み込みの見出しは共通フレームの同じモデルを読む。 ALL
                // のホスト名を、現在の子ダイアログのタイトルへ変えない。
                return current->embedded ? result(true)
                    : result(SDL_SetWindowTitle(current->window,value.title.c_str()));
            case WindowAttribute::Icon: {
                // ALL は共通の見出しにアイコンを描く。 借用す
                // るネイティブのホストのアプリアイコンは変えない。
                if (current->embedded && (!host_options_.keep_top_bar || model->overlay_surface))
                    return WindowAttributeResult::Unsupported;
                // SDL3にはOS既定のアイコンへ戻す操作がなく、nullptr
                // も拒否する。 nullptrや空の代替アイ
                // コンを渡して、リセットできたことにしない。
                if (!value.icon) return current->embedded ? result(true) : WindowAttributeResult::Unsupported;
                if (!value.icon_image || value.icon_image->frames.empty()) return WindowAttributeResult::Failed;
                const auto& frame=value.icon_image->frames.front();
                if (frame.pixel_format!=PixelFormat::RGBA32) return WindowAttributeResult::Unsupported;
                const auto pixels=std::uint64_t(frame.pixel_width)*frame.pixel_height;
                if (!pixels || pixels>frame.pixels.size()/4U ||
                    frame.pixel_width>std::uint32_t(std::numeric_limits<int>::max()/4) ||
                    frame.pixel_height>std::uint32_t(std::numeric_limits<int>::max()))
                    return WindowAttributeResult::Failed;
                if (current->embedded) return result(true);
                // SDL_SetWindowIcon は同期的に変換・複製する
                // 。元画像の世代は requested が保持する。 ここで
                // はマスク・拡大縮小をせず、レンダラーのテクスチャーも不要。
                ImageSurface surface(SDL_CreateSurfaceFrom(int(frame.pixel_width),int(frame.pixel_height),
                    SDL_PIXELFORMAT_RGBA32,const_cast<std::byte*>(frame.pixels.data()),int(frame.pixel_width*4U)));
                if (!surface) return WindowAttributeResult::Failed;
                return result(SDL_SetWindowIcon(current->window,surface.get()));
            }
            case WindowAttribute::Resizable:
                // 組み込み画面の辺・角は、共通フレームの入力状態が管理する
                // 。 借用する ALL ホストのサイズ変更機能は変えない。
                return current->embedded ? ((!value.resizable ||
                    (host_options_.keep_top_bar && !model->overlay_surface))
                        ? result(true) : WindowAttributeResult::Unsupported)
                    : result(SDL_SetWindowResizable(current->window,value.resizable));
            case WindowAttribute::SizeLimits: {
                if(!current->embedded)
                    return result(apply_native_window_limits(current->window,value.minimum,value.maximum));
                const auto geometry=plan_window_geometry(*model,geometry_request);
                if(const auto old=current->hosted_bounds; old.x!=geometry.bounds.x || old.y!=geometry.bounds.y ||
                    old.width!=geometry.bounds.width || old.height!=geometry.bounds.height)
                    set_window_bounds(model,geometry.bounds);
                return result(true);
            }
            case WindowAttribute::AlwaysOnTop:
                window_stack_.reconcile_order(&modal_state_);
                return current->embedded ? result(true)
                    : result(SDL_SetWindowAlwaysOnTop(current->window,value.always_on_top));
            case WindowAttribute::Maximized: {
                const auto maximize=current->maximize;
                WindowMaximizeContext context;
                context.embedded=current->embedded;
                context.bounds=current->embedded ? current->hosted_bounds : geometry_request.bounds;
                context.available=geometry_request.available;
                context.timestamp=SDL_GetTicksNS();
                if(!current->embedded) context.native_maximized=(SDL_GetWindowFlags(current->window)&SDL_WINDOW_MAXIMIZED)!=0;
                context.live=[this,token] {return native_for(token)!=nullptr;};
                context.request_native=[this,token](bool maximize) {
                    const auto* native=native_for(token);
                    return native && !native->embedded && (maximize
                        ? SDL_MaximizeWindow(native->window) : SDL_RestoreWindow(native->window));
                };
                context.place=[this,token,model](const WindowGeometryPlan& plan) {
                    if(!native_for(token)) return false;
                    set_window_bounds(model,plan.bounds);
                    return native_for(token)!=nullptr;
                };
                return maximize->update(model,std::move(context));
            }
            default:
                // ネイティブの変更済み表示と、独立した最小化・最大化ボタンに
                // は、それぞれ対応機能を考慮するアダプターが必要。 仮のタイ
                // トル接尾辞、借用ホストの最大化、見かけの成功で代用しない。
                return WindowAttributeResult::Unsupported;
            }
        },[this,token] { return native_for(token)!=nullptr; });
    }

    Native& ensure(const std::shared_ptr<Window>& model) {
        if (const auto found = windows_.find(model.get()); found != windows_.end()) return found->second;
        const auto bounds=model->bounds();
        const auto authored_bounds=authored_window_bounds(*model);
        if (host_options_.all_mode && host_options_.native_sdl_window != 0 &&
            host_options_.native_renderer != 0) {
            auto* handle = reinterpret_cast<SDL_Window*>(host_options_.native_sdl_window);
            auto* renderer = reinterpret_cast<SDL_Renderer*>(host_options_.native_renderer);
            const Rect host=editor_hosted_canvas_bounds(host_options_);

            std::optional<Rect> anchor;
            if (const auto owner = model->owner.lock()) {
                const auto owner_native = windows_.find(owner.get());
                if (owner_native != windows_.end() && owner_native->second.embedded &&
                    owner_native->second.window==handle)
                    anchor = owner_native->second.hosted_bounds;
            }
            const auto hosted=plan_window_geometry(
                *model,{hosted_window_outer_bounds(*model,bounds,host_options_.keep_top_bar),
                        host,anchor,true}).bounds;
            Native native{model, handle, renderer, SDL_GetWindowID(handle), true,
                          false, hosted, next_window_generation_++};
            native.authored_bounds = authored_bounds;
            return windows_.emplace(model.get(), std::move(native)).first->second;
        }
        if ((native_platform() == Platform::Android || native_platform() == Platform::IOS ||
             native_platform() == Platform::Web) &&
            single_surface_root_) {
            const auto root = windows_.find(single_surface_root_);
            if (root != windows_.end() && root->second.window && root->second.renderer) {
                int root_width{},root_height{};
                SDL_GetWindowSize(root->second.window,&root_width,&root_height);
                SDL_Rect safe{0,0,root_width,root_height};
                if(!SDL_GetWindowSafeArea(root->second.window,&safe)) safe={0,0,root_width,root_height};
                const Rect anchor{float(safe.x),float(safe.y),float(safe.w),float(safe.h)};
                std::optional<Rect> owner_bounds;
                if (const auto owner = model->owner.lock()) {
                    const auto found=windows_.find(owner.get());
                    if(found!=windows_.end() && found->second.window==root->second.window)
                        owner_bounds=found->second.embedded ? found->second.hosted_bounds : anchor;
                }
                const auto hosted=plan_window_geometry(
                    *model,{hosted_window_outer_bounds(*model,bounds,host_options_.keep_top_bar),
                            anchor,owner_bounds,true}).bounds;
                Native native{model, root->second.window, root->second.renderer,
                              root->second.id, true, false, hosted,
                              next_window_generation_++};
                native.authored_bounds = authored_bounds;
                return windows_.emplace(model.get(), std::move(native)).first->second;
            }
            single_surface_root_ = nullptr;
        }
        SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
        SDL_Window* parent_handle{};
        if (const auto owner=model->owner.lock()) {
            const auto parent=windows_.find(owner.get());
            if (parent!=windows_.end()) parent_handle=parent->second.window;
        }
        // 保存済みのデスクトップ位置は、主モニターではなく、その位置のモニターを選ぶ。
        const auto requested_display=native_client_bounds(parent_handle,
            parent_handle && model->center_on_owner ? std::optional<Point>{} : Point{bounds.x,bounds.y});
        const auto initial=plan_window_geometry(*model,{bounds,requested_display.value_or(Rect{})});
        int width=static_cast<int>(initial.bounds.width),height=static_cast<int>(initial.bounds.height);
        if (model->resizable) flags |= SDL_WINDOW_RESIZABLE;
        if (model->utility_window) flags |= SDL_WINDOW_UTILITY;
        // show() で表示する前に、配置とサイズ制限を確定する。
        flags |= SDL_WINDOW_HIDDEN;
        // 表示後の変更も含め、クライアント領域の不透明度は共通フレームが管理す
        // る。 不透明なフレームもこのバッファを完全に覆い、ネイティブの不透
        // 明度を重ねたり、 OS固有のクライアント合成処理を使ったりしない。
        flags |= SDL_WINDOW_TRANSPARENT;
        if (model->overlay_surface)
            flags |= SDL_WINDOW_BORDERLESS | SDL_WINDOW_NOT_FOCUSABLE;
        // 完全な Native 項目の登録後にだけ所有権を移す
        // 。二つのSDL資源の生成後にも、 状態の確保・登録が
        // 例外を出す場合がある。 無関係な登録済みの画面を無効
        // 化せず、レンダラー、ウィンドウの順に解放して戻す。
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> pending_window(
            SDL_CreateWindow(model->title.c_str(), width, height, flags), SDL_DestroyWindow);
        auto* handle = pending_window.get();
        if (!handle) throw std::runtime_error(std::string("SDL_CreateWindow: ") + SDL_GetError());
        std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> pending_renderer(
            SDL_CreateRenderer(handle, nullptr), SDL_DestroyRenderer);
        auto* renderer = pending_renderer.get();
        if (!renderer) throw std::runtime_error(std::string("SDL_CreateRenderer: ") + SDL_GetError());
        configure_standalone_present_mode(renderer, present_mode_);
        if (frame_profile_.enabled) {
            int actual{};
            const bool queried = SDL_GetRenderVSync(renderer, &actual);
            int output_width{}, output_height{};
            (void)SDL_GetRenderOutputSize(renderer, &output_width, &output_height);
            const char* driver = SDL_GetRendererName(renderer);
            editor_action_log("Editor native surface renderer=" +
                std::string(driver ? driver : "unknown") + " client=" +
                std::to_string(width) + "x" + std::to_string(height) +
                " output=" + std::to_string(output_width) + "x" +
                std::to_string(output_height) + " pixel_density=" +
                std::to_string(SDL_GetWindowPixelDensity(handle)) +
                " display_scale=" + std::to_string(SDL_GetWindowDisplayScale(handle)) +
                " transparent=" + std::to_string(
                    (SDL_GetWindowFlags(handle) & SDL_WINDOW_TRANSPARENT) != 0));
            editor_action_log("Editor present mode requested=" +
                std::string(present_mode_name(present_mode_)) + " actual=" +
                (queried ? std::to_string(actual) : std::string("unavailable")));
        }
        // 先に指定されたモニター・所有者へ移し、その後で実際のクライアン
        // トの作業領域を調べる。 ネイティブの枠は親と異なる場合がある。
        std::optional<Rect> owner_bounds;
        if(parent_handle) {
            int x{},y{},w{},h{};
            SDL_GetWindowPosition(parent_handle,&x,&y);SDL_GetWindowSize(parent_handle,&w,&h);
            owner_bounds=Rect{float(x),float(y),float(w),float(h)};
        }
        const auto provisional=plan_window_geometry(*model,
            {bounds,requested_display.value_or(Rect{}),owner_bounds});
        SDL_SetWindowPosition(handle,int(provisional.bounds.x),int(provisional.bounds.y));
        const auto placement=plan_window_geometry(*model,
            {bounds,native_client_bounds(handle).value_or(Rect{}),owner_bounds});
        apply_native_window_limits(handle,placement.minimum,placement.maximum);
        SDL_SetWindowSize(handle,int(placement.bounds.width),int(placement.bounds.height));
        SDL_SetWindowPosition(handle,int(placement.bounds.x),int(placement.bounds.y));
        if(model->always_on_top) SDL_SetWindowAlwaysOnTop(handle,true);
        Native native{model, handle, renderer, SDL_GetWindowID(handle), false, true,
                      {}, next_window_generation_++};
        if(parent_handle && SDL_SetWindowParent(handle,parent_handle)) {
            for(const auto& [_, parent]:windows_)
                if(parent.owns_handles && parent.window==parent_handle) {
                    native.parent_generation=parent.generation;break;
                }
        }
        native.authored_bounds = authored_bounds;
        const auto [entry, inserted] = windows_.try_emplace(model.get(), std::move(native));
        if (!inserted) return entry->second;
        (void)pending_renderer.release();
        (void)pending_window.release();
        // 初期の中央配置は、SDLが実際の画面を選び、枠を
        // 考慮したクライアントの作業領域を返してから確定す
        // る。 実際のデスクトップ原点を直ちにモデルへ戻
        // し、終了時の保存が中央配置済みの位置を読むよう
        // にする。 そうしないと、初回終了で未保存の (0,0)
        // 指定が正しい位置を上書きしてしまう。
        model->set_bounds(placement.bounds);
        entry->second.authored_bounds = authored_window_bounds(*model);
        if (native_platform() == Platform::Android || native_platform() == Platform::IOS ||
            native_platform() == Platform::Web)
            single_surface_root_ = model.get();
        return entry->second;
    }

    void destroy(Native& native) noexcept {
        if(native.owns_handles) {
            text_input_publication_.retire(native.id);
            external_drop_.retire(native.id);
            pen_input_.retire(native.id);
        }
        clear_text_texture_cache(native.renderer);
        clear_image_texture_cache(native.renderer);
        if (native.current_frame) SDL_DestroyTexture(native.current_frame);
        if (native.pending_frame) SDL_DestroyTexture(native.pending_frame);
        if (native.opacity_layer) SDL_DestroyTexture(native.opacity_layer);
        native.opacity_layer = nullptr;
        native.current_frame = nullptr;
        native.pending_frame = nullptr;
        if (native.owns_handles && native.renderer) SDL_DestroyRenderer(native.renderer);
        if (native.owns_handles && native.window) SDL_DestroyWindow(native.window);
        native.renderer = nullptr;
        native.window = nullptr;
    }

    [[nodiscard]] bool retiring(std::shared_ptr<Window> window) const {
        std::unordered_set<const Window*> visited;
        for (;window && visited.insert(window.get()).second;window=window->owner.lock()) {
            const auto found=windows_.find(window.get());
            if (found!=windows_.end() && found->second.pending_destroy) return true;
        }
        return false;
    }

    void finish_frame(SDL_Renderer* renderer) noexcept {
        const auto users=frame_users_.find(renderer);
        if (users==frame_users_.end() || --users->second!=0) return;
        frame_users_.erase(users);
        // 子のレンダラーの処理完了により、別のレンダラーの親が解放可能になる場合がある。
        drain_native_retirements();
    }

    void drain_native_retirements() noexcept {
        if(!native_retirements_pending_ || releasing_native_) return;
        releasing_native_=true;
        struct Reset {bool& active;~Reset() {active=false;}} reset{releasing_native_};
        try {
            for(;;) {
                std::vector<SurfaceLifetimeEntry> entries;
                entries.reserve(windows_.size());
                native_retirements_pending_=false;
                for(const auto& [_, native]:windows_) {
                    entries.push_back({native.generation,native.parent_generation,
                        reinterpret_cast<std::uintptr_t>(native.renderer),native.owns_handles,native.pending_destroy});
                    native_retirements_pending_|=native.pending_destroy;
                }
                if(!native_retirements_pending_) return;
                std::vector<std::uintptr_t> busy;
                for(const auto& [renderer,users]:frame_users_)
                    if(users) busy.push_back(reinterpret_cast<std::uintptr_t>(renderer));
                const auto order=plan_surface_release(entries,busy);
                if(order.empty()) return;
                const auto found=std::find_if(windows_.begin(),windows_.end(),
                    [&](const auto& item) {return item.second.generation==order.front();});
                if(found==windows_.end()) return;
                const auto key=found->first;
                if (accessibility_) accessibility_->retire(found->second.id);
                destroy(found->second);
                windows_.erase(key);
                // ネイティブの解放後に、もう一度状態を取得する。 再入した終了
                // が別の資源を追加している場合があるため、古い計画を使わない。
            }
        } catch(...) {
            // 確保失敗で使用中の親を破棄したり、描画スコープのデストラクタ
            // ーから例外を出したりしない。 次のイベント処理で再試行する。
            native_retirements_pending_=true;
        }
    }

    [[nodiscard]] static bool prepare_pending_frame(Native& native, int width,
                                                     int height) {
        if (!native.renderer || width <= 0 || height <= 0)
            return false;
        if (native.pending_frame &&
            (native.pending_width != width || native.pending_height != height)) {
            SDL_DestroyTexture(native.pending_frame);
            native.pending_frame = nullptr;
        }
        if (!native.pending_frame) {
            native.pending_frame = SDL_CreateTexture(
                native.renderer, SDL_PIXELFORMAT_RGBA32,
                SDL_TEXTUREACCESS_TARGET, width, height);
            if (!native.pending_frame) return false;
            SDL_SetTextureBlendMode(native.pending_frame, SDL_BLENDMODE_NONE);
            native.pending_width = width;
            native.pending_height = height;
        }
        return true;
    }

    Native* native_for(SDL_WindowID id) {
        return native_for(window_stack_.front(id));
    }

    Native* native_for(const std::shared_ptr<Window>& model) {
        if (!model) return nullptr;
        const auto found=windows_.find(model.get());
        return found==windows_.end() || found->second.pending_destroy ? nullptr : &found->second;
    }

    [[nodiscard]] static NativeToken token_for(const Native& native) noexcept {
        return {native.model.get(), native.id, native.generation};
    }

    Native* native_for(const NativeToken token) {
        const auto found = windows_.find(token.model);
        if (found == windows_.end() || found->second.id != token.id ||
            found->second.generation != token.generation || found->second.pending_destroy)
            return nullptr;
        return &found->second;
    }

    void refresh_ui_font() {
        font_ = fonts_->font_for(effective_editor_widget_font());
    }

    TTF_Font* font_for(const FontSpec& specification) {
        // 描画用に指定されたフォントには、ここで通常のUI文字倍率を追加適用しない。
        return fonts_->font_for(specification);
    }

    TTF_Font* font_for(const WidgetPtr& widget) {
        return font_for(effective_editor_widget_font(widget&&widget->font()?&*widget->font():nullptr));
    }


    // D版の images.cursor(CType) と同様、コンテントのアイコンを使い、中心を操作点にする。
    SDL_Cursor* custom_cursor(const ResourceId id) {
        if (!id || !graphics_) return nullptr;
        // イベントビューのコンテントアイコンの graphPx など、
        // 他の表示ではエディタのUI倍率を使う。 カーソル画像にも同じ
        // 倍率を適用し、倍率変更時はキャッシュしたカーソルを作り直す。
        const float ui_scale = std::max(1.0F, editor_ui_text_scale());
        if (ui_scale != custom_cursor_scale_) {
            for (auto& [_, cached] : custom_cursors_) if (cached) SDL_DestroyCursor(cached);
            custom_cursors_.clear();
            custom_cursor_scale_ = ui_scale;
        }
        if (const auto found = custom_cursors_.find(id); found != custom_cursors_.end())
            return found->second;
        SDL_Cursor* cursor = nullptr;
        const auto* image = graphics_->image(id);
        if (image && !image->frames.empty()) {
            const auto& frame = image->frames.front();
            SDL_PixelFormat format = SDL_PIXELFORMAT_UNKNOWN;
            int bytes = 0;
            switch (frame.pixel_format) {
            case PixelFormat::RGBA32: format = SDL_PIXELFORMAT_RGBA32; bytes = 4; break;
            case PixelFormat::BGRA32: format = SDL_PIXELFORMAT_BGRA32; bytes = 4; break;
            case PixelFormat::RGB24: format = SDL_PIXELFORMAT_RGB24; bytes = 3; break;
            case PixelFormat::BGR24: format = SDL_PIXELFORMAT_BGR24; bytes = 3; break;
            default: break;
            }
            const int width = static_cast<int>(frame.pixel_width);
            const int height = static_cast<int>(frame.pixel_height);
            if (bytes && width > 0 && height > 0 &&
                frame.pixels.size() >= static_cast<std::size_t>(width) * height * bytes) {
                if (SDL_Surface* surface = SDL_CreateSurfaceFrom(
                        width, height, format,
                        const_cast<std::byte*>(frame.pixels.data()), width * bytes)) {
                    const int scaled_width = std::max(1, static_cast<int>(std::lround(width * ui_scale)));
                    const int scaled_height = std::max(1, static_cast<int>(std::lround(height * ui_scale)));
                    SDL_Surface* scaled = (scaled_width != width || scaled_height != height)
                        ? SDL_ScaleSurface(surface, scaled_width, scaled_height, SDL_SCALEMODE_NEAREST)
                        : nullptr;
                    SDL_Surface* source = scaled ? scaled : surface;
                    cursor = SDL_CreateColorCursor(source, source->w / 2, source->h / 2);
                    if (scaled) SDL_DestroySurface(scaled);
                    SDL_DestroySurface(surface);
                }
            }
        }
        custom_cursors_[id] = cursor;
        return cursor;
    }

    void update_cursor(const CursorKind kind) {
        if (kind == CursorKind::Custom) {
            if (SDL_Cursor* cursor = custom_cursor(active_custom_cursor())) {
                SDL_SetCursor(cursor);
                return;
            }
            update_cursor(CursorKind::Default);
            return;
        }
        const auto found = cursors_.find(kind);
        if (found != cursors_.end()) {
            SDL_SetCursor(found->second);
            return;
        }
        SDL_SystemCursor system = SDL_SYSTEM_CURSOR_DEFAULT;
        switch (kind) {
        case CursorKind::Text: system = SDL_SYSTEM_CURSOR_TEXT; break;
        case CursorKind::Hand: system = SDL_SYSTEM_CURSOR_POINTER; break;
        case CursorKind::Move: system = SDL_SYSTEM_CURSOR_MOVE; break;
        case CursorKind::Crosshair: system = SDL_SYSTEM_CURSOR_CROSSHAIR; break;
        case CursorKind::Wait: system = SDL_SYSTEM_CURSOR_WAIT; break;
        case CursorKind::Forbidden: system = SDL_SYSTEM_CURSOR_NOT_ALLOWED; break;
        case CursorKind::SizeHorizontal: system = SDL_SYSTEM_CURSOR_EW_RESIZE; break;
        case CursorKind::SizeVertical: system = SDL_SYSTEM_CURSOR_NS_RESIZE; break;
        case CursorKind::SizeDiagonalNorthEast: system = SDL_SYSTEM_CURSOR_NESW_RESIZE; break;
        case CursorKind::SizeDiagonalNorthWest: system = SDL_SYSTEM_CURSOR_NWSE_RESIZE; break;
        default: break;
        }
        if (SDL_Cursor* cursor = SDL_CreateSystemCursor(system)) {
            cursors_[kind] = cursor;
            SDL_SetCursor(cursor);
        }
    }

    void fill(SDL_Renderer* renderer, Rect rect, SDL_Color color) {
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
        SDL_FRect native = sdl_rect(rect);
        SDL_RenderFillRect(renderer, &native);
    }

    void border(SDL_Renderer* renderer, Rect rect, SDL_Color color) {
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
        SDL_FRect native = sdl_rect(rect);
        SDL_RenderRect(renderer, &native);
    }

    void text(SDL_Renderer* renderer, std::string_view value, float x, float y,
              SDL_Color color = kText,
              TTF_Font* selected_font = nullptr, const float rotation = 0.0F,
              const bool antialias = true) {
        if(!selected_font)selected_font=font_;
        if (!selected_font || value.empty()) return;
        const std::uint32_t packed_color = static_cast<std::uint32_t>(color.r) |
            (static_cast<std::uint32_t>(color.g) << 8U) |
            (static_cast<std::uint32_t>(color.b) << 16U) |
            (static_cast<std::uint32_t>(color.a) << 24U);
        const TextTextureLookup lookup{renderer,selected_font,value,packed_color,antialias};
        if (const auto found = text_texture_cache_.find(lookup);
            found != text_texture_cache_.end()) {
            text_texture_recency_.splice(text_texture_recency_.begin(),
                text_texture_recency_, found->second.recent);
            SDL_FRect destination{x, y, found->second.width, found->second.height};
            if (rotation != 0.0F) {
                destination.x += (found->second.height - found->second.width) * 0.5F;
                destination.y += (found->second.width - found->second.height) * 0.5F;
                (void)SDL_RenderTextureRotated(renderer, found->second.texture, nullptr,
                                               &destination, rotation, nullptr,
                                               SDL_FLIP_NONE);
            } else {
                (void)SDL_RenderTexture(renderer, found->second.texture, nullptr, &destination);
            }
            return;
        }
        SDL_Surface* surface = antialias
            ? TTF_RenderText_Blended(selected_font, value.data(), value.size(), color)
            : TTF_RenderText_Solid(selected_font, value.data(), value.size(), color);
        if (!surface) return;
        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
        std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> texture_owner(texture, SDL_DestroyTexture);
        const CachedTextTexture cached{texture, static_cast<float>(surface->w),
                                      static_cast<float>(surface->h), {}};
        SDL_DestroySurface(surface);
        if (!texture) return;
        constexpr std::size_t maximum_text_textures = 4096U;
        // 上限に達しても、全レンダラーの文字テクスチャーを一度に破棄し
        // ない。 最も長く使っていない一件を除き、よく使うものを残す。
        if (text_texture_cache_.size() >= maximum_text_textures) {
            const auto oldest = text_texture_cache_.find(*text_texture_recency_.back());
            if (oldest->second.texture) SDL_DestroyTexture(oldest->second.texture);
            text_texture_recency_.pop_back();
            text_texture_cache_.erase(oldest);
            if (!text_texture_limit_logged_) {
                text_texture_limit_logged_ = true;
                editor_action_log("PERF cache=text.texture policy=lru capacity=4096 first_eviction");
            }
        }
        const auto [inserted, added] = text_texture_cache_.emplace(
            TextTextureKey{renderer,selected_font,std::string(value),packed_color,antialias},cached);
        if (added) {
            try {
                text_texture_recency_.push_front(&inserted->first);
                inserted->second.recent = text_texture_recency_.begin();
            } catch (...) {
                text_texture_cache_.erase(inserted);
                throw;
            }
            (void)texture_owner.release();
        }
        SDL_FRect destination{x, y, inserted->second.width, inserted->second.height};
        if (rotation != 0.0F) {
            destination.x += (inserted->second.height - inserted->second.width) * 0.5F;
            destination.y += (inserted->second.width - inserted->second.height) * 0.5F;
            (void)SDL_RenderTextureRotated(renderer, inserted->second.texture, nullptr,
                                           &destination, rotation, nullptr,
                                           SDL_FLIP_NONE);
        } else {
            (void)SDL_RenderTexture(renderer, inserted->second.texture, nullptr, &destination);
        }
    }

    void clear_text_texture_cache(SDL_Renderer* renderer = nullptr) noexcept {
        for (auto it = text_texture_cache_.begin(); it != text_texture_cache_.end();) {
            if (renderer && it->first.renderer != renderer) {
                ++it;
                continue;
            }
            if (it->second.texture) SDL_DestroyTexture(it->second.texture);
            text_texture_recency_.erase(it->second.recent);
            it = text_texture_cache_.erase(it);
        }
    }

    [[nodiscard]] static SDL_Color widget_color(const WidgetPtr& widget,
                                                const SDL_Color fallback) noexcept {
        if (!widget || !widget->foreground()) return fallback;
        const Color color = *widget->foreground();
        return {color.red, color.green, color.blue, color.alpha};
    }

    static void set_native_clip(SDL_Renderer* renderer,Rect bounds) {
        std::optional<RasterClip> inherited;
        SDL_Rect previous{};
        if(SDL_RenderClipEnabled(renderer) && SDL_GetRenderClipRect(renderer,&previous))
            inherited=RasterClip{previous.x,previous.y,previous.w,previous.h};
        const auto prepared=prepare_raster_clip(bounds,inherited);
        const SDL_Rect clip{prepared.x,prepared.y,prepared.width,prepared.height};
        SDL_SetRenderClipRect(renderer,&clip);
    }
    class NativeClipScope {
    public:
        NativeClipScope(SDL_Renderer* renderer,Rect bounds):renderer_(renderer) {
            enabled_=SDL_RenderClipEnabled(renderer_);
            if(enabled_) SDL_GetRenderClipRect(renderer_,&previous_);
            set_native_clip(renderer_,bounds);
        }
        ~NativeClipScope() {SDL_SetRenderClipRect(renderer_,enabled_ ? &previous_:nullptr);}
        NativeClipScope(const NativeClipScope&)=delete;
        NativeClipScope& operator=(const NativeClipScope&)=delete;
    private:
        SDL_Renderer* renderer_;
        SDL_Rect previous_{};
        bool enabled_{};
    };
    [[nodiscard]] static float measured_width(TTF_Font* selected_font,
                                               std::string_view value) {
        if (!selected_font || value.empty()) return 0.0F;
        int width{}, height{};
        return TTF_GetStringSize(selected_font, value.data(), value.size(), &width, &height)
            ? static_cast<float>(width) : 0.0F;
    }


    Size measure_authored_text(std::string_view value,const FontSpec& font,float wrap_width) {
        return fonts_->measure_text(value,font,wrap_width);
    }
    class SdlDrawingContext final : public DrawingContext {
    public:
        SdlDrawingContext(SdlGuiBackend& owner, SDL_Renderer* renderer, Rect bounds)
            : owner_(owner), renderer_(renderer),
              origin_{bounds.x, bounds.y},
              bounds_{0.0F, 0.0F, bounds.width, bounds.height} {
            inherited_clip_enabled_ = SDL_RenderClipEnabled(renderer_);
            if (inherited_clip_enabled_)
                SDL_GetRenderClipRect(renderer_, &inherited_clip_);
            SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
            clip(bounds_);
        }
        ~SdlDrawingContext() override {
            SDL_SetRenderClipRect(renderer_, inherited_clip_enabled_
                                             ? &inherited_clip_ : nullptr);
        }

        void save() override {
            SDL_Rect rect{};
            states_.push_back({SDL_RenderClipEnabled(renderer_), {}, antialias_});
            if (states_.back().enabled && SDL_GetRenderClipRect(renderer_, &rect))
                states_.back().rect = rect;
        }
        void restore() override {
            if (states_.empty()) return;
            const State state = states_.back();
            states_.pop_back();
            SDL_SetRenderClipRect(renderer_, state.enabled ? &state.rect : nullptr);
            antialias_ = state.antialias;
        }
        void clip(Rect rect) override {
            rect.x+=origin_.x; rect.y+=origin_.y;
            set_native_clip(renderer_,rect);
        }
        void set_antialias(const bool value) override { antialias_ = value; }
        void clear(Color color) override { fill_rect(bounds_, color); }
        void draw_line(Point from, Point to, Color color, float width,
                       StrokeStyle style) override {
            const std::array<Point,2> path{from,to};
            render_geometry(stroke_path(path,false,width,style),color, true);
        }
        void draw_rect(Rect rect, Color color, float width, StrokeStyle style) override {
            render_geometry(stroke_path(rectangle_path(rect),true,width,style),color, true);
        }
        bool draw_xor_rect(Rect rect,Color color,float width) override {
            if(!std::isfinite(rect.x)||!std::isfinite(rect.y)||!std::isfinite(rect.width)||!std::isfinite(rect.height)||
               rect.width<0||rect.height<0||!std::isfinite(width)||width<0)return false;
            rect.x+=origin_.x;rect.y+=origin_.y;
            int output_width{},output_height{};
            if(!SDL_GetCurrentRenderOutputSize(renderer_,&output_width,&output_height))return false;
            RasterClip clip{0,0,output_width,output_height};
            SDL_Rect inherited{};
            if(SDL_RenderClipEnabled(renderer_) && SDL_GetRenderClipRect(renderer_,&inherited))
                clip=prepare_raster_clip({float(inherited.x),float(inherited.y),float(inherited.w),float(inherited.h)},clip);
            // 重ならない線の帯だけを読み戻す。 共通の形状計算で、矩
            // 形内部を取得せずに正確なXOR・アルファの結果を保つ。
            for(const auto band:xor_rectangle_regions(rect,width,clip)) {
            const SDL_Rect native{band.x,band.y,band.width,band.height};
            ImageSurface surface(SDL_RenderReadPixels(renderer_,&native));if(!surface)return false;
            std::string error;auto pixels=image_frame_from_surface(surface.get(),{},std::chrono::milliseconds{},error);
            if(!error.empty()||!xor_rectangle_pixels(pixels,
                {rect.x-native.x,rect.y-native.y,rect.width,rect.height},color,width))return false;
            // 読み戻し・アップロードは必要なSDLの基本操作。 RGBのXORと
            // 被覆計算は共通化し、アルファとクリッピングは出力先の状態を保つ。
            SDL_Texture* texture=SDL_CreateTexture(renderer_,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,
                static_cast<int>(pixels.pixel_width),static_cast<int>(pixels.pixel_height));
            if(!texture)return false;
            struct TextureGuard{SDL_Texture* value;~TextureGuard(){SDL_DestroyTexture(value);}}guard{texture};
            if(!SDL_UpdateTexture(texture,nullptr,pixels.pixels.data(),static_cast<int>(pixels.pixel_width*4))||
               !SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_NONE)||!SDL_SetTextureScaleMode(texture,SDL_SCALEMODE_NEAREST))return false;
            const SDL_FRect destination{float(native.x),float(native.y),float(native.w),float(native.h)};
            if(!SDL_RenderTexture(renderer_,texture,nullptr,&destination))return false;
            }
            return true;
        }
        void fill_rect(Rect rect, Color color) override {
            rect.x += origin_.x;
            rect.y += origin_.y;
            owner_.fill(renderer_, rect, {color.red,color.green,color.blue,color.alpha});
        }
        void draw_polygon(std::span<const Point> points, Color color, float width,
                          StrokeStyle style) override {
            render_geometry(stroke_path(points,true,width,style),color, true);
        }
        void fill_polygon(std::span<const Point> points, Color color, FillRule rule) override {
            render_geometry(polygon_geometry(points,rule),color);
        }
        void draw_ellipse(Rect rect, Color color, float width, StrokeStyle style) override {
            render_geometry(stroke_path(ellipse_path(rect),true,width,style),color, true);
        }
        void fill_ellipse(Rect rect, Color color) override {
            render_geometry(polygon_geometry(ellipse_path(rect)),color);
        }
        void fill_gradient(Rect rect, Color first, Color second,
                           GradientAxis axis, CompositeMode mode) override {
            rect.x += origin_.x;
            rect.y += origin_.y;
            const SDL_Rect native{
                static_cast<int>(std::floor(rect.x)),
                static_cast<int>(std::floor(rect.y)),
                std::max(0, static_cast<int>(std::ceil(rect.width))),
                std::max(0, static_cast<int>(std::ceil(rect.height)))};
            if (native.w <= 0 || native.h <= 0) return;
            const auto interpolate = [](const Uint8 from, const Uint8 to,
                                        const int part) {
                const int delta = static_cast<int>(to) - static_cast<int>(from);
                return static_cast<Uint8>(std::clamp(
                    static_cast<int>(from) +
                        ((delta * part + (delta >= 0 ? 128 : -128)) >> 8),
                    0, 255));
            };
            const auto source = [&](const int x, const int y) {
                const int part = axis == GradientAxis::Horizontal
                    ? (x << 8) / std::max(1, native.w)
                    : axis == GradientAxis::Vertical
                        ? (y << 8) / std::max(1, native.h) : 0;
                return Color{interpolate(first.red, second.red, part),
                             interpolate(first.green, second.green, part),
                             interpolate(first.blue, second.blue, part),
                             interpolate(first.alpha, second.alpha, part)};
            };
            if (mode == CompositeMode::Normal || mode == CompositeMode::Mask) {
                for (int offset = 0;
                     offset < (axis == GradientAxis::Horizontal ? native.w :
                               axis == GradientAxis::Vertical ? native.h : 1);
                     ++offset) {
                    const auto color = source(axis == GradientAxis::Horizontal ? offset : 0,
                                              axis == GradientAxis::Vertical ? offset : 0);
                    const Rect strip = axis == GradientAxis::Horizontal
                        ? Rect{rect.x + offset, rect.y, 1.0F, rect.height}
                        : axis == GradientAxis::Vertical
                            ? Rect{rect.x, rect.y + offset, rect.width, 1.0F}
                            : rect;
                    owner_.fill(renderer_, strip,
                                {color.red,color.green,color.blue,color.alpha});
                }
                return;
            }
            SDL_Surface* surface = SDL_RenderReadPixels(renderer_, &native);
            if (!surface) return;
            if (!SDL_LockSurface(surface)) {
                SDL_DestroySurface(surface);
                return;
            }
            const auto fround = [](const int value) {
                return std::clamp((value + 128) >> 8, 0, 255);
            };
            for (int y = 0; y < native.h; ++y) for (int x = 0; x < native.w; ++x) {
                Uint8 r{},g{},b{},a{};
                if (!SDL_ReadSurfacePixel(surface,x,y,&r,&g,&b,&a)) continue;
                const auto filter=source(x,y);
                if (filter.alpha == 0U) continue;
                const auto blend = [&](const Uint8 destination, const Uint8 value) {
                    const int d=destination, s=value, alpha=filter.alpha;
                    if (mode == CompositeMode::Add)
                        return std::clamp(fround(d*(255-alpha)+(d+s)*alpha),0,255);
                    if (mode == CompositeMode::Subtract)
                        return std::max(fround(d*(255-alpha)),
                                        std::clamp(d-fround(s*alpha),0,255));
                    const int factor = alpha == 255 ? s
                        : fround(s*alpha+(255-alpha)*255);
                    return fround(d*factor);
                };
                SDL_WriteSurfacePixel(surface,x,y,
                    static_cast<Uint8>(blend(r,filter.red)),
                    static_cast<Uint8>(blend(g,filter.green)),
                    static_cast<Uint8>(blend(b,filter.blue)),a);
            }
            SDL_UnlockSurface(surface);
            SDL_Texture* texture=SDL_CreateTextureFromSurface(renderer_,surface);
            SDL_DestroySurface(surface);
            if (!texture) return;
            SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_NONE);
            const SDL_FRect destination{static_cast<float>(native.x),
                                        static_cast<float>(native.y),
                                        static_cast<float>(native.w),
                                        static_cast<float>(native.h)};
            (void)SDL_RenderTexture(renderer_,texture,nullptr,&destination);
            SDL_DestroyTexture(texture);
        }
        void draw_text(std::string_view value, Point origin, const FontSpec& specification, Color color) override {
            owner_.text(renderer_,value,origin.x + origin_.x,origin.y + origin_.y,
                        {color.red,color.green,color.blue,color.alpha},
                        owner_.font_for(specification),0.0F,specification.antialias);
        }
        void draw_text_rotated(std::string_view value, Point origin,
                               const FontSpec& specification, Color color,
                               float degrees) override {
            owner_.text(renderer_, value, origin.x + origin_.x, origin.y + origin_.y,
                        {color.red,color.green,color.blue,color.alpha},
                        owner_.font_for(specification),degrees,specification.antialias);
        }
        void draw_image(ResourceId image, Rect source, Rect destination,
                        Interpolation interpolation) override {
            destination.x += origin_.x;
            destination.y += origin_.y;
            owner_.render_image(renderer_,image,0U,destination,source,interpolation);
        }
        void draw_image_with_alpha(ResourceId image, Rect source, Rect destination,
                                   Interpolation interpolation, std::uint8_t alpha) override {
            if (alpha == 255U) { draw_image(image,source,destination,interpolation); return; }
            const auto original=owner_.graphics_ ? owner_.graphics_->retain_image(image) : nullptr;
            if (!original) return;
            auto faded=std::make_shared<ImageData>(*original);
            apply_image_opacity(*faded,alpha);
            destination.x += origin_.x;
            destination.y += origin_.y;
            // 一時画像は、描画のたびにテクスチャーキャッシュを増やさない。
            owner_.render_image_data(renderer_,{},*faded,0U,destination,source,interpolation);
        }
        [[nodiscard]] Size measure_text(std::string_view value, const FontSpec& font,
                                        float wrap_width) override {
            return owner_.measure_authored_text(value,font,wrap_width);
        }
        FontMetrics font_metrics(const FontSpec& font) override {return owner_.fonts_->font_metrics(font);}
    private:
        struct State { bool enabled{}; SDL_Rect rect{}; bool antialias{}; };
        void render_geometry_at(const DrawingGeometry& geometry, Color color,
                                const Point offset) {
            set(color);
            std::vector<SDL_FPoint> line;
            for (const auto& path:geometry.lines) {
                line.clear(); line.reserve(path.size());
                for (const auto point:path) line.push_back({
                    point.x+origin_.x+offset.x, point.y+origin_.y+offset.y});
                if (line.size()==1) SDL_RenderPoint(renderer_,line.front().x,line.front().y);
                else if (!line.empty()) SDL_RenderLines(renderer_,line.data(),static_cast<int>(line.size()));
            }
            if (geometry.triangles.empty()) return;
            const SDL_FColor rgba{color.red/255.0F,color.green/255.0F,color.blue/255.0F,color.alpha/255.0F};
            std::vector<SDL_Vertex> vertices;
            vertices.reserve(geometry.triangles.size()*3);
            for (const auto& triangle:geometry.triangles)
                for (const auto p:triangle) vertices.push_back({{
                    p.x+origin_.x+offset.x, p.y+origin_.y+offset.y},rgba,{0,0}});
            SDL_RenderGeometry(renderer_,nullptr,vertices.data(),static_cast<int>(vertices.size()),nullptr,0);
        }
        void render_geometry(const DrawingGeometry& geometry, Color color,
                             const bool outline = false) {
            if (antialias_ && outline && color.alpha != 0U) {
                auto edge = color;
                edge.alpha = static_cast<std::uint8_t>(
                    std::max(1, static_cast<int>(color.alpha) / 5));
                for (const Point offset : std::array<Point, 4>{{
                         {-0.45F, 0.0F}, {0.45F, 0.0F},
                         {0.0F, -0.45F}, {0.0F, 0.45F}}})
                    render_geometry_at(geometry, edge, offset);
            }
            render_geometry_at(geometry, color, {});
        }
        void set(Color color) { SDL_SetRenderDrawColor(renderer_,color.red,color.green,color.blue,color.alpha); }
        SdlGuiBackend& owner_;
        SDL_Renderer* renderer_{};
        Point origin_{};
        Rect bounds_{};
        bool inherited_clip_enabled_{};
        SDL_Rect inherited_clip_{};
        std::vector<State> states_;
        bool antialias_{};
    };

    // SDLの状態は描画先ごとに属する。ホストの表示設定を物理的な
    // 出力解像度の作成中テクスチャーへ反映し、 完成画素を1対1
    // でコピーする。文字・アイコンの二段目の拡大縮小を加えない。
    struct SurfaceRenderState {
        SDL_Renderer* renderer;
        SDL_Texture* target;
        int logical_width{}, logical_height{};
        SDL_RendererLogicalPresentation logical_mode{};
        float scale_x{1}, scale_y{1};
        bool viewport_set{}, clipped{};
        SDL_Rect viewport{}, clip{};
        SDL_Color color{};
        SDL_BlendMode blend{};
        explicit SurfaceRenderState(SDL_Renderer* r) : renderer(r), target(SDL_GetRenderTarget(r)) {
            SDL_GetRenderLogicalPresentation(r,&logical_width,&logical_height,&logical_mode);
            SDL_GetRenderScale(r,&scale_x,&scale_y);
            viewport_set=SDL_RenderViewportSet(r); SDL_GetRenderViewport(r,&viewport);
            clipped=SDL_RenderClipEnabled(r); SDL_GetRenderClipRect(r,&clip);
            SDL_GetRenderDrawColor(r,&color.r,&color.g,&color.b,&color.a);
            SDL_GetRenderDrawBlendMode(r,&blend);
        }
        void apply_view() const {
            SDL_SetRenderLogicalPresentation(renderer,logical_width,logical_height,logical_mode);
            SDL_SetRenderScale(renderer,scale_x,scale_y);
            SDL_SetRenderViewport(renderer,viewport_set ? &viewport : nullptr);
            SDL_SetRenderClipRect(renderer,clipped ? &clip : nullptr);
        }
        void restore() const {
            SDL_SetRenderTarget(renderer,target); apply_view();
            SDL_SetRenderDrawColor(renderer,color.r,color.g,color.b,color.a);
            SDL_SetRenderDrawBlendMode(renderer,blend);
        }
        ~SurfaceRenderState() { restore(); }
    };

    std::vector<std::shared_ptr<Window>> surface_windows(SDL_Renderer* renderer) {
        auto result=window_stack_.windows();
        std::erase_if(result,[&](const auto& model) {
            const auto* native=native_for(model);
            return !native || !model->visible() || native->renderer!=renderer;
        });
        return result;
    }

    void render_surface_frame(SDL_Renderer* renderer, SurfacePresent present) {
        UiTextMeasureScope text_metrics{[this](std::string_view text, const FontSpec& font) {
            return fonts_->measure_text(text, font, 0);
        }, [this](const FontSpec& font) { return fonts_->ink_center_offset(font); }};
        // 属性の実体化がアプリの配置処理へ再入し、ウィンドウを閉じる場
        // 合がある。 開始前にレンダラーとネイティブの資源を保持する。
        ++frame_users_[renderer];
        struct FrameGuard {
            SdlGuiBackend& self; SDL_Renderer* renderer;
            ~FrameGuard() { self.finish_frame(renderer); }
        } frame_guard{*this,renderer};
        if(frame_users_[renderer]==1)
            for(const auto& model:surface_windows(renderer))
                if(const auto* native=native_for(model)) synchronize_window_attributes(token_for(*native));
        std::vector<FrameUpdateTarget> targets;
        const auto now=SDL_GetTicks();
        for (const auto& model : surface_windows(renderer))
            if (auto* native=native_for(model)) {
                // 組み込みの ALL は単独版の再描画ループを回さないが、
                // 両経路で同じ共通フレームのアニメーション時刻を守る。
                if (native->animation.due(now) && !native->updates->pending()) native->updates->request();
                targets.push_back({model,native->updates});
            }
        SurfaceUpdateTransaction batch(surface_updates_,reinterpret_cast<std::uintptr_t>(renderer),std::move(targets));
        if (!batch.started()) {
            // ALL のホストが毎回自身の画面を消去しても、モデ
            // ルの一括処理は完成画素を保持する。 再帰的な描画
            // で、現在書き込み中のテクスチャーを読み取らない。
            if (batch.can_replay_completed() && !batch.windows().empty()) {
                if (frame_profile_.enabled) ++frame_profile_.replayed;
                if (auto* previous=native_for(batch.windows().front())) {
                    SurfaceRenderState host(renderer);
                    copy_completed_frame(*previous,present);
                }
            }
            return;
        }
        const auto frame_time=SDL_GetTicks();
        const auto tooltip_revision=hover_state_.tooltip().revision();
        auto prepared_tooltip=hover_state_.tooltip();
        if (batch.windows().empty()) return;
        auto* anchor=native_for(batch.windows().front());
        const auto token=token_for(*anchor);
        const bool owned=anchor->owns_handles;

        SurfaceRenderState host(renderer);
        int width{},height{};
        if (owned) {
            // 単独版の既存の論理クライアント領域のラスターサイズを維持する。 最後
            // のホストへの一回のコピーが、ネイティブ出力・DPIの拡大縮小を行う。
            SDL_GetWindowSize(anchor->window,&width,&height);
        } else if (host.target) {
            float w{},h{}; SDL_GetTextureSize(host.target,&w,&h);
            width=static_cast<int>(w); height=static_cast<int>(h);
        } else SDL_GetRenderOutputSize(renderer,&width,&height);
        bool valid=prepare_pending_frame(*anchor,width,height) &&
                   SDL_SetRenderTarget(renderer,anchor->pending_frame);
        if (valid) {
            if (owned) {
                SDL_SetRenderLogicalPresentation(renderer,0,0,SDL_LOGICAL_PRESENTATION_DISABLED);
                SDL_SetRenderScale(renderer,1,1); SDL_SetRenderViewport(renderer,nullptr);
                SDL_SetRenderClipRect(renderer,nullptr);
            } else host.apply_view();
            SDL_SetRenderDrawColor(renderer,0,0,0,0); SDL_RenderClear(renderer);
            for (const auto& model : batch.windows()) {
                auto* native=native_for(model);
                const auto snapshot=native ? paint_window(*native,frame_time,prepared_tooltip) : std::nullopt;
                if (!snapshot || !batch.append(model,*snapshot)) { valid=false; break; }
            }
            valid=valid && hover_state_.tooltip().revision()==tooltip_revision && batch.complete(surface_windows(renderer));
        }
        anchor=native_for(token);
        host.restore(); // 描画先になっている間は、破棄待ちのテクスチャーを削除しない。
        if (!anchor || !anchor->model->visible()) return;
        if (valid) {
            (void)hover_state_.tooltip().publish(tooltip_revision,std::move(prepared_tooltip));
            std::swap(anchor->current_frame,anchor->pending_frame);
            std::swap(anchor->current_width,anchor->pending_width);
            std::swap(anchor->current_height,anchor->pending_height);
            ++anchor->frame_sequence;
            if (frame_profile_.enabled) {
                ++frame_profile_.completed;
                // 製品のサービス構成は、意図的に主ループより長く存続
                // する場合がある。 バックエンド破棄時だけに頼らず、
                // 動作中の計測結果を反映して確認できるようにする。
                if (frame_profile_.completed == 1U ||
                    frame_profile_.completed % 16U == 0U) {
                    if (editor_diagnostics_enabled()) editor_action_log(frame_profile_.describe());
                    else SDL_Log("%s", frame_profile_.describe().c_str());
                }
            }
        }
        else if (frame_profile_.enabled) ++frame_profile_.rejected;
        // 未完了の処理は以前のラスターを維持し、スコープ終了時に同じ
        // 対象群を再予約する。 描画途中のテクスチャーは公開しない。
        copy_completed_frame(*anchor,present);
    }

    void copy_completed_frame(Native& native, SurfacePresent present) {
        if (!native.current_frame || !native.renderer) return;
        auto* renderer=native.renderer;
        bool copied{};
        if (native.owns_handles) {
            // 完成した単独版のテクスチャーは、ネイティブのクライアント領域
            // 全体を覆う。 コピー前にバックバッファを固定の灰色で消すと
            // 、サイズ変更のコピーが失敗・遅延したときに灰色が点滅する。
            // 全テクスチャーのコピー成功まで以前の完成済みバックバッフ
            // ァを残し、 成功したその世代だけを Present する。
            copied=SDL_SetTextureBlendMode(native.current_frame,SDL_BLENDMODE_NONE) &&
                   SDL_RenderTexture(renderer,native.current_frame,nullptr,nullptr);
        } else {
            SDL_SetRenderLogicalPresentation(renderer,0,0,SDL_LOGICAL_PRESENTATION_DISABLED);
            SDL_SetRenderScale(renderer,1,1); SDL_SetRenderViewport(renderer,nullptr);
            SDL_SetRenderClipRect(renderer,nullptr);
            // 透明な画素へのオフスクリーン描画では、RGBはすで
            // にアルファ乗算済み。 縁のアルファを二度掛けない。
            const bool blend=SDL_SetTextureBlendMode(native.current_frame,
                                                     SDL_BLENDMODE_BLEND_PREMULTIPLIED);
            const SDL_FRect destination{0,0,float(native.current_width),float(native.current_height)};
            copied=blend && SDL_RenderTexture(renderer,native.current_frame,nullptr,&destination);
        }
        if (!copied) {
            // 消去しただけ・合成途中の任意のバックバッファを
            // 公開しない。 共通の完全な処理を再予約する。
            if (native.updates) native.updates->request();
            return;
        }
        if (native.owns_handles && present == SurfacePresent::Standalone) {
            const EditorPerformanceScope present_performance("frame.present", 50000);
            const auto started = frame_profile_.enabled
                ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
            SDL_RenderPresent(renderer);
            if (frame_profile_.enabled)
                frame_profile_.present.add(std::chrono::steady_clock::now() - started);
        }
        // 借用した描画面の表示更新は、このアダプターではなくホストが行う。
    }

    class SdlFrameRasterSink final : public FrameRasterSink {
    public:
        SdlFrameRasterSink(SdlGuiBackend& owner,Native& native)
            : owner_(owner),renderer_(native.renderer),native_(native) {}
        bool begin_layer(Rect bounds,std::uint8_t opacity) override {
            if (layer_state_) return false;
            auto state=std::make_unique<SurfaceRenderState>(renderer_);
            float fw{},fh{};
            if (!state->target || !SDL_GetTextureSize(state->target,&fw,&fh)) return false;
            const int width=static_cast<int>(fw),height=static_cast<int>(fh);
            if (width<=0 || height<=0) return false;
            if (native_.opacity_layer && (native_.opacity_width!=width || native_.opacity_height!=height)) {
                SDL_DestroyTexture(native_.opacity_layer); native_.opacity_layer=nullptr;
            }
            if (!native_.opacity_layer) {
                native_.opacity_layer=SDL_CreateTexture(renderer_,SDL_PIXELFORMAT_RGBA32,
                    SDL_TEXTUREACCESS_TARGET,width,height);
                if (!native_.opacity_layer) return false;
                native_.opacity_width=width; native_.opacity_height=height;
            }
            if (!SDL_SetRenderTarget(renderer_,native_.opacity_layer)) return false;
            state->apply_view();
            SDL_SetRenderDrawColor(renderer_,0,0,0,0);
            if (!SDL_RenderClear(renderer_)) return false;
            set_native_clip(renderer_,bounds);
            opacity_=opacity; layer_state_=std::move(state);
            return true;
        }
        bool end_layer(bool publish) noexcept override {
            if (!layer_state_) return false;
            auto state=std::move(layer_state_);
            state->restore();
            if(SDL_GetRenderTarget(renderer_)!=state->target) return false;
            if (!publish) return true;
            // 親と同じラスター寸法・表示設定で描いたレイヤーを画素座標
            // で転送し、RGBとアルファに不透明度を一度だけ掛ける。
            bool ok=SDL_SetRenderLogicalPresentation(renderer_,0,0,SDL_LOGICAL_PRESENTATION_DISABLED);
            ok=SDL_SetRenderScale(renderer_,1,1) && ok;
            ok=SDL_SetRenderViewport(renderer_,nullptr) && ok;
            ok=SDL_SetRenderClipRect(renderer_,nullptr) && ok;
            ok=SDL_SetTextureBlendMode(native_.opacity_layer,SDL_BLENDMODE_BLEND_PREMULTIPLIED) && ok;
            ok=SDL_SetTextureColorMod(native_.opacity_layer,opacity_,opacity_,opacity_) && ok;
            ok=SDL_SetTextureAlphaMod(native_.opacity_layer,opacity_) && ok;
            const SDL_FRect destination{0,0,float(native_.opacity_width),float(native_.opacity_height)};
            if (ok) ok=SDL_RenderTexture(renderer_,native_.opacity_layer,nullptr,&destination);
            SDL_SetTextureColorMod(native_.opacity_layer,255,255,255);
            SDL_SetTextureAlphaMod(native_.opacity_layer,255);
            return ok; // state が描画先、表示設定、クリップ、描画状態を復元する。
        }
        void clear(Color color) override {
            SDL_SetRenderDrawColor(renderer_,color.red,color.green,color.blue,color.alpha);
            SDL_RenderClear(renderer_);
        }
        void push_clip(Rect bounds) override {
            SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_BLEND);
            clips_.push_back(std::make_unique<NativeClipScope>(renderer_,bounds));
        }
        void pop_clip() override { clips_.pop_back(); }
        void decoration(const DecorationPresentation& commands) override {
            owner_.render_decoration(renderer_,commands);
        }
        void text(std::string_view value,Point origin,Color color,const FontSpec& font) override {
            owner_.text(renderer_,value,origin.x,origin.y,
                {color.red,color.green,color.blue,color.alpha},owner_.font_for(font),
                0.0F,font.antialias);
        }
        void image(const ImageData& data,std::size_t frame,std::optional<Rect> source,
                   Rect destination,Interpolation interpolation) override {
            owner_.render_image_data(renderer_,{},data,frame,destination,source,interpolation);
        }
        void owned_image(const std::shared_ptr<const ImageData>& data,std::size_t frame,
                         std::optional<Rect> source,Rect destination,
                         Interpolation interpolation) override {
            if (data) owner_.render_image_data(renderer_,data,*data,frame,destination,source,interpolation);
        }
        void canvas(const CanvasFramePresentation& view) override {
            SdlDrawingContext context(owner_,renderer_,view.bounds);
            view.drawing.replay_owned(context,[&](const std::shared_ptr<const ImageData>& data,
                                                  Rect source,Rect destination,Interpolation interpolation) {
                destination.x+=view.bounds.x; destination.y+=view.bounds.y;
                owned_image(data,0U,source,destination,interpolation);
            });
        }
    private:
        SdlGuiBackend& owner_;
        SDL_Renderer* renderer_;
        Native& native_;
        std::unique_ptr<SurfaceRenderState> layer_state_;
        std::uint8_t opacity_{255};
        std::vector<std::unique_ptr<NativeClipScope>> clips_;
    };

    std::optional<FrameStructureSnapshot> paint_window(Native& native, std::uint64_t frame_time,
        TooltipStateMachine& prepared_tooltip) {
        if (!native.renderer || !native.model || native.pending_destroy) return {};

        Rect content = native.embedded ? native.hosted_bounds : native.model->bounds();
        int frame_width = 0;
        int frame_height = 0;
        if (!native.embedded) {
            SDL_GetWindowSize(native.window, &frame_width, &frame_height);
            SDL_Rect safe{0, 0, frame_width, frame_height};
            // デスクトップのウィンドウは、クライアント矩形全体を描
            // く。 SDLの安全領域が、サイズ変更後のクライアント
            // ではなくモニターの作業領域を返す場合があるため、 そ
            // れを使って下部に未描画の帯を残さない。 安全領域の余
            // 白を考慮するのは、スマートフォン・タブレットだけ。
            if ((native_platform() == Platform::Android ||
                 native_platform() == Platform::IOS) &&
                !SDL_GetWindowSafeArea(native.window, &safe))
                safe = SDL_Rect{0, 0, frame_width, frame_height};
            content = {static_cast<float>(safe.x), static_cast<float>(safe.y),
                       static_cast<float>(safe.w), static_cast<float>(safe.h)};
        }
        const auto shell=prepare_window_chrome(*native.model,content,native.embedded,host_options_.keep_top_bar);
        content=shell.content;
        const auto token=token_for(native);
        const auto model=native.model;

        auto* const renderer=native.renderer;
        ++frame_users_[renderer];
        // 共通の描画面の一括処理が、対象群全体の更新世代を管理する。
        struct DrawingGuard {
            std::function<void()> reset;
            ~DrawingGuard() { reset(); }
        } drawing_guard{[this,renderer] { finish_frame(renderer); }};
        struct RenderStateGuard {
            SDL_Renderer* renderer;
            SDL_Texture* target;
            bool clipped;
            SDL_Rect clip{};
            ~RenderStateGuard() {
                SDL_SetRenderTarget(renderer,target);
                SDL_SetRenderClipRect(renderer,clipped ? &clip : nullptr);
            }
        } render_state{renderer,SDL_GetRenderTarget(renderer),SDL_RenderClipEnabled(renderer)};
        if (render_state.clipped) SDL_GetRenderClipRect(renderer,&render_state.clip);
        const auto prepare_started = frame_profile_.enabled
            ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        auto frame=prepare_window_frame(edit_input_context(),context_popup_,model,content,
            [this,token] { return native_for(token)!=nullptr; },frame_time,
            hover_state_.hovered(),hover_state_.position(),
            prepare_pointer_feedback(model,mouse_input_,touch_input_),&prepared_tooltip);
        if (!frame || !native_for(token)) return {};
        const auto drawing_started = frame_profile_.enabled
            ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        if (frame_profile_.enabled)
            frame_profile_.layout.add(drawing_started - prepare_started);
        frame->shell=shell;
        DrawingCaptureServices capture;
        capture.measure_text=[this](std::string_view value,const FontSpec& font,float wrap) {
            return measure_authored_text(value,font,wrap);
        };
        capture.retain_image=[this](ResourceId id)->std::shared_ptr<const ImageData> {
            return graphics_ ? graphics_->retain_image(id) : nullptr;
        };
        if (!prepare_frame_drawing(*frame,capture,[this,token] { return native_for(token)!=nullptr; })) {
            if (auto* still=native_for(token)) still->updates->request();
            return {};
        }
        if (frame_profile_.enabled) {
            const auto prepared=std::chrono::steady_clock::now();
            frame_profile_.drawing.add(prepared - drawing_started);
            frame_profile_.prepare.add(prepared - prepare_started);
        }
        const auto& presentation=*frame;
        native.animation=presentation.animation;
        window_stack_.set_bounds(model,native.embedded ? native.hosted_bounds : content);
        SdlFrameRasterSink sink(*this,native);
        const auto raster_started = frame_profile_.enabled
            ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        if (!replay_window_frame(presentation,sink)) return {};
        if (frame_profile_.enabled)
            frame_profile_.raster.add(std::chrono::steady_clock::now() - raster_started);
        if (const auto drag = drag_drop_ ? drag_drop_->visual() : std::nullopt;
            drag && widget_in_input_scope(model, drag->source)) {
            const SDL_FRect ghost{drag->bounds.x, drag->bounds.y,
                                  drag->bounds.width, drag->bounds.height};
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            if (drag->operation == DropOperation::Move)
                SDL_SetRenderDrawColor(renderer, 56, 120, 215, 88);
            else
                SDL_SetRenderDrawColor(renderer, 96, 96, 96, 72);
            SDL_RenderFillRect(renderer, &ghost);
            SDL_SetRenderDrawColor(renderer, 30, 75, 150, 180);
            SDL_RenderRect(renderer, &ghost);
        }
        if (!native_for(token) || !model->visible()) return {};
        publish_text_input(token,presentation.text_input,presentation.focus_revision);
        // レイアウトや描画のコールバックから要求された再描画は、保留状態を維持する。
        return presentation.structure;
    }

    void render_single_surface() {
        if (!single_surface_root_) return;
        const auto root = windows_.find(single_surface_root_);
        if (root == windows_.end() || !root->second.model ||
            !root->second.model->visible() || !root->second.renderer) {
            return;
        }
        render_surface_frame(root->second.renderer, SurfacePresent::Standalone);
    }

    void render_decoration(SDL_Renderer* renderer,const DecorationPresentation& view) {
        std::vector<SDL_FPoint> points;
        for(const auto& primitive:view) {
            const auto c=primitive.color;
            if(primitive.kind==DecorationKind::FillRect) {fill(renderer,primitive.bounds,{c.red,c.green,c.blue,c.alpha}); continue;}
            if(primitive.kind==DecorationKind::StrokeRect) {border(renderer,primitive.bounds,{c.red,c.green,c.blue,c.alpha}); continue;}
            points.clear(); points.reserve(primitive.points.size());
            for(const auto p:primitive.points) points.push_back({p.x,p.y});
            if(points.empty()) continue;
            SDL_SetRenderDrawColor(renderer,c.red,c.green,c.blue,c.alpha);
            if(primitive.kind==DecorationKind::Points) SDL_RenderPoints(renderer,points.data(),static_cast<int>(points.size()));
            else SDL_RenderLines(renderer,points.data(),static_cast<int>(points.size()));
        }
    }


    void close_combo_popup() { combo_popup_.close(); }


    void render_image(SDL_Renderer* renderer, ResourceId id, std::size_t frame_index,
                      Rect destination, std::optional<Rect> source = std::nullopt,
                      Interpolation interpolation = Interpolation::Linear) {
        auto image = graphics_ ? graphics_->retain_image(id) : nullptr;
        if (image) render_image_data(renderer,image,*image,frame_index,destination,source,interpolation);
    }
    void render_image_data(SDL_Renderer* renderer,std::shared_ptr<const ImageData> owner,
                           const ImageData& image,std::size_t frame_index,
                           Rect destination,std::optional<Rect> source,Interpolation interpolation) {
        if (image.frames.empty()) return;
        const auto selected_frame=std::min(frame_index, image.frames.size() - 1U);
        const auto& frame = image.frames[selected_frame];
        if (frame.pixel_format != PixelFormat::RGBA32 || frame.pixels.empty()) return;
        const auto pixel_count = static_cast<std::uint64_t>(frame.pixel_width) * frame.pixel_height;
        if (!pixel_count || pixel_count > frame.pixels.size() / 4U) return;
        const bool filtered = interpolation != Interpolation::Nearest;
        const ImageTextureKey cache_key{renderer,&image,selected_frame,filtered};
        SDL_Texture* texture{};
        if (owner) {
            if (const auto found=image_texture_cache_.find(cache_key);
                found!=image_texture_cache_.end()) texture=found->second.texture;
        }
        std::vector<std::byte> premultiplied;
        const void* pixels = frame.pixels.data();
        if (!texture && filtered) {
            // 元画像はマスク適用済み。ストレートRGBAを補間すると、ア
            // ルファゼロでも隠れた紫などのキー色が縁に混ざる。 WindowsDrawingContext
            // と同様、アルファ乗算済みRGBAを補間し、
            // 対応する合成モードで描く。 サイズ変更後にキ
            // ー色を判定したり、元画像を変更したりしない。
            premultiplied.assign(frame.pixels.begin(),
                                 frame.pixels.begin() + pixel_count * 4U);
            for (std::size_t offset = 0; offset < premultiplied.size(); offset += 4U) {
                const auto alpha = std::to_integer<unsigned>(premultiplied[offset + 3U]);
                for (std::size_t channel = 0; channel < 3U; ++channel)
                    premultiplied[offset + channel] = static_cast<std::byte>(
                        (std::to_integer<unsigned>(premultiplied[offset + channel]) * alpha + 127U) / 255U);
            }
            pixels = premultiplied.data();
        }
        bool transient=false;
        if (!texture) {
            texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                SDL_TEXTUREACCESS_STATIC, static_cast<int>(frame.pixel_width), static_cast<int>(frame.pixel_height));
            if (!texture) return;
            if (!SDL_UpdateTexture(texture, nullptr, pixels, static_cast<int>(frame.pixel_width * 4U)) ||
                !SDL_SetTextureBlendMode(texture, filtered ? SDL_BLENDMODE_BLEND_PREMULTIPLIED
                                                          : SDL_BLENDMODE_BLEND)) {
                SDL_DestroyTexture(texture);
                return;
            }
            SDL_SetTextureScaleMode(texture, interpolation == Interpolation::Nearest
                ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR);
            const auto bytes=static_cast<std::size_t>(pixel_count)*4U;
            constexpr std::size_t maximum_image_textures=1024U;
            constexpr std::size_t maximum_image_texture_bytes=128U*1024U*1024U;
            if (owner && bytes<=maximum_image_texture_bytes) {
                if (image_texture_cache_.size()>=maximum_image_textures ||
                    image_texture_cache_bytes_+bytes>maximum_image_texture_bytes)
                    clear_image_texture_cache();
                image_texture_cache_bytes_+=bytes;
                image_texture_cache_.emplace(cache_key,CachedImageTexture{std::move(owner),texture,bytes});
            } else transient=true;
        }
        SDL_FRect target = sdl_rect(destination);
        SDL_FRect source_rect{};
        const SDL_FRect* source_pointer = nullptr;
        if (source) {
            source_rect = sdl_rect(*source);
            source_pointer = &source_rect;
        }
        SDL_RenderTexture(renderer, texture, source_pointer, &target);
        if (transient) SDL_DestroyTexture(texture);
    }

    void clear_image_texture_cache(SDL_Renderer* renderer=nullptr) noexcept {
        for (auto it=image_texture_cache_.begin();it!=image_texture_cache_.end();) {
            if (renderer && it->first.renderer!=renderer) {++it;continue;}
            if (it->second.texture) SDL_DestroyTexture(it->second.texture);
            image_texture_cache_bytes_-=std::min(image_texture_cache_bytes_,it->second.bytes);
            it=image_texture_cache_.erase(it);
        }
    }

    WindowInputLifecycleContext window_lifecycle_context() {
        return {edit_focus_context(),hosted_caption_,chrome_input_,mouse_input_,
                touch_input_,context_popup_,hover_state_,drag_drop_,&modal_state_,
                [this](const auto& window) {request_redraw(window,std::nullopt);},
                [this](const auto& window) {
                    auto* native = native_for(window);
                    if (native && native->window && window->visible() &&
                        window->enabled() && !window->overlay_surface)
                        SDL_RaiseWindow(native->window);
                }};
    }

    ModalOperationContext modal_operation_context(const std::shared_ptr<Window>& owner) {
        ModalOperationContext context{modal_state_,focus_state_.focused()};
        const auto* native=native_for(owner);
        const auto token=native ? std::optional{token_for(*native)} : std::nullopt;
        context.live=[this,owner,token] {
            return (!dispatcher_ || !dispatcher_->stopped()) &&
                (!owner || (token && native_for(*token))) &&
                !window_close_.disposing(owner) && !retiring(owner);
        };
        context.suspend_input=[this,owner,live=context.live] {
            suspend_window_input(window_lifecycle_context(),owner,{},live);
            synchronize_native_focus();
        };
        context.iterate=[this] { return pump_modal_iteration(); };
        context.current_focus=[this] { return focus_state_.focused(); };
        context.focus_revision=[this] {return focus_state_.revision();};
        context.restore=[this](const auto&) {synchronize_native_focus();};
        context.set_focus=[this](const WidgetPtr& widget) { set_focus(widget); };
        context.surface_available=[this](const std::shared_ptr<Window>& window) {
            return window && windows_.contains(window.get()) &&
                !window_close_.disposing(window) && !retiring(window);
        };
        return context;
    }

    void request_window_close(std::shared_ptr<Window> model) {
        window_close_.request(model,[this](const auto& window) { close(window); });
    }

    EditFocusContext edit_focus_context() {
        return {focus_state_, inline_edit_, number_edit_, combo_edit_, combo_popup_};
    }

    InputPublicationContext input_publication_context(std::optional<NativeToken> source={}) {
        return {edit_input_context(),window_stack_,modal_state_,
            [this,source](const auto& window) {
                return source && window.get()==source->model ? native_for(*source)!=nullptr : native_for(window)!=nullptr;
            },
            [this] {synchronize_native_focus();},
            [this](const auto& window) {request_redraw(window,std::nullopt);},
            [this](const auto& widget) {publish_accessibility(widget);}};
    }
    void set_focus(const WidgetPtr& widget) {
        const auto previous = focus_state_.focused();
        if (previous && !std::dynamic_pointer_cast<NumberEdit>(previous)) {
            for (const auto& [unused, native] : windows_) {
                (void)unused;
                if (native.window && !native.numeric_text_input && widget_in_input_scope(native.model, previous)) {
                    if (const auto mode = native_ime_mode(native.window))
                        set_editor_ime_mode(*mode);
                    break;
                }
            }
        }
        (void)publish_edit_focus(input_publication_context(),widget);
    }

    void synchronize_native_focus() {
        const EditorPerformanceScope performance("input.focus-synchronization");
        publish_accessibility(focus_state_.focused());
        std::vector<TextInputSurface> surfaces;
        for(const auto& [_,native]:windows_)
            if(native.window) surfaces.push_back({native.model,native.id});
        const auto revision=focus_state_.revision();
        const auto plan=plan_text_input_surfaces(window_stack_,modal_state_,focus_state_.focused(),surfaces);
        for(const auto& surface:plan.stop) {
            auto* native=native_for(surface.scope);
            if(!native || native->id!=surface.surface) continue;
            const auto token=token_for(*native);
            const bool stopped=!SDL_TextInputActive(native->window) || SDL_StopTextInput(native->window);
            if(stopped) text_input_publication_.retire(surface.surface);
            else if(auto* retry=native_for(token)) retry->updates->request();
            if(focus_state_.revision()!=revision) return;
        }
        if(auto* current=native_for(plan.active_scope)) start_widget_text_input(*current);
    }

    void publish_accessibility(const WidgetPtr& widget) {
        const auto presentation=prepare_widget_accessibility(widget,focus_state_.focused());
        const auto& name=presentation.name;
        const auto& description=presentation.description;
        const auto role = presentation.kind
            ? std::to_string(static_cast<unsigned>(*presentation.kind)) : std::string{};
        const auto value = presentation.password ? std::string{} : presentation.value;
        std::string selection;
        for (const auto index : presentation.selected_indices) {
            if (!selection.empty()) selection.push_back(',');
            selection += std::to_string(index);
        }
        for (const auto id : presentation.selected_tree_items) {
            if (!selection.empty()) selection.push_back(',');
            selection += std::to_string(id);
        }
        const auto* active=native_for(window_stack_.owner_of(widget));
        const bool active_live=active && active->window && !active->pending_destroy;
        const auto active_surface=active_live ? active->window : nullptr;
        if (accessibility_) {
            // Apple・ネイティブの登録先は、論理的な子モデルではなく実際のSDL
            // 描画面をキーにする。 新しいフォーカス先を反映する前に、他の現在
            // の画面をすべて無効化する。 そうしないと、フォーカスを失った古い
            // ネイティブ要素が、ウィンドウを閉じるまで操作可能なままになる。
            std::vector<std::uint64_t> surfaces;
            surfaces.reserve(windows_.size());
            for (const auto& [_, native] : windows_)
                if (native.window) surfaces.push_back(native.id);
            retire_inactive_accessibility_surfaces(*accessibility_, surfaces,
                active_live ? std::optional<std::uint64_t>{active->id} : std::nullopt);
        }
        std::unordered_set<SDL_Window*> published;
        for (auto& [_, native] : windows_) {
            if (!native.window || !published.insert(native.window).second) continue;
            const bool owns_focus=native.window==active_surface;
            const SDL_PropertiesID properties = SDL_GetWindowProperties(native.window);
            if (properties != 0U) {
                if (!owns_focus || name.empty()) SDL_ClearProperty(
                    properties, "cardwirth.cwxeditor.accessibility.name");
                else SDL_SetStringProperty(
                    properties, "cardwirth.cwxeditor.accessibility.name", name.c_str());
                if (!owns_focus || description.empty()) SDL_ClearProperty(
                    properties, "cardwirth.cwxeditor.accessibility.description");
                else SDL_SetStringProperty(
                    properties, "cardwirth.cwxeditor.accessibility.description",
                    description.c_str());
                const auto publish_string = [&](const char* key, const std::string& text) {
                    if (!owns_focus || text.empty()) SDL_ClearProperty(properties, key);
                    else SDL_SetStringProperty(properties, key, text.c_str());
                };
                publish_string("cardwirth.cwxeditor.accessibility.role", role);
                publish_string("cardwirth.cwxeditor.accessibility.value", value);
                publish_string("cardwirth.cwxeditor.accessibility.selection", selection);
                if (!owns_focus) {
                    SDL_ClearProperty(properties, "cardwirth.cwxeditor.accessibility.enabled");
                    SDL_ClearProperty(properties, "cardwirth.cwxeditor.accessibility.read_only");
                    SDL_ClearProperty(properties, "cardwirth.cwxeditor.accessibility.checked");
                } else {
                    SDL_SetBooleanProperty(properties,
                        "cardwirth.cwxeditor.accessibility.enabled", presentation.enabled);
                    SDL_SetBooleanProperty(properties,
                        "cardwirth.cwxeditor.accessibility.read_only", presentation.read_only);
                    if (presentation.checked)
                        SDL_SetBooleanProperty(properties,
                            "cardwirth.cwxeditor.accessibility.checked", *presentation.checked);
                    else SDL_ClearProperty(properties,
                        "cardwirth.cwxeditor.accessibility.checked");
                }
            }
        }
        if (!accessibility_announcements_.update(widget,presentation)) return;
#if defined(__ANDROID__)
        auto* environment = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
        auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
        if (!environment || !activity) return;
        jclass type = environment->GetObjectClass(activity);
        jmethodID method = type ? environment->GetMethodID(
            type, "cardwirthAnnounceAccessibility",
            "(Ljava/lang/String;Ljava/lang/String;)V") : nullptr;
        if (method && !environment->ExceptionCheck()) {
            jstring native_name = environment->NewStringUTF(name.c_str());
            jstring native_description = environment->NewStringUTF(description.c_str());
            environment->CallVoidMethod(activity, method, native_name, native_description);
            if (native_name) environment->DeleteLocalRef(native_name);
            if (native_description) environment->DeleteLocalRef(native_description);
        }
        if (environment->ExceptionCheck()) environment->ExceptionClear();
        if (type) environment->DeleteLocalRef(type);
        environment->DeleteLocalRef(activity);
#endif
        if (accessibility_ && active_live) {
            accessibility_->publish(active->id, active_surface, window_stack_.owner_of(widget), widget, presentation);
        }
    }



    EditInputContext edit_input_context(std::function<bool()> live = {}) {
        const auto interaction=editor_interaction_metrics();
        return {edit_focus_context(), text_edit_states_,
            [this](const WidgetPtr& widget) {
                auto* font = font_for(widget);
                return EditInputMetrics{
                    text_edit_line_height(font ? std::optional<float>{static_cast<float>(TTF_GetFontHeight(font))}
                                               : std::nullopt, widget && widget->font() ? &*widget->font() : nullptr),
                    [font](std::string_view value) { return measured_width(font, value); }};
            }, text_clipboard(), interaction.row_height, interaction.combo_visible_rows,
            application_key_filter_, &context_popup_, std::move(live), &mouse_input_,
            drag_drop_, &hover_state_, color_picker_, color_picker_async_};
    }

    static TextClipboardAccess text_clipboard() {
        TextClipboardAccess clipboard;
        clipboard.read = []() -> std::optional<std::string> {
            char* value = SDL_GetClipboardText();
            if (!value) return std::nullopt;
            std::string result(value);
            SDL_free(value);
            return result;
        };
        clipboard.write = [](std::string_view value) {
            const bool written = SDL_SetClipboardText(std::string(value).c_str());
#if defined(__EMSCRIPTEN__)
            if (written) publish_web_clipboard_text(value);
#endif
            return written;
        };
        return clipboard;
    }






    void start_widget_text_input(Native& native) {
        const auto token = token_for(native);
        const auto scope = native.model;
        // plan_text_input_surfaces が、表示・有効
        // ・モーダル条件を満たす唯一の論理範囲として選択済み。 アダプター
        // はネイティブIMEへの反映だけを行う。 ここで同じ方針を再実装す
        // ると、モーダル・フォーカス遷移中に共通の判断とずれる場合がある。
        const auto revision=focus_state_.revision();
        const auto input = prepare_text_input(edit_input_context(), scope);
        publish_text_input(token,input,revision);
    }

    void publish_text_input(NativeToken token,
                            const std::optional<TextInputPresentation>& input,
                            std::uint64_t revision) {
        auto* current=native_for(token);
        if(!current) return;
        const auto host_map=current->embedded?host_coordinate_map_:CoordinatePointMap{};
        NativeTextInputObservation observation;observation.active=SDL_TextInputActive(current->window);
        SDL_Rect previous{};
        if(SDL_GetTextInputArea(current->window,&previous,nullptr))
            observation.area=TextInputArea{previous.x,previous.y,previous.w,previous.h};
        const auto plan=text_input_publication_.prepare(
            {edit_input_context(),window_stack_,modal_state_,current->model,current->id,revision,
             [this,token] {return native_for(token)!=nullptr;},
             [this,token,host_map](Point p)->std::optional<Point> {
                 const auto host=host_map?host_map(p):std::optional<Point>{p};
                 const auto* live=native_for(token);if(!host || !live) return {};
                 Point mapped;
                 if(!SDL_RenderCoordinatesToWindow(live->renderer,host->x,host->y,&mapped.x,&mapped.y)) return {};
                 return mapped;
             }},input,observation);
        current=native_for(token);
        if(!current || plan.kind==TextInputPublicationKind::Ignore) return;
        bool success=true;
        if(plan.kind==TextInputPublicationKind::Stop) {
            if(SDL_TextInputActive(current->window)) success=SDL_StopTextInput(current->window);
        } else {
            if(plan.clear_composition) success=SDL_ClearComposition(current->window);
            if(success && plan.start) success=SDL_StartTextInput(current->window);
            if(success && plan.update_area) {
                SDL_Rect area{plan.area.x,plan.area.y,plan.area.width,plan.area.height};
                success=SDL_SetTextInputArea(current->window,&area,0);
            }
        }
        const bool committed=text_input_publication_.commit(plan,success);
        // IMMの変換モードは、現在の入力セッションに属する。
        // 保存値を毎フレーム適用すると、ユーザーの日本語・英
        // 字の切り替えを戻し、IMEの変換を中断してしまう。
        current=native_for(token);
        if (committed && current && input && plan.restore_ime_mode) {
            const auto focused = focus_state_.focused();
            current->numeric_text_input = static_cast<bool>(std::dynamic_pointer_cast<NumberEdit>(focused)) ||
                (input->editor && input->editor->numeric_input);
            apply_native_ime_mode(current->window, current->numeric_text_input ? 0 : editor_ime_mode());
        }
        if(!success) if(auto* retry=native_for(token)) retry->updates->request();
    }


    PassiveInputContext passive_input_context(const NativeToken& token) {
        const auto interaction=editor_interaction_metrics();
        return {hover_state_, context_popup_, combo_popup_, inline_edit_, wheel_scroll_,
            interaction.row_height, interaction.tab_height, interaction.expand_header_height,
            interaction.resize_margin,
            [this,token](const auto& edit) -> ScrollTextAccess {
                const auto* native=native_for(token);
                if (!native) return {nullptr,{},0,[]{return false;}};
                return prepare_edit_scroll_access(edit_input_context(
                    [this,token] {return native_for(token)!=nullptr;}),edit,native->model);
            },
            [this,token] { return native_for(token)!=nullptr; }};
    }



    ControlActivationContext activation_context() {
        const auto interaction=editor_interaction_metrics();
        return {inline_edit_,combo_popup_,combo_edit_,number_edit_,context_popup_,
            interaction.row_height,interaction.combo_visible_rows,
            [this](const WidgetPtr& widget) {
                return activate_control_focus(input_publication_context(),widget);
            },
            [this](const std::shared_ptr<TextEdit>& edit,Point point,Modifiers modifiers) {
                select_edit_text_at(edit_input_context(), edit, point, modifiers);
            },
            [](const WidgetPtr& widget) { arrange_widget(widget,widget->bounds()); },
            color_picker_, color_picker_async_};
    }


    std::optional<PointerEvent> translate_pointer(Native& native, const SDL_Event& event) {
        PointerEvent pointer;
        pointer.pointer_type = PointerType::Mouse;
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            pointer.action = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? PointerAction::Down : PointerAction::Up;
            pointer.button = sdl_pointer_button(event.button.button);
            pointer.pointer_id = event.button.which;
            pointer.position = {event.button.x, event.button.y};
        } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
            pointer.action = PointerAction::Move;
            pointer.pointer_id = event.motion.which;
            pointer.position = {event.motion.x, event.motion.y};
        } else if (event.type == SDL_EVENT_FINGER_DOWN || event.type == SDL_EVENT_FINGER_UP ||
                   event.type == SDL_EVENT_FINGER_MOTION || event.type == SDL_EVENT_FINGER_CANCELED) {
            int width{}, height{}; SDL_GetWindowSize(native.window, &width, &height);
            pointer.pointer_type = PointerType::Touch;
            pointer.pointer_id = static_cast<std::uint64_t>(event.tfinger.fingerID);
            pointer.button = PointerButton::Primary;
            pointer.action = event.type == SDL_EVENT_FINGER_DOWN ? PointerAction::Down :
                event.type == SDL_EVENT_FINGER_UP ? PointerAction::Up :
                event.type == SDL_EVENT_FINGER_CANCELED ? PointerAction::Cancel : PointerAction::Move;
            pointer.position = {event.tfinger.x * width, event.tfinger.y * height};
            pointer.pressure = event.tfinger.pressure;
        } else return std::nullopt;
        if (native.embedded) {
            // 組み込みウィンドウは一つのエディタの描画面を共有し、その
            // 座標はすでにポップアップ・重ね表示の座標系となっている。
            pointer.screen_position = pointer.position;
            pointer.screen_position_available = true;
        } else {
            // SDLのウィンドウ位置は、エディタが使うクラ
            // イアント原点。 後の待機中イベントの位置にな
            // っている可能性がある現在のマウス位置ではなく
            // 、 そのイベント自身のローカル座標を加える。
            int window_x{}, window_y{};
            SDL_GetWindowPosition(native.window, &window_x, &window_y);
            pointer.screen_position = {
                pointer.position.x + static_cast<float>(window_x),
                pointer.position.y + static_cast<float>(window_y)};
            pointer.screen_position_available = true;
        }
        const auto modifiers = native_modifier_snapshot(SDL_GetModState());
        pointer.modifiers = modifiers.modifiers;
        pointer.physical_control = modifiers.physical_control;
        if (event.common.timestamp) pointer.timestamp_ns=event.common.timestamp;
        return pointer;
    }

    bool handle_pointer_input(Native& native, const SDL_Event& event) {
        const auto input=translate_pointer(native,event);
        if(!input) return false;
        const bool down=event.type==SDL_EVENT_MOUSE_MOTION
            ? (event.motion.state & SDL_BUTTON_LMASK)!=0
            : event.type==SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button==SDL_BUTTON_LEFT;
        return handle_pointer_input(native, *input, down, 0);
    }

    PointerInputContext pointer_input_context(Native& native) {
        const auto token=token_for(native);
        const auto interaction=editor_interaction_metrics();
        PointerInputContext context{mouse_input_,touch_input_,chrome_input_,hosted_caption_,
            activation_context(),edit_input_context(),passive_input_context(token),drag_drop_,
            interaction.drag_threshold,interaction.touch_scroll_threshold,
            [this,token] {return native_for(token)!=nullptr;},{}};
        // 動かさない長押しは右クリックと同じ操作を
        // 開き、二つ目のドラッグ操作を予約しない。
        context.long_press_is_context_click = true;
        context.long_press_delay = std::chrono::milliseconds{650};
        if (system_) context.click_timing.interval = system_->double_click_time();
        if(native.embedded && host_options_.keep_top_bar && native.model &&
           native.model->host_mode==WindowHostMode::EmbeddedDialog) {
            // ネイティブ入力と共通の時間判定で、同じ配置済み座標を使う。
            const auto host=surface_available_bounds(native);
            context.surface=PointerCaptionSurface{&native.hosted_bounds,host,
                host_options_.keep_top_bar,host_options_.allow_internal_move};
        }
        context.click_timing.distance=interaction.drag_threshold;
        context.click_timing.touch_distance=scaled_ui_metric(40.0F);
        return context;
    }

    PointerPublicationContext pointer_publication_context(NativeToken token) {
        return {input_publication_context(token),
            [this,token](const auto& model,Rect bounds) {
                if(auto* current=native_for(token); current && current->embedded) {
                    current->hosted_bounds=bounds;
                    current->authored_bounds=authored_window_bounds(*model);
                    current->attributes->invalidate_constraints();
                }
            },
            [this](CursorKind cursor) {update_cursor(cursor);},
            [this](const auto& model) {request_window_close(model);},
            [this](const auto& model) {minimize_window(model);},
            [this](const auto& model, const bool redo) {send_history_key(model, redo);}};
    }

    // 見出しの取り消し・やり直しボタンはキーボード経路を再利用する
    // 。 各ダイアログのCtrl+Z/Ctrl+Yの処理を、フォ
    // ーカス中の編集欄・一覧・履歴管理も含めてそのまま適用する。
    void send_history_key(const std::shared_ptr<Window>& model, const bool redo) {
        if (!model) return;
        const auto found = windows_.find(model.get());
        if (found == windows_.end()) return;
        const auto token = token_for(found->second);
        const KeyEvent key{redo ? Key::Y : Key::Z, Modifiers::Control, false, false,
                           redo ? "y" : "z"};
        (void)dispatch_and_publish_edit_key(input_publication_context(token), model, key, true);
        (void)dispatch_and_publish_edit_key(input_publication_context(token), model, key, false);
    }

    void advance_pointer_timers() {
        if(!pointer_timers_pending(mouse_input_,touch_input_)) return;
        // ネイティブ側は起床通知だけを行う。先に待機中の解放イベントを処理し、
        // フレームの遅れで、終了済みのタップが長押しにならないようにする。
        std::vector<PointerTimerTarget> targets;
        for (auto& [_, native] : windows_) {
            if(native.pending_destroy) continue;
            const auto token=token_for(native);
            targets.push_back({native.model,pointer_input_context(native),pointer_publication_context(token)});
        }
        advance_and_publish_pointers(targets);
    }

    bool handle_pointer_input(Native& native, PointerEvent input, bool down, unsigned clicks) {
        const auto token=token_for(native);
        const auto model=native.model;
        auto context=pointer_input_context(native);
        const auto result=dispatch_and_publish_pointer(pointer_publication_context(token),
            std::move(context),model,input,down,clicks);
        return result.handled;
    }

    bool dispatchNativeEventOnce(SDL_Event& event) {
        const bool modal_active_at_entry = !modal_state_.sessions().empty();
        ++native_event_dispatch_depth_;
        bool handled{};
        try {
            handled = handle(event);
        } catch (...) {
            --native_event_dispatch_depth_;
            throw;
        }
        --native_event_dispatch_depth_;
        if (handled && native_event_dispatch_depth_ == 0U &&
            !modal_active_at_entry && modal_state_.sessions().empty())
            successful_native_event_dispatch_completed();
        return handled;
    }

    bool handle(SDL_Event& event) {
        const EditorPerformanceScope performance("input.native-event");
        // SDLのタイムスタンプと SDL_GetTicksNS は同じ単調
        // 時計を使う。 通知の遅れはハンドラー自体の実行時間とは別に記録し、
        // 大量発生時は間引いて、診断が入力の滞留を悪化させないようにする。
        if (editor_diagnostics_enabled() && event.common.timestamp != 0) {
            const auto now = SDL_GetTicksNS();
            static thread_local Uint64 last_delayed_event_log{};
            if (now >= event.common.timestamp &&
                now - event.common.timestamp >= 100000000ULL &&
                (last_delayed_event_log == 0 || now - last_delayed_event_log >= 1000000000ULL)) {
                last_delayed_event_log = now;
                editor_action_log("PERF operation=input.delivery-delay type=" +
                    std::to_string(event.type) + " elapsed_us=" +
                    std::to_string((now - event.common.timestamp) / 1000ULL));
            }
        }
        if (event.type == SDL_EVENT_RENDER_DEVICE_RESET) {
            invalidate_renderer_resources();
            return true;
        }
        UiTextMeasureScope text_metrics{[this](std::string_view text, const FontSpec& font) {
            return fonts_->measure_text(text, font, 0);
        }, [this](const FontSpec& font) { return fonts_->ink_center_offset(font); }};
        if (event.type == SDL_EVENT_USER) {
            wake_posted_.store(false, std::memory_order_release);
            return true;
        }
        if (lifecycle_handler_) {
            using Visibility = ::cardwirth::platform::AppVisibility;
            const bool background = event.type == SDL_EVENT_WILL_ENTER_BACKGROUND ||
                event.type == SDL_EVENT_DID_ENTER_BACKGROUND || event.type == SDL_EVENT_TERMINATING;
            const bool foreground = event.type == SDL_EVENT_DID_ENTER_FOREGROUND;
            const bool window_event = event.type == SDL_EVENT_WINDOW_MINIMIZED ||
                event.type == SDL_EVENT_WINDOW_RESTORED || event.type == SDL_EVENT_WINDOW_MAXIMIZED ||
                event.type == SDL_EVENT_WINDOW_HIDDEN ||
                event.type == SDL_EVENT_WINDOW_SHOWN;
            const bool root_event = window_event && std::ranges::any_of(windows_, [&](const auto& item) {
                const auto& native = item.second;
                return native.window == SDL_GetWindowFromEvent(&event) &&
                    native.model->owner.expired() && !native.model->overlay_surface;
            });
            if (background || foreground || root_event) {
                const auto reason = background || foreground ? Visibility::Background :
                    event.type == SDL_EVENT_WINDOW_MINIMIZED || event.type == SDL_EVENT_WINDOW_RESTORED ||
                        event.type == SDL_EVENT_WINDOW_MAXIMIZED
                        ? Visibility::Minimized : Visibility::Hidden;
                const bool suspended = background || event.type == SDL_EVENT_WINDOW_MINIMIZED ||
                    event.type == SDL_EVENT_WINDOW_HIDDEN;
                if (audio_visibility_.set(reason, suspended))
                    lifecycle_handler_(audio_visibility_.suspended() ? LifecycleEvent::EnterBackground :
                        LifecycleEvent::EnterForeground);
            } else if (event.type == SDL_EVENT_LOW_MEMORY)
                lifecycle_handler_(LifecycleEvent::MemoryPressure);
        }
        if (event.type == SDL_EVENT_QUIT) {
            window_close_.request_all(window_stack_,[this](const auto& window) {close(window);});
            return !windows_.empty();
        }
        if (auto drop = sdl_external_drop(event)) {
            std::unordered_map<const Window*, NativeToken> tokens;
            for (const auto& [model, native] : windows_)
                if (native.id == drop->surface && !native.pending_destroy)
                    tokens.emplace(model, token_for(native));
            if (tokens.empty()) { external_drop_.retire(drop->surface); return true; }
            // コールバック前にネイティブの世代を取得する。 論理的
            // な対象の選択とドロップの全判断は共通層が管理する。
            (void)external_drop_.receive(std::move(*drop), {
                *drag_drop_, window_stack_, modal_state_,
                [this, tokens = std::move(tokens)](const auto& scope) {
                    const auto found = tokens.find(scope.get());
                    return found != tokens.end() && native_for(found->second) != nullptr;
                },
                [this](const auto& scope) { request_redraw(scope, std::nullopt); }});
            return true;
        }
        SDL_WindowID id = 0;
        if (event.type >= SDL_EVENT_PEN_PROXIMITY_IN && event.type <= SDL_EVENT_PEN_AXIS) {
            if (auto pen = pen_input_.decode(event)) {
                const auto modifiers = native_modifier_snapshot(SDL_GetModState());
                pen->pointer.modifiers = modifiers.modifiers;
                pen->pointer.physical_control = modifiers.physical_control;
                // ウィンドウの前面化・フォーカス移動より前に、型とIDで入力を分離する。
                if (!mouse_input_.accepts_pointer(pen->pointer)) return true;
                const auto route = route_window_input(window_lifecycle_context(), window_stack_, modal_state_,
                    {pen->surface, WindowInputKind::Mouse, pen->pointer.position,
                     pen->pointer.pointer_id, pen->pointer.action == PointerAction::Down});
                if (auto* target = native_for(route.window)) {
                    if (route.raised) target->updates->request();
                    (void)handle_pointer_input(*target, pen->pointer, pen->primary_down, 0);
                }
            }
            return true;
        }
        if (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST)
            id = event.window.windowID;
        else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                 event.type == SDL_EVENT_MOUSE_BUTTON_UP)
            id = event.button.windowID;
        else if (event.type == SDL_EVENT_MOUSE_MOTION)
            id = event.motion.windowID;
        else if (event.type == SDL_EVENT_MOUSE_WHEEL)
            id = event.wheel.windowID;
        else if (event.type == SDL_EVENT_FINGER_DOWN || event.type == SDL_EVENT_FINGER_UP ||
                 event.type == SDL_EVENT_FINGER_MOTION || event.type == SDL_EVENT_FINGER_CANCELED)
            id = event.tfinger.windowID;
        else if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP)
            id = event.key.windowID;
        else if (event.type == SDL_EVENT_TEXT_INPUT)
            id = event.text.windowID;
        else if (event.type == SDL_EVENT_TEXT_EDITING)
            id = event.edit.windowID;

        if(event.type==SDL_EVENT_WINDOW_MAXIMIZED || event.type==SDL_EVENT_WINDOW_RESTORED) {
            // 実際の ALL ホストの通知は、前面の子ダイアログの状態で
            // はない。 そのネイティブ画面を所有する世代だけが受け取る。
            for(const auto& model:window_stack_.windows()) {
                auto* target=native_for(model);
                if(!target || target->id!=id || target->embedded || !target->owns_handles) continue;
                const bool maximized=(SDL_GetWindowFlags(target->window)&SDL_WINDOW_MAXIMIZED)!=0;
                target->maximize->observe(model,maximized,event.window.timestamp);
                int x{},y{},width{},height{};
                if(SDL_GetWindowPosition(target->window,&x,&y) &&
                   SDL_GetWindowSize(target->window,&width,&height))
                    target->maximize->observe_geometry(model,
                        {static_cast<float>(x),static_cast<float>(y),
                         static_cast<float>(width),static_cast<float>(height)},maximized);
                target->attributes->invalidate(WindowAttribute::Maximized);
                target->updates->request();
            }
            return true;
        }

        // 論理ウィンドウを選択・前面化する前に、SDLがタッチ・ペンから生成した
        // マウスイベントを除く。 一つの物理操作には、一つの入力経路だけを使う。
        if ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) &&
            (event.button.which == SDL_TOUCH_MOUSEID || event.button.which == SDL_PEN_MOUSEID)) return true;
        if (event.type == SDL_EVENT_MOUSE_MOTION &&
            (event.motion.which == SDL_TOUCH_MOUSEID || event.motion.which == SDL_PEN_MOUSEID)) return true;
        if (event.type == SDL_EVENT_MOUSE_WHEEL &&
            (event.wheel.which == SDL_TOUCH_MOUSEID || event.wheel.which == SDL_PEN_MOUSEID)) return true;
        std::optional<WindowNotificationKind> notice;
        switch (event.type) {
        case SDL_EVENT_WINDOW_FOCUS_GAINED: notice=WindowNotificationKind::Activated; break;
        case SDL_EVENT_WINDOW_FOCUS_LOST: notice=WindowNotificationKind::Deactivated; break;
        case SDL_EVENT_WINDOW_MOUSE_LEAVE: notice=WindowNotificationKind::PointerLeft; break;
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED: notice=WindowNotificationKind::CloseRequested; break;
        case SDL_EVENT_WINDOW_EXPOSED: notice=WindowNotificationKind::Exposed; break;
        case SDL_EVENT_WINDOW_MOVED:
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED: notice=WindowNotificationKind::GeometryChanged; break;
        default: break;
        }
        if (notice) {
            if (*notice==WindowNotificationKind::GeometryChanged) {
                for(auto& [_,target]:windows_)
                    if(target.id==id && !target.pending_destroy && target.model->overlay_surface)
                        synchronize_input_mask(target);
                // Window::bounds は MainWindow
                // が終了時に保存する値。 SDLのサイズ変更による再
                // 描画だけでなく、OSの移動・サイズも反映し、 通常終
                // 了で古い起動位置を保存し直さないようにする。 デスク
                // トップの形状を表すのはネイティブ画面の所有モデルだ
                // けで、組み込み・重ね表示のモデルは論理境界を使う。
                for (auto& [model,target] : windows_) {
                    if (target.id!=id || target.pending_destroy || target.embedded ||
                        !target.owns_handles || !target.window) continue;
                    int x{},y{},width{},height{};
                    if (!SDL_GetWindowPosition(target.window,&x,&y) ||
                        !SDL_GetWindowSize(target.window,&width,&height)) continue;
                    const Rect observed{static_cast<float>(x),static_cast<float>(y),
                                        static_cast<float>(width),static_cast<float>(height)};
                    model->set_bounds(observed);
                    target.maximize->observe_geometry(target.model,observed,
                        (SDL_GetWindowFlags(target.window)&SDL_WINDOW_MAXIMIZED)!=0);
                    target.authored_bounds=authored_window_bounds(*model);
                    window_stack_.set_bounds(target.model,
                                             {0,0,observed.width,observed.height});
                }
            }
            WindowNotification notification{*notice,id};
            notification.pointer.position=notification.pointer.screen_position=hover_state_.position();
            const auto modifiers = native_modifier_snapshot(SDL_GetModState());
            notification.pointer.modifiers=modifiers.modifiers;
            notification.pointer.physical_control=modifiers.physical_control;
            std::unordered_map<const Window*,NativeToken> tokens;
            for (const auto& [model,target]:windows_) if (target.id==id && !target.pending_destroy) {
                tokens.emplace(model,token_for(target));
                if (target.owns_handles && !target.embedded) notification.physical_owner=target.model;
            }
            if (*notice==WindowNotificationKind::Deactivated) pen_input_.retire(id);
            // ここではOS入力の正規化と世代の検査だけを行う。 通知先の
            // 選択、入力専用のモーダル制限の迂回、 コールバック後の最終
            // 的なフォーカス・再描画状態の反映は、共通の方針に任せる。
            dispatch_window_notification({window_lifecycle_context(),window_stack_,window_close_,
                [this,tokens=std::move(tokens)](const auto& model) {
                    const auto found=tokens.find(model.get());
                    return found!=tokens.end() && native_for(found->second)!=nullptr;
                },
                [this](const auto& model) {close(model);},
                [this] {synchronize_native_focus();}},std::move(notification));
            return *notice!=WindowNotificationKind::CloseRequested || !windows_.empty();
        }
        // 正規化した入力の制限は、共通の route_window_input で一度だけ行う。
        Native* native = native_for(id);
        const bool mouse_button = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP;
        const bool mouse_motion = event.type == SDL_EVENT_MOUSE_MOTION;
        const bool mouse_wheel = event.type == SDL_EVENT_MOUSE_WHEEL;
        const bool touch = event.type == SDL_EVENT_FINGER_DOWN || event.type == SDL_EVENT_FINGER_UP ||
                           event.type == SDL_EVENT_FINGER_MOTION || event.type == SDL_EVENT_FINGER_CANCELED;
        const bool keyboard = event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP ||
                              event.type == SDL_EVENT_TEXT_INPUT || event.type == SDL_EVENT_TEXT_EDITING;
        if (mouse_button || mouse_motion || mouse_wheel || touch || keyboard) {
            std::optional<Point> position;
            if (mouse_button) position=Point{event.button.x,event.button.y};
            else if (mouse_motion) position=Point{event.motion.x,event.motion.y};
            else if (mouse_wheel) position=Point{event.wheel.mouse_x,event.wheel.mouse_y};
            else if (touch && native) {
                int width{},height{}; SDL_GetWindowSize(native->window,&width,&height);
                position=Point{event.tfinger.x*width,event.tfinger.y*height};
            }
            const auto route = route_window_input(window_lifecycle_context(), window_stack_, modal_state_,
                {id, keyboard ? WindowInputKind::Keyboard :
                     touch ? WindowInputKind::Touch : WindowInputKind::Mouse,
                 position, touch ? static_cast<std::uint64_t>(event.tfinger.fingerID) : 0,
                 event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_FINGER_DOWN});
            native = native_for(route.window);
            if (!native) return true;
            if (route.raised) native->updates->request();
        }
        if (native && (mouse_button || mouse_motion || touch))
            return handle_pointer_input(*native,event);
        if (event.type == SDL_EVENT_MOUSE_WHEEL && native) {
            const auto token = token_for(*native);
            WheelEvent wheel;
            wheel.position = {event.wheel.mouse_x, event.wheel.mouse_y};
            const float factor = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0F : 1.0F;
            wheel.delta_x = event.wheel.x * factor;
            wheel.delta_y = event.wheel.y * factor;
            wheel.modifiers = current_modifiers();
            (void)dispatch_and_publish_wheel(input_publication_context(token),
                passive_input_context(token),native->model,wheel);
            return true;
        }

        if ((event.type == SDL_EVENT_TEXT_INPUT || event.type == SDL_EVENT_TEXT_EDITING) && native) {
            const auto token = token_for(*native);
            const auto model = native->model;
            const bool composing = event.type == SDL_EVENT_TEXT_EDITING;
            const auto preedit=composing ? std::optional<TextComposition>{composition_from_characters(
                event.edit.text ? event.edit.text : "",event.edit.start,event.edit.length)} : std::nullopt;
            (void)dispatch_and_publish_edit_text(input_publication_context(token),model,
                {composing ? preedit->text : (event.text.text ? event.text.text : ""), composing, false},
                preedit ? &*preedit : nullptr);
            return true;
        }
        if ((event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) && native) {
            const auto token = token_for(*native);
            const auto model = native->model;
            const auto modifiers = native_modifier_snapshot(event.key.mod);
            const KeyEvent key{sdl_key(event.key.key), modifiers.modifiers, event.key.repeat,
                               false, sdl_printable_key(event.key.key), modifiers.physical_control,
                               sdl_key_character(event.key.scancode, event.key.mod)};
            const bool down = event.type == SDL_EVENT_KEY_DOWN;
            (void)dispatch_and_publish_edit_key(input_publication_context(token),model,key,down);
            return true;
        }
        return true;
    }

    std::unordered_map<Window*,Native> windows_;
    std::unordered_map<TextTextureKey, CachedTextTexture, TextTextureKeyHash,TextTextureKeyEqual>
        text_texture_cache_;
    std::list<const TextTextureKey*> text_texture_recency_;
    bool text_texture_limit_logged_{};
    std::unordered_map<ImageTextureKey,CachedImageTexture,ImageTextureKeyHash>
        image_texture_cache_;
    std::size_t image_texture_cache_bytes_{};
    std::unordered_map<SDL_Renderer*,std::size_t> frame_users_;
    bool native_retirements_pending_{},releasing_native_{};
    SurfaceUpdateRegistry surface_updates_;
    WindowStackStateMachine window_stack_;
    FocusStateMachine focus_state_;
    TextInputPublicationStateMachine text_input_publication_;
    ExternalDropStateMachine external_drop_;
    SdlPenNormalizer pen_input_;
    WindowTextScaleStateMachine text_scale_changes_;
    HoverStateMachine hover_state_;
    MouseInputStateMachine mouse_input_;
    HostedCaptionStateMachine hosted_caption_;
    std::unordered_map<const Window*, std::vector<std::weak_ptr<Window>>> minimized_groups_;
    MenuPopupStateMachine context_popup_;
    ComboPopupStateMachine combo_popup_;
    TextEditStateStore text_edit_states_;
    TouchInputStateMachine touch_input_;
    TTF_Font* font_{};
    std::shared_ptr<SdlFontProvider> fonts_;
    bool owns_sdl_{};
    EditorWindowHostOptions host_options_{};
    EditorPresentMode present_mode_{EditorPresentMode::Immediate};
    FrameProfile frame_profile_{};
    std::shared_ptr<EventLoopDispatcher> dispatcher_;
    std::shared_ptr<SdlGraphicsBackend> graphics_;
    std::shared_ptr<DragDropStateMachine> drag_drop_;
    std::shared_ptr<SystemBackend> system_;
    std::shared_ptr<PlatformAccessibilityBackend> accessibility_;
    std::shared_ptr<AccessibilityActionDispatcher> accessibility_dispatcher_;
    std::shared_ptr<int> accessibility_dispatch_lifetime_;
    ChromeInputStateMachine chrome_input_;
    void cancel_frame_capture(const WidgetPtr& root) {
        chrome_input_.cancel_for(root);
        mouse_input_.cancel_for(root);
    }
    UiScrollStateMachine wheel_scroll_;
    InlineEditStateMachine inline_edit_;
    NumberEditSession number_edit_;
    ComboEditSession combo_edit_;
    ModalStateMachine modal_state_;
    WindowCloseStateMachine window_close_;
    std::size_t native_event_dispatch_depth_{};
    struct AsyncModalSession {
        ModalStateMachine::SessionId id{};
        std::shared_ptr<Dialog> dialog;
        std::shared_ptr<Window> owner;
        bool beginning{};
        bool end_requested{};
        bool defer_end_until_top{};
        bool finishing{};
        bool deferred_by_warning{};
        Action dismiss;
    };
    std::unordered_map<Dialog*, AsyncModalSession> async_modal_sessions_;
    std::uint64_t next_window_generation_{1};
    std::unordered_map<CursorKind, SDL_Cursor*> cursors_;
    std::unordered_map<ResourceId, SDL_Cursor*> custom_cursors_;
    float custom_cursor_scale_{1.0F};
    std::function<std::optional<Color>(Color)> color_picker_;
    AsyncColorPicker color_picker_async_;
    AccessibilityAnnouncementStateMachine accessibility_announcements_;
    std::atomic_bool wake_posted_{false};
    std::function<void(LifecycleEvent)> lifecycle_handler_;
    ::cardwirth::platform::AppVisibility audio_visibility_;
    Window* single_surface_root_{};
};

class SdlClipboard final : public ClipboardBackend {
    static constexpr std::string_view kTextMime{"text/plain;charset=utf-8"};
    static constexpr std::string_view kPlainTextMime{"text/plain"};
    static constexpr std::string_view kFilesMime{
        "application/x-cwxeditor-file-list"};

    struct Payload {
        std::unordered_map<std::string, std::vector<std::byte>> values;
        std::vector<std::string> mime_types;
    };

    static void appendU64(std::vector<std::byte>& output, std::uint64_t value) {
        for (unsigned shift = 0U; shift < 64U; shift += 8U)
            output.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
    }

    static bool readU64(const std::span<const std::byte> input,
                        std::size_t& offset, std::uint64_t& value) {
        if (input.size() - std::min(input.size(), offset) < 8U) return false;
        value = 0U;
        for (unsigned shift = 0U; shift < 64U; shift += 8U)
            value |= static_cast<std::uint64_t>(
                std::to_integer<unsigned char>(input[offset++])) << shift;
        return true;
    }

    static std::vector<std::byte> encodeFiles(
        const std::vector<std::filesystem::path>& files) {
        std::vector<std::byte> output;
        appendU64(output, static_cast<std::uint64_t>(files.size()));
        for (const auto& file : files) {
            const auto encoded = file.generic_u8string();
            appendU64(output, static_cast<std::uint64_t>(encoded.size()));
            const auto* first = reinterpret_cast<const std::byte*>(encoded.data());
            output.insert(output.end(), first, first + encoded.size());
        }
        return output;
    }

    static std::vector<std::filesystem::path> decodeFiles(
        const std::span<const std::byte> input) {
        std::vector<std::filesystem::path> result;
        std::size_t offset{};
        std::uint64_t count{};
        if (!readU64(input, offset, count) || count > input.size()) return result;
        result.reserve(static_cast<std::size_t>(count));
        for (std::uint64_t index = 0U; index < count; ++index) {
            std::uint64_t size{};
            if (!readU64(input, offset, size) ||
                size > input.size() - std::min(input.size(), offset))
                return {};
            const auto* text = reinterpret_cast<const char*>(input.data() + offset);
            result.push_back(std::filesystem::u8path(
                std::string_view(text, static_cast<std::size_t>(size))));
            offset += static_cast<std::size_t>(size);
        }
        return offset == input.size() ? result
                                      : std::vector<std::filesystem::path>{};
    }

    static const void* SDLCALL provide(void* userdata, const char* mime,
                                       std::size_t* size) {
        if (size) *size = 0U;
        if (!userdata || !mime) return nullptr;
        const auto* payload = static_cast<const Payload*>(userdata);
        const auto found = payload->values.find(mime);
        if (found == payload->values.end()) return nullptr;
        if (size) *size = found->second.size();
        return found->second.empty() ? nullptr : found->second.data();
    }

    static void SDLCALL cleanup(void* userdata) {
        delete static_cast<Payload*>(userdata);
    }

    static std::vector<std::byte> clipboardBytes(const std::string_view mime) {
        std::size_t size{};
        void* raw = SDL_GetClipboardData(std::string(mime).c_str(), &size);
        if (!raw) return {};
        const auto* first = static_cast<const std::byte*>(raw);
        std::vector<std::byte> result(first, first + size);
        SDL_free(raw);
        return result;
    }

public:
    void clear() override { (void)SDL_ClearClipboardData(); }

    void write(const ClipboardData& data) override {
        auto payload = std::make_unique<Payload>();
        const char* platform = SDL_GetPlatform();
        const bool windows = platform != nullptr &&
            std::string_view(platform) == "Windows";
        const auto native_text = clipboard_text_for_native(
            data.text, windows ? std::string_view("\r\n")
                               : std::string_view("\n"));
        const auto* text_first = reinterpret_cast<const std::byte*>(native_text.data());
        std::vector<std::byte> text_bytes(text_first,
                                         text_first + native_text.size());
        payload->values.emplace(std::string(kTextMime), text_bytes);
        payload->values.emplace(std::string(kPlainTextMime), std::move(text_bytes));
        for (const auto& [mime, bytes] : data.formats) {
            if (!mime.empty() && mime != kTextMime && mime != kPlainTextMime &&
                mime != kFilesMime)
                payload->values.emplace(mime, bytes);
        }
        if (!data.files.empty())
            payload->values.emplace(std::string(kFilesMime), encodeFiles(data.files));

        payload->mime_types.reserve(payload->values.size());
        for (const auto& [mime, bytes] : payload->values) {
            static_cast<void>(bytes);
            payload->mime_types.push_back(mime);
        }
        std::vector<const char*> mime_pointers;
        mime_pointers.reserve(payload->mime_types.size());
        for (const auto& mime : payload->mime_types)
            mime_pointers.push_back(mime.c_str());

        // SDLは反映成功後にだけデータを所有する。テキストと構造
        // 化形式を一つのOSクリップボード世代に結び付け、 外部
        // アプリが置き換えるとSDLが後処理を呼ぶことで、 古い
        // XMLと新しいネイティブ文字列が混ざらないようにする。
        if (SDL_SetClipboardData(provide, cleanup, payload.get(),
                                 mime_pointers.data(), mime_pointers.size())) {
            (void)payload.release();
#if defined(__EMSCRIPTEN__)
            if (!native_text.empty()) publish_web_clipboard_text(native_text);
#endif
        }
    }

    ClipboardData read() const override {
        ClipboardData result;
        if (char* value = SDL_GetClipboardText()) {
            result.text = normalize_clipboard_text(value);
            SDL_free(value);
        }
        std::size_t count{};
        char** mime_types = SDL_GetClipboardMimeTypes(&count);
        if (!mime_types) return result;
        for (std::size_t index = 0U; index < count; ++index) {
            if (!mime_types[index]) continue;
            const std::string_view mime(mime_types[index]);
            if (mime == kTextMime || mime == kPlainTextMime) continue;
            auto bytes = clipboardBytes(mime);
            if (mime == kFilesMime) result.files = decodeFiles(bytes);
            else result.formats.emplace(std::string(mime), std::move(bytes));
        }
        SDL_free(mime_types);
        return result;
    }

    ClipboardData read_formats(std::span<const std::string_view> requested) const override {
        ClipboardData result;
        bool text_read{};
        for (const auto mime : requested) {
            if (mime == kTextMime || mime == kPlainTextMime) {
                if (!text_read && SDL_HasClipboardText()) {
                    if (char* value = SDL_GetClipboardText()) {
                        result.text = normalize_clipboard_text(value);
                        SDL_free(value);
                    }
                }
                text_read = true;
            } else if (contains(mime)) {
                auto bytes = clipboardBytes(mime);
                if (mime == kFilesMime) result.files = decodeFiles(bytes);
                else result.formats.emplace(std::string(mime), std::move(bytes));
            }
        }
        return result;
    }

    bool contains(const std::string_view mime) const override {
        if (mime == kTextMime || mime == kPlainTextMime)
            return SDL_HasClipboardText();
        return SDL_HasClipboardData(std::string(mime).c_str());
    }
};

class SdlDialogs final : public DialogBackend {
public:
    SdlDialogs(std::shared_ptr<SdlGuiBackend> gui,
               std::shared_ptr<EventLoopDispatcher> dispatcher,
               std::shared_ptr<SystemBackend> system)
        : gui_(std::move(gui)), dispatcher_(std::move(dispatcher)), system_(std::move(system)) {}

    void set_document_root(std::filesystem::path root) override {
        document_root_ = std::move(root).lexically_normal();
    }

    // 選択したパスは、編集中のシナリオなどでこのサービスより長く使われる場合
    // がある。 一時保存領域を解放できるのは、明示的な取り込み完了時だけ。
    void release_import_file(const std::filesystem::path& path) override {
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
        ::cardwirth::explorer::release_upload_paths({path});
#elif defined(__APPLE__) && TARGET_OS_IOS
        ::cardwirth::explorer::apple::release_upload_paths({path});
#else
        (void)path;
#endif
    }

    MessageResult message(const MessageDialogRequest& request) override {
        const auto owner=gui_->dialog_parent_model();
        auto presentation=make_message_dialog(request,owner);
        (void)gui_->show_modal(presentation.dialog,owner);
        return presentation.selected().value_or(MessageResult::None);
    }

    bool supports_message_async() const noexcept override { return true; }

    bool message_async(const MessageDialogRequest& request,
                       MessageCompletion completed) override {
        if (!gui_ || !dispatcher_ || !completed) return false;
        const auto owner = gui_->dialog_parent_model();
        auto presentation = make_message_dialog(request, owner);
        const auto dialog = presentation.dialog;
        const auto selected = presentation.selected;
        if (!dialog || dispatcher_->stopped() ||
            !gui_->begin_modal(dialog, owner)) return false;

        // この軽量な値ダイアログの生成処理にはプラットフォームの
        // コールバックがないため、モデルの結果を定期的に確認する
        // 。 共通の入力経路がボタンの結果を反映し、その次の通
        // 知処理でモーダルを閉じる。 入れ子のイベント処理や、
        // 同期的なJS・Pthreadの応答待ちには入らない。
        struct State final {
            std::shared_ptr<SdlGuiBackend> gui;
            std::shared_ptr<EventLoopDispatcher> dispatcher;
            std::shared_ptr<Dialog> dialog;
            std::function<std::optional<MessageResult>()> selected;
            MessageCompletion completed;
            std::shared_ptr<std::function<void()>> poll;
            bool finished{};

            void finish(const MessageResult result) {
                if (finished) return;
                finished = true;
                // モデル側のコールバックを呼ぶ前に、状態と定期確認の循
                // 環参照を切る。 完了した素材確認ごとに、一時的な UseCounter
                // がアプリ終了まで残ることを防ぐ。
                poll.reset();
                auto callback = std::move(completed);
                if (gui && dialog) gui->end_modal(dialog);
                try {
                    if (callback) callback(result);
                } catch (...) {
                    // 非同期のモデル側コールバックの例外を、SDLのイベント処理の外
                    // へ出さない。 所有するトランザクションが自身の状態を報告する。
                }
            }
        };
        const auto state = std::make_shared<State>(State{
            gui_, dispatcher_, dialog, selected, std::move(completed), {}, false});
        const auto poll = std::make_shared<std::function<void()>>();
        state->poll = poll;
        *poll = [state] {
            if (state->finished) return;
            if (!state->gui || !state->dispatcher || state->dispatcher->stopped()) {
                state->finish(MessageResult::Cancel);
                return;
            }
            if (!state->dialog ||
                (state->dialog->result == DialogResult::None &&
                 !state->dialog->visible())) {
                state->finish(MessageResult::Cancel);
                return;
            }
            if (state->dialog->result != DialogResult::None) {
                state->finish(state->selected
                    ? state->selected().value_or(MessageResult::Cancel)
                    : MessageResult::Cancel);
                return;
            }
            state->dispatcher->post_delayed(std::chrono::milliseconds(16), [state] {
                const auto poll = state->poll;
                if (poll && !state->finished) (*poll)();
            });
        };
        dispatcher_->post([state] {
            const auto poll = state->poll;
            if (poll && !state->finished) (*poll)();
        });
        return true;
    }

    std::vector<std::filesystem::path> open_files(const FileDialogRequest& request) override {
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
        return stored_mobile_pick(FilePickerOperation::OpenFile, request);
#elif defined(__APPLE__) && TARGET_OS_IOS
        return stored_mobile_pick(FilePickerOperation::OpenFile, request);
#else
        return show_file_dialog(SDL_FILEDIALOG_OPENFILE, request).paths;
#endif
    }

    bool open_files_async(const FileDialogRequest& request,
                          FileDialogOpenCompletion completed) override {
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__) || \
    (defined(__APPLE__) && TARGET_OS_IOS)
        if (!completed) return false;
        const auto operation = request.allow_multiple
            ? FilePickerOperation::OpenFiles : FilePickerOperation::OpenFile;
        return stored_mobile_pick_async(operation, request, std::move(completed));
#else
        return DialogBackend::open_files_async(request, std::move(completed));
#endif
    }

    std::optional<std::filesystem::path> save_file(const FileDialogRequest& request) override {
        return save_file_result(request).path;
    }

    FileDialogSaveResult save_file_result(const FileDialogRequest& request) override {
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
        auto chosen = std::make_shared<std::optional<std::size_t>>(request.selected_filter);
        auto paths = stored_mobile_pick(FilePickerOperation::SaveFile, request, chosen);
        return {paths.empty() ? std::nullopt
                              : std::optional<std::filesystem::path>(std::move(paths.front())),
                *chosen};
#elif defined(__APPLE__) && TARGET_OS_IOS
        auto chosen = std::make_shared<std::optional<std::size_t>>(request.selected_filter);
        auto paths = stored_mobile_pick(FilePickerOperation::SaveFile, request, chosen);
        return {paths.empty() ? std::nullopt
                              : std::optional<std::filesystem::path>(paths.front()),
                *chosen};
#else
        auto selection = show_file_dialog(SDL_FILEDIALOG_SAVEFILE, request);
        return {selection.paths.empty() ? std::nullopt
                                        : std::optional<std::filesystem::path>(selection.paths.front()),
                selection.selected_filter};
#endif
    }

    bool save_file_result_async(const FileDialogRequest& request,
                                FileDialogSaveCompletion completed) override {
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__) || \
    (defined(__APPLE__) && TARGET_OS_IOS)
        if (!completed) return false;
        // 要求時の既定値ではなく、ユーザーが選んだファイルの種類。
        auto chosen = std::make_shared<std::optional<std::size_t>>(request.selected_filter);
        return stored_mobile_pick_async(
            FilePickerOperation::SaveFile, request,
            [completed = std::move(completed), chosen](
                std::vector<std::filesystem::path> paths) mutable {
                if (paths.empty()) {
                    completed({std::nullopt, *chosen});
                    return;
                }
                completed({std::move(paths.front()), *chosen});
            }, chosen);
#else
        return DialogBackend::save_file_result_async(
            request, std::move(completed));
#endif
    }

    std::optional<std::filesystem::path> select_directory(const FileDialogRequest& request) override {
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
        auto paths = stored_mobile_pick(FilePickerOperation::SelectDirectory, request);
#elif defined(__APPLE__) && TARGET_OS_IOS
        auto paths = stored_mobile_pick(FilePickerOperation::SelectDirectory, request);
#else
        auto paths = show_file_dialog(SDL_FILEDIALOG_OPENFOLDER, request).paths;
#endif
        return paths.empty() ? std::nullopt : std::optional<std::filesystem::path>(paths.front());
    }

    bool select_directory_async(const FileDialogRequest& request,
                                FileDialogDirectoryCompletion completed) override {
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__) || \
    (defined(__APPLE__) && TARGET_OS_IOS)
        if (!completed) return false;
        return stored_mobile_pick_async(
            FilePickerOperation::SelectDirectory, request,
            [completed = std::move(completed)](
                std::vector<std::filesystem::path> paths) mutable {
                if (paths.empty()) completed(std::nullopt);
                else completed(std::move(paths.front()));
            });
#else
        return DialogBackend::select_directory_async(
            request, std::move(completed));
#endif
    }

    bool finalize_save_file(const std::filesystem::path& path, bool success,
                            std::string& error) override {
#if defined(__EMSCRIPTEN__)
        // /user は選択した永続的な提供側に接続され、書き込み側の
        // 終了・改名が共通の入出力サーバー経由で確定する。 通常のエ
        // ディタ保存を、外部ダウンロード・一時保存APIへ送らない。
        const auto normalized = path.lexically_normal().generic_string();
        if (!normalized.starts_with("/user/")) {
            error = "保存先はUserData内である必要があります";
            return false;
        }
        error.clear();
        return success;
#elif defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IOS)
        try {
            const auto root = std::filesystem::canonical(mobile_user_root());
            const auto target = std::filesystem::weakly_canonical(path);
            auto a = root.begin(), b = target.begin();
            for (; a != root.end(); ++a, ++b)
                if (b == target.end() || *a != *b) throw std::runtime_error("保存領域外の保存先です");
            error.clear(); return success;
        } catch (const std::exception& failure) { error = failure.what(); return false; }
#else
        static_cast<void>(path);
        error.clear();
        return success;
#endif
    }

    std::optional<Color> select_color(Color initial) override {
        if (!gui_) return std::nullopt;
        const auto owner=gui_->dialog_parent_model();
        auto presentation=make_color_selection_dialog(initial,owner);
        (void)gui_->show_modal(presentation.dialog,owner);
        return presentation.selected();
    }

    // message_async と同じモーダルセッション・定期確認の規則を使い、入れ子のイベント処理は行わない。
    bool select_color_async(Color initial, ColorCompletion completed) override {
        if (!gui_ || !dispatcher_ || !completed) return false;
        const auto owner = gui_->dialog_parent_model();
        auto presentation = make_color_selection_dialog(initial, owner);
        const auto dialog = presentation.dialog;
        if (!dialog || dispatcher_->stopped() ||
            !gui_->begin_modal(dialog, owner)) return false;
        struct State final {
            std::shared_ptr<SdlGuiBackend> gui;
            std::shared_ptr<EventLoopDispatcher> dispatcher;
            std::shared_ptr<Dialog> dialog;
            std::function<std::optional<Color>()> selected;
            ColorCompletion completed;
            std::shared_ptr<std::function<void()>> poll;
            bool finished{};

            void finish(const std::optional<Color> result) {
                if (finished) return;
                finished = true;
                poll.reset();
                auto callback = std::move(completed);
                if (gui && dialog) gui->end_modal(dialog);
                try {
                    if (callback) callback(result);
                } catch (...) {
                }
            }
        };
        const auto state = std::make_shared<State>(State{
            gui_, dispatcher_, dialog, presentation.selected, std::move(completed),
            {}, false});
        const auto poll = std::make_shared<std::function<void()>>();
        state->poll = poll;
        *poll = [state] {
            if (state->finished) return;
            if (!state->gui || !state->dispatcher || state->dispatcher->stopped() ||
                !state->dialog ||
                (state->dialog->result == DialogResult::None &&
                 !state->dialog->visible())) {
                state->finish(std::nullopt);
                return;
            }
            if (state->dialog->result != DialogResult::None) {
                state->finish(state->selected ? state->selected() : std::nullopt);
                return;
            }
            state->dispatcher->post_delayed(std::chrono::milliseconds(16), [state] {
                const auto poll = state->poll;
                if (poll && !state->finished) (*poll)();
            });
        };
        dispatcher_->post([state] {
            const auto poll = state->poll;
            if (poll && !state->finished) (*poll)();
        });
        return true;
    }

    std::optional<FontSpec> select_font(const FontSpec& initial) override {
        if (!gui_) return std::nullopt;
        const auto owner=gui_->dialog_parent_model();
        auto presentation=make_font_selection_dialog(initial,owner);
        (void)gui_->show_modal(presentation.dialog,owner);
        return presentation.selected();
    }

private:
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__) || \
    (defined(__APPLE__) && TARGET_OS_IOS)
    std::filesystem::path mobile_user_root() const {
        if (!document_root_.empty()) return document_root_;
        Services services;
        services.system = system_;
        return editor_standalone_user_root(services);
    }
    std::vector<std::filesystem::path> stored_mobile_pick(
        FilePickerOperation operation, const FileDialogRequest& request,
        std::shared_ptr<std::optional<std::size_t>> selected_filter = {}) {
        const auto owner = gui_->dialog_parent_model();
        auto presentation = make_stored_file_dialog(mobile_user_root(), operation, request, owner,
                                                    std::move(selected_filter));
        (void)gui_->show_modal(presentation.dialog, owner);
        return presentation.selected().value_or(std::vector<std::filesystem::path>{});
    }
    bool stored_mobile_pick_async(
        FilePickerOperation operation, const FileDialogRequest& request,
        FileDialogOpenCompletion completed,
        std::shared_ptr<std::optional<std::size_t>> selected_filter = {}) {
        if (!gui_ || !dispatcher_ || !completed || dispatcher_->stopped())
            return false;
        const auto owner = gui_->dialog_parent_model();
        auto presentation = make_stored_file_dialog(
            mobile_user_root(), operation, request, owner, std::move(selected_filter));
        const auto dialog = presentation.dialog;
        if (!dialog || !gui_->begin_modal(dialog, owner)) return false;

        struct State final {
            std::shared_ptr<SdlGuiBackend> gui;
            std::shared_ptr<EventLoopDispatcher> dispatcher;
            std::shared_ptr<Dialog> dialog;
            std::function<std::optional<std::vector<std::filesystem::path>>()> selected;
            FileDialogOpenCompletion completed;
            std::shared_ptr<std::function<void()>> poll;
            bool finished{};

            void finish(std::vector<std::filesystem::path> paths) {
                if (finished) return;
                finished = true;
                poll.reset();
                auto callback = std::move(completed);
                try {
                    if (gui && dialog) gui->end_modal(dialog);
                } catch (...) {
                    // モーダルの終了処理によって、選択完了が呼び出
                    // し元の実行中操作を終了させることを妨げない。
                }
                try {
                    if (callback) callback(std::move(paths));
                } catch (...) {
                    // 完了処理はSDLのイベント処理の一部であり、プラット
                    // フォームのコールバック境界を越えて例外を出さない。
                }
            }
        };
        const auto state = std::make_shared<State>(State{
            gui_, dispatcher_, dialog, presentation.selected,
            std::move(completed), {}, false});
        const auto poll = std::make_shared<std::function<void()>>();
        state->poll = poll;
        *poll = [state] {
            if (state->finished) return;
            if (!state->gui || !state->dispatcher || state->dispatcher->stopped()) {
                state->finish({});
                return;
            }
            if (!state->dialog ||
                (state->dialog->result == DialogResult::None &&
                 !state->dialog->visible())) {
                state->finish({});
                return;
            }
            if (state->dialog->result != DialogResult::None) {
                std::vector<std::filesystem::path> paths;
                try {
                    if (state->selected)
                        paths = state->selected().value_or(
                            std::vector<std::filesystem::path>{});
                } catch (...) {
                    // 不正な選択はキャンセルとして返すが、選
                    // 択画面のモーダル表示は安全に終了する。
                }
                state->finish(std::move(paths));
                return;
            }
            const auto timer = state->dispatcher->post_delayed(
                std::chrono::milliseconds(16), [state] {
                    const auto poll = state->poll;
                    if (poll && !state->finished) (*poll)();
                });
            if (!timer) state->finish({});
        };
        dispatcher_->post([state] {
            const auto poll = state->poll;
            if (poll && !state->finished) (*poll)();
        });
        return true;
    }
#endif
#if defined(__APPLE__) && TARGET_OS_IOS
    [[nodiscard]] bool wait_for_apple(const std::atomic_bool& done,
                                       std::function<void()> start) {
        ExternalDialogOperation operation;
        operation.start=std::move(start);
        operation.completed=[&] { return done.load(std::memory_order_acquire); };
        operation.platform_poll=[] {
            // UIDocumentPicker のデリゲートと調整
            // 付きコピーの完了通知は、UIKitの主ループから届
            // く。 単なる SDL_Delay では主ループを止
            // め、D版互換の同期ダイアログが永久に待ってしまう。
            (void)CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, true);
        };
        return gui_->run_external_dialog(gui_->dialog_parent_model(),std::move(operation));
    }

    std::vector<std::filesystem::path> apple_pick(
        const bool directory, const std::filesystem::path& suggested,
        const bool save, const FileDialogRequest& request) {
        const auto plan = plan_native_file_dialog(save ? FilePickerOperation::SaveFile :
            directory ? FilePickerOperation::SelectDirectory : FilePickerOperation::OpenFile, request);
        struct Selection {
            std::atomic_bool done{};
            std::atomic_bool callback_claimed{};
            ::cardwirth::explorer::NativePathSelection value;
            std::mutex mutex;
            bool release_imports{};
            ~Selection() {
                try {
                    if (release_imports && !value.paths.empty())
                        ::cardwirth::explorer::apple::release_upload_paths(value.paths);
                    else if (!release_imports) {
                        // 所有者のキャンセル後に遅れて届いた結果は、書き込み側へ渡さ
                        // れていない。 その保留中の一時領域の使用権だけを解放する。
                        for (const auto& path : value.paths)
                            ::cardwirth::explorer::apple::finalize_download_path(
                                path, false, [](bool, std::string) {});
                    }
                } catch (...) {
                    // OSの完了コールバックから例外を出さない。 放置されたキ
                    // ャッシュ項目は、提供側の起動時の後処理でも対象にする。
                }
            }
        };
        auto selection = std::make_shared<Selection>();
        selection->release_imports=!save;
        const auto callback = [selection](::cardwirth::explorer::NativePathSelection value) {
            // 提供側のコールバックは一回限りの完了通知。 不正な実
            // 装が二つ目の結果を返したら安全のため拒否し、 呼び出
            // し元へ渡していないその結果の一時領域だけを解放する。
            if (selection->callback_claimed.exchange(true, std::memory_order_acq_rel)) {
                try {
                    if (selection->release_imports)
                        ::cardwirth::explorer::apple::release_upload_paths(value.paths);
                    else for (const auto& path : value.paths)
                        ::cardwirth::explorer::apple::finalize_download_path(
                            path, false, [](bool, std::string) {});
                } catch (...) {}
                return;
            }
            {
                std::scoped_lock lock(selection->mutex);
                selection->value = std::move(value);
            }
            selection->done.store(true, std::memory_order_release);
        };
        const auto start=[&] { if (save) {
            ::cardwirth::explorer::apple::choose_download_path(
                gui_->dialog_parent_window(), suggested, false, callback);
        } else if (directory) {
            ::cardwirth::explorer::apple::choose_upload_folders(gui_->dialog_parent_window(), callback);
        } else {
            ::cardwirth::explorer::apple::ImportOptions options;
            options.title = plan.title;
            options.multiple = plan.allow_multiple;
            const auto& extensions = plan.combined_extensions;
            if (!extensions.unrestricted) options.extensions = extensions.extensions;
            ::cardwirth::explorer::apple::choose_import_files(
                gui_->dialog_parent_window(), std::move(options), callback);
        } };
        if (!wait_for_apple(selection->done,start)) return {};
        std::scoped_lock lock(selection->mutex);
        if (selection->value.cancelled ||
            selection->value.status == ::cardwirth::explorer::TransferStatus::Cancelled) return {};
        if (!selection->value.ok()) throw std::runtime_error(selection->value.error.empty()
            ? "Appleファイル選択に失敗しました" : selection->value.error);
        if (selection->value.paths.empty() || std::any_of(selection->value.paths.begin(),
                selection->value.paths.end(), [](const auto& path) { return path.empty(); }))
            throw std::runtime_error("Appleファイル選択から保存・読込先を取得できませんでした");
        if (save && selection->value.paths.size() != 1)
            throw std::runtime_error("Appleファイル選択から複数の保存先が返されました");
        if (!save && !plan.allow_multiple && selection->value.paths.size() > 1) {
            for (std::size_t index = 1; index < selection->value.paths.size(); ++index)
                release_import_file(selection->value.paths[index]);
        }
        auto paths = std::move(selection->value.paths);
        selection->value.paths.clear();
        if (!plan.allow_multiple && paths.size() > 1) paths.resize(1);
        return paths;
    }
#endif

    struct NativeFileDialogSelection {
        std::vector<std::filesystem::path> paths;
        std::optional<std::size_t> selected_filter;
    };

    struct FileDialogState {
        std::atomic_bool done{};
        std::atomic_bool cancelled{};
        std::vector<std::string> paths_utf8;
        std::exception_ptr failure;
        // コールバックの開始前に、不変の実行計画と代替の識別情報を確立する
        // 。 コールバックは selected_filter を書いてか
        // ら解放順序で完了し、待機側は取得順序で完了を確認してから読む。
        NativeFileDialogPlan plan;
        std::optional<std::size_t> selected_filter;
        std::vector<std::string> labels;
        std::vector<std::string> patterns;
        std::vector<SDL_DialogFileFilter> filters;
        SDL_PropertiesID properties{};

        ~FileDialogState() {
            if (properties) SDL_DestroyProperties(properties);
        }
    };

    static void SDLCALL file_dialog_callback(void* userdata,
                                             const char* const* files,
                                             const int native_filter_index) noexcept {
        std::unique_ptr<std::shared_ptr<FileDialogState>> holder(
            static_cast<std::shared_ptr<FileDialogState>*>(userdata));
        const auto state = *holder;
        // SDLのUTF-8文字列は、このコールバック中だけ有効。 Windows
        // のANSIコードページで復号せず、C ABIの外へ例外を出さない。
        // done の解放・取得の同期により、パスと失敗の両方を待機側へ渡す。
        try {
            if (!state->cancelled.load(std::memory_order_acquire)) {
                state->selected_filter = native_file_dialog_source_filter(
                    state->plan, native_filter_index, state->selected_filter);
                if (!files) throw std::runtime_error(
                    std::string("native file dialog failed: ") + SDL_GetError());
                for (std::size_t index = 0; files[index]; ++index)
                    state->paths_utf8.emplace_back(files[index]);
            }
        } catch (...) {
            state->failure = std::current_exception();
        }
        state->done.store(true, std::memory_order_release);
    }

    NativeFileDialogSelection show_file_dialog(
        SDL_FileDialogType type, const FileDialogRequest& request) {
        const auto mode = type == SDL_FILEDIALOG_OPENFILE ? FilePickerOperation::OpenFile :
            type == SDL_FILEDIALOG_SAVEFILE ? FilePickerOperation::SaveFile :
            type == SDL_FILEDIALOG_OPENFOLDER ? FilePickerOperation::SelectDirectory :
            throw std::invalid_argument("invalid SDL file dialog type");
        const auto plan = plan_native_file_dialog(mode, request);
        auto state = std::make_shared<FileDialogState>();
        state->plan = plan;
        // 説明は実行計画に保持し、タイトルを書き換え
        // ず、SDLの専用の説明プロパティへ渡す。
        state->selected_filter = request.selected_filter;
        state->labels.reserve(plan.filters.size());
        state->patterns.reserve(plan.filters.size());
        for (const auto& filter : plan.filters) {
            state->labels.push_back(filter.label);
            const auto& extensions = filter.extensions;
            if (extensions.unrestricted) {
                state->patterns.emplace_back("*");
                continue;
            }
            std::string pattern;
            for (const auto& extension : extensions.extensions) {
                if (!pattern.empty()) pattern.push_back(';');
                pattern += extension;
            }
            state->patterns.push_back(std::move(pattern));
        }
        state->filters.reserve(state->labels.size());
        for (std::size_t index = 0; index < state->labels.size(); ++index)
            state->filters.push_back(
                {state->labels[index].c_str(), state->patterns[index].c_str()});

        state->properties = SDL_CreateProperties();
        const auto require_property = [](const bool ok) {
            if (!ok) throw std::runtime_error(
                std::string("native file dialog properties failed: ") + SDL_GetError());
        };
        require_property(state->properties != 0);
        if (!plan.title.empty()) require_property(SDL_SetStringProperty(
            state->properties, SDL_PROP_FILE_DIALOG_TITLE_STRING, plan.title.c_str()));
        if (mode == FilePickerOperation::SelectDirectory && !plan.description.empty())
            require_property(SDL_SetStringProperty(
                state->properties, SDL_PROP_FILE_DIALOG_DESCRIPTION_STRING,
                plan.description.c_str()));
        if (!plan.initial_location_utf8.empty()) require_property(SDL_SetStringProperty(
            state->properties, SDL_PROP_FILE_DIALOG_LOCATION_STRING, plan.initial_location_utf8.c_str()));
        if (!state->filters.empty()) {
            require_property(SDL_SetPointerProperty(state->properties, SDL_PROP_FILE_DIALOG_FILTERS_POINTER,
                                   state->filters.data()));
            require_property(SDL_SetNumberProperty(state->properties, SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER,
                                  static_cast<Sint64>(state->filters.size())));
        }
        require_property(SDL_SetBooleanProperty(state->properties, SDL_PROP_FILE_DIALOG_MANY_BOOLEAN,
                               plan.allow_multiple));
        // SDLの保存選択画面は常に上書き確認を行い、抑止するプ
        // ロパティはない。 選択自体ではファイルを書き込まない。
        ExternalDialogOperation operation;
        operation.start=[state,type,gui=gui_,require_property] {
            // 入力の確定・フォーカス喪失がネイティブの所有者を閉じる・置き換える場合がある。
            // 共通のモーダル用の入力停止コールバックが終わってから、ハンドルを取得する。
            if(auto* parent=gui->dialog_parent_window())
                require_property(SDL_SetPointerProperty(state->properties,SDL_PROP_FILE_DIALOG_WINDOW_POINTER,parent));
            auto* callback_state=new std::shared_ptr<FileDialogState>(state);
            SDL_ShowFileDialogWithProperties(type,file_dialog_callback,callback_state,state->properties);
        };
        operation.completed=[state] { return state->done.load(std::memory_order_acquire); };
        operation.cancel=[state] { state->cancelled.store(true,std::memory_order_release); };
        if(!gui_->run_external_dialog(gui_->dialog_parent_model(),std::move(operation)))
            return {{}, request.selected_filter};
        if (state->failure) std::rethrow_exception(state->failure);
        return {native_file_dialog_paths(plan, state->paths_utf8), state->selected_filter};
    }

#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
    std::vector<std::filesystem::path> document_pick(
        FilePickerOperation kind, const FileDialogRequest& request,
        std::optional<std::size_t>* selected_filter = nullptr) {
        const auto plan = plan_native_file_dialog(kind, request);
        const bool exporting = kind == FilePickerOperation::SaveFile;
        struct Selection {
            ::cardwirth::explorer::NativePathSelection result;
            bool exporting{};
            bool handed_off{};
            ~Selection() {
#if defined(__EMSCRIPTEN__)
                // 保存済みの選択は、借用した永続パスであり、一時領域の使用権ではない。
                return;
#endif
                if (handed_off) return;
                // 検証、モーダル状態の復元、所有ウィンドウのいずれかが失敗した
                // 場合、書き込み側に所有権は渡っていない。 SAFが登録した一
                // 時領域だけを中止し、任意のネイティブファイルは削除しない。
                for (const auto& path : result.paths) {
                    try {
                        if (exporting) {
                            ::cardwirth::explorer::finalize_download_path_async(
                                path, false, [](bool, std::string) {});
                        } else {
                            ::cardwirth::explorer::release_upload_paths({path});
                        }
                    } catch (...) {}
                }
            }
        } selection;
        selection.exporting = exporting;
        struct AsyncSelection {
            std::atomic_bool completed{};
            std::mutex mutex;
            ::cardwirth::explorer::NativePathSelection result;
            ::cardwirth::explorer::TransferRequest request;
        };
        auto pending = std::make_shared<AsyncSelection>();
        ExternalDialogOperation operation;
        operation.start = [&, pending] {
            // デスクトップと同様、入力停止のコールバック後に実際の親を取得する。
            auto* parent = gui_->dialog_parent_window();
            auto callback = [pending](::cardwirth::explorer::NativePathSelection result) {
                {
                    std::scoped_lock lock(pending->mutex);
                    pending->result = std::move(result);
                }
                pending->completed.store(true, std::memory_order_release);
            };
#if defined(__EMSCRIPTEN__)
            ::cardwirth::explorer::StoredPathRequest stored;
            stored.title = plan.title;
            stored.initial_location = plan.initial_location_utf8;
            stored.directory = kind == FilePickerOperation::SelectDirectory;
            stored.save = exporting;
            stored.allow_multiple = plan.allow_multiple;
            stored.confirm_overwrite = plan.confirm_overwrite;
            stored.selected_filter = request.selected_filter;
            for (const auto& filter : request.filters) {
                stored.filter_labels.push_back(filter.label);
                std::string patterns;
                for (const auto& pattern : filter.patterns) {
                    if (!patterns.empty()) patterns += ';';
                    patterns += pattern;
                }
                stored.filter_extensions.push_back(std::move(patterns));
            }
            if (!plan.combined_extensions.unrestricted)
                for (const auto& extension : plan.combined_extensions.extensions) {
                    if (!stored.extensions.empty()) stored.extensions += ';';
                    stored.extensions += extension;
                }
            pending->request = ::cardwirth::explorer::choose_stored_paths_async(
                stored, std::move(callback));
#else
            if (exporting) {
                pending->request = ::cardwirth::explorer::choose_download_path_async(
                    parent, std::filesystem::u8path(request.initial_name), false,
                    std::move(callback));
            } else if (kind == FilePickerOperation::SelectDirectory) {
                pending->request = ::cardwirth::explorer::choose_upload_folders_async(
                    parent, std::move(callback));
            } else {
                pending->request = ::cardwirth::explorer::choose_upload_files_async(
                    parent, std::move(callback));
            }
#endif
        };
        operation.completed = [pending] {
            return pending->completed.load(std::memory_order_acquire);
        };
        operation.cancel = [pending] { pending->request.cancel(); };
        if(!gui_->run_external_dialog(gui_->dialog_parent_model(),std::move(operation))) return {};
        {
            std::scoped_lock lock(pending->mutex);
            selection.result = std::move(pending->result);
        }
        const auto& result = selection.result;
        if (result.cancelled || result.status == ::cardwirth::explorer::TransferStatus::Cancelled)
            return {};
        if (!result.ok())
            throw std::runtime_error(result.error.empty()
                ? "ファイル選択に失敗しました" : result.error);
        if (result.paths.empty() || (exporting && result.paths.size() != 1))
            throw std::runtime_error("ファイル選択の結果が不正です");
        if (selected_filter && result.selected_filter) {
            if (*result.selected_filter >= request.filters.size())
                throw std::runtime_error("保存形式の選択結果が不正です");
            *selected_filter = result.selected_filter;
        }
        // デスクトップと同じUTF-8の結果検証と複数選択の規則を使う。
        // SAFは提供側のURIをアプリ専用のネイティブパスへ解決済み。
        std::vector<std::string> paths_utf8;
        paths_utf8.reserve(result.paths.size());
        for (const auto& path : result.paths) {
            const auto text = path.u8string();
            paths_utf8.emplace_back(text.begin(), text.end());
        }
        auto paths = native_file_dialog_paths(plan, paths_utf8);
        if (!exporting && result.paths.size() > paths.size()) {
            // 絞り込みで除かれるのは末尾だけとは限らない。
            // 拒否した提供側・ブラウザーの一時領域だけを
            // 正確に解放し、エディタへ渡した全パスを残す。
            for (const auto& candidate : result.paths) {
                if (std::find(paths.begin(), paths.end(), candidate) == paths.end())
                    release_import_file(candidate);
            }
        }
        selection.handed_off = true; // 一時領域の使用権は、明示的な取り込み完了処理または書き込み側が管理する。
        return paths;
    }
#endif

    std::shared_ptr<SdlGuiBackend> gui_;
    std::shared_ptr<EventLoopDispatcher> dispatcher_;
    std::shared_ptr<SystemBackend> system_;
    std::filesystem::path document_root_;
};

} // namespace

EditorPresentMode parse_editor_present_mode(std::string_view value) noexcept {
    std::string normalized;
    normalized.reserve(value.size());
    for (const unsigned char character : value) {
        if (!std::isspace(character))
            normalized.push_back(static_cast<char>(std::tolower(character)));
    }
    if (normalized.empty()) return EditorPresentMode::Immediate;
    if (normalized == "vsync" || normalized == "sync" || normalized == "1")
        return EditorPresentMode::VSync;
    if (normalized == "immediate" || normalized == "off" || normalized == "0")
        return EditorPresentMode::Immediate;
    return EditorPresentMode::Adaptive;
}

EditorPresentMode editor_present_mode_from_environment() noexcept {
    const char* const value = SDL_getenv("CWXEDITOR_PRESENT_MODE");
    return parse_editor_present_mode(value ? std::string_view{value} : std::string_view{});
}

void configure_sdl_ui_before_init() {
    // SDLは通常、非アクティブなウィンドウを有効化したクリ
    // ックを捨てる。 エディタのツールパレットは即時操作用な
    // ので、最初のクリックを捨てると、 別のウィンドウ・ダ
    // イアログから戻るたびに二度押しが必要に見えてしまう。
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    // SDLの既定はネイティブのIME未確定文字表示。 エディ
    // タは選択・下線も含む共通の未確定文字表示を保持するため
    // 、 合成イベントを要求し、候補一覧だけをOSに任せる。
    SDL_SetHint(SDL_HINT_IME_IMPLEMENTED_UI, "composition");
}

void install_sdl_ui_services(Services& services) {
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__) || defined(__APPLE__)
    ::cardwirth::explorer::cleanup_stale_transfers();
#endif
    auto fonts = std::make_shared<SdlFontProvider>();
    auto graphics = std::make_shared<SdlGraphicsBackend>(fonts);
    if (!services.dispatcher) services.dispatcher=std::make_shared<EventLoopDispatcher>();
    auto dispatcher = std::dynamic_pointer_cast<EventLoopDispatcher>(services.dispatcher);
    auto drag_drop = std::make_shared<DragDropStateMachine>(services.dispatcher);
    std::shared_ptr<PlatformAccessibilityBackend> accessibility;
#if defined(__APPLE__)
    accessibility=make_apple_accessibility_backend();
#endif
    services.graphics=graphics;
    services.image_codecs=std::make_shared<SdlImageCodecBackend>();
    auto gui = std::make_shared<SdlGuiBackend>(
        std::move(fonts), std::move(graphics), dispatcher, drag_drop, services.system,
        std::move(accessibility));
    services.gui=gui;
    services.clipboard=std::make_shared<SdlClipboard>();
    auto dialogs=make_dispatched_dialog_backend(
        std::make_shared<SdlDialogs>(gui, std::move(dispatcher), services.system), services.dispatcher);
    gui->set_color_picker([weak=std::weak_ptr<DialogBackend>(dialogs)](const Color initial) {
        if (const auto backend=weak.lock()) return backend->select_color(initial);
        return std::optional<Color>{};
    });
    gui->set_color_picker_async([weak=std::weak_ptr<DialogBackend>(dialogs)](
            const Color initial, std::function<void(std::optional<Color>)> completed) {
        const auto backend=weak.lock();
        return backend && backend->select_color_async(initial, std::move(completed));
    });
    services.dialogs=dialogs;
    services.file_picker=std::make_shared<NativeFilePicker>(std::move(dialogs));
    services.drag_drop=std::move(drag_drop);
}

void set_sdl_application_lifecycle_handler(
    Services& services, std::function<void(LifecycleEvent)> handler) {
    if (const auto gui = std::dynamic_pointer_cast<SdlGuiBackend>(services.gui))
        gui->set_application_lifecycle_handler(std::move(handler));
}

void invalidate_sdl_renderer_resources(Services& services) {
    if (const auto gui = std::dynamic_pointer_cast<SdlGuiBackend>(services.gui))
        gui->invalidate_renderer_resources();
}

} // namespace cwxeditor::lib
