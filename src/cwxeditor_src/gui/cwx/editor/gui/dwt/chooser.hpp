#pragma once

// cwx/editor/gui/dwt/chooser.d
// のC++20の選択部品。公開APIは共通部品で表す。 シ
// ナリオ資源には実際のSummary/Flag/Area/Card
// を使い、 従来DWTのプロパティ群から読んでいた設定
// は小さなChooserPropertiesで渡す。

#include "cwxeditor_src/core/cwx/area.hpp"
#include "cwxeditor_src/core/cwx/card.hpp"
#include "cwxeditor_src/core/cwx/features.hpp"
#include "cwxeditor_src/core/cwx/flag.hpp"
#include "cwxeditor_src/core/cwx/path.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/core/cwx/usecounter.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/customtable.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/customtext.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dmenu.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/incsearch.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/namewindow.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/events.hpp"
#include "cwxeditor_src/lib/portable.hpp"
#include "cwxeditor_src/lib/services.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::dprops { class Props; }

namespace cwxeditor::cwx::editor::gui::dwt::commons {
class Commons;
}

namespace cwxeditor::cwx::editor::gui::dwt::chooser {

namespace lib = ::cwxeditor::lib;
namespace area = ::cwxeditor::cwx::area;
namespace card = ::cwxeditor::cwx::card;
namespace flag = ::cwxeditor::cwx::flag;
namespace path = ::cwxeditor::cwx::path;
namespace summary = ::cwxeditor::cwx::summary;
namespace usecounter = ::cwxeditor::cwx::usecounter;
namespace customtable = ::cwxeditor::cwx::editor::gui::dwt::customtable;
namespace customtext = ::cwxeditor::cwx::editor::gui::dwt::customtext;
namespace incsearch = ::cwxeditor::cwx::editor::gui::dwt::incsearch;
namespace namewindow = ::cwxeditor::cwx::editor::gui::dwt::namewindow;

using String = lib::String;
using StringView = lib::StringView;
using Parent = lib::Container*;
using Widget = lib::Widget;
using WidgetPtr = lib::WidgetPtr;
using Container = lib::Container;
using ContainerPtr = std::shared_ptr<Container>;
using Services = lib::Services;
using ResourceId = lib::ResourceId;
using Summary = summary::Summary;
using SummaryPtr = lib::Owner<Summary>;
using UseCounter = usecounter::UseCounter;
using UseCounterPtr = lib::Owner<UseCounter>;
using Commons = namewindow::Commons;
using TextMenuModifyCore = customtext::TextMenuModify;
using CouponType = ::cwxeditor::cwx::features::CouponType;
using FKCKind = ::cwxeditor::cwx::types::FKCKind;
using ExpandedMap = std::unordered_map<String, bool>;

using CouponId = usecounter::CouponId;
using GossipId = usecounter::GossipId;
using CompleteStampId = usecounter::CompleteStampId;
using KeyCodeId = usecounter::KeyCodeId;
using CellNameId = usecounter::CellNameId;
using CardGroupId = usecounter::CardGroupId;

enum class CouponComboType {
    AllCoupons,
    GetLose,
    Talker,
    Cast,
    Valued,
};

enum class VariableKind { Flag, Step, Variant };

/// chooser.dが使う設定。既定値はEditorの共通
/// ・ヘッドレスの値に合わせる。 ホストはウィンドウツールキ
/// ットの型を公開せず、スキン固有値や資源IDを指定できる。
struct ChooserImages {
    ResourceId flag{};
    ResourceId localFlag{};
    ResourceId step{};
    ResourceId localStep{};
    ResourceId variant{};
    ResourceId localVariant{};
    ResourceId flagDir{};
    ResourceId area{};
    ResourceId battle{};
    ResourceId packages{};
    ResourceId casts{};
    ResourceId info{};
    ResourceId areaDir{};
    ResourceId emptyIcon{};
    ResourceId randomValue{};
    ResourceId selectedPlayerCardNumber{};
    ResourceId startArea{};
};

struct ChooserMessages {
    String allExpanded{"すべて展開"};
    String noFlagRef{"(なし)"};
    String randomValue{"ランダム値"};
    String selectedPlayerValue{"選択メンバ番号"};
    String flagDirRoot{"フラグ・ステップ"};
    String localVariablesOwner{"[%1$s.%2$s]"};
    String incrementalSearch{"増分検索"};
    String convertCouponType{"クーポン種別を変換"};
    String copyConvertedCoupon{"変換したクーポンをコピー"};
    String keyCodeTiming{"キーコード発火時機"};
    String copyConvertedKeyCode{"変換したキーコードをコピー"};
};

/// このモジュールが使うDのProps項目に対応する具体型。
struct ChooserProperties {
    /// 任意のEditor通知元。nullなら単独・ヘッドレスの
    /// 選択部品は、従来の取得だけの動作を維持する。 ホストが所
    /// 有し、破棄前に関連する選択部品を終了させる必要がある。
    ::cwxeditor::cwx::editor::gui::dwt::commons::Commons* editor_commons{};
    bool logicalSort{true};
    bool usedCouponToCombo{true};
    bool useNamesAfterStandard{};
    bool showSpNature{};
    bool showCurrentValueOnTopAlways{};
    bool selectVariableWithTree{true};
    bool showAreaDirTree{true};
    bool expandChooserItems{true};
    bool saveExpanded{true};
    bool copyConverted{};
    bool useIgnitionType{true};
    std::size_t comboVisibleItemCount{20U};
    std::int32_t flagsWidth{260};
    std::int32_t flagsHeight{180};
    std::int32_t idColumn{50};
    std::int32_t nameTableWidth{300};
    std::int32_t nameTableHeight{180};
    std::function<void(std::int32_t)> storeIdColumn{};
    String localVariablePrefix{"Local"};
    String randomValue{"??Random"};
    String selectedPlayerCardNumber{"??SelectedPlayer"};
    ChooserImages images{};
    ChooserMessages messages{};
    std::vector<String> standardCoupons{};
    std::vector<String> standardCouponsForValued{};
    std::function<std::vector<String>(CouponComboType)> couponStandards{};
    std::vector<String> standardKeyCodes{};
    std::vector<String> standardSelections{};
    std::function<std::vector<String>(StringView)> standardKeyCodesForSkin{};
    std::function<std::vector<String>(StringView)> standardSelectionsForSkin{};
    std::function<bool(StringView, CouponComboType)> couponFilter{};
    std::function<void(StringView, bool)> openCWXPath{};
    std::function<String(StringView, CouponType, bool)> convertCoupon{};
    std::function<String(StringView, FKCKind)> convertKeyCode{};
    // Editorの通知を購読しない単独APIの呼び出し元も、同じ共有プ
    // レビューコンテキストを渡せる。 Propsは呼び出し元が所有する。
    ::cwxeditor::cwx::editor::gui::dwt::dprops::Props* editor_props{};
    std::function<::cwxeditor::cwx::skin::Skin::Ptr()> previewSkin{};
};

using Props = ChooserProperties;

/// createCouponComboのD形式の版で使う出力引数。 コ
/// ントローラーは任意で、必要なら文字列のアダプターだけを保持できる。
struct TextMenuModify {
    customtext::TMM text{};
    std::shared_ptr<TextMenuModifyCore> controller{};
    dmenu::TextMenuModifyPtr inputBinding{};

