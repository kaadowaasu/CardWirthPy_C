#include "cw/lib/simd_image.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

#if defined(CARDWIRTH_SIMD_IMAGE_FORCE_SCALAR)
#elif defined(_M_X64) || defined(__x86_64__) || \
    (defined(_M_IX86_FP) && _M_IX86_FP >= 2) || defined(__SSE2__)
#define CARDWIRTH_SIMD_IMAGE_SSE2 1
#include <emmintrin.h>
#elif defined(_M_ARM64) || defined(__aarch64__)
#define CARDWIRTH_SIMD_IMAGE_NEON 1
#include <arm_neon.h>
#endif

namespace cw::lib::simd_image {
namespace {

constexpr std::uint32_t packed_rgba(std::uint8_t red, std::uint8_t green,
                                    std::uint8_t blue, std::uint8_t alpha) noexcept
{
    return static_cast<std::uint32_t>(red) |
        (static_cast<std::uint32_t>(green) << 8U) |
        (static_cast<std::uint32_t>(blue) << 16U) |
        (static_cast<std::uint32_t>(alpha) << 24U);
}

inline void alpha_blend_pixel(const std::uint8_t* source,
                              std::uint8_t* destination, bool rgb_destination = false) noexcept
{
    const unsigned alpha = source[3];
    if (rgb_destination) {
        for (unsigned channel=0;channel<3;++channel)
            destination[channel]=alpha==255 ? source[channel] : static_cast<std::uint8_t>(
                (source[channel]*alpha+destination[channel]*(256U-alpha))>>8U);
        return;
    }
    // pygame 1.9.6 の ALPHA_BLEND と同様、透明な出力先には
    // 通常の RGB を保持する。 ここでアルファを掛けると後の転送で暗くなる。空で
    // ない出力先には、256 を基準にした RGB 式と別に丸めたアルファ式を使う。
    if (!destination[3]) { std::memcpy(destination, source, 4); return; }
    const unsigned inverse = 256U - alpha;
    destination[0] = static_cast<std::uint8_t>(
        (source[0] * (alpha + 1U) + destination[0] * inverse) >> 8U);
    destination[1] = static_cast<std::uint8_t>(
        (source[1] * (alpha + 1U) + destination[1] * inverse) >> 8U);
    destination[2] = static_cast<std::uint8_t>(
        (source[2] * (alpha + 1U) + destination[2] * inverse) >> 8U);
    destination[3] = static_cast<std::uint8_t>(
        alpha + destination[3] - alpha * destination[3] / 255U);
}

#if defined(CARDWIRTH_SIMD_IMAGE_SSE2)
inline __m128i div255_u16(__m128i value) noexcept
{
    const auto one = _mm_set1_epi16(1);
    return _mm_srli_epi16(
        _mm_add_epi16(_mm_add_epi16(value, one), _mm_srli_epi16(value, 8)), 8);
}

inline __m128i replicate_alpha_u16(__m128i value) noexcept
{
    value = _mm_shufflelo_epi16(value, _MM_SHUFFLE(3, 3, 3, 3));
    return _mm_shufflehi_epi16(value, _MM_SHUFFLE(3, 3, 3, 3));
}

inline __m128i alpha_blend_words(__m128i source, __m128i destination, bool rgb_destination = false) noexcept
{
    const auto alpha = replicate_alpha_u16(source);
    const auto destination_alpha = replicate_alpha_u16(destination);
    const auto inverse = _mm_sub_epi16(_mm_set1_epi16(256), alpha);
    const auto rgb = _mm_srli_epi16(_mm_add_epi16(
        _mm_mullo_epi16(source, _mm_add_epi16(alpha, _mm_set1_epi16(rgb_destination ? 0 : 1))),
        _mm_mullo_epi16(destination, inverse)), 8);
    const auto output_alpha = _mm_sub_epi16(_mm_add_epi16(alpha, destination_alpha),
        div255_u16(_mm_mullo_epi16(alpha, destination_alpha)));
    const auto alpha_lanes = _mm_set_epi16(-1, 0, 0, 0, -1, 0, 0, 0);
    if (rgb_destination) {
        const auto opaque_source = _mm_cmpeq_epi16(alpha, _mm_set1_epi16(255));
        const auto rgb_result = _mm_or_si128(_mm_and_si128(opaque_source, source), _mm_andnot_si128(opaque_source, rgb));
        return _mm_or_si128(_mm_andnot_si128(alpha_lanes, rgb_result), _mm_and_si128(alpha_lanes, destination));
    }
    const auto result = _mm_or_si128(_mm_andnot_si128(alpha_lanes, rgb),
                                    _mm_and_si128(alpha_lanes, output_alpha));
    const auto empty_target = _mm_cmpeq_epi16(destination_alpha, _mm_setzero_si128());
    return _mm_or_si128(_mm_and_si128(empty_target, source), _mm_andnot_si128(empty_target, result));
}

inline __m128i alpha_blend_four(__m128i source, __m128i destination, bool rgb_destination = false) noexcept
{
    const auto zero = _mm_setzero_si128();
    const auto source_low = _mm_unpacklo_epi8(source, zero);
    const auto source_high = _mm_unpackhi_epi8(source, zero);
    const auto destination_low = _mm_unpacklo_epi8(destination, zero);
    const auto destination_high = _mm_unpackhi_epi8(destination, zero);
    return _mm_packus_epi16(alpha_blend_words(source_low, destination_low, rgb_destination),
                            alpha_blend_words(source_high, destination_high, rgb_destination));
}
#elif defined(CARDWIRTH_SIMD_IMAGE_NEON)
inline uint16x8_t div255_u16(uint16x8_t value) noexcept
{
    return vshrq_n_u16(vaddq_u16(vaddq_u16(value, vdupq_n_u16(1)),
                                  vshrq_n_u16(value, 8)), 8);
}

inline uint8x16_t alpha_blend_four(uint8x16_t source,
                                   uint8x16_t destination, bool rgb_destination = false) noexcept
{
    alignas(16) static constexpr std::uint8_t alpha_indices_data[16] = {
        3, 3, 3, 3, 7, 7, 7, 7, 11, 11, 11, 11, 15, 15, 15, 15};
    alignas(16) static constexpr std::uint8_t alpha_mask_data[16] = {
        0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255};
    const auto alpha_bytes = vqtbl1q_u8(source, vld1q_u8(alpha_indices_data));
    const auto destination_alpha_bytes = vqtbl1q_u8(destination, vld1q_u8(alpha_indices_data));
    const auto source_low = vmovl_u8(vget_low_u8(source));
    const auto source_high = vmovl_u8(vget_high_u8(source));
    const auto destination_low = vmovl_u8(vget_low_u8(destination));
    const auto destination_high = vmovl_u8(vget_high_u8(destination));
    const auto alpha_low = vmovl_u8(vget_low_u8(alpha_bytes));
    const auto alpha_high = vmovl_u8(vget_high_u8(alpha_bytes));
    const auto inverse_low = vsubq_u16(vdupq_n_u16(256), alpha_low);
    const auto inverse_high = vsubq_u16(vdupq_n_u16(256), alpha_high);
    const auto rgb_low = vshrq_n_u16(vaddq_u16(vmulq_u16(source_low, vaddq_u16(alpha_low, vdupq_n_u16(rgb_destination ? 0 : 1))),
                                             vmulq_u16(destination_low, inverse_low)), 8);
    const auto rgb_high = vshrq_n_u16(vaddq_u16(vmulq_u16(source_high, vaddq_u16(alpha_high, vdupq_n_u16(rgb_destination ? 0 : 1))),
                                              vmulq_u16(destination_high, inverse_high)), 8);
    const auto da_low = vmovl_u8(vget_low_u8(destination_alpha_bytes));
    const auto da_high = vmovl_u8(vget_high_u8(destination_alpha_bytes));
    const auto output_alpha_low = vsubq_u16(vaddq_u16(alpha_low, da_low), div255_u16(vmulq_u16(alpha_low, da_low)));
    const auto output_alpha_high = vsubq_u16(vaddq_u16(alpha_high, da_high), div255_u16(vmulq_u16(alpha_high, da_high)));
    const auto rgb = vcombine_u8(vqmovn_u16(rgb_low), vqmovn_u16(rgb_high));
    if (rgb_destination) {
        const auto rgb_result = vbslq_u8(vceqq_u8(alpha_bytes, vdupq_n_u8(255)), source, rgb);
        return vbslq_u8(vld1q_u8(alpha_mask_data), destination, rgb_result);
    }
    const auto output_alpha = vcombine_u8(vqmovn_u16(output_alpha_low),
                                           vqmovn_u16(output_alpha_high));
    const auto result = vbslq_u8(vld1q_u8(alpha_mask_data), output_alpha, rgb);
    return vbslq_u8(vceqq_u8(destination_alpha_bytes, vdupq_n_u8(0)), source, result);
}
#endif

} // namespace

namespace {
// これらのヘルパーは pygame 1.9.6 の transform.c（git blob
// 6eb344f82ef54009cc03a8780dc206cda24246a9
// ）の数値規則を実装する。 中央サンプリングやアルファ乗算済みフィルターではない。SIMD
// は演算手段だけとし、 全ホストで軸の順序・重み・整数の切り捨てを統一する。
std::size_t resample_bytes(std::size_t width, std::size_t height)
{
    const auto limit = std::vector<std::uint8_t>{}.max_size();
    if (!width || !height || width > static_cast<std::size_t>((std::numeric_limits<int>::max)()) ||
        height > static_cast<std::size_t>((std::numeric_limits<int>::max)()) ||
        width > limit / 4 || height > limit / (width * 4))
        throw std::length_error("smoothscale: invalid image dimensions");
    return width * height * 4;
}

void interpolate_span(const std::uint8_t* first, const std::uint8_t* second,
                      std::uint8_t* result, std::size_t bytes, std::uint32_t fraction)
{
    if (!fraction) { std::memcpy(result, first, bytes); return; }
    const auto inverse = 65536U - fraction;
    std::size_t i = 0;
#if defined(CARDWIRTH_SIMD_IMAGE_SSE2)
    const auto zero = _mm_setzero_si128();
    const auto f = _mm_set1_epi16(static_cast<short>(fraction));
    const auto g = _mm_set1_epi16(static_cast<short>(inverse));
    const auto mix = [&](const __m128i a, const __m128i b) {
        const auto al = _mm_mullo_epi16(a, g), ah = _mm_mulhi_epu16(a, g);
        const auto bl = _mm_mullo_epi16(b, f), bh = _mm_mulhi_epu16(b, f);
        const auto lo = _mm_srli_epi32(_mm_add_epi32(_mm_unpacklo_epi16(al, ah), _mm_unpacklo_epi16(bl, bh)), 16);
        const auto hi = _mm_srli_epi32(_mm_add_epi32(_mm_unpackhi_epi16(al, ah), _mm_unpackhi_epi16(bl, bh)), 16);
        return _mm_packs_epi32(lo, hi);
    };
    for (; i + 16 <= bytes; i += 16) {
        const auto a = _mm_loadu_si128(reinterpret_cast<const __m128i*>(first + i));
        const auto b = _mm_loadu_si128(reinterpret_cast<const __m128i*>(second + i));
        const auto lo = mix(_mm_unpacklo_epi8(a, zero), _mm_unpacklo_epi8(b, zero));
        const auto hi = mix(_mm_unpackhi_epi8(a, zero), _mm_unpackhi_epi8(b, zero));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(result + i), _mm_packus_epi16(lo, hi));
    }
#elif defined(CARDWIRTH_SIMD_IMAGE_NEON)
    const auto mix = [&](const uint16x8_t a, const uint16x8_t b) {
        const auto lo = vmlal_n_u16(vmull_n_u16(vget_low_u16(a), static_cast<std::uint16_t>(inverse)), vget_low_u16(b), static_cast<std::uint16_t>(fraction));
        const auto hi = vmlal_n_u16(vmull_n_u16(vget_high_u16(a), static_cast<std::uint16_t>(inverse)), vget_high_u16(b), static_cast<std::uint16_t>(fraction));
        return vcombine_u16(vshrn_n_u32(lo, 16), vshrn_n_u32(hi, 16));
    };
    for (; i + 16 <= bytes; i += 16) {
        const auto a = vld1q_u8(first + i), b = vld1q_u8(second + i);
        const auto lo = mix(vmovl_u8(vget_low_u8(a)), vmovl_u8(vget_low_u8(b)));
        const auto hi = mix(vmovl_u8(vget_high_u8(a)), vmovl_u8(vget_high_u8(b)));
        vst1q_u8(result + i, vcombine_u8(vmovn_u16(lo), vmovn_u16(hi)));
    }
#endif
    for (; i < bytes; ++i)
        result[i] = static_cast<std::uint8_t>((first[i] * inverse + second[i] * fraction) >> 16U);
}

void add_resample_row(std::uint16_t* accumulator, const std::uint8_t* row, std::size_t bytes)
{
    std::size_t i = 0;
#if defined(CARDWIRTH_SIMD_IMAGE_SSE2)
    const auto zero = _mm_setzero_si128();
    for (; i + 16 <= bytes; i += 16) {
        const auto pixels = _mm_loadu_si128(reinterpret_cast<const __m128i*>(row + i));
        const auto lo = _mm_loadu_si128(reinterpret_cast<const __m128i*>(accumulator + i));
        const auto hi = _mm_loadu_si128(reinterpret_cast<const __m128i*>(accumulator + i + 8));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(accumulator + i), _mm_add_epi16(lo, _mm_unpacklo_epi8(pixels, zero)));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(accumulator + i + 8), _mm_add_epi16(hi, _mm_unpackhi_epi8(pixels, zero)));
    }
