#include "cwxeditor_src/lib/editor_diagnostics.hpp"
#include "cwxeditor_src/core/cwx/graphics.hpp"
#include "cwxeditor_src/core/cwx/image_real.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <utility>

namespace cwxeditor::cwx::graphics {
namespace {

using Byte = std::uint8_t;
using ImageReal = detail::ImageReal;

[[nodiscard]] std::size_t checkedMultiply(std::size_t left,
                                           std::size_t right,
                                           const char* message) {
    if (right != 0U &&
        left > std::numeric_limits<std::size_t>::max() / right) {
        throw std::length_error(message);
    }
    return left * right;
}

[[nodiscard]] std::size_t checkedAdd(std::size_t left, std::size_t right,
                                     const char* message) {
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        throw std::length_error(message);
    }
    return left + right;
}

[[nodiscard]] std::size_t bytesForDepth(std::size_t depth) {
    switch (depth) {
    case 16U: return 2U;
    case 24U: return 3U;
    case 32U: return 4U;
    default: throw std::invalid_argument("unsupported bit depth");
    }
}

[[nodiscard]] std::size_t alignedStride(std::size_t width,
                                        std::size_t bpp) {
    const auto row = checkedMultiply(width, bpp, "pixel row is too large");
    return checkedAdd(row, (4U - row % 4U) % 4U,
                      "pixel row alignment overflow");
}

void validateImage(const lib::Bytes& data, const lib::Bytes& alpha,
                   std::size_t width, std::size_t height, std::size_t depth,
                   std::size_t bytesPerLine, std::size_t bpp) {
    const auto minimumBpp = bytesForDepth(depth);
    if (bpp < minimumBpp) {
        throw std::invalid_argument("pixel spacing is too short");
    }
    const auto rowBytes = checkedMultiply(
        width, bpp, "pixel row is too large");
    if (bytesPerLine < rowBytes) {
        throw std::invalid_argument("pixel stride is too short");
    }
    const auto dataBytes = checkedMultiply(
        bytesPerLine, height, "pixel buffer is too large");
    if (data.size() < dataBytes) {
        throw std::invalid_argument("pixel buffer is too short");
    }
    if (!alpha.empty()) {
        const auto alphaBytes = checkedMultiply(
            width, height, "alpha buffer is too large");
        if (alpha.size() < alphaBytes) {
            throw std::invalid_argument("alpha buffer is too short");
        }
    }
}

[[nodiscard]] std::size_t effectiveBpp(std::size_t width,
                                       std::size_t bytesPerLine,
                                       std::size_t depth,
                                       std::size_t bpp) {
    if (bpp != 0U) return bpp;
    if (width != 0U) return bytesPerLine / width;
    return bytesForDepth(depth);
}

void requireCoordinate(const Pixels& pixels, std::size_t x, std::size_t y) {
    if (x >= pixels.width || y >= pixels.height) {
        throw std::out_of_range("pixel coordinate");
    }
}

[[nodiscard]] std::int32_t randomExclusive(std::mt19937& random,
                                             std::int32_t upper) {
    if (upper <= 0) return 0;
    // std.random.uniformの整数オーバーロードは
    // MT19937の出力を直接使い、剰余の偏りを生む末尾部分
    // だけを拒否する。C++規格はuniform_int_distribution
    // の対応付けを未規定としているため、それを使うと
    // Dの固定シードでも実装依存の異なる画像になる。
    const auto upperDistance = static_cast<std::uint32_t>(upper);
    std::uint32_t offset = 0U;
    std::uint32_t bucketFront = 0U;
    do {
        const auto randomNumber = random();
        offset = randomNumber % upperDistance;
        bucketFront = randomNumber - offset;
    } while (bucketFront > std::numeric_limits<std::uint32_t>::max() -
                                 (upperDistance - 1U));
    return static_cast<std::int32_t>(offset);
}

[[nodiscard]] lib::Bytes& emptyBytes() {
    static lib::Bytes value;
    return value;
}

[[nodiscard]] std::int64_t toSigned(std::size_t value) {
    if (value > static_cast<std::size_t>(
                    std::numeric_limits<std::int64_t>::max())) {
        throw std::length_error("pixel coordinate is too large");
    }
    return static_cast<std::int64_t>(value);
}

}  // namespace

