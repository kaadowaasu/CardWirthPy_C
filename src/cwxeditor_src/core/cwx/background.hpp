#pragma once

// cwx/background.dのC++20
// 移植。 Dの背景セルは参照型なので、
// C++ではlib::Ownerで保持
// する。 親へのリンクとCWXパスの
// 検索結果は、Dのオブジェクト構造と
// 同じく所有権を持たない参照とする。

#include "cwxeditor_src/core/cwx/msgutils.hpp"
#include "cwxeditor_src/core/cwx/msgs.hpp"
#include "cwxeditor_src/core/cwx/path.hpp"
#include "cwxeditor_src/core/cwx/props.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/system.hpp"
#include "cwxeditor_src/core/cwx/textholder.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/core/cwx/usecounter.hpp"
#include "cwxeditor_src/core/cwx/xml.hpp"
#include "cwxeditor_src/lib/array_slice.hpp"
#include "cwxeditor_src/lib/portable.hpp"
#include "cwxeditor_src/lib/nullable_string.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::card {
class XMLOption;
}  // namespace cwxeditor::cwx::card

namespace cwxeditor::cwx::usecounter {
class UseCounter;
struct FlagId;
struct StepId;
struct VariantId;
class FlagUser;
class StepUser;
class VariantUser;
class PathUser;
class CellNameUser;
}  // namespace cwxeditor::cwx::usecounter

