#define SDL_MAIN_USE_CALLBACKS 1

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "platform_font.hpp"
#include "runtime_library.hpp"
#include "cw/argparser.hpp"
#include "cw/sprite/statusbar.hpp"
#include "status_text_metrics.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr int kLogicalWidth = 632;
constexpr int kLogicalHeight = 453;
constexpr int kSceneHeight = 420;
constexpr int kButtonCount = 5;

std::vector<std::string> resource_candidates(const std::string& relative) {
    std::vector<std::string> candidates;
    candidates.emplace_back(relative);  // Androidの資産ルート。
    candidates.emplace_back("test_resource/" + relative);

    if (const char* base = SDL_GetBasePath()) {
        const std::string root(base);
        candidates.emplace_back(root + "test_resource/" + relative);
        candidates.emplace_back(root + "../Resources/test_resource/" + relative);  // macOSのアプリバンドル。
        candidates.emplace_back(root + "Resources/test_resource/" + relative);     // iOSのアプリバンドル。
    }

#if defined(CARDWIRTH_RESOURCE_DIR)
    candidates.emplace_back(std::string(CARDWIRTH_RESOURCE_DIR) + "/" + relative);
#endif
    return candidates;
}

SDL_IOStream* open_resource(const std::string& relative) {
    for (const auto& path : resource_candidates(relative)) {
        if (SDL_IOStream* stream = SDL_IOFromFile(path.c_str(), "rb")) {
            return stream;
        }
    }
    SDL_Log("Resource not found: %s", relative.c_str());
    return nullptr;
}

SDL_Texture* load_texture(SDL_Renderer* renderer, const std::string& relative) {
    SDL_IOStream* stream = open_resource(relative);
    if (!stream) {
        return nullptr;
    }
    SDL_Texture* texture = IMG_LoadTexture_IO(renderer, stream, true);
    if (!texture) {
        SDL_Log("Image load failed (%s): %s", relative.c_str(), SDL_GetError());
        return nullptr;
    }
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    return texture;
}

std::shared_ptr<::cw::lib::Surface> copy_rgba_surface(SDL_Surface* source) {
    if (!source || source->w <= 0 || source->h <= 0) {
        return {};
    }
    SDL_Surface* rgba = SDL_ConvertSurface(source, SDL_PIXELFORMAT_RGBA32);
    if (!rgba) {
        SDL_Log("RGBA surface conversion failed: %s", SDL_GetError());
        return {};
    }
    const std::size_t row_bytes = static_cast<std::size_t>(rgba->w) * 4U;
    auto result = std::make_shared<::cw::lib::Surface>();
    result->size = ::cw::lib::Size{static_cast<::cw::lib::Integer>(rgba->w),
                                   static_cast<::cw::lib::Integer>(rgba->h)};
    result->rgba.resize(row_bytes * static_cast<std::size_t>(rgba->h));
    const auto* source_pixels = static_cast<const std::uint8_t*>(rgba->pixels);
    for (int y = 0; y < rgba->h; ++y) {
        std::copy_n(source_pixels + static_cast<std::size_t>(y) *
                        static_cast<std::size_t>(rgba->pitch),
                    row_bytes,
                    result->rgba.data() + static_cast<std::size_t>(y) * row_bytes);
    }
    SDL_DestroySurface(rgba);
    return result;
}

std::vector<std::string> status_resource_candidates(const std::string& resource) {
    std::vector<std::string> candidates;
    const auto add = [&candidates](std::string value) {
        if (value.empty() || std::find(candidates.begin(), candidates.end(), value) != candidates.end()) {
            return;
        }
        candidates.emplace_back(std::move(value));
    };
    add(resource);
    const bool has_extension = resource.find_last_of('.') != std::string::npos;
    const std::array<const char*, 4> prefixes{"", "image/", "skin/", "resource/"};
    const std::array<const char*, 4> extensions{".png", ".bmp", ".webp", ".jpg"};
    for (const char* prefix : prefixes) {
        const std::string base = std::string(prefix) + resource;
        add(base);
        if (!has_extension) {
            for (const char* extension : extensions) {
                add(base + extension);
            }
        }
    }
    return candidates;
}