bool FC::eqRgb(const FC& other) const noexcept {
    return r == other.r && g == other.g && b == other.b;
}

bool operator==(const FC& left, const FC& right) noexcept {
    return left.r == right.r && left.g == right.g && left.b == right.b &&
           left.a == right.a;
}

FC FCu::fc() const noexcept {
    return {static_cast<Byte>(r), static_cast<Byte>(g), static_cast<Byte>(b),
            static_cast<Byte>(a)};
}

bool FCf::eqRgb(const FC& other) const noexcept {
    return r == other.r && g == other.g && b == other.b;
}

Pixels::Pixels() : data(emptyBytes()), alpha(emptyBytes()) {}

Pixels::Pixels(lib::Bytes& data, lib::Bytes& alpha, std::size_t width,
               std::size_t height, std::size_t depth,
               std::size_t bytesPerLine, std::size_t bpp)
    : data(data),
      alpha(alpha),
      width(width),
      height(height),
      depth(depth),
      bytesPerLine(bytesPerLine),
      bpp(effectiveBpp(width, bytesPerLine, depth, bpp)) {
    validateImage(this->data, this->alpha, this->width, this->height,
                  this->depth, this->bytesPerLine, this->bpp);
}

FC Pixels::get(std::size_t x, std::size_t y) const {
    requireCoordinate(*this, x, y);
    const auto i = checkedAdd(
        checkedMultiply(y, bytesPerLine, "pixel offset is too large"),
        checkedMultiply(x, bpp, "pixel offset is too large"),
        "pixel offset is too large");
    const auto a = alpha.empty() ? Byte(0) : alpha[y * width + x];
    switch (depth) {
    case 32U:
    case 24U:
        return {data[i + 2U], data[i + 1U], data[i], a};
    case 16U: {
        const auto d1 = static_cast<std::size_t>(data[i]);
        const auto d2 = static_cast<std::size_t>(data[i + 1U]);
        return {static_cast<Byte>((d2 << 1U) & 0xf8U),
                static_cast<Byte>((d2 << 6U) | ((d1 & 0xc0U) >> 2U)),
                static_cast<Byte>(d1 << 3U), a};
    }
    default: throw std::invalid_argument("unsupported bit depth");
    }
}

void Pixels::set(std::size_t x, std::size_t y, Byte r, Byte g, Byte b,
                 Byte a) {
    requireCoordinate(*this, x, y);
    const auto i = checkedAdd(
        checkedMultiply(y, bytesPerLine, "pixel offset is too large"),
        checkedMultiply(x, bpp, "pixel offset is too large"),
        "pixel offset is too large");
    switch (depth) {
    case 24U:
    case 32U:
        data[i + 2U] = r;
        data[i + 1U] = g;
        data[i] = b;
        break;
    case 16U:
        data[i + 1U] = static_cast<Byte>(((r & 0xf8U) >> 1U) |
                                         (g >> 6U));
        data[i] = static_cast<Byte>(((g << 2U) & 0xe0U) | (b >> 3U));
        break;
    default: throw std::invalid_argument("unsupported bit depth");
    }
    if (!alpha.empty()) alpha[y * width + x] = a;
}

void Pixels::set(std::size_t x, std::size_t y, std::uint32_t r,
                 std::uint32_t g, std::uint32_t b, std::uint32_t a) {
    set(x, y, static_cast<Byte>(r), static_cast<Byte>(g), static_cast<Byte>(b),
        static_cast<Byte>(a));
}

void Pixels::set(std::size_t x, std::size_t y, const FC& color) {
    set(x, y, color.r, color.g, color.b, color.a);
}

void Pixels::swap(std::size_t x1, std::size_t y1, std::size_t x2,
                  std::size_t y2) {
    const auto temporary = get(x1, y1);
    set(x1, y1, get(x2, y2));
    set(x2, y2, temporary);
}