#elif defined(CARDWIRTH_SIMD_IMAGE_NEON)
    for (; i + 16 <= bytes; i += 16) {
        const auto pixels = vld1q_u8(row + i);
        vst1q_u16(accumulator + i, vaddq_u16(vld1q_u16(accumulator + i), vmovl_u8(vget_low_u8(pixels))));
        vst1q_u16(accumulator + i + 8, vaddq_u16(vld1q_u16(accumulator + i + 8), vmovl_u8(vget_high_u8(pixels))));
    }
#endif
    for (; i < bytes; ++i) accumulator[i] = static_cast<std::uint16_t>(accumulator[i] + row[i]);
}

void resample_horizontal(const std::uint8_t* source, std::uint8_t* result,
                         std::size_t sw, std::size_t dw, std::size_t height)
{
    if (sw == dw) { std::memcpy(result, source, dw * height * 4); return; }
    if (dw > sw) {
        // 全行で共有する入力位置と重みを、一度だけ事前計算する。
        struct Sample { std::size_t index; std::uint32_t fraction; };
        std::vector<Sample> samples(dw);
        for (std::size_t x = 0; x < dw; ++x) {
            const auto location = static_cast<std::uint64_t>(x) * (sw - 1);
            samples[x] = {static_cast<std::size_t>(location / dw), static_cast<std::uint32_t>((location % dw) * 65536ULL / dw)};
        }
        for (std::size_t y = 0; y < height; ++y)
            for (std::size_t x = 0; x < dw; ++x) {
                const auto p = samples[x];
                const auto* a = source + (y * sw + p.index) * 4;
                const auto* b = source + (y * sw + (std::min)(p.index + 1, sw - 1)) * 4;
                interpolate_span(a, b, result + (y * dw + x) * 4, 4, p.fraction);
            }
        return;
    }
    const std::uint64_t space = static_cast<std::uint64_t>(sw) * 65536ULL / dw;
    const std::uint64_t reciprocal = 0x100000000ULL / space;
    for (std::size_t y = 0; y < height; ++y) {
        std::uint16_t sum[4]{};
        auto remaining = space;
        std::size_t output = 0;
        // 従来の固定小数点比率は、大きく近い寸法同士では 1 に切り下がる
        // 場合がある。 その計算を維持しつつ、dw を超えて書き込まない。
        for (std::size_t x = 0; x < sw && output < dw; ++x) {
            const auto* pixel = source + (y * sw + x) * 4;
            if (remaining > 65536) {
                for (std::size_t c = 0; c < 4; ++c) sum[c] = static_cast<std::uint16_t>(sum[c] + pixel[c]);
                remaining -= 65536;
            } else {
                const auto remainder = 65536 - remaining;
                for (std::size_t c = 0; c < 4; ++c) {
                    result[(y * dw + output) * 4 + c] = static_cast<std::uint8_t>((sum[c] + ((pixel[c] * remaining) >> 16)) * reciprocal >> 16);
                    sum[c] = static_cast<std::uint16_t>((pixel[c] * remainder) >> 16);
                }
                ++output;remaining = space - remainder;
            }
        }
    }
}

