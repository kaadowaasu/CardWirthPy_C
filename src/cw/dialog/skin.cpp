#ifdef _WIN32
#define _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#endif
#include "cw/dialog/skin.hpp"
#include "cw/cw.hpp"
#include "cw/data.hpp"
#include "cw/binary/cwyado.hpp"
#include "cw/binary/util.hpp"
#include "cw/dialog/settings.hpp"
#include "cw/dialog/edit.hpp"
#include "cw/dialog/message.hpp"
#include "cw/dialog/progress.hpp"
#include "cw/dialog/textedit_input.hpp"
#include "cw/dialog/textedit_render.hpp"
#include "cw/frame.hpp"
#include "cw/lib/encoding.hpp"
#include "cw/lib/python_text.hpp"
#include "cw/lib/ui_surface.hpp"
#include "cw/setting.hpp"
#include "cw/skin/convert.hpp"
#include "cw/thread.hpp"
#include "cw/util.hpp"
#include "cw/xmlcreater.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace cw::dialog::skin {

namespace {
using Element=std::shared_ptr<::cw::data::CWPyElement>;
using Tree=std::shared_ptr<::cw::data::CWPyElementTree>;
using String=::cw::lib::String;
using Integer=::cw::lib::Integer;

bool skin_common_confirm(const String& text);

SkinBasePanel* g_active_base=nullptr;
SkinInfoPanel* g_active_info=nullptr;
SkinFeaturePanel* g_active_feature=nullptr;
SkinSoundPanel* g_active_sound=nullptr;
SkinMessagePanel* g_active_message=nullptr;
SkinCardPanel* g_active_card=nullptr;
SkinConversionDialog* g_active_conversion=nullptr;
SkinEditDialog* g_active_edit=nullptr;
String normalize_decimal(String value, String fallback="0");

std::vector<Element> children(const Tree& tree, const String& path)
{
    if (!tree) return {};
    const auto found=tree->find(path);
    // Python の Element の反復と同様、直接の子だけを
    // 取得し、Element.iter() の再帰走査にはしない。
    return found && *found ? (*found)->iterate() : std::vector<Element>{};
}

std::vector<Element> children(const Element& element, const String& path)
{
    if (!element) return {};
    const auto found=element->find(path);
    return found && *found ? (*found)->iterate() : std::vector<Element>{};
}

String lower_ascii(String value)
{
    std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool natural_less(const String& left, const String& right)
{
    // スキンの Python の sorted_by_attr と同様、他の Engine 呼び出し側と同じ LogicalStr
    // の順序を使う。 Unicode の小文字化と、同順位の場合の元の表記による比較も含める。
    return ::cw::util::logical_compare(left,right)<0;
}

template<typename Table>
std::vector<String> sorted_keys(const Table& table)
{
    std::vector<String> result;
    for (const auto& item:table) result.push_back(item.first);
    std::sort(result.begin(),result.end(),natural_less);
    return result;
}

Integer parse_integer(String value, Integer fallback, Integer minimum, Integer maximum)
{
    try {
        std::size_t used=0;
        const auto parsed=std::stoll(value,&used);
        if (used!=value.size()) return fallback;
        return std::clamp<Integer>(parsed,minimum,maximum);
    } catch (...) {
        return fallback;
    }
}

String normalize_decimal(String value, String fallback)
{
    try {
        std::size_t used=0;
        auto parsed=std::stod(value,&used);
        if (used!=value.size() || !std::isfinite(parsed)) return fallback;
        // Python の GridCellFloatEditor(4,1) は値の範囲ではなく精度を指定する。
        std::ostringstream formatted;
        formatted.imbue(std::locale::classic());
        formatted << std::fixed << std::setprecision(1) << parsed;
        return formatted.str();
    } catch (...) {
        return fallback;
    }
}

Element property(const Tree& tree)
{
    return tree?tree->find_exists("Property"):Element{};
}

std::shared_ptr<::cw::setting::LocalSetting> callback_local(const ::cw::lib::Callback& callback)
{
    if (!callback) return {};
    const auto value=callback({},{});
    const auto object=std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    if (!object || !*object) return {};
    return std::dynamic_pointer_cast<::cw::setting::LocalSetting>(*object);
}

std::shared_ptr<::cw::setting::LocalSetting> new_local()
{
    auto result=std::make_shared<::cw::setting::LocalSetting>();
    result->init();
    return result;
}

String skin_xml_path(const String& skindirname)
{
    // xml2etree は全 OS で UTF-8 を受け付ける。ネイティブパスが UTF-16 の Windows も含む。
    return ::cw::lib::encoding::path_to_utf8(
        std::filesystem::u8path("Data/Skin")/std::filesystem::u8path(skindirname)/"Skin.xml");
}

void set_property(const Tree& tree, const String& path, const String& value)
{
    if (tree) tree->edit(path,value,std::nullopt);
}

void set_optional_skin_property(const Tree& tree, const String& path,
                                const String& tag, const String& value)
{
    if (!tree) return;
    if (const auto found=tree->find(path); found && *found) {
        tree->edit(path,value,std::nullopt);
        return;
    }
    const auto property=tree->find("Property");
    if (!property || !*property)
        throw std::out_of_range("Skin Property element is missing");
    (*property)->append(::cw::data::make_element(tag,value));
}

void replace_skin_file(const std::filesystem::path& temporary,const std::filesystem::path& target)
{
// UIの実装は一つに統一し、
// 別経路の実装を設けない。
#ifdef _WIN32
    // 必要最小限のファイルシステム処理とし、両方の UI モードで同じ取り込み処理を呼ぶ。
    if(!MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw std::system_error(static_cast<int>(GetLastError()),std::system_category(),"スキンファイルの置換に失敗しました");
#else
    std::error_code error;std::filesystem::rename(temporary,target,error);
    if(error) throw std::filesystem::filesystem_error("スキンファイルの置換に失敗しました",temporary,target,error);
#endif
}

void write_tree_transactionally(const Tree& tree, const String& target_text)
{
    if (!tree || target_text.empty()) throw std::runtime_error("Skin.xmlの保存先が空です。");
    const auto target=std::filesystem::u8path(target_text);
    if (!target.parent_path().empty()) std::filesystem::create_directories(target.parent_path());
    const auto nonce=std::chrono::steady_clock::now().time_since_epoch().count();
    const auto temporary=target.parent_path()/std::filesystem::u8path(
        ::cw::lib::encoding::path_to_utf8(target.filename())+".tmp-"+std::to_string(nonce));
    const auto cleanup=[&] {
        std::error_code ec;
        std::filesystem::remove(temporary,ec);
    };
    try {
        const auto bytes=tree->write_bytes();
        {
            std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
            if (!output) throw std::runtime_error("Skin.xmlの一時ファイルを開けません。");
            output.write(reinterpret_cast<const char*>(bytes.data()),
                         static_cast<std::streamsize>(bytes.size()));
            output.flush();
            if (!output) throw std::runtime_error("Skin.xmlの一時ファイルを書き込めません。");
            output.close();
            if (!output) throw std::runtime_error("Skin.xmlの一時ファイルを閉じられません。");
        }
        replace_skin_file(temporary,target);
        // Python の e.write_file(skinpath) と同様、このバイト
        // 列の書き出しでは XML ツリーのファイル識別情報や未保存編集フラグを変えない。
    } catch (...) {
        cleanup();
        throw;
    }
}

void set_feature_row(const Element& data, const SkinFeatureRow& row)
{
    if (!data) return;
    // GetCellValue は未編集のセルをそのまま保持する。 数値エディタ
    // ーの検証・整形はシリアライズ時ではなく finish_edit で行う。
    data->find_exists("Name")->set_text(row[0]);
    const std::array<String,6> physical{"dex","agl","int","str","vit","min"};
    auto p=data->find_exists("Physical");
    for (std::size_t i=0;i<physical.size();++i) p->set(physical[i],row[i+1]);
    const std::array<String,5> mental{"aggressive","cheerful","brave","cautious","trickish"};
    auto m=data->find_exists("Mental");
    for (std::size_t i=0;i<mental.size();++i) m->set(mental[i],row[i+7]);
}

void get_feature_row(const Element& data, SkinFeatureRow& row)
{
    if (!data) return;
    row[0]=data->gettext("Name","");
    const std::array<String,6> physical{"dex","agl","int","str","vit","min"};
    auto p=data->find_exists("Physical");
    for (std::size_t i=0;i<physical.size();++i) row[i+1]=p->get(physical[i],"0");
    const std::array<String,5> mental{"aggressive","cheerful","brave","cautious","trickish"};
    auto m=data->find_exists("Mental");
    for (std::size_t i=0;i<mental.size();++i) row[i+7]=m->get(mental[i],"0");
}

std::vector<Element> feature_elements(const Tree& tree)
{
    std::vector<Element> result;
    for (const auto* tag:{"Sexes","Periods","Natures","Makings"}) {
        const auto part=children(tree,tag);
        result.insert(result.end(),part.begin(),part.end());
    }
    return result;
}

void fill_card_labels(const std::map<String,Tree>& table, std::vector<String>& labels,
                      Integer& row, const String& prefix)
{
    for (const auto& key:sorted_keys(table)) {
        const auto& tree=table.at(key);
        if (!tree) continue;
        const auto area=tree->gettext("Property/Name","");
        for (const auto& card:children(tree,"MenuCards")) {
            labels.push_back(prefix+area+": "+card->gettext("Property/Name",""));
            ++row;
        }
    }
}

void fill_card_values(const std::map<String,Tree>& table, std::vector<SkinCardRow>& rows,
                      Integer& row)
{
    for (const auto& key:sorted_keys(table)) {
        const auto& tree=table.at(key);
        if (!tree) continue;
        for (const auto& card:children(tree,"MenuCards")) {
            if (static_cast<std::size_t>(row)>=rows.size()) { SkinCardRow empty{}; rows.push_back(empty); }
            rows[static_cast<std::size_t>(row)]={card->gettext("Property/Name",""),card->gettext("Property/Description","")};
            ++row;
        }
    }
}

void apply_card_values(const std::map<String,Tree>& table, const std::vector<SkinCardRow>& rows,
                       Integer& row)
{
    for (const auto& key:sorted_keys(table)) {
        const auto& tree=table.at(key);
        if (!tree) continue;
        for (const auto& card:children(tree,"MenuCards")) {
            if (static_cast<std::size_t>(row)<rows.size()) {
                card->find_exists("Property/Name")->set_text(rows[static_cast<std::size_t>(row)][0]);
                card->find_exists("Property/Description")->set_text(rows[static_cast<std::size_t>(row)][1]);
            }
            ++row;
        }
    }
}

bool is_image_extension(const String& extension)
{
    bool result=false;
    std::apply([&](const auto&... value) { ((result=result || lower_ascii(value)==extension),...); },::cw::g_EXTS_IMG);
    return result;
}

std::filesystem::path import_resource_path(const std::filesystem::path& target,const String& name,const String& extension)
{
    const auto relative=std::filesystem::u8path(name+extension);
    if(relative.empty() || relative.is_absolute()) throw std::runtime_error("インポート先の相対パスが不正です。");
    for(const auto& part:relative) if(part=="..") throw std::runtime_error("インポート先がスキンの外を指しています。");
    return target/relative;
}
void import_resource_file(const std::filesystem::path& source,const std::filesystem::path& target,
                          const String& new_name,const String& extension)
{
    const auto destination=import_resource_path(target,new_name,extension);
    // Python と同様、Image/ だけでなく、対応する Button/・Dialog/・Cursor/ などのサブディレクトリーも作る。
    std::filesystem::create_directories(destination.parent_path());
    static std::atomic<std::uint64_t> sequence{};
    const auto staging=destination.parent_path()/std::filesystem::u8path(
        ::cw::lib::encoding::path_to_utf8(destination.filename())+".import-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(++sequence));
    // 専用の一時ディレクトリーを不可分に確保する。 部分コピーが失敗しても、既
    // 存のファイルや名前が衝突したファイルを消さずに後処理できるようにする。
    if(!std::filesystem::create_directory(staging)) throw std::runtime_error("インポート一時フォルダが既に存在します。");
    const auto temporary=staging/"resource";
    const auto cleanup=[&] {
        std::error_code ignored;
        std::filesystem::permissions(temporary,std::filesystem::perms::owner_write,std::filesystem::perm_options::add,ignored);
        std::filesystem::remove(temporary,ignored);std::filesystem::remove(staging,ignored);
    };
    try {
        std::filesystem::copy_file(source,temporary,std::filesystem::copy_options::none);
        std::filesystem::last_write_time(temporary,std::filesystem::last_write_time(source));
        replace_skin_file(temporary,destination);
        cleanup();
    } catch(...) {
        cleanup();
        throw;
    }
}
void remove_alternate_images(const std::filesystem::path& target,const String& new_name,const String& keep_extension)
{
    const auto remove=[&](const std::filesystem::path& path) {
        // 一致する画像候補だけを削除し、ディレクトリー全体は削除しない。
        if(std::filesystem::is_directory(std::filesystem::symlink_status(path)))
            throw std::runtime_error("画像の別形式の位置にフォルダがあります。");
        std::filesystem::remove(path);
    };
    std::apply([&](const auto&... extension) {
        (([&] {
            const auto ext=String(extension);
            if(ext!=keep_extension) remove(import_resource_path(target,new_name,ext));
            std::apply([&](const auto&... scale) {
                ((remove(import_resource_path(target,new_name+".x"+std::to_string(scale),ext))),...);
            },::cw::g_SCALE_LIST);
        }()),...);
    },::cw::g_EXTS_IMG);
}

std::shared_ptr<::cw::setting::Setting> conversion_setting()
{
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting) return ::cw::g_cwpy->m_setting;
    auto result=std::make_shared<::cw::setting::Setting>();
    result->init_settings(std::nullopt,true);
    return result;
}

void register_generated_scenario(const ::cw::skin::convert::Converter& converter)
{
    if (converter.m_scenariodir.empty()) return;
    const auto exe=std::filesystem::u8path(converter.m_exe);
    const auto source=std::filesystem::u8path(converter.m_scenariodir);
    const auto target=(source.is_absolute()?source:exe.parent_path()/source).lexically_normal();
    const auto path1=::cw::util::get_keypath(::cw::util::get_symlinktarget(::cw::lib::encoding::path_to_utf8(target)));
    auto setting=conversion_setting();
    bool registered=false;
    for (const auto& [type,unused]:setting->m_folderoftype) {
        (void)unused;
        if (type==converter.m_skintype) { registered=true; break; }
    }
    std::error_code ec;
    const auto skins=std::filesystem::u8path("Data/Skin");
    if (!registered && std::filesystem::is_directory(skins,ec)) {
        for (const auto& entry:std::filesystem::directory_iterator(skins,ec)) {
            if (ec || !entry.is_directory(ec) || ::cw::lib::encoding::path_to_utf8(entry.path().filename())==converter.m_skindirname) continue;
            const auto skinpath=entry.path()/"Skin.xml";
            if (!std::filesystem::is_regular_file(skinpath,ec)) continue;
            const auto tree=::cw::data::xml2etree(::cw::lib::encoding::path_to_utf8(skinpath),"",std::nullopt,std::nullopt,true);
            if (tree && tree->gettext("Property/Type","")==converter.m_skintype) { registered=true; break; }
        }
    }
    if (!registered) {
        auto scenario=::cw::util::relpath(path1,".");
        if (scenario.rfind("..",0)==0) scenario=path1;
        setting->m_folderoftype.emplace_back(converter.m_skintype,scenario);
    }
    setting->write();
}

void register_generated_yado(const ::cw::skin::convert::Converter& converter)
{
    if (converter.m_yadodir.empty() || converter.m_version<std::tuple<Integer,Integer,Integer,Integer>{1,2,0,0}) return;
    const auto exe=std::filesystem::u8path(converter.m_exe);
    const auto source=std::filesystem::u8path(converter.m_yadodir);
    const auto target=(source.is_absolute()?source:exe.parent_path()/source).lexically_normal();
    const auto yado_root=std::filesystem::u8path("Yado");
    std::error_code ec;
    std::filesystem::create_directories(yado_root,ec);
    if (ec) throw std::filesystem::filesystem_error("create_directories",yado_root,ec);
    std::set<String> exists;
    if (std::filesystem::is_directory(yado_root,ec)) for (const auto& entry:std::filesystem::directory_iterator(yado_root,ec)) {
        if (ec) break;
        exists.insert(::cw::util::get_keypath(::cw::util::get_symlinktarget(
            ::cw::util::get_linktarget(::cw::lib::encoding::path_to_utf8(entry.path())))));
    }
    if (!std::filesystem::is_directory(target,ec)) return;
    for (const auto& entry:std::filesystem::directory_iterator(target,ec)) {
        if (ec || !entry.is_directory(ec)) continue;
        const auto dpath=::cw::lib::encoding::path_to_utf8(entry.path());
        auto yado=std::make_shared<::cw::binary::cwyado::CWYado>();
        yado->initialize(dpath,"Yado");
        if (!yado->is_convertible()) continue;
        if (exists.contains(::cw::util::get_keypath(::cw::util::get_symlinktarget(dpath)))) continue;
        auto link=::cw::binary::util::check_duplicate(::cw::lib::encoding::path_to_utf8(yado_root/entry.path().filename())+".lnk");
        ::cw::util::create_link(link,dpath);
    }
}

}

SkinHostServices make_default_skin_host_services()
{
    SkinHostServices callbacks{};
    callbacks.reload_skin=[](String id) {
        if (::cw::g_cwpy)
            ::cw::g_cwpy->update_skin(std::move(id),false,false,std::nullopt,true,false);
    };
    callbacks.update_titlebar=[]() {
        if (::cw::g_cwpy) ::cw::g_cwpy->update_titlebar();
    };
    callbacks.update_vocation120=[](bool value) {
        if (::cw::g_cwpy) ::cw::g_cwpy->update_vocation120(value);
    };
    callbacks.update_messagestyle=[]() {
        if (::cw::g_cwpy) ::cw::g_cwpy->update_messagestyle();
    };
    callbacks.update_curtainstyle=[]() {
        if (::cw::g_cwpy) ::cw::g_cwpy->update_curtainstyle();
    };
    callbacks.update_fullscreenbackground=[]() {
        if (::cw::g_cwpy) ::cw::g_cwpy->update_fullscreenbackground();
    };
    return callbacks;
}

namespace {
SkinHostServices& skin_host_services()
{
    static auto services = make_default_skin_host_services();
    return services;
}
}

void SkinBasePanel::initialize(std::shared_ptr<::cw::lib::Panel> arg_parent, std::shared_ptr<::cw::skin::convert::Converter> arg_conv)
{
    dialog_retained_surface().reset();
    (void)arg_parent;
    g_active_base=this;
    cancel_document_request();m_finished=false;m_focus=-1;m_exe_check_pending=false;m_error_message.clear();
    m_converter=std::move(arg_conv);
    m_conv=::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(m_converter));
    m_exe_value=m_converter?m_converter->m_exe:String{};
    m_original_exe=m_exe_value;
    m_datadir_value=m_converter?m_converter->m_datadir:String{};
    m_scenariodir_value=m_converter?m_converter->m_scenariodir:String{};
    m_yadodir_value=m_converter?m_converter->m_yadodir:String{};
    m_exectrl=std::make_shared<::cw::lib::Window>();
    m_datactrl=std::make_shared<::cw::lib::Window>();
    m_scenarioctrl=std::make_shared<::cw::lib::Window>();
    m_yadoctrl=std::make_shared<::cw::lib::Window>();
    m_exectrl->title=m_exe_value; m_datactrl->title=m_datadir_value;
    m_scenarioctrl->title=m_scenariodir_value; m_yadoctrl->title=m_yadodir_value;
    m_info=std::make_shared<SkinInfoPanel>();
    m_info->initialize(std::make_shared<::cw::lib::Panel>());
    if (m_converter && m_converter->m_data) {
        auto& data=*m_converter->m_data;
        m_info->set_values(data.gettext("Property/Type",""),data.gettext("Property/Name",""),
                           data.gettext("Property/Author",""),data.gettext("Property/Description",""),
                           m_converter->m_version<=std::tuple<Integer,Integer,Integer,Integer>{1,2,0,99}
                               ? true : data.getbool("Property/CW120VocationLevel",std::nullopt,false),
                           m_converter->m_initialcash);
    }
    _do_layout();
    _bind();
    OnInput({});
    sync_edits();
}

void SkinBasePanel::_bind()
{
    // 全ホストで、入力は共通 Skin Presentation が処理する。
}

void SkinBasePanel::_do_layout()
{
    // 配置は SkinConversionDialog::dialog_presentation() が管理する。
}

void SkinBasePanel::OnLeaveExeCtrl(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (!m_exe_value.empty()) queue_exe_check();
}

::cw::lib::String SkinBasePanel::_get_basedir()
{
    const auto path=std::filesystem::u8path(m_exe_value);
    return path.has_parent_path()?::cw::lib::encoding::path_to_utf8(path.parent_path()):String{};
}

void SkinBasePanel::_selected_exe(::cw::lib::String arg_exe)
{
    if(m_finished) return;
    std::error_code error;
    const bool exists=std::filesystem::is_regular_file(std::filesystem::u8path(arg_exe),error);
    const bool changed=arg_exe!=m_original_exe;
    // Python と同様、コールバックは記憶する実行ファイルを更新する。
    // 選択側はそのコールバックが戻った後にだけ、対象コントロールへ反映する。
    m_original_exe=arg_exe;
    OnInput({});
    if (!exists || !changed) {
        return;
    }
    // Python と同様、新たに選択した実行ファイルごとに確認する。
    m_auto_extract_confirmed=confirm_extraction(
        ::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(arg_exe).filename())+
        "の情報を自動抽出しますか？");
    if (m_finished || !m_auto_extract_confirmed || !m_converter) return;
    {
        extract_executable(arg_exe);
        m_datadir_value=m_converter->m_datadir;
        m_scenariodir_value=m_converter->m_scenariodir;
        m_yadodir_value=m_converter->m_yadodir;
        if (m_converter->m_data) {
            auto& data=*m_converter->m_data;
            m_info->set_values(data.gettext("Property/Type",""),data.gettext("Property/Name",""),
                               data.gettext("Property/Author",""),data.gettext("Property/Description",""),
                               m_converter->m_version<=std::tuple<Integer,Integer,Integer,Integer>{1,2,0,99},
                               m_converter->m_initialcash);
        }
        if(m_on_extracted) m_on_extracted();
        sync_edits();OnInput({});
    }}

void SkinBasePanel::OnInput(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    // Python の SkinBasePanel.OnInput と同様、全必須項目
    // に str.strip() を適用する。 全角空白など Python が空白と
    // 扱う非 ASCII 文字だけで、ALL の OK が有効にならないようにする。
    const auto nonempty=[](const String& value){return ::cw::lib::python_strip_has_text(value);};
    m_ok_enabled=m_info && nonempty(m_exe_value) && nonempty(m_datadir_value) &&
                 nonempty(m_info->m_type) && nonempty(m_info->m_name);
    if(m_exectrl) m_exectrl->title=m_exe_value;
    if(m_datactrl) m_datactrl->title=m_datadir_value;
    if(m_scenarioctrl) m_scenarioctrl->title=m_scenariodir_value;
    if(m_yadoctrl) m_yadoctrl->title=m_yadodir_value;
    request_dialog_presentation_refresh();
}

