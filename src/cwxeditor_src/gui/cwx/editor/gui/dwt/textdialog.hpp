#pragma once

// cwx/editor/gui/dwt/textdialog.d
// の移植可能なC++20実装。 SWT/DWTのハ
// ンドルを公開せず、TextDialogが共通Dialog
// とTextEditを所有し、 ホストの機能はlib::Services
// から受け取る。ネイティブ・ヘッドレ
// スなどで同じ状態と結果を扱える。

#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/dialogs.hpp"
#include "cwxeditor_src/lib/services.hpp"
#include "cwxeditor_src/lib/text.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dmenu.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::dprops { class Props; }

namespace cwxeditor::cwx::editor::gui::dwt::textdialog {

class TextDialog final {
public:
    using Services = ::cwxeditor::lib::Services;
    using Window = ::cwxeditor::lib::Window;
    using Dialog = ::cwxeditor::lib::Dialog;
    using Container = ::cwxeditor::lib::Container;
    using TextEdit = ::cwxeditor::lib::TextEdit;
    using DialogResult = ::cwxeditor::lib::DialogResult;

    TextDialog(Services services, std::shared_ptr<Window> parent,
               std::string title, ::cwxeditor::lib::ResourceId icon,
               std::string text, bool readOnly = true,
               ::cwxeditor::lib::Size size = {}, dprops::Props* prop = nullptr);

    TextDialog(std::string title, std::string text, bool readOnly = true,
               ::cwxeditor::lib::Size size = {})
        : TextDialog({}, {}, std::move(title), 0, std::move(text), readOnly,
                     size) {}

    TextDialog(const TextDialog&) = delete;
    TextDialog& operator=(const TextDialog&) = delete;
    TextDialog(TextDialog&&) = delete;
    TextDialog& operator=(TextDialog&&) = delete;
    ~TextDialog();

    /// Editorから返す文字列。プラットフォームの改行コードは正規化する。
    [[nodiscard]] std::string text() const;
    /// 編集文字列を置き換え、readOnlyに応じてカーソルを移す。
    void setText(std::string value);
    /// この補助ダイアログのnoScenarioは、Dと同じく常にtrue。
    [[nodiscard]] bool noScenario() const noexcept { return true; }

    /// 既存の所有コンテナー内に共通部品を作る。 繰り返
    /// し呼んでも重複せず、現在の文字列・選択を保つ。
    void setup(Container& area);
    /// 親の領域が渡されていなければ、専用パネルを作る。
    void setup();

    /// 設定されたGUIバックエンドで表示し、結果を保持する。
    [[nodiscard]] DialogResult show();
    /// Dと同じく、受理して閉じたダイアログのopen()はtrueを返す。
    [[nodiscard]] bool open();
    /// DWTのキャンセル経路で、バックエンドを通じて閉じる。
    [[nodiscard]] bool close();
    /// バックエンドを通じて閉じ、共通形式の結果を明示的に設定する。
    void close(DialogResult result);

    /// 画面の画像一覧を、共通の資源IDで保持する。 lib
    /// のDialogはOS固有の画像ハンドルを持たない
    /// ため、ネイティブ側が使う状態としてIDを残す。
    void setImages(std::vector<::cwxeditor::lib::ResourceId> images);

    [[nodiscard]] const Services& services() const noexcept { return services_; }
    [[nodiscard]] const std::shared_ptr<Window>& parent() const noexcept {
        return parent_;
    }
    [[nodiscard]] const std::shared_ptr<Dialog>& dialog() const noexcept {
        return dialog_;
    }
    [[nodiscard]] const std::shared_ptr<TextEdit>& viewer() const noexcept {
        return viewer_;
    }
    [[nodiscard]] bool readOnly() const noexcept { return readOnly_; }
    [[nodiscard]] bool initialized() const noexcept { return initialized_; }
    [[nodiscard]] bool closed() const noexcept { return closed_; }
    [[nodiscard]] DialogResult result() const noexcept {
        return dialog_ ? dialog_->result : DialogResult::None;
    }
    [[nodiscard]] ::cwxeditor::lib::ResourceId icon() const noexcept { return icon_; }
    [[nodiscard]] const std::vector<::cwxeditor::lib::ResourceId>& images() const noexcept {
        return images_;
    }

private:
    Services services_{};
    dprops::Props* prop_{};
    dmenu::TextMenuModifyPtr text_menu_{};
    std::shared_ptr<Window> parent_{};
    std::shared_ptr<Dialog> dialog_{};
    std::shared_ptr<::cwxeditor::lib::Panel> area_{};
    std::shared_ptr<TextEdit> viewer_{};
    std::shared_ptr<::cwxeditor::lib::Panel> buttons_{};
    std::shared_ptr<::cwxeditor::lib::Button> ok_{};
    std::shared_ptr<::cwxeditor::lib::Button> cancel_{};
    std::string text_{};
    ::cwxeditor::lib::ResourceId icon_{};
    std::vector<::cwxeditor::lib::ResourceId> images_{};
    ::cwxeditor::lib::Size size_{};
    bool readOnly_{true};
    bool initialized_{};
    bool closed_{};

    static std::string normalizeReturnCode(std::string_view value);
    void configureDialog(std::string title, ::cwxeditor::lib::ResourceId icon,
                         ::cwxeditor::lib::Size size);
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::textdialog
