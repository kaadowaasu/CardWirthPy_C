#pragma once

// cwx/expression.dのC++20
// 移植。式モジュールがパーサーと値モデルを所有
// し、 生成された互換層には依存しない。文字列
// はUTF-8、式ノードは共有所有、 コール
// バックはstd::functionを使う。

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "cwxeditor_src/core/cwx/msgutils.hpp"
#include "cwxeditor_src/core/cwx/path.hpp"
#include "cwxeditor_src/core/cwx/textholder.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/core/cwx/usecounter.hpp"
#include "cwxeditor_src/lib/array_slice.hpp"
#include "cwxeditor_src/lib/portable.hpp"

namespace cwxeditor::cwx {
namespace flag {
class Variant;
}
namespace props {
class CProps;
}
namespace summary {
class Summary;
}

namespace expression {

using String = lib::String;
using StringView = lib::StringView;
using CProps = props::CProps;
using CwxPath = path::CWXPath;
using PartPtr = lib::Owner<class Part>;
using ConstPartPtr = lib::Owner<const Part>;
using PartList = lib::Array<PartPtr>;
using ConstPartList = lib::Array<ConstPartPtr>;
using PartSlice = lib::SharedSlice<PartPtr>;
using ConstPartSlice = lib::SharedSlice<const ConstPartPtr>;
using ExprErrors = lib::Array<struct ExprError>;

/// 共有される読み取り専用のPart格納先。構築中の可変配列はConstOwnerSlice
/// 経由で公開し、 格納先の置き換えが見えるようにする。既にconstの評価結果
/// はSharedSlice<const ConstPartPtr>を直接使う。
class ConstPartView {
    using MutableView = lib::ConstOwnerSlice<Part>;

public:
    using value_type = ConstPartPtr;
    using reference = const ConstPartPtr;

    class const_iterator {
    public:
        using value_type = ConstPartPtr;
        using reference = const ConstPartPtr;
        using pointer = void;
        using difference_type = std::ptrdiff_t;
        using iterator_category = std::forward_iterator_tag;
        using iterator_concept = std::forward_iterator_tag;

        const_iterator() = default;
        [[nodiscard]] reference operator*() const {
            return std::visit([](const auto& iterator) -> reference {
                return *iterator;
            }, iterator_);
        }
        const_iterator& operator++() {
            std::visit([](auto& iterator) { ++iterator; }, iterator_);
            return *this;
        }
        const_iterator operator++(int) {
            auto previous = *this;
            ++(*this);
            return previous;
        }
        friend bool operator==(const const_iterator& left,
                               const const_iterator& right) {
            if (const auto* mutableLeft =
                    std::get_if<MutableIterator>(&left.iterator_)) {
                const auto* mutableRight =
                    std::get_if<MutableIterator>(&right.iterator_);
                return mutableRight && *mutableLeft == *mutableRight;
            }
            const auto* constLeft = std::get_if<ConstPartSlice::iterator>(&left.iterator_);
            const auto* constRight = std::get_if<ConstPartSlice::iterator>(&right.iterator_);
            return constLeft && constRight && *constLeft == *constRight;
        }
        friend bool operator!=(const const_iterator& left,
                               const const_iterator& right) {
            return !(left == right);
        }

    private:
        friend class ConstPartView;
        using MutableIterator = typename MutableView::const_iterator;
        using Iterator = std::variant<typename MutableView::const_iterator,
                                      ConstPartSlice::iterator>;
        explicit const_iterator(Iterator iterator) : iterator_(std::move(iterator)) {}
        Iterator iterator_{};
    };

    ConstPartView() = default;
    ConstPartView(PartSlice source) : storage_(MutableView(std::move(source))) {}
    ConstPartView(ConstPartSlice source) : storage_(std::move(source)) {}
    explicit ConstPartView(MutableView source) : storage_(std::move(source)) {}
    ConstPartView(PartList values)
        : ConstPartView(PartSlice(std::move(values))) {}
    ConstPartView(ConstPartList values)
        : ConstPartView(ConstPartSlice(std::move(values))) {}

