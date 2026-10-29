#include "cw/binary/cwyado.hpp"
#include "cw/binary/cardclone.hpp"
#include "cw/binary/adventurer.hpp"
#include "cw/binary/album.hpp"
#include "cw/binary/beast.hpp"
#include "cw/binary/cwfile.hpp"
#include "cw/binary/environment.hpp"
#include "cw/binary/item.hpp"
#include "cw/binary/party.hpp"
#include "cw/binary/bgimage.hpp"
#include "cw/binary/skill.hpp"
#include "cw/binary/util.hpp"
#include "cw/yadodb.hpp"
#include "cw/util.hpp"
#include "cw/cw.hpp"
#include "cw/data.hpp"
#include "cw/character.hpp"
#include "cw/header.hpp"
#include "cw/lib/encoding.hpp"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <system_error>
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace cw::binary::cwyado {
namespace {
using S=::cw::lib::String; using I=::cw::lib::Integer; using B=::cw::binary::base::CWBinaryBase;
S lower(S s){std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return char(std::tolower(c));});return s;}
S p8(const std::filesystem::path& p){return ::cw::lib::encoding::path_to_utf8(p);}
S name(const S&p){return p8(std::filesystem::u8path(p).filename());}
S stem(const S&p){return p8(std::filesystem::u8path(p).stem());}
S ext(const S&p){auto e=lower(p8(std::filesystem::u8path(p).extension()));return !e.empty()&&e[0]=='.'?e.substr(1):e;}
S join(const S&a,const S&b){return p8(std::filesystem::u8path(a)/std::filesystem::u8path(b));}
void copy_one(const S&src,const S&dst){std::filesystem::copy(std::filesystem::u8path(src),std::filesystem::u8path(dst),std::filesystem::copy_options::recursive);}
void restore_copy_access(const S& destination)
{
    const auto path = std::filesystem::u8path(destination);
#if defined(_WIN32)
    // WindowsのCPythonの access() は実行権限を無視する。
    const bool accessible = ::_waccess(path.c_str(), 6) == 0;
#else
    const bool accessible = ::access(path.c_str(), R_OK | W_OK | X_OK) == 0;
#endif
    if (!accessible)
        std::filesystem::permissions(path,
            std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
            std::filesystem::perm_options::replace);
}
S value_string(const ::cw::lib::Value&v){
 if(auto p=std::get_if<S>(&v.storage()))return *p;
 if(auto p=std::get_if<I>(&v.storage()))return std::to_string(*p);
 if(auto p=std::get_if<::cw::lib::Real>(&v.storage()))return std::to_string(*p);
 if(auto p=std::get_if<bool>(&v.storage()))return *p?"True":"False";
 return {};
}
std::shared_ptr<::cw::data::CWPyElement> element_from_value(const ::cw::lib::Value&v){
 if(auto p=std::get_if<::cw::lib::ObjectPtr>(&v.storage())){
  if(!*p)return {};
  if(auto e=std::dynamic_pointer_cast<::cw::data::CWPyElement>(*p))return e;
  if(auto t=std::dynamic_pointer_cast<::cw::data::CWPyElementTree>(*p))return t->getroot();
 }
 return {};
}
S gtempdir(){return value_string(::cw::g_tempdir);}
S relnorm(const S&p,const S&base){
 try{return p8(std::filesystem::relative(std::filesystem::u8path(p),std::filesystem::u8path(base)));}
 catch(...){return p;}
}
void remove_path(const S&p){if(p.empty())return;try{::cw::util::remove(p,false);}catch(...){std::error_code ec;std::filesystem::remove_all(std::filesystem::u8path(p),ec);}}
S header_path(const std::shared_ptr<::cw::header::AdventurerHeader>&h){return h?value_string(h->m_fpath):S{};}
S card_path(const std::shared_ptr<::cw::header::CardHeader>&h){return h?h->m_fpath:S{};}
S write_xml(const std::shared_ptr<::cw::data::CWPyElement>&root,const S&path){
 if(!root)throw std::invalid_argument("XML root is null");
 auto tree=std::make_shared<::cw::data::CWPyElementTree>();tree->initialize(path,root);tree->write_file(path);return path;
}
S bool_text(bool v){return v?"True":"False";}
I int_text(const S&s,I d=0){try{std::size_t p=0;auto x=std::stoll(s,&p);if(p!=s.size())return d;return static_cast<I>(x);}catch(...){return d;}}
S absolute_or_join(const S&base,const S&p){if(std::filesystem::u8path(p).is_absolute())return p;return join(base,p);}
std::tuple<std::vector<std::shared_ptr<::cw::binary::adventurer::AdventurerCard>>,std::vector<std::shared_ptr<::cw::binary::album::Album>>> load_album120_local(const std::shared_ptr<::cw::binary::cwfile::CWFile>&f){
 if(!f)throw std::invalid_argument("album120 file");(void)f->dword();const I count=f->dword();std::vector<std::shared_ptr<::cw::binary::adventurer::AdventurerCard>>cards;std::vector<std::shared_ptr<::cw::binary::album::Album>>albums;
 for(I i=0;i<count;++i){auto card=std::make_shared<::cw::binary::adventurer::AdventurerCard>();card->initialize(nullptr,nullptr,true);card->m_fname=f->m_filename;auto adv=std::make_shared<::cw::binary::adventurer::Adventurer>();adv->initialize(std::static_pointer_cast<B>(card),f,true,false,true);card->m_adventurer=adv;if(!adv->m_is_dead){cards.push_back(card);continue;}auto album=std::make_shared<::cw::binary::album::Album>();album->initialize({},std::nullopt,true);album->m_name=adv->m_name;album->m_image=adv->m_image;album->m_level=adv->m_level;album->m_dex=adv->m_dex;album->m_agl=adv->m_agl;album->m_int=adv->m_int;album->m_str=adv->m_str;album->m_vit=adv->m_vit;album->m_min=adv->m_min;album->m_aggressive=adv->m_aggressive;album->m_cheerful=adv->m_cheerful;album->m_brave=adv->m_brave;album->m_cautious=adv->m_cautious;album->m_trickish=adv->m_trickish;album->m_avoid=adv->m_avoid;album->m_resist=adv->m_resist;album->m_defense=adv->m_defense;album->m_description=adv->m_description;album->m_coupons=adv->m_coupons;albums.push_back(album);}
 return {cards,albums};
}
}

