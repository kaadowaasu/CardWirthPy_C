#include "cw/sdl_frame_adapter.hpp"
#include "cw/util.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>

namespace {

class SdlFrameAdapter final : public ::cw::frame::NativeFrameAdapter {
public:
    ~SdlFrameAdapter() override { destroy_surface(); }

    std::optional<::cw::lib::Integer> default_font_pixel_height() const override {
        const auto display = window_ ? SDL_GetDisplayForWindow(window_) : SDL_GetPrimaryDisplay();
        const auto scale = window_ ? SDL_GetWindowDisplayScale(window_)
                                   : (display ? SDL_GetDisplayContentScale(display) : 0.0F);
        if (!std::isfinite(scale) || scale <= 0.0F) return std::nullopt;
        // SDL 固有の基本処理として、wxMSW の画面 HDC の LOGPIXELSY
        // の代わりに、ディスプレイのユーザー指定の内容倍率を使う。 こ
        // こで同等の 12pt のピクセル高へ変換し、Frame は引き
        // 続き Py と同じ共通の dpi_level の式を適用する。
        return static_cast<::cw::lib::Integer>(std::lround(16.0 * scale));
    }

    void attach_surface(::cw::frame::Frame& frame,
                        const std::shared_ptr<::cw::lib::Window>& panel) override {
        frame_ = &frame;

        const int width = panel ? positive(panel->bounds.width) : 960;
        const int height = panel ? positive(panel->bounds.height) : 640;
        window_ = SDL_CreateWindow(
            "CardWirthPy_C", width, height, SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE);
        if (!window_) {
            throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
        }

        renderer_ = SDL_CreateRenderer(window_, nullptr);
        if (!renderer_) {
            const std::string accelerated_error = SDL_GetError();
            renderer_ = SDL_CreateRenderer(window_, "software");
            if (!renderer_) {
                const std::string software_error = SDL_GetError();
                destroy_surface();
                throw std::runtime_error(
                    "SDL_CreateRenderer failed (accelerated: " + accelerated_error
                    + "; software: " + software_error + ")");
            }
        }
        refresh_client_size();

        frame.m_native_window_handle = reinterpret_cast<std::uintptr_t>(window_);
        if (panel) {
            panel->native_handle = frame.m_native_window_handle;
            if (const auto geometry = ::cw::frame::query_sdl_frame_geometry(frame)) {
                // ここでは基本処理だけを行い、保存された軸の位置は後で共通 Frame が適用する。
                panel->bounds.x = geometry->first.x;
                panel->bounds.y = geometry->first.y;
            }
        }
    }

    void detach_surface(::cw::frame::Frame& frame) override {
        if (frame_ == &frame) {
            frame.m_native_window_handle = 0;
            frame_ = nullptr;
        }
        destroy_surface();
    }

    ::cw::lib::Rect display_geometry(const ::cw::frame::Frame&) const override {
        SDL_Rect bounds{};
        const SDL_DisplayID display = window_ ? SDL_GetDisplayForWindow(window_)
                                              : SDL_GetPrimaryDisplay();
        // Py の Frame.get_displaysize と同様、GetGeometry に相当するモニ
        // ター全体の範囲を使う。 利用可能な作業領域は、ウィンドウが収まるかの確認用に別の基本処理で取得する。
        if (display && SDL_GetDisplayBounds(display, &bounds)) {
            return {bounds.x, bounds.y, bounds.w, bounds.h};
        }
        return {0, 0, 1920, 1080};
    }

    void set_iconized(::cw::frame::Frame&, bool iconized) override {
        if (!window_) return;
        if (iconized) SDL_MinimizeWindow(window_);
        else SDL_RestoreWindow(window_);
    }

    void set_title(::cw::frame::Frame&, const ::cw::lib::String& title) override {
        if (window_) SDL_SetWindowTitle(window_, title.c_str());
    }

    void set_icon(::cw::frame::Frame&,
                  const std::shared_ptr<::cw::lib::Window>&) override {
        // アイコン設定で、Frame::set_title が公開した見出しをリセットしない。
    }

    std::optional<::cw::lib::Point> map_client_to_surface(
        const ::cw::frame::Frame& frame, ::cw::lib::Point point) const override {
        if (!window_) return std::nullopt;

        const auto window_width = client_width_.load(std::memory_order_acquire);
        const auto window_height = client_height_.load(std::memory_order_acquire);
        ::cw::lib::Integer surface_width = 0;
        ::cw::lib::Integer surface_height = 0;
        {
            std::scoped_lock lock(frame.m_present_mutex);
            surface_width = frame.m_present_width;
            surface_height = frame.m_present_height;
        }
        if (window_width <= 0 || window_height <= 0 ||
            surface_width <= 0 || surface_height <= 0) {
            return std::nullopt;
        }
        return ::cw::frame::map_client_point_to_surface(
            point, window_width, window_height, surface_width, surface_height);
    }