    [[nodiscard]] bool empty() const noexcept { return !controller || text.empty(); }
    [[nodiscard]] String getText() const { return text.getText(); }
    void setText(StringView value) { text.setText(value); }
};

/// 最小限のlib::ComboBoxの項目だけでは表せない、メニュー
/// ・検索状態を持つComboBox。 通常のlib部品として扱える。
class ChooserCombo final : public customtext::CCombo {
public:
    using CCombo::CCombo;

    std::shared_ptr<lib::Menu> menu{std::make_shared<lib::Menu>(true)};
    std::shared_ptr<incsearch::IncSearch> incrementalSearch{};
    std::shared_ptr<TextMenuModifyCore> textMenu{};
    dmenu::TextMenuModifyPtr textInputBinding{};
    std::function<void()> refresh{};
    std::function<bool()> catchModify{};
    bool hasItem{};
    std::shared_ptr<void> notificationBinding{};
    std::shared_ptr<void> couponPreviewBinding{};
};

using ComboPtr = std::shared_ptr<ChooserCombo>;

// データの取得。各関数に既定プロパティ版を用意し、 元の三引
// 数のD関数を使う呼び出し元が同じ手順を使えるようにする。
[[nodiscard]] std::vector<String> addInitValue(
    const Commons& comm, std::vector<String> values, StringView initValue,
    const ChooserProperties& properties = {});
[[nodiscard]] std::vector<String> allCoupons(
    const Commons& comm, const SummaryPtr& summ, CouponComboType type,
    const ChooserProperties& properties = {});
/// 共通の使用済みクーポンフィルターを設定する。 システムクーポ
/// ンと、呼び出し元の正式な候補群で既に除外された任意名を省く。
void setCouponFilter(ChooserProperties& properties,
                     std::function<bool(StringView)> isSystemCoupon,
                     std::vector<String> excluded = {});
/// 部品の候補一覧が開いている間、共有クーポン一覧の通知元を
/// 保持する。 返された所有ハンドルを解放すると接続を外す。
[[nodiscard]] std::shared_ptr<void> watchCouponChanges(
    ::cwxeditor::cwx::editor::gui::dwt::commons::Commons* publisher,
    lib::Action refresh);
[[nodiscard]] std::vector<String> selectionStandards(
    const ChooserProperties& properties);
[[nodiscard]] std::shared_ptr<void> watchSelectionChanges(
    ::cwxeditor::cwx::editor::gui::dwt::commons::Commons* publisher,
    lib::Action refresh);
[[nodiscard]] std::shared_ptr<void> watchCardGroupChanges(
    ::cwxeditor::cwx::editor::gui::dwt::commons::Commons* publisher,
    lib::Action refresh);
[[nodiscard]] std::shared_ptr<void> watchVariableChanges(
    ::cwxeditor::cwx::editor::gui::dwt::commons::Commons* publisher,
    VariableKind kind, lib::Action refresh);
[[nodiscard]] std::vector<String> allGossips(
    const Commons& comm, const SummaryPtr& summ,
    const ChooserProperties& properties = {});
[[nodiscard]] std::vector<String> allCompleteStamps(
    const Commons& comm, const SummaryPtr& summ,
    const ChooserProperties& properties = {});
[[nodiscard]] std::vector<String> allKeyCodes(
    const Commons& comm, const SummaryPtr& summ,
    const ChooserProperties& properties = {});
[[nodiscard]] std::vector<String> allCellNames(
    const Commons& comm, const SummaryPtr& summ,
    const ChooserProperties& properties = {});
[[nodiscard]] std::vector<String> allCardGroups(
    const Commons& comm, const SummaryPtr& summ,
    const ChooserProperties& properties = {});

/// 選択メニューへ変換・コピー操作を登録する。 コマンド文字列は
/// ヘッドレスでも固定とし、メニューの列挙IDに依存させない。
void createCouponTypeMenu(const Commons& comm, const ComboPtr& combo,
                          bool convert, bool copy,
                          const ChooserProperties& properties = {});
void createCouponTypeMenu(const Commons& comm, const std::shared_ptr<lib::ComboBox>& combo,
                          const std::shared_ptr<lib::Menu>& menu, bool convert, bool copy,
                          const ChooserProperties& properties = {});
void createKeyCodeTimingMenu(const Commons& comm, const ComboPtr& combo,
                             bool convert, bool copy,
                             const ChooserProperties& properties = {});

// 同じタイミング操作を、ネイティブの表に表示されるComboにも付ける。
void createKeyCodeTimingMenu(const Commons& comm,
    const std::shared_ptr<lib::ComboBox>& combo, const std::shared_ptr<lib::Menu>& menu,
    bool convert, bool copy, const ChooserProperties& properties = {});

namespace detail {

// DのKeyCodeファクトリーはバイト順、公開のallKeyCodesはicmp/incmp順を使う。
[[nodiscard]] std::vector<String> keyCodeFactoryValues(
    const Commons& comm, const SummaryPtr& summ, const ChooserProperties& properties);

struct ComboState {
    std::shared_ptr<incsearch::IncSearch> search{};
    std::shared_ptr<void> notificationBinding{};
    std::function<std::vector<String>()> values{};
    std::function<void()> refresh{};
    String initial{};
    bool hasItem{};
    bool firstPopulation{true};
    bool separateInitial{};
};

void populateCombo(const std::shared_ptr<lib::ComboBox>& combo,
                   const std::shared_ptr<ComboState>& state,
                   StringView initValue);
void attachTextMenu(const Commons& comm, const std::shared_ptr<lib::ComboBox>& combo,
                    const std::function<bool()>& catchMod,
                    TextMenuModify* out = nullptr);
void attachSearchMenu(const std::shared_ptr<lib::ComboBox>& combo,
                      const std::shared_ptr<ComboState>& state,
                      std::weak_ptr<lib::GuiBackend> gui,
                      bool allowSearch, StringView label);
ComboPtr asChooserCombo(const std::shared_ptr<lib::ComboBox>& combo);
ChooserProperties chooserProperties(const ChooserProperties& properties);
incsearch::IncSearchSettings searchSettings(const ChooserProperties& properties);
void storeSearchType(const ChooserProperties& properties,
                     incsearch::SearchType type);
void attachCouponRefresh(const ComboPtr& combo,
                         const ChooserProperties& properties);
void attachCouponSpecialChars(const ComboPtr& combo, const Commons& comm,
                              const SummaryPtr& summ, const UseCounterPtr& uc,
                              const ChooserProperties& properties);
enum class NameRefreshKind { Gossip, CompleteStamp, KeyCode, CellName };
void attachNameRefresh(const ComboPtr& combo, const ChooserProperties& properties,
                       NameRefreshKind kind);

template <typename T>
[[nodiscard]] std::shared_ptr<T> makeCombo(Parent parent) {
    static_assert(std::is_base_of_v<lib::ComboBox, T>,
                  "chooser combo type must derive from lib::ComboBox");
    auto result = std::make_shared<T>();
    result->editable = true;
    if (parent) parent->add(result);
    return result;
}

template <typename T>
void rememberState(const Commons& comm, const std::shared_ptr<T>& combo,
                   const std::shared_ptr<ComboState>& state) {
    const std::weak_ptr<T> weak_combo = combo;
    combo->on_focus = [state, weak_combo] {
        if (const auto target = weak_combo.lock()) {
            state->search->checkEnabled();
            state->search->handleFocusChanged(target.get());
        }
    };
    combo->on_blur = [state, gui = comm.services.gui] {
        if (gui) state->search->handleFocusChanged(gui->focused_widget().get());
    };
    combo->on_visibility_changed = [state](const bool visible) {
        if (!visible) state->search->close();
        else state->search->checkEnabled();
    };
    if (comm.services.gui) {
        if (const auto owner = comm.services.gui->window_owner(combo)) {
            const auto previous_bounds = owner->on_bounds_changed;
            const std::weak_ptr<ComboState> weak_state = state;
            owner->on_bounds_changed = [previous_bounds, weak_state](const lib::Rect bounds) {
                if (previous_bounds) previous_bounds(bounds);
                if (const auto current = weak_state.lock()) current->search->resize();
            };
        }
    }
}

template <typename T>
[[nodiscard]] std::shared_ptr<T> createValueCombo(
    const Commons& comm, Parent parent, const std::function<bool()>& catchMod,
    StringView initValue, std::function<std::vector<String>()> values,
    const ChooserProperties& properties, bool textMenu = true,
    bool providerSeparatesInitial = false,
    std::function<std::shared_ptr<void>(lib::Action)> refreshBinding = {}) {
    auto combo = makeCombo<T>(parent);
    combo->visible_item_count = properties.comboVisibleItemCount;
    auto state = std::make_shared<ComboState>();
    state->initial = String(initValue);
    state->separateInitial = properties.showCurrentValueOnTopAlways && !providerSeparatesInitial;
    state->values = std::move(values);
    const std::weak_ptr<ComboState> weak_state = state;
    const std::weak_ptr<T> weak_combo = combo;
    state->search = std::make_shared<incsearch::IncSearch>(
        comm.services, combo.get(), [weak_state] {
            const auto current = weak_state.lock();
            return current && current->hasItem;
        }, std::vector<incsearch::AdditionMatcher>{}, searchSettings(properties));
    state->refresh = [weak_combo, weak_state, init = String(initValue)] {
        const auto target = weak_combo.lock();
        const auto current = weak_state.lock();
        if (target && current)
            populateCombo(std::static_pointer_cast<lib::ComboBox>(target), current, init);
    };
    if (refreshBinding) state->notificationBinding = refreshBinding(state->refresh);
    state->search->modEvent.push_back([weak_state, properties] {
        const auto current = weak_state.lock();
        if (!current) return;
        storeSearchType(properties, current->search->searchType());
        current->refresh();
    });
    rememberState(comm, combo, state);
    combo->edit_text = String(initValue);
    state->refresh();
    attachSearchMenu(std::static_pointer_cast<lib::ComboBox>(combo), state,
                     comm.services.gui, true,
                     properties.messages.incrementalSearch);
    if (textMenu) attachTextMenu(comm, std::static_pointer_cast<lib::ComboBox>(combo),
                                 catchMod);
    if (auto chooser = asChooserCombo(std::static_pointer_cast<lib::ComboBox>(combo))) {
        chooser->incrementalSearch = state->search;
        chooser->refresh = state->refresh;
        chooser->catchModify = catchMod;
        chooser->hasItem = state->hasItem;
    }
    (void)properties;
    return combo;
}

}  // namespace detail

template <typename T = ChooserCombo>
[[nodiscard]] std::shared_ptr<T> createCouponCombo(
    const Commons& comm, const SummaryPtr& summ, const UseCounterPtr& uc,
    Parent parent, std::function<bool()> catchMod, CouponComboType type,
    String initValue, bool expandSPChars = false,
    const ChooserProperties& properties = {}) {
    auto combo = detail::createValueCombo<T>(
        comm, parent, catchMod, initValue,
        [comm, summ, type, initValue, properties] {
            return addInitValue(comm, allCoupons(comm, summ, type, properties),
                                initValue, properties);
        },
        properties, true, true);
    if (auto chooser = detail::asChooserCombo(std::static_pointer_cast<lib::ComboBox>(combo))) {
        createCouponTypeMenu(comm, chooser, true, true, properties);
        detail::attachCouponRefresh(chooser, properties);
        if (expandSPChars) detail::attachCouponSpecialChars(chooser, comm, summ, uc, properties);
    }
    return combo;
}

template <typename T = ChooserCombo>
[[nodiscard]] std::shared_ptr<T> createCouponCombo(
    const Commons& comm, const SummaryPtr& summ, const UseCounterPtr& uc,
    Parent parent, std::function<bool()> catchMod, CouponComboType type,
    String initValue, TextMenuModify& tmm, bool expandSPChars = false,
    const ChooserProperties& properties = {}) {
    auto combo = createCouponCombo<T>(comm, summ, uc, parent, std::move(catchMod), type,
                                      initValue, expandSPChars, properties);
    detail::attachTextMenu(comm, std::static_pointer_cast<lib::ComboBox>(combo),
                           {}, &tmm);
    return combo;
}

template <typename T = ChooserCombo>
[[nodiscard]] std::shared_ptr<T> createGossipCombo(
    const Commons& comm, const SummaryPtr& summ, Parent parent,
    std::function<bool()> catchMod, String initValue,
    const ChooserProperties& properties = {}) {
    auto combo = detail::createValueCombo<T>(
        comm, parent, catchMod, initValue,
        [comm, summ, initValue, properties] {
            return addInitValue(comm, allGossips(comm, summ, properties), initValue, properties);
        }, properties, true, true);
    if (auto chooser = detail::asChooserCombo(std::static_pointer_cast<lib::ComboBox>(combo)))
        detail::attachNameRefresh(chooser, properties, detail::NameRefreshKind::Gossip);
    return combo;
}

template <typename T = ChooserCombo>
[[nodiscard]] std::shared_ptr<T> createCompleteStampCombo(
    const Commons& comm, const SummaryPtr& summ, Parent parent,
    std::function<bool()> catchMod, String initValue,
    const ChooserProperties& properties = {}) {
    auto combo = detail::createValueCombo<T>(
        comm, parent, catchMod, initValue,
        [comm, summ, initValue, properties] {
            return addInitValue(comm, allCompleteStamps(comm, summ, properties), initValue, properties);
        }, properties, true, true);
    if (auto chooser = detail::asChooserCombo(std::static_pointer_cast<lib::ComboBox>(combo)))
        detail::attachNameRefresh(chooser, properties,
                                  detail::NameRefreshKind::CompleteStamp);
    return combo;
}

template <typename T = ChooserCombo>
[[nodiscard]] std::shared_ptr<T> createKeyCodeCombo(
    const Commons& comm, const SummaryPtr& summ, Parent parent,
    std::function<bool()> catchMod, String initValue, bool withIgnitionType,
    std::function<String()> skinType = {},
    std::function<std::vector<String>()> standardKeyCodes = {},
    const ChooserProperties& properties = {}) {
    auto values = [comm, summ, skinType, standardKeyCodes, initValue, properties] {
        auto p = properties;
        if (standardKeyCodes) p.standardKeyCodes = standardKeyCodes();
        else if (skinType && p.standardKeyCodesForSkin)
            p.standardKeyCodes = p.standardKeyCodesForSkin(skinType());
        // Dのファクトリーは、公開のallKeyCodesと異なりバイト順で並べる。
        return addInitValue(comm, detail::keyCodeFactoryValues(comm, summ, p), initValue, p);
    };
    auto combo = detail::createValueCombo<T>(comm, parent, catchMod, initValue,
                                              std::move(values), properties, true, true);
    if (auto chooser = detail::asChooserCombo(std::static_pointer_cast<lib::ComboBox>(combo))) {
        createKeyCodeTimingMenu(comm, chooser, withIgnitionType, true, properties);
        detail::attachNameRefresh(chooser, properties, detail::NameRefreshKind::KeyCode);
    }
    return combo;
}

template <typename T = ChooserCombo>
[[nodiscard]] std::shared_ptr<T> createCellNameCombo(
    const Commons& comm, const SummaryPtr& summ, Parent parent,
    std::function<bool()> catchMod, String initValue,
    const ChooserProperties& properties = {}) {
    auto combo = detail::createValueCombo<T>(
        comm, parent, catchMod, initValue,
        [comm, summ, initValue, properties] {
            return addInitValue(comm, allCellNames(comm, summ, properties), initValue, properties);
        }, properties, true, true);
    if (summ && !summ->scenarioPath().empty())
        if (auto chooser = detail::asChooserCombo(
                std::static_pointer_cast<lib::ComboBox>(combo)))
            detail::attachNameRefresh(chooser, properties, detail::NameRefreshKind::CellName);
    return combo;
}

template <typename T = ChooserCombo>
[[nodiscard]] std::shared_ptr<T> createCardGroupCombo(
    const Commons& comm, const SummaryPtr& summ, Parent parent,
    std::function<bool()> catchMod, String initValue,
    const ChooserProperties& properties = {}) {
    return detail::createValueCombo<T>(
        comm, parent, catchMod, initValue,
        [comm, summ, initValue, properties] {
            if (!summ && !comm.source.keys) return std::vector<String>{};
            return addInitValue(comm, allCardGroups(comm, summ, properties), initValue, properties);
        }, properties, true, true,
        [publisher = properties.editor_commons, summ,
         has_source_keys = static_cast<bool>(comm.source.keys)](lib::Action refresh) {
            const bool loaded_summary = summ && !summ->scenarioPath().empty();
            const bool standalone_source = !summ && has_source_keys;
            if (!loaded_summary && !standalone_source) return std::shared_ptr<void>{};
            // 単独のSourceアダプターはSummaryなしでCardGroupsを提供
            // できる。 この追加経路でも、ホストからのCardGroup変更に追従する。
            return watchCardGroupChanges(publisher, std::move(refresh));
        });
}

/// すべてのFlagChooser<F, ...>で使う共通実装。
class FlagChooserBase {
public:
    using Callback = lib::Action;

