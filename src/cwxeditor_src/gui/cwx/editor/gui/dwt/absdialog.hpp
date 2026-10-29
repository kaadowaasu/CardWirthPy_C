#pragma once

// cwx/editor/gui/dwt/absdialog.d
// の移植可能なC++20実装。 DWTのシェル
// とリスナーの階層を、共通の部品・レイアウト・
// イベント・サービスで表す。 派生ダイアログ
// もcontent()で同じ部品を構築し、こ
// の境界へネイティブの部品型を持ち込まない。

#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dprops.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/editdialogsession.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/dialogs.hpp"
#include "cwxeditor_src/lib/events.hpp"
#include "cwxeditor_src/lib/layout.hpp"
#include "cwxeditor_src/lib/services.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::absdialog {

namespace lib = ::cwxeditor::lib;
namespace structs = ::cwxeditor::cwx::structs;
using Props = ::cwxeditor::cwx::editor::gui::dwt::dprops::Props;
using DSize = structs::DSize;
using WSize = structs::WSize;

struct ButtonInfo {
    lib::String name;
    lib::Action func;
};

enum class StyleFlag : std::uint32_t {
    None = 0U,
    ReadOnly = 1U << 0U,
};

[[nodiscard]] constexpr std::uint32_t operator|(StyleFlag left,
                                                  StyleFlag right) noexcept {
    return static_cast<std::uint32_t>(left) |
           static_cast<std::uint32_t>(right);
}

class AbsDialog : public editdialogsession::EditDialogSession {
public:
    using Dialog = lib::Dialog;
    using Window = lib::Window;
    using Container = lib::Container;
    using Panel = lib::Panel;
    using Button = lib::Button;
    using ComboBox = lib::ComboBox;
    using TextEdit = lib::TextEdit;
    using NumberEdit = lib::NumberEdit;
    using ResourceId = lib::ResourceId;
    using BoundsCallback = std::function<void(int, int, int, int)>;
    using ComboCheck = std::function<bool(ComboBox&)>;
    using TextCheck = std::function<bool(TextEdit&)>;

    std::vector<lib::Action> closeEvent;
    std::vector<lib::Action> applyEvent;
    std::vector<lib::Action> appliedEvent;
    std::vector<BoundsCallback> calcBoundsEvent;
    std::vector<lib::Action> openedEvent;

    AbsDialog(Props& prop, std::shared_ptr<Window> parent, lib::String text,
              ResourceId image, bool resizable, DSize* size = nullptr,
              bool apply = false, bool cancel = true,
              std::vector<ButtonInfo> buttons = {}, lib::Services services = {});
    AbsDialog(Props& prop, std::shared_ptr<Window> parent, bool modal,
              lib::String text, ResourceId image, bool resizable,
              DSize* size = nullptr, bool apply = false, bool cancel = true,
              std::vector<ButtonInfo> buttons = {}, bool right_group = false,
              lib::Services services = {});
    AbsDialog(Props& prop, std::shared_ptr<Window> parent,
              std::uint32_t style_flags, bool modal, lib::String text,
              ResourceId image, bool resizable, DSize* size = nullptr,
              bool apply = false, bool cancel = true,
              std::vector<ButtonInfo> buttons = {}, bool right_group = false,
              lib::Services services = {});

    AbsDialog(Props& prop, std::shared_ptr<Window> parent, lib::String text,
              ResourceId image, bool resizable,
              std::shared_ptr<DSize> size, bool apply = false,
              bool cancel = true, std::vector<ButtonInfo> buttons = {},
              lib::Services services = {});
    AbsDialog(Props& prop, std::shared_ptr<Window> parent, bool modal,
              lib::String text, ResourceId image, bool resizable,
              std::shared_ptr<DSize> size, bool apply = false,
              bool cancel = true, std::vector<ButtonInfo> buttons = {},
              bool right_group = false,
              lib::Services services = {});
    AbsDialog(Props& prop, std::shared_ptr<Window> parent,
              std::uint32_t style_flags, bool modal, lib::String text,
              ResourceId image, bool resizable, std::shared_ptr<DSize> size,
              bool apply = false, bool cancel = true,
              std::vector<ButtonInfo> buttons = {}, bool right_group = false,
              lib::Services services = {});

    AbsDialog(const AbsDialog&) = delete;
    AbsDialog& operator=(const AbsDialog&) = delete;
    AbsDialog(AbsDialog&&) = delete;
    AbsDialog& operator=(AbsDialog&&) = delete;
    virtual ~AbsDialog();