void turn(lib::Bytes& data, lib::Bytes& alpha, std::size_t& width,
          std::size_t& height, std::size_t& bytesPerLine, Turn effect,
          std::size_t depth) {
    if (static_cast<std::int32_t>(effect) == 0 || width < 1U ||
        height < 1U) {
        return;
    }
    const auto bpp = bytesPerLine / width;
    validateImage(data, alpha, width, height, depth, bytesPerLine, bpp);
    const auto newWidth = height;
    const auto newHeight = width;
    const auto newBytesPerLine = alignedStride(newWidth, bpp);
    lib::Bytes result(checkedMultiply(newBytesPerLine, newHeight,
                                      "rotated image is too large"));
    lib::Bytes resultAlpha(
        alpha.empty() ? 0U
                      : checkedMultiply(newWidth, newHeight,
                                        "rotated alpha is too large"));
    Pixels base(data, alpha, width, height, depth, bytesPerLine, bpp);
    Pixels rotated(result, resultAlpha, newWidth, newHeight, depth,
                   newBytesPerLine, bpp);
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            const auto value = base.get(x, y);
            if (static_cast<std::int32_t>(effect) == 1) {
                rotated.set(height - 1U - y, x, value);
            } else if (static_cast<std::int32_t>(effect) == 2) {
                rotated.set(y, width - 1U - x, value);
            } else {
                throw std::invalid_argument("unsupported turn effect");
            }
        }
    }
    data = std::move(result);
    alpha = std::move(resultAlpha);
    width = newWidth;
    height = newHeight;
    bytesPerLine = newBytesPerLine;
}

lib::Bytes flip(lib::Bytes& data, lib::Bytes& alpha, std::size_t depth,
                std::size_t width, std::size_t height,
                std::size_t bytesPerLine) {
    if (width < 1U || height < 1U) return data;
    Pixels pixels(data, alpha, width, height, depth, bytesPerLine);
    for (std::size_t y = 0; y < height / 2U; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            pixels.swap(x, y, x, height - y - 1U);
        }
    }
    return data;
}

lib::Bytes mirror(lib::Bytes& data, lib::Bytes& alpha, std::size_t depth,
                  std::size_t width, std::size_t height,
                  std::size_t bytesPerLine) {
    if (width < 1U || height < 1U) return data;
    Pixels pixels(data, alpha, width, height, depth, bytesPerLine);
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width / 2U; ++x) {
            pixels.swap(x, y, width - x - 1U, y);
        }
    }
    return data;
}

void round(FCu& rgb) noexcept {
    rgb.r = std::clamp<std::int32_t>(rgb.r, 0, 255);
    rgb.g = std::clamp<std::int32_t>(rgb.g, 0, 255);
    rgb.b = std::clamp<std::int32_t>(rgb.b, 0, 255);
    rgb.a = std::clamp<std::int32_t>(rgb.a, 0, 255);
}

template <typename Effect>
lib::Bytes pixelProc(Effect effect, lib::Bytes& data, lib::Bytes& alpha,
                     std::size_t depth, std::size_t width,
                     std::size_t height, std::size_t bytesPerLine) {
    if (width < 1U || height < 1U) return data;
    const auto bpp = bytesPerLine / width;
    Pixels pixels(data, alpha, width, height, depth, bytesPerLine, bpp);
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            const auto color = pixels.get(x, y);
            // DはここでRGBだけを指定してFCuを
            // 構築する。そのため外部アルファ面がある
            // 場合、既定のアルファ値0を書き戻す。
            FCu rgb{color.r, color.g, color.b};
            pixelProcImpl(effect, rgb);
            pixels.set(x, y, rgb.fc());
        }
    }
    return data;
}

lib::Bytes colorexchange(Colorexchange effect, lib::Bytes& data,
                         lib::Bytes& alpha, std::size_t depth,
                         std::size_t width, std::size_t height,
                         std::size_t bytesPerLine) {
    return pixelProc(effect, data, alpha, depth, width, height,
                     bytesPerLine);
}

lib::Bytes colormap(Colormap effect, lib::Bytes& data, lib::Bytes& alpha,
                    std::size_t depth, std::size_t width, std::size_t height,
                    std::size_t bytesPerLine) {
    return pixelProc(effect, data, alpha, depth, width, height,
                     bytesPerLine);
}

