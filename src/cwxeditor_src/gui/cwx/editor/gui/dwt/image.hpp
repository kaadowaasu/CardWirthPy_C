#pragma once

// cwx/editor/gui/dwt/image.dに対応する、移植可能なC++20の資源管理。

#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/lib/graphics.hpp"
#include "cwxeditor_src/lib/services.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::image {

namespace lib = ::cwxeditor::lib;
namespace types = ::cwxeditor::cwx::types;

using String = lib::String;
using StringView = lib::StringView;
using Image = lib::ResourceId;
using Cursor = Image;
using ImageArray = std::vector<Image>;

class Images final {
public:
    explicit Images(String appPath, lib::Services services = {});
    ~Images();

    Images(const Images&) = delete;
    Images& operator=(const Images&) = delete;
    Images(Images&&) = delete;
    Images& operator=(Images&&) = delete;

    void disposeImages() noexcept;
    // 組み込み、追加の絶対パス、独自パスで同じ資源読み込み処理を使う。
    [[nodiscard]] Image load(StringView path, std::uint32_t targetScale = 0) {
        return imgd(path,targetScale);
    }

    // DのMenuItemごとのSWT Imageと異なり、C++では共有管理先
    // に借用IDをキャッシュし、管理先の破棄時に解放する。 Services::tasks
    // でシェル資源の抽出を始め、Services::dispatcherで画像
    // 転送を公開する。 completedはUIスレッドで呼ぶ。still_needed
    // で、閉じた・開き直したメニューの画像を転送前に破棄できる。
    void requestApplicationIcon(const std::filesystem::path& executable,
                                lib::Size requested_size,
                                std::function<void(Image)> completed,
                                std::function<bool()> still_needed = {});

    // この管理先が所有する復号済みフレームの範囲。未知のIDには範囲を返さない。
    [[nodiscard]] lib::Size imageSize(Image image) const noexcept;

    // DのDirectoryWindowの切り取り状態に使う、キャッシュ済みの半透明画像。
    [[nodiscard]] Image skeleton(Image source);

    [[nodiscard]] Image emptyIcon();
    [[nodiscard]] Image app();
    [[nodiscard]] ImageArray icon();
    [[nodiscard]] Image largeIcon();

    [[nodiscard]] Image toolBar();
    [[nodiscard]] Image toolGroup();
    [[nodiscard]] Image text();
    [[nodiscard]] Image classicEngine();
    [[nodiscard]] Image warning();
    [[nodiscard]] Image summary();

    [[nodiscard]] Image cards();
    [[nodiscard]] Image cardsWithFlag();
    [[nodiscard]] Image runAwayableEnemyCard();
    [[nodiscard]] Image runAwayableEnemyCardWithFlag();
    [[nodiscard]] Image backs();
    [[nodiscard]] Image backsWithFlag();
    [[nodiscard]] Image colorCell();
    [[nodiscard]] Image colorCellWithFlag();
    [[nodiscard]] Image textCell();
    [[nodiscard]] Image textCellWithFlag();
    [[nodiscard]] Image pcCell();
    [[nodiscard]] Image pcCellWithFlag();
    [[nodiscard]] Image imageFont();

    [[nodiscard]] Image inheritBackground();
    [[nodiscard]] Image notInheritBackground();
    [[nodiscard]] Image bgm();
    [[nodiscard]] Image se();
    [[nodiscard]] Image unknown();

    [[nodiscard]] Image folder();
    [[nodiscard]] Image scenario();
    [[nodiscard]] Image areaDir();
    [[nodiscard]] Image area();
    [[nodiscard]] Image battle();
    [[nodiscard]] Image packages();
    [[nodiscard]] Image packageDup();
    [[nodiscard]] Image startArea();