    [[nodiscard]] const std::shared_ptr<Dialog>& dialog() const noexcept {
        return window_;
    }
    [[nodiscard]] std::shared_ptr<Dialog>& dialog() noexcept { return window_; }
    [[nodiscard]] Props& props() const noexcept { return *prop_; }
    [[nodiscard]] const std::shared_ptr<Panel>& content() const noexcept {
        return area_;
    }
    [[nodiscard]] const std::shared_ptr<Panel>& mainPanel() const noexcept {
        return main_panel_;
    }
    [[nodiscard]] const std::shared_ptr<Panel>& addition() const noexcept {
        return addition_;
    }
    [[nodiscard]] const std::shared_ptr<Panel>& buttonsPanel() const noexcept {
        return buttons_panel_;
    }
    [[nodiscard]] const std::shared_ptr<Panel>& rightGroup() const noexcept {
        return right_group_;
    }
    [[nodiscard]] const std::vector<std::shared_ptr<Button>>& buttons() const
        noexcept {
        return buttons_;
    }
    [[nodiscard]] const std::vector<ResourceId>& images() const noexcept {
        return images_;
    }

    void rightGroupSize(int width, int height);
    void useRightGroup(bool right_group);
    void setImages(std::vector<ResourceId> images);

    void windowSizeInfo(DSize* size);
    void windowSizeInfo(std::shared_ptr<DSize> size);
    void clearWindowSizeInfo();
    void saveWin();

    void title(lib::String value);
    [[nodiscard]] const lib::String& windowTitle() const noexcept {
        return window_->title;
    }
    [[nodiscard]] ResourceId image() const noexcept { return image_; }

    void enterClose(bool value);
    [[nodiscard]] bool enterClose() const noexcept { return enter_close_; }
    void firstFocusIsOK(bool value);
    [[nodiscard]] bool firstFocusIsOK() const noexcept {
        return first_focus_is_ok_;
    }
    void forceCancel();
    void modal(bool value) noexcept;

    [[nodiscard]] bool open();
    // ブラウザーホスト向けの、処理をブロックしないモーダル
    // 開始。 OK・キャンセル・閉じる操作でセッションが終
    // 了してから、完了通知をちょうど一度呼ぶ。 ネイティブ
    // の呼び出し元は、同期的なopen()を引き続き使う。
    [[nodiscard]] bool openAsync(std::function<void(bool)> completed);
    void active();
    [[nodiscard]] bool close();

    [[nodiscard]] bool noScenario() const noexcept override { return false; }
    [[nodiscard]] bool noApply() const noexcept;
    // 有効な共通ダイアログの一覧を取得する。DWTのqSaveと同様に、 シ
    // ナリオの置き換え・再読み込み前に開いているAbsDialogを走査でき
    // るよう、 共通基底に登録一覧を持たせ、どの表示実装でも同じ規則を使う。
    [[nodiscard]] static std::vector<AbsDialog*> liveDialogs();
    [[nodiscard]] bool forceApplying() const noexcept {
        return force_applying_;
    }
    void forceApply();
    [[nodiscard]] lib::Window* editDialogWindow() const noexcept override {
        return window_.get();
    }
    [[nodiscard]] bool editDialogPending() const noexcept override {
        return noApply();
    }
    [[nodiscard]] bool applyEditDialog() override {
        forceApply();
        return !noApply();
    }

protected:
    virtual void setup(Container& area) = 0;

    [[nodiscard]] const lib::Services& services() const noexcept {
        return services_;
    }

    virtual bool apply();
    virtual bool close(bool ok, bool& cancel);
    virtual bool close(bool ok);
    virtual void opened();

    // 複数のUI更新を途中の状態を見せずに行う。Windowsで
    // はWM_SETREDRAWを使い、 他の実装ではGuiBackend
    // の共通の代替処理で最後に一度だけ再描画する。
    void withRedrawSuspended(const lib::Action& update);

    void check();
    void applyEnabled();
    void applyEnabled(bool force);
    void warning(std::vector<lib::String> warnings);
    void setWarningIcon(ResourceId image) noexcept { warning_icon_ = image; }

    void checker(const std::shared_ptr<ComboBox>& text,
                 ComboCheck check = {});
    void checker(ComboBox& text, ComboCheck check = {});
    void checker(const std::shared_ptr<TextEdit>& text,
                 TextCheck check = {});
    void checker(TextEdit& text, TextCheck check = {});

public:
    template <typename T>
    void mod(const std::shared_ptr<T>& control) {
        if (control) mod(*control);
    }