    FlagChooserBase(Commons comm, SummaryPtr summ, UseCounterPtr uc, Parent parent,
                    bool saveExpanded, bool canSelectNothing, bool random,
                    VariableKind kind,
                    ChooserProperties properties = {});
    virtual ~FlagChooserBase();

    std::vector<Callback> modEvent;
    std::vector<Callback> modEventWithSame;

    [[nodiscard]] WidgetPtr widget() const noexcept;
    [[nodiscard]] std::shared_ptr<incsearch::IncSearch> flagIncSearchControl() const noexcept {
        return flagIncSearch_;
    }
    void flagIncSearch();
    void refFlags(const flag::FlagList& flags, const flag::StepList& steps,
                  const flag::VariantList& variants);
    void delFlags(const flag::FlagList& flags, const flag::StepList& steps,
                  const flag::VariantList& variants);
    [[nodiscard]] ExpandedMap& expandedTable() noexcept { return expanded_; }
    [[nodiscard]] const ExpandedMap& expandedTable() const noexcept { return expanded_; }
    [[nodiscard]] std::vector<std::shared_ptr<flag::Flag>> flags(
        const flag::FlagDirPtr& dir) const;
    [[nodiscard]] std::vector<std::shared_ptr<flag::Flag>> allFlags(
        const flag::FlagDirPtr& dir) const;
    [[nodiscard]] bool hasVar(const flag::FlagDirPtr& dir) const;
    [[nodiscard]] std::vector<std::shared_ptr<flag::Step>> steps(
        const flag::FlagDirPtr& dir) const;
    [[nodiscard]] std::vector<std::shared_ptr<flag::Step>> allSteps(
        const flag::FlagDirPtr& dir) const;
    [[nodiscard]] bool hasStep(const flag::FlagDirPtr& dir) const;
    [[nodiscard]] std::vector<std::shared_ptr<flag::Variant>> variants(
        const flag::FlagDirPtr& dir) const;
    [[nodiscard]] std::vector<std::shared_ptr<flag::Variant>> allVariants(
        const flag::FlagDirPtr& dir) const;
    [[nodiscard]] bool hasVariant(const flag::FlagDirPtr& dir) const;
    void refreshFlags();
    void openFlagView();
    [[nodiscard]] bool canOpenView() const;
    void saveExpanded();
    void initControl();
    void selected(String path);
    [[nodiscard]] const String& selected() const noexcept { return selected_; }
    [[nodiscard]] String selectedWithDir() const;

