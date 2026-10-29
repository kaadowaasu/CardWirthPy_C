#pragma once

// CardWirthPy のバイナリー Package の読み書きを移植した型付き C++ API。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"
#include "cw/binary/base.hpp"

namespace cw::binary::package {
class Package : public ::cw::binary::base::CWBinaryBase {
public:
    Package() = default;
    virtual ~Package() = default;
    std::optional<std::shared_ptr<::cw::data::CWPyElement>> m_data{};
    std::vector<std::shared_ptr<::cw::binary::event::SimpleEvent>> m_events{};
    ::cw::lib::Integer m_type{7};
    bool m_nameonly{};
    void initialize(::cw::lib::Value arg_parent, std::shared_ptr<::cw::binary::cwfile::CWFile> arg_f, bool arg_yadodata, bool arg_nameonly, ::cw::lib::String arg_materialdir, bool arg_image_export);
    std::shared_ptr<::cw::data::CWPyElement> get_data() override;
    static void unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f, std::shared_ptr<::cw::data::CWPyElement> arg_data);
};

void main();
}

