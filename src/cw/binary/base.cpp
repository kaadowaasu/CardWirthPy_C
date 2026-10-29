// cw/binary/base.py に対応するバイナリ変換の共通処理。
#include "cw/binary/base.hpp"
#include "cw/binary/beast.hpp"

#include "cw/binary/cwfile.hpp"
#include "cw/binary/image.hpp"
#include "cw/binary/util.hpp"
#include "cw/cw.hpp"
#include "cw/data.hpp"
#include "cw/lib/encoding.hpp"
#include "cw/lib/imagecodec.hpp"
#include "cw/thread.hpp"
#include "cw/util.hpp"
#include "platform/storage/storage_mount.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace cw::binary::base {
namespace {

thread_local std::optional<std::vector<::cw::lib::String>> g_image_import_roots;

::cw::lib::String safe_filename(::cw::lib::String value) {
    return ::cw::binary::util::check_filename(std::move(value));
}

::cw::lib::String join_paths(std::initializer_list<::cw::lib::String> parts) {
    ::cw::lib::Arguments values;
    values.reserve(parts.size());
    for (const auto& part : parts) {
        values.emplace_back(part);
    }
    return ::cw::binary::util::join_paths(values);
}

::cw::lib::String repl_escapechar(::cw::lib::String value) {
    const std::array<std::pair<::cw::lib::String, ::cw::lib::String>, 5> replacements{{
        {"&", "&amp;"}, {"<", "&lt;"}, {">", "&gt;"},
        {"\"", "&quot;"}, {"'", "&apos;"}}};
    for (const auto& [from, to] : replacements) {
        std::size_t pos = 0;
        while ((pos = value.find(from, pos)) != ::cw::lib::String::npos) {
            value.replace(pos, from.size(), to); pos += to.size();
        }
    }
    return value;
}

::cw::lib::String value_string(const ::cw::lib::Value& value) {
    if (const auto* string = std::get_if<::cw::lib::String>(&value.storage())) return *string;
    return {};
}

::cw::lib::Bytes convert_image_to_bmp(::cw::lib::Bytes image, bool convertbitmap) {
    // cw.binary.base.import_image() と
    // 同様、BMP以外は画像を開き、変換して保存する。 変換無効な
    // ら元バイトを変更せず、BMPもバイト単位で維持する。 要求さ
    // れた非BMPの経路だけで、RGBA復号・BMP符号化を行う。
    if (!convertbitmap) return image;
    const auto info = ::cw::lib::imagecodec::inspect(image);
    if (info.format == ::cw::lib::imagecodec::Format::BMP) return image;
    const auto rgba = ::cw::lib::imagecodec::decode_rgba(image);
    return ::cw::lib::imagecodec::encode_bmp(rgba);
}

template <std::size_t N>
::cw::lib::String enum_forward(::cw::lib::Integer value,
                               const std::array<::cw::lib::String, N>& values,
                               const ::cw::lib::String& fpath) {
    if (value < 0 || value >= static_cast<::cw::lib::Integer>(N))
        throw std::invalid_argument(fpath);
    return values[static_cast<std::size_t>(value)];
}

template <std::size_t N>
::cw::lib::Integer enum_reverse(const ::cw::lib::String& value,
                                const std::array<::cw::lib::String, N>& values,
                                const ::cw::lib::String& message) {
    for (std::size_t i = 0; i < N; ++i)
        if (values[i] == value) return static_cast<::cw::lib::Integer>(i);
    throw ::cw::binary::cwfile::UnsupportedError(std::nullopt,
        message + "「" + value + "」");
}

const std::vector<std::pair<::cw::lib::String,::cw::lib::String>>& content_types() {
    static const std::vector<std::pair<::cw::lib::String,::cw::lib::String>> v{
        {"Start",""},{"Link","Start"},{"Start","Battle"},{"End",""},{"End","BadEnd"},
        {"Change","Area"},{"Talk","Message"},{"Play","Bgm"},{"Change","BgImage"},{"Play","Sound"},
        {"Wait",""},{"Effect",""},{"Branch","Select"},{"Branch","Ability"},{"Branch","Random"},
        {"Branch","Flag"},{"Set","Flag"},{"Branch","MultiStep"},{"Set","Step"},{"Branch","Cast"},
        {"Branch","Item"},{"Branch","Skill"},{"Branch","Info"},{"Branch","Beast"},{"Branch","Money"},
        {"Branch","Coupon"},{"Get","Cast"},{"Get","Item"},{"Get","Skill"},{"Get","Info"},
        {"Get","Beast"},{"Get","Money"},{"Get","Coupon"},{"Lose","Cast"},{"Lose","Item"},
        {"Lose","Skill"},{"Lose","Info"},{"Lose","Beast"},{"Lose","Money"},{"Lose","Coupon"},
        {"Talk","Dialog"},{"Set","StepUp"},{"Set","StepDown"},{"Reverse","Flag"},{"Branch","Step"},
        {"Elapse","Time"},{"Branch","Level"},{"Branch","Status"},{"Branch","PartyNumber"},{"Show","Party"},
        {"Hide","Party"},{"Effect","Break"},{"Call","Start"},{"Link","Package"},{"Call","Package"},
        {"Branch","Area"},{"Branch","Battle"},{"Branch","CompleteStamp"},{"Get","CompleteStamp"},{"Lose","CompleteStamp"},
        {"Branch","Gossip"},{"Get","Gossip"},{"Lose","Gossip"},{"Branch","IsBattle"},{"Redisplay",""},
        {"Check","Flag"},{"Substitute","Step"},{"Substitute","Flag"},{"Branch","StepValue"},{"Branch","FlagValue"},
        {"Branch","RandomSelect"},{"Branch","KeyCode"},{"Check","Step"},{"Branch","Round"}};
    return v;
}

}  // namespace

ImageImportScope::ImageImportScope(::cw::lib::String tempdir, ::cw::lib::String yadodir)
    : m_previous(g_image_import_roots)
{
    // Python版のプロセス全体の切り替えと異なり、並行するUI・ワーカーの処理を分離する。
    g_image_import_roots = std::vector<::cw::lib::String>{std::move(tempdir), std::move(yadodir)};
}