void CWYado::initialize(S path,S dst,S skin){m_name=name(path);m_path=std::move(path);m_dir=::cw::binary::util::check_duplicate(join(dst,m_name));m_skintype=std::move(skin);m_message.clear();m_curnum=0;m_maxnum=1;m_datalist.clear();m_wyd.reset();m_wchs.clear();m_wcps.clear();m_wrms.clear();m_wpls.clear();m_wpts.clear();m_nowadventuringparties.clear();m_errorlog.clear();m_yadofiles.clear();m_cardfiles.clear();m_otherfiles.clear();m_otherdirs.clear();m_environmentpath.clear();
 const std::set<S> y={"wch","wcp","wpl","wpt","wrm","whs"},c={"wsm","wid","wcl"},ignore={"wck","wci","wcb"};
 for(const auto&e:std::filesystem::directory_iterator(std::filesystem::u8path(m_path))){auto p=p8(e.path()),n=p8(e.path().filename());if(e.is_regular_file()){auto x=ext(n);if(n=="Environment.wyd"&&m_environmentpath.empty()){m_environmentpath=p;m_yadofiles.push_back(p);}else if(y.count(x))m_yadofiles.push_back(p);else if(c.count(x))m_cardfiles.push_back(p);else if(!ignore.count(x))m_otherfiles.push_back(p);}else m_otherdirs.push_back(p);}}
void CWYado::write_errorlog(S s){m_errorlog+=s+"\n";}
bool CWYado::is_convertible(){if(m_environmentpath.empty())return false;try{auto d=load_yadofile(m_environmentpath);auto e=std::dynamic_pointer_cast<::cw::binary::environment::Environment>(d);if(!e)return false;m_wyd.reset();if(e->m_dataversion_int==8||e->m_dataversion_int==10||e->m_dataversion_int==11){/* Python版が代入するのは対応バージョンだけなので、ここでの狭い型への変換は正確。 */m_dataversion_int=e->m_dataversion_int.to_integer();return true;}return false;}catch(...){::cw::util::print_ex(std::nullopt);return false;}}

std::shared_ptr<B> CWYado::load_yadofile(S path){auto f=std::make_shared<::cw::binary::cwfile::CWFile>(path,"rb");std::shared_ptr<B>d;auto x=ext(path);
 if(x=="wyd"){auto v=std::make_shared<::cw::binary::environment::Environment>();v->initialize({},f,true,false);v->m_skintype=m_skintype;m_wyd=v;d=v;}
 else if(x=="wch"){auto v=std::make_shared<::cw::binary::adventurer::AdventurerHeader>();v->initialize(nullptr,f,true,m_dataversion_int);m_wchs.push_back(v);d=v;}
 else if(x=="wcp"){auto v=std::make_shared<::cw::binary::adventurer::AdventurerCard>();v->initialize(nullptr,f,true);m_wcps.push_back(v);d=v;}
 else if(x=="wrm"){auto v=std::make_shared<::cw::binary::album::Album>();v->initialize({},f,true);m_wrms.push_back(v);d=v;}
 else if(x=="wpl"){auto v=std::make_shared<::cw::binary::party::Party>();v->initialize({},f,true,m_dataversion_int);m_wpls.push_back(v);d=v;}
 else if(x=="wpt"){auto v=std::make_shared<::cw::binary::party::PartyMembers>();v->initialize({},f,true,m_dataversion_int);m_wpts.push_back(v);d=v;}
 else if(x=="whs"){auto [cards,albums]=::cw::binary::party::load_album120({},f);m_wcps.insert(m_wcps.end(),cards.begin(),cards.end());m_wrms.insert(m_wrms.end(),albums.begin(),albums.end());}
 else throw std::invalid_argument(path);f->close();return d;}

