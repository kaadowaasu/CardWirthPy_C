#pragma once

// UIの実装は一つに統一し、
// 別経路の実装を設けない。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"
#include <mutex>

namespace cw::dialog::skininstall {
using SkinRecord = std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, bool>;

struct InstallOptions {
    // Python版SysMessageの初期チェック状態に対応する。
    bool accepted{true};
    bool overwrite{true};
    bool remove_installed{true};
    bool switch_skin{false};
};

class ProgressObj : public ::cw::lib::Object {
public:
    ::cw::lib::String m_msg{};
    ::cw::lib::Integer m_value{0};
    ::cw::lib::Integer m_maximum{0};
    std::vector<::cw::lib::String> m_errors{};
    std::vector<SkinRecord> m_installed_skindirnames{};
    std::vector<::cw::lib::String> m_all_removes_repl{};
    std::vector<::cw::lib::String> m_all_removes_base{};
    ::cw::lib::String m_tempdir{};
    bool m_cancelled{false};
    bool m_finished{false};
    bool m_cleanup_done{false};
    mutable std::mutex m_mutex{};
    void advance(const ::cw::lib::String& msg, ::cw::lib::Integer amount = 1);
    void set_value(::cw::lib::Integer value);
    void snapshot(::cw::lib::String& msg, ::cw::lib::Integer& value,
                  ::cw::lib::Integer& maximum) const;
    void cancel();
    bool cancelled() const;
};

std::vector<SkinRecord> install_skin(std::vector<::cw::lib::String> paths,
                                     std::shared_ptr<::cw::lib::Window> parent,
                                     bool canswitch = true);
void main();
}
