#pragma once

#include "cw/binary/base.hpp"
#include "cw/fwd.hpp"

namespace cw::binary::coupon {

class Coupon : public ::cw::binary::base::CWBinaryBase {
public:
    Coupon() = default;
    Coupon(std::shared_ptr<::cw::binary::base::CWBinaryBase> parent,
           std::shared_ptr<::cw::binary::cwfile::CWFile> file,
           bool yadodata = false, ::cw::lib::Integer dataversion = 5);
    virtual ~Coupon() = default;

    std::shared_ptr<::cw::data::CWPyElement> m_data{};
    ::cw::lib::Integer m_value{};

    void initialize(std::shared_ptr<::cw::binary::base::CWBinaryBase> arg_parent,
                    std::shared_ptr<::cw::binary::cwfile::CWFile> arg_f,
                    bool arg_yadodata = false,
                    ::cw::lib::Integer arg_dataversion = 5);
    std::shared_ptr<::cw::data::CWPyElement> get_data() override;
    static void unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f,
                       std::shared_ptr<::cw::data::CWPyElement> arg_data);
};

void main();

}  // namespace cw::binary::coupon