std::shared_ptr<B> CWYado::load_cardfile(S path,const std::map<S,I>&types){auto f=std::make_shared<::cw::binary::cwfile::CWFile>(path,"rb");std::shared_ptr<B>d;auto fn=name(path);
 auto make=[&](I t,std::shared_ptr<::cw::binary::cwfile::CWFile>in){if(t==1){auto v=std::make_shared<::cw::binary::skill::SkillCard>();v->initialize({},in,true);return std::static_pointer_cast<B>(v);}if(t==2){auto v=std::make_shared<::cw::binary::item::ItemCard>();v->initialize({},in,true);return std::static_pointer_cast<B>(v);}if(t==3){auto v=std::make_shared<::cw::binary::beast::BeastCard>();v->initialize({},in,true);return std::static_pointer_cast<B>(v);}throw std::invalid_argument(path);};
 if(ext(fn)=="wcl"){auto base=std::filesystem::u8path(path);base.replace_extension();for(const auto&pair:std::vector<std::pair<S,I>>{{".wck",1},{".wci",2},{".wcb",3}}){auto cp=p8(base)+pair.first;if(!std::filesystem::is_regular_file(std::filesystem::u8path(cp)))continue;auto f2=std::make_shared<::cw::binary::cwfile::CWFile>(cp,"rb");d=make(pair.second,f2);f2->close();(void)f->string();(void)f->string();d->m_image=f->image();if(auto v=std::dynamic_pointer_cast<::cw::binary::skill::SkillCard>(d))v->m_fname=name(cp);else if(auto v=std::dynamic_pointer_cast<::cw::binary::item::ItemCard>(d))v->m_fname=name(cp);else if(auto v=std::dynamic_pointer_cast<::cw::binary::beast::BeastCard>(d))v->m_fname=name(cp);break;}if(!d)throw std::invalid_argument(path);}
 else{auto it=types.find(stem(fn));if(it==types.end())throw std::invalid_argument(path);d=make(it->second,f);}f->close();return d;}

