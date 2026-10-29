#include "sdl_font_provider.hpp"
#include "editor_diagnostics.hpp"
#include "app/platform_font.hpp"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include FT_SYNTHESIS_H

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#if defined(_WIN32)
#include <dwrite.h>
#endif

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <exception>
#include <limits>
#include <list>
#include <map>
#include <optional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <cstdint>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cwxeditor::lib {
namespace {

std::mutex& runtime_mutex() { static std::mutex mutex; return mutex; }

struct TtfRuntime {
    bool initialized{};
    TtfRuntime() {
        std::scoped_lock lock(runtime_mutex());
        // 別のエディタ・エンジンがTTFを初期化済みでも、各提供側が参
        // 照を一つ持つ。 他の所有者のフォント実行環境を終了させない。
        initialized = TTF_Init();
    }
    ~TtfRuntime() {
        std::scoped_lock lock(runtime_mutex());
        if (initialized) TTF_Quit();
    }
};

struct FontDeleter {
    void operator()(TTF_Font* value) const noexcept { TTF_CloseFont(value); }
};
using Font = std::unique_ptr<TTF_Font, FontDeleter>;
constexpr const char* font_source_property="cwxeditor.font.source.path";
constexpr const char* font_source_face_property="cwxeditor.font.source.face";

struct ContourFile {
    void* bytes{};std::size_t length{};
    ~ContourFile(){SDL_free(bytes);}
};
struct ContourFont {
    FT_Library library{};FT_Face face{};std::shared_ptr<ContourFile> file;
    ~ContourFont(){if(face)FT_Done_Face(face);if(library)FT_Done_FreeType(library);}
    static std::shared_ptr<ContourFont> open(TTF_Font* selected,
        std::unordered_map<std::string,std::weak_ptr<ContourFile>>* files=nullptr){
        const auto* path=SDL_GetStringProperty(TTF_GetFontProperties(selected),font_source_property,nullptr);
        if(!path)return {};
        const double pixels=TTF_GetFontSize(selected);
        const auto selected_properties=TTF_GetFontProperties(selected);
        const auto raw_face_index=SDL_GetNumberProperty(selected_properties,font_source_face_property,0);
        if(!std::isfinite(pixels)||pixels<=0||pixels>double(std::numeric_limits<FT_F26Dot6>::max())/64||
           raw_face_index<0||raw_face_index>static_cast<Sint64>(std::numeric_limits<FT_Long>::max()))return {};
        const auto face_index=static_cast<FT_Long>(raw_face_index);
        auto font=std::make_shared<ContourFont>();
        if(files)font->file=(*files)[path].lock();
        if(!font->file){
            font->file=std::make_shared<ContourFile>();
            font->file->bytes=SDL_LoadFile(path,&font->file->length);
            if(!font->file->bytes)return {};
            if(files)(*files)[path]=font->file;
        }
        if(font->file->length>std::size_t(std::numeric_limits<FT_Long>::max())||FT_Init_FreeType(&font->library)||
           FT_New_Memory_Face(font->library,static_cast<const FT_Byte*>(font->file->bytes),static_cast<FT_Long>(font->file->length),face_index,&font->face)||
           FT_Set_Char_Size(font->face,0,static_cast<FT_F26Dot6>(std::round(pixels*64)),72,72)||
           FT_Select_Charmap(font->face,FT_ENCODING_UNICODE))return {};
        return font;
    }
};

#if defined(_WIN32)
std::optional<std::wstring> windows_wide(std::string_view utf8) {
    if (utf8.empty() || utf8.find('\0') != std::string_view::npos ||
        utf8.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) return {};
    const int input_length = static_cast<int>(utf8.size());
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                           utf8.data(), input_length, nullptr, 0);
    if (length <= 0) return {};
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), input_length,
                            result.data(), length) != length) return {};
    return result;
}
#endif

#if defined(_WIN32)
template<class T>
class DirectWriteOwner final {
public:
    DirectWriteOwner() = default;
    DirectWriteOwner(const DirectWriteOwner&) = delete;
    DirectWriteOwner& operator=(const DirectWriteOwner&) = delete;
    ~DirectWriteOwner() { reset(); }
    T** put() noexcept { reset(); return &value_; }
    T* get() const noexcept { return value_; }
    T* operator->() const noexcept { return value_; }
    explicit operator bool() const noexcept { return value_ != nullptr; }
private:
    void reset() noexcept { if (value_) { value_->Release(); value_ = nullptr; } }
    T* value_{};
};

struct DirectWriteApi final {
    IDWriteFactory* factory{};
    DirectWriteApi() {
        // ALL と単独版で、同じリンク済みのDirectWriteのフォントファミリー解決処理を使う。
        (void)DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                  reinterpret_cast<IUnknown**>(&factory));
    }
    ~DirectWriteApi() { if (factory) factory->Release(); }
};

struct DirectWriteSelection final {
    std::string path;
    std::uint32_t face_index{};
    bool synthesize_bold{};
    bool synthesize_italic{};
};

std::optional<std::wstring> directwrite_wide(std::string_view utf8) {
    return windows_wide(utf8);
}

std::optional<std::string> directwrite_utf8(std::wstring_view wide) {
    if (wide.empty() || wide.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) return {};
    const int input_length = static_cast<int>(wide.size());
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), input_length,
                                           nullptr, 0, nullptr, nullptr);
    if (length <= 0) return {};
    std::string result(static_cast<std::size_t>(length), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), input_length,
                            result.data(), length, nullptr, nullptr) != length) return {};
    return result;
}

