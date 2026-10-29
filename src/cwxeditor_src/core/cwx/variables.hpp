#pragma once

// cwx/variables.dのC++20移植。 プロパティの情報とXML
// の走査は、settings.dのProp/XMLFuncsに合わせる。

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <utility>

#include "cwxeditor_src/core/cwx/settings.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/core/cwx/xml.hpp"
#include "cwxeditor_src/lib/portable.hpp"

namespace cwxeditor::cwx::event {
class Content;
}

namespace cwxeditor::cwx::variables {

namespace lib = ::cwxeditor::lib;
namespace settings = ::cwxeditor::cwx::settings;
namespace structs = ::cwxeditor::cwx::structs;
namespace types = ::cwxeditor::cwx::types;

// 割合が0なら、百分率導入前の設定を示す。 XMLキーを
// 変えず、従来の整数の画像倍率も読み込めるようにする。
[[nodiscard]] inline std::uint32_t previewScalePercent(
    const std::uint32_t percent, const std::uint32_t legacyScale) noexcept {
    return percent == 0U ? std::clamp(legacyScale, 1U, 16U) * 100U
                         : std::clamp(percent, 100U, 200U);
}

/// event.Contentの実装に依存しない、Dのevent.ContentInitializer相当の型。
struct ContentInitializer {
    inline static constexpr lib::StringView XML_NAME = "contentInitializer";
    lib::String dataVersion;
    lib::OrderedDictionary<types::CType, std::shared_ptr<::cwxeditor::cwx::event::Content>> initializer;

