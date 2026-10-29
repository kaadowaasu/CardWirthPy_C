#pragma once

#include "cwxeditor_src/lib/portable.hpp"

#include <cstddef>
#include <cstdint>
#include <random>
#include <type_traits>

namespace cwxeditor::cwx::jpy {

// これらの列挙型はcwx.jpyで宣言する。前方宣言を使い、
// 低水準の画像モジュールを上位のJPYパーサーへ依存させない。
enum class Colormap : std::int32_t;
enum class Colorexchange : std::int32_t;
enum class Filter : std::int32_t;
enum class Mask : std::int32_t;
enum class Noise : std::int32_t;
enum class Turn : std::int32_t;

}  // namespace cwxeditor::cwx::jpy

namespace cwxeditor::cwx::graphics {

using Colormap = jpy::Colormap;
using Colorexchange = jpy::Colorexchange;
using Filter = jpy::Filter;
using Mask = jpy::Mask;
using Noise = jpy::Noise;
using Turn = jpy::Turn;

struct FC {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 0;

    [[nodiscard]] bool eqRgb(const FC& other) const noexcept;
    friend bool operator==(const FC& left, const FC& right) noexcept;
    friend bool operator!=(const FC& left, const FC& right) noexcept {
        return !(left == right);
    }
};

struct FCu {
    std::int32_t r = 0;
    std::int32_t g = 0;
    std::int32_t b = 0;
    std::int32_t a = 0;

    [[nodiscard]] FC fc() const noexcept;
};

struct FCf {
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    double a = 0.0;

    [[nodiscard]] bool eqRgb(const FC& other) const noexcept;
};

// 直接色のビットマップと、任意の独立したアルファ平面への
// 参照。 Dのスライスと同じく、元データの共有を保つ。
struct Pixels {
    lib::Bytes& data;
    lib::Bytes& alpha;
    std::size_t width = 0;
    std::size_t height = 0;
    std::size_t depth = 0;
    std::size_t bytesPerLine = 0;
    std::size_t bpp = 0;

    Pixels();
    Pixels(lib::Bytes& data, lib::Bytes& alpha, std::size_t width,
           std::size_t height, std::size_t depth, std::size_t bytesPerLine,
           std::size_t bpp = 0);