void resample_vertical(const std::uint8_t* source, std::uint8_t* result,
                       std::size_t width, std::size_t sh, std::size_t dh)
{
    const auto row_bytes = width * 4;
    if (sh == dh) { std::memcpy(result, source, row_bytes * dh); return; }
    if (dh > sh) {
        for (std::size_t y = 0; y < dh; ++y) {
            const auto location = static_cast<std::uint64_t>(y) * (sh - 1);
            const auto index = static_cast<std::size_t>(location / dh);
            const auto fraction = static_cast<std::uint32_t>((location % dh) * 65536ULL / dh);
            interpolate_span(source + index * row_bytes, source + (std::min)(index + 1, sh - 1) * row_bytes,
                             result + y * row_bytes, row_bytes, fraction);
        }
        return;
    }
    const std::uint64_t space = static_cast<std::uint64_t>(sh) * 65536ULL / dh;
    const std::uint64_t reciprocal = 0x100000000ULL / space;
    std::vector<std::uint16_t> sum(row_bytes);
    auto remaining = space;
    std::size_t output = 0;
    for (std::size_t y = 0; y < sh && output < dh; ++y) {
        const auto* row = source + y * row_bytes;
        if (remaining > 65536) { add_resample_row(sum.data(), row, row_bytes); remaining -= 65536; }
        else {
            const auto remainder = 65536 - remaining;
            for (std::size_t i = 0; i < row_bytes; ++i) {
                result[output * row_bytes + i] = static_cast<std::uint8_t>((sum[i] + ((row[i] * remaining) >> 16)) * reciprocal >> 16);
                sum[i] = static_cast<std::uint16_t>((row[i] * remainder) >> 16);
            }
            ++output;remaining = space - remainder;
        }
    }
}
} // namespace

void smoothscale_rgba(const std::uint8_t* source, std::size_t source_width,
                      std::size_t source_height, std::uint8_t* destination,
                      std::size_t width, std::size_t height)
{
    (void)resample_bytes(source_width, source_height);
    const auto destination_bytes = resample_bytes(width, height);
    if (!source || !destination) throw std::invalid_argument("smoothscale: null buffer");
    if (source_width == width && source_height == height) { std::memcpy(destination, source, destination_bytes); return; }
    if (source_width == width) { resample_vertical(source, destination, width, source_height, height); return; }
    if (source_height == height) { resample_horizontal(source, destination, source_width, width, height); return; }
    std::vector<std::uint8_t> intermediate(resample_bytes(width, source_height));
    resample_horizontal(source, intermediate.data(), source_width, width, source_height);
    resample_vertical(intermediate.data(), destination, width, source_height, height);
}