    [[nodiscard]] xml::XNode toNode() const;
    void toNode(xml::XNode& node) const;
    void fromNode(xml::XNode& node);
};

class FlexEtcProps : public settings::Properties {
public:
    settings::Prop<lib::String, true, false> languageDir{"languageDir", "lang"};
    settings::Prop<lib::String, false, false> languageFile{"languageFile", ""};
    settings::Prop<bool, false, false> useSystemLanguage{"useSystemLanguage", true};
    settings::Prop<std::uint32_t, false, false> warningWindowsHandleCount{"warningWindowsHandleCount", 1536};
    settings::Prop<std::uint32_t, false, false> liftWarningWindowsHandleCount{"liftWarningWindowsHandleCount", 1536};
    settings::Prop<lib::String, false, false> targetVersion{"targetVersion", "1.50"};
    settings::Prop<std::int32_t, false, false> imeMode{"imeMode", 0};
    settings::Prop<std::uint32_t, false, false> imageScale{"imageScale", 1, 2016091800};
    settings::Prop<std::uint32_t, false, false> drawingScale{"drawingScale", 1};
    settings::Prop<std::uint32_t, false, false> imageScalePercent{"imageScalePercent", 0};
    settings::Prop<std::uint32_t, false, false> drawingScalePercent{"drawingScalePercent", 0};
    settings::Prop<std::uint32_t, false, false> uiTextScale{"uiTextScale", 100};
    settings::Prop<std::uint32_t, true, false> tabs{"tabs", 4};
    settings::Prop<bool, false, false> toolsLock{"toolsLock", false};
    settings::Prop<lib::Array<std::int32_t>, false, false> toolsOrder{"toolsOrder", lib::Array<std::int32_t>{}};
    settings::Prop<lib::Array<std::int32_t>, false, false> toolsWrapIndices{"toolsWrapIndices", lib::Array<std::int32_t>{}};
    settings::Prop<std::int32_t, true, false> comboVisibleItemCount{"comboVisibleItemCount", 20};
    settings::Prop<std::int32_t, false, false> directorySashL{"directorySashL", 2};
    settings::Prop<std::int32_t, false, false> directorySashR{"directorySashR", 5};
    settings::Prop<bool, false, false> directorySashV{"directorySashV", false};
    settings::Prop<std::int32_t, false, false> filesSortColumn{"filesSortColumn", 1};
    settings::Prop<std::int32_t, false, false> filesSortDirection{"filesSortDirection", static_cast<std::int32_t>(structs::SortDir::Up)};
    settings::Prop<std::int32_t, false, true> fileNameColumn{"fileNameColumn", 150};
    settings::Prop<std::int32_t, false, true> fileExtColumn{"fileExtColumn", 60};
    settings::Prop<std::int32_t, false, true> fileCountColumn{"fileCountColumn", 60};
    settings::Prop<std::int32_t, false, true> areaIdColumn{"areaIdColumn", 50};
    settings::Prop<std::int32_t, false, true> areaNameColumn{"areaNameColumn", 190};
    settings::Prop<std::int32_t, false, true> areaCountColumn{"areaCountColumn", 60};
    settings::Prop<std::int32_t, false, true> importAreaIdColumn{"importAreaIdColumn", 50};
    settings::Prop<std::int32_t, false, true> importAreaNameColumn{"importAreaNameColumn", 190};
    settings::Prop<std::int32_t, false, true> importAreaCountColumn{"importAreaCountColumn", 60};
    settings::Prop<std::int32_t, false, false> areasSortColumn{"areasSortColumn", 0};
    settings::Prop<std::int32_t, false, false> areasSortDirection{"areasSortDirection", static_cast<std::int32_t>(structs::SortDir::Up)};
    settings::Prop<std::int32_t, false, false> importAreasSortColumn{"importAreasSortColumn", 0};
    settings::Prop<std::int32_t, false, false> importAreasSortDirection{"importAreasSortDirection", static_cast<std::int32_t>(structs::SortDir::Up)};
    settings::Prop<std::int32_t, false, false> summaryParamSashL{"summaryParamSashL", 2, 2012101100};
    settings::Prop<std::int32_t, false, false> summaryParamSashR{"summaryParamSashR", 1, 2012101100};
    settings::Prop<std::int32_t, false, false> rCouponsStartAreaSashL{"rCouponsStartAreaSashL", 1, 2012101100};
    settings::Prop<std::int32_t, false, false> rCouponsStartAreaSashR{"rCouponsStartAreaSashR", 1, 2012101100};
    settings::Prop<std::int32_t, false, false> areaViewL{"areaViewL", 1};
    settings::Prop<std::int32_t, false, false> areaViewR{"areaViewR", 4};
    settings::Prop<std::int32_t, false, false> battleViewL{"battleViewL", 1};
    settings::Prop<std::int32_t, false, false> battleViewR{"battleViewR", 4};
    settings::Prop<std::int32_t, false, false> bgImageViewL{"bgImageViewL", 1};
    settings::Prop<std::int32_t, false, false> bgImageViewR{"bgImageViewR", 4};
    settings::Prop<std::int32_t, false, false> areaViewImageFlagL{"areaViewImageFlagL", 4};
    settings::Prop<std::int32_t, false, false> areaViewImageFlagR{"areaViewImageFlagR", 1};
    settings::Prop<std::int32_t, false, false> battleViewImageFlagL{"battleViewImageFlagL", 4};
    settings::Prop<std::int32_t, false, false> battleViewImageFlagR{"battleViewImageFlagR", 1};
    settings::Prop<std::int32_t, false, false> bgImageViewImageFlagL{"bgImageViewImageFlagL", 4};
    settings::Prop<std::int32_t, false, false> bgImageViewImageFlagR{"bgImageViewImageFlagR", 1};
    settings::Prop<std::int32_t, true, false> partyCardAlpha{"partyCardAlpha", 176};
    settings::Prop<bool, false, false> viewPartyCardsArea{"viewPartyCardsArea", true};
    settings::Prop<bool, false, false> viewPartyCardsBattle{"viewPartyCardsBattle", true};
    settings::Prop<bool, false, false> viewEnemyCardDebug{"viewEnemyCardDebug", false};
    settings::Prop<bool, false, false> viewPartyCardsEvent{"viewPartyCardsEvent", true};
    settings::Prop<std::int32_t, true, false> messageAlpha{"messageAlpha", 176};
    settings::Prop<bool, false, false> viewMessageArea{"viewMessageArea", false};
    settings::Prop<bool, false, false> viewMessageBattle{"viewMessageBattle", false};
    settings::Prop<bool, false, false> viewMessageEvent{"viewMessageEvent", false};
    // 意図的な変更(D版と異なる): D版の初期値は true だが、あえて初期値を
    // false(カード参照の表示オフ)にしている。ALL・standalone 共通。
    settings::Prop<bool, false, false> viewReferenceCards{"viewReferenceCards", false};
    settings::Prop<bool, false, false> fixedImagesMenuCards{"fixedImagesMenuCards", false};
    settings::Prop<bool, false, false> fixedImagesCells{"fixedImagesCells", false};
    settings::Prop<bool, false, false> fixedImagesBackground{"fixedImagesBackground", true};
    settings::Prop<bool, false, false> fixedImagesBattle{"fixedImagesBattle", false};
    settings::Prop<bool, false, false> fixedImagesEvent{"fixedImagesEvent", false};
    settings::Prop<bool, false, false> fixedImagesEventBackground{"fixedImagesEventBackground", true};
    settings::Prop<bool, false, false> fixedImagesDefaultBackground{"fixedImagesDefaultBackground", true};
    settings::Prop<bool, false, false> viewCards{"viewCards", true};
    settings::Prop<bool, false, false> viewBgImages{"viewBgImages", true};
    settings::Prop<bool, false, false> showGrid{"showGrid", false};
    settings::Prop<std::int32_t, false, false> areaSashT{"areaSashT", 5};
    settings::Prop<std::int32_t, false, false> areaSashB{"areaSashB", 4};
    settings::Prop<std::int32_t, false, false> areaSashL{"areaSashL", 1};
    settings::Prop<std::int32_t, false, false> areaSashR{"areaSashR", 4};
    settings::Prop<std::int32_t, false, false> importAreaSashL{"importAreaSashL", 1};
    settings::Prop<std::int32_t, false, false> importAreaSashR{"importAreaSashR", 3};
    settings::Prop<bool, false, false> areaSashV{"areaSashV", false};
    settings::Prop<bool, false, false> importAreaSashV{"importAreaSashV", true};
    settings::Prop<std::int32_t, false, false> flagSashL{"flagSashL", 1};
    settings::Prop<std::int32_t, false, false> flagSashR{"flagSashR", 4};
    settings::Prop<bool, false, false> flagSashV{"flagSashV", false};
    settings::Prop<std::int32_t, true, true> flagsWidth{"flagsWidth", 150};
    settings::Prop<std::int32_t, true, true> flagsHeight{"flagsHeight", 200};
    settings::Prop<std::int32_t, false, false> menuCardSashL{"menuCardSashL", 5, 2012101100};
    settings::Prop<std::int32_t, false, false> menuCardSashR{"menuCardSashR", 3, 2012101100};
    settings::Prop<std::int32_t, false, false> enemyCardSashL{"enemyCardSashL", 3};
    settings::Prop<std::int32_t, false, false> enemyCardSashR{"enemyCardSashR", 5};
    settings::Prop<std::int32_t, false, false> backSashL{"backSashL", 5};
    settings::Prop<std::int32_t, false, false> backSashR{"backSashR", 3};
    settings::Prop<std::int32_t, true, true> bgImageSampleWidth{"bgImageSampleWidth", 150};
    settings::Prop<std::int32_t, true, true> bgImageSampleHeight{"bgImageSampleHeight", 150};
    settings::Prop<std::int32_t, true, false> cellPCNumberWidth{"cellPCNumberWidth", 150, false};
    settings::Prop<std::int32_t, false, false> textCellVSashT{"textCellVSashT", 2};
    settings::Prop<std::int32_t, false, false> textCellVSashB{"textCellVSashB", 1};
    settings::Prop<std::int32_t, false, false> textCellHSashL{"textCellHSashL", 2};
    settings::Prop<std::int32_t, false, false> textCellHSashR{"textCellHSashR", 1};
    settings::Prop<std::int32_t, false, false> textCellPreviewSashL{"textCellPreviewSashL", 1};
    settings::Prop<std::int32_t, false, false> textCellPreviewSashR{"textCellPreviewSashR", 1};
    settings::Prop<std::int32_t, true, true> textCellPreviewWidth{"textCellPreviewWidth", 100};
    settings::Prop<std::int32_t, true, true> textCellPreviewHeight{"textCellPreviewHeight", 100};
    settings::Prop<std::int32_t, true, true> textCellBoxWidth{"textCellBoxWidth", 100};
    settings::Prop<std::int32_t, true, true> textCellBoxHeight{"textCellBoxHeight", 50};
    settings::Prop<std::int32_t, false, true> cardIdColumn{"cardIdColumn", 50};
    settings::Prop<std::int32_t, false, true> cardNameColumn{"cardNameColumn", 100};
    settings::Prop<std::int32_t, false, true> cardNumberColumn{"cardNumberColumn", 60};
    settings::Prop<std::int32_t, false, true> cardDescriptionColumn{"cardDescriptionColumn", 280};
    settings::Prop<std::int32_t, false, true> cardCountColumn{"cardCountColumn", 60};
    settings::Prop<std::int32_t, false, true> handCardIdColumn{"handCardIdColumn", 50};
    settings::Prop<std::int32_t, false, true> handCardNameColumn{"handCardNameColumn", 100};
    settings::Prop<std::int32_t, false, true> handCardNumberColumn{"handCardNumberColumn", 60};
    settings::Prop<std::int32_t, false, true> handCardDescriptionColumn{"handCardDescriptionColumn", 50};
    settings::Prop<std::int32_t, false, true> importCardIdColumn{"importCardIdColumn", 50};
    settings::Prop<std::int32_t, false, true> importCardNameColumn{"importCardNameColumn", 100};
    settings::Prop<std::int32_t, false, true> importCardNumberColumn{"importCardNumberColumn", 60};
    settings::Prop<std::int32_t, false, true> importCardDescriptionColumn{"importCardDescriptionColumn", 50};
    settings::Prop<std::int32_t, false, true> importCardCountColumn{"importCardCountColumn", 30};
    settings::Prop<std::int32_t, false, true> importHandCardIdColumn{"importHandCardIdColumn", 50};
    settings::Prop<std::int32_t, false, true> importHandCardNameColumn{"importHandCardNameColumn", 100};
    settings::Prop<std::int32_t, false, true> importHandCardNumberColumn{"importHandCardNumberColumn", 60};
    settings::Prop<std::int32_t, false, true> importHandCardDescriptionColumn{"importHandCardDescriptionColumn", 50};
    settings::Prop<std::int32_t, false, false> mainCardsSortColumn{"mainCardsSortColumn", 0};
    settings::Prop<std::int32_t, false, false> mainCardsSortDirection{"mainCardsSortDirection", static_cast<std::int32_t>(structs::SortDir::Up)};
    settings::Prop<std::int32_t, false, false> handCardsSortColumn{"handCardsSortColumn", 0};
    settings::Prop<std::int32_t, false, false> handCardsSortDirection{"handCardsSortDirection", static_cast<std::int32_t>(structs::SortDir::Up)};
    settings::Prop<std::int32_t, false, false> importCardsSortColumn{"importCardsSortColumn", 0};
    settings::Prop<std::int32_t, false, false> importCardsSortDirection{"importCardsSortDirection", static_cast<std::int32_t>(structs::SortDir::Up)};
    settings::Prop<std::int32_t, false, false> importHandCardsSortColumn{"importHandCardsSortColumn", 0};
    settings::Prop<std::int32_t, false, false> importHandCardsSortDirection{"importHandCardsSortDirection", static_cast<std::int32_t>(structs::SortDir::Up)};
    settings::Prop<structs::CRGB, false, false> linkCardMaskColor{"linkCardMaskColor", structs::CRGB(0, 255, 0, 64), true};
    settings::Prop<std::int32_t, false, false> negativeCardNameBorder{"negativeCardNameBorder", 116, true};
    settings::Prop<structs::CRGB, true, false> cardNameBorderingColorForWhite{"cardNameBorderingColorForWhite", structs::CRGB(0, 0, 0, 64)};
    settings::Prop<structs::CRGB, true, false> cardNameBorderingColorForBlack{"cardNameBorderingColorForBlack", structs::CRGB(255, 255, 255, 64)};
    settings::Prop<std::int32_t, true, true> couponWidth{"couponWidth", 250};
    settings::Prop<std::int32_t, true, true> couponHeight{"couponHeight", 80};
    settings::Prop<std::int32_t, true, true> couponValueColumn{"couponValueColumn", 40};
    settings::Prop<std::int32_t, false, false> couponEventL{"couponEventL", 1};
    settings::Prop<std::int32_t, false, false> couponEventR{"couponEventR", 2};
    settings::Prop<std::int32_t, false, true> idColumn{"idColumn", 50};
    settings::Prop<std::int32_t, false, true> valueNumberColumn{"valueNumberColumn", 50};
    settings::Prop<std::int32_t, true, true> nameTableWidth{"nameTableWidth", 250};
    settings::Prop<std::int32_t, true, true> nameTableHeight{"nameTableHeight", 250};
    settings::Prop<std::int32_t, false, false> flagEventSashL{"flagEventSashL", 3};
    settings::Prop<std::int32_t, false, false> flagEventSashR{"flagEventSashR", 2};
    settings::Prop<std::int32_t, true, true> nameWidth{"nameWidth", 300};
    settings::Prop<std::int32_t, true, true> firesWidth{"firesWidth", 120};
    settings::Prop<std::int32_t, true, true> flagNameWidth{"flagNameWidth", 150};
    settings::Prop<std::int32_t, true, true> flagInitWidth{"flagInitWidth", 50};
    settings::Prop<std::int32_t, true, true> flagValueWidth{"flagValueWidth", 50};
    settings::Prop<std::int32_t, false, true> flagNameColumn{"flagNameColumn", 150};
    settings::Prop<std::int32_t, false, true> flagInitColumn{"flagInitColumn", 90};
    settings::Prop<std::int32_t, false, true> variableInitializationColumn{"variableInitializationColumn", 80};
    settings::Prop<std::int32_t, false, true> flagCountColumn{"flagCountColumn", 60};
    settings::Prop<std::int32_t, false, true> localFlagNameColumn{"localFlagNameColumn", 100};
    settings::Prop<std::int32_t, false, true> localFlagInitColumn{"localFlagInitColumn", 80};
    settings::Prop<std::int32_t, false, true> localVariableInitializationColumn{"localVariableInitializationColumn", 80};
    settings::Prop<std::int32_t, false, true> localFlagCountColumn{"localFlagCountColumn", 60};
    settings::Prop<std::int32_t, false, false> localVariablesSashL{"localVariablesSashL", 1};
    settings::Prop<std::int32_t, false, false> localVariablesSashR{"localVariablesSashR", 4};
    settings::Prop<std::uint32_t, true, false> limitForNumberOfVariant{"limitForNumberOfVariant", 12};
    settings::Prop<std::int32_t, true, true> filesWidth{"filesWidth", 150};
    settings::Prop<std::int32_t, true, true> filesHeight{"filesHeight", 150};
    settings::Prop<std::int32_t, true, true> talkersWidth{"talkersWidth", 100};
    settings::Prop<std::int32_t, true, true> motionsWidth{"motionsWidth", 150};
    settings::Prop<std::int32_t, true, true> incrementalSearchBoxWidth{"incrementalSearchBoxWidth", 100};
    settings::Prop<bool, true, false> showMainToolBar{"showMainToolBar", true, 2017042400};
    settings::Prop<bool, true, false> showSceneToolBar{"showSceneToolBar", true, 2017042400};
    settings::Prop<bool, true, false> showEventToolBar{"showEventToolBar", true, 2017042400};
    settings::Prop<std::int32_t, false, false> flagCombiSashL{"flagCombiSashL", 1};
    settings::Prop<std::int32_t, false, false> flagCombiSashR{"flagCombiSashR", 1};
    settings::Prop<bool, false, false> useCoolBar{"useCoolBar", true};
    settings::Prop<std::int32_t, false, false> mainToolBarCustomSashL{"mainToolBarCustomSashL", 1};
    settings::Prop<std::int32_t, false, false> mainToolBarCustomSashR{"mainToolBarCustomSashR", 1};
    settings::Prop<std::int32_t, false, false> stepTopSashL{"stepTopSashL", 3, 2019042700};
    settings::Prop<std::int32_t, false, false> stepTopSashR{"stepTopSashR", 4, 2019042700};
    settings::Prop<std::int32_t, false, false> flagTopSashL{"flagTopSashL", 3};
    settings::Prop<std::int32_t, false, false> flagTopSashR{"flagTopSashR", 2};
    settings::Prop<std::int32_t, true, true> toolTipWidth{"toolTipWidth", 300};
    settings::Prop<std::int32_t, true, true> colorExampleWidth{"colorExampleWidth", 50};
    settings::Prop<std::int32_t, true, true> buttonWidth{"buttonWidth", 100};
    settings::Prop<std::int32_t, true, true> radioGroupSeparatorWidth{"radioGroupSeparatorWidth", 15};
    settings::Prop<std::int32_t, true, true> couponValueColumnWidth{"couponValueColumnWidth", 40};
    settings::Prop<std::int32_t, false, true> idNameColumn{"idNameColumn", 330};
    settings::Prop<std::int32_t, false, true> idCountColumn{"idCountColumn", 60};
    settings::Prop<std::int32_t, false, false> couponSortColumn{"couponSortColumn", 0};
    settings::Prop<std::int32_t, false, false> couponSortDirection{"couponSortDirection", static_cast<std::int32_t>(structs::SortDir::Up)};
    settings::Prop<std::int32_t, false, false> gossipSortColumn{"gossipSortColumn", 0};
    settings::Prop<std::int32_t, false, false> gossipSortDirection{"gossipSortDirection", static_cast<std::int32_t>(structs::SortDir::Up)};
    settings::Prop<std::int32_t, false, false> completeStampSortColumn{"completeStampSortColumn", 0};
    settings::Prop<std::int32_t, false, false> completeStampSortDirection{"completeStampSortDirection", static_cast<std::int32_t>(structs::SortDir::Up)};
    settings::Prop<std::int32_t, false, false> keyCodeSortColumn{"keyCodeSortColumn", 0};
    settings::Prop<std::int32_t, false, false> keyCodeSortDirection{"keyCodeSortDirection", static_cast<std::int32_t>(structs::SortDir::Up)};
    settings::Prop<std::int32_t, false, false> cellNameSortColumn{"cellNameSortColumn", 0};
    settings::Prop<std::int32_t, false, false> cellNameSortDirection{"cellNameSortDirection", static_cast<std::int32_t>(structs::SortDir::Up)};
    settings::Prop<std::int32_t, false, false> cardGroupSortColumn{"cardGroupSortColumn", 0};
    settings::Prop<std::int32_t, false, false> cardGroupSortDirection{"cardGroupSortDirection", static_cast<std::int32_t>(structs::SortDir::Up)};
    settings::Prop<std::int32_t, false, false> authorNewAreaNameSashL{"authorNewAreaNameSashL", 1};
    settings::Prop<std::int32_t, false, false> authorNewAreaNameSashR{"authorNewAreaNameSashR", 1};
    settings::Prop<std::int32_t, false, true> historyScenarioNameColumn{"historyScenarioNameColumn", 150};
    settings::Prop<std::int32_t, false, true> historyScenarioPathColumn{"historyScenarioPathColumn", 300};
    settings::Prop<std::int32_t, false, true> executionPartyNameColumn{"executionPartyNameColumn", 150};
    settings::Prop<std::int32_t, false, true> executionPartyYadoColumn{"executionPartyYadoColumn", 150};
    settings::Prop<std::int32_t, false, true> executionPartyEngineColumn{"executionPartyEngineColumn", 150};
    settings::Prop<std::int32_t, false, false> expressionAndTargetSashT{"expressionAndTargetSashT", -1};
    settings::Prop<std::int32_t, false, false> expressionAndTargetSashB{"expressionAndTargetSashB", -1};
    settings::Prop<std::int32_t, false, false> expressionFunctionAndArgsSashT{"expressionFunctionAndArgsSashT", 3};
    settings::Prop<std::int32_t, false, false> expressionFunctionAndArgsSashB{"expressionFunctionAndArgsSashB", 2};
    settings::Prop<std::int32_t, false, true> functionCallEditorWidth{"functionCallEditorWidth", 350};
    settings::Prop<std::int32_t, false, true> functionCallEditorHeight{"functionCallEditorHeight", 432};
    settings::Prop<std::int32_t, false, true> functionArgumentNameColumn{"functionArgumentNameColumn", 100};
    settings::Prop<std::int32_t, false, true> functionArgumentTypeColumn{"functionArgumentTypeColumn", 80};
    settings::Prop<std::int32_t, false, true> functionArgumentValueColumn{"functionArgumentValueColumn", 120};
    settings::Prop<std::uint32_t, true, false> expressionCheckingDelay{"expressionCheckingDelay", 500};
    settings::Prop<std::int32_t, false, true> keyCodesByFeaturesFeatureColumn{"keyCodesByFeaturesFeatureColumn", 95};
    settings::Prop<std::int32_t, false, true> keyCodesByFeaturesKeyCodeColumn{"keyCodesByFeaturesKeyCodeColumn", 90};
    settings::Prop<std::int32_t, false, true> keyCodesByMotionsMotionColumn{"keyCodesByMotionsMotionColumn", 85};
    settings::Prop<std::int32_t, false, true> keyCodesByMotionsElementColumn{"keyCodesByMotionsElementColumn", 85};
    settings::Prop<std::int32_t, false, true> keyCodesByMotionsKeyCodeColumn{"keyCodesByMotionsKeyCodeColumn", 90};
    settings::Prop<std::int32_t, false, true> elementOverrideNameColumn{"elementOverrideNameColumn", 50};
    settings::Prop<std::int32_t, false, true> elementOverrideTargetTypeColumn{"elementOverrideTargetTypeColumn", 110};
    settings::Prop<std::int32_t, false, false> keyCodesElementSashL{"keyCodesElementSashL", 3};
    settings::Prop<std::int32_t, false, false> keyCodesElementSashR{"keyCodesElementSashR", 2};
    settings::Prop<std::uint32_t, true, false> partyMax{"partyMax", 6};
    settings::Prop<std::int32_t, false, false> cardScaleMax{"cardScaleMax", 300};
    settings::Prop<std::int32_t, false, false> cardScaleMin{"cardScaleMin", 50};
    settings::Prop<std::int32_t, false, false> layerMax{"layerMax", 9999};
    settings::Prop<std::uint32_t, true, false> posLeftMax{"posLeftMax", 9999};
    settings::Prop<std::uint32_t, true, false> posTopMax{"posTopMax", 9999};
    settings::Prop<std::uint32_t, true, false> backWidthMax{"backWidthMax", 9999};
    settings::Prop<std::uint32_t, true, false> backHeightMax{"backHeightMax", 9999};
    settings::Prop<std::uint32_t, true, false> levelMax{"levelMax", 15};
    settings::Prop<std::uint32_t, true, false> castLevelMax{"castLevelMax", 99};
    settings::Prop<std::uint32_t, true, false> lifeMax{"lifeMax", 9999};
    settings::Prop<std::uint32_t, true, false> couponValueMax{"couponValueMax", 999};
    settings::Prop<std::uint32_t, true, false> physicalMax{"physicalMax", 15};
    settings::Prop<std::uint32_t, true, false> mentalMax{"mentalMax", 4};
    settings::Prop<std::uint32_t, true, false> skillLevelMax{"skillLevelMax", 999};
    settings::Prop<std::uint32_t, true, false> useCountMax{"useCountMax", 999};
    settings::Prop<std::uint32_t, true, false> priceMax{"priceMax", 999999};
    settings::Prop<std::uint32_t, true, false> enhanceMax{"enhanceMax", 10};
    settings::Prop<std::uint32_t, true, false> roundMax{"roundMax", 999};
    settings::Prop<std::uint32_t, true, false> paralyzeMax{"paralyzeMax", 40};
    settings::Prop<std::uint32_t, true, false> poisonMax{"poisonMax", 40};
    settings::Prop<std::uint32_t, true, false> cardNumberMax{"cardNumberMax", 99};
    settings::Prop<std::uint32_t, true, false> waitMax{"waitMax", 1000};
    settings::Prop<std::uint32_t, true, false> uValueMax{"uValueMax", 999};
    settings::Prop<std::uint32_t, true, false> skillPowerMax{"skillPowerMax", 9};
    settings::Prop<std::uint32_t, true, false> beastMaxNest{"beastMaxNest", 99};
    settings::Prop<bool, false, false> flagInitValue{"flagInitValue", true};
    settings::Prop<std::int32_t, false, false> stepInitValue{"stepInitValue", 0};
    settings::Prop<std::uint32_t, false, false> stepCountMax{"stepCountMax", 10000, true};
    settings::Prop<lib::String, false, false> stepValueName{"stepValueName", "Step - $N"};
    settings::Prop<std::int32_t, false, true> imageListWidth{"imageListWidth", 380};
    settings::Prop<std::int32_t, false, true> imageListHeight{"imageListHeight", 300};
    settings::Prop<std::int32_t, false, true> imageListForBgImagesWidth{"imageListForBgImagesWidth", 380};
    settings::Prop<std::int32_t, false, true> imageListForBgImagesHeight{"imageListForBgImagesHeight", 300};
    settings::Prop<std::int32_t, false, true> layerListWidth{"layerListWidth", 300};
    settings::Prop<std::int32_t, false, true> layerListHeight{"layerListHeight", 350};
    settings::Prop<bool, false, false> cardLife{"cardLife", true};
    settings::Prop<bool, false, false> cardDetails{"cardDetails", false};
    settings::Prop<std::int32_t, true, true> cardsMarginX{"cardsMarginX", 5};
    settings::Prop<std::int32_t, true, true> cardsSpaceX{"cardsSpaceX", 8};
    settings::Prop<std::int32_t, true, true> cardsMarginY{"cardsMarginY", 5};
    settings::Prop<std::int32_t, true, true> cardsSpaceY{"cardsSpaceY", 8};
    settings::Prop<std::int32_t, true, true> cardsTitleSpace{"cardsTitleSpace", 2};
    settings::Prop<std::int32_t, true, true> cardsFocusLinePadding{"cardsFocusLinePadding", 2};
    settings::Prop<std::int32_t, true, false> cardsDefaultWrap{"cardsDefaultWrap", 4};
    settings::Prop<std::int32_t, false, false> seKeyCodeSashL{"seKeyCodeSashL", 4, 2012101100};
    settings::Prop<std::int32_t, false, false> seKeyCodeSashR{"seKeyCodeSashR", 7, 2012101100};
    settings::Prop<std::int32_t, false, false> effectContentSEKeyCodeSashL{"effectContentSEKeyCodeSashL", 4};
    settings::Prop<std::int32_t, false, false> effectContentSEKeyCodeSashR{"effectContentSEKeyCodeSashR", 7};
    settings::Prop<std::int32_t, false, false> talkMainSashL{"talkMainSashL", 3};
    settings::Prop<std::int32_t, false, false> talkMainSashR{"talkMainSashR", 7};
    settings::Prop<std::int32_t, false, false> talkLeftSashL{"talkLeftSashL", 4};
    settings::Prop<std::int32_t, false, false> talkLeftSashR{"talkLeftSashR", 5};
    settings::Prop<std::int32_t, true, false> msgBackR{"msgBackR", 0};
    settings::Prop<std::int32_t, true, false> msgBackG{"msgBackG", 0};
    settings::Prop<std::int32_t, true, false> msgBackB{"msgBackB", 128};
    settings::Prop<std::int32_t, true, false> msgForeR{"msgForeR", 255};
    settings::Prop<std::int32_t, true, false> msgForeG{"msgForeG", 255};
    settings::Prop<std::int32_t, true, false> msgForeB{"msgForeB", 255};
    settings::Prop<std::int32_t, true, false> textTabs{"textTabs", 4};
    settings::Prop<structs::CRGB, true, false> messageLineColor1{"messageLineColor1", structs::CRGB(0, 0, 0)};
    settings::Prop<structs::CRGB, true, false> messageLineColor2{"messageLineColor2", structs::CRGB(128, 0, 0)};
    settings::Prop<structs::CRGB, true, false> messageBackColor{"messageBackColor", structs::CRGB(0, 0, 128)};
    settings::Prop<structs::CRGB, true, false> messageForeColor{"messageForeColor", structs::CRGB(255, 255, 255)};
    settings::Prop<structs::CRGB, true, false> messageHemColor{"messageHemColor", structs::CRGB(0, 0, 0)};
    settings::Prop<std::uint32_t, false, false> gridX{"gridX", 10};
    settings::Prop<std::uint32_t, false, false> gridY{"gridY", 10};
    settings::Prop<std::uint32_t, false, false> gridRange{"gridRange", 5};
    settings::Prop<structs::CRGB, true, false> gridColor{"gridColor", structs::CRGB(64, 64, 64)};
    settings::Prop<structs::CRGB, true, false> gridHighlightColor{"gridHighlightColor", structs::CRGB(192, 192, 192)};
    settings::Prop<std::uint32_t, true, false> enhanceMaxVal{"enhanceMaxVal", 10};
    settings::Prop<std::uint32_t, true, false> enhanceHighVal{"enhanceHighVal", 7};
    settings::Prop<std::uint32_t, true, false> enhanceMiddleVal{"enhanceMiddleVal", 4};
    settings::Prop<structs::CRGB, true, false> enhanceColorMax{"enhanceColorHigh", structs::CRGB(255, 0, 0)};
    settings::Prop<structs::CRGB, true, false> enhanceColorHigh{"enhanceColorHigh", structs::CRGB(175, 0, 0)};
    settings::Prop<structs::CRGB, true, false> enhanceColorMiddle{"enhanceColorMiddle", structs::CRGB(127, 0, 0)};
    settings::Prop<structs::CRGB, true, false> enhanceColorLow{"enhanceColorLow", structs::CRGB(79, 0, 0)};
    settings::Prop<structs::CRGB, true, false> penaltyColorMax{"penaltyColorHigh", structs::CRGB(0, 0, 51)};
    settings::Prop<structs::CRGB, true, false> penaltyColorHigh{"penaltyColorHigh", structs::CRGB(0, 0, 85)};
    settings::Prop<structs::CRGB, true, false> penaltyColorMiddle{"penaltyColorMiddle", structs::CRGB(0, 0, 136)};
    settings::Prop<structs::CRGB, true, false> penaltyColorLow{"penaltyColorLow", structs::CRGB(0, 0, 187)};
    settings::Prop<std::int32_t, true, false> textCellDefaultWidth{"textCellDefaultWidth", 100};
    settings::Prop<std::int32_t, true, false> textCellDefaultHeight{"textCellDefaultHeight", 100};
    settings::Prop<lib::String, true, false> textCellDefaultFontClassic{"textCellDefaultFontClassic", "ＭＳ ゴシック"};
    settings::Prop<lib::String, true, false> textCellDefaultFont{"textCellDefaultFont", "IPA ゴシック"};
    settings::Prop<std::int32_t, true, false> textCellDefaultFontSize{"textCellDefaultFontSize", 18};
    settings::Prop<structs::CRGB, true, false> textCellDefaultColor{"textCellDefaultColor", structs::CRGB(0, 0, 0, 255)};
    settings::Prop<structs::CRGB, true, false> textCellDefaultBorderingColor{"textCellDefaultBorderingColor", structs::CRGB(255, 255, 255, 255)};
    settings::Prop<std::uint32_t, true, false> fontSizeMax{"fontSizeMax", 999};
    settings::Prop<std::uint32_t, true, false> borderingWidthMax{"borderingWidthMax", 99};
    settings::Prop<std::int32_t, true, false> colorCellDefaultWidth{"colorCellDefaultWidth", 100};
    settings::Prop<std::int32_t, true, false> colorCellDefaultHeight{"colorCellDefaultHeight", 100};
    settings::Prop<structs::CRGB, true, false> colorCellDefaultColor1{"colorCellDefaultColor1", structs::CRGB(255, 255, 255, 255)};
    settings::Prop<structs::CRGB, true, false> colorCellDefaultColor2{"colorCellDefaultColor2", structs::CRGB(255, 255, 255, 255)};
    settings::Prop<lib::String, true, false> noFileName{"noFileName", "_"};
    settings::Prop<lib::Array<std::int32_t>, false, false> contentsOrder{"contentsOrder", lib::Array<std::int32_t>{}};
    settings::Prop<bool, false, false> contentsLock{"contentsLock", false};
    settings::Prop<lib::Array<std::int32_t>, false, false> contentsWrapIndices{"contentsWrapIndices", lib::Array<std::int32_t>{4, 6, 8}};
    settings::Prop<bool, false, false> contentsAutoOpen{"contentsAutoOpen", true};
    settings::Prop<std::int32_t, false, false> contentsPutMode{"contentsPutMode", 0};
    settings::Prop<bool, false, false> contentsInsertFirst{"contentsInsertFirst", false};
    settings::Prop<bool, false, false> showContentsGroupName{"showContentsGroupName", true};
    settings::Prop<bool, false, false> showEventContentDescription{"showEventContentDescription", true};
    settings::Prop<bool, false, false> showMotionDescription{"showMotionDescription", true};
    settings::Prop<bool, false, false> contentsFloat{"contentsFloat", false};
    settings::Prop<bool, false, false> contentsAutoHide{"contentsAutoHide", false};
    settings::Prop<bool, false, false> comboListVisible{"comboListVisible", true};
    settings::Prop<bool, false, false> showCurrentValueOnTopAlways{"showCurrentValueOnTopAlways", false};
    settings::Prop<std::int32_t, true, false> showContentsBoxHeightWhenNoToolBar{"showContentsBoxHeightWhenNoToolBar", 8};
    settings::Prop<bool, false, false> showSummaryInAreaTable{"showSummaryInAreaTable", true};
    settings::Prop<bool, false, false> saveTableViewSelection{"saveTableViewSelection", true};
    settings::Prop<bool, false, false> clickIsOpenEvent{"clickIsOpenEvent", false};
    settings::Prop<bool, false, false> smoothingCard{"smoothingCard", true};
    settings::Prop<std::int32_t, true, true> ignorePathsWidth{"ignorePathsWidth", 50};
    settings::Prop<std::int32_t, true, true> menuSettingsHeight{"menuSettingsHeight", 150};
    settings::Prop<std::int32_t, true, true> settingListWidth{"settingListWidth", 150};
    settings::Prop<std::int32_t, true, true> settingListHeight{"settingListHeight", 150};
    settings::Prop<std::int32_t, false, false> importOptionMaterials{"importOptionMaterials", static_cast<std::int32_t>(types::ImportTypeReference1::NoOverwrite)};
    settings::Prop<std::int32_t, false, false> importOptionVariables{"importOptionVariables", static_cast<std::int32_t>(types::ImportTypeReference1::Rename)};
    settings::Prop<std::int32_t, false, false> importOptionCasts{"importOptionCasts", static_cast<std::int32_t>(types::ImportTypeReference2::Rename)};
    settings::Prop<std::int32_t, false, false> importOptionSkills{"importOptionSkills", static_cast<std::int32_t>(types::ImportTypeReference2::Rename)};
    settings::Prop<std::int32_t, false, false> importOptionItems{"importOptionItems", static_cast<std::int32_t>(types::ImportTypeReference2::Rename)};
    settings::Prop<std::int32_t, false, false> importOptionBeasts{"importOptionBeasts", static_cast<std::int32_t>(types::ImportTypeReference2::Rename)};
    settings::Prop<std::int32_t, false, false> importOptionInfos{"importOptionInfos", static_cast<std::int32_t>(types::ImportTypeReference2::Rename)};
    settings::Prop<std::int32_t, false, false> importOptionAreas{"importOptionAreas", static_cast<std::int32_t>(types::ImportTypeReference2::Rename)};
    settings::Prop<std::int32_t, false, false> importOptionBattles{"importOptionBattles", static_cast<std::int32_t>(types::ImportTypeReference2::Rename)};
    settings::Prop<std::int32_t, false, false> importOptionPackages{"importOptionPackages", static_cast<std::int32_t>(types::ImportTypeReference2::Rename)};
    settings::Prop<std::int32_t, false, false> importOptionIncludedFiles{"importOptionIncludedFiles", static_cast<std::int32_t>(types::ImportTypeIncluded::AsIs)};
    settings::Prop<std::int32_t, false, false> importOptionIncludedFilesWithoutIncluding{"importOptionIncludedFilesWithoutIncluding", static_cast<std::int32_t>(types::ImportTypeIncluded::Exclude)};
    settings::Prop<std::int32_t, false, false> importOptionIncludedBgImages{"importOptionIncludedBgImages", static_cast<std::int32_t>(types::ImportTypeIncluded::AsIs)};
    settings::Prop<std::int32_t, false, false> importOptionHands{"importOptionHands", static_cast<std::int32_t>(types::ImportTypeIncluded::AsIs)};
    settings::Prop<std::int32_t, false, false> importOptionBeastsInMotions{"importOptionBeastsInMotions", static_cast<std::int32_t>(types::ImportTypeIncluded::AsIs)};
    settings::Prop<bool, false, false> importOptionOverwriteScenarioInfo{"importOptionOverwriteScenarioInfo", false};
    settings::Prop<lib::String, false, false> wallpaper{"wallpaper", ""};
    settings::Prop<std::int32_t, false, false> wallpaperStyle{"wallpaperStyle", static_cast<std::int32_t>(structs::WallpaperStyle::Tile)};
    settings::Prop<std::int32_t, false, false> wallColorR{"wallColorR", 0};
    settings::Prop<std::int32_t, false, false> wallColorG{"wallColorG", 0};
    settings::Prop<std::int32_t, false, false> wallColorB{"wallColorB", 128};
    // 既定値は共有の記述子として保つ。DのPropは配列の記述子をINITとvalue
    // へコピーするため、 スキンの代替値の取得も同じ既定の要素を参照する。
    settings::Prop<lib::SharedSlice<structs::BgImageS>, false, false> bgImagesDefault{"bgImagesDefault", lib::SharedSlice<structs::BgImageS>{structs::BgImageS("MapOfWirth", 0, 0, 632, 420, false)}};
    settings::Prop<std::int32_t, false, false> bgImageSettingsSashL{"bgImageSettingsSashL", 1};
    settings::Prop<std::int32_t, false, false> bgImageSettingsSashR{"bgImageSettingsSashR", 1};
    settings::Prop<std::int32_t, false, false> selectionKeyCodeSashL{"selectionKeyCodeSashL", 2};
    settings::Prop<std::int32_t, false, false> selectionKeyCodeSashR{"selectionKeyCodeSashR", 5};
    settings::Prop<std::int32_t, false, false> bgImageSettingsKeyCodesSashL{"bgImageSettingsKeyCodesSashL", 3};
    settings::Prop<std::int32_t, false, false> bgImageSettingsKeyCodesSashR{"bgImageSettingsKeyCodesSashR", 2};
    settings::Prop<std::int32_t, false, false> keyCodesByFeaturesKeyCodesByMotionSashL{"keyCodesByFeaturesKeyCodesByMotionSashL", 3};
    settings::Prop<std::int32_t, false, false> keyCodesByFeaturesKeyCodesByMotionSashR{"keyCodesByFeaturesKeyCodesByMotionSashR", 4};
    settings::Prop<std::int32_t, false, false> bgImageSelectionSashL{"bgImageSelectionSashL", 8};
    settings::Prop<std::int32_t, false, false> bgImageSelectionSashR{"bgImageSelectionSashR", 3};
    settings::Prop<std::int32_t, false, false> outerToolsSashL{"outerToolsSashL", 1};
    settings::Prop<std::int32_t, false, false> outerToolsSashR{"outerToolsSashR", 2};
    settings::Prop<std::int32_t, false, false> outerToolShortcutSashL{"outerToolShortcutSashL", 1, 2014103100};
    settings::Prop<std::int32_t, false, false> outerToolShortcutSashR{"outerToolShortcutSashR", 4, 2014103100};
    settings::Prop<std::int32_t, false, false> classicEnginesSashL{"classicEnginesSashL", 1};
    settings::Prop<std::int32_t, false, false> classicEnginesSashR{"classicEnginesSashR", 2};
    settings::Prop<std::int32_t, false, false> classicEnginesNameTypeSashL{"classicEnginesNameTypeSashL", 4};
    settings::Prop<std::int32_t, false, false> classicEnginesNameTypeSashR{"classicEnginesNameTypeSashR", 3};
    settings::Prop<std::int32_t, false, false> classicEngineShortcutSashL{"classicEngineShortcutSashL", 1, 2014103100};
    settings::Prop<std::int32_t, false, false> classicEngineShortcutSashR{"classicEngineShortcutSashR", 4, 2014103100};
    settings::Prop<std::int32_t, false, true> featureDefaultNameWidth{"featureDefaultNameWidth", 100};
    settings::Prop<std::int32_t, false, true> featureVariantNameWidth{"featureVariantNameWidth", 100};
    settings::Prop<std::int32_t, false, true> featureManualNameWidth{"featureManualNameWidth", 100};
    settings::Prop<std::int32_t, false, false> eventTemplatesSashL{"eventTemplatesSashL", 1};
    settings::Prop<std::int32_t, false, false> eventTemplatesSashR{"eventTemplatesSashR", 2};
    settings::Prop<std::int32_t, false, false> eventTemplatesOfScenarioSashL{"eventTemplatesOfScenarioSashL", 1};
    settings::Prop<std::int32_t, false, false> eventTemplatesOfScenarioSashR{"eventTemplatesOfScenarioSashR", 2};
    settings::Prop<std::int32_t, false, false> eventTemplateShortcutSashL{"eventTemplateShortcutSashL", 1, 2014103100};
    settings::Prop<std::int32_t, false, false> eventTemplateShortcutSashR{"eventTemplateShortcutSashR", 4, 2014103100};
    settings::Prop<std::int32_t, false, false> eventTemplateOfScenarioShortcutSashL{"eventTemplateOfScenarioShortcutSashL", 1, 2014103100};
    settings::Prop<std::int32_t, false, false> eventTemplateOfScenarioShortcutSashR{"eventTemplateOfScenarioShortcutSashR", 5, 2014103100};
    settings::Prop<std::int32_t, false, false> scenarioTemplatesSashL{"scenarioTemplatesSashL", 1};
    settings::Prop<std::int32_t, false, false> scenarioTemplatesSashR{"scenarioTemplatesSashR", 2};
    settings::Prop<std::int32_t, false, false> templatesSashL{"templatesSashL", 1};
    settings::Prop<std::int32_t, false, false> templatesSashR{"templatesSashR", 1};
    settings::Prop<std::int32_t, false, false> toolsClassicEnginesSashL{"toolsClassicEnginesSashL", 1};
    settings::Prop<std::int32_t, false, false> toolsClassicEnginesSashR{"toolsClassicEnginesSashR", 1};
    settings::Prop<std::int32_t, true, true> selectionWidth{"selectionWidth", 100};
    settings::Prop<std::int32_t, true, true> keyCodeWidth{"keyCodeWidth", 100};
    settings::Prop<std::int32_t, true, true> roundWidth{"roundWidth", 80};
    settings::Prop<lib::String, false, false> scenarioPath{"scenarioPath", ""};
    settings::Prop<lib::String, false, false> tempPath{"tempPath", "temp"};
    settings::Prop<lib::String, false, false> backupPath{"backupPath", "backup"};
    settings::Prop<bool, false, false> backupEnabled{"backupEnabled", true};
    settings::Prop<std::int32_t, false, false> backupIntervalType{"backupIntervalType", 0};
    settings::Prop<std::int32_t, false, false> backupInterval{"backupInterval", 15};
    settings::Prop<std::int32_t, false, false> backupIntervalEdit{"backupIntervalEdit", 20};
    settings::Prop<std::int32_t, false, false> backupCount{"backupCount", 10};
    settings::Prop<bool, false, false> autoSave{"autoSave", false};
    settings::Prop<bool, false, false> backupRefAuthor{"backupRefAuthor", false};
    settings::Prop<bool, false, false> backupBeforeSaveEnabled{"backupBeforeSaveEnabled", true};
    settings::Prop<lib::String, false, false> backupBeforeSavePath{"backupBeforeSavePath", "backup"};
    settings::Prop<lib::String, true, false> backupBeforeSaveDir{"backupBeforeSaveDir", "files"};
    settings::Prop<bool, false, false> backupArchived{"backupArchived", true};
    settings::Prop<std::int32_t, false, false> ignoreMenuSashL{"ignoreMenuSashL", 2};
    settings::Prop<std::int32_t, false, false> ignoreMenuSashR{"ignoreMenuSashR", 1};
    settings::Prop<std::uint32_t, true, false> highValueOfImageControl{"highValueOfImageControl", 10};
    settings::Prop<bool, true, false> showDuplicateViewInToolBar{"showDuplicateViewInToolBar", false};
    settings::Prop<lib::Array<structs::OpenHistory>, false, false> openHistories{"openHistories", lib::Array<structs::OpenHistory>{}};
    settings::Prop<lib::Array<structs::OpenHistory>, false, false> scenarioBookmarks{"scenarioBookmarks", lib::Array<structs::OpenHistory>{}};
    settings::Prop<std::int32_t, false, false> historyMax{"historyMax", 10};
    settings::Prop<std::int32_t, false, false> historySnipLength{"historySnipLength", 30};
    settings::Prop<bool, true, false> removeScenarioHistoryNonExistingWithPriority{"removeScenarioHistoryNonExistingWithPriority", true};
    settings::Prop<lib::String, false, false> lastScenario{"lastScenario", ""};
    settings::Prop<lib::Array<structs::OpenHistory>, false, false> importHistory{"importHistory", lib::Array<structs::OpenHistory>{}};
    settings::Prop<lib::Array<structs::OpenHistory>, false, false> importBookmarks{"importBookmarks", lib::Array<structs::OpenHistory>{}};
    settings::Prop<std::int32_t, true, true> searchResultTableWidth{"searchResultTableWidth", 400};
    settings::Prop<std::int32_t, true, true> searchResultTableHeight{"searchResultTableHeight", 200};
    settings::Prop<lib::String, true> engine{ "engine",
#if defined(_WIN32)
        "CardWirthPy.exe"
#else
        "CardWirthPy"
#endif
    };
    settings::Prop<lib::String, true, false> engineScript{"engineScript", "cardwirth.py"};
    settings::Prop<lib::String, false, false> enginePath{"enginePath", ""};
    settings::Prop<lib::String, true, false> pythonCommand{"pythonCommand", "python"};
    settings::Prop<structs::ExecutionParty, false, false> lastExecutedParty{"lastExecutedParty", structs::ExecutionParty{}};
    settings::Prop<lib::Array<structs::ExecutionParty>, false, false> executedParties{"executedParties", lib::Array<structs::ExecutionParty>{}};
    settings::Prop<lib::Array<structs::ExecutionParty>, false, false> executedPartyBookmarks{"executedPartyBookmarks", lib::Array<structs::ExecutionParty>{}};
    settings::Prop<std::uint32_t, false, false> executedPartiesMax{"executedPartiesMax", 10};
    settings::Prop<bool, true, false> removePartyHistoryNonExistingWithPriority{"removePartyHistoryNonExistingWithPriority", true};
    settings::Prop<lib::String, true, false> dataDir{"dataDir", "Data"};
    settings::Prop<bool, false, false> findEnginePath{"findEnginePath", true};
    settings::Prop<lib::String, true, false> defaultSkin{"defaultSkin", "MedievalFantasy"};
    settings::Prop<lib::String, true, false> defaultSkinName{"defaultSkinName", "Classic"};
    settings::Prop<lib::String, false, false> defaultAuthor{"defaultAuthor", ""};
    settings::Prop<lib::String, false, false> lastSkinType{"lastSkinType", ""};
    settings::Prop<lib::String, false, false> lastSkinName{"lastSkinName", ""};
    settings::Prop<bool, false, false> saveSkinName{"saveSkinName", true};
    settings::Prop<bool, false, false> replaceClassicResourceExtension{"replaceClassicResourceExtension", true};
    settings::Prop<lib::String, true, false> tableExtension{"tableExtension", ".bmp"};
    settings::Prop<lib::String, true, false> midiExtension{"midiExtension", ".mid"};
    settings::Prop<lib::String, true, false> waveExtension{"waveExtension", ".wav"};
    settings::Prop<lib::String, false, false> classicEngineRegex{"classicEngineRegex", "^(CW´|.+Wirth(_.+|Next)?)\\.exe$"};
    settings::Prop<lib::String, false, false> classicDataDirRegex{"classicDataDirRegex", "^Data|D_[A-Z]1|[A-Z]_dt$"};
    settings::Prop<lib::String, true, false> classicMatchKey{"classicMatchKey", lib::String("Table") + lib::String(1, std::filesystem::path::preferred_separator) + "MapOfWirth.BMP"};
    settings::Prop<bool, false, false> expandXMLs{"expandXMLs", false};
    settings::Prop<bool, false, false> xmlCopy{"xmlCopy", false};
    settings::Prop<bool, false, false> showSpNature{"showSpNature", false};
    settings::Prop<bool, false, false> saveChangedOnlyWsn{"saveChangedOnlyWsn", true};
    settings::Prop<bool, false, false> saveChangedOnly{"saveChangedOnly", false};
    settings::Prop<bool, false, false> xmlFileNameIsIDOnly{"xmlFileNameIsIDOnly", false};
    settings::Prop<bool, false, false> archiveInNewThread{"archiveInNewThread", true};
    settings::Prop<bool, false, false> saveInnerImagePath{"saveInnerImagePath", false};
    settings::Prop<bool, false, false> linkCard{"linkCard", false};
    settings::Prop<bool, false, false> traceDirectories{"traceDirectories", true};
    settings::Prop<bool, false, false> logicalSort{"logicalSort", true};
    settings::Prop<bool, false, false> copyDesc{"copyDesc", false};
    settings::Prop<bool, false, false> refCardsAtEditBgImage{"refCardsAtEditBgImage", true};
    settings::Prop<bool, false, false> showImagePreview{"showImagePreview", true};
    settings::Prop<bool, false, false> maskCardImagePreview{"maskCardImagePreview", true};
    settings::Prop<bool, false, false> ignoreBackgroundInRange{"ignoreBackgroundInRange", true};
    settings::Prop<bool, false, false> classicStyleTree{"classicStyleTree", false};
    settings::Prop<bool, false, false> adjustContentName{"adjustContentName", true};
    settings::Prop<bool, false, false> showHandMark{"showHandMark", true};
    settings::Prop<bool, false, false> showHandMarkAlways{"showHandMarkAlways", true};
    settings::Prop<bool, false, false> showEventTreeMark{"showEventTreeMark", true};
    settings::Prop<bool, false, false> showEventTreeMarkAlways{"showEventTreeMarkAlways", true};
    settings::Prop<bool, false, false> ignoreEmptyStart{"ignoreEmptyStart", true};
    settings::Prop<bool, false, false> showSkillCardLevel{"showSkillCardLevel", true};
    settings::Prop<bool, false, false> switchTabWheel{"switchTabWheel", true};
    settings::Prop<bool, false, false> closeTabWithMiddleClick{"closeTabWithMiddleClick", true};
    settings::Prop<bool, false, false> openTabAtRightOfCurrentTab{"openTabAtRightOfCurrentTab", true};
    settings::Prop<bool, false, false> showCloseButtonAllTab{"showCloseButtonAllTab", false};
    settings::Prop<bool, false, false> cautionBeforeReplace{"cautionBeforeReplace", false};
    settings::Prop<bool, false, false> startIncrementalSearchWhenKeyDown{"startIncrementalSearchWhenKeyDown", true};
    settings::Prop<bool, false, false> radarStyleParams{"radarStyleParams", true};
    settings::Prop<bool, false, false> showAptitudeOnCastCardEditor{"showAptitudeOnCastCardEditor", true};
    settings::Prop<bool, false, false> showCardListHeader{"showCardListHeader", true};
    settings::Prop<bool, false, false> showCardListTitle{"showCardListTitle", true};
    settings::Prop<bool, false, false> applyDialogsBeforeSave{"applyDialogsBeforeSave", true};
    settings::Prop<bool, false, false> useNamesAfterStandard{"useNamesAfterStandard", false};
    settings::Prop<bool, false, false> expandChooserItems{"expandChooserItems", true};
    settings::Prop<bool, false, false> restorePositionOfEventTreeView{"restorePositionOfEventTreeView", true};
    settings::Prop<bool, false, false> selectVariableWithTree{"selectVariableWithTree", true};
    settings::Prop<bool, false, false> useCurrentStartName{"useCurrentStartName", true};
    settings::Prop<bool, false, false> autoUpdateJpy1File{"autoUpdateJpy1File", false};
    settings::Prop<bool, false, false> straightEventTreeView{"straightEventTreeView", true};
    settings::Prop<std::int32_t, false, false> eventTreeSlope{"eventTreeSlope", 16};
    settings::Prop<bool, false, false> forceIndentBranchContent{"forceIndentBranchContent", true};
    settings::Prop<bool, false, false> showTerminalMark{"showTerminalMark", true};
    settings::Prop<bool, false, false> showTargetStartLineNumber{"showTargetStartLineNumber", true};
    settings::Prop<bool, false, false> clickIconIsStartEdit{"clickIconIsStartEdit", false};
    settings::Prop<bool, false, false> showAreaDirTree{"showAreaDirTree", true};
    settings::Prop<bool, false, false> incrementNewAreaName{"incrementNewAreaName", true};
    settings::Prop<bool, false, false> saveNeedChanged{"saveNeedChanged", true};
    settings::Prop<bool, false, false> canVanishWorkAreaInMainWindow{"canVanishWorkAreaInMainWindow", false};
    settings::Prop<bool, false, false> switchFixedWithCheckBox{"switchFixedWithCheckBox", true};
    settings::Prop<bool, false, false> resizingImageWithRatio{"resizingImageWithRatio", true};
    settings::Prop<bool, false, false> drawXORSelectionLine{"drawXORSelectionLine", false};
    settings::Prop<bool, false, false> showSceneViewSelectionFilter{"showSceneViewSelectionFilter", true};
    settings::Prop<bool, false, false> showItemNumberOfSceneAndEventView{"showItemNumberOfSceneAndEventView", true};
    settings::Prop<bool, false, false> showCardStatusUnderPointer{"showCardStatusUnderPointer", true};
    settings::Prop<std::int32_t, false, false> showStatusTime{"showStatusTime", static_cast<std::int32_t>(structs::ShowStatusTime::Always)};
    settings::Prop<bool, false, false> showEventTreeLineNumber{"showEventTreeLineNumber", true};
    settings::Prop<bool, false, false> showEventTreeDetail{"showEventTreeDetail", true};
    settings::Prop<std::int32_t, false, false> editTriggerType{"editTriggerType", static_cast<std::int32_t>(structs::EditTrigger::Slow)};
    settings::Prop<bool, false, false> specifySelectedEventTree{"specifySelectedEventTree", true};
    settings::Prop<bool, false, false> spinnerUpDownWithWheel{"spinnerUpDownWithWheel", true};
    settings::Prop<std::int32_t, false, false> soundPlayType{"soundPlayType", 0};
    settings::Prop<std::int32_t, false, false> soundEffectPlayType{"soundEffectPlayType", -1};
    settings::Prop<std::int32_t, false, false> bgmVolume{"bgmVolume", 100};
    settings::Prop<std::int32_t, false, false> seVolume{"seVolume", 100};
    settings::Prop<lib::String, true, false> cardWirthPyDefaultSoundFont{"cardWirthPyDefaultSoundFont", "Data/SoundFont/005.6mg_Aspirin_Stereo_V1.2_Bank.sf2"};
    settings::Prop<std::uint32_t, false, false> loopCountMax{"loopCountMax", 100, true};
    settings::Prop<std::uint32_t, false, false> fadeInMax{"fadeInMax", 1000, true};
    settings::Prop<bool, false, false> excludeCardSizeImage{"excludeCardSizeImage", true};
    settings::Prop<std::uint32_t, false, false> selectionColumnsMax{"selectionColumnsMax", 4, true};
    settings::Prop<bool, true, false> showInheritBackground{"showInheritBackground", true};
    settings::Prop<std::int32_t, false, true> detailAreaWidth{"detailAreaWidth", 200};
    settings::Prop<std::int32_t, false, false> searchPlan{"searchPlan", 0};
    settings::Prop<std::int32_t, false, false> searchIDKind{"searchIDKind", 0};
    settings::Prop<lib::Array<lib::String>, false, false> searchHistories{"searchHistories", lib::Array<lib::String>{}};
    settings::Prop<lib::Array<lib::String>, false, false> replaceHistories{"replaceHistories", lib::Array<lib::String>{}};
    settings::Prop<std::int32_t, false, false> searchHistoryMax{"searchHistoryMax", 50};
    settings::Prop<std::int32_t, false, false> replaceRangeSashL{"replaceRangeSashL", 3, 2012090100};
    settings::Prop<std::int32_t, false, false> replaceRangeSashR{"replaceRangeSashR", 1, 2012090100};
    settings::Prop<bool, false, false> replaceTextNotIgnoreCase{"replaceTextNotIgnoreCase", false};
    settings::Prop<bool, false, false> replaceTextRegExp{"replaceTextRegExp", false};
    settings::Prop<bool, false, false> replaceTextWildcard{"replaceTextWildcard", false};
    settings::Prop<bool, false, false> replaceTextExactMatch{"replaceTextExactMatch", false};
    settings::Prop<bool, false, false> replaceTextIgnoreReturnCode{"replaceTextIgnoreReturnCode", false};
    settings::Prop<lib::String, false, false> grepDir{"grepDir", ""};
    settings::Prop<lib::Array<lib::String>, false, false> grepDirHistories{"grepDirHistories", lib::Array<lib::String>{}};
    settings::Prop<bool, false, false> grepSubDir{"grepSubDir", true};
    settings::Prop<std::int32_t, true, false> searchResultRefreshCount{"searchResultRefreshCount", 100};
    settings::Prop<bool, false, false> searchResultRealtime{"searchResultRealtime", false};
    settings::Prop<bool, true, false> showRouteOfSearchResult{"showRouteOfSearchResult", true};
    settings::Prop<lib::String, false, false> searchErrorTargetVersion{"searchErrorTargetVersion", ""};
    settings::Prop<bool, false, false> replaceTextSummary{"replaceTextSummary", true};
    settings::Prop<bool, false, false> replaceTextScenario{"replaceTextScenario", false};
    settings::Prop<bool, false, false> replaceTextAuthor{"replaceTextAuthor", false};
    settings::Prop<bool, false, false> replaceTextMessage{"replaceTextMessage", true};
    settings::Prop<bool, false, false> replaceTextCardName{"replaceTextCardName", true};
    settings::Prop<bool, false, false> replaceTextCardDescription{"replaceTextCardDescription", true};
    settings::Prop<bool, false, false> replaceTextEventText{"replaceTextEventText", true};
    settings::Prop<bool, false, false> replaceTextStart{"replaceTextStart", true};
    settings::Prop<bool, false, false> replaceTextVariableName{"replaceTextVariableName", true};
    settings::Prop<bool, false, false> replaceTextVariableValue{"replaceTextVariableValue", true};
    settings::Prop<bool, false, false> replaceTextExpression{"replaceTextExpression", true};
    settings::Prop<bool, false, false> replaceTextCoupon{"replaceTextCoupon", true};
    settings::Prop<bool, false, false> replaceTextGossip{"replaceTextGossip", true};
    settings::Prop<bool, false, false> replaceTextEndScenario{"replaceTextEndScenario", true};
    settings::Prop<bool, false, false> replaceTextAreaName{"replaceTextAreaName", true};
    settings::Prop<bool, false, false> replaceTextKeyCode{"replaceTextKeyCode", true};
    settings::Prop<bool, false, false> replaceTextCellName{"replaceTextCellName", true};
    settings::Prop<bool, false, false> replaceTextCardGroup{"replaceTextCardGroup", true};
    settings::Prop<bool, false, false> replaceTextFile{"replaceTextFile", false};
    settings::Prop<bool, false, false> replaceTextComment{"replaceTextComment", true};
    settings::Prop<bool, false, false> replaceTextJptx{"replaceTextJptx", false};
    settings::Prop<bool, false, false> replaceTextTextFile{"replaceTextTextFile", false};
    settings::Prop<bool> searchContentsStart{"searchContentsStart", false};
    settings::Prop<bool> searchContentsStartBattle{"searchContentsStartBattle", false};
    settings::Prop<bool> searchContentsEnd{"searchContentsEnd", false};
    settings::Prop<bool> searchContentsEndBadEnd{"searchContentsEndBadEnd", false};
    settings::Prop<bool> searchContentsChangeArea{"searchContentsChangeArea", false};
    settings::Prop<bool> searchContentsChangeBgImage{"searchContentsChangeBgImage", false};
    settings::Prop<bool> searchContentsEffect{"searchContentsEffect", false};
    settings::Prop<bool> searchContentsEffectBreak{"searchContentsEffectBreak", false};
    settings::Prop<bool> searchContentsLinkStart{"searchContentsLinkStart", false};
    settings::Prop<bool> searchContentsLinkPackage{"searchContentsLinkPackage", false};
    settings::Prop<bool> searchContentsTalkMessage{"searchContentsTalkMessage", false};
    settings::Prop<bool> searchContentsTalkDialog{"searchContentsTalkDialog", false};
    settings::Prop<bool> searchContentsPlayBgm{"searchContentsPlayBgm", false};
    settings::Prop<bool> searchContentsPlaySound{"searchContentsPlaySound", false};
    settings::Prop<bool> searchContentsWait{"searchContentsWait", false};
    settings::Prop<bool> searchContentsElapseTime{"searchContentsElapseTime", false};
    settings::Prop<bool> searchContentsCallStart{"searchContentsCallStart", false};
    settings::Prop<bool> searchContentsCallPackage{"searchContentsCallPackage", false};
    settings::Prop<bool> searchContentsBranchFlag{"searchContentsBranchFlag", false};
    settings::Prop<bool> searchContentsBranchMultiStep{"searchContentsBranchMultiStep", false};
    settings::Prop<bool> searchContentsBranchStep{"searchContentsBranchStep", false};
    settings::Prop<bool> searchContentsBranchSelect{"searchContentsBranchSelect", false};
    settings::Prop<bool> searchContentsBranchAbility{"searchContentsBranchAbility", false};
    settings::Prop<bool> searchContentsBranchRandom{"searchContentsBranchRandom", false};
    settings::Prop<bool> searchContentsBranchLevel{"searchContentsBranchLevel", false};
    settings::Prop<bool> searchContentsBranchStatus{"searchContentsBranchStatus", false};
    settings::Prop<bool> searchContentsBranchPartyNumber{"searchContentsBranchPartyNumber", false};
    settings::Prop<bool> searchContentsBranchArea{"searchContentsBranchArea", false};
    settings::Prop<bool> searchContentsBranchBattle{"searchContentsBranchBattle", false};
    settings::Prop<bool> searchContentsBranchIsBattle{"searchContentsBranchIsBattle", false};
    settings::Prop<bool> searchContentsBranchCast{"searchContentsBranchCast", false};
    settings::Prop<bool> searchContentsBranchItem{"searchContentsBranchItem", false};
    settings::Prop<bool> searchContentsBranchSkill{"searchContentsBranchSkill", false};
    settings::Prop<bool> searchContentsBranchInfo{"searchContentsBranchInfo", false};
    settings::Prop<bool> searchContentsBranchBeast{"searchContentsBranchBeast", false};
    settings::Prop<bool> searchContentsBranchMoney{"searchContentsBranchMoney", false};
    settings::Prop<bool> searchContentsBranchCoupon{"searchContentsBranchCoupon", false};
    settings::Prop<bool> searchContentsBranchCompleteStamp{"searchContentsBranchCompleteStamp", false};
    settings::Prop<bool> searchContentsBranchGossip{"searchContentsBranchGossip", false};
    settings::Prop<bool> searchContentsSetFlag{"searchContentsSetFlag", false};
    settings::Prop<bool> searchContentsSetStep{"searchContentsSetStep", false};
    settings::Prop<bool> searchContentsSetStepUp{"searchContentsSetStepUp", false};
    settings::Prop<bool> searchContentsSetStepDown{"searchContentsSetStepDown", false};
    settings::Prop<bool> searchContentsReverseFlag{"searchContentsReverseFlag", false};
    settings::Prop<bool> searchContentsCheckFlag{"searchContentsCheckFlag", false};
    settings::Prop<bool> searchContentsGetCast{"searchContentsGetCast", false};
    settings::Prop<bool> searchContentsGetItem{"searchContentsGetItem", false};
    settings::Prop<bool> searchContentsGetSkill{"searchContentsGetSkill", false};
    settings::Prop<bool> searchContentsGetInfo{"searchContentsGetInfo", false};
    settings::Prop<bool> searchContentsGetBeast{"searchContentsGetBeast", false};
    settings::Prop<bool> searchContentsGetMoney{"searchContentsGetMoney", false};
    settings::Prop<bool> searchContentsGetCoupon{"searchContentsGetCoupon", false};
    settings::Prop<bool> searchContentsGetCompleteStamp{"searchContentsGetCompleteStamp", false};
    settings::Prop<bool> searchContentsGetGossip{"searchContentsGetGossip", false};
    settings::Prop<bool> searchContentsLoseCast{"searchContentsLoseCast", false};
    settings::Prop<bool> searchContentsLoseItem{"searchContentsLoseItem", false};
    settings::Prop<bool> searchContentsLoseSkill{"searchContentsLoseSkill", false};
    settings::Prop<bool> searchContentsLoseInfo{"searchContentsLoseInfo", false};
    settings::Prop<bool> searchContentsLoseBeast{"searchContentsLoseBeast", false};
    settings::Prop<bool> searchContentsLoseMoney{"searchContentsLoseMoney", false};
    settings::Prop<bool> searchContentsLoseCoupon{"searchContentsLoseCoupon", false};
    settings::Prop<bool> searchContentsLoseCompleteStamp{"searchContentsLoseCompleteStamp", false};
    settings::Prop<bool> searchContentsLoseGossip{"searchContentsLoseGossip", false};
    settings::Prop<bool> searchContentsShowParty{"searchContentsShowParty", false};
    settings::Prop<bool> searchContentsHideParty{"searchContentsHideParty", false};
    settings::Prop<bool> searchContentsRedisplay{"searchContentsRedisplay", false};
    settings::Prop<bool> searchContentsSubstituteStep{"searchContentsSubstituteStep", false};
    settings::Prop<bool> searchContentsSubstituteFlag{"searchContentsSubstituteFlag", false};
    settings::Prop<bool> searchContentsBranchStepCmp{"searchContentsBranchStepCmp", false};
    settings::Prop<bool> searchContentsBranchFlagCmp{"searchContentsBranchFlagCmp", false};
    settings::Prop<bool> searchContentsBranchRandomSelect{"searchContentsBranchRandomSelect", false};
    settings::Prop<bool> searchContentsBranchKeyCode{"searchContentsBranchKeyCode", false};
    settings::Prop<bool> searchContentsCheckStep{"searchContentsCheckStep", false};
    settings::Prop<bool> searchContentsBranchRound{"searchContentsBranchRound", false};
    settings::Prop<bool> searchContentsMoveBgImage{"searchContentsMoveBgImage", false};
    settings::Prop<bool> searchContentsReplaceBgImage{"searchContentsReplaceBgImage", false};
    settings::Prop<bool> searchContentsLoseBgImage{"searchContentsLoseBgImage", false};
    settings::Prop<bool> searchContentsBranchMultiCoupon{"searchContentsBranchMultiCoupon", false};
    settings::Prop<bool> searchContentsBranchMultiRandom{"searchContentsBranchMultiRandom", false};
    settings::Prop<bool> searchContentsMoveCard{"searchContentsMoveCard", false};
    settings::Prop<bool> searchContentsChangeEnvironment{"searchContentsChangeEnvironment", false};
    settings::Prop<bool> searchContentsBranchVariant{"searchContentsBranchVariant", false};
    settings::Prop<bool> searchContentsSetVariant{"searchContentsSetVariant", false};
    settings::Prop<bool> searchContentsCheckVariant{"searchContentsCheckVariant", false};
    settings::Prop<bool> searchContentsCustomXmlContent{"searchContentsCustomXmlContent", false};
    settings::Prop<lib::Array<lib::String>, false, false> plainTextFileExtensions{"plainTextFileExtensions", lib::Array<lib::String>{".txt", ".ini", ".sli"}};
    settings::Prop<bool, false, false> replaceNameCoupon{"replaceNameCoupon", true};
    settings::Prop<bool, false, false> replaceNameGossip{"replaceNameGossip", true};
    settings::Prop<bool, false, false> replaceNameEndScenario{"replaceNameEndScenario", true};
    settings::Prop<bool, false, false> replaceNameKeyCode{"replaceNameKeyCode", true};
    settings::Prop<bool, false, false> replaceNameCellName{"replaceNameCellName", true};
    settings::Prop<bool, false, false> replaceNameCardGroup{"replaceNameCardGroup", true};
    settings::Prop<bool, false, false> searchUnusedFlag{"searchUnusedFlag", true};
    settings::Prop<bool, false, false> searchUnusedStep{"searchUnusedStep", true};
    settings::Prop<bool, false, false> searchUnusedVariant{"searchUnusedVariant", true};
    settings::Prop<bool, false, false> searchUnusedArea{"searchUnusedArea", true};
    settings::Prop<bool, false, false> searchUnusedBattle{"searchUnusedBattle", true};
    settings::Prop<bool, false, false> searchUnusedPackage{"searchUnusedPackage", true};
    settings::Prop<bool, false, false> searchUnusedCast{"searchUnusedCast", true};
    settings::Prop<bool, false, false> searchUnusedSkill{"searchUnusedSkill", true};
    settings::Prop<bool, false, false> searchUnusedItem{"searchUnusedItem", true};
    settings::Prop<bool, false, false> searchUnusedBeast{"searchUnusedBeast", true};
    settings::Prop<bool, false, false> searchUnusedInfo{"searchUnusedInfo", true};
    settings::Prop<bool, false, false> searchUnusedStart{"searchUnusedStart", true};
    settings::Prop<bool, false, false> searchUnusedPath{"searchUnusedPath", true};
    settings::Prop<bool, false, false> searchOpenDialog{"searchOpenDialog", false};
    settings::Prop<std::int32_t, false, true> searchResultColumnMain{"searchResultColumnMain", 400};
    settings::Prop<std::int32_t, false, true> searchResultColumnParent{"searchResultColumnParent", 200};
    settings::Prop<std::int32_t, false, true> searchResultColumnCouponCount{"searchResultColumnCouponCount", 60};
    settings::Prop<std::int32_t, false, true> searchResultColumnErrorDesc{"searchResultColumnErrorDesc", 500};
    settings::Prop<std::int32_t, false, true> searchResultColumnScenario{"searchResultColumnScenario", 500};
    settings::Prop<std::int32_t, false, false> incrementalSearchType{"incrementalSearchType", 0};
    settings::Prop<bool, true, false> usedCouponToCombo{"usedCouponToCombo", true};
    settings::Prop<std::uint32_t, true, false> numberStepToVariantThreshold{"numberStepToVariantThreshold", 100};
    settings::Prop<lib::String, true, false> newScenarioName{"newScenarioName", "新規シナリオ"};
    settings::Prop<lib::String, false, false> newAreaName{"newAreaName", "開始エリア"};
    settings::Prop<lib::Array<lib::String>, false, false> flagTrues{"flagTrues", lib::Array<lib::String>{"TRUE", "表示", "ON", "有", "可", "済み"}, true};
    settings::Prop<lib::Array<lib::String>, false, false> flagFalses{"flagFalses", lib::Array<lib::String>{"FALSE", "非表示", "OFF", "無", "不可", "まだ"}, true};
    settings::Prop<lib::SharedSlice<structs::BgImageSetting>, false, false> bgImageSettings{"bgImageSettings", lib::SharedSlice<structs::BgImageSetting>{
		structs::BgImageSetting("冒険者の宿", 116, 15, 400, 260, false, types::LAYER_BACK_CELL),
		structs::BgImageSetting("冒険者の宿(フレーム)", 116, 14, 400, 261, true, types::LAYER_BACK_CELL),
		structs::BgImageSetting("フル", 0, 0, 632, 420, false, types::LAYER_BACK_CELL),
		structs::BgImageSetting("フル(マスク)", 0, 0, 632, 420, true, types::LAYER_BACK_CELL),
		structs::BgImageSetting("カード", 0, 0, 74, 94, true, types::LAYER_BACK_CELL),
		structs::BgImageSetting("冒険者カード", 0, 0, 95, 130, false, types::LAYER_BACK_CELL),
		structs::BgImageSetting("ゲームオーバー", 116, 55, 400, 260, false, types::LAYER_BACK_CELL),
		structs::BgImageSetting("Qubes 地面", 160, 80, 320, 160, true, types::LAYER_BACK_CELL),
		structs::BgImageSetting("Qubes 左後", 80, 0, 240, 160, true, types::LAYER_BACK_CELL),
		structs::BgImageSetting("Qubes 右後", 320, 0, 240, 160, true, types::LAYER_BACK_CELL),
		structs::BgImageSetting("Qubes 左前", 80, 80, 240, 200, true, types::LAYER_BACK_CELL),
		structs::BgImageSetting("Qubes 右前", 320, 80, 240, 200, true, types::LAYER_BACK_CELL)
	}};
    settings::Prop<lib::Array<lib::String>, false, false> standardCoupons{"standardCoupons", lib::Array<lib::String>{
		"：Ｒ", "＿１", "＿２", "＿３", "＿４", "＿５", "＿６", "＿消滅予約", "：レベル補正中"
	}, true};
    settings::Prop<lib::Array<lib::String>, false, false> standardCouponsForValued{"standardCouponsForValued", lib::Array<lib::String>{
		"＿１", "＿２", "＿３", "＿４", "＿５", "＿６"
	}, true};
    settings::Prop<lib::SharedSlice<lib::String>, false, false> standardKeyCodes{"standardKeyCodes", lib::SharedSlice<lib::String>{
		"攻撃",
		"治療",
		"魔法",
		"召喚獣",
		"気功法",
		"遠距離攻撃",
		"神聖な攻撃",
		"魔法による攻撃",
		"炎による攻撃",
		"冷気による攻撃",
		"暗殺",
		"精神を回復",
		"中毒を解除",
		"麻痺を解除",
		"眠り",
		"麻痺",
		"中毒",
		"呪縛",
		"沈黙",
		"召喚",
		"鑑定",
		"解錠",
		"呪縛を解除",
		"沈黙を解除",
		"魔法を解除",
		"",
		"魔力感知",
		"生命感知",
		"魔法の鍵",
		"解読",
		"石化",
		"石化を解除",
		"蝙蝠変化",
		"明かり",
		"目つぶし",
		"魅了",
		"透明",
		"召喚獣を付与",
		"暴露",
		"即死",
		"一撃必殺",
		"対象消去",
		"恐慌",
		"不浄な攻撃",
		"呪い",
		"飛行",
		"浮遊",
		"灯火",
		"",
		"フェイント",
		"防御",
		"逃走",
		"カード交換",
		"ペナルティ",
		"リサイクル",
		"",
		"一撃",
		"守備",
	}};
    settings::Prop<lib::SharedSlice<structs::KeyCodeByFeature>, false, false> keyCodesByFeatures{"keyCodesByFeatures", lib::SharedSlice<structs::KeyCodeByFeature>{
		structs::KeyCodeByFeature("魔法である", "魔法"),
		structs::KeyCodeByFeature("気功法である", "気功法"),
		structs::KeyCodeByFeature("遠距離攻撃である", "遠距離攻撃"),
		structs::KeyCodeByFeature("不意打ちである", "暗殺"),
		structs::KeyCodeByFeature("召喚獣である", "召喚獣"),
		structs::KeyCodeByFeature("観察・鑑定を行う", "鑑定"),
		structs::KeyCodeByFeature("解錠を行う", "解錠"),
		structs::KeyCodeByFeature("魔法を解く", "魔法を解除"),
	}};
    settings::Prop<lib::SharedSlice<structs::KeyCodeByMotion>, false, false> keyCodesByMotions{"keyCodesByMotions", lib::SharedSlice<structs::KeyCodeByMotion>{
		structs::KeyCodeByMotion(types::MType::Damage, false, types::Element::All, "攻撃"),
		structs::KeyCodeByMotion(types::MType::Absorb, false, types::Element::All, "攻撃"),
		structs::KeyCodeByMotion(types::MType::Heal, false, types::Element::All, "治療"),
		structs::KeyCodeByMotion(types::MType::VanishTarget, true, types::Element::Miracle, "神聖な攻撃"),
		structs::KeyCodeByMotion(types::MType::Damage, true, types::Element::Miracle, "魔法による攻撃"),
		structs::KeyCodeByMotion(types::MType::Absorb, true, types::Element::Miracle, "魔法による攻撃"),
		structs::KeyCodeByMotion(types::MType::Damage, true, types::Element::Magic, "魔法による攻撃"),
		structs::KeyCodeByMotion(types::MType::Absorb, true, types::Element::Magic, "魔法による攻撃"),
		structs::KeyCodeByMotion(types::MType::Damage, true, types::Element::Fire, "炎による攻撃"),
		structs::KeyCodeByMotion(types::MType::Absorb, true, types::Element::Fire, "炎による攻撃"),
		structs::KeyCodeByMotion(types::MType::Damage, true, types::Element::Ice, "冷気による攻撃"),
		structs::KeyCodeByMotion(types::MType::Absorb, true, types::Element::Ice, "冷気による攻撃"),
		structs::KeyCodeByMotion(types::MType::Normal, false, types::Element::All, "精神を回復"),
		structs::KeyCodeByMotion(types::MType::DisPoison, false, types::Element::All, "中毒を解除"),
		structs::KeyCodeByMotion(types::MType::DisParalyze, false, types::Element::All, "麻痺を解除"),
		structs::KeyCodeByMotion(types::MType::Sleep, false, types::Element::All, "眠り"),
		structs::KeyCodeByMotion(types::MType::Paralyze, false, types::Element::All, "麻痺"),
		structs::KeyCodeByMotion(types::MType::Poison, false, types::Element::All, "中毒"),
		structs::KeyCodeByMotion(types::MType::Bind, false, types::Element::All, "呪縛"),
		structs::KeyCodeByMotion(types::MType::Silence, false, types::Element::All, "沈黙"),
		structs::KeyCodeByMotion(types::MType::DisBind, false, types::Element::All, "呪縛を解除"),
		structs::KeyCodeByMotion(types::MType::DisSilence, false, types::Element::All, "沈黙を解除"),
		structs::KeyCodeByMotion(types::MType::VanishTarget, false, types::Element::All, "対象消去"),
	}};
    settings::Prop<lib::SharedSlice<lib::String>, false, false> standardSelections{"standardSelections", lib::SharedSlice<lib::String>{
		"はい",
		"いいえ",
		"キャンセル",
		"Yes",
		"No",
		"Cancel",
	}};
    settings::Prop<structs::SettingsWithSkinTypeList, false, false> settingsWithSkinTypes{"settingsWithSkinTypes", structs::SettingsWithSkinTypeList({
		structs::SettingsWithSkinType("Modern", false, false, true, { }, { }, { }, {
			"攻撃",
			"治療",
			"召喚獣",
			"遠距離攻撃",
			"魔法による攻撃",
			"炎による攻撃",
			"冷気による攻撃",
			"暗殺",
			"精神を回復",
			"中毒を解除",
			"麻痺を解除",
			"眠り",
			"麻痺",
			"中毒",
			"呪縛",
			"沈黙",
			"召喚",
			"鑑定",
			"解錠",
			"呪縛を解除",
			"沈黙を解除",
			"魔法を解除",
			"",
			"超能力",
			"探偵の心得",
			"機械",
			"故障",
			"ハッキング",
			"学問",
			"学術",
			"能力変化",
			"防御力増大",
			"抵抗力増大",
			"行動力増大",
			"回避力増大",
			"魔物殺し",
			"銃による攻撃",
			"暴露",
			"隠れる",
			"尾行",
			"窃盗",
			"カード偽造",
			"魔法を無効化",
			"精神力を回復",
			"精神力回復",
			"仮想体",
			"",
			"フェイント",
			"防御",
			"逃走",
			"カード交換",
			"ペナルティ",
			"リサイクル",
			"",
			"一撃",
			"守備",
		}, {
			structs::KeyCodeByFeature("遠距離攻撃である", "遠距離攻撃"),
			structs::KeyCodeByFeature("超能力である", "超能力"),
			structs::KeyCodeByFeature("探偵の技術である", "探偵の心得"),
			structs::KeyCodeByFeature("銃による攻撃である", "銃による攻撃"),
			structs::KeyCodeByFeature("機械を攻撃する", "故障"),
			structs::KeyCodeByFeature("機械を操作する", "機械"),
			structs::KeyCodeByFeature("コンピュータを攻撃する", "ハッキング"),
			structs::KeyCodeByFeature("対魔物の攻撃である", "魔物殺し"),
			structs::KeyCodeByFeature("学問である", "学問"),
			structs::KeyCodeByFeature("学術である", "学術"),
			structs::KeyCodeByFeature("不意打ちである", "暗殺"),
			structs::KeyCodeByFeature("観察・鑑定を行う", "鑑定"),
			structs::KeyCodeByFeature("姿を隠す", "隠れる"),
			structs::KeyCodeByFeature("尾行を行う", "尾行"),
			structs::KeyCodeByFeature("窃盗を行う", "窃盗"),
			structs::KeyCodeByFeature("カードを偽造する", "カード偽造"),
			structs::KeyCodeByFeature("召喚獣である", "召喚獣"),
			structs::KeyCodeByFeature("仮想体である", "仮想体"),
			structs::KeyCodeByFeature("防御力を強化する", "防御力増大"),
			structs::KeyCodeByFeature("抵抗力を強化する", "抵抗力増大"),
			structs::KeyCodeByFeature("行動力を強化する", "行動力増大"),
			structs::KeyCodeByFeature("回避力を強化する", "回避力増大"),
		}, {
			structs::KeyCodeByMotion(types::MType::Damage, false, types::Element::All, "攻撃"),
			structs::KeyCodeByMotion(types::MType::Absorb, false, types::Element::All, "攻撃"),
			structs::KeyCodeByMotion(types::MType::Heal, false, types::Element::All, "治療"),
			structs::KeyCodeByMotion(types::MType::Damage, true, types::Element::Miracle, "魔法による攻撃"),
			structs::KeyCodeByMotion(types::MType::Absorb, true, types::Element::Miracle, "魔法による攻撃"),
			structs::KeyCodeByMotion(types::MType::Damage, true, types::Element::Magic, "魔法による攻撃"),
			structs::KeyCodeByMotion(types::MType::Absorb, true, types::Element::Magic, "魔法による攻撃"),
			structs::KeyCodeByMotion(types::MType::Damage, true, types::Element::Fire, "炎による攻撃"),
			structs::KeyCodeByMotion(types::MType::Absorb, true, types::Element::Fire, "炎による攻撃"),
			structs::KeyCodeByMotion(types::MType::Damage, true, types::Element::Ice, "冷気による攻撃"),
			structs::KeyCodeByMotion(types::MType::Absorb, true, types::Element::Ice, "冷気による攻撃"),
			structs::KeyCodeByMotion(types::MType::Normal, false, types::Element::All, "精神を回復"),
			structs::KeyCodeByMotion(types::MType::DisPoison, false, types::Element::All, "中毒を解除"),
			structs::KeyCodeByMotion(types::MType::DisParalyze, false, types::Element::All, "麻痺を解除"),
			structs::KeyCodeByMotion(types::MType::Sleep, false, types::Element::All, "眠り"),
			structs::KeyCodeByMotion(types::MType::Paralyze, false, types::Element::All, "麻痺"),
			structs::KeyCodeByMotion(types::MType::Poison, false, types::Element::All, "中毒"),
			structs::KeyCodeByMotion(types::MType::Bind, false, types::Element::All, "呪縛"),
			structs::KeyCodeByMotion(types::MType::Silence, false, types::Element::All, "沈黙"),
			structs::KeyCodeByMotion(types::MType::DisBind, false, types::Element::All, "呪縛を解除"),
			structs::KeyCodeByMotion(types::MType::DisSilence, false, types::Element::All, "沈黙を解除"),
			structs::KeyCodeByMotion(types::MType::FaceUp, false, types::Element::All, "暴露"),
			structs::KeyCodeByMotion(types::MType::AntiMagic, false, types::Element::All, "魔法を無効化"),
			structs::KeyCodeByMotion(types::MType::GetSkillPower, false, types::Element::All, "精神力を回復"),
			structs::KeyCodeByMotion(types::MType::GetSkillPower, false, types::Element::All, "精神力回復"),
			structs::KeyCodeByMotion(types::MType::EnhanceAction, false, types::Element::All, "能力変化"),
			structs::KeyCodeByMotion(types::MType::EnhanceAvoid, false, types::Element::All, "能力変化"),
			structs::KeyCodeByMotion(types::MType::EnhanceResist, false, types::Element::All, "能力変化"),
			structs::KeyCodeByMotion(types::MType::EnhanceDefense, false, types::Element::All, "能力変化"),
		}),
		structs::SettingsWithSkinType("Monsters", true, false, true, {
			structs::BgImageS("MapOfWirth", 0, 0, 632, 420, false),
		}, {
			structs::BgImageSetting("妖魔の森", 116, 15, 400, 260, false, types::LAYER_BACK_CELL),
			structs::BgImageSetting("妖魔の森(フレーム)", 116, 14, 400, 261, true, types::LAYER_BACK_CELL),
			structs::BgImageSetting("フル", 0, 0, 632, 420, false, types::LAYER_BACK_CELL),
			structs::BgImageSetting("フル(マスク)", 0, 0, 632, 420, true, types::LAYER_BACK_CELL),
			structs::BgImageSetting("カード", 0, 0, 74, 94, true, types::LAYER_BACK_CELL),
			structs::BgImageSetting("妖魔カード", 0, 0, 95, 130, false, types::LAYER_BACK_CELL),
			structs::BgImageSetting("ゲームオーバー", 116, 55, 400, 260, false, types::LAYER_BACK_CELL),
			structs::BgImageSetting("Qubes 地面", 160, 80, 320, 160, true, types::LAYER_BACK_CELL),
			structs::BgImageSetting("Qubes 左後", 80, 0, 240, 160, true, types::LAYER_BACK_CELL),
			structs::BgImageSetting("Qubes 右後", 320, 0, 240, 160, true, types::LAYER_BACK_CELL),
			structs::BgImageSetting("Qubes 左前", 80, 80, 240, 200, true, types::LAYER_BACK_CELL),
			structs::BgImageSetting("Qubes 右前", 320, 80, 240, 200, true, types::LAYER_BACK_CELL)
		}, { }, {
			"攻撃",
			"撹乱",
			"防御",
			"体力を回復",
			"魔法",
			"召喚獣",
			"気功法",
			"遠距離攻撃",
			"神聖な攻撃",
			"魔法による攻撃",
			"音による攻撃",
			"視覚による攻撃",
			"暗殺",
			"対象消去",
			"精神を回復",
			"中毒を解除",
			"麻痺を解除",
			"眠り",
			"麻痺",
			"中毒",
			"呪縛",
			"沈黙",
			"召喚",
			"鑑定",
			"解錠",
			"人間言語",
			"呪縛を解除",
			"沈黙を解除",
			"魔法を解除",
			"",
			"誘惑",
			"盗む",
			"煙幕",
			"",
			"フェイント",
			"防御",
			"逃走",
			"カード交換",
			"ペナルティ",
			"リサイクル",
			"",
			"一撃",
			"守備",
		}, {
			structs::KeyCodeByFeature("魔法である", "魔法"),
			structs::KeyCodeByFeature("気功法である", "気功法"),
			structs::KeyCodeByFeature("遠距離攻撃である", "遠距離攻撃"),
			structs::KeyCodeByFeature("不意打ちである", "暗殺"),
			structs::KeyCodeByFeature("召喚獣である", "召喚獣"),
			structs::KeyCodeByFeature("観察・鑑定を行う", "鑑定"),
			structs::KeyCodeByFeature("解錠を行う", "解錠"),
			structs::KeyCodeByFeature("魔法を解く", "魔法を解除"),
			structs::KeyCodeByFeature("誘惑を行う", "誘惑"),
			structs::KeyCodeByFeature("人間の言語を使用する", "人間言語"),
			structs::KeyCodeByFeature("窃盗を行う", "盗む"),
			structs::KeyCodeByFeature("煙幕を張る", "煙幕"),
		}, {
			structs::KeyCodeByMotion(types::MType::Damage, false, types::Element::All, "攻撃"),
			structs::KeyCodeByMotion(types::MType::Absorb, false, types::Element::All, "攻撃"),
			structs::KeyCodeByMotion(types::MType::Heal, false, types::Element::All, "体力を回復"),
			structs::KeyCodeByMotion(types::MType::VanishTarget, true, types::Element::Miracle, "神聖な攻撃"),
			structs::KeyCodeByMotion(types::MType::Damage, true, types::Element::Miracle, "魔法による攻撃"),
			structs::KeyCodeByMotion(types::MType::Absorb, true, types::Element::Miracle, "魔法による攻撃"),
			structs::KeyCodeByMotion(types::MType::Damage, true, types::Element::Magic, "魔法による攻撃"),
			structs::KeyCodeByMotion(types::MType::Absorb, true, types::Element::Magic, "魔法による攻撃"),
			structs::KeyCodeByMotion(types::MType::Damage, true, types::Element::Fire, "音による攻撃"),
			structs::KeyCodeByMotion(types::MType::Absorb, true, types::Element::Fire, "音による攻撃"),
			structs::KeyCodeByMotion(types::MType::Damage, true, types::Element::Ice, "視覚による攻撃"),
			structs::KeyCodeByMotion(types::MType::Absorb, true, types::Element::Ice, "視覚による攻撃"),
			structs::KeyCodeByMotion(types::MType::Normal, false, types::Element::All, "精神を回復"),
			structs::KeyCodeByMotion(types::MType::DisPoison, false, types::Element::All, "中毒を解除"),
			structs::KeyCodeByMotion(types::MType::DisParalyze, false, types::Element::All, "麻痺を解除"),
			structs::KeyCodeByMotion(types::MType::Sleep, false, types::Element::All, "眠り"),
			structs::KeyCodeByMotion(types::MType::Paralyze, false, types::Element::All, "麻痺"),
			structs::KeyCodeByMotion(types::MType::Poison, false, types::Element::All, "中毒"),
			structs::KeyCodeByMotion(types::MType::Bind, false, types::Element::All, "呪縛"),
			structs::KeyCodeByMotion(types::MType::Silence, false, types::Element::All, "沈黙"),
			structs::KeyCodeByMotion(types::MType::DisBind, false, types::Element::All, "呪縛を解除"),
			structs::KeyCodeByMotion(types::MType::DisSilence, false, types::Element::All, "沈黙を解除"),
			structs::KeyCodeByMotion(types::MType::Confuse, false, types::Element::All, "撹乱"),
			structs::KeyCodeByMotion(types::MType::VanishTarget, false, types::Element::All, "対象消去"),
		}, {
			structs::ElementOverride(types::Element::Fire, "presetimage://elm_hearing.png", "音", ""),
			structs::ElementOverride(types::Element::Ice, "presetimage://elm_vision.png", "視覚", ""),
		}),
		structs::SettingsWithSkinType("Oedo", true, true, true, {
			structs::BgImageS("MapOfWirth", 0, 0, 632, 420, false),
		}, {
			structs::BgImageSetting("万長屋", 116, 15, 400, 260, false, types::LAYER_BACK_CELL),
			structs::BgImageSetting("万長屋(フレーム)", 116, 14, 400, 261, true, types::LAYER_BACK_CELL),
			structs::BgImageSetting("フル", 0, 0, 632, 420, false, types::LAYER_BACK_CELL),
			structs::BgImageSetting("フル(マスク)", 0, 0, 632, 420, true, types::LAYER_BACK_CELL),
			structs::BgImageSetting("カード", 0, 0, 74, 94, true, types::LAYER_BACK_CELL),
			structs::BgImageSetting("仕事人カード", 0, 0, 95, 130, false, types::LAYER_BACK_CELL),
			structs::BgImageSetting("ゲームオーバー", 116, 55, 400, 260, false, types::LAYER_BACK_CELL),
			structs::BgImageSetting("Qubes 地面", 160, 80, 320, 160, true, types::LAYER_BACK_CELL),
			structs::BgImageSetting("Qubes 左後", 80, 0, 240, 160, true, types::LAYER_BACK_CELL),
			structs::BgImageSetting("Qubes 右後", 320, 0, 240, 160, true, types::LAYER_BACK_CELL),
			structs::BgImageSetting("Qubes 左前", 80, 80, 240, 200, true, types::LAYER_BACK_CELL),
			structs::BgImageSetting("Qubes 右前", 320, 80, 240, 200, true, types::LAYER_BACK_CELL)
		}, {
			"是",
			"認",
			"否",
			"きゃんせる",
			"はい",
			"いいえ",
			"取り消し",
		}, {
			"攻撃",
			"撹乱",
			"防御",
			"治療",
			"魔法",
			"召喚獣",
			"気功法",
			"遠距離攻撃",
			"神聖な攻撃",
			"魔法による攻撃",
			"炎による攻撃",
			"冷気による攻撃",
			"暗殺",
			"対象消去",
			"精神を回復",
			"中毒を解除",
			"麻痺を解除",
			"眠り",
			"麻痺",
			"中毒",
			"呪縛",
			"沈黙",
			"召喚",
			"鑑定",
			"解錠",
			"呪縛を解除",
			"沈黙を解除",
			"魔法を解除",
			"明かり",
			"",
			"フェイント",
			"逃走",
			"カード交換",
			"ペナルティ",
			"リサイクル",
			"",
			"一撃",
			"守備",
		}, {
			structs::KeyCodeByFeature("魔法である", "魔法"),
			structs::KeyCodeByFeature("気功法である", "気功法"),
			structs::KeyCodeByFeature("遠距離攻撃である", "遠距離攻撃"),
			structs::KeyCodeByFeature("不意打ちである", "暗殺"),
			structs::KeyCodeByFeature("防御を行う", "防御"),
			structs::KeyCodeByFeature("召喚獣である", "召喚獣"),
			structs::KeyCodeByFeature("観察・鑑定を行う", "鑑定"),
			structs::KeyCodeByFeature("解錠を行う", "解錠"),
			structs::KeyCodeByFeature("魔法を解く", "魔法を解除"),
			structs::KeyCodeByFeature("光源になる", "明かり"),
		}, {
			structs::KeyCodeByMotion(types::MType::Damage, false, types::Element::All, "攻撃"),
			structs::KeyCodeByMotion(types::MType::Absorb, false, types::Element::All, "攻撃"),
			structs::KeyCodeByMotion(types::MType::Confuse, false, types::Element::All, "撹乱"),
			structs::KeyCodeByMotion(types::MType::Heal, false, types::Element::All, "治療"),
			structs::KeyCodeByMotion(types::MType::VanishTarget, true, types::Element::Miracle, "神聖な攻撃"),
			structs::KeyCodeByMotion(types::MType::Damage, true, types::Element::Miracle, "魔法による攻撃"),
			structs::KeyCodeByMotion(types::MType::Absorb, true, types::Element::Miracle, "魔法による攻撃"),
			structs::KeyCodeByMotion(types::MType::Damage, true, types::Element::Magic, "魔法による攻撃"),
			structs::KeyCodeByMotion(types::MType::Absorb, true, types::Element::Magic, "魔法による攻撃"),
			structs::KeyCodeByMotion(types::MType::Damage, true, types::Element::Fire, "炎による攻撃"),
			structs::KeyCodeByMotion(types::MType::Absorb, true, types::Element::Fire, "炎による攻撃"),
			structs::KeyCodeByMotion(types::MType::Damage, true, types::Element::Ice, "冷気による攻撃"),
			structs::KeyCodeByMotion(types::MType::Absorb, true, types::Element::Ice, "冷気による攻撃"),
			structs::KeyCodeByMotion(types::MType::Normal, false, types::Element::All, "精神を回復"),
			structs::KeyCodeByMotion(types::MType::DisPoison, false, types::Element::All, "中毒を解除"),
			structs::KeyCodeByMotion(types::MType::DisParalyze, false, types::Element::All, "麻痺を解除"),
			structs::KeyCodeByMotion(types::MType::Sleep, false, types::Element::All, "眠り"),
			structs::KeyCodeByMotion(types::MType::Paralyze, false, types::Element::All, "麻痺"),
			structs::KeyCodeByMotion(types::MType::Poison, false, types::Element::All, "中毒"),
			structs::KeyCodeByMotion(types::MType::Bind, false, types::Element::All, "呪縛"),
			structs::KeyCodeByMotion(types::MType::Silence, false, types::Element::All, "沈黙"),
			structs::KeyCodeByMotion(types::MType::DisBind, false, types::Element::All, "呪縛を解除"),
			structs::KeyCodeByMotion(types::MType::DisSilence, false, types::Element::All, "沈黙を解除"),
			structs::KeyCodeByMotion(types::MType::VanishTarget, false, types::Element::All, "対象消去"),
		}),
		structs::SettingsWithSkinType("School", false, false, true, { }, { }, { }, {
			"攻撃",
			"撹乱",
			"防御",
			"治療",
			"魔法",
			"召喚獣",
			"気功法",
			"遠距離攻撃",
			"神聖な攻撃",
			"魔法による攻撃",
			"炎による攻撃",
			"冷気による攻撃",
			"水による攻撃",
			"雷による攻撃",
			"暗殺",
			"対象消去",
			"精神を回復",
			"中毒を解除",
			"麻痺を解除",
			"眠り",
			"麻痺",
			"中毒",
			"呪縛",
			"沈黙",
			"召喚",
			"鑑定",
			"解錠",
			"呪縛を解除",
			"沈黙を解除",
			"魔法を解除",
			"飛行",
			"跳躍",
			"魔力感知",
			"生命感知",
			"解読",
			"話術",
			"挑発",
			"魅了",
			"盗む",
			"明かり",
			"食料",
			"",
			"フェイント",
			"逃走",
			"カード交換",
			"ペナルティ",
			"リサイクル",
			"",
			"一撃",
			"守備",
		}, {
			structs::KeyCodeByFeature("魔法である", "魔法"),
			structs::KeyCodeByFeature("気功法である", "気功法"),
			structs::KeyCodeByFeature("遠距離攻撃である", "遠距離攻撃"),
			structs::KeyCodeByFeature("水による攻撃である", "水による攻撃"),
			structs::KeyCodeByFeature("雷による攻撃である", "雷による攻撃"),
			structs::KeyCodeByFeature("不意打ちである", "暗殺"),
			structs::KeyCodeByFeature("防御を行う", "防御"),
			structs::KeyCodeByFeature("召喚獣である", "召喚獣"),
			structs::KeyCodeByFeature("観察・鑑定を行う", "鑑定"),
			structs::KeyCodeByFeature("解錠を行う", "解錠"),
			structs::KeyCodeByFeature("魔法を解く", "魔法を解除"),
			structs::KeyCodeByFeature("飛行能力を得る", "飛行"),
			structs::KeyCodeByFeature("跳躍する", "跳躍"),
			structs::KeyCodeByFeature("魔力を感知する", "魔力感知"),
			structs::KeyCodeByFeature("生命を感知する", "生命感知"),
			structs::KeyCodeByFeature("解読を行う", "解読"),
			structs::KeyCodeByFeature("話術である", "話術"),
			structs::KeyCodeByFeature("挑発を行う", "挑発"),
			structs::KeyCodeByFeature("魅了する", "魅了"),
			structs::KeyCodeByFeature("盗みを行う", "盗む"),
			structs::KeyCodeByFeature("光源になる", "明かり"),
			structs::KeyCodeByFeature("食料品になる", "食料"),
		}, {
			structs::KeyCodeByMotion(types::MType::Damage, false, types::Element::All, "攻撃"),
			structs::KeyCodeByMotion(types::MType::Absorb, false, types::Element::All, "攻撃"),
			structs::KeyCodeByMotion(types::MType::Confuse, false, types::Element::All, "撹乱"),
			structs::KeyCodeByMotion(types::MType::Heal, false, types::Element::All, "治療"),
			structs::KeyCodeByMotion(types::MType::VanishTarget, true, types::Element::Miracle, "神聖な攻撃"),
			structs::KeyCodeByMotion(types::MType::Damage, true, types::Element::Miracle, "魔法による攻撃"),
			structs::KeyCodeByMotion(types::MType::Absorb, true, types::Element::Miracle, "魔法による攻撃"),
			structs::KeyCodeByMotion(types::MType::Damage, true, types::Element::Magic, "魔法による攻撃"),
			structs::KeyCodeByMotion(types::MType::Absorb, true, types::Element::Magic, "魔法による攻撃"),
			structs::KeyCodeByMotion(types::MType::Damage, true, types::Element::Fire, "炎による攻撃"),
			structs::KeyCodeByMotion(types::MType::Absorb, true, types::Element::Fire, "炎による攻撃"),
			structs::KeyCodeByMotion(types::MType::Damage, true, types::Element::Ice, "冷気による攻撃"),
			structs::KeyCodeByMotion(types::MType::Absorb, true, types::Element::Ice, "冷気による攻撃"),
			structs::KeyCodeByMotion(types::MType::Normal, false, types::Element::All, "精神を回復"),
			structs::KeyCodeByMotion(types::MType::DisPoison, false, types::Element::All, "中毒を解除"),
			structs::KeyCodeByMotion(types::MType::DisParalyze, false, types::Element::All, "麻痺を解除"),
			structs::KeyCodeByMotion(types::MType::Sleep, false, types::Element::All, "眠り"),
			structs::KeyCodeByMotion(types::MType::Paralyze, false, types::Element::All, "麻痺"),
			structs::KeyCodeByMotion(types::MType::Poison, false, types::Element::All, "中毒"),
			structs::KeyCodeByMotion(types::MType::Bind, false, types::Element::All, "呪縛"),
			structs::KeyCodeByMotion(types::MType::Silence, false, types::Element::All, "沈黙"),
			structs::KeyCodeByMotion(types::MType::DisBind, false, types::Element::All, "呪縛を解除"),
			structs::KeyCodeByMotion(types::MType::DisSilence, false, types::Element::All, "沈黙を解除"),
			structs::KeyCodeByMotion(types::MType::VanishTarget, false, types::Element::All, "対象消去"),
		}),
		structs::SettingsWithSkinType("ScienceFiction", false, false, true, { }, { }, { }, {
			"攻撃",
			"手札の交換",
			"撹乱",
			"守備",
			"治療",
			"遠距離攻撃",
			"魔法による攻撃",
			"炎による攻撃",
			"冷気による攻撃",
			"暗殺",
			"対象消去",
			"精神を回復",
			"中毒を解除",
			"麻痺を解除",
			"眠り",
			"麻痺",
			"中毒",
			"呪縛",
			"沈黙",
			"鑑定",
			"解錠",
			"呪縛を解除",
			"沈黙を解除",
			"",
			"狙撃",
			"隠形",
			"擬態",
			"全体攻撃",
			"銃声",
			"異能力",
			"サイバー攻撃",
			"システム撹乱",
			"システム破壊",
			"システム解析",
			"システム掌握",
			"",
			"フェイント",
			"逃走",
			"カード交換",
			"ペナルティ",
			"リサイクル",
			"",
			"一撃",
		}, {
			structs::KeyCodeByFeature("銃声が鳴る", "銃声"),
			structs::KeyCodeByFeature("異能力である", "異能力"),
			structs::KeyCodeByFeature("遠距離攻撃である", "遠距離攻撃"),
			structs::KeyCodeByFeature("全体攻撃である", "全体攻撃"),
			structs::KeyCodeByFeature("狙い撃つ", "狙撃"),
			structs::KeyCodeByFeature("不意打ちである", "暗殺"),
			structs::KeyCodeByFeature("防御を行う", "守備"),
			structs::KeyCodeByFeature("撹乱を行う", "撹乱"),
			structs::KeyCodeByFeature("姿を隠す", "隠形"),
			structs::KeyCodeByFeature("擬態する", "擬態"),
			structs::KeyCodeByFeature("調査を行う", "調査"),
			structs::KeyCodeByFeature("鑑定を行う", "鑑定"),
			structs::KeyCodeByFeature("解錠を行う", "解錠"),
			structs::KeyCodeByFeature("システムを撹乱する", "システム撹乱"),
			structs::KeyCodeByFeature("システムを破壊する", "システム破壊"),
			structs::KeyCodeByFeature("システムを解析する", "システム解析"),
			structs::KeyCodeByFeature("システムを掌握する", "システム掌握"),
		}, {
			structs::KeyCodeByMotion(types::MType::Damage, false, types::Element::All, "攻撃"),
			structs::KeyCodeByMotion(types::MType::Absorb, false, types::Element::All, "攻撃"),
			structs::KeyCodeByMotion(types::MType::Heal, false, types::Element::All, "治療"),
			structs::KeyCodeByMotion(types::MType::Normal, false, types::Element::All, "精神を回復"),
			structs::KeyCodeByMotion(types::MType::Damage, true, types::Element::Fire, "炎による攻撃"),
			structs::KeyCodeByMotion(types::MType::Absorb, true, types::Element::Fire, "炎による攻撃"),
			structs::KeyCodeByMotion(types::MType::Damage, true, types::Element::Ice, "冷気による攻撃"),
			structs::KeyCodeByMotion(types::MType::Absorb, true, types::Element::Ice, "冷気による攻撃"),
			structs::KeyCodeByMotion(types::MType::DisPoison, false, types::Element::All, "中毒を解除"),
			structs::KeyCodeByMotion(types::MType::DisParalyze, false, types::Element::All, "麻痺を解除"),
			structs::KeyCodeByMotion(types::MType::Sleep, false, types::Element::All, "眠り"),
			structs::KeyCodeByMotion(types::MType::Paralyze, false, types::Element::All, "麻痺"),
			structs::KeyCodeByMotion(types::MType::Poison, false, types::Element::All, "中毒"),
			structs::KeyCodeByMotion(types::MType::Bind, false, types::Element::All, "呪縛"),
			structs::KeyCodeByMotion(types::MType::Silence, false, types::Element::All, "沈黙"),
			structs::KeyCodeByMotion(types::MType::DisBind, false, types::Element::All, "呪縛を解除"),
			structs::KeyCodeByMotion(types::MType::DisSilence, false, types::Element::All, "沈黙を解除"),
			structs::KeyCodeByMotion(types::MType::Damage, true, types::Element::Miracle, "サイバー攻撃"),
			structs::KeyCodeByMotion(types::MType::Absorb, true, types::Element::Miracle, "サイバー攻撃"),
			structs::KeyCodeByMotion(types::MType::Paralyze, true, types::Element::Miracle, "サイバー攻撃"),
			structs::KeyCodeByMotion(types::MType::Poison, true, types::Element::Miracle, "サイバー攻撃"),
			structs::KeyCodeByMotion(types::MType::LoseSkillPower, true, types::Element::Miracle, "サイバー攻撃"),
			structs::KeyCodeByMotion(types::MType::Sleep, true, types::Element::Miracle, "サイバー攻撃"),
			structs::KeyCodeByMotion(types::MType::Confuse, true, types::Element::Miracle, "サイバー攻撃"),
			structs::KeyCodeByMotion(types::MType::Overheat, true, types::Element::Miracle, "サイバー攻撃"),
			structs::KeyCodeByMotion(types::MType::Panic, true, types::Element::Miracle, "サイバー攻撃"),
			structs::KeyCodeByMotion(types::MType::Bind, true, types::Element::Miracle, "サイバー攻撃"),
			structs::KeyCodeByMotion(types::MType::Silence, true, types::Element::Miracle, "サイバー攻撃"),
			structs::KeyCodeByMotion(types::MType::FaceUp, true, types::Element::Miracle, "サイバー攻撃"),
			structs::KeyCodeByMotion(types::MType::VanishTarget, true, types::Element::Miracle, "サイバー攻撃"),
			structs::KeyCodeByMotion(types::MType::VanishBeast, true, types::Element::Miracle, "サイバー攻撃"),
			structs::KeyCodeByMotion(types::MType::DealConfuseCard, true, types::Element::Miracle, "サイバー攻撃"),
			structs::KeyCodeByMotion(types::MType::CancelAction, true, types::Element::Miracle, "サイバー攻撃"),
			structs::KeyCodeByMotion(types::MType::Damage, true, types::Element::Miracle, "システム破壊"),
			structs::KeyCodeByMotion(types::MType::Absorb, true, types::Element::Miracle, "システム破壊"),
			structs::KeyCodeByMotion(types::MType::Confuse, true, types::Element::Miracle, "システム撹乱"),
			structs::KeyCodeByMotion(types::MType::FaceUp, true, types::Element::Miracle, "システム解析"),
			structs::KeyCodeByMotion(types::MType::VanishTarget, false, types::Element::All, "対象消去"),
		}, {
			structs::ElementOverride(types::Element::Miracle, "presetimage://elm_electronic.png", "電子", "電子制御された存在"),
		}),
	})};
    settings::Prop<lib::Array<structs::OuterTool>> outerTools{ "outerTools",
#if defined(_WIN32)
        lib::Array<structs::OuterTool>{structs::OuterTool("メモ帳", "notepad $F", "", "", ""), structs::OuterTool("ペイント", "mspaint $F", "", "", "")}
#else
        lib::Array<structs::OuterTool>{}
#endif
    };
    settings::Prop<lib::Array<lib::String>> ignorePaths{ "ignorePaths",
#if defined(_WIN32)
        lib::Array<lib::String>{".*", "Thumbs.db"}
#else
        lib::Array<lib::String>{".*"}
#endif
    };
    settings::Prop<lib::String, false, false> selectedArchiveFilter{"selectedArchiveFilter", ".zip"};
    settings::Prop<lib::Array<structs::ClassicEngine>, false, false> classicEngines{"classicEngines", lib::Array<structs::ClassicEngine>{}};
    settings::Prop<bool, false, false> addNewClassicEngine{"addNewClassicEngine", true};
    settings::Prop<lib::Array<structs::SkinTypeByClassicEngine>, true, false> skinTypesByClassicEngines{"skinTypesByClassicEngines", lib::Array<structs::SkinTypeByClassicEngine>{
		structs::SkinTypeByClassicEngine("School", "^s_c_wirth\\.exe$", ""),
		structs::SkinTypeByClassicEngine("Modern", "^modernwirth\\.exe$", ""),
		structs::SkinTypeByClassicEngine("Monsters", "^darkwirth\\.exe$", ""),
		structs::SkinTypeByClassicEngine("Oedo", "^oedowirth\\.exe$", ""),
		structs::SkinTypeByClassicEngine("ScienceFiction", "", "^.*sfv$"),
	}};
    settings::Prop<bool, false, false> drawCountOfUseOfStart{"drawCountOfUseOfStart", true};
    settings::Prop<bool, false, false> drawContentTreeLine{"drawContentTreeLine", true};
    settings::Prop<bool, false, false> drawContentWarnings{"drawContentWarnings", true};
    settings::Prop<bool, false, false> drawContentIconOnDetail{"drawContentIconOnDetail", true};
    settings::Prop<std::int32_t, true, true> commentBoxDistance{"commentBoxDistance", 50};
    settings::Prop<std::int32_t, true, true> warningImageWidth{"warningImageWidth", 200};
    settings::Prop<std::int32_t, true, true> warningImageWidthForTable{"warningImageWidthForTable", 80};
    settings::Prop<std::int32_t, true, true> warningImageWidthForTree{"warningImageWidthForTree", 80};
    settings::Prop<std::int32_t, true, true> warningImageWidthForCardList{"warningImageWidthForCardList", 50};
    settings::Prop<structs::CRGB, true, false> warningImageColor{"warningImageColor", structs::CRGB(255, 128, 128)};
    settings::Prop<bool, false, false> doubleIO{"doubleIO", true};
    settings::Prop<bool, false, false> cautionToScenarioLoadErrors{"cautionToScenarioLoadErrors", true};
    settings::Prop<bool, false, false> reconstruction{"reconstruction", true};
    settings::Prop<bool, false, false> branchRandomFailureFormat{"branchRandomFailureFormat", true};
    settings::Prop<bool, false, false> openLastScenario{"openLastScenario", true};
    settings::Prop<bool, false, false> imageCache{"imageCache", true};
    settings::Prop<lib::String, false, false> savedSound{"savedSound", ""};
    settings::Prop<bool, false, false> connContentTools{"connContentTools", true};
    settings::Prop<std::int32_t, true, false> previewAlpha{"previewAlpha", 255};
    settings::Prop<std::int32_t, true, true> previewMaxWidth{"previewMaxWidth", 150};
    settings::Prop<std::int32_t, true, true> previewMaxHeight{"previewMaxHeight", 150};
    settings::Prop<std::int32_t, false, false> undoMaxMainView{"undoMaxMainView", 1024};
    settings::Prop<std::int32_t, false, false> undoMaxEvent{"undoMaxEvent", 1024};
    settings::Prop<std::int32_t, false, false> undoMaxEtc{"undoMaxEtc", 1024};
    settings::Prop<std::int32_t, false, false> undoMaxLimit{"undoMaxLimit", std::numeric_limits<std::int16_t>::max()};
    settings::Prop<std::int32_t, false, false> undoMaxReplace{"undoMaxReplace", 16};
    settings::Prop<std::int32_t, false, false> dialogStatus{"dialogStatus", static_cast<std::int32_t>(structs::DialogStatus::Top)};
    settings::Prop<bool, false, false> showInputGuide{"showInputGuide", true};
    settings::Prop<bool, false, false> showSummaryPreview{"showSummaryPreview", true};
    settings::Prop<bool, false, false> floatMessagePreview{"floatMessagePreview", false};
    settings::Prop<bool, false, false> useMessageWindowColorInTextContentDialog{"useMessageWindowColorInTextContentDialog", true};
    settings::Prop<bool, false, false> showDialogPreview{"showDialogPreview", true};
    settings::Prop<bool, false, false> showMessagePreview{"showMessagePreview", true};
    settings::Prop<std::int32_t, false, true> messageVarKindColumn{"messageVarKindColumn", 200};
    settings::Prop<std::int32_t, false, true> messageVarValueColumn{"messageVarValueColumn", 250};
    settings::Prop<std::int32_t, false, true> textVarKindColumn{"textVarKindColumn", 160};
    settings::Prop<std::int32_t, false, true> textVarValueColumn{"textVarValueColumn", 180};
    settings::Prop<std::int32_t, true, true> messageVarTableHeight{"messageVarTableHeight", 300};
    settings::Prop<lib::String, false, false> messageVarSelected{"messageVarSelected", "[選択中----14]"};
    settings::Prop<lib::String, false, false> messageVarUnselected{"messageVarUnselected", "[選択外----14]"};
    settings::Prop<lib::String, false, false> messageVarRandom{"messageVarRandom", "[ランダム--14]"};
    settings::Prop<lib::String, false, false> messageVarCard{"messageVarCard", "[カード--12]"};
    settings::Prop<lib::String, false, false> messageVarRef{"messageVarRef", "[話者------14]"};
    settings::Prop<lib::String, false, false> messageVarTeam{"messageVarTeam", "[チーム名------------------30]"};
    settings::Prop<lib::String, false, false> messageVarYado{"messageVarYado", "[宿屋名--------18]"};
    settings::Prop<std::uint32_t, false, false> messageVarSelectedPlayerCardNumber{"messageVarSelectedPlayerCardNumber", 0};
    settings::Prop<lib::Array<lib::String>, false, false> messageVarPlayerCardName{"messageVarPlayerCardName", lib::Array<lib::String>{
		"[PC名1-----14]",
		"[PC名2-----14]",
		"[PC名3-----14]",
		"[PC名4-----14]",
		"[PC名5-----14]",
		"[PC名6-----14]",
	}};
    settings::Prop<lib::String, true, false> messageVarPlayerCardNameDefault{"messageVarPlayerCardNameDefault", "[PC名%N-----14]"};
    settings::Prop<std::uint32_t, false, false> selectedPlayerCardName{"selectedPlayerCardName", 1};
    settings::Prop<bool, false, false> showVariableValuesInEventText{"showVariableValuesInEventText", false};
    settings::Prop<bool, false, false> editSelectionWithCombo{"editSelectionWithCombo", true};
    settings::Prop<lib::Array<structs::ScTemplate>, false, false> scenarioTemplates{"scenarioTemplates", lib::Array<structs::ScTemplate>{}};
    settings::Prop<lib::String, false, false> defaultScenarioTemplate{"defaultScenarioTemplate", ""};
    settings::Prop<bool, false, false> defaultIsTemplate{"defaultIsTemplate", false};
    settings::Prop<bool, false, false> createScenarioDir{"createScenarioDir", true};
    settings::Prop<lib::Array<structs::EvTemplate>, false, false> eventTemplates{"eventTemplates", lib::Array<structs::EvTemplate>{}};
    settings::Prop<std::int32_t, true, true> scriptVarTableHeight{"scriptVarTableHeight", 300};
    settings::Prop<lib::String, false, false> archivePath{"archivePath", ""};
    settings::Prop<lib::Array<structs::ContentConversionGroup>, false, false> contentConversionGroups{"contentConversionGroups", lib::Array<structs::ContentConversionGroup>{
		structs::ContentConversionGroup({types::CType::LinkStart, types::CType::CallStart, types::CType::LinkPackage, types::CType::CallPackage}),
		structs::ContentConversionGroup({types::CType::ShowParty, types::CType::HideParty}),
		structs::ContentConversionGroup({types::CType::ChangeBgImage, types::CType::MoveBgImage, types::CType::ReplaceBgImage, types::CType::LoseBgImage, types::CType::Redisplay}),
		structs::ContentConversionGroup({types::CType::BranchSelect, types::CType::BranchRandomSelect}),
		structs::ContentConversionGroup({types::CType::TalkMessage, types::CType::TalkDialog}),
		structs::ContentConversionGroup({types::CType::BranchAbility, types::CType::BranchLevel, types::CType::BranchStatus, types::CType::BranchPartyNumber, types::CType::BranchKeyCode}),
		structs::ContentConversionGroup({types::CType::BranchArea, types::CType::BranchBattle, types::CType::BranchIsBattle, types::CType::BranchRound}),
		structs::ContentConversionGroup({types::CType::BranchRandom, types::CType::BranchMultiRandom}),
		structs::ContentConversionGroup({types::CType::BranchFlag, types::CType::SetFlag, types::CType::ReverseFlag, types::CType::SubstituteFlag, types::CType::CheckFlag, types::CType::MoveCard}),
		structs::ContentConversionGroup({types::CType::BranchMultiStep, types::CType::BranchStep, types::CType::SetStep, types::CType::SetStepUp, types::CType::SetStepDown, types::CType::SubstituteStep, types::CType::BranchStepCmp, types::CType::CheckStep}),
		structs::ContentConversionGroup({types::CType::CheckFlag, types::CType::CheckStep}),
		structs::ContentConversionGroup({types::CType::BranchCast, types::CType::GetCast, types::CType::LoseCast}),
		structs::ContentConversionGroup({types::CType::BranchItem, types::CType::GetItem, types::CType::LoseItem}),
		structs::ContentConversionGroup({types::CType::BranchSkill, types::CType::GetSkill, types::CType::LoseSkill}),
		structs::ContentConversionGroup({types::CType::BranchBeast, types::CType::GetBeast, types::CType::LoseBeast}),
		structs::ContentConversionGroup({types::CType::BranchItem, types::CType::BranchSkill, types::CType::BranchBeast, types::CType::BranchKeyCode}),
		structs::ContentConversionGroup({types::CType::GetItem, types::CType::GetSkill, types::CType::GetBeast}),
		structs::ContentConversionGroup({types::CType::LoseItem, types::CType::LoseSkill, types::CType::LoseBeast}),
		structs::ContentConversionGroup({types::CType::BranchInfo, types::CType::GetInfo, types::CType::LoseInfo}),
		structs::ContentConversionGroup({types::CType::BranchMoney, types::CType::GetMoney, types::CType::LoseMoney}),
		structs::ContentConversionGroup({types::CType::BranchCoupon, types::CType::GetCoupon, types::CType::LoseCoupon, types::CType::BranchMultiCoupon}),
		structs::ContentConversionGroup({types::CType::BranchGossip, types::CType::GetGossip, types::CType::LoseGossip}),
		structs::ContentConversionGroup({types::CType::BranchCompleteStamp, types::CType::GetCompleteStamp, types::CType::LoseCompleteStamp}),
		structs::ContentConversionGroup({types::CType::ChangeArea, types::CType::StartBattle}),
		structs::ContentConversionGroup({types::CType::End, types::CType::EndBadEnd}),
		structs::ContentConversionGroup({types::CType::Effect, types::CType::PlayBgm, types::CType::PlaySound}),
		structs::ContentConversionGroup({types::CType::BranchVariant, types::CType::SetVariant, types::CType::CheckVariant}),
	}};
    settings::Prop<lib::Array<ContentInitializer>, false, false> contentInitializers{"contentInitializers", lib::Array<ContentInitializer>{}};
    settings::Prop<structs::ToolBarSettings, false, false> mainToolBar{"mainToolBar", structs::ToolBarSettings({
		{
			structs::Tool(types::MenuID::New),
			structs::Tool(types::MenuID::Open),
			structs::Tool(types::MenuID::Save),
		},
		{
			structs::Tool(types::MenuID::Undo),
			structs::Tool(types::MenuID::Redo),
		},
		{
			structs::Tool(types::MenuID::Cut),
			structs::Tool(types::MenuID::Copy),
			structs::Tool(types::MenuID::Paste),
		},
		{
			structs::Tool(types::MenuID::Up),
			structs::Tool(types::MenuID::Down),
		},
		{
			structs::Tool(types::MenuID::Find),
		},
		{
			structs::Tool(types::MenuID::NewAreaDir),
			structs::Tool(types::MenuID::NewArea),
			structs::Tool(types::MenuID::NewBattle),
			structs::Tool(types::MenuID::NewPackage),
			structs::Tool::opCall(),
			structs::Tool(types::MenuID::NewFlagDir),
			structs::Tool(types::MenuID::NewFlag),
			structs::Tool(types::MenuID::NewStep),
			structs::Tool(types::MenuID::NewVariant),
		},
		{
			structs::Tool(types::MenuID::ShowCardProp),
			structs::Tool(types::MenuID::ShowCardDetail),
			structs::Tool::opCall(),
			structs::Tool(types::MenuID::NewCast),
			structs::Tool(types::MenuID::NewSkill),
			structs::Tool(types::MenuID::NewItem),
			structs::Tool(types::MenuID::NewBeast),
			structs::Tool(types::MenuID::NewInfo),
		},
		{
			structs::Tool(types::MenuID::OpenDir),
		},
		{
			structs::Tool(types::MenuID::ExecEngine),
			structs::Tool(types::MenuID::ExecEngineWithParty),
			structs::Tool::opCall(),
			structs::Tool(types::MenuID::OpenImportSource),
			structs::Tool::opCall(),
			structs::Tool(types::MenuID::Settings),
		},
	})};
    settings::Prop<bool, false, false> gentleAngleEventTree{"gentleAngleEventTree", false, true};

