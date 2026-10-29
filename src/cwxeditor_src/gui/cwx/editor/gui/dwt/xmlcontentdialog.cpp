#include "xmlcontentdialog.hpp"
#include "cwxeditor_src/lib/window_host.hpp"
#include "chooser.hpp"
#include "eventdialog.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"
#include "cwxeditor_src/lib/ui_window_lifecycle.hpp"
// UIの実装は一つに統一し、
// 別経路の実装を設けない。

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <regex>
#include <sstream>
#include <system_error>
#include <utility>

namespace cwxeditor::cwx::editor::gui::dwt::xmlcontentdialog {
namespace {

[[nodiscard]] bool isBranchType(const StringView type) {
    static constexpr StringView values[] = {
        "BranchFlag", "BranchFlagValue", "BranchStep", "BranchStepValue",
        "BranchSelect", "BranchAbility", "BranchRandom", "BranchLevel",
        "BranchStatus", "BranchPartyNumber", "BranchRound", "BranchCast",
        "BranchItem", "BranchSkill", "BranchBeast", "BranchInfo",
        "BranchMoney", "BranchCoupon", "BranchCompleteStamp", "BranchGossip",
        "BranchKeyCode",
    };
    return std::find(std::begin(values), std::end(values), type) !=
           std::end(values);
}

[[nodiscard]] bool isTextExtension(const StringView extension) {
    return extension == ".xml" || extension == ".txt" || extension == ".md" ||
           extension == ".csv" || extension == ".json";
}

[[nodiscard]] String readFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("テンプレートの読み込みに失敗しました");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

[[nodiscard]] String pathText(const std::filesystem::path& path) {
    return ::cwxeditor::cwx::structs::pathToUtf8(path);
}

[[nodiscard]] String typeTemplate(const StringView type) {
    if (type == "ChangeBgImage") {
        return R"(<Change type="BgImage" transition="Default" transitionspeed="5">
<BgImages>
<BgImage smoothing="Default" mask="True" cellname="テンプレート">
<ImagePath>Bill.png</ImagePath>
<Flag>フラグ\汎用1</Flag>
<Location left="118" top="26"/>
<Size width="400" height="370"/>
<Layer>500</Layer>
</BgImage>
</BgImages>
</Change>)";
    }
    if (type == "MoveBgImage") {
        return R"(<Move type="BgImage" transition="Default" transitionspeed="5" cellname="テンプレート" positiontype="Absolute" x="50" y="50" sizetype="Percentage" width="80" height="80" doanime="False"/>)";
    }
    if (type == "LoseBgImage") {
        return R"(<Lose type="BgImage" transition="Default" transitionspeed="5" cellname="テンプレート" doanime="False"/>)";
    }
    if (type == "ReplaceBgImage") {
        return R"(<Replace type="BgImage" transition="Default" transitionspeed="5" cellname="テンプレート" doanime="False">
<BgImages>
<BgImage smoothing="Default" mask="True" cellname="テンプレート">
<ImagePath>Book.png</ImagePath>
<Flag/>
<Location left="69" top="39"/>
<Size width="460" height="280"/>
</BgImage>
</BgImages>
</Replace>)";
    }
    if (type == "MultipleMoveLose") return R"(<MultipleMoveLose />)";
    if (type == "Effect") {
        return R"(<Effect sound="効果（攻撃）.wav" volume="100" loopcount="1" initialeffect="True" initialsound="使用（光線）.wav" mental="Aggressive" physical="Int" targetm="Random" visual="Reverse" effecttype="Physic" resisttype="Unfail" level="1" successrate="5" ignite="True" refability="true">
<Motions>
<Motion type="Heal" element="All" damagetype="Normal" value="1"/>
<Motion type="Damage" element="All" damagetype="Normal" value="1"/>
<Motion type="Heal" element="All" damagetype="Normal" value="1"/>
</Motions>
<KeyCodes>攻撃</KeyCodes>
</Effect>)";
    }
    if (type == "InputDialog")
        return R"(<InputDialog width="200" height="200" Multiple="True" Variable="変数名"></InputDialog>)";
    if (type == "ChangeEnemy") {
        return R"(<ChangeEnemy cardGroup="敵3" regex="False">
<EnemyCard escape="False">
<Property>
<Id>4</Id>
<Flag/>
<Location left="355" top="81"/>
<Size scale="100%"/>
<CardGroup>敵3</CardGroup>
</Property>
<Events/>
</EnemyCard>
</ChangeEnemy>)";
    }
    if (type == "SetFlag") return R"(<Set type="Flag" flag="フラグ\汎用2" value="False"/>)";
    if (type == "BranchFlag") return R"(<Branch type="Flag" flag="フラグ\汎用2"/>)";
    if (type == "BranchFlagValue") return R"(<Branch type="FlagValue" from="フラグ\汎用1" to="フラグ\汎用2"/>)";
    if (type == "ReverseFlag") return R"(<Reverse type="Flag" flag="フラグ\汎用2"/>)";
    if (type == "BranchStep") return R"(<Branch type="Step" step="ステップ\汎用1" value="1"/>)";
    if (type == "BranchStepValue") return R"(<Branch type="StepValue" from="ステップ\汎用1" to="ステップ\汎用2"/>)";
    if (type == "MoveCard") return R"(<Move type="Card" cardgroup="再配置用" positiontype="Relative" x="50" scale="95" layer="330"/>)";
    if (type == "Wait") return R"(<Wait value="10"/>)";
    if (type == "GetMoney") return R"(<Get type="Money" value="600"/>)";
    if (type == "GetCast") return R"(<Get type="Cast" id="1" startaction="NextRound"/>)";
    if (type == "GetItem") return R"(<Get type="Item" id="1" targets="Field" number="1"/>)";
    if (type == "GetSkill") return R"(<Get type="Skill" id="1" targets="Field" number="1"/>)";
    if (type == "GetBeast") return R"(<Get type="Beast" id="1" targets="Field" number="1"/>)";
    if (type == "GetInfo") return R"(<Get type="Info" id="1"/>)";
    if (type == "GetCoupon") return R"(<Get type="Coupon" coupon="一時クーポン-配布用" targets="Random" value="0"/>)";
    if (type == "GetCompleteStamp") return R"(<Get type="CompleteStamp" scenario="一時終了シナリオ-配布用"/>)";
    if (type == "GetGossip") return R"(<Get type="Gossip" gossip="一時ゴシップ-配布用" spchars="True"/>)";
    if (type == "LoseCast") return R"(<Lose type="Cast" id="1"/>)";
    if (type == "LoseItem") return R"(<Lose type="Item" id="1" targets="Field" number="0"/>)";
    if (type == "LoseSkill") return R"(<Lose type="Skill" id="1" targets="Field" number="0"/>)";
    if (type == "LoseBeast") return R"(<Lose type="Beast" id="1" targets="Field" number="0"/>)";
    if (type == "LoseInfo") return R"(<Lose type="Info" id="1"/>)";
    if (type == "LoseMoney") return R"(<Lose type="Money" value="2000"/>)";
    if (type == "LoseCoupon") return R"(<Lose type="Coupon" coupon="一時クーポン-配布用" targets="Party"/>)";
    if (type == "LoseCompleteStamp") return R"(<Lose type="CompleteStamp" scenario="一時終了シナリオ-配布用"/>)";
    if (type == "LoseGossip") return R"(<Lose type="Gossip" gossip="一時ゴシップ-配布用"/>)";
    if (type == "BranchSelect") {
        return R"(<Branch type="Select" targetall="True" initialValue="-2" method="Valued">
<Coupons>
<Coupon value="1">＿子供</Coupon>
<Coupon value="1">＿♀</Coupon>
<Coupon value="1">＿標準型</Coupon>
</Coupons>
</Branch>)";
    }
    if (type == "BranchAbility") return R"(<Branch type="Ability" mental="Aggressive" physical="Dex" value="3" targetm="Selected"/>)";
    if (type == "BranchRandom") return R"(<Branch type="Random" value="40"/>)";
    if (type == "BranchLevel") return R"(<Branch type="Level" average="True" value="5"/>)";
    if (type == "BranchStatus") return R"(<Branch type="Status" status="Active" targetm="Random"/>)";
    if (type == "BranchPartyNumber") return R"(<Branch type="PartyNumber" value="4"/>)";
    if (type == "BranchRound") return R"(<Branch type="Round" comparison="=" round="3"/>)";
    if (type == "BranchCast") return R"(<Branch type="Cast" id="1"/>)";
    if (type == "BranchItem") return R"(<Branch type="Item" id="1" targets="Field" number="1"/>)";
    if (type == "BranchSkill") return R"(<Branch type="Skill" id="1" targets="Field" number="1"/>)";
    if (type == "BranchBeast") return R"(<Branch type="Beast" id="1" targets="Field" number="1"/>)";
    if (type == "BranchInfo") return R"(<Branch type="Info" id="1"/>)";
    if (type == "BranchMoney") return R"(<Branch type="Money" value="400"/>)";
    if (type == "BranchCoupon") return R"(<Branch type="Coupon" targets="Random" coupon="＿♂"/>)";
    if (type == "BranchMultiCoupon") {
        return R"(<Branch type="MultiCoupon" targets="Random">
</Branch>)";
    }
    if (type == "BranchCompleteStamp") return R"(<Branch type="CompleteStamp" scenario=""/>)";
    if (type == "BranchGossip") return R"(<Branch type="Gossip" gossip=""/>)";
    if (type == "BranchKeyCode") return R"(<Branch type="KeyCode" targetkc="Random" keyCode="" selectcard="True" effectCardType="All"/>)";
    if (type == "SetVariantStr")
        return R"(<Set type="VariantStr" variant="コモン\汎用1" insert="False"><![CDATA[こ"こ"に"文"字"を"い"れ"て"く"だ"さ"い"。]]></Set>)";
    if (type == "EndAction") return R"(<EndAction/>)";
    if (type == "SetActionEvent") return R"(<Action type="package" update="1" action="2" fps="30"></Action>)";
    if (type == "SetStep") return R"(<Set type="Step" step="ステップ\汎用1" value="4"/>)";
    if (type == "SetStepUp") return R"(<Set type="StepUp" step="ステップ\汎用1"/>)";
    if (type == "SetStepDown") return R"(<Set type="StepDown" step="ステップ\汎用1"/>)";
    if (type == "SubstituteStep") return R"(<Substitute type="Step" from="ステップ\汎用1" to="ステップ\汎用2"/>)";
    if (type == "SubstituteFlag") return R"(<Substitute type="Flag" from="フラグ\汎用1" to="フラグ\汎用2"/>)";
    if (type == "TalkDialog") {
        return R"(<Talk type="Dialog" initialValue="1" boundarycheck="True" targetm="Selected">
<Dialogs>
<Dialog>
<RequiredCoupons>＿上品\n</RequiredCoupons>
<Text>


「上品口調です。」
</Text>
</Dialog>
<Dialog>
<RequiredCoupons>＿子供\n</RequiredCoupons>
<Text>


「子供口調です。」
</Text>
</Dialog>
<Dialog>
<RequiredCoupons>＿♀\n</RequiredCoupons>
<Text>


「女性口調です。」
</Text>
</Dialog>
<Dialog>
<RequiredCoupons/>
<Text>


「その他口調です。」
</Text>
</Dialog>
</Dialogs>
<Coupons/>
</Talk>)";
    }
    if (type == "PlayBgm") return R"(<Play type="Bgm" path="DefBattle.ogg" channel="0" volume="100" loopcount="0" fadein="0"/>)";
    if (type == "PlaySound") return R"(<Play type="Sound" path="システム・クリック.wav" channel="0" volume="100" loopcount="1" fadein="0"/>)";
    if (type == "TalkMessage") {
        return R"(<Talk type="Message" boundarycheck="True" path="Material/宿の亭主.bmp">
<Text>


「その依頼に興味があるのかね？」
</Text>
</Talk>)";
    }
    if (type == "CheckStep") return R"(<Check type="Step" step="ステップ\汎用1" value="0" comparison="="/>)";
    if (type == "CheckFlag") return R"(<Check type="Flag" flag="フラグ\汎用1"/>)";
    if (type == "CheckVariant") {
        return R"(<Check type="Variant">
<Expression>@"コモン\汎用1" = 1</Expression>
</Check>)";
    }
    return {};
}

}  // namespace

