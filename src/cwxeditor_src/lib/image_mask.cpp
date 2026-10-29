#include "image_mask.hpp"
#include "cw/lib/simd_image.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace cwxeditor::lib {
namespace {
void change_alpha(ImageData& image,std::uint8_t value,bool replace){
    for(auto& frame:image.frames){
        const auto count64=std::uint64_t(frame.pixel_width)*frame.pixel_height;
        if(!count64 || count64>std::numeric_limits<std::size_t>::max()/4)continue;
        const auto count=static_cast<std::size_t>(count64);
        std::size_t stride{};
        switch(frame.pixel_format){case PixelFormat::Gray8:stride=1;break;case PixelFormat::RGB24:case PixelFormat::BGR24:stride=3;break;case PixelFormat::RGBA32:case PixelFormat::BGRA32:stride=4;break;default:continue;}
        if(frame.pixels.size()<count*stride)continue;
        if(stride<4){
            std::vector<std::byte> rgba(count*4);
            for(std::size_t i=0;i<count;++i){
                const auto* source=frame.pixels.data()+i*stride;
                if(stride==1)rgba[i*4]=rgba[i*4+1]=rgba[i*4+2]=source[0];
                else for(std::size_t c=0;c<3;++c)rgba[i*4+c]=source[frame.pixel_format==PixelFormat::BGR24 ? 2-c : c];
                rgba[i*4+3]=std::byte(255);
            }
            frame.pixels=std::move(rgba);frame.pixel_format=PixelFormat::RGBA32;
        }
        auto* pixels=reinterpret_cast<std::uint8_t*>(frame.pixels.data());
        if(replace) cw::lib::simd_image::force_alpha(pixels,count,value);
        else cw::lib::simd_image::multiply_alpha(pixels,count,value);
    }
}
}
void apply_image_opacity(ImageData& image,std::uint8_t opacity){if(opacity!=255)change_alpha(image,opacity,false);}
void set_image_alpha(ImageData& image,std::uint8_t alpha){change_alpha(image,alpha,true);}

namespace {
bool color_key(ImageFrame& frame, std::int64_t x, std::int64_t y,
               bool preserve_explicit_alpha, std::optional<Color> explicit_key) {
    std::size_t stride{};
    switch (frame.pixel_format) {
    case PixelFormat::Gray8: stride = 1; break;
    case PixelFormat::RGB24:
    case PixelFormat::BGR24: stride = 3; break;
    case PixelFormat::RGBA32:
    case PixelFormat::BGRA32: stride = 4; break;
    default: return false;
    }
    const auto count64 = static_cast<std::uint64_t>(frame.pixel_width) * frame.pixel_height;
    if (!count64 || count64 > std::numeric_limits<std::size_t>::max() / 4U)
        return false;
    const auto count = static_cast<std::size_t>(count64);
    if (frame.pixels.size() < count * stride) return false;
    if (preserve_explicit_alpha && stride == 4U) {
        for (std::size_t index = 0; index < count; ++index)
            if (frame.pixels[index * 4U + 3U] != std::byte{255}) return true;
    }
    const auto rgb = [&](std::size_t index) {
        const auto* pixel = frame.pixels.data() + index * stride;
        if (stride == 1U) return std::array{pixel[0], pixel[0], pixel[0]};
        if (frame.pixel_format == PixelFormat::BGR24 ||
            frame.pixel_format == PixelFormat::BGRA32)
            return std::array{pixel[2], pixel[1], pixel[0]};
        return std::array{pixel[0], pixel[1], pixel[2]};
    };
    x = std::clamp<std::int64_t>(x, 0, frame.pixel_width - 1U);
    y = std::clamp<std::int64_t>(y, 0, frame.pixel_height - 1U);
    const auto key = explicit_key
        ? std::array{std::byte(explicit_key->red), std::byte(explicit_key->green), std::byte(explicit_key->blue)}
        : rgb(static_cast<std::size_t>(y) * frame.pixel_width + x);
    if (stride == 4U) {
        // 元のチャンネル順とキー色以外の全アルファを保持する。 アンチエイ
        // リアスの縁、アニメーション情報、論理サイズ・元画像サイズも含む。
        const bool bgra=frame.pixel_format==PixelFormat::BGRA32;
        cw::lib::simd_image::colorkey_alpha_zero(
            reinterpret_cast<std::uint8_t*>(frame.pixels.data()),count,
            std::to_integer<std::uint8_t>(key[bgra?2:0]),
            std::to_integer<std::uint8_t>(key[1]),
            std::to_integer<std::uint8_t>(key[bgra?0:2]));
    } else {
        std::vector<std::byte> rgba(count * 4U);
        for (std::size_t index = 0; index < count; ++index) {
            const auto color = rgb(index);
            std::copy(color.begin(), color.end(), rgba.begin() + index * 4U);
            rgba[index * 4U + 3U] = color == key ? std::byte{} : std::byte{255};
        }
        frame.pixels = std::move(rgba);
        frame.pixel_format = PixelFormat::RGBA32;
    }
    return true;
}

} // namespace
bool apply_image_color_key(ImageFrame& frame, std::int64_t x, std::int64_t y,
                           bool preserve_explicit_alpha) {
    return color_key(frame,x,y,preserve_explicit_alpha,{});
}
bool apply_image_color_key(ImageFrame& frame, Color key) {
    return color_key(frame,0,0,false,key);
}
void apply_image_color_key(ImageData& image, Color key) {
    for(auto& frame:image.frames) apply_image_color_key(frame,key);
}