    [[nodiscard]] bool searchContents(types::CType id) const;
    void searchContents(types::CType id, bool value);

    void toNode(xml::XNode& node, bool writeAll) const;
    void toNode(xml::XNode& node) const { toNode(node, false); }
    [[nodiscard]] static FlexEtcProps fromNode(xml::XNode& node, std::uint64_t dataVersion);

private:
    template <typename Visitor>
    void forEachProperty(Visitor&& visitor) {
        visitor(languageDir);
        visitor(languageFile);
        visitor(useSystemLanguage);
        visitor(warningWindowsHandleCount);
        visitor(liftWarningWindowsHandleCount);
        visitor(targetVersion);
        visitor(imeMode);
        visitor(imageScale);
        visitor(drawingScale);
        visitor(imageScalePercent);
        visitor(drawingScalePercent);
        visitor(uiTextScale);
        visitor(tabs);
        visitor(toolsLock);
        visitor(toolsOrder);
        visitor(toolsWrapIndices);
        visitor(comboVisibleItemCount);
        visitor(directorySashL);
        visitor(directorySashR);
        visitor(directorySashV);
        visitor(filesSortColumn);
        visitor(filesSortDirection);
        visitor(fileNameColumn);
        visitor(fileExtColumn);
        visitor(fileCountColumn);
        visitor(areaIdColumn);
        visitor(areaNameColumn);
        visitor(areaCountColumn);
        visitor(importAreaIdColumn);
        visitor(importAreaNameColumn);
        visitor(importAreaCountColumn);
        visitor(areasSortColumn);
        visitor(areasSortDirection);
        visitor(importAreasSortColumn);
        visitor(importAreasSortDirection);
        visitor(summaryParamSashL);
        visitor(summaryParamSashR);
        visitor(rCouponsStartAreaSashL);
        visitor(rCouponsStartAreaSashR);
        visitor(areaViewL);
        visitor(areaViewR);
        visitor(battleViewL);
        visitor(battleViewR);
        visitor(bgImageViewL);
        visitor(bgImageViewR);
        visitor(areaViewImageFlagL);
        visitor(areaViewImageFlagR);
        visitor(battleViewImageFlagL);
        visitor(battleViewImageFlagR);
        visitor(bgImageViewImageFlagL);
        visitor(bgImageViewImageFlagR);
        visitor(partyCardAlpha);
        visitor(viewPartyCardsArea);
        visitor(viewPartyCardsBattle);
        visitor(viewEnemyCardDebug);
        visitor(viewPartyCardsEvent);
        visitor(messageAlpha);
        visitor(viewMessageArea);
        visitor(viewMessageBattle);
        visitor(viewMessageEvent);
        visitor(viewReferenceCards);
        visitor(fixedImagesMenuCards);
        visitor(fixedImagesCells);
        visitor(fixedImagesBackground);
        visitor(fixedImagesBattle);
        visitor(fixedImagesEvent);
        visitor(fixedImagesEventBackground);
        visitor(fixedImagesDefaultBackground);
        visitor(viewCards);
        visitor(viewBgImages);
        visitor(showGrid);
        visitor(areaSashT);
        visitor(areaSashB);
        visitor(areaSashL);
        visitor(areaSashR);
        visitor(importAreaSashL);
        visitor(importAreaSashR);
        visitor(areaSashV);
        visitor(importAreaSashV);
        visitor(flagSashL);
        visitor(flagSashR);
        visitor(flagSashV);
        visitor(flagsWidth);
        visitor(flagsHeight);
        visitor(menuCardSashL);
        visitor(menuCardSashR);
        visitor(enemyCardSashL);
        visitor(enemyCardSashR);
        visitor(backSashL);
        visitor(backSashR);
        visitor(bgImageSampleWidth);
        visitor(bgImageSampleHeight);
        visitor(cellPCNumberWidth);
        visitor(textCellVSashT);
        visitor(textCellVSashB);
        visitor(textCellHSashL);
        visitor(textCellHSashR);
        visitor(textCellPreviewSashL);
        visitor(textCellPreviewSashR);
        visitor(textCellPreviewWidth);
        visitor(textCellPreviewHeight);
        visitor(textCellBoxWidth);
        visitor(textCellBoxHeight);
        visitor(cardIdColumn);
        visitor(cardNameColumn);
        visitor(cardNumberColumn);
        visitor(cardDescriptionColumn);
        visitor(cardCountColumn);
        visitor(handCardIdColumn);
        visitor(handCardNameColumn);
        visitor(handCardNumberColumn);
        visitor(handCardDescriptionColumn);
        visitor(importCardIdColumn);
        visitor(importCardNameColumn);
        visitor(importCardNumberColumn);
        visitor(importCardDescriptionColumn);
        visitor(importCardCountColumn);
        visitor(importHandCardIdColumn);
        visitor(importHandCardNameColumn);
        visitor(importHandCardNumberColumn);
        visitor(importHandCardDescriptionColumn);
        visitor(mainCardsSortColumn);
        visitor(mainCardsSortDirection);
        visitor(handCardsSortColumn);
        visitor(handCardsSortDirection);
        visitor(importCardsSortColumn);
        visitor(importCardsSortDirection);
        visitor(importHandCardsSortColumn);
        visitor(importHandCardsSortDirection);
        visitor(linkCardMaskColor);
        visitor(negativeCardNameBorder);
        visitor(cardNameBorderingColorForWhite);
        visitor(cardNameBorderingColorForBlack);
        visitor(couponWidth);
        visitor(couponHeight);
        visitor(couponValueColumn);
        visitor(couponEventL);
        visitor(couponEventR);
        visitor(idColumn);
        visitor(valueNumberColumn);
        visitor(nameTableWidth);
        visitor(nameTableHeight);
        visitor(flagEventSashL);
        visitor(flagEventSashR);
        visitor(nameWidth);
        visitor(firesWidth);
        visitor(flagNameWidth);
        visitor(flagInitWidth);
        visitor(flagValueWidth);
        visitor(flagNameColumn);
        visitor(flagInitColumn);
        visitor(variableInitializationColumn);
        visitor(flagCountColumn);
        visitor(localFlagNameColumn);
        visitor(localFlagInitColumn);
        visitor(localVariableInitializationColumn);
        visitor(localFlagCountColumn);
        visitor(localVariablesSashL);
        visitor(localVariablesSashR);
        visitor(limitForNumberOfVariant);
        visitor(filesWidth);
        visitor(filesHeight);
        visitor(talkersWidth);
        visitor(motionsWidth);
        visitor(incrementalSearchBoxWidth);
        visitor(showMainToolBar);
        visitor(showSceneToolBar);
        visitor(showEventToolBar);
        visitor(flagCombiSashL);
        visitor(flagCombiSashR);
        visitor(useCoolBar);
        visitor(mainToolBarCustomSashL);
        visitor(mainToolBarCustomSashR);
        visitor(stepTopSashL);
        visitor(stepTopSashR);
        visitor(flagTopSashL);
        visitor(flagTopSashR);
        visitor(toolTipWidth);
        visitor(colorExampleWidth);
        visitor(buttonWidth);
        visitor(radioGroupSeparatorWidth);
        visitor(couponValueColumnWidth);
        visitor(idNameColumn);
        visitor(idCountColumn);
        visitor(couponSortColumn);
        visitor(couponSortDirection);
        visitor(gossipSortColumn);
        visitor(gossipSortDirection);
        visitor(completeStampSortColumn);
        visitor(completeStampSortDirection);
        visitor(keyCodeSortColumn);
        visitor(keyCodeSortDirection);
        visitor(cellNameSortColumn);
        visitor(cellNameSortDirection);
        visitor(cardGroupSortColumn);
        visitor(cardGroupSortDirection);
        visitor(authorNewAreaNameSashL);
        visitor(authorNewAreaNameSashR);
        visitor(historyScenarioNameColumn);
        visitor(historyScenarioPathColumn);
        visitor(executionPartyNameColumn);
        visitor(executionPartyYadoColumn);
        visitor(executionPartyEngineColumn);
        visitor(expressionAndTargetSashT);
        visitor(expressionAndTargetSashB);
        visitor(expressionFunctionAndArgsSashT);
        visitor(expressionFunctionAndArgsSashB);
        visitor(functionCallEditorWidth);
        visitor(functionCallEditorHeight);
        visitor(functionArgumentNameColumn);
        visitor(functionArgumentTypeColumn);
        visitor(functionArgumentValueColumn);
        visitor(expressionCheckingDelay);
        visitor(keyCodesByFeaturesFeatureColumn);
        visitor(keyCodesByFeaturesKeyCodeColumn);
        visitor(keyCodesByMotionsMotionColumn);
        visitor(keyCodesByMotionsElementColumn);
        visitor(keyCodesByMotionsKeyCodeColumn);
        visitor(elementOverrideNameColumn);
        visitor(elementOverrideTargetTypeColumn);
        visitor(keyCodesElementSashL);
        visitor(keyCodesElementSashR);
        visitor(partyMax);
        visitor(cardScaleMax);
        visitor(cardScaleMin);
        visitor(layerMax);
        visitor(posLeftMax);
        visitor(posTopMax);
        visitor(backWidthMax);
        visitor(backHeightMax);
        visitor(levelMax);
        visitor(castLevelMax);
        visitor(lifeMax);
        visitor(couponValueMax);
        visitor(physicalMax);
        visitor(mentalMax);
        visitor(skillLevelMax);
        visitor(useCountMax);
        visitor(priceMax);
        visitor(enhanceMax);
        visitor(roundMax);
        visitor(paralyzeMax);
        visitor(poisonMax);
        visitor(cardNumberMax);
        visitor(waitMax);
        visitor(uValueMax);
        visitor(skillPowerMax);
        visitor(beastMaxNest);
        visitor(flagInitValue);
        visitor(stepInitValue);
        visitor(stepCountMax);
        visitor(stepValueName);
        visitor(imageListWidth);
        visitor(imageListHeight);
        visitor(imageListForBgImagesWidth);
        visitor(imageListForBgImagesHeight);
        visitor(layerListWidth);
        visitor(layerListHeight);
        visitor(cardLife);
        visitor(cardDetails);
        visitor(cardsMarginX);
        visitor(cardsSpaceX);
        visitor(cardsMarginY);
        visitor(cardsSpaceY);
        visitor(cardsTitleSpace);
        visitor(cardsFocusLinePadding);
        visitor(cardsDefaultWrap);
        visitor(seKeyCodeSashL);
        visitor(seKeyCodeSashR);
        visitor(effectContentSEKeyCodeSashL);
        visitor(effectContentSEKeyCodeSashR);
        visitor(talkMainSashL);
        visitor(talkMainSashR);
        visitor(talkLeftSashL);
        visitor(talkLeftSashR);
        visitor(msgBackR);
        visitor(msgBackG);
        visitor(msgBackB);
        visitor(msgForeR);
        visitor(msgForeG);
        visitor(msgForeB);
        visitor(textTabs);
        visitor(messageLineColor1);
        visitor(messageLineColor2);
        visitor(messageBackColor);
        visitor(messageForeColor);
        visitor(messageHemColor);
        visitor(gridX);
        visitor(gridY);
        visitor(gridRange);
        visitor(gridColor);
        visitor(gridHighlightColor);
        visitor(enhanceMaxVal);
        visitor(enhanceHighVal);
        visitor(enhanceMiddleVal);
        visitor(enhanceColorMax);
        visitor(enhanceColorHigh);
        visitor(enhanceColorMiddle);
        visitor(enhanceColorLow);
        visitor(penaltyColorMax);
        visitor(penaltyColorHigh);
        visitor(penaltyColorMiddle);
        visitor(penaltyColorLow);
        visitor(textCellDefaultWidth);
        visitor(textCellDefaultHeight);
        visitor(textCellDefaultFontClassic);
        visitor(textCellDefaultFont);
        visitor(textCellDefaultFontSize);
        visitor(textCellDefaultColor);
        visitor(textCellDefaultBorderingColor);
        visitor(fontSizeMax);
        visitor(borderingWidthMax);
        visitor(colorCellDefaultWidth);
        visitor(colorCellDefaultHeight);
        visitor(colorCellDefaultColor1);
        visitor(colorCellDefaultColor2);
        visitor(noFileName);
        visitor(contentsOrder);
        visitor(contentsLock);
        visitor(contentsWrapIndices);
        visitor(contentsAutoOpen);
        visitor(contentsPutMode);
        visitor(contentsInsertFirst);
        visitor(showContentsGroupName);
        visitor(showEventContentDescription);
        visitor(showMotionDescription);
        visitor(contentsFloat);
        visitor(contentsAutoHide);
        visitor(comboListVisible);
        visitor(showCurrentValueOnTopAlways);
        visitor(showContentsBoxHeightWhenNoToolBar);
        visitor(showSummaryInAreaTable);
        visitor(saveTableViewSelection);
        visitor(clickIsOpenEvent);
        visitor(smoothingCard);
        visitor(ignorePathsWidth);
        visitor(menuSettingsHeight);
        visitor(settingListWidth);
        visitor(settingListHeight);
        visitor(importOptionMaterials);
        visitor(importOptionVariables);
        visitor(importOptionCasts);
        visitor(importOptionSkills);
        visitor(importOptionItems);
        visitor(importOptionBeasts);
        visitor(importOptionInfos);
        visitor(importOptionAreas);
        visitor(importOptionBattles);
        visitor(importOptionPackages);
        visitor(importOptionIncludedFiles);
        visitor(importOptionIncludedFilesWithoutIncluding);
        visitor(importOptionIncludedBgImages);
        visitor(importOptionHands);
        visitor(importOptionBeastsInMotions);
        visitor(importOptionOverwriteScenarioInfo);
        visitor(wallpaper);
        visitor(wallpaperStyle);
        visitor(wallColorR);
        visitor(wallColorG);
        visitor(wallColorB);
        visitor(bgImagesDefault);
        visitor(bgImageSettingsSashL);
        visitor(bgImageSettingsSashR);
        visitor(selectionKeyCodeSashL);
        visitor(selectionKeyCodeSashR);
        visitor(bgImageSettingsKeyCodesSashL);
        visitor(bgImageSettingsKeyCodesSashR);
        visitor(keyCodesByFeaturesKeyCodesByMotionSashL);
        visitor(keyCodesByFeaturesKeyCodesByMotionSashR);
        visitor(bgImageSelectionSashL);
        visitor(bgImageSelectionSashR);
        visitor(outerToolsSashL);
        visitor(outerToolsSashR);
        visitor(outerToolShortcutSashL);
        visitor(outerToolShortcutSashR);
        visitor(classicEnginesSashL);
        visitor(classicEnginesSashR);
        visitor(classicEnginesNameTypeSashL);
        visitor(classicEnginesNameTypeSashR);
        visitor(classicEngineShortcutSashL);
        visitor(classicEngineShortcutSashR);
        visitor(featureDefaultNameWidth);
        visitor(featureVariantNameWidth);
        visitor(featureManualNameWidth);
        visitor(eventTemplatesSashL);
        visitor(eventTemplatesSashR);
        visitor(eventTemplatesOfScenarioSashL);
        visitor(eventTemplatesOfScenarioSashR);
        visitor(eventTemplateShortcutSashL);
        visitor(eventTemplateShortcutSashR);
        visitor(eventTemplateOfScenarioShortcutSashL);
        visitor(eventTemplateOfScenarioShortcutSashR);
        visitor(scenarioTemplatesSashL);
        visitor(scenarioTemplatesSashR);
        visitor(templatesSashL);
        visitor(templatesSashR);
        visitor(toolsClassicEnginesSashL);
        visitor(toolsClassicEnginesSashR);
        visitor(selectionWidth);
        visitor(keyCodeWidth);
        visitor(roundWidth);
        visitor(scenarioPath);
        visitor(tempPath);
        visitor(backupPath);
        visitor(backupEnabled);
        visitor(backupIntervalType);
        visitor(backupInterval);
        visitor(backupIntervalEdit);
        visitor(backupCount);
        visitor(autoSave);
        visitor(backupRefAuthor);
        visitor(backupBeforeSaveEnabled);
        visitor(backupBeforeSavePath);
        visitor(backupBeforeSaveDir);
        visitor(backupArchived);
        visitor(ignoreMenuSashL);
        visitor(ignoreMenuSashR);
        visitor(highValueOfImageControl);
        visitor(showDuplicateViewInToolBar);
        visitor(openHistories);
        visitor(scenarioBookmarks);
        visitor(historyMax);
        visitor(historySnipLength);
        visitor(removeScenarioHistoryNonExistingWithPriority);
        visitor(lastScenario);
        visitor(importHistory);
        visitor(importBookmarks);
        visitor(searchResultTableWidth);
        visitor(searchResultTableHeight);
        visitor(engine);
        visitor(engineScript);
        visitor(enginePath);
        visitor(pythonCommand);
        visitor(lastExecutedParty);
        visitor(executedParties);
        visitor(executedPartyBookmarks);
        visitor(executedPartiesMax);
        visitor(removePartyHistoryNonExistingWithPriority);
        visitor(dataDir);
        visitor(findEnginePath);
        visitor(defaultSkin);
        visitor(defaultSkinName);
        visitor(defaultAuthor);
        visitor(lastSkinType);
        visitor(lastSkinName);
        visitor(saveSkinName);
        visitor(replaceClassicResourceExtension);
        visitor(tableExtension);
        visitor(midiExtension);
        visitor(waveExtension);
        visitor(classicEngineRegex);
        visitor(classicDataDirRegex);
        visitor(classicMatchKey);
        visitor(expandXMLs);
        visitor(xmlCopy);
        visitor(showSpNature);
        visitor(saveChangedOnlyWsn);
        visitor(saveChangedOnly);
        visitor(xmlFileNameIsIDOnly);
        visitor(archiveInNewThread);
        visitor(saveInnerImagePath);
        visitor(linkCard);
        visitor(traceDirectories);
        visitor(logicalSort);
        visitor(copyDesc);
        visitor(refCardsAtEditBgImage);
        visitor(showImagePreview);
        visitor(maskCardImagePreview);
        visitor(ignoreBackgroundInRange);
        visitor(classicStyleTree);
        visitor(adjustContentName);
        visitor(showHandMark);
        visitor(showHandMarkAlways);
        visitor(showEventTreeMark);
        visitor(showEventTreeMarkAlways);
        visitor(ignoreEmptyStart);
        visitor(showSkillCardLevel);
        visitor(switchTabWheel);
        visitor(closeTabWithMiddleClick);
        visitor(openTabAtRightOfCurrentTab);
        visitor(showCloseButtonAllTab);
        visitor(cautionBeforeReplace);
        visitor(startIncrementalSearchWhenKeyDown);
        visitor(radarStyleParams);
        visitor(showAptitudeOnCastCardEditor);
        visitor(showCardListHeader);
        visitor(showCardListTitle);
        visitor(applyDialogsBeforeSave);
        visitor(useNamesAfterStandard);
        visitor(expandChooserItems);
        visitor(restorePositionOfEventTreeView);
        visitor(selectVariableWithTree);
        visitor(useCurrentStartName);
        visitor(autoUpdateJpy1File);
        visitor(straightEventTreeView);
        visitor(eventTreeSlope);
        visitor(forceIndentBranchContent);
        visitor(showTerminalMark);
        visitor(showTargetStartLineNumber);
        visitor(clickIconIsStartEdit);
        visitor(showAreaDirTree);
        visitor(incrementNewAreaName);
        visitor(saveNeedChanged);
        visitor(canVanishWorkAreaInMainWindow);
        visitor(switchFixedWithCheckBox);
        visitor(resizingImageWithRatio);
        visitor(drawXORSelectionLine);
        visitor(showSceneViewSelectionFilter);
        visitor(showItemNumberOfSceneAndEventView);
        visitor(showCardStatusUnderPointer);
        visitor(showStatusTime);
        visitor(showEventTreeLineNumber);
        visitor(showEventTreeDetail);
        visitor(editTriggerType);
        visitor(specifySelectedEventTree);
        visitor(spinnerUpDownWithWheel);
        visitor(soundPlayType);
        visitor(soundEffectPlayType);
        visitor(bgmVolume);
        visitor(seVolume);
        visitor(cardWirthPyDefaultSoundFont);
        visitor(loopCountMax);
        visitor(fadeInMax);
        visitor(excludeCardSizeImage);
        visitor(selectionColumnsMax);
        visitor(showInheritBackground);
        visitor(detailAreaWidth);
        visitor(searchPlan);
        visitor(searchIDKind);
        visitor(searchHistories);
        visitor(replaceHistories);
        visitor(searchHistoryMax);
        visitor(replaceRangeSashL);
        visitor(replaceRangeSashR);
        visitor(replaceTextNotIgnoreCase);
        visitor(replaceTextRegExp);
        visitor(replaceTextWildcard);
        visitor(replaceTextExactMatch);
        visitor(replaceTextIgnoreReturnCode);
        visitor(grepDir);
        visitor(grepDirHistories);
        visitor(grepSubDir);
        visitor(searchResultRefreshCount);
        visitor(searchResultRealtime);
        visitor(showRouteOfSearchResult);
        visitor(searchErrorTargetVersion);
        visitor(replaceTextSummary);
        visitor(replaceTextScenario);
        visitor(replaceTextAuthor);
        visitor(replaceTextMessage);
        visitor(replaceTextCardName);
        visitor(replaceTextCardDescription);
        visitor(replaceTextEventText);
        visitor(replaceTextStart);
        visitor(replaceTextVariableName);
        visitor(replaceTextVariableValue);
        visitor(replaceTextExpression);
        visitor(replaceTextCoupon);
        visitor(replaceTextGossip);
        visitor(replaceTextEndScenario);
        visitor(replaceTextAreaName);
        visitor(replaceTextKeyCode);
        visitor(replaceTextCellName);
        visitor(replaceTextCardGroup);
        visitor(replaceTextFile);
        visitor(replaceTextComment);
        visitor(replaceTextJptx);
        visitor(replaceTextTextFile);
        visitor(plainTextFileExtensions);
        visitor(replaceNameCoupon);
        visitor(replaceNameGossip);
        visitor(replaceNameEndScenario);
        visitor(replaceNameKeyCode);
        visitor(replaceNameCellName);
        visitor(replaceNameCardGroup);
        visitor(searchContentsStart);
        visitor(searchContentsStartBattle);
        visitor(searchContentsEnd);
        visitor(searchContentsEndBadEnd);
        visitor(searchContentsChangeArea);
        visitor(searchContentsChangeBgImage);
        visitor(searchContentsEffect);
        visitor(searchContentsEffectBreak);
        visitor(searchContentsLinkStart);
        visitor(searchContentsLinkPackage);
        visitor(searchContentsTalkMessage);
        visitor(searchContentsTalkDialog);
        visitor(searchContentsPlayBgm);
        visitor(searchContentsPlaySound);
        visitor(searchContentsWait);
        visitor(searchContentsElapseTime);
        visitor(searchContentsCallStart);
        visitor(searchContentsCallPackage);
        visitor(searchContentsBranchFlag);
        visitor(searchContentsBranchMultiStep);
        visitor(searchContentsBranchStep);
        visitor(searchContentsBranchSelect);
        visitor(searchContentsBranchAbility);
        visitor(searchContentsBranchRandom);
        visitor(searchContentsBranchLevel);
        visitor(searchContentsBranchStatus);
        visitor(searchContentsBranchPartyNumber);
        visitor(searchContentsBranchArea);
        visitor(searchContentsBranchBattle);
        visitor(searchContentsBranchIsBattle);
        visitor(searchContentsBranchCast);
        visitor(searchContentsBranchItem);
        visitor(searchContentsBranchSkill);
        visitor(searchContentsBranchInfo);
        visitor(searchContentsBranchBeast);
        visitor(searchContentsBranchMoney);
        visitor(searchContentsBranchCoupon);
        visitor(searchContentsBranchCompleteStamp);
        visitor(searchContentsBranchGossip);
        visitor(searchContentsSetFlag);
        visitor(searchContentsSetStep);
        visitor(searchContentsSetStepUp);
        visitor(searchContentsSetStepDown);
        visitor(searchContentsReverseFlag);
        visitor(searchContentsCheckFlag);
        visitor(searchContentsGetCast);
        visitor(searchContentsGetItem);
        visitor(searchContentsGetSkill);
        visitor(searchContentsGetInfo);
        visitor(searchContentsGetBeast);
        visitor(searchContentsGetMoney);
        visitor(searchContentsGetCoupon);
        visitor(searchContentsGetCompleteStamp);
        visitor(searchContentsGetGossip);
        visitor(searchContentsLoseCast);
        visitor(searchContentsLoseItem);
        visitor(searchContentsLoseSkill);
        visitor(searchContentsLoseInfo);
        visitor(searchContentsLoseBeast);
        visitor(searchContentsLoseMoney);
        visitor(searchContentsLoseCoupon);
        visitor(searchContentsLoseCompleteStamp);
        visitor(searchContentsLoseGossip);
        visitor(searchContentsShowParty);
        visitor(searchContentsHideParty);
        visitor(searchContentsRedisplay);
        visitor(searchContentsSubstituteStep);
        visitor(searchContentsSubstituteFlag);
        visitor(searchContentsBranchStepCmp);
        visitor(searchContentsBranchFlagCmp);
        visitor(searchContentsBranchRandomSelect);
        visitor(searchContentsBranchKeyCode);
        visitor(searchContentsCheckStep);
        visitor(searchContentsBranchRound);
        visitor(searchContentsMoveBgImage);
        visitor(searchContentsReplaceBgImage);
        visitor(searchContentsLoseBgImage);
        visitor(searchContentsBranchMultiCoupon);
        visitor(searchContentsBranchMultiRandom);
        visitor(searchContentsMoveCard);
        visitor(searchContentsChangeEnvironment);
        visitor(searchContentsBranchVariant);
        visitor(searchContentsSetVariant);
        visitor(searchContentsCheckVariant);
        visitor(searchContentsCustomXmlContent);
        visitor(searchUnusedFlag);
        visitor(searchUnusedStep);
        visitor(searchUnusedVariant);
        visitor(searchUnusedArea);
        visitor(searchUnusedBattle);
        visitor(searchUnusedPackage);
        visitor(searchUnusedCast);
        visitor(searchUnusedSkill);
        visitor(searchUnusedItem);
        visitor(searchUnusedBeast);
        visitor(searchUnusedInfo);
        visitor(searchUnusedStart);
        visitor(searchUnusedPath);
        visitor(searchOpenDialog);
        visitor(searchResultColumnMain);
        visitor(searchResultColumnParent);
        visitor(searchResultColumnCouponCount);
        visitor(searchResultColumnErrorDesc);
        visitor(searchResultColumnScenario);
        visitor(incrementalSearchType);
        visitor(usedCouponToCombo);
        visitor(numberStepToVariantThreshold);
        visitor(newScenarioName);
        visitor(newAreaName);
        visitor(flagTrues);
        visitor(flagFalses);
        visitor(bgImageSettings);
        visitor(standardCoupons);
        visitor(standardCouponsForValued);
        visitor(standardKeyCodes);
        visitor(keyCodesByFeatures);
        visitor(keyCodesByMotions);
        visitor(standardSelections);
        visitor(settingsWithSkinTypes);
        visitor(outerTools);
        visitor(ignorePaths);
        visitor(selectedArchiveFilter);
        visitor(classicEngines);
        visitor(addNewClassicEngine);
        visitor(skinTypesByClassicEngines);
        visitor(drawCountOfUseOfStart);
        visitor(drawContentTreeLine);
        visitor(drawContentWarnings);
        visitor(drawContentIconOnDetail);
        visitor(commentBoxDistance);
        visitor(warningImageWidth);
        visitor(warningImageWidthForTable);
        visitor(warningImageWidthForTree);
        visitor(warningImageWidthForCardList);
        visitor(warningImageColor);
        visitor(doubleIO);
        visitor(cautionToScenarioLoadErrors);
        visitor(reconstruction);
        visitor(branchRandomFailureFormat);
        visitor(openLastScenario);
        visitor(imageCache);
        visitor(savedSound);
        visitor(connContentTools);
        visitor(previewAlpha);
        visitor(previewMaxWidth);
        visitor(previewMaxHeight);
        visitor(undoMaxMainView);
        visitor(undoMaxEvent);
        visitor(undoMaxEtc);
        visitor(undoMaxLimit);
        visitor(undoMaxReplace);
        visitor(dialogStatus);
        visitor(showInputGuide);
        visitor(showSummaryPreview);
        visitor(floatMessagePreview);
        visitor(useMessageWindowColorInTextContentDialog);
        visitor(showDialogPreview);
        visitor(showMessagePreview);
        visitor(messageVarKindColumn);
        visitor(messageVarValueColumn);
        visitor(textVarKindColumn);
        visitor(textVarValueColumn);
        visitor(messageVarTableHeight);
        visitor(messageVarSelected);
        visitor(messageVarUnselected);
        visitor(messageVarRandom);
        visitor(messageVarCard);
        visitor(messageVarRef);
        visitor(messageVarTeam);
        visitor(messageVarYado);
        visitor(messageVarSelectedPlayerCardNumber);
        visitor(messageVarPlayerCardName);
        visitor(messageVarPlayerCardNameDefault);
        visitor(selectedPlayerCardName);
        visitor(showVariableValuesInEventText);
        visitor(editSelectionWithCombo);
        visitor(scenarioTemplates);
        visitor(defaultScenarioTemplate);
        visitor(defaultIsTemplate);
        visitor(createScenarioDir);
        visitor(eventTemplates);
        visitor(scriptVarTableHeight);
        visitor(archivePath);
        visitor(contentConversionGroups);
        visitor(contentInitializers);
        visitor(mainToolBar);
        visitor(gentleAngleEventTree);
    }
    template <typename Visitor>
    void forEachProperty(Visitor&& visitor) const {
        visitor(languageDir);
        visitor(languageFile);
        visitor(useSystemLanguage);
        visitor(warningWindowsHandleCount);
        visitor(liftWarningWindowsHandleCount);
        visitor(targetVersion);
        visitor(imeMode);
        visitor(imageScale);
        visitor(drawingScale);
        visitor(imageScalePercent);
        visitor(drawingScalePercent);
        visitor(uiTextScale);
        visitor(tabs);
        visitor(toolsLock);
        visitor(toolsOrder);
        visitor(toolsWrapIndices);
        visitor(comboVisibleItemCount);
        visitor(directorySashL);
        visitor(directorySashR);
        visitor(directorySashV);
        visitor(filesSortColumn);
        visitor(filesSortDirection);
        visitor(fileNameColumn);
        visitor(fileExtColumn);
        visitor(fileCountColumn);
        visitor(areaIdColumn);
        visitor(areaNameColumn);
        visitor(areaCountColumn);
        visitor(importAreaIdColumn);
        visitor(importAreaNameColumn);
        visitor(importAreaCountColumn);
        visitor(areasSortColumn);
        visitor(areasSortDirection);
        visitor(importAreasSortColumn);
        visitor(importAreasSortDirection);
        visitor(summaryParamSashL);
        visitor(summaryParamSashR);
        visitor(rCouponsStartAreaSashL);
        visitor(rCouponsStartAreaSashR);
        visitor(areaViewL);
        visitor(areaViewR);
        visitor(battleViewL);
        visitor(battleViewR);
        visitor(bgImageViewL);
        visitor(bgImageViewR);
        visitor(areaViewImageFlagL);
        visitor(areaViewImageFlagR);
        visitor(battleViewImageFlagL);
        visitor(battleViewImageFlagR);
        visitor(bgImageViewImageFlagL);
        visitor(bgImageViewImageFlagR);
        visitor(partyCardAlpha);
        visitor(viewPartyCardsArea);
        visitor(viewPartyCardsBattle);
        visitor(viewEnemyCardDebug);
        visitor(viewPartyCardsEvent);
        visitor(messageAlpha);
        visitor(viewMessageArea);
        visitor(viewMessageBattle);
        visitor(viewMessageEvent);
        visitor(viewReferenceCards);
        visitor(fixedImagesMenuCards);
        visitor(fixedImagesCells);
        visitor(fixedImagesBackground);
        visitor(fixedImagesBattle);
        visitor(fixedImagesEvent);
        visitor(fixedImagesEventBackground);
        visitor(fixedImagesDefaultBackground);
        visitor(viewCards);
        visitor(viewBgImages);
        visitor(showGrid);
        visitor(areaSashT);
        visitor(areaSashB);
        visitor(areaSashL);
        visitor(areaSashR);
        visitor(importAreaSashL);
        visitor(importAreaSashR);
        visitor(areaSashV);
        visitor(importAreaSashV);
        visitor(flagSashL);
        visitor(flagSashR);
        visitor(flagSashV);
        visitor(flagsWidth);
        visitor(flagsHeight);
        visitor(menuCardSashL);
        visitor(menuCardSashR);
        visitor(enemyCardSashL);
        visitor(enemyCardSashR);
        visitor(backSashL);
        visitor(backSashR);
        visitor(bgImageSampleWidth);
        visitor(bgImageSampleHeight);
        visitor(cellPCNumberWidth);
        visitor(textCellVSashT);
        visitor(textCellVSashB);
        visitor(textCellHSashL);
        visitor(textCellHSashR);
        visitor(textCellPreviewSashL);
        visitor(textCellPreviewSashR);
        visitor(textCellPreviewWidth);
        visitor(textCellPreviewHeight);
        visitor(textCellBoxWidth);
        visitor(textCellBoxHeight);
        visitor(cardIdColumn);
        visitor(cardNameColumn);
        visitor(cardNumberColumn);
        visitor(cardDescriptionColumn);
        visitor(cardCountColumn);
        visitor(handCardIdColumn);
        visitor(handCardNameColumn);
        visitor(handCardNumberColumn);
        visitor(handCardDescriptionColumn);
        visitor(importCardIdColumn);
        visitor(importCardNameColumn);
        visitor(importCardNumberColumn);
        visitor(importCardDescriptionColumn);
        visitor(importCardCountColumn);
        visitor(importHandCardIdColumn);
        visitor(importHandCardNameColumn);
        visitor(importHandCardNumberColumn);
        visitor(importHandCardDescriptionColumn);
        visitor(mainCardsSortColumn);
        visitor(mainCardsSortDirection);
        visitor(handCardsSortColumn);
        visitor(handCardsSortDirection);
        visitor(importCardsSortColumn);
        visitor(importCardsSortDirection);
        visitor(importHandCardsSortColumn);
        visitor(importHandCardsSortDirection);
        visitor(linkCardMaskColor);
        visitor(negativeCardNameBorder);
        visitor(cardNameBorderingColorForWhite);
        visitor(cardNameBorderingColorForBlack);
        visitor(couponWidth);
        visitor(couponHeight);
        visitor(couponValueColumn);
        visitor(couponEventL);
        visitor(couponEventR);
        visitor(idColumn);
        visitor(valueNumberColumn);
        visitor(nameTableWidth);
        visitor(nameTableHeight);
        visitor(flagEventSashL);
        visitor(flagEventSashR);
        visitor(nameWidth);
        visitor(firesWidth);
        visitor(flagNameWidth);
        visitor(flagInitWidth);
        visitor(flagValueWidth);
        visitor(flagNameColumn);
        visitor(flagInitColumn);
        visitor(variableInitializationColumn);
        visitor(flagCountColumn);
        visitor(localFlagNameColumn);
        visitor(localFlagInitColumn);
        visitor(localVariableInitializationColumn);
        visitor(localFlagCountColumn);
        visitor(localVariablesSashL);
        visitor(localVariablesSashR);
        visitor(limitForNumberOfVariant);
        visitor(filesWidth);
        visitor(filesHeight);
        visitor(talkersWidth);
        visitor(motionsWidth);
        visitor(incrementalSearchBoxWidth);
        visitor(showMainToolBar);
        visitor(showSceneToolBar);
        visitor(showEventToolBar);
        visitor(flagCombiSashL);
        visitor(flagCombiSashR);
        visitor(useCoolBar);
        visitor(mainToolBarCustomSashL);
        visitor(mainToolBarCustomSashR);
        visitor(stepTopSashL);
        visitor(stepTopSashR);
        visitor(flagTopSashL);
        visitor(flagTopSashR);
        visitor(toolTipWidth);
        visitor(colorExampleWidth);
        visitor(buttonWidth);
        visitor(radioGroupSeparatorWidth);
        visitor(couponValueColumnWidth);
        visitor(idNameColumn);
        visitor(idCountColumn);
        visitor(couponSortColumn);
        visitor(couponSortDirection);
        visitor(gossipSortColumn);
        visitor(gossipSortDirection);
        visitor(completeStampSortColumn);
        visitor(completeStampSortDirection);
        visitor(keyCodeSortColumn);
        visitor(keyCodeSortDirection);
        visitor(cellNameSortColumn);
        visitor(cellNameSortDirection);
        visitor(cardGroupSortColumn);
        visitor(cardGroupSortDirection);
        visitor(authorNewAreaNameSashL);
        visitor(authorNewAreaNameSashR);
        visitor(historyScenarioNameColumn);
        visitor(historyScenarioPathColumn);
        visitor(executionPartyNameColumn);
        visitor(executionPartyYadoColumn);
        visitor(executionPartyEngineColumn);
        visitor(expressionAndTargetSashT);
        visitor(expressionAndTargetSashB);
        visitor(expressionFunctionAndArgsSashT);
        visitor(expressionFunctionAndArgsSashB);
        visitor(functionCallEditorWidth);
        visitor(functionCallEditorHeight);
        visitor(functionArgumentNameColumn);
        visitor(functionArgumentTypeColumn);
        visitor(functionArgumentValueColumn);
        visitor(expressionCheckingDelay);
        visitor(keyCodesByFeaturesFeatureColumn);
        visitor(keyCodesByFeaturesKeyCodeColumn);
        visitor(keyCodesByMotionsMotionColumn);
        visitor(keyCodesByMotionsElementColumn);
        visitor(keyCodesByMotionsKeyCodeColumn);
        visitor(elementOverrideNameColumn);
        visitor(elementOverrideTargetTypeColumn);
        visitor(keyCodesElementSashL);
        visitor(keyCodesElementSashR);
        visitor(partyMax);
        visitor(cardScaleMax);
        visitor(cardScaleMin);
        visitor(layerMax);
        visitor(posLeftMax);
        visitor(posTopMax);
        visitor(backWidthMax);
        visitor(backHeightMax);
        visitor(levelMax);
        visitor(castLevelMax);
        visitor(lifeMax);
        visitor(couponValueMax);
        visitor(physicalMax);
        visitor(mentalMax);
        visitor(skillLevelMax);
        visitor(useCountMax);
        visitor(priceMax);
        visitor(enhanceMax);
        visitor(roundMax);
        visitor(paralyzeMax);
        visitor(poisonMax);
        visitor(cardNumberMax);
        visitor(waitMax);
        visitor(uValueMax);
        visitor(skillPowerMax);
        visitor(beastMaxNest);
        visitor(flagInitValue);
        visitor(stepInitValue);
        visitor(stepCountMax);
        visitor(stepValueName);
        visitor(imageListWidth);
        visitor(imageListHeight);
        visitor(imageListForBgImagesWidth);
        visitor(imageListForBgImagesHeight);
        visitor(layerListWidth);
        visitor(layerListHeight);
        visitor(cardLife);
        visitor(cardDetails);
        visitor(cardsMarginX);
        visitor(cardsSpaceX);
        visitor(cardsMarginY);
        visitor(cardsSpaceY);
        visitor(cardsTitleSpace);
        visitor(cardsFocusLinePadding);
        visitor(cardsDefaultWrap);
        visitor(seKeyCodeSashL);
        visitor(seKeyCodeSashR);
        visitor(effectContentSEKeyCodeSashL);
        visitor(effectContentSEKeyCodeSashR);
        visitor(talkMainSashL);
        visitor(talkMainSashR);
        visitor(talkLeftSashL);
        visitor(talkLeftSashR);
        visitor(msgBackR);
        visitor(msgBackG);
        visitor(msgBackB);
        visitor(msgForeR);
        visitor(msgForeG);
        visitor(msgForeB);
        visitor(textTabs);
        visitor(messageLineColor1);
        visitor(messageLineColor2);
        visitor(messageBackColor);
        visitor(messageForeColor);
        visitor(messageHemColor);
        visitor(gridX);
        visitor(gridY);
        visitor(gridRange);
        visitor(gridColor);
        visitor(gridHighlightColor);
        visitor(enhanceMaxVal);
        visitor(enhanceHighVal);
        visitor(enhanceMiddleVal);
        visitor(enhanceColorMax);
        visitor(enhanceColorHigh);
        visitor(enhanceColorMiddle);
        visitor(enhanceColorLow);
        visitor(penaltyColorMax);
        visitor(penaltyColorHigh);
        visitor(penaltyColorMiddle);
        visitor(penaltyColorLow);
        visitor(textCellDefaultWidth);
        visitor(textCellDefaultHeight);
        visitor(textCellDefaultFontClassic);
        visitor(textCellDefaultFont);
        visitor(textCellDefaultFontSize);
        visitor(textCellDefaultColor);
        visitor(textCellDefaultBorderingColor);
        visitor(fontSizeMax);
        visitor(borderingWidthMax);
        visitor(colorCellDefaultWidth);
        visitor(colorCellDefaultHeight);
        visitor(colorCellDefaultColor1);
        visitor(colorCellDefaultColor2);
        visitor(noFileName);
        visitor(contentsOrder);
        visitor(contentsLock);
        visitor(contentsWrapIndices);
        visitor(contentsAutoOpen);
        visitor(contentsPutMode);
        visitor(contentsInsertFirst);
        visitor(showContentsGroupName);
        visitor(showEventContentDescription);
        visitor(showMotionDescription);
        visitor(contentsFloat);
        visitor(contentsAutoHide);
        visitor(comboListVisible);
        visitor(showCurrentValueOnTopAlways);
        visitor(showContentsBoxHeightWhenNoToolBar);
        visitor(showSummaryInAreaTable);
        visitor(saveTableViewSelection);
        visitor(clickIsOpenEvent);
        visitor(smoothingCard);
        visitor(ignorePathsWidth);
        visitor(menuSettingsHeight);
        visitor(settingListWidth);
        visitor(settingListHeight);
        visitor(importOptionMaterials);
        visitor(importOptionVariables);
        visitor(importOptionCasts);
        visitor(importOptionSkills);
        visitor(importOptionItems);
        visitor(importOptionBeasts);
        visitor(importOptionInfos);
        visitor(importOptionAreas);
        visitor(importOptionBattles);
        visitor(importOptionPackages);
        visitor(importOptionIncludedFiles);
        visitor(importOptionIncludedFilesWithoutIncluding);
        visitor(importOptionIncludedBgImages);
        visitor(importOptionHands);
        visitor(importOptionBeastsInMotions);
        visitor(importOptionOverwriteScenarioInfo);
        visitor(wallpaper);
        visitor(wallpaperStyle);
        visitor(wallColorR);
        visitor(wallColorG);
        visitor(wallColorB);
        visitor(bgImagesDefault);
        visitor(bgImageSettingsSashL);
        visitor(bgImageSettingsSashR);
        visitor(selectionKeyCodeSashL);
        visitor(selectionKeyCodeSashR);
        visitor(bgImageSettingsKeyCodesSashL);
        visitor(bgImageSettingsKeyCodesSashR);
        visitor(keyCodesByFeaturesKeyCodesByMotionSashL);
        visitor(keyCodesByFeaturesKeyCodesByMotionSashR);
        visitor(bgImageSelectionSashL);
        visitor(bgImageSelectionSashR);
        visitor(outerToolsSashL);
        visitor(outerToolsSashR);
        visitor(outerToolShortcutSashL);
        visitor(outerToolShortcutSashR);
        visitor(classicEnginesSashL);
        visitor(classicEnginesSashR);
        visitor(classicEnginesNameTypeSashL);
        visitor(classicEnginesNameTypeSashR);
        visitor(classicEngineShortcutSashL);
        visitor(classicEngineShortcutSashR);
        visitor(featureDefaultNameWidth);
        visitor(featureVariantNameWidth);
        visitor(featureManualNameWidth);
        visitor(eventTemplatesSashL);
        visitor(eventTemplatesSashR);
        visitor(eventTemplatesOfScenarioSashL);
        visitor(eventTemplatesOfScenarioSashR);
        visitor(eventTemplateShortcutSashL);
        visitor(eventTemplateShortcutSashR);
        visitor(eventTemplateOfScenarioShortcutSashL);
        visitor(eventTemplateOfScenarioShortcutSashR);
        visitor(scenarioTemplatesSashL);
        visitor(scenarioTemplatesSashR);
        visitor(templatesSashL);
        visitor(templatesSashR);
        visitor(toolsClassicEnginesSashL);
        visitor(toolsClassicEnginesSashR);
        visitor(selectionWidth);
        visitor(keyCodeWidth);
        visitor(roundWidth);
        visitor(scenarioPath);
        visitor(tempPath);
        visitor(backupPath);
        visitor(backupEnabled);
        visitor(backupIntervalType);
        visitor(backupInterval);
        visitor(backupIntervalEdit);
        visitor(backupCount);
        visitor(autoSave);
        visitor(backupRefAuthor);
        visitor(backupBeforeSaveEnabled);
        visitor(backupBeforeSavePath);
        visitor(backupBeforeSaveDir);
        visitor(backupArchived);
        visitor(ignoreMenuSashL);
        visitor(ignoreMenuSashR);
        visitor(highValueOfImageControl);
        visitor(showDuplicateViewInToolBar);
        visitor(openHistories);
        visitor(scenarioBookmarks);
        visitor(historyMax);
        visitor(historySnipLength);
        visitor(removeScenarioHistoryNonExistingWithPriority);
        visitor(lastScenario);
        visitor(importHistory);
        visitor(importBookmarks);
        visitor(searchResultTableWidth);
        visitor(searchResultTableHeight);
        visitor(engine);
        visitor(engineScript);
        visitor(enginePath);
        visitor(pythonCommand);
        visitor(lastExecutedParty);
        visitor(executedParties);
        visitor(executedPartyBookmarks);
        visitor(executedPartiesMax);
        visitor(removePartyHistoryNonExistingWithPriority);
        visitor(dataDir);
        visitor(findEnginePath);
        visitor(defaultSkin);
        visitor(defaultSkinName);
        visitor(defaultAuthor);
        visitor(lastSkinType);
        visitor(lastSkinName);
        visitor(saveSkinName);
        visitor(replaceClassicResourceExtension);
        visitor(tableExtension);
        visitor(midiExtension);
        visitor(waveExtension);
        visitor(classicEngineRegex);
        visitor(classicDataDirRegex);
        visitor(classicMatchKey);
        visitor(expandXMLs);
        visitor(xmlCopy);
        visitor(showSpNature);
        visitor(saveChangedOnlyWsn);
        visitor(saveChangedOnly);
        visitor(xmlFileNameIsIDOnly);
        visitor(archiveInNewThread);
        visitor(saveInnerImagePath);
        visitor(linkCard);
        visitor(traceDirectories);
        visitor(logicalSort);
        visitor(copyDesc);
        visitor(refCardsAtEditBgImage);
        visitor(showImagePreview);
        visitor(maskCardImagePreview);
        visitor(ignoreBackgroundInRange);
        visitor(classicStyleTree);
        visitor(adjustContentName);
        visitor(showHandMark);
        visitor(showHandMarkAlways);
        visitor(showEventTreeMark);
        visitor(showEventTreeMarkAlways);
        visitor(ignoreEmptyStart);
        visitor(showSkillCardLevel);
        visitor(switchTabWheel);
        visitor(closeTabWithMiddleClick);
        visitor(openTabAtRightOfCurrentTab);
        visitor(showCloseButtonAllTab);
        visitor(cautionBeforeReplace);
        visitor(startIncrementalSearchWhenKeyDown);
        visitor(radarStyleParams);
        visitor(showAptitudeOnCastCardEditor);
        visitor(showCardListHeader);
        visitor(showCardListTitle);
        visitor(applyDialogsBeforeSave);
        visitor(useNamesAfterStandard);
        visitor(expandChooserItems);
        visitor(restorePositionOfEventTreeView);
        visitor(selectVariableWithTree);
        visitor(useCurrentStartName);
        visitor(autoUpdateJpy1File);
        visitor(straightEventTreeView);
        visitor(eventTreeSlope);
        visitor(forceIndentBranchContent);
        visitor(showTerminalMark);
        visitor(showTargetStartLineNumber);
        visitor(clickIconIsStartEdit);
        visitor(showAreaDirTree);
        visitor(incrementNewAreaName);
        visitor(saveNeedChanged);
        visitor(canVanishWorkAreaInMainWindow);
        visitor(switchFixedWithCheckBox);
        visitor(resizingImageWithRatio);
        visitor(drawXORSelectionLine);
        visitor(showSceneViewSelectionFilter);
        visitor(showItemNumberOfSceneAndEventView);
        visitor(showCardStatusUnderPointer);
        visitor(showStatusTime);
        visitor(showEventTreeLineNumber);
        visitor(showEventTreeDetail);
        visitor(editTriggerType);
        visitor(specifySelectedEventTree);
        visitor(spinnerUpDownWithWheel);
        visitor(soundPlayType);
        visitor(soundEffectPlayType);
        visitor(bgmVolume);
        visitor(seVolume);
        visitor(cardWirthPyDefaultSoundFont);
        visitor(loopCountMax);
        visitor(fadeInMax);
        visitor(excludeCardSizeImage);
        visitor(selectionColumnsMax);
        visitor(showInheritBackground);
        visitor(detailAreaWidth);
        visitor(searchPlan);
        visitor(searchIDKind);
        visitor(searchHistories);
        visitor(replaceHistories);
        visitor(searchHistoryMax);
        visitor(replaceRangeSashL);
        visitor(replaceRangeSashR);
        visitor(replaceTextNotIgnoreCase);
        visitor(replaceTextRegExp);
        visitor(replaceTextWildcard);
        visitor(replaceTextExactMatch);
        visitor(replaceTextIgnoreReturnCode);
        visitor(grepDir);
        visitor(grepDirHistories);
        visitor(grepSubDir);
        visitor(searchResultRefreshCount);
        visitor(searchResultRealtime);
        visitor(showRouteOfSearchResult);
        visitor(searchErrorTargetVersion);
        visitor(replaceTextSummary);
        visitor(replaceTextScenario);
        visitor(replaceTextAuthor);
        visitor(replaceTextMessage);
        visitor(replaceTextCardName);
        visitor(replaceTextCardDescription);
        visitor(replaceTextEventText);
        visitor(replaceTextStart);
        visitor(replaceTextVariableName);
        visitor(replaceTextVariableValue);
        visitor(replaceTextExpression);
        visitor(replaceTextCoupon);
        visitor(replaceTextGossip);
        visitor(replaceTextEndScenario);
        visitor(replaceTextAreaName);
        visitor(replaceTextKeyCode);
        visitor(replaceTextCellName);
        visitor(replaceTextCardGroup);
        visitor(replaceTextFile);
        visitor(replaceTextComment);
        visitor(replaceTextJptx);
        visitor(replaceTextTextFile);
        visitor(plainTextFileExtensions);
        visitor(replaceNameCoupon);
        visitor(replaceNameGossip);
        visitor(replaceNameEndScenario);
        visitor(replaceNameKeyCode);
        visitor(replaceNameCellName);
        visitor(replaceNameCardGroup);
        visitor(searchContentsStart);
        visitor(searchContentsStartBattle);
        visitor(searchContentsEnd);
        visitor(searchContentsEndBadEnd);
        visitor(searchContentsChangeArea);
        visitor(searchContentsChangeBgImage);
        visitor(searchContentsEffect);
        visitor(searchContentsEffectBreak);
        visitor(searchContentsLinkStart);
        visitor(searchContentsLinkPackage);
        visitor(searchContentsTalkMessage);
        visitor(searchContentsTalkDialog);
        visitor(searchContentsPlayBgm);
        visitor(searchContentsPlaySound);
        visitor(searchContentsWait);
        visitor(searchContentsElapseTime);
        visitor(searchContentsCallStart);
        visitor(searchContentsCallPackage);
        visitor(searchContentsBranchFlag);
        visitor(searchContentsBranchMultiStep);
        visitor(searchContentsBranchStep);
        visitor(searchContentsBranchSelect);
        visitor(searchContentsBranchAbility);
        visitor(searchContentsBranchRandom);
        visitor(searchContentsBranchLevel);
        visitor(searchContentsBranchStatus);
        visitor(searchContentsBranchPartyNumber);
        visitor(searchContentsBranchArea);
        visitor(searchContentsBranchBattle);
        visitor(searchContentsBranchIsBattle);
        visitor(searchContentsBranchCast);
        visitor(searchContentsBranchItem);
        visitor(searchContentsBranchSkill);
        visitor(searchContentsBranchInfo);
        visitor(searchContentsBranchBeast);
        visitor(searchContentsBranchMoney);
        visitor(searchContentsBranchCoupon);
        visitor(searchContentsBranchCompleteStamp);
        visitor(searchContentsBranchGossip);
        visitor(searchContentsSetFlag);
        visitor(searchContentsSetStep);
        visitor(searchContentsSetStepUp);
        visitor(searchContentsSetStepDown);
        visitor(searchContentsReverseFlag);
        visitor(searchContentsCheckFlag);
        visitor(searchContentsGetCast);
        visitor(searchContentsGetItem);
        visitor(searchContentsGetSkill);
        visitor(searchContentsGetInfo);
        visitor(searchContentsGetBeast);
        visitor(searchContentsGetMoney);
        visitor(searchContentsGetCoupon);
        visitor(searchContentsGetCompleteStamp);
        visitor(searchContentsGetGossip);
        visitor(searchContentsLoseCast);
        visitor(searchContentsLoseItem);
        visitor(searchContentsLoseSkill);
        visitor(searchContentsLoseInfo);
        visitor(searchContentsLoseBeast);
        visitor(searchContentsLoseMoney);
        visitor(searchContentsLoseCoupon);
        visitor(searchContentsLoseCompleteStamp);
        visitor(searchContentsLoseGossip);
        visitor(searchContentsShowParty);
        visitor(searchContentsHideParty);
        visitor(searchContentsRedisplay);
        visitor(searchContentsSubstituteStep);
        visitor(searchContentsSubstituteFlag);
        visitor(searchContentsBranchStepCmp);
        visitor(searchContentsBranchFlagCmp);
        visitor(searchContentsBranchRandomSelect);
        visitor(searchContentsBranchKeyCode);
        visitor(searchContentsCheckStep);
        visitor(searchContentsBranchRound);
        visitor(searchContentsMoveBgImage);
        visitor(searchContentsReplaceBgImage);
        visitor(searchContentsLoseBgImage);
        visitor(searchContentsBranchMultiCoupon);
        visitor(searchContentsBranchMultiRandom);
        visitor(searchContentsMoveCard);
        visitor(searchContentsChangeEnvironment);
        visitor(searchContentsBranchVariant);
        visitor(searchContentsSetVariant);
        visitor(searchContentsCheckVariant);
        visitor(searchContentsCustomXmlContent);
        visitor(searchUnusedFlag);
        visitor(searchUnusedStep);
        visitor(searchUnusedVariant);
        visitor(searchUnusedArea);
        visitor(searchUnusedBattle);
        visitor(searchUnusedPackage);
        visitor(searchUnusedCast);
        visitor(searchUnusedSkill);
        visitor(searchUnusedItem);
        visitor(searchUnusedBeast);
        visitor(searchUnusedInfo);
        visitor(searchUnusedStart);
        visitor(searchUnusedPath);
        visitor(searchOpenDialog);
        visitor(searchResultColumnMain);
        visitor(searchResultColumnParent);
        visitor(searchResultColumnCouponCount);
        visitor(searchResultColumnErrorDesc);
        visitor(searchResultColumnScenario);
        visitor(incrementalSearchType);
        visitor(usedCouponToCombo);
        visitor(numberStepToVariantThreshold);
        visitor(newScenarioName);
        visitor(newAreaName);
        visitor(flagTrues);
        visitor(flagFalses);
        visitor(bgImageSettings);
        visitor(standardCoupons);
        visitor(standardCouponsForValued);
        visitor(standardKeyCodes);
        visitor(keyCodesByFeatures);
        visitor(keyCodesByMotions);
        visitor(standardSelections);
        visitor(settingsWithSkinTypes);
        visitor(outerTools);
        visitor(ignorePaths);
        visitor(selectedArchiveFilter);
        visitor(classicEngines);
        visitor(addNewClassicEngine);
        visitor(skinTypesByClassicEngines);
        visitor(drawCountOfUseOfStart);
        visitor(drawContentTreeLine);
        visitor(drawContentWarnings);
        visitor(drawContentIconOnDetail);
        visitor(commentBoxDistance);
        visitor(warningImageWidth);
        visitor(warningImageWidthForTable);
        visitor(warningImageWidthForTree);
        visitor(warningImageWidthForCardList);
        visitor(warningImageColor);
        visitor(doubleIO);
        visitor(cautionToScenarioLoadErrors);
        visitor(reconstruction);
        visitor(branchRandomFailureFormat);
        visitor(openLastScenario);
        visitor(imageCache);
        visitor(savedSound);
        visitor(connContentTools);
        visitor(previewAlpha);
        visitor(previewMaxWidth);
        visitor(previewMaxHeight);
        visitor(undoMaxMainView);
        visitor(undoMaxEvent);
        visitor(undoMaxEtc);
        visitor(undoMaxLimit);
        visitor(undoMaxReplace);
        visitor(dialogStatus);
        visitor(showInputGuide);
        visitor(showSummaryPreview);
        visitor(floatMessagePreview);
        visitor(useMessageWindowColorInTextContentDialog);
        visitor(showDialogPreview);
        visitor(showMessagePreview);
        visitor(messageVarKindColumn);
        visitor(messageVarValueColumn);
        visitor(textVarKindColumn);
        visitor(textVarValueColumn);
        visitor(messageVarTableHeight);
        visitor(messageVarSelected);
        visitor(messageVarUnselected);
        visitor(messageVarRandom);
        visitor(messageVarCard);
        visitor(messageVarRef);
        visitor(messageVarTeam);
        visitor(messageVarYado);
        visitor(messageVarSelectedPlayerCardNumber);
        visitor(messageVarPlayerCardName);
        visitor(messageVarPlayerCardNameDefault);
        visitor(selectedPlayerCardName);
        visitor(showVariableValuesInEventText);
        visitor(editSelectionWithCombo);
        visitor(scenarioTemplates);
        visitor(defaultScenarioTemplate);
        visitor(defaultIsTemplate);
        visitor(createScenarioDir);
        visitor(eventTemplates);
        visitor(scriptVarTableHeight);
        visitor(archivePath);
        visitor(contentConversionGroups);
        visitor(contentInitializers);
        visitor(mainToolBar);
        visitor(gentleAngleEventTree);
    }
};

}  // namespace cwxeditor::cwx::variables
