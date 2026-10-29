#include "cw/skin/win32res.hpp"
#include "cw/lib/encoding.hpp"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <type_traits>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace cw::skin::win32res {
using I=::cw::lib::Integer; using B=::cw::lib::Bytes; using S=::cw::lib::String;
namespace {
void range(const B& b,std::size_t p,std::size_t n){if(p>b.size()||n>b.size()-p)throw std::out_of_range("resource range");}
I u16(const B& b,std::size_t p){range(b,p,2);return b[p]|(I(b[p+1])<<8);}
I u32(const B& b,std::size_t p){range(b,p,4);return b[p]|(I(b[p+1])<<8)|(I(b[p+2])<<16)|(I(b[p+3])<<24);}
void a16(B& b,I v){b.push_back(v&255);b.push_back((v>>8)&255);}
void a32(B& b,I v){for(int n=0;n<4;++n)b.push_back((v>>(n*8))&255);}
void utf8code(S& out,unsigned c){if(c<0x80)out.push_back(char(c));else if(c<0x800){out.push_back(char(0xc0|(c>>6)));out.push_back(char(0x80|(c&63)));}else if(c<0x10000){out.push_back(char(0xe0|(c>>12)));out.push_back(char(0x80|((c>>6)&63)));out.push_back(char(0x80|(c&63)));}else{out.push_back(char(0xf0|(c>>18)));out.push_back(char(0x80|((c>>12)&63)));out.push_back(char(0x80|((c>>6)&63)));out.push_back(char(0x80|(c&63)));}}
S utf16(const B& b,std::size_t p,std::size_t byte_count){
    if(byte_count%2!=0)throw std::runtime_error("odd UTF-16 byte count");
    range(b,p,byte_count);
    bool little=true;
    if(byte_count>=2&&b[p]==0xff&&b[p+1]==0xfe){p+=2;byte_count-=2;}
    else if(byte_count>=2&&b[p]==0xfe&&b[p+1]==0xff){p+=2;byte_count-=2;little=false;}
    S out;
    const auto units=byte_count/2;
    for(std::size_t i=0;i<units;++i){
        const auto at=p+i*2;
        unsigned c=little?unsigned(b[at])|(unsigned(b[at+1])<<8)
                         :(unsigned(b[at])<<8)|unsigned(b[at+1]);
        if(0xd800<=c&&c<=0xdbff){
            if(i+1>=units)throw std::runtime_error("unterminated UTF-16 surrogate");
            const auto next=at+2;
            const unsigned low=little?unsigned(b[next])|(unsigned(b[next+1])<<8)
                                      :(unsigned(b[next])<<8)|unsigned(b[next+1]);
            if(low<0xdc00||0xdfff<low)throw std::runtime_error("invalid UTF-16 surrogate");
            c=0x10000+((c-0xd800)<<10)+(low-0xdc00);++i;
        }else if(0xdc00<=c&&c<=0xdfff)throw std::runtime_error("unexpected UTF-16 low surrogate");
        utf8code(out,c);
    }
    return out;
}
template<class T>T array_value(const std::shared_ptr<ResArray>& array,const T& fallback){
    if(!array)return fallback;
    if constexpr(std::is_same_v<T,std::vector<::cw::lib::Value>>){
        return array->m_values;
    }else if constexpr(std::is_same_v<T,std::vector<S>>){
        std::vector<S> result;
        for(const auto& value:array->m_values){
            const auto* text=std::get_if<S>(&value.storage());
            if(!text)return fallback;
            result.push_back(*text);
        }
        return result;
    }else if constexpr(std::is_same_v<T,std::vector<B>>){
        std::vector<B> result;
        for(const auto& value:array->m_values){
            const auto* bytes=std::get_if<B>(&value.storage());
            if(!bytes)return fallback;
            result.push_back(*bytes);
        }
        return result;
    }else{
        return fallback;
    }
}
// Py は外側の list 型だけを比較するため、List[str] の既定
// 値に対して bytes の一覧を返す場合がある。 型付き C++ の vector<T>
// は別型の要素を公開できないので、その不一致では既定値を維持する。
// 動的な外側リストの動作には ResArray と find_res_8
// を使い、要求された要素型を強制せず保存済みの一覧種別を保つ。
template<class T>T find_t(const std::shared_ptr<ResTable>& t,const std::vector<B>& path,const T& d){
    assert(!path.empty());
    if(!t)return d;
    auto i=t->m_table.find(path[0]);
    if(i==t->m_table.end())return d;
    if(path.size()==1){
        if(auto p=std::get_if<T>(&i->second))return *p;
        if(auto array=std::get_if<std::shared_ptr<ResArray>>(&i->second))
            return array_value(*array,d);
        return d;
    }
    auto c=std::get_if<std::shared_ptr<ResTable>>(&i->second);
    if(!c||!*c)return d;
    return find_t(*c,std::vector<B>(path.begin()+1,path.end()),d);
}
#ifdef _WIN32
std::wstring utf8wide(const S& text){
    if(text.empty())return {};
    const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);
    if(n<=0)throw std::runtime_error("UTF-8 resource path conversion failed");
    std::wstring result(static_cast<std::size_t>(n),L'\0');
    if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),result.data(),n)!=n)
        throw std::runtime_error("UTF-8 resource path conversion failed");
    return result;
}
#if !defined(_WIN64)
LPCSTR win_resource_name_a(const ResourceId& id,S& storage){
    if(const auto* number=std::get_if<I>(&id)){
        if(*number<0||*number>std::numeric_limits<WORD>::max())return nullptr;
        return MAKEINTRESOURCEA(static_cast<WORD>(*number));
    }
    storage=std::get<S>(id);
    return storage.c_str();
}
#endif
#endif
std::filesystem::path resource_path(const S& path){
#ifdef _WIN32
    return std::filesystem::path(utf8wide(path));
#else
    return std::filesystem::u8path(path);
#endif
}
}
#if defined(_WIN32) && !defined(_WIN64)
bool g__winapi=true;
#else
bool g__winapi=false;
#endif
::cw::lib::Value g_ResType{};
void ResTable::initialize(){m_table.clear();}
void Win32Res::initialize(S fpath){dispose();if(!fpath.empty())laod_resmodule(std::move(fpath));}
void Win32Res::py_del(){dispose();}
void Win32Res::dispose(){
#ifdef _WIN32
 if(m_winapi&&m_winhandle)FreeLibrary(reinterpret_cast<HMODULE>(m_winhandle));
#endif
 m_table.clear();m_winhandle=0;m_winapi=false;
}
ResourceId Win32Res::_res_name(const B& base,I addr,I w1){if((std::uint64_t(w1)&0x80000000u)!=0){auto p=std::size_t(addr+(w1&0x7fffffff));const auto units=std::size_t(u16(base,p));return utf16(base,p+2,units*2);}return w1;}

