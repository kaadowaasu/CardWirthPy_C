#include "cw/lib/encoding.hpp"
#include "cw/lib/unicode_lower_data.hpp"
#include "cw/lib/python_lower_data.hpp"
#include "cw/lib/python_upper_data.hpp"
#include "cw/lib/python_japanese_codecs.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <limits>
#include <sstream>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__ANDROID__)
#include <jni.h>

#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_system.h>
#else
#include <SDL3/SDL_stdinc.h>
#endif

namespace cw::lib::encoding {
namespace {

using CodePoint = std::uint32_t;
constexpr CodePoint replacement = 0xfffdU;

void require_range(const Bytes& bytes, std::size_t offset, std::size_t count)
{
    if (offset > bytes.size() || count > bytes.size() - offset) throw EncodingError("binary data range is out of bounds");
}

void append_utf8(String& out, CodePoint cp)
{
    if (cp <= 0x7fU) out.push_back(static_cast<char>(cp));
    else if (cp <= 0x7ffU) {
        out.push_back(static_cast<char>(0xc0U | (cp >> 6)));
        out.push_back(static_cast<char>(0x80U | (cp & 0x3fU)));
    } else if (cp <= 0xffffU) {
        out.push_back(static_cast<char>(0xe0U | (cp >> 12)));
        out.push_back(static_cast<char>(0x80U | ((cp >> 6) & 0x3fU)));
        out.push_back(static_cast<char>(0x80U | (cp & 0x3fU)));
    } else {
        out.push_back(static_cast<char>(0xf0U | (cp >> 18)));
        out.push_back(static_cast<char>(0x80U | ((cp >> 12) & 0x3fU)));
        out.push_back(static_cast<char>(0x80U | ((cp >> 6) & 0x3fU)));
        out.push_back(static_cast<char>(0x80U | (cp & 0x3fU)));
    }
}

struct Utf8Unit {
    CodePoint codepoint{};
    std::size_t size{};
    bool valid{};
};

Utf8Unit peek_utf8(const String& text, std::size_t offset)
{
    if (offset >= text.size()) return {};
    const auto first = static_cast<unsigned char>(text[offset]);
    std::size_t count = 0;
    CodePoint cp = 0;
    if (first <= 0x7fU) return {first, 1, true};
    if ((first & 0xe0U) == 0xc0U) { count = 2; cp = first & 0x1fU; }
    else if ((first & 0xf0U) == 0xe0U) { count = 3; cp = first & 0x0fU; }
    else if ((first & 0xf8U) == 0xf0U) { count = 4; cp = first & 0x07U; }
    else return {replacement, 1, false};
    if (offset + count > text.size()) return {replacement, 1, false};
    for (std::size_t i = 1; i < count; ++i) {
        const auto c = static_cast<unsigned char>(text[offset + i]);
        if ((c & 0xc0U) != 0x80U) return {replacement, 1, false};
        cp = (cp << 6) | (c & 0x3fU);
    }
    const bool shortest = (count == 2 && cp >= 0x80U) || (count == 3 && cp >= 0x800U) ||
                          (count == 4 && cp >= 0x10000U);
    const bool valid = shortest && cp <= 0x10ffffU && !(cp >= 0xd800U && cp <= 0xdfffU);
    return {valid ? cp : replacement, valid ? count : 1, valid};
}

std::vector<CodePoint> codepoints(const String& text, ErrorPolicy policy)
{
    std::vector<CodePoint> result;
    std::size_t offset = 0;
    while (offset < text.size()) {
        const auto unit = peek_utf8(text, offset);
        if (!unit.valid) {
            if (policy == ErrorPolicy::Strict) throw EncodingError("invalid UTF-8 sequence");
            ++offset;
            if (policy == ErrorPolicy::Replace) result.push_back(replacement);
            continue;
        }
        result.push_back(unit.codepoint);
        offset += unit.size;
    }
    return result;
}

String codepoints_to_utf8(const std::vector<CodePoint>& values)
{
    String result;
    for (const auto cp : values) append_utf8(result, cp);
    return result;
}

Bytes encode_utf8_impl(const String& text, ErrorPolicy policy)
{
    const auto values = codepoints(text, policy);
    const auto result = codepoints_to_utf8(values);
    return Bytes(result.begin(), result.end());
}

String decode_utf8_bytes(const Bytes& bytes, ErrorPolicy policy, bool strip_bom)
{
    String raw;
    if (!bytes.empty()) raw.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (strip_bom && raw.size() >= 3 && static_cast<unsigned char>(raw[0]) == 0xef &&
        static_cast<unsigned char>(raw[1]) == 0xbb && static_cast<unsigned char>(raw[2]) == 0xbf) raw.erase(0, 3);
    return codepoints_to_utf8(codepoints(raw, policy));
}

bool is_line_break(CodePoint codepoint) noexcept
{
    switch (codepoint) {
    case 0x000aU: case 0x000bU: case 0x000cU: case 0x000dU:
    case 0x001cU: case 0x001dU: case 0x001eU: case 0x0085U:
    case 0x2028U: case 0x2029U:
        return true;
    default:
        return false;
    }
}

std::uint16_t read16_raw(const Bytes& bytes, std::size_t offset, ByteOrder order)
{
    require_range(bytes, offset, 2);
    const auto a = bytes[offset], b = bytes[offset + 1];
    return order == ByteOrder::Little ? static_cast<std::uint16_t>(a | (b << 8))
                                      : static_cast<std::uint16_t>((a << 8) | b);
}

String decode_utf16_impl(const Bytes& bytes, ByteOrder order, ErrorPolicy policy, bool strip_bom)
{
    std::size_t offset = 0;
    if (strip_bom && bytes.size() >= 2) {
        if (bytes[0] == 0xff && bytes[1] == 0xfe) { order = ByteOrder::Little; offset = 2; }
        else if (bytes[0] == 0xfe && bytes[1] == 0xff) { order = ByteOrder::Big; offset = 2; }
    }
    if ((bytes.size() - offset) % 2 != 0) {
        if (policy == ErrorPolicy::Strict) throw EncodingError("odd UTF-16 byte count");
    }
    std::vector<CodePoint> values;
    while (offset + 1 < bytes.size()) {
        const auto unit = read16_raw(bytes, offset, order); offset += 2;
        if (unit >= 0xd800U && unit <= 0xdbffU) {
            if (offset + 1 < bytes.size()) {
                const auto low = read16_raw(bytes, offset, order);
                if (low >= 0xdc00U && low <= 0xdfffU) {
                    offset += 2;
                    values.push_back(0x10000U + ((static_cast<CodePoint>(unit) - 0xd800U) << 10) + low - 0xdc00U);
                    continue;
                }
            }
            if (policy == ErrorPolicy::Strict) throw EncodingError("unpaired UTF-16 high surrogate");
            if (policy == ErrorPolicy::Replace) values.push_back(replacement);
        } else if (unit >= 0xdc00U && unit <= 0xdfffU) {
            if (policy == ErrorPolicy::Strict) throw EncodingError("unpaired UTF-16 low surrogate");
            if (policy == ErrorPolicy::Replace) values.push_back(replacement);
        } else values.push_back(unit);
    }
    if (offset < bytes.size() && policy == ErrorPolicy::Replace) values.push_back(replacement);
    return codepoints_to_utf8(values);
}

Bytes encode_utf16_impl(const String& text, ByteOrder order, ErrorPolicy policy, bool emit_bom)
{
    const auto values = codepoints(text, policy);
    Bytes result;
    if (emit_bom) {
        if (order == ByteOrder::Little) { result.push_back(0xff); result.push_back(0xfe); }
        else { result.push_back(0xfe); result.push_back(0xff); }
    }
    const auto put = [&](std::uint16_t unit) {
        if (order == ByteOrder::Little) { result.push_back(static_cast<std::uint8_t>(unit)); result.push_back(static_cast<std::uint8_t>(unit >> 8)); }
        else { result.push_back(static_cast<std::uint8_t>(unit >> 8)); result.push_back(static_cast<std::uint8_t>(unit)); }
    };
    for (const auto cp : values) {
        if (cp <= 0xffffU) put(static_cast<std::uint16_t>(cp));
        else { const auto n = cp - 0x10000U; put(static_cast<std::uint16_t>(0xd800U + (n >> 10))); put(static_cast<std::uint16_t>(0xdc00U + (n & 0x3ffU))); }
    }
    return result;
}

String decode_utf32_impl(const Bytes& bytes, ByteOrder order, ErrorPolicy policy, bool strip_bom)
{
    std::size_t offset = 0;
    if (strip_bom && bytes.size() >= 4) {
        if (bytes[0] == 0xff && bytes[1] == 0xfe && bytes[2] == 0 && bytes[3] == 0) {
            order = ByteOrder::Little; offset = 4;
        } else if (bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 0xfe && bytes[3] == 0xff) {
            order = ByteOrder::Big; offset = 4;
        }
    }
    if ((bytes.size() - offset) % 4 != 0 && policy == ErrorPolicy::Strict)
        throw EncodingError("unaligned UTF-32 byte count");
    std::vector<CodePoint> values;
    while (offset + 3 < bytes.size()) {
        CodePoint value{};
        if (order == ByteOrder::Little) {
            value = static_cast<CodePoint>(bytes[offset]) |
                    (static_cast<CodePoint>(bytes[offset + 1]) << 8U) |
                    (static_cast<CodePoint>(bytes[offset + 2]) << 16U) |
                    (static_cast<CodePoint>(bytes[offset + 3]) << 24U);
        } else {
            value = (static_cast<CodePoint>(bytes[offset]) << 24U) |
                    (static_cast<CodePoint>(bytes[offset + 1]) << 16U) |
                    (static_cast<CodePoint>(bytes[offset + 2]) << 8U) |
                    static_cast<CodePoint>(bytes[offset + 3]);
        }
        offset += 4;
        if (value > 0x10ffffU || (value >= 0xd800U && value <= 0xdfffU)) {
            if (policy == ErrorPolicy::Strict) throw EncodingError("invalid UTF-32 code point");
            if (policy == ErrorPolicy::Replace) values.push_back(replacement);
        } else values.push_back(value);
    }
    if (offset < bytes.size() && policy == ErrorPolicy::Replace) values.push_back(replacement);
    return codepoints_to_utf8(values);
}

Bytes encode_utf32_impl(const String& text, ByteOrder order, ErrorPolicy policy, bool emit_bom)
{
    Bytes result;
    if (emit_bom) {
        if (order == ByteOrder::Little) result.insert(result.end(), {0xff, 0xfe, 0, 0});
        else result.insert(result.end(), {0, 0, 0xfe, 0xff});
    }
    for (const auto value : codepoints(text, policy)) {
        if (order == ByteOrder::Little) {
            result.push_back(static_cast<std::uint8_t>(value));
            result.push_back(static_cast<std::uint8_t>(value >> 8U));
            result.push_back(static_cast<std::uint8_t>(value >> 16U));
            result.push_back(static_cast<std::uint8_t>(value >> 24U));
        } else {
            result.push_back(static_cast<std::uint8_t>(value >> 24U));
            result.push_back(static_cast<std::uint8_t>(value >> 16U));
            result.push_back(static_cast<std::uint8_t>(value >> 8U));
            result.push_back(static_cast<std::uint8_t>(value));
        }
    }
    return result;
}

constexpr std::array<std::uint16_t, 128> cp437_table{
    0x00c7,0x00fc,0x00e9,0x00e2,0x00e4,0x00e0,0x00e5,0x00e7,0x00ea,0x00eb,0x00e8,0x00ef,0x00ee,0x00ec,0x00c4,0x00c5,
    0x00c9,0x00e6,0x00c6,0x00f4,0x00f6,0x00f2,0x00fb,0x00f9,0x00ff,0x00d6,0x00dc,0x00a2,0x00a3,0x00a5,0x20a7,0x0192,
    0x00e1,0x00ed,0x00f3,0x00fa,0x00f1,0x00d1,0x00aa,0x00ba,0x00bf,0x2310,0x00ac,0x00bd,0x00bc,0x00a1,0x00ab,0x00bb,
    0x2591,0x2592,0x2593,0x2502,0x2524,0x2561,0x2562,0x2556,0x2555,0x2563,0x2551,0x2557,0x255d,0x255c,0x255b,0x2510,
    0x2514,0x2534,0x252c,0x251c,0x2500,0x253c,0x255e,0x255f,0x255a,0x2554,0x2569,0x2566,0x2560,0x2550,0x256c,0x2567,
    0x2568,0x2564,0x2565,0x2559,0x2558,0x2552,0x2553,0x256b,0x256a,0x2518,0x250c,0x2588,0x2584,0x258c,0x2590,0x2580,
    0x03b1,0x00df,0x0393,0x03c0,0x03a3,0x03c3,0x00b5,0x03c4,0x03a6,0x0398,0x03a9,0x03b4,0x221e,0x03c6,0x03b5,0x2229,
    0x2261,0x00b1,0x2265,0x2264,0x2320,0x2321,0x00f7,0x2248,0x00b0,0x2219,0x00b7,0x221a,0x207f,0x00b2,0x25a0,0x00a0};

String decode_cp437_impl(const Bytes& bytes)
{
    String result;
    for (const auto byte : bytes) append_utf8(result, byte < 0x80 ? byte : cp437_table[byte - 0x80]);
    return result;
}

Bytes encode_cp437_impl(const String& text, ErrorPolicy policy)
{
    Bytes result;
    for (const auto cp : codepoints(text, policy)) {
        if (cp < 0x80U) result.push_back(static_cast<std::uint8_t>(cp));
        else {
            const auto it = std::find(cp437_table.begin(), cp437_table.end(), cp);
            if (it == cp437_table.end()) {
                if (policy == ErrorPolicy::Strict) throw EncodingError("character is not representable in CP437");
                if (policy == ErrorPolicy::Replace) result.push_back('?');
            } else result.push_back(static_cast<std::uint8_t>(0x80 + (it - cp437_table.begin())));
        }
    }
    return result;
}

String decode_latin1_impl(const Bytes& bytes)
{
    String result;
    for (const auto byte : bytes) append_utf8(result, byte);
    return result;
}

Bytes encode_latin1_impl(const String& text, ErrorPolicy policy)
{
    Bytes result;
    for (const auto cp : codepoints(text, policy)) {
        if (cp <= 0xffU) result.push_back(static_cast<std::uint8_t>(cp));
        else if (policy == ErrorPolicy::Strict) throw EncodingError("character is not representable in Latin-1");
        else if (policy == ErrorPolicy::Replace) result.push_back('?');
    }
    return result;
}

bool cp932_lead(std::uint8_t c) noexcept { return (c >= 0x81U && c <= 0x9fU) || (c >= 0xe0U && c <= 0xfcU); }

#if defined(_WIN32)
std::wstring cp932_piece(const std::uint8_t* data, int size)
{
    const int required = MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, reinterpret_cast<const char*>(data), size, nullptr, 0);
    if (required <= 0) throw EncodingError("invalid CP932 sequence");
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, reinterpret_cast<const char*>(data), size, result.data(), required) <= 0) throw EncodingError("CP932 conversion failed");
    return result;
}

