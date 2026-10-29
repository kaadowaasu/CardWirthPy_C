#include "areaview.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"

#include <algorithm>

namespace cwxeditor::cwx::editor::gui::dwt::areaview {

int spnValue(const std::span<const int> keys, const int value) noexcept {
    if (keys.empty()) return value;
    return std::all_of(keys.begin(), keys.end(), [first=keys.front()](int item) {
        return item == first;
    }) ? keys.front() : value;
}

std::vector<detail::Placement> detail::placeSelection(std::vector<Placement> selected,
    const GeometryCommand command, const bool cards, const int canvasWidth,
    const int canvasHeight) {
    if (selected.empty()) return selected;
    // 共通実装の意図的な安全対策。広い符号付き型で座標を計算し、C++の符号
    // 付き未定義動作を避けるため、公開するDと同じ幅の座標へ飽和変換する。
    const auto narrow=[](std::int64_t value) {
        return static_cast<int>(std::clamp<std::int64_t>(value,
            std::numeric_limits<int>::min(),std::numeric_limits<int>::max()));
    };
    std::int64_t left=selected.front().x,top=selected.front().y;
    std::int64_t right=left+selected.front().width,bottom=top+selected.front().height;
    int width=selected.front().width,height=selected.front().height,scale=selected.front().scale;
    for (const auto& item:selected) {
        left=std::min<std::int64_t>(left,item.x);top=std::min<std::int64_t>(top,item.y);
        right=std::max<std::int64_t>(right,std::int64_t(item.x)+item.width);
        bottom=std::max<std::int64_t>(bottom,std::int64_t(item.y)+item.height);
        const bool big=command==GeometryCommand::Big;
        width=big?std::max(width,item.width):std::min(width,item.width);
        height=big?std::max(height,item.height):std::min(height,item.height);
        scale=big?std::max(scale,item.scale):std::min(scale,item.scale);
    }
    if (command==GeometryCommand::Even && selected.size()>1) {
        std::stable_sort(selected.begin(),selected.end(),[](const auto& a,const auto& b){return a.x<b.x;});
        const auto first=selected.front().x;
        const auto last=right-selected.back().width;
        for (std::size_t i=0;i<selected.size();++i)
            selected[i].x=narrow(std::int64_t(first)+std::llround(
                double(last-first)/double(selected.size()-1)*double(i)));
        return selected;
    }
    for (auto& item:selected) {
        switch (command) {
        case GeometryCommand::Top:item.y=narrow(top);break;
        case GeometryCommand::Bottom:item.y=narrow(bottom-item.height);break;
        case GeometryCommand::Left:item.x=narrow(left);break;
        case GeometryCommand::Right:item.x=narrow(right-item.width);break;
        case GeometryCommand::NearTop:item.y=narrow(std::int64_t(item.y)-top);break;
        case GeometryCommand::NearBottom:item.y=narrow(std::int64_t(item.y)+canvasHeight-bottom);break;
        case GeometryCommand::NearLeft:item.x=narrow(std::int64_t(item.x)-left);break;
        case GeometryCommand::NearRight:item.x=narrow(std::int64_t(item.x)+canvasWidth-right);break;
        case GeometryCommand::CenterH:case GeometryCommand::Center:
            item.x=narrow(std::int64_t(item.x)+(canvasWidth-(right-left))/2-left);
            if (command==GeometryCommand::CenterH)break;
            [[fallthrough]];
        case GeometryCommand::CenterV:
            item.y=narrow(std::int64_t(item.y)+(canvasHeight-(bottom-top))/2-top);break;
        case GeometryCommand::Big:case GeometryCommand::Small:
            if(cards)item.scale=scale;else{item.width=width;item.height=height;}break;
        case GeometryCommand::Expand:
            if(!cards){item.x=0;item.y=0;item.width=canvasWidth;item.height=canvasHeight;}break;
        case GeometryCommand::Even:break;
        }
    }
    return selected;
}

BattleOtherSettingsDialog::BattleOtherSettingsDialog(
    Props& prop, Battle& battle, std::shared_ptr<lib::Window> parent,
    lib::Services services)
    : absdialog::AbsDialog(prop, std::move(parent), true,
                           "その他設定",
                           prop.images().load(dprops::StringView{
                               "battle_cards.png"}),
                           false, nullptr,
                           false, true, {}, false, std::move(services)),
      prop_(&prop), battle_(&battle),
      result_transition_(battle.battleEndLoseBgTransition()),
      result_speed_(battle.battleEndLoseBgTransitionSpeed()),
      result_do_anime_(battle.battleEndLoseBgDoAnime()),
      result_ignore_effect_booster_(battle.battleEndLoseBgIgnoreEffectBooster()) {}

BattleOtherSettingsDialog::BattleOtherSettingsDialog(
    AreaViewContext& context, Props& prop, Battle& battle,
    std::shared_ptr<lib::Window> parent)
    : BattleOtherSettingsDialog(prop, battle, std::move(parent), context.services) {}

void BattleOtherSettingsDialog::setup(Container& content) {
    transition_control_ = std::make_shared<lib::ComboBox>();
    speed_control_ = std::make_shared<lib::NumberEdit>();
    anime_control_ = std::make_shared<lib::CheckBox>(
        prop_->requiredMsgs().doAnime.value);
    booster_control_ = std::make_shared<lib::CheckBox>(
        prop_->requiredMsgs().ignoreEffectBooster.value);

    for (const auto transition : types::ALL_TRANSITION) {
        transition_control_->items.push_back(
            {prop_->requiredMsgs().transitionName(transition),
             std::to_string(static_cast<std::int32_t>(transition))});
    }
    // DのBattleOtherSettingsDialogは、この共通設定を画面
    // 遷移の一覧へ直接適用する。ここでもドロップダウンの表示行数を設定可能にする。
    transition_control_->visible_item_count = static_cast<std::size_t>(
        std::max<std::int32_t>(1, prop_->var().comboVisibleItemCount.value));
    const auto found = std::find_if(
        transition_control_->items.begin(), transition_control_->items.end(),
        [this](const lib::ChoiceItem& item) {
            return item.value ==
                   std::to_string(static_cast<std::int32_t>(result_transition_));
        });
    if (found != transition_control_->items.end()) {
        transition_control_->selected_index = static_cast<std::size_t>(
            std::distance(transition_control_->items.begin(), found));
    }
    speed_control_->minimum = 0.0;
    speed_control_->maximum = 10.0;
    speed_control_->value = result_speed_;
    anime_control_->checked = result_do_anime_;
    booster_control_->checked = result_ignore_effect_booster_;

    auto content_layout = std::make_shared<lib::GridLayout>();
    content_layout->columns = 1U;
    content.set_layout(content_layout);
    auto group = std::make_shared<lib::GroupBox>();
    group->title = "戦闘終了時消去の背景セルの設定";
    auto group_layout = std::make_shared<lib::GridLayout>();
    group_layout->columns = 3U;
    group_layout->spacing = 6.0F;
    group_layout->padding = {8.0F, 8.0F, 8.0F, 8.0F};
    group->set_layout(group_layout);
    group->set_layout_item({.row = 0, .column = 0,
                            .horizontal = lib::Align::Stretch});

    // SWTの順次配置と異なり、共通グリッドは各コントロールのセ
    // ルを明示する必要がある。既定セルでは画面遷移の行と重なる。
    auto transition_label = std::make_shared<lib::Label>(prop_->requiredMsgs().transition.value);
    transition_label->set_layout_item({.row = 0, .column = 0,
        .vertical = lib::Align::Center});
    group->add(transition_label);
    transition_control_->set_layout_item({.row = 0, .column = 1,
                                          .column_span = 2,
                                          .horizontal = lib::Align::Stretch});
    group->add(transition_control_);
    auto speed_label = std::make_shared<lib::Label>(prop_->requiredMsgs().transitionSpeed.value);
    speed_label->set_layout_item({.row = 1, .column = 0,
        .vertical = lib::Align::Center});
    group->add(speed_label);
    speed_control_->set_layout_item({.row = 1, .column = 1,
        .vertical = lib::Align::Center,
        .minimum = {120.0F, 0.0F}, .preferred = {120.0F, 0.0F}});
    group->add(speed_control_);
    auto speed_hint = std::make_shared<lib::Label>(
        ::cwxeditor::cwx::utils::tryFormat(prop_->requiredMsgs().rangeHint.value, 0, 10));
    speed_hint->set_layout_item({.row = 1, .column = 2,
        .vertical = lib::Align::Center});
    group->add(speed_hint);
    anime_control_->set_layout_item({.row = 2, .column = 0,
                                     .column_span = 3});
    group->add(anime_control_);
    booster_control_->set_layout_item({.row = 3, .column = 0,
                                       .column_span = 3});
    group->add(booster_control_);
    content.add(group);

    const auto refresh_speed = [this](std::optional<std::size_t> index) {
        const auto transition = index && *index < types::ALL_TRANSITION.size()
                                    ? types::ALL_TRANSITION[*index]
                                    : result_transition_;
        speed_control_->set_enabled(transition != types::Transition::Default &&
                                    transition != types::Transition::None);
    };
    transition_control_->on_selection_changed = refresh_speed;
    refresh_speed(transition_control_->selected_index);
    mod(*transition_control_);
    mod(*speed_control_);
    mod(*anime_control_);
    mod(*booster_control_);
}

void BattleOtherSettingsDialog::storeResult() {
    if (!transition_control_ || !speed_control_ || !anime_control_ ||
        !booster_control_) {
        return;
    }
    if (transition_control_->selected_index &&
        *transition_control_->selected_index < types::ALL_TRANSITION.size()) {
        result_transition_ = types::ALL_TRANSITION[*transition_control_->selected_index];
    }
    result_speed_ = std::clamp(static_cast<int>(std::lround(speed_control_->value)),
                               0, 10);
    result_do_anime_ = anime_control_->checked;
    result_ignore_effect_booster_ = booster_control_->checked;
}

bool BattleOtherSettingsDialog::apply() {
    storeResult();
    return battle_ != nullptr;
}

bool BattleOtherSettingsDialog::close(const bool ok) {
    if (ok && !apply()) return false;
    return absdialog::AbsDialog::close(ok);
}

bool BattleOtherSettingsDialog::commit() {
    return apply();
}

BattleBGMSettingsDialog::BattleBGMSettingsDialog(
    AreaViewContext& context, Props& prop, Battle& battle, Summary* summary)
    : absdialog::AbsDialog(prop, {}, true, "戦闘BGM設定",
                           prop.images().load(dprops::StringView{"bgm.png"}),
                           false, nullptr, false, true, {}, false, context.services),
      prop_(&prop) {
    selector_ = std::make_unique<materialselect::MaterialSelect<materialselect::MtType::BGM>>(
        context.services, prop, summary, context.skin_owner, false);
    selector_->canContinue(!summary || !summary->legacy());
    selector_->path2(battle.music(), true, true, -1, false);
    selector_->volume(static_cast<int>(battle.volume()));
    selector_->loopCount(static_cast<int>(battle.loopCount()));
    selector_->fadeIn(static_cast<int>(battle.fadeIn()));
    selector_->continueBGM(battle.continueBGM());
}

void BattleBGMSettingsDialog::setup(Container& content) {
    auto layout = std::make_shared<lib::GridLayout>();
    layout->columns = 1U;
    layout->spacing = 8.0F;
    content.set_layout(layout);
    const auto options = selector_->createPlayingOptions(this->content(), false, true);
    options->set_layout_item({.row = 0U, .horizontal = lib::Align::Stretch});
    const auto fade = selector_->createFadeIn(this->content());
    auto row = std::make_shared<lib::LinearLayout>(lib::Orientation::Horizontal);
    row->spacing = 6.0F;
    row->cross_axis = lib::Align::Center;
    fade->set_layout(row);
    fade->set_layout_item({.row = 1U, .horizontal = lib::Align::Stretch});
    const auto controls = fade->children();
    fade->clear();
    fade->add(std::make_shared<lib::Label>(prop_->requiredMsgs().fadeIn.value));
    for (const auto& control : controls) {
        fade->add(control);
        if (const auto number = std::dynamic_pointer_cast<lib::NumberEdit>(control))
            mod(*number);
    }
    fade->add(std::make_shared<lib::Label>(prop_->requiredMsgs().fadeInHint.value));
    if (selector_->volumeControl()) mod(*selector_->volumeControl());
    if (selector_->loopControl()) mod(*selector_->loopControl());
    // 値はダイアログ内の選択用の編集中状態に属する。取消ではBattleを変え
    // ず、呼び出し側がOK時に既存の場面の取り消しトランザクションで確定する。
}

template class AbstractAreaView<Area, MenuCard, true, true>;
template class AbstractAreaView<Battle, EnemyCard, true, false>;

}  // namespace cwxeditor::cwx::editor::gui::dwt::areaview
