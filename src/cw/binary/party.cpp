#include "cw/binary/party.hpp"

#include "cw/binary/adventurer.hpp"
#include "cw/binary/album.hpp"
#include "cw/binary/bgimage.hpp"
#include "cw/binary/beast.hpp"
#include "cw/binary/card_common.hpp"
#include "cw/binary/cwfile.hpp"
#include "cw/binary/item.hpp"
#include "cw/binary/skill.hpp"
#include "cw/binary/util.hpp"
#include "cw/cw.hpp"
#include "cw/data.hpp"
#include "cw/lib/encoding.hpp"
#include "cw/header.hpp"
#include "cw/setting.hpp"
#include "cw/thread.hpp"
#include "cw/util.hpp"
#include "cw/yadodb.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>

namespace cw::binary::party {
using B = ::cw::binary::base::CWBinaryBase;
using I = ::cw::lib::Integer;
using S = ::cw::lib::String;
using E = std::shared_ptr<::cw::data::CWPyElement>;

namespace {
std::optional<std::shared_ptr<B>> parent_from_value(const ::cw::lib::Value& v) { return ::cw::binary::card_common::parent_from_value(v); }
std::vector<S> split_lines(const S& text) {
    // Pythonの str.splitlines と同じ規則を共有し、Unicode
    // の改行と、末尾区切りの後に空行を増やさない動作を含める。
    return ::cw::lib::encoding::splitlines(text);
}
I parse_integer(const S& text) { return ::cw::binary::card_common::to_integer(text); }
I parse_money(const S& text) {
    // Python版と同様、XMLの任意精度整数を範囲内へ制限してからバイナリへ書く。
    return ::cw::lib::require_clamped_python_nonnegative_integer(text, 999999);
}
S stem(const S& path) {
    const auto filename = ::cw::lib::encoding::path_to_utf8(
        ::cw::lib::encoding::path_from_utf8(path).filename());
    return std::get<0>(::cw::util::splitext(filename));
}
void set_card_limit(const std::shared_ptr<B>& card,I value) { if(auto skill=std::dynamic_pointer_cast<::cw::binary::skill::SkillCard>(card)) skill->m_limit=value; else if(auto item=std::dynamic_pointer_cast<::cw::binary::item::ItemCard>(card)) item->m_limit=value; else if(auto beast=std::dynamic_pointer_cast<::cw::binary::beast::BeastCard>(card)) beast->m_limit=value; else throw std::invalid_argument("unknown backpack card type"); }
E root_from_value(const ::cw::lib::Value& value) { if(const auto* o=std::get_if<::cw::lib::ObjectPtr>(&value.storage())) { if(auto t=std::dynamic_pointer_cast<::cw::data::CWPyElementTree>(*o)) return t->getroot(); if(auto e=std::dynamic_pointer_cast<::cw::data::CWPyElement>(*o)) return e; } return nullptr; }
E find_element(const E& root,const S& path) { if(!root) return nullptr; const auto f=root->find(path); return f&&*f?*f:nullptr; }
S text_at(const E& root,const S& path,const S& fallback={}) { const auto e=find_element(root,path); return e?e->m_text:fallback; }
I int_at(const E& root,const S& path,I fallback) { const auto text=text_at(root,path); return text.empty()?fallback:parse_integer(text); }
S member_filename(const E& member) {
    return member ? ::cw::lib::encoding::path_to_utf8(
        ::cw::lib::encoding::path_from_utf8(member->m_fpath).filename()) : S{};
}
S absolute_path(const S& path) {
    auto result = ::cw::lib::encoding::path_to_utf8(
        std::filesystem::absolute(::cw::lib::encoding::path_from_utf8(path)));
#if defined(_WIN32)
    for (auto& character : result) if (character == '/') character = '\\';
#endif
    return result;
}
S join_path(const S& a,const S& b) {
    return ::cw::lib::encoding::path_to_utf8(
        ::cw::lib::encoding::path_from_utf8(a) /
        ::cw::lib::encoding::path_from_utf8(b));
}
S conversion_error(const S& party,const E& member,const S& suffix,const std::optional<S>& func=std::nullopt) {
    const S name=text_at(member,"Property/Name");
    // funcnameが空でも、UnsupportedErrorは一般的な変換失敗で
    // はなく未対応として扱う。 Python版と同様、その空の診断項目も保持する。
    return func ? party+" の "+name+suffix+" は対象エンジンで使用できない機能("+*func+")を使用しているため、変換しません。\n" : party+" の "+name+suffix+" は変換できませんでした。\n";
}
void report_conversion_error(const std::shared_ptr<::cw::binary::cwfile::CWFileWriter>& file,const S& party,const E& member,const S& suffix,const std::optional<S>& func=std::nullopt) {
    if(file)file->log_error(conversion_error(party,member,suffix,func));
}
E xml_root(const S& path) { const auto tree=::cw::data::xml2etree(path,"",std::nullopt,std::nullopt,true); return tree?tree->getroot():nullptr; }
std::vector<E> member_elements(const std::shared_ptr<::cw::data::Party>& party) { std::vector<E> r; if(!party) return r; for(const auto& v:party->m_members) if(auto e=root_from_value(v)) r.push_back(e); return r; }
}

void BackpackCard::initialize(std::shared_ptr<PartyMembers> parent,std::optional<std::shared_ptr<::cw::binary::cwfile::CWFile>> file,bool yadodata) { B::initialize(parent?std::optional<std::shared_ptr<B>>(parent):std::nullopt,file,yadodata); m_data.reset();m_fname.clear();m_uselimit=0;m_mine=false; if(file&&*file){m_fname=(*file)->rawstring();m_uselimit=(*file)->dword();m_mine=(*file)->boolean();} }
void BackpackCard::set_data(std::shared_ptr<B> data) { if(!data) throw std::invalid_argument("BackpackCard data is null"); m_data=std::move(data); }
void BackpackCard::set_data(::cw::lib::Value data) { if(const auto*o=std::get_if<::cw::lib::ObjectPtr>(&data.storage())){set_data(std::dynamic_pointer_cast<B>(*o));return;} throw std::invalid_argument("BackpackCard data is not a binary card"); }
E BackpackCard::get_data() { if(!m_data) throw std::logic_error("BackpackCard has no card data"); return m_data->get_data(); }
S BackpackCard::create_xml(S dpath) { if(!m_data) throw std::logic_error("BackpackCard has no card data"); set_card_limit(m_data,m_uselimit); if(!m_mine)m_data->set_image_export(false,true); const auto data=m_data->get_data(); if(!m_mine)data->set("scenariocard","True"); return m_data->create_xml(std::move(dpath)); }
void BackpackCard::unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> file,E data,S fname,bool mine) { if(!file||!data) throw std::invalid_argument("BackpackCard::unconv null argument"); file->write_rawstring(std::get<0>(::cw::util::splitext(fname)));file->write_dword(data->getint("Property/UseLimit",std::nullopt,I{0}));file->write_bool(mine); }

