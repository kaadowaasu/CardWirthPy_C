#pragma once

// CardWirthPy のバイナリー InfoCard の読み書きを移植した型付き C++ API。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"
#include "cw/binary/base.hpp"

namespace cw::binary::info {
class InfoCard : public ::cw::binary::base::CWBinaryBase {
public:
    InfoCard() = default;
    virtual ~InfoCard() = default;
    std::optional<std::shared_ptr<::cw::data::CWPyElement>> m_data{};
    ::cw::lib::String m_description{};
    ::cw::lib::String m_imgpath{};
    ::cw::lib::Integer m_type{};
    bool m_nameonly{};
    void initialize(::cw::lib::Value arg_parent, std::shared_ptr<::cw::binary::cwfile::CWFile> arg_f, bool arg_yadodata, bool arg_nameonly, ::cw::lib::String arg_materialdir, bool arg_image_export);
    std::shared_ptr<::cw::data::CWPyElement> get_data() override;
    static void unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f, std::shared_ptr<::cw::data::CWPyElement> arg_data);
};

void main();
}