// ネイティブの再読み込みでは、アルバムと冒険中パーティの関連も消す。 Python
// 版は共通の一覧・カウンターだけを戻し、この二つの構築時の集合は残す
// が、 再利用時の古い・重複した関連を防ぐため追加でリセットする。
void CWYado::load(){m_datalist.clear();m_wyd.reset();m_wchs.clear();m_wcps.clear();m_wpls.clear();m_wpts.clear();m_wrms.clear();m_nowadventuringparties.clear();m_curnum_n=0;m_curnum=0;m_maxnum=I(m_yadofiles.size()+m_cardfiles.size()+1);bool wchwarn=false,wptwarn=false;publish_progress();
 for(const auto&p:m_yadofiles){m_message=name(p)+" を読込中...";++m_curnum_n;m_curnum=m_curnum_n*50/m_maxnum;publish_progress();try{load_yadofile(p);}catch(...){::cw::util::print_ex(std::nullopt);S msg=name(p)+" は読込できませんでした。\n";if(lower(p).size()>=4&&lower(p).substr(lower(p).size()-4)==".wch"&&!wchwarn){msg+="キャラクターが所持しているカードが破損している可能性があります。その場合、あらかじめカードを荷物袋やカード置場へ移動する事で変換が可能になるかもしれません。\n";wchwarn=true;}else if(lower(p).size()>=4&&lower(p).substr(lower(p).size()-4)==".wpt"&&!wptwarn){msg+="パーティメンバが所持しているカードが破損している可能性があります。その場合、あらかじめカードを荷物袋やカード置場へ移動する事で変換が可能になるかもしれません。\n";wptwarn=true;}write_errorlog(msg);}}
 if(!m_wyd)throw std::runtime_error("Environment.wyd was not loaded");auto types=m_wyd->get_cardtypedict();std::map<S,std::shared_ptr<B>>cards;std::vector<S>card_order;
 for(const auto&p:m_cardfiles){m_message=name(p)+" を読込中...";++m_curnum_n;m_curnum=m_curnum_n*50/m_maxnum;publish_progress();try{auto d=load_cardfile(p,types);S fn;if(auto v=std::dynamic_pointer_cast<::cw::binary::skill::SkillCard>(d))fn=v->m_fname;else if(auto v=std::dynamic_pointer_cast<::cw::binary::item::ItemCard>(d))fn=v->m_fname;else if(auto v=std::dynamic_pointer_cast<::cw::binary::beast::BeastCard>(d))fn=v->m_fname;if(!cards.contains(fn))card_order.push_back(fn);cards[fn]=d;}catch(...){::cw::util::print_ex(std::nullopt);write_errorlog(name(p)+" は読込できませんでした。\n");}}
 // Python版と同様、データ一覧を構築する前に関連付け段階を通知する。
 m_message="データリストを作成中...";++m_curnum_n;m_curnum=m_curnum_n*50/m_maxnum;publish_progress();
 for(auto&w:m_wcps){for(auto&h:m_wchs)if(h->m_fname==w->m_fname){if(h->m_image)w->set_image(*h->m_image);break;}if(m_dataversion_int<=8&&w->m_adventurer){m_wyd->m_money+=w->m_adventurer->m_money;w->m_adventurer->m_money=0;}}
 for(auto&t:m_wpts)for(auto&p:m_wpls)if(t->m_fname==p->m_fname){p->m_cards=t->m_cards;t->m_wpl=p;if(t->m_nowadventuring)m_nowadventuringparties.emplace_back(p,t);if(m_dataversion_int<=8){p->m_name=t->m_name;p->m_money=t->m_money;}break;}
 std::set<S>used;auto card=[&](const S&fn){auto it=cards.find(fn);if(it==cards.end())return std::shared_ptr<B>{};if(used.count(fn))return detail::deepcopy_card(it->second);used.insert(fn);return it->second;};
 if(m_dataversion_int>=10){for(auto&p:m_wpls)for(auto&c:p->m_cards)if(auto d=card(c->m_fname))c->set_data(d);for(auto&c:m_wyd->m_unusedcards)if(auto d=card(c->m_fname))c->set_data(d);}
 else for(const auto& fn:card_order){const auto& d=cards.at(fn);const auto cardtype=ext(fn)=="wck"?1:ext(fn)=="wci"?2:ext(fn)=="wcb"?3:0;auto u=std::make_shared<::cw::binary::environment::UnusedCard>();u->initialize(m_wyd,nullptr,true);u->m_fname=fn;if(auto v=std::dynamic_pointer_cast<::cw::binary::skill::SkillCard>(d))u->m_uselimit=v->m_limit;else if(auto v=std::dynamic_pointer_cast<::cw::binary::item::ItemCard>(d))u->m_uselimit=v->m_limit;else if(auto v=std::dynamic_pointer_cast<::cw::binary::beast::BeastCard>(d))u->m_uselimit=v->m_limit;if(cardtype){u->set_data(d);m_wyd->m_unusedcards.push_back(u);}}
 m_datalist.insert(m_datalist.end(),m_wcps.begin(),m_wcps.end());m_datalist.insert(m_datalist.end(),m_wpts.begin(),m_wpts.end());m_datalist.insert(m_datalist.end(),m_wpls.begin(),m_wpls.end());m_datalist.insert(m_datalist.end(),m_wrms.begin(),m_wrms.end());m_datalist.push_back(m_wyd);m_maxnum=I(m_datalist.size()+m_otherfiles.size()+m_otherdirs.size()+m_nowadventuringparties.size());}

