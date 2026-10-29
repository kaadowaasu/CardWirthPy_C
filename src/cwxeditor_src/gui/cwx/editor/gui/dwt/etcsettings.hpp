#pragma once

// cwx/editor/gui/dwt/etcsettings.d
// の移植可能なC++20実装。 オン・オフ
// のEditor設定を、ScrollView
// がExpandBarを持ち、各分類をGroupBox
// 、 各設定をFlexEtcProps
// へ接続したCheckBoxで表す。SWT
// やOSのハンドルは公開しない。

#include "cwxeditor_src/core/cwx/variables.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/incsearch.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/dialogs.hpp"
#include "cwxeditor_src/lib/services.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::etcsettings {

class EtcSettings final {
public:
    using Services = ::cwxeditor::lib::Services;
    using Widget = ::cwxeditor::lib::Widget;
    using Container = ::cwxeditor::lib::Container;
    using Panel = ::cwxeditor::lib::Panel;
    using ScrollView = ::cwxeditor::lib::ScrollView;
    using ExpandBar = ::cwxeditor::lib::ExpandBar;
    using CheckBox = ::cwxeditor::lib::CheckBox;
    using DialogResult = ::cwxeditor::lib::DialogResult;
    using Props = ::cwxeditor::cwx::variables::FlexEtcProps;
    using Action = ::cwxeditor::lib::Action;

    /// 通常はDのProps/Msgsから渡される翻訳文
    /// 字列。 項目がなければ固定の設定・分類キーを表示
    /// し、ヘッドレスなどでもモデルを使えるようにする。
    struct Presentation {
        std::unordered_map<std::string, std::string> categories;
        std::unordered_map<std::string, std::string> labels;
        std::function<std::string(std::string_view)> message;
        incsearch::IncSearchSettings searchSettings;
        std::string searchShortcut;
        std::function<void(incsearch::SearchType)> searchTypeChanged;

        [[nodiscard]] std::string category(std::string_view key) const;
        [[nodiscard]] std::string label(std::string_view key) const;
    };

    /// Dの一つのチェック欄に対応する公開情報。 元の遅延_apply
    /// と同じく、実際のプロパティはapply()で変更する。
    struct SettingControl {
        std::string key;
        std::string category;
        std::string label;
        std::shared_ptr<CheckBox> checkbox;
    };

    EtcSettings(Services services, std::shared_ptr<Widget> parent,
                Props& properties, Presentation presentation = {});

    EtcSettings(Props& properties, Presentation presentation = {})
        : EtcSettings({}, {}, properties, std::move(presentation)) {}

    EtcSettings(const EtcSettings&) = delete;
    EtcSettings& operator=(const EtcSettings&) = delete;
    EtcSettings(EtcSettings&&) = delete;
    EtcSettings& operator=(EtcSettings&&) = delete;
    ~EtcSettings();

    /// 既存の所有コンテナー内に共通部品を作る。 繰り返し
    /// 呼んでも重複せず、現在のチェック・検索状態を保つ。
    void setup(Container& area);
    /// 所有コンテナーが渡されていなければ、専用パネルを作る。
    void setup();

    /// Dの逐次検索を開始する。検索語を渡すと、 ヘッド
    /// レスでも検索欄へ入力したのと同じ操作を行える。
    void incSearch();
    void incSearch(std::string query);
    void setSearchQuery(std::string query);
    void clearSearch();
    /// 保持中の設定画面の所有者が閉じたら、既存のポップアップも閉じる。
    void closeSearch();
    [[nodiscard]] const std::string& searchQuery() const noexcept {
        return search_query_;
    }

    /// 逐次検索の変更後に表示可否とスクロール内容の寸法を更新する。
    /// DWTのshowCheckBoxes/scSizeに対応する。
    void showCheckBoxes();
    void scSize();
    [[nodiscard]] std::size_t visibleCount() const noexcept {
        return visible_count_;
    }

    /// ダイアログの編集を確定せず、外部で変わ
    /// ったコアのプロパティを部品へ同期する。
    void syncFromProperties();

    /// 全操作部品を検証し、共有の設定作業コピーへ反映する。
    /// 設定画面はcommit=falseを渡し、永続化に成功
    /// してから中止の基準と適用通知を進められるようにする。
    [[nodiscard]] bool apply(bool commit = true);
    void commitAppliedValues();
    /// 部品の編集を破棄し、構築時または最後の適用
    /// 成功時の値へ戻す。 ホストが後から中止を
    /// 選んだ場合は、適用済みの処理も巻き戻す。
    void cancel();
    [[nodiscard]] bool finish(DialogResult result);
    [[nodiscard]] bool validate() const;
    [[nodiscard]] std::vector<std::string> validationErrors() const;

