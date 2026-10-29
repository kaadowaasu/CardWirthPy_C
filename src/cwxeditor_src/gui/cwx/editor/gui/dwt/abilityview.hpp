#pragma once

// cwx/editor/gui/dwt/abilityview.d
// の移植可能なC++20実装。 DのSWT Composite
// が持つ二組のラジオボタンの所有関係と通知
// 規則を保ち、 libの部品インターフェー
// スだけを公開する。呼び出し元やGUI実装
// にSWT/DWTのハンドルを要求しない。

#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/services.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::msgs {
class Msgs;
}  // namespace cwxeditor::cwx::msgs

namespace cwxeditor::cwx::editor::gui::dwt::abilityview {

using Physical = ::cwxeditor::cwx::types::Physical;
using Mental = ::cwxeditor::cwx::types::Mental;
using Services = ::cwxeditor::lib::Services;
using Parent = std::shared_ptr<::cwxeditor::lib::Container>;
using RadioButton = std::shared_ptr<::cwxeditor::lib::RadioButton>;

/// AbilityViewで使う翻訳済み文
/// 字列。DはCommons.prop.msgs
/// から読むが、 この部品に必要な項目だ
/// けをまとめ、Editor全体のプロパ
/// ティがなくても使えるようにする。 既
/// 定値はcwx.msgsと同じにする。
struct Messages {
    std::string aptPhysical;
    std::string aptMental;
    std::array<std::string, 6> physicalNames;
    std::array<std::string, 10> mentalNames;

    Messages();
    explicit Messages(const ::cwxeditor::cwx::msgs::Msgs& catalog);

    [[nodiscard]] const std::string& physicalName(Physical value) const;
    [[nodiscard]] const std::string& mentalName(Mental value) const;
};

/// このビューが使うDのCommonsの一部に対応する、共
/// 通の具体型。 SWTやアプリに固有のCommons全体
/// ではなく、メッセージとラジオボタン群の間隔だけを渡す。
struct Commons {
    Messages messages{};
    std::int32_t radioGroupSeparatorWidth{15};

    Commons() = default;
    Commons(Messages value, std::int32_t separatorWidth)
        : messages(std::move(value)),
          radioGroupSeparatorWidth(separatorWidth) {}
    explicit Commons(const ::cwxeditor::cwx::msgs::Msgs& catalog);
};

class AbilityView final {
public:
    using ModCallback = ::cwxeditor::lib::Action;
    using PhysicalButtons = std::array<RadioButton, 6>;
    using MentalButtons = std::array<RadioButton, 10>;

    /// 二組のラジオボタンを作り、parentがあれば共通の
    /// ルートパネルを接続する。 readOnlyはDの整数
    /// フラグと同じく、0以外ならユーザーの選択を禁止する。
    AbilityView(Commons comm = {}, Parent parent = {}, int readOnly = 0,
                Services services = {});

    AbilityView(const AbilityView&) = delete;
    AbilityView& operator=(const AbilityView&) = delete;
    AbilityView(AbilityView&&) = delete;
    AbilityView& operator=(AbilityView&&) = delete;
    ~AbilityView();

    /// Dの公開デリゲート配列に対応する。部品・バックエンドから選択した
    /// 後に通知するが、 プログラムによるプロパティ設定では呼ばない。
    std::vector<ModCallback> modEvent;

    [[nodiscard]] Physical physical() const;
    void physical(Physical value);
    [[nodiscard]] Mental mental() const;
    void mental(Mental value);

    [[nodiscard]] bool enabled() const noexcept { return enabled_; }
    void enabled(bool value) noexcept;

    [[nodiscard]] int readOnly() const noexcept { return readOnly_; }
    [[nodiscard]] const Services& services() const noexcept { return services_; }
    [[nodiscard]] const Commons& commons() const noexcept { return commons_; }

    /// DのCompositeに対応するルートパネル。GroupBox・パネル
    /// ・ラジオボタン全体を所有し、 任意のlibバックエンドで描画できる。
    [[nodiscard]] const std::shared_ptr<::cwxeditor::lib::Panel>& widget() const
        noexcept {
        return root_;
    }
    [[nodiscard]] const std::shared_ptr<::cwxeditor::lib::GroupBox>&
    physicalGroup() const noexcept {
        return physicalGroup_;
    }
    [[nodiscard]] const std::shared_ptr<::cwxeditor::lib::GroupBox>&
    mentalGroup() const noexcept {
        return mentalGroup_;
    }
    [[nodiscard]] const PhysicalButtons& physicalButtons() const noexcept {
        return physicalButtons_;
    }
    [[nodiscard]] const MentalButtons& mentalButtons() const noexcept {
        return mentalButtons_;
    }

    [[nodiscard]] const RadioButton& physicalButton(Physical value) const;
    [[nodiscard]] const RadioButton& mentalButton(Mental value) const;

    /// DのmodEvent配列への追加に相当する登録用の補助関数
    /// 。 単純な呼び出し元へvector操作を公開せずに済む。
    void addModificationListener(ModCallback callback);

    /// バックエンドによらずラジオボタンの選択を再現する。 notify
    /// が真なら、実際のユーザー選択と同じ通知経路を使う。
    void selectPhysical(Physical value, bool notify = true);
    void selectMental(Mental value, bool notify = true);

private:
    Commons commons_{};
    Services services_{};
    Parent parent_{};
    std::shared_ptr<::cwxeditor::lib::Panel> root_{};
    std::shared_ptr<::cwxeditor::lib::GroupBox> physicalGroup_{};
    std::shared_ptr<::cwxeditor::lib::GroupBox> mentalGroup_{};
    std::shared_ptr<::cwxeditor::lib::Panel> physicalPanel_{};
    std::shared_ptr<::cwxeditor::lib::Panel> mentalPanel_{};
    PhysicalButtons physicalButtons_{};
    MentalButtons mentalButtons_{};
    bool enabled_{true};
    int readOnly_{};

    template <typename Component>
    [[nodiscard]] std::shared_ptr<Component> createComponent(
        ::cwxeditor::lib::ComponentKind kind) const;

    void build();
    void updatePhysicalButtons() noexcept;
    void updateMentalButtons() noexcept;
    void notifyModification();
    [[nodiscard]] static std::size_t physicalIndex(Physical value);
    [[nodiscard]] static std::size_t mentalIndex(Mental value);
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::abilityview