    template <typename T>
    void mod(T& control) {
        const std::weak_ptr<int> alive = lifetime_;
        if constexpr (std::is_same_v<T, TextEdit>) {
            auto previous = control.on_changed;
            control.on_changed = [this, previous, alive](std::string_view value) {
                if (alive.expired()) return;
                if (previous) previous(value);
                if (!alive.expired()) onMod();
            };
        } else if constexpr (std::is_base_of_v<ComboBox, T>) {
            auto previous = control.on_selection_changed;
            control.on_selection_changed =
                [this, previous, alive](std::optional<std::size_t> index) {
                    if (alive.expired()) return;
                    if (previous) previous(index);
                    if (!alive.expired()) onMod();
                };
            // DのSWT.ModifyはSelectionと別の通知
            // 。自由入力・IME・貼り付け・消去でも 変更済みにする
            // 。初期の候補一覧が空の派生した選択部品も対象にする。
            auto previous_text = control.on_text_changed;
            control.on_text_changed = [this, previous_text, alive](std::string_view value) {
                if (alive.expired()) return;
                if (previous_text) previous_text(value);
                if (!alive.expired()) onMod();
            };
        } else if constexpr (std::is_same_v<T, NumberEdit>) {
            auto previous = control.on_changed;
            control.on_changed = [this, previous, alive](double value) {
                if (alive.expired()) return;
                if (previous) previous(value);
                if (!alive.expired()) onMod();
            };
        } else if constexpr (std::is_base_of_v<lib::ToggleButton, T>) {
            auto previous = control.on_changed;
            control.on_changed = [this, previous, alive](bool value) {
                if (alive.expired()) return;
                if (previous) previous(value);
                if (!alive.expired()) onMod();
            };
        } else if constexpr (std::is_same_v<T, Button>) {
            auto previous = control.on_click;
            control.on_click = [this, previous, alive] {
                if (alive.expired()) return;
                if (previous) previous();
                if (!alive.expired()) onMod();
            };
        } else if constexpr (requires { control.modEvent.push_back(lib::Action{}); }) {
            // Dはネイティブ部品だけでなく、modEventを持
            // つ独自の編集ビューも受け付ける。 同じイベント群へ
            // 登録し、各ダイアログに別のアダプターを要求しない。
            control.modEvent.push_back([this, alive] {
                if (!alive.expired()) onMod();
            });
        } else if constexpr (requires { control.addModifyListener(lib::Action{}); }) {
            control.addModifyListener([this, alive] {
                if (!alive.expired()) onMod();
            });
        } else if constexpr (requires { control.addSelectionListener(lib::Action{}); }) {
            control.addSelectionListener([this, alive] {
                if (!alive.expired()) onMod();
            });
        } else {
            static_assert(detail::always_false<T>,
                          "mod requires a portable editable control");
        }
    }

    [[nodiscard]] bool ignoreMod() const noexcept { return ignore_mod_; }
    void ignoreMod(bool value) noexcept { ignore_mod_ = value; }
    [[nodiscard]] bool catchMod() const noexcept { return !ignore_mod_; }

private:
    struct ComboCheckEntry {
        ComboBox* widget{};
        ComboCheck check;
    };
    struct TextCheckEntry {
        TextEdit* widget{};
        TextCheck check;
    };

    struct detail {
        template <typename>
        static constexpr bool always_false = false;
    };

    Props* prop_{};
    lib::Services services_{};
    std::shared_ptr<Window> parent_;
    std::shared_ptr<Dialog> window_;
    std::shared_ptr<Panel> main_panel_;
    std::shared_ptr<Panel> area_;
    std::shared_ptr<Panel> addition_;
    std::shared_ptr<Panel> right_group_;
    std::shared_ptr<Panel> buttons_panel_;
    std::vector<std::shared_ptr<Button>> buttons_;
    std::vector<ButtonInfo> pending_button_info_;
    std::shared_ptr<Button> ok_button_;
    std::shared_ptr<Button> apply_button_;
    std::vector<ResourceId> images_;

    std::shared_ptr<DSize> size_owner_;
    DSize* size_{};
    std::function<void(bool)> clear_setup_window_;

    ResourceId image_{};
    ResourceId warning_icon_{};
    std::uint32_t style_flags_{};
    bool modal_{};
    bool has_apply_{};
    bool pending_read_only_{};
    bool pending_cancel_{};
    bool buttons_initialized_{};
    bool force_apply_enabled_{};
    bool ignore_mod_{};
    bool applied_{};
    bool in_close_event_{};
    bool return_value_{};
    bool enter_close_{};
    bool first_focus_is_ok_{};
    bool force_cancel_{};
    bool force_applying_{};
    bool setup_done_{};
    bool closed_{};
    std::shared_ptr<int> lifetime_{std::make_shared<int>(0)};
    std::uint64_t presentation_generation_{};
    bool async_open_{};
    bool async_finishing_{};
    std::function<void(bool)> async_completion_;

    std::vector<ComboCheckEntry> combo_checks_;
    std::vector<TextCheckEntry> text_checks_;

    void initialize(std::uint32_t style_flags, bool modal,
                    lib::String text, ResourceId image, bool resizable,
                    DSize* size, bool apply, bool cancel,
                    std::vector<ButtonInfo> buttons, bool right_group);
    void useRightGroupImpl(bool right_group);
    void initializeButtons();
    [[nodiscard]] std::shared_ptr<Button> createButton(
        const std::shared_ptr<Panel>& parent, lib::String text,
        lib::Action push);
    void onMod();
    void checkerImpl(ComboBox& text);
    void checkerImpl(TextEdit& text);
    [[nodiscard]] bool processCloseRequest();
    [[nodiscard]] bool openImpl(std::function<void(bool)> completed,
                                bool asynchronous);
    void finishPresentedSurface();
    void ok();
    void cancel();
    void calculateBounds(bool set_location);
    [[nodiscard]] lib::Size preferredSize() const;
    [[nodiscard]] static int rounded(float value) noexcept;
    [[nodiscard]] static lib::String comboText(const ComboBox& combo);
    void warningImpl(const std::vector<lib::String>& warnings);
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::absdialog
