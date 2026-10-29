#pragma once

// cwx/editor/gui/dwt/dprops.d
// の移植可能なC++20実装。 ダイアロ
// グが使う小さな共有窓口を保ち、SWTの
// オブジェクトは共通の描画・部品で表す。
// ネイティブGUIの型は公開しない。

#include "cwxeditor_src/core/cwx/props.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/system.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/core/cwx/variables.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/graphics.hpp"
#include "cwxeditor_src/lib/services.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/properties.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/image.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cardwirth::platform {
class Storage;
}

namespace cwxeditor::cwx::editor::gui::dwt::dprops {

namespace lib = ::cwxeditor::lib;
namespace props = ::cwxeditor::cwx::props;
namespace structs = ::cwxeditor::cwx::structs;
namespace types = ::cwxeditor::cwx::types;
namespace variables = ::cwxeditor::cwx::variables;

using String = lib::String;
using StringView = lib::StringView;
using Image = lib::ResourceId;

// vectorを使う編集部品には、入力時に明示的なスナップショッ
// トを渡す。 モデル側の選択部品はSharedSliceを使い、
// スキン別設定の記述子コピーでも Dと同じ外側の要素共有を保つ。
template <typename T>
[[nodiscard]] lib::Array<typename lib::SharedSlice<T>::value_type>
arraySnapshot(const lib::SharedSlice<T>& source) {
    using Value = typename lib::SharedSlice<T>::value_type;
    lib::Array<Value> result;
    result.reserve(source.size());
    for (std::size_t index = 0; index < source.size(); ++index)
        result.push_back(source[index]);
    return result;
}

/// DWTのImagesに対応する資源管理。
/// 画像は必要時に復号する。 作った資源ID
/// をすべて所有し、破棄時にGraphicsBackend
/// 経由で解放する。 バックエンドがnull
/// でも有効とし、ヘッドレステストと
/// 同じく例外なしでID 0を返す。
class Images final {
public:
    Images(String appPath, lib::Services services = {});
    ~Images();

    Images(const Images&) = delete;
    Images& operator=(const Images&) = delete;
    Images(Images&&) = delete;
    Images& operator=(Images&&) = delete;

    [[nodiscard]] const String& appPath() const noexcept { return app_path_; }
    [[nodiscard]] const lib::Services& services() const noexcept { return services_; }
    // 共有の表示にはDの全アイコン名が必要。 別のキャッシュや
    // 対応表を作らず、この同じ所有権付きの資源管理を借用する。
    [[nodiscard]] image::Images& standardRegistry() noexcept { return standard_images_; }

    [[nodiscard]] Image load(StringView path);
    [[nodiscard]] Image load(const std::filesystem::path& path);
    void requestApplicationIcon(const std::filesystem::path& executable,
                                lib::Size requested_size,
                                std::function<void(Image)> completed,
                                std::function<bool()> still_needed = {});

    [[nodiscard]] Image element(types::Element element);
    [[nodiscard]] Image elementHearing();
    [[nodiscard]] Image elementVision();
    [[nodiscard]] Image elementElectronic();

    // 標準DWTアイコンの管理窓口。ここで公開し、 各ダイアログでDの画像名
    // の対応を重複させたりMainWindowへ依存したりしないようにする。
    [[nodiscard]] Image motion(types::MType type);
    [[nodiscard]] Image msnDelete();
    [[nodiscard]] Image menu(types::MenuID id);
    [[nodiscard]] Image flag();
    [[nodiscard]] Image step();
    [[nodiscard]] Image variant();
    [[nodiscard]] Image variantRef();
    [[nodiscard]] Image functions();
    [[nodiscard]] Image setBeast() const;
    [[nodiscard]] Image warning() const;
    [[nodiscard]] Image script() const;
    [[nodiscard]] Image talker(types::Talker talker);
    [[nodiscard]] Image scTalker(types::Talker talker);
    [[nodiscard]] Image playerCardName();
    [[nodiscard]] Image selectedPlayerCardNumber();
    [[nodiscard]] Image color(char32_t code);
    [[nodiscard]] Image scRef();
    [[nodiscard]] Image scTeam();
    [[nodiscard]] Image scYado();
    [[nodiscard]] Image cards();
    [[nodiscard]] Image backs();
    [[nodiscard]] Image bgm();
    [[nodiscard]] Image se();
    [[nodiscard]] Image folder();
    [[nodiscard]] Image unknown();
    // ImportOptionDialogもDのCLabelアイコンと同じ標準資源を使う。
    [[nodiscard]] Image text();
    [[nodiscard]] Image flagDir();
    [[nodiscard]] Image casts();
    [[nodiscard]] Image skill();
    [[nodiscard]] Image item();
    [[nodiscard]] Image beast();
    [[nodiscard]] Image info();
    [[nodiscard]] Image area();
    [[nodiscard]] Image battle();
    [[nodiscard]] Image packages();
    [[nodiscard]] Image coupon(::cwxeditor::cwx::features::CouponType type,
                               std::int32_t value);