    void bind(::cw::frame::Frame& frame, EventSink sink) override {
        frame_ = &frame;
        sink_ = std::move(sink);
        if (window_) {
            refresh_client_size();
            SDL_ShowWindow(window_);
            SDL_RaiseWindow(window_);
        }
    }

    bool post_exec(::cw::frame::Frame&) override {
        // Linux の Frame::main ループは、表示キューを継続的に処理する。
        return window_ != nullptr;
    }

    void present_now(::cw::frame::Frame& frame) override { render(frame); }

    void present(::cw::frame::Frame& frame,
                 const std::optional<::cw::lib::Rect>&) override {
        render(frame);
    }

    void adjust_position(::cw::frame::Frame& frame,
                         const std::shared_ptr<::cw::lib::Window>& panel) override {
        if (!window_ || !panel) {
            return;
        }

        const auto* object =
            std::get_if<::cw::lib::ObjectPtr>(&frame.m_panel.storage());
        const auto frame_panel =
            object && *object
                ? std::dynamic_pointer_cast<::cw::lib::Window>(*object)
                : nullptr;

        // NativeFrameAdapter::adjust_position() が物理的に動かすのは
        // Engine のメインネイティブウィンドウだけ。
        // Frame::move_dlg() からダイアログが渡されても、
        // その論理座標をメイン SDL_Window へ適用してはいけない。
        if (!frame_panel || panel.get() != frame_panel.get()) {
            return;
        }

        const auto geometry = ::cw::frame::query_sdl_frame_geometry(frame);
        if (!geometry) throw std::runtime_error("SDL window position geometry is unavailable");
        ::cw::lib::Window requested;
        requested.bounds = {panel->bounds.x, panel->bounds.y,
                            geometry->first.width, geometry->first.height};
        const auto placed = ::cw::util::constrain_window_to_work_area(
            requested.bounds, ::cw::util::window_work_area(requested));
        int client_x{}, client_y{};
        if (!SDL_GetWindowPosition(window_, &client_x, &client_y))
            throw std::runtime_error(std::string("SDL_GetWindowPosition failed: ") + SDL_GetError());
        // 共通座標は装飾を含む外側ウィンドウを表す。 SDL の
        // 基本処理では、枠のずれを含めた表示領域の原点を使う。
        SDL_SetWindowPosition(window_,
            static_cast<int>(placed.x + client_x - geometry->first.x),
            static_cast<int>(placed.y + client_y - geometry->first.y));
        panel->bounds.x = placed.x;
        panel->bounds.y = placed.y;
    }

    void set_client_size(::cw::frame::Frame&,
                         const std::shared_ptr<::cw::lib::Window>& panel,
                         ::cw::lib::Integer width,
                         ::cw::lib::Integer height) override {
        if (window_) {
            if (!(SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN)) {
                // wx の起動・復元と同様、最小・最大寸法を GetBestSize
                // に固定する。 SDL の制限は表示領域のピクセルで指定す
                // るため、復元時に最大値を下げる前に以前の最小値を解除する。
                if (!SDL_SetWindowMinimumSize(window_, 1, 1) ||
                    !SDL_SetWindowMaximumSize(window_, positive(width), positive(height)) ||
                    !SDL_SetWindowMinimumSize(window_, positive(width), positive(height)))
                    throw std::runtime_error(std::string("SDL fixed client size failed: ") + SDL_GetError());
            }
            SDL_SetWindowSize(window_, positive(width), positive(height));
            // 次の UI 側の更新で最終のプラットフォーム寸法を取得するまで、
            // 要求した論理サイズをゲームスレッドから使えるように保持する。
            client_width_.store(positive(width), std::memory_order_release);
            client_height_.store(positive(height), std::memory_order_release);
        }
        if (panel) {
            panel->bounds.width = positive(width);
            panel->bounds.height = positive(height);
        }
    }

    void set_fullscreen(::cw::frame::Frame&, bool fullscreen) override {
        if (window_ && !SDL_SetWindowFullscreen(window_, fullscreen))
            throw std::runtime_error(std::string("SDL_SetWindowFullscreen failed: ") + SDL_GetError());
        refresh_client_size();
    }

    bool dispatch_menu_accelerator(::cw::frame::Frame&, ::cw::lib::Integer) override {
        return false;
    }

    ::cw::frame::ScreenshotWindows screenshot_windows(::cw::frame::Frame& frame, bool capture_clients) override {
        return NativeFrameAdapter::screenshot_windows(frame,capture_clients);
    }
    bool compose_dialogs(::cw::frame::Frame& frame, ::cw::lib::Bitmap& bitmap, ::cw::lib::Integer y,
        std::tuple<::cw::lib::Integer,::cw::lib::Integer,::cw::lib::Integer> fore,
        std::tuple<::cw::lib::Integer,::cw::lib::Integer,::cw::lib::Integer> back) override {
        frame._put_dlgscreenshots(std::shared_ptr<::cw::lib::Bitmap>(&bitmap,[](auto*){}),y,fore,back);
        return true;
    }

    void watch_left_button(::cw::frame::Frame&, ::cw::lib::Integer) override {}

