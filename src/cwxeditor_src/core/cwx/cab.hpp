#pragma once

#include "cwxeditor_src/lib/portable.hpp"

namespace cwxeditor::cwx::cab {

using String = ::cwxeditor::lib::String;
using StringView = ::cwxeditor::lib::StringView;
using IncludeCallback = ::cwxeditor::lib::Callback<bool(String)>;
using ExpandCallback = ::cwxeditor::lib::Callback<String(String)>;

/// Cabinetアーカイブの実装が利用可能か返す。
/// Cabinet対応はEditorのアーカイブ処
/// 理を通す。Windowsでは圧縮Cabinet
/// に SetupAPIのネイティブ実装を使える。
/// 無圧縮Cabinetは共通の読み込み器で扱う。
[[nodiscard]] bool canUncab() noexcept;

/// コピー元のツリーをCabinetアーカイブ
/// へ追加する。 コピー元はディレクトリでも単
/// 一ファイルでもよい。任意のコールバックは各
/// パスで呼ばれ、 falseならその項目を、
/// ディレクトリならその子孫も含めて除外する。
[[nodiscard]] bool cab(StringView source, StringView cabinet,
                       const IncludeCallback& include = {});

/// コピー元ディレクトリからの相対パスで保持するファイル内容。 元の
/// 作業ツリーを変更せず、対応する物理ファイルの内容より優先する。
struct MemoryFile {
    String path;
    ::cwxeditor::lib::Bytes data;
};
[[nodiscard]] bool cab(StringView source, StringView cabinet,
                       const IncludeCallback& include,
                       const ::cwxeditor::lib::Array<MemoryFile>& files);

/// Cabinetアーカイブをディレクトリへ展
/// 開する。 expandがあれば各項目のUTF-8
/// 名を渡し、返された名前を展開先に使う。
/// 空文字列が返された項目は省略する。
[[nodiscard]] bool uncab(StringView cabinet, StringView destination,
                         const ExpandCallback& expand = {});

/// 末尾の要素がfile_nameに一致する、アーカイブ内のUTF-8パスを
/// 返す。 Cabinetが不正、または該当項目がなければ空文字列を返す。
[[nodiscard]] String cabHasFile(StringView cabinet, StringView file_name);

}  // namespace cwxeditor::cwx::cab