ImageImportScope::~ImageImportScope()
{
    g_image_import_roots = std::move(m_previous);
}

void CWBinaryBase::initialize(std::optional<std::shared_ptr<::cw::binary::base::CWBinaryBase>> arg_parent, std::optional<std::shared_ptr<::cw::binary::cwfile::CWFile>> arg_f, bool arg_yadodata, ::cw::lib::String arg_materialdir, bool arg_image_export)
{
    set_root(arg_parent);
    m_xmltype = "CWBinaryBase";
    m_fpath = arg_f && *arg_f ? (*arg_f)->m_filename : "";
    m_materialbasedir.clear();
    set_materialdir(std::move(arg_materialdir));
    set_image_export(arg_image_export, false);
    m_yadodb = std::nullopt;
    m_xmlpath.clear();
    m_name.clear();
    m_id = 0;
    m_image = std::nullopt;
    m_dir.clear();
    m_imgdir.clear();
    // Python版と同様、子の引数が省略・既定falseで
    // も親のモードを引き継ぐ。 ネイティブの呼び出し元が明示
    // 的にモードを渡しても、子の有効モードは変えない。 arg_yadodata
    // を使うのはルートの初期化だけ。
    m_yadodata = arg_parent && *arg_parent ? (*arg_parent)->m_yadodata : arg_yadodata;
}

void CWBinaryBase::set_root(std::optional<std::shared_ptr<::cw::binary::base::CWBinaryBase>> arg_parent)
{
    if (arg_parent && *arg_parent) {
        if (auto root = (*arg_parent)->m_root.lock()) m_root = root;
        else m_root = *arg_parent;
    } else {
        m_root.reset();
    }
}

std::shared_ptr<::cw::binary::base::CWBinaryBase> CWBinaryBase::get_root()
{
    // Python版もルートを弱参照にするが、失効するとassert
    // で失敗する。 ネイティブの共有所有では祖先の破棄後も子が
    // 残り得るため、その境界では子自身をルートとして保持する。
    if (auto root = m_root.lock()) return root;
    return shared_from_this();
}

void CWBinaryBase::set_dir(::cw::lib::String arg_path)
{
    get_root()->m_dir = std::move(arg_path);
}

::cw::lib::String CWBinaryBase::get_dir()
{
    return get_root()->m_dir;
}

void CWBinaryBase::set_imgdir(::cw::lib::String arg_path)
{
    get_root()->m_imgdir = std::move(arg_path);
}

::cw::lib::String CWBinaryBase::get_imgdir()
{
    return get_root()->m_imgdir;
}

::cw::lib::String CWBinaryBase::get_fname()
{
    auto name = ::cw::lib::encoding::path_to_utf8(
        std::filesystem::u8path(m_fpath).filename());
    // Python版と同様、ドットだけの名前も含め、cw.util.splitext
    // に委譲する。 ファイル名の処理は同じ共通の分割規則を使う。
    return std::get<0>(::cw::util::splitext(name));
}

bool CWBinaryBase::is_root()
{
    return get_root().get() == this;
}

bool CWBinaryBase::is_yadodata()
{
    return m_yadodata;
}

void CWBinaryBase::set_materialdir(::cw::lib::String arg_materialdir)
{
    m__materialdir = std::move(arg_materialdir);
}

::cw::lib::String CWBinaryBase::get_materialdir()
{
    auto root = get_root();
    return root.get() == this ? m__materialdir : root->m__materialdir;
}

void CWBinaryBase::set_image_export(bool arg_image_export, bool arg_force)
{
    m__image_export = arg_image_export;
    m__force_exportsetting = arg_force;
}

bool CWBinaryBase::get_image_export()
{
    if (m__force_exportsetting) return m__image_export;
    auto root = get_root();
    return root.get() == this ? m__image_export : root->m__image_export;
}

::cw::lib::String CWBinaryBase::create_xml(::cw::lib::String arg_dpath)
{
    set_dir(arg_dpath);
    ::cw::lib::String path_text;
    const auto destination = get_dir();
    if (m_xmltype == "Summary" || m_xmltype == "Environment") {
        path_text = ::cw::binary::util::check_duplicate(
            join_paths({destination, m_xmltype + ".xml"}));
    } else if (m_xmltype == "Party") {
        const auto partydir = ::cw::binary::util::check_duplicate(
            join_paths({destination, "Party", safe_filename(m_name)}));
        path_text = join_paths({partydir, "Party.xml"});
    } else {
        auto filename = safe_filename(m_name) + ".xml";
        if (!is_yadodata()) {
            std::ostringstream prefix;
            prefix << std::setw(2) << std::setfill('0') << m_id << '_';
            filename = prefix.str() + filename;
        }
        const auto candidate = join_paths({destination, m_xmltype, filename});
        path_text = ::cw::binary::util::check_duplicate(candidate);
    }
    const auto output_path = std::filesystem::u8path(path_text);
    if (!output_path.parent_path().empty()) {
        if (::cardwirth::platform::is_mounted_user_path(output_path)) {
            if (!::cardwirth::platform::make_mounted_user_directory(
                    output_path.parent_path(), true))
                throw std::runtime_error("failed to create mounted XML directory: " + path_text);
        } else {
            std::filesystem::create_directories(output_path.parent_path());
        }
    }
    auto data = get_data();
    if (!data) throw std::runtime_error("get_data returned null for " + m_xmltype);
    ::cw::data::CWPyElementTree(data).write_file(path_text);
    m_xmlpath = path_text;
    return m_xmlpath;
}