XmlContentDialog::XmlContentDialog(
    commons::Commons& comm, dprops::Props& prop,
    std::shared_ptr<lib::Window> shell, SummaryPtr summary, SkinPtr forceSkin,
    UseCounterPtr useCounter, ContentPtr parent, ContentPtr event)
    : comm_(&comm), prop_(&prop), shell_(std::move(shell)),
      summary_(std::move(summary)), force_skin_(std::move(forceSkin)),
      use_counter_(std::move(useCounter)), parent_content_(std::move(parent)),
      event_(std::move(event)) {
    initializeState();
    variantChoices = [summary = summary_, counter = use_counter_] {
        std::vector<String> result;
        std::vector<flag::FlagDirPtr> roots;
        if (counter) {
            if (const auto owner = std::dynamic_pointer_cast<flag::LocalVariableOwner>(
                    counter->owner())) {
                if (const auto local = owner->flagDirRoot()) roots.push_back(local);
            }
        }
        if (summary) {
            if (const auto global = summary->flagDirRoot();
                global && std::find(roots.begin(), roots.end(), global) == roots.end())
                roots.push_back(global);
        }
        for (const auto& root : roots) {
            if (!root) continue;
            for (const auto& value : root->allVariants())
                if (value && !value->path().empty()) result.push_back(value->path());
        }
        std::ranges::sort(result);
        result.erase(std::ranges::unique(result).begin(), result.end());
        return result;
    };
    const auto dialogs = prop.images().services().dialogs;
    confirmInitialize = [dialogs](StringView title, StringView message) {
        if (!dialogs) return false;
        lib::MessageDialogRequest request;
        request.title = String(title);
        request.message = String(message);
        request.severity = lib::MessageSeverity::Question;
        request.buttons = lib::MessageButtons::OKCancel;
        const auto result = dialogs->message(request);
        return result == lib::MessageResult::OK;
    };
    confirmOverwrite = [dialogs](StringView title, StringView message) {
        if (!dialogs) return false;
        lib::MessageDialogRequest request;
        request.title = String(title);
        request.message = String(message);
        request.severity = lib::MessageSeverity::Warning;
        request.buttons = lib::MessageButtons::OKCancel;
        return dialogs->message(request) == lib::MessageResult::OK;
    };
    reportMessage = [dialogs](const lib::MessageSeverity severity, StringView title,
                              StringView message) {
        if (!dialogs) return;
        lib::MessageDialogRequest request;
        request.title = String(title);
        request.message = String(message);
        request.severity = severity;
        request.buttons = lib::MessageButtons::OK;
        // Web/ALLなど非同期のメッセージ表示に対応するバックエンド
        // では、 ボタンのコールバックから入れ子の同期ループに入らない。
        if (dialogs->supports_message_async() &&
            dialogs->message_async(request, [](lib::MessageResult) {}))
            return;
        (void)dialogs->message(request);
    };
    askAsync = [dialogs](const lib::MessageSeverity severity, StringView title,
                         StringView message, std::function<void(bool)> done) {
        if (!dialogs) { if (done) done(false); return; }
        lib::MessageDialogRequest request;
        request.title = String(title);
        request.message = String(message);
        request.severity = severity;
        request.buttons = lib::MessageButtons::OKCancel;
        if (dialogs->supports_message_async()) {
            auto shared = std::make_shared<std::function<void(bool)>>(std::move(done));
            if (dialogs->message_async(request, [shared](const lib::MessageResult result) {
                    if (*shared) (*shared)(result == lib::MessageResult::OK);
                }))
                return;
            done = std::move(*shared);
        }
        const bool accepted = dialogs->message(request) == lib::MessageResult::OK;
        if (done) done(accepted);
    };
}

XmlContentDialog::XmlContentDialog(
    commons::Commons& comm, dprops::Props& prop, SummaryPtr summary,
    ContentPtr parent, ContentPtr event, SkinPtr forceSkin,
    UseCounterPtr useCounter)
    : XmlContentDialog(comm, prop, {}, std::move(summary), std::move(forceSkin),
                       std::move(useCounter), std::move(parent),
                       std::move(event)) {}

XmlContentDialog::~XmlContentDialog() {
    reference_lifetime_.reset();
    if (event_host_) {
        event_host_->closeEvent.clear();
        event_host_->forceCancel();
        event_host_.reset();
    }
    if (reference_dialog_ && prop_ && prop_->images().services().gui) {
        prop_->images().services().gui->end_modal(reference_dialog_);
        if (reference_dialog_->visible())
            prop_->images().services().gui->close(reference_dialog_);
    }
    if (variant_dialog_ && prop_ && prop_->images().services().gui) {
        prop_->images().services().gui->end_modal(variant_dialog_);
        if (variant_dialog_->visible())
            prop_->images().services().gui->close(variant_dialog_);
    }
    if (reference_list_) reference_list_->on_selection_changed = {};
}

void XmlContentDialog::initializeState() {
    buildTypeEntries();
    assignment_ = event_ ? event_->xmlAssignment() : 1;
    if (assignment_ != 0) assignment_ = 1;
    draft_text_ = event_ ? event_->xmlStr() : String{};
    resetTextHistory();
    if (event_) {
        selected_reference_.clear();
    }
}

