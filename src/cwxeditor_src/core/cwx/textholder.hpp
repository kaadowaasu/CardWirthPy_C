#pragma once

// cwx/textholder.dのC++20移植。TextHolder
// が文字列由来の索引を所有する。 特殊文字の解析・置換は
// cwx::msgutils、CWXの親・パスの所有関係
// はcwx::pathで扱う。 SimpleTextHolder
// は全体の改名通知を所有するため、カウンターのコール
// バック基底を読み込む。 usecounter.hpp
// 側は保持型を前方宣言するだけなので循環しない。

#include "cwxeditor_src/core/cwx/perf.hpp"
#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "cwxeditor_src/core/cwx/msgutils.hpp"
#include "cwxeditor_src/core/cwx/path.hpp"
#include "cwxeditor_src/core/cwx/usecounter.hpp"

namespace cwxeditor::cwx::usecounter {
class UseCounter;
struct FlagId;
struct StepId;
struct VariantId;
struct PathId;
class FlagUser;
class StepUser;
class VariantUser;
class PathUser;
template <typename T> class UserLeaseState;
}  // namespace cwxeditor::cwx::usecounter

namespace cwxeditor::cwx::textholder {

enum class TextHolderType {
    Message,
    SimpleText,
    Coupon,
    Gossip,
    CardName,
};

using String = ::cwxeditor::lib::String;
using Owner = ::cwxeditor::lib::Owner<::cwxeditor::cwx::path::CWXPath>;
using Observer = ::cwxeditor::lib::Observer<::cwxeditor::cwx::path::CWXPath>;
using UseCounter = ::cwxeditor::cwx::usecounter::UseCounter;
using UseCounterOwner = ::cwxeditor::lib::Owner<UseCounter>;
using ChangeHandler = std::function<void()>;
using FindIndex = std::function<std::size_t(const std::string&)>;

namespace detail {

enum class IdKind { Generic, Flag, Step, Variant, Path };

template <typename T>
inline constexpr IdKind id_kind_v = [] {
    using U = std::remove_cvref_t<T>;
    if constexpr (std::is_same_v<U, ::cwxeditor::cwx::usecounter::FlagId>) {
        return IdKind::Flag;
    } else if constexpr (std::is_same_v<U, ::cwxeditor::cwx::usecounter::StepId>) {
        return IdKind::Step;
    } else if constexpr (std::is_same_v<U, ::cwxeditor::cwx::usecounter::VariantId>) {
        return IdKind::Variant;
    } else if constexpr (std::is_same_v<U, ::cwxeditor::cwx::usecounter::PathId>) {
        return IdKind::Path;
    } else {
        return IdKind::Generic;
    }
}();

inline std::string id_string(std::string_view value) {
    return std::string(value);
}

inline std::string id_string(const std::string& value) {
    return value;
}

template <typename T>
std::string id_string(const T& value) {
    using U = std::remove_cvref_t<T>;
    if constexpr (std::is_convertible_v<T, std::string>) {
        return static_cast<std::string>(value);
    } else if constexpr (requires { std::string(value.toString()); }) {
        return std::string(value.toString());
    } else if constexpr (requires { std::string(value.to_string()); }) {
        return std::string(value.to_string());
    } else if constexpr (requires { std::string(value.value()); }) {
        return std::string(value.value());
    } else {
        static_assert(std::is_same_v<U, void>,
                      "TextHolder IDs must provide string conversion or toString()");
    }
}

}  // namespace detail

/// 式・イベント・パスの所有者で使う、共通の文字列保持インターフェース。
class ISimpleTextHolder {
public:
    virtual ~ISimpleTextHolder() = default;

    [[nodiscard]] virtual std::string text() const = 0;
    virtual void text(std::string_view value) = 0;
    [[nodiscard]] virtual std::vector<std::string> flagsInText() const = 0;
    [[nodiscard]] virtual std::vector<std::string> stepsInText() const = 0;
    [[nodiscard]] virtual std::vector<std::string> variantsInText() const = 0;
    virtual void changeInText(std::size_t index, std::string_view id) = 0;