std::optional<DirectWriteSelection> resolve_windows_editor_font_uncached(const FontSpec& specification) {
    const auto family = directwrite_wide(specification.family);
    if (!family || family->size() >= LF_FACESIZE) return {};
    DirectWriteApi& api = []() -> DirectWriteApi& { static DirectWriteApi value; return value; }();
    if (!api.factory) return {};

    LOGFONTW logfont{};
    // IDWriteFont の選択はサイズを含まない。安定した仮の高さを使い
    // 、 最初の小さい要求が、ファミリー全体の選択失敗としてキャッシュされな
    // いようにする。 指定サイズは、下でSDLフォントを開くときに適用する。
    logfont.lfHeight = 1;
    logfont.lfWeight = specification.weight >= 600U ? FW_BOLD : FW_NORMAL;
    logfont.lfItalic = specification.italic ? TRUE : FALSE;
    logfont.lfUnderline = specification.underline && !specification.decorations_in_policy ? TRUE : FALSE;
    logfont.lfStrikeOut = specification.strikeout && !specification.decorations_in_policy ? TRUE : FALSE;
    logfont.lfCharSet = DEFAULT_CHARSET;
    logfont.lfOutPrecision = OUT_DEFAULT_PRECIS;
    logfont.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    logfont.lfQuality = specification.antialias ? ANTIALIASED_QUALITY : DEFAULT_QUALITY;
    logfont.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    std::copy(family->begin(), family->end(), logfont.lfFaceName);

    DirectWriteOwner<IDWriteGdiInterop> gdi;
    if (FAILED(api.factory->GetGdiInterop(gdi.put()))) return {};
    DirectWriteOwner<IDWriteFont> font;
    if (FAILED(gdi->CreateFontFromLOGFONT(&logfont, font.put()))) return {};
    DirectWriteOwner<IDWriteFontFace> face;
    if (FAILED(font->CreateFontFace(face.put()))) return {};

    UINT32 file_count = 0;
    if (FAILED(face->GetFiles(&file_count, nullptr)) || file_count != 1) return {};
    DirectWriteOwner<IDWriteFontFile> file;
    if (FAILED(face->GetFiles(&file_count, file.put())) || !file) return {};
    DirectWriteOwner<IDWriteFontFileLoader> loader;
    if (FAILED(file->GetLoader(loader.put()))) return {};
    DirectWriteOwner<IDWriteLocalFontFileLoader> local_loader;
    if (FAILED(loader->QueryInterface(__uuidof(IDWriteLocalFontFileLoader),
                                     reinterpret_cast<void**>(local_loader.put())))) return {};

    const void* reference_key = nullptr;
    UINT32 reference_key_size = 0;
    if (FAILED(file->GetReferenceKey(&reference_key, &reference_key_size))) return {};
    UINT32 path_length = 0;
    if (FAILED(local_loader->GetFilePathLengthFromKey(reference_key, reference_key_size, &path_length)) ||
        path_length == std::numeric_limits<UINT32>::max()) return {};
    std::wstring wide_path(static_cast<std::size_t>(path_length) + 1U, L'\0');
    if (FAILED(local_loader->GetFilePathFromKey(reference_key, reference_key_size,
                                                wide_path.data(), static_cast<UINT32>(wide_path.size())))) return {};
    wide_path.resize(path_length);
    const auto path = directwrite_utf8(wide_path);
    if (!path || path->empty()) return {};

    const UINT32 face_index = face->GetIndex();
    if (static_cast<std::uint64_t>(face_index) >
        static_cast<std::uint64_t>(std::numeric_limits<FT_Long>::max())) return {};
    const DWRITE_FONT_SIMULATIONS simulations = font->GetSimulations();
    const bool simulated_bold = (simulations & DWRITE_FONT_SIMULATIONS_BOLD) != 0;
    const bool simulated_italic = (simulations & DWRITE_FONT_SIMULATIONS_OBLIQUE) != 0;
    return DirectWriteSelection{*path, face_index,
        specification.weight >= 600U && (simulated_bold || font->GetWeight() < DWRITE_FONT_WEIGHT_DEMI_BOLD),
        specification.italic && (simulated_italic || font->GetStyle() == DWRITE_FONT_STYLE_NORMAL)};
}

std::optional<DirectWriteSelection> resolve_windows_editor_font(const FontSpec& specification) {
    using Key = std::tuple<std::string, bool, bool>;
    static std::mutex mutex;
    static std::map<Key, std::optional<DirectWriteSelection>> cache;
    // CreateFontFromLOGFONT が返す IDWriteFont はLOGFONTのサ
    // イズを保持しない。 解決はファミリー・太さ・書体に依存するため、サイズは意図的にキーに含めない。
    const Key key{specification.family, specification.weight >= 600U, specification.italic};
    std::scoped_lock lock(mutex);
    if (const auto found = cache.find(key); found != cache.end()) return found->second;
    auto selected = resolve_windows_editor_font_uncached(specification);
    cache.emplace(key, selected); // 文字サイズごとやディレクトリ走査ではなく、ファミリー・書体ごとに一度だけOSへ問い合わせる。
    return selected;
}