void XmlContentDialog::buildTypeEntries() {
    type_entries_.clear();
    xml_type_labels_.clear();
    if (!prop_) return;
    const auto& m = prop_->requiredMsgs();
    const auto add = [this](String label, String type) {
        type_entries_.push_back({std::move(label), std::move(type)});
    };

    add(m.xmlTypeChangeBgImage.value, "ChangeBgImage");
    add(m.xmlTypeMoveBgImage.value, "MoveBgImage");
    add(m.xmlTypeLoseBgImage.value, "LoseBgImage");
    add(m.xmlTypeReplaceBgImage.value, "ReplaceBgImage");
    add(m.xmlTypeMoveCard.value, "MoveCard");
    add(m.xmlTypeMultipleMoveLose.value, "MultipleMoveLose");
    add(m.xmlTypeEffect.value, "Effect");
    add(m.xmlTypeTalkMessage.value, "TalkMessage");
    add(m.xmlTypeTalkDialog.value, "TalkDialog");
    add(m.xmlTypePlayBgm.value, "PlayBgm");
    add(m.xmlTypePlaySound.value, "PlaySound");
    add(m.xmlTypeWait.value, "Wait");
    add(m.xmlTypeSetFlag.value, "SetFlag");
    add(m.xmlTypeReverseFlag.value, "ReverseFlag");
    add(m.xmlTypeSubstituteFlag.value, "SubstituteFlag");
    add(m.xmlTypeBranchFlag.value, "BranchFlag");
    add(m.xmlTypeBranchFlagValue.value, "BranchFlagValue");
    add(m.xmlTypeCheckFlag.value, "CheckFlag");
    add(m.xmlTypeSetStep.value, "SetStep");
    add(m.xmlTypeSetStepUp.value, "SetStepUp");
    add(m.xmlTypeSetStepDown.value, "SetStepDown");
    add(m.xmlTypeSubstituteStep.value, "SubstituteStep");
    add(m.xmlTypeBranchStep.value, "BranchStep");
    add(m.xmlTypeBranchStepValue.value, "BranchStepValue");
    add(m.xmlTypeCheckStep.value, "CheckStep");
    add(m.xmlTypeSetVariantStr.value, "SetVariantStr");
    add(m.xmlTypeCheckVariant.value, "CheckVariant");
    add(m.xmlTypeBranchSelect.value, "BranchSelect");
    add(m.xmlTypeBranchAbility.value, "BranchAbility");
    add(m.xmlTypeBranchRandom.value, "BranchRandom");
    add(m.xmlTypeBranchLevel.value, "BranchLevel");
    add(m.xmlTypeBranchStatus.value, "BranchStatus");
    add(m.xmlTypeBranchPartyNumber.value, "BranchPartyNumber");
    add(m.xmlTypeBranchRound.value, "BranchRound");
    add(m.xmlTypeBranchCast.value, "BranchCast");
    add(m.xmlTypeBranchItem.value, "BranchItem");
    add(m.xmlTypeBranchSkill.value, "BranchSkill");
    add(m.xmlTypeBranchBeast.value, "BranchBeast");
    add(m.xmlTypeBranchInfo.value, "BranchInfo");
    add(m.xmlTypeBranchMoney.value, "BranchMoney");
    add(m.xmlTypeBranchCoupon.value, "BranchCoupon");
    add(m.xmlTypeBranchMultiCoupon.value, "BranchMultiCoupon");
    add(m.xmlTypeBranchCompleteStamp.value, "BranchCompleteStamp");
    add(m.xmlTypeBranchGossip.value, "BranchGossip");
    add(m.xmlTypeBranchKeyCode.value, "BranchKeyCode");
    add(m.xmlTypeGetCast.value, "GetCast");
    add(m.xmlTypeGetItem.value, "GetItem");
    add(m.xmlTypeGetSkill.value, "GetSkill");
    add(m.xmlTypeGetBeast.value, "GetBeast");
    add(m.xmlTypeGetInfo.value, "GetInfo");
    add(m.xmlTypeGetMoney.value, "GetMoney");
    add(m.xmlTypeGetCoupon.value, "GetCoupon");
    add(m.xmlTypeGetCompleteStamp.value, "GetCompleteStamp");
    add(m.xmlTypeGetGossip.value, "GetGossip");
    add(m.xmlTypeLoseCast.value, "LoseCast");
    add(m.xmlTypeLoseItem.value, "LoseItem");
    add(m.xmlTypeLoseSkill.value, "LoseSkill");
    add(m.xmlTypeLoseBeast.value, "LoseBeast");
    add(m.xmlTypeLoseInfo.value, "LoseInfo");
    add(m.xmlTypeLoseMoney.value, "LoseMoney");
    add(m.xmlTypeLoseCoupon.value, "LoseCoupon");
    add(m.xmlTypeLoseCompleteStamp.value, "LoseCompleteStamp");
    add(m.xmlTypeLoseGossip.value, "LoseGossip");
    add(m.xmlTypeSetActionEvent.value, "SetActionEvent");
    add(m.xmlTypeEndAction.value, "EndAction");
    add(m.xmlTypeInputDialog.value, "InputDialog");
    add(m.xmlTypeChangeEnemy.value, "ChangeEnemy");

    for (const auto& entry : type_entries_) xml_type_labels_.push_back(entry.label);
}

PanelPtr XmlContentDialog::createControl(const Parent& parent,
                                         const bool includeFooter) {
    if (!panel_) {
        panel_ = std::make_shared<lib::Panel>();
        if (parent) parent->add(panel_);
        (void)setup(panel_, includeFooter);
    }
    return panel_;
}

PanelPtr XmlContentDialog::setup(const PanelPtr& area,
                                 const bool includeFooter) {
    if (!area) return {};
    if (panel_ && panel_ != area && !panel_->children().empty()) return panel_;
    panel_ = area;
    auto root_layout = std::make_shared<lib::GridLayout>();
    root_layout->columns = 5U;
    root_layout->padding = {6.0F, 4.0F, 6.0F, 4.0F};
    root_layout->spacing = 5.0F;
    panel_->set_layout(root_layout);
    if (text_area_) return panel_;

    text_group_ = std::make_shared<lib::GroupBox>();
    text_group_->title = prop_ ? prop_->requiredMsgs().xmlContentStr.value : "XML 文字列";
    text_group_->set_id(1);
    text_group_->set_layout(std::make_shared<lib::FillLayout>());
    text_group_->set_layout_item({.row=0U,.column=0U,.column_span=5U,
                                  .preferred={558.0F,205.0F},.grow=1.0F});
    panel_->add(text_group_);

    text_area_ = std::make_shared<lib::TextEdit>();
    text_area_->set_id(2);
    text_area_->multiline = true;
    text_area_->wrap = false;
    text_area_->vertical_scroll = true;
    text_area_->horizontal_scroll = true;
    text_group_->add(text_area_);

    xml_type_combo_ = std::make_shared<lib::ComboBox>();
    xml_type_combo_->set_id(3);
    xml_type_combo_->editable = false;
    xml_type_combo_->items.reserve(type_entries_.size());
    for (const auto& entry : type_entries_)
        xml_type_combo_->items.push_back({entry.label, entry.type});
    xml_type_combo_->set_layout_item({.row=1U,.column=1U,.column_span=2U,
                                      .preferred={340.0F,23.0F},.grow=1.0F});
    panel_->add(xml_type_combo_);

    assign_off_ = std::make_shared<lib::RadioButton>();
    assign_on_ = std::make_shared<lib::RadioButton>();
    assign_off_->set_id(4);
    assign_on_->set_id(5);
    assign_off_->text = prop_ ? prop_->requiredMsgs().xmlAssignOff.value : "Off";
    assign_on_->text = prop_ ? prop_->requiredMsgs().xmlAssignOn.value : "On";
    assign_off_->group = "xml-assignment";
    assign_on_->group = "xml-assignment";
    assign_off_->set_layout_item({.row=2U,.column=1U,.preferred={70.0F,0.0F}});
    assign_on_->set_layout_item({.row=2U,.column=2U,.preferred={70.0F,0.0F}});
    panel_->add(assign_off_);
    panel_->add(assign_on_);

    initialize_button_ = std::make_shared<lib::Button>();
    reference_button_ = std::make_shared<lib::Button>();
    initialize_button_->text = prop_ ? prop_->requiredMsgs().initializeLabel.value : "初期化";
    reference_button_->text = prop_ ? prop_->requiredMsgs().openReferenceLabel.value : "参考を開く";
    initialize_button_->set_layout_item({.row=1U,.column=3U});
    reference_button_->set_layout_item({.row=1U,.column=4U});
    panel_->add(initialize_button_);
    panel_->add(reference_button_);

    insert_reference_button_ = std::make_shared<lib::Button>();
    insert_name_button_ = std::make_shared<lib::Button>();
    check_button_ = std::make_shared<lib::Button>();
    insert_reference_button_->text = prop_ ? prop_->requiredMsgs().insertVariantReference.value
                                            : "コモン参照の挿入";
    insert_name_button_->text = prop_ ? prop_->requiredMsgs().insertVariantPath.value
                                      : "コモン名の挿入";
    check_button_->text = "XML構文をチェック";
    insert_reference_button_->set_layout_item({.row=3U,.column=1U});
    insert_name_button_->set_layout_item({.row=3U,.column=2U});
    check_button_->set_layout_item({.row=3U,.column=3U,.column_span=2U});
    const std::array captions{prop_ ? prop_->requiredMsgs().xmlTypeLabel.value : String{"XML種別"},
        prop_ ? prop_->requiredMsgs().xmlAssignLabel.value : String{"代入"},
        prop_ ? prop_->requiredMsgs().variableLabel.value : String{"変数"}};
    for (std::size_t row = 0; row < captions.size(); ++row) {
        auto label = std::make_shared<lib::Label>();
        label->text = captions[row];
        label->set_layout_item({.row=row + 1U, .column=0U, .vertical=lib::Align::Center});
        panel_->add(label);
    }
    panel_->add(insert_reference_button_);
    panel_->add(insert_name_button_);
    panel_->add(check_button_);

    reference_list_ = std::make_shared<lib::ListView>();
    reference_viewer_ = std::make_shared<lib::TextEdit>();
    reference_viewer_->multiline = true;
    reference_viewer_->wrap = true;
    reference_viewer_->horizontal_scroll = true;
    reference_viewer_->read_only = true;
    reference_list_->set_id(6);
    reference_viewer_->set_id(7);
    reference_list_->set_visible(false);
    reference_viewer_->set_visible(false);

    if (includeFooter) {
        auto footer = std::make_shared<lib::Panel>();
        footer->set_layout(
            std::make_shared<lib::LinearLayout>(lib::Orientation::Horizontal));
        ok_button_ = std::make_shared<lib::Button>("OK");
        ok_button_->default_button = true;
        ok_button_->on_click = [this] {
            if (!apply()) return;
            if (dialog_) dialog_->result = lib::DialogResult::OK;
            close();
        };
        cancel_button_ = std::make_shared<lib::Button>("キャンセル");
        cancel_button_->cancel_button = true;
        cancel_button_->on_click = [this] {
            if (dialog_) dialog_->result = lib::DialogResult::Cancel;
            close();
        };
        footer->add(ok_button_);
        footer->add(cancel_button_);
        footer->set_layout_item({.row=4U, .column=0U, .column_span=5U});
        panel_->add(footer);
    }

    bindControls();
    setTextInternal(draft_text_, false);
    if (event_) {
        selectXmlType(typeToLabel(event_->xmlType()));
    } else if (!xml_type_labels_.empty()) {
        selectXmlType(xml_type_labels_.front());
    }
    setAssignment(assignment_);
    dirty_ = false;
    resetTextHistory();
    return panel_;
}