    [[nodiscard]] std::size_t size() const noexcept {
        return std::visit([](const auto& source) { return source.size(); }, storage_);
    }
    [[nodiscard]] bool empty() const noexcept { return size() == 0; }
    [[nodiscard]] reference operator[](std::size_t index) const {
        return std::visit([index](const auto& source) -> reference {
            return source[index];
        }, storage_);
    }
    [[nodiscard]] reference at(std::size_t index) const {
        if (index >= size()) throw std::out_of_range("expression Part view index");
        return (*this)[index];
    }
    [[nodiscard]] reference front() const {
        if (empty()) throw std::out_of_range("expression Part view front");
        return (*this)[0];
    }
    [[nodiscard]] reference back() const {
        if (empty()) throw std::out_of_range("expression Part view back");
        return (*this)[size() - 1];
    }
    [[nodiscard]] ConstPartView slice(std::size_t first, std::size_t last) const {
        if (first > last || last > size())
            throw std::out_of_range("expression Part view range");
        return std::visit([first, last](const auto& source) -> ConstPartView {
            return ConstPartView(source.slice(first, last));
        }, storage_);
    }
    [[nodiscard]] const_iterator begin() const {
        return std::visit([](const auto& source) -> const_iterator {
            return const_iterator(typename const_iterator::Iterator{source.begin()});
        }, storage_);
    }
    [[nodiscard]] const_iterator end() const {
        return std::visit([](const auto& source) -> const_iterator {
            return const_iterator(typename const_iterator::Iterator{source.end()});
        }, storage_);
    }

private:
    std::variant<MutableView, ConstPartSlice> storage_{MutableView{}};
};

enum class EvalMode : std::int32_t {
    All = 0,
    TypeCheck = 1,
};

struct ExprError {
    String message{};
    std::size_t errLine{};
    std::size_t errPos{};
    String file{};
    std::size_t line{};