lib::Bytes emboss(lib::Bytes& data, lib::Bytes& alpha, std::size_t depth,
                  std::size_t width, std::size_t height,
                  std::size_t bytesPerLine) {
    if (width < 1U || height < 1U) return data;
    Pixels pixels(data, alpha, width, height, depth, bytesPerLine);
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            const auto nextX = x + 1U < width ? x + 1U : x;
            const auto nextY = y + 1U < height ? y + 1U : y;
            const auto current = pixels.get(x, y);
            const auto next = pixels.get(nextX, nextY);
            auto value = (static_cast<std::int32_t>(next.r) + next.g + next.b) /
                             3 -
                         (static_cast<std::int32_t>(current.r) + current.g +
                          current.b) /
                             3 +
                         128;
            if (value < 0 || value > 255) value = 0;
            const auto result = static_cast<Byte>(value);
            pixels.set(x, y, result, result, result, current.a);
        }
    }
    return data;
}

lib::Bytes deffusion(lib::Bytes& data, lib::Bytes& alpha,
                     std::size_t depth, std::size_t width,
                     std::size_t height, std::size_t bytesPerLine) {
    if (width < 1U || height < 1U) return data;
    const auto bpp = bytesPerLine / width;
    validateImage(data, alpha, width, height, depth, bytesPerLine, bpp);
    std::mt19937 random(1U);
    lib::Bytes result(data.size());
    lib::Bytes resultAlpha(alpha.size());
    Pixels output(result, resultAlpha, width, height, depth, bytesPerLine,
                  bpp);
    for (std::size_t y = 0; y < height; ++y) {
        const auto jy = std::min(
            height - 1U,
            y + static_cast<std::size_t>(randomExclusive(random, 3)));
        for (std::size_t x = 0; x < width; ++x) {
            const auto jx = std::min(
                width - 1U,
                x + static_cast<std::size_t>(randomExclusive(random, 3)));
            // 出力側のビューを意図的に読み、D実装のcwconv.dll
            // の従来の拡散動作に合わせる。
            output.set(x, y, output.get(jx, jy));
        }
    }
    data = std::move(result);
    alpha = std::move(resultAlpha);
    return data;
}

lib::Bytes filter(Filter effect, lib::Bytes& data, lib::Bytes& alpha,
                  std::size_t depth, std::size_t width, std::size_t height,
                  std::size_t bytesPerLine) {
    const auto value = static_cast<std::int32_t>(effect);
    if (value == 0 || width < 1U || height < 1U) return data;
    if (value == 7 || value == 9) {
        return pixelProc(effect, data, alpha, depth, width, height,
                         bytesPerLine);
    }
    if (value == 8) {
        return deffusion(data, alpha, depth, width, height, bytesPerLine);
    }
    if (value == 10) {
        return emboss(data, alpha, depth, width, height, bytesPerLine);
    }

    std::array<std::array<std::int32_t, 3>, 3> kernel{};
    std::int32_t divisor = 1;
    std::int32_t adjustment = 0;
    switch (value) {
    case 1:
        for (auto& row : kernel) row.fill(1);
        divisor = 9;
        break;
    case 2:
        kernel = {{{-1, -1, -1}, {-1, 24, -1}, {-1, -1, -1}}};
        divisor = 16;
        break;
    case 3:
        kernel = {{{1, 3, 1}, {3, 5, 3}, {1, 3, 1}}};
        divisor = 16;
        break;
    case 4:
        kernel = {{{-1, -1, -1}, {0, 1, 0}, {1, 1, 1}}};
        break;
    case 5:
        kernel = {{{-1, -2, -1}, {0, 0, 0}, {1, 2, 1}}};
        adjustment = 128;
        break;
    case 6:
        kernel = {{{1, 1, 1}, {1, -15, 1}, {1, 1, 1}}};
        break;
    default:
        return data;
    }

    const auto bpp = bytesPerLine / width;
    validateImage(data, alpha, width, height, depth, bytesPerLine, bpp);
    lib::Bytes result(data.size());
    lib::Bytes resultAlpha(
        alpha.empty() ? 0U
                      : checkedMultiply(width, height,
                                        "filtered alpha is too large"));
    Pixels base(data, alpha, width, height, depth, bytesPerLine, bpp);
    Pixels output(result, resultAlpha, width, height, depth, bytesPerLine,
                  bpp);
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            std::int32_t r = 0;
            std::int32_t g = 0;
            std::int32_t b = 0;
            for (std::int32_t xt = 0; xt < 3; ++xt) {
                for (std::int32_t yt = 0; yt < 3; ++yt) {
                    auto sourceX = toSigned(x) + xt - 1;
                    auto sourceY = toSigned(y) + yt - 1;
                    sourceX = std::clamp<std::int64_t>(
                        sourceX, 0, toSigned(width - 1U));
                    sourceY = std::clamp<std::int64_t>(
                        sourceY, 0, toSigned(height - 1U));
                    const auto color = base.get(
                        static_cast<std::size_t>(sourceX),
                        static_cast<std::size_t>(sourceY));
                    const auto weight = kernel[yt][xt];
                    r += static_cast<std::int32_t>(color.r) * weight;
                    g += static_cast<std::int32_t>(color.g) * weight;
                    b += static_cast<std::int32_t>(color.b) * weight;
                }
            }
            FCu color{r / divisor + adjustment, g / divisor + adjustment,
                      b / divisor + adjustment, 0};
            round(color);
            output.set(x, y, color.fc());
        }
    }
    data = std::move(result);
    alpha = std::move(resultAlpha);
    return data;
}