S CWYado::convert(){
 if(m_datalist.empty())load();if(!m_wyd)throw std::logic_error("Environment.wyd was not loaded");
 std::filesystem::create_directories(std::filesystem::u8path(m_dir));auto db=std::make_shared<::cw::yadodb::YadoDB>();db->initialize(m_dir,::cw::yadodb::g_YADO);m_curnum_n=0;m_curnum=50;publish_progress();
 for(auto&d:m_datalist){if(!d)continue;d->m_yadodb=db;m_message=name(d->m_fpath)+" を変換中...";++m_curnum_n;m_curnum=std::min<I>(99,50+m_curnum_n*50/std::max<I>(1,m_maxnum));publish_progress();try{
   const S out=d->create_xml(m_dir);if(auto p=std::dynamic_pointer_cast<::cw::binary::party::Party>(d);p&&m_wyd->m_partyname==stem(p->m_fpath))m_wyd->m_cwpypartyname=p8(std::filesystem::path(relnorm(out,m_dir)));
   if(auto e=std::dynamic_pointer_cast<::cw::binary::environment::Environment>(d))for(const auto&c:e->m_errorcards)if(c)write_errorlog(c->m_fname+" は読込できませんでした。\n");
   if(auto p=std::dynamic_pointer_cast<::cw::binary::party::Party>(d))for(const auto&c:p->m_errorcards)if(c)write_errorlog(c->m_fname+" は読込できませんでした。\n");
  }catch(...){::cw::util::print_ex(std::nullopt);write_errorlog(name(d->m_fpath)+" は変換できませんでした。\n");}}
 for(auto&[p,m]:m_nowadventuringparties){if(!p)continue;m_message=p->m_name+" の冒険中情報を変換中...";++m_curnum_n;m_curnum=std::min<I>(99,50+m_curnum_n*50/std::max<I>(1,m_maxnum));publish_progress();try{create_log(p,m);}catch(...){::cw::util::print_ex(std::nullopt);write_errorlog(p->m_name+" の冒険中情報は変換できませんでした。\n");}}
 db->commit();db->close();
 for(auto&p:m_otherfiles){m_message=name(p)+" をコピー中...";++m_curnum_n;m_curnum=std::min<I>(99,50+m_curnum_n*50/std::max<I>(1,m_maxnum));publish_progress();auto d=::cw::binary::util::check_duplicate(join(m_dir,name(p)));std::filesystem::copy_file(std::filesystem::u8path(p),std::filesystem::u8path(d),std::filesystem::copy_options::none);restore_copy_access(d);}
 for(auto&p:m_otherdirs){m_message=name(p)+" をコピー中...";++m_curnum_n;m_curnum=std::min<I>(99,50+m_curnum_n*50/std::max<I>(1,m_maxnum));publish_progress();auto d=::cw::binary::util::check_duplicate(join(m_dir,name(p)));copy_one(p,d);restore_copy_access(d);}
 for(const auto&n:{"Adventurer","Album","BeastCard","ItemCard","SkillCard","Party"})std::filesystem::create_directories(std::filesystem::u8path(join(m_dir,n)));m_curnum=100;publish_progress();return m_dir;
}