Font open_windows_editor_font_face(const std::string& path, float points, std::uint32_t face_index) {
    SDL_PropertiesID properties = SDL_CreateProperties();
    if (!properties) return {};
    const bool configured = SDL_SetStringProperty(properties, TTF_PROP_FONT_CREATE_FILENAME_STRING, path.c_str()) &&
        SDL_SetFloatProperty(properties, TTF_PROP_FONT_CREATE_SIZE_FLOAT, points) &&
        SDL_SetNumberProperty(properties, TTF_PROP_FONT_CREATE_FACE_NUMBER, static_cast<Sint64>(face_index));
    Font font{configured ? TTF_OpenFontWithProperties(properties) : nullptr};
    SDL_DestroyProperties(properties);
    return font;
}
#endif

struct FontSizeTarget final {
    int height{};
    int ascent{};
    int descent{};
    bool compare_extents{};
};

#if defined(_WIN32)
std::optional<FontSizeTarget> windows_font_size_target(const FontSpec& specification) {
    if (!std::isfinite(specification.points) || specification.points < 0.0F ||
        specification.points > 4096.0F) return {};
    std::optional<std::wstring> family = specification.family.empty()
        ? std::optional<std::wstring>{std::wstring{}}
        : windows_wide(specification.family);
    if (!family || family->size() >= LF_FACESIZE) return {};

    LOGFONTW logfont{};
    const auto size = static_cast<LONG>(std::lround(specification.points));
    switch (specification.size_mode) {
    case FontSizeMode::CellPixels: logfont.lfHeight = std::max<LONG>(1, size); break;
    case FontSizeMode::CharacterPixels: logfont.lfHeight = -std::max<LONG>(1, size); break;
    case FontSizeMode::DefaultHeight: logfont.lfHeight = 0; break;
    case FontSizeMode::TypographicPoints: return {};
    }
    logfont.lfWeight = static_cast<LONG>(std::min<std::uint32_t>(specification.weight, 1000U));
    logfont.lfItalic = specification.italic ? TRUE : FALSE;
    logfont.lfCharSet = DEFAULT_CHARSET;
    logfont.lfOutPrecision = OUT_DEFAULT_PRECIS;
    logfont.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    logfont.lfQuality = specification.antialias ? ANTIALIASED_QUALITY : DEFAULT_QUALITY;
    logfont.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    std::copy(family->begin(), family->end(), logfont.lfFaceName);
    HFONT requested = CreateFontIndirectW(&logfont);
    if (!requested) return {};
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) { DeleteObject(requested); return {}; }
    const HGDIOBJ previous = SelectObject(dc, requested);
    if (!previous || previous == HGDI_ERROR) {
        DeleteObject(requested);
        DeleteDC(dc);
        return {};
    }

    TEXTMETRICW metrics{};
    const bool have_metrics = GetTextMetricsW(dc, &metrics) != FALSE && metrics.tmHeight > 0;
    SelectObject(dc, previous);
    DeleteObject(requested);
    DeleteDC(dc);
    if (!have_metrics) return {};
    const int cell_height = static_cast<int>(metrics.tmHeight);
    const int character_height = std::max(1, cell_height - static_cast<int>(metrics.tmInternalLeading));
    const int target_height = specification.size_mode == FontSizeMode::CharacterPixels
        ? character_height : cell_height;
    return FontSizeTarget{target_height, static_cast<int>(metrics.tmAscent),
                          static_cast<int>(metrics.tmDescent),
                          specification.size_mode != FontSizeMode::CharacterPixels};
}
#endif

FontSizeTarget requested_font_size_target(const FontSpec& specification,
                                          TTF_Font* default_font) {
#if defined(_WIN32)
    if (const auto system = windows_font_size_target(specification)) return *system;
#endif
    if (specification.size_mode == FontSizeMode::DefaultHeight && default_font) {
        return {TTF_GetFontHeight(default_font), TTF_GetFontAscent(default_font),
                std::abs(TTF_GetFontDescent(default_font)), true};
    }
    const int requested = static_cast<int>(std::clamp(
        std::lround(std::fabs(static_cast<double>(specification.points))), 1L, 16384L));
    // D版と同様、指定画素数を現在の画面DPIで FontData のポイン
    // ト数に変換する。 そのポイント数の描画は、この論理画素の寸法に戻る。
    return {requested, 0, 0, false};
}

int selected_font_height(TTF_Font* font, FontSizeMode mode) {
    if (!font) return 0;
    if (mode == FontSizeMode::CharacterPixels) {
        // D版の CreateFontW の負の高さはEM高さを表す。SDL_ttf
        // はポイント数で候補を開き、 フォントの縦DPIで換算すると公称のEM
        // の画素数になる。 FreeTypeはその画素数を最も近い整数に丸める。
        int horizontal_dpi{}, vertical_dpi{};
        const float points = TTF_GetFontSize(font);
        if (!std::isfinite(points) || points <= 0.0F) return 0;
        if (!TTF_GetFontDPI(font, &horizontal_dpi, &vertical_dpi)) return 0;
        (void)horizontal_dpi;
        if (vertical_dpi <= 0) return 0;
        const double em_pixels = static_cast<double>(points) * vertical_dpi / 72.0;
        if (!std::isfinite(em_pixels) ||
            em_pixels > static_cast<double>(std::numeric_limits<int>::max())) return 0;
        return std::max(1, static_cast<int>(std::lround(em_pixels)));
    }
    return TTF_GetFontHeight(font);
}