lib::Bytes mask(Mask effect, lib::Bytes& data, lib::Bytes& alpha,
                std::size_t depth, std::size_t width, std::size_t height,
                std::size_t bytesPerLine) {
    if (width < 1U || height < 1U) return data;
    Pixels pixels(data, alpha, width, height, depth, bytesPerLine);
    const auto maskColor = pixels.get(0, 0);
    const auto value = static_cast<std::int32_t>(effect);
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            const bool vertical = (value == 1 || value == 3) && !(x & 1U);
            const bool horizontal = (value == 2 || value == 3) && !(y & 1U);
            if (vertical || horizontal) {
                pixels.set(x, y, maskColor.r, maskColor.g, maskColor.b,
                           maskColor.a);
            }
        }
    }
    return data;
}

void noiseImpl(std::mt19937& random, Noise effect, FCu& rgb,
               std::int32_t value) {
    switch (static_cast<std::int32_t>(effect)) {
    case 0: return;
    case 1:
        rgb.r += value;
        rgb.g += value;
        rgb.b += value;
        break;
    case 2: {
        const auto result = (rgb.r > value || rgb.g > value || rgb.b > value)
                                ? 255
                                : 0;
        rgb.r = result;
        rgb.g = result;
        rgb.b = result;
        break;
    }
    case 3:
        if (value >= 0) {
            const auto randomValue = randomExclusive(random, value);
            rgb.r += randomValue;
            rgb.g += randomValue;
            rgb.b += randomValue;
        } else {
            const auto result = randomExclusive(random, 2) ? 255 : 0;
            rgb.r = result;
            rgb.g = result;
            rgb.b = result;
        }
        break;
    case 4:
        if (value >= 0) {
            rgb.r += randomExclusive(random, value);
            rgb.g += randomExclusive(random, value);
            rgb.b += randomExclusive(random, value);
        } else {
            rgb.r = randomExclusive(random, 2) ? 255 : 0;
            rgb.g = randomExclusive(random, 2) ? 255 : 0;
            rgb.b = randomExclusive(random, 2) ? 255 : 0;
        }
        break;
    default: return;
    }
    round(rgb);
}