void Win32Res::laod_resmodule(S path){
    dispose();
#ifdef _WIN32
    DWORD load_error=ERROR_FILE_NOT_FOUND;
#endif
#if defined(_WIN32) && !defined(_WIN64)
    // Python の ctypes 経路と同様、コードを実行せずデータファイルとして読み込む。
    // Win32 API の経路は Python と同じく、32 ビットプロセスだけで使う。
    const auto wide_path = utf8wide(path);
    const auto handle=LoadLibraryExW(wide_path.c_str(),nullptr,LOAD_LIBRARY_AS_DATAFILE|LOAD_WITH_ALTERED_SEARCH_PATH);
    if(handle){m_winhandle=reinterpret_cast<std::uintptr_t>(handle);m_winapi=true;return;}
    load_error=GetLastError();
#endif
    std::ifstream f(resource_path(path),std::ios::binary);if(!f){
#ifdef _WIN32
      throw std::system_error(static_cast<int>(load_error),std::system_category(),"resource module open failed: "+path);
#else
      throw std::runtime_error("resource module open failed: "+path);
#endif
    }
    B base((std::istreambuf_iterator<char>(f)),{});range(base,0,64);if(base[0]!='M'||base[1]!='Z')throw std::runtime_error("invalid MZ");
    auto pe=std::size_t(u32(base,60));range(base,pe,24);if(base[pe]!='P'||base[pe+1]!='E'||base[pe+2]||base[pe+3])throw std::runtime_error("invalid PE");
    auto coff=pe+4;I sections=u16(base,coff+2),optsize=u16(base,coff+16);auto opt=coff+20;range(base,opt,std::size_t(optsize));const auto magic=u16(base,opt);if(magic!=0x10b&&magic!=0x20b)throw std::runtime_error("unknown PE optional-header magic");
    const auto resource_directory=opt+(magic==0x10b?112:128);
    if(resource_directory<opt||resource_directory+4>opt+std::size_t(optsize))throw std::runtime_error("resource directory is outside optional header");
    I rrva=u32(base,resource_directory);auto st=opt+std::size_t(optsize);
    bool found=false;I raw=0,srva=rrva,rsize=0;for(I n=0;n<sections;++n){auto p=st+std::size_t(n)*40;range(base,p,40);S name(reinterpret_cast<const char*>(base.data()+p),5);I rva=u32(base,p+12);if(name==".rsrc"||rva==rrva){srva=rva;raw=u32(base,p+20);rsize=u32(base,p+16);found=true;break;}}
    if(!found||rsize<=0)throw std::runtime_error("resource section not found");
    range(base,std::size_t(raw),std::size_t(rsize));
    auto counts=[&](std::size_t p){range(base,p,16);return std::pair<I,I>{u16(base,p+12),u16(base,p+14)};};auto [rn,ri]=counts(std::size_t(raw));auto e1=std::size_t(raw)+16;
    for(I a=0;a<rn+ri;++a,e1+=8){range(base,e1,8);I n1=u32(base,e1),d1=u32(base,e1+4);if(!(std::uint64_t(d1)&0x80000000u))throw std::runtime_error("resource level1");auto k1=_res_name(base,raw,n1);auto p2=std::size_t(raw+(d1&0x7fffffff));auto [n2n,n2i]=counts(p2);auto e2=p2+16;
      for(I b=0;b<n2n+n2i;++b,e2+=8){range(base,e2,8);I n2=u32(base,e2),d2=u32(base,e2+4);if(!(std::uint64_t(d2)&0x80000000u))throw std::runtime_error("resource level2");auto k2=_res_name(base,raw,n2);auto p3=std::size_t(raw+(d2&0x7fffffff));auto [n3n,n3i]=counts(p3);if(n3n+n3i<1)throw std::runtime_error("empty resource language");range(base,p3+16,8);I de=u32(base,p3+20);if(std::uint64_t(de)&0x80000000u)throw std::runtime_error("resource level3");auto dp=std::size_t(raw+de);range(base,dp,8);I rva=u32(base,dp),size=u32(base,dp+4);if(rva<srva)throw std::runtime_error("resource data RVA before section");auto delta=std::size_t(rva-srva);if(delta>std::size_t(rsize)||std::size_t(size)>std::size_t(rsize)-delta)throw std::runtime_error("resource data outside section");auto off=std::size_t(raw)+delta;range(base,off,std::size_t(size));m_table[k1][k2]=B(base.begin()+off,base.begin()+off+size);}}
}