    ExprError() = default;
    ExprError(String msg, std::size_t errorLine, std::size_t errorPos,
              String sourceFile = {}, std::size_t sourceLine = 0)
        : message(std::move(msg)), errLine(errorLine), errPos(errorPos),
          file(std::move(sourceFile)), line(sourceLine) {}
};

class ExprException : public std::runtime_error {
public:
    ExprException(String file, std::size_t line, String expression,
                  ExprErrors errors);
    [[nodiscard]] const String& sourceFile() const noexcept { return file_; }
    [[nodiscard]] std::size_t sourceLine() const noexcept { return line_; }
    [[nodiscard]] const String& expression() const noexcept { return expression_; }
    [[nodiscard]] const ExprErrors& errors() const noexcept { return errors_; }

private:
    String file_;
    std::size_t line_{};
    String expression_;
    ExprErrors errors_;
};

struct Token {
    bool entity{false};
    std::size_t line{0};
    std::size_t pos{0};
    std::size_t index{0};
    String token{};
};

class Part : public std::enable_shared_from_this<Part> {
public:
    explicit Part(Token value = {});
    virtual ~Part() = default;
    [[nodiscard]] virtual ConstPartPtr toValueObj(const CProps* prop,
                                                   ExprErrors& errors) const;
    [[nodiscard]] virtual String stringValue() const;
    [[nodiscard]] virtual String toString() const;
    Token token;
};

class Expr final : public Part {
public:
    Expr(Token token, PartList values);
    Expr(Token token, PartSlice values);
    Expr(Token token, ConstPartView values);
    Expr(Token token, ConstPartList values);
    ConstPartView parts;
    [[nodiscard]] String toString() const override;
};

class NumberValue final : public Part {
public:
    NumberValue(Token token, double value);
    double numVal{};
    [[nodiscard]] String stringValue() const override;
    [[nodiscard]] String toString() const override;
};

class StringValue final : public Part {
public:
    StringValue(Token token, String value);
    String strVal;
    [[nodiscard]] String stringValue() const override;
    [[nodiscard]] String toString() const override;
};

class BooleanValue final : public Part {
public:
    BooleanValue(Token token, bool value);
    bool boolVal{};
    [[nodiscard]] String stringValue() const override;
    [[nodiscard]] String toString() const override;
};

class ListValue final : public Part {
public:
    ListValue(Token token, PartList values);
    ListValue(Token token, PartSlice values);
    ListValue(Token token, ConstPartView values);
    ListValue(Token token, ConstPartList values);
    ConstPartView listVal;
    [[nodiscard]] String stringValue() const override;
    [[nodiscard]] String toString() const override;
};

class UnknownValue final : public Part {
public:
    explicit UnknownValue(Token token = {});
    [[nodiscard]] String stringValue() const override;
    [[nodiscard]] String toString() const override;
};

class StructureValue final : public Part {
public:
    StructureValue(Token token, String name, PartList values);
    StructureValue(Token token, String name, ConstPartList values);
    StructureValue(Token token, String name, ConstPartView values);
    String structName;
    ConstPartView structVal;
    [[nodiscard]] String stringValue() const override;
    [[nodiscard]] String toString() const override;
};

class Symbol final : public Part {
public:
    Symbol(Token token, String value);
    String symbol;
    [[nodiscard]] ConstPartPtr toValueObj(const CProps* prop,
                                           ExprErrors& errors) const override;
    [[nodiscard]] String stringValue() const override;
    [[nodiscard]] String toString() const override;
};

class UnaryOperator final : public Part {
public:
    UnaryOperator(Token token, String op);
    String operator_;
    // ネイティブ専用の参照補助。Dは単項演算子を後置記法のトークン配列へ保存
    // し、 オペランドのフィールドを持たない。この拡張は読み取り専用にする。
    ConstPartPtr operand{};
    [[nodiscard]] ConstPartPtr call(const CProps* prop, ConstPartPtr rhs,
                                    ExprErrors& errors) const;
    [[nodiscard]] String toString() const override;
};

struct VariableInfo;

class Operator final : public Part {
public:
    Operator(Token token, String op);
    String operator_;
    // ネイティブ専用の参照補助。Dは演算子を後置記法のトークン配列へ保
    // 存し、 子のフィールドを持たない。この拡張も読み取り専用にする。
    ConstPartPtr lhs{};
    ConstPartPtr rhs{};
    [[nodiscard]] ConstPartPtr call(const CProps* prop, EvalMode mode,
                                    ConstPartPtr lhs, ConstPartPtr rhs,
                                    ExprErrors& errors) const;
    [[nodiscard]] ConstPartPtr call(const CProps* prop, EvalMode mode,
                                    const VariableInfo& info, ConstPartPtr lhs,
                                    ConstPartPtr rhs, ExprErrors& errors) const;
    [[nodiscard]] String toString() const override;
};

/// フラグ・式の評価・カードのメタデータで共用する値。
struct VariantVal {
    bool valid{false};
    types::VariantType type{types::VariantType::Number};
    double numVal{0.0};
    String strVal{};
    bool boolVal{false};
    lib::Array<VariantVal> listVal{};
    String structName{};
    lib::Array<VariantVal> structVal{};

    VariantVal() = default;
    explicit VariantVal(types::VariantType valueType) : valid(true), type(valueType) {}
    explicit VariantVal(const flag::Variant& value);

    [[nodiscard]] static VariantVal invalidValue() { return {}; }
    [[nodiscard]] static VariantVal numValue(double value);
    [[nodiscard]] static VariantVal strValue(String value);
    [[nodiscard]] static VariantVal boolValue(bool value);
    [[nodiscard]] static VariantVal listValue(lib::Array<VariantVal> value);
    [[nodiscard]] static VariantVal structValue(String name,
                                                 lib::Array<VariantVal> value);