std::shared_ptr<::cw::lib::Surface> load_status_surface(const std::string& resource) {
    for (const auto& candidate : status_resource_candidates(resource)) {
        if (SDL_IOStream* stream = open_resource(candidate)) {
            SDL_Surface* decoded = IMG_Load_IO(stream, true);
            if (decoded) {
                auto result = copy_rgba_surface(decoded);
                SDL_DestroySurface(decoded);
                if (result) {
                    return result;
                }
            } else {
                SDL_Log("Status image load failed (%s): %s", candidate.c_str(), SDL_GetError());
            }
        }
    }
    return {};
}

struct CachedText {
    SDL_Texture* texture{};
    float width{};
    float height{};
};

class TextRenderer {
public:
    bool initialize(SDL_Renderer* renderer) {
        renderer_ = renderer;
        if (!TTF_Init()) {
            SDL_Log("TTF_Init failed: %s", SDL_GetError());
            return false;
        }
        ttf_initialized_ = true;
        font_path_ = find_platform_font();
        if (font_path_.empty()) {
            SDL_Log("No installed font found; falling back to SDL debug text.");
            return false;
        }
        font_ = TTF_OpenFont(font_path_.c_str(), 16.0F);
        if (!font_) {
            SDL_Log("TTF_OpenFont failed (%s): %s", font_path_.c_str(), SDL_GetError());
            return false;
        }
        SDL_Log("Using system font: %s", font_path_.c_str());
        return true;
    }

    ~TextRenderer() {
        for (auto& [key, entry] : cache_) {
            (void)key;
            SDL_DestroyTexture(entry.texture);
        }
        if (font_) {
            TTF_CloseFont(font_);
        }
        if (ttf_initialized_) {
            TTF_Quit();
        }
    }

    TextRenderer(const TextRenderer&) = delete;
    TextRenderer& operator=(const TextRenderer&) = delete;
    TextRenderer() = default;

    void draw(const std::string& text, float x, float y, SDL_Color color, bool centered = false) {
        if (!renderer_) {
            return;
        }
        if (!font_) {
            const char* fallback = "SYSTEM FONT NOT FOUND";
            SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
            SDL_RenderDebugText(renderer_, x, y, fallback);
            return;
        }

        const std::string key = text + "#" + std::to_string(color.r) + ":" +
                                std::to_string(color.g) + ":" + std::to_string(color.b);
        auto found = cache_.find(key);
        if (found == cache_.end()) {
            SDL_Surface* surface = TTF_RenderText_Blended(font_, text.c_str(), text.size(), color);
            if (!surface) {
                SDL_Log("Text render failed: %s", SDL_GetError());
                return;
            }
            CachedText cached;
            cached.texture = SDL_CreateTextureFromSurface(renderer_, surface);
            cached.width = static_cast<float>(surface->w);
            cached.height = static_cast<float>(surface->h);
            SDL_DestroySurface(surface);
            if (!cached.texture) {
                return;
            }
            found = cache_.emplace(key, cached).first;
        }

        const CachedText& cached = found->second;
        SDL_FRect destination{
            centered ? x - cached.width * 0.5F : x,
            centered ? y - cached.height * 0.5F : y,
            cached.width,
            cached.height,
        };
        SDL_RenderTexture(renderer_, cached.texture, nullptr, &destination);
    }

    [[nodiscard]] std::shared_ptr<::cw::lib::Surface> render_surface(
        const std::string& text, float point_size, SDL_Color color, bool bold) {
        if (!font_ || text.empty()) {
            return {};
        }
        const float old_size = TTF_GetFontSize(font_);
        const TTF_FontStyleFlags old_style = TTF_GetFontStyle(font_);
        const float requested_size = std::clamp(point_size, 1.0F, 256.0F);
        if (!TTF_SetFontSize(font_, requested_size)) {
            SDL_Log("TTF_SetFontSize failed: %s", SDL_GetError());
            return {};
        }
        TTF_FontStyleFlags requested_style = old_style;
        if (bold) {
            requested_style = static_cast<TTF_FontStyleFlags>(requested_style | TTF_STYLE_BOLD);
        } else {
            requested_style = static_cast<TTF_FontStyleFlags>(requested_style & ~TTF_STYLE_BOLD);
        }
        TTF_SetFontStyle(font_, requested_style);
        SDL_Surface* rendered = TTF_RenderText_Blended(font_, text.c_str(), text.size(), color);
        std::shared_ptr<::cw::lib::Surface> result;
        if (rendered) {
            result = copy_rgba_surface(rendered);
            SDL_DestroySurface(rendered);
        } else {
            SDL_Log("Status text render failed: %s", SDL_GetError());
        }
        TTF_SetFontStyle(font_, old_style);
        TTF_SetFontSize(font_, old_size);
        return result;
    }