void XmlContentDialog::bindControls() {
    text_area_->on_changed = [this](StringView) {
        if (!changing_text_) markChanged();
    };
    xml_type_combo_->on_selection_changed = [this](std::optional<std::size_t> index) {
        if (index && *index < xml_type_combo_->items.size()) {
            selected_type_label_ = xml_type_combo_->items[*index].text;
        }
        markChanged();
    };
    assign_off_->on_changed = [this](bool checked) {
        if (checked) setAssignment(0);
    };
    assign_on_->on_changed = [this](bool checked) {
        if (checked) setAssignment(1);
    };
    initialize_button_->on_click = [this] { initializeTemplateInteractive(); };
    reference_button_->on_click = [this] { (void)openReference(); };
    insert_reference_button_->on_click = [this] {
        showVariantChooser(true);
    };
    insert_name_button_->on_click = [this] {
        showVariantChooser(false);
    };
    check_button_->on_click = [this] {
        const auto result = checkXml(assignment_ != 0);
        // D版と同様、空入力は警告とし、解析結果は情報またはエラーとして表示する。
        const auto severity = result.checkedText.empty() ? lib::MessageSeverity::Warning
            : result.valid ? lib::MessageSeverity::Information
                           : lib::MessageSeverity::Error;
        if (reportMessage) reportMessage(severity, "XML構文チェック", result.message);
    };
    reference_list_->on_selection_changed = [this](const std::vector<std::size_t>& values) {
        if (values.empty()) return;
        (void)selectReferenceFile(values.front());
    };
    text_menu_ = std::make_shared<lib::Menu>(true);
    text_menu_->items = {{"元に戻す", "undo"}, {"やり直す", "redo"},
                         {"", "", {}, {}, true, false, false, true},
                         {"切り取り", "cut"}, {"コピー", "copy"},
                         {"貼り付け", "paste"},
                         {"", "", {}, {}, true, false, false, true},
                         {"すべて選択", "select-all"}};
    text_menu_->on_command = [this](StringView command) {
        if (command == "undo") (void)undoText();
        else if (command == "redo") (void)redoText();
        else if (command == "cut") (void)cutSelection();
        else if (command == "copy") copySelection();
        else if (command == "paste") pasteClipboard();
        else if (command == "select-all") selectAllText();
        if (text_menu_ && text_menu_->on_open) text_menu_->on_open();
    };
    text_menu_->on_open = [this] {
        const bool has_editor = static_cast<bool>(text_area_);
        const bool editable = has_editor && !text_area_->read_only;
        const bool has_selection = editable && text_area_->selection_length != 0U;
        const bool has_clipboard = prop_ && prop_->images().services().clipboard;
        const bool can_paste = editable && has_clipboard &&
            !prop_->images().services().clipboard->read_text().empty();
        for (auto& item : text_menu_->items) {
            if (item.command == "undo") item.enabled = text_history_index_ != 0U;
            else if (item.command == "redo")
                item.enabled = text_history_index_ + 1U < text_history_.size();
            else if (item.command == "cut") item.enabled = has_selection && has_clipboard;
            else if (item.command == "copy") item.enabled = has_selection && has_clipboard;
            else if (item.command == "paste") item.enabled = can_paste;
            else if (item.command == "select-all")
                item.enabled = has_editor && !text_area_->text.empty();
        }
    };
    text_area_->on_pointer = [this](lib::PointerEvent& event) {
        if (event.button != lib::PointerButton::Secondary ||
            event.action != lib::PointerAction::Up || !prop_ ||
            !prop_->images().services().gui)
            return;
        prop_->images().services().gui->show_context_menu(
            text_menu_, text_area_, event.position);
        event.handled = true;
    };
}

void XmlContentDialog::open() {
    if (!prop_ || !prop_->images().services().gui) return;
    if (!event_host_) {
        eventdialog::Context context;
        context.prop = const_cast<::cwxeditor::cwx::props::CProps*>(&prop_->parent());
        context.editor_props = prop_;
        context.editor_commons = comm_;
        context.dialog_owner = shell_;
        context.services = prop_->images().services();
        // D版と同様、XML設定はモードレスのイベント設定として扱う。 別のモー
        // ダル画面や入れ子のイベントループを作らず、共通アダプターを再利用する。
        event_host_ = eventdialog::createXmlContentHost(context, *this);
        const std::weak_ptr<const int> life = reference_lifetime_;
        event_host_->closeEvent.push_back([this, life] {
            if (life.expired()) return;
            open_ = false;
            close(); // D版と同様、ホストとともに子の参照・変数ウィンドウを閉じる。
            if (closed) closed();
        });
    }
    open_ = true;
    event_host_->active();
}

void XmlContentDialog::active() {
    if (!open_) {
        open();
        return;
    }
    if (prop_ && prop_->images().services().gui && text_area_)
        prop_->images().services().gui->focus(text_area_);
}

void XmlContentDialog::showVariantChooser(const bool reference) {
    if (!prop_ || !prop_->images().services().gui) return;
    auto gui = prop_->images().services().gui;
    // D版と同様、コモン参照・コモン名の挿入を押すたびに、 XMLダ
    // イアログを中心とした新しい400x400のダイアログを開く。
    if (variant_dialog_) {
        gui->end_modal(variant_dialog_);
        if (variant_dialog_->visible()) gui->close(variant_dialog_);
        variant_dialog_.reset();
        variant_chooser_.reset();
    }
    auto dialog = gui->create_dialog();
    if (!dialog) return;
    variant_dialog_ = dialog;
    dialog->title = reference ? prop_->requiredMsgs().insertVariantReference.value
                              : prop_->requiredMsgs().insertVariantPath.value;
    const auto owner = dialog_ ? std::static_pointer_cast<lib::Window>(dialog_)
                               : shell_;
    lib::configure_owned_window(dialog, owner, lib::OwnedWindowKind::Modal);
    dialog->center_on_owner = true;
    dialog->set_bounds({0.0F, 0.0F, 400.0F, 400.0F});
    auto layout = std::make_shared<lib::GridLayout>();
    layout->columns = 1U;
    dialog->set_layout(layout);
    chooser::Commons chooser_commons;
    chooser_commons.services = prop_->images().services();
    chooser::ChooserProperties properties;
    properties.editor_commons = comm_;
    properties.selectVariableWithTree = prop_->var().selectVariableWithTree.value;
    properties.expandChooserItems = prop_->var().expandChooserItems.value;
    properties.logicalSort = prop_->var().logicalSort.value;
    properties.flagsWidth = prop_->var().flagsWidth.value;
    properties.flagsHeight = prop_->var().flagsHeight.value;
    properties.images.variant = prop_->images().variant();
    properties.messages.allExpanded = prop_->requiredMsgs().allExpanded.value;
    properties.openCWXPath = [comm = comm_](const StringView path, const bool activate) {
        if (comm) (void)comm->openCWXPath(String{path}, activate);
    };
    auto variable_chooser = std::make_shared<chooser::FlagChooser<::cwxeditor::cwx::flag::Variant, false>>(
        chooser_commons, summary_, use_counter_, dialog.get(), true, properties);
    variant_chooser_ = variable_chooser;
    variable_chooser->widget()->set_layout_item({.row=0U, .column=0U,
        .horizontal=lib::Align::Stretch, .vertical=lib::Align::Stretch, .grow=1.0F});
    auto footer = std::make_shared<lib::Panel>();
    footer->set_layout(
        std::make_shared<lib::LinearLayout>(lib::Orientation::Horizontal));
    footer->set_layout_item({.row = 1U, .column = 0U,
                             .horizontal = lib::Align::Center,
                             .minimum = {240.0F, 28.0F}});
    auto ok = std::make_shared<lib::Button>(prop_->requiredMsgs().dlgTextOK.value);
    auto cancel = std::make_shared<lib::Button>(prop_->requiredMsgs().dlgTextCancel.value);
    ok->default_button = true;
    cancel->cancel_button = true;
    const std::weak_ptr<lib::Dialog> weak_dialog = dialog;
    const auto finish = [this, weak_dialog, gui](const lib::DialogResult result) {
        const auto value = weak_dialog.lock();
        if (!value) return;
        value->result = result;
        gui->end_modal(value);
        if (value->visible()) gui->close(value);
        if (variant_dialog_ == value) {
            variant_dialog_.reset();
            variant_chooser_.reset();
        }
    };
    // D版と同様、OKでは変数が選択されている場合だけ挿入し、選択の有無によらず閉じる。
    ok->on_click = [this, finish, chooser = std::weak_ptr(variable_chooser), reference] {
        String selected;
        if (const auto value = chooser.lock()) selected = value->selected();
        if (!selected.empty()) {
            if (reference) insertVariantReference(selected);
            else insertVariantName(selected);
        }
        finish(lib::DialogResult::OK);
    };
    cancel->on_click = [finish] { finish(lib::DialogResult::Cancel); };
    dialog->on_close_requested = [finish] { finish(lib::DialogResult::Cancel); };
    footer->add(ok);
    footer->add(cancel);
    dialog->add(footer);
    // 対応する環境では非ブロッキングのモーダル表示を使う。 入れ子のループは Web ALL を停止させるため。
    if (!gui->begin_modal(dialog, owner)) (void)gui->show_modal(dialog, owner);
}

