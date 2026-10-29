#pragma once
#include "replacedialog.hpp"
#include "3rdparty/srell/srell.hpp"
namespace cwxeditor::cwx::editor::gui::dwt::replacedialog {
// Dは常にgm/gimでコンパイルする。検証・件数取得・置換・検索前の絞り込みでフラグを共
// 通化する。 ここで対象にするのはLFによる複数行であり、すべての正規表現の方言ではない。
inline auto replaceRegexOptions(const ReplaceOptions& options) {
    auto flags = srell::regex_constants::ECMAScript | srell::regex_constants::multiline;
    if (!options.notIgnoreCase) flags |= srell::regex_constants::icase;
    return flags;
}
}
