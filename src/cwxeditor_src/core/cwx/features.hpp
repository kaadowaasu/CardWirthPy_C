#pragma once

#include <compare>
#include <cstddef>
#include <functional>

namespace cwxeditor::cwx::features {

/// クーポンのタイプ。
enum class CouponType {
    Normal,    ///< 通常。
    Hide,      ///< 隠蔽クーポン。
    System,    ///< システムクーポン。
    Dur,       ///< 時限クーポン。
    DurBattle, ///< 戦闘中限定時限クーポン。
};

/// DのTypedef2はTを包む小さな値
/// 型。initが既定値を指定し、Tagで
/// 内容が同じ別名型も異なるC++型と
/// して区別する。Dのcookieも同じ
/// 目的で使われる。  Dのalias
/// value thisとmixin Proxy!value
/// は、変換・比較・代入・演算を内包
/// する値へ委ねる。 C++では変換
/// 演算子でこれらを提供し、左辺の変
/// 換では処理できない代入だけを 明
/// 示的なオーバーロードで実装する。
template <typename T, T init = T{}, typename Tag = void>
struct Typedef2 {
    using value_type = T;
    using tag_type = Tag;
    static constexpr T initial_value = init;

    T value = init;

    constexpr Typedef2() noexcept = default;
    constexpr explicit Typedef2(T new_value) noexcept : value(new_value) {}

    constexpr Typedef2& operator=(T new_value) noexcept {
        value = new_value;
        return *this;
    }

    constexpr operator T&() noexcept { return value; }
    constexpr operator const T&() const noexcept { return value; }

    constexpr bool operator==(const Typedef2&) const noexcept = default;
    constexpr auto operator<=>(const Typedef2&) const noexcept = default;

    /// Dのhash_tはsize_tの別名。
    [[nodiscard]] constexpr std::size_t toHash() const noexcept {
        return static_cast<std::size_t>(value);
    }
};

// Dのcookieは型の識別にだけ使う文字列。 C++で
// は状態を持たないタグで同じ区別を行い、値を保存しない。
struct SexTag {};
struct PeriodTag {};
struct NatureTag {};
struct MakingsTag {};

/// 性別。
using Sex = Typedef2<std::size_t, 0, SexTag>;

/// 年代。
using Period = Typedef2<std::size_t, 0, PeriodTag>;

/// 素質。
using Nature = Typedef2<std::size_t, 0, NatureTag>;

/// 特徴。
using Makings = Typedef2<std::size_t, 0, MakingsTag>;

}  // namespace cwxeditor::cwx::features

// Dの連想配列は値のラッパーにtoHashと等価比較を使う。 標
// 準のハッシュを特殊化し、バックエンドへの依存をこのモデルヘッダ
// ーへ持ち込まずに、 std::unordered_map/std::unordered_set
// のキーとして使えるようにする。
namespace std {

template <typename T, T Init, typename Tag>
struct hash<::cwxeditor::cwx::features::Typedef2<T, Init, Tag>> {
    [[nodiscard]] constexpr std::size_t operator()(
        const ::cwxeditor::cwx::features::Typedef2<T, Init, Tag>& value) const
        noexcept(noexcept(std::hash<T>{}(value.value))) {
        return std::hash<T>{}(value.value);
    }
};

}  // namespace std
