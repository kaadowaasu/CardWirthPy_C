#pragma once

// cw/update.pyに対応する更新処理。公開APIは小さ
// く保ち、 UpdateInfo.xmlはupdate_files()
// で解釈する。親ディレクトリへの逸脱を禁止する 既定の規
// 則を必要とする呼び出し元向けに、パス検査は公開する。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"

namespace cw::update {
void update_files(::cw::lib::String arg_dpath, ::cw::lib::String arg_rmname, std::optional<std::vector<::cw::lib::String>> arg_permit = std::nullopt);
bool update_files_check_targetpath(::cw::lib::String arg_fpath);
void main();
}