    [[nodiscard]] Image areaSceneView();
    [[nodiscard]] Image areaSceneViewDup();
    [[nodiscard]] Image areaEventTreeView();
    [[nodiscard]] Image areaEventTreeViewDup();
    [[nodiscard]] Image battleSceneView();
    [[nodiscard]] Image battleSceneViewDup();
    [[nodiscard]] Image battleEventTreeView();
    [[nodiscard]] Image battleEventTreeViewDup();

    [[nodiscard]] Image yado();
    [[nodiscard]] Image debugYado();
    [[nodiscard]] Image team();
    [[nodiscard]] Image playerCardName();
    [[nodiscard]] Image selectedPlayerCardNumber();
    [[nodiscard]] Image randomValue();

    [[nodiscard]] Image casts();
    [[nodiscard]] Image skill();
    [[nodiscard]] Image skillDup();
    [[nodiscard]] Image item();
    [[nodiscard]] Image itemDup();
    [[nodiscard]] Image beast();
    [[nodiscard]] Image beastDup();
    [[nodiscard]] Image info();
    [[nodiscard]] Image playerCard();

    [[nodiscard]] Image flagDir();
    [[nodiscard]] Image flag();
    [[nodiscard]] Image step();
    [[nodiscard]] Image variant();
    [[nodiscard]] Image variantRef();
    [[nodiscard]] Image localFlag();
    [[nodiscard]] Image localStep();
    [[nodiscard]] Image localVariant();
    [[nodiscard]] Image functions();

    [[nodiscard]] Image couponNormal();
    [[nodiscard]] Image couponPlus();
    [[nodiscard]] Image couponMinus();
    [[nodiscard]] Image couponHigh();
    [[nodiscard]] Image couponDelete();
    [[nodiscard]] Image couponSystem();
    [[nodiscard]] Image couponDur();
    [[nodiscard]] Image couponDurBattle();
    [[nodiscard]] Image gossip();
    [[nodiscard]] Image endScenario();
    [[nodiscard]] Image evtArrow();
    [[nodiscard]] Image evtAutoOpen();
    [[nodiscard]] Image evtInsertFirst();

    [[nodiscard]] Image couponView();
    [[nodiscard]] Image gossipView();
    [[nodiscard]] Image completeStampView();
    [[nodiscard]] Image keyCodeView();
    [[nodiscard]] Image cellNameView();
    [[nodiscard]] Image cardGroupView();

    [[nodiscard]] Image actionCard(types::ActionCardType type);
    [[nodiscard]] Image content(types::CType type);
    [[nodiscard]] Image msnDelete();
    [[nodiscard]] Image motion(types::MType type);
    [[nodiscard]] Image element(types::Element element);
    [[nodiscard]] Image elementHearing();
    [[nodiscard]] Image elementVision();
    [[nodiscard]] Image elementElectronic();
    [[nodiscard]] Image talker(types::Talker talker);

    [[nodiscard]] Image hand();
    [[nodiscard]] Image handWith(std::uint32_t targetScale);
    [[nodiscard]] Image handEmpty();
    [[nodiscard]] Image handEmptyWith(std::uint32_t targetScale);
    [[nodiscard]] Image eventTree();
    [[nodiscard]] Image eventTreeWith(std::uint32_t targetScale);
    [[nodiscard]] Image eventTreeEmpty();
    [[nodiscard]] Image eventTreeEmptyWith(std::uint32_t targetScale);
    [[nodiscard]] Image eventTreeAnd();
    [[nodiscard]] Image eventTreeSelected();
    [[nodiscard]] Image eventTreeAndSelected();
    [[nodiscard]] Image defStart();
    [[nodiscard]] Image keyCode();
    [[nodiscard]] Image round();
    [[nodiscard]] Image newIgnition();
    [[nodiscard]] Image expandTree();
    [[nodiscard]] Image foldTree();
    [[nodiscard]] Image showEventTreeDetail();
    [[nodiscard]] Image showEventTreeLineNumber();
    [[nodiscard]] Image addRound();
    [[nodiscard]] Image delRound();

