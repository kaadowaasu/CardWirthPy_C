#include "cw/skin/util.hpp"
#include "cw/cw.hpp"
#include "cw/data.hpp"
#include "cw/header.hpp"
#include "cw/dialog/progress.hpp"
#include "cw/lib/archive.hpp"
#include "cw/lib/encoding.hpp"
#include "cw/util.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace cw::skin::util {
void SkinInstallError::initialize(::cw::lib::String arg_message)
{
    m_message=std::move(arg_message);
}

::cw::lib::Integer g_INSTALL_PROGRESS{3};
::cw::lib::Integer g_INSTALL_PROGRESS_ARCHIVE{5};

namespace {
using S2=::cw::lib::String;
using I2=::cw::lib::Integer;
using Element2=std::shared_ptr<::cw::data::CWPyElement>;
namespace fs2=std::filesystem;
thread_local ProgressObj* g_progress_object2=nullptr;

class ScopedProgressObject final {
public:
    explicit ScopedProgressObject(ProgressObj* current) noexcept
        : previous_(g_progress_object2) {
        g_progress_object2=current;
    }
    ~ScopedProgressObject() noexcept { g_progress_object2=previous_; }
    ScopedProgressObject(const ScopedProgressObject&)=delete;
    ScopedProgressObject& operator=(const ScopedProgressObject&)=delete;
private:
    ProgressObj* previous_{};
};

fs2::path path_from_utf82(const S2& value)
{
    return ::cw::lib::encoding::path_from_utf8(value);
}

S2 path_to_utf82(const fs2::path& value)
{
    return ::cw::lib::encoding::path_to_utf8(value);
}

struct DefaultProgressSession2 {
    std::shared_ptr<::cw::dialog::progress::SysProgressDialog> dialog{};

    void update(const S2& message, I2 amount)
    {
        if (!dialog) {
            dialog=std::make_shared<::cw::dialog::progress::SysProgressDialog>();
            dialog->initialize({},"スキンのインストール",message,100,0,false,420);
        } else {
            dialog->UpdateProgress(dialog->snapshot().value,message);
        }
        // 展開コールバックはファイル数に応じて呼ばれ、総量を事前確定
        // できないため、共通Presentationを不定進捗として駆動する。
        for (I2 i=0; i<std::max<I2>(1,amount); ++i) dialog->pulse();
    }

