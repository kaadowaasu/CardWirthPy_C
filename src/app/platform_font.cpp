#include "platform_font.hpp"
#include "cw/lib/encoding.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <CoreText/CoreText.h>
#endif

#if defined(__ANDROID__)
#include <SDL3/SDL_system.h>
#endif

// WebとLinuxでは、同梱フォントだけを使って文字を描画する。
#if defined(__EMSCRIPTEN__) || (defined(__linux__) && !defined(__ANDROID__))
#define CARDWIRTH_BUNDLED_FONTS 1
#endif

namespace {

std::string normalized_family(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const unsigned char character : value) {
        // 「ヒラギノ角ゴ」などのフォント名をキャッシュ・検索キーで区別できるよう
        // 、UTF-8のバイト列を保つ。 バイト単位のctypeではUTF-8
        // の大文字・小文字を安全に揃えられないため、ASCIIだけを変換する。
        if (character >= 0x80U)
            result.push_back(static_cast<char>(character));
        else if (std::isalnum(character) != 0)
            result.push_back(static_cast<char>(std::tolower(character)));
    }
    return result;
}

[[maybe_unused]] bool generic_sans_family(std::string_view value) {
    const std::string normalized = normalized_family(value);
    return normalized.empty() || normalized == "systemui" || normalized == "sans" ||
           normalized == "sansserif" || normalized == "ui" || normalized == "default" ||
           normalized == "gothic" || normalized == "uigothic" ||
           normalized == "pgothic" || normalized == "ipagothic" ||
           normalized == "ipauigothic" || normalized == "ipapgothic" ||
           normalized == "ipaゴシック" || normalized == "ipauiゴシック" ||
           normalized == "ipapゴシック" || normalized == "msgothic" ||
           normalized == "msuigothic" || normalized == "mspgothic" ||
           normalized == "ＭＳゴシック" || normalized == "ＭＳＵＩゴシック" ||
           normalized == "ＭＳＰゴシック";
}

[[maybe_unused]] bool generic_serif_family(std::string_view value) {
    const std::string normalized = normalized_family(value);
    return normalized == "serif" || normalized == "uiserif" ||
           normalized == "mincho" || normalized == "pmincho" ||
           normalized == "ipamincho" || normalized == "ipapmincho" ||
           normalized == "ipa明朝" || normalized == "ipap明朝" ||
           normalized == "msmincho" || normalized == "mspmincho" ||
           normalized == "ＭＳ明朝" || normalized == "ＭＳＰ明朝";
}

[[maybe_unused]] bool generic_monospace_family(std::string_view value) {
    const std::string normalized = normalized_family(value);
    return normalized == "monospace" || normalized == "monospaced" ||
           normalized == "uimonospace" || normalized == "code";
}

[[maybe_unused]] std::string first_existing(const std::vector<std::string>& candidates) {
    std::error_code error;
    for (const auto& candidate : candidates) {
        const auto path=::cw::lib::encoding::path_from_utf8(candidate);
        if (!candidate.empty() && std::filesystem::exists(path, error)) {
            if(path.is_absolute()) return candidate;
            const auto absolute=std::filesystem::absolute(path,error);
            if(!error) return ::cw::lib::encoding::path_to_utf8(absolute.lexically_normal());
        }
        error.clear();
    }
    return {};
}

[[maybe_unused]] std::string matching_font_file(
    const std::vector<std::filesystem::path>& roots, std::string_view family) {
    const std::string wanted = normalized_family(family);
    if (wanted.empty()) return {};
    std::error_code error;
    for (const auto& root : roots) {
        if (!std::filesystem::is_directory(root, error)) {
            error.clear();
            continue;
        }
        std::filesystem::recursive_directory_iterator iterator(
            root, std::filesystem::directory_options::skip_permission_denied, error);
        const std::filesystem::recursive_directory_iterator end;
        while (!error && iterator != end) {
            const auto& entry = *iterator;
            if (entry.is_regular_file(error)) {
                const std::string extension = normalized_family(
                    ::cw::lib::encoding::path_to_utf8(entry.path().extension()));
                const std::string filename = normalized_family(
                    ::cw::lib::encoding::path_to_utf8(entry.path().stem()));
                if ((extension == "ttf" || extension == "ttc" || extension == "otf") &&
                    filename.find(wanted) != std::string::npos)
                    return ::cw::lib::encoding::path_to_utf8(entry.path());
            }
            error.clear();
            iterator.increment(error);
        }
        error.clear();
    }
    return {};
}

