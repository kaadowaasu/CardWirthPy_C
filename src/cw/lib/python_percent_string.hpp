#pragma once
#include "cw/lib/encoding.hpp"
#include "cw/lib/python_printable_data.hpp"
#include <algorithm>
#include <limits>
#include <optional>
#include <cstdio>
#include <charconv>
#include <array>
#include <stdexcept>

namespace cw::lib {
namespace python_percent_detail {
inline std::vector<std::uint32_t> codepoints(const String& text) {
    const auto bytes=encoding::utf8_to_utf32(text,encoding::ByteOrder::Little);
    std::vector<std::uint32_t> out;out.reserve(bytes.size()/4);
    for(std::size_t i=0;i+3<bytes.size();i+=4)out.push_back(bytes[i]|(std::uint32_t(bytes[i+1])<<8)|(std::uint32_t(bytes[i+2])<<16)|(std::uint32_t(bytes[i+3])<<24));
    return out;
}
inline String text(const std::vector<std::uint32_t>& points,std::size_t count) {
    Bytes bytes;bytes.reserve(count*4);
    for(std::size_t i=0;i<count;++i)for(unsigned shift=0;shift<32;shift+=8)bytes.push_back(std::uint8_t(points[i]>>shift));
    return encoding::utf32_to_utf8(bytes,encoding::ByteOrder::Little,encoding::ErrorPolicy::Strict,false);
}
inline bool printable(std::uint32_t cp) {
    const auto& ranges=python_printable_data::ranges;
    const auto it=std::lower_bound(ranges.begin(),ranges.end(),cp,[](const auto& r,auto value){return r.last<value;});
    return it!=ranges.end()&&it->first<=cp;
}
inline String repr(const String& value,bool ascii) {
    const auto points=codepoints(value);
    const char quote=value.find('\'')!=String::npos&&value.find('"')==String::npos?'"':'\'';
    String out(1,quote);constexpr char hex[]="0123456789abcdef";
    for(auto cp:points) {
        if(cp==std::uint32_t(quote)||cp=='\\'){out+='\\';out+=char(cp);}
        else if(cp=='\n')out+="\\n";else if(cp=='\r')out+="\\r";else if(cp=='\t')out+="\\t";
        else if((ascii&&cp>=128)||!printable(cp)) {
            const unsigned digits=cp<=0xff?2:cp<=0xffff?4:8;out+='\\';out+=digits==2?'x':digits==4?'u':'U';
            for(unsigned digit=digits;digit>0;--digit)out+=hex[(cp>>(4*(digit-1)))&15];
        } else out+=text(std::vector<std::uint32_t>{cp},1);
    }
    out+=quote;return out;
}
}
// 文字列または整数の引数一つを使う、Python の Unicode の % 書式。名前・
// 確認・金額の各処理で同じパーサーを使う。Python の TypeError/ValueError
// は C++ の invalid_argument/length_error で表す
// 。String は UTF-8 のスカラー値の文字列なので、不正バイトやサロゲート
// は共通の符号化境界で扱い、Python 内部の単独サロゲート文字列は受け入れない。
inline String python_percent_value(const String& format,const String& value, std::optional<Integer> integer) {
    namespace detail=python_percent_detail;
    (void)detail::codepoints(format);
    String out;bool consumed=false;
    for(std::size_t i=0;i<format.size();) {
        if(format[i]!='%'){out+=format[i++];continue;}
        ++i;if(i==format.size())throw std::invalid_argument("incomplete format");
        if(format[i]=='%'){out+='%';++i;continue;}
        bool left=false, zero=false, plus=false, space=false, alternate=false;
        while(i<format.size()&&String("#0- +").find(format[i])!=String::npos){left|=format[i]=='-';zero|=format[i]=='0';plus|=format[i]=='+';space|=format[i]==' ';alternate|=format[i]=='#';++i;}
        const auto number=[&]() {
            std::size_t result=0;
            while(i<format.size()&&format[i]>='0'&&format[i]<='9') {
                const auto digit=std::size_t(format[i++]-'0');
                if(result>(std::size_t((std::numeric_limits<std::ptrdiff_t>::max)())-digit)/10)throw std::length_error("format width or precision too large");
                result=result*10+digit;
            }
            return result;
        };
        if(i<format.size()&&format[i]=='*')throw std::invalid_argument("* wants int");
        const auto width=number();bool has_precision=false;std::size_t precision=0;
        if(i<format.size()&&format[i]=='.'){++i;has_precision=true;if(i<format.size()&&format[i]=='*')throw std::invalid_argument("* wants int");precision=number();}
        if(i<format.size()&&String("hlL").find(format[i])!=String::npos)++i;
        if(i==format.size())throw std::invalid_argument("incomplete format");
        const char conversion=format[i++];
        if(consumed)throw std::invalid_argument("not enough arguments for format string");consumed=true;
        String rendered;
        if (integer && String("diuoxX").find(conversion)!=String::npos) {
            const auto signed_value=*integer;
            const auto magnitude=signed_value<0?std::uint64_t(-(signed_value+1))+1:std::uint64_t(signed_value);
            std::array<char,65> digits{};
            const int base=conversion=='o'?8:(conversion=='x'||conversion=='X'?16:10);
            const auto result=std::to_chars(digits.data(),digits.data()+digits.size(),magnitude,base);
            rendered.assign(digits.data(),result.ptr);
            if(conversion=='X')for(auto& ch:rendered)if(ch>='a'&&ch<='f')ch=char(ch-'a'+'A');
            if(has_precision&&precision>rendered.size()) rendered.insert(0,precision-rendered.size(),'0');
            String prefix=signed_value<0?"-":plus?"+":space?" ":"";
            if(alternate&&base==8)prefix+="0o";
            else if(alternate&&base==16)prefix+=conversion=='X'?"0X":"0x";
            if(zero&&!left&&width>prefix.size()+rendered.size())rendered.insert(0,width-prefix.size()-rendered.size(),'0');
            rendered=prefix+rendered;
        } else if (integer && String("eEfFgG").find(conversion)!=String::npos) {
            String spec="%";
            if(left)spec+='-';if(plus)spec+='+';if(space)spec+=' ';if(alternate)spec+='#';if(zero)spec+='0';
            spec+=std::to_string(width);
            if(has_precision)spec+="."+std::to_string(precision);
            spec+=conversion;
            const auto count=std::snprintf(nullptr,0,spec.c_str(),static_cast<double>(*integer));
            if(count<0)throw std::invalid_argument("invalid floating point format");
            std::vector<char> buffer(static_cast<std::size_t>(count)+1);
            std::snprintf(buffer.data(),buffer.size(),spec.c_str(),static_cast<double>(*integer));
            rendered.assign(buffer.data(),static_cast<std::size_t>(count));
        } else if (integer && conversion=='c') {
            if(*integer<0||*integer>0x10ffff)throw std::out_of_range("%c argument outside Unicode range");
            rendered=detail::text(std::vector<std::uint32_t>{static_cast<std::uint32_t>(*integer)},1);
        } else {
            if(conversion!='s'&&conversion!='r'&&conversion!='a'&&conversion!='c')
                throw std::invalid_argument("unsupported conversion for argument");
            rendered=!integer&&(conversion=='r'||conversion=='a')?detail::repr(value,conversion=='a'):value;
        }
        auto points=detail::codepoints(rendered);
        if(conversion=='c'&&points.size()!=1)throw std::invalid_argument("%c requires a character");
        const auto count=conversion!='c'&&String("sra").find(conversion)!=String::npos&&has_precision?(std::min)(points.size(),precision):points.size();
        rendered=detail::text(points,count);
        const auto padding=width>count?width-count:0;
        if(!left)out.append(padding,' ');out+=rendered;if(left)out.append(padding,' ');
    }
    if(!consumed)throw std::invalid_argument("not all arguments converted during string formatting");
    return out;
}
inline String python_percent_string(const String& format,const String& value) {
    return python_percent_value(format,value,std::nullopt);
}
inline String python_percent_integer(const String& format,Integer value) {
    return python_percent_value(format,std::to_string(value),value);
}

}