lib::Bytes noise(Noise effect, std::int32_t value, lib::Bytes& data,
                 lib::Bytes& alpha, std::size_t depth, std::size_t width,
                 std::size_t height, std::size_t bytesPerLine) {
    if (static_cast<std::int32_t>(effect) == 0 || value == 0 ||
        width < 1U || height < 1U) {
        return data;
    }
    value %= 256;
    if (static_cast<std::int32_t>(effect) == 5 && value <= 0) {
        // Dは正規化後にだけ負のMOSAICサイズを拒否
        // する。256や-256など256の倍数は0にな
        // り、x % valueでゼロ除算を起こす。その
        // 不具合を再現せず、このサイズでは何もしない。
        return data;
    }
    Pixels pixels(data, alpha, width, height, depth, bytesPerLine);
    std::mt19937 random(42U);
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            if (static_cast<std::int32_t>(effect) == 5) {
                const auto block = static_cast<std::size_t>(value);
                const auto sourceX = x - x % block;
                const auto sourceY = y - y % block;
                pixels.set(x, y, pixels.get(sourceX, sourceY));
            } else {
                const auto original = pixels.get(x, y);
                // Dと同様に、一時的なFCuはRGBだけ
                // で初期化する。そのためノイズ操作を書き
                // 戻すと、外部アルファの値は0になる。
                FCu color{original.r, original.g, original.b};
                noiseImpl(random, effect, color, value);
                pixels.set(x, y, color.fc());
            }
        }
    }
    return data;
}

lib::Bytes resize(std::size_t newWidth, std::size_t newHeight,
                  lib::Bytes& data, lib::Bytes& alpha, std::size_t depth,
                  std::size_t width, std::size_t height,
                  std::size_t bytesPerLine, std::size_t& newBytesPerLine) {
    const ::cwxeditor::lib::EditorPerformanceScope performance("image.resize.nearest");
    newBytesPerLine = 0;
    if (width < 1U || height < 1U) return data;
    if (width == newWidth && height == newHeight) {
        newBytesPerLine = bytesPerLine;
        return data;
    }
    const auto bpp = bytesPerLine / width;
    validateImage(data, alpha, width, height, depth, bytesPerLine, bpp);
    const ImageReal pixelWidth = static_cast<ImageReal>(newWidth) /
                                   static_cast<ImageReal>(width);
    const ImageReal pixelHeight = static_cast<ImageReal>(newHeight) /
                                    static_cast<ImageReal>(height);
    newBytesPerLine = alignedStride(newWidth, bpp);
    lib::Bytes result(checkedMultiply(newHeight, newBytesPerLine,
                                      "resized image is too large"));
    lib::Bytes resultAlpha(
        alpha.empty() ? 0U
                      : checkedMultiply(newWidth, newHeight,
                                        "resized alpha is too large"));
    if (newWidth == 0U || newHeight == 0U) {
        data = std::move(result);
        alpha = std::move(resultAlpha);
        return data;
    }
    Pixels base(data, alpha, width, height, depth, bytesPerLine, bpp);
    // Dのresizeは誤って幅と高さの両方にnewHeightを使う。
    // 長方形の外部アルファ面で行を越えたり重ねたりしないよう、ここでは
    // 既存の安全な幅を維持する。標本化の計算はDのreal精度に従う。
    Pixels output(result, resultAlpha, newWidth, newHeight, depth,
                  newBytesPerLine, bpp);
    // 正確なDのreal座標は、行が変わっても同じ。その精度は維
    // 持し、画素ごとにソフトウェアの除算をせず一度だけ計算する。
    std::vector<std::size_t> sourceXs(newWidth);
    for(std::size_t x=0;x<newWidth;++x)
        sourceXs[x]=std::min(static_cast<std::size_t>(ImageReal(x)/pixelWidth),width-1U);
    for (std::size_t y = 0; y < newHeight; ++y) {
        auto sourceY = static_cast<std::size_t>(
            static_cast<ImageReal>(y) / pixelHeight);
        sourceY = std::min(sourceY, height - 1U);
        for (std::size_t x = 0; x < newWidth; ++x) {
            output.set(x, y, base.get(sourceXs[x], sourceY));
        }
    }
    data = std::move(result);
    alpha = std::move(resultAlpha);
    return data;
}