    [[nodiscard]] const std::shared_ptr<lib::TreeView>& tree() const noexcept { return tree_; }
    [[nodiscard]] const customtable::TableModelPtr& list() const noexcept { return list_; }
    [[nodiscard]] const std::shared_ptr<lib::CheckBox>& allExpandedButton() const noexcept {
        return all_expanded_;
    }

public:
    struct Choice {
        String path;
        String name;
        String directory;
        String cwx_path;
        ResourceId image{};
        bool directory_item{};
        bool special{};
        bool selected_player{};
        bool random{};
    };
protected:
    [[nodiscard]] std::vector<Choice> choices() const;
    [[nodiscard]] const ChooserProperties& properties() const noexcept { return properties_; }
    [[nodiscard]] const Commons& commons() const noexcept { return comm_; }
    [[nodiscard]] SummaryPtr summaryOwner() const noexcept { return summ_; }
    [[nodiscard]] UseCounterPtr useCounter() const noexcept { return uc_; }
    [[nodiscard]] VariableKind variableKind() const noexcept { return kind_; }
    void selectChoice(const Choice& value);

private:
    Commons comm_{};
    SummaryPtr summ_{};
    UseCounterPtr uc_{};
    ChooserProperties properties_{};
    // 閉じた選択部品が、通知元のMainWindowより長く残る場合がある
    // 。 Dと同じ終了の区切りを保ち、破棄済みのC++所有者を参照しない。
    std::weak_ptr<const int> editor_commons_lifetime_;
    [[nodiscard]] ::cwxeditor::cwx::editor::gui::dwt::commons::Commons*
    liveEditorCommons() const noexcept {
        return editor_commons_lifetime_.expired() ? nullptr : properties_.editor_commons;
    }
    VariableKind kind_{VariableKind::Flag};
    bool can_select_nothing_{};
    bool random_{};
    Parent parent_{};
    ContainerPtr panel_{};
    std::shared_ptr<lib::TreeView> tree_{};
    customtable::TableModelPtr list_{};
    std::shared_ptr<lib::CheckBox> all_expanded_{};
    std::shared_ptr<incsearch::IncSearch> flagIncSearch_{};
    ExpandedMap expanded_{};
    String selected_{};
    String selected_with_dir_{};
    String search_query_{};
    bool can_inc_search_{};
    bool save_expanded_{};
    std::unordered_map<lib::TreeItemId, Choice> tree_choices_{};
    std::vector<Choice> visible_choices_{};
    std::uint64_t ref_flag_connection_{};
    std::uint64_t del_flag_connection_{};
    std::uint64_t ref_flag_dir_connection_{};
    std::uint64_t del_flag_dir_connection_{};
    std::uint64_t ref_skin_connection_{};
    std::uint64_t ref_data_version_connection_{};
    std::uint64_t repl_text_connection_{};
    std::uint64_t repl_id_connection_{};
    std::uint64_t select_style_connection_{};