template<class OpenAt>
std::optional<float> fit_selected_face_size(const FontSpec& specification,
                                            TTF_Font* default_font,
                                            const float seed_points,
                                            OpenAt&& open_at) {
    const auto target = requested_font_size_target(specification, default_font);
    if (target.height <= 0) return seed_points;
    struct Candidate final { float points{}; int height{}, ascent{}, descent{}; };
    std::optional<Candidate> best;
    const auto score = [&](const Candidate& value) {
        const int extent_error = target.compare_extents
            ? std::abs(value.ascent - target.ascent) + std::abs(value.descent - target.descent)
            : 0;
        return std::tuple{std::abs(value.height - target.height), extent_error, value.points};
    };
    const auto consider = [&](const float points, std::optional<Candidate>& current) {
        Font font = open_at(points);
        if (!font) return;
        Candidate candidate{points, selected_font_height(font.get(), specification.size_mode),
                            TTF_GetFontAscent(font.get()),
                            std::abs(TTF_GetFontDescent(font.get()))};
        if (candidate.height <= 0) return;
        if (!current || score(candidate) < score(*current)) current = candidate;
    };
    consider(seed_points, best);
    if (best && best->height == target.height &&
        (!target.compare_extents || (best->ascent == target.ascent && best->descent == target.descent)))
        return best->points;

    float low = 1.0F;
    float high = std::max(2.0F, static_cast<float>(target.height) * 4.0F);
    for (int step = 0; step < 28; ++step) {
        const float middle = low + (high - low) * 0.5F;
        Font font = open_at(middle);
        if (!font) break;
        const int height = selected_font_height(font.get(), specification.size_mode);
        if (height <= 0) break;
        Candidate candidate{middle, height, TTF_GetFontAscent(font.get()),
                            std::abs(TTF_GetFontDescent(font.get()))};
        if (!best || score(candidate) < score(*best)) best = candidate;
        if (height < target.height) low = middle;
        else high = middle;
    }
    consider(low, best);
    consider(high, best);
    // 最初の選択書体は seed_points で開いてある。 一時的なサイ
    // ズ調整が失敗した場合は、そのハンドルを明示的な最終の代替として残す。
    return best ? std::optional<float>{best->points} : std::nullopt;
}

std::string utf8_path(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

bool has_japanese_ui_glyphs(TTF_Font* font) {
    return font && TTF_FontHasGlyph(font, 0x65E5U) &&
           TTF_FontHasGlyph(font, 0x672CU) && TTF_FontHasGlyph(font, 0x8A9EU);
}

FontSpec effective_font(FontSpec value) {
    // D版の createFontFromPixels と同様、指定された正の画素
    // サイズをそのまま渡す。 UI用の最小値4を使うと、小さいTextCellを黙
    // って拡大し、元サイズと二倍のアンチエイリアス用サイズが同じになってしまう。
    if (value.size_mode == FontSizeMode::DefaultHeight) {
        value.points = 0.0F;
    } else {
        const float requested = std::isfinite(value.points) ? value.points : 10.0F;
        value.points = value.size_mode == FontSizeMode::CharacterPixels
            ? std::max(1.0F, std::fabs(requested))
            : std::max(1.0F, requested);
    }
    return value;
}

bool same_font(const FontSpec& a, const FontSpec& b) {
    return a.family == b.family && a.points == b.points && a.weight == b.weight &&
           a.italic == b.italic && a.underline == b.underline && a.strikeout == b.strikeout &&
           a.decorations_in_policy == b.decorations_in_policy && a.size_mode == b.size_mode;
}

std::string resolve_default_font() {
    std::vector<std::filesystem::path> candidates;
    // Web/Linuxでは常に同梱フォントを使い、両方で同じ文字をラスタライズする。
    if (const auto bundled = bundled_ui_font(); !bundled.empty())
        candidates.push_back(bundled);
    if (const char* base = SDL_GetBasePath()) {
        const auto root = std::filesystem::u8path(base);
        candidates.emplace_back(root / "resource" / "NotoSansJP-Regular.ttf");
        candidates.emplace_back(root / "resource" / "NotoSansCJKjp-Regular.otf");
        candidates.emplace_back(root / "resource" / "Font" / "NotoSansCJKjp-Regular.otf");
        candidates.emplace_back(root / "lib" / "NotoSansCJKjp-Regular.otf");
        candidates.emplace_back(root / "Data" / "Font" / "NotoSansCJKjp-Regular.otf");
        candidates.emplace_back(root / "Data" / "Font" / "gothic.ttf");
    }
    if (const auto path = find_platform_font(); !path.empty())
        candidates.emplace_back(std::filesystem::u8path(path));
#if defined(__ANDROID__)
    candidates.emplace_back("/system/fonts/NotoSansCJK-Regular.ttc");
    candidates.emplace_back("/system/fonts/NotoSansJP-Regular.otf");
#elif defined(__APPLE__)
    candidates.emplace_back(std::filesystem::u8path("/System/Library/Fonts/ヒラギノ角ゴシック W3.ttc"));
    candidates.emplace_back("/System/Library/Fonts/Helvetica.ttc");
#endif
    for (const auto& candidate : candidates) {
        std::error_code ec;
        if (!std::filesystem::exists(candidate, ec)) continue;
        const auto path = utf8_path(candidate);
        Font font{TTF_OpenFont(path.c_str(), 12.0F)};
        if (has_japanese_ui_glyphs(font.get())) return path;
    }
    return {};
}

Size measure(TTF_Font* font, std::string_view text, int wrap) {
    if (!font || text.empty()) return {};
    int width{}, height{};
    const bool ok = wrap > 0
        ? TTF_GetStringSizeWrapped(font, text.data(), text.size(), wrap, &width, &height)
        : TTF_GetStringSize(font, text.data(), text.size(), &width, &height);
    return ok ? Size{static_cast<float>(width), static_cast<float>(height)} : Size{};
}

} // namespace