String XmlContentDialog::text() const {
    return text_area_ ? text_area_->text : draft_text_;
}

void XmlContentDialog::setText(String value) { setTextInternal(std::move(value), true); }

void XmlContentDialog::setTextInternal(String value, const bool notify) {
    if (!text_area_) {
        draft_text_ = std::move(value);
        if (notify) markChanged();
        return;
    }
    changing_text_ = true;
    text_area_->text = std::move(value);
    draft_text_ = text_area_->text;
    text_area_->selection_start = text_area_->text.size();
    text_area_->selection_length = 0;
    changing_text_ = false;
    if (notify) markChanged();
}

void XmlContentDialog::markChanged() {
    draft_text_ = text();
    recordTextHistory();
    dirty_ = true;
    // 編集は独立した未確定値として扱う。モデルの確定は EventDialog が一
    // 度だけ行い、 適用時の EventView の取り消し履歴・通知も一度にする。
    if (modified) modified();
}

String XmlContentDialog::selectedXmlType(const std::shared_ptr<lib::Window>&) const {
    if (!xml_type_combo_) return selected_type_label_;
    if (xml_type_combo_->selected_index &&
        *xml_type_combo_->selected_index < xml_type_combo_->items.size())
        return xml_type_combo_->items[*xml_type_combo_->selected_index].text;
    return {};
}

void XmlContentDialog::selectXmlType(String label) {
    selected_type_label_ = std::move(label);
    if (!xml_type_combo_) return;
    const auto& selected = selected_type_label_;
    std::optional<std::size_t> selected_index;
    for (std::size_t i = 0; i < xml_type_combo_->items.size(); ++i) {
        if (xml_type_combo_->items[i].text == selected) {
            selected_index = i;
            break;
        }
    }
    auto items = xml_type_combo_->items;
    (void)lib::replace_combo_items(xml_type_combo_, std::move(items),
                                   selected_index, selected);
}

int XmlContentDialog::textLength(const lib::TextEdit& text_edit) const {
    return static_cast<int>(std::min<std::size_t>(text_edit.text.size(), std::numeric_limits<int>::max()));
}

void XmlContentDialog::replaceTextByEdit(String value) {
    if (text() == value) return;
    setTextInternal(std::move(value), true);
}

void XmlContentDialog::resetTextHistory() {
    text_history_.assign(1U, text());
    text_history_index_ = 0U;
}

void XmlContentDialog::recordTextHistory() {
    const auto value = text();
    if (text_history_.empty()) {
        text_history_.push_back(value);
        text_history_index_ = 0U;
        return;
    }
    if (text_history_[text_history_index_] == value) return;
    text_history_.erase(text_history_.begin() +
                        static_cast<std::ptrdiff_t>(text_history_index_ + 1U),
                        text_history_.end());
    text_history_.push_back(value);
    text_history_index_ = text_history_.size() - 1U;
}

bool XmlContentDialog::undoText() {
    if (text_history_.empty() || text_history_index_ == 0U) return false;
    --text_history_index_;
    setTextInternal(text_history_[text_history_index_], false);
    dirty_ = true;
    if (modified) modified();
    return true;
}

bool XmlContentDialog::redoText() {
    if (text_history_.empty() || text_history_index_ + 1U >= text_history_.size()) return false;
    ++text_history_index_;
    setTextInternal(text_history_[text_history_index_], false);
    dirty_ = true;
    if (modified) modified();
    return true;
}

bool XmlContentDialog::replaceSelectionByCommand(const StringView value) {
    if (!text_area_ || text_area_->read_only) return false;
    // 共通の TextEdit の位置は、すべてのバックエンドでUTF-8の
    // バイト位置。 共通の編集操作が文字境界を正規化し、IME・履歴を扱う。
    return lib::replace_text_selection(*text_area_, value);
}

void XmlContentDialog::insertTextByEdit(String value) {
    if (value.empty()) return;
    if (!text_area_) {
        setText(text() + value);
        return;
    }
    (void)replaceSelectionByCommand(value);
}

void XmlContentDialog::insertVariantReference(const StringView name) {
    if (!name.empty()) insertTextByEdit("@" + String(name) + "@");
}

void XmlContentDialog::insertVariantName(const StringView name) {
    if (!name.empty()) insertTextByEdit("\"" + String(name) + "\"");
}

void XmlContentDialog::copySelection() const {
    if (!prop_ || !prop_->images().services().clipboard || !text_area_) return;
    lib::TextEditInteractionState state;
    lib::synchronize_text_edit_state(*text_area_, state);
    lib::commit_text_edit_state(*text_area_, state);
    const auto begin = std::min(text_area_->selection_start, text_area_->text.size());
    const auto length = std::min(text_area_->selection_length, text_area_->text.size() - begin);
    lib::ClipboardData data;
    data.text = text_area_->text.substr(begin, length);
    prop_->images().services().clipboard->write(data);
}

bool XmlContentDialog::cutSelection() {
    if (!text_area_ || text_area_->read_only || text_area_->selection_length == 0U)
        return false;
    copySelection();
    return replaceSelectionByCommand({});
}

void XmlContentDialog::pasteClipboard() {
    if (!prop_ || !prop_->images().services().clipboard) return;
    const auto data = prop_->images().services().clipboard->read();
    if (!data.text.empty()) (void)replaceSelectionByCommand(data.text);
}

void XmlContentDialog::selectAllText() {
    if (!text_area_) return;
    text_area_->selection_start = 0U;
    text_area_->selection_length = text_area_->text.size();
}

const XmlContentDialog::TypeEntry* XmlContentDialog::findType(
    const StringView label) const {
    const auto found = std::find_if(type_entries_.begin(), type_entries_.end(),
                                    [label](const TypeEntry& entry) {
                                        return entry.label == label;
                                    });
    return found == type_entries_.end() ? nullptr : &*found;
}

XmlContentDialog::TypeEntry* XmlContentDialog::findType(const StringView label) {
    const auto found = std::find_if(type_entries_.begin(), type_entries_.end(),
                                    [label](const TypeEntry& entry) {
                                        return entry.label == label;
                                    });
    return found == type_entries_.end() ? nullptr : &*found;
}