void Party::initialize(::cw::lib::Value parent,std::shared_ptr<::cw::binary::cwfile::CWFile> file,bool yadodata,::cw::lib::PythonInteger dataversion) { if(!file) throw std::invalid_argument("Party file is null"); B::initialize(parent_from_value(parent),file,yadodata);m_xmltype="Party";m_type=2;m_fname=get_fname();m_memberslist.clear(); if(dataversion>=10){(void)file->word();(void)file->string();(void)file->image();for(const auto&member : ::cw::util::decodetextlist(file->string(true)))if(!member.empty())m_memberslist.push_back(::cw::binary::util::check_filename(member));m_name=file->string();m_money=file->dword();m_nowadventuring=file->boolean();}else{for(const auto&member : ::cw::util::decodetextlist(file->string(true)))if(!member.empty())m_memberslist.push_back(::cw::binary::util::check_filename(member));(void)file->string();(void)file->string();(void)file->image();m_name.clear();m_money=0;m_nowadventuring=file->boolean();}m_cards.clear();m_errorcards.clear();m_data.reset(); }
// Party はローカルの直列化が成功してから m_data
// を公開する。 Python版の早い self.data
// 代入とは異なり、不完全なツリーを次回用にキャッシュしない。
E Party::get_data() { if(m_data&&*m_data)return *m_data;auto data=::cw::data::make_element("Party"),property=::cw::data::make_element("Property");property->append(::cw::data::make_element("Name",m_name));property->append(::cw::data::make_element("Money",std::to_string(m_money)));property->append(::cw::data::make_element("Members"));data->append(property);m_data=data;return data; }
S Party::create_xml(S path) { const S basedir=path,output=B::create_xml(path);const auto root=get_root();if(!root)throw std::logic_error("Party has no root");if(root->m_yadodb&&*root->m_yadodb)(*root->m_yadodb)->insert_party(output,false,-1);const S carddir=::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(output).parent_path());auto carddb=std::make_shared<::cw::yadodb::YadoDB>();carddb->initialize(carddir,::cw::yadodb::g_PARTY);m_errorcards.clear();I order=0;for(const auto&card:m_cards){if(!card||!card->m_data){if(card)m_errorcards.push_back(card);continue;}{card->m_data->m_materialbasedir=basedir;carddb->insert_card(card->create_xml(carddir),false,order++);}}carddb->commit();carddb->close();return output; }
// ネイティブ版は画像の検索・復号前にPartyのXMLを検証するが、Python
// 版は先に画像を取り込む。 XML値が有効になるまで素材処理を遅らせ
// るため、失敗順序は異なる。 画像の取り込みはネイティブの出力先頭
// 部分の後なので、この境界は出力全体を一括で確定するものではない。
void Party::unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> file,E data,S yadoname,std::map<S,S> member_table,S scenarioname) { if(!file||!data)throw std::invalid_argument("Party::unconv null argument");const bool now=!scenarioname.empty();if(now)yadoname=scenarioname;S name,members;I money=0;for(const auto&e:data->m_children)if(e&&e->m_tag=="Property")for(const auto&p:e->m_children)if(p){if(p->m_tag=="Name")name=p->m_text;else if(p->m_tag=="Money")money=parse_money(p->m_text);else if(p->m_tag=="Members"){std::vector<S>seq;for(const auto&m:p->m_children)if(m&&m->m_tag=="Member"&&!m->m_text.empty()){const auto i=member_table.find(m->m_text);if(i!=member_table.end())seq.push_back(i->second);}members=::cw::util::encodetextlist(seq);}}S skin;I ext=::cw::g_M_IMG;if(::cw::g_cwpy){if(::cw::g_cwpy->m_setting)skin=::cw::g_cwpy->m_setting->m_skindir;else if(const auto*v=std::get_if<S>(&::cw::g_cwpy->m_skindir.storage()))skin=*v;if(::cw::g_cwpy->m_rsrc)ext=::cw::g_cwpy->m_rsrc->m_ext_img;}const auto requested=::cw::util::join_paths({::cw::lib::Value(skin),::cw::lib::Value("Resource/Image/Card/COMMAND0")});const auto imagepath=::cw::util::find_resource(requested,ext);file->write_word(0);file->write_string(yadoname);file->write_image(B::import_image(file,imagepath,true,true,"TopLeft"));file->write_string(members,true);file->write_string(name);file->write_dword(money);file->write_bool(now); }

