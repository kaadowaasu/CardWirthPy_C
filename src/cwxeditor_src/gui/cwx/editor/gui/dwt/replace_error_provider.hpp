#pragma once

#include "cwxeditor_src/gui/cwx/editor/gui/dwt/replacedialog.hpp"
#include "cwxeditor_src/core/cwx/props.hpp"
#include "cwxeditor_src/core/cwx/skin.hpp"

namespace cwxeditor::cwx::editor::gui::dwt::replaceerror {

namespace lib = ::cwxeditor::lib;
namespace props = ::cwxeditor::cwx::props;
namespace skin = ::cwxeditor::cwx::skin;
using String = lib::String;
using StringView = lib::StringView;

struct ProviderConfig {
    const props::CProps* properties{};
    skin::Skin::Ptr skin;
    String defaultTargetVersion{"1.50"};
    std::vector<String> ignorePaths;
    std::size_t batchSize{32U};
};

/// Dのreplacedialog.dのsearchErrorImpl相当。 共通
/// の中止可能な一括APIで、モデル構造の警告とシナリオファイルの診断を報告する。
class Provider final {
public:
    explicit Provider(ProviderConfig config = {});
    [[nodiscard]] replacedialog::ReplaceProviderResult operator()(
        replacedialog::ReplaceQueryContext& context) const;

private:
    ProviderConfig config_;
};

/// Providerが受け取る固定の表記。wsn:<n>またはclassic:<version>。
[[nodiscard]] std::vector<lib::ChoiceItem> versionChoices();

}  // namespace cwxeditor::cwx::editor::gui::dwt::replaceerror