    [[nodiscard]] Image keyCodeTiming(types::FKCKind kind);
    [[nodiscard]] Image addCoupon();
    [[nodiscard]] Image altCoupon();
    [[nodiscard]] Image delCoupon();
    [[nodiscard]] Image addKeyCode();
    [[nodiscard]] Image delKeyCode();
    [[nodiscard]] Image setBeast();
    [[nodiscard]] Image sound();
    [[nodiscard]] Image setTalkerCoupon();
    [[nodiscard]] Image color(char32_t colorCode);
    [[nodiscard]] Image scTalker(types::Talker talker);
    [[nodiscard]] Image scRef();
    [[nodiscard]] Image scTeam();
    [[nodiscard]] Image scYado();
    [[nodiscard]] Image summaryFile();
    [[nodiscard]] Image scenarioArchive();
    [[nodiscard]] Image classic();
    [[nodiscard]] Image script();
    [[nodiscard]] Image editSceneBattle();
    [[nodiscard]] Image editEventBattle();

    [[nodiscard]] Image menu(types::MenuID id);
    [[nodiscard]] Cursor cursor(types::CType type);

private:
    struct ImgRegKey {
        String key;
        std::uint32_t dpi{};
        std::uint32_t asset_dpi{};

        [[nodiscard]] bool operator==(const ImgRegKey& other) const noexcept {
            return dpi == other.dpi && asset_dpi==other.asset_dpi && key == other.key;
        }
    };

    struct ImgRegKeyHash {
        [[nodiscard]] std::size_t operator()(const ImgRegKey& value) const noexcept {
            const auto keyHash = std::hash<String>{}(value.key);
            const auto dpiHash = std::hash<std::uint32_t>{}(value.dpi);
            return std::hash<std::uint32_t>{}(value.asset_dpi) ^ keyHash ^ (dpiHash + static_cast<std::size_t>(0x9e3779b9U) +
                              (keyHash << 6U) + (keyHash >> 2U));
        }
    };

    struct ApplicationIconKey {
        String executable;
        std::uint32_t width{}, height{};
        std::uint64_t last_modified_ticks{};
        [[nodiscard]] bool operator==(const ApplicationIconKey& other) const noexcept {
            return executable == other.executable && width == other.width && height == other.height &&
                   last_modified_ticks == other.last_modified_ticks;
        }
    };

    struct ApplicationIconKeyHash {
        [[nodiscard]] std::size_t operator()(const ApplicationIconKey& value) const noexcept {
            std::size_t seed = std::hash<String>{}(value.executable);
            const auto combine = [&seed](const std::uint32_t part) {
                seed ^= std::hash<std::uint32_t>{}(part) + static_cast<std::size_t>(0x9e3779b9U) +
                        (seed << 6U) + (seed >> 2U);
            };
            combine(value.width);
            combine(value.height);
            seed ^= std::hash<std::uint64_t>{}(value.last_modified_ticks) +
                    static_cast<std::size_t>(0x9e3779b9U) + (seed << 6U) + (seed >> 2U);
            return seed;
        }
    };

    [[nodiscard]] Image imgd(StringView path, std::uint32_t targetScale = 0);
    [[nodiscard]] Image loadImage(StringView path, std::uint32_t targetScale);
    [[nodiscard]] std::uint32_t defaultScale() const noexcept;
    void initIcon();
    struct ApplicationIconState;

    String app_path_;
    lib::Services services_;
    std::unordered_map<ImgRegKey, Image, ImgRegKeyHash> image_registry_;
    std::unordered_map<types::CType, Cursor> cursor_registry_;
    ImageArray icon_;
    Image large_icon_{};
    std::unordered_set<Image> owned_images_;
    std::shared_ptr<ApplicationIconState> application_icon_state_;
    std::unordered_map<Image, lib::ImageData> decoded_images_;
    std::unordered_map<Image, Image> skeleton_images_;
};


}  // namespace cwxeditor::cwx::editor::gui::dwt::image