void SkinCardPanel::initialize(std::shared_ptr<::cw::lib::Panel> arg_parent, std::shared_ptr<::cw::skin::convert::Converter> arg_conv)
{
    (void)arg_parent;
    g_active_card=this;
    m_row_labels.clear();
    m_rows.clear();
    if (arg_conv) {
        // Python版はConverter("")からData/SkinBaseのカード行を作る。
        auto baseconv=std::make_shared<::cw::skin::convert::Converter>();
        baseconv->initialize("");
        Integer row=0;
        for (const auto& key:sorted_keys(baseconv->m_actioncard)) {
            const auto& card=baseconv->m_actioncard.at(key);
            m_row_labels.push_back("アクション:"+(card?card->gettext("Property/Name",""):String{})); ++row;
        }
        for (const auto& key:sorted_keys(baseconv->m_specialcard)) {
            const auto& card=baseconv->m_specialcard.at(key);
            m_row_labels.push_back("特殊カード:"+(card?card->gettext("Property/Name",""):String{})); ++row;
        }
        fill_card_labels(baseconv->m_title,m_row_labels,row,"");
        fill_card_labels(baseconv->m_yado,m_row_labels,row,"");
        fill_card_labels(baseconv->m_scenario,m_row_labels,row,"");
        fill_card_labels(baseconv->m_gameover,m_row_labels,row,"");
        baseconv->dispose();
    }
    set_values(std::move(arg_conv));
    _do_layout();
}

void SkinCardPanel::set_values(std::shared_ptr<::cw::skin::convert::Converter> arg_conv)
{
    m_rows.clear();
    if (!arg_conv) {
        m_rows.resize(m_row_labels.size());
        return;
    }
    Integer row=0;
    for (const auto& key:sorted_keys(arg_conv->m_actioncard)) {
        const auto& tree=arg_conv->m_actioncard.at(key);
        if (!tree) continue;
        if (static_cast<std::size_t>(row)>=m_rows.size()) { SkinCardRow empty{}; m_rows.push_back(empty); }
        m_rows[static_cast<std::size_t>(row)]={tree->gettext("Property/Name",""),tree->gettext("Property/Description","")}; ++row;
    }
    for (const auto& key:sorted_keys(arg_conv->m_specialcard)) {
        const auto& tree=arg_conv->m_specialcard.at(key);
        if (!tree) continue;
        if (static_cast<std::size_t>(row)>=m_rows.size()) { SkinCardRow empty{}; m_rows.push_back(empty); }
        m_rows[static_cast<std::size_t>(row)]={tree->gettext("Property/Name",""),tree->gettext("Property/Description","")}; ++row;
    }
    fill_card_values(arg_conv->m_title,m_rows,row);
    fill_card_values(arg_conv->m_yado,m_rows,row);
    fill_card_values(arg_conv->m_scenario,m_rows,row);
    fill_card_values(arg_conv->m_gameover,m_rows,row);
    if (m_rows.size()<m_row_labels.size()) m_rows.resize(m_row_labels.size());
    m_valid=true;
}

void SkinCardPanel::get_values(std::shared_ptr<::cw::skin::convert::Converter> arg_conv)
{
    m_valid=true;
    if (!arg_conv) return;
    Integer row=0;
    for (const auto& key:sorted_keys(arg_conv->m_actioncard)) {
        const auto& tree=arg_conv->m_actioncard.at(key);
        if (tree && static_cast<std::size_t>(row)<m_rows.size()) {
            tree->find_exists("Property/Name")->set_text(m_rows[static_cast<std::size_t>(row)][0]);
            tree->find_exists("Property/Description")->set_text(m_rows[static_cast<std::size_t>(row)][1]);
        }
        ++row;
    }
    for (const auto& key:sorted_keys(arg_conv->m_specialcard)) {
        const auto& tree=arg_conv->m_specialcard.at(key);
        if (tree && static_cast<std::size_t>(row)<m_rows.size()) {
            tree->find_exists("Property/Name")->set_text(m_rows[static_cast<std::size_t>(row)][0]);
            tree->find_exists("Property/Description")->set_text(m_rows[static_cast<std::size_t>(row)][1]);
        }
        ++row;
    }
    apply_card_values(arg_conv->m_title,m_rows,row);
    apply_card_values(arg_conv->m_yado,m_rows,row);
    apply_card_values(arg_conv->m_scenario,m_rows,row);
    apply_card_values(arg_conv->m_gameover,m_rows,row);
}

void SkinCardPanel::set_cell(Integer arg_row, Integer arg_col, String arg_value)
{
    if (arg_row<0 || arg_col<0 || arg_col>=2) return;
    if (static_cast<std::size_t>(arg_row)>=m_rows.size()) m_rows.resize(static_cast<std::size_t>(arg_row)+1);
    m_rows[static_cast<std::size_t>(arg_row)][static_cast<std::size_t>(arg_col)]=std::move(arg_value);
}

String SkinCardPanel::get_cell(Integer arg_row, Integer arg_col) const
{
    if (arg_row<0 || arg_col<0 || arg_col>=2 || static_cast<std::size_t>(arg_row)>=m_rows.size()) return {};
    return m_rows[static_cast<std::size_t>(arg_row)][static_cast<std::size_t>(arg_col)];
}

void SkinCardPanel::_do_layout()
{
}

namespace {
Integer skin_parse_integer(const String& value, const Integer fallback)
{
    try {
        std::size_t consumed{};
        const auto parsed=std::stoll(value,&consumed);
        return consumed==value.size()?parsed:fallback;
    } catch (...) { return fallback; }
}

bool skin_common_confirm(const String& text)
{
    using namespace ::cw::dialog::message;
    auto dialog=std::make_shared<SysMessage>();
    dialog->initialize({},"メッセージ",text,
        std::vector<MessageChoice>{{"はい",kWxIdYes,105,{}},{"いいえ",kWxIdNo,105,{}}});
    return ::cw::dialog::show_common_modal_presentation(dialog) &&
           dialog->m_return_code==kWxIdYes;
}

void skin_common_message(const String& text)
{
    using namespace ::cw::dialog::message;
    auto dialog=std::make_shared<SysMessage>();
    dialog->initialize({},"メッセージ",text,
        std::vector<MessageChoice>{{"OK",kWxIdOk,105,{}}});
    (void)::cw::dialog::show_common_modal_presentation(dialog);
}


void skin_presentation_button(::cw::dialog::DialogPresentation& presentation,
                              const std::shared_ptr<::cw::lib::Surface>& surface,
                              const String& label, const ::cw::lib::Rect rect,
                              const ::cw::dialog::DialogPresentationAction action,
                              const int index, const bool enabled=true,const bool paint=true)
{
    if(paint)::cw::lib::ui::draw_button_face(surface,rect,{enabled});
    const auto font=::cw::lib::ui::configured_font("button",13);
    const auto w=::cw::lib::ui::label_width(label,font.pixels,font.bold,font.face);
    if(paint)::cw::lib::ui::draw_label(surface,label,rect.x+std::max<Integer>(4,(rect.width-w)/2),
        rect.y+std::max<Integer>(2,(rect.height-font.pixels)/2),font.pixels,font.bold,
        enabled?std::tuple<Integer,Integer,Integer>{0,0,0}:std::tuple<Integer,Integer,Integer>{120,120,120},font.face);
    presentation.controls.push_back({action,index,static_cast<float>(rect.x),static_cast<float>(rect.y),
        static_cast<float>(rect.width),static_cast<float>(rect.height),enabled});
    presentation.controls.back().hover_highlight=true;
}

::cw::dialog::DialogPresentation render_skin_presentation(
    const String& route, const String& title, const String& warning,
    const std::vector<String>& tabs, const Integer active_tab, const bool accept_enabled,
    const bool show_import,::cw::lib::ui::RetainedSurface& retained,
    const std::shared_ptr<::cw::dialog::settings::SettingsDialog>& local={},
    const std::shared_ptr<SkinInfoPanel>& info={},
    const std::shared_ptr<SkinBasePanel>& paths={},
    const std::shared_ptr<SkinGridPresentation>& grid={})
{
    using Action=::cw::dialog::DialogPresentationAction;
    constexpr Integer width=760,height=620;
    const auto scale=std::max<::cw::lib::Real>(::cw::g_UP_WIN,1.0);
    ::cw::lib::ui::ScaledRetainedCanvas canvas(retained,{width,height},scale,{240,240,240,255});
    ::cw::dialog::DialogPresentation result;result.route=route;result.title=title;
    Integer top=44;
    if(!warning.empty())top+=28;
    std::optional<::cw::dialog::DialogPresentation> grid_body,local_body,info_body,path_body;
    // 外側の表示プロバイダーは、これらの子のホストである。単体
    // 版・ホスト内の Engine の外枠が外側に対して行う
    // のと同様、 子を読み取る前に描画開始を記録する。子の合
    // 成中に公開された更新は、次のフレームの保留として残す。
    if(grid) {grid->begin_dialog_presentation_refresh();grid_body=grid->dialog_presentation();}
    if(local) {local->begin_dialog_presentation_refresh();local_body=local->dialog_presentation();}
    if(info) {info->begin_dialog_presentation_refresh();info_body=info->dialog_presentation();}
    if(paths) {paths->begin_dialog_presentation_refresh();path_body=paths->dialog_presentation();}
    const auto mix=[](std::uint64_t& seed,std::uint64_t value) {
        seed^=value+0x9e3779b97f4a7c15ULL+(seed<<6U)+(seed>>2U);
    };
    std::uint64_t header_signature=std::hash<String>{}(warning);mix(header_signature,static_cast<std::uint64_t>(active_tab+1));
    for(const auto& tab:tabs)mix(header_signature,std::hash<String>{}(tab));
    std::uint64_t footer_signature=static_cast<std::uint64_t>(accept_enabled);mix(footer_signature,static_cast<std::uint64_t>(show_import));
    const bool paint_header=canvas.component_changed("skin.outer.header",{0,0,width,top},header_signature);
    const bool paint_footer=canvas.component_changed("skin.outer.footer",{0,height-40,width,40},footer_signature);
    struct BodyComponent {const char* key;std::optional<::cw::dialog::DialogPresentation>* body;Integer x,y;};
    std::array<BodyComponent,4> bodies{{
        {"skin.outer.grid",&grid_body,20,top},{"skin.outer.local",&local_body,20,top},
        {"skin.outer.info",&info_body,20,top+(show_import?0:144)},{"skin.outer.paths",&path_body,20,top}}};
    std::array<bool,4> paint_bodies{};
    for(std::size_t i=0;i<bodies.size();++i) {
        auto& item=bodies[i];
        if(!*item.body || !(*item.body)->surface) {(void)retained.remove_component(item.key);continue;}
        const auto& body=**item.body;
        const Integer logical_width=std::max<Integer>(1,static_cast<Integer>(std::llround(body.surface->size.width/scale)));
        const Integer logical_height=std::max<Integer>(1,static_cast<Integer>(std::llround(body.surface->size.height/scale)));
        std::uint64_t signature=body.surface_revision;mix(signature,reinterpret_cast<std::uintptr_t>(body.surface.get()));
        paint_bodies[i]=canvas.component_changed(item.key,{item.x,item.y,logical_width,logical_height},signature);
    }
    canvas.capture_base_and_restore();
    const auto make_layer=[](Integer w,Integer h) {
        auto layer=std::make_shared<::cw::lib::Surface>();layer->size={w,h};
        layer->rgba.assign(static_cast<std::size_t>(w)*h*4U,0);
        ::cw::lib::ui::fill_rect(layer,{0,0,w,h},{240,240,240,255});return layer;
    };
    auto header=paint_header?make_layer(width,top):canvas.surface;
    const auto warning_font=::cw::lib::ui::configured_font("dlgmsg2",12,true);
    Integer tab_x=10;
    for(std::size_t i=0;i<tabs.size();++i) {
        const Integer tab_width=std::max<Integer>(72,static_cast<Integer>((width-20)/tabs.size())-4);
        const ::cw::lib::Rect rect{tab_x,8,tab_width,28};
        if(paint_header&&static_cast<Integer>(i)==active_tab)::cw::lib::ui::fill_rect(header,rect,{255,255,255,255});
        skin_presentation_button(result,header,tabs[i],rect,Action::Auxiliary,1000+static_cast<int>(i),true,paint_header);
        tab_x+=tab_width+4;
    }
    if(paint_header&&!warning.empty())::cw::lib::ui::draw_label(header,warning,14,44,warning_font.pixels,
        warning_font.bold,{130,0,0},warning_font.face);
    if(paint_header)canvas.composite({0,0,width,top},header);
    auto footer=paint_footer?make_layer(width,40):canvas.surface;
    const auto footer_button=[&](const String& label,::cw::lib::Rect rect,Action action,int index,bool enabled=true) {
        const auto before=result.controls.size();rect.y-=height-40;
        skin_presentation_button(result,footer,label,rect,action,index,enabled,paint_footer);
        result.controls[before].y+=height-40;
    };
    if(show_import)footer_button("CardWirth用リソースのインポート...",{12,height-40,270,30},Action::Create,0);
    footer_button(show_import?"OK":"決定",{width-224,height-40,100,30},Action::Accept,0,accept_enabled);
    footer_button(show_import?"キャンセル":"中止",{width-114,height-40,100,30},Action::Cancel,0);
    if(paint_footer)canvas.composite({0,height-40,width,40},footer);
    const auto outer_control_count=result.controls.size();
    for(std::size_t i=0;i<bodies.size();++i)if(*bodies[i].body) {
        auto& body=**bodies[i].body;
        const auto dx=canvas.pixel(bodies[i].x),dy=canvas.pixel(bodies[i].y);
        if(paint_bodies[i]&&body.surface)::cw::lib::ui::blend_surface(canvas.surface,
            std::const_pointer_cast<::cw::lib::Surface>(body.surface),dx,dy);
    }
    if(grid_body) {
        auto& body=*grid_body;const auto dx=canvas.pixel(20),dy=canvas.pixel(top);
        for(auto control:body.controls) {
            control.index+=30000;control.x+=static_cast<float>(dx);control.y+=static_cast<float>(dy);
            result.controls.push_back(std::move(control));
        }
        result.wants_text_input=body.wants_text_input;result.input_text=body.input_text;
        result.composition=body.composition;result.composition_start=body.composition_start;result.composition_length=body.composition_length;
    }
    if(local_body) {
        auto& body=*local_body;const auto dx=canvas.pixel(20),dy=canvas.pixel(top);
        if(local->m_settings_popup) result.controls.push_back({Action::Auxiliary,
            SkinSettingsPresentation::local_control_base-1,0,0,
            static_cast<float>(width*scale),static_cast<float>(height*scale),true});
        for(auto control:body.controls) {
            control.index+=SkinSettingsPresentation::local_control_base;
            control.x+=static_cast<float>(dx); control.y+=static_cast<float>(dy);
            result.controls.push_back(std::move(control));
        }
        result.wants_text_input=body.wants_text_input; result.input_text=body.input_text;
        result.composition=body.composition; result.composition_start=body.composition_start;
        result.composition_length=body.composition_length;
        if(local->document_request_pending())
            for(auto& control:result.controls) if(control.action!=Action::Cancel) control.enabled=false;
    }
    if(info_body) {
        auto& body=*info_body;const auto dx=canvas.pixel(20),dy=canvas.pixel(top+(show_import?0:144));
        if(info->m_type_popup) result.controls.push_back({Action::Auxiliary,10099,0,0,
            static_cast<float>(width*scale),static_cast<float>(height*scale),true});
        for(auto control:body.controls) {
            control.index+=10000;control.x+=static_cast<float>(dx);control.y+=static_cast<float>(dy);
            result.controls.push_back(std::move(control));
        }
        result.wants_text_input=body.wants_text_input;result.input_text=body.input_text;
        result.composition=body.composition;result.composition_start=body.composition_start;
        result.composition_length=body.composition_length;
    }
    if(path_body) {
        auto& body=*path_body;const auto dx=canvas.pixel(20),dy=canvas.pixel(top);
        for(auto control:body.controls) {
            control.index+=5000;control.x+=static_cast<float>(dx);control.y+=static_cast<float>(dy);
            if(info && info->m_type_popup) control.enabled=false;
            result.controls.push_back(std::move(control));
        }
        if(body.wants_text_input) {
            result.wants_text_input=true;result.input_text=body.input_text;result.composition=body.composition;
            result.composition_start=body.composition_start;result.composition_length=body.composition_length;
        }
        if(paths->document_request_pending()) {
            for(auto& control:result.controls) if(control.action!=Action::Cancel) control.enabled=false;
            result.wants_text_input=false;result.composition.clear();
        }
    }
    if(scale>1.0+1e-9)for(std::size_t i=0;i<outer_control_count;++i) {
        auto& control=result.controls[i];
        control.x=static_cast<float>(control.x*scale);control.y=static_cast<float>(control.y*scale);
        control.width=static_cast<float>(control.width*scale);control.height=static_cast<float>(control.height*scale);
    }
    canvas.finish();result.surface=canvas.surface;result.damage=retained.damage;result.surface_revision=retained.revision;
    return result;
}
}

namespace {
constexpr int grid_width=720,grid_height=480,grid_header=26,grid_row_height=24;
constexpr int grid_bottom=434,grid_view_height=grid_bottom-grid_header;
constexpr int grid_right=704,grid_vscroll=-2,grid_hscroll=-3;
auto grid_font() {return ::cw::lib::ui::configured_font("inputname",12);}
std::vector<String> grid_text_lines(const String& text) {
    std::vector<String> lines;
    std::size_t start=0;
    do {
        const auto end=text.find('\n',start);
        auto line=text.substr(start,end==String::npos?String::npos:end-start);
        if(!line.empty() && line.back()=='\r') line.pop_back();
        lines.push_back(std::move(line));
        if(end==String::npos) break;
        start=end+1;
    } while(start<=text.size());
    return lines;
}
std::shared_ptr<::cw::lib::Surface> grid_surface(int width,int height) {
    auto result=std::make_shared<::cw::lib::Surface>();result->size={width,height};
    result->rgba.assign(static_cast<std::size_t>(width)*height*4U,255);return result;
}
}
int SkinGridPresentation::row_label_width() const
{
    if(label_width>0) return std::clamp(label_width,32,grid_right-24);
    if(m_label_extent>0) return m_label_extent;
    const auto font=grid_font();int width=40;
    if(row_count && row_label) for(int r=0;r<row_count();++r)
        width=std::max(width,static_cast<int>(::cw::lib::ui::label_width(row_label(r),font.pixels,font.bold,font.face))+12);
    return m_label_extent=std::min(width,320);
}
int SkinGridPresentation::total_width() const
{
    int result=0;for(const auto& c:columns) result+=std::max(24,c.width);return result;
}
bool SkinGridPresentation::is_column_resize_control(int index) const noexcept
{
    return index<=column_resize_base && index>column_resize_base-static_cast<int>(columns.size());
}
bool SkinGridPresentation::is_resize_control(int index) const noexcept
{
    return index==row_label_resize || is_column_resize_control(index) ||
        (index<=row_resize_base && index>row_resize_base-(row_count?row_count():0));
}
int SkinGridPresentation::row_height(int row) const
{
    return row>=0 && row<static_cast<int>(m_row_heights.size())?m_row_heights[row]:grid_row_height;
}
Integer SkinGridPresentation::row_top(int row) const
{
    const int count=row_count?row_count():0;
    Integer y=0;
    for(int r=0;r<std::clamp(row,0,count);++r) y+=row_height(r);
    return y;
}
Integer SkinGridPresentation::vertical_position() const
{
    return row_top(m_scroll)+m_row_offset;
}
int SkinGridPresentation::row_at(Integer position) const
{
    const int count=row_count?row_count():0;
    position=std::max<Integer>(0,position);
    int row=0;
    while(row+1<count && position>=row_height(row)) position-=row_height(row++);
    return row;
}
void SkinGridPresentation::set_vertical_position(Integer position)
{
    const int count=row_count?row_count():0;
    position=std::clamp<Integer>(position,0,std::max<Integer>(0,row_top(count)-grid_view_height));
    m_scroll=row_at(position);
    m_row_offset=count?position-row_top(m_scroll):0;
}
::cw::lib::Rect SkinGridPresentation::cell_rect(int row,int column) const
{
    int x=row_label_width()-m_horizontal;
    for(int c=0;c<column;++c) x+=std::max(24,columns[c].width);
    return {x,grid_header+row_top(row)-vertical_position(),std::max(24,columns[column].width),row_height(row)};
}
void SkinGridPresentation::reveal_cell()
{
    const int count=row_count?row_count():0;
    if(!count || columns.empty()) return;
    m_row=std::clamp(m_row,0,count-1);m_column=std::clamp(m_column,0,static_cast<int>(columns.size())-1);
    const auto top=row_top(m_row),bottom=top+row_height(m_row),position=vertical_position();
    if(top<position || row_height(m_row)>grid_view_height) set_vertical_position(top);
    else if(bottom>position+grid_view_height) set_vertical_position(bottom-grid_view_height);
    else set_vertical_position(position);
    const auto rect=cell_rect(m_row,m_column);const int left=row_label_width();
    if(rect.x<left) m_horizontal-=static_cast<int>(left-rect.x);
    else if(rect.x+rect.width>grid_right) m_horizontal+=static_cast<int>(rect.x+rect.width-grid_right);
    m_horizontal=std::clamp(m_horizontal,0,std::max(0,total_width()-(grid_right-left)));
}
bool SkinGridPresentation::finish_edit(bool accept)
{
    m_resize_control=0;
    if(!m_editing) return true;
    if(accept) {
        if(!m_edit.composition.empty()) return false;
        if(!row_count || m_row>=row_count() || m_column>=static_cast<int>(columns.size())) {
            m_editing=false;return true;
        }
        auto value=m_edit.text;bool valid=true;
        if(columns[m_column].kind==CellKind::Integer) {
            try {std::size_t used{};auto n=std::stoll(value,&used);valid=used==value.size() && n>=-99 && n<=99;
                if(valid) value=std::to_string(n);} catch(...) {valid=false;}
            if(!valid) m_validation_error="整数を -99 ～ 99 の範囲で入力してください。";
        } else if(columns[m_column].kind==CellKind::Decimal) {
            value=normalize_decimal(value,"");valid=!value.empty();
            if(!valid) m_validation_error="有効な数値を入力してください。";
        }
        if(!valid) {request_dialog_presentation_refresh();return false;}
        if(write_cell) write_cell(m_row,m_column,std::move(value));
    }
    m_editing=false;m_dragging=false;m_validation_error.clear();m_edit.reset("");
    request_dialog_presentation_refresh();return true;
}
bool SkinGridPresentation::is_selection_control(int index) const noexcept
{
    const int count=row_count?row_count():0;
    return index==select_all_control ||
        (index<=row_select_base && index>row_select_base-count) ||
        (index<=column_select_base && index>column_select_base-static_cast<int>(columns.size()));
}
bool SkinGridPresentation::cell_selected(int row,int column) const
{
    return m_selected.contains({row,column});
}
void SkinGridPresentation::clear_selection()
{
    m_validation_error.clear();
    m_selected.clear();m_extend_base.clear();
    m_extent_row=m_row;m_extent_column=m_column;
    request_dialog_presentation_refresh();
}
void SkinGridPresentation::prune_selection()
{
    const int count=row_count?row_count():0,ncols=static_cast<int>(columns.size());
    const auto invalid=[&](const auto& cell) {
        return cell.first<0 || cell.first>=count || cell.second<0 || cell.second>=ncols;
    };
    std::erase_if(m_selected,invalid);std::erase_if(m_extend_base,invalid);
    m_row=std::clamp(m_row,0,std::max(0,count-1));
    m_column=std::clamp(m_column,0,std::max(0,ncols-1));
    m_extent_row=std::clamp(m_extent_row,0,std::max(0,count-1));
    m_extent_column=std::clamp(m_extent_column,0,std::max(0,ncols-1));
}
void SkinGridPresentation::reveal_target(int row,int column)
{
    const auto saved=std::pair{m_row,m_column};
    m_row=row;m_column=column;reveal_cell();
    m_row=saved.first;m_column=saved.second;
}
void SkinGridPresentation::extend_selection(int row,int column,SelectionKind kind)
{
    const int count=row_count?row_count():0,ncols=static_cast<int>(columns.size());
    if(count<=0 || ncols<=0) return;
    m_validation_error.clear();
    row=std::clamp(row,0,count-1);column=std::clamp(column,0,ncols-1);
    int top=std::min(m_row,row),bottom=std::max(m_row,row);
    int left=std::min(m_column,column),right=std::max(m_column,column);
    if(kind==SelectionKind::Rows) {left=0;right=ncols-1;column=ncols-1;}
    if(kind==SelectionKind::Columns) {top=0;bottom=count-1;row=count-1;}
    m_selected=m_extend_base;
    for(int r=top;r<=bottom;++r) for(int col=left;col<=right;++col) m_selected.emplace(r,col);
    m_extent_row=row;m_extent_column=column;
    reveal_target(kind==SelectionKind::Columns?m_row:row,kind==SelectionKind::Rows?m_column:column);
    request_dialog_presentation_refresh();
}
void SkinGridPresentation::select_all()
{
    m_validation_error.clear();
    const int count=row_count?row_count():0,ncols=static_cast<int>(columns.size());
    m_selected.clear();m_extend_base.clear();
    for(int r=0;r<count;++r) for(int col=0;col<ncols;++col) m_selected.emplace(r,col);
    m_extent_row=std::max(0,count-1);m_extent_column=std::max(0,ncols-1);
    request_dialog_presentation_refresh();
}
void SkinGridPresentation::select_target(int row,int column,SelectionKind kind,
    ::cw::dialog::DialogPresentationModifiers modifiers)
{
    using namespace ::cw::dialog;
    const int count=row_count?row_count():0,ncols=static_cast<int>(columns.size());
    if(count<=0 || ncols<=0 || !finish_edit(true)) return;
    m_validation_error.clear();
    row=std::clamp(row,0,count-1);column=std::clamp(column,0,ncols-1);
    const bool shift=modifiers&DialogPresentationModifierShift,ctrl=modifiers&DialogPresentationModifierControl;
    if(shift && !ctrl) {extend_selection(row,column,kind);return;}
    int top=row,bottom=row,left=column,right=column;
    if(kind==SelectionKind::Rows) {left=0;right=ncols-1;}
    if(kind==SelectionKind::Columns) {top=0;bottom=count-1;}
    m_row=row;m_column=column;
    if(ctrl && !shift) {
        bool full=true;
        for(int r=top;r<=bottom;++r) for(int col=left;col<=right;++col)
            full=full && cell_selected(r,col);
        m_extend_base=m_selected;
        for(int r=top;r<=bottom;++r) for(int col=left;col<=right;++col) {
            if(full) m_selected.erase({r,col});
            else m_selected.emplace(r,col);
        }
        if(full) m_extend_base=m_selected;
    } else {
        clear_selection();
        if(kind!=SelectionKind::Cells)
            for(int r=top;r<=bottom;++r) for(int col=left;col<=right;++col) m_selected.emplace(r,col);
    }
    m_extent_row=kind==SelectionKind::Columns?count-1:row;
    m_extent_column=kind==SelectionKind::Rows?ncols-1:column;
    reveal_cell();request_dialog_presentation_refresh();
}
bool SkinGridPresentation::decode_selection_control(int index,int& row,int& column,SelectionKind& kind) const
{
    const int count=row_count?row_count():0,ncols=static_cast<int>(columns.size());
    if(count<=0 || ncols<=0) return false;
    if(index>=0 && index/ncols<count) {row=index/ncols;column=index%ncols;kind=SelectionKind::Cells;return true;}
    if(index<=row_select_base && index>row_select_base-count) {
        row=row_select_base-index;column=m_column;kind=SelectionKind::Rows;return true;
    }
    if(index<=column_select_base && index>column_select_base-ncols) {
        row=m_row;column=column_select_base-index;kind=SelectionKind::Columns;return true;
    }
    return false;
}
std::pair<int,int> SkinGridPresentation::point_cell(float x,float y) const
{
    int col=0;
    Integer edge=row_label_width()-m_horizontal;
    while(col+1<static_cast<int>(columns.size()) && x>=edge+std::max(24,columns[col].width))
        edge+=std::max(24,columns[col++].width);
    return {row_at(vertical_position()+static_cast<Integer>(y-grid_header)),col};
}
std::optional<String> SkinGridPresentation::selection_text() const
{
    if(!read_cell || !row_count || row_count()<=0 || columns.empty()) return String{};
    int top=m_row,bottom=m_row,left=m_column,right=m_column;
    if(!m_selected.empty()) {
        top=row_count();left=static_cast<int>(columns.size());bottom=right=-1;
        for(const auto& [row,col]:m_selected) {
            top=std::min(top,row);bottom=std::max(bottom,row);left=std::min(left,col);right=std::max(right,col);
        }
        if(static_cast<std::size_t>(bottom-top+1)*static_cast<std::size_t>(right-left+1)!=m_selected.size())
            return std::nullopt; // wx.Grid と同様、離れた複数の範囲は TSV にシリアライズできない。
    }
    String text;
    for(int row=std::max(0,top);row<=std::min(row_count()-1,bottom);++row) {
        for(int col=std::max(0,left);col<=std::min(static_cast<int>(columns.size())-1,right);++col) {
            if(col>left) text+='\t';
            text+=read_cell(row,col);
        }
        text+='\n';
    }
    return text;
}

