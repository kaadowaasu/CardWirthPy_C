#include "cw/binary/bgimage.hpp"
#include "cw/binary/cwfile.hpp"
#include "cw/data.hpp"
#include "cw/lib/python_integer.hpp"
#include "cw/util.hpp"
#include <cctype>
#include <cmath>
#include <stdexcept>

namespace cw::binary::bgimage { namespace {
using Base=::cw::binary::base::CWBinaryBase;using Element=::cw::data::CWPyElement;using ElementPtr=std::shared_ptr<Element>;using Integer=::cw::lib::Integer;using String=::cw::lib::String;
// Pythonの getint はネイティブIntegerの範囲外でも int(float(text))
// を保持する。 固定幅の出力項目だけでは、書き込み時まで格納不能の
// 番兵値を残す。 通常の値・エラーには共通の getint を使う。
Integer attr(const ElementPtr& e, const String& key, Integer fallback, bool required=false) {
    const auto text = e->get(key, "");
    if (!text.empty()) {
        try {
            const auto value = ::cw::data::parse_python_float(text);
            const auto limit = std::ldexp(1.0, std::numeric_limits<Integer>::digits);
            if (std::isfinite(value) && value < -limit)
                return std::numeric_limits<Integer>::min();
            if (std::isfinite(value) && value >= limit)
                return std::numeric_limits<Integer>::max();
        } catch (const std::exception&) {
            // 下で、共通の getint にPython版と同じXMLエラーの文脈を生成させる。
        }
    }
    return e->getint(".", ::cw::lib::Value(key),
                     required ? std::nullopt : std::optional<Integer>{fallback});
}
bool boolean(const String&s){return ::cw::util::str2bool(s);}
String bt(bool v){return v?"True":"False";}
std::shared_ptr<Base> parent(const ::cw::lib::Value&v){if(const auto*o=std::get_if<::cw::lib::ObjectPtr>(&v.storage()))return std::dynamic_pointer_cast<Base>(*o);return nullptr;}
void rgba(const std::shared_ptr<::cw::binary::cwfile::CWFileWriter>&f,const Color&c){f->write_ubyte(std::get<0>(c));f->write_ubyte(std::get<1>(c));f->write_ubyte(std::get<2>(c));f->write_ubyte(std::get<3>(c));}
}

std::shared_ptr<Element> BgImage_get_data_makecolor(String tag,Color c){auto e=::cw::data::make_element(std::move(tag));e->set("r",std::to_string(std::get<0>(c)));e->set("g",std::to_string(std::get<1>(c)));e->set("b",std::to_string(std::get<2>(c)));e->set("a",std::to_string(std::get<3>(c)));return e;}
Color BgImage_unconv_getcolor(ElementPtr e,Color d){return {attr(e,"r",std::get<0>(d)),attr(e,"g",std::get<1>(d)),attr(e,"b",std::get<2>(d)),attr(e,"a",std::get<3>(d))};}

void BgImage::initialize(::cw::lib::Value pv,std::shared_ptr<::cw::binary::cwfile::CWFile> f,bool y){if(!f)throw std::invalid_argument("BgImage requires input");std::optional<std::shared_ptr<Base>> p;if(auto q=parent(pv))p=q;Base::initialize(p,f,y);m_xmltype="BgImage";m_data.reset();
    m_left=f->dword();m_top=f->dword();const auto ew=f->dword();Integer ver=2;if(ew<=39999)m_width=ew;else if(ew<=49999){ver=4;m_width=ew-40000;}else if(ew<=59999){ver=5;m_width=ew-50000;}else{ver=6;m_width=ew-60000;}m_height=f->dword();
    if(ver<=5){m_type=0;m_imgpath=::cw::util::validate_filepath(f->string());m_mask=f->boolean();if(ver>2){m_flag=f->string();m_unknown=f->byte();}else{m_flag="";m_unknown=0;}return;}
    const auto t=f->byte();if(t==2){m_type=1;m_mask=f->boolean();m_text=f->string(true);m_fontface=f->string();m_fontsize=f->dword();m_color={f->ubyte(),f->ubyte(),f->ubyte(),f->ubyte()};const auto s=f->byte();m_bold=s&1;m_italic=s&2;m_underline=s&4;m_strike=s&8;m_bordering=s&16;m_vertical=s&32;
        if(m_bordering){m_btype=f->byte();m_bcolor={f->ubyte(),f->ubyte(),f->ubyte(),f->ubyte()};m_bwidth=f->dword();}else{m_btype=-1;m_bcolor={255,255,255,255};m_bwidth=1;}(void)f->byte();(void)f->dword();(void)f->dword();(void)f->byte();m_flag=f->string();m_unknown=f->byte();
    }else if(t==3){m_type=2;m_blend=f->byte();m_gradient=f->byte();auto b=f->ubyte(),g=f->ubyte(),r=f->ubyte(),a=f->ubyte();m_color1={r,g,b,a};if(m_gradient){b=f->ubyte();g=f->ubyte();r=f->ubyte();a=f->ubyte();m_color2={r,g,b,a};}else m_color2={255,255,255,255};m_flag=f->string();m_unknown=f->byte();}else throw std::invalid_argument("Background type: "+std::to_string(t));}

// BgImage はローカルの直列化が成功してから m_data
// を公開する。 Python版の早い self.data
// 代入とは異なり、不完全なツリーを次回用にキャッシュしない。
std::shared_ptr<Element> BgImage::get_data(){if(m_data&&*m_data)return *m_data;ElementPtr d;if(m_type==0){d=::cw::data::make_element("BgImage");d->set("mask",bt(m_mask));d->append(::cw::data::make_element("ImagePath",get_materialpath(m_imgpath)));}
    else if(m_type==1){d=::cw::data::make_element("TextCell");d->append(::cw::data::make_element("Text",m_text));auto font=::cw::data::make_element("Font",m_fontface);font->set("size",std::to_string(m_fontsize));font->set("bold",bt(m_bold));font->set("italic",bt(m_italic));font->set("underline",bt(m_underline));font->set("strike",bt(m_strike));d->append(font);d->append(::cw::data::make_element("Vertical",bt(m_vertical)));d->append(BgImage_get_data_makecolor("Color",m_color));if(m_bordering){auto b=::cw::data::make_element("Bordering");b->set("type",conv_borderingtype(m_btype));b->set("width",std::to_string(m_bwidth));b->append(BgImage_get_data_makecolor("Color",m_bcolor));d->append(b);}d->append(::cw::data::make_element("UpdateType","Fixed"));}
    else if(m_type==2){d=::cw::data::make_element("ColorCell");d->append(::cw::data::make_element("BlendMode",conv_blendmode(m_blend)));d->append(BgImage_get_data_makecolor("Color",m_color1));if(m_gradient){auto g=::cw::data::make_element("Gradient");g->set("direction",conv_gradientdir(m_gradient));g->append(BgImage_get_data_makecolor("EndColor",m_color2));d->append(g);}}else throw std::invalid_argument(std::to_string(m_type)+" is Invalid Background Type");
    d->append(::cw::data::make_element("Flag",m_flag));auto l=::cw::data::make_element("Location");l->set("left",std::to_string(m_left));l->set("top",std::to_string(m_top));d->append(l);auto s=::cw::data::make_element("Size");s->set("width",std::to_string(m_width));s->set("height",std::to_string(m_height));d->append(s);m_data=d;return d;}

void BgImage::unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> f,ElementPtr d){if(!f||!d)throw std::invalid_argument("BgImage::unconv requires file and data");Integer left=0,top=0,width=0,height=0,unknown=0;String flag,img,text,font;bool mask=false;Integer fontsize=12,btype=-1,bwidth=1,blend=0,gradient=0;Color color{0,0,0,255},bc{255,255,255,255},c1{255,255,255,255},c2{0,0,0,255};bool bold=false,italic=false,underline=false,strike=false,vertical=false;if(!d->get("cellname","").empty())f->check_wsnversion("1","セル名称");mask=d->getbool(".",::cw::lib::Value("mask"),false);
    for(const auto&e:d->m_children)if(e){if(e->m_tag=="Flag")flag=e->m_text;else if(e->m_tag=="Location"){left=attr(e,"left",0,true);top=attr(e,"top",0,true);}else if(e->m_tag=="Size"){width=attr(e,"width",0,true);height=attr(e,"height",0,true);}else if(d->m_tag=="BgImage"&&e->m_tag=="ImagePath")img=e->m_text;
        else if(d->m_tag=="TextCell"){f->check_version(1.50,"テキストセル");if(e->m_tag=="Text")text=e->m_text;else if(e->m_tag=="Font"){font=e->m_text;fontsize=attr(e,"size",fontsize);for(const auto&x:e->m_children)if(x&&x->m_tag=="Color")color=BgImage_unconv_getcolor(x,color);bold=e->getbool(".",::cw::lib::Value("bold"),bold);italic=e->getbool(".",::cw::lib::Value("italic"),italic);underline=e->getbool(".",::cw::lib::Value("underline"),underline);strike=e->getbool(".",::cw::lib::Value("strike"),strike);}else if(e->m_tag=="Vertical")vertical=boolean(e->m_text);else if(e->m_tag=="Bordering"){btype=Base::unconv_borderingtype(e->get("type","None"));bwidth=attr(e,"width",1);for(const auto&x:e->m_children)if(x&&x->m_tag=="Color")bc=BgImage_unconv_getcolor(x,bc);}else if(e->m_tag=="UpdateType"&&e->m_text!="Fixed")f->check_wsnversion("4","テキストセルの更新タイプ");else if(e->m_tag=="Antialias"&&e->getbool(".",::cw::lib::Value(false),std::nullopt))f->check_wsnversion("4","テキストセルのアンチエイリアス");}
        else if(d->m_tag=="ColorCell"){f->check_version(1.50,"カラーセル");if(e->m_tag=="BlendMode")blend=Base::unconv_blendmode(e->m_text);else if(e->m_tag=="Color")c1=BgImage_unconv_getcolor(e,c1);else if(e->m_tag=="Gradient"){gradient=Base::unconv_gradientdir(e->get("direction","None"));for(const auto&x:e->m_children)if(x&&x->m_tag=="EndColor")c2=BgImage_unconv_getcolor(x,c2);}}else if(d->m_tag=="PCCell")f->check_wsnversion("1","プレイヤーキャラクターセル");}
    if(d->m_tag=="BgImage"){f->write_dword(left);f->write_dword(top);f->write_dword(::cw::lib::add_cardwirth_resource_id_bias(width,50000));f->write_dword(height);f->write_string(img);f->write_bool(mask);f->write_string(flag);f->write_byte(unknown);}else if(d->m_tag=="TextCell"){f->check_version(1.50,"テキストセル");f->write_dword(left);f->write_dword(top);f->write_dword(::cw::lib::add_cardwirth_resource_id_bias(width,60000));f->write_dword(height);f->write_bool(mask);f->write_string(text,true);f->write_string(font);f->write_dword(fontsize);rgba(f,color);Integer style=(bold?1:0)|(italic?2:0)|(underline?4:0)|(strike?8:0)|(btype!=-1?16:0)|(vertical?32:0);f->write_byte(style);if(btype!=-1){f->write_byte(btype);rgba(f,bc);f->write_dword(bwidth);}f->write_byte(100);f->write_dword(0);f->write_dword(0);f->write_byte(vertical?2:0);f->write_string(flag);f->write_byte(unknown);}else if(d->m_tag=="ColorCell"){f->check_version(1.50,"カラーセル");f->write_byte(blend);f->write_byte(gradient);f->write_ubyte(std::get<2>(c1));f->write_ubyte(std::get<1>(c1));f->write_ubyte(std::get<0>(c1));f->write_ubyte(std::get<3>(c1));if(gradient){f->write_ubyte(std::get<2>(c2));f->write_ubyte(std::get<1>(c2));f->write_ubyte(std::get<0>(c2));f->write_ubyte(std::get<3>(c2));}f->write_string(flag);f->write_byte(unknown);}else if(d->m_tag=="PCCell")f->check_wsnversion("1","プレイヤーキャラクターセル");}
void main(){}
}  // namespace cw::binary::bgimage