namespace cwxeditor::cwx::background {

using ::cwxeditor::cwx::path::CWXPath;
using ::cwxeditor::cwx::props::CProps;
using ::cwxeditor::cwx::structs::CRGB;
using ::cwxeditor::cwx::system::XMLInfo;
using ::cwxeditor::cwx::types::BlendMode;
using ::cwxeditor::cwx::types::BorderingType;
using ::cwxeditor::cwx::types::GradientDir;
using ::cwxeditor::cwx::types::Smoothing;
using ::cwxeditor::cwx::types::UpdateType;
using XNode = ::cwxeditor::cwx::xml::XNode;
using XMLOption = ::cwxeditor::cwx::card::XMLOption;
using UseCounter = ::cwxeditor::cwx::usecounter::UseCounter;
using FlagId = ::cwxeditor::cwx::usecounter::FlagId;
using StepId = ::cwxeditor::cwx::usecounter::StepId;
using VariantId = ::cwxeditor::cwx::usecounter::VariantId;
using UseCounterPtr = ::cwxeditor::lib::Owner<UseCounter>;

class BgImage;
class BackgroundOwner;
class BgImageOwner;
class TextPath;
using BgImagePtr = ::cwxeditor::lib::Owner<BgImage>;
using BgImageArray = ::cwxeditor::lib::SharedArray<BgImagePtr>;
using ChangeHandler = ::cwxeditor::lib::Callback<void()>;

/// XMLノードが背景セルではない場合の例外。
class AreaException final : public std::runtime_error {
public:
    explicit AreaException(::cwxeditor::lib::String message)
        : std::runtime_error(std::move(message)) {}
};

/// 背景セル共通の状態と、XML・パスの処理。
class BgImage : public CWXPath,
                public ::cwxeditor::cwx::path::Commentable,
                public std::enable_shared_from_this<BgImage> {
public:
    inline static constexpr std::string_view XML_NAME_M = "BgImages";
    inline static constexpr std::int32_t LAYER_BACK_CELL = 0;

    BgImage(const BgImage&) = delete;
    BgImage& operator=(const BgImage&) = delete;
    ~BgImage() override;

    [[nodiscard]] lib::Owner<CWXPath> retainForCallback() override {
        return std::static_pointer_cast<CWXPath>(weak_from_this().lock());
    }

    [[nodiscard]] virtual bool equals(const BgImage& other) const noexcept;
    [[nodiscard]] bool opEquals(const BgImage& other) const noexcept {
        return equals(other);
    }
    [[nodiscard]] friend bool operator==(const BgImage& left,
                                         const BgImage& right) noexcept {
        return left.equals(right);
    }
    [[nodiscard]] friend bool operator!=(const BgImage& left,
                                         const BgImage& right) noexcept {
        return !left.equals(right);
    }

    /// 別のセルから共通部分をコピーする。派生クラスは固有部分もコピーする。
    virtual void deepCopy(const BgImage& other);

    [[nodiscard]] virtual ::cwxeditor::lib::String name(
        const CProps& props) const;
    [[nodiscard]] ::cwxeditor::lib::String name() const;
    [[nodiscard]] virtual BgImagePtr dup() const = 0;

    void changeHandler(ChangeHandler change) {
        [[maybe_unused]] const auto receiverLifetime = retainLifetime();
        if (change) {
            change_ = std::make_shared<ChangeHandler>(std::move(change));
        } else {
            change_.reset();
        }
    }
    void changed() override;

    [[nodiscard]] UseCounterPtr useCounter() const noexcept {
        return use_counter_ ? use_counter_->global() : UseCounterPtr{};
    }
    virtual void setUseCounter(UseCounterPtr counter,
                               CWXPath* counter_owner = nullptr) noexcept;
    virtual void removeUseCounter() noexcept;
    [[nodiscard]] CWXPath* ucOwner() const noexcept { return uc_owner_; }

    [[nodiscard]] ::cwxeditor::lib::NullableString flag() const;
    void flag(::cwxeditor::lib::String value);
    template <typename T> requires (
        std::is_same_v<std::remove_cvref_t<T>, ::cwxeditor::lib::NullableString> ||
        std::is_same_v<std::remove_cvref_t<T>, std::nullptr_t>)
    void flag(T&& value) {
        [[maybe_unused]] const auto receiverLifetime = retainLifetime();
        assignFlag(::cwxeditor::lib::NullableString(std::forward<T>(value)));
    }
    [[nodiscard]] bool mask() const noexcept { return mask_; }
    void mask(bool value) noexcept;
    [[nodiscard]] std::int32_t x() const noexcept { return x_; }
    void x(std::int32_t value) noexcept;
    [[nodiscard]] std::int32_t y() const noexcept { return y_; }
    void y(std::int32_t value) noexcept;
    [[nodiscard]] std::int32_t width() const noexcept { return width_; }
    void width(std::int32_t value) noexcept;
    [[nodiscard]] std::int32_t height() const noexcept { return height_; }
    void height(std::int32_t value) noexcept;
    [[nodiscard]] ::cwxeditor::lib::String cellName() const;
    void cellName(::cwxeditor::lib::String value);
    [[nodiscard]] std::int32_t layer() const noexcept { return layer_; }
    void layer(std::int32_t value) noexcept;
    [[nodiscard]] ::cwxeditor::lib::String comment() const noexcept override {
        return comment_;
    }
    void comment(::cwxeditor::lib::String value) override;

    [[nodiscard]] virtual ::cwxeditor::lib::String connectedFile() const {
        return {};
    }

    [[nodiscard]] static BgImageArray bgImagesFromNode(
        XNode& node, bool can_inherit, const XMLInfo& version);
    [[nodiscard]] static BgImageArray bgImagesFromNode(XNode& node,
                                                        bool can_inherit);
    static void toNode(const BgImageArray& cells, bool can_inherit,
                       XNode& parent, const XMLOption& option);
    static void toNode(const BgImageArray& cells, bool can_inherit,
                       XNode& parent);

    virtual void toNode(XNode& node, const XMLOption& option) const = 0;
    void toNode(XNode& node) const;

protected:
    friend class TextPath;

    void assignFlag(::cwxeditor::lib::NullableString value);
    // 変更通知のハンドラーが、設定関数の書き込み完了前に
    // 呼び出し元の最後の共有参照を解放する場合がある。
    [[nodiscard]] BgImagePtr retainLifetime() noexcept {
        return weak_from_this().lock();
    }
    [[nodiscard]] ::cwxeditor::lib::Owner<const BgImage>
    retainLifetime() const noexcept {
        return weak_from_this().lock();
    }
    void invalidateLifetime() noexcept {
        if (background_lifetime_) {
            background_lifetime_->alive.store(false, std::memory_order_release);
        }
    }
    BgImage(::cwxeditor::lib::StringView flag, std::int32_t x, std::int32_t y,
            std::int32_t width, std::int32_t height, bool mask);

    void dupImpl(BgImage& cell) const;
    void toNodeCommon(XNode& element, bool use_mask) const;
    void fromNodeCommon(XNode& node);
    virtual void toNodeImpl(XNode& node) const = 0;

public:
    static void appendEmptyToNode(XNode& node, const XMLOption& option);
    static void appendEmptyToNode(XNode& node);
    [[nodiscard]] static BgImagePtr createFromNode(XNode& node,
                                                    const XMLInfo& version);
    [[nodiscard]] static BgImagePtr createFromNode(XNode& node);

    /// 所有権を持たないコンテナーへのリンクを設定・解除する。
    void owner(BackgroundOwner* value) noexcept;
    void owner(BackgroundOwner* value,
               std::weak_ptr<path::OwnerLifetimeToken> lifetime) noexcept;
    /// 所有者のトークンが無効化された後の終了処理で使う、同一性だ
    /// けの逆参照検査。 所有者のポインターを参照先へたどらない。
    [[nodiscard]] bool ownedBy(
        BackgroundOwner* value,
        const std::weak_ptr<path::OwnerLifetimeToken>& lifetime) const noexcept {
        return owner_.matchesBorrowed(value, lifetime);
    }
    [[nodiscard]] BackgroundOwner* owner() const noexcept {
        const auto owner = owner_.lock();
        return owner.get();
    }

    [[nodiscard]] ::cwxeditor::lib::String cwxPath(bool id) override;
    [[nodiscard]] ::cwxeditor::lib::Owner<CWXPath> findCWXPath(
        std::string_view path) override;
    [[nodiscard]] ::cwxeditor::lib::Array<
        ::cwxeditor::lib::Observer<CWXPath>>
    cwxChilds() override;
    [[nodiscard]] ::cwxeditor::lib::Observer<CWXPath> cwxParent() override;

protected:
    /// 共有セルは本来の制御ブロックを保持する。キャッシュされた代理オブジェ
    /// クトは ネイティブのスタック上のセル専用で、寿命トークンで保護する。
    [[nodiscard]] ::cwxeditor::lib::Owner<CWXPath> selfOwner();

    bool mask_ = false;
    std::int32_t x_ = 0;
    std::int32_t y_ = 0;
    std::int32_t width_ = 0;
    std::int32_t height_ = 0;
    std::int32_t layer_ = LAYER_BACK_CELL;
    ::cwxeditor::lib::String comment_;
    // 通知間で呼び出し可能オブジェクトの可変状態を保つ。呼び出し中は
    // 局所的に保持し、 コールバックが自分を置き換えても安全にする。
    std::shared_ptr<ChangeHandler> change_;
    UseCounterPtr use_counter_;
    ::cwxeditor::lib::Owner<::cwxeditor::cwx::usecounter::FlagUser> flag_user_;
    ::cwxeditor::lib::Owner<::cwxeditor::cwx::usecounter::CellNameUser>
        cell_name_user_;
    CWXPath* uc_owner_ = nullptr;
    path::OwnerLink<BackgroundOwner> owner_;
    ::cwxeditor::lib::Owner<CWXPath> self_owner_proxy_;
    std::shared_ptr<path::OwnerLifetimeToken> background_lifetime_{
        std::make_shared<path::OwnerLifetimeToken>()};
};

/// 背景画像セル。
class ImageCell final : public BgImage {
public:
    inline static constexpr std::string_view XML_NAME = "BgImage";