    /// GUIと同じようにプログラムから設定を変更する
    /// 。 通常の選択・依存関係のリスナーを呼ぶが、コ
    /// アの値への反映はapply()まで遅らせる。
    [[nodiscard]] bool setChecked(std::string_view key, bool checked);
    [[nodiscard]] std::optional<bool> checked(std::string_view key) const;
    [[nodiscard]] std::shared_ptr<CheckBox> control(std::string_view key) const;

    void addApplyListener(Action listener);
    void addCancelListener(Action listener);

    [[nodiscard]] const Services& services() const noexcept { return services_; }
    [[nodiscard]] const std::shared_ptr<Widget>& parent() const noexcept {
        return parent_;
    }
    [[nodiscard]] Props& properties() const noexcept { return properties_; }
    [[nodiscard]] const Presentation& presentation() const noexcept {
        return presentation_;
    }
    [[nodiscard]] const std::shared_ptr<Panel>& panel() const noexcept {
        return panel_;
    }
    [[nodiscard]] const std::shared_ptr<ScrollView>& scrollView() const noexcept {
        return scroll_;
    }
    [[nodiscard]] const std::shared_ptr<ExpandBar>& expandBar() const noexcept {
        return expand_bar_;
    }
    [[nodiscard]] const std::shared_ptr<lib::TextEdit>& searchEdit() const noexcept {
        return search_edit_;
    }
    [[nodiscard]] const std::vector<std::shared_ptr<Panel>>& categories() const noexcept {
        return category_boxes_;
    }
    [[nodiscard]] const std::vector<SettingControl>& controls() const noexcept {
        return controls_;
    }
    [[nodiscard]] bool initialized() const noexcept { return initialized_; }
    [[nodiscard]] bool modified() const noexcept;

    /// DのmodEvent配列をホストにも公開する。
    /// 選択リスナーが配列内の各処理を順番に呼ぶ。
    std::vector<Action> modEvent;

private:
    struct Binding {
        SettingControl control;
        std::function<bool()> read;
        std::function<void(bool)> write;
    };

    Services services_{};
    std::shared_ptr<Widget> parent_{};
    Props& properties_;
    Presentation presentation_{};
    std::shared_ptr<Panel> panel_{};
    std::shared_ptr<lib::TextEdit> search_edit_{};
    std::shared_ptr<incsearch::IncSearch> inc_search_{};
    std::shared_ptr<lib::Menu> search_menu_{};
    std::shared_ptr<int> lifetime_{std::make_shared<int>(0)};
    std::uint64_t search_revision_{};
    bool search_bound_{};
    std::shared_ptr<ScrollView> scroll_{};
    std::shared_ptr<ExpandBar> expand_bar_{};
    std::vector<std::shared_ptr<Panel>> category_boxes_{};
    std::vector<std::string> category_keys_{};
    std::vector<Binding> bindings_{};
    std::vector<SettingControl> controls_{};
    std::unordered_map<std::string, bool> cancel_values_{};
    std::vector<Action> apply_events_{};
    std::vector<Action> cancel_events_{};
    std::string search_query_{};
    std::size_t visible_count_{};
    bool initialized_{};
    bool applying_{};

    void buildControls(Container& area);
    void buildBinding(std::string_view key, std::string_view category,
                      std::string_view label,
                      std::function<bool()> read,
                      std::function<void(bool)> write,
                      std::shared_ptr<Panel>& group);
    void installListeners(Binding& binding);
    void bindSearch(const lib::WidgetPtr& widget);
    void markModified(std::string_view key);
    void enforceExclusiveOptions(std::string_view changed_key);
    void updateDependencies();
    [[nodiscard]] std::string fold(std::string_view value) const;
    [[nodiscard]] bool matches(std::string_view value) const;
    [[nodiscard]] std::shared_ptr<Panel> makePanel() const;
    [[nodiscard]] std::shared_ptr<ScrollView> makeScrollView() const;
    [[nodiscard]] std::shared_ptr<ExpandBar> makeExpandBar() const;
    [[nodiscard]] std::shared_ptr<CheckBox> makeCheckBox(std::string text) const;
    [[nodiscard]] Binding* findBinding(std::string_view key);
    [[nodiscard]] const Binding* findBinding(std::string_view key) const;
    void captureCancelValues();
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::etcsettings