    [[nodiscard]] const std::string& font_path() const { return font_path_; }
    [[nodiscard]] auto measure_status_text(const ::cw::lib::String& text,
                                          ::cw::lib::Integer points, bool bold) {
        return ::cardwirth::app::measure_status_text(font_, text, points, bold);
    }
    [[nodiscard]] bool available() const { return font_ != nullptr; }

private:
    SDL_Renderer* renderer_{};
    TTF_Font* font_{};
    bool ttf_initialized_{};
    std::string font_path_;
    std::map<std::string, CachedText> cache_;
};

class AnimationLayer {
public:
    bool initialize(SDL_Renderer* renderer, const std::string& relative) {
        renderer_ = renderer;
        SDL_IOStream* stream = open_resource(relative);
        if (!stream) {
            return false;
        }
        decoder_ = IMG_CreateAnimationDecoder_IO(stream, true, "WEBP");
        if (!decoder_) {
            SDL_Log("Animation decoder failed (%s): %s", relative.c_str(), SDL_GetError());
            return false;
        }
        return decode_next(SDL_GetTicks());
    }

    ~AnimationLayer() {
        SDL_DestroyTexture(texture_);
        if (decoder_) {
            IMG_CloseAnimationDecoder(decoder_);
        }
    }

    AnimationLayer() = default;
    AnimationLayer(const AnimationLayer&) = delete;
    AnimationLayer& operator=(const AnimationLayer&) = delete;

    void update(Uint64 now) {
        if (!decoder_ || now < next_frame_at_) {
            return;
        }
        if (!decode_next(now)) {
            SDL_ClearError();
            if (IMG_ResetAnimationDecoder(decoder_)) {
                decode_next(now);
            }
        }
    }

    void draw(const SDL_FRect& destination) const {
        if (texture_) {
            SDL_RenderTexture(renderer_, texture_, nullptr, &destination);
        }
    }

    [[nodiscard]] bool available() const { return decoder_ != nullptr && texture_ != nullptr; }

private:
    bool decode_next(Uint64 now) {
        SDL_Surface* frame = nullptr;
        Uint64 duration = 0;
        if (!IMG_GetAnimationDecoderFrame(decoder_, &frame, &duration)) {
            return false;
        }
        SDL_Texture* next = SDL_CreateTextureFromSurface(renderer_, frame);
        SDL_DestroySurface(frame);
        if (!next) {
            return false;
        }
        SDL_SetTextureScaleMode(next, SDL_SCALEMODE_LINEAR);
        SDL_SetTextureBlendMode(next, SDL_BLENDMODE_BLEND);
        SDL_DestroyTexture(texture_);
        texture_ = next;
        next_frame_at_ = now + std::max<Uint64>(duration, 16);
        return true;
    }

    SDL_Renderer* renderer_{};
    IMG_AnimationDecoder* decoder_{};
    SDL_Texture* texture_{};
    Uint64 next_frame_at_{};
};

class AudioSystem {
public:
    bool initialize() {
        if (!MIX_Init()) {
            SDL_Log("MIX_Init failed: %s", SDL_GetError());
            return false;
        }
        initialized_ = true;
        mixer_ = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
        if (!mixer_) {
            SDL_Log("Audio device failed: %s", SDL_GetError());
            return false;
        }
        bgm_track_ = MIX_CreateTrack(mixer_);
        if (!bgm_track_) {
            return false;
        }

        const std::array<const char*, 4> files{
            "music/BGM1.mp3", "music/BGM2.mp3", "music/BGM3.mp3", "music/BGM4.mp3"};
        for (const char* file : files) {
            SDL_IOStream* stream = open_resource(file);
            bgm_.push_back(stream ? MIX_LoadAudio_IO(mixer_, stream, false, true) : nullptr);
        }

        if (SDL_IOStream* stream = open_resource("sound/システム・クリック.ogg")) {
            effect_ = MIX_LoadAudio_IO(mixer_, stream, true, true);
        }
        return true;
    }

