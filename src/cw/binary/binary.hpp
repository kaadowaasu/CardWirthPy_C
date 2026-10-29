#pragma once

#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"

namespace cw::binary {
class ConvertingThread : public ::cw::lib::Thread {
public:
    ConvertingThread() = default;
    ~ConvertingThread() override;
    bool m_complete{};
    ::cw::lib::Value m_cwdata{};
    ::cw::lib::String m_path{};
    void initialize(::cw::lib::Value arg_cwdata);
    void run();
};

extern std::vector<::cw::lib::String> g_py_all;

void main();
}