    void connectObservers();
    void disconnectObservers() noexcept;
    void setSelectedFromTree(const std::vector<lib::TreeItemId>& values);
    void setSelectedFromList(const std::vector<std::size_t>& values);
    void notifySelection(StringView old);
    [[nodiscard]] bool matches(StringView value) const;
    struct PreparedTreeChoices {
        std::vector<lib::TreeNode> nodes;
        std::unordered_map<lib::TreeItemId, Choice> choices;
        lib::TreeItemId next_id{};
    };
    [[nodiscard]] PreparedTreeChoices prepareTreeChoices(const std::vector<Choice>& values) const;
    lib::TreeItemId next_tree_id_{1};
    std::uint64_t refresh_revision_{};
    // 最後のメンバーなので、破棄時は部品・提供元より先に無効になる。
    std::shared_ptr<int> callback_lifetime_{std::make_shared<int>(0)};
};

template <typename F, bool CanSelNothing, bool Random = false>
class FlagChooser final : public FlagChooserBase {
public:
    using Value = F;
    static constexpr bool canSelectNothing = CanSelNothing;
    static constexpr bool randomValue = Random;

    FlagChooser(Commons comm, SummaryPtr summ, UseCounterPtr uc, Parent parent,
                bool saveExpanded = true, ChooserProperties properties = {})
        : FlagChooserBase(std::move(comm), std::move(summ), std::move(uc), parent,
                          saveExpanded, CanSelNothing, Random, kindFor(),
                          std::move(properties)) {}