    ImageCell();
    ~ImageCell() override;
    ImageCell(::cwxeditor::lib::String path,
              ::cwxeditor::lib::StringView flag, std::int32_t x, std::int32_t y,
              std::int32_t width, std::int32_t height, bool mask);

    [[nodiscard]] bool equals(const BgImage& other) const noexcept override;
    void deepCopy(const BgImage& other) override;
    [[nodiscard]] ::cwxeditor::lib::String name(
        const CProps& props) const override;
    [[nodiscard]] BgImagePtr dup() const override;

    [[nodiscard]] ::cwxeditor::lib::String path() const;
    void path(::cwxeditor::lib::String value);
    [[nodiscard]] Smoothing smoothing() const noexcept { return smoothing_; }
    void smoothing(Smoothing value) noexcept;
    [[nodiscard]] ::cwxeditor::lib::String connectedFile() const override;

    void setUseCounter(UseCounterPtr counter,
                       CWXPath* counter_owner = nullptr) noexcept override;
    void removeUseCounter() noexcept override;
    void toNode(XNode& node, const XMLOption& option) const override;
    [[nodiscard]] static ::cwxeditor::lib::Owner<ImageCell> createFromNode(
        XNode& node, const XMLInfo& version);
    [[nodiscard]] static ::cwxeditor::lib::Owner<ImageCell> createFromNode(
        XNode& node);

private:
    void toNodeImpl(XNode& node) const override;
    Smoothing smoothing_ = Smoothing::Default;
    ::cwxeditor::lib::Owner<::cwxeditor::cwx::usecounter::PathUser> path_user_;
};

/// テキストセル（CardWirth 1.50）。
class TextCell final : public BgImage,
                       public ::cwxeditor::cwx::textholder::ISimpleTextHolder {
public:
    inline static constexpr std::string_view XML_NAME = "TextCell";

    TextCell();
    ~TextCell() override;
    TextCell(::cwxeditor::lib::String text,
             ::cwxeditor::lib::String font_name, std::uint32_t size,
             CRGB color, bool bold, bool italic, bool underline, bool strike,
             bool vertical, bool antialias, BorderingType bordering_type,
             CRGB bordering_color, std::uint32_t bordering_width,
             UpdateType update_type, ::cwxeditor::lib::StringView flag,
             std::int32_t x, std::int32_t y, std::int32_t width,
             std::int32_t height, bool mask);

    [[nodiscard]] bool equals(const BgImage& other) const noexcept override;
    void deepCopy(const BgImage& other) override;
    [[nodiscard]] ::cwxeditor::lib::String name(
        const CProps& props) const override;
    [[nodiscard]] BgImagePtr dup() const override;

    [[nodiscard]] ::cwxeditor::lib::String text() const noexcept override {
        return text_;
    }
    void text(std::string_view value) override;
    [[nodiscard]] const ::cwxeditor::lib::String& fontName() const noexcept {
        return font_name_;
    }
    void fontName(::cwxeditor::lib::String value);
    [[nodiscard]] std::uint32_t size() const noexcept { return size_; }
    void size(std::uint32_t value) noexcept;
    [[nodiscard]] const CRGB& color() const noexcept { return color_; }
    void color(CRGB value) noexcept;
    [[nodiscard]] bool bold() const noexcept { return bold_; }
    void bold(bool value) noexcept;
    [[nodiscard]] bool italic() const noexcept { return italic_; }
    void italic(bool value) noexcept;
    [[nodiscard]] bool underline() const noexcept { return underline_; }
    void underline(bool value) noexcept;
    [[nodiscard]] bool strike() const noexcept { return strike_; }
    void strike(bool value) noexcept;
    [[nodiscard]] bool vertical() const noexcept { return vertical_; }
    void vertical(bool value) noexcept;
    [[nodiscard]] bool antialias() const noexcept { return antialias_; }
    void antialias(bool value) noexcept;
    [[nodiscard]] BorderingType borderingType() const noexcept {
        return bordering_type_;
    }
    void borderingType(BorderingType value) noexcept;
    [[nodiscard]] const CRGB& borderingColor() const noexcept {
        return bordering_color_;
    }
    void borderingColor(CRGB value) noexcept;
    [[nodiscard]] std::uint32_t borderingWidth() const noexcept {
        return bordering_width_;
    }
    void borderingWidth(std::uint32_t value) noexcept;
    [[nodiscard]] UpdateType updateType() const noexcept { return update_type_; }
    void updateType(UpdateType value) noexcept;

    [[nodiscard]] CWXPath* ucOwner() const noexcept { return BgImage::ucOwner(); }
    void setUseCounter(UseCounterPtr counter,
                       CWXPath* counter_owner = nullptr) noexcept override;
    void removeUseCounter() noexcept override;

    [[nodiscard]] std::vector<std::string> flagsInText() const override;
    [[nodiscard]] std::vector<std::string> stepsInText() const override;
    [[nodiscard]] std::vector<std::string> variantsInText() const override;
    [[nodiscard]] std::vector<std::string> fontsInText() const;
    [[nodiscard]] std::vector<char> colorsInText() const;
    [[nodiscard]] std::vector<char> namesInText() const;
    void changeInText(std::size_t index, std::string_view id) override;
    void changeFlagInText(std::size_t index, std::string_view id) override;
    void changeStepInText(std::size_t index, std::string_view id) override;
    void changeVariantInText(std::size_t index, std::string_view id) override;
    void changePathInText(std::size_t index, std::string_view id) override;

    template <typename Id>
    void changeInText(std::size_t index, const Id& id) {
        [[maybe_unused]] const auto receiverLifetime = retainLifetime();
        using I = std::remove_cvref_t<Id>;
        const auto value = idToString(id);
        if constexpr (std::is_same_v<I, FlagId>) {
            changeFlagInText(index, value);
        } else if constexpr (std::is_same_v<I, StepId>) {
            changeStepInText(index, value);
        } else if constexpr (std::is_same_v<I, VariantId>) {
            changeVariantInText(index, value);
        } else {
            changeInText(index, value);
        }
    }

    void rebuildTextUsers();
    void rebuildTextUsers(const std::vector<std::string>& flags,
                          const std::vector<std::string>& steps,
                          const std::vector<std::string>& variants,
                          const std::vector<std::string>& fonts);
    bool renameFlagInText(std::string_view oldId, std::string_view newId);
    bool renameStepInText(std::string_view oldId, std::string_view newId);
    bool renameVariantInText(std::string_view oldId, std::string_view newId);
    bool renameFontInText(std::string_view oldId, std::string_view newId);
    void removeTextUsers() noexcept;

    void toNode(XNode& node, const XMLOption& option) const override;
    [[nodiscard]] static ::cwxeditor::lib::Owner<TextCell> createFromNode(
        XNode& node, const XMLInfo& version);
    [[nodiscard]] static ::cwxeditor::lib::Owner<TextCell> createFromNode(
        XNode& node);
    [[nodiscard]] ::cwxeditor::lib::Owner<CWXPath> findCWXPath(
        std::string_view path) override;

private:
    friend class TextPath;
    void toNodeImpl(XNode& node) const override;
    template <typename Id>
    static ::cwxeditor::lib::String idToString(const Id& id) {
        if constexpr (requires { id.toString(); }) {
            return id.toString();
        } else if constexpr (requires { id.to_string(); }) {
            return id.to_string();
        } else if constexpr (requires { id.value(); }) {
            return id.value();
        } else if constexpr (std::is_convertible_v<Id, std::string_view>) {
            return ::cwxeditor::lib::String(std::string_view(id));
        } else if constexpr (std::is_convertible_v<Id, ::cwxeditor::lib::String>) {
            return static_cast<::cwxeditor::lib::String>(id);
        } else {
            return {};
        }
    }

    ::cwxeditor::lib::String text_;
    ::cwxeditor::lib::String font_name_;
    std::uint32_t size_ = 18;
    CRGB color_{0, 0, 0, 255};
    bool bold_ = false;
    bool italic_ = false;
    bool underline_ = false;
    bool strike_ = false;
    bool vertical_ = false;
    bool antialias_ = false;
    BorderingType bordering_type_ = BorderingType::None;
    CRGB bordering_color_{255, 255, 255, 255};
    std::uint32_t bordering_width_ = 1;
    UpdateType update_type_ = UpdateType::Variables;
    ::cwxeditor::lib::Owner<TextPath> text_path_;
    ::cwxeditor::lib::Array<::cwxeditor::lib::Owner<
        ::cwxeditor::cwx::usecounter::FlagUser>> text_flag_users_;
    ::cwxeditor::lib::Array<::cwxeditor::lib::Owner<
        ::cwxeditor::cwx::usecounter::StepUser>> text_step_users_;
    ::cwxeditor::lib::Array<::cwxeditor::lib::Owner<
        ::cwxeditor::cwx::usecounter::VariantUser>> text_variant_users_;
    ::cwxeditor::lib::Array<::cwxeditor::lib::Owner<
        ::cwxeditor::cwx::usecounter::PathUser>> text_path_users_;
};

/// TextCellのSimpleTextHolderから公開するCWXの子要素。
class TextPath final : public CWXPath,
                       public std::enable_shared_from_this<TextPath> {
public:
    explicit TextPath(TextCell* owner);
    ~TextPath() override;
    TextPath(const TextPath&) = delete;
    TextPath& operator=(const TextPath&) = delete;
    TextPath(TextPath&&) = delete;
    TextPath& operator=(TextPath&&) = delete;

    [[nodiscard]] ::cwxeditor::lib::String cwxPath(bool id) override;
    [[nodiscard]] ::cwxeditor::lib::Owner<CWXPath> findCWXPath(
        std::string_view path) override;
    [[nodiscard]] ::cwxeditor::lib::Array<
        ::cwxeditor::lib::Observer<CWXPath>>
    cwxChilds() override {
        return {};
    }
    [[nodiscard]] ::cwxeditor::lib::Observer<CWXPath> cwxParent() override;
    void changed() override;

private:
    friend class TextCell;
    [[nodiscard]] TextCell* liveOwner() const noexcept;
    TextCell* owner_ = nullptr;
    std::weak_ptr<path::OwnerLifetimeToken> owner_lifetime_;
    // ネイティブのスタック上のTextPathはこのインスタンスを借用するため
    // 、無効化トークンが必要。 Dのクラス参照では、この寿命の差は生じない。
    std::shared_ptr<path::OwnerLifetimeToken> lifetime_{
        std::make_shared<path::OwnerLifetimeToken>()};
};

/// 単色・グラデーションの色セル（CardWirth 1.50）。
class ColorCell final : public BgImage {
public:
    inline static constexpr std::string_view XML_NAME = "ColorCell";