std::optional<B> Win32Res::lookup_rcdata(ResourceId type,ResourceId name)const{auto a=m_table.find(type);if(a==m_table.end())return std::nullopt;auto b=a->second.find(name);if(b==a->second.end())return std::nullopt;return b->second;}
std::optional<B> Win32Res::get_rcdata(ResourceId type,ResourceId name)const{
#if defined(_WIN32) && !defined(_WIN64)
 if(m_winapi&&m_winhandle){
  S name_storage,type_storage;const auto* resource_name=win_resource_name_a(name,name_storage);const auto* resource_type=win_resource_name_a(type,type_storage);if(!resource_name||!resource_type)return std::nullopt;
  const auto module=reinterpret_cast<HMODULE>(m_winhandle);const auto resource=FindResourceA(module,resource_name,resource_type);if(!resource)return std::nullopt;
  const auto size=SizeofResource(module,resource);const auto loaded=LoadResource(module,resource);if(!loaded)return std::nullopt;const auto locked=LockResource(loaded);if(size&&!locked)return std::nullopt;
  if(!size)return B{};const auto* first=static_cast<const std::uint8_t*>(locked);return B(first,first+size);
 }
#endif
 return lookup_rcdata(type,std::move(name));
}
std::optional<B> Win32Res::get_rcdata(I type,B name)const{
#if defined(_WIN32) && !defined(_WIN64)
 if(m_winapi) return get_rcdata(ResourceId{type},ResourceId{S(name.begin(),name.end())});
#endif
 return get_rcdata(ResourceId{type},ResourceId{::cw::lib::encoding::decode_utf8(name)});
}
std::optional<B> Win32Res::get_rcdata(I type,::cw::lib::Value name)const{if(auto p=std::get_if<I>(&name.storage()))return get_rcdata(type,ResourceId{*p});if(auto p=std::get_if<S>(&name.storage()))return get_rcdata(type,ResourceId{*p});if(auto p=std::get_if<B>(&name.storage()))return get_rcdata(type,*p);return std::nullopt;}
std::optional<B> Win32Res::get_rcdata(::cw::lib::Value type,::cw::lib::Value name)const{
    const auto raw_bytes=[](const B& bytes,bool raw) {
        return raw ? S(bytes.begin(),bytes.end())
                   : ::cw::lib::encoding::decode_utf8(bytes);
    };
    const bool raw=
#if defined(_WIN32) && !defined(_WIN64)
        m_winapi;
#else
        false;
#endif
    const auto to_id=[&](const ::cw::lib::Value& value)->std::optional<ResourceId>{
        if(const auto* p=std::get_if<I>(&value.storage()))return ResourceId{*p};
        if(const auto* p=std::get_if<S>(&value.storage()))return ResourceId{*p};
        if(const auto* p=std::get_if<B>(&value.storage()))return ResourceId{raw_bytes(*p,raw)};
        return std::nullopt;
    };
    const auto type_id=to_id(type);
    const auto name_id=to_id(name);
    if(!type_id||!name_id)return std::nullopt;
    return get_rcdata(*type_id,*name_id);
}
std::optional<B> Win32Res::get_bitmap(S name){auto d=get_rcdata(g_RT_BITMAP,name);if(!d||d->empty())return {};if(u32(*d,0)!=40)throw std::runtime_error(std::to_string(u32(*d,0)));I bits=u16(*d,14),used=u32(*d,32),palette=used?used*4:(bits==1?8:bits==4?64:bits==8?1024:0);B out{'B','M'};a32(out,14+d->size());a16(out,0);a16(out,0);a32(out,14+40+palette);out.insert(out.end(),d->begin(),d->end());return out;}
std::optional<B> Win32Res::get_cursor(::cw::lib::Value number){
 ResourceId id;
 if(auto s=std::get_if<S>(&number.storage())){auto g=get_rcdata(g_RT_GROUP_CURSOR,*s);if(!g||g->empty())return {};id=u16(*g,18);}
 else if(auto i=std::get_if<I>(&number.storage()))id=*i;else throw std::invalid_argument("cursor id");
 auto src=get_rcdata(g_RT_CURSOR,id);if(!src||src->empty())return {};range(*src,0,4);
 I x=u16(*src,0),y=u16(*src,2);B d(src->begin()+4,src->end());
 if(u32(d,0)!=40)throw std::runtime_error(std::to_string(u32(d,0)));
 I w=u32(d,4);const auto signed_height=static_cast<std::int64_t>(static_cast<std::int32_t>(u32(d,8)));
 I h=signed_height<0?-signed_height:signed_height;I bits=u16(d,14);
 if(bits==1&&u32(d,16)==0&&u32(d,36)==0){h/=2;bits=0;}
 if(w>255||h>255||bits>255)throw std::overflow_error("cursor dimensions");
 B out;a16(out,0);a16(out,2);a16(out,1);
 out.push_back(static_cast<std::uint8_t>(w));out.push_back(static_cast<std::uint8_t>(h));out.push_back(static_cast<std::uint8_t>(bits));out.push_back(0);
 a16(out,x);a16(out,y);a32(out,d.size());a32(out,22);out.insert(out.end(),d.begin(),d.end());return out;
}