// ネイティブの戻り型は固定され、この互換入口は常に整数値を返す。 PartyMembers の
// 初期化ではフラグに split_variables_bool を使い、 型付きの変換でPython
// の split_variables(text, step=False) に合わせる。
std::map<S,I> PartyMembers::split_variables(S text,bool){std::map<S,I>r;for(const auto&line:split_lines(text)){const auto p=line.rfind('=');if(p!=S::npos)r[line.substr(0,p)]=parse_integer(line.substr(p+1));}return r;}
std::map<S,bool> PartyMembers::split_variables_bool(S text){std::map<S,bool>r;for(const auto&line:split_lines(text)){const auto p=line.rfind('=');if(p!=S::npos)r[line.substr(0,p)]=parse_integer(line.substr(p+1))!=0;}return r;}
std::vector<I> PartyMembers::split_ids(S text){std::vector<I>r;for(const auto&line:split_lines(text))if(!line.empty())r.push_back(parse_integer(line));return r;}

// 旧形式の相対シナリオパスは、Python版と同様に元ファイルの三階層上を基
// 準にする。 ネイティブはファイルシステムのパス連結とUTF-8変換を使い、
// Pythonの join_paths の文字列連結・末尾除去とは空値・
// 末尾区切りの扱いを区別する。 バージョン10のパスは保存されたまま使う。
void PartyMembers::initialize(::cw::lib::Value parent,std::shared_ptr<::cw::binary::cwfile::CWFile> file,bool yadodata,::cw::lib::PythonInteger dataversion) { if(!file)throw std::invalid_argument("PartyMembers file is null");B::initialize(parent_from_value(parent),file,yadodata);m_xmltype="PartyMembers";m_type=3;m_fname=get_fname();m_adventurers.clear();m_vanisheds.clear();m_cards.clear();m_bgimgs.clear();m_steps.clear();m_flags.clear();m_friendcards.clear();m_infocards.clear();m_summary.reset();I advnum=file->byte()-(dataversion>=10?30:10);for(int i=0;i<4;++i)(void)file->byte();auto self=std::static_pointer_cast<B>(shared_from_this());I vannum=0;for(I i=0;i<advnum;++i){auto a=std::make_shared<::cw::binary::adventurer::AdventurerWithImage>();a->initialize(self,file,false);m_adventurers.push_back(a);if(dataversion>=10)vannum=file->byte();else(void)file->byte();}if(vannum>0){(void)file->dword();for(I i=0;i<vannum;++i){auto a=std::make_shared<::cw::binary::adventurer::AdventurerWithImage>();a->initialize(self,file,false);m_vanisheds.push_back(a);if(i+1<vannum)(void)file->byte();}std::reverse(m_vanisheds.begin(),m_vanisheds.end());}else for(int i=0;i<3;++i)(void)file->byte();auto me=std::static_pointer_cast<PartyMembers>(shared_from_this());if(dataversion>=10){m_name=file->string();const I n=file->dword();for(I i=0;i<n;++i){auto c=std::make_shared<BackpackCard>();c->initialize(me,file,false);m_cards.push_back(c);}}else{file->seek(-4,1);m_name=m_adventurers.at(0)->m_adventurer->m_name+"一行";const I n=file->dword();for(I i=0;i<n;++i){const I t=file->byte();std::shared_ptr<B>d;if(t==1){auto c=std::make_shared<::cw::binary::skill::SkillCard>();c->initialize(std::nullopt,file,true);d=c;}else if(t==2){auto c=std::make_shared<::cw::binary::item::ItemCard>();c->initialize(std::nullopt,file,true);d=c;}else if(t==3){auto c=std::make_shared<::cw::binary::beast::BeastCard>();c->initialize(std::nullopt,file,true);d=c;}else throw std::invalid_argument(m_fname);auto c=std::make_shared<BackpackCard>();c->initialize(me,std::nullopt,false);c->m_fname=d->m_name;if(t==2) c->m_uselimit=std::dynamic_pointer_cast<::cw::binary::item::ItemCard>(d)->m_limit;else if(t==3)c->m_uselimit=std::dynamic_pointer_cast<::cw::binary::beast::BeastCard>(d)->m_limit;c->m_mine=true;c->set_data(d);m_cards.push_back(c);}}m_wpl.reset();if(dataversion>=10){m_money=file->dword();m_money_beforeadventure=file->dword();m_nowadventuring=file->boolean();if(m_nowadventuring){(void)file->word();m_scenariopath=file->rawstring();m_areaid=file->dword();m_steps=split_variables(file->rawstring(),true);m_flags=split_variables_bool(file->rawstring());m_friendcards=split_ids(file->rawstring());m_infocards=split_ids(file->rawstring());m_music=file->rawstring();const I n=file->dword();for(I i=0;i<n;++i){auto b=std::make_shared<::cw::binary::bgimage::BgImage>();b->initialize(::cw::binary::card_common::self_value(*this),file,false);m_bgimgs.push_back(b);}}}else{m_money=0;for(const auto&a:m_adventurers){m_money+=a->m_adventurer->m_money;a->m_adventurer->m_money=0;}m_money_beforeadventure=m_money;m_nowadventuring=file->boolean();if(m_nowadventuring){m_scenariopath.clear();m_summary=std::make_shared<::cw::binary::summary::Summary>();m_summary->initialize({},file,true,false,"Material",true,true);for(const auto&s:m_summary->m_steps)if(s)m_steps[s->m_name]=s->m_py_default;for(const auto&f:m_summary->m_flags)if(f)m_flags[f->m_name]=f->m_py_default;m_scenariopath=file->rawstring();if(!std::filesystem::u8path(m_scenariopath).is_absolute()){const auto d=std::filesystem::u8path(file->m_filename).parent_path().parent_path().parent_path();m_scenariopath=::cw::lib::encoding::path_to_utf8(d/std::filesystem::u8path(m_scenariopath));}m_areaid=file->dword();const I fn=file->dword();for(I i=0;i<fn;++i)m_friendcards.push_back(file->dword());const I in=file->dword();for(I i=0;i<in;++i)m_infocards.push_back(file->dword());m_music=file->rawstring();const I n=file->dword();for(I i=0;i<n;++i){auto b=std::make_shared<::cw::binary::bgimage::BgImage>();b->initialize(::cw::binary::card_common::self_value(*this),file,false);m_bgimgs.push_back(b);}}}}