lib::Bytes smoothResize(std::size_t newWidth, std::size_t newHeight,
                        lib::Bytes& data, lib::Bytes& alpha,
                        std::size_t depth, std::size_t width,
                        std::size_t height, std::size_t bytesPerLine,
                        std::size_t& newBytesPerLine) {
    const ::cwxeditor::lib::EditorPerformanceScope performance("image.resize.bilinear");
    newBytesPerLine = 0;
    if (width < 1U || height < 1U) return data;
    if (width == newWidth && height == newHeight) {
        newBytesPerLine = bytesPerLine;
        return data;
    }
    const auto bpp = bytesPerLine / width;
    validateImage(data, alpha, width, height, depth, bytesPerLine, bpp);
    const ImageReal pixelWidth = static_cast<ImageReal>(newWidth) /
                                   static_cast<ImageReal>(width);
    const ImageReal pixelHeight = static_cast<ImageReal>(newHeight) /
                                    static_cast<ImageReal>(height);
    newBytesPerLine = alignedStride(newWidth, bpp);
    lib::Bytes result(checkedMultiply(newHeight, newBytesPerLine,
                                      "smoothed image is too large"));
    lib::Bytes resultAlpha(
        alpha.empty() ? 0U
                      : checkedMultiply(newWidth, newHeight,
                                        "smoothed alpha is too large"));
    if (newWidth == 0U || newHeight == 0U) {
        data = std::move(result);
        alpha = std::move(resultAlpha);
        return data;
    }
    Pixels base(data, alpha, width, height, depth, bytesPerLine, bpp);
    Pixels output(result, resultAlpha, newWidth, newHeight, depth,
                  newBytesPerLine, bpp);
    struct HorizontalSample {std::int64_t base;ImageReal fraction,multiplier;};
    std::vector<HorizontalSample> horizontal;
    horizontal.reserve(newWidth);
    for(std::size_t x=0;x<newWidth;++x){
        const auto coordinate=ImageReal(x)/pixelWidth;
        const auto fraction=coordinate-static_cast<std::uint64_t>(coordinate);
        horizontal.push_back({static_cast<std::int64_t>(static_cast<std::int32_t>(coordinate)),fraction,ImageReal(1)-fraction});
    }
    for (std::size_t y = 0; y < newHeight; ++y) {
        const auto sourceYValue = static_cast<ImageReal>(y) / pixelHeight;
        // Dはバイリニア補間の基準座標をintへ変換する。最近傍補間のsize_tとは異なる。
        const auto baseY = static_cast<std::int64_t>(static_cast<std::int32_t>(sourceYValue));
        // Dは丸めたint32の基準座標とは独立にty %1.0を使う。
        const auto yFraction = sourceYValue - static_cast<std::uint64_t>(sourceYValue);
        const auto yMultiplier = ImageReal(1) - yFraction;
        for (std::size_t x = 0; x < newWidth; ++x) {
            const auto& sample=horizontal[x];
            const auto baseX=sample.base;
            const auto xFraction=sample.fraction;
            const auto xMultiplier=sample.multiplier;
            std::array<std::array<FC, 2>, 2> colors{};
            for (std::int32_t row = 0; row < 2; ++row) {
                for (std::int32_t column = 0; column < 2; ++column) {
                    auto sourceY = baseY + row;
                    auto sourceX = baseX + column;
                    sourceY = std::clamp<std::int64_t>(
                        sourceY, 0, toSigned(height - 1U));
                    sourceX = std::clamp<std::int64_t>(
                        sourceX, 0, toSigned(width - 1U));
                    colors[row][column] = base.get(
                        static_cast<std::size_t>(sourceX),
                        static_cast<std::size_t>(sourceY));
                }
            }
            const auto interpolate = [&](auto component) {
                return static_cast<std::int32_t>(
                    xMultiplier *
                        (component(colors[0][0]) * yMultiplier +
                         component(colors[1][0]) * yFraction) +
                    xFraction *
                        (component(colors[0][1]) * yMultiplier +
                         component(colors[1][1]) * yFraction));
            };
            FCu color{interpolate([](FC c) { return c.r; }),
                      interpolate([](FC c) { return c.g; }),
                      interpolate([](FC c) { return c.b; }),
                      interpolate([](FC c) { return c.a; })};
            round(color);
            output.set(x, y, color.fc());
        }
    }
    data = std::move(result);
    alpha = std::move(resultAlpha);
    return data;
}

