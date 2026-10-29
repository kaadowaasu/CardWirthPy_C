#pragma once

#include "widget.hpp"

#include <memory>
#include <string>

namespace cardwirth::ui {

class Backend {
public:
    virtual ~Backend() = default;

    virtual std::shared_ptr<Button> create_button(const std::string& text) = 0;
    virtual std::shared_ptr<Label> create_label(const std::string& text) = 0;
    virtual std::shared_ptr<Panel> create_panel() = 0;
    virtual std::shared_ptr<Dialog> create_dialog() = 0;

    // メッセージボックスはバックエンドが提供する。 ダイアログ側か
    // らMessageBoxWなどのOS APIを直接呼ばない。
    virtual DialogResult message_box(const std::string& title,
                                     const std::string& text) = 0;
};

std::shared_ptr<Backend> make_headless_backend();

}  // namespace cardwirth::ui