#if defined(__APPLE__)
std::string apple_font_path(CTFontRef font) {
    if (!font) return {};
    CTFontDescriptorRef descriptor = CTFontCopyFontDescriptor(font);
    if (!descriptor) return {};
    CFTypeRef attribute = CTFontDescriptorCopyAttribute(descriptor, kCTFontURLAttribute);
    CFRelease(descriptor);
    if (!attribute || CFGetTypeID(attribute) != CFURLGetTypeID()) {
        if (attribute) CFRelease(attribute);
        return {};
    }
    char path[4096]{};
    const bool converted = CFURLGetFileSystemRepresentation(
        static_cast<CFURLRef>(attribute), true, reinterpret_cast<UInt8*>(path), sizeof(path));
    CFRelease(attribute);
    return converted ? std::string(path) : std::string{};
}

std::string apple_japanese_font(std::string_view family) {
    CTFontRef base = nullptr;
    if (generic_serif_family(family)) {
        // 日本語の明朝体を優先する。New YorkやTimesを
        // 起点にCoreTextへ日本語の補完を任せると、 OSの
        // バージョンによってはゴシック系の代替フォントが選ばれる。
        base = CTFontCreateWithName(CFSTR("Hiragino Mincho ProN"), 17.0, nullptr);
        if (!base) base = CTFontCreateWithName(CFSTR("YuMincho"), 17.0, nullptr);
        if (!base) base = CTFontCreateWithName(CFSTR("New York"), 17.0, nullptr);
        if (!base) base = CTFontCreateWithName(CFSTR("Times"), 17.0, nullptr);
    } else if (!generic_sans_family(family) && !generic_monospace_family(family)) {
        const auto* bytes = reinterpret_cast<const UInt8*>(family.data());
        if (CFStringRef name = CFStringCreateWithBytes(
                kCFAllocatorDefault, bytes, static_cast<CFIndex>(family.size()),
                kCFStringEncodingUTF8, false)) {
            base = CTFontCreateWithName(name, 17.0, nullptr);
            CFRelease(name);
        }
    }
    if (!base)
        base = CTFontCreateUIFontForLanguage(
            generic_monospace_family(family) ? kCTFontUIFontUserFixedPitch
                                             : kCTFontUIFontSystem,
            17.0, CFSTR("ja"));
    if (!base) {
        return {};
    }

    CFStringRef sample = CFSTR("日本語");
    CTFontRef matched = CTFontCreateForString(base, sample, CFRangeMake(0, CFStringGetLength(sample)));
    CFRelease(base);
    if (!matched) {
        return {};
    }

    const std::string path = apple_font_path(matched);
    CFRelease(matched);
    return path;
}
#endif


}  // namespace

std::optional<std::filesystem::path> bundled_font_root() {
#if defined(__EMSCRIPTEN__)
    // C++の初期化前に、web_fonts.jsが別途イ
    // ンストールされたフォントパッケージをここへ展開する。
    return std::filesystem::path("/program");
#elif defined(CARDWIRTH_BUNDLED_FONTS)
    // Linuxパッケージは同じファイルを実行ファイルの隣へ配置する。 CMake
    // が<実行ファイルのディレクトリ>/resourceへコピーする。
    static const std::filesystem::path root = [] {
        std::error_code error;
        const auto executable = std::filesystem::read_symlink("/proc/self/exe", error);
        if (!error && !executable.empty()) return executable.parent_path() / "resource";
        error.clear();
        return std::filesystem::current_path(error) / "resource";
    }();
    return root;
#else
    return std::nullopt;
#endif
}

std::filesystem::path bundled_ui_font() {
    const auto root = bundled_font_root();
    if (!root) return {};
    const auto path = *root / "NotoSansCJKjp-Regular.otf";
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) ? path : std::filesystem::path{};
}

std::filesystem::path bundled_logical_font(std::string_view family) {
    const auto root = bundled_font_root();
    if (!root) return {};
    const std::string key = normalized_family(family);
    for (const auto& [alias, file] : {
        std::pair{"gothic", "gothic.ttf"}, {"IPAGothic", "gothic.ttf"},
        {"IPAゴシック", "gothic.ttf"}, {"MS Gothic", "gothic.ttf"}, {"ＭＳ ゴシック", "gothic.ttf"},
        {"uigothic", "uigothic.ttf"}, {"IPAUIGothic", "uigothic.ttf"},
        {"IPA UIゴシック", "uigothic.ttf"}, {"MS UI Gothic", "uigothic.ttf"}, {"ＭＳ ＵＩゴシック", "uigothic.ttf"},
        {"mincho", "mincho.ttf"}, {"IPAMincho", "mincho.ttf"},
        {"IPA明朝", "mincho.ttf"}, {"MS Mincho", "mincho.ttf"}, {"ＭＳ 明朝", "mincho.ttf"},
        {"pgothic", "pgothic.ttf"}, {"IPAPGothic", "pgothic.ttf"},
        {"IPA Pゴシック", "pgothic.ttf"}, {"MS PGothic", "pgothic.ttf"}, {"ＭＳ Ｐゴシック", "pgothic.ttf"},
        {"pmincho", "pmincho.ttf"}, {"IPAPMincho", "pmincho.ttf"},
        {"IPA P明朝", "pmincho.ttf"}, {"MS PMincho", "pmincho.ttf"}, {"ＭＳ Ｐ明朝", "pmincho.ttf"}}) {
        if (key != normalized_family(alias)) continue;
        const auto path = *root / "Font" / file;
        std::error_code error;
        return std::filesystem::is_regular_file(path, error) ? path : std::filesystem::path{};
    }
    return {};
}

