#pragma once

#include "cwxeditor_src/core/cwx/beast_owner.hpp"
#include "cwxeditor_src/core/cwx/path.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/core/cwx/xml.hpp"
#include "cwxeditor_src/lib/array_slice.hpp"
#include "cwxeditor_src/lib/portable.hpp"
#include "cwxeditor_src/lib/shared_dictionary.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <string_view>

namespace cwxeditor::cwx::card {
class BeastCard;
class XMLOption;
}  // namespace cwxeditor::cwx::card

namespace cwxeditor::cwx::system {
class XMLInfo;
}  // namespace cwxeditor::cwx::system

namespace cwxeditor::cwx::usecounter {
class UseCounter;
}  // namespace cwxeditor::cwx::usecounter

namespace cwxeditor::cwx::event { class Content; }

namespace cwxeditor::cwx::motion {

using MType = types::MType;
using MArg = types::MArg;
using Element = types::Element;
using DamageType = types::DamageType;
using CwxPath = path::CWXPath;
using ChangeHandler = lib::Callback<void()>;

using MotionOwnerLifetime = path::OwnerLifetimeToken;

/// 効果モーションの名前・詳細の表を初
/// 期化する。 D版ではプロパティから
/// 表を初めて使うときに遅延初期化する
/// ため、C++でも同じ入口を用意す
/// る。 複数回呼んでも問題はない。
void static_this();

struct MDetail {
    lib::String name;
    lib::SharedDictionary<MArg, lib::String> args;
    bool rmdur{};

    /// 効果が指定された引数を使うかどうかを返す。
    [[nodiscard]] bool use(MArg arg) const;
    /// 引数に対応するXML属性名を返す。 子
    /// 要素で表す引数の場合は空文字列を返す。
    [[nodiscard]] lib::String attr(MArg arg) const;

    [[nodiscard]] static MDetail opCall(lib::String name);
    [[nodiscard]] static MDetail opCall(
        lib::String name, lib::SharedDictionary<MArg, lib::String> args);
    // ネイティブの値辞書は、この境界で明示的なスナップショットとする。
    // 共有の記述子を受け取るオーバーロードが、D互換の実装になる。
    template <typename NativeArgs>
        requires std::is_same_v<std::remove_cvref_t<NativeArgs>,
                                lib::Dictionary<MArg, lib::String>>
    [[nodiscard]] static MDetail opCall(lib::String name, NativeArgs&& args) {
        return opCall(std::move(name),
            lib::SharedDictionary<MArg, lib::String>::fromSnapshot(args));
    }
};

/// Dの効果モーション詳細表と名前の逆引き表全体。 後続の検索で
/// Dの表が再構成されても、取得したスナップショットは保持する。
[[nodiscard]] lib::Dictionary<MType, MDetail> MOTION_DETAILS();
[[nodiscard]] lib::Dictionary<lib::String, MType> MTYPE_MAP();

/// 保存形式の効果モーション名と列挙値を相互に変換する。
[[nodiscard]] MType mTypeFromName(lib::StringView name);
[[nodiscard]] lib::String mTypeToName(MType type);

/// 効果の列を保持する、所有者側の共通処理。
class MotionUser {
private:
    lib::SharedSlice<lib::Owner<class Motion>> motions_;
    ChangeHandler change_;
    lib::Observer<usecounter::UseCounter> use_counter_;
    lib::Observer<CwxPath> use_counter_owner_;
    lib::Observer<class MotionOwner> cwx_path_;
    // 共有所有のAPIはDのクラス引数を保持する。 埋め込みの利用元は明示
    // 的な生ポインター版のコンストラクターを使い、所有権の循環を避ける。
    lib::Owner<class MotionOwner> cwx_path_owner_;
    class MotionOwner* cwx_path_raw_{};
    std::weak_ptr<MotionOwnerLifetime> cwx_path_lifetime_;

public:
    /// 共有所有で渡されたパスは保持する。親のコン
    /// ストラクター内でD形式の構築を行うため、
    /// 生ポインター版も残すが、こちらは所有しない
    /// 。 親が共有所有されるまでの間だけ使う。
    explicit MotionUser(const lib::Owner<class MotionOwner>& cwxPath);
    explicit MotionUser(class MotionOwner* cwxPath) noexcept;
    ~MotionUser();