void rgba_to_rgb_black(const std::uint8_t* source, std::uint8_t* destination,
                       std::size_t pixel_count, bool has_pixel_alpha)
{
    if (!pixel_count) return;
    if (!source || !destination || pixel_count > std::numeric_limits<std::size_t>::max() / 4u)
        throw std::invalid_argument("invalid RGBA to RGB conversion buffer");
    std::size_t index = 0;
#if defined(CARDWIRTH_SIMD_IMAGE_SSE2)
    const auto zero = _mm_setzero_si128();
    const auto bias = _mm_set1_epi16(255);
    const auto first_rgb = _mm_set_epi32(0, 0, 0, 0x00ffffff);
    for (; index + 4u <= pixel_count; index += 4u) {
        auto value = _mm_loadu_si128(reinterpret_cast<const __m128i*>(source + index * 4u));
        if (has_pixel_alpha) {
            auto low = _mm_unpacklo_epi8(value, zero);
            auto high = _mm_unpackhi_epi8(value, zero);
            low = _mm_srli_epi16(_mm_add_epi16(_mm_mullo_epi16(low, replicate_alpha_u16(low)), bias), 8);
            high = _mm_srli_epi16(_mm_add_epi16(_mm_mullo_epi16(high, replicate_alpha_u16(high)), bias), 8);
            value = _mm_packus_epi16(low, high);
        }
        const auto packed = _mm_or_si128(_mm_or_si128(
            _mm_and_si128(value, first_rgb),
            _mm_slli_si128(_mm_and_si128(_mm_srli_si128(value, 4), first_rgb), 3)),
            _mm_or_si128(
            _mm_slli_si128(_mm_and_si128(_mm_srli_si128(value, 8), first_rgb), 6),
            _mm_slli_si128(_mm_and_si128(_mm_srli_si128(value, 12), first_rgb), 9)));
        // 四ピクセルの出力は 12 バイトなので、16 バイトで書くと範囲外になる。
        std::memcpy(destination + index * 3u, &packed, 12);
    }
#elif defined(CARDWIRTH_SIMD_IMAGE_NEON)
    const auto bias = vdupq_n_u16(255);
    for (; index + 16u <= pixel_count; index += 16u) {
        const auto channels = vld4q_u8(source + index * 4u);
        uint8x16x3_t packed;
        for (unsigned channel = 0; channel < 3; ++channel) {
            auto value = channels.val[channel];
            if (has_pixel_alpha) {
                const auto low = vmull_u8(vget_low_u8(value), vget_low_u8(channels.val[3]));
                const auto high = vmull_u8(vget_high_u8(value), vget_high_u8(channels.val[3]));
                value = vcombine_u8(vshrn_n_u16(vaddq_u16(low, bias), 8),
                                    vshrn_n_u16(vaddq_u16(high, bias), 8));
            }
            packed.val[channel] = value;
        }
        vst3q_u8(destination + index * 3u, packed);
    }
#endif
    for (; index < pixel_count; ++index) {
        const unsigned alpha = has_pixel_alpha ? source[index * 4u + 3u] : 255u;
        for (unsigned channel = 0; channel < 3; ++channel)
            destination[index * 3u + channel] = static_cast<std::uint8_t>(
                (source[index * 4u + channel] * alpha + 255u) >> 8);
    }
}

const char* backend_name() noexcept
{
#if defined(CARDWIRTH_SIMD_IMAGE_SSE2)
    return "sse2";
#elif defined(CARDWIRTH_SIMD_IMAGE_NEON)
    return "neon";
#else
    return "scalar";
#endif
}

void fill_rgba(std::uint8_t* pixels, std::size_t pixel_count,
               std::uint8_t red, std::uint8_t green,
               std::uint8_t blue, std::uint8_t alpha) noexcept
{
    if (!pixels) return;
    std::size_t index = 0;
    [[maybe_unused]] const auto packed = packed_rgba(red, green, blue, alpha);
#if defined(CARDWIRTH_SIMD_IMAGE_SSE2)
    const auto value = _mm_set1_epi32(static_cast<int>(packed));
    for (; index + 4 <= pixel_count; index += 4)
        _mm_storeu_si128(reinterpret_cast<__m128i*>(pixels + index * 4U), value);
#elif defined(CARDWIRTH_SIMD_IMAGE_NEON)
    const auto value = vdupq_n_u32(packed);
    for (; index + 4 <= pixel_count; index += 4)
        vst1q_u32(reinterpret_cast<std::uint32_t*>(pixels + index * 4U), value);
#endif
    for (; index < pixel_count; ++index) {
        auto* pixel = pixels + index * 4U;
        pixel[0] = red;
        pixel[1] = green;
        pixel[2] = blue;
        pixel[3] = alpha;
    }
}

void force_alpha(std::uint8_t* pixels, std::size_t pixel_count,
                 std::uint8_t alpha) noexcept
{
    if (!pixels) return;
    std::size_t index = 0;
#if defined(CARDWIRTH_SIMD_IMAGE_SSE2)
    const auto rgb_mask = _mm_set1_epi32(0x00ffffff);
    const auto alpha_value = _mm_set1_epi32(static_cast<int>(alpha) << 24);
    for (; index + 4 <= pixel_count; index += 4) {
        auto value = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pixels + index * 4U));
        value = _mm_or_si128(_mm_and_si128(value, rgb_mask), alpha_value);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(pixels + index * 4U), value);
    }
#elif defined(CARDWIRTH_SIMD_IMAGE_NEON)
    const auto rgb_mask = vdupq_n_u32(0x00ffffffU);
    const auto alpha_value = vdupq_n_u32(static_cast<std::uint32_t>(alpha) << 24U);
    for (; index + 4 <= pixel_count; index += 4) {
        auto value = vld1q_u32(reinterpret_cast<const std::uint32_t*>(pixels + index * 4U));
        value = vorrq_u32(vandq_u32(value, rgb_mask), alpha_value);
        vst1q_u32(reinterpret_cast<std::uint32_t*>(pixels + index * 4U), value);
    }