    ~AudioSystem() {
        if (bgm_track_) {
            MIX_StopTrack(bgm_track_, 0);
            MIX_DestroyTrack(bgm_track_);
        }
        for (MIX_Audio* audio : bgm_) {
            if (audio) {
                MIX_DestroyAudio(audio);
            }
        }
        if (effect_) {
            MIX_DestroyAudio(effect_);
        }
        if (mixer_) {
            MIX_DestroyMixer(mixer_);
        }
        if (initialized_) {
            MIX_Quit();
        }
    }

    AudioSystem() = default;
    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;

    std::string play_next_bgm() {
        if (!bgm_track_ || bgm_.empty()) {
            return "音声デバイスを利用できません";
        }
        for (std::size_t attempts = 0; attempts < bgm_.size(); ++attempts) {
            const std::size_t index = next_bgm_;
            next_bgm_ = (next_bgm_ + 1) % bgm_.size();
            if (!bgm_[index]) {
                continue;
            }
            MIX_StopTrack(bgm_track_, 0);
            if (!MIX_SetTrackAudio(bgm_track_, bgm_[index])) {
                continue;
            }
            const SDL_PropertiesID options = SDL_CreateProperties();
            SDL_SetNumberProperty(options, MIX_PROP_PLAY_LOOPS_NUMBER, -1);
            const bool started = MIX_PlayTrack(bgm_track_, options);
            SDL_DestroyProperties(options);
            if (started) {
                return "BGM" + std::to_string(index + 1) + " を再生中";
            }
        }
        return "BGMを読み込めません";
    }

    std::string play_effect() {
        if (mixer_ && effect_ && MIX_PlayAudio(mixer_, effect_)) {
            return "クリック効果音を再生";
        }
        return "効果音を再生できません";
    }

    std::string stop_bgm() {
        if (bgm_track_) {
            MIX_StopTrack(bgm_track_, 0);
        }
        return "BGMを停止";
    }

    [[nodiscard]] bool available() const {
        return mixer_ && bgm_track_ && effect_ &&
               std::any_of(bgm_.begin(), bgm_.end(), [](MIX_Audio* audio) { return audio != nullptr; });
    }

private:
    bool initialized_{};
    MIX_Mixer* mixer_{};
    MIX_Track* bgm_track_{};
    std::vector<MIX_Audio*> bgm_;
    MIX_Audio* effect_{};
    std::size_t next_bgm_{};
};

struct StatusBarNativeState {
    SDL_Window* window{};
    SDL_Renderer* renderer{};
    TextRenderer* text{};
    AudioSystem* audio{};
    bool active{true};
    std::mutex mutex;
    std::map<::cw::lib::Integer, std::vector<std::shared_ptr<::cw::lib::Object>>> layers;
    std::vector<::cw::lib::Rect> dirty;
};

struct Button {
    SDL_FRect rect{};
    std::string label;
};

struct AppState {
    SDL_Window* window{};
    SDL_Renderer* renderer{};
    SDL_Texture* background{};
    SDL_Texture* book{};
    SDL_Texture* logo{};
    std::unique_ptr<TextRenderer> text;
    std::unique_ptr<AnimationLayer> animation;
    std::unique_ptr<AudioSystem> audio;
    std::shared_ptr<StatusBarNativeState> statusbar_native;
    std::shared_ptr<::cw::sprite::statusbar::StatusBarHostServices> statusbar_backend;
    std::array<Button, kButtonCount> buttons{};
    std::string status{"ボタンを押して機能を確認してください"};
    bool show_animation{true};
    bool show_layers{true};
    bool smoke_test{};
    bool smoke_failed{};
    Uint64 started_at{};

