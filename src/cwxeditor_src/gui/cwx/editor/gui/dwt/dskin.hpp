#pragma once

// cwx/editor/gui/dwt/dskin.d
// の移植可能なC++20実装。 std::shared_ptr
// で所有し、UTF-8文字列を受け渡し、画像
// ・アイコンは共通のサービスから取得する。

#include "cwxeditor_src/core/cwx/skin.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dprops.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/imagelistwindow.hpp"
#include "cwxeditor_src/lib/services.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace cwxeditor::cwx::editor::gui::dwt::dskin {

namespace lib = ::cwxeditor::lib;
namespace structs = ::cwxeditor::cwx::structs;
namespace types = ::cwxeditor::cwx::types;

using Props = dprops::Props;
using Skin = ::cwxeditor::cwx::skin::Skin;
using Summary = ::cwxeditor::cwx::summary::Summary;
using ClassicEngine = structs::ClassicEngine;
using Enhance = types::Enhance;
using Mentality = types::Mentality;
using MaskType = ::cwxeditor::cwx::skin::MaskType;
using String = lib::String;
using StringView = lib::StringView;
using SkinPtr = Skin::Ptr;
using ImageDataWithScale =
    ::cwxeditor::cwx::editor::gui::dwt::imagelistwindow::ImageDataWithScale;
using SyncExec = std::function<void(std::function<void()>)>;

[[nodiscard]] SkinPtr createClassicSkin(const Props& prop,
                                        const ClassicEngine& engine);
[[nodiscard]] bool findCWPy(Props& prop, StringView scenarioPath);
[[nodiscard]] SkinPtr findSkin2(const Props& prop, StringView type,
                                StringView name);
[[nodiscard]] bool hasSkin(const Props& prop, StringView type);
[[nodiscard]] Skin::Table skinTable(const Props& prop);
[[nodiscard]] lib::Array<String> findSkinTypes(const Props& prop);

[[nodiscard]] ImageDataWithScale imgd(StringView path,
                                      std::uint32_t targetScale,
                                      MaskType maskType, lib::Services services = {});
[[nodiscard]] ImageDataWithScale imgr(StringView legacyEngine,
                                      StringView resourceName,
                                      std::uint32_t targetScale,
                                      MaskType maskType, lib::Services services = {});

[[nodiscard]] ImageDataWithScale loadIcon(
    StringView executable, int width, int height, SyncExec syncExec = {},
    lib::Services services = {});
// 一度だけ解決・検証し、Services::tasksで抽出する。 完了はServices::dispatcher
// へ送り、資源管理が所有するIDを返す。
void loadIconAsync(dprops::Images& images, StringView executable,
                   int width, int height, lib::Services services,
                   std::function<void(lib::ResourceId)> completed,
                   std::function<bool()> still_needed = {});

[[nodiscard]] ImageDataWithScale loadBgImage(const Props& prop,
                                             const Skin& skin,
                                             const Summary& summary,
                                             StringView path,
                                             std::uint32_t targetScale);

[[nodiscard]] ImageDataWithScale summary(const Skin& skin,
                                         std::uint32_t targetScale,
                                         lib::Services services = {});
[[nodiscard]] ImageDataWithScale menuCard(const Skin& skin,
                                          std::uint32_t targetScale,
                                          lib::Services services = {});
[[nodiscard]] ImageDataWithScale castCard(const Skin& skin,
                                          std::uint32_t targetScale,
                                          lib::Services services = {});
[[nodiscard]] ImageDataWithScale castCardInjury(const Skin& skin,
                                                std::uint32_t targetScale);
[[nodiscard]] ImageDataWithScale castCardDanger(const Skin& skin,
                                                std::uint32_t targetScale);
[[nodiscard]] ImageDataWithScale castCardFaint(const Skin& skin,
                                               std::uint32_t targetScale);
[[nodiscard]] ImageDataWithScale castCardBind(const Skin& skin,
                                              std::uint32_t targetScale);
[[nodiscard]] ImageDataWithScale castCardParaly(const Skin& skin,
                                                std::uint32_t targetScale);
[[nodiscard]] ImageDataWithScale castCardPetrif(const Skin& skin,
                                                std::uint32_t targetScale);