String wide_utf8(const std::wstring& wide)
{
    if (wide.empty()) return {};
    const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0) throw EncodingError("UTF-16 to UTF-8 conversion failed");
    String result(static_cast<std::size_t>(required), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), static_cast<int>(wide.size()), result.data(), required, nullptr, nullptr) <= 0) throw EncodingError("UTF-16 to UTF-8 conversion failed");
    return result;
}

std::wstring cp_to_wide(CodePoint cp)
{
    std::wstring result;
    if (cp <= 0xffffU) result.push_back(static_cast<wchar_t>(cp));
    else { const auto n = cp - 0x10000U; result.push_back(static_cast<wchar_t>(0xd800U + (n >> 10))); result.push_back(static_cast<wchar_t>(0xdc00U + (n & 0x3ffU))); }
    return result;
}

Bytes cp932_wide_piece(const std::wstring& wide)
{
    BOOL used_default = FALSE;
    const int required = WideCharToMultiByte(932, WC_NO_BEST_FIT_CHARS, wide.data(), static_cast<int>(wide.size()), nullptr, 0, "?", &used_default);
    if (required <= 0 || used_default) throw EncodingError("character is not representable in CP932");
    Bytes result(static_cast<std::size_t>(required));
    used_default = FALSE;
    if (WideCharToMultiByte(932, WC_NO_BEST_FIT_CHARS, wide.data(), static_cast<int>(wide.size()), reinterpret_cast<char*>(result.data()), required, "?", &used_default) <= 0 || used_default) throw EncodingError("CP932 conversion failed");
    return result;
}

#elif defined(__ANDROID__)
bool clear_android_exception(JNIEnv* environment) noexcept
{
    if (!environment || !environment->ExceptionCheck()) return false;
    environment->ExceptionClear();
    return true;
}

class AndroidLocalFrame {
public:
    explicit AndroidLocalFrame(JNIEnv* environment) : m_environment(environment)
    {
        if (!m_environment || m_environment->PushLocalFrame(12) < 0 || clear_android_exception(m_environment))
            throw EncodingError("Android charset conversion setup failed");
        m_active = true;
    }

    AndroidLocalFrame(const AndroidLocalFrame&) = delete;
    AndroidLocalFrame& operator=(const AndroidLocalFrame&) = delete;

    ~AndroidLocalFrame()
    {
        if (m_active) m_environment->PopLocalFrame(nullptr);
    }

private:
    JNIEnv* m_environment{};
    bool m_active{};
};

JNIEnv* android_jni_environment()
{
    auto* environment = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    if (!environment) throw EncodingError("Android charset conversion is unavailable");
    return environment;
}