    friend bool operator==(const VariantVal& lhs, const VariantVal& rhs);
    friend bool operator!=(const VariantVal& lhs, const VariantVal& rhs) {
        return !(lhs == rhs);
    }
};

struct StructureMember {
    String name{};
    String dataVersion{};
    bool isPublic{false};
    VariantVal defaultValue;
    bool typeCheck{false};
    types::VariantType type{types::VariantType::Number};
    double minValue{std::numeric_limits<double>::quiet_NaN()};
    String structName{};
};

struct StructureInfo {
    String name{};
    String dataVersion{};
    bool isPublic{false};
    lib::SharedSlice<const StructureMember> members{};
    std::size_t requiredMemberNum{0};

    StructureInfo() = default;
    StructureInfo(String valueName, String version, bool publicValue,
                  lib::SharedSlice<const StructureMember> valueMembers,
                  std::size_t required);
    [[nodiscard]] std::ptrdiff_t indexOf(StringView memberName) const;

private:
    std::unordered_map<String, std::size_t> indexTable_;
};

using VarGetter = msgutils::VarGetter;
using ExistsGetter = std::function<bool(const String&)>;
using FlagTextGetter = std::function<String(const String&, bool)>;
using StepTextGetter = std::function<String(const String&, std::uint32_t)>;
using StepMaxGetter = std::function<std::uint32_t(const String&)>;
using VariantGetter = std::function<VariantVal(const String&)>;
using SelectedGetter = std::function<std::uint32_t()>;
using FindCouponGetter = std::function<std::uint32_t(std::uint32_t, const String&, std::uint32_t)>;
using CouponTextGetter = std::function<String(std::uint32_t, std::uint32_t)>;
using FindGossipGetter = std::function<std::uint32_t(const String&, std::uint32_t)>;
using GossipTextGetter = std::function<String(std::uint32_t)>;
using FindKeyCodeGetter = std::function<std::uint32_t(const StructureValue&, const String&, std::uint32_t)>;
using KeyCodeTextGetter = std::function<String(const StructureValue&, std::uint32_t)>;
using CastNameGetter = std::function<String(std::uint32_t)>;
using CardNameGetter = std::function<String(std::int32_t, std::uint32_t)>;
using TextGetter = std::function<String()>;
using MouseGetter = std::function<double()>;
using CardVariantGetter = std::function<VariantVal(std::int32_t, std::uint32_t, const String&)>;
using CardFlagGetter = std::function<bool(std::int32_t, std::uint32_t, const String&)>;
using CardStepGetter = std::function<std::uint32_t(std::int32_t, std::uint32_t, const String&)>;

struct VariableInfo {
    std::function<bool(const String&)> isTargetWsnVersion;
    ExistsGetter existsFlag;
    ExistsGetter existsStep;
    ExistsGetter existsVariant;
    FlagTextGetter flagText;
    StepTextGetter stepText;
    StepMaxGetter stepMax;
    VariantGetter variantValue;
    std::function<bool(const String&)> flagValue;
    std::function<std::uint32_t(const String&)> stepValue;
    SelectedGetter selectedPlayerCardNumber;
    FindCouponGetter findCoupon;
    CouponTextGetter couponText;
    FindGossipGetter findGossip;
    GossipTextGetter gossipText;
    FindKeyCodeGetter findKeyCode;
    KeyCodeTextGetter KeyCodeText;
    CastNameGetter castName;
    CardNameGetter cardName;
    TextGetter partyName;
    TextGetter yadoName;
    TextGetter scenarioName;
    MouseGetter mouseX;
    MouseGetter mouseY;
    CardVariantGetter variantValueOfCard;
    CardFlagGetter flagValueOfCard;
    CardStepGetter stepValueOfCard;

