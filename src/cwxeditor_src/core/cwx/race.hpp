#pragma once
#include "cwxeditor_src/core/cwx/perf.hpp"

// cwx/race.dのC++20移植。

#include "cwxeditor_src/core/cwx/coupon.hpp"
#include "cwxeditor_src/core/cwx/props.hpp"
#include "cwxeditor_src/core/cwx/system.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/core/cwx/xml.hpp"
#include "cwxeditor_src/lib/portable.hpp"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace cwxeditor::cwx::race {

using String = ::cwxeditor::lib::String;
using StringView = ::cwxeditor::lib::StringView;
using Element = ::cwxeditor::cwx::types::Element;
using Enhance = ::cwxeditor::cwx::types::Enhance;
using Mental = ::cwxeditor::cwx::types::Mental;
using Physical = ::cwxeditor::cwx::types::Physical;
using Coupon = ::cwxeditor::cwx::coupon::Coupon;
using CouponsOwner = ::cwxeditor::cwx::coupon::CouponsOwner;
using XMLInfo = ::cwxeditor::cwx::system::XMLInfo;
using CProps = ::cwxeditor::cwx::props::CProps;
using XNode = ::cwxeditor::cwx::xml::XNode;

/// 1レベル毎のEP増加量の標準値。
inline constexpr std::uint32_t DEFAULT_EP_PER_LEVEL = 10U;

/// 種族関連の例外。
class RaceException : public std::runtime_error {
public:
    explicit RaceException(String message);
};

/// 種族のパラメーターを共通化する。Dはテンプレー
/// トをRaceとCastCardへ直接mixin
/// するが、 C++では基底クラスを使い、両者で全
/// APIを再利用する。 Set=falseなら変
/// 更用オーバーロードをコンパイル時に除外し、D
/// のstatic if (Set)に合わせる。
template <bool Set>
class RaceParam {
private:
    bool automaton_{false};     // 心を持たない
    bool constructure_{false};  // 魔法生物
    bool undead_{false};        // 命を持たない
    bool unholy_{false};        // 不浄な存在
    bool weaponRes_{false};
    bool magicRes_{false};
    lib::Dictionary<Element, bool> resist_;
    lib::Dictionary<Element, bool> weakness_;
    lib::Dictionary<Physical, std::uint32_t> physical_;
    lib::Dictionary<Mental, double> mental_;
    lib::Dictionary<Enhance, std::int32_t> defaultEnhance_;

protected:
    /// 変更通知を仮想関数にし、CastCardなどがRaceParamの
    /// 設定通知を 自身のCWXPathの変更通知へ接続できるようにする。
    virtual void changed() {}
    // カードを使う所有者では、通知後の値の書き込みまでネイティブの受信先を保
    // 持する。 その他のRaceParamの所有者は、空の既定処理を使う。
    [[nodiscard]] virtual std::shared_ptr<void> retainMutationReceiver() {
        return {};
    }

    void constructRace();
    void setFeature(XNode& parent) const;
    void setAbility(XNode& parent) const;
    void loadFeature(XNode& featureNode, const XMLInfo& ver);
    void loadAbility(const CProps& prop, XNode& abilityNode,
                     const XMLInfo& ver);
    void loadAbility(std::uint32_t physicalNormal, XNode& abilityNode,
                     const XMLInfo& ver);

public:
    virtual ~RaceParam() = default;

    /// rからパラメータをコピーする。
    template <typename T>
    void copyRaceParam(const T& r) {
        [[maybe_unused]] const auto receiver = retainMutationReceiver();
        // Dはこの呼び出し中のクラス引数を保持する。再利用可能なミ
        // ックスインはCardへ依存させず、 互換の公開weak_from_this
        // を持つ入力だけを保持する。 値・スタック上の
        // RaceParamには、追加の所有者は不要。
        [[maybe_unused]] const auto donor = [&]() -> std::shared_ptr<const void> {
            if constexpr (requires {
                              std::shared_ptr<const void>{r.weak_from_this().lock()};
                          }) {
                return r.weak_from_this().lock();
            }
            return {};
        }();
        ::cwxeditor::cwx::perf::TraceScope trace_race_97(__FILE__, __LINE__);
        automaton(r.automaton());
        constructure(r.constructure());
        undead(r.undead());
        unholy(r.unholy());
        weaponResist(r.weaponResist());
        magicResist(r.magicResist());
        resist(Element::Fire, r.resist(Element::Fire));
        resist(Element::Ice, r.resist(Element::Ice));
        weakness(Element::Fire, r.weakness(Element::Fire));
        weakness(Element::Ice, r.weakness(Element::Ice));
        physical(Physical::Dex, r.physical(Physical::Dex));
        physical(Physical::Agl, r.physical(Physical::Agl));
        physical(Physical::Int, r.physical(Physical::Int));
        physical(Physical::Str, r.physical(Physical::Str));
        physical(Physical::Vit, r.physical(Physical::Vit));
        physical(Physical::Min, r.physical(Physical::Min));
        mental(Mental::Aggressive, r.mental(Mental::Aggressive));
        mental(Mental::Cheerful, r.mental(Mental::Cheerful));
        mental(Mental::Brave, r.mental(Mental::Brave));
        mental(Mental::Cautious, r.mental(Mental::Cautious));
        mental(Mental::Trickish, r.mental(Mental::Trickish));
        defaultEnhance(Enhance::Avoid, r.defaultEnhance(Enhance::Avoid));
        defaultEnhance(Enhance::Resist, r.defaultEnhance(Enhance::Resist));
        defaultEnhance(Enhance::Defense, r.defaultEnhance(Enhance::Defense));
    }