S PartyMembers::create_xml(S dpath){if(!m_wpl||!*m_wpl)throw std::logic_error("PartyMembers has no Party");const auto data=(*m_wpl)->get_data(),members=data->find_exists("Property/Members");for(const auto&a:m_adventurers){const auto path=a->create_xml(dpath);members->append(::cw::data::make_element("Member",stem(path)));}return (*m_wpl)->m_xmlpath;}
void PartyMembers::create_vanisheds_xml(S dpath){for(const auto&a:m_vanisheds){const auto data=a->get_data();data->find_exists("Property")->set("lost","True");a->create_xml(dpath);}}
S PartyMembers::join_variables(std::vector<E> data)
{
    S result;
    for (const auto& element : data) {
        if (!element) throw std::invalid_argument("join_variables requires an element");
        // Pythonの CWPyElement.get と同様、ElementTree.get とは異なり、既定値は空文字列。
        S text = element->get("value", "");
        if (text == "True") text = "1";
        else if (text == "False") text = "0";
        result += element->m_text + "=" + text + "\r\n";
    }
    return result;
}
S PartyMembers::join_ids(std::vector<E> data)
{
    S result;
    for (const auto& element : data) {
        if (!element) throw std::invalid_argument("join_ids requires an element");
        // Python版と同様、空の項目を保持し、CastCardには Property/Id を必須とする。
        const auto text = element->m_tag == "CastCard"
            ? element->find_exists("Property/Id")->m_text : element->m_text;
        result += text + "\r\n";
    }
    return result;
}