    VariableInfo() = default;
    // 共有所有のSummary入力はデリゲートが保持する。ネイティブの呼び出し元は、 prop
    // と共有所有でないSummaryをコールバックの寿命の間維持する必要がある。
    VariableInfo(const CProps* prop, const summary::Summary* summ,
                 const usecounter::CounterOwner& uc, String targetVersion,
                 const msgutils::NameMap& names, const VarGetter& flags,
                 const VarGetter& steps, const VarGetter& variants,
                 const VarGetter& sysSteps, CastNameGetter castName,
                 CardNameGetter cardName, TextGetter partyName,
                 TextGetter yadoName, TextGetter scenarioName,
                 MouseGetter mouseX, MouseGetter mouseY,
                 CardVariantGetter variantValueOfCard,
                 CardFlagGetter flagValueOfCard,
                 CardStepGetter stepValueOfCard);
    VariableInfo(const VariableInfo& base, ExistsGetter variantExists,
                 VariantGetter variantLookup);
};

class Function final : public Part {
public:
    Function(Token token, String name, PartList arguments);
    Function(Token token, String name, PartSlice arguments);
    Function(Token token, String name, ConstPartView arguments);
    Function(Token token, String name, ConstPartList arguments);
    String funcName;
    ConstPartView args;
    [[nodiscard]] ConstPartPtr call(const CProps* prop, EvalMode mode,
                               const VariableInfo& info,
                               ExprErrors& errors) const;
    [[nodiscard]] String toString() const override;
};

using ExpressionFunction = std::function<ConstPartPtr(const CProps*, EvalMode,
                                                       const VariableInfo&, const Function&,
                                                       ConstPartView, ExprErrors&)>;

// Dモジュールの補助関数に対応する、公開の解析・評価処理。
[[nodiscard]] PartList parseExpression(const CProps* prop, StringView text,
                                       ExprErrors& errors);
[[nodiscard]] PartList parseSemantics(const CProps* prop, const lib::Array<Token>& tokens,
                                      std::size_t& index, ExprErrors& errors);
[[nodiscard]] PartList parseArguments(const CProps* prop, const lib::Array<Token>& tokens,
                                      std::size_t& index, ExprErrors& errors);
[[nodiscard]] ConstPartPtr calculate(const CProps* prop, EvalMode mode,
                                     const VariableInfo& info, ConstPartView expr,
                                     ExprErrors& errors);
[[nodiscard]] VariantVal eval(const CProps* prop, EvalMode mode,
                              const VariableInfo& info, StringView text);
[[nodiscard]] ConstPartPtr evalFunctionPart(const CProps* prop, EvalMode mode,
                                            const VariableInfo& info, ConstPartPtr arg,
                                            ExprErrors& errors);
[[nodiscard]] ConstPartPtr evalListElement(const CProps* prop, EvalMode mode,
                                           const VariableInfo& info, const ListValue& list,
                                           std::size_t index, ExprErrors& errors);
[[nodiscard]] ConstPartPtr evalListValue(const CProps* prop, EvalMode mode,
                                         const VariableInfo& info, const ListValue& list,
                                         ExprErrors& errors);
[[nodiscard]] ConstPartPtr evalValueDeep(const CProps* prop, EvalMode mode,
                                         const VariableInfo& info, ConstPartPtr value,
                                         ExprErrors& errors);
[[nodiscard]] bool equalsFunctionValue(const CProps* prop, EvalMode mode,
                                       const VariableInfo& info, ConstPartPtr lhs,
                                       ConstPartPtr rhs, ExprErrors& errors);
[[nodiscard]] ConstPartPtr callIfLazy(const CProps* prop, EvalMode mode,
                                      const VariableInfo& info, const Function& func,
                                      ConstPartView args, ExprErrors& errors);
[[nodiscard]] ConstPartPtr callAndDLazy(const CProps* prop, EvalMode mode,
                                        const VariableInfo& info, const Function& func,
                                        ConstPartView args, ExprErrors& errors);
[[nodiscard]] ConstPartPtr callOrDLazy(const CProps* prop, EvalMode mode,
                                       const VariableInfo& info, const Function& func,
                                       ConstPartView args, ExprErrors& errors);
[[nodiscard]] ConstPartPtr callSwitchLazy(const CProps* prop, EvalMode mode,
                                          const VariableInfo& info, const Function& func,
                                          ConstPartView args, ExprErrors& errors);
[[nodiscard]] ConstPartPtr callForLazy(const CProps* prop, EvalMode mode,
                                       const VariableInfo& info, const Function& func,
                                       ConstPartView args, ExprErrors& errors);

[[nodiscard]] const StructureInfo& structureInfo(StringView name);
[[nodiscard]] lib::SharedSlice<const StructureMember> structureMembers(StringView name);
[[nodiscard]] lib::SharedSlice<VariantVal> cutOptionalMembers(
    StringView name, lib::SharedSlice<VariantVal> value);
[[nodiscard]] lib::SharedSlice<const VariantVal> cutOptionalMembers(
    StringView name, lib::SharedSlice<const VariantVal> value);
[[nodiscard]] bool isValidStructure(StringView name,
                                    const lib::Array<String>& memberNames,
                                    const lib::Array<VariantVal>& values);
[[nodiscard]] PartPtr variantValToValueObj(Token token, const VariantVal& value);
[[nodiscard]] VariantVal partToVariantVal(ConstPartPtr value);

[[nodiscard]] bool checkWsnVersion(const VariableInfo& info, ConstPartPtr part,
                                   StringView version, StringView message,
                                   ExprErrors& errors);
[[nodiscard]] bool checkWsnVersionForFunctionExists(const CProps* prop,
                                                    const VariableInfo& info,
                                                    const Function& func,
                                                    StringView version,
                                                    ExprErrors& errors);

enum class ArgType : std::int32_t {
    Number,
    String,
    Boolean,
    List,
    Expression,
    Status,
    CardInfo,
    NumberOrString,
    NumberOrList,
    Any,
    Flag,
    Step,
    Variant,
    VariantRef,
    NoArgument,
};

struct ArgDef {
    ArgType type{ArgType::Any};
    String name{};
    ArgType initValueType{ArgType::NoArgument};
    String initValue{};
    bool optional{false};
    bool varArg{false};
};

struct FuncDef {
    lib::Array<types::FunctionCategory> category{};
    String name{};
    String desc{};
    String shortDesc{};
    String example{};
    lib::Array<ArgDef> args{};
    ArgType returnType{ArgType::Any};
};

[[nodiscard]] lib::Array<FuncDef> functionDefinitions(const CProps* prop);

/// 式の所有者と、参照数のコールバック先。
class Expression final : public path::CWXPath,
                         public textholder::ISimpleTextHolder,
                         public usecounter::ChgFlagCallback,
                         public usecounter::ChgStepCallback,
                         public usecounter::ChgVariantCallback,
                         public usecounter::ChgCouponCallback,
                         public usecounter::ChgGossipCallback,
                         public std::enable_shared_from_this<Expression> {
public:
    using ChangeHandler = std::function<void()>;

    Expression() = default;
    explicit Expression(String text) { this->text(std::move(text)); }
    ~Expression() override = default;

    [[nodiscard]] lib::Owner<CwxPath> retainForCallback() override {
        return std::static_pointer_cast<CwxPath>(weak_from_this().lock());
    }

    void changeHandler(ChangeHandler change) { changed_ = std::move(change); }
    [[nodiscard]] String text() const override { return text_; }
    void text(std::string_view value) override;
    void changed() override;
    [[nodiscard]] ExprErrors getExpressionErrors(const CProps* prop,
                                                 const VariableInfo& info) const;
    [[nodiscard]] VariantVal returnValue(const CProps* prop) const;

    [[nodiscard]] lib::Array<String> flagsInText() const override;
    [[nodiscard]] lib::Array<String> stepsInText() const override;
    [[nodiscard]] lib::Array<String> variantsInText() const override;
    [[nodiscard]] lib::Array<String> couponsInText() const;
    [[nodiscard]] lib::Array<String> gossipsInText() const;
    [[nodiscard]] lib::Array<String> keyCodesInText() const;

    [[nodiscard]] usecounter::CounterOwner useCounter() const { return uc_; }
    [[nodiscard]] lib::Owner<CwxPath> ucOwner() override { return ucOwner_; }
    [[nodiscard]] std::uint64_t useCounterRevision() const noexcept override { return use_counter_revision_; }
    [[nodiscard]] std::uint64_t useCounterBindingRevision() const noexcept override { return use_counter_binding_revision_; }
    void setUseCounter(const usecounter::CounterOwner& counter,
                       const lib::Owner<CwxPath>& owner) override;
    void removeUseCounter() override;

    void changeInText(std::size_t index, std::string_view id) override;
    void changeFlagInText(std::size_t index, std::string_view id) override;
    void changeStepInText(std::size_t index, std::string_view id) override;
    void changeVariantInText(std::size_t index, std::string_view id) override;
    void changePathInText(std::size_t index, std::string_view id) override;
    void changeInText(std::size_t index, const usecounter::FlagId& id);
    void changeInText(std::size_t index, const usecounter::StepId& id);
    void changeInText(std::size_t index, const usecounter::VariantId& id);
    void changeInText(std::size_t index, const usecounter::CouponId& id);
    void changeInText(std::size_t index, const usecounter::GossipId& id);

    bool changeCallback(const usecounter::FlagId& oldValue,
                        const usecounter::FlagId& newValue) override;
    bool changeCallback(const usecounter::StepId& oldValue,
                        const usecounter::StepId& newValue) override;
    bool changeCallback(const usecounter::VariantId& oldValue,
                        const usecounter::VariantId& newValue) override;
    bool changeCallback(const usecounter::CouponId& oldValue,
                        const usecounter::CouponId& newValue) override;
    bool changeCallback(const usecounter::GossipId& oldValue,
                        const usecounter::GossipId& newValue) override;

    [[nodiscard]] lib::Owner<CwxPath> owner() const {
        return weak_owner_mode_ ? weak_owner_.lock() : owner_;
    }
    void owner(const lib::Owner<CwxPath>& value) {
        weak_owner_mode_ = false; weak_owner_.reset(); owner_ = value;
    }
    // ContentがExpressionを所有し、逆向きは所有しない参照にする。
    void weakOwner(const lib::Owner<CwxPath>& value) {
        owner_.reset(); weak_owner_ = value; weak_owner_mode_ = true;
    }
    [[nodiscard]] String cwxPath(bool id) override;
    [[nodiscard]] lib::Owner<CwxPath> findCWXPath(std::string_view value) override;
    [[nodiscard]] lib::Array<lib::Observer<CwxPath>> cwxChilds() override { return {}; }
    [[nodiscard]] lib::Observer<CwxPath> cwxParent() override;

private:
    void removeTextUseCounter();
    void rebuildUsers();
    template <typename Id>
    bool replaceInExpression(const Id& oldValue, const Id& newValue);

    String text_{};
    lib::Array<lib::Owner<usecounter::FlagUser>> flags_{};
    lib::Array<lib::Owner<usecounter::StepUser>> steps_{};
    lib::Array<lib::Owner<usecounter::VariantUser>> variants_{};
    lib::Array<lib::Owner<usecounter::CouponUser>> coupons_{};
    lib::Array<lib::Owner<usecounter::GossipUser>> gossips_{};
    lib::Array<lib::Owner<usecounter::KeyCodeUser>> keyCodes_{};
    usecounter::CounterOwner uc_{};
    std::uint64_t use_counter_revision_{};
    std::uint64_t use_counter_binding_revision_{};
    lib::Owner<CwxPath> ucOwner_{};
    ChangeHandler changed_{};
    PartList expr_{};
    lib::Owner<CwxPath> owner_{};
    lib::Observer<CwxPath> weak_owner_{};
    bool weak_owner_mode_{};
    // 再解析後にUndoが保持するUserも含め、Userはこの寿命トーク
    // ンを弱参照する。 トークン自体はExpressionを所有しない。
    lib::Owner<CwxPath> user_owner_{usecounter::detail::nonOwning<CwxPath>(this)};
};

[[nodiscard]] String variantValueToText(const flag::Variant& value);
[[nodiscard]] String variantValueToText(const VariantVal& value);
[[nodiscard]] String variantValueToPreviewText(const flag::Variant& value);
[[nodiscard]] String variantValueToPreviewText(const VariantVal& value);
[[nodiscard]] String variantValueToTextImpl(types::VariantType type, double numVal,
                                            StringView strVal, bool boolVal,
                                            const lib::Array<VariantVal>& listVal,
                                            StringView structName,
                                            const lib::Array<VariantVal>& structVal);
[[nodiscard]] String variantValueToPreviewTextImpl(types::VariantType type,
                                                   double numVal, StringView strVal,
                                                   bool boolVal,
                                                   const lib::Array<VariantVal>& listVal,
                                                   StringView structName,
                                                   const lib::Array<VariantVal>& structVal);
[[nodiscard]] VariantVal variantValueFromText(StringView text);

// Dのすべての関数の入口を、同じ具体的な呼び出し形
// 式へ揃える。 宣言はEditorとのソース互換用
// に残し、実装はexpression.cppの
// 評価済み引数を受け取る一つの関数表へ集約する。
#define CWX_EXPRESSION_FUNCTIONS(X) \
    X(Max) X(Min) X(Len) X(Find) X(Left) X(Right) X(Mid) X(Str) X(Value) X(Int) \
    X(IsStr) X(IsNumber) X(IsList) X(Var) X(FlagValue) X(FlagText) X(StepValue) \
    X(StepText) X(StepMax) X(Dice) X(Selected) X(LatestUseCardOwner) X(CharExists) \
    X(CastType) X(CastName) X(FindCoupon) X(CouponText) X(FindGossip) X(GossipText) \
    X(PartyName) X(List) X(List_e) X(At) X(LLen) X(LFind) X(LLeft) X(LRight) X(LMid) \
    X(PartyMoney) X(PartyNumber) X(YadoName) X(SkinType) X(BattleRound) X(CastLevel) \
    X(CouponValue) X(LifeRatio) X(StatusValue) X(StatusRound) X(SelectedCard) \
    X(CardName) X(CardType) X(CardRarity) X(CardPrice) X(CardLevel) X(CardCount) \
    X(FindKeyCode) X(KeyCodeText) X(Floor) X(Ceil) X(Sum) X(Eval) X(Format) X(Sind) \
    X(Cosd) X(Tand) X(Distance) X(ParseFloat) X(ParseInt) X(ScenarioName) \
    X(CardMaxUseLimit) X(Datetime) X(Unixtime) X(GetVariantValueOfCard) \
    X(GetFlagValueOfCard) X(GetStepValueOfCard) X(HasVariantValue) X(HasStep) X(HasFlag) \
    X(SetVariantValueOfCard) X(SetStepValueOfCard) X(SetFlagValueOfCard) X(MouseX) X(MouseY) \
    X(MakeList) X(LFindRand) X(GetRandomList) X(ConcatStrings) X(PickByIndices) \
    X(InPolygon) X(LGetRandom) X(LSetMulti) X(LRemove) X(LRemoveMulti) X(LInsert) \
    X(LUnique) X(LRemoveAll) X(LCount) X(LSet) X(Contain)

#define CWX_DECLARE_EXPRESSION_FUNCTION(name) \
    [[nodiscard]] ConstPartPtr func##name(const CProps* prop, EvalMode mode, \
                                          const VariableInfo& info, const Function& func, \
                                          ConstPartView args, ExprErrors& errors);
CWX_EXPRESSION_FUNCTIONS(CWX_DECLARE_EXPRESSION_FUNCTION)
#undef CWX_DECLARE_EXPRESSION_FUNCTION
#undef CWX_EXPRESSION_FUNCTIONS

}  // namespace expression
}  // namespace cwxeditor::cwx