lib::Bytes bordering(lib::Bytes data, lib::Bytes alpha, std::size_t depth,
                     std::size_t width, std::size_t height,
                     std::size_t bytesPerLine, const FC& a, const FC& b,
                     std::uint32_t bWidth) {
    if (width < 1U || height < 1U) return data;
    const auto bpp = bytesPerLine / width;
    Pixels base(data, alpha, width, height, depth, bytesPerLine, bpp);
    lib::Bytes result = data;
    lib::Bytes resultAlpha = alpha;
    Pixels output(result, resultAlpha, width, height, depth, bytesPerLine,
                  bpp);
    const auto lastX = width - 1U;
    const auto lastY = height - 1U;
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            if (base.get(x, y) == a) continue;
            bool found = false;
            found = found || (x > 0U && base.get(x - 1U, y) == a);
            found = found || (y > 0U && base.get(x, y - 1U) == a);
            found = found || (x < lastX && base.get(x + 1U, y) == a);
            found = found || (y < lastY && base.get(x, y + 1U) == a);
            found = found ||
                    (x > 0U && y > 0U && base.get(x - 1U, y - 1U) == a);
            found = found || (x > 0U && y < lastY &&
                              base.get(x - 1U, y + 1U) == a);
            found = found || (x < lastX && y > 0U &&
                              base.get(x + 1U, y - 1U) == a);
            found = found || (x < lastX && y < lastY &&
                              base.get(x + 1U, y + 1U) == a);
            if (!found) continue;

            const auto half = static_cast<std::int64_t>(bWidth / 2U);
            const auto signedX = toSigned(x);
            const auto signedY = toSigned(y);
            const auto beginX = std::max<std::int64_t>(0, signedX - half);
            const auto beginY = std::max<std::int64_t>(0, signedY - half);
            const auto endX = std::min<std::int64_t>(
                toSigned(width), signedX - half + bWidth);
            const auto endY = std::min<std::int64_t>(
                toSigned(height), signedY - half + bWidth);
            for (auto wy = beginY; wy < endY; ++wy) {
                for (auto wx = beginX; wx < endX; ++wx) {
                    output.set(static_cast<std::size_t>(wx),
                               static_cast<std::size_t>(wy), b);
                }
            }
        }
    }
    return result;
}

lib::Bytes adjustBordering(lib::Bytes data, lib::Bytes alpha,
                           std::size_t depth, std::size_t width,
                           std::size_t height, std::size_t bytesPerLine,
                           const FC& a, const FC& b, const FC& c) {
    if (width < 1U || height < 1U) return data;
    const auto bpp = bytesPerLine / width;
    Pixels base(data, alpha, width, height, depth, bytesPerLine, bpp);
    lib::Bytes result = data;
    lib::Bytes resultAlpha = alpha;
    Pixels output(result, resultAlpha, width, height, depth, bytesPerLine,
                  bpp);
    const auto lastX = width - 1U;
    const auto lastY = height - 1U;
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            if (!base.get(x, y).eqRgb(c)) continue;
            bool foundA = false;
            bool foundB = false;
            const auto inspect = [&](std::size_t nx, std::size_t ny) {
                const auto color = base.get(nx, ny);
                foundA = foundA || color.eqRgb(a);
                foundB = foundB || color.eqRgb(b);
            };
            if (x > 0U) inspect(x - 1U, y);
            if (x < lastX) inspect(x + 1U, y);
            if (y > 0U) inspect(x, y - 1U);
            if (y < lastY) inspect(x, y + 1U);
            if (foundA && foundB) output.set(x, y, a);
        }
    }
    return result;
}

lib::Bytes createAlphaDataFrom(const lib::Bytes& data, std::size_t depth,
                               std::size_t width, std::size_t height,
                               std::size_t bytesPerLine) {
    if (width < 1U || height < 1U) return data;
    lib::Bytes copy = data;
    lib::Bytes noAlpha;
    Pixels base(copy, noAlpha, width, height, depth, bytesPerLine,
                bytesPerLine / width);
    lib::Bytes alpha(checkedMultiply(width, height,
                                     "alpha buffer is too large"));
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            alpha[y * width + x] = base.get(x, y).r;
        }
    }
    return alpha;
}

}  // namespace cwxeditor::cwx::graphics