::cw::lib::String CWBinaryBase::export_image()
{
    if (!m_image || m_image->empty()) return {};
    if (!get_image_export()) {
        return ::cw::binary::image::data_to_code(*m_image);
    }
    auto basedir = m_materialbasedir.empty() ? get_dir() : m_materialbasedir;
    ::cw::lib::String imgdir;
    if (m_xmltype == "Summary") {
        imgdir = basedir;
    } else if (const auto* beast = dynamic_cast<::cw::binary::beast::BeastCard*>(this);
               m_xmltype == "BeastCard" && beast && beast->m_summoneffect) {
        // Pythonの Base.export_image と同様、通常のカード
        // だけでなく召喚獣もここで扱い、 ルートディレクトリと完了経路を共有する。
        imgdir = get_imgdir();
        const auto root = get_root();
        if (basedir.empty()) basedir = root->m_materialbasedir;
        if (imgdir.empty()) {
            imgdir = join_paths({basedir, get_materialdir(), root->m_xmltype,
                                 safe_filename(root->m_name)});
            imgdir = ::cw::binary::util::check_duplicate(imgdir);
            set_imgdir(imgdir);
        }
    } else if (m_xmltype == "Adventurer" || m_xmltype == "SkillCard" ||
               m_xmltype == "ItemCard" || m_xmltype == "BeastCard" || m_xmltype == "CastCard") {
        imgdir = join_paths({basedir, get_materialdir(), m_xmltype, safe_filename(m_name)});
        imgdir = ::cw::binary::util::check_duplicate(imgdir);
        set_imgdir(imgdir);
    } else {
        imgdir = join_paths({basedir, get_materialdir(), m_xmltype});
    }
    const auto image_name = m_xmltype == "Summary" ? m_xmltype + ".bmp" : safe_filename(m_name) + ".bmp";
    const auto path_text = ::cw::binary::util::check_duplicate(join_paths({imgdir, image_name}));
    const auto path = std::filesystem::u8path(path_text);
    // Python版と同様、画像を開く前に makedirs(imgdir)
    // を試す。 空のディレクトリは不正とし、黙って作業ディレクトリへ書かない。
    if (imgdir.empty())
        throw std::filesystem::filesystem_error("empty image output directory",
            std::filesystem::path{}, std::make_error_code(std::errc::no_such_file_or_directory));
    if (::cardwirth::platform::is_mounted_user_path(path)) {
        if (!::cardwirth::platform::write_mounted_user_file(path, std::as_bytes(std::span(*m_image)), true))
            throw std::runtime_error("failed to export mounted embedded image");
    } else {
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(m_image->data()), static_cast<std::streamsize>(m_image->size()));
        if (!output) throw std::runtime_error("failed to export embedded image");
        ::cw::binary::cwfile::finish_binary_output(output);
    }
    // Python版と同様、basedirが空でも path.replace(basedir + "/",
    // "", 1) の文字列規則を使う。 relpathに変えると、不要な ../ が加わる場合がある。
    auto reference = path_text;
    const auto marker = basedir + "/";
    const auto position = reference.find(marker);
    if (position != ::cw::lib::String::npos) reference.erase(position, marker.size());
    return repl_escapechar(reference);
}

void CWBinaryBase::check_imgpath(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f, std::shared_ptr<::cw::data::CWPyElement> arg_e_imgpath, ::cw::lib::String arg_defpostype)
{
    if (!arg_e_imgpath) return;
    if (arg_e_imgpath->m_tag != "ImagePath" && arg_e_imgpath->m_tag != "Talk")
        throw std::invalid_argument("invalid image path element: " + arg_e_imgpath->m_tag);
    // ネイティブのnullの書き込み先は、検証専用の境界。 上のタグ検査は残すが、書
    // き込み先のバージョン調整は省略する。 Python版も、標準以外の画像位置で
    // check_wsnversion() を呼ぶときだけ書き込み先を必要とする。
    if (!arg_f) return;
    const auto position_type = arg_e_imgpath->get("positiontype", "Default");
    if (position_type != "Default" && position_type != arg_defpostype) {
        arg_f->check_wsnversion("2", "イメージ位置の指定");
    }
}

void CWBinaryBase::check_coupon(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f, ::cw::lib::String arg_coupon)
{
    if (arg_f && (arg_coupon == "＠効果対象" || arg_coupon == "＠効果対象外" ||
                  arg_coupon == "＠イベント対象" || arg_coupon == "＠使用者")) {
        arg_f->check_wsnversion("2", arg_coupon);
    }
}

std::optional<::cw::lib::Bytes> CWBinaryBase::import_image(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f, ::cw::lib::Value arg_imagepath, bool arg_convertbitmap, bool arg_fullpath, ::cw::lib::String arg_defpostype)
{
    if (const auto* path = std::get_if<::cw::lib::String>(&arg_imagepath.storage())) {
        return import_image(std::move(arg_f), *path, arg_convertbitmap, arg_fullpath,
                            std::move(arg_defpostype));
    }
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&arg_imagepath.storage())) {
        auto element = std::dynamic_pointer_cast<::cw::data::CWPyElement>(*object);
        if (element) return import_image(std::move(arg_f), element, arg_convertbitmap,
                                         arg_fullpath, std::move(arg_defpostype));
    }
    return std::nullopt;
}

std::optional<::cw::lib::Bytes> CWBinaryBase::import_image(
    std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f,
    const ::cw::lib::String& arg_imagepath, bool arg_convertbitmap,
    bool arg_fullpath, ::cw::lib::String) {
    (void)arg_f;
    if (arg_imagepath.empty()) return std::nullopt;
    ::cw::lib::Bytes bytes;
    if (::cw::binary::image::path_is_code(arg_imagepath)) {
        // 埋め込みの binaryimage:// データも、ファイル画像と同
        // じ変換経路を使う。 特に convertbitmap=true なら
        // 、埋め込みのPNG/JPEG/GIF/WebPもBMPへ変換する。
        bytes = ::cw::binary::image::code_to_data(arg_imagepath);
        return convert_image_to_bmp(std::move(bytes), arg_convertbitmap);
    }
    const auto requested = std::filesystem::u8path(arg_imagepath);
    if (!arg_fullpath && requested.is_absolute()) return std::nullopt;
    std::filesystem::path path;
    if (arg_fullpath) {
        path = std::filesystem::u8path(arg_imagepath);
    } else {
        std::vector<std::filesystem::path> candidates;
        if (g_image_import_roots) {
            for (const auto& root : *g_image_import_roots)
                if (!root.empty()) candidates.emplace_back(std::filesystem::u8path(root));
        } else if (::cw::g_cwpy) {
            if (const auto* scenario_value = std::get_if<::cw::lib::ObjectPtr>(
                    &::cw::g_cwpy->m_sdata.storage()); scenario_value && *scenario_value) {
                if (const auto scenario = std::dynamic_pointer_cast<::cw::data::ScenarioData>(*scenario_value)) {
                    const auto scenario_temp = value_string(scenario->m_tempdir);
                    if (!scenario_temp.empty()) candidates.emplace_back(std::filesystem::u8path(scenario_temp));
                }
            }
            if (!::cw::g_cwpy->m_tempdir.empty()) candidates.emplace_back(std::filesystem::u8path(::cw::g_cwpy->m_tempdir));
            if (!::cw::g_cwpy->m_yadodir.empty()) candidates.emplace_back(std::filesystem::u8path(::cw::g_cwpy->m_yadodir));
        }
        for (const auto& base : candidates) {
            const auto candidate = base / requested;
            if (std::filesystem::is_regular_file(candidate)) { path = candidate; break; }
        }
    }
    if (path.empty() || !std::filesystem::is_regular_file(path)) return std::nullopt;
    std::ifstream input(path, std::ios::binary);
    bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return convert_image_to_bmp(std::move(bytes), arg_convertbitmap);
}