    // LIFEカードの重ね表示にも、Editor資源と同じ所有権付きの管理先を使う。
    [[nodiscard]] Image handWith(std::uint32_t scale, bool empty = false);
    [[nodiscard]] Image eventTreeWith(std::uint32_t scale, bool empty = false);
    [[nodiscard]] lib::Size imageSize(Image image) const noexcept;

    /// 組み込み要素の資源パスを上書きする。次の参照時に遅延読み込みする。
    void setElementPath(types::Element element, String path);
    void setElementHearingPath(String path);
    void setElementVisionPath(String path);
    void setElementElectronicPath(String path);

    /// すべてのキャッシュ資源を解放し、管理オブジェクト自体は再利用可能なままにする。
    void disposeImages() noexcept;

private:
    String app_path_;
    lib::Services services_;
    std::unordered_map<types::Element, String> element_paths_;
    String hearing_path_;
    String vision_path_;
    String electronic_path_;
    // 資源の読み込みで、共有画像キャッシュを更新する。
    mutable image::Images standard_images_;

    [[nodiscard]] String defaultPath(StringView filename) const;
    [[nodiscard]] Image loadPath(const std::filesystem::path& path);
};

using ElementOverrideTable = std::unordered_map<types::Element, structs::ElementOverride>;

struct ElementImageCacheEntry {
    Image image{};
    String path;
};
using ElementImageCache = std::unordered_map<types::Element, ElementImageCacheEntry>;

inline constexpr StringView PRESET_IMAGE_SCHEMA = "presetimage://";

struct PresetImageInfo {
    String file;
    String name;
    Image image{};
};

struct DwtPoint {
    std::int32_t x{};
    std::int32_t y{};
};

struct DwtRgb {
    std::uint8_t red{};
    std::uint8_t green{};
    std::uint8_t blue{};
};

struct DwtRgbResult {
    DwtRgb color;
    std::int32_t alpha{};
};

enum class DwtFontStyle : std::int32_t {
    Normal = 0,
    Bold = 1,
    Italic = 2,
};

struct DwtFontData {
    String name;
    std::int32_t height{};
    DwtFontStyle style{DwtFontStyle::Normal};
};

/// 共通のプロパティ接続。取得・反映のコールバ
/// ックで独自部品にも使え、 下の補助オーバ
/// ーロードでは標準のlib部品へ接続する。
struct PropertyBinding {
    String key;
    lib::Action pull;
    lib::Action push;
    // 追加のC++接続API。コピー間で終了状態を共有し、外された接続
    // を 部品や処理中のスナップショットから呼び出せないようにする。
    std::shared_ptr<bool> active{std::make_shared<bool>(true)};
};

class Props final {
public:
    using ChangeListener = lib::Action;
    using MenuBuilder = std::function<String(const props::CProps&, types::MenuID)>;

    Props(String confFilePath, props::CProps& parent, lib::Services services = {},
          cardwirth::platform::Storage* storage = nullptr);
    Props(props::CProps& parent, lib::Services services = {});
    Props(String appPath, const system::System& sys,
          String confFilePath = {}, lib::Services services = {});
    ~Props();

    Props(const Props&) = delete;
    Props& operator=(const Props&) = delete;
    Props(Props&&) = delete;
    Props& operator=(Props&&) = delete;