[[nodiscard]] ImageDataWithScale castCardSleep(const Skin& skin,
                                               std::uint32_t targetScale);
[[nodiscard]] ImageDataWithScale lifeBar(const Skin& skin,
                                         std::uint32_t targetScale);
[[nodiscard]] ImageDataWithScale lifeGuage(const Skin& skin,
                                           std::uint32_t targetScale);
[[nodiscard]] ImageDataWithScale lifeGuage2(const Skin& skin,
                                            std::uint32_t targetScale);
[[nodiscard]] ImageDataWithScale lifeGuage2Mask(const Skin& skin,
                                                std::uint32_t targetScale);
[[nodiscard]] ImageDataWithScale enhanceUp(const Skin& skin,
                                           std::uint32_t targetScale,
                                           Enhance value);
[[nodiscard]] ImageDataWithScale enhanceDown(const Skin& skin,
                                             std::uint32_t targetScale,
                                             Enhance value);
[[nodiscard]] ImageDataWithScale mentality(const Skin& skin,
                                            std::uint32_t targetScale,
                                            Mentality value);
[[nodiscard]] ImageDataWithScale bind(const Skin& skin,
                                      std::uint32_t targetScale);
[[nodiscard]] ImageDataWithScale silence(const Skin& skin,
                                         std::uint32_t targetScale);
[[nodiscard]] ImageDataWithScale faceUp(const Skin& skin,
                                        std::uint32_t targetScale);
[[nodiscard]] ImageDataWithScale antiMagic(const Skin& skin,
                                           std::uint32_t targetScale);
[[nodiscard]] ImageDataWithScale paralyze(const Skin& skin,
                                          std::uint32_t targetScale);
[[nodiscard]] ImageDataWithScale poison(const Skin& skin,
                                        std::uint32_t targetScale);
[[nodiscard]] ImageDataWithScale summon(const Skin& skin,
                                        std::uint32_t targetScale);
[[nodiscard]] ImageDataWithScale itemCard(const Skin& skin,
                                          std::uint32_t targetScale);
[[nodiscard]] ImageDataWithScale skillCard(const Skin& skin,
                                           std::uint32_t targetScale);
[[nodiscard]] ImageDataWithScale beastCard(const Skin& skin,
                                           std::uint32_t targetScale);
[[nodiscard]] ImageDataWithScale optionCard(const Skin& skin,
                                            std::uint32_t targetScale);
[[nodiscard]] ImageDataWithScale infoCard(const Skin& skin,
                                          std::uint32_t targetScale);
[[nodiscard]] ImageDataWithScale cardHold(const Skin& skin,
                                          std::uint32_t targetScale);
[[nodiscard]] ImageDataWithScale cardPenalty(const Skin& skin,
                                             std::uint32_t targetScale);
[[nodiscard]] ImageDataWithScale rare(const Skin& skin,
                                      std::uint32_t targetScale);
[[nodiscard]] ImageDataWithScale premier(const Skin& skin,
                                         std::uint32_t targetScale);
[[nodiscard]] ImageDataWithScale aptVeryHigh(const Skin& skin,
                                             std::uint32_t targetScale);
[[nodiscard]] ImageDataWithScale aptHigh(const Skin& skin,
                                         std::uint32_t targetScale);
[[nodiscard]] ImageDataWithScale aptNormal(const Skin& skin,
                                           std::uint32_t targetScale);
[[nodiscard]] ImageDataWithScale aptLow(const Skin& skin,
                                        std::uint32_t targetScale);
[[nodiscard]] ImageDataWithScale use0(const Skin& skin,
                                      std::uint32_t targetScale);
[[nodiscard]] ImageDataWithScale use1(const Skin& skin,
                                      std::uint32_t targetScale);
[[nodiscard]] ImageDataWithScale use2(const Skin& skin,
                                      std::uint32_t targetScale);
[[nodiscard]] ImageDataWithScale use3(const Skin& skin,
                                      std::uint32_t targetScale);
[[nodiscard]] ImageDataWithScale use4(const Skin& skin,
                                      std::uint32_t targetScale);

[[nodiscard]] ImageDataWithScale spChar(const Skin& skin,
                                        std::uint32_t targetScale,
                                        char32_t character,
                                        lib::Services services = {});

}  // namespace cwxeditor::cwx::editor::gui::dwt::dskin