String XmlContentDialog::labelToType(const StringView label) const {
    // D版の labelToType と同様、適用とは別の、独自の順序付き比較を使う。
    if (prop_ && !lib::editor_window_host_options().all_mode) {
        const auto& m = prop_->requiredMsgs();
        if (label == m.xmlTypeChangeBgImage.value) return "ChangeBgImage";
        if (label == m.xmlTypeMoveBgImage.value) return "MoveBgImage";
        if (label == m.xmlTypeLoseBgImage.value) return "LoseBgImage";
        if (label == m.xmlTypeReplaceBgImage.value) return "ReplaceBgImage";
        if (label == m.xmlTypeMultipleMoveLose.value) return "MultipleMoveLose";
        if (label == m.xmlTypeEffect.value) return "Effect";
        if (label == m.xmlTypeInputDialog.value) return "InputDialog";
        if (label == m.xmlTypeChangeEnemy.value) return "ChangeEnemy";
        if (label == m.xmlTypeSetFlag.value) return "SetFlag";
        if (label == m.xmlTypeTalkMessage.value) return "TalkMessage";
        if (label == m.xmlTypeTalkDialog.value) return "TalkDialog";
        if (label == m.xmlTypePlayBgm.value) return "PlayBgm";
        if (label == m.xmlTypePlaySound.value) return "PlaySound";
        if (label == m.xmlTypeCheckStep.value) return "CheckStep";
        if (label == m.xmlTypeCheckFlag.value) return "CheckFlag";
        if (label == m.xmlTypeCheckVariant.value) return "CheckVariant";
        if (label == m.xmlTypeBranchFlag.value) return "BranchFlag";
        if (label == m.xmlTypeBranchFlagValue.value) return "BranchFlagValue";
        if (label == m.xmlTypeReverseFlag.value) return "ReverseFlag";
        if (label == m.xmlTypeBranchStep.value) return "BranchStep";
        if (label == m.xmlTypeBranchStepValue.value) return "BranchStepValue";
        if (label == m.xmlTypeMoveCard.value) return "MoveCard";
        if (label == m.xmlTypeWait.value) return "Wait";
        if (label == m.xmlTypeGetMoney.value) return "GetMoney";
        if (label == m.xmlTypeSetVariantStr.value) return "SetVariantStr";
        if (label == m.xmlTypeSetActionEvent.value) return "SetActionEvent";
        if (label == m.xmlTypeEndAction.value) return "EndAction";
        if (label == m.xmlTypeBranchSelect.value) return "BranchSelect";
        if (label == m.xmlTypeBranchAbility.value) return "BranchAbility";
        if (label == m.xmlTypeBranchRandom.value) return "BranchRandom";
        if (label == m.xmlTypeBranchLevel.value) return "BranchLevel";
        if (label == m.xmlTypeBranchStatus.value) return "BranchStatus";
        if (label == m.xmlTypeBranchPartyNumber.value) return "BranchPartyNumber";
        if (label == m.xmlTypeBranchRound.value) return "BranchRound";
        if (label == m.xmlTypeBranchCast.value) return "BranchCast";
        if (label == m.xmlTypeBranchItem.value) return "BranchItem";
        if (label == m.xmlTypeBranchSkill.value) return "BranchSkill";
        if (label == m.xmlTypeBranchBeast.value) return "BranchBeast";
        if (label == m.xmlTypeBranchInfo.value) return "BranchInfo";
        if (label == m.xmlTypeBranchMoney.value) return "BranchMoney";
        if (label == m.xmlTypeBranchCoupon.value) return "BranchCoupon";
        if (label == m.xmlTypeBranchMultiCoupon.value) return "BranchMultiCoupon";
        if (label == m.xmlTypeBranchCompleteStamp.value) return "BranchCompleteStamp";
        if (label == m.xmlTypeBranchGossip.value) return "BranchGossip";
        if (label == m.xmlTypeBranchKeyCode.value) return "BranchKeyCode";
        if (label == m.xmlTypeSetStep.value) return "SetStep";
        if (label == m.xmlTypeSetStepUp.value) return "SetStepUp";
        if (label == m.xmlTypeSetStepDown.value) return "SetStepDown";
        if (label == m.xmlTypeSubstituteStep.value) return "SubstituteStep";
        if (label == m.xmlTypeSubstituteFlag.value) return "SubstituteFlag";
        if (label == m.xmlTypeGetCast.value) return "GetCast";
        if (label == m.xmlTypeGetItem.value) return "GetItem";
        if (label == m.xmlTypeGetSkill.value) return "GetSkill";
        if (label == m.xmlTypeGetBeast.value) return "GetBeast";
        if (label == m.xmlTypeGetInfo.value) return "GetInfo";
        if (label == m.xmlTypeGetCoupon.value) return "GetCoupon";
        if (label == m.xmlTypeGetCompleteStamp.value) return "GetCompleteStamp";
        if (label == m.xmlTypeGetGossip.value) return "GetGossip";
        if (label == m.xmlTypeLoseCast.value) return "LoseCast";
        if (label == m.xmlTypeLoseItem.value) return "LoseItem";
        if (label == m.xmlTypeLoseSkill.value) return "LoseSkill";
        if (label == m.xmlTypeLoseBeast.value) return "LoseBeast";
        if (label == m.xmlTypeLoseInfo.value) return "LoseInfo";
        if (label == m.xmlTypeLoseMoney.value) return "LoseMoney";
        if (label == m.xmlTypeLoseCoupon.value) return "LoseCoupon";
        if (label == m.xmlTypeLoseCompleteStamp.value) return "LoseCompleteStamp";
        if (label == m.xmlTypeLoseGossip.value) return "LoseGossip";
        return String(label);
    }
    if (const auto* entry = findType(label)) return entry->type;
    return String(label);
}

String XmlContentDialog::typeToLabel(const StringView type) const {
    const auto found = std::find_if(type_entries_.begin(), type_entries_.end(),
                                    [type](const TypeEntry& entry) {
                                        return entry.type == type;
                                    });
    return found == type_entries_.end() ? String(type) : found->label;
}

String XmlContentDialog::templateDirectory(const StringView xmlTypeLabel) const {
    if (!prop_) return {};
    const auto type = labelToType(xmlTypeLabel);
    return prop_->toAppAbs("resource/xmlcontent/template/" + type);
}

String XmlContentDialog::lowerExtension(const StringView value) {
    String result(value);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](const unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return result;
}

std::vector<String> XmlContentDialog::templateFiles(const StringView xmlTypeLabel,
                                                    const bool includeHidden) const {
    std::vector<String> result;
    const auto directory = templateDirectory(xmlTypeLabel);
    if (directory.empty()) return result;
    std::error_code error;
    const auto path = ::cwxeditor::cwx::structs::pathFromUtf8(directory);
    if (!std::filesystem::is_directory(path, error)) return result;
    for (const auto& entry : std::filesystem::directory_iterator(path, error)) {
        if (error || !entry.is_regular_file(error)) continue;
        const auto name = pathText(entry.path().filename());
        const auto extension = lowerExtension(pathText(entry.path().extension()));
        if (includeHidden || isTextExtension(extension)) result.push_back(name);
    }
    std::sort(result.begin(), result.end());
    return result;
}

String XmlContentDialog::getXmlTemplate(const StringView xmlTypeLabel) const {
    return typeTemplate(labelToType(xmlTypeLabel));
}

String XmlContentDialog::message(const StringView key, String fallback) const {
    if (!prop_) return fallback;
    const auto& m = prop_->requiredMsgs();
    if (key == "confirmInitialize") return m.confirmInitialize.value;
    if (key == "xmlTypeNotSelected") return m.xmlTypeNotSelected.value;
    if (key == "initializeLabel") return m.initializeLabel.value;
    if (key == "openReferenceLabel") return m.openReferenceLabel.value;
    return fallback;
}

bool XmlContentDialog::initializeTemplate(const bool force) {
    const auto label = selectedXmlType(shell_);
    if (label.empty()) return false;
    const auto title = prop_ ? prop_->requiredMsgs().initializeLabel.value : "初期化";
    const auto question = message("confirmInitialize", "XMLテンプレートで初期化してもよろしいですか?");
    if (!force && (!confirmInitialize || !confirmInitialize(title, question))) return false;
    if (!text().empty() && !force) {
        if (!confirmOverwrite ||
            !confirmOverwrite(title, "既にテキストが入力されています。上書きしてよろしいですか？"))
            return false;
    }
    replaceTextByEdit(getXmlTemplate(label));
    return true;
}

void XmlContentDialog::initializeTemplateInteractive() {
    const auto label = selectedXmlType(shell_);
    if (label.empty()) return;
    const String title = prop_ ? prop_->requiredMsgs().initializeLabel.value : String{"初期化"};
    const String question = message("confirmInitialize", "XMLテンプレートで初期化してもよろしいですか?");
    if (!askAsync) return;
    // 確認画面が開いている間に、このダイアログが閉じられる場合がある。
    const std::weak_ptr<const int> alive = reference_lifetime_;
    const auto replace = [this, alive, label] {
        if (alive.expired()) return;
        replaceTextByEdit(getXmlTemplate(label));
    };
    askAsync(lib::MessageSeverity::Question, title, question,
        [this, alive, title, replace](const bool accepted) {
            if (!accepted || alive.expired()) return;
            if (text().empty()) { replace(); return; }
            askAsync(lib::MessageSeverity::Warning, title,
                     "既にテキストが入力されています。上書きしてよろしいですか？",
                     [replace](const bool overwrite) { if (overwrite) replace(); });
        });
}

bool XmlContentDialog::selectReferenceFile(const std::size_t index) {
    if (index >= reference_files_.size()) return false;
    auto& file = reference_files_[index];
    String loaded_text;
    try {
        loaded_text = readFile(::cwxeditor::cwx::structs::pathFromUtf8(file.path));
    } catch (const std::exception& exception) {
        if (reportMessage)
            reportMessage(lib::MessageSeverity::Error,
                          prop_ ? prop_->requiredMsgs().dlgTitError.value : String{"エラー"},
                          String(exception.what()) + ": " + file.path);
        return false;
    }
    file.text = std::move(loaded_text);
    selected_reference_ = file.name;
    reference_text_ = file.text;
    if (reference_viewer_)
        (void)lib::set_text_value(*reference_viewer_, reference_text_,
                                  lib::TextValueOrigin::Programmatic);
    return true;
}