#endif
    for (; index < pixel_count; ++index) pixels[index * 4U + 3U] = alpha;
}

void multiply_alpha(std::uint8_t* pixels, std::size_t pixel_count,
                    std::uint8_t multiplier) noexcept
{
    if (!pixels) return;
    std::size_t index = 0;
#if defined(CARDWIRTH_SIMD_IMAGE_SSE2)
    const auto rgb_mask = _mm_set1_epi32(0x00ffffff);
    const auto multiplier_words = _mm_set1_epi16(multiplier);
    for (; index + 4 <= pixel_count; index += 4) {
        const auto value = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pixels + index * 4U));
        const auto alpha = _mm_srli_epi32(value, 24);
        const auto scaled = div255_u16(_mm_mullo_epi16(alpha, multiplier_words));
        const auto result = _mm_or_si128(_mm_and_si128(value, rgb_mask), _mm_slli_epi32(scaled, 24));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(pixels + index * 4U), result);
    }
#elif defined(CARDWIRTH_SIMD_IMAGE_NEON)
    const auto rgb_mask = vdupq_n_u32(0x00ffffffU);
    const auto multiplier_value = vdupq_n_u32(multiplier);
    for (; index + 4 <= pixel_count; index += 4) {
        const auto value = vreinterpretq_u32_u8(vld1q_u8(pixels + index * 4U));
        const auto product = vmulq_u32(vshrq_n_u32(value, 24), multiplier_value);
        const auto scaled = vshrq_n_u32(vaddq_u32(vaddq_u32(product, vdupq_n_u32(1)),
                                                   vshrq_n_u32(product, 8)), 8);
        const auto result = vorrq_u32(vandq_u32(value, rgb_mask), vshlq_n_u32(scaled, 24));
        vst1q_u8(pixels + index * 4U, vreinterpretq_u8_u32(result));
    }
#endif
    for (; index < pixel_count; ++index) {
        auto& alpha = pixels[index * 4U + 3U];
        alpha = static_cast<std::uint8_t>(static_cast<unsigned>(alpha) * multiplier / 255U);
    }
}

void multiply_rgba_alpha(std::uint8_t* pixels, std::size_t pixel_count,
                         std::uint8_t multiplier, bool affect_alpha) noexcept
{
    if (!pixels) return;
    // pygame 1.9.6 の BLEND_RGBA_MULT と同様、全チャンネルに floor(value*factor/256) を使う。
    std::size_t index = 0;
#if defined(CARDWIRTH_SIMD_IMAGE_SSE2)
    const auto zero = _mm_setzero_si128();
    const auto alpha_factor = static_cast<short>(affect_alpha ? multiplier : 256);
    const auto factors = _mm_set_epi16(alpha_factor,255,255,255,alpha_factor,255,255,255);
    for (; index + 4 <= pixel_count; index += 4) {
        const auto pixels4 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pixels + index * 4U));
        const auto lo = _mm_srli_epi16(_mm_mullo_epi16(_mm_unpacklo_epi8(pixels4, zero), factors), 8);
        const auto hi = _mm_srli_epi16(_mm_mullo_epi16(_mm_unpackhi_epi8(pixels4, zero), factors), 8);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(pixels + index * 4U), _mm_packus_epi16(lo, hi));
    }
#elif defined(CARDWIRTH_SIMD_IMAGE_NEON)
    const auto alpha_factor = static_cast<std::uint16_t>(affect_alpha ? multiplier : 256);
    const std::uint16_t factor_values[8]{255,255,255,alpha_factor,255,255,255,alpha_factor};
    const auto factors = vld1q_u16(factor_values);
    for (; index + 4 <= pixel_count; index += 4) {
        const auto pixels4 = vld1q_u8(pixels + index * 4U);
        const auto lo = vmovn_u16(vshrq_n_u16(vmulq_u16(vmovl_u8(vget_low_u8(pixels4)), factors), 8));
        const auto hi = vmovn_u16(vshrq_n_u16(vmulq_u16(vmovl_u8(vget_high_u8(pixels4)), factors), 8));
        vst1q_u8(pixels + index * 4U, vcombine_u8(lo, hi));
    }
#endif
    for (; index < pixel_count; ++index) {
        auto* pixel = pixels + index * 4U;
        for (std::size_t channel = 0; channel < 3; ++channel)
            pixel[channel] = static_cast<std::uint8_t>(static_cast<unsigned>(pixel[channel]) * 255U >> 8U);
        if (affect_alpha) pixel[3] = static_cast<std::uint8_t>(static_cast<unsigned>(pixel[3]) * multiplier >> 8U);
    }
}

void subtract_alpha(std::uint8_t* pixels, std::size_t pixel_count,
                    std::uint8_t amount) noexcept
{
    if (!pixels) return;
    std::size_t index = 0;
#if defined(CARDWIRTH_SIMD_IMAGE_SSE2)
    const auto decrement = _mm_set1_epi32(static_cast<int>(amount) << 24);
    for (; index + 4 <= pixel_count; index += 4) {
        const auto value = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pixels + index * 4U));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(pixels + index * 4U),
                         _mm_subs_epu8(value, decrement));
    }
#elif defined(CARDWIRTH_SIMD_IMAGE_NEON)
    const auto decrement = vreinterpretq_u8_u32(vdupq_n_u32(
        static_cast<std::uint32_t>(amount) << 24U));
    for (; index + 4 <= pixel_count; index += 4) {
        const auto value = vld1q_u8(pixels + index * 4U);
        vst1q_u8(pixels + index * 4U, vqsubq_u8(value, decrement));
    }
#endif
    for (; index < pixel_count; ++index) {
        auto& alpha = pixels[index * 4U + 3U];
        alpha = static_cast<std::uint8_t>(alpha > amount ? alpha - amount : 0);
    }
}

void invert_rgb(std::uint8_t* pixels, std::size_t pixel_count) noexcept
{
    xor_rgb(pixels, pixel_count, 255, 255, 255);
}

void xor_rgb(std::uint8_t* pixels, std::size_t pixel_count,
             std::uint8_t first, std::uint8_t second, std::uint8_t third) noexcept
{
    if (!pixels) return;
    std::size_t index = 0;
#if defined(CARDWIRTH_SIMD_IMAGE_SSE2)
    const auto mask = _mm_set1_epi32(static_cast<int>(packed_rgba(first, second, third, 0)));
    for (; index + 4 <= pixel_count; index += 4) {
        auto value = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pixels + index * 4U));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(pixels + index * 4U), _mm_xor_si128(value, mask));
    }