void SkinGridPresentation::begin_edit()
{
    if(m_editing || !row_count || !row_count() || columns.empty() || !read_cell) return;
    clear_selection();reveal_cell();m_edit.reset(read_cell(m_row,m_column),true);m_editing=true;
    request_dialog_presentation_refresh();
}
::cw::dialog::DialogPresentation SkinGridPresentation::dialog_presentation()
{
    namespace ui=::cw::lib::ui;namespace edit=::cw::dialog::textedit;
    using Action=::cw::dialog::DialogPresentationAction;
    ::cw::dialog::DialogPresentation result;result.route="SKIN_GRID";
    m_label_extent=0;prune_selection();
    const auto font=grid_font();const int left=row_label_width(),count=row_count?row_count():0;
    set_vertical_position(vertical_position());
    m_horizontal=std::clamp(m_horizontal,0,std::max(0,total_width()-(grid_right-left)));
    const auto scale=std::max<::cw::lib::Real>(::cw::g_UP_WIN,1.0);
    auto& retained=dialog_retained_surface();
    ui::ScaledRetainedCanvas canvas(retained,{grid_width,grid_height},scale,{255,255,255,255});
    const auto mix=[](std::uint64_t& seed,std::uint64_t value) {
        seed^=value+0x9e3779b97f4a7c15ULL+(seed<<6U)+(seed>>2U);
    };
    std::uint64_t header_signature=static_cast<std::uint64_t>(left);mix(header_signature,static_cast<std::uint64_t>(m_horizontal+1));
    for(int c=0;c<static_cast<int>(columns.size());++c) {
        mix(header_signature,std::hash<String>{}(columns[c].title));mix(header_signature,static_cast<std::uint64_t>(columns[c].width));
        bool selected=count>0;for(int r=0;r<count;++r)selected=selected&&cell_selected(r,c);
        mix(header_signature,static_cast<std::uint64_t>(selected));
    }
    const bool paint_header=canvas.component_changed("skin.grid.header",{0,0,grid_right,grid_header},header_signature);
    const auto position=vertical_position();
    std::map<int,bool> paint_rows;std::map<int,::cw::lib::Rect> row_regions;
    std::set<String> active_row_components;
    for(int r=m_scroll;r<count;++r) {
        const Integer y=grid_header+row_top(r)-position,row_h=row_height(r);
        if(y>=grid_bottom)break;
        const Integer y0=std::max<Integer>(grid_header,y),y1=std::min<Integer>(grid_bottom,y+row_h);
        if(y1<=y0)continue;
        std::uint64_t signature=static_cast<std::uint64_t>(r+1);mix(signature,static_cast<std::uint64_t>(position+1));
        mix(signature,static_cast<std::uint64_t>(m_horizontal+1));mix(signature,static_cast<std::uint64_t>(row_h));
        if(row_label)mix(signature,std::hash<String>{}(row_label(r)));
        for(int c=0;c<static_cast<int>(columns.size());++c) {
            mix(signature,static_cast<std::uint64_t>(columns[c].width));
            if(read_cell)mix(signature,std::hash<String>{}(read_cell(r,c)));
            mix(signature,static_cast<std::uint64_t>(cell_selected(r,c)));
        }
        mix(signature,static_cast<std::uint64_t>(m_row==r));
        if(m_row==r) {
            mix(signature,static_cast<std::uint64_t>(m_column+1));mix(signature,static_cast<std::uint64_t>(m_editing));
            mix(signature,std::hash<String>{}(m_edit.text));mix(signature,std::hash<String>{}(m_edit.composition));
            mix(signature,m_edit.caret);mix(signature,m_edit.anchor);mix(signature,static_cast<std::uint64_t>(m_edit.horizontal+1));
            mix(signature,static_cast<std::uint64_t>(m_edit.composition_start+1));
            mix(signature,static_cast<std::uint64_t>(m_edit.composition_length+1));
        }
        const auto key="skin.grid.row."+std::to_string(r);active_row_components.insert(key);
        const ::cw::lib::Rect region{0,y0,grid_right,y1-y0};row_regions[r]=region;
        paint_rows[r]=canvas.component_changed(key,region,signature);
    }
    std::vector<String> retired_rows;
    for(const auto& [key,unused]:retained.components)
        if(key.rfind("skin.grid.row.",0)==0&&!active_row_components.contains(key))retired_rows.push_back(key);
    for(const auto& key:retired_rows)(void)retained.remove_component(key);
    const auto total_height=row_top(count);const int visible=grid_right-left,total=total_width();
    std::uint64_t vertical_signature=static_cast<std::uint64_t>(total_height);mix(vertical_signature,static_cast<std::uint64_t>(position+1));
    std::uint64_t horizontal_signature=static_cast<std::uint64_t>(total);mix(horizontal_signature,static_cast<std::uint64_t>(visible));
    mix(horizontal_signature,static_cast<std::uint64_t>(m_horizontal+1));
    const bool paint_vertical=total_height>grid_view_height&&canvas.component_changed(
        "skin.grid.vertical",{grid_right,grid_header,16,grid_view_height},vertical_signature);
    if(total_height<=grid_view_height)(void)retained.remove_component("skin.grid.vertical");
    const bool paint_horizontal=total>visible&&canvas.component_changed(
        "skin.grid.horizontal",{left,grid_bottom,visible,16},horizontal_signature);
    if(total<=visible)(void)retained.remove_component("skin.grid.horizontal");
    const bool paint_status=canvas.component_changed("skin.grid.status",{0,450,grid_width,30},
        std::hash<String>{}(m_validation_error));
    canvas.capture_base_and_restore();
    const bool any_row_paint=std::any_of(paint_rows.begin(),paint_rows.end(),[](const auto& item){return item.second;});
    const bool any_paint=paint_header||any_row_paint||paint_vertical||paint_horizontal||paint_status;
    auto surface=any_paint?grid_surface(grid_width,grid_height):std::shared_ptr<::cw::lib::Surface>{};
    // ユーザーが表示領域より高い行を作れるため、セル画像の確保前に
    // 縦横両方を切り取る。 一部だけ見える行で見出しを覆わない。
    auto draw=[&](::cw::lib::Rect rect,const String& text,bool header,bool selected,bool editing,bool heading=false,bool cursor=false,bool paint=true) {
        const int x0=static_cast<int>(std::max<Integer>(header?0:left,rect.x));
        const int x1=static_cast<int>(std::min<Integer>(grid_right,rect.x+rect.width));
        const int y0=static_cast<int>(std::max<Integer>(heading?0:grid_header,rect.y));
        const int y1=static_cast<int>(std::min<Integer>(heading?grid_header:grid_bottom,rect.y+rect.height));
        if(x1<=x0 || y1<=y0 || !paint) return;
        const Integer local_top=rect.y-y0;
        auto cell=grid_surface(static_cast<int>(rect.width),y1-y0);
        ui::fill_rect(cell,{0,0,rect.width,y1-y0},selected && !editing?::cw::lib::Color{224,238,255,255}:
            header || heading?::cw::lib::Color{240,240,240,255}: ::cw::lib::Color{255,255,255,255});
        if(editing) {
            // 高いセルでもエディターは一行の高さにする。 セル全高の
            // 画像を確保せず、内容をセルに合わせてスクロールする。
            auto inner=grid_surface(static_cast<int>(rect.width)-6,static_cast<int>(std::max<Integer>(20,font.pixels+12)));
            const auto display=edit::display_text(m_edit.text,m_edit.caret,m_edit.anchor,
                {m_edit.composition,m_edit.composition_start,m_edit.composition_length});
            edit::draw_single_line(inner,display,font,4,true,m_edit.horizontal);
            ui::blend_surface(cell,inner,3,local_top+2);
        } else {
            const auto lines=grid_text_lines(text);
            const Integer advance=font.pixels+2;
            Integer y=local_top+(header || heading?(rect.height-static_cast<Integer>(lines.size())*advance)/2:6);
            for(const auto& line:lines) {
                if(y+advance>0 && y<y1-y0)
                    ui::draw_label(cell,line,4,y,font.pixels,font.bold,{0,0,0},font.face);
                y+=advance;
            }
        }
        ui::fill_rect(cell,{0,local_top+rect.height-1,rect.width,1},{200,200,200,255});
        ui::fill_rect(cell,{rect.width-1,0,1,y1-y0},{200,200,200,255});
        if(cursor) {
            const ::cw::lib::Color blue{0,100,200,255};
            ui::fill_rect(cell,{0,local_top,rect.width,1},blue);
            ui::fill_rect(cell,{0,local_top+rect.height-1,rect.width,1},blue);
            ui::fill_rect(cell,{0,0,1,y1-y0},blue);ui::fill_rect(cell,{rect.width-1,0,1,y1-y0},blue);
        }
        auto clipped=grid_surface(x1-x0,y1-y0);
        ui::blend_surface(clipped,cell,rect.x-x0,0);ui::blend_surface(surface,clipped,x0,y0);
    };
    for(int c=0;c<static_cast<int>(columns.size());++c) {
        auto rect=cell_rect(m_scroll,c);rect.y=0;rect.height=grid_header;
        bool selected=count>0;for(int r=0;r<count;++r) selected=selected && cell_selected(r,c);
        draw(rect,columns[c].title,false,selected,false,true,false,paint_header);
        const int x0=static_cast<int>(std::max<Integer>(left,rect.x));
        const int x1=static_cast<int>(std::min<Integer>(grid_right,rect.x+rect.width));
        if(x1>x0) result.controls.push_back({Action::Select,column_select_base-c,
            static_cast<float>(x0),0,static_cast<float>(x1-x0),grid_header,true,true});
    }
    draw({0,0,left,grid_header},"",true,false,false,true,false,paint_header);
    result.controls.push_back({Action::Select,select_all_control,0,0,static_cast<float>(left),grid_header,
        count>0 && !columns.empty(),true});
    // Python の wx.Grid と同様、共通表で
    // も見出しから列幅を変更できるようにする。 すべての
    // 表示倍率で、描画と当たり判定に同じ論理配置を使う。
    for(int c=0;c<static_cast<int>(columns.size());++c) {
        const auto rect=cell_rect(m_scroll,c);
        const int boundary=static_cast<int>(rect.x+rect.width);
        if(boundary<=left || boundary>grid_right) continue;
        const int x0=std::max(left,boundary-3),x1=std::min(grid_right,boundary+3);
        result.controls.push_back({Action::Auxiliary,column_resize_base-c,
            static_cast<float>(x0),0,static_cast<float>(x1-x0),grid_header,true,true});
        result.controls.back().activate_on_double_click=true;
    }
    result.controls.push_back({Action::Auxiliary,row_label_resize,static_cast<float>(left-3),0,
        6,grid_header,true,true});
    result.controls.back().activate_on_double_click=true;
    for(int r=m_scroll;r<count;++r) {
        const Integer y=grid_header+row_top(r)-position,height=row_height(r);
        if(y>=grid_bottom) break;
        bool row_selected=!columns.empty();
        for(int c=0;c<static_cast<int>(columns.size());++c) row_selected=row_selected && cell_selected(r,c);
        draw({0,y,left,height},row_label?row_label(r):"",true,row_selected,false,false,false,paint_rows[r]);
        const int label_y0=static_cast<int>(std::max<Integer>(grid_header,y));
        const int label_y1=static_cast<int>(std::min<Integer>(grid_bottom,y+height));
        if(label_y1>label_y0) result.controls.push_back({Action::Select,row_select_base-r,0,
            static_cast<float>(label_y0),static_cast<float>(left),static_cast<float>(label_y1-label_y0),true,true});
        for(int c=0;c<static_cast<int>(columns.size());++c) {
            auto rect=cell_rect(r,c);const bool cursor=r==m_row && c==m_column;
            draw(rect,read_cell?read_cell(r,c):"",false,cell_selected(r,c) || cursor,
                cursor && m_editing,false,cursor,paint_rows[r]);
            const int x0=std::max<int>(left,rect.x),x1=std::min<int>(grid_right,rect.x+rect.width);
            const int y0=static_cast<int>(std::max<Integer>(grid_header,y));
            const int y1=static_cast<int>(std::min<Integer>(grid_bottom,y+height));
            if(x1>x0 && y1>y0) result.controls.push_back({Action::Select,r*static_cast<int>(columns.size())+c,
                static_cast<float>(x0),static_cast<float>(y0),static_cast<float>(x1-x0),
                static_cast<float>(y1-y0),true,true});
        }
        if(y+height<=grid_bottom) {
            const int y0=static_cast<int>(std::max<Integer>(grid_header,y+height-3));
            const int y1=static_cast<int>(std::min<Integer>(grid_bottom,y+height+3));
            result.controls.push_back({Action::Auxiliary,row_resize_base-r,0,static_cast<float>(y0),
                static_cast<float>(left),static_cast<float>(y1-y0),true,true});
            result.controls.back().activate_on_double_click=true;
        }
    }
    // 区切り線のつかみ領域は、隣接する両方の行・列ラベルより優先する。
    std::stable_partition(result.controls.begin(),result.controls.end(),[&](const auto& control) {
        return !is_resize_control(control.index);
    });
    if(total_height>grid_view_height) {
        if(paint_vertical)ui::fill_rect(surface,{grid_right,grid_header,16,grid_view_height},{230,230,230,255});
        const int thumb=static_cast<int>(std::max<Integer>(20,grid_view_height*grid_view_height/total_height));
        const auto y=grid_header+(grid_view_height-thumb)*position/(total_height-grid_view_height);
        if(paint_vertical)ui::fill_rect(surface,{grid_right+2,y,12,thumb},{150,150,150,255});
        result.controls.push_back({Action::Auxiliary,grid_vscroll,grid_right,grid_header,16,
            grid_view_height,true,true});
    }
    if(total>visible) {
        if(paint_horizontal)ui::fill_rect(surface,{left,grid_bottom,visible,16},{230,230,230,255});
        const int thumb=std::max(20,visible*visible/total);
        const int x=left+(visible-thumb)*m_horizontal/(total-visible);
        if(paint_horizontal)ui::fill_rect(surface,{x,grid_bottom+2,thumb,12},{150,150,150,255});
        result.controls.push_back({Action::Auxiliary,grid_hscroll,static_cast<float>(left),grid_bottom,
            static_cast<float>(visible),16,true,true});
    }
    if(paint_status)ui::draw_label(surface,m_validation_error,4,456,font.pixels,font.bold,{180,0,0},font.face);
    // エディターを開く前でも、文字イベントを選択セルへ届ける。
    result.wants_text_input=count>0 && !columns.empty() && m_resize_control==0 && !m_selecting;
    if(m_editing) {
        result.input_text=m_edit.text;result.composition=m_edit.composition;
        result.composition_start=m_edit.composition_start;result.composition_length=m_edit.composition_length;
    }
    const auto composite_region=[&](::cw::lib::Rect rect,bool paint) {
        if(!paint||rect.width<=0||rect.height<=0)return;
        auto layer=grid_surface(rect.width,rect.height);ui::blend_surface(layer,surface,-rect.x,-rect.y);
        canvas.composite(rect,layer);
    };
    composite_region({0,0,grid_right,grid_header},paint_header);
    for(const auto& [row,rect]:row_regions)composite_region(rect,paint_rows[row]);
    composite_region({grid_right,grid_header,16,grid_view_height},paint_vertical);
    composite_region({left,grid_bottom,visible,16},paint_horizontal);
    composite_region({0,450,grid_width,30},paint_status);
    const auto control_scale=static_cast<float>(scale);
    for(auto& c:result.controls) {c.x*=control_scale;c.y*=control_scale;c.width*=control_scale;c.height*=control_scale;}
    canvas.finish();result.surface=canvas.surface;result.damage=retained.damage;result.surface_revision=retained.revision;return result;
}
void SkinGridPresentation::dialog_presentation_action(::cw::dialog::DialogPresentationAction action,int index)
{
    using Action=::cw::dialog::DialogPresentationAction;
    if(action==Action::Activate && is_resize_control(index)) {auto_size(index);return;}
    if(action==Action::Cancel) {
        if(m_editing && !m_edit.composition.empty()) {m_edit.set_composition("");request_dialog_presentation_refresh();}
        else if(m_editing) finish_edit(false);
        else if(m_selecting) {
            if(m_selection_moved) {
                m_selected=m_drag_selection;m_extent_row=m_drag_extent_row;m_extent_column=m_drag_extent_column;
            }
            dialog_presentation_pointer_cancel();m_suppress_drag_until_release=true;
        } else clear_selection();
        return;
    }
    if(action==Action::Accept) {dialog_presentation_enter(m_modifiers);return;}
    if(action!=Action::Select && action!=Action::Activate) return;
    if(index==select_all_control) {if(finish_edit(true)) select_all();return;}
    int row{},col{};SelectionKind kind;
    if(!decode_selection_control(index,row,col,kind)) return;
    select_target(row,col,kind,m_modifiers);
    if(action==Action::Activate && kind==SelectionKind::Cells && row==m_row && col==m_column) begin_edit();
    request_dialog_presentation_refresh();
}
void SkinGridPresentation::auto_size(int control)
{
    if(!finish_edit(true)) return;
    const auto font=grid_font();
    const int count=row_count?row_count():0;
    const auto width=[&](const String& text) {
        Integer value=0;
        for(const auto& line:grid_text_lines(text))
            value=std::max(value,::cw::lib::ui::label_width(line,font.pixels,font.bold,font.face));
        return value;
    };
    const auto position=vertical_position();
    if(control==row_label_resize) {
        Integer value=40;
        if(row_label) for(int r=0;r<count;++r) value=std::max(value,width(row_label(r))+12);
        label_width=static_cast<int>(std::clamp<Integer>(value,32,grid_right-24));
    } else if(control<=row_resize_base) {
        const int row=row_resize_base-control;
        std::size_t lines=row_label?grid_text_lines(row_label(row)).size():1;
        if(read_cell) for(int c=0;c<static_cast<int>(columns.size());++c)
            lines=std::max(lines,grid_text_lines(read_cell(row,c)).size());
        const auto height=std::clamp<Integer>(static_cast<Integer>(lines)*(font.pixels+2)+10,20,4096);
        if(row>=static_cast<int>(m_row_heights.size())) m_row_heights.resize(row+1,grid_row_height);
        m_row_heights[row]=static_cast<int>(height);
    } else {
        const int column=column_resize_base-control;
        Integer value=width(columns[column].title);
        if(read_cell) for(int r=0;r<count;++r) value=std::max(value,width(read_cell(r,column)));
        columns[column].width=static_cast<int>(std::clamp<Integer>(value+8,24,4096));
    }
    set_vertical_position(position);
    m_horizontal=std::clamp(m_horizontal,0,std::max(0,total_width()-(grid_right-row_label_width())));
    request_dialog_presentation_refresh();
}
void SkinGridPresentation::dialog_presentation_text(const std::string& text,bool composition)
{
    if(m_resize_control!=0 || m_selecting || m_suppress_drag_until_release) return;
    if(!m_editing) begin_edit();if(!m_editing) return;
    if(composition) m_edit.set_composition(text);
    else {if(text=="\b" && !m_edit.composition.empty()) return;m_edit.insert(text,0,false);}
    m_validation_error.clear();request_dialog_presentation_refresh();
}
void SkinGridPresentation::dialog_presentation_text_event(const ::cw::dialog::DialogTextEditingEvent& event)
{
    if(m_resize_control!=0 || m_selecting || m_suppress_drag_until_release) return;
    if(event.composition) {
        begin_edit();if(m_editing) m_edit.set_composition(event.text,event.start,event.length);
        request_dialog_presentation_refresh();
    } else dialog_presentation_text(event.text,false);
}
void SkinGridPresentation::dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers)
{
    if(m_resize_control!=0 || m_selecting || (modifiers & ::cw::dialog::DialogPresentationModifierControl)) return;
    if(!finish_edit(true)) return;
    // wx.Grid と同様、編集を確定してから下へ移動し、Shift 付きなら下へ選択を広げる。
    dialog_presentation_key(::cw::dialog::DialogPresentationKey::Down,true,modifiers);
}
void SkinGridPresentation::dialog_presentation_key(::cw::dialog::DialogPresentationKey key,bool pressed,
    ::cw::dialog::DialogPresentationModifiers modifiers)
{
    using namespace ::cw::dialog;
    using Key=DialogPresentationKey;
    if(!pressed || !row_count || !row_count() || columns.empty()) return;
    m_modifiers=modifiers;prune_selection();
    if(key==Key::Escape && m_selecting) {
        if(m_selection_moved) {
            m_selected=m_drag_selection;m_extent_row=m_drag_extent_row;m_extent_column=m_drag_extent_column;
        }
        dialog_presentation_pointer_cancel();m_suppress_drag_until_release=true;return;
    }
    if(m_resize_control!=0 || m_selecting) return;
    if(m_editing && !m_edit.composition.empty()) return;
    if(key==Key::Escape) {
        if(m_editing) finish_edit(false);else clear_selection();return;
    }
    if(key==Key::Enter) {dialog_presentation_enter(modifiers);return;}
    if(key==Key::F2) {begin_edit();return;}
    if(modifiers & DialogPresentationModifierAlt) return;
    if(m_editing && textedit::single_line_key(m_edit,key,modifiers,0,
       presentation_clipboard_text,presentation_set_clipboard_text)) {
        request_dialog_presentation_refresh();return;
    }
    const bool ctrl=modifiers & DialogPresentationModifierControl,shift=modifiers & DialogPresentationModifierShift;
    if(!m_editing && ctrl) {
        if((key==Key::C || key==Key::Insert) && !shift) {
            if(const auto text=selection_text()) {if(clipboard_write) clipboard_write(*text);m_validation_error.clear();}
            else m_validation_error="離れた複数の範囲はコピーできません。";
            request_dialog_presentation_refresh();return;
        }
        if(key==Key::V || key==Key::X) {
            begin_edit();textedit::single_line_key(m_edit,key,modifiers,0,
                presentation_clipboard_text,presentation_set_clipboard_text);
            request_dialog_presentation_refresh();return;
        }
    }
    if(!m_editing && key==Key::Space && (ctrl || shift)) {
        if(ctrl && shift) select_all();
        else extend_selection(m_extent_row,m_extent_column,ctrl?SelectionKind::Columns:SelectionKind::Rows);
        return;
    }
    if(key==Key::Delete || key==Key::Backspace) {
        begin_edit();
        // wx.Grid と同様、Delete/Backspace で文字編集を始めた
        // 場合は先頭・末尾の文字を消す。 表の範囲選択を一括削除の指示とは扱わない。
        m_edit.move(key==Key::Delete?0:m_edit.text.size(),false);
        m_edit.move(key==Key::Delete?textedit::next(m_edit.text,0):
            textedit::previous(m_edit.text,m_edit.text.size()),true);
        m_edit.insert("",0,false);request_dialog_presentation_refresh();return;
    }
    const bool navigation=key==Key::Tab || key==Key::Left || key==Key::Right || key==Key::Up ||
        key==Key::Down || key==Key::Home || key==Key::End || key==Key::PageUp || key==Key::PageDown;
    if(!navigation || (ctrl && (key==Key::PageUp || key==Key::PageDown)) || !finish_edit(true)) return;
    int row=shift && key!=Key::Tab?m_extent_row:m_row;
    int col=shift && key!=Key::Tab?m_extent_column:m_column;
    const int count=row_count(),cells=static_cast<int>(columns.size());
    if(key==Key::Tab) {
        const int next=std::clamp(m_row*cells+m_column+(shift?-1:1),0,count*cells-1);
        row=next/cells;col=next%cells;
    } else if(key==Key::Left || key==Key::Right || key==Key::Up || key==Key::Down) {
        const bool vertical=key==Key::Up || key==Key::Down;
        const int delta=key==Key::Left || key==Key::Up?-1:1;
        int& pos=vertical?row:col;const int maximum=(vertical?count:cells)-1;
        const auto empty=[&](int p) {
            return !read_cell || read_cell(vertical?p:m_row,vertical?m_column:p).empty();
        };
        if(ctrl && pos+delta>=0 && pos+delta<=maximum) {
            const bool contiguous=!empty(pos) && !empty(pos+delta);
            pos+=delta;
            while(pos+delta>=0 && pos+delta<=maximum) {
                if(contiguous) {if(empty(pos+delta)) break;}
                else if(!empty(pos)) break;
                pos+=delta;
            }
        } else pos+=delta;
    } else if(key==Key::PageUp) row=row_at(row_top(row)-grid_view_height);
    else if(key==Key::PageDown) row=row_at(row_top(row)+grid_view_height);
    else if(key==Key::Home) {col=0;if(ctrl) row=0;}
    else if(key==Key::End) {col=cells-1;if(ctrl) row=count-1;}
    row=std::clamp(row,0,count-1);col=std::clamp(col,0,cells-1);
    if(shift && key!=Key::Tab) extend_selection(row,col,SelectionKind::Cells);
    else select_target(row,col,SelectionKind::Cells,0);
}
void SkinGridPresentation::dialog_presentation_pointer(::cw::dialog::DialogPresentationAction action,int index,
    float x,float y,bool pressed)
{
    const auto scale=std::max<double>(::cw::g_UP_WIN,1.0);x/=static_cast<float>(scale);y/=static_cast<float>(scale);
    if(!std::isfinite(x) || !std::isfinite(y)) return;
    if(action==::cw::dialog::DialogPresentationAction::Auxiliary && is_resize_control(index)) {
        const bool row=index<=row_resize_base;
        const bool label=index==row_label_resize;
        const int item=row?row_resize_base-index:column_resize_base-index;
        const float coordinate=row?y:x;
        if(m_resize_control==0) {
            if(!pressed || !finish_edit(true)) return;
            m_resize_control=index;m_resize_origin=coordinate;
            m_resize_size=row?row_height(item):label?row_label_width():std::max(24,columns[item].width);
        }
        if(m_resize_control!=index) return;
        const auto position=vertical_position();
        const int size=static_cast<int>(std::llround(std::clamp(
            static_cast<double>(m_resize_size)+coordinate-m_resize_origin,
            row?20.0:label?32.0:24.0,label?static_cast<double>(grid_right-24):4096.0)));
        if(row) {
            if(item>=static_cast<int>(m_row_heights.size())) m_row_heights.resize(item+1,grid_row_height);
            m_row_heights[item]=size;
        } else if(label) label_width=size;
        else columns[item].width=size;
        set_vertical_position(position);
        m_horizontal=std::clamp(m_horizontal,0,std::max(0,total_width()-(grid_right-row_label_width())));
        if(!pressed) m_resize_control=0;
        request_dialog_presentation_refresh();return;
    }
    if(index==grid_vscroll || index==grid_hscroll) {
        if(!finish_edit(true)) return;
        if(index==grid_vscroll) {
            const auto total=row_top(row_count?row_count():0);
            const int thumb=static_cast<int>(std::max<Integer>(20,grid_view_height*grid_view_height/std::max<Integer>(1,total)));
            const double fraction=std::clamp((y-grid_header-thumb/2.0)/std::max(1,grid_view_height-thumb),0.0,1.0);
            set_vertical_position(static_cast<Integer>(std::llround(fraction*std::max<Integer>(0,total-grid_view_height))));
        } else {
            const int left=row_label_width(),visible=grid_right-left,total=total_width();
            const int thumb=std::max(20,visible*visible/std::max(1,total));
            m_horizontal=static_cast<int>(std::llround((x-left-thumb/2.0)/std::max(1,visible-thumb)*std::max(0,total-visible)));
            m_horizontal=std::clamp(m_horizontal,0,std::max(0,total-visible));
        }
        request_dialog_presentation_refresh();return;
    }
    if(m_suppress_drag_until_release) {
        if(!pressed) m_suppress_drag_until_release=false;
        return;
    }
    if(index==select_all_control) {
        if(pressed && !m_selecting && finish_edit(true)) select_all();
        m_selecting=pressed;request_dialog_presentation_refresh();return;
    }
    if(m_editing && !m_edit.composition.empty()) return;
    int row{},column{};SelectionKind kind;
    if(!decode_selection_control(index,row,column,kind)) return;
    const bool same=kind==SelectionKind::Cells && row==m_row && column==m_column;
    if(m_editing && same) {
        const auto rect=cell_rect(m_row,m_column);const auto font=grid_font();
        const auto target=x-rect.x-3+m_edit.horizontal;std::size_t pos=0;
        while(pos<m_edit.text.size()) {
            const auto next=::cw::dialog::textedit::next(m_edit.text,pos);
            const auto a=::cw::lib::ui::label_width(m_edit.text.substr(0,pos),font.pixels,font.bold,font.face,font.italic);
            const auto b=::cw::lib::ui::label_width(m_edit.text.substr(0,next),font.pixels,font.bold,font.face,font.italic);
            if(target<(a+b)/2) break;pos=next;
        }
        m_edit.move(pos,m_dragging || (m_modifiers & ::cw::dialog::DialogPresentationModifierShift));
        m_dragging=pressed;request_dialog_presentation_refresh();return;
    }
    if(!m_selecting) {
        if(!pressed) return;
        select_target(row,column,kind,m_modifiers);
        if(m_editing) return; // 拒否された数値編集、または未完了の IME 変換。
        m_selecting=true;m_selection_moved=false;m_select_x=x;m_select_y=y;m_selection_kind=kind;
        m_slow_edit=same && m_modifiers==0 && kind==SelectionKind::Cells;
        m_drag_selection=m_selected;
        m_drag_extent_row=m_extent_row;m_drag_extent_column=m_extent_column;
    } else if(std::abs(x-m_select_x)>3 || std::abs(y-m_select_y)>3) {
        m_selection_moved=true;m_slow_edit=false;
        if(!(m_modifiers & ::cw::dialog::DialogPresentationModifierControl) || cell_selected(m_row,m_column)) {
            const auto [target_row,target_col]=point_cell(x,y);
            extend_selection(target_row,target_col,m_selection_kind);
        }
    }
    if(!pressed) {
        m_selecting=false;
        if(m_slow_edit && !m_selection_moved) begin_edit();
        m_slow_edit=false;
    }
    request_dialog_presentation_refresh();
}
void SkinGridPresentation::dialog_presentation_pointer_cancel()
{
    m_dragging=false;m_selecting=false;m_slow_edit=false;m_selection_moved=false;
    m_suppress_drag_until_release=false;m_resize_control=0;
    request_dialog_presentation_refresh();
}