    [[nodiscard]] std::vector<std::shared_ptr<F>> flags(const flag::FlagDirPtr& dir) const {
        if constexpr (std::is_same_v<F, flag::Flag>) return FlagChooserBase::flags(dir);
        else if constexpr (std::is_same_v<F, flag::Step>) return steps(dir);
        else return variants(dir);
    }
    [[nodiscard]] std::vector<std::shared_ptr<F>> allFlags(const flag::FlagDirPtr& dir) const {
        if constexpr (std::is_same_v<F, flag::Flag>) return FlagChooserBase::allFlags(dir);
        else if constexpr (std::is_same_v<F, flag::Step>) return allSteps(dir);
        else return allVariants(dir);
    }
    [[nodiscard]] bool hasVar(const flag::FlagDirPtr& dir) const {
        if constexpr (std::is_same_v<F, flag::Flag>) return FlagChooserBase::hasVar(dir);
        else if constexpr (std::is_same_v<F, flag::Step>) return hasStep(dir);
        else return hasVariant(dir);
    }

private:
    static constexpr VariableKind kindFor() {
        if constexpr (std::is_same_v<F, flag::Flag>) return VariableKind::Flag;
        else if constexpr (std::is_same_v<F, flag::Step>) return VariableKind::Step;
        else return VariableKind::Variant;
    }
};

template <typename T = ChooserCombo>
[[nodiscard]] std::shared_ptr<T> createSelectionCombo(
    const Commons& comm, Parent parent, std::function<bool()> catchMod,
    String initValue, const ChooserProperties& properties = {}) {
    return detail::createValueCombo<T>(
        comm, parent, catchMod, initValue,
        [comm, initValue, properties] {
            auto values = selectionStandards(properties);
            if (initValue.empty()) values.insert(values.begin(), {});
            return addInitValue(comm, std::move(values), initValue, properties);
        }, properties, true, true,
        [publisher = properties.editor_commons](lib::Action refresh) {
            return watchSelectionChanges(publisher, std::move(refresh));
        });
}

template <typename T = ChooserCombo, typename F = flag::Flag>
[[nodiscard]] std::shared_ptr<T> createVariableCombo(
    const Commons& comm, const SummaryPtr& summ, const UseCounterPtr& uc,
    Parent parent, std::function<bool()> catchMod, String initValue,
    const ChooserProperties& properties = {}) {
    static_assert(std::is_same_v<F, flag::Flag> || std::is_same_v<F, flag::Step> ||
                  std::is_same_v<F, flag::Variant>,
                  "variable combo type must match D Flag, Step, or Variant");
    constexpr VariableKind kind = [] {
        if constexpr (std::is_same_v<F, flag::Flag>) return VariableKind::Flag;
        else if constexpr (std::is_same_v<F, flag::Step>) return VariableKind::Step;
        else return VariableKind::Variant;
    }();
    return detail::createValueCombo<T>(
        comm, parent, catchMod, initValue,
        [summ, uc, properties] {
            std::vector<String> result;
            if (!summ) return result;
            const auto root = summ->flagDirRoot();
            const auto vars = flag::allVars<F>(root, uc);
            result.reserve(vars.size());
            for (const auto& value : vars) if (value) result.push_back(value->path());
            std::stable_sort(result.begin(), result.end(), [logical = properties.logicalSort](
                const String& left, const String& right) {
                return logical ? (::cwxeditor::cwx::utils::ncmp(left, right) < 0)
                                : (left < right);
            });
            return result;
        }, properties, true, false,
        [publisher = properties.editor_commons, kind](lib::Action refresh) {
            return watchVariableChanges(publisher, kind, std::move(refresh));
        });
}

enum class AreaChoiceKind { Area, Battle, Package, Cast, Info };

/// AreaChooser<A, StartArea>の共通実装。
class AreaChooserBase {
public:
    AreaChooserBase(Commons comm, SummaryPtr summ, Parent parent,
                    AreaChoiceKind kind, bool startArea,
                    ChooserProperties properties = {});
    virtual ~AreaChooserBase();
    AreaChooserBase(const AreaChooserBase&) = delete;
    AreaChooserBase& operator=(const AreaChooserBase&) = delete;
    AreaChooserBase(AreaChooserBase&&) = delete;
    AreaChooserBase& operator=(AreaChooserBase&&) = delete;

