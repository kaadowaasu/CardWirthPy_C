#pragma once

#include "cwxeditor_src/core/cwx/event.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dprops.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/keycodeview.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/roundview.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/gui.hpp"
#include "cwxeditor_src/lib/services.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::commons {
class Commons;
}

namespace cwxeditor::cwx::editor::gui::dwt::eventtreedialog {

namespace lib = ::cwxeditor::lib;
namespace dprops = ::cwxeditor::cwx::editor::gui::dwt::dprops;
namespace event = ::cwxeditor::cwx::event;
namespace summary = ::cwxeditor::cwx::summary;
namespace types = ::cwxeditor::cwx::types;
namespace system = ::cwxeditor::cwx::system;
namespace keycodeview = ::cwxeditor::cwx::editor::gui::dwt::keycodeview;
namespace roundview = ::cwxeditor::cwx::editor::gui::dwt::roundview;
namespace commons = ::cwxeditor::cwx::editor::gui::dwt::commons;

using String = lib::String;
using StringView = lib::StringView;
using Props = dprops::Props;
using Summary = summary::Summary;
using EventTree = event::EventTree;
using EventTreeOwner = event::EventTreeOwner;
using MatchingType = types::MatchingType;
using Window = std::shared_ptr<lib::Window>;
using Dialog = std::shared_ptr<lib::Dialog>;

/// Dの所有者インターフェースはこれらを仮想プロパティとして
/// 公開する。 現在の共通EventTreeOwnerはツリ
/// ーの保持だけを公開するため、 対応する仮想関数が加わる
/// までは、不足する機能フラグをダイアログへ明示的に渡す。
enum class OwnerKind {
    Generic,
    MenuCard,
    EnemyCard,
    Area,
    Battle,
    PlayerCardEvents,
    Package,
};

struct OwnerCapabilities {
    OwnerKind kind{OwnerKind::Generic};
    bool canHasFireEnter{true};
    bool canHasFireLose{};
    bool canHasFireEscape{};
    bool canHasFireEveryRound{};
    bool canHasFireRoundEnd{};
    bool canHasFireRound0{};
    bool canHasFireRound{};
    bool canHasFireKeyCode{true};
    bool withKeyCodeIgnitionType{};

    [[nodiscard]] static OwnerCapabilities generic() noexcept;
    [[nodiscard]] static OwnerCapabilities menuCard() noexcept;
    [[nodiscard]] static OwnerCapabilities enemyCard() noexcept;
    [[nodiscard]] static OwnerCapabilities area() noexcept;
    [[nodiscard]] static OwnerCapabilities battle() noexcept;
    [[nodiscard]] static OwnerCapabilities playerCardEvents() noexcept;
    [[nodiscard]] static OwnerCapabilities package() noexcept;
};

/// イベントツリーの発火条件ダイアログの共通実
/// 装。 オブジェクトはGUI部品だけを所有し
/// 、ownerとeventTreeはDの非所
/// 有参照と同じく、画面の寿命中に借用する。
class EventTreeDialog final {
public:
    EventTreeDialog(Props& prop, Window parent, Summary* summ,
                    EventTreeOwner& owner, EventTree& eventTree, bool create,
                    bool readOnly, OwnerCapabilities capabilities = {},
                    lib::Services services = {}, commons::Commons* editorCommons = nullptr);

    EventTreeDialog(Props& prop, Window parent, Summary& summ,
                    EventTreeOwner& owner, EventTree& eventTree, bool create,
                    bool readOnly, OwnerCapabilities capabilities = {},
                    lib::Services services = {}, commons::Commons* editorCommons = nullptr)
        : EventTreeDialog(prop, std::move(parent), &summ, owner, eventTree,
                          create, readOnly, capabilities, std::move(services), editorCommons) {}

    EventTreeDialog(const EventTreeDialog&) = delete;
    EventTreeDialog& operator=(const EventTreeDialog&) = delete;
    EventTreeDialog(EventTreeDialog&&) = delete;
    EventTreeDialog& operator=(EventTreeDialog&&) = delete;
    ~EventTreeDialog();
    void close(lib::DialogResult result = lib::DialogResult::Cancel);
    void applyTransaction(std::function<bool(const std::function<bool()>&)> value) { transaction_ = std::move(value); }

    [[nodiscard]] EventTreeOwner& eventTreeOwner() const noexcept { return *owner_; }
    [[nodiscard]] EventTree* eventTreePtr() const noexcept { return event_tree_; }
    [[nodiscard]] EventTree& eventTree() const noexcept { return *event_tree_; }
    [[nodiscard]] const OwnerCapabilities& capabilities() const noexcept {
        return capabilities_;
    }

    /// 既存の共通コンテナー内にダイアログの部品を作る。
    void setup(lib::Container& area);
    void setup(const std::shared_ptr<lib::Container>& area);