    ~AppState() {
        ::cw::sprite::statusbar::set_host_services({});
        statusbar_backend.reset();
        if (statusbar_native) {
            std::scoped_lock lock(statusbar_native->mutex);
            statusbar_native->active = false;
            statusbar_native->window = nullptr;
            statusbar_native->renderer = nullptr;
            statusbar_native->text = nullptr;
            statusbar_native->audio = nullptr;
        }
        statusbar_native.reset();
        audio.reset();
        animation.reset();
        text.reset();
        SDL_DestroyTexture(logo);
        SDL_DestroyTexture(book);
        SDL_DestroyTexture(background);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
    }
};

bool has_argument(int argc, char* argv[], const char* expected) {
    for (int index = 1; index < argc; ++index) {
        if (SDL_strcmp(argv[index], expected) == 0) {
            return true;
        }
    }
    return false;
}

void handle_click(AppState& state, float window_x, float window_y) {
    float x = window_x;
    float y = window_y;
    SDL_RenderCoordinatesFromWindow(state.renderer, window_x, window_y, &x, &y);
    for (int index = 0; index < kButtonCount; ++index) {
        const SDL_FRect& rect = state.buttons[index].rect;
        if (x < rect.x || x > rect.x + rect.w || y < rect.y || y > rect.y + rect.h) {
            continue;
        }
        switch (index) {
            case 0:
                state.status = state.audio->play_next_bgm();
                break;
            case 1:
                state.status = state.audio->play_effect();
                break;
            case 2:
                state.show_animation = !state.show_animation;
                state.status = state.show_animation ? "動画レイヤーを表示" : "動画レイヤーを非表示";
                break;
            case 3:
                state.show_layers = !state.show_layers;
                state.status = state.show_layers ? "画像レイヤーを表示" : "画像レイヤーを非表示";
                break;
            case 4:
                state.status = state.audio->stop_bgm();
                break;
            default:
                break;
        }
        break;
    }
}

std::shared_ptr<::cw::lib::Surface> resize_status_surface(
    const std::shared_ptr<::cw::lib::Surface>& source, ::cw::lib::Integer width,
    ::cw::lib::Integer height) {
    if (!source || width <= 0 || height <= 0 || source->size.width <= 0 || source->size.height <= 0) {
        return {};
    }
    if (source->size.width == width && source->size.height == height) {
        return source;
    }
    const auto target_width = static_cast<std::size_t>(width);
    const auto target_height = static_cast<std::size_t>(height);
    if (target_width > std::numeric_limits<std::size_t>::max() / target_height / 4U) {
        return {};
    }
    auto result = std::make_shared<::cw::lib::Surface>();
    result->size = {width, height};
    result->rgba.resize(target_width * target_height * 4U);
    for (::cw::lib::Integer y = 0; y < height; ++y) {
        const auto source_y = std::min<::cw::lib::Integer>(
            source->size.height - 1,
            static_cast<::cw::lib::Integer>((static_cast<long double>(y) * source->size.height) / height));
        for (::cw::lib::Integer x = 0; x < width; ++x) {
            const auto source_x = std::min<::cw::lib::Integer>(
                source->size.width - 1,
                static_cast<::cw::lib::Integer>((static_cast<long double>(x) * source->size.width) / width));
            const auto source_index = static_cast<std::size_t>(source_y * source->size.width + source_x) * 4U;
            const auto target_index = static_cast<std::size_t>(y * width + x) * 4U;
            if (source_index + 4U > source->rgba.size() || target_index + 4U > result->rgba.size()) {
                continue;
            }
            std::copy_n(source->rgba.begin() + static_cast<std::ptrdiff_t>(source_index), 4,
                        result->rgba.begin() + static_cast<std::ptrdiff_t>(target_index));
        }
    }
    return result;
}