    [[nodiscard]] String enginePath() const;
    [[nodiscard]] String tempPath() const;
    [[nodiscard]] String backupPath() const;
    [[nodiscard]] String backupBeforeSavePath() const;

    [[nodiscard]] const props::CProps& parent() const noexcept { return *parent_; }
    [[nodiscard]] const system::System& sys() const noexcept { return parent_->sys(); }
    [[nodiscard]] Images& images() noexcept { return images_; }
    [[nodiscard]] const Images& images() const noexcept { return images_; }
    [[nodiscard]] lib::Owner<const props::Msgs> msgs() const noexcept { return parent_->msgs(); }
    [[nodiscard]] const props::Msgs& requiredMsgs() const { return parent_->requiredMsgs(); }
    [[nodiscard]] const props::Looks& looks() const noexcept { return parent_->looks(); }
    /// よく使うetc設定への互換参照。 実際の所有者
    /// はflex()が返す完全なFlexProps。
    [[nodiscard]] variables::FlexEtcProps& var() noexcept { return var_; }
    [[nodiscard]] const variables::FlexEtcProps& var() const noexcept { return var_; }
    [[nodiscard]] properties::FlexProps& flex() noexcept { return flex_; }
    [[nodiscard]] const properties::FlexProps& flex() const noexcept { return flex_; }
    [[nodiscard]] cardwirth::platform::Storage* storage() const noexcept {
        return storage_;
    }
    // 借用側は通知をまたいだ後でこれを確認し、 Propsやそ
    // こから受け取ったポインターへ再び触れてよいか判断できる。
    [[nodiscard]] std::weak_ptr<void> lifetimeToken() const noexcept {
        return lifetime_;
    }
    [[nodiscard]] const lib::Services& services() const noexcept { return services_; }

    [[nodiscard]] const String& configFilePath() const noexcept { return config_file_path_; }
    [[nodiscard]] String toAppAbs(StringView path) const;

    [[nodiscard]] String buildTool(types::MenuID id) const;
    [[nodiscard]] String buildMenu(types::MenuID id) const;
    void setToolBuilder(MenuBuilder builder) { tool_builder_ = std::move(builder); }
    void setMenuBuilder(MenuBuilder builder) { menu_builder_ = std::move(builder); }

    [[nodiscard]] bool targetVersion(const summary::Summary* summ, StringView ver) const;
    [[nodiscard]] bool targetVersion(bool legacy, StringView ver) const;
    [[nodiscard]] bool isTargetVersion(const summary::Summary* summ, StringView ver) const;
    [[nodiscard]] bool isTargetVersion(bool legacy, StringView dataVersion,
                                       StringView wsnVer) const;

    [[nodiscard]] std::int32_t s(std::int32_t value) const noexcept;
    [[nodiscard]] structs::CInsets s(structs::CInsets insets) const noexcept;
    [[nodiscard]] std::uint32_t imageScalePercent() const noexcept;
    [[nodiscard]] std::uint32_t drawingScalePercent() const noexcept;
    [[nodiscard]] float imageScaleFactor() const noexcept;
    [[nodiscard]] float drawingScaleFactor() const noexcept;
    [[nodiscard]] std::uint32_t drawingScale() const noexcept;
    [[nodiscard]] std::uint32_t drawingScaleForImage(const summary::Summary* summ) const;
    [[nodiscard]] std::int32_t ds(std::int32_t value) const noexcept;
    [[nodiscard]] structs::CPoint ds(structs::CPoint point) const noexcept;
    [[nodiscard]] structs::CSize ds(structs::CSize size) const noexcept;
    [[nodiscard]] structs::CRect ds(structs::CRect rect) const noexcept;
    [[nodiscard]] structs::CInsets ds(structs::CInsets insets) const noexcept;
    [[nodiscard]] structs::CFont ds(structs::CFont font) const noexcept;

    [[nodiscard]] structs::CFont adjustFont(structs::CFont font) const;
    [[nodiscard]] String adjustFont(StringView face) const;

