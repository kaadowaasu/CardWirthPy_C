#include "cw/binary/image.hpp"
#include <stdexcept>

namespace cw::binary::image {
::cw::lib::String g_BINARY_HEADER{"binaryimage://"};
namespace {
constexpr char table[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
int decode(unsigned char c){if(c>='A'&&c<='Z')return c-'A';if(c>='a'&&c<='z')return c-'a'+26;if(c>='0'&&c<='9')return c-'0'+52;if(c=='+')return 62;if(c=='/')return 63;return -1;}
::cw::lib::String encode64(const ::cw::lib::Bytes& d){::cw::lib::String out;out.reserve((d.size()+2)/3*4);std::size_t i=0;for(;i+3<=d.size();i+=3){const auto v=(std::uint32_t(d[i])<<16)|(std::uint32_t(d[i+1])<<8)|d[i+2];out+=table[(v>>18)&63];out+=table[(v>>12)&63];out+=table[(v>>6)&63];out+=table[v&63];}if(d.size()-i==1){const auto v=std::uint32_t(d[i])<<16;out+=table[(v>>18)&63];out+=table[(v>>12)&63];out+="==";}else if(d.size()-i==2){const auto v=(std::uint32_t(d[i])<<16)|(std::uint32_t(d[i+1])<<8);out+=table[(v>>18)&63];out+=table[(v>>12)&63];out+=table[(v>>6)&63];out+='=';}return out;}
::cw::lib::Bytes decode64(const ::cw::lib::String& s) {
    // CPython 3.8の base64.b64decode(str, validate=False)
    // と同様、 パディングが終了を示すのは、一まとまりの二文字目・三文字目の後だけ。
    ::cw::lib::String filtered;
    for (unsigned char c : s) {
        if (c >= 128) throw std::invalid_argument("non-ASCII base64 input");
        if (decode(c) >= 0 || c == '=') filtered.push_back(static_cast<char>(c));
    }
    ::cw::lib::Bytes out;
    unsigned value = 0, bits = 0, position = 0;
    for (std::size_t i = 0; i < filtered.size(); ++i) {
        if (filtered[i] == '=') {
            if (position == 3 || (position == 2 && i+1 < filtered.size() && filtered[i+1] == '='))
                return out;
            continue;
        }
        value = (value << 6) | static_cast<unsigned>(decode(static_cast<unsigned char>(filtered[i])));
        bits += 6;
        position = (position+1) & 3;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>(value >> bits));
            value &= (1U << bits)-1;
        }
    }
    if (bits) throw std::invalid_argument("incorrect base64 padding");
    return out;
}
}

bool path_is_code(::cw::lib::String arg_path)
{
    return arg_path.rfind(g_BINARY_HEADER,0)==0;
}

::cw::lib::Bytes code_to_data(::cw::lib::String arg_code)
{
    if(!path_is_code(arg_code))return {};
    return decode64(arg_code.substr(g_BINARY_HEADER.size()));
}

::cw::lib::String data_to_code(::cw::lib::Bytes arg_data)
{
    if(arg_data.empty())return {};
    return g_BINARY_HEADER+encode64(arg_data);
}

}

