#pragma once

#include "cw/binary/base.hpp"
#include "cw/fwd.hpp"

namespace cw::binary::event {

class Event : public ::cw::binary::base::CWBinaryBase {
public:
    Event() = default;
    ~Event() override = default;

    std::vector<std::shared_ptr<::cw::binary::content::Content>> m_contents{};
    std::vector<::cw::lib::Integer> m_ignitions{};
    ::cw::lib::String m_keycodes{};
    std::optional<std::shared_ptr<::cw::data::CWPyElement>> m_data{};

    void initialize(::cw::lib::Value arg_parent,
                    std::shared_ptr<::cw::binary::cwfile::CWFile> arg_f,
                    bool arg_yadodata = false);
    std::shared_ptr<::cw::data::CWPyElement> get_data() override;
    static void unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f,
                       std::shared_ptr<::cw::data::CWPyElement> arg_data);
};

class SimpleEvent : public ::cw::binary::base::CWBinaryBase {
public:
    SimpleEvent() = default;
    ~SimpleEvent() override = default;

    std::vector<std::shared_ptr<::cw::binary::content::Content>> m_contents{};
    std::optional<std::shared_ptr<::cw::data::CWPyElement>> m_data{};

    void initialize(::cw::lib::Value arg_parent,
                    std::shared_ptr<::cw::binary::cwfile::CWFile> arg_f,
                    bool arg_yadodata = false);
    std::shared_ptr<::cw::data::CWPyElement> get_data() override;
    static void unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f,
                       std::shared_ptr<::cw::data::CWPyElement> arg_data);
};

void main();

}  // namespace cw::binary::event