    ColorCell();
    ColorCell(BlendMode blend_mode, GradientDir gradient_dir, CRGB color1,
              CRGB color2, ::cwxeditor::lib::StringView flag, std::int32_t x,
              std::int32_t y, std::int32_t width, std::int32_t height,
              bool mask);

    [[nodiscard]] bool equals(const BgImage& other) const noexcept override;
    void deepCopy(const BgImage& other) override;
    [[nodiscard]] ::cwxeditor::lib::String name(
        const CProps& props) const override;
    [[nodiscard]] BgImagePtr dup() const override;

    [[nodiscard]] BlendMode blendMode() const noexcept { return blend_mode_; }
    void blendMode(BlendMode value) noexcept;
    [[nodiscard]] GradientDir gradientDir() const noexcept {
        return gradient_dir_;
    }
    void gradientDir(GradientDir value) noexcept;
    [[nodiscard]] const CRGB& color1() const noexcept { return color1_; }
    void color1(CRGB value) noexcept;
    [[nodiscard]] const CRGB& color2() const noexcept { return color2_; }
    void color2(CRGB value) noexcept;

    void toNode(XNode& node, const XMLOption& option) const override;
    [[nodiscard]] static ::cwxeditor::lib::Owner<ColorCell> createFromNode(
        XNode& node, const XMLInfo& version);
    [[nodiscard]] static ::cwxeditor::lib::Owner<ColorCell> createFromNode(
        XNode& node);

private:
    void toNodeImpl(XNode& node) const override;
    BlendMode blend_mode_ = BlendMode::Normal;
    GradientDir gradient_dir_ = GradientDir::None;
    CRGB color1_{255, 255, 255, 255};
    CRGB color2_{0, 0, 0, 255};
};

/// プレイヤーキャラクターのセル（Wsn.1）。
class PCCell final : public BgImage {
public:
    inline static constexpr std::string_view XML_NAME = "PCCell";