std::optional<std::shared_ptr<ResTable>> Win32Res::get_tpf0form(B name){
    auto data=get_rcdata(g_RT_RCDATA,name);
    if(!data||data->size()<4||!std::equal(data->begin(),data->begin()+4,"TPF0"))return {};
    auto root=std::make_shared<ResTable>();
    std::vector<std::shared_ptr<ResTable>> stack{root};
    std::size_t p=4;
    const auto slice_end=[&](std::size_t at,std::uint64_t length) {
        const auto remaining=at<data->size()?data->size()-at:0;
        return at+static_cast<std::size_t>(std::min<std::uint64_t>(length,remaining));
    };
    while(p<data->size()){
        I clen=(*data)[p++];
        if(!clen){
            if(stack.empty())throw std::runtime_error("TPF0 stack");
            stack.pop_back();
            continue;
        }
        p=slice_end(p,static_cast<std::uint64_t>(clen));
        range(*data,p,1);
        I nlen=(*data)[p++];
        const auto node_end=slice_end(p,static_cast<std::uint64_t>(nlen));
        B node(data->begin()+p,data->begin()+node_end);
        p=node_end;
        if(stack.empty())throw std::runtime_error("TPF0 root");
        auto child=std::make_shared<ResTable>();
        stack.back()->m_table[node]=child;
        stack.push_back(child);
        while(true){
            range(*data,p,1);
            I klen=(*data)[p++];
            if(!klen)break;
            const auto key_end=slice_end(p,static_cast<std::uint64_t>(klen));
            B k(data->begin()+p,data->begin()+key_end);
            p=key_end;
            range(*data,p,1);
            I t=(*data)[p++];
            ResValue v;
            if(t==2){
                range(*data,p,1);
                v=I(std::int8_t((*data)[p++]));
            }else if(t==3){
                v=u16(*data,p);
                p+=2;
            }else if(t==8){
                v=false;
            }else if(t==9){
                v=true;
            }else if(t==6||t==7){
                range(*data,p,1);
                I n=(*data)[p++];
                const auto end=slice_end(p,static_cast<std::uint64_t>(n));
                B bytes(data->begin()+p,data->begin()+end);
                v=::cw::lib::encoding::cp932_to_utf8(bytes);
                p=end;
            }else if(t==10){
                I n=u32(*data,p);
                p+=4;
                const auto end=slice_end(p,static_cast<std::uint64_t>(n));
                v=B(data->begin()+p,data->begin()+end);
                p=end;
            }else if(t==11){
                auto a=std::make_shared<ResArray>();
                a->m_kind=ResArrayKind::Bytes;
                while(true){
                    range(*data,p,1);
                    I n=(*data)[p++];
                    if(!n)break;
                    const auto end=slice_end(p,static_cast<std::uint64_t>(n));
                    a->m_values.emplace_back(B(data->begin()+p,data->begin()+end));
                    p=end;
                }
                v=std::move(a);
            }else if(t==18){
                I n=u32(*data,p);
                p+=4;
                const auto byte_count=std::min<std::uint64_t>(
                    static_cast<std::uint64_t>(n)*2,data->size()-p);
                v=utf16(*data,p,static_cast<std::size_t>(byte_count));
                p+=static_cast<std::size_t>(byte_count);
            }else if(t==1){
                auto a=std::make_shared<ResArray>();
                a->m_kind=ResArrayKind::Values;
                while(true){
                    range(*data,p,1);
                    I q=(*data)[p++];
                    if(!q)break;
                    if(q==2){
                        range(*data,p,1);
                        a->m_values.emplace_back(I((*data)[p++]));
                    }else if(q==3){
                        a->m_values.emplace_back(u16(*data,p));
                        p+=2;
                    }else if(q==6){
                        range(*data,p,1);
                        I n=(*data)[p++];
                        const auto end=slice_end(p,static_cast<std::uint64_t>(n));
                        B bytes(data->begin()+p,data->begin()+end);
                        a->m_values.emplace_back(::cw::lib::encoding::cp932_to_utf8(bytes));
                        p=end;
                    }else{
                        // Python と同様、型付き要素のループを抜け、
                        // 未知の型の一バイトを配列の終端として消費する。
                        break;
                    }
                }
                v=std::move(a);
            }else{
                throw std::runtime_error("value type: "+std::to_string(t));
            }
            stack.back()->m_table[k]=std::move(v);
        }
    }
    return root;
}