    std::optional<bool> modal_message(::cw::frame::Frame&,
                                      const ::cw::lib::String&,
                                      const ::cw::lib::String&,
                                      bool,
                                      bool) override {
        return std::nullopt;
    }

private:
    static int positive(::cw::lib::Integer value) {
        return static_cast<int>(std::max<::cw::lib::Integer>(1, value));
    }

    void destroy_surface() {
        if (texture_) {
            SDL_DestroyTexture(texture_);
            texture_ = nullptr;
        }
        if (renderer_) {
            SDL_DestroyRenderer(renderer_);
            renderer_ = nullptr;
        }
        if (window_) {
            SDL_DestroyWindow(window_);
            window_ = nullptr;
        }
        client_width_.store(0, std::memory_order_release);
        client_height_.store(0, std::memory_order_release);
        texture_width_ = 0;
        texture_height_ = 0;
    }

    void refresh_client_size() {
        if (!window_) {
            client_width_.store(0, std::memory_order_release);
            client_height_.store(0, std::memory_order_release);
            return;
        }
        int width = 0;
        int height = 0;
        if (SDL_GetWindowSize(window_, &width, &height)) {
            client_width_.store(width, std::memory_order_release);
            client_height_.store(height, std::memory_order_release);
        }
    }

    bool ensure_texture(int width, int height) {
        if (texture_ && texture_width_ == width && texture_height_ == height) {
            return true;
        }
        if (texture_) {
            SDL_DestroyTexture(texture_);
            texture_ = nullptr;
        }
        texture_ = SDL_CreateTexture(
            renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, width, height);
        if (!texture_) {
            return false;
        }
        SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_LINEAR);
        texture_width_ = width;
        texture_height_ = height;
        return true;
    }

    void render(::cw::frame::Frame& frame) {
        if (!window_ || !renderer_) {
            return;
        }
        // SDL のウィンドウ問い合わせはメインスレッド専用である。 render() は所有
        // スレッドの Frame::drain_present_queue から呼ばれるため、
        // ここで更新し、 ゲームスレッドの当たり判定は不可分に公開したスナップショットを使う。
        refresh_client_size();

        std::shared_ptr<::cw::lib::Surface> surface;
        {
            std::lock_guard<std::mutex> lock(frame.m_present_mutex);
            surface = frame.m_present_surface;
        }
        if (!surface || surface->size.width <= 0 || surface->size.height <= 0 ||
            surface->rgba.empty()) {
            return;
        }
        if (!ensure_texture(surface->size.width, surface->size.height)) {
            return;
        }
        if (!SDL_UpdateTexture(
                texture_, nullptr, surface->rgba.data(), surface->size.width * 4)) {
            return;
        }

        const auto window_width = client_width_.load(std::memory_order_acquire);
        const auto window_height = client_height_.load(std::memory_order_acquire);
        const SDL_FRect destination{
            0.0F, 0.0F, static_cast<float>(std::max(1, window_width)),
            static_cast<float>(std::max(1, window_height))};
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
        SDL_RenderClear(renderer_);
        SDL_RenderTexture(renderer_, texture_, nullptr, &destination);
        SDL_RenderPresent(renderer_);
    }

    SDL_Window* window_{nullptr};
    SDL_Renderer* renderer_{nullptr};
    SDL_Texture* texture_{nullptr};
    ::cw::frame::Frame* frame_{nullptr};
    EventSink sink_{};
    std::atomic<int> client_width_{0};
    std::atomic<int> client_height_{0};
    int texture_width_{0};
    int texture_height_{0};
};

} // namespace

namespace cw::frame {

std::optional<std::pair<::cw::lib::Rect, ::cw::lib::Size>>
query_sdl_frame_geometry(const Frame& frame) {
    if (!frame.m_native_window_handle) return std::nullopt;
    auto* window = reinterpret_cast<SDL_Window*>(frame.m_native_window_handle);
    int x{}, y{}, w{}, h{}, top{}, left{}, bottom{}, right{};
    if (!SDL_GetWindowPosition(window, &x, &y) || !SDL_GetWindowSize(window, &w, &h))
        return std::nullopt;
    // SDL の位置は表示領域の原点を表すが、wx の GetPosition/GetSize
    // は装飾を含む外枠を表す。 枠なしの Web など、装飾
    // の問い合わせに対応しない場合は SDL がゼロを返す。
    (void)SDL_GetWindowBordersSize(window, &top, &left, &bottom, &right);
    return std::make_pair(::cw::lib::Rect{static_cast<::cw::lib::Integer>(x)-left,
                                         static_cast<::cw::lib::Integer>(y)-top,
                                         static_cast<::cw::lib::Integer>(w)+left+right,
                                         static_cast<::cw::lib::Integer>(h)+top+bottom},
                          ::cw::lib::Size{w,h});
}

std::shared_ptr<NativeFrameAdapter> make_sdl_native_adapter() {
    return std::make_shared<SdlFrameAdapter>();
}

} // namespace cw::frame