struct SdlFontProvider::Impl {
    // 実行環境を解放する前に全フォントを閉じるよう、最初に宣言する。
    TtfRuntime runtime;
    const std::thread::id owner{std::this_thread::get_id()};
    const std::string default_path{runtime.initialized ? resolve_default_font() : std::string{}};
    struct CachedFont { FontSpec specification; Font font; };
    std::vector<CachedFont> cache;
    using FontFitKey = std::tuple<std::string, std::uint32_t, std::string,
                                  std::uint32_t, bool, FontSizeMode, int, bool>;
    mutable std::mutex font_fit_mutex;
    mutable std::map<FontFitKey, float> fitted_sizes;

    std::optional<float> resolve_font_size_points(const FontSpec& specification,
                                                   TTF_Font* selected,
                                                   const std::string& path,
                                                   const std::uint32_t face_index,
                                                   const bool directwrite_selected,
                                                   const float seed_points) const {
        if (specification.size_mode == FontSizeMode::TypographicPoints) return seed_points;
        const int request = specification.size_mode == FontSizeMode::DefaultHeight
            ? 0 : static_cast<int>(std::clamp(
                std::lround(std::fabs(static_cast<double>(specification.points))), 1L, 16384L));
        const FontFitKey key{path, face_index, specification.family, specification.weight,
                             specification.italic, specification.size_mode, request,
                             directwrite_selected};
        {
            std::scoped_lock lock(font_fit_mutex);
            if (const auto found = fitted_sizes.find(key); found != fitted_sizes.end())
                return found->second;
        }
        const auto open_at = [&](const float points) -> Font {
#if defined(_WIN32)
            return directwrite_selected
                ? open_windows_editor_font_face(path, points, face_index)
                : Font{TTF_OpenFont(path.c_str(), points)};
#else
            (void)face_index;
            (void)directwrite_selected;
            return Font{TTF_OpenFont(path.c_str(), points)};
#endif
        };
        auto resolved = fit_selected_face_size(specification, selected, seed_points, open_at);
        if (!resolved) {
            const auto target = requested_font_size_target(specification, selected);
            const int current_height = selected_font_height(selected, specification.size_mode);
            if (target.height > 0 && current_height > 0)
                resolved = seed_points * static_cast<float>(target.height) /
                           static_cast<float>(current_height);
            else if (specification.size_mode == FontSizeMode::DefaultHeight)
                resolved = seed_points;
        }
        if (!resolved || !std::isfinite(*resolved) || *resolved <= 0.0F) return {};
        std::scoped_lock lock(font_fit_mutex);
        return fitted_sizes.emplace(key, *resolved).first->second;
    }
    // UIの字形抽出は、既存のファミリーの代替も含めて open() が選んだ同じファイ
    // ルを使う。 ワーカーからの呼び出しは、独立したFreeTypeのフェイスを持つ。
    std::unordered_map<TTF_Font*,std::shared_ptr<ContourFont>> contours;
    // フェイスは、対応するキャッシュ済み TTF_Font の寿命に従う。 サイズ・書体
    // が異なっても一つのファイル領域を共有し、八フェイスを超えてもキャッシュを捨てない。
    std::unordered_map<std::string,std::weak_ptr<ContourFile>> contour_files;
    struct MeasureKey {
        std::string text;
        FontSpec font;
        int wrap{};
        bool operator==(const MeasureKey& other) const noexcept {
            return text == other.text && wrap == other.wrap &&
                   same_font(font, other.font);
        }
    };
    struct MeasureLookup {
        std::string_view text;
        const FontSpec* font{};
        int wrap{};
    };
    struct MeasureKeyHash {
        using is_transparent=void;
        static std::size_t hash(std::string_view text,const FontSpec& font,int wrap) noexcept {
            std::size_t result = std::hash<std::string_view>{}(text);
            const auto combine = [&result](const std::size_t hash) {
                result ^= hash + 0x9e3779b9U + (result << 6U) + (result >> 2U);
            };
            combine(std::hash<std::string_view>{}(font.family));
            combine(std::hash<float>{}(font.points));
            combine(std::hash<std::uint32_t>{}(font.weight));
            combine(std::hash<bool>{}(font.italic));
            combine(std::hash<bool>{}(font.underline));
            combine(std::hash<bool>{}(font.strikeout));
            combine(std::hash<bool>{}(font.decorations_in_policy));
            combine(std::hash<unsigned>{}(static_cast<unsigned>(font.size_mode)));
            combine(std::hash<int>{}(wrap));
            return result;
        }
        std::size_t operator()(const MeasureKey& value) const noexcept {
            return hash(value.text,value.font,value.wrap);
        }
        std::size_t operator()(const MeasureLookup& value) const noexcept {
            return hash(value.text,*value.font,value.wrap);
        }
    };
    struct MeasureKeyEqual {
        using is_transparent=void;
        static bool equal(std::string_view a_text,const FontSpec& a_font,int a_wrap,
                          std::string_view b_text,const FontSpec& b_font,int b_wrap) noexcept {
            return a_text==b_text && a_wrap==b_wrap && same_font(a_font,b_font);
        }
        bool operator()(const MeasureKey& a,const MeasureKey& b) const noexcept {
            return equal(a.text,a.font,a.wrap,b.text,b.font,b.wrap);
        }
        bool operator()(const MeasureKey& a,const MeasureLookup& b) const noexcept {
            return equal(a.text,a.font,a.wrap,b.text,*b.font,b.wrap);
        }
        bool operator()(const MeasureLookup& a,const MeasureKey& b) const noexcept {
            return equal(a.text,*a.font,a.wrap,b.text,b.font,b.wrap);
        }
    };
    // UIラベル・表のセル・メニュー見出しは、不変のフレームを準備
    // する間に繰り返し計測される。 上限付きキャッシュを使い、再
    // 描画のたびに同じ入力でSDL_ttfを呼ばない。 倍率・フ
    // ォントの変更は、自然に別の FontSpec キーになる。
    std::list<const MeasureKey*> measurement_recency;
    struct CachedMeasurement {
        Size size;
        std::list<const MeasureKey*>::iterator recent;
    };
    std::unordered_map<MeasureKey, CachedMeasurement, MeasureKeyHash, MeasureKeyEqual> measurements;
    bool measurement_limit_logged{};

