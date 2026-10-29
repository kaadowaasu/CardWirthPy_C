#pragma once
#include "cw/binary/base.hpp"
#include "cw/fwd.hpp"

namespace cw::binary::bgimage {
using Color = std::tuple<::cw::lib::Integer,::cw::lib::Integer,::cw::lib::Integer,::cw::lib::Integer>;
class BgImage : public ::cw::binary::base::CWBinaryBase {
public:
    BgImage()=default; ~BgImage() override=default;
    ::cw::lib::Integer m_left{},m_top{},m_width{},m_height{},m_type{};
    ::cw::lib::String m_imgpath{},m_flag{},m_text{},m_fontface{};
    bool m_mask{},m_bold{},m_italic{},m_underline{},m_strike{},m_bordering{},m_vertical{};
    ::cw::lib::Integer m_unknown{},m_fontsize{12},m_btype{-1},m_bwidth{1},m_blend{},m_gradient{};
    Color m_color{0,0,0,255},m_bcolor{255,255,255,255},m_color1{255,255,255,255},m_color2{0,0,0,255};
    std::optional<std::shared_ptr<::cw::data::CWPyElement>> m_data{};
    void initialize(::cw::lib::Value,std::shared_ptr<::cw::binary::cwfile::CWFile>,bool=false);
    std::shared_ptr<::cw::data::CWPyElement> get_data() override;
    static void unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter>,std::shared_ptr<::cw::data::CWPyElement>);
};
std::shared_ptr<::cw::data::CWPyElement> BgImage_get_data_makecolor(::cw::lib::String,Color);
Color BgImage_unconv_getcolor(std::shared_ptr<::cw::data::CWPyElement>,Color);
void main();
}  // namespace cw::binary::bgimage
