#pragma once

#include "cwxeditor_src/lib/portable.hpp"

#include <cstdint>

namespace cwxeditor::cwx::sjis {

using String = ::cwxeditor::lib::String;
using StringView = ::cwxeditor::lib::StringView;

/// 対応するコードページ。バイト文字列のAPIではUNIをUTF-8とする。
enum class CP : std::uint8_t {
    SJIS,
    UNI,
};

/// Shift-JIS変換表を初期化する。スレッド安全
/// で繰り返し呼べ、 各変換関数から自動的に実行する。
void init();

/// コードポイントが指定のコードページで有効か返す。
bool valid(CP code_page, char32_t c) noexcept;

/// DのテンプレートAPI（valid!(CP.SJIS)(c)）に対応する、CPを特殊化した版。
template <CP CodePage>
[[nodiscard]] inline bool valid(char32_t c) noexcept {
    return valid(CodePage, c);
}

/// UnicodeコードポイントをShift-JISで表せるかどうか。
[[nodiscard]] bool canConvToSJIS(char32_t c) noexcept;

/// UTF-8（CP::UNI）とShift-JIS
/// （CP::SJIS）を相互に変換する。 戻り値
/// はバイト文字列。UTF-8からShift-JIS
/// への変換では、D版と同じく 変換表にない文字
/// を?へ置き換える。throwErrorはAPI
/// 互換用に残すが、この方向では使わない。
template <CP To, CP From>
[[nodiscard]] String convTo(StringView s,
                                 bool throwError = true,
                                 StringView unknownChar = "?");

/// UTF-8からShift-JISへ変換する。
[[nodiscard]] String tosjis(StringView s, bool throwError = true);

/// convTo<CP::SJIS, CP::UNI>の別名に相当するラッパー。
[[nodiscard]] String tosjism(StringView s, bool throwError = true);

/// Shift-JISからUTF-8へ変換する。
[[nodiscard]] String touni(StringView s,
                                bool throwError = true,
                                StringView unknownChar = "?");

/// convTo<CP::UNI, CP::SJIS>の別名に相当するラッパー。
[[nodiscard]] String tounim(StringView s,
                                 bool throwError = true,
                                 StringView unknownChar = "?");

/// UTF-8からNUL終端のShift-JISへ変換する。 戻り値のstd::string
/// は終端を含み、元の文字列にあった埋め込みNULも含み得る。
[[nodiscard]] String tosjisz(StringView s, bool throwError = true);

/// tosjismの別名に相当する、NUL終端のラッパー。
[[nodiscard]] String tosjismz(StringView s, bool throwError = true);

/// cがShift-JISの1バイトで表されるかどうか。
[[nodiscard]] bool isSJIS1ByteChar(char32_t c) noexcept;

namespace detail {

[[nodiscard]] String convertToSJIS(StringView s);
[[nodiscard]] String convertToUnicode(StringView s,
                                      bool throwError,
                                      StringView unknownChar);

}  // namespace detail

template <CP To, CP From>
[[nodiscard]] inline String convTo(StringView s,
                                        bool throwError,
                                        StringView unknownChar) {
    static_assert((To == CP::SJIS && From == CP::UNI) ||
                      (To == CP::UNI && From == CP::SJIS) ||
                      (To == From),
                  "unsupported code-page conversion");

    if constexpr (To == From) {
        return String(s);
    } else if constexpr (To == CP::SJIS) {
        (void)throwError;
        (void)unknownChar;
        return detail::convertToSJIS(s);
    } else {
        return detail::convertToUnicode(s, throwError, unknownChar);
    }
}

}  // namespace cwxeditor::cwx::sjis