    template <typename Id>
    void changeInText(const std::size_t index, const Id& id) {
        if constexpr (detail::id_kind_v<Id> == detail::IdKind::Flag) {
            changeFlagInText(index, detail::id_string(id));
        } else if constexpr (detail::id_kind_v<Id> == detail::IdKind::Step) {
            changeStepInText(index, detail::id_string(id));
        } else if constexpr (detail::id_kind_v<Id> == detail::IdKind::Variant) {
            changeVariantInText(index, detail::id_string(id));
        } else if constexpr (detail::id_kind_v<Id> == detail::IdKind::Path) {
            changePathInText(index, detail::id_string(id));
        } else {
            changeInText(index, detail::id_string(id));
        }
    }

    virtual void changeFlagInText(std::size_t index, std::string_view id) = 0;
    virtual void changeStepInText(std::size_t index, std::string_view id) = 0;
    virtual void changeVariantInText(std::size_t index, std::string_view id) = 0;
    virtual void changePathInText(std::size_t index, std::string_view id) = 0;
};

/// メッセージに対応する文字列保持インターフェース。
class ITextHolder : public virtual ISimpleTextHolder {
public:
    ~ITextHolder() override = default;

    [[nodiscard]] virtual std::vector<std::string> fontsInText() const = 0;
    virtual void changeInText(std::size_t index,
                              const ::cwxeditor::cwx::usecounter::PathId& id) = 0;
};

/// 状態変数や特殊な名前の参照記号を含められる文字列。
class SimpleTextHolder : public ::cwxeditor::cwx::path::CWXPath,
                         public virtual ISimpleTextHolder,
                         public ::cwxeditor::cwx::usecounter::ChgFlagCallback,
                         public ::cwxeditor::cwx::usecounter::ChgStepCallback,
                         public ::cwxeditor::cwx::usecounter::ChgVariantCallback,
                         public std::enable_shared_from_this<SimpleTextHolder> {
public:
    SimpleTextHolder(Owner owner,
                     TextHolderType type,
                     std::string_view cwx_path_category = "text",
                     FindIndex find_index = {});
    ~SimpleTextHolder() override = default;

    [[nodiscard]] Owner retainForCallback() override;

    [[nodiscard]] TextHolderType type() const noexcept { return type_; }
    void changeHandler(ChangeHandler change) { ::cwxeditor::cwx::perf::TraceScope trace_textholder_d178(__FILE__, __LINE__);  changed_ = std::move(change); retired_change_owner_.reset(); }

    [[nodiscard]] std::string text() const override { ::cwxeditor::cwx::perf::TraceScope trace_textholder_d185(__FILE__, __LINE__);  return text_; }
    void text(std::string_view value) override;

    [[nodiscard]] std::vector<std::string> flagsInText() const override;
    [[nodiscard]] std::vector<std::string> stepsInText() const override;
    [[nodiscard]] std::vector<std::string> variantsInText() const override;
    [[nodiscard]] virtual std::vector<char> namesInText() const;

    [[nodiscard]] UseCounterOwner useCounter() const { return use_counter_; }
    [[nodiscard]] Owner ucOwner() const { return uc_owner_; }
    [[nodiscard]] Owner ucOwner() override { return uc_owner_; }
    void setUseCounter(const UseCounterOwner& counter,
                       const Owner& counter_owner) override;
    [[nodiscard]] std::uint64_t useCounterRevision() const noexcept override {
        return use_counter_revision_;
    }
    [[nodiscard]] std::uint64_t useCounterBindingRevision() const noexcept override {
        return use_counter_binding_revision_;
    }

protected:
    virtual void removeTextUseCounter() noexcept;
    void rebuildUseCounterUsers();
    void noteUseCounterBindingChange() noexcept;
    void setUseCounterImpl(const UseCounterOwner& counter,
                           const Owner& counter_owner);

public:
    void removeUseCounter() noexcept override;
    void changeInText(std::size_t index, std::string_view id) override;
    void changeFlagInText(std::size_t index, std::string_view id) override;
    void changeStepInText(std::size_t index, std::string_view id) override;
    void changeVariantInText(std::size_t index, std::string_view id) override;
    virtual void changePathInText(std::size_t index, std::string_view id) override;

    template <typename Id>
    void changeInText(const std::size_t index, const Id& id) {
        if constexpr (detail::id_kind_v<Id> == detail::IdKind::Flag) {
            changeFlagInText(index, detail::id_string(id));
        } else if constexpr (detail::id_kind_v<Id> == detail::IdKind::Step) {
            changeStepInText(index, detail::id_string(id));
        } else if constexpr (detail::id_kind_v<Id> == detail::IdKind::Variant) {
            changeVariantInText(index, detail::id_string(id));
        } else if constexpr (detail::id_kind_v<Id> == detail::IdKind::Path) {
            changePathInText(index, detail::id_string(id));
        } else {
            changeInText(index, detail::id_string(id));
        }
    }

    virtual bool changeCallback(std::string_view old_value, std::string_view new_value);
    bool changeCallback(const ::cwxeditor::cwx::usecounter::FlagId& old_value,
                        const ::cwxeditor::cwx::usecounter::FlagId& new_value) override;
    bool changeCallback(const ::cwxeditor::cwx::usecounter::StepId& old_value,
                        const ::cwxeditor::cwx::usecounter::StepId& new_value) override;
    bool changeCallback(const ::cwxeditor::cwx::usecounter::VariantId& old_value,
                        const ::cwxeditor::cwx::usecounter::VariantId& new_value) override;

    template <typename Id>
    bool changeCallback(const Id& old_value, const Id& new_value) {
        if constexpr (detail::id_kind_v<Id> == detail::IdKind::Flag) {
            return changeFlagCallback(detail::id_string(old_value), detail::id_string(new_value));
        } else if constexpr (detail::id_kind_v<Id> == detail::IdKind::Step) {
            return changeStepCallback(detail::id_string(old_value), detail::id_string(new_value));
        } else if constexpr (detail::id_kind_v<Id> == detail::IdKind::Variant) {
            return changeVariantCallback(detail::id_string(old_value), detail::id_string(new_value));
        } else if constexpr (detail::id_kind_v<Id> == detail::IdKind::Path) {
            return changePathCallback(detail::id_string(old_value), detail::id_string(new_value));
        } else {
            return changeCallback(detail::id_string(old_value), detail::id_string(new_value));
        }
    }

    virtual bool changeFlagCallback(std::string_view old_value, std::string_view new_value);
    virtual bool changeStepCallback(std::string_view old_value, std::string_view new_value);
    virtual bool changeVariantCallback(std::string_view old_value, std::string_view new_value);
    virtual bool changePathCallback(std::string_view old_value, std::string_view new_value);

    [[nodiscard]] String cwxPath(bool id) override;
    [[nodiscard]] Owner findCWXPath(std::string_view path) override;
    [[nodiscard]] std::vector<Observer> cwxChilds() override { return {}; }
    [[nodiscard]] Observer cwxParent() override;
    void changed() override {}

    [[nodiscard]] Owner owner() const { return owner_.lock(); }
    void owner(Owner value);
    // 名前付きクーポンの置換時は、CWX
    // パスをたどるための関連と、生存中の
    // パス所有者への変更通知を維持する。
    void retire(bool preserveNavigation = false) noexcept;


protected:
    void notifyChange();
    void indexText();
    void updateIdentityUseCounter(std::string_view value, bool add);
    [[nodiscard]] Owner selfOwner();

    TextHolderType type_{TextHolderType::SimpleText};
    Observer owner_;
    // 単独所有・スタック上の保持型はenable_shared_from_this()
    // を使えないが、 生成された*UserはUseCounterの改名通知で
    // 有効なCWXPath識別子を必要とする。 保持型が削除しない共有参照の
    // 基点を持ち、UserBaseの生の所有者に関する寿命契約へ合わせる。
    Owner self_anchor_;
    // 元の保持型の所有者（CouponUser/GossipUser）は
    // 、所有権を持たないshared_ptrのアダプター。 制御ブロック
    // を保持して弱い識別参照を有効に保つが、User自体は所有しない。
    Owner local_owner_;
    std::string text_;
    std::vector<std::string> flags_;
    std::vector<std::string> steps_;
    std::vector<std::string> variants_;
    UseCounterOwner use_counter_;
    Owner uc_owner_;
    std::uint64_t use_counter_revision_{};
    std::uint64_t use_counter_binding_revision_{};
    std::string cwx_path_category_;
    ChangeHandler changed_;
    Observer retired_change_owner_;
    FindIndex find_index_;
    std::vector<std::shared_ptr<usecounter::FlagUser>> flag_users_;
    std::vector<std::shared_ptr<usecounter::StepUser>> step_users_;
    std::vector<std::shared_ptr<usecounter::VariantUser>> variant_users_;
    std::array<std::shared_ptr<void>, 3> text_user_owner_lease_states_{};
};

/// 単純な参照記号に加え、フォントと色の索引を持つメッセージ文字列。
class TextHolder : public SimpleTextHolder,
                   public ITextHolder,
                   public ::cwxeditor::cwx::usecounter::ChgPathCallback {
public:
    explicit TextHolder(Owner owner, std::string_view cwx_path_category = "text")
        : SimpleTextHolder(std::move(owner), TextHolderType::Message, cwx_path_category) {}
    ~TextHolder() override = default;

    [[nodiscard]] std::string text() const override { return SimpleTextHolder::text(); }
    void text(std::string_view value) override;
    [[nodiscard]] std::vector<std::string> flagsInText() const override {
        return SimpleTextHolder::flagsInText();
    }
    [[nodiscard]] std::vector<std::string> stepsInText() const override {
        return SimpleTextHolder::stepsInText();
    }
    [[nodiscard]] std::vector<std::string> variantsInText() const override {
        return SimpleTextHolder::variantsInText();
    }
    [[nodiscard]] std::vector<std::string> fontsInText() const override;
    [[nodiscard]] std::vector<char> namesInText() const override;
    [[nodiscard]] std::vector<char> colorsInText() const { ::cwxeditor::cwx::perf::TraceScope trace_textholder_d93(__FILE__, __LINE__);  return colors_; }

    void setUseCounter(const UseCounterOwner& counter,
                       const Owner& counter_owner) override;
    using SimpleTextHolder::ucOwner;
    [[nodiscard]] Owner ucOwner() override { return SimpleTextHolder::ucOwner(); }
    void removeUseCounter() noexcept override {
        SimpleTextHolder::removeUseCounter();
    }
    [[nodiscard]] std::uint64_t useCounterRevision() const noexcept override {
        return SimpleTextHolder::useCounterRevision();
    }
    [[nodiscard]] std::uint64_t useCounterBindingRevision() const noexcept override {
        return SimpleTextHolder::useCounterBindingRevision();
    }
    using SimpleTextHolder::changeInText;
    void changeInText(std::size_t index, std::string_view id) override;
    void changeInText(
        std::size_t index,
        const ::cwxeditor::cwx::usecounter::PathId& id) override;
    void changeFlagInText(std::size_t index, std::string_view id) override;
    void changeStepInText(std::size_t index, std::string_view id) override;
    void changeVariantInText(std::size_t index, std::string_view id) override;
    void changePathInText(std::size_t index, std::string_view id) override;
    using SimpleTextHolder::changeCallback;
    bool changeCallback(std::string_view old_value, std::string_view new_value) override;
    bool changeCallback(const ::cwxeditor::cwx::usecounter::PathId& old_value,
                        const ::cwxeditor::cwx::usecounter::PathId& new_value) override;
    bool changeFlagCallback(std::string_view old_value,
                            std::string_view new_value) override;
    bool changeStepCallback(std::string_view old_value,
                            std::string_view new_value) override;
    bool changeVariantCallback(std::string_view old_value,
                               std::string_view new_value) override;
    bool changePathCallback(std::string_view old_value, std::string_view new_value) override;

private:
    void removeTextUseCounter() noexcept override;
    void rebuildPathUsers();
    void reindexMessageText();
    std::vector<std::string> fonts_;
    std::vector<char> colors_;
    std::vector<std::shared_ptr<usecounter::PathUser>> path_users_;
};

}  // namespace cwxeditor::cwx::textholder
