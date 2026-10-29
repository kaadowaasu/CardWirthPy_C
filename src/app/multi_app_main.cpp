#define SDL_MAIN_USE_CALLBACKS 1

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3/SDL_main.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "app_shell.hpp"
#if defined(CARDWIRTH_ALL_HOST) && !defined(__EMSCRIPTEN__)
#include "additional_data.hpp"
#endif
#include "ui/text_geometry.hpp"
#if defined(CARDWIRTH_ALL_HOST)
#include "cardwirth_build_timestamp.hpp"
#include "all/all_display_mode.hpp"
#include "all/all_dialog_layer.hpp"
#include "all/all_layout.hpp"
#include "cw/lib/pointer_capture.hpp"
#include "cw/lib/system_clipboard.hpp"
#include "cw/cw.hpp"
#include "cw/eventhandler.hpp"
#endif
#include "explorer/component.hpp"
#include "explorer/modal.hpp"
#include "explorer/platform_transfer.hpp"
#include "editor_launch.hpp"
#if defined(CARDWIRTH_EMBED_EDITOR)
#include "cwxeditor_src/lib/sdl_ui.hpp"
#endif
#if defined(CARDWIRTH_EMBED_ENGINE)
#include "engine_runtime.hpp"
#include "hosted_window_title.hpp"
#include "hosted_dialog_pointer.hpp"
#include "debug_control_server.hpp"
#include "cw/lib/sdl_game_key.hpp"
#include "cw/lib/sdl_key.hpp"
#include "cw/util.hpp"
#endif
#include "platform_font.hpp"
#include "cw/lib/ui_surface.hpp"
#include "runtime_library.hpp"
#if defined(__APPLE__)
#include "platform_apple.hpp"
#endif
#include "platform/storage/app_runtime_context.hpp"
#include "platform/app_visibility.hpp"
#include "platform/storage/storage.hpp"
#include "process.hpp"
#include "ui/backend.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#endif

#if defined(CARDWIRTH_ALL_HOST) && defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

constexpr int kLegacyPageWidth = 960;
constexpr int kLegacyPageHeight = 640;
#if defined(CARDWIRTH_ALL_HOST)
// ALLのEditorは初期表示（1.0＝領域に収めた画
// 面）より縮小しない。 そこからの拡大だけを許可する。
constexpr float kEditorZoomMinimum = 1.00F;
constexpr float kEditorZoomMaximum = 2.00F;
constexpr float kEditorZoomStep = 0.05F;
#endif
// Explorerのナビゲー
// ション領域を狭く保ち、空い
// た幅をファイル一覧へ割り当
// てる。 左端4px、幅82px
// のナビゲーション領域の右
// に6pxの間隔を空け、
// ファイル一覧とパス欄
// はx=92から始める。
constexpr float kExplorerContentLeft = 92.0F;
// パス（アドレス）欄の右端は、ページの端まで広げる。
constexpr float kExplorerAddressRight = 958.0F;
#if defined(CARDWIRTH_ALL_HOST)
constexpr int kWindowWidth = cardwirth::app::all::kAllPcDesignWidth;
constexpr int kWindowHeight = cardwirth::app::all::kAllPcDesignHeight;
#else
constexpr int kWindowWidth = kLegacyPageWidth;
constexpr int kWindowHeight = kLegacyPageHeight;
#endif

std::string path_to_utf8(const std::filesystem::path& value) {
    const auto encoded = value.u8string();
    return std::string(reinterpret_cast<const char*>(encoded.data()), encoded.size());
}

bool argument_value(int argc, char* argv[], std::string_view name, std::string& value) {
    const std::string assignment = std::string(name) + "=";
    for (int index = 1; index < argc; ++index) {
        if (argv[index] && std::string_view(argv[index]).starts_with(assignment)) {
            value = std::string_view(argv[index]).substr(assignment.size());
            return true;
        }
        if (argv[index] && name == argv[index]) {
            value = index + 1 < argc && argv[index + 1] ? argv[index + 1] : "";
            return true;
        }
    }
    return false;
}

enum class PathArgumentState { Absent, Valid, Invalid };

PathArgumentState path_argument_value(int argc, char* argv[],
                                      std::string_view name,
                                      std::string& value) {
    const std::string assignment = std::string(name) + "=";
    bool found = false;
    for (int index = 1; index < argc; ++index) {
        if (!argv[index]) continue;
        const std::string_view argument(argv[index]);
        std::string candidate;
        if (argument.starts_with(assignment)) {
            candidate.assign(argument.substr(assignment.size()));
        } else if (argument == name) {
            if (index + 1 >= argc || !argv[index + 1] ||
                std::string_view(argv[index + 1]).starts_with("-"))
                return PathArgumentState::Invalid;
            candidate = argv[++index];
        } else {
            continue;
        }
        if (found || candidate.empty()) return PathArgumentState::Invalid;
        found = true;
        value = std::move(candidate);
    }
    return found ? PathArgumentState::Valid : PathArgumentState::Absent;
}

std::filesystem::path base_directory() {
#if defined(__EMSCRIPTEN__)
    // Webパッケージにはプログラムだけを含める。 Data・Scenario・Yado
    // など実行時のファイルは、ユーザーが選んだ永続化UserDataへ置く。
    return "/user";
#elif defined(__ANDROID__)
    if (const char* internal = SDL_GetAndroidInternalStoragePath(); internal && *internal)
        return std::filesystem::path(internal) / "app";
    return std::filesystem::current_path() / "app";
#elif defined(__APPLE__)
    if (auto root = cardwirth::app::apple::bundle_resource_root(); !root.empty()) return root;
    if (const char* base = SDL_GetBasePath(); base && *base) return std::filesystem::path(base);
    return std::filesystem::current_path();
#else
    if (const char* base = SDL_GetBasePath(); base && *base) return std::filesystem::path(base);
    return std::filesystem::current_path();
#endif
}

// すべてのSDL製品で、書き込み可能なルートの選択規則を共通にする。 Android
// のライブラリディレクトリは展開済み・読み取り専用なので、SDL_GetBasePath
// からUserDataを求めない。 OS固有のアプリデータ配置はSDL_GetPrefPath
// に集約する。 テストや文書用に別のルートが必要なら
// 、呼び出し元は--user-dataを指定できる。
std::filesystem::path writable_user_root() {
#if defined(__EMSCRIPTEN__)
    return "/user";
#elif defined(__ANDROID__)
    if (char* preference_path = SDL_GetPrefPath("jp.cardwirth", "CardWirthC")) {
        const std::filesystem::path result(preference_path);
        SDL_free(preference_path);
        return result;
    }
#elif defined(__APPLE__)
    if (auto root = cardwirth::app::apple::writable_user_root(); !root.empty()) return root;
    if (char* preference_path = SDL_GetPrefPath("jp.cardwirth", "CardWirthC")) {
        const std::filesystem::path result(preference_path);
        SDL_free(preference_path);
        return result;
    }
#endif
    return base_directory() / "UserData";
}

std::optional<std::pair<int, int>> browser_canvas_size() {
#if defined(__EMSCRIPTEN__)
    double css_width = 0.0;
    double css_height = 0.0;
    if (emscripten_get_element_css_size("#canvas", &css_width, &css_height) == EMSCRIPTEN_RESULT_SUCCESS &&
        std::isfinite(css_width) && std::isfinite(css_height) && css_width > 0 && css_height > 0)
        return std::pair{std::max(1, static_cast<int>(std::lround(css_width))),
                         std::max(1, static_cast<int>(std::lround(css_height)))};
#endif
    return std::nullopt;
}

std::pair<int, int> initial_window_size() {
    if (const auto browser_size = browser_canvas_size()) return *browser_size;
    return {kWindowWidth, kWindowHeight};
}

constexpr bool web_engine_available() noexcept {
#if defined(__EMSCRIPTEN__) && !CARDWIRTH_WEB_PTHREADS
    return false;
#else
    return true;
#endif
}

struct ButtonEntry {
    enum class Visual {
        Default,
        Toolbar,
        Sidebar,
        FileRow,
        Pager,
        Danger,
        Primary,
    };

    std::shared_ptr<cardwirth::ui::Button> button;
    cardwirth::ui::Rect rect;
    Visual visual{Visual::Default};
    bool selected{};
    bool modal{};
    std::optional<std::size_t> explorer_row;
    bool activate_on_release{};
    std::optional<std::size_t> home_card_art;
    bool host_navigation{};
};

struct LabelEntry {
    std::shared_ptr<cardwirth::ui::Label> label;
    SDL_Color color{32, 32, 32, 255};
    bool modal{};
};

struct ExplorerIcon {
    enum class Kind { Home, Folder, File, Trash, Blocked };
    cardwirth::ui::Rect rect;
    Kind kind{Kind::File};
};

struct CachedText {
    SDL_Texture* texture{};
    float width{};
    float height{};
};

#if defined(CARDWIRTH_ALL_HOST)
struct AllHomeTexture {
    SDL_Texture* texture{};
    float width{};
    float height{};
};

struct AllHomeAssets {
    AllHomeTexture background{};
    AllHomeTexture card{};
    std::array<AllHomeTexture, 4> art{};
    bool load_attempted{};
};
#endif

class TextRenderer {
public:
    bool initialize(SDL_Renderer* renderer) {
        renderer_ = renderer;
        // SDL_ttfはALLのSDL_AppInitで一度だけ初期化する。 TextRenderer
        // はこのプロセス共通サービスを借りるだけとし、埋め込みEngine
        // と共有しても ホームへ戻る際にTTF_Quitを二重に呼ばない。
        if (!TTF_WasInit()) return false;
        const auto font_path = find_platform_font();
        if (font_path.empty()) return false;
        font_ = TTF_OpenFont(font_path.c_str(), 16.0F);
        if (!font_)
            SDL_Log("TTF_OpenFont failed (%s): %s", font_path.c_str(), SDL_GetError());
        return font_ != nullptr;
    }

    ~TextRenderer() {
        clear_cache();
        if (font_) TTF_CloseFont(font_);
    }

    void clear_cache() noexcept {
        for (auto& [key, value] : cache_) {
            (void)key;
            SDL_DestroyTexture(value.texture);
        }
        cache_.clear();
    }

    TextRenderer(const TextRenderer&) = delete;
    TextRenderer& operator=(const TextRenderer&) = delete;
    TextRenderer() = default;

    void draw(const std::string& text, float x, float y,
              SDL_Color color = SDL_Color{240, 240, 240, 255}) {
        if (text.empty()) return;
        if (!font_) {
            SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
            SDL_RenderDebugText(renderer_, x, y, text.c_str());
            return;
        }
        std::string key = text;
        key.push_back(static_cast<char>(color.r));
        key.push_back(static_cast<char>(color.g));
        key.push_back(static_cast<char>(color.b));
        key.push_back(static_cast<char>(color.a));
        auto found = cache_.find(key);
        if (found == cache_.end()) {
            if (cache_.size() >= 1024) clear_cache();
            SDL_Surface* surface = TTF_RenderText_Blended(font_, text.c_str(), text.size(), color);
            if (!surface) return;
            CachedText cached;
            cached.texture = SDL_CreateTextureFromSurface(renderer_, surface);
            cached.width = static_cast<float>(surface->w);
            cached.height = static_cast<float>(surface->h);
            SDL_DestroySurface(surface);
            if (!cached.texture) return;
            found = cache_.emplace(std::move(key), cached).first;
        }
        const SDL_FRect destination{x, y, found->second.width, found->second.height};
        SDL_RenderTexture(renderer_, found->second.texture, nullptr, &destination);
    }

    void draw_in_rect(const std::string& text, SDL_FRect rect, float pixel_size,
                      bool centered,
                      SDL_Color color = SDL_Color{240, 240, 240, 255}) {
        if (text.empty() || !std::isfinite(pixel_size) || pixel_size <= 0 ||
            !std::isfinite(rect.x) || !std::isfinite(rect.y) ||
            !std::isfinite(rect.w) || !std::isfinite(rect.h) || rect.w <= 0 || rect.h <= 0) return;
        if (!font_) {
            draw(text, rect.x, rect.y, color);
            return;
        }
        std::string key = text;
        key.push_back(static_cast<char>(color.r));
        key.push_back(static_cast<char>(color.g));
        key.push_back(static_cast<char>(color.b));
        key.push_back(static_cast<char>(color.a));
        auto found = cache_.find(key);
        if (found == cache_.end()) {
            if (cache_.size() >= 1024) clear_cache();
            SDL_Surface* surface = TTF_RenderText_Blended(font_, text.c_str(), text.size(), color);
            if (!surface) return;
            CachedText cached;
            cached.texture = SDL_CreateTextureFromSurface(renderer_, surface);
            cached.width = static_cast<float>(surface->w);
            cached.height = static_cast<float>(surface->h);
            SDL_DestroySurface(surface);
            if (!cached.texture) return;
            found = cache_.emplace(std::move(key), cached).first;
        }
        // 代替フォントは同じポイントサイズでも行が高くなる場合がある
        // 。 幅だけでなく高さも収め、短いラベルがステータスダイア
        // ログやExplorerの隣の行へはみ出さないようにする。
        const auto fitted = cardwirth::ui::fit_text_rect(
            {rect.x, rect.y, rect.w, rect.h},
            found->second.width, found->second.height, pixel_size / 16.0F, centered);
        const SDL_FRect destination{
            fitted.x, fitted.y, fitted.width, fitted.height};
        SDL_RenderTexture(renderer_, found->second.texture, nullptr, &destination);
    }

    void draw_tooltip(const std::string& text, const SDL_FRect anchor,
                      const SDL_FRect safe, const float pixel_size) {
        if (!font_ || text.empty() || safe.w <= 36.0F || safe.h <= 16.0F) return;
        // ALLのデバッガーのホバーヘルプは、従来の14pxの2倍で
        // 表示する。 長い行を小さなフォントへ縮めず、利用可能な幅で
        // 折り返す。 背景の大きさは実際の描画行の高さから求める。
        const float scale = pixel_size / 16.0F;
        constexpr float padding_x = 14.0F;
        constexpr float padding_y = 4.0F;
        const float maximum_width = std::min(safe.w - 8.0F, 1240.0F);
        const auto wrap_width = static_cast<int>(
            std::max(1.0F, std::floor((maximum_width - padding_x * 2.0F) / scale)));
        const auto key = text + std::string("\0tooltip:", 9U) + std::to_string(wrap_width);
        auto found = cache_.find(key);
        if (found == cache_.end()) {
            if (cache_.size() >= 1024) clear_cache();
            SDL_Surface* surface = TTF_RenderText_Blended_Wrapped(
                font_, text.c_str(), text.size(), {0, 0, 0, 255}, wrap_width);
            if (!surface) return;
            CachedText cached;
            cached.texture = SDL_CreateTextureFromSurface(renderer_, surface);
            cached.width = static_cast<float>(surface->w);
            cached.height = static_cast<float>(surface->h);
            SDL_DestroySurface(surface);
            if (!cached.texture) return;
            found = cache_.emplace(key, cached).first;
        }
        const float width = found->second.width * scale + padding_x * 2.0F;
        const float height = found->second.height * scale + padding_y * 2.0F;
        const float x = std::clamp(anchor.x, safe.x + 4.0F,
                                  std::max(safe.x + 4.0F, safe.x + safe.w - width - 4.0F));
        float y = anchor.y + anchor.h + 3.0F;
        if (y + height > safe.y + safe.h - 4.0F) y = anchor.y - height - 3.0F;
        y = std::clamp(y, safe.y + 4.0F,
                       std::max(safe.y + 4.0F, safe.y + safe.h - height - 4.0F));
        const SDL_FRect background{x, y, width, height};
        SDL_SetRenderDrawColor(renderer_, 255, 255, 225, 255);
        SDL_RenderFillRect(renderer_, &background);
        SDL_SetRenderDrawColor(renderer_, 96, 96, 80, 255);
        SDL_RenderRect(renderer_, &background);
        const SDL_FRect destination{x + padding_x, y + padding_y,
                                    found->second.width * scale, found->second.height * scale};
        SDL_RenderTexture(renderer_, found->second.texture, nullptr, &destination);
    }

    void draw_card_name(const std::string& text, const SDL_FPoint origin,
                        const float maximum_width, const float scale,
                        const SDL_Color color) {
        if (text.empty() || maximum_width <= 0.0F || scale <= 0.0F) return;
        if (!font_) {
            draw(text, origin.x, origin.y, color);
            return;
        }
        std::string key = text;
        key.push_back(static_cast<char>(color.r));
        key.push_back(static_cast<char>(color.g));
        key.push_back(static_cast<char>(color.b));
        key.push_back(static_cast<char>(color.a));
        key.append("\0card-name", 10U);
        auto found = cache_.find(key);
        if (found == cache_.end()) {
            if (cache_.size() >= 1024) clear_cache();
            const float old_size = TTF_GetFontSize(font_);
            const auto old_style = TTF_GetFontStyle(font_);
            TTF_SetFontSize(font_, 12.0F);
            TTF_SetFontStyle(font_, static_cast<TTF_FontStyleFlags>(
                old_style | TTF_STYLE_BOLD));
            SDL_Surface* surface = TTF_RenderText_Blended(
                font_, text.c_str(), text.size(), color);
            TTF_SetFontStyle(font_, old_style);
            TTF_SetFontSize(font_, old_size);
            if (!surface) return;
            CachedText cached;
            cached.texture = SDL_CreateTextureFromSurface(renderer_, surface);
            cached.width = static_cast<float>(surface->w);
            cached.height = static_cast<float>(surface->h);
            SDL_DestroySurface(surface);
            if (!cached.texture) return;
            found = cache_.emplace(std::move(key), cached).first;
        }
        float width = found->second.width * scale;
        float height = found->second.height * scale;
        if (width > maximum_width) {
            const float fit = maximum_width / width;
            width *= fit;
            height *= fit;
        }
        const SDL_FRect destination{origin.x, origin.y, width, height};
        SDL_RenderTexture(renderer_, found->second.texture, nullptr, &destination);
    }

    // draw()で自然な16pxのフォントとして描いたときのtextの幅。
    [[nodiscard]] float measure(std::string_view text) const {
        if (text.empty()) return 0.0F;
        if (!font_) return 8.0F * static_cast<float>(text.size());  // デバッグ文字描画による代替処理。
        int width{}, height{};
        if (!TTF_GetStringSize(font_, text.data(), text.size(), &width, &height)) return 0.0F;
        return static_cast<float>(width);
    }

    // 通常のフォント計測経路ではコードポイント単位で収まる位置
    // を返す。フォントがない場合はバイト数による概算となる。
    [[nodiscard]] std::size_t fit_bytes(std::string_view text, float maximum_width) const {
        if (text.empty() || maximum_width <= 0.0F) return 0;
        if (!font_)
            return std::min(text.size(), static_cast<std::size_t>(maximum_width / 8.0F));
        int measured_width{};
        std::size_t measured_length{};
        if (!TTF_MeasureString(font_, text.data(), text.size(),
                               static_cast<int>(maximum_width), &measured_width,
                               &measured_length))
            return 0;
        return std::min(measured_length, text.size());
    }

private:
    SDL_Renderer* renderer_{};
    TTF_Font* font_{};
    std::map<std::string, CachedText> cache_;
};

// Explorerのテキスト編集画面の配置（論理サイズ960x640）。 ダイア
// ログはページ全体の高さを使い、描画・当たり判定・IME領域で同じ値を参照する。
constexpr SDL_FRect kTextEditorPanel{70.0F, 6.0F, 820.0F, 628.0F};
constexpr SDL_FRect kTextEditorBox{100.0F, 92.0F, 760.0F, 456.0F};
constexpr SDL_FRect kTextEditorSearchBox{100.0F, 580.0F, 220.0F, 38.0F};
constexpr SDL_FRect kExplorerImageDialog{40.0F, 30.0F, 880.0F, 580.0F};
constexpr float kTextEditorNumberX = 108.0F;
constexpr float kTextEditorTextX = 158.0F;
constexpr float kTextEditorTextRight = 852.0F;
constexpr float kTextEditorFirstLineY = 98.0F;
constexpr float kTextEditorLineHeight = 22.0F;

std::string truncate_utf8(std::string value, std::size_t maximum_bytes) {
    if (value.size() <= maximum_bytes) return value;
    std::size_t end = maximum_bytes;
    while (end > 0 && (static_cast<unsigned char>(value[end]) & 0xC0U) == 0x80U) --end;
    value.resize(end);
    value += "...";
    return value;
}

std::string format_file_size(std::uintmax_t bytes) {
    constexpr std::uintmax_t kibibyte = 1024;
    constexpr std::uintmax_t mebibyte = kibibyte * 1024;
    constexpr std::uintmax_t gibibyte = mebibyte * 1024;
    if (bytes >= gibibyte) return std::to_string(bytes / gibibyte) + " GB";
    if (bytes >= mebibyte) return std::to_string(bytes / mebibyte) + " MB";
    if (bytes >= kibibyte) return std::to_string(bytes / kibibyte) + " KB";
    return std::to_string(bytes) + " B";
}

std::string format_modified_time(
    const std::optional<std::filesystem::file_time_type>& modified) {
    if (!modified) return "-";
    const auto system_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        *modified - std::filesystem::file_time_type::clock::now() +
        std::chrono::system_clock::now());
    const auto raw_time = std::chrono::system_clock::to_time_t(system_time);
    std::tm local{};
#if defined(_WIN32)
    if (localtime_s(&local, &raw_time) != 0) return "-";
#else
    if (!localtime_r(&raw_time, &local)) return "-";
#endif
    std::ostringstream formatted;
    formatted << std::put_time(&local, "%Y/%m/%d %H:%M");
    return formatted.str();
}

float compact_button_width(std::string_view value) {
    constexpr float ascii_width = 9.0F;
    constexpr float wide_width = 17.0F;
    constexpr float minimum_text_width = wide_width * 3.0F;
    constexpr float horizontal_padding = 12.0F;
    float text_width = 0.0F;
    for (std::size_t offset = 0; offset < value.size();) {
        const auto first = static_cast<unsigned char>(value[offset]);
        if (first < 0x80U) {
            text_width += ascii_width;
            ++offset;
            continue;
        }
        text_width += wide_width;
        ++offset;
        while (offset < value.size() &&
               (static_cast<unsigned char>(value[offset]) & 0xc0U) == 0x80U)
            ++offset;
    }
    return std::max(minimum_text_width, text_width) + horizontal_padding;
}

#if defined(CARDWIRTH_ALL_HOST) && !defined(__EMSCRIPTEN__)
struct NativePackageInstall {
    std::atomic<int> phase{1}; // 1: 実行中、2: 完了、3: 失敗。
    std::atomic<int> done{0};
    std::atomic<int> total{0};
    int count{};
    std::string error;
};
#endif

struct RuntimeState {
    SDL_Window* window{};
    SDL_Renderer* renderer{};
    std::shared_ptr<cardwirth::ui::Backend> backend;
    std::unique_ptr<cardwirth::app::AppShell> shell;
    std::vector<ButtonEntry> buttons;
    std::vector<LabelEntry> labels;
    std::vector<ExplorerIcon> explorer_icons;
    std::shared_ptr<cardwirth::ui::Dialog> dialog;
    std::unique_ptr<TextRenderer> text_renderer;
    std::unique_ptr<cardwirth::explorer::ExplorerComponent> explorer;
    // 終了中の読み込み処理は、所有するI/Oが完了するまで保持する。 ホームへ戻
    // るときに、遅いディレクトリや画像の読み込みをUIスレッドでjoinしない。
    std::vector<std::unique_ptr<cardwirth::explorer::ExplorerComponent>> retiring_explorers;
    // 移行中の他の埋め込みサービスが参照するため、 従来のruntime_storage
    // メンバーはユーザーマウントの名前として残す。
    std::unique_ptr<cardwirth::platform::Storage> asset_storage;
    std::unique_ptr<cardwirth::platform::Storage> runtime_storage;
    std::unique_ptr<cardwirth::platform::AppRuntimeContext> runtime_context;
    cardwirth::explorer::ExplorerModalState explorer_modal;
    bool explorer_extraction_pending{};
    std::vector<std::filesystem::path> explorer_drop_paths;
    std::vector<std::filesystem::path> explorer_drop_batch;
    std::vector<std::filesystem::path> explorer_pending_upload_paths;
    std::vector<std::string> explorer_upload_conflicts;
    std::string explorer_modal_error;
    // Explorerのテキスト編集用の水平スクロール量（ピクセル）。
    // 描画・当たり判定・選択範囲を広げるマウスドラッグで共用する。
    float explorer_text_hscroll{};
    bool explorer_text_dragging{};
    // Explorerのモーダルボタンの短い押下表示。 クリック後にconfigure_page
    // がボタンを作り直すため、押されたものを矩形で記録する。
    cardwirth::ui::Rect explorer_pressed_rect{};
    std::uint64_t explorer_pressed_until{};
    // Explorerのタブ（ドロップダウン）。各タブがディレクトリを記憶し、 切り替え時
    // に一つのExplorerComponentを有効なタブのディレクトリへ移動させる。
    std::vector<std::filesystem::path> explorer_tabs;
    std::size_t explorer_active_tab{};
    std::uint64_t explorer_tab_revision{};
    SDL_Texture* preview_texture{};
    SDL_FRect preview_rect{};
    bool preview_active{};
    bool image_preview_enlarged{};
    bool scrollbar_visible{};
    SDL_FRect scrollbar_thumb{};
    bool finger_tracking{};
    bool finger_scrolled{};
    SDL_FingerID active_finger{};
    float finger_start_y{};
    float finger_last_y{};
    float finger_scroll_remainder{};
    float finger_start_x{};
    Uint64 finger_down_at{};
    std::optional<std::size_t> finger_start_row;
    std::optional<std::size_t> finger_last_selection_row;
    bool finger_selecting{};
    bool pointer_extend_selection{};
    std::shared_ptr<cardwirth::ui::Button> pending_release_button;
    Uint64 last_tap_at{};
    float last_tap_x{};
    float last_tap_y{};
    cardwirth::app::ProcessPtr child;
    std::filesystem::path engine_path;
    std::filesystem::path self_path;
    bool smoke_test{};
    bool smoke_failed{};
    bool quit_requested{};
    bool app_in_background{};
    cardwirth::platform::AppVisibility app_visibility;
    bool initial_frame_rendered{};
    bool transfer_picker_active{};
    std::shared_ptr<std::atomic_bool> callback_lifetime{
        std::make_shared<std::atomic_bool>(true)};
    // Engineのコールバックはワーカーから呼ばれる場合がある。 AppShell
    // のUI状態を所有するSDL_AppIterateからだけ、状態を反映する。
    std::mutex pending_status_mutex;
    std::vector<std::string> pending_status_messages;
    bool launch_editor_on_start{};
    bool launch_engine_on_start{};
    bool nonactive_debug{};
    bool communication_debug{};
    std::string communication_debug_port{};
    bool force_engine_debug{};
    std::string engine_yado;
    std::vector<std::string> encrypted_engine_arguments;
    // 製品ホストへ渡されたEngineのゲーム用引数は、埋め
    // 込み起動でも引き継ぐ。 ホスト専用の遷移・テスト設定
    // と分け、すべての埋め込み・外部起動で同じ契約に従う。
    std::vector<std::string> forwarded_engine_arguments;
    std::string hosted_test_dialog;
    bool ttf_initialized{};
#if defined(CARDWIRTH_EMBED_ENGINE)
    bool owns_engine_logger{};
#endif
    Uint64 started_at{};
#if defined(CARDWIRTH_ALL_HOST)
    struct HostedStatusTexture final {
        // テクスチャをキャッシュしている間は、不変のスナップショットも保持する。 更新通知を
        // 飛ばした後に生アドレスが再利用されると、キャッシュが誤って一致する可能性がある。
        std::shared_ptr<const ::cw::lib::Surface> source{};
        SDL_Texture* texture{};
    };
    cardwirth::app::all::AllLayoutEngine all_layout_engine{};
    cardwirth::app::all::EngineLayoutPreferences engine_layout_preferences{};
    cardwirth::app::all::AllLayoutSnapshot all_layout_snapshot{};
    cardwirth::app::all::AllDialogLayer all_dialog_layer{};
    AllHomeAssets all_home_assets{};
    ::cw::dialog::DialogPresentationCursorHost engine_dialog_cursor;
    // ボタンのホバー表示は実際のマウスだけに追従す
    // る。 タッチやSDLがタッチから合成したマ
    // ウス移動で、タップ後の強調表示を残さない。
    bool dialog_hover_by_mouse{};
    cardwirth::app::all::AspectResizeGuard aspect_resize_guard{};
    bool all_layout_resize_pending{};
    Uint64 browser_viewport_check_after{};
    bool all_page_rebuild_pending{};
    // モバイル・Webの画面回転では途中のウィンドウ・出力サイズが複数通
    // 知される。 一時的なサイズごとにEditorのテクスチャを破棄せず
    // 、前のフレームを保持し、 落ち着いたレイアウトを一度だけ適用する。
    Uint64 all_layout_resize_settle_until{};
    int all_layout_resize_observed_window_width{};
    int all_layout_resize_observed_window_height{};
    int all_layout_resize_observed_output_width{};
    int all_layout_resize_observed_output_height{};
    bool native_aspect_ratio_active{};
    int active_all_page_height{};
    std::optional<cardwirth::app::Page> active_all_page{};
    std::unique_ptr<cardwirth::app::HostedEngineSession> hosted_engine{};
    std::unique_ptr<cardwirth::app::HostedEditorSession> hosted_editor{};
    SDL_Texture* hosted_editor_canvas{};
    // EngineとEditorからホームへ戻るときに共用する、ホスト側の確認画面。
    bool editor_home_confirmation_active{};
    // ホストの入力コールバックが戻ってから、Editorの変更破棄確認を実行する。
    bool editor_home_return_pending{};
    // Engine・Editorの起動やホームへの復帰は時間がか
    // かるため、クリックの1フレーム後に実行する。 先に処理中
    // の表示を出し、処理中はポインターとキーの入力を無視する。
    std::function<void()> busy_action;
    // ALL追加仕様: Webスマホは横向きを待ってからHOMEの移動を実行する。
    std::function<void()> home_landscape_action;
    bool engine_data_navigation_pending{};
    bool busy_action_presented{};
    // ホームへの復帰が承認された後、指定セッションが停止するまで処理中とする。
    bool home_return_busy{};
    bool home_return_waits_engine{};
    Uint64 busy_started_at{};
    // Editorのホームボタンと同じコールバック（HostedEditorOptions::request_home
    // ）。 Web版ALLではブラウザーの戻るボタンもこの経路へ通す。
    std::function<void()> editor_request_home;
    // Web版ALLで、Editor用のブラウザー履歴保護が有効かどうか。
    bool web_editor_back_guard_armed{};
    // Editorの入力コールバックが戻ってから、データ画面へ移る前の変更破棄確認を実行する。
    bool editor_data_navigation_pending{};
    // Editorの「フォルダーを開く」操作で指定された、Explorerのルートから
    // の相対フォルダー。 Data Explorerページの準備ができてから表示する。
    std::optional<std::filesystem::path> editor_data_directory;
    // モードレスの確認画面が開いている間も、保留中の画面遷移を保持する。
    bool editor_navigation_confirmation_active{};
    bool close_editor_after_engine{};
    // ALL仕様: OS終了も保存・キャンセル確認を通してから終了する。
    enum class HostClosePhase { Idle, Requested, Confirming, Engine, Editor };
    HostClosePhase host_close_phase{HostClosePhase::Idle};
    cardwirth::app::Page host_close_origin{cardwirth::app::Page::Home};
    std::optional<cardwirth::app::AppShell> host_close_shell;
    bool reset_editor_on_next_launch{};
    std::optional<int> editor_home_confirmation_press{};
    SDL_FingerID editor_home_confirmation_finger{};
    int editor_logical_height{cardwirth::app::all::kAllEditorDefaultHeight};
    int editor_left_side_space{};
    int editor_right_side_space{};
    int editor_top_side_space{};
    int editor_bottom_side_space{};
    bool home_other_open{};
    bool home_install_prompt_declined{};
    bool home_install_prompt_checked{};
    bool home_install_prompt_pending{};
    bool home_install_prompt_active{};
    bool home_initial_install_active{};
    std::vector<std::string> home_initial_install_remaining;
    bool home_package_install_busy{};
#if !defined(__EMSCRIPTEN__)
    std::shared_ptr<NativePackageInstall> home_package_install;
#endif
    int home_package_progress_current{};
    int home_package_progress_total{};
    std::string home_other_status;
    float editor_zoom{1.0F};
    float editor_base_scale{1.0F};
    float editor_offset_x{};
    float editor_offset_y{};
    int editor_canvas_width{cardwirth::app::all::kAllPcDesignWidth};
    int editor_canvas_height{cardwirth::app::all::kAllEditorDesignHeight};
    std::map<SDL_FingerID, cardwirth::app::all::Point> editor_fingers{};
    bool editor_pinch_active{};
    SDL_FingerID editor_pinch_finger_a{};
    SDL_FingerID editor_pinch_finger_b{};
    float editor_pinch_start_distance{1.0F};
    float editor_pinch_start_zoom{1.0F};
    cardwirth::app::all::Point editor_pinch_anchor{};
    bool editor_zoom_test{};
    int editor_zoom_test_stage{};
    Uint64 editor_zoom_test_started_at{};
    std::filesystem::path editor_zoom_test_report{};
    std::optional<cardwirth::ui::Rect> editor_zoom_test_home_before{};
    cardwirth::app::all::Rect window_rect{};
    cardwirth::app::all::Rect display_rect{};
    cardwirth::app::all::Rect display_usable_rect{};
    cardwirth::app::all::Rect os_safe_rect{};
    int render_output_width{};
    int render_output_height{};
    std::optional<cardwirth::app::all::Size> native_engine_dialog_size{};
    SDL_Texture* hosted_engine_texture{};
    int hosted_engine_texture_width{};
    int hosted_engine_texture_height{};
    SDL_Texture* hosted_debugger_texture{};
    std::uint64_t hosted_debugger_texture_sequence{};
    std::uint64_t hosted_engine_frame_sequence{};
    std::uint64_t hosted_engine_modal_sequence{};
    std::uint64_t hosted_engine_modal_model_id{};
    std::optional<cardwirth::app::HostedEngineModalKind> hosted_test_dialog_kind{};
    bool hosted_test_dialog_sent{};
    SDL_Texture* hosted_dialog_texture{};
    std::uint64_t hosted_dialog_texture_sequence{};
    // ネイティブホストの画面取得は、実際に転送済みの不変画像を対象とする。 表示
    // テクスチャへまだ反映されていない、ゲームスレッドの新しい状態は使わない。
    std::optional<cardwirth::app::HostedEngineModalSnapshot> hosted_dialog_capture_snapshot{};
    std::uint64_t hosted_debugger_sequence{};
    std::uint64_t hosted_status_sequence{};
    std::map<cardwirth::app::HostedStatusAction, HostedStatusTexture>
        hosted_status_textures{};
    // ステータスダイアログの所持金アイコン。0は宿（MONEYY）、1はパーティ（MONEYP）。
    HostedStatusTexture hosted_money_icon_textures[2]{};
    cardwirth::app::Page hosted_engine_parent{cardwirth::app::Page::Home};
    cardwirth::app::Page hosted_editor_parent{cardwirth::app::Page::Home};
    ::cw::dialog::DialogPointerCapture hosted_dialog_pointer{};
    // Engineダイアログの開閉時に、押下の解放を待つ。 kHostedDialogTransitionWaitsForRelease
    // を参照。
    std::uint64_t release_gate_modal{};
    bool release_gate_blocked{};
    bool release_gate_mouse_down{};
    std::vector<SDL_FingerID> release_gate_fingers{};
    std::uint64_t hosted_swipe_model{};
    // 現在の指を押し始めた時点で開いていたEngineのモーダル画面（0
    // はなし）。 ホストの長押し操作は、そのダイアログだけを対象にする。
    std::uint64_t hosted_touch_down_modal{};
    ::cw::dialog::DialogScrollGesture hosted_scroll_gesture;
    std::optional<cardwirth::app::HostedDebuggerControlSnapshot>
        pending_debugger_control{};
    std::optional<cardwirth::app::HostedStatusAction> pending_status_action{};
    // ステータス・ショートカットボタンの押下表示。 短いタッ
    // プでも分かるよう、解放後も押された見た目を短時間保つ。
    std::optional<cardwirth::app::HostedStatusAction> status_pressed_flash{};
    Uint64 status_pressed_flash_until{};
    bool pending_debugger_activate{};
    cw::lib::PointerCapture engine_pointer_capture{};
    bool hosted_touch_game{};
    int hosted_touch_clicks{1};
    bool all_long_press_context_sent{};
    float all_touch_last_x{};
    float all_touch_last_y{};
    std::vector<std::string> pending_dialog_drop_files{};
    bool hosted_debugger_focused{};
    bool hosted_layout_test{};
    int hosted_layout_test_stage{};
    Uint64 hosted_layout_test_stage_at{};
    std::filesystem::path hosted_layout_test_report{};
    std::optional<cardwirth::app::all::Rect> hosted_layout_engine_alone{};
    bool hosted_engine_smoke_test{};
    bool hosted_engine_smoke_stop_requested{};
    bool hosted_engine_smoke_input_sent{};
    bool hosted_engine_smoke_activation_sent{};
    bool hosted_engine_smoke_confirmation_sent{};
    bool hosted_engine_smoke_input_changed{};
    bool hosted_engine_smoke_action_observed{};
    std::string hosted_engine_smoke_probe{};
    std::uint64_t hosted_engine_smoke_input_fingerprint{};
    Uint64 hosted_engine_smoke_input_at{};
    Uint64 hosted_engine_smoke_activation_at{};
    Uint64 hosted_engine_smoke_confirmation_at{};
    std::filesystem::path hosted_engine_smoke_report{};
    bool hosted_key_input_test{};
    bool hosted_key_input_test_stop_requested{};
    int hosted_key_input_test_stage{};
    Uint64 hosted_key_input_test_stage_at{};
    bool hosted_key_backslash_down{};
    bool hosted_key_backslash_up{};
    bool hosted_key_right_ctrl_down{};
    bool hosted_key_right_ctrl_up{};
    bool hosted_key_left_down{};
    bool hosted_key_focus_cleared{};
    bool hosted_key_late_up_clean{};
    std::filesystem::path hosted_key_input_test_report{};
    bool hosted_status_action_test{};
    bool hosted_status_action_test_stop_requested{};
    int hosted_status_action_test_stage{};
    Uint64 hosted_status_action_test_stage_at{};
    bool hosted_status_geometry_scaled{};
    bool hosted_status_settings_opened{};
    bool hosted_status_settings_closed{};
    bool hosted_status_debugger_opened{};
    bool hosted_status_debugger_suppressed_panel{};
    bool hosted_status_debugger_bottom_bar_visible{};
    bool hosted_status_debugger_closed{};
    bool hosted_status_panel_restored{};
    bool hosted_status_all_primary_actions_present{};
    std::filesystem::path hosted_status_action_test_report{};
    std::uintptr_t native_host_window{};
#endif

    ~RuntimeState() {
        callback_lifetime->store(false, std::memory_order_release);
        try { cardwirth::explorer::release_upload_paths(explorer_pending_upload_paths); }
        catch (...) {}
        // ホスト・描画・メディア・借用中のStorageを破棄する前に、 Explorer
        // の読み込みワーカーをjoinし、完了通知を無効にする。
        explorer.reset();
        retiring_explorers.clear();
#if defined(CARDWIRTH_ALL_HOST)
        hosted_editor.reset();
        hosted_engine.reset();
        all_dialog_layer.clear();
        SDL_DestroyTexture(all_home_assets.background.texture);
        SDL_DestroyTexture(all_home_assets.card.texture);
        for (auto& art : all_home_assets.art) SDL_DestroyTexture(art.texture);
        SDL_DestroyTexture(hosted_engine_texture);
        SDL_DestroyTexture(hosted_debugger_texture);
        SDL_DestroyTexture(hosted_dialog_texture);
        SDL_DestroyTexture(hosted_editor_canvas);
        for (auto& [action, cached] : hosted_status_textures) {
            (void)action;
            SDL_DestroyTexture(cached.texture);
        }
        for (auto& cached : hosted_money_icon_textures) SDL_DestroyTexture(cached.texture);
#endif
        if (child) child->terminate();
#if defined(CARDWIRTH_EMBED_ENGINE)
        // ロガーを終了する前に、上の埋め込みEngineの停止とjoinを完了させる。
        if (owns_engine_logger) {
            cardwirth::debug::log("ALL host shutdown: Engine workers joined");
            cardwirth::debug::shutdown_logger();
        }
#endif
        dialog.reset();
        SDL_DestroyTexture(preview_texture);
        labels.clear();
        buttons.clear();
        text_renderer.reset();
        if (ttf_initialized) TTF_Quit();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
    }
};

#if defined(CARDWIRTH_ALL_HOST)
void configure_page(RuntimeState& state);
AllHomeTexture load_all_home_texture(SDL_Renderer* renderer,
                                     const std::filesystem::path& path,
                                     const bool magenta_transparent) {
    AllHomeTexture result;
    const auto encoded = path_to_utf8(path);
    SDL_Surface* surface = IMG_Load(encoded.c_str());
    if (!surface) {
        SDL_Log("ALL Home image load failed (%s): %s", encoded.c_str(), SDL_GetError());
        return result;
    }
    result.width = static_cast<float>(surface->w);
    result.height = static_cast<float>(surface->h);
    if (magenta_transparent)
        (void)SDL_SetSurfaceColorKey(
            surface, true, SDL_MapSurfaceRGB(surface, 255, 0, 255));
    result.texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);
    if (!result.texture) {
        SDL_Log("ALL Home texture creation failed (%s): %s", encoded.c_str(), SDL_GetError());
        return result;
    }
    SDL_SetTextureScaleMode(result.texture, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureBlendMode(result.texture, SDL_BLENDMODE_BLEND);
    return result;
}

void ensure_all_home_assets(RuntimeState& state) {
    auto& assets = state.all_home_assets;
    if (assets.load_attempted) return;
    assets.load_attempted = true;
#if defined(__ANDROID__)
    // Androidではdist/android/UserDataをfiles/直下へ配置する。 base_directory()
    // が指すfiles/appは、変更不可のData/resource用の領域。
    const auto root = state.shell->user_data().root / "System" / "image" / "all";
#else
    const auto root = base_directory() / "System" / "image" / "all";
#endif
    assets.background = load_all_home_texture(state.renderer, root / "background.png", false);
    assets.card = load_all_home_texture(state.renderer, root / "card.png", false);
    constexpr std::array<std::string_view, 4> names{
        "engine.bmp", "editor.bmp", "data_explorer.bmp", "help.bmp"};
    for (std::size_t index = 0; index < names.size(); ++index)
        assets.art[index] = load_all_home_texture(
            state.renderer, root / std::filesystem::path(names[index]), true);
}

void clamp_editor_view(RuntimeState& state) noexcept {
    state.editor_zoom = std::clamp(
        state.editor_zoom, kEditorZoomMinimum, kEditorZoomMaximum);
    const auto& layout = state.all_layout_snapshot.host_to_render;
    const auto viewport = state.all_layout_snapshot.page_rect.valid()
        ? state.all_layout_snapshot.page_rect
        : (state.all_layout_snapshot.safe_rect.valid()
            ? state.all_layout_snapshot.safe_rect
            : cardwirth::app::all::Rect{0.0F, 0.0F,
                  static_cast<float>(cardwirth::app::all::kAllPcDesignWidth),
                  static_cast<float>(state.editor_logical_height)});
    // 描画と入力には、AllLayoutEngineが公開
    // する同じ変換元・変換先を使う。 Editorは利用可
    // 能な高さを満たし、同じ均一倍率から論理幅を求める。
    const auto fallback_surface =
        cardwirth::app::all::editor_logical_surface_size(
            state.all_layout_snapshot.safe_rect.valid()
                ? state.all_layout_snapshot.safe_rect : viewport, state.editor_logical_height,
            state.editor_left_side_space, state.editor_right_side_space,
            state.editor_top_side_space, state.editor_bottom_side_space);
    const auto logical_surface = layout.source.valid() ? layout.source
                                                        : fallback_surface;
    state.editor_base_scale = std::isfinite(layout.scale_x) && layout.scale_x > 0.0F
        ? layout.scale_x
        : (logical_surface.valid() && viewport.valid()
            ? viewport.height / logical_surface.height : 1.0F);
    if (!std::isfinite(state.editor_base_scale) || state.editor_base_scale <= 0.0F)
        state.editor_base_scale = 1.0F;
    const int canvas_width = std::max(1, static_cast<int>(std::lround(
        logical_surface.valid() ? logical_surface.width
                                : cardwirth::app::all::kAllPcDesignWidth)));
    const int canvas_height = std::max(1, static_cast<int>(std::lround(
        logical_surface.valid() ? logical_surface.height
                                : static_cast<float>(state.editor_logical_height))));
    if (state.editor_canvas_width != canvas_width ||
        state.editor_canvas_height != canvas_height) {
        state.editor_canvas_width = canvas_width;
        state.editor_canvas_height = canvas_height;
        SDL_DestroyTexture(state.hosted_editor_canvas);
        state.hosted_editor_canvas = nullptr;
        if (state.hosted_editor)
            state.hosted_editor->resize_surface(canvas_width, canvas_height);
    }
    const float content_scale = state.editor_base_scale * state.editor_zoom;
    const float scaled_width = static_cast<float>(state.editor_canvas_width) * content_scale;
    const float scaled_height = static_cast<float>(state.editor_canvas_height) * content_scale;
    if (scaled_width <= viewport.width) {
        state.editor_offset_x = viewport.x + (viewport.width - scaled_width) * 0.5F;
    }
    else
        state.editor_offset_x = std::clamp(
            state.editor_offset_x, viewport.right() - scaled_width, viewport.x);
    if (scaled_height <= viewport.height)
        state.editor_offset_y = viewport.y + (viewport.height - scaled_height) * 0.5F;
    else
        state.editor_offset_y = std::clamp(
            state.editor_offset_y, viewport.bottom() - scaled_height, viewport.y);
}

cardwirth::app::all::CoordinateTransform editor_surface_transform(const RuntimeState& state) noexcept {
    const float scale = state.editor_base_scale * state.editor_zoom;
    return {{state.editor_offset_x,state.editor_offset_y,
             static_cast<float>(state.editor_canvas_width)*scale,
             static_cast<float>(state.editor_canvas_height)*scale},
            {static_cast<float>(state.editor_canvas_width),
             static_cast<float>(state.editor_canvas_height)},scale,scale};
}

cardwirth::app::all::Point editor_to_local(
    const RuntimeState& state, const float host_x, const float host_y) noexcept {
    return editor_surface_transform(state).to_source_unclipped({host_x,host_y})
        .value_or(cardwirth::app::all::Point{-1000000.0F,-1000000.0F});
}

cardwirth::app::all::Rect editor_destination_rect(const RuntimeState& state) noexcept {
    return editor_surface_transform(state).destination;
}

bool same_optional_size(
    const std::optional<cardwirth::app::all::Size>& left,
    const std::optional<cardwirth::app::all::Size>& right) noexcept {
    if (left.has_value() != right.has_value()) return false;
    if (!left) return true;
    return std::abs(left->width - right->width) < 0.01F &&
           std::abs(left->height - right->height) < 0.01F;
}
#endif

#if defined(CARDWIRTH_ALL_HOST)
cardwirth::app::all::Page all_page(cardwirth::app::Page page) noexcept {
    using AllPage = cardwirth::app::all::Page;
    switch (page) {
        case cardwirth::app::Page::Home: return AllPage::Home;
        case cardwirth::app::Page::CardWirthPy: return AllPage::Engine;
        case cardwirth::app::Page::XEditor: return AllPage::Editor;
        case cardwirth::app::Page::DataExplorer: return AllPage::Explorer;
    }
    return AllPage::Home;
}

void refresh_all_layout(RuntimeState& state) {
    state.all_layout_resize_pending = false;
    using namespace cardwirth::app::all;
    state.editor_logical_height = clamp_editor_height(state.editor_logical_height);
    state.editor_left_side_space = clamp_editor_side_space(state.editor_left_side_space);
    state.editor_right_side_space = clamp_editor_side_space(state.editor_right_side_space);
    state.editor_top_side_space = clamp_editor_side_space(state.editor_top_side_space);
    state.editor_bottom_side_space = clamp_editor_side_space(state.editor_bottom_side_space);
    AllLayoutInput input;
    input.page = all_page(state.shell ? state.shell->page() : cardwirth::app::Page::Home);
    input.editor_design_height = state.editor_logical_height;
    input.editor_left_side_space = state.editor_left_side_space;
    input.editor_right_side_space = state.editor_right_side_space;
    input.editor_top_side_space = state.editor_top_side_space;
    input.editor_bottom_side_space = state.editor_bottom_side_space;
    const int page_design_height = input.page == Page::Editor
        ? state.editor_logical_height : kWindowHeight;
#if defined(__EMSCRIPTEN__)
    input.platform = {AllPlatformClass::Web, true, false, true, false, true};
#elif defined(__ANDROID__) || defined(CARDWIRTH_IOS)
    input.platform = {AllPlatformClass::MobileTablet, true, false, true, false, false};
#else
    input.platform = {AllPlatformClass::NativePc, false, true, true, true, false};
#endif
    int window_width = 0;
    int window_height = 0;
    if (!state.window || !SDL_GetWindowSize(state.window, &window_width, &window_height) ||
        window_width <= 0 || window_height <= 0) {
        window_width = kWindowWidth;
        window_height = kWindowHeight;
    }
    state.window_rect = {0.0F, 0.0F, static_cast<float>(window_width),
                         static_cast<float>(window_height)};

    int render_width = window_width;
    int render_height = window_height;
    if (state.renderer) {
        int queried_width = 0;
        int queried_height = 0;
        if (SDL_GetRenderOutputSize(state.renderer, &queried_width, &queried_height) &&
            queried_width > 0 && queried_height > 0) {
            render_width = queried_width;
            render_height = queried_height;
        }
    }
    state.render_output_width = render_width;
    state.render_output_height = render_height;

    SDL_Rect safe{0, 0, window_width, window_height};
    if (state.window) {
        SDL_Rect queried{};
        if (SDL_GetWindowSafeArea(state.window, &queried) &&
            queried.w > 0 && queried.h > 0)
            safe = queried;
        const SDL_DisplayID display = SDL_GetDisplayForWindow(state.window);
        SDL_Rect bounds{};
        if (display && SDL_GetDisplayBounds(display, &bounds))
            state.display_rect = {static_cast<float>(bounds.x), static_cast<float>(bounds.y),
                                  static_cast<float>(bounds.w), static_cast<float>(bounds.h)};
        if (display && SDL_GetDisplayUsableBounds(display, &bounds))
            state.display_usable_rect = {
                static_cast<float>(bounds.x), static_cast<float>(bounds.y),
                static_cast<float>(bounds.w), static_cast<float>(bounds.h)};
    }

#if defined(__EMSCRIPTEN__) || defined(__ANDROID__) || defined(CARDWIRTH_IOS)
    // PC以外のALLは描画出力の寸法を基準にする。固定のデスクトップ用論
    // 理サイズを使うと、 ALLのレイアウト処理前にSDLが外側の余白を作
    // ってしまう。 論理ルートを実際の出力に合わせ、STRETCHはウィ
    // ンドウ座標から出力ピクセルへの変換だけに使う。 Engineの内容
    // 自体は引き続きAllLayoutEngineが均一な倍率で収める。
    if (state.renderer &&
        !SDL_SetRenderLogicalPresentation(state.renderer, render_width, render_height,
                                          SDL_LOGICAL_PRESENTATION_STRETCH))
        SDL_Log("Unable to synchronize mobile ALL viewport: %s", SDL_GetError());
    const float scale_x = static_cast<float>(render_width) /
                          static_cast<float>(window_width);
    const float scale_y = static_cast<float>(render_height) /
                          static_cast<float>(window_height);
    state.os_safe_rect = {safe.x * scale_x, safe.y * scale_y,
                          safe.w * scale_x, safe.h * scale_y};
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
    // AndroidのMainActivityはシステムバー
    // を隠し、SDL画面をディスプレイ全体へ配置する。 一時
    // 的な切り欠きの余白などをALLのルートへ渡すと、全画面
    // のホームやExplorerがずれてしまう。 Webでも
    // ツールバー変更や画面回転の途中は、CSSのキャンバスが
    // 直前のSDLサイズより小さくなり得る。 確実に描画でき
    // るのはレンダラーの出力矩形だけなので、OSの安全領域を
    // 拡縮して使わない。 そうしないと、ホームやEngine
    // の下端がキャンバスの外へはみ出して欠ける場合がある。
    input.full_rect = {0.0F, 0.0F, static_cast<float>(render_width),
                       static_cast<float>(render_height)};
    input.safe_rect = input.full_rect;
#else
    // ALLの最上位ルートは利用可能な内容領域そのものとし、 推奨
    // サイズの子を内包する、より大きな設計用キャンバスにはしない。
    input.full_rect = state.os_safe_rect;
    input.safe_rect = state.os_safe_rect;
#endif
#else
    if (input.page == Page::Editor) {
        // PCのEditorもWeb・モバイルと同じく、高さを基準にした可変幅
        // の画面とする。 ホームとEngineは従来の固定設計サイズを保つ。
        if (state.renderer &&
            !SDL_SetRenderLogicalPresentation(state.renderer, render_width, render_height,
                                              SDL_LOGICAL_PRESENTATION_STRETCH))
            SDL_Log("Unable to synchronize ALL Editor viewport: %s", SDL_GetError());
        const float scale_x = static_cast<float>(render_width) /
                              static_cast<float>(window_width);
        const float scale_y = static_cast<float>(render_height) /
                              static_cast<float>(window_height);
        state.os_safe_rect = {safe.x * scale_x, safe.y * scale_y,
                              safe.w * scale_x, safe.h * scale_y};
        input.full_rect = {0.0F, 0.0F, static_cast<float>(render_width),
                           static_cast<float>(render_height)};
        input.safe_rect = state.os_safe_rect;
    } else {
        input.full_rect = {0.0F, 0.0F, static_cast<float>(kWindowWidth),
                           static_cast<float>(page_design_height)};
        input.safe_rect = input.full_rect;
        const float scale_x = static_cast<float>(kWindowWidth) /
                              static_cast<float>(window_width);
        const float scale_y = static_cast<float>(page_design_height) /
                              static_cast<float>(window_height);
        state.os_safe_rect = {safe.x * scale_x, safe.y * scale_y,
                              safe.w * scale_x, safe.h * scale_y};
        input.safe_rect = state.os_safe_rect;
    }
#endif

    input.debug_side = state.engine_layout_preferences.debug_side();
    const bool engine_visible = input.page == Page::Engine;
    const auto debugger = state.hosted_engine && engine_visible
        ? state.hosted_engine->debugger_snapshot()
        : cardwirth::app::HostedDebuggerSnapshot{};
    // ALLはすべてのOSでEngineの隣にデバッガーを埋め込む。 公開され
    // る画面は、WindowsのRelease版と互換の共通実装の全体とする。
    input.debugger_visible = debugger.visible;
    const auto status = state.hosted_engine && engine_visible
        ? state.hosted_engine->status_snapshot()
        : cardwirth::app::HostedStatusSnapshot{};
    // ALLの側面パネルでは、デバッガーとステータスダイアログを同時に表示しない。
    input.status_dialog_visible = status.visible && !debugger.visible;
    input.debugger_intrinsic_size = {
        static_cast<float>(debugger.preferred_width),
        static_cast<float>(debugger.preferred_height)};
    input.engine_intrinsic_size = {
        static_cast<float>(std::get<0>(::cw::g_SIZE_GAME)),
        static_cast<float>(std::get<1>(::cw::g_SIZE_GAME))};
    if (state.hosted_engine) {
        const auto frame = state.hosted_engine->frame_snapshot();
        if (frame.surface && frame.surface->size.width > 0 && frame.surface->size.height > 0) {
            input.engine_intrinsic_size = {
                static_cast<float>(frame.surface->size.width),
                static_cast<float>(frame.surface->size.height)};
        }
    }
    // 実際の操作を所有するボタンがあるため、Engine内の従来の横型
    // ステータスバーは維持する。 ALLの縦型ダイアログが有効な間は、
    // ホストにはPythonのSIZE_AREA（632x420）だけ
    // を表示し、 配置とポインター変換にも同じ切り抜き領域を使う。覆わ
    // れた高さ453pxの画像は使わない。 下のバーを隠すのは縦型が実
    // 際に見える間だけとする。Debug表示中は側面の縦型が隠れるため
    // 、 設定値だけで判定すると両方のステータス表示が消えてしまう。
    if (input.status_dialog_visible)
        input.engine_intrinsic_size =
            cardwirth::app::all::status_dialog_engine_source_size(
                input.engine_intrinsic_size);
    // Engineの画像寸法は論理的な元サイズであり、物理表示の
    // 上限ではない。 利用可能な幅か高さのどちらかへ達するまで拡
    // 大し、ダイアログとDebugにも同じグループ倍率を使う。
    input.max_upscale = std::numeric_limits<float>::max();
    if (state.hosted_engine && engine_visible) {
        if (const auto modal = state.hosted_engine->modal_snapshot()) {
            if (modal->presentation_surface &&
                modal->presentation_surface->size.width > 0 &&
                modal->presentation_surface->size.height > 0) {
                // この画面は既にラスタ画像なので、ALLの想
                // 定する論理サイズへ戻してから 子孫用の表示
                // 倍率を適用し、二重に拡大しないようにする。
                const Size intrinsic = hosted_dialog_preferred_size(
                    {static_cast<float>(modal->presentation_surface->size.width),
                     static_cast<float>(modal->presentation_surface->size.height)},
                    modal->debugger_descendant, modal->presentation_scale);
                input.dialogs.push_back({
                    intrinsic, {}, DialogOwnerScope::Engine,
                    DialogPlacementPolicy::Center, false, false, modal->debugger_descendant,
                    {static_cast<float>(modal->center_offset_x / modal->presentation_scale),
                     static_cast<float>(modal->center_offset_y / modal->presentation_scale)}});
            }
        }
    }
    if (state.native_engine_dialog_size) {
        input.dialogs.push_back({
            *state.native_engine_dialog_size, {}, DialogOwnerScope::Engine,
            DialogPlacementPolicy::Center, false, false});
    }
    state.all_layout_snapshot = state.all_layout_engine.compute(input);
    if (input.page == Page::Editor) clamp_editor_view(state);

    // ホストの操作部品はEngine・Editorの座標変換に含めない。 サイズ変
    // 更・画面回転・ピンチ操作でも、物理的な安全領域を基準とした位置を維持する。
    for (auto& entry : state.buttons) {
        if (!entry.button) continue;
        const auto text = entry.button->text();
        if (input.page == Page::Editor && text == "ホームに戻る") {
            const float scale = std::max(0.75F, std::min(
                input.safe_rect.width / static_cast<float>(kWindowWidth),
                input.safe_rect.height / static_cast<float>(kWindowHeight)));
            entry.rect = {input.safe_rect.x + 8.0F * scale,
                          input.safe_rect.bottom() - 48.0F * scale,
                          112.0F * scale, 40.0F * scale};
            entry.button->set_rect(entry.rect);
        } else if (input.page == Page::Engine && entry.host_navigation) {
            const float scale = std::max(0.75F, std::min(
                input.safe_rect.width / static_cast<float>(kWindowWidth),
                input.safe_rect.height / static_cast<float>(kWindowHeight)));
            entry.rect = {input.safe_rect.right() - 96.0F * scale,
                          input.safe_rect.bottom() - 58.0F * scale,
                          80.0F * scale, 42.0F * scale};
            entry.button->set_rect(entry.rect);
        }
    }
}

void flush_all_layout_resize(RuntimeState& state) {
    if (!state.all_layout_resize_pending) return;
    if (state.all_layout_resize_settle_until != 0 &&
        SDL_GetTicks() < state.all_layout_resize_settle_until)
        return;
    // 安定したサイズの測定値は一度だけ使う。再計算前に期
    // 限をリセットし、 後から同じ寸法の回転通知が来て
    // も即時更新せず、改めて安定待ちできるようにする。
    state.all_layout_resize_settle_until = 0;
    const auto previous_page = state.all_layout_snapshot.page_rect;
    const auto page = state.shell ? state.shell->page()
                                  : cardwirth::app::Page::Home;
    const bool rebuild_page = state.all_page_rebuild_pending;
    state.all_page_rebuild_pending = false;
    refresh_all_layout(state);
    const auto& current_page = state.all_layout_snapshot.page_rect;
    const bool viewport_changed = previous_page.valid() && current_page.valid() &&
        (std::abs(previous_page.width - current_page.width) > 0.5F ||
         std::abs(previous_page.height - current_page.height) > 0.5F);
    if (rebuild_page && viewport_changed &&
        (page == cardwirth::app::Page::Home ||
         page == cardwirth::app::Page::DataExplorer)) {
        // ホームとExplorerのモデル・セッションは保持するが、
        // ホスト側の操作部品・行の配置・ラベルは回転後に作り直す。 Home and Explorer retain their model/session,
        configure_page(state);
    }
    if (state.hosted_editor) state.hosted_editor->constrain_to_host();
}

void request_all_layout_resize(RuntimeState& state, const bool force_settle = false) {
    int window_width{};
    int window_height{};
    int output_width{};
    int output_height{};
    if (state.window) SDL_GetWindowSize(state.window, &window_width, &window_height);
    if (state.renderer)
        SDL_GetRenderOutputSize(state.renderer, &output_width, &output_height);
    const bool observed_changed =
        state.all_layout_resize_observed_window_width != window_width ||
        state.all_layout_resize_observed_window_height != window_height ||
        state.all_layout_resize_observed_output_width != output_width ||
        state.all_layout_resize_observed_output_height != output_height;
    state.all_layout_resize_pending = true;
    state.all_page_rebuild_pending = true;
    if (observed_changed || state.all_layout_resize_settle_until == 0) {
        state.all_layout_resize_observed_window_width = window_width;
        state.all_layout_resize_observed_window_height = window_height;
        state.all_layout_resize_observed_output_width = output_width;
        state.all_layout_resize_observed_output_height = output_height;
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__) || defined(CARDWIRTH_IOS)
        const bool dimension_changed =
            static_cast<int>(std::lround(state.window_rect.width)) != window_width ||
            static_cast<int>(std::lround(state.window_rect.height)) != window_height ||
            state.render_output_width != output_width ||
            state.render_output_height != output_height;
        state.all_layout_resize_settle_until = (force_settle || dimension_changed)
            ? SDL_GetTicks() + 180U : 0U;
#else
        state.all_layout_resize_settle_until = 0U;
#endif
    }
#if !defined(_WIN32)
    flush_all_layout_resize(state);
#endif
}

void configure_all_page_surface(RuntimeState& state, const cardwirth::app::Page page) {
    const int height = page == cardwirth::app::Page::XEditor
        ? state.editor_logical_height : kWindowHeight;
    if (state.active_all_page_height == height && state.active_all_page == page) return;
    state.active_all_page_height = height;
    state.active_all_page = page;
#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__) && !defined(CARDWIRTH_IOS)
    if (state.renderer) {
        if (page == cardwirth::app::Page::XEditor) {
            int width{}, output_height{};
            if (!SDL_GetRenderOutputSize(state.renderer, &width, &output_height) ||
                width <= 0 || output_height <= 0) {
                width = kWindowWidth;
                output_height = height;
            }
            SDL_SetRenderLogicalPresentation(state.renderer, width, output_height,
                                              SDL_LOGICAL_PRESENTATION_STRETCH);
        } else {
            SDL_SetRenderLogicalPresentation(state.renderer, kWindowWidth, height,
                                              SDL_LOGICAL_PRESENTATION_LETTERBOX);
        }
    }
#endif
#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__) && !defined(CARDWIRTH_IOS)
    if (!state.window) return;
    if (page == cardwirth::app::Page::XEditor) {
        // Editorの高さから均一なUI倍率を求める。幅は自由に変
        // 更でき、 その倍率に対応する論理キャンバス幅へ変換する。
        SDL_SetWindowMinimumSize(
            state.window,
            static_cast<int>(std::lround(480.0F * cardwirth::app::all::kAllPcMinimumScale)),
            static_cast<int>(std::lround(height * cardwirth::app::all::kAllPcMinimumScale)));
        (void)SDL_SetWindowAspectRatio(state.window, 0.0F, 0.0F);
        state.native_aspect_ratio_active = false;
        return;
    }
    const float aspect = static_cast<float>(kWindowWidth) / static_cast<float>(height);
    SDL_SetWindowMinimumSize(
        state.window,
        static_cast<int>(std::lround(kWindowWidth * cardwirth::app::all::kAllPcMinimumScale)),
        static_cast<int>(std::lround(height * cardwirth::app::all::kAllPcMinimumScale)));
    state.native_aspect_ratio_active = SDL_SetWindowAspectRatio(state.window, aspect, aspect);
    int width{}, current_height{};
    if (SDL_GetWindowSize(state.window, &width, &current_height) && width > 0) {
        int target_width = width;
        int target_height = std::max(1, static_cast<int>(std::lround(width / aspect)));
        SDL_Rect work{};
        const auto display = SDL_GetDisplayForWindow(state.window);
        if (display && SDL_GetDisplayUsableBounds(display, &work) && target_height > work.h) {
            target_height = work.h;
            target_width = std::max(1, static_cast<int>(std::lround(target_height * aspect)));
        }
        if (target_width != width || target_height != current_height)
            SDL_SetWindowSize(state.window, target_width, target_height);
        state.aspect_resize_guard.acknowledge(target_width, target_height);
    }
#endif
}

std::optional<cardwirth::app::all::Point> hosted_engine_point(
    const RuntimeState& state, float host_x, float host_y) {
    if (!state.all_layout_snapshot.engine_debug) return std::nullopt;
    const auto& layout = *state.all_layout_snapshot.engine_debug;
    const cardwirth::app::all::Point host{host_x, host_y};
    if (!layout.engine_content_rect.contains(host) || layout.scale <= 0.0F)
        return std::nullopt;
    return cardwirth::app::all::Point{
        (host_x - layout.engine_content_rect.x) / layout.scale,
        (host_y - layout.engine_content_rect.y) / layout.scale};
}

std::uint64_t hosted_frame_fingerprint(const cardwirth::app::HostedEngineFrameSnapshot& frame) {
    if (!frame.surface) return 0;
    // 選択や強調表示の更新検出には、サンプリングしたFNV-1aハッシュを使う。
    // スモークテストの更新ごとに数MBのフレーム全体をハッシュ化するのを避ける。
    std::uint64_t hash = 1469598103934665603ULL;
    const auto& rgba = frame.surface->rgba;
    for (std::size_t offset = 0; offset < rgba.size(); offset += 64) {
        hash ^= rgba[offset];
        hash *= 1099511628211ULL;
    }
    if (!rgba.empty()) {
        hash ^= rgba.back();
        hash *= 1099511628211ULL;
    }
    hash ^= static_cast<std::uint64_t>(frame.surface->size.width);
    hash *= 1099511628211ULL;
    hash ^= static_cast<std::uint64_t>(frame.surface->size.height);
    return hash;
}

bool contains(SDL_FRect rect, float x, float y) noexcept {
    return x >= rect.x && y >= rect.y && x <= rect.x + rect.w && y <= rect.y + rect.h;
}

#if defined(CARDWIRTH_ALL_HOST)
struct HostedStatusControl {
    SDL_FRect rect{};
    cardwirth::app::HostedStatusButtonSnapshot button{};
};

std::vector<HostedStatusControl> hosted_status_controls(const RuntimeState& state) {
    std::vector<HostedStatusControl> result;
    if (!state.hosted_engine || !state.all_layout_snapshot.engine_debug) return result;
    const auto& layout = *state.all_layout_snapshot.engine_debug;
    if (!layout.status_dialog_visible) return result;
    const auto snapshot = state.hosted_engine->status_snapshot();
    if (!snapshot.click_wait_notice.empty()) return result;
    const auto panel = layout.status_dialog_rect;
    const float x = panel.x;
    const float ui_scale = std::max(0.1F, panel.width / 90.0F);
    const float cell = panel.width * 0.5F;
    const float row_h = 42.0F * ui_scale;
    const float button_gap = 2.0F * ui_scale;
    const float regular_button_height = row_h - button_gap;
    const float wide_button_height = regular_button_height * 1.5F;
    const float close_button_extra = wide_button_height - regular_button_height;
    const float wide_row_step = wide_button_height + button_gap;
    const float bottom_button_height = 34.0F * 1.5F * ui_scale;
    const float bottom_button_step = bottom_button_height + button_gap;
    // 宿の所持金を表示しない場合も、所持金の2行分は確保する。
    // CloseDialogは従来の最上段より高いため、下の
    // 項目は増えた高さの後から配置し、枠に隠れないようにする。
    float next_y = panel.y + (42.0F + 84.0F) * ui_scale + close_button_extra;
    int half_column = 0;
    const auto is_bottom = [](const auto action) {
        return action == cardwirth::app::HostedStatusAction::CampTable ||
            action == cardwirth::app::HostedStatusAction::BattleAction ||
            action == cardwirth::app::HostedStatusAction::BattleRunAway ||
            action == cardwirth::app::HostedStatusAction::CancelCardControl;
    };
    for (const auto& button : snapshot.buttons) {
        if (button.action == cardwirth::app::HostedStatusAction::CloseDialog) {
            result.push_back({{x, panel.y, panel.width,
                               wide_button_height}, button});
            continue;
        }
        if (is_bottom(button.action)) continue;
        if (button.full_width) {
            if (half_column) { next_y += row_h; half_column = 0; }
            result.push_back({{x, next_y, panel.width,
                               wide_button_height}, button});
            next_y += wide_row_step;
        } else {
            result.push_back({{x + cell * half_column, next_y, cell,
                               regular_button_height}, button});
            if (++half_column == 2) { half_column = 0; next_y += row_h; }
        }
    }
    const auto append_bottom = [&](const cardwirth::app::HostedStatusAction action,
                                   const float y, const float height) {
        const auto found = std::find_if(snapshot.buttons.begin(), snapshot.buttons.end(),
            [action](const auto& value) { return value.action == action; });
        if (found != snapshot.buttons.end())
            result.push_back({{x, y, panel.width, height}, *found});
    };
    append_bottom(cardwirth::app::HostedStatusAction::CampTable,
                  panel.bottom() - bottom_button_step, bottom_button_height);
    append_bottom(cardwirth::app::HostedStatusAction::BattleAction,
                  panel.bottom() - bottom_button_step, bottom_button_height);
    append_bottom(cardwirth::app::HostedStatusAction::BattleRunAway,
                   panel.bottom() - 2.0F * bottom_button_step, bottom_button_height);
    const bool primary_bottom_control = std::any_of(snapshot.buttons.begin(), snapshot.buttons.end(),
        [](const auto& button) {
            return button.action == cardwirth::app::HostedStatusAction::CampTable ||
                button.action == cardwirth::app::HostedStatusAction::BattleAction;
        });
    const bool runaway_control = std::any_of(snapshot.buttons.begin(), snapshot.buttons.end(),
        [](const auto& button) {
            return button.action == cardwirth::app::HostedStatusAction::BattleRunAway;
        });
    float reserved_bottom = primary_bottom_control ? bottom_button_step : 0.0F;
    if (runaway_control) reserved_bottom = std::max(reserved_bottom, 2.0F * bottom_button_step);
    const bool cancel_control = std::any_of(snapshot.buttons.begin(), snapshot.buttons.end(),
        [](const auto& button) {
            return button.action == cardwirth::app::HostedStatusAction::CancelCardControl;
        });
    if (cancel_control) {
        append_bottom(cardwirth::app::HostedStatusAction::CancelCardControl,
                       panel.bottom() - (reserved_bottom + bottom_button_step),
                       bottom_button_height);
        reserved_bottom += bottom_button_step;
    }
    // ALL専用の右側ショートカットパネル。ボタンはすべてキャンプ・
    // テーブルボタンと同じ大きさで、 最上部のボタンが下のショート
    // カットを折り畳む・展開する。設定が一つもなければ表示しない。
    if (!snapshot.shortcuts.empty() && layout.shortcut_panel_rect.valid()) {
        const auto right = layout.shortcut_panel_rect;
        float y = right.y;
        cardwirth::app::HostedStatusButtonSnapshot toggle;
        toggle.action = cardwirth::app::HostedStatusAction::ToggleShortcuts;
        toggle.label = snapshot.shortcuts_folded ? "展開する" : "たたむ";
        toggle.full_width = true;
        result.push_back({{right.x, y, right.width, bottom_button_height}, toggle});
        y += bottom_button_step;
        if (!snapshot.shortcuts_folded) {
            for (const auto& shortcut : snapshot.shortcuts) {
                if (y + bottom_button_height > right.bottom()) break;
                result.push_back({{right.x, y, right.width, bottom_button_height}, shortcut});
                y += bottom_button_step;
            }
        }
    }
    return result;
}

bool hosted_shortcut_action(const cardwirth::app::HostedStatusAction action) noexcept {
    using Action = cardwirth::app::HostedStatusAction;
    return action == Action::ToggleShortcuts || action == Action::Save ||
        action == Action::Load || action == Action::Screenshot ||
        action == Action::Instructions || action == Action::CardScreenshot ||
        action == Action::FullRecovery || action == Action::CustomStatus;
}

std::optional<cardwirth::app::HostedStatusAction> hosted_status_action_at(
    const RuntimeState& state, float x, float y) {
    // ALLではメッセージログ表示中、ステータスダイアログ
    // のどこに触れてもログを閉じる。 大部分をボタンが占め
    // るため、ログ表示中はボタン本来の操作を無効にする。
    if (state.hosted_engine && state.all_layout_snapshot.engine_debug &&
        state.all_layout_snapshot.engine_debug->status_dialog_visible) {
        const auto& panel = state.all_layout_snapshot.engine_debug->status_dialog_rect;
        if (contains({panel.x, panel.y, panel.width, panel.height}, x, y) &&
            state.hosted_engine->status_snapshot().backlog_showing)
            return cardwirth::app::HostedStatusAction::CancelBacklog;
    }
    for (const auto& control : hosted_status_controls(state))
        if (contains(control.rect, x, y) && control.button.enabled)
            return control.button.action;
    return std::nullopt;
}

std::shared_ptr<cardwirth::ui::Button> hosted_home_button_at(
    const RuntimeState& state, float x, float y) {
    for (const auto& entry : state.buttons) {
        if (!entry.button || !entry.host_navigation ||
            !entry.button->visible() || !entry.button->enabled()) continue;
        const auto& rect = entry.rect;
        if (x >= rect.x && x <= rect.x + rect.width &&
            y >= rect.y && y <= rect.y + rect.height) return entry.button;
    }
    return {};
}

void finish_home_confirmation(RuntimeState& state, bool accepted) {
    state.editor_home_confirmation_active = false;
    state.editor_home_confirmation_press.reset();
    if (state.shell->page() == cardwirth::app::Page::CardWirthPy) {
        if (accepted && state.hosted_engine && state.hosted_engine->running()) {
            // ホームへの復帰はホスト側で確認済みなので、再確認しない。
            state.hosted_engine->request_stop(true);
            state.shell->set_status("Engineを終了しています…");
            state.home_return_busy = true;
            state.home_return_waits_engine = true;
            state.busy_started_at = SDL_GetTicks();
        }
    } else if (state.hosted_editor && state.hosted_editor->running()) {
        if (accepted) {
            // 処理中の表示は、入力が必要なEditorの変更破棄確認が済ん
            // でからprocess_editor_home_returnで
            // 開始する。 このALLのマウス・キーコールバック内ではEditor
            // の確認画面を開かず、 ホストイベントが戻った後のSDL_AppIterate
            // で入れ子のモーダル処理を始める。
            state.editor_home_return_pending = true;
        } else {
            state.hosted_editor->set_input_blocked(false);
        }
    }
}

void process_editor_home_return(RuntimeState& state,
                                const bool confirmation_already_accepted = false) {
    if (!state.hosted_editor || !state.hosted_editor->running()) return;
    state.hosted_editor->set_input_blocked(false);
    // ホームはシナリオの内容を書き込まない。変更破棄がキャンセルされた場合は、 Editor
    // と、その背後のEngine・Explorerのセッションを維持する。
    if (!confirmation_already_accepted &&
        !state.hosted_editor->prepare_stop(true)) return;
    if (state.hosted_engine && state.hosted_engine->running()) {
        state.close_editor_after_engine = true;
        // 変更破棄の確認が承認されたら、すぐホームへ戻る。
        // EditorとEngineがバックグラウンド
        // で非同期保存の完了を待っていても、 Editor
        // 画面を残してアプリが停止したように見せない。
        state.shell->set_initial_page(cardwirth::app::Page::Home);
        configure_page(state);
        state.shell->set_status("Engineを終了しています…");
        state.hosted_engine->request_stop(true);
        state.home_return_busy = true;
        state.home_return_waits_engine = true;
        state.busy_started_at = SDL_GetTicks();
        return;
    }
    state.shell->set_initial_page(cardwirth::app::Page::Home);
    configure_page(state);
    state.hosted_editor->request_stop();
    state.shell->set_status("Editorを終了しています…");
    state.home_return_busy = true;
    state.home_return_waits_engine = false;
    state.busy_started_at = SDL_GetTicks();
}

void process_editor_data_navigation(RuntimeState& state,
                                    const bool confirmation_already_accepted = false) {
    if (!state.hosted_editor || !state.hosted_editor->running() ||
        state.shell->page() != cardwirth::app::Page::XEditor ||
        state.editor_home_confirmation_active ||
        state.host_close_phase != RuntimeState::HostClosePhase::Idle) return;
    state.hosted_editor->set_input_blocked(false);
    if (!confirmation_already_accepted &&
        !state.hosted_editor->prepare_external_navigation()) return;
    state.shell->navigate(cardwirth::app::Page::DataExplorer);
    configure_page(state);
}

void complete_editor_navigation_confirmation(RuntimeState& state,
                                             const bool home_route,
                                             const bool accepted) {
    state.editor_navigation_confirmation_active = false;
    state.editor_home_return_pending = false;
    state.editor_data_navigation_pending = false;
    if (!accepted) state.editor_data_directory.reset();
    if (!state.hosted_editor || !state.hosted_editor->running()) return;
    if (!accepted) {
        state.hosted_editor->set_input_blocked(false);
        return;
    }
    try {
        if (home_route) process_editor_home_return(state, true);
        else process_editor_data_navigation(state, true);
    } catch (const std::exception& error) {
        state.hosted_editor->set_input_blocked(false);
        state.shell->set_status(
            std::string(home_route ? "ホームに戻る前" : "データ画面へ移る前") +
            "の確認に失敗しました: " + error.what());
    } catch (...) {
        state.hosted_editor->set_input_blocked(false);
        state.shell->set_status(home_route
            ? "ホームに戻る前の確認に失敗しました。"
            : "データ画面へ移る前の確認に失敗しました。");
    }
}

void process_pending_editor_navigation(RuntimeState& state) {
    using cardwirth::app::DeferredEditorRouteAction;
    if (!state.editor_home_return_pending && !state.editor_data_navigation_pending) return;
    if (state.editor_navigation_confirmation_active) return;

    const bool editor_page_active = state.shell &&
        state.shell->page() == cardwirth::app::Page::XEditor;
    const bool editor_running = state.hosted_editor && state.hosted_editor->running();
    const bool save_in_progress = editor_running &&
        state.hosted_editor->save_in_progress();
    const bool host_close_idle =
        state.host_close_phase == RuntimeState::HostClosePhase::Idle &&
        !state.editor_home_confirmation_active;
    const auto action = cardwirth::app::deferred_editor_route_action(
        true, editor_page_active, editor_running, save_in_progress, host_close_idle);

    if (action == DeferredEditorRouteAction::Ignore) {
        state.editor_home_return_pending = false;
        state.editor_data_navigation_pending = false;
        return;
    }
    if (action == DeferredEditorRouteAction::Wait) {
        if (save_in_progress) {
            state.hosted_editor->set_input_blocked(true);
            state.shell->set_status("保存処理の完了を待っています…");
        }
        return;
    }

    if (state.editor_home_return_pending) {
        // 外側のホーム確認でEditorの入力を遮断していたため、再び有効にする。 Editor
        // の非同期モードレスダイアログへSDLイベントを届けられるようにする。
        state.hosted_editor->set_input_blocked(false);
        const std::weak_ptr<std::atomic_bool> lifetime = state.callback_lifetime;
        state.editor_navigation_confirmation_active = true;
        bool async_started{};
        bool async_failed{};
        try {
            async_started = state.hosted_editor->prepare_stop_async(true,
                [&state, lifetime](const bool accepted) {
                    const auto alive = lifetime.lock();
                    if (!alive || !alive->load(std::memory_order_acquire)) return;
                    complete_editor_navigation_confirmation(state, true, accepted);
                });
        } catch (const std::exception& error) {
            async_failed = true;
            state.editor_navigation_confirmation_active = false;
            if (state.hosted_editor) state.hosted_editor->set_input_blocked(false);
            state.shell->set_status(
                std::string("ホームに戻る前の確認に失敗しました: ") + error.what());
        } catch (...) {
            async_failed = true;
            state.editor_navigation_confirmation_active = false;
            if (state.hosted_editor) state.hosted_editor->set_input_blocked(false);
            state.shell->set_status("ホームに戻る前の確認に失敗しました。");
        }
        if (async_started) return;
        if (async_failed) {
            state.editor_home_return_pending = false;
            state.editor_data_navigation_pending = false;
            return;
        }
        state.editor_navigation_confirmation_active = false;
        // ネイティブのダイアログ実装が非同期APIを持たない場合
        // に備え、 従来の同期的な確認処理を代替経路として残す。
        try {
            process_editor_home_return(state);
        } catch (const std::exception& error) {
            if (state.hosted_editor) state.hosted_editor->set_input_blocked(false);
            state.shell->set_status(
                std::string("ホームに戻る前の確認に失敗しました: ") + error.what());
        } catch (...) {
            if (state.hosted_editor) state.hosted_editor->set_input_blocked(false);
            state.shell->set_status("ホームに戻る前の確認に失敗しました。");
        }
        state.editor_home_return_pending = false;
        state.editor_data_navigation_pending = false;
        return;
    }

    state.hosted_editor->set_input_blocked(false);
    const std::weak_ptr<std::atomic_bool> lifetime = state.callback_lifetime;
    state.editor_navigation_confirmation_active = true;
    bool async_started{};
    bool async_failed{};
    try {
        async_started = state.hosted_editor->prepare_external_navigation_async(
            [&state, lifetime](const bool accepted) {
                const auto alive = lifetime.lock();
                if (!alive || !alive->load(std::memory_order_acquire)) return;
                complete_editor_navigation_confirmation(state, false, accepted);
            });
    } catch (const std::exception& error) {
        async_failed = true;
        state.editor_navigation_confirmation_active = false;
        if (state.hosted_editor) state.hosted_editor->set_input_blocked(false);
        state.shell->set_status(
            std::string("データ画面へ移る前の確認に失敗しました: ") + error.what());
    } catch (...) {
        async_failed = true;
        state.editor_navigation_confirmation_active = false;
        if (state.hosted_editor) state.hosted_editor->set_input_blocked(false);
        state.shell->set_status("データ画面へ移る前の確認に失敗しました。");
    }
    if (async_started) return;
    if (async_failed) {
        state.editor_home_return_pending = false;
        state.editor_data_navigation_pending = false;
        return;
    }
    state.editor_navigation_confirmation_active = false;
    // 非同期処理に未対応のダイアログ実装向けに、同期的なネイティブ処理を残す。
    try {
        process_editor_data_navigation(state);
    } catch (const std::exception& error) {
        if (state.hosted_editor) state.hosted_editor->set_input_blocked(false);
        state.shell->set_status(
            std::string("データ画面へ移る前の確認に失敗しました: ") + error.what());
    } catch (...) {
        if (state.hosted_editor) state.hosted_editor->set_input_blocked(false);
        state.shell->set_status("データ画面へ移る前の確認に失敗しました。");
    }
    state.editor_data_navigation_pending = false;
}
#endif

std::optional<cardwirth::app::all::Point> hosted_dialog_point(
    const RuntimeState& state, const float host_x, const float host_y,
    const bool allow_outside = false) {
    if (!state.hosted_engine || state.all_layout_snapshot.dialogs.empty())
        return std::nullopt;
    const auto modal = state.hosted_engine->modal_snapshot();
    if (!modal || !modal->presentation_surface ||
        modal->presentation_surface->size.width <= 0 ||
        modal->presentation_surface->size.height <= 0)
        return std::nullopt;
    const auto& placed = state.all_layout_snapshot.dialogs.front().rect;
    if (placed.width <= 0.0F || placed.height <= 0.0F ||
        (!allow_outside && (host_x < placed.x || host_y < placed.y ||
        host_x > placed.right() || host_y > placed.bottom())))
        return std::nullopt;
    return cardwirth::app::all::Point{
        (host_x - placed.x) *
            static_cast<float>(modal->presentation_surface->size.width) / placed.width,
        (host_y - placed.y) *
            static_cast<float>(modal->presentation_surface->size.height) / placed.height};
}

void cancel_hosted_engine_pointer(RuntimeState& state, bool notify = true) {
    state.engine_dialog_cursor.clear();
    state.engine_pointer_capture.cancel();
    state.hosted_touch_game = false;
    state.hosted_touch_clicks = 1;
    state.finger_tracking = false;
    state.hosted_dialog_pointer.reset();
    state.pending_debugger_control.reset();
    state.pending_debugger_activate = false;
    state.pending_release_button.reset();
    if (notify && state.hosted_engine) {
        state.hosted_engine->debugger_pointer_cancel();
        state.hosted_engine->pointer_cancel();
    }
}

// ALL仕様（試験用のon/off。設定画面には出さず、ソース上で切り替える）:
// Engineのダイアログが開く/閉じる（別のダイアログに替わるのも含む）とき、
// タッチ・クリックが押されたままなら、それが全て離されるまで入力を受け付けない。
// 開く前の押下の続き（離す操作・移動）がダイアログのボタンに届くのを防ぐ。
constexpr bool kHostedDialogTransitionWaitsForRelease = true;

// ポインターイベントを破棄する場合はtrueを返す。
bool hosted_dialog_release_gate(RuntimeState& state, const SDL_Event& event) {
    const auto modal = state.hosted_engine->modal_snapshot();
    const std::uint64_t modal_id = modal ? modal->model_id : 0;
    if (modal_id != state.release_gate_modal) {
        state.release_gate_modal = modal_id;
        if (kHostedDialogTransitionWaitsForRelease &&
            (state.release_gate_mouse_down || !state.release_gate_fingers.empty())) {
            state.release_gate_blocked = true;
            cancel_hosted_engine_pointer(state);
        }
    }
    bool pointer_event = false;
    switch (event.type) {
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        pointer_event = true;
        // タッチから合成されたマウスイベントは、指の状態で追跡する。
        if (event.button.which != SDL_TOUCH_MOUSEID)
            state.release_gate_mouse_down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        break;
    case SDL_EVENT_FINGER_DOWN:
        pointer_event = true;
        if (std::find(state.release_gate_fingers.begin(), state.release_gate_fingers.end(),
                      event.tfinger.fingerID) == state.release_gate_fingers.end())
            state.release_gate_fingers.push_back(event.tfinger.fingerID);
        break;
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED:
        pointer_event = true;
        state.release_gate_fingers.erase(std::remove(state.release_gate_fingers.begin(),
            state.release_gate_fingers.end(), event.tfinger.fingerID),
            state.release_gate_fingers.end());
        break;
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_WHEEL:
    case SDL_EVENT_FINGER_MOTION:
        pointer_event = true;
        break;
    default:
        break;
    }
    if (!state.release_gate_blocked || !pointer_event)
        return false;
    // 最後の解放も元の押下操作の一部なので破棄
    // し、 次の押下から通常どおり受け付ける。
    if (!state.release_gate_mouse_down && state.release_gate_fingers.empty())
        state.release_gate_blocked = false;
    return true;
}

void relay_hosted_dialog_action(RuntimeState& state,
    const cardwirth::app::HostedEngineDialogAction action,const int index) {
    if (!state.hosted_engine) return;
    const auto mods = SDL_GetModState();
    state.hosted_engine->dialog_action(
        action, index, ::cw::lib::engine_input_modifiers_from_sdl(mods));
}

auto hosted_dialog_pointer_relay(RuntimeState& state) {
        return [&state](const cardwirth::app::HostedEngineDialogControlSnapshot& control,
                    float x,float y,bool pressed) {
        if(state.hosted_engine)
            state.hosted_engine->dialog_pointer(
                control.action,control.index,x,y,pressed,
                ::cw::lib::engine_input_modifiers_from_sdl(SDL_GetModState()));
    };
}

void cancel_hosted_dialog_pointer(RuntimeState& state) {
    if (state.hosted_dialog_pointer.active() && state.hosted_engine)
        state.hosted_engine->pointer_cancel();
    state.hosted_dialog_pointer.reset();
}

bool begin_hosted_dialog_pointer(RuntimeState& state,float x,float y) {
    const auto modal=state.hosted_engine?state.hosted_engine->modal_snapshot():std::nullopt;
    const auto point=hosted_dialog_point(state,x,y);
    if(!modal || !point) { cancel_hosted_dialog_pointer(state); return false; }
    state.hosted_dialog_pointer.down(modal->presentation_controls,point->x,point->y,
                                     hosted_dialog_pointer_relay(state));
    return state.hosted_dialog_pointer.active();
}

bool move_hosted_dialog_pointer(RuntimeState& state,float x,float y,bool pressed) {
    const auto point=hosted_dialog_point(state,x,y,true);
    if(!point) { cancel_hosted_dialog_pointer(state); return false; }
    return state.hosted_dialog_pointer.move(point->x,point->y,pressed,
                                            hosted_dialog_pointer_relay(state));
}

void end_hosted_dialog_pointer(RuntimeState& state,float x,float y,int clicks) {
    const auto modal=state.hosted_engine?state.hosted_engine->modal_snapshot():std::nullopt;
    const auto point=hosted_dialog_point(state,x,y,true);
    if(!modal || !point) { cancel_hosted_dialog_pointer(state); return; }
    state.hosted_dialog_pointer.up(modal->presentation_controls,point->x,point->y,clicks,
        hosted_dialog_pointer_relay(state),
        [&state](const auto action,const int index) {
            relay_hosted_dialog_action(state,action,index);
        });
}

struct HostedDebuggerButton {
    cardwirth::app::HostedDebuggerControlSnapshot control{};
    std::string label{};
    std::string tooltip{};
    SDL_FRect rect{};
    bool enabled{true};
};

std::optional<cardwirth::app::all::CoordinateTransform> hosted_debugger_transform(
    const RuntimeState& state) {
    if (!state.hosted_engine || !state.all_layout_snapshot.engine_debug ||
        !state.all_layout_snapshot.engine_debug->debugger_visible)
        return std::nullopt;
    const auto& dock = state.all_layout_snapshot.engine_debug->debugger_rect;
    const auto debugger = state.hosted_engine
        ? state.hosted_engine->debugger_snapshot()
        : cardwirth::app::HostedDebuggerSnapshot{};
    if (!debugger.presentation_surface ||
        debugger.presentation_surface->size.width <= 0 ||
        debugger.presentation_surface->size.height <= 0) return std::nullopt;
    const cardwirth::app::all::Size source{
        static_cast<float>(debugger.presentation_surface->size.width),
        static_cast<float>(debugger.presentation_surface->size.height)};
    // ALLのデバッガー画像は624x880の固定サイズ。
    // 正方形に近いホストでは 表示枠の幅や高さが元画像より
    // 小さくなるため、等倍の中央配置では上下がはみ出す。
    // 共通画面の縦横比を保って均一に拡縮する。横幅はメイン
    // 領域の半分とし （compute_engine_debug_layout
    // ）、元画像より大きい倍率も許す。
    auto transform = cardwirth::app::all::uniform_fit_transform(dock, source, 0.0F);
    if (!transform.destination.valid()) return std::nullopt;
    // ALL仕様: Debugはメイン(Engine)に隙間なく隣接させる。Debug枠がEngineより
    // 広い場合(Engineが高さで縮む時は余りをDebug枠へ回している)、中央寄せ
    // だとEngineとの間に空白ができるため、Engine側の辺に揃える。余りは外側の
    // 画面端に回る。
    const auto& engine = state.all_layout_snapshot.engine_debug->engine_slot_rect;
    if (dock.x >= engine.right() - 0.5F)
        transform.destination.x = dock.x;
    else
        transform.destination.x = dock.right() - transform.destination.width;
    return transform;
}

std::optional<cardwirth::app::all::Rect> hosted_debugger_destination(const RuntimeState& state) {
    if (const auto transform = hosted_debugger_transform(state))
        return transform->destination;
    return std::nullopt;
}

std::optional<cardwirth::app::all::Point> hosted_debugger_point(
    const RuntimeState& state, float x, float y, bool allow_outside = false) {
    // Engineのモーダルダイアログは、独立したデバッガーを無効にしない。
    // 共通モーダルに実際に覆われた部分だけを、そのモーダルの入力対象とする。
    if (!allow_outside && hosted_dialog_point(state, x, y)) return std::nullopt;
    const auto transform = hosted_debugger_transform(state);
    if (!transform) return std::nullopt;
    const auto& destination = transform->destination;
    const auto& dock = state.all_layout_snapshot.engine_debug->debugger_rect;
    if (!allow_outside && (x < dock.x || x >= dock.right() || y < dock.y || y >= dock.bottom() ||
        x < destination.x || x >= destination.right() ||
        y < destination.y || y >= destination.bottom())) return std::nullopt;
    return transform->to_source_unclipped({x, y});
}

std::vector<HostedDebuggerButton> hosted_debugger_buttons(const RuntimeState& state) {
    std::vector<HostedDebuggerButton> result;
    const auto transform = hosted_debugger_transform(state);
    if (!transform) return result;
    const auto debugger = state.hosted_engine->debugger_snapshot();
    for (const auto& control : debugger.controls) {
        const auto rect = transform->to_host({control.x, control.y,
                                              control.width, control.height});
        result.push_back({control, control.label, control.tooltip,
            {rect.x, rect.y, rect.width, rect.height}, control.enabled});
    }
    return result;
}

std::optional<cardwirth::app::HostedDebuggerControlSnapshot> hosted_debugger_control_at(
    const RuntimeState& state, float x, float y) {
    if (!hosted_debugger_point(state, x, y)) return std::nullopt;
    for (const auto& button : hosted_debugger_buttons(state))
        if (button.enabled && contains(button.rect, x, y)) return button.control;
    return std::nullopt;
}

std::uint32_t hosted_modifiers(SDL_Keymod modifiers) noexcept;
void invoke_hosted_debugger_pointer(RuntimeState& state,
    const cardwirth::app::HostedDebuggerControlSnapshot& control,float x,float y,bool pressed) {
    if(const auto point=hosted_debugger_point(state,x,y,true))
        state.hosted_engine->debugger_pointer(control.action,control.index,point->x,point->y,pressed,
            hosted_modifiers(SDL_GetModState()));
    else state.hosted_engine->debugger_pointer_cancel();
}
void invoke_hosted_debugger_release(RuntimeState& state,
    const cardwirth::app::HostedDebuggerControlSnapshot& control,float x,float y,int clicks) {
    if(control.continuous_pointer) invoke_hosted_debugger_pointer(state,control,x,y,false);
    const auto released=hosted_debugger_control_at(state,x,y);
    const auto point=hosted_debugger_point(state,x,y,true);
    if(point) if(const auto action=::cw::dialog::dialog_release_action(control,
        released?&*released:nullptr,point->x,point->y,clicks))
        state.hosted_engine->debugger_action(*action,control.index);
}

void render_hosted_debugger_tooltip(RuntimeState& state) {
    if (!state.text_renderer || state.hosted_engine->modal_snapshot()) return;
    float window_x = 0.0F;
    float window_y = 0.0F;
    SDL_GetMouseState(&window_x, &window_y);
    float x = window_x;
    float y = window_y;
    SDL_RenderCoordinatesFromWindow(state.renderer, window_x, window_y, &x, &y);
    const auto buttons = hosted_debugger_buttons(state);
    const auto hovered = std::find_if(
        buttons.begin(), buttons.end(), [x, y](const auto& button) {
            return contains(button.rect, x, y) && !button.tooltip.empty();
        });
    if (hovered == buttons.end()) return;

    const auto& safe = state.all_layout_snapshot.safe_rect;
    state.text_renderer->draw_tooltip(
        hovered->tooltip, hovered->rect,
        {safe.x, safe.y, safe.width, safe.height}, 28.0F);
}

int hosted_native_key(SDL_Keycode key) noexcept;

#if defined(CARDWIRTH_ALL_HOST)
// ステータスダイアログの操作は解放時に確定する。CancelBacklog
// はホスト専用で、 キーボードと同じEscの押下として送ってログを
// 閉じる。 タップ中の移動許容値はALLの論理座標へ換算し、表示倍
// 率に応じて約24dp（約4mm）とする。 従来の10pxを下回
// らず、この範囲内の指の揺れはスワイプやスクロールとみなさない。
float hosted_touch_slop(const RuntimeState& state) {
    int width = 0, height = 0;
    SDL_GetWindowSize(state.window, &width, &height);
    const float display_scale = std::max(1.0F, SDL_GetWindowDisplayScale(state.window));
    const float logical_per_point = width > 0
        ? state.all_layout_snapshot.host_rect.width / static_cast<float>(width) : 1.0F;
    return std::max(10.0F, 24.0F * display_scale * logical_per_point);
}

void send_hosted_status_action(RuntimeState& state,
                               const cardwirth::app::HostedStatusAction action) {
    state.status_pressed_flash = action;
    state.status_pressed_flash_until = SDL_GetTicks() + 180;
    if (action == cardwirth::app::HostedStatusAction::OpenData) {
        state.engine_data_navigation_pending = true;
        return;
    }
    if (action == cardwirth::app::HostedStatusAction::CancelBacklog) {
        const int escape = hosted_native_key(SDLK_ESCAPE);
        state.hosted_engine->key(escape, true);
        state.hosted_engine->key(escape, false);
        return;
    }
    state.hosted_engine->status_action(action);
}
#endif

int hosted_native_key(SDL_Keycode key) noexcept {
    return static_cast<int>(::cw::lib::engine_game_key_from_sdl(key));
}

std::optional<cardwirth::app::HostedEngineDialogKey> hosted_debugger_key(
    const SDL_Keycode key) noexcept {
    return ::cw::dialog::presentation_key_from_sdl(key);
}

std::uint32_t hosted_modifiers(SDL_Keymod modifiers) noexcept {
    return ::cw::lib::engine_input_modifiers_from_sdl(modifiers);
}
#endif

void set_label(RuntimeState& state, const std::string& text, cardwirth::ui::Rect rect,
               SDL_Color color = SDL_Color{32, 32, 32, 255}, bool modal = false) {
    auto label = state.backend->create_label(text);
    label->set_rect(rect);
    state.labels.push_back(LabelEntry{std::move(label), color, modal});
}

void add_button(RuntimeState& state, const std::string& text, cardwirth::ui::Rect rect,
                 std::function<void()> callback,
                 ButtonEntry::Visual visual = ButtonEntry::Visual::Default,
                  bool enabled = true, bool selected = false, bool modal = false,
                  std::optional<std::size_t> explorer_row = std::nullopt,
                  bool activate_on_release = false,
                  std::optional<std::size_t> home_card_art = std::nullopt) {
    auto button = state.backend->create_button(text);
    button->set_rect(rect);
    button->set_enabled(enabled);
    button->on_click(std::move(callback));
    state.buttons.push_back(ButtonEntry{
        std::move(button), rect, visual, selected, modal, explorer_row,
        activate_on_release, home_card_art});
}

std::filesystem::path find_engine(const std::filesystem::path& base,
                                  const std::string& requested) {
    if (!requested.empty()) {
        const std::filesystem::path candidate(requested);
        if (std::filesystem::is_regular_file(candidate)) return candidate;
    }
    const std::array<std::string, 2> names{
#ifdef _WIN32
        "CardWirthPy_C.exe", "CardWirthPy_C_debug.exe"
#else
        "CardWirthPy_C", "CardWirthPy_C_debug"
#endif
    };
    for (const auto& name : names) {
        const auto candidate = base / name;
        if (std::filesystem::is_regular_file(candidate)) return candidate;
    }
    return {};
}

void configure_page(RuntimeState& state);
#if defined(CARDWIRTH_ALL_HOST)
constexpr std::string_view kAllHomeSettingsFile = "Config/all_home.settings";

bool web_host_is_ios() {
#if defined(__EMSCRIPTEN__)
    // ここではプラットフォームの判定だけを行い、設定の初期値と保存は共通処理にする。
    return MAIN_THREAD_EM_ASM_INT({
        return (/iPad|iPhone|iPod/.test(navigator.userAgent) ||
            (navigator.platform === 'MacIntel' && navigator.maxTouchPoints > 1)) ? 1 : 0;
    }) != 0;
#else
    return false;
#endif
}

bool web_phone_needs_landscape() {
#if defined(__EMSCRIPTEN__)
    // ここではブラウザーの基本操作だけを提供し、遷移と待機の方針はホストで管理する。
    return MAIN_THREAD_EM_ASM_INT({
        var mobile = (navigator.userAgentData && navigator.userAgentData.mobile) ||
            /iPhone|iPod|Android.*Mobile/.test(navigator.userAgent);
        if (!mobile) return 0;
        var type = screen.orientation && screen.orientation.type;
        if (type) return type.indexOf('landscape') === 0 ? 0 : 1;
        if (typeof window.orientation === 'number')
            return Math.abs(window.orientation) === 90 ? 0 : 1;
        return screen.width > screen.height ? 0 : 1;
    }) != 0;
#else
    return false;
#endif
}

void request_home_landscape_navigation(RuntimeState& state,
                                       std::function<void()> action) {
    if (!web_phone_needs_landscape()) {
        action();
        return;
    }
    state.home_landscape_action = std::move(action);
    configure_page(state);
}

void load_all_home_settings(RuntimeState& state) {
    using namespace cardwirth::app::all;
    state.editor_logical_height = kAllEditorDefaultHeight;
    // iOSとWASM（インストール済みPWAを含む）の初期設定。
    const bool ios_web = web_host_is_ios();
    state.editor_left_side_space = ios_web ? 40 : 0;
    state.editor_right_side_space = ios_web ? 40 : 0;
    state.editor_top_side_space = ios_web ? 20 : 0;
    state.editor_bottom_side_space = ios_web ? 20 : 0;
    if (!state.runtime_context) return;
    const auto bytes = state.runtime_context->user_storage_ref().read(
        std::filesystem::path(kAllHomeSettingsFile));
    if (!bytes || bytes->empty()) return;
    const std::string value(reinterpret_cast<const char*>(bytes->data()), bytes->size());
    const auto read_integer = [&value](const std::string_view key, int& target) {
        const auto pos = value.find(key);
        if (pos == std::string::npos) return false;
        const auto first = pos + key.size();
        const auto last_pos = value.find_first_of("\r\n", first);
        const auto token = std::string_view(value).substr(
            first, last_pos == std::string::npos ? value.size() - first
                                                  : last_pos - first);
        int parsed{};
        const auto result = std::from_chars(token.data(), token.data() + token.size(),
                                            parsed);
        if (result.ec == std::errc{} && result.ptr == token.data() + token.size()) {
            target = parsed;
            return true;
        }
        return false;
    };
    read_integer("editor_height=", state.editor_logical_height);
    int legacy_side_space{};
    if (read_integer("editor_side_space=", legacy_side_space)) {
        state.editor_left_side_space = legacy_side_space;
        state.editor_right_side_space = legacy_side_space;
    }
    read_integer("editor_left_side_space=", state.editor_left_side_space);
    read_integer("editor_right_side_space=", state.editor_right_side_space);
    read_integer("editor_top_side_space=", state.editor_top_side_space);
    read_integer("editor_bottom_side_space=", state.editor_bottom_side_space);
    int install_prompt_declined{};
    read_integer("install_prompt_declined=", install_prompt_declined);
    state.home_install_prompt_declined = install_prompt_declined != 0;
    state.editor_logical_height = clamp_editor_height(state.editor_logical_height);
    state.editor_left_side_space = clamp_editor_side_space(state.editor_left_side_space);
    state.editor_right_side_space = clamp_editor_side_space(state.editor_right_side_space);
    state.editor_top_side_space = clamp_editor_side_space(state.editor_top_side_space);
    state.editor_bottom_side_space = clamp_editor_side_space(state.editor_bottom_side_space);
}

bool save_all_home_settings(RuntimeState& state) {
    using namespace cardwirth::app::all;
    if (!state.runtime_context) return false;
    state.editor_logical_height = clamp_editor_height(state.editor_logical_height);
    state.editor_left_side_space = clamp_editor_side_space(state.editor_left_side_space);
    state.editor_right_side_space = clamp_editor_side_space(state.editor_right_side_space);
    state.editor_top_side_space = clamp_editor_side_space(state.editor_top_side_space);
    state.editor_bottom_side_space = clamp_editor_side_space(state.editor_bottom_side_space);
    const auto value = "editor_height=" +
        std::to_string(state.editor_logical_height) +
        "\neditor_left_side_space=" +
        std::to_string(state.editor_left_side_space) +
        "\neditor_right_side_space=" +
        std::to_string(state.editor_right_side_space) +
        "\neditor_top_side_space=" + std::to_string(state.editor_top_side_space) +
        "\neditor_bottom_side_space=" + std::to_string(state.editor_bottom_side_space) +
        "\ninstall_prompt_declined=" +
        std::to_string(state.home_install_prompt_declined ? 1 : 0) + "\n";
    return state.runtime_context->user_storage_ref().write_atomic(
        std::filesystem::path(kAllHomeSettingsFile),
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(value.data()), value.size()), true);
}

void set_editor_home_height(RuntimeState& state, const int height) {
    state.editor_logical_height = cardwirth::app::all::clamp_editor_height(height);
    state.home_other_status = save_all_home_settings(state)
        ? "エディタの縦サイズを保存しました。次回Editor起動時に反映します。"
        : "エディタの縦サイズを保存できませんでした。";
    configure_page(state);
}

enum class EditorMarginSide { Left, Top, Right, Bottom };

int& editor_side_space(RuntimeState& state, const EditorMarginSide side) {
    switch (side) {
    case EditorMarginSide::Left: return state.editor_left_side_space;
    case EditorMarginSide::Top: return state.editor_top_side_space;
    case EditorMarginSide::Right: return state.editor_right_side_space;
    case EditorMarginSide::Bottom: return state.editor_bottom_side_space;
    }
    return state.editor_left_side_space;
}

const char* editor_side_name(const EditorMarginSide side) {
    switch (side) {
    case EditorMarginSide::Left: return "左";
    case EditorMarginSide::Top: return "上";
    case EditorMarginSide::Right: return "右";
    case EditorMarginSide::Bottom: return "下";
    }
    return "";
}

void set_editor_home_side_space(RuntimeState& state, const EditorMarginSide side,
                                const int side_space) {
    auto& target = editor_side_space(state,side);
    target = cardwirth::app::all::clamp_editor_side_space(side_space);
    state.home_other_status = save_all_home_settings(state)
        ? std::string("エディタの") + editor_side_name(side) +
              "余白を保存しました。次回Editor起動時に反映します。"
        : std::string("エディタの") + editor_side_name(side) +
              "余白を保存できませんでした。";
    configure_page(state);
}

void start_home_package_install(RuntimeState& state, const char* group,
                                const char* label) {
    if (state.home_package_install_busy) return;
#if defined(__EMSCRIPTEN__)
    state.home_package_install_busy = true;
    state.home_package_progress_current = 0;
    state.home_package_progress_total = 0;
    state.home_other_status = std::string(label) + "をダウンロードしています…";
    MAIN_THREAD_EM_ASM({
        const group = UTF8ToString($0);
        const s = {};
        s.phase = 1;
        s.done = 0;
        s.total = 0;
        s.count = 0;
        s.error = "";
        globalThis.CardWirthAllPackageInstallState = s;
        const progress = text => {
            const parts = String(text || "").split(" ")[0].split("/");
            if (parts.length >= 2) {
                s.done = Number(parts[0]) || 0;
                s.total = Number(parts[1]) || 0;
            }
        };
        const fn = globalThis.CardWirthInstallAdditionalPackage;
        if (typeof fn !== "function") {
            s.error = "追加データのインストール機能を読み込めませんでした。";
            s.phase = 3;
            return;
        }
        // PROXY_TO_PTHREADでC++を動かす場合も、インストーラーとその状
        // 態はブラウザーのメインスレッドに属する。 同期的に発生したエラーも捕捉する。
        Promise.resolve().then(() => fn(group, progress)).then(count => {
            s.count = Number(count) || 0;
            s.phase = 2;
        }).catch(error => {
            console.error(error);
            s.error = String(error?.message || error || "不明なエラー").slice(0, 4096);
            s.phase = 3;
        });
    }, group);
#else
    if (!state.shell) return;
    state.home_package_install_busy = true;
    state.home_package_progress_current = 0;
    state.home_package_progress_total = 0;
    state.home_other_status = std::string(label) + "をダウンロードしています…";
    auto task = std::make_shared<NativePackageInstall>();
    state.home_package_install = task;
    const auto user_root = state.shell->user_data().root;
    const std::string requested_group(group);
    std::thread([task, user_root, requested_group] {
        try {
            task->count = cardwirth::app::install_additional_data(
                user_root, requested_group, [task](int done, int total) {
                    task->done.store(done, std::memory_order_relaxed);
                    task->total.store(total, std::memory_order_relaxed);
                });
            task->phase.store(2, std::memory_order_release);
        } catch (const std::exception& error) {
            task->error = error.what();
            task->phase.store(3, std::memory_order_release);
        }
    }).detach();
#endif
    configure_page(state);
}

void finish_home_install_prompt(RuntimeState& state, bool accepted) {
    if (!state.home_install_prompt_active) return;
    state.home_install_prompt_active = false;
    if (accepted) {
        state.home_other_open = false;
        state.home_initial_install_active = true;
        // 初回導入では基本データと追加顔絵をまとめて導入する。
        // Scenarioが初めに空なら、基本パッケージ後も追加シナリオを導入する。
        state.home_initial_install_remaining = {"faces"};
        std::error_code scenario_error;
        const auto& scenario = state.shell->user_data().scenario;
        if (!std::filesystem::is_directory(scenario, scenario_error) ||
            std::filesystem::is_empty(scenario, scenario_error))
            state.home_initial_install_remaining.emplace_back("scenarios");
        start_home_package_install(state, "basic", "基本データ");
    } else {
        state.home_install_prompt_declined = true;
        if (!save_all_home_settings(state)) {
            state.home_other_open = true;
            state.home_other_status = "インストール確認を表示しない設定を保存できませんでした。";
        }
        configure_page(state);
    }
}

void finish_home_initial_install(RuntimeState& state) {
    if (!state.home_initial_install_active || state.home_package_install_busy || !state.shell) return;
    state.home_initial_install_active = false;
    state.home_initial_install_remaining.clear();
    state.home_other_open = false;
    state.shell->set_initial_page(cardwirth::app::Page::Home);
    configure_page(state);
}

void poll_home_package_install(RuntimeState& state) {
    if (!state.home_package_install_busy) return;
    int phase{}, done{}, total{}, count{};
    std::string error;
#if defined(__EMSCRIPTEN__)
    int values[4]{};
    // ワーカーのグローバル変数を監視したり、毎フレーム4回の同期呼び出しを行った
    // りせず、 ブラウザーのメインスレッドから一つのスナップショットを取得する。
    auto* error_text = static_cast<char*>(MAIN_THREAD_EM_ASM_PTR({
        const s = globalThis.CardWirthAllPackageInstallState;
        const offset = $0 >>> 2;
        HEAP32[offset] = s?.phase || 0;
        HEAP32[offset + 1] = s?.done || 0;
        HEAP32[offset + 2] = s?.total || 0;
        HEAP32[offset + 3] = s?.count || 0;
        if (s?.phase === 2 || s?.phase === 3) {
            globalThis.CardWirthAllPackageInstallState = null;
            return s.error ? stringToNewUTF8(s.error) : 0;
        }
        return 0;
    }, values));
    phase = values[0]; done = values[1]; total = values[2]; count = values[3];
    if (error_text) { error = error_text; std::free(error_text); }
#else
    if (!state.home_package_install) return;
    auto task = state.home_package_install;
    phase = task->phase.load(std::memory_order_acquire);
    done = task->done.load(std::memory_order_relaxed);
    total = task->total.load(std::memory_order_relaxed);
    if (phase == 2 || phase == 3) {
        count = task->count;
        error = task->error;
        state.home_package_install.reset();
    }
#endif
    // ダウンロードの基本操作は環境ごとに異なるが、 進捗・完
    // 了・ホームへの遷移はすべてのALLで共通の規則を使う。
    if (phase == 1) {
        if (done == state.home_package_progress_current &&
            total == state.home_package_progress_total) return;
        state.home_package_progress_current = done;
        state.home_package_progress_total = total;
        state.home_other_status = total > 0
            ? "データをダウンロードしています " +
                std::to_string(done) + "/" + std::to_string(total)
            : "データをダウンロードしています…";
    } else if (phase == 2 || phase == 3) {
        state.home_package_install_busy = false;
        if (state.home_initial_install_active && phase == 2 &&
            !state.home_initial_install_remaining.empty()) {
            const auto next = state.home_initial_install_remaining.front();
            state.home_initial_install_remaining.erase(state.home_initial_install_remaining.begin());
            start_home_package_install(state, next.c_str(),
                next == "faces" ? "追加顔絵" : "追加シナリオ");
            return;
        }
        if (phase == 3) state.home_initial_install_remaining.clear();
        if (state.home_initial_install_active) {
            state.home_other_status = phase == 2 ? "インストールが完了しました。"
                : "インストールに失敗しました。";
        } else state.home_other_status = phase == 2
            ? std::to_string(count) +
                "ファイルを保存しました。既存の他のファイルは保持しています。"
            : "追加データのダウンロードに失敗しました。";
        if (phase == 3 && !error.empty()) state.home_other_status += " " + error;
    } else return;
    if (state.shell && state.shell->page() == cardwirth::app::Page::Home &&
        (state.home_other_open || state.home_initial_install_active)) configure_page(state);
}

bool pump_editor_modal(RuntimeState& state);
#endif

#if defined(__EMSCRIPTEN__) && defined(CARDWIRTH_ALL_HOST)
// Web版ALLのEditorでは、ブラウザーの戻るボタンをEditor
// のホームボタンとして扱う。 Editorの実行中は履歴の先頭に保護用の
// 項目を一つ置き、戻る操作で外れたら ページを離れずにwasmメモリの
// フラグを立て、保護項目を積み直す。 履歴とwindowはブラウザーの
// メインスレッドにしかないため、リスナーはそこで登録する。 フラグは共
// 有される場合もあるwasmメモリへ置き、アプリスレッドから取得する。
std::atomic<std::int32_t> g_web_editor_back_requested{0};

void set_web_editor_back_guard(RuntimeState& state, bool armed) {
    if (state.web_editor_back_guard_armed == armed) return;
    state.web_editor_back_guard_armed = armed;
    g_web_editor_back_requested.store(0);
    // 2GiBより上のwasm32アドレスには、符号なしのHEAP添字を使
    // う。 EM_ASM内では//コメントや括弧の外のカンマを使わない。
    // カンマはマクロ引数の区切りになる。 popstateハンドラーは保
    // 護用の履歴を復元してから、wasmメモリ経由で戻る操作を通知する。
    MAIN_THREAD_EM_ASM({
        const flag = $0;
        const armed = !!$1;
        const g = globalThis;
        if (!g.CardWirthAllEditorBack) {
            g.CardWirthAllEditorBack = {};
            g.CardWirthAllEditorBack.armed = false;
            g.CardWirthAllEditorBack.flag = flag;
            g.addEventListener("popstate", () => {
                const s = g.CardWirthAllEditorBack;
                if (!s.armed) return;
                try { history.pushState({ cardwirthAllEditorGuard: true }, ""); } catch (e) {}
                Atomics.store(HEAP32, s.flag >>> 2, 1);
            });
        }
        const s = g.CardWirthAllEditorBack;
        s.flag = flag;
        if (armed && !s.armed) {
            const current = history.state;
            if (!(current && current.cardwirthAllEditorGuard)) {
                try { history.pushState({ cardwirthAllEditorGuard: true }, ""); } catch (e) {}
            }
        }
        s.armed = armed;
    }, reinterpret_cast<std::intptr_t>(&g_web_editor_back_requested), armed ? 1 : 0);
}

// Editorの入れ子のモーダルループ中はallow_homeをfalseにする。
// モーダル表示中はホームボタンを押せないため、ブラウザーの戻る操作も破棄する。
void poll_web_editor_back(RuntimeState& state, bool allow_home) {
    const bool active = state.shell && state.hosted_editor &&
        state.hosted_editor->running() &&
        state.shell->page() == cardwirth::app::Page::XEditor;
    set_web_editor_back_guard(state, active);
    if (g_web_editor_back_requested.exchange(0) == 0) return;
    if (!active || !allow_home || !state.editor_request_home) return;
    state.editor_request_home();
}
#endif

SDL_Rect native_text_input_area(RuntimeState& state, const SDL_FRect logical,
                                const bool legacy_page_coordinates = false) {
    float left = logical.x;
    float top = logical.y;
    float right = logical.x + logical.w;
    float bottom = logical.y + logical.h;
    SDL_Rect previous_viewport{};
    float previous_scale_x = 1.0F;
    float previous_scale_y = 1.0F;
    bool restore_transform = false;
#if defined(CARDWIRTH_ALL_HOST)
    if (legacy_page_coordinates && state.renderer) {
        // ホームとData ExplorerはALLの従来型ページの表示
        // 領域で描画するが、 IMEの候補ウィンドウにはネイティブウィ
        // ンドウ座標が必要。描画・ポインター入力と同じ 順方向の変換を
        // 使い、高DPI・余白付き表示・モバイルの各配置を一致させる。
        const auto& transform = state.all_layout_snapshot.host_to_render;
        SDL_GetRenderViewport(state.renderer, &previous_viewport);
        SDL_GetRenderScale(state.renderer, &previous_scale_x, &previous_scale_y);
        const SDL_Rect viewport{
            static_cast<int>(std::lround(transform.destination.x / transform.scale_x)),
            static_cast<int>(std::lround(transform.destination.y / transform.scale_y)),
            static_cast<int>(std::lround(transform.destination.width / transform.scale_x)),
            static_cast<int>(std::lround(transform.destination.height / transform.scale_y))};
        SDL_SetRenderViewport(state.renderer, &viewport);
        SDL_SetRenderScale(state.renderer, transform.scale_x, transform.scale_y);
        restore_transform = true;
    }
#else
    (void)legacy_page_coordinates;
#endif
    const bool mapped = state.renderer &&
        SDL_RenderCoordinatesToWindow(state.renderer, logical.x, logical.y, &left, &top) &&
        SDL_RenderCoordinatesToWindow(state.renderer, logical.x + logical.w,
                                      logical.y + logical.h, &right, &bottom);
    if (restore_transform) {
        SDL_SetRenderScale(state.renderer, previous_scale_x, previous_scale_y);
        SDL_SetRenderViewport(state.renderer, &previous_viewport);
    }
    if (!mapped) {
        left = logical.x;
        top = logical.y;
        right = logical.x + logical.w;
        bottom = logical.y + logical.h;
    }
    return {static_cast<int>(std::lround(left)), static_cast<int>(std::lround(top)),
            std::max(1, static_cast<int>(std::lround(right - left))),
            std::max(1, static_cast<int>(std::lround(bottom - top)))};
}

void update_text_input(RuntimeState& state) {
#if defined(CARDWIRTH_ALL_HOST)
    // 埋め込みEditorは、フォーカス中の部品のIME矩形を自分で公開する。 ホ
    // ストページの更新で、その文字入力セッションを停止したり位置を変えたりしない。
    if (state.shell && state.shell->page() == cardwirth::app::Page::XEditor &&
        state.hosted_editor && state.hosted_editor->running())
        return;
#endif
    if (state.explorer_modal.wants_text_input()) {
        if (!SDL_TextInputActive(state.window)) SDL_StartTextInput(state.window);
        const SDL_FRect logical = state.explorer_modal.kind() ==
                cardwirth::explorer::ExplorerModalKind::TextEditor
            ? (state.explorer_modal.search_active() ? kTextEditorSearchBox : kTextEditorBox)
            : SDL_FRect{290, 280, 380, 44};
        const SDL_Rect area = native_text_input_area(state, logical, true);
        SDL_SetTextInputArea(state.window, &area, 0);
    }
#if defined(CARDWIRTH_ALL_HOST)
    else if (state.hosted_engine && state.shell->page() == cardwirth::app::Page::CardWirthPy) {
        const auto modal = state.hosted_engine->modal_snapshot();
        if (modal && ::cw::dialog::dialog_text_input_enabled(modal->wants_text_input,
                modal->text_input_hardware_only, SDL_HasKeyboard())) {
            if (!SDL_TextInputActive(state.window)) SDL_StartTextInput(state.window);
            const auto area_rect = state.all_layout_snapshot.dialogs.empty()
                ? cardwirth::app::all::Rect{}
                : state.all_layout_snapshot.dialogs.front().rect;
            const SDL_Rect area = native_text_input_area(
                state, SDL_FRect{area_rect.x, area_rect.y,
                                 area_rect.width, area_rect.height});
            SDL_SetTextInputArea(state.window, &area, 0);
        } else if(!modal && state.hosted_debugger_focused &&
                  state.hosted_engine->debugger_snapshot().visible &&
                  state.hosted_engine->debugger_snapshot().wants_text_input) {
            if(!SDL_TextInputActive(state.window)) SDL_StartTextInput(state.window);
            if(state.all_layout_snapshot.engine_debug) {
                const auto& rect=state.all_layout_snapshot.engine_debug->debugger_rect;
                const SDL_Rect area=native_text_input_area(
                    state,SDL_FRect{rect.x,rect.y,rect.width,rect.height});
                SDL_SetTextInputArea(state.window,&area,0);
            }
        } else {
            SDL_StopTextInput(state.window);
        }
    }
#endif
    else {
        SDL_StopTextInput(state.window);
    }
}

void activate_selected_explorer_item(RuntimeState& state) {
    if (!state.explorer) return;
    // 結果はExplorerが所有し、ホストの更新処理で受け取る。 保留中の読み込みは、RuntimeState
    // やExplorerComponentを呼び戻すコールバックを保持しない。
    state.explorer->open_selected_async(true);
    configure_page(state);
}

void begin_explorer_extraction(RuntimeState& state) {
    if (!state.explorer || state.explorer_extraction_pending ||
        state.explorer_modal.kind() ==
            cardwirth::explorer::ExplorerModalKind::ExtractProgress)
        return;
    state.pending_release_button.reset();
    state.finger_tracking = false;
    state.finger_scrolled = false;
    state.finger_selecting = false;
    state.finger_start_row.reset();
    state.finger_last_selection_row.reset();
    state.explorer_modal_error.clear();
    state.explorer_extraction_pending = true;
    state.explorer_modal.show_extract_progress();
    configure_page(state);
}

void start_pending_explorer_extraction(RuntimeState& state) {
    if (!state.explorer_extraction_pending) return;
    if (!state.explorer || state.shell->page() != cardwirth::app::Page::DataExplorer) {
        state.explorer_extraction_pending = false;
        return;
    }
    state.explorer_extraction_pending = false;
    const auto lifetime = std::weak_ptr<std::atomic_bool>(state.callback_lifetime);
    auto* runtime = &state;
    (void)state.explorer->extract_selected_async(
        [lifetime, runtime](cardwirth::explorer::OperationResult result) mutable {
            const auto alive = lifetime.lock();
            if (!alive || !alive->load(std::memory_order_acquire)) return;
            if (result.ok && runtime->explorer &&
                runtime->explorer->controller().has_flatten_candidate()) {
                runtime->explorer_modal.show_flatten_extraction(
                    runtime->explorer->controller().flatten_candidate_name());
            } else {
                // エラーや完了結果はステータス行に残るため
                // 、 完了後は進捗のモーダル画面を閉じる。
                runtime->explorer_modal.dismiss();
                runtime->explorer_modal_error.clear();
            }
            if (runtime->shell->page() == cardwirth::app::Page::DataExplorer)
                configure_page(*runtime);
        });
}

void begin_explorer_paste(RuntimeState& state) {
    if (!state.explorer || state.explorer_modal.kind() ==
            cardwirth::explorer::ExplorerModalKind::ExtractProgress) return;
    const bool move = state.explorer->controller().move_pending();
    state.explorer_modal.show_extract_progress(move ? "移動・保存中" : "コピー・保存中");
    configure_page(state);
    const auto lifetime = std::weak_ptr<std::atomic_bool>(state.callback_lifetime);
    auto* runtime = &state;
    (void)state.explorer->paste_copy_async(
        [lifetime, runtime](cardwirth::explorer::OperationResult) {
            const auto alive = lifetime.lock();
            if (!alive || !alive->load(std::memory_order_acquire)) return;
            runtime->explorer_modal.dismiss();
            if (runtime->shell->page() == cardwirth::app::Page::DataExplorer)
                configure_page(*runtime);
        });
}

// Explorerのタブ名にはディレクトリ名を使い、ルートの場合は「ルート」とする。
std::string explorer_tab_title(const std::filesystem::path& directory,
                               const std::filesystem::path& trash) {
    if (directory.empty()) return "ルート";
    if (directory == trash) return "trash";
    const auto name = path_to_utf8(directory.filename());
    return name.empty() ? path_to_utf8(directory) : name;
}

// compact_button_width相当の推定幅がmax_widthに収まるよう、文字列を短縮する。
std::string fit_explorer_text(const std::string& text, float max_width) {
    constexpr float ascii_width = 9.0F;
    constexpr float wide_width = 17.0F;
    float width = 0.0F;
    for (std::size_t offset = 0; offset < text.size();) {
        const auto first = static_cast<unsigned char>(text[offset]);
        std::size_t length = first < 0x80U ? 1U : first >= 0xF0U ? 4U : first >= 0xE0U ? 3U : 2U;
        const float glyph = first < 0x80U ? ascii_width : wide_width;
        if (width + glyph > max_width) return text.substr(0, offset) + "…";
        width += glyph;
        offset += length;
    }
    return text;
}

void commit_explorer_rename(RuntimeState& state) {
    if (!state.explorer) return;
    using cardwirth::explorer::ExplorerNameInputPurpose;
    const auto purpose = state.explorer_modal.name_input_purpose();
    const auto& input = state.explorer_modal.confirmation_input();
    const auto result = purpose == ExplorerNameInputPurpose::Rename
        ? state.explorer->rename_selected(input)
        : state.explorer->create_entry(input,
              purpose == ExplorerNameInputPurpose::NewFolder);
    if (result.ok) {
        state.explorer_modal.dismiss();
        state.explorer_modal_error.clear();
    } else {
        state.explorer_modal_error = result.message;
    }
}

void switch_explorer_tab(RuntimeState& state, std::size_t index) {
    if (!state.explorer || index >= state.explorer_tabs.size()) return;
    state.explorer_active_tab = index;
    state.explorer_tab_revision = state.explorer->controller().navigation_revision();
    state.explorer->navigate_to_async(state.explorer_tabs[index]);
}

// システムのクリップボードをExplorerのテキストエディターまたは検索欄へ貼り付
// ける。 内部ではLFに統一したテキストを扱うため、ここでCR/LFを正規化する。
void paste_into_explorer_editor(RuntimeState& state) {
    auto text = ::cw::lib::system_clipboard_text();
    std::string normalized;
    normalized.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == '\r') {
            normalized.push_back('\n');
            if (index + 1 < text.size() && text[index + 1] == '\n') ++index;
        } else {
            normalized.push_back(text[index]);
        }
    }
    if (!normalized.empty()) state.explorer_modal.insert(normalized);
}

void configure_explorer_modal(RuntimeState& state) {
    using cardwirth::explorer::ExplorerModalKind;
    const auto kind = state.explorer_modal.kind();
    if (kind == ExplorerModalKind::None) return;
    const auto modal_label = [&state](const std::string& text, cardwirth::ui::Rect rect,
                                     SDL_Color color = SDL_Color{42, 47, 54, 255}) {
        set_label(state, text, rect, color, true);
    };
    const auto modal_button = [&state](const std::string& text, cardwirth::ui::Rect rect,
                                      std::function<void()> callback,
                                      ButtonEntry::Visual visual = ButtonEntry::Visual::Toolbar,
                                      bool enabled = true) {
        rect.width = compact_button_width(text);
        add_button(state, text, rect, std::move(callback), visual, enabled, false, true);
    };

    if (kind == ExplorerModalKind::TabMenu) {
        // タブ選択欄の直下に、同じ100px幅のドロップダウン一覧を置く。
        constexpr float item_width = 100.0F;
        constexpr float item_height = 34.0F;
        const float x = kExplorerContentLeft + 14.0F;
        float y = 100.0F;
        const auto trash = state.explorer->controller().filesystem().trash();
        for (std::size_t index = 0; index < state.explorer_tabs.size(); ++index) {
            const bool active = index == state.explorer_active_tab;
            add_button(state,
                       fit_explorer_text(std::to_string(index + 1) + " " +
                           explorer_tab_title(state.explorer_tabs[index], trash),
                           item_width - 14.0F),
                       {x, y, item_width, item_height}, [&state, index] {
                           state.explorer_modal.dismiss();
                           if (index != state.explorer_active_tab) switch_explorer_tab(state, index);
                           configure_page(state);
                       }, active ? ButtonEntry::Visual::Primary : ButtonEntry::Visual::Toolbar,
                       true, active, true);
            y += item_height + 2.0F;
        }
        add_button(state, "＋追加", {x, y, item_width, item_height}, [&state] {
            state.explorer_modal.dismiss();
            state.explorer_tabs.push_back(std::filesystem::path{});
            switch_explorer_tab(state, state.explorer_tabs.size() - 1U);
            configure_page(state);
        }, ButtonEntry::Visual::Toolbar, true, false, true);
        y += item_height + 2.0F;
        add_button(state, "×閉じる", {x, y, item_width, item_height}, [&state] {
            state.explorer_modal.dismiss();
            if (state.explorer_tabs.size() > 1U) {
                state.explorer_tabs.erase(state.explorer_tabs.begin() +
                    static_cast<std::ptrdiff_t>(state.explorer_active_tab));
                switch_explorer_tab(state, std::min(state.explorer_active_tab,
                                                    state.explorer_tabs.size() - 1U));
            }
            configure_page(state);
        }, ButtonEntry::Visual::Toolbar, state.explorer_tabs.size() > 1U, false, true);
    } else if (kind == ExplorerModalKind::MoreActions) {
        const auto view = state.explorer->view_state();
        modal_label("その他の操作", {286, 206, 390, 28}, {29, 78, 137, 255});
        modal_button("名前の変更", {286, 256, 110, 42}, [&state] {
            const auto& explorer = state.explorer->controller();
            const auto index = explorer.selected_index();
            if (!index || *index >= explorer.entries().size()) return;
            state.explorer_modal_error.clear();
            state.explorer_modal.show_rename(explorer.entries()[*index].name);
            configure_page(state);
        }, ButtonEntry::Visual::Primary, view.can_rename);
        modal_button("新規ファイル", {410, 256, 110, 42}, [&state] {
            state.explorer_modal_error.clear();
            state.explorer_modal.show_new_entry(false, "新規ファイル.txt");
            configure_page(state);
        }, ButtonEntry::Visual::Primary, view.can_create);
        modal_button("新規フォルダ", {534, 256, 110, 42}, [&state] {
            state.explorer_modal_error.clear();
            state.explorer_modal.show_new_entry(true, "新規フォルダ");
            configure_page(state);
        }, ButtonEntry::Visual::Primary, view.can_create);
        // 移動はコピーと同様に対象を保持し、移動先フォルダーで貼り付けた時点で移す。
        modal_button("移動", {286, 302, 110, 42}, [&state] {
            const auto result = state.explorer->begin_move();
            if (result.ok) state.explorer_modal.show_move_paste_notice();
            state.explorer_modal_error.clear();
            configure_page(state);
        }, ButtonEntry::Visual::Primary, view.can_move);
        modal_button("閉じる", {510, 346, 110, 42}, [&state] {
            state.explorer_modal.dismiss();
            state.explorer_modal_error.clear();
            configure_page(state);
        });
    } else if (kind == ExplorerModalKind::MovePasteNotice) {
        modal_label("貼り付けで移動します。", {286, 230, 410, 28});
        modal_button("OK", {450, 314, 110, 42}, [&state] {
            state.explorer_modal.dismiss();
            configure_page(state);
        }, ButtonEntry::Visual::Primary);
    } else if (kind == ExplorerModalKind::RenameInput) {
        using cardwirth::explorer::ExplorerNameInputPurpose;
        const auto purpose = state.explorer_modal.name_input_purpose();
        modal_label(purpose == ExplorerNameInputPurpose::NewFile ? "新規ファイルの作成"
                    : purpose == ExplorerNameInputPurpose::NewFolder ? "新規フォルダの作成"
                    : "名前の変更", {286, 196, 390, 28}, {29, 78, 137, 255});
        modal_label(purpose == ExplorerNameInputPurpose::Rename
                        ? "新しい名前を入力してください。"
                        : "作成する名前を入力してください。",
                    {290, 244, 390, 22}, {91, 98, 107, 255});
        modal_label(truncate_utf8(state.explorer_modal.confirmation_input() +
                                      state.explorer_modal.composition(), 60) + "|",
                    {304, 291, 350, 24});
        if (!state.explorer_modal_error.empty())
            modal_label(truncate_utf8(state.explorer_modal_error, 60), {290, 326, 400, 20},
                        {155, 45, 45, 255});
        modal_button("キャンセル", {380, 350, 110, 42}, [&state] {
            state.explorer_modal.dismiss();
            state.explorer_modal_error.clear();
            configure_page(state);
        });
        modal_button(purpose == ExplorerNameInputPurpose::Rename ? "変更" : "作成",
                     {510, 350, 110, 42}, [&state] {
            commit_explorer_rename(state);
            configure_page(state);
        }, ButtonEntry::Visual::Primary);
    } else if (kind == ExplorerModalKind::ContextActions) {
        const auto view = state.explorer->view_state();
        modal_label("選択項目の操作", {286, 178, 390, 28}, {29, 78, 137, 255});
        modal_button("開く", {286, 226, 110, 42}, [&state] {
            state.explorer_modal.dismiss();
            activate_selected_explorer_item(state);
        }, ButtonEntry::Visual::Primary, view.can_open);
        modal_button("コピー", {410, 226, 110, 42}, [&state] {
            state.explorer->begin_copy();
            state.explorer_modal.dismiss();
            configure_page(state);
        }, ButtonEntry::Visual::Toolbar, view.can_copy);
        modal_button("貼り付け", {534, 226, 110, 42}, [&state] {
            begin_explorer_paste(state);
        }, ButtonEntry::Visual::Toolbar, view.can_paste);
        modal_button("展開", {286, 282, 110, 42}, [&state] {
            begin_explorer_extraction(state);
        }, ButtonEntry::Visual::Toolbar, view.can_extract);
        const bool destructive_available = view.can_trash || view.can_restore ||
                                           view.can_delete_permanently;
        const std::string destructive_text = view.can_restore ? "復元" :
            (view.can_delete_permanently ? "完全削除" : "trashへ");
        modal_button(destructive_text, {410, 282, 110, 42}, [&state] {
            const auto current = state.explorer->view_state();
            if (current.can_restore)
                state.explorer_modal.show_restore_from_trash(current.selected_count);
            else if (current.can_delete_permanently)
                state.explorer_modal.show_delete_permanently(current.selected_count);
            else if (current.can_trash)
                state.explorer_modal.show_move_to_trash(current.selected_count);
            configure_page(state);
        }, view.can_delete_permanently || view.can_trash
               ? ButtonEntry::Visual::Danger : ButtonEntry::Visual::Primary,
            destructive_available);
        modal_button("閉じる", {534, 282, 110, 42}, [&state] {
            state.explorer_modal.dismiss();
            configure_page(state);
        });
    } else if (kind == ExplorerModalKind::ConfirmMoveToTrash) {
        modal_label("trashへ移動しますか？", {286, 206, 390, 28}, {145, 42, 42, 255});
        modal_label(std::to_string(state.explorer_modal.affected_count()) +
                        "項目をtrashへ移動します。",
                    {286, 250, 390, 24});
        modal_label("元に戻す操作はこの画面では行いません。", {286, 282, 390, 24},
                    {91, 98, 107, 255});
        modal_button("キャンセル", {380, 346, 110, 42}, [&state] {
            state.explorer_modal.dismiss();
            state.explorer_modal_error.clear();
            configure_page(state);
        });
        modal_button("移動する", {510, 346, 110, 42}, [&state] {
            state.explorer->move_selected_to_trash();
            state.explorer_modal.dismiss();
            configure_page(state);
        }, ButtonEntry::Visual::Danger);
    } else if (kind == ExplorerModalKind::ConfirmRestoreFromTrash) {
        modal_label("trashから復元しますか？", {286, 206, 390, 28}, {29, 110, 73, 255});
        modal_label(std::to_string(state.explorer_modal.affected_count()) +
                        "項目を元の場所へ復元します。",
                    {286, 250, 410, 24});
        modal_label("元のフォルダがない場合はアプリルートへ復元します。",
                    {286, 282, 430, 24}, {91, 98, 107, 255});
        modal_button("キャンセル", {380, 346, 110, 42}, [&state] {
            state.explorer_modal.dismiss();
            state.explorer_modal_error.clear();
            configure_page(state);
        });
        modal_button("復元する", {510, 346, 110, 42}, [&state] {
            state.explorer->restore_selected_from_trash();
            state.explorer_modal.dismiss();
            configure_page(state);
        }, ButtonEntry::Visual::Primary);
    } else if (kind == ExplorerModalKind::ConfirmDeletePermanently) {
        modal_label("完全削除しますか？", {286, 206, 390, 28}, {145, 42, 42, 255});
        modal_label(std::to_string(state.explorer_modal.affected_count()) +
                        "項目を完全削除します。元に戻せません。",
                    {286, 250, 430, 24});
        modal_label("この操作はtrash内の選択項目だけに適用されます。",
                    {286, 282, 430, 24}, {91, 98, 107, 255});
        modal_button("キャンセル", {380, 346, 110, 42}, [&state] {
            state.explorer_modal.dismiss();
            state.explorer_modal_error.clear();
            configure_page(state);
        });
        modal_button("完全削除", {510, 346, 110, 42}, [&state] {
            state.explorer->delete_selected_permanently();
            state.explorer_modal.dismiss();
            configure_page(state);
        }, ButtonEntry::Visual::Danger);
    } else if (kind == ExplorerModalKind::ConfirmOpenText) {
        modal_label(truncate_utf8(state.explorer_modal.document().title, 48) +
                        " を開きますか？",
                    {286, 232, 410, 28}, {29, 78, 137, 255});
        modal_button("キャンセル", {380, 346, 110, 42}, [&state] {
            state.explorer_modal.dismiss();
            state.explorer_modal_error.clear();
            configure_page(state);
        });
        modal_button("開く", {510, 346, 110, 42}, [&state] {
            state.explorer_modal.begin_text_editor();
            state.explorer_text_hscroll = 0.0F;
            state.explorer_text_dragging = false;
            configure_page(state);
        }, ButtonEntry::Visual::Primary);
    } else if (kind == ExplorerModalKind::TextEditor) {
        // ダイアログはページ全体の高さを使う。文書の行・選択範
        // 囲・カーソルは ラベル部品ではなくレンダラーで描き
        // 、実測した文字位置へ選択範囲の強調表示を合わせる。
        modal_label("テキスト編集", {100, 14, 170, 28}, {29, 78, 137, 255});
        modal_label(truncate_utf8(state.explorer_modal.document().title, 70),
                    {270, 16, 560, 24});
        // 編集用の操作ボタン。タッチでもすべてのコマンドを使えるようにする。
        auto& modal = state.explorer_modal;
        float tool_x = kTextEditorBox.x;
        const auto tool = [&](const std::string& text, std::function<void()> callback,
                              bool enabled = true, bool selected = false) {
            const float width = compact_button_width(text);
            add_button(state, text, {tool_x, 48, width, 36}, std::move(callback),
                       ButtonEntry::Visual::Toolbar, enabled, selected, true);
            tool_x += width + 6.0F;
        };
        const bool selection = modal.has_selection();
        tool("全選択", [&state] {
            state.explorer_modal.select_all();
            configure_page(state);
        });
        tool("範囲選択", [&state] {
            state.explorer_modal.set_range_mode(!state.explorer_modal.range_mode());
            configure_page(state);
        }, true, modal.range_mode());
        tool("コピー", [&state] {
            if (state.explorer_modal.has_selection())
                ::cw::lib::set_system_clipboard_text(state.explorer_modal.selected_text());
            configure_page(state);
        }, selection);
        tool("切り取り", [&state] {
            if (state.explorer_modal.has_selection())
                ::cw::lib::set_system_clipboard_text(state.explorer_modal.cut_selection());
            configure_page(state);
        }, selection);
        tool("貼り付け", [&state] {
            paste_into_explorer_editor(state);
            configure_page(state);
        });
        tool("削除", [&state] {
            state.explorer_modal.delete_selection();
            configure_page(state);
        }, selection);
        tool("検索", [&state] {
            if (state.explorer_modal.search_active()) state.explorer_modal.end_search();
            else state.explorer_modal.begin_search();
            configure_page(state);
        }, true, modal.search_active());
        modal_label(modal.dirty() ? "未保存の変更があります" : "変更なし",
                    {104, 552, 230, 22},
                    modal.dirty() ? SDL_Color{155, 72, 35, 255}
                                  : SDL_Color{91, 98, 107, 255});
        if (!state.explorer_modal_error.empty())
            modal_label(truncate_utf8(state.explorer_modal_error, 62), {330, 552, 520, 22},
                        {155, 45, 45, 255});
        if (modal.search_active()) {
            // 検索語自体はkTextEditorSearchBox内へ描画する。
            const float search_right = kTextEditorSearchBox.x + kTextEditorSearchBox.w + 6.0F;
            const bool query = !modal.search_query().empty();
            modal_button("前へ", {search_right, 578, 0, 42}, [&state] {
                if (!state.explorer_modal.find(false))
                    state.explorer_modal_error = "見つかりません";
                else state.explorer_modal_error.clear();
                configure_page(state);
            }, ButtonEntry::Visual::Toolbar, query);
            modal_button("次へ", {search_right + 69, 578, 0, 42}, [&state] {
                if (!state.explorer_modal.find(true))
                    state.explorer_modal_error = "見つかりません";
                else state.explorer_modal_error.clear();
                configure_page(state);
            }, ButtonEntry::Visual::Toolbar, query);
        }
        modal_button("キャンセル", {616, 578, 110, 42}, [&state] {
            state.explorer_modal.dismiss();
            state.explorer_modal_error.clear();
            configure_page(state);
        });
        modal_button("保存", {744, 578, 110, 42}, [&state] {
            auto document = state.explorer_modal.document();
            const auto result = state.explorer->save_text_document(document);
            if (result.ok) {
                state.explorer_modal.dismiss();
                state.explorer_modal_error.clear();
                state.explorer->refresh_async();
            } else state.explorer_modal_error = result.message;
            configure_page(state);
        }, ButtonEntry::Visual::Primary);
    } else if (kind == ExplorerModalKind::ConfirmEmptyTrash) {
        modal_label("trashを空にしますか？", {266, 176, 430, 28}, {145, 42, 42, 255});
        modal_label("trash内のすべての項目を完全削除します。元に戻せません。",
                    {266, 222, 440, 24});
        modal_label("確認のため trash と入力してください。", {290, 258, 390, 22},
                    {91, 98, 107, 255});
        modal_label(state.explorer_modal.confirmation_input() + "|",
                    {304, 291, 350, 24});
        modal_button("キャンセル", {370, 380, 110, 42}, [&state] {
            state.explorer_modal.dismiss();
            state.explorer_modal_error.clear();
            configure_page(state);
        });
        modal_button("完全削除", {500, 380, 110, 42}, [&state] {
            if (!state.explorer_modal.empty_trash_confirmed()) return;
            state.explorer->empty_trash();
            state.explorer_modal.dismiss();
            configure_page(state);
        }, ButtonEntry::Visual::Danger, state.explorer_modal.empty_trash_confirmed());
    } else if (kind == ExplorerModalKind::ExtractProgress) {
        const auto& title = state.explorer_modal.subject_name();
        modal_label(title.empty() ? "解凍・保存中" : title,
                    {286, 202, 390, 28}, {29, 78, 137, 255});
        modal_label(title.empty() ? "アーカイブを解凍して保存しています。" :
                    "保存が完了するまでお待ちください。", {286, 242, 410, 24});
        modal_label("処理が完了するまでお待ちください。", {286, 278, 410, 22},
                    {91, 98, 107, 255});
    } else if (kind == ExplorerModalKind::DownloadProgress) {
        modal_label("ダウンロード準備中", {286, 202, 390, 28}, {29, 78, 137, 255});
        modal_label("zipファイルに変換中です。", {286, 242, 410, 24});
        modal_label("処理が完了するまでお待ちください。", {286, 278, 410, 22}, {91, 98, 107, 255});
    } else if (kind == ExplorerModalKind::ChooseUploadType) {
        const bool upload_busy = state.transfer_picker_active;
        const bool confirm_overwrite = !state.explorer_pending_upload_paths.empty();
        const auto begin_import = [&state](std::vector<std::filesystem::path> paths,
                                           bool overwrite) {
            const auto lifetime = std::weak_ptr<std::atomic_bool>(state.callback_lifetime);
            auto* runtime = &state;
            const auto staging_paths = paths;
            state.explorer_pending_upload_paths.clear();
            state.explorer_upload_conflicts.clear();
            state.transfer_picker_active = true;
            state.explorer_modal_error = "UserDataへ読み込み中…";
            (void)state.explorer->import_external_async(
                std::move(paths), [lifetime, runtime, paths = staging_paths](
                    cardwirth::explorer::OperationResult result) mutable {
                    cardwirth::explorer::release_upload_paths(paths);
                    const auto alive = lifetime.lock();
                    if (!alive || !alive->load(std::memory_order_acquire)) return;
                    runtime->transfer_picker_active = false;
                    if (result.ok) {
                        runtime->explorer_modal.dismiss();
                        runtime->explorer_modal_error.clear();
                    } else {
                        runtime->explorer_modal_error = std::move(result.message);
                    }
                    configure_page(*runtime);
                }, overwrite);
            configure_page(state);
        };
        modal_label(upload_busy ? "アップロード中" :
                    confirm_overwrite ? "上書き確認" : "アップロード",
                    {286, 202, 390, 28}, {29, 78, 137, 255});
        if (upload_busy) {
            modal_label("ファイルをUserDataへ読み込んでいます。",
                        {286, 242, 410, 24});
            modal_label("処理が完了するまでお待ちください。",
                        {286, 278, 410, 22}, {91, 98, 107, 255});
        } else if (confirm_overwrite) {
            const auto count = state.explorer_upload_conflicts.size();
            modal_label(truncate_utf8(state.explorer_upload_conflicts.front(), 30) +
                            (count > 1 ? " ほか" + std::to_string(count - 1) + "項目" : "") +
                            " は既にあります。", {286, 242, 410, 24});
            modal_label("上書きしますか？フォルダは中身ごと置き換えます。",
                        {286, 278, 410, 22}, {155, 45, 45, 255});
        } else {
            modal_label(state.explorer_drop_paths.empty()?"現在のフォルダへ追加する種類を選んでください。":"ドロップしたファイルを追加しますか？",
                        {286, 242, 410, 24});
        }
        if (!upload_busy && !confirm_overwrite && !state.explorer_modal_error.empty())
            modal_label(truncate_utf8(state.explorer_modal_error, 54), {286, 278, 400, 22},
                        {155, 45, 45, 255});
        // 非同期の取り込みが完了するまで、共通のアップロード処理
        // とモーダル画面を保持する。 下の描画処理で処理中の表
        // 示を出し、ここで戻ることで全モーダルボタンを取り除く
        // 。 すべての対応OSで、転送中はモーダル状態を保つ。
        if (upload_busy) return;
        if (confirm_overwrite) {
            modal_button("上書き", {425, 332, 110, 42}, [&state, begin_import] {
                begin_import(std::move(state.explorer_pending_upload_paths), true);
            }, ButtonEntry::Visual::Danger);
            modal_button("キャンセル", {548, 332, 110, 42}, [&state] {
                try { cardwirth::explorer::release_upload_paths(state.explorer_pending_upload_paths); }
                catch (...) {}
                state.explorer_pending_upload_paths.clear();
                state.explorer_upload_conflicts.clear();
                state.explorer_modal.dismiss();
                state.explorer_modal_error.clear();
                configure_page(state);
            });
            return;
        }
        const auto upload = [&state, begin_import](bool folders) {
            if (state.transfer_picker_active) return;
            state.transfer_picker_active = true;
            state.explorer_modal_error = "OSのファイル選択を開いています…";
            configure_page(state);
            auto lifetime = std::weak_ptr<std::atomic_bool>(state.callback_lifetime);
            auto* runtime = &state;
            auto completed = [lifetime, runtime, begin_import](cardwirth::explorer::NativePathSelection selection) {
                const auto alive = lifetime.lock();
                if (!alive || !alive->load(std::memory_order_acquire)) return;
                if (selection.cancelled) {
                    runtime->transfer_picker_active = false;
                    runtime->explorer_modal.dismiss();
                    runtime->explorer_modal_error.clear();
                } else if (!selection.error.empty()) {
                    runtime->transfer_picker_active = false;
                    runtime->explorer_modal_error = std::move(selection.error);
                } else {
                    auto paths = std::move(selection.paths);
                    try {
                        const auto& controller = runtime->explorer->controller();
                        const auto entries = controller.filesystem().list(controller.current_directory());
                        const auto folded = [](std::string name) {
                            std::transform(name.begin(), name.end(), name.begin(),
                                [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                            return name;
                        };
                        for (const auto& path : paths) {
                            const auto raw = path.filename().u8string();
                            const std::string name(reinterpret_cast<const char*>(raw.data()), raw.size());
                            const auto key = folded(name);
                            if (std::any_of(entries.begin(), entries.end(), [&](const auto& entry) {
                                    return folded(entry.name) == key;
                                })) runtime->explorer_upload_conflicts.push_back(name);
                        }
                        if (!runtime->explorer_upload_conflicts.empty()) {
                            runtime->explorer_pending_upload_paths = std::move(paths);
                            runtime->transfer_picker_active = false;
                            runtime->explorer_modal_error.clear();
                        } else {
                            begin_import(std::move(paths), false);
                        }
                    } catch (const std::exception& error) {
                        cardwirth::explorer::release_upload_paths(paths);
                        runtime->transfer_picker_active = false;
                        runtime->explorer_modal_error = error.what();
                    }
                }
                configure_page(*runtime);
            };
            if(!state.explorer_drop_paths.empty()){
                cardwirth::explorer::NativePathSelection selection;
                selection.status=cardwirth::explorer::TransferStatus::Success;
                selection.paths=std::move(state.explorer_drop_paths);
                state.explorer_drop_paths.clear();
                completed(std::move(selection));
            } else if (folders)
                cardwirth::explorer::choose_upload_folders_async(
                    state.window, std::move(completed));
            else
                cardwirth::explorer::choose_upload_files_async(
                    state.window, std::move(completed));
        };
        modal_button(state.explorer_drop_paths.empty()?"ファイル":"追加する", {302, 332, 110, 42}, [upload] { upload(false); },
                     ButtonEntry::Visual::Primary, !state.transfer_picker_active);
        if(state.explorer_drop_paths.empty())modal_button("フォルダ", {425, 332, 110, 42}, [upload] { upload(true); },
                     ButtonEntry::Visual::Primary, !state.transfer_picker_active);
        modal_button("キャンセル", {548, 332, 110, 42}, [&state] {
            state.explorer_drop_paths.clear();
            state.explorer_modal.dismiss();
            state.explorer_modal_error.clear();
            configure_page(state);
        });
    } else if (kind == ExplorerModalKind::ConfirmFlattenExtraction) {
        const auto name = state.explorer_modal.subject_name();
        modal_label(truncate_utf8(name + "/" + name, 44) +
                        " の二重フォルダです。",
                    {270, 206, 430, 28}, {155, 72, 35, 255});
        modal_label("中間のフォルダを消してまとめますか？",
                    {286, 250, 400, 24});
        if (!state.explorer_modal_error.empty())
            modal_label(truncate_utf8(state.explorer_modal_error, 54), {286, 282, 400, 22},
                        {155, 45, 45, 255});
        modal_button("そのまま", {380, 346, 110, 42}, [&state] {
            state.explorer->keep_extracted_directory();
            state.explorer_modal.dismiss();
            state.explorer_modal_error.clear();
            configure_page(state);
        });
        modal_button("まとめる", {510, 346, 110, 42}, [&state] {
            const auto result = state.explorer->flatten_extracted_directory();
            if (result.ok) {
                state.explorer_modal.dismiss();
                state.explorer_modal_error.clear();
            } else state.explorer_modal_error = result.message;
            configure_page(state);
        }, ButtonEntry::Visual::Primary);
    }
}

// 時間のかかるページ切り替えは、処理中の表示を1フレーム出した後に実行する。
void begin_busy_action(RuntimeState& state, std::function<void()> action) {
    if (state.busy_action || !action) return; // 切り替えは一度に一つだけ行う。
    state.busy_action = std::move(action);
    state.busy_action_presented = false;
    state.busy_started_at = SDL_GetTicks();
}

[[nodiscard]] bool host_busy(const RuntimeState& state) noexcept {
    return static_cast<bool>(state.busy_action) || state.home_return_busy;
}

bool launch_engine(RuntimeState& state, cardwirth::app::Page parent_page,
                   const std::vector<std::string>& requested_arguments = {}) {
#if defined(__EMSCRIPTEN__) && !CARDWIRTH_WEB_PTHREADS
    (void)parent_page;
    (void)requested_arguments;
    state.shell->set_status("Web Engineにはpthread対応ブラウザが必要です");
    return false;
#endif
#if defined(CARDWIRTH_ALL_HOST)
    if (state.hosted_engine && state.hosted_engine->running()) {
        if (requested_arguments.empty() && parent_page == cardwirth::app::Page::XEditor &&
            state.hosted_engine_parent == cardwirth::app::Page::XEditor) {
            state.close_editor_after_engine = false;
            state.hosted_engine->set_covered(false);
            state.hosted_engine->focus(
                (SDL_GetWindowFlags(state.window) & SDL_WINDOW_INPUT_FOCUS) != 0);
            state.shell->navigate(cardwirth::app::Page::CardWirthPy);
            configure_page(state);
            return true;
        }
        state.shell->set_status("CardWirthPy_C はALL内で既に実行中です");
        return false;
    }
#endif
    if (state.child && state.child->running()) {
        state.shell->set_status("CardWirthPy_C は既に起動しています");
        return false;
    }
    {
        // 埋め込みワーカーの状態通知は非同期で届く。完了セ
        // ッションは次の起動前にjoinするが、 UIキュ
        // ーに残った通知を新しいセッションの状態として表示
        // しないよう、 共通のホスト処理で世代を区別する。
        std::scoped_lock lock(state.pending_status_mutex);
        state.pending_status_messages.clear();
    }
#if defined(CARDWIRTH_EMBED_ENGINE)
    if (state.shell->engine_backend() == cardwirth::app::EngineBackend::Embedded) {
        if (!state.runtime_context) {
            state.shell->set_status("Embedded Engine runtime context is unavailable");
            return false;
        }
#if defined(CARDWIRTH_ALL_HOST)
        cardwirth::app::AppLaunchOptions options;
        options.host_mode = cardwirth::app::EngineHostMode::All;
        options.executable_path = state.self_path;
        options.application_directory = base_directory();
        options.user_data_root = state.shell->user_data().root;
        options.arguments = {"--mode", "engine", "--user-data",
                             path_to_utf8(state.shell->user_data().root)};
        options.arguments.insert(options.arguments.end(),
                                 state.encrypted_engine_arguments.begin(),
                                 state.encrypted_engine_arguments.end());
        if (requested_arguments.empty())
            options.arguments.insert(options.arguments.end(),
                                     state.forwarded_engine_arguments.begin(),
                                     state.forwarded_engine_arguments.end());
        if (state.nonactive_debug)
            options.arguments.emplace_back("--nonactive-debug");
        if (state.communication_debug) {
            options.arguments.emplace_back("--communication-debug");
            if (!state.communication_debug_port.empty()) {
                options.arguments.emplace_back("--communication-debug-port");
                options.arguments.emplace_back(state.communication_debug_port);
            }
        }
        if (state.force_engine_debug)
            options.arguments.emplace_back("-debug");
        if (!requested_arguments.empty()) {
            options.arguments.insert(options.arguments.end(), requested_arguments.begin(),
                                     requested_arguments.end());
        } else if (!state.engine_yado.empty()) {
            options.arguments.emplace_back("-yado");
            options.arguments.emplace_back(state.engine_yado);
        }
        auto session = std::make_unique<cardwirth::app::HostedEngineSession>();
        std::uintptr_t native_host_window = 0;
#ifdef _WIN32
        native_host_window = reinterpret_cast<std::uintptr_t>(SDL_GetPointerProperty(
            SDL_GetWindowProperties(state.window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
#endif
        state.native_host_window = native_host_window;
        if (!session->start(*state.runtime_context, options, native_host_window)) {
            state.shell->set_status("Hosted Engine sessionを開始できませんでした");
            return false;
        }
        state.hosted_engine_parent = parent_page;
        state.hosted_engine = std::move(session);
        // ホーム・Explorerから移動しても、ALLウィンドウが既にフォ
        // ーカスを持つため 新しいSDLフォーカスイベントは発生しない。埋め
        // 込みFrameは非フォーカス時のボタン入力を拒否するので、 次のAlt+Tab
        // を待たず、現在のホストのフォーカスをすぐに同期する。
        state.hosted_engine->focus(
            (SDL_GetWindowFlags(state.window) & SDL_WINDOW_INPUT_FOCUS) != 0);
        state.hosted_test_dialog_kind.reset();
        state.hosted_test_dialog_sent = false;
        const std::pair<std::string_view, cardwirth::app::HostedEngineModalKind>
            dialog_test_routes[] = {
                {"settings", cardwirth::app::HostedEngineModalKind::Settings},
                {"yado", cardwirth::app::HostedEngineModalKind::YadoSelect},
                {"party", cardwirth::app::HostedEngineModalKind::PartySelect},
                {"player", cardwirth::app::HostedEngineModalKind::PlayerSelect},
                {"scenario", cardwirth::app::HostedEngineModalKind::ScenarioSelect},
                {"album", cardwirth::app::HostedEngineModalKind::Album},
                {"party-record", cardwirth::app::HostedEngineModalKind::PartyRecord},
                {"cards", cardwirth::app::HostedEngineModalKind::CardControl},
                {"adventurer", cardwirth::app::HostedEngineModalKind::AdventurerCreate},
                {"party-edit", cardwirth::app::HostedEngineModalKind::PartyEdit},
                {"battle", cardwirth::app::HostedEngineModalKind::BattleCommand},
                {"instructions", cardwirth::app::HostedEngineModalKind::Instructions},
            };
        if (const auto route = std::find_if(
                std::begin(dialog_test_routes), std::end(dialog_test_routes),
                [&](const auto& entry) { return entry.first == state.hosted_test_dialog; });
            route != std::end(dialog_test_routes)) {
            state.hosted_test_dialog_kind = route->second;
            // Engineが最初のフレームを公開する前にモーダルを要求
            // すると、 ホストが画面を確認する前に共通の入れ子ダイア
            // ログループへ入る場合がある。 非アクティブの統合テスト
            // では、最初のフレームの後に実際の共通ダイアログを開く。
            if (!state.hosted_engine_smoke_test) {
                state.hosted_engine->show_dialog_for_test(route->second);
                state.hosted_test_dialog_sent = true;
            }
        } else if (!state.hosted_test_dialog.empty()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Unknown --hosted-test-dialog route: %s",
                         state.hosted_test_dialog.c_str());
        }
        state.hosted_engine_frame_sequence = 0;
        state.hosted_engine_modal_sequence = 0;
        state.hosted_engine_modal_model_id = 0;
        state.shell->navigate(cardwirth::app::Page::CardWirthPy);
        configure_page(state);
        return true;
#else
        cardwirth::app::AppLaunchOptions options;
        options.executable_path = state.self_path;
        options.application_directory = base_directory();
        options.user_data_root = state.shell->user_data().root;
        options.arguments = {"--mode", "engine", "--user-data",
                             path_to_utf8(state.shell->user_data().root)};
        options.arguments.insert(options.arguments.end(),
                                 state.encrypted_engine_arguments.begin(),
                                 state.encrypted_engine_arguments.end());
        if (requested_arguments.empty())
            options.arguments.insert(options.arguments.end(),
                                     state.forwarded_engine_arguments.begin(),
                                     state.forwarded_engine_arguments.end());
        if (!requested_arguments.empty()) {
            options.arguments.insert(options.arguments.end(), requested_arguments.begin(),
                                     requested_arguments.end());
        } else if (!state.engine_yado.empty()) {
            options.arguments.emplace_back("-yado");
            options.arguments.emplace_back(state.engine_yado);
        }
        state.shell->begin_child(parent_page);
        // 埋め込みAPIは、現在のEngineのFrameが入れ子のネイティ
        // ブループを所有するため同期的に動く。 そのループが最前面を使う間は
        // ALLウィンドウを隠し、Engineからホームへ戻ったら復元する。
        SDL_HideWindow(state.window);
        const int exit_code = cardwirth::app::run_engine_embedded(*state.runtime_context, options);
        SDL_ShowWindow(state.window);
        state.shell->end_child(exit_code);
        configure_page(state);
        return true;
#endif
    }
#endif
    if (state.engine_path.empty()) {
        state.shell->set_status("CardWirthPy_C.exe が見つかりません。Engineを先にビルドしてください");
        return false;
    }

    cardwirth::app::ProcessSpec spec;
    spec.executable = state.engine_path;
    spec.working_directory = state.shell->user_data().root;
#if defined(CARDWIRTH_EMBED_ENGINE)
    spec.arguments = {"--mode", "engine", "--user-data", path_to_utf8(state.shell->user_data().root)};
#else
    spec.arguments = {"--user-data", path_to_utf8(state.shell->user_data().root)};
#endif
    spec.arguments.insert(spec.arguments.end(),
                          state.encrypted_engine_arguments.begin(),
                          state.encrypted_engine_arguments.end());
    if (requested_arguments.empty())
        spec.arguments.insert(spec.arguments.end(),
                              state.forwarded_engine_arguments.begin(),
                              state.forwarded_engine_arguments.end());
    if (!requested_arguments.empty()) {
        spec.arguments.insert(spec.arguments.end(), requested_arguments.begin(),
                              requested_arguments.end());
    } else if (!state.engine_yado.empty()) {
        spec.arguments.emplace_back("-yado");
        spec.arguments.emplace_back(state.engine_yado);
    }
    std::string error;
    state.child = cardwirth::app::launch_process(spec, error);
    if (!state.child) {
        state.shell->set_status(std::move(error));
        return false;
    }
    state.shell->begin_child(parent_page);
    // 子が最前面を使う間はEditorを中断する。 SDL
    // ウィンドウも隠し、子の操作中にEditorへキー
    // ボードやマウス入力が誤って届かないようにする。
    SDL_HideWindow(state.window);
    return true;
}

void launch_editor(RuntimeState& state, cardwirth::app::Page parent_page) {
    if (state.shell->suspended()) return;
    if (!cardwirth::app::editor_embedded_available()) {
        state.shell->set_status("XEditor_CはこのALLビルドに接続されていません (Unsupported)");
        return;
    }
    if (!state.runtime_context) {
        state.shell->set_status("XEditor_Cを起動できません: 共通Storageがありません");
        return;
    }

#if defined(CARDWIRTH_ALL_HOST)
    if (state.hosted_editor && state.hosted_editor->running()) {
        state.shell->set_status("XEditor_C はALL内で既に実行中です");
        return;
    }
    auto session = std::make_unique<cardwirth::app::HostedEditorSession>();
    state.editor_zoom = 1.0F;
    state.editor_offset_x = 0.0F;
    state.editor_offset_y = 0.0F;
    state.editor_fingers.clear();
    state.editor_pinch_active = false;
    cardwirth::app::EditorLaunchOptions options;
    options.executable_path = state.self_path;
    options.application_directory = base_directory();
    cardwirth::app::EditorHostOptions host_options;
    host_options.restore_previous_scenario = !state.reset_editor_on_next_launch;
#if defined(__EMSCRIPTEN__)
    options.application_directory = "/program";
#endif
    // Windowsを含め、製品の表示実装はすべてSDLを使う。 どのOSで
    // も同じホスト画面と論理キャンバスを渡す。HWNDだけの設定は 廃止した
    // ネイティブ表示用であり、誤って単独起動のEditorを作る原因になる。
    host_options.native_parent = reinterpret_cast<std::uintptr_t>(state.window);
    host_options.native_sdl_window = reinterpret_cast<std::uintptr_t>(state.window);
    host_options.native_renderer = reinterpret_cast<std::uintptr_t>(state.renderer);
    const auto editor_available = state.all_layout_snapshot.safe_rect.valid()
        ? state.all_layout_snapshot.safe_rect
        : state.window_rect;
    const auto editor_surface = cardwirth::app::all::editor_logical_surface_size(
        editor_available, state.editor_logical_height,
        state.editor_left_side_space, state.editor_right_side_space,
        state.editor_top_side_space, state.editor_bottom_side_space);
    host_options.initial_bounds = {0, 0,
        std::max(1, static_cast<int>(std::lround(editor_surface.width))),
        std::max(1, static_cast<int>(std::lround(editor_surface.height)))};
    // 外側のウィンドウはALLだけが所有するため、Editorのメイン画面
    // は固定の枠なしルートとする。 論理的なDialogは埋め込みの子とし
    // て扱い、タイトルとタイトルバーのドラッグを残す。 設定や編集の各ダイ
    // アログを識別し、ALLのクライアント領域内で移動できるようにする。
    host_options.keep_top_bar = true;
    host_options.allow_internal_move = true;
    // Android・iOS・WebのALLでは別のEngineアプリを起動で
    // きない。 すべてのOSでEditorの起動コマンドを内蔵Engineへ
    // 送り、Editorから受け取った シナリオ・パーティの引数を維持する。
    host_options.execute_engine = [&state](const std::string&,
                                           const std::vector<std::string>& arguments,
                                           const std::filesystem::path&) {
        return launch_engine(state, cardwirth::app::Page::XEditor, arguments);
    };
    host_options.request_engine = [&state] {
        if (state.shell->page() != cardwirth::app::Page::XEditor ||
            state.editor_home_confirmation_active) return;
        (void)launch_engine(state, cardwirth::app::Page::XEditor);
    };
    host_options.request_data = [&state] {
        state.editor_data_directory.reset();
        if (state.shell->page() != cardwirth::app::Page::XEditor ||
            state.editor_home_confirmation_active ||
            state.editor_home_return_pending ||
            state.editor_data_navigation_pending ||
            state.host_close_phase != RuntimeState::HostClosePhase::Idle ||
            !state.hosted_editor || !state.hosted_editor->running()) return;
        // qSaveは変更破棄・保存のモーダル確認を開く場合がある。ホームへ
        // の移動と同様に、 先にこのEditor入力コールバックを戻し、モー
        // ダルのイベントループが 同じ配送スタックへ再入しないようにする。
        state.editor_data_navigation_pending = true;
    };
    // ALL仕様: エディタの「フォルダを開く」は Data Explorer でそのフォルダを
    // 表示する (Web/Android では OS のファイラを起動できない)。Data Explorer
    // は論理 Storage パスで動くため、ユーザーデータ root からの相対パスに変換
    // し、root 外は拒否する。
    host_options.request_data_directory = [&state, request_data = host_options.request_data](
                                              const std::filesystem::path& directory) {
        if (!state.shell || directory.empty() || !request_data) return false;
        std::filesystem::path root;
        if (state.runtime_context) {
            root = state.runtime_context->paths.user_root;
            if (root.empty()) root = state.runtime_context->storage_ref().root();
        } else {
            root = state.shell->user_data().root;
        }
        if (root.empty()) return false;
        const auto relative = directory.lexically_normal().lexically_relative(
            root.lexically_normal());
        if (relative.empty() || relative.is_absolute()) return false;
        for (const auto& part : relative)
            if (part == "..") return false;
        // データボタンと同じ、保存・変更破棄の確認を通る経路を使う。
        request_data();
        if (!state.editor_data_navigation_pending) return false;
        state.editor_data_directory = relative == "." ? std::filesystem::path{} : relative;
        return true;
    };
    host_options.request_home = [&state] {
        if (!state.hosted_editor || !state.hosted_editor->running() ||
            state.editor_home_return_pending ||
            state.editor_data_navigation_pending ||
            state.host_close_phase != RuntimeState::HostClosePhase::Idle) return;
        state.hosted_editor->set_input_blocked(true);
        state.editor_home_confirmation_active = true;
        state.editor_home_confirmation_press.reset();
        SDL_StopTextInput(state.window);
    };
    state.editor_request_home = host_options.request_home;
    session->set_host_iteration([&state] { return pump_editor_modal(state); });
    if (!session->start(*state.runtime_context, options, host_options)) {
        state.shell->set_status(session->error().empty()
            ? "XEditor_CをALL内で開始できませんでした" : session->error());
        return;
    }
    state.hosted_editor_parent = parent_page;
    state.hosted_editor = std::move(session);
    state.reset_editor_on_next_launch = false;
    state.shell->navigate(cardwirth::app::Page::XEditor);
    configure_page(state);
    return;
#else
    // 単独起動の代替処理では、同期的なネイティブ子ウィンドウのライフサイクルを保つ。
    state.shell->begin_child(parent_page);
    SDL_HideWindow(state.window);
    cardwirth::app::EditorLaunchOptions options;
    options.executable_path = state.self_path;
    options.application_directory = base_directory();
    const auto launch = cardwirth::app::run_editor_embedded(
        *state.runtime_context, options);
    SDL_ShowWindow(state.window);
    state.shell->end_child(launch.exit_code);
    if (!launch.error.empty()) state.shell->set_status(launch.error);
    configure_page(state);
#endif
}

void navigate(RuntimeState& state, cardwirth::app::Page page) {
    state.shell->navigate(page);
    configure_page(state);
}

void configure_page(RuntimeState& state) {
    SDL_DestroyTexture(state.preview_texture);
    state.preview_texture = nullptr;
    state.preview_rect = {};
    state.preview_active = false;
    state.image_preview_enlarged = false;
    state.scrollbar_visible = false;
    state.scrollbar_thumb = {};
    state.buttons.clear();
    state.labels.clear();
    state.explorer_icons.clear();
    state.dialog = state.backend->create_dialog();
    state.dialog->set_title("CardWirthPy_C");
    state.dialog->show();

    auto page = state.shell->page();
#if defined(CARDWIRTH_ALL_HOST)
    const bool returning_from_explorer_to_editor =
        state.active_all_page == cardwirth::app::Page::DataExplorer &&
        page == cardwirth::app::Page::XEditor && state.hosted_editor &&
        state.hosted_editor->running();
    // 保持中のEngineの音声は、特定の戻るボタンではなくページの所有権で制御する
    // 。 ダイアログやExplorer経由で戻る場合も同じ扱いにする。 OSによる中
    // 断はFrame::apply_audio_suspensionで独立して扱う。
    if (state.hosted_engine)
        state.hosted_engine->set_covered(page != cardwirth::app::Page::CardWirthPy);
    if (state.hosted_editor)
        state.hosted_editor->set_covered(page != cardwirth::app::Page::XEditor);
#endif
    if (state.explorer)
        state.explorer->controller().set_audio_suspended(state.app_in_background ||
            page != cardwirth::app::Page::DataExplorer);
    if (page == cardwirth::app::Page::Home) {
        // ホームではExplorerのセッションを保持しない。保存済みのファイルには触れない。
        if (state.explorer) {
            state.explorer->controller().cancel_async();
            state.retiring_explorers.push_back(std::move(state.explorer));
        }
        state.explorer_extraction_pending = false;
        try { cardwirth::explorer::release_upload_paths(state.explorer_pending_upload_paths); }
        catch (...) {}
        state.explorer_pending_upload_paths.clear();
        state.explorer_upload_conflicts.clear();
        state.explorer_modal.dismiss();
        state.explorer_modal_error.clear();
    }
#if defined(CARDWIRTH_ALL_HOST)
    if (page != cardwirth::app::Page::XEditor &&
        page != cardwirth::app::Page::CardWirthPy) {
        state.editor_home_confirmation_active = false;
        state.editor_home_confirmation_press.reset();
    }
    // Engine・Explorerの背後でもEditorのセッションと文書は維持する
    // が、 覆われた操作部品へ入力を渡さない。戻ったら保存したフォーカスを復元する。
    if (state.hosted_editor)
        state.hosted_editor->set_input_blocked(
            page != cardwirth::app::Page::XEditor || state.editor_home_confirmation_active);
#endif
    // ALL以外ではXEditorを互換用の経路として残す。
    // ALLではこのページに実際の埋め込みEditorを持つ。
#if !defined(CARDWIRTH_ALL_HOST)
    if (page == cardwirth::app::Page::XEditor) {
        state.shell->set_initial_page(cardwirth::app::Page::Home);
        page = cardwirth::app::Page::Home;
    }
#endif
#if defined(CARDWIRTH_ALL_HOST)
    configure_all_page_surface(state, page);
    refresh_all_layout(state);
#endif
    const auto back = [&state] { state.shell->go_back(); configure_page(state); };
    switch (page) {
        case cardwirth::app::Page::Home:
#if defined(CARDWIRTH_ALL_HOST)
        {
            set_label(state, std::string("Build: ") + cardwirth::app::kBuildTimestamp,
                      {16, static_cast<float>(kLegacyPageHeight - 30), 520, 22});
            if (!state.home_install_prompt_checked) {
                state.home_install_prompt_checked = true;
                std::error_code data_error, scenario_error;
                const auto& folders = state.shell->user_data();
                const bool missing = !std::filesystem::is_directory(folders.data, data_error) ||
                    !std::filesystem::is_directory(folders.scenario, scenario_error);
                state.home_install_prompt_active = !state.home_install_prompt_declined &&
                    (state.home_install_prompt_pending || missing);
                state.home_install_prompt_pending = false;
            }
            if (state.home_install_prompt_active) {
                set_label(state, "DataまたはScenarioフォルダがありません。", {170, 235, 620, 30});
                set_label(state, "webcardwirth.netからダウンロードしますか？", {170, 278, 620, 30});
                set_label(state, "*あとでその他からもインストールできます。", {170, 320, 620, 30});
                add_button(state, "はい", {310, 368, 150, 44}, [&state] {
                    finish_home_install_prompt(state, true);
                }, ButtonEntry::Visual::Primary);
                add_button(state, "いいえ", {500, 368, 150, 44}, [&state] {
                    finish_home_install_prompt(state, false);
                });
                break;
            }
            if (state.home_landscape_action) {
                set_label(state, "スマートフォンを横向きにしてください。", {170, 250, 620, 36});
                set_label(state, "横向きになると、選んだ画面へ移動します。", {170, 300, 620, 30});
                add_button(state, "キャンセル", {390, 368, 180, 44}, [&state] {
                    state.home_landscape_action = {};
                    configure_page(state);
                });
                break;
            }
            if (state.home_initial_install_active) {
                set_label(state, state.home_package_install_busy
                    ? "データをインストールしています…" : state.home_other_status,
                    {170, 250, 620, 54});
                if (state.home_package_install_busy)
                    set_label(state, state.home_other_status, {170, 312, 620, 40});
                else add_button(state, "OK", {405, 368, 150, 44}, [&state] {
                    finish_home_initial_install(state);
                }, ButtonEntry::Visual::Primary);
                break;
            }
            const auto layout = cardwirth::app::all::home_menu_layout(
                {0, 0, static_cast<float>(kLegacyPageWidth),
                 static_cast<float>(kLegacyPageHeight)});
            const auto button_rect = [&layout](const std::size_t index) {
                const auto& rect = layout.cards[index];
                return cardwirth::ui::Rect{rect.x, rect.y, rect.width, rect.height};
            };
            if (state.home_other_open) {
                set_label(state, "その他", {48, 34, 760, 40});
                add_button(state, "基本データをダウンロード", {460, 112, 360, 48},
                           [&state] {
                               start_home_package_install(state, "basic", "基本データ");
                           }, ButtonEntry::Visual::Primary,
                           !state.home_package_install_busy);
                add_button(state, "追加シナリオをダウンロード", {72, 112, 360, 48},
                           [&state] {
                               start_home_package_install(state, "scenarios", "追加シナリオ");
                           }, ButtonEntry::Visual::Primary,
                           !state.home_package_install_busy);
                add_button(state, "追加顔絵をダウンロード", {72, 174, 360, 48},
                           [&state] {
                               start_home_package_install(state, "faces", "追加顔絵");
                           }, ButtonEntry::Visual::Primary,
                           !state.home_package_install_busy);
                set_label(state, "エディタの縦サイズ (597～1600 logical px)",
                          {72, 262, 520, 28});
                set_label(state, std::to_string(state.editor_logical_height),
                          {608, 262, 100, 28});
                add_button(state, "-10", {72, 306, 74, 42}, [&state] {
                    set_editor_home_height(state, state.editor_logical_height - 10);
                }, ButtonEntry::Visual::Toolbar,
                           state.editor_logical_height > cardwirth::app::all::kAllEditorMinimumHeight);
                add_button(state, "-1", {154, 306, 74, 42}, [&state] {
                    set_editor_home_height(state, state.editor_logical_height - 1);
                }, ButtonEntry::Visual::Toolbar,
                           state.editor_logical_height > cardwirth::app::all::kAllEditorMinimumHeight);
                add_button(state, "+1", {236, 306, 74, 42}, [&state] {
                    set_editor_home_height(state, state.editor_logical_height + 1);
                }, ButtonEntry::Visual::Toolbar,
                           state.editor_logical_height < cardwirth::app::all::kAllEditorMaximumHeight);
                add_button(state, "+10", {318, 306, 74, 42}, [&state] {
                    set_editor_home_height(state, state.editor_logical_height + 10);
                }, ButtonEntry::Visual::Toolbar,
                           state.editor_logical_height < cardwirth::app::all::kAllEditorMaximumHeight);
                add_button(state, "680", {420, 306, 74, 42}, [&state] {
                    set_editor_home_height(state, 680);
                }, ButtonEntry::Visual::Toolbar);
                add_button(state, "710", {502, 306, 74, 42}, [&state] {
                    set_editor_home_height(state, 710);
                }, ButtonEntry::Visual::Toolbar);
                add_button(state, "910", {584, 306, 74, 42}, [&state] {
                    set_editor_home_height(state, 910);
                }, ButtonEntry::Visual::Toolbar);
                add_button(state, "1420", {666, 306, 84, 42}, [&state] {
                    set_editor_home_height(state, 1420);
                }, ButtonEntry::Visual::Toolbar);
                const auto add_side_space_controls = [&](const EditorMarginSide side,
                                                         const int x, const int y) {
                    const int value = editor_side_space(state,side);
                    const float row_x = static_cast<float>(x);
                    const float row_y = static_cast<float>(y);
                    const auto change = [&state, side](const int delta) {
                        set_editor_home_side_space(state, side, editor_side_space(state,side) + delta);
                    };
                    const auto reset = [&state, side] {
                        set_editor_home_side_space(state, side, 0);
                    };
                    set_label(state, std::string("エディタ") + editor_side_name(side) + "の余白（0～320px）",
                        {row_x, row_y, 290, 28});
                    set_label(state, std::to_string(value) + " px", {row_x+290, row_y, 80, 28});
                    add_button(state, "-10", {row_x, row_y + 36.0F, 58, 42},
                               [change] { change(-10); },
                               ButtonEntry::Visual::Toolbar, value > 0);
                    add_button(state, "-1", {row_x+66, row_y + 36.0F, 58, 42},
                               [change] { change(-1); },
                               ButtonEntry::Visual::Toolbar, value > 0);
                    add_button(state, "+1", {row_x+132, row_y + 36.0F, 58, 42},
                               [change] { change(1); },
                               ButtonEntry::Visual::Toolbar,
                               value < cardwirth::app::all::kAllEditorMaximumSideSpace);
                    add_button(state, "+10", {row_x+198, row_y + 36.0F, 58, 42},
                               [change] { change(10); },
                               ButtonEntry::Visual::Toolbar,
                               value < cardwirth::app::all::kAllEditorMaximumSideSpace);
                    add_button(state, "0", {row_x+264, row_y + 36.0F, 58, 42}, reset,
                               ButtonEntry::Visual::Toolbar, value != 0);
                };
                add_side_space_controls(EditorMarginSide::Left, 72, 358);
                add_side_space_controls(EditorMarginSide::Top, 460, 358);
                add_side_space_controls(EditorMarginSide::Right, 72, 448);
                add_side_space_controls(EditorMarginSide::Bottom, 460, 448);
                if (!state.home_other_status.empty())
                    set_label(state, state.home_other_status, {72, 540, 760, 38});
                // その他のページでは、左下のビルド情報を見える状態に保つ。
                add_button(state, "HOMEへ戻る", {710, 582, 220, 46}, [&state] {
                    state.home_other_open = false;
                    configure_page(state);
                }, ButtonEntry::Visual::Toolbar);
                break;
            }
            const std::array<std::string, 4> names{
                "ゲーム開始", "シナリオ編集", "データを確認", "その他"};
            add_button(state, names[0], button_rect(0),
                       [&state] { request_home_landscape_navigation(state, [&state] {
                           begin_busy_action(state, [&state] {
                               launch_engine(state, cardwirth::app::Page::Home); }); }); },
                       ButtonEntry::Visual::Default, web_engine_available(), false, false,
                       std::nullopt, false, 0U);
            add_button(state, names[1], button_rect(1),
                       [&state] { request_home_landscape_navigation(state, [&state] {
                           begin_busy_action(state, [&state] {
                               launch_editor(state, cardwirth::app::Page::Home); }); }); },
                       ButtonEntry::Visual::Default,
                       cardwirth::app::editor_embedded_available(), false, false,
                       std::nullopt, false, 1U);
            add_button(state, names[2], button_rect(2),
                       [&state] { request_home_landscape_navigation(state, [&state] {
                           navigate(state, cardwirth::app::Page::DataExplorer); }); },
                       ButtonEntry::Visual::Default, true, false, false,
                       std::nullopt, false, 2U);
            add_button(state, names[3], button_rect(3), [&state] {
                state.home_other_open = true;
                configure_page(state);
            }, ButtonEntry::Visual::Default, true, false, false,
               std::nullopt, false, 3U);
            break;
        }
#else
            set_label(state, "CardWirthPy_C  HOME", {48, 34, 760, 40});
            set_label(state, "UserData: " + path_to_utf8(state.shell->user_data().root), {48, 82, 860, 24});
            add_button(state, "CardWirthPy_C", {72, 164, 250, 56},
                       [&state] { begin_busy_action(state, [&state] {
                           launch_engine(state, cardwirth::app::Page::Home); }); },
                       ButtonEntry::Visual::Default, web_engine_available());
            add_button(state, "XEditor_C", {355, 164, 250, 56},
                       [&state] { begin_busy_action(state, [&state] {
                           launch_editor(state, cardwirth::app::Page::Home); }); },
                       ButtonEntry::Visual::Default,
                       cardwirth::app::editor_embedded_available());
            add_button(state, "Data Explorer", {638, 164, 250, 56},
                       [&state] { navigate(state, cardwirth::app::Page::DataExplorer); });
            set_label(state, "CardWirthPy_C / XEditor_C / Data Explorer をここから起動できます", {72, 266, 816, 26});
#if defined(__EMSCRIPTEN__) && !CARDWIRTH_WEB_PTHREADS
            set_label(state, "Engine: Web pthread版で利用できます / Editor: 利用できます",
                      {72, 302, 816, 26}, {95, 58, 35, 255});
#elif defined(__EMSCRIPTEN__)
            set_label(state, "Engine / Editor / Explorer: Web ALL内で利用できます",
                      {72, 302, 816, 26}, {45, 104, 66, 255});
#elif defined(__APPLE__)
            set_label(state, "Engine / Editor / Explorer: Apple ALL内で利用できます",
                      {72, 302, 816, 26}, {45, 104, 66, 255});
#endif
            break;
#endif

        case cardwirth::app::Page::XEditor:
#if defined(CARDWIRTH_ALL_HOST)
            if (!state.hosted_editor || !state.hosted_editor->running()) {
                set_label(state, "XEditor_Cを終了しています…", {48, 34, 760, 40});
            }
#endif
            break;

        case cardwirth::app::Page::DataExplorer: {
            if (!state.explorer) {
                // 新しいExplorerはルートから開始し、以前のタブは使わない。
                state.explorer_tabs.clear();
                state.explorer_active_tab = 0;
                state.explorer_tab_revision = 0;
                try {
                    auto filesystem = state.runtime_context
                        ? cardwirth::explorer::make_local_filesystem(*state.runtime_context)
                        : cardwirth::explorer::make_local_filesystem(state.shell->user_data().root);
                    if (state.runtime_context) {
                        // ブラウザー・SAF・Appleの提供元を使うExplorerのパスは
                        // 、論理Storageパスとして扱う。 OSのファイル名とみなさず、PreviewService
                        // で選択されたファイルだけをデコードする。
                        auto& storage = state.runtime_context->user_storage_ref();
                        auto previews = cardwirth::explorer::PreviewService(
                            storage, storage.root() / "Data");
                        state.explorer = std::make_unique<cardwirth::explorer::ExplorerComponent>(
                            std::move(filesystem), std::move(previews), 8, false);
                    } else {
                        state.explorer = std::make_unique<cardwirth::explorer::ExplorerComponent>(
                            std::move(filesystem), 8, false);
                    }
                } catch (const std::exception& error) {
                    set_label(state, std::string("Explorer initialization failed: ") + error.what(),
                              {48, 82, 860, 24});
                    add_button(state, "Back", {72, 164, 250, 56}, back);
                    break;
                }
            }
            auto& component = *state.explorer;
            if (state.editor_data_directory) {
                // Editorから指定されたフォルダー（ALLの「フォルダーを開く」）。
                component.navigate_to_async(std::move(*state.editor_data_directory));
                state.editor_data_directory.reset();
            }
            auto& explorer = component.controller();
            const auto* preview = explorer.preview() ? &*explorer.preview() : nullptr;
            state.preview_active = preview != nullptr;
            const auto view = component.view_state();
            // 移動が完了したら、有効なタブのディレクトリも同期する。
            if (state.explorer_tabs.empty()) {
                state.explorer_tabs.push_back(view.current_directory);
                state.explorer_active_tab = 0;
                state.explorer_tab_revision = explorer.navigation_revision();
            } else if (explorer.navigation_revision() != state.explorer_tab_revision) {
                state.explorer_active_tab =
                    std::min(state.explorer_active_tab, state.explorer_tabs.size() - 1U);
                state.explorer_tabs[state.explorer_active_tab] = view.current_directory;
                state.explorer_tab_revision = explorer.navigation_revision();
            }
            // ナビゲーション領域全体をExplorerの枠の端へ少
            // し寄せる。 内部のアイコンと文字の間隔は変えない。
            constexpr float sidebar_left = 4.0F;
            // ナビゲーション領域は幅82pxとし、ル
            // ートの表示名は「ルート」にする。 アイ
            // コンは別に描くため、文字は先頭の空白で
            // 調整せず、アイコンの後ろから配置する。
            constexpr float sidebar_width = 82.0F;
            // Explorerのすべての内容を、render()
            // で描く表示ペインと同じ座標系で扱う。
            constexpr float content_left = kExplorerContentLeft;
            constexpr float file_icon_x = content_left + 21.0F;
            constexpr float file_name_x = content_left + 50.0F;

            float toolbar_x = 20.0F;
            const auto toolbar_rect = [&toolbar_x](std::string_view text) {
                const auto width = compact_button_width(text);
                const cardwirth::ui::Rect result{toolbar_x, 10, width, 42};
                toolbar_x += width + 6.0F;
                return result;
            };

            add_button(state, "アップロード", toolbar_rect("アップロード"), [&state] {
                state.explorer_modal_error.clear();
                state.explorer_modal.show_upload_type();
                configure_page(state);
            }, ButtonEntry::Visual::Primary);
            add_button(state, "一つ戻る", toolbar_rect("一つ戻る"), [&state] {
                state.explorer->go_back_async();
                configure_page(state);
            }, ButtonEntry::Visual::Toolbar, view.can_go_back);
            add_button(state, "更新", toolbar_rect("更新"), [&state] {
                state.explorer->refresh_async();
                configure_page(state);
            }, ButtonEntry::Visual::Toolbar);
            add_button(state, "開く", toolbar_rect("開く"), [&state] {
                activate_selected_explorer_item(state);
            }, ButtonEntry::Visual::Primary, view.can_open);
            add_button(state, "コピー", toolbar_rect("コピー"), [&state] {
                state.explorer->begin_copy();
                configure_page(state);
            }, ButtonEntry::Visual::Toolbar, view.can_copy);
            add_button(state, "貼り付け", toolbar_rect("貼り付け"), [&state] {
                begin_explorer_paste(state);
            }, ButtonEntry::Visual::Toolbar, view.can_paste);
            if (view.in_trash) {
                add_button(state, "復元", toolbar_rect("復元"), [&state] {
                    const auto view = state.explorer->view_state();
                    state.explorer_modal_error.clear();
                    state.explorer_modal.show_restore_from_trash(view.selected_count);
                    configure_page(state);
                }, ButtonEntry::Visual::Primary, view.can_restore);
                add_button(state, "完全削除", toolbar_rect("完全削除"), [&state] {
                    const auto view = state.explorer->view_state();
                    state.explorer_modal_error.clear();
                    state.explorer_modal.show_delete_permanently(view.selected_count);
                    configure_page(state);
                }, ButtonEntry::Visual::Danger, view.can_delete_permanently);
            }
            if (view.at_trash_root || !view.in_trash) {
                const std::string trash_label = view.at_trash_root ? "trashを空に" : "trashへ";
                add_button(state, trash_label, toolbar_rect(trash_label), [&state] {
                    const auto view = state.explorer->view_state();
                    state.explorer_modal_error.clear();
                    if (view.at_trash_root) state.explorer_modal.show_empty_trash();
                    else state.explorer_modal.show_move_to_trash(view.selected_count);
                    configure_page(state);
                }, ButtonEntry::Visual::Danger, view.at_trash_root || view.can_trash);
            }
            add_button(state, "展開", toolbar_rect("展開"), [&state] {
                begin_explorer_extraction(state);
            }, ButtonEntry::Visual::Toolbar, view.can_extract);
            std::filesystem::path download_name;
            bool download_is_directory = false;
            bool download_as_zip = false;
            if (view.selected_count == 1) {
                if (const auto index = explorer.selected_index();
                    index && *index < explorer.entries().size()) {
                    download_name = explorer.entries()[*index].path.filename();
                    download_is_directory =
                        explorer.entries()[*index].type == cardwirth::explorer::EntryType::Directory;
                }
            } else if (view.selected_count > 1) {
                download_name = std::filesystem::path{u8"選択項目.zip"};
                download_as_zip = true;
            }
            add_button(state, "ダウンロード", toolbar_rect("ダウンロード"),
                       [&state, download_name, download_is_directory, download_as_zip] {
                if (state.transfer_picker_active) return;
                const bool named_download = !download_name.empty();
                const bool zip_folder = named_download && download_is_directory &&
                    !cardwirth::explorer::supports_direct_folder_download();
                const bool zip_download = download_as_zip || zip_folder;
                auto suggested_name = download_name;
                if (zip_folder) suggested_name += ".zip";
                state.transfer_picker_active = true;
                state.explorer_modal.show_download_progress();
                state.explorer->set_status("OSの保存先選択を開いています…");
                configure_page(state);
                auto lifetime = std::weak_ptr<std::atomic_bool>(state.callback_lifetime);
                auto* runtime = &state;
                auto selected = [lifetime, runtime, named_download, zip_download](
                                    cardwirth::explorer::NativePathSelection selection) {
                    const auto alive = lifetime.lock();
                    if (!alive || !alive->load(std::memory_order_acquire)) return;
                    try {
                    if (selection.cancelled) {
                        runtime->transfer_picker_active = false;
                        runtime->explorer_modal.dismiss();
                        runtime->explorer->set_status("保存をキャンセルしました");
                        configure_page(*runtime);
                        return;
                    }
                    if (!selection.error.empty() || selection.paths.empty()) {
                        runtime->transfer_picker_active = false;
                        runtime->explorer_modal.dismiss();
                        runtime->explorer->set_status(selection.error.empty()
                            ? "保存先を取得できません" : std::move(selection.error));
                        configure_page(*runtime);
                        return;
                    }
                    const auto output_path = selection.paths.front();
                    runtime->explorer->set_status("書き出し中…");
                    auto exported = [lifetime, runtime, output_path](
                                        cardwirth::explorer::OperationResult operation) mutable {
                        auto operation_message = std::move(operation.message);
                        cardwirth::explorer::finalize_download_path_async(
                            output_path, operation.ok,
                            [lifetime, runtime, operation_ok = operation.ok,
                             operation_message = std::move(operation_message)](
                                bool finalized, std::string finalize_error) mutable {
                                const auto still_alive = lifetime.lock();
                                if (!still_alive ||
                                    !still_alive->load(std::memory_order_acquire)) return;
                                runtime->transfer_picker_active = false;
                                runtime->explorer_modal.dismiss();
                                if (!finalized)
                                    runtime->explorer->set_status(std::move(finalize_error));
                                else if (!operation_ok && !operation_message.empty())
                                    runtime->explorer->set_status(std::move(operation_message));
                                configure_page(*runtime);
                            });
                    };
                    if (zip_download)
                        (void)runtime->explorer->export_selected_as_zip_async(
                            output_path, std::move(exported));
                    else if (named_download)
                        (void)runtime->explorer->export_selected_as_async(
                            output_path, std::move(exported));
                    else
                        (void)runtime->explorer->export_selected_async(
                            output_path, std::move(exported));
                    configure_page(*runtime);
                    } catch (const std::exception& error) {
                        runtime->transfer_picker_active = false;
                        runtime->explorer_modal.dismiss();
                        runtime->explorer->set_status(error.what());
                        configure_page(*runtime);
                    }
                };
                try {
                if (named_download)
                    cardwirth::explorer::choose_download_path_async(
                        state.window, suggested_name,
                        download_is_directory && !zip_download, std::move(selected));
                else
                    cardwirth::explorer::choose_download_folder_async(
                        state.window, std::move(selected));
                } catch (const std::exception& error) {
                    state.transfer_picker_active = false;
                    state.explorer_modal.dismiss();
                    state.explorer->set_status(error.what());
                    configure_page(state);
                }
            }, ButtonEntry::Visual::Toolbar,
               view.can_download && !state.transfer_picker_active);
            add_button(state, "その他", toolbar_rect("その他"), [&state] {
                state.explorer_modal_error.clear();
                state.explorer_modal.show_more_actions();
                configure_page(state);
            }, ButtonEntry::Visual::Toolbar);
            if (!view.copy_sources.empty()) {
                add_button(state, "取消", toolbar_rect("取消"), [&state] {
                    state.explorer->cancel_copy();
                    configure_page(state);
                }, ButtonEntry::Visual::Toolbar);
            }

            set_label(state, "場所", {14, 76, sidebar_width - 14.0F, 24},
                      SDL_Color{92, 99, 108, 255});

            std::vector<std::pair<std::string, std::filesystem::path>> breadcrumbs;
            breadcrumbs.emplace_back("ルート", std::filesystem::path{});
            std::filesystem::path accumulated;
            for (const auto& part : view.current_directory) {
                const auto text = path_to_utf8(part);
                if (text.empty() || text == ".") continue;
                accumulated /= part;
                breadcrumbs.emplace_back(text, accumulated);
            }
            // ルートのパンくずの左に置く、タブ選択用のドロップダウン。
            constexpr float tab_selector_width = 100.0F;
            add_button(state,
                       fit_explorer_text(explorer_tab_title(
                           state.explorer_tabs[state.explorer_active_tab],
                           explorer.filesystem().trash()), tab_selector_width - 38.0F) + " ▼",
                       {content_left + 14.0F, 66, tab_selector_width, 32}, [&state] {
                           state.explorer_modal_error.clear();
                           state.explorer_modal.show_tab_menu();
                           configure_page(state);
                       }, ButtonEntry::Visual::Toolbar);
            float breadcrumb_x = content_left + 14.0F + tab_selector_width + 6.0F;
            for (std::size_t index = 0; index < breadcrumbs.size(); ++index) {
                const auto& [text, path] = breadcrumbs[index];
                if (index != 0) {
                    set_label(state, ">", {breadcrumb_x, 72, 16, 24},
                              SDL_Color{104, 111, 120, 255});
                    breadcrumb_x += 18.0F;
                }
                const float width = std::clamp(compact_button_width(text), 63.0F, 190.0F);
                if (breadcrumb_x + width > kExplorerAddressRight - 4.0F) break;
                add_button(state, truncate_utf8(text, 24), {breadcrumb_x, 66, width, 32},
                           [&state, path] {
                               state.explorer->navigate_to_async(path);
                               configure_page(state);
                           }, ButtonEntry::Visual::Toolbar);
                breadcrumb_x += width + 4.0F;
            }

            const auto navigate_to = [&state](std::filesystem::path directory) {
                state.explorer->navigate_to_async(directory);
                configure_page(state);
            };
            struct NavigationItem {
                const char* label;
                std::filesystem::path path;
            };
            // 表示名は変えず、実際の保存パスは有効なExplorer
            // のバックエンドから取得する。 大文字・小文字を
            // 区別する環境ではAppPathsがTrashを
            // 使うが、 UIの表示名は小文字のままにする。
            const auto trash_path = explorer.filesystem().trash();
            const std::array<NavigationItem, 5> navigation{{
                {"ルート", {}}, {"Data", "Data"}, {"Scenario", "Scenario"},
                {"Yado", "Yado"}, {"trash", trash_path},
            }};
            for (std::size_t index = 0; index < navigation.size(); ++index) {
                const auto& item = navigation[index];
                const float y = 112.0F + static_cast<float>(index) * 52.0F;
                add_button(state, item.label,
                           {sidebar_left, y, sidebar_width, 46},
                           [navigate_to, path = item.path] { navigate_to(path); },
                           ButtonEntry::Visual::Sidebar, true,
                           view.current_directory == item.path);
                const auto kind = index == 0 ? ExplorerIcon::Kind::Home :
                    index == navigation.size() - 1 ? ExplorerIcon::Kind::Trash :
                    ExplorerIcon::Kind::Folder;
                // ALLのどの倍率でも、アイコンとラベルの間に1
                // 文字幅の隙間を保つ。 4pxの隙間では、ブラウ
                // ザーで縮小したときに両者がつながって見える。
                state.explorer_icons.push_back({{sidebar_left + 3.0F, y + 14, 18, 18}, kind});
            }
#if !defined(CARDWIRTH_EXPLORER_ONLY)
            // ホーム画面はALLが管理する。戻る操作はファイ
            // ル操作から分け、 ナビゲーション領域の下端へ固
            // 定して、どのディレクトリでも同じ位置に置く。
            add_button(state, state.shell->back_page() == cardwirth::app::Page::XEditor
                                  ? "Editor" : "ホームに戻る",
                       {sidebar_left, 496.0F, sidebar_width, 42.0F}, [&state] {
                const bool returning_to_editor =
                    state.shell->back_page() == cardwirth::app::Page::XEditor &&
                    state.hosted_editor && state.hosted_editor->running();
                if (returning_to_editor)
                    state.shell->go_back();
                else
                    state.shell->set_initial_page(cardwirth::app::Page::Home);
                configure_page(state);
            }, ButtonEntry::Visual::Primary, true, false, false, std::nullopt, true);
#endif

            // プレビュー表示中も、右側の空き領域
            // を使いながら小さな内側余白を保つ。
            const float list_right = preview ? 692.0F : 952.0F;
            constexpr float scrollbar_width = 20.0F;
            const bool needs_scrollbar = view.item_count > view.visible_capacity;
            const float rows_right = list_right - (needs_scrollbar ? scrollbar_width + 4.0F : 0.0F);
            const float size_x = preview ? 452.0F : 600.0F;
            const float modified_x = preview ? 526.0F : 700.0F;
            const auto active_header = [&view](cardwirth::explorer::ExplorerSortColumn column) {
                return view.sort_column == column ? ButtonEntry::Visual::Primary
                                                   : ButtonEntry::Visual::Toolbar;
            };
            const auto header_text = [&view](std::string label,
                                             cardwirth::explorer::ExplorerSortColumn column) {
                if (view.sort_column != column) return label;
                label += view.sort_direction == cardwirth::explorer::ExplorerSortDirection::Ascending
                    ? " ▲" : " ▼";
                return label;
            };
            add_button(state, header_text("名前", cardwirth::explorer::ExplorerSortColumn::Name),
                       {content_left, 108, size_x - content_left - 2.0F, 36},
                       [&state] {
                           state.explorer->toggle_sort(cardwirth::explorer::ExplorerSortColumn::Name);
                           configure_page(state);
                       }, active_header(cardwirth::explorer::ExplorerSortColumn::Name));
            add_button(state, header_text("サイズ", cardwirth::explorer::ExplorerSortColumn::Size),
                       {size_x, 108, modified_x - size_x - 2.0F, 36},
                       [&state] {
                           state.explorer->toggle_sort(cardwirth::explorer::ExplorerSortColumn::Size);
                           configure_page(state);
                       }, active_header(cardwirth::explorer::ExplorerSortColumn::Size));
            add_button(state,
                       header_text("更新時間", cardwirth::explorer::ExplorerSortColumn::Modified),
                       {modified_x, 108, rows_right - modified_x, 36},
                       [&state] {
                           state.explorer->toggle_sort(
                               cardwirth::explorer::ExplorerSortColumn::Modified);
                           configure_page(state);
                       }, active_header(cardwirth::explorer::ExplorerSortColumn::Modified));
            for (const auto& visible : view.rows) {
                const auto& entry = visible.entry;
                const float y = 146.0F + static_cast<float>(visible.visible_row) * 49.0F;
                add_button(state, "", {content_left, y, rows_right - content_left, 47},
                            [&state, row = visible.visible_row] {
                                state.explorer->select_visible_row(
                                    row, state.pointer_extend_selection);
                                configure_page(state);
                            }, ButtonEntry::Visual::FileRow, true, visible.selected, false,
                            visible.visible_row);
                const bool is_directory = entry.type == cardwirth::explorer::EntryType::Directory;
                const bool is_file = entry.type == cardwirth::explorer::EntryType::File;
                state.explorer_icons.push_back({{file_icon_x, y + 14, 18, 18},
                    is_directory ? ExplorerIcon::Kind::Folder :
                    is_file ? ExplorerIcon::Kind::File : ExplorerIcon::Kind::Blocked});
                set_label(state, truncate_utf8(entry.name, preview ? 23 : 42),
                           {file_name_x, y + 12, size_x - file_name_x - 12.0F, 24});
                if (is_file)
                    set_label(state, format_file_size(entry.size),
                              {size_x + 12.0F, y + 12, modified_x - size_x - 14.0F, 24},
                              SDL_Color{88, 96, 105, 255});
                set_label(state, format_modified_time(entry.modified),
                          {modified_x + 12.0F, y + 12, rows_right - modified_x - 14.0F, 24},
                          SDL_Color{88, 96, 105, 255});
            }

            if (needs_scrollbar) {
                state.scrollbar_visible = true;
                const float track_height = 392.0F;
                const float thumb_height = std::max(
                    38.0F, track_height * static_cast<float>(view.visible_capacity) /
                                     static_cast<float>(view.item_count));
                const auto maximum_offset = view.item_count - view.visible_capacity;
                const float progress = maximum_offset == 0 ? 0.0F :
                    static_cast<float>(view.scroll_offset) / static_cast<float>(maximum_offset);
                state.scrollbar_thumb = {list_right - scrollbar_width - 2.0F, 146.0F +
                    (track_height - thumb_height) * progress, scrollbar_width, thumb_height};
            }

            if (preview) {
                set_label(state, "プレビュー", {716, 118, 210, 24}, SDL_Color{29, 78, 137, 255});
                set_label(state, truncate_utf8(preview->title, 28), {716, 150, 220, 24});
                if (preview->kind == cardwirth::explorer::PreviewKind::Image) {
                    state.preview_texture = SDL_CreateTexture(
                        state.renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                        preview->image_width, preview->image_height);
                    if (state.preview_texture) {
                        SDL_UpdateTexture(state.preview_texture, nullptr, preview->rgba.data(),
                                          preview->image_width * 4);
                        SDL_SetTextureScaleMode(state.preview_texture, SDL_SCALEMODE_LINEAR);
                        const float scale = std::min(220.0F / preview->image_width,
                                                     310.0F / preview->image_height);
                        state.preview_rect.w = preview->image_width * scale;
                        state.preview_rect.h = preview->image_height * scale;
                        state.preview_rect.x = 716.0F + (220.0F - state.preview_rect.w) * 0.5F;
                        state.preview_rect.y = 186.0F + (310.0F - state.preview_rect.h) * 0.5F;
                    }
                    set_label(state, preview->text, {716, 510, 220, 24},
                              SDL_Color{88, 96, 105, 255});
                } else if (preview->kind == cardwirth::explorer::PreviewKind::Audio) {
                    set_label(state, "音楽を再生中", {716, 202, 220, 28},
                              SDL_Color{29, 110, 73, 255});
                    set_label(state, "別の場所を触ると停止します", {716, 238, 224, 24},
                              SDL_Color{88, 96, 105, 255});
                } else if (preview->kind == cardwirth::explorer::PreviewKind::Video) {
                    set_label(state, "動画を再生中", {716, 202, 220, 28},
                              SDL_Color{29, 110, 73, 255});
                    set_label(state, "OSの動画プレイヤーを使用", {716, 238, 224, 24},
                              SDL_Color{88, 96, 105, 255});
                } else {
                    std::istringstream input(preview->text);
                    std::string line;
                    float text_y = 190.0F;
                    unsigned line_count = 0;
                    while (line_count < 16 && std::getline(input, line)) {
                        set_label(state, truncate_utf8(std::move(line), 28),
                                  {716, text_y, 224, 19}, SDL_Color{73, 80, 89, 255});
                        text_y += 20.0F;
                        ++line_count;
                    }
                }
            }

            std::string status = view.status;
            if (!view.copy_sources.empty())
                status = (view.move_pending ? "移動待機: " : "コピー待機: ") +
                 std::to_string(view.copy_sources.size()) + "項目";
            else if (status == std::to_string(view.item_count) + "項目") status.clear();
            set_label(state, status, {content_left + 12.0F, 566, 518, 24},
                      SDL_Color{73, 80, 89, 255});
            set_label(state, std::to_string(view.item_count) + " 個の項目",
                      {20, 566, sidebar_width - 20.0F, 24},
                      SDL_Color{88, 96, 105, 255});
            break;
        }

        case cardwirth::app::Page::CardWirthPy:
#if defined(CARDWIRTH_ALL_HOST)
#if !defined(CARDWIRTH_ENGINE_ONLY)
            // ALLではEngine側にページの操作部品を持たせない。 ホス
            // トの戻るボタンを一つだけ重ねて表示し、解放時に操作を確定する。
            {
            const auto safe = state.all_layout_snapshot.safe_rect;
            const float chrome_scale = std::max(0.75F, std::min(
                safe.width / static_cast<float>(kWindowWidth),
                safe.height / static_cast<float>(kWindowHeight)));
            add_button(state, state.shell->back_page() == cardwirth::app::Page::XEditor
                                  ? "Editor" : "Home",
                       {safe.right() - 96.0F * chrome_scale,
                        safe.y + 16.0F * chrome_scale,
                        80.0F * chrome_scale, 42.0F * chrome_scale}, [&state] {
                if (state.host_close_phase != RuntimeState::HostClosePhase::Idle) return;
                if (state.hosted_engine && state.hosted_engine->running()) {
                    cancel_hosted_engine_pointer(state);
                    if (state.shell->back_page() == cardwirth::app::Page::XEditor &&
                        state.hosted_editor && state.hosted_editor->running()) {
                        state.close_editor_after_engine = false;
                        state.hosted_engine->set_covered(true);
                        state.hosted_engine->focus(false);
                        state.all_dialog_layer.clear();
                        state.shell->go_back();
                        configure_page(state);
                        return;
                    }
                    state.editor_home_confirmation_active = true;
                    state.editor_home_confirmation_press.reset();
                } else {
                    state.shell->go_back();
                    configure_page(state);
                }
            }, ButtonEntry::Visual::Toolbar, true, false, false, std::nullopt, true);
            state.buttons.back().host_navigation = true;
            }
#endif
#else
            // このページは、単独起動ホスト用のモデル上の経路として残す。
            set_label(state, "CardWirthPy_C", {48, 34, 760, 40});
            add_button(state, "Launch", {72, 164, 250, 56},
                       [&state] { launch_engine(state, cardwirth::app::Page::CardWirthPy); });
            add_button(state, "Home", {355, 164, 250, 56}, back);
#endif
            break;
    }
    if (page == cardwirth::app::Page::DataExplorer && state.explorer_modal.active())
        configure_explorer_modal(state);
    update_text_input(state);
#if defined(CARDWIRTH_ALL_HOST)
    // ボタン・Escape・キーボード・モバイルの戻る操作は
    // すべてconfigure_pageを通る。 入力方法
    // 別の経路を重複させず、ここで一度だけ状態を合わせる。
    if (returning_from_explorer_to_editor)
        state.hosted_editor->reconcile_external_changes();
#endif
}

std::optional<std::size_t> explorer_row_at(const RuntimeState& state, float x, float y) {
    for (const auto& entry : state.buttons) {
        if (!entry.explorer_row) continue;
        const auto rect = entry.rect;
        if (x >= rect.x && x <= rect.x + rect.width &&
            y >= rect.y && y <= rect.y + rect.height)
            return entry.explorer_row;
    }
    return std::nullopt;
}

void show_explorer_context_actions(RuntimeState& state, std::size_t visible_row) {
    if (!state.explorer) return;
    if (state.explorer->controller().preview()) state.explorer->close_preview();
    (void)state.explorer->select_visible_row(visible_row, false);
    state.explorer_modal_error.clear();
    state.explorer_modal.show_context_actions();
    configure_page(state);
}

// ページ座標で指定されたテキスト欄内の位置へ、Explorerの編集
// カーソルを置く。 描画と同じ文字の実測値と水平スクロール量を使う。
bool place_explorer_text_caret(RuntimeState& state, float x, float y, bool extend) {
    if (state.explorer_modal.kind() != cardwirth::explorer::ExplorerModalKind::TextEditor ||
        !state.text_renderer)
        return false;
    const auto& box = kTextEditorBox;
    if (x < box.x || x > box.x + box.w || y < box.y || y > box.y + box.h) return false;
    const auto lines = state.explorer_modal.visible_lines();
    if (lines.empty()) return false;
    const auto row = static_cast<std::size_t>(
        std::max(0.0F, y - kTextEditorFirstLineY) / kTextEditorLineHeight);
    const auto& line = lines[std::min(row, lines.size() - 1)];
    const auto content = std::string_view(state.explorer_modal.document().text)
        .substr(line.start, line.end - line.start);
    const float target = std::max(0.0F, x - kTextEditorTextX + state.explorer_text_hscroll);
    auto byte = state.text_renderer->fit_bytes(content, target);
    // 指定位置にある文字の、近い側の境界へ丸める。
    if (byte < content.size()) {
        auto next = byte + 1;
        while (next < content.size() &&
               (static_cast<unsigned char>(content[next]) & 0xC0U) == 0x80U) ++next;
        const float left = state.text_renderer->measure(content.substr(0, byte));
        const float right = state.text_renderer->measure(content.substr(0, next));
        if (target - left > right - target) byte = next;
    }
    state.explorer_modal.place_cursor_at(line.number, byte, extend);
    return true;
}

void handle_explorer_modal_pointer(RuntimeState& state, float x, float y,
                                   bool extend = false) {
    using cardwirth::explorer::ExplorerModalKind;
    if (state.transfer_picker_active &&
        (state.explorer_modal.kind() == ExplorerModalKind::ChooseUploadType ||
         state.explorer_modal.kind() == ExplorerModalKind::DownloadProgress))
        return;
    for (const auto& entry : state.buttons) {
        if (!entry.modal) continue;
        const auto rect = entry.rect;
        if (x >= rect.x && x <= rect.x + rect.width &&
            y >= rect.y && y <= rect.y + rect.height) {
            if (entry.button && entry.button->enabled()) {
                state.explorer_pressed_rect = rect;
                state.explorer_pressed_until = SDL_GetTicks() + 180U;
            }
            auto clicked = entry.button;
            clicked->click();
            return;
        }
    }
    if (state.explorer_modal.kind() == ExplorerModalKind::TabMenu) {
        // ドロップダウンの外側をクリックしたら閉じる。
        state.explorer_modal.dismiss();
        configure_page(state);
        return;
    }
    if (state.explorer_modal.kind() == ExplorerModalKind::TextEditor) {
        const auto inside = [](const SDL_FRect& rect, float px, float py) {
            return px >= rect.x && px <= rect.x + rect.w &&
                   py >= rect.y && py <= rect.y + rect.h;
        };
        if (!inside(kTextEditorPanel, x, y)) {
            if (state.explorer_modal.text_editor_focused()) {
                state.explorer_modal.blur_text_editor();
                configure_page(state); // SDL_StopTextInputでモバイルのキーボードを閉じる。
            }
            return;
        }
        if (state.explorer_modal.search_active() &&
            inside(kTextEditorSearchBox, x, y)) {
            state.explorer_modal.focus_text_editor();
            configure_page(state);
            return;
        }
    }
    if (place_explorer_text_caret(state, x, y, extend)) {
        state.explorer_modal.focus_text_editor();
        configure_page(state);
    }
}

void handle_explorer_pointer(RuntimeState& state, float x, float y, bool activate_file,
                             bool extend_selection = false) {
    if (state.image_preview_enlarged) {
        // このタップは拡大画像で処理する。外側に触れたら通常のプ
        // レビューへ戻し、 ダイアログの背後の操作は実行しない。
        if (x < kExplorerImageDialog.x ||
            x > kExplorerImageDialog.x + kExplorerImageDialog.w ||
            y < kExplorerImageDialog.y ||
            y > kExplorerImageDialog.y + kExplorerImageDialog.h)
            state.image_preview_enlarged = false;
        return;
    }
    if (state.explorer_modal.active()) {
        handle_explorer_modal_pointer(state, x, y, extend_selection);
        return;
    }
    if (state.preview_texture && x >= state.preview_rect.x &&
        x <= state.preview_rect.x + state.preview_rect.w &&
        y >= state.preview_rect.y && y <= state.preview_rect.y + state.preview_rect.h) {
        state.image_preview_enlarged = true;
        return;
    }
    // プレビューはモードレス。通常のレイアウトへ戻してから、 閉じるき
    // っかけになった同じタップをExplorerの操作へ続けて渡す。
    if (state.explorer && state.explorer->controller().preview()) {
        state.explorer->close_preview();
        configure_page(state);
    }

    std::shared_ptr<cardwirth::ui::Button> clicked;
    ButtonEntry::Visual clicked_visual = ButtonEntry::Visual::Default;
    for (const auto& entry : state.buttons) {
        const auto rect = entry.rect;
        if (x >= rect.x && x <= rect.x + rect.width &&
            y >= rect.y && y <= rect.y + rect.height) {
            clicked = entry.button;
            clicked_visual = entry.visual;
            break;
        }
    }
    state.pointer_extend_selection = extend_selection;
    if (clicked) clicked->click();
    state.pointer_extend_selection = false;
    if (activate_file && clicked && clicked_visual == ButtonEntry::Visual::FileRow &&
        state.explorer) {
        activate_selected_explorer_item(state);
    }
}

void render_text(RuntimeState& state, const std::string& value, float x, float y) {
    state.text_renderer->draw(value, x, y);
}

#if defined(CARDWIRTH_ALL_HOST)
void clear_hosted_dialog_texture(RuntimeState& state) {
    SDL_DestroyTexture(state.hosted_dialog_texture);
    state.hosted_dialog_texture = nullptr;
    state.hosted_dialog_texture_sequence = 0;
    state.hosted_dialog_capture_snapshot.reset();
}

void invalidate_all_renderer_textures(RuntimeState& state) {
    if (state.hosted_editor) state.hosted_editor->renderer_reset();
    // デバイスのリセットでは、CPU側に保持した更新番号は進まない
    // 。 共有ALLレンダラーの全キャッシュを無効にし、次のフレー
    // ムで各資源を 正式な元データから再作成して全体を転送する。
    SDL_DestroyTexture(state.preview_texture);
    state.preview_texture = nullptr;
    SDL_DestroyTexture(state.all_home_assets.background.texture);
    SDL_DestroyTexture(state.all_home_assets.card.texture);
    for (auto& art : state.all_home_assets.art) SDL_DestroyTexture(art.texture);
    state.all_home_assets = {};
    SDL_DestroyTexture(state.hosted_engine_texture);
    state.hosted_engine_texture = nullptr;
    state.hosted_engine_texture_width = 0;
    state.hosted_engine_texture_height = 0;
    state.hosted_engine_frame_sequence = 0;
    SDL_DestroyTexture(state.hosted_debugger_texture);
    state.hosted_debugger_texture = nullptr;
    state.hosted_debugger_texture_sequence = 0;
    clear_hosted_dialog_texture(state);
    SDL_DestroyTexture(state.hosted_editor_canvas);
    state.hosted_editor_canvas = nullptr;
    for (auto& [action, cached] : state.hosted_status_textures) {
        (void)action;
        SDL_DestroyTexture(cached.texture);
    }
    state.hosted_status_textures.clear();
    for (auto& cached : state.hosted_money_icon_textures) {
        SDL_DestroyTexture(cached.texture);
        cached = {};
    }
    state.hosted_status_sequence = 0;
    if (state.text_renderer) state.text_renderer->clear_cache();
}

SDL_Texture* create_hosted_surface_texture(
    SDL_Renderer* renderer, const std::shared_ptr<const ::cw::lib::Surface>& surface) {
    if (!renderer) return nullptr;
    if (!surface || surface->size.width <= 0 || surface->size.height <= 0) return nullptr;
    const int width = static_cast<int>(surface->size.width);
    const int height = static_cast<int>(surface->size.height);
    if (surface->rgba.size() != static_cast<std::size_t>(width) *
            static_cast<std::size_t>(height) * 4U) return nullptr;
    auto* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                      SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!texture) return nullptr;
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    if (!SDL_UpdateTexture(texture, nullptr, surface->rgba.data(), width * 4)) {
        SDL_DestroyTexture(texture);
        return nullptr;
    }
    return texture;
}

SDL_Texture* create_hosted_surface_texture(
    RuntimeState& state, const std::shared_ptr<const ::cw::lib::Surface>& surface) {
    return create_hosted_surface_texture(state.renderer, surface);
}

SDL_Texture* hosted_status_button_texture(
    RuntimeState& state,
    const cardwirth::app::HostedStatusButtonSnapshot& button) {
    auto& cached = state.hosted_status_textures[button.action];
    if (cached.source == button.image) return cached.texture;
    SDL_DestroyTexture(cached.texture);
    cached.texture = nullptr;
    cached.source = button.image;
    if (button.image)
        cached.texture = create_hosted_surface_texture(state, button.image);
    return cached.texture;
}

// 横幅全体を使うステータスダイアログのボタンを、ステータスバーの文字ボタン
// （Resource::_create_statusbtnbmpの戦闘開始な
// ど）と同じ見た目にする。 縦方向のグラデーション・角丸の灰色枠・明るい上端
// をホストで直接描き、 ボタン内に別のステータスバーのボタン画像を重ねない。
void draw_status_wide_button_face(SDL_Renderer* renderer, const SDL_FRect& r,
                                  const bool enabled, const bool pressed,
                                  const float ui_scale) {
    if (r.w <= 2.0F || r.h <= 2.0F) return;
    const float radius = std::min({3.0F * ui_scale, r.w * 0.5F, r.h * 0.5F});
    const int rows = std::max(1, static_cast<int>(std::ceil(r.h)));
    for (int row = 0; row < rows; ++row) {
        const float y = r.y + static_cast<float>(row);
        const float t = (static_cast<float>(row) + 0.5F) / r.h;  // 上端が0、下端が1。
        float red, green, blue;
        if (!enabled) {
            red = green = blue = 240.0F;
        } else if (pressed) {
            // 上半分は220→208、下半分は208→196。
            const float v = t < 0.5F ? 220.0F - 12.0F * t * 2.0F : 208.0F - 12.0F * (t - 0.5F) * 2.0F;
            red = green = blue = v;
        } else {
            // 上半分は白に近く保ち、下半分はわずかに黄色味を付けて暗くする。 Python
            // 版の色は255,255,255と255,255,232。
            if (t < 0.5F) {
                red = green = blue = 255.0F - 4.0F * (0.5F - t) * 2.0F;
            } else {
                const float d = 16.0F * (t - 0.5F) * 2.0F;
                red = green = 255.0F - d;
                blue = 232.0F - d;
            }
        }
        // 角の丸みの外側は塗りつぶさない。
        float inset = 0.0F;
        const float from_edge = std::min(static_cast<float>(row), r.h - 1.0F - static_cast<float>(row));
        if (from_edge < radius) {
            const float dy = radius - from_edge - 0.5F;
            inset = radius - std::sqrt(std::max(0.0F, radius * radius - dy * dy));
        }
        SDL_SetRenderDrawColor(renderer, static_cast<Uint8>(red), static_cast<Uint8>(green),
                               static_cast<Uint8>(blue), 255);
        SDL_RenderLine(renderer, r.x + inset, y, r.x + r.w - 1.0F - inset, y);
    }
    // 有効で押されていないボタンの内側上端へ、明るい面取りを描く。
    if (enabled && !pressed) {
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderLine(renderer, r.x + radius, r.y + 1.0F, r.x + r.w - 1.0F - radius, r.y + 1.0F);
    }
    // 角丸の輪郭。
    const Uint8 line = enabled ? 128 : 192;
    SDL_SetRenderDrawColor(renderer, line, line, line, 255);
    const float left = r.x, top = r.y, right = r.x + r.w - 1.0F, bottom = r.y + r.h - 1.0F;
    SDL_RenderLine(renderer, left + radius, top, right - radius, top);
    SDL_RenderLine(renderer, left + radius, bottom, right - radius, bottom);
    SDL_RenderLine(renderer, left, top + radius, left, bottom - radius);
    SDL_RenderLine(renderer, right, top + radius, right, bottom - radius);
    constexpr int kArcSteps = 6;
    const auto arc = [&](float cx, float cy, float start) {
        float px = cx + radius * std::cos(start), py = cy + radius * std::sin(start);
        for (int i = 1; i <= kArcSteps; ++i) {
            const float a = start + 1.5707963F * static_cast<float>(i) / kArcSteps;
            const float nx = cx + radius * std::cos(a), ny = cy + radius * std::sin(a);
            SDL_RenderLine(renderer, px, py, nx, ny);
            px = nx; py = ny;
        }
    };
    arc(left + radius, top + radius, 3.1415927F);          // 左上。
    arc(right - radius, top + radius, 4.7123890F);         // 右上。
    arc(right - radius, bottom - radius, 0.0F);            // 右下。
    arc(left + radius, bottom - radius, 1.5707963F);       // 左下。
}

SDL_Texture* hosted_money_icon_texture(
    RuntimeState& state, const std::size_t slot,
    const std::shared_ptr<const ::cw::lib::Surface>& icon) {
    auto& cached = state.hosted_money_icon_textures[slot];
    if (cached.source == icon) return cached.texture;
    SDL_DestroyTexture(cached.texture);
    cached.texture = nullptr;
    cached.source = icon;
    if (icon) cached.texture = create_hosted_surface_texture(state, icon);
    return cached.texture;
}

void render_hosted_editor_canvas(RuntimeState& state) {
    if (!state.hosted_editor || !state.hosted_editor->running()) return;
    if (!state.hosted_editor_canvas) {
        state.hosted_editor_canvas = SDL_CreateTexture(
            state.renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET,
            state.editor_canvas_width, state.editor_canvas_height);
        if (!state.hosted_editor_canvas) return;
        SDL_SetTextureScaleMode(state.hosted_editor_canvas, SDL_SCALEMODE_LINEAR);
        SDL_SetTextureBlendMode(state.hosted_editor_canvas, SDL_BLENDMODE_NONE);
    }

    SDL_Texture* previous_target = SDL_GetRenderTarget(state.renderer);
    SDL_Rect previous_viewport{};
    float previous_scale_x = 1.0F;
    float previous_scale_y = 1.0F;
    SDL_GetRenderViewport(state.renderer, &previous_viewport);
    SDL_GetRenderScale(state.renderer, &previous_scale_x, &previous_scale_y);
    if (!SDL_SetRenderTarget(state.renderer, state.hosted_editor_canvas)) return;
    SDL_SetRenderViewport(state.renderer, nullptr);
    SDL_SetRenderScale(state.renderer, 1.0F, 1.0F);
    SDL_SetRenderDrawColor(state.renderer, 128, 128, 128, 255);
    SDL_RenderClear(state.renderer);
    state.hosted_editor->set_surface_transform(editor_surface_transform(state));
    state.hosted_editor->render_hosted();

    SDL_SetRenderTarget(state.renderer, previous_target);
    SDL_SetRenderViewport(state.renderer, &previous_viewport);
    SDL_SetRenderScale(state.renderer, previous_scale_x, previous_scale_y);
    const auto placement = editor_destination_rect(state);
    const SDL_FRect destination{placement.x,placement.y,placement.width,placement.height};
    SDL_RenderTexture(state.renderer, state.hosted_editor_canvas, nullptr, &destination);
}

void update_hosted_dialog_texture(RuntimeState& state) {
    const auto modal = state.hosted_engine ? state.hosted_engine->modal_snapshot() : std::nullopt;
    if (!modal || !modal->presentation_surface) {
        state.engine_dialog_cursor.clear();
        if (state.hosted_dialog_texture) clear_hosted_dialog_texture(state);
        else state.hosted_dialog_capture_snapshot.reset();
        return;
    }
    if(SDL_GetMouseFocus()==state.window) {
        float mouse_x{},mouse_y{};SDL_GetMouseState(&mouse_x,&mouse_y);
        float x{},y{};
        if(!SDL_RenderCoordinatesFromWindow(state.renderer,mouse_x,mouse_y,&x,&y)) {x=-1;y=-1;}
        const auto point=hosted_dialog_point(state,x,y);
        if(point) state.engine_dialog_cursor.update(modal->presentation_controls,point->x,point->y);
        else state.engine_dialog_cursor.clear();
    }
    if (modal->sequence == state.hosted_dialog_texture_sequence) return;
    clear_hosted_dialog_texture(state);
    state.hosted_dialog_texture =
        create_hosted_surface_texture(state, modal->presentation_surface);
    if (state.hosted_dialog_texture) {
        state.hosted_dialog_texture_sequence = modal->sequence;
        state.hosted_dialog_capture_snapshot = modal;
    }
}

void update_hosted_debugger_texture(RuntimeState& state) {
    const auto debugger = state.hosted_engine
        ? state.hosted_engine->debugger_snapshot()
        : cardwirth::app::HostedDebuggerSnapshot{};
    if(state.hosted_engine && !state.hosted_engine->modal_snapshot() &&
       !state.explorer_modal.wants_text_input()) {
        const bool wanted=debugger.visible && state.hosted_debugger_focused && debugger.wants_text_input;
        if(SDL_TextInputActive(state.window)!=wanted) update_text_input(state);
    }
    if (!debugger.visible || !debugger.presentation_surface) {
        SDL_DestroyTexture(state.hosted_debugger_texture);
        state.hosted_debugger_texture = nullptr;
        state.hosted_debugger_texture_sequence = 0;
        return;
    }
    if (state.hosted_debugger_texture &&
        state.hosted_debugger_texture_sequence == debugger.sequence) return;
    SDL_DestroyTexture(state.hosted_debugger_texture);
    state.hosted_debugger_texture =
        create_hosted_surface_texture(state, debugger.presentation_surface);
    state.hosted_debugger_texture_sequence = state.hosted_debugger_texture
        ? debugger.sequence : 0;
}

void update_hosted_engine_texture(RuntimeState& state) {
    if (!state.hosted_engine) return;
    const auto snapshot = state.hosted_engine->frame_snapshot();
    if (!snapshot.surface || snapshot.sequence == state.hosted_engine_frame_sequence)
        return;
    const int width = static_cast<int>(snapshot.surface->size.width);
    const int height = static_cast<int>(snapshot.surface->size.height);
    if (width <= 0 || height <= 0 ||
        snapshot.surface->rgba.size() !=
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U)
        return;
    if (!state.hosted_engine_texture || state.hosted_engine_texture_width != width ||
        state.hosted_engine_texture_height != height) {
        SDL_DestroyTexture(state.hosted_engine_texture);
        state.hosted_engine_texture = SDL_CreateTexture(
            state.renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
            width, height);
        state.hosted_engine_texture_width = width;
        state.hosted_engine_texture_height = height;
        if (state.hosted_engine_texture)
            SDL_SetTextureScaleMode(state.hosted_engine_texture, SDL_SCALEMODE_NEAREST);
        refresh_all_layout(state);
    }
    if (state.hosted_engine_texture &&
        SDL_UpdateTexture(state.hosted_engine_texture, nullptr,
                          snapshot.surface->rgba.data(), width * 4))
        state.hosted_engine_frame_sequence = snapshot.sequence;
}

void update_hosted_engine_window_title(RuntimeState& state, bool engine_page) {
    if (!state.window) return;
    const auto snapshot = engine_page && state.hosted_engine
        ? state.hosted_engine->frame_snapshot()
        : cardwirth::app::HostedEngineFrameSnapshot{};
    cardwirth::app::apply_hosted_engine_window_title(
        state.window, engine_page, snapshot.title);
}

#endif

#if defined(CARDWIRTH_ALL_HOST)
// ウィンドウ全体を暗くし、12個の点が回る処理中表示を出力ピク
// セル座標で描く。 描画後は呼び出し元の倍率と表示領域を戻す。
void render_busy_spinner(RuntimeState& state) {
    if (!state.renderer) return;
    float scale_x = 1.0F, scale_y = 1.0F;
    SDL_Rect viewport{};
    const bool had_viewport = SDL_GetRenderViewport(state.renderer, &viewport);
    SDL_GetRenderScale(state.renderer, &scale_x, &scale_y);
    SDL_SetRenderScale(state.renderer, 1.0F, 1.0F);
    SDL_SetRenderViewport(state.renderer, nullptr);
    int width = 0, height = 0;
    SDL_GetCurrentRenderOutputSize(state.renderer, &width, &height);
    if (width > 0 && height > 0) {
        SDL_SetRenderDrawBlendMode(state.renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(state.renderer, 0, 0, 0, 90);
        const SDL_FRect overlay{0.0F, 0.0F, static_cast<float>(width),
                                static_cast<float>(height)};
        SDL_RenderFillRect(state.renderer, &overlay);
        const float cx = static_cast<float>(width) * 0.5F;
        const float cy = static_cast<float>(height) * 0.5F;
        const float radius = std::max(12.0F, std::min(width, height) * 0.045F);
        const float dot = std::max(3.0F, radius * 0.28F);
        constexpr int kDots = 12;
        const int head = static_cast<int>((SDL_GetTicks() / 80U) % kDots);
        for (int index = 0; index < kDots; ++index) {
            const float angle = static_cast<float>(index) * 6.2831853F / kDots;
            const int age = (head - index + kDots) % kDots;
            const auto alpha = static_cast<Uint8>(255 - age * 18);
            SDL_SetRenderDrawColor(state.renderer, 255, 255, 255, alpha);
            const SDL_FRect square{cx + std::sin(angle) * radius - dot * 0.5F,
                                   cy - std::cos(angle) * radius - dot * 0.5F,
                                   dot, dot};
            SDL_RenderFillRect(state.renderer, &square);
        }
        SDL_SetRenderDrawBlendMode(state.renderer, SDL_BLENDMODE_NONE);
    }
    SDL_SetRenderScale(state.renderer, scale_x, scale_y);
    SDL_SetRenderViewport(state.renderer, had_viewport ? &viewport : nullptr);
}
#endif

// Explorerのテキスト編集欄を描く。行番
// 号、水平スクロール後の各行、 選択範囲、カー
// ソル、IMEの未確定文字列、検索欄を含める。
void render_explorer_text_editor(RuntimeState& state) {
    auto& text_renderer = *state.text_renderer;
    const auto& modal = state.explorer_modal;
    const auto& text = modal.document().text;
    const SDL_Color text_color{43, 48, 55, 255};
    const SDL_Color number_color{130, 136, 144, 255};
    const float text_width = kTextEditorTextRight - kTextEditorTextX;

    SDL_SetRenderDrawColor(state.renderer, 251, 252, 253, 255);
    SDL_RenderFillRect(state.renderer, &kTextEditorBox);
    SDL_SetRenderDrawColor(state.renderer, 206, 212, 219, 255);
    SDL_RenderRect(state.renderer, &kTextEditorBox);

    const auto lines = modal.visible_lines();
    const auto cursor = modal.cursor();
    const auto [selection_start, selection_end] = modal.selection_range();
    const bool editor_composition = !modal.search_active() && !modal.composition().empty();

    // カーソルが水平方向の表示範囲内に収まるようにする。
    // 全行と当たり判定で一つのオフセットを共有する。
    for (const auto& line : lines) {
        if (cursor < line.start || cursor > line.end) continue;
        const float caret_x = text_renderer.measure(
            std::string_view(text).substr(line.start, cursor - line.start)) +
            (editor_composition ? text_renderer.measure(modal.composition()) : 0.0F);
        auto& scroll = state.explorer_text_hscroll;
        if (caret_x - scroll > text_width - 12.0F) scroll = caret_x - text_width + 12.0F;
        if (caret_x - scroll < 0.0F) scroll = std::max(0.0F, caret_x - 40.0F);
    }
    const float scroll = state.explorer_text_hscroll;

    float y = kTextEditorFirstLineY;
    for (const auto& line : lines) {
        text_renderer.draw(std::to_string(line.number + 1), kTextEditorNumberX, y, number_color);
        std::string content = text.substr(line.start, line.end - line.start);
        const bool caret_line = cursor >= line.start && cursor <= line.end;
        const std::size_t caret_byte = caret_line ? cursor - line.start : 0;
        if (caret_line && editor_composition) content.insert(caret_byte, modal.composition());
        const auto x_of = [&](std::size_t byte) {
            return kTextEditorTextX - scroll +
                text_renderer.measure(std::string_view(content).substr(0, byte));
        };

        // 選択範囲の強調表示。改行は短い矩形で示す。
        if (selection_start < selection_end && selection_start <= line.end &&
            selection_end > line.start) {
            const auto from = std::max(selection_start, line.start) - line.start;
            const auto to = std::min(selection_end, line.end) - line.start;
            float left = x_of(from);
            float right = x_of(to) + (selection_end > line.end ? 6.0F : 0.0F);
            left = std::max(left, kTextEditorTextX);
            right = std::min(right, kTextEditorTextRight);
            if (right > left) {
                const SDL_FRect band{left, y - 1.0F, right - left, kTextEditorLineHeight};
                SDL_SetRenderDrawColor(state.renderer, 173, 214, 255, 255);
                SDL_RenderFillRect(state.renderer, &band);
            }
        }

        // 行のうち、画面に見える部分。
        const std::size_t hidden = text_renderer.fit_bytes(content, scroll);
        const float slice_x = x_of(hidden);
        const auto rest = std::string_view(content).substr(hidden);
        const std::size_t shown = text_renderer.fit_bytes(
            rest, kTextEditorTextRight - slice_x);
        if (shown > 0)
            text_renderer.draw(std::string(rest.substr(0, shown)), slice_x, y, text_color);

        if (caret_line && modal.text_editor_focused() && !modal.search_active()) {
            const auto caret_end = caret_byte + (editor_composition ? modal.composition().size() : 0);
            if (editor_composition) {
                // IMEの未確定文字列に下線を付ける。
                const float from = std::max(x_of(caret_byte), kTextEditorTextX);
                const float to = std::min(x_of(caret_end), kTextEditorTextRight);
                SDL_SetRenderDrawColor(state.renderer, 43, 48, 55, 255);
                if (to > from) SDL_RenderLine(state.renderer, from, y + 19.0F, to, y + 19.0F);
            }
            const float caret_x = x_of(caret_end);
            if (caret_x >= kTextEditorTextX - 1.0F && caret_x <= kTextEditorTextRight) {
                const SDL_FRect caret{caret_x, y, 2.0F, 20.0F};
                SDL_SetRenderDrawColor(state.renderer, 29, 78, 137, 255);
                SDL_RenderFillRect(state.renderer, &caret);
            }
        }
        y += kTextEditorLineHeight;
    }

    if (modal.search_active()) {
        SDL_SetRenderDrawColor(state.renderer, 252, 253, 254, 255);
        SDL_RenderFillRect(state.renderer, &kTextEditorSearchBox);
        SDL_SetRenderDrawColor(state.renderer, 150, 167, 187, 255);
        SDL_RenderRect(state.renderer, &kTextEditorSearchBox);
        std::string query = modal.search_query() + modal.composition();
        const float box_width = kTextEditorSearchBox.w - 16.0F;
        // 長い検索語は末尾が見えるようにする。
        std::size_t start = 0;
        while (start < query.size() && text_renderer.measure(
                   std::string_view(query).substr(start)) > box_width) {
            ++start;
            while (start < query.size() &&
                   (static_cast<unsigned char>(query[start]) & 0xC0U) == 0x80U) ++start;
        }
        query.erase(0, start);
        const float text_y = kTextEditorSearchBox.y + 9.0F;
        if (query.empty())
            text_renderer.draw("検索する文字", kTextEditorSearchBox.x + 8.0F, text_y,
                               SDL_Color{150, 156, 164, 255});
        else
            text_renderer.draw(query, kTextEditorSearchBox.x + 8.0F, text_y, text_color);
        if (modal.text_editor_focused()) {
            const SDL_FRect caret{kTextEditorSearchBox.x + 8.0F + text_renderer.measure(query),
                                  text_y, 2.0F, 20.0F};
            SDL_SetRenderDrawColor(state.renderer, 29, 78, 137, 255);
            SDL_RenderFillRect(state.renderer, &caret);
        }
    }
}

void render(RuntimeState& state) {
#if defined(CARDWIRTH_ALL_HOST)
    // Webでは最後のCSSサイズがSDLのサイズ変更通知なしに届く場合が
    // ある。 描画領域を所有するSDLを通して寸法を合わせ、既存の共通サイ
    // ズ変更処理で Engine・Editor・各ダイアログを配置する。DOM
    // の計測頻度を制限し、 寸法が変わらなければ描画先を作り直さない。
    const auto viewport_now = SDL_GetTicks();
    if (state.window && viewport_now >= state.browser_viewport_check_after) {
        state.browser_viewport_check_after = viewport_now + 250U;
        if (const auto browser_size = browser_canvas_size()) {
            int width{}, height{};
            if (SDL_GetWindowSize(state.window,&width,&height) &&
                (width!=browser_size->first || height!=browser_size->second))
                SDL_SetWindowSize(state.window,browser_size->first,browser_size->second);
        }
    }
    // スマホの回転時など、最後のサイズ変更イベントの後でキャン
    // バスの内部寸法を変えるブラウザーがある。 表示時にSDL
    // の実寸を確認し、横向き画面に対して縦向き時の寸法で 倍率
    // ・当たり判定・次のフレームを計算し続けないようにする。
    int current_window_width = 0;
    int current_window_height = 0;
    int current_output_width = 0;
    int current_output_height = 0;
    const bool window_changed = state.window &&
        SDL_GetWindowSize(state.window, &current_window_width, &current_window_height) &&
        current_window_width > 0 && current_window_height > 0 &&
        (static_cast<int>(std::lround(state.window_rect.width)) != current_window_width ||
         static_cast<int>(std::lround(state.window_rect.height)) != current_window_height);
    const bool output_changed = state.renderer &&
        SDL_GetRenderOutputSize(state.renderer, &current_output_width, &current_output_height) &&
        current_output_width > 0 && current_output_height > 0 &&
        (state.render_output_width != current_output_width ||
         state.render_output_height != current_output_height);
    if (window_changed || output_changed) request_all_layout_resize(state);
    // AppIterateを呼ばない、Editorの入れ子のモーダルループにも適用する。
    flush_all_layout_resize(state);
    poll_home_package_install(state);
#endif
    const bool home_page = state.shell->page() == cardwirth::app::Page::Home;
    const bool explorer_page = state.shell->page() == cardwirth::app::Page::DataExplorer;
    const bool editor_page = state.shell->page() == cardwirth::app::Page::XEditor;
#if defined(CARDWIRTH_ALL_HOST)
    const bool engine_page = state.shell->page() == cardwirth::app::Page::CardWirthPy;
    update_hosted_engine_window_title(state, engine_page);
    if (engine_page || editor_page) {
        // ホーム・Explorerは従来の960x640キャンバス用の
        // 表示領域と倍率を設定するが、 Engineと埋め込みEditor
        // は現在のALL出力座標で描く。各ページの描画前に両方をリセッ
        // トし、 回転中や連続するサイズ変更中にページを切り替えても、
        // 縦向きの古い表示領域で 横向きの新フレームが切り取られない
        // ようにする。出力ピクセルとの対応は refresh_all_layout()
        // が設定したホストの論理表示を基準とする。
        SDL_SetRenderViewport(state.renderer, nullptr);
        SDL_SetRenderScale(state.renderer, 1.0F, 1.0F);
    }
    if (engine_page) {
        update_hosted_engine_texture(state);
        update_hosted_dialog_texture(state);
        update_hosted_debugger_texture(state);
    }
#endif
    if (explorer_page)
        SDL_SetRenderDrawColor(state.renderer, 250, 251, 252, 255);
    else if (editor_page)
        SDL_SetRenderDrawColor(state.renderer, 128, 128, 128, 255);
    else
        SDL_SetRenderDrawColor(state.renderer, 21, 25, 34, 255);
    SDL_RenderClear(state.renderer);

#if defined(CARDWIRTH_ALL_HOST)
    // ホーム・Explorerの既存部品は、ALLの物理領
    // 域全体の中央に置く960x640のページ座標を使う。
    // EditorはALL全体の座標を使い、従来の拡大処
    // 理を挟まず、中央配置された移動可能な子画面を描く。
    if (!engine_page && !editor_page) {
        const auto& transform = state.all_layout_snapshot.host_to_render;
        SDL_Rect page_viewport{
            static_cast<int>(std::lround(transform.destination.x / transform.scale_x)),
            static_cast<int>(std::lround(transform.destination.y / transform.scale_y)),
            static_cast<int>(std::lround(transform.destination.width / transform.scale_x)),
            static_cast<int>(std::lround(transform.destination.height / transform.scale_y))};
        SDL_SetRenderViewport(state.renderer, &page_viewport);
        SDL_SetRenderScale(state.renderer, transform.scale_x, transform.scale_y);
    }
#endif

#if defined(CARDWIRTH_ALL_HOST)
    if (home_page) {
        ensure_all_home_assets(state);
        if (state.home_install_prompt_active || state.home_initial_install_active ||
            state.home_landscape_action) {
            SDL_SetRenderDrawBlendMode(state.renderer, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(state.renderer, 21, 25, 34, 255);
            const SDL_FRect page{0, 0, static_cast<float>(kLegacyPageWidth),
                                      static_cast<float>(kLegacyPageHeight)};
            SDL_RenderFillRect(state.renderer, &page);
            const SDL_FRect dialog{130, 205, 700, 230};
            SDL_SetRenderDrawColor(state.renderer, 48, 59, 76, 255);
            SDL_RenderFillRect(state.renderer, &dialog);
            SDL_SetRenderDrawColor(state.renderer, 104, 123, 149, 255);
            SDL_RenderRect(state.renderer, &dialog);
        } else if (state.home_other_open) {
            // その他のページはホームの背景カードではなく、設定・
            // ダウンロード操作を表示する。 インストールされて
            // いる背景素材によらず、必要なコントラストを保つ。
            SDL_SetRenderDrawBlendMode(state.renderer, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(state.renderer, 29, 36, 48, 255);
            const SDL_FRect page{0, 0,
                                 static_cast<float>(kLegacyPageWidth),
                                 static_cast<float>(kLegacyPageHeight)};
            SDL_RenderFillRect(state.renderer, &page);
            SDL_SetRenderDrawColor(state.renderer, 88, 105, 128, 255);
            const SDL_FRect frame{24, 20,
                                  static_cast<float>(kLegacyPageWidth - 48),
                                  static_cast<float>(kLegacyPageHeight - 64)};
            SDL_RenderRect(state.renderer, &frame);
            SDL_SetRenderDrawColor(state.renderer, 48, 59, 76, 255);
            const SDL_FRect header{24, 20,
                                   static_cast<float>(kLegacyPageWidth - 48), 72};
            SDL_RenderFillRect(state.renderer, &header);
            SDL_SetRenderDrawColor(state.renderer, 104, 123, 149, 255);
            SDL_RenderRect(state.renderer, &header);
        } else {
            const auto layout = cardwirth::app::all::home_menu_layout(
                {0, 0, static_cast<float>(kLegacyPageWidth),
                 static_cast<float>(kLegacyPageHeight)});
            if (state.all_home_assets.background.texture) {
                const SDL_FRect destination{
                    layout.background.x, layout.background.y,
                    layout.background.width, layout.background.height};
                SDL_RenderTexture(state.renderer, state.all_home_assets.background.texture,
                                  nullptr, &destination);
            }
        }
    }
#endif

    if (explorer_page) {
        auto fill = [&state](SDL_FRect rect, SDL_Color color) {
            SDL_SetRenderDrawColor(state.renderer, color.r, color.g, color.b, color.a);
            SDL_RenderFillRect(state.renderer, &rect);
        };
        fill({0, 0, 960, 58}, {246, 248, 251, 255});
        fill({0, 58, kExplorerContentLeft, 490}, {243, 246, 249, 255});
        fill({kExplorerContentLeft, 58, 960 - kExplorerContentLeft, 490},
             {255, 255, 255, 255});
        fill({kExplorerContentLeft, 62, kExplorerAddressRight - kExplorerContentLeft, 40},
             {249, 250, 252, 255});
        fill({kExplorerContentLeft, 108, 960 - kExplorerContentLeft, 36},
             {247, 249, 251, 255});
        fill({0, 548, 960, 92}, {246, 248, 250, 255});
        SDL_SetRenderDrawColor(state.renderer, 214, 219, 225, 255);
        SDL_FRect address{kExplorerContentLeft, 62, kExplorerAddressRight - kExplorerContentLeft, 40};
        SDL_RenderRect(state.renderer, &address);
        SDL_RenderLine(state.renderer, kExplorerContentLeft, 58,
                       kExplorerContentLeft, 548);
        SDL_RenderLine(state.renderer, 0, 58, 960, 58);
        SDL_RenderLine(state.renderer, kExplorerContentLeft, 144, 960, 144);
        SDL_RenderLine(state.renderer, 0, 548, 960, 548);
        if (state.preview_active) {
            fill({700, 108, 260, 440}, {249, 250, 252, 255});
            SDL_SetRenderDrawColor(state.renderer, 214, 219, 225, 255);
            SDL_RenderLine(state.renderer, 700, 108, 700, 548);
        }
    }

    if (state.preview_texture)
        SDL_RenderTexture(state.renderer, state.preview_texture, nullptr, &state.preview_rect);

    const auto draw_button = [&state, home_page, explorer_page](const ButtonEntry& entry) {
        if (!entry.button->visible()) return;
        const auto rect = entry.rect;
        SDL_FRect area{rect.x, rect.y, rect.width, rect.height};
#if defined(CARDWIRTH_ALL_HOST)
        if (home_page && entry.home_card_art && *entry.home_card_art < 4U) {
            ensure_all_home_assets(state);
            const auto& assets = state.all_home_assets;
            if (assets.card.texture)
                SDL_RenderTexture(state.renderer, assets.card.texture, nullptr, &area);
            const auto content = cardwirth::app::all::home_menu_card_layout(
                {rect.x, rect.y, rect.width, rect.height});
            const auto& art = assets.art[*entry.home_card_art];
            if (art.texture) {
                const SDL_FRect art_rect{content.art.x, content.art.y,
                                         content.art.width, content.art.height};
                SDL_RenderTexture(state.renderer, art.texture, nullptr, &art_rect);
            }
            for (const auto [dx, dy] : {std::pair{-1.0F, 0.0F},
                                        std::pair{1.0F, 0.0F},
                                        std::pair{0.0F, -1.0F},
                                        std::pair{0.0F, 1.0F}}) {
                const SDL_FPoint outline{
                    content.name_origin.x + dx * content.scale,
                    content.name_origin.y + dy * content.scale};
                state.text_renderer->draw_card_name(
                    entry.button->text(), outline, content.name_max_width,
                    content.scale, {255, 255, 255, 255});
            }
            state.text_renderer->draw_card_name(
                entry.button->text(),
                {content.name_origin.x, content.name_origin.y},
                content.name_max_width, content.scale,
                entry.button->enabled() ? SDL_Color{0, 0, 0, 255}
                                        : SDL_Color{110, 110, 110, 255});
            if (!entry.button->enabled()) {
                SDL_SetRenderDrawBlendMode(state.renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(state.renderer, 180, 180, 180, 120);
                SDL_RenderFillRect(state.renderer, &area);
            }
            return;
        }
#endif
        SDL_Color background{58, 76, 98, 255};
        SDL_Color border{150, 170, 200, 255};
        SDL_Color text{240, 240, 240, 255};
        if (explorer_page) {
            background = {255, 255, 255, 255};
            border = {210, 216, 223, 255};
            text = {36, 42, 49, 255};
            switch (entry.visual) {
                case ButtonEntry::Visual::Toolbar:
                    background = {249, 250, 252, 255};
                    // テキスト編集の範囲選択・検索などの切り
                    // 替えボタンは、有効状態を明確に示す。
                    if (entry.selected) {
                        background = {196, 222, 250, 255};
                        border = {74, 132, 196, 255};
                        text = {18, 62, 112, 255};
                    }
                    break;
                case ButtonEntry::Visual::Primary:
                    background = {226, 239, 255, 255};
                    border = {121, 169, 221, 255};
                    text = {23, 78, 137, 255};
                    break;
                case ButtonEntry::Visual::Danger:
                    background = {255, 245, 245, 255};
                    border = {225, 180, 180, 255};
                    text = {155, 45, 45, 255};
                    break;
                case ButtonEntry::Visual::Sidebar:
                    background = entry.selected ? SDL_Color{218, 234, 252, 255}
                                                : SDL_Color{243, 246, 249, 255};
                    border = entry.selected ? SDL_Color{151, 190, 229, 255}
                                            : background;
                    break;
                case ButtonEntry::Visual::FileRow:
                    background = entry.selected ? SDL_Color{218, 234, 252, 255}
                                                : SDL_Color{255, 255, 255, 255};
                    border = entry.selected ? SDL_Color{142, 184, 225, 255}
                                            : SDL_Color{235, 238, 241, 255};
                    break;
                case ButtonEntry::Visual::Pager:
                    background = {255, 255, 255, 255};
                    break;
                case ButtonEntry::Visual::Default:
                    break;
            }
            if (!entry.button->enabled()) {
                background = {245, 246, 247, 255};
                border = {227, 229, 232, 255};
                text = {166, 171, 178, 255};
            } else if (entry.modal && SDL_GetTicks() < state.explorer_pressed_until &&
                       entry.rect.x == state.explorer_pressed_rect.x &&
                       entry.rect.y == state.explorer_pressed_rect.y &&
                       entry.rect.width == state.explorer_pressed_rect.width &&
                       entry.rect.height == state.explorer_pressed_rect.height) {
                // 押した直後は、短時間だけ暗く沈んだ見た目にする。
                background = {150, 184, 222, 255};
                border = {48, 98, 160, 255};
                text = {10, 40, 80, 255};
            }
        } else if (!entry.button->enabled()) {
            background = {36, 44, 56, 255};
            text = {130, 135, 143, 255};
        }
        SDL_SetRenderDrawColor(state.renderer, background.r, background.g, background.b, background.a);
        SDL_RenderFillRect(state.renderer, &area);
        SDL_SetRenderDrawColor(state.renderer, border.r, border.g, border.b, border.a);
        SDL_RenderRect(state.renderer, &area);
        if (entry.visual == ButtonEntry::Visual::Sidebar) {
            // アイコン用の列を確保し、残りのボタン領域の中央へ文字を置く。
            // 文字の縦位置は従来のy+11固定ではなく、この領域から決める。
            state.text_renderer->draw_in_rect(
                entry.button->text(),
                SDL_FRect{rect.x + 24.0F, rect.y, rect.width - 24.0F, rect.height},
                16.0F, true, text);
        } else {
            // ボタンの配置に使う矩形と同じ矩形で、文
            // 字の水平・垂直方向の中央位置を求める。
            state.text_renderer->draw_in_rect(entry.button->text(), area, 16.0F, true, text);
        }
    };
    for (const auto& entry : state.buttons) {
        // Engineの内容はモバイルの安全領域全体
        // を使うため、ホスト側の戻るボタンはその後
        // に重ねる。 上部のメイン・デバッガーのツ
        // ールバーを覆わないよう、下端へ配置する。
        if (!entry.modal
#if defined(CARDWIRTH_ALL_HOST)
            && !(engine_page && entry.host_navigation)
#endif
        ) draw_button(entry);
    }
#if defined(CARDWIRTH_ALL_HOST)
    // 埋め込み製品の内容を描いてから、ホスト側のナビゲーション部品を重ねる。
    if (engine_page && state.all_layout_snapshot.engine_debug) {
        const auto& layout = *state.all_layout_snapshot.engine_debug;
        const SDL_FRect engine_window{layout.engine_slot_rect.x,
            layout.engine_slot_rect.y, layout.engine_slot_rect.width,
            layout.engine_slot_rect.height};
        SDL_SetRenderDrawColor(state.renderer, 0, 0, 0, 255);
        SDL_RenderFillRect(state.renderer, &engine_window);
        if (state.hosted_engine_texture) {
            const auto& rect = layout.engine_content_rect;
            SDL_FRect destination{rect.x, rect.y, rect.width, rect.height};
            const auto status = state.hosted_engine
                ? state.hosted_engine->status_snapshot()
                : cardwirth::app::HostedStatusSnapshot{};
            // レイアウト・入力座標と同じ表示判定を使う。Debug
            // が側面を使っている間は、 通常の下部ステータスバーを
            // 使えるよう、Engineのフレーム全体を表示する。
            if (layout.status_dialog_visible) {
                const auto cropped =
                    cardwirth::app::all::status_dialog_engine_source_size({
                        static_cast<float>(state.hosted_engine_texture_width),
                        static_cast<float>(state.hosted_engine_texture_height)});
                const SDL_FRect source{
                    0.0F, 0.0F, cropped.width, cropped.height};
                SDL_RenderTexture(state.renderer, state.hosted_engine_texture,
                                  &source, &destination);
            } else {
                SDL_RenderTexture(state.renderer, state.hosted_engine_texture,
                                  nullptr, &destination);
            }
        }
    }
    if (engine_page && state.hosted_engine && state.text_renderer &&
        state.all_layout_snapshot.engine_debug &&
        state.all_layout_snapshot.engine_debug->status_dialog_visible) {
        const auto snapshot = state.hosted_engine->status_snapshot();
        const auto& panel = state.all_layout_snapshot.engine_debug->status_dialog_rect;
        const SDL_FRect panel_rect{panel.x, panel.y, panel.width, panel.height};
        SDL_SetRenderDrawColor(state.renderer, 236, 236, 236, 255);
        SDL_RenderFillRect(state.renderer, &panel_rect);
        SDL_SetRenderDrawColor(state.renderer, 92, 92, 92, 255);
        SDL_RenderRect(state.renderer, &panel_rect);
        const float ui_scale = std::max(0.1F, panel.width / 90.0F);
        const float close_button_extra = (40.0F * ui_scale) * 0.5F;
        const auto money = [&](float y, const std::string& value,
                               const SDL_Color background,
                               SDL_Texture* icon = nullptr) {
            const SDL_FRect rect{panel.x, y, panel.width, 34.0F * ui_scale};
            SDL_SetRenderDrawColor(state.renderer, background.r, background.g,
                                   background.b, background.a);
            SDL_RenderFillRect(state.renderer, &rect);
            float text_x = rect.x + 2.0F * ui_scale;
            if (icon) {
                // ALL仕様: ステータスバーの資金パネルと同じアイコンを一番左に置く。
                float icon_w = 0.0F, icon_h = 0.0F;
                if (SDL_GetTextureSize(icon, &icon_w, &icon_h) && icon_w > 0.0F && icon_h > 0.0F) {
                    const float height = 18.0F * ui_scale;
                    const float width = icon_w * height / icon_h;
                    const SDL_FRect destination{rect.x,
                                                rect.y + (rect.h - height) * 0.5F, width, height};
                    SDL_RenderTexture(state.renderer, icon, nullptr, &destination);
                    text_x = destination.x + width + 2.0F * ui_scale;
                }
            }
            state.text_renderer->draw_in_rect(value,
                {text_x, rect.y, rect.x + rect.w - 2.0F * ui_scale - text_x, rect.h},
                14.0F * ui_scale, true);
        };
        // YadoMoneyPanel/PartyMoneyPanel
        // と同じ表示にする。 対応する実行時オブジェクトがない行は操
        // 作不可とし、共通ステータスバーと同じ灰色の無効表示を使う。
        if (snapshot.battle_round_visible)
            money(panel.y + 46.0F * ui_scale + close_button_extra,
                  "round " + snapshot.battle_round,
                  SDL_Color{0, 0, 128, 255});
        else if (snapshot.yado_money_visible)
            money(panel.y + 46.0F * ui_scale + close_button_extra, snapshot.yado_money,
                  snapshot.yado_money_enabled ? SDL_Color{0, 69, 0, 255}
                                              : SDL_Color{96, 96, 96, 255},
                  hosted_money_icon_texture(state, 0, snapshot.yado_money_icon));
        money(panel.y + 84.0F * ui_scale + close_button_extra,
              snapshot.party_money,
              !snapshot.party_money_enabled ? SDL_Color{96, 96, 96, 255}
              : snapshot.party_money == "0" ? SDL_Color{128, 0, 0, 255}
                                              : SDL_Color{0, 0, 128, 255},
              hosted_money_icon_texture(state, 1, snapshot.party_money_icon));
        const auto controls = hosted_status_controls(state);
        // 右側ショートカットの背景はボタンの背後だけに
        // 描き、 折り畳み時は残りの側面領域を空ける。
        {
            std::optional<SDL_FRect> shortcut_area;
            for (const auto& control : controls) {
                if (!hosted_shortcut_action(control.button.action)) continue;
                const auto& r = control.rect;
                if (!shortcut_area) { shortcut_area = r; continue; }
                shortcut_area->h = r.y + r.h - shortcut_area->y;
            }
            if (shortcut_area) {
                const float pad = 2.0F * ui_scale;
                const SDL_FRect area{shortcut_area->x, shortcut_area->y,
                                     shortcut_area->w, shortcut_area->h + pad};
                SDL_SetRenderDrawColor(state.renderer, 236, 236, 236, 255);
                SDL_RenderFillRect(state.renderer, &area);
                SDL_SetRenderDrawColor(state.renderer, 92, 92, 92, 255);
                SDL_RenderRect(state.renderer, &area);
            }
        }
        for (const auto& control : controls) {
            const auto& r = control.rect;
            // 現在押下中、または解放から180ms以内。
            const bool pressing = control.button.enabled &&
                (state.pending_status_action == control.button.action ||
                 (state.status_pressed_flash == control.button.action &&
                  SDL_GetTicks() < state.status_pressed_flash_until));
            if (control.button.full_width) {
                // ALL仕様: 横長のボタンはステータスバーの文字ボタン（戦闘開始など）
                // と同じ装飾（グラデーション・角丸の枠）で描く。
                draw_status_wide_button_face(state.renderer, r, control.button.enabled,
                                             pressing || control.button.pushed, ui_scale);
            } else if (pressing) {
                SDL_SetRenderDrawColor(state.renderer, 150, 184, 222, 255);
                SDL_RenderFillRect(state.renderer, &r);
                SDL_SetRenderDrawColor(state.renderer, 0, 84, 153, 255);
                SDL_RenderRect(state.renderer, &r);
                const SDL_FRect inner{r.x + 1.0F, r.y + 1.0F, r.w - 2.0F, r.h - 2.0F};
                SDL_RenderRect(state.renderer, &inner);
            } else {
            SDL_SetRenderDrawColor(state.renderer,
                control.button.pushed ? 184 : (control.button.enabled ? 248 : 190),
                control.button.pushed ? 210 : (control.button.enabled ? 248 : 190),
                control.button.pushed ? 236 : (control.button.enabled ? 248 : 190), 255);
            SDL_RenderFillRect(state.renderer, &r);
            SDL_SetRenderDrawColor(state.renderer, 96, 96, 96, 255);
            SDL_RenderRect(state.renderer, &r);
            }
            if (auto* texture = cardwirth::app::hosted_status_text_button(control.button.action)
                    ? nullptr : hosted_status_button_texture(state, control.button);
                texture && control.button.image) {
                const float source_width =
                    static_cast<float>(control.button.image->size.width);
                const float source_height =
                    static_cast<float>(control.button.image->size.height);
                const float padding = 2.0F * ui_scale;
                const float available_width = std::max(1.0F, r.w - padding * 2.0F);
                const float available_height = std::max(1.0F, r.h - padding * 2.0F);
                const float fit = std::min(available_width / source_width,
                                           available_height / source_height);
                const SDL_FRect destination{
                    r.x + (r.w - source_width * fit) * 0.5F,
                    r.y + (r.h - source_height * fit) * 0.5F,
                    source_width * fit, source_height * fit};
                SDL_RenderTexture(state.renderer, texture, nullptr, &destination);
            } else {
                // ラベル中の改行で、同じ高さの複数行に分割する。 「スクリ
                // ーン」と「ショット」を分けるショートカットなどに使う。
                const SDL_Color ink = control.button.enabled ? SDL_Color{20,20,20,255}
                                                             : SDL_Color{105,105,105,255};
                const float font = (control.button.full_width ? 14.0F : 10.0F) * ui_scale;
                std::vector<std::string> lines;
                for (std::size_t start = 0;;) {
                    const auto end = control.button.label.find('\n', start);
                    lines.push_back(control.button.label.substr(start, end - start));
                    if (end == std::string::npos) break;
                    start = end + 1;
                }
                const float line_h = r.h / static_cast<float>(lines.size());
                for (std::size_t i = 0; i < lines.size(); ++i)
                    state.text_renderer->draw_in_rect(lines[i],
                        {r.x, r.y + line_h * static_cast<float>(i), r.w, line_h}, font, true, ink);
            }
        }
    }
    if (engine_page && state.hosted_engine && state.text_renderer &&
        state.all_layout_snapshot.engine_debug &&
        state.all_layout_snapshot.engine_debug->status_dialog_visible &&
        !state.hosted_engine->status_snapshot().click_wait_notice.empty()) {
        const auto& panel = state.all_layout_snapshot.engine_debug->status_dialog_rect;
        const SDL_FRect rect{panel.x, panel.y, panel.width, panel.height};
        SDL_SetRenderDrawColor(state.renderer, 192, 0, 0, 255);
        SDL_RenderFillRect(state.renderer, &rect);
        // 幅の狭い縦型パネルでは、横型バーと異なり明示的な折り返しが必要。
        const std::array<const char*, 4> lines{"クリックor", "(右クリックor", "長時間タッチ)", "で進みます。"};
        const float font = panel.width / 9.0F;
        for (std::size_t i = 0; i < 4; ++i) {
            const SDL_FRect line{panel.x, panel.y + panel.height * 0.5F +
                (static_cast<float>(i)-2.0F)*font*1.6F, panel.width, font*1.6F};
            state.text_renderer->draw_in_rect(lines[i], line, font, true, {255,255,255,255});
        }
    }
    if (engine_page && state.hosted_engine && state.all_layout_snapshot.engine_debug &&
        state.all_layout_snapshot.engine_debug->debugger_visible) {
        const auto debugger = state.hosted_engine->debugger_snapshot();
        const auto& rect = state.all_layout_snapshot.engine_debug->debugger_rect;
        if (state.hosted_debugger_texture && debugger.presentation_surface) {
            SDL_SetRenderDrawColor(state.renderer, 245, 245, 245, 255);
            const SDL_FRect debugger_slot{rect.x, rect.y, rect.width, rect.height};
            SDL_RenderFillRect(state.renderer, &debugger_slot);
            if (const auto destination = hosted_debugger_destination(state)) {
                const SDL_FRect fitted{
                    destination->x, destination->y,
                    destination->width, destination->height};
                SDL_RenderTexture(state.renderer, state.hosted_debugger_texture,
                                  nullptr, &fitted);
            }
        }
    }
    if (engine_page) {
        for (const auto& entry : state.buttons) {
            if (!entry.modal && entry.button && entry.host_navigation)
                draw_button(entry);
        }
    }
    if (editor_page && state.hosted_editor && state.hosted_editor->running())
        render_hosted_editor_canvas(state);
#endif
    if (explorer_page) {
        for (const auto& icon : state.explorer_icons) {
            const float x = icon.rect.x;
            const float y = icon.rect.y;
            const float w = icon.rect.width;
            const float h = icon.rect.height;
            if (icon.kind == ExplorerIcon::Kind::Folder) {
                SDL_SetRenderDrawColor(state.renderer, 225, 169, 42, 255);
                SDL_FRect tab{x + 1, y + 1, w * 0.48F, 5};
                SDL_FRect body{x, y + 5, w, h - 5};
                SDL_RenderFillRect(state.renderer, &tab);
                SDL_RenderFillRect(state.renderer, &body);
                SDL_SetRenderDrawColor(state.renderer, 190, 135, 25, 255);
                SDL_RenderRect(state.renderer, &body);
            } else if (icon.kind == ExplorerIcon::Kind::File) {
                SDL_SetRenderDrawColor(state.renderer, 235, 244, 255, 255);
                SDL_FRect page{x + 2, y, w - 4, h};
                SDL_RenderFillRect(state.renderer, &page);
                SDL_SetRenderDrawColor(state.renderer, 62, 119, 181, 255);
                SDL_RenderRect(state.renderer, &page);
                SDL_RenderLine(state.renderer, x + 5, y + 6, x + w - 5, y + 6);
                SDL_RenderLine(state.renderer, x + 5, y + 10, x + w - 5, y + 10);
            } else if (icon.kind == ExplorerIcon::Kind::Home) {
                SDL_SetRenderDrawColor(state.renderer, 72, 126, 184, 255);
                SDL_FRect house{x + 3, y + 8, w - 6, h - 8};
                SDL_RenderFillRect(state.renderer, &house);
                SDL_RenderLine(state.renderer, x + 1, y + 9, x + w * 0.5F, y + 1);
                SDL_RenderLine(state.renderer, x + w * 0.5F, y + 1, x + w - 1, y + 9);
            } else if (icon.kind == ExplorerIcon::Kind::Trash) {
                SDL_SetRenderDrawColor(state.renderer, 104, 116, 128, 255);
                SDL_FRect bin{x + 4, y + 5, w - 8, h - 6};
                SDL_RenderRect(state.renderer, &bin);
                SDL_RenderLine(state.renderer, x + 2, y + 4, x + w - 2, y + 4);
                SDL_RenderLine(state.renderer, x + 7, y + 1, x + w - 7, y + 1);
            } else {
                SDL_SetRenderDrawColor(state.renderer, 150, 150, 150, 255);
                SDL_RenderLine(state.renderer, x + 2, y + 2, x + w - 2, y + h - 2);
                SDL_RenderLine(state.renderer, x + w - 2, y + 2, x + 2, y + h - 2);
            }
        }
        if (state.scrollbar_visible) {
            SDL_SetRenderDrawColor(state.renderer, 222, 226, 231, 255);
            SDL_FRect track{state.scrollbar_thumb.x, 146, state.scrollbar_thumb.w, 392};
            SDL_RenderFillRect(state.renderer, &track);
            SDL_SetRenderDrawColor(state.renderer, 125, 137, 150, 255);
            SDL_RenderFillRect(state.renderer, &state.scrollbar_thumb);
        }
    }
    for (const auto& entry : state.labels) {
        if (!entry.modal && entry.label->visible()) {
            const auto rect = entry.label->rect();
            const auto color = explorer_page ? entry.color : SDL_Color{240, 240, 240, 255};
            state.text_renderer->draw_in_rect(entry.label->text(),
                {rect.x, rect.y, rect.width, rect.height}, 16.0F, false, color);
        }
    }
    if (explorer_page && state.image_preview_enlarged && state.preview_texture) {
        SDL_SetRenderDrawBlendMode(state.renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(state.renderer, 20, 26, 34, 160);
        const SDL_FRect overlay{0, 0, 960, 640};
        SDL_RenderFillRect(state.renderer, &overlay);
        SDL_SetRenderDrawBlendMode(state.renderer, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(state.renderer, 255, 255, 255, 255);
        SDL_RenderFillRect(state.renderer, &kExplorerImageDialog);
        SDL_SetRenderDrawColor(state.renderer, 176, 183, 191, 255);
        SDL_RenderRect(state.renderer, &kExplorerImageDialog);
        float image_width = 0.0F;
        float image_height = 0.0F;
        if (SDL_GetTextureSize(state.preview_texture, &image_width, &image_height) &&
            image_width > 0.0F && image_height > 0.0F) {
            const float scale = std::min({840.0F / image_width,
                                          500.0F / image_height, 2.0F});
            const SDL_FRect enlarged{
                480.0F - image_width * scale * 0.5F,
                318.0F - image_height * scale * 0.5F,
                image_width * scale, image_height * scale};
            SDL_RenderTexture(state.renderer, state.preview_texture, nullptr, &enlarged);
        }
        state.text_renderer->draw_in_rect("外側をタップして戻る",
            {60, 574, 840, 24}, 16.0F, true, {88, 96, 105, 255});
    }
#if !defined(CARDWIRTH_ALL_HOST)
    // ALLのどのページにも、下部の受動的なステータス行は
    // 設けない。 ホームへ戻るボタンや埋め込みEditor
    // の下に、単独起動シェル用の状態文字列を描かない。
    if (!explorer_page)
        render_text(state, state.shell->status(), 48.0F, 570.0F);
#endif

    if (explorer_page && state.explorer_modal.active()) {
        using cardwirth::explorer::ExplorerModalKind;
        const bool dropdown = state.explorer_modal.kind() == ExplorerModalKind::TabMenu;
        SDL_FRect panel{250, 180, 460, 230};
        if (dropdown) {
            // タブのドロップダウンは選択欄の下に一覧とし
            // て描き、Explorer全体は暗くしない。
            float left = 960.0F, top = 640.0F, right = 0.0F, bottom = 0.0F;
            for (const auto& entry : state.buttons) {
                if (!entry.modal) continue;
                left = std::min(left, entry.rect.x);
                top = std::min(top, entry.rect.y);
                right = std::max(right, entry.rect.x + entry.rect.width);
                bottom = std::max(bottom, entry.rect.y + entry.rect.height);
            }
            panel = right > left ? SDL_FRect{left - 2.0F, top - 2.0F,
                                             right - left + 4.0F, bottom - top + 4.0F}
                                 : SDL_FRect{0, 0, 0, 0};
        } else {
            SDL_SetRenderDrawBlendMode(state.renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(state.renderer, 20, 26, 34, 125);
            SDL_FRect overlay{0, 0, 960, 640};
            SDL_RenderFillRect(state.renderer, &overlay);
            SDL_SetRenderDrawBlendMode(state.renderer, SDL_BLENDMODE_NONE);
        }

        if (state.explorer_modal.kind() == ExplorerModalKind::TextEditor)
            panel = kTextEditorPanel;
        else if (state.explorer_modal.kind() == ExplorerModalKind::ConfirmEmptyTrash)
            panel = {220, 150, 520, 300};
        SDL_SetRenderDrawColor(state.renderer, 255, 255, 255, 255);
        SDL_RenderFillRect(state.renderer, &panel);
        SDL_SetRenderDrawColor(state.renderer, 176, 183, 191, 255);
        SDL_RenderRect(state.renderer, &panel);

        if (state.explorer_modal.kind() == ExplorerModalKind::TextEditor) {
            render_explorer_text_editor(state);
        } else if (state.explorer_modal.kind() == ExplorerModalKind::ConfirmEmptyTrash ||
                   state.explorer_modal.kind() == ExplorerModalKind::RenameInput) {
            SDL_FRect input{290, 280, 380, 44};
            SDL_SetRenderDrawColor(state.renderer, 252, 253, 254, 255);
            SDL_RenderFillRect(state.renderer, &input);
            SDL_SetRenderDrawColor(state.renderer, 150, 167, 187, 255);
            SDL_RenderRect(state.renderer, &input);
        }
        for (const auto& entry : state.buttons) {
            if (entry.modal) draw_button(entry);
        }
        for (const auto& entry : state.labels) {
            if (entry.modal && entry.label->visible()) {
                const auto rect = entry.label->rect();
                state.text_renderer->draw_in_rect(entry.label->text(),
                    {rect.x, rect.y, rect.width, rect.height}, 16.0F, false, entry.color);
            }
        }
        if ((state.transfer_picker_active &&
             (state.explorer_modal.kind() == ExplorerModalKind::ChooseUploadType ||
              state.explorer_modal.kind() == ExplorerModalKind::DownloadProgress)) ||
            state.explorer_modal.kind() == ExplorerModalKind::ExtractProgress) {
            constexpr float kTau = 6.28318530717958647692F;
            constexpr float kCenterX = 480.0F;
            constexpr float kCenterY = 334.0F;
            constexpr float kInnerRadius = 15.0F;
            constexpr float kOuterRadius = 30.0F;
            const float phase = static_cast<float>(SDL_GetTicks() % 800U) / 800.0F * kTau;
            SDL_SetRenderDrawBlendMode(state.renderer, SDL_BLENDMODE_BLEND);
            for (int spoke = 0; spoke < 8; ++spoke) {
                const float angle = phase + kTau * static_cast<float>(spoke) / 8.0F;
                const float inner_x = kCenterX + std::cos(angle) * kInnerRadius;
                const float inner_y = kCenterY + std::sin(angle) * kInnerRadius;
                const float outer_x = kCenterX + std::cos(angle) * kOuterRadius;
                const float outer_y = kCenterY + std::sin(angle) * kOuterRadius;
                const auto age = (spoke + 8 -
                                  static_cast<int>(SDL_GetTicks() / 100U) % 8) % 8;
                const Uint8 alpha = static_cast<Uint8>(255 - age * 24);
                SDL_SetRenderDrawColor(state.renderer, 29, 78, 137, alpha);
                SDL_RenderLine(state.renderer, inner_x, inner_y, outer_x, outer_y);
            }
            SDL_SetRenderDrawBlendMode(state.renderer, SDL_BLENDMODE_NONE);
        }
    }
#if defined(CARDWIRTH_ALL_HOST)
    if (engine_page && state.hosted_engine && state.all_layout_snapshot.engine_debug &&
        state.all_layout_snapshot.engine_debug->debugger_visible)
        render_hosted_debugger_tooltip(state);
    bool screenshot_geometry_published = false;
    if (engine_page && state.hosted_engine) {
        if (const auto modal = state.hosted_engine->modal_snapshot();
            modal && modal->presentation_surface &&
            state.hosted_dialog_texture &&
            !state.all_layout_snapshot.dialogs.empty()) {
            SDL_SetRenderDrawBlendMode(state.renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(state.renderer, 0, 0, 0, 150);
            const auto& root = state.all_layout_snapshot.host_rect;
            const SDL_FRect overlay{root.x, root.y, root.width, root.height};
            SDL_RenderFillRect(state.renderer, &overlay);
            SDL_SetRenderDrawBlendMode(state.renderer, SDL_BLENDMODE_NONE);
            const auto& placed = state.all_layout_snapshot.dialogs.front().rect;
            const SDL_FRect destination{placed.x, placed.y, placed.width, placed.height};
            if (state.all_layout_snapshot.engine_debug) {
                const auto frame = state.all_layout_snapshot.engine_debug->engine_rect;
                state.hosted_engine->publish_screenshot_geometry(state.hosted_dialog_capture_snapshot,
                    {frame.x,frame.y,frame.width,frame.height},
                    {placed.x,placed.y,placed.width,placed.height}, state.hosted_debugger_focused);
                screenshot_geometry_published = true;
            }
            SDL_RenderTexture(state.renderer, state.hosted_dialog_texture, nullptr,
                              &destination);
            // 単独起動と同じ、押しボタン共通のマウスホバー表示。
            if (const auto& hovered = state.engine_dialog_cursor.hovered_button();
                hovered && state.dialog_hover_by_mouse)
                ::cw::dialog::render_dialog_hover_highlight(state.renderer, *hovered,
                    placed.x, placed.y,
                    placed.width / static_cast<float>(modal->presentation_surface->size.width),
                    placed.height / static_cast<float>(modal->presentation_surface->size.height));
            // ALL仕様: エンジン側ダイアログの外周も茶色の枠で区別する。
            // 既存の3px基準を半分にして、内容領域を圧迫しない細さにする。
            const float border = std::min(1.5F * state.all_layout_snapshot.host_rect.width / 960.0F,
                                          std::min(placed.width, placed.height) * 0.5F);
            SDL_SetRenderDrawColor(state.renderer, 139, 90, 43, 255);
            const SDL_FRect edges[] = {
                {placed.x, placed.y, placed.width, border},
                {placed.x, placed.y + placed.height - border, placed.width, border},
                {placed.x, placed.y, border, placed.height},
                {placed.x + placed.width - border, placed.y, border, placed.height}};
            SDL_RenderFillRects(state.renderer, edges, 4);
        }
    }
    if (state.hosted_engine && !screenshot_geometry_published) {
        const auto frame = state.all_layout_snapshot.engine_debug
            ? state.all_layout_snapshot.engine_debug->engine_rect
            : cardwirth::app::all::Rect{};
        state.hosted_engine->publish_screenshot_geometry(std::nullopt,
            {frame.x,frame.y,frame.width,frame.height}, {}, state.hosted_debugger_focused);
    }
    if ((editor_page || engine_page) && state.editor_home_confirmation_active) {
        using cardwirth::app::all::editor_home_confirmation_layout;
        const auto modal = editor_home_confirmation_layout(
            state.all_layout_snapshot.host_rect);
        SDL_SetRenderDrawBlendMode(state.renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(state.renderer, 104, 104, 104, 190);
        const SDL_FRect overlay{modal.overlay.x, modal.overlay.y,
                                modal.overlay.width, modal.overlay.height};
        SDL_RenderFillRect(state.renderer, &overlay);
        SDL_SetRenderDrawBlendMode(state.renderer, SDL_BLENDMODE_NONE);

        const SDL_FRect shadow{modal.panel.x + 5.0F * modal.scale,
                               modal.panel.y + 6.0F * modal.scale,
                               modal.panel.width, modal.panel.height};
        SDL_SetRenderDrawColor(state.renderer, 55, 55, 55, 255);
        SDL_RenderFillRect(state.renderer, &shadow);
        const SDL_FRect panel{modal.panel.x, modal.panel.y,
                              modal.panel.width, modal.panel.height};
        SDL_SetRenderDrawColor(state.renderer, 246, 246, 246, 255);
        SDL_RenderFillRect(state.renderer, &panel);
        SDL_SetRenderDrawColor(state.renderer, 92, 92, 92, 255);
        SDL_RenderRect(state.renderer, &panel);

        const SDL_FRect message{modal.message.x, modal.message.y,
                                modal.message.width, modal.message.height};
        if (state.text_renderer)
            state.text_renderer->draw_in_rect(
                engine_page ? (state.shell->back_page() == cardwirth::app::Page::XEditor
                                  ? "ゲームを終了してEditorに戻りますか？"
                                  : "ゲームを終了してホームに戻りますか？")
                            : "全画面をリセットして戻りますか？", message,
                20.0F * modal.scale, true, SDL_Color{24, 24, 24, 255});
        const auto draw_confirmation_button = [&state, &modal](
            const cardwirth::app::all::Rect& rect, const char* text,
            const bool primary) {
            const SDL_FRect area{rect.x, rect.y, rect.width, rect.height};
            SDL_SetRenderDrawColor(state.renderer,
                primary ? 221 : 248, primary ? 235 : 248,
                primary ? 252 : 248, 255);
            SDL_RenderFillRect(state.renderer, &area);
            SDL_SetRenderDrawColor(state.renderer,
                primary ? 67 : 130, primary ? 119 : 130,
                primary ? 173 : 130, 255);
            SDL_RenderRect(state.renderer, &area);
            if (state.text_renderer)
                state.text_renderer->draw_in_rect(
                    text, area, 17.0F * modal.scale, true,
                    SDL_Color{24, 24, 24, 255});
        };
        draw_confirmation_button(modal.accept_button, "はい", true);
        draw_confirmation_button(modal.cancel_button, "いいえ", false);
    }
#endif
#if defined(CARDWIRTH_ALL_HOST)
    if (!engine_page) {
        SDL_SetRenderScale(state.renderer, 1.0F, 1.0F);
        SDL_SetRenderViewport(state.renderer, nullptr);
    }
    if (host_busy(state)) render_busy_spinner(state);
#endif
    SDL_RenderPresent(state.renderer);
    if (state.busy_action) state.busy_action_presented = true;
}

#if defined(CARDWIRTH_ALL_HOST)
bool pump_editor_modal(RuntimeState& state) {
    if (state.quit_requested || !state.hosted_editor || !state.hosted_editor->running()) return false;
#if defined(__EMSCRIPTEN__) && defined(CARDWIRTH_ALL_HOST)
    poll_web_editor_back(state, false);
#endif
    SDL_Event event{};
    if (SDL_WaitEventTimeout(&event,16)) {
        do {
            // 拡大・移動・タッチの選別・サイズ変更・ホストの遷移を含め、外側と
            // 同じイベント経路を使う。 show_modalがスタック上にある
            // 間にセッションが終了しないよう、AppIterateは呼ばない。
            const auto result=SDL_AppEvent(&state,&event);
            if (result!=SDL_APP_CONTINUE) {
                state.quit_requested=true;
                state.smoke_failed=state.smoke_failed || result==SDL_APP_FAILURE;
                return false;
            }
            if (state.quit_requested) return false;
        } while (SDL_PollEvent(&event));
    }
    if (!state.hosted_editor->running() || state.shell->page()!=cardwirth::app::Page::XEditor) return false;
    if (!state.app_in_background) render(state);
    return !state.quit_requested;
}
#endif

}  // namespace

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
    ::cardwirth::app::configure_runtime_library_search();
#if defined(CARDWIRTH_EXPLORER_ONLY)
    SDL_SetAppMetadata("DataExplorer_C", "0.1.0", "jp.cardwirth.cardwirthc.explorer");
#elif defined(CARDWIRTH_EDITOR_ONLY)
    SDL_SetAppMetadata("XEditor_C", "0.1.0", "jp.cardwirth.cardwirthc.editor");
#elif defined(CARDWIRTH_ENGINE_ONLY)
    // LinuxのEngineは単一製品のALLホストとしてビルドするが、デスクトッ
    // プへの登録名はEngineとする。 WSLgが表示できるよう、SDLのWayland/X11
    // 用app_idを生成される.desktopファイル名に合わせる。
    SDL_SetAppMetadata("CardWirthPy_C", "0.1.0", "jp.cardwirth.cardwirthc.engine");
#elif defined(CARDWIRTH_ALL_HOST)
    SDL_SetAppMetadata("CardWirthPy_C", "0.1.0", "jp.cardwirth.cardwirthc.all");
#else
    SDL_SetAppMetadata("CardWirthPy_C", "0.1.0", "jp.cardwirth.cardwirthc.multi");
#endif
#if defined(CARDWIRTH_EMBED_ENGINE)
    for (int index = 1; index + 1 < argc; ++index) {
        if (argv[index] && argv[index + 1] && std::string_view(argv[index]) == "--mode" &&
            std::string_view(argv[index + 1]) == "engine") {
            // ここから戻ると、SDLのコールバックホストがSDL_AppQuit
            // を呼ぶ。 まだシェル状態は作成していないため、この子起動のSDL/Win32
            // 資源はEngineがすべて所有する。 コアがCRTのargv
            // グローバル変数を調べなくて済むよう、起動設定を明示的に渡す。
            cardwirth::app::AppLaunchOptions options;
            if (argc > 0 && argv && argv[0]) options.executable_path = argv[0];
            for (int arg = 1; arg < argc; ++arg)
                if (argv[arg]) options.arguments.emplace_back(argv[arg]);
            return cardwirth::app::run_cardwirth_engine(options) == 0 ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
        }
    }
#endif
#if defined(__EMSCRIPTEN__)
    // SDLのアプリコールバックはrequestAnimationFrame
    // で動かす。 ブラウザー環境によってはページから更新頻度の
    // 設定を引き継ぐため、 0を明示してRAFを選び、setTimeout
    // 経由でWebGLの表示処理を動かさない。
    SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, "0");
#endif
#if defined(CARDWIRTH_EMBED_EDITOR)
    // SDL_Initより先に指定する。Windowsでは映像・IMEの初
    // 期化時に、 アプリ側で描く未確定文字列を公開するかどうかが決まる。
    cwxeditor::lib::configure_sdl_ui_before_init();
#endif
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS)) return SDL_APP_FAILURE;
    if (!TTF_Init()) {
        SDL_Quit();
        return SDL_APP_FAILURE;
    }

    std::string requested_root;
    if (path_argument_value(argc, argv, "--user-data", requested_root) ==
        PathArgumentState::Invalid) {
        SDL_Log("Invalid --user-data: specify one non-empty path");
        TTF_Quit();
        SDL_Quit();
        return SDL_APP_FAILURE;
    }
    std::filesystem::path default_root = writable_user_root();
#if !defined(__ANDROID__) && !defined(__APPLE__) && !defined(__EMSCRIPTEN__) && \
    (defined(CARDWIRTH_EDITOR_ONLY) || defined(CARDWIRTH_EXPLORER_ONLY))
    // 通常のデスクトップ配置では、単独起動の仕様に合わせて XEditor_C
    // の既定ルートを実行ファイルのディレクトリにする。 Apple
    // のバンドルは読み取り専用、Webは/userを使うため、これ
    // らではwritable_user_root()を維持する。
    default_root = base_directory();
#endif
    const auto root = requested_root.empty() ? default_root : std::filesystem::path(requested_root);
    auto user_data = cardwirth::app::UserDataLayout::from_root(root);
#if defined(CARDWIRTH_ALL_HOST)
    // ALLの配置設定は不足するディレクトリを作る場合がある。 空の初回
    // インストールで確認を省略しないよう、設定前の検査結果を保持する。
    std::error_code data_directory_error, scenario_directory_error;
    const bool home_required_folders_missing =
        !std::filesystem::is_directory(user_data.data, data_directory_error) ||
        !std::filesystem::is_directory(user_data.scenario, scenario_directory_error);
#endif
    std::string setup_error;
#if defined(CARDWIRTH_ENGINE_PRIVATE_ENCRYPTION) && defined(CARDWIRTH_ENGINE_ONLY)
    // 暗号化OneWindow製品は、平文のUserDataツリーを持たない。 ゲー
    // ムから参照する前に、埋め込みEngineがこのパス上の配置をUserData.data
    // へ置き換えるため、 外側のSDLホストではディレクトリを実体化しない。
#elif defined(CARDWIRTH_ALL_HOST) && (defined(__EMSCRIPTEN__) || defined(__ANDROID__))
    // Web版ALLはIndexedDBから/userを復元し、Android版はSDL
    // 起動前に同梱のUserData初期データを展開する。 ホームを開くだけで、デ
    // スクトップEngine用のBackup・TempなどをUserData直下へ
    // 作らない。 Editorは開いた際にeditor/Backupやeditor/Temp
    // を作り、 その他の必要な親ディレクトリは各書き込み処理が用意する。
#if defined(__ANDROID__)
    if (requested_root.empty()) {
        // 旧Android版ALLが共有ルートへ作ったフォルダーは
        // 、空の実ディレクトリだけを削除する。 既存のユーザーデー
        // タを移動・削除せず、ExplorerのTrashも残す。
        for (const char* name : {"Backup", "Config", "Database", "Temp", "Cache"}) {
            const auto path = user_data.root / name;
            std::error_code error;
            const std::filesystem::directory_entry entry(path, error);
            if (!error && entry.is_directory(error) && !entry.is_symlink(error) &&
                std::filesystem::is_empty(path, error) && !error)
                (void)std::filesystem::remove(path, error);
        }
    }
#endif
#else
    // デスクトップ・Apple版ALLには管理付きのDataコピーが必要だが、 Editor
    // の作業ルートはUserData/editor配下で別に初期化する。
#if defined(CARDWIRTH_ALL_HOST)
    constexpr bool create_auxiliary_roots = false;
#else
    constexpr bool create_auxiliary_roots = true;
#endif
    if (!user_data.ensure(setup_error, base_directory(), create_auxiliary_roots)) {
        SDL_Log("%s", setup_error.c_str());
        TTF_Quit();
        SDL_Quit();
        return SDL_APP_FAILURE;
    }
#endif
#if defined(__APPLE__)
    const auto exclude_apple_backup = [](const std::filesystem::path& path,
                                         const char* label) {
        // ALLのCache/Tempは、ホーム起動時ではなく実際に使う時点で作る。
        if (!std::filesystem::exists(path)) return;
        if (!cardwirth::app::apple::exclude_from_backup(path))
            SDL_Log("Failed to exclude %s from Apple backup: %s", label,
                    path.string().c_str());
    };
    // Dataは同梱データの複製、Cache/Tempは破棄可能なデータとする。 Yado
    // ・Scenario・Config・Databaseはバックアップ対象に残す。
    exclude_apple_backup(user_data.data, "UserData/Data");
    exclude_apple_backup(user_data.cache, "UserData/Cache");
    exclude_apple_backup(user_data.temp, "UserData/Temp");
    cardwirth::explorer::cleanup_stale_transfers();
#elif defined(__EMSCRIPTEN__)
    cardwirth::explorer::cleanup_stale_transfers();
#endif

    auto state = std::make_unique<RuntimeState>();
    // セキュリティに関係するEngineの選択設定は、共通ホスト
    // へ渡された値をそのまま保つ。 通常のホスト専用引数を組み直
    // す際、埋め込み起動のDBや鍵を既定値へ暗黙に置き換えない。
    for (int index = 1; index < argc; ++index) {
        if (!argv[index]) continue;
        const std::string argument = argv[index];
        if (argument.starts_with("--encrypted-user-data=") ||
            argument.starts_with("--encryption-key=")) {
            state->encrypted_engine_arguments.push_back(argument);
        } else if (argument == "--encrypted-user-data" ||
                   argument == "--encryption-key") {
            state->encrypted_engine_arguments.push_back(argument);
            if (index + 1 < argc && argv[index + 1] &&
                !std::string_view(argv[index + 1]).starts_with("--"))
                state->encrypted_engine_arguments.emplace_back(argv[++index]);
        }
    }
    // Engineのゲーム用引数を保ち、SDLホストで-debug/-yado
    // だけに組み直さない。 値はそのままコピーし、不
    // 正・不足の判定は共通Engineの解析処理へ任
    // せる。 ここに設定規則や検証を重複させない。
    for (int index = 1; index < argc; ++index) {
        if (!argv[index]) continue;
        const std::string argument = argv[index];
        const bool takes_value = argument == "-party" ||
            argument == "-scenario" || argument == "-skin" ||
            argument == "--force-skin";
        const bool flag = argument == "-h" || argument == "--help" ||
            argument == "--debug-skin";
        if (!takes_value && !flag) continue;
        state->forwarded_engine_arguments.push_back(argument);
        if (takes_value && index + 1 < argc && argv[index + 1])
            state->forwarded_engine_arguments.emplace_back(argv[++index]);
    }
    state->nonactive_debug = std::any_of(argv + 1, argv + argc, [](const char* value) {
        return value && std::string_view(value) == "--nonactive-debug";
    });
    state->communication_debug = std::any_of(argv + 1, argv + argc, [](const char* value) {
        return value && std::string_view(value) == "--communication-debug";
    });
    argument_value(argc, argv, "--communication-debug-port", state->communication_debug_port);
    state->force_engine_debug = std::any_of(argv + 1, argv + argc, [](const char* value) {
        return value && std::string_view(value) == "-debug";
    });
    argument_value(argc, argv, "-yado", state->engine_yado);
    if (state->engine_yado.empty()) argument_value(argc, argv, "--yado", state->engine_yado);
    argument_value(argc, argv, "--hosted-test-dialog", state->hosted_test_dialog);
    state->ttf_initialized = true;
    state->smoke_test = std::any_of(argv + 1, argv + argc, [](const char* value) {
        return value && std::string_view(value) == "--smoke-test";
    });
#if defined(CARDWIRTH_ALL_HOST)
    state->hosted_engine_smoke_test = std::any_of(
        argv + 1, argv + argc, [](const char* value) {
            return value && std::string_view(value) == "--hosted-engine-smoke-test";
        });
    state->hosted_status_action_test = std::any_of(
        argv + 1, argv + argc, [](const char* value) {
            return value && std::string_view(value) == "--hosted-status-action-test";
        });
    state->hosted_key_input_test = std::any_of(
        argv + 1, argv + argc, [](const char* value) {
            return value && std::string_view(value) == "--hosted-key-input-test";
        });
    state->hosted_layout_test = std::any_of(
        argv + 1, argv + argc, [](const char* value) {
            return value && std::string_view(value) == "--hosted-layout-test";
        });
    state->editor_zoom_test = std::any_of(
        argv + 1, argv + argc, [](const char* value) {
            return value && std::string_view(value) == "--editor-zoom-test";
        });
    state->smoke_test = state->smoke_test || state->hosted_engine_smoke_test ||
                        state->hosted_status_action_test || state->hosted_key_input_test;
    if (state->hosted_status_action_test)
        state->force_engine_debug = true;
    std::string hosted_smoke_report;
    if (argument_value(argc, argv, "--hosted-engine-smoke-report", hosted_smoke_report))
        state->hosted_engine_smoke_report = std::filesystem::path(hosted_smoke_report);
    std::string hosted_key_input_report;
    if (argument_value(argc, argv, "--hosted-key-input-report", hosted_key_input_report))
        state->hosted_key_input_test_report =
            std::filesystem::path(hosted_key_input_report);
    std::string hosted_status_action_report;
    if (argument_value(argc, argv, "--hosted-status-action-report",
                       hosted_status_action_report))
        state->hosted_status_action_test_report =
            std::filesystem::path(hosted_status_action_report);
    std::string hosted_layout_report;
    if (argument_value(argc, argv, "--hosted-layout-report", hosted_layout_report))
        state->hosted_layout_test_report = std::filesystem::path(hosted_layout_report);
    std::string editor_zoom_report;
    if (argument_value(argc, argv, "--editor-zoom-report", editor_zoom_report))
        state->editor_zoom_test_report = std::filesystem::path(editor_zoom_report);
#endif
    std::string requested_engine;
    argument_value(argc, argv, "--engine-path", requested_engine);
    state->self_path = base_directory() /
                       (argc > 0 && argv[0] ? std::filesystem::path(argv[0]).filename() : "CardWirthPy_C_All");
#if defined(CARDWIRTH_EMBED_ENGINE)
    state->engine_path = state->self_path;
#else
    state->engine_path = find_engine(base_directory(), requested_engine);
#endif
    state->backend = cardwirth::ui::make_headless_backend();
    state->shell = std::make_unique<cardwirth::app::AppShell>(std::move(user_data));
    std::string requested_engine_backend;
    argument_value(argc, argv, "--engine-backend", requested_engine_backend);
    if (requested_engine_backend == "embedded")
        state->shell->set_engine_backend(cardwirth::app::EngineBackend::Embedded);
    else if (requested_engine_backend == "external")
#if defined(CARDWIRTH_ALL_HOST)
        // ALLは一つのホストで動く製品。埋め込み表示を選択したとき、 子
        // プロセスや別の最上位Engineウィンドウへ暗黙に切り替えない。
        state->shell->set_engine_backend(cardwirth::app::EngineBackend::Embedded);
#else
        state->shell->set_engine_backend(cardwirth::app::EngineBackend::ExternalProcess);
#endif
#if defined(CARDWIRTH_EMBED_ENGINE)
    if (requested_engine_backend.empty()) {
        // ALLは同じ実行ファイル内にEngineを持つため、既定で
        // は埋め込み起動を使い、 別のCardWirthPy_C.exe
        // の配置を不要にする。デスクトップは--engine-backend
        // externalで 従来の子プロセスを選べるが、モ
        // バイル・Webには子プロセスの代替経路を設けない。
        state->shell->set_engine_backend(cardwirth::app::EngineBackend::Embedded);
    }
#endif
#if defined(CARDWIRTH_ALL_HOST)
    std::string requested_debug_side;
    if (argument_value(argc, argv, "--debug-side", requested_debug_side)) {
        if (requested_debug_side == "left")
            state->engine_layout_preferences.set_debug_side(
                cardwirth::app::all::DebugDockSide::Left);
        else if (requested_debug_side == "right")
            state->engine_layout_preferences.set_debug_side(
                cardwirth::app::all::DebugDockSide::Right);
        else
            SDL_Log("Ignoring invalid --debug-side value '%s'; expected left or right",
                    requested_debug_side.c_str());
    }
#endif
    try {
        const auto& user_root = state->shell->user_data().root;
        // インストール済み資産は読み込み専用の境界とする。 両方がネイティブの保存先で
        // も、実行時とEditorの書き込みはruntime_storageを通す。
        state->asset_storage = cardwirth::platform::make_read_only_storage(
            cardwirth::platform::make_native_storage(base_directory()));
#if defined(CARDWIRTH_ENGINE_PRIVATE_ENCRYPTION) && defined(CARDWIRTH_ENGINE_ONLY)
        state->runtime_storage = cardwirth::platform::make_native_storage(user_root, false);
#else
        state->runtime_storage = cardwirth::platform::make_native_storage(user_root);
#endif
        auto paths = cardwirth::platform::AppPaths::from_roots(base_directory(), user_root);
        state->runtime_context = std::make_unique<cardwirth::platform::AppRuntimeContext>(
            *state->asset_storage, *state->runtime_storage, std::move(paths));
        state->runtime_context->sdl_initialized = true;
        state->runtime_context->ttf_initialized = state->ttf_initialized;
        state->runtime_context->audio_initialized = true;
#if defined(CARDWIRTH_ALL_HOST)
        load_all_home_settings(*state);
        state->home_install_prompt_pending = home_required_folders_missing;
        std::string requested_editor_height;
        if (argument_value(argc, argv, "--editor-height", requested_editor_height)) {
            int parsed{};
            const auto parsed_result = std::from_chars(
                requested_editor_height.data(),
                requested_editor_height.data() + requested_editor_height.size(), parsed);
            if (parsed_result.ec == std::errc{} &&
                parsed_result.ptr == requested_editor_height.data() + requested_editor_height.size())
                state->editor_logical_height = cardwirth::app::all::clamp_editor_height(parsed);
            else
                SDL_Log("Ignoring invalid --editor-height value '%s'; expected 597..1600",
                        requested_editor_height.c_str());
        }
#endif
#if defined(CARDWIRTH_EMBED_ENGINE)
        // 埋め込みEngineはowns_logger=falseとし、SDLホストがロガーを一
        // 度だけ初期化する。 ホームとEngineの切り替えのたびにログを切り詰めない。 明示的
        // なUserDataを使うEngine起動と同じく、診断記録もUserDataへ置く。
        cardwirth::debug::set_nonactive_automation_enabled(state->nonactive_debug);
#if !defined(CARDWIRTH_ENGINE_PRIVATE_ENCRYPTION) || !defined(CARDWIRTH_ENGINE_ONLY)
        cardwirth::debug::initialize_logger(user_root);
        state->owns_engine_logger = true;
        cardwirth::debug::log("ALL host logger initialized");
#endif
#endif
        auto* runtime_state = state.get();
        const std::weak_ptr<std::atomic_bool> callback_lifetime =
            state->callback_lifetime;
        state->runtime_context->report_error =
            [runtime_state, callback_lifetime](std::string_view message) {
            const auto alive = callback_lifetime.lock();
            if (!runtime_state || !alive ||
                !alive->load(std::memory_order_acquire)) return;
            std::scoped_lock lock(runtime_state->pending_status_mutex);
            runtime_state->pending_status_messages.emplace_back(message);
        };
    } catch (const std::exception& error) {
        SDL_Log("Runtime context initialization failed: %s", error.what());
        // StorageはEngine・Editor・Explorerの
        // 共通のデータ境界。 ないまま続けると画面が部分的にしか使えず、
        // 書き込みが意図しない代替先へ流れる場合があるため、 どのOS
        // でもappstateを公開する前にホストの起動を失敗させる。
        return SDL_APP_FAILURE;
    }
#if defined(CARDWIRTH_ENGINE_ONLY)
    // AndroidのEngineは単一製品のホストとする。SDL_AppInit
    // は処理をブロックせず、 最初の描画フレームより前に埋め込みEngineを
    // 起動する。 Windows以外でPython互換のダイアログを表示する経
    // 路はこれに統一し、 単独起動用のネイティブ表示への代替経路は設けない。
    state->shell->set_initial_page(cardwirth::app::Page::Home);
    state->launch_engine_on_start = true;
#elif defined(CARDWIRTH_EDITOR_ONLY)
    state->shell->set_initial_page(cardwirth::app::Page::XEditor);
#elif defined(CARDWIRTH_EXPLORER_ONLY)
    state->shell->set_initial_page(cardwirth::app::Page::DataExplorer);
#else
    // デスクトップの診断条件を揃えるとともに、 スモークテストでホー
    // ムだけでなく埋め込みExplorerも確認できるようにする。
    std::string requested_initial_page;
    argument_value(argc, argv, "--initial-page", requested_initial_page);
    if (requested_initial_page == "explorer")
        state->shell->set_initial_page(cardwirth::app::Page::DataExplorer);
    else if (requested_initial_page == "engine") {
        state->shell->set_initial_page(cardwirth::app::Page::Home);
        state->launch_engine_on_start = !state->smoke_test
#if defined(CARDWIRTH_ALL_HOST)
            || state->hosted_engine_smoke_test || state->hosted_status_action_test ||
               state->hosted_key_input_test
#endif
            ;
    }
    else if (requested_initial_page == "editor") {
        state->shell->set_initial_page(cardwirth::app::Page::Home);
        // 統合検証用の起動で使う。スモークテストはモーダル状態にしない。
        state->launch_editor_on_start = !state->smoke_test;
    }
    else
        state->shell->set_initial_page(cardwirth::app::Page::Home);
#endif

#if defined(__ANDROID__)
    // Androidアプリには/tmpもTMPDIRもないため、アーカイブ
    // 展開・プレビュー・シナリオ読み込みなどの std::filesystem::temp_directory_path()
    // が失敗する。 各処理の開始前に一度だけ、TMPDIR
    // をアプリ専用キャッシュへ設定する。
    if (const char* cache = SDL_GetAndroidCachePath(); cache && *cache)
        (void)SDL_setenv_unsafe("TMPDIR", cache, 1);
#endif
#if defined(__ANDROID__) || defined(CARDWIRTH_IOS)
    // モバイルアプリは左右いずれかの横向きに限定する。Info.plist
    // とAndroidの マニフェスト・Activityでも同じ指
    // 定を行う。SDLは画面の向きを適用し直す際もこの設定を読む。
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
#endif
    if (!state->smoke_test) {
        // WSLgは新しい画面を表示してもRemoteAppをアクティブ化しな
        // い場合がある。 SDL_ShowWindowとSDL_RaiseWindow
        // の両方でアクティブ化できるよう、ウィンドウ作成前に設定する。
        SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, "1");
        SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_RAISED, "1");
    }
    const auto flags = static_cast<SDL_WindowFlags>(SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY
#if defined(CARDWIRTH_ALL_HOST) && defined(__ANDROID__)
                                                     | SDL_WINDOW_FULLSCREEN
#endif
                                                     | (state->smoke_test ? SDL_WINDOW_HIDDEN : 0));
#if defined(CARDWIRTH_EXPLORER_ONLY)
    const char* window_title = "DataExplorer_C";
#elif defined(CARDWIRTH_EDITOR_ONLY)
    const char* window_title = "XEditor_C";
#else
    const char* window_title = "CardWirthPy_C";
#endif
    const auto [initial_width, initial_height] = initial_window_size();
    if (!SDL_CreateWindowAndRenderer(window_title, initial_width, initial_height, flags,
                                     &state->window, &state->renderer)) {
        SDL_Log("Window creation failed: %s", SDL_GetError());
        state.reset();
        SDL_Quit();
        return SDL_APP_FAILURE;
    }
#if defined(CARDWIRTH_ALL_HOST) && !defined(__EMSCRIPTEN__) && \
    !defined(__ANDROID__) && !defined(CARDWIRTH_IOS)
    // ステータスダイアログはALL内のEngine左側へ配置す
    // るが、物理的なALLホスト自体は動かさない。 メインウ
    // ィンドウは常に選択されたデスクトップ画面の中央に作る。
    SDL_SetWindowPosition(state->window, SDL_WINDOWPOS_CENTERED,
                          SDL_WINDOWPOS_CENTERED);
#endif
    if (!state->smoke_test) {
        // SDLの通常ウィンドウは既定で表示されるが、WSLg
        // のRemoteApp連携では 新しいWayland
        // 画面がデスクトップへ出ない場合があるため、明示的に
        // 表示する。 GUIでない端末から起動すると初回の入
        // 力フォーカスがない場合もあるので、 ここでも最初
        // の描画後にも前面化・アクティブ化し、Alt+Tab
        // まで初期画面が空白のままにならないようにする。
        SDL_ShowWindow(state->window);
        SDL_RaiseWindow(state->window);
    }
#if defined(CARDWIRTH_ALL_HOST) && !defined(__EMSCRIPTEN__) && \
    !defined(__ANDROID__) && !defined(CARDWIRTH_IOS)
    const auto window_config = cardwirth::app::all::make_native_pc_window_config();
    SDL_SetWindowMinimumSize(state->window, window_config.minimum_width,
                             window_config.minimum_height);
    state->native_aspect_ratio_active = window_config.aspect_ratio &&
        SDL_SetWindowAspectRatio(state->window, *window_config.aspect_ratio,
                                 *window_config.aspect_ratio);
    if (!state->native_aspect_ratio_active)
        SDL_Log("Native ALL aspect lock unavailable; resize fallback is active: %s",
                SDL_GetError());
#endif
#if !defined(CARDWIRTH_ALL_HOST) || \
    (!defined(__EMSCRIPTEN__) && !defined(__ANDROID__) && !defined(CARDWIRTH_IOS))
    SDL_SetRenderLogicalPresentation(state->renderer, kWindowWidth, kWindowHeight,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);
#endif
    state->text_renderer = std::make_unique<TextRenderer>();
    if (!state->text_renderer->initialize(state->renderer))
        SDL_Log("Japanese system font could not be initialized; using debug text");
#if defined(CARDWIRTH_ALL_HOST)
    refresh_all_layout(*state);
    if (state->smoke_test
#if defined(__EMSCRIPTEN__)
        && false
#endif
#if defined(__ANDROID__) || defined(CARDWIRTH_IOS)
        && false
#endif
    ) {
        int client_width = 0;
        int client_height = 0;
        if (!SDL_GetWindowSize(state->window, &client_width, &client_height) ||
            client_width != cardwirth::app::all::kAllPcDesignWidth ||
            client_height != cardwirth::app::all::kAllPcDesignHeight) {
            state->smoke_failed = true;
            SDL_Log("ALL initial client size mismatch: actual=%dx%d expected=%dx%d",
                    client_width, client_height,
                    cardwirth::app::all::kAllPcDesignWidth,
                    cardwirth::app::all::kAllPcDesignHeight);
        }
    }
#endif
    configure_page(*state);
    state->started_at = SDL_GetTicks();
    *appstate = state.release();
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    auto& state = *static_cast<RuntimeState*>(appstate);
#if defined(CARDWIRTH_ALL_HOST)
    if (event->type == SDL_EVENT_MOUSE_MOTION)
        state.dialog_hover_by_mouse = event->motion.which != SDL_TOUCH_MOUSEID;
    else if (event->type == SDL_EVENT_FINGER_DOWN || event->type == SDL_EVENT_FINGER_MOTION)
        state.dialog_hover_by_mouse = false;
    // ページ切り替えの読み込み中は、クリック・タップ・キー入力で別の操作を始
    // めない。 ウィンドウ・フォーカス・終了イベントは通常どおり処理する。
    if (host_busy(state)) {
        switch (event->type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP:
        case SDL_EVENT_MOUSE_MOTION: case SDL_EVENT_MOUSE_WHEEL:
        case SDL_EVENT_FINGER_DOWN: case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_MOTION: case SDL_EVENT_FINGER_CANCELED:
        case SDL_EVENT_PEN_DOWN: case SDL_EVENT_PEN_UP: case SDL_EVENT_PEN_MOTION:
        case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP:
        case SDL_EVENT_TEXT_INPUT: case SDL_EVENT_TEXT_EDITING:
            return SDL_APP_CONTINUE;
        default: break;
        }
    }
#endif
    if (event->type == SDL_EVENT_QUIT) {
        if (state.explorer_modal.kind() ==
                cardwirth::explorer::ExplorerModalKind::TextEditor &&
            state.explorer_modal.dirty()) {
            // OSからの終了もEscapeと同じデータ保護規則に従う
            // 。 未保存のExplorer文書は保存するか明示的に
            // キャンセルしてから、プロセス全体の終了処理を始める。
            state.explorer_modal_error =
                "未保存の変更があります。保存またはキャンセルしてから終了してください。";
            configure_page(state);
            return SDL_APP_CONTINUE;
        }
#if defined(CARDWIRTH_ALL_HOST)
        if (state.host_close_phase == RuntimeState::HostClosePhase::Idle) {
            state.host_close_origin = state.shell->page();
            state.host_close_shell = *state.shell;
            state.host_close_phase = RuntimeState::HostClosePhase::Requested;
        }
        // Editorの入れ子のモーダル処理中には、セッションを破棄しない。
        return SDL_APP_CONTINUE;
#else
        return SDL_APP_SUCCESS;
#endif
    }
#if defined(CARDWIRTH_ALL_HOST)
    if (event->type == SDL_EVENT_RENDER_DEVICE_RESET &&
        (!SDL_GetWindowFromEvent(event) || SDL_GetWindowFromEvent(event) == state.window)) {
        invalidate_all_renderer_textures(state);
        if (state.shell->page() == cardwirth::app::Page::DataExplorer)
            configure_page(state); // 保持中の画像プレビューも作り直す。
        return SDL_APP_CONTINUE;
    }
#endif
#if defined(CARDWIRTH_ALL_HOST) && defined(_WIN32)
    // Windowsは一連のサイズ変更で、論理寸法・ピクセル寸法
    // ・再表示をまとめて通知する。 各通知でEditorの描画先
    // を再作成せず、描画または次の入力の前に一度だけ配置し直す。
    const bool resize_notice = event->type == SDL_EVENT_WINDOW_RESIZED ||
        event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
        event->type == SDL_EVENT_WINDOW_SAFE_AREA_CHANGED ||
        event->type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED ||
        event->type == SDL_EVENT_DISPLAY_CONTENT_SCALE_CHANGED ||
        event->type == SDL_EVENT_WINDOW_EXPOSED;
    if (!resize_notice) flush_all_layout_resize(state);
#endif
#if !defined(CARDWIRTH_ALL_HOST)
    // 単独起動のExplorerも同じプレビューサービスを使うが、後続のホストページ
    // のライフサイクルは持たない。 ここでもOSによる表示状態の理由は独立して扱う。
    using PreviewVisibility = cardwirth::platform::AppVisibility;
    const bool preview_background = event->type == SDL_EVENT_WILL_ENTER_BACKGROUND ||
        event->type == SDL_EVENT_DID_ENTER_BACKGROUND || event->type == SDL_EVENT_TERMINATING;
    const bool preview_foreground = event->type == SDL_EVENT_DID_ENTER_FOREGROUND;
    const bool preview_hide = (event->type == SDL_EVENT_WINDOW_HIDDEN ||
        event->type == SDL_EVENT_WINDOW_MINIMIZED) && SDL_GetWindowFromEvent(event) == state.window;
    const bool preview_show = (event->type == SDL_EVENT_WINDOW_SHOWN ||
        event->type == SDL_EVENT_WINDOW_RESTORED || event->type == SDL_EVENT_WINDOW_MAXIMIZED) &&
        SDL_GetWindowFromEvent(event) == state.window;
    if (preview_background || preview_foreground || preview_hide || preview_show) {
        const auto reason = preview_background || preview_foreground ? PreviewVisibility::Background :
            event->type == SDL_EVENT_WINDOW_MINIMIZED || event->type == SDL_EVENT_WINDOW_RESTORED ||
                event->type == SDL_EVENT_WINDOW_MAXIMIZED ? PreviewVisibility::Minimized : PreviewVisibility::Hidden;
        if (state.app_visibility.set(reason, preview_background || preview_hide)) {
            state.app_in_background = state.app_visibility.suspended();
            if (state.explorer)
                state.explorer->controller().set_audio_suspended(state.app_in_background);
        }
    }
#endif
#if defined(CARDWIRTH_ALL_HOST)
    const bool host_window_suspended =
        (event->type == SDL_EVENT_WINDOW_MINIMIZED || event->type == SDL_EVENT_WINDOW_HIDDEN) &&
        SDL_GetWindowFromEvent(event) == state.window;
    const bool host_window_resumed =
        (event->type == SDL_EVENT_WINDOW_RESTORED || event->type == SDL_EVENT_WINDOW_MAXIMIZED ||
         event->type == SDL_EVENT_WINDOW_SHOWN) &&
        SDL_GetWindowFromEvent(event) == state.window;
    using Visibility = cardwirth::platform::AppVisibility;
    const bool entering_background = event->type == SDL_EVENT_WILL_ENTER_BACKGROUND ||
        event->type == SDL_EVENT_DID_ENTER_BACKGROUND || event->type == SDL_EVENT_TERMINATING;
    const bool entering_foreground = event->type == SDL_EVENT_DID_ENTER_FOREGROUND;
    if (entering_background || entering_foreground || host_window_suspended || host_window_resumed) {
        const auto reason = entering_background || entering_foreground ? Visibility::Background :
            event->type == SDL_EVENT_WINDOW_MINIMIZED || event->type == SDL_EVENT_WINDOW_RESTORED ||
                event->type == SDL_EVENT_WINDOW_MAXIMIZED
                ? Visibility::Minimized : Visibility::Hidden;
        if (!state.app_visibility.set(reason, entering_background || host_window_suspended))
            return SDL_APP_CONTINUE;
    }
    if (entering_background || host_window_suspended) {
        state.app_in_background = true;
        if (state.explorer) state.explorer->controller().set_audio_suspended(true);
        if (state.hosted_editor && state.hosted_editor->running() &&
            state.shell->page() == cardwirth::app::Page::XEditor) {
            // 外側のALLループがアプリのライフサイクルイベントを消費する
            // ため、ホストの指の状態を消去する前に Editorのフォーカ
            // スとキャプチャを明示的に解除する。 モバイルのアプリ切り替え
            // 後に、押下中の部品や変換中の文字入力を残さないようにする。
            SDL_Event lost{};
            lost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
            lost.window.windowID = SDL_GetWindowID(state.window);
            state.hosted_editor->dispatch_native_event(&lost);
        }
        if (state.hosted_editor && state.hosted_editor->running())
            state.hosted_editor->enter_background();
        state.editor_fingers.clear();
        state.editor_pinch_active = false;
        state.pending_release_button.reset();
        state.finger_tracking = false;
        state.finger_scrolled = false;
        state.finger_selecting = false;
        state.finger_start_row.reset();
        state.finger_last_selection_row.reset();
        if (state.hosted_engine) {
            cancel_hosted_engine_pointer(state);
            state.hosted_engine->focus(false);
            state.hosted_engine->suspend();
        }
        return SDL_APP_CONTINUE;
    }
    if (event->type == SDL_EVENT_DID_ENTER_FOREGROUND || host_window_resumed) {
        state.app_in_background = false;
        if (state.explorer) state.explorer->controller().set_audio_suspended(
            state.shell->page() != cardwirth::app::Page::DataExplorer);
        refresh_all_layout(state);
        if (state.hosted_engine) {
            state.hosted_engine->resume();
            state.hosted_engine->focus(state.shell->page() == cardwirth::app::Page::CardWirthPy);
        }
        if (state.hosted_editor) {
            if (state.hosted_editor->running())
                state.hosted_editor->enter_foreground();
            state.hosted_editor->constrain_to_host();
            if (state.hosted_editor->running() &&
                state.shell->page() == cardwirth::app::Page::XEditor) {
                SDL_Event gained{};
                gained.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
                gained.window.windowID = SDL_GetWindowID(state.window);
                state.hosted_editor->dispatch_native_event(&gained);
            }
        }
        return SDL_APP_CONTINUE;
    }
    if (event->type == SDL_EVENT_LOW_MEMORY) {
        if (state.hosted_editor && state.hosted_editor->running())
            state.hosted_editor->memory_pressure();
        SDL_DestroyTexture(state.preview_texture);
        state.preview_texture = nullptr;
        SDL_DestroyTexture(state.hosted_engine_texture);
        state.hosted_engine_texture = nullptr;
        SDL_DestroyTexture(state.hosted_debugger_texture);
        state.hosted_debugger_texture = nullptr;
        state.hosted_debugger_texture_sequence = 0;
        SDL_DestroyTexture(state.hosted_editor_canvas);
        state.hosted_editor_canvas = nullptr;
        state.hosted_engine_texture_width = 0;
        state.hosted_engine_texture_height = 0;
        state.hosted_engine_frame_sequence = 0;
        clear_hosted_dialog_texture(state);
        if (state.text_renderer) state.text_renderer->clear_cache();
        if (state.explorer && state.explorer->controller().preview())
            state.explorer->close_preview();
        if (state.hosted_engine) state.hosted_engine->trim_memory();
        return SDL_APP_CONTINUE;
    }
    if (event->type == SDL_EVENT_WINDOW_MAXIMIZED) {
#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__) && !defined(CARDWIRTH_IOS)
        // 設計寸法kAllPcDesignWidth:kAllPcDesignHeight
        // の縦横比で収まる最大サイズを中央配置する。
        SDL_Rect work_area{};
        const SDL_DisplayID display = SDL_GetDisplayForWindow(state.window);
        if (display && SDL_GetDisplayUsableBounds(display, &work_area)) {
            const auto target = cardwirth::app::all::largest_native_pc_size(
                work_area.w, work_area.h);
            if (target.width > 0 && target.height > 0) {
                SDL_RestoreWindow(state.window);
                SDL_SetWindowSize(state.window, target.width, target.height);
                SDL_SetWindowPosition(
                    state.window,
                    work_area.x + (work_area.w - target.width) / 2,
                    work_area.y + (work_area.h - target.height) / 2);
            }
        }
#endif
        request_all_layout_resize(state);
    } else if (event->type == SDL_EVENT_WINDOW_RESIZED) {
        if (state.engine_pointer_capture.active()) cancel_hosted_engine_pointer(state);
        const int width = event->window.data1;
        const int height = event->window.data2;
        // Editorは利用可能な高さを倍率の基準とし、幅は自
        // 由に変えられる。 この規則はデスクトップ・モバイル
        // ・Webで共通。 下の縦横比補正は、固定配置のホー
        // ム・Engine・Explorerだけに適用する。
#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__) && !defined(CARDWIRTH_IOS)
        if (state.shell->page() != cardwirth::app::Page::XEditor) {
            if (const auto correction = state.aspect_resize_guard.correction(width, height))
                SDL_SetWindowSize(state.window, correction->width, correction->height);
        }
#else
        (void)width;
        (void)height;
#endif
        request_all_layout_resize(state);
    } else if (event->type == SDL_EVENT_DISPLAY_ORIENTATION) {
        // モバイル・Webでは、2回目のウィンドウサイズ変更がな
        // くても向きの変更が通知される場合がある。 どちらの通知
        // でも、短い安定待ちの後に現在の描画出力からALLを再
        // 計算する。 JS/Java層は変更を検知するだけとし
        // 、確定した配置と再描画はこのC++側で一度だけ行う。
        request_all_layout_resize(state, true);
    } else if (event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
               event->type == SDL_EVENT_WINDOW_SAFE_AREA_CHANGED ||
               event->type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED ||
               event->type == SDL_EVENT_DISPLAY_CONTENT_SCALE_CHANGED) {
        ::cw::lib::ui::set_system_dialog_scale(SDL_GetWindowDisplayScale(state.window));
        if (state.engine_pointer_capture.active()) cancel_hosted_engine_pointer(state);
        request_all_layout_resize(state);
    }
#endif
    if (state.shell->suspended() && state.shell->page() != cardwirth::app::Page::DataExplorer)
        return SDL_APP_CONTINUE;
#if defined(CARDWIRTH_ALL_HOST)
    const bool hosted_engine_page =
        state.shell->page() == cardwirth::app::Page::CardWirthPy &&
        state.hosted_engine && state.hosted_engine->running();
    if (!hosted_engine_page && state.engine_pointer_capture.active())
        cancel_hosted_engine_pointer(state);
    if (hosted_engine_page) {
        if (hosted_dialog_release_gate(state, *event)) return SDL_APP_CONTINUE;
        // 操作の確定と取消は、実際の指の解放／キャンセルイベントで
        // 扱う。合成マウスイベントはタップ回数の取得にだけ使う。
        if(event->type==SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.which==SDL_TOUCH_MOUSEID &&
           !state.engine_pointer_capture.active())
            state.hosted_touch_clicks=std::max(1,static_cast<int>(event->button.clicks));
        // タッチが所有する保留中の操作対象を、マウスイベントで置き換えない。
        if (state.engine_pointer_capture.active() &&
            (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN || event->type == SDL_EVENT_MOUSE_BUTTON_UP ||
             event->type == SDL_EVENT_MOUSE_MOTION || event->type == SDL_EVENT_MOUSE_WHEEL))
            return SDL_APP_CONTINUE;
        if (event->type == SDL_EVENT_FINGER_DOWN) {
            if (state.engine_pointer_capture.active()) return SDL_APP_CONTINUE;
            // 通常の連続タップで、既にキューに入っているUPを取り消さない。
            const bool displaced_control = state.hosted_dialog_pointer.active() ||
                state.pending_debugger_control.has_value() || static_cast<bool>(state.pending_release_button);
            const int clicks=state.hosted_touch_clicks;
            cancel_hosted_engine_pointer(state, displaced_control);
            state.hosted_touch_clicks=clicks;
            state.engine_pointer_capture.begin(event->tfinger.touchID, event->tfinger.fingerID);
        } else if (event->type == SDL_EVENT_FINGER_MOTION ||
                   event->type == SDL_EVENT_FINGER_UP || event->type == SDL_EVENT_FINGER_CANCELED) {
            if (!state.engine_pointer_capture.owns(event->tfinger.touchID, event->tfinger.fingerID))
                return SDL_APP_CONTINUE;
            if (event->type == SDL_EVENT_FINGER_CANCELED) {
                cancel_hosted_engine_pointer(state);
                return SDL_APP_CONTINUE;
            }
            if (event->type == SDL_EVENT_FINGER_UP)
                state.engine_pointer_capture.end(event->tfinger.touchID, event->tfinger.fingerID);
        }
    }
    // ドロップ先はメイン画面の場合もあるため、Engineページの
    // 表示中は ダイアログが開いていなくても対象パスを保持する。
    if (!hosted_engine_page) state.pending_dialog_drop_files.clear();
    if (!hosted_engine_page || !state.hosted_engine->modal_snapshot())
        state.engine_dialog_cursor.clear();
    if(state.shell->page()==cardwirth::app::Page::DataExplorer&&
        (event->type==SDL_EVENT_DROP_BEGIN||event->type==SDL_EVENT_DROP_FILE||event->type==SDL_EVENT_DROP_COMPLETE)){
        if(state.explorer_modal.active()||state.transfer_picker_active)return SDL_APP_CONTINUE;
        if(event->type==SDL_EVENT_DROP_BEGIN)state.explorer_drop_batch.clear();
        else if(event->type==SDL_EVENT_DROP_FILE&&event->drop.data)
            state.explorer_drop_batch.push_back(std::filesystem::u8path(event->drop.data));
        else if(event->type==SDL_EVENT_DROP_COMPLETE&&!state.explorer_drop_batch.empty()){
            state.explorer_drop_paths=std::move(state.explorer_drop_batch);
            state.explorer_drop_batch.clear();
            state.explorer_modal.show_upload_type();
            state.explorer_modal_error.clear();
            configure_page(state);
        }
        return SDL_APP_CONTINUE;
    }
    if (hosted_engine_page && !state.editor_home_confirmation_active &&
        (event->type == SDL_EVENT_DROP_BEGIN || event->type == SDL_EVENT_DROP_FILE ||
         event->type == SDL_EVENT_DROP_COMPLETE)) {
        // ドロップのデータはSDLからの借用なので、完了するまでパスをコピ
        // ーして保持する。 共通ダイアログはファイルごとではなく、操作全
        // 体で一度だけ呼ぶ。 PyのFrameと同様にメイン画面でもスキ
        // ン変換・スキンやシナリオの導入を受け付け、 ダイアログがない場
        // 合はすべてのホストでFrame.OnDropFilesへ送る。
        if (event->type == SDL_EVENT_DROP_BEGIN)
            state.pending_dialog_drop_files.clear();
        else if (event->type == SDL_EVENT_DROP_FILE && event->drop.data && *event->drop.data)
            state.pending_dialog_drop_files.emplace_back(event->drop.data);
        else if (event->type == SDL_EVENT_DROP_COMPLETE) {
            auto paths = std::move(state.pending_dialog_drop_files);
            state.pending_dialog_drop_files.clear();
            state.hosted_engine->dialog_drop_files(std::move(paths));
        }
        return SDL_APP_CONTINUE;
    }
    // SDL_TOUCH_MOUSEIDを除外しない限り、SDLはすべてのタ
    // ッチ操作にマウスイベントを合成する。 ALLは後続で指のイベントを処理
    // するため、両方を転送するとAndroidの1回のタップが Engine
    // へ2回のクリックとして届く。特にステータスバーで重複を防ぐ必要がある。
    const bool synthetic_touch_mouse =
        ((event->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
          event->type == SDL_EVENT_MOUSE_BUTTON_UP) &&
         event->button.which == SDL_TOUCH_MOUSEID) ||
        (event->type == SDL_EVENT_MOUSE_MOTION &&
         event->motion.which == SDL_TOUCH_MOUSEID) ||
        (event->type == SDL_EVENT_MOUSE_WHEEL &&
         event->wheel.which == SDL_TOUCH_MOUSEID);
    if (synthetic_touch_mouse) {
#if defined(__ANDROID__)
        if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
            event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
            SDL_Log("CardWirthInput Android dropped synthetic touch mouse phase=%s button=%u",
                    event->type == SDL_EVENT_MOUSE_BUTTON_DOWN ? "down" : "up",
                    static_cast<unsigned>(event->button.button));
        }
#endif
        return SDL_APP_CONTINUE;
    }
    if (state.home_initial_install_active && state.shell->page() == cardwirth::app::Page::Home &&
        (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP ||
         event->type == SDL_EVENT_TEXT_INPUT || event->type == SDL_EVENT_TEXT_EDITING)) {
        if (!state.home_package_install_busy && event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat &&
            (event->key.key == SDLK_RETURN || event->key.key == SDLK_KP_ENTER ||
             event->key.key == SDLK_ESCAPE || event->key.key == SDLK_AC_BACK)) {
            finish_home_initial_install(state);
        }
        return SDL_APP_CONTINUE;
    }
    if (state.home_landscape_action && state.shell->page() == cardwirth::app::Page::Home &&
        (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP ||
         event->type == SDL_EVENT_TEXT_INPUT || event->type == SDL_EVENT_TEXT_EDITING)) {
        if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat &&
            (event->key.key == SDLK_ESCAPE || event->key.key == SDLK_AC_BACK)) {
            state.home_landscape_action = {};
            configure_page(state);
        }
        return SDL_APP_CONTINUE;
    }
    if (state.home_install_prompt_active && state.shell->page() == cardwirth::app::Page::Home &&
        (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP ||
         event->type == SDL_EVENT_TEXT_INPUT || event->type == SDL_EVENT_TEXT_EDITING)) {
        if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat) {
            if (event->key.key == SDLK_ESCAPE || event->key.key == SDLK_AC_BACK || event->key.key == SDLK_N)
                finish_home_install_prompt(state, false);
            else if (event->key.key == SDLK_RETURN || event->key.key == SDLK_KP_ENTER || event->key.key == SDLK_Y)
                finish_home_install_prompt(state, true);
        }
        return SDL_APP_CONTINUE;
    }
    if (state.editor_home_confirmation_active &&
        (state.shell->page() == cardwirth::app::Page::XEditor || hosted_engine_page) &&
        (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP ||
         event->type == SDL_EVENT_TEXT_INPUT || event->type == SDL_EVENT_TEXT_EDITING)) {
        if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat) {
            if (event->key.key == SDLK_ESCAPE || event->key.key == SDLK_AC_BACK)
                finish_home_confirmation(state, false);
            else if (event->key.key == SDLK_RETURN || event->key.key == SDLK_KP_ENTER)
                finish_home_confirmation(state, true);
        }
        return SDL_APP_CONTINUE;
    }
    if (hosted_engine_page &&
        (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP)) {
        // ホストのショートカットでEngine用のキーを奪わない。 ホー
        // ムへ戻る操作は、解放時に実行する重ね表示のボタンから提供する。
        if (state.hosted_debugger_focused &&
            state.hosted_engine->debugger_snapshot().visible &&
            !state.hosted_engine->modal_snapshot()) {
            if (const auto key = hosted_debugger_key(event->key.key))
                state.hosted_engine->debugger_key(
                    *key, event->type == SDL_EVENT_KEY_DOWN,
                    hosted_modifiers(event->key.mod));
        } else if (const auto modal = state.hosted_engine->modal_snapshot()) {
            if (event->key.key == SDLK_LEFT || event->key.key == SDLK_RIGHT ||
                event->key.key == SDLK_UP || event->key.key == SDLK_DOWN) {
                auto key = cardwirth::app::HostedEngineDialogKey::Left;
                if (event->key.key == SDLK_RIGHT)
                    key = cardwirth::app::HostedEngineDialogKey::Right;
                else if (event->key.key == SDLK_UP)
                    key = cardwirth::app::HostedEngineDialogKey::Up;
                else if (event->key.key == SDLK_DOWN)
                    key = cardwirth::app::HostedEngineDialogKey::Down;
                state.hosted_engine->dialog_key(
                    key,
                    event->type == SDL_EVENT_KEY_DOWN,
                    hosted_modifiers(event->key.mod));
            } else if (event->type == SDL_EVENT_KEY_DOWN) {
                const bool repeated_commit = event->key.repeat &&
                    (event->key.key == SDLK_ESCAPE || event->key.key == SDLK_AC_BACK ||
                     event->key.key == SDLK_RETURN || event->key.key == SDLK_KP_ENTER);
                if (repeated_commit) {
                    // モーダル中のキーリピートは消費し、次のモデルまで閉じないようにする。
                }
                else if (::cw::lib::engine_sdl_cancel(event->key.key, true, event->key.repeat))
                    state.hosted_engine->dialog_key(
                        cardwirth::app::HostedEngineDialogKey::Escape, true, hosted_modifiers(event->key.mod));
                else if (event->key.key == SDLK_RETURN || event->key.key == SDLK_KP_ENTER)
                    state.hosted_engine->dialog_key(
                        cardwirth::app::HostedEngineDialogKey::Enter, true, hosted_modifiers(event->key.mod));
                else if (event->key.key == SDLK_UP)
                    state.hosted_engine->dialog_action(cardwirth::app::HostedEngineDialogAction::Previous);
                else if (event->key.key == SDLK_DOWN)
                    state.hosted_engine->dialog_action(cardwirth::app::HostedEngineDialogAction::Next);
                else if (const auto key = hosted_debugger_key(event->key.key))
                    state.hosted_engine->dialog_key(*key, true, hosted_modifiers(event->key.mod));
            }
        } else if (!state.all_dialog_layer.native_engine_dialog_active())
            state.hosted_engine->key(
                hosted_native_key(event->key.key), event->type == SDL_EVENT_KEY_DOWN,
                hosted_modifiers(event->key.mod));
        return SDL_APP_CONTINUE;
    }
    if (hosted_engine_page && event->type == SDL_EVENT_TEXT_INPUT) {
        if (const auto modal = state.hosted_engine->modal_snapshot();
            modal && modal->wants_text_input)
            state.hosted_engine->text_input(event->text.text ? event->text.text : "");
        else if(!modal && state.hosted_debugger_focused &&
                state.hosted_engine->debugger_snapshot().wants_text_input)
            state.hosted_engine->debugger_text(event->text.text ? event->text.text : "",
                false,-1,0,hosted_modifiers(SDL_GetModState()));
        return SDL_APP_CONTINUE;
    }
    if (hosted_engine_page && event->type == SDL_EVENT_TEXT_EDITING) {
        if (const auto modal = state.hosted_engine->modal_snapshot();
            modal && modal->wants_text_input)
            state.hosted_engine->text_editing(
                event->edit.text ? event->edit.text : "", event->edit.start, event->edit.length);
        else if(!modal && state.hosted_debugger_focused &&
                state.hosted_engine->debugger_snapshot().wants_text_input)
            state.hosted_engine->debugger_text(
                event->edit.text ? event->edit.text : "",true,event->edit.start,event->edit.length,
                hosted_modifiers(SDL_GetModState()));
        return SDL_APP_CONTINUE;
    }
    if (hosted_engine_page && event->type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
        state.hosted_engine->focus(true);
        return SDL_APP_CONTINUE;
    }
    if (hosted_engine_page && event->type == SDL_EVENT_WINDOW_FOCUS_LOST) {
        // 共通モデルはFocus(false)でキャプチャを解除する。ホスト側のク
        // リック候補も破棄し、 遅れて届くUPでダイアログが確定しないようにする。
        cancel_hosted_engine_pointer(state);
        state.hosted_engine->debugger_blur();
        state.hosted_engine->focus(false);
        return SDL_APP_CONTINUE;
    }
    const bool hosted_editor_input_page =
        state.shell->page() == cardwirth::app::Page::XEditor &&
        state.hosted_editor && state.hosted_editor->running();
    if (hosted_editor_input_page &&
        (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP ||
         event->type == SDL_EVENT_TEXT_INPUT || event->type == SDL_EVENT_TEXT_EDITING ||
         event->type == SDL_EVENT_WINDOW_FOCUS_GAINED ||
         event->type == SDL_EVENT_WINDOW_FOCUS_LOST)) {
        state.hosted_editor->dispatch_native_event(event);
        return SDL_APP_CONTINUE;
    }
#endif
    // アップロードは一つの処理単位として扱う。ネイティブの選択画面
    // が戻った後も、 UserDataへの取り込みが完了するまでア
    // ップロードのモーダル状態を保つ。 Androidは非同期の選
    // 択画面、他の環境は既存の実装を使うが、この扱いは共通にする。
    if (state.transfer_picker_active && state.explorer_modal.active() &&
        (state.explorer_modal.kind() == cardwirth::explorer::ExplorerModalKind::ChooseUploadType ||
         state.explorer_modal.kind() == cardwirth::explorer::ExplorerModalKind::DownloadProgress) &&
        (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP ||
         event->type == SDL_EVENT_TEXT_INPUT || event->type == SDL_EVENT_TEXT_EDITING ||
         event->type == SDL_EVENT_MOUSE_MOTION || event->type == SDL_EVENT_MOUSE_WHEEL ||
         event->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
         event->type == SDL_EVENT_MOUSE_BUTTON_UP ||
         event->type == SDL_EVENT_FINGER_DOWN || event->type == SDL_EVENT_FINGER_MOTION ||
         event->type == SDL_EVENT_FINGER_UP || event->type == SDL_EVENT_FINGER_CANCELED)) {
        state.pending_release_button.reset();
        state.finger_tracking = false;
        return SDL_APP_CONTINUE;
    }
    if (state.explorer_modal.kind() ==
            cardwirth::explorer::ExplorerModalKind::ExtractProgress) {
        state.pending_release_button.reset();
        state.finger_tracking = false;
        state.finger_scrolled = false;
        state.finger_selecting = false;
        state.finger_start_row.reset();
        state.finger_last_selection_row.reset();
        return SDL_APP_CONTINUE;
    }
    if (event->type == SDL_EVENT_KEY_DOWN &&
        (event->key.key == SDLK_ESCAPE || event->key.key == SDLK_AC_BACK)) {
        if (state.explorer_modal.active()) {
            // Escapeは、まずテキストエディターの検索欄を閉じる。
            if (state.explorer_modal.kind() ==
                    cardwirth::explorer::ExplorerModalKind::TextEditor &&
                state.explorer_modal.search_active()) {
                state.explorer_modal.end_search();
                configure_page(state);
                return SDL_APP_CONTINUE;
            }
            if (state.explorer_modal.kind() ==
                    cardwirth::explorer::ExplorerModalKind::TextEditor &&
                state.explorer_modal.dirty()) {
                state.explorer_modal_error = "未保存です。キャンセルボタンで閉じてください。";
                configure_page(state);
                return SDL_APP_CONTINUE;
            }
            if (state.explorer_modal.kind() ==
                    cardwirth::explorer::ExplorerModalKind::ConfirmFlattenExtraction &&
                state.explorer)
                state.explorer->keep_extracted_directory();
            state.explorer_modal.dismiss();
            state.explorer_modal_error.clear();
            configure_page(state);
            return SDL_APP_CONTINUE;
        }
        if (state.shell->page() == cardwirth::app::Page::DataExplorer && state.explorer) {
            if (state.image_preview_enlarged) {
                state.image_preview_enlarged = false;
                return SDL_APP_CONTINUE;
            }
            auto& explorer = state.explorer->controller();
            if (explorer.preview()) {
                state.explorer->close_preview();
                configure_page(state);
                return SDL_APP_CONTINUE;
            }
            if (explorer.can_go_back()) {
                state.explorer->go_back_async();
                configure_page(state);
                return SDL_APP_CONTINUE;
            }
#if defined(CARDWIRTH_EXPLORER_ONLY)
            return SDL_APP_SUCCESS;
#endif
        }
        if (state.shell->page() == cardwirth::app::Page::Home) return SDL_APP_SUCCESS;
        state.shell->go_back();
        configure_page(state);
        return SDL_APP_CONTINUE;
    }

    if (state.explorer_modal.active() && event->type == SDL_EVENT_TEXT_INPUT) {
        if (state.explorer_modal.kind() == cardwirth::explorer::ExplorerModalKind::TextEditor &&
            !state.explorer_modal.text_editor_focused()) return SDL_APP_CONTINUE;
        state.explorer_modal.insert(event->text.text ? event->text.text : "");
        configure_page(state);
        return SDL_APP_CONTINUE;
    }
    if (state.explorer_modal.active() && event->type == SDL_EVENT_TEXT_EDITING) {
        if (state.explorer_modal.kind() == cardwirth::explorer::ExplorerModalKind::TextEditor &&
            !state.explorer_modal.text_editor_focused()) return SDL_APP_CONTINUE;
        state.explorer_modal.update_composition(
            event->edit.text ? event->edit.text : "", event->edit.start, event->edit.length);
        configure_page(state);
        return SDL_APP_CONTINUE;
    }
    if (state.explorer_modal.active() && event->type == SDL_EVENT_KEY_DOWN) {
        using cardwirth::explorer::ExplorerModalKind;
        const auto kind = state.explorer_modal.kind();
        const auto key = event->key.key;
        const bool shift = (event->key.mod & SDL_KMOD_SHIFT) != 0;
        const bool command = (event->key.mod & SDL_KMOD_CTRL) != 0
#if defined(__APPLE__)
            || (event->key.mod & SDL_KMOD_GUI) != 0
#endif
            ;
        const bool text_editor = kind == ExplorerModalKind::TextEditor;
        const bool searching = text_editor && state.explorer_modal.search_active();
        const auto find_match = [&state](bool forward) {
            if (state.explorer_modal.find(forward)) state.explorer_modal_error.clear();
            else state.explorer_modal_error = "見つかりません";
        };
        if (key == SDLK_BACKSPACE) state.explorer_modal.backspace();
        else if (text_editor && command && key == SDLK_A && !searching)
            state.explorer_modal.select_all();
        else if (text_editor && command && key == SDLK_C && !searching) {
            if (state.explorer_modal.has_selection())
                ::cw::lib::set_system_clipboard_text(state.explorer_modal.selected_text());
        } else if (text_editor && command && key == SDLK_X && !searching) {
            if (state.explorer_modal.has_selection())
                ::cw::lib::set_system_clipboard_text(state.explorer_modal.cut_selection());
        } else if (text_editor && command && key == SDLK_V)
            paste_into_explorer_editor(state);
        else if (text_editor && command && key == SDLK_F)
            state.explorer_modal.begin_search();
        else if (text_editor && key == SDLK_F3)
            find_match(!shift);
        else if (searching && (key == SDLK_RETURN || key == SDLK_KP_ENTER))
            find_match(!shift);
        else if (text_editor && key == SDLK_DELETE)
            state.explorer_modal.erase_forward();
        else if (text_editor && key == SDLK_LEFT)
            state.explorer_modal.move_left(shift);
        else if (text_editor && key == SDLK_RIGHT)
            state.explorer_modal.move_right(shift);
        else if (text_editor && key == SDLK_UP)
            state.explorer_modal.move_vertical(-1, shift);
        else if (text_editor && key == SDLK_DOWN)
            state.explorer_modal.move_vertical(1, shift);
        else if (text_editor && key == SDLK_HOME)
            state.explorer_modal.move_home(shift);
        else if (text_editor && key == SDLK_END)
            state.explorer_modal.move_end(shift);
        else if (text_editor && key == SDLK_PAGEUP)
            state.explorer_modal.scroll(-12);
        else if (text_editor && key == SDLK_PAGEDOWN)
            state.explorer_modal.scroll(12);
        else if (text_editor &&
                 (key == SDLK_RETURN || key == SDLK_KP_ENTER))
            state.explorer_modal.insert("\n");
        else if (kind == ExplorerModalKind::TextEditor && key == SDLK_S &&
                  ((event->key.mod & SDL_KMOD_CTRL)
#if defined(__APPLE__)
                   || (event->key.mod & SDL_KMOD_GUI)
#endif
                  )) {
            auto document = state.explorer_modal.document();
            if (state.explorer->save_text_document(document).ok) {
                state.explorer_modal.dismiss();
                state.explorer->refresh_async();
            }
        } else if (kind == ExplorerModalKind::ConfirmEmptyTrash &&
                   (key == SDLK_RETURN || key == SDLK_KP_ENTER) &&
                   state.explorer_modal.empty_trash_confirmed()) {
            state.explorer->empty_trash();
            state.explorer_modal.dismiss();
        } else if (kind == ExplorerModalKind::RenameInput &&
                   (key == SDLK_RETURN || key == SDLK_KP_ENTER)) {
            commit_explorer_rename(state);
        } else if (kind == ExplorerModalKind::ConfirmRestoreFromTrash &&
                   (key == SDLK_RETURN || key == SDLK_KP_ENTER)) {
            state.explorer->restore_selected_from_trash();
            state.explorer_modal.dismiss();
        } else if (kind == ExplorerModalKind::ConfirmDeletePermanently &&
                   (key == SDLK_RETURN || key == SDLK_KP_ENTER)) {
            state.explorer->delete_selected_permanently();
            state.explorer_modal.dismiss();
        }
        configure_page(state);
        return SDL_APP_CONTINUE;
    }
    if (state.shell->page() == cardwirth::app::Page::DataExplorer && state.explorer &&
        !state.explorer_modal.active() && event->type == SDL_EVENT_KEY_DOWN) {
        auto& component = *state.explorer;
        auto& controller = component.controller();
        const auto key = event->key.key;
        const bool command = (event->key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
        const bool extend = (event->key.mod & SDL_KMOD_SHIFT) != 0;
        const auto select_absolute = [&](std::size_t target) {
            const auto count = controller.entries().size();
            if (count == 0) return;
            target = std::min(target, count - 1U);
            const auto capacity = std::max<std::size_t>(1U, component.rows_per_page());
            if (target < component.scroll_offset()) {
                component.scroll_by(-static_cast<int>(component.scroll_offset() - target));
            } else if (target >= component.scroll_offset() + capacity) {
                component.scroll_by(static_cast<int>(
                    target - (component.scroll_offset() + capacity - 1U)));
            }
            (void)controller.select(target, extend);
            configure_page(state);
        };
        if (command && key == SDLK_C) {
            component.begin_copy();
            configure_page(state);
            return SDL_APP_CONTINUE;
        }
        if (command && key == SDLK_V) {
            begin_explorer_paste(state);
            return SDL_APP_CONTINUE;
        }
        if (command && key == SDLK_A) {
            const auto count = controller.entries().size();
            if (count != 0U) {
                (void)controller.select(0U, false);
                if (count > 1U) (void)controller.select(count - 1U, true);
                configure_page(state);
            }
            return SDL_APP_CONTINUE;
        }
        if (key == SDLK_UP || key == SDLK_DOWN || key == SDLK_HOME || key == SDLK_END ||
            key == SDLK_PAGEUP || key == SDLK_PAGEDOWN) {
            const auto count = controller.entries().size();
            if (count == 0U) return SDL_APP_CONTINUE;
            const auto selected = controller.selected_index();
            std::size_t target{};
            if (!selected) {
                if (key == SDLK_UP || key == SDLK_END) target = count - 1U;
                else if (key == SDLK_PAGEUP)
                    target = count > component.rows_per_page()
                        ? count - component.rows_per_page() : 0U;
            } else {
                const auto current = *selected;
                target = current;
                if (key == SDLK_UP) target = current == 0U ? 0U : current - 1U;
                else if (key == SDLK_DOWN) target = std::min(count - 1U, current + 1U);
                else if (key == SDLK_HOME) target = 0U;
                else if (key == SDLK_END) target = count - 1U;
                else if (key == SDLK_PAGEUP)
                    target = current > component.rows_per_page()
                        ? current - component.rows_per_page() : 0U;
                else if (key == SDLK_PAGEDOWN)
                    target = std::min(count - 1U, current + component.rows_per_page());
            }
            select_absolute(target);
            return SDL_APP_CONTINUE;
        }
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            activate_selected_explorer_item(state);
            return SDL_APP_CONTINUE;
        }
        if (key == SDLK_BACKSPACE && controller.can_go_up()) {
            component.go_up();
            configure_page(state);
            return SDL_APP_CONTINUE;
        }
        if (key == SDLK_F5) {
            component.refresh();
            configure_page(state);
            return SDL_APP_CONTINUE;
        }
        if (key == SDLK_DELETE) {
            const auto view = component.view_state();
            state.explorer_modal_error.clear();
            if (view.can_delete_permanently)
                state.explorer_modal.show_delete_permanently(view.selected_count);
            else if (view.can_trash)
                state.explorer_modal.show_move_to_trash(view.selected_count);
            if (state.explorer_modal.active()) configure_page(state);
            return SDL_APP_CONTINUE;
        }
    }
    const auto logical_coordinates = [&state](float window_x, float window_y) {
        float x = window_x;
        float y = window_y;
#if defined(CARDWIRTH_ALL_HOST)
        if (state.shell->page() != cardwirth::app::Page::CardWirthPy &&
            state.shell->page() != cardwirth::app::Page::XEditor) {
            // ホーム・Explorerの従来型キャンバスを描
            // くときと同じ表示領域・倍率で入力を変換する。
            // ホスト座標へ変換した後に独自の逆変換を重ねる
            // と、WSLgの高密度なウィンドウ座標が 描画
            // された操作部品から水平方向にずれる。描画の順
            // 変換と同様、逆変換もSDLへ一括して任せる。
            const auto& transform = state.all_layout_snapshot.host_to_render;
            SDL_Rect previous_viewport{};
            float previous_scale_x = 1.0F;
            float previous_scale_y = 1.0F;
            SDL_GetRenderViewport(state.renderer, &previous_viewport);
            SDL_GetRenderScale(state.renderer, &previous_scale_x, &previous_scale_y);
            const SDL_Rect page_viewport{
                static_cast<int>(std::lround(transform.destination.x / transform.scale_x)),
                static_cast<int>(std::lround(transform.destination.y / transform.scale_y)),
                static_cast<int>(std::lround(transform.destination.width / transform.scale_x)),
                static_cast<int>(std::lround(transform.destination.height / transform.scale_y))};
            SDL_SetRenderViewport(state.renderer, &page_viewport);
            SDL_SetRenderScale(state.renderer, transform.scale_x, transform.scale_y);
            const bool converted = SDL_RenderCoordinatesFromWindow(
                state.renderer, window_x, window_y, &x, &y);
            SDL_SetRenderScale(state.renderer, previous_scale_x, previous_scale_y);
            SDL_SetRenderViewport(state.renderer, &previous_viewport);
            if (!converted)
                return std::pair{-1000000.0F, -1000000.0F};
            return std::pair{x, y};
        }
#endif
        SDL_RenderCoordinatesFromWindow(state.renderer, window_x, window_y, &x, &y);
        return std::pair{x, y};
    };
    const auto finger_coordinates = [&state, &logical_coordinates](const SDL_TouchFingerEvent& finger) {
        int width = 0;
        int height = 0;
        SDL_GetWindowSize(state.window, &width, &height);
        return logical_coordinates(finger.x * static_cast<float>(width),
                                   finger.y * static_cast<float>(height));
    };

#if defined(CARDWIRTH_ALL_HOST)
    if (state.editor_home_confirmation_active &&
        (state.shell->page() == cardwirth::app::Page::XEditor || hosted_engine_page)) {
        const auto hit = [&state](const float x, const float y) {
            const auto modal = cardwirth::app::all::editor_home_confirmation_layout(
                state.all_layout_snapshot.host_rect);
            const cardwirth::app::all::Point point{x, y};
            if (modal.accept_button.contains(point)) return 1;
            if (modal.cancel_button.contains(point)) return 2;
            return 0;
        };
        const auto activate = [&state](const int action) {
            finish_home_confirmation(state, action == 1);
        };
        if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
            event->button.button == SDL_BUTTON_LEFT) {
            const auto [x, y] = logical_coordinates(event->button.x, event->button.y);
            const int action = hit(x, y);
            state.editor_home_confirmation_press = action ? std::optional<int>(action)
                                                          : std::nullopt;
        } else if (event->type == SDL_EVENT_MOUSE_BUTTON_UP &&
                   event->button.button == SDL_BUTTON_LEFT) {
            const auto [x, y] = logical_coordinates(event->button.x, event->button.y);
            const int action = hit(x, y);
            if (state.editor_home_confirmation_press &&
                *state.editor_home_confirmation_press == action) activate(action);
            else state.editor_home_confirmation_press.reset();
        } else if (event->type == SDL_EVENT_FINGER_DOWN) {
            const auto [x, y] = finger_coordinates(event->tfinger);
            const int action = hit(x, y);
            state.editor_home_confirmation_press = action ? std::optional<int>(action)
                                                          : std::nullopt;
            state.editor_home_confirmation_finger = event->tfinger.fingerID;
        } else if ((event->type == SDL_EVENT_FINGER_UP ||
                    event->type == SDL_EVENT_FINGER_CANCELED) &&
                   event->tfinger.fingerID == state.editor_home_confirmation_finger) {
            const auto [x, y] = finger_coordinates(event->tfinger);
            const int action = event->type == SDL_EVENT_FINGER_UP ? hit(x, y) : 0;
            if (state.editor_home_confirmation_press &&
                *state.editor_home_confirmation_press == action) activate(action);
            else state.editor_home_confirmation_press.reset();
        }
        // このホスト側のモーダルは入力を排他的に扱う。 ポインター・ホイー
        // ル・ペン・ドロップ・ジェスチャーを背後のEditorへ渡さない。
        return SDL_APP_CONTINUE;
    }
    const bool hosted_editor_page =
        state.shell->page() == cardwirth::app::Page::XEditor &&
        state.hosted_editor && state.hosted_editor->running();
    if (hosted_editor_page) {
        const auto host_button_at = [&state](float x, float y)
            -> std::shared_ptr<cardwirth::ui::Button> {
            // Editorを手前に表示する。Editorに覆われ
            // たホストの操作部品は、表示もクリックもできない。
            if (editor_destination_rect(state).contains(
                    cardwirth::app::all::Point{x, y})) return {};
            for (const auto& entry : state.buttons) {
                if (!entry.button || !entry.button->visible() || !entry.button->enabled()) continue;
                const auto& rect = entry.rect;
                if (x >= rect.x && x <= rect.x + rect.width &&
                    y >= rect.y && y <= rect.y + rect.height)
                    return entry.button;
            }
            return {};
        };
        if (event->type >= SDL_EVENT_PEN_PROXIMITY_IN && event->type <= SDL_EVENT_PEN_AXIS) {
            if (event->type == SDL_EVENT_PEN_DOWN) {
                const auto [x, y] = logical_coordinates(event->ptouch.x, event->ptouch.y);
                // 露出しているALLの操作部品は既存のマウス経路で処理する。
                // 背後のEditorのポップアップへ同じ押下を重ねて渡さない。
                if (host_button_at(x, y)) return SDL_APP_CONTINUE;
            }
            SDL_Event translated = *event;
            const auto map = [&](float& px, float& py) {
                const auto [x, y] = logical_coordinates(px, py);
                const auto local = editor_to_local(state, x, y);
                px = local.x; py = local.y;
            };
            switch (event->type) {
            case SDL_EVENT_PEN_DOWN: case SDL_EVENT_PEN_UP:
                map(translated.ptouch.x, translated.ptouch.y); break;
            case SDL_EVENT_PEN_BUTTON_DOWN: case SDL_EVENT_PEN_BUTTON_UP:
                map(translated.pbutton.x, translated.pbutton.y); break;
            case SDL_EVENT_PEN_MOTION:
                map(translated.pmotion.x, translated.pmotion.y); break;
            case SDL_EVENT_PEN_AXIS:
                map(translated.paxis.x, translated.paxis.y); break;
            default: break; // 近接イベントには位置情報がない。
            }
            state.hosted_editor->set_surface_transform(editor_surface_transform(state));
            state.hosted_editor->dispatch_native_event(&translated);
            return SDL_APP_CONTINUE;
        }
        if (event->type == SDL_EVENT_DROP_BEGIN || event->type == SDL_EVENT_DROP_POSITION ||
            event->type == SDL_EVENT_DROP_FILE || event->type == SDL_EVENT_DROP_TEXT ||
            event->type == SDL_EVENT_DROP_COMPLETE) {
            SDL_Event translated = *event;
            if (event->type != SDL_EVENT_DROP_BEGIN) {
                const auto [x, y] = logical_coordinates(event->drop.x, event->drop.y);
                const auto local = editor_to_local(state, x, y);
                translated.drop.x = local.x;
                translated.drop.y = local.y;
            }
            state.hosted_editor->set_surface_transform(editor_surface_transform(state));
            state.hosted_editor->dispatch_native_event(&translated);
            return SDL_APP_CONTINUE;
        }
        bool route_host_chrome = false;

        // Editorの表示先の外側に露出したホスト部品だけをALLへ渡す
        // 。 重なっている操作部品はEditorの背後に置いたままとする。
        if ((event->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
             event->type == SDL_EVENT_MOUSE_BUTTON_UP) &&
            event->button.button == SDL_BUTTON_LEFT) {
            const auto [x, y] = logical_coordinates(event->button.x, event->button.y);
            route_host_chrome = event->type == SDL_EVENT_MOUSE_BUTTON_DOWN
                ? static_cast<bool>(host_button_at(x, y))
                : static_cast<bool>(state.pending_release_button);
        }
        if (event->type == SDL_EVENT_FINGER_DOWN) {
            const auto [x, y] = finger_coordinates(event->tfinger);
            if (state.editor_fingers.empty() && !state.editor_pinch_active) {
                if (auto button = host_button_at(x, y)) {
                state.pending_release_button = std::move(button);
                state.finger_tracking = true;
                state.active_finger = event->tfinger.fingerID;
                return SDL_APP_CONTINUE;
                }
            }
        } else if (event->type == SDL_EVENT_FINGER_MOTION &&
                   state.finger_tracking && state.pending_release_button &&
                   event->tfinger.fingerID == state.active_finger) {
            return SDL_APP_CONTINUE;
        } else if (event->type == SDL_EVENT_FINGER_UP &&
                   state.finger_tracking && state.pending_release_button &&
                   event->tfinger.fingerID == state.active_finger) {
            const auto [x, y] = finger_coordinates(event->tfinger);
            auto pending = std::move(state.pending_release_button);
            state.pending_release_button.reset();
            state.pending_status_action.reset();
            state.finger_tracking = false;
            if (host_button_at(x, y) == pending) pending->click();
            return SDL_APP_CONTINUE;
        } else if (event->type == SDL_EVENT_FINGER_CANCELED &&
                   state.finger_tracking && state.pending_release_button &&
                   event->tfinger.fingerID == state.active_finger) {
            state.pending_release_button.reset();
            state.finger_tracking = false;
            return SDL_APP_CONTINUE;
        }
        if (!route_host_chrome) {
            // Editor内では実際のペンイベントを優先する。 外側ではroute_host_chrome
            // で既存のホームボタンの経路を使う。
            if (((event->type == SDL_EVENT_MOUSE_BUTTON_DOWN || event->type == SDL_EVENT_MOUSE_BUTTON_UP) &&
                 event->button.which == SDL_PEN_MOUSEID) ||
                (event->type == SDL_EVENT_MOUSE_MOTION && event->motion.which == SDL_PEN_MOUSEID) ||
                (event->type == SDL_EVENT_MOUSE_WHEEL && event->wheel.which == SDL_PEN_MOUSEID))
                return SDL_APP_CONTINUE;
            SDL_Event translated = *event;
            bool erase_editor_finger_after_dispatch = false;
            const auto encode_touch = [&state](SDL_Event& translated_event,
                                               const cardwirth::app::all::Point host) {
                const auto local = editor_to_local(state, host.x, host.y);
                int native_width = 1;
                int native_height = 1;
                SDL_GetWindowSize(state.window, &native_width, &native_height);
                native_width = std::max(1, native_width);
                native_height = std::max(1, native_height);
                translated_event.tfinger.windowID = SDL_GetWindowID(state.window);
                // SDLの指の座標は、ネイティブウィンドウを基準に正規
                // 化される。 ALLのEditorは可変幅なので、ホー
                // ム用の固定値で変換すると、 1888x910と縦横
                // 比が異なるAndroid端末でタップ位置がずれる。
                translated_event.tfinger.x = local.x / static_cast<float>(native_width);
                translated_event.tfinger.y = local.y / static_cast<float>(native_height);
            };

            if (event->type == SDL_EVENT_FINGER_DOWN ||
                event->type == SDL_EVENT_FINGER_UP ||
                event->type == SDL_EVENT_FINGER_MOTION ||
                event->type == SDL_EVENT_FINGER_CANCELED) {
                const auto [host_x, host_y] = finger_coordinates(event->tfinger);
                const cardwirth::app::all::Point host{host_x, host_y};
                const SDL_FingerID finger = event->tfinger.fingerID;
                if (event->type == SDL_EVENT_FINGER_DOWN) {
                    state.editor_fingers[finger] = host;
                    if (!state.editor_pinch_active && state.editor_fingers.size() >= 2U) {
                        const auto first = std::find_if(
                            state.editor_fingers.begin(), state.editor_fingers.end(),
                            [finger](const auto& entry) { return entry.first != finger; });
                        if (first != state.editor_fingers.end()) {
                            state.editor_pinch_active = true;
                            state.editor_pinch_finger_a = first->first;
                            state.editor_pinch_finger_b = finger;
                            const float dx = host.x - first->second.x;
                            const float dy = host.y - first->second.y;
                            state.editor_pinch_start_distance =
                                std::max(1.0F, std::hypot(dx, dy));
                            state.editor_pinch_start_zoom = state.editor_zoom;
                            const cardwirth::app::all::Point midpoint{
                                (host.x + first->second.x) * 0.5F,
                                (host.y + first->second.y) * 0.5F};
                            state.editor_pinch_anchor =
                                editor_to_local(state, midpoint.x, midpoint.y);

                            // 最初の指は既にタップ・スクロール候補と
                            // して配送済み。 2本指のピンチへ移る前
                            // に取り消し、どちらの指の解放でもEditor
                            // の部品を誤操作しないようにする。
                            SDL_Event canceled = *event;
                            canceled.type = SDL_EVENT_FINGER_CANCELED;
                            canceled.tfinger.fingerID = first->first;
                            encode_touch(canceled, first->second);
                            state.hosted_editor->dispatch_native_event(&canceled);
                        }
                    }
                    if (state.editor_pinch_active) return SDL_APP_CONTINUE;
                } else {
                    const auto found = state.editor_fingers.find(finger);
                    if (found == state.editor_fingers.end()) return SDL_APP_CONTINUE;
                    found->second = host;
                    if (state.editor_pinch_active) {
                        if (event->type == SDL_EVENT_FINGER_MOTION) {
                            const auto first = state.editor_fingers.find(
                                state.editor_pinch_finger_a);
                            const auto second = state.editor_fingers.find(
                                state.editor_pinch_finger_b);
                            if (first != state.editor_fingers.end() &&
                                second != state.editor_fingers.end()) {
                                const float dx = second->second.x - first->second.x;
                                const float dy = second->second.y - first->second.y;
                                const float distance = std::max(1.0F, std::hypot(dx, dy));
                                const float raw_zoom = state.editor_pinch_start_zoom *
                                    distance / state.editor_pinch_start_distance;
                                state.editor_zoom = std::clamp(
                                    std::round(raw_zoom / kEditorZoomStep) * kEditorZoomStep,
                                    kEditorZoomMinimum, kEditorZoomMaximum);
                                const cardwirth::app::all::Point midpoint{
                                    (first->second.x + second->second.x) * 0.5F,
                                    (first->second.y + second->second.y) * 0.5F};
                                state.editor_offset_x = midpoint.x -
                                    state.editor_pinch_anchor.x *
                                        state.editor_base_scale * state.editor_zoom;
                                state.editor_offset_y = midpoint.y -
                                    state.editor_pinch_anchor.y *
                                        state.editor_base_scale * state.editor_zoom;
                                clamp_editor_view(state);
                            }
                        }
                        if (event->type == SDL_EVENT_FINGER_UP ||
                            event->type == SDL_EVENT_FINGER_CANCELED) {
                            state.editor_fingers.erase(found);
                            if (state.editor_fingers.empty())
                                state.editor_pinch_active = false;
                        }
                        return SDL_APP_CONTINUE;
                    }
                    erase_editor_finger_after_dispatch =
                        event->type == SDL_EVENT_FINGER_UP ||
                        event->type == SDL_EVENT_FINGER_CANCELED;
                }
                encode_touch(translated, host);
            }
            if (event->type == SDL_EVENT_MOUSE_MOTION) {
                const auto [x, y] = logical_coordinates(event->motion.x, event->motion.y);
                const auto local = editor_to_local(state, x, y);
                translated.motion.x = local.x;
                translated.motion.y = local.y;
            } else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                       event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
                const auto [x, y] = logical_coordinates(event->button.x, event->button.y);
                const auto local = editor_to_local(state, x, y);
                translated.button.x = local.x;
                translated.button.y = local.y;
            } else if (event->type == SDL_EVENT_MOUSE_WHEEL) {
                const auto [x, y] = logical_coordinates(
                    event->wheel.mouse_x, event->wheel.mouse_y);
                const auto local = editor_to_local(state, x, y);
                translated.wheel.mouse_x = local.x;
                translated.wheel.mouse_y = local.y;
            }
            state.hosted_editor->set_surface_transform(editor_surface_transform(state));
            state.hosted_editor->dispatch_native_event(&translated);
            if (erase_editor_finger_after_dispatch)
                state.editor_fingers.erase(event->tfinger.fingerID);
            return SDL_APP_CONTINUE;
        }
    }
    if (hosted_engine_page && event->type == SDL_EVENT_WINDOW_MOUSE_LEAVE) {
        state.engine_dialog_cursor.clear();
        state.hosted_engine->debugger_hover(-1, -1);
    }
    if (hosted_engine_page && event->type == SDL_EVENT_WINDOW_MOUSE_LEAVE &&
        state.hosted_engine->modal_snapshot()) {
        state.hosted_engine->dialog_hover(-1, -1);
        return SDL_APP_CONTINUE;
    }
    if (hosted_engine_page && event->type == SDL_EVENT_MOUSE_MOTION) {
        const auto [x, y] = logical_coordinates(event->motion.x, event->motion.y);
        if(const auto modal=state.hosted_engine->modal_snapshot()) {
            const auto point=hosted_dialog_point(state,x,y);
            if(point) state.engine_dialog_cursor.update(modal->presentation_controls,point->x,point->y);
            else state.engine_dialog_cursor.clear();
        } else state.engine_dialog_cursor.clear();
        if (state.pending_debugger_control && state.pending_debugger_control->continuous_pointer) {
            if(event->motion.state & SDL_BUTTON_LMASK)
                invoke_hosted_debugger_pointer(state,*state.pending_debugger_control,x,y,true);
            else {
                state.pending_debugger_control.reset();
                state.hosted_engine->debugger_pointer_cancel();
            }
            return SDL_APP_CONTINUE;
        }
        const auto debugger_point = hosted_debugger_point(state, x, y);
        state.hosted_engine->debugger_hover(debugger_point ? debugger_point->x : -1,
                                            debugger_point ? debugger_point->y : -1);
        if (debugger_point) return SDL_APP_CONTINUE;
        if (state.hosted_engine->modal_snapshot() &&
            state.hosted_dialog_pointer.active() &&
            move_hosted_dialog_pointer(state,x,y,
                (event->motion.state & SDL_BUTTON_LMASK)!=0)) {
        } else if (state.hosted_engine->modal_snapshot()) {
            const auto point = hosted_dialog_point(state, x, y);
            state.hosted_engine->dialog_hover(point ? point->x : -1.0F, point ? point->y : -1.0F);
        } else if (!state.hosted_engine->modal_snapshot() &&
            !state.all_dialog_layer.native_engine_dialog_active()) {
            if (const auto local = hosted_engine_point(state, x, y))
                state.hosted_engine->pointer_motion(
                    static_cast<int>(std::lround(local->x)),
                    static_cast<int>(std::lround(local->y)));
        }
        return SDL_APP_CONTINUE;
    }
    if (hosted_engine_page && event->type == SDL_EVENT_MOUSE_WHEEL) {
        // このホイールイベントに含まれるポインター位置を使う。特にWeb
        // のALLでは SDL_GetMouseStateが遅れた
        // 位置やキャンバス原点を返す場合があり、 詳細設定などのモ
        // ーダル画面で有効なホイール入力が無視される原因になる。
        const float window_x = event->wheel.mouse_x;
        const float window_y = event->wheel.mouse_y;
        const auto [x, y] = logical_coordinates(window_x, window_y);
        const bool horizontal = event->wheel.y == 0.0F && event->wheel.x != 0.0F;
        const float raw_rotation = horizontal ? event->wheel.x : event->wheel.y;
        const float directed_rotation = event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED
            ? -raw_rotation : raw_rotation;
        // SDLが渡す小数付きの各軸の量を保ち、 横ホ
        // イールの符号には共通のPy互換規則を使う。
        const float wheel_y = directed_rotation * static_cast<float>(
            cw::util::get_wheelrotation(1, horizontal));
        if (wheel_y == 0.0F) return SDL_APP_CONTINUE;
        // 手前のモーダルは、自分の矩形上のホイール入力を所有
        // する。 背後にデバッガーが見えていても渡さない。
        if (state.hosted_engine->modal_snapshot()) {
            if (const auto point=hosted_dialog_point(state,x,y)) {
                state.hosted_engine->dialog_wheel(point->x,point->y,wheel_y>0.0F?1:-1);
                return SDL_APP_CONTINUE;
            }
        }
        if (const auto point = hosted_debugger_point(state, x, y)) {
            state.hosted_engine->debugger_scroll(point->x, point->y, wheel_y > 0.0F ? -3 : 3);
            return SDL_APP_CONTINUE;
        }
        if (!state.hosted_engine->modal_snapshot() && !state.all_dialog_layer.native_engine_dialog_active()) {
            if (const auto local = hosted_engine_point(state, x, y))
                state.hosted_engine->pointer_wheel(
                    static_cast<int>(std::lround(local->x)),
                    static_cast<int>(std::lround(local->y)),
                    wheel_y > 0.0F ? 1 : -1);
        }
        return SDL_APP_CONTINUE;
    }
#endif

    if (event->type == SDL_EVENT_MOUSE_WHEEL &&
        state.shell->page() == cardwirth::app::Page::DataExplorer && state.explorer) {
        if (state.image_preview_enlarged) return SDL_APP_CONTINUE;
        if (state.explorer_modal.active()) {
            if (state.explorer_modal.kind() ==
                cardwirth::explorer::ExplorerModalKind::TextEditor) {
                const int direction = event->wheel.y > 0.0F ? -3 : 3;
                state.explorer_modal.scroll(direction);
                configure_page(state);
            }
            return SDL_APP_CONTINUE;
        }
        if (event->wheel.y != 0.0F) {
            const int direction = event->wheel.y > 0.0F ? -3 : 3;
            state.explorer->scroll_by(direction);
            configure_page(state);
        }
        return SDL_APP_CONTINUE;
    }

    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT) {
        const auto [x, y] = logical_coordinates(event->button.x, event->button.y);
        state.pending_release_button.reset();
        if (state.shell->page() == cardwirth::app::Page::DataExplorer &&
            state.image_preview_enlarged) {
            handle_explorer_pointer(state, x, y, false);
            return SDL_APP_CONTINUE;
        }
#if defined(CARDWIRTH_ALL_HOST)
        state.pending_status_action.reset();
#endif
#if defined(CARDWIRTH_ALL_HOST)
        if (hosted_engine_page) {
            if (!state.hosted_engine->modal_snapshot() &&
                !state.all_dialog_layer.native_engine_dialog_active()) {
                state.pending_status_action = hosted_status_action_at(state, x, y);
                if (state.pending_status_action) return SDL_APP_CONTINUE;
            }
            state.pending_debugger_control = hosted_debugger_control_at(state, x, y);
            state.pending_debugger_activate = event->button.clicks >= 2;
            if (state.pending_debugger_control) {
                state.hosted_debugger_focused = true;
                if(state.pending_debugger_control->continuous_pointer)
                    invoke_hosted_debugger_pointer(state,*state.pending_debugger_control,x,y,true);
                return SDL_APP_CONTINUE;
            }
        }
        if (hosted_engine_page && state.hosted_engine->modal_snapshot()) {
            begin_hosted_dialog_pointer(state,x,y);
            return SDL_APP_CONTINUE;
        }
        if (hosted_engine_page && state.all_dialog_layer.native_engine_dialog_active())
            return SDL_APP_CONTINUE;
        if (hosted_engine_page) {
            // ホームへの操作部品はEngineより手前、モーダルダイアログより奥に描く。
            if (auto button = hosted_home_button_at(state, x, y)) {
                state.pending_release_button = std::move(button);
                return SDL_APP_CONTINUE;
            }
            if (const auto local = hosted_engine_point(state, x, y)) {
                state.hosted_debugger_focused = false;
                if(state.hosted_engine) state.hosted_engine->debugger_blur();
                state.hosted_engine->pointer_button(
                    static_cast<int>(std::lround(local->x)),
                    static_cast<int>(std::lround(local->y)), 1, true);
                return SDL_APP_CONTINUE;
            }
        }
#endif
        for (const auto& entry : state.buttons) {
            const auto rect = entry.rect;
            if (entry.activate_on_release && x >= rect.x && x <= rect.x + rect.width &&
                y >= rect.y && y <= rect.y + rect.height) {
                state.pending_release_button = entry.button;
                return SDL_APP_CONTINUE;
            }
        }
        handle_explorer_pointer(state, x, y, event->button.clicks >= 2,
                                (SDL_GetModState() & SDL_KMOD_SHIFT) != 0);
        // テキスト欄内の押下で、ドラッグによる範囲選択を開始する。
        state.explorer_text_dragging =
            state.explorer_modal.kind() ==
                cardwirth::explorer::ExplorerModalKind::TextEditor &&
            x >= kTextEditorBox.x && x <= kTextEditorBox.x + kTextEditorBox.w &&
            y >= kTextEditorBox.y && y <= kTextEditorBox.y + kTextEditorBox.h;
        return SDL_APP_CONTINUE;
    }

    if (state.explorer_text_dragging) {
        if (event->type == SDL_EVENT_MOUSE_MOTION &&
            state.explorer_modal.kind() ==
                cardwirth::explorer::ExplorerModalKind::TextEditor &&
            (event->motion.state & SDL_BUTTON_LMASK) != 0) {
            const auto [x, y] = logical_coordinates(event->motion.x, event->motion.y);
            // 欄の上下へドラッグしたら、1行ずつスクロールする。
            float clamped_y = y;
            if (y < kTextEditorBox.y) {
                state.explorer_modal.scroll(-1);
                clamped_y = kTextEditorBox.y;
            } else if (y > kTextEditorBox.y + kTextEditorBox.h) {
                state.explorer_modal.scroll(1);
                clamped_y = kTextEditorBox.y + kTextEditorBox.h;
            }
            const float clamped_x = std::clamp(x, kTextEditorBox.x,
                                               kTextEditorBox.x + kTextEditorBox.w);
            if (place_explorer_text_caret(state, clamped_x, clamped_y, true))
                configure_page(state);
            return SDL_APP_CONTINUE;
        }
        if (event->type == SDL_EVENT_MOUSE_BUTTON_UP &&
            event->button.button == SDL_BUTTON_LEFT)
            state.explorer_text_dragging = false;
    }

#if defined(CARDWIRTH_ALL_HOST)
    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP && event->button.button == SDL_BUTTON_LEFT &&
        hosted_engine_page && state.pending_status_action) {
        const auto [x, y] = logical_coordinates(event->button.x, event->button.y);
        const auto pending = *state.pending_status_action;
        state.pending_status_action.reset();
        if (hosted_status_action_at(state, x, y) == pending &&
            !state.hosted_engine->modal_snapshot() &&
            !state.all_dialog_layer.native_engine_dialog_active())
            send_hosted_status_action(state, pending);
        return SDL_APP_CONTINUE;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP && event->button.button == SDL_BUTTON_LEFT &&
        hosted_engine_page && state.pending_debugger_control) {
        const auto [x, y] = logical_coordinates(event->button.x, event->button.y);
        const auto pending = *state.pending_debugger_control;
        const bool activate = state.pending_debugger_activate;
        state.pending_debugger_control.reset();
        state.pending_debugger_activate = false;
        invoke_hosted_debugger_release(state,pending,x,y,activate?2:1);
        return SDL_APP_CONTINUE;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP && event->button.button == SDL_BUTTON_LEFT &&
        hosted_engine_page && state.hosted_dialog_pointer.active()) {
        const auto [x, y] = logical_coordinates(event->button.x, event->button.y);
        end_hosted_dialog_pointer(state,x,y,event->button.clicks);
        return SDL_APP_CONTINUE;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP && event->button.button == SDL_BUTTON_LEFT &&
        hosted_engine_page && state.hosted_engine->modal_snapshot())
        return SDL_APP_CONTINUE;
#endif

    if (state.shell->page() == cardwirth::app::Page::DataExplorer && state.explorer &&
        !state.explorer_modal.active() && event->type == SDL_EVENT_MOUSE_BUTTON_UP &&
        event->button.button == SDL_BUTTON_RIGHT) {
        const auto [x, y] = logical_coordinates(event->button.x, event->button.y);
        if (const auto row = explorer_row_at(state, x, y))
            show_explorer_context_actions(state, *row);
        return SDL_APP_CONTINUE;
    }

    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP && event->button.button == SDL_BUTTON_LEFT &&
        state.pending_release_button) {
        const auto [x, y] = logical_coordinates(event->button.x, event->button.y);
        auto pending = std::move(state.pending_release_button);
        state.pending_release_button.reset();
        for (const auto& entry : state.buttons) {
            const auto rect = entry.rect;
            if (entry.button == pending && x >= rect.x && x <= rect.x + rect.width &&
                y >= rect.y && y <= rect.y + rect.height) {
                pending->click();
                break;
            }
        }
        return SDL_APP_CONTINUE;
    }

#if defined(CARDWIRTH_ALL_HOST)
    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP && event->button.button == SDL_BUTTON_LEFT &&
        hosted_engine_page) {
        const auto [x, y] = logical_coordinates(event->button.x, event->button.y);
        if (!state.hosted_engine->modal_snapshot() &&
            !state.all_dialog_layer.native_engine_dialog_active())
        if (const auto local = hosted_engine_point(state, x, y))
            state.hosted_engine->pointer_button(
                static_cast<int>(std::lround(local->x)),
                static_cast<int>(std::lround(local->y)), 1, false);
        return SDL_APP_CONTINUE;
    }
    if (hosted_engine_page &&
        (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
         event->type == SDL_EVENT_MOUSE_BUTTON_UP) &&
        event->button.button != SDL_BUTTON_LEFT) {
        const auto [x, y] = logical_coordinates(event->button.x, event->button.y);
        if (const auto point = hosted_debugger_point(state, x, y)) {
            if (event->button.button == SDL_BUTTON_RIGHT && event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
                state.hosted_debugger_focused = true;
                state.hosted_engine->debugger_context(point->x, point->y);
            }
        } else if (state.hosted_engine->modal_snapshot()) {
            if (event->button.button == SDL_BUTTON_RIGHT &&
                event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
                const auto point = hosted_dialog_point(state, x, y);
                state.hosted_engine->dialog_context(point ? point->x : -1.0F, point ? point->y : -1.0F);
            }
            if (event->button.button == SDL_BUTTON_MIDDLE) {
                const auto point = hosted_dialog_point(state, x, y);
                state.hosted_engine->dialog_middle(point ? point->x : -1.0F, point ? point->y : -1.0F,
                    event->type == SDL_EVENT_MOUSE_BUTTON_DOWN);
            }
        } else if (!state.all_dialog_layer.native_engine_dialog_active()) {
            if (const auto local = hosted_engine_point(state, x, y)) {
                const int button = event->button.button == SDL_BUTTON_MIDDLE ? 2 : 3;
                state.hosted_engine->pointer_button(
                    static_cast<int>(std::lround(local->x)),
                    static_cast<int>(std::lround(local->y)), button,
                    event->type == SDL_EVENT_MOUSE_BUTTON_DOWN);
            }
        }
        return SDL_APP_CONTINUE;
    }
#endif

    if (event->type == SDL_EVENT_FINGER_DOWN) {
        const auto [x, y] = finger_coordinates(event->tfinger);
#if defined(CARDWIRTH_ALL_HOST)
        if (hosted_engine_page) {
#if defined(__ANDROID__)
            SDL_Log("CardWirthInput Android touch->Engine phase=down finger=%lld x=%.2f y=%.2f",
                    static_cast<long long>(event->tfinger.fingerID),
                    static_cast<double>(x), static_cast<double>(y));
#endif
            state.finger_tracking = true;
            state.finger_scrolled = false;
            state.active_finger = event->tfinger.fingerID;
            state.finger_down_at = SDL_GetTicks();
            state.finger_start_x = state.all_touch_last_x = x;
            state.finger_start_y = state.all_touch_last_y = y;
            state.finger_last_y=y;
            state.finger_scroll_remainder=0;
            state.hosted_swipe_model=0;
            {
                const auto down_modal = state.hosted_engine ? state.hosted_engine->modal_snapshot()
                                                            : decltype(state.hosted_engine->modal_snapshot()){};
                state.hosted_touch_down_modal = down_modal ? down_modal->model_id : 0;
            }
            state.all_long_press_context_sent = false;
            state.pending_release_button.reset();
            state.pending_status_action.reset();
            state.finger_start_row.reset();
            state.finger_last_selection_row.reset();
            if (!state.hosted_engine->modal_snapshot() &&
                !state.all_dialog_layer.native_engine_dialog_active())
                state.pending_status_action = hosted_status_action_at(state, x, y);
            state.pending_debugger_control = state.pending_status_action
                ? std::nullopt : hosted_debugger_control_at(state, x, y);
            state.pending_debugger_activate = false;
            if (state.pending_status_action) {
                // 押し始めたのと同じボタン内で解放したときに、操作を確定する。
            } else if (state.pending_debugger_control) {
                state.hosted_debugger_focused = true;
                if(state.pending_debugger_control->continuous_pointer)
                    invoke_hosted_debugger_pointer(state,*state.pending_debugger_control,x,y,true);
                // Engineのモーダル表示中も、デバッガーは操作できる。
            } else if (state.hosted_engine->modal_snapshot()) {
                begin_hosted_dialog_pointer(state,x,y);
                if(!state.hosted_dialog_pointer.continuous())
                    if(const auto modal=state.hosted_engine->modal_snapshot();modal&&hosted_dialog_point(state,x,y))
                    {
                        state.hosted_swipe_model=modal->model_id;
                        state.hosted_scroll_gesture.begin(x,y,24.0F,hosted_touch_slop(state));
                    }
            } else if (!state.all_dialog_layer.native_engine_dialog_active()) {
                if (auto button = hosted_home_button_at(state, x, y)) {
                    state.pending_release_button = std::move(button);
                } else if (const auto local = hosted_engine_point(state, x, y)) {
                    state.hosted_debugger_focused = false;
                if(state.hosted_engine) state.hosted_engine->debugger_blur();
                    state.hosted_touch_game = true;
                    state.hosted_engine->pointer_button(
                        static_cast<int>(std::lround(local->x)),
                        static_cast<int>(std::lround(local->y)), 1, true, true);
                } else {
                    for (const auto& entry : state.buttons) {
                        const auto rect = entry.rect;
                        if (entry.activate_on_release && entry.button &&
                            entry.button->visible() && entry.button->enabled() &&
                            x >= rect.x && x <= rect.x + rect.width &&
                            y >= rect.y && y <= rect.y + rect.height) {
                            state.pending_release_button = entry.button;
                            break;
                        }
                    }
                }
            }
            return SDL_APP_CONTINUE;
        }
#endif
        if (state.shell->page() == cardwirth::app::Page::DataExplorer &&
            state.explorer_modal.kind() == cardwirth::explorer::ExplorerModalKind::TextEditor &&
            state.explorer_modal.text_editor_focused() &&
            (x < kTextEditorPanel.x || x > kTextEditorPanel.x + kTextEditorPanel.w ||
             y < kTextEditorPanel.y || y > kTextEditorPanel.y + kTextEditorPanel.h)) {
            state.explorer_modal.blur_text_editor();
            configure_page(state);
        }
        state.finger_tracking = true;
        state.finger_scrolled = false;
        state.finger_selecting = false;
        state.active_finger = event->tfinger.fingerID;
        state.finger_start_x = x;
        state.finger_start_y = y;
        state.finger_last_y = y;
        state.finger_scroll_remainder = 0.0F;
        state.finger_down_at = SDL_GetTicks();
        state.finger_start_row = state.explorer_modal.active() ||
            state.image_preview_enlarged ? std::nullopt :
            explorer_row_at(state, x, y);
        state.finger_last_selection_row.reset();
        return SDL_APP_CONTINUE;
    }

    if (event->type == SDL_EVENT_FINGER_MOTION && state.finger_tracking &&
        event->tfinger.fingerID == state.active_finger) {
        const auto [x, y] = finger_coordinates(event->tfinger);
        if (state.shell->page() == cardwirth::app::Page::DataExplorer &&
            state.image_preview_enlarged) return SDL_APP_CONTINUE;
#if defined(CARDWIRTH_ALL_HOST)
        if (hosted_engine_page) {
            state.all_touch_last_x = x;
            state.all_touch_last_y = y;
            const float slop = hosted_touch_slop(state);
            if (std::abs(x - state.finger_start_x) > slop ||
                std::abs(y - state.finger_start_y) > slop)
                state.finger_scrolled = true;
            if(state.hosted_swipe_model){
                const auto modal=state.hosted_engine->modal_snapshot();
                if(!modal||modal->model_id!=state.hosted_swipe_model){
                    cancel_hosted_dialog_pointer(state);
                    return SDL_APP_CONTINUE;
                }
                const int lines=state.hosted_scroll_gesture.move(x,y);
                state.finger_last_y=y;
                if(state.hosted_scroll_gesture.dragged()){
                    state.finger_scrolled=true;
                    // スクロール前に元の選択候補を破棄し、 指の下へ移
                    // 動してきた別の行をUPで実行しないようにする。
                    cancel_hosted_dialog_pointer(state);
                    if(lines!=0){
                        if(const auto point=hosted_dialog_point(state,state.finger_start_x,state.finger_start_y,true))
                            state.hosted_engine->dialog_scroll(point->x,point->y,lines);
                    }
                    return SDL_APP_CONTINUE;
                }
            }
            if(state.pending_debugger_control) {
                if(state.pending_debugger_control->continuous_pointer)
                    invoke_hosted_debugger_pointer(state,*state.pending_debugger_control,x,y,true);
                return SDL_APP_CONTINUE;
            }
            if (state.hosted_engine->modal_snapshot() &&
                state.hosted_dialog_pointer.active() &&
                move_hosted_dialog_pointer(state,x,y,true)) {
                return SDL_APP_CONTINUE;
            }
            if (state.hosted_touch_game && !state.hosted_engine->modal_snapshot() &&
                !state.all_dialog_layer.native_engine_dialog_active()) {
                if (const auto local = hosted_engine_point(state, x, y))
                    state.hosted_engine->pointer_motion(
                        static_cast<int>(std::lround(local->x)),
                        static_cast<int>(std::lround(local->y)), true);
            }
            return SDL_APP_CONTINUE;
        }
#endif
        if (state.explorer_modal.active()) {
            state.finger_scroll_remainder += state.finger_last_y - y;
            state.finger_last_y = y;
            if (std::abs(y - state.finger_start_y) > 10.0F) state.finger_scrolled = true;
            if (state.finger_scrolled && state.explorer_modal.kind() ==
                    cardwirth::explorer::ExplorerModalKind::TextEditor &&
                std::abs(state.finger_scroll_remainder) >= 22.0F) {
                const int lines = static_cast<int>(state.finger_scroll_remainder / 22.0F);
                state.finger_scroll_remainder -= static_cast<float>(lines) * 22.0F;
                state.explorer_modal.scroll(lines);
                configure_page(state);
            }
            return SDL_APP_CONTINUE;
        }
        if (state.finger_selecting) {
            if (const auto row = explorer_row_at(state, x, y);
                row && row != state.finger_last_selection_row) {
                (void)state.explorer->controller().select(*row, true);
                state.finger_last_selection_row = row;
                configure_page(state);
            }
            return SDL_APP_CONTINUE;
        }
        if (state.finger_start_row && SDL_GetTicks() - state.finger_down_at >= 450 &&
            std::abs(x - state.finger_start_x) <= 22.0F &&
            std::abs(y - state.finger_start_y) <= 22.0F) {
            const auto row = *state.finger_start_row;
            // 長押しが成立した状態を保持する。行をまたいで動かすと範囲を広
            // げ、 動かさずに離した場合は通常のコンテキスト操作を開く。
            (void)state.explorer->controller().select(row, false);
            state.finger_selecting = true;
            state.finger_last_selection_row = row;
            configure_page(state);
            return SDL_APP_CONTINUE;
        }
        state.finger_scroll_remainder += state.finger_last_y - y;
        state.finger_last_y = y;
        if (std::abs(y - state.finger_start_y) > 10.0F) {
            state.finger_scrolled = true;
            state.finger_start_row.reset();
        }
        if (state.finger_scrolled && state.shell->page() == cardwirth::app::Page::DataExplorer &&
            state.explorer && std::abs(state.finger_scroll_remainder) >= 28.0F) {
            if (state.explorer->controller().preview()) state.explorer->close_preview();
            const int rows = static_cast<int>(state.finger_scroll_remainder / 28.0F);
            state.finger_scroll_remainder -= static_cast<float>(rows) * 28.0F;
            state.explorer->scroll_by(rows);
            configure_page(state);
        }
        return SDL_APP_CONTINUE;
    }

    if (event->type == SDL_EVENT_FINGER_CANCELED && state.finger_tracking &&
        event->tfinger.fingerID == state.active_finger) {
        // Explorer・ホームは指の入力を直接処理する。特にAndroid
        // がジェスチャーを奪った場合など、 キャンセル時は埋め込みEditor
        // ・Engineと同じく、保留中の操作確定をすべて破棄する。
        state.finger_tracking = false;
        state.finger_scrolled = false;
        state.finger_selecting = false;
        state.finger_start_row.reset();
        state.finger_last_selection_row.reset();
        state.pending_release_button.reset();
        return SDL_APP_CONTINUE;
    }

    if (event->type == SDL_EVENT_FINGER_UP && state.finger_tracking &&
        event->tfinger.fingerID == state.active_finger) {
        const auto [x, y] = finger_coordinates(event->tfinger);
        state.finger_tracking = false;
#if defined(CARDWIRTH_ALL_HOST)
        if (hosted_engine_page) {
#if defined(__ANDROID__)
            SDL_Log("CardWirthInput Android touch->Engine phase=up finger=%lld x=%.2f y=%.2f",
                    static_cast<long long>(event->tfinger.fingerID),
                    static_cast<double>(x), static_cast<double>(y));
#endif
            const bool game_press = std::exchange(state.hosted_touch_game, false);
            const int clicks=std::exchange(state.hosted_touch_clicks,1);
            if(std::exchange(state.hosted_swipe_model,0)&&state.finger_scrolled){
                cancel_hosted_dialog_pointer(state);
                state.pending_release_button.reset();
                return SDL_APP_CONTINUE;
            }
            if (std::exchange(state.all_long_press_context_sent, false)) {
                state.pending_status_action.reset();
                state.pending_debugger_control.reset();
                state.pending_release_button.reset();
                return SDL_APP_CONTINUE;
            }
            if (state.pending_status_action) {
                const auto pending = *state.pending_status_action;
                state.pending_status_action.reset();
                if (hosted_status_action_at(state, x, y) == pending &&
                    !state.hosted_engine->modal_snapshot() &&
                    !state.all_dialog_layer.native_engine_dialog_active())
                    send_hosted_status_action(state, pending);
            } else if (state.pending_debugger_control) {
                const auto pending = *state.pending_debugger_control;
                state.pending_debugger_control.reset();
                state.pending_debugger_activate=false;
                invoke_hosted_debugger_release(state,pending,x,y,clicks);
            } else if (state.hosted_dialog_pointer.active()) {
                end_hosted_dialog_pointer(state,x,y,clicks);
            } else if (state.pending_release_button) {
                auto pending = std::move(state.pending_release_button);
                state.pending_release_button.reset();
                for (const auto& entry : state.buttons) {
                    const auto rect = entry.rect;
                    if (entry.button == pending && x >= rect.x &&
                        x <= rect.x + rect.width && y >= rect.y &&
                        y <= rect.y + rect.height) {
                        pending->click();
                        break;
                    }
                }
            } else if (game_press && !state.hosted_engine->modal_snapshot() &&
                       !state.all_dialog_layer.native_engine_dialog_active()) {
                if (const auto local = hosted_engine_point(state, x, y)) {
                    state.hosted_engine->pointer_button(
                        static_cast<int>(std::lround(local->x)),
                        static_cast<int>(std::lround(local->y)), 1, false, true);
                } else state.hosted_engine->pointer_cancel();
            } else if (game_press) {
                state.hosted_engine->pointer_cancel();
            }
            return SDL_APP_CONTINUE;
        }
#endif
        if (state.explorer_modal.active()) {
            if (!state.finger_scrolled) handle_explorer_modal_pointer(state, x, y);
            return SDL_APP_CONTINUE;
        }
        if (state.finger_selecting) {
            const auto start = state.finger_start_row;
            const auto last = state.finger_last_selection_row;
            state.finger_selecting = false;
            state.finger_start_row.reset();
            state.finger_last_selection_row.reset();
            if (start && last == start)
                show_explorer_context_actions(state, *start);
            else
                configure_page(state);
            return SDL_APP_CONTINUE;
        }
        if (!state.finger_scrolled && state.finger_start_row &&
            SDL_GetTicks() - state.finger_down_at >= 450) {
            const auto row = *state.finger_start_row;
            state.finger_start_row.reset();
            show_explorer_context_actions(state, row);
            return SDL_APP_CONTINUE;
        }
        if (!state.finger_scrolled) {
            const auto now = SDL_GetTicks();
            const float delta_x = x - state.last_tap_x;
            const float delta_y = y - state.last_tap_y;
            const bool double_tap = now - state.last_tap_at <= 450 &&
                delta_x * delta_x + delta_y * delta_y <= 24.0F * 24.0F;
            handle_explorer_pointer(state, x, y, double_tap, false);
            state.last_tap_at = now;
            state.last_tap_x = x;
            state.last_tap_y = y;
        }
        return SDL_APP_CONTINUE;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    auto& state = *static_cast<RuntimeState*>(appstate);
    {
        std::vector<std::string> messages;
        {
            std::scoped_lock lock(state.pending_status_mutex);
            messages.swap(state.pending_status_messages);
        }
        if (!messages.empty() && state.shell)
            state.shell->set_status(std::move(messages.back()));
    }
    if (state.quit_requested)
        return state.smoke_failed ? SDL_APP_FAILURE : SDL_APP_SUCCESS;
#if defined(CARDWIRTH_ALL_HOST)
    if (state.home_landscape_action && state.shell->page() == cardwirth::app::Page::Home &&
        !web_phone_needs_landscape()) {
        auto action = std::move(state.home_landscape_action);
        state.home_landscape_action = {};
        action();
    }
    if (state.engine_data_navigation_pending) {
        state.engine_data_navigation_pending = false;
        if (state.shell->page() == cardwirth::app::Page::CardWirthPy &&
            state.hosted_engine && state.hosted_engine->running() &&
            !state.hosted_engine->modal_snapshot() &&
            state.host_close_phase == RuntimeState::HostClosePhase::Idle) {
            cancel_hosted_engine_pointer(state);
            state.hosted_engine->focus(false);
            navigate(state, cardwirth::app::Page::DataExplorer);
        }
    }
    if (state.busy_action && state.busy_action_presented) {
        auto action = std::move(state.busy_action);
        state.busy_action = nullptr;
        state.busy_action_presented = false;
        action();
    }
    if (state.home_return_busy) {
        // 設定や保存の失敗ではEditorの終了を取り消す。動作中の
        // Editorへ戻ってエラーを表示し、 ホームの処理中表示
        // の背後で、終了しないセッションを待ち続けないようにする。
        if (!state.home_return_waits_engine && state.hosted_editor &&
            state.hosted_editor->running() && !state.hosted_editor->stop_requested()) {
            state.home_return_busy = false;
            state.shell->set_initial_page(cardwirth::app::Page::XEditor);
            configure_page(state);
        }
        // 停止中のセッションが終了するまで処理中とする。 継続が正当なセ
        // ッションでもホームを塞ぎ続けないよう、安全上限を30秒とする。
        const bool waiting = state.home_return_waits_engine
            ? state.hosted_engine && state.hosted_engine->running()
            : state.hosted_editor && state.hosted_editor->running();
        if (!waiting || SDL_GetTicks() - state.busy_started_at > 30000U)
            state.home_return_busy = false;
    }
#endif
#if defined(__EMSCRIPTEN__) && defined(CARDWIRTH_ALL_HOST)
    poll_web_editor_back(state, true);
#endif
#if defined(CARDWIRTH_ALL_HOST)
    using ClosePhase = RuntimeState::HostClosePhase;
    const auto cancel_host_close = [&] {
        state.host_close_phase = ClosePhase::Idle;
        if (state.hosted_editor) state.hosted_editor->set_input_blocked(false);
        if (state.hosted_engine) {
            const bool covered = state.host_close_origin != cardwirth::app::Page::CardWirthPy;
            state.hosted_engine->set_covered(covered);
            state.hosted_engine->focus(!covered);
        }
        if (state.host_close_shell) *state.shell = *state.host_close_shell;
        state.host_close_shell.reset();
        configure_page(state);
    };
    if (state.host_close_phase == ClosePhase::Requested) {
        state.host_close_phase = ClosePhase::Confirming;
        state.close_editor_after_engine = false;
        // 確認画面を表示できるよう、子ページによる中断を一時的に解除する。
        state.shell->set_initial_page(state.host_close_origin);
        state.editor_home_confirmation_active = false;
        state.editor_home_confirmation_press.reset();
        if (state.hosted_editor && state.hosted_editor->running()) {
            state.hosted_editor->set_input_blocked(false);
            state.shell->navigate(cardwirth::app::Page::XEditor);
            configure_page(state);
            bool accepted = false;
            try {
                accepted = state.hosted_editor->prepare_stop();
            } catch (const std::exception& error) {
                cancel_host_close();
                state.shell->set_status(std::string("終了前の保存確認に失敗しました: ") + error.what());
                return SDL_APP_CONTINUE;
            }
            if (!accepted) {
                cancel_host_close();
                return SDL_APP_CONTINUE;
            }
            state.hosted_editor->set_input_blocked(true);
        }
        if (state.hosted_engine && state.hosted_engine->running()) {
            state.host_close_phase = ClosePhase::Engine;
            state.hosted_engine->set_covered(false);
            state.hosted_engine->focus(true);
            state.shell->navigate(cardwirth::app::Page::CardWirthPy);
            configure_page(state);
            state.hosted_engine->request_stop();
        } else {
            state.host_close_phase = ClosePhase::Editor;
            if (state.hosted_editor) state.hosted_editor->request_stop();
        }
    }
    if (state.host_close_phase == ClosePhase::Engine) {
        if (state.hosted_engine && state.hosted_engine->close_cancelled()) {
            cancel_host_close();
        } else if (!state.hosted_engine || state.hosted_engine->finished()) {
            state.host_close_phase = ClosePhase::Editor;
            if (state.hosted_editor) state.hosted_editor->request_stop();
        }
    }
    if (state.host_close_phase == ClosePhase::Editor &&
        (!state.hosted_editor || state.hosted_editor->finished()) &&
        (!state.hosted_engine || state.hosted_engine->finished())) {
        state.quit_requested = true;
        return SDL_APP_CONTINUE;
    }
    flush_all_layout_resize(state);
#endif
    std::erase_if(state.retiring_explorers, [](const auto& explorer) {
        return !explorer->dispatching() && !explorer->controller().io_service().busy();
    });
    if (state.explorer && state.explorer->dispatch_async() != 0) {
        if (auto document = state.explorer->controller().take_opened_text()) {
            state.explorer_modal_error.clear();
            state.explorer_modal.show_open_text(std::move(*document));
        }
        configure_page(state);
    }
    // アーカイブのI/Oを始める前に、展開のモーダル画面を1フレーム描
    // く。 ワーカーの完了通知が配送されるまで、排他的な状態を保つ。
    start_pending_explorer_extraction(state);
    if (state.launch_editor_on_start) {
        state.launch_editor_on_start = false;
        launch_editor(state, cardwirth::app::Page::Home);
    }
    if (state.launch_engine_on_start) {
        state.launch_engine_on_start = false;
        launch_engine(state, cardwirth::app::Page::Home);
    }
#if defined(CARDWIRTH_ALL_HOST)
    if (state.finger_tracking && !state.finger_scrolled &&
        !state.all_long_press_context_sent && state.hosted_engine &&
        state.hosted_engine->running() &&
        state.shell->page() == cardwirth::app::Page::CardWirthPy &&
        SDL_GetTicks() - state.finger_down_at >= 650) {
        const float x = state.all_touch_last_x;
        const float y = state.all_touch_last_y;
        const auto debugger = hosted_debugger_point(state, x, y);
        // この指を押し始めた時点で開いていたダイアログだけを
        // 対象にする。 PCカードの長押しで情報画面が開く場
        // 合など、同じ操作が開いたダイアログへ、 ホストのタ
        // イマーから2回目の右クリックを連続して送らない。
        const auto modal = state.hosted_engine->modal_snapshot();
        const auto dialog = modal && modal->model_id == state.hosted_touch_down_modal
            ? hosted_dialog_point(state, x, y) : std::nullopt;
        if (dialog && state.hosted_dialog_pointer.continuous() &&
            !state.hosted_dialog_pointer.long_press_context()) {
            // 押下中のスピナー・スライダーは、解放まで主タッチを所有する
            // 。 同じ操作をContextへ切り替えると、wxと異なり
            // 数値編集が取り消され、 ダイアログまで閉じる場合がある。
            state.all_long_press_context_sent = true;
        } else if (dialog || debugger) {
            // スマホ・タブレットのALLには物理的な副ボタンがない。保留中
            // の主タッチを取り消してから、 デスクトップの右クリックと同じ
            // 経路へ送る。ゲーム画面の長押しはEventHandlerだけ
            // が管理し、 ホスト側の別タイマーで入力キューと競合させない。
            cancel_hosted_engine_pointer(state);
            state.all_long_press_context_sent = true;
            if (debugger)
                state.hosted_engine->debugger_context(debugger->x, debugger->y);
            else if (dialog)
                state.hosted_engine->dialog_context(dialog->x, dialog->y);
        }
    }
#endif
    if (state.finger_tracking && !state.finger_scrolled && !state.finger_selecting &&
        state.finger_start_row && !state.explorer_modal.active() && state.explorer &&
        SDL_GetTicks() - state.finger_down_at >= 450) {
        const auto row = *state.finger_start_row;
        (void)state.explorer->controller().select(row, false);
        state.finger_selecting = true;
        state.finger_last_selection_row = row;
        configure_page(state);
    }
    if (state.child && !state.child->running()) {
        const int exit_code = state.child->exit_code();
        state.child.reset();
        state.shell->end_child(exit_code);
        SDL_ShowWindow(state.window);
    }
#if defined(CARDWIRTH_ALL_HOST)
    if (state.hosted_editor) {
        state.hosted_editor->poll_background();
        process_pending_editor_navigation(state);
        if (state.hosted_editor->running() &&
            state.shell->page() == cardwirth::app::Page::XEditor)
            state.hosted_editor->tick(false);
        if (state.editor_zoom_test && state.hosted_editor->running()) {
            const Uint64 now = SDL_GetTicks();
            if (state.editor_zoom_test_started_at == 0)
                state.editor_zoom_test_started_at = now;
            const auto push_finger = [&state](SDL_EventType type, SDL_FingerID finger,
                                               float x, float y) {
                SDL_Event event{};
                event.type = type;
                event.tfinger.type = type;
                event.tfinger.timestamp = SDL_GetTicksNS();
                event.tfinger.touchID = static_cast<SDL_TouchID>(1);
                event.tfinger.fingerID = finger;
                event.tfinger.windowID = SDL_GetWindowID(state.window);
                event.tfinger.x = x;
                event.tfinger.y = y;
                event.tfinger.pressure = type == SDL_EVENT_FINGER_UP ? 0.0F : 1.0F;
                SDL_PushEvent(&event);
            };
            const Uint64 elapsed = now - state.editor_zoom_test_started_at;
            if (state.editor_zoom_test_stage == 0 && elapsed >= 2500) {
                for (const auto& entry : state.buttons) {
                    if (entry.button && entry.button->text() == "ホームに戻る") {
                        state.editor_zoom_test_home_before = entry.rect;
                        break;
                    }
                }
                push_finger(SDL_EVENT_FINGER_DOWN, static_cast<SDL_FingerID>(101),
                            0.35F, 0.50F);
                state.editor_zoom_test_stage = 1;
                state.editor_zoom_test_started_at = now;
            } else if (state.editor_zoom_test_stage == 1 && elapsed >= 80) {
                push_finger(SDL_EVENT_FINGER_DOWN, static_cast<SDL_FingerID>(102),
                            0.65F, 0.50F);
                state.editor_zoom_test_stage = 2;
                state.editor_zoom_test_started_at = now;
            } else if (state.editor_zoom_test_stage == 2 && elapsed >= 80) {
                push_finger(SDL_EVENT_FINGER_MOTION, static_cast<SDL_FingerID>(101),
                            0.20F, 0.50F);
                state.editor_zoom_test_stage = 3;
                state.editor_zoom_test_started_at = now;
            } else if (state.editor_zoom_test_stage == 3 && elapsed >= 80) {
                // 2本目の指を動かして間隔と位置を同時に変え、
                // 拡大と中点を保つ移動を組み合わせて検証する。
                push_finger(SDL_EVENT_FINGER_MOTION, static_cast<SDL_FingerID>(102),
                            0.90F, 0.50F);
                state.editor_zoom_test_stage = 4;
                state.editor_zoom_test_started_at = now;
            } else if (state.editor_zoom_test_stage == 4 && elapsed >= 120) {
                push_finger(SDL_EVENT_FINGER_UP, static_cast<SDL_FingerID>(101),
                            0.20F, 0.50F);
                state.editor_zoom_test_stage = 5;
                state.editor_zoom_test_started_at = now;
            } else if (state.editor_zoom_test_stage == 5 && elapsed >= 80) {
                push_finger(SDL_EVENT_FINGER_UP, static_cast<SDL_FingerID>(102),
                            0.90F, 0.50F);
                state.editor_zoom_test_stage = 6;
                state.editor_zoom_test_started_at = now;
            } else if (state.editor_zoom_test_stage == 6 && elapsed >= 250 &&
                       state.editor_fingers.empty() && !state.editor_pinch_active) {
                cardwirth::ui::Rect home{};
                bool home_fixed = false;
                for (const auto& entry : state.buttons) {
                    if (entry.button && entry.button->text() == "ホームに戻る") {
                        home = entry.rect;
                        if (state.editor_zoom_test_home_before) {
                            const auto& before = *state.editor_zoom_test_home_before;
                            home_fixed = std::abs(home.x - before.x) < 0.01F &&
                                         std::abs(home.y - before.y) < 0.01F &&
                                         std::abs(home.width - before.width) < 0.01F &&
                                         std::abs(home.height - before.height) < 0.01F;
                        }
                        break;
                    }
                }
                const bool quantized = std::abs(
                    std::round(state.editor_zoom / kEditorZoomStep) * kEditorZoomStep -
                    state.editor_zoom) < 0.001F;
                const bool no_accidental_home =
                    state.shell->page() == cardwirth::app::Page::XEditor &&
                    state.hosted_editor && state.hosted_editor->running();
                if (!state.editor_zoom_test_report.empty()) {
                    std::ofstream report(state.editor_zoom_test_report,
                                         std::ios::binary | std::ios::trunc);
                    report << std::fixed << std::setprecision(3)
                           << "zoom=" << state.editor_zoom << "\n"
                           << "offset_x=" << state.editor_offset_x << "\n"
                           << "offset_y=" << state.editor_offset_y << "\n"
                           << "min_zoom=" << kEditorZoomMinimum << "\n"
                           << "max_zoom=" << kEditorZoomMaximum << "\n"
                           << "zoom_step=" << kEditorZoomStep << "\n"
                           << "quantized=" << (quantized ? 1 : 0) << "\n"
                           << "gesture_complete=1\n"
                           << "home_fixed=" << (home_fixed ? 1 : 0) << "\n"
                           << "home_x=" << home.x << "\n"
                           << "home_y=" << home.y << "\n"
                           << "home_width=" << home.width << "\n"
                           << "home_height=" << home.height << "\n"
                           << "root_x=" << state.all_layout_snapshot.host_rect.x << "\n"
                           << "root_y=" << state.all_layout_snapshot.host_rect.y << "\n"
                           << "root_width=" << state.all_layout_snapshot.host_rect.width << "\n"
                           << "root_height=" << state.all_layout_snapshot.host_rect.height << "\n"
                           << "no_accidental_home=" << (no_accidental_home ? 1 : 0)
                           << "\n";
                }
                state.editor_zoom_test_stage = 7;
            }
        }
        if (state.hosted_editor->finished()) {
            state.home_return_busy = false;
            const int exit_code = state.hosted_editor->exit_code();
            const auto error = state.hosted_editor->error();
            state.hosted_editor.reset();
            // ホームへ戻ると実行中のセッションは破棄するが、復元用の保存
            // 設定は残す。 自動再開を抑止するのは失敗したセッションだけ
            // で、 通常の再起動では前回のシナリオと場面タブを復元する。
            state.reset_editor_on_next_launch = exit_code != 0;
            state.editor_home_confirmation_active = false;
            state.editor_home_confirmation_press.reset();
            state.editor_fingers.clear();
            state.editor_pinch_active = false;
            SDL_DestroyTexture(state.hosted_editor_canvas);
            state.hosted_editor_canvas = nullptr;
            state.shell->set_initial_page(cardwirth::app::Page::Home);
            state.shell->set_status(error.empty()
                ? (exit_code == 0 ? "Editorを終了しました"
                                  : "Editorが終了しました (code=" +
                                        std::to_string(exit_code) + ")")
                : "Editor error: " + error);
            configure_page(state);
        }
    }
    if (state.hosted_engine) {
        if (state.close_editor_after_engine && state.hosted_engine->close_cancelled()) {
            // キャンセルは所有者を閉じる要求全体の取り消しを意味する。 Engine
            // が終了した後でEditorを閉じる予約としては扱わない。
            state.close_editor_after_engine = false;
            if (state.shell->page() == cardwirth::app::Page::CardWirthPy &&
                state.shell->back_page() == cardwirth::app::Page::XEditor) {
                state.hosted_engine->set_covered(true);
                state.hosted_engine->focus(false);
                state.all_dialog_layer.clear();
                state.shell->go_back();
                configure_page(state);
            }
        }
        if (state.hosted_engine_smoke_test &&
            state.hosted_engine_frame_sequence != 0 &&
            state.hosted_test_dialog_kind &&
            !state.hosted_test_dialog_sent) {
            state.hosted_engine->show_dialog_for_test(*state.hosted_test_dialog_kind);
            state.hosted_test_dialog_sent = true;
        }
        // クリックのスモークテストでは、実際のカード操作を実行する。 開いた
        // Engineのダイアログが非表示のネイティブモーダルループへ入らな
        // いよう、 プロセス内で確認し、製品のダイアログ動作自体は変えない。
        if (state.hosted_engine_smoke_test && state.hosted_engine_smoke_action_observed &&
            state.hosted_engine->modal_snapshot())
            state.hosted_engine->dialog_action(
                cardwirth::app::HostedEngineDialogAction::Cancel);
        else if (state.all_layout_snapshot.engine_debug) {
            state.all_dialog_layer.update(
                state.native_host_window,
                state.all_layout_snapshot.engine_debug->engine_slot_rect,
                state.all_layout_snapshot.engine_debug->debugger_rect,
                state.all_layout_snapshot.engine_debug->scale);
            const auto native_dialog_size =
                state.all_dialog_layer.native_engine_dialog_intrinsic_size();
            if (!same_optional_size(native_dialog_size,
                                    state.native_engine_dialog_size)) {
                state.native_engine_dialog_size = native_dialog_size;
                refresh_all_layout(state);
            }
        }
        const auto debugger = state.hosted_engine->debugger_snapshot();
        if (debugger.sequence != state.hosted_debugger_sequence) {
            state.hosted_debugger_sequence = debugger.sequence;
            // マウス押下から解放までの間に、選択の再描画でモデルの更新番号
            // が進む場合がある。 操作中の共通部品とフォーカスを維持し、
            // ここで解除してクリックやEnterが失われないようにする。
            if (!debugger.visible) {
                state.pending_debugger_control.reset();
                state.pending_debugger_activate = false;
                state.hosted_debugger_focused = false;
                if(state.hosted_engine) state.hosted_engine->debugger_blur();
            }
            refresh_all_layout(state);
        }
        const auto status = state.hosted_engine->status_snapshot();
        if (status.sequence != state.hosted_status_sequence) {
            state.hosted_status_sequence = status.sequence;
            // Engineのテクスチャ寸法が変わらなくても、設定で表示状
            // 態が変わる場合がある。 毎フレーム可変のEngine UI
            // を調べず、不変のステータス通知ごとに一度だけ再計算する。
            refresh_all_layout(state);
        }
        const auto modal = state.hosted_engine->modal_snapshot();
        const std::uint64_t modal_sequence = modal ? modal->sequence : 0;
        if (modal_sequence != state.hosted_engine_modal_sequence) {
            state.hosted_engine_modal_sequence = modal_sequence;
            cardwirth::app::synchronize_hosted_dialog_pointer(
                state.hosted_dialog_pointer, state.hosted_engine_modal_model_id, modal);
            state.hosted_engine->displayed_dialog_model(
                state.hosted_engine_modal_model_id);
            refresh_all_layout(state);
            update_text_input(state);
        }
        if (state.hosted_status_action_test &&
            state.hosted_engine_frame_sequence != 0 &&
            !state.hosted_status_action_test_stop_requested) {
            const auto now = SDL_GetTicks();
            const auto layout = state.all_layout_snapshot.engine_debug;
            const auto find_action = [&status](const auto action) {
                return std::find_if(status.buttons.begin(), status.buttons.end(),
                    [action](const auto& button) { return button.action == action; });
            };
            // 最初の非同期フレームは、ステータスバーの初期化前に
            // 届く場合がある。 必要な部品が揃ってから操作テスト
            // を開始し、揃わない場合はタイムアウトで失敗とする。
            const bool primary_actions_ready =
                find_action(cardwirth::app::HostedStatusAction::Settings) != status.buttons.end() &&
                find_action(cardwirth::app::HostedStatusAction::F9) != status.buttons.end() &&
                find_action(cardwirth::app::HostedStatusAction::Debugger) != status.buttons.end();
            if (state.hosted_status_action_test_stage == 0 && status.visible && layout &&
                primary_actions_ready &&
                layout->status_dialog_visible && layout->status_dialog_rect.valid() &&
                layout->engine_content_rect.valid()) {
                const float expected_ratio = 90.0F / 632.0F;
                const float actual_ratio = layout->status_dialog_rect.width /
                    layout->engine_content_rect.width;
                const bool adjacent = std::abs(layout->status_dialog_rect.right() -
                                                layout->engine_content_rect.x) < 1.5F;
                state.hosted_status_geometry_scaled =
                    adjacent && std::abs(actual_ratio - expected_ratio) < 0.015F;
                state.hosted_status_all_primary_actions_present =
                    find_action(cardwirth::app::HostedStatusAction::Settings) !=
                        status.buttons.end() &&
                    find_action(cardwirth::app::HostedStatusAction::F9) !=
                        status.buttons.end() &&
                    find_action(cardwirth::app::HostedStatusAction::Debugger) !=
                        status.buttons.end();
                state.hosted_engine->status_action(
                    cardwirth::app::HostedStatusAction::Settings);
                state.hosted_status_action_test_stage = 1;
                state.hosted_status_action_test_stage_at = now;
            } else if (state.hosted_status_action_test_stage == 1 && modal &&
                       modal->kind == cardwirth::app::HostedEngineModalKind::Settings) {
                state.hosted_status_settings_opened = true;
                state.hosted_engine->dialog_action(
                    cardwirth::app::HostedEngineDialogAction::Cancel);
                state.hosted_status_action_test_stage = 2;
                state.hosted_status_action_test_stage_at = now;
            } else if (state.hosted_status_action_test_stage == 2 && !modal &&
                       now - state.hosted_status_action_test_stage_at >= 300) {
                state.hosted_status_settings_closed = true;
                state.hosted_engine->status_action(
                    cardwirth::app::HostedStatusAction::Debugger);
                state.hosted_status_action_test_stage = 3;
                state.hosted_status_action_test_stage_at = now;
            } else if (state.hosted_status_action_test_stage == 3 && debugger.visible &&
                       layout && layout->debugger_visible) {
                state.hosted_status_debugger_opened = true;
                state.hosted_status_debugger_suppressed_panel =
                    !layout->status_dialog_visible;
                const auto cropped =
                    cardwirth::app::all::status_dialog_engine_source_size({
                        static_cast<float>(state.hosted_engine_texture_width),
                        static_cast<float>(state.hosted_engine_texture_height)});
                // Debugの表示中は縦型のステータス表示が隠れる
                // 。 SIZE_AREAだけを切り抜かず、下部バー
                // を含むEngine画像全体を表示領域へ収める。
                const bool frame_has_bottom_bar =
                    static_cast<float>(state.hosted_engine_texture_height) >
                    cropped.height + 0.5F;
                const float source_ratio = state.hosted_engine_texture_height > 0
                    ? static_cast<float>(state.hosted_engine_texture_width) /
                        static_cast<float>(state.hosted_engine_texture_height)
                    : 0.0F;
                const float visible_ratio = layout->engine_content_rect.height > 0.0F
                    ? layout->engine_content_rect.width /
                        layout->engine_content_rect.height
                    : 0.0F;
                state.hosted_status_debugger_bottom_bar_visible =
                    frame_has_bottom_bar &&
                    std::abs(source_ratio - visible_ratio) < 0.015F;
                state.hosted_engine->debugger_command(
                    cardwirth::app::HostedDebuggerCommand::Close);
                state.hosted_status_action_test_stage = 4;
                state.hosted_status_action_test_stage_at = now;
            } else if (state.hosted_status_action_test_stage == 4 && !debugger.visible &&
                       layout && layout->status_dialog_visible) {
                state.hosted_status_debugger_closed = true;
                state.hosted_status_panel_restored = true;
                state.hosted_status_action_test_stop_requested = true;
                state.hosted_engine->request_stop();
            }
        }
        if (state.hosted_layout_test &&
            state.hosted_engine_frame_sequence != 0) {
            const Uint64 now = SDL_GetTicks();
            if (state.hosted_layout_test_stage == 0 &&
                now - state.started_at >= 9000 &&
                state.all_layout_snapshot.engine_debug) {
                state.hosted_layout_engine_alone =
                    state.all_layout_snapshot.engine_debug->engine_rect;
                state.hosted_engine->focus(true);
                // OSのアクセラレーターやキー解放の競合には依存せず、
                // 同じ完全なデバッガーモデルを明示的に要求する。 公開さ
                // れたスナップショットが表示されてから、配置を記録する。
                state.hosted_engine->debugger_command(
                    cardwirth::app::HostedDebuggerCommand::Show);
                state.hosted_layout_test_stage = 1;
                state.hosted_layout_test_stage_at = now;
            } else if (state.hosted_layout_test_stage == 1 &&
                       debugger.visible && state.all_layout_snapshot.engine_debug &&
                       state.all_layout_snapshot.engine_debug->debugger_visible) {
                const auto layout = state.all_layout_snapshot.engine_debug;
                const auto alone = state.hosted_layout_engine_alone;
                const auto root = state.all_layout_snapshot.host_rect;
                const auto safe = state.all_layout_snapshot.safe_rect;
                const auto nearly_equal = [](const float left, const float right) {
                    return std::abs(left - right) < 1.5F;
                };
                const auto same_rect = [&nearly_equal](const cardwirth::app::all::Rect& left,
                                                       const cardwirth::app::all::Rect& right) {
                    return nearly_equal(left.x, right.x) &&
                           nearly_equal(left.y, right.y) &&
                           nearly_equal(left.width, right.width) &&
                           nearly_equal(left.height, right.height);
                };
                const bool root_matches_safe = same_rect(root, safe);
                const bool alone_fills_root = alone && same_rect(*alone, root);
                const bool horizontal = layout && layout->debugger_visible &&
                    layout->debugger_rect.valid() &&
                    (layout->debugger_rect.x >= layout->engine_slot_rect.right() - 0.5F ||
                     layout->engine_slot_rect.x >= layout->debugger_rect.right() - 0.5F);
                const float union_left = std::min(layout->engine_rect.x,
                                                  layout->debugger_rect.x);
                const float union_top = std::min(layout->engine_rect.y,
                                                 layout->debugger_rect.y);
                const float union_right = std::max(layout->engine_rect.right(),
                                                   layout->debugger_rect.right());
                const float union_bottom = std::max(layout->engine_rect.bottom(),
                                                    layout->debugger_rect.bottom());
                const bool group_fills_root = same_rect(layout->group_rect, root);
                const bool tiled_fills_root = nearly_equal(union_left, root.x) &&
                    nearly_equal(union_top, root.y) &&
                    nearly_equal(union_right, root.right()) &&
                    nearly_equal(union_bottom, root.bottom()) &&
                    nearly_equal(layout->gap, 0.0F);
                const bool vertical_edges_fill =
                    nearly_equal(layout->engine_rect.y, root.y) &&
                    nearly_equal(layout->engine_rect.bottom(), root.bottom()) &&
                    nearly_equal(layout->debugger_rect.y, root.y) &&
                    nearly_equal(layout->debugger_rect.bottom(), root.bottom());
                const bool layout_pass = debugger.visible && root_matches_safe &&
                    alone_fills_root && horizontal && group_fills_root &&
                    tiled_fills_root && vertical_edges_fill;
                if (!state.hosted_layout_test_report.empty()) {
                    std::ofstream report(state.hosted_layout_test_report,
                                         std::ios::binary | std::ios::trunc);
                    report << std::fixed << std::setprecision(3)
                           << "debugger_visible=" << (debugger.visible ? 1 : 0) << "\n"
                           << "horizontal=" << (horizontal ? 1 : 0) << "\n"
                           << "root_matches_safe=" << (root_matches_safe ? 1 : 0) << "\n"
                           << "alone_fills_root=" << (alone_fills_root ? 1 : 0) << "\n"
                           << "group_fills_root=" << (group_fills_root ? 1 : 0) << "\n"
                           << "tiled_fills_root=" << (tiled_fills_root ? 1 : 0) << "\n"
                           << "vertical_edges_fill=" << (vertical_edges_fill ? 1 : 0) << "\n"
                           << "outer_padding=0\n"
                           << "layout_pass=" << (layout_pass ? 1 : 0) << "\n"
                           << "window_x=" << state.window_rect.x << "\n"
                           << "window_y=" << state.window_rect.y << "\n"
                           << "window_width=" << state.window_rect.width << "\n"
                           << "window_height=" << state.window_rect.height << "\n"
                           << "render_output_width=" << state.render_output_width << "\n"
                           << "render_output_height=" << state.render_output_height << "\n"
                           << "display_x=" << state.display_rect.x << "\n"
                           << "display_y=" << state.display_rect.y << "\n"
                           << "display_width=" << state.display_rect.width << "\n"
                           << "display_height=" << state.display_rect.height << "\n"
                           << "display_usable_x=" << state.display_usable_rect.x << "\n"
                           << "display_usable_y=" << state.display_usable_rect.y << "\n"
                           << "display_usable_width=" << state.display_usable_rect.width << "\n"
                           << "display_usable_height=" << state.display_usable_rect.height << "\n"
                           << "safe_x=" << safe.x << "\n"
                           << "safe_y=" << safe.y << "\n"
                           << "safe_width=" << safe.width << "\n"
                           << "safe_height=" << safe.height << "\n"
                           << "root_x=" << root.x << "\n"
                           << "root_y=" << root.y << "\n"
                           << "root_width=" << root.width << "\n"
                           << "root_height=" << root.height << "\n";
                    if (alone)
                        report << "alone_x=" << alone->x << "\n"
                               << "alone_y=" << alone->y << "\n"
                               << "alone_width=" << alone->width << "\n"
                               << "alone_height=" << alone->height << "\n";
                    if (layout)
                        report << "group_scale=" << layout->scale << "\n"
                               << "engine_x=" << layout->engine_rect.x << "\n"
                               << "engine_y=" << layout->engine_rect.y << "\n"
                               << "engine_width=" << layout->engine_rect.width << "\n"
                               << "engine_height=" << layout->engine_rect.height << "\n"
                               << "engine_content_x=" << layout->engine_content_rect.x << "\n"
                               << "engine_content_y=" << layout->engine_content_rect.y << "\n"
                               << "engine_content_width=" << layout->engine_content_rect.width << "\n"
                               << "engine_content_height=" << layout->engine_content_rect.height << "\n"
                               << "debugger_x=" << layout->debugger_rect.x << "\n"
                               << "debugger_y=" << layout->debugger_rect.y << "\n"
                               << "debugger_width=" << layout->debugger_rect.width << "\n"
                               << "debugger_height=" << layout->debugger_rect.height << "\n"
                               << "engine_debug_gap=" << layout->gap << "\n";
                }
                state.hosted_layout_test_stage = 2;
            }
        }
        if (state.hosted_key_input_test &&
            state.hosted_engine_frame_sequence != 0 &&
            !state.hosted_key_input_test_stop_requested) {
            const Uint64 now = SDL_GetTicks();
            const auto probe = state.hosted_engine->input_probe();
            const auto probe_has_key = [&probe](const int key) {
                const auto marker = probe.find(" keys=");
                if (marker == std::string::npos) return false;
                const std::string wanted = std::to_string(key) + ':';
                auto position = marker + 6;
                while (position < probe.size() && probe[position] != ' ') {
                    if (probe.compare(position, wanted.size(), wanted) == 0)
                        return true;
                    const auto comma = probe.find(',', position);
                    if (comma == std::string::npos) break;
                    position = comma + 1;
                }
                return false;
            };
            const auto push_key = [&state](const Uint32 type, const SDL_Keycode key,
                                            const SDL_Keymod modifiers = SDL_KMOD_NONE) {
                SDL_Event event{};
                event.type = type;
                event.key.windowID = SDL_GetWindowID(state.window);
                event.key.key = key;
                event.key.mod = modifiers;
                event.key.down = type == SDL_EVENT_KEY_DOWN;
                SDL_PushEvent(&event);
            };
            const auto advance = [&state, now](const int stage) {
                state.hosted_key_input_test_stage = stage;
                state.hosted_key_input_test_stage_at = now;
            };
            if (state.hosted_key_input_test_stage == 0 &&
                now - state.started_at >= 2500) {
                state.hosted_engine->focus(true);
                push_key(SDL_EVENT_KEY_DOWN, SDLK_BACKSLASH);
                advance(1);
            } else if (state.hosted_key_input_test_stage == 1 &&
                       probe_has_key(92)) {
                state.hosted_key_backslash_down = true;
                push_key(SDL_EVENT_KEY_UP, SDLK_BACKSLASH);
                advance(2);
            } else if (state.hosted_key_input_test_stage == 1 &&
                       now - state.hosted_key_input_test_stage_at >= 350) {
                // タイトル画面の遷移は押下状態を消去するため、 同じ物
                // 理入力の段階を繰り返し、状態が通知されるまで待つ。
                push_key(SDL_EVENT_KEY_DOWN, SDLK_BACKSLASH);
                state.hosted_key_input_test_stage_at = now;
            } else if (state.hosted_key_input_test_stage == 2 &&
                       !probe_has_key(92)) {
                state.hosted_key_backslash_up = true;
                push_key(SDL_EVENT_KEY_DOWN, SDLK_RCTRL, SDL_KMOD_RCTRL);
                advance(3);
            } else if (state.hosted_key_input_test_stage == 2 &&
                       now - state.hosted_key_input_test_stage_at >= 350) {
                push_key(SDL_EVENT_KEY_UP, SDLK_BACKSLASH);
                state.hosted_key_input_test_stage_at = now;
            } else if (state.hosted_key_input_test_stage == 3 &&
                       probe_has_key(306)) {
                state.hosted_key_right_ctrl_down = true;
                push_key(SDL_EVENT_KEY_UP, SDLK_RCTRL);
                advance(4);
            } else if (state.hosted_key_input_test_stage == 3 &&
                       now - state.hosted_key_input_test_stage_at >= 350) {
                push_key(SDL_EVENT_KEY_DOWN, SDLK_RCTRL, SDL_KMOD_RCTRL);
                state.hosted_key_input_test_stage_at = now;
            } else if (state.hosted_key_input_test_stage == 4 &&
                       !probe_has_key(306)) {
                state.hosted_key_right_ctrl_up = true;
                push_key(SDL_EVENT_KEY_DOWN, SDLK_LEFT);
                advance(5);
            } else if (state.hosted_key_input_test_stage == 4 &&
                       now - state.hosted_key_input_test_stage_at >= 350) {
                push_key(SDL_EVENT_KEY_UP, SDLK_RCTRL);
                state.hosted_key_input_test_stage_at = now;
            } else if (state.hosted_key_input_test_stage == 5 &&
                       probe_has_key(276)) {
                state.hosted_key_left_down = true;
                SDL_Event focus{};
                focus.type = SDL_EVENT_WINDOW_FOCUS_LOST;
                focus.window.windowID = SDL_GetWindowID(state.window);
                SDL_PushEvent(&focus);
                advance(6);
            } else if (state.hosted_key_input_test_stage == 5 &&
                       now - state.hosted_key_input_test_stage_at >= 350) {
                push_key(SDL_EVENT_KEY_DOWN, SDLK_LEFT);
                state.hosted_key_input_test_stage_at = now;
            } else if (state.hosted_key_input_test_stage == 6 &&
                       now - state.hosted_key_input_test_stage_at >= 350) {
                SDL_Event focus{};
                focus.type = SDL_EVENT_WINDOW_FOCUS_LOST;
                focus.window.windowID = SDL_GetWindowID(state.window);
                SDL_PushEvent(&focus);
                state.hosted_key_input_test_stage_at = now;
            } else if (state.hosted_key_input_test_stage == 6 &&
                       !probe_has_key(276)) {
                state.hosted_key_focus_cleared = true;
                // フォーカスを失った後に遅れて届く物理UPで、操作が発生しないことを確認する。
                push_key(SDL_EVENT_KEY_UP, SDLK_LEFT);
                SDL_Event focus{};
                focus.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
                focus.window.windowID = SDL_GetWindowID(state.window);
                SDL_PushEvent(&focus);
                advance(7);
            } else if (state.hosted_key_input_test_stage == 7 &&
                       now - state.hosted_key_input_test_stage_at >= 150 &&
                       !probe_has_key(276)) {
                state.hosted_key_late_up_clean = true;
                const bool passed = state.hosted_key_backslash_down &&
                    state.hosted_key_backslash_up &&
                    state.hosted_key_right_ctrl_down &&
                    state.hosted_key_right_ctrl_up &&
                    state.hosted_key_left_down && state.hosted_key_focus_cleared &&
                    state.hosted_key_late_up_clean;
                if (!state.hosted_key_input_test_report.empty()) {
                    std::ofstream report(state.hosted_key_input_test_report,
                                         std::ios::binary | std::ios::trunc);
                    report << "had_frame=1\n"
                           << "backslash_down=" << (state.hosted_key_backslash_down ? 1 : 0) << "\n"
                           << "backslash_up=" << (state.hosted_key_backslash_up ? 1 : 0) << "\n"
                           << "right_ctrl_down=" << (state.hosted_key_right_ctrl_down ? 1 : 0) << "\n"
                           << "right_ctrl_up=" << (state.hosted_key_right_ctrl_up ? 1 : 0) << "\n"
                           << "left_down=" << (state.hosted_key_left_down ? 1 : 0) << "\n"
                           << "focus_cleared=" << (state.hosted_key_focus_cleared ? 1 : 0) << "\n"
                           << "late_up_clean=" << (state.hosted_key_late_up_clean ? 1 : 0) << "\n"
                           << "passed=" << (passed ? 1 : 0) << "\n";
                }
                state.hosted_key_input_test_stop_requested = true;
                state.hosted_engine->request_stop();
                state.smoke_failed = !passed;
                state.quit_requested = true;
            }
        }
        if (state.hosted_engine_smoke_test &&
            state.hosted_engine_frame_sequence != 0 &&
            !state.hosted_engine_smoke_stop_requested) {
            const auto now = SDL_GetTicks();
            const auto frame = state.hosted_engine->frame_snapshot();
            const auto push_start_click = [&state, &frame] {
                // 宿が未選択でも、タイトルのStartカードを固定
                // の操作対象にできる。 最初のクリックで長いオープ
                // ニングを打ち切り、set_title()/deal_cards()
                // による 入力消去が終わってから、後のクリックで
                // Startを実行する。 選択アニメーションで
                // カードが移動・拡縮するため、左端ではなく本体
                // の中央を狙う。 端を狙うと、最初の選択には当
                // たっても次の実行クリックが外れる場合がある。
                const int x = frame.surface ? frame.surface->size.width * 7 / 16 : 0;
                const int y = frame.surface ? frame.surface->size.height * 9 / 20 : 0;
                // SDL_WINDOW_HIDDENは、スモークテス
                // ト中にOSのフォーカス喪失イベントを起こす。 実
                // 際のALL入力はフォーカスがある状態で届くため、
                // 疑似ポインター操作のたびにその状態を復元する。
                state.hosted_engine->focus(true);
                // 実際のALLウィンドウと同じSDL_AppEventの経路を
                // 通す。 HostedEngineSessionを直接呼ぶと、
                // このスモークテストが検出するべき入力経路の不具合を通らない。
                const auto& layout = *state.all_layout_snapshot.engine_debug;
                const float host_x = layout.engine_content_rect.x +
                                     static_cast<float>(x) * layout.scale;
                const float host_y = layout.engine_content_rect.y +
                                     static_cast<float>(y) * layout.scale;
                // SDLのマウスイベントはネイティブウィンドウ座標
                // を使う。 上のEngine矩形は描画座標なので、
                // イベントを積む前に逆変換し、 高密度なAndroid
                // 画面で描画変換を二重に適用しないようにする。
                float window_x = host_x;
                float window_y = host_y;
                if (!SDL_RenderCoordinatesToWindow(
                        state.renderer, host_x, host_y, &window_x, &window_y))
                    return;
                SDL_Event motion{};
                motion.type = SDL_EVENT_MOUSE_MOTION;
                motion.motion.windowID = SDL_GetWindowID(state.window);
                motion.motion.x = window_x;
                motion.motion.y = window_y;
                SDL_PushEvent(&motion);
                SDL_Event down{};
                down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
                down.button.windowID = SDL_GetWindowID(state.window);
                down.button.button = SDL_BUTTON_LEFT;
                down.button.down = true;
                down.button.x = window_x;
                down.button.y = window_y;
                SDL_PushEvent(&down);
                SDL_Event up = down;
                up.type = SDL_EVENT_MOUSE_BUTTON_UP;
                up.button.down = false;
                SDL_PushEvent(&up);
            };
            const auto push_selected_activation = [&push_start_click] {
                // オープニングと前のクリックのアニメーションが入力ロ
                // ックを解除してから、 通常のSDLポインター経路で
                // 確定する。Androidでは3秒後の最初のクリック
                // が オープニングに消費され、2回目がStartの選
                // 択だけになる場合がある。 タイトルの動作が落ち着く
                // まで十分に待ってから、選択済みカードを実行する。
                push_start_click();
            };
            if (!state.hosted_engine_smoke_input_sent && now - state.started_at >= 3000) {
                state.hosted_engine_smoke_input_fingerprint = hosted_frame_fingerprint(frame);
                state.hosted_engine_smoke_input_sent = true;
                state.hosted_engine_smoke_input_at = now;
                push_start_click();
            } else if (state.hosted_engine_smoke_input_sent &&
                       now - state.hosted_engine_smoke_input_at >= 250) {
                state.hosted_engine_smoke_input_changed =
                    hosted_frame_fingerprint(frame) != state.hosted_engine_smoke_input_fingerprint;
                state.hosted_engine_smoke_action_observed =
                    state.hosted_engine->modal_snapshot().has_value();
                if (state.hosted_engine_smoke_input_changed &&
                    !state.hosted_engine_smoke_action_observed &&
                    !state.hosted_engine_smoke_activation_sent &&
                    now - state.hosted_engine_smoke_input_at >= 2000) {
                    state.hosted_engine_smoke_activation_sent = true;
                    state.hosted_engine_smoke_activation_at = now;
                    push_start_click();
                }
                if (state.hosted_engine_smoke_activation_sent &&
                    !state.hosted_engine_smoke_action_observed &&
                    !state.hosted_engine_smoke_confirmation_sent &&
                     now - state.hosted_engine_smoke_activation_at >= 6000) {
                    state.hosted_engine_smoke_confirmation_sent = true;
                    state.hosted_engine_smoke_confirmation_at = now;
                    push_selected_activation();
                }
                const bool selection_timed_out =
                    !state.hosted_engine_smoke_activation_sent &&
                    now - state.hosted_engine_smoke_input_at >= 2500;
                const bool activation_timed_out =
                    state.hosted_engine_smoke_confirmation_sent &&
                    now - state.hosted_engine_smoke_confirmation_at >= 3000;
                if (state.hosted_engine_smoke_action_observed || selection_timed_out ||
                    activation_timed_out) {
                    state.hosted_engine_smoke_stop_requested = true;
                    state.hosted_engine_smoke_probe = state.hosted_engine->input_probe();
                    // 終了処理に不具合があっても、ホスト
                    // 側からEngineへポインター入
                    // 力を正常に渡せたかの結果を残す。
                    if (!state.hosted_engine_smoke_report.empty()) {
                        std::ofstream report(state.hosted_engine_smoke_report,
                                             std::ios::binary | std::ios::trunc);
                        report << "had_frame=1\n"
                               << "input_sent=1\n"
                               << "activation_sent="
                               << (state.hosted_engine_smoke_activation_sent ? 1 : 0) << "\n"
                               << "confirmation_sent="
                               << (state.hosted_engine_smoke_confirmation_sent ? 1 : 0) << "\n"
                               << "input_changed="
                               << (state.hosted_engine_smoke_input_changed ? 1 : 0) << "\n"
                               << "action_observed="
                               << (state.hosted_engine_smoke_action_observed ? 1 : 0) << "\n"
                               << "probe=" << state.hosted_engine_smoke_probe << "\n"
                               << "shutdown_pending=1\n";
                    }
                    if (state.hosted_engine->modal_snapshot())
                        state.hosted_engine->dialog_action(
                            cardwirth::app::HostedEngineDialogAction::Cancel);
                    state.hosted_engine->request_stop();
                }
            }
        }
        if (state.hosted_engine->finished()) {
            const bool was_foreground = state.shell->page() == cardwirth::app::Page::CardWirthPy;
            if (was_foreground) {
                state.editor_home_confirmation_active = false;
                state.editor_home_confirmation_press.reset();
            }
            const int exit_code = state.hosted_engine->exit_code();
            const auto error = state.hosted_engine->error();
            const bool had_frame = state.hosted_engine_frame_sequence != 0;
            state.hosted_engine.reset();
            state.all_dialog_layer.clear();
            state.native_engine_dialog_size.reset();
            SDL_DestroyTexture(state.hosted_engine_texture);
            state.hosted_engine_texture = nullptr;
            state.hosted_engine_texture_width = 0;
            state.hosted_engine_texture_height = 0;
            state.hosted_engine_frame_sequence = 0;
            state.hosted_engine_modal_sequence = 0;
            state.hosted_engine_modal_model_id = 0;
            clear_hosted_dialog_texture(state);
            SDL_DestroyTexture(state.hosted_debugger_texture);
            state.hosted_debugger_texture = nullptr;
            state.hosted_debugger_texture_sequence = 0;
            state.hosted_debugger_sequence = 0;
            state.hosted_status_sequence = 0;
            for (auto& [action, cached] : state.hosted_status_textures) {
                (void)action;
                SDL_DestroyTexture(cached.texture);
            }
            state.hosted_status_textures.clear();
            for (auto& cached : state.hosted_money_icon_textures) {
                SDL_DestroyTexture(cached.texture);
                cached = {};
            }
#if defined(CARDWIRTH_ENGINE_ONLY)
            // 単一製品のホストにはホーム・Explorerの移動先がない。
            // 起動失敗を含め、Engineの完了をアプリ全体の完了とする。
            state.quit_requested = true;
#else
            if (was_foreground) state.shell->go_back();
#endif
            if (state.close_editor_after_engine && state.hosted_editor) {
                state.close_editor_after_engine = false;
                state.hosted_editor->request_stop();
                state.home_return_waits_engine = false;
            }
            state.shell->set_status(error.empty()
                ? (exit_code == 0 ? "Engineを終了しました"
                                  : "Engineが終了しました (code=" + std::to_string(exit_code) + ")")
                : "Engine error: " + error);
#if !defined(CARDWIRTH_ENGINE_ONLY)
            configure_page(state);
#endif
            if (state.hosted_engine_smoke_test) {
                state.smoke_failed = exit_code != 0 || !error.empty() || !had_frame ||
                                     !state.hosted_engine_smoke_action_observed;
                if (!state.hosted_engine_smoke_report.empty()) {
                    std::ofstream report(state.hosted_engine_smoke_report,
                                         std::ios::binary | std::ios::trunc);
                    report << "exit_code=" << exit_code << "\n"
                           << "had_frame=" << (had_frame ? 1 : 0) << "\n"
                           << "input_sent=" << (state.hosted_engine_smoke_input_sent ? 1 : 0) << "\n"
                           << "activation_sent=" << (state.hosted_engine_smoke_activation_sent ? 1 : 0) << "\n"
                           << "confirmation_sent=" << (state.hosted_engine_smoke_confirmation_sent ? 1 : 0) << "\n"
                           << "input_changed=" << (state.hosted_engine_smoke_input_changed ? 1 : 0) << "\n"
                           << "action_observed=" << (state.hosted_engine_smoke_action_observed ? 1 : 0) << "\n"
                           << "probe=" << state.hosted_engine_smoke_probe << "\n"
                           << "error=" << error << "\n";
                }
                state.quit_requested = true;
            }
            if (state.hosted_key_input_test) {
                const bool passed = exit_code == 0 && error.empty() && had_frame &&
                    state.hosted_key_backslash_down && state.hosted_key_backslash_up &&
                    state.hosted_key_right_ctrl_down && state.hosted_key_right_ctrl_up &&
                    state.hosted_key_left_down && state.hosted_key_focus_cleared &&
                    state.hosted_key_late_up_clean;
                state.smoke_failed = !passed;
                if (!state.hosted_key_input_test_report.empty()) {
                    std::ofstream report(state.hosted_key_input_test_report,
                                         std::ios::binary | std::ios::trunc);
                    report << "exit_code=" << exit_code << "\n"
                           << "had_frame=" << (had_frame ? 1 : 0) << "\n"
                           << "backslash_down=" << (state.hosted_key_backslash_down ? 1 : 0) << "\n"
                           << "backslash_up=" << (state.hosted_key_backslash_up ? 1 : 0) << "\n"
                           << "right_ctrl_down=" << (state.hosted_key_right_ctrl_down ? 1 : 0) << "\n"
                           << "right_ctrl_up=" << (state.hosted_key_right_ctrl_up ? 1 : 0) << "\n"
                           << "left_down=" << (state.hosted_key_left_down ? 1 : 0) << "\n"
                           << "focus_cleared=" << (state.hosted_key_focus_cleared ? 1 : 0) << "\n"
                           << "late_up_clean=" << (state.hosted_key_late_up_clean ? 1 : 0) << "\n"
                           << "passed=" << (passed ? 1 : 0) << "\n"
                           << "error=" << error << "\n";
                }
                state.quit_requested = true;
            }
            if (state.hosted_status_action_test) {
                const bool passed = exit_code == 0 && error.empty() && had_frame &&
                    state.hosted_status_geometry_scaled &&
                    state.hosted_status_all_primary_actions_present &&
                    state.hosted_status_settings_opened &&
                    state.hosted_status_settings_closed &&
                    state.hosted_status_debugger_opened &&
                    state.hosted_status_debugger_suppressed_panel &&
                    state.hosted_status_debugger_bottom_bar_visible &&
                    state.hosted_status_debugger_closed &&
                    state.hosted_status_panel_restored;
                state.smoke_failed = !passed;
                if (!state.hosted_status_action_test_report.empty()) {
                    std::ofstream report(state.hosted_status_action_test_report,
                                         std::ios::binary | std::ios::trunc);
                    report << "exit_code=" << exit_code << "\n"
                           << "had_frame=" << (had_frame ? 1 : 0) << "\n"
                           << "geometry_scaled=" << (state.hosted_status_geometry_scaled ? 1 : 0) << "\n"
                           << "primary_actions_present=" << (state.hosted_status_all_primary_actions_present ? 1 : 0) << "\n"
                           << "settings_opened=" << (state.hosted_status_settings_opened ? 1 : 0) << "\n"
                           << "settings_closed=" << (state.hosted_status_settings_closed ? 1 : 0) << "\n"
                            << "debugger_opened=" << (state.hosted_status_debugger_opened ? 1 : 0) << "\n"
                            << "debugger_suppressed_panel=" << (state.hosted_status_debugger_suppressed_panel ? 1 : 0) << "\n"
                            << "debugger_bottom_bar_visible=" << (state.hosted_status_debugger_bottom_bar_visible ? 1 : 0) << "\n"
                            << "debugger_closed=" << (state.hosted_status_debugger_closed ? 1 : 0) << "\n"
                           << "panel_restored=" << (state.hosted_status_panel_restored ? 1 : 0) << "\n"
                           << "passed=" << (passed ? 1 : 0) << "\n"
                           << "error=" << error << "\n";
                }
                state.quit_requested = true;
            }
        }
        if (state.hosted_engine_smoke_test &&
             SDL_GetTicks() - state.started_at > 22000) {
            state.smoke_failed = true;
            if (!state.hosted_engine_smoke_report.empty()) {
                std::ofstream report(state.hosted_engine_smoke_report,
                                     std::ios::binary | std::ios::trunc);
                report << "timeout=1\n";
            }
            state.hosted_engine.reset();
            state.quit_requested = true;
        }
        if (state.hosted_status_action_test &&
            SDL_GetTicks() - state.started_at > 22000) {
            state.smoke_failed = true;
            if (!state.hosted_status_action_test_report.empty()) {
                std::ofstream report(state.hosted_status_action_test_report,
                                     std::ios::binary | std::ios::trunc);
                report << "timeout=1\n"
                       << "stage=" << state.hosted_status_action_test_stage << "\n"
                       << "settings_opened=" << (state.hosted_status_settings_opened ? 1 : 0) << "\n"
                       << "debugger_opened=" << (state.hosted_status_debugger_opened ? 1 : 0) << "\n";
            }
            state.hosted_engine.reset();
            state.quit_requested = true;
        }
        if (state.hosted_key_input_test &&
            SDL_GetTicks() - state.started_at > 16000) {
            state.smoke_failed = true;
            if (!state.hosted_key_input_test_report.empty()) {
                std::ofstream report(state.hosted_key_input_test_report,
                                     std::ios::binary | std::ios::trunc);
                report << "timeout=1\n"
                       << "stage=" << state.hosted_key_input_test_stage << "\n"
                       << "backslash_down=" << (state.hosted_key_backslash_down ? 1 : 0) << "\n"
                       << "right_ctrl_down=" << (state.hosted_key_right_ctrl_down ? 1 : 0) << "\n"
                       << "left_down=" << (state.hosted_key_left_down ? 1 : 0) << "\n"
                       << "focus_cleared=" << (state.hosted_key_focus_cleared ? 1 : 0) << "\n";
            }
            state.hosted_engine.reset();
            state.quit_requested = true;
        }
    }
#endif
    // WSLgはデスクトップシェルがアクティブ化するまで、新しいRemoteApp
    // を背景として報告する場合がある。 最初の描画を省略すると、
    // 動作中でもAlt+Tabまで黒いウィンドウのままになるた
    // め、 初回の1フレームはアクティブ状態にかかわらず表示す
    // る。 それ以降の背景フレームは、通常の省電力動作を保つ。
    const bool publish_initial_frame =
        !state.app_in_background || !state.initial_frame_rendered;
    const bool is_first_frame = !state.initial_frame_rendered;
    if (publish_initial_frame) {
        render(state);
        state.initial_frame_rendered = true;
        if (is_first_frame && state.window) {
            // WSLgのRemoteApp画面は最初のフレームが確定してからア
            // クティブ化できる。 SDLで表示した後に、再表示と前面化を行う。
            SDL_ShowWindow(state.window);
            SDL_RaiseWindow(state.window);
        }
    }
    if (state.smoke_test
#if defined(CARDWIRTH_ALL_HOST)
        && !state.hosted_engine_smoke_test && !state.hosted_status_action_test &&
           !state.hosted_key_input_test
#endif
        && SDL_GetTicks() - state.started_at > 500) {
        // 任意のCardWirthPy子実行ファイルがなくても、 シ
        // ェルが初期化と描画を行えることをスモークテストで確認する。
        return state.smoke_failed ? SDL_APP_FAILURE : SDL_APP_SUCCESS;
    }
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result) {
    delete static_cast<RuntimeState*>(appstate);
    SDL_Quit();
#if defined(CARDWIRTH_ALL_HOST) && (defined(_WIN32) || defined(__linux__))
    // Engineには、ALLホスト導入前の破棄順に依存するプロセス寿
    // 命の互換レジストリーが残る。 RuntimeStateと埋め込み
    // セッションは上で順序付きの終了処理を済ませているため、 順序が
    // 定まらないネイティブの静的破棄を省略する。 Windowsでの
    // 終了後のアクセス違反や、Linuxでのアロケーター破損を防ぐ。
    std::_Exit(result == SDL_APP_SUCCESS ? EXIT_SUCCESS : EXIT_FAILURE);
#else
    (void)result;
#endif
}