void PartyMembers::unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> file,std::shared_ptr<::cw::data::Party> party,std::map<S,std::tuple<S,E>> yadocards,S logdir){if(!file||!party)throw std::invalid_argument("PartyMembers::unconv null argument");const auto adventurers=member_elements(party);std::vector<E>vanisheds;I moneybefore=0,areaid=0;bool now=false;S scenariopath,music,steps,flags,friendcards,infocards;E bgimgs;const S name=party->m_name;if (!logdir.empty()) {
    const auto log = xml_root(join_path(logdir, "ScenarioLog.xml"));
    const auto saved_party = xml_root(join_path(logdir, "Party/Party.xml"));
    if (!log || !saved_party) throw std::invalid_argument("PartyMembers::unconv requires scenario log XML");
    const auto saved_money = saved_party->find("Property/Money");
    if (saved_money && *saved_money) {
        // XMLの整数はネイティブIntegerを超える場合があるが、Python版と同様、書き込
        // み前に範囲内へ制限する。 不正な文字列には通常の getint のXML診断を維持する。
        const auto clamped = ::cw::lib::clamp_python_nonnegative_integer((*saved_money)->m_text, 999999);
        if (clamped) moneybefore = *clamped;
        else moneybefore = std::clamp(saved_party->getint("Property/Money", ::cw::lib::Value(party->m_money), std::nullopt), I{0}, I{999999});
    } else {
        moneybefore = std::clamp(party->m_money, I{0}, I{999999});
    }
    now = true;
    scenariopath = log->gettext("Property/WsnPath", S{});
    areaid = log->getint("Property/AreaId", ::cw::lib::Value(I{0}), std::nullopt);
    // Pythonの getfind と同様、一覧が空でもこれらのコンテナーを
    // 必須とする。 データを省略するのではなく、出力前に不正なログを拒否する。
    steps = join_variables(log->find_exists("Steps")->m_children);
    flags = join_variables(log->find_exists("Flags")->m_children);
    friendcards = join_ids(log->find_exists("CastCards")->m_children);
    infocards = join_ids(log->find_exists("InfoCards")->m_children);
    music = log->gettext("Property/MusicPath", S{});
    if (music.empty()) music = log->gettext("Property/MusicPaths/MusicPath", S{});
    bgimgs = find_element(log, "BgImages");
    for (const auto& lost : log->find_exists("LostAdventurers")->m_children) {
        if (!lost) throw std::invalid_argument("LostAdventurers requires an element");
        vanisheds.push_back(xml_root(::cw::util::join_yadodir(lost->m_text)));
    }
    std::reverse(vanisheds.begin(), vanisheds.end());
}
const auto advpos=file->tell();file->write_byte(static_cast<I>(adventurers.size())+30);file->write_byte(0);file->write_byte(0);file->write_byte(0);file->write_byte(5);I advnum=0;std::vector<S> errorlog;for(std::size_t i=0;i<adventurers.size();++i){const auto member=adventurers[i];E logdata;if(!logdir.empty())logdata=xml_root(join_path(join_path(logdir,"Members"),member_filename(member)));const auto pos=file->tell();try{::cw::binary::adventurer::AdventurerWithImage::unconv(file,member,logdata);if(i+1<adventurers.size())file->write_byte(0);++advnum;}catch(const ::cw::binary::cwfile::UnsupportedError&ex){file->seek(pos);errorlog.push_back(conversion_error(name,member,"",ex.m_funcname));}catch(...){::cw::util::print_ex(std::nullopt);file->seek(pos);errorlog.push_back(conversion_error(name,member,""));}}if(advnum==0)throw ::cw::binary::cwfile::UnsupportedError(name+" は全メンバが変換に失敗したため、変換しません。\n");for(const auto& message:errorlog)file->log_error(message);auto end=file->tell();file->seek(advpos);file->write_byte(advnum+30);file->seek(end);const auto vanpos=file->tell();file->write_byte(static_cast<I>(vanisheds.size()));I vannum=0;if(!vanisheds.empty()){file->write_dword(0);for(std::size_t i=0;i<vanisheds.size();++i){const auto member=vanisheds[i];E logdata;if(!logdir.empty())logdata=xml_root(join_path(join_path(logdir,"Members"),member_filename(member)));const auto pos=file->tell();try{::cw::binary::adventurer::AdventurerWithImage::unconv(file,member,logdata);if(i+1<vanisheds.size())file->write_byte(0);++vannum;}catch(const ::cw::binary::cwfile::UnsupportedError&ex){file->seek(pos);report_conversion_error(file,name,member,"(消去前データ)",ex.m_funcname);}catch(...){::cw::util::print_ex(std::nullopt);file->seek(pos);report_conversion_error(file,name,member,"(消去前データ)");}}end=file->tell();file->seek(vanpos);file->write_byte(vannum);file->seek(end);}else{file->write_byte(0);file->write_byte(0);file->write_byte(0);}file->write_string(name);const auto cardpos=file->tell();file->write_dword(static_cast<I>(party->m_backpack.size()));I cardnum=0;for(const auto&header:party->m_backpack){if(!header)continue;const auto it=yadocards.find(header->m_fpath);if(it==yadocards.end())continue;const auto&[path,data]=it->second;const bool scenariocard=data&&::cw::util::str2bool(data->get("scenariocard","False"));BackpackCard::unconv(file,data,path,!scenariocard);++cardnum;}end=file->tell();file->seek(cardpos);file->write_dword(cardnum);file->seek(end);file->write_dword(std::clamp(party->m_money,I{0},I{999999}));if(now){file->write_dword(moneybefore);file->write_bool(true);file->write_word(0);file->write_rawstring(absolute_path(scenariopath));file->write_dword(areaid);file->write_rawstring(steps);file->write_rawstring(flags);file->write_rawstring(friendcards);file->write_rawstring(infocards);file->write_rawstring(music);if(!bgimgs)file->write_dword(0);else{file->write_dword(static_cast<I>(bgimgs->m_children.size()));for(const auto&e:bgimgs->m_children)::cw::binary::bgimage::BgImage::unconv(file,e);}}else{file->write_dword(0);file->write_bool(false);}}

