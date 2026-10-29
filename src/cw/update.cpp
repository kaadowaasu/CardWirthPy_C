#include "cw/update.hpp"

#include "cw/data.hpp"
#include "cw/util.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {

using String = ::cw::lib::String;
using Path = std::filesystem::path;

// cw.util.join_paths()は意図的にパスの結合と区切り文字の
// 正規化だけを行い、..は解決しない。実際のファイル名ではこの挙動を保ち、
// UpdateInfo.xmlの対象を検証するときだけ字句的に正規化する。
String join_path(std::initializer_list<String> parts)
{
    ::cw::lib::Arguments values;
    values.reserve(parts.size());
    for (const auto& part : parts) {
        values.emplace_back(part);
    }
    return ::cw::util::join_paths(values);
}

String lexical_relative(String value)
{
    return ::cw::util::normalize_path(std::move(value));
}

// Pythonは../の接頭辞だけを検査し、単独の..では親へ移動できる
// 。ここでは両方を更新ルートの外への移動として扱い、親ディレクトリ全体
// の移動で、子要素に適用する明示的な許可検査を回避できないようにする。
bool has_parent_escape(const String& value)
{
    return value == ".." || value.rfind("../", 0) == 0;
}

// Pythonは空文字列を含む文字列の単純な接頭辞を許容する。ここでは空でな
// い許可ディレクトリを正規化し、パス要素の境界を要求する。../Scenario
// への許可が../ScenarioExtraにも適用されないようにする。
bool permitted_parent_path(const String& value, const std::vector<String>& permit)
{
    const auto normalized = lexical_relative(value);
    for (const auto& raw_prefix : permit) {
        if (raw_prefix.empty()) continue;
        const auto prefix = lexical_relative(raw_prefix);
        if (normalized == prefix) return true;
        // ネイティブ版ではパス要素の境界を保つ。Pyは単純な接頭
        // 辞を使う。許可ディレクトリが/で終わる場合は既に境界が
        // あるため、さらに/を足して正当な子要素を拒否しない。
        const auto descendant_prefix = prefix.back() == '/' ? prefix : prefix + "/";
        if (normalized.rfind(descendant_prefix, 0) == 0) return true;
    }
    return false;
}

bool check_targetpath_impl(const String& raw, const std::vector<String>& permit)
{
    // Pythonの実装は空のmovetoを受け付ける。空なら移動しない。
    if (raw.empty()) return true;

    const String normalized_input = ::cw::util::normalize_path(raw);
    const Path path = std::filesystem::u8path(normalized_input);
    // Windowsのis_absolute()はルートからのパスとドライブ文字付きパスの
    // 両方を扱う。プラットフォーム間の一貫性のためhas_root_name()も残す。
    if (path.is_absolute() || path.has_root_name() || path.has_root_directory()) return false;

    const auto normalized = lexical_relative(normalized_input);
    if (!has_parent_escape(normalized)) return true;
    return permitted_parent_path(normalized, permit);
}

bool regular_file(const String& path)
{
    std::error_code ec;
    return std::filesystem::is_regular_file(std::filesystem::u8path(path), ec);
}

bool directory(const String& path)
{
    std::error_code ec;
    return std::filesystem::is_directory(std::filesystem::u8path(path), ec);
}

void create_parent_directory(const String& path)
{
    const auto parent = std::filesystem::u8path(path).parent_path();
    if (parent.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec) throw std::filesystem::filesystem_error("create_directories", parent, ec);
}

} // namespace

namespace cw::update {
void update_files(::cw::lib::String arg_dpath, ::cw::lib::String arg_rmname, std::optional<std::vector<::cw::lib::String>> arg_permit)
{
    const auto permit = arg_permit.value_or(std::vector<String>{});
    const auto info_path = join_path({arg_dpath, "UpdateInfo.xml"});
    if (!regular_file(info_path)) return;
    // rmnameは削除処理に使う一時ツリーの構築に用いる。Python
    // の呼び出し元はバージョン名のような値を渡すが、ここではル
    // ートや親ディレクトリの要素を拒否し、不正な実行時引数で
    // Data/Tempの外へ処理先が変わらないようにする。
    if (!check_targetpath_impl(arg_rmname, {})) return;

    // 別の更新処理がロックを保持している場合、create_mutex()はfalseを返す。
    // 取得後のすべての例外ではPythonのfinally/release_mutexと同じ
    // 解放処理を行う。既に行った移動は、shutil.moveと同様に意図的に巻き戻さない。
    if (!::cw::util::create_mutex(arg_dpath)) return;
    try {
        const auto data = ::cw::data::xml2etree(info_path, "", std::nullopt, std::nullopt, false);
        const auto root = data ? data->getroot() : nullptr;
        const auto temp_root = join_path({"Data/Temp", arg_rmname + "_RemovedFiles"});
        if (root) {
            for (const auto& element : root->m_children) {
                if (!element || element->m_tag != "UpdateFile") continue;
                if (element->m_text.empty()) continue;

                const auto source = join_path({arg_dpath, element->m_text});
                // PythonはXMLパスの検証より前に
                // 移動元の存在を調べる。空や存在しない
                // 移動元の扱いも含め、この順序を保つ。
                if (!regular_file(source)) continue;
                if (!check_targetpath_impl(element->m_text, permit)) continue;

                const auto moveto_relative = element->get("moveto", "");
                if (!check_targetpath_impl(moveto_relative, permit)) continue;
                bool remove = element->getbool(".", ::cw::lib::Value("remove"), false);
                if (!moveto_relative.empty()) {
                    const auto target = join_path({arg_dpath, moveto_relative});
                    if (regular_file(target)) {
                        // 移動先が既に存在する場合、Pythonは
                        // そのまま残し、移動元を削除候補とする。
                        remove = true;
                    } else {
                        create_parent_directory(target);
                        ::cw::util::rename_file(source, target, false);
                        std::cout << "Auto Update: Move from " << source << " to " << target << ".\n";
                        continue;
                    }
                }

                if (!remove) continue;
                // 削除はマニフェストのチェックサムとの完全一致が
                // 条件。get_md5が例外を投げた場合もミュ
                // ーテックスを解放し、更新成功として扱わない。
                if (element->get("md5", "") != ::cw::util::get_md5(source)) continue;

                auto relative_for_trash = join_path({element->m_text});
                while (relative_for_trash.rfind("../", 0) == 0) relative_for_trash.erase(0, 3);
                const auto temporary = join_path({temp_root, relative_for_trash});
                create_parent_directory(temporary);
                ::cw::util::rename_file(source, temporary, false);
                std::cout << "Auto Update: Move from " << source << " to " << temporary << ".\n";
            }
        }

        if (directory(temp_root)) {
            // send_trashboxはシェルの失敗や中止を例外で通知する。
            // これはネイティブ版の意図的な差異で、util.cppを参照。Py
            // はここでその状態を無視するため、残ったツリーをゴミ箱へ移せなく
            // ても起動時の更新を中止しない。ファイルの移動自体は完了済み。
            try {
                ::cw::util::remove(temp_root, true);
                std::cout << "Auto Update: Remove " << temp_root << ".\n";
            } catch (const std::exception&) {
                ::cw::util::print_ex(std::nullopt);
            }
        }
    } catch (...) {
        ::cw::util::release_mutex();
        throw;
    }
    ::cw::util::release_mutex();
}

bool update_files_check_targetpath(::cw::lib::String arg_fpath)
{
    return check_targetpath_impl(arg_fpath, {});
}

void main()
{
}

}