std::optional<::cw::lib::Bytes> CWBinaryBase::import_image(
    std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f,
    const std::shared_ptr<::cw::data::CWPyElement>& arg_imagepath,
    bool arg_convertbitmap, bool arg_fullpath, ::cw::lib::String arg_defpostype) {
    if (!arg_imagepath) return std::nullopt;
    std::shared_ptr<::cw::data::CWPyElement> image_element = arg_imagepath;
    if (arg_imagepath->m_tag == "ImagePaths") {
        if (arg_imagepath->m_children.size() > 1 && arg_f) {
            arg_f->check_wsnversion("1", "複合イメージ");
        }
        const auto found = arg_imagepath->find("ImagePath");
        if (!found) return std::nullopt;
        image_element = *found;
    }
    if (image_element->m_tag != "ImagePath") return std::nullopt;
    check_imgpath(arg_f, image_element, arg_defpostype);
    return import_image(std::move(arg_f), image_element->m_text, arg_convertbitmap,
                        arg_fullpath, std::move(arg_defpostype));
}

std::shared_ptr<::cw::data::CWPyElement> CWBinaryBase::get_data()
{
    throw std::invalid_argument("CWBinaryBase::get_data must be overridden");
}

::cw::lib::String CWBinaryBase::get_materialpath(::cw::lib::String arg_path)
{
    if (arg_path == "（なし）" || arg_path.empty()) return {};
    arg_path = ::cw::util::validate_filepath(std::move(arg_path));
    if (arg_path.empty()) return {};
    const auto material = get_materialdir();
    return material.empty() ? arg_path : join_paths({material, arg_path});
}

::cw::lib::String CWBinaryBase::materialpath(::cw::lib::String arg_path)
{
    if (arg_path.rfind("binaryimage://", 0) == 0) return arg_path;
    arg_path = ::cw::util::validate_filepath(std::move(arg_path));
    if (arg_path.empty()) return {};
    static const ::cw::lib::String prefix = "Material/";
    return arg_path.rfind(prefix, 0) == 0 ? arg_path.substr(prefix.size()) : arg_path;
}

::cw::lib::String CWBinaryBase::get_indent(::cw::lib::Integer arg_indent)
{
    return arg_indent > 0 ? ::cw::lib::String(static_cast<std::size_t>(arg_indent), ' ') : ::cw::lib::String{};
}

// 受け取る型による意図的な順序の違い。Python版は辞書の挿
// 入順だが、 この公開ネイティブAPIは std::map を
// 受け取り、キー順に属性を出力する。 属性値と引用符の整形は同
// じに保つが、Python版の挿入順を再現するものではない。
::cw::lib::String CWBinaryBase::get_propertiestext(std::map<::cw::lib::String, ::cw::lib::String> arg_d)
{
    ::cw::lib::String result;
    for (const auto& [key, value] : arg_d) result += " " + key + "=\"" + value + "\"";
    return result;
}

std::tuple<::cw::lib::String, ::cw::lib::String> CWBinaryBase::conv_contenttype(::cw::lib::Integer arg_n)
{
    const auto& v=content_types();
    if(arg_n<0||arg_n>=static_cast<::cw::lib::Integer>(v.size()))throw std::invalid_argument(m_fpath);
    return v[static_cast<std::size_t>(arg_n)];
}

::cw::lib::Integer CWBinaryBase::unconv_contenttype(::cw::lib::String arg_ctype, ::cw::lib::String arg_n, std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f)
{
    const auto& v=content_types();
    for(std::size_t i=0;i<v.size();++i)if(v[i].first==arg_ctype&&v[i].second==arg_n)return static_cast<::cw::lib::Integer>(i);
    static const std::map<std::pair<::cw::lib::String,::cw::lib::String>,std::pair<::cw::lib::String,::cw::lib::String>> ext{
        {{"Replace","BgImage"},{"1","背景置換コンテント"}},{{"Lose","BgImage"},{"1","背景削除コンテント"}},
        {{"Move","BgImage"},{"1","背景再配置コンテント"}},{{"Branch","MultiCoupon"},{"2","クーポン多岐分岐コンテント"}},
        {{"Branch","MultiRandom"},{"2","ランダム多岐分岐コンテント"}},{{"Move","Card"},{"3","カード再配置コンテント"}},
        {{"Change","Environment"},{"4","状況設定コンテント"}},{{"Set","Variant"},{"4","コモン設定コンテント"}},
        {{"Branch","Variant"},{"4","コモン分岐コンテント"}},{{"Check","Variant"},{"4","コモン判定コンテント"}}};
    if(const auto it=ext.find({arg_ctype,arg_n});it!=ext.end()){arg_f->check_wsnversion(it->second.first,it->second.second);return 0;}
    throw std::invalid_argument(arg_ctype + ", " + arg_n);
}

::cw::lib::String CWBinaryBase::conv_target_member(::cw::lib::Integer arg_n)
{
    static const std::array<::cw::lib::String,7> v{"Selected","Random","Party","SelectedSleep","RandomSleep","PartySleep","Party"};
    return enum_forward(arg_n,v,m_fpath);
}

