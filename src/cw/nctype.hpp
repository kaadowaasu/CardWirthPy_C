#pragma once
#include "cw/lib/compat.hpp"
namespace cw::nctype {
extern ::cw::lib::String g_NC_ASCII_LATIN,g_NC_ASCII_NUMBER,g_NC_ASCII_SYMBOL,g_NC_CJK_KANJI;
extern ::cw::lib::String g_NC_CYRILLIC,g_NC_GREEK,g_NC_HIRAGANA,g_NC_HW_KATAKANA;
extern ::cw::lib::String g_NC_JIS_KANJI_1,g_NC_JIS_KANJI_2,g_NC_JIS_KANJI_3,g_NC_JIS_KANJI_4;
extern ::cw::lib::String g_NC_KATAKANA,g_NC_LATIN,g_NC_NUMBER,g_NC_SYMBOL,g_NC_UNKNOWN;
::cw::lib::String nctype(::cw::lib::String arg_c);
}