void SkinGridPresentation::dialog_presentation_scroll(float x,float y,int lines)
{
    if(m_resize_control!=0) return;
    const auto scale=std::max<double>(::cw::g_UP_WIN,1.0);x/=static_cast<float>(scale);y/=static_cast<float>(scale);
    if(x<0 || x>=grid_width || y<0 || y>=grid_height || !finish_edit(true)) return;
    if(m_modifiers & ::cw::dialog::DialogPresentationModifierShift)
        m_horizontal=std::clamp(m_horizontal-lines*24,0,std::max(0,total_width()-(grid_right-row_label_width())));
    else set_vertical_position(vertical_position()-static_cast<Integer>(lines)*grid_row_height);
    request_dialog_presentation_refresh();
}

void SkinSettingsPresentation::bind_grid_panels(std::shared_ptr<SkinFeaturePanel> feature,
    std::shared_ptr<SkinSoundPanel> sound,std::shared_ptr<SkinMessagePanel> message,std::shared_ptr<SkinCardPanel> card)
{
    for(auto& grid:m_grid_panels) grid=std::make_shared<SkinGridPresentation>();
    auto grid=m_grid_panels[0];
    for(int col=0;col<12;++col) grid->columns.push_back({feature->m_column_labels[col],col==0?80:40,
        col==0?SkinGridPresentation::CellKind::Text:col<=6?SkinGridPresentation::CellKind::Integer:SkinGridPresentation::CellKind::Decimal});
    grid->row_count=[feature]{return static_cast<int>(feature->m_rows.size());};
    grid->row_label=[feature](int r){return r<static_cast<int>(feature->m_row_labels.size())?feature->m_row_labels[r]:String{};};
    grid->read_cell=[feature](int r,int c){return feature->get_cell(r,c);};
    grid->write_cell=[feature](int r,int c,String value){feature->set_cell(r,c,std::move(value));};
    grid=m_grid_panels[1];grid->columns={{"ファイル名(拡張子を除く)",170}};
    grid->row_count=[sound]{return static_cast<int>(sound->m_values.size());};
    grid->row_label=[sound](int r){return r<static_cast<int>(sound->m_row_labels.size())?sound->m_row_labels[r]:String{};};
    grid->read_cell=[sound](int r,int){return sound->get_cell(r);};
    grid->write_cell=[sound](int r,int,String value){sound->set_cell(r,std::move(value));};
    grid=m_grid_panels[2];grid->columns={{"メッセージ(\\n=改行, \\\\=\\)",380}};grid->label_width=150;
    grid->row_count=[message]{return static_cast<int>(message->m_values.size());};
    grid->row_label=[message](int r){return r<static_cast<int>(message->m_row_labels.size())?message->m_row_labels[r]:String{};};
    grid->read_cell=[message](int r,int){return message->get_cell(r);};
    grid->write_cell=[message](int r,int,String value){message->set_cell(r,std::move(value));};
    grid=m_grid_panels[3];grid->columns={{"名称",80},{"解説(\\n=改行, \\\\=\\)",300}};
    grid->row_count=[card]{return static_cast<int>(card->m_rows.size());};
    grid->row_label=[card](int r){return r<static_cast<int>(card->m_row_labels.size())?card->m_row_labels[r]:String{};};
    grid->read_cell=[card](int r,int c){return card->get_cell(r,c);};
    grid->write_cell=[card](int r,int c,String value){card->set_cell(r,c,std::move(value));};
}
void SkinSettingsPresentation::use_grid_panel(int tab)
{
    if(tab==m_grid_tab) return;
    if(!finish_grid_edit(true)) return;
    m_grid_tab=tab>=1 && tab<=4?tab:0;
}
std::shared_ptr<SkinGridPresentation> SkinSettingsPresentation::active_grid_panel() const
{
    return !dialog_presentation_finished()?grid_presenter(m_grid_tab):nullptr;
}
bool SkinSettingsPresentation::finish_grid_edit(bool accept)
{
    if(auto grid=active_grid_panel()) return grid->finish_edit(accept);
    return true;
}
bool SkinSettingsPresentation::grid_panel_action(::cw::dialog::DialogPresentationAction action,int index)
{
    using Action=::cw::dialog::DialogPresentationAction;
    auto grid=active_grid_panel();if(!grid) return false;
    // 共通の子表示内では、スクロールバー・列見出しの ID は負数とする。
    if(index>=grid_control_base-3 || (grid->is_resize_control(index-grid_control_base) || grid->is_selection_control(index-grid_control_base))) {
        grid->dialog_presentation_action(action,index-grid_control_base);return true;
    }
    if(action==Action::Cancel && index<0 && (grid->m_editing || grid->has_selection() || grid->selection_dragging())) {
        grid->dialog_presentation_action(action,index);return true;
    }
    if(!grid->finish_edit(action!=Action::Cancel)) return true;
    return false;
}

void SkinSettingsPresentation::bind_local_settings(
    std::shared_ptr<::cw::dialog::settings::DrawingSettingPanel> drawing,
    std::shared_ptr<::cw::dialog::settings::FontSettingPanel> fonts)
{
    finish_local_settings(false);
    m_local_active=false;
    m_local_settings=std::make_shared<::cw::dialog::settings::SettingsDialog>();
    m_local_settings->configure_local_panels(std::move(drawing),std::move(fonts));
}

void SkinSettingsPresentation::use_local_settings(int tab)
{
    if(!m_local_settings) return;
    if(tab<0) {
        if(m_local_active) m_local_settings->finish_panel_edit(true);
        m_local_active=false;
    } else {
        m_local_settings->select_local_panel(tab==1);
        m_local_active=true;
    }
}

std::shared_ptr<::cw::dialog::settings::SettingsDialog> SkinSettingsPresentation::active_local_settings() const
{
    return m_local_active && !dialog_presentation_finished()?m_local_settings:nullptr;
}

void SkinSettingsPresentation::finish_local_settings(bool accept)
{
    finish_grid_edit(accept);
    if(m_path_panel) {m_path_panel->finish_edit(false);m_path_panel->cancel_document_request();}
    if(m_info_panel) m_info_panel->finish_edit();
    if(!m_local_settings) return;
    m_local_settings->finish_panel_edit(accept);
    m_local_settings->cancel_document_request();
}

bool SkinSettingsPresentation::local_settings_action(::cw::dialog::DialogPresentationAction action,int index)
{
    using Action=::cw::dialog::DialogPresentationAction;
    if(!m_local_active || !m_local_settings) return false;
    if(m_local_settings->document_request_pending()) {
        if(action!=Action::Cancel) return true;
        finish_local_settings(false); return false;
    }
    if(m_local_settings->m_settings_popup) {
        m_local_settings->dialog_presentation_action(action,index>=local_control_base?index-local_control_base:-1);
        return true;
    }
    if(index>=local_control_base) {
        m_local_settings->dialog_presentation_action(action,index-local_control_base);
        return true;
    }
    if(action==Action::Cancel && index<0 && m_local_settings->m_scenario_edit>=0) {
        m_local_settings->finish_panel_edit(false); return true;
    }
    // 下部ボタンのクリックでは保存前にセル編集を確定するが、キャンセルでは破棄する。
    m_local_settings->finish_panel_edit(action!=Action::Cancel);
    return false;
}