::cw::lib::Integer CWBinaryBase::unconv_target_member(::cw::lib::String arg_n, std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f, bool arg_effectcontent)
{
    if(arg_n=="Selected")return 0;if(arg_n=="Random")return 1;if(arg_n=="Unselected"||arg_n=="Party")return 2;
    if(arg_n=="SelectedSleep")return 3;if(arg_n=="RandomSleep")return 4;if(arg_n=="PartySleep")return 5;
    if(arg_n=="CouponHolder"){arg_f->check_wsnversion(arg_effectcontent?"2":"3","任意の称号所持者の指定");return 0;}
    if(arg_n=="CardTarget"&&arg_effectcontent){arg_f->check_wsnversion("2","カードの効果対象の指定");return 0;}
    throw ::cw::binary::cwfile::UnsupportedError(std::nullopt,"対象「"+arg_n+"」");
}

::cw::lib::String CWBinaryBase::conv_target_member_dialog(::cw::lib::Integer arg_n)
{
    if(arg_n==-1||arg_n==0)return "Selected";if(arg_n==1)return "Random";if(arg_n==2)return "Unselected";if(arg_n==3)return "Valued";
    throw std::invalid_argument(m_fpath);
}

::cw::lib::Integer CWBinaryBase::unconv_target_member_dialog(::cw::lib::String arg_n, std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f)
{
    if(arg_n=="Selected")return 0;if(arg_n=="Random")return 1;if(arg_n=="Unselected")return 2;
    if(arg_n=="Valued"){arg_f->check_version(1.50,"評価メンバ");return 3;}
    throw ::cw::binary::cwfile::UnsupportedError(std::nullopt,"話者「"+arg_n+"」");
}

::cw::lib::String CWBinaryBase::conv_target_scope(::cw::lib::Integer arg_n)
{
    static const std::array<::cw::lib::String,6> v{"Selected","Random","Party","Backpack","PartyAndBackpack","Field"};
    return enum_forward(arg_n,v,m_fpath);
}

::cw::lib::Integer CWBinaryBase::unconv_target_scope(::cw::lib::String arg_n, std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f)
{
    static const std::array<::cw::lib::String,6> v{"Selected","Random","Party","Backpack","PartyAndBackpack","Field"};
    for(std::size_t i=0;i<v.size();++i)if(v[i]==arg_n)return static_cast<::cw::lib::Integer>(i);
    if(arg_n=="CouponHolder"){arg_f->check_wsnversion("3","任意の称号所持者の指定");return 0;}
    if(arg_n=="SelectedCard"){arg_f->check_wsnversion("3","範囲に選択カードを指定");return 0;}
    throw ::cw::binary::cwfile::UnsupportedError(std::nullopt,"対象「"+arg_n+"」");
}

::cw::lib::String CWBinaryBase::conv_target_scope_coupon(::cw::lib::Integer arg_n)
{
    static const std::array<::cw::lib::String,4> v{"Selected","Random","Party","Field"};return enum_forward(arg_n,v,m_fpath);
}

::cw::lib::Integer CWBinaryBase::unconv_target_scope_coupon(::cw::lib::String arg_n, std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f)
{
    if(arg_n=="Selected")return 0;if(arg_n=="Random")return 1;if(arg_n=="Party")return 2;
    if(arg_n=="Field"){arg_f->check_version(1.30,"クーポン所持分岐のフィールド全体指定");return 3;}
    if(arg_n=="Npc"){arg_f->check_wsnversion("5","クーポン検索範囲の同行キャスト指定");return 0;}
    throw ::cw::binary::cwfile::UnsupportedError(std::nullopt,"クーポン検索範囲「"+arg_n+"」");
}

std::set<::cw::lib::String> CWBinaryBase::conv_castranges(::cw::lib::Integer arg_n)
{
    std::set<::cw::lib::String> result;
    if (arg_n & 1) result.insert("Party");
    if (arg_n & 2) result.insert("Enemy");
    if (arg_n & 4) result.insert("Npc");
    return result;
}

::cw::lib::Integer CWBinaryBase::unconv_castranges(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    if (!arg_data) throw std::invalid_argument("unconv_castranges requires XML data");
    ::cw::lib::Integer result = 0;
    for (const auto& child : arg_data->m_children) {
        if (child->m_text == "Party") result |= 1;
        else if (child->m_text == "Enemy") result |= 2;
        else if (child->m_text == "Npc") result |= 4;
        else throw ::cw::binary::cwfile::UnsupportedError(std::nullopt, "ランダム選択範囲「" + child->m_text + "」");
    }
    return result;
}

::cw::lib::String CWBinaryBase::conv_keycoderange(::cw::lib::Integer arg_n)
{
    static const std::array<::cw::lib::String,4> v{"Selected","Random","Backpack","PartyAndBackpack"};return enum_forward(arg_n,v,m_fpath);
}

::cw::lib::Integer CWBinaryBase::unconv_keycoderange(::cw::lib::String arg_n, std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f)
{
    if(arg_n=="Selected")return 0;if(arg_n=="Random")return 1;if(arg_n=="Backpack")return 2;if(arg_n=="PartyAndBackpack")return 3;
    if(arg_n=="CouponHolder"||arg_n=="CardTarget"){arg_f->check_wsnversion("2","キーコードを手札から検索");return 0;}
    if(arg_n=="SelectedCard"){arg_f->check_wsnversion("3","範囲に選択カードを指定");return 0;}
    throw ::cw::binary::cwfile::UnsupportedError(std::nullopt,"キーコード検索範囲「"+arg_n+"」");
}

::cw::lib::String CWBinaryBase::conv_spreadtype(::cw::lib::Integer arg_n)
{
    static const std::array<::cw::lib::String, 2> v{"Auto", "Custom"};
    return enum_forward(arg_n, v, m_fpath);
}

::cw::lib::Integer CWBinaryBase::unconv_spreadtype(::cw::lib::String arg_n)
{
    static const std::array<::cw::lib::String, 2> v{"Auto", "Custom"};
    return enum_reverse(arg_n, v, "カード配置タイプ");
}

