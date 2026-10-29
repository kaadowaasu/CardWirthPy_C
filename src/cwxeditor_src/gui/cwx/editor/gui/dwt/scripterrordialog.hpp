#pragma once

// cwx/editor/gui/dwt/scripterrordialog.d
// のC++20移植。 DWTのシェル・リスナーを
// 共通のダイアログ・部品で表す。 エラー文字列
// は例外の寿命と独立して保持し、クリップボード
// 操作はlib::Servicesへ委譲する。

#include "cwxeditor_src/core/cwx/script.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dprops.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/textdialog.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dmenu.hpp"
#include "cwxeditor_src/lib/clipboard.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/dialogs.hpp"
#include "cwxeditor_src/lib/services.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::scripterrordialog {

/// 通常DのPropsから渡される表示値。明示的に持つこと
/// で、翻訳済み・ヘッドレスの画面でも使えるようにする。
struct ScriptErrorPresentation {
    std::string title{"CWXスクリプトエラー"};
    std::string heading{"CWXスクリプトのコンパイル中にエラーが発生しました。"};
    std::string too_many_errors{"エラーが100件を超えたため、スクリプトの解析を終了します。"};
    std::string close_label{"OK"};
    ::cwxeditor::lib::ResourceId icon{};
    ::cwxeditor::lib::Size size{};
    std::size_t tab_width{4};
    std::optional<::cwxeditor::lib::FontSpec> font;
    std::function<std::function<void()>(
        const std::shared_ptr<::cwxeditor::lib::Window>&)> bind_size;
};

[[nodiscard]] ScriptErrorPresentation presentationFromProps(const dprops::Props& prop);
[[nodiscard]] ScriptErrorPresentation presentationFromProps(dprops::Props& prop);

class ScriptErrorDialog final {
public:
    using Services = ::cwxeditor::lib::Services;
    using Widget = ::cwxeditor::lib::Widget;
    using Window = ::cwxeditor::lib::Window;
    using Dialog = ::cwxeditor::lib::Dialog;
    using Container = ::cwxeditor::lib::Container;
    using Panel = ::cwxeditor::lib::Panel;
    using TextEdit = ::cwxeditor::lib::TextEdit;
    using Button = ::cwxeditor::lib::Button;
    using DialogResult = ::cwxeditor::lib::DialogResult;
    using ScriptException = ::cwxeditor::cwx::script::CWXScriptException;
    using ScriptError = ::cwxeditor::cwx::script::CWXSError;
    using CompileOption = ::cwxeditor::cwx::script::CompileOption;

    ScriptErrorDialog(Services services, std::shared_ptr<Widget> parent,
                      const ScriptException& exception, std::string base,
                      CompileOption option = {},
                      ScriptErrorPresentation presentation = {});

    ScriptErrorDialog(const ScriptException& exception, std::string base,
                      CompileOption option = {},
                      ScriptErrorPresentation presentation = {})
        : ScriptErrorDialog({}, {}, exception, std::move(base), option,
                            std::move(presentation)) {}

    ScriptErrorDialog(const ScriptErrorDialog&) = delete;
    ScriptErrorDialog& operator=(const ScriptErrorDialog&) = delete;
    ScriptErrorDialog(ScriptErrorDialog&&) = delete;
    ScriptErrorDialog& operator=(ScriptErrorDialog&&) = delete;
    ~ScriptErrorDialog();

    [[nodiscard]] bool noScenario() const noexcept { return true; }

    /// 既存の共通の所有コンテナー内に部品を作る。
    void setup(Container& area);
    /// このダイアログへ接続した専用パネルを作る。
    void setup();

    /// 読み取り専用の結果欄に表示する文字列。
    [[nodiscard]] const std::string& text() const noexcept { return display_text_; }
    [[nodiscard]] const std::string& displayText() const noexcept {
        return display_text_;
    }
    /// コールバック形式のエラー報告にも、同じ翻訳済みの診断を使う。
    [[nodiscard]] static std::string formatError(
        const ScriptException& exception, const ::cwxeditor::cwx::script::Props* prop,
        std::string_view base, const CompileOption& option = {});
    [[nodiscard]] const std::string& sourceText() const noexcept { return source_text_; }
    [[nodiscard]] const std::string& baseText() const noexcept { return base_; }
    [[nodiscard]] const std::vector<ScriptError>& errors() const noexcept {
        return errors_;
    }
    [[nodiscard]] bool over100() const noexcept { return over100_; }
    [[nodiscard]] const CompileOption& compileOption() const noexcept { return option_; }
    [[nodiscard]] const ScriptErrorPresentation& presentation() const noexcept {
        return presentation_;
    }

    /// 全選択、またはバイト単位のUTF-8の選択範囲を置き換える。
    void selectAll();
    void setSelection(std::size_t start, std::size_t length);
    [[nodiscard]] std::string selectedText() const;
    [[nodiscard]] std::size_t selectionStart() const noexcept;
    [[nodiscard]] std::size_t selectionLength() const noexcept;

    /// 選択文字列を共通のクリップボードサービスでコピーする。
    [[nodiscard]] bool canCopy() const noexcept;
    [[nodiscard]] bool copySelection();
    [[nodiscard]] bool copy() { return copySelection(); }

    /// DWT形式のモードレスのエラー画面として表示する。
    [[nodiscard]] DialogResult show();
    /// Dと同じく、このモードレス画面のopen()はfalseを返す。
    [[nodiscard]] bool open();
    [[nodiscard]] bool close();
    void close(DialogResult result);
    /// Dの親のDisposeListener/forceCancelに対応する終了処理。
    void notifyParentClosed();

    [[nodiscard]] const Services& services() const noexcept { return services_; }
    [[nodiscard]] const std::shared_ptr<Widget>& parent() const noexcept {
        return parent_;
    }
    [[nodiscard]] const std::shared_ptr<Window>& owner() const noexcept { return owner_; }
    [[nodiscard]] const std::shared_ptr<Dialog>& dialog() const noexcept {
        return dialog_;
    }
    [[nodiscard]] const std::shared_ptr<TextEdit>& viewer() const noexcept {
        return viewer_;
    }
    [[nodiscard]] const std::shared_ptr<Button>& closeButton() const noexcept {
        return close_button_;
    }
    [[nodiscard]] DialogResult result() const noexcept {
        return dialog_ ? dialog_->result : DialogResult::None;
    }
    [[nodiscard]] bool initialized() const noexcept { return initialized_; }
    [[nodiscard]] bool closed() const noexcept { return closed_; }
    [[nodiscard]] bool visible() const noexcept {
        return dialog_ && dialog_->visible();
    }

private:
    Services services_{};
    std::shared_ptr<Widget> parent_{};
    std::shared_ptr<Window> owner_{};
    std::shared_ptr<Dialog> dialog_{};
    std::shared_ptr<Panel> area_{};
    std::shared_ptr<TextEdit> viewer_{};
    dmenu::TextMenuModifyPtr text_menu_{};
    std::shared_ptr<Button> close_button_{};
    std::string source_text_{};
    std::vector<ScriptError> errors_{};
    bool over100_{};
    std::string base_{};
    CompileOption option_{};
    ScriptErrorPresentation presentation_{};
    std::function<void()> release_size_binding_{};
    std::string display_text_{};
    bool initialized_{};
    bool closed_{};
    std::shared_ptr<void> lifetime_{std::make_shared<int>(0)};

    static std::string formatDisplay(
        std::string_view heading, const std::vector<ScriptError>& errors,
        bool over100, std::string_view over100Message,
        std::string_view base, std::string_view source,
        const CompileOption& option);
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::scripterrordialog