void apply_image_color_key(ImageData& image, std::int64_t x, std::int64_t y,
                           bool preserve_explicit_alpha) {
    for (auto& frame : image.frames)
        apply_image_color_key(frame, x, y, preserve_explicit_alpha);
}

ImageSourceTransparency png_source_transparency(std::span<const std::byte> bytes) {
    ImageSourceTransparency result;
    constexpr std::array<std::uint8_t,8> signature{137,80,78,71,13,10,26,10};
    if(bytes.size()<8) return result;
    const auto u8=[&](std::size_t i){return std::to_integer<std::uint8_t>(bytes[i]);};
    for(std::size_t i=0;i<8;++i) if(u8(i)!=signature[i])return result;
    const auto u32=[&](std::size_t i){return (std::uint32_t(u8(i))<<24)|(std::uint32_t(u8(i+1))<<16)|(std::uint32_t(u8(i+2))<<8)|u8(i+3);};
    const auto u16=[&](std::size_t i){return (std::uint16_t(u8(i))<<8)|u8(i+1);};
    std::array<Color,256> palette{};
    std::size_t colors{};
    int type=-1, depth{};
    bool trns=false;
    for(std::size_t pos=8;pos+12<=bytes.size();) {
        const auto size=u32(pos); const auto tag=u32(pos+4); const auto data=pos+8;
        if(pos==8 && tag!=0x49484452U)return {};
        if(tag==0x49444154U) return type<0 || (type==3 && !colors) ? ImageSourceTransparency{} : result; // IDAT
        if(size>bytes.size()-pos-12) return {};
        // CRCは付加情報の整合性検査であり、二つ目の画素デコーダーではない。 特
        // に、無視した不正なtRNSによって、元画像に alphaData がある
        // と誤判定しない。 無関係な補助データの整合性は既存のデコーダーに任せる。
        const bool metadata=tag==0x49484452U||tag==0x504c5445U||tag==0x74524e53U||tag==0x6163544cU;
        if(metadata) {
            std::uint32_t crc=0xffffffffU;
            for(std::size_t i=pos+4;i<data+size;++i) {
                crc^=u8(i);for(unsigned bit=0;bit<8;++bit)
                    crc=(crc>>1)^(0xedb88320U & (0U-(crc&1U)));
            }
            if((~crc)!=u32(data+size))return {};
        }
        if(tag==0x49484452U) { // IHDR
            if(type!=-1 || size!=13 || !u32(data) || !u32(data+4) || u32(data)>0x7fffffffU || u32(data+4)>0x7fffffffU)return {};
            depth=u8(data+8);type=u8(data+9);
            const bool valid= type==0 ? (depth==1||depth==2||depth==4||depth==8||depth==16)
                : type==3 ? (depth==1||depth==2||depth==4||depth==8)
                : (type==2||type==4||type==6) && (depth==8||depth==16);
            if(!valid || u8(data+10)!=0 || u8(data+11)!=0 || u8(data+12)>1)return {};
            result.kind=(type==4||type==6)?ImageTransparency::AlphaData:ImageTransparency::None;
        } else if(tag==0x504c5445U) { // PLTE
            if(type<0 || colors || trns || !size || size>768 || size%3 || (type==3 && size/3>(1U<<depth)))return {};
            colors=size/3;
            for(std::size_t i=0;i<colors;++i)palette[i]={u8(data+i*3),u8(data+i*3+1),u8(data+i*3+2),255};
        } else if(tag==0x74524e53U) { // tRNS
            if(type<0 || trns)return {};trns=true;
            if(type==3) {
                if(!colors || !size || size>colors)return {};
                std::size_t nonopaque{}, index{};
                for(std::size_t i=0;i<size;++i)if(u8(data+i)!=255){++nonopaque;index=i;}
                if(nonopaque==1 && u8(data+index)==0) {
                    result.kind=ImageTransparency::PaletteKey;result.palette_index=static_cast<std::uint16_t>(index);result.key=palette[index];
                } else if(nonopaque)result.kind=ImageTransparency::AlphaData;
            } else if(type==0 && size==2) {
                const auto sample=u16(data);
                if(depth!=16 && sample>=(1U<<depth))return {};
                const auto value=static_cast<std::uint8_t>(depth==16 ? sample>>8 : sample*255/((1U<<depth)-1));
                result.kind=ImageTransparency::PaletteKey;result.palette_index=static_cast<std::uint16_t>(depth==16?value:sample);result.key=Color{value,value,value,255};
            } else if(type==2 && size==6) {
                if(depth==8 && (u16(data)>255 || u16(data+2)>255 || u16(data+4)>255))return {};
                const auto channel=[&](std::size_t i){return static_cast<std::uint8_t>(depth==16?u16(i)>>8:u16(i));};
                result.kind=ImageTransparency::DirectKey;result.key=Color{channel(data),channel(data+2),channel(data+4),255};
            } else return {};
        } else if(tag==0x6163544cU) { // IDAT より前の acTL。
            if(result.animated || size!=8 || !u32(data))return {};result.animated=true;
        } else if(tag==0x49454e44U || !(tag&0x20000000U)) return {}; // 画像データなし、または未知の必須チャンク。
        pos+=static_cast<std::size_t>(size)+12;
    }
    return {};
}

}  // namespace cwxeditor::lib
