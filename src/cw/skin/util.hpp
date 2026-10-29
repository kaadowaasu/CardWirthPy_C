#pragma once

// cw/skin/util.py から移植したスキンの検索・導入モデル。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"
#include <stdexcept>

namespace cw::skin::util {
class ProgressObj : public ::cw::lib::Object {
public:
    ProgressObj() = default;
    virtual ~ProgressObj() = default;
    ::cw::lib::Integer m_file_count{};
    ::cw::lib::Integer m_decompress_count{};
};

class SkinInstallError : public std::runtime_error, public ::cw::lib::Object {
public:
    SkinInstallError() : std::runtime_error("") {}
    explicit SkinInstallError(const ::cw::lib::String& message) : std::runtime_error(message), m_message(message) {}
    virtual ~SkinInstallError() = default;
    ::cw::lib::String m_message{};
    void initialize(::cw::lib::String arg_message);
    const char* what() const noexcept override { return m_message.c_str(); }
};

extern ::cw::lib::Integer g_INSTALL_PROGRESS;
extern ::cw::lib::Integer g_INSTALL_PROGRESS_ARCHIVE;

std::vector<::cw::lib::String> find_skin(::cw::lib::String arg_name, ::cw::lib::String arg_author, ::cw::lib::String arg_skintype = {});
std::optional<std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String>> get_skininfo(::cw::lib::String arg_path);
bool is_skin(::cw::lib::String arg_path);
void _progress_with_msg(::cw::lib::String arg_msg, ::cw::lib::Integer arg_progress = 1);
::cw::lib::String install_skin(::cw::lib::String arg_path, ::cw::lib::String arg_tempdir, ::cw::lib::Callback arg_progress = {});
::cw::lib::String install_skin_decompress(::cw::lib::String arg_path, ::cw::lib::String arg_tempdir, ::cw::lib::Callback arg_startup, ::cw::lib::Callback arg_progress);
void ProgressObj_install_skin___init__(::cw::lib::Value arg_self);
void install_skin_startup(::cw::lib::Integer arg_num);
bool install_skin_progress_arc(::cw::lib::Integer arg_num);
void main();
}