void SkinSettingsPresentation::use_path_panel(std::shared_ptr<SkinBasePanel> panel,bool active)
{
    if(m_path_active && (!active || m_path_panel!=panel)) m_path_panel->finish_edit(true);
    m_path_panel=std::move(panel);m_path_active=active && m_path_panel!=nullptr;
}
std::shared_ptr<SkinBasePanel> SkinSettingsPresentation::active_path_panel() const
{
    return m_path_active && !dialog_presentation_finished()?m_path_panel:nullptr;
}
void SkinSettingsPresentation::use_info_panel(std::shared_ptr<SkinInfoPanel> panel,bool active,int y,int height)
{
    if(m_info_active && (!active || m_info_panel!=panel)) m_info_panel->finish_edit();
    m_info_panel=std::move(panel);m_info_active=active && m_info_panel!=nullptr;m_info_y=y;
    if(m_info_panel) m_info_panel->m_presentation_height=height;
}
std::shared_ptr<SkinInfoPanel> SkinSettingsPresentation::active_info_panel() const
{
    return m_info_active && !dialog_presentation_finished()?m_info_panel:nullptr;
}
bool SkinSettingsPresentation::info_panel_action(::cw::dialog::DialogPresentationAction action,int index)
{
    if(auto info=active_info_panel();info && info->m_type_popup && index>=path_control_base && index<info_control_base) {
        info->dialog_presentation_action(::cw::dialog::DialogPresentationAction::Auxiliary,-1);return true;
    }
    if(auto paths=active_path_panel()) {
        if(paths->document_request_pending()) {
            if(action==::cw::dialog::DialogPresentationAction::Cancel) {paths->cancel_document_request();return false;}
            return true;
        }
        if(action==::cw::dialog::DialogPresentationAction::Cancel && index<0 && paths->has_composition()) {
            paths->clear_composition();return true;
        }
        if(index>=path_control_base && index<info_control_base) {
            if(auto info=active_info_panel()) info->finish_edit();
            paths->dialog_presentation_action(action,index-path_control_base);return true;
        }
        paths->finish_edit(action!=::cw::dialog::DialogPresentationAction::Cancel);
    }
    auto panel=active_info_panel();if(!panel) return false;
    if(action==::cw::dialog::DialogPresentationAction::Cancel && index<0 && panel->has_composition()) {
        panel->clear_composition();return true;
    }
    if(panel->m_type_popup || (index>=info_control_base && index<local_control_base)) {
        panel->dialog_presentation_action(action,index>=info_control_base?index-info_control_base:-1);
        return true;
    }
    panel->finish_edit();return false;
}
void SkinSettingsPresentation::dialog_presentation_modifiers(::cw::dialog::DialogPresentationModifiers modifiers)
{
    if(dialog_presentation_finished() || settings_input_blocked()) return;
    if(auto grid=active_grid_panel()) grid->dialog_presentation_modifiers(modifiers);
    if(auto paths=active_path_panel()) paths->dialog_presentation_modifiers(modifiers);
    if(auto info=active_info_panel()) info->dialog_presentation_modifiers(modifiers);
    if(m_local_settings) m_local_settings->dialog_presentation_modifiers(modifiers);
}
void SkinSettingsPresentation::dialog_presentation_text(const std::string& text,bool composition)
{
    if(dialog_presentation_finished() || settings_input_blocked()) return;
    if(auto grid=active_grid_panel()) {grid->dialog_presentation_text(text,composition);return;}
    if(auto paths=active_path_panel()) {
        if(paths->document_request_pending()) return;
        if(paths->has_focus()) {paths->dialog_presentation_text(text,composition);return;}
    }
    if(auto info=active_info_panel()) info->dialog_presentation_text(text,composition);
    if(auto panel=active_local_settings()) panel->dialog_presentation_text(text,composition);
}
void SkinSettingsPresentation::dialog_presentation_text_event(const ::cw::dialog::DialogTextEditingEvent& event)
{
    if(dialog_presentation_finished() || settings_input_blocked()) return;
    if(auto grid=active_grid_panel()) {grid->dialog_presentation_text_event(event);return;}
    if(auto paths=active_path_panel()) {
        if(paths->document_request_pending()) return;
        if(paths->has_focus()) {paths->dialog_presentation_text_event(event);return;}
    }
    if(auto info=active_info_panel()) info->dialog_presentation_text_event(event);
    if(auto panel=active_local_settings()) panel->dialog_presentation_text_event(event);
}
void SkinSettingsPresentation::dialog_presentation_key(::cw::dialog::DialogPresentationKey key,bool pressed,
    ::cw::dialog::DialogPresentationModifiers modifiers)
{
    if(dialog_presentation_finished() || settings_input_blocked()) return;
    if(auto grid=active_grid_panel()) {grid->dialog_presentation_key(key,pressed,modifiers);return;}
    if(auto paths=active_path_panel()) {
        if(paths->document_request_pending()) return;
        if(paths->has_focus()) {paths->dialog_presentation_key(key,pressed,modifiers);return;}
    }
    if(auto info=active_info_panel()) info->dialog_presentation_key(key,pressed,modifiers);
    if(auto panel=active_local_settings()) panel->dialog_presentation_key(key,pressed,modifiers);
}
void SkinSettingsPresentation::dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers)
{
    if(dialog_presentation_finished() || settings_input_blocked()) return;
    if(auto grid=active_grid_panel()) {grid->dialog_presentation_enter(modifiers);return;}
    if(auto paths=active_path_panel()) {
        if(paths->document_request_pending() || paths->has_composition()) return;
        if(paths->has_focus()) {paths->finish_edit(true);return;}
    }
    if(auto info=active_info_panel();info && info->has_composition()) return;
    if(auto info=active_info_panel();info && (info->m_type_popup || info->multiline_focused())) {
        info->dialog_presentation_enter(modifiers);return;
    }
    if(auto panel=active_local_settings()) panel->dialog_presentation_enter(modifiers);
    else dialog_presentation_action(::cw::dialog::DialogPresentationAction::Accept,-1);
}
void SkinSettingsPresentation::dialog_presentation_pointer(::cw::dialog::DialogPresentationAction action,int index,
    float x,float y,bool pressed)
{
    if(dialog_presentation_finished()) return;
    if(settings_input_blocked()) {if(!pressed && action==::cw::dialog::DialogPresentationAction::Cancel) dialog_presentation_action(action,index);return;}
    if(auto grid=active_grid_panel();grid &&
       (index>=grid_control_base-3 || (grid->is_resize_control(index-grid_control_base) || grid->is_selection_control(index-grid_control_base)))) {
        const auto scale=std::max<double>(::cw::g_UP_WIN,1.0);
        grid->dialog_presentation_pointer(action,index-grid_control_base,static_cast<float>(x-20*scale),
            static_cast<float>(y-m_local_y*scale),pressed);return;
    }
    if(auto paths=active_path_panel();paths && paths->document_request_pending()) return;
    if(auto paths=active_path_panel();paths && index>=path_control_base && index<info_control_base) {
        if(auto info=active_info_panel();info && info->m_type_popup) {
            if(pressed) info->dialog_presentation_action(::cw::dialog::DialogPresentationAction::Auxiliary,-1);
            return;
        }
        if(auto info=active_info_panel()) info->finish_edit();
        const auto scale=std::max<double>(::cw::g_UP_WIN,1.0);
        paths->dialog_presentation_pointer(action,index-path_control_base,static_cast<float>(x-20*scale),
                                          static_cast<float>(y-m_local_y*scale),pressed);
    } else if(auto info=active_info_panel();info && index>=info_control_base && index<local_control_base) {
        if(auto paths=active_path_panel()) paths->finish_edit(true);
        const auto scale=std::max<double>(::cw::g_UP_WIN,1.0);
        info->dialog_presentation_pointer(action,index-info_control_base,static_cast<float>(x-20*scale),
                                         static_cast<float>(y-m_info_y*scale),pressed);
    } else if(auto panel=active_local_settings(); panel && index>=local_control_base) {
        const auto scale=std::max<double>(::cw::g_UP_WIN,1.0);
        panel->dialog_presentation_pointer(action,index-local_control_base,
            static_cast<float>(x-20*scale),static_cast<float>(y-(m_local_y-panel->local_panel_top)*scale),pressed);
    } else if(!pressed) dialog_presentation_action(action,index);
}
void SkinSettingsPresentation::dialog_presentation_pointer_cancel()
{
    if(auto grid=active_grid_panel()) grid->dialog_presentation_pointer_cancel();
    if(auto paths=active_path_panel()) paths->dialog_presentation_pointer_cancel();
    if(auto info=active_info_panel()) info->dialog_presentation_pointer_cancel();
    if(auto panel=active_local_settings()) panel->dialog_presentation_pointer_cancel();
}
void SkinSettingsPresentation::dialog_presentation_scroll(float x,float y,int lines)
{
    if(dialog_presentation_finished() || settings_input_blocked()) return;
    if(auto grid=active_grid_panel()) {
        const auto scale=std::max<double>(::cw::g_UP_WIN,1.0);
        grid->dialog_presentation_scroll(static_cast<float>(x-20*scale),static_cast<float>(y-m_local_y*scale),lines);return;
    }
    if(auto paths=active_path_panel();paths && paths->document_request_pending()) return;
    if(auto info=active_info_panel()) {
        const auto scale=std::max<double>(::cw::g_UP_WIN,1.0);
        info->dialog_presentation_scroll(static_cast<float>(x-20*scale),static_cast<float>(y-m_info_y*scale),lines);return;
    }
    if(auto panel=active_local_settings()) {
        const auto scale=std::max<double>(::cw::g_UP_WIN,1.0);
        panel->dialog_presentation_scroll(static_cast<float>(x-20*scale),
            static_cast<float>(y-(m_local_y-panel->local_panel_top)*scale),lines);
    } else ::cw::dialog::DialogPresentationProvider::dialog_presentation_scroll(x,y,lines);
}
void SkinSettingsPresentation::dialog_presentation_hover(float x,float y)
{
    if(dialog_presentation_finished() || settings_input_blocked()) return;
    if(auto paths=active_path_panel()) {
        const auto scale=std::max<double>(::cw::g_UP_WIN,1.0);
        paths->dialog_presentation_hover(static_cast<float>(x-20*scale),static_cast<float>(y-m_local_y*scale));
    }
}
bool SkinSettingsPresentation::dialog_presentation_needs_refresh() const noexcept
{
    return ::cw::dialog::DialogPresentationProvider::dialog_presentation_needs_refresh() ||
        (grid_presenter(m_grid_tab) && grid_presenter(m_grid_tab)->dialog_presentation_needs_refresh()) ||
        (m_path_active && m_path_panel && m_path_panel->dialog_presentation_needs_refresh()) ||
        (m_info_active && m_info_panel && m_info_panel->dialog_presentation_needs_refresh()) ||
        (m_local_active && m_local_settings && m_local_settings->dialog_presentation_needs_refresh());
}
void SkinSettingsPresentation::dialog_presentation_refreshed() noexcept
{
    ::cw::dialog::DialogPresentationProvider::dialog_presentation_refreshed();
    // 直前の合成で実際に読み取った子だけを描画済みにする
    // 。 非表示タブの更新をここで解除すると、そのタブが
    // 非アクティブな間に公開された変更を失ってしまう。
    if(auto grid=active_grid_panel()) grid->dialog_presentation_refreshed();
    if(auto paths=active_path_panel()) paths->dialog_presentation_refreshed();
    if(auto info=active_info_panel()) info->dialog_presentation_refreshed();
    if(auto local=active_local_settings()) local->dialog_presentation_refreshed();
}

SkinConversionDialog::~SkinConversionDialog()
{
    if(m_pane_base) m_pane_base->close_panel();
    if (m_conversion_worker.joinable()) m_conversion_worker.join();
    if (g_active_conversion==this) g_active_conversion=nullptr;
}

void SkinConversionDialog::initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_exe, bool arg_from_settings, std::optional<::cw::lib::Callback> arg_get_localsettings)
{
    dialog_retained_surface().reset();
    if (g_active_conversion && g_active_conversion!=this)
        throw std::runtime_error("スキン変換ダイアログは既に開かれています。");
    if (g_active_edit)
        throw std::runtime_error("スキン編集ダイアログは既に開かれています。");
    m_native_parent_handle=arg_parent?arg_parent->native_handle:0;
    g_active_conversion=this;
    g_active_edit=nullptr;
    m_cwpy_debug=true;
    m_from_settings_value=arg_from_settings;
    m_from_settings=::cw::lib::Value(arg_from_settings);
    m_successful=false; m_select_skin=false; m_skindirname.clear();
    m_closed=false; m_cancelled=false; m_progress_active=false; m_error_message.clear();
    m_presentation_tab=0; m_presentation_page=0;
    m_conversion_worker_done=false;
    {
        std::lock_guard lock(m_conversion_mutex);
        m_conversion_worker_error.clear();
    }
    m_conv=std::make_shared<::cw::skin::convert::Converter>();
    m_conv->initialize(std::move(arg_exe));
    // Python と同じ三つのローカル設定の取得元を使う。 明示的なコールバック、実
    // 行中の CWPy の設定、新規の独立した LocalSetting の順に扱う。
    if (arg_get_localsettings) {
        m_local=callback_local(*arg_get_localsettings);
    } else if (::cw::g_cwpy && ::cw::g_cwpy->m_setting) {
        m_local=::cw::g_cwpy->m_setting->m_local;
    }
    if (!m_local) m_local=new_local();
    const bool use_copybase=static_cast<bool>(arg_get_localsettings) ||
        static_cast<bool>(::cw::g_cwpy && ::cw::g_cwpy->m_setting);

    const auto [skin_count,unknown_version]=::cw::frame::get_skincount();
    if (skin_count==0) {
        m_warning_text=unknown_version
            ? "インストールされているスキンは未知のバージョンです。\n" +
                ::cw::g_APP_NAME + "をアップデートするか、対応バージョンのスキンをインストールするか、スキンの自動生成を行ってください。"
            : "スキンがインストールされていません。\nスキンを入手してインストールするか、自動生成を行なってください。";
    }
    auto panel=std::make_shared<::cw::lib::Panel>();
    m_pane_base=std::make_shared<SkinBasePanel>();
    m_pane_feature=std::make_shared<SkinFeaturePanel>();
    m_pane_sound=std::make_shared<SkinSoundPanel>();
    m_pane_message=std::make_shared<SkinMessagePanel>();
    m_pane_card=std::make_shared<SkinCardPanel>();
    m_pane_base->initialize(panel,m_conv);
    m_pane_feature->initialize(panel,m_conv);
    m_pane_sound->initialize(panel,m_conv);
    m_pane_message->initialize(panel,m_conv);
    m_pane_card->initialize(panel,m_conv);
    bind_grid_panels(m_pane_feature,m_pane_sound,m_pane_message,m_pane_card);
    m_pane_draw=std::make_shared<::cw::dialog::settings::DrawingSettingPanel>();
    m_pane_font=std::make_shared<::cw::dialog::settings::FontSettingPanel>();
    m_pane_draw->initialize(panel,true,arg_get_localsettings,use_copybase);
    m_pane_draw->load(std::nullopt,m_local);
    m_pane_font->initialize(panel,true,arg_get_localsettings,use_copybase);
    m_pane_font->load(std::nullopt,m_local);
    bind_local_settings(m_pane_draw,m_pane_font);
    m_pane_base->m_on_extracted=[converter=std::weak_ptr<::cw::skin::convert::Converter>(m_conv),
        feature=std::weak_ptr<SkinFeaturePanel>(m_pane_feature),sound=std::weak_ptr<SkinSoundPanel>(m_pane_sound),
        message=std::weak_ptr<SkinMessagePanel>(m_pane_message),card=std::weak_ptr<SkinCardPanel>(m_pane_card)] {
        if(auto conv=converter.lock()) {
            if(auto pane=feature.lock()) pane->set_values(conv);
            if(auto pane=sound.lock()) pane->set_values(conv);
            if(auto pane=message.lock()) pane->set_values(conv);
            if(auto pane=card.lock()) pane->set_values(conv);
        }
    };
    use_path_panel(m_pane_base,true);
    m_ok_enabled=m_pane_base->m_ok_enabled;
    _do_layout();
    _bind();
}

void SkinConversionDialog::_bind()
{
}

void SkinConversionDialog::OnOk(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m_closed || m_progress_active || !finish_grid_edit(true)) return;
    if (!m_conv || !m_pane_base || !m_pane_base->m_info) return;
    if(m_pane_base->document_request_pending()) return;
    if(auto panel=local_settings_presenter(); panel && panel->document_request_pending()) return;
    finish_local_settings(true);
    try {m_pane_base->_selected_exe(m_pane_base->m_exe_value);}
    catch(const std::exception& error) {m_error_message=error.what();skin_common_message(m_error_message);return;}
    if(m_closed || m_pane_base->dialog_presentation_finished()) return;
    m_pane_base->OnInput({});
    if (!m_pane_base->m_ok_enabled) {
        m_error_message="本体、データフォルダ、種別、名前を入力してください。";
        skin_common_message(m_error_message);
        return;
    }
    m_pane_feature->get_values(m_conv);
    m_pane_sound->get_values(m_conv);
    m_pane_message->get_values(m_conv);
    m_pane_card->get_values(m_conv);
    m_conv->m_exe=m_pane_base->m_exe_value;
    m_conv->m_datadir=m_pane_base->m_datadir_value;
    m_conv->m_scenariodir=m_pane_base->m_scenariodir_value;
    m_conv->m_yadodir=m_pane_base->m_yadodir_value;
    if (!m_conv->m_data) {
        m_error_message="変換元データがありません。";
        skin_common_message(m_error_message);
        return;
    }
    const auto& info=*m_pane_base->m_info;
    set_property(m_conv->m_data,"Property/Name",info.m_name);
    set_property(m_conv->m_data,"Property/Type",info.m_type);
    set_property(m_conv->m_data,"Property/Author",info.m_author);
    set_property(m_conv->m_data,"Property/Description",info.m_description);
    set_property(m_conv->m_data,"Property/CW120VocationLevel",info.m_vocation120_value?"True":"False");
    set_property(m_conv->m_data,"Property/InitialCash",std::to_string(info.m_initial_cash));
    auto settings=m_conv->m_data->find("Settings");
    Element settings_element=settings && *settings ? *settings : ::cw::data::make_element("Settings");
    if (!settings || !*settings) m_conv->m_data->getroot()->append(settings_element); else settings_element->clear();
    m_local=new_local();
    m_pane_draw->apply_localsettings(m_local);
    m_pane_font->apply_localsettings(m_local);
    ::cw::xmlcreater::create_localsettings(settings_element,m_local);
    m_conv->m_failure=false;
    m_conv->m_complete=false;
    m_conv->m_errormessage.clear();
    m_conv->m_curnum=0;
    m_progress_active=true;
    m_conversion_worker_done=false;
    {
        std::lock_guard lock(m_conversion_mutex);
        m_conversion_worker_error.clear();
    }
    auto progress=std::make_shared<::cw::dialog::progress::SysProgressDialog>();
    progress->initialize({},"スキンの変換 ["+m_conv->m_exe+"]","スキンの変換を開始しています。",
                         std::max<Integer>(1,m_conv->m_maximum),0,false);
    bool started=false,shown=false;
    try {
        // 宿変換と同じモーダルワーカーの生存期間を使い、ホストがある場合だけ開
        // 始する。 どの終了経路でも結果を読む前にワーカーの終了を待つ。進捗
        // 文字列は同期付きのモデル状態へ送り、ワーカーから直接読み続けない。
        shown=::cw::dialog::progress::run_modal_task(progress,
            [converter=m_conv,&started](const auto& update) {
                started=true;
                converter->run(update);
            });
    } catch(const std::exception& error) {
        m_conversion_worker_error=error.what();
    } catch(...) {
        m_conversion_worker_error="スキンの自動生成に失敗しました。";
    }
    if(!started) {
        m_progress_active=false;
        m_error_message=m_conversion_worker_error.empty()
            ?"進捗ダイアログを表示できないため、スキンの変換を開始しませんでした。"
            :m_conversion_worker_error;
        request_dialog_presentation_refresh();
        return;
    }
    m_conversion_worker_done=true;
    if(!shown && m_conversion_worker_error.empty()) {
        m_progress_active=false;
        m_error_message="進捗画面が閉じられたため、スキンの登録・切り替えを中止しました。";
        request_dialog_presentation_refresh();
        return;
    }
    complete_conversion();
}

void SkinConversionDialog::complete_conversion()
{
    if (!m_progress_active || !m_conversion_worker_done || !m_conv) return;
    if (m_conversion_worker.joinable()) m_conversion_worker.join();
    m_progress_active=false;
    String worker_error;
    {
        std::lock_guard lock(m_conversion_mutex);
        worker_error=m_conversion_worker_error;
    }
    if (!worker_error.empty()) {
        m_error_message=worker_error;
        skin_common_message(m_error_message);
        return;
    }
    // Python と同様、変換後に生成された Scenario/Yado
    // の対象を登録する。 独立した二つの try/except
    // に合わせ、登録失敗は成功したスキン変換から意図的に切り離す。
    try {
        register_generated_scenario(*m_conv);
    } catch (const std::exception& ex) {
        m_status_message=ex.what();
    }
    try {
        register_generated_yado(*m_conv);
    } catch (const std::exception& ex) {
        m_status_message=ex.what();
    }
    if (m_conv->m_failure) {
        m_error_message=m_conv->m_errormessage;
        skin_common_message(m_error_message);
        return;
    }
    if (!m_conv->m_scenariodir.empty() && m_scenario_registration) m_scenario_registration(m_conv->m_scenariodir);
    if (!m_conv->m_yadodir.empty() && m_yado_registration) m_yado_registration(m_conv->m_yadodir);
    m_successful=true;
    const auto [skin_count,unused]=::cw::frame::get_skincount(); (void)unused;
    if (m_from_settings_value || skin_count<=1) {
        m_select_skin=true;
        m_skindirname=m_conv->m_skindirname;
    } else {
        const String message="スキンの自動生成に成功しました。生成したスキンに切り替えますか？";
        m_select_skin=skin_common_confirm(message);
        if (m_select_skin) m_skindirname=m_conv->m_skindirname;
    }
    m_conv->dispose();
    if(m_pane_base) m_pane_base->close_panel();
    m_closed=true;
    if (m_close_callback) m_close_callback();
}

void SkinConversionDialog::OnCancel(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m_progress_active) return;
    finish_local_settings(false);
    if(m_pane_base) m_pane_base->close_panel();
    if (m_conv) m_conv->dispose();
    m_cancelled=true; m_closed=true; m_progress_active=false;
    if (m_close_callback) m_close_callback();
}

SkinEditDialog::~SkinEditDialog()
{
    cancel_import_request();
    if (g_active_edit==this) g_active_edit=nullptr;
}

void SkinConversionDialog::set_scenario_registration(std::function<void(String)> arg_callback)
{
    m_scenario_registration=std::move(arg_callback);
}

void SkinConversionDialog::set_yado_registration(std::function<void(String)> arg_callback)
{
    m_yado_registration=std::move(arg_callback);
}

void SkinConversionDialog::_do_layout()
{
    // UI の実装は共通の一つに保つ。旧 Win32
    // ウィンドウは共通 Presentation
    // との一致を検証するまで比較用に残すが、
    // 実行時の配置は常にこのモデルが供給する。
}

::cw::dialog::DialogPresentation SkinConversionDialog::dialog_presentation()
{
    static const std::vector<String> tabs{
        "基本","特性","サウンド","メッセージ","カード","描画","フォント"};
    use_path_panel(m_pane_base,m_presentation_tab==0);
    use_info_panel(m_pane_base?m_pane_base->m_info:nullptr,m_presentation_tab==0,(m_warning_text.empty()?44:72)+144,336);
    if(m_pane_base) {m_pane_base->OnInput({});m_ok_enabled=m_pane_base->m_ok_enabled;}
    use_local_settings(m_presentation_tab>=5?static_cast<int>(m_presentation_tab-5):-1);
    place_local_settings(m_warning_text.empty()?44:72);
    use_grid_panel(static_cast<int>(m_presentation_tab));
    return render_skin_presentation("SKIN_CONVERSION","スキンの自動生成",m_warning_text,
        tabs,m_presentation_tab,m_ok_enabled && !m_progress_active,false,dialog_retained_surface(),
        active_local_settings(),active_info_panel(),active_path_panel(),active_grid_panel());
}

void SkinConversionDialog::dialog_presentation_action(
    const ::cw::dialog::DialogPresentationAction action, const int index)
{
    using Action=::cw::dialog::DialogPresentationAction;
    if (m_closed || m_progress_active) return;
    use_grid_panel(static_cast<int>(m_presentation_tab));
    if(grid_panel_action(action,index)) return;
    use_path_panel(m_pane_base,m_presentation_tab==0);
    use_info_panel(m_pane_base?m_pane_base->m_info:nullptr,m_presentation_tab==0,(m_warning_text.empty()?44:72)+144,336);
    if(info_panel_action(action,index)) return;
    use_local_settings(m_presentation_tab>=5?static_cast<int>(m_presentation_tab-5):-1);
    if(local_settings_action(action,index)) return;
    if (action==Action::Accept) { OnOk({}); return; }
    if (action==Action::Cancel) { OnCancel({}); return; }
    if (action==Action::Previous) { m_presentation_page=std::max<Integer>(0,m_presentation_page-1); return; }
    if (action==Action::Next) { ++m_presentation_page; return; }
    if (action!=Action::Auxiliary) return;
    if (index>=1000 && index<1007) {
        m_presentation_tab=index-1000; m_presentation_page=0;
        use_grid_panel(static_cast<int>(m_presentation_tab));
        use_path_panel(m_pane_base,m_presentation_tab==0);
        use_info_panel(m_pane_base?m_pane_base->m_info:nullptr,m_presentation_tab==0,(m_warning_text.empty()?44:72)+144,336);
        use_local_settings(m_presentation_tab>=5?static_cast<int>(m_presentation_tab-5):-1); return;
    }
}

bool SkinConversionDialog::dialog_presentation_finished() const noexcept { return m_closed; }
void SkinConversionDialog::dialog_presentation_complete() {}