jstring android_charset_name(JNIEnv* environment, const char* name)
{
    auto value = environment->NewStringUTF(name);
    if (!value || clear_android_exception(environment))
        throw EncodingError(String("Android charset is unavailable: ") + name);
    return value;
}

String android_decode_charset(const std::uint8_t* data, std::size_t size,
                              const char* charset_name)
{
    auto* environment = android_jni_environment();
    AndroidLocalFrame frame(environment);
    auto bytes = environment->NewByteArray(static_cast<jsize>(size));
    if (!bytes || clear_android_exception(environment))
        throw EncodingError("Android CP932 byte allocation failed");
    if (size != 0U) {
        environment->SetByteArrayRegion(bytes, 0, static_cast<jsize>(size),
                                        reinterpret_cast<const jbyte*>(data));
        if (clear_android_exception(environment))
            throw EncodingError("Android CP932 byte copy failed");
    }
    auto string_class = environment->FindClass("java/lang/String");
    auto constructor = string_class
        ? environment->GetMethodID(string_class, "<init>", "([BLjava/lang/String;)V")
        : nullptr;
    auto charset = android_charset_name(environment, charset_name);
    if (!string_class || !constructor || clear_android_exception(environment))
        throw EncodingError("Android String charset constructor is unavailable");
    auto converted = static_cast<jstring>(
        environment->NewObject(string_class, constructor, bytes, charset));
    if (!converted || clear_android_exception(environment))
        throw EncodingError("invalid CP932 sequence");
    const auto length = environment->GetStringLength(converted);
    const auto* characters = environment->GetStringChars(converted, nullptr);
    if (!characters || clear_android_exception(environment))
        throw EncodingError("Android CP932 result access failed");
    String result;
    result.reserve(static_cast<std::size_t>(length) * 3U);
    bool invalid = false;
    for (jsize index = 0; index < length; ++index) {
        CodePoint codepoint = characters[index];
        if (codepoint == replacement) invalid = true;
        if (codepoint >= 0xd800U && codepoint <= 0xdbffU && index + 1 < length) {
            const CodePoint low = characters[index + 1];
            if (low >= 0xdc00U && low <= 0xdfffU) {
                codepoint = 0x10000U + ((codepoint - 0xd800U) << 10U) + low - 0xdc00U;
                ++index;
            } else {
                invalid = true;
                codepoint = replacement;
            }
        } else if (codepoint >= 0xd800U && codepoint <= 0xdfffU) {
            invalid = true;
            codepoint = replacement;
        }
        append_utf8(result, codepoint);
    }
    environment->ReleaseStringChars(converted, characters);
    if (invalid) throw EncodingError("invalid CP932 sequence");
    return result;
}

Bytes android_encode_charset(const String& text, const char* charset_name)
{
    const auto values = codepoints(text, ErrorPolicy::Strict);
    std::vector<jchar> characters;
    characters.reserve(values.size() * 2U);
    for (const auto codepoint : values) {
        if (codepoint <= 0xffffU) {
            characters.push_back(static_cast<jchar>(codepoint));
        } else {
            const auto value = codepoint - 0x10000U;
            characters.push_back(static_cast<jchar>(0xd800U + (value >> 10U)));
            characters.push_back(static_cast<jchar>(0xdc00U + (value & 0x3ffU)));
        }
    }
    auto* environment = android_jni_environment();
    Bytes result;
    {
        AndroidLocalFrame frame(environment);
        auto converted = environment->NewString(
            characters.empty() ? nullptr : characters.data(),
            static_cast<jsize>(characters.size()));
        auto string_class = environment->FindClass("java/lang/String");
        auto get_bytes = string_class
            ? environment->GetMethodID(string_class, "getBytes", "(Ljava/lang/String;)[B")
            : nullptr;
        auto charset = android_charset_name(environment, charset_name);
        if (!converted || !string_class || !get_bytes || clear_android_exception(environment))
            throw EncodingError("Android CP932 encoder is unavailable");
        auto bytes = static_cast<jbyteArray>(
            environment->CallObjectMethod(converted, get_bytes, charset));
        if (!bytes || clear_android_exception(environment))
            throw EncodingError("character is not representable in CP932");
        const auto length = environment->GetArrayLength(bytes);
        result.resize(static_cast<std::size_t>(length));
        if (length != 0) {
            environment->GetByteArrayRegion(bytes, 0, length,
                                            reinterpret_cast<jbyte*>(result.data()));
            if (clear_android_exception(environment))
                throw EncodingError("Android CP932 result copy failed");
        }
    }
    return result;
}

String cp932_piece(const std::uint8_t* data, std::size_t size)
{
    return android_decode_charset(data, size, "windows-31j");
}

Bytes cp932_wide_piece(const String& text)
{
    auto result = android_encode_charset(text, "windows-31j");
    if (cp932_piece(result.data(), result.size()) != text)
        throw EncodingError("character is not representable in CP932");
    return result;
}

#else
// SDL_iconvを共通の変換入口として使う。macOSではCMakeがシステム
// iconvを選択するため、CP932/Windows-31Jの変換表を利用できる。
String iconv_string(const char* to_code, const char* from_code, const char* data, std::size_t size)
{
    char* converted = SDL_iconv_string(to_code, from_code, data, size);
    if (!converted) throw EncodingError("iconv conversion failed");
    String result(converted); SDL_free(converted); return result;
}

String cp932_piece(const std::uint8_t* data, std::size_t size) { return iconv_string("UTF-8", "CP932", reinterpret_cast<const char*>(data), size); }
Bytes cp932_wide_piece(const String& text) { const auto converted = iconv_string("CP932", "UTF-8", text.data(), text.size()); return Bytes(converted.begin(), converted.end()); }
#endif

String decode_cp932_impl(const Bytes& bytes, ErrorPolicy policy)
{
    String result;
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto size = cp932_lead(bytes[offset]) && offset + 1 < bytes.size() ? 2U : 1U;
        try {
#if defined(_WIN32)
            result += wide_utf8(cp932_piece(bytes.data() + offset, static_cast<int>(size)));
#else
            result += cp932_piece(bytes.data() + offset, size);
#endif
            offset += size;
        } catch (const EncodingError&) {
            if (policy == ErrorPolicy::Strict) throw;
            if (policy == ErrorPolicy::Replace) append_utf8(result, replacement);
            ++offset;
        }
    }
    return result;
}

Bytes encode_cp932_impl(const String& text, ErrorPolicy policy)
{
    Bytes result;
    for (const auto cp : codepoints(text, policy)) {
        const auto piece = codepoints_to_utf8({cp});
        try {
#if defined(_WIN32)
            const auto wide = cp_to_wide(cp); const auto encoded = cp932_wide_piece(wide); result.insert(result.end(), encoded.begin(), encoded.end());
#else
            const auto encoded = cp932_wide_piece(piece); result.insert(result.end(), encoded.begin(), encoded.end());
#endif
        } catch (const EncodingError&) {
            if (policy == ErrorPolicy::Strict) throw;
            if (policy == ErrorPolicy::Replace) result.push_back('?');
        }
    }
    return result;
}