    std::vector<lib::Action> modEvent;
    [[nodiscard]] WidgetPtr widget() const noexcept;
    void areaIncSearch();
    void refA();
    [[nodiscard]] ExpandedMap& expandedTable() noexcept { return expanded_; }
    [[nodiscard]] const ExpandedMap& expandedTable() const noexcept { return expanded_; }
    void saveExpanded();
    void refreshAreas();
    void updateImages();
    [[nodiscard]] ResourceId image(lib::TreeItemId item) const;
    void openAreaView();
    [[nodiscard]] bool canOpenView() const;
    void initControl();
    void selected(std::uint64_t id);
    [[nodiscard]] std::uint64_t selected() const noexcept { return selected_; }
    void startArea(std::uint64_t id);
    [[nodiscard]] std::uint64_t startArea() const noexcept { return start_area_; }

    [[nodiscard]] const std::shared_ptr<lib::TreeView>& tree() const noexcept { return tree_; }
    [[nodiscard]] const customtable::TableModelPtr& list() const noexcept { return list_; }
    [[nodiscard]] const std::shared_ptr<lib::CheckBox>& allExpandedButton() const noexcept {
        return all_expanded_;
    }
    [[nodiscard]] const std::shared_ptr<incsearch::IncSearch>& areaIncSearchControl() const noexcept {
        return areaIncSearch_;
    }

public:
    struct Choice {
        std::uint64_t id{};
        String name;
        String base_name;
        String directory;
        String cwx_path;
        ResourceId image{};
    };
protected:
    [[nodiscard]] const ChooserProperties& properties() const noexcept { return properties_; }
    [[nodiscard]] AreaChoiceKind choiceKind() const noexcept { return kind_; }
    [[nodiscard]] const SummaryPtr& summaryOwner() const noexcept { return summ_; }
    [[nodiscard]] std::vector<Choice> choices() const;

private:
    Commons comm_{};
    SummaryPtr summ_{};
    Parent parent_{};
    ContainerPtr panel_{};
    std::shared_ptr<lib::TreeView> tree_{};
    customtable::TableModelPtr list_{};
    std::shared_ptr<lib::CheckBox> all_expanded_{};
    std::shared_ptr<incsearch::IncSearch> areaIncSearch_{};
    ExpandedMap expanded_{};
    AreaChoiceKind kind_{AreaChoiceKind::Area};
    ChooserProperties properties_{};
    // 閉じた選択部品が、通知元のMainWindowより長く残る場合がある
    // 。 Dと同じ終了の区切りを保ち、破棄済みのC++所有者を参照しない。
    std::weak_ptr<const int> editor_commons_lifetime_;
    [[nodiscard]] ::cwxeditor::cwx::editor::gui::dwt::commons::Commons*
    liveEditorCommons() const noexcept {
        return editor_commons_lifetime_.expired() ? nullptr : properties_.editor_commons;
    }
    std::uint64_t selected_{};
    std::uint64_t start_area_{};
    bool start_area_enabled_{};
    bool can_inc_search_{};
    std::unordered_map<lib::TreeItemId, Choice> tree_choices_{};
    std::vector<Choice> visible_choices_{};
    std::uint64_t ref_connection_{};
    std::uint64_t del_connection_{};
    std::uint64_t ref_area_table_connection_{};
    std::uint64_t table_style_connection_{};
    std::uint64_t ref_skin_connection_{};
    std::uint64_t ref_data_version_connection_{};
    std::uint64_t repl_text_connection_{};
    std::uint64_t repl_id_connection_{};