#elif defined(CARDWIRTH_SIMD_IMAGE_NEON)
    const auto mask = vdupq_n_u32(packed_rgba(first, second, third, 0));
    for (; index + 4 <= pixel_count; index += 4) {
        auto value = vld1q_u32(reinterpret_cast<const std::uint32_t*>(pixels + index * 4U));
        vst1q_u32(reinterpret_cast<std::uint32_t*>(pixels + index * 4U), veorq_u32(value, mask));
    }
#endif
    for (; index < pixel_count; ++index) {
        auto* pixel = pixels + index * 4U;
        pixel[0] ^= first;
        pixel[1] ^= second;
        pixel[2] ^= third;
    }
}

void composite_straight_alpha(const std::uint8_t* source, std::uint8_t* destination,
                             std::size_t pixel_count, bool swap_source_red_blue) noexcept
{
    if (!source || !destination) return;
    std::size_t index = 0;
    // 共通キャンバスでは不透明な出力先が多い。 丸めも含め分子は 16
    // ビットに正確に収まり、半透明のピクセルには D の式を維持する。
#if defined(CARDWIRTH_SIMD_IMAGE_SSE2)
    const auto zero = _mm_setzero_si128();
    const auto mask = _mm_set1_epi32(static_cast<int>(0xff000000U));
    for (; index + 4 <= pixel_count; index += 4) {
        auto in = _mm_loadu_si128(reinterpret_cast<const __m128i*>(source + index * 4));
        const auto out = _mm_loadu_si128(reinterpret_cast<const __m128i*>(destination + index * 4));
        if (_mm_movemask_epi8(_mm_cmpeq_epi32(_mm_and_si128(out, mask), mask)) != 0xffff) break;
        if (swap_source_red_blue) {
            in = _mm_or_si128(_mm_and_si128(in, _mm_set1_epi32(static_cast<int>(0xff00ff00U))),
                _mm_or_si128(_mm_slli_epi32(_mm_and_si128(in, _mm_set1_epi32(255)), 16),
                             _mm_and_si128(_mm_srli_epi32(in, 16), _mm_set1_epi32(255))));
        }
        const auto blend = [](__m128i s, __m128i d) {
            const auto a = replicate_alpha_u16(s);
            return div255_u16(_mm_add_epi16(_mm_set1_epi16(127),
                _mm_add_epi16(_mm_mullo_epi16(s, a),
                    _mm_mullo_epi16(d, _mm_sub_epi16(_mm_set1_epi16(255), a)))));
        };
        const auto result = _mm_or_si128(mask, _mm_packus_epi16(
            blend(_mm_unpacklo_epi8(in, zero), _mm_unpacklo_epi8(out, zero)),
            blend(_mm_unpackhi_epi8(in, zero), _mm_unpackhi_epi8(out, zero))));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(destination + index * 4), result);
    }
#endif
    for (; index < pixel_count; ++index) {
        const auto* in = source + index * 4;
        auto* out = destination + index * 4;
        const unsigned sa = in[3], da = out[3];
        if (!sa) continue;
        const unsigned alpha = sa * 255 + da * (255 - sa);
        for (int c = 0; c < 3; ++c)
            out[c] = static_cast<std::uint8_t>((in[swap_source_red_blue ? 2-c : c] * sa * 255 +
                out[c] * da * (255-sa) + alpha/2) / alpha);
        out[3] = static_cast<std::uint8_t>((alpha+127)/255);
    }
}

void swap_red_blue(const std::uint8_t* source, std::uint8_t* destination,
                   std::size_t pixel_count) noexcept
{
    if (!source || !destination) return;
    std::size_t index = 0;
#if defined(CARDWIRTH_SIMD_IMAGE_SSE2)
    const auto green_alpha = _mm_set1_epi32(static_cast<int>(0xff00ff00U));
    const auto red = _mm_set1_epi32(0x000000ff);
    const auto blue = _mm_set1_epi32(0x00ff0000);
    for (; index + 4 <= pixel_count; index += 4) {
        const auto value = _mm_loadu_si128(reinterpret_cast<const __m128i*>(source + index * 4U));
        const auto swapped = _mm_or_si128(_mm_and_si128(value, green_alpha),
            _mm_or_si128(_mm_slli_epi32(_mm_and_si128(value, red), 16),
                         _mm_srli_epi32(_mm_and_si128(value, blue), 16)));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(destination + index * 4U), swapped);
    }
#elif defined(CARDWIRTH_SIMD_IMAGE_NEON)
    const auto green_alpha = vdupq_n_u32(0xff00ff00U);
    const auto red = vdupq_n_u32(0x000000ffU);
    const auto blue = vdupq_n_u32(0x00ff0000U);
    for (; index + 4 <= pixel_count; index += 4) {
        const auto value = vld1q_u32(reinterpret_cast<const std::uint32_t*>(source + index * 4U));
        const auto swapped = vorrq_u32(vandq_u32(value, green_alpha),
            vorrq_u32(vshlq_n_u32(vandq_u32(value, red), 16),
                      vshrq_n_u32(vandq_u32(value, blue), 16)));
        vst1q_u32(reinterpret_cast<std::uint32_t*>(destination + index * 4U), swapped);
    }
#endif
    for (; index < pixel_count; ++index) {
        const auto* input = source + index * 4U;
        auto* output = destination + index * 4U;
        const auto red_value = input[0];
        const auto blue_value = input[2];
        output[0] = blue_value;
        output[1] = input[1];
        output[2] = red_value;
        output[3] = input[3];
    }
}

void colorkey_alpha_zero(std::uint8_t* pixels, std::size_t pixel_count,
                         std::uint8_t red, std::uint8_t green,
                         std::uint8_t blue, bool clear_rgb) noexcept
{
    if (!pixels) return;
    [[maybe_unused]] const auto key = packed_rgba(red, green, blue, 0) & 0x00ffffffU;
    std::size_t index = 0;
#if defined(CARDWIRTH_SIMD_IMAGE_SSE2)
    const auto rgb_mask = _mm_set1_epi32(0x00ffffff);
    const auto alpha_mask = _mm_set1_epi32(static_cast<int>(clear_rgb ? 0xffffffffU : 0xff000000U));
    const auto key_value = _mm_set1_epi32(static_cast<int>(key));
    for (; index + 4 <= pixel_count; index += 4) {
        auto value = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pixels + index * 4U));
        const auto equal = _mm_cmpeq_epi32(_mm_and_si128(value, rgb_mask), key_value);
        value = _mm_andnot_si128(_mm_and_si128(equal, alpha_mask), value);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(pixels + index * 4U), value);
    }