void SkinEditDialog::initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_skindirname, std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, bool, ::cw::lib::Integer> arg_skinsummary, ::cw::lib::Callback arg_get_localsettings)
{
    dialog_retained_surface().reset();
    if (g_active_edit && g_active_edit!=this)
        throw std::runtime_error("スキン編集ダイアログは既に開かれています。");
    if (g_active_conversion)
        throw std::runtime_error("スキン変換ダイアログは既に開かれています。");
    m_native_parent_handle=arg_parent?arg_parent->native_handle:0;
    g_active_edit=this;
    g_active_conversion=nullptr;
    m_cwpy_debug=true;
    m_skindirname_value=std::move(arg_skindirname);
    m_skinsummary_typed=arg_skinsummary;
    m_warning_text="ここでの編集結果は、設定ダイアログでのOK・キャンセルの選択に関わらず即時に反映されます。";
    cancel_import_request();
    m_closed=false; m_cancelled=false; m_saved=false; m_imported_resources=0; m_status_message.clear();m_import_error.clear();
    m_presentation_tab=0; m_presentation_page=0;
    const bool current_skin=::cw::g_cwpy && ::cw::g_cwpy->m_setting &&
        ::cw::g_cwpy->m_setting->m_skindirname==m_skindirname_value;
    if (current_skin) {
        m_local=::cw::g_cwpy->m_setting->m_skin_local;
    } else {
        m_local=callback_local(arg_get_localsettings);
    }
    if (!m_local) m_local=new_local();
    const auto tree=::cw::data::xml2etree(skin_xml_path(m_skindirname_value),"",std::nullopt,std::nullopt,true);
    if (tree) {
        const auto settings=tree->find("Settings");
        if (!current_skin && settings && *settings) {
            m_local=new_local();
            m_local->load(::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(*settings)));
        }
    }
    m_info=std::make_shared<SkinInfoPanel>();
    m_info->initialize(std::make_shared<::cw::lib::Panel>());
    m_info->set_values(std::get<0>(m_skinsummary_typed),std::get<1>(m_skinsummary_typed),
                       std::get<2>(m_skinsummary_typed),std::get<3>(m_skinsummary_typed),
                       std::get<4>(m_skinsummary_typed),std::get<5>(m_skinsummary_typed));
    m_pane_draw=std::make_shared<::cw::dialog::settings::DrawingSettingPanel>();
    m_pane_font=std::make_shared<::cw::dialog::settings::FontSettingPanel>();
    auto panel=std::make_shared<::cw::lib::Panel>();
    // Python と同様、同じ設定パネルを埋め込み、両方へ親の現在の編集値を取得するコールバッ
    // クを渡す。 これにより「基本設定をコピー」に、設定画面でまだ適用していない変更も含める。
    m_pane_draw->initialize(panel,true,arg_get_localsettings,true); m_pane_draw->load(std::nullopt,m_local);
    m_pane_font->initialize(panel,true,arg_get_localsettings,true);
    const auto setting=::cw::g_cwpy && ::cw::g_cwpy->m_setting
        ? std::optional<std::shared_ptr<::cw::setting::Setting>>{::cw::g_cwpy->m_setting}
        : std::nullopt;
    m_pane_font->load(setting,m_local);
    bind_local_settings(m_pane_draw,m_pane_font);
    _do_layout();
    _bind();
}

void SkinEditDialog::_bind()
{
}

void SkinEditDialog::OnOk(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m_closed || import_request_pending() || !m_info) return;
    if(auto panel=local_settings_presenter(); panel && panel->document_request_pending()) return;
    finish_local_settings(true);
    m_info->validate();
    m_skinsummary_typed={m_info->m_type,m_info->m_name,m_info->m_author,m_info->m_description,
                         m_info->m_vocation120_value,m_info->m_initial_cash};
    auto tree=::cw::data::xml2etree(skin_xml_path(m_skindirname_value),"",std::nullopt,std::nullopt,true);
    if (!tree) {
        m_status_message="Skin.xmlを読み込めません。";
        skin_common_message(m_status_message);
        return;
    }
    set_property(tree,"Property/Type",m_info->m_type);
    set_property(tree,"Property/Name",m_info->m_name);
    set_property(tree,"Property/Author",m_info->m_author);
    set_property(tree,"Property/Description",m_info->m_description);
    // Python版と同じく、旧スキンで省略可能だった2項目だけは編集時に補完する。
    // 必須項目まで暗黙生成すると破損Skin.xmlを正常扱いしてしまうため対象を広げない。
    set_optional_skin_property(tree,"Property/CW120VocationLevel","CW120VocationLevel",
                               m_info->m_vocation120_value?"True":"False");
    set_optional_skin_property(tree,"Property/InitialCash","InitialCash",
                               std::to_string(m_info->m_initial_cash));
    const auto settings=tree->find("Settings");
    Element settings_element=settings && *settings ? *settings : ::cw::data::make_element("Settings");
    if (!settings || !*settings) tree->getroot()->append(settings_element); else settings_element->clear();
    m_local=new_local();
    m_pane_draw->apply_localsettings(m_local);
    m_pane_font->apply_localsettings(m_local);
    ::cw::xmlcreater::create_localsettings(settings_element,m_local);
    // 使用中の Skin.xml を直接切り詰めない。書き込
    // み失敗や途中のプロセス終了でも以前のスキンを利用できる
    // よう、 検証済みの完全な XML を不可分に公開する。
    write_tree_transactionally(tree,skin_xml_path(m_skindirname_value));
    m_saved=true;
    const bool current_skin=::cw::g_cwpy && ::cw::g_cwpy->m_setting &&
        ::cw::g_cwpy->m_setting->m_skindirname==m_skindirname_value;
    if (current_skin) {
        const auto local=m_local;
        const auto skinname=m_info->m_name;
        const auto skintype=m_info->m_type;
        const auto vocation120=m_info->m_vocation120_value;
        const auto initialcash=m_info->m_initial_cash;
        const ::cw::lib::Callback update_runtime=
            [local,skinname,skintype,vocation120,initialcash](const auto&,const auto&) {
                SkinEditDialog_OnOk_func(local,skinname,vocation120,initialcash,skintype);
                return ::cw::lib::Value{};
            };
        if (::cw::g_cwpy) ::cw::g_cwpy->exec_func(update_runtime);
        else update_runtime({}, {});
        // Python はここで空でない apply_localsettings のタプル
        // 全体を判定するため、 使用中のスキンを保存した場合は常にリソース全体の再読み込み
        // を予約する。 最後のメッセージ更新フラグを、フォント更新フラグとして解釈しない。
        const auto skindirname=m_skindirname_value;
        const ::cw::lib::Callback reload=
            [skindirname](const auto&,const auto&) {
                SkinEditDialog_OnOk_update_skin(skindirname);
                return ::cw::lib::Value{};
            };
        if (::cw::g_cwpy) ::cw::g_cwpy->exec_func(reload);
        else reload({}, {});
    }
    m_closed=true;
    if (m_close_callback) m_close_callback();
}

void SkinEditDialog::OnCancel(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m_closed) return;
    cancel_import_request();
    finish_local_settings(false);
    m_cancelled=true;
    m_closed=true;
    if (m_close_callback) m_close_callback();
}

void SkinEditDialog::OnImportResource(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;(void)request_import();
}
bool SkinEditDialog::confirm_resource_import(const String& message) {return skin_common_confirm(message);}
void SkinEditDialog::cancel_import_request()
{
    m_import_token.reset();m_import_ticket.reset();request_dialog_presentation_refresh();
}
bool SkinEditDialog::request_import(SkinBasePanel::DocumentRequest start)
{
    namespace doc=::cw::lib::documents;
    const auto keep_alive=weak_from_this().lock();
    if(m_closed || import_request_pending() || !keep_alive) return false;
    if(auto panel=local_settings_presenter();panel && panel->document_request_pending()) return false;
    finish_local_settings(true);m_imported_resources=0;m_status_message.clear();m_import_error.clear();
    const auto token=std::make_shared<int>(0);m_import_token=token;
    try {
        const String confirmation="CardWirth用のイメージリソースをインポートしてこのスキンのリソースに上書きしますか？\n(元に戻すことはできません)";
        if(!confirm_resource_import(confirmation)) {if(m_import_token==token) cancel_import_request();return false;}
        if(m_closed || m_import_token!=token) return false;
        const auto target=std::filesystem::absolute(std::filesystem::u8path("Data/Skin")/
            std::filesystem::u8path(m_skindirname_value)/"Resource/Image").lexically_normal();
        doc::Request request;request.operation=doc::Operation::OpenFolder;
        request.title="インポート元(CardWirth 1.50以降の\"Data/Resource\"フォルダ)を選択してください";
        // 以前の選択は選択画面の初期候補にすぎない。Python と同様、取り込みごとに確認する。
        request.initial_location=::cw::lib::encoding::path_to_utf8(std::filesystem::absolute(
            std::filesystem::u8path(m_import_source_dir.empty()?"Data/Resource":m_import_source_dir)).lexically_normal());
        const auto weak=weak_from_this();
        if(!start) start=::cw::dialog::request_presentation_document;
        auto ticket=start(request,[weak,token,target](doc::Result result) {
            if(auto self=weak.lock()) self->finish_import(token,target,std::move(result));
        });
        if(m_import_token==token) {
            m_import_ticket=std::move(ticket);
            if(!m_import_ticket || !m_import_ticket->pending())
                finish_import(token,target,{doc::Status::Failed,{},"フォルダ選択を開始できませんでした。"});
        }
    } catch(const std::exception& error) {
        if(m_import_token==token) {cancel_import_request();m_import_error=error.what();report_import_result();}
    } catch(...) {
        if(m_import_token==token) {cancel_import_request();m_import_error="フォルダ選択に失敗しました。";report_import_result();}
    }
    request_dialog_presentation_refresh();return true;
}
void SkinEditDialog::finish_import(const std::shared_ptr<int>& token,const std::filesystem::path& target,
                                   ::cw::lib::documents::Result result)
{
    namespace doc=::cw::lib::documents;
    if(m_closed || m_import_token!=token) return;
    if(result.status==doc::Status::Cancelled) {cancel_import_request();return;}
    try {
        if(result.status!=doc::Status::Success || result.documents.size()!=1)
            throw std::runtime_error(result.error.empty()?"フォルダを選択できませんでした。":result.error);
        const auto& selected=result.documents.front(); // すべてのコピーが終わるまで、アクセス権と一時保存の利用権を保持する。
        if(selected.kind!=doc::LocationKind::NativePath || selected.location.empty())
            throw std::runtime_error("インポートには読み取り可能なローカルフォルダが必要です。");
        const auto source=std::filesystem::u8path(selected.location);
        if(!std::filesystem::is_directory(source)) throw std::runtime_error("インポート元フォルダが見つかりません。");
        m_import_source_dir=selected.location;
        std::map<String,String> images,cursors;
        for(const auto& [name,mapped]: ::cw::skin::convert::g_IMGTBL) images[lower_ascii(name)]=mapped;
        for(const auto& [name,mapped]: ::cw::skin::convert::g_IMGTBL_C) images[lower_ascii(name)]=mapped;
        // C++ の正しいカーソル表を維持する。Python ではこれらを誤って imgtbl に追加している。
        for(const auto& [name,mapped]: ::cw::skin::convert::g_CURTBL) cursors[lower_ascii(name)]=mapped;
        for(const auto& entry:std::filesystem::directory_iterator(source)) {
            if(!entry.is_regular_file()) continue;
            const auto ext=lower_ascii(::cw::lib::encoding::path_to_utf8(entry.path().extension()));
            const auto name=lower_ascii(::cw::lib::encoding::path_to_utf8(entry.path().stem()));
            if(const auto found=images.find(name);found!=images.end() && is_image_extension(ext)) {
                import_resource_file(entry.path(),target,found->second,ext);++m_imported_resources;
                // Python と同様、コピーしてから別の拡張子や不要になった拡大画像を削除する。
                remove_alternate_images(target,found->second,ext);
            } else if(const auto found=cursors.find(name);found!=cursors.end() && ext==".cur") {
                import_resource_file(entry.path(),target,found->second,ext);++m_imported_resources;
            }
        }
    } catch(const std::exception& error) {m_import_error=error.what();}
      catch(...) {m_import_error="不明なインポートエラーです。";}
    cancel_import_request();report_import_result();
}
void SkinEditDialog::report_import_result()
{
    m_status_message=m_import_error.empty()?String{}:String("リソースのインポート中にエラーが発生しました。\n")+m_import_error;
    if(m_imported_resources>0) {
        if(!m_status_message.empty()) m_status_message+="\n";
        m_status_message+=std::to_string(m_imported_resources)+"件のリソースをインポートしました。";
    } else if(m_status_message.empty()) m_status_message="インポート可能なリソースはありません。";
    request_dialog_presentation_refresh();
    const auto weak=weak_from_this();const auto message=m_status_message;
    ::cw::dialog::post_common_dialog_action([weak,message](const auto&,const auto&)->::cw::lib::Value {
        if(auto self=weak.lock();self && !self->m_closed && self->m_status_message==message) skin_common_message(message);
        return {};
    });
}

void SkinEditDialog::set_import_source(String arg_source_dir)
{
    m_import_source_dir=std::move(arg_source_dir);
}

void SkinEditDialog::_do_layout()
{
    // 実行時の配置は常にこの共通 Presentation
    // モデルが供給し、UI
    // の実装を一つに保つ。
}

::cw::dialog::DialogPresentation SkinEditDialog::dialog_presentation()
{
    static const std::vector<String> tabs{"基本","描画","フォント"};
    use_info_panel(m_info,m_presentation_tab==0,m_warning_text.empty()?44:72,480);
    use_local_settings(m_presentation_tab>=1?static_cast<int>(m_presentation_tab-1):-1);
    place_local_settings(m_warning_text.empty()?44:72);
    auto result=render_skin_presentation("SKIN_EDIT","スキンの編集",m_warning_text,tabs,
        m_presentation_tab,true,true,dialog_retained_surface(),active_local_settings(),active_info_panel());
    if(import_request_pending()) {
        for(auto& control:result.controls) if(control.action!=::cw::dialog::DialogPresentationAction::Cancel) control.enabled=false;
        result.wants_text_input=false;result.composition.clear();
    }
    return result;
}

void SkinEditDialog::dialog_presentation_action(
    const ::cw::dialog::DialogPresentationAction action, const int index)
{
    using Action=::cw::dialog::DialogPresentationAction;
    if (m_closed) return;
    if(import_request_pending()) {if(action==Action::Cancel) OnCancel({});return;}
    use_info_panel(m_info,m_presentation_tab==0,m_warning_text.empty()?44:72,480);
    if(info_panel_action(action,index)) return;
    use_local_settings(m_presentation_tab>=1?static_cast<int>(m_presentation_tab-1):-1);
    if(local_settings_action(action,index)) return;
    if (action==Action::Accept) { OnOk({}); return; }
    if (action==Action::Cancel) { OnCancel({}); return; }
    if (action==Action::Create) { OnImportResource({}); return; }
    if (action==Action::Previous) { m_presentation_page=std::max<Integer>(0,m_presentation_page-1); return; }
    if (action==Action::Next) { ++m_presentation_page; return; }
    if (action!=Action::Auxiliary) return;
    if (index>=1000 && index<1003) {
        m_presentation_tab=index-1000; m_presentation_page=0;
        use_info_panel(m_info,m_presentation_tab==0,m_warning_text.empty()?44:72,480);
        use_local_settings(m_presentation_tab>=1?static_cast<int>(m_presentation_tab-1):-1); return;
    }
}

bool SkinEditDialog::dialog_presentation_finished() const noexcept { return m_closed; }
void SkinEditDialog::dialog_presentation_complete() {}

void SkinFeaturePanel::initialize(std::shared_ptr<::cw::lib::Panel> arg_parent, std::shared_ptr<::cw::skin::convert::Converter> arg_conv)
{
    (void)arg_parent;
    g_active_feature=this;
    m_column_labels={"名称","器用","敏捷","知力","筋力","生命","精神","好戦","社交","勇猛","慎重","狡猾"};
    m_row_labels.clear();
    const auto base=::cw::data::xml2etree("Data/SkinBase/Skin.xml","",std::nullopt,std::nullopt,true);
    if (base) for (const auto& data:feature_elements(base)) m_row_labels.push_back(data->gettext("Name",""));
    set_values(std::move(arg_conv));
    _do_layout();
}

void SkinFeaturePanel::set_values(std::shared_ptr<::cw::skin::convert::Converter> arg_conv)
{
    m_rows.clear();
    if (!arg_conv || !arg_conv->m_data) {
        m_rows.resize(m_row_labels.size());
        return;
    }
    for (const auto& data:feature_elements(arg_conv->m_data)) {
        SkinFeatureRow row{};
        get_feature_row(data,row);
        m_rows.push_back(std::move(row));
    }
    if (m_rows.size()<m_row_labels.size()) m_rows.resize(m_row_labels.size());
    m_valid=true;
}

void SkinFeaturePanel::get_values(std::shared_ptr<::cw::skin::convert::Converter> arg_conv)
{
    m_valid=true;
    if (!arg_conv || !arg_conv->m_data) return;
    const auto elements=feature_elements(arg_conv->m_data);
    const auto count=std::min(elements.size(),m_rows.size());
    for (std::size_t i=0;i<count;++i) set_feature_row(elements[i],m_rows[i]);
}

void SkinFeaturePanel::set_cell(Integer arg_row, Integer arg_col, String arg_value)
{
    if (arg_row<0 || arg_col<0 || arg_col>=12) return;
    if (static_cast<std::size_t>(arg_row)>=m_rows.size()) m_rows.resize(static_cast<std::size_t>(arg_row)+1);
    m_rows[static_cast<std::size_t>(arg_row)][static_cast<std::size_t>(arg_col)]=std::move(arg_value);
}

String SkinFeaturePanel::get_cell(Integer arg_row, Integer arg_col) const
{
    if (arg_row<0 || arg_col<0 || arg_col>=12 || static_cast<std::size_t>(arg_row)>=m_rows.size()) return {};
    return m_rows[static_cast<std::size_t>(arg_row)][static_cast<std::size_t>(arg_col)];
}

void SkinFeaturePanel::_do_layout()
{
}


namespace {
constexpr int info_type_arrow=6,info_cash_up=7,info_cash_down=8,info_scrollbar=9,info_choice_first=100;
auto info_font() { return ::cw::lib::ui::configured_font("inputname",12); }
int info_line_height() { return static_cast<int>(info_font().pixels)+5; }
}