    void close()
    {
        if (!dialog) return;
        dialog->close();
        dialog.reset();
    }
};

thread_local DefaultProgressSession2 g_default_progress2{};

void report2(const ::cw::lib::Callback& callback, const S2& message, I2 progress_value)
{
    if (callback) callback({::cw::lib::Value(message),::cw::lib::Value(progress_value)},{});
}

I2 callback_integer2(const ::cw::lib::Arguments& args)
{
    if (args.empty()) throw std::invalid_argument("skin progress callback requires an integer");
    if (const auto* value=std::get_if<I2>(&args.front().storage())) return *value;
    if (const auto* value=std::get_if<bool>(&args.front().storage())) return *value ? 1 : 0;
    if (const auto* value=std::get_if<::cw::lib::Decimal>(&args.front().storage()))
        return value->to_integer(::cw::lib::DecimalRounding::Down);
    if (const auto* value=std::get_if<::cw::lib::Real>(&args.front().storage())) {
        if (!std::isfinite(*value)) throw std::invalid_argument("skin progress callback returned a non-finite Real");
        // 切り捨てる前に Real を Decimal の文字列へ変換する。 共通の
        // Decimal の範囲・桁あふれ検査を維持し、未検査のキャストを避ける。
        std::ostringstream text;
        text << std::setprecision(std::numeric_limits<::cw::lib::Real>::max_digits10) << *value;
        return ::cw::lib::Decimal::parse(text.str()).to_integer(::cw::lib::DecimalRounding::Down);
    }
    throw std::invalid_argument("skin progress callback argument is not an integer");
}

S2 lower2(S2 value)
{
    std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool supported_version2(const S2& value)
{
    if (value.empty()) return false;
    bool supported=false;
    std::apply([&](const auto&... version) {
        ((supported = supported || value == version), ...);
    }, ::cw::g_SUPPORTED_SKIN);
    return supported;
}

Element2 property_element2(const Element2& root)
{
    if (!root) return {};
    if (root->get_tag()=="Property") return root;
    const auto found=root->find("Property");
    return found && *found ? *found : Element2{};
}

std::optional<std::tuple<S2,S2,S2>> skininfo_from_property2(const ::cw::header::GetProperty& property)
{
    const auto root = property.m_attrs.find(std::nullopt);
    if (root == property.m_attrs.end()) return std::nullopt;
    const auto version = root->second.find("dataVersion");
    if (!supported_version2(version == root->second.end() ? "0" : version->second)) return std::nullopt;
    const auto value = [&](const S2& name) {
        const auto found = property.m_properties.find(name);
        return found == property.m_properties.end() ? S2{} : found->second;
    };
    return std::tuple<S2,S2,S2>{value("Name"), value("Author"), value("Type")};
}

S2 archive_entry_basename2(S2 name)
{
    std::replace(name.begin(),name.end(),'\\','/');
    const auto slash=name.find_last_of('/');
    return slash==S2::npos ? name : name.substr(slash+1);
}

std::optional<std::tuple<S2,S2,S2>> skininfo_from_archive2(const S2& path)
{
    const auto reader=::cw::lib::archive::Reader::open(path_from_utf82(path));
    for (const auto& entry:reader.entries()) {
        if (archive_entry_basename2(entry.name)!="Skin.xml") continue;
        const auto bytes=reader.read(entry);
        ::cw::lib::Bytes xml_bytes;
        xml_bytes.reserve(bytes.size());
        for (const auto byte : bytes)
            xml_bytes.push_back(std::to_integer<std::uint8_t>(byte));
        ::cw::header::GetProperty property;
        property.initialize("", ::cw::lib::Value(std::move(xml_bytes)), std::nullopt);
        const auto info=skininfo_from_property2(property);
        if (info) return info;
    }
    return std::nullopt;
}

std::optional<std::tuple<S2,S2,S2>> skininfo_from_cab2(const S2& path)
{
    // Reader は CAB を直接扱えるため、同時調査で安全でない
    // シェル展開や共有の Temp/Cab ディレクトリーは使わない。
    return skininfo_from_archive2(path);
}

bool archive_has_skin2(const S2& path)
{
    const auto reader=::cw::lib::archive::Reader::open(path_from_utf82(path));
    return std::any_of(reader.entries().begin(),reader.entries().end(),[](const auto& entry) {
        return archive_entry_basename2(entry.name)=="Skin.xml";
    });
}

void copy_directory2(const fs2::path& source, const fs2::path& target, bool overwrite)
{
    std::error_code ec;
    if (!fs2::is_directory(source,ec)) throw std::filesystem::filesystem_error("copy_directory",source,ec);
    if (!overwrite && fs2::exists(target,ec))
        throw std::filesystem::filesystem_error("copy_directory destination exists",target,
                                                std::make_error_code(std::errc::file_exists));
    fs2::create_directories(target,ec);
    if (ec) throw std::filesystem::filesystem_error("create_directories",target,ec);
    std::vector<std::pair<fs2::path,fs2::path>> directories{{source,target}};
    const auto canonical_source=fs2::canonical(source,ec);
    if (ec) throw std::filesystem::filesystem_error("canonical",source,ec);
    std::vector<fs2::path> canonical_ancestors{canonical_source};
    fs2::recursive_directory_iterator iterator(
        source,fs2::directory_options::follow_directory_symlink,ec),end;
    if (ec) throw std::filesystem::filesystem_error("recursive_directory_iterator",source,ec);
    for (; iterator!=end; iterator.increment(ec)) {
        if (ec) throw std::filesystem::filesystem_error("recursive_directory_iterator",source,ec);
        const auto entry=*iterator;
        // relative() は正規化により、たどったリンクの名前を参照先のパ
        // スへ置き換える。 Python の copytree(symlinks=False)
        // と同様、内容はたどるがリンクの字句上の位置は維持する。
        const auto relative=entry.path().lexically_relative(source);
        if (relative.empty() || relative.is_absolute() ||
            (!relative.empty() && *relative.begin()==".."))
            throw std::filesystem::filesystem_error(
                "copy_directory path escaped source",entry.path(),source,
                std::make_error_code(std::errc::invalid_argument));
        const auto destination=target/relative;
        const bool is_directory=entry.is_directory(ec);
        if (ec) throw std::filesystem::filesystem_error("status",entry.path(),ec);
        if (is_directory) {
            const auto depth=static_cast<std::size_t>(iterator.depth());
            if (canonical_ancestors.size()>depth+1)
                canonical_ancestors.resize(depth+1);
            const auto canonical_entry=fs2::canonical(entry.path(),ec);
            if (ec) throw std::filesystem::filesystem_error("canonical",entry.path(),ec);
            if (std::find(canonical_ancestors.begin(),canonical_ancestors.end(),canonical_entry)!=
                canonical_ancestors.end())
                throw std::filesystem::filesystem_error(
                    "copy_directory symbolic-link cycle",entry.path(),
                    std::make_error_code(std::errc::too_many_symbolic_link_levels));
            canonical_ancestors.push_back(canonical_entry);
            fs2::create_directories(destination,ec);
            if (ec) throw std::filesystem::filesystem_error("create_directories",destination,ec);
            directories.emplace_back(entry.path(),destination);
        } else if (entry.is_regular_file(ec)) {
            fs2::create_directories(destination.parent_path(),ec);
            if (ec) throw std::filesystem::filesystem_error("create_directories",destination.parent_path(),ec);
            const auto options=overwrite ? fs2::copy_options::overwrite_existing : fs2::copy_options::none;
            fs2::copy_file(entry.path(),destination,options,ec);
            if (ec) throw std::filesystem::filesystem_error("copy_file",entry.path(),destination,ec);
            fs2::permissions(destination,entry.status().permissions(),ec);
            if (ec) throw std::filesystem::filesystem_error("permissions",entry.path(),destination,ec);
            const auto write_time=fs2::last_write_time(entry.path(),ec);
            if (ec) throw std::filesystem::filesystem_error("last_write_time",entry.path(),ec);
            fs2::last_write_time(destination,write_time,ec);
            if (ec) throw std::filesystem::filesystem_error("last_write_time",destination,ec);
        }
    }
    if (ec) throw std::filesystem::filesystem_error("recursive_directory_iterator",source,ec);
    // copytree/copystatと同じく、子の作成後にディレクトリmetadataを戻す。
    for (auto it=directories.rbegin(); it!=directories.rend(); ++it) {
        fs2::permissions(it->second,fs2::status(it->first).permissions(),ec);
        if (ec) throw std::filesystem::filesystem_error("permissions",it->first,it->second,ec);
        const auto write_time=fs2::last_write_time(it->first,ec);
        if (ec) throw std::filesystem::filesystem_error("last_write_time",it->first,ec);
        fs2::last_write_time(it->second,write_time,ec);
        if (ec) throw std::filesystem::filesystem_error("last_write_time",it->second,ec);
    }
}

void remove_path2(const fs2::path& path)
{
    if (path.empty()) return;
    std::error_code ec;
    fs2::remove_all(path,ec);
}

S2 basename2(const fs2::path& path)
{
    const auto name=path_to_utf82(path.filename());
    return name.empty() ? path_to_utf82(path) : name;
}
}

std::vector<::cw::lib::String> find_skin(::cw::lib::String arg_name, ::cw::lib::String arg_author, ::cw::lib::String arg_skintype)
{
    std::vector<S2> result;
    const auto root=fs2::path("Data")/"Skin";
    std::vector<fs2::path> entries;
    for (const auto& entry:fs2::directory_iterator(root)) entries.push_back(entry.path());
    std::sort(entries.begin(),entries.end(),[](const auto& left,const auto& right) {
        return path_to_utf82(left.filename())<path_to_utf82(right.filename());
    });
    for (const auto& entry:entries) {
        const auto info=get_skininfo(path_to_utf82(entry));
        if (!info || std::get<0>(*info)!=arg_name || std::get<1>(*info)!=arg_author) continue;
        if (!arg_skintype.empty() && std::get<2>(*info)!=arg_skintype) continue;
        result.push_back(path_to_utf82(entry.filename()));
    }
    return result;
}

std::optional<std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String>> get_skininfo(::cw::lib::String arg_path)
{
    // get_skininfo() は Python の get_linktarget()
    // を呼ばない。 ファイルシステムのディレクトリーリンクは is_directory()
    // でたどるが、シェルの .lnk ファイルは通常のスキン以外のパスとして扱う。
    const auto path=path_from_utf82(arg_path);
    if (fs2::is_directory(path)) {
        try {
            const auto skin=path/"Skin.xml";
            if (!fs2::is_regular_file(skin)) return std::nullopt;
            ::cw::header::GetProperty property;
            property.initialize(path_to_utf82(skin), std::nullopt, std::nullopt);
            return skininfo_from_property2(property);
        } catch (...) {
            // Python版GetPropertyと同様、壊れた展開済みスキンだけは一覧から除外する。
            ::cw::util::print_ex(std::optional<::cw::lib::Value>(::cw::lib::Value(arg_path)));
            return std::nullopt;
        }
    }
    const auto extension=lower2(path.extension().generic_string());
    if (extension==".zip" || extension==".lzh")
        return skininfo_from_archive2(path_to_utf82(path));
    if (extension==".cab") return skininfo_from_cab2(arg_path);
    return std::nullopt;
}

bool is_skin(::cw::lib::String arg_path)
{
    try {
        const auto path=path_from_utf82(::cw::util::get_linktarget(arg_path));
        if (fs2::is_directory(path)) return fs2::is_regular_file(path/"Skin.xml");
        const auto extension=lower2(path.extension().generic_string());
        if (extension==".zip" || extension==".lzh" || extension==".cab")
            return archive_has_skin2(arg_path);
    } catch (...) {
        ::cw::util::print_ex(std::optional<::cw::lib::Value>(::cw::lib::Value(arg_path)));
    }
    return false;
}

void _progress_with_msg(::cw::lib::String arg_msg, ::cw::lib::Integer arg_progress)
{
    // Python の既定コールバックと同様、意図的に何もしない。
    (void)arg_msg;
    (void)arg_progress;
}

::cw::lib::String install_skin(::cw::lib::String arg_path, ::cw::lib::String arg_tempdir, ::cw::lib::Callback arg_progress)
{
    // 呼び出し側に見えるパスを維持する。 Python はインストール先の
    // Skin ディレクトリーを作る時、入力ディレクトリーの名前を使う。
    const fs2::path input=path_from_utf82(arg_path);
    const S2 display_name=basename2(input);
    fs2::path temporary;
    fs2::path skinpath;
    fs2::path source_directory;
    const bool default_progress=!arg_progress;
    if (default_progress) {
        g_default_progress2.close();
        arg_progress=[](const ::cw::lib::Arguments& args,const ::cw::lib::KeywordArguments&) {
            const S2 message=args.empty() ? S2{} : args.front().as_string();
            I2 amount=1;
            if (args.size()>1) {
                const ::cw::lib::Arguments amount_arg{args[1]};
                amount=callback_integer2(amount_arg);
            }
            _progress_with_msg(message,amount);
            return ::cw::lib::Value{};
        };
    }
    const auto report_progress=[&](const S2& message,I2 value) { report2(arg_progress,message,value); };
    try {
        if (fs2::is_regular_file(input)) {
            const auto extension=lower2(input.extension().generic_string());
            report_progress(display_name+" を展開中...",1);
            if (extension!=".zip" && extension!=".lzh" && extension!=".cab")
                throw SkinInstallError(display_name+" はスキンではありません。");
            ProgressObj progress_object;
            try {
                temporary=path_from_utf82(install_skin_decompress(path_to_utf82(input),arg_tempdir,
                    [&,display_name](const ::cw::lib::Arguments& args,const ::cw::lib::KeywordArguments&) {
                        progress_object.m_file_count=callback_integer2(args);
                        report_progress(display_name+" を展開中... (0/"+
                                        std::to_string(progress_object.m_file_count)+")",1);
                        return ::cw::lib::Value{};
                    },
                    [&,display_name](const ::cw::lib::Arguments& args,const ::cw::lib::KeywordArguments&) {
                        progress_object.m_decompress_count=callback_integer2(args);
                        report_progress(display_name+" を展開中... ("+
                                        std::to_string(progress_object.m_decompress_count)+"/"+
                                        std::to_string(progress_object.m_file_count)+")",1);
                        return ::cw::lib::Value(false);
                    }));
            } catch (...) {
                ::cw::util::print_ex(std::optional<::cw::lib::Value>(::cw::lib::Value(path_to_utf82(input))));
                throw SkinInstallError(display_name+" の展開に失敗しました。");
            }
            skinpath=temporary/"Skin.xml";
            if (!fs2::is_regular_file(skinpath)) {
                if (!temporary.empty()) {
                    std::error_code ec;
                    for (const auto& entry:fs2::recursive_directory_iterator(temporary,ec)) {
                        if (!ec && entry.is_regular_file(ec) && entry.path().filename()=="Skin.xml") {
                            skinpath=entry.path(); break;
                        }
                    }
                }
            }
            if (!fs2::is_regular_file(skinpath)) throw SkinInstallError(display_name+" はスキンのアーカイブではありません。");
            source_directory=skinpath.parent_path();
        } else {
            source_directory=input;
            skinpath=source_directory/"Skin.xml";
        }
        report_progress("スキン情報を読み込んでいます...",1);
        std::shared_ptr<::cw::data::CWPyElementTree> tree;
        try {
            tree=::cw::data::xml2etree(path_to_utf82(skinpath),"",std::nullopt,std::nullopt,false);
        } catch (...) {
            ::cw::util::print_ex(std::optional<::cw::lib::Value>(::cw::lib::Value(path_to_utf82(skinpath))));
            throw SkinInstallError(display_name+" のスキン情報の読み込みに失敗しました。");
        }
        if (!tree || !tree->getroot()) throw SkinInstallError(display_name+" のスキン情報の読み込みに失敗しました。");
        if (!supported_version2(tree->getroot()->get("dataVersion","0")))
            throw SkinInstallError(display_name+" は未対応のデータバージョンです。CardWirthPy本体をアップグレードしてください。");
        const auto property=property_element2(tree->getroot());
        if (!property) throw SkinInstallError(display_name+" のスキン情報の読み込みに失敗しました。");
        const auto info=std::tuple<S2,S2,S2>{property->gettext("Name",""),property->gettext("Author",""),property->gettext("Type","")};
        const auto base_data=property->find("BaseSkin");
        const bool has_base=base_data && *base_data;
        std::vector<S2> base_names;
        if (has_base) {
            const auto base_name=(*base_data)->gettext("Name","");
            const auto base_author=(*base_data)->gettext("Author","");
            const auto base_type=(*base_data)->gettext("Type","");
            base_names=find_skin(base_name,base_author,base_type);
            if (base_names.empty()) {
                S2 details=" スキン名 = "+(base_name.empty()?S2("(無し)"):base_name);
                details+="\n 作者名 = "+(base_author.empty()?S2("(無し)"):base_author);
                if (!base_type.empty()) details+="\n タイプ = "+base_type;
                throw SkinInstallError(display_name+" のベースとなるスキンが見つかりません。\n"+details);
            }
        }
        // Python の dirname(join(directory,"Skin.xml"))
        // は末尾の区切りを除く。 どちらの入力形式も、解決した Skin.xml の親を使う。
        const auto source_name=path_to_utf82(skinpath.parent_path().filename());
        const auto destination=path_from_utf82(::cw::util::dupcheck_plus(
            path_to_utf82(fs2::path("Data")/"Skin"/path_from_utf82(source_name)),false,false));
        S2 display_skin_name=std::get<0>(info).empty() ? S2("(無名のスキン)") : std::get<0>(info);
        if (!std::get<1>(info).empty()) display_skin_name+="("+std::get<1>(info)+")";
        try {
            if (has_base) {
                report_progress("「"+display_skin_name+"」のベーススキンをコピーしています...",1);
                copy_directory2(fs2::path("Data")/"Skin"/path_from_utf82(base_names.front()),destination,false);
                report_progress("「"+display_skin_name+"」のデータをコピーしています...",1);
                copy_directory2(source_directory,destination,true);
            } else {
                report_progress("「"+display_skin_name+"」のデータをコピーしています...",2);
                copy_directory2(source_directory,destination,false);
            }
        } catch (const SkinInstallError&) {
            remove_path2(destination); throw;
        } catch (...) {
            ::cw::util::print_ex(std::optional<::cw::lib::Value>(::cw::lib::Value(path_to_utf82(input))));
            remove_path2(destination);
            throw SkinInstallError(display_name+" のコピーに失敗しました。");
        }
        if (!temporary.empty()) {
            report_progress("一時ファイルを削除しています...",1);
            remove_path2(temporary);
            temporary.clear();
        }
        if (default_progress) g_default_progress2.close();
        return path_to_utf82(destination);
    } catch (...) {
        if (!temporary.empty()) {
            report_progress("一時ファイルを削除しています...",1);
            remove_path2(temporary);
        }
        if (default_progress) g_default_progress2.close();
        throw;
    }
}

::cw::lib::String install_skin_decompress(::cw::lib::String arg_path, ::cw::lib::String arg_tempdir, ::cw::lib::Callback arg_startup, ::cw::lib::Callback arg_progress)
{
    const auto extension=lower2(path_to_utf82(path_from_utf82(arg_path).extension()));
    const bool is_cab=extension==".cab";
    if (!is_cab && extension!=".zip" && extension!=".lzh")
        throw SkinInstallError(basename2(path_from_utf82(arg_path))+" はスキンではありません。");
    auto progress_object=std::make_shared<ProgressObj>();
    const ScopedProgressObject active_progress(progress_object.get());
    const auto startup=[&](const ::cw::lib::Arguments& args,const ::cw::lib::KeywordArguments&) {
        progress_object->m_file_count=callback_integer2(args);
        if (arg_startup) arg_startup({::cw::lib::Value(progress_object->m_file_count)},{});
        return ::cw::lib::Value{};
    };
    const auto progress_arc=[&](const ::cw::lib::Arguments& args,const ::cw::lib::KeywordArguments&) {
        progress_object->m_decompress_count=callback_integer2(args);
        if (arg_progress) arg_progress({::cw::lib::Value(progress_object->m_decompress_count)},{});
        return ::cw::lib::Value(false);
    };
    {
        const auto result=is_cab
            ? ::cw::util::decompress_cab(arg_path,arg_tempdir,"",startup,progress_arc,true)
            : ::cw::util::decompress_zip(arg_path,arg_tempdir,"",startup,progress_arc,true,std::nullopt);
        return result;
    }}

void ProgressObj_install_skin___init__(::cw::lib::Value arg_self)
{
    const auto object=std::get_if<::cw::lib::ObjectPtr>(&arg_self.storage());
    if (!object || !*object) return;
    const auto progress_object=std::dynamic_pointer_cast<ProgressObj>(*object);
    if (!progress_object) return;
    progress_object->m_file_count=0;
    progress_object->m_decompress_count=0;
    g_progress_object2=progress_object.get();
}

void install_skin_startup(::cw::lib::Integer arg_num)
{
    if (g_progress_object2) g_progress_object2->m_file_count=arg_num;
}

bool install_skin_progress_arc(::cw::lib::Integer arg_num)
{
    if (g_progress_object2) g_progress_object2->m_decompress_count=arg_num;
    return false;
}

void main()
{
    // Python版も直接実行時の処理を持たない（module API専用）。
}

}