void draw_status_surface(const std::shared_ptr<StatusBarNativeState>& native,
                         const std::shared_ptr<::cw::lib::Surface>& surface,
                         const ::cw::lib::Rect& rect) {
    if (!native || !surface || rect.width <= 0 || rect.height <= 0 ||
        surface->size.width <= 0 || surface->size.height <= 0 ||
        surface->size.width > std::numeric_limits<int>::max() / 4 ||
        surface->size.height > std::numeric_limits<int>::max() / 4) {
        return;
    }
    const auto expected_bytes = static_cast<std::size_t>(surface->size.width) *
                                static_cast<std::size_t>(surface->size.height) * 4U;
    if (surface->rgba.size() < expected_bytes) {
        return;
    }
    std::scoped_lock lock(native->mutex);
    if (!native->active || !native->renderer) {
        return;
    }
    SDL_Surface* source = SDL_CreateSurfaceFrom(
        static_cast<int>(surface->size.width), static_cast<int>(surface->size.height),
        SDL_PIXELFORMAT_RGBA32, const_cast<std::uint8_t*>(surface->rgba.data()),
        static_cast<int>(surface->size.width * 4));
    if (!source) {
        SDL_Log("Status surface creation failed: %s", SDL_GetError());
        return;
    }
    SDL_Texture* texture = SDL_CreateTextureFromSurface(native->renderer, source);
    SDL_DestroySurface(source);
    if (!texture) {
        SDL_Log("Status texture creation failed: %s", SDL_GetError());
        return;
    }
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    const SDL_FRect destination{static_cast<float>(rect.x), static_cast<float>(rect.y),
                                static_cast<float>(rect.width), static_cast<float>(rect.height)};
    SDL_RenderTexture(native->renderer, texture, nullptr, &destination);
    SDL_DestroyTexture(texture);
}

void install_statusbar_backend(AppState& state) {
    auto native = std::make_shared<StatusBarNativeState>();
    native->window = state.window;
    native->renderer = state.renderer;
    native->text = state.text.get();
    native->audio = state.audio.get();

    auto backend = std::make_shared<::cw::sprite::statusbar::StatusBarHostServices>();
    backend->load_resource = [](const ::cw::lib::String& resource,
                                      ::cw::lib::Integer requested_size,
                                      ::cw::lib::Color color) {
        (void)color;
        const auto loaded = load_status_surface(resource);
        if (!loaded) {
            return std::shared_ptr<::cw::lib::Surface>{};
        }
        const auto size = std::clamp<::cw::lib::Integer>(requested_size, 1, 1024);
        return resize_status_surface(loaded, size, size);
    };
    backend->render_text = [native](const ::cw::lib::String& text,
                                    const ::cw::lib::String& role,
                                    ::cw::lib::Integer point_size,
                                    ::cw::lib::Color color, bool bold) {
        (void)role;
        std::scoped_lock lock(native->mutex);
        if (!native->active || !native->text) {
            return std::shared_ptr<::cw::lib::Surface>{};
        }
        const auto size = std::clamp<::cw::lib::Integer>(point_size, 1, 256);
        const SDL_Color native_color{color.r, color.g, color.b, color.a};
        return native->text->render_surface(text, static_cast<float>(size), native_color, bold);
    };
    backend->measure_text = [native](const ::cw::lib::String& text,
                                     const ::cw::lib::String&, ::cw::lib::Integer points, bool bold)
        -> std::optional<::cw::sprite::statusbar::StatusBarHostServices::TextMetrics> {
        std::scoped_lock lock(native->mutex);
        if (!native->active || !native->text) return std::nullopt;
        return native->text->measure_status_text(text, points, bold);
    };
    backend->add_layer = [native](::cw::lib::Integer layer,
                                  const std::vector<std::shared_ptr<::cw::lib::Object>>& objects) {
        std::scoped_lock lock(native->mutex);
        if (!native->active) {
            return;
        }
        auto& bucket = native->layers[layer];
        for (const auto& object : objects) {
            if (!object) {
                continue;
            }
            const auto found = std::find_if(bucket.begin(), bucket.end(),
                                            [&object](const auto& existing) {
                                                return existing.get() == object.get();
                                            });
            if (found == bucket.end()) {
                bucket.push_back(object);
            }
        }
    };
    backend->remove_layer = [native](::cw::lib::Integer layer) {
        std::scoped_lock lock(native->mutex);
        if (!native->active) {
            return;
        }
        native->layers.erase(layer);
    };
    backend->invalidate = [native](const ::cw::lib::Rect& rect) {
        std::scoped_lock lock(native->mutex);
        if (!native->active) {
            return;
        }
        if (native->dirty.size() >= 1024U) {
            native->dirty.erase(native->dirty.begin(), native->dirty.begin() + 512);
        }
        native->dirty.push_back(rect);
    };
    backend->draw_surface = [native](const std::shared_ptr<::cw::lib::Surface>& surface,
                                     const ::cw::lib::Rect& rect,
                                     ::cw::lib::Integer layer) {
        (void)layer;
        draw_status_surface(native, surface, rect);
    };
    backend->draw_edge_pixel = [native](::cw::lib::Integer x, ::cw::lib::Integer y, bool inside) {
        if (!native) {
            return;
        }
        std::scoped_lock lock(native->mutex);
        if (!native->active || !native->renderer) {
            return;
        }
        SDL_SetRenderDrawColor(native->renderer, inside ? 192 : 0, inside ? 192 : 0,
                               inside ? 192 : 0, 255);
        SDL_RenderPoint(native->renderer, static_cast<float>(x), static_cast<float>(y));
    };
    backend->play_sound = [native](const ::cw::lib::String& name) {
        (void)name;
        if (!native) {
            return;
        }
        std::scoped_lock lock(native->mutex);
        if (native->active && native->audio) {
            (void)native->audio->play_effect();
        }
    };
    backend->inject_mouse_button_up = [native](::cw::lib::Integer button) {
        if (!native) {
            return;
        }
        std::scoped_lock lock(native->mutex);
        if (!native->active || !native->window) {
            return;
        }
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_BUTTON_UP;
        event.button.button = static_cast<Uint8>(std::clamp<::cw::lib::Integer>(button, 0, 255));
        event.button.windowID = SDL_GetWindowID(native->window);
        if (!SDL_PushEvent(&event)) {
            SDL_Log("Status mouse-up event enqueue failed: %s", SDL_GetError());
        }
    };

    state.statusbar_native = std::move(native);
    state.statusbar_backend = std::move(backend);
    ::cw::sprite::statusbar::set_host_services(state.statusbar_backend);
}