CodePoint japanese_decode_pair(const auto& map, unsigned first, unsigned second)
{
    if(first<0x21 || first>0x7e || second<0x21 || second>0x7e)throw EncodingError("invalid Japanese code unit");
    const auto cp=map[(first-0x21)*94+second-0x21];
    if(cp==0xffff)throw EncodingError("unassigned Japanese code unit");
    return cp;
}
String decode_japanese_impl(const Bytes& bytes, ErrorPolicy policy, bool shift_jis)
{
    String out;
    for(std::size_t position=0;position<bytes.size();) {
        const auto begin=position;const unsigned first=bytes[position];
        std::size_t error_size=1;
        try {
            CodePoint cp{};std::size_t length=1;
            if(first<0x80)cp=first;
            else if(shift_jis) {
                if(first>=0xa1 && first<=0xdf)cp=0xff61+first-0xa1;
                else {
                    if(!((first>=0x81 && first<=0x9f)||(first>=0xe0 && first<=0xea)))throw EncodingError("invalid Shift-JIS lead");
                    if(position+1>=bytes.size())throw EncodingError("incomplete Shift-JIS sequence");
                    const unsigned second=bytes[position+1];
                    if(second<0x40 || second==0x7f || second>0xfc)throw EncodingError("invalid Shift-JIS trail");
                    const auto row_base=first<0xe0?first-0x81:first-0xc1;
                    const auto column=second<0x80?second-0x40:second-0x41;
                    const auto row=2*row_base+(column<0x5e?0:1)+0x21;
                    const auto cell=(column<0x5e?column:column-0x5e)+0x21;
                    cp=(row==0x21 && cell==0x40)?0xff3c:japanese_decode_pair(japanese::jis0208,row,cell);length=2;
                }
            } else if(first==0x8e) {
                if(position+1>=bytes.size() || bytes[position+1]<0xa1 || bytes[position+1]>0xdf)throw EncodingError("invalid EUC-JP halfwidth sequence");
                cp=0xff61+bytes[position+1]-0xa1;length=2;
            } else if(first==0x8f) {
                if(position+2>=bytes.size()){
                    // CPython 3.8 の multibytecodec の MBERR_TOOFEW
                    // と同様、 残りの断片全体を一つの置換・エラーとして消費する。
                    error_size=bytes.size()-position;
                    throw EncodingError("incomplete EUC-JP plane2 sequence");
                }
                cp=japanese_decode_pair(japanese::jis0212,bytes[position+1]^0x80,bytes[position+2]^0x80);length=3;
            } else {
                if(position+1>=bytes.size())throw EncodingError("incomplete EUC-JP sequence");
                const auto second=bytes[position+1];
                cp=(first==0xa1 && second==0xc0)?0xff3c:japanese_decode_pair(japanese::jis0208,first^0x80,second^0x80);length=2;
            }
            append_utf8(out,cp);position+=length;
        } catch(const EncodingError&) {
            if(policy==ErrorPolicy::Strict)throw;
            if(policy==ErrorPolicy::Replace)append_utf8(out,replacement);
            position=begin+error_size;
        }
    }
    return out;
}
Bytes encode_japanese_impl(const String& text,ErrorPolicy policy,bool shift_jis)
{
    Bytes out;
    for(const auto cp:codepoints(text,policy)) {
        try {
            if(cp<0x80 || cp==0xa5 || cp==0x203e){out.push_back(static_cast<std::uint8_t>(cp==0xa5?0x5c:cp==0x203e?0x7e:cp));continue;}
            if(cp>=0xff61 && cp<=0xff9f){if(!shift_jis)out.push_back(0x8e);out.push_back(static_cast<std::uint8_t>(cp-0xff61+0xa1));continue;}
            auto found=std::lower_bound(japanese::encode_map.begin(),japanese::encode_map.end(),cp,[](const auto& row,auto code){return row.unicode<code;});
            unsigned jis=cp==0xff3c?0x2140:(found!=japanese::encode_map.end() && found->unicode==cp?found->jis:0xffff);
            if(jis==0xffff || (shift_jis && (jis&0x8000)))throw EncodingError("unrepresentable Japanese character");
            if(shift_jis){const auto row=jis>>8,cell=jis&0xff;const auto column=(((row-0x21)&1)?0x5e:0)+(cell-0x21);const auto lead=(row-0x21)>>1;out.push_back(static_cast<std::uint8_t>(lead<0x1f?lead+0x81:lead+0xc1));out.push_back(static_cast<std::uint8_t>(column<0x3f?column+0x40:column+0x41));}
            else {if(jis&0x8000)out.push_back(0x8f);out.push_back(static_cast<std::uint8_t>((jis>>8)|0x80));out.push_back(static_cast<std::uint8_t>((jis&0xff)|0x80));}
        } catch(const EncodingError&) {if(policy==ErrorPolicy::Strict)throw;if(policy==ErrorPolicy::Replace)out.push_back('?');}
    }
    return out;
}
String decode_euc_jp_impl(const Bytes& bytes, ErrorPolicy policy) {return decode_japanese_impl(bytes,policy,false);}
Bytes encode_euc_jp_impl(const String& text, ErrorPolicy policy) {return encode_japanese_impl(text,policy,false);}

bool range(CodePoint cp, CodePoint first, CodePoint last) noexcept { return cp >= first && cp <= last; }

bool wide_or_ambiguous(CodePoint cp) noexcept
{
    // Unicode East Asian Width のW/Fと、CardWirthが全角扱いするAの範囲。
    return range(cp, 0x1100, 0x115f) || range(cp, 0x2329, 0x232a) || range(cp, 0x2e80, 0xa4cf) ||
           range(cp, 0xac00, 0xd7a3) || range(cp, 0xf900, 0xfaff) || range(cp, 0xfe10, 0xfe19) ||
           range(cp, 0xfe30, 0xfe6f) || range(cp, 0xff00, 0xff60) || range(cp, 0xffe0, 0xffe6) ||
           range(cp, 0x1f000, 0x1faff) || range(cp, 0x20000, 0x3fffd) ||
           range(cp, 0x00a1, 0x00a1) || range(cp, 0x00a4, 0x00a4) || range(cp, 0x00a7, 0x00a8) ||
           range(cp, 0x00aa, 0x00aa) || range(cp, 0x00ad, 0x00ae) || range(cp, 0x00b0, 0x00b4) ||
           range(cp, 0x00b6, 0x00ba) || range(cp, 0x00bc, 0x00bf) || range(cp, 0x0391, 0x03c9) ||
           range(cp, 0x0401, 0x0451) || range(cp, 0x2010, 0x203b) || range(cp, 0x2100, 0x214f);
}

bool joiner(CodePoint cp) noexcept { return cp == 0x200dU || range(cp, 0x1f3fb, 0x1f3ff) || range(cp, 0xfe00, 0xfe0f); }

bool word_ascii(const String& s)
{
    if (s.empty()) return false;
    for (const auto cp : codepoints(s, ErrorPolicy::Ignore)) if (!((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || (cp >= '0' && cp <= '9') || cp == '_')) return false;
    return true;
}

bool contains_cluster(const String& chars, const String& cluster)
{
    if (chars.empty() || cluster.empty()) return false;
    std::size_t pos = 0;
    while (pos < chars.size()) {
        const auto end = chars.find('|', pos);
        if (chars.substr(pos, end == String::npos ? end : end - pos) == cluster) return true;
        if (end == String::npos) break; pos = end + 1;
    }
    return chars.find(cluster) != String::npos;
}

}  // namespace

String decode(const Bytes& bytes, TextEncoding encoding, DecodeOptions options)
{
    switch (encoding) {
    case TextEncoding::Utf8: return decode_utf8_bytes(bytes, options.errors, options.strip_bom);
    case TextEncoding::Utf8Bom: return decode_utf8_bytes(bytes, options.errors, true);
    case TextEncoding::Utf16: {
        if (bytes.size() >= 2 && bytes[0] == 0xff && bytes[1] == 0xfe) return decode_utf16_impl(bytes, ByteOrder::Little, options.errors, true);
        if (bytes.size() >= 2 && bytes[0] == 0xfe && bytes[1] == 0xff) return decode_utf16_impl(bytes, ByteOrder::Big, options.errors, true);
        // Python の str(bytes,"utf-16") と同様、BOM のないネイティブのリトルエンディアンを受け付ける。
        return decode_utf16_impl(bytes, ByteOrder::Little, options.errors, false);
    }
    case TextEncoding::Utf16LE: return decode_utf16_impl(bytes, ByteOrder::Little, options.errors, options.strip_bom);
    case TextEncoding::Utf16BE: return decode_utf16_impl(bytes, ByteOrder::Big, options.errors, options.strip_bom);
    case TextEncoding::Utf32: {
        if (bytes.size() >= 4 && bytes[0] == 0xff && bytes[1] == 0xfe && bytes[2] == 0 && bytes[3] == 0)
            return decode_utf32_impl(bytes, ByteOrder::Little, options.errors, true);
        if (bytes.size() >= 4 && bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 0xfe && bytes[3] == 0xff)
            return decode_utf32_impl(bytes, ByteOrder::Big, options.errors, true);
        // 参照元の Python はリトルエンディアンの環境で動くため、その順序を維持する。
        return decode_utf32_impl(bytes, ByteOrder::Little, options.errors, false);
    }
    case TextEncoding::Utf32LE: return decode_utf32_impl(bytes, ByteOrder::Little, options.errors, options.strip_bom);
    case TextEncoding::Utf32BE: return decode_utf32_impl(bytes, ByteOrder::Big, options.errors, options.strip_bom);
    case TextEncoding::Cp932: return decode_cp932_impl(bytes, options.errors);
    case TextEncoding::ShiftJis: return decode_japanese_impl(bytes,options.errors,true);
    case TextEncoding::EucJp: return decode_euc_jp_impl(bytes, options.errors);
    case TextEncoding::Cp437: return decode_cp437_impl(bytes);
    case TextEncoding::Latin1: return decode_latin1_impl(bytes);
    }
    throw EncodingError("unknown text encoding");
}

Bytes encode(const String& text, TextEncoding encoding, EncodeOptions options)
{
    switch (encoding) {
    case TextEncoding::Utf8: {
        auto result = encode_utf8_impl(text, options.errors);
        if (options.emit_bom) result.insert(result.begin(), {0xef, 0xbb, 0xbf});
        return result;
    }
    case TextEncoding::Utf8Bom: { auto result = encode_utf8_impl(text, options.errors); result.insert(result.begin(), {0xef, 0xbb, 0xbf}); return result; }
    case TextEncoding::Utf16: return encode_utf16_impl(text, ByteOrder::Little, options.errors, true);
    case TextEncoding::Utf16LE: return encode_utf16_impl(text, ByteOrder::Little, options.errors, options.emit_bom);
    case TextEncoding::Utf16BE: return encode_utf16_impl(text, ByteOrder::Big, options.errors, options.emit_bom);
    case TextEncoding::Utf32: return encode_utf32_impl(text, ByteOrder::Little, options.errors, true);
    case TextEncoding::Utf32LE: return encode_utf32_impl(text, ByteOrder::Little, options.errors, options.emit_bom);
    case TextEncoding::Utf32BE: return encode_utf32_impl(text, ByteOrder::Big, options.errors, options.emit_bom);
    case TextEncoding::Cp932: return encode_cp932_impl(text, options.errors);
    case TextEncoding::ShiftJis: return encode_japanese_impl(text,options.errors,true);
    case TextEncoding::EucJp: return encode_euc_jp_impl(text, options.errors);
    case TextEncoding::Cp437: return encode_cp437_impl(text, options.errors);
    case TextEncoding::Latin1: return encode_latin1_impl(text, options.errors);
    }
    throw EncodingError("unknown text encoding");
}