    /// パラメータを比較する。
    template <typename T>
    [[nodiscard]] bool equalsRace(const T& r) const {
        ::cwxeditor::cwx::perf::TraceScope trace_race_126(__FILE__, __LINE__);
        return automaton() == r.automaton() &&
               constructure() == r.constructure() &&
               undead() == r.undead() && unholy() == r.unholy() &&
               weaponResist() == r.weaponResist() &&
               magicResist() == r.magicResist() &&
               resist(Element::Fire) == r.resist(Element::Fire) &&
               resist(Element::Ice) == r.resist(Element::Ice) &&
               weakness(Element::Fire) == r.weakness(Element::Fire) &&
               weakness(Element::Ice) == r.weakness(Element::Ice) &&
               physical(Physical::Dex) == r.physical(Physical::Dex) &&
               physical(Physical::Agl) == r.physical(Physical::Agl) &&
               physical(Physical::Int) == r.physical(Physical::Int) &&
               physical(Physical::Str) == r.physical(Physical::Str) &&
               physical(Physical::Vit) == r.physical(Physical::Vit) &&
               physical(Physical::Min) == r.physical(Physical::Min) &&
               mental(Mental::Aggressive) == r.mental(Mental::Aggressive) &&
               mental(Mental::Cheerful) == r.mental(Mental::Cheerful) &&
               mental(Mental::Brave) == r.mental(Mental::Brave) &&
               mental(Mental::Cautious) == r.mental(Mental::Cautious) &&
               mental(Mental::Trickish) == r.mental(Mental::Trickish) &&
               defaultEnhance(Enhance::Avoid) ==
                   r.defaultEnhance(Enhance::Avoid) &&
               defaultEnhance(Enhance::Resist) ==
                   r.defaultEnhance(Enhance::Resist) &&
               defaultEnhance(Enhance::Defense) ==
                   r.defaultEnhance(Enhance::Defense);
    }

    /// 命を持たないか。
    [[nodiscard]] bool undead() const noexcept { return undead_; }
    /// 心を持たないか。
    [[nodiscard]] bool automaton() const noexcept { return automaton_; }
    /// 不浄な存在か。
    [[nodiscard]] bool unholy() const noexcept { return unholy_; }
    /// 魔法生物か。
    [[nodiscard]] bool constructure() const noexcept { return constructure_; }
    /// 武器が効かないか。
    [[nodiscard]] bool weaponResist() const noexcept { return weaponRes_; }
    /// 魔法が効かないか。
    [[nodiscard]] bool magicResist() const noexcept { return magicRes_; }

    template <bool Enabled = Set>
        requires Enabled
    void undead(const bool value) {
        [[maybe_unused]] const auto receiver = retainMutationReceiver();
        if (undead_ != value) changed();
        undead_ = value;
    }
    template <bool Enabled = Set>
        requires Enabled
    void automaton(const bool value) {
        [[maybe_unused]] const auto receiver = retainMutationReceiver();
        if (automaton_ != value) changed();
        automaton_ = value;
    }
    template <bool Enabled = Set>
        requires Enabled
    void unholy(const bool value) {
        [[maybe_unused]] const auto receiver = retainMutationReceiver();
        if (unholy_ != value) changed();
        unholy_ = value;
    }
    template <bool Enabled = Set>
        requires Enabled
    void constructure(const bool value) {
        [[maybe_unused]] const auto receiver = retainMutationReceiver();
        if (constructure_ != value) changed();
        constructure_ = value;
    }
    template <bool Enabled = Set>
        requires Enabled
    void weaponResist(const bool value) {
        [[maybe_unused]] const auto receiver = retainMutationReceiver();
        if (weaponRes_ != value) changed();
        weaponRes_ = value;
    }
    template <bool Enabled = Set>
        requires Enabled
    void magicResist(const bool value) {
        [[maybe_unused]] const auto receiver = retainMutationReceiver();
        if (magicRes_ != value) changed();
        magicRes_ = value;
    }