::cw::lib::Rect SkinInfoPanel::edit_rect(int field) const
{
    if(field==3) return {84,108,636,m_presentation_height-158};
    if(field==4) return {84,m_presentation_height-34,122,28};
    return {84,field*36,field==0?610:636,28};
}
std::vector<::cw::dialog::textedit::Line> SkinInfoPanel::edit_lines() const
{
    const auto font=info_font();
    return ::cw::dialog::textedit::layout(m_edits[3].text,edit_rect(3).width-22,
        [&](std::string_view text){return static_cast<int>(::cw::lib::ui::label_width(
            std::string(text),font.pixels,font.bold,font.face,font.italic));});
}
void SkinInfoPanel::sync_edits()
{
    const std::array<String,5> values{m_type,m_name,m_author,m_description,std::to_string(m_initial_cash)};
    for(int i=0;i<5;++i)
        if(i!=m_focus && values[i]!=m_edits[i].text) m_edits[i].reset(values[i]);
}
void SkinInfoPanel::publish_edit()
{
    if(m_focus==0) m_type=m_edits[0].text;
    else if(m_focus==1) m_name=m_edits[1].text;
    else if(m_focus==2) m_author=m_edits[2].text;
    else if(m_focus==3) m_description=m_edits[3].text;
    else if(m_focus==4) m_initial_cash=std::clamp<Integer>(
        skin_parse_integer(m_edits[4].text,m_initial_cash),0,::cw::lib::money::maximum);
    request_dialog_presentation_refresh();
}
void SkinInfoPanel::finish_edit()
{
    publish_edit();
    m_focus=-1;m_type_popup=false;m_dragging=false;
    for(auto& edit:m_edits) edit.set_composition("");
    sync_edits();
    request_dialog_presentation_refresh();
}
void SkinInfoPanel::focus_edit(int field)
{
    if(field==m_focus) return;
    finish_edit();
    if(field>=0 && field<=5) m_focus=field;
}
::cw::dialog::DialogPresentation SkinInfoPanel::dialog_presentation()
{
    using Action=::cw::dialog::DialogPresentationAction;
    namespace ui=::cw::lib::ui;
    namespace edit=::cw::dialog::textedit;
    sync_edits();
    ::cw::dialog::DialogPresentation result;result.route="SKIN_INFO";
    const auto font=info_font();
    const auto label_font=ui::configured_font("dlgmsg2",12);
    const std::array<String,5> labels{"種別","名前","作者","解説","初期資金"};
    auto& retained=dialog_retained_surface();
    ui::ScaledRetainedCanvas canvas(retained,{720,m_presentation_height},std::max<double>(::cw::g_UP_WIN,1.0));
    const auto mix=[](std::uint64_t& seed,std::uint64_t value){seed^=value+0x9e3779b97f4a7c15ULL+(seed<<6U)+(seed>>2U);};
    std::array<::cw::lib::Rect,5> regions{{{0,0,720,36},{0,36,720,36},{0,72,720,36},
        {0,108,720,m_presentation_height-158},{0,m_presentation_height-50,720,50}}};
    // 変更検出用の値を保存する前に、キャレットによる複数行スクロールを解
    // 決する。 先に保存すると、最初の下端フレームに古いスクロール値が記
    // 録され、後でホイールでその位置へ戻った時に変更なしと誤判定する。
    {
        auto& buffer=m_edits[3];const auto rect=edit_rect(3);
        edit::Preedit preedit;if(m_focus==3)preedit.set(buffer.composition,buffer.composition_start,buffer.composition_length);
        const auto display=edit::display_text(buffer.text,buffer.caret,buffer.anchor,preedit);
        const int line_height=info_line_height(),visible=std::max<int>(1,(rect.height-6)/line_height);
        const auto lines=edit::layout(display.text,rect.width-22,[&](std::string_view value){return static_cast<int>(
            ui::label_width(String(value),font.pixels,font.bold,font.face,font.italic));});
        int caret_row=0;for(int i=0;i<static_cast<int>(lines.size());++i)if(display.caret>=lines[i].first)caret_row=i;
        if(buffer.reveal && m_focus==3){buffer.scroll=std::min(buffer.scroll,caret_row);buffer.scroll=std::max(buffer.scroll,caret_row-visible+1);}
        buffer.scroll=std::clamp(buffer.scroll,0,std::max(0,static_cast<int>(lines.size())-visible));buffer.reveal=false;
    }
    std::array<bool,5> changed{};
    for(int field=0;field<5;++field) {
        const auto& buffer=m_edits[field];std::uint64_t signature=std::hash<String>{}(buffer.text);
        mix(signature,std::hash<String>{}(buffer.composition));mix(signature,buffer.caret);mix(signature,buffer.anchor);
        mix(signature,static_cast<std::uint64_t>(buffer.horizontal));mix(signature,static_cast<std::uint64_t>(buffer.scroll));
        mix(signature,static_cast<std::uint64_t>(m_focus==field));mix(signature,static_cast<std::uint64_t>(m_type_popup));
        if(field==4) mix(signature,static_cast<std::uint64_t>(m_vocation120_value));
        changed[field]=canvas.component_changed("skin.info.field."+std::to_string(field),regions[field],signature);
    }
    const int popup_count=static_cast<int>(m_type_choices.size()),popup_visible=std::min(8,popup_count);
    const ::cw::lib::Rect popup_rect{84,28,636,popup_visible*24};
    bool popup_changed=false;
    if(m_type_popup) {
        m_choice=std::clamp(m_choice,0,std::max(0,popup_count-1));
        m_choice_top=std::clamp(m_choice_top,0,std::max(0,popup_count-popup_visible));
        std::uint64_t signature=static_cast<std::uint64_t>(m_choice+1);
        mix(signature,static_cast<std::uint64_t>(m_choice_top+1));
        for(const auto& item:m_type_choices) mix(signature,std::hash<String>{}(item));
        popup_changed=canvas.component_changed("skin.info.popup",popup_rect,signature);
    } else (void)retained.remove_component("skin.info.popup");
    canvas.capture_base_and_restore();
    for(int field=0;field<4;++field) {
        const auto rect=edit_rect(field);auto& buffer=m_edits[field];const auto region=regions[field];
        if(changed[field]) {
            auto layer=std::make_shared<::cw::lib::Surface>();layer->size={region.width,region.height};
            layer->rgba.assign(static_cast<std::size_t>(region.width*region.height)*4,255);
            ui::fill_rect(layer,{0,0,region.width,region.height},{240,240,240,255});
            const ::cw::lib::Rect local{rect.x,rect.y-region.y,rect.width,rect.height};
            ui::draw_label(layer,labels[field],0,local.y+6,label_font.pixels,label_font.bold,{0,0,0},label_font.face);
            ui::fill_rect(layer,local,{255,255,255,255});ui::draw_sunken_border(layer,local);
            edit::Preedit preedit;if(field==m_focus) preedit.set(buffer.composition,buffer.composition_start,buffer.composition_length);
            const auto display=edit::display_text(buffer.text,buffer.caret,buffer.anchor,preedit);
            auto inner=std::make_shared<::cw::lib::Surface>();inner->size={rect.width-(field==3?22:8),rect.height-6};
            inner->rgba.assign(static_cast<std::size_t>(inner->size.width*inner->size.height)*4,255);
            if(field==3) {
                const int line_height=info_line_height(),visible=std::max<int>(1,inner->size.height/line_height);
                const auto lines=edit::layout(display.text,inner->size.width,[&](std::string_view value){return static_cast<int>(ui::label_width(String(value),font.pixels,font.bold,font.face,font.italic));});
                int caret_row=0;for(int i=0;i<static_cast<int>(lines.size());++i) if(display.caret>=lines[i].first) caret_row=i;
                if(buffer.reveal && field==m_focus) {buffer.scroll=std::min(buffer.scroll,caret_row);buffer.scroll=std::max(buffer.scroll,caret_row-visible+1);}
                buffer.scroll=std::clamp(buffer.scroll,0,std::max(0,static_cast<int>(lines.size())-visible));buffer.reveal=false;
                for(int row=buffer.scroll;row<std::min<int>(lines.size(),buffer.scroll+visible);++row) {
                    const auto line=lines[row];const auto offset=[&](std::size_t pos){return std::clamp(pos,line.first,line.last)-line.first;};
                    auto one=display;one.text=display.text.substr(line.first,line.last-line.first);one.caret=offset(display.caret);
                    one.first=offset(display.first);one.last=offset(display.last);one.preedit_first=offset(display.preedit_first);one.preedit_last=offset(display.preedit_last);
                    one.composing=display.composing && display.preedit_first<=line.last && display.preedit_last>=line.first;
                    auto row_surface=std::make_shared<::cw::lib::Surface>();row_surface->size={inner->size.width,line_height};row_surface->rgba.assign(static_cast<std::size_t>(inner->size.width*line_height)*4,255);
                    int horizontal=0;const bool selected=display.first<line.last && display.last>line.first;
                    edit::draw_single_line(row_surface,one,font,1,field==m_focus && (caret_row==row || selected || one.composing),horizontal);
                    ui::blend_surface(inner,row_surface,0,(row-buffer.scroll)*line_height);
                }
                const ::cw::lib::Rect track{local.x+local.width-16,local.y+2,14,local.height-4};
                ui::fill_rect(layer,track,{230,230,230,255});const int thumb=std::max<int>(16,track.height*visible/std::max<int>(visible,lines.size()));
                const int travel=track.height-thumb,scrollmax=std::max<int>(1,static_cast<int>(lines.size())-visible);
                ui::fill_rect(layer,{track.x,track.y+travel*buffer.scroll/scrollmax,track.width,thumb},{160,160,160,255});
            } else edit::draw_single_line(inner,display,font,1,field==m_focus,buffer.horizontal);
            ui::blend_surface(layer,inner,local.x+4,local.y+3);
            if(field==0) {::cw::dialog::DialogPresentation painted;skin_presentation_button(painted,layer,"v",{694,0,26,28},Action::Auxiliary,info_type_arrow);}
            canvas.composite(region,layer);
        }
        result.controls.push_back({Action::Select,field,static_cast<float>(rect.x),static_cast<float>(rect.y),
            static_cast<float>(rect.width-(field==3?18:0)),static_cast<float>(rect.height),true,true,labels[field]});
        if(field==0) result.controls.push_back({Action::Auxiliary,info_type_arrow,694,0,26,28,true});
        if(field==3) {const ::cw::lib::Rect track{rect.x+rect.width-16,rect.y+2,14,rect.height-4};result.controls.push_back({Action::Auxiliary,info_scrollbar,
            static_cast<float>(track.x),static_cast<float>(track.y),static_cast<float>(track.width),static_cast<float>(track.height),true,true});}
    }
    const auto cash=edit_rect(4);auto& cash_buffer=m_edits[4];
    if(changed[4]) {
        const auto region=regions[4];auto layer=std::make_shared<::cw::lib::Surface>();layer->size={720,50};layer->rgba.assign(720*50*4,255);ui::fill_rect(layer,{0,0,720,50},{240,240,240,255});
        const ::cw::lib::Rect local{cash.x,cash.y-region.y,cash.width,cash.height};ui::draw_label(layer,labels[4],0,local.y+6,label_font.pixels,label_font.bold,{0,0,0},label_font.face);
        ui::fill_rect(layer,local,{255,255,255,255});ui::draw_sunken_border(layer,local);auto inner=std::make_shared<::cw::lib::Surface>();inner->size={cash.width-8,cash.height-6};inner->rgba.assign(static_cast<std::size_t>(inner->size.width*inner->size.height)*4,255);
        edit::Preedit preedit;if(m_focus==4)preedit.set(cash_buffer.composition,cash_buffer.composition_start,cash_buffer.composition_length);
        edit::draw_single_line(inner,edit::display_text(cash_buffer.text,cash_buffer.caret,cash_buffer.anchor,preedit),font,1,m_focus==4,cash_buffer.horizontal);ui::blend_surface(layer,inner,local.x+4,local.y+3);
        ::cw::dialog::DialogPresentation painted;skin_presentation_button(painted,layer,"+",{206,16,20,14},Action::Auxiliary,info_cash_up);skin_presentation_button(painted,layer,"-",{206,30,20,14},Action::Auxiliary,info_cash_down);
        const ::cw::lib::Rect check{248,22,16,16};ui::draw_checkbox(layer,check,m_vocation120_value);
        ui::draw_label(layer,"CardWirth 1.20相当のカード適性計算を行う",272,22,label_font.pixels,label_font.bold,{0,0,0},label_font.face);canvas.composite(region,layer);
    }
    result.controls.push_back({Action::Select,4,static_cast<float>(cash.x),static_cast<float>(cash.y),static_cast<float>(cash.width),static_cast<float>(cash.height),true,true,labels[4]});
    result.controls.push_back({Action::Auxiliary,info_cash_up,206,static_cast<float>(m_presentation_height-34),20,14,true});
    result.controls.push_back({Action::Auxiliary,info_cash_down,206,static_cast<float>(m_presentation_height-20),20,14,true});
    result.controls.push_back({Action::Toggle,5,244,static_cast<float>(m_presentation_height-34),476,28,true});
    if(m_focus>=0 && m_focus<5) {
        const auto& buffer=m_edits[m_focus];result.wants_text_input=true;result.input_text=buffer.text;
        result.composition=buffer.composition;result.composition_start=buffer.composition_start;
        result.composition_length=buffer.composition_length;
    }
    if(m_type_popup) {
        result.controls.push_back({Action::Auxiliary,99,0,0,720,static_cast<float>(m_presentation_height),true});
        if(popup_changed) {
        auto popup=std::make_shared<::cw::lib::Surface>();popup->size={popup_rect.width,popup_rect.height};popup->rgba.assign(static_cast<std::size_t>(popup_rect.width*popup_rect.height)*4,255);
        ui::fill_rect(popup,{0,0,popup_rect.width,popup_rect.height},{255,255,255,255});ui::draw_sunken_border(popup,{0,0,popup_rect.width,popup_rect.height});
        const auto selection=ui::system_selection_colours();
        for(int row=0;row<popup_visible;++row) {
            const int i=m_choice_top+row,y=28+row*24;
            if(i==m_choice) ui::fill_rect(popup,{1,row*24,634,24},selection.background);
            ui::draw_label(popup,m_type_choices[i],4,row*24+4,font.pixels,font.bold,
                i==m_choice?std::tuple<Integer,Integer,Integer>{selection.text.r,selection.text.g,selection.text.b}:std::tuple<Integer,Integer,Integer>{0,0,0},font.face);
        }
        canvas.composite(popup_rect,popup);
        }
        for(int row=0;row<popup_visible;++row) {
            const int i=m_choice_top+row,y=28+row*24;
            result.controls.push_back({Action::Select,info_choice_first+i,84,static_cast<float>(y),636,24,true});
        }
    }
    canvas.finish();result.surface=canvas.surface;result.damage=retained.damage;result.surface_revision=retained.revision;
    const auto scale=std::max<double>(::cw::g_UP_WIN,1.0);
    for(auto& control:result.controls) {control.x*=static_cast<float>(scale);control.y*=static_cast<float>(scale);
        control.width*=static_cast<float>(scale);control.height*=static_cast<float>(scale);}
    return result;
}
void SkinInfoPanel::dialog_presentation_action(::cw::dialog::DialogPresentationAction action,int index)
{
    using Action=::cw::dialog::DialogPresentationAction;
    if(m_type_popup) {
        if(action==Action::Select && index>=info_choice_first && index-info_choice_first<static_cast<int>(m_type_choices.size())) {
            m_type=m_type_choices[index-info_choice_first];m_edits[0].reset(m_type);
        }
        m_type_popup=false;request_dialog_presentation_refresh();return;
    }
    if(action==Action::Select && index>=0 && index<5) focus_edit(index);
    else if(action==Action::Toggle && index==5) {focus_edit(5);m_vocation120_value=!m_vocation120_value;}
    else if(action==Action::Auxiliary && index==info_type_arrow) {
        focus_edit(0);m_type_popup=true;
        const auto found=std::find(m_type_choices.begin(),m_type_choices.end(),m_type);
        m_choice=found==m_type_choices.end()?0:static_cast<int>(found-m_type_choices.begin());
        m_choice_top=std::max(0,m_choice-7);
    } else if(action==Action::Auxiliary && (index==info_cash_up || index==info_cash_down)) {
        finish_edit();m_initial_cash=std::clamp<Integer>(m_initial_cash+(index==info_cash_up?1:-1),0,::cw::lib::money::maximum);
        sync_edits();m_focus=4;
    }
    request_dialog_presentation_refresh();
}
void SkinInfoPanel::dialog_presentation_text(const std::string& text,bool composition)
{
    if(m_type_popup || m_focus<0 || m_focus>=5) return;
    auto& edit=m_edits[m_focus];
    if(composition) edit.set_composition(text);
    else {if(text=="\b" && !edit.composition.empty()) return;edit.insert(text,0,m_focus==3);publish_edit();}
    request_dialog_presentation_refresh();
}
void SkinInfoPanel::dialog_presentation_text_event(const ::cw::dialog::DialogTextEditingEvent& event)
{
    if(event.composition && !m_type_popup && m_focus>=0 && m_focus<5)
        m_edits[m_focus].set_composition(event.text,event.start,event.length);
    else dialog_presentation_text(event.text,event.composition);
    request_dialog_presentation_refresh();
}
void SkinInfoPanel::dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers)
{
    if(m_type_popup) dialog_presentation_action(::cw::dialog::DialogPresentationAction::Select,info_choice_first+m_choice);
    else if(m_focus==3 && modifiers==0) dialog_presentation_text("\n",false);
}
void SkinInfoPanel::dialog_presentation_key(::cw::dialog::DialogPresentationKey key,bool pressed,
    ::cw::dialog::DialogPresentationModifiers modifiers)
{
    using Key=::cw::dialog::DialogPresentationKey;
    namespace edit=::cw::dialog::textedit;
    m_modifiers=modifiers;
    if(!pressed) return;
    if(modifiers & ::cw::dialog::DialogPresentationModifierAlt) {
        if(m_focus==0 && key==Key::Down) dialog_presentation_action(::cw::dialog::DialogPresentationAction::Auxiliary,info_type_arrow);
        return;
    }
    const bool ctrl=(modifiers & ::cw::dialog::DialogPresentationModifierControl)!=0;
    const bool shift=(modifiers & ::cw::dialog::DialogPresentationModifierShift)!=0;
    if(m_type_popup) {
        if(key==Key::Enter) dialog_presentation_enter(modifiers);
        else {
            if(key==Key::Home) m_choice=0;
            if(key==Key::End) m_choice=static_cast<int>(m_type_choices.size())-1;
            if(key==Key::Up) --m_choice;
            if(key==Key::Down) ++m_choice;
            m_choice=std::clamp(m_choice,0,std::max(0,static_cast<int>(m_type_choices.size())-1));
            m_choice_top=std::max(std::min(m_choice_top,m_choice),m_choice-7);
        }
    } else if(m_focus>=0 && m_focus<5 && !m_edits[m_focus].composition.empty()) return;
    else if(key==Key::Tab && !ctrl) focus_edit((m_focus+(shift?5:1)+6)%6);
    else if(key==Key::Space && m_focus==5) m_vocation120_value=!m_vocation120_value;
    else if(m_focus==0 && (key==Key::Down || key==Key::Up || key==Key::F4))
        dialog_presentation_action(::cw::dialog::DialogPresentationAction::Auxiliary,info_type_arrow);
    else if(m_focus==4 && (key==Key::Up || key==Key::Down))
        dialog_presentation_action(::cw::dialog::DialogPresentationAction::Auxiliary,key==Key::Up?info_cash_up:info_cash_down);
    else if(m_focus>=0 && m_focus<5) {
        auto& buffer=m_edits[m_focus];
        if(m_focus==3 && ctrl && key==Key::V) buffer.insert(::cw::dialog::presentation_clipboard_text(),0,true);
        else if(m_focus==3 && !ctrl && (key==Key::Home || key==Key::End || key==Key::Up || key==Key::Down ||
                                        key==Key::PageUp || key==Key::PageDown)) {
            const auto lines=edit_lines();int row=0;
            for(int i=0;i<static_cast<int>(lines.size());++i) if(buffer.caret>=lines[i].first) row=i;
            if(key==Key::Home) buffer.move(lines[row].first,shift);
            else if(key==Key::End) buffer.move(lines[row].last,shift);
            else {
                const auto column=edit::count(std::string_view(buffer.text).substr(lines[row].first,buffer.caret-lines[row].first));
                const int amount=(key==Key::PageUp || key==Key::PageDown)?std::max<int>(1,(edit_rect(3).height-6)/info_line_height()):1;
                row=std::clamp(row+((key==Key::Up || key==Key::PageUp)?-amount:amount),0,static_cast<int>(lines.size())-1);
                auto position=lines[row].first;
                for(std::size_t c=0;c<column && position<lines[row].last;++c) position=edit::next(buffer.text,position);
                buffer.move(position,shift);
            }
        } else edit::single_line_key(buffer,key,modifiers,0,::cw::dialog::presentation_clipboard_text,
                                    ::cw::dialog::presentation_set_clipboard_text);
        publish_edit();
    }
    request_dialog_presentation_refresh();
}
void SkinInfoPanel::dialog_presentation_pointer(::cw::dialog::DialogPresentationAction action,int index,
    float x,float y,bool pressed)
{
    namespace edit=::cw::dialog::textedit;
    const auto scale=std::max<double>(::cw::g_UP_WIN,1.0);x/=static_cast<float>(scale);y/=static_cast<float>(scale);
    if(index==info_scrollbar && action==::cw::dialog::DialogPresentationAction::Auxiliary) {
        const auto rect=edit_rect(3);const int visible=std::max<int>(1,(rect.height-6)/info_line_height());
        const int maximum=std::max<int>(0,static_cast<int>(edit_lines().size())-visible);
        m_edits[3].scroll=std::clamp<int>(static_cast<int>((y-rect.y)*maximum/std::max<Integer>(1,rect.height-4)),0,maximum);
        m_edits[3].reveal=false;request_dialog_presentation_refresh();return;
    }
    if(index<0 || index>=5 || m_type_popup || (!pressed && !m_dragging)) return;
    if(m_focus>=0 && m_focus<5 && !m_edits[m_focus].composition.empty()) return;
    focus_edit(index);auto& buffer=m_edits[index];const auto rect=edit_rect(index);const auto font=info_font();
    std::size_t first=0,last=buffer.text.size();
    if(index==3) {
        const auto lines=edit_lines();
        const int row=std::clamp<int>(static_cast<int>((y-rect.y-3)/info_line_height())+buffer.scroll,0,static_cast<int>(lines.size())-1);
        first=lines[row].first;last=lines[row].last;
    }
    const auto target=x-rect.x-4+(index==3?0:buffer.horizontal);
    auto pos=first;
    while(pos<last) {
        const auto next=edit::next(buffer.text,pos);
        const auto left=::cw::lib::ui::label_width(buffer.text.substr(first,pos-first),font.pixels,font.bold,font.face,font.italic);
        const auto right=::cw::lib::ui::label_width(buffer.text.substr(first,next-first),font.pixels,font.bold,font.face,font.italic);
        if(target<(left+right)/2) break;pos=next;
    }
    buffer.move(pos,m_dragging || (m_modifiers & ::cw::dialog::DialogPresentationModifierShift));
    m_dragging=pressed;request_dialog_presentation_refresh();
}
void SkinInfoPanel::dialog_presentation_scroll(float x,float y,int lines)
{
    const auto scale=std::max<double>(::cw::g_UP_WIN,1.0);x/=static_cast<float>(scale);y/=static_cast<float>(scale);
    if(m_type_popup) {
        m_choice=std::clamp(m_choice+lines,0,std::max(0,static_cast<int>(m_type_choices.size())-1));
        m_choice_top=std::max(std::min(m_choice_top,m_choice),m_choice-7);
    } else {
        const auto rect=edit_rect(3);
        if(x<rect.x || x>=rect.x+rect.width || y<rect.y || y>=rect.y+rect.height) return;
        const int visible=std::max<int>(1,(edit_rect(3).height-6)/info_line_height());
        m_edits[3].scroll=std::clamp<int>(m_edits[3].scroll+lines,0,
            std::max<int>(0,static_cast<int>(edit_lines().size())-visible));
        m_edits[3].reveal=false;
    }
    request_dialog_presentation_refresh();
}


namespace {
::cw::lib::Rect skin_path_rect(int row) { return {84,row*32,548,26}; }
}