    [[nodiscard]] lib::String cwxPath(bool id) const;
    void changeHandler(ChangeHandler change);

private:
    void changed();

public:
    [[nodiscard]] lib::Observer<usecounter::UseCounter> useCounter();
    void setUseCounter(const lib::Owner<usecounter::UseCounter>& uc,
                       const lib::Owner<CwxPath>& ucOwner);
    void removeUseCounter();

    void motions(lib::SharedSlice<lib::Owner<class Motion>> value);
    void motions(lib::Array<lib::Owner<class Motion>> value);
    void motions(std::initializer_list<lib::Owner<class Motion>> value);
    [[nodiscard]] lib::SharedSlice<lib::Owner<class Motion>> motions() noexcept;
    [[nodiscard]] lib::ConstOwnerSlice<class Motion> motions() const noexcept;
};

/// 効果モーション列を所有するカード・コンテントのインターフェース。
class MotionOwner : public virtual CwxPath {
    std::shared_ptr<MotionOwnerLifetime> motion_lifetime_{
        std::make_shared<MotionOwnerLifetime>()};

public:
    MotionOwner() = default;
    // 効果モーションの所有者は固有の識別を持つツリーノードで、内
    // 包するMotionUserはそのアドレスを記録する。 ネイ
    // ティブのコピー・移動では寿命トークンや生の所有者参照を複製
    // せず、 各モデルの明示的なdup・深いコピーの処理を使う。
    MotionOwner(const MotionOwner&) = delete;
    MotionOwner& operator=(const MotionOwner&) = delete;
    MotionOwner(MotionOwner&&) = delete;
    MotionOwner& operator=(MotionOwner&&) = delete;
    ~MotionOwner() override { invalidateMotionLifetime(); }
    [[nodiscard]] std::weak_ptr<MotionOwnerLifetime> motionLifetimeToken() const noexcept {
        return motion_lifetime_;
    }
    void invalidateMotionLifetime() noexcept {
        if (motion_lifetime_) motion_lifetime_->alive.store(false, std::memory_order_release);
        motion_lifetime_.reset();
    }
    [[nodiscard]] virtual lib::SharedSlice<lib::Owner<class Motion>> motions() = 0;
    [[nodiscard]] virtual lib::ConstOwnerSlice<class Motion> motions() const = 0;
};

/// 効果系カード・コンテントのツリーに接続された効果。
class Motion : public card::BeastOwner,
               public path::Commentable,
               public std::enable_shared_from_this<Motion> {
private:
    lib::String object_id_;
    MType type_{};
    lib::Observer<usecounter::UseCounter> use_counter_;
    lib::Observer<CwxPath> use_counter_owner_;
    ChangeHandler change_;
    Element element_ = Element::All;
    DamageType damage_type_ = DamageType::LevelRatio;
    std::uint32_t u_value_ = 1U;
    std::int32_t a_value_{};
    std::uint32_t round_ = 10U;
    lib::Owner<card::BeastCard> beast_;
    std::uint32_t max_nest_ = 1U;
    lib::String comment_;
    lib::Observer<MotionOwner> owner_;
    // 所有者へのリンクには、その所有者の実際の共有制御ブロック
    // を使う。 トークンは生のスタック上の所有者を使う際の保護
    // だけに使い、親のメモリを所有するハンドルとはみなさない。
    std::weak_ptr<MotionOwnerLifetime> owner_lifetime_;
    MotionOwner* owner_raw_{};

    friend class MotionUser;
    friend class ::cwxeditor::cwx::event::Content;

public:
    inline static constexpr std::string_view XML_NAME = "Motion";

    Motion(MType type, Element element);
    ~Motion() override;

    [[nodiscard]] lib::Owner<CwxPath> retainForCallback() override {
        return std::static_pointer_cast<CwxPath>(weak_from_this().lock());
    }

    [[nodiscard]] lib::String objectId() const;
    [[nodiscard]] MType type() const noexcept { return type_; }
    [[nodiscard]] MDetail detail() const;

    void changeHandler(ChangeHandler change);
    [[nodiscard]] lib::Observer<usecounter::UseCounter> useCounter();
    void setUseCounter(const lib::Owner<usecounter::UseCounter>& uc,
                       const lib::Owner<CwxPath>& ucOwner);
    void removeUseCounter();

    [[nodiscard]] lib::Owner<Motion> dup() const;
    [[nodiscard]] bool opEquals(const Motion& other) const;
    [[nodiscard]] bool opEquals(const Motion* other) const {
        return other != nullptr && opEquals(*other);
    }
    [[nodiscard]] bool operator==(const Motion& other) const {
        return opEquals(other);
    }

    [[nodiscard]] Element element() const noexcept { return element_; }
    void element(Element value);

    [[nodiscard]] DamageType damageType() const noexcept { return damage_type_; }
    void damageType(DamageType value);

    [[nodiscard]] std::uint32_t uValue() const noexcept { return u_value_; }
    inline static constexpr std::int32_t uValue_min = 1;
    inline static constexpr std::int32_t uValue_max =
        std::numeric_limits<std::int32_t>::max();
    void uValue(std::int32_t value);

    [[nodiscard]] std::int32_t aValue() const noexcept { return a_value_; }
    inline static constexpr std::int32_t aValue_min = -10;
    inline static constexpr std::int32_t aValue_max = 10;
    void aValue(std::int32_t value);

    [[nodiscard]] std::int32_t round() const noexcept {
        return static_cast<std::int32_t>(round_);
    }
    void round(std::int32_t value);
    inline static constexpr std::int32_t round_min = 1;
    inline static constexpr std::int32_t round_max =
        std::numeric_limits<std::int32_t>::max();

    [[nodiscard]] lib::Owner<card::BeastCard> beast();
    [[nodiscard]] lib::Owner<const card::BeastCard> beast() const;
    [[nodiscard]] lib::Owner<card::BeastCard> beast(std::uint64_t id) override;
    [[nodiscard]] lib::Owner<const card::BeastCard> beast(
        std::uint64_t id) const override;
    [[nodiscard]] lib::SharedSlice<lib::Owner<card::BeastCard>>
    beasts() override;
    [[nodiscard]] lib::ConstOwnerSlice<card::BeastCard>
    beasts() const override;
    void beast(const lib::Owner<const card::BeastCard>& value);
    /// 読み込み・複製の内部処理用の設定関数。 Dのパッケージ内限定
    /// のnewBeastと同じく、渡されたカード自体は複製しない。
    void newBeast(const lib::Owner<card::BeastCard>& value);
    [[nodiscard]] std::uint64_t setBeastFromNode(
        xml::XNode& node, const system::XMLInfo& ver);

    [[nodiscard]] std::uint32_t maxNest() const noexcept { return max_nest_; }
    void maxNest(std::uint32_t value);
    inline static constexpr std::uint32_t maxNest_init = 1U;
    inline static constexpr std::uint32_t maxNest_min = 0U;
    inline static constexpr std::uint32_t maxNest_max = 999U;

    [[nodiscard]] lib::String comment() const override { return comment_; }
    void comment(lib::String value) override;
    void changed() override;

    [[nodiscard]] lib::String toXML(
        const lib::Owner<card::XMLOption>& opt) const;
    [[nodiscard]] xml::XNode toNode(
        const lib::Owner<card::XMLOption>& opt) const;
    [[nodiscard]] xml::XNode toNode(
        xml::XNode& node, const lib::Owner<card::XMLOption>& opt) const;

    [[nodiscard]] static lib::Owner<Motion> createFromNode(
        xml::XNode& node, const system::XMLInfo& ver);

    [[nodiscard]] lib::String cwxPath(bool id) override;
    [[nodiscard]] lib::Owner<CwxPath> findCWXPath(
        std::string_view value) override;
    [[nodiscard]] lib::Array<lib::Observer<CwxPath>> cwxChilds() override;
    [[nodiscard]] lib::Observer<CwxPath> cwxParent() override;

private:
    static std::int32_t roundValue(std::int32_t value, std::int32_t max,
                                   std::int32_t min) noexcept;
    [[nodiscard]] bool hasBeastId(std::uint64_t id) const;
    [[nodiscard]] lib::SharedSlice<lib::Owner<card::BeastCard>> beastSnapshot() const;
    [[nodiscard]] xml::XNode toNodeImpl(
        xml::XNode& node, const lib::Owner<card::XMLOption>& opt) const;
    void setBeastImpl(const lib::Owner<card::BeastCard>& value);
    void attachOwner(MotionOwner* raw, const lib::Owner<MotionOwner>& owner);
};

}  // namespace cwxeditor::cwx::motion