    PCCell();
    PCCell(std::uint32_t pc_number, bool expand,
           ::cwxeditor::lib::StringView flag, std::int32_t x, std::int32_t y,
           std::int32_t width, std::int32_t height, bool mask);

    [[nodiscard]] bool equals(const BgImage& other) const noexcept override;
    void deepCopy(const BgImage& other) override;
    [[nodiscard]] ::cwxeditor::lib::String name(
        const CProps& props) const override;
    [[nodiscard]] BgImagePtr dup() const override;

    [[nodiscard]] std::uint32_t pcNumber() const noexcept { return pc_number_; }
    void pcNumber(std::uint32_t value) noexcept;
    [[nodiscard]] bool expand() const noexcept { return expand_; }
    void expand(bool value) noexcept;
    [[nodiscard]] Smoothing smoothing() const noexcept { return smoothing_; }
    void smoothing(Smoothing value) noexcept;

    void toNode(XNode& node, const XMLOption& option) const override;
    [[nodiscard]] static ::cwxeditor::lib::Owner<PCCell> createFromNode(
        XNode& node, const XMLInfo& version);
    [[nodiscard]] static ::cwxeditor::lib::Owner<PCCell> createFromNode(
        XNode& node);

private:
    void toNodeImpl(XNode& node) const override;
    std::uint32_t pc_number_ = 0;
    bool expand_ = false;
    Smoothing smoothing_ = Smoothing::Default;
};

/// 型付きセルはContentにも属する。Contentのbacks()は従
/// 来のパス一覧を返すため、 その互換表現と型付きグラフの所有権を分ける。
class BackgroundOwner : public virtual CWXPath {
public:
    ~BackgroundOwner() override = default;
    [[nodiscard]] virtual std::weak_ptr<path::OwnerLifetimeToken>
    backgroundLifetimeToken() const noexcept = 0;
    [[nodiscard]] virtual ::cwxeditor::lib::ConstOwnerSlice<BgImage>
    backgroundImages() const = 0;
};

/// 編集可能な背景セルのコンテナーが使う、所有者のインターフェース。
class BgImageOwner : public BackgroundOwner {
public:
    ~BgImageOwner() override = default;
    [[nodiscard]] virtual ::cwxeditor::lib::ConstOwnerSlice<BgImage>
    backs() const = 0;
    [[nodiscard]] virtual ::cwxeditor::lib::SharedSlice<BgImagePtr>
    backs() = 0;
    [[nodiscard]] ::cwxeditor::lib::ConstOwnerSlice<BgImage>
    backgroundImages() const override { return backs(); }
};

/// 背景の変更を編集中に使う、変更可能な所有者・コンテナー。
class BgImageContainer final : public BgImageOwner,
                               public std::enable_shared_from_this<BgImageContainer> {
public:
    explicit BgImageContainer(BgImageArray cells = {},
                              UseCounterPtr counter = {});
    ~BgImageContainer() override;
    [[nodiscard]] ::cwxeditor::lib::Owner<CWXPath>
    retainForCallback() override {
        return std::static_pointer_cast<CWXPath>(weak_from_this().lock());
    }

    [[nodiscard]] ::cwxeditor::lib::String cwxPath(bool id) override;
    [[nodiscard]] ::cwxeditor::lib::Owner<CWXPath> findCWXPath(
        std::string_view path) override;
    [[nodiscard]] ::cwxeditor::lib::Array<
        ::cwxeditor::lib::Observer<CWXPath>>
    cwxChilds() override;
    [[nodiscard]] ::cwxeditor::lib::Observer<CWXPath> cwxParent() override;
    void changed() override {}

    [[nodiscard]] UseCounterPtr useCounter() const noexcept {
        return use_counter_;
    }
    [[nodiscard]] std::weak_ptr<path::OwnerLifetimeToken>
    backgroundLifetimeToken() const noexcept override {
        return background_lifetime_;
    }
    void dispose();
    [[nodiscard]] ::cwxeditor::lib::ConstOwnerSlice<BgImage>
    backs() const override { return ::cwxeditor::lib::ConstOwnerSlice<BgImage>(cells_); }
    [[nodiscard]] ::cwxeditor::lib::SharedSlice<BgImagePtr>
    backs() override { return cells_; }
    void backs(BgImageArray cells);
    void append(BgImagePtr cell);
    void insert(std::size_t index, BgImagePtr cell);
    void set(std::size_t index, BgImagePtr cell);
    void removeBgImage(std::size_t index);
    void swapBacks(std::size_t index1, std::size_t index2);

    [[nodiscard]] static ::cwxeditor::lib::String BtoXML(
        const BgImageArray& cells, const XMLOption& option);
    [[nodiscard]] static ::cwxeditor::lib::String BtoXML(
        const BgImageArray& cells);
    static bool BfromXML(::cwxeditor::lib::StringView xml,
                         BgImageArray& cells, const XMLInfo& version) noexcept;
    static bool BfromXML(::cwxeditor::lib::StringView xml,
                         BgImageArray& cells) noexcept;

private:
    void bind(BgImagePtr& cell);
    void unbind(BgImagePtr& cell) noexcept;

    ::cwxeditor::lib::SharedSlice<BgImagePtr> cells_;
    UseCounterPtr use_counter_;
    std::shared_ptr<path::OwnerLifetimeToken> background_lifetime_{
        std::make_shared<path::OwnerLifetimeToken>()};
};

[[nodiscard]] bool isInheritBackground(const BgImageArray& cells) noexcept;
[[nodiscard]] bool isInheritBackground(
    const ::cwxeditor::lib::SharedSlice<BgImagePtr>& cells) noexcept;

}  // namespace cwxeditor::cwx::background