    [[nodiscard]] lib::SharedSlice<structs::BgImageS> bgImagesDefault(StringView type) const;
    [[nodiscard]] lib::SharedSlice<structs::BgImageSetting> bgImageSettings(StringView type) const;
    [[nodiscard]] lib::SharedSlice<String> standardSelections(StringView type) const;
    [[nodiscard]] lib::SharedSlice<String> standardKeyCodes(StringView type) const;
    [[nodiscard]] lib::SharedSlice<structs::KeyCodeByFeature> keyCodesByFeatures(StringView type) const;
    [[nodiscard]] lib::SharedSlice<structs::KeyCodeByMotion> keyCodesByMotions(StringView type) const;
    [[nodiscard]] ElementOverrideTable elementOverrides(StringView type) const;

    [[nodiscard]] Image elementImage(const ElementOverrideTable& table,
                                     types::Element element,
                                     ElementImageCache& imageCache);
    [[nodiscard]] Image elementImage(const structs::ElementOverride& elementOverride,
                                     ElementImageCache& imageCache);

    // プロパティ編集の一括処理と変更通知。
    [[nodiscard]] bool modified() const;
    [[nodiscard]] bool validate(lib::Array<String>* errors = nullptr) const;
    // 保存済み設定を、現在の窓口と編集の基準
    // 状態の両方へ読み直す。 IPCの受信側
    // は、通常の変更通知より前にこれを使う。
    [[nodiscard]] bool reload();
    [[nodiscard]] bool apply();
    void cancel();
    void addChangeListener(ChangeListener listener);
    void notifyChanged(bool committed = false);

    // 汎用部品と標準の共通部品への接続。
    [[nodiscard]] std::size_t bindProperty(PropertyBinding binding);
    [[nodiscard]] bool bindTextProperty(String key, const std::shared_ptr<lib::TextEdit>& widget);
    [[nodiscard]] bool bindCheckProperty(String key, const std::shared_ptr<lib::CheckBox>& widget);
    [[nodiscard]] bool bindNumberProperty(String key, const std::shared_ptr<lib::NumberEdit>& widget);
    // 独自の共通ダイアログで、DのAbsDialog/setupShellと同じ寸法保
    // 存を行う。 返された解除処理は、ダイアログのラッパーを破棄する前に実行すること。
    [[nodiscard]] std::function<void()> bindDialogSize(
        const std::shared_ptr<lib::Window>& window, structs::DSize& size);
    void removeBinding(std::size_t bindingId);
    void syncBindings();

private:
    std::unique_ptr<props::CProps> owned_parent_;
    props::CProps* parent_{};
    String config_file_path_;
    lib::Services services_;
    cardwirth::platform::Storage* storage_{};
    Images images_;
    properties::FlexProps flex_;
    properties::FlexProps committed_flex_;
    variables::FlexEtcProps& var_;
    variables::FlexEtcProps committed_;
    String committed_serialized_;
    std::vector<PropertyBinding> bindings_;
    std::shared_ptr<void> lifetime_{std::make_shared<int>(0)};
    std::vector<ChangeListener> change_listeners_;
    MenuBuilder tool_builder_;
    MenuBuilder menu_builder_;

    void initialize(StringView confFilePath);
    void loadConfiguration();
    void selectLanguage();
    bool visitBindings(bool push);
    [[nodiscard]] String serializeProperties() const;
    [[nodiscard]] bool setTextValue(StringView key, StringView value);
    [[nodiscard]] bool setBoolValue(StringView key, bool value);
    [[nodiscard]] bool setNumberValue(StringView key, double value);
    [[nodiscard]] String textValue(StringView key) const;
    [[nodiscard]] bool boolValue(StringView key, bool& value) const;
    [[nodiscard]] bool numberValue(StringView key, double& value) const;
};

[[nodiscard]] lib::Array<PresetImageInfo> presetIcons(Props& prop);
[[nodiscard]] DwtPoint dwtData(structs::CPoint value) noexcept;
[[nodiscard]] DwtPoint dwtData(structs::CSize value) noexcept;
[[nodiscard]] DwtRgbResult dwtData(structs::CRGB value) noexcept;
[[nodiscard]] DwtRgb dwtData(structs::CRGB value, std::int32_t& alpha) noexcept;
[[nodiscard]] DwtFontData dwtData(structs::CFont value);

}  // namespace cwxeditor::cwx::editor::gui::dwt::dprops