String decode_utf8(const Bytes& bytes, ErrorPolicy errors, bool strip_bom)
{
    return decode_utf8_bytes(bytes, errors, strip_bom);
}

Bytes encode_utf8(const String& text, ErrorPolicy errors)
{
    return encode_utf8_impl(text, errors);
}

String decode_system_text(const Bytes& bytes)
{
    if (bytes.empty()) return {};
#if defined(_WIN32)
    // CPython の通常の Windows テキストファイルは、システムの ANSI コ
    // ードページを使う。 UTF-8 モードに対応するネイティブの実行フラグはない。 OS
    // 差はここでの文字コード選択だけとし、INI 解析・改行の規則はこの基本処理の外で扱う。
    const auto page = GetACP();
    if (page == CP_UTF8) return decode_utf8(bytes, ErrorPolicy::Strict, false);
    if (page == 932) return cp932_to_utf8(bytes, ErrorPolicy::Strict);
    if (bytes.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
        throw EncodingError("default text input exceeds native codec range");
    const auto size = static_cast<int>(bytes.size());
    const auto data = reinterpret_cast<const char*>(bytes.data());
    const auto count = MultiByteToWideChar(page, MB_ERR_INVALID_CHARS, data, size, nullptr, 0);
    if (count <= 0) throw EncodingError("invalid system text encoding");
    std::wstring wide(static_cast<std::size_t>(count), L'\0');
    if (!MultiByteToWideChar(page, MB_ERR_INVALID_CHARS, data, size, wide.data(), count))
        throw EncodingError("system text conversion failed");
    return wide_utf8(wide);
#else
    // Android/Web と共通ホストは UTF-8 テキストを使う。 任意の旧 POSIX
    // ロケールや、Python のプロセス専用 UTF-8 モードは再現しない。
    return decode_utf8(bytes, ErrorPolicy::Strict, false);
#endif
}

String cp932_to_utf8(const Bytes& bytes, ErrorPolicy errors) { return decode_cp932_impl(bytes, errors); }
Bytes utf8_to_cp932(const String& text, ErrorPolicy errors) { return encode_cp932_impl(text, errors); }
String cp437_to_utf8(const Bytes& bytes, ErrorPolicy errors) { (void)errors; return decode_cp437_impl(bytes); }
Bytes utf8_to_cp437(const String& text, ErrorPolicy errors) { return encode_cp437_impl(text, errors); }
String latin1_to_utf8(const Bytes& bytes) { return decode_latin1_impl(bytes); }
Bytes utf8_to_latin1(const String& text, ErrorPolicy errors) { return encode_latin1_impl(text, errors); }
String utf16_to_utf8(const Bytes& bytes, ByteOrder order, ErrorPolicy errors, bool strip_bom) { return decode_utf16_impl(bytes, order, errors, strip_bom); }
Bytes utf8_to_utf16(const String& text, ByteOrder order, ErrorPolicy errors, bool emit_bom) { return encode_utf16_impl(text, order, errors, emit_bom); }
String utf32_to_utf8(const Bytes& bytes, ByteOrder order, ErrorPolicy errors, bool strip_bom) { return decode_utf32_impl(bytes, order, errors, strip_bom); }
Bytes utf8_to_utf32(const String& text, ByteOrder order, ErrorPolicy errors, bool emit_bom) { return encode_utf32_impl(text, order, errors, emit_bom); }
String euc_jp_to_utf8(const Bytes& bytes, ErrorPolicy errors) { return decode_euc_jp_impl(bytes, errors); }
Bytes utf8_to_euc_jp(const String& text, ErrorPolicy errors) { return encode_euc_jp_impl(text, errors); }

namespace {
bool has_utf32_bom(const Bytes& bytes)
{
    return bytes.size() >= 4 &&
        ((bytes[0] == 0xff && bytes[1] == 0xfe && bytes[2] == 0 && bytes[3] == 0) ||
         (bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 0xfe && bytes[3] == 0xff));
}

String decode_candidate_chain(const Bytes& bytes, bool archive_order)
{
    // 元の decode_text/decode_zipname の不具合を修正する。UTF32LE
    // の BOM は UTF16LE の印でも始まるため、 Python の順番どおりの代替処理
    // では UTF16 と認識して NUL が入る。既存の C++ の BOM 補正を維持する。
    if (has_utf32_bom(bytes)) return decode(bytes, TextEncoding::Utf32);
    const auto attempt=[&](TextEncoding codec)->std::optional<String>{try{return decode(bytes,codec);}catch(const EncodingError&){return {};}};
    if(auto text=attempt(TextEncoding::Utf8Bom))return *text;
    if(!archive_order) {
        if(auto text=attempt(TextEncoding::Utf8))return *text;
        if(auto text=attempt(TextEncoding::ShiftJis))return *text;
    } else {
        if(auto text=attempt(TextEncoding::Cp932))return *text;
        if(auto text=attempt(TextEncoding::EucJp))return *text;
        if(auto text=attempt(TextEncoding::Utf8))return *text;
    }
    if(auto text=attempt(TextEncoding::Utf16))return *text;
    if(auto text=attempt(TextEncoding::Utf32))return *text;
    // Py の Windows の mbcs はこの位置で端末の ACP を
    // 試すが、旧日本語 CardWirth のバイト列には全ホストで明示的に
    // CP932 を使う。 別のロケール・OS へ移動しただけで、同じ保
    // 存テキストを別の欧文 ACP として解釈しないためである。 日本語以
    // 外の Windows ACP のバイト列では、意図的に動作が異なる。
    if(!archive_order) {
        if(auto text=attempt(TextEncoding::Cp932))return *text;
        if(auto text=attempt(TextEncoding::EucJp))return *text;
    }
    return decode(bytes,TextEncoding::Cp437,{ErrorPolicy::Replace,true});
}
} // namespace

String decode_legacy_text(const Bytes& bytes) { return decode_candidate_chain(bytes, false); }
String decode_legacy_archive_name(const Bytes& bytes) { return decode_candidate_chain(bytes, true); }

std::filesystem::path path_from_utf8(const String& text, ErrorPolicy errors)
{
    const auto values = codepoints(text, errors);
#if defined(_WIN32)
    std::wstring native;
    for (const auto cp : values) native += cp_to_wide(cp);
    return std::filesystem::path(native);
#else
    const auto valid = codepoints_to_utf8(values);
    return std::filesystem::u8path(valid);
#endif
}

String path_to_utf8(const std::filesystem::path& path, ErrorPolicy errors)
{
#if defined(_WIN32)
    // プログラムへ渡すパス文字列は共通の区切り文字を使う。 Windows
    // のネイティブ表現は、ファイルシステム・Win32 API が std::filesystem::path
    // 自体を使う時にだけ作る。
    const auto generic = path.generic_wstring();
    std::vector<CodePoint> values;
    for (std::size_t i = 0; i < generic.size();) {
        const auto first = static_cast<std::uint16_t>(generic[i++]);
        if (first >= 0xd800U && first <= 0xdbffU) {
            if (i < generic.size()) {
                const auto second = static_cast<std::uint16_t>(generic[i]);
                if (second >= 0xdc00U && second <= 0xdfffU) {
                    ++i;
                    values.push_back(0x10000U + ((first - 0xd800U) << 10U) + (second - 0xdc00U));
                    continue;
                }
            }
            if (errors == ErrorPolicy::Strict) throw EncodingError("filesystem path contains an unpaired UTF-16 surrogate");
            if (errors == ErrorPolicy::Replace) values.push_back(replacement);
            continue;
        }
        if (first >= 0xdc00U && first <= 0xdfffU) {
            if (errors == ErrorPolicy::Strict) throw EncodingError("filesystem path contains an unpaired UTF-16 surrogate");
            if (errors == ErrorPolicy::Replace) values.push_back(replacement);
            continue;
        }
        values.push_back(first);
    }
    return codepoints_to_utf8(values);
#else
    const auto generic = path.generic_u8string();
    const String utf8(reinterpret_cast<const char*>(generic.data()), generic.size());
    return codepoints_to_utf8(codepoints(utf8, errors));
#endif
}

template<class Mappings, class Expansions>
void append_case_mapping(String& output, CodePoint cp,
                         const Mappings& mappings, const Expansions& expansions)
{
            const auto expansion = std::lower_bound(expansions.begin(), expansions.end(), cp,
                [](const auto& entry, CodePoint value) { return entry.source < value; });
            if (expansion != expansions.end() && expansion->source == cp) {
                for (std::uint32_t k = 0; k < expansion->count; ++k) append_utf8(output, expansion->values[k]);
            } else {
                const auto mapping = std::lower_bound(mappings.begin(), mappings.end(), cp,
                    [](const auto& entry, CodePoint value) { return entry.last < value; });
                const bool mapped = mapping != mappings.end() && mapping->first <= cp &&
                    (cp - mapping->first) % mapping->step == 0;
                append_utf8(output, mapped ? static_cast<CodePoint>(static_cast<std::int64_t>(cp) + mapping->delta) : cp);
            }
}

template<class Mappings,class Expansions,class CasedRanges,class IgnorableRanges>
String lower_impl(const String& text,const Mappings& mappings,const Expansions& expansions,const CasedRanges& cased,const IgnorableRanges& ignorable)
{
    const auto input = codepoints(text, ErrorPolicy::Replace);
    const auto contains = [](const auto& ranges, CodePoint cp) {
        const auto found = std::lower_bound(ranges.begin(), ranges.end(), cp,
            [](const auto& range, CodePoint value) { return range.last < value; });
        return found != ranges.end() && found->first <= cp;
    };
    // 文脈は小文字化済みの結果ではなく元の文字列で評価する。 Case_Ignorable
    // と Cased が重なる場合は前者を優先する。 先読み
    // を一度だけ計算し、長い結合文字列でも処理量を線形に保つ。
    std::vector<bool> cased_after(input.size() + 1, false);
    bool next_cased = false;
    for (std::size_t i = input.size(); i > 0; --i) {
        const auto cp = input[i - 1];
        if (!contains(ignorable, cp)) next_cased = contains(cased, cp);
        cased_after[i - 1] = next_cased;
    }
    String output;
    output.reserve(text.size());
    bool previous_cased = false;
    for (std::size_t i = 0; i < input.size(); ++i) {
        const auto cp = input[i];
        if (cp == 0x03a3U && previous_cased && !cased_after[i + 1]) {
            append_utf8(output, 0x03c2U); // Unicode の語末シグマ規則。
        } else {
            append_case_mapping(output, cp, mappings, expansions);
        }
        if (!contains(ignorable, cp)) previous_cased = contains(cased, cp);
    }
    return output;
}

String lower(const String& text)
{
    namespace data=unicode_lower_data;
    return lower_impl(text,data::mappings,data::expansions,data::cased,data::ignorable);
}

String python_lower(const String& text)
{
    namespace data=python_lower_data;
    return lower_impl(text,data::mappings,data::expansions,data::cased,data::ignorable);
}

String python_upper(const String& text)
{
    namespace data = python_upper_data;
    String output;
    output.reserve(text.size());
    for (const auto cp : codepoints(text, ErrorPolicy::Replace))
        append_case_mapping(output, cp, data::mappings, data::expansions);
    return output;
}

String casefold(const String& text)
{
    std::vector<CodePoint> values;
    for (auto cp : codepoints(text, ErrorPolicy::Replace)) {
        // Python の casefold で検索キーに影響する、複数文字への変換と互換形。
        if (cp == 0x00dfU || cp == 0x1e9eU) { values.push_back('s'); values.push_back('s'); continue; }
        if (cp == 0x0130U) { values.push_back('i'); values.push_back(0x0307U); continue; }
        if (cp == 0x0149U) { values.push_back(0x02bcU); values.push_back('n'); continue; }
        if (cp == 0x0587U) { values.push_back(0x0565U); values.push_back(0x0582U); continue; }
        if (cp == 0xfb00U) { values.push_back('f'); values.push_back('f'); continue; }
        if (cp == 0xfb01U) { values.push_back('f'); values.push_back('i'); continue; }
        if (cp == 0xfb02U) { values.push_back('f'); values.push_back('l'); continue; }
        if (cp == 0xfb03U || cp == 0xfb04U) { values.push_back('f'); values.push_back('f'); values.push_back(cp == 0xfb03U ? 'i' : 'l'); continue; }
        if (cp == 0xfb05U || cp == 0xfb06U) { values.push_back('s'); values.push_back('t'); continue; }
        if (cp >= 'A' && cp <= 'Z') cp += 'a' - 'A';
        else if (cp >= 0xff21U && cp <= 0xff3aU) cp += 0xff41U - 0xff21U;
        else if (cp >= 0xc0U && cp <= 0xdeU && cp != 0xd7U) cp += 0x20U;
        else if (cp >= 0x391U && cp <= 0x3abU && cp != 0x3a2U) cp += 0x20U;
        else if (cp == 0x03c2U) cp = 0x03c3U;
        else if (cp >= 0x410U && cp <= 0x42fU) cp += 0x20U;
        else if (cp == 0x0401U) cp = 0x0451U;
        values.push_back(cp);
    }
    return codepoints_to_utf8(values);
}

String collation_key(const String& text)
{
    // locale.strxfrm() は端末のロケールで結果が変
    // わり、シナリオ順序を再現できなくするため、意図的に呼ばない。
    return casefold(text);
}

CharacterClass character_class(const String& character) noexcept
{
    try {
        const auto values = codepoints(character, ErrorPolicy::Strict); if (values.empty()) return CharacterClass::Unknown;
        const auto cp = values.front();
        if ((cp >= '0' && cp <= '9') || range(cp, 0xff10U, 0xff19U)) return CharacterClass::Number;
        if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') || range(cp, 0xff21U, 0xff3aU) || range(cp, 0xff41U, 0xff5aU)) return CharacterClass::Latin;
        if (range(cp, 0x3040U, 0x309fU)) return CharacterClass::Hiragana;
        if (range(cp, 0x30a0U, 0x30ffU) || range(cp, 0x31f0U, 0x31ffU)) return CharacterClass::Katakana;
        if (range(cp, 0xff61U, 0xff9fU)) return CharacterClass::HalfWidthKatakana;
        if (range(cp, 0x370U, 0x3ffU)) return CharacterClass::Greek;
        if (range(cp, 0x400U, 0x52fU)) return CharacterClass::Cyrillic;
        if (range(cp, 0x3400U, 0x4dbfU) || range(cp, 0x4e00U, 0x9fffU) || range(cp, 0xf900U, 0xfaffU) || range(cp, 0x20000U, 0x2ffffU)) return CharacterClass::CjkIdeograph;
        if ((cp <= 0x7fU && !((cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z'))) ||
            wide_or_ambiguous(cp) || cp < 0x20U || range(cp, 0x2000U, 0x2bffU)) return CharacterClass::Symbol;
        return CharacterClass::Unknown;
    } catch (...) { return CharacterClass::Unknown; }
}

String encodewrap(const String& text)
{
    String result;
    for (const auto c : text) {
        if (c == '\\') result += "\\\\";
        else if (c == '\n') result += "\\n";
        else if (c != '\r') result.push_back(c);
    }
    return result;
}

String decodewrap(const String& text, const String& newline)
{
    String result; bool escaped = false;
    for (const auto c : text) {
        if (escaped) { if (c == 'n') result += newline; else result.push_back(c); escaped = false; }
        else if (c == '\\') escaped = true;
        else result.push_back(c);
    }
    return result; // Python と同様、末尾の単独の逆スラッシュは意図的に捨てる。
}

String encodetextlist(const std::vector<String>& lines)
{
    String joined;
    for (std::size_t i = 0; i < lines.size(); ++i) { if (i) joined.push_back('\n'); joined += lines[i]; }
    return encodewrap(joined);
}

std::vector<String> decodetextlist(const String& text)
{
    if (text.empty()) return {};
    const auto decoded = decodewrap(text); std::vector<String> result; std::size_t begin = 0;
    while (begin <= decoded.size()) { const auto end = decoded.find('\n', begin); if (end == String::npos) { result.push_back(decoded.substr(begin)); break; } result.push_back(decoded.substr(begin, end - begin)); begin = end + 1; }
    return result;
}

std::vector<String> splitlines(const String& text)
{
    std::vector<String> result;
    std::size_t line_start = 0;
    std::size_t offset = 0;
    while (offset < text.size()) {
        const auto unit = peek_utf8(text, offset);
        if (!unit.valid || !is_line_break(unit.codepoint)) {
            offset += unit.size ? unit.size : 1;
            continue;
        }
        result.push_back(text.substr(line_start, offset - line_start));
        offset += unit.size;
        if (unit.codepoint == '\r' && offset < text.size()) {
            const auto next = peek_utf8(text, offset);
            if (next.valid && next.codepoint == '\n') offset += next.size;
        }
        line_start = offset;
    }
    if (line_start < text.size()) result.push_back(text.substr(line_start));
    return result;
}

bool is_combining(CodePoint cp) noexcept
{
    return range(cp, 0x0300, 0x036f) || range(cp, 0x0483, 0x0489) || range(cp, 0x0591, 0x05bd) ||
           range(cp, 0x05bf, 0x05c7) || range(cp, 0x0610, 0x061a) || range(cp, 0x064b, 0x065f) ||
           range(cp, 0x0670, 0x0670) || range(cp, 0x06d6, 0x06ed) || range(cp, 0x1ab0, 0x1aff) ||
           range(cp, 0x1dc0, 0x1dff) || range(cp, 0x20d0, 0x20ff) || range(cp, 0xfe20, 0xfe2f) ||
           range(cp, 0xfe00, 0xfe0f) || range(cp, 0x1f3fb, 0x1f3ff);
}

int codepoint_width(CodePoint cp) noexcept
{
    if (is_combining(cp) || cp == 0x200dU || cp == 0x200bU) return 0;
    return wide_or_ambiguous(cp) ? 2 : 1;
}

bool is_half_width(const String& character) noexcept
{
    try { const auto values = codepoints(character, ErrorPolicy::Strict); return values.size() == 1 && codepoint_width(values.front()) == 1; }
    catch (...) { return false; }
}

std::vector<String> graphemes(const String& text)
{
    std::vector<String> result; std::size_t offset = 0; bool previous_zwj = false; int regional_count = 0;
    while (offset < text.size()) {
        const auto begin = offset; const auto unit = peek_utf8(text, offset);
        if (!unit.valid) { offset += 1; result.push_back("\xef\xbf\xbd"); previous_zwj = false; regional_count = 0; continue; }
        offset += unit.size;
        const String piece = text.substr(begin, unit.size);
        const bool regional = range(unit.codepoint, 0x1f1e6, 0x1f1ff);
        const bool crlf = unit.codepoint == 0x0aU && !result.empty() && result.back() == "\r";
        const bool emoji_tag = range(unit.codepoint, 0xe0020U, 0xe007fU);
        const bool keycap = unit.codepoint == 0x20e3U;
        if (!result.empty() && (crlf || is_combining(unit.codepoint) || joiner(unit.codepoint) || emoji_tag || keycap || previous_zwj || (regional && regional_count == 1))) result.back() += piece;
        else result.push_back(piece);
        previous_zwj = unit.codepoint == 0x200dU; regional_count = regional ? regional_count + 1 : 0;
    }
    std::vector<String> normalized;
    for (auto& cluster : result) {
        const auto cps = codepoints(cluster, ErrorPolicy::Replace);
        if (cps.size() > 1 && (cps.back() == 0xff9eU || cps.back() == 0xff9fU)) {
            const auto pos = cluster.size() - 3; normalized.push_back(cluster.substr(0, pos)); normalized.push_back(cluster.substr(pos));
        } else normalized.push_back(std::move(cluster));
    }
    return normalized;
}

int text_width(const String& text)
{
    int width = 0;
    for (const auto cp : codepoints(text, ErrorPolicy::Ignore)) width += codepoint_width(cp);
    return width;
}

std::tuple<String, String> slice_str(const String& text, int width)
{
    if (width <= 0) return {String{}, text};
    String left, right;
    int used = 0;
    bool cut = false;
    for (const auto& cluster : graphemes(text)) { const auto values = codepoints(cluster, ErrorPolicy::Ignore); const int cw = values.empty() ? 0 : codepoint_width(values.front()); if (!cut && used + cw <= width) { left += cluster; used += cw; } else { cut = true; right += cluster; } }
    return {std::move(left), std::move(right)};
}

String rjustify(const String& text, int width, const String& fill)
{
    String result = text; if (fill.empty()) return result; for (int n = std::max(0, width - text_width(text)); n-- > 0;) result += fill; return result;
}

String ljustify(const String& text, int width, const String& fill)
{
    if (fill.empty()) return text; String result; for (int n = std::max(0, width - text_width(text)); n-- > 0;) result += fill; return result + text;
}

String default_wrap_chars() { return "｡|､|，|、|。|．|）|」|』|〕|｝|】"; }
String default_open_chars() { return "\"'(<[`{‘“〈《≪「『【〔（＜［｛｢"; }
String default_close_chars() { return "!\"'),.:;>?]`}゜’”′″、。々＞》≫」』】〕〟゛°ゝゞヽヾ〻！），．：；＞？］｝｡｣､ﾞﾟぁぃぅぇぉァィゥェォｧｨｩｪｫヵっッｯゃゅょャュョｬｭｮゎヮㇵㇶㇷㇸㇹㇺ…―ーｰ"; }

String txtwrap(const String& source, int mode, int width, const String& wrap_chars, bool encoded_text, std::set<std::size_t>* special_positions)
{
    String text = encoded_text ? decodewrap(source) : source;
    switch (mode) {
    case 1: case 4: width = 37; /* Python と同様、これらのモードでは禁則文字集合を固定する。 */ break;
    case 2: width = 32; break;
    case 3: width = 42; break;
    case 5: width = 24; break;
    case 6: width = 48; break;
    default: if (width <= 0) width = 30; break;
    }
    String effective_wrap_chars;
    if (mode == 1 || mode == 4 || mode == 5 || mode == 6) {
        // これらのモードは、呼び出し側の値に関係なく util.py の WRAPS_CHARS を使う。
        effective_wrap_chars = default_wrap_chars();
    } else if (mode != 2 && mode != 3) {
        effective_wrap_chars = wrap_chars;
    }
    if (width <= 0) return {};
    const auto input_specials = special_positions ? *special_positions : std::set<std::size_t>{};
    std::set<std::size_t> output_specials;
    const auto chars = effective_wrap_chars; const bool special_mode = mode == 2 || mode == 3; String result; int used = 0; std::size_t index = 0; std::size_t result_index = 0; bool marker = false;
    for (const auto& cluster : graphemes(text)) {
        if (cluster == "\n") { result.push_back('\n'); used = 0; ++index; ++result_index; marker = false; continue; }
        const auto cp = codepoints(cluster, ErrorPolicy::Ignore);
        const bool special = special_mode && input_specials.find(index) != input_specials.end() &&
                             !cp.empty() && (cp.front() == '#' || cp.front() == '&');
        if (special) output_specials.insert(result_index);
        const int cw = marker || special ? 0 : (cp.empty() ? 0 : codepoint_width(cp.front()));
        if (used > 0 && used + cw > width) {
            if (contains_cluster(chars, cluster)) {
                result += cluster;
                result.push_back('\n');
                result_index += cp.size();
                output_specials.insert(result_index++);
                used = 0;
                marker = false;
                index += cp.size();
                continue;
            }
            result.push_back('\n'); output_specials.insert(result_index++); used = 0;
        }
        result += cluster; used += cw; marker = special && cp.size() == 1; index += cp.size(); result_index += cp.size();
    }
    while (!result.empty() && (result.back() == ' ' || result.back() == '\t' || result.back() == '\n')) result.pop_back();
    if (special_positions) *special_positions = std::move(output_specials);
    return result;
}

String wordwrap(const String& source, int width, const String& open_chars, const String& close_chars, std::set<std::size_t>* special_positions)
{
    if (width <= 0) return source;
    const auto opens = open_chars.empty() ? default_open_chars() : open_chars;
    const auto closes = close_chars.empty() ? default_close_chars() : close_chars;
    const auto input_specials = special_positions ? *special_positions : std::set<std::size_t>{};
    struct Token { String text; int width{}; bool special{}; bool opening{}; bool closing{}; bool word{}; };
    std::vector<std::vector<Token>> output_lines;
    std::set<std::size_t> wrap_boundaries;
    std::size_t source_index = 0;
    std::size_t byte_begin = 0;
    while (byte_begin <= source.size()) {
        const auto byte_end = source.find('\n', byte_begin);
        const String line = byte_end == String::npos ? source.substr(byte_begin) : source.substr(byte_begin, byte_end - byte_begin);
        const auto clusters = graphemes(line);
        std::vector<Token> tokens;
        for (std::size_t i = 0; i < clusters.size();) {
            const auto cps = codepoints(clusters[i], ErrorPolicy::Ignore);
            const CodePoint cp = cps.empty() ? 0 : cps.front();
            const bool special = input_specials.find(source_index) != input_specials.end();
            if (special && i + 1 < clusters.size()) {
                Token token; token.text = clusters[i] + clusters[i + 1]; token.special = true;
                token.width = cp == '&' ? 0 : text_width(token.text);
                tokens.push_back(std::move(token));
                source_index += cps.size() + codepoints(clusters[i + 1], ErrorPolicy::Ignore).size();
                i += 2;
                continue;
            }
            const bool ascii_word = (cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') ||
                                    (cp >= 'a' && cp <= 'z') || cp == '_';
            const bool full_word = range(cp, 0xff10U, 0xff19U) || range(cp, 0xff21U, 0xff3aU) ||
                                   range(cp, 0xff41U, 0xff5aU) || cp == 0xff3fU;
            Token token; token.text = clusters[i]; token.word = ascii_word || full_word;
            token.width = text_width(token.text);
            token.opening = contains_cluster(opens, clusters[i]);
            token.closing = contains_cluster(closes, clusters[i]);
            source_index += cps.size();
            ++i;
            while (token.word && i < clusters.size()) {
                const auto next = codepoints(clusters[i], ErrorPolicy::Ignore);
                const CodePoint ncp = next.empty() ? 0 : next.front();
                const bool same = ascii_word
                    ? ((ncp >= '0' && ncp <= '9') || (ncp >= 'A' && ncp <= 'Z') || (ncp >= 'a' && ncp <= 'z') || ncp == '_')
                    : (range(ncp, 0xff10U, 0xff19U) || range(ncp, 0xff21U, 0xff3aU) || range(ncp, 0xff41U, 0xff5aU) || ncp == 0xff3fU);
                if (!same || input_specials.find(source_index) != input_specials.end()) break;
                token.text += clusters[i]; token.width += text_width(clusters[i]); source_index += next.size(); ++i;
            }
            tokens.push_back(std::move(token));
        }

        std::vector<Token> current;
        int used = 0;
        const auto flush = [&]() {
            output_lines.push_back(std::move(current));
            wrap_boundaries.insert(output_lines.size() - 1);
            current.clear();
            used = 0;
        };
        for (auto token : tokens) {
            if (!current.empty() && used + token.width > width) {
                if (token.closing && used <= width) {
                    current.push_back(std::move(token));
                    used += current.back().width;
                    continue;
                }
                if (token.closing) {
                    std::vector<Token> trailing_closes;
                    while (!current.empty() && current.back().closing && !current.back().special) {
                        used -= current.back().width;
                        trailing_closes.insert(trailing_closes.begin(), std::move(current.back()));
                        current.pop_back();
                    }
                    if (!current.empty()) flush();
                    current = std::move(trailing_closes);
                    used = 0; for (const auto& item : current) used += item.width;
                    used += token.width; current.push_back(std::move(token));
                    continue;
                }
                std::vector<Token> trailing;
                while (!current.empty() && current.back().opening && !current.back().special) {
                    used -= current.back().width;
                    trailing.insert(trailing.begin(), std::move(current.back()));
                    current.pop_back();
                }
                if (current.empty() && !trailing.empty()) {
                    current = std::move(trailing);
                    used = 0; for (const auto& item : current) used += item.width;
                    flush();
                } else {
                    if (!current.empty()) flush();
                    current = std::move(trailing);
                    used = 0; for (const auto& item : current) used += item.width;
                }
            }
            while (token.word && !token.special && used + token.width > width) {
                const int available = std::max(1, width - used);
                auto [left, right] = slice_str(token.text, available);
                if (left.empty()) { if (!current.empty()) flush(); else break; continue; }
                Token part{left + (right.empty() ? String{} : String("-")), text_width(left) + (right.empty() ? 0 : 1), false, false, false, true};
                current.push_back(std::move(part));
                if (!right.empty()) flush();
                token.text = std::move(right); token.width = text_width(token.text);
                if (token.text.empty()) break;
            }
            if (!token.text.empty()) { used += token.width; current.push_back(std::move(token)); }
        }
        if (!current.empty() || tokens.empty()) output_lines.push_back(std::move(current));
        if (byte_end == String::npos) break;
        ++source_index;
        byte_begin = byte_end + 1;
    }

    String result;
    std::set<std::size_t> output_specials;
    std::size_t result_index = 0;
    for (std::size_t i = 0; i < output_lines.size(); ++i) {
        if (i) {
            result.push_back('\n');
            if (wrap_boundaries.find(i - 1) != wrap_boundaries.end()) output_specials.insert(result_index);
            ++result_index;
        }
        for (const auto& token : output_lines[i]) {
            if (token.special) output_specials.insert(result_index);
            result += token.text;
            result_index += codepoints(token.text, ErrorPolicy::Ignore).size();
        }
    }
    if (special_positions) *special_positions = std::move(output_specials);
    return result;
}

std::uint8_t read_u8(const Bytes& data, std::size_t offset) { require_range(data, offset, 1); return data[offset]; }
std::int8_t read_i8(const Bytes& data, std::size_t offset) { return static_cast<std::int8_t>(read_u8(data, offset)); }
std::uint16_t read_u16(const Bytes& data, std::size_t offset, ByteOrder order) { require_range(data, offset, 2); return order == ByteOrder::Little ? static_cast<std::uint16_t>(data[offset] | (data[offset + 1] << 8)) : static_cast<std::uint16_t>((data[offset] << 8) | data[offset + 1]); }
std::int16_t read_i16(const Bytes& data, std::size_t offset, ByteOrder order) { return static_cast<std::int16_t>(read_u16(data, offset, order)); }
std::uint32_t read_u32(const Bytes& data, std::size_t offset, ByteOrder order) { require_range(data, offset, 4); std::uint32_t v = 0; if (order == ByteOrder::Little) for (int i = 3; i >= 0; --i) v = (v << 8) | data[offset + i]; else for (int i = 0; i < 4; ++i) v = (v << 8) | data[offset + i]; return v; }
std::int32_t read_i32(const Bytes& data, std::size_t offset, ByteOrder order) { return static_cast<std::int32_t>(read_u32(data, offset, order)); }
std::uint64_t read_u64(const Bytes& data, std::size_t offset, ByteOrder order) { require_range(data, offset, 8); std::uint64_t v = 0; if (order == ByteOrder::Little) for (int i = 7; i >= 0; --i) v = (v << 8) | data[offset + i]; else for (int i = 0; i < 8; ++i) v = (v << 8) | data[offset + i]; return v; }
std::int64_t read_i64(const Bytes& data, std::size_t offset, ByteOrder order) { return static_cast<std::int64_t>(read_u64(data, offset, order)); }

void append_u8(Bytes& data, std::uint8_t value) { data.push_back(value); }
void append_i8(Bytes& data, std::int8_t value) { append_u8(data, static_cast<std::uint8_t>(value)); }
void append_u16(Bytes& data, std::uint16_t value, ByteOrder order) { if (order == ByteOrder::Little) { data.push_back(static_cast<std::uint8_t>(value)); data.push_back(static_cast<std::uint8_t>(value >> 8)); } else { data.push_back(static_cast<std::uint8_t>(value >> 8)); data.push_back(static_cast<std::uint8_t>(value)); } }
void append_i16(Bytes& data, std::int16_t value, ByteOrder order) { append_u16(data, static_cast<std::uint16_t>(value), order); }
void append_u32(Bytes& data, std::uint32_t value, ByteOrder order) { for (int i = 0; i < 4; ++i) { const int shift = order == ByteOrder::Little ? i * 8 : (3 - i) * 8; data.push_back(static_cast<std::uint8_t>(value >> shift)); } }
void append_i32(Bytes& data, std::int32_t value, ByteOrder order) { append_u32(data, static_cast<std::uint32_t>(value), order); }
void append_u64(Bytes& data, std::uint64_t value, ByteOrder order) { for (int i = 0; i < 8; ++i) { const int shift = order == ByteOrder::Little ? i * 8 : (7 - i) * 8; data.push_back(static_cast<std::uint8_t>(value >> shift)); } }
void append_i64(Bytes& data, std::int64_t value, ByteOrder order) { append_u64(data, static_cast<std::uint64_t>(value), order); }

template<class T, class Append>
void store(Bytes& data, std::size_t offset, T value, Append append)
{
    Bytes encoded; append(encoded, value); if (offset > data.size() || encoded.size() > std::numeric_limits<std::size_t>::max() - offset) throw EncodingError("integer write out of range"); if (data.size() < offset + encoded.size()) data.resize(offset + encoded.size()); std::copy(encoded.begin(), encoded.end(), data.begin() + static_cast<std::ptrdiff_t>(offset));
}

void store_u16(Bytes& data, std::size_t offset, std::uint16_t value, ByteOrder order) { store(data, offset, value, [order](Bytes& b, auto v) { append_u16(b, v, order); }); }
void store_i16(Bytes& data, std::size_t offset, std::int16_t value, ByteOrder order) { store(data, offset, value, [order](Bytes& b, auto v) { append_i16(b, v, order); }); }
void store_u32(Bytes& data, std::size_t offset, std::uint32_t value, ByteOrder order) { store(data, offset, value, [order](Bytes& b, auto v) { append_u32(b, v, order); }); }
void store_i32(Bytes& data, std::size_t offset, std::int32_t value, ByteOrder order) { store(data, offset, value, [order](Bytes& b, auto v) { append_i32(b, v, order); }); }
void store_u64(Bytes& data, std::size_t offset, std::uint64_t value, ByteOrder order) { store(data, offset, value, [order](Bytes& b, auto v) { append_u64(b, v, order); }); }
void store_i64(Bytes& data, std::size_t offset, std::int64_t value, ByteOrder order) { store(data, offset, value, [order](Bytes& b, auto v) { append_i64(b, v, order); }); }

}  // namespace cw::lib::encoding