    /// 炎/冷気が無効。
    [[nodiscard]] bool resist(const Element element) const;
    template <bool Enabled = Set>
        requires Enabled
    void resist(const Element element, const bool value) {
        [[maybe_unused]] const auto receiver = retainMutationReceiver();
        if (resist(element) != value) changed();
        resist_[element] = value;
        if (value) weakness(element, false);
    }

    /// 炎/冷気が弱点。
    [[nodiscard]] bool weakness(const Element element) const;
    template <bool Enabled = Set>
        requires Enabled
    void weakness(const Element element, const bool value) {
        [[maybe_unused]] const auto receiver = retainMutationReceiver();
        if (weakness(element) != value) changed();
        weakness_[element] = value;
        if (value) resist(element, false);
    }

    /// 身体能力。
    [[nodiscard]] std::uint32_t physical(const Physical value) const;
    template <bool Enabled = Set>
        requires Enabled
    void physical(const Physical value, const std::uint32_t amount) {
        [[maybe_unused]] const auto receiver = retainMutationReceiver();
        if (physical(value) != amount) changed();
        physical_[value] = amount;
    }

    /// 精神傾向。
    [[nodiscard]] double mental(const Mental value) const;
    template <bool Enabled = Set>
        requires Enabled
    void mental(const Mental value, const double amount) {
        [[maybe_unused]] const auto receiver = retainMutationReceiver();
        switch (value) {
        case Mental::Aggressive:
        case Mental::Cheerful:
        case Mental::Brave:
        case Mental::Cautious:
        case Mental::Trickish:
            if (mental(value) != amount) changed();
            mental_[value] = amount;
            break;
        case Mental::Unaggressive:
        case Mental::Uncheerful:
        case Mental::Unbrave:
        case Mental::Uncautious:
        case Mental::Untrickish:
            // D版の不具合との差異。Dは負の連想配列キーを読み書
            // きするが、 初期化と取得は正のキーだけを使う。指
            // 定した傾向が参照できるよう、正のキーへ書き込む。
            if (mental(value) != amount) changed();
            mental_[positiveMental(value)] = -amount;
            break;
        default:
            // Dのfinal switchで該当値がない場合と同じく、ネイティブ側でも失敗にする。
            throw std::logic_error("Invalid mental parameter");
        }
    }

    /// 常に掛かっている能力ボーナス。
    [[nodiscard]] std::int32_t defaultEnhance(const Enhance value) const;
    template <bool Enabled = Set>
        requires Enabled
    void defaultEnhance(const Enhance value, const std::int32_t amount) {
        [[maybe_unused]] const auto receiver = retainMutationReceiver();
        if (defaultEnhance(value) != amount) changed();
        defaultEnhance_[value] = amount;
    }

private:
    [[nodiscard]] static constexpr Mental positiveMental(const Mental value) {
        switch (value) {
        case Mental::Unaggressive: return Mental::Aggressive;
        case Mental::Uncheerful: return Mental::Cheerful;
        case Mental::Unbrave: return Mental::Brave;
        case Mental::Uncautious: return Mental::Cautious;
        case Mental::Untrickish: return Mental::Trickish;
        default: return value;
        }
    }
};

/// 種族。
class Race : public CouponsOwner, public RaceParam<false> {
private:
    String name_;
    String desc_;
    double levelCoefficient_{1.0};
    std::uint32_t epPerLevel_{DEFAULT_EP_PER_LEVEL};
    lib::SharedSlice<lib::Owner<Coupon>> coupons_;

    Race();

public:
    /// XMLノードから種族を生成。
    [[nodiscard]] static lib::Owner<Race> fromNode(const CProps& prop,
                                                    XNode& node,
                                                    const XMLInfo& ver);

    /// 種族名。
    [[nodiscard]] String name() const;
    /// 解説。
    [[nodiscard]] String desc() const;
    /// レベル判定式に掛ける係数。
    [[nodiscard]] double levelCoefficient() const noexcept;
    /// １レベル毎のEP獲得量。
    [[nodiscard]] std::uint32_t epPerLevel() const noexcept;
    /// 初期クーポン。
    [[nodiscard]] lib::SharedSlice<lib::Owner<Coupon>> coupons() override;
    [[nodiscard]] lib::ConstOwnerSlice<Coupon> coupons() const override;
};

}  // namespace cwxeditor::cwx::race