::cw::lib::String CWBinaryBase::conv_statustype(::cw::lib::Integer arg_n)
{
    static const std::array<::cw::lib::String, 27> v{
        "Active","Inactive","Alive","Dead","Fine","Injured","HeavyInjured",
        "Unconscious","Poison","Sleep","Bind","Paralyze","Confuse","Overheat",
        "Brave","Panic","Silence","FaceUp","AntiMagic","UpAction","UpAvoid",
        "UpResist","UpDefense","DownAction","DownAvoid","DownResist","DownDefense"};
    return enum_forward(arg_n, v, m_fpath);
}

::cw::lib::Integer CWBinaryBase::unconv_statustype(::cw::lib::String arg_n, std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f)
{
    static const std::array<::cw::lib::String, 27> v{
        "Active","Inactive","Alive","Dead","Fine","Injured","HeavyInjured",
        "Unconscious","Poison","Sleep","Bind","Paralyze","Confuse","Overheat",
        "Brave","Panic","Silence","FaceUp","AntiMagic","UpAction","UpAvoid",
        "UpResist","UpDefense","DownAction","DownAvoid","DownResist","DownDefense"};
    const auto n = enum_reverse(arg_n, v, "ステータス");
    // UnsupportedError.funcname には、Python版と同じ個別
    // の機能名を残す。 変換ログに表示されるため、まとめた名前では実際の原因が失われる。
    static const std::array<::cw::lib::String, 15> feature{
        "混乱ステータスの判別", "激昂ステータスの判別", "勇敢ステータスの判別", "恐慌ステータスの判別",
        "沈黙ステータスの判別", "暴露ステータスの判別", "魔法無効化ステータスの判別",
        "行動力上昇ステータスの判別", "回避力上昇ステータスの判別", "抵抗力上昇ステータスの判別", "防御力上昇ステータスの判別",
        "行動力低下ステータスの判別", "回避力低下ステータスの判別", "抵抗力低下ステータスの判別", "防御力低下ステータスの判別"};
    if (n >= 12 && n <= 15) arg_f->check_version(1.30, feature[n - 12]);
    else if (n >= 16) arg_f->check_version(1.50, feature[n - 12]);
    return n;
}

::cw::lib::String CWBinaryBase::conv_effectcardtype(::cw::lib::Integer arg_n)
{
    static const std::array<::cw::lib::String, 4> v{"All","Skill","Item","Beast"};
    return enum_forward(arg_n, v, m_fpath);
}

::cw::lib::Integer CWBinaryBase::unconv_effectcardtype(::cw::lib::String arg_n)
{
    static const std::array<::cw::lib::String, 4> v{"All","Skill","Item","Beast"};
    return enum_reverse(arg_n, v, "カードタイプ");
}

::cw::lib::String CWBinaryBase::conv_comparison4(::cw::lib::Integer arg_n)
{
    static const std::array<::cw::lib::String, 4> v{"=","<>","<",">"};
    return enum_forward(arg_n, v, m_fpath);
}

::cw::lib::Integer CWBinaryBase::unconv_comparison4(::cw::lib::String arg_n)
{
    static const std::array<::cw::lib::String, 4> v{"=","<>","<",">"};
    return enum_reverse(arg_n, v, "比較結果");
}

::cw::lib::String CWBinaryBase::conv_comparison3(::cw::lib::Integer arg_n)
{
    static const std::array<::cw::lib::String, 3> v{"=","<",">"};
    return enum_forward(arg_n, v, m_fpath);
}

::cw::lib::Integer CWBinaryBase::unconv_comparison3(::cw::lib::String arg_n)
{
    static const std::array<::cw::lib::String, 3> v{"=","<",">"};
    return enum_reverse(arg_n, v, "比較判定結果");
}

::cw::lib::String CWBinaryBase::conv_effectmotion_element(::cw::lib::Integer arg_n)
{
    static const std::array<::cw::lib::String, 7> v{"All","Health","Mind","Miracle","Magic","Fire","Ice"};
    return enum_forward(arg_n, v, m_fpath);
}

::cw::lib::Integer CWBinaryBase::unconv_effectmotion_element(::cw::lib::String arg_n)
{
    static const std::array<::cw::lib::String, 7> v{"All","Health","Mind","Miracle","Magic","Fire","Ice"};
    return enum_reverse(arg_n, v, "効果属性");
}

::cw::lib::String CWBinaryBase::conv_effectmotion_type(::cw::lib::Integer arg_tabn, ::cw::lib::Integer arg_n)
{
    static const std::array<std::vector<::cw::lib::String>, 9> v{{
        {"Heal","Damage","Absorb"},{"Paralyze","DisParalyze","Poison","DisPoison"},
        {"GetSkillPower","LoseSkillPower"},{"Sleep","Confuse","Overheat","Brave","Panic","Normal"},
        {"Bind","DisBind","Silence","DisSilence","FaceUp","FaceDown","AntiMagic","DisAntiMagic"},
        {"EnhanceAction","EnhanceAvoid","EnhanceResist","EnhanceDefense"},
        {"VanishTarget","VanishCard","VanishBeast"},
        {"DealAttackCard","DealPowerfulAttackCard","DealCriticalAttackCard","DealFeintCard","DealDefenseCard","DealDistanceCard","DealConfuseCard","DealSkillCard","CancelAction"},
        {"SummonBeast"}}};
    if (arg_tabn < 0 || arg_tabn >= static_cast<::cw::lib::Integer>(v.size())) throw std::invalid_argument(m_fpath);
    const auto& row=v[static_cast<std::size_t>(arg_tabn)];
    if (arg_n < 0 || arg_n >= static_cast<::cw::lib::Integer>(row.size())) throw std::invalid_argument(m_fpath);
    return row[static_cast<std::size_t>(arg_n)];
}