std::tuple<std::vector<std::shared_ptr<::cw::binary::adventurer::AdventurerCard>>,std::vector<std::shared_ptr<::cw::binary::album::Album>>> load_album120(::cw::lib::Value parent,std::shared_ptr<::cw::binary::cwfile::CWFile>file){if(!file)throw std::invalid_argument("album120 file is null");(void)file->dword();const I n=file->dword();std::vector<std::shared_ptr<::cw::binary::adventurer::AdventurerCard>>cards;std::vector<std::shared_ptr<::cw::binary::album::Album>>albums;const auto parent_object=parent_from_value(parent);for(I i=0;i<n;++i){auto card=std::make_shared<::cw::binary::adventurer::AdventurerCard>();card->initialize(parent_object?*parent_object:nullptr,nullptr,true);card->m_fname=file->m_filename;card->m_adventurer=std::make_shared<::cw::binary::adventurer::Adventurer>();card->m_adventurer->initialize(card,file,true,false,true);const auto&a=card->m_adventurer;if(!a->m_is_dead){cards.push_back(card);continue;}auto album=std::make_shared<::cw::binary::album::Album>();album->initialize(parent,std::nullopt,true);album->m_name=a->m_name;album->m_image=a->m_image;album->m_level=a->m_level;album->m_dex=a->m_dex;album->m_agl=a->m_agl;album->m_int=a->m_int;album->m_str=a->m_str;album->m_vit=a->m_vit;album->m_min=a->m_min;album->m_aggressive=a->m_aggressive;album->m_cheerful=a->m_cheerful;album->m_brave=a->m_brave;album->m_cautious=a->m_cautious;album->m_trickish=a->m_trickish;album->m_avoid=a->m_avoid;album->m_resist=a->m_resist;album->m_defense=a->m_defense;album->m_description=a->m_description;album->m_coupons=a->m_coupons;albums.push_back(album);}return{std::move(cards),std::move(albums)};}
void main(){}
}
