#include "backend.hpp"

namespace cardwirth::ui {
namespace {

class HeadlessBackend final : public Backend {
public:
    std::shared_ptr<Button> create_button(const std::string& text) override {
        return std::make_shared<Button>(text);
    }
    std::shared_ptr<Label> create_label(const std::string& text) override {
        return std::make_shared<Label>(text);
    }
    std::shared_ptr<Panel> create_panel() override {
        return std::make_shared<Panel>();
    }
    std::shared_ptr<Dialog> create_dialog() override {
        return std::make_shared<Dialog>();
    }
    DialogResult message_box(const std::string&, const std::string&) override {
        // ヘッドレス実装は機能の有無を表すもので、確認操作の代わりにはな
        // らない。 UIがないことをOKとして扱うと、誤ってこの実装が
        // 接続された製品で、 破壊的な操作が無確認で承認されてしまう。
        return DialogResult::Cancel;
    }
};

}  // namespace

std::shared_ptr<Backend> make_headless_backend() {
    return std::make_shared<HeadlessBackend>();
}

}  // namespace cardwirth::ui
