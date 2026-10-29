#include "cwxeditor_src/gui/cwx/editor/gui/dwt/scenehost.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/eventdialog.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/commons.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dutils.hpp"
#include "cw/lib/money.hpp"
// UIの実装は一つに統一し、
// 別経路の実装を設けない。
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/bgimagedialog.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/effectcarddialog.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/exprutils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/messageutils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/xmlcontentdialog.hpp"

#include "cwxeditor_src/core/cwx/msgs.hpp"
#include "cwxeditor_src/core/cwx/flag.hpp"
#include "cwxeditor_src/core/cwx/path.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/core/cwx/warning.hpp"
#include "cwxeditor_src/lib/xml.hpp"
#include "cwxeditor_src/lib/ui_window_lifecycle.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"
#include "cwxeditor_src/lib/ui_metrics.hpp"
#include "cwxeditor_src/lib/window_host.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace cwxeditor::cwx::editor::gui::dwt::eventdialog {

namespace {

const msgs::Msgs& contentDialogMessages(const Context& context);

[[nodiscard]] WarningList typedAudioWarnings(
    const materialselect::MaterialSelectBase& selector, const Context& context,
    const SummaryPtr& summary) {
    // Dの型付き音声ダイアログが警告を構成する。解決済みの選択ファイ
    // ルを先に調べ、オプション警告を追加する。型付きモデルアダプタ
    // ーは指定パスの音声検査を省き、局所的な警告経路を一つにする。
    const auto path = selector.filePath();
    const bool legacy = summary && summary->legacy();
    WarningList result;
    if (context.prop && context.editor_props) {
        const auto& target_version = context.editor_props->var().targetVersion.value;
        if (selector.type() == materialselect::MtType::BGM) {
            const auto file_warnings = Skin::warningBGM(
                *context.prop, path, legacy, target_version);
            result.insert(result.end(), file_warnings.begin(), file_warnings.end());
        } else if (selector.type() == materialselect::MtType::SE) {
            const auto file_warnings = Skin::warningSE(
                *context.prop, path, legacy, target_version);
            result.insert(result.end(), file_warnings.begin(), file_warnings.end());
        }
    }
    const auto option_warnings = selector.warnings();
    result.insert(result.end(), option_warnings.begin(), option_warnings.end());
    return result;
}

[[nodiscard]] WarningList branchLocalWarnings(
    const Context& context, const SummaryPtr& summary, const bool includeStatus,
    const types::Status status, const bool invertResult) {
    WarningList result;
    const auto* editorProps = context.editor_props;
    if (includeStatus && editorProps && status != types::Status::None) {
        const auto& messages = editorProps->requiredMsgs();
        const auto statusValue = static_cast<std::int32_t>(status);
        if (!editorProps->targetVersion(summary.get(), "1.30")) {
            if (statusValue >= static_cast<std::int32_t>(types::Status::Silence)) {
                result.push_back(utils::tryFormat(
                    messages.warningBranchStatusMental.value,
                    messages.statusName(status), "1.50"));
            } else if (statusValue >= static_cast<std::int32_t>(types::Status::Confuse)) {
                result.push_back(utils::tryFormat(
                    messages.warningBranchStatusMental.value,
                    messages.statusName(status), "1.30"));
            }
        } else if (!editorProps->targetVersion(summary.get(), "1.50") &&
                   statusValue >= static_cast<std::int32_t>(types::Status::Silence)) {
            result.push_back(utils::tryFormat(
                messages.warningBranchStatusMental.value,
                messages.statusName(status), "1.50"));
        }
    }
    if (invertResult && editorProps &&
        !editorProps->isTargetVersion(summary.get(), "4")) {
        result.push_back(editorProps->requiredMsgs().warningInvertResult.value);
    }
    return result;
}

[[nodiscard]] WarningList dTextWarnings2(
    const props::CProps& prop, const Skin& skin, const Summary* summary,
    const UseCounter* useCounter, const StringView targetVersion,
    const std::span<const String> texts) {
    const bool classic = summary && summary->legacy();
    const String wsnVersion = summary
        ? summary->dataVersion()
        : String(versioninfo::LATEST_VERSION);
    WarningList result;
    for (const auto& text : texts) {
        lib::Array<String> flags;
        lib::Array<String> steps;
        lib::Array<String> variants;
        lib::Array<String> fonts;
        std::vector<char> colors;
        msgutils::textUseItems(text, flags, steps, variants, fonts, colors);
        // DのtextWarnings2は変数と文字コードを検証するが、これらの
        // クーポン・ゴシップの呼び出しでは、文字ごとのフォント・画像警告を省く。
        fonts.clear();
        colors.clear();
        lib::Dictionary<String, bool> warnedFlags;
        lib::Dictionary<String, bool> warnedSteps;
        lib::Dictionary<String, bool> warnedVariants;
        lib::Dictionary<String, bool> warnedFonts;
        lib::Dictionary<char, bool> warnedColors;
        const auto textWarnings = warning::textWarnings(
            prop, skin, summary, useCounter, classic, wsnVersion, targetVersion,
            text, flags, steps, variants, fonts, colors, warnedFlags,
            warnedSteps, warnedVariants, warnedFonts, warnedColors).all;
        for (const auto& textWarning : textWarnings)
            if (std::find(result.begin(), result.end(), textWarning) == result.end())
                result.push_back(textWarning);
    }
    return result;
}

[[nodiscard]] int clampSpinnerSelection(const int value, const int minimum,
                                        const int maximum) {
    // DのSpinner#setSelectionは読み込み値を設定範囲へ制限す
    // る。NumberEditは初期値をそのまま保存するため、ここで正規化する。
    return std::clamp(value, minimum, maximum);
}

// スクショ/イベント設定のD版参照画像から計測し
// たクライアントサイズ。画像には撮影環境のWindows
// 枠（横16px、縦39px）が含まれるが、モ
// デルはクライアント矩形を保持する。この表を共
// 通ダイアログ経路に置き、入れ子の推奨サイズ変
// 更で移植先ごとに余白が増え続けるのを防ぐ。
[[nodiscard]] std::optional<lib::Size> dReferenceClientSize(const types::CType type) {
    using types::CType;
    switch (type) {
    case CType::StartBattle:          return lib::Size{315.0F, 290.0F};
    case CType::End:                  return lib::Size{308.0F, 102.0F};
    case CType::ChangeArea:           return lib::Size{314.0F, 352.0F};
    case CType::ChangeBgImage:        return lib::Size{965.0F, 564.0F};
    case CType::Effect:               return lib::Size{487.0F, 464.0F};
    case CType::EffectBreak:          return lib::Size{323.0F, 82.0F};
    case CType::LinkStart:            return lib::Size{312.0F, 309.0F};
    case CType::LinkPackage:          return lib::Size{310.0F, 286.0F};
    // 共通Editorの操作部に、二行のメッセージ設定が入る領域を確保
    // する。周囲の配置だけを変え、メッセージ幅や折り返しは変えない。
    case CType::TalkMessage:          return lib::Size{1180.0F, 410.0F};
    // Dの参照画像「セリフ.png」のクライアント幅は1272px。
    // 共通操作部の操作行を見えるように、既存の高さの余裕を維持する。
    case CType::TalkDialog:           return lib::Size{1272.0F, 620.0F};
    case CType::PlayBgm:              return lib::Size{311.0F, 426.0F};
    case CType::PlaySound:            return lib::Size{316.0F, 395.0F};
    case CType::Wait:                 return lib::Size{310.0F, 102.0F};
    case CType::CallStart:            return lib::Size{306.0F, 308.0F};
    case CType::CallPackage:          return lib::Size{317.0F, 291.0F};
    case CType::BranchFlag:           return lib::Size{313.0F, 333.0F};
    case CType::BranchMultiStep:      return lib::Size{319.0F, 360.0F};
    case CType::BranchStep:           return lib::Size{317.0F, 335.0F};
    case CType::BranchSelect:         return lib::Size{307.0F, 352.0F};
    case CType::BranchAbility:        return lib::Size{377.0F, 301.0F};
    case CType::BranchRandom:         return lib::Size{312.0F, 110.0F};
    case CType::BranchLevel:          return lib::Size{317.0F, 101.0F};
    case CType::BranchStatus:         return lib::Size{351.0F, 409.0F};
    case CType::BranchPartyNumber:    return lib::Size{309.0F, 87.0F};
    case CType::BranchCast:           return lib::Size{316.0F, 292.0F};
    case CType::BranchItem:           return lib::Size{401.0F, 343.0F};
    case CType::BranchSkill:          return lib::Size{406.0F, 343.0F};
    case CType::BranchInfo:           return lib::Size{312.0F, 291.0F};
    case CType::BranchBeast:          return lib::Size{404.0F, 335.0F};
    case CType::BranchMoney:          return lib::Size{309.0F, 103.0F};
    case CType::BranchCoupon:         return lib::Size{421.0F, 271.0F};
    case CType::BranchCompleteStamp:  return lib::Size{338.0F, 93.0F};
    case CType::BranchGossip:         return lib::Size{329.0F, 112.0F};
    case CType::SetFlag:              return lib::Size{317.0F, 364.0F};
    case CType::SetStep:              return lib::Size{315.0F, 330.0F};
    case CType::SetStepUp:            return lib::Size{320.0F, 332.0F};
    case CType::SetStepDown:          return lib::Size{317.0F, 330.0F};
    case CType::ReverseFlag:          return lib::Size{319.0F, 361.0F};
    case CType::CheckFlag:            return lib::Size{303.0F, 326.0F};
    case CType::GetCast:              return lib::Size{312.0F, 319.0F};
    case CType::GetItem:              return lib::Size{408.0F, 315.0F};
    case CType::GetSkill:             return lib::Size{400.0F, 311.0F};
    case CType::GetInfo:              return lib::Size{313.0F, 289.0F};
    case CType::GetBeast:             return lib::Size{398.0F, 310.0F};
    case CType::GetMoney:             return lib::Size{313.0F, 105.0F};
    case CType::GetCoupon:            return lib::Size{527.0F, 229.0F};
    case CType::GetCompleteStamp:     return lib::Size{334.0F, 91.0F};
    case CType::GetGossip:            return lib::Size{333.0F, 107.0F};
    case CType::LoseCast:             return lib::Size{306.0F, 288.0F};
    case CType::LoseItem:             return lib::Size{406.0F, 313.0F};
    case CType::LoseSkill:            return lib::Size{390.0F, 294.0F};
    case CType::LoseInfo:             return lib::Size{312.0F, 292.0F};
    case CType::LoseBeast:            return lib::Size{396.0F, 313.0F};
    case CType::LoseMoney:            return lib::Size{317.0F, 111.0F};
    case CType::LoseCoupon:           return lib::Size{527.0F, 229.0F};
    case CType::LoseCompleteStamp:    return lib::Size{327.0F, 92.0F};
    case CType::LoseGossip:           return lib::Size{333.0F, 110.0F};
    case CType::ShowParty:            return lib::Size{312.0F, 92.0F};
    case CType::HideParty:            return lib::Size{316.0F, 87.0F};
    case CType::Redisplay:            return lib::Size{319.0F, 114.0F};
    case CType::SubstituteStep:       return lib::Size{314.0F, 302.0F};
    case CType::SubstituteFlag:       return lib::Size{443.0F, 330.0F};
    case CType::BranchStepCmp:        return lib::Size{443.0F, 329.0F};
    case CType::BranchFlagCmp:        return lib::Size{440.0F, 330.0F};
    case CType::BranchRandomSelect:   return lib::Size{394.0F, 445.0F};
    case CType::BranchKeyCode:        return lib::Size{338.0F, 324.0F};
    case CType::CheckStep:            return lib::Size{319.0F, 360.0F};
    case CType::BranchRound:          return lib::Size{316.0F, 92.0F};
    case CType::MoveBgImage:          return lib::Size{393.0F, 301.0F};
    case CType::ReplaceBgImage:       return lib::Size{969.0F, 571.0F};
    case CType::LoseBgImage:          return lib::Size{393.0F, 163.0F};
    case CType::BranchMultiCoupon:    return lib::Size{318.0F, 174.0F};
    case CType::MoveCard:             return lib::Size{313.0F, 296.0F};
    case CType::ChangeEnvironment:    return lib::Size{319.0F, 145.0F};
    case CType::BranchVariant:        return lib::Size{478.0F, 247.0F};
    case CType::SetVariant:           return lib::Size{571.0F, 537.0F};
    case CType::CheckVariant:         return lib::Size{467.0F, 248.0F};
    case CType::CustomXmlContent:     return lib::Size{565.0F, 352.0F};
    default:                          return std::nullopt;
    }
}

// ALLではコンテントダイアログの計測済み
// 最小幅にextraを加える。最小サイズ
// で開くため、初期幅も同じ量だけ広がる。
void widenAllMinimumWidth(lib::Dialog& dialog, const float extra) {
    if (!lib::editor_window_host_options().all_mode) return;
    const auto measured = lib::window_content_minimum_size(dialog);
    const float scale = std::max(1.0F, lib::editor_ui_text_scale());
    auto minimum = dialog.minimum_size.value_or(lib::Size{});
    minimum.width = std::max(minimum.width, measured.width / scale) + extra;
    dialog.minimum_size = minimum;
}

// 初期幅が最小幅と等しいコンテントダイアログ（EventDialog::installTypedBody
// 参照）。
[[nodiscard]] bool opensAtMinimumWidth(const types::CType type) noexcept {
    using types::CType;
    switch (type) {
    case CType::StartBattle:
    case CType::End:
    case CType::ShowParty:
    case CType::HideParty:
    case CType::ChangeArea:
    case CType::EffectBreak:
    case CType::LinkStart:
    case CType::LinkPackage:
    case CType::MoveCard:
    // カード所持・取得・喪失のダイアログ。ALLでは左列を広げる。
    case CType::BranchSkill:
    case CType::BranchItem:
    case CType::BranchBeast:
    case CType::GetSkill:
    case CType::GetItem:
    case CType::GetBeast:
    case CType::LoseSkill:
    case CType::LoseItem:
    case CType::LoseBeast:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] String replaceCouponMarker(String text, const StringView marker) {
    if (const auto position = text.find("%1$s"); position != String::npos)
        text.replace(position, 4U, marker);
    return text;
}

template <typename Id>
[[nodiscard]] std::vector<String> usedNames(const UseCounterPtr& counter) {
    std::vector<String> values;
    if (!counter) return values;
    for (const auto& key : counter->template keys<Id>()) {
        const auto name = static_cast<String>(key);
        if (!name.empty() && std::find(values.begin(), values.end(), name) == values.end())
            values.push_back(name);
    }
    std::stable_sort(values.begin(), values.end());
    return values;
}

[[nodiscard]] std::shared_ptr<lib::ComboBox> makeCellNameCombo(
    const UseCounterPtr& counter, String initial,
    std::function<void(String)> changed) {
    auto combo = std::make_shared<lib::ComboBox>();
    combo->editable = true;
    combo->edit_text = std::move(initial);
    for (const auto& name : usedNames<usecounter::CellNameId>(counter))
        combo->items.push_back({name, name});
    combo->on_text_changed = [changed](const StringView value) {
        if (changed) changed(String(value));
    };
    combo->on_selection_changed = [combo, changed](
                                      const std::optional<std::size_t> index) {
        if (!index || *index >= combo->items.size() || !changed) return;
        const auto& item = combo->items[*index];
        changed(item.value.empty() ? item.text : item.value);
    };
    return combo;
}

[[nodiscard]] std::shared_ptr<lib::Panel> makeGridPanel(
    const std::size_t columns, const float spacing = 5.0F,
    const lib::Insets padding = {5.0F, 5.0F, 5.0F, 5.0F}) {
    auto panel = std::make_shared<lib::Panel>();
    auto layout = std::make_shared<lib::GridLayout>();
    layout->columns = columns;
    layout->spacing = spacing;
    layout->padding = padding;
    panel->set_layout(layout);
    return panel;
}

[[nodiscard]] std::shared_ptr<lib::GroupBox> makeGroupBox(
    String title, const std::size_t columns, const float spacing = 4.0F) {
    auto group = std::make_shared<lib::GroupBox>();
    group->title = std::move(title);
    auto layout = std::make_shared<lib::GridLayout>();
    layout->columns = columns;
    layout->spacing = spacing;
    // SWTのGroupのクライアント領域は、Win32のグループ
    // ボックスより詰まっている。モデルの余白を小さく保ち、ネイティ
    // ブ表示でもDの参照サイズで先頭・末尾行が潰れないようにする。
    layout->padding = {5.0F, 10.0F, 5.0F, 4.0F};
    group->set_layout(layout);
    return group;
}

[[nodiscard]] std::shared_ptr<lib::Label> addLabel(
    const std::shared_ptr<lib::Container>& parent, String text,
    const std::size_t row, const std::size_t column,
    const std::size_t span = 1U, const lib::Size preferred = {}) {
    auto label = std::make_shared<lib::Label>(std::move(text));
    label->set_layout_item({.row = row, .column = column,
                            .column_span = span,
                            .preferred = preferred});
    parent->add(label);
    return label;
}

void addVariableHeading(const std::shared_ptr<lib::Container>& parent,
                        String text, lib::ResourceId image) {
    auto heading = makeGridPanel(2U, 3.0F, {});
    heading->set_layout_item({.row=0U, .column=0U, .horizontal=lib::Align::Stretch});
    auto icon = std::make_shared<lib::ImageView>();
    icon->image = image;
    icon->border = false;
    icon->set_layout_item({.row=0U, .column=0U, .horizontal=lib::Align::Center,
        .vertical=lib::Align::Center, .minimum={16.0F,16.0F}, .preferred={16.0F,16.0F}});
    heading->add(icon);
    auto label = addLabel(heading, std::move(text), 0U, 1U);
    label->set_layout_item({.row=0U, .column=1U, .horizontal=lib::Align::Stretch,
        .vertical=lib::Align::Center, .grow=1.0F});
    parent->add(heading);
}

void setEnabledTree(const lib::WidgetPtr& widget, const bool enabled) {
    if (!widget) return;
    widget->set_enabled(enabled);
    if (const auto container = std::dynamic_pointer_cast<lib::Container>(widget)) {
        for (const auto& child : container->children())
            setEnabledTree(child, enabled);
    }
}

[[nodiscard]] String joinLines(const lib::Array<String>& values) {
    String result;
    for (const auto& value : values) {
        if (!result.empty()) result.push_back('\n');
        result += value;
    }
    return result;
}

[[nodiscard]] lib::Array<String> splitLines(StringView value) {
    lib::Array<String> result;
    std::size_t begin = 0U;
    while (begin <= value.size()) {
        const auto end = value.find('\n', begin);
        auto line = String(value.substr(begin, end == StringView::npos
                                                   ? value.size() - begin
                                                   : end - begin));
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) result.push_back(std::move(line));
        if (end == StringView::npos) break;
        begin = end + 1U;
    }
    return result;
}

[[nodiscard]] std::shared_ptr<lib::ComboBox> makeCoordinateCombo(
    const types::CoordinateType selected,
    std::function<void(types::CoordinateType)> changed) {
    auto combo = std::make_shared<lib::ComboBox>();
    combo->editable = false;
    combo->items = {{"変更無し", "0"}, {"直接指定", "1"},
                    {"現在値基準", "2"}, {"パーセンテージ", "3"}};
    combo->selected_index = std::min<std::size_t>(
        static_cast<std::size_t>(selected), combo->items.size() - 1U);
    combo->on_selection_changed = [changed = std::move(changed)](
                                      const std::optional<std::size_t> index) {
        if (!index || *index > 3U || !changed) return;
        changed(static_cast<types::CoordinateType>(*index));
    };
    return combo;
}

void populateStackedTransitionControls(
    const std::shared_ptr<lib::Container>& body, TransitionPanel& transition,
    const Context& context, lib::Action changed) {
    const auto* messages = context.prop ? &context.prop->requiredMsgs() : nullptr;
    addLabel(body, messages ? messages->transition.value : "背景切替方式",
             0U, 0U);
    auto kind = std::make_shared<lib::ComboBox>();
    kind->editable = false;
    for (const auto value : types::ALL_TRANSITION) {
        kind->items.push_back({messages ? messages->transitionName(value)
                                        : std::to_string(static_cast<int>(value)),
                               std::to_string(static_cast<int>(value))});
    }
    kind->selected_index = std::min<std::size_t>(
        static_cast<std::size_t>(transition.transition()),
        kind->items.size() - 1U);
    kind->set_layout_item({.row = 0U, .column = 1U, .column_span = 2U,
                           .preferred = {190.0F, 24.0F}});
    body->add(kind);

    addLabel(body, messages ? messages->transitionSpeed.value
                             : "背景切替ウェイト", 1U, 0U);
    auto speed = std::make_shared<lib::NumberEdit>();
    speed->minimum = 0.0;
    speed->maximum = 10.0;
    speed->value = transition.transitionSpeed();
    speed->set_enabled(transition.transition() != types::Transition::Default &&
                       transition.transition() != types::Transition::None);
    speed->set_layout_item({.row = 1U, .column = 1U,
                            .preferred = {70.0F, 24.0F}});
    speed->on_changed = [&transition, changed](const double value) {
        transition.setTransitionSpeed(static_cast<std::int32_t>(value));
        transition.refreshTS();
        if (changed) changed();
    };
    body->add(speed);
    addLabel(body, "(0～10)", 1U, 2U);
    transition.bindControls(kind, speed);

    kind->on_selection_changed = [&transition, speed, changed](
                                     const std::optional<std::size_t> index) {
        if (!index || *index >= types::ALL_TRANSITION.size()) return;
        transition.setTransition(types::ALL_TRANSITION[*index]);
        transition.refreshTS();
        speed->set_enabled(transition.transition() != types::Transition::Default &&
                           transition.transition() != types::Transition::None);
        if (changed) changed();
    };
}

[[nodiscard]] std::shared_ptr<lib::GroupBox> makeTransitionControls(
    TransitionPanel& transition, const Context& context, lib::Action changed) {
    const auto* messages = context.prop ? &context.prop->requiredMsgs() : nullptr;
    auto group = makeGroupBox(
        messages ? messages->transitionType.value : "背景切替方式", 3U);
    populateStackedTransitionControls(group, transition, context,
                                      std::move(changed));
    return group;
}

[[nodiscard]] std::shared_ptr<lib::Panel> makeStackedTransitionControls(
    TransitionPanel& transition, const Context& context, lib::Action changed) {
    auto panel = makeGridPanel(3U, 4.0F, {});
    populateStackedTransitionControls(panel, transition, context,
                                      std::move(changed));
    return panel;
}

[[nodiscard]] std::shared_ptr<lib::Panel> makeCompactTransitionControls(
    TransitionPanel& transition, const Context& context, lib::Action changed) {
    auto row = makeGridPanel(5U, 4.0F, {});
    const auto* messages = context.prop ? &context.prop->requiredMsgs() : nullptr;
    addLabel(row, messages ? messages->transition.value : "背景切替方式", 0U, 0U);
    auto kind = std::make_shared<lib::ComboBox>();
    kind->editable = false;
    for (const auto value : types::ALL_TRANSITION) {
        kind->items.push_back({messages ? messages->transitionName(value)
                                        : std::to_string(static_cast<int>(value)),
                               std::to_string(static_cast<int>(value))});
    }
    kind->selected_index = std::min<std::size_t>(
        static_cast<std::size_t>(transition.transition()), kind->items.size() - 1U);
    kind->set_layout_item({.row = 0U, .column = 1U,
                           .preferred = {150.0F, 22.0F}, .grow = 1.0F});
    row->add(kind);
    addLabel(row, messages ? messages->transitionSpeed.value
                            : "背景切替ウェイト", 0U, 2U);
    auto speed = std::make_shared<lib::NumberEdit>();
    speed->minimum = 0.0;
    speed->maximum = 10.0;
    speed->value = transition.transitionSpeed();
    const auto refresh_speed = [&transition, speed] {
        speed->set_enabled(transition.transition() != types::Transition::Default &&
                           transition.transition() != types::Transition::None);
    };
    speed->set_layout_item({.row = 0U, .column = 3U,
                            .preferred = {48.0F, 22.0F}});
    speed->on_changed = [&transition, changed](const double value) {
        transition.setTransitionSpeed(static_cast<std::int32_t>(value));
        transition.refreshTS();
        if (changed) changed();
    };
    row->add(speed);
    addLabel(row, "(0～10)", 0U, 4U);
    transition.bindControls(kind, speed);
    kind->on_selection_changed = [&transition, refresh_speed, changed](
                                      const std::optional<std::size_t> index) {
        if (!index || *index >= types::ALL_TRANSITION.size()) return;
        transition.setTransition(types::ALL_TRANSITION[*index]);
        transition.refreshTS();
        refresh_speed();
        if (changed) changed();
    };
    refresh_speed();
    return row;
}

[[nodiscard]] chooser::Commons chooserCommons(const Context& context) {
    chooser::Commons result;
    result.services = context.services;
    result.refreshToolBar = context.refreshToolBar;
    if (context.editor_props) {
        result.menuText = [prop = context.editor_props](auto id) { return prop->buildMenu(id); };
        result.menuImage = [prop = context.editor_props](auto id) { return prop->images().menu(id); };
    }
    return result;
}

[[nodiscard]] chooser::ChooserProperties chooserProperties(
    const Context& context,
    std::function<void(StringView, bool)> open_path = {}) {
    chooser::ChooserProperties result;
    result.editor_props = context.editor_props;
    if (context.prop) result.convertCoupon = [prop = context.prop](StringView value, chooser::CouponType type, bool ignoreSystem) {
        return prop->sys().convCoupon(value, type, ignoreSystem);
    };
    if (context.vars) {
        result.logicalSort = context.vars->logicalSort.value;
        result.showAreaDirTree = context.vars->showAreaDirTree.value;
        result.selectVariableWithTree = context.vars->selectVariableWithTree.value;
        result.expandChooserItems = context.vars->expandChooserItems.value;
        result.flagsWidth = context.vars->flagsWidth.value;
        result.flagsHeight = context.vars->flagsHeight.value;
        result.idColumn = context.vars->idColumn.value;
        result.nameTableWidth = context.vars->nameTableWidth.value;
        result.nameTableHeight = context.vars->nameTableHeight.value;
        result.comboVisibleItemCount = static_cast<std::size_t>(
            std::max<std::int32_t>(1, context.vars->comboVisibleItemCount.value));
        result.showCurrentValueOnTopAlways = context.vars->showCurrentValueOnTopAlways.value;
        result.usedCouponToCombo = context.vars->usedCouponToCombo.value;
        result.useNamesAfterStandard = context.vars->useNamesAfterStandard.value;
        result.showSpNature = context.vars->showSpNature.value;
    }
    if (context.editor_props) {
        auto* const props = context.editor_props;
        const auto image = [props](StringView name) { return props->images().load(name); };
        result.images.flag = image("flag.png");
        result.images.localFlag = image("flag_l.png");
        result.images.step = image("step.png");
        result.images.localStep = image("step_l.png");
        result.images.variant = image("variant.png");
        result.images.localVariant = image("variant_l.png");
        result.images.flagDir = image("flagdir.png");
        result.images.area = image("area.png");
        result.images.battle = image("battle.png");
        result.images.packages = image("package.png");
        result.images.casts = image("cast.png");
        result.images.info = image("info.png");
        result.images.areaDir = image("areadir.png");
        result.images.emptyIcon = image("empty.png");
        result.images.randomValue = image("random_value.png");
        result.images.selectedPlayerCardNumber = image("sc_sel_pc_num.png");
        result.images.startArea = image("start_area.png");
        result.storeIdColumn = [props](const std::int32_t width) {
            props->var().idColumn.value = width;
            (void)props->apply();
        };
    }
    if (context.prop) {
        result.messages.allExpanded = context.prop->requiredMsgs().allExpanded.value;
        const auto& msgs = context.prop->requiredMsgs();
        result.messages.noFlagRef = msgs.defaultSelection(msgs.noFlagRef.value);
        result.messages.randomValue = msgs.defaultSelection(msgs.randomValue.value);
        result.messages.selectedPlayerValue = msgs.defaultSelection(msgs.selectedPlayerValue.value);
        result.messages.flagDirRoot = msgs.flagDirRoot.value;
        result.messages.localVariablesOwner = msgs.localVariablesOwner.value;
        result.messages.incrementalSearch = msgs.menuText(types::MenuID::IncSearch);
    }
    result.editor_commons = context.editor_commons;
    if (!open_path && context.editor_commons) {
        open_path = [common = context.editor_commons](const StringView value,
                                                       const bool activate) {
            (void)common->openCWXPath(String(value), activate);
        };
    }
    result.openCWXPath = std::move(open_path);
    return result;
}

[[nodiscard]] abilityview::Commons abilityCommons(const Context& context) {
    auto result = context.prop ? abilityview::Commons(context.prop->requiredMsgs())
                               : abilityview::Commons{};
    if (context.vars) result.radioGroupSeparatorWidth = context.vars->radioGroupSeparatorWidth.value;
    return result;
}

void bindHoldingCouponKind(const Context& context,
                           const std::shared_ptr<lib::ComboBox>& coupon,
                           const std::shared_ptr<lib::ComboBox>& kind) {
    static constexpr std::array kinds{features::CouponType::Normal, features::CouponType::Hide,
        features::CouponType::Dur, features::CouponType::DurBattle, features::CouponType::System};
    kind->editable = false;
    kind->items.clear();
    for (const auto value : kinds)
        kind->items.push_back({context.prop ? context.prop->requiredMsgs().couponTypeShortDesc(value)
                                           : std::to_string(static_cast<int>(value)),
                              std::to_string(static_cast<int>(value))});
    const std::weak_ptr<lib::ComboBox> weak_coupon = coupon, weak_kind = kind;
    const auto update = [prop = context.prop, weak_coupon, weak_kind] {
        const auto text = weak_coupon.lock(), type = weak_kind.lock();
        if (!text || !type || !prop) return;
        const auto found = std::find(kinds.begin(), kinds.end(), prop->sys().couponType(text->edit_text));
        type->selected_index = found == kinds.end() ? 0U : static_cast<std::size_t>(found - kinds.begin());
    };
    const auto old_text = coupon->on_text_changed;
    coupon->on_text_changed = [old_text, update](StringView text) {
        if (old_text) old_text(text);
        update();
    };
    const auto old_selection = coupon->on_selection_changed;
    coupon->on_selection_changed = [old_selection, update](std::optional<std::size_t> index) {
        if (old_selection) old_selection(index);
        update();
    };
    kind->on_selection_changed = [prop = context.prop, weak_coupon](std::optional<std::size_t> index) {
        const auto text = weak_coupon.lock();
        if (!prop || !text || !index || *index >= kinds.size()) return;
        (void)lib::set_combo_edit_text(*text, prop->sys().convCoupon(text->edit_text, kinds[*index], false));
    };
    update();
}

[[nodiscard]] spcarddialog::areaviewutils::Commons animationCommons(
    const Context& context, const Summary* summary, const Skin* skin) {
    spcarddialog::areaviewutils::Commons result;
    if (context.editor_props)
        result = spcarddialog::areaviewutils::Commons(*context.editor_props);
    result.services = context.services;
    result.summary = summary;
    result.skin = skin;
    result.dialogOwner = context.dialog_owner;
    return result;
}

[[nodiscard]] motionview::Props motionProperties(const Context& context) {
    motionview::Props result;
    result.services = context.services;
    if (context.prop) {
        const auto& messages = context.prop->requiredMsgs();
        result.sys = &context.prop->sys();
        result.msgs.motionKind = messages.motionKind.value;
        result.msgs.motionElement = messages.motionElement.value;
        result.msgs.motionBeast = messages.motionBeast.value;
        result.msgs.motionRound = messages.motionRound.value;
        result.msgs.motionEnhValue = messages.motionEnhValue.value;
        result.msgs.motionDamageType = messages.motionDamageType.value;
        result.msgs.motionValue = messages.motionValue.value;
        result.msgs.beastNone = messages.beastNone.value;
        result.msgs.setBeast = messages.setBeast.value;
        result.msgs.groupVitality = "生命力";
        result.msgs.groupPhysical = "肉体";
        result.msgs.groupSkill = "技能";
        result.msgs.groupMental = "精神";
        result.msgs.groupMagic = "魔法";
        result.msgs.groupEnhance = "能力";
        result.msgs.groupVanish = "消滅";
        result.msgs.groupCard = "カード";
        result.msgs.groupBeast = "召喚";
        result.msgs.groupEtc = "その他";
        result.msgs.undo = "元に戻す";
        result.msgs.redo = "やり直す";
        result.msgs.deleteMotion = "削除";
        result.msgs.up = "上へ";
        result.msgs.down = "下へ";
        result.msgs.newMotion = "効果の追加";
        result.msgs.noSelection = "効果が選択されていません";
        for (const auto& [type, detail] : motion::MOTION_DETAILS()) {
            (void)detail;
            result.msgs.names[type] = messages.motionName(type);
            result.msgs.descriptions[type] = messages.motionDesc(type);
        }
    }
    if (context.vars) {
        result.etc.undoMaxEtc = static_cast<std::size_t>(
            std::max(0, context.vars->undoMaxEtc.value));
        result.etc.motionsWidth = static_cast<std::size_t>(
            std::max(1, context.vars->motionsWidth.value));
        result.etc.comboVisibleItemCount = static_cast<std::size_t>(
            std::max(1, context.vars->comboVisibleItemCount.value));
        result.etc.roundMax = std::max<std::uint32_t>(
            1U, context.vars->roundMax.value);
        result.etc.uValueMax = std::max<std::uint32_t>(
            1U, context.vars->uValueMax.value);
        result.etc.skillPowerMax = std::max<std::uint32_t>(
            1U, context.vars->skillPowerMax.value);
        result.etc.beastMaxNest = std::max<std::uint32_t>(
            1U, context.vars->beastMaxNest.value);
        result.etc.imageScale = static_cast<std::int32_t>(
            ::cwxeditor::cwx::variables::previewScalePercent(
                context.vars->imageScalePercent.value, context.vars->imageScale.value));
        result.etc.showMotionDescription =
            context.vars->showMotionDescription.value;
        result.etc.xmlCopy = context.vars->xmlCopy.value;
    }
    if (context.editor_props) {
        auto& images = context.editor_props->images();
        result.motionIcon = [&images](const types::MType type) {
            return images.motion(type);
        };
        result.deleteIcon = images.msnDelete();
        result.upIcon = images.menu(types::MenuID::Up);
        result.downIcon = images.menu(types::MenuID::Down);
        result.looks.motionRoundDefault = static_cast<std::int32_t>(
            context.editor_props->looks().motionRoundDefault());
    }
    return result;
}

[[nodiscard]] String fallbackContentName(CType type) {
    const auto detail = event::contentDetail(type);
    if (!detail.names.empty() && !detail.names.front().empty())
        return detail.names.front();
    return "Content" + std::to_string(static_cast<std::int32_t>(type));
}

[[nodiscard]] bool containsRange(const std::vector<types::Range>& values,
                                 types::Range value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

[[nodiscard]] bool isFlagType(CType type) noexcept {
    switch (type) {
    case CType::BranchFlag:
    case CType::SetFlag:
    case CType::ReverseFlag:
    case CType::CheckFlag:
    case CType::SubstituteFlag:
    case CType::BranchFlagCmp:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool isCardBranch(CType type) noexcept {
    return type == CType::BranchSkill || type == CType::BranchItem ||
           type == CType::BranchBeast;
}

template <typename Dialog, typename... Args>
[[nodiscard]] EventDialogPtr makeDialog(const bool create, Args&&... args) {
    auto dialog = std::make_shared<Dialog>(std::forward<Args>(args)...);
    dialog->setCreateMode(create);
    return dialog;
}

void applyCreateDefaults(const ContentPtr& content) {
    if (!content) return;
    // DはEventオブジェクトなしで新規ダイアログを
    // 作り、下記の操作部の既定値を保つ。共通EventView
    // はモードレス表示中に一時Contentを所有
    // するため、ダイアログが複製する前にその値を設
    // 定する。構築後では各型付きコンストラクターが
    // 0の値を操作部へ読み込み済みで、遅すぎる。
    switch (content->type()) {
    case CType::SetFlag:
        content->flagValue(true);
        break;
    case CType::BranchSelect:
        content->initValue(1);
        break;
    case CType::Effect:
        content->successRate(successRateMax);
        break;
    default:
        break;
    }
}

[[nodiscard]] exprutils::Commons expressionCommons(
    const Context& context, dprops::Props& props, const Summary* summary,
    const Skin* skin, const std::shared_ptr<lib::Window>& owner) {
    exprutils::Commons result(props);
    result.services = context.services;
    result.summary = summary;
    result.skin = skin;
    // 「関数の挿入」のツールウィンドウは、このコンテント
    // ダイアログに所有させる。所有元なしの最上位ウィン
    // ドウにすると、コンテント側が入力を保持したままで
    // 操作できず、Editorが停止したように見える。
    result.dialogOwner = owner;
    if (context.openCWXPath) {
        result.open_cwx_path = context.openCWXPath;
    } else if (auto* common = context.editor_commons) {
        const auto life = common->lifetimeToken();
        result.open_cwx_path = [common, life](const StringView path,
                                              const bool activate) {
            return !life.expired() &&
                common->openCWXPath(String(path), activate);
        };
        result.subscribeUndoMax = [common, life](lib::Action action) {
            if (life.expired()) return lib::Action{};
            const auto token = common->refUndoMax.add(std::move(action));
            return lib::Action([common, life, token] {
                if (!life.expired()) common->refUndoMax.remove(token);
            });
        };
    }
    return result;
}

class VariantExpressionEventDialogAdapter final : public EventDialog {
public:
    VariantExpressionEventDialogAdapter(
        Context context, SummaryPtr summary, Parent parent,
        ContentPtr parentContent, ContentPtr content, CType type,
        SkinPtr skin, UseCounterPtr useCounter)
        : EventDialog(std::move(context), std::move(summary), type,
                      std::move(parent), std::move(parentContent),
                      std::move(content), std::move(skin),
                      std::move(useCounter)) {
        if (type != CType::BranchVariant && type != CType::CheckVariant)
            throw std::invalid_argument("expression dialog requires a variant event");
    }

    void setupBody() override {
        auto& props = *context_.editor_props;
        body_ = std::make_shared<lib::Panel>();
        auto layout = std::make_shared<lib::GridLayout>();
        layout->columns = 1U;
        body_->set_layout(layout);
        editor_ = std::make_unique<exprutils::ExpressionEventDialog>(
            expressionCommons(context_, props, summary_.get(), skin_.get(), widget_),
            props, widget_, summary_.get(), skin_.get(), use_counter_,
            parent_content_, type_, ensureDraft());
        editor_->setup(*body_);
        if (const auto& expression = editor_->expressionEditor(); expression) {
            expression->modEvent.push_back([this] { editorChanged(); });
            expression->modReturnValueEvent.push_back(
                [this] { editorChanged(); });
        }
        replaceWarnings(currentWarnings());
        installTypedBody(body_, {484.0F, 300.0F});
        // ALLではCheckVariantの初期幅と最小幅を100px広げる。
        if (type_ == CType::CheckVariant) widenAllMinimumWidth(*widget_, 100.0F);
    }

    bool apply() override {
        if (!editor_ || !editor_->apply()) return false;
        replaceWarnings(currentWarnings());
        return commitDraft();
    }

    bool openCWXPath(String path, const bool activate) override {
        if (editor_ && editor_->openCWXPath(path, activate)) return true;
        return EventDialog::openCWXPath(std::move(path), activate);
    }

private:
    void refExternalFlagAndStep() override {
        if (editor_ && editor_->expressionEditor()) editor_->expressionEditor()->refFlagAndStep({}, {}, {});
        refreshWarning();
    }
    void refDataVersion() override {
        if (editor_ && editor_->expressionEditor()) editor_->expressionEditor()->refDataVersion();
        EventDialog::refDataVersion();
    }
    void refreshWarning() override { replaceWarnings(currentWarnings()); }
    void editorChanged() {
        setApplyEnabled(true);
        if (editor_) replaceWarnings(currentWarnings());
        refreshTypedBody();
    }

    [[nodiscard]] WarningList currentWarnings() const {
        if (!editor_ || !draft_) return {};
        const auto snapshot = draft_->dup();
        if (const auto& expression = editor_->expressionEditor(); expression)
            snapshot->expression(expression->expression());
        return modelWarnings(*snapshot);
    }

    std::shared_ptr<lib::Panel> body_{};
    std::unique_ptr<exprutils::ExpressionEventDialog> editor_{};
};

class SetVariantEventDialogAdapter final : public EventDialog {
public:
    SetVariantEventDialogAdapter(
        Context context, SummaryPtr summary, Parent parent,
        ContentPtr parentContent, ContentPtr content, SkinPtr skin,
        UseCounterPtr useCounter)
        : EventDialog(std::move(context), std::move(summary), CType::SetVariant,
                      std::move(parent), std::move(parentContent),
                      std::move(content), std::move(skin),
                      std::move(useCounter)) {}

    void setupBody() override {
        auto& props = *context_.editor_props;
        body_ = std::make_shared<lib::Panel>();
        auto layout = std::make_shared<lib::GridLayout>();
        layout->columns = 1U;
        body_->set_layout(layout);
        editor_ = std::make_unique<exprutils::SetVariantDialog>(
            expressionCommons(context_, props, summary_.get(), skin_.get(), widget_),
            props, widget_, summary_.get(), skin_.get(), use_counter_,
            parent_content_, ensureDraft());
        editor_->configureChooser(chooserCommons(context_),
            chooserProperties(context_, [this](StringView path, bool activate) {
                (void)EventDialog::openExternalCWXPath(String(path), activate);
            }));
        editor_->setup(*body_);
        // ALL仕様: 「演算結果を得る」を左下、OK などと同じ行に置く。
        lib::WidgetPtr footer_leading;
        if (lib::editor_window_host_options().all_mode) {
            if (const auto& button = editor_->calculationButton(); button) {
                body_->remove(button);
                footer_leading = button;
            }
        }
        if (const auto& expression = editor_->expressionEditor(); expression) {
            expression->modEvent.push_back([this] { editorChanged(); });
            expression->modReturnValueEvent.push_back(
                [this] { editorChanged(); });
        }
        editor_->warningChanged.push_back([this] { editorChanged(); });
        replaceWarnings(currentWarnings());
        installTypedBody(body_, {590.0F, 578.0F}, true, footer_leading);
        // ALLではSetVariantの初期幅と最小幅を50px広げる。
        widenAllMinimumWidth(*widget_, 50.0F);
    }

    bool apply() override {
        if (!editor_ || !editor_->apply()) return false;
        replaceWarnings(currentWarnings());
        return commitDraft();
    }

    bool openCWXPath(String path, const bool activate) override {
        if (editor_ && editor_->openCWXPath(path, activate)) return true;
        return EventDialog::openCWXPath(std::move(path), activate);
    }

private:
    void refExternalFlagAndStep() override {
        if (!editor_) return;
        const auto root = summary_ ? summary_->flagDirRoot() : flag::FlagDirPtr{};
        const auto flags = flag::allVars<flag::Flag>(root, use_counter_);
        const auto steps = flag::allVars<flag::Step>(root, use_counter_);
        const auto variants = flag::allVars<flag::Variant>(root, use_counter_);
        if (flags.empty() && steps.empty() && variants.empty()) { close(); return; }
        editor_->refFlagsAndSteps(flags, steps, variants);
        if (editor_->expressionEditor()) editor_->expressionEditor()->refFlagAndStep(flags, steps, variants);
        refreshWarning();
    }
    void refDataVersion() override {
        if (editor_ && editor_->expressionEditor()) editor_->expressionEditor()->refDataVersion();
        EventDialog::refDataVersion();
    }
    void refreshWarning() override { replaceWarnings(currentWarnings()); }
    void editorChanged() {
        setApplyEnabled(true);
        if (editor_) replaceWarnings(currentWarnings());
        refreshTypedBody();
    }

    [[nodiscard]] WarningList currentWarnings() const {
        if (!editor_ || !draft_) return {};
        const auto snapshot = draft_->dup();
        if (const auto& expression = editor_->expressionEditor(); expression)
            snapshot->expression(expression->expression());
        snapshot->flag({});
        snapshot->step({});
        snapshot->variant({});
        switch (editor_->selectedType()) {
        case exprutils::VariableType::Flag:
            snapshot->flag(editor_->selectedName());
            break;
        case exprutils::VariableType::Step:
            snapshot->step(editor_->selectedName());
            break;
        case exprutils::VariableType::Variant:
            snapshot->variant(editor_->selectedName());
            break;
        }
        return modelWarnings(*snapshot);
    }

    std::shared_ptr<lib::Panel> body_{};
    std::unique_ptr<exprutils::SetVariantDialog> editor_{};
};

class CustomXmlEventDialogAdapter final : public EventDialog {
public:
    CustomXmlEventDialogAdapter(
        Context context, SummaryPtr summary, Parent parent,
        ContentPtr parentContent, ContentPtr content, SkinPtr skin,
        UseCounterPtr useCounter, xmlcontentdialog::XmlContentDialog* borrowed = nullptr)
        : EventDialog(std::move(context), std::move(summary),
                      CType::CustomXmlContent, std::move(parent),
                      std::move(parentContent), std::move(content),
                      std::move(skin), std::move(useCounter)), editor_(borrowed), borrowed_(borrowed != nullptr) {}

    ~CustomXmlEventDialogAdapter() override {
        if (borrowed_ && editor_ && modified_bound_) editor_->modified = std::move(previous_modified_);
    }

    void setupBody() override {
        if (!editor_) {
            owned_editor_ = std::make_unique<xmlcontentdialog::XmlContentDialog>(
                *context_.editor_commons, *context_.editor_props, widget_, summary_,
                skin_, use_counter_, parent_content_, ensureDraft());
            editor_ = owned_editor_.get();
        }
        editor_->bindEventShell(widget_);
        previous_modified_ = editor_->modified;
        modified_bound_ = true;
        editor_->modified = [this] {
            setApplyEnabled(true);
            if (previous_modified_) previous_modified_();
        };
        installTypedBody(editor_->createControl({}, false), {590.0F, 396.0F});
    }

    bool apply() override {
        if (!editor_ || !editor_->apply()) return false;
        return borrowed_ || commitDraft();
    }

private:
    std::unique_ptr<xmlcontentdialog::XmlContentDialog> owned_editor_{};
    xmlcontentdialog::XmlContentDialog* editor_{};
    bool borrowed_{};
    bool modified_bound_{};
    lib::Action previous_modified_{};
};

}  // namespace

EventDialogPtr createXmlContentHost(const Context& context,
                                   xmlcontentdialog::XmlContentDialog& editor) {
    return std::make_shared<CustomXmlEventDialogAdapter>(context, editor.summary(),
        editor.shell(), editor.parentContent(), editor.event(), editor.forceSkin(),
        editor.useCounter(), &editor);
}

EventDialog::EventDialog(Context context, SummaryPtr summary, CType type,
                         Parent parent, ContentPtr parentContent,
                         ContentPtr content, SkinPtr forceSkin,
                         UseCounterPtr useCounter, const bool create,
                         const bool enterClose)
    : context_(std::move(context)), summary_(std::move(summary)), type_(type),
      parent_(std::move(parent)), parent_content_(std::move(parentContent)),
      event_(std::move(content)), skin_(forceSkin), original_forced_skin_(std::move(forceSkin)),
      use_counter_(std::move(useCounter)), create_(create),
      enter_close_(enterClose), widget_(std::make_shared<lib::Dialog>()) {
    if (event_ && event_->type() != type_)
        throw std::invalid_argument("event type does not match dialog type");
    draft_ = event_ ? event_->dup() : ContentPtr{};
    if (!skin_) skin_ = context_.findSkin(summary_.get());
    title_ = formatTitle(contentName());
    if (context_.contentImage) icon_ = context_.contentImage(type_);
    widget_->title = title_;
    widget_->icon = icon_;
    // ALLでは全コンテントダイアログを最小サイズで開く。
    widget_->initial_minimum_size = lib::editor_window_host_options().all_mode;
    auto owner = std::dynamic_pointer_cast<lib::Window>(parent_);
    if (!owner) owner = context_.dialog_owner;
    lib::configure_owned_window(widget_, owner,
        lib::OwnedWindowKind::Modeless);
    widget_->on_close_requested = [this] { close(); };
    widget_->set_bounds({0.0F, 0.0F, 720.0F, 560.0F});
    apply_enabled_ = static_cast<bool>(parent_content_) &&
                     (!event_ || !event_->parent());
    // DはここでShellを作り、操作部と初期検証はopen/setup
    // で行う。この保持型の共通Shellは初期非表示とする。
    widget_->set_visible(false);
}

EventDialog::~EventDialog() {
    // ダイアログの状態を解放する前に、待機中の特殊文字コールバックを無効にする。
    special_text_lifetime_.reset();
    special_text_updates_.clear();
    unbindExternalNotifications();
    if (widget_) widget_->on_close_requested = {};
    if (ok_button_) ok_button_->on_click = {};
    if (apply_button_) apply_button_->on_click = {};
    if (cancel_button_) cancel_button_->on_click = {};
    childNamesApplied = {};
    appliedEvent.clear();
    closeEvent.clear();
}

void EventDialog::buildCommonControls() {
    auto layout = std::make_shared<lib::GridLayout>();
    layout->columns = 3;
    layout->spacing = 6.0F;
    layout->padding = {8.0F, 8.0F, 8.0F, 8.0F};
    widget_->set_layout(layout);

    warning_label_ = std::make_shared<lib::Label>();
    warning_label_->text = warnings_.empty() ? String{} : warnings_.front();
    warning_label_->set_visible(!warning_label_->text.empty());
    warning_label_->set_layout_item({.row = 0, .column = 0, .column_span = 3,
                                     .grow = 0.0F});
    widget_->add(warning_label_);

    property_table_ = std::make_shared<lib::TableView>();
    property_table_->columns = {{"項目", 260.0F}, {"値", 380.0F}};
    property_table_->full_row_selection = true;
    property_table_->on_cell_edited =
        [this](const std::size_t row, const std::size_t column, String value) {
            if (column == 1U) updateProperty(row, std::move(value));
        };
    property_table_->set_layout_item({.row = 1, .column = 0,
                                      .column_span = 3, .grow = 0.55F});
    widget_->add(property_table_);

    xml_editor_ = std::make_shared<lib::TextEdit>();
    xml_editor_->multiline = true;
    xml_editor_->text = draft_ ? draft_->toXML() : String{};
    xml_editor_->on_blur = [this] { rebuildPropertyTable(); };
    xml_editor_->set_layout_item({.row = 2, .column = 0, .column_span = 3,
                                  .grow = 0.45F});
    widget_->add(xml_editor_);

    ok_button_ = std::make_shared<lib::Button>();
    ok_button_->text = "OK";
    ok_button_->default_button = true;
    ok_button_->on_click = [this] {
        if (applyXmlDraft()) finishClose(lib::DialogResult::OK);
    };
    ok_button_->set_layout_item({.row = 3, .column = 0});
    widget_->add(ok_button_);

    apply_button_ = std::make_shared<lib::Button>();
    apply_button_->text =
        (context_.prop ? context_.prop->requiredMsgs().apply.value : String{"適用"}) +
        "(&A)";
    apply_button_->on_click = [this] { (void)applyXmlDraft(); };
    apply_button_->set_layout_item({.row = 3, .column = 1});
    widget_->add(apply_button_);

    cancel_button_ = std::make_shared<lib::Button>();
    cancel_button_->text = context_.prop
        ? context_.prop->requiredMsgs().dlgTextCancel.value
        : String{"キャンセル(&C)"};
    cancel_button_->cancel_button = true;
    cancel_button_->on_click = [this] { finishClose(lib::DialogResult::Cancel); };
    cancel_button_->set_layout_item({.row = 3, .column = 2});
    widget_->add(cancel_button_);
    rebuildPropertyTable();
}

void EventDialog::installTypedBody(const lib::WidgetPtr& body,
                                   const lib::Size size,
                                   const bool showApply,
                                   const lib::WidgetPtr& footerLeading) {
    const auto reference_size = dReferenceClientSize(type_);
    auto effective_size = reference_size.value_or(size);
    // 外側の操作部の余白は、本体ではなくウィンドウの高さに確保する。
    effective_size.height += 6.0F;
    if (reference_size) {
        // 参照画像は特定のWindows/SWTのフォン
        // ト寸法を使う。比率は維持しつつ、別の日本語シス
        // テムフォントでも字形の上下や数値入力の文字が切
        // れないよう数画素の余裕を設ける。内容を犠牲にせ
        // ず、参照より少し大きいウィンドウを許可する。
        effective_size.width += 4.0F;
        effective_size.height += 4.0F;
    }
    widget_->clear();
    widget_->set_bounds({0.0F, 0.0F, effective_size.width,
                         effective_size.height});
    widget_->minimum_size = effective_size;
    // ALLではこれらのダイアログを最小幅で開く。
    // 上記の参照幅と、計測した内容の最小幅の大き
    // い方を使う。推奨幅へ広げると、長いエリア・
    // 開始・カード名で初期ウィンドウがDの配置よ
    // り大幅に広がるため。高さは内容に合わせる。
    widget_->initial_preferred_width = !(lib::editor_window_host_options().all_mode &&
                                         opensAtMinimumWidth(type_));
    auto layout = std::make_shared<lib::GridLayout>();
    layout->columns = 1;
    layout->spacing = 0.0F;
    // SWTの設定領域の余白はbodyが持つ。ここで別の
    // 枠余白を足すと二重計上になり、Dのクライアントサ
    // イズへ制限した際に両方向で12pxずつ本体が縮む
    // 。パーティ表示・非表示、待機、ラウンド分岐などで
    // 数値入力やグループが切れるため、上と左右はそろえ
    // 、下の操作部に必要な余白だけを確保する。DのAbsDialog
    // のフッターも設定領域の直下に置く。
    layout->padding = {0.0F, 0.0F, 6.0F, 0.0F};
    widget_->set_layout(layout);

    // 型付きダイアログはDに合わせ、タブの本体から
    // 始める。タブ上に常時空の行を確保せず、検証メ
    // ッセージは所有するEditorが表示する。
    warning_label_.reset();

    if (body) {
        body->set_layout_item({.row = 0, .column = 0,
                               .grow = 1.0F});
        widget_->add(body);
    }

    // 本体のグリッド列に操作ボタンを入れると、それぞれがウィンド
    // ウ幅の三分の一に広がるため分離する。従来の6pxの行間は
    // 上下へ均等に配分し、本体高を維持して操作部を中央に置く。
    auto footer = std::make_shared<lib::Panel>();
    auto footer_layout = std::make_shared<lib::LinearLayout>(lib::Orientation::Horizontal);
    footer_layout->spacing = 6.0F;
    footer_layout->cross_axis = lib::Align::Center;
    footer_layout->padding = {3.0F, 0.0F, 3.0F, 0.0F};
    footer->set_layout(footer_layout);
    footer->set_layout_item({.row = 1, .column = 0,
                             .horizontal = lib::Align::Stretch,
                             .vertical = lib::Align::Center});
    const auto add_spacer = [&footer] {
        auto spacer = std::make_shared<lib::Spacer>();
        spacer->set_layout_item({.horizontal = lib::Align::Stretch, .grow = 1.0F});
        footer->add(spacer);
    };
    if (footerLeading) {
        auto item = footerLeading->layout_item();
        item.horizontal = lib::Align::Start;
        item.vertical = lib::Align::Center;
        item.grow = 0.0F;
        footerLeading->set_layout_item(item);
        footer->add(footerLeading);
    }
    add_spacer();

    ok_button_ = std::make_shared<lib::Button>("OK");
    ok_button_->default_button = true;
    ok_button_->on_click = [this] {
        if (apply()) finishClose(lib::DialogResult::OK);
    };
    ok_button_->set_layout_item({.vertical = lib::Align::Center,
                                 .preferred = {96.0F, 26.0F}});
    footer->add(ok_button_);

    cancel_button_ = std::make_shared<lib::Button>(
        context_.prop ? context_.prop->requiredMsgs().dlgTextCancel.value
                      : String{"キャンセル(&C)"});
    cancel_button_->cancel_button = true;
    cancel_button_->on_click = [this] {
        finishClose(lib::DialogResult::Cancel);
    };
    cancel_button_->set_layout_item({.vertical = lib::Align::Center,
                                     .preferred = {96.0F, 26.0F}});
    footer->add(cancel_button_);

    apply_button_ = std::make_shared<lib::Button>(
        (context_.prop ? context_.prop->requiredMsgs().apply.value : String{"適用"}) +
        "(&A)");
    apply_button_->set_enabled(showApply && apply_enabled_);
    apply_button_->on_click = [this] { (void)apply(); };
    apply_button_->set_layout_item({.vertical = lib::Align::Center,
                                     .preferred = {96.0F, 26.0F}});
    footer->add(apply_button_);
    // Dの背景コンテントのダイアログは操作群を右へ寄せる。
    // AbsDialogは計算などの追加操作を左、標準操作
    // を右に置く。追加操作なしではC++の中央配置を維持す
    // るが、追加コントロールをまたいで群を中央配置しない。
    if (!footerLeading && type_ != CType::ChangeBgImage && type_ != CType::ReplaceBgImage &&
        type_ != CType::MoveBgImage && type_ != CType::LoseBgImage)
        add_spacer();
    widget_->add(footer);
    xml_editor_.reset();
    property_table_.reset();
    updateWarningPresentation();
}

void EventDialog::installMessageBody(
    messageutils::AbstractMessageDialog& editor,
    const lib::WidgetPtr& editor_body, const lib::Size fallback_size) {
    const auto& geometry = editor.geometry();
    const lib::Size saved_size{
        geometry.width > 0 ? static_cast<float>(geometry.width)
                           : fallback_size.width,
        geometry.height > 0 ? static_cast<float>(geometry.height)
                            : fallback_size.height};

    // installTypedBodyはDの参照クライアントサ
    // イズを使う。子の推奨値も以前の拡張済みウィンドウではなく
    // 、同じ基準を使う。自動計測で旧サイズへ戻るのを防ぐため。
    auto dialog_size = dReferenceClientSize(type_).value_or(saved_size);
    const auto preview = editor.previewWidget();
    // 固定のプレビュー幅（メッセージウィンドウと左右各1px）
    // はダイアログとともに広げない。余った幅は編集列へ渡す。
    const float preview_fixed = editor.previewFixedWidth();
    const auto preview_width = preview
        ? (preview_fixed > 0.0F ? preview_fixed
                                : std::clamp(editor.previewWidth(), 280.0F,
                                             dialog_size.width * 0.6F))
        : 0.0F;
    auto editor_width = std::max(
        360.0F, dialog_size.width - preview_width - (preview ? 5.0F : 0.0F));
    const auto actual_editor = editor_body ? editor_body : editor.editorWidget();
    // ALLでは編集列を操作部に必要な幅だけにし、ダイア
    // ログを編集部とプレビューの合計で決める。参照幅の
    // 余りをすべて編集列へ渡すと文字欄や変数コンボが伸
    // び、大きなフォントと合わせて横スクロールが出る。
    if (lib::editor_window_host_options().all_mode && actual_editor) {
        const lib::DialogFontLayoutScope font_scope(true);
        const auto needed_size = lib::preferred_ui_size(actual_editor);
        // 編集列の幅には推奨幅ではなく最小幅を
        // 使う。固定幅の参照・選択行、メッセー
        // ジ欄の列数、画像選択部に合わせる。内
        // 側パネルの推奨ヒントで、どの行にも不
        // 要な幅や選択行の右の空白を作らない。
        const auto minimum_size = lib::minimum_ui_size(actual_editor);
        const float needed = std::min(needed_size.width, minimum_size.width) /
                             lib::editor_ui_text_scale();
        editor_width = std::max(360.0F, std::ceil(needed));
        dialog_size.width = editor_width + preview_width + (preview ? 5.0F : 0.0F);
        // 台詞では編集部の下に空白を残さず、編集部の
        // 高さと操作行でダイアログ高を決める。44
        // は下記size_editorと同じフッタ
        // ー余裕。内側パネルの高い推奨値ではなく最
        // 小高を使い、オプション行と操作行の間の空
        // 帯を避ける。ただしプレビュー列も収める。
        if (type_ == CType::TalkDialog) {
            float content_height = std::min(needed_size.height, minimum_size.height);
            if (preview)
                content_height = std::max(content_height,
                                          lib::minimum_ui_size(preview).height);
            dialog_size.height = std::ceil(
                content_height / lib::editor_ui_text_scale()) + 44.0F;
        }
    }
    // 意図的な変更(D版と異なる): ALL ではアクション行(プレビュー/OK/キャンセル/
    // 適用)をエディタ列の下ではなくダイアログ全幅に置く。エディタ列の下だと
    // ボタン群の幅がエディタ列を広げ、エディタ行の右に未使用の余白が残った。
    // そのためプレビューは上段だけを占める。
    const bool all_footer = lib::editor_window_host_options().all_mode;
    const auto preview_item = [preview_fixed, dialog_size,
                               all_footer](const float width) {
        return lib::LayoutItem{0U, 1U, all_footer ? 1U : 2U, 1U,
            lib::Align::Stretch,
            lib::Align::Stretch,
            {preview_fixed > 0.0F ? width : 0.0F, 0.0F},
            {width, std::max(300.0F, dialog_size.height - 44.0F)}, {},
            preview_fixed > 0.0F ? 0.0F : 1.0F};
    };
    auto message_body = std::make_shared<lib::Panel>();
    auto message_layout = std::make_shared<lib::GridLayout>();
    message_layout->columns = preview ? 2U : 1U;
    message_layout->equal_width = false;
    message_layout->spacing = 5.0F;
    message_layout->padding = {};
    message_body->set_layout(message_layout);
    const auto size_editor = [actual_editor, dialog_size](const float width) {
        if (!actual_editor) return;
        const lib::DialogFontLayoutScope font_scope(true);
        // 操作部にさらに高さが必要でなければ、Dのコンパクトな
        // 基準を維持する。計測最小高より小さい参照高ヒントでは
        // 、フッターが見えていても最後のオプション行が切れる。
        const float minimum_height = lib::minimum_ui_size(actual_editor).height /
            lib::editor_ui_text_scale();
        actual_editor->set_layout_item(
            lib::LayoutItem{0U, 0U, 1U, 1U, lib::Align::Stretch,
                            lib::Align::Stretch, {0.0F, minimum_height},
                            {width, std::max({300.0F, minimum_height,
                                dialog_size.height - 44.0F})}, {}, 1.0F});
    };
    if (actual_editor) {
        size_editor(editor_width);
        message_body->add(actual_editor);
    }
    if (preview) {
        preview->set_layout_item(preview_item(preview_width));
        message_body->add(preview);
    }

    installTypedBody(message_body, dialog_size, true);
    if (lib::editor_window_host_options().all_mode) {
        auto bounds = widget_->bounds();
        bounds.width = dialog_size.width;
        if (type_ == CType::TalkDialog) bounds.height = dialog_size.height;
        widget_->set_bounds(bounds);
    }
    widget_->minimum_size = dialog_size;
    widget_->maximized = geometry.maximized;

    // Dは伸縮可能な左の追加領域にPreviewを
    // 置き、自然幅のボタン群をフッター右端に置く。
    widget_->clear();
    auto root_layout = std::make_shared<lib::GridLayout>();
    root_layout->columns = 1U;
    root_layout->spacing = 0.0F;
    // Dのメッセージ・台詞編集部と右のプレビューは、クライアント
    // 端に接する。操作行が余白を持つため、二重の外枠を足さない。
    root_layout->padding = {};
    widget_->set_layout(root_layout);
    if (message_body) {
        message_body->set_layout_item(
            lib::LayoutItem{0U, 0U, 1U, 1U, lib::Align::Stretch,
                            lib::Align::Stretch, {}, {}, {}, 1.0F});
        widget_->add(message_body);
    }

    auto footer = std::make_shared<lib::Panel>();
    auto footer_layout = std::make_shared<lib::GridLayout>();
    footer_layout->columns = 2U;
    footer_layout->equal_width = false;
    footer_layout->spacing = 4.0F;
    footer->set_layout(footer_layout);
    const auto footer_item = [all_footer](const bool with_preview) {
        return lib::LayoutItem{1U, 0U, 1U,
                               all_footer && with_preview ? 2U : 1U,
                               lib::Align::Stretch, lib::Align::Center, {},
                               {0.0F, 32.0F}, {}, 0.0F};
    };
    footer->set_layout_item(footer_item(static_cast<bool>(preview)));

    auto addition = std::make_shared<lib::Panel>();
    // DのAbsDialogの追加領域は、自然幅の入れ物として使
    // う。一列のGridLayoutでは、Windows側がPreview
    // 切替をフッターの空いた半分全体へ伸ばしてしまう。
    auto addition_layout = std::make_shared<lib::LinearLayout>(
        lib::Orientation::Horizontal);
    addition_layout->cross_axis = lib::Align::Center;
    addition->set_layout(addition_layout);
    addition->set_layout_item(
        lib::LayoutItem{0U, 0U, 1U, 1U, lib::Align::Stretch,
                        lib::Align::Center, {}, {0.0F, 32.0F}, {}, 1.0F});
    if (const auto& preview_toggle = editor.previewToggle(); preview_toggle) {
        preview_toggle->set_layout_item(
            lib::LayoutItem{0U, 0U, 1U, 1U, lib::Align::Start,
                            lib::Align::Center, {98.0F, 26.0F},
                            {98.0F, 26.0F}, {}, 0.0F});
        addition->add(preview_toggle);
    }
    footer->add(addition);

    auto buttons = std::make_shared<lib::Panel>();
    auto buttons_layout = std::make_shared<lib::LinearLayout>(
        lib::Orientation::Horizontal);
    buttons_layout->cross_axis = lib::Align::Center;
    buttons_layout->spacing = 6.0F;
    buttons->set_layout(buttons_layout);
    buttons->set_layout_item(
        lib::LayoutItem{0U, 1U, 1U, 1U, lib::Align::End,
                        lib::Align::Center, {}, {0.0F, 32.0F}, {}, 0.0F});
    for (const auto& button : {ok_button_, cancel_button_, apply_button_}) {
        button->set_layout_item({.preferred = {96.0F, 26.0F}});
        buttons->add(button);
    }
    footer->add(buttons);
    // Dは編集列の下にフッターを置く一方、プレ
    // ビューはフッター行も含む右側全体を使う。
    message_body->add(footer);

    editor.previewModeChanged([this, &editor, message_body, message_layout,
                               actual_editor, footer, dialog_size, size_editor,
                               preview_item, footer_item] {
        const auto preview = editor.previewWidget();
        const float fixed = editor.previewFixedWidth();
        const auto preview_width = preview
            ? (fixed > 0.0F ? fixed
                            : std::clamp(editor.previewWidth(), 280.0F,
                                         dialog_size.width * 0.6F))
            : 0.0F;
        const auto editor_width = std::max(
            360.0F, dialog_size.width - preview_width -
                         (preview ? 5.0F : 0.0F));
        message_layout->columns = preview ? 2U : 1U;
        message_body->clear();
        if (actual_editor) {
            size_editor(editor_width);
            message_body->add(actual_editor);
        }
        if (preview) {
            preview->set_layout_item(preview_item(preview_width));
            message_body->add(preview);
        }
        footer->set_layout_item(footer_item(static_cast<bool>(preview)));
        message_body->add(footer);
        if (context_.services.gui && widget_)
            context_.services.gui->request_redraw(widget_);
    });

    editor.setPreviewHostWindow(widget_);
    // Dは所有Shellを表示した後にだけopenedを呼ぶ。
    opened_body_ = [&editor] { editor.opened(); };
    closeEvent.push_back([this] {
        if (!context_.editor_props || !widget_) return;
        const auto bounds = lib::authored_window_bounds(*widget_);
        const auto save = [this, &bounds](auto& value) {
            value.x(static_cast<std::int32_t>(std::lround(bounds.x)));
            value.y(static_cast<std::int32_t>(std::lround(bounds.y)));
            value.width(static_cast<std::int32_t>(std::lround(bounds.width)));
            value.height(static_cast<std::int32_t>(std::lround(bounds.height)));
            value.maximized(widget_->maximized);
        };
        if (type_ == CType::TalkDialog)
            save(context_.editor_props->flex().speakDlg);
        else
            save(context_.editor_props->flex().msgDlg);
    });
}

void EventDialog::refreshTypedBody() {
    if (warning_label_) {
        warning_label_->text = warnings_.empty() ? String{} : warnings_.front();
        warning_label_->set_visible(!warning_label_->text.empty());
    }
    if (context_.services.gui) context_.services.gui->request_redraw(widget_);
}

void EventDialog::replaceWarnings(WarningList value) {
    warnings_ = std::move(value);
    updateWarningPresentation();
}

void EventDialog::addWarning(String value) {
    warnings_.push_back(std::move(value));
    updateWarningPresentation();
}

WarningList EventDialog::modelWarnings(const Content& value) const {
    if (!context_.prop || !skin_) return {};
    const bool classic = summary_ && summary_->legacy();
    const auto data_version = summary_ ? summary_->dataVersion() : String{};
    const auto target_version = context_.editor_props
        ? String(context_.editor_props->var().targetVersion.value)
        : String{};
    // Dは切り離した編集中の値を、許可されたイベント内の変数範囲で検証す
    // る。同じローカル変数所有元を持つ独立カウンターを使い、プレビュー利
    // 用者を使用中のシナリオ・下位カウンターへ加えて使用一覧を汚さない。
    const auto scoped_owner = use_counter_ ? use_counter_->owner() : lib::Owner<path::CWXPath>{};
    const auto scoped_variables = std::dynamic_pointer_cast<flag::LocalVariableOwner>(scoped_owner);
    const auto source_content = event_ ? event_ : parent_content_;
    const auto source_tree = source_content ? source_content->tree() : EventTreePtr{};
    const bool admitted = scoped_variables && source_tree &&
        source_tree->useCounter() == use_counter_ && summary_ &&
        summary_->findCWXPath(scoped_owner->cwxPath(true)) == scoped_owner;
    auto scoped_value = admitted ? value.dup() : ContentPtr{};
    if (scoped_value) {
        const auto preview_counter = std::make_shared<UseCounter>(scoped_owner);
        scoped_value->setUseCounter(preview_counter, scoped_owner);
    }
    auto result = warning::warnings(*context_.prop, *skin_, summary_.get(),
                                    scoped_value ? *scoped_value : value,
                                    classic, data_version, target_version,
                                    warning::WarningDomain::TypedEventDialog);
    // ダイアログは親の連鎖を持たない複製・新規Content
    // を検証するため、モデル検査ではイベントツリーへ到達でき
    // ず、全開始リンクを「見つからない」と判定し得る。編集対
    // 象のコンテントまたは親のツリーで、開始名を再検査する。
    if (!value.start().empty() && !value.tree()) {
        const auto owner = event_ ? event_ : parent_content_;
        if (const auto tree = owner ? owner->tree() : EventTreePtr{}) {
            bool found = false;
            for (const auto& start : tree->starts()) {
                if (start && start->name() == value.start()) {
                    found = true;
                    break;
                }
            }
            if (found) {
                const auto message = utils::tryFormat(
                    context_.prop->requiredMsgs().searchErrorStartNotFound.value,
                    value.start());
                result.erase(std::remove(result.begin(), result.end(), message),
                             result.end());
            }
        }
    }
    return result;
}

void EventDialog::updateWarningPresentation() {
    // DのAbsDialogは警告行を確保しない。既存のOKボタン
    // に印を付け、現在の全警告をツールチップに表示する。警告なし
    // ではクライアント領域を使わず、型付きの本体を再配置しない。
    if (!ok_button_) return;
    if (!warnings_.empty() && !warning_icon_ && context_.editor_props)
        warning_icon_ = context_.editor_props->images().load(StringView{"warning.png"});
    ok_button_->icon = warnings_.empty() ? ResourceId{} : warning_icon_;

    String tooltip;
    for (std::size_t index = 0; index < warnings_.size(); ++index) {
        if (index != 0U) tooltip.push_back('\n');
        for (const char character : warnings_[index]) {
            if (character == '&') tooltip += "&&";
            else tooltip.push_back(character);
        }
    }
    ok_button_->set_tooltip(std::move(tooltip));
    if (context_.services.gui) context_.services.gui->request_redraw(ok_button_);
}

void EventDialog::rebuildPropertyTable() {
    if (!property_table_ || !xml_editor_) return;
    const auto source_text = xml_editor_->text;
    std::vector<lib::TableRow> rows;
    decltype(property_bindings_) bindings;
    lib::XmlNode root;
    String warning;
    WarningList model_warnings;
    try {
        root = lib::XmlNode::parse(source_text, true);
        if (const auto parsed = Content::createFromNode(root, nullptr, false);
            parsed && parsed->type() == type_)
            model_warnings = modelWarnings(*parsed);
        std::function<void(lib::XmlNode, String)> append =
            [&](lib::XmlNode node, String path) {
                for (const auto& attribute : node.attributes()) {
                    lib::TableRow row;
                    row.cells = {{path + "/@" + attribute.name},
                                 {attribute.value}};
                    rows.push_back(std::move(row));
                    bindings.push_back({node, attribute.name, false});
                }
                const auto children = node.element_children();
                if (children.empty()) {
                    lib::TableRow row;
                    row.cells = {{path + "/text()"}, {node.value()}};
                    rows.push_back(std::move(row));
                    bindings.push_back({node, {}, true});
                    return;
                }
                std::unordered_map<String, std::size_t> indices;
                for (auto child : children) {
                    const auto index = indices[child.name()]++;
                    append(child, path + "/" + child.name() + "[" +
                                      std::to_string(index) + "]");
                }
            };
        append(root, "/" + root.name());
    } catch (const std::exception& error) {
        root = {};
        warning = String("XMLエラー: ") + error.what();
    }
    const std::weak_ptr<lib::TextEdit> editor = xml_editor_;
    auto publication = lib::prepare_table_rows(
        property_table_, std::move(rows), {}, {},
        [editor, source_text] {
            const auto current = editor.lock();
            return current && current->text == source_text;
        });
    if (!publication.commit()) return;
    property_root_ = std::move(root);
    property_bindings_.swap(bindings);
    replaceWarnings(std::move(model_warnings));
    if (warning_label_) warning_label_->text = std::move(warning);
    if (context_.services.gui)
        context_.services.gui->request_redraw(property_table_);
}

void EventDialog::updateProperty(const std::size_t row, String value) {
    if (row >= property_bindings_.size() || !property_root_) return;
    auto& binding = property_bindings_[row];
    try {
        if (binding.text) {
            binding.node.value(value);
        } else {
            binding.node.remove_attr(binding.attribute);
            binding.node.new_attr(binding.attribute, value);
        }
        xml_editor_->text = property_root_.text();
        rebuildPropertyTable();
    } catch (const std::exception& error) {
        if (warning_label_) warning_label_->text = String("入力エラー: ") + error.what();
    }
}

bool EventDialog::applyXmlDraft() {
    if (!xml_editor_ || !event_) return false;
    try {
        auto node = lib::XmlNode::parse(xml_editor_->text, true);
        auto parsed = Content::createFromNode(node, nullptr, false);
        if (!parsed || parsed->type() != type_)
            throw std::invalid_argument("コンテント種別が一致しません");
        event_->shallowCopy(*parsed);
        draft_ = event_->dup();
        warning_label_->text.clear();
        for (const auto& callback : appliedEvent) if (callback) callback();
        if (callbacks_.contentChanged) callbacks_.contentChanged(event_);
        if (callbacks_.applied) callbacks_.applied();
        return true;
    } catch (const std::exception& error) {
        if (warning_label_) warning_label_->text = String("XMLエラー: ") + error.what();
        if (context_.errorReporter) context_.errorReporter(error.what());
        if (context_.services.gui) context_.services.gui->request_redraw(widget_);
        return false;
    }
}

void EventDialog::open() {
    if (!setup_done_) {
        // 型付きのDダイアログは専用の操作部だけを作る。生XMLやプロパティ
        // の編集部を作って編集中の値を直列化し、その後捨てる処理は行わない。
        setupBody();
        if (context_.editor_props) {
            auto& geometry = context_.editor_props->flex();
            if (type_ == CType::BranchVariant || type_ == CType::CheckVariant)
                bindDialogGeometry(geometry.expressionDlg);
            else if (type_ == CType::SetVariant)
                bindDialogGeometry(geometry.expressionWithTargetDlg);
        }
        setup_done_ = true;
    }
    bindExternalNotifications();
    refDataVersion();
    open_ = true;
    widget_->result = lib::DialogResult::None;
    widget_->set_visible(true);
    if (context_.services.gui) context_.services.gui->show(widget_);
    if (opened_body_) opened_body_();
}

void EventDialog::active() {
    open();
    if (context_.services.gui && xml_editor_)
        context_.services.gui->focus(xml_editor_);
}

bool EventDialog::apply() {
    return commitDraft();
}

bool EventDialog::applyAndClose() {
    if (!apply()) return false;
    finishClose(lib::DialogResult::OK);
    return true;
}

void EventDialog::forceCancel() {
    finishClose(lib::DialogResult::Cancel);
}

void EventDialog::close() {
    finishClose(widget_->result == lib::DialogResult::None
                    ? lib::DialogResult::Cancel : widget_->result);
}

void EventDialog::finishClose(const lib::DialogResult result) {
    if (!open_) return;
    const auto keep_alive = shared_from_this();
    open_ = false;
    childNamesApplied = {};
    unbindExternalNotifications();
    widget_->result = result;
    if (context_.services.gui) context_.services.gui->close(widget_);
    const auto callbacks = callbacks_;
    for (const auto& callback : closeEvent) {
        if (callback) callback();
    }
    if (callbacks.closed) callbacks.closed();
}

void EventDialog::setEvent(ContentPtr value) {
    if (value && value->type() != type_)
        throw std::invalid_argument("event type does not match dialog type");
    event_ = std::move(value);
    draft_ = event_ ? event_->dup() : ContentPtr{};
    apply_enabled_ = static_cast<bool>(parent_content_) &&
                     (!event_ || !event_->parent());
    title_ = formatTitle(contentName());
    widget_->title = title_;
    if (xml_editor_) xml_editor_->text = draft_ ? draft_->toXML() : String{};
    rebuildPropertyTable();
    refDataVersion();
}

void EventDialog::setSummary(SummaryPtr value) {
    summary_ = std::move(value);
    refSkin();
    refDataVersion();
}

void EventDialog::setSkin(SkinPtr value) {
    skin_ = std::move(value);
    if (!skin_) skin_ = context_.findSkin(summary_.get());
}

void EventDialog::refSkin() {
    // Dでは初期値編集部も含め、明示されたforceSkinを固定する
    // 。通常のシナリオダイアログは、両ホストで現在の解決処理に従う。
    skin_ = original_forced_skin_
        ? original_forced_skin_ : context_.findSkin(summary_.get());
    if (context_.contentImage) icon_ = context_.contentImage(type_);
    if (widget_) widget_->icon = icon_;
    if (context_.services.gui && widget_)
        context_.services.gui->request_redraw(widget_);
    refreshSpecialTextPreviews();
}

void EventDialog::refDataVersion() {
    for (const auto& update : version_control_updates_) if (update) update();
    refreshWarning();
}

bool EventDialog::openCWXPath(String path, const bool) {
    // Dと同様に、基底ダイアログは全体の移動処理を持たない。明示的な操作はopenExternalCWXPathを使う。
    return path.empty();
}

bool EventDialog::openExternalCWXPath(String path, const bool activate) {
    if (callbacks_.openPath) return callbacks_.openPath(path, activate);
    if (context_.openCWXPath) return context_.openCWXPath(path, activate);
    if (context_.editor_commons)
        return context_.editor_commons->openCWXPath(std::move(path), activate);
    return false;
}

void EventDialog::refreshWarning() {
    refreshSpecialTextPreviews();
    // Dの基底EventDialogはイベント内の警告を持たない。DでrefreshWarning
    // を上書きする派生ダイアログだけが、型付き警告を明示的に有効にする。
    replaceWarnings({});
}

void EventDialog::refreshSpecialTextPreviews() {
    std::erase_if(special_text_updates_, [](const auto& entry) { return entry.first.expired(); });
    for (const auto& [widget, update] : special_text_updates_) if (update) update();
}

void EventDialog::bindSpecialText(const lib::WidgetPtr& editor, std::function<bool()> enabled) {
    if (!editor || !context_.editor_props) return;
    const std::weak_ptr<const int> weak_lifetime = special_text_lifetime_;
    const auto text_editor = std::dynamic_pointer_cast<lib::TextEdit>(editor);
    const auto combo_editor = std::dynamic_pointer_cast<lib::ComboBox>(editor);
    if (!text_editor && !combo_editor) return;
    std::erase_if(special_text_menus_, [](const auto& entry) { return entry.first.expired(); });
    for (const auto& [widget, binding] : special_text_menus_)
        if (widget.lock() == editor) return;
    dmenu::TextMenuModifyPtr binding;
    if (const auto chooser = std::dynamic_pointer_cast<chooser::ChooserCombo>(editor)) binding = chooser->textInputBinding;
    if (!binding) {
        dmenu::Commons comm;
        comm.services = context_.services;
        comm.undoMaxEtc = static_cast<std::size_t>(std::max<std::int32_t>(0, context_.editor_props->var().undoMaxEtc));
        comm.presentation.menuText = [prop = context_.editor_props](auto id) { return prop->buildMenu(id); };
        comm.presentation.menuImage = [prop = context_.editor_props](auto id) { return prop->images().menu(id); };
        binding = dmenu::createTextMenu(comm, text_editor ? dmenu::TMM(text_editor) : dmenu::TMM(combo_editor));
    }
    if (weak_lifetime.expired()) return;
    special_text_menus_.emplace_back(editor, binding);
    // ネイティブのコントロールやメニューはダイアログより長く残り得る。待機中のコー
    // ルバックがダイアログを保持したり、終了後の状態を読んだりしないようにする。
    if (enabled) enabled = [weak_lifetime, getter = std::move(enabled)] {
        return !weak_lifetime.expired() && getter();
    };
    messageutils::MessageContext message;
    message.services = context_.services;
    message.props = context_.editor_props;
    message.summary = summary_.get();
    message.summary_owner = summary_;
    message.editor_commons = context_.editor_commons;
    message.use_counter = use_counter_.get();
    message.use_counter_owner = use_counter_;
    (void)messageutils::setupSPCharsMenu(message, summary_.get(), [this, weak_lifetime] {
            return weak_lifetime.expired() ? SkinPtr{} : skin_;
        },
        use_counter_.get(), binding->menu(), false, true, enabled,
        [weak_lifetime, weak = std::weak_ptr<dmenu::TextMenuModify>(binding)](StringView value) {
            if (weak_lifetime.expired()) return;
            if (const auto owner = weak.lock()) {
                auto text = owner->text();
                auto next = text.getText();
                const auto selection = text.getSelection();
                const auto start = std::min(next.size(), static_cast<std::size_t>(std::max(0.0F, selection.x)));
                const auto end = std::clamp(static_cast<std::size_t>(std::max(0.0F, selection.y)), start, next.size());
                next.replace(start, end - start, value);
                text.setText(next);
                if (weak_lifetime.expired()) return;
                const auto caret = static_cast<float>(start + value.size());
                text.setSelection({caret, caret});
            }
        });
    if (weak_lifetime.expired()) return;
    const auto update = [this, weak_lifetime, weak = std::weak_ptr<lib::Widget>(editor),
        menu = std::weak_ptr<dmenu::TextMenuModify>(binding), enabled] {
        if (weak_lifetime.expired()) return;
        const auto owner = menu.lock();
        if (!owner) return;
        const auto prop = context_.editor_props;
        const auto summary = summary_;
        const auto skin = skin_;
        const auto counter = use_counter_;
        const bool expand = enabled && enabled();
        if (weak_lifetime.expired()) return;
        const auto preview = expand
            ? messageutils::createSPCharPreview(*prop, summary.get(), skin, counter.get(), owner->text().getText(), true)
            : String{};
        if (weak_lifetime.expired()) return;
        if (const auto target = weak.lock()) target->set_tooltip(preview);
    };
    special_text_updates_.emplace_back(editor, update);
    const auto menu = binding->menu();
    const auto previous_open = menu->on_open;
    menu->on_open = [weak_lifetime, previous_open] {
        if (!weak_lifetime.expired() && previous_open) previous_open();
    };
    for (auto& item : menu->items) if (item.command == "put-special" && item.open_children) {
        const auto children = item.open_children;
        item.open_children = [weak_lifetime, children] {
            if (weak_lifetime.expired()) return lib::Array<lib::MenuItem>{};
            auto result = children();
            return weak_lifetime.expired() ? lib::Array<lib::MenuItem>{} : result;
        };
    }
    const auto changed = text_editor ? text_editor->on_changed : combo_editor->on_text_changed;
    auto handler = [weak_lifetime, changed, update, weak = std::weak_ptr<dmenu::TextMenuModify>(binding)](StringView value) {
        if (weak_lifetime.expired()) return;
        if (changed) changed(value);
        if (weak_lifetime.expired()) return;
        update();
        if (weak_lifetime.expired()) return;
        if (const auto owner = weak.lock()) owner->modifyText();
    };
    if (text_editor) text_editor->on_changed = std::move(handler);
    else combo_editor->on_text_changed = std::move(handler);
    update();
}

void EventDialog::refExternalPath(StringView, StringView, bool) {}
void EventDialog::refExternalPaths(StringView) {}
void EventDialog::refExternalFlagAndStep() { refreshWarning(); }
void EventDialog::delExternalFlagAndStep() { refExternalFlagAndStep(); }
void EventDialog::delExternalFlagAndStep(bool, bool) {
    // 種別専用の削除処理を持たないダイアログでは、既存の派生フックを維持する。
    delExternalFlagAndStep();
}
void EventDialog::refExternalImageScale() {}
void EventDialog::refExternalUndoMax() {}
void EventDialog::refExternalResources(chooser::AreaChoiceKind) {}

void EventDialog::delContent(const ContentPtr& value) {
    if (!value) return;
    if ((event_ && event_->isDescendant(value)) ||
        (parent_content_ && parent_content_->isDescendant(value))) {
        forceCancel();
    }
}

void EventDialog::bindExternalNotifications() {
    if (notification_commons_ || !context_.editor_commons) return;
    const auto weak = weak_from_this();
    if (weak.expired()) return;
    notification_commons_ = context_.editor_commons;
    const auto names_changed = [weak] {
        if (const auto dialog = weak.lock()) dialog->refExternalNames();
    };
    ref_gossips_connection_ = notification_commons_->refGossips.add(names_changed);
    ref_stamps_connection_ = notification_commons_->refCompleteStamps.add(names_changed);
    repl_text_connection_ = notification_commons_->replText.add(names_changed);
    ref_skin_connection_ = notification_commons_->refSkin.add([weak] {
        if (const auto dialog = weak.lock()) dialog->refSkin();
    });
    ref_data_version_connection_ = notification_commons_->refDataVersion.add([weak] {
        if (const auto dialog = weak.lock()) dialog->refDataVersion();
    });
    // Dはデータ形式と対象バージョンの両変更でコンテン
    // ト警告を更新する。一つの仮想経路を使い、型付き
    // ダイアログ独自の互換性操作部の更新も維持する。
    ref_target_version_connection_ = notification_commons_->refTargetVersion.add([weak] {
        if (const auto dialog = weak.lock()) dialog->refDataVersion();
    });
    ref_path_connection_ = notification_commons_->refPath.add(
        [weak](String old_path, String new_path, bool is_directory) {
            if (const auto dialog = weak.lock()) {
                dialog->refExternalPath(old_path, new_path, is_directory);
                dialog->refreshSpecialTextPreviews();
            }
        });
    ref_paths_connection_ = notification_commons_->refPaths.add(
        [weak](String parent_path) {
            if (const auto dialog = weak.lock()) {
                dialog->refExternalPaths(parent_path);
                dialog->refreshSpecialTextPreviews();
            }
        });
    del_paths_connection_ = notification_commons_->delPaths.add([weak] {
        if (const auto dialog = weak.lock()) dialog->refExternalPaths(StringView{});
    });
    ref_flag_step_connection_ = notification_commons_->refFlagAndStep.add(
        [weak](commons::flag::FlagList, commons::flag::StepList, commons::flag::VariantList) {
            if (const auto dialog = weak.lock(); dialog && dialog->isOpen()) { dialog->refExternalFlagAndStep(); if (dialog->isOpen()) dialog->refreshSpecialTextPreviews(); }
        });
    del_flag_step_connection_ = notification_commons_->delFlagAndStep.add(
        [weak](flag::FlagList deletedFlags, flag::StepList deletedSteps,
               flag::VariantList) {
            if (const auto dialog = weak.lock(); dialog && dialog->isOpen()) {
                dialog->delExternalFlagAndStep(!deletedFlags.empty(),
                                               !deletedSteps.empty());
                if (dialog->isOpen()) dialog->refreshSpecialTextPreviews();
            }
        });
    ref_image_scale_connection_ = notification_commons_->refImageScale.add([weak] {
        if (const auto dialog = weak.lock()) dialog->refExternalImageScale();
    });
    ref_undo_max_connection_ = notification_commons_->refUndoMax.add([weak] {
        if (const auto dialog = weak.lock()) dialog->refExternalUndoMax();
    });
    ref_preview_values_connection_ = notification_commons_->refPreviewValues.add([weak] {
        if (const auto dialog = weak.lock()) dialog->refreshSpecialTextPreviews();
    });
    del_content_connection_ = notification_commons_->delContent.add(
        [weak](const ContentPtr& deleted) {
            if (const auto dialog = weak.lock()) dialog->delContent(deleted);
        });
    // Dの資源選択ダイアログは、選択状態とは独立して削除を
    // 監視する。絞り込みですでに選択IDが0の場合も含む。
    del_resource_area_connection_ = notification_commons_->delArea.add([weak](const commons::AreaPtr&) {
        if (const auto dialog = weak.lock()) dialog->refExternalResources(chooser::AreaChoiceKind::Area);
    });
    del_resource_battle_connection_ = notification_commons_->delBattle.add([weak](const commons::BattlePtr&) {
        if (const auto dialog = weak.lock()) dialog->refExternalResources(chooser::AreaChoiceKind::Battle);
    });
    del_resource_package_connection_ = notification_commons_->delPackage.add([weak](const commons::PackagePtr&) {
        if (const auto dialog = weak.lock()) dialog->refExternalResources(chooser::AreaChoiceKind::Package);
    });
    del_resource_cast_connection_ = notification_commons_->delCast.add([weak](const commons::CastCardPtr&) {
        if (const auto dialog = weak.lock()) dialog->refExternalResources(chooser::AreaChoiceKind::Cast);
    });
    del_resource_info_connection_ = notification_commons_->delInfo.add([weak](const commons::InfoCardPtr&) {
        if (const auto dialog = weak.lock()) dialog->refExternalResources(chooser::AreaChoiceKind::Info);
    });
}

void EventDialog::unbindExternalNotifications() noexcept {
    if (!notification_commons_) return;
    notification_commons_->refGossips.remove(ref_gossips_connection_);
    notification_commons_->refCompleteStamps.remove(ref_stamps_connection_);
    notification_commons_->replText.remove(repl_text_connection_);
    notification_commons_->refSkin.remove(ref_skin_connection_);
    notification_commons_->refDataVersion.remove(ref_data_version_connection_);
    notification_commons_->refTargetVersion.remove(ref_target_version_connection_);
    notification_commons_->refPath.remove(ref_path_connection_);
    notification_commons_->refPaths.remove(ref_paths_connection_);
    notification_commons_->delPaths.remove(del_paths_connection_);
    notification_commons_->refFlagAndStep.remove(ref_flag_step_connection_);
    notification_commons_->delFlagAndStep.remove(del_flag_step_connection_);
    notification_commons_->refImageScale.remove(ref_image_scale_connection_);
    notification_commons_->refUndoMax.remove(ref_undo_max_connection_);
    notification_commons_->refPreviewValues.remove(ref_preview_values_connection_);
    notification_commons_->delContent.remove(del_content_connection_);
    notification_commons_->delArea.remove(del_resource_area_connection_);
    notification_commons_->delBattle.remove(del_resource_battle_connection_);
    notification_commons_->delPackage.remove(del_resource_package_connection_);
    notification_commons_->delCast.remove(del_resource_cast_connection_);
    notification_commons_->delInfo.remove(del_resource_info_connection_);
    notification_commons_ = nullptr;
    ref_skin_connection_ = 0;
    ref_data_version_connection_ = 0;
    ref_target_version_connection_ = 0;
    ref_path_connection_ = 0;
    ref_paths_connection_ = 0;
    del_paths_connection_ = 0;
    ref_flag_step_connection_ = 0;
    del_flag_step_connection_ = 0;
    ref_image_scale_connection_ = 0;
    ref_undo_max_connection_ = 0;
    del_content_connection_ = 0;
    del_resource_area_connection_ = 0;
    del_resource_battle_connection_ = 0;
    del_resource_package_connection_ = 0;
    del_resource_cast_connection_ = 0;
    del_resource_info_connection_ = 0;
}

ContentPtr EventDialog::ensureDraft() {
    if (!draft_) draft_ = std::make_shared<Content>(type_);
    return draft_;
}

bool EventDialog::commitDraft() {
    const auto value = ensureDraft();
    if (!event_) {
        event_ = std::make_shared<Content>(type_, value->name());
        if (parent_content_) event_->parent(parent_content_);
    }
    event_->shallowCopy(*value);
    // Dは空の名前だけを補う。親の項目の後に現在の子を正規化し、古い編
    // 集値の位置をコピーしたり、指定済みラベルを再検証したりしない。
    if (type_ == CType::CustomXmlContent && context_.prop) {
        const auto changes = xmlcontentdialog::XmlContentDialog::applyBranchNames(
            event_, *context_.prop);
        if (childNamesApplied) childNamesApplied(changes);
    }
    const auto applied = appliedEvent;
    for (const auto& callback : applied) {
        if (callback) callback();
    }
    if (callbacks_.contentChanged) callbacks_.contentChanged(event_);
    if (context_.refreshToolBar) context_.refreshToolBar();
    widget_->result = lib::DialogResult::Apply;
    setApplyEnabled(false);
    return true;
}

bool EventDialog::targetVersion(const StringView target,
                                const StringView wsn) const {
    if (summary_)
        return summary_->isTargetVersion(wsn.empty() ? target : wsn);
    if (context_.prop)
        return context_.prop->isTargetVersion({}, target, wsn);
    return true;
}

String EventDialog::contentName() const {
    if (context_.contentName) return context_.contentName(type_);
    if (context_.prop) return context_.prop->requiredMsgs().contentName(type_);
    return fallbackContentName(type_);
}

String EventDialog::formatTitle(String name) const {
    if (context_.prop)
        return utils::tryFormat(context_.prop->requiredMsgs().dlgTitContent.value, name);
    return std::move(name);
}

ContentCommentDialog::ContentCommentDialog(Context context, Parent parent,
                                           ContentPtr parentContent,
                                           ContentPtr content)
    : context_(std::move(context)), parent_(std::move(parent)),
      parent_content_(std::move(parentContent)), event_(std::move(content)),
      widget_(std::make_shared<lib::Dialog>()) {
    if (!event_) throw std::invalid_argument("comment dialog requires content");
    draft_ = event_->dup();
    comment_ = draft_->comment();
    notification_commons_ = context_.editor_commons;
    if (notification_commons_)
        notification_commons_lifetime_ = notification_commons_->lifetimeToken();
    setupUi();
}

ContentCommentDialog::ContentCommentDialog(Context context, Parent parent,
                                           EventTreePtr eventTree)
    : context_(std::move(context)), parent_(std::move(parent)),
      event_tree_(std::move(eventTree)), widget_(std::make_shared<lib::Dialog>()) {
    if (!event_tree_)
        throw std::invalid_argument("comment dialog requires event tree");
    comment_ = event_tree_->comment();
    notification_commons_ = context_.editor_commons;
    if (notification_commons_)
        notification_commons_lifetime_ = notification_commons_->lifetimeToken();
    setupUi();
}

ContentCommentDialog::~ContentCommentDialog() {
    notification_lifetime_.reset();
    if (notification_commons_ && !notification_commons_lifetime_.expired())
        notification_commons_->delContent.remove(del_content_connection_);
}

void ContentCommentDialog::setupUi() {
    widget_->title = context_.prop ? context_.prop->requiredMsgs().dlgTitComment.value
                                   : String{"Comment"};
    auto owner = std::dynamic_pointer_cast<lib::Window>(parent_);
    if (!owner) owner = context_.dialog_owner;
    lib::configure_owned_window(widget_, owner,
        lib::OwnedWindowKind::Modeless);
    widget_->minimum_size = lib::Size{420.0F, 260.0F};
    widget_->set_bounds({0.0F, 0.0F, 560.0F, 380.0F});
    widget_->on_close_requested = [this] { finishClose(lib::DialogResult::Cancel); };

    auto layout = std::make_shared<lib::GridLayout>();
    layout->columns = 3U;
    layout->spacing = 6.0F;
    layout->padding = {8.0F, 8.0F, 8.0F, 8.0F};
    widget_->set_layout(layout);

    editor_ = std::make_shared<lib::TextEdit>();
    editor_->multiline = true;
    editor_->wrap = true;
    editor_->vertical_scroll = true;
    editor_->text = comment_;
    editor_->selection_start = editor_->text.size();
    editor_->on_changed = [this](const StringView value) {
        comment_.assign(value);
    };
    editor_->set_layout_item({.row = 0U, .column = 0U, .column_span = 3U,
                              .grow = 1.0F});
    widget_->add(editor_);
    // 標準テキストメニューが取り消し履歴を提供する。入力・切り
    // 取り・貼り付けをCtrl+Z/Ctrl+Yで戻せる。ALL
    // のタイトル部の元に戻す・やり直しも同じキーを送る。
    {
        dmenu::Commons comm;
        comm.services = context_.services;
        if (context_.editor_props) {
            comm.presentation.textTabs = static_cast<std::size_t>(
                std::max(1, context_.editor_props->var().textTabs.value));
            comm.undoMaxEtc = static_cast<std::size_t>(
                std::max<std::int32_t>(0, context_.editor_props->var().undoMaxEtc));
            comm.presentation.menuText = [prop = context_.editor_props](auto id) {
                return prop->buildMenu(id);
            };
            comm.presentation.menuImage = [prop = context_.editor_props](auto id) {
                return prop->images().menu(id);
            };
        }
        text_menu_ = dmenu::createTextMenu(comm, dmenu::TMM(editor_));
    }
    // Dはテキストメニューの設定後に、共通のテキストダイアログ用フォント
    // を適用する。他のテキストダイアログと同じ共通のフォント解決を使う。
    if (context_.editor_props) {
        const auto default_size = editor_->font()
            ? static_cast<std::uint32_t>(editor_->font()->points) : 0U;
        editor_->set_font(dutils::fontSpecFromDwtData(
            context_.editor_props->adjustFont(
                context_.editor_props->looks().textDlgFont(default_size))));
    }

    ok_button_ = std::make_shared<lib::Button>("OK");
    ok_button_->default_button = true;
    ok_button_->on_click = [this] {
        if (apply()) finishClose(lib::DialogResult::OK);
    };
    ok_button_->set_layout_item({.row = 1U, .column = 0U,
                                 .preferred = {96.0F, 26.0F}});
    widget_->add(ok_button_);

    cancel_button_ = std::make_shared<lib::Button>(
        context_.prop ? context_.prop->requiredMsgs().dlgTextCancel.value
                      : String{"キャンセル(&C)"});
    cancel_button_->cancel_button = true;
    cancel_button_->on_click = [this] {
        finishClose(lib::DialogResult::Cancel);
    };
    cancel_button_->set_layout_item({.row = 1U, .column = 1U,
                                     .preferred = {96.0F, 26.0F}});
    widget_->add(cancel_button_);

    apply_button_ = std::make_shared<lib::Button>(
        (context_.prop ? context_.prop->requiredMsgs().apply.value : String{"適用"}) +
        "(&A)");
    apply_button_->on_click = [this] { (void)apply(); };
    apply_button_->set_layout_item({.row = 1U, .column = 2U,
                                    .preferred = {96.0F, 26.0F}});
    widget_->add(apply_button_);
}

bool ContentCommentDialog::apply() {
    if (closed_ || applying_) return false;
    const auto life = std::weak_ptr<int>(notification_lifetime_);
    const auto revision = operation_revision_;
    const auto content = event_;
    const auto tree = event_tree_;
    const auto widget = widget_;
    const auto commit = commitComment;
    const auto callbacks = appliedEvent;
    applying_ = true;
    const auto finish = [this, life](void*) {
        if (!life.expired()) applying_ = false;
    };
    const std::unique_ptr<void, decltype(finish)> guard(this, finish);
    const auto current = [this, life, revision, content, tree, widget] {
        return !life.expired() && !closed_ && operation_revision_ == revision &&
               event_ == content && event_tree_ == tree && widget_ == widget;
    };
    // DのContentCommentDialog.applyは改行と末尾の一つのLFを正規化する。
    const auto saved = utils::lastRet(dutils::wrapReturnCode(comment_));
    if (content) {
        if (commit) {
            if (!commit(saved) || !current()) return false;
        } else {
            content->comment(saved);
            if (!current()) return false;
        }
        draft_ = content->dup();
    } else if (tree) {
        tree->comment(saved);
        if (!current()) return false;
    } else {
        return false;
    }
    for (const auto& callback : callbacks) {
        if (callback) callback();
        if (!current()) return false;
    }
    widget->result = lib::DialogResult::Apply;
    return true;
}

void ContentCommentDialog::open() {
    ++operation_revision_;
    closed_ = false;
    if (event_ && notification_commons_ && !notification_commons_lifetime_.expired() &&
        !del_content_connection_) {
        const auto life = std::weak_ptr<int>(notification_lifetime_);
        del_content_connection_ = notification_commons_->delContent.add(
            [this, life](const ContentPtr& deleted) {
                if (life.expired() || !deleted) return;
                // Dのeventdialog.d:141に合わせ、祖先関係がまだ残っている間に取り消す。
                if ((event_ && event_->isDescendant(deleted)) ||
                    (parent_content_ && parent_content_->isDescendant(deleted)))
                    forceCancel();
            });
    }
    open_ = true;
    widget_->result = lib::DialogResult::None;
    if (context_.services.gui) {
        context_.services.gui->show(widget_);
        context_.services.gui->focus(editor_);
    } else {
        widget_->set_visible(true);
    }
}

void ContentCommentDialog::active() {
    if (!open_) {
        open();
        return;
    }
    if (context_.services.gui) {
        context_.services.gui->show(widget_);
        context_.services.gui->focus(editor_);
    }
}

void ContentCommentDialog::forceCancel() {
    finishClose(lib::DialogResult::Cancel);
}

void ContentCommentDialog::close() {
    finishClose(widget_->result == lib::DialogResult::None
                    ? lib::DialogResult::Cancel : widget_->result);
}

void ContentCommentDialog::finishClose(const lib::DialogResult result) {
    if (!open_) return;
    const auto life = std::weak_ptr<int>(notification_lifetime_);
    const auto widget = widget_;
    const auto gui = context_.services.gui;
    const auto callbacks = closeEvent;
    open_ = false;
    closed_ = true;
    ++operation_revision_;
    if (notification_commons_ && !notification_commons_lifetime_.expired())
        notification_commons_->delContent.remove(del_content_connection_);
    del_content_connection_ = 0;
    widget->result = result;
    if (gui) gui->close(widget);
    if (life.expired()) return;
    for (const auto& callback : callbacks) {
        if (callback) callback();
        if (life.expired()) return;
    }
}

RangePanel::RangePanel(Context context, SummaryPtr summary,
                       UseCounterPtr useCounter,
                       std::vector<types::Range> ranges, String initCoupon,
                       CType type, ContentPtr content)
    : context_(std::move(context)), summary_(std::move(summary)),
      use_counter_(std::move(useCounter)), ranges_(std::move(ranges)),
      type_(type), initial_coupon_(std::move(initCoupon)),
      coupon_(initial_coupon_) {
    if (ranges_.empty()) ranges_.push_back(types::Range::Selected);
    range_ = ranges_.front();
    if (content) {
        const auto candidate = content->range();
        if (containsRange(ranges_, candidate)) range_ = candidate;
        initial_coupon_ = content->holdingCoupon();
        coupon_ = initial_coupon_;
    }
    refDataVersion();
}

bool RangePanel::rangeEnabled(const types::Range value) const noexcept {
    if (!containsRange(ranges_, value)) return false;
    return !summary_ || !summary_->legacy() ||
        (value != types::Range::CouponHolder &&
         value != types::Range::CardTarget && value != types::Range::Npc);
}

void RangePanel::setRange(const types::Range value) {
    if (rangeEnabled(value)) range_ = value;
    else if (!ranges_.empty()) range_ = ranges_.front();
    updateEnabled();
}

void RangePanel::refDataVersion() {
    if (!rangeEnabled(range_)) {
        range_ = ranges_.front();
    }
    updateEnabled();
}

void RangePanel::updateEnabled() {
    enabled_ = true;
    (void)warnings();
    if (refresh_warning_) refresh_warning_();
}

const WarningList& RangePanel::warnings() const {
    // Dは読み取りごとに、現在のクーポンとシナリオから警告を求める。
    warnings_.clear();
    if (!context_.prop) return warnings_;
    const auto& prop=*context_.prop;
    const auto& messages=prop.requiredMsgs();
    const auto target=context_.vars ? context_.vars->targetVersion.value : String{};
    const auto supports=[&](const StringView version) {
        return context_.editor_props ? context_.editor_props->isTargetVersion(summary_.get(),version)
            : prop.isTargetVersion(summary_,target,version);
    };
    if (range_ == types::Range::CouponHolder) {
        const String version=type_==CType::Effect ? "2" : "3";
        if ((type_==CType::Effect || type_==CType::GetCoupon || type_==CType::LoseCoupon) && !supports(version))
            warnings_.push_back(utils::tryFormat(messages.warningCouponHolder.value,
                messages.contentName(type_),version));
        const auto coupons=warning::couponWarnings(prop,summary_ && summary_->legacy(),
            summary_ ? summary_->dataVersion() : String(versioninfo::LATEST_VERSION),target,
            coupon_,false,messages.couponForRange.value);
        warnings_.insert(warnings_.end(),coupons.begin(),coupons.end());
    }
    if (range_ == types::Range::CardTarget && !supports("2"))
        warnings_.push_back(messages.warningCardTarget.value);
    // DのRangePanelはNPCの制限判定を持たない。CouponEventと
    // BranchMultiCouponが外側のダイアログのフックで警告を構成する。
    return warnings_;
}

TransitionPanel::TransitionPanel(Context context, SummaryPtr summary,
                                 ContentPtr content)
    : context_(std::move(context)), summary_(std::move(summary)) {
    if (content) {
        transition_ = content->transition();
        transition_speed_ = content->transitionSpeed();
    }
    refreshTS();
    if (context_.editor_commons) {
        version_connection_ = context_.editor_commons->refDataVersion.add([this] { refreshControls(); });
        scenario_connection_ = context_.editor_commons->refScenario.add(
            [this](const SummaryPtr&) { refreshControls(); });
    }
}

TransitionPanel::~TransitionPanel() {
    if (context_.editor_commons) {
        context_.editor_commons->refDataVersion.remove(version_connection_);
        context_.editor_commons->refScenario.remove(scenario_connection_);
    }
}

void TransitionPanel::bindControls(const std::shared_ptr<lib::ComboBox>& kind,
                                   const std::shared_ptr<lib::NumberEdit>& speed) {
    kind_control_ = kind;
    speed_control_ = speed;
    if (context_.prop && !kind->items.empty())
        kind->items.front().text = context_.prop->requiredMsgs().defaultSelection(
            context_.prop->requiredMsgs().transitionName(types::Transition::Default));
    refreshControls();
}

void TransitionPanel::refreshControls() {
    if (const auto kind = kind_control_.lock())
        kind->set_enabled(!summary_ || !summary_->legacy() || transition_ != types::Transition::Default);
    if (const auto speed = speed_control_.lock())
        speed->set_enabled(transition_ != types::Transition::Default && transition_ != types::Transition::None);
}

void TransitionPanel::refreshTS() {
    refreshControls();
    warnings_.clear();
    if (context_.prop && transition_ != types::Transition::Default &&
        !context_.prop->isTargetVersion(summary_, "", "")) {
        warnings_.push_back(context_.prop->requiredMsgs().warningTransitionType.value);
    }
    const auto callbacks = modEvent;
    for (const auto& callback : callbacks) {
        if (callback) callback();
    }
}

template <CType Type, typename... Extra>
void AreaSelectDialog<Type, Extra...>::buildControls() {
    auto root = makeGridPanel(1U, 5.0F, {3.0F, 3.0F, 3.0F, 3.0F});
    auto chooser_host = makeGridPanel(1U, 0.0F, {});
    chooser_host->set_layout_item({.row = 0, .column = 0,
                                   .preferred = {295.0F, 205.0F},
                                   .grow = 1.0F});
    root->add(chooser_host);

    auto properties = chooserProperties(
        context_, [this](const StringView path_value, const bool activate) {
            (void)openExternalCWXPath(String(path_value), activate);
        });
    constexpr auto choice_kind = [] {
        if constexpr (Type == CType::ChangeArea) return chooser::AreaChoiceKind::Area;
        else if constexpr (Type == CType::StartBattle) return chooser::AreaChoiceKind::Battle;
        else if constexpr (Type == CType::CallPackage || Type == CType::LinkPackage)
            return chooser::AreaChoiceKind::Package;
        else if constexpr (Type == CType::BranchCast || Type == CType::GetCast ||
                           Type == CType::LoseCast)
            return chooser::AreaChoiceKind::Cast;
        else
            return chooser::AreaChoiceKind::Info;
    }();
    area_chooser_ = std::make_unique<chooser::AreaChooserBase>(
        chooserCommons(context_), summary_, chooser_host.get(),
        choice_kind, false, std::move(properties));
    area_chooser_->selected(selected_);
    if constexpr (choice_kind == chooser::AreaChoiceKind::Cast ||
                  choice_kind == chooser::AreaChoiceKind::Info) {
        if (const auto& list = area_chooser_->list(); list && list->columns.size() >= 2U) {
            // DのCast/Infoの選択部は、見出しのない枠だけの一覧。
            list->columns[0].title.clear();
            list->columns[0].width = 50.0F;
            list->columns[1].title.clear();
            list->columns[1].width = 245.0F;
        }
    }
    area_chooser_->modEvent.push_back([this] {
        selected_ = area_chooser_ ? area_chooser_->selected() : selected_;
        setApplyEnabled(true);
    });
    // 保存値がない新規コンテントでは、選択部は最初の項目へ切り
    // 替える。その表示上の選択を、実際の選択として採用する。
    selected_ = area_chooser_->selected();
    // AreaChooserはツリー・表の両モードで、共通の内部配置を管理する。
    area_chooser_->widget()->set_layout_item(
        {.horizontal = lib::Align::Stretch, .vertical = lib::Align::Stretch,
         .preferred = {295.0F, 204.0F}, .grow = 1.0F});

    if constexpr (Type == CType::GetCast) {
        // DのCast取得ダイアログは、選択部の隣にこの
        // 設定を置く。モデルはすでにstartAction
        // を保存するため、操作できる編集欄も提供する。
        auto action = makeGridPanel(2U, 4.0F, {});
        addLabel(action, "戦闘行動開始タイミング", 0U, 0U);
        auto combo = std::make_shared<lib::ComboBox>();
        combo->editable = false;
        combo->items = {{"即座に行動", "now"},
                        {"このラウンドから行動", "current"},
                        {"次のラウンドから行動", "next"}};
        combo->set_enabled(!summary_ || !summary_->legacy());
        version_control_updates_.push_back([this, combo] {
            const bool legacy = summary_ && summary_->legacy();
            if (legacy) {
                const bool normalized = start_action_ != types::StartAction::NextRound;
                start_action_ = types::StartAction::NextRound;
                combo->selected_index = 2U;
                // Dはこの形式変換を未確定の編集として保持する。
                if (normalized) setApplyEnabled(true);
            }
            combo->set_enabled(!legacy);
        });
        combo->selected_index = start_action_ == types::StartAction::Now
            ? 0U
            : (start_action_ == types::StartAction::CurrentRound ? 1U : 2U);
        combo->on_selection_changed = [this](const auto index) {
            if (!index) return;
            start_action_ = *index == 0U
                ? types::StartAction::Now
                : (*index == 1U ? types::StartAction::CurrentRound
                                : types::StartAction::NextRound);
            refreshWarning();
            setApplyEnabled(true);
        };
        combo->set_layout_item({.row = 0U, .column = 1U,
                                .preferred = {152.0F, 24.0F}, .grow = 1.0F});
        action->add(combo);
        action->set_layout_item({.row = 1U, .column = 0U,
                                 .preferred = {295.0F, 24.0F}});
        root->add(action);
        installTypedBody(root, {330.0F, 330.0F}, true);
    } else if constexpr (Type == CType::ChangeArea) {
        auto separator = std::make_shared<lib::Separator>();
        separator->set_layout_item({.row = 1, .column = 0,
                                    .preferred = {295.0F, 1.0F}});
        root->add(separator);
        auto transition_body = makeStackedTransitionControls(
            transition_, context_, [this] { setApplyEnabled(true); });
        transition_body->set_layout_item({.row = 2, .column = 0,
                                          .preferred = {295.0F, 76.0F}});
        root->add(transition_body);
        installTypedBody(root, {307.0F, 385.0F}, true);
    } else {
        installTypedBody(root, {330.0F, 330.0F}, true);
    }
    if (context_.editor_props) bindDialogGeometry(context_.editor_props->flex().selEvtDlg);
}

template <CType Type, typename... Extra>
void AreaSelectDialog<Type, Extra...>::refreshWarning() {
    if constexpr (Type == CType::GetCast) {
        // DのGetCastの上書き処理は、この開始時行動の互換規則だけを担当する。
        WarningList result;
        if (summary_ && context_.editor_props) {
            const bool unsupported = summary_->legacy()
                ? start_action_ != types::StartAction::NextRound
                : !context_.editor_props->isTargetVersion(summary_.get(), "2") &&
                      start_action_ != types::StartAction::Now;
            if (unsupported)
                result.push_back(
                    context_.editor_props->requiredMsgs().warningStartAction.value);
        }
        replaceWarnings(std::move(result));
    } else {
        refreshSpecialTextPreviews();
        replaceWarnings({});
    }
}

template void AreaSelectDialog<CType::ChangeArea>::buildControls();
template void AreaSelectDialog<CType::StartBattle>::buildControls();
template void AreaSelectDialog<CType::CallPackage>::buildControls();
template void AreaSelectDialog<CType::LinkPackage>::buildControls();
template void AreaSelectDialog<CType::BranchCast>::buildControls();
template void AreaSelectDialog<CType::GetCast>::buildControls();
template void AreaSelectDialog<CType::LoseCast>::buildControls();
template void AreaSelectDialog<CType::BranchInfo>::buildControls();
template void AreaSelectDialog<CType::GetInfo>::buildControls();
template void AreaSelectDialog<CType::LoseInfo>::buildControls();
template void AreaSelectDialog<CType::ChangeArea>::refreshWarning();
template void AreaSelectDialog<CType::StartBattle>::refreshWarning();
template void AreaSelectDialog<CType::CallPackage>::refreshWarning();
template void AreaSelectDialog<CType::LinkPackage>::refreshWarning();
template void AreaSelectDialog<CType::BranchCast>::refreshWarning();
template void AreaSelectDialog<CType::GetCast>::refreshWarning();
template void AreaSelectDialog<CType::LoseCast>::refreshWarning();
template void AreaSelectDialog<CType::BranchInfo>::refreshWarning();
template void AreaSelectDialog<CType::GetInfo>::refreshWarning();
template void AreaSelectDialog<CType::LoseInfo>::refreshWarning();

template <CType Type>
void StartSelectDialog<Type>::buildControls() {
    auto root = makeGridPanel(1U, 4.0F, {4.0F, 4.0F, 4.0F, 4.0F});
    start_control_ = std::make_shared<lib::ListView>();
    incsearch::IncSearchSettings search_settings;
    if (context_.vars) {
        search_settings.startIncrementalSearchWhenKeyDown =
            context_.vars->startIncrementalSearchWhenKeyDown.value;
        const auto* vars = context_.vars;
        search_settings.startIncrementalSearchWhenKeyDownProvider = [vars] {
            return vars && vars->startIncrementalSearchWhenKeyDown.value;
        };
        search_settings.incrementalSearchBoxWidth =
            context_.vars->incrementalSearchBoxWidth.value;
        search_settings.incrementalSearchType =
            context_.vars->incrementalSearchType.value;
    }
    if (context_.prop) {
        search_settings.containsLabel = context_.prop->requiredMsgs().incSearchContains.value;
        search_settings.wildcardLabel = context_.prop->requiredMsgs().incSearchWildcard.value;
        search_settings.regexLabel = context_.prop->requiredMsgs().incSearchRegex.value;
        search_settings.windowTitle =
            context_.prop->requiredMsgs().menuText(types::MenuID::IncSearch);
    }
    incremental_search_ = std::make_shared<incsearch::IncSearch>(
        context_.services, start_control_.get(), [this] { return hasStarts(); },
        std::vector<incsearch::AdditionMatcher>{}, std::move(search_settings));
    incremental_search_->modEvent.push_back([this] {
        if (context_.vars)
            context_.vars->incrementalSearchType.value =
                static_cast<std::int32_t>(incremental_search_->searchType());
        refreshStarts();
    });

    start_control_->on_selection_changed = [this](const auto& selection) {
        if (selection.empty() || selection.front() >= visible_starts_.size() ||
            !visible_starts_[selection.front()]) return;
        selected_ = visible_starts_[selection.front()]->name();
        setApplyEnabled(true);
    };
    start_control_->on_activate = [this](const std::size_t index) {
        if (index >= visible_starts_.size() || !visible_starts_[index]) return;
        selected_ = visible_starts_[index]->name();
        setApplyEnabled(true);
        openSelected();
    };
    start_control_->on_text_input = [this](lib::TextInputEvent& event) {
        if (incremental_search_ &&
            incremental_search_->handleHostTextInput(event, start_control_.get()))
            event.handled = true;
    };
    start_control_->on_key_down = [this](lib::KeyEvent& event) {
        if (incremental_search_ &&
            incremental_search_->handleHostKeyDown(event, start_control_.get()))
            event.handled = true;
    };
    start_control_->on_focus = [this] {
        if (incremental_search_) incremental_search_->handleFocusChanged(start_control_.get());
    };
    start_control_->on_blur = [this] {
        if (!incremental_search_ || !context_.services.gui) return;
        const auto focused = context_.services.gui->focused_widget();
        incremental_search_->handleFocusChanged(focused.get());
    };
    start_control_->on_visibility_changed = [this](const bool visible) {
        if (!incremental_search_) return;
        if (!visible) incremental_search_->close();
        else incremental_search_->checkEnabled();
    };
    if (widget_) {
        const auto previous_bounds = widget_->on_bounds_changed;
        widget_->on_bounds_changed = [this, previous_bounds](const lib::Rect bounds) {
            if (previous_bounds) previous_bounds(bounds);
            if (incremental_search_) incremental_search_->resize();
        };
        const auto previous_deactivated = widget_->on_deactivated;
        widget_->on_deactivated = [this, previous_deactivated] {
            if (previous_deactivated) previous_deactivated();
            if (!incremental_search_ || !context_.services.gui) return;
            const auto focused = context_.services.gui->focused_widget();
            incremental_search_->handleFocusChanged(focused.get());
        };
    }

    context_menu_ = std::make_shared<lib::Menu>(true);
    const auto search_label = context_.prop
        ? context_.prop->requiredMsgs().menuText(types::MenuID::IncSearch)
        : String("インクリメンタル検索");
    const auto open_label = context_.prop
        ? context_.prop->requiredMsgs().menuText(types::MenuID::OpenAtEventView)
        : String("イベントビューで開く");
    context_menu_->items = {
        {search_label, "incremental-search"},
        {.separator = true},
        {open_label, "open-event-view"},
    };
    context_menu_->on_open = [this] {
        if (!context_menu_) return;
        auto items = context_menu_->items;
        if (items.size() >= 3U) {
            items[0].enabled = hasStarts();
            items[2].enabled = start_control_ && !start_control_->selection.empty();
        }
        (void)lib::replace_menu_items(context_menu_, std::move(items));
    };
    const std::weak_ptr<int> search_lifetime = start_lifetime_;
    context_menu_->on_command = [this, search_lifetime](const StringView command) {
        if (search_lifetime.expired()) return;
        if (command == "incremental-search") {
            const auto search = incremental_search_;
            const auto list = start_control_;
            const auto gui = context_.services.gui;
            if (!search || !list || !gui) return;
            (void)gui->focus_control(list, true);
            // Dは検索Shellを開く前に親の一覧をアクティブ化する。フォーカス操
            // 作が、このモードレスダイアログを同期的に閉じたり終了させたりし得る。
            if (search_lifetime.expired() || !isOpen()) return;
            if (context_.services.gui != gui || search != incremental_search_ ||
                list != start_control_) return;
            search->startIncSearch();
        } else if (command == "open-event-view") {
            openSelected();
        }
    };
    start_control_->on_pointer = [this](lib::PointerEvent& event) {
        if (event.button != lib::PointerButton::Secondary ||
            event.action != lib::PointerAction::Up || !context_.services.gui ||
            !context_menu_ || !start_control_) return;
        if (context_menu_->on_open) context_menu_->on_open();
        context_.services.gui->show_context_menu(context_menu_, start_control_,
                                                  event.position);
        event.handled = true;
    };

    refreshStarts({}, true);
    if (context_.editor_commons) {
        ref_content_connection_ = context_.editor_commons->refContent.add(
            [this](const ContentPtr&) { refreshStarts(); });
        del_content_connection_ = context_.editor_commons->delContent.add(
            [this](const ContentPtr& deleted) {
                if ((event_ && event_->isDescendant(deleted)) ||
                    (parent_content_ && parent_content_->isDescendant(deleted)))
                    return;
                refreshStarts(deleted);
            });
        repl_text_connection_ = context_.editor_commons->replText.add(
            [this] { refreshStarts(); });
    }
    // Dは対応するSummaryがない場合、Startの選択を無効にする。
    if (!summary_) start_control_->set_enabled(false);
    start_control_->set_layout_item({.row = 0U, .column = 0U,
                                     .preferred = {300.0F, 255.0F}, .grow = 1.0F});
    root->add(start_control_);
    installTypedBody(root, {325.0F, 345.0F}, true);
    if (context_.editor_props) bindDialogGeometry(context_.editor_props->flex().selEvtDlg);
}

template <CType Type>
StartSelectDialog<Type>::~StartSelectDialog() {
    start_lifetime_.reset();
    if (!context_.editor_commons) return;
    context_.editor_commons->refContent.remove(ref_content_connection_);
    context_.editor_commons->delContent.remove(del_content_connection_);
    context_.editor_commons->replText.remove(repl_text_connection_);
}

template <CType Type>
bool StartSelectDialog<Type>::hasStarts() const {
    const auto tree = parent_content_ ? parent_content_->tree() : EventTreePtr{};
    return tree && !tree->starts().empty();
}

template <CType Type>
void StartSelectDialog<Type>::refreshStarts(const ContentPtr& deleted,
                                           const bool initial) {
    if (!start_control_) return;
    const auto order_revision = start_control_->item_order_revision;
    const auto old_selected = selected_;
    std::vector<lib::ListItem> items;
    std::vector<ContentPtr> starts;
    std::vector<std::size_t> selection;
    if (const auto tree = parent_content_ ? parent_content_->tree() : EventTreePtr{}) {
        for (const auto& start : tree->starts()) {
            if (!start || start == deleted ||
                (incremental_search_ && !incremental_search_->match(start->name())))
                continue;
            if (start->name() == old_selected) selection = {items.size()};
            items.push_back({start->name(), start->name(),
                             context_.contentImage
                                 ? context_.contentImage(CType::Start)
                                 : ResourceId{}});
            starts.push_back(start);
        }
    }
    if (initial && selection.empty() && !items.empty() &&
        (!incremental_search_ || !incremental_search_->isSearching())) {
        selection = {0U};
        if (starts.front()) selected_ = starts.front()->name();
    } else if (deleted && deleted->name() == old_selected && !starts.empty() &&
               starts.front()) {
        selection = {0U};
        selected_ = starts.front()->name();
    }
    const std::weak_ptr<lib::ListView> weak = start_control_;
    if (!lib::replace_list_items(start_control_, std::move(items), selection,
            selection.empty() ? std::optional<std::size_t>{} : selection.front(),
            [weak, order_revision] {
                const auto target = weak.lock();
                return target && target->item_order_revision == order_revision;
            })) return;
    visible_starts_ = std::move(starts);
}

template <CType Type>
void StartSelectDialog<Type>::openSelected() {
    if (!start_control_ || start_control_->selection.empty()) return;
    const auto index = start_control_->selection.front();
    if (index >= visible_starts_.size() || !visible_starts_[index]) return;
    try {
        (void)openExternalCWXPath(path::cpaddattr(visible_starts_[index]->cwxPath(true),
                                         "shallow"),
                          false);
    } catch (const std::exception& error) {
        // DはEventViewへの移動失敗を捕捉・記録し、ダイアログを維持する。
        (void)utils::printStackTrace();
        utils::debugln(error.what());
    }
}

template void StartSelectDialog<CType::LinkStart>::buildControls();
template void StartSelectDialog<CType::CallStart>::buildControls();
template StartSelectDialog<CType::LinkStart>::~StartSelectDialog();
template StartSelectDialog<CType::CallStart>::~StartSelectDialog();
template bool StartSelectDialog<CType::LinkStart>::hasStarts() const;
template bool StartSelectDialog<CType::CallStart>::hasStarts() const;
template void StartSelectDialog<CType::LinkStart>::refreshStarts(const ContentPtr&, bool);
template void StartSelectDialog<CType::CallStart>::refreshStarts(const ContentPtr&, bool);
template void StartSelectDialog<CType::LinkStart>::openSelected();
template void StartSelectDialog<CType::CallStart>::openSelected();

void ClearEventDialog::buildControls() {
    auto root = makeGridPanel(1U, 4.0F, {5.0F, 5.0F, 5.0F, 5.0F});
    const auto& messages = contentDialogMessages(context_);
    auto group = makeGroupBox(messages.afterClear.value, 1U, 2.0F);
    const auto add_choice = [this, group](String text, const bool value,
                                          const std::size_t row) {
        auto radio = std::make_shared<lib::RadioButton>(std::move(text));
        radio->group = "after-clear";
        radio->checked = complete_ == value;
        radio->on_changed = [this, value](const bool checked) {
            if (!checked) return;
            complete_ = value;
            setApplyEnabled(true);
        };
        radio->set_layout_item({.row = row, .column = 0U,
                                .preferred = {245.0F, 24.0F}});
        group->add(radio);
    };
    add_choice(messages.afterClearEndMark.value, true, 0U);
    add_choice(messages.afterClearNoEndMark.value, false, 1U);
    root->add(group);
    installTypedBody(root, {350.0F, 165.0F}, true);
}

template <CType Type>
void NumericEventDialog<Type>::buildControls() {
    auto root = makeGridPanel(1U, 3.0F, {4.0F, 4.0F, 4.0F, 4.0F});
    const auto& messages = contentDialogMessages(context_);
    String title = messages.moneyName.value;
    if constexpr (Type == CType::Wait) {
        title = messages.waitName.value;
        minimum_ = 0;
        maximum_ = context_.vars ? context_.vars->waitMax.value : 1000;
    } else if constexpr (Type == CType::BranchRandom) {
        title = messages.randomName.value; minimum_ = 0; maximum_ = 100;
    } else if constexpr (Type == CType::BranchPartyNumber) {
        title = messages.partyNumName.value; minimum_ = 1;
        maximum_ = context_.vars ? context_.vars->partyMax.value : 6;
    } else if constexpr (Type == CType::BranchMoney || Type == CType::GetMoney || Type == CType::LoseMoney) {
        // C++の所持金拡張。金額イベントをカード価格の設定上限で制限し
        // ない。カード価格と旧形式への出力は、それぞれの上限を維持する。
        minimum_ = 0;
        maximum_ = static_cast<decltype(maximum_)>(::cw::lib::money::maximum);
    } else if (maximum_ <= minimum_) {
        minimum_ = 0;
        maximum_ = context_.vars ? context_.vars->priceMax.value : 999999;
    }
    maximum_ = std::max(minimum_, maximum_);
    value_ = std::clamp(value_, minimum_, maximum_);
    std::vector<int> increments;
    if (maximum_ >= 10) {
        for (int amount : {5, 10, 100, 1000})
            if (amount <= maximum_) increments.push_back(amount);
    }
    const bool horizontal = lib::editor_window_host_options().all_mode;
    const auto columns = 2U + increments.size() * (horizontal ? 2U : 1U);
    auto group = makeGroupBox(title, 1U);
    auto row = makeGridPanel(columns, 4.0F, {});
    auto number = std::make_shared<lib::NumberEdit>();
    number->minimum = minimum_; number->maximum = maximum_; number->value = value_;
    number->on_changed = [this](double value) {
        value_ = static_cast<int>(value);
        setApplyEnabled(true);
    };
    number->set_layout_item({.row = 0U, .column = 0U,
                             .preferred = {70.0F, 20.0F}});
    row->add(number);
    for (std::size_t i = 0; i < increments.size(); ++i) {
        for (std::size_t sign = 0; sign < 2; ++sign) {
            const int amount = sign == 0 ? increments[i] : -increments[i];
            auto button = std::make_shared<lib::Button>(
                amount > 0 ? "+" + std::to_string(amount) : std::to_string(amount));
            button->on_click = [this, number, amount] {
                const auto next = static_cast<std::int64_t>(value_) + amount;
                value_ = static_cast<int>(std::clamp<std::int64_t>(next, minimum_, maximum_));
                number->value = value_;
                setApplyEnabled(true);
            };
            button->set_layout_item({
                .row = horizontal ? 0U : sign,
                .column = 1U + (horizontal ? i * 2U + sign : i),
                .preferred = {42.0F, 20.0F}});
            row->add(button);
        }
    }
    addLabel(row, utils::tryFormat(messages.rangeHint.value, minimum_, maximum_),
             0U, columns - 1U, 1U, {90.0F, 23.0F});
    if constexpr (Type == CType::BranchPartyNumber) {
        // ALLの人数判定分岐では、数値欄と範囲ラベルをグループ内
        // で横方向に中央配置する。自然幅を使い、引き伸ばさない。
        if (horizontal) {
            auto item = row->layout_item();
            item.horizontal = lib::Align::Center;
            item.grow = 0.0F;
            row->set_layout_item(item);
        }
    }
    group->add(row); root->add(group);
    installTypedBody(root, increments.empty() ? lib::Size{328.0F, 132.0F}
                                             : lib::Size{330.0F, 154.0F}, true);
}

template void NumericEventDialog<CType::Wait>::buildControls();
template void NumericEventDialog<CType::BranchRandom>::buildControls();
template void NumericEventDialog<CType::BranchPartyNumber>::buildControls();
template void NumericEventDialog<CType::BranchMoney>::buildControls();
template void NumericEventDialog<CType::GetMoney>::buildControls();
template void NumericEventDialog<CType::LoseMoney>::buildControls();

namespace {

const std::array<std::pair<types::Status, const char*>, 28U> kStatusChoices{{
    {types::Status::Active, "行動可能"}, {types::Status::Inactive, "行動不可"},
    {types::Status::Alive, "生存"}, {types::Status::Dead, "非生存"},
    {types::Status::Fine, "健康"}, {types::Status::Injured, "負傷"},
    {types::Status::HeavyInjured, "重傷"}, {types::Status::Unconscious, "意識不明"},
    {types::Status::Poison, "中毒"}, {types::Status::Sleep, "眠り"},
    {types::Status::Bind, "呪縛"}, {types::Status::Paralyze, "麻痺/石化"},
    {types::Status::Confuse, "混乱"}, {types::Status::Overheat, "激昂"},
    {types::Status::Brave, "勇敢"}, {types::Status::Panic, "恐慌"},
    {types::Status::Silence, "沈黙"}, {types::Status::FaceUp, "暴露"},
    {types::Status::AntiMagic, "魔法無効化"}, {types::Status::UpAction, "行動力上昇"},
    {types::Status::UpAvoid, "回避力上昇"}, {types::Status::UpResist, "抵抗力上昇"},
    {types::Status::UpDefense, "防御力上昇"}, {types::Status::DownAction, "行動力低下"},
    {types::Status::DownAvoid, "回避力低下"}, {types::Status::DownResist, "抵抗力低下"},
    {types::Status::DownDefense, "防御力低下"}, {types::Status::None, "指定なし"}
}};

std::shared_ptr<lib::GroupBox> makeRangeRadios(
    const String& title, const std::vector<std::pair<types::Range, String>>& choices,
    const types::Range selected, const std::function<void(types::Range)>& changed,
    const std::size_t columns = 1U) {
    auto group = makeGroupBox(title, columns);
    for (std::size_t index = 0U; index < choices.size(); ++index) {
        auto radio = std::make_shared<lib::RadioButton>(choices[index].second);
        radio->group = "range";
        radio->checked = choices[index].first == selected;
        const auto value = choices[index].first;
        radio->on_changed = [changed, value](const bool checked) {
            if (checked && changed) changed(value);
        };
        radio->set_layout_item({.row = index / columns, .column = index % columns,
                                .preferred = {128.0F, 22.0F}});
        group->add(radio);
    }
    return group;
}

std::shared_ptr<lib::GroupBox> makeStatusRadios(
    const Context& context, const types::Status selected,
    const std::function<void(types::Status)>& changed,
    const bool enabled = true) {
    const auto& messages = contentDialogMessages(context);
    auto group = makeGroupBox(messages.judgeState.value, 4U);
    for (std::size_t index = 0U; index < kStatusChoices.size() - 1U; ++index) {
        const auto [value, unused_text] = kStatusChoices[index];
        (void)unused_text;
        auto radio = std::make_shared<lib::RadioButton>(messages.statusName(value));
        radio->group = "status";
        radio->checked = value == selected;
        radio->set_enabled(enabled);
        radio->on_changed = [changed, value](const bool checked) {
            if (checked && changed) changed(value);
        };
        radio->set_layout_item({.row = index / 4U, .column = index % 4U,
                                .preferred = {84.0F, 22.0F}});
        group->add(radio);
    }
    return group;
}

const msgs::Msgs& contentDialogMessages(const Context& context) {
    if (context.prop) return context.prop->requiredMsgs();
    static const msgs::Msgs defaults;
    return defaults;
}

std::shared_ptr<lib::GroupBox> makeStatusHint(const Context& context) {
    const auto& messages = contentDialogMessages(context);
    auto group = makeGroupBox(messages.stateHint.value, 1U);
    for (const auto& text : {messages.statusActive.value, messages.statusInactive.value,
                            messages.statusAlive.value, messages.statusDead.value}) {
        group->add(std::make_shared<lib::Label>(text));
    }
    return group;
}

} // namespace

void BrLevelDialog::buildControls() {
    auto root = makeGridPanel(2U, 5.0F, {5.0F, 5.0F, 5.0F, 5.0F});
    const auto& messages = contentDialogMessages(context_);
    auto target = makeGroupBox(messages.judgeTarget.value, 1U);
    for (const auto& [text, average] : std::array<std::pair<String, bool>, 2U>{{
             {messages.selectedLevel.value, false}, {messages.allMemberLevel.value, true}}}) {
        auto radio = std::make_shared<lib::RadioButton>(text);
        radio->group = "level-target";
        radio->checked = average_ == average;
        radio->on_changed = [this, average](const bool checked) {
            if (checked) { average_ = average; setApplyEnabled(true); }
        };
        target->add(radio);
    }
    root->add(target);
    auto level = makeGroupBox(messages.judgeLevel.value, 2U);
    auto number = std::make_shared<lib::NumberEdit>();
    const auto level_maximum = context_.vars
        ? static_cast<int>(context_.vars->castLevelMax.value) : 99;
    unsigned_level_ = std::clamp(unsigned_level_, 1, std::max(1, level_maximum));
    number->minimum = 1; number->maximum = level_maximum; number->value = unsigned_level_;
    number->on_changed = [this](const double value) {
        unsigned_level_ = static_cast<int>(value); setApplyEnabled(true);
    };
    auto range_label = std::make_shared<lib::Label>(
        utils::tryFormat(messages.rangeHint.value, 1, level_maximum));
    if (lib::editor_window_host_options().all_mode) {
        // ALLではレベル欄と範囲ラベルを「判
        // 定レベル」グループの縦横中央へ置く。
        auto centred = makeGridPanel(2U, 4.0F, {});
        centred->set_layout_item({.row = 0U, .column = 0U, .column_span = 2U,
                                  .horizontal = lib::Align::Center,
                                  .vertical = lib::Align::Center,
                                  .grow = 1.0F});
        centred->add(number);
        centred->add(range_label);
        level->add(centred);
    } else {
        level->add(number);
        level->add(range_label);
    }
    root->add(level);
    installTypedBody(root, {328.0F, 150.0F}, true);
}

void BrStateDialog::buildControls() {
    const bool legacy = summary_ && summary_->legacy();
    const auto& messages = contentDialogMessages(context_);
    auto root = makeGridPanel(2U, 5.0F, {5.0F, 5.0F, 5.0F, 5.0F});
    auto range = makeRangeRadios(messages.judgeTarget.value, {
        {types::Range::Selected, messages.rangeName(types::Range::Selected)},
        {types::Range::Random, messages.rangeName(types::Range::Random)},
        {types::Range::Party, messages.rangeName(types::Range::Party)}},
        range_.range(), [this](const auto value) { range_.setRange(value); setApplyEnabled(true); });
    range->set_layout_item({.row = 0U, .column = 0U,
                            .preferred = {174.0F, 88.0F},
                            .constrain_minimum_to_preferred = true});
    root->add(range);
    auto result = makeGroupBox(messages.resultType.value, 1U);
    auto combo = std::make_shared<lib::ComboBox>();
    combo->items = {{messages.resultTypeNormalForStatus.value, "normal"},
                    {messages.resultTypeInvertForStatus.value, "invert"}};
    combo->selected_index = invert_result_ ? 1U : 0U;
    combo->set_enabled(!legacy || invert_result_);
    version_control_updates_.push_back([this, combo] {
        combo->set_enabled(!summary_ || !summary_->legacy() || invert_result_);
    });
    combo->on_selection_changed = [this, combo](const auto index) {
        if (index) {
            invert_result_ = *index == 1U;
            combo->set_enabled(!summary_ || !summary_->legacy() || invert_result_);
            setApplyEnabled(true);
        }
    };
    result->set_layout_item({.row = 0U, .column = 1U,
                             .preferred = {171.0F, 88.0F},
                             .constrain_minimum_to_preferred = true});
    result->add(combo); root->add(result);
    auto statuses = makeStatusRadios(context_, status_, [this](const auto value) {
        status_ = value; setApplyEnabled(true);
    });
    statuses->set_layout_item({.row = 1U, .column = 0U, .column_span = 2U,
                               .preferred = {382.0F, 160.0F},
                               .constrain_minimum_to_preferred = true});
    root->add(statuses);
    auto hint = makeStatusHint(context_);
    hint->set_layout_item({.row = 2U, .column = 0U, .column_span = 2U,
                           .preferred = {350.0F, 105.0F},
                           .constrain_minimum_to_preferred = true});
    root->add(hint);
    installTypedBody(root, {365.0F, 450.0F}, true);
}

void BrStateDialog::refreshWarning() {
    replaceWarnings(branchLocalWarnings(
        context_, summary_, true, status_, invert_result_));
}

template <CType Type, typename F, bool SelValue>
void FlagStepDialog<Type, F, SelValue>::refreshWarning() {
    // DのFlagStepDialogが扱うのは、これらの局所的な警告だけ。シナリオ全体のモデル診
    // 断は、warning::warningsのScenarioValidation経路に置く。
    WarningList result;
    if constexpr (Type == CType::CheckStep) {
        if (context_.prop && !targetVersion("1.50")) {
            const auto& messages = context_.prop->requiredMsgs();
            result.push_back(utils::tryFormat(
                messages.warningUnknownContent.value,
                messages.contentName(CType::CheckStep), "1.50"));
        }
    }
    if constexpr (Type == CType::SetFlag || Type == CType::ReverseFlag) {
        if (animation_control_) {
            const auto animationWarnings = animation_control_->warnings();
            result.insert(result.end(), animationWarnings.begin(),
                          animationWarnings.end());
        }
    }
    replaceWarnings(std::move(result));
}

template <CType Type, typename F, bool SelValue>
void FlagStepDialog<Type, F, SelValue>::refreshFlagValues() {
    if constexpr (Type == CType::CheckStep) {
        if (step_comparison_label_) {
            String text;
            if (summary_ && context_.prop) if (const auto step = flag::findVar<flag::Step>(
                    summary_->flagDirRoot(), use_counter_, selected_)) {
                const auto count = step->count();
                if (count) text = utils::tryFormat(context_.prop->requiredMsgs().stepValueIs.value,
                    step->path(), step->getValue(static_cast<std::uint32_t>(std::clamp(
                        selected_value_, 0, static_cast<int>(count - 1)))));
            }
            step_comparison_label_->text = std::move(text);
        }
    }
    if (!flag_value_control_) return;
    std::vector<lib::TableRow> rows;
    std::vector<std::size_t> selection;
    constexpr bool flag_type = Type == CType::BranchFlag ||
                               Type == CType::SetFlag ||
                               Type == CType::ReverseFlag ||
                               Type == CType::CheckFlag;
    if constexpr (flag_type) {
        bool found_flag = false;
        String on;
        String off;
        if (summary_) {
            if (const auto value = flag::findVar<flag::Flag>(
                    summary_->flagDirRoot(), use_counter_, selected_)) {
                on = value->on();
                off = value->off();
                found_flag = true;
            }
        }
        rows = {{{{std::move(on)}}}, {{{std::move(off)}}}};
        if (!found_flag) rows.clear();
        if constexpr (SelValue)
            if (found_flag)
            selection = {
                selected_value_ != 0 ? std::size_t{0} : std::size_t{1}};
    } else {
        if (summary_) {
            if (const auto value = flag::findVar<flag::Step>(
                    summary_->flagDirRoot(), use_counter_, selected_)) {
                const auto values = value->values();
                for (std::size_t index = 0U; index < values.size(); ++index)
                    rows.push_back(
                        {{{values[index], std::to_string(index)}}});
            }
        }
        if constexpr (SelValue) {
            if (!rows.empty()) {
                selected_value_ = std::clamp(selected_value_, 0,
                                             static_cast<int>(rows.size() - 1));
                selection = {static_cast<std::size_t>(selected_value_)};
            }
        }
    }
    if (!lib::replace_table_rows(flag_value_control_, std::move(rows),
                                 std::move(selection))) return;
    if (context_.services.gui)
        context_.services.gui->request_redraw(flag_value_control_);
}

template <CType Type, typename F, bool SelValue>
void FlagStepDialog<Type, F, SelValue>::refExternalFlagAndStep() {
    if (!isOpen() || variables_retired_) return;
    // DはシナリオのSummaryがある場合だけ、FlagStepDialog
    // を変数通知へ接続する。共通C++のEventDialogの購読は、意図的
    // にパスのないSummaryを使う設定の初期値編集部にも届くため区別する。
    if (!summary_ || summary_->scenarioPath().empty()) return;
    if (flag_chooser_) {
        selected_ = flag_chooser_->selected();
        selected_with_dir_ = flag_chooser_->selectedWithDir();
    }
    // 同じパスの選択部の更新はModifyを通知しない。選択したFlag/Step
    // の値を保ちながら、値ラベルを独立に再構築する。
    refreshFlagValues();
}

template <CType Type, typename F, bool SelValue>
void FlagStepDialog<Type, F, SelValue>::delExternalFlagAndStep(
    const bool flagDeleted, const bool stepDeleted) {
    if (!isOpen() || variables_retired_) return;
    // DのdelFSは、削除通知にこのダイアログの変数種別が含まれなければ戻る。
    constexpr bool flag_type = Type == CType::BranchFlag || Type == CType::SetFlag ||
        Type == CType::ReverseFlag || Type == CType::CheckFlag;
    if constexpr (flag_type) {
        if (!flagDeleted) return;
    } else if (!stepDeleted) {
        return;
    }
    // パスのない初期値編集ホストでは、DのscenarioPath条件付きの購読
    // に合わせる。無関係なMainWindowの変数削除で編集を終了させない。
    if (!summary_ || summary_->scenarioPath().empty()) return;
    const auto root = summary_ ? summary_->flagDirRoot() : flag::FlagDirPtr{};
    const bool available = flag_type ? !flag::allVars<flag::Flag>(root, use_counter_).empty()
                                    : !flag::allVars<flag::Step>(root, use_counter_).empty();
    if (!available) {
        variables_retired_ = true;
        forceCancel();
        return;
    }
    refExternalFlagAndStep();
}

template <CType Type, typename F, bool SelValue>
void FlagStepDialog<Type, F, SelValue>::buildControls() {
    constexpr bool flag_type = Type == CType::BranchFlag ||
                               Type == CType::SetFlag ||
                               Type == CType::ReverseFlag ||
                               Type == CType::CheckFlag;
    auto root = makeGridPanel(1U, 5.0F, {3.0F, 3.0F, 3.0F, 3.0F});
    flag_split_ = std::make_shared<lib::SplitView>();
    flag_split_->orientation = lib::Orientation::Horizontal;
    flag_split_->weights = context_.vars
        ? std::vector<float>{
              static_cast<float>(context_.vars->flagEventSashL.value),
              static_cast<float>(context_.vars->flagEventSashR.value)}
        : std::vector<float>{3.0F, 2.0F};
    flag_split_->on_weights_changed = [this](const std::vector<float>& weights) {
        if (!context_.vars || weights.size() < 2U) return;
        context_.vars->flagEventSashL.value =
            static_cast<std::int32_t>(std::lround(weights[0]));
        context_.vars->flagEventSashR.value =
            static_cast<std::int32_t>(std::lround(weights[1]));
    };
    flag_split_->set_layout_item({.row = 0, .column = 0,
                                  .preferred = {298.0F, 265.0F}, .grow = 1.0F});
    root->add(flag_split_);

    auto left = makeGridPanel(1U, 4.0F, {});
    addVariableHeading(left, flag_type
                       ? (context_.prop ? context_.prop->requiredMsgs().flag.value : "フラグ")
                       : (context_.prop ? context_.prop->requiredMsgs().step.value : "ステップ"),
             context_.editor_props ? (flag_type ? context_.editor_props->images().flag()
                 : context_.editor_props->images().step()) : lib::ResourceId{});
    flag_split_->add(left);
    auto right = makeGridPanel(1U, 4.0F, {});
    addLabel(right,
             flag_type
                 ? (context_.prop ? context_.prop->requiredMsgs().flagValue.value : "値")
                 : (context_.prop ? context_.prop->requiredMsgs().stepValue.value : "段階"),
             0, 0, 1U, {88.0F, 19.0F});
    flag_split_->add(right);

    auto chooser_host = makeGridPanel(1U, 0.0F, {});
    chooser_host->set_layout_item({.row = 1, .column = 0,
                                   .preferred = {205.0F, 238.0F},
                                   .grow = 1.0F});
    left->add(chooser_host);
    auto properties = chooserProperties(
        context_, [this](const StringView path_value, const bool activate) {
            (void)openExternalCWXPath(String(path_value), activate);
        });
    flag_chooser_ = std::make_unique<chooser::FlagChooserBase>(
        chooserCommons(context_), summary_, use_counter_, chooser_host.get(),
        true, false, false,
        flag_type ? chooser::VariableKind::Flag : chooser::VariableKind::Step,
        std::move(properties));
    flag_chooser_->selected(selected_);
    // 更新・検索はmodEventだけを公開する。絞り込みで隠
    // れても指定参照を同期し、保存済みの値はリセットしない。
    flag_chooser_->modEvent.push_back([this] {
        selected_ = flag_chooser_->selected();
        setApplyEnabled(true);
    });
    flag_chooser_->modEventWithSame.push_back([this] {
        const auto next = flag_chooser_ ? flag_chooser_->selected() : selected_;
        const auto with_dir = flag_chooser_ ? flag_chooser_->selectedWithDir() : selected_with_dir_;
        if constexpr (SelValue) {
            // Dは保持した葉ではなくselectedWithDirを比較する。フ
            // ォルダークリックでは現在の値をリセットせず、ラベルを再構築する。
            if (with_dir == selected_with_dir_) {
                const auto first = flag_type ? 1 : 0;
                if (selected_value_ != first) {
                    selected_value_ = first;
                    setApplyEnabled(true);
                }
            }
        }
        selected_with_dir_ = with_dir;
        selected_ = next;
        refreshFlagValues();
    });
    // 初期の参照先欠落の代替は、既存コンテントにも適用する。D
    // は代替参照を採用してから保存済みFlag/Stepの値
    // を戻す。新規コンテントは最初の行の宣言の既定値を持つ。
    selected_ = flag_chooser_->selected();
    selected_with_dir_ = flag_chooser_->selectedWithDir();
    if (!chooser_host->children().empty()) {
        if (const auto chooser_panel = std::dynamic_pointer_cast<lib::Container>(
                chooser_host->children().front())) {
            auto layout = std::make_shared<lib::GridLayout>();
            layout->columns = 1U;
            layout->spacing = 4.0F;
            chooser_panel->set_layout(layout);
            if (flag_chooser_->tree())
                flag_chooser_->tree()->set_layout_item(
                    {.row = 0, .column = 0, .preferred = {205.0F, 205.0F},
                     .grow = 1.0F});
            if (flag_chooser_->allExpandedButton())
                flag_chooser_->allExpandedButton()->set_layout_item(
                    {.row = 1, .column = 0, .preferred = {205.0F, 24.0F}});
        }
    }

    flag_value_control_ = std::make_shared<lib::TableView>();
    flag_value_control_->set_enabled(SelValue && summary_ && !summary_->scenarioPath().empty());
    flag_value_control_->columns = {{String{}, 82.0F}};
    flag_value_control_->header_visible = false;
    flag_value_control_->on_selection_changed = [this](const auto& selection) {
        if (selection.empty()) return;
        if constexpr (SelValue) {
            if constexpr (flag_type)
                selected_value_ = selection.front() == 0U ? 1 : 0;
            else
                selected_value_ = static_cast<int>(selection.front());
        }
        refreshFlagValues();
        setApplyEnabled(true);
    };
    flag_value_control_->set_layout_item({.row = 1, .column = 0,
                                          .preferred = {88.0F, 238.0F},
                                          .grow = 1.0F});
    right->add(flag_value_control_);
    refreshFlagValues();

    if constexpr (Type == CType::CheckStep) {
        step_comparison_label_ = std::make_shared<lib::Label>();
        step_comparison_label_->set_layout_item({.row = 1, .column = 0});
        root->add(step_comparison_label_);
        auto comparison = std::make_shared<lib::ComboBox>();
        comparison->editable = false;
        comparison->items = {{context_.prop ? context_.prop->requiredMsgs().comparison4Name(types::Comparison4::Eq) : "であれば", "="},
                             {context_.prop ? context_.prop->requiredMsgs().comparison4Name(types::Comparison4::Ne) : "でなければ", "<>"},
                             {context_.prop ? context_.prop->requiredMsgs().comparison4Name(types::Comparison4::Lt) : "より大きければ", "<"},
                             {context_.prop ? context_.prop->requiredMsgs().comparison4Name(types::Comparison4::Gt) : "より小さければ", ">"}};
        comparison->selected_index = static_cast<std::size_t>(comparison_);
        comparison->on_selection_changed = [this](const auto index) {
            if (!index || *index > 3U) return;
            comparison_ = static_cast<types::Comparison4>(*index);
            setApplyEnabled(true);
        };
        comparison->set_layout_item({.row = 2U, .column = 0U,
                                     .preferred = {298.0F, 24.0F}});
        root->add(comparison);
        refreshFlagValues();
    }

    if constexpr (Type == CType::SetFlag || Type == CType::ReverseFlag) {
        animation_control_ = std::make_shared<spcarddialog::CardAnimationPanel>(
            animationCommons(context_, summary_.get(), skin_.get()), summary_.get(),
            root, spcarddialog::CardAnimationPanelType::SetFlag);
        animation_control_->speed(card_speed_);
        animation_control_->overrideCardSpeed(override_card_speed_);
        animation_control_->widget()->set_layout_item(
            {.row = 1, .column = 0, .preferred = {330.0F, 48.0F},
             .constrain_minimum_to_preferred = true});
        version_control_updates_.push_back([this] { if (animation_control_) animation_control_->updateEnabled(); });
        animation_control_->modEvent.push_back([this] {
            if (!animation_control_) return;
            card_speed_ = animation_control_->speed();
            override_card_speed_ = animation_control_->overrideCardSpeed();
            setApplyEnabled(true);
            refreshWarning();
        });
    }
    lib::Size dialog_size{317.0F, 389.0F};
    if (context_.editor_props) {
        const auto& geometry = context_.editor_props->flex().flagEvtDlg;
        if (geometry.width() > 0 && geometry.height() > 0)
            dialog_size = {static_cast<float>(geometry.width()),
                           static_cast<float>(geometry.height())};
    }
    installTypedBody(root, dialog_size, true);
    widget_->minimum_size = lib::Size{300.0F, 320.0F};
    // ALLのフラグ変更・反転は最小幅と初期幅を100px
    // 広げ、「カード本体の設定より…」の行の切
    // れを防ぐ。フラグ変更（設定）はさらに200px
    // 、フラグ分岐は200px広げる。
    if constexpr (Type == CType::SetFlag)
        widenAllMinimumWidth(*widget_, 100.0F + 200.0F);
    else if constexpr (Type == CType::ReverseFlag)
        widenAllMinimumWidth(*widget_, 100.0F);
    else if constexpr (Type == CType::BranchFlag)
        widenAllMinimumWidth(*widget_, 200.0F);
    if constexpr (Type == CType::CheckStep) {
        if (lib::editor_window_host_options().all_mode) {
            // ALLのステップ判定は、十段階の値一覧を全て表示する
            // ため計測最小高より70px高くする。ALLのコンテン
            // トダイアログは最小サイズで開くため、初期高も同じ。
            const auto measured = lib::window_content_minimum_size(*widget_);
            const float scale = std::max(1.0F, lib::editor_ui_text_scale());
            widget_->minimum_size->height =
                std::max(widget_->minimum_size->height, measured.height / scale) + 70.0F;
        }
    }
    closeEvent.push_back([this] {
        if (!context_.editor_props || !widget_) return;
        const auto bounds = lib::authored_window_bounds(*widget_);
        auto& geometry = context_.editor_props->flex().flagEvtDlg;
        geometry.width(static_cast<std::int32_t>(std::lround(bounds.width)));
        geometry.height(static_cast<std::int32_t>(std::lround(bounds.height)));
    });
}

template void FlagStepDialog<CType::SetFlag, void, true>::buildControls();
template void FlagStepDialog<CType::SetFlag, void, true>::refExternalFlagAndStep();
template void FlagStepDialog<CType::SetFlag, void, true>::delExternalFlagAndStep(bool, bool);
template void FlagStepDialog<CType::SetFlag, void, true>::refreshFlagValues();
template void FlagStepDialog<CType::BranchFlag, void, false>::buildControls();
template void FlagStepDialog<CType::BranchFlag, void, false>::refExternalFlagAndStep();
template void FlagStepDialog<CType::BranchFlag, void, false>::delExternalFlagAndStep(bool, bool);
template void FlagStepDialog<CType::BranchFlag, void, false>::refreshFlagValues();
template void FlagStepDialog<CType::ReverseFlag, void, false>::buildControls();
template void FlagStepDialog<CType::ReverseFlag, void, false>::refExternalFlagAndStep();
template void FlagStepDialog<CType::ReverseFlag, void, false>::delExternalFlagAndStep(bool, bool);
template void FlagStepDialog<CType::ReverseFlag, void, false>::refreshFlagValues();
template void FlagStepDialog<CType::CheckFlag, void, false>::buildControls();
template void FlagStepDialog<CType::CheckFlag, void, false>::refExternalFlagAndStep();
template void FlagStepDialog<CType::CheckFlag, void, false>::delExternalFlagAndStep(bool, bool);
template void FlagStepDialog<CType::CheckFlag, void, false>::refreshFlagValues();
template void FlagStepDialog<CType::BranchMultiStep, void, false>::buildControls();
template void FlagStepDialog<CType::BranchMultiStep, void, false>::refExternalFlagAndStep();
template void FlagStepDialog<CType::BranchMultiStep, void, false>::delExternalFlagAndStep(bool, bool);
template void FlagStepDialog<CType::BranchMultiStep, void, false>::refreshFlagValues();
template void FlagStepDialog<CType::BranchStep, void, true>::buildControls();
template void FlagStepDialog<CType::BranchStep, void, true>::refExternalFlagAndStep();
template void FlagStepDialog<CType::BranchStep, void, true>::delExternalFlagAndStep(bool, bool);
template void FlagStepDialog<CType::BranchStep, void, true>::refreshFlagValues();
template void FlagStepDialog<CType::SetStep, void, true>::buildControls();
template void FlagStepDialog<CType::SetStep, void, true>::refExternalFlagAndStep();
template void FlagStepDialog<CType::SetStep, void, true>::delExternalFlagAndStep(bool, bool);
template void FlagStepDialog<CType::SetStep, void, true>::refreshFlagValues();
template void FlagStepDialog<CType::SetStepUp, void, false>::buildControls();
template void FlagStepDialog<CType::SetStepUp, void, false>::refExternalFlagAndStep();
template void FlagStepDialog<CType::SetStepUp, void, false>::delExternalFlagAndStep(bool, bool);
template void FlagStepDialog<CType::SetStepUp, void, false>::refreshFlagValues();
template void FlagStepDialog<CType::SetStepDown, void, false>::buildControls();
template void FlagStepDialog<CType::SetStepDown, void, false>::refExternalFlagAndStep();
template void FlagStepDialog<CType::SetStepDown, void, false>::delExternalFlagAndStep(bool, bool);
template void FlagStepDialog<CType::SetStepDown, void, false>::refreshFlagValues();
template void FlagStepDialog<CType::CheckStep, void, true>::buildControls();
template void FlagStepDialog<CType::CheckStep, void, true>::refExternalFlagAndStep();
template void FlagStepDialog<CType::CheckStep, void, true>::delExternalFlagAndStep(bool, bool);
template void FlagStepDialog<CType::CheckStep, void, true>::refreshFlagValues();
template void FlagStepDialog<CType::SetFlag, void, true>::refreshWarning();
template void FlagStepDialog<CType::BranchFlag, void, false>::refreshWarning();
template void FlagStepDialog<CType::ReverseFlag, void, false>::refreshWarning();
template void FlagStepDialog<CType::CheckFlag, void, false>::refreshWarning();
template void FlagStepDialog<CType::BranchMultiStep, void, false>::refreshWarning();
template void FlagStepDialog<CType::BranchStep, void, true>::refreshWarning();
template void FlagStepDialog<CType::SetStep, void, true>::refreshWarning();
template void FlagStepDialog<CType::SetStepUp, void, false>::refreshWarning();
template void FlagStepDialog<CType::SetStepDown, void, false>::refreshWarning();
template void FlagStepDialog<CType::CheckStep, void, true>::refreshWarning();

template <CType Type, typename F, bool Random>
void FlagStepCombiDialog<Type, F, Random>::refreshVariableValues(const bool first) {
    auto& table = first ? first_values_ : second_values_;
    if (!table) return;
    std::vector<lib::TableRow> rows;
    const auto& selected = first ? first_ : second_;
    constexpr bool flag_type = Type == CType::SubstituteFlag ||
                               Type == CType::BranchFlagCmp;
    if constexpr (flag_type) {
        String on = "TRUE";
        String off = "FALSE";
        if (summary_) {
            if (const auto value = flag::findVar<flag::Flag>(
                    summary_->flagDirRoot(), use_counter_, selected)) {
                on = value->on();
                off = value->off();
            }
        }
        rows = {{{{std::move(on)}}}, {{{std::move(off)}}}};
    } else if (summary_) {
        if (const auto value = flag::findVar<flag::Step>(
                summary_->flagDirRoot(), use_counter_, selected)) {
            const auto values = value->values();
            for (std::size_t index = 0U; index < values.size(); ++index)
                rows.push_back({{{values[index], std::to_string(index)}}});
        }
    }
    if (!lib::replace_table_rows(table, std::move(rows), {})) return;
    if (context_.services.gui) context_.services.gui->request_redraw(table);
}

template <CType Type, typename F, bool Random>
void FlagStepCombiDialog<Type, F, Random>::refExternalFlagAndStep() {
    if (!isOpen() || variables_retired_) return;
    if (first_chooser_) first_ = first_chooser_->selected();
    if (second_chooser_) second_ = second_chooser_->selected();
    refreshVariableValues(true);
    if (isOpen()) refreshVariableValues(false);
}

template <CType Type, typename F, bool Random>
void FlagStepCombiDialog<Type, F, Random>::delExternalFlagAndStep() {
    if (!isOpen() || variables_retired_) return;
    constexpr bool flag_type = Type == CType::SubstituteFlag || Type == CType::BranchFlagCmp;
    const auto root = summary_ ? summary_->flagDirRoot() : flag::FlagDirPtr{};
    const bool available = flag_type ? !flag::allVars<flag::Flag>(root, use_counter_).empty()
                                    : !flag::allVars<flag::Step>(root, use_counter_).empty();
    if (!available) {
        variables_retired_ = true;
        forceCancel();
        return;
    }
    refExternalFlagAndStep();
}

template <CType Type, typename F, bool Random>
void FlagStepCombiDialog<Type, F, Random>::buildControls() {
    constexpr bool flag_type = Type == CType::SubstituteFlag ||
                               Type == CType::BranchFlagCmp;
    constexpr bool comparison = Type == CType::BranchStepCmp ||
                                Type == CType::BranchFlagCmp;
    const auto kind = flag_type ? chooser::VariableKind::Flag
                                : chooser::VariableKind::Step;
    auto root = makeGridPanel(1U, 5.0F, {4.0F, 4.0F, 4.0F, 4.0F});
    variable_split_ = std::make_shared<lib::SplitView>();
    variable_split_->orientation = lib::Orientation::Horizontal;
    variable_split_->weights = context_.vars
        ? std::vector<float>{
              static_cast<float>(std::max(1, context_.vars->flagCombiSashL.value)),
              static_cast<float>(std::max(1, context_.vars->flagCombiSashR.value))}
        : std::vector<float>{1.0F, 1.0F};
    variable_split_->on_weights_changed = [this](const std::vector<float>& weights) {
        if (!context_.vars || weights.size() < 2U) return;
        context_.vars->flagCombiSashL.value = static_cast<std::int32_t>(
            std::max(1.0F, std::round(weights[0])));
        context_.vars->flagCombiSashR.value = static_cast<std::int32_t>(
            std::max(1.0F, std::round(weights[1])));
    };
    variable_split_->set_layout_item({.row=0U,.column=0U,
                                      .preferred={400.0F,275.0F},.grow=1.0F});
    root->add(variable_split_);

    const auto build_side = [this, kind](const bool first) {
        auto side = makeGridPanel(1U, 3.0F, {});
        side->set_layout_item({.preferred = {195.0F, 275.0F}, .grow = 1.0F});
        String label = first
            ? (comparison ? "ソース変数(比較元)" : "ソース変数(代入元)")
            : (comparison ? "ターゲット変数(比較先)" : "ターゲット変数(代入先)");
        if (context_.prop) {
            const auto& msgs = context_.prop->requiredMsgs();
            label = first ? (comparison ? msgs.cmpSource.value : msgs.substituteSource.value)
                          : (comparison ? msgs.cmpTarget.value : msgs.substituteTarget.value);
        }
        addVariableHeading(side, label, context_.editor_props
            ? (kind == chooser::VariableKind::Flag ? context_.editor_props->images().flag()
                 : context_.editor_props->images().step()) : lib::ResourceId{});
        auto host = makeGridPanel(1U, 0.0F, {});
        host->set_layout_item({.row = 1U, .column = 0U,
                               .preferred = {195.0F, 245.0F}, .grow = 1.0F});
        side->add(host);
        auto properties = chooserProperties(
            context_, [this](const StringView path, const bool activate) {
                (void)openExternalCWXPath(String(path), activate);
            });
        auto chooser_value = std::make_unique<chooser::FlagChooserBase>(
            chooserCommons(context_), summary_, use_counter_, host.get(), true,
            false, first && Random, kind, std::move(properties));
        chooser_value->selected(first ? first_ : second_);
        chooser_value->modEvent.push_back([this, first] {
            const auto* chooser_ptr = first ? first_chooser_.get()
                                            : second_chooser_.get();
            if (!chooser_ptr) return;
            if (first) first_ = chooser_ptr->selected();
            else second_ = chooser_ptr->selected();
            refreshVariableValues(first);
            setApplyEnabled(true);
        });
        // 選択部の代替である最初の葉を、実際の選択として採用する。
        if (first) first_ = chooser_value->selected();
        else second_ = chooser_value->selected();
        if (first) first_chooser_ = std::move(chooser_value);
        else second_chooser_ = std::move(chooser_value);
        variable_split_->add(side);
    };
    build_side(true);
    build_side(false);

    // DのFlagStepCombiDialogは、SubstituteFlagだ
    // けにCardAnimationPanelを設ける。SubstituteStep
    // は二つのステップ選択部だけを持ち、適用契約にもカード速度の項目はない。
    if constexpr (Type == CType::SubstituteFlag) {
        animation_control_ = std::make_shared<spcarddialog::CardAnimationPanel>(
            animationCommons(context_, summary_.get(), skin_.get()), summary_.get(),
            root, spcarddialog::CardAnimationPanelType::SetFlag);
        animation_control_->speed(card_speed_);
        animation_control_->overrideCardSpeed(override_card_speed_);
        animation_control_->widget()->set_layout_item(
            {.row = 1U, .column = 0U,
             .preferred = {520.0F, 48.0F},
             .constrain_minimum_to_preferred = true});
        version_control_updates_.push_back([this] { if (animation_control_) animation_control_->updateEnabled(); });
        animation_control_->modEvent.push_back([this] {
            if (!animation_control_) return;
            card_speed_ = animation_control_->speed();
            override_card_speed_ = animation_control_->overrideCardSpeed();
            setApplyEnabled(true);
        });
    }
    installTypedBody(root, {430.0F, 390.0F}, true);
    if (context_.editor_props) bindDialogGeometry(context_.editor_props->flex().flagCombiDlg);
    // ALLのフラグ代入は、最小幅・初期幅を150px（100pxと追加50px）広げる。
    if constexpr (Type == CType::SubstituteFlag) widenAllMinimumWidth(*widget_, 150.0F);
}

template void FlagStepCombiDialog<CType::SubstituteStep, void, true>::buildControls();
template void FlagStepCombiDialog<CType::SubstituteStep, void, true>::refExternalFlagAndStep();
template void FlagStepCombiDialog<CType::SubstituteStep, void, true>::delExternalFlagAndStep();
template void FlagStepCombiDialog<CType::SubstituteStep, void, true>::refreshVariableValues(bool);
template void FlagStepCombiDialog<CType::SubstituteFlag, void, true>::buildControls();
template void FlagStepCombiDialog<CType::SubstituteFlag, void, true>::refExternalFlagAndStep();
template void FlagStepCombiDialog<CType::SubstituteFlag, void, true>::delExternalFlagAndStep();
template void FlagStepCombiDialog<CType::SubstituteFlag, void, true>::refreshVariableValues(bool);
template void FlagStepCombiDialog<CType::BranchStepCmp, void, false>::buildControls();
template void FlagStepCombiDialog<CType::BranchStepCmp, void, false>::refExternalFlagAndStep();
template void FlagStepCombiDialog<CType::BranchStepCmp, void, false>::delExternalFlagAndStep();
template void FlagStepCombiDialog<CType::BranchStepCmp, void, false>::refreshVariableValues(bool);
template void FlagStepCombiDialog<CType::BranchFlagCmp, void, false>::buildControls();
template void FlagStepCombiDialog<CType::BranchFlagCmp, void, false>::refExternalFlagAndStep();
template void FlagStepCombiDialog<CType::BranchFlagCmp, void, false>::delExternalFlagAndStep();
template void FlagStepCombiDialog<CType::BranchFlagCmp, void, false>::refreshVariableValues(bool);

template <CType Type, typename F, bool Random>
void FlagStepCombiDialog<Type, F, Random>::refreshWarning() {
    WarningList result;
    if (context_.editor_props &&
        !context_.editor_props->targetVersion(summary_.get(), "1.30")) {
        const auto& messages = context_.editor_props->requiredMsgs();
        result.push_back(utils::tryFormat(
            messages.warningUnknownContent.value,
            messages.contentName(Type), "1.30"));
    }
    if constexpr (Type == CType::SubstituteStep) {
        if (context_.prop && context_.editor_props &&
            utils::icmp(first_, context_.prop->sys().selectedPlayerCardNumber()) == 0 &&
            !context_.editor_props->isTargetVersion(summary_.get(), "2")) {
            result.push_back(
                context_.editor_props->requiredMsgs().warningSelectedPlayerValue.value);
        }
    }
    if (animation_control_) {
        const auto& animation_warnings = animation_control_->warnings();
        result.insert(result.end(), animation_warnings.begin(),
                      animation_warnings.end());
    }
    replaceWarnings(std::move(result));
}
template void FlagStepCombiDialog<CType::SubstituteStep, void, true>::refreshWarning();
template void FlagStepCombiDialog<CType::SubstituteFlag, void, true>::refreshWarning();
template void FlagStepCombiDialog<CType::BranchStepCmp, void, false>::refreshWarning();
template void FlagStepCombiDialog<CType::BranchFlagCmp, void, false>::refreshWarning();

namespace {

[[nodiscard]] std::shared_ptr<lib::Panel> buildAudioSelectorBody(
    materialselect::MaterialSelectBase& selector, const bool bgm,
    const Context& context) {
    auto root = makeGridPanel(1U, 5.0F, {});
    const auto dirs = selector.createDirsCombo();
    std::shared_ptr<lib::Button> stop;
    if (!bgm) stop = selector.createStopButton();
    const auto play = selector.createPlayButton();
    const auto refresh = selector.createRefreshButton();
    const auto directory = selector.createDirectoryButton();
    const auto files = selector.createFileList();
    std::shared_ptr<timebar::TimeBar> playing;
    if (bgm) playing = selector.createPlayingBar();

    const auto main = selector.widget();
    auto main_layout = std::make_shared<lib::GridLayout>();
    main_layout->columns = bgm ? 4U : 5U;
    main_layout->spacing = 3.0F;
    main_layout->padding = {};
    main->set_layout(main_layout);
    dirs->set_layout_item({.row = 0, .column = 0,
                           .preferred = {bgm ? 222.0F : 187.0F, 24.0F},
                           .grow = 1.0F});
    std::size_t column = 1U;
    if (stop) {
        stop->text.clear();
        stop->accessibility_name = "停止";
        if (context.editor_props)
            stop->icon = context.editor_props->images().load(
                StringView{"sound_stop.png"});
        stop->set_enabled(true);
        stop->set_layout_item({.row = 0, .column = column++,
                               .preferred = {27.0F, 24.0F}});
    }
    play->text.clear();
    play->accessibility_name = "再生";
    if (context.editor_props)
        play->icon = context.editor_props->images().load(
            StringView{"sound_play.png"});
    play->set_layout_item({.row = 0, .column = column++,
                           .preferred = {27.0F, 24.0F}});
    refresh->accessibility_name = "更新";
    refresh->set_layout_item({.row = 0, .column = column++,
                              .preferred = {27.0F, 24.0F}});
    directory->accessibility_name = "フォルダを開く";
    directory->set_layout_item({.row = 0, .column = column,
                                .preferred = {27.0F, 24.0F}});
    files->set_layout_item({.row = 1, .column = 0,
                            .column_span = bgm ? 4U : 5U,
                            .preferred = {bgm ? 312.0F : 304.0F,
                                          bgm ? 250.0F : 239.0F},
                            .grow = 1.0F});
    if (playing) {
        playing->set_layout_item({.row = 2, .column = 0,
                                  .column_span = bgm ? 4U : 5U,
                                  .preferred = {312.0F, 28.0F}});
    }
    main->set_layout_item({.row = 0, .column = 0,
                           .preferred = {bgm ? 312.0F : 304.0F,
                                         bgm ? 303.0F : 268.0F},
                           .grow = 1.0F});
    root->add(main);

    const auto options = selector.createPlayingOptions({}, true, false);
    // MaterialSelectは、接続済みのチャンネル・音量・ループの三
    // 区画とラベルを所有済み。panel->children()からこの行
    // を再構築すると、区画パネルを操作部自身と誤認し、Ch.: Ch.:の
    // ようなラベル重複や、一つのウィジェットの二重の論理親を作ってしまう。
    options->set_layout_item({.row = 1, .column = 0,
                               .preferred = {bgm ? 312.0F : 304.0F, 43.0F}});
    root->add(options);

    const auto* messages = context.prop ? &context.prop->requiredMsgs() : nullptr;
    const auto fade = selector.createFadeIn();
    const auto fade_controls = fade->children();
    fade->clear();
    auto fade_layout = std::make_shared<lib::GridLayout>();
    fade_layout->columns = 3U;
    fade_layout->spacing = 4.0F;
    fade_layout->padding = {};
    fade->set_layout(fade_layout);
    addLabel(fade,
             messages ? messages->fadeIn.value + ":" : String{"フェードイン時間:"},
             0, 0, 1U, {112.0F, 23.0F});
    if (!fade_controls.empty()) {
        fade_controls.front()->set_layout_item({.row = 0, .column = 1,
                                                .preferred = {58.0F, 24.0F}});
        fade->add(fade_controls.front());
    }
    addLabel(fade,
             messages ? messages->fadeInHint.value : String{"× 0.1秒"},
             0, 2, 1U, {62.0F, 23.0F});
    fade->set_layout_item({.row = 2, .column = 0,
                           .preferred = {bgm ? 312.0F : 304.0F, 25.0F}});
    root->add(fade);
    return root;
}

[[nodiscard]] std::shared_ptr<lib::Panel> buildFallbackAudioBody(
    String& path, std::uint32_t& channel, std::uint32_t& volume,
    std::uint32_t& loop_count, std::uint32_t& fade_in, const bool bgm,
    const lib::Action& modified) {
    auto root = makeGridPanel(1U, 5.0F, {});
    auto picker = makeGridPanel(bgm ? 4U : 5U, 3.0F, {});
    auto dirs = std::make_shared<lib::ComboBox>();
    dirs->items.push_back({bgm ? "[BGM停止]" : "[指定無し]", ""});
    dirs->selected_index = 0U;
    dirs->set_layout_item({.row = 0, .column = 0,
                           .preferred = {bgm ? 222.0F : 187.0F, 24.0F}});
    picker->add(dirs);
    std::size_t column = 1U;
    if (!bgm) {
        auto stop = std::make_shared<lib::Button>("■");
        stop->accessibility_name = "停止";
        stop->set_layout_item({.row = 0, .column = column++,
                               .preferred = {27.0F, 24.0F}});
        picker->add(stop);
    }
    for (const auto& [text, name] :
         std::array<std::pair<String, String>, 3>{{{"▶", "再生"},
                                                   {"↻", "更新"},
                                                   {"▣", "フォルダを開く"}}}) {
        auto button = std::make_shared<lib::Button>(text);
        button->accessibility_name = name;
        button->set_layout_item({.row = 0, .column = column++,
                                 .preferred = {27.0F, 24.0F}});
        picker->add(button);
    }
    auto files = std::make_shared<lib::TableView>();
    files->columns = {{String{}, bgm ? 312.0F : 304.0F}};
    files->header_visible = false;
    if (!path.empty()) files->rows.push_back({{{path}}});
    files->set_layout_item({.row = 1, .column = 0,
                            .column_span = bgm ? 4U : 5U,
                            .preferred = {bgm ? 312.0F : 304.0F,
                                          bgm ? 250.0F : 239.0F},
                            .grow = 1.0F});
    picker->add(files);
    picker->set_layout_item({.row = 0, .column = 0, .grow = 1.0F});
    root->add(picker);

    auto options = makeGridPanel(8U, 3.0F, {});
    addLabel(options, "Ch:", 0, 0);
    auto channel_control = std::make_shared<lib::ComboBox>();
    channel_control->items = {{"主音声", "0"}, {"副音声", "1"}};
    channel_control->selected_index = std::min<std::size_t>(channel, 1U);
    channel_control->on_selection_changed = [&channel, modified](const auto value) {
        if (value) channel = static_cast<std::uint32_t>(*value);
        if (modified) modified();
    };
    channel_control->set_layout_item({.row = 0, .column = 1,
                                      .preferred = {70.0F, 24.0F}});
    options->add(channel_control);
    addLabel(options, "音量", 0, 2);
    auto volume_control = std::make_shared<lib::NumberEdit>();
    volume_control->minimum = 0; volume_control->maximum = 100;
    volume_control->value = volume;
    volume_control->on_changed = [&volume, modified](const double value) {
        volume = static_cast<std::uint32_t>(value);
        if (modified) modified();
    };
    volume_control->set_layout_item({.row = 0, .column = 3,
                                     .preferred = {47.0F, 24.0F}});
    options->add(volume_control);
    addLabel(options, "%", 0, 4);
    addLabel(options, "ループ", 0, 5);
    auto loop_control = std::make_shared<lib::NumberEdit>();
    loop_control->minimum = bgm ? 0 : 1; loop_control->maximum = 100;
    loop_control->value = loop_count;
    loop_control->on_changed = [&loop_count, modified](const double value) {
        loop_count = static_cast<std::uint32_t>(value);
        if (modified) modified();
    };
    loop_control->set_layout_item({.row = 0, .column = 6,
                                   .preferred = {46.0F, 24.0F}});
    options->add(loop_control);
    addLabel(options, bgm ? "(0=∞)" : "(1=1回)", 0, 7);
    root->add(options);

    auto fade = makeGridPanel(3U, 4.0F, {});
    addLabel(fade, "フェードイン", 0, 0);
    auto fade_control = std::make_shared<lib::NumberEdit>();
    fade_control->minimum = 0; fade_control->maximum = 1000;
    fade_control->value = fade_in / 100.0;
    fade_control->on_changed = [&fade_in, modified](const double value) {
        fade_in = static_cast<std::uint32_t>(value * 100.0);
        if (modified) modified();
    };
    fade_control->set_layout_item({.row = 0, .column = 1,
                                   .preferred = {58.0F, 24.0F}});
    fade->add(fade_control);
    addLabel(fade, "×0.1秒", 0, 2);
    root->add(fade);
    return root;
}

}  // namespace

BgmDialog::BgmDialog(Context context, SummaryPtr summary, Parent parent,
                     ContentPtr parentContent, ContentPtr content, SkinPtr skin,
                     UseCounterPtr useCounter)
    : EventDialog(std::move(context), std::move(summary), CType::PlayBgm,
                  std::move(parent), std::move(parentContent),
                  std::move(content), std::move(skin),
                  std::move(useCounter)) {
    if (draft_) {
        path_ = draft_->bgmPath();
        volume_ = draft_->bgmVolume();
        loop_count_ = draft_->bgmLoopCount();
        fade_in_ = draft_->bgmFadeIn();
        channel_ = draft_->bgmChannel();
    }
}

void BgmDialog::refSkin() {
    EventDialog::refSkin();
    if (selector_) selector_->refSkin();
}

void BgmDialog::refreshWarning() {
    replaceWarnings(selector_
        ? typedAudioWarnings(*selector_, context_, summary_)
        : WarningList{});
}

void BgmDialog::buildControls() {
    std::shared_ptr<lib::Panel> body;
    if (context_.editor_props) {
        const auto label = context_.prop
            ? context_.prop->requiredMsgs().defaultSelection(
                  context_.prop->requiredMsgs().bgmStop.value)
            : String{"[BGM停止]"};
        auto definitions = [label](const bool) {
            return std::vector<String>{label};
        };
        selector_ = std::make_unique<materialselect::MaterialSelect<
            materialselect::MtType::BGM>>(
            context_.services, *context_.editor_props, summary_.get(), SkinPtr{},
            false, lib::Action{}, std::move(definitions));
        selector_->bindEditorCommons(context_.editor_commons);
        selector_->path2(path_, false, false, -1, false);
        selector_->volume(static_cast<int>(volume_));
        selector_->loopCount(static_cast<int>(loop_count_));
        selector_->channel(static_cast<int>(channel_));
        selector_->fadeIn(static_cast<int>(fade_in_));
        body = buildAudioSelectorBody(*selector_, true, context_);
        selector_->modEvent.push_back([this] { syncSelector(); });
        selector_->loadedEvent.push_back([this] { refreshTypedBody(); });
    } else {
        body = buildFallbackAudioBody(
            path_, channel_, volume_, loop_count_, fade_in_, true,
            [this] { setApplyEnabled(true); });
    }
    lib::Size dialog_size{324.0F, 462.0F};
    if (context_.editor_props) {
        const auto& geometry = context_.editor_props->flex().soundEvtDlg;
        if (geometry.width() > 0) dialog_size.width = geometry.width();
        if (geometry.height() > 0) dialog_size.height = geometry.height();
    }
    installTypedBody(body, dialog_size, true);
    widget_->minimum_size = lib::Size{300.0F, 360.0F};
    closeEvent.push_back([this] {
        if (!context_.editor_props || !widget_) return;
        const auto bounds = lib::authored_window_bounds(*widget_);
        auto& geometry = context_.editor_props->flex().soundEvtDlg;
        geometry.width(static_cast<std::int32_t>(std::lround(bounds.width)));
        geometry.height(static_cast<std::int32_t>(std::lround(bounds.height)));
    });
}

void BgmDialog::syncSelector() {
    if (!selector_) return;
    path_ = selector_->path();
    volume_ = static_cast<std::uint32_t>(selector_->volume());
    loop_count_ = static_cast<std::uint32_t>(selector_->loopCount());
    channel_ = static_cast<std::uint32_t>(selector_->channel());
    fade_in_ = static_cast<std::uint32_t>(selector_->fadeIn());
    setApplyEnabled(true);
    refreshTypedBody();
}

SeDialog::SeDialog(Context context, SummaryPtr summary, Parent parent,
                   ContentPtr parentContent, ContentPtr content, SkinPtr skin,
                   UseCounterPtr useCounter)
    : EventDialog(std::move(context), std::move(summary), CType::PlaySound,
                  std::move(parent), std::move(parentContent),
                  std::move(content), std::move(skin),
                  std::move(useCounter)) {
    if (draft_) {
        path_ = draft_->soundPath();
        volume_ = draft_->soundVolume();
        loop_count_ = draft_->soundLoopCount();
        fade_in_ = draft_->soundFadeIn();
        channel_ = draft_->soundChannel();
    }
}

void SeDialog::refSkin() {
    EventDialog::refSkin();
    if (selector_) selector_->refSkin();
}

void SeDialog::refreshWarning() {
    replaceWarnings(selector_
        ? typedAudioWarnings(*selector_, context_, summary_)
        : WarningList{});
}

void SeDialog::buildControls() {
    std::shared_ptr<lib::Panel> body;
    if (context_.editor_props) {
        const auto label = context_.prop
            ? context_.prop->requiredMsgs().defaultSelection(
                  context_.prop->requiredMsgs().noSelect.value)
            : String{"[指定無し]"};
        auto definitions = [label](const bool) {
            return std::vector<String>{label};
        };
        selector_ = std::make_unique<materialselect::MaterialSelect<
            materialselect::MtType::SE>>(
            context_.services, *context_.editor_props, summary_.get(), SkinPtr{},
            false, lib::Action{}, std::move(definitions));
        selector_->bindEditorCommons(context_.editor_commons);
        selector_->path2(path_, false, false, -1, false);
        selector_->volume(static_cast<int>(volume_));
        selector_->loopCount(static_cast<int>(loop_count_));
        selector_->channel(static_cast<int>(channel_));
        selector_->fadeIn(static_cast<int>(fade_in_));
        body = buildAudioSelectorBody(*selector_, false, context_);
        selector_->modEvent.push_back([this] { syncSelector(); });
        selector_->loadedEvent.push_back([this] { refreshTypedBody(); });
    } else {
        body = buildFallbackAudioBody(
            path_, channel_, volume_, loop_count_, fade_in_, false,
            [this] { setApplyEnabled(true); });
    }
    lib::Size dialog_size{316.0F, 432.0F};
    if (context_.editor_props) {
        const auto& geometry = context_.editor_props->flex().soundEvtDlg;
        if (geometry.width() > 0) dialog_size.width = geometry.width();
        if (geometry.height() > 0) dialog_size.height = geometry.height();
    }
    installTypedBody(body, dialog_size, true);
    widget_->minimum_size = lib::Size{300.0F, 350.0F};
    closeEvent.push_back([this] {
        if (!context_.editor_props || !widget_) return;
        const auto bounds = lib::authored_window_bounds(*widget_);
        auto& geometry = context_.editor_props->flex().soundEvtDlg;
        geometry.width(static_cast<std::int32_t>(std::lround(bounds.width)));
        geometry.height(static_cast<std::int32_t>(std::lround(bounds.height)));
    });
}

void SeDialog::syncSelector() {
    if (!selector_) return;
    path_ = selector_->path();
    volume_ = static_cast<std::uint32_t>(selector_->volume());
    loop_count_ = static_cast<std::uint32_t>(selector_->loopCount());
    channel_ = static_cast<std::uint32_t>(selector_->channel());
    fade_in_ = static_cast<std::uint32_t>(selector_->fadeIn());
    setApplyEnabled(true);
    refreshTypedBody();
}

BrMemberDialog::BrMemberDialog(Context context, SummaryPtr summary, Parent parent,
                               ContentPtr parentContent, ContentPtr content,
                               UseCounterPtr useCounter, SkinPtr skin)
    : EventDialog(std::move(context), std::move(summary), CType::BranchSelect,
                  std::move(parent), std::move(parentContent), std::move(content),
                  std::move(skin), std::move(useCounter)) {
    if (draft_) {
        target_all_ = draft_->targetAll();
        selection_method_ = draft_->selectionMethod();
        // ダイアログの編集可能なレコードは独自の一覧に保持する。Content
        // は編集中の有効なクーポン項目への記述子を返すため。
        const auto draftCoupons = draft_->coupons();
        coupon_records_.assign(draftCoupons.begin(), draftCoupons.end());
        for (const auto& coupon : coupon_records_)
            if (coupon) coupons_.push_back(coupon->name());
        init_value_ = draft_->initValue();
    }

}

void BrMemberDialog::setupBody() {
    const auto& messages = context_.prop->requiredMsgs();
    auto root = std::make_shared<lib::Panel>();
    auto root_layout = std::make_shared<lib::GridLayout>();
    root_layout->columns = 1;
    root_layout->spacing = 4.0F;
    root_layout->padding = {4.0F, 4.0F, 4.0F, 4.0F};
    root->set_layout(root_layout);

    auto choices = std::make_shared<lib::Panel>();
    auto choices_layout = std::make_shared<lib::GridLayout>();
    choices_layout->columns = 2;
    choices_layout->spacing = 6.0F;
    // Dはこの二グループの行にzeroMarginGridLayout
    // を使う。共通の既定余白を残すと、コンパクトなBranchSelect
    // の参照幅から10pxが失われ、長い対象ラベルが切れる。
    choices_layout->padding = {};
    choices->set_layout(choices_layout);
    choices->set_layout_item({.row = 0, .column = 0,
                              .horizontal = lib::Align::Stretch});

    const auto make_radio_group = [&](String title, const lib::Array<String>& labels,
                                      String group_name,
                                      lib::Array<std::shared_ptr<lib::RadioButton>>& output,
                                      const std::size_t selected,
                                      std::function<void(std::size_t)> changed) {
        auto group = std::make_shared<lib::GroupBox>();
        group->title = std::move(title);
        auto layout = std::make_shared<lib::LinearLayout>();
        layout->spacing = 2.0F;
        layout->padding = {5.0F, 8.0F, 5.0F, 4.0F};
        // DのCenterLayoutは、自然幅のラ
        // ジオ列を各Groupの中央へ置く。両グル
        // ープを同じラベル表示幅へ引き伸ばさない。
        layout->cross_axis = lib::Align::Center;
        group->set_layout(layout);
        for (std::size_t index = 0; index < labels.size(); ++index) {
            auto button = std::make_shared<lib::RadioButton>(labels[index]);
            button->group = group_name;
            button->checked = index == selected;
            button->on_changed = [index, changed](bool checked) {
                if (checked && changed) changed(index);
            };
            output.push_back(button);
            group->add(button);
        }
        return group;
    };
    auto target_group = make_radio_group(
        messages.selectMember.value,
        {messages.activeMember.value, messages.allMember.value},
        "branch-select-target", target_buttons_, target_all_ ? 1U : 0U,
        [this](std::size_t index) { target_all_ = index == 1U; setApplyEnabled(true); });
    target_group->set_layout_item({.row = 0, .column = 0,
                                   .preferred = {0.0F, 78.0F}, .grow = 1.0F});
    choices->add(target_group);
    auto method_group = make_radio_group(
        messages.selectMethod.value,
        {messages.manualMethod.value, messages.randomMethod.value,
         messages.valuedMethod.value},
        "branch-select-method", method_buttons_,
        static_cast<std::size_t>(selection_method_),
        [this](std::size_t index) {
            selection_method_ = static_cast<types::SelectionMethod>(index);
            refreshMemberControls();
            setApplyEnabled(true);
        });
    method_group->set_layout_item({.row = 0, .column = 1,
                                    .preferred = {0.0F, 78.0F}, .grow = 1.0F});
    choices->add(method_group);
    root->add(choices);

    auto valued_group = std::make_shared<lib::GroupBox>();
    valued_group->title = messages.valued.value;
    auto valued_layout = std::make_shared<lib::GridLayout>();
    valued_layout->columns = 2;
    valued_layout->spacing = 3.0F;
    valued_layout->padding = {5.0F, 8.0F, 5.0F, 4.0F};
    valued_group->set_layout(valued_layout);
    valued_group->set_layout_item({.row = 1, .column = 0,
                                   .horizontal = lib::Align::Stretch,
                                   .vertical = lib::Align::Stretch,
                                   .preferred = {283.0F, 218.0F}, .grow = 1.0F,
                                   .constrain_minimum_to_preferred = true});
    auto init_label = std::make_shared<lib::Label>(messages.initValue.value);
    init_label->set_layout_item({.row = 0, .column = 0});
    valued_group->add(init_label);
    init_value_editor_ = std::make_shared<lib::NumberEdit>();
    const auto max_value = context_.vars
        ? static_cast<int>(context_.vars->couponValueMax.value) : 999;
    init_value_editor_->minimum = -max_value;
    init_value_editor_->maximum = max_value;
    init_value_editor_->value = init_value_;
    init_value_editor_->on_changed = [this](double value) {
        init_value_ = static_cast<int>(value);
        setApplyEnabled(true);
    };
    init_value_editor_->set_layout_item({.row = 0, .column = 1,
                                         .preferred = {110.0F, 24.0F}});
    valued_group->add(init_value_editor_);
    if (context_.editor_commons) {
        coupon_view_ = std::make_shared<couponview::CouponView<couponview::CVType::Valued>>(
            *context_.editor_commons, summary_, use_counter_, valued_group);
        coupon_view_->coupons(coupon_records_);
        coupon_view_->panel()->set_layout_item({.row = 1, .column = 0,
                                                 .column_span = 2,
                                                 .horizontal = lib::Align::Stretch,
                                                 .vertical = lib::Align::Stretch,
                                                .preferred = {265.0F, 160.0F},
                                                .grow = 1.0F});
        coupon_view_->addModifyListener([this] {
            if (!coupon_view_) return;
            coupon_records_ = coupon_view_->coupons();
            coupons_.clear();
            for (const auto& coupon : coupon_records_)
                if (coupon) coupons_.push_back(coupon->name());
            refreshMemberControls();
            setApplyEnabled(true);
        });
    }
    root->add(valued_group);
    installTypedBody(root, {520.0F, 475.0F});
    refreshMemberControls();
}

void BrMemberDialog::refreshMemberControls() {
    const bool valued = selection_method_ == types::SelectionMethod::Valued;
    const bool available = !summary_ || !summary_->legacy() || valued ||
                           !coupon_records_.empty();
    if (coupon_view_) coupon_view_->enabled(available && valued);
    if (init_value_editor_) init_value_editor_->set_enabled(available && valued);
    if (method_buttons_.size() > static_cast<std::size_t>(types::SelectionMethod::Valued))
        method_buttons_[static_cast<std::size_t>(types::SelectionMethod::Valued)]->set_enabled(
            available);
    refreshWarning();
}

void BrMemberDialog::refDataVersion() {
    EventDialog::refDataVersion();
    refreshMemberControls();
}

void BrMemberDialog::refreshWarning() {
    if (coupon_view_ && context_.prop) {
        std::int64_t maximum = init_value_, minimum = init_value_;
        for (const auto& coupon : coupon_records_) if (coupon) {
            if (coupon->value() < 0) minimum += coupon->value();
            else maximum += coupon->value();
        }
        const auto& msgs = context_.prop->requiredMsgs();
        const auto format = maximum <= 0 ? msgs.selectMemberValuedMaxIsLess0.value
            : minimum <= 0 ? msgs.selectMemberValuedMaxMinLess0.value : msgs.selectMemberValuedMaxMin.value;
        coupon_view_->toolTip(utils::tryFormat(format, maximum, minimum));
    }
    WarningList result;
    const auto* prop = context_.editor_props;
    if (!prop) {
        replaceWarnings(std::move(result));
        return;
    }
    if (selection_method_ == types::SelectionMethod::Valued &&
        !prop->isTargetVersion(summary_.get(), "1")) {
        result.push_back(prop->requiredMsgs().warningValuedSelectionMethod.value);
    }
    const bool classic = summary_ && summary_->legacy();
    const auto version = summary_ ? summary_->dataVersion() : String{};
    for (const auto& coupon : coupon_records_) {
        if (!coupon) continue;
        const auto current = warning::couponWarnings(
            prop->parent(), classic, version, prop->var().targetVersion.value,
            coupon->name(), false, prop->requiredMsgs().valued.value);
        result.insert(result.end(), current.begin(), current.end());
    }
    replaceWarnings(std::move(result));
}

bool BrMemberDialog::apply() {
    auto value = ensureDraft();
    value->targetAll(target_all_);
    value->selectionMethod(selection_method_);
    if (selection_method_ == types::SelectionMethod::Valued) {
        // Dは他の項目だけが変わった場合も、現在の表示から得た値を確定する。
        // 適用までは取り込んだ生の行を保ち、取得時は順序・値を維持して空名だ
        // けを除く。ヘッドレスの呼び出し側はキャッシュした入力を維持する。
        if (coupon_view_) {
            coupon_records_ = coupon_view_->coupons();
            coupons_.clear();
            for (const auto& coupon : coupon_records_)
                if (coupon) coupons_.push_back(coupon->name());
        }
        value->couponRecords(coupon_records_);
        value->initValue(init_value_);
    } else {
        value->couponRecords({});
        value->initValue(0);
    }
    return commitDraft();
}

BrPowerDialog::BrPowerDialog(Context context, SummaryPtr summary, Parent parent,
                             ContentPtr parentContent, ContentPtr content,
                             SkinPtr skin, UseCounterPtr useCounter)
    : EventDialog(std::move(context), std::move(summary),
                  CType::BranchAbility, std::move(parent),
                  std::move(parentContent), std::move(content),
                  std::move(skin), std::move(useCounter)) {
    if (draft_) {
        signed_level_ = draft_->signedLevel();
        target_ = draft_->targetS();
        physical_ = draft_->physical();
        mental_ = draft_->mental();
        invert_result_ = draft_->invertResult();
    }
    if (target_.m != types::Target::M::Selected &&
        target_.m != types::Target::M::Random &&
        target_.m != types::Target::M::Party)
        target_.m = types::Target::M::Selected;
}

void BrPowerDialog::buildControls() {
    auto root = makeGridPanel(3U, 5.0F, {3.0F, 3.0F, 3.0F, 3.0F});
    const auto cast_level_max = static_cast<int>(
        context_.vars ? context_.vars->castLevelMax.value : 99U);
    auto judge = makeGridPanel(1U, 4.0F, {});
    judge->set_layout_item({.row = 0, .column = 0,
                            .preferred = {122.0F, 258.0F}, .grow = 1.0F});

    auto level_group = makeGroupBox(
        context_.prop ? context_.prop->requiredMsgs().targetLevel.value : "対象レベル",
        2U);
    auto level = std::make_shared<lib::NumberEdit>();
    level->minimum = -static_cast<double>(cast_level_max);
    level->maximum = static_cast<double>(cast_level_max);
    level->value = signed_level_;
    level->on_changed = [this](const double value) {
        signed_level_ = static_cast<int>(value);
        setApplyEnabled(true);
    };
    level->set_layout_item({.row = 0, .column = 0,
                            .preferred = {56.0F, 24.0F}});
    level_group->add(level);
    addLabel(level_group,
             context_.prop
                 ? utils::tryFormat(context_.prop->requiredMsgs().rangeHint.value,
                                    -cast_level_max, cast_level_max)
                 : "(" + std::to_string(-cast_level_max) + "～" +
                       std::to_string(cast_level_max) + ")",
             0, 1, 1U, {58.0F, 23.0F});
    level_group->set_layout_item({.row = 0, .column = 0,
                                  .preferred = {122.0F, 55.0F}});
    judge->add(level_group);

    auto target_group = makeGroupBox(
        context_.prop ? context_.prop->requiredMsgs().judgeTarget.value : "判定対象",
        1U);
    constexpr std::array target_values{
        types::Target::M::Selected, types::Target::M::Random,
        types::Target::M::Party};
    for (std::size_t index = 0; index < target_values.size(); ++index) {
        const auto value = target_values[index];
        auto radio = std::make_shared<lib::RadioButton>(
            context_.prop ? context_.prop->requiredMsgs().targetName(value)
                          : std::to_string(index));
        radio->group = "ability-target";
        radio->checked = target_.m == value;
        radio->on_changed = [this, value](const bool checked) {
            if (!checked) return;
            target_.m = value;
            setApplyEnabled(true);
        };
        radio->set_layout_item({.row = index, .column = 0,
                                .preferred = {112.0F, 22.0F}});
        target_group->add(radio);
    }
    target_group->set_layout_item({.row = 1, .column = 0,
                                   .preferred = {122.0F, 110.0F}});
    judge->add(target_group);

    auto sleep_group = makeGroupBox(
        context_.prop ? context_.prop->requiredMsgs().judgeSleep.value : "眠り判定",
        1U);
    for (std::size_t index = 0; index < 2U; ++index) {
        const bool value = index != 0U;
        auto radio = std::make_shared<lib::RadioButton>(
            context_.prop
                ? (value ? context_.prop->requiredMsgs().sleepEnabled.value
                         : context_.prop->requiredMsgs().sleepDisabled.value)
                : (value ? "睡眠・呪縛者有効" : "睡眠・呪縛者無効"));
        radio->group = "ability-sleep";
        radio->checked = target_.sleep == value;
        radio->on_changed = [this, value](const bool checked) {
            if (!checked) return;
            target_.sleep = value;
            setApplyEnabled(true);
        };
        radio->set_layout_item({.row = index, .column = 0,
                                .preferred = {112.0F, 22.0F}});
        sleep_group->add(radio);
    }
    sleep_group->set_layout_item({.row = 2, .column = 0,
                                  .preferred = {122.0F, 67.0F}});
    judge->add(sleep_group);

    auto success_group = makeGroupBox(
        context_.prop ? context_.prop->requiredMsgs().resultType.value : "成功条件", 1U);
    invert_result_control_ = std::make_shared<lib::CheckBox>(
        context_.prop ? context_.prop->requiredMsgs().resultTypeInvertForAbility.value
                      : "条件に合わない場合に成功とする");
    invert_result_control_->checked = invert_result_;
    invert_result_control_->set_enabled(
        !summary_ || !summary_->legacy() || invert_result_);
    invert_result_control_->on_changed = [this](const bool checked) {
        invert_result_ = checked;
        invert_result_control_->set_enabled(!summary_ || !summary_->legacy() || invert_result_);
        setApplyEnabled(true);
    };
    version_control_updates_.push_back([this] {
        invert_result_control_->set_enabled(!summary_ || !summary_->legacy() || invert_result_);
    });
    success_group->add(invert_result_control_);
    success_group->set_layout_item({.row = 3, .column = 0,
                                    .preferred = {122.0F, 48.0F}});
    judge->add(success_group);
    root->add(judge);

    auto ability_host = makeGridPanel(1U, 0.0F, {});
    ability_host->set_layout_item({.row = 0, .column = 1,
                                   .column_span = 2,
                                   .preferred = {240.0F, 258.0F},
                                   .grow = 1.0F});
    root->add(ability_host);
    ability_control_ = std::make_unique<abilityview::AbilityView>(
        abilityCommons(context_), ability_host, 0, context_.services);
    ability_control_->physical(physical_);
    ability_control_->mental(mental_);
    ability_control_->widget()->set_layout_item(
        {.row = 0, .column = 0, .preferred = {240.0F, 258.0F},
         .grow = 1.0F});
    ability_control_->addModificationListener([this] {
        if (!ability_control_) return;
        physical_ = ability_control_->physical();
        mental_ = ability_control_->mental();
        setApplyEnabled(true);
    });

    installTypedBody(root, {379.0F, 327.0F}, true);
}

void BrPowerDialog::refreshWarning() {
    replaceWarnings(branchLocalWarnings(
        context_, summary_, false, types::Status::None, invert_result_));
}

bool BrPowerDialog::apply() {
    if (ability_control_) {
        physical_ = ability_control_->physical();
        mental_ = ability_control_->mental();
    }
    writeEditedFields(ensureDraft());
    return commitDraft();
}

void BrPowerDialog::writeEditedFields(const ContentPtr& value) const {
    value->signedLevel(signed_level_);
    value->targetS(target_);
    value->physical(physical_);
    value->mental(mental_);
    value->invertResult(invert_result_);
}

void BrLevelDialog::writeEditedFields(const ContentPtr& value) const {
    value->average(average_);
    value->unsignedLevel(unsigned_level_);
}

bool BrLevelDialog::apply() {
    writeEditedFields(ensureDraft());
    return commitDraft();
}

void BrStateDialog::writeEditedFields(const ContentPtr& value) const {
    value->range(range_.range());
    value->status(status_);
    value->invertResult(invert_result_);
}

bool BrStateDialog::apply() {
    writeEditedFields(ensureDraft());
    return commitDraft();
}

template <CType Type, typename C, bool Delete, types::Range RangeDef>
void CardEventDialog<Type, C, Delete, RangeDef>::refreshCardList(bool deleted, bool notify) {
    if (!card_table_) return;
    std::vector<lib::TableRow> rows;
    std::vector<std::size_t> selection;
    const auto old_id = selected_id_;
    card_count_ = 0;
    const auto append = [&](const auto& values) {
        for (const auto& card : values) {
            if (!card) continue;
            ++card_count_;
            if (card_search_ && !card_search_->match(card->name())) continue;
            const auto id = std::to_string(card->id());
            lib::ResourceId image{};
            if (context_.editor_props) {
                if constexpr (Type == CType::BranchSkill || Type == CType::GetSkill || Type == CType::LoseSkill)
                    image = context_.editor_props->images().load(dprops::StringView{"skill.png"});
                else if constexpr (Type == CType::BranchItem || Type == CType::GetItem || Type == CType::LoseItem)
                    image = context_.editor_props->images().load(dprops::StringView{"item.png"});
                else image = context_.editor_props->images().load(dprops::StringView{"beast.png"});
            }
            if (card->id() == old_id) selection = {rows.size()};
            rows.push_back({{{id, id, image}, {card->name(), id}}});
        }
    };
    if (summary_) {
        if constexpr (Type == CType::BranchSkill || Type == CType::GetSkill || Type == CType::LoseSkill) append(summary_->skills());
        else if constexpr (Type == CType::BranchItem || Type == CType::GetItem || Type == CType::LoseItem) append(summary_->items());
        else append(summary_->beasts());
    }
    if (selection.empty() && !rows.empty() && (!Delete || range_ != types::Range::SelectedCard)) selection = {0U};
    auto id = selection.empty() ? 0U : static_cast<std::uint64_t>(std::stoull(rows[selection.front()].cells.front().value));
    if constexpr (Delete) {
        // Dは新形式の最後のLose対象が削除されても、指定IDを維持する。C++
        // では表を空にし、開く操作でDの古いTableItemを使わせない。
        if (deleted && summary_ && !summary_->legacy() && card_count_ == 0)
            id = old_id;
    }
    // DのrefreshListは選択カードを戻した後にTable.showSelection
    // を呼ぶ。共通表が更新した一覧を公開する際も、同じ行を表示内へ移す。
    const auto reveal = selection.empty()
        ? std::optional<std::size_t>{} : std::optional<std::size_t>{selection.front()};
    auto publication = lib::prepare_table_rows(
        card_table_, std::move(rows), std::move(selection), reveal);
    if (!publication.commit()) return;
    selected_id_ = id;
    if (refresh_card_enabled_) refresh_card_enabled_();
    if (deleted && card_count_ == 0 && (!Delete || (summary_ && summary_->legacy()))) {
        forceCancel(); return;
    }
    if (notify && old_id != selected_id_) setApplyEnabled(true);
}

template <CType Type, typename C, bool Delete, types::Range RangeDef>
void CardEventDialog<Type, C, Delete, RangeDef>::refreshWarning() {
    WarningList result;
    const auto* messages = context_.prop ? &context_.prop->requiredMsgs() : nullptr;
    if (messages && summary_ && range_ != types::Range::SelectedCard &&
        selected_id_ == 0U) {
        if constexpr (Type == CType::BranchSkill || Type == CType::GetSkill ||
                      Type == CType::LoseSkill)
            result.push_back(messages->searchErrorNoSkill.value);
        else if constexpr (Type == CType::BranchItem || Type == CType::GetItem ||
                           Type == CType::LoseItem)
            result.push_back(messages->searchErrorNoItem.value);
        else
            result.push_back(messages->searchErrorNoBeast.value);
    }
    const auto supports = [this](const StringView version) {
        if (context_.editor_props)
            return context_.editor_props->isTargetVersion(summary_.get(), version);
        return context_.prop && context_.prop->isTargetVersion(
            summary_ && summary_->legacy(),
            summary_ ? summary_->dataVersion() : String{}, version);
    };
    if constexpr (Type == CType::BranchSkill || Type == CType::BranchItem ||
                  Type == CType::BranchBeast) {
        if (!supports("3") && select_card_ && messages)
            result.push_back(messages->warningSelectCard.value);
    }
    if (!supports("3") && range_ == types::Range::SelectedCard && messages)
        result.push_back(utils::tryFormat(messages->warningRangeSelectedCard.value));
    if constexpr (Type == CType::BranchSkill || Type == CType::BranchItem ||
                  Type == CType::BranchBeast) {
        if (!supports("4") && invert_result_ && messages)
            result.push_back(messages->warningInvertResult.value);
    }
    replaceWarnings(std::move(result));
}

template <CType Type, typename C, bool Delete, types::Range RangeDef>
void CardEventDialog<Type, C, Delete, RangeDef>::buildControls() {
    struct Controls {
        std::shared_ptr<lib::NumberEdit> number;
        std::shared_ptr<lib::TableView> cards;
        std::shared_ptr<lib::CheckBox> all_delete;
        std::shared_ptr<lib::CheckBox> select_card;
        std::shared_ptr<lib::ComboBox> invert;
        std::shared_ptr<lib::RadioButton> selected_card_range;
        std::function<void()> refresh_enabled;
    };
    auto controls = std::make_shared<Controls>();
    const auto& messages = contentDialogMessages(context_);
    const bool legacy = summary_ && summary_->legacy();
    // ALLはUIフォントが大きく、Dの左列の固定幅では枚数・適
    // 用範囲・成功条件の文字が切れるため、左列を1.3倍にする。
    const float left_scale =
        lib::editor_window_host_options().all_mode ? 1.3F : 1.0F;
    auto root = makeGridPanel(2U, 5.0F, {5.0F, 5.0F, 5.0F, 5.0F});
    auto left = makeGridPanel(1U, 4.0F, {});
    auto number_group = makeGroupBox(messages.cardNumber.value, 2U);
    auto number = controls->number = std::make_shared<lib::NumberEdit>();
    const auto card_number_max = context_.vars
        ? static_cast<int>(context_.vars->cardNumberMax.value) : 99;
    number->minimum = 1; number->maximum = card_number_max; number->value = card_number_;
    number->on_changed = [this](const double value) {
        card_number_ = static_cast<int>(value); setApplyEnabled(true);
    };
    number_group->add(number);
    number_group->add(std::make_shared<lib::Label>(utils::tryFormat(
        messages.rangeHint.value, 1, card_number_max)));
    if constexpr (Delete) {
        auto all_delete = controls->all_delete =
            std::make_shared<lib::CheckBox>(messages.cardAllDelete.value);
        all_delete->checked = delete_all_;
        all_delete->set_layout_item({.row = 1U, .column = 0U,
                                     .column_span = 2U,
                                     .preferred = {125.0F, 22.0F}});
        number_group->add(all_delete);
    }
    left->add(number_group);
    auto ranges = makeGroupBox(messages.cardEventRange.value, 1U);
    const std::array range_choices{
        types::Range::Selected, types::Range::Random, types::Range::Party,
        types::Range::Backpack, types::Range::PartyAndBackpack,
        types::Range::Field, types::Range::SelectedCard};
    for (std::size_t index = 0; index < range_choices.size(); ++index) {
        const auto value = range_choices[index];
        const auto label = value == types::Range::SelectedCard &&
                (Type == CType::GetSkill || Type == CType::GetItem ||
                 Type == CType::GetBeast)
            ? messages.rangeNameSelectedCardForReplace.value
            : messages.rangeName(value);
        auto radio = std::make_shared<lib::RadioButton>(label);
        radio->group = "card-event-range";
        radio->checked = range_ == value;
        if (value == types::Range::Field)
            radio->set_tooltip(contentDialogMessages(context_).rangeDescField.value);
        if (value == types::Range::SelectedCard) {
            controls->selected_card_range = radio;
            radio->set_enabled(!legacy || radio->checked);
        }
        radio->on_changed = [this, value, controls](const bool checked) {
            if (!checked) return;
            range_ = value;
            if (controls->refresh_enabled) controls->refresh_enabled();
            // Dは範囲選択をすべてrefDataVersion()
            // へ渡し、操作部の有効状態と警告を更新する。
            refreshWarning();
            setApplyEnabled(true);
        };
        radio->set_layout_item({.row = index, .column = 0U,
                                .preferred = {128.0F * left_scale, 22.0F}});
        ranges->add(radio);
    }
    left->add(ranges);
    if constexpr (Type == CType::BranchSkill || Type == CType::BranchItem ||
                  Type == CType::BranchBeast) {
        auto result = makeGroupBox(messages.resultType.value, 1U);
        auto combo = controls->invert = std::make_shared<lib::ComboBox>();
        combo->items = {{messages.resultTypeNormal.value, "has"},
                        {messages.resultTypeInvert.value, "not"}};
        combo->selected_index = invert_result_ ? 1U : 0U;
        combo->on_selection_changed = [this, controls](const auto index) {
            if (index) {
                invert_result_ = *index == 1U;
                if (controls->refresh_enabled) controls->refresh_enabled();
                refreshWarning();
                setApplyEnabled(true);
            }
        };
        result->add(combo); left->add(result);
    }
    left->set_layout_item({.row = 0U, .column = 0U,
                           .minimum = {135.0F * left_scale, 0.0F},
                           .preferred = {135.0F * left_scale, 310.0F}});
    root->add(left);

    auto cards = controls->cards = std::make_shared<lib::TableView>();
    // Dは見出しなしのアイコン・IDと名前の選択一覧とし
    // て描く。二列目に空でないタイトルを付けると、Windows
    // 側がヘッダーを作り、全カードが一行下がる。
    const float id_width = context_.vars
        ? static_cast<float>(context_.vars->idColumn.value)
        : 50.0F;
    cards->columns = {{"", id_width}, {"", 245.0F}};
    card_table_ = cards;
    cards->header_visible = false;
    refreshCardList(false, false);
    cards->on_selection_changed = [this, cards](const auto& selection) {
        if (selection.empty() || selection.front() >= cards->rows.size()) {
            selected_id_ = 0; setApplyEnabled(true); return;
        }
        selected_id_ = static_cast<std::uint64_t>(std::stoull(cards->rows[selection.front()].cells.front().value));
        setApplyEnabled(true);
    };
    cards->on_column_resized = [this](const std::size_t column,
                                      const float width) {
        if (column != 0U || !context_.editor_props) return;
        context_.editor_props->var().idColumn.value =
            static_cast<std::int32_t>(std::lround(width));
        (void)context_.editor_props->apply();
    };
    cards->edit_on_double_click = false;
    cards->on_cell_activate = [this, cards](const std::size_t row,
                                            const std::size_t) {
        if (row >= cards->rows.size()) return;
        const auto id = static_cast<std::uint64_t>(
            std::stoull(cards->rows[row].cells.front().value));
        const auto open = [this, id](const auto& values) {
            for (const auto& card : values) {
                if (card && card->id() == id) {
                    try {
                        (void)openExternalCWXPath(
                            path::cpaddattr(card->cwxPath(true), "shallow"), false);
                    } catch (const std::exception& error) {
                        (void)utils::printStackTrace();
                        utils::debugln(error.what());
                    }
                    return;
                }
            }
        };
        if (!summary_) return;
        if constexpr (Type == CType::BranchSkill || Type == CType::GetSkill ||
                      Type == CType::LoseSkill)
            open(summary_->skills());
        else if constexpr (Type == CType::BranchItem || Type == CType::GetItem ||
                           Type == CType::LoseItem)
            open(summary_->items());
        else
            open(summary_->beasts());
    };
    const float table_width = context_.vars
        ? static_cast<float>(context_.vars->nameTableWidth.value)
        : 300.0F;
    const float table_height = context_.vars
        ? static_cast<float>(context_.vars->nameTableHeight.value)
        : 180.0F;
    cards->set_layout_item({.row = 0U, .column = 1U,
                            .preferred = {table_width, table_height},
                            .grow = 1.0F});
    root->add(cards);
    if constexpr (Type == CType::BranchSkill || Type == CType::BranchItem || Type == CType::BranchBeast) {
        auto select = controls->select_card =
            std::make_shared<lib::CheckBox>(messages.selectFoundCard.value);
        select->checked = select_card_;
        select->on_changed = [this, controls](const bool value) {
            select_card_ = value;
            if (controls->refresh_enabled) controls->refresh_enabled();
            refreshWarning();
            setApplyEnabled(true);
        };
        select->set_layout_item({.row = 1U, .column = 1U, .preferred = {285.0F, 23.0F}});
        root->add(select);
    }
    controls->refresh_enabled = [this, controls] {
        const bool legacy = summary_ && summary_->legacy();
        const bool selected_card = range_ == types::Range::SelectedCard;
        if (controls->selected_card_range)
            controls->selected_card_range->set_enabled(
                !legacy || controls->selected_card_range->checked);
        if (controls->all_delete)
            controls->all_delete->set_enabled(!selected_card);
        if (controls->number)
            controls->number->set_enabled(!selected_card &&
                (!controls->all_delete || !controls->all_delete->checked));
        if (controls->cards)
            controls->cards->set_enabled(card_count_ != 0 &&
                                         (!Delete || !selected_card));
        if (controls->select_card)
            controls->select_card->set_enabled(
                !selected_card && (!legacy || controls->select_card->checked));
        if (controls->invert)
            controls->invert->set_enabled(!legacy || invert_result_);
    };
    if constexpr (Delete) {
        controls->all_delete->on_changed =
            [this, controls](const bool value) {
                delete_all_ = value;
                if (controls->refresh_enabled) controls->refresh_enabled();
                setApplyEnabled(true);
            };
    }
    version_control_updates_.push_back(controls->refresh_enabled);
    controls->refresh_enabled();
    installTypedBody(root, {421.0F, 379.0F}, true);
    refresh_card_enabled_ = controls->refresh_enabled;
    incsearch::IncSearchSettings search_settings;
    if (context_.editor_props) {
        const auto* prop = context_.editor_props;
        search_settings.startIncrementalSearchWhenKeyDown = prop->var().startIncrementalSearchWhenKeyDown.value;
        search_settings.startIncrementalSearchWhenKeyDownProvider = [prop] {
            return prop && prop->var().startIncrementalSearchWhenKeyDown.value;
        };
        search_settings.incrementalSearchBoxWidth = prop->var().incrementalSearchBoxWidth.value;
        search_settings.incrementalSearchType = prop->var().incrementalSearchType.value;
        search_settings.containsLabel = prop->requiredMsgs().incSearchContains.value;
        search_settings.wildcardLabel = prop->requiredMsgs().incSearchWildcard.value;
        search_settings.regexLabel = prop->requiredMsgs().incSearchRegex.value;
        search_settings.windowTitle = prop->requiredMsgs().menuText(types::MenuID::IncSearch);
    }
    const auto life = std::weak_ptr<void>(card_lifetime_);
    card_search_ = std::make_shared<incsearch::IncSearch>(context_.services, cards.get(),
        [this, life] { return !life.expired() && card_count_ != 0; },
        std::vector<incsearch::AdditionMatcher>{}, search_settings);
    card_search_->modEvent.push_back([this, life] {
        if (life.expired()) return;
        if (context_.editor_props) context_.editor_props->var().incrementalSearchType.value = static_cast<int>(card_search_->searchType());
        refreshCardList();
    });
    cards->on_text_input = [this, life](lib::TextInputEvent& event) {
        if (!life.expired() && card_search_->handleHostTextInput(event, card_table_.get())) event.handled = true;
    };
    cards->on_key_down = [this, life](lib::KeyEvent& event) {
        if (!life.expired() && card_search_->handleHostKeyDown(event, card_table_.get())) event.handled = true;
    };
    card_menu_ = std::make_shared<lib::Menu>(true);
    for (const auto& [id, command] : std::array<std::pair<types::MenuID, String>, 2>{{
        {types::MenuID::IncSearch, "search"}, {types::MenuID::OpenAtCardView, "open"}}})
        card_menu_->items.push_back({context_.editor_props ? context_.editor_props->buildMenu(id) : command,
            command, {}, context_.editor_props ? context_.editor_props->images().menu(id) : lib::ResourceId{}});
    card_menu_->on_open = [this, life] {
        if (life.expired()) return;
        for (auto& item : card_menu_->items) item.enabled = item.command == "search" ? card_count_ != 0 : !card_table_->selection.empty();
    };
    card_menu_->on_command = [this, life](StringView command) {
        if (life.expired()) return;
        if (command == "search") {
            const auto search = card_search_;
            const auto cards = card_table_;
            const auto gui = context_.services.gui;
            if (!search || !cards || !gui) return;
            (void)gui->focus_control(cards, true);
            // フォーカス操作が、この型付きダイアログを同期的に閉じたり終了させたりし得る。
            if (life.expired() || !isOpen()) return;
            if (context_.services.gui != gui || search != card_search_ ||
                cards != card_table_) return;
            search->startIncSearch();
        } else if (command == "open" && !card_table_->selection.empty() && card_table_->on_cell_activate)
            card_table_->on_cell_activate(card_table_->selection.front(), 0U);
    };
    cards->on_pointer = [this, life](lib::PointerEvent& event) {
        if (life.expired() || event.action != lib::PointerAction::Down || event.button != lib::PointerButton::Secondary || !context_.services.gui) return;
        card_menu_->on_open();
        event.handled = true;
        context_.services.gui->show_context_menu(card_menu_, card_table_, event.position);
    };
    if (context_.editor_commons) {
        const auto bind = [this](auto& signal, bool deleted) {
            auto* source = &signal;
            const auto token = signal.add([this, life = std::weak_ptr<void>(card_lifetime_), deleted](auto&&...) {
                if (!life.expired()) refreshCardList(deleted);
            });
            card_disconnect_.push_back([source, token] { source->remove(token); });
        };
        auto& comm = *context_.editor_commons;
        if constexpr (Type == CType::BranchSkill || Type == CType::GetSkill || Type == CType::LoseSkill) {
            bind(comm.refSkill, false); bind(comm.delSkill, true);
        } else if constexpr (Type == CType::BranchItem || Type == CType::GetItem || Type == CType::LoseItem) {
            bind(comm.refItem, false); bind(comm.delItem, true);
        } else { bind(comm.refBeast, false); bind(comm.delBeast, true); }
    }
}

template void CardEventDialog<CType::BranchSkill, void, false, types::Range::Field>::buildControls();
template void CardEventDialog<CType::BranchItem, void, false, types::Range::Field>::buildControls();
template void CardEventDialog<CType::BranchBeast, void, false, types::Range::Field>::buildControls();
template void CardEventDialog<CType::GetSkill, void, false, types::Range::Selected>::buildControls();
template void CardEventDialog<CType::GetItem, void, false, types::Range::Selected>::buildControls();
template void CardEventDialog<CType::GetBeast, void, false, types::Range::Selected>::buildControls();
template void CardEventDialog<CType::LoseSkill, void, true, types::Range::Field>::buildControls();
template void CardEventDialog<CType::LoseItem, void, true, types::Range::Field>::buildControls();
template void CardEventDialog<CType::LoseBeast, void, true, types::Range::Field>::buildControls();
template void CardEventDialog<CType::BranchSkill, void, false, types::Range::Field>::refreshWarning();
template void CardEventDialog<CType::BranchItem, void, false, types::Range::Field>::refreshWarning();
template void CardEventDialog<CType::BranchBeast, void, false, types::Range::Field>::refreshWarning();
template void CardEventDialog<CType::GetSkill, void, false, types::Range::Selected>::refreshWarning();
template void CardEventDialog<CType::GetItem, void, false, types::Range::Selected>::refreshWarning();
template void CardEventDialog<CType::GetBeast, void, false, types::Range::Selected>::refreshWarning();
template void CardEventDialog<CType::LoseSkill, void, true, types::Range::Field>::refreshWarning();
template void CardEventDialog<CType::LoseItem, void, true, types::Range::Field>::refreshWarning();
template void CardEventDialog<CType::LoseBeast, void, true, types::Range::Field>::refreshWarning();

void BrRandomSelectDialog::buildControls() {
    struct Controls {
        std::shared_ptr<lib::NumberEdit> minimum;
        std::shared_ptr<lib::NumberEdit> maximum;
        std::shared_ptr<lib::Container> statuses;
        std::shared_ptr<lib::ComboBox> result;
        std::function<void()> refresh_enabled;
    };
    auto controls = std::make_shared<Controls>();
    const bool legacy = summary_ && summary_->legacy();
    const auto& messages = contentDialogMessages(context_);
    auto root = makeGridPanel(2U, 5.0F, {5.0F, 5.0F, 5.0F, 5.0F});
    auto target = makeGroupBox(messages.selectMember.value, 3U);
    const std::array<types::CastRange, 3U> choices{
        types::CastRange::Party, types::CastRange::Enemy, types::CastRange::Npc};
    for (std::size_t i = 0; i < choices.size(); ++i) {
        const auto choice = choices[i];
        auto check = std::make_shared<lib::CheckBox>(messages.castRangeName(choice));
        check->checked = std::find(cast_range_.begin(), cast_range_.end(), choice) != cast_range_.end();
        check->on_changed = [this, value = choice](const bool checked) {
            const auto found = std::find(cast_range_.begin(), cast_range_.end(), value);
            if (checked && found == cast_range_.end()) cast_range_.push_back(value);
            if (!checked) std::erase(cast_range_, value);
            setApplyEnabled(true);
        };
        target->add(check);
    }
    auto level_check = std::make_shared<lib::CheckBox>(messages.randomSelectHasLevel.value);
    level_check->checked = has_level_; target->add(level_check);
    auto status_check = std::make_shared<lib::CheckBox>(messages.randomSelectHasStatus.value);
    status_check->checked = has_status_; target->add(status_check);
    target->set_layout_item({.row = 0U, .column = 0U,
                             .preferred = {295.0F, 72.0F},
                             .constrain_minimum_to_preferred = true});
    root->add(target);
    auto result = makeGroupBox(messages.resultType.value, 1U);
    auto result_combo = controls->result = std::make_shared<lib::ComboBox>();
    result_combo->items = {{messages.resultTypeNormalForCondition.value, "normal"},
                           {messages.resultTypeInvertForCondition.value, "invert"}};
    result_combo->selected_index = invert_result_ ? 1U : 0U;
    result_combo->on_selection_changed = [this, controls](const auto index) {
        if (index) {
            invert_result_ = *index == 1U;
            if (controls->refresh_enabled) controls->refresh_enabled();
            setApplyEnabled(true);
        }
    };
    result->set_layout_item({.row = 0U, .column = 1U,
                             .preferred = {110.0F, 72.0F},
                             .constrain_minimum_to_preferred = true});
    result->add(result_combo); root->add(result);
    auto levels = makeGroupBox(messages.targetLevel.value, 4U);
    const auto level_maximum=context_.vars?static_cast<int>(context_.vars->castLevelMax.value):99;
    auto min = controls->minimum = std::make_shared<lib::NumberEdit>(); min->minimum=1; min->maximum=level_maximum; min->value=level_min_ ? level_min_ : 1;
    auto max = controls->maximum = std::make_shared<lib::NumberEdit>(); max->minimum=1; max->maximum=level_maximum; max->value=level_max_ ? level_max_ : 1;
    min->set_enabled(has_level_); max->set_enabled(has_level_);
    min->on_changed=[this,max](double v){
        level_min_=static_cast<int>(v);
        if (level_min_ > static_cast<int>(max->value)) {
            level_max_ = level_min_;
            max->value = level_max_;
        }
        setApplyEnabled(true);
    };
    max->on_changed=[this,min](double v){
        level_max_=static_cast<int>(v);
        if (level_max_ < static_cast<int>(min->value)) {
            level_min_ = level_max_;
            min->value = level_min_;
        }
        setApplyEnabled(true);
    };
    levels->add(min); levels->add(std::make_shared<lib::Label>(messages.levSep.value));
    levels->add(max);
    levels->add(std::make_shared<lib::Label>(
        utils::tryFormat(messages.rangeHint.value, 1, level_maximum)));
    levels->set_layout_item({.row=1U,.column=0U,.column_span=2U,
                             .preferred={410.0F,50.0F},
                             .constrain_minimum_to_preferred=true}); root->add(levels);
    auto statuses = makeStatusRadios(context_, status_, [this](auto value){status_=value;setApplyEnabled(true);}, has_status_);
    controls->statuses = statuses;
    statuses->set_layout_item({.row=2U,.column=0U,.column_span=2U,
                               .preferred={410.0F,160.0F},
                               .constrain_minimum_to_preferred=true}); root->add(statuses);
    auto hint=makeStatusHint(context_); hint->set_layout_item({.row=3U,.column=0U,.column_span=2U,
                                                       .preferred={410.0F,105.0F},
                                                       .constrain_minimum_to_preferred=true}); root->add(hint);
    controls->refresh_enabled = [this, controls] {
        const bool legacy = summary_ && summary_->legacy();
        controls->minimum->set_enabled(has_level_);
        controls->maximum->set_enabled(has_level_);
        setEnabledTree(controls->statuses, has_status_);
        controls->result->set_enabled((has_level_ || has_status_) &&
            (!legacy || invert_result_));
    };
    level_check->on_changed=[this,controls](bool v){
        has_level_=v;
        controls->refresh_enabled();
        setApplyEnabled(true);
    };
    status_check->on_changed=[this,controls](bool v){
        has_status_=v;
        controls->refresh_enabled();
        setApplyEnabled(true);
    };
    version_control_updates_.push_back(controls->refresh_enabled);
    controls->refresh_enabled();
    installTypedBody(root,{405.0F,505.0F},true);
}

void BranchRoundDialog::buildControls() {
    const auto& messages = contentDialogMessages(context_);
    auto root=makeGridPanel(1U,4.0F,{6,6,6,6}); auto group=makeGroupBox(messages.roundCondition.value,4U);
    group->add(std::make_shared<lib::Label>(messages.roundIs.value));
    const auto round_max=context_.vars?static_cast<double>(context_.vars->roundMax.value):999.0;
    auto number=std::make_shared<lib::NumberEdit>(); number->minimum=0;number->maximum=round_max;number->value=round_;
    number->on_changed=[this](double v){round_=static_cast<std::uint32_t>(v);setApplyEnabled(true);}; group->add(number);
    group->add(std::make_shared<lib::Label>(messages.roundCmpIs.value));
    auto cmp=std::make_shared<lib::ComboBox>();
    cmp->items={{context_.prop?context_.prop->requiredMsgs().comparison3Name(types::Comparison3::Eq):"である","eq"},
                {context_.prop?context_.prop->requiredMsgs().comparison3Name(types::Comparison3::Lt):"より大きい","lt"},
                {context_.prop?context_.prop->requiredMsgs().comparison3Name(types::Comparison3::Gt):"より小さい","gt"}};
    cmp->selected_index=comparison_==types::Comparison3::Eq?0U:(comparison_==types::Comparison3::Lt?1U:2U);
    cmp->on_selection_changed=[this](auto i){if(i){comparison_=*i==0?types::Comparison3::Eq:(*i==1?types::Comparison3::Lt:types::Comparison3::Gt);setApplyEnabled(true);}};
    group->add(cmp); root->add(group); installTypedBody(root,{328.0F,134.0F},true);
}

void BranchRoundDialog::refreshWarning() {
    // Dがここで扱うのは対象バージョンの警告だけ。モデル検査はScenarioValidationに保つ。
    WarningList result;
    if (context_.editor_props &&
        !context_.editor_props->targetVersion(summary_.get(), "1.50")) {
        const auto& messages = context_.editor_props->requiredMsgs();
        result.push_back(utils::tryFormat(
            messages.warningUnknownContent.value,
            messages.contentName(CType::BranchRound), "1.50"));
    }
    replaceWarnings(std::move(result));
}

void BrKeyCodeDialog::buildControls() {
    struct Controls {
        std::shared_ptr<lib::RadioButton> selected_card_range;
        std::shared_ptr<lib::ComboBox> matching;
        std::shared_ptr<lib::ComboBox> invert;
        std::shared_ptr<lib::CheckBox> select_card;
        std::function<void()> refresh_enabled;
    };
    auto controls = std::make_shared<Controls>();
    const auto* messages = &contentDialogMessages(context_);
    const bool legacy = summary_ && summary_->legacy();
    auto root = makeGridPanel(2U, 5.0F, {5,5,5,5});
    auto range = makeGroupBox(messages->range.value, 1U);
    const std::array ranges{
        types::Range::Selected, types::Range::Random, types::Range::Backpack,
        types::Range::PartyAndBackpack, types::Range::SelectedCard};
    for (std::size_t index = 0U; index < ranges.size(); ++index) {
        const auto value = ranges[index];
        auto radio = std::make_shared<lib::RadioButton>(messages->rangeName(value));
        radio->group = "key-code-range";
        radio->checked = range_ == value;
        if (value == types::Range::SelectedCard) {
            controls->selected_card_range = radio;
            radio->set_enabled(!legacy || radio->checked);
        }
        radio->on_changed = [this, controls, value](const bool checked) {
            if (!checked) return;
            range_ = value;
            if (controls->refresh_enabled) controls->refresh_enabled();
            refreshWarning();
            setApplyEnabled(true);
        };
        radio->set_layout_item({.row=index,.column=0U,.preferred={160.0F,22.0F}});
        range->add(radio);
    }
    range->set_layout_item({.row=0U,.column=0U,.preferred={170.0F,175.0F}}); root->add(range);
    auto kinds=makeGroupBox(messages->cardType.value,1U);
    const auto legacy_display = std::make_shared<std::optional<bool>>();
    const auto rebuild_kinds = [this, kinds, legacy_display, messages] {
        const bool legacy = summary_ && summary_->legacy();
        if (*legacy_display && **legacy_display == legacy) return;
        const bool format_change = legacy_display->has_value();
        *legacy_display = legacy;
        kinds->clear();
        const auto old_flags = std::array{target_skill_,target_item_,target_beast_,target_hand_};
        if (legacy) {
            const bool skill_only = target_skill_ && !target_item_ && !target_beast_ && !target_hand_;
            const bool item_only = !target_skill_ && target_item_ && !target_beast_;
            const bool beast_only = !target_skill_ && !target_item_ && target_beast_ && !target_hand_;
            if (item_only) target_hand_ = true;
            else if (!skill_only && !beast_only)
                target_skill_ = target_item_ = target_beast_ = target_hand_ = true;
        }
    if (legacy) {
        struct LegacyChoice { String text; int value; };
        int selected = 0;
        if (target_skill_ && !target_item_ && !target_beast_) selected = 1;
        else if (!target_skill_ && target_item_ && !target_beast_) selected = 2;
        else if (!target_skill_ && !target_item_ && target_beast_) selected = 3;
        const std::array<LegacyChoice,4U> choices{{
            {messages->effectCardTypeName(types::EffectCardType::All),0},
            {messages->effectCardTypeName(types::EffectCardType::Skill),1},
            {messages->effectCardTypeName(types::EffectCardType::Item),2},
            {messages->effectCardTypeName(types::EffectCardType::Beast),3}}};
        for (const auto choice : choices) {
            auto radio=std::make_shared<lib::RadioButton>(choice.text);
            radio->group="legacy-key-code-card-kind";
            radio->checked=selected==choice.value;
            radio->on_changed=[this,value=choice.value](bool checked){
                if(!checked)return;
                target_skill_=value==0||value==1;
                target_item_=value==0||value==2;
                target_beast_=value==0||value==3;
                target_hand_=value==0||value==2;
                refreshWarning();
                setApplyEnabled(true);
            };
            kinds->add(radio);
        }
    } else {
        const auto add_kind=[this,kinds](const String& text,bool& target){
            auto check=std::make_shared<lib::CheckBox>(text); check->checked=target;
            check->on_changed=[this,&target](bool value){target=value;refreshWarning();setApplyEnabled(true);}; kinds->add(check);
        };
        add_kind(messages->effectCardTypeName(types::EffectCardType::Skill),target_skill_);
        add_kind(messages->effectCardTypeName(types::EffectCardType::Item),target_item_);
        add_kind(messages->effectCardTypeName(types::EffectCardType::Beast),target_beast_);
        add_kind(messages->effectCardTypeName(types::EffectCardType::Hand),target_hand_);
    }

        if (format_change && old_flags != std::array{target_skill_,target_item_,target_beast_,target_hand_})
            setApplyEnabled(true);
        refreshTypedBody();
    };
    version_control_updates_.push_back(rebuild_kinds);
    rebuild_kinds();
    kinds->set_layout_item({.row=0U,.column=1U,.preferred={170.0F,175.0F}}); root->add(kinds);
    auto key_group=makeGroupBox(messages->keyCode.value,1U);
    const auto weak=weak_from_this();
    auto key_properties=chooserProperties(context_);
    // スキン・キー・文字の通知でこの副作用のないプロバイダーを再評価し
    // 、候補を最新にする。共通コンボはユーザーの編集中の値を保持する。
    const auto standards=[weak] {
        const auto owner=weak.lock();
        if(!owner || !owner->context().editor_props) return std::vector<String>{};
        // 通知時に現在のスキンを解決し、監視先の順序に依存しない。
        const auto current_skin=owner->context().findSkin(owner->summary().get());
        return ::cwxeditor::cwx::editor::gui::dwt::dprops::arraySnapshot(
            owner->context().editor_props->standardKeyCodes(
                current_skin ? current_skin->type() : String{}));
    };
    auto key=chooser::createKeyCodeCombo(chooserCommons(context_),summary_,nullptr,
        [weak] { const auto owner=weak.lock(); return owner && owner->isOpen(); },
        key_code_,false,{},standards,key_properties);
    const auto previous_text=key->on_text_changed;
    key->on_text_changed=[this,weak,previous_text](std::string_view value) {
        const auto keep=weak.lock(); if(!keep || !keep->isOpen())return;
        if(previous_text)previous_text(value);
        if(!isOpen())return;
        key_code_=String(value);refreshWarning();setApplyEnabled(true);
    };
    const auto previous_selection=key->on_selection_changed;
    key->on_selection_changed=[this,weak,weak_key=std::weak_ptr<lib::ComboBox>(key),previous_selection](auto index) {
        const auto keep=weak.lock(); if(!keep || !keep->isOpen())return;
        if(previous_selection)previous_selection(index);
        if(!isOpen())return;
        const auto key=weak_key.lock();
        if(key && index && *index<key->items.size()){key_code_=key->items[*index].value;refreshWarning();setApplyEnabled(true);}
    };
    key_group->add(key); key_group->set_layout_item({.row=1U,.column=0U,.column_span=2U,.preferred={350.0F,55.0F}}); root->add(key_group);
    auto result=makeGroupBox(messages->resultType.value,2U);
    result->add(std::make_shared<lib::Label>(messages->matchingConditionForKeyCode.value)); auto matching=controls->matching=std::make_shared<lib::ComboBox>();
    matching->items={{messages->matchingConditionForKeyCodeNormalDesc.value,"has"},
                     {messages->matchingConditionForKeyCodeInvertDesc.value,"not"}};
    matching->selected_index=matching_condition_==types::MatchingCondition::Has?0U:1U;
    matching->on_selection_changed=[this,controls](auto i){if(i){matching_condition_=*i?types::MatchingCondition::HasNot:types::MatchingCondition::Has;if(controls->refresh_enabled)controls->refresh_enabled();refreshWarning();setApplyEnabled(true);}}; result->add(matching);
    result->add(std::make_shared<lib::Label>(messages->matchedCard.value)); auto invert=controls->invert=std::make_shared<lib::ComboBox>();
    invert->items={{messages->resultTypeNormalDesc.value,"normal"},
                   {messages->resultTypeInvertDesc.value,"invert"}}; invert->selected_index=invert_result_?1U:0U;
    invert->on_selection_changed=[this,controls](auto i){if(i){invert_result_=*i==1;if(controls->refresh_enabled)controls->refresh_enabled();refreshWarning();setApplyEnabled(true);}}; result->add(invert);
    result->set_layout_item({.row=2U,.column=0U,.column_span=2U,.preferred={350.0F,82.0F}}); root->add(result);
    auto select=controls->select_card=std::make_shared<lib::CheckBox>(messages->selectFoundCard.value); select->checked=select_card_;
    select->on_changed=[this,controls](bool v){
        select_card_=v;
        if (controls->refresh_enabled) controls->refresh_enabled();
        refreshWarning();
        setApplyEnabled(true);
    }; select->set_layout_item({.row=3U,.column=0U,.column_span=2U}); root->add(select);
    controls->refresh_enabled=[this,controls]{
        const bool legacy = summary_ && summary_->legacy();
        const bool selected_card=range_==types::Range::SelectedCard;
        controls->selected_card_range->set_enabled(!legacy || selected_card);
        controls->matching->set_enabled(!legacy ||
            matching_condition_==types::MatchingCondition::HasNot);
        controls->invert->set_enabled(!legacy || invert_result_);
        controls->select_card->set_enabled(!selected_card &&
            (!legacy || select_card_) && !invert_result_);
    };
    version_control_updates_.push_back(controls->refresh_enabled);
    controls->refresh_enabled();
    installTypedBody(root,{345.0F,382.0F},true);
}

template <CType Type, bool EditValue, bool Field>
void CouponEventDialog<Type, EditValue, Field>::buildCouponFields() {
    coupon_view_.reset();
    coupon_fields_->clear();
    const auto weak_dialog = weak_from_this();
    const std::weak_ptr<const int> weak_coupon_lifetime = coupon_callback_lifetime_;
    const auto coupons = coupon_fields_;
    if (multi_mode_ && context_.editor_commons) {
        coupon_view_ = std::make_unique<couponview::CouponView<couponview::CVType::Branch>>(
            *context_.editor_commons, summary_, use_counter_, coupons);
        coupon_view_->couponNames(coupon_names_);
        coupon_view_->setupNameEditor([this](const auto& editor) {
            bindSpecialText(editor, [this] { return expand_sp_chars_; });
        });
        coupon_view_->setupInlineNameEditor([this](const auto& editor) {
            bindSpecialText(editor, [this] { return expand_sp_chars_; });
        });
        const auto table = coupon_view_->widget();
        const auto previous_pointer = table->on_pointer;
        table->on_pointer = [this, previous_pointer](lib::PointerEvent& event) {
            if (previous_pointer) previous_pointer(event);
            if (event.action != lib::PointerAction::Move || !coupon_view_ || !context_.editor_props) return;
            const auto row = coupon_view_->widget()->rowAt(event.position);
            const auto names = coupon_view_->couponNames();
            coupon_view_->toolTip(expand_sp_chars_ && row && *row < names.size()
                ? messageutils::createSPCharPreview(*context_.editor_props, summary_.get(), skin_, use_counter_.get(), names[*row], true)
                : String{});
        };
        coupon_view_->panel()->set_layout_item(
            {.preferred={270.0F,172.0F},.grow=1.0F});
        coupon_view_->modEvent.push_back([this] {
            if (coupon_view_) coupon_names_ = coupon_view_->couponNames();
            setApplyEnabled(true);
        });
        auto match=makeGridPanel(2U,4.0F,{}); for(const auto& [text,value]:std::array<std::pair<String,types::MatchingType>,2>{{{"全てに一致",types::MatchingType::And},{"どれか一つに一致",types::MatchingType::Or}}}){
            auto radio=std::make_shared<lib::RadioButton>(text);radio->group="coupon-match";radio->checked=matching_type_==value;
            radio->on_changed=[this,value](bool v){if(v){matching_type_=value;setApplyEnabled(true);}};match->add(radio);
        } coupons->add(match);
    } else {
        // DはここでcreateCouponComboを使い、シナリオ
        // に使用済みクーポンがなくてもスキン・標準のクーポンを含める。
        auto properties = chooserProperties(context_);
        properties.couponStandards = [this, weak_dialog, weak_coupon_lifetime](chooser::CouponComboType type) {
            [[maybe_unused]] const auto dialog_keep_alive = weak_dialog.lock();
            if(weak_coupon_lifetime.expired())return std::vector<String>{};
            dutils::EditorContext coupon_context;
            coupon_context.prop = context_.editor_props;
            coupon_context.skin = skin_.get();
            coupon_context.summary = summary_.get();
            return dutils::castCoupons(coupon_context, false, {},
                type == chooser::CouponComboType::GetLose, false);
        };
        chooser::setCouponFilter(properties,
            [prop = context_.prop](StringView value) {
                return prop && prop->sys().isCouponType(
                    String(value), features::CouponType::System);
            });
        constexpr auto combo_type = Type == CType::GetCoupon || Type == CType::LoseCoupon
            ? chooser::CouponComboType::GetLose : chooser::CouponComboType::AllCoupons;
        auto edit = chooser::createCouponCombo(chooserCommons(context_), summary_,
            use_counter_, nullptr, [weak_coupon_lifetime] { return !weak_coupon_lifetime.expired(); },
            combo_type, coupon_, false, properties);
        using CouponButtons = std::vector<std::pair<features::CouponType,
            std::shared_ptr<lib::RadioButton>>>;
        const auto type_buttons=std::make_shared<CouponButtons>();
        const std::weak_ptr<lib::ComboBox> weak_edit = edit;
        const std::weak_ptr<CouponButtons> weak_type_buttons = type_buttons;
        const auto refresh_types=[this, weak_dialog, weak_coupon_lifetime](const std::shared_ptr<CouponButtons>& buttons){
            [[maybe_unused]] const auto dialog_keep_alive = weak_dialog.lock();
            if(weak_coupon_lifetime.expired() || !isOpen() || !context_.prop || !buttons)return;
            const auto selected=context_.prop->sys().couponType(coupon_);
            for(const auto& [type,button]:*buttons){
                button->checked=type==selected;
                if(context_.services.gui)context_.services.gui->request_redraw(button);
                if(weak_coupon_lifetime.expired() || !isOpen())return;
            }
        };
        const auto previous_text_changed = edit->on_text_changed;
        edit->on_text_changed=[this, weak_dialog, weak_coupon_lifetime, type_buttons, refresh_types,
                               previous_text_changed](std::string_view value){
            [[maybe_unused]] const auto dialog_keep_alive = weak_dialog.lock();
            if(weak_coupon_lifetime.expired() || !isOpen())return;
            if (previous_text_changed) previous_text_changed(value);
            if(weak_coupon_lifetime.expired() || !isOpen())return;
            coupon_=String(value);
            refresh_types(type_buttons);
            if(weak_coupon_lifetime.expired() || !isOpen())return;
            setApplyEnabled(true);
        };
        const auto previous_selection_changed = edit->on_selection_changed;
        edit->on_selection_changed=[this, weak_dialog, weak_coupon_lifetime, weak_edit, type_buttons, refresh_types,
                                    previous_selection_changed](const auto index){
            [[maybe_unused]] const auto dialog_keep_alive = weak_dialog.lock();
            if(weak_coupon_lifetime.expired() || !isOpen())return;
            if (previous_selection_changed) previous_selection_changed(index);
            if(weak_coupon_lifetime.expired() || !isOpen())return;
            const auto combo = weak_edit.lock();
            if(combo && index && *index<combo->items.size()){
                coupon_=combo->items[*index].value;
                refresh_types(type_buttons);
                if(weak_coupon_lifetime.expired() || !isOpen())return;
                setApplyEnabled(true);
            }
        };
        edit->set_layout_item({.preferred={342.0F,23.0F},.grow=1.0F});
        bindSpecialText(edit, [this] { return expand_sp_chars_; });
        coupons->add(edit);
        const std::array<features::CouponType,5U> coupon_types{
            features::CouponType::Normal,features::CouponType::Hide,
            features::CouponType::Dur,features::CouponType::DurBattle,
            features::CouponType::System};
        for(const auto coupon_type:coupon_types){
            String label=context_.prop?context_.prop->requiredMsgs().couponTypeLongDesc(coupon_type):String{"ノーマル"};
            if(context_.prop){
                const auto& sys=context_.prop->sys();
                switch(coupon_type){
                case features::CouponType::Normal:break;
                case features::CouponType::Hide:label=replaceCouponMarker(std::move(label),sys.couponHide());break;
                case features::CouponType::Dur:label=replaceCouponMarker(std::move(label),sys.couponDur());break;
                case features::CouponType::DurBattle:label=replaceCouponMarker(std::move(label),sys.couponDurBattle());break;
                case features::CouponType::System:label=replaceCouponMarker(std::move(label),sys.couponSystem());break;
                }
            }
            auto radio=std::make_shared<lib::RadioButton>(std::move(label));
            radio->group="coupon-event-type";
            radio->checked=context_.prop?context_.prop->sys().couponType(coupon_)==coupon_type:
                         coupon_type==features::CouponType::Normal;
            radio->on_changed=[this, weak_dialog, weak_coupon_lifetime, weak_edit, weak_type_buttons,
                               coupon_type, refresh_types](bool checked){
                [[maybe_unused]] const auto dialog_keep_alive = weak_dialog.lock();
                const auto combo = weak_edit.lock();
                const auto buttons = weak_type_buttons.lock();
                if(!checked || weak_coupon_lifetime.expired() || !isOpen() || !combo || !buttons)return;
                if(context_.prop)
                    coupon_=context_.prop->sys().convCoupon(
                        coupon_,coupon_type,false);
                combo->edit_text=coupon_;
                if(weak_coupon_lifetime.expired() || !isOpen())return;
                refresh_types(buttons);
                if(weak_coupon_lifetime.expired() || !isOpen())return;
                setApplyEnabled(true);
                if(weak_coupon_lifetime.expired() || !isOpen())return;
                if(context_.services.gui)
                    context_.services.gui->request_redraw(combo);
            };
            type_buttons->push_back({coupon_type,radio});
            coupons->add(radio);
        }
    }
}

template <CType Type, bool EditValue, bool Field>
void CouponEventDialog<Type, EditValue, Field>::refDataVersion() {
    range_.refDataVersion();
    const bool multi = Type == CType::BranchCoupon && (!summary_ || !summary_->legacy());
    if (multi != multi_mode_ && coupon_fields_) {
        if (multi) {
            coupon_names_ = coupon_.empty() ? lib::Array<String>{} : lib::Array<String>{coupon_};
            // DのcreateCouponViewは、再生成した複数クーポン編集部をAndへ戻す。
            matching_type_ = types::MatchingType::And;
        }
        else {
            if (coupon_view_) coupon_names_ = coupon_view_->couponNames();
            coupon_ = coupon_names_.empty() ? String{} : coupon_names_.front();
        }
        multi_mode_ = multi;
        buildCouponFields();
        refreshTypedBody();
    }
    EventDialog::refDataVersion();
}

template <CType Type, bool EditValue, bool Field>
bool CouponEventDialog<Type, EditValue, Field>::openCWXPath(
    String pathValue, const bool activate) {
    if (path::cpcategory(pathValue) == "coupon") {
        if (coupon_view_) {
            const auto index = path::cpindex(pathValue);
            pathValue = path::cpbottom(pathValue);
            ::cwxeditor::cwx::editor::gui::dwt::dutils::forceFocus(
                std::static_pointer_cast<lib::Widget>(coupon_view_->widget()),
                activate, context_.services);
            return coupon_view_->select(static_cast<int>(index)) &&
                   path::cpempty(pathValue);
        }
        if (coupon_fields_)
            return path::cpempty(path::cpbottom(pathValue));
    }
    return EventDialog::openCWXPath(std::move(pathValue), activate);
}

template <CType Type, bool EditValue, bool Field>
void CouponEventDialog<Type, EditValue, Field>::buildControls() {
    constexpr bool holder_dialog = Type == CType::GetCoupon ||
                                   Type == CType::LoseCoupon;
    auto root=makeGridPanel(holder_dialog ? 1U : 2U,5.0F,{5,5,5,5});
    auto left=makeGridPanel(1U,4.0F,{});
    std::vector<std::pair<types::Range,String>> range_values{
        {types::Range::Selected,"現在選択中のメンバ"},
        {types::Range::Random,"パーティの誰か一人"},
        {types::Range::Party,"パーティの全員"}};
    if constexpr (Field) {
        range_values.push_back({types::Range::Field,"フィールド全体"});
        range_values.push_back({types::Range::Npc,"同行キャスト"});
    }
    if (holder_dialog)
        range_values.push_back({types::Range::CouponHolder,"称号所有者"});

    auto range=makeGroupBox("適用範囲",1U);
    const std::weak_ptr<const int> weak_coupon_lifetime = coupon_callback_lifetime_;
    // バックエンドがダイアログ終了後も範囲ラジオを保持す
    // る場合がある。コールバック内の保持先操作部は弱参照
    // だけとし、クーポン選択部の通知購読を延命しない。
    auto holder_controls = std::make_shared<std::vector<std::weak_ptr<lib::Widget>>>();
    const std::weak_ptr<lib::GuiBackend> weak_gui = context_.services.gui;
    auto refresh_holder = [this, weak_coupon_lifetime, holder_controls,
                           weak_gui] {
        if (weak_coupon_lifetime.expired()) return;
        const bool enabled = range_.range() == types::Range::CouponHolder;
        if (weak_coupon_lifetime.expired()) return;
        for (const auto& weak_control : *holder_controls) {
            if (weak_coupon_lifetime.expired()) return;
            if (const auto control = weak_control.lock()) {
                control->set_enabled(enabled);
                if (const auto gui = weak_gui.lock()) gui->request_redraw(control);
            }
        }
    };
    version_control_updates_.push_back(refresh_holder);
    for (std::size_t index=0; index<range_values.size(); ++index) {
        const auto [value,text]=range_values[index];
        auto radio=std::make_shared<lib::RadioButton>(text);
        radio->group="coupon-event-range";
        radio->checked=range_.range()==value;
        const auto refresh_range = [this, radio, value] {
            const bool legacy = summary_ && summary_->legacy();
            radio->set_enabled(!legacy || (value != types::Range::Npc && value != types::Range::CouponHolder));
            radio->checked = range_.range() == value;
        };
        refresh_range();
        version_control_updates_.push_back(refresh_range);
        radio->on_changed=[this,value,weak_coupon_lifetime,refresh_holder](bool checked){
            if (weak_coupon_lifetime.expired()) return;
            if(!checked || (summary_ && summary_->legacy() &&
                (value == types::Range::Npc || value == types::Range::CouponHolder))) return;
            range_.setRange(value);
            if (weak_coupon_lifetime.expired()) return;
            refresh_holder();
            if (weak_coupon_lifetime.expired()) return;
            setApplyEnabled(true);
        };
        range->add(radio);
    }
    if (holder_dialog) {
        auto holder_row=makeGridPanel(2U,3.0F,{});
        auto holder_properties = chooserProperties(context_);
        holder_properties.couponStandards = [prop = context_.editor_props, common = context_.editor_commons,
                                              summ = summary_](chooser::CouponComboType type) {
            dutils::EditorContext coupon_context;
            coupon_context.prop = prop;
            const auto skin = common ? common->skin() : SkinPtr{};
            coupon_context.skin = skin.get();
            coupon_context.summary = summ.get();
            return dutils::castCoupons(coupon_context,
                type == chooser::CouponComboType::Talker, {},
                type == chooser::CouponComboType::GetLose,
                type == chooser::CouponComboType::Valued);
        };
        chooser::setCouponFilter(holder_properties,
            [prop = context_.prop](StringView value) {
                return prop && prop->sys().isCouponType(
                    String(value), features::CouponType::System);
            });
        const auto weak_dialog = weak_from_this();
        auto holder = chooser::createCouponCombo(chooserCommons(context_), summary_,
            use_counter_, nullptr, [weak_coupon_lifetime] { return !weak_coupon_lifetime.expired(); },
            chooser::CouponComboType::AllCoupons, range_.holdingCoupon(), false,
            holder_properties);
        const std::weak_ptr<lib::ComboBox> weak_holder = holder;
        const auto previous_text_changed = holder->on_text_changed;
        holder->on_text_changed=[this, weak_dialog, weak_coupon_lifetime, previous_text_changed](StringView value){
            [[maybe_unused]] const auto dialog_keep_alive = weak_dialog.lock();
            if(weak_coupon_lifetime.expired() || !isOpen())return;
            if (previous_text_changed) previous_text_changed(value);
            if(weak_coupon_lifetime.expired() || !isOpen())return;
            range_.setCoupon(String(value));setApplyEnabled(true);
        };
        const auto previous_selection_changed = holder->on_selection_changed;
        holder->on_selection_changed=[this, weak_dialog, weak_coupon_lifetime, weak_holder,
                                      previous_selection_changed](const auto index){
            [[maybe_unused]] const auto dialog_keep_alive = weak_dialog.lock();
            if(weak_coupon_lifetime.expired() || !isOpen())return;
            if (previous_selection_changed) previous_selection_changed(index);
            if(weak_coupon_lifetime.expired() || !isOpen())return;
            const auto combo = weak_holder.lock();
            if(combo && index&&*index<combo->items.size()){
                range_.setCoupon(combo->items[*index].value);
                setApplyEnabled(true);
            }
        };
        holder->set_layout_item({.row=0U,.column=0U,.preferred={96.0F,23.0F},.grow=1.0F});
        holder_row->add(holder);
        auto holder_kind=std::make_shared<lib::ComboBox>();
        bindHoldingCouponKind(context_, holder, holder_kind);
        holder_kind->set_layout_item({.row=0U,.column=1U,.preferred={58.0F,23.0F}});
        holder_row->add(holder_kind);
        holder_controls->emplace_back(holder);
        holder_controls->emplace_back(holder_kind);
        range->add(holder_row);
    }
    range->set_layout_item({.row=0U,.column=0U,.preferred={178.0F,holder_dialog?142.0F:190.0F}});
    left->add(range);
    refresh_holder();
    if constexpr(Type==CType::BranchCoupon){
        auto result=makeGroupBox("成功条件",1U);
        auto invert=std::make_shared<lib::ComboBox>();
        invert->items={{"所有している","normal"},{"所有していない","invert"}};
        invert->selected_index=invert_result_?1U:0U;
        const bool legacy=summary_&&summary_->legacy();
        invert->set_enabled(!legacy||invert_result_);
        version_control_updates_.push_back([this,invert] {
            invert->set_enabled(!summary_ || !summary_->legacy() || invert_result_);
        });
        invert->on_selection_changed=[this,invert](const auto index){
            const bool legacy=summary_&&summary_->legacy();
            if(!index)return;
            invert_result_=*index==1U;
            invert->set_enabled(!legacy||invert_result_);
            setApplyEnabled(true);
        };
        result->add(invert);left->add(result);
    }
    if constexpr(EditValue){
        auto score=makeGroupBox("得点",2U);
        const auto coupon_max=context_.vars?static_cast<double>(context_.vars->couponValueMax.value):999.0;
        auto value=std::make_shared<lib::NumberEdit>();
        value->minimum=-coupon_max;value->maximum=coupon_max;value->value=coupon_value_;
        value->on_changed=[this](double v){coupon_value_=static_cast<int>(v);setApplyEnabled(true);};
        value->set_layout_item({.row=0U,.column=0U,.preferred={55.0F,23.0F}});
        score->add(value);
        score->add(std::make_shared<lib::Label>("(-" + std::to_string(static_cast<int>(coupon_max)) + "～" + std::to_string(static_cast<int>(coupon_max)) + ")"));
        score->set_layout_item({.preferred={178.0F,62.0F}});
        left->add(score);
    }
    left->set_layout_item({.row=0U,.column=0U,.preferred={178.0F,holder_dialog?202.0F:240.0F}});
    auto coupons=makeGroupBox("クーポン名",1U);
    coupon_fields_ = makeGridPanel(1U, 4.0F, {});
    coupons->add(coupon_fields_);
    buildCouponFields();
    const auto& messages = contentDialogMessages(context_);
    auto expand=std::make_shared<lib::CheckBox>(messages.expandSPChars.value);
    String hint;
    for (const char character : messages.expandSPCharsHint.value) {
        if (character == '&') hint += "&&";
        else hint.push_back(character);
    }
    expand->set_tooltip(std::move(hint));
    expand->checked=expand_sp_chars_;
    const bool legacy=summary_&&summary_->legacy();
    expand->set_enabled(!legacy||expand_sp_chars_);
    version_control_updates_.push_back([this,expand]{expand->set_enabled(!summary_||!summary_->legacy()||expand_sp_chars_);});
    expand->on_changed=[this,expand](bool v){expand_sp_chars_=v;expand->set_enabled(!summary_||!summary_->legacy()||expand_sp_chars_);setApplyEnabled(true);};
    coupons->add(expand);
    coupons->set_layout_item({.row=0U,.column=1U,.preferred={365.0F,202.0F},.grow=1.0F});
    if constexpr (holder_dialog) {
        coupon_split_ = std::make_shared<lib::SplitView>();
        coupon_split_->orientation = lib::Orientation::Horizontal;
        coupon_split_->set_layout_item({.horizontal=lib::Align::Stretch,
                                        .vertical=lib::Align::Stretch,
                                        .preferred={548.0F,202.0F},
                                        .grow=1.0F});
        coupon_split_->add(left);
        coupon_split_->add(coupons);
        const auto sash_left = context_.vars
            ? context_.vars->couponEventL.value : 1;
        const auto sash_right = context_.vars
            ? context_.vars->couponEventR.value : 2;
        coupon_split_->weights = {static_cast<float>(std::max(1, sash_left)),
                                  static_cast<float>(std::max(1, sash_right))};
        coupon_split_->on_weights_changed = [this](const std::vector<float>& weights) {
            if (!context_.vars || weights.size() < 2U) return;
            context_.vars->couponEventL.value = static_cast<std::int32_t>(
                std::max(1.0F, std::round(weights[0])));
            context_.vars->couponEventR.value = static_cast<std::int32_t>(
                std::max(1.0F, std::round(weights[1])));
        };
        root->add(coupon_split_);
    } else {
        root->add(left);
        root->add(coupons);
    }
    installTypedBody(root,{560.0F,holder_dialog?270.0F:310.0F},true);
    if (context_.editor_props) {
        const auto restore=[this](const auto& geometry) {
            auto bounds=widget_->bounds();
            if (geometry.width()>0) bounds.width=static_cast<float>(geometry.width());
            if (geometry.height()>0) bounds.height=static_cast<float>(geometry.height());
            widget_->set_bounds(bounds);
        };
        if (multi_mode_) restore(context_.editor_props->flex().multiCouponEvtDlg);
        else restore(context_.editor_props->flex().couponEvtDlg);
        closeEvent.push_back([this] {
            const auto bounds=lib::authored_window_bounds(*widget_);
            const auto save=[&](auto& geometry) {
                geometry.width(static_cast<std::int32_t>(bounds.width));
                geometry.height(static_cast<std::int32_t>(bounds.height));
            };
            // ダイアログを開いたまま、シナリオのバージョン変更で単一・複数モードが
            // 切り替わる場合がある。Dはその時点で保存先のDSizeを切り替える。
            if (multi_mode_) save(context_.editor_props->flex().multiCouponEvtDlg);
            else save(context_.editor_props->flex().couponEvtDlg);
        });
    }
}

template <CType Type, bool EditValue, bool Field>
void CouponEventDialog<Type, EditValue, Field>::refreshWarning() {
    refreshSpecialTextPreviews();
    WarningList result;
    const bool classic = summary_ && summary_->legacy();
    const String wsn_version = summary_
        ? summary_->dataVersion()
        : String(versioninfo::LATEST_VERSION);
    const String target_version = context_.editor_props
        ? String(context_.editor_props->var().targetVersion.value)
        : (context_.vars ? String(context_.vars->targetVersion.value) : String{});
    const auto supports_wsn = [&](const StringView version) {
        if (context_.editor_props)
            return context_.editor_props->isTargetVersion(summary_.get(), version);
        return context_.prop && context_.prop->isTargetVersion(
            classic, wsn_version, version);
    };
    const auto supports_classic = [&](const StringView version) {
        if (context_.editor_props)
            return context_.editor_props->targetVersion(summary_.get(), version);
        return context_.prop && context_.prop->targetVersion(version, target_version);
    };
    const auto append_text_warnings = [&](const std::span<const String> texts) {
        if (!context_.prop || !skin_) return;
        const auto values = dTextWarnings2(
            *context_.prop, *skin_, summary_.get(), use_counter_.get(),
            target_version, texts);
        result.insert(result.end(), values.begin(), values.end());
    };

    if (coupon_view_) {
        const auto names = coupon_view_->couponNames();
        if (!supports_wsn("2") && names.size() > 1U && context_.prop)
            result.push_back(context_.prop->requiredMsgs().warningBranchCouponMulti.value);
        const auto view_warnings = coupon_view_->warnings();
        result.insert(result.end(), view_warnings.begin(), view_warnings.end());
        if (expand_sp_chars_) {
            if (!supports_wsn("4") && context_.prop)
                result.push_back(
                    context_.prop->requiredMsgs().warningExpandSPCharsInCoupon.value);
            append_text_warnings(names);
        }
    } else if (context_.prop) {
        const bool get_lose = Type == CType::GetCoupon || Type == CType::LoseCoupon;
        const auto coupon_warnings = warning::couponWarnings(
            *context_.prop, classic, wsn_version, target_version, coupon_,
            get_lose, contentDialogMessages(context_).couponName.value);
        result.insert(result.end(), coupon_warnings.begin(), coupon_warnings.end());
        if (expand_sp_chars_) {
            if (!supports_wsn("4"))
                result.push_back(
                    context_.prop->requiredMsgs().warningExpandSPCharsInCoupon.value);
            const std::array<String, 1> texts{coupon_};
            append_text_warnings(texts);
        }
    }

    if constexpr (Field) {
        if (summary_ && classic && !supports_classic("1.30") &&
            range_.range() == types::Range::Field && context_.prop)
            result.push_back(
                context_.prop->requiredMsgs().warningBranchCouponAtField.value);
        if (range_.range() == types::Range::Npc && !supports_wsn("5") &&
            context_.prop)
            result.push_back(
                context_.prop->requiredMsgs().warningBranchCouponAtNpc.value);
    }
    if constexpr (Type == CType::BranchCoupon) {
        if (!supports_wsn("4") && invert_result_ && context_.prop)
            result.push_back(context_.prop->requiredMsgs().warningInvertResult.value);
    }
    const auto& range_warnings = range_.warnings();
    result.insert(result.end(), range_warnings.begin(), range_warnings.end());
    replaceWarnings(std::move(result));
}

template void CouponEventDialog<CType::BranchCoupon,false,true>::buildControls();
template void CouponEventDialog<CType::GetCoupon,true,false>::buildControls();
template void CouponEventDialog<CType::LoseCoupon,false,false>::buildControls();
template void CouponEventDialog<CType::BranchCoupon,false,true>::refreshWarning();
template void CouponEventDialog<CType::GetCoupon,true,false>::refreshWarning();
template void CouponEventDialog<CType::LoseCoupon,false,false>::refreshWarning();

template <CType Type, bool Gossip>
void OneTextEventDialog<Type, Gossip>::buildControls() {
    const auto& messages = contentDialogMessages(context_);
    auto root=makeGridPanel(1U,4.0F,{6,6,6,6});
    auto group=makeGroupBox(Gossip ? messages.gossipName.value : messages.endName.value,1U);
    auto comm = chooserCommons(context_);
    auto properties = chooserProperties(context_);
    if constexpr (Gossip)
        name_combo_ = chooser::createGossipCombo(comm, summary_, nullptr, [this]{return true;}, text_, properties);
    else
        name_combo_ = chooser::createCompleteStampCombo(comm, summary_, nullptr, [this]{return true;}, text_, properties);
    auto combo = name_combo_;
    combo->set_layout_item({.preferred = {context_.vars ? static_cast<float>(context_.vars->nameWidth.value) : 300.0F, 0.0F}, .grow = 1.0F});
    const auto previous_text_changed = combo->on_text_changed;
    combo->on_text_changed=[this,previous_text_changed](std::string_view value){
        if (previous_text_changed) previous_text_changed(value);
        text_=String(value);setApplyEnabled(true);
    };
    const auto previous_selection_changed = combo->on_selection_changed;
    combo->on_selection_changed=[this,combo,previous_selection_changed](auto i){
        if (previous_selection_changed) previous_selection_changed(i);
        if(i&&*i<combo->items.size()){text_=combo->items[*i].value;setApplyEnabled(true);}
    };group->add(combo);
    if constexpr(Gossip){
        bindSpecialText(combo, [this] { return expand_sp_chars_; });
        auto expand=std::make_shared<lib::CheckBox>(messages.expandSPChars.value);
        String hint;
        for (const char character : messages.expandSPCharsHint.value) {
            if (character == '&') hint += "&&";
            else hint.push_back(character);
        }
        expand->set_tooltip(std::move(hint));
        expand->checked=expand_sp_chars_;
        const bool legacy=summary_&&summary_->legacy();
        expand->set_enabled(!legacy||expand_sp_chars_);
        version_control_updates_.push_back([this,expand]{expand->set_enabled(!summary_||!summary_->legacy()||expand_sp_chars_);});
    expand->on_changed=[this,expand](bool v){expand_sp_chars_=v;expand->set_enabled(!summary_||!summary_->legacy()||expand_sp_chars_);setApplyEnabled(true);};
        group->add(expand);
    }
    root->add(group);installTypedBody(root,{Gossip?360.0F:348.0F,Gossip?155.0F:130.0F},true);
    if (context_.editor_props) {
        if constexpr (Gossip) bindDialogGeometry(context_.editor_props->flex().gossipEvtDlg);
        else bindDialogGeometry(context_.editor_props->flex().inputEvtDlg);
    }
}

template void OneTextEventDialog<CType::BranchGossip,true>::buildControls();
template void OneTextEventDialog<CType::GetGossip,true>::buildControls();
template void OneTextEventDialog<CType::LoseGossip,true>::buildControls();
template void OneTextEventDialog<CType::BranchCompleteStamp,false>::buildControls();
template void OneTextEventDialog<CType::GetCompleteStamp,false>::buildControls();
template void OneTextEventDialog<CType::LoseCompleteStamp,false>::buildControls();

template <CType Type, bool Gossip>
void OneTextEventDialog<Type, Gossip>::refreshWarning() {
    refreshSpecialTextPreviews();
    WarningList result;
    if (context_.prop) {
        const auto& messages = contentDialogMessages(context_);
        const auto& label = Gossip ? messages.gossipName.value
                                   : messages.endName.value;
        result = warning::sjisWarnings(*context_.prop, summary_.get(), text_, label);
    }
    if constexpr (Gossip) {
        if (expand_sp_chars_) {
            if (context_.editor_props &&
                !context_.editor_props->isTargetVersion(summary_.get(), "4"))
                result.push_back(context_.editor_props->requiredMsgs()
                                     .warningExpandSPCharsInGossip.value);
            if (context_.prop && skin_) {
                const String target_version = context_.editor_props
                    ? String(context_.editor_props->var().targetVersion.value)
                    : (context_.vars
                           ? String(context_.vars->targetVersion.value) : String{});
                const std::array<String, 1> texts{text_};
                const auto text_warnings = dTextWarnings2(
                    *context_.prop, *skin_, summary_.get(), use_counter_.get(),
                    target_version, texts);
                result.insert(result.end(), text_warnings.begin(),
                              text_warnings.end());
            }
        }
    }
    replaceWarnings(std::move(result));
}
template void OneTextEventDialog<CType::BranchGossip,true>::refreshWarning();
template void OneTextEventDialog<CType::GetGossip,true>::refreshWarning();
template void OneTextEventDialog<CType::LoseGossip,true>::refreshWarning();
template void OneTextEventDialog<CType::BranchCompleteStamp,false>::refreshWarning();
template void OneTextEventDialog<CType::GetCompleteStamp,false>::refreshWarning();
template void OneTextEventDialog<CType::LoseCompleteStamp,false>::refreshWarning();

void BranchMultiCouponDialog::buildControls() {
    const auto& messages = contentDialogMessages(context_);
    auto root=makeGridPanel(2U,5.0F,{6,6,6,6});
    auto range=makeGroupBox(messages.judgeTarget.value,1U);
    // 新しい監視先を追加せず、ダイアログの既存のバージョン通知を使う。
    version_control_updates_.push_back([this] { range_.refDataVersion(); });
    for (const auto value : {types::Range::Selected, types::Range::Random,
                              types::Range::Party, types::Range::Field, types::Range::Npc}) {
        const auto text = messages.rangeName(value);
        auto radio=std::make_shared<lib::RadioButton>(text);
        radio->group="range";
        const auto refresh = [this, radio, value] {
            radio->set_enabled(value != types::Range::Npc || !summary_ || !summary_->legacy());
            radio->checked=range_.range()==value;
        };
        refresh();
        version_control_updates_.push_back(refresh);
        radio->on_changed=[this,value](bool checked) {
            if (!checked || (value == types::Range::Npc && summary_ && summary_->legacy())) return;
            range_.setRange(value);setApplyEnabled(true);
        };
        range->add(radio);
    }
    root->add(range);
    auto special=makeGroupBox(messages.spChars.value,1U);
    auto expand=std::make_shared<lib::CheckBox>(messages.expandSPChars.value);
    String hint;
    for (const char character : messages.expandSPCharsHint.value) {
        if (character == '&') hint += "&&";
        else hint.push_back(character);
    }
    expand->set_tooltip(std::move(hint));
    expand->checked=expand_sp_chars_;
    // Dは旧形式のMultiCouponでも、このチェック欄を意図的に許
    // 可する。通常の単一・真偽のクーポンダイアログは別の条件を維持する。
    expand->on_changed=[this](bool v){expand_sp_chars_=v;setApplyEnabled(true);};
    special->add(expand);root->add(special);
    installTypedBody(root,{325.0F,205.0F},true);
}

EffectDialog::EffectDialog(Context context, SummaryPtr summary, Parent parent,
                           ContentPtr parentContent, ContentPtr content,
                           UseCounterPtr useCounter, SkinPtr skin)
    : EventDialog(std::move(context), std::move(summary), CType::Effect,
                  std::move(parent), std::move(parentContent),
                  std::move(content), std::move(skin),
                  std::move(useCounter)),
      range_(context_, summary_, use_counter_,
             {types::Range::Selected, types::Range::Random,
              types::Range::Party, types::Range::CouponHolder,
              types::Range::CardTarget},
             {}, CType::Effect, draft_) {
    if (draft_) {
        loadDraft();
    } else if (context_.prop) {
        holding_coupon_ = context_.prop->sys().effectTargetCoupon();
        range_.setCoupon(holding_coupon_);
    }
}

void EffectDialog::loadDraft() {
    // Contentの共有項目の記述子から、ダイアログ所有の一覧をコピーする。その後MotionView::motions
    // が各Motionを複製し、独立に編集する。
    const auto draftMotions = draft_->motions();
    motions_.assign(draftMotions.begin(), draftMotions.end());
    signed_level_ = draft_->signedLevel();
    initial_effect_ = draft_->initialEffect();
    initial_sound_path_ = draft_->initialSoundPath();
    initial_sound_volume_ = draft_->initialSoundVolume();
    initial_sound_loop_count_ = draft_->initialSoundLoopCount();
    initial_sound_channel_ = draft_->initialSoundChannel();
    initial_sound_fade_in_ = draft_->initialSoundFadeIn();
    sound_path_ = draft_->soundPath();
    sound_volume_ = draft_->soundVolume();
    sound_loop_count_ = draft_->soundLoopCount();
    sound_channel_ = draft_->soundChannel();
    sound_fade_in_ = draft_->soundFadeIn();
    success_rate_ = draft_->successRate();
    effect_type_ = draft_->effectType();
    resist_ = draft_->resist();
    card_visual_ = draft_->cardVisual();
    holding_coupon_ = draft_->holdingCoupon();
    ref_ability_ = draft_->refAbility();
    physical_ = draft_->physical();
    mental_ = draft_->mental();
    ignite_ = draft_->ignite();
    key_codes_ = draft_->keyCodes();
    card_speed_ = draft_->cardSpeed();
    override_card_speed_ = draft_->overrideCardSpeed();
    absorb_to_ = draft_->absorbTo();
    range_.setRange(draft_->range());
    range_.setCoupon(holding_coupon_);
}

void EffectDialog::refreshMotionTable() {
    if (motion_view_) {
        motions_ = motion_view_->motions();
        motion_table_ = motion_view_->table();
    }
    if (!motion_table_) return;
    if (context_.services.gui) context_.services.gui->request_redraw(motion_table_);
}

void EffectDialog::buildControls() {
    const auto makeGrid = [](const std::size_t columns, const float spacing = 5.0F) {
        auto panel = std::make_shared<lib::Panel>();
        auto layout = std::make_shared<lib::GridLayout>();
        layout->columns = columns;
        layout->spacing = spacing;
        layout->padding = {5.0F, 5.0F, 5.0F, 5.0F};
        panel->set_layout(layout);
        return panel;
    };
    const auto makeGroup = [](String title, const std::size_t columns) {
        auto group = std::make_shared<lib::GroupBox>();
        group->title = std::move(title);
        auto layout = std::make_shared<lib::GridLayout>();
        layout->columns = columns;
        layout->spacing = 4.0F;
        layout->padding = {7.0F, 12.0F, 7.0F, 6.0F};
        group->set_layout(layout);
        return group;
    };
    const auto addLabel = [](const std::shared_ptr<lib::Container>& parent,
                             String text, std::size_t row, std::size_t column,
                             std::size_t span = 1) {
        auto label = std::make_shared<lib::Label>(std::move(text));
        label->set_layout_item({.row = row, .column = column,
                                .column_span = span,
                                .preferred = {0.0F, 19.0F}});
        parent->add(label);
        return label;
    };

    auto rootTabs = std::make_shared<lib::TabView>();

    // 効果: D版と同じカテゴリツールバーを、完全なMotionViewの上に配置する。
    auto effectPage = makeGrid(1);
    motionview::Commons motion_commons;
    motion_commons.services = context_.services;
    motion_commons.useCounter = use_counter_;
    motion_commons.refreshToolBar = context_.refreshToolBar;
    auto motion_props = motionProperties(context_);
    motion_props.editorProps = context_.editor_props;
    motion_props.skin = skin_.get();
    motion_props.dialogOwner = widget_;
    const auto element_cache = std::make_shared<dprops::ElementImageCache>();
    motion_props.elementIcon = [this, element_cache](const types::Element element) {
        if (!context_.editor_props) return lib::ResourceId{};
        return context_.editor_props->elementImage(context_.editor_props->elementOverrides(
            skin_ ? skin_->type() : String{}), element, *element_cache);
    };
    motion_props.elementText = [this](const types::Element element) {
        if (!context_.editor_props) return types::fromElement(element);
        const auto overrides = context_.editor_props->elementOverrides(skin_ ? skin_->type() : String{});
        const auto found = overrides.find(element);
        return found != overrides.end() && !found->second.name.empty() ? found->second.name
            : context_.editor_props->requiredMsgs().elementName(element);
    };
    motion_props.elementTooltip = [this](const types::Element element) -> String {
        if (!context_.editor_props) return {};
        const auto& messages = context_.editor_props->requiredMsgs();
        if (element == types::Element::All) return messages.elementEffectiveAll.value;
        if (element == types::Element::Fire || element == types::Element::Ice)
            return messages.elementWeaknessOrResist.value;
        auto target = element == types::Element::Health ? messages.undead.value
            : element == types::Element::Mind ? messages.automaton.value
            : element == types::Element::Miracle ? messages.unholy.value : messages.constructure.value;
        const auto overrides = context_.editor_props->elementOverrides(skin_ ? skin_->type() : String{});
        if (const auto found = overrides.find(element);
            found != overrides.end() && !found->second.targetType.empty()) target = found->second.targetType;
        return ::cwxeditor::cwx::utils::tryFormat(
            element == types::Element::Health || element == types::Element::Mind
                ? messages.elementNoEffective.value : messages.elementEffective.value, target);
    };
    motionview::bindUseEventHost(motion_props, context_.editor_commons, context_.editor_props);
    if (context_.vars) {
        motion_props.etc.linkCard = context_.vars->linkCard.value;
        motion_props.linkCardProvider = [vars = context_.vars] { return vars->linkCard.value; };
    }
    motion_props.openBeastCard = [this](
        Summary* summary, const lib::Owner<path::CWXPath>& owner,
        const motionview::BeastPtr& card, const bool read_only,
        const StringView nested_path, const bool shell_activate,
        lib::Action applying, lib::Action applied, lib::Action closed) {
        return context_.editBeastCard && context_.editBeastCard(
            summary, owner, card, read_only, nested_path, shell_activate,
            std::move(applying), std::move(applied), std::move(closed));
    };
    motion_props.openFilePath = [this](const StringView path, const bool activate,
                                     const bool select) {
        return context_.editor_commons &&
            context_.editor_commons->openFilePath(String{path}, activate, select);
    };
    motion_props.openCWXPath = [this](const StringView value,
                                      const bool activate) {
        return EventDialog::openExternalCWXPath(String(value), activate);
    };
    lib::Owner<path::CWXPath> motion_owner;
    if (parent_content_)
        motion_owner = std::static_pointer_cast<path::CWXPath>(parent_content_);
    motion_view_ = std::make_unique<motionview::MotionView>(
        std::move(motion_commons), std::move(motion_props), summary_,
        std::move(motion_owner), use_counter_);
    if (context_.editor_commons) {
        motion_ref_beast_connection_ = context_.editor_commons->refBeast.add(
            [this](const commons::BeastCardPtr& value) {
                if (motion_view_) motion_view_->refBeast(value);
            });
        motion_del_beast_connection_ = context_.editor_commons->delBeast.add(
            [this](const commons::CWXPathPtr& owner,
                   const commons::BeastCardPtr& value) {
                if (motion_view_) motion_view_->delBeast(owner.get(), value);
            });
        motion_ref_image_scale_connection_ =
            context_.editor_commons->refImageScale.add([this] {
                if (!motion_view_ || !context_.editor_props) return;
                motion_view_->refImageScale(static_cast<std::int32_t>(
                    context_.editor_props->imageScalePercent()));
            });
        motion_ref_card_image_status_connection_ =
            context_.editor_commons->refCardImageStatus.add([this] {
                if (motion_view_) motion_view_->refCardImageStatus();
            });
        motion_ref_element_connection_ = context_.editor_commons->refElementOverrides.add([this] {
            if (motion_view_) motion_view_->refSkin(skin_.get());
        });
    }
    auto motion_control = motion_view_->createControl();
    motion_view_->motions(motions_);
    motion_mod_connection_ = motion_view_->modelChanged.connect([this] {
        refreshMotionTable();
        setApplyEnabled(true);
    });
    motion_warning_connection_ = motion_view_->warningEvent.connect([this] {
        refreshEffectEnabled();
    });
    motion_control->set_layout_item({.row = 0, .column = 0,
                                     .preferred = {478.0F, 414.0F},
                                     .grow = 1.0F,
                                     .constrain_minimum_to_preferred = true});
    effectPage->add(motion_control);
    motion_table_ = motion_view_->table();
    refreshMotionTable();

    // 設定タブ。
    auto settingsPage = makeGrid(2);
    auto leftSettings = makeGrid(1);
    // ALLの左設定列は自然幅を保つ。広げたダイ
    // アログの余りは右列へ渡し、レベル・属性グル
    // ープを伸ばしてラジオの横に空白を作らない。
    const bool all_settings = lib::editor_window_host_options().all_mode;
    leftSettings->set_layout_item({.row = 0, .column = 0,
                                   .horizontal = all_settings ? lib::Align::Start
                                                              : lib::Align::Stretch,
                                   .preferred = {224.0F, 390.0F},
                                   .grow = all_settings ? 0.0F : 1.0F,
                                   .constrain_minimum_to_preferred = true});
    auto levelGroup = makeGroup("対象レベル", 2);
    levelGroup->set_layout_item({.row = 0, .column = 0,
                                 .preferred = {176.0F, 56.0F}});
    signed_level_control_ = std::make_shared<lib::NumberEdit>();
    const int effect_level_max = context_.vars ? context_.vars->castLevelMax.value : 99;
    signed_level_control_->minimum = -effect_level_max;
    signed_level_control_->maximum = effect_level_max;
    signed_level_control_->value = signed_level_;
    signed_level_control_->on_changed = [this](double value) {
        signed_level_ = static_cast<int>(value);
        setApplyEnabled(true);
    };
    signed_level_control_->set_layout_item({.row = 0, .column = 0,
                                            .preferred = {88.0F, 24.0F}});
    levelGroup->add(signed_level_control_);
    addLabel(levelGroup, "(-" + std::to_string(effect_level_max) + "～" + std::to_string(effect_level_max) + ")", 0, 1);
    leftSettings->add(levelGroup);

    auto effectTypeGroup = makeGroup("効果属性", 2);
    effectTypeGroup->set_layout_item({.row = 1, .column = 0,
                                      .preferred = {176.0F, 122.0F}});
    const std::vector<types::EffectType> effectTypes{
        types::EffectType::Physic, types::EffectType::Magic,
        types::EffectType::MagicalPhysic, types::EffectType::PhysicalMagic,
        types::EffectType::None};
    for (std::size_t index = 0; index < effectTypes.size(); ++index) {
        const auto value = effectTypes[index];
        auto radio = std::make_shared<lib::RadioButton>(
            context_.prop
                ? utils::tryFormat(context_.prop->requiredMsgs().effectTypeElement.value,
                                   context_.prop->requiredMsgs().effectTypeName(value))
                : std::to_string(static_cast<int>(value)));
        radio->group = "effect-type";
        if (context_.prop) radio->set_tooltip(context_.prop->requiredMsgs().effectTypeDesc(value));
        radio->checked = effect_type_ == value;
        radio->on_changed = [this, value](bool checked) {
            if (checked) {
                effect_type_ = value;
                setApplyEnabled(true);
            }
        };
        radio->set_layout_item({.row = index < 2 ? 0 : index - 1, .column = index < 2 ? index : 0,
                                .column_span = index < 2 ? 1U : 2U,
                                .preferred = {102.0F, 24.0F}});
        effectTypeGroup->add(radio);
    }
    leftSettings->add(effectTypeGroup);

    auto resistGroup = makeGroup("抵抗属性", 2);
    resistGroup->set_layout_item({.row = 2, .column = 0,
                                  .preferred = {176.0F, 82.0F}});
    const std::vector<types::Resist> resists{
        types::Resist::Avoid, types::Resist::Resist, types::Resist::Unfail};
    for (std::size_t index = 0; index < resists.size(); ++index) {
        const auto value = resists[index];
        auto radio = std::make_shared<lib::RadioButton>(
            context_.prop ? context_.prop->requiredMsgs().resistName(value)
                          : std::to_string(static_cast<int>(value)));
        radio->group = "resist-type";
        if (context_.prop) radio->set_tooltip(context_.prop->requiredMsgs().resistDesc(value));
        radio->checked = resist_ == value;
        radio->on_changed = [this, value](bool checked) {
            if (checked) {
                resist_ = value;
                setApplyEnabled(true);
            }
        };
        radio->set_layout_item({.row = index / 2, .column = index % 2,
                                .preferred = {78.0F, 24.0F}});
        resistGroup->add(radio);
    }
    leftSettings->add(resistGroup);
    auto absorbGroup = makeGroup("吸収効果", 1);
    absorbGroup->set_layout_item({.row = 3, .column = 0,
                                  .preferred = {224.0F, 82.0F}});
    absorb_control_ = std::make_shared<lib::CheckBox>(
        context_.prop ? context_.prop->requiredMsgs().absorbToSelected.value
                      : "選択メンバを吸収者にする");
    absorb_control_->checked = absorb_to_ == types::AbsorbTo::Selected;
    absorb_control_->on_changed = [this](bool checked) {
        absorb_to_ = checked ? types::AbsorbTo::Selected : types::AbsorbTo::None;
        refreshEffectEnabled();
        setApplyEnabled(true);
    };
    absorb_control_->set_layout_item({.row = 0, .column = 0,
                                      .preferred = {204.0F, 25.0F}});
    absorbGroup->add(absorb_control_);
    leftSettings->add(absorbGroup);
    settingsPage->add(leftSettings);

    auto rightSettings = makeGrid(2);
    rightSettings->set_layout_item({.row = 0, .column = 1,
                                    .preferred = {310.0F, 390.0F}, .grow = 1.0F,
                                    .constrain_minimum_to_preferred = true});
    auto visualGroup = makeGroup("視覚効果", 1);
    const std::vector<types::CardVisual> visuals{
        types::CardVisual::None, types::CardVisual::Reverse,
        types::CardVisual::Horizontal, types::CardVisual::Vertical};
    for (std::size_t index = 0; index < visuals.size(); ++index) {
        const auto value = visuals[index];
        auto radio = std::make_shared<lib::RadioButton>(
            context_.prop ? context_.prop->requiredMsgs().cardVisualName(value)
                          : std::to_string(static_cast<int>(value)));
        radio->group = "visual-type";
        radio->checked = card_visual_ == value;
        radio->on_changed = [this, value](bool checked) {
            if (checked) {
                card_visual_ = value;
                setApplyEnabled(true);
            }
        };
        radio->set_layout_item({.row = index, .column = 0,
                                .preferred = {132.0F, 25.0F}});
        visualGroup->add(radio);
    }
    visualGroup->set_layout_item({.row = 0, .column = 0,
                                  .preferred = {142.0F, 142.0F}});
    rightSettings->add(visualGroup);
    range_controls_.clear();
    auto rangeGroup = makeGroup("適用範囲", 2);
    const std::vector<types::Range> ranges{
        types::Range::Selected, types::Range::Random, types::Range::Party,
        types::Range::CouponHolder, types::Range::CardTarget};
    for (std::size_t index = 0; index < ranges.size(); ++index) {
        const auto value = ranges[index];
        auto radio = std::make_shared<lib::RadioButton>(
            context_.prop ? context_.prop->requiredMsgs().rangeName(value)
                          : std::to_string(static_cast<int>(value)));
        range_controls_.push_back({value, radio});
        radio->group = "effect-range";
        radio->checked = range_.range() == value;
        if (value == types::Range::CardTarget && context_.prop)
            radio->set_tooltip(context_.prop->requiredMsgs().rangeDescCardTarget.value);
        radio->on_changed = [this, value](bool checked) {
            // グループは新しいラジオへ通知する前に旧選択を解除する。解除
            // 時に旧モデルを再適用すると、その遷移を取り消してしまう。
            if (!checked) return;
            if (!range_.rangeEnabled(value)) {
                refreshEffectEnabled();
                return;
            }
            range_.setRange(value);
            refreshEffectEnabled();
            setApplyEnabled(true);
        };
        radio->set_layout_item({.row = index, .column = 0,
                                .column_span = 2,
                                .preferred = {146.0F, 25.0F}});
        rangeGroup->add(radio);
    }
    holding_coupon_control_ = std::make_shared<lib::ComboBox>();
    holding_coupon_control_->editable = true;
    holding_coupon_control_->edit_text = holding_coupon_;
    auto coupon_commons = chooserCommons(context_);
    const auto coupons = chooser::allCoupons(
        coupon_commons, summary_, chooser::CouponComboType::AllCoupons,
        chooserProperties(context_));
    for (const auto& coupon : coupons)
        holding_coupon_control_->items.push_back({coupon, coupon});
    holding_coupon_control_->on_selection_changed = [this](const auto value) {
        if (!value || !holding_coupon_control_ ||
            *value >= holding_coupon_control_->items.size()) return;
        holding_coupon_ = holding_coupon_control_->items[*value].value;
        range_.setCoupon(holding_coupon_);
        setApplyEnabled(true);
    };
    holding_coupon_control_->on_text_changed = [this](const StringView value) {
        holding_coupon_ = String(value);
        range_.setCoupon(holding_coupon_);
        setApplyEnabled(true);
    };
    holding_coupon_control_->set_layout_item({.row = ranges.size(), .column = 0,
                                              .preferred = {105.0F, 24.0F}});
    rangeGroup->add(holding_coupon_control_);
    holding_coupon_kind_control_ = std::make_shared<lib::ComboBox>();
    bindHoldingCouponKind(context_, holding_coupon_control_, holding_coupon_kind_control_);
    holding_coupon_kind_control_->set_layout_item({.row = ranges.size(), .column = 1});
    rangeGroup->add(holding_coupon_kind_control_);
    rangeGroup->set_layout_item({.row = 0, .column = 1,
                                 .preferred = {158.0F, 174.0F}});
    rightSettings->add(rangeGroup);

    auto successGroup = makeGroup("成功率修正値", 3);
    successGroup->set_layout_item({.row = 1, .column = 0, .column_span = 2,
                                   .preferred = {306.0F, 92.0F}});
    addLabel(successGroup, "絶対失敗 (-5)", 0, 0);
    auto success = std::make_shared<lib::Slider>();
    success->minimum = -5; success->maximum = 5; success->step = 1;
    // DのSWT Scaleは一刻みごとに目盛りを描く。-5～+5の各値に一つずつ付ける。
    success->tick_interval = 1;
    success->page_step = 5;
    success->value = success_rate_;
    success->on_changed = [this](double value) {
        success_rate_ = static_cast<int>(value);
        setApplyEnabled(true);
    };
    success->set_layout_item({.row = 0, .column = 1,
                              .preferred = {150.0F, 25.0F}, .grow = 1.0F});
    successGroup->add(success);
    addLabel(successGroup, "絶対成功 (+5)", 0, 2);
    rightSettings->add(successGroup);
    auto animationGroup = makeGroup("アニメーション", 1);
    animationGroup->set_layout_item({.row = 2, .column = 0, .column_span = 2,
                                     .preferred = {306.0F, 86.0F}});
    animation_control_ = std::make_shared<spcarddialog::CardAnimationPanel>(
        animationCommons(context_, summary_.get(), skin_.get()), summary_.get(),
        animationGroup, spcarddialog::CardAnimationPanelType::MoveCard);
    animation_control_->speed(card_speed_);
    animation_control_->overrideCardSpeed(override_card_speed_);
    animation_control_->widget()->set_layout_item(
        {.row = 0, .column = 0, .preferred = {290.0F, 42.0F}});
    version_control_updates_.push_back([this] { if (animation_control_) animation_control_->updateEnabled(); });
    animation_control_->modEvent.push_back([this] {
        if (!animation_control_) return;
        card_speed_ = animation_control_->speed();
        override_card_speed_ = animation_control_->overrideCardSpeed();
        setApplyEnabled(true);
    });
    rightSettings->add(animationGroup);
    settingsPage->add(rightSettings);

    // 能力参照タブ。
    auto abilityPage = makeGrid(2);
    auto refGroup = makeGroup("能力参照", 1);
    refGroup->set_layout_item({.row = 0, .column = 0, .column_span = 2,
                               .preferred = {490.0F, 56.0F}});
    ref_ability_control_ = std::make_shared<lib::CheckBox>(
        context_.prop ? context_.prop->requiredMsgs().refAbility.value
                      : "成功率と効果値の計算に選択中のメンバのレベルと能力を使用する");
    ref_ability_control_->checked = ref_ability_;
    ref_ability_control_->on_changed = [this](bool checked) {
        ref_ability_ = checked;
        refreshEffectEnabled();
        setApplyEnabled(true);
    };
    refGroup->add(ref_ability_control_);
    abilityPage->add(refGroup);
    auto ability_host = makeGrid(1, 0.0F);
    ability_host->set_layout_item({.row = 1, .column = 0,
                                   .column_span = 2,
                                   .preferred = {480.0F, 352.0F},
                                   .grow = 1.0F});
    abilityPage->add(ability_host);
    ability_control_ = std::make_unique<abilityview::AbilityView>(
        abilityCommons(context_), ability_host, 0, context_.services);
    ability_control_->physical(physical_);
    ability_control_->mental(mental_);
    ability_control_->widget()->set_layout_item(
        {.row = 0, .column = 0, .preferred = {480.0F, 352.0F},
         .grow = 1.0F});
    ability_control_->addModificationListener([this] {
        if (!ability_control_) return;
        physical_ = ability_control_->physical();
        mental_ = ability_control_->mental();
        setApplyEnabled(true);
    });

    // イベント発火タブ。
    auto ignitePage = makeGrid(1);
    auto igniteGroup = makeGroup("イベント発火の有無", 1);
    igniteGroup->set_layout_item({.row = 0, .column = 0,
                                   .preferred = {490.0F, 72.0F}});
    ignite_control_ = std::make_shared<lib::CheckBox>(
        context_.prop ? context_.prop->requiredMsgs().ignite.value
                      : "効果適用時にキーコードイベントと死亡イベントを発火させる");
    ignite_control_->checked = ignite_;
    ignite_control_->on_changed = [this](bool checked) {
        ignite_ = checked;
        refreshEffectEnabled();
        setApplyEnabled(true);
    };
    igniteGroup->add(ignite_control_);
    addLabel(igniteGroup,
             "※ 発火したイベント内で選択メンバが変更される可能性があります",
             1, 0);
    ignitePage->add(igniteGroup);

    ignite_split_ = std::make_shared<lib::SplitView>();
    ignite_split_->orientation = lib::Orientation::Horizontal;
    ignite_split_->weights = context_.vars
        ? std::vector<float>{
              static_cast<float>(context_.vars->effectContentSEKeyCodeSashL.value),
              static_cast<float>(context_.vars->effectContentSEKeyCodeSashR.value)}
        : std::vector<float>{4.0F, 7.0F};
    ignite_split_->on_weights_changed = [this](const std::vector<float>& weights) {
        if (!context_.vars || weights.size() < 2U) return;
        context_.vars->effectContentSEKeyCodeSashL.value =
            static_cast<std::int32_t>(std::lround(weights[0]));
        context_.vars->effectContentSEKeyCodeSashR.value =
            static_cast<std::int32_t>(std::lround(weights[1]));
    };
    ignite_split_->set_layout_item({.row = 1, .column = 0,
                                    .preferred = {490.0F, 322.0F}, .grow = 1.0F});
    ignitePage->add(ignite_split_);

    initial_sound_select_ =
        std::make_unique<effectcarddialog::SoundSelect>();
    sound_select_ = std::make_unique<effectcarddialog::SoundSelect>();
    auto sounds = effectcarddialog::createEffectSoundPanel(
        ignite_split_, context_, summary_,
        context_.prop ? context_.prop->requiredMsgs().initialEffectAndSound.value
                      : "効果音と初期効果",
        false, true, initial_effect_control_, *initial_sound_select_,
        *sound_select_);
    sounds->set_layout_item({.preferred = {188.0F, 322.0F}, .grow = 1.0F});
    initial_effect_control_->checked = initial_effect_;
    initial_effect_control_->on_changed = [this](bool checked) {
        initial_effect_ = checked;
        refreshEffectEnabled();
        setApplyEnabled(true);
    };
    initial_sound_select_->setPath(initial_sound_path_);
    initial_sound_select_->volume = initial_sound_volume_;
    initial_sound_select_->loopCount = initial_sound_loop_count_;
    initial_sound_select_->channel = initial_sound_channel_;
    initial_sound_select_->fadeIn = initial_sound_fade_in_;
    sound_select_->setPath(sound_path_);
    sound_select_->volume = sound_volume_;
    sound_select_->loopCount = sound_loop_count_;
    sound_select_->channel = sound_channel_;
    sound_select_->fadeIn = sound_fade_in_;
    initial_sound_select_->syncControls();
    sound_select_->syncControls();
    auto soundModified = [this] {
        if (!initial_sound_select_ || !sound_select_) return;
        initial_sound_path_ = initial_sound_select_->path;
        initial_sound_volume_ = initial_sound_select_->volume;
        initial_sound_loop_count_ = initial_sound_select_->loopCount;
        initial_sound_channel_ = initial_sound_select_->channel;
        initial_sound_fade_in_ = initial_sound_select_->fadeIn;
        sound_path_ = sound_select_->path;
        sound_volume_ = sound_select_->volume;
        sound_loop_count_ = sound_select_->loopCount;
        sound_channel_ = sound_select_->channel;
        sound_fade_in_ = sound_select_->fadeIn;
        setApplyEnabled(true);
    };
    initial_sound_select_->modEvent.push_back(soundModified);
    initial_sound_select_->loadedEvent.push_back(soundModified);
    sound_select_->modEvent.push_back(soundModified);
    sound_select_->loadedEvent.push_back(soundModified);

    auto keyCodes = makeGroup(
        context_.prop ? context_.prop->requiredMsgs().keyCodes.value
                      : "イベント発火のキーコード",
        1);
    key_code_group_ = keyCodes;
    keyCodes->set_layout_item({.preferred = {282.0F, 322.0F}, .grow = 1.0F});
    keycodeview::KeyCodeViewConfig key_config;
    // ネイティブ・共通ウィジェットはEffectダイアログのホストより長く保持され
    // 得る。借用ホストの全プロバイダーは、そのホストを参照する前に生存確認する。
    const std::weak_ptr<int> key_host_life = key_host_lifetime_;
    key_config.system = context_.prop ? &context_.prop->sys() : nullptr;
    key_config.canDuplicate = true;
    key_config.withIgnitionType = false;
    key_config.summaryLegacy = summary_ && summary_->legacy();
    key_config.dataVersion = summary_ ? summary_->dataVersion() : String{"4"};
    key_config.services = context_.services;
    key_config.dialogOwner = widget_;
    key_config.editorCommons = context_.editor_commons;
    key_config.refreshToolBar = [life = key_host_life, refresh = context_.refreshToolBar] {
        if (!life.expired() && refresh) refresh();
    };
    if (context_.editor_props) {
        key_config.targetVersion150 =
            context_.editor_props->targetVersion(summary_.get(), "1.50");
        key_config.resources.keyCodeImage =
            context_.editor_props->images().load(StringView{"key_code.png"});
        key_config.resources.addKeyCodeImage =
            context_.editor_props->images().load(StringView{"add_key_code.png"});
        key_config.resources.delKeyCodeImage =
            context_.editor_props->images().load(StringView{"del_key_code.png"});
        key_config.resources.warningImage =
            context_.editor_props->images().load(StringView{"warning.png"});
        key_config.resources.addByFeaturesImage =
            context_.editor_props->images().load(StringView{"add_key_codes_by_features.png"});
        key_config.resources.upImage =
            context_.editor_props->images().menu(types::MenuID::Up);
        key_config.resources.downImage =
            context_.editor_props->images().menu(types::MenuID::Down);
    }
    if (context_.prop) {
        const auto& messages = context_.prop->requiredMsgs();
        key_config.keyCodesMaxLegacy = static_cast<std::size_t>(
            std::max(0, context_.prop->looks().keyCodesMaxLegacy()));
        key_config.resources.keyCodeLabel = messages.keyCode.value;
        key_config.resources.addKeyCodeText = messages.addKeyCode.value;
        key_config.resources.delKeyCodeText = messages.delKeyCode.value;
        key_config.resources.addByFeaturesText =
            messages.menuTextAddKeyCodesByFeatures.value;
        key_config.resources.upText = messages.menuTextUp.value;
        key_config.resources.downText = messages.menuTextDown.value;
        key_config.resources.timingLabels = {
            messages.keyCodeTimingUse.value,
            messages.keyCodeTimingSuccess.value,
            messages.keyCodeTimingFailure.value,
            messages.keyCodeTimingHasNot.value};
        key_config.resources.selectFeaturesText = messages.selectFeatures.value;
        key_config.resources.selectFeaturesHint = messages.selectFeaturesHint.value;
        key_config.resources.addKeyCodesErrorText = messages.addKeyCodesError.value;
        key_config.resources.addKeyCodesErrorDesc = messages.addKeyCodesErrorDesc.value;
        key_config.resources.searchErrorKeyCodeMatchingAll =
            messages.searchErrorKeyCodeMatchingAll.value;
        key_config.resources.warningHasNotKeyCode = messages.warningHasNotKeyCode.value;
        key_config.resources.warningRunAwayCard = messages.warningRunAwayCard.value;
        key_config.resources.warningKeyCodeCount = messages.warningKeyCodeCount.value;
    }
    if (context_.vars)
        key_config.undoMax = static_cast<std::size_t>(
            std::max(0, context_.vars->undoMaxEtc.value));
    const auto skin_type = skin_ ? skin_->type() : String{};
    if (context_.editor_props) {
        auto* editor_props = context_.editor_props;
        key_config.keyCodesByFeatures = [editor_props, skin_type, life = key_host_life] {
            return life.expired() ? lib::Array<keycodeview::KeyCodeByFeature>{}
                                  : ::cwxeditor::cwx::editor::gui::dwt::dprops::arraySnapshot(
                                        editor_props->keyCodesByFeatures(skin_type));
        };
        key_config.keyCodesByMotions = [editor_props, skin_type, life = key_host_life] {
            return life.expired() ? lib::Array<keycodeview::KeyCodeByMotion>{}
                                  : ::cwxeditor::cwx::editor::gui::dwt::dprops::arraySnapshot(
                                        editor_props->keyCodesByMotions(skin_type));
        };
        key_config.standardKeyCodes = [editor_props, skin_type, life = key_host_life] {
            return life.expired() ? lib::Array<String>{}
                                  : ::cwxeditor::cwx::editor::gui::dwt::dprops::arraySnapshot(
                                        editor_props->standardKeyCodes(skin_type));
        };
    }
    key_config.getMotions = [this, life = key_host_life] {
        lib::Array<keycodeview::MotionInfo> result;
        if (life.expired()) return result;
        const auto& values = motion_view_ ? motion_view_->motions() : motions_;
        for (const auto& value : values)
            if (value) result.emplace_back(*value);
        return result;
    };
    if (context_.prop) {
        const auto* prop = context_.prop;
        const auto summary = summary_;
        key_config.warningProvider = [prop, summary, life = key_host_life](const StringView value) {
            if (life.expired()) return lib::Array<String>{};
            return warning::sjisWarnings(*prop, summary, value,
                                         prop->requiredMsgs().keyCode.value);
        };
    }
    if (context_.services.clipboard) {
        const auto clipboard = context_.services.clipboard;
        key_config.copyText = [clipboard](const StringView value) {
            lib::ClipboardData data;
            data.text = String(value);
            clipboard->write(data);
        };
    }
    key_config.summaryLegacyProvider = [this, life = key_host_life] {
        return !life.expired() && summary_ && summary_->legacy();
    };
    key_config.dataVersionProvider = [this, life = key_host_life] {
        if (life.expired()) return String{};
        return summary_ ? summary_->dataVersion() : String{"4"};
    };
    key_config.keyCodesMaxLegacyProvider = [this, life = key_host_life] {
        if (life.expired()) return std::size_t{};
        return static_cast<std::size_t>(std::max(
            0, context_.prop ? context_.prop->looks().keyCodesMaxLegacy() : 5));
    };
    key_config.undoMaxProvider = [this, life = key_host_life] {
        if (life.expired()) return std::size_t{};
        return static_cast<std::size_t>(std::max(
            0, context_.vars ? context_.vars->undoMaxEtc.value : 100));
    };
    key_config.xmlCopyProvider = [this, life = key_host_life] {
        return !life.expired() && context_.vars && context_.vars->xmlCopy.value;
    };
    key_config.catchModify = [] { return true; };
    key_code_view_ = std::make_shared<keycodeview::KeyCodeView>(
        keyCodes.get(), keycodeview::STYLE_NONE, std::move(key_config),
        [] { return true; });
    key_code_view_->setKeyCodes(key_codes_);
    key_code_view_->set_layout_item({.row = 0, .column = 0,
                                     .preferred = {270.0F, 286.0F}, .grow = 1.0F});
    key_code_view_->addModifyListener([this, life = key_host_life] {
        if (life.expired() || !key_code_view_) return;
        key_codes_ = key_code_view_->keyCodes();
        refreshEffectEnabled();
        setApplyEnabled(true);
    });
    keyCodes->add(key_code_view_);
    ignite_split_->add(keyCodes);

    rootTabs->pages.push_back({"効果", 0, effectPage, false});
    rootTabs->pages.push_back({"設定", 0, settingsPage, false});
    rootTabs->pages.push_back({"能力参照", 0, abilityPage, false});
    rootTabs->pages.push_back({"イベント発火", 0, ignitePage, false});
    rootTabs->selected_index = 0;
    lib::Size dialog_size{500.0F, 497.0F};
    if (context_.editor_props) {
        const auto& geometry = context_.editor_props->flex().effEvtDlg;
        if (geometry.width() > 0 && geometry.height() > 0)
            dialog_size = {static_cast<float>(geometry.width()),
                           static_cast<float>(geometry.height())};
    }
    installTypedBody(rootTabs, dialog_size, true);
    // ALLの効果ダイアログは最小幅を350px
    // 広げる（420から620、さらに770）
    // 。最小サイズで開くため、初期幅も同じ。
    widget_->minimum_size = lib::Size{
        lib::editor_window_host_options().all_mode ? 770.0F : 420.0F, 390.0F};
    // Dは親ShellとともにMotion表示を破棄する。C++は閉じたダイアロ
    // グを保持できるため、Closeで内部のBeast・コメント画面を終了する。
    closeEvent.push_back([this] {
        auto* const view = motion_view_.get();
        if (view) view->closeAuxiliaryEditors();
    });
    closeEvent.push_back([this] {
        if (!context_.editor_props || !widget_) return;
        const auto bounds = lib::authored_window_bounds(*widget_);
        auto& geometry = context_.editor_props->flex().effEvtDlg;
        geometry.width(static_cast<std::int32_t>(std::lround(bounds.width)));
        geometry.height(static_cast<std::int32_t>(std::lround(bounds.height)));
    });
    refreshEffectEnabled();
}

EffectDialog::~EffectDialog() {
    key_host_lifetime_.reset();
    if (context_.editor_commons) {
        context_.editor_commons->refElementOverrides.remove(motion_ref_element_connection_);
        context_.editor_commons->refBeast.remove(motion_ref_beast_connection_);
        context_.editor_commons->delBeast.remove(motion_del_beast_connection_);
        context_.editor_commons->refImageScale.remove(
            motion_ref_image_scale_connection_);
        context_.editor_commons->refCardImageStatus.remove(
            motion_ref_card_image_status_connection_);
    }
    for (const auto& toolbar : motion_category_tools_)
        if (toolbar) toolbar->on_command = {};
    if (motion_view_) {
        motion_view_->modelChanged.remove(motion_mod_connection_);
        motion_view_->warningEvent.remove(motion_warning_connection_);
    }
    if (key_code_view_) {
        key_code_view_->clearModifyListeners();
        // KeyCodeViewは補助ダイアログの所有Windowを保持する
        // 。ホストを閉じた後に循環が残らないよう、逆向きの子の接続を外す。
        if (key_code_group_) key_code_group_->remove(key_code_view_);
        key_code_view_.reset();
    }
}

bool EffectDialog::openCWXPath(String path_value, const bool activate) {
    return motion_view_ && motion_view_->openCWXPath(path_value, activate);
}

void EffectDialog::refSkin() {
    EventDialog::refSkin();
    if (motion_view_) motion_view_->refSkin(skin_.get());
}

void EffectDialog::refExternalUndoMax() {
    if (context_.vars) {
        const auto maximum = static_cast<std::size_t>(
            std::max(0, context_.vars->undoMaxEtc.value));
        if (key_code_view_) key_code_view_->setUndoLimit(maximum);
        if (!motion_view_) return;
        motion_view_->props().etc.undoMaxEtc = static_cast<std::size_t>(
            std::max(0, context_.vars->undoMaxEtc.value));
        motion_view_->refUndoMax();
    }
}

void EffectDialog::refreshEffectEnabled() {
    range_.refDataVersion();
    for (const auto& [value, control] : range_controls_) {
        if (!control) continue;
        control->set_enabled(range_.rangeEnabled(value));
        control->checked = range_.range() == value;
    }
    if (holding_coupon_kind_control_)
        holding_coupon_kind_control_->set_enabled(range_.range() == types::Range::CouponHolder);
    const bool legacy = summary_ && summary_->legacy();
    if (ignite_control_)
        ignite_control_->set_enabled(!legacy);
    if (initial_effect_control_)
        initial_effect_control_->set_enabled(
            !legacy || initial_effect_control_->checked);
    if (absorb_control_)
        absorb_control_->set_enabled(!legacy || absorb_control_->checked);
    if (ref_ability_control_)
        ref_ability_control_->set_enabled(!legacy);
    if (signed_level_control_)
        signed_level_control_->set_enabled(!ref_ability_);
    if (ability_control_) ability_control_->enabled(ref_ability_);
    if (holding_coupon_control_)
        holding_coupon_control_->set_enabled(
            range_.range() == types::Range::CouponHolder);
    for (const auto& control : initial_sound_controls_)
        if (control) control->set_enabled(initial_effect_);
    if (initial_sound_select_)
        initial_sound_select_->setEnabled(initial_effect_);
    if (key_code_view_)
        key_code_view_->enabled(ignite_ || !key_codes_.empty());
    if (animation_control_) animation_control_->updateEnabled();
    refreshWarning();
    refreshTypedBody();
}

void EffectDialog::refDataVersion() {
    refreshEffectEnabled();
    if (motion_view_) motion_view_->refEnabled();
    EventDialog::refDataVersion();
}

void EffectDialog::refreshWarning() {
    WarningList current;
    // Dのツールチップは、効果内容・範囲、互換性、速度
    // ・キーコード、初期効果警告、音声の順を維持する。
    if (motion_view_) {
        const auto motion_warnings = motion_view_->warnings();
        current.insert(current.end(), motion_warnings.begin(),
                       motion_warnings.end());
    }
    const auto& range_warnings = range_.warnings();
    current.insert(current.end(), range_warnings.begin(), range_warnings.end());

    if (context_.prop) {
        const auto& messages = context_.prop->requiredMsgs();
        if (summary_ && !targetVersion("2")) {
            if (ref_ability_) current.push_back(messages.warningRefAbility.value);
            if (ignite_) current.push_back(messages.warningIgnite.value);
        }
    }
    if (animation_control_) {
        const auto animation_warnings = animation_control_->warnings();
        current.insert(current.end(), animation_warnings.begin(),
                       animation_warnings.end());
    }
    if (context_.prop && !ignite_ && !key_codes_.empty())
        current.push_back(context_.prop->requiredMsgs().warningIgnoreKeyCode.value);
    if (key_code_view_ && key_code_view_->enabled()) {
        const auto key_warnings = key_code_view_->warnings();
        current.insert(current.end(), key_warnings.begin(), key_warnings.end());
    }
    if (context_.prop && !targetVersion("4")) {
        const auto& messages = context_.prop->requiredMsgs();
        if (initial_effect_)
            current.push_back(messages.warningInitialEffect.value);
        if (absorb_to_ == types::AbsorbTo::Selected)
            current.push_back(messages.warningAbsorbToSelected.value);
    }
    if (context_.prop) {
        const auto& messages = context_.prop->requiredMsgs();
        for (const auto* selector : {initial_sound_select_.get(), sound_select_.get()}) {
            if (!selector) continue;
            const auto path = selector->filePath;
            const bool legacy = summary_ && summary_->legacy();
            if (legacy && !path.empty() && !selector->selectedDefDir)
                current.push_back(messages.warningNotDefaultSE.value);
            const auto format = Skin::warningSE(*context_.prop, path, legacy,
                context_.vars ? context_.vars->targetVersion.value : String{});
            current.insert(current.end(), format.begin(), format.end());
            const auto options = selector->warnings;
            current.insert(current.end(), options.begin(), options.end());
        }
    }
    replaceWarnings(std::move(current));
}
bool EffectDialog::apply() {
    range_.refDataVersion();
    if (motion_view_) motions_ = motion_view_->motions();
    // D の Spinner 下限に合わせて既存の 0 補正を維持し、設定上限は選択行の操作に任せる。
    for (const auto& motion : motions_)
        if (motion && motion->detail().use(types::MArg::Beast))
            motion->maxNest(std::max(1U, motion->maxNest()));
    if (key_code_view_) key_codes_ = key_code_view_->keyCodes();
    if (ability_control_) {
        physical_ = ability_control_->physical();
        mental_ = ability_control_->mental();
    }
    if (animation_control_) {
        card_speed_ = animation_control_->speed();
        override_card_speed_ = animation_control_->overrideCardSpeed();
    }
    if (initial_sound_select_) {
        initial_sound_select_->syncFromControls();
        initial_sound_path_ = initial_sound_select_->path;
        initial_sound_volume_ = initial_sound_select_->volume;
        initial_sound_loop_count_ = initial_sound_select_->loopCount;
        initial_sound_channel_ = initial_sound_select_->channel;
        initial_sound_fade_in_ = initial_sound_select_->fadeIn;
    }
    if (sound_select_) {
        sound_select_->syncFromControls();
        sound_path_ = sound_select_->path;
        sound_volume_ = sound_select_->volume;
        sound_loop_count_ = sound_select_->loopCount;
        sound_channel_ = sound_select_->channel;
        sound_fade_in_ = sound_select_->fadeIn;
    }
    auto value = ensureDraft();
    value->motions(motions_);
    value->signedLevel(signed_level_);
    value->initialEffect(initial_effect_);
    value->initialSoundPath(initial_sound_path_);
    value->initialSoundVolume(initial_sound_volume_);
    value->initialSoundLoopCount(initial_sound_loop_count_);
    value->initialSoundChannel(initial_sound_channel_);
    value->initialSoundFadeIn(initial_sound_fade_in_);
    value->soundPath(sound_path_);
    value->soundVolume(sound_volume_);
    value->soundLoopCount(sound_loop_count_);
    value->soundChannel(sound_channel_);
    value->soundFadeIn(sound_fade_in_);
    value->successRate(success_rate_);
    value->effectType(effect_type_);
    value->resist(resist_);
    value->cardVisual(card_visual_);
    value->range(range_.range());
    value->holdingCoupon(range_.holdingCoupon());
    value->refAbility(ref_ability_);
    value->physical(physical_);
    value->mental(mental_);
    value->ignite(ignite_);
    value->keyCodes(key_codes_);
    value->cardSpeed(card_speed_);
    value->overrideCardSpeed(override_card_speed_);
    value->absorbTo(absorb_to_);
    return commitDraft();
}

MessageEventDialog::MessageEventDialog(
    Context context, SummaryPtr summary, Parent parent,
    ContentPtr parentContent, ContentPtr content, SkinPtr skin,
    UseCounterPtr useCounter)
    : EventDialog(std::move(context), std::move(summary), CType::TalkMessage,
                  std::move(parent), std::move(parentContent),
                  std::move(content), std::move(skin),
                  std::move(useCounter)) {}

void MessageEventDialog::setupBody() {
    auto value = ensureDraft();
    messageutils::MessageContext messageContext;
    messageContext.services = context_.services;
    messageContext.props = context_.editor_props;
    messageContext.summary = summary_.get();
    messageContext.summary_owner = summary_;
    messageContext.editor_commons = context_.editor_commons;
    messageContext.forced_skin = lib::editor_window_host_options().all_mode
        ? skin_ : original_forced_skin_;
    const auto preview_active = std::make_shared<bool>(true);
    if (!lib::editor_window_host_options().all_mode && !original_forced_skin_ &&
        context_.editor_commons) {
        auto* const commons = context_.editor_commons;
        const auto lifetime = commons->lifetimeToken();
        // 通常のDのEventWindowプレビューは、現在のCommons
        // のSkinを解決する。明示的に渡したSkinは固定する。
        messageContext.current_skin = [commons, lifetime, preview_active]() -> SkinPtr {
            return lifetime.expired() || !*preview_active ? SkinPtr{} : commons->skin();
        };
    }
    messageContext.use_counter = use_counter_.get();
    messageContext.use_counter_owner = use_counter_;
    messageContext.on_changed = [this] { setApplyEnabled(true); };
    messageContext.on_warnings_changed =
        [this](const WarningList& value) { replaceWarnings(value); };
    editor_ = std::make_unique<messageutils::MessageDialog>(
        std::move(messageContext), parent_content_.get(), value.get());
    const auto body = editor_->setup({}, false);
    // Dの参照画像はWin32の外枠込みで1154×442。メ
    // ッセージダイアログのクライアント幅だけに3pxを加える。
    installMessageBody(*editor_, body, {1141.0F, 406.0F});
    opened_body_ = [this, preview_active] {
        const auto keep_alive = shared_from_this();
        auto* const editor = editor_.get();
        if (!editor) return;
        const bool reopening = !*preview_active;
        *preview_active = true;
        if (auto* selector = editor->imageSelect()) selector->active(true);
        if (!open_ || editor_.get() != editor) return;
        editor->opened();
        if (reopening && open_ && editor_.get() == editor && editor->context().current_skin)
            editor->refImageScale();
    };
    closeEvent.push_back([this, preview_active] {
        *preview_active = false;
        if (editor_) {
            if (auto* selector = editor_->imageSelect()) selector->active(false);
        }
    });
    replaceWarnings(editor_->currentWarnings());
}

bool MessageEventDialog::apply() {
    if (!editor_ || !editor_->apply()) return false;
    replaceWarnings(editor_->currentWarnings());
    return commitDraft();
}

void MessageEventDialog::refSkin() {
    EventDialog::refSkin();
    if (!editor_) return;
    editor_->context().forced_skin = lib::editor_window_host_options().all_mode
        ? skin_ : original_forced_skin_;
    editor_->refSkin();
    // refImageScaleは新しいスキン・倍率でプレビューを更新する。
    editor_->refImageScale();
    refreshWarning();
}

void MessageEventDialog::refDataVersion() {
    if (!editor_) {
        EventDialog::refDataVersion();
        return;
    }
    editor_->refDataVersion();
    replaceWarnings(editor_->currentWarnings());
}

bool MessageEventDialog::openCWXPath(String path, const bool activate) {
    if (editor_ && editor_->openCWXPath(path, activate)) return true;
    return EventDialog::openCWXPath(std::move(path), activate);
}

void MessageEventDialog::refreshWarning() {
    if (!editor_) {
        EventDialog::refreshWarning();
        return;
    }
    editor_->refreshWarning();
    replaceWarnings(editor_->currentWarnings());
}

void MessageEventDialog::refExternalPath(StringView old_path,
                                         StringView new_path,
                                         const bool is_directory) {
    if (editor_) (void)editor_->refPath(old_path, new_path, is_directory);
}

void MessageEventDialog::refExternalPaths(StringView parent_path) {
    if (editor_) editor_->refPaths(parent_path);
}

void MessageEventDialog::refExternalFlagAndStep() {
    if (editor_) editor_->refFlagAndStep();
}

void MessageEventDialog::refExternalImageScale() {
    if (editor_) editor_->refImageScale();
}

void MessageEventDialog::refExternalUndoMax() {
    if (editor_) editor_->refUndoMax();
}

SpeakEventDialog::SpeakEventDialog(
    Context context, SummaryPtr summary, Parent parent,
    ContentPtr parentContent, ContentPtr content, SkinPtr skin,
    UseCounterPtr useCounter)
    : EventDialog(std::move(context), std::move(summary), CType::TalkDialog,
                  std::move(parent), std::move(parentContent),
                  std::move(content), std::move(skin),
                  std::move(useCounter)) {}

void SpeakEventDialog::setupBody() {
    auto value = ensureDraft();
    messageutils::MessageContext messageContext;
    messageContext.services = context_.services;
    messageContext.props = context_.editor_props;
    messageContext.summary = summary_.get();
    messageContext.summary_owner = summary_;
    messageContext.editor_commons = context_.editor_commons;
    messageContext.forced_skin = skin_;
    messageContext.use_counter = use_counter_.get();
    messageContext.use_counter_owner = use_counter_;
    messageContext.on_changed = [this] { setApplyEnabled(true); };
    messageContext.on_warnings_changed =
        [this](const WarningList& value) { replaceWarnings(value); };
    editor_ = std::make_unique<messageutils::SpeakDialog>(
        std::move(messageContext), parent_content_.get(), value.get());
    const auto body = editor_->setup({}, false);
    // 台詞・話者の編集部には台詞選択ペインが加わるため、DWT
    // では通常のメッセージ編集部より広い幅に収まる。
    installMessageBody(*editor_, body, {1284.0F, 506.0F});
    // Dは所有ダイアログの表示後、opened()から保存済みの
    // 浮動プレビューを開く。プレビューのshowコールバックが再
    // 入してUIを終了・置換し得る間、このホストを生存させる。
    opened_body_ = [this] {
        const auto keep_alive = shared_from_this();
        if (!open_) return;
        auto* const editor = editor_.get();
        if (!editor) return;
        editor->opened();
    };
    replaceWarnings(editor_->currentWarnings());
}

SpeakEventDialog::~SpeakEventDialog() = default;

bool SpeakEventDialog::apply() {
    if (!editor_ || !editor_->apply()) return false;
    replaceWarnings(editor_->currentWarnings());
    return commitDraft();
}

void SpeakEventDialog::refSkin() {
    EventDialog::refSkin();
    if (!editor_) return;
    editor_->context().forced_skin = skin_;
    editor_->refSkin();
    // refImageScaleは新しいスキン・倍率でプレビューを更新する。
    editor_->refImageScale();
    refreshWarning();
}

void SpeakEventDialog::refDataVersion() {
    if (!editor_) {
        EventDialog::refDataVersion();
        return;
    }
    editor_->refDataVersion();
    replaceWarnings(editor_->currentWarnings());
}

bool SpeakEventDialog::openCWXPath(String path, const bool activate) {
    if (editor_ && editor_->openCWXPath(path, activate)) return true;
    return EventDialog::openCWXPath(std::move(path), activate);
}

void SpeakEventDialog::refreshWarning() {
    if (!editor_) {
        EventDialog::refreshWarning();
        return;
    }
    editor_->refreshWarning();
    replaceWarnings(editor_->currentWarnings());
}

void SpeakEventDialog::refExternalPath(StringView old_path,
                                       StringView new_path,
                                       const bool is_directory) {
    if (editor_) (void)editor_->refPath(old_path, new_path, is_directory);
}

void SpeakEventDialog::refExternalPaths(StringView parent_path) {
    if (editor_) editor_->refPaths(parent_path);
}

void SpeakEventDialog::refExternalFlagAndStep() {
    if (editor_) editor_->refFlagAndStep();
}

void SpeakEventDialog::refExternalImageScale() {
    if (editor_) editor_->refImageScale();
}

void SpeakEventDialog::refExternalUndoMax() {
    if (editor_) editor_->refUndoMax();
}


void BgImagesDialog::disposeBackgroundDraft() {
    // Close後もネイティブのウィジェット・制御部が保持され得る。後
    // のGCや破棄を待たず、この終了境界で一時的な参照関係を解放する。
    auto dialogs = std::move(active_background_dialogs_);
    for (const auto& dialog : dialogs) if (dialog) dialog->close();
    background_view_.reset();
    background_undo_.reset();
    if (background_container_) background_container_->dispose();
}

void BgImagesDialog::buildControls() {
    auto root = makeGridPanel(1U);
    auto editor_host = makeGridPanel(1U, 0.0F, {});
    editor_host->set_layout_item({.row = 0U, .column = 0U,
                                  .preferred = {950.0F, 450.0F}, .grow = 1.0F});
    root->add(editor_host);

    if (context_.editor_props && background_container_) {
        background_context_.services = context_.services;
        // Dはダイアログの下位カウンターをBgImagesViewへ渡す。参照するMenuCard
        // のタイトルプレビューで使う、イベント内の変数を保持するため。
        background_context_.use_counter_owner = use_counter_;
        background_context_.import_scene_image = scenehost::imageImporter(
            context_.editor_props, context_.services, summary_, skin_);
        // このモードレス編集部が開いている間、BgImagesView
        // はスキン再読み込みをまたいで残り得る。生の
        // 構築引数を保持せず、状態内で共有所有する。
        background_context_.skin_owner = skin_;
        // イベント所有の背景一覧は、ホストの場面用CommentDialog
        // ファクトリーと更新購読を共有する。型付きのコメントコマン
        // ドで、所有場面と同じ有効なモードレス編集部を開くため。
        background_context_.make_scene_comment_dialog = context_.make_scene_comment_dialog;
        background_context_.subscribe_scene_comment_updates =
            context_.subscribe_scene_comment_updates;
        // DのBgImagesViewは、参照するArea/Battleを場
        // 面として開く。このコンテントダイアログと同じシナリオ所有元を使う。
        if (auto* common = context_.editor_commons) {
            const auto life = common->lifetimeToken();
            background_context_.open_area_scene =
                [common, life, prop = context_.editor_props, source = summary_](
                    ::cwxeditor::cwx::area::AbstractArea* target) {
                    if (life.expired() || !prop || !source || !target) return;
                    if (const auto value = dynamic_cast<::cwxeditor::cwx::area::Area*>(target)) {
                        const auto owner = source->area(value->id());
                        if (owner.get() == value)
                            (void)common->openAreaScene(*prop, source.get(), owner, true, nullptr);
                    } else if (const auto value = dynamic_cast<::cwxeditor::cwx::area::Battle*>(target)) {
                        const auto owner = source->battle(value->id());
                        if (owner.get() == value)
                            (void)common->openAreaScene(*prop, source.get(), owner, true, nullptr);
                    }
                };
        }
        // DのremoveBackは、Summaryが未保存でも削除したセル
        // のモードレス編集部を閉じる。イベントの編集中の値は所有元内のパス
        // を使うため、このダイアログの一致する子ウィンドウだけを終了する。
        background_context_.retire_local_bg_image = [this](std::string_view path) {
            std::vector<std::shared_ptr<bgimagedialog::BgImageDialog>> closing;
            for (const auto& dialog : active_background_dialogs_)
                if (dialog && dialog->back() && dialog->back()->cwxPath(true) == path)
                    closing.push_back(dialog);
            // closeEventは項目を外し、この所有元も終了させ得る。
            // 終了コールバックの配送後はthisやそのベクターを読まない。
            for (const auto& dialog : closing) dialog->forceCancel();
        };
        background_context_.show_inherit_background =
            type_ != CType::ReplaceBgImage &&
            (!context_.vars || context_.vars->showInheritBackground.value);
        // ツールバー・表示状態の更新は構築中にも行う。Dのmod(view)
        // と同様に、ダイアログを変更済みにするのは下記のモデル変更通知だけ。
        background_context_.refresh_toolbar = [] {};
        background_context_.open_cwx_path = [this](const std::string_view path,
                                                   const bool activate) {
            return openCWXPath(std::string(path), activate);
        };
        background_context_.create_background = [this](const int type) {
            editBackgroundCell(type, true);
        };
        background_context_.callbacks.edit = [this] {
            editBackgroundCell(-1, false);
        };
        background_context_.callbacks.edit_target = [this](
            const ::cwxeditor::cwx::editor::gui::dwt::areawindow::SceneEditTargetKind kind,
            const std::size_t index) {
            if (kind != ::cwxeditor::cwx::editor::gui::dwt::areawindow::SceneEditTargetKind::Background ||
                !background_view_ || index >= background_view_->area().backs().size())
                return;
            const auto target = background_view_->area().backs()[index];
            if (!target) return;
            const auto existing = std::find_if(
                active_background_dialogs_.begin(), active_background_dialogs_.end(),
                [&target](const auto& item) {
                    return item && item->back() == target && item->dialog() &&
                           item->dialog()->visible();
                });
            if (existing != active_background_dialogs_.end()) {
                if (context_.services.gui)
                    context_.services.gui->focus((*existing)->dialog());
                return;
            }
            // Dのキャッシュを優先するeditBack(BgImage)に合わ
            // せ、新しい編集部だけがクリック画像を一覧選択へ反映する。edit_target
            // はBgImagesView::editBackから配送済
            // みなので、コールバックへ再入せず、対象を直接選んで開く。
            background_view_->selectBackIndex(index);
            if (!background_view_) return;
            editBackgroundCell(-1, false, target);
        };
        const auto undo_max = context_.vars
            ? static_cast<std::size_t>(std::max(0, context_.vars->undoMaxEvent.value))
            : std::size_t{100};
        background_undo_ = std::make_shared<undo::UndoManager>(undo_max);
        background_view_ = std::make_unique<areaview::BgImagesView>(
            background_context_, *context_.editor_props, summary_.get(), nullptr,
            *background_container_, editor_host, background_undo_,
            background_context_.show_inherit_background, false);
        if (context_.editor_props->var().refCardsAtEditBgImage.value)
            background_view_->refAreaTarget(context_.referenceArea);
        // Dは有効な参照ComboをArea/Battleの改名
        // ・削除通知へ接続し、Combo破棄時にリスナーを外す。
        if (auto* common = context_.editor_commons;
            common && summary_ && !summary_->scenarioPath().empty() &&
            !lib::editor_window_host_options().all_mode) {
            struct ReferenceSubscriptions {
                commons::Commons* common{};
                std::weak_ptr<const int> lifetime;
                std::uint64_t area_ref{}, area_del{}, battle_ref{}, battle_del{};
                std::uint64_t card_ref{}, card_add{}, card_del{}, card_up{}, card_down{};
                std::uint64_t card_groups{}, card_indices{};
                void disconnect() noexcept {
                    if (!common || lifetime.expired()) return;
                    common->refArea.remove(area_ref);
                    common->delArea.remove(area_del);
                    common->refBattle.remove(battle_ref);
                    common->delBattle.remove(battle_del);
                    common->refMenuCard.remove(card_ref);
                    common->addMenuCard.remove(card_add);
                    common->delMenuCard.remove(card_del);
                    common->upMenuCard.remove(card_up);
                    common->downMenuCard.remove(card_down);
                    common->refCardGroups.remove(card_groups);
                    common->refMenuCardIndices.remove(card_indices);
                    common = nullptr;
                }
                ~ReferenceSubscriptions() { disconnect(); }
            };
            auto subscriptions = std::make_shared<ReferenceSubscriptions>();
            subscriptions->common = common;
            subscriptions->lifetime = common->lifetimeToken();
            auto* view = background_view_.get();
            const auto view_life = view->sceneLifetime();
            const auto refresh = [view, view_life](const auto&) {
                if (!view_life.expired()) view->refreshRefAreas();
            };
            subscriptions->area_ref = common->refArea.add(refresh);
            subscriptions->area_del = common->delArea.add(refresh);
            subscriptions->battle_ref = common->refBattle.add(refresh);
            subscriptions->battle_del = common->delBattle.add(refresh);
            subscriptions->card_ref = common->refMenuCard.add([view, view_life](String path) {
                if (!view_life.expired()) view->refRefMenuCard(std::move(path));
            });
            subscriptions->card_add = common->addMenuCard.add([view, view_life](String path) {
                if (!view_life.expired()) view->addRefMenuCard(std::move(path));
            });
            subscriptions->card_del = common->delMenuCard.add([view, view_life](String path) {
                if (!view_life.expired()) view->delRefMenuCard(std::move(path));
            });
            subscriptions->card_up = common->upMenuCard.add(
                [view, view_life](String path, std::vector<int> indices, int count) {
                    if (!view_life.expired()) view->upRefMenuCards(std::move(path), std::move(indices), count);
                });
            subscriptions->card_down = common->downMenuCard.add(
                [view, view_life](String path, std::vector<int> indices, int count) {
                    if (!view_life.expired()) view->downRefMenuCards(std::move(path), std::move(indices), count);
                });
            // 共通の場面トランザクションは削除前にdelMenuCard、削除後に
            // refCardGroupsを出し、並べ替えは一括インデックス通知を使
            // う。参照変更でイベントを変更済みにしないよう、表示だけを更新する。
            subscriptions->card_groups = common->refCardGroups.add([view, view_life] {
                if (!view_life.expired()) view->refreshPresentation();
            });
            subscriptions->card_indices = common->refMenuCardIndices.add(
                [view, view_life](std::shared_ptr<const ::cwxeditor::cwx::area::AbstractArea> owner,
                                  std::vector<std::pair<std::size_t, std::size_t>>) {
                    if (!view_life.expired() && owner)
                        (void)view->refreshNotifiedReferenceCards(const_cast<::cwxeditor::cwx::area::AbstractArea*>(owner.get())->cwxPath(true));
                });
            // close()なしで制御部が破棄されても、保持した所有権で接
            // 続を解除する。通知コールバック自身はこの購読を所有しない。
            closeEvent.push_back([subscriptions] { subscriptions->disconnect(); });
        }
        background_view_->modEvent.push_back([this] {
            setApplyEnabled(true);
            refreshCellNameChoices();
            refreshWarning();
        });
    }

    std::size_t row = 1U;
    const auto detail = event::contentDetail(type_);
    // Dは常にPropsを持つ。PropsのないヘッドレスC++呼び出し側には、固定文字列の代替を残す。
    if (detail.use(types::CArg::CellName)) {
        auto target = makeGridPanel(4U, 4.0F, {});
        addLabel(target, context_.prop
            ? context_.prop->requiredMsgs().cellName.value : "対象セル名称", 0U, 0U);
        cell_name_control_ = makeCellNameCombo(
            use_counter_, cell_name_, [this](String value) {
            cell_name_ = std::move(value);
            setApplyEnabled(true);
            refreshWarning();
        });
        cell_name_control_->set_layout_item({.row = 0U, .column = 1U,
                                             .preferred = {390.0F, 24.0F},
                                             .grow = 1.0F});
        target->add(cell_name_control_);
        auto separator = std::make_shared<lib::Separator>();
        separator->orientation = lib::Orientation::Vertical;
        separator->set_layout_item({.row = 0U, .column = 2U,
                                    .preferred = {1.0F, 24.0F}});
        target->add(separator);
        auto transition = makeCompactTransitionControls(
            transition_, context_, [this] {
                setApplyEnabled(true);
                refreshWarning();
            });
        transition->set_layout_item({.row = 0U, .column = 3U,
                                     .horizontal = lib::Align::End});
        target->add(transition);
        target->set_layout_item({.row = row++, .column = 0U});
        root->add(target);
    } else {
        auto transition = makeCompactTransitionControls(
            transition_, context_, [this] {
                setApplyEnabled(true);
                refreshWarning();
            });
        transition->set_layout_item({.row = row++, .column = 0U,
                                     .horizontal = lib::Align::End});
        root->add(transition);
    }

    if (detail.use(types::CArg::DoAnime)) {
        auto options = makeGridPanel(2U, 8.0F, {});
        auto anime = std::make_shared<lib::CheckBox>(context_.prop
            ? context_.prop->requiredMsgs().doAnime.value : "JPY1アニメーションを実行する");
        anime->checked = do_anime_;
        anime->on_changed = [this](const bool value) {
            do_anime_ = value;
            setApplyEnabled(true);
        };
        anime->set_layout_item({.row = 0U, .column = 0U});
        options->add(anime);
        auto ignore = std::make_shared<lib::CheckBox>(context_.prop
            ? context_.prop->requiredMsgs().ignoreEffectBooster.value
            : "エフェクトブースター関係のセルを無視する");
        ignore->checked = ignore_effect_booster_;
        ignore->on_changed = [this](const bool value) {
            ignore_effect_booster_ = value;
            setApplyEnabled(true);
            refreshWarning();
        };
        ignore->set_layout_item({.row = 0U, .column = 1U});
        options->add(ignore);
        options->set_layout_item({.row = row++, .column = 0U,
                                  .horizontal = lib::Align::End});
        root->add(options);
    }
    installTypedBody(root, {979.0F, 601.0F}, true);

    // Dの背景メニューを、既定背景のホストと共有する。
    auto menu = background_view_ ? background_view_->createMenuBar()
                                 : std::make_shared<lib::Menu>(false);
    // このダイアログは明示的なグリッドを使うため、MenuBarはウィンドウの操
    // 作部として自動配置されない。Dの上部メニューに合わせ、最初の行を確保する。
    for (const auto& child : widget_->children()) {
        auto item = child->layout_item();
        ++item.row;
        child->set_layout_item(item);
    }
    menu->set_layout_item({.row = 0U, .column = 0U});
    widget_->insert(0U, menu);
    if (background_view_ && context_.editor_props) {
        // 固定のD参照画像とは異なり、初回は現在の画像倍
        // 率と保存済み分割比率で場面全体を収める。場面
        // 自体のサイズやユーザーの分割設定は変えない。
        const auto& vars = context_.editor_props->var();
        const auto scene = background_view_->canvas()->layout_item().minimum;
        const auto gap = lib::kEditorUiMetrics.split_spacing;
        // ALLでは左右の一覧幅を
        // 各250pxとして初期幅
        // を求める。単体版は保存さ
        // れた分割比率に合わせる。
        const bool all_mode = lib::editor_window_host_options().all_mode;
        float full_width{};
        if (all_mode) {
            const auto side = areaview::BgImagesView::kBgImagesListExtent;
            full_width = side + gap + (scene.width + 20.0F) + gap + side;
        } else {
            // 単体版は保存した分割比率に合わせる動作を維持する。
            const auto fraction = [](int first, int second) {
                const double a = std::max(1, first), b = std::max(1, second);
                return a / (a + b);
            };
            const auto right_width = (scene.width + 20.0F) /
                fraction(vars.bgImageViewImageFlagL.value, vars.bgImageViewImageFlagR.value) + gap;
            full_width = static_cast<float>(right_width /
                fraction(vars.bgImageViewR.value, vars.bgImageViewL.value) + gap);
        }
        // ALLの仕様差として、セル一覧とフラグ一覧の高さを
        // 、従来の場面に合わせた高さより40px短くする。
        const float kListHeightReduction = all_mode ? 40.0F : 0.0F;
        auto host_item = editor_host->layout_item();
        host_item.preferred = {
            static_cast<float>(std::ceil(full_width)),
            scene.height + (vars.showSceneToolBar.value ? 52.0F : 20.0F) -
                kListHeightReduction};
        editor_host->set_layout_item(host_item);
        const auto needed = lib::preferred_ui_size(widget_);
        auto bounds = widget_->bounds();
        const auto scale = lib::editor_ui_text_scale();
        bounds.width = std::max(bounds.width, needed.width / scale);
        if (all_mode) {
            // ALLの高さは内容に正確に合わせる。Dの参
            // 照高を使うと、一覧が高いまま残ってしまう。
            bounds.height = needed.height / scale;
            widget_->set_bounds(bounds);
            if (widget_->minimum_size)
                widget_->minimum_size->height =
                    std::min(widget_->minimum_size->height, bounds.height);
        } else {
            bounds.height = std::max(bounds.height, needed.height / scale);
            widget_->set_bounds(bounds);
        }
    }
    refreshWarning();
}

void BgImagesDialog::editBackgroundCell(const int requested_type,
                                        const bool create,
                                        std::shared_ptr<background::BgImage> target) {
    if (!background_view_ || !context_.editor_props) return;

    std::vector<std::shared_ptr<background::BgImage>> values;
    if (create) {
        std::shared_ptr<background::BgImage> value;
        if (requested_type == 1) {
            // DのcreateBackgroundImplに合わせ、新規テキストセルは設定された既定サイ
            // ズ（textCellDefaultWidth/Height、100×100）から始める。
            auto cell = std::make_shared<background::TextCell>();
            cell->width(context_.editor_props->var().textCellDefaultWidth.value);
            cell->height(context_.editor_props->var().textCellDefaultHeight.value);
            value = std::move(cell);
        } else if (requested_type == 2) {
            // Dと同じく、色セルはcolorCellDefaultWidth/Heightで作る。
            auto cell = std::make_shared<background::ColorCell>();
            cell->width(context_.editor_props->var().colorCellDefaultWidth.value);
            cell->height(context_.editor_props->var().colorCellDefaultHeight.value);
            value = std::move(cell);
        } else if (requested_type == 3) {
            // Dと同じく、PCセルはカードのサイズで作る。
            auto cell = std::make_shared<background::PCCell>();
            const auto size = context_.editor_props->looks().cardSize();
            cell->width(static_cast<std::int32_t>(size.width));
            cell->height(static_cast<std::int32_t>(size.height));
            value = std::move(cell);
        }
        else
            value = std::make_shared<background::ImageCell>();
        values.push_back(std::move(value));
    } else if (target) {
        values.push_back(std::move(target));
    } else {
        values = background_view_->selectedBacks();
    }
    if (values.empty()) return;

    std::erase_if(active_background_dialogs_, [](const auto& existing) {
        return !existing || !existing->dialog() || !existing->dialog()->visible();
    });

    auto parent_window = std::dynamic_pointer_cast<lib::Window>(widget_);
    bgimagedialog::BgImageDialogContext dialog_context(*context_.editor_props);
    dialog_context.services = context_.services;
    dialog_context.common.services = context_.services;
    dialog_context.common.skin = skin_.get();
    dialog_context.skin_owner = skin_;
    dialog_context.common.summary = summary_.get();
    dialog_context.editor_commons = context_.editor_commons;
    dialog_context.refresh_toolbar = [this] { setApplyEnabled(true); };
    dialog_context.refresh_cell_names = [this] {
        if (context_.editor_commons) context_.editor_commons->refCellNames.call();
    };
    dialog_context.settings = [this] {
        const String type = skin_ ? skin_->type() : String{};
        auto result = ::cwxeditor::cwx::editor::gui::dwt::dprops::arraySnapshot(
            context_.editor_props->bgImageSettings(type));
        if (result.empty())
            result = ::cwxeditor::cwx::editor::gui::dwt::dprops::arraySnapshot(
                context_.editor_props->bgImageSettings({}));
        return result;
    };
    const auto used_names = [this]<typename Id> {
        std::vector<String> result;
        if (!use_counter_) return result;
        for (const auto& key : use_counter_->template keys<Id>()) {
            const String name = static_cast<String>(key);
            if (!name.empty() &&
                std::find(result.begin(), result.end(), name) == result.end())
                result.push_back(name);
        }
        std::stable_sort(result.begin(), result.end());
        return result;
    };
    dialog_context.flags = used_names.template operator()<usecounter::FlagId>();
    dialog_context.cell_names =
        used_names.template operator()<usecounter::CellNameId>();
    dialog_context.cell_names_provider = [this] {
        std::vector<String> result;
        if (!use_counter_) return result;
        for (const auto& key : use_counter_->keys<usecounter::CellNameId>()) {
            const String name = static_cast<String>(key);
            if (!name.empty() && std::find(result.begin(), result.end(), name) == result.end())
                result.push_back(name);
        }
        std::stable_sort(result.begin(), result.end());
        return result;
    };

    for (const auto& value : values) {
    if (!value) continue;
    if (!create) {
        const auto existing = std::find_if(
            active_background_dialogs_.begin(), active_background_dialogs_.end(),
            [&value](const auto& item) { return item && item->back() == value; });
        if (existing != active_background_dialogs_.end()) {
            if (context_.services.gui) context_.services.gui->focus((*existing)->dialog());
            continue;
        }
    }
    std::shared_ptr<bgimagedialog::BgImageDialog> dialog;
    if (auto image = std::dynamic_pointer_cast<background::ImageCell>(value)) {
        dialog = std::make_shared<bgimagedialog::ImageCellDialog>(
            dialog_context, *context_.editor_props, summary_.get(), skin_.get(),
            image, create, parent_window, use_counter_);
    } else if (auto text = std::dynamic_pointer_cast<background::TextCell>(value)) {
        dialog = std::make_shared<bgimagedialog::TextCellDialog>(
            dialog_context, *context_.editor_props, summary_.get(), skin_.get(),
            text, create, parent_window, use_counter_);
    } else if (auto color = std::dynamic_pointer_cast<background::ColorCell>(value)) {
        dialog = std::make_shared<bgimagedialog::ColorCellDialog>(
            dialog_context, *context_.editor_props, summary_.get(), color,
            create, parent_window, use_counter_);
    } else if (auto pc = std::dynamic_pointer_cast<background::PCCell>(value)) {
        dialog = std::make_shared<bgimagedialog::PCCellDialog>(
            dialog_context, *context_.editor_props, summary_.get(), skin_.get(),
            pc, create, parent_window, use_counter_);
    }
    if (!dialog) continue;

    const auto transaction = std::make_shared<
        areaview::BgImagesView::ExternalBackEditPtr>(
            create ? areaview::BgImagesView::ExternalBackEditPtr{}
                   : background_view_->beginExternalBackEdit(value));
    const auto inserted = std::make_shared<bool>(!create);
    const auto weak_dialog = std::weak_ptr<bgimagedialog::BgImageDialog>(dialog);
    dialog->appliedEvent.push_back([this, weak_dialog, inserted, transaction] {
        const auto current = weak_dialog.lock();
        if (!current || !background_view_) return;
        if (!*inserted) {
            // ダイアログはモードレスで繰り返し適用できる。
            // 保持したオブジェクトは一度だけ挿入し、その後
            // の適用は同じセルへの外部編集として記録する。
            if (background_view_->insertCreatedBgImage(current->back()) >= 0) {
                *inserted = true;
                *transaction = background_view_->beginExternalBackEdit(current->back());
            }
        } else {
            (void)background_view_->commitExternalBackEdit(*transaction);
        }
        setApplyEnabled(true);
        refreshCellNameChoices();
        if (context_.editor_commons) {
            context_.editor_commons->refCellNames.call();
            context_.editor_commons->refreshToolBar();
        }
    });
    dialog->closeEvent.push_back([this, weak_dialog, transaction] {
        if (background_view_) background_view_->endExternalBackEdit(*transaction);
        const auto closed = weak_dialog.lock();
        std::erase_if(active_background_dialogs_, [&closed](const auto& item) {
            return !item || item == closed;
        });
    });
    // これらの編集部のAbsDialog::open()は、意図的にブロッ
    // クしない。親が閉じるまで制御部を保持する。スタック上のローカル制御
    // 部では、ユーザーが編集する前に開いたばかりの画面を破棄してしまう。
    active_background_dialogs_.push_back(dialog);
    (void)dialog->open();
    }
}

void BgImagesDialog::refreshCellNameChoices() {
    if (!cell_name_control_) return;
    const auto text = cell_name_control_->edit_text;
    cell_name_control_->items.clear();
    for (const auto& name : usedNames<usecounter::CellNameId>(use_counter_))
        cell_name_control_->items.push_back({name, name});
    cell_name_control_->edit_text = text;
}

bool BgImagesDialog::openCWXPath(String path, const bool activate) {
    return background_view_ && background_view_->openCWXPath(path, activate);
}

void BgImagesDialog::refreshWarning() {
    WarningList result;
    const auto* prop = context_.editor_props;
    if (!prop) {
        replaceWarnings(std::move(result));
        return;
    }
    const auto& messages = prop->requiredMsgs();
    if (cell_name_control_) {
        if (cell_name_.empty()) {
            // 意図的な追加として、元のダイアログの上書き処理では
            // 省略するD共通の必須名の診断を、ここでも公開する。
            result.push_back(messages.searchErrorNoCellName.value);
        } else {
            auto values = warning::sjisWarnings(
                prop->parent(), summary_, cell_name_, messages.cellName.value);
            result.insert(result.end(), std::make_move_iterator(values.begin()),
                          std::make_move_iterator(values.end()));
        }
    }

    std::unordered_map<String, bool> seen;
    const bool classic = summary_ && summary_->legacy();
    const auto version = summary_ ? summary_->dataVersion() : String(versioninfo::LATEST_VERSION);
    if (skin_ && background_container_) {
        for (const auto& background : background_container_->backs()) {
            if (!background) continue;
            auto values = warning::warnings(
                prop->parent(), *skin_, summary_.get(), *background, classic,
                version, prop->var().targetVersion.value);
            if (type_ == CType::ReplaceBgImage && ignore_effect_booster_) {
                if (const auto image =
                        std::dynamic_pointer_cast<background::ImageCell>(background)) {
                    const auto image_path = image->path();
                    const auto dot = image_path.find_last_of('.');
                    const auto extension = dot == String::npos
                        ? String{}
                        : enumutils::capLower(StringView(image_path).substr(dot));
                    if (extension == ".jpy1" || extension == ".jptx" ||
                        extension == ".jpdc") {
                        values.push_back(messages
                            .warningEffectBoosterFileWithReplaceBgImage.value);
                    }
                }
            }
            for (auto& warning_text : values) {
                if (!seen.emplace(warning_text, true).second) continue;
                result.push_back(std::move(warning_text));
            }
        }
    }
    if (type_ == CType::ReplaceBgImage && !targetVersion("1")) {
        const auto text = utils::tryFormat(
            messages.warningUnknownContentWsn.value, contentName(), "1");
        result.push_back(text);
    }
    for (const auto& text : transition_.warnings()) result.push_back(text);
    replaceWarnings(std::move(result));
}

void BgImagesDialog::refSkin() {
    EventDialog::refSkin();
    background_context_.skin_owner = skin_;
    if (background_view_) background_view_->refresh();
    // C++の意図的な追加として、現在のスキンの警告を直ちに表
    // 示する。Dは別の編集による更新まで、この一覧を変えない。
    refreshWarning();
}

void BgImagesDialog::refDataVersion() {
    transition_.refreshTS();
    refreshWarning();
}

void BgImagesDialog::refExternalPath(StringView, StringView, bool) {
    if (background_view_) background_view_->refresh();
    refreshWarning();
}

void BgImagesDialog::refExternalPaths(StringView) {
    if (background_view_) background_view_->refresh();
    refreshWarning();
}

void BgImagesDialog::refExternalFlagAndStep() {
    if (background_view_) background_view_->refresh();
    refreshWarning();
}

void BgImagesDialog::refExternalImageScale() {
    if (!background_view_) return;
    if (lib::editor_window_host_options().all_mode) background_view_->refresh();
    else background_view_->refImageScale();
}

void BgImagesDialog::refExternalUndoMax() {
    if (!background_undo_ || !context_.vars) return;
    background_undo_->max(static_cast<std::size_t>(
        std::max(0, context_.vars->undoMaxEvent.value)));
}

void LoseBgImageDialog::buildControls() {
    auto root = makeGridPanel(1U, 4.0F, {5.0F, 5.0F, 5.0F, 5.0F});
    const auto& messages = contentDialogMessages(context_);
    auto target = makeGroupBox(messages.cellName.value, 1U);
    auto cell = chooser::createCellNameCombo(
        chooserCommons(context_), summary_, nullptr, [] { return true; },
        cell_name_, chooserProperties(context_));
    const auto previous_text_changed = cell->on_text_changed;
    cell->on_text_changed = [this, previous_text_changed](const StringView value) {
        if (previous_text_changed) previous_text_changed(value);
        cell_name_ = String(value);
        setApplyEnabled(true);
    };
    const auto previous_selection_changed = cell->on_selection_changed;
    const std::weak_ptr<lib::ComboBox> weak_cell = cell;
    cell->on_selection_changed = [this, weak_cell, previous_selection_changed](
                                      const std::optional<std::size_t> index) {
        if (previous_selection_changed) previous_selection_changed(index);
        const auto combo = weak_cell.lock();
        if (!combo || !index || *index >= combo->items.size()) return;
        const auto& item = combo->items[*index];
        cell_name_ = item.value.empty() ? item.text : item.value;
        setApplyEnabled(true);
    };
    cell->set_layout_item({.row = 0U, .column = 0U,
                           .preferred = {260.0F, 22.0F}, .grow = 1.0F});
    target->add(cell);
    target->set_layout_item({.row = 0U, .column = 0U});
    root->add(target);
    auto transition = makeCompactTransitionControls(
        transition_, context_, [this] { setApplyEnabled(true); });
    transition->set_layout_item({.row = 1U, .column = 0U});
    root->add(transition);
    auto options = makeGridPanel(1U, 2.0F, {});
    auto anime = std::make_shared<lib::CheckBox>(messages.doAnime.value);
    anime->checked = do_anime_;
    anime->on_changed = [this](const bool value) {
        do_anime_ = value;
        setApplyEnabled(true);
    };
    anime->set_layout_item({.row = 0U, .column = 0U});
    options->add(anime);
    auto ignore = std::make_shared<lib::CheckBox>(messages.ignoreEffectBooster.value);
    ignore->checked = ignore_effect_booster_;
    ignore->on_changed = [this](const bool value) {
        ignore_effect_booster_ = value;
        setApplyEnabled(true);
    };
    ignore->set_layout_item({.row = 1U, .column = 0U});
    options->add(ignore);
    options->set_layout_item({.row = 2U, .column = 0U, .horizontal = lib::Align::End});
    root->add(options);
    installTypedBody(root, {393.0F, 163.0F}, true);
}

void ShowHidePartyDialog::buildControls() {
    auto root = makeGridPanel(1U);
    // Dはこのグループに、ローカライズされたカード速度のメッセージを付ける。
    const auto animation_title = context_.editor_props
        ? context_.editor_props->requiredMsgs().cardSpeed.value
        : String{"アニメーション"};
    auto animation = makeGroupBox(animation_title, 1U);
    animation_control_ = std::make_shared<spcarddialog::CardAnimationPanel>(
        animationCommons(context_, summary_.get(), skin_.get()), summary_.get(),
        animation, spcarddialog::CardAnimationPanelType::MenuCard);
    animation_control_->speed(card_speed_);
    animation_control_->widget()->set_layout_item(
        {.row = 0U, .column = 0U, .preferred = {290.0F, 42.0F},
         .constrain_minimum_to_preferred = true});
    version_control_updates_.push_back([this] { if (animation_control_) animation_control_->updateEnabled(); });
    animation_control_->modEvent.push_back([this] {
        if (!animation_control_) return;
        card_speed_ = animation_control_->speed();
        setApplyEnabled(true);
    });
    animation->set_layout_item({.row = 0U, .column = 0U});
    root->add(animation);
    installTypedBody(root, {350.0F, 145.0F}, true);
}

void ShowHidePartyDialog::refreshWarning() {
    // Dはこのダイアログの警告を、CardAnimationPanelだけから構成する。
    WarningList result;
    if (animation_control_) {
        const auto warnings = animation_control_->warnings();
        result.insert(result.end(), warnings.begin(), warnings.end());
    }
    replaceWarnings(std::move(result));
}

void RefreshDialog::buildControls() {
    auto root = makeGridPanel(1U);
    auto transition = makeTransitionControls(
        transition_, context_, [this] { setApplyEnabled(true); });
    transition->set_layout_item({.row = 0U, .column = 0U});
    root->add(transition);
    installTypedBody(root, {360.0F, 190.0F}, true);
}

void RefreshDialog::refreshWarning() {
    // Dはこのダイアログの警告を、TransitionPanelだけから構成する。
    replaceWarnings(transition_.warnings());
}

void MoveBgImageDialog::buildControls() {
    const auto left_max = context_.editor_props
        ? context_.editor_props->var().posLeftMax.value : 9999;
    const auto top_max = context_.editor_props
        ? context_.editor_props->var().posTopMax.value : 9999;
    const auto width_max = context_.editor_props
        ? context_.editor_props->var().backWidthMax.value : 9999;
    const auto height_max = context_.editor_props
        ? context_.editor_props->var().backHeightMax.value : 9999;
    x_ = clampSpinnerSelection(x_, -left_max, left_max);
    y_ = clampSpinnerSelection(y_, -top_max, top_max);
    width_ = clampSpinnerSelection(width_, -width_max, width_max);
    height_ = clampSpinnerSelection(height_, -height_max, height_max);
    auto root = makeGridPanel(1U, 4.0F, {5.0F, 5.0F, 5.0F, 5.0F});
    auto target = makeGroupBox("対象セル名称", 1U);
    auto cell = chooser::createCellNameCombo(
        chooserCommons(context_), summary_, nullptr, [] { return true; },
        cell_name_, chooserProperties(context_));
    const auto previous_text_changed = cell->on_text_changed;
    cell->on_text_changed = [this, previous_text_changed](const StringView value) {
        if (previous_text_changed) previous_text_changed(value);
        cell_name_ = String(value);
        setApplyEnabled(true);
    };
    const auto previous_selection_changed = cell->on_selection_changed;
    const std::weak_ptr<lib::ComboBox> weak_cell = cell;
    cell->on_selection_changed = [this, weak_cell, previous_selection_changed](
                                      const std::optional<std::size_t> index) {
        if (previous_selection_changed) previous_selection_changed(index);
        const auto combo = weak_cell.lock();
        if (!combo || !index || *index >= combo->items.size()) return;
        const auto& item = combo->items[*index];
        cell_name_ = item.value.empty() ? item.text : item.value;
        setApplyEnabled(true);
    };
    cell->set_layout_item({.row = 0U, .column = 0U,
                           .preferred = {260.0F, 22.0F}, .grow = 1.0F});
    target->add(cell);
    target->set_layout_item({.row = 0U, .column = 0U});
    root->add(target);

    auto geometry = makeGridPanel(2U, 4.0F, {});
    // Dは横・縦の各組を一行に置く。
    auto position_group = makeGroupBox("位置の変更", 4U, 1.0F);
    const lib::Array<String> coordinate_labels = {
        "変更無し", "直接指定", "現在値基準", "パーセンテージ"};
    auto refresh_position = std::make_shared<lib::Action>();
    for (std::size_t index = 0; index < coordinate_labels.size(); ++index) {
        auto radio = std::make_shared<lib::RadioButton>(coordinate_labels[index]);
        radio->group = "move-bg-position";
        radio->checked = static_cast<std::size_t>(position_type_) == index;
        radio->on_changed = [this, index, refresh_position](bool checked) {
            if (!checked) return;
            position_type_ = static_cast<types::CoordinateType>(index);
            if (*refresh_position) (*refresh_position)();
            setApplyEnabled(true);
        };
        radio->set_layout_item({.row = index, .column = 0U, .column_span = 2U});
        auto placement = radio->layout_item();
        placement.column_span = 4U;
        radio->set_layout_item(placement);
        position_group->add(radio);
    }
    addLabel(position_group, "横の値", 4U, 0U);
    auto x = std::make_shared<lib::NumberEdit>();
    x->maximum = context_.editor_props ? context_.editor_props->var().posLeftMax.value : 9999;
    x->minimum = -x->maximum; x->value = x_;
    x->on_changed = [this](const double value) { x_ = static_cast<int>(value); setApplyEnabled(true); };
    x->set_layout_item({.row = 4U, .column = 1U, .preferred = {48.0F, 20.0F}});
    position_group->add(x);
    addLabel(position_group, "縦の値", 4U, 2U);
    auto y = std::make_shared<lib::NumberEdit>();
    y->maximum = context_.editor_props ? context_.editor_props->var().posTopMax.value : 9999;
    y->minimum = -y->maximum; y->value = y_;
    y->on_changed = [this](const double value) { y_ = static_cast<int>(value); setApplyEnabled(true); };
    y->set_layout_item({.row = 4U, .column = 3U, .preferred = {48.0F, 20.0F}});
    position_group->add(y);
    *refresh_position = [this, x, y] {
        const bool enabled = position_type_ != types::CoordinateType::None;
        x->set_enabled(enabled);
        y->set_enabled(enabled);
    };
    (*refresh_position)();
    position_group->set_layout_item({.row = 0U, .column = 0U, .grow = 1.0F});
    geometry->add(position_group);

    auto size_group = makeGroupBox("サイズの変更", 4U, 1.0F);
    auto refresh_size = std::make_shared<lib::Action>();
    for (std::size_t index = 0; index < coordinate_labels.size(); ++index) {
        auto radio = std::make_shared<lib::RadioButton>(coordinate_labels[index]);
        radio->group = "move-bg-size";
        radio->checked = static_cast<std::size_t>(size_type_) == index;
        radio->on_changed = [this, index, refresh_size](bool checked) {
            if (!checked) return;
            size_type_ = static_cast<types::CoordinateType>(index);
            if (*refresh_size) (*refresh_size)();
            setApplyEnabled(true);
        };
        radio->set_layout_item({.row = index, .column = 0U, .column_span = 2U});
        auto placement = radio->layout_item();
        placement.column_span = 4U;
        radio->set_layout_item(placement);
        size_group->add(radio);
    }
    addLabel(size_group, "横の値", 4U, 0U);
    auto width = std::make_shared<lib::NumberEdit>();
    width->maximum = context_.editor_props ? context_.editor_props->var().backWidthMax.value : 9999;
    width->minimum = -width->maximum; width->value = width_;
    width->on_changed = [this](const double value) { width_ = static_cast<int>(value); setApplyEnabled(true); };
    width->set_layout_item({.row = 4U, .column = 1U, .preferred = {48.0F, 20.0F}});
    size_group->add(width);
    addLabel(size_group, "縦の値", 4U, 2U);
    auto height = std::make_shared<lib::NumberEdit>();
    height->maximum = context_.editor_props ? context_.editor_props->var().backHeightMax.value : 9999;
    height->minimum = -height->maximum; height->value = height_;
    height->on_changed = [this](const double value) { height_ = static_cast<int>(value); setApplyEnabled(true); };
    height->set_layout_item({.row = 4U, .column = 3U, .preferred = {48.0F, 20.0F}});
    size_group->add(height);
    *refresh_size = [this, width, height] {
        const bool enabled = size_type_ != types::CoordinateType::None;
        width->set_enabled(enabled);
        height->set_enabled(enabled);
    };
    (*refresh_size)();
    size_group->set_layout_item({.row = 0U, .column = 1U, .grow = 1.0F});
    geometry->add(size_group);
    geometry->set_layout_item({.row = 1U, .column = 0U});
    root->add(geometry);

    auto transition = makeCompactTransitionControls(
        transition_, context_, [this] { setApplyEnabled(true); });
    transition->set_layout_item({.row = 2U, .column = 0U});
    root->add(transition);
    auto options = makeGridPanel(1U, 2.0F, {});
    auto anime = std::make_shared<lib::CheckBox>("JPY1アニメーションを実行する");
    anime->checked = do_anime_;
    anime->on_changed = [this](const bool value) { do_anime_ = value; setApplyEnabled(true); };
    anime->set_layout_item({.row = 0U, .column = 0U});
    options->add(anime);
    auto ignore = std::make_shared<lib::CheckBox>("エフェクトブースター関係のセルを無視する");
    ignore->checked = ignore_effect_booster_;
    ignore->on_changed = [this](const bool value) { ignore_effect_booster_ = value; setApplyEnabled(true); };
    ignore->set_layout_item({.row = 1U, .column = 0U});
    options->add(ignore);
    options->set_layout_item({.row = 3U, .column = 0U, .horizontal = lib::Align::End});
    root->add(options);
    installTypedBody(root, {393.0F, 301.0F}, true);
}

void MoveCardDialog::buildControls() {
    const auto pos_max = context_.editor_props
        ? context_.editor_props->var().posLeftMax.value : 9999;
    const auto top_max = context_.editor_props
        ? context_.editor_props->var().posTopMax.value : 9999;
    const auto scale_min = context_.editor_props
        ? context_.editor_props->var().cardScaleMin.value : 50;
    const auto scale_max = context_.editor_props
        ? context_.editor_props->var().cardScaleMax.value : 300;
    const auto layer_max = context_.editor_props
        ? context_.editor_props->var().layerMax.value : 9999;
    x_ = clampSpinnerSelection(x_, -pos_max, pos_max);
    y_ = clampSpinnerSelection(y_, -top_max, top_max);
    scale_ = clampSpinnerSelection(scale_, scale_min, scale_max);
    layer_ = clampSpinnerSelection(layer_, types::LAYER_BACK_CELL, layer_max);
    auto root = makeGridPanel(2U);
    auto target = makeGroupBox("対象カードグループ", 1U);
    auto group_name = chooser::createCardGroupCombo(
        chooserCommons(context_), summary_, nullptr, [this] { return true; },
        card_group_, chooserProperties(context_));
    group_name->on_selection_changed = [this, group_name](
                                            const std::optional<std::size_t> index) {
        if (!index || *index >= group_name->items.size()) return;
        card_group_ = group_name->items[*index].value;
        setApplyEnabled(true);
        refreshWarning();
    };
    const auto previous_text_changed = group_name->on_text_changed;
    group_name->on_text_changed = [this, previous_text_changed](const StringView value) {
        if (previous_text_changed) previous_text_changed(value);
        card_group_ = String(value);
        setApplyEnabled(true);
        refreshWarning();
    };
    group_name->set_layout_item({.row = 0U, .column = 0U,
                                 .preferred = {292.0F, 24.0F}, .grow = 1.0F});
    target->add(group_name);
    target->set_layout_item({.row = 0U, .column = 0U, .column_span = 2U,
                             .preferred = {305.0F, 54.0F}});
    root->add(target);

    auto position_group = makeGroupBox("位置の変更", 2U);
    auto x = std::make_shared<lib::NumberEdit>();
    x->minimum = -pos_max; x->maximum = pos_max; x->value = x_;
    x->on_changed = [this](const double value) { x_ = static_cast<int>(value); setApplyEnabled(true); };
    x->set_layout_item({.row = 4U, .column = 1U, .preferred = {52.0F, 24.0F}});
    auto y = std::make_shared<lib::NumberEdit>();
    y->minimum = -top_max; y->maximum = top_max; y->value = y_;
    y->on_changed = [this](const double value) { y_ = static_cast<int>(value); setApplyEnabled(true); };
    y->set_layout_item({.row = 5U, .column = 1U, .preferred = {52.0F, 24.0F}});
    const std::array<std::pair<types::CoordinateType, String>, 4U> positions{{
        {types::CoordinateType::None, "変更無し"},
        {types::CoordinateType::Absolute, "直接指定"},
        {types::CoordinateType::Relative, "現在値基準"},
        {types::CoordinateType::Percentage, "パーセンテージ"}}};
    for (std::size_t index = 0; index < positions.size(); ++index) {
        const auto [value, label] = positions[index];
        auto radio = std::make_shared<lib::RadioButton>(label);
        radio->group = "move-card-position";
        radio->checked = position_type_ == value;
        radio->on_changed = [this, value, x, y](const bool checked) {
            if (!checked) return;
            position_type_ = value;
            const bool enabled = value != types::CoordinateType::None;
            x->set_enabled(enabled);
            y->set_enabled(enabled);
            setApplyEnabled(true);
        };
        radio->set_layout_item({.row = index, .column = 0U,
                                .column_span = 2U,
                                .preferred = {96.0F, 24.0F}});
        position_group->add(radio);
    }
    addLabel(position_group, "横の値", 4U, 0U);
    position_group->add(x);
    addLabel(position_group, "縦の値", 5U, 0U);
    position_group->add(y);
    const bool position_enabled = position_type_ != types::CoordinateType::None;
    x->set_enabled(position_enabled);
    y->set_enabled(position_enabled);
    position_group->set_layout_item({.row = 1U, .column = 0U, .row_span = 3U,
                                     .preferred = {108.0F, 226.0F}});
    root->add(position_group);

    auto scale_group = makeGroupBox("拡大率", 3U);
    auto change_scale = std::make_shared<lib::CheckBox>("拡大率を変更する");
    change_scale->checked = change_scale_;
    auto scale = std::make_shared<lib::NumberEdit>();
    scale->minimum = scale_min;
    scale->maximum = scale_max;
    scale->value = scale_;
    scale->set_enabled(change_scale_);
    change_scale->on_changed = [this, scale](const bool value) {
        change_scale_ = value; scale->set_enabled(value); setApplyEnabled(true);
    };
    scale->on_changed = [this](const double value) { scale_ = static_cast<int>(value); setApplyEnabled(true); };
    change_scale->set_layout_item({.row = 0U, .column = 0U});
    scale->set_layout_item({.row = 0U, .column = 1U, .preferred = {52.0F, 24.0F}});
    scale_group->add(change_scale); scale_group->add(scale);
    addLabel(scale_group, "%", 0U, 2U);
    scale_group->set_layout_item({.row = 1U, .column = 1U,
                                  .preferred = {223.0F, 68.0F}});
    root->add(scale_group);

    auto layer_group = makeGroupBox("レイヤ", 2U);
    auto change_layer = std::make_shared<lib::CheckBox>("レイヤを変更する");
    change_layer->checked = change_layer_;
    auto layer = std::make_shared<lib::NumberEdit>();
    layer->minimum = types::LAYER_BACK_CELL;
    layer->maximum = layer_max;
    layer->value = layer_;
    layer->set_enabled(change_layer_);
    change_layer->on_changed = [this, layer](const bool value) {
        change_layer_ = value; layer->set_enabled(value); setApplyEnabled(true);
    };
    layer->on_changed = [this](const double value) { layer_ = static_cast<int>(value); setApplyEnabled(true); };
    change_layer->set_layout_item({.row = 0U, .column = 0U});
    layer->set_layout_item({.row = 0U, .column = 1U,
                            .preferred = {52.0F, 24.0F}});
    layer_group->add(change_layer); layer_group->add(layer);
    layer_group->set_layout_item({.row = 2U, .column = 1U,
                                  .preferred = {223.0F, 68.0F}});
    root->add(layer_group);

    auto animation = makeGroupBox("アニメーション", 1U);
    animation_control_ = std::make_shared<spcarddialog::CardAnimationPanel>(
        animationCommons(context_, summary_.get(), skin_.get()), summary_.get(),
        animation, spcarddialog::CardAnimationPanelType::MoveCard);
    animation_control_->speed(card_speed_);
    animation_control_->overrideCardSpeed(override_card_speed_);
    animation_control_->widget()->set_layout_item(
        {.row = 0U, .column = 0U, .preferred = {210.0F, 56.0F},
         .constrain_minimum_to_preferred = true});
    version_control_updates_.push_back([this] { if (animation_control_) animation_control_->updateEnabled(); });
    animation_control_->modEvent.push_back([this] {
        if (!animation_control_) return;
        card_speed_ = animation_control_->speed();
        override_card_speed_ = animation_control_->overrideCardSpeed();
        setApplyEnabled(true);
        refreshWarning();
    });
    animation->set_layout_item({.row = 3U, .column = 1U,
                                .preferred = {223.0F, 84.0F}});
    root->add(animation);
    installTypedBody(root, {313.0F, 296.0F}, true);
    refreshWarning();
}

void MoveCardDialog::refreshWarning() {
    WarningList warnings;
    if (!context_.prop) {
        replaceWarnings(std::move(warnings));
        return;
    }
    const auto& messages = context_.prop->requiredMsgs();
    // 意図的な追加として、この編集部にも共通の必須グループの警告を
    // 残す。Dのダイアログの上書き処理は文字コードだけを検査する。
    if (card_group_.empty())
        warnings.push_back(messages.searchErrorNoCardGroup.value);
    const auto encoding = warning::sjisWarnings(
        *context_.prop, summary_, card_group_, messages.cardGroup.value);
    warnings.insert(warnings.end(), encoding.begin(), encoding.end());
    if (!targetVersion("3"))
        warnings.push_back(utils::tryFormat(
            messages.warningUnknownContentWsn.value, contentName(), "3"));
    if (animation_control_) {
        const auto animation = animation_control_->warnings();
        warnings.insert(warnings.end(), animation.begin(), animation.end());
    }
    replaceWarnings(std::move(warnings));
}

bool BgImagesDialog::apply() {
    auto value = ensureDraft();
    if (legacy_backs_override_) {
        // 従来のパス一覧の呼び出し側も、同じ型付き背景セルへ設定する。区切り
        // 文字の表記はContentの共通PathUser境界で管理する。
        value->backs(backs_);
    } else if (background_container_) {
        background::BgImageArray cells;
        for (const auto& cell : background_container_->backs())
            if (cell) cells.push_back(cell->dup());
        value->backgroundImages(std::move(cells));
    }
    value->transition(transition_.transition());
    value->transitionSpeed(transition_.transitionSpeed());
    value->cellName(cell_name_);
    value->doAnime(do_anime_);
    value->ignoreEffectBooster(ignore_effect_booster_);
    return commitDraft();
}

void LoseBgImageDialog::writeEditedFields(const ContentPtr& value) const {
    value->cellName(cell_name_);
    value->transition(transition_.transition());
    value->transitionSpeed(transition_.transitionSpeed());
    value->doAnime(do_anime_);
    value->ignoreEffectBooster(ignore_effect_booster_);
}

bool LoseBgImageDialog::apply() {
    writeEditedFields(ensureDraft());
    return commitDraft();
}

void BgmDialog::writeEditedFields(const ContentPtr& value) const {
    value->bgmPath(path_);
    value->bgmVolume(volume_);
    value->bgmLoopCount(loop_count_);
    value->bgmFadeIn(fade_in_);
    value->bgmChannel(channel_);
}

bool BgmDialog::apply() {
    writeEditedFields(ensureDraft());
    return commitDraft();
}

void SeDialog::writeEditedFields(const ContentPtr& value) const {
    value->soundPath(path_);
    value->soundVolume(volume_);
    value->soundLoopCount(loop_count_);
    value->soundFadeIn(fade_in_);
    value->soundChannel(channel_);
}

bool SeDialog::apply() {
    writeEditedFields(ensureDraft());
    return commitDraft();
}

void ShowHidePartyDialog::writeEditedFields(const ContentPtr& value) const {
    value->cardSpeed(card_speed_);
}

bool ShowHidePartyDialog::apply() {
    writeEditedFields(ensureDraft());
    return commitDraft();
}

void RefreshDialog::writeEditedFields(const ContentPtr& value) const {
    value->transition(transition_.transition());
    value->transitionSpeed(transition_.transitionSpeed());
}

bool RefreshDialog::apply() {
    writeEditedFields(ensureDraft());
    return commitDraft();
}

bool BrRandomSelectDialog::apply() {
    const std::array<types::CastRange, 3U> choices{
        types::CastRange::Party, types::CastRange::Enemy, types::CastRange::Npc};
    lib::Array<types::CastRange> selected_ranges;
    for (const auto choice : choices)
        if (std::find(cast_range_.begin(), cast_range_.end(), choice) !=
            cast_range_.end())
            selected_ranges.push_back(choice);
    cast_range_ = std::move(selected_ranges);
    auto value = ensureDraft();
    value->castRange(cast_range_);
    if (has_level_) {
        if (level_max_ != 0 && level_min_ > level_max_)
            level_min_ = level_max_;
        value->levelMin(level_min_);
        value->levelMax(level_max_);
    } else {
        value->levelMin(0);
        value->levelMax(0);
    }
    value->status(has_status_ ? status_ : types::Status::None);
    value->invertResult(invert_result_);
    return commitDraft();
}

void BrKeyCodeDialog::writeEditedFields(const ContentPtr& value) const {
    value->keyCodeRange(range_);
    value->targetIsSkill(target_skill_);
    value->targetIsItem(target_item_);
    value->targetIsBeast(target_beast_);
    value->targetIsHand(target_hand_);
    value->keyCode(key_code_);
    value->matchingCondition(matching_condition_);
    value->invertResult(invert_result_);
    value->selectCard(select_card_);
}

bool BrKeyCodeDialog::apply() {
    writeEditedFields(ensureDraft());
    return commitDraft();
}

void BranchRoundDialog::writeEditedFields(const ContentPtr& value) const {
    value->round(round_);
    value->comparison3(comparison_);
}

bool BranchRoundDialog::apply() {
    writeEditedFields(ensureDraft());
    return commitDraft();
}

void MoveBgImageDialog::writeEditedFields(const ContentPtr& value) const {
    value->cellName(cell_name_);
    value->positionType(position_type_);
    value->x(x_);
    value->y(y_);
    value->sizeType(size_type_);
    value->width(width_);
    value->height(height_);
    value->transition(transition_.transition());
    value->transitionSpeed(transition_.transitionSpeed());
    value->doAnime(do_anime_);
    value->ignoreEffectBooster(ignore_effect_booster_);
}

bool MoveBgImageDialog::apply() {
    writeEditedFields(ensureDraft());
    return commitDraft();
}

void MoveCardDialog::writeEditedFields(const ContentPtr& value) const {
    value->cardGroup(card_group_);
    value->positionType(position_type_);
    value->x(x_);
    value->y(y_);
    value->scale(change_scale_ ? scale_ : -1);
    value->layer(change_layer_ ? layer_ : -1);
    value->cardSpeed(card_speed_);
    value->overrideCardSpeed(override_card_speed_);
}

bool MoveCardDialog::apply() {
    writeEditedFields(ensureDraft());
    return commitDraft();
}

void BranchMultiCouponDialog::writeEditedFields(const ContentPtr& value) const {
    value->range(range_.range());
    value->expandSPChars(expand_sp_chars_);
}

bool BranchMultiCouponDialog::apply() {
    writeEditedFields(ensureDraft());
    return commitDraft();
}

void LoseBgImageDialog::refreshWarning() {
    // Dの順序に合わせ、セル名の文字コード、Wsn.1の制限、TransitionPanelの順に検査する。
    WarningList result;
    if (context_.prop)
        result = warning::sjisWarnings(*context_.prop, summary_.get(),
                                       cell_name_, contentDialogMessages(context_).cellName.value);
    if (context_.editor_props &&
        !context_.editor_props->isTargetVersion(summary_.get(), "1")) {
        const auto& messages = context_.editor_props->requiredMsgs();
        result.push_back(utils::tryFormat(
            messages.warningUnknownContentWsn.value,
            messages.contentName(CType::LoseBgImage), "1"));
    }
    const auto& transition_warnings = transition_.warnings();
    result.insert(result.end(), transition_warnings.begin(),
                  transition_warnings.end());
    replaceWarnings(std::move(result));
}

void BrRandomSelectDialog::refreshWarning() {
    // Dの排他的な1.30/1.50の分岐に続き、反転の警告を行う順序に合わせる。
    WarningList result;
    if (context_.editor_props) {
        const auto& editor_props = *context_.editor_props;
        const auto& messages = editor_props.requiredMsgs();
        if (!editor_props.targetVersion(summary_.get(), "1.30")) {
            result.push_back(utils::tryFormat(
                messages.warningUnknownContent.value,
                messages.contentName(CType::BranchRandomSelect), "1.30"));
        } else if (!editor_props.targetVersion(summary_.get(), "1.50")) {
            const auto status = status_;
            if (static_cast<std::int32_t>(status) >=
                static_cast<std::int32_t>(types::Status::Silence)) {
                result.push_back(utils::tryFormat(
                    messages.warningBranchStatusMental.value,
                    messages.statusName(status), "1.50"));
            }
        }
        if (invert_result_ &&
            !editor_props.isTargetVersion(summary_.get(), "4"))
            result.push_back(messages.warningInvertResult.value);
    }
    replaceWarnings(std::move(result));
}

void BrKeyCodeDialog::refreshWarning() {
    // Dの旧形式・Wsnのカテゴリ、範囲、HasNot、反転の制限は局所的に扱う。
    WarningList result;
    if (context_.prop)
        result = warning::sjisWarnings(*context_.prop, summary_.get(),
                                       key_code_, contentDialogMessages(context_).keyCode.value);
    if (context_.editor_props) {
        const auto& editor_props = *context_.editor_props;
        const auto& messages = editor_props.requiredMsgs();
        const bool legacy = summary_ && summary_->legacy();
        if (legacy) {
            if (!editor_props.targetVersion(summary_.get(), "1.50")) {
                result.push_back(utils::tryFormat(
                    messages.warningUnknownContent.value,
                    messages.contentName(CType::BranchKeyCode), "1.50"));
            }
            const bool all = target_skill_ && target_item_ && target_beast_ && target_hand_;
            const bool item = !target_skill_ && target_item_ && !target_beast_ && target_hand_;
            if (all || item)
                result.push_back(messages.warningBranchKeyCodeWithItem.value);
        } else if (!editor_props.isTargetVersion(summary_.get(), "2")) {
            const bool supported = !target_hand_ &&
                ((target_skill_ && target_item_ && target_beast_) ||
                 (target_skill_ && !target_item_ && !target_beast_) ||
                 (!target_skill_ && target_item_ && !target_beast_) ||
                 (!target_skill_ && !target_item_ && target_beast_));
            if (!supported)
                result.push_back(messages.warningBranchKeyCodeAtWsn1.value);
        }
        if (!legacy && !editor_props.isTargetVersion(summary_.get(), "3") &&
            select_card_)
            result.push_back(messages.warningSelectCard.value);
        if (!editor_props.isTargetVersion(summary_.get(), "3") &&
            range_ == types::Range::SelectedCard)
            result.push_back(utils::tryFormat(messages.warningRangeSelectedCard.value));
        if (!editor_props.isTargetVersion(summary_.get(), "5") &&
            matching_condition_ == types::MatchingCondition::HasNot)
            result.push_back(messages.warningHasNotBranchKeyCode.value);
        if (!editor_props.isTargetVersion(summary_.get(), "4") && invert_result_)
            result.push_back(messages.warningInvertResult.value);
    }
    replaceWarnings(std::move(result));
}

void MoveBgImageDialog::refreshWarning() {
    // Dの順序に合わせ、セル名の文字コード、Wsn.1の制限、TransitionPanelの順に検査する。
    WarningList result;
    if (context_.prop)
        result = warning::sjisWarnings(*context_.prop, summary_.get(),
                                       cell_name_, contentDialogMessages(context_).cellName.value);
    if (context_.editor_props &&
        !context_.editor_props->isTargetVersion(summary_.get(), "1")) {
        const auto& messages = context_.editor_props->requiredMsgs();
        result.push_back(utils::tryFormat(
            messages.warningUnknownContentWsn.value,
            messages.contentName(CType::MoveBgImage), "1"));
    }
    const auto& transition_warnings = transition_.warnings();
    result.insert(result.end(), transition_warnings.begin(),
                  transition_warnings.end());
    replaceWarnings(std::move(result));
}

void BranchMultiCouponDialog::refreshWarning() {
    // Dが扱うのは、これらのコンテントのバージョン、NPC範囲、展開の警告だけ。
    WarningList result;
    if (context_.editor_props) {
        const auto& editor_props = *context_.editor_props;
        const auto& messages = editor_props.requiredMsgs();
        if (!editor_props.isTargetVersion(summary_.get(), "2"))
            result.push_back(utils::tryFormat(
                messages.warningUnknownContentWsn.value,
                messages.contentName(CType::BranchMultiCoupon), "2"));
        if (range_.range() == types::Range::Npc &&
            !editor_props.isTargetVersion(summary_.get(), "5"))
            result.push_back(messages.warningBranchCouponAtNpc.value);
        if (expand_sp_chars_ &&
            !editor_props.isTargetVersion(summary_.get(), "4"))
            result.push_back(messages.warningExpandSPCharsInCoupon.value);
    }
    replaceWarnings(std::move(result));
}

void EffectBreakDialog::buildControls() {
    auto root = makeGridPanel(1U, 4.0F, {5.0F, 5.0F, 5.0F, 5.0F});
    const auto& messages = contentDialogMessages(context_);
    auto group = makeGroupBox(messages.transitionType.value, 1U);
    auto consume = std::make_shared<lib::CheckBox>(messages.consumeCard.value);
    consume_card_control_ = consume;
    consume->checked = consume_card_;
    consume->on_changed = [this](const bool checked) {
        consume_card_ = checked;
        setApplyEnabled(true);
        refDataVersion();
    };
    consume->set_layout_item({.row = 0U, .column = 0U,
                              .preferred = {325.0F, 24.0F}});
    group->add(consume);
    root->add(group);
    installTypedBody(root, {360.0F, 145.0F}, true);
    refDataVersion();
}

void EffectBreakDialog::writeEditedFields(const ContentPtr& value) const {
    value->consumeCard(consume_card_);
}

bool EffectBreakDialog::apply() {
    writeEditedFields(ensureDraft());
    return commitDraft();
}

void EffectBreakDialog::refDataVersion() {
    // 旧形式では「発動カードを消費する
    // 」が有効ならチェック欄を無効化し
    // 、その設定を維持する。無効な場合
    // は互換な設定へ変更できるよう操
    // 作を許す。基底の初期更新時はチ
    // ェック欄が未生成の場合がある。
    if (const auto consume = consume_card_control_.lock()) {
        const bool legacy = summary_ && summary_->legacy();
        consume->set_enabled(!legacy || !consume_card_);
    }
    EventDialog::refDataVersion();
}

void EffectBreakDialog::refreshWarning() {
    WarningList warnings;
    if (!consume_card_ && !targetVersion("3") && context_.prop)
        warnings.push_back(context_.prop->requiredMsgs().warningConsumeCard.value);
    replaceWarnings(std::move(warnings));
}

void ChangeEnvironmentDialog::buildControls() {
    auto root = makeGridPanel(1U, 5.0F, {5.0F, 5.0F, 5.0F, 5.0F});
    const auto& messages = contentDialogMessages(context_);
    auto group = makeGroupBox(messages.environmentStatus.value, 2U);
    const auto make_status = [this, group](String label,
                                           types::EnvironmentStatus initial,
                                           const std::size_t row,
                                           auto setter) {
        addLabel(group, std::move(label), row, 0U, 1U, {135.0F, 24.0F});
        auto combo = std::make_shared<lib::ComboBox>();
        combo->editable = false;
        combo->items = {{"設定しない", "NotSet"},
                        {"有効にする", "Enable"},
                        {"無効にする", "Disable"}};
        for (std::size_t i = 0; i < combo->items.size(); ++i) {
            const auto status = static_cast<types::EnvironmentStatus>(i);
            if (context_.prop)
                combo->items[i].text = row == 0U
                    ? context_.prop->requiredMsgs().environmentStatusName(status)
                    : context_.prop->requiredMsgs().environmentStatusNameEnabled(status);
            else if (row == 0U && i > 0U)
                combo->items[i].text = i == 1U ? "使用可能にする" : "使用不能にする";
        }
        combo->selected_index = std::min<std::size_t>(
            static_cast<std::size_t>(initial), combo->items.size() - 1U);
        combo->on_selection_changed = [this, setter](const auto index) {
            if (!index || *index > 2U) return;
            setter(static_cast<types::EnvironmentStatus>(*index));
            setApplyEnabled(true);
            refDataVersion();
        };
        combo->set_layout_item({.row = row, .column = 1U,
                                .preferred = {130.0F, 24.0F}});
        group->add(combo);
    };
    make_status(messages.backpack.value, backpack_enabled_, 0U,
                [this](const auto value) { backpack_enabled_ = value; });
    make_status(messages.gameOver.value, game_over_enabled_, 1U,
                [this](const auto value) { game_over_enabled_ = value; });
    make_status(messages.runAway.value, run_away_enabled_, 2U,
                [this](const auto value) { run_away_enabled_ = value; });
    root->add(group);
    installTypedBody(root, {315.0F, 225.0F}, true);
    refDataVersion();
}

void ChangeEnvironmentDialog::writeEditedFields(const ContentPtr& value) const {
    value->backpackEnabled(backpack_enabled_);
    value->gameOverEnabled(game_over_enabled_);
    value->runAwayEnabled(run_away_enabled_);
}

bool ChangeEnvironmentDialog::apply() {
    writeEditedFields(ensureDraft());
    return commitDraft();
}

void ChangeEnvironmentDialog::refreshWarning() {
    WarningList warnings;
    if (!context_.prop) {
        replaceWarnings(std::move(warnings));
        return;
    }
    const auto& messages = context_.prop->requiredMsgs();
    if (!targetVersion("4"))
        warnings.push_back(utils::tryFormat(
            messages.warningUnknownContentWsn.value, contentName(), "4"));
    if (game_over_enabled_ != types::EnvironmentStatus::NotSet &&
        !targetVersion("5"))
        warnings.push_back(utils::tryFormat(messages.warningEnvironmentEnabled.value,
                                            messages.gameOver.value, "5"));
    if (run_away_enabled_ != types::EnvironmentStatus::NotSet &&
        !targetVersion("5"))
        warnings.push_back(utils::tryFormat(messages.warningEnvironmentEnabled.value,
                                            messages.runAway.value, "5"));
    replaceWarnings(std::move(warnings));
}

bool hasDialog(const CType type, const bool existsSummary) {
    switch (type) {
    case CType::Start:
    case CType::EndBadEnd:
    case CType::ElapseTime:
    case CType::BranchArea:
    case CType::BranchBattle:
    case CType::BranchIsBattle:
    case CType::BranchMultiRandom:
        return false;
    case CType::StartBattle:
    case CType::LinkStart:
    case CType::CallStart:
    case CType::LinkPackage:
    case CType::CallPackage:
    case CType::BranchFlag:
    case CType::SetFlag:
    case CType::ReverseFlag:
    case CType::BranchMultiStep:
    case CType::BranchStep:
    case CType::SetStep:
    case CType::SetStepUp:
    case CType::SetStepDown:
    case CType::SubstituteStep:
    case CType::SubstituteFlag:
    case CType::BranchStepCmp:
    case CType::BranchFlagCmp:
    case CType::CheckFlag:
    case CType::BranchCast:
    case CType::BranchInfo:
    case CType::GetInfo:
    case CType::LoseCast:
    case CType::LoseInfo:
        return existsSummary;
    default:
        return true;
    }
}

EventDialogPtr createEventDialog(const Context& context, SummaryPtr summary,
                                 SkinPtr forceSkin, UseCounterPtr useCounter,
                                 Parent parent, ContentPtr value,
                                 ContentPtr parentContent, const bool create) {
    if (!value || !hasDialog(value->type(), static_cast<bool>(summary)))
        return EventDialogPtr{};
    if (create) applyCreateDefaults(value);
    const auto type = value->type();
    switch (type) {
    case CType::ChangeArea:
        return makeDialog<AreaSelectDialog<CType::ChangeArea>>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::StartBattle:
        return makeDialog<AreaSelectDialog<CType::StartBattle>>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::CallPackage:
        return makeDialog<AreaSelectDialog<CType::CallPackage>>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::LinkPackage:
        return makeDialog<AreaSelectDialog<CType::LinkPackage>>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::BranchCast:
        return makeDialog<AreaSelectDialog<CType::BranchCast>>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::GetCast:
        return makeDialog<AreaSelectDialog<CType::GetCast>>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::LoseCast:
        return makeDialog<AreaSelectDialog<CType::LoseCast>>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::BranchInfo:
        return makeDialog<AreaSelectDialog<CType::BranchInfo>>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::GetInfo:
        return makeDialog<AreaSelectDialog<CType::GetInfo>>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::LoseInfo:
        return makeDialog<AreaSelectDialog<CType::LoseInfo>>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::LinkStart:
        return makeDialog<StartSelectDialog<CType::LinkStart>>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::CallStart:
        return makeDialog<StartSelectDialog<CType::CallStart>>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::End:
        return makeDialog<ClearEventDialog>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::BranchCoupon:
        return makeDialog<BranchCouponDialog>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::GetCoupon:
        return makeDialog<GetCouponDialog>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::LoseCoupon:
        return makeDialog<LoseCouponDialog>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::BranchGossip:
    case CType::GetGossip:
    case CType::LoseGossip:
        if (type == CType::BranchGossip)
            return makeDialog<GossipEventDialog<CType::BranchGossip>>(create,
                context, summary, parent, parentContent, value, forceSkin, useCounter);
        if (type == CType::GetGossip)
            return makeDialog<GossipEventDialog<CType::GetGossip>>(create,
                context, summary, parent, parentContent, value, forceSkin, useCounter);
        return makeDialog<GossipEventDialog<CType::LoseGossip>>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::BranchCompleteStamp:
    case CType::GetCompleteStamp:
    case CType::LoseCompleteStamp:
        if (type == CType::BranchCompleteStamp)
            return makeDialog<EndEventDialog<CType::BranchCompleteStamp>>(create,
                context, summary, parent, parentContent, value, forceSkin, useCounter);
        if (type == CType::GetCompleteStamp)
            return makeDialog<EndEventDialog<CType::GetCompleteStamp>>(create,
                context, summary, parent, parentContent, value, forceSkin, useCounter);
        return makeDialog<EndEventDialog<CType::LoseCompleteStamp>>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::ChangeBgImage:
    case CType::ReplaceBgImage:
        return makeDialog<BgImagesDialog>(create,
            context, summary, parent, parentContent, value, type, forceSkin,
            useCounter);
    case CType::LoseBgImage:
        return makeDialog<LoseBgImageDialog>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::PlayBgm:
        return makeDialog<BgmDialog>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::PlaySound:
        return makeDialog<SeDialog>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::Wait:
        return makeDialog<WaitEventDialog>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::BranchRandom:
        return makeDialog<BrRandomEventDialog>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::BranchPartyNumber:
        return makeDialog<BrNumEventDialog>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::BranchMoney:
        return makeDialog<NumericEventDialog<CType::BranchMoney>>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::GetMoney:
        return makeDialog<NumericEventDialog<CType::GetMoney>>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::LoseMoney:
        return makeDialog<NumericEventDialog<CType::LoseMoney>>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::Effect:
        return makeDialog<EffectDialog>(create,
            context, summary, parent, parentContent, value, useCounter, forceSkin);
    case CType::TalkMessage:
        return makeDialog<MessageEventDialog>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::TalkDialog:
        return makeDialog<SpeakEventDialog>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::EffectBreak:
        return makeDialog<EffectBreakDialog>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::BranchFlag:
        return makeDialog<BrFlagDialog>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::BranchMultiStep:
        return makeDialog<BrStepNDialog>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::BranchStep:
        return makeDialog<BrStepULDialog>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::SetFlag:
        return makeDialog<FlagSetDialog>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::SetStep:
        return makeDialog<StepSetDialog>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::SetStepUp:
        return makeDialog<StepPlusDialog>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::SetStepDown:
        return makeDialog<StepMinusDialog>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::ReverseFlag:
        return makeDialog<FlagRDialog>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::CheckFlag:
        return makeDialog<FlagJudgeDialog>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::CheckStep:
        return makeDialog<CheckStepDialog>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::SubstituteStep:
        return makeDialog<SubstituteStepDialog>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::SubstituteFlag:
        return makeDialog<SubstituteFlagDialog>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::BranchStepCmp:
        return makeDialog<BrStepCmpDialog>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::BranchFlagCmp:
        return makeDialog<BrFlagCmpDialog>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::BranchSelect:
        return makeDialog<BrMemberDialog>(create,
            context, summary, parent, parentContent, value, useCounter, forceSkin);
    case CType::BranchAbility:
        return makeDialog<BrPowerDialog>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::BranchLevel:
        return makeDialog<BrLevelDialog>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::BranchStatus:
        return makeDialog<BrStateDialog>(create,
            context, summary, parent, parentContent, value, useCounter, forceSkin);
    case CType::BranchSkill:
        return makeDialog<BrSkillDialog>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::BranchItem:
        return makeDialog<BrItemDialog>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::BranchBeast:
        return makeDialog<BrBeastDialog>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::GetSkill:
        return makeDialog<GetSkillDialog>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::GetItem:
        return makeDialog<GetItemDialog>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::GetBeast:
        return makeDialog<GetBeastDialog>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::LoseSkill:
        return makeDialog<LostSkillDialog>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::LoseItem:
        return makeDialog<LostItemDialog>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::LoseBeast:
        return makeDialog<LostBeastDialog>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::ShowParty:
    case CType::HideParty:
        return makeDialog<ShowHidePartyDialog>(create,
            type, context, summary, parent, parentContent, value, forceSkin,
            useCounter);
    case CType::Redisplay:
        return makeDialog<RefreshDialog>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::BranchRandomSelect:
        return makeDialog<BrRandomSelectDialog>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::BranchKeyCode:
        return makeDialog<BrKeyCodeDialog>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::BranchRound:
        return makeDialog<BranchRoundDialog>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::MoveBgImage:
        return makeDialog<MoveBgImageDialog>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::MoveCard:
        return makeDialog<MoveCardDialog>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::BranchMultiCoupon:
        return makeDialog<BranchMultiCouponDialog>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::ChangeEnvironment:
        return makeDialog<ChangeEnvironmentDialog>(create,
            context, summary, parent, parentContent, value, forceSkin, useCounter);
    case CType::BranchVariant:
    case CType::CheckVariant:
        if (context.editor_props) {
            return makeDialog<VariantExpressionEventDialogAdapter>(create,
                context, summary, parent, parentContent, value, type, forceSkin,
                useCounter);
        }
        break;
    case CType::SetVariant:
        if (context.editor_props) {
            return makeDialog<SetVariantEventDialogAdapter>(create,
                context, summary, parent, parentContent, value, forceSkin,
                useCounter);
        }
        break;
    case CType::CustomXmlContent:
        if (context.editor_props && context.editor_commons) {
            return makeDialog<CustomXmlEventDialogAdapter>(create,
                context, summary, parent, parentContent, value, forceSkin,
                useCounter);
        }
        break;
    default:
        break;
    }
    return makeDialog<EventDialog>(create,
        context, summary, type, parent, parentContent, value, forceSkin,
        useCounter, create);
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::eventdialog