std::string find_platform_font(std::string_view family) {
    static std::mutex cache_mutex;
    static std::unordered_map<std::string, std::string> cache;
    const std::string cache_key = normalized_family(family);
    {
        std::scoped_lock lock(cache_mutex);
        if (const auto found = cache.find(cache_key); found != cache.end())
            return found->second;
    }

    std::string selected;
#if defined(CARDWIRTH_BUNDLED_FONTS)
    // WebとLinuxではCardWirthの論理フォント名に対
    // 応するIPAフォントを使い、 それ以外は同梱のNotoを使う
    // 。ホストのフォントは参照せず、両環境で同じ文字描画にする。
    auto bundled = bundled_logical_font(family);
    if (bundled.empty()) bundled = bundled_ui_font();
    selected = ::cw::lib::encoding::path_to_utf8(bundled);
#elif defined(_WIN32)
    char* windir_value = nullptr;
    std::size_t windir_size = 0;
    _dupenv_s(&windir_value, &windir_size, "WINDIR");
    const std::string windir = windir_value ? windir_value : "C:\\Windows";
    std::free(windir_value);
    const std::string fonts = windir + "\\Fonts\\";
    selected = first_existing({
        fonts + "YuGothR.ttc",
        fonts + "meiryo.ttc",
        fonts + "msgothic.ttc",
        fonts + "segoeui.ttf",
    });
#elif defined(__APPLE__)
    selected = apple_japanese_font(family);
    if (selected.empty())
        selected = first_existing({
            "/System/Library/Fonts/ヒラギノ角ゴシック W3.ttc",
            "/System/Library/Fonts/Helvetica.ttc",
        });
#elif defined(__ANDROID__)
    // APKはWeb版が/programに配置するものと同じNotoフォント
    // を、 <内部ストレージ>/app/Data/Fontへ展開する。この絶
    // 対パスで解決する。 作業ディレクトリ基準のData/Fontでは、ALL
    // のEditorなどアプリルート以外から起動した場合に フォントが見つ
    // からず、端末のフォントへ切り替わる。その行高の差で一覧の行数が変わ
    // り、 Android版とWeb版のALLダイアログに違いが生じる。
    std::string bundled_sans;
    if (const char* internal = SDL_GetAndroidInternalStoragePath(); internal && *internal)
        bundled_sans = ::cw::lib::encoding::path_to_utf8(
            std::filesystem::path(internal) / "app" / "Data" / "Font" /
            "NotoSansCJKjp-Regular.otf");
    if (generic_sans_family(family))
        selected = first_existing({bundled_sans, "Data/Font/NotoSansCJKjp-Regular.otf"});
    if (!generic_sans_family(family))
        selected = matching_font_file({"/system/fonts", "/product/fonts"}, family);
    if (selected.empty() && generic_serif_family(family))
        selected = first_existing({
            "/system/fonts/NotoSerifCJK-Regular.ttc",
            "/system/fonts/NotoSerifJP-Regular.otf",
            "/system/fonts/NotoSerif-Regular.ttf",
        });
    if (selected.empty() && generic_monospace_family(family))
        selected = first_existing({
            "/system/fonts/NotoSansMonoCJK-Regular.ttc",
            "/system/fonts/RobotoMono-Regular.ttf",
            "/system/fonts/DroidSansMono.ttf",
        });
    if (selected.empty())
        selected = first_existing({bundled_sans, "Data/Font/NotoSansCJKjp-Regular.otf"});
    if (selected.empty())
        selected = first_existing({
            "/system/fonts/NotoSansCJK-Regular.ttc",
            "/system/fonts/NotoSansJP-Regular.otf",
            "/system/fonts/NotoSans-Regular.ttf",
            "/system/fonts/Roboto-Regular.ttf",
        });
#endif

    {
        std::scoped_lock lock(cache_mutex);
        cache.emplace(cache_key, selected);
    }
    return selected;
}