    [[nodiscard]] FC get(std::size_t x, std::size_t y) const;
    void set(std::size_t x, std::size_t y, std::uint8_t r, std::uint8_t g,
             std::uint8_t b, std::uint8_t a);
    void set(std::size_t x, std::size_t y, std::uint32_t r,
             std::uint32_t g, std::uint32_t b, std::uint32_t a);
    void set(std::size_t x, std::size_t y, const FC& color);
    void swap(std::size_t x1, std::size_t y1, std::size_t x2,
              std::size_t y2);
};

void turn(lib::Bytes& data, lib::Bytes& alpha, std::size_t& width,
          std::size_t& height, std::size_t& bytesPerLine, Turn effect,
          std::size_t depth);
[[nodiscard]] lib::Bytes flip(lib::Bytes& data, lib::Bytes& alpha,
                              std::size_t depth, std::size_t width,
                              std::size_t height,
                              std::size_t bytesPerLine);
[[nodiscard]] lib::Bytes mirror(lib::Bytes& data, lib::Bytes& alpha,
                                std::size_t depth, std::size_t width,
                                std::size_t height,
                                std::size_t bytesPerLine);

void round(FCu& rgb) noexcept;

// Dモジュールで公開されている補助関数を、型付きテンプレートとして保持する。
template <typename T>
void pixelProcImpl(T effect, FCu& rgb) {
    using Effect = std::remove_cv_t<T>;
    const auto value = static_cast<std::int32_t>(effect);
    if constexpr (std::is_same_v<Effect, Colorexchange>) {
        switch (value) {
        case 1: {
            const auto r = rgb.r;
            rgb.r = rgb.g;
            rgb.g = rgb.b;
            rgb.b = r;
            break;
        }
        case 2: {
            const auto r = rgb.r;
            rgb.r = rgb.b;
            rgb.b = rgb.g;
            rgb.g = r;
            break;
        }
        case 3:
            std::swap(rgb.r, rgb.g);
            break;
        case 4:
            std::swap(rgb.r, rgb.b);
            break;
        case 5:
            std::swap(rgb.g, rgb.b);
            break;
        default:
            break;
        }
    } else if constexpr (std::is_same_v<Effect, Colormap>) {
        if (value == 0) return;
        const auto gray = (rgb.r + rgb.g + rgb.b) / 3;
        if (value == 1) {
            rgb.r = gray;
            rgb.g = gray;
            rgb.b = gray;
            return;
        }
        static constexpr std::int32_t offsets[][3] = {
            {0, 0, 0},       {0, 0, 0},       {30, 0, -30},
            {255, 0, 30},    {255, 0, 0},     {0, 255, 0},
            {0, 0, 255},     {191, 191, 0},   {191, 0, 191},
            {0, 191, 191},   {0, -255, -255}, {-255, 0, -255},
            {-255, -255, 0}, {0, 0, -255},    {0, -255, 0},
            {-255, 0, 0},
        };
        const auto index = (value >= 0 && value < 16) ? value : 0;
        rgb.r = gray + offsets[index][0];
        rgb.g = gray + offsets[index][1];
        rgb.b = gray + offsets[index][2];
        round(rgb);
    } else if constexpr (std::is_same_v<Effect, Filter>) {
        if (value == 7) {
            const auto result = (rgb.r == 0 && rgb.g == 0 && rgb.b == 0)
                                    ? 255
                                    : 0;
            rgb.r = result;
            rgb.g = result;
            rgb.b = result;
        } else if (value == 9) {
            rgb.r = 255 - rgb.r;
            rgb.g = 255 - rgb.g;
            rgb.b = 255 - rgb.b;
        }
    }
}

[[nodiscard]] lib::Bytes colorexchange(
    Colorexchange effect, lib::Bytes& data, lib::Bytes& alpha,
    std::size_t depth, std::size_t width, std::size_t height,
    std::size_t bytesPerLine);
[[nodiscard]] lib::Bytes colormap(Colormap effect, lib::Bytes& data,
                                  lib::Bytes& alpha, std::size_t depth,
                                  std::size_t width, std::size_t height,
                                  std::size_t bytesPerLine);
[[nodiscard]] lib::Bytes emboss(lib::Bytes& data, lib::Bytes& alpha,
                                std::size_t depth, std::size_t width,
                                std::size_t height,
                                std::size_t bytesPerLine);
[[nodiscard]] lib::Bytes deffusion(lib::Bytes& data, lib::Bytes& alpha,
                                   std::size_t depth, std::size_t width,
                                   std::size_t height,
                                   std::size_t bytesPerLine);
[[nodiscard]] lib::Bytes filter(Filter effect, lib::Bytes& data,
                                lib::Bytes& alpha, std::size_t depth,
                                std::size_t width, std::size_t height,
                                std::size_t bytesPerLine);
[[nodiscard]] lib::Bytes mask(Mask effect, lib::Bytes& data, lib::Bytes& alpha,
                              std::size_t depth, std::size_t width,
                              std::size_t height,
                              std::size_t bytesPerLine);
void noiseImpl(std::mt19937& random, Noise effect, FCu& rgb,
               std::int32_t value);
[[nodiscard]] lib::Bytes noise(Noise effect, std::int32_t value,
                               lib::Bytes& data, lib::Bytes& alpha,
                               std::size_t depth, std::size_t width,
                               std::size_t height,
                               std::size_t bytesPerLine);
[[nodiscard]] lib::Bytes resize(std::size_t newWidth, std::size_t newHeight,
                                lib::Bytes& data, lib::Bytes& alpha,
                                std::size_t depth, std::size_t width,
                                std::size_t height,
                                std::size_t bytesPerLine,
                                std::size_t& newBytesPerLine);
[[nodiscard]] lib::Bytes smoothResize(
    std::size_t newWidth, std::size_t newHeight, lib::Bytes& data,
    lib::Bytes& alpha, std::size_t depth, std::size_t width,
    std::size_t height, std::size_t bytesPerLine,
    std::size_t& newBytesPerLine);
[[nodiscard]] lib::Bytes bordering(
    lib::Bytes data, lib::Bytes alpha, std::size_t depth, std::size_t width,
    std::size_t height, std::size_t bytesPerLine, const FC& a, const FC& b,
    std::uint32_t bWidth);
[[nodiscard]] lib::Bytes adjustBordering(
    lib::Bytes data, lib::Bytes alpha, std::size_t depth, std::size_t width,
    std::size_t height, std::size_t bytesPerLine, const FC& a, const FC& b,
    const FC& c);
[[nodiscard]] lib::Bytes createAlphaDataFrom(
    const lib::Bytes& data, std::size_t depth, std::size_t width,
    std::size_t height, std::size_t bytesPerLine);

}  // namespace cwxeditor::cwx::graphics