    /// 発火条件のモードレス編集画面を表示する。適用では反映するだけで閉じない。
    [[nodiscard]] bool open();

    /// 現在の操作部品の値をEventTreeへ戻す。 部品が未構築、
    /// または所有者・ツリー参照が失われた場合だけfalseを返す。
    [[nodiscard]] bool apply();

    void refreshWarning();
    void setAutoName();

    [[nodiscard]] bool pendingCreation() const noexcept { return create_; }
    [[nodiscard]] const Dialog& dialog() const noexcept { return dialog_; }
    [[nodiscard]] const std::shared_ptr<lib::Container>& content() const noexcept {
        return content_;
    }
    [[nodiscard]] const std::shared_ptr<lib::TextEdit>& nameControl() const noexcept {
        return name_;
    }
    [[nodiscard]] const std::shared_ptr<lib::ComboBox>& matchingTypeControl() const noexcept {
        return key_code_matching_type_;
    }
    [[nodiscard]] const std::shared_ptr<lib::CheckBox>& enterControl() const noexcept {
        return enter_;
    }
    [[nodiscard]] const std::shared_ptr<lib::CheckBox>& loseControl() const noexcept {
        return lose_;
    }
    [[nodiscard]] const std::shared_ptr<lib::CheckBox>& escapeControl() const noexcept {
        return escape_;
    }
    [[nodiscard]] const std::shared_ptr<lib::CheckBox>& everyRoundControl() const noexcept {
        return every_round_;
    }
    [[nodiscard]] const std::shared_ptr<lib::CheckBox>& roundEndControl() const noexcept {
        return round_end_;
    }
    [[nodiscard]] const std::shared_ptr<lib::CheckBox>& round0Control() const noexcept {
        return round0_;
    }
    [[nodiscard]] const std::shared_ptr<keycodeview::KeyCodeView>& keyCodesControl() const noexcept {
        return key_codes_;
    }
    [[nodiscard]] const std::shared_ptr<roundview::RoundView>& roundsControl() const noexcept {
        return rounds_;
    }
    [[nodiscard]] const std::vector<String>& warnings() const noexcept { return warnings_; }
    [[nodiscard]] bool modified() const noexcept { return modified_; }
    [[nodiscard]] bool readOnly() const noexcept { return read_only_; }
    [[nodiscard]] bool autoNameEnabled() const noexcept { return set_auto_name_; }

private:
    std::shared_ptr<int> callback_lifetime_{std::make_shared<int>(0)};
    Props* prop_{};
    Summary* summary_{};
    EventTreeOwner* owner_{};
    EventTree* event_tree_{};
    OwnerCapabilities capabilities_{};
    Window parent_{};
    lib::Services services_{};
    commons::Commons* editor_commons_{};
    std::weak_ptr<const int> editor_commons_lifetime_{};
    std::uint64_t ref_data_version_connection_{};
    std::uint64_t ref_target_version_connection_{};
    Dialog dialog_{};
    std::shared_ptr<lib::Container> content_{};

    bool create_{};
    bool read_only_{};
    bool ignore_mod_{};
    bool set_auto_name_{true};
    bool initialized_{};
    bool modified_{};

    std::shared_ptr<lib::TextEdit> name_{};
    std::shared_ptr<lib::ComboBox> key_code_matching_type_{};
    std::shared_ptr<lib::CheckBox> enter_{};
    std::shared_ptr<lib::CheckBox> lose_{};
    std::shared_ptr<lib::CheckBox> escape_{};
    std::shared_ptr<lib::CheckBox> every_round_{};
    std::shared_ptr<lib::CheckBox> round_end_{};
    std::shared_ptr<lib::CheckBox> round0_{};
    std::shared_ptr<keycodeview::KeyCodeView> key_codes_{};
    std::shared_ptr<roundview::RoundView> rounds_{};
    std::shared_ptr<lib::Label> warning_label_{};
    std::shared_ptr<lib::Button> ok_button_{};
    std::shared_ptr<lib::Button> cancel_button_{};
    std::shared_ptr<lib::Button> apply_button_{};
    std::function<bool(const std::function<bool()>&)> transaction_{};
    bool applyImpl();
    void unbindExternalNotifications() noexcept;
    std::vector<String> warnings_{};

    [[nodiscard]] MatchingType keyCodeMatchingType() const noexcept;
    [[nodiscard]] bool catchModify() noexcept;
    [[nodiscard]] keycodeview::KeyCodeViewConfig keyCodeConfig() const;
    [[nodiscard]] roundview::RoundViewConfig roundConfig() const;

    void createDialog();
    void buildNameGroup(lib::Container& area, std::size_t columns);
    void buildSystemGroup(lib::Container& area, std::size_t column);
    void buildKeyCodeGroup(lib::Container& area, std::size_t column);
    void buildRoundGroup(lib::Container& area, std::size_t column);
    void populateControls();
    void updateWarningLabel();
    void markChanged();
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::eventtreedialog