#elif defined(CARDWIRTH_SIMD_IMAGE_NEON)
    const auto rgb_mask = vdupq_n_u32(0x00ffffffU);
    const auto alpha_mask = vdupq_n_u32(clear_rgb ? 0xffffffffU : 0xff000000U);
    const auto key_value = vdupq_n_u32(key);
    for (; index + 4 <= pixel_count; index += 4) {
        auto value = vld1q_u32(reinterpret_cast<const std::uint32_t*>(pixels + index * 4U));
        const auto equal = vceqq_u32(vandq_u32(value, rgb_mask), key_value);
        value = vbicq_u32(value, vandq_u32(equal, alpha_mask));
        vst1q_u32(reinterpret_cast<std::uint32_t*>(pixels + index * 4U), value);
    }
#endif
    for (; index < pixel_count; ++index) {
        auto* pixel = pixels + index * 4U;
        if (pixel[0] == red && pixel[1] == green && pixel[2] == blue) {
            if (clear_rgb) pixel[0] = pixel[1] = pixel[2] = 0;
            pixel[3] = 0;
        }
    }
}

void alpha_blend_rgba(const std::uint8_t* source, std::size_t source_stride,
                      std::uint8_t* destination, std::size_t destination_stride,
                      std::size_t width, std::size_t height, std::uint8_t surface_alpha,
                      bool replace_pixel_alpha, bool rgb_destination) noexcept
{
    if (!replace_pixel_alpha && surface_alpha != 255) {
        surface_alpha_blend_rgba(source, source_stride, destination, destination_stride,
                                width, height, surface_alpha);
        return;
    }
    if (!source || !destination || width == 0 || height == 0) return;
    for (std::size_t y = 0; y < height; ++y) {
        const auto* source_pixel = source + y * source_stride;
        auto* destination_pixel = destination + y * destination_stride;
        std::size_t x = 0;
#if defined(CARDWIRTH_SIMD_IMAGE_SSE2)
        for (; x + 4 <= width; x += 4) {
            auto source_value = _mm_loadu_si128(
                reinterpret_cast<const __m128i*>(source_pixel + x * 4U));
            if (replace_pixel_alpha)
                source_value = _mm_or_si128(_mm_and_si128(source_value, _mm_set1_epi32(0x00ffffff)),
                    _mm_set1_epi32(static_cast<int>(static_cast<std::uint32_t>(surface_alpha) << 24U)));
            const auto destination_value = _mm_loadu_si128(
                reinterpret_cast<const __m128i*>(destination_pixel + x * 4U));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(destination_pixel + x * 4U),
                             alpha_blend_four(source_value, destination_value, rgb_destination));
        }
#elif defined(CARDWIRTH_SIMD_IMAGE_NEON)
        for (; x + 4 <= width; x += 4) {
            auto source_value = vld1q_u8(source_pixel + x * 4U);
            if (replace_pixel_alpha)
                source_value = vreinterpretq_u8_u32(vorrq_u32(
                    vandq_u32(vreinterpretq_u32_u8(source_value), vdupq_n_u32(0x00ffffffU)),
                    vdupq_n_u32(static_cast<std::uint32_t>(surface_alpha) << 24U)));
            const auto destination_value = vld1q_u8(destination_pixel + x * 4U);
            vst1q_u8(destination_pixel + x * 4U,
                     alpha_blend_four(source_value, destination_value, rgb_destination));
        }
#endif
        for (; x < width; ++x) {
            if (replace_pixel_alpha) {
                const auto* pixel = source_pixel + x * 4U;
                const std::uint8_t effective[4]{pixel[0],pixel[1],pixel[2],surface_alpha};
                alpha_blend_pixel(effective, destination_pixel + x * 4U, rgb_destination);
            } else alpha_blend_pixel(source_pixel + x * 4U, destination_pixel + x * 4U, rgb_destination);
        }
    }
}

void surface_alpha_blend_rgba(const std::uint8_t* source, std::size_t source_stride,
                              std::uint8_t* destination, std::size_t destination_stride,
                              std::size_t width, std::size_t height,
                              std::uint8_t alpha, bool generic_rgb_rounding,
                              bool rgb565_source, bool rgb565_key) noexcept
{
    if (!source || !destination || !alpha) return;
    if (alpha == 255 && rgb565_source) {
        // SDL1 の透過色なしの RGB565 から RGB32 への変換
        // 表と同様、入力の各バイトを別々に展開する。 緑は両方の表の項目から
        // 成る。透過色付きの汎用変換は、成分ビットをシフトするだけとする。
        for (std::size_t y = 0; y < height; ++y) for (std::size_t x = 0; x < width; ++x) {
            const auto* s = source + y * source_stride + x * 4U;
            auto* d = destination + y * destination_stride + x * 4U;
            const unsigned r=s[0]>>3U, g=s[1]>>2U, b=s[2]>>3U;
            d[0]=static_cast<std::uint8_t>(rgb565_key ? r<<3U : r*255U/31U);
            d[1]=static_cast<std::uint8_t>(rgb565_key ? g<<2U : (g&56U)*255U/63U+(g&7U)*255U/63U);
            d[2]=static_cast<std::uint8_t>(rgb565_key ? b<<3U : b*255U/31U);
        }
        return;
    }
    if (alpha == 255) {
        // Py/SDL の RGB 描画面にはピクセルアルファがないため、 四バイ
        // ト目で不透明 RGB のコピー結果を変えず、出力先のアルファを維持する。
        for (std::size_t y = 0; y < height; ++y) {
            const auto* src = source + y * source_stride;
            auto* dst = destination + y * destination_stride;
            std::size_t x = 0;
#if defined(CARDWIRTH_SIMD_IMAGE_SSE2)
            const auto alpha_mask = _mm_set1_epi32(static_cast<int>(0xff000000U));
            for (; x + 4 <= width; x += 4) {
                const auto s = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + x * 4U));
                const auto d = _mm_loadu_si128(reinterpret_cast<const __m128i*>(dst + x * 4U));
                _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + x * 4U),
                    _mm_or_si128(_mm_andnot_si128(alpha_mask, s), _mm_and_si128(alpha_mask, d)));
            }