// ネイティブ版は固定の一時ScenarioLogツリーを空の状態から作り直す。 Python
// 版のMembersディレクトリの新規作成は既存だと失敗するが、 ここでは以
// 前の途中変換のファイルが新しいパーティのアーカイブへ混ざらないようにする。
void CWYado::create_log(std::shared_ptr<::cw::binary::party::Party>p,std::shared_ptr<::cw::binary::party::PartyMembers>m){
 if(!p||!m||!m->m_nowadventuring)return;
 if(!m_wyd)throw std::logic_error("ScenarioLog requires Environment");
 const S temp=gtempdir(),logdir=join(temp,"ScenarioLog");
 remove_path(logdir);std::filesystem::create_directories(std::filesystem::u8path(join(logdir,"Party")));
 std::filesystem::create_directories(std::filesystem::u8path(join(logdir,"Members")));

 auto root=::cw::data::make_element("ScenarioLog"),prop=::cw::data::make_element("Property");
 prop->append(::cw::data::make_element("Name",p->m_name));
 prop->append(::cw::data::make_element("WsnPath",m->m_scenariopath));
 prop->append(::cw::data::make_element("RoundAutoStart",bool_text(false)));
 prop->append(::cw::data::make_element("Debug",bool_text(m_wyd->m_yadotype==2)));
 prop->append(::cw::data::make_element("AreaId",std::to_string(m->m_areaid)));
 prop->append(::cw::data::make_element("MusicPath",m->m_music));
 prop->append(::cw::data::make_element("Yado",m_wyd->m_name));
 prop->append(::cw::data::make_element("Party",p->m_name));root->append(prop);
 auto bg=::cw::data::make_element("BgImages");m->set_materialdir("");
 for(const auto&b:m->m_bgimgs)if(b)bg->append(b->get_data());root->append(bg);
 // フラグ・ステップのXMLの子は、ネイティブの std::map
 // のキー順に並ぶ。 Pythonの辞書は挿入順なので表現上の違
 // いを残しつつ、共通のログ書き込み処理で同じ名前・値を保存する。
 auto flags=::cw::data::make_element("Flags");for(const auto&[n,v]:m->m_flags)flags->append(::cw::data::make_element("Flag",n,std::map<S,S>{{"value",bool_text(v)}}));root->append(flags);
 auto steps=::cw::data::make_element("Steps");for(const auto&[n,v]:m->m_steps)steps->append(::cw::data::make_element("Step",n,std::map<S,S>{{"value",std::to_string(v)}}));root->append(steps);
 root->append(::cw::data::make_element("Gossips"));root->append(::cw::data::make_element("CompleteStamps"));
 auto infos=::cw::data::make_element("InfoCards");for(const auto id:m->m_infocards)infos->append(::cw::data::make_element("InfoCard",std::to_string(id)));root->append(infos);
 auto casts=::cw::data::make_element("CastCards");for(auto it=m->m_friendcards.rbegin();it!=m->m_friendcards.rend();++it)casts->append(::cw::data::make_element("FriendCard",std::to_string(*it)));root->append(casts);
 root->append(::cw::data::make_element("DeletedFiles"));auto lost=::cw::data::make_element("LostAdventurers");root->append(lost);
 m->create_vanisheds_xml(m->get_dir());
 for(const auto&a:m->m_vanisheds)if(a){const S rel=relnorm(a->m_xmlpath,a->get_dir());lost->append(::cw::data::make_element("LostAdventurer",p8(std::filesystem::path(rel))));}
 write_xml(root,join(logdir,"ScenarioLog.xml"));

 auto partyroot=::cw::data::make_element("ScenarioLog"),partyprop=::cw::data::make_element("Property");
 partyprop->append(::cw::data::make_element("Name",m->m_name));partyprop->append(::cw::data::make_element("Money",std::to_string(m->m_money_beforeadventure)));
 auto members=::cw::data::make_element("Members");partyprop->append(members);partyroot->append(partyprop);
 auto append_member=[&](const auto&a){if(a){auto fn=name(a->m_xmlpath);members->append(::cw::data::make_element("LostAdventurer",stem(fn)));}};
 for(const auto&a:m->m_adventurers)append_member(a);for(const auto&a:m->m_vanisheds)append_member(a);
 write_xml(partyroot,join(logdir,"Party/Party.xml"));
 for(const auto&a:m->m_adventurers)if(a)write_xml(a->get_f9data(),join(join(logdir,"Members"),name(a->m_xmlpath)));
 for(const auto&a:m->m_vanisheds)if(a)write_xml(a->get_f9data(),join(join(logdir,"Members"),name(a->m_xmlpath)));
 auto backpack=::cw::data::make_element("BackpackFiles");
 const S cdpath=p8(std::filesystem::u8path(p->m_xmlpath).parent_path());::cw::yadodb::YadoDB carddb;carddb.initialize(cdpath,::cw::yadodb::g_PARTY);
 for(const auto&fp:carddb.get_cardfpaths(false))backpack->append(::cw::data::make_element("File",fp));carddb.close();write_xml(backpack,join(logdir,"Backpack.xml"));
 const S wsl=p8(std::filesystem::u8path(p->m_xmlpath).replace_extension(".wsl"));::cw::util::compress_zip(logdir,wsl,true);remove_path(logdir);
}
// ネイティブの情報だけの処理生成は、宿の仮名とnullの取り込み元を
// 許し、変換時に出力作成前に拒否する。 Python版は直ちに ydata.name
// を参照するため、この入力エラーの報告が早い。
void UnconvCWYado::initialize(std::shared_ptr<::cw::data::YadoData>y,S dst,::cw::lib::Real engine){m_ydata=std::move(y);m_targetengine=engine;m_name=m_ydata?m_ydata->m_name:S{"Yado"};m_dir=::cw::binary::util::check_duplicate(join(dst,::cw::binary::util::check_filename(m_name)));m_errorlog.clear();m_message.clear();m_curnum=0;m_maxnum=1;if(m_ydata)m_maxnum+=static_cast<I>(m_ydata->m_storehouse.size()+m_ydata->m_standbys.size()+m_ydata->m_partys.size()*2+m_ydata->m_album.size());}
void UnconvCWYado::write_errorlog(S s){m_errorlog+=s+"\n";}
void UnconvCWYado::convert(){
 if(!m_ydata)throw std::invalid_argument("YadoData is null");
 ::cw::binary::base::ImageImportScope image_roots(m_ydata->m_tempdir,m_ydata->m_yadodir);
 publish_progress();std::filesystem::create_directories(std::filesystem::u8path(m_dir));
 const auto output_path=[&](const S&n,const S&e){return ::cw::binary::util::check_duplicate(join(m_dir,::cw::binary::util::check_filename(n)+e));};
 const auto writer=[this](const S&path){auto f=std::make_shared<::cw::binary::cwfile::CWFileWriter>();f->initialize(path,"wb",false,m_targetengine,[this](const ::cw::lib::Arguments&a,const ::cw::lib::KeywordArguments&){if(!a.empty())if(auto p=std::get_if<S>(&a.front().storage()))write_errorlog(*p);return ::cw::lib::Value{};});return f;};
 const auto xml_file=[](const S&path){auto e=::cw::data::xml2element(path,"",std::nullopt,std::nullopt,false,std::nullopt);if(!e)throw std::runtime_error("XML data is empty: "+path);return e;};
 const auto card_write=[&](const std::shared_ptr<::cw::header::CardHeader>&h)->std::tuple<std::shared_ptr<::cw::data::CWPyElement>,S>{
  if(!h)throw std::invalid_argument("null card header");auto d=xml_file(h->m_fpath);auto out=output_path(h->m_name,".wid");auto f=writer(out);try{if(h->m_type=="SkillCard")::cw::binary::skill::SkillCard::unconv(f,d,false);else if(h->m_type=="ItemCard")::cw::binary::item::ItemCard::unconv(f,d,false);else if(h->m_type=="BeastCard")::cw::binary::beast::BeastCard::unconv(f,d,false);/* Python版には最後のelseがなく、不明な種類でも空の出力ファイルを書き出す。 */ f->flush();f->close();return {d,out};}catch(...){f->close();remove_path(out);throw;}
 };
 // Python版の変換内の yadocards 辞書と同様、同じ元
 // パスの後の出力は名前・データを置き換えるが、 Environment.wyd
 // の初出順は変えない。 検索マップと順序ベクターでこの同
 // じ規則を保ち、Python版との意図的な差は設けない。
 std::map<S,std::tuple<S,std::shared_ptr<::cw::data::CWPyElement>>>yadocards;std::vector<std::tuple<S,std::tuple<S,std::shared_ptr<::cw::data::CWPyElement>>>>yado_order;std::vector<std::tuple<S,std::shared_ptr<::cw::data::CWPyElement>>>unusedcards;auto set_yadocard=[&](const S&key,std::tuple<S,std::shared_ptr<::cw::data::CWPyElement>>value){yadocards[key]=value;const auto it=std::find_if(yado_order.begin(),yado_order.end(),[&](const auto&entry){return std::get<0>(entry)==key;});if(it==yado_order.end())yado_order.emplace_back(key,std::move(value));else std::get<1>(*it)=std::move(value);};
 auto unsupported=[this](const ::cw::binary::cwfile::UnsupportedError&e,const S&fallback){write_errorlog(e.m_msg.empty()?fallback+" は対象エンジンで使用できない機能("+e.m_funcname+")を使用しているため、変換しません。\n":e.m_msg);};
 for(const auto&h:m_ydata->m_storehouse){if(!h)continue;++m_curnum;m_message=h->m_name+" を変換中...";publish_progress();try{auto[d,p]=card_write(h);const S fn=name(p);unusedcards.emplace_back(fn,d);set_yadocard(h->m_fpath,std::make_tuple(fn,d));}catch(const ::cw::binary::cwfile::UnsupportedError&e){unsupported(e,h->m_name);}catch(...){::cw::util::print_ex(std::nullopt);write_errorlog(h->m_name+" は変換できませんでした。\n");}}
 for(const auto&h:m_ydata->m_standbys){if(!h)continue;++m_curnum;m_message=h->m_name+" を変換中...";publish_progress();const S in=header_path(h);
  // Python版と同様、出力ごとの例外処理より前に待機中の冒険者
  // を読み込み・回復する。 元XMLやキャラクター設定が不正なら
  // 変換を中止し、 黙って続けて異なるメンバー集合を出力しない。
  auto d=xml_file(in);auto tree=std::make_shared<::cw::data::CWPyElementTree>();tree->initialize(in,d);auto ch=std::make_shared<::cw::character::Character>();ch->initialize(tree);ch->set_fullrecovery();d=tree->getroot();
  S ppath=output_path(h->m_name,".wcp"),hpath=output_path(h->m_name,".wch");try{auto f=writer(ppath);::cw::binary::adventurer::AdventurerCard::unconv(f,d);f->flush();f->close();f=writer(hpath);::cw::binary::adventurer::AdventurerHeader::unconv(f,d,ppath);f->flush();f->close();}catch(const ::cw::binary::cwfile::UnsupportedError&e){unsupported(e,h->m_name);remove_path(ppath);remove_path(hpath);}catch(...){::cw::util::print_ex(std::nullopt);write_errorlog(h->m_name+" は変換できませんでした。\n");remove_path(ppath);remove_path(hpath);}}
 struct PartyPair{std::shared_ptr<::cw::header::PartyHeader>header;std::shared_ptr<::cw::data::Party>party;};std::vector<PartyPair>parties;
 for(const auto&h:m_ydata->m_partys){if(!h)continue;++m_curnum;m_message=h->m_name+" の荷物袋を変換中...";publish_progress();auto pt=std::make_shared<::cw::data::Party>();pt->initialize(h,true,m_ydata->m_yadodir,m_ydata->m_tempdir);parties.push_back({h,pt});auto write_party_card=[&](const std::shared_ptr<::cw::header::CardHeader>&ch){if(!ch)return;try{auto[d,p]=card_write(ch);set_yadocard(ch->m_fpath,std::make_tuple(name(p),d));}catch(const ::cw::binary::cwfile::UnsupportedError&e){unsupported(e,h->m_name+" の所持する "+ch->m_name);}catch(...){::cw::util::print_ex(std::nullopt);write_errorlog(h->m_name+" の "+ch->m_name+" は変換できませんでした。\n");}};for(const auto&ch:pt->m_backpack)write_party_card(ch);for(const auto&ch:pt->m_backpack_moved)write_party_card(ch);}
 std::map<S,S>partytable;
 for (auto& entry : parties) {
    auto h = entry.header;
    auto pt = entry.party;
    if (!h || !pt) continue;
    ++m_curnum;
    m_message = h->m_name + " を変換中...";
    publish_progress();
    S logdir, scen;
    const S pxml = value_string(pt->m_path);
    const S logroot = join(gtempdir(), "ScenarioLog");
    // Python版と同様、このパーティのアーカイブを調べる前に、以前のシナリオログを除く。
    if (std::filesystem::is_directory(std::filesystem::u8path(logroot))) remove_path(logroot);
    const S wsl = p8(std::filesystem::u8path(pxml).replace_extension(".wsl"));
    if (std::filesystem::is_regular_file(std::filesystem::u8path(wsl))) {
        ::cw::util::decompress_zip(wsl, gtempdir(), "ScenarioLog", std::nullopt, std::nullopt, false, std::nullopt);
        logdir = logroot;
        const auto log = ::cw::data::xml2etree(join(logdir, "ScenarioLog.xml"), "", std::nullopt, std::nullopt, false);
        if (!log) throw std::invalid_argument("Inn conversion requires scenario log XML");
        // Python版と同様、Nameを必須とし、ログは出力ごとの例外処理の外で読む。
        scen = log->gettext("Property/Name", std::nullopt);
        if (scen.empty()) scen = "noname";
    }
    const auto names = h->get_membernames();
    std::map<S,S> tbl;
    for (std::size_t i = 0; i < h->m_members.size(); ++i) {
        // Python版のインデックス参照と同様、名前がなければ出力ファイルを開く前に例外とする。
        tbl[value_string(h->m_members[i])] = names.at(i);
    }
    const S p1 = output_path(pt->m_name, ".wpl");
    const S p2 = output_path(pt->m_name, ".wpt");
    try {
        auto d = element_from_value(pt->m_data);
        if (!d) d = xml_file(pxml);
        // 現行Python版と同様、展開したScenarioLogを PartyMembers::unconv
        // 用に残し、 パーティの両出力が成功してから削除する
        // 。 出力ごとの失敗処理では、早期に消さず保持する。
        auto f=writer(p1);::cw::binary::party::Party::unconv(f,d,m_ydata->m_name,tbl,scen);f->flush();f->close();f=writer(p2);::cw::binary::party::PartyMembers::unconv(f,pt,yadocards,logdir);f->flush();f->close();const S hp=value_string(h->m_fpath);const S base=lower(hp).rfind("yado",0)==0?m_ydata->m_yadodir:m_ydata->m_tempdir;partytable[p8(std::filesystem::path(relnorm(hp,base)))]=stem(p2);if(!logdir.empty())remove_path(join(gtempdir(),"ScenarioLog"));}catch(const ::cw::binary::cwfile::UnsupportedError&e){unsupported(e,h->m_name);remove_path(p1);remove_path(p2);}catch(...){::cw::util::print_ex(std::nullopt);write_errorlog(h->m_name+" は変換できませんでした。\n");remove_path(p1);remove_path(p2);}}
 for(const auto&h:m_ydata->m_album){if(!h)continue;++m_curnum;m_message=h->m_name+" を変換中...";publish_progress();// Python版と同様、アルバムXMLの読み込みは出力ごとの変換例外処理の外に置く。
  auto d=xml_file(header_path(h));const S out=output_path(h->m_name,".wrm");try{auto f=writer(out);::cw::binary::album::Album::unconv(f,d);f->flush();f->close();}catch(const ::cw::binary::cwfile::UnsupportedError&e){unsupported(e,h->m_name);remove_path(out);}catch(...){::cw::util::print_ex(std::nullopt);write_errorlog(h->m_name+" は変換できませんでした。\n");remove_path(out);}}
 ++m_curnum;m_message="宿情報を変換中...";publish_progress();try{if(!m_ydata->m_environment)throw std::logic_error("YadoData environment is null");auto d=m_ydata->m_environment->find_exists(".");auto f=writer(join(m_dir,"Environment.wyd"));::cw::binary::environment::Environment::unconv(f,d,partytable,unusedcards,yado_order);f->flush();f->close();}catch(...){::cw::util::print_ex(std::nullopt);write_errorlog("宿情報は変換できませんでした。\n");}
}
void main(){}
}