    Font open(const FontSpec& specification) const {
        const EditorPerformanceScope performance("font.open");
        if (!runtime.initialized) return {};
        // SDL_ttf はポイント数を符号付き26.6形式の整数単位に変換す
        // る。 既定高さの明示要求では中立の開始サイズを使い、 共通の寸法調整
        // 処理が、選択したTTF書体をプラットフォームの目標寸法へ合わせる。
        const float seed_points = specification.size_mode == FontSizeMode::DefaultHeight
            ? 10.0F
            : (specification.size_mode == FontSizeMode::TypographicPoints
                ? specification.points
                : std::clamp(specification.points, 1.0F, 64.0F));
        if(!std::isfinite(seed_points)||seed_points<=0||
           double(seed_points)>double(std::numeric_limits<int>::max())/64)return {};
        std::string path = default_path;
        std::uint32_t face_index = 0;
        bool synthesize_bold = specification.weight >= 600U;
        bool synthesize_italic = specification.italic;
        bool directwrite_selected = false;
#if defined(_WIN32)
        if (!specification.family.empty()) {
            if (const auto selected = resolve_windows_editor_font(specification)) {
                path = selected->path;
                face_index = selected->face_index;
                synthesize_bold = selected->synthesize_bold;
                synthesize_italic = selected->synthesize_italic;
                directwrite_selected = true;
            }
        }
#endif
        if (!specification.family.empty()
#if defined(_WIN32)
            && !directwrite_selected
#endif
            ) {
            const auto selected = find_platform_font(specification.family);
            if (!selected.empty()) path = selected;
        }
        if (path.empty()) return {};
#if defined(_WIN32)
        Font font = directwrite_selected
            ? open_windows_editor_font_face(path, seed_points, face_index)
            : Font{TTF_OpenFont(path.c_str(), seed_points)};
        const bool check_selected_face = directwrite_selected || path != default_path || face_index != 0;
#else
        Font font{TTF_OpenFont(path.c_str(), seed_points)};
        const bool check_selected_face = path != default_path;
#endif
        // ContourFont は同じ解決済みファイルのフェイス番号を使い、
        // FreeTypeの style_flags を確認して、 本来の太字
        // ・斜体に疑似装飾を重ねない。 D版のGDIは欠けた字形ごとにWindows
        // のフォントリンクを使えるが、SDL_ttfは一書体だけを開く。 そ
        // の再現とはせず、日本語を揃えた書体全体への既存の代替を維持する。
        if (check_selected_face && (!font || !has_japanese_ui_glyphs(font.get()))) {
            font.reset();
            path = default_path;
            face_index = 0;
#if defined(_WIN32)
            directwrite_selected = false;
#endif
            synthesize_bold = specification.weight >= 600U;
            synthesize_italic = specification.italic;
            if (!default_path.empty()) font.reset(TTF_OpenFont(path.c_str(), seed_points));
        }
        if (!font) return {};
        const auto set_source_properties = [&](TTF_Font* selected) {
            const auto properties = TTF_GetFontProperties(selected);
            const bool path_set = SDL_SetStringProperty(properties, font_source_property, path.c_str());
            const bool face_set = SDL_SetNumberProperty(properties, font_source_face_property,
                                                         static_cast<Sint64>(face_index));
            return face_index == 0 || (path_set && face_set);
        };
        if (!set_source_properties(font.get())) return {};
        if (specification.size_mode != FontSizeMode::TypographicPoints) {
            if (const auto points = resolve_font_size_points(
                    specification, font.get(), path, face_index, directwrite_selected, seed_points);
                points && std::abs(*points - seed_points) >= (1.0F / 64.0F)) {
#if defined(_WIN32)
                Font sized = directwrite_selected
                    ? open_windows_editor_font_face(path, *points, face_index)
                    : Font{TTF_OpenFont(path.c_str(), *points)};
#else
                Font sized{TTF_OpenFont(path.c_str(), *points)};
#endif
                if (sized && set_source_properties(sized.get())) font = std::move(sized);
            }
        }
        TTF_FontStyleFlags style = TTF_STYLE_NORMAL;
        if (synthesize_bold) style |= TTF_STYLE_BOLD;
        if (synthesize_italic) style |= TTF_STYLE_ITALIC;
        if (specification.underline && !specification.decorations_in_policy) style |= TTF_STYLE_UNDERLINE;
        if (specification.strikeout && !specification.decorations_in_policy) style |= TTF_STYLE_STRIKETHROUGH;
        TTF_SetFontStyle(font.get(), style);
        return font;
    }
};