std::tuple<::cw::lib::Integer, ::cw::lib::Integer> CWBinaryBase::unconv_effectmotion_type(::cw::lib::String arg_n, std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f)
{
    static const std::array<std::vector<::cw::lib::String>, 9> v{{
        {"Heal","Damage","Absorb"},{"Paralyze","DisParalyze","Poison","DisPoison"},
        {"GetSkillPower","LoseSkillPower"},{"Sleep","Confuse","Overheat","Brave","Panic","Normal"},
        {"Bind","DisBind","Silence","DisSilence","FaceUp","FaceDown","AntiMagic","DisAntiMagic"},
        {"EnhanceAction","EnhanceAvoid","EnhanceResist","EnhanceDefense"},
        {"VanishTarget","VanishCard","VanishBeast"},
        {"DealAttackCard","DealPowerfulAttackCard","DealCriticalAttackCard","DealFeintCard","DealDefenseCard","DealDistanceCard","DealConfuseCard","DealSkillCard","CancelAction"},
        {"SummonBeast"}}};
    for (std::size_t tab = 0; tab < v.size(); ++tab) {
        const auto& row = v[tab];
        for (std::size_t index = 0; index < row.size(); ++index) {
            if (row[index] == arg_n) {
                if (arg_n == "CancelAction") arg_f->check_version(1.50);
                return {static_cast<::cw::lib::Integer>(tab),
                        static_cast<::cw::lib::Integer>(index)};
            }
        }
    }
    if (arg_n=="NoEffect") { arg_f->check_wsnversion("2","効果無し効果"); return {0,1}; }
    throw ::cw::binary::cwfile::UnsupportedError(std::nullopt,"効果「"+arg_n+"」");
}

::cw::lib::String CWBinaryBase::conv_effectmotion_damagetype(::cw::lib::Integer arg_n)
{
    static const std::array<::cw::lib::String, 3> v{"LevelRatio","Normal","Max"};
    return enum_forward(arg_n, v, m_fpath);
}

::cw::lib::Integer CWBinaryBase::unconv_effectmotion_damagetype(::cw::lib::String arg_n)
{
    static const std::array<::cw::lib::String, 3> v{"LevelRatio","Normal","Max"};
    return enum_reverse(arg_n, v, "効果量計算タイプ");
}

::cw::lib::String CWBinaryBase::conv_card_effecttype(::cw::lib::Integer arg_n)
{
    static const std::vector<::cw::lib::String> values{
        "Physic", "Magic", "MagicalPhysic", "PhysicalMagic", "None"};
    if (arg_n < 0 || arg_n >= static_cast<::cw::lib::Integer>(values.size()))
        throw std::invalid_argument(m_fpath);
    return values[static_cast<std::size_t>(arg_n)];
}

::cw::lib::Integer CWBinaryBase::unconv_card_effecttype(::cw::lib::String arg_n)
{
    if (arg_n == "Physic") return 0; if (arg_n == "Magic") return 1;
    if (arg_n == "MagicalPhysic") return 2; if (arg_n == "PhysicalMagic") return 3;
    if (arg_n == "None") return 4;
    throw ::cw::binary::cwfile::UnsupportedError(std::nullopt, "基本効果属性「" + arg_n + "」");
}

::cw::lib::String CWBinaryBase::conv_card_resisttype(::cw::lib::Integer arg_n)
{
    if (arg_n == 0) return "Avoid"; if (arg_n == 1) return "Resist";
    if (arg_n == 2) return "Unfail";
    throw std::invalid_argument(m_fpath);
}

::cw::lib::Integer CWBinaryBase::unconv_card_resisttype(::cw::lib::String arg_n)
{
    if (arg_n == "Avoid") return 0; if (arg_n == "Resist") return 1;
    if (arg_n == "Unfail") return 2;
    throw ::cw::binary::cwfile::UnsupportedError(std::nullopt, "回避属性「" + arg_n + "」");
}

::cw::lib::String CWBinaryBase::conv_card_visualeffect(::cw::lib::Integer arg_n)
{
    static const std::vector<::cw::lib::String> values{"None", "Reverse", "Horizontal", "Vertical"};
    if (arg_n < 0 || arg_n >= static_cast<::cw::lib::Integer>(values.size()))
        throw std::invalid_argument(m_fpath);
    return values[static_cast<std::size_t>(arg_n)];
}

::cw::lib::Integer CWBinaryBase::unconv_card_visualeffect(::cw::lib::String arg_n)
{
    if (arg_n == "None") return 0; if (arg_n == "Reverse") return 1;
    if (arg_n == "Horizontal") return 2; if (arg_n == "Vertical") return 3;
    throw ::cw::binary::cwfile::UnsupportedError(std::nullopt, "カード視覚効果「" + arg_n + "」");
}

::cw::lib::String CWBinaryBase::conv_card_physicalability(::cw::lib::Integer arg_n)
{
    static const std::vector<::cw::lib::String> values{"Dex", "Agl", "Int", "Str", "Vit", "Min"};
    if (arg_n < 0 || arg_n >= static_cast<::cw::lib::Integer>(values.size()))
        throw std::invalid_argument(m_fpath);
    return values[static_cast<std::size_t>(arg_n)];
}

::cw::lib::Integer CWBinaryBase::unconv_card_physicalability(::cw::lib::String arg_n)
{
    if (arg_n == "Dex") return 0; if (arg_n == "Agl") return 1; if (arg_n == "Int") return 2;
    if (arg_n == "Str") return 3; if (arg_n == "Vit") return 4; if (arg_n == "Min") return 5;
    throw ::cw::binary::cwfile::UnsupportedError(std::nullopt, "身体能力「" + arg_n + "」");
}

::cw::lib::String CWBinaryBase::conv_card_mentalability(::cw::lib::Integer arg_n)
{
    switch (arg_n) {
    case 1: return "Aggressive"; case -1: return "Unaggressive";
    case 2: return "Cheerful"; case -2: return "Uncheerful";
    case 3: return "Brave"; case -3: return "Unbrave";
    case 4: return "Cautious"; case -4: return "Uncautious";
    case 5: return "Trickish"; case -5: return "Untrickish";
    default: throw std::invalid_argument(m_fpath);
    }
}

::cw::lib::Integer CWBinaryBase::unconv_card_mentalability(::cw::lib::String arg_n)
{
    if (arg_n == "Aggressive") return 1; if (arg_n == "Unaggressive") return -1;
    if (arg_n == "Cheerful") return 2; if (arg_n == "Uncheerful") return -2;
    if (arg_n == "Brave") return 3; if (arg_n == "Unbrave") return -3;
    if (arg_n == "Cautious") return 4; if (arg_n == "Uncautious") return -4;
    if (arg_n == "Trickish") return 5; if (arg_n == "Untrickish") return -5;
    throw ::cw::binary::cwfile::UnsupportedError(std::nullopt, "精神傾向「" + arg_n + "」");
}