String& SkinBasePanel::path_value(int row)
{
    switch(row) {
    case 0:return m_exe_value;case 1:return m_datadir_value;case 2:return m_scenariodir_value;
    default:return m_yadodir_value;
    }
}
void SkinBasePanel::sync_edits()
{
    for(int row=0;row<4;++row) if(m_focus!=row && m_edits[row].text!=path_value(row)) m_edits[row].reset(path_value(row));
}
void SkinBasePanel::publish_edit()
{
    if(m_focus>=0) {
        if(path_value(m_focus)!=m_edits[m_focus].text) m_access[m_focus]={};
        path_value(m_focus)=m_edits[m_focus].text;
    }
    OnInput({});
}
void SkinBasePanel::clear_composition()
{
    if(m_focus>=0) m_edits[m_focus].set_composition("");
    request_dialog_presentation_refresh();
}
void SkinBasePanel::finish_edit(bool check_exe)
{
    if(m_finished) return;
    const bool exe=m_focus==0;
    publish_edit();m_focus=-1;m_dragging=false;
    for(auto& buffer:m_edits) buffer.set_composition("");
    sync_edits();
    if(check_exe && exe) queue_exe_check();
}
void SkinBasePanel::close_panel()
{
    finish_edit(false);m_finished=true;cancel_document_request();m_on_extracted={};
}
bool SkinBasePanel::confirm_extraction(const String& message) { return skin_common_confirm(message); }
void SkinBasePanel::extract_executable(const String& path) { m_converter->init(path); }
void SkinBasePanel::queue_exe_check()
{
    if(m_finished || document_request_pending() || m_exe_check_pending || m_exe_value==m_original_exe) return;
    const auto weak=weak_from_this();if(weak.expired()) return;
    m_exe_check_pending=true;
    if(!::cw::dialog::post_common_dialog_action([weak](const auto&,const auto&)->::cw::lib::Value {
        if(const auto self=weak.lock()) {
            self->m_exe_check_pending=false;
            if(self->m_finished || self->document_request_pending()) return {};
            try {self->_selected_exe(self->m_exe_value);}
            catch(const std::exception& error) {self->report_error(error.what());}
        }
        return {};
    })) m_exe_check_pending=false;
}
void SkinBasePanel::report_error(String message)
{
    m_error_message=std::move(message);request_dialog_presentation_refresh();
    const auto weak=weak_from_this();const auto text=m_error_message;
    ::cw::dialog::post_common_dialog_action([weak,text](const auto&,const auto&)->::cw::lib::Value {
        if(auto self=weak.lock();self && !self->m_finished) skin_common_message(text);
        return {};
    });
}
void SkinBasePanel::cancel_document_request()
{
    m_document_token.reset();m_document_ticket.reset();request_dialog_presentation_refresh();
}
bool SkinBasePanel::request_document(int row,DocumentRequest start)
{
    namespace doc=::cw::lib::documents;
    if(row<0 || row>=4 || m_finished || document_request_pending() || weak_from_this().expired()) return false;
    finish_edit(false);m_error_message.clear();
    std::error_code error;
    auto base=std::filesystem::absolute(std::filesystem::u8path(_get_basedir().empty()?".":_get_basedir()),error);
    if(error) {report_error("本体の基準フォルダを取得できません。");return false;}
    doc::Request request;
    static const std::array<String,4> titles{"スキン生成元となるカードワース本体の選択",
        "スキン生成元のデータフォルダを選択してください。","スキン生成元のシナリオフォルダを選択してください。",
        "スキン生成元の宿フォルダを選択してください。"};
    request.title=titles[row];request.operation=row==0?doc::Operation::OpenFiles:doc::Operation::OpenFolder;
    auto location=std::filesystem::u8path(path_value(row));
    if(row>0 && !location.is_absolute()) location=base/location;
    if(!location.empty()) {
        location=std::filesystem::absolute(location,error);
        if(!error) request.initial_location=::cw::lib::encoding::path_to_utf8(location.lexically_normal());
    }
    if(row==0) request.filters={{"カードワース本体","exe"},{"すべてのファイル","*"}};
    const auto weak=weak_from_this();const auto token=std::make_shared<int>(0);m_document_token=token;
    request_dialog_presentation_refresh();
    if(!start) start=::cw::dialog::request_presentation_document;
    try {
        auto ticket=start(request,[weak,token,row,base](doc::Result result) {
            if(auto self=weak.lock()) self->finish_document(token,row,base,std::move(result));
        });
        if(m_document_token==token) {
            m_document_ticket=std::move(ticket);
            if(!m_document_ticket || !m_document_ticket->pending())
                finish_document(token,row,base,{doc::Status::Failed,{},"ファイル選択を開始できませんでした。"});
        }
    } catch(const std::exception& error) {finish_document(token,row,base,{doc::Status::Failed,{},error.what()});}
      catch(...) {finish_document(token,row,base,{doc::Status::Failed,{},"ファイル選択に失敗しました。"});}
    return true;
}
void SkinBasePanel::finish_document(const std::shared_ptr<int>& token,int row,const std::filesystem::path& base,
                                   ::cw::lib::documents::Result result)
{
    namespace doc=::cw::lib::documents;
    if(m_document_token!=token || m_finished) return;
    cancel_document_request();
    if(result.status==doc::Status::Cancelled) return;
    if(result.status!=doc::Status::Success || result.documents.size()!=1) {
        report_error(result.error.empty()?"ファイルを選択できませんでした。":result.error);return;
    }
    auto selected=std::move(result.documents.front());
    if(selected.kind!=doc::LocationKind::NativePath || selected.location.empty()) {
        report_error("スキン生成には継続して読み取れるローカルのファイル・フォルダが必要です。");return;
    }
    try {
        const auto path=std::filesystem::u8path(selected.location);
        std::error_code error;
        const bool valid=row==0?std::filesystem::is_regular_file(path,error):std::filesystem::is_directory(path,error);
        if(!valid || error) {report_error(row==0?"選択した本体が見つかりません。":"選択したフォルダが見つかりません。");return;}
        String value=selected.location;
        if(row>0) {
            value=::cw::util::selected_document_path(selected.location,::cw::lib::encoding::path_to_utf8(base));
        }
        m_access[row]=std::move(selected);
        // ファイル用の生成処理は target.SetValue より前にコールバック
        // を呼ぶ。 フォルダー分岐にはファイルのコールバックがなく、値を直接確定する。
        if(row==0) _selected_exe(value);
        path_value(row)=std::move(value);sync_edits();OnInput({});
    } catch(const std::exception& error) {report_error(error.what());}
}
::cw::dialog::DialogPresentation SkinBasePanel::dialog_presentation()
{
    namespace ui=::cw::lib::ui;namespace edit=::cw::dialog::textedit;
    using Action=::cw::dialog::DialogPresentationAction;
    sync_edits();
    ::cw::dialog::DialogPresentation result;result.route="SKIN_PATHS";
    const auto font=ui::configured_font("inputname",12),label=ui::configured_font("dlgmsg2",12);
    static const std::array<String,4> labels{"本体","データ","シナリオ","宿"};
    const bool enabled=!m_finished && !document_request_pending();
    auto& retained=dialog_retained_surface();
    ui::ScaledRetainedCanvas canvas(retained,{720,136},std::max<double>(::cw::g_UP_WIN,1.0));
    std::array<bool,4> changed{};
    for(int row=0;row<4;++row) {
        std::uint64_t signature=std::hash<String>{}(m_edits[row].text);
        signature^=std::hash<String>{}(m_edits[row].composition)+0x9e3779b97f4a7c15ULL+(signature<<6U)+(signature>>2U);
        signature^=static_cast<std::uint64_t>(m_edits[row].caret)+(signature<<6U)+(signature>>2U);
        signature^=static_cast<std::uint64_t>(m_edits[row].anchor)+(signature<<6U)+(signature>>2U);
        signature^=static_cast<std::uint64_t>(m_edits[row].horizontal)+(signature<<6U)+(signature>>2U);
        signature^=static_cast<std::uint64_t>(m_focus==row)+(signature<<6U)+(signature>>2U);
        signature^=static_cast<std::uint64_t>(enabled)+(signature<<6U)+(signature>>2U);
        changed[row]=canvas.component_changed("skin.paths.row."+std::to_string(row),{0,row*34,720,34},signature);
    }
    canvas.capture_base_and_restore();
    for(int row=0;row<4;++row) {
        const auto rect=skin_path_rect(row);auto& buffer=m_edits[row];
        if(changed[row]) {
            auto layer=std::make_shared<::cw::lib::Surface>();layer->size={720,34};layer->rgba.assign(720*34*4,255);
            ui::fill_rect(layer,{0,0,720,34},{240,240,240,255});
            const ::cw::lib::Rect local{rect.x,rect.y-row*34,rect.width,rect.height};
            ui::draw_label(layer,labels[row],0,local.y+5,label.pixels,label.bold,{0,0,0},label.face);
            ui::fill_rect(layer,local,enabled ? ::cw::lib::Color{255,255,255,255} : ::cw::lib::Color{230,230,230,255});
            ui::draw_sunken_border(layer,local);
            auto inner=std::make_shared<::cw::lib::Surface>();inner->size={rect.width-8,rect.height-4};
            inner->rgba.assign(static_cast<std::size_t>(inner->size.width*inner->size.height)*4,255);
            edit::Preedit preedit;if(m_focus==row) preedit.set(buffer.composition,buffer.composition_start,buffer.composition_length);
            edit::draw_single_line(inner,edit::display_text(buffer.text,buffer.caret,buffer.anchor,preedit),font,1,
                                   enabled && m_focus==row,buffer.horizontal);
            ui::blend_surface(layer,inner,local.x+4,local.y+2);
            ::cw::dialog::DialogPresentation painted;
            skin_presentation_button(painted,layer,"参照...",{640,local.y,80,26},Action::Auxiliary,10+row,enabled);
            canvas.composite({0,row*34,720,34},layer);
        }
        result.controls.push_back({Action::Select,row,static_cast<float>(rect.x),static_cast<float>(rect.y),
            static_cast<float>(rect.width),static_cast<float>(rect.height),enabled,true,labels[row]});
        result.controls.push_back({Action::Auxiliary,10+row,640.0F,static_cast<float>(rect.y),80.0F,26.0F,enabled});
    }
    if(has_focus() && enabled) {
        const auto& buffer=m_edits[m_focus];result.wants_text_input=true;result.input_text=buffer.text;
        result.composition=buffer.composition;result.composition_start=buffer.composition_start;result.composition_length=buffer.composition_length;
    }
    const auto scale=static_cast<float>(std::max<double>(::cw::g_UP_WIN,1.0));
    for(auto& c:result.controls) {c.x*=scale;c.y*=scale;c.width*=scale;c.height*=scale;}
    canvas.finish();result.surface=canvas.surface;result.damage=retained.damage;result.surface_revision=retained.revision;return result;
}
void SkinBasePanel::dialog_presentation_action(::cw::dialog::DialogPresentationAction action,int index)
{
    if(m_finished || document_request_pending()) return;
    if(action==::cw::dialog::DialogPresentationAction::Select && index>=0 && index<4) {
        if(m_focus!=index) {finish_edit(true);m_focus=index;}
    } else if(action==::cw::dialog::DialogPresentationAction::Auxiliary && index>=10 && index<14)
        request_document(index-10);
    request_dialog_presentation_refresh();
}
void SkinBasePanel::dialog_presentation_text(const std::string& text,bool composition)
{
    if(m_finished || document_request_pending() || !has_focus()) return;
    auto& buffer=m_edits[m_focus];
    if(composition) buffer.set_composition(text);
    else {if(text=="\b" && !buffer.composition.empty()) return;buffer.insert(text,0,false);publish_edit();}
    request_dialog_presentation_refresh();
}
void SkinBasePanel::dialog_presentation_text_event(const ::cw::dialog::DialogTextEditingEvent& event)
{
    if(event.composition && !m_finished && !document_request_pending() && has_focus())
        m_edits[m_focus].set_composition(event.text,event.start,event.length);
    else dialog_presentation_text(event.text,event.composition);
    request_dialog_presentation_refresh();
}
void SkinBasePanel::dialog_presentation_key(::cw::dialog::DialogPresentationKey key,bool pressed,
    ::cw::dialog::DialogPresentationModifiers modifiers)
{
    if(m_finished || document_request_pending() || !has_focus() || !pressed) return;
    m_modifiers=modifiers;
    if(has_composition()) return;
    if(key==::cw::dialog::DialogPresentationKey::Tab && !(modifiers & (::cw::dialog::DialogPresentationModifierControl|::cw::dialog::DialogPresentationModifierAlt))) {
        const int row=(m_focus+((modifiers & ::cw::dialog::DialogPresentationModifierShift)?3:1))%4;
        finish_edit(true);m_focus=row;
    } else ::cw::dialog::textedit::single_line_key(m_edits[m_focus],key,modifiers,0,
        ::cw::dialog::presentation_clipboard_text,::cw::dialog::presentation_set_clipboard_text);
    publish_edit();
}
void SkinBasePanel::dialog_presentation_pointer(::cw::dialog::DialogPresentationAction action,int index,float x,float,bool pressed)
{
    if(m_finished || document_request_pending() || has_composition() || index<0 || index>=4 || (!pressed && !m_dragging)) return;
    dialog_presentation_action(action,index);
    auto& buffer=m_edits[index];const auto rect=skin_path_rect(index);const auto font=::cw::lib::ui::configured_font("inputname",12);
    const auto target=x/std::max<double>(::cw::g_UP_WIN,1.0)-rect.x-4+buffer.horizontal;
    std::size_t pos=0;
    while(pos<buffer.text.size()) {
        const auto next=::cw::dialog::textedit::next(buffer.text,pos);
        const auto left=::cw::lib::ui::label_width(buffer.text.substr(0,pos),font.pixels,font.bold,font.face,font.italic);
        const auto right=::cw::lib::ui::label_width(buffer.text.substr(0,next),font.pixels,font.bold,font.face,font.italic);
        if(target<(left+right)/2) break;pos=next;
    }
    buffer.move(pos,m_dragging || (m_modifiers & ::cw::dialog::DialogPresentationModifierShift));m_dragging=pressed;
    request_dialog_presentation_refresh();
}
void SkinBasePanel::dialog_presentation_hover(float x,float y)
{
    if(m_focus!=0 || has_composition() || m_dragging) return;
    const auto scale=std::max<double>(::cw::g_UP_WIN,1.0);x/=static_cast<float>(scale);y/=static_cast<float>(scale);
    const auto rect=skin_path_rect(0);
    if(x<rect.x || x>=rect.x+rect.width || y<rect.y || y>=rect.y+rect.height) queue_exe_check();
}


void SkinInfoPanel::initialize(std::shared_ptr<::cw::lib::Panel> arg_parent)
{
    dialog_retained_surface().reset();
    (void)arg_parent;
    g_active_info=this;
    m_type_choices={"MedievalFantasy","Modern","Monsters","Oedo","School","ScienceFiction"};
    std::error_code ec;
    const auto root=std::filesystem::u8path("Data/Skin");
    if (std::filesystem::is_directory(root,ec)) {
        for (const auto& entry:std::filesystem::directory_iterator(root,ec)) {
            if (ec || !entry.is_directory(ec)) continue;
            const auto tree=::cw::data::xml2etree(::cw::lib::encoding::path_to_utf8(entry.path()/"Skin.xml"),"",std::nullopt,std::nullopt,true);
            if (tree) {
                const auto type=tree->gettext("Property/Type","");
                m_type_choices.push_back(type);
            }
        }
    }
    std::sort(m_type_choices.begin(),m_type_choices.end(),natural_less);
    m_type_choices.erase(std::unique(m_type_choices.begin(),m_type_choices.end()),m_type_choices.end());
    m_types.clear();
    for (const auto& type:m_type_choices) m_types.emplace_back(type);
    _do_layout();
    _bind();
}

void SkinInfoPanel::_bind()
{
}

void SkinInfoPanel::_do_layout()
{
}

void SkinInfoPanel::set_values(String arg_type, String arg_name, String arg_author,
                               String arg_description, bool arg_vocation120, Integer arg_initialcash)
{
    finish_edit();
    m_type=std::move(arg_type);
    m_name=std::move(arg_name);
    m_author=std::move(arg_author);
    m_description=std::move(arg_description);
    m_vocation120_value=arg_vocation120;
    m_initial_cash=std::clamp<Integer>(arg_initialcash,0,::cw::lib::money::maximum);
    validate();
    sync_edits();
}

void SkinInfoPanel::validate()
{
    m_initial_cash=std::clamp<Integer>(m_initial_cash,0,::cw::lib::money::maximum);
}

void SkinMessagePanel::initialize(std::shared_ptr<::cw::lib::Panel> arg_parent, std::shared_ptr<::cw::skin::convert::Converter> arg_conv)
{
    (void)arg_parent;
    g_active_message=this;
    m_row_labels.clear();
    const auto base=::cw::data::xml2etree("Data/SkinBase/Skin.xml","",std::nullopt,std::nullopt,true);
    if (base) {
        for (const auto& message:children(base,"Messages")) {
            const auto text=message->get_text();
            m_row_labels.push_back(text.empty()?String("(空のテキスト)") : ::cw::util::encodewrap(text));
        }
        Integer nature_count=0;
        for (const auto& nature:children(base,"Natures")) {
            if (nature_count++>=6) break;
            m_row_labels.push_back(::cw::util::encodewrap(nature->find_exists("Description")->get_text()));
        }
        for (const auto& path:{"Periods/Period[3]/Coupons/Coupon","Periods/Period[4]/Coupons/Coupon"}) {
            const auto found=base->find(path);
            m_row_labels.push_back(found && *found?::cw::util::encodewrap((*found)->get_text()):String{});
        }
        const auto gameover=::cw::data::xml2etree("Data/SkinBase/Resource/Xml/GameOver/01_GameOver.xml","",std::nullopt,std::nullopt,true);
        if (gameover) {
            const auto talk=gameover->find("Events/Event//Talk");
            if (talk && *talk) {
                m_row_labels.push_back((*talk)->find_exists("Text")->get_text());
                for (const auto* path:{"Contents/Post[1]","Contents/Post[2]","Contents/Post[4]"}) m_row_labels.push_back((*talk)->find_exists(path)->get("name",{}));
            }
        }
    }
    set_values(std::move(arg_conv));
    _do_layout();
}

void SkinMessagePanel::set_values(std::shared_ptr<::cw::skin::convert::Converter> arg_conv)
{
    m_values.clear();
    if (!arg_conv || !arg_conv->m_data) {
        m_values.resize(m_row_labels.size());
        return;
    }
    for (const auto& message:children(arg_conv->m_data,"Messages")) m_values.push_back(::cw::util::encodewrap(message->get_text()));
    Integer nature_count=0;
    for (const auto& nature:children(arg_conv->m_data,"Natures")) {
        if (nature_count++>=6) break;
        m_values.push_back(::cw::util::encodewrap(nature->find_exists("Description")->get_text()));
    }
    for (const auto* path:{"Periods/Period[3]/Coupons/Coupon","Periods/Period[4]/Coupons/Coupon"}) {
        const auto found=arg_conv->m_data->find(path);
        m_values.push_back(found && *found?::cw::util::encodewrap((*found)->get_text()):String{});
    }
    const auto gameover=arg_conv->m_gameover.find("01_GameOver");
    if (gameover!=arg_conv->m_gameover.end() && gameover->second) {
        const auto talk=gameover->second->find("Events/Event//Talk");
        if (talk && *talk) {
            m_values.push_back((*talk)->find_exists("Text")->get_text());
            for (const auto* path:{"Contents/Post[1]","Contents/Post[2]","Contents/Post[4]"}) m_values.push_back((*talk)->find_exists(path)->get("name",{}));
        }
    }
    if (m_values.size()<m_row_labels.size()) m_values.resize(m_row_labels.size());
    m_valid=true;
}

// Python の最後のゲームオーバー項目 Post[4] は get("name",
// edited_value) で読み取るだけになっており、編集値を保存しない。 Post[1]/Post[2]
// と同様に set を使い、このラベルの編集を出力へ反映する。
void SkinMessagePanel::get_values(std::shared_ptr<::cw::skin::convert::Converter> arg_conv)
{
    m_valid=true;
    if (!arg_conv || !arg_conv->m_data) return;
    std::size_t row=0;
    for (const auto& message:children(arg_conv->m_data,"Messages")) if (row<m_values.size()) message->set_text(::cw::util::decodewrap(m_values[row++]));
    Integer nature_count=0;
    for (const auto& nature:children(arg_conv->m_data,"Natures")) {
        if (nature_count++>=6) break;
        if (row>=m_values.size()) break;
        nature->find_exists("Description")->set_text(::cw::util::decodewrap(m_values[row++]));
    }
    for (const auto* path:{"Periods/Period[3]/Coupons/Coupon","Periods/Period[4]/Coupons/Coupon"}) {
        if (row>=m_values.size()) break;
        const auto found=arg_conv->m_data->find(path);
        if (found && *found) (*found)->set_text(m_values[row]);
        ++row;
    }
    const auto gameover=arg_conv->m_gameover.find("01_GameOver");
    if (gameover!=arg_conv->m_gameover.end() && gameover->second) {
        const auto talk=gameover->second->find("Events/Event//Talk");
        if (talk && *talk) {
            if (row<m_values.size()) (*talk)->find_exists("Text")->set_text(m_values[row++]);
            for (const auto* path:{"Contents/Post[1]","Contents/Post[2]","Contents/Post[4]"}) {
                if (row>=m_values.size()) break;
                (*talk)->find_exists(path)->set("name",m_values[row++]);
            }
        }
    }
}

void SkinMessagePanel::set_cell(Integer arg_row, String arg_value)
{
    if (arg_row<0) return;
    if (static_cast<std::size_t>(arg_row)>=m_values.size()) m_values.resize(static_cast<std::size_t>(arg_row)+1);
    m_values[static_cast<std::size_t>(arg_row)]=std::move(arg_value);
}

String SkinMessagePanel::get_cell(Integer arg_row) const
{
    return arg_row<0 || static_cast<std::size_t>(arg_row)>=m_values.size() ? String{} : m_values[static_cast<std::size_t>(arg_row)];
}

void SkinMessagePanel::_do_layout()
{
}

void SkinSoundPanel::initialize(std::shared_ptr<::cw::lib::Panel> arg_parent, std::shared_ptr<::cw::skin::convert::Converter> arg_conv)
{
    (void)arg_parent;
    g_active_sound=this;
    m_row_labels.clear();
    const auto base=::cw::data::xml2etree("Data/SkinBase/Skin.xml","",std::nullopt,std::nullopt,true);
    if (base) for (const auto& sound:children(base,"Sounds")) m_row_labels.push_back(sound->get_text());
    set_values(std::move(arg_conv));
    _do_layout();
}

void SkinSoundPanel::set_values(std::shared_ptr<::cw::skin::convert::Converter> arg_conv)
{
    m_values.clear();
    if (!arg_conv || !arg_conv->m_data) {
        m_values.resize(m_row_labels.size());
        return;
    }
    for (const auto& sound:children(arg_conv->m_data,"Sounds")) m_values.push_back(sound->get_text());
    if (m_values.size()<m_row_labels.size()) m_values.resize(m_row_labels.size());
    m_valid=true;
}

void SkinSoundPanel::get_values(std::shared_ptr<::cw::skin::convert::Converter> arg_conv)
{
    m_valid=true;
    if (!arg_conv || !arg_conv->m_data) return;
    const auto sounds=children(arg_conv->m_data,"Sounds");
    for (std::size_t i=0;i<sounds.size() && i<m_values.size();++i) sounds[i]->set_text(m_values[i]);
}

void SkinSoundPanel::set_cell(Integer arg_row, String arg_value)
{
    if (arg_row<0) return;
    if (static_cast<std::size_t>(arg_row)>=m_values.size()) m_values.resize(static_cast<std::size_t>(arg_row)+1);
    m_values[static_cast<std::size_t>(arg_row)]=std::move(arg_value);
}

String SkinSoundPanel::get_cell(Integer arg_row) const
{
    return arg_row<0 || static_cast<std::size_t>(arg_row)>=m_values.size() ? String{} : m_values[static_cast<std::size_t>(arg_row)];
}

void SkinSoundPanel::_do_layout()
{
}

std::shared_ptr<::cw::setting::LocalSetting> SkinConversionDialog___init___get_local()
{
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_local)
        return ::cw::g_cwpy->m_setting->m_local;
    return new_local();
}

std::shared_ptr<::cw::setting::LocalSetting> SkinConversionDialog___init___get_local_2()
{
    return new_local();
}

void SkinConversionDialog_OnOk_progress()
{
    // 生成された旧クロージャーとの互換
    // 性のために残す空の入口。進捗表示
    // は実際の変換処理側で更新する。
}

void SkinEditDialog_OnOk_func(std::shared_ptr<::cw::setting::LocalSetting> arg_local, ::cw::lib::String arg_skinname, bool arg_vocation120, ::cw::lib::Integer arg_initialcash, ::cw::lib::String arg_skintype)
{
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting) {
        ::cw::g_cwpy->m_setting->m_skin_local=std::move(arg_local);
        ::cw::g_cwpy->m_setting->m_skinname=std::move(arg_skinname);
        ::cw::g_cwpy->m_setting->m_initialcash=arg_initialcash;
        // Python と同様、予約するクロージャーにはこのダイアログで確認した種別を保持する
        // 。 後でファイルを読み直すと失敗したり、別の選択スキンを読んだりする可能性がある。
        ::cw::g_cwpy->m_setting->m_skintype=std::move(arg_skintype);
        if (skin_host_services().update_titlebar) skin_host_services().update_titlebar();
        if (skin_host_services().update_vocation120) skin_host_services().update_vocation120(arg_vocation120);
    }
}

void SkinEditDialog_OnOk_update_skin(::cw::lib::String arg_skindirname)
{
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting &&
        ::cw::g_cwpy->m_setting->m_skindirname==arg_skindirname) {
        // 保存先の XML は OnOk で書き込み済みである。 実際のスプライト
        // ・フォントリソースの再読み込みは、ネイティブの UI アダプターが行う。
        ::cw::g_cwpy->m_setting->m_skindir=
            ::cw::lib::encoding::path_to_utf8(std::filesystem::u8path("Data/Skin")/std::filesystem::u8path(arg_skindirname));
    }
    if (skin_host_services().reload_skin) skin_host_services().reload_skin(arg_skindirname);
}

void SkinEditDialog_OnImportResource_import_res(::cw::lib::String arg_dpath, ::cw::lib::String arg_fname, ::cw::lib::String arg_ext, ::cw::lib::String arg_newresname)
{
    import_resource_file(std::filesystem::u8path(arg_dpath)/std::filesystem::u8path(arg_fname),
                         std::filesystem::u8path(arg_dpath),arg_newresname,arg_ext);
}

void SkinBasePanel__do_layout_add_base(std::shared_ptr<::cw::lib::Control> arg_ctrl, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos)
{
    (void)arg_ctrl;
    (void)arg_pos;
}

void SkinInfoPanel__do_layout_add_info(std::shared_ptr<::cw::lib::Control> arg_ctrl, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos, ::cw::lib::Integer arg_colspan, ::cw::lib::Integer arg_rowspan, bool arg_expand, bool arg_growable)
{
    (void)arg_ctrl;
    (void)arg_pos;
    (void)arg_colspan;
    (void)arg_rowspan;
    (void)arg_expand;
    (void)arg_growable;
}

::cw::lib::Integer SkinFeaturePanel_set_values_set_rowdata(std::shared_ptr<::cw::data::CWPyElement> arg_data, ::cw::lib::Integer arg_row)
{
    (void)arg_data;
    return arg_row+1;
}

::cw::lib::Integer SkinFeaturePanel_get_values_get_rowdata(std::shared_ptr<::cw::data::CWPyElement> arg_data, ::cw::lib::Integer arg_row)
{
    (void)arg_data;
    return arg_row+1;
}

::cw::lib::Integer SkinCardPanel___init___put_areacards(std::map<::cw::lib::String, std::shared_ptr<::cw::data::CWPyElementTree>> arg_table, ::cw::lib::Integer arg_row)
{
    Integer row=arg_row;
    for (const auto& key:sorted_keys(arg_table)) if (arg_table[key]) row+=static_cast<Integer>(children(arg_table[key],"MenuCards").size());
    return row;
}

::cw::lib::Integer SkinCardPanel_set_values_put_areacards(std::map<::cw::lib::String, std::shared_ptr<::cw::data::CWPyElementTree>> arg_table, ::cw::lib::Integer arg_row)
{
    return SkinCardPanel___init___put_areacards(std::move(arg_table),arg_row);
}

::cw::lib::Integer SkinCardPanel_get_values_get_areacards(std::map<::cw::lib::String, std::shared_ptr<::cw::data::CWPyElementTree>> arg_table, ::cw::lib::Integer arg_row)
{
    return SkinCardPanel___init___put_areacards(std::move(arg_table),arg_row);
}

void main()
{
}

}