SdlFontProvider::SdlFontProvider() : impl_(std::make_unique<Impl>()) {
    if (!impl_->runtime.initialized)
        throw std::runtime_error(std::string("SDL_ttf initialization failed: ") +
                                 SDL_GetError());
    if (impl_->default_path.empty())
        throw std::runtime_error(
            "SDL UI font selection failed: no usable Japanese font");
    editor_action_log("SDL UI font selected: " + impl_->default_path);
}
SdlFontProvider::~SdlFontProvider() = default;

TTF_Font* SdlFontProvider::font_for(const FontSpec& specification) {
    if (std::this_thread::get_id() != impl_->owner)
        throw std::logic_error("SDL rendering fonts must be used on their creating thread");
    const auto effective = effective_font(specification);
    for (const auto& cached : impl_->cache)
        if (same_font(cached.specification, effective)) return cached.font.get();
    auto font = impl_->open(effective);
    if (!font) return nullptr;
    impl_->cache.push_back({effective, std::move(font)});
    return impl_->cache.back().font.get();
}

std::optional<float> SdlFontProvider::ink_center_offset(const FontSpec& font) {
    if (std::this_thread::get_id() != impl_->owner) return std::nullopt;
    TTF_Font* handle = font_for(font);
    if (!handle) return std::nullopt;
    int minx{}, maxx{}, miny{}, maxy{}, advance{};
    if (!TTF_GetGlyphMetrics(handle, 0x65E5U, &minx, &maxx, &miny, &maxy, &advance) ||
        maxy <= miny) return std::nullopt;
    // 字形の寸法は基線を基準とし、yは上向き。 行の領域はアセントから
    // ディセントまでで、measure_text もその範囲を使う。
    const float ascent = static_cast<float>(TTF_GetFontAscent(handle));
    const float height = static_cast<float>(TTF_GetFontHeight(handle));
    if (height <= 0.0F) return std::nullopt;
    const float ink_center_from_top = ascent - (static_cast<float>(maxy) + static_cast<float>(miny)) * 0.5F;
    return ink_center_from_top - height * 0.5F;
}

int SdlFontProvider::wrap_pixels(float width) noexcept {
    if (!std::isfinite(width) || width <= 0) return 0;
    return static_cast<int>(std::clamp(static_cast<double>(width), 1.0,
        static_cast<double>(std::numeric_limits<int>::max())));
}

Size SdlFontProvider::measure_text(std::string_view text, const FontSpec& font, float wrap_width) {
    if (text.empty()) return {};
    const int wrap = wrap_pixels(wrap_width);
    if (std::this_thread::get_id() == impl_->owner) {
        const auto effective = effective_font(font);
        const Impl::MeasureLookup lookup{text,&effective,wrap};
        if (const auto found = impl_->measurements.find(lookup);
            found != impl_->measurements.end()) {
            impl_->measurement_recency.splice(impl_->measurement_recency.begin(),
                impl_->measurement_recency, found->second.recent);
            return found->second.size;
        }
        const auto result = measure(font_for(effective), text, wrap);
        constexpr std::size_t maximum_measurements = 4096U;
        // キャッシュ上限に達しても、全計測を捨てず、最近使ったメニ
        // ュー・UIの計測値を残す。 上限自体は維持する。unordered_map
        // のキー参照は再ハッシュでも有効。
        if (impl_->measurements.size() >= maximum_measurements) {
            impl_->measurements.erase(*impl_->measurement_recency.back());
            impl_->measurement_recency.pop_back();
            if (!impl_->measurement_limit_logged) {
                impl_->measurement_limit_logged = true;
                editor_action_log("PERF cache=font.measure policy=lru capacity=4096 first_eviction");
            }
        }
        const auto [entry, added] = impl_->measurements.emplace(
            Impl::MeasureKey{std::string(text),effective,wrap}, Impl::CachedMeasurement{result,{}});
        if (added) {
            try {
                impl_->measurement_recency.push_front(&entry->first);
                entry->second.recent = impl_->measurement_recency.begin();
            } catch (...) {
                impl_->measurements.erase(entry);
                throw;
            }
        }
        return result;
    }
    // SDL_ttf の計測は、ハンドルを作ったスレッドで行う必要がある。
    // UIハンドルをミューテックスで共有しても、その条件には違反する。
    const auto local = impl_->open(effective_font(font));
    return measure(local.get(), text, wrap);
}

FontMetrics SdlFontProvider::font_metrics(const FontSpec& font) {
    const auto metrics=[](TTF_Font* handle){
        if(!handle)return FontMetrics{};
        // SDL_ttf は基線より下のディセントを負で返す。 公開の寸法で
        // は、D版・SWTの FontMetrics と同じ正の距離にする。
        return FontMetrics{float(TTF_GetFontHeight(handle)),float(TTF_GetFontAscent(handle)),
                           float(std::abs(TTF_GetFontDescent(handle)))};
    };
    if(std::this_thread::get_id()==impl_->owner)return metrics(font_for(font));
    const auto local=impl_->open(effective_font(font));return metrics(local.get());
}