::cw::lib::String CWBinaryBase::conv_card_target(::cw::lib::Integer arg_n)
{
    static const std::vector<::cw::lib::String> values{"None", "User", "Party", "Enemy", "Both"};
    if (arg_n < 0 || arg_n >= static_cast<::cw::lib::Integer>(values.size()))
        throw std::invalid_argument(m_fpath);
    return values[static_cast<std::size_t>(arg_n)];
}

::cw::lib::Integer CWBinaryBase::unconv_card_target(::cw::lib::String arg_n)
{
    if (arg_n == "None") return 0; if (arg_n == "User") return 1; if (arg_n == "Party") return 2;
    if (arg_n == "Enemy") return 3; if (arg_n == "Both") return 4;
    throw ::cw::binary::cwfile::UnsupportedError(std::nullopt, "効果目標「" + arg_n + "」");
}

::cw::lib::String CWBinaryBase::conv_card_premium(::cw::lib::Integer arg_n)
{
    if (arg_n == 0) return "Normal"; if (arg_n == 1) return "Rare";
    if (arg_n == 2) return "Premium";
    throw std::invalid_argument(m_fpath);
}

::cw::lib::Integer CWBinaryBase::unconv_card_premium(::cw::lib::String arg_n)
{
    if (arg_n == "Normal") return 0; if (arg_n == "Rare") return 1;
    if (arg_n == "Premium") return 2;
    throw ::cw::binary::cwfile::UnsupportedError(std::nullopt, "カード価値「" + arg_n + "」");
}

::cw::lib::String CWBinaryBase::conv_mentality(::cw::lib::Integer arg_n)
{
    switch (arg_n) {
    case 0: return "Normal";
    case 1: return "Sleep";
    case 2: return "Confuse";
    case 3: return "Overheat";
    case 4: return "Brave";
    case 5: return "Panic";
    default: throw std::invalid_argument(m_fpath);
    }
}

::cw::lib::Integer CWBinaryBase::unconv_mentality(::cw::lib::String arg_n)
{
    if (arg_n == "Normal") return 0;
    if (arg_n == "Sleep") return 1;
    if (arg_n == "Confuse") return 2;
    if (arg_n == "Overheat") return 3;
    if (arg_n == "Brave") return 4;
    if (arg_n == "Panic") return 5;
    throw ::cw::binary::cwfile::UnsupportedError(
        std::nullopt, "精神状態「" + arg_n + "」");
}

::cw::lib::String CWBinaryBase::conv_yadotype(::cw::lib::Integer arg_n)
{
    if (arg_n == 1) return "Normal"; if (arg_n == 2) return "Debug";
    throw std::invalid_argument(m_fpath);
}

::cw::lib::Integer CWBinaryBase::unconv_yadotype(::cw::lib::String arg_n)
{
    if (arg_n == "Normal") return 1; if (arg_n == "Debug") return 2;
    throw ::cw::binary::cwfile::UnsupportedError(std::nullopt, "宿タイプ「" + arg_n + "」");
}

::cw::lib::String CWBinaryBase::conv_yado_summaryview(::cw::lib::Integer arg_n)
{
    static const std::array<::cw::lib::String, 4> v{"HideHiddenAndCompleteScenario","HideHiddenScenario","ShowAll","ShowFittingScenario"};
    return enum_forward(arg_n, v, m_fpath);
}

::cw::lib::Integer CWBinaryBase::unconv_yado_summaryview(::cw::lib::String arg_n)
{
    static const std::array<::cw::lib::String, 4> v{"HideHiddenAndCompleteScenario","HideHiddenScenario","ShowAll","ShowFittingScenario"};
    return enum_reverse(arg_n, v, "シナリオ表示タイプ");
}

::cw::lib::String CWBinaryBase::conv_yado_bgchange(::cw::lib::Integer arg_n)
{
    static const std::array<::cw::lib::String, 4> v{"NoAnimation","ReedShape","ColorShade","ReplaceDot"};
    return enum_forward(arg_n, v, m_fpath);
}

::cw::lib::Integer CWBinaryBase::unconv_yado_bgchange(::cw::lib::String arg_n)
{
    static const std::array<::cw::lib::String, 4> v{"NoAnimation","ReedShape","ColorShade","ReplaceDot"};
    return enum_reverse(arg_n, v, "背景切替方式");
}

::cw::lib::String CWBinaryBase::conv_borderingtype(::cw::lib::Integer arg_n)
{
    static const std::array<::cw::lib::String, 2> v{"Outline","Inline"};
    return enum_forward(arg_n, v, m_fpath);
}

::cw::lib::Integer CWBinaryBase::unconv_borderingtype(::cw::lib::String arg_n)
{
    static const std::array<::cw::lib::String, 2> v{"Outline","Inline"};
    return enum_reverse(arg_n, v, "縁取りタイプ");
}

::cw::lib::String CWBinaryBase::conv_blendmode(::cw::lib::Integer arg_n)
{
    if (arg_n == 0 || arg_n == 1) return "Normal"; if (arg_n == 2) return "Add";
    if (arg_n == 3) return "Subtract"; if (arg_n == 4) return "Multiply";
    throw std::invalid_argument(m_fpath);
}

::cw::lib::Integer CWBinaryBase::unconv_blendmode(::cw::lib::String arg_n)
{
    if (arg_n == "Normal") return 0; if (arg_n == "Add") return 2;
    if (arg_n == "Subtract") return 3; if (arg_n == "Multiply") return 4;
    throw ::cw::binary::cwfile::UnsupportedError(std::nullopt, "合成方法「" + arg_n + "」");
}

::cw::lib::String CWBinaryBase::conv_gradientdir(::cw::lib::Integer arg_n)
{
    static const std::array<::cw::lib::String, 3> v{"None","LeftToRight","TopToBottom"};
    return enum_forward(arg_n, v, m_fpath);
}

::cw::lib::Integer CWBinaryBase::unconv_gradientdir(::cw::lib::String arg_n)
{
    static const std::array<::cw::lib::String, 3> v{"None","LeftToRight","TopToBottom"};
    return enum_reverse(arg_n, v, "グラデーション方向");
}

void main()
{
}

}

