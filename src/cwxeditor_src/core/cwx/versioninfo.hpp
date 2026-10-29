#pragma once

#include <cstdint>

#include "cwxeditor_src/lib/portable.hpp"

namespace cwxeditor::cwx::versioninfo {

// Dの@version.txtの製品名ではなく、WebCardWirth
// の公開名・サイトを使う。 数値の形式互換バージョンは元のEditor
// の値を維持し、これらの文字列で形式選択は変えない。
inline const ::cwxeditor::lib::String APP_VERSION = "Version.9.0_cws";
inline constexpr std::uint64_t APP_VERSION_NUM = 2020062000ULL;
inline const ::cwxeditor::lib::String APP_WEB_SITE_URI =
    "https://webcardwirth.net/";

inline const ::cwxeditor::lib::String LATEST_VERSION = "6";
inline const ::cwxeditor::lib::String DEFAULT_VERSION = "6";

inline const ::cwxeditor::lib::Array<::cwxeditor::lib::String> VERSIONS{
    "6",
    "5",
    "4",
    "3",
    "2",
    "1",
    "",
};

inline const ::cwxeditor::lib::Array<::cwxeditor::lib::String> VERSION_NAMES{
    "Wsn.6",
    "Wsn.5",
    "Wsn.4",
    "Wsn.3",
    "Wsn.2",
    "Wsn.1",
    "Wsn.0",
};

inline const ::cwxeditor::lib::Array<::cwxeditor::lib::String> ENGINES{
    // このC++ Engineの形式はWsn.6とする。ここでは表示名だけを扱う。
    "CardWirthPy_C1(Wsn.6)",
    "CardWirthPy 5",
    "CardWirthPy 4",
    "CardWirthPy 3",
    "CardWirthPy 2",
    "CardWirthPy 1",
    "CardWirthPy 0.12.3",
};

inline const ::cwxeditor::lib::Array<::cwxeditor::lib::String> CLASSIC_VERSIONS{
    "1.60",
    "1.50",
    "1.30",
    "1.29",
    "1.28",
};

inline const ::cwxeditor::lib::Array<::cwxeditor::lib::String> CLASSIC_ENGINES{
    "CardWirthNext 1.60",
    "CardWirth 1.50",
    "CardWirth 1.30",
    "CardWirth 1.29",
    "CardWirth 1.28",
};

inline const ::cwxeditor::lib::Array<std::int32_t> IMAGE_SCALES{2, 4, 8, 16};

/// verがシナリオのdataVersion以下か返す。
[[nodiscard]] bool isTargetVersion(::cwxeditor::lib::StringView dataVersion,
                                   ::cwxeditor::lib::StringView ver);

}  // namespace cwxeditor::cwx::versioninfo