#elif defined(CARDWIRTH_SIMD_IMAGE_NEON)
            const auto alpha_mask = vreinterpretq_u8_u32(vdupq_n_u32(0xff000000U));
            for (; x + 4 <= width; x += 4) {
                const auto s = vld1q_u8(src + x * 4U);
                const auto d = vld1q_u8(dst + x * 4U);
                vst1q_u8(dst + x * 4U, vbslq_u8(alpha_mask, d, s));
            }
#endif
            for (; x < width; ++x)
                for (std::size_t channel = 0; channel < 3; ++channel)
                    dst[x * 4U + channel] = src[x * 4U + channel];
        }
        return;
    }
    // SDL1 の汎用 RGB 転送は、RGB24・透過色などで 8 ビット右シフ
    // ト前に 255 を加える。 最適化された RGB32 経路では加算しない。こ
    // の数値規則は CPU バックエンド間で共有し、OS ごとの方針にはしない。
    const unsigned bias = generic_rgb_rounding ? 255U : 0U;
    for (std::size_t y = 0; y < height; ++y) {
        const auto* src = source + y * source_stride;
        auto* dst = destination + y * destination_stride;
        std::size_t x = 0;
#if defined(CARDWIRTH_SIMD_IMAGE_SSE2)
        const auto zero = _mm_setzero_si128();
        const auto factor = _mm_set1_epi16(alpha);
        const auto inverse = _mm_set1_epi16(256 - alpha);
        const auto alpha_mask = _mm_set1_epi32(static_cast<int>(0xff000000U));
        for (; x + 4 <= width; x += 4) {
            auto s = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + x * 4U));
            if (rgb565_source) s = _mm_and_si128(s, _mm_set1_epi32(0x00f8fcf8));
            const auto d = _mm_loadu_si128(reinterpret_cast<const __m128i*>(dst + x * 4U));
            const auto blend = [&](const __m128i a, const __m128i b) {
                return _mm_srli_epi16(_mm_add_epi16(_mm_add_epi16(_mm_mullo_epi16(a, factor),
                                                    _mm_mullo_epi16(b, inverse)), _mm_set1_epi16(static_cast<short>(bias))), 8);
            };
            const auto rgb = _mm_packus_epi16(
                blend(_mm_unpacklo_epi8(s, zero), _mm_unpacklo_epi8(d, zero)),
                blend(_mm_unpackhi_epi8(s, zero), _mm_unpackhi_epi8(d, zero)));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + x * 4U),
                _mm_or_si128(_mm_andnot_si128(alpha_mask, rgb), _mm_and_si128(alpha_mask, d)));
        }
#elif defined(CARDWIRTH_SIMD_IMAGE_NEON)
        const auto factor = vdupq_n_u16(alpha);
        const auto inverse = vdupq_n_u16(256 - alpha);
        const auto alpha_mask = vreinterpretq_u8_u32(vdupq_n_u32(0xff000000U));
        for (; x + 4 <= width; x += 4) {
            auto s = vld1q_u8(src + x * 4U);
            if (rgb565_source) s = vandq_u8(s, vreinterpretq_u8_u32(vdupq_n_u32(0x00f8fcf8U)));
            const auto d = vld1q_u8(dst + x * 4U);
            const auto blend = [&](const uint16x8_t a, const uint16x8_t b) {
                return vshrq_n_u16(vaddq_u16(vaddq_u16(vmulq_u16(a, factor), vmulq_u16(b, inverse)), vdupq_n_u16(static_cast<std::uint16_t>(bias))), 8);
            };
            const auto rgb = vcombine_u8(
                vmovn_u16(blend(vmovl_u8(vget_low_u8(s)), vmovl_u8(vget_low_u8(d)))),
                vmovn_u16(blend(vmovl_u8(vget_high_u8(s)), vmovl_u8(vget_high_u8(d)))));
            vst1q_u8(dst + x * 4U, vbslq_u8(alpha_mask, d, rgb));
        }
#endif
        for (; x < width; ++x)
            for (std::size_t channel = 0; channel < 3; ++channel)
                dst[x * 4U + channel] = static_cast<std::uint8_t>(
                    ((rgb565_source ? src[x * 4U + channel] & (channel == 1 ? 252U : 248U) : src[x * 4U + channel]) * alpha + dst[x * 4U + channel] * (256U - alpha) + bias) >> 8U);
    }
}

void blend_rgb_rgba(const std::uint8_t* source, std::size_t source_stride,
                     std::uint8_t* destination, std::size_t destination_stride,
                     std::size_t width, std::size_t height, bool add) noexcept
{
    if (!source || !destination) return;
    for (std::size_t y=0;y<height;++y) {
        const auto* s=source+y*source_stride;
        auto* d=destination+y*destination_stride;
        std::size_t x=0;
#if defined(CARDWIRTH_SIMD_IMAGE_SSE2)
        const auto alpha_mask=_mm_set1_epi32(static_cast<int>(0xff000000U));
        for (;x+4<=width;x+=4) {
            const auto sv=_mm_loadu_si128(reinterpret_cast<const __m128i*>(s+x*4U));
            const auto dv=_mm_loadu_si128(reinterpret_cast<const __m128i*>(d+x*4U));
            const auto rgb=add ? _mm_adds_epu8(sv,dv) : _mm_min_epu8(sv,dv);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(d+x*4U),
                _mm_or_si128(_mm_andnot_si128(alpha_mask,rgb),_mm_and_si128(alpha_mask,dv)));
        }
#elif defined(CARDWIRTH_SIMD_IMAGE_NEON)
        const auto alpha_mask=vreinterpretq_u8_u32(vdupq_n_u32(0xff000000U));
        for (;x+4<=width;x+=4) {
            const auto sv=vld1q_u8(s+x*4U),dv=vld1q_u8(d+x*4U);
            const auto rgb=add ? vqaddq_u8(sv,dv) : vminq_u8(sv,dv);
            vst1q_u8(d+x*4U,vbslq_u8(alpha_mask,dv,rgb));
        }
#endif
        for (;x<width;++x) for (std::size_t channel=0;channel<3;++channel)
            d[x*4U+channel]=add ? static_cast<std::uint8_t>((std::min)(255U,static_cast<unsigned>(s[x*4U+channel])+d[x*4U+channel]))
                : (std::min)(s[x*4U+channel],d[x*4U+channel]);
    }
}

} // namespace cw::lib::simd_image
