#include "cw/binary/coupon.hpp"

#include "cw/binary/cwfile.hpp"
#include "cw/data.hpp"
#include "cw/lib/python_integer.hpp"

#include <cctype>
#include <regex>
#include <stdexcept>

namespace cw::binary::coupon {

Coupon::Coupon(std::shared_ptr<::cw::binary::base::CWBinaryBase> parent,
               std::shared_ptr<::cw::binary::cwfile::CWFile> file,
               bool yadodata, ::cw::lib::Integer dataversion) {
    initialize(std::move(parent), std::move(file), yadodata, dataversion);
}

void Coupon::initialize(std::shared_ptr<::cw::binary::base::CWBinaryBase> arg_parent,
                        std::shared_ptr<::cw::binary::cwfile::CWFile> arg_f,
                        bool arg_yadodata, ::cw::lib::Integer arg_dataversion) {
    CWBinaryBase::initialize(arg_parent, arg_f, arg_yadodata);
    m_xmltype = "Coupon";
    if (arg_f) {
        m_name = arg_f->string();
        m_value = arg_f->dword();
        if (arg_dataversion <= 4) {
            static const std::regex old_gene_pattern("^＠Ｇ[01]{10}-[0-9]+$");
            if (std::regex_match(m_name, old_gene_pattern)) {
                const auto separator = m_name.rfind('-');
                m_value = std::stoll(m_name.substr(separator + 1));
                m_name.resize(separator);
            }
        }
    } else {
        m_name.clear();
        m_value = 0;
    }
    m_data.reset();
}

std::shared_ptr<::cw::data::CWPyElement> Coupon::get_data() {
    if (!m_data) {
        m_data = ::cw::data::make_element("Coupon", m_name);
        m_data->set("value", std::to_string(m_value));
    }
    return m_data;
}

void Coupon::unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f,
                    std::shared_ptr<::cw::data::CWPyElement> arg_data) {
    if (!arg_f || !arg_data) throw std::invalid_argument("Coupon::unconv requires file and data");
    const auto value_text = arg_data->get("value", "");
    const auto value = ::cw::lib::require_python_integer(value_text);
    arg_f->write_string(arg_data->m_text);
    arg_f->write_dword(value);
}

void main() {}

}  // namespace cw::binary::coupon