std::vector<ReferenceFile> XmlContentDialog::openReference() {
    reference_files_.clear();
    selected_reference_.clear();
    reference_text_.clear();
    if (reference_list_)
        (void)lib::replace_list_items(reference_list_, {}, {}, {});
    if (reference_viewer_)
        (void)lib::set_text_value(*reference_viewer_, {},
                                  lib::TextValueOrigin::Programmatic);
    const auto label = selectedXmlType(shell_);
    if (label.empty()) {
        if (reportMessage) reportMessage(lib::MessageSeverity::Warning, prop_ ? prop_->requiredMsgs().dlgTitWarning.value : String{"警告"}, message("xmlTypeNotSelected", "XMLタイプを選択してください"));
        return reference_files_;
    }
    const auto directory = templateDirectory(label);
    std::error_code error;
    const auto directory_path = ::cwxeditor::cwx::structs::pathFromUtf8(directory);
    const bool has_directory = !directory.empty() &&
        std::filesystem::is_directory(directory_path, error);
    if (!has_directory) {
        if (reportMessage)
            reportMessage(lib::MessageSeverity::Warning,
                          prop_ ? prop_->requiredMsgs().dlgTitWarning.value : String{"警告"},
                          "テンプレートフォルダが見つかりません: " + directory);
        return reference_files_;
    }
    const auto names = templateFiles(label, false);
    if (names.empty()) {
        if (reportMessage)
            reportMessage(lib::MessageSeverity::Information,
                          prop_ ? prop_->requiredMsgs().dlgTitInfo.value : String{"情報"},
                          "参照用テンプレートが見つかりません。");
        return reference_files_;
    }
    for (const auto& name : names) {
        ReferenceFile file;
        file.name = name;
        file.path = pathText(directory_path /
                             ::cwxeditor::cwx::structs::pathFromUtf8(name));
        try { file.text = readFile(::cwxeditor::cwx::structs::pathFromUtf8(file.path)); }
        catch (const std::exception&) {
            // D版と同様、一覧の項目を選んだときにテンプレートを読む。 読
            // めない項目も表示したままにし、選択操作でエラーを知らせる。
            file.text.clear();
        }
        reference_files_.push_back(std::move(file));
    }
    // D版と同様、参照ウィンドウは空の表示欄で開く。 各
    // ファイルは一覧で選ばれてから読み込み、表示する。
    if (reference_list_) {
        std::vector<lib::ListItem> items;
        items.reserve(reference_files_.size());
        for (const auto& file : reference_files_)
            items.push_back({file.name, file.path});
        (void)lib::replace_list_items(reference_list_, std::move(items), {}, {});
    }
    if (reference_viewer_)
        (void)lib::set_text_value(*reference_viewer_, {},
                                  lib::TextValueOrigin::Programmatic);
    showReferenceDialog();
    if (referenceOpened) referenceOpened(reference_files_);
    return reference_files_;
}

void XmlContentDialog::showReferenceDialog() {
    if (!prop_) return;
    if (!reference_viewer_) (void)createControl();
    reference_viewer_->text = reference_text_;
    const auto services = prop_->images().services();
    if (reference_dialog_ && services.gui) {
        services.gui->end_modal(reference_dialog_);
        if (reference_dialog_->visible()) services.gui->close(reference_dialog_);
    }
    reference_dialog_ = std::make_shared<lib::Dialog>();
    auto window = reference_dialog_;
    lib::configure_owned_window(window, dialog_ ? std::static_pointer_cast<lib::Window>(dialog_) : shell_,
                                lib::OwnedWindowKind::Modal);
    window->title = prop_->requiredMsgs().openReferenceLabel.value + " - " + selectedXmlType(shell_);
    window->set_bounds({0.0F, 0.0F, 920.0F, 520.0F});
    auto layout = std::make_shared<lib::GridLayout>();
    layout->columns = 2U;
    layout->spacing = 6.0F;
    layout->padding = {6.0F, 6.0F, 6.0F, 6.0F};
    window->set_layout(layout);
    auto scroll = std::make_shared<lib::ScrollView>();
    scroll->horizontal_scroll = true;
    scroll->set_layout_item({.row=0U, .column=0U, .vertical=lib::Align::Stretch,
                             .preferred={300.0F,0.0F}, .grow=0.35F});
    auto files = std::make_shared<lib::Panel>();
    auto file_layout = std::make_shared<lib::GridLayout>();
    file_layout->columns = 1U;
    file_layout->spacing = 4.0F;
    files->set_layout(file_layout);
    scroll->add(files);
    window->add(scroll);
    const std::weak_ptr<const int> alive = reference_lifetime_;
    for (std::size_t index = 0; index < reference_files_.size(); ++index) {
        auto button = std::make_shared<lib::Button>("[" + std::to_string(index + 1U) + "] " +
                                                   reference_files_[index].name);
        button->set_tooltip(reference_files_[index].path);
        button->on_click = [this, alive, index, services] {
            if (alive.expired() || index >= reference_files_.size()) return;
            if (!selectReferenceFile(index)) return;
            const auto& file = reference_files_[index];
            reference_viewer_->set_tooltip(file.path);
            if (services.gui) services.gui->focus(reference_viewer_);
        };
        files->add(button);
    }
    reference_viewer_->set_visible(true);
    reference_viewer_->vertical_scroll = true;
    reference_viewer_->minimum_visible_lines = 12;
    reference_viewer_->set_layout_item({.row=0U, .column=1U,
        .horizontal=lib::Align::Stretch, .vertical=lib::Align::Stretch, .grow=0.65F});
    window->add(reference_viewer_);
    auto footer = std::make_shared<lib::Panel>();
    footer->set_layout(std::make_shared<lib::LinearLayout>(lib::Orientation::Horizontal));
    footer->set_layout_item({.row=1U, .column=0U, .column_span=2U, .horizontal=lib::Align::Center});
    auto copy = std::make_shared<lib::Button>(prop_->requiredMsgs().referenceCopyAllLabel.value);
    const auto viewer = reference_viewer_;
    copy->on_click = [viewer, clipboard = services.clipboard] {
        if (clipboard) { lib::ClipboardData data; data.text = viewer->text; clipboard->write(data); }
    };
    auto close_button = std::make_shared<lib::Button>(prop_->requiredMsgs().dlgTextClose.value);
    close_button->cancel_button = true;
    const std::weak_ptr<lib::Dialog> weak_window = window;
    const auto close = [weak_window, gui = services.gui] {
        if (const auto value = weak_window.lock()) {
            value->result = lib::DialogResult::Cancel;
            if (gui) {
                gui->end_modal(value);
                if (value->visible()) gui->close(value);
            } else value->set_visible(false);
        }
    };
    close_button->on_click = close;
    window->on_close_requested = close;
    footer->add(copy);
    footer->add(close_button);
    window->add(footer);
    const std::weak_ptr<lib::TextEdit> weak_viewer = viewer;
    auto menu = std::make_shared<lib::Menu>(true);
    menu->items = {{prop_->buildMenu(types::MenuID::Copy), "copy"},
                   {prop_->buildMenu(types::MenuID::SelectAll), "select-all"}};
    menu->on_command = [weak_viewer, clipboard = services.clipboard](const StringView command) {
        const auto viewer = weak_viewer.lock();
        if (!viewer) return;
        if (command == "select-all") {
            lib::TextEditInteractionState state;
            (void)lib::select_all_text(*viewer, state);
        } else if (command == "copy" && clipboard) {
            const auto start = std::min(viewer->selection_start, viewer->text.size());
            lib::ClipboardData data;
            data.text = viewer->text.substr(start, std::min(viewer->selection_length, viewer->text.size() - start));
            clipboard->write(data);
        }
    };
    viewer->on_pointer = [weak_viewer, menu, gui = services.gui](lib::PointerEvent& event) {
        if (event.button == lib::PointerButton::Secondary && event.action == lib::PointerAction::Up && gui)
            if (const auto value = weak_viewer.lock()) {
                gui->show_context_menu(menu, value, event.position);
                event.handled = true;
            }
    };
    // 表示側が対応する場合は非ブロッキングにする。 入れ子の同期モーダル
    // ループは、ブラウザー内の Web ALL エディタを停止させる。
    const auto owner = dialog_ ? std::static_pointer_cast<lib::Window>(dialog_) : shell_;
    if (services.gui) {
        if (!services.gui->begin_modal(window, owner))
            (void)services.gui->show_modal(window, owner);
    } else window->set_visible(true);
}

bool XmlContentDialog::variableExists(const StringView name) const {
    if (!summary_) return true;
    const auto root = summary_->flagDirRoot();
    if (!root) return true;
    if (root->findVariant(String(name))) return true;
    return root->getVariant(String(name)) != nullptr;
}

XmlCheckResult XmlContentDialog::checkXml(const bool substituteVariables) const {
    XmlCheckResult result;
    result.checkedText = text();
    if (result.checkedText.empty()) {
        result.message = "XML文字列が入力されていません。";
        return result;
    }
    if (substituteVariables) {
        static const std::regex pattern(R"(@([^@]+)@)");
        String checked;
        std::size_t cursor = 0;
        for (std::sregex_iterator it(result.checkedText.begin(), result.checkedText.end(), pattern),
             end; it != end; ++it) {
            const auto& match = *it;
            const auto position = static_cast<std::size_t>(match.position());
            const auto length = static_cast<std::size_t>(match.length());
            checked.append(result.checkedText, cursor, position - cursor);
            const String name = match.str(1);
            if (variableExists(name)) {
                checked.append(length, ' ');
                result.replacedVariables.push_back(name);
            } else {
                checked.append(match.str());
            }
            cursor = position + length;
        }
        checked.append(result.checkedText, cursor, String::npos);
        result.checkedText = std::move(checked);
    }
    try {
        (void)lib::XmlNode::parse(result.checkedText, true);
        result.valid = true;
        result.message = "XML構文は正しいです。\n構成のみの確認で、内容については保証されません。";
    } catch (const std::exception& exception) {
        result.valid = false;
        result.message = "XML構文エラーが発見されました:\n" + String(exception.what());
    }
    if (!result.replacedVariables.empty()) {
        String prefix;
        for (std::size_t i = 0; i < result.replacedVariables.size(); ++i) {
            if (i != 0) prefix += "、";
            prefix += result.replacedVariables[i];
        }
        result.message = prefix + " を空白に置き換え、検証しました。\n\n" + result.message;
    }
    return result;
}