void render(AppState& state) {
    state.animation->update(SDL_GetTicks());
    SDL_SetRenderDrawColor(state.renderer, 18, 20, 26, 255);
    SDL_RenderClear(state.renderer);

    const SDL_FRect scene{0.0F, 0.0F, static_cast<float>(kLogicalWidth), static_cast<float>(kSceneHeight)};
    if (state.background) {
        SDL_RenderTexture(state.renderer, state.background, nullptr, &scene);
    }
    if (state.show_animation && state.animation->available()) {
        state.animation->draw(scene);
    }
    if (state.show_layers) {
        if (state.book) {
            const SDL_FRect destination{86.0F, 70.0F, 460.0F, 280.0F};
            SDL_RenderTexture(state.renderer, state.book, nullptr, &destination);
        }
        if (state.logo) {
            const SDL_FRect destination{18.0F, 260.0F, 181.0F, 106.0F};
            SDL_RenderTexture(state.renderer, state.logo, nullptr, &destination);
        }
    }

    SDL_SetRenderDrawBlendMode(state.renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(state.renderer, 0, 0, 0, 170);
    const SDL_FRect title_back{7.0F, 7.0F, 365.0F, 49.0F};
    SDL_RenderFillRect(state.renderer, &title_back);
    state.text->draw("SDL3 CardWirth 最小サンプル", 15.0F, 10.0F, SDL_Color{255, 245, 210, 255});
    state.text->draw(state.status, 15.0F, 32.0F, SDL_Color{235, 235, 235, 255});

    SDL_SetRenderDrawColor(state.renderer, 27, 30, 38, 255);
    const SDL_FRect bar{0.0F, static_cast<float>(kSceneHeight), static_cast<float>(kLogicalWidth), 33.0F};
    SDL_RenderFillRect(state.renderer, &bar);
    for (const Button& button : state.buttons) {
        SDL_SetRenderDrawColor(state.renderer, 74, 82, 99, 255);
        SDL_RenderFillRect(state.renderer, &button.rect);
        SDL_SetRenderDrawColor(state.renderer, 180, 190, 210, 255);
        SDL_RenderRect(state.renderer, &button.rect);
        state.text->draw(button.label, button.rect.x + button.rect.w * 0.5F,
                         button.rect.y + button.rect.h * 0.5F,
                         SDL_Color{255, 255, 255, 255}, true);
    }
    SDL_RenderPresent(state.renderer);
}

}  // namespace

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
    ::cardwirth::app::configure_runtime_library_search();
    // macOSなどWindows以外のデスクトップでは、SDLが共通
    // のエントリーポイントを提供する。 EngineがArgParser
    // で起動設定を読む前に、SDLのargvを明示的に渡す。 CRT
    // のグローバル変数に依存すると、macOSでは引数が空になる。
    ::cw::argparser::set_app_launch_options(argc, argv);
    SDL_SetAppMetadata("CardWirth SDL Sample", "0.1.0", "jp.cardwirth.sdl-sample");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    auto state = std::make_unique<AppState>();
    state->smoke_test = has_argument(argc, argv, "--smoke-test");
    const SDL_WindowFlags flags = static_cast<SDL_WindowFlags>(
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
        (state->smoke_test ? SDL_WINDOW_HIDDEN : 0));
    if (!SDL_CreateWindowAndRenderer("CardWirth SDL Sample", 948, 680, flags,
                                     &state->window, &state->renderer)) {
        SDL_Log("Window creation failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_SetRenderLogicalPresentation(state->renderer, kLogicalWidth, kLogicalHeight,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_SetRenderVSync(state->renderer, 1);

    state->background = load_texture(state->renderer, "image/MapOfWirth.png");
    state->book = load_texture(state->renderer, "image/Book.png");
    state->logo = load_texture(state->renderer, "image/Logo.png");

    state->text = std::make_unique<TextRenderer>();
    state->text->initialize(state->renderer);
    state->animation = std::make_unique<AnimationLayer>();
    if (!state->animation->initialize(state->renderer, "image/MapOfWirth_animated.webp")) {
        state->status = "動画素材を読み込めません（変換手順はREADME参照）";
    }
    state->audio = std::make_unique<AudioSystem>();
    state->audio->initialize();
    state->smoke_failed = !state->background || !state->book || !state->logo ||
                          !state->animation->available() || !state->text->available() ||
                          !state->audio->available();

    const std::array<const char*, kButtonCount> labels{
        "BGM切替", "効果音", "動画 ON/OFF", "画像レイヤー", "BGM停止"};
    for (int index = 0; index < kButtonCount; ++index) {
        state->buttons[index] = Button{
            SDL_FRect{4.0F + static_cast<float>(index) * 126.0F, 423.0F, 122.0F, 27.0F},
            labels[index],
        };
    }
    install_statusbar_backend(*state);
    state->started_at = SDL_GetTicks();
    *appstate = state.release();
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    auto& state = *static_cast<AppState*>(appstate);
    switch (event->type) {
        case SDL_EVENT_QUIT:
            return SDL_APP_SUCCESS;
        case SDL_EVENT_KEY_DOWN:
            if (event->key.key == SDLK_ESCAPE) {
                return SDL_APP_SUCCESS;
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (event->button.button == SDL_BUTTON_LEFT) {
                handle_click(state, event->button.x, event->button.y);
            }
            break;
        case SDL_EVENT_FINGER_DOWN: {
            int width = 0;
            int height = 0;
            SDL_GetWindowSize(state.window, &width, &height);
            handle_click(state, event->tfinger.x * static_cast<float>(width),
                         event->tfinger.y * static_cast<float>(height));
            break;
        }
        default:
            break;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    auto& state = *static_cast<AppState*>(appstate);
    render(state);
    if (state.smoke_test && SDL_GetTicks() - state.started_at > 800) {
        return state.smoke_failed ? SDL_APP_FAILURE : SDL_APP_SUCCESS;
    }
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result) {
    (void)result;
    ::cw::sprite::statusbar::set_host_services({});
    delete static_cast<AppState*>(appstate);
    SDL_Quit();
}