std::optional<GlyphPath> SdlFontProvider::glyph_path(char32_t scalar,const FontSpec& specification) {
    if(scalar>0x10ffff||(scalar>=0xd800&&scalar<=0xdfff))return std::nullopt;
    const bool ui_thread=std::this_thread::get_id()==impl_->owner;
    Font worker_font;TTF_Font* selected{};
    if(ui_thread)selected=font_for(specification);
    else{worker_font=impl_->open(effective_font(specification));selected=worker_font.get();}
    if(!selected)return std::nullopt;
    std::shared_ptr<ContourFont> native;
    if(ui_thread){
        const auto found=impl_->contours.find(selected);
        if(found!=impl_->contours.end())native=found->second;
        else{native=ContourFont::open(selected,&impl_->contour_files);if(native)impl_->contours.emplace(selected,native);}
    }else native=ContourFont::open(selected);
    if(!native)return std::nullopt;
    const auto index=FT_Get_Char_Index(native->face,static_cast<FT_ULong>(scalar));
    // 有効なスカラー値がこの書体にない場合は、通常のネイティブ描
    // 画と同様、.notdef の輪郭を使う。 輪郭機能自体が
    // 未対応なのではないため、行全体を代替描画へ切り替えない。
    const auto flags=FT_LOAD_NO_BITMAP|(specification.antialias?FT_LOAD_TARGET_NORMAL:FT_LOAD_TARGET_MONO);
    if(FT_Load_Glyph(native->face,index,flags)||native->face->glyph->format!=FT_GLYPH_FORMAT_OUTLINE)return std::nullopt;
    auto& outline=native->face->glyph->outline;
    FT_BBox bounds{};FT_Outline_Get_CBox(&outline,&bounds);
    // フォントによっては、送り幅だけの空白を輪郭ゼロ個ではなく、寸
    // 法ゼロの輪郭として表す。 独立した送り幅は保持するが、指定さ
    // れた丸い枠線が点として描いてしまうような座標は公開しない。
    if(!outline.n_points||(bounds.xMin==bounds.xMax&&bounds.yMin==bounds.yMax))return GlyphPath{};
    // 同梱のSDL_ttfの字形処理と同じ疑似的な傾斜を使う。 配置・線
    // ・アンチエイリアスの方針は、このネイティブ抽出には持ち込まない。
    if(specification.italic && !(native->face->style_flags&FT_STYLE_FLAG_ITALIC)){FT_Matrix shear{1L<<16,0x0366AL,0,1L<<16};FT_Outline_Transform(&outline,&shear);}
    if(specification.weight>=600 && !(native->face->style_flags&FT_STYLE_FLAG_BOLD)){
        // SWTと同様、太字の輪郭はネイティブのフォント処理から得る
        // 。 FreeTypeの輪郭合成で対応し、SDLのビットマッ
        // プの位置ずらしを複製しない。 重なった輪郭を作ると、SWT
        // 標準の偶奇規則の塗りつぶしで打ち消し合ってしまうため。
        const auto strength=FT_Pos(native->face->size->metrics.y_ppem/10)*64;
        if(FT_Outline_EmboldenXY(&outline,strength,0))return std::nullopt;
    }
    struct Extraction {
        GlyphPath path;float ascent;bool open{};std::exception_ptr error;
        Point point(const FT_Vector* value)const{return {float(value->x)/64,ascent-float(value->y)/64};}
        void close(){if(open){path.commands.push_back({GlyphPathKind::Close,{}});open=false;}}
    } extracted{{},float(TTF_GetFontAscent(selected))};
    FT_Outline_Funcs callbacks{};
    callbacks.move_to=[](const FT_Vector* p,void* data){auto& e=*static_cast<Extraction*>(data);try{e.close();e.path.commands.push_back({GlyphPathKind::Move,{e.point(p)}});e.open=true;return 0;}catch(...){e.error=std::current_exception();return 1;}};
    callbacks.line_to=[](const FT_Vector* p,void* data){auto& e=*static_cast<Extraction*>(data);try{e.path.commands.push_back({GlyphPathKind::Line,{e.point(p)}});return 0;}catch(...){e.error=std::current_exception();return 1;}};
    callbacks.conic_to=[](const FT_Vector* c,const FT_Vector* p,void* data){auto& e=*static_cast<Extraction*>(data);try{e.path.commands.push_back({GlyphPathKind::Quadratic,{e.point(c),e.point(p)}});return 0;}catch(...){e.error=std::current_exception();return 1;}};
    callbacks.cubic_to=[](const FT_Vector* a,const FT_Vector* b,const FT_Vector* p,void* data){auto& e=*static_cast<Extraction*>(data);try{e.path.commands.push_back({GlyphPathKind::Cubic,{e.point(a),e.point(b),e.point(p)}});return 0;}catch(...){e.error=std::current_exception();return 1;}};
    const auto decomposition=FT_Outline_Decompose(&outline,&callbacks,&extracted);
    if(extracted.error)std::rethrow_exception(extracted.error);
    if(decomposition)return std::nullopt;
    extracted.close();
    return std::move(extracted.path);
}

} // namespace cwxeditor::lib