std::vector<S> find_res(std::shared_ptr<ResTable>t,std::vector<B>p,std::vector<S>d){return find_t(t,p,d);}std::vector<::cw::lib::Value> find_res(std::shared_ptr<ResTable>t,std::vector<B>p,std::vector<::cw::lib::Value>d){return find_t(t,p,d);}bool find_res_2(std::shared_ptr<ResTable>t,std::vector<B>p,bool d){return find_t(t,p,d);}I find_res_3(std::shared_ptr<ResTable>t,std::vector<B>p,I d){return find_t(t,p,d);}S find_res_4(std::shared_ptr<ResTable>t,std::vector<B>p,S d){return find_t(t,p,d);}B find_res_5(std::shared_ptr<ResTable>t,std::vector<B>p,B d){return find_t(t,p,d);}std::vector<B> find_res_6(std::shared_ptr<ResTable>t,std::vector<B>p,std::vector<B>d){return find_t(t,p,d);}std::shared_ptr<ResTable> find_res_7(std::shared_ptr<ResTable>t,std::vector<B>p,std::shared_ptr<ResTable>d){return find_t(t,p,d);}
::cw::lib::Value find_res_8(std::shared_ptr<ResTable> t,std::vector<B> p,::cw::lib::Value d){
 if(p.empty())throw std::invalid_argument("empty resource path");
 if(!t)return d;
 auto i=t->m_table.find(p.front());if(i==t->m_table.end())return d;
 if(p.size()>1){
  auto child=std::get_if<std::shared_ptr<ResTable>>(&i->second);if(!child||!*child)return d;
  p.erase(p.begin());return find_res_8(*child,std::move(p),std::move(d));
 }
 // 動的な Value 型で表せる末端の値だけを返す。
 if(auto q=std::get_if<I>(&i->second);q&&std::holds_alternative<I>(d.storage()))return *q;
 if(auto q=std::get_if<S>(&i->second);q&&std::holds_alternative<S>(d.storage()))return *q;
 if(auto q=std::get_if<bool>(&i->second);q&&std::holds_alternative<bool>(d.storage()))return *q;
 if(auto q=std::get_if<B>(&i->second);q&&std::holds_alternative<B>(d.storage()))return *q;
 if(const auto* expected=std::get_if<::cw::lib::ObjectPtr>(&d.storage());expected&&*expected){
  if(const auto expected_table=std::dynamic_pointer_cast<ResTable>(*expected)){
   (void)expected_table;
   if(auto q=std::get_if<std::shared_ptr<ResTable>>(&i->second);q&&*q)
    return ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(*q));
   return d;
  }
  if(const auto expected_array=std::dynamic_pointer_cast<ResArray>(*expected)){
   (void)expected_array;
   // Python と同様、list 同士だけを比較し、保存した一覧
   // の要素型は検査しない。 動的アダプターでは要求された ResArray
   // の種別を強制せず、実際に保存された表現を維持する。
   if(auto stored=std::get_if<std::shared_ptr<ResArray>>(&i->second);stored&&*stored)
    return ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(*stored));
   auto result=std::make_shared<ResArray>();
   if(auto q=std::get_if<std::vector<::cw::lib::Value>>(&i->second)){
    result->m_kind=ResArrayKind::Values;
    result->m_values=*q;
   }else if(auto strings=std::get_if<std::vector<S>>(&i->second)){
    result->m_kind=ResArrayKind::Strings;
    for(const auto& value:*strings)result->m_values.emplace_back(value);
   }else if(auto bytes=std::get_if<std::vector<B>>(&i->second)){
    result->m_kind=ResArrayKind::Bytes;
    for(const auto& value:*bytes)result->m_values.emplace_back(value);
   }else return d;
   return ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(result));
  }
 }
 return d;
 }
 void main(){}
 }