    void connectObservers();
    void disconnectObservers() noexcept;
    void setSelectedFromTree(const std::vector<lib::TreeItemId>& values);
    void setSelectedFromList(const std::vector<std::size_t>& values);
    void notifySelection(std::uint64_t old, bool selection_event = false);
    [[nodiscard]] bool matches(StringView value) const;
    struct PreparedTreeChoices {
        std::vector<lib::TreeNode> nodes;
        std::unordered_map<lib::TreeItemId, Choice> choices;
        lib::TreeItemId next_id{};
    };
    [[nodiscard]] PreparedTreeChoices prepareTreeChoices(
        const std::vector<Choice>& values, std::uint64_t start) const;
    void updateImages(std::uint64_t start);
    void activateChoice(std::uint64_t id);
    lib::TreeItemId next_tree_id_{1};
    std::uint64_t refresh_revision_{};
    // 最初に破棄し、保持された部品のコールバックからコントローラーを使わせない。
    std::shared_ptr<int> callback_lifetime_{std::make_shared<int>(0)};
};

template <typename A, bool StartArea>
class AreaChooser final : public AreaChooserBase {
public:
    AreaChooser(Commons comm, SummaryPtr summ, Parent parent,
                ChooserProperties properties = {})
        : AreaChooserBase(std::move(comm), std::move(summ), parent, kindFor(), StartArea,
                          std::move(properties)) {}

    void refA(const std::shared_ptr<A>&) { AreaChooserBase::refA(); }

    template <bool Enabled = StartArea>
    requires Enabled
    void startArea(std::uint64_t id) { AreaChooserBase::startArea(id); }

    template <bool Enabled = StartArea>
    requires Enabled
    [[nodiscard]] std::uint64_t startArea() const { return AreaChooserBase::startArea(); }

private:
    static constexpr AreaChoiceKind kindFor() {
        if constexpr (std::is_same_v<A, area::Area>) return AreaChoiceKind::Area;
        else if constexpr (std::is_same_v<A, area::Battle>) return AreaChoiceKind::Battle;
        else if constexpr (std::is_same_v<A, area::Package>) return AreaChoiceKind::Package;
        else if constexpr (std::is_same_v<A, card::CastCard>) return AreaChoiceKind::Cast;
        else return AreaChoiceKind::Info;
    }
};

void checkAllExpanded(const std::shared_ptr<lib::CheckBox>& button,
                      const std::shared_ptr<lib::TreeView>& tree);
[[nodiscard]] std::shared_ptr<lib::CheckBox> createAllExpandedButton(
    const ChooserProperties& properties, Parent parent,
    const std::shared_ptr<lib::TreeView>& tree);

}  // namespace cwxeditor::cwx::editor::gui::dwt::chooser