void XmlContentDialog::setAssignment(const int value) noexcept {
    assignment_ = value == 0 ? 0 : 1;
    if (assign_off_) assign_off_->checked = assignment_ == 0;
    if (assign_on_) assign_on_->checked = assignment_ != 0;
    dirty_ = true;
}

std::vector<std::pair<ContentPtr, String>> XmlContentDialog::applyBranchNames(
    const ContentPtr& event, const ::cwxeditor::cwx::props::CProps& prop) {
    std::vector<std::pair<ContentPtr, String>> changes;
    if (!event) return changes;
    const auto& type = event->xmlType();
    const bool talk = type == "TalkMessage" || type == "TalkDialog";
    if (!talk && !isBranchType(type)) return changes;
    for (std::size_t index = 0; index < event->next().size(); ++index) {
        const auto& child = event->next()[index];
        if (!child || !child->name().empty()) continue;
        const auto old_name = child->name();
        child->setName(&prop, talk ? prop.sys().evtChildOK("")
            : index == 0 ? prop.sys().evtChildTrue() : prop.sys().evtChildFalse());
        if (child->name() != old_name) changes.emplace_back(child, old_name);
    }
    return changes;
}

bool XmlContentDialog::apply() {
    if (!prop_) return false;
    // D版の XmlContentDialog.apply と同様、テキストはそのまま
    // 保存する。 XML構文は、明示的な「XML構文をチェック」ボタンでだけ検査する。
    const bool created = !event_;
    if (created) event_ = std::make_shared<Content>(event::CType::CustomXmlContent, "");
    const auto label = selectedXmlType(shell_);
    const auto type = [&]() -> String {
        if (lib::editor_window_host_options().all_mode) return labelToType(label);
        // D版と同様、適用時にはテンプレート検索とは別の順序付きラベル対応を使う。
        const auto& m = prop_->requiredMsgs();
        if (label == m.xmlTypeChangeBgImage.value) return "ChangeBgImage";
        if (label == m.xmlTypeMoveBgImage.value) return "MoveBgImage";
        if (label == m.xmlTypeLoseBgImage.value) return "LoseBgImage";
        if (label == m.xmlTypeReplaceBgImage.value) return "ReplaceBgImage";
        if (label == m.xmlTypeMultipleMoveLose.value) return "MultipleMoveLose";
        if (label == m.xmlTypeEffect.value) return "Effect";
        if (label == m.xmlTypeInputDialog.value) return "InputDialog";
        if (label == m.xmlTypeChangeEnemy.value) return "ChangeEnemy";
        if (label == m.xmlTypeSetFlag.value) return "SetFlag";
        if (label == m.xmlTypeBranchFlag.value) return "BranchFlag";
        if (label == m.xmlTypeBranchFlagValue.value) return "BranchFlagValue";
        if (label == m.xmlTypeReverseFlag.value) return "ReverseFlag";
        if (label == m.xmlTypeBranchStep.value) return "BranchStep";
        if (label == m.xmlTypeBranchStepValue.value) return "BranchStepValue";
        if (label == m.xmlTypeMoveCard.value) return "MoveCard";
        if (label == m.xmlTypeWait.value) return "Wait";
        if (label == m.xmlTypeGetMoney.value) return "GetMoney";
        if (label == m.xmlTypeSetVariantStr.value) return "SetVariantStr";
        if (label == m.xmlTypeSetActionEvent.value) return "SetActionEvent";
        if (label == m.xmlTypeEndAction.value) return "EndAction";
        if (label == m.xmlTypeBranchSelect.value) return "BranchSelect";
        if (label == m.xmlTypeBranchAbility.value) return "BranchAbility";
        if (label == m.xmlTypeBranchRandom.value) return "BranchRandom";
        if (label == m.xmlTypeBranchLevel.value) return "BranchLevel";
        if (label == m.xmlTypeBranchStatus.value) return "BranchStatus";
        if (label == m.xmlTypeBranchPartyNumber.value) return "BranchPartyNumber";
        if (label == m.xmlTypeBranchRound.value) return "BranchRound";
        if (label == m.xmlTypeBranchCast.value) return "BranchCast";
        if (label == m.xmlTypeBranchItem.value) return "BranchItem";
        if (label == m.xmlTypeBranchSkill.value) return "BranchSkill";
        if (label == m.xmlTypeBranchBeast.value) return "BranchBeast";
        if (label == m.xmlTypeBranchInfo.value) return "BranchInfo";
        if (label == m.xmlTypeBranchMoney.value) return "BranchMoney";
        if (label == m.xmlTypeBranchCoupon.value) return "BranchCoupon";
        if (label == m.xmlTypeBranchMultiCoupon.value) return "BranchMultiCoupon";
        if (label == m.xmlTypeBranchCompleteStamp.value) return "BranchCompleteStamp";
        if (label == m.xmlTypeBranchGossip.value) return "BranchGossip";
        if (label == m.xmlTypeBranchKeyCode.value) return "BranchKeyCode";
        if (label == m.xmlTypeSetStep.value) return "SetStep";
        if (label == m.xmlTypeSetStepUp.value) return "SetStepUp";
        if (label == m.xmlTypeSetStepDown.value) return "SetStepDown";
        if (label == m.xmlTypeSubstituteStep.value) return "SubstituteStep";
        if (label == m.xmlTypeSubstituteFlag.value) return "SubstituteFlag";
        if (label == m.xmlTypeTalkMessage.value) return "TalkMessage";
        if (label == m.xmlTypeTalkDialog.value) return "TalkDialog";
        if (label == m.xmlTypePlayBgm.value) return "PlayBgm";
        if (label == m.xmlTypePlaySound.value) return "PlaySound";
        if (label == m.xmlTypeCheckStep.value) return "CheckStep";
        if (label == m.xmlTypeCheckFlag.value) return "CheckFlag";
        if (label == m.xmlTypeCheckVariant.value) return "CheckVariant";
        if (label == m.xmlTypeGetSkill.value) return "GetSkill";
        if (label == m.xmlTypeGetCast.value) return "GetCast";
        if (label == m.xmlTypeGetItem.value) return "GetItem";
        if (label == m.xmlTypeGetBeast.value) return "GetBeast";
        if (label == m.xmlTypeGetInfo.value) return "GetInfo";
        if (label == m.xmlTypeGetCoupon.value) return "GetCoupon";
        if (label == m.xmlTypeGetCompleteStamp.value) return "GetCompleteStamp";
        if (label == m.xmlTypeGetGossip.value) return "GetGossip";
        if (label == m.xmlTypeLoseCast.value) return "LoseCast";
        if (label == m.xmlTypeLoseItem.value) return "LoseItem";
        if (label == m.xmlTypeLoseSkill.value) return "LoseSkill";
        if (label == m.xmlTypeLoseBeast.value) return "LoseBeast";
        if (label == m.xmlTypeLoseInfo.value) return "LoseInfo";
        if (label == m.xmlTypeLoseMoney.value) return "LoseMoney";
        if (label == m.xmlTypeLoseCoupon.value) return "LoseCoupon";
        if (label == m.xmlTypeLoseCompleteStamp.value) return "LoseCompleteStamp";
        if (label == m.xmlTypeLoseGossip.value) return "LoseGossip";
        return String(label);
    }();
    event_->xmlStr(text());
    event_->xmlType(type);
    event_->xmlAssignment(assignment_);
    event_->xmlVariable({});
    (void)applyBranchNames(event_, prop_->parent());
    if (use_counter_) event_->setUseCounter(use_counter_, parent_content_);
    // D版の Content.add(prop, ...) と同様、新しい子に親の validText() を適用する。
    if (created && parent_content_)
        parent_content_->add(prop_ ? &prop_->parent() : nullptr, event_);
    dirty_ = false;
    if (comm_) comm_->changed.call();
    if (contentChanged) contentChanged(event_);
    if (applied) applied();
    return true;
}

void XmlContentDialog::close() {
    if (reference_dialog_ && prop_ && prop_->images().services().gui) {
        prop_->images().services().gui->end_modal(reference_dialog_);
        if (reference_dialog_->visible())
            prop_->images().services().gui->close(reference_dialog_);
    }
    if (variant_dialog_ && prop_ && prop_->images().services().gui) {
        prop_->images().services().gui->end_modal(variant_dialog_);
        if (variant_dialog_->visible())
            prop_->images().services().gui->close(variant_dialog_);
    }
    variant_dialog_.reset();
    variant_chooser_.reset();
    if (event_host_ && event_host_->isOpen()) {
        event_host_->forceCancel();
        return;
    }
    if (!open_) return;
    open_ = false;
    if (dialog_ && prop_ && prop_->images().services().gui && dialog_->visible())
        prop_->images().services().gui->close(dialog_);
    if (closed) closed();
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::xmlcontentdialog
